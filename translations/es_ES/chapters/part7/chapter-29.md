---
title: "Portabilidad y abstracción de drivers"
description: "Creación de drivers portables entre distintas arquitecturas de FreeBSD"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 29
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 270
language: "es-ES"
---
# Portabilidad y abstracción en drivers

## Introducción

Llegas al Capítulo 29 con un tipo particular de experiencia. Has escrito tres drivers que, a primera vista, parecen muy distintos entre sí. En el Capítulo 26 conectaste un dispositivo de caracteres a `cdevsw` y enseñaste a `/dev` cómo llegar a tu código. En el Capítulo 27 alimentaste bloques a GEOM y dejaste que un sistema de archivos reposara sobre él. En el Capítulo 28 moldeaste una interfaz de red, la registraste en la pila y observaste cómo fluían paquetes en ambas direcciones. Cada uno de esos drivers fue diseñado para encajar en un subsistema concreto, y cada uno te enseñó un contrato específico con el kernel de FreeBSD. Si los lees en paralelo, notarás que también comparten muchas cosas: un softc, un camino de probe y attach, asignación de recursos, desmontaje, una gestión cuidadosa de la concurrencia y atención al ciclo de vida de carga y descarga.

Este capítulo trata de ese esqueleto compartido. Y, más importante aún, trata de lo que ocurre con ese esqueleto cuando el resto del mundo cambia a su alrededor. La máquina en la que construiste tu driver hoy difícilmente será la única que lo ejecute alguna vez. Un driver escrito para una estación de trabajo x86 en 2026 puede que se compile para un servidor ARM en 2027, para una placa embebida RISC-V en 2028 y para una versión FreeBSD 15 en 2029. Por el camino, el hardware con el que habla puede cambiar de una tarjeta PCI a un dongle USB o a un dispositivo simulado en `bhyve`. La API del kernel puede evolucionar. El sistema en el que se integra puede cambiar de FreeBSD a NetBSD para algún consumidor posterior. El código que empezó siendo pequeño y con un único propósito tiene que convertirse en algo que sobreviva esa turbulencia con la menor cirugía posible.

Esa supervivencia es lo que entendemos por **portabilidad**. La portabilidad no es una sola propiedad. Es una pequeña familia de propiedades relacionadas. Incluye la capacidad de compilarse en una arquitectura de CPU que no era el objetivo original. Incluye la capacidad de conectarse a un bus físico diferente, como PCI un día y USB otro. Incluye la capacidad de alternar entre un dispositivo real y un sustituto simulado durante las pruebas sin reescribir la mitad del archivo. Incluye la capacidad de convivir con los cambios en la API del kernel, de modo que un cambio de nombre o una obsolescencia en una versión futura de FreeBSD no te obligue a tirar el driver. Y, ocasionalmente, incluye la capacidad de compartir código con los primos de FreeBSD, como NetBSD y OpenBSD, sin convertir toda la base de código en un laberinto de compatibilidad.

El Capítulo 29 te enseña a diseñar, estructurar y refactorizar un driver para que cada uno de esos tipos de cambio sea un problema local en lugar de una reescritura global. El capítulo no trata sobre un nuevo subsistema. Ya los conoces todos. Trata de la disciplina de ingeniería que permite a un driver envejecer con elegancia. Es la parte del desarrollo de un driver que no se manifiesta el primer día, cuando el módulo carga limpiamente y el dispositivo responde, pero que importa enormemente en el día mil, cuando ese mismo módulo tiene que funcionar en tres arquitecturas, dos buses y cuatro versiones de FreeBSD sin dejar de ser legible para nuevos colaboradores.

No vamos a inventar un nuevo driver desde cero. En cambio, tomaremos una forma de driver que ya te resulta familiar y la haremos evolucionar. Aprenderás a separar el código dependiente del hardware de la lógica que no depende de él. Aprenderás a ocultar un bus físico detrás de una interfaz de backend, de modo que una variante PCI y una variante USB puedan compartir el mismo núcleo. Aprenderás a dividir un único archivo `.c` en una pequeña familia de archivos, cada uno con una responsabilidad clara. Aprenderás a usar los helpers de endianness del kernel, los tipos de ancho fijo y las abstracciones `bus_space`/`bus_dma` para que los accesos a registros funcionen tanto en máquinas little-endian como big-endian, y tanto en tamaños de palabra de 32 bits como de 64 bits. Aprenderás a usar la compilación condicional sin convertir el código fuente en un matorral de `#ifdef`. Y aprenderás a dar un paso atrás y versionar el driver para que los usuarios de tu código sepan qué soporta y qué no.

A lo largo de todo esto, el tono sigue siendo el mismo que en los capítulos anteriores. La portabilidad parece abstracta cuando lees sobre ella en un manual, pero es concreta cuando te sientas frente a un archivo de código fuente de un driver y decides qué función va dónde. Nos mantendremos cerca de esa segunda perspectiva. Cada patrón que encuentres estará ligado a una práctica real de FreeBSD, y a menudo a un archivo real bajo `/usr/src/` que puedes abrir y leer junto al texto.

Antes de comenzar, resiste la tentación de leer este capítulo como un conjunto de reglas. No lo es. Es un conjunto de **hábitos**. Cada hábito existe porque una versión futura de ti mismo le agradecerá a la versión actual haberlo adquirido. Un driver que nunca tenga que portarse seguirá beneficiándose de haber sido escrito como si pudiera serlo, porque los mismos hábitos que ayudan a la portabilidad también ayudan a la legibilidad, a las pruebas y al mantenimiento a largo plazo. No trates este capítulo como un pulido opcional. Trátalo como la parte del oficio que separa un driver que funciona de un driver que sigue funcionando.

## Guía para el lector: cómo usar este capítulo

Este capítulo es algo diferente de los tres que lo preceden. Los Capítulos 26, 27 y 28 estaban estructurados, cada uno, alrededor de un subsistema concreto con una API concreta. El Capítulo 29 está construido alrededor de una forma de pensar. Seguirás leyendo código y haciendo laboratorios, pero el tema es cómo se **organiza** el código de un driver, no qué función del kernel llama. Esa distinción afecta a la mejor manera de estudiarlo.

Si eliges el **camino de solo lectura**, prevé unas dos o tres horas de concentración. Al terminar, reconocerás los patrones estructurales comunes que usan los drivers maduros de FreeBSD para mantenerse portables y sabrás qué buscar cuando abras un árbol de código fuente de un driver desconocido. Esta es una forma legítima de usar el capítulo en una primera lectura, porque las ideas aquí expuestas se vuelven más útiles una vez que las has visto en acción en tu propio trabajo.

Si eliges el **camino de lectura más laboratorios**, prevé entre cinco y ocho horas repartidas en una o dos sesiones. Tomarás un pequeño driver de referencia y lo refactorizarás progresivamente hasta darle una forma portable con múltiples backends. Dividirás un único archivo en un núcleo y dos archivos de backend, introducirás una interfaz de backend y harás que el driver compile limpiamente tanto con los backends opcionales activados como sin ellos. Los laboratorios son deliberadamente incrementales, de modo que cada paso se sostiene por sí solo y te deja con un módulo funcional en lugar de uno roto.

Si eliges el **camino de lectura más laboratorios más desafíos**, prevé un fin de semana o varias tardes. Los desafíos llevan la refactorización más lejos: añadir un tercer backend, exponer metadatos de versión al espacio de usuario, simular un entorno big-endian y escribir una matriz de build que demuestre que el driver compila en todas las configuraciones documentadas. Cada desafío se sostiene por sí solo y utiliza únicamente material de este capítulo y de los anteriores.

Debes continuar trabajando en una máquina FreeBSD 14.3 prescindible, como en los capítulos anteriores. Los laboratorios no requieren hardware especial, un bus complicado ni una NIC concreta. Funcionan en una máquina virtual básica con el árbol de código fuente instalado, un compilador en funcionamiento y las herramientas habituales de construcción de módulos del kernel. Una instantánea antes de empezar no cuesta nada, y lo único que necesitas para deshacer un error es poder reiniciar.

Una nota sobre los prerrequisitos. Debes sentirte cómodo con todo lo visto en los Capítulos 26, 27 y 28: escribir un módulo del kernel, asignar un softc, gestionar el ciclo de vida de probe y attach, registrarse en `cdevsw`, GEOM o `ifnet` según corresponda, y razonar sobre el camino de carga y descarga. También debes sentirte cómodo con `make` y con el sistema de construcción `bsd.kmod.mk` de FreeBSD, ya que buena parte del capítulo trata sobre cómo dividir y compilar un driver con múltiples archivos. Si algo de eso te resulta inseguro, un repaso rápido de los capítulos anteriores te ahorrará tiempo aquí.

Por último, no te saltes la sección de resolución de problemas al final del capítulo. Las refactorizaciones de portabilidad fallan de unas pocas formas características, y aprender a reconocer esos patrones pronto es más útil que memorizar qué macro vive en qué cabecera. Un driver portable que casi funciona es peor que uno no portable que definitivamente funciona, porque la portabilidad te da una falsa sensación de confianza. Trata el material de resolución de problemas como parte de la lección.

### Trabaja sección por sección

El capítulo está estructurado como una progresión deliberada. La Sección 1 define qué significa realmente la portabilidad para un driver y por qué nos importa. La Sección 2 recorre el proceso de aislar el código dependiente del hardware de la lógica que no lo es. La Sección 3 enseña cómo ocultar backends detrás de una interfaz para que el driver principal no necesite saber si está hablando con una tarjeta PCI o un dongle USB. La Sección 4 organiza esas piezas en una disposición de archivos clara. La Sección 5 aborda la portabilidad arquitectónica: endianness, alineación y tamaño de palabra. La Sección 6 enseña el uso disciplinado de la compilación condicional y la selección de características en tiempo de construcción. La Sección 7 echa un breve vistazo a la compatibilidad entre BSDs, lo suficiente para ser útil sin convertir el capítulo en un manual de portado a NetBSD. La Sección 8 da un paso atrás y aborda la refactorización y el versionado para el mantenimiento a largo plazo.

Se espera que leas estas secciones en orden. Cada una asume que las anteriores están frescas en tu memoria, y cada laboratorio se construye sobre los resultados del anterior.

### Una advertencia amable sobre la sobreingeniería

Antes de entrar en materia, una palabra de precaución. Los patrones de portabilidad pueden convertirse en un fin en sí mismos si no tienes cuidado. Es posible construir un driver con tantas capas, tan abstraído y tan envuelto que nadie, incluido tú, pueda seguir lo que realmente ocurre en un camino de código determinado. El objetivo de este capítulo no es maximizar la abstracción, sino elegir el nivel de abstracción adecuado para el driver en cuestión.

Para un driver pequeño con un único backend y sin perspectiva de un segundo, el diseño portable más sencillo es un único archivo `.c` que use `bus_read_*`, helpers de endianness y tipos de ancho fijo en todo el código. Sin interfaz de backend. Sin división de archivos. La inversión se detiene ahí, y eso es suficiente. La refactorización a una disposición con múltiples backends puede ocurrir más adelante, cuando surja la necesidad.

Para un driver que ya soporta dos o tres variantes, la disposición completa con múltiples archivos se paga sola, y la refactorización para llegar ahí ahorra más tiempo del que cuesta. Para un driver que podría algún día soportar variantes adicionales pero que todavía no las soporta, el punto intermedio es un único archivo con una separación interna limpia (una interfaz de backend como struct, aunque solo se instancie un backend), listo para dividirse en múltiples archivos cuando llegue el segundo backend.

Hacer coincidir la complejidad del diseño con la complejidad del problema es el oficio. Lee este capítulo con eso en mente. No todos los patrones que contiene pertenecen a todos los drivers. Los patrones pertenecen donde se pagan a sí mismos.

### Mantén cerca el driver de referencia

Los laboratorios de este capítulo giran en torno a un pequeño driver de referencia llamado `portdrv`. Lo encontrarás en `examples/part-07/ch29-portability/`, organizado de la misma forma que los ejemplos de los capítulos anteriores. Cada directorio de laboratorio contiene el estado del driver en ese paso, junto con el Makefile y cualquier archivo de soporte necesario para compilarlo y probarlo. No te limites a leer el laboratorio; escribe los cambios, construye el módulo y cárgalo. El ciclo de retroalimentación es rápido porque la refactorización o compila o no compila, y ejecutar el módulo después de cada paso confirma que no rompiste el driver accidentalmente al mover código de un lado a otro.

### Abre el árbol de código fuente de FreeBSD

Varias secciones de este capítulo hacen referencia a drivers reales de FreeBSD como ejemplos concretos de estructura portable. Los archivos de interés son `/usr/src/sys/dev/uart/uart_core.c`, `/usr/src/sys/dev/uart/uart_bus_pci.c`, `/usr/src/sys/dev/uart/uart_bus_fdt.c`, `/usr/src/sys/dev/uart/uart_bus_acpi.c`, `/usr/src/sys/dev/uart/uart_bus.h`, `/usr/src/sys/modules/uart/Makefile`, `/usr/src/sys/sys/_endian.h`, `/usr/src/sys/sys/bus_dma.h`, y las cabeceras de bus por arquitectura ubicadas en `/usr/src/sys/amd64/include/_bus.h`, `/usr/src/sys/arm64/include/_bus.h`, `/usr/src/sys/arm/include/_bus.h` y `/usr/src/sys/riscv/include/_bus.h`. Ábrelos conforme el texto los vaya señalando. Su estructura es ya la mitad de la lección.

### Mantén el diario de laboratorio

Continúa el diario de laboratorio que comenzaste en el Capítulo 26. Para este capítulo, anota una entrada breve al final de cada laboratorio: qué archivos editaste, qué opciones de compilación habilitaste, qué backends compilaron y qué advertencias produjo el compilador. La portabilidad es fácil de proclamar y difícil de verificar, y un registro escrito de tus experimentos es la forma más rápida de detectar cuándo has introducido una regresión sin darte cuenta.

### Márcate un ritmo

Si tu comprensión se nubla durante alguna sección, detente. Vuelve a leer la subsección anterior. Prueba un experimento pequeño, por ejemplo recompila el driver con un backend desactivado y comprueba que sigue construyendo. Las refactorizaciones de portabilidad recompensan los pasos lentos y deliberados. Una división apresurada que deja una función en el archivo equivocado es más difícil de corregir que una tranquila que la coloca correctamente desde el principio.

## Cómo sacar el máximo partido a este capítulo

El Capítulo 29 no es una lista de referencia. Es un taller sobre estructura. La forma más eficaz de utilizarlo es mantener el driver entre manos mientras lees, y preguntarte ante cada patrón que encuentres: ¿qué problema resuelve esto y qué pasaría si ese problema no existiera? Esa pregunta es el camino más rápido para interiorizar los hábitos.

### Refactoriza en pasos pequeños

No intentes pasar de un driver en un único archivo a una arquitectura completamente portable, con múltiples backends y compatible con varios BSDs, en una sola sesión. Los laboratorios están divididos en pasos discretos por una razón. Cada paso introduce exactamente un cambio en el código y confirma que el módulo sigue cargando limpiamente después. Esa disciplina refleja la práctica real. Las refactorizaciones profesionales de drivers rara vez son reescrituras grandes y dramáticas; son una larga secuencia de movimientos pequeños y revisables, cada uno de los cuales puede deshacerse si algo sale mal.

### Lee la salida del compilador

El compilador es tu colaborador durante una refactorización de portabilidad. Un include que falta, un prototipo obsoleto, una función movida a un archivo que no la lista en `SRCS`: todo eso se convierte en errores de compilación en el momento en que construyes, y los mensajes son precisos si los lees con atención. Cuando falle el build, resiste el impulso de adivinar; abre el archivo al que apunta el compilador y corrige el símbolo específico que no encuentra. A lo largo de los laboratorios del capítulo, desarrollarás intuición sobre cómo reacciona el sistema de build ante distintos tipos de reorganización.

### Usa el control de versiones como red de seguridad

Haz un commit antes de cada paso de refactorización. El historial git del proyecto te ofrece una forma de volver a un estado conocido y correcto sin ningún coste. Si una división sale mal, revertir el cambio es un único comando, no una hora de búsqueda para encontrar qué rompiste. No necesitas subir estos commits a ningún lado; son para ti.

### Compara con drivers reales de FreeBSD

Cuando el capítulo señale un archivo real bajo `/usr/src/sys/`, ábrelo. El driver `uart(4)` en particular es un estudio excelente de abstracción por backends: su lógica principal en `uart_core.c` no sabe nada del bus, y sus archivos de backend añaden la conexión por PCI, FDT y ACPI con una pequeña cantidad de código cada uno. Leer esos archivos junto a este capítulo te enseñará lo que significa "bien hecho" con mucha más eficacia que cualquier ejemplo sintético.

### Toma notas sobre patrones, no sobre sintaxis

Los macros y nombres de función concretos de este capítulo son menos importantes que los patrones que hay detrás de ellos. Un interfaz de backend definido como una estructura de punteros a función es un patrón que usarás durante el resto de tu carrera; la ortografía particular de las funciones en `portdrv` es una lección de una sola vez. Escribe los patrones en tu diario. Vuelve a ellos cuando diseñes tus propios drivers.

### Confía en el sistema de build

El `bsd.kmod.mk` de FreeBSD es maduro y hace una gran cantidad de trabajo por ti. Un driver con varios archivos no es significativamente más complicado de construir que uno con un único archivo; añades una línea a `SRCS` y el build sigue funcionando. Apóyate en eso. No escribas tu propia maquinaria de Makefile cuando la existente ya hace el trabajo.

### Tómate descansos

El trabajo de refactorización tiene un coste cognitivo particular. Estás manteniendo en la cabeza dos versiones del código a la vez, la anterior al cambio y la posterior, y la mente se fatiga más rápido que durante el desarrollo de código nuevo. Dos o tres horas de trabajo concentrado suelen ser más productivas que un sprint ininterrumpido de siete horas. Si te notas copiando y pegando sin leer, levántate diez minutos.

Con esos hábitos bien asentados, podemos empezar.

## Sección 1: ¿Qué significa "portable" para un driver de dispositivo?

La portabilidad es una de esas palabras que todo el mundo usa y poca gente concreta. Cuando alguien dice que un driver es portable, puede estar refiriéndose a cualquiera de media docena de cosas distintas. Antes de hablar sobre cómo conseguirla, necesitamos tener claro qué es exactamente lo que pretendemos lograr. Esta sección da ese paso con cuidado, porque todas las secciones posteriores dependen de compartir el mismo vocabulario.

### Una definición operativa

Para este libro, un driver es portable cuando cambiar uno de los siguientes elementos no obliga a cambiar la lógica central del driver:

- la arquitectura de CPU para la que se compila
- el bus a través del cual accede al hardware
- la revisión concreta del hardware detrás del bus
- la versión de FreeBSD en la que se ejecuta, dentro de límites razonables
- el despliegue físico, como hardware real frente a un simulador

Observa que esta definición es negativa. No dice que un driver portable funcione en todas partes sin modificación, porque ningún driver lo hace. Dice que cuando cualquiera de estos elementos cambia, el cambio es **local**. La lógica central permanece donde estaba. El código específico del hardware o de la plataforma absorbe el cambio. Esa localidad es el significado práctico de la portabilidad, y todo lo que se trata en este capítulo trata sobre cómo organizar el código para que el cambio sea local.

Un driver que no es portable suele revelarse de tres maneras. Primera, un pequeño cambio en el entorno produce diffs dispersos por todo el archivo. Segunda, el autor tiene que leer el archivo entero para saber dónde hacer un cambio, porque la información sobre el bus o la arquitectura está esparcida por toda la lógica. Tercera, añadir soporte para un nuevo backend requiere duplicar el driver entero o atravesar cada función con bloques `#ifdef`. Si alguna vez has intentado dar soporte a una segunda variante de un dispositivo de esa manera, ya sabes lo frágil que se vuelve el resultado.

### Portabilidad entre arquitecturas

FreeBSD funciona en una gran variedad de arquitecturas de CPU, y todas ellas pueden cargar módulos del kernel. Las que encontrarás con más probabilidad en la práctica son `amd64` (la familia x86 de 64 bits), `i386` (x86 de 32 bits heredado), `arm64` (la familia ARM de 64 bits), `armv7` (la familia ARM de 32 bits), `riscv` (tanto en 32 como en 64 bits) y `powerpc` y `powerpc64` para sistemas PowerPC de la generación anterior y de clase servidor. Un driver portable entre arquitecturas compila y funciona correctamente en todas ellas sin que el autor tenga que saber nada específico sobre, por ejemplo, las convenciones de llamada de `amd64` o las reglas de alineación de `arm64`.

Las arquitecturas de CPU difieren en varios aspectos que el autor de un driver debe tomar en serio. Difieren en **endianness** (orden de bytes): la mayoría de las habituales hoy son little-endian, pero `powerpc` y `powerpc64` pueden configurarse como big-endian y lo son históricamente. Difieren en **tamaño de palabra**: los sistemas de 32 y 64 bits tienen anchuras de entero nativas distintas, lo que importa cuando guardas una dirección en una variable. Difieren en **alineación**: `amd64` tolera un acceso de 32 bits no alineado, pero `arm` en algunos núcleos lanzará un fallo. Difieren en la disposición física de la memoria y en el comportamiento de las barreras de memoria, aunque esos son problemas que las primitivas de sincronización del kernel ya resuelven por ti si las usas correctamente.

Un driver escrito sin cuidado no compilará en absoluto en algunas de estas arquitecturas, o peor aún, compilará pero se comportará mal. Un driver escrito con un poco de cuidado compila limpiamente en todas partes y hace lo mismo en todas partes. La diferencia entre esos dos resultados es casi siempre cuestión de usar los tipos correctos, los helpers de endianness correctos y las funciones de acceso correctas, no de un conocimiento profundo del kernel.

### Portabilidad entre buses

El segundo eje de la portabilidad es el bus a través del cual el driver se comunica con el hardware. El driver clásico de FreeBSD habla con un dispositivo PCI. Pero la misma función hardware puede aparecer detrás de otros buses también: por USB, por una región de memoria mapeada específica de la plataforma conectada mediante Device Tree (FDT) en sistemas embebidos, por una interfaz Low Pin Count o SMBus, o por nada en absoluto cuando el dispositivo se simula por software. El árbol `/usr/src/sys/dev/uart/` es un ejemplo magnífico: `uart_bus_pci.c`, `uart_bus_fdt.c`, `uart_bus_acpi.c` y varios archivos más pequeños enseñan al núcleo UART compartido cómo ser alcanzado a través de un bus concreto, y el propio núcleo, en `uart_core.c`, no le importa cuál se usó.

Cuando un driver es portable entre buses, añadir un bus nuevo es cuestión de escribir un archivo pequeño que traduzca entre la API del bus y la API interna del driver, y luego listar ese archivo en el sistema de build del kernel. El núcleo no necesita cambiar. Si hoy tienes una variante basada en PCI de un dispositivo y mañana el mismo silicio aparece soldado a una placa, tu driver incorpora la nueva conexión en unas pocas centenas de líneas en lugar de unos pocos miles.

### Portabilidad entre sistemas

El tercer eje de la portabilidad es la compatibilidad entre sistemas: ¿puede compartirse tu driver con otros sistemas operativos, especialmente los otros BSDs? Esta es la forma de portabilidad que menos importa para la mayoría de los proyectos exclusivos de FreeBSD y la que más esfuerzo consume cuando sí importa, razón por la cual la trataremos brevemente en este capítulo. La respuesta corta es que NetBSD y OpenBSD comparten una gran historia con FreeBSD, y un driver escrito con cuidado en el dialecto de FreeBSD a menudo puede traducirse a esos sistemas con una cantidad modesta de trabajo. Un driver escrito sin cuidado, con suposiciones de FreeBSD dispersas por todas partes, habitualmente no puede traducirse sin una reescritura importante.

A nuestros efectos, la compatibilidad entre BSDs significa escribir el driver de forma que las convenciones específicas de FreeBSD se introduzcan en un número reducido de lugares bien conocidos y puedan envolvirse o sustituirse cuando sea necesario. Verás ese patrón en la Sección 7.

### Portabilidad a lo largo del tiempo

Una cuarta forma de portabilidad es más sutil pero importa enormemente durante la vida de un proyecto: la portabilidad a lo largo del tiempo. FreeBSD es un sistema vivo. Las APIs evolucionan, los macros se renombran y las convenciones antiguas dejan paso a las nuevas. El manejador opaco `if_t` para interfaces de red, por ejemplo, reemplazó al puntero directo `struct ifnet *` a lo largo de FreeBSD 13 y 14; los drivers que usaban la API opaca siguieron compilando, mientras que los que usaban la estructura directa tuvieron que actualizarse. Un driver cuidadoso con las APIs que utiliza, las cabeceras que incluye y cómo detecta la versión del kernel para el que se compila, sobrevivirá a esas evoluciones con poco más que una comprobación de versión al comienzo de unos pocos archivos.

Enseguida conocerás el macro `__FreeBSD_version`. Considéralo un sello de versión que el propio kernel lleva consigo y que tu driver puede comprobar para adaptarse a la superficie de API de la versión concreta para la que se construye. Usado con moderación, es una herramienta discreta y eficaz. Usado en exceso, convierte el código fuente en un mosaico de parches.

### Portabilidad entre despliegues

Una última forma de portabilidad, a menudo olvidada, es la portabilidad entre hardware real y un simulador. En el Capítulo 17 aprendiste lo valioso que es poder probar un driver sin el dispositivo real delante. Un driver cuyo backend puede cambiarse en tiempo de compilación entre "el dispositivo real" y "un sustituto simulado" es mucho más fácil de desarrollar, revisar y probar que uno que solo puede ejecutarse contra el hardware verdadero. Esta forma de portabilidad es la más barata de lograr, porque solo cuesta un pequeño diseño inicial, y se paga a sí misma cada vez que se detecta un error en un runner de integración continua que no dispone del hardware.

### Por qué importa la portabilidad

Es tentador tratar la portabilidad como algo deseable pero opcional, como algo que abordarás «más adelante» cuando el driver se estabilice. En la práctica, la portabilidad es más barata cuando se diseña desde el principio y más dolorosa cuando se incorpora a posteriori, una vez que el driver ha crecido hasta las diez mil líneas. Hay tres razones concretas por las que la portabilidad merece ese esfuerzo.

La primera es que el hardware varía. El dispositivo que soportas hoy raramente es el único que tendrás que soportar en tu vida. Los fabricantes de chips producen variantes. Un sensor que funcionaba sobre I2C el año pasado puede aparecer sobre USB este año y sobre PCIe el siguiente, con el mismo modelo de programación en su base. Si tu lógica principal es independiente del hardware, esas variantes son baratas de soportar. Si no lo es, son caras.

La segunda es que las plataformas evolucionan. Un driver que se distribuye en `amd64` este año puede necesitar ejecutarse en `arm64` el siguiente, a medida que crecen los despliegues en sistemas embebidos. Un driver que asume memoria little-endian se comportará mal en un objetivo big-endian, y los errores serán sutiles porque la mayor parte del tiempo el código parece correcto. Organizar el driver de modo que el orden de bytes (endianness) se gestione en un número reducido de lugares evidentes significa que el salto a una nueva arquitectura es una tarea de una tarde, no una investigación forense de un mes.

La tercera es que el tiempo pasa. Un driver de larga vida sobrevive a su autor. Un driver con una estructura limpia puede ser mantenido por alguien que llegue de nuevo sin ningún contexto previo. Un driver que sea un enredo de bloques condicionales y suposiciones sobre el hardware no puede, y tiende a ser reescrito desde cero por el siguiente mantenedor. El coste acumulado de esas reescrituras, medido a lo largo de la vida de un proyecto, es muy superior al coste de haber escrito el driver original con un estilo portable.

### Duplicación de código frente a diseño modular

Antes de continuar, merece la pena nombrar una tentación a la que se enfrenta todo autor de drivers: la duplicación. Cuando aparece una segunda variante de un dispositivo, la forma más rápida de darle soporte es copiar el driver existente y cambiar las partes que difieren. Esto funciona, a corto plazo. También es una de las decisiones más costosas que puede tomar un proyecto, porque cada bug encontrado en una copia debe encontrarse en la otra, cada mejora debe duplicarse, y cada problema de seguridad debe parchearse dos veces. Después de la tercera o cuarta variante, el proyecto se ha vuelto imposible de mantener.

La alternativa es el diseño modular. Las partes que son iguales viven en un único lugar. Las partes que son diferentes viven en archivos pequeños específicos de cada backend. El núcleo se compila una sola vez. Cada backend se compila por separado y se enlaza contra el núcleo. Cuando se corrige un bug en el núcleo, todas las variantes se benefician. Cuando aparece una nueva variante, solo hace falta añadir un archivo pequeño. Este es el enfoque que los drivers de FreeBSD del árbol principal adoptan de forma abrumadora, por las mismas razones.

El capítulo 29 te enseña a reconocer dónde se está colando la duplicación y cómo desmontarla antes de que se calcifique. Esa habilidad te reportará beneficios durante el resto de tu carrera en programación de sistemas.

### Una historia de dos drivers

Antes de cerrar esta sección, es útil comparar dos drivers hipotéticos que un equipo podría escribir para el mismo dispositivo: uno pensado desde el principio con la portabilidad en mente, y otro sin ella. La historia es compuesta, pero cada escenario que contiene ha ocurrido en proyectos reales de FreeBSD, generalmente más de una vez.

El Driver A se escribe deprisa para dar soporte a una única tarjeta PCI en `amd64`. El autor escribe cada acceso a registro como una llamada directa a `bus_read_4(sc->sc_res, offset)`. La sonda PCI, la función attach, el switch de ioctl y el código de volcado de registros conviven en un único archivo de unas mil doscientas líneas. El autor conoce la teoría del endianness, pero asume que el host es little-endian porque el objetivo es `amd64`. El driver se publica, funciona y supera su primera ronda de pruebas con clientes. El equipo se declara victorioso.

Seis meses después, un segundo cliente solicita soporte para una variante USB del mismo dispositivo. El modelo de programación es idéntico; el mecanismo es diferente. El autor abre el Driver A y considera las opciones. La opción uno es añadir un segundo camino de attach y esparcir ramas `if (sc->sc_is_usb)` por todo el archivo; el autor enseguida ve que eso implica tocar más de cien funciones, cada una de las cuales crecería con una nueva rama. La opción dos es copiar el Driver A en un Driver A'-USB y cambiar las partes que difieren. El autor elige la opción dos porque es más rápida. Ahora hay dos drivers. Cada corrección de bug debe aplicarse dos veces, una en cada copia, y tras la tercera corrección el equipo se da cuenta de que ya han olvidado aplicarla a una de ellas. Un impuesto de mantenimiento empieza a acumularse en silencio.

Un año después, un tercer cliente quiere que ese mismo silicio tenga soporte en una placa `arm64`. El driver PCI casi funciona en `arm64`, pero el endianness de un registro concreto estaba mal, y el autor no se había dado cuenta porque nunca había compilado en una plataforma big-endian. El driver USB funciona, pero solo después de que el autor descubre que una estructura a la que había lanzado un puntero `volatile` no estaba alineada en `arm64` y producía fallos de bus. Añade una tercera copia, Driver A''-USB-ARM, con correcciones para la alineación y el endianness. El equipo mantiene ahora tres drivers. La proporción de bugs frente a correcciones ha aumentado. El total de líneas de código se ha triplicado, pero el comportamiento compartido no.

El Driver B lo escribe un equipo diferente al mismo tiempo, para el mismo dispositivo. El autor empieza por trazar la línea entre el código dependiente del hardware y el independiente en la primera hora, antes de escribir ninguna lógica. Crea una interfaz de backend con cuatro operaciones: `attach`, `detach`, `read_reg` y `write_reg`. Implementa primero un backend PCI y escribe el núcleo contra esa interfaz. También implementa un backend de simulación durante el desarrollo, porque es más sencillo probar el núcleo sin necesidad de una tarjeta real. El driver se publica con unas mil ochocientas líneas repartidas en seis archivos.

Seis meses después, llega la solicitud de USB. El autor escribe un archivo `portdrv_usb.c` de unas trescientas líneas, añade una línea al Makefile y lo publica. El núcleo no cambia. El backend USB se revisa de forma aislada, porque no toca ningún otro archivo. Las correcciones de bugs en el núcleo benefician automáticamente a ambos backends.

Un año después llega la solicitud de `arm64`. El autor compila en `arm64`, detecta un aviso del compilador sobre una comparación con signo frente a `uint32_t`, corrige el aviso y publica. El endianness está resuelto porque todo valor multibyte en un límite de registro pasó desde el primer día por `le32toh` o `htole32`. La alineación está resuelta porque el autor usó `bus_read_*` y `memcpy` en lugar de punteros directos. La adaptación a `arm64` es un trabajo de un día, no de un mes.

Dos equipos distintos, el mismo dispositivo, el mismo tiempo, los mismos clientes. Al cabo de dieciocho meses, el Driver A tiene tres copias, dos mil líneas de divergencia entre ellas y un goteo constante de bugs que afectan a una copia y no a las demás. El Driver B tiene una sola copia, una interfaz de backend y un historial de cambios que parece una línea recta. Los equipos que los escribieron valen ahora cantidades muy diferentes para el proyecto.

La moraleja es sencilla, aunque fácil de olvidar: **el camino barato al comienzo de un driver suele ser el camino caro a lo largo de su vida útil**. La portabilidad es una inversión que da frutos, pero solo si se hace pronto. Añadir portabilidad a posteriori a un driver que ya ha crecido hasta varios miles de líneas es posible y a veces necesario, pero lleva más tiempo que escribir el driver desde cero con el estilo portable. Esto no es un argumento a favor del sobrediseño; es un argumento a favor de dar los pequeños pasos correctos en la primera semana, antes de que el driver tenga tiempo de calcificarse.

### El coste de aplazar la portabilidad

El argumento «ya añadiremos portabilidad más adelante» suena razonable y casi siempre está equivocado. Hay tres costes específicos que paga una refactorización tardía y que un diseño temprano no paga.

**El coste de detección.** Los bugs de portabilidad en un driver que nunca se probó de forma portable son invisibles. Esperan latentes hasta el día en que alguien intenta compilar en una nueva plataforma o en una nueva versión. Para entonces, otro trabajo ya depende del driver, y una corrección que toca una suposición fundamental puede propagarse por todo el proyecto. Una inversión temprana en patrones portables significa que los bugs se detectan en la primera semana, no en el tercer año.

**El coste de revisión.** Un driver que mezcla código dependiente del hardware con lógica de núcleo es más difícil de revisar, porque cada cambio es potencialmente un cambio a la abstracción arquitectónica. Los revisores o bien se ralentizan o, con más frecuencia, empiezan a aprobar parches sin entenderlos del todo. La calidad del proyecto decae en silencio. Un driver bien dividido da a los revisores una señal clara: un parche en el núcleo merece escrutinio porque afecta a todos los backends; un parche en un archivo de backend es local y puede aprobarse con mayor rapidez.

**El coste de colaboradores.** El código enmarañado echa para atrás a los nuevos colaboradores. Un driver con una estructura evidente, en el que las nuevas funcionalidades parecen adiciones a un patrón existente, atrae contribuciones. Un driver que es una pared de condiciones anidadas las repele. A lo largo de la vida de un proyecto, esa es la diferencia entre una comunidad saludable y un único mantenedor que lucha por mantenerse al día.

Ninguno de estos costes es visible en la primera semana. Los tres aparecen antes de que termine el primer año. En el tercero ya dominan el proyecto. Por eso la portabilidad, discutida en abstracto, parece opcional y, discutida con proyectos reales en mente, no lo es en absoluto.

### Por qué FreeBSD lo hace más fácil que la mayoría

Una nota final, y relativamente optimista. Entre los kernels de uso generalizado, FreeBSD es uno de los más fáciles de usar como objetivo para la portabilidad, porque el proyecto ha tomado en serio las abstracciones portables desde el principio. `bus_space(9)` existe para que los autores de drivers no tengan que escribir accesos a registro específicos de cada arquitectura. `bus_dma(9)` existe para que DMA se comporte de forma idéntica en todas las plataformas. Newbus existe para que la lógica de sondeo específica del bus sea separable de la lógica principal del driver. Los helpers de endianness en `/usr/src/sys/sys/endian.h` existen para que una única expresión funcione tanto en hosts little-endian como big-endian. Los tipos de anchura fija en `/usr/src/sys/sys/types.h` existen para que nunca importe cuántos bits tiene un int en esa plataforma.

Compara esto con proyectos que crecieron orgánicamente a través de arquitecturas sin un plan, y encontrarás drivers plagados de `#ifdef CONFIG_ARM`, con casts de punteros directos a la memoria del dispositivo y con código ad hoc de inversión de bytes incluido directamente en cada acceso a registro. FreeBSD podría haber acabado así y no lo hizo, porque los mantenedores principales se preocuparon por la portabilidad desde el principio. Tus drivers pueden heredar esa disciplina de forma gratuita; el coste es aprender qué abstracciones usar y usarlas de forma coherente.

Este es el espíritu con el que está escrito el resto del capítulo. Las herramientas están en su lugar. Los patrones son claros. La inversión es pequeña si se hace pronto. Comencemos.

### Cerrando la sección 1

Ahora tienes una definición funcional de portabilidad y un conjunto de ejes a lo largo de los cuales varía: arquitectura, bus, sistema operativo, tiempo y despliegue. También tienes una idea de por qué la portabilidad importa como preocupación práctica de ingeniería, no como una sutileza teórica, ilustrada por el contraste entre un driver descuidado y uno cuidadoso a lo largo de dieciocho meses. Las siguientes secciones pasan de la definición al método. En la Sección 2 comenzamos el trabajo práctico de portabilidad aislando las partes de un driver que dependen de los detalles del hardware de las partes que no. Esa distinción es el primer movimiento estructural que hace todo driver portable, y es el movimiento que reporta mayores beneficios con el menor coste.

## Sección 2: Aislamiento del código dependiente del hardware

El primer movimiento concreto hacia un driver portable es separar el código que depende de detalles específicos del hardware del código que no depende de ellos. Esto suena obvio cuando se enuncia en abstracto, y es sorprendentemente fácil equivocarse en la práctica. El código dependiente del hardware tiende a filtrarse a lugares inesperados, porque el camino desde un dato hasta el hardware es largo, y cada paso a lo largo de ese camino tiene el potencial de codificar una suposición que debería haber quedado en otro sitio.

Esta sección introduce la idea en términos concretos, te muestra los tipos de código que cuentan como dependientes del hardware, y luego recorre los dos mecanismos más comunes que FreeBSD te proporciona para situar ese código detrás de una abstracción: la familia de macros `bus_space(9)` para el acceso a registros, y la familia `bus_dma(9)` para los buffers que el dispositivo lee y escribe directamente. Al final de esta sección, reconocerás el patrón de aislamiento de hardware cuando lo veas en drivers reales y sabrás cómo aplicarlo al tuyo.

### ¿Qué cuenta como código dependiente del hardware?

Seamos precisos sobre lo que queremos decir. El código dependiente del hardware es aquel cuya corrección depende de hechos específicos de un determinado silicio o del bus en el que reside. Los ejemplos clásicos incluyen:

- el desplazamiento numérico de un registro dentro de la región de memoria mapeada del dispositivo
- la disposición exacta de bits en una palabra de estado
- el tamaño en bytes de una FIFO
- la secuencia requerida de escrituras en registros para inicializar el dispositivo
- el endianness en que el dispositivo espera recibir los valores multibyte
- las restricciones de alineación de los buffers DMA para un motor DMA concreto

Ten en cuenta que ninguno de estos hechos es aplicable a todos los dispositivos que tu driver podría llegar a gestionar. Si mañana el fabricante de hardware lanza una revisión con una profundidad de FIFO distinta, cada línea que asumía la profundidad anterior deberá cambiar. Si el mismo silicio aparece sobre USB en lugar de PCI, cada llamada a `bus_space_read_*` deberá convertirse en otra cosa. Esos son los puntos de unión donde la portabilidad se sostiene o se rompe.

Por el contrario, el código es **independiente** del hardware cuando trabaja con los datos una vez que han salido del control del dispositivo. Una función que recorre una cola de peticiones pendientes, asigna números de secuencia, gestiona timeouts o coordina con la mitad superior del kernel (como GEOM, `ifnet` o `cdevsw`) no necesita saber si los datos llegaron a través de PCI, USB o un simulador. Siempre que la capa dependiente del hardware haya elevado los datos a una representación limpia y uniforme, al resto del driver no le importará cómo llegaron.

El objetivo, entonces, es trazar una línea divisoria a través del driver. Por encima de esa línea vive la lógica independiente del hardware. Por debajo de ella vive todo lo que conoce los registros, el DMA o las APIs específicas del bus. Cuanto más arriba pueda trazarse esa línea sin distorsionar el código, más portable será el driver.

### Un pequeño experimento mental

Antes de introducir las herramientas, prueba con un experimento mental. Imagina un driver para un dispositivo inventado llamado «widget» que realiza operaciones de I/O sencillas. El dispositivo tiene dos registros: un registro de control en el offset 0x00 y un registro de datos en el offset 0x04. El driver escribe un byte en el registro de datos, activa un bit en el registro de control para iniciar una transferencia, sondea otro bit en el registro de control hasta que se limpia y, después, lee un estado del registro de datos.

Ahora imagina que el mismo widget se fabrica en dos formas físicas. Una es una tarjeta PCI en la que ambos registros se acceden a través de un BAR mapeado en memoria. La otra es un dongle USB en el que ambos registros se acceden mediante transferencias de control USB. El modelo de programación es idéntico, pero el mecanismo para leer y escribir los registros es completamente diferente.

¿Cómo escribes el núcleo del driver, la parte que realiza las transferencias y sondea la finalización, de forma que funcione con ambas formas?

La respuesta, que guía el resto de esta sección, es dejar de escribir accesos a registros de forma directa. En su lugar, escribe accesores. La lógica central llama a `widget_read_reg(sc, offset)` y a `widget_write_reg(sc, offset, value)`. Esos accesores son específicos del backend: el backend PCI los mapea a `bus_space_read_4` y `bus_space_write_4`, y el backend USB los mapea a transferencias de control USB. La lógica central no sabe, ni necesita saber, qué backend está en uso.

Este es el movimiento fundamental del aislamiento de hardware, y escala bien. Una vez que tu driver lee y escribe registros únicamente a través de accesores, cambiar el backend se convierte en un cambio local. ¿Añadir un tercer backend para un dispositivo simulado? Escribe un tercer conjunto de accesores que lean y escriban en un buffer en memoria. Nada más en el driver cambia.

### Una mirada más detallada al driver uart

Antes de llegar a las primitivas de FreeBSD, resulta útil trazar la forma de un driver real y maduro para que puedas ver las ideas en su contexto. Abre `/usr/src/sys/dev/uart/uart_core.c` y desplázate con calma. Verás que el archivo contiene la máquina de estados de un UART: la integración con TTY, las rutas de transmisión y recepción, el enrutamiento de interrupciones y los hooks del ciclo de vida. No encontrarás `bus_space_read_4` en ningún lugar de este archivo (o más bien, lo encontrarás solo en helpers que a su vez están abstraídos detrás de métodos específicos de clase), y no encontrarás ninguna mención a PCI, FDT ni ACPI. El núcleo no sabe en qué bus está conectado el UART. Solo conoce el modelo de programación de un UART.

Ahora abre `/usr/src/sys/dev/uart/uart_bus_pci.c`. Este archivo es breve. Declara una tabla de IDs de fabricante y dispositivo, implementa un probe PCI que compara con dicha tabla, implementa un attach PCI que asigna un BAR y lo conecta a la clase, y registra el driver con Newbus mediante `DRIVER_MODULE`. Es el backend PCI del UART. Compáralo con `/usr/src/sys/dev/uart/uart_bus_fdt.c`, que hace el mismo trabajo para plataformas Device Tree, y con `/usr/src/sys/dev/uart/uart_bus_acpi.c` para plataformas ACPI.

Por último, abre `/usr/src/sys/dev/uart/uart_bus.h`. Esta es la cabecera que une las piezas. Define `struct uart_class`, que es esencialmente la interfaz de backend del UART: un conjunto de punteros a función más algunos campos de configuración. Cada variante hardware del UART proporciona una instancia de `struct uart_class`. El núcleo invoca las funciones a través de la clase en lugar de llamar directamente a ninguna variante.

Este es el mismo patrón que construiremos en la Sección 3, solo que en el dialecto específico que FreeBSD usa para los UARTs. Leer el driver del UART junto a este capítulo consolidará las ideas mucho mejor que leer el capítulo por sí solo. Mantén `/usr/src/sys/dev/uart/uart_bus.h` abierto en una segunda ventana mientras lees el resto de la Sección 2 y toda la Sección 3.

### La abstracción `bus_space(9)`

FreeBSD ya te proporciona una herramienta que resuelve este problema para dispositivos mapeados en memoria y con puertos de I/O: la API `bus_space(9)`. Ya la has encontrado en el Capítulo 16, así que esto es un repaso más que una introducción nueva. El propósito de `bus_space` es permitirte escribir un único acceso a registros que funcione correctamente con independencia de la arquitectura del CPU, de si el dispositivo se accede mediante I/O mapeado en memoria o a través de puertos de I/O, y de la topología de bus específica de la máquina.

En el nivel del modelo de programación, mantienes dos valores opacos por dispositivo: un `bus_space_tag_t` y un `bus_space_handle_t`. Una vez que los tienes, accedes al dispositivo a través de llamadas como:

```c
uint32_t value = bus_space_read_4(sc->sc_tag, sc->sc_handle, REG_CONTROL);
bus_space_write_4(sc->sc_tag, sc->sc_handle, REG_CONTROL, value | CTRL_START);
```

En `amd64`, el tag y el handle son simples enteros. Puedes confirmarlo abriendo `/usr/src/sys/amd64/include/_bus.h`, donde ambos están definidos como `uint64_t`. En `arm64`, el tag es un puntero a una estructura de punteros a función y el handle es un `u_long`. Puedes verlo en `/usr/src/sys/arm64/include/_bus.h`. La familia ARM necesita la indirección porque distintas plataformas dentro de ARM pueden mapear la memoria de forma diferente, y un despacho mediante punteros a función las maneja a todas de forma uniforme.

Esta diferencia arquitectónica es fundamental para la portabilidad. Si evitas `bus_space` y usas un puntero `volatile uint32_t *` directo a la memoria del dispositivo, tu código funcionará en `amd64` y fallará en silencio o de forma visible en algunas plataformas ARM. Si usas `bus_space`, el mismo código funciona en ambas. Esa es la ganancia de portabilidad más importante que puedes obtener de este capítulo con el menor esfuerzo.

Las funciones `bus_space` forman una familia que varía a lo largo de tres ejes. Primero, la anchura: `bus_space_read_1`, `bus_space_read_2`, `bus_space_read_4` y, en arquitecturas de 64 bits, `bus_space_read_8`. Segundo, la dirección: `read` o `write`. Tercero, la multiplicidad: la variante simple accede a un único valor, las variantes `_multi_` acceden a un buffer de valores y las variantes `_region_` acceden a una región contigua con un incremento implícito. Para la mayoría de los drivers, las variantes de un único valor son el caballo de batalla, y recurrirás a las demás cuando tengas FIFOs o buffers de paquetes que mover en bloque.

Puedes confirmar la presencia de estas funciones abriendo `/usr/src/sys/sys/bus.h`. La cabecera contiene una larga serie de definiciones de macros para `bus_space_read_1`, `bus_space_write_1`, `bus_space_read_2` y así sucesivamente, cada una definida en términos de las primitivas específicas de la arquitectura.

### Envolver `bus_space` para mayor claridad

Incluso cuando estás decidido a usar `bus_space`, deberías dar un paso más y envolver las llamadas en accesores específicos del driver. ¿Por qué? Porque `bus_space_read_4(sc->sc_tag, sc->sc_handle, REG_CONTROL)` sin envolver aparece docenas de veces en un driver en crecimiento, y cada aparición son tres argumentos de ruido visual cuando lo único que importa es el offset y el valor. Y lo que es más importante, la llamada directa codifica de forma rígida la idea de que el dispositivo se accede a través de `bus_space`. Si alguna vez quieres añadir un backend diferente, cada una de esas llamadas debe cambiar.

Un envoltorio local del driver te da tanto un punto de llamada más limpio como un único lugar donde hacer cambios. El envoltorio tiene este aspecto:

```c
static inline uint32_t
widget_read_reg(struct widget_softc *sc, bus_size_t off)
{
	return (bus_space_read_4(sc->sc_btag, sc->sc_bhandle, off));
}

static inline void
widget_write_reg(struct widget_softc *sc, bus_size_t off, uint32_t val)
{
	bus_space_write_4(sc->sc_btag, sc->sc_bhandle, off, val);
}
```

Ahora el resto del driver llama a `widget_read_reg(sc, REG_CONTROL)` y a `widget_write_reg(sc, REG_CONTROL, val)`. Los puntos de llamada se leen como especificaciones de intención en lugar de como fontanería. Y cuando añadas un backend USB en la siguiente sección, puedes cambiar el cuerpo de estas dos funciones para despachar según el tipo de backend sin tocar ninguno de los cientos de llamadores.

Este patrón, *envuelve la primitiva, luego llama al envoltorio*, es la primera y más común herramienta de portabilidad de drivers. Conviértelo en un hábito. Siempre que te encuentres escribiendo `bus_space_read_*` o `bus_space_write_*` en un archivo que no sea el específico del backend, detente y pregúntate si un envoltorio te serviría mejor.

### Acceso condicional a registros por bus

Una vez que los envoltorios están en su lugar, la siguiente pregunta es qué hacer cuando el mismo driver debe admitir múltiples buses. Hay dos enfoques comunes.

El primer enfoque es la **selección en tiempo de compilación**. El driver se construye una vez por bus y los envoltorios se implementan de forma diferente en cada build. Este es el enfoque que `uart(4)` adopta en algunos contextos. Cada archivo de backend de bus construye su propio conjunto de helpers de attach y llama a la API del núcleo. Los envoltorios no necesitan despacho en tiempo de ejecución porque cada backend se compila con su propia especialización. Este enfoque produce el binario más pequeño y rápido, pero requiere construir el driver una vez por cada backend de bus que quieras admitir.

El segundo enfoque es el **despacho en tiempo de ejecución**. El driver se construye una vez e incluye soporte para todos los backends habilitados. En el momento del attach, el driver detecta qué backend está realmente en uso y almacena un puntero a función en el softc. Cada llamada a través del envoltorio tiene el coste de una indirección. Este enfoque es ligeramente más flexible a costa de una pequeña sobrecarga en tiempo de ejecución.

Para la mayoría de los drivers, especialmente los de nivel principiante, el enfoque más claro es un híbrido: construir el núcleo una vez, construir cada backend como un archivo fuente compilado por separado y usar una pequeña tabla de backend por instancia que el núcleo usa para despachar. Los envoltorios son funciones inline que leen la tabla de backend y llaman a través de ella. Verás exactamente este patrón en la Sección 3 cuando introduzcamos formalmente la interfaz de backend.

### La abstracción `bus_dma(9)`

Para los dispositivos que realizan acceso directo a memoria (DMA) para transferir datos entre la memoria del sistema y el dispositivo, la abstracción análoga es `bus_dma(9)`. El DMA depende del hardware de formas que el acceso a registros no lo hace: la dirección física que ve el dispositivo no siempre coincide con la dirección virtual del kernel que maneja tu código, los requisitos de alineación varían según el dispositivo y el bus, y algunas arquitecturas necesitan operaciones explícitas de vaciado o invalidación de caché entre el CPU y el motor de DMA para mantener la coherencia.

Abre `/usr/src/sys/sys/bus_dma.h` y observa la API principal. Sin entrar en toda la profundidad de `bus_dma`, la forma de la interfaz es la siguiente:

```c
int bus_dma_tag_create(bus_dma_tag_t parent, bus_size_t alignment,
    bus_addr_t boundary, bus_addr_t lowaddr, bus_addr_t highaddr,
    bus_dma_filter_t *filtfunc, void *filtfuncarg,
    bus_size_t maxsize, int nsegments, bus_size_t maxsegsz,
    int flags, bus_dma_lock_t *lockfunc, void *lockfuncarg,
    bus_dma_tag_t *dmat);

int bus_dmamem_alloc(bus_dma_tag_t dmat, void **vaddr, int flags,
    bus_dmamap_t *mapp);

int bus_dmamap_load(bus_dma_tag_t dmat, bus_dmamap_t map, void *buf,
    bus_size_t buflen, bus_dmamap_callback_t *callback,
    void *callback_arg, int flags);

void bus_dmamap_sync(bus_dma_tag_t dmat, bus_dmamap_t dmamap,
    bus_dmasync_op_t op);

void bus_dmamap_unload(bus_dma_tag_t dmat, bus_dmamap_t dmamap);
void bus_dmamem_free(bus_dma_tag_t dmat, void *vaddr, bus_dmamap_t map);
```

La mecánica completa de `bus_dma` se trata en profundidad en capítulos posteriores. Para los propósitos de este capítulo, solo necesitas la consecuencia de portabilidad: **usa `bus_dma` para todos los buffers visibles por el dispositivo**, y oculta su uso detrás de helpers locales del driver, igual que hiciste con el acceso a registros. El motivo es idéntico. Una llamada directa a `bus_dmamap_load` codifica la suposición de que estás usando `bus_dma` y dificulta sustituir un buffer en memoria para pruebas simuladas, o un framework de DMA diferente para una plataforma poco habitual. Una función de envoltorio como `widget_dma_load(sc, buf, len)` oculta esa suposición.

La naturaleza basada en callbacks de `bus_dmamap_load` es en sí misma una característica de portabilidad sobre la que merece la pena detenerse. El callback se invoca con la lista de segmentos físicos en los que se ha mapeado el buffer. En un sistema con un mapeo virtual a físico simple de 1:1, esa lista tendrá una sola entrada. En un sistema que necesita bounce buffers para alcanzar rangos de direcciones limitados, la lista puede tener varias entradas. Tu driver escribe un único callback, y ese callback maneja ambos casos de forma uniforme. Esa es la aportación de portabilidad de `bus_dma`: el callback ve un modelo simple, y la complejidad vive dentro del propio `bus_dma`.

### Un patrón práctico de cabecera por backend

Uniendo estas ideas, un driver portable suele usar una pequeña familia de archivos de cabecera y archivos fuente para hacer visible la separación en la estructura del sistema de archivos. El núcleo incluye `widget_common.h` para tipos y prototipos que no están vinculados a ningún bus. El archivo fuente del núcleo, habitualmente `widget_core.c`, contiene la lógica independiente del hardware. Cada backend de bus vive en su propio archivo `.c`, como `widget_pci.c` o `widget_usb.c`, e incluye `widget_backend.h` para las definiciones de la interfaz de backend. El Makefile lista el núcleo y los backends que estén habilitados en `SRCS`, y el build del kernel hace el resto.

Verás esta estructura aplicada paso a paso en la Sección 4 con el driver de referencia `portdrv`. Antes de llegar ahí, necesitamos formalizar cómo es una «interfaz de backend». Ese es el tema de la siguiente sección.

### Errores comunes al aislar el código de hardware

Hay un puñado de errores que se repiten con suficiente frecuencia como para merecer mención explícita. Cada uno es pequeño por sí solo, pero en conjunto pueden resultar muy perjudiciales.

El primer error es el **acceso oculto**. Una función auxiliar en algún lugar profundo del driver accede al softc y realiza un acceso al registro directamente, en lugar de pasar por el accessor. El resultado es una sola fuga, pero esa fuga puede ser suficiente para romper la abstracción. Cuando leas un driver, busca llamadas a `bus_space_*` fuera de los archivos de backend designados; son señales de alerta.

El segundo error es la **fuga de offsets**. El driver escribe `bus_space_read_4(tag, handle, 0x20)` con un offset literal. Los offsets literales son aceptables como constantes denominadas `REG_CONTROL` en un archivo de cabecera, pero son peligrosos como enteros sin nombre. Un offset sin nombre no te dice nada sobre qué hace el registro, y renumerar el registro en una revisión futura del hardware se convierte en un ejercicio de buscar y reemplazar. Define siempre los nombres de los registros como macros o enumeraciones en el archivo de cabecera, y utiliza esos nombres en el código.

El tercer error es la **confusión de tipos**. Un registro devuelve un valor de 32 bits, pero el código almacena el resultado en un `int` o un `unsigned long`. En un sistema de 64 bits esto suele funcionar por casualidad, porque el valor cabe. En un sistema de 32 bits con un `int` con signo, un registro cuyo bit más significativo está activo se convierte en negativo, y las comparaciones fallan. Utiliza siempre el tipo que corresponda a la anchura del registro: `uint8_t`, `uint16_t`, `uint32_t` o `uint64_t` según corresponda.

El cuarto error son las **suposiciones de endianness en la capa del accessor**. Las funciones `bus_space` devuelven el valor tal como lo ve el host, en el orden de bytes del host. Si el dispositivo utiliza un orden de bytes específico en sus registros, la conversión se realiza en otro lugar del código, usando `htole32` o `be32toh` según sea necesario. No incorpores la conversión en el accessor, porque otras partes del driver pueden acceder al mismo registro con fines administrativos y no desean que se les aplique la conversión.

El quinto error es **no usar ningún accessor**. El driver utiliza punteros `volatile uint32_t *` directamente para mapear los registros. Esto funciona en algunas arquitecturas y falla en otras. Anula por completo el propósito de `bus_space`. Si estás leyendo un driver que hace esto, da por hecho que está roto en al menos una arquitectura, aunque no puedas determinar cuál en ese momento.

### Subregiones, barreras y operaciones en ráfaga

Tres características de `bus_space` merecen un recorrido breve, porque aparecen con suficiente frecuencia en drivers reales como para que las encuentres incluso en bases de código pequeñas.

La característica de **subregión** te permite dividir una región mapeada en partes lógicas y entregar cada parte a un fragmento de código distinto. Si el BAR de tu dispositivo es de 64 KB y el driver tiene conceptualmente tres bancos de registros separados en los desplazamientos 0, 0x4000 y 0x8000, puedes crear tres subregiones con `bus_space_subregion` y entregar cada una a la función que la gestione. Cada subregión tiene su propia tag y su propio handle, y los accesos a ella llevan de forma natural desplazamientos desde el inicio de la subregión, no desde el inicio del BAR completo. El resultado es código que lee `bus_space_read_4(bank_tag, bank_handle, REG_CONTROL)` en lugar de `bus_space_read_4(whole_tag, whole_handle, BANK_B_BASE + REG_CONTROL)`. Los desplazamientos se vuelven locales al banco y no arrastran la aritmética de direcciones del BAR completo en cada punto de llamada.

La característica de **barrera** tiene que ver con el orden. Cuando escribes en un registro del dispositivo, el kernel tiene que proporcionar dos garantías distintas. La primera, que la escritura llega realmente al dispositivo, en lugar de quedarse en un buffer de escritura del CPU o en un puente de bus. La segunda, que las escrituras ocurren en el orden que el programador esperaba. En la mayoría de configuraciones `amd64` habituales, estas garantías vienen de manera gratuita; en `arm64` y algunas otras plataformas, no es así. El primitivo es `bus_space_barrier(tag, handle, offset, length, flags)`, donde `flags` es una combinación de `BUS_SPACE_BARRIER_READ` y `BUS_SPACE_BARRIER_WRITE`. La llamada dice: «todas las lecturas o escrituras en el rango especificado anteriores a este punto deben completarse antes que cualquiera posterior». La usas cuando tu driver depende del orden en que el dispositivo ve los accesos a registros, por ejemplo al armar una interrupción después de configurar un descriptor DMA.

La característica de **ráfaga** gestiona las transferencias rápidas hacia o desde la memoria del dispositivo. Cuando necesitas copiar un paquete hacia un FIFO o desde un buffer DMA, llamar a `bus_space_write_4` en un bucle es correcto pero lento. Las funciones `bus_space_write_multi_4` y `bus_space_read_multi_4` realizan la ráfaga completa en una sola llamada, y en arquitecturas que disponen de instrucciones de movimiento especializadas las utilizan. Para un driver de red que transfiere tramas a alta velocidad, los helpers de ráfaga pueden marcar la diferencia entre la velocidad de línea y la mitad de esa velocidad, y el coste de utilizarlos es únicamente una firma de llamada ligeramente diferente.

Ninguna de estas características es obligatoria para un driver pequeño. Todas ellas merecen conocerse, porque su ausencia en un driver en crecimiento lleva a soluciones alternativas más difíciles de leer y más lentas de ejecutar que el primitivo que se intentaba evitar.

### Encapsulado en capas: del primitivo al dominio

Los accesores mostrados anteriormente en esta sección son la primera capa de encapsulado en torno a `bus_space`. Los drivers reales a veces se benefician de una segunda capa que eleva los accesores a un vocabulario específico del dominio. La idea es sencilla: una vez que tienes `portdrv_read_reg(sc, off)`, puedes construir sobre ella funciones como `portdrv_read_status(sc)`, `portdrv_arm_interrupt(sc)` o `portdrv_load_descriptor(sc, idx, addr, len)`. El núcleo del driver habla entonces en el vocabulario del dispositivo en lugar de en accesos directos a registros.

```c
static inline uint32_t
portdrv_read_status(struct portdrv_softc *sc)
{
	return (portdrv_read_reg(sc, REG_STATUS));
}

static inline bool
portdrv_is_ready(struct portdrv_softc *sc)
{
	return ((portdrv_read_status(sc) & STATUS_READY) != 0);
}

static inline void
portdrv_arm_interrupt(struct portdrv_softc *sc, uint32_t mask)
{
	portdrv_write_reg(sc, REG_INTR_MASK, mask);
}
```

El código que llama a `portdrv_is_ready(sc)` en lugar de `(portdrv_read_reg(sc, REG_STATUS) & STATUS_READY) != 0` expresa intención en lugar de fontanería. Cuando la definición de «listo» cambia en una revisión más nueva del dispositivo, el cambio se produce en una única función inline en lugar de hacerse en cada punto de llamada. Es el mismo patrón que los accesores primitivos, solo que aplicado a la siguiente capa superior.

No lo hagas en exceso. Un wrapper por cada lectura de registro trivial es encapsular por encapsular. Añade un wrapper de nivel de dominio cuando su nombre sea significativamente más informativo que el acceso a registro, o cuando la operación sea lo suficientemente no trivial como para que el lector se beneficie de un nombre. Para una simple lectura de registro, el wrapper primitivo suele ser suficiente.

### Capas de accesores dentro del árbol de FreeBSD

Puedes ver este enfoque por capas utilizado en drivers maduros de FreeBSD. Observa `/usr/src/sys/dev/e1000/` para el driver Intel Gigabit Ethernet. El archivo `e1000_api.c` expone funciones de despacho como `e1000_reset_hw`, `e1000_init_hw` y `e1000_check_for_link`, cada una de las cuales redirige a un helper de la familia de chips (por ejemplo, los helpers del 82571 viven en `e1000_82571.c`, los del 82575 en `e1000_82575.c`, y así sucesivamente). Esos helpers específicos del chip realizan su trabajo de registros mediante `E1000_READ_REG` y `E1000_WRITE_REG`, que están definidos en `/usr/src/sys/dev/e1000/e1000_osdep.h` como macros que se expanden a `bus_space_read_4` y `bus_space_write_4` sobre la tag y el handle almacenados en el `struct e1000_osdep` del driver. Cuatro capas: primitivo de bus, macro accesora de registro, helper de familia de chips y API orientada al driver en `if_em.c`. Cada capa añade un poco más de significado, y cada capa puede modificarse sin perturbar las demás.

Para tus propios drivers, dos capas es generalmente el objetivo correcto: un accesor primitivo que envuelva `bus_read_*` o despache a través del backend, y un conjunto de helpers de dominio que eleven el primitivo al vocabulario del dispositivo. Tres capas, si el driver es grande y abarca múltiples subsistemas. Más de tres, y el encapsulado probablemente está oscureciendo en lugar de clarificando.

### Cuando la línea entre el código dependiente del hardware y el independiente se desdibuja

La línea entre el código dependiente del hardware y el código independiente del hardware suele estar clara, pero hay casos límite que merece la pena nombrar.

Un caso límite es la **lógica de temporización**. Supón que el driver debe esperar hasta 50 microsegundos para que aparezca un bit de estado después de escribir en un registro. La espera en sí es dependiente del hardware, porque el valor de 50 microsegundos proviene del datasheet del dispositivo. Pero el andamiaje (el bucle que sondea, el timeout que abandona y el retorno de error) es independiente del hardware. El diseño correcto es tener un pequeño helper en el núcleo que tome el predicado de sondeo como puntero a función o expresión inline, y una constante específica del hardware para el timeout. El núcleo es propietario del bucle de sondeo; el backend es propietario de la constante.

```c
/* Core. */
static int
portdrv_wait_for(struct portdrv_softc *sc,
    bool (*predicate)(struct portdrv_softc *sc),
    int timeout_us)
{
	int i;

	for (i = 0; i < timeout_us; i += 10) {
		if (predicate(sc))
			return (0);
		DELAY(10);
	}
	return (ETIMEDOUT);
}
```

El backend proporciona el predicado (que puede ser trivial, como leer un registro de estado) y el timeout. Esto mantiene la estructura del bucle en un único lugar y permite que los detalles específicos del dispositivo residan donde les corresponde.

Otro caso límite son las **máquinas de estado**. La máquina de estado de un bucle típico de procesamiento de solicitudes es independiente del hardware: inactivo, pendiente, en curso, completado. Las transiciones, sin embargo, pueden depender de eventos de hardware. Un patrón habitual es mantener la máquina de estado en el núcleo e invocar métodos del backend para las operaciones que puedan diferir entre backends. El núcleo dice «inicia la transferencia»; el backend hace lo que eso signifique. Las transiciones de estado ocurren en el núcleo en función de los valores de retorno del backend.

Un tercer caso límite es el **registro y la telemetría**. Quieres un único `device_printf` en el núcleo cuando falla una transferencia, no uno por backend. Pero el printf puede querer incluir información específica del backend, como el registro que causó el error. La solución es exponer un método del backend como `describe_error(sc, buf, buflen)` que rellene un buffer con un resumen legible por humanos, y hacer que el núcleo lo invoque. El núcleo es propietario de la línea de registro; el backend es propietario del vocabulario.

El tema común en todos estos casos límite es el mismo: **coloca la estructura en el núcleo y coloca los detalles en el backend**. Cuando no estés seguro de dónde pertenece un fragmento de código, pregúntate a qué categoría pertenece: estructura o detalle. La estructura trata de qué sucede. Los detalles tratan de exactamente cómo se toca el hardware para que suceda.

### Cerrando la sección 2

Ya has visto el primer movimiento estructural que hace todo driver portable: trazar una línea clara entre el código dependiente del hardware y el código independiente del hardware, y expresar esa línea en archivos y funciones accesoras reales en lugar de en comentarios. Las abstracciones `bus_space(9)` y `bus_dma(9)` son el regalo de FreeBSD a ese esfuerzo, porque ya ocultan la mayor parte de la complejidad específica de la arquitectura. Envolverlas en accesores locales del driver te da la pieza final: un único lugar donde cambiar cuando el backend cambia. El encapsulado en capas, el uso disciplinado de subregiones y barreras, y la colocación cuidadosa del código de casos límite entre el núcleo y el backend hacen que la línea entre ambos sea estable en lugar de frágil.

La siguiente sección da el paso más allá de los accesores. Si el driver tiene que soportar múltiples backends en el mismo binario, ¿cómo es la interfaz entre el núcleo y los backends? Ahí es donde la idea de una **interfaz de backend**, expresada como un struct de punteros a función, se vuelve esencial.

## Sección 3: Abstracción del comportamiento del dispositivo

En la sección 2 trazamos una línea entre el código dependiente del hardware y el código independiente del hardware, y envolvimos las operaciones primitivas de bus detrás de accesores locales del driver. Ese movimiento gestiona el caso simple, en el que el driver habla con un tipo de dispositivo a través de un tipo de bus. En esta sección gestionamos el caso más difícil e interesante: un driver que soporta múltiples backends en el mismo build.

La técnica que hace esto manejable es sencilla de describir y profundamente útil en la práctica. El núcleo del driver define una **interfaz**: un struct de punteros a función que describe qué operaciones debe proporcionar un backend. Cada backend proporciona una instancia de ese struct, rellena con sus propias implementaciones. El núcleo llama a través del struct en lugar de llamar a cualquier backend directamente. Añadir un nuevo backend es cuestión de escribir una instancia más del struct y un conjunto más de funciones; el núcleo no se toca.

Este patrón está en todas partes en FreeBSD. El propio framework Newbus está construido sobre una versión más elaborada de él, llamada `kobj`, y has estado usando `kobj` indirectamente cada vez que rellenaste un array `device_method_t` en capítulos anteriores. En este capítulo presentamos una versión más ligera que puedes aplicar a tus propios drivers sin toda la ceremonia de `kobj`.

### Por qué una interfaz formal supera a las cadenas if-else

El primer instinto de un programador que necesita soportar dos backends es a menudo ramificar en función del tipo de backend en cada punto de llamada. Algo así:

```c
static void
widget_start_transfer(struct widget_softc *sc)
{
	if (sc->sc_backend == BACKEND_PCI) {
		bus_space_write_4(sc->sc_btag, sc->sc_bhandle,
		    REG_CONTROL, CTRL_START);
	} else if (sc->sc_backend == BACKEND_USB) {
		widget_usb_ctrl_write(sc, REG_CONTROL, CTRL_START);
	} else if (sc->sc_backend == BACKEND_SIM) {
		sc->sc_sim_state.control |= CTRL_START;
	}
}
```

Esto funciona para una función. Se vuelve insoportable en la décima. Cada función adquiere una sentencia switch. Cada nuevo backend añade otra rama a cada switch. El número de modificaciones crece como el producto del número de funciones y de backends. Tras el tercer backend, el driver apenas es legible, y cualquier cambio en la forma de una operación del backend requiere visitas a una docena de archivos.

La solución estructural es reemplazar la cadena con una única indirección. Define un struct de punteros a función:

```c
struct widget_backend {
	const char *name;
	int   (*attach)(struct widget_softc *sc);
	void  (*detach)(struct widget_softc *sc);
	uint32_t (*read_reg)(struct widget_softc *sc, bus_size_t off);
	void     (*write_reg)(struct widget_softc *sc, bus_size_t off, uint32_t val);
	int   (*start_transfer)(struct widget_softc *sc,
	                        const void *buf, size_t len);
	int   (*poll_done)(struct widget_softc *sc);
};
```

Cada backend proporciona una instancia de este struct. El backend PCI define `widget_pci_read_reg`, `widget_pci_write_reg` y el resto, y luego rellena un `struct widget_backend` con esos punteros a función:

```c
const struct widget_backend widget_pci_backend = {
	.name           = "pci",
	.attach         = widget_pci_attach,
	.detach         = widget_pci_detach,
	.read_reg       = widget_pci_read_reg,
	.write_reg      = widget_pci_write_reg,
	.start_transfer = widget_pci_start_transfer,
	.poll_done      = widget_pci_poll_done,
};
```

El backend USB hace lo mismo, el backend de simulación hace lo mismo, y así sucesivamente. El softc lleva un puntero al backend que esta instancia esté usando:

```c
struct widget_softc {
	device_t  sc_dev;
	const struct widget_backend *sc_be;
	/* ... bus-specific fields, kept opaque to the core ... */
	void     *sc_backend_priv;
};
```

Y el núcleo llama a través del puntero:

```c
static void
widget_start_transfer(struct widget_softc *sc, const void *buf, size_t len)
{
	(void)sc->sc_be->start_transfer(sc, buf, len);
}
```

El núcleo no ramifica. El núcleo no menciona `pci`, `usb` ni `sim`. El núcleo llama a una operación, y la operación se busca a través de un puntero. Este es el movimiento fundamental. Léelo varias veces y deja que se asiente; el resto de la sección es elaboración y detalle concreto.

### Configuración del backend en el momento del attach

Cada ruta de attach instala el backend correcto en el softc. El código de attach PCI tiene aproximadamente este aspecto:

```c
static int
widget_pci_attach(device_t dev)
{
	struct widget_softc *sc = device_get_softc(dev);
	int err;

	sc->sc_dev = dev;
	sc->sc_be = &widget_pci_backend;

	/* Allocate bus resources, store them in sc->sc_backend_priv. */
	err = widget_pci_alloc_resources(sc);
	if (err != 0)
		return (err);

	/* Hand off to the core, which will use the backend through sc->sc_be. */
	return (widget_core_attach(sc));
}
```

El attach USB es estructuralmente idéntico: asigna sus recursos de bus, instala `widget_usb_backend` y llama a `widget_core_attach`. El attach de simulación es idéntico también: instala `widget_sim_backend`, que no asigna nada real, y llama a `widget_core_attach`.

La lección es que **la ruta de attach por backend es pequeña, y su función es preparar el softc para el core**. Una vez que el softc tiene un backend válido y un estado privado del backend, el core toma el relevo y realiza el trabajo universal de conectar el driver con el resto del kernel.

### Cómo lo hace el driver UART real

Abre `/usr/src/sys/dev/uart/uart_bus.h` y ve a la definición de `struct uart_class`. Verás el patrón que acabamos de describir, con un matiz propio de FreeBSD. El matiz es que el driver UART usa el framework `kobj(9)`, que añade una capa de despacho en tiempo de compilación sobre los punteros a función, pero la esencia es la misma. Una `struct uart_class` lleva un puntero `uc_ops` a una estructura de operaciones, junto con algunos campos de configuración, y cada variante del driver específica para un hardware determinado proporciona su propia instancia de `uart_class`.

Abre ahora `/usr/src/sys/dev/uart/uart_bus_pci.c`. El array `device_method_t uart_pci_methods[]` al comienzo del archivo declara qué funciones implementan los métodos de dispositivo específicos de PCI. Compáralo con `/usr/src/sys/dev/uart/uart_bus_fdt.c`, que hace el mismo trabajo para plataformas basadas en Device Tree. Ambos archivos son cortos porque hacen muy poco: instalan la clase correcta, asignan recursos y ceden el control al núcleo en `uart_core.c`.

Este es exactamente el patrón que estás aprendiendo en esta sección, solo que vestido con el idioma formal de `kobj` que FreeBSD usa para los drivers de bus. Cuando escribas tu propia abstracción más ligera para un driver más pequeño, puedes usar una struct simple de punteros a función, y el resultado será más fácil de leer y razonar que un `kobj` completo. Para un driver con unos pocos backends y un conjunto de métodos simple, la struct simple es la opción correcta.

### Cómo elegir el conjunto de operaciones

Diseñar una interfaz de backend es una cuestión de gusto y criterio. No existe una fórmula mecánica, pero algunos principios ayudan.

Primero, **toda operación que varía según el backend pertenece a la interfaz; toda operación que no varía no pertenece a ella**. Si el mismo código es correcto independientemente de si el backend es PCI o USB, pon ese código en el núcleo. No lo pongas en la interfaz, porque cada backend tendría entonces que implementarlo, y las implementaciones serían idénticas, lo que desvirtúa el propósito.

Segundo, **mantén la interfaz estrecha**. Cuantas menos operaciones, menos trabajo para implementar un nuevo backend y menos superficie a mantener. Si dos operaciones aparecen siempre juntas en los puntos de llamada, considera fusionarlas en una sola operación que realice ambas.

Tercero, **prefiere las operaciones gruesas a las finas**. Una operación como `start_transfer(sc, buf, len)` es gruesa: captura la intención de una transferencia en una sola llamada. Un par equivalente como `write_reg(sc, DATA, ...)` seguido de `write_reg(sc, CONTROL, CTRL_START)` es de grano fino, y obliga al núcleo a conocer el mapa de registros. Las operaciones gruesas permiten a los backends implementar la secuencia como necesiten, incluyendo formas que el núcleo no puede conocer. Esta es también la clave para la simulación de backends: un simulador puede «realizar» una transferencia sin escribir realmente ningún registro.

Cuarto, **deja espacio para el estado privado**. Cada backend puede necesitar sus propios datos por instancia que al núcleo no le importan. Un campo `void *sc_backend_priv` en el softc, propiedad del backend, es el patrón habitual. El backend PCI almacena allí sus recursos de bus. El backend USB almacena allí sus handles de dispositivo USB. El backend de simulación almacena allí su mapa de registros en memoria. El núcleo nunca lee ni escribe este campo; simplemente pasa el softc sin modificarlo.

Quinto, **define una struct con versión si quieres que la interfaz evolucione con el tiempo**. Cuando la interfaz es pequeña y el número de backends también lo es, puedes añadir un campo y actualizar todos los backends en un solo commit. Cuando los backends son externos a tu árbol o se mantienen por separado, puede que quieras un campo de versión en la struct, para que un backend pueda rechazar el attach si el núcleo espera una forma más reciente. Este es el mismo truco que FreeBSD utiliza en algunas de sus KPIs.

### Cómo evitar la proliferación de if-else

Una vez que la interfaz está en su lugar, estate atento a una forma sutil de regresión: la reintroducción de ramas específicas de backend. Puede ocurrir de forma inocente. Se añade una nueva funcionalidad que solo tiene sentido en un backend, y en lugar de extender la interfaz, el desarrollador escribe `if (sc->sc_be == &widget_pci_backend) { ... }` en algún lugar del núcleo. Funciona, una vez. Repítelo tres o cuatro veces y el driver ya no está abstractamente limpio; el backend se va filtrando de nuevo en el núcleo a través de comparaciones de etiquetas.

La solución correcta para una funcionalidad que solo un backend soporta es añadir una operación a la interfaz que los demás backends implementen de forma trivial. Si solo el backend PCI soporta, por ejemplo, la coalescencia de interrupciones, añade `set_coalesce(sc, usec)` a la interfaz e impleméntala como un no-op en los backends USB y de simulación. El núcleo la llama incondicionalmente. Esto mantiene al núcleo ignorante de la identidad del backend a la vez que permite la especialización por backend.

### Descubrimiento y registro del backend

Una última pregunta práctica: ¿cómo se realiza el attach de un backend en primer lugar? La respuesta varía según el bus. En PCI, la maquinaria Newbus del kernel recorre el árbol PCI y llama a la función probe de cada driver candidato. Escribes `widget_pci_probe` y lo registras con `DRIVER_MODULE(widget_pci, ...)`; el kernel hace el resto. En USB, el framework `usbd` realiza una exploración análoga sobre los dispositivos USB y llama al probe de cada driver candidato. En un backend puramente virtual o simulado, no hay ningún bus que recorrer, y el backend se suele adjuntar mediante un hook de inicialización de módulo que crea una única instancia.

El punto importante sobre portabilidad es este: **la maquinaria de registro de cada bus forma parte del backend, no del núcleo**. El núcleo nunca llama a `DRIVER_MODULE` ni a `USB_PNP_HOST_INFO` ni a ninguna macro de registro específica de bus. El núcleo exporta un punto de entrada limpio (`widget_core_attach`) al que cada backend llama cuando su propia lógica de attach está lista. Todo lo específico del bus ocurre en el archivo del backend. Esto es lo que mantiene el núcleo suficientemente limpio para trasladarlo sin cambios a un nuevo bus.

### Errores frecuentes al diseñar una interfaz

Algunos errores se repiten con suficiente frecuencia como para mencionarlos.

**Sobreabstracción.** No toda operación pertenece a la interfaz. Una operación que solo necesita un backend debe vivir en el código privado de ese backend, no forzarse dentro de la struct. Una interfaz llena de implementaciones stub con `return 0;` ha crecido demasiado.

**Abstracción insuficiente.** Por el contrario, una interfaz que obliga a cada backend a replicar las mismas cinco líneas de lógica de configuración ha quedado demasiado estrecha. Si tres backends comienzan su `start_transfer` con las mismas dos líneas, mueve esas dos líneas al núcleo y haz que la operación de la interfaz reciba una entrada ya validada.

**Exposición de tipos de backend.** Si el núcleo tiene un `switch` sobre la identidad del backend, la abstracción está rota. El núcleo debe saber qué operaciones llamar, no qué backend las proporciona.

**Mezcla de políticas de sincronización.** Cada backend debe respetar la política de locking del núcleo. Si el núcleo llama a `start_transfer` con el lock del softc tomado, cada backend debe respetar eso y no dormirse. Si el núcleo llama a `attach` sin lock, el backend no debe asumir que hay uno tomado. Una interfaz no es solo un conjunto de firmas; es también un contrato sobre el contexto que la rodea.

**Ignorar el ciclo de vida.** Si el núcleo llama a `backend->attach` y recibe un código de error distinto de cero, no debe llamar después a `backend->detach`, porque no se adjuntó nada. Documenta el contrato y escribe los backends para que lo respeten. La confusión en este punto lleva a doble liberación de memoria y a errores de use-after-free.

### Una mirada más detallada a `kobj(9)`

Dado que Newbus usa internamente `kobj(9)`, merece la pena detenerse un momento a ver qué hace el kernel por ti cuando escribes `device_method_t`. El mecanismo puede parecer intimidante, pero la idea subyacente es el mismo despacho mediante punteros a función del que llevamos hablando, solo que formalizado.

Abre `/usr/src/sys/kern/subr_kobj.c` y observa cómo se registran las clases. Una `kobj_class` contiene un nombre, una lista de punteros a métodos, una tabla de operaciones que el kernel construye en el momento de cargar el módulo, y un tamaño para los datos de instancia. Cuando escribes:

```c
static device_method_t portdrv_pci_methods[] = {
	DEVMETHOD(device_probe,  portdrv_pci_probe),
	DEVMETHOD(device_attach, portdrv_pci_attach),
	DEVMETHOD(device_detach, portdrv_pci_detach),
	DEVMETHOD_END
};
```

estás construyendo una de estas listas de métodos. El kernel compila la lista en una tabla de despacho en tiempo de ejecución, y cada llamada como `DEVICE_PROBE(dev)` se convierte en una búsqueda en la tabla seguida de una llamada a través de un puntero a función. Desde la perspectiva del programador, el punto de llamada es una sola macro; desde la perspectiva de la máquina, es una indirección.

Para una abstracción de backend pequeña, el patrón de struct de punteros a función que mostramos antes es suficiente y más fácil de leer. Cuando la interfaz supera los ocho métodos aproximadamente y empieza a acumular implementaciones por defecto, `kobj` comienza a justificar su complejidad. La capacidad de que una subclase herede métodos de una clase base, de que un método tenga como valor por defecto un no-op cuando una clase no lo proporciona, y de ser verificado estáticamente mediante el encabezado de ID de método justifica la maquinaria adicional. Para la mayoría de los drivers de este libro no necesitarás `kobj` directamente, pero reconocer que hace lo mismo que tu struct simple hace que la maquinaria de Newbus resulte menos mágica.

### Backends apilados y delegación

Un patrón relacionado y más avanzado es el backend **apilado** o **delegante**. A veces un backend es por naturaleza una envoltura delgada sobre otro backend, con alguna pequeña transformación aplicada. Imagina un backend que obliga a registrar todos los accesos de escritura a registros para depuración, o uno que añade un retardo antes de cada lectura para simular un bus lento, o uno que registra el tráfico de registros en un buffer circular para reproducirlo después. Cada uno de ellos es funcionalmente una envoltura alrededor de un backend real.

El patrón es sencillo. La struct del backend envolvente incluye un puntero a un backend interno:

```c
struct portdrv_debug_priv {
	const struct portdrv_backend *inner;
	void                         *inner_priv;
	/* logging state, statistics, etc. */
};

static uint32_t
portdrv_debug_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	struct portdrv_debug_priv *dp = sc->sc_backend_priv;
	void *saved_priv = sc->sc_backend_priv;
	uint32_t val;

	sc->sc_backend_priv = dp->inner_priv;
	val = dp->inner->read_reg(sc, off);
	sc->sc_backend_priv = saved_priv;

	device_printf(sc->sc_dev, "read  0x%04jx -> 0x%08x\n",
	    (uintmax_t)off, val);
	return (val);
}
```

El backend de depuración toma prestada la implementación del backend real para la lectura efectiva del registro, y añade registro de eventos a su alrededor. El núcleo no sabe que está hablando con una envoltura. En el momento del attach, el driver puede instalar el backend PCI normal o el backend de depuración que lo envuelve. Este es un patrón útil para el desarrollo, para registrar una traza de un fallo en el boot, o para construir un simulador que reproduzca una traza grabada.

No todos los drivers necesitan backends apilados. Cuando los necesites, el patrón anterior es la plantilla. La interfaz se mantiene igual; una instancia de backend simplemente tiene la flexibilidad de delegar en otra instancia en lugar de hacer el trabajo ella misma.

### Cómo hacer explícito el contrato de la interfaz

Una buena interfaz de backend no es solo una struct; es un contrato sobre qué hace cada método, qué precondiciones tiene y qué efectos secundarios produce. El contrato vive en un comentario al comienzo del encabezado:

```c
/*
 * portdrv_backend.h - backend interface contract.
 *
 * The core acquires sc->sc_mtx (a MTX_DEF) before calling any method
 * except attach and detach. Methods must not sleep while holding this
 * lock. The core releases the lock before calling attach and detach,
 * because those methods may allocate memory with M_WAITOK or perform
 * other sleepable operations.
 *
 * read_reg and write_reg must be side-effect-free from the core's
 * point of view, other than the corresponding register access.
 * start_transfer may record pending work but must not block; it
 * returns 0 on success or an errno on failure. poll_done returns
 * non-zero when the transfer has completed and zero otherwise.
 *
 * Backends may return EOPNOTSUPP for an operation they do not
 * support. The core treats EOPNOTSUPP as "feature not present",
 * and its callers degrade gracefully.
 */
```

Este tipo de comentario no es decoración. Es el cuerpo de conocimiento que un nuevo colaborador necesita para implementar un nuevo backend correctamente. Sin él, el autor del tercer backend debe hacer ingeniería inversa del contrato a partir de los dos existentes, y normalmente lo malinterpreta ligeramente. Con él, el tercer backend es un ejercicio sencillo.

Incluye en el contrato como mínimo: las reglas de locking (qué locks tiene el núcleo en cada llamada a método), las reglas de sueño (qué métodos pueden dormirse), las convenciones de valor de retorno, y cualquier efecto secundario del que dependa el núcleo. Si el contrato permite que los métodos sean `NULL`, indícalo. Si algunos métodos son obligatorios y otros opcionales, indícalo también.

### Interfaces mínimas frente a interfaces ricas

Una pregunta de diseño recurrente es cuán rica debe ser la interfaz. ¿Debe el backend exponer un único método `do_transfer` que cubra todos los tipos de transferencia, o una familia de métodos especializados para lectura, escritura, comando y estado? ¿Debe el núcleo gestionar la cola de solicitudes de alto nivel y pedir al backend que ejecute cada paso, o debe el backend ser propietario de la cola e informar de las finalizaciones hacia arriba?

No hay una respuesta universal, pero dos principios ayudan.

**Si la operación difiere solo en el vocabulario, unifícala.** `read_reg` y `write_reg` difieren únicamente en si el acceso es de salida o de entrada. Naturalmente son dos métodos, pero podrían unificarse razonablemente en un único `access_reg(sc, off, direction, valp)`. En la práctica, tener dos métodos resulta más claro que un flag de dirección, porque los puntos de llamada se leen mejor. Separa cuando la separación es evidente.

**Si las operaciones difieren en su contenido, mantenlas separadas.** Un dispositivo puede admitir tanto transferencias de tipo "command" como de tipo "data" que son completamente distintas a nivel de registro. Fusiónalas en un solo método y obligas al backend a despachar según un flag interno. Mantenlas como dos métodos y la tarea del backend se vuelve obvia. Separa cuando la separación es sustancial.

Una prueba útil: escribe la documentación del método que estás considerando. Si esa documentación necesita describir dos comportamientos distinguidos por un parámetro, separa. Si describe un único comportamiento cuyos parámetros varían el valor, mantenlo.

### El contrato «blando» de un backend

Más allá del contrato explícito, existe un contrato blando sobre la gestión de recursos. Cada backend debe ser coherente en quién asigna qué, cuándo es válida cada pieza de estado y quién la libera en el momento del apagado. La convención habitual es la siguiente:

- El código per-attach del backend asigna su estado privado en `sc_backend_priv` y los recursos de bus que necesita. Regresa antes de que el core empiece a usar la interfaz.
- El core asigna su propio estado (locks, colas, dispositivos de caracteres) después de que el backend esté instalado, en `portdrv_core_attach`.
- En el detach, el core desmonta primero su propio estado, luego llama al método `detach` del backend, que libera los recursos del backend y libera `sc_backend_priv`.

Este orden importa. Si el core desmonta su dispositivo de caracteres antes de que el backend detenga el DMA, una transferencia en vuelo podría disparar un callback hacia un objeto destruido. Si el backend libera `sc_backend_priv` antes de que el core finalice una escritura, el acceso del core provoca un pánico por use-after-free. Documenta el orden de desmontaje en el contrato del backend, y respétalo en cada backend.

Revisar el contrato blando es una excelente actividad que debes realizar cuando escribas un nuevo backend. Recorre mentalmente los caminos de attach y detach, haciendo un seguimiento de qué objeto está vivo y cuál está siendo desmontado. La mayoría de los bugs en un backend bien escrito son bugs de ciclo de vida, y son más fáciles de detectar antes de que el código se ejecute que después.

### Cerrando la Sección 3

La interfaz backend es la columna vertebral conceptual de un driver portable. Los accesores gestionan la variación a pequeña escala, como el ancho de los registros y la arquitectura, pero la interfaz backend gestiona la variación a gran escala, como el bus completo. Una vez que tienes una interfaz, añadir un backend es un cambio en un único archivo, y el core queda inmunizado frente a las preocupaciones específicas del bus. La primera vez que lo experimentes, comprenderás por qué el patrón es tan universal en el código del kernel. Entender `kobj(9)` como una forma más elaborada de la misma idea, y reconocer cuándo merecen la pena el esfuerzo de los backends apilados o un contrato cuidadoso, son los próximos pasos para escribir drivers que escalen más allá de una única variante.

En la Sección 4 transformamos la interfaz backend en un diseño concreto de archivos: qué funciones residen en qué archivo, qué cabecera incluye cada archivo y cómo el sistema de build lo ensambla todo. Las ideas son las mismas; el resultado es un driver multifichero funcional que puedes compilar y cargar.

## Sección 4: División del código en módulos lógicos

Un driver portable no suele ser un único archivo. Es una pequeña familia de archivos, cada uno con una responsabilidad clara, y un sistema de build que sabe cómo ensamblarlos. Esta sección toma la interfaz backend de la Sección 3 y la expresa como un diseño de directorios concreto que puedes escribir, compilar y cargar. El diseño de archivos no es sagrado, pero es una convención bien establecida en FreeBSD, y seguirlo hará que tu driver resulte familiar a cualquier otra persona que lo lea.

El objetivo es un diseño en el que cada archivo responde exactamente a una pregunta. Un lector que se enfrente al driver por primera vez debería poder adivinar qué archivo abrir en función de lo que busca, sin necesidad de usar grep. Eso es lo que significa «módulos lógicos» en este capítulo: no módulo en el sentido de `.ko`, sino módulo en el sentido estructural, una unidad de código con un único propósito.

### El diseño canónico

Empieza por el driver de referencia `portdrv`. Tras la refactorización, tiene este aspecto en disco:

```text
portdrv/
├── Makefile
├── portdrv.h           # cross-file public prototypes and types
├── portdrv_common.h    # types shared between core and backends
├── portdrv_backend.h   # the backend interface struct
├── portdrv_core.c      # hardware-independent logic
├── portdrv_pci.c       # PCI backend
├── portdrv_usb.c       # USB backend (optional, conditional)
├── portdrv_sim.c       # simulation backend (optional, conditional)
├── portdrv_sysctl.c    # sysctl tree, helpers
├── portdrv_ioctl.c     # ioctl handlers, if the driver exposes them
└── portdrv_dma.c       # DMA helpers, if the driver uses DMA
```

Ese es el objetivo. No todos los drivers necesitan cada archivo. Un driver pequeño que no hace DMA no tiene `portdrv_dma.c`. Un driver que solo tiene un backend no necesita archivos de backend separados y puede mantener el código específico del hardware en un único `portdrv_pci.c` junto al core. El diseño escala a medida que el driver crece, y la idea clave es que **las nuevas responsabilidades reciben archivos nuevos en lugar de nuevas secciones en un archivo existente**.

### Responsabilidades por archivo

Recorramos el propósito previsto de cada archivo, para que el diseño resulte menos abstracto.

- **`portdrv.h`**: la cabecera pública del driver. Es la que incluyen `portdrv_sysctl.c` y otros archivos internos para acceder al softc y a los puntos de entrada del core. Incorpora los tipos comunes, predeclara el softc y expone las funciones que cada archivo necesita llamar a través de los límites entre archivos.

- **`portdrv_common.h`**: el subconjunto de tipos que los backends también necesitan. Separarlo de `portdrv.h` resulta útil porque los archivos de backend no deberían necesitar ver cada detalle interno del core. Si el softc tiene, por ejemplo, un campo que solo es relevante para el manejo de ioctl, el tipo de ese campo puede permanecer en `portdrv.h`, mientras que el puntero a la interfaz backend se expone en `portdrv_common.h`. En drivers más pequeños puedes fusionar las dos cabeceras si la separación parece forzada.

- **`portdrv_backend.h`**: la definición canónica y única de la interfaz backend: el struct de punteros a función, las constantes para la identificación del backend y las declaraciones de los helpers compartidos entre backends. Todos los archivos de backend la incluyen. El core también la incluye. Este es el archivo que abres cuando quieres saber qué espera el core de un backend.

- **`portdrv_core.c`**: la lógica independiente del hardware. El attach y el detach del driver en su conjunto, la cola de solicitudes, la asignación del softc, el registro del dispositivo de caracteres o la interfaz, la ruta de invocación de callbacks. El core no incluye ninguna cabecera específica del bus, como `dev/pci/pcivar.h` o `dev/usb/usb.h`. Si te encuentras incluyendo una cabecera así en `portdrv_core.c`, algo ha salido mal.

- **`portdrv_pci.c`**: todo lo que conoce PCI. El array `device_method_t` para PCI, el probe de PCI, el asignador específico de PCI, el manejador de interrupciones de PCI si el driver usa uno, y el registro con `DRIVER_MODULE`. La implementación del backend PCI de cada función de la interfaz backend vive aquí. Este archivo incluye `dev/pci/pcivar.h` y las demás cabeceras de PCI; el core no.

- **`portdrv_usb.c`**: el backend USB, con su propio attach, su propio registro a través del framework USB y su propia implementación de la interfaz backend. Paralelo en estructura al backend PCI, pero accediendo al hardware a través de una API completamente diferente.

- **`portdrv_sim.c`**: el backend de simulación. Una implementación puramente software que respeta la misma interfaz pero almacena el estado de los registros en memoria y sintetiza las finalizaciones. Útil para pruebas y CI; esencial para el desarrollo cuando el hardware real no está disponible.

- **`portdrv_sysctl.c`**: el árbol sysctl del driver y las funciones helper que leen y establecen sus variables. Mantener sysctl fuera del core tiene dos ventajas. En primer lugar, el archivo del core permanece centrado en I/O. En segundo lugar, el árbol sysctl se vuelve fácil de ampliar sin añadir ruido al core.

- **`portdrv_ioctl.c`**: la sentencia switch que despacha los comandos ioctl, un helper por comando. Los drivers grandes acumulan muchos ioctls, y moverlos a su propio archivo mantiene legible el flujo principal del core. Un driver pequeño con dos ioctls puede mantenerlos en el core.

- **`portdrv_dma.c`**: helpers para la configuración y el desmontaje de DMA. Las funciones `portdrv_dma_create_tag`, `portdrv_dma_alloc_buffer`, `portdrv_dma_free_buffer`, y similares, cada una de las cuales envuelve una primitiva `bus_dma`. Estos helpers son utilizados por los backends pero no dependen de ningún bus específico. Aislarlos en su propio archivo deja claro cuál es la superficie DMA del driver.

### Un archivo core mínimo

El archivo core es donde vivirá la mayor parte de tu lógica no trivial. Aquí tienes un esbozo de cómo es `portdrv_core.c`, mostrando solo los elementos estructurales. Escríbelo, compílalo y cárgalo; los detalles se completan en el laboratorio al final del capítulo.

```c
/*
 * portdrv_core.c - hardware-independent core for the portdrv driver.
 *
 * This file knows about the backend interface and the softc, but
 * does not include any bus-specific header. Backends are installed
 * by per-bus attach paths in portdrv_pci.c, portdrv_usb.c, etc.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/mutex.h>

#include "portdrv.h"
#include "portdrv_common.h"
#include "portdrv_backend.h"

static MALLOC_DEFINE(M_PORTDRV, "portdrv", "portdrv driver state");

int
portdrv_core_attach(struct portdrv_softc *sc)
{
	KASSERT(sc != NULL, ("portdrv_core_attach: NULL softc"));
	KASSERT(sc->sc_be != NULL,
	    ("portdrv_core_attach: backend not installed"));

	mtx_init(&sc->sc_mtx, "portdrv", NULL, MTX_DEF);

	/* Call the backend-specific attach step. */
	if (sc->sc_be->attach != NULL) {
		int err = sc->sc_be->attach(sc);
		if (err != 0) {
			mtx_destroy(&sc->sc_mtx);
			return (err);
		}
	}

	/* Hardware is up; register with the upper half of the kernel
	 * (cdev, ifnet, GEOM, etc. as appropriate). */
	return (portdrv_core_register_cdev(sc));
}

void
portdrv_core_detach(struct portdrv_softc *sc)
{
	portdrv_core_unregister_cdev(sc);
	if (sc->sc_be->detach != NULL)
		sc->sc_be->detach(sc);
	mtx_destroy(&sc->sc_mtx);
}

int
portdrv_core_submit(struct portdrv_softc *sc, const void *buf, size_t len)
{
	/* All of this logic is hardware-independent. */
	return (sc->sc_be->start_transfer(sc, buf, len));
}
```

Observa lo que no hay en este archivo. Ningún `#include <dev/pci/pcivar.h>`. Ningún probe de PCI. Ningún descriptor USB. Ningún desplazamiento de registro. El core es lógica pura y delegación. Es suficientemente pequeño como para que un lector pueda mantener el archivo completo en la cabeza, y no cambia cuando se añade un nuevo backend.

### Un archivo backend mínimo

El archivo por backend es el único lugar donde aparecen las cabeceras específicas del bus. Un esbozo de `portdrv_pci.c`:

```c
/*
 * portdrv_pci.c - PCI backend for the portdrv driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include "portdrv.h"
#include "portdrv_common.h"
#include "portdrv_backend.h"

struct portdrv_pci_softc {
	struct resource *pci_bar;
	int              pci_bar_rid;
	struct resource *pci_irq;
	int              pci_irq_rid;
	void            *pci_ih;
};

static uint32_t
portdrv_pci_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	struct portdrv_pci_softc *psc = sc->sc_backend_priv;

	return (bus_read_4(psc->pci_bar, off));
}

static void
portdrv_pci_write_reg(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	struct portdrv_pci_softc *psc = sc->sc_backend_priv;

	bus_write_4(psc->pci_bar, off, val);
}

static int
portdrv_pci_attach_be(struct portdrv_softc *sc)
{
	/* Finish any backend-specific setup after resources are claimed. */
	return (0);
}

static void
portdrv_pci_detach_be(struct portdrv_softc *sc)
{
	/* Tear down any backend-specific state. */
}

const struct portdrv_backend portdrv_pci_backend = {
	.name           = "pci",
	.attach         = portdrv_pci_attach_be,
	.detach         = portdrv_pci_detach_be,
	.read_reg       = portdrv_pci_read_reg,
	.write_reg      = portdrv_pci_write_reg,
	.start_transfer = portdrv_pci_start_transfer,
	.poll_done      = portdrv_pci_poll_done,
};

static int
portdrv_pci_probe(device_t dev)
{
	if (pci_get_vendor(dev) != PORTDRV_VENDOR ||
	    pci_get_device(dev) != PORTDRV_DEVICE)
		return (ENXIO);
	device_set_desc(dev, "portdrv (PCI backend)");
	return (BUS_PROBE_DEFAULT);
}

static int
portdrv_pci_attach(device_t dev)
{
	struct portdrv_softc *sc = device_get_softc(dev);
	struct portdrv_pci_softc *psc;
	int err;

	psc = malloc(sizeof(*psc), M_PORTDRV, M_WAITOK | M_ZERO);
	sc->sc_dev = dev;
	sc->sc_be = &portdrv_pci_backend;
	sc->sc_backend_priv = psc;

	psc->pci_bar_rid = PCIR_BAR(0);
	psc->pci_bar = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &psc->pci_bar_rid, RF_ACTIVE);
	if (psc->pci_bar == NULL) {
		free(psc, M_PORTDRV);
		sc->sc_backend_priv = NULL;
		return (ENXIO);
	}

	err = portdrv_core_attach(sc);
	if (err != 0) {
		bus_release_resource(dev, SYS_RES_MEMORY, psc->pci_bar_rid,
		    psc->pci_bar);
		free(psc, M_PORTDRV);
		sc->sc_backend_priv = NULL;
	}
	return (err);
}

static int
portdrv_pci_detach(device_t dev)
{
	struct portdrv_softc *sc = device_get_softc(dev);
	struct portdrv_pci_softc *psc = sc->sc_backend_priv;

	portdrv_core_detach(sc);
	if (psc != NULL) {
		if (psc->pci_bar != NULL)
			bus_release_resource(dev, SYS_RES_MEMORY,
			    psc->pci_bar_rid, psc->pci_bar);
		free(psc, M_PORTDRV);
		sc->sc_backend_priv = NULL;
	}
	return (0);
}

static device_method_t portdrv_pci_methods[] = {
	DEVMETHOD(device_probe,  portdrv_pci_probe),
	DEVMETHOD(device_attach, portdrv_pci_attach),
	DEVMETHOD(device_detach, portdrv_pci_detach),
	DEVMETHOD_END
};

static driver_t portdrv_pci_driver = {
	"portdrv_pci",
	portdrv_pci_methods,
	sizeof(struct portdrv_softc)
};

DRIVER_MODULE(portdrv_pci, pci, portdrv_pci_driver, 0, 0);
MODULE_VERSION(portdrv_pci, 1);
MODULE_DEPEND(portdrv_pci, portdrv_core, 1, 1, 1);
```

De nuevo, observa qué está presente y qué no. Los includes de cabeceras específicos de PCI están todos aquí. El probe de PCI está aquí. La macro `DRIVER_MODULE` está aquí. Nada de esto está en el core. Si a continuación estás escribiendo un backend USB, ese archivo tiene una estructura idéntica pero incluye cabeceras USB en su lugar y se registra con el subsistema USB.

### Organización de las cabeceras

Las tres cabeceras `portdrv.h`, `portdrv_common.h` y `portdrv_backend.h` tienen este aspecto en miniatura.

`portdrv.h` es la cara pública del driver dentro de los archivos del driver:

```c
#ifndef _PORTDRV_H_
#define _PORTDRV_H_

#include <sys/malloc.h>

/* Softc, opaque to anything outside the core. */
struct portdrv_softc;

/* Core entry points called by backends. */
int  portdrv_core_attach(struct portdrv_softc *sc);
void portdrv_core_detach(struct portdrv_softc *sc);
int  portdrv_core_submit(struct portdrv_softc *sc,
          const void *buf, size_t len);

MALLOC_DECLARE(M_PORTDRV);

#endif /* !_PORTDRV_H_ */
```

`portdrv_common.h` contiene los tipos que comparten los backends y el core:

```c
#ifndef _PORTDRV_COMMON_H_
#define _PORTDRV_COMMON_H_

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>

/* Forward declaration of the backend interface. */
struct portdrv_backend;

/* The softc, visible to the backends so they can install sc_be, etc. */
struct portdrv_softc {
	device_t                     sc_dev;
	const struct portdrv_backend *sc_be;
	void                        *sc_backend_priv;

	struct mtx                   sc_mtx;
	struct cdev                 *sc_cdev;

	/* other common fields ... */
};

#endif /* !_PORTDRV_COMMON_H_ */
```

`portdrv_backend.h` contiene la interfaz y las declaraciones de la instancia canónica de cada backend:

```c
#ifndef _PORTDRV_BACKEND_H_
#define _PORTDRV_BACKEND_H_

#include <sys/types.h>
#include <machine/bus.h>

#include "portdrv_common.h"

struct portdrv_backend {
	const char *name;
	int   (*attach)(struct portdrv_softc *sc);
	void  (*detach)(struct portdrv_softc *sc);
	uint32_t (*read_reg)(struct portdrv_softc *sc, bus_size_t off);
	void     (*write_reg)(struct portdrv_softc *sc, bus_size_t off,
	                      uint32_t val);
	int   (*start_transfer)(struct portdrv_softc *sc,
	                        const void *buf, size_t len);
	int   (*poll_done)(struct portdrv_softc *sc);
};

extern const struct portdrv_backend portdrv_pci_backend;
extern const struct portdrv_backend portdrv_usb_backend;
extern const struct portdrv_backend portdrv_sim_backend;

#endif /* !_PORTDRV_BACKEND_H_ */
```

Estas cabeceras son breves a propósito. Una cabecera que requiere una página para leerla es una cabecera que está ocultando algo. Divide los tipos donde se usan, y mantén cada cabecera libre de prototipos innecesarios.

### El Makefile

El lado del build de este diseño es agradablemente sencillo en FreeBSD. `bsd.kmod.mk` gestiona los módulos multifichero de forma natural. El Makefile tiene este aspecto:

```make
# Makefile for portdrv - Chapter 29 reference driver.

KMOD=	portdrv
SRCS=	portdrv_core.c \
	portdrv_sysctl.c \
	portdrv_ioctl.c \
	portdrv_dma.c

# Backends are selected at build time.
.if defined(PORTDRV_WITH_PCI) && ${PORTDRV_WITH_PCI} == "yes"
SRCS+=	portdrv_pci.c
CFLAGS+= -DPORTDRV_WITH_PCI
.endif

.if defined(PORTDRV_WITH_USB) && ${PORTDRV_WITH_USB} == "yes"
SRCS+=	portdrv_usb.c
CFLAGS+= -DPORTDRV_WITH_USB
.endif

.if defined(PORTDRV_WITH_SIM) && ${PORTDRV_WITH_SIM} == "yes"
SRCS+=	portdrv_sim.c
CFLAGS+= -DPORTDRV_WITH_SIM
.endif

# If no backend was explicitly selected, enable the simulation
# backend so that the driver still builds and loads.
.if !defined(PORTDRV_WITH_PCI) && \
    !defined(PORTDRV_WITH_USB) && \
    !defined(PORTDRV_WITH_SIM)
SRCS+=	portdrv_sim.c
CFLAGS+= -DPORTDRV_WITH_SIM
.endif

SYSDIR?=	/usr/src/sys

.include <bsd.kmod.mk>
```

Compílalo con cualquier combinación de backends:

```sh
make clean
make PORTDRV_WITH_PCI=yes PORTDRV_WITH_SIM=yes
```

El `portdrv.ko` resultante contiene solo los archivos que pediste. Eliminar un backend es tan sencillo como quitar su variable de make. Este es el enfoque de selección en tiempo de compilación, y funciona bien para drivers cuyo conjunto de backends se conoce en tiempo de build.

### Comparación con `/usr/src/sys/modules/uart/Makefile`

Abre `/usr/src/sys/modules/uart/Makefile` y compara. La estructura es similar en espíritu: una lista `SRCS` que nombra cada archivo, más bloques condicionales que seleccionan backends y helpers específicos del hardware según la arquitectura. El build de UART es más elaborado que el nuestro porque el driver admite muchos backends y muchas variantes de hardware, pero la forma es reconocible. Estudiar este archivo junto con el del driver de referencia hará que ambos resulten más comprensibles.

Dedica unos minutos a recorrer el Makefile de UART desde el principio. Observa cómo la lista `SRCS` se construye mediante adiciones, no con una única asignación: `SRCS+= uart_tty.c`, `SRCS+= uart_dev_ns8250.c`, y así sucesivamente. Cada adición puede estar protegida por un condicional que comprueba una macro de arquitectura o un flag de característica. El resultado es un único Makefile que produce builds diferentes en `amd64`, `arm64` y `riscv`, con diferentes conjuntos de backends habilitados en cada uno. Esa es la estructura que querrás que tengan tus propios drivers una vez que crezcan para cubrir más de una plataforma.

### Patrones de Makefile para drivers portables

Unos pocos patrones de Makefile se repiten con tanta frecuencia que merecen un nombre explícito. Verás cada uno de ellos en Makefiles reales de módulos FreeBSD y en el driver de referencia.

La **lista de core incondicional**:

```make
KMOD=	portdrv
SRCS=	portdrv_core.c portdrv_ioctl.c portdrv_sysctl.c
```

Este es siempre el punto de partida: los archivos que se compilan siempre, independientemente de la configuración.

La **lista de backend condicional**:

```make
.if ${MACHINE_CPUARCH} == "amd64" || ${MACHINE_CPUARCH} == "i386"
SRCS+=	portdrv_x86_helpers.c
.endif

.if ${MACHINE} == "arm64"
SRCS+=	portdrv_arm64_helpers.c
.endif
```

Este patrón permite incluir un archivo solo en una arquitectura específica. Es menos común en drivers que en subsistemas del kernel, pero es la herramienta adecuada cuando existe código genuinamente específico de la arquitectura.

El **bloque por característica**:

```make
.if defined(PORTDRV_WITH_DMA) && ${PORTDRV_WITH_DMA} == "yes"
SRCS+=		portdrv_dma.c
CFLAGS+=	-DPORTDRV_WITH_DMA
.endif
```

Cada característica que se puede activar o desactivar tiene un bloque. El bloque añade un archivo fuente y un indicador del preprocesador. Este es el patrón que usarás con más frecuencia.

La **dependencia de cabecera**:

```make
beforebuild: genheader
genheader:
	sh ${.CURDIR}/gen_registers.sh > ${.OBJDIR}/portdrv_regs.h
```

Cuando una cabecera se genera a partir de un archivo de datos (por ejemplo, a partir de una descripción de registros proporcionada por el fabricante), este patrón garantiza que la cabecera esté actualizada antes de que se ejecute el build principal. Úsalo con moderación; cada artefacto generado es un punto de mantenimiento.

El **bloque de disciplina de CFLAGS**:

```make
CFLAGS+=	-Wall
CFLAGS+=	-Wmissing-prototypes
CFLAGS+=	-Wno-sign-compare
CFLAGS+=	-Werror=implicit-function-declaration
```

Esto activa avisos que detectan errores de portabilidad comunes. Trata los nuevos avisos como errores y corrígelos conforme aparezcan. La opción `-Werror=implicit-function-declaration` detecta en particular la clase de error en la que un archivo llama a una función sin haber incluido la cabecera adecuada; en algunas plataformas el compilador asumiría en silencio un tipo de retorno `int` y continuaría, generando un error latente.

La **adición de rutas de inclusión**:

```make
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.CURDIR}/include
CFLAGS+=	-I${SYSDIR}/contrib/portdrv_vendor_headers
```

Cuando el driver tiene su propio directorio de cabeceras, o cuando depende de cabeceras suministradas por el fabricante incorporadas en un área contrib, estas adiciones `-I` son la forma de indicarle al compilador dónde buscar. Mantén la lista corta; cada ruta adicional amplía la superficie de posibles conflictos entre cabeceras.

### Cuando varios módulos comparten código

Una pregunta que surge cuando los drivers se multiplican es cómo compartir código auxiliar entre ellos. Los dos enfoques son:

**Módulo de biblioteca.** Un archivo `.ko` independiente proporciona funciones auxiliares, y los drivers dependientes declaran `MODULE_DEPEND` sobre él. Este enfoque es limpio cuando las funciones auxiliares son sustanciales, pero cada carga del módulo exige cargar primero el módulo auxiliar.

**Archivo fuente compartido.** Varios drivers incluyen el mismo archivo `.c` en sus `SRCS`. Cada driver obtiene su propia copia de las funciones, compilada en su propio `.ko`. Es más sencillo, pero duplica el código en el binario de cada driver.

Para funciones auxiliares de pocas centenas de líneas, el enfoque de archivo fuente compartido suele ser el adecuado. La duplicación es mínima y el build es más sencillo. Para funciones auxiliares de más de mil líneas, el módulo de biblioteca se amortiza, ya que las actualizaciones del código auxiliar afectan a un único módulo y no a cada driver que lo utiliza.

Un tercer enfoque consiste en mover el código auxiliar al kernel base y exportarlo. Esta es la opción correcta cuando el código auxiliar es verdaderamente de propósito general y pertenece al ABI del kernel. Para funciones auxiliares específicas de un driver, es excesivo.

### Ventajas de la división

¿Por qué tomarse tanta molestia? Las ventajas no son cosméticas.

**Compilaciones más rápidas.** Un cambio en `portdrv_ioctl.c` recompila un solo archivo y vuelve a enlazar el módulo. Un driver monolítico recompila todo cada vez.

**Pruebas más sencillas.** El backend de simulación puede cargarse solo, sin hardware alguno, y el núcleo puede ejercitarse completamente. Realizar pruebas unitarias de un driver de este modo amplía considerablemente lo que es posible.

**Superficie de API más limpia.** Cada archivo expone únicamente lo que los demás necesitan. Cuando una función se declara en `portdrv.h`, esa declaración es un compromiso de que la función forma parte de la API interna del driver. Las funciones que no se declaran ahí son implícitamente privadas de su archivo.

**Revisión más sencilla.** Un revisor de código que conozca la estructura puede recorrer un parche de un vistazo mirando qué archivos han cambiado. Un parche que solo toca `portdrv_pci.c` está realizando trabajo específico de PCI. Un parche que toca `portdrv_core.c` está haciendo algo que afecta a todos los backends, y merece un escrutinio más detenido.

**Menor coste al añadir un backend.** Una vez establecida la estructura, un nuevo backend es simplemente un nuevo archivo. En un driver monolítico, un nuevo backend implica reescribir la mitad del archivo.

### Errores habituales al dividir un driver

Algunos errores son fáciles de cometer.

**Mover demasiado poco.** Una división que deja cabeceras específicas del bus en el archivo "core" no ha llegado lo suficientemente lejos. El núcleo solo debería incluir cabeceras de subsistema (`sys/bus.h`, `sys/conf.h`, etc.), no cabeceras específicas del bus. Si te descubres incluyendo `dev/pci/pcivar.h` en el núcleo, es que has dejado lógica específica de PCI donde no debe estar.

**Mover demasiado.** Una división que mueve cada función auxiliar a su propio archivo crea una docena de archivos diminutos y una maraña de dependencias cruzadas. El objetivo es el equilibrio. Si tres funciones se usan siempre juntas y solo tienen sentido juntas, pertenecen al mismo archivo.

**Inclusiones circulares.** Cuando la cabecera A incluye la cabecera B y la cabecera B incluye la cabecera A, el compilador se queja de tipos incompletos. La solución casi siempre es una declaración anticipada (forward declaration) en una de las cabeceras, para que la otra pueda incluirla sin problemas.

**Ruptura silenciosa de la API.** Cuando mueves una función de `portdrv.c` a `portdrv_sysctl.c`, su declaración debe ser visible para cualquier código que la llame. Revisa las advertencias del compilador; una declaración ausente suele aparecer como una advertencia de declaración implícita de función antes de convertirse en un error en tiempo de enlace.

**Entradas olvidadas en `SRCS`.** Un archivo nuevo que no figura en `SRCS` no se compila. El driver compilará y enlazará si tienes mala suerte y todos sus símbolos ya están disponibles en otro lugar, o fallará en el enlace con un misterioso error de símbolo no definido. Cuando añadas un archivo, inclúyelo siempre en `SRCS` en el mismo commit.

### Higiene de cabeceras y grafos de inclusión

Las líneas `#include` de un archivo fuente son una ventana a sus dependencias. Un archivo que incluye `dev/pci/pcivar.h` está declarando, de la forma más clara que puede un programador de C, que conoce PCI. Un archivo que solo incluye `sys/param.h`, `sys/bus.h`, `sys/malloc.h` y las propias cabeceras del driver está declarando independencia de cualquier bus específico. La disciplina de mantener las listas de inclusión pequeñas y enfocadas se denomina higiene de cabeceras, y es la silenciosa compañera de la interfaz del backend.

Tres reglas mantienen limpios los grafos de inclusión.

**Regla uno: incluye lo que usas, declara lo que no.** Si un archivo usa `bus_dma_tag_create`, debe incluir `<sys/bus.h>` y `<machine/bus.h>`. Si simplemente recibe un puntero a un `bus_dma_tag_t` pero no lo desreferencia, puede bastar con una declaración anticipada en una cabecera. Incluir en exceso arrastra dependencias innecesarias; incluir en defecto provoca advertencias de declaración implícita de función. Ambos problemas tienen solución, pero el objetivo correcto es incluir exactamente lo necesario.

**Regla dos: incluye la cabecera mínima que te proporciona lo que necesitas.** `<sys/param.h>` es un comodín excelente, pero arrastra un grafo transitivo muy grande. Si solo necesitas tipos enteros de ancho fijo, puede bastar con `<sys/types.h>`. El build funciona en ambos casos; el tiempo de compilación y el grafo de dependencias se reducen cuando eres disciplinado.

**Regla tres: nunca confíes en las inclusiones transitivas.** Si tu archivo `.c` usa `memset`, debe incluir `<sys/libkern.h>` (o `<string.h>` en el espacio de usuario), aunque otra cabecera ya lo haya incorporado. Las inclusiones transitivas no forman parte del contrato; cambian cuando se refactoriza otra cabecera. Una inclusión explícita es robusta; una implícita es un error en espera de materializarse.

Cuando dividas un driver siguiendo la estructura de esta sección, revisa de nuevo las inclusiones de cada archivo `.c`. Elimina las que no necesitas; añade las que estabas obteniendo de forma transitiva. Tras esta revisión, cada archivo debería tener una lista de inclusiones que se corresponda con lo que realmente hace. El resultado es un árbol de código fuente en el que un cambio en una cabecera no se propaga de forma inesperada por el resto.

Un modelo mental útil es considerar el grafo de inclusión como el **equivalente en tiempo de compilación de la interfaz del backend**. Ambos declaran qué depende de qué. Ambos se mantienen limpios gracias a la disciplina. Ambos se amortizan cuando el driver se refactoriza. Si la interfaz del backend es el grafo de dependencias del código en ejecución, el grafo de inclusión es el grafo de dependencias del código fuente, y ambos merecen el mismo cuidado.

### Gestión de `CFLAGS` y visibilidad de símbolos

Un driver de múltiples archivos acaba necesitando algunos parámetros de configuración en tiempo de compilación para su propia compilación. Estos suelen tomar la forma de adiciones a `CFLAGS` en el Makefile:

```make
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-Wall -Wmissing-prototypes
CFLAGS+=	-DPORTDRV_VERSION='"2.0"'
```

La primera línea hace que las propias cabeceras del driver sean localizables; la segunda activa advertencias que detectan errores habituales de portabilidad; la tercera incrusta una cadena de versión directamente en el módulo compilado. Cada flag es pequeño; juntos dan al driver una pequeña superficie de control que no se filtra al código fuente.

Ten cuidado con la visibilidad de los símbolos. En un módulo del kernel, toda función no estática es un símbolo potencialmente exportado. Si dos drivers definen una función llamada `portdrv_helper`, el kernel aceptará el que se cargue primero y rechazará el segundo, produciendo un error desconcertante. Añade un prefijo a tus símbolos: llama a las funciones `portdrv_core_attach`, no `attach`; llama a las funciones `portdrv_pci_probe`, no `probe`. Las funciones estáticas nunca se convierten en símbolos y pueden nombrarse de forma más concisa.

Esto no es solo una cuestión de estilo. En FreeBSD, la tabla de símbolos de un módulo del kernel se comparte con todos los demás módulos cargados. Un nombre sin prefijo colisiona con todo lo que el kernel haya cargado alguna vez. Usa un prefijo coherente y prefiere `static` siempre que la función no necesite ser visible fuera de su archivo.

### Un grafo de dependencias en papel

Un buen ejercicio cuando divides un driver por primera vez es dibujar su grafo de inclusión en papel. Una caja para cada archivo; una flecha del archivo A al archivo B cuando A incluye B. El grafo debe ser acíclico, fluir hacia abajo desde los archivos fuente hasta las cabeceras más primitivas, y no contener aristas inesperadas.

Para el driver de referencia `portdrv`, el grafo tiene un aspecto aproximadamente así:

```text
portdrv_core.c
	|
	+-> portdrv.h
	+-> portdrv_common.h
	+-> portdrv_backend.h  ---> portdrv_common.h
	+-> <sys/param.h>, <sys/bus.h>, <sys/malloc.h>, ...

portdrv_pci.c
	|
	+-> portdrv.h
	+-> portdrv_common.h
	+-> portdrv_backend.h
	+-> <dev/pci/pcivar.h>, <dev/pci/pcireg.h>
	+-> <sys/bus.h>, <machine/bus.h>, <sys/rman.h>, ...

portdrv_sim.c
	|
	+-> portdrv.h
	+-> portdrv_common.h
	+-> portdrv_backend.h
	+-> <sys/malloc.h>
```

El backend de PCI es el único archivo que accede a `dev/pci/`. El núcleo es el único archivo que incorpora las cabeceras universales de subsistema. El backend de simulación es el más ligero de los tres porque no se comunica con hardware real.

Dibuja este grafo antes de hacer commit de una nueva estructura de archivos. Si una flecha va en dirección incorrecta, corrige las inclusiones antes de escribir más código. Un grafo que fluye hacia abajo es señal de un buen diseño por capas; un grafo con ciclos es señal de un diseño que necesita reconsiderarse.

### Análisis estático y revisiones continuas

Los drivers de múltiples archivos se benefician de una pequeña dosis de análisis estático. Las dos herramientas que se amortizan casi de inmediato son:

**`make -k -j buildkernel`** con las advertencias activadas. El build de FreeBSD suele estar limpio, pero un archivo nuevo frecuentemente expone un prototipo olvidado, una variable no utilizada o una discrepancia entre tipos con y sin signo. Lee cada advertencia; corrige cada advertencia.

**`cppcheck`** o **`scan-build`** sobre los fuentes del driver. Ninguna es perfecta, pero cada una detecta una clase de error que el compilador no detecta. Un free olvidado, la desreferencia de un puntero posiblemente NULL, una rama muerta y un use-after-free son hallazgos habituales. Ejecuta una de ellas periódicamente, no necesariamente en cada commit.

Para un driver grande, considera ejecutar `clang-tidy` con un conjunto moderado de comprobaciones. Para un driver pequeño, las advertencias del compilador más una revisión humana ocasional suelen ser suficientes.

Un driver de múltiples archivos es más fácil de revisar que uno monolítico precisamente porque cada archivo tiene un ámbito reducido. Los revisores pueden tomar un archivo cada vez, comprender su responsabilidad y verificar su conformidad con la estructura. Un revisor que conozca las convenciones puede recorrer un parche grande con rapidez porque el papel de cada archivo resulta evidente por su lugar en la estructura. Esta es la rentabilidad en coste de revisión mencionada antes, y se multiplica a lo largo de la vida del driver.

### Cerrando la sección 4

Ahora dispones de una estructura de archivos concreta que puedes aplicar a cualquier driver: un archivo núcleo, una pequeña familia de archivos de backend, una cabecera por responsabilidad, y un Makefile que te permite seleccionar los backends en tiempo de compilación. La estructura no es obligatoria, pero se corresponde con lo que usan los drivers reales de FreeBSD en la práctica, y escala desde drivers pequeños hasta drivers grandes sin necesidad de reorganización. La higiene disciplinada de cabeceras, un grafo de inclusión limpio y un prefijado coherente de símbolos son las compañeras de la estructura; juntas hacen del árbol de código fuente del driver un lugar agradable en el que moverse.

La estructura también enseña al lector dónde buscar. Un nuevo mantenedor que conozca las convenciones puede navegar por el driver desde el primer día, en lugar del décimo. A lo largo de la vida de un proyecto longevo, eso vale más de lo que parece.

La sección 5 pasa de la organización de archivos al tema que resulta más fácil de equivocar sutilmente: dar soporte a múltiples arquitecturas de CPU. El mismo driver debe compilar en `amd64`, `arm64`, `riscv` y el resto de los ports de FreeBSD, y debe comportarse correctamente en cada uno de ellos. Las herramientas tienen que ver casi todas con el orden de bytes (endianness), la alineación y el tamaño de palabra, y usarlas correctamente requiere menos esfuerzo del que parece.

## Sección 5: Soporte de múltiples arquitecturas

La portabilidad arquitectónica es la forma de portabilidad que requiere menos código y más atención. El kernel y el compilador ya hacen la mayor parte del trabajo por ti; tu tarea como autor de un driver no es añadir ingeniosidad sino evitar introducir errores. Cada error arquitectónico habitual en el código de un driver tiene un idioma FreeBSD correspondiente que lo previene. Esta sección presenta esos idiomas y muestra cómo usarlos.

Los tres ejes en los que las arquitecturas difieren de formas que los drivers pueden sentir directamente son el endianness, la alineación y el tamaño de palabra. Los abordaremos en orden y, a continuación, veremos cómo probar la portabilidad arquitectónica sin necesidad de poseer hardware para cada arquitectura.

### Cómo varía `bus_space` entre plataformas

Para entender por qué importa esta abstracción, conviene echar un vistazo rápido a cómo se implementa en cada una de las arquitecturas que FreeBSD soporta. El mecanismo es distinto en cada una, pero la interfaz contra la que programas es siempre la misma.

En `amd64`, el tag es simplemente una etiqueta de tipo que distingue el I/O mapeado en memoria de los puertos de I/O, y el handle es la dirección virtual de la región mapeada. Una llamada a `bus_space_read_4` en `amd64` se convierte, internamente, en una instrucción de carga hacia la dirección mapeada. La llamada es un envoltorio fino sobre una carga directa; el coste de indirección es prácticamente nulo.

En `arm64`, el tag es un puntero a una tabla de punteros de función, y el handle es la dirección base de la región mapeada. Una llamada a `bus_space_read_4` en `arm64` se convierte en una llamada indirecta a través de esa tabla de punteros de función. La indirección es necesaria porque distintas plataformas ARM pueden mapear la memoria con atributos diferentes de cacheabilidad y ordenación, y la tabla de funciones codifica esos detalles para cada plataforma. El coste es una única llamada indirecta, que puede medirse en bucles muy ajustados pero resulta despreciable en un driver típico.

En `riscv`, el mecanismo es similar al de `arm64`, con una justificación parecida. En versiones antiguas de `powerpc`, el tag y el handle codifican la configuración de endianness, de modo que una llamada a `bus_space_read_4` en un bus big-endian conectado a una CPU little-endian realiza el intercambio de bytes correcto sin que el driver lo sepa.

Todo esto es invisible para el driver. Tu código llama a `bus_read_4(res, offset)` y obtiene el valor correcto, en cada arquitectura y en cualquier configuración de bus. El precio es que no puedes saltarte la API; en el momento en que bajas a un puntero directo, pierdes toda esta maquinaria y tu driver solo funcionará en una plataforma concreta.

### La endianness en tres preguntas

La endianness es el orden en que se almacenan en memoria los bytes de un valor multibyte. En un sistema little-endian como `amd64`, `arm64` o `riscv`, el byte menos significativo de un valor de 32 bits se almacena primero en memoria y el más significativo al final. En un sistema big-endian como algunas configuraciones de `powerpc`, el orden es el inverso. La CPU no ve los bytes de forma individual en ninguno de los dos casos; lee la palabra completa y la interpreta según la endianness del sistema. El problema para los drivers es que el hardware no siempre comparte el punto de vista de la CPU.

Hay tres preguntas que todo autor de drivers debe hacerse, y tres clases de respuesta.

**Pregunta 1: ¿Tiene el dispositivo un orden de bytes nativo?** Muchos dispositivos lo tienen. Un dispositivo PCI puede especificar que un registro concreto contiene un valor de 32 bits en orden little-endian. Las tramas Ethernet, en cambio, son big-endian. Cuando el orden de bytes del dispositivo difiere del de la CPU, el driver debe convertir entre ambos de forma explícita.

**Pregunta 2: ¿Realiza el bus alguna conversión implícita?** Normalmente no. `bus_space_read_4` devuelve el valor de 32 bits tal como lo ve el host, en el orden de bytes del host. Si el dispositivo almacenó ese valor en un orden de bytes diferente, la conversión es responsabilidad del driver. Lo mismo ocurre con los buffers DMA leídos y escritos a través de `bus_dma`. Los bytes del buffer son los que escribió el dispositivo, en el orden que este prefiera; tu código debe interpretarlos correctamente.

**Pregunta 3: ¿En qué dirección estoy convirtiendo?** Para convertir del dispositivo al host, usa `le32toh`, `be32toh` y sus equivalentes de 16 y 64 bits. Para convertir del host al dispositivo, usa `htole32`, `htobe32` y sus equivalentes. El convenio de nombres es sencillo: la primera parte indica el origen, la segunda el destino, y `h` representa el host.

### Los helpers de endianness en detalle

Abre `/usr/src/sys/sys/_endian.h` y observa cómo están definidos los helpers. En un host little-endian, `htole32(x)` es simplemente `(uint32_t)(x)` porque no hace falta ningún intercambio de bytes, y `htobe32(x)` es `__bswap32(x)` porque el dispositivo espera el orden contrario. En un host big-endian, el patrón se invierte: `htobe32(x)` es la identidad y `htole32(x)` realiza el intercambio. Esto significa que tu código puede llamar al mismo helper en cada arquitectura y el comportamiento correcto ocurre por debajo.

El conjunto completo de helpers que usarás en código de driver es:

```c
htole16(x), htole32(x), htole64(x)
htobe16(x), htobe32(x), htobe64(x)
le16toh(x), le32toh(x), le64toh(x)
be16toh(x), be32toh(x), be64toh(x)
```

Además de los atajos de orden de bytes de red `htons`, `htonl`, `ntohs`, `ntohl`, equivalentes a la familia `htobe`/`betoh` para valores de 16 y 32 bits.

Úsalos siempre que almacenes un valor multibyte en un registro de dispositivo o en un buffer DMA, o cuando leas uno. Nunca escribas:

```c
sc->sc_regs->control = 0x12345678;  /* Wrong if the device expects LE. */
```

En su lugar, escribe:

```c
widget_write_reg(sc, REG_CONTROL, htole32(0x12345678));
```

El coste en tiempo de ejecución es nulo en un host little-endian, porque el compilador convierte el helper en una operación sin efecto (no-op). El beneficio en un host big-endian es la corrección del código.

### Una regla mental sencilla

Cuando enseño este patrón a nuevos autores de drivers, utilizo una regla que detecta la mayoría de los errores antes de que ocurran: **todo valor multibyte que salga de la CPU o entre en ella a través del hardware debe pasar por un helper de endianness**. Registros. Buffers DMA. Cabeceras de paquetes. Anillos de descriptores. Absolutamente todos.

Si ves un patrón `*ptr = value` o `value = *ptr` sobre una cantidad multibyte visible por el dispositivo, y los extremos no usan un helper de endianness, eso es un bug o una casualidad afortunada; la única forma de saber cuál es conocer la arquitectura en la que se ejecuta el código.

La misma regla, aplicada de forma coherente, hace que portar un driver a una plataforma big-endian sea casi trivial. Si los helpers de endianness estaban desde el principio, el cambio de plataforma no afecta al código. Si no lo estaban, el proceso se convierte en una búsqueda exhaustiva por todo el driver de cada lectura y escritura que haya tocado memoria del dispositivo, y cada una de ellas es un bug potencial.

### La alineación y el macro NO_STRICT_ALIGNMENT

El segundo eje arquitectónico es la alineación. Un valor de 32 bits está *alineado* en una dirección que es múltiplo de cuatro. En `amd64`, los accesos no alineados están permitidos y son solo ligeramente más lentos que los alineados. En algunos núcleos ARM, un acceso no alineado puede producir un fallo de bus o corromper datos de forma silenciosa, según la instrucción y la configuración específicas.

La política del kernel es que el código de driver debe generar accesos alineados. Si conviertes un puntero de buffer a `uint32_t *` y lo desreferencias, el buffer debe estar alineado a cuatro bytes. La mayoría de los asignadores del kernel proporcionan memoria alineada de forma natural, por lo que esto suele ocurrir automáticamente. Se complica al analizar un protocolo de comunicaciones que no alinea sus campos, como una trama Ethernet cuya cabecera IP está desalineada dos bytes debido al tamaño impar de la cabecera Ethernet.

FreeBSD expone el macro `__NO_STRICT_ALIGNMENT`, definido en `/usr/src/sys/x86/include/_types.h` y sus equivalentes en otras arquitecturas, que los drivers comprueban ocasionalmente para ver si la plataforma tolera accesos no alineados. Para código de driver de propósito general, no deberías depender de él. En su lugar, usa accesores byte a byte, o `memcpy`, para copiar valores multibyte fuera de la memoria no alineada antes de interpretarlos. El compilador es bueno optimizando `memcpy(&val, ptr, sizeof(val))` como una única carga alineada en arquitecturas que lo permiten, y como una carga byte a byte en las que lo necesitan.

Un patrón típico para leer de forma segura un valor de 32 bits no alineado desde un buffer de bytes es:

```c
static inline uint32_t
load_unaligned_le32(const uint8_t *p)
{
	uint32_t v;
	memcpy(&v, p, sizeof(v));
	return (le32toh(v));
}
```

Esto funciona en cualquier arquitectura. El `memcpy` gestiona la alineación; el `le32toh` gestiona la endianness. Usa este tipo de helper siempre que extraigas un valor multibyte de un formato de protocolo de red o de un buffer del dispositivo que pueda no estar alineado.

### El tamaño de palabra: 32 frente a 64 bits

El tercer eje arquitectónico es el tamaño de palabra. En una plataforma de 32 bits, un puntero ocupa cuatro bytes y `long` tiene típicamente cuatro bytes. En una plataforma de 64 bits, ambos tienen ocho bytes. La mayor parte del kernel de FreeBSD y todas las arquitecturas comunes actuales son de 64 bits, pero no todas, e incluso las plataformas de 64 bits ejecutan en ocasiones código de compatibilidad de 32 bits.

El error que hay que evitar es usar `int` o `long` cuando el tamaño importa. Un valor de registro tiene un número concreto de bits, no "lo que `int` resulte ser". Si un registro es de 32 bits, usa `uint32_t`. Si es de 16 bits, usa `uint16_t`. Esta regla es fácil de recordar y elimina por completo toda una clase de bugs.

FreeBSD expone estos tipos a través de `/usr/src/sys/sys/types.h` y sus inclusiones indirectas. Los typedef de anchura fija estándar son:

```c
int8_t,   uint8_t
int16_t,  uint16_t
int32_t,  uint32_t
int64_t,  uint64_t
```

El kernel también expone `intmax_t`, `uintmax_t`, `intptr_t` y `uintptr_t` cuando necesitas almacenar un entero que debe tener al menos la misma anchura que cualquier otro entero, o que debe coincidir con el ancho de un puntero.

No uses `u_int32_t` ni `u_char` en código nuevo. Son nombres heredados que siguen existiendo por compatibilidad con versiones anteriores, pero no son el estilo preferido. El convenio en el código de driver moderno de FreeBSD es la familia `uint32_t`.

### Los tipos de dirección de bus

Una familia de tipos relacionada regula las direcciones que usan el dispositivo y el bus. Están definidos por arquitectura en `/usr/src/sys/amd64/include/_bus.h`, `/usr/src/sys/arm64/include/_bus.h` y sus equivalentes. Los tipos principales son:

- `bus_addr_t`: una dirección de bus, típicamente `uint64_t` en plataformas de 64 bits.
- `bus_size_t`: un tamaño en el bus, con la misma anchura subyacente que `bus_addr_t`.
- `bus_space_tag_t`: la etiqueta opaca que se pasa a las funciones `bus_space_*`.
- `bus_space_handle_t`: el handle opaco hacia la región mapeada.
- `bus_dma_tag_t`: la etiqueta opaca que usa `bus_dma`.
- `bus_dmamap_t`: el handle opaco del mapa DMA.

Usar estos tipos en lugar de `uint64_t` o `void *` directos es la forma en que el kernel aplica la abstracción arquitectónica. En `amd64`, pueden ser enteros simples; en `arm64`, algunos son punteros a estructuras. Tu código compila de la misma manera en todas partes y funciona correctamente en cada plataforma.

### Pruebas en arquitecturas emuladas

Una de las preguntas más prácticas es: si solo tienes una máquina `amd64`, ¿cómo compruebas que el driver funciona en `arm64` o en un sistema big-endian? La respuesta es la emulación.

QEMU, que FreeBSD soporta bien, puede ejecutar huéspedes `arm64` en un host `amd64`. El rendimiento es inferior al nativo, pero suficiente para pruebas funcionales. Un flujo de trabajo sencillo es:

1. Instala la versión de FreeBSD para `arm64` en una imagen QEMU.
2. Arranca la imagen en QEMU con disco y RAM suficientes.
3. Compila el driver en modo cruzado (o compílalo dentro del huésped).
4. Carga el módulo y ejecuta tus pruebas.

La infraestructura de build de FreeBSD admite compilación cruzada mediante `make buildkernel TARGET=arm64 TARGET_ARCH=aarch64`. Para los módulos del kernel, se aplican las mismas variables `TARGET` y `TARGET_ARCH`. El resultado es un módulo para `arm64` que puedes copiar en el huésped `arm64` y probar allí.

Para pruebas en big-endian, existen los targets `powerpc64` y `powerpc` de QEMU, y FreeBSD tiene versiones para ambos. Las pruebas en PowerPC big-endian son especialmente valiosas porque son la forma más rápida de encontrar bugs de endianness ocultos: un driver que funciona perfectamente en `amd64` pero corrompe cada registro en PowerPC casi con toda seguridad se ha olvidado de algún `htole32`.

Para el trabajo diario, basta con ejecutar el driver una vez en un huésped de arquitectura diferente como parte de tu proceso de publicación. No hace falta hacerlo en cada commit. Pero tampoco deberías omitirlo por completo, porque los bugs que detecta son los que de otro modo encontrarían tus usuarios.

### Caso práctico: un anillo de descriptores DMA

Un ejemplo concreto de cómo se combinan estos patrones es un anillo de descriptores DMA. Muchos dispositivos exponen un anillo de descriptores en memoria compartida: cada descriptor describe una transferencia, y el dispositivo recorre el anillo de forma autónoma a medida que atiende las transferencias. El anillo es un ejemplo perfecto de estructura compartida donde la endianness, la alineación, el tamaño de palabra y la coherencia de caché interactúan entre sí.

Un descriptor típico podría tener este aspecto en C:

```c
struct portdrv_desc {
	uint64_t addr;      /* bus address of the payload */
	uint32_t len;       /* payload length in bytes */
	uint16_t flags;     /* control bits */
	uint16_t status;    /* completion status */
};
```

Cuatro consideraciones se aplican a esta estructura.

**Endianness.** El dispositivo lee y escribe estos campos. El orden de bytes en que los interpreta viene dado por el datasheet. Si el dispositivo espera little-endian, cada escritura desde la CPU debe pasar por `htole64` o `htole32` según corresponda, y cada lectura del dispositivo debe pasar por `le64toh` o `le32toh`.

**Alineación.** El dispositivo lee el descriptor completo en una sola transacción, por lo que el descriptor debe estar alineado al menos en su frontera natural. Para este layout, son 8 bytes debido al campo `addr` de 64 bits. La llamada a `bus_dma_tag_create` que asigna el anillo debe establecer `alignment` en al menos 8.

**Tamaño de palabra.** El campo `addr` es de 64 bits porque el dispositivo puede necesitar acceder a cualquier dirección física en un sistema de 64 bits. En un sistema de 32 bits con un dispositivo exclusivamente de 32 bits, podrías declararlo `uint32_t`, pero la elección portable es `uint64_t` con los bits superiores a cero cuando no se usen.

**Coherencia de caché.** Después de que la CPU escriba un descriptor y antes de que el dispositivo lo lea, los cachés de la CPU deben volcarse a memoria para que el dispositivo vea la actualización. Después de que el dispositivo escriba el campo de estado y antes de que la CPU lo lea, los cachés de la CPU para la memoria del anillo deben invalidarse para no leer un valor obsoleto. La llamada a `bus_dmamap_sync` con `BUS_DMASYNC_PREWRITE` gestiona el primer caso; con `BUS_DMASYNC_POSTREAD` gestiona el segundo.

Combinando todo esto, el código para encolar un descriptor tiene un aspecto similar a este:

```c
static int
portdrv_enqueue(struct portdrv_softc *sc, bus_addr_t payload_addr, size_t len)
{
	struct portdrv_desc *d;
	int idx;

	/* Pick a ring slot. */
	idx = sc->sc_tx_head;
	d = &sc->sc_tx_ring[idx];

	/* Populate the descriptor in device byte order. */
	d->addr   = htole64(payload_addr);
	d->len    = htole32((uint32_t)len);
	d->flags  = htole16(DESC_FLAG_VALID);
	d->status = 0;

	/* Ensure the CPU's writes reach memory before the device reads. */
	bus_dmamap_sync(sc->sc_ring_tag, sc->sc_ring_map,
	    BUS_DMASYNC_PREWRITE);

	/* Notify the device. Use a barrier to ensure the notification
	 * is seen after the descriptor writes. */
	bus_barrier(sc->sc_bar, 0, 0,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	portdrv_write_reg(sc, REG_TX_DOORBELL, (uint32_t)idx);

	sc->sc_tx_head = (idx + 1) % sc->sc_ring_size;
	return (0);
}
```

Las cuatro consideraciones son visibles en estas pocas líneas. Las llamadas a `htole*` gestionan la endianness. La llamada a `bus_dmamap_sync` gestiona la coherencia de caché. La llamada a `bus_barrier` gestiona el orden. El `uint64_t` para `addr` gestiona el tamaño de palabra.

Y, lo que es crucial, este código es idéntico en `amd64`, `arm64` y `powerpc64`. Los helpers se convierten en la operación correcta en cada plataforma por debajo. El autor del driver escribe una sola versión y funciona en todas partes.

### Un layout de registros que merece hacerse con cuidado

Aquí tienes un layout de registros de un dispositivo hipotético, escrito como una estructura C:

```c
struct widget_regs {
	uint32_t control;       /* offset 0x00 */
	uint32_t status;        /* offset 0x04 */
	uint32_t data;          /* offset 0x08 */
	uint8_t  pad[4];        /* offset 0x0C: padding */
	uint64_t dma_addr;      /* offset 0x10 */
	uint16_t len;           /* offset 0x18 */
	uint16_t flags;         /* offset 0x1A */
};
```

Hay dos aspectos de esto que conviene tener en cuenta. En primer lugar, el padding es explícito. Si el bloque de registros tiene un hueco entre `data` y `dma_addr`, escribe un miembro `pad[]`. No confíes en que el compilador inserte el padding del tamaño correcto, porque el compilador no sabe nada sobre el dispositivo. En segundo lugar, los tipos coinciden exactamente con el ancho de cada registro. Un campo `len` de 16 bits es `uint16_t`, no `int` ni `u_int`.

Sin embargo, no leas ni escribas estos registros haciendo un cast de un puntero a la estructura. El patrón

```c
struct widget_regs *r = (void *)sc->sc_regs_vaddr;
r->control = htole32(value);
```

es una trampa de portabilidad. En `amd64` funciona. En `arm64` puede funcionar o puede producir un fallo, dependiendo de la alineación del mapeo. En algunos cores ARM puede producir un comportamiento específico de la implementación. El kernel proporciona `bus_space` por una razón; úsalo.

La estructura anterior es útil para la documentación y para los nombres de constantes, pero el código debe acceder a los registros a través de funciones de acceso que usen `bus_space` o `bus_read_*`/`bus_write_*`.

### Uso de los helpers más recientes `bus_read_*`/`bus_write_*`

FreeBSD proporciona una familia de funciones algo más cómoda: `bus_read_1`, `bus_read_2`, `bus_read_4`, `bus_read_8` y sus equivalentes `bus_write_*`. Reciben un `struct resource *` en lugar del par (`tag`, `handle`), por lo que resultan más concisas en el punto de llamada. Internamente utilizan `bus_space` por debajo. Para los drivers modernos son el idioma preferido.

```c
uint32_t v = bus_read_4(sc->sc_bar, REG_CONTROL);
bus_write_4(sc->sc_bar, REG_CONTROL, v | CTRL_START);
```

Estas funciones están definidas en `/usr/src/sys/sys/bus.h`. Úsalas en lugar de las formas más antiguas `bus_space_*` cuando escribas código nuevo; ambas funcionan correctamente en todas las arquitecturas soportadas.

### Errores frecuentes al dar soporte a múltiples arquitecturas

A continuación se presenta una breve lista de errores comunes, cada uno de los cuales afectaría a un driver descuidado en al menos una plataforma de FreeBSD.

**Usar `int` para el ancho de un registro.** Si el registro es de 32 bits, usa `uint32_t`. Un `int` tiene el tamaño correcto en todas las arquitecturas de FreeBSD actuales, pero es un tipo con signo, y los patrones de bits con el bit más significativo activo se convierten en números negativos. Esto provoca problemas cuando comparas el valor o lo desplazas.

**Hacer cast de punteros a memoria de dispositivo.** Escribir a través de un puntero `volatile uint32_t *` a una región mapeada funciona en algunas arquitecturas y en otras no. Usa `bus_read_*`/`bus_write_*`. Siempre.

**Empaquetar sin atributos de empaquetado.** Si realmente debes usar un struct para describir un wire-format layout, usa `__packed` para asegurarte de que el compilador no inserte relleno de alineación. Sin embargo, ten en cuenta que acceder a los campos de un struct empaquetado está sujeto a problemas de alineación en arquitecturas con restricciones estrictas de alineación. Un patrón más seguro es leer los campos de un buffer de bytes con `memcpy` y los helpers de endianness, tal como se mostró antes.

**Asumir que `sizeof(long) == 4` o `sizeof(long) == 8`.** En plataformas de 32 bits, `long` ocupa 4 bytes; en plataformas de 64 bits, ocupa 8. Si el tamaño importa, usa `uint32_t` o `uint64_t` de forma explícita.

**Olvidarse de las plataformas de 32 bits.** Aunque el mundo principal de FreeBSD es en gran parte de 64 bits, siguen existiendo despliegues de ARM de 32 bits, despliegues de MIPS de 32 bits y despliegues heredados de `i386`. Probar únicamente en `amd64` no garantiza la portabilidad. Como mínimo, compila de forma cruzada para un destino de 32 bits de manera periódica.

### Barreras de memoria y ordenación

Los accesos a registros y memoria no siempre llegan a sus destinos en el orden en que tu código los escribe. Los CPUs modernos reordenan cargas y almacenamientos como optimización, y lo mismo hacen los puentes de bus y los controladores de memoria. En las configuraciones más comunes de `amd64`, la reordenación del hardware es suficientemente conservadora como para que los drivers raramente la noten. En `arm64`, `riscv` y otras arquitecturas con ordenación débil, la reordenación es más agresiva, y un driver que depende de un orden específico en los accesos a registros puede fallar de manera misteriosa si no informa al hardware de esa dependencia.

La herramienta para expresar la ordenación es la **barrera de memoria**. FreeBSD proporciona dos familias:

- `bus_space_barrier(tag, handle, offset, length, flags)` para la ordenación con respecto a los accesos al dispositivo. El argumento `flags` es una combinación de `BUS_SPACE_BARRIER_READ` y `BUS_SPACE_BARRIER_WRITE`.
- `atomic_thread_fence_rel`, `atomic_thread_fence_acq` y funciones relacionadas para la ordenación con respecto a la memoria normal. Se encuentran en `/usr/src/sys/sys/atomic_common.h` y en los archivos atómicos específicos de cada arquitectura.

Un uso típico de `bus_space_barrier` se produce cuando armas una interrupción después de configurar un descriptor DMA:

```c
/* Program the DMA descriptor. */
portdrv_write_reg(sc, REG_DMA_ADDR_LO, htole32((uint32_t)addr));
portdrv_write_reg(sc, REG_DMA_ADDR_HI, htole32((uint32_t)(addr >> 32)));
portdrv_write_reg(sc, REG_DMA_LEN, htole32(len));

/* Ensure the descriptor writes are visible before we arm the IRQ. */
bus_barrier(sc->sc_bar, 0, 0,
    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

/* Now it is safe to tell the device to start. */
portdrv_write_reg(sc, REG_DMA_CTL, DMA_CTL_START);
```

En una arquitectura con ordenación fuerte como `amd64`, la llamada a `bus_barrier` es prácticamente gratuita y la barrera está implícita en la mayoría de los casos. En `arm64`, la barrera es una instrucción real que impide que el CPU reordene las escrituras en los registros, y sin ella el dispositivo podría ver el registro `DMA_CTL_START` escrito antes que los registros de dirección, lo que casi con toda seguridad es un error.

Usa barreras siempre que tu código dependa de un orden específico en los accesos a registros, especialmente en el traspaso entre el CPU y el dispositivo. Consulta el datasheet para conocer los casos en que se requiere un orden de escritura específico, y rodea esas secciones con barreras. Omitirlas es la forma más rápida de escribir un driver que funciona en tu mesa y falla en producción en una máquina diferente.

### Coherencia de caché y `bus_dmamap_sync`

El tercer problema de ordenación es la coherencia de caché. En algunas arquitecturas, el CPU tiene cachés que no son automáticamente coherentes con DMA. Si escribes en un buffer DMA desde el CPU y luego le indicas al dispositivo que lo lea, el dispositivo puede ver datos obsoletos que todavía están en la caché del CPU. A la inversa, si el dispositivo escribe en un buffer DMA, el CPU puede leer datos obsoletos que se almacenaron en caché antes de la escritura.

FreeBSD gestiona esto a través de `bus_dmamap_sync`. Después de escribir en un buffer que el dispositivo va a leer, llamas a:

```c
bus_dmamap_sync(sc->sc_dmat, sc->sc_dmap, BUS_DMASYNC_PREWRITE);
```

Después de que el dispositivo haya escrito en un buffer que el CPU va a leer, llamas a:

```c
bus_dmamap_sync(sc->sc_dmat, sc->sc_dmap, BUS_DMASYNC_POSTREAD);
```

En `amd64`, donde las cachés son coherentes con DMA, estas llamadas suelen ser no-ops. En arquitecturas donde no son coherentes, `bus_dmamap_sync` emite la operación correcta de flush o invalidación de caché. Un driver que usa `bus_dma` pero omite estas llamadas sync funciona en `amd64` y falla en otras arquitecturas.

Al igual que con los helpers de endianness, la regla es uniforme: todo buffer DMA compartido entre el CPU y el dispositivo debe pasar por `bus_dmamap_sync` en el traspaso. El buffer va del CPU al dispositivo: `BUS_DMASYNC_PREWRITE`. El buffer vuelve del dispositivo al CPU: `BUS_DMASYNC_POSTREAD`. Las llamadas sync no tienen coste en sistemas coherentes y son esenciales en los no coherentes. Úsalas de forma consistente, y la portabilidad arquitectónica para los buffers DMA se consigue prácticamente sin esfuerzo adicional.

### Bounce buffers y límites de dirección

Algunos dispositivos no pueden acceder a todas las direcciones de la memoria física del sistema. Un dispositivo de 32 bits en un sistema de 64 bits solo puede direccionar los primeros cuatro gigabytes de memoria física; cualquier dirección por encima de ese límite es inalcanzable. El componente `bus_dma` de FreeBSD gestiona esto mediante **bounce buffers**: si la dirección física de un buffer está fuera del alcance del dispositivo, `bus_dma` asigna un buffer temporal que sí está dentro del alcance, copia los datos entre el buffer real y el bounce buffer, y le presenta al dispositivo la dirección del bounce buffer.

Los bounce buffers son invisibles para el driver si este usa `bus_dma` correctamente. Los campos `lowaddr` y `highaddr` de `bus_dma_tag_create` le indican al kernel qué puede y qué no puede alcanzar el dispositivo. Si el driver establece `lowaddr` en `BUS_SPACE_MAXADDR_32BIT`, el kernel usará bounce buffers para cualquier buffer por encima de 4 GB. Las llamadas a `bus_dmamap_sync` gestionan la copia automáticamente.

Los bounce buffers no son gratuitos. Consumen memoria, implican una copia por operación y añaden latencia. Los dispositivos bien diseñados los evitan con soporte de direccionamiento de 64 bits. Sin embargo, el autor de un driver no tiene control sobre el ancho de dirección del dispositivo; la única decisión relevante para la portabilidad es declarar los límites correctamente y dejar que `bus_dma` se encargue del resto. Un driver que miente sobre los límites del dispositivo corromperá la memoria de forma silenciosa en un sistema grande. Un driver que dice la verdad funcionará en cualquier sistema, con bounce buffers cuando sea necesario.

### Pruebas de humo en distintas arquitecturas

La portabilidad arquitectónica se verifica ejecutando el driver en una arquitectura diferente. Incluso una sola prueba de este tipo es capaz de detectar una cantidad sorprendente de errores latentes. La prueba de humo mínima viable es:

1. Construye o descarga una release de FreeBSD para una arquitectura diferente, típicamente `arm64` o `powerpc64`.
2. Instala la release en un entorno QEMU.
3. Construye el driver dentro del entorno, o haz un cross-build en el host.
4. Carga el módulo.
5. Ejercita las funcionalidades principales del driver.
6. Observa si el driver se comporta como se espera.

Incluso este mínimo resulta revelador. La primera vez que ejecutes tu driver en un host big-endian, cualquier `htole32` o `be32toh` que falte se manifestará como valores de registro obviamente incorrectos. La primera vez que lo ejecutes en `arm64`, cualquier cast directo de un puntero a memoria de dispositivo causará normalmente un fallo de alineación en el primer acceso. La primera vez que lo construyas en `riscv`, cualquier suposición codificada directamente sobre las extensiones del conjunto de instrucciones fallará en tiempo de compilación. Cada error tarda minutos en encontrarse porque su síntoma es inmediato.

No necesitas hacer esto en cada commit. Una vez por release es suficiente para la mayoría de los drivers. El coste es bajo (un entorno QEMU y una hora de tu tiempo), y los errores que detectas son los que tus usuarios habrían reportado de otro modo.

### La realidad de FreeBSD en plataformas no x86

Una nota práctica sobre qué arquitecturas importan en el mundo de FreeBSD hoy en día. En el momento de escribir esto, `amd64` es la arquitectura de producción dominante con gran diferencia, seguida de `arm64` (con un crecimiento rápido en despliegues embebidos y de servidores), `riscv` (en crecimiento en investigación y nichos embebidos específicos) y `powerpc64` (usado en ciertos entornos HPC y heredados). La plataforma `i386` más antigua sigue teniendo soporte para despliegues heredados. La familia ARM de 32 bits (`armv7`) tiene soporte pero es menos común que `arm64`.

Para un driver que se espera que viva en el árbol principal, que funcione correctamente en `amd64`, `arm64` y que al menos compile en `riscv` suele ser suficiente. El port `powerpc64` es una prueba útil para detectar problemas de big-endian aunque no tengas usuarios finales en esa plataforma. El port `i386` es una prueba útil para problemas de tamaño de palabra de 32 bits. Dar soporte a todas ellas es el comportamiento por defecto en todo el proyecto; eliminar el soporte de cualquiera de ellas es una decisión que necesita una razón específica.

Para un driver mantenido fuera del árbol, la pregunta es para qué hardware das soporte. Si tus usuarios están únicamente en `amd64`, puedes legítimamente apuntar solo a esa plataforma. Sin embargo, el hábito de escribir código portable cuesta poco y protege frente a sorpresas, y con frecuencia descubrirás que el mismo driver resulta útil en una plataforma que no tenías prevista como destino original.

### Apunte: CHERI-BSD y Morello

Hay un rincón del universo FreeBSD que queda fuera de las arquitecturas de producción y merece una breve nota para los lectores curiosos. Si alguna vez encuentras una placa llamada **Morello**, o un árbol de sistema operativo llamado **CHERI-BSD**, estás ante una plataforma de investigación y no ante un objetivo del árbol principal. Vale la pena conocer el significado de estos nombres, porque cambian suposiciones que este capítulo ha tratado como universales.

**Qué es CHERI.** CHERI son las siglas de Capability Hardware Enhanced RISC Instructions. Es una extensión del conjunto de instrucciones desarrollada conjuntamente por el Computer Laboratory de la Universidad de Cambridge y SRI International. El objetivo es reemplazar el modelo de punteros simple que C ha utilizado desde los años 70 con **punteros de capacidad** (capability pointers): punteros que llevan consigo sus propios límites, permisos y una etiqueta de validez comprobada por hardware. Una capacidad no es simplemente una dirección entera. Es un pequeño objeto que el procesador sabe cómo desreferenciar, y genera una trampa si el programa intenta usarlo fuera de la región a la que se le concedió acceso.

El efecto práctico es que muchas clases de errores de seguridad de memoria se vuelven detectables por hardware. Un desbordamiento de buffer que sale por el final de un array ya no corrompe la memoria adyacente; genera una trampa. Un use-after-free ya no lee en silencio lo que haya en la dirección antigua; la capacidad liberada puede revocarse de modo que su bit de etiqueta quede inválido, y cualquier intento de desreferenciarla genera una trampa. La falsificación de punteros en el sentido clásico se vuelve imposible, porque las capacidades no pueden sintetizarse a partir de enteros simples.

**Qué es Morello.** Morello es la implementación prototipo de CHERI por parte de Arm sobre el conjunto de instrucciones Armv8.2-A. El programa Morello, anunciado por Arm junto con UK Research and Innovation en 2019, produjo el primer hardware con capacidades CHERI ampliamente disponible: placas de desarrollo y un sistema en chip de referencia que investigadores, universidades y algunos socios industriales podían obtener y programar. La placa Morello es lo más parecido que existe actualmente a una máquina CHERI de grado productivo, pero es explícitamente un prototipo de investigación. No se comercializa como plataforma de servidor de propósito general y no se espera que lo sea en su forma actual.

**Qué es CHERI-BSD.** CHERI-BSD es un fork de FreeBSD mantenido por el proyecto Cambridge CHERI que tiene como objetivo el hardware CHERI, incluido Morello. Es el sistema operativo en el que se realiza la mayor parte del trabajo práctico de portabilidad del código de userland y del kernel a una arquitectura de capacidades. CHERI-BSD es una plataforma de investigación. Sigue de cerca a FreeBSD, pero no es una versión oficial de FreeBSD, no está respaldada por el FreeBSD Project en el mismo sentido que `amd64` o `arm64`, y no es un objetivo para el que un autor de drivers típico necesite publicar software hoy en día. Su propósito es permitir que investigadores y primeros adoptantes estudien qué ocurre con el kernel de un sistema operativo real cuando el modelo de punteros cambia por debajo de él.

**Lo que necesita saber un autor de drivers.** Aunque nunca llegues a trabajar con CHERI-BSD, es útil conocer el tipo de suposiciones que rompe, porque esas suposiciones aparecen en el código ordinario de drivers sin que nadie lo note. Hay tres puntos que destacan.

Primero, **los punteros de capacidad llevan límites de subobjeto**. Un puntero al interior de un `struct softc` no es simplemente una dirección de byte; está acotado al subrango que le haya asignado el asignador de memoria o la construcción del lenguaje. Si un driver utiliza aritmética de punteros para ir de un campo a otro sin pasar por el puntero contenedor, esa aritmética puede producir una trampa en CHERI aunque pareciera inofensiva en `amd64`. El remedio suele ser derivar los punteros desde el puntero base de la estructura utilizando los modismos C adecuados y evitar los casts que borran información de tipo. Esta es la misma disciplina que este capítulo ha venido recomendando para la portabilidad sin CHERI; CHERI simplemente hace que el modo de fallo sea inmediato en lugar de ocasional.

Segundo, **las capacidades liberadas pueden ser revocadas**. CHERI-BSD puede recorrer la memoria para invalidar capacidades cuya asignación de respaldo haya sido liberada, de modo que los punteros colgantes no puedan utilizarse ni siquiera de forma indirecta. Un driver que almacena un puntero en un nodo sysctl, una entrada del árbol de dispositivos o algún handle entre subsistemas, y luego libera la asignación subyacente sin notificárselo al kernel, puede dejar atrás una capacidad revocada. En `amd64`, el use-after-free podría simplemente leer basura; en CHERI produce una trampa de forma determinista. Eso es generalmente algo positivo, pero significa que la disciplina del ciclo de vida del driver debe ser honesta con respecto a cada referencia que entrega.

Tercero, **las APIs del kernel tienen un aspecto similar pero llevan semántica de capacidades por debajo**. Las interfaces `bus_space(9)` y `bus_dma(9)` tienen el mismo aspecto a nivel de código fuente en CHERI-BSD que en el FreeBSD oficial, pero los valores que devuelven son capacidades en lugar de direcciones simples. Un driver que utilice estas APIs a través de los accesores previstos generalmente se portará con pocos cambios. Un driver que convierte un `bus_space_handle_t` a `uintptr_t` y luego de vuelta, o que fabrica punteros a la memoria del dispositivo mediante aritmética de enteros, fallará, porque los metadatos de capacidad se pierden en la conversión de ida y vuelta por enteros.

Algunos drivers se portan a CHERI-BSD con cambios mínimos porque ya se mantenían dentro de los accesores que este capítulo ha venido recomendando. Otros necesitan una **disciplina de capacidades** explícita: revisar cada cast, cada aritmética de punteros hecha a mano y cada lugar donde un puntero se almacena en un tipo que no es puntero. El proyecto CHERI-BSD publica informes de experiencia de portabilidad para varios subsistemas, y leer uno o dos de ellos es una forma rápida de comprobar cómo es esa disciplina en la práctica. El volumen de cambios necesarios varía mucho según el driver, y nadie debería asumir ni que un driver determinado es trivialmente limpio para CHERI ni que necesita una reescritura total, sin haberlo intentado antes.

**Dónde buscar.** La descripción técnica fundamental es el artículo ISCA de 2014 *The CHERI Capability Model: Revisiting RISC in an Age of Risk*, que aparece en el árbol de documentación de FreeBSD en `/usr/src/share/doc/papers/bsdreferences.bib`. Los artículos posteriores del mismo grupo describen la implementación de Morello, la revocación de capacidades, el soporte del compilador y la experiencia de portabilidad en el mundo real. Para materiales prácticos, el proyecto CHERI-BSD y el grupo de investigación CHERI del Cambridge Computer Laboratory mantienen documentación, instrucciones de compilación y notas de portabilidad; busca por los nombres del proyecto en lugar de depender de una URL impresa aquí, porque los sitios de los proyectos de investigación cambian de ubicación con el tiempo.

El propósito de mencionar CHERI-BSD en un capítulo de portabilidad no es empujarte hacia él. Es decirte que los hábitos que este capítulo ha venido enseñando (usar accesores en lugar de punteros directos, respetar la distinción entre direcciones y handles opacos, y evitar los casts gratuitos) producen beneficios en más de una arquitectura. Son los mismos hábitos que hacen que un driver funcione en `arm64`, en `riscv` y en máquinas de capacidades experimentales. Cuanto más te mantengas alejado de los trucos ingeniosos con punteros, menor será la superficie de sorpresas CHERI de tu driver, incluso si nunca compilas para CHERI en absoluto.

### Cerrando la sección 5

Ya has conocido los tres ejes en los que difieren las arquitecturas, así como los patrones de FreeBSD que aborda cada uno. El endianness se gestiona con las funciones auxiliares de endian; la alineación, con `bus_read_*` y `memcpy`; el tamaño de palabra, con tipos de anchura fija. Las barreras de memoria ordenan los accesos visibles para el dispositivo, y `bus_dmamap_sync` garantiza la coherencia entre las cachés del CPU y los buffers de DMA. Aplicados de forma sistemática, estos patrones reducen la portabilidad entre arquitecturas a casi ningún trabajo adicional por driver y a casi ningún riesgo extra. Un driver que los aplique correctamente en `amd64` normalmente compilará sin problemas y funcionará bien en `arm64`, `riscv`, `powerpc` y el resto con tan solo recompilar y pasar una prueba de humo.

En la sección 6, pasamos de la portabilidad entre arquitecturas a la flexibilidad en tiempo de compilación: cómo usar la compilación condicional y las opciones de compilación del kernel para seleccionar características, activar la depuración y cambiar entre backends reales y simulados sin llenar el código fuente de un revoltijo de `#ifdef`.

## Sección 6: Compilación condicional y flexibilidad en tiempo de compilación

La compilación condicional es el arte de hacer que un mismo árbol de código fuente produzca binarios distintos según lo que se haya solicitado en tiempo de compilación. Es tentadora, a veces necesaria y extraordinariamente fácil de abusar. Un driver que usa `#ifdef` con criterio es más limpio y mantenible que uno que no lo hace. Un driver que recurre a `#ifdef` en cada decisión se convierte en una maraña que nadie quiere tocar.

Esta sección explica cuándo la compilación condicional es la herramienta adecuada, qué formas de ella prefiere FreeBSD y cómo mantener el código fuente legible cuando existe variación real en tiempo de compilación.

### Los tres niveles de compilación condicional

La compilación condicional en un driver de FreeBSD ocurre en tres niveles bien diferenciados. Cada uno tiene un propósito distinto, y elegir el nivel correcto para un problema concreto evita la mayor parte de la fealdad por la que la compilación condicional tiene mala fama.

**Nivel uno: código específico de arquitectura.** A veces es necesario escribir código diferente para distintas arquitecturas de CPU. Un ejemplo es el ensamblado inline de intercambio de bytes específico para una plataforma que carece de un builtin general del compilador. FreeBSD lo resuelve colocando el código por arquitectura en un archivo por arquitectura. Observa cómo difiere `/usr/src/sys/amd64/include/_bus.h` de `/usr/src/sys/arm64/include/_bus.h`. Cada archivo contiene las definiciones adecuadas para su arquitectura. El código fuente principal no usa `#ifdef __amd64__` para seleccionar entre ellos; incluye `<machine/bus.h>` y el build del kernel elige el archivo concreto correcto mediante las rutas de inclusión. Este mecanismo se denomina **cabecera específica de máquina**, y debes preferirlo siempre que necesites más de unas pocas líneas de código por arquitectura. Si colocas un gran bloque de `#ifdef __arm64__` en un archivo fuente del driver, probablemente estás reinventando este mecanismo de forma deficiente.

**Nivel dos: características opcionales.** Las características opcionales son aquellas con las que tu driver puede compilarse o no, y cuya presencia es una elección, no una necesidad arquitectónica. Backends de simulación, rastreo de depuración, nodos sysctl, soporte de protocolo opcional. FreeBSD los gestiona mediante el sistema de opciones del kernel: añades `options PORTDRV_DEBUG` a la configuración del kernel o pasas `-DPORTDRV_DEBUG` a `make`. Dentro del código, proteges los bloques relevantes con `#ifdef PORTDRV_DEBUG`. Como la opción es tuya, la controlas de forma limpia, y la característica se compila o no.

**Nivel tres: compatibilidad de API.** FreeBSD evoluciona. Una macro cambia de nombre, una firma de función cambia, un comportamiento que antes era implícito se vuelve explícito. Un driver que quiere compilar tanto en una versión antigua como en una nueva usa `__FreeBSD_version` para bifurcar según la versión del kernel. Esta es la forma más sutil de compilación condicional, porque la misma operación lógica se expresa de dos maneras y debes mantener ambas hasta que la versión antigua deje de recibir soporte. El enfoque correcto es mantener estas ramas pequeñas y refactorizarlas en funciones auxiliares o macros cortas.

### La macro `__FreeBSD_version`

Todo kernel de FreeBSD define `__FreeBSD_version` en `/usr/src/sys/sys/param.h`. Es un entero único que codifica el número de versión: por ejemplo, `1403000` corresponde a FreeBSD 14.3-RELEASE. Puedes usarlo para proteger código que depende de un cambio concreto de API:

```c
#if __FreeBSD_version >= 1400000
	/* Use the new if_t API. */
	if_setflags(ifp, flags);
#else
	/* Use the older direct struct access. */
	ifp->if_flags = flags;
#endif
```

Dos reglas sobre `__FreeBSD_version`. Primera, úsalo con moderación; cada guarda es una rama que debe probarse en ambas configuraciones. Segunda, abstrae la rama en una función auxiliar en cuanto aparezca más de dos veces. Si necesitas `if_setflags` en diez lugares, escribe una macro auxiliar en una cabecera de compatibilidad y úsala en todos lados, en lugar de duplicar el `#if` en cada punto de llamada.

La compatibilidad es un coste acumulativo. Dos bloques `#if` son manejables, y veinte son una carga de mantenimiento. Haz que cada uno valga la pena, y retíralos en cuanto la versión antigua deje de recibir soporte.

Para hacerte una idea de cómo se ve esto en el árbol, abre `/usr/src/sys/dev/gve/gve_main.c`. El driver Google Virtual Ethernet es actual, está en el árbol y admite varias versiones a la vez, por lo que sus guardas `__FreeBSD_version` documentan migraciones recientes de API con la mínima ceremonia. Destacan dos ejemplos breves. El primero está en la ruta de attach, donde el driver establece los flags de la interfaz:

```c
#if __FreeBSD_version >= 1400086
	if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
#else
	if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST | IFF_KNOWSEPOCH);
#endif
```

Las versiones antiguas requerían que los drivers establecieran `IFF_KNOWSEPOCH` para declarar que usaban la epoch de red correctamente. A partir de `1400086`, el kernel ya no necesita ese opt-in, y los drivers dejan de establecer el flag. La rama es pequeña, la llamada al accessor (`if_setflags`) es la misma en ambos lados, y solo difiere la lista de flags. Esta es la forma mínima que deben adoptar estas guardas.

El segundo ejemplo está en la declaración del módulo, al final del mismo archivo:

```c
#if __FreeBSD_version < 1301503
static devclass_t gve_devclass;

DRIVER_MODULE(gve, pci, gve_driver, gve_devclass, 0, 0);
#else
DRIVER_MODULE(gve, pci, gve_driver, 0, 0);
#endif
```

A partir de `1301503`, la macro `DRIVER_MODULE` dejó de aceptar un argumento `devclass_t`; las versiones más recientes pasan `0` en su lugar y las antiguas todavía necesitan un `devclass_t` real. Ambas ramas siguen registrando el driver con `DRIVER_MODULE`; la diferencia es un único argumento y la variable `static` que lo alimenta. Cuando tu propio driver necesite abarcar este límite, tus guardas deberían ser tan simples como estas. Si no lo son, suele ser señal de que el shim de compatibilidad debería vivir en una pequeña macro auxiliar en lugar de en cada punto de llamada.

### El mecanismo de opciones del kernel

Para las opciones específicas del driver, el mecanismo de opciones del kernel de FreeBSD te proporciona una forma limpia de exponer flags de características. El mecanismo tiene tres partes:

1. La opción se declara en un archivo `options` por driver que reside junto al código fuente del driver, para los drivers que están en el árbol. Para los drivers fuera del árbol, como tu driver de referencia, defines la opción mediante un `CFLAGS+= -DPORTDRV_SOMETHING` en el Makefile y proteges el código con `#ifdef PORTDRV_SOMETHING`.

2. La opción se activa o desactiva en tiempo de compilación. Para los builds del kernel, se incluye en el archivo de configuración del kernel (`/usr/src/sys/amd64/conf/GENERIC` o un archivo personalizado) como `options PORTDRV_SOMETHING`. Para los builds de módulos, se incluye en el Makefile o en la línea de comandos de `make` como `-DPORTDRV_SOMETHING`.

3. El código lee la opción con `#ifdef PORTDRV_SOMETHING`.

Es un mecanismo sencillo, pero suficiente para la mayoría de las necesidades. Un driver que lo usa con cuidado tiene un puñado de opciones, cada una protegiendo una pequeña pieza de funcionalidad bien definida, y cada una con una descripción clara en el README.

### El modo simulado como opción de compilación

Un buen ejemplo del patrón de opciones del kernel es el modo simulado. La idea es permitir que el driver se compile y cargue sin ningún hardware real, seleccionando un backend de software que imita el dispositivo. Ya has visto este patrón en la sección 3; ahora lo expresamos como una opción en tiempo de compilación.

El Makefile añade:

```make
.if defined(PORTDRV_WITH_SIM) && ${PORTDRV_WITH_SIM} == "yes"
SRCS+=	portdrv_sim.c
CFLAGS+= -DPORTDRV_WITH_SIM
.endif
```

El núcleo registra el backend simulado en su carga de módulo si el flag está activado:

```c
static int
portdrv_modevent(module_t mod, int type, void *arg)
{
	switch (type) {
	case MOD_LOAD:
#ifdef PORTDRV_WITH_SIM
		/* Create a single simulated instance. */
		portdrv_sim_create();
#endif
		return (0);
	case MOD_UNLOAD:
#ifdef PORTDRV_WITH_SIM
		portdrv_sim_destroy();
#endif
		return (0);
	}
	return (0);
}
```

Y `portdrv_sim.c` solo se compila si el flag está activado.

Este patrón ofrece varios beneficios a la vez. El driver puede desarrollarse en una máquina sin el hardware real. La suite de pruebas puede ejecutarse en cualquier VM con FreeBSD. Los revisores pueden cargar el driver y ejercitar la lógica principal, que es donde suelen estar los bugs interesantes, sin necesitar el hardware. Y los builds de producción, que no pasan `PORTDRV_WITH_SIM=yes`, excluyen el código de simulación por completo, de modo que ningún backend stub está presente en los binarios de distribución.

### Builds de depuración

La segunda opción típica es un build de depuración. Los drivers reales suelen tener una opción `PORTDRV_DEBUG` que activa el registro detallado, aserciones adicionales y, a veces, rutas más lentas que validan los invariantes de forma más agresiva. El patrón es:

```c
#ifdef PORTDRV_DEBUG
#define PD_DBG(sc, fmt, ...) \
	device_printf((sc)->sc_dev, "DEBUG: " fmt "\n", ##__VA_ARGS__)
#else
#define PD_DBG(sc, fmt, ...) do { (void)(sc); } while (0)
#endif
```

Cualquier parte del código fuente que necesite un mensaje de depuración condicional usa `PD_DBG(sc, "got %u bytes", len)`. En un build de producción, la macro no produce nada, por lo que el mensaje se elimina completamente en la compilación. En un build de depuración, la macro imprime. Los puntos de llamada no están protegidos con `#ifdef`; lo está la macro.

Este es el truco clave para mantener la compilación condicional legible: **esconde el `#ifdef` dentro de una macro y expón una llamada uniforme en el punto de uso**. El código fuente se lee de la misma manera en ambas configuraciones, y el lector no tiene que desenredar mentalmente media docena de ramas `#if` para entender el código.

### Flags de características mediante sysctl y dmesg

Una pregunta relacionada pero distinta es: ¿cómo descubre el usuario o un operador con qué características se compiló este driver? Un driver de producción responde a esto normalmente con un árbol sysctl y un banner impreso en la carga del módulo.

En la carga del módulo, el driver imprime un mensaje breve que se identifica a sí mismo y las características que admite:

```c
printf("portdrv: version %s loaded; backends:"
#ifdef PORTDRV_WITH_PCI
       " pci"
#endif
#ifdef PORTDRV_WITH_USB
       " usb"
#endif
#ifdef PORTDRV_WITH_SIM
       " sim"
#endif
       "\n", PORTDRV_VERSION);
```

La salida en el registro del kernel indica a un operador exactamente qué backends están compilados. Un build que se supone que tiene soporte USB pero cuyo dmesg muestra solo `backends: pci` revela la configuración incorrecta de un vistazo.

En tiempo de ejecución, un árbol sysctl expone la misma información de forma legible por máquina:

```text
dev.portdrv.0.version: 2.0
dev.portdrv.0.backend: pci
dev.portdrv.0.features.dma: 1
dev.portdrv.0.features.debug: 0
```

Un script puede leerlos, un sistema de monitorización puede alertar sobre ellos y un informe de bug puede incluirlos sin que quien lo reporta tenga que adivinar.

### Cómo evitar la expansión descontrolada de `#ifdef`

El mayor escollo de la compilación condicional es su tendencia a expandirse descontroladamente. Se añade una característica y se protege con `#ifdef FEATURE_A`. Un mes después se añade otra característica y se protege con `#ifdef FEATURE_B`. Seis meses después, un mantenedor añade una corrección de bug que toca ambas características, y adquiere `#if defined(FEATURE_A) && defined(FEATURE_B)`. Pronto cada función tiene cinco guardas, cada una protegiendo un puñado de líneas, y el lector no puede trazar un único camino por el código sin resolver mentalmente un producto cartesiano de ramas `#if`.

Tres reglas mantienen la expansión bajo control.

**Mantén las guardas gruesas.** Protege una función entera, o un archivo entero, en lugar de líneas dispersas en medio de una función. Si te encuentras añadiendo `#ifdef` dentro del cuerpo de una función más de una vez, probablemente la función debería dividirse: una versión para `FEATURE_A`, otra para `!FEATURE_A`, y un pequeño despachador en el punto de llamada.

**Esconde las guardas en macros.** Si necesitas una operación condicional en muchos lugares, envuélvela en una macro o función inline que tenga una forma vacía en la rama `#else`. Los puntos de llamada no tendrán guardas en absoluto.

**Revisa el conjunto de guardas periódicamente.** Un flag de característica que nadie ha desactivado en dos años es efectivamente obligatorio. Elimínalo. Un flag que protege una característica que fue eliminada es código muerto. Elimínalo también. Un flag que significa algo diferente ahora de lo que significaba cuando se introdujo debería renombrarse.

### Condicionales específicos de arquitectura

Hay situaciones en las que la herramienta correcta es verdaderamente un `#if` sobre la macro de arquitectura. Un ejemplo de código real, de `/usr/src/sys/dev/vnic/nicvf_queues.c`:

```c
#if BYTE_ORDER == BIG_ENDIAN
	return ((i & ~3) + 3 - (i & 3));
#else
	return (i);
#endif
```

Esta función calcula un índice cuya disposición depende del orden de bytes del host. No hay manera de ocultar esto en una macro, porque las dos formas son genuinamente distintas. Lo correcto es hacer exactamente lo que hace el driver real: proteger el mínimo posible, comentar qué hacen las dos ramas y seguir adelante. No intentes eliminar cada `#if`.

La clave es la disciplina. Usa `#if BYTE_ORDER` solo donde exista una diferencia arquitectónica real, y mantén el bloque condicional lo más pequeño posible. Un bloque `#if` de cinco líneas es legible. Uno de cincuenta, por lo general, no lo es.

### Compatibilidad con subsistemas opcionales

A veces un driver admite características que dependen de subsistemas opcionales. Por ejemplo, un driver de red podría admitir checksum offload solo si el kernel se construyó con `options INET`. El patrón es:

```c
#ifdef INET
	/* Set up IPv4 checksum offload. */
#endif
#ifdef INET6
	/* Set up IPv6 checksum offload. */
#endif
```

El sistema de build del kernel proporciona estas guardas automáticamente; no necesitas definirlas tú mismo. Pero sí debes respetarlas. Un driver que usa `struct ip` de forma incondicional fallará al compilar cuando `options INET` no esté configurado, aunque el kernel admita compilarse sin IPv4. Comprueba el build en la configuración que puedan usar tus usuarios; normalmente basta con añadir una guarda.

### Cómo evitar comprobaciones en tiempo de ejecución que deberían ser en tiempo de compilación

Un error sutil es implementar en tiempo de ejecución lo que debería ser una decisión en tiempo de compilación. Si tu driver tiene un backend de simulación y un backend PCI, y el usuario elige uno en tiempo de compilación, no hay razón para llevar ambos en el binario. Tomar la decisión en tiempo de ejecución, por ejemplo mediante un sysctl que alterna entre backends, desperdicia memoria y complica las pruebas. Toma la decisión en tiempo de compilación, excluye del build el backend que no se use, y el driver quedará más pequeño y sencillo.

El error inverso también es posible: convertir en una decisión de compilación lo que debería ser una elección en tiempo de ejecución. Si el mismo binario debe admitir varias piezas de hardware simultáneamente, la elección es genuinamente en tiempo de ejecución, y un indicador de compilación es la herramienta equivocada. La pregunta correcta que debes hacerte cada vez es: «¿Necesito ambos comportamientos en el mismo driver cargado?». Si la respuesta es sí, en tiempo de ejecución. Si es no, en tiempo de compilación.

### Archivos de opciones y configuración del kernel

Para los drivers que se distribuyen con el propio árbol del kernel, FreeBSD ofrece una forma más estructurada de exponer las opciones de build: el archivo `options`. Cada archivo de opciones reside junto al código fuente del driver y lista las opciones que el driver reconoce, junto con una descripción breve y un valor por defecto. El archivo se llama `options` y sigue un formato sencillo:

```text
# options - portdrv options
PORTDRV_DEBUG
PORTDRV_WITH_SIM
PORTDRV_WITH_PCI
```

Las opciones listadas de esta manera pueden habilitarse en el archivo de configuración del kernel, normalmente en `/usr/src/sys/amd64/conf/GENERIC` o en una configuración de kernel personalizada, así:

```text
options	PORTDRV_DEBUG
options	PORTDRV_WITH_SIM
```

El sistema de build del kernel se encarga de propagar los indicadores `-D` al compilador. Dentro del driver, el código usa guardas con `#ifdef PORTDRV_DEBUG` exactamente igual que con una opción definida en el Makefile. La diferencia es organizativa: los archivos de opciones residen en el árbol de código fuente junto al driver, de modo que quien se enfrente a un driver desconocido sepa exactamente qué opciones reconoce.

Para los drivers fuera del árbol de código fuente, el enfoque basado en Makefile es más sencillo, y es el que hemos utilizado en este capítulo. Para los drivers dentro del árbol que se espera compilar como parte de una configuración del kernel, el archivo de opciones es el mecanismo canónico.

### Sugerencias de carga de módulos y `device.hints`

Un tema relacionado pero distinto es el mecanismo `device.hints`. Las sugerencias son una forma de configurar dispositivos en tiempo de boot, antes de que el driver se haya inicializado completamente. Residen en `/boot/device.hints` y tienen la forma:

```text
hint.portdrv.0.mode="simulation"
hint.portdrv.0.debug="1"
```

Dentro del driver, se leen con `resource_string_value` o `resource_int_value`:

```c
int mode;
const char *mode_str;

if (resource_string_value("portdrv", 0, "mode", &mode_str) == 0) {
	/* Apply the hint. */
}
```

Las sugerencias son una forma de configurar el driver en tiempo de ejecución sin necesidad de recompilar. Son especialmente útiles en sistemas embebidos donde los parámetros del dispositivo se conocen en el momento del diseño del sistema, pero no en el momento de compilar el driver. Para un driver portable entre variantes de hardware, las sugerencias pueden ser el mecanismo mediante el cual se reconoce cada variante.

Las sugerencias no son un sustituto de la selección de características en tiempo de compilación. Son complementarias. Los indicadores de compilación deciden qué puede hacer el driver; las sugerencias deciden qué hace realmente en un arranque concreto. Un driver puede tener ambos, y la mayoría de los drivers maduros los tienen.

### Indicadores de características mediante `MODULE_PNP_INFO`

Para los drivers que se enganchan a hardware identificado por IDs de fabricante y dispositivo, `MODULE_PNP_INFO` es una forma de declarar esa identificación en los metadatos del módulo. El kernel y las herramientas en espacio de usuario leen estos metadatos para decidir qué driver cargar cuando hay un dispositivo concreto, y `devd(8)` los utiliza para asociar dispositivos con módulos.

Una declaración típica tiene este aspecto:

```c
MODULE_PNP_INFO("U32:vendor;U32:device", pci, portdrv_pci,
    portdrv_pci_ids, nitems(portdrv_pci_ids));
```

donde `portdrv_pci_ids` es un array de registros de identificación que también consulta la función probe del driver. Los metadatos se incrustan en el archivo `.ko` y son leídos por `kldxref(8)` para construir un índice que se usa en el boot y al conectar dispositivos.

Para un driver portable con múltiples backends, cada backend declara su propio `MODULE_PNP_INFO`. El backend PCI declara IDs de PCI; el backend USB declara IDs de USB; el backend de simulación no declara nada porque no tiene hardware asociado. El kernel selecciona el backend correcto automáticamente cuando hay hardware coincidente.

### Los límites de `__FreeBSD_version`

`__FreeBSD_version` es valioso, pero tiene un límite. Indica con qué versión del kernel estás compilando, no qué versión está ejecutándose realmente. Como los módulos del kernel están estrechamente ligados al kernel en el que se cargan, normalmente coinciden, pero hay casos límite: módulos compilados con un árbol ligeramente más antiguo y cargados en uno ligeramente más nuevo, o módulos compilados con un conjunto de parches personalizado. En estos casos, `__FreeBSD_version` puede llevarte a conclusiones incorrectas.

El enfoque más robusto para decisiones en tiempo de ejecución es consultar directamente al kernel mediante un sysctl:

```c
int major = 0;
size_t sz = sizeof(major);

sysctlbyname("kern.osreldate", &major, &sz, NULL, 0);
if (major < 1400000) {
	/* Older kernel. Use the legacy code path. */
}
```

Esto lee la versión del kernel que está ejecutándose realmente. Para la mayoría de los drivers es excesivo; para drivers que deben adaptarse en tiempo de ejecución, es la herramienta adecuada. El habitual `__FreeBSD_version` en tiempo de compilación es una afirmación sobre el kernel en el momento del build; `kern.osreldate` es una afirmación sobre el kernel en tiempo de ejecución.

En el código de un driver concretamente, la comprobación en tiempo de compilación es casi siempre lo que necesitas. El módulo casi siempre se carga en el kernel contra el que se compiló, y la comprobación de ABI del kernel mediante `MODULE_DEPEND` detecta los raros casos de incompatibilidad.

### Organización de las capas de compatibilidad

Cuando las guardas de `__FreeBSD_version` empiezan a aparecer en más de unos pocos lugares, la decisión correcta es centralizarlas en un encabezado de compatibilidad. La disposición típica es:

```c
/* portdrv_compat.h - compatibility shims for FreeBSD API changes. */

#ifndef _PORTDRV_COMPAT_H_
#define _PORTDRV_COMPAT_H_

#include <sys/param.h>

#if __FreeBSD_version >= 1400000
#include <net/if.h>
#include <net/if_var.h>
#define PD_IF_SETFLAGS(ifp, flags)  if_setflags((ifp), (flags))
#define PD_IF_GETFLAGS(ifp)         if_getflags(ifp)
#else
#define PD_IF_SETFLAGS(ifp, flags)  ((ifp)->if_flags = (flags))
#define PD_IF_GETFLAGS(ifp)         ((ifp)->if_flags)
#endif

#if __FreeBSD_version >= 1300000
#define PD_CV_WAIT(cvp, mtxp)  cv_wait_unlock((cvp), (mtxp))
#else
#define PD_CV_WAIT(cvp, mtxp)  cv_wait((cvp), (mtxp))
#endif

#endif /* !_PORTDRV_COMPAT_H_ */
```

El código principal usa `PD_IF_SETFLAGS(ifp, flags)` y `PD_CV_WAIT(cvp, mtxp)` en lugar de las llamadas directas al kernel. El encabezado de compatibilidad es el único lugar donde aparece `__FreeBSD_version`, y las guardas están agrupadas en lugar de dispersas. Cuando se abandona la versión más antigua compatible, eliminas las ramas `#else` y dejas en su lugar los nombres modernos.

Este patrón es especialmente valioso para drivers que deben admitir un rango de versiones del kernel, como los drivers fuera del árbol que utilizan usuarios en diferentes sistemas. Un driver dentro del árbol normalmente solo tiene que admitir la versión actual más las anteriores que el proyecto se comprometa a soportar, que suelen ser una o dos.

### Telemetría en tiempo de compilación

Un truco pequeño pero útil es incrustar telemetría en tiempo de compilación en el módulo. Una sola línea en el Makefile:

```make
CFLAGS+=	-DPORTDRV_BUILD_DATE='"${:!date "+%Y-%m-%dT%H:%M:%S"!}"'
```

captura la marca de tiempo del build en una cadena que el driver puede imprimir al cargarse. Combinado con una cadena de versión y una lista de backends habilitados, el banner de carga del módulo se convierte en un pequeño artefacto de diagnóstico:

```text
portdrv: version 2.0 built 2026-04-19T14:32:17 loaded, backends: pci sim
```

Cuando un usuario reporta un error, puede pegar el banner en el informe, y sabrás exactamente qué versión está ejecutando. Es trivial de añadir e inmensamente valioso durante la vida útil del driver.

### Cerrando la sección 6

La compilación condicional es una herramienta poderosa. Usada con mesura, mantiene las características opcionales limpias, te permite admitir múltiples versiones de FreeBSD y habilita backends de simulación sin contaminar los builds de producción. Usada de forma descuidada, convierte el código fuente en un laberinto. Las normas de moderación son: preferir los encabezados específicos de arquitectura frente a `#ifdef __arch__`, mantener los indicadores de opción a un nivel alto, ocultar las guardas dentro de macros, y eliminar los indicadores que hayan cumplido su propósito. Los archivos de opciones, las sugerencias de dispositivo, `MODULE_PNP_INFO` y un encabezado de compatibilidad te ofrecen una pequeña familia de herramientas para gestionar la variación en tiempo de compilación sin saturar la lógica principal.

La sección 7 hace un breve desvío fuera de FreeBSD. A veces los drivers necesitan compartir código con NetBSD u OpenBSD, y aunque ese no es un objetivo principal de este libro, conocer el terreno te protege de decisiones de diseño que perjudicarían la portabilidad entre BSDs incluso cuando no la necesitas de inmediato.

## Sección 7: Adaptación para la compatibilidad entre BSDs

Los tres BSDs de código abierto, FreeBSD, NetBSD y OpenBSD, comparten una larga historia. A nivel de código fuente divergieron de un antepasado común, y aunque cada uno ha evolucionado en su propia dirección, aún se reconocen mutuamente. Un driver de dispositivo escrito cuidadosamente al estilo FreeBSD puede a menudo portarse a NetBSD u OpenBSD con un esfuerzo significativo pero acotado; uno escrito sin cuidado, no.

Esta sección es deliberadamente breve. Este es un libro de FreeBSD, y convertir el Capítulo 29 en un manual de portabilidad entre BSDs desplazaría material que pertenece a capítulos posteriores. Lo que haremos aquí es suficiente para que puedas identificar qué partes de un driver de FreeBSD son fácilmente portables y cuáles necesitan traducción. No aprenderás a portar; aprenderás contra qué diseñar, de modo que la portabilidad sea una posibilidad en lugar de una imposibilidad.

### Áreas de acuerdo entre los BSDs

Las áreas en las que FreeBSD, NetBSD y OpenBSD coinciden más sólidamente son aquellas en las que más se apoya tu driver:

- **Lenguaje C y cadena de herramientas.** Los tres usan los mismos estándares C y compiladores similares (clang en FreeBSD y OpenBSD; gcc en NetBSD con clang disponible).
- **La forma general de los módulos del kernel.** Los tres admiten módulos del kernel cargables con un ciclo de vida similar: carga, inicialización, attach, detach, descarga.
- **La forma general de los drivers de dispositivo.** Probe, attach, detach. Estado por instancia. Asignación de recursos. Manejadores de interrupciones. Árboles de dispositivos al estilo Newbus (NetBSD tiene `config`; FreeBSD tiene Newbus; OpenBSD tiene `autoconf`).
- **Biblioteca C estándar y POSIX.** Incluso en espacio de usuario, los tres sistemas comparten suficiente para que la mayoría de las utilidades en userland se porten de forma trivial.
- **Muchos protocolos específicos del dispositivo y formatos de intercambio.** Una trama Ethernet es una trama Ethernet, y un comando NVMe es un comando NVMe, en cualquiera de los tres.

Un driver cuya lógica interesante está por encima de la capa de API del kernel (analizando protocolos, gestionando máquinas de estado y coordinando transferencias) es en gran medida portable. Esa lógica interesante normalmente no sabe nada de `bus_dma` ni de `device_t`; conoce el modelo de programación del hardware.

### Áreas de divergencia entre los BSDs

Las áreas donde los tres BSDs difieren se concentran en las APIs del kernel:

- **Marco de bus.** El Newbus de FreeBSD, el `autoconf(9)` de NetBSD y el `autoconf(9)` de OpenBSD (mismo nombre, maquinaria diferente) no son compatibles a nivel de código fuente. Los callbacks son similares en espíritu, pero tienen firmas diferentes.
- **Abstracción DMA.** El `bus_dma(9)` de FreeBSD tiene parientes cercanos en NetBSD y OpenBSD también llamados `bus_dma(9)`, pero los tipos y algunas firmas de función difieren. El modelo general es compartido; la API exacta no.
- **Asignación de memoria.** `malloc(9)` existe en los tres con la misma idea, pero con familias de funciones e indicadores ligeramente diferentes.
- **Primitivas de sincronización.** Los mutex, las variables de condición y los epochs existen en los tres, pero los nombres específicos y los indicadores difieren.
- **Interfaces de red.** El `ifnet` y el manejador opaco `if_t` de FreeBSD, el `struct ifnet` de NetBSD y el `struct ifnet` de OpenBSD parecen similares, pero han divergido en los detalles.
- **Dispositivos de caracteres.** El marco `cdev` de FreeBSD, el `cdevsw` de NetBSD y la maquinaria de dispositivos de caracteres de OpenBSD difieren en cómo registran y despachan.

Esta no es una lista completa, pero capta las fuentes habituales de dificultad en la portabilidad.

### El estilo compatible con múltiples BSDs

Si quieres que tu driver sea portable entre BSDs sin comprometerte a mantener tres versiones, la estrategia más económica a largo plazo es escribirlo en un estilo que aísle las partes específicas de FreeBSD. Los patrones son los que ya has aprendido en este capítulo:

- **Aísla el código que depende del hardware.** Una interfaz de backend oculta las APIs de bus. Sustituir el `bus_dma` de FreeBSD por el de NetBSD implica cambiar la implementación del backend, no la parte central.
- **Envuelve los primitivos del kernel que uses con frecuencia.** Si `malloc(M_TYPE, size, M_NOWAIT)` se reemplaza en NetBSD por una variante con una sintaxis ligeramente distinta, envolverlo en `portdrv_alloc(size)` significa cambiar un único lugar en vez de cien.
- **Mantén la lógica central libre de dependencias de subsistemas del kernel.** La cola de peticiones, la máquina de estados y el análisis del protocolo no necesitan incluir `sys/bus.h` ni `net/if_var.h`.
- **Separa el registro de la lógica.** La macro `DRIVER_MODULE` y sus equivalentes representan una pequeña porción de código localizada por driver. Colocarla en un archivo dedicado por cada BSD resulta más fácil que dispersarla por todo el driver.
- **Usa tipos estándar.** `uint32_t` y `size_t` existen en los tres BSDs. `u_int32_t` e `int32` son grafías más antiguas que resultan más portables en sentido estricto, pero menos idiomáticas en FreeBSD.

Si has seguido el diseño de archivos de la Sección 4, gran parte de esto ya está en su lugar. La parte central en `portdrv_core.c` es independiente de los subsistemas del kernel. Los backends en `portdrv_pci.c` y `portdrv_usb.c` son donde reside el código específico de FreeBSD. Un port hipotético a NetBSD añadiría `portdrv_pci_netbsd.c` y dejaría la parte central sin cambios, salvo pequeñas diferencias de tipos.

### Wrappers de compatibilidad

Para los casos en los que realmente necesitas que el mismo código fuente compile en múltiples BSD, la técnica estándar es un header de compatibilidad. Algo como:

```c
/* portdrv_os.h - OS-specific wrappers */
#ifdef __FreeBSD__
#include <sys/malloc.h>
#define PD_MALLOC(sz, flags) malloc((sz), M_PORTDRV, (flags))
#define PD_FREE(p)           free((p), M_PORTDRV)
#endif

#ifdef __NetBSD__
#include <sys/malloc.h>
#define PD_MALLOC(sz, flags) kmem_alloc((sz), (flags))
#define PD_FREE(p)           kmem_free((p), sz)
#endif

#ifdef __OpenBSD__
#include <sys/malloc.h>
#define PD_MALLOC(sz, flags) malloc((sz), M_DEVBUF, (flags))
#define PD_FREE(p)           free((p), M_DEVBUF, sz)
#endif
```

El core usa `PD_MALLOC` y `PD_FREE` en todas partes. Un header cambia por cada nuevo OS; el core nunca cambia. Aplica la misma técnica a los locks, al DMA, a la impresión de mensajes, a cualquier primitiva de FreeBSD que difiera en los demás BSD. El resultado es un driver cuya superficie específica de OS es un único archivo y cuyo core es compartido.

Ten en cuenta la contrapartida. Escribir el driver de esta manera tiene un pequeño coste inicial: no puedes llamar simplemente a `malloc` al estilo de FreeBSD; tienes que llamar a tu wrapper. Si realmente no necesitas portabilidad entre BSD, ese coste no te aporta nada. Si la necesitas, el coste es mucho menor que la alternativa de mantener tres drivers.

### Listado de las APIs específicas de FreeBSD que utilizas

Un ejercicio útil antes de plantearte siquiera el trabajo de portabilidad entre BSD es catalogar las APIs específicas de FreeBSD de las que depende tu driver. Repasa el código fuente y anota cada función, macro y nombre de estructura que sea exclusivo de FreeBSD. La lista suele incluir:

- `bus_alloc_resource_any`, `bus_release_resource`
- `bus_setup_intr`, `bus_teardown_intr`
- `bus_read_1`, `bus_read_2`, `bus_read_4`, y las variantes de escritura
- `bus_dma_tag_create`, `bus_dmamap_load`, `bus_dmamap_sync`, `bus_dmamap_unload`
- `callout_init_mtx`, `callout_reset`, `callout_stop`, `callout_drain`
- `malloc`, `free`, `MALLOC_DEFINE`
- `mtx_init`, `mtx_lock`, `mtx_unlock`, `mtx_destroy`
- `sx_init`, `sx_slock`, `sx_xlock`
- `device_printf`, `device_get_softc`, `device_set_desc`
- `DRIVER_MODULE`, `MODULE_VERSION`, `MODULE_DEPEND`
- `DEVMETHOD`, `DEVMETHOD_END`
- `SYSCTL_ADD_NODE`, `SYSCTL_ADD_UINT`, etc.
- `if_alloc`, `if_free`, `ether_ifattach`, `if_attach`
- `cdevsw`, `make_dev`, `destroy_dev`

Cada elemento de esta lista es candidato a convertirse en un wrapper. No todos necesitan ser envueltos, y un driver que envuelve todo está sobrediseñado. Pero la lista te indica dónde ocurriría el trabajo de portabilidad si alguna vez fuera necesario, y es un artefacto útil para el mantenimiento incluso sin ningún plan de portabilidad entre BSD.

### Cuándo comprometerse con el soporte entre BSD

El soporte entre BSD no es gratuito. Impone un coste continuo sobre el desarrollo, porque cada funcionalidad debe probarse en cada BSD soportado. Impone un coste en legibilidad, porque los wrappers reemplazan las llamadas directas a la API. Impone un coste en rendimiento, aunque normalmente pequeño, porque los wrappers añaden indirección.

Ese coste merece la pena cuando existe una razón clara. Algunos ejemplos:

- **Tienes usuarios en múltiples BSD.** Un fabricante de dispositivos que quiere que su producto sea compatible con FreeBSD y NetBSD tiene una razón empresarial para asumir la complejidad.
- **El driver se desarrolla en comunidad entre varios BSD.** Los proyectos de código abierto a veces tienen colaboradores en los tres BSD, y unificar la base de código reduce el trabajo duplicado.
- **El driver está financiado por un proyecto con requisitos entre BSD.** Algunos proyectos académicos, infraestructuras de investigación o despliegues embebidos tienen requisitos de OS mixtos integrados en sus planes.

Si ninguno de estos casos se aplica, no diseñes de antemano para el soporte entre BSD. Sigue los patrones de este capítulo, porque son buenas prácticas de FreeBSD por derecho propio, y permanece dentro de FreeBSD. Si la necesidad de soporte entre BSD aparece más adelante, los patrones ya establecidos harán que la portabilidad eventual sea más económica que si no los hubieras aplicado.

### Un ejemplo concreto de wrapping

Para que el enfoque de wrapping entre BSD resulte concreto, considera un driver que utiliza un temporizador callout. En FreeBSD, la API es la familia `callout`. El código podría ser así:

```c
callout_init_mtx(&sc->sc_timer, &sc->sc_mtx, 0);
callout_reset(&sc->sc_timer, hz / 10, portdrv_timer_cb, sc);
/* ... later ... */
callout_stop(&sc->sc_timer);
callout_drain(&sc->sc_timer);
```

En NetBSD, la API análoga es también la familia `callout`, pero las firmas difieren ligeramente. En OpenBSD, `timeout` es el nombre equivalente. Un driver entre BSD envuelve el uso:

```c
/* portdrv_os.h */
#ifdef __FreeBSD__
typedef struct callout portdrv_timer_t;
#define PD_TIMER_INIT(t, m)   callout_init_mtx((t), (m), 0)
#define PD_TIMER_ARM(t, ticks, cb, arg) \
    callout_reset((t), (ticks), (cb), (arg))
#define PD_TIMER_STOP(t)      callout_stop(t)
#define PD_TIMER_DRAIN(t)     callout_drain(t)
#endif

#ifdef __NetBSD__
typedef struct callout portdrv_timer_t;
/* ... NetBSD-specific definitions ... */
#endif

#ifdef __OpenBSD__
typedef struct timeout portdrv_timer_t;
#define PD_TIMER_INIT(t, m)   timeout_set((t), (cb), (arg))
#define PD_TIMER_ARM(t, ticks, cb, arg) \
    timeout_add((t), (ticks))
/* ... */
#endif
```

El código del core usa las macros `PD_TIMER_*`. Las ramas por OS en el header traducen entre el nombre abstracto y la API concreta de cada OS. Este es el enfoque de wrapper en miniatura: una abstracción, muchas implementaciones.

Observa que el wrapper hace algo más que renombrar llamadas. También cubre diferencias reales en el funcionamiento de las APIs. El `callout_init_mtx` de FreeBSD vincula el callout a un mutex para que el callback se ejecute con el mutex adquirido. El `timeout_set` de OpenBSD no tiene este vínculo; el callback debe adquirir el mutex por sí mismo. Las macros del wrapper absorben esta diferencia en beneficio del core, presentando la semántica de FreeBSD como la forma canónica. En OpenBSD, la implementación de `PD_TIMER_INIT` puede registrar un callback intermedio que adquiere el mutex antes de llamar al callback del usuario.

Esto muestra tanto el poder como el coste del wrapping. El poder está en que el código del core no necesita saber nada sobre la diferencia. El coste está en que la capa de wrapper tiene lógica no trivial propia, que debe mantenerse y probarse. El wrapping no es gratuito, y la capa de wrapper crece a medida que crece el conjunto de APIs envueltas.

### En qué consiste realmente «portabilizar»

Una portabilización real entre BSD raramente es un único commit. Es una secuencia de pequeños pasos, cada uno de los cuales hace el driver un poco más portable antes del salto final al OS objetivo:

1. **Auditoría.** Lista cada API específica de FreeBSD que usa el driver. La lista se convierte en el plan de trabajo.
2. **Envuelve.** Introduce wrappers, una API a la vez. Para cada wrapper, reemplaza cada llamada en el driver por el wrapper. Haz un commit tras cada API.
3. **Prueba en FreeBSD.** Confirma que los wrappers no cambian el comportamiento en FreeBSD. Este paso es importante: un bug introducido durante el wrapping debe detectarse antes del port, no durante él.
4. **Compila en el objetivo.** Añade el archivo header específico del objetivo. Implementa los wrappers para el OS objetivo. Compila y corrige los errores de compilación hasta que el driver enlace correctamente.
5. **Carga en el objetivo.** Carga el módulo. Corrige los símbolos o dependencias que falten.
6. **Prueba en el objetivo.** Ejercita las funcionalidades del driver. Corrige los bugs que aparezcan.
7. **Itera.** Cada corrección de bug debería volver al driver de FreeBSD si expone un problema real, no una peculiaridad específica de la plataforma.

Esto está lejos de ser una tarea de un día. Para un driver de tamaño mediano, suele ser una semana o dos de trabajo concentrado. El coste se amortiza cuando el port tiene éxito y la base de código compartida resulta más fácil de mantener que dos drivers separados.

### Cuando la portabilidad entre BSD implica portabilidad multiplataforma en general

Algunos drivers se benefician de ejecutarse no solo en otros BSD, sino en otros sistemas operativos por completo. Linux es el objetivo más obvio, pero también macOS, Windows, e incluso reimplementaciones en espacio de usuario de los servicios del kernel. Un driver escrito con una interfaz de backend limpia y un core que habla únicamente en su propio vocabulario puede usarse como pieza de construcción en cualquiera de estos entornos; solo el backend y la capa de wrapper cambian.

Esta es la razón profunda por la que los patrones de este capítulo importan. No son solo para FreeBSD; son sobre cómo estructurar cualquier driver de modo que el OS sea un detalle. Las herramientas específicas (`bus_dma`, `bus_space`, `__FreeBSD_version`) son de FreeBSD, pero los patrones que encarnan son universales.

### Referencias para los curiosos sobre NetBSD y OpenBSD

Si sientes curiosidad por NetBSD o OpenBSD, algunos apuntes te ahorrarán tiempo.

La documentación de drivers de NetBSD reside en su árbol, y el equivalente a `/usr/src/sys` es el mismo en NetBSD; puedes comparar los dos árboles directamente. La página de manual `bus_dma(9)` de NetBSD explica su API con gran detalle. La infraestructura `config(8)` es la respuesta de NetBSD a Newbus y tiene su propio manual.

El estilo de drivers de OpenBSD es más conservador que el de FreeBSD. OpenBSD valora la simplicidad por encima de la flexibilidad, y sus drivers tienden a ser más pequeños y directos que los equivalentes de FreeBSD. Si lees código fuente de drivers de OpenBSD, verás menos capas de abstracción y menos opciones de compilación. Es una elección de diseño; no es intrínsecamente mejor ni peor que el estilo de FreeBSD.

Ambos proyectos tienen excelentes listas de correo y canales de IRC. Si portas un driver, pedir revisión a la comunidad ahorra tiempo. Ambas comunidades son en general acogedoras con los desarrolladores de FreeBSD que se acercan a ellas con respeto.

### Cerrando la sección 7

La portabilidad entre BSD es un tema profundo, pero sus fundamentos son simples: aísla las dependencias del subsistema del kernel detrás de wrappers, mantén la lógica del core independiente del OS y lista las APIs específicas de FreeBSD que uses para saber dónde están los límites. Si alguna vez necesitas portar a NetBSD o OpenBSD, solo tendrás que traducir el límite; el core pasa intacto. El wrapping en sí no es un trabajo trivial, pero la capa de wrapper crece lentamente y es proporcional a la amplitud de APIs que usa el driver, no al tamaño total del driver. Para la mayoría de los drivers exclusivos de FreeBSD, la preparación para la portabilidad entre BSD no está justificada; los patrones que la soportan merecen conocerse porque hacen que el driver sea mejor incluso cuando permanece exclusivamente en FreeBSD.

En la sección 8, damos un paso atrás y abordamos una pregunta diferente: ¿cómo empaquetas, documentas y versionas un driver portable para que otros puedan compilarlo, probarlo y usarlo de forma segura? El buen código no es suficiente; las buenas prácticas en torno al código también importan.

## Sección 8: Refactorización y versionado para la portabilidad

Un driver portable es un artefacto, pero la portabilidad también es una práctica. El código es una parte; la forma en que se organiza, documenta y versiona es el resto. Esta sección aborda ese resto. Asume que ya has refactorizado el driver con la estructura descrita en las secciones 2 a 7, y plantea: ¿qué más deberías hacer para que tu trabajo sobreviva al contacto con otras personas y otros sistemas?

Las respuestas implican documentación, versionado, validación del build y un poco de mantenimiento rutinario. Nada de esto es glamuroso. Todo ello se amortiza la primera vez que alguien nuevo intenta usar tu driver.

### Un breve README.portability.md

Todo driver portable se beneficia de un documento breve que indique sus plataformas soportadas, los backends soportados, las opciones de compilación y las limitaciones conocidas. En el driver de referencia `portdrv`, ese documento es `README.portability.md`. Un lector que tome el driver por primera vez debería poder leerlo en unos minutos y saber si su entorno es compatible.

Un buen README de portabilidad tiene cuatro secciones.

**Plataformas soportadas.** Lista las versiones de FreeBSD y arquitecturas de CPU en las que se ha probado el driver, y aquellas en las que se sabe o se espera que funcione aunque no hayan sido probadas. Por ejemplo:

```text
Tested on:
- FreeBSD 14.3-RELEASE, amd64
- FreeBSD 14.3-RELEASE, arm64 (QEMU guest)

Expected to work but not regularly tested:
- FreeBSD 14.2-RELEASE and later, amd64
- FreeBSD 14.3-RELEASE, riscv64

Not supported:
- FreeBSD 13.x and earlier (API changes required)
- NetBSD / OpenBSD (see Section 7; see compatibility file portdrv_os.h)
```

**Backends soportados.** Lista cada backend, el entorno de hardware o software al que apunta y cualquier restricción. Por ejemplo:

```text
pci: PCI variant, requires a device with vendor 0x1234, device 0x5678.
usb: USB variant, requires a USB 2.0 or later host controller.
sim: Pure software simulation. No hardware required. Useful for tests.
```

**Opciones de compilación.** Lista cada flag `-D` que reconoce el Makefile y qué hace cada uno. Menciona qué combinaciones se espera que funcionen:

```text
PORTDRV_WITH_PCI=yes       Enable the PCI backend.
PORTDRV_WITH_USB=yes       Enable the USB backend.
PORTDRV_WITH_SIM=yes       Enable the simulation backend.
PORTDRV_DEBUG=yes          Enable verbose debug logging.

If no backend flag is set, PORTDRV_WITH_SIM is enabled by default
so that a plain "make" produces a loadable module.
```

**Limitaciones conocidas.** Indica con honestidad qué no soporta el driver. Una lista corta y honesta es más útil que una larga o evasiva.

```text
The simulation backend does not attempt to emulate interrupt
latency or DMA bandwidth limits. It completes transfers
synchronously. Do not use it as a substitute for performance
testing against real hardware.
```

Un README de portabilidad vive junto al código fuente del driver. Actualízalo siempre que cambie el conjunto de plataformas soportadas. Si tomas un driver que no tiene uno, escribirlo es la actividad de revisión más valiosa que puedes realizar.

### Versionar el driver

Los módulos del kernel llevan una versión mediante `MODULE_VERSION`. Un driver portable debería versionar tres cosas: el driver en su conjunto, cada backend y la interfaz del backend.

La versión del driver es un entero único que se pasa a `MODULE_VERSION`:

```c
MODULE_VERSION(portdrv, 2);
```

Increméntalo siempre que cambies algo que un consumidor dependiente pueda observar, por ejemplo cuando añadas un nuevo ioctl, cambies la semántica de uno existente o renombres un sysctl.

Cada backend tiene su propio registro de módulo. Versiona cada uno de forma independiente:

```c
MODULE_VERSION(portdrv_pci, 1);
MODULE_DEPEND(portdrv_pci, portdrv_core, 1, 2, 2);
```

La llamada a `MODULE_DEPEND` expresa que `portdrv_pci` depende de `portdrv_core` con una versión en el rango [1, 2] donde 2 es el valor preferido. El kernel rechaza cargar el backend contra un core que no entiende, lo que evita que una incompatibilidad produzca caídas misteriosas.

La propia interfaz de backend puede versionarse dentro de la estructura. Un campo `version` al inicio de `struct portdrv_backend` permite que el núcleo compruebe que cada backend se compiló contra la forma correcta:

```c
struct portdrv_backend {
	uint32_t     version;
	const char  *name;
	int   (*attach)(struct portdrv_softc *sc);
	/* ... */
};

#define PORTDRV_BACKEND_VERSION 2
```

Cuando el `portdrv_core_attach` del núcleo detecta que `sc->sc_be->version != PORTDRV_BACKEND_VERSION`, rechaza el attach y registra un mensaje claro. Esto captura el caso en que alguien actualiza el núcleo pero olvida recompilar un backend, sin necesidad de que el kernel sufra un crash para dejar constancia del problema.

Usa estos mecanismos con moderación. Cada uno impone un coste en tiempo de mantenimiento, porque cualquier incremento de versión debe coordinarse. La recompensa es que, cuando algo falla, el error es inmediato y claro en lugar de vago y diferido.

### Documenta las configuraciones de build soportadas

Junto a `README.portability.md`, mantén una breve **matriz de build** que registre qué combinaciones de backends y opciones se espera que compilen y cuáles han sido probadas recientemente. Es un artefacto de gestión de proyectos, no de ejecución, pero es de un valor incalculable cuando un nuevo mantenedor se hace cargo o cuando un revisor quiere saber si una configuración específica seguirá funcionando después de un cambio.

Una matriz de build práctica tiene este aspecto:

```text
| Config                             | Compiles | Tested | Notes           |
|------------------------------------|----------|--------|-----------------|
| (default: SIM only)                | yes      | yes    | Baseline CI.    |
| PCI only                           | yes      | yes    |                 |
| USB only                           | yes      | yes    |                 |
| PCI + USB                          | yes      | yes    |                 |
| PCI + USB + SIM                    | yes      | yes    | Full build.     |
| PCI + DEBUG                        | yes      | yes    |                 |
| PCI + USB + DEBUG                  | yes      | no     | Should be OK.   |
| None of PCI/USB/SIM (no backend)   | yes      | yes    | SIM auto-enabled|
```

Regenera la matriz antes de cada release. Es una pequeña tarea que ahorra mucho tiempo después. Cuando los usuarios registran errores contra configuraciones específicas, puedes ver de un vistazo si se afirmaba que esa configuración funcionaba y, en caso afirmativo, si fue probada en el último release.

### Validar el build antes de una release

Una buena disciplina de release para un driver portable es validar automáticamente cada configuración documentada antes de cada release. La automatización lo hace económico. Un script de shell corto que recorre la matriz de build, invoca `make clean` y `make`, y reporta el éxito o el fracaso, suele ser suficiente:

```sh
#!/bin/sh
# Validate portdrv builds in every supported configuration.

set -e

configs="
    PORTDRV_WITH_SIM=yes
    PORTDRV_WITH_PCI=yes
    PORTDRV_WITH_USB=yes
    PORTDRV_WITH_PCI=yes PORTDRV_WITH_USB=yes
    PORTDRV_WITH_PCI=yes PORTDRV_WITH_USB=yes PORTDRV_WITH_SIM=yes
    PORTDRV_WITH_PCI=yes PORTDRV_DEBUG=yes
"

OLDIFS="$IFS"
echo "$configs" | while read cfg; do
    [ -z "$cfg" ] && continue
    echo "==> Building with: $cfg"
    make clean > /dev/null
    if env $cfg make > build.log 2>&1; then
        echo "    OK"
    else
        echo "    FAIL (see build.log)"
        tail -n 20 build.log
        exit 1
    fi
done
```

Ejecuta esto antes de etiquetar una release. Cuando pasa, sabes que todas las configuraciones que documentas como soportadas compilan de verdad. Cuando falla, sabes exactamente qué configuración se rompió y puedes corregirla antes de distribuirlo. Esta es la práctica de calidad más barata y valiosa que un driver portable puede adoptar.

### Cuándo incrementar la versión

Una pregunta frecuente entre los nuevos autores de drivers es: «¿Cuándo debo incrementar el número de versión del módulo?». La respuesta corta es que se incrementa cuando cambia cualquier cosa observable para los consumidores. La respuesta larga distingue tres tipos de cambio:

**Adiciones compatibles hacia atrás.** Añadir un nuevo comando ioctl, un nuevo sysctl, una nueva opción de módulo, sin cambiar el comportamiento de los comandos existentes. Incrementa la versión menor. Los consumidores existentes siguen funcionando.

**Cambios incompatibles hacia atrás.** Renombrar un ioctl, cambiar el significado de un sysctl existente o romper un comportamiento previamente documentado. Incrementa la versión mayor. Actualiza el `MODULE_DEPEND` en los módulos dependientes. Documenta la ruptura en una nota de lanzamiento.

**Refactorizaciones internas que los usuarios no pueden observar.** Mover funciones entre archivos, renombrar variables privadas, reformatear. No incrementes la versión. Los cambios internos son internos.

Ten en cuenta que `MODULE_VERSION` es un entero y los consumidores deciden cómo interpretarlo. Para un driver con una base de usuarios pequeña, un número de versión por cambio observable es suficiente. Para un driver con una comunidad más grande y más dependientes, establecer una convención como «major * 100 + minor» te permite codificar más información en un único entero. En cualquier caso, documenta qué significan tus números de versión para que los futuros mantenedores y consumidores puedan interpretarlos.

### La refactorización para la portabilidad como disciplina continua

Un driver portable no es un artefacto terminado. Incluso después de la refactorización inicial, el driver continúa evolucionando: nuevas funcionalidades, nuevas variantes de hardware, nuevas APIs del kernel, nuevas correcciones de errores. Mantener el driver portable es una disciplina continua, no un logro puntual.

Tres hábitos ayudan.

**Revisa los nuevos parches en busca de riesgos de portabilidad.** Cuando un colaborador añade un nuevo acceso a registro, ¿pasa por el accessor? Cuando añaden una nueva función específica del backend, ¿encaja en la interfaz, o fuerza un `if (sc->sc_be == pci)` en el núcleo? Cuando añaden un nuevo tipo, ¿es de ancho fijo? Estas preguntas llevan unos pocos segundos por parche y detectan la mayoría de las regresiones.

**Vuelve a ejecutar la matriz de build con regularidad.** Un build automatizado mensual o semanal de todas las configuraciones soportadas detecta las regresiones silenciosas en el momento en que aparecen. Si los recursos de CI son limitados, ejecuta la matriz en los pull requests y de forma nocturna en la rama principal.

**Vuelve a probar en una plataforma que no sea amd64 periódicamente.** Incluso un único guest `arm64` arrancado bajo QEMU una vez al trimestre detecta un número sorprendente de errores arquitectónicos. La prueba no tiene que ser exhaustiva; con solo cargar el driver y ejecutar el backend de simulación es suficiente para revelar muchos problemas de endianness y alineamiento.

### La forma final de un driver portable

Después de todo esto, un driver portable tiene una forma reconocible. El núcleo es pequeño y no conoce los buses específicos. Cada backend es un archivo ligero que implementa una interfaz limpia. Los encabezados son cortos y cada uno tiene un propósito claro. El Makefile usa feature flags para seleccionar backends y opciones. El driver usa `uint32_t` y su familia para tamaños, `bus_read_*` y `bus_write_*` para el acceso a registros, `bus_dma_*` para DMA, y funciones auxiliares de endianness cuando los valores multibyte cruzan el límite del hardware. El driver documenta sus plataformas soportadas y las opciones de build. El driver tiene una matriz de build y un script de validación.

Un lector que abre el código fuente por primera vez puede encontrar lo que necesita en minutos. Un nuevo colaborador puede añadir un backend en una tarde. Un revisor puede auditar un parche sin leer el driver completo. Esa es la forma.

### Estabilidad de ABI frente a API

Al versionar drivers, a menudo se pasa por alto una distinción sutil: la diferencia entre la **interfaz de programación de aplicaciones** (API) y la **interfaz binaria de aplicaciones** (ABI). La API es lo que ve un programador: nombres de funciones, tipos de parámetros, comportamiento esperado. La ABI es lo que ve el enlazador: nombres de símbolos, disposición de estructuras, convenciones de llamada, alineamiento.

Un cambio que no modifica la API puede igualmente romper la ABI. Añadir un campo en mitad de una estructura es un ejemplo clásico: los archivos fuente que usan la estructura compilan sin cambios, pero los módulos binarios compilados contra la disposición antigua leerán campos incorrectos en tiempo de ejecución porque los desplazamientos han cambiado.

Para los módulos del kernel, la estabilidad de la ABI importa porque los módulos se cargan en un kernel en ejecución y el kernel y el módulo deben coincidir en la disposición de las estructuras que comparten. Esta es la razón por la que `MODULE_DEPEND` incluye números de versión: que un módulo antiguo se niegue a cargarse contra un kernel más nuevo, o viceversa, es más seguro que leer silenciosamente campos incorrectos.

Dos reglas ayudan a preservar la estabilidad de la ABI a lo largo del tiempo.

**Regla uno: añade campos solo al final de las estructuras.** Los campos existentes conservan sus desplazamientos y los consumidores más antiguos los leen correctamente. Los consumidores más nuevos, que conocen el campo añadido, pueden acceder a él al final.

**Regla dos: no cambies nunca el tipo ni el tamaño de un campo existente.** Si necesitas ampliar un campo, añade uno nuevo y deja el antiguo por compatibilidad. Es incómodo, pero es el precio de la estabilidad de la ABI.

Los drivers con una audiencia pequeña pueden ignorar la estabilidad de la ABI la mayor parte del tiempo, porque cada release reconstruye todo. Los drivers cuyos consumidores no pueden reconstruir todo, como los módulos out-of-tree que los usuarios compilan contra su propio kernel, deben ser más cuidadosos.

### Rutas de actualización y degradación

Un driver maduro debe ser utilizable durante la actualización y la degradación. Las actualizaciones son sencillas: instala el nuevo módulo, descarga el antiguo y carga el nuevo. Las degradaciones son a veces más difíciles, porque el nuevo módulo puede haber guardado estado que el módulo antiguo no puede leer.

Para la mayoría de drivers, el estado no es persistente, por lo que la degradación es fácil. Para los drivers que almacenan estado en los datos de un dispositivo de caracteres, en metadatos del sistema de archivos o en una base de datos que mantiene el propio dispositivo, las rutas de degradación merecen reflexión. Tu formato de estado debe incluir un campo de versión; las versiones antiguas deben rechazar la lectura de formatos más nuevos y producir un error claro; las versiones más nuevas deben leer los formatos antiguos cuando sea posible.

Las notas de lanzamiento son el mecanismo de despliegue más sencillo. Una nota de lanzamiento que diga «esta versión cambia el formato del estado; la degradación no es posible sin pérdida de datos» vale más que un sofisticado sistema de migración de estado que nadie usa. Comunica el impacto de la actualización con claridad y deja que tus usuarios decidan.

### Telemetría y observabilidad

Un driver portable desplegado ampliamente se beneficia de una telemetría ligera. No el registro intrusivo que inunda el log del kernel, sino contadores discretos que un operador puede consultar para ver si el driver está sano. El mecanismo habitual de FreeBSD es un árbol de sysctl:

```c
SYSCTL_ADD_UQUAD(ctx, tree, OID_AUTO, "transfers_ok",
    CTLFLAG_RD, &sc->sc_stats.transfers_ok,
    "Successful transfers");
SYSCTL_ADD_UQUAD(ctx, tree, OID_AUTO, "transfers_err",
    CTLFLAG_RD, &sc->sc_stats.transfers_err,
    "Failed transfers");
SYSCTL_ADD_UQUAD(ctx, tree, OID_AUTO, "queue_depth",
    CTLFLAG_RD, &sc->sc_stats.queue_depth,
    "Current queue depth");
```

Los operadores pueden consultarlos con `sysctl dev.portdrv.0`, los sistemas de monitorización pueden recopilarlos a intervalos y los informes de errores pueden incluir sus valores sin que el informador deba hacer nada especial. El coste para el driver es un campo por contador y un registro sysctl por campo. Para un driver maduro, el árbol de telemetría crece de forma natural a medida que surgen preguntas.

Añade la telemetría primero, depura después. Cuando llega un informe de error y los valores de sysctl muestran lo que ocurrió, la depuración es mucho más rápida que cuando esos valores faltan.

### Lista de comprobación tras la refactorización

Tras una refactorización de portabilidad, vale la pena recorrer una breve lista de comprobación antes de declarar el trabajo terminado. La lista es deliberadamente concisa; cada elemento representa una sutileza que debes confirmar.

1. ¿Incluye el archivo núcleo algún encabezado específico de bus? En caso afirmativo, mueve la inclusión o el código que la usa.
2. ¿Todo acceso a registros pasa por un accessor o un método de backend? Busca con grep `bus_read_`, `bus_write_`, `bus_space_read_` y `bus_space_write_` en el archivo núcleo; cada aparición es un candidato a revisar.
3. ¿Usa el driver tipos de ancho fijo para todo valor cuyo tamaño importa? Busca `int`, `long`, `unsigned` y `size_t` en contextos de acceso a registros; reemplázalos con `uint32_t`, `uint64_t` y similares según corresponda.
4. ¿Todo valor multibyte que cruza el límite del hardware pasa por una función auxiliar de endianness? Busca conversiones directas `*(uint32_t *)` en buffers DMA e imágenes de registros.
5. ¿Tiene éxito el build en todas las configuraciones documentadas? Ejecuta el script de la matriz de build.
6. ¿Se carga el módulo correctamente, con el banner esperado? Carga cada configuración en orden y comprueba dmesg.
7. ¿Se desconecta el driver correctamente? Descárgalo y confirma que no hay panics ni recursos perdidos.
8. ¿Está la documentación al día? Vuelve a leer `README.portability.md` y asegúrate de que refleja la realidad.
9. ¿Está el cuaderno de laboratorio al día? Anota qué se hizo, qué te sorprendió y qué esperas cambiar en el futuro.

Recorrer la lista lleva aproximadamente una hora. Encontrar un problema durante la comprobación es menos costoso que encontrarlo en producción.

### Despliegues y reversiones

Cuando un driver refactorizado está listo para distribuirse, considera la estrategia de despliegue. Para una base de usuarios pequeña, un único release es suficiente. Para una base mayor, un despliegue escalonado reduce el riesgo:

1. **Pruebas internas.** Despliega primero en sistemas de prueba internos. Ejecuta el driver durante al menos un día antes de exponerlo a usuarios externos.
2. **Primeros adoptantes.** Distribúyelo a un grupo reducido de usuarios externos que hayan optado por ello. Recoge opiniones durante una semana.
3. **Lanzamiento general.** Distribúyelo a todos los usuarios. Anuncia el lanzamiento con un registro de cambios.
4. **Plan de reversión.** Documenta cómo volver a la versión anterior. Un driver que puede descargarse y reemplazarse por la versión anterior es más seguro que uno que está entrelazado con el resto del sistema.

La granularidad del despliegue depende de tu proyecto. Un proyecto personal no necesita despliegues escalonados. Un driver comercial que atiende a cientos de usuarios sí los necesita. Piensa en el equilibrio y elige de forma deliberada.

### Cerrando la sección 8

Ahora sabes no solo cómo escribir código portable, sino cómo empaquetarlo para que otros puedan consumirlo de forma segura. El versionado, la documentación, una matriz de build y un script de validación son las pequeñas e inglamorosas prácticas que separan un driver de aficionado de uno listo para producción. Cuestan poco adoptarlas y dan sus frutos la primera vez que alguien más toca el código, incluido tu yo futuro. La estabilidad de la ABI, las rutas de actualización, la telemetría, una lista de comprobación tras la refactorización y una estrategia de despliegue son las prácticas complementarias que maduran un driver a largo plazo.

El material restante del capítulo incluye laboratorios prácticos, ejercicios de desafío y una guía de resolución de problemas. Los laboratorios te permiten aplicar todas las técnicas de este capítulo al driver de referencia. Los desafíos van más allá, para que puedas practicar los mismos patrones sobre variaciones del mismo problema. La guía de resolución de problemas cataloga los fallos que es más probable que encuentres a lo largo del camino.

## Laboratorios prácticos

Los laboratorios de este capítulo te guían a través de la transformación de un driver pequeño de un solo archivo en un driver portable, con múltiples backends y múltiples archivos. La implementación de referencia, `portdrv`, se encuentra en `examples/part-07/ch29-portability/`. Cada laboratorio es autocontenido y te deja con un módulo funcional que se carga sin problemas. Puedes hacerlos todos o detenerte en cualquier momento; cada uno es útil por sí solo.

Antes de empezar, accede al directorio de ejemplos e inspecciona su estado actual:

```sh
cd /path/to/FDD-book/examples/part-07/ch29-portability
ls
```

Deberías ver un directorio `lab01-monolithic/` que contiene el driver de punto de partida, junto con los directorios `lab02` hasta `lab07` que contienen los pasos sucesivos de refactorización. Cada directorio de laboratorio tiene su propio `Makefile` y README. Trabaja primero en `lab01-monolithic`; cuando termines un laboratorio, pasa al siguiente directorio, que contiene el estado del driver tras ese paso.

Un consejo rápido de flujo de trabajo: haz una copia de trabajo local del directorio para cada laboratorio, de modo que puedas comparar tu versión con la de referencia después.

```sh
cp -r lab01-monolithic mywork
cd mywork
```

Después sigue las instrucciones.

### Configuración del entorno de laboratorio

Antes de comenzar cualquiera de los laboratorios, configura el entorno de trabajo. Unos minutos de preparación te ahorrarán horas de problemas después.

Crea un directorio de trabajo fuera del repositorio para no incluir accidentalmente la salida del laboratorio en un commit:

```sh
mkdir -p ~/fdd-labs/ch29
cd ~/fdd-labs/ch29
cp -r /path/to/FDD-book/examples/part-07/ch29-portability/* .
```

Comprueba que `make` funciona con los archivos del laboratorio por defecto antes de hacer ningún cambio:

```sh
cd lab01-monolithic
make clean && make
ls *.ko
```

Si el build falla, detente y depura ahora. Un laboratorio no sirve de nada si el estado inicial está roto.

A continuación, verifica que puedes cargar y descargar el driver de referencia:

```sh
sudo kldload ./portdrv.ko
dmesg | tail
sudo kldunload portdrv
```

Si la carga falla, comprueba `dmesg` para ver el mensaje de error. Los motivos más habituales son una dependencia de módulo que falta o un conflicto con otro driver. Corrígelos antes de comenzar los laboratorios.

Por último, empieza un cuaderno de registro sencillo. Un archivo de texto plano o un archivo Markdown en tu directorio de trabajo es suficiente:

```text
=== Chapter 29 Lab Logbook ===
Date: 2026-04-19
System: FreeBSD 14.3-RELEASE, amd64
```

Cada laboratorio añadirá una entrada. El cuaderno de registro no es para uso público; es un registro para ti mismo en el futuro.

### Laboratorio 1: Auditoría del driver monolítico

El driver de punto de partida es un único archivo que compila y se carga, pero mezcla todas las responsabilidades en un mismo lugar. El objetivo de este laboratorio es **detectar** los problemas de portabilidad sin corregirlos todavía. Entrenar la vista es el primer paso.

```sh
cd lab01-monolithic
less portdrv.c
```

Mientras lees, anota en tu cuaderno de laboratorio cada uno de los siguientes puntos:

1. ¿Qué líneas realizan accesos a registros? Marca cada `bus_space_read_*`, `bus_space_write_*`, `bus_read_*`, `bus_write_*` y cualquier desreferencia de puntero directo a memoria del dispositivo.
2. ¿Qué líneas asignan o manipulan buffers de DMA? Marca cada llamada a `bus_dma_*`.
3. ¿Qué líneas incluyen cabeceras específicas del bus, como `dev/pci/pcivar.h`?
4. ¿Qué líneas tienen IDs de fabricante o de dispositivo escritos directamente en el código?
5. ¿Qué líneas usan tipos que podrían ser incorrectos en una plataforma de 32 bits (`int`, `long`, `size_t`)?
6. ¿Qué líneas usan `htons`, `htonl` u otro helper de orden de bytes similar?
7. ¿Qué líneas están protegidas con `#ifdef`?

Ahora construye y carga el driver:

```sh
make clean
make
sudo kldload ./portdrv.ko
dmesg | tail -5
sudo kldunload portdrv
```

Confirma que el módulo se carga sin errores. Si no es así, corrige el build antes de continuar.

**Reflexión.** Al final del laboratorio, tu cuaderno de registro debería tener un párrafo breve que describa el estado de portabilidad actual del driver. Algo como: «Los accesos a registros pasan por `bus_read_*`, así que la portabilidad arquitectónica es aceptable, pero están dispersos por el archivo sin usar accessors. El código específico de PCI está entremezclado con la lógica central. No se usa ningún helper de orden de bytes en ningún lugar. No hay backend de simulación. El driver solo compila en `amd64`; no lo he probado en `arm64`.»

Este párrafo es la línea de base con la que cada laboratorio posterior medirá el progreso.

### Laboratorio 2: Introducción de accessors de registro

El primer cambio estructural consiste en centralizar el acceso a registros en accessors, tal como se describe en la Sección 2. Todavía no vas a dividir el archivo; solo vas a añadir una capa de indirección.

Añade dos funciones static inline al principio de `portdrv.c`:

```c
static inline uint32_t
portdrv_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	return (bus_read_4(sc->sc_bar, off));
}

static inline void
portdrv_write_reg(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	bus_write_4(sc->sc_bar, off, val);
}
```

Después, a lo largo del archivo, reemplaza cada `bus_read_4(sc->sc_bar, X)` por `portdrv_read_reg(sc, X)` y cada `bus_write_4(sc->sc_bar, X, V)` por `portdrv_write_reg(sc, X, V)`. Hazlo con cuidado. Después de cada pequeño lote de sustituciones, reconstruye:

```sh
make clean && make
```

Y recarga:

```sh
sudo kldunload portdrv 2>/dev/null
sudo kldload ./portdrv.ko
dmesg | tail -5
```

Confirma que el driver sigue funcionando. La capa de accessors es funcionalmente invisible; el comportamiento binario no debería cambiar.

**Punto de control.** Cuenta el número de apariciones de `bus_read_*` y `bus_write_*` en el archivo tras el cambio. Todas deberían estar dentro de los accessors. Si alguna está fuera, encuéntrala y sustitúyela. Esa revisión exhaustiva es el objetivo central del laboratorio.

### Laboratorio 3: Extracción de la interfaz de backend

Ahora que existen los accessors, añade una interfaz de backend. Crea un nuevo archivo `portdrv_backend.h` con la definición de la estructura de la Sección 3:

```c
#ifndef _PORTDRV_BACKEND_H_
#define _PORTDRV_BACKEND_H_

struct portdrv_softc;

struct portdrv_backend {
	const char *name;
	int   (*attach)(struct portdrv_softc *sc);
	void  (*detach)(struct portdrv_softc *sc);
	uint32_t (*read_reg)(struct portdrv_softc *sc, bus_size_t off);
	void     (*write_reg)(struct portdrv_softc *sc, bus_size_t off,
	                      uint32_t val);
};

extern const struct portdrv_backend portdrv_pci_backend;

#endif
```

En `portdrv.c`, añade el campo del softc:

```c
struct portdrv_softc {
	device_t sc_dev;
	struct resource *sc_bar;
	int sc_bar_rid;
	const struct portdrv_backend *sc_be;   /* new */
	/* ... */
};
```

Reescribe las funciones accessor existentes para que despachen a través del backend:

```c
static inline uint32_t
portdrv_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	return (sc->sc_be->read_reg(sc, off));
}

static inline void
portdrv_write_reg(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	sc->sc_be->write_reg(sc, off, val);
}
```

Define una primera instancia de la interfaz:

```c
static uint32_t
portdrv_pci_read_reg_impl(struct portdrv_softc *sc, bus_size_t off)
{
	return (bus_read_4(sc->sc_bar, off));
}

static void
portdrv_pci_write_reg_impl(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	bus_write_4(sc->sc_bar, off, val);
}

const struct portdrv_backend portdrv_pci_backend = {
	.name     = "pci",
	.read_reg = portdrv_pci_read_reg_impl,
	.write_reg = portdrv_pci_write_reg_impl,
	/* attach and detach stay NULL for now. */
};
```

En la función `portdrv_attach` existente, instala el backend:

```c
sc->sc_be = &portdrv_pci_backend;
```

Reconstruye, carga y confirma que el driver sigue funcionando.

**Punto de control.** La lógica central ya no toca `bus_read_*` ni `bus_write_*` directamente. Cada acceso a un registro pasa por el backend. Verifica esto buscando `bus_read_` y `bus_write_` en el archivo con grep; todas deberían estar dentro de las funciones `_impl`.

### Laboratorio 4: División del núcleo y el backend en archivos separados

Ahora que la interfaz está en su lugar, divide el archivo.

Crea tres archivos:

- `portdrv_core.c`: contiene la definición del softc, los accessors de registro, la lógica de attach/detach que no es específica de PCI y el registro del módulo para el núcleo.
- `portdrv_pci.c`: contiene el probe PCI, el attach y detach específicos de PCI, las funciones `_impl` y la estructura del backend.
- `portdrv_backend.h`: contiene la interfaz del backend.

Los detalles de la división están en la implementación de referencia bajo `lab04-split`. Estúdiala después de haber hecho tu propia división y compara.

Actualiza el `Makefile` para listar ambos archivos fuente:

```make
SRCS= portdrv_core.c portdrv_pci.c
```

Reconstruye y carga. El driver debería cargarse de forma idéntica a antes, pero el código fuente ahora está organizado.

**Punto de control.** Abre `portdrv_core.c` y busca `#include <dev/pci/pcivar.h>`. No debería aparecer. El núcleo está libre de includes específicos de PCI. Ese es el hito.

### Laboratorio 5: Incorporación de un backend de simulación

Añade un segundo backend que no requiera ningún hardware. Crea `portdrv_sim.c` con su propia implementación de la interfaz de backend:

```c
/* portdrv_sim.c - simulation backend for portdrv */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/malloc.h>

#include "portdrv.h"
#include "portdrv_common.h"
#include "portdrv_backend.h"

struct portdrv_sim_priv {
	uint32_t regs[256];  /* simulated register file */
};

static uint32_t
portdrv_sim_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	struct portdrv_sim_priv *psp = sc->sc_backend_priv;
	if (off / 4 >= nitems(psp->regs))
		return (0);
	return (psp->regs[off / 4]);
}

static void
portdrv_sim_write_reg(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	struct portdrv_sim_priv *psp = sc->sc_backend_priv;
	if (off / 4 < nitems(psp->regs))
		psp->regs[off / 4] = val;
}

static int
portdrv_sim_attach_be(struct portdrv_softc *sc)
{
	return (0);
}

static void
portdrv_sim_detach_be(struct portdrv_softc *sc)
{
}

const struct portdrv_backend portdrv_sim_backend = {
	.name     = "sim",
	.attach   = portdrv_sim_attach_be,
	.detach   = portdrv_sim_detach_be,
	.read_reg = portdrv_sim_read_reg,
	.write_reg = portdrv_sim_write_reg,
};
```

Añade un hook de carga de módulo que cree una instancia simulada cuando el driver se carga sin hardware real:

```c
static int
portdrv_sim_modevent(module_t mod, int type, void *arg)
{
	static struct portdrv_softc *sim_sc;
	static struct portdrv_sim_priv *sim_priv;

	switch (type) {
	case MOD_LOAD:
		sim_sc = malloc(sizeof(*sim_sc), M_PORTDRV, M_WAITOK | M_ZERO);
		sim_priv = malloc(sizeof(*sim_priv), M_PORTDRV,
		    M_WAITOK | M_ZERO);
		sim_sc->sc_be = &portdrv_sim_backend;
		sim_sc->sc_backend_priv = sim_priv;
		return (portdrv_core_attach(sim_sc));
	case MOD_UNLOAD:
		if (sim_sc != NULL) {
			portdrv_core_detach(sim_sc);
			free(sim_priv, M_PORTDRV);
			free(sim_sc, M_PORTDRV);
		}
		return (0);
	}
	return (0);
}

static moduledata_t portdrv_sim_mod = {
	"portdrv_sim",
	portdrv_sim_modevent,
	NULL
};

DECLARE_MODULE(portdrv_sim, portdrv_sim_mod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(portdrv_sim, 1);
MODULE_DEPEND(portdrv_sim, portdrv_core, 1, 1, 1);
```

Actualiza el Makefile para incluir el nuevo archivo:

```make
SRCS= portdrv_core.c portdrv_pci.c portdrv_sim.c
```

Reconstruye y carga:

```sh
make clean && make
sudo kldload ./portdrv.ko
dmesg | tail -10
```

Deberías ver tanto el backend PCI registrándose con el bus como una instancia simulada creada por el hook de módulo del backend de simulación. Interactúa con la instancia simulada a través de su dispositivo de caracteres (si lo hay) y confirma que las escrituras y lecturas de registros se comportan como enteros almacenados.

**Punto de control.** Ahora tienes un driver que compila con dos backends y ejecuta ambos simultáneamente. El backend PCI gestiona cualquier hardware real que descubra el kernel; el backend de simulación te proporciona una instancia de software para pruebas.

### Laboratorio 6: Compilación condicional de backends

Haz que cada backend sea condicional a un indicador en tiempo de compilación. Edita el Makefile:

```make
KMOD= portdrv
SRCS= portdrv_core.c

.if defined(PORTDRV_WITH_PCI) && ${PORTDRV_WITH_PCI} == "yes"
SRCS+= portdrv_pci.c
CFLAGS+= -DPORTDRV_WITH_PCI
.endif

.if defined(PORTDRV_WITH_SIM) && ${PORTDRV_WITH_SIM} == "yes"
SRCS+= portdrv_sim.c
CFLAGS+= -DPORTDRV_WITH_SIM
.endif

.if !defined(PORTDRV_WITH_PCI) && !defined(PORTDRV_WITH_SIM)
SRCS+= portdrv_sim.c
CFLAGS+= -DPORTDRV_WITH_SIM
.endif

SYSDIR?= /usr/src/sys
.include <bsd.kmod.mk>
```

Compila con varias combinaciones:

```sh
make clean && make PORTDRV_WITH_PCI=yes
make clean && make PORTDRV_WITH_SIM=yes
make clean && make PORTDRV_WITH_PCI=yes PORTDRV_WITH_SIM=yes
make clean && make
```

Confirma que cada build tiene éxito y produce un módulo. Carga cada módulo uno tras otro y observa la salida de dmesg. El mensaje en el momento de la carga debería identificar qué backends están presentes.

Añade un banner al evento de carga de módulo del núcleo:

```c
static int
portdrv_core_modevent(module_t mod, int type, void *arg)
{
	switch (type) {
	case MOD_LOAD:
		printf("portdrv: version %d loaded, backends:"
#ifdef PORTDRV_WITH_PCI
		       " pci"
#endif
#ifdef PORTDRV_WITH_SIM
		       " sim"
#endif
		       "\n", PORTDRV_VERSION);
		return (0);
	/* ... */
	}
	return (0);
}
```

Ahora `dmesg` en el momento de la carga te indica exactamente qué soporta este build.

**Punto de control.** Ahora puedes seleccionar, en tiempo de compilación, qué backends están presentes en el módulo. El código fuente no está contaminado con `#ifdef` dentro de los cuerpos de función; la selección ocurre a nivel del Makefile y el banner oculta su cadena de `#ifdef` en una ubicación controlada.

### Laboratorio 7: Acceso a registros seguro respecto al orden de bytes

Elige un registro del driver que represente un valor multibyte con un orden de bytes específico. La mayoría de los dispositivos reales lo documentan en la hoja de datos; para el driver de referencia, supón que el registro `REG_DATA` en el desplazamiento 0x08 es little-endian.

Modifica los accessors para aplicar la conversión de orden de bytes de forma explícita. **No** incorpores la conversión en el accessor de registro general; los registros de hardware en sí mismos están en orden del host a través de `bus_read_4`. La conversión se aplica a la **interpretación** del valor.

Añade un helper de nivel superior:

```c
static uint32_t
portdrv_read_data_le(struct portdrv_softc *sc)
{
	uint32_t raw = portdrv_read_reg(sc, REG_DATA);
	return (le32toh(raw));
}

static void
portdrv_write_data_le(struct portdrv_softc *sc, uint32_t val)
{
	portdrv_write_reg(sc, REG_DATA, htole32(val));
}
```

Úsalos en el núcleo siempre que el código esté manipulando un valor almacenado en formato little-endian en el registro.

**Punto de control.** En `amd64` los helpers de orden de bytes son no-ops, por lo que el comportamiento no cambia. En una plataforma big-endian, los helpers realizan intercambios de bytes que antes habrían faltado. Ahora estás preparado para la portabilidad arquitectónica aunque todavía no hayas probado en un host big-endian.

### Laboratorio 8: Script de matriz de compilación

Escribe un script breve que construya el driver en cada configuración admitida e informe del éxito o fracaso. Guárdalo como `validate-build.sh` en el directorio del capítulo.

```sh
#!/bin/sh
set -e

configs="
PORTDRV_WITH_SIM=yes
PORTDRV_WITH_PCI=yes
PORTDRV_WITH_PCI=yes PORTDRV_WITH_SIM=yes
"

echo "$configs" | while read cfg; do
	[ -z "$cfg" ] && continue
	printf "==> %s ... " "$cfg"
	make clean > /dev/null 2>&1
	if env $cfg make > build.log 2>&1; then
		echo "OK"
	else
		echo "FAIL"
		tail -n 20 build.log
		exit 1
	fi
done
```

Ejecútalo:

```sh
chmod +x validate-build.sh
./validate-build.sh
```

Si las tres configuraciones se construyen correctamente, tienes una matriz de compilación mínima establecida. Cualquier cambio futuro en el driver puede verificarse con la matriz mediante una única invocación.

**Punto de control.** El driver se construye en cada configuración que has anunciado. Tienes una forma legible por máquina de confirmar ese hecho. Esta es la última pieza estructural de un driver portable.

### Laboratorio 9: Ejercitar el backend de simulación desde el espacio de usuario

Una refactorización solo es tan buena como las pruebas que puedes ejecutar contra ella. Una vez que el backend de simulación está en su lugar, puedes ejercitar la lógica central del driver desde un pequeño programa en espacio de usuario sin necesitar ningún hardware. Este laboratorio recorre un arnés de pruebas ligero.

Crea un programa en espacio de usuario, `portdrv_test.c`, que abra `/dev/portdrv0` (o el dispositivo de caracteres que el driver cree para la instancia simulada), escriba un valor conocido, lo lea de vuelta e imprima el resultado:

```c
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char *argv[])
{
	const char *dev = (argc > 1) ? argv[1] : "/dev/portdrv0";
	int fd;
	char buf[64];

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		perror(dev);
		return (1);
	}

	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "hello from userland\n");
	if (write(fd, buf, strlen(buf)) < 0) {
		perror("write");
		close(fd);
		return (1);
	}

	memset(buf, 0, sizeof(buf));
	if (read(fd, buf, sizeof(buf) - 1) < 0) {
		perror("read");
		close(fd);
		return (1);
	}

	printf("Read back: %s", buf);
	close(fd);
	return (0);
}
```

Compílalo con `cc -o portdrv_test portdrv_test.c`. Carga el driver con el backend de simulación habilitado y ejecuta el programa de prueba:

```sh
sudo kldload ./portdrv.ko
./portdrv_test
```

Si el backend de simulación almacena y recupera los datos correctamente, la salida debería confirmar un ciclo de escritura-lectura satisfactorio. Si la prueba falla, el backend de simulación es el lugar aislado para depurar: sin hardware, sin sorpresas del ciclo de vida del driver, solo la lógica central más el backend simulado.

**Punto de control.** Tienes una prueba integral reproducible que ejercita la ruta del dispositivo de caracteres del driver, su gestión de I/O y su backend de simulación sin tocar ningún hardware real. Esta prueba puede ejecutarse en CI, en el portátil de un desarrollador o dentro de un huésped de `bhyve` sin tarjetas PCI.

### Laboratorio 10: Incorporación de un árbol sysctl básico

Introduce un árbol sysctl con raíz en `dev.portdrv.<unit>` que expone un puñado de valores visibles en tiempo de ejecución. El objetivo de este laboratorio no es conectar cada estadística al sysctl; es establecer la estructura para que las funcionalidades posteriores puedan extenderla fácilmente.

En `portdrv_core.c`, añade un par de helpers:

```c
static void
portdrv_sysctl_init(struct portdrv_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;

	ctx = device_get_sysctl_ctx(sc->sc_dev);
	tree = device_get_sysctl_tree(sc->sc_dev);

	SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "backend", CTLFLAG_RD,
	    __DECONST(char *, sc->sc_be->name), 0,
	    "Backend name");

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "version", CTLFLAG_RD,
	    NULL, PORTDRV_VERSION,
	    "Driver version");

	SYSCTL_ADD_UQUAD(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "transfers_ok", CTLFLAG_RD,
	    &sc->sc_stats.transfers_ok,
	    "Successful transfers");
}
```

Llama a `portdrv_sysctl_init(sc)` al final de `portdrv_core_attach`. Después de cargar el driver, inspecciona el árbol:

```sh
sysctl dev.portdrv.0
```

Deberías ver:

```text
dev.portdrv.0.%desc: portdrv (PCI backend)
dev.portdrv.0.%parent: pci0
dev.portdrv.0.backend: pci
dev.portdrv.0.version: 2
dev.portdrv.0.transfers_ok: 0
```

Ejercita el driver y observa cómo aumenta el contador `transfers_ok`. Si no aumenta, el núcleo no lo está actualizando; eso es un error que corregir.

**Punto de control.** El estado en tiempo de ejecución del driver ahora es observable desde el espacio de usuario sin necesidad de analizar dmesg. Los sistemas de monitorización y los informes de errores ad-hoc pueden incluir una instantánea del árbol sysctl, y conoces el estado del driver de un vistazo.

### Laboratorio 11: Selección de backend en tiempo de ejecución

Para un uso avanzado, permite activar o desactivar el backend de simulación del driver en tiempo de ejecución mediante un sysctl o un parámetro de módulo. Este laboratorio introduce la idea de hacer que la selección del backend sea flexible en el momento de carga del módulo, algo útil para pipelines de CI que necesitan que el mismo módulo se comporte de forma diferente según el entorno.

Añade un tunable al módulo:

```c
static int portdrv_sim_instances = 0;
SYSCTL_INT(_debug_portdrv, OID_AUTO, sim_instances,
    CTLFLAG_RDTUN, &portdrv_sim_instances, 0,
    "Number of simulation backend instances to create at load time");
TUNABLE_INT("debug.portdrv.sim_instances", &portdrv_sim_instances);
```

En el hook de carga del módulo, crea el número de instancias de simulación que solicite el tunable:

```c
for (i = 0; i < portdrv_sim_instances; i++)
	portdrv_sim_create_instance(i);
```

En el momento de la carga, el operador puede establecer el tunable desde `/boot/loader.conf`:

```text
debug.portdrv.sim_instances="2"
```

Carga el módulo y confirma que aparecen dos instancias simuladas. El backend PCI, si está compilado, se sigue enganchando a cualquier hardware real que encuentre. Los dos tipos de instancia coexisten en el mismo módulo, sin ningún conflicto entre ellos.

**Punto de control.** El driver es controlable en el momento de carga mediante un tunable, lo que significa que el mismo binario puede comportarse de forma diferente en distintos despliegues sin necesidad de recompilar. Este es el tipo de flexibilidad en tiempo de ejecución que a un driver de un solo archivo le resultaría incómodo gestionar, y que un driver multi-backend bien estructurado maneja como una extensión natural.

### Laboratorio 12: Escribir un arnés de CI mínimo

Combina el script de la matriz de build del Laboratorio 8 con una prueba de carga básica. El objetivo es un único comando que valide el driver en cada configuración soportada y confirme que se carga correctamente en todas ellas. Automatiza esto en un bucle similar a CI que puedas ejecutar antes de hacer commit de los cambios.

```sh
#!/bin/sh
# ci-check.sh - validate portdrv in every supported configuration.
set -e

configs="
PORTDRV_WITH_SIM=yes
PORTDRV_WITH_PCI=yes
PORTDRV_WITH_PCI=yes PORTDRV_WITH_SIM=yes
"

echo "$configs" | while read cfg; do
	[ -z "$cfg" ] && continue
	printf "==> Build: %s ... " "$cfg"
	make clean > /dev/null 2>&1
	if env $cfg make > build.log 2>&1; then
		echo "OK"
	else
		echo "FAIL"
		tail -n 20 build.log
		exit 1
	fi
	printf "==> Load : %s ... " "$cfg"
	if sudo kldload ./portdrv.ko > load.log 2>&1; then
		sudo kldunload portdrv > /dev/null 2>&1
		echo "OK"
	else
		echo "FAIL"
		cat load.log
		exit 1
	fi
done
echo "All configurations passed."
```

Ejecútalo:

```sh
chmod +x ci-check.sh
./ci-check.sh
```

**Punto de control.** Un único script confirma que cada configuración que documentas como soportada se construye y carga correctamente. Puedes invocarlo antes de cada commit, o integrarlo en un sistema CI que lo ejecute en cada pull request. El coste es de uno o dos minutos por ejecución; la recompensa es detectar regresiones a los pocos minutos de introducirlas.

## Ejercicios de desafío

Los siguientes desafíos llevan la refactorización más lejos y te dan espacio para practicar los patrones del capítulo en variaciones del mismo problema. Cada desafío es independiente y se basa únicamente en el material de este capítulo y los anteriores. Afróntalo a tu propio ritmo; están diseñados para consolidar el dominio adquirido, no para introducir nuevas bases.

### Desafío 1: Añadir un tercer backend

Añade un tercer backend al driver de referencia. Los detalles concretos dependen de tus intereses:

- Un backend MMIO que acceda al dispositivo a través de un nodo de Device Tree en una plataforma embebida.
- Un hijo de un bus I2C o SPI.
- Un backend que use el framework virtio de `bhyve` como invitado.

El requisito estructural es el mismo en cada caso: un nuevo archivo `portdrv_xxx.c`, una nueva implementación de la interfaz del backend, una nueva entrada en el Makefile y un nuevo flag de compilación. Una vez terminado el trabajo, confirma que el driver sigue construyéndose en todas las configuraciones soportadas anteriormente y además en la nueva.

### Desafío 2: Exponer metadatos de versión mediante sysctl

Crea un subárbol sysctl en `dev.portdrv.<unit>.version` que exponga:

- la versión del driver
- el nombre del backend
- la versión del backend
- los flags que se establecieron en tiempo de compilación

El árbol sysctl debe rellenarse en `portdrv_sysctl.c` (créalo si no existe) y leer sus valores de constantes definidas en los archivos individuales del backend. En tiempo de ejecución, el operador puede conocer todo lo que necesita saber sobre el build ejecutando `sysctl dev.portdrv.0`.

### Desafío 3: Simular un host big-endian

Probablemente no tengas una máquina big-endian. Simula la experiencia.

Escribe un modo de depuración puntual, controlado por un flag de compilación `PORTDRV_FAKE_BE`, en el que todos los helpers de endianness se reemplacen por macros que asumen un host big-endian. El código debe seguir compilando y ejecutándose en `amd64`, pero cualquier valor multibyte que debería haberse convertido ahora se convertirá de forma incorrecta (ese es precisamente el objetivo). Carga el driver con este flag activado y ejercita el backend de simulación. Si el driver funciona, enhorabuena: tu uso de los helpers de endianness es lo suficientemente consistente como para sobrevivir a un cambio de endianness. Si no funciona, los lugares donde falla son los lugares donde te has olvidado un `htole` o un `be32toh`.

Este es un ejercicio de laboratorio, no un patrón de producción. El conocimiento que adquieres, sin embargo, es exactamente el que te protegería en una plataforma big-endian real.

### Desafío 4: Cross-build para arm64

Instala o arranca un invitado FreeBSD `arm64` en QEMU. Construye el driver dentro del invitado, o haz el cross-build en tu host `amd64` usando:

```sh
make TARGET=arm64 TARGET_ARCH=aarch64
```

Copia el `.ko` resultante en el invitado y cárgalo. Ejecuta el backend de simulación y confirma que el driver se comporta de forma idéntica. Anota los avisos que emita el compilador de `arm64` que el de `amd64` no emitió; suelen ser desajustes de alineación o de tamaño que merecen una corrección.

### Desafío 5: Escribir un README.portability.md

Redacta un `README.portability.md` que documente:

- las plataformas en las que se ha probado el driver
- los backends que soporta
- las opciones de build que reconoce el Makefile
- cualquier limitación, error o problema conocido

Este desafío trata de escritura, no de código. El artefacto es tan importante para la portabilidad del driver como el código en sí. Sé breve; sé honesto.

### Desafío 6: Crear una interfaz de backend versionada

Añade un campo `version` a `struct portdrv_backend` y haz que el core rechace un backend cuya versión no coincida con la esperada por el core. Añade una segunda versión con un campo adicional pequeño (por ejemplo, `configure_dma`), y confirma que un backend compilado contra la versión antigua todavía carga contra un core que tolera ambas, mientras que un backend compilado contra la versión nueva se niega a cargar contra un core más antiguo.

Este desafío es más profundo de lo que parece. Versionar una interfaz requiere una reflexión cuidadosa sobre la compatibilidad. Vale la pena practicar los mecanismos una vez en un contexto aislado para saber cómo aplicarlos cuando tus propios drivers los necesiten.

### Desafío 7: Auditar un driver real de FreeBSD

Elige uno de los drivers referenciados en este capítulo, por ejemplo `/usr/src/sys/dev/uart/`, y audítalo comparándolo con los patrones vistos aquí. Responde a:

- ¿Cómo está separado el core de los backends?
- ¿Dónde están los accesores de registro, y usan `bus_read_*`?
- ¿Qué backends soporta el driver, y cómo se registra cada uno?
- ¿Existen cadenas de `#ifdef`, y están bien justificadas?
- ¿Cómo está estructurado el Makefile, y qué opciones expone?
- ¿Qué cambiarías para hacer el driver más portable, según tu criterio?

Escribe un breve informe en tu cuaderno de laboratorio. El ejercicio es para ti; el objetivo es entrenar tu ojo con código real.

### Desafío 8: Medir el coste del despacho de backend

La interfaz del backend introduce una indirección de puntero a función por cada acceso al registro. En un driver que realiza millones de accesos por segundo, esa indirección puede o no ser medible. Diseña un experimento para averiguarlo.

Usa el backend de simulación, para que la medición no se vea enturbiada por la latencia del hardware. Modifica el driver para que realice un bucle cerrado de llamadas a `portdrv_read_reg` en respuesta a un ioctl. Mide el tiempo del bucle con `clock_gettime(CLOCK_MONOTONIC)`. Compara dos builds: uno en el que el accesor despacha a través del backend, y otro en el que el accesor llama a `bus_read_4` directamente.

Anota tus conclusiones en tu cuaderno de laboratorio. En una CPU `amd64` moderna, es probable que encuentres que la diferencia está en el orden de un solo dígito porcentual o menos. En una CPU más antigua o más simple, puede ser mayor. Entender el coste de forma empírica, en lugar de estimarlo, es la forma correcta de fundamentar decisiones de diseño futuras.

### Desafío 9: Añadir un segundo objetivo de versión de FreeBSD

Toma el driver y haz que se construya tanto en FreeBSD 14.3 como en FreeBSD 14.2. Identifica una diferencia de API entre las dos versiones que use tu driver, envuélvela en guardas `__FreeBSD_version` y confirma que el módulo se construye correctamente contra las cabeceras de ambas versiones.

Documenta la diferencia y tu enfoque de envoltura en una nota breve al principio del archivo donde vive la guarda. Este ejercicio te introduce en el lado práctico de la compatibilidad entre versiones, algo que necesitarás si alguna vez mantienes un driver out-of-tree contra un rango de versiones de FreeBSD.

### Desafío 10: Diseñar un nuevo driver desde cero

Partiendo de los patrones de este capítulo, diseña un driver portable para un dispositivo hipotético de tu elección. El dispositivo debe tener al menos dos variantes de hardware (por ejemplo, PCI y USB), un modelo de registros simple y ser susceptible de simulación. Escribe:

- Un documento de diseño de una página que describa la estructura del driver.
- El archivo de cabecera `mydev_backend.h` que defina la interfaz del backend.
- Una implementación esqueleto del core que compile y enlace con funciones de marcador.
- El Makefile con selección condicional de backend.

No necesitas implementar el driver completo. El ejercicio consiste en practicar la fase de diseño, que es donde se toman la mayoría de las decisiones de portabilidad. Comparte tu diseño con un compañero o un mentor y escucha sus preguntas; las preguntas revelan las partes del diseño que todavía no eran evidentes para un lector nuevo.

## Resolución de problemas y errores comunes

Las refactorizaciones de portabilidad fallan de formas características. Esta sección cataloga los fallos que es más probable que encuentres, con síntomas concretos y soluciones concretas.

### Síntoma: el build falla con «Undefined reference» al dividir un archivo

Una función que era `static` en el archivo original ahora se llama desde un archivo diferente. El compilador no la ve.

**Solución.** Elimina el calificador `static` de la definición de la función y añade una declaración de ella en la cabecera adecuada. Si la función debe permanecer privada de un archivo, busca el llamador en el otro archivo y reestructura para que la llamada cruce una interfaz adecuada en lugar de un símbolo privado.

### Síntoma: el build falla con «Implicit function declaration» al dividir

Un archivo llama a una función cuyo prototipo no ha visto. Este es el error clásico de «moví la definición pero olvidé la declaración».

**Solución.** Añade `#include "portdrv.h"` o la cabecera adecuada al principio del archivo que llama. Si la cabecera no declara la función, añade la declaración. Trata el prototipo como parte de la superficie de la API; toda función que cruce archivos debe tener una declaración en una cabecera compartida.

### Síntoma: el build tiene éxito, pero la carga falla con un error de dependencia

El registro del módulo declara un `MODULE_DEPEND` sobre un módulo que no está cargado, o especifica una versión fuera del rango esperado.

**Solución.** Usa `kldstat` para ver qué está cargado. Carga primero el módulo requerido y luego el tuyo. Comprueba que el `MODULE_VERSION` de la dependencia coincide con el rango en `MODULE_DEPEND`. Si estás en medio de una refactorización y la cadena de dependencias es inestable, relaja temporalmente el rango de versiones o elimina la dependencia hasta que la refactorización se estabilice.

### Síntoma: el driver se carga, pero dmesg muestra solo un backend

Los flags del Makefile no se establecieron como esperabas. El banner informa solo de los backends cuyo `#ifdef` se cumplió.

**Solución.** Comprueba los flags en la línea de comandos de `make`. Examina la expansión real del Makefile:

```sh
make -n
```

La opción `-n` imprime lo que haría `make` sin ejecutarlo, y puedes ver si `-DPORTDRV_WITH_PCI` llegó realmente al comando de compilación. Si no es así, algo en el condicional del Makefile está mal.

### Síntoma: las lecturas de registro devuelven cero en ARM pero funcionan en x86

Estás accediendo al dispositivo mediante desreferenciación directa de puntero en lugar de `bus_read_*`.

**Solución.** Reemplaza cada desreferenciación directa de puntero de memoria de dispositivo por una llamada a `bus_read_*` o `bus_space_read_*`. En `amd64` la diferencia es invisible; en ARM es la diferencia entre funcionar y no funcionar.

### Síntoma: el backend de simulación funciona, pero el backend PCI se cuelga en attach

Los recursos PCI no se asignaron correctamente, o la estructura de datos privada del backend no se inicializó correctamente.

**Solución.** Añade una traza con `device_printf` en cada paso de la función de attach PCI para acotar dónde se produce el bloqueo. Comprueba que `bus_alloc_resource_any` haya devuelto un recurso no NULL. Comprueba que el callback `attach` del backend se invoque después de que `sc->sc_backend_priv` esté instalado; una desreferencia NULL en el callback es fácil de provocar y evidente en cuanto se detecta.

### Síntoma: el build falla en una versión diferente de FreeBSD

Una API cambió entre la versión en la que desarrollaste y la versión en la que intentaste compilar. El compilador se queja de una función que falta o de una firma que no coincide.

**Solución.** Envuelve el código afectado con `#if __FreeBSD_version >= N ... #else ... #endif`, donde N es la versión en la que la API adoptó su forma actual. Si el cambio se usa en muchos lugares, abstráelo en una función auxiliar dentro de un encabezado de compatibilidad para que el `#if` aparezca en un único lugar.

### Síntoma: el driver entra en pánico con "Locking Assertion Failed" tras dividir el backend

El core y el backend no coinciden en qué lock se mantiene en el momento de llamar al método del backend.

**Solución.** Documenta, en un comentario al principio de `portdrv_backend.h`, qué lock mantiene el core en cada llamada a método. Para cada operación del backend, el core mantiene el mutex del softc durante la llamada o no lo hace, pero de forma consistente. Audita todos los backends para verificar el cumplimiento. La causa habitual es un backend que adquiere un lock que el core ya tiene, o a la inversa.

### Síntoma: el driver compila, pero `make clean` deja archivos objeto obsoletos

`bsd.kmod.mk` sabe cómo limpiar los archivos propios del KMOD, pero puede pasar por alto los archivos adicionales que hayas añadido. La consecuencia son archivos `.o` obsoletos que se enlazan en el siguiente build y provocan un comportamiento extraño.

**Solución.** Elimina el directorio de build y vuelve a empezar:

```sh
rm -f *.o *.ko
make clean
```

A continuación, reconstruye. Añade los archivos obsoletos a una variable `CLEANFILES` en el Makefile si son generados durante el build.

### Síntoma: el build para otra arquitectura falla con "Cannot Find `<machine/bus.h>`"

El entorno de compilación cruzada está incompleto o la arquitectura de destino no está instalada.

**Solución.** Sigue el handbook de FreeBSD sobre compilación cruzada. Por lo general, necesitas instalar el árbol de código fuente para la arquitectura de destino y ejecutar `make TARGET=arm64 TARGET_ARCH=aarch64` desde la raíz del árbol de código fuente. Construir un único módulo fuera del árbol requiere el soporte de compilación cruzada del sistema base; la documentación correspondiente se encuentra al inicio de `/usr/src/Makefile` y en el capítulo "Cross-Build" del handbook.

### Síntoma: el `sysctl` que acabas de añadir no aparece

El sysctl se registró en un archivo que no se compila en el módulo, o el código de registro no llega a ejecutarse.

**Solución.** Confirma que `portdrv_sysctl.c` está en `SRCS`. Confirma que el registro del sysctl se llama desde la ruta de carga del módulo o desde attach. Añade un `device_printf` justo antes de la llamada al registro para confirmar que se ejecuta.

### Síntoma: el driver funciona en hardware real pero no en `bhyve`

El hardware simulado en `bhyve` presenta un comportamiento ligeramente diferente al del dispositivo real. Con frecuencia se trata de una diferencia entre MSI e INTx, o de una lista de capacidades PCI ligeramente distinta.

**Solución.** Comprueba las capacidades de las que depende tu driver. Usa `pciconf -lvc` tanto en el host como en el invitado para ver qué difiere. Si una capacidad que tu driver necesita no está disponible en el invitado, el driver debería detectarlo y degradarse de forma elegante, en lugar de asumir que la capacidad siempre está presente.

### Síntoma: el rendimiento varía enormemente entre arquitecturas

Estás accediendo a datos no alineados mediante un cast directo. En `amd64`, el acceso no alineado es barato; en `arm64`, puede ser varias veces más lento, si es que el hardware lo admite.

**Solución.** Sustituye los casts directos por `memcpy` seguido de una función auxiliar de endianness, tal como se muestra en la sección 5. El código resultante es rápido en todas las arquitecturas y correcto en todas ellas.

### Síntoma: `kldunload` se queda bloqueado

Algo sigue usando el módulo. Por lo general, hay un callout todavía programado, una interrupción todavía en vuelo, o un contador de referencias positivo.

**Solución.** Añade una llamada a `callout_drain` en tu ruta de detach. Añade trazas con `device_printf` en cada paso de detach para ver cuál está bloqueado. Busca descriptores de archivo abiertos sobre los dispositivos de caracteres que crea el driver; `kldunload` no terminará mientras un dispositivo esté abierto. Usa `fstat` para identificar qué proceso mantiene el dispositivo.

### Síntoma: comportamiento diferente entre `make PORTDRV_WITH_SIM=yes` y `make PORTDRV_WITH_SIM=yes PORTDRV_DEBUG=yes`

El código de depuración está cambiando un comportamiento que no debería cambiar. Con frecuencia se debe a que un printf de depuración llama a una función que tiene efectos secundarios, o a que la ruta de depuración mantiene un lock más tiempo que la ruta de producción.

**Solución.** Revisa cada llamada a `PD_DBG`. Si alguna de ellas invoca una función en lugar de limitarse a formatear un valor, sustituye la invocación por una cadena precalculada. La impresión de depuración debe ser una operación puramente observacional sin efectos secundarios; si la sentencia de log tiene efectos secundarios, el comportamiento de producción y el de depuración divergirán de maneras difíciles de rastrear.

### Síntoma: advertencia sobre "Incompatible Pointer Types" tras mover una función

Una función que recibía un puntero en el archivo original ahora recibe un puntero ligeramente diferente en el nuevo archivo. Por lo general, el tipo ha derivado entre versiones de un encabezado, o un typedef ha cambiado de forma sutil.

**Solución.** Abre ambos archivos y compara las declaraciones. Si los tipos son realmente los mismos, hace falta la declaración en un encabezado compartido para que ambos archivos vean la misma definición. Si los tipos han derivado, elige el correcto y actualiza el llamador o el llamado.

### Síntoma: `bus_dma` falla con `ENOMEM` en `amd64` pero funciona en `arm64`

De forma contraintuitiva, esto suele ser un problema de configuración en `bus_dma_tag_create`. El campo `lowaddr` se estableció con un valor que no coincide con el mapa de memoria real del sistema.

**Solución.** Comprueba los parámetros `lowaddr` y `highaddr`. Para un dispositivo de 64 bits sin restricciones, usa `BUS_SPACE_MAXADDR`. Para un dispositivo de 32 bits, usa `BUS_SPACE_MAXADDR_32BIT`. Las direcciones incorrectas producen asignaciones espurias de bounce buffer que pueden fallar en sistemas con mucha memoria.

### Síntoma: el programa en espacio de usuario ve datos distintos a los que muestra el log del kernel

La llamada a `uiomove(9)` está transfiriendo bytes, pero el endianness de los valores difiere entre la interpretación del kernel y la del espacio de usuario.

**Solución.** Decide qué orden de bytes usa la interfaz de tu dispositivo de caracteres. Documéntalo en un comentario sobre la estructura `cdevsw`. Usa `htole32`/`le32toh` u otras funciones auxiliares similares cuando los valores crucen la frontera entre el kernel y el espacio de usuario, igual que harías para las fronteras con el dispositivo.

### Síntoma: el módulo carga, pero `kldstat` lo muestra en una posición distinta a la esperada

Las dependencias del módulo se están cargando en un orden inesperado. Normalmente esto es consecuencia de que falta una declaración `MODULE_DEPEND`.

**Solución.** Declara cada módulo del que depende el tuyo con `MODULE_DEPEND`. El kernel resuelve el orden de carga automáticamente; sin las declaraciones, el orden no está especificado.

### Síntoma: un build de depuración consume mucha más memoria que un build de producción

Las sentencias de depuración están asignando memoria en cada invocación, o una estructura de datos exclusiva de depuración crece sin límite.

**Solución.** Revisa cada llamada a `PD_DBG` para asegurarte de que ninguna realiza asignaciones de memoria. Si una estructura de datos exclusiva de depuración debe crecer, limita su tamaño explícitamente. Un build de depuración que provoca condiciones de falta de memoria es más engañoso que útil.

### Síntoma: las pruebas pasan en la máquina de desarrollo pero fallan en CI

La máquina de CI tiene una versión de FreeBSD diferente, un conjunto diferente de módulos cargados o una configuración de hardware diferente. La diferencia está exponiendo un bug latente.

**Solución.** Investiga la diferencia concreta. Si el entorno de CI es correcto y la máquina de desarrollo está enmascarando un bug, acéptalo y corrígelo. Si el entorno de CI está mal configurado, corrígelo. En cualquier caso, la divergencia entre el entorno de desarrollo y el de CI es una señal que merece tomarse en serio.

### Síntoma: un sysctl puede leerse pero no escribirse, aunque se estableció `CTLFLAG_RW`

El registro del sysctl usa un puntero a una constante de solo lectura, o el manejador ignora las escrituras, o el nodo padre es de solo lectura.

**Solución.** Confirma que la variable subyacente es modificable. Confirma que `CTLFLAG_RW` está realmente en el nodo hoja, no en el padre. Si hay instalado un manejador personalizado, asegúrate de que gestiona la dirección de escritura y no solo la de lectura.

### Síntoma: el driver entra en pánico al ejecutar `kldunload` tras funcionar durante mucho tiempo

Un callout, taskqueue o workqueue de larga duración se ha liberado mientras aún estaba registrado, o una referencia a un recurso no se libera en el orden correcto.

**Solución.** Llama a `callout_drain` y a las funciones de drenado equivalentes durante detach. Asegúrate de que cada recurso asignado durante attach se libera durante detach, en orden inverso. Recorre las rutas de attach y detach sobre papel, rastreando cada asignación; esto detecta la mayoría de los pánicos al apagarse.

### Síntoma: el módulo compilado de forma cruzada se niega a cargar en el destino

El ABI del módulo compilado de forma cruzada no coincide con el ABI del kernel de destino. Esto es casi siempre una discrepancia de versión entre el árbol de build y el destino.

**Solución.** Asegúrate de que `TARGET` y `TARGET_ARCH` en la compilación cruzada coincidan exactamente con el destino. Si el destino ejecuta FreeBSD 14.3-RELEASE, compila contra el árbol de código fuente de 14.3, no contra el árbol 15-CURRENT. La compatibilidad de ABI de módulos entre versiones mayores no está garantizada.

### Síntoma: el método `attach` de un backend se llama dos veces sobre el mismo softc

O bien dos threads compiten por ejecutar attach, o bien el backend se está volviendo a conectar sin un detach intermedio.

**Solución.** Añade un indicador de estado en el softc que registre si el backend ya ha sido conectado. El core debería negarse a reconectar un softc que ya está conectado. Por simetría, detach debería limpiar ese indicador. Esto captura la mayoría de los bugs de attach duplicado a nivel lógico, en lugar de esperar a un pánico por agotamiento de recursos.

### Síntoma: el compilador advierte sobre una variable no utilizada en un bloque condicional

La variable solo se usa cuando un `#ifdef` concreto está activo, y el build que se está compilando tiene ese indicador desactivado.

**Solución.** Mueve la declaración de la variable al interior del bloque `#ifdef` si solo se usa allí. Si la variable se usa en ambas ramas pero el compilador no puede ver una de ellas, añade `(void)var;` en la rama no utilizada para indicarle al compilador que la variable se deja sin usar intencionalmente.

### Síntoma: el build tiene éxito, pero `kldxref` se queja de metadatos que faltan

La declaración `MODULE_PNP_INFO` está mal formada, o el array al que apunta tiene una forma incorrecta. `kldxref(8)` construye un índice que mapea identificadores de hardware a módulos, y un `MODULE_PNP_INFO` malformado rompe ese índice.

**Solución.** Comprueba los argumentos de `MODULE_PNP_INFO` contra la documentación. La cadena de formato debe coincidir con la disposición del array de identificación, y el recuento debe ser correcto. Una discrepancia se detecta en el arranque cuando se ejecuta `kldxref`, pero es más fácil detectarla durante el build ejecutando `kldxref -v` sobre el directorio e inspeccionando la salida.

### Síntoma: un backend que funcionaba ayer falla hoy tras una actualización del kernel

Una API del kernel de la que depende el backend ha cambiado en la nueva versión. El core está bien; el backend está roto.

**Solución.** Consulta `UPDATING` en el árbol de código fuente del kernel para ver si hay notas sobre la API que cambió. Consulta las notas de `SHARED_LIBS` y de ABI de módulos de la versión. Envuelve la llamada a la API afectada en un guard de `__FreeBSD_version` para que el driver soporte tanto la versión antigua como la nueva. Si el responsable del backend no eres tú, notifícale el problema.

### Síntoma: el driver rechaza un dispositivo que debería funcionar

La tabla de coincidencias de la función probe está incompleta, o el ID del dispositivo se está leyendo en el orden de bytes incorrecto.

**Solución.** Imprime los IDs de fabricante y de dispositivo que está viendo la función probe. Compáralos con la hoja de datos del dispositivo. Si los valores parecen estar con los bytes invertidos, probe está usando el acceso o la máscara incorrectos. Si los valores parecen correctos pero la tabla no los incluye, añade los nuevos IDs.

### Síntoma: un backend USB funciona con un dispositivo pero no con otro del mismo tipo

Los dispositivos USB tienen descriptores que pueden variar ligeramente entre revisiones de firmware del mismo producto. El backend está haciendo una coincidencia demasiado estricta sobre un campo opcional.

**Solución.** Revisa la lógica de coincidencia USB. Haz la coincidencia sobre el ID de fabricante y el ID de producto; ignora los números de versión y los números de serie a menos que sean genuinamente significativos. El objetivo es identificar toda la familia de productos, no una compilación concreta de ella.

### Síntoma: tras dividir un archivo, algunas funciones `static` ya no pueden expandirse en línea

Las funciones que eran `static inline` en un archivo ahora se invocan entre archivos, lo que impide la expansión en línea y puede causar una pequeña regresión de rendimiento.

**Solución.** Si la función es pequeña y crítica para el rendimiento, mantenla en un archivo de cabecera como `static inline` para que cada archivo que lo incluya obtenga su propia copia. Si la función es más grande, muévela al archivo `.c` correspondiente como una función normal. El compilador aún puede expandirla en línea entre unidades de traducción si la optimización en tiempo de enlace está activa, pero en el código del kernel ese caso es poco frecuente.

### Síntoma: `make` recompila todo aunque solo haya cambiado un archivo

El Makefile no realiza un seguimiento correcto de las dependencias de los archivos de cabecera. Los cambios en un archivo de cabecera compartido provocan que todos los archivos que lo incluyen se recompilen, lo cual es correcto. Pero a veces el Makefile es demasiado agresivo y recompila archivos que no tienen relación.

**Solución.** Comprueba que `bsd.kmod.mk` esté generando archivos de dependencias (normalmente tienen la extensión `.depend`). Si no existen, añade `.include <bsd.dep.mk>` o asegúrate de que la cadena de inclusión por defecto se ejecute. Para un driver pequeño, las recompilaciones completas son generalmente aceptables; para uno grande, el seguimiento de dependencias merece la pena.

### Orientación general

Si estás atascado en un problema de portabilidad que no encaja con ninguno de los casos anteriores, tres hábitos suelen ayudar.

**Reduce la configuración.** Si el driver falla solo con ciertos backends activos, compílalo con un único backend a la vez y busca la configuración en la que falla por primera vez.

**Reduce la arquitectura.** Si el driver falla en `arm64` pero no en `amd64`, el error es casi con toda seguridad de endianness, alineación o discrepancia en el tamaño de un tipo. Esas son las únicas tres causas habituales de comportamiento específico de arquitectura en el código de un driver.

**Reduce la función.** Añade printfs, añade `KASSERT`s, elimina código hasta que el driver más pequeño que quede siga fallando. Los errores del kernel son difíciles de razonar en abstracto, pero se vuelven manejables en cuanto puedes reproducirlos desde un ejemplo mínimo.

**Haz bisección del cambio.** Si el driver funcionaba en un commit anterior y ahora falla, ejecuta `git bisect`. Los errores del kernel suelen ser regresiones concretas, y la bisección encuentra el primer commit problemático en O(log n) pasos.

**Lee las advertencias del compilador, incluso las molestas.** Un aviso de "variable asignada pero no utilizada" puede apuntar a un campo que fue renombrado y quedó olvidado. Una advertencia de "declaración implícita de función" es un include que falta. Un aviso de "comparación entre tipos con y sin signo" puede ser el origen exacto del desbordamiento que estás persiguiendo. Las advertencias del compilador son consejos gratuitos; úsalos.

## Una retrospectiva: los patrones de un vistazo

Antes de la nota de cierre, aquí tienes una retrospectiva compacta de los patrones que este capítulo ha presentado. Úsala como referencia rápida cuando vuelvas al capítulo o cuando estés revisando tu propio código. Cada entrada es la idea condensada en una frase, con un puntero de vuelta a la sección que la introduce en detalle.

**Accessors en lugar de primitivas.** Envuelve cada acceso a un registro en una función local del driver en lugar de llamar directamente a `bus_read_*` en el código que contiene la lógica del dispositivo. El wrapper es un único punto de cambio, nombra la operación y te permite sustituir la implementación sin tocar los llamadores. Véase la Sección 2.

**Interfaz de backend.** Describe la parte variable del driver como una estructura de punteros a función. Cada bus, variante de hardware o simulación se convierte en una instancia de la estructura. El núcleo invoca a través de la estructura sin necesidad de conocer qué backend está presente. Véase la Sección 3.

**División de archivos por responsabilidad.** Coloca la lógica central en un archivo, cada backend en su propio archivo, y cada preocupación significativa de un subsistema (sysctl, ioctl, helpers de DMA) en su propio archivo. El lector debería ser capaz de adivinar qué archivo abrir a partir de lo que está buscando. Véase la Sección 4.

**Tipos de ancho fijo.** Usa `uint8_t` a `uint64_t` para valores cuyo tamaño importa. Evita `int`, `long`, `unsigned` y `size_t` cuando el ancho está fijado por el dispositivo o el protocolo. Véase la Sección 5.

**Helpers de endianness.** Todo valor multibyte que cruza el límite del hardware pasa por `htole32`, `le32toh` o sus equivalentes. Sin excepciones. Véase la Sección 5.

**`bus_space(9)` y `bus_dma(9)`.** Usa las APIs independientes de la arquitectura para el acceso a registros y para DMA. Nunca hagas un cast de un puntero sin procesar a memoria de dispositivo, y nunca calcules direcciones físicas directamente. Véase las Secciones 2 y 5.

**Barreras de memoria.** Usa `bus_barrier` para imponer el orden entre accesos visibles por el dispositivo cuando el orden importa. Usa `bus_dmamap_sync` para imponer la coherencia entre las cachés de la CPU y los buffers de DMA. Véase la Sección 5.

**Compilación condicional con moderación.** Usa flags en tiempo de compilación para características que deban incluirse o excluirse. Mantén los bloques `#ifdef` en un nivel de granularidad grueso. Oculta los guardas dentro de macros cuando se usen en muchos lugares. Véase la Sección 6.

**`__FreeBSD_version`.** Úsalo con mesura para proteger código que depende de una versión concreta del kernel. Centraliza los guardas en un archivo de cabecera de compatibilidad si aparecen más de un par de veces. Véase la Sección 6.

**Wrappers para portabilidad entre BSDs.** Aísla las APIs específicas del sistema operativo detrás de macros wrapper cuando se necesite compatibilidad con otros BSDs. Mantén el núcleo libre de código específico del sistema operativo. Véase la Sección 7.

**Versionado de módulos.** Usa `MODULE_VERSION` y `MODULE_DEPEND` para hacer visibles al kernel los problemas de actualización y orden de carga. Incrementa las versiones cuando cambia el comportamiento observable. Véase la Sección 8.

**Matriz de compilación.** Mantén una lista de configuraciones de build compatibles. Ejecuta un script que compile cada configuración antes de una entrega. Véase la Sección 8.

**README de portabilidad.** Documenta las plataformas compatibles, los backends, las opciones de compilación y las limitaciones en un readme breve. Actualízalo cada vez que cambie alguno de esos aspectos. Véase la Sección 8.

**Telemetría.** Expón los contadores clave a través de un árbol sysctl. Los informes de errores que incluyen la salida de sysctl son más fáciles de clasificar que los que no la incluyen. Véase la Sección 8.

Imprime esta lista o guarda una copia en tus notas. Los patrones funcionan porque se refuerzan mutuamente, y verlos juntos es la forma más rápida de interiorizar la estructura de un driver portable.

## Antipatrones de portabilidad: qué desaprender

Habiendo visto los patrones que funcionan, conviene nombrar los que no. La mayoría de los desastres de portabilidad en drivers no son fallos espectaculares de criterio, sino hábitos pequeños y cómodos que se acumulan. Un autor de drivers que copia una función que funciona sin entenderla tendrá la tentación, seis meses después, de copiarla de nuevo con una pequeña modificación. Un driver que tiene un `#ifdef` para una arquitectura tendrá, seis meses después, cinco. Cada paso parece inofensivo; juntos producen un código que nadie quiere tocar.

Este breve catálogo nombra los antipatrones para que puedas reconocerlos en tu propio código y en el código que revisas. El objetivo no es avergonzar a nadie, sino darte el vocabulario para identificar la deriva pronto y corregirla.

### El driver "en mi máquina funciona"

Un driver que solo tiene un objetivo, una versión de FreeBSD y un bus oculta los errores de portabilidad en lugar de carecer de ellos. El código puede compilarse y ejecutarse sin problemas durante años, pero en el momento en que alguien intente compilarlo en una arquitectura diferente o en una versión más nueva, todos los atajos quedan expuestos de golpe. El antídoto no es probar en todas las plataformas desde el primer día, sino escribir como si algún día fuera a hacerlo, de modo que el eventual port sea un ejercicio de una semana en lugar de tres meses. Los tipos de ancho fijo, los helpers de endianness y `bus_read_*` son un seguro barato contra el futuro que aún no puedes ver.

### El `#ifdef` silencioso

El bloque `#ifdef` sin ningún comentario que explique por qué está ahí es casi siempre un error. Si el bloque protege código genuinamente específico de una arquitectura, dilo, y explica qué hace la rama alternativa. Si protege un workaround para un error en un chip concreto, di qué chip, qué error y cuándo podría eliminarse. Un archivo de driver con `#ifdef`s sin documentar es un archivo donde el tú del futuro no sabrá si la rama sigue siendo necesaria. Documenta o elimina.

### El cast a la ligera

Un cast de un puntero a un tipo entero, o de un entero de un tamaño a otro, es casi siempre un error en potencia. En `amd64`, un puntero tiene 64 bits y un `long` tiene 64 bits, así que el cast funciona casualmente. En un sistema `i386`, un puntero tiene 32 bits y `long` tiene 32 bits, así que el cast también funciona casualmente. En ARM de 32 bits, un puntero tiene 32 bits pero `uint64_t` tiene, por supuesto, 64 bits; aquí el cast cambia de tamaño y genera una advertencia solo en un subconjunto de builds. Los casts que el compilador acepta silenciosamente en una plataforma y advierte en otra son precisamente los casts que debes evitar. Si la lógica requiere una conversión, usa `uintptr_t`, que está diseñado explícitamente para contener un puntero, y documenta por qué es necesario el cast.

### El registro optimista

Una lectura o escritura de registro que el driver asume que tendrá éxito es un registro optimista. En hardware real, una lectura de una dirección no mapeada puede devolver `0xFFFFFFFF`, puede provocar un machine-check en la CPU o puede producir silenciosamente basura, dependiendo del bus y la plataforma. En un backend de simulación, la misma lectura devuelve una constante conocida. Los drivers que nunca comprueban la lectura patológica son drivers que funcionan hasta que un día el hardware se apaga o queda detrás de un bus PCI que se desconectó por un evento de hotplug. El código defensivo a nivel de accessor (¿tiene sentido el valor devuelto?) es más barato que depurar el pánico que sigue.

### La comprobación de versión ad hoc

`if (major_version >= 14)` disperso por todo el driver es un antipatrón. Las comprobaciones de versión pertenecen a un archivo de cabecera de compatibilidad, no repartidas por el código. Un único `#define PORTDRV_HAS_NEW_DMA_API 1` en un solo lugar, protegido por una única comprobación de `__FreeBSD_version`, mantiene el resto del driver limpio. Las comprobaciones de versión dispersas también son frágiles: cuando la API cambie de nuevo, olvidarás una de las decenas de comprobaciones dispersas y el driver fallará de forma sutil en una versión.

### El hack "temporal"

Un comentario que dice `// TODO: remove before release` o `// FIXME: arm64` es una promesa que te haces a ti mismo. El noventa por ciento de esas promesas nunca se cumplen. Antes de escribir un hack temporal, pregúntate si existe una manera de resolver el problema correctamente en media hora. A menudo la hay. Cuando de verdad no la hay, el hack debería ir acompañado de un ticket de seguimiento y un recordatorio en el calendario. Sin esas dos cosas, "temporal" se convierte en "permanente" en el plazo de un ciclo de versiones.

### La constante duplicada

Un número mágico escrito una vez es una decisión de diseño. Un número mágico escrito dos veces es un error esperando a que una de las copias se actualice sin la otra. Las constantes para direcciones de registros, máscaras de bits, timeouts y límites deben vivir en un único archivo de cabecera e incluirse en todos los lugares donde se usen. Cuando te encuentres escribiendo el mismo número en dos sitios distintos, detente y ponlo en un `#define`.

### El `struct softc` que no para de crecer

Un softc que acumula campos en múltiples commits sin ninguna reorganización es un olor a código problemático. Es una señal de que cada característica añadida al driver puso su estado directamente en el softc sin preguntarse si ese estado pertenecía a otro lugar. Cuando un softc tiene más de veinte campos, considera agrupar los campos relacionados en estructuras anidadas: `sc->dma.tag`, `sc->dma.map`, `sc->dma.seg` en lugar de `sc->dma_tag`, `sc->dma_map`, `sc->dma_seg`. La refactorización es pequeña, y la estructura resultante se lee mejor en los logs y en la salida del depurador.

### La cobertura de pruebas desigual

Un driver cuyas pruebas solo cubren el camino feliz es un driver cuyos caminos de error morderán a alguien en producción. Toda función que pueda fallar debería ser comprobable, y debería comprobarse, al menos en el modo de fallo más obvio. El backend de simulación hace esto barato; úsalo para inyectar fallos (sin memoria, timeout de DMA, dispositivo sin respuesta) y verifica que el núcleo los gestiona con elegancia. Un driver que nunca ha sido probado contra un backend que falla es un driver cuyos caminos de error son pensamiento ilusorio.

### La abstracción gratuita

También se puede caer en el error contrario. Un driver con una interfaz de backend de doce métodos, tres capas de envoltura y una factoría abstracta para instancias de backend probablemente está sobrediseñado para la necesidad concreta que pretende resolver. Las abstracciones tienen un coste: ocultan lo que hace el código, dispersan el conocimiento entre archivos y dificultan la depuración. El nivel de abstracción adecuado es el que te permite resolver el problema de hoy con limpieza, sin que el problema de mañana resulte significativamente más fácil por ello. Ante la duda, empieza con menos abstracción y añádela cuando un segundo caso de uso concreto justifique el esfuerzo.

### El commit monolítico

Un commit que introduce accesores, divide archivos, añade una interfaz de backend y crea una matriz de Makefile es imposible de revisar, imposible de bisecar e imposible de revertir de forma limpia. Divide el trabajo: un commit por paso, cada uno dejando el driver en un estado compilable y cargable. Los commits pequeños también comunican la intención de diseño: el mensaje de commit «introduce accessors» le dice a un lector exactamente qué pretende conseguir ese commit, mientras que un commit general titulado «refactor for portability» no les dice nada útil.

Reconocer estos antipatrones en tu propio código es una señal de madurez. Los patrones del cuerpo principal del capítulo son lo que debes hacer; los antipatrones que se describen aquí son los que debes vigilar. Cuando encuentres uno en un driver que escribiste el año pasado, no te reproches nada; simplemente corrígelo. Todo el mundo escribe antipatrones en algún momento. Lo que distingue a un autor experimentado es la rapidez con la que los detecta y los corrige.

## La portabilidad en el mundo real: tres historias breves

Los principios calan mejor cuando van ligados a consecuencias. A continuación se presentan tres historias breves basadas en patrones que se repiten en el desarrollo real de FreeBSD. Ninguna de las anécdotas que siguen corresponde a un incidente concreto; cada una es una composición de problemas que los autores de drivers experimentados han visto más de una vez. Los detalles son ilustrativos, no literales. Las lecciones son exactamente las que se enuncian.

### Historia 1: El bug de endianness que durmió cinco años

Un driver para un controlador de almacenamiento fue escrito en 2019 en una estación de trabajo `amd64`, desplegado en unos pocos servidores de producción y ejecutándose sin problemas durante cinco años. A finales de 2024, un equipo que migraba a servidores `arm64` para mejorar la eficiencia energética cargó el mismo driver en el nuevo hardware. Las lecturas funcionaban. Las escrituras tenían éxito. Una semana después aparecieron corrupciones de datos esporádicas, limitadas a una pequeña fracción de las peticiones de I/O.

El bug era una sola línea en la configuración de descriptores del driver: una asignación directa de struct que depositaba un valor little-endian nativo de `amd64` en un descriptor de anillo que el hardware interpretaba en su propio orden de bytes. En `amd64`, el orden de bytes del hardware coincidía casualmente con el del host, por lo que la asignación directa funcionaba por casualidad. En `arm64`, el kernel también funciona en little-endian, así que el código no falló de inmediato, pero el hardware en cuestión tenía una revisión de firmware más reciente que cambió el orden de bytes de los descriptores para ciertos tipos de comandos. El orden de bytes del que el driver dependía por casualidad desapareció, y solo una carga de trabajo específica (escrituras en streaming que cruzaban un límite de descriptor) provocaba la corrupción.

La corrección fue un cambio de una sola línea para usar `htole32` en cada campo de descriptor. El diagnóstico llevó tres semanas e implicó a dos equipos, porque nadie había escrito el driver original esperando que se encontrara con una segunda plataforma o una revisión de hardware. La lección es que los bugs de endianness no se anuncian en el momento en que se introducen; pueden dormir durante años hasta que un cambio en otro lugar los despierta. Escribir `htole32` desde el principio es más barato que pagar el precio de ese sueño.

### Historia 2: El bloque condicional que nadie recordaba

Un driver de red que soportaba tanto variantes conectadas por PCIe como variantes embebidas conectadas por FDT llevaba un bloque `#ifdef PCI` de dos líneas en su ruta de attach. El bloque activaba una función de gestión de errores que solo importaba en la variante PCIe, porque la variante embebida usaba un subsistema distinto para el mismo propósito. El `#ifdef` no tenía comentario.

Tres años después, el autor del driver pasó a otros proyectos. El driver adquirió un nuevo mantenedor. Una actualización del kernel cambió la semántica de la función controlada por el `#ifdef`, y el driver empezó a generar errores espurios en la variante embebida. Nadie sabía si el `#ifdef` seguía siendo necesario, si alguna vez lo había sido, o si estaba ocultando un bug en la ruta embebida o en la ruta PCIe. Una semana de investigación reveló que sí, el `#ifdef` seguía siendo correcto; documentaba una asimetría real entre las dos rutas; pero como no tenía comentario, cada nuevo lector tenía que reconstruir ese conocimiento desde cero.

La corrección consistió en añadir un comentario de tres líneas sobre el `#ifdef`, citando la diferencia de subsistema y nombrando el commit del kernel que introdujo la necesidad del bloque. Coste total de la corrección: diez minutos. Coste total de la investigación a lo largo de tres años: unas cuarenta horas de ingeniero.

La lección es que los bloques de compilación condicional se convierten en un misterio muy rápidamente. Cada `#ifdef` debería tener un comentario que explique a un lector futuro por qué está ahí. El comentario se amortiza la primera vez que cualquier otra persona toca el archivo.

### Historia 3: El backend de simulación que salvó una versión

Un driver para un sensor personalizado recibió un backend de simulación al final de su primer ciclo de desarrollo. La adición requirió dos días de trabajo y se consideró un detalle agradable más que una necesidad. El hardware real era escaso, caro y estaba en un laboratorio al otro lado del edificio respecto al desarrollador principal.

Al final del ciclo de versión, apareció una regresión: bajo una secuencia específica de operaciones de usuario, el driver entraba en deadlock al descargarse. Reproducir el bug en el hardware real requería reservar el laboratorio, que estaba en uso para la validación de otro producto. El desarrollador usó en su lugar el backend de simulación, reprodujo el deadlock en una hora, identificó el `mtx_unlock` que faltaba y lo causaba, y entregó la corrección ese mismo día.

La lección es que los backends de simulación no son solo para pruebas; sirven para reproducir bugs sin tener que competir por hardware escaso. Los dos días invertidos en el backend de simulación ahorraron quizás una semana de presión en el calendario y, posiblemente, salvaron la versión. El patrón merece la pena.

Estas tres historias ilustran lo que el capítulo ha estado argumentando de forma abstracta. Los bugs de endianness duermen. Los bloques condicionales envejecen. Los backends de simulación se amortizan rápidamente. Los incidentes específicos son ilustrativos, pero los patrones son reales y se repiten en distintos drivers y a lo largo de los años. Conocerlos de antemano marca la diferencia entre escribir un driver que mantendrás y escribir un driver que te avergonzará.

## Una nota final breve sobre la disciplina

Este capítulo te ha pedido que tengas paciencia con decisiones pequeñas y estructurales. Los patrones de este capítulo, los accesores, las interfaces de backend, la división de archivos, los helpers de endian, las etiquetas de versión, no resultan emocionantes la primera vez que los encuentras. Son el tipo de cosas que un nuevo autor de drivers aprende porque alguien con más experiencia se lo dijo. El yo más joven los lee, asiente y los apunta como una lista de verificación.

Leerlos como una lista de verificación es perder de vista lo esencial. Los patrones funcionan porque moldean los hábitos con los que escribes código nuevo. El primer driver que escribes con accesores ya implementados es también el primer driver en el que nunca olvidas comprobar si un recurso es NULL, porque el accesor es el lugar natural para hacer esa comprobación y lo escribiste una vez. El primer driver que estructuras en núcleo y backends es también el primer driver en el que añadir una funcionalidad significa tocar un solo archivo. El primer driver que versionas con `MODULE_VERSION` y `MODULE_DEPEND` es también el primer driver cuya ruta de actualización resulta obvia para tus usuarios. Los beneficios no están en los patrones; están en los hábitos que los patrones crean.

El hábito requiere tiempo. Escribirás el siguiente driver más rápido que este, y el que venga después aún más rápido, no porque los patrones tengan atajos, sino porque tus dedos dejan de buscar el camino largo. El coste de la portabilidad, medido por driver, disminuye bruscamente después del primero. Por eso este capítulo importa para el resto de tu carrera, y no solo para este libro.

Los grandes autores de drivers de FreeBSD no se hicieron grandes memorizando los patrones de este capítulo. Se hicieron grandes aplicándolos en cada ocasión, incluso en los drivers pequeños y aburridos que nadie iba a leer. Esa práctica escala. Adóptala cuanto antes.

### Cómo se ve el éxito

Una señal en el camino, ya que has llegado hasta aquí. Cuando los patrones de este capítulo empiezan a sentirse automáticos, algo cambia en la forma en que escribes drivers. Dejas de escribir `bus_read_4(res, 0x08)` y buscas `portdrv_read_reg(sc, REG_STATUS)` de forma instintiva. Dejas de preguntarte en qué archivo va una nueva función; simplemente lo sabes. Dejas de pensar en si usar `htole32` o no; si el valor cruza un límite de hardware, recibe el helper, punto. Dejas de discutir en revisiones de código sobre si un flag debe ser en tiempo de compilación o en tiempo de ejecución, porque tienes una heurística que funciona. No son ganancias triviales. Son la diferencia entre escribir drivers y escribirlos bien.

El cambio no ocurre en un día concreto. Ocurre de forma gradual, a lo largo de tres o cuatro drivers escritos en este estilo. Un día te das cuenta de que un patrón que antes te hacía pensar es ahora simplemente lo que hacen tus dedos. Esa es la señal de que el hábito se ha consolidado. Sigue adelante.

### Una nota sobre el camino por delante

La Parte 7 de este libro se titula «Temas de maestría» por una razón. El Capítulo 29 es el primer capítulo en el que el contenido trata del oficio más que de la técnica. Los capítulos 30 al 38 continuarán en este espíritu: cada capítulo aborda un tema que distingue a un autor de drivers competente de uno experimentado. Virtualización y contenerización en el Capítulo 30. Gestión de interrupciones y procesamiento diferido en el Capítulo 31. Patrones avanzados de DMA en el Capítulo 32. Y así sucesivamente.

Los patrones de este capítulo reaparecerán a lo largo de todos esos capítulos. Un driver con soporte para virtualización utiliza la misma abstracción de backend para añadir un backend virtio. Un driver dirigido por interrupciones utiliza los mismos accesores para gestionar los registros de habilitación y reconocimiento de interrupciones. Un driver DMA avanzado usa las mismas llamadas `bus_dmamap_sync` en secuencias más sofisticadas. Todo lo que has aprendido aquí es una base para lo que viene después.

Trata el Capítulo 29 como el punto de unión entre los capítulos sobre subsistemas de drivers de la Parte 6 y los capítulos de maestría de la Parte 7. Ya lo has cruzado. El resto del libro se construye sobre lo que aprendiste aquí.

## Miniglosario de términos de portabilidad

A continuación se presenta un breve glosario, dirigido al lector que quiera repasar el vocabulario central del capítulo en un solo lugar. Úsalo como repaso, no como sustituto de las explicaciones del texto principal.

- **Backend.** Una implementación de la interfaz principal del driver para un bus, variante de hardware o simulación específicos. Un driver puede tener múltiples backends en el mismo módulo.
- **Interfaz de backend.** Una struct de punteros a función a los que llama el core. Cada backend proporciona una instancia de esta struct con sus propias implementaciones.
- **Core.** La lógica independiente del hardware de un driver. Llama a la interfaz de backend pero no sabe nada sobre buses concretos.
- **Accessor.** Un envoltorio local al driver en torno a una operación primitiva como `bus_read_4`. Proporciona al driver un único punto de cambio cuando la primitiva o el backend difieren.
- **`bus_space(9)`.** La API independiente de la arquitectura para el acceso a dispositivos con memoria mapeada y puertos I/O. Definida en `/usr/src/sys/sys/bus.h` y en los archivos `/usr/src/sys/<arch>/include/_bus.h` de cada arquitectura.
- **`bus_dma(9)`.** La API independiente de la arquitectura para el acceso directo a memoria por parte de los dispositivos. Definida en `/usr/src/sys/sys/bus_dma.h`.
- **Endianness.** El orden de bytes en que se almacena un valor multibyte en memoria. En FreeBSD se gestiona mediante funciones auxiliares declaradas en `/usr/src/sys/sys/endian.h`.
- **Alineación.** El requisito de que un valor multibyte se almacene en una dirección que sea múltiplo de su tamaño. Es flexible en `amd64` e `i386`; se aplica de forma estricta en ciertos núcleos ARM.
- **Tamaño de palabra.** El ancho entero nativo de una CPU. Existen plataformas FreeBSD de 32 y 64 bits. Los drivers deben usar tipos de ancho fijo siempre que el tamaño importe.
- **Tipo de ancho fijo.** Un tipo C cuyo tamaño está garantizado: `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t` y sus equivalentes con signo. Declarados en `/usr/src/sys/sys/types.h` y sus inclusiones.
- **`__FreeBSD_version`.** Una macro entera que identifica la versión del kernel de FreeBSD contra la que se está compilando el driver. Se usa para proteger código que depende de una versión específica de la API del kernel.
- **Opciones del kernel.** Flags de tiempo de compilación con nombre declarados en archivos de configuración del kernel. Se usan para activar o desactivar características opcionales. El driver define flags `PORTDRV_*` en el Makefile y protege el código con `#ifdef PORTDRV_*`.
- **Matriz de build.** Una tabla de configuraciones de build admitidas, junto con la indicación de si se espera que cada configuración compile y de si ha sido probada. Se valida mediante un script que itera sobre la matriz.
- **Versionado de backend.** Un campo `version` en la struct de la interfaz de backend que permite al core rechazar un backend compilado contra una interfaz no coincidente.
- **Envoltorio de compatibilidad.** Una macro o función que abstrae una diferencia de API del kernel entre versiones de FreeBSD o entre diferentes BSDs. Se usa con moderación, porque cada envoltorio supone un coste de mantenimiento.
- **Backend de simulación.** Un backend exclusivamente software que no se comunica con hardware real. Se usa para probar el core sin necesidad de un dispositivo físico.
- **Dependencia de módulo.** Una declaración, mediante `MODULE_DEPEND`, de que un módulo requiere otro en un rango de versiones específico. La impone el kernel en tiempo de carga.
- **Subregión.** Una porción con nombre de una región más grande mapeada en memoria, delimitada con `bus_space_subregion`. Se usa para asignar a cada banco lógico de registros su propio tag y handle.
- **Barrera de memoria.** Una instrucción o directiva del compilador que impone orden entre operaciones de memoria. Se emite mediante `bus_barrier` o la familia `atomic_thread_fence_*`.
- **Bounce buffer.** Un buffer temporal usado por `bus_dma` cuando un dispositivo no puede acceder directamente a la dirección física de un buffer solicitado. Transparente para el driver; funciona gracias a la cooperación entre `bus_dma_tag_create` y `bus_dmamap_sync`.
- **Grafo de inclusiones.** El grafo dirigido que indica qué archivos fuente o de cabecera incluyen a cuáles. Un grafo de inclusiones limpio es señal de un driver bien estructurado.
- **Telemetría de tiempo de compilación.** Información de build (versión, fecha de compilación, backends activados) incorporada en el binario e impresa en el momento de la carga. Útil para depuración e informes de errores.
- **API vs. ABI.** La interfaz de programación de aplicaciones es lo que ven los programadores; la interfaz binaria de aplicaciones es lo que ven los enlazadores. Un cambio puede preservar una y romper la otra; las prácticas de portabilidad deben tener en cuenta ambas.
- **Backend apilado.** Un backend cuya implementación envuelve a otro backend, añadiendo registro de eventos, trazado, retardos u otro comportamiento. Útil para herramientas de depuración y reproducción.
- **I/O mapeada en memoria (MMIO).** Una técnica en la que los registros del dispositivo aparecen como direcciones de memoria que la CPU puede leer o escribir. En FreeBSD se accede a ellos a través de `bus_space` y no mediante punteros directos.
- **I/O mapeada por puertos (PIO).** Un esquema más antiguo, habitual en `i386`, en el que se accede a los registros del dispositivo a través de un espacio de direcciones I/O separado. También se accede a ellos mediante `bus_space`, que abstrae la diferencia entre MMIO y PIO.
- **Segmento DMA.** Un rango de direcciones físicas contiguas al que el dispositivo accede directamente. `bus_dma_segment_t` describe uno; una transferencia DMA puede implicar varios segmentos.
- **Coherencia.** La propiedad de que una posición de memoria presenta el mismo valor a todos los observadores (CPU, motor DMA, cachés) en el mismo momento. En algunas arquitecturas, lograr coherencia requiere llamadas explícitas a `bus_dmamap_sync`.
- **KASSERT.** Una macro de aserción en espacio del kernel. Provoca un pánico del kernel cuando la condición es falsa; se elimina en las compilaciones de producción. Se usa para comprobar invariantes durante el desarrollo.
- **DEVMETHOD.** Una macro en un array `device_method_t` que vincula el nombre de un método Newbus a una implementación. Se termina con `DEVMETHOD_END`.
- **DRIVER_MODULE.** La macro que enlaza el nombre de un driver Newbus, el bus padre, la definición del driver y los callbacks de eventos opcionales. Normalmente aparece una vez por cada conexión de bus.
- **`MODULE_PNP_INFO`.** Una declaración que describe los identificadores de dispositivo que reclama un driver, de modo que `devmatch(8)` pueda cargar automáticamente el módulo cuando aparezca hardware coincidente. Ortogonal a `MODULE_VERSION` y `MODULE_DEPEND`.
- **Tunable del cargador.** Una variable establecida desde `/boot/loader.conf` que un módulo del kernel lee mediante `TUNABLE_INT` o similar. La forma más habitual de parametrizar un driver en tiempo de boot.
- **Nodo sysctl.** Una variable con nombre expuesta a través de la interfaz `sysctl(3)`. Puede ser de solo lectura, escribible o un manejador procedimental. Un driver portable expone contadores y opciones mediante sysctl.
- **device hints.** Un mecanismo heredado pero aún útil de FreeBSD para suministrar configuración por instancia (IRQs, direcciones I/O) desde `/boot/device.hints`. Lo analiza el framework Newbus.
- **Opciones del kernel (`opt_*.h`).** Archivos de cabecera por configuración generados por el sistema de build, usados para proteger características del kernel con `#ifdef`. Distintos de los flags de build a nivel de módulo.
- **Lista negra de módulos.** Un mecanismo (mediante `/boot/loader.conf` o `kldxref`) para impedir que un módulo se cargue automáticamente. Útil durante la depuración.
- **`bus_space_tag_t` y `bus_space_handle_t`.** Tipos opacos que juntos identifican una región de memoria. El tag indica el tipo de espacio; el handle apunta dentro de él. Se pasan como los dos primeros argumentos a `bus_space_read_*` y `bus_space_write_*`.
- **Ruta de attach.** La secuencia de llamadas Newbus (`probe`, `attach`, `detach`) que vincula un driver a una instancia de dispositivo. Los drivers portables aíslan la lógica específica del bus dentro de esta ruta.
- **Tiempo de vida del softc.** El tiempo de vida de una instancia de `struct softc`, delimitado por `attach` (asignación) y `detach` (liberación). Un driver portable trata el softc como el estado por instancia con autoridad, no como una colección de variables globales del módulo.
- **Hot path.** La ruta de código ejecutada en cada operación de I/O. Código crítico para el rendimiento. Hay que mantenerla pequeña y libre de comprobaciones innecesarias.
- **Cold path.** Código ejecutado raramente, como durante el attach, el detach o la recuperación de errores. La corrección prima sobre el rendimiento; el manejo explícito de errores corresponde aquí.
- **Hotplug.** La capacidad de un dispositivo de añadirse o extraerse físicamente de un sistema en funcionamiento. El hotplug PCI es el caso de uso principal en FreeBSD; los drivers que se espera que lo soporten deben gestionar `detach` correctamente en cualquier momento.
- **Refactor.** Un cambio estructural en el código que preserva su comportamiento observable. Los refactors de portabilidad suelen introducir accessors, dividir archivos e introducir interfaces de backend; ninguno de ellos debería cambiar lo que ven los usuarios.
- **Invariante.** Una propiedad que una pieza de código garantiza en un punto dado de la ejecución. Declarar invariantes con `KASSERT` los hace comprobables durante el desarrollo y autodocumentados a partir de entonces.
- **Configuración de build.** Una combinación de flags de Makefile, opciones del kernel y plataforma de destino que define un build específico. La matriz de build de un driver es el conjunto de configuraciones que admite oficialmente.
- **Entrega en binario.** La entrega de un driver como archivo `.ko` precompilado en lugar de como código fuente. Las entregas en binario portables son más difíciles que las entregas en código fuente; la compatibilidad de plataforma y versión se convierte en una preocupación en tiempo de ejecución.
- **Entrega en código fuente.** La entrega de un driver como código fuente, típicamente en forma de tarball o repositorio git. Más fácil de portar que una entrega en binario, porque las cabeceras del kernel y el compilador del usuario se adaptan a su plataforma.
- **Ciclo de vida del driver.** El conjunto de transiciones que atraviesa un driver desde la carga hasta la descarga: registrar, probe, attach, operar, detach, cancelar el registro. Los drivers portables documentan dónde puede fallar cada transición.
- **Ruta de migración.** Los pasos documentados que sigue un usuario al actualizar de una versión antigua del driver a una nueva. Un driver portable mantiene las rutas de migración cortas y reversibles.

Ten este glosario a mano mientras lees el Capítulo 30 y los capítulos que siguen. Cada término reaparece con suficiente frecuencia como para que una referencia rápida valga la pena.

## Preguntas frecuentes

Los nuevos autores de drivers tienden a hacer las mismas preguntas mientras trabajan en su primera refactorización de portabilidad. Aquí están las más comunes, con respuestas breves y directas. Cada respuesta es una señal en el camino, no un tratamiento exhaustivo; sigue las pistas hasta la sección correspondiente del capítulo si quieres más detalle.

**P: ¿Cuánto de esto se aplica si mi driver solo necesita ejecutarse en amd64?**

Todo, excepto las secciones específicas de arquitectura. Aunque nunca planees dar soporte a `arm64`, la disciplina de usar tipos de ancho fijo, funciones auxiliares de endianness y `bus_read_*` cuesta casi nada y te protege frente a errores que aparecerían también en `amd64`, simplemente en casos límite. La interfaz de backend y la división de archivos mejoran la legibilidad y el mantenimiento incluso cuando solo hay un backend. Si algún día tu primer driver da lugar a un segundo, te alegrarás de haber adquirido ese hábito.

**P: ¿Debería todo driver tener un backend de simulación?**

No, pero muchos deberían tenerlo. La pregunta que hay que hacerse es si el driver puede ejercitarse de forma significativa sin el hardware real. Si es así, un backend de simulación te permite ejecutar pruebas unitarias y detectar regresiones en ejecutores de CI sin necesitar el hardware. Si no, un backend de simulación es código adicional sin ningún beneficio. Los drivers de almacenamiento, los de red y muchos drivers de sensores se benefician de ello; un driver para un hardware personalizado muy específico puede que no.

**P: ¿Por qué usar una estructura de punteros a función en lugar del framework `kobj(9)` de FreeBSD?**

Para drivers pequeños, una struct sencilla es más simple y fácil de leer. `kobj(9)` es un sistema potente que añade resolución de métodos en tiempo de compilación y procesamiento de archivos de interfaz, por eso lo usa Newbus. Para un driver con unos pocos backends y un conjunto de métodos sencillo, la struct plana te proporciona la mayor parte de las ventajas con una fracción de la maquinaria. Si tu driver crece lo suficiente como para que la complejidad de `kobj` merezca la pena, migrar a él es una refactorización local.

**P: Mi driver tiene hoy un solo backend. ¿Debería definir de todas formas una interfaz de backend?**

Probablemente sí, si el esfuerzo es pequeño. Una interfaz con una sola entrada cuesta casi nada y te permite añadir un segundo backend con poco esfuerzo más adelante. Si el esfuerzo es grande porque el driver está fuertemente acoplado a su bus actual, considera primero refactorizar para separar las responsabilidades, y añade la interfaz cuando realmente se necesite un segundo backend.

**P: ¿Cómo decido dónde trazar la línea entre el núcleo y el backend?**

Una prueba práctica: ¿puedes imaginar escribir la misma lógica para un backend simulado sin cambiar el código en absoluto? Si es así, esa lógica pertenece al núcleo. Si la lógica hace referencia inherentemente a PCI, USB o cualquier bus específico, pertenece al backend. En casos dudosos, pon la lógica en el núcleo y comprueba si el backend de simulación puede usarla sin cambios; si no puede, muévela.

**P: ¿Qué hago con un driver que usa un puntero `volatile` en bruto a la memoria del dispositivo?**

Reemplaza cada acceso con `bus_read_*` o `bus_write_*`. Esta es una refactorización única y es la mejora de portabilidad más importante que puedes hacer en un driver.

**P: ¿Con qué frecuencia debería volver a ejecutar mi matriz de compilación?**

En cada commit a la rama principal, si puedes. Semanalmente si no puedes. Cuanto más tiempo viva una regresión sin detectarse, más trabajo costará encontrar su origen mediante bisección. La validación automatizada de la matriz es barata; una sesión de bisección para rastrear el problema es cara.

**P: ¿Puedo usar `#ifdef __amd64__` dentro de un archivo fuente del driver?**

Con moderación. Prefiere cabeceras específicas de arquitectura para diferencias no triviales entre plataformas. Si realmente necesitas una pequeña cantidad de código específico de la plataforma, un bloque `#if` corto es aceptable, pero si crece más allá de unas pocas líneas, considera mover el código a un archivo separado e incluirlo mediante una ruta selectiva de arquitectura o una cabecera envolvente.

**P: ¿Por qué el Makefile del driver de referencia recurre a SIM si no se selecciona ningún backend?**

Para que un simple `make` produzca un módulo cargable. Un driver que se niega a compilar sin un conjunto de flags específico es un punto de fricción para los nuevos colaboradores y para el CI. El fallback a SIM significa que cualquiera que ejecute `make` obtiene algo que puede cargar y explorar, incluso sin hardware.

**P: ¿Cuándo debería añadir `MODULE_VERSION` y `MODULE_DEPEND`?**

Siempre, incluso para drivers internos. `MODULE_VERSION` es una declaración de una sola línea que indica a quienes usan el driver qué versión tienen. `MODULE_DEPEND` evita que se carguen combinaciones incompatibles. Ambas son prácticamente gratuitas y ahorran horas de depuración cuando algo no coincide.

**P: ¿Cómo gestiona un driver cross-BSD cosas como `malloc` que tienen firmas diferentes?**

Mediante una cabecera de compatibilidad por sistema operativo que mapea un envoltorio local del driver (por ejemplo, `portdrv_malloc`) sobre la primitiva correcta del sistema operativo. El núcleo llama a `portdrv_malloc`, y la cabecera de compatibilidad contiene una rama por sistema operativo. El núcleo nunca se contamina con código específico del sistema operativo; solo lo está la cabecera envolvente.

**P: ¿Vale la pena hacer un driver de producción compatible con múltiples BSD si todos mis usuarios están en FreeBSD?**

En general, no. El coste de mantenimiento es real, y el trabajo de compatibilidad que no se ejercita tiende a degradarse. Los patrones de este capítulo siguen siendo beneficiosos para el driver exclusivo de FreeBSD, pero la capa de envoltorio explícita para otros BSD solo debería introducirse cuando haya un plan concreto para darles soporte.

**P: ¿Cómo pruebo el endianness si mi único hardware es `amd64`?**

Usa QEMU con un objetivo big-endian como `qemu-system-ppc64`. Arranca una versión big-endian de FreeBSD en el huésped, haz una compilación cruzada de tu driver para `powerpc64` y ejecútalo allí. Esta es la única forma fiable de detectar errores de endianness sin necesidad de desplegar en hardware big-endian real. El desafío 3 de este capítulo ofrece una aproximación más ligera mediante un flag de compilación.

**P: Mi driver compila en FreeBSD 14.3 y falla en 14.2. ¿Qué está ocurriendo?**

Una API añadida en 14.3. Usa `__FreeBSD_version` para proteger el uso de la nueva API y proporciona un fallback para la versión anterior. Si la nueva API es esencial y no puede evitarse, documenta claramente la versión mínima compatible en `README.portability.md`.

**P: ¿Por qué el capítulo habla constantemente del driver UART?**

Porque `/usr/src/sys/dev/uart/` es uno de los ejemplos más claros del árbol de FreeBSD de un driver con una separación limpia entre núcleo y backend y varios backends bien factorizados. Leerlo en paralelo con este capítulo es una de las mejores formas de interiorizar los patrones, y por eso lo menciono tan a menudo.

**P: ¿Hay otros drivers de FreeBSD que ilustren bien estos patrones?**

Sí. Más allá de `uart(4)`, la familia `sdhci(4)` en `/usr/src/sys/dev/sdhci/` es un buen estudio: la lógica del núcleo en `sdhci.c`, con backends PCI y FDT en `sdhci_pci.c` y `sdhci_fdt.c`. El driver `ahci(4)` en `/usr/src/sys/dev/ahci/` también separa su núcleo de sus conexiones al bus de forma limpia. El driver `ixgbe` en `/usr/src/sys/dev/ixgbe/` muestra accesores por capas y una separación estricta entre los ayudantes específicos del hardware y el resto del driver. Leer cualquiera de ellos como fuente de estudio paralela es valioso; escoger el más parecido al tipo de driver que estás escribiendo lo es aún más.

**P: Mi driver ya tiene quinientas líneas. ¿Es demasiado tarde para refactorizar en busca de portabilidad?**

No. Quinientas líneas es poco. Incluso cinco mil líneas son refactorizables, aunque la refactorización llevará unos días. Lo importante es hacerla de forma incremental: primero introduce los accesores, luego divide los archivos y después introduce la interfaz de backend. Cada paso deja el driver funcionando, y puedes detenerte en cualquier momento si las prioridades cambian.

**P: ¿Qué ocurre si mi hardware tiene una sola variante física y es poco probable que cambie?**

Entonces la portabilidad entre variantes de hardware no es una preocupación para ti. Pero la portabilidad entre arquitecturas, entre versiones de FreeBSD y entre hardware real y simulado sigue mereciendo el esfuerzo. Incluso un driver de variante única se beneficia de tener un backend de simulación para las pruebas y de usar las funciones auxiliares de endianness para que funcione en todas las arquitecturas que FreeBSD soporta. Los patrones aquí descritos no son solo sobre «qué pasa si aparece nuevo hardware»; también tratan de hacer que tu único driver sea robusto en su forma actual.

**P: ¿Cómo obtengo revisión sobre una refactorización de portabilidad?**

Si estás trabajando en un driver integrado en el árbol, envía el cambio a `freebsd-hackers@freebsd.org` o al mantenedor del subsistema correspondiente. Para drivers fuera del árbol, pregunta a un colega o a un mentor. En cualquier caso, divide la refactorización en parches pequeños: un parche por paso. Un revisor puede seguir seis parches pequeños; no puede seguir uno grande. Cuanto más pequeño hagas cada cambio, mejor será la revisión que recibirás.

**P: ¿Debería renombrar el driver cuando lo refactorizo para portabilidad?**

Por lo general, no. La refactorización es un cambio interno; los usuarios no necesitan preocuparse por ello. El nombre del módulo, el nombre del nodo de dispositivo y el nombre del árbol sysctl deberían permanecer estables para que las configuraciones existentes sigan funcionando. La estructura interna de archivos es un detalle que el usuario nunca ve.

**P: ¿Cómo justifico ante un responsable que quiere funcionalidades el tiempo dedicado a la portabilidad?**

Habla en términos de costes futuros concretos. «Si damos soporte a una segunda variante de hardware más adelante, adaptar el driver actual llevará tres semanas; una refactorización ahora toma una semana, y cualquier adaptación posterior tomaría dos días.» «Si un cliente reporta un error en arm64, el driver actual tiene que portarse antes de que podamos siquiera reproducirlo; el driver refactorizado se reproduciría en una VM.» La portabilidad se paga sola en el segundo año, no en el primero, y comunicar ese horizonte es lo que facilita la conversación.

**P: ¿Pueden dos drivers compartir un módulo de biblioteca común?**

Sí. FreeBSD soporta dependencias entre módulos mediante `MODULE_DEPEND`. Puedes construir una biblioteca común como su propio módulo y hacer que tus drivers dependan de ella. Así es como algunos de los drivers integrados en el árbol comparten código auxiliar. El mecanismo es algo engorroso para un proyecto pequeño, pero es el enfoque correcto cuando dos drivers comparten de verdad código auxiliar significativo.

**P: ¿Cuál es la prueba mínima que debería ejecutar tras una refactorización antes de hacer commit?**

Compilar. Cargar. Ejecutar el caso de uso básico una vez. Descargar. Recompilar desde cero. Volver a cargar. Si todo esto tiene éxito, la refactorización probablemente es segura. Si el driver tiene una suite de pruebas, ejecútala. Si no hay suite de pruebas, la refactorización es un buen momento para escribir una, porque el backend de simulación facilita la ejecución de las pruebas.

**P: ¿Cómo se relaciona la portabilidad con la seguridad?**

El código portable es generalmente más seguro que el no portable, porque la disciplina que respalda la portabilidad (interfaces claras, responsabilidades acotadas, uso cuidadoso de tipos, validación estricta de entradas) también respalda la seguridad. Un driver que usa `uint32_t` de forma consistente tiene menos probabilidades de desbordarse. Un driver con una interfaz de backend limpia tiene menos probabilidades de tener rutas de error mal definidas que filtren memoria. Un driver probado en múltiples arquitecturas tiene más probabilidades de haber encontrado casos límite que expongan errores de seguridad. Esta no es una relación formal, pero sí es real, y ambas preocupaciones se refuerzan mutuamente más de lo que entran en conflicto.

**P: ¿Cuánto de este capítulo se aplica a los drivers que descargo de proveedores en lugar de escribir yo mismo?**

Menos directamente, pero algo sí. Cuando revisas un driver proporcionado por un proveedor, los patrones de este capítulo te permiten evaluar su calidad. Un driver de proveedor que es un único archivo de cinco mil líneas con cadenas de `#ifdef` dispersas tiene mayor riesgo que uno estructurado en núcleo y backend. Cuando consumes ese tipo de driver, puedes usar las preguntas de auditoría del desafío 7 para formarte una opinión sobre su mantenibilidad. Si las respuestas son desalentadoras, tenlo en cuenta al decidir cuánto dependes de ese driver.

**P: ¿Cuál es la idea más importante de este capítulo?**

Que el coste de la portabilidad es una inversión que realizas una sola vez por driver, y que el retorno de esa inversión es proporcional al tiempo que el driver siga en uso. Los patrones concretos (interfaces de backend, helpers de endian, tipos de anchura fija, división de archivos) son todos manifestaciones de la misma disciplina de fondo: **separa los detalles que pueden cambiar de la lógica que no debería cambiar, y expresa esa separación en código, no en comentarios**. Aprende esa disciplina y la aplicarás en todas partes, no solo en drivers de FreeBSD.

**P: ¿Cómo depuro un driver que solo entra en pánico en arm64?**

Comienza por el propio mensaje de pánico; con frecuencia nombra la función culpable y el tipo de fallo (alineación, acceso sin alinear, puntero nulo, memoria inválida). Recompila el driver con símbolos de depuración (`CFLAGS+= -g`), cárgalo en el sistema arm64 bajo `kgdb` o `ddb`, y reproduce el pánico. Los fallos por acceso sin alinear son el tipo de pánico más común exclusivo de arm64; casi siempre significan que se ha accedido a un miembro de struct en un desplazamiento no alineado con la alineación natural de su tipo. Corrígelos empaquetando o reordenando los campos del struct, o usando `memcpy` para lecturas y escrituras que deban ser necesariamente no alineadas por diseño.

**P: ¿Cuál es el equilibrio adecuado entre la flexibilidad en tiempo de ejecución y la flexibilidad en tiempo de compilación?**

Una regla práctica útil: si una elección varía entre despliegues del mismo binario, conviértela en un parámetro ajustable en tiempo de ejecución. Si una elección varía entre versiones del driver (porque una característica es nueva, o porque una plataforma no está soportada), conviértela en un indicador de tiempo de compilación. Los parámetros ajustables permiten a los usuarios reconfigurar sin recompilar; los indicadores de tiempo de compilación mantienen el binario pequeño y preciso. En caso de duda, empieza con tiempo de compilación y promociona a tiempo de ejecución solo cuando surja una necesidad concreta.

**P: ¿Es correcto deshabilitar un backend en tiempo de ejecución mediante un sysctl?**

Sí, pero solo para backends que admitan una reinicialización limpia. Deshabilitar un backend a mitad de una operación puede provocar fugas de recursos o dejar el driver en un estado inconsistente. El patrón más seguro es exponer la elección en el momento del attach (mediante un parámetro del loader) y tratar el sysctl en tiempo de ejecución como de solo lectura, salvo durante una ventana de mantenimiento bien definida. Si expones la deshabilitación en tiempo de ejecución, pruébala exhaustivamente, incluso bajo carga de I/O.

**P: ¿Cómo contribuyo una corrección de portabilidad al proyecto FreeBSD?**

Si la corrección es para un driver ya existente en el árbol de código fuente, el camino canónico es registrar un bug en el Bugzilla de FreeBSD, adjuntar un parche en formato unified diff (o, para cambios más grandes, como commits de `git`), y esperar a que un committer lo recoja o publicar el parche en `freebsd-hackers@freebsd.org` para revisión. Incluye una descripción clara del bug, los pasos para reproducirlo y la plataforma o plataformas en las que aparece. Los parches más pequeños se revisan más rápido; si tu corrección es grande, divídela en partes revisables de forma independiente.

**P: ¿Necesita cada driver un README.portability.md?**

Todo driver que se distribuya más allá del equipo de un único desarrollador se beneficia de uno. El archivo no tiene por qué ser largo; incluso tres secciones (plataformas soportadas, configuraciones de build, limitaciones conocidas) son suficientes para cubrir lo esencial. El archivo se amortiza la primera vez que un usuario hace una pregunta que el README responde; envías un enlace en lugar de escribir la respuesta de nuevo.

**P: ¿Qué ocurre si olvido llamar a `bus_dmamap_sync` después de un DMA?**

En `amd64`, normalmente nada visible, porque la CPU y el motor DMA comparten la misma jerarquía de memoria y cache. En `arm64` con un dispositivo que no es coherente con la cache, verás datos obsoletos: la CPU lee su copia en cache en lugar de la memoria que escribió el dispositivo. El bug depende de la arquitectura, es intermitente y difícil de reproducir sin casos de prueba explícitos. Por eso `bus_dmamap_sync` es obligatorio en código portable, incluso cuando un destino concreto no lo requiere estrictamente.

**P: ¿Debo usar `kobj(9)` o punteros a función simples para mi interfaz de backend?**

Para menos de cinco o seis métodos, los punteros a función simples en un struct son más sencillos y más fáciles de depurar. Para interfaces más grandes, especialmente las que implican herencia o sobreescritura de métodos, `kobj(9)` empieza a compensar la ceremonia que conlleva. Si no estás seguro, comienza con punteros a función; migrar más adelante es una refactorización local, y para entonces tendrás una idea clara de si `kobj` merece la pena.

**P: ¿Cuál es la mejor forma de entender cómo funciona un driver existente?**

Lee el código de arriba abajo. Empieza por `DRIVER_MODULE` (el ancla de Newbus) y trabaja hacia afuera: el método attach, luego los métodos llamados desde attach, y después sus llamados. Toma notas a medida que avanzas: una descripción de una línea de cada función en un archivo de texto plano es suficiente. Tras una hora o dos tendrás un mapa funcional del driver. La profundidad llega de repetir este ejercicio con varios drivers; ninguna pasada única te convertirá en un experto.

**P: ¿Cómo verifico que mi matriz de build cubre realmente lo que creo que cubre?**

Escribe un test que falle si falta una configuración. La forma más sencilla es un bucle de shell que itere sobre las configuraciones listadas en `README.portability.md` y compruebe que cada una tiene una entrada correspondiente en el script de la matriz de build. De forma menos rigurosa, revisa el README y el script uno junto al otro una vez por versión y confirma que coinciden. El objetivo no es ser perfecto; es que las dos fuentes de verdad permanezcan alineadas con el tiempo.

**P: ¿Puedo mezclar código específico de clang y código específico de gcc en un driver portable?**

Idealmente no. El sistema base de FreeBSD usa `clang` por defecto, y escribir C portable que compile limpiamente bajo ambos compiladores rara vez es difícil. Cuando una característica concreta solo está disponible bajo un compilador (por ejemplo, un sanitizador específico o un atributo propio del compilador), protégela con `#ifdef __clang__` o `#ifdef __GNUC__` y proporciona un fallback. Pero tales casos deberían ser excepcionales; la mayor parte del tiempo, C11 estándar funciona en todas partes.

**P: ¿Qué hago cuando dos backends necesitan compartir código auxiliar?**

Coloca el código auxiliar en un archivo compartido, por ejemplo `portdrv_shared.c`, y enlaza ambos backends contra él. Alternativamente, colócalo en el núcleo si realmente pertenece ahí. Evita la tentación de copiar el código auxiliar en cada backend; copiar y pegar es lo contrario de la portabilidad. Si te encuentras queriendo duplicar una función, esa función quiere ser compartida, y la única pregunta es dónde vive la copia compartida.

**P: ¿Cómo versiono una interfaz de backend que necesita evolucionar?**

Añade un campo `version` al struct del backend. El núcleo comprueba el campo en el momento del registro y rechaza los backends compilados contra una interfaz más antigua. Cuando la interfaz evoluciona de forma compatible con versiones anteriores, incrementa la versión menor; cuando evoluciona de forma incompatible, incrementa la versión mayor y exige que todos los backends sean actualizados. El mecanismo es sencillo y previene la clase de bug en la que un backend obsoleto funciona mal silenciosamente porque su disposición de struct ha cambiado.

**P: ¿Qué hace que un driver sea «bueno» más allá de los patrones de este capítulo?**

Los buenos drivers son fiables, rápidos, mantenibles y amables con sus usuarios. La fiabilidad proviene de un manejo cuidadoso de errores y de pruebas exhaustivas. La velocidad proviene de un camino de datos limpio que evita trabajo innecesario y locks. La mantenibilidad proviene de los patrones de este capítulo. La amabilidad proviene de mensajes de error claros, un logging útil y documentación que respeta el tiempo del lector. Un driver que funciona bien en las cuatro dimensiones es un driver que merece distribuirse; un driver que funciona bien en solo dos es un driver que envejecerá mal. Apunta a las cuatro.

**P: ¿Cómo sé cuándo está terminado un driver?**

Nunca del todo. Un driver «en producción» es un driver bajo mantenimiento continuo, porque el mundo del hardware no deja de cambiar a su alrededor. Una definición razonable de «listo para distribuir» es: el driver compila limpiamente en cada configuración soportada, supera los tests que has escrito para él, sobrevive al soak test de fiabilidad y tiene un README que refleja su estado actual. Más allá de eso, cada driver es una conversación continua entre su autor, sus usuarios y la plataforma. Los patrones de este capítulo son la forma de esa conversación; el driver en sí es el registro de ella.

**P: ¿Qué debería leer después del Capítulo 30?**

Los capítulos que siguen en la Parte 7 desarrollan las bases del Capítulo 29 en direcciones concretas: virtualización (Capítulo 30), gestión avanzada de interrupciones (Capítulo 31), patrones avanzados de DMA (Capítulo 32), y así sucesivamente. Léelos en orden; cada uno da por supuestos los patrones de este capítulo como punto de partida. Si prefieres ampliar en lugar de profundizar, lee un driver de un área de FreeBSD que no hayas explorado antes (almacenamiento si has estado trabajando en red, o viceversa), y busca cómo aparecen allí los patrones del Capítulo 29. La amplitud y la profundidad se refuerzan mutuamente; alterna entre ellas.

## Dónde aparecen los patrones del capítulo en el árbol real

Un breve recorrido por el árbol de código fuente de FreeBSD, señalando dónde puedes ver los patrones de este capítulo aplicados en la práctica. Úsalo como lista de lectura cuando quieras practicar el reconocimiento de estas estructuras en código de producción.

**`/usr/src/sys/dev/uart/`** para la abstracción de backend. El núcleo en `uart_core.c` está libre de código específico del bus. Los backends en `uart_bus_pci.c`, `uart_bus_fdt.c` y `uart_bus_acpi.c` implementan cada uno la misma interfaz a través de buses distintos. La interfaz en sí reside en `uart_bus.h` como `struct uart_class`. Consulta también los archivos `uart_dev_*.c` para las distintas variantes de hardware, cada una de ellas una instancia de `struct uart_class`.

**`/usr/src/sys/dev/sdhci/`** para la abstracción en capas de bus y hardware. El núcleo `sdhci.c` es lo suficientemente completo como para tener su propia estructura interna, con backends para PCI (`sdhci_pci.c`) y FDT (`sdhci_fdt.c`), además de helpers específicos de SoC para plataformas concretas. El driver ilustra cómo una abstracción madura acomoda tanto las variaciones de bus como las variaciones menores de hardware dentro de un único bus.

**`/usr/src/sys/dev/e1000/`** para los accesores de registros en capas. El driver de Ethernet Gigabit de Intel apila su acceso a registros en cuatro capas (primitivas `bus_space_*`, los macros `E1000_READ_REG`/`E1000_WRITE_REG` en `e1000_osdep.h`, los helpers de familia de chip en los archivos por generación, y la API orientada al driver en `if_em.c`), tal como se explica en la Sección 2. Leerlo junto a dicha sección es una buena manera de entender cuándo se justifican múltiples capas.

**`/usr/src/sys/dev/ahci/`** para la división por bus y plataforma a la vez. AHCI da soporte a controladoras de almacenamiento conectadas por PCI a lo largo de varias generaciones y en plataformas tanto x86 como no x86. El driver utiliza una interfaz de backend compacta y helpers de inicialización por plataforma.

**`/usr/src/sys/dev/virtio/`** para el patrón paravirtual. El transporte virtio es en sí mismo una abstracción portable, y los drivers que lo usan ilustran cómo un backend puede ser puramente virtual. Cada driver virtio (red, bloque, consola, etc.) se comunica con el transporte virtio en lugar de con un bus físico. Este es el patrón al que volverá el Capítulo 30.

**`/usr/src/sys/net/if_tuntap.c`** para el patrón de simulación aplicado a un driver real. El driver `tuntap(4)` no es el backend de otra cosa; es un driver completo que sintetiza una interfaz de red íntegramente en software. El patrón resulta útil incluso en código de producción.

**`/usr/src/sys/sys/bus.h`** para los helpers `bus_read_*` y `bus_write_*`. Vale la pena leer esta cabecera de arriba a abajo, porque deja claro cuánta abstracción arquitectónica se apila en lo que parece una simple llamada a función.

**`/usr/src/sys/sys/endian.h`** para los helpers de endianness. Una cabecera breve que merece leerse entera al menos una vez, porque entender a qué se expande realmente `htole32` en cada plataforma es la mejor manera de recordar por qué debes usarlo.

**`/usr/src/sys/sys/bus_dma.h`** para la API de DMA. Otra cabecera breve, y el punto de partida para comprender la abstracción `bus_dma`.

**`/usr/src/sys/modules/*/Makefile`** para patrones de Makefile reales. Navega al azar; casi todos los subdirectorios tienen uno, y cada uno muestra una forma distinta de reto de portabilidad resuelto mediante un Makefile concreto.

Leer estos archivos es una de las mejores maneras de interiorizar las ideas del capítulo. No todos los archivos son simples ni pequeños, pero cada uno encarna lecciones difíciles de transmitir en abstracto. Trata el árbol como una biblioteca de ejemplos, no solo como una referencia.

## Un calendario de estudio autónomo para los patrones

Si quieres profundizar en las lecciones del capítulo antes de pasar al Capítulo 30, un modesto calendario de estudio autónomo es una buena forma de hacerlo. Las sugerencias que encontrarás a continuación están pensadas para caber en unas pocas tardes, no en un curso de una semana, y cada una te deja con un pequeño fragmento de código concreto que conservar.

**Semana 1: el driver de referencia de principio a fin.** Construye `portdrv` en cada configuración de backend. Carga, prueba y descarga cada una. Recorre el código fuente con el capítulo a mano y anota el código con los números de sección que introducen cada patrón. Al final de la semana tendrás una copia anotada de un driver funcional y portable, y un mapa mental claro de dónde aparece cada patrón en la práctica.

**Semana 2: lectura en el mundo real.** Elige un driver de la lista de la sección anterior ("Dónde aparecen los patrones del capítulo en el árbol real"). `uart(4)` es una buena primera opción; `sdhci(4)` es una buena segunda. Lee el archivo del núcleo y un archivo de backend. Para cada patrón que este capítulo introdujo, localiza el constructo análogo en el driver real y anótalo. Unas pocas páginas de notas de este ejercicio harán más por tus habilidades que leer otro capítulo en frío.

**Semana 3: un pequeño driver propio.** Escribe un driver original y pequeño desde cero usando los patrones del capítulo. Puede ser un pseudo-dispositivo, un sensor simulado o un envoltorio alrededor de una funcionalidad existente de FreeBSD expuesta como dispositivo de caracteres. El tema concreto no importa; lo que importa es que uses accesores, separes el núcleo del backend, estructures el Makefile para funcionalidades opcionales y escribas un archivo `README.portability.md`. El resultado será pequeño y quizás sencillo, pero habrás escrito un driver con los patrones del capítulo desde la primera línea, sin haberlos añadido a posteriori.

**Semana 4: una auditoría.** Encuentra cualquier driver de FreeBSD, dentro o fuera del árbol, que te resulte interesante. Aplícale el Desafío 7: repasa las preguntas de la auditoría y puntúa el driver. Escribe tus conclusiones como un breve memorando, como si se lo enviaras al autor original. No lo envíes; escríbelo como práctica. El ejercicio de formalizar lo que observas en un driver agudizará tu ojo crítico.

**Después de cuatro semanas**, habrás construido un driver, leído dos, escrito uno original pequeño y auditado otro más. Eso no es una cantidad trivial de práctica, y es suficiente para llevar los patrones del capítulo a tu propio trabajo a largo plazo. El calendario es una sugerencia, no una prescripción; ajústalo al tiempo que tengas disponible.

Una advertencia: no lo trates como una carrera. Los patrones se asientan gradualmente, y el beneficio proviene de la exposición repetida, no de ninguna sesión intensa única. Un calendario lento y constante, una hora o dos cada tarde, produce una comprensión más duradera que un sprint de un solo día que cubre el mismo terreno.

Si terminas el calendario de cuatro semanas y quieres seguir, elige uno de los temas de los capítulos posteriores (interrupciones, DMA o virtio) y aplica el mismo patrón de cuatro semanas: lee uno, estudia dos, construye uno, audita uno. El método se generaliza a cualquier tema del desarrollo del kernel de FreeBSD, y una vez que lo hayas hecho dos veces deja de parecer un calendario: simplemente se convierte en tu manera de aprender.

## Una nota sobre la colaboración y la revisión de código

Los drivers portables son más fáciles de revisar que los drivers no portables. Eso no es un accidente; es consecuencia de los patrones de este capítulo. Los accesores hacen que cada acceso a un registro sea revisable de forma aislada. La división en backend deja claro qué partes del driver son independientes del bus y cuáles no. Los tipos de ancho fijo y los helpers de endianness permiten que un revisor verifique la corrección sin ejecutar el código. Un revisor que puede ver la estructura puede centrarse en la lógica, que es lo que realmente quieres que revise.

Si estás revisando un driver escrito en el estilo de este capítulo, hay algunas preguntas que conviene tener a mano. ¿Cada acceso a un registro pasa por un accesor? ¿Está bien definida la interfaz del backend, con contratos de método claros? ¿Todos los valores multibyte que cruzan el límite del hardware pasan por los helpers de endianness? ¿El Makefile admite las configuraciones de build que el README declara? ¿Se incrementa la versión del módulo cuando cambia el comportamiento externo? Una revisión que responde estas preguntas es una revisión que añade valor; una revisión que solo comprueba el formato es una revisión que malgasta el tiempo.

Si eres tú quien recibe la revisión, no te tomes los comentarios de estilo como algo personal. Un revisor que señala un accesor omitido o un cast `volatile` sin protección te está ayudando a evitar un bug antes de que se convierta en uno. Acepta el feedback, haz el cambio y agradece al revisor. El camino más rápido para convertirte en un mejor autor de drivers es recibir revisiones frecuentes de autores que son mejores que tú.

### Listas de comprobación para la revisión de código

Si te encuentras revisando muchos drivers, una lista de comprobación breve ahorra tiempo. La que figura a continuación es compatible con los patrones de este capítulo; imprímela y tenla cerca de tu entorno de revisión.

- **Cada acceso a un registro pasa por un accesor.** Busca con grep `bus_read_`, `bus_write_` y `volatile` en los archivos que no sean el archivo de accesores. Si hay coincidencias, márcalas.
- **Cada valor multibyte que cruza el límite del hardware usa los helpers de endianness.** Examina cada struct que represente un descriptor, paquete o mensaje. Verifica que las lecturas usen `le*toh`/`be*toh` y las escrituras `htole*`/`htobe*`.
- **Los tipos de ancho fijo se usan de forma coherente.** Busca con grep `int`, `long`, `short` y `unsigned` en contextos que toquen registros de dispositivo o descriptores DMA. Si el código usa un tipo de ancho no fijo, pregunta si el tamaño era realmente desconocido.
- **El contrato de la interfaz del backend está documentado.** Un lector de la cabecera del backend debe entender el contrato que cumple cada método sin leer el código fuente del núcleo.
- **Los bloques de compilación condicional tienen comentarios.** Los bloques `#ifdef` sin una explicación de por qué existe el bloque son una señal de advertencia. Pide al autor que añada la justificación o que elimine el bloque.
- **La versión del módulo y sus dependencias están declaradas.** `MODULE_VERSION` y, cuando corresponda, `MODULE_DEPEND` están presentes. La versión coincide con la versión documentada del driver.
- **El Makefile admite las configuraciones de build documentadas.** Las configuraciones que menciona el README se compilan realmente al intentarlo. Si el README está desactualizado, dilo.
- **Los caminos de error liberan lo que asignaron.** La limpieza al estilo goto, si se usa, se deshace correctamente. Cada asignación está equilibrada con una liberación.
- **El bloqueo es coherente.** La misma estructura de datos está siempre protegida por el mismo lock, y el núcleo no llama al backend mientras mantiene un lock a menos que la documentación del backend lo permita.
- **Existen pruebas y pasan.** Como mínimo, un script de matriz de build y una prueba de humo para cada backend. Idealmente, un arnés de CI que las ejecute automáticamente.

Una revisión que cubre estos puntos es una revisión que mejora el driver. No toda revisión encontrará problemas en cada categoría, pero recorrer la lista rápidamente confirma qué categorías están limpias y cuáles merecen una atención más profunda.

## Una breve sección sobre el pragmatismo

No todos los consejos de este capítulo se aplican por igual a todos los drivers. El capítulo ha sido orientado al driver que se va a distribuir, que se mantendrá durante años, que se ejecutará en más de una plataforma y que lo leerá más de un autor. Un driver desechable escrito para confirmar una peculiaridad de hardware no necesita nada de esto. Un driver personal escrito para un único portátil no necesita la mayor parte. Los patrones son proporcionales a la vida útil prevista y al alcance del driver.

¿Cómo decides cuánto aplicar? Algunas reglas generales:

- Si el driver solo se cargará en una máquina y se reescribirá cuando se reemplace el hardware, usa tipos de ancho fijo y helpers de endianness, y omite el resto. Incluso un driver desechable se beneficia de esas dos pequeñas disciplinas.
- Si el driver se compartirá con colegas pero no se distribuirá, añade accesores y una estructura de Makefile sencilla. No lo lamentarás cuando un colega quiera compilarlo en su máquina.
- Si el driver se distribuirá a más de un cliente, aplica todo lo de las Secciones 2 a 5. La interfaz de backend, la división limpia de archivos y el uso cuidadoso de tipos son todos necesarios.
- Si el driver se incorporará al árbol o se mantendrá durante cinco años o más, aplica todo lo del capítulo. Este es el driver donde la inversión resulta más claramente rentable.

El pragmatismo consiste en saber en qué categoría encaja un driver dado y dedicarle la cantidad de esfuerzo que le corresponde. Tanto invertir de menos como invertir de más resulta perjudicial: lo primero te deja con un driver frágil; lo segundo malgasta tiempo en abstracciones que nunca llegan a compensar el esfuerzo. Este capítulo te ha mostrado qué hacer cuando un driver merece hacerse bien; el juicio sobre cuándo merece la pena es tuyo.

## Preguntas que hacerte antes de considerar un driver terminado

Antes de considerar completo un driver portable, repasa las siguientes preguntas. Son un repaso final, una última revisión de preparación para los patrones que ha introducido este capítulo. La lista se lee rápido; recorrerla con honestidad lleva más tiempo del que esperas, porque en la mayoría de los drivers aparece al menos un punto que se puede mejorar.

**¿Adivinaría un nuevo lector la organización del archivo a la primera?** Si alguien que nunca ha visto tu driver se enterase de que existe un bug en la ruta de attach PCI, ¿abriría el archivo correcto de inmediato? Si no es así, es posible que la división de archivos sea confusa o que los nombres no sean descriptivos.

**¿Compilaría el driver sin cambios en arm64, riscv64 o powerpc64?** No es necesario probarlo en todas las plataformas, pero la respuesta debería ser un sí rotundo para cada una de las plataformas que FreeBSD soporta. Si dudas, la razón de esa duda es algo que hay que corregir.

**¿Puede un usuario sustituir el hardware por una simulación y recorrer todos los caminos de código visibles al usuario?** Si el backend de simulación no puede alcanzar un determinado camino (por ejemplo, el código de recuperación de errores), la cobertura de pruebas de ese camino es implícita. Haz que la simulación sea lo suficientemente completa como para alcanzarlo.

**Si desaparecieses mañana, ¿sabría el siguiente mantenedor por qué existe cada bloque `#ifdef`?** No solo por el propio condicional, sino por el comentario que hay encima. Si algún `#ifdef` no está explicado, es un agujero en la memoria institucional del driver.

**¿Falla el driver de forma ruidosa cuando algo va mal, o falla en silencio?** Un driver que devuelve silenciosamente cero cuando debería devolver `ENODEV` es más difícil de depurar que uno que imprime una advertencia. Los modos de fallo silenciosos son los que sorprenden a la gente en producción.

**¿Puedes nombrar cada lock del driver y decir qué protege?** Si no puedes ofrecer una descripción de una sola frase para cada lock y los datos que protege, el diseño de locking probablemente es menos claro de lo que crees. Corrígelo antes de dar el driver por terminado.

**¿Tiene éxito `kldunload` siempre?** Ejecuta el driver, envía algo de tráfico a través de él y luego intenta descargarlo. Si se cuelga, el driver tiene una referencia que no está liberando. Encuentra esa referencia antes de publicarlo.

**¿Sobrevive el driver a ciclos repetidos de `kldload`/`kldunload`?** Ejecuta diez cargas y descargas en bucle. Si el uso de memoria va aumentando, tienes una fuga. Si la décima descarga falla donde la primera tuvo éxito, tienes una referencia obsoleta.

**¿Gestiona el driver que se cargue antes que sus dependencias?** Las declaraciones `MODULE_DEPEND` son el mecanismo, pero merece la pena comprobar manualmente que cargar tu driver desde `/boot/loader.conf` (antes de que userland esté completamente operativo) funciona como esperas.

**¿Son los mensajes de error lo suficientemente específicos para ser útiles en un informe de bug?** Si un usuario te envía `portdrv0: attach failed`, ¿puedes identificar la causa? Si no, añade contexto: qué operación falló, con qué código, en qué bus.

**¿Supera el Makefile un build limpio con `-Werror`?** Si no es así, algo en tu código está generando advertencias. Las advertencias son el compilador indicándote problemas en el código; no silencies la señal.

**¿Has eliminado todos los `printf` añadidos para depuración?** Los prints de depuración dejados en el código de producción ralentizan el driver y llenan de ruido `dmesg`. Usa `if_printf`, `device_printf` o macros de depuración con activación controlada, y elimina todo lo que no sea necesario.

Recorrer esta lista antes de cada publicación es un hábito que vale la pena desarrollar. Un driver que supera las doce preguntas es un driver listo para publicar; uno que falla en alguna de ellas tiene un problema que vale la pena resolver antes de que salga la versión.

## Recursos para seguir aprendiendo

Los patrones de este capítulo tienen suficiente profundidad como para que una sola lectura no los agote. Los recursos que se presentan a continuación son los que con mayor probabilidad compensan el esfuerzo de profundizar en ellos.

**El árbol de código fuente de FreeBSD.** Ningún libro sobre drivers de FreeBSD puede sustituir el tiempo dedicado a leer el árbol en sí. Los drivers concretos a los que apunta este capítulo (`uart`, `sdhci`, `ahci`, `e1000`, `virtio`, `tuntap`) son puntos de partida. Cuando encuentres uno que te guste, lee su historial completo de commits. La evolución de un driver suele enseñar tanto como el estado actual.

**El FreeBSD Handbook.** El Handbook no trata principalmente sobre el desarrollo de drivers, pero sus capítulos sobre el kernel, sobre jails y sobre `bhyve` proporcionan el contexto que explica cómo se despliegan los drivers. Un driver portable coopera con estas características; leer sobre ellas forma parte de escribir para ellas.

**`sys/sys/bus.h`, `sys/sys/bus_dma.h`, `sys/sys/endian.h`, `sys/sys/systm.h`.** Estas cabeceras son lo suficientemente cortas como para leerlas de arriba abajo en una sola sesión. Cada una enseña un aspecto de portabilidad diferente: `bus.h` las abstracciones del bus, `bus_dma.h` las abstracciones de DMA, `endian.h` los ayudantes de endianness, `systm.h` las utilidades generales del kernel. Léelas, márcalas, vuelve a ellas.

**El FreeBSD Architecture Handbook.** Este es el documento que hay que leer cuando quieres entender por qué las cosas son como son. Es menos práctico que este libro, pero lo complementa explicando la historia y el razonamiento que hay detrás de los subsistemas clave del kernel.

**La página de manual `style(9)`.** Sin glamour, pero la referencia más importante para saber cómo debe lucir tu código. Léela una vez en profundidad; repásala superficialmente cada seis meses. La familiaridad con el estilo del árbol hace que tus drivers sean más fáciles de leer y revisar para la comunidad.

**Foros de la comunidad: `freebsd-hackers@freebsd.org` y `freebsd-arch@freebsd.org`.** Estas listas son los mejores lugares para hacer preguntas sobre problemas no triviales de drivers. Busca en los archivos antes de publicar; muchas preguntas ya han sido respondidas. Cuando publiques, sigue las convenciones de la comunidad: asunto claro, descripción autocontenida, versiones y plataformas concretas indicadas.

**El log de commits de FreeBSD (`git log` en el árbol de código fuente).** Cuando quieras entender cómo llegó a existir una característica concreta, trázala a través del log. Encontrarás discusiones de diseño incrustadas en los mensajes de commit, especialmente para cambios grandes. Lee con amplitud: no solo los commits del driver que estás estudiando, sino también los cambios del kernel que impulsan los nuevos requisitos de los drivers.

**Tus propios cambios a lo largo del tiempo.** Si aplicas los patrones de este capítulo en varios drivers, mantenlos en un sistema de control de versiones y repasa el historial de vez en cuando. Tu propia evolución como autor de drivers es tan instructiva como cualquier referencia externa; revisar las decisiones que tomaste hace seis meses es una de las mejores formas de ver en qué estás creciendo.

Estos recursos te sostendrán más allá de los límites de este libro. El libro es una introducción y un andamiaje; la maestría viene del trabajo continuo de aplicar los patrones, leer más código y refinar tu propio gusto por la ingeniería. Ve a tu ritmo y mantén la curiosidad.

## Diez principios para llevar adelante

Un capítulo tan extenso merece una síntesis final. Los diez principios que siguen son lo que hay que recordar si nada más de este capítulo se asienta en la memoria. Cada uno es una destilación de una sección; juntos conforman la forma de un driver portable.

**1. Separa lo que cambia de lo que no cambia.** El corazón de la portabilidad está en saber qué partes de tu driver tienen probabilidad de variar y aislarlas detrás de una interfaz. Los buses cambian. Las variantes de hardware cambian. Las arquitecturas cambian. La lógica central del driver no debería hacerlo.

**2. Escribe a través de accessors, no a través de primitivas.** Cada acceso a un registro pasa por una función con nombre en tu driver. El nombre describe la operación, no la primitiva subyacente. Algún día te alegrarás de haberlo hecho.

**3. Usa tipos de ancho fijo en los límites del hardware.** `uint32_t` para un campo de registro de 32 bits, `uint64_t` para una palabra descriptora de 64 bits. Nunca `int` o `long` cuando el tamaño importa.

**4. Intercambia bytes en el límite, y solo en el límite.** Cada valor multibyte que cruza del CPU al hardware o viceversa pasa por un ayudante de endianness. El resto del código no tiene que preocuparse por el orden de bytes.

**5. Expresa la variación como datos, no como condicionales.** Una tabla de backends, una struct de punteros a función, un registro indexado por ID de dispositivo. Estas opciones son más fáciles de leer, probar y ampliar que las largas cadenas `if-else`.

**6. Divide los archivos por responsabilidad, no por tamaño.** El núcleo en un archivo, cada backend en el suyo propio, cada gran preocupación de subsistema en el suyo. Los límites de los archivos comunican la intención de diseño.

**7. Mantén la compilación condicional gruesa y comentada.** Un bloque `#ifdef` con un comentario claro es un activador de características. Un mar de `#ifdef` anidados sin comentarios es una crisis de mantenimiento.

**8. Versiona todo lo que puede cambiar.** `MODULE_VERSION`, campos de versión de interfaz de backend, `README.portability.md`. Cuando el mundo del driver cambia, sus números de versión hacen el cambio legible.

**9. Prueba con simulación, construye sobre una matriz.** Un backend de simulación ejercita tu núcleo sin hardware. Una matriz de build detecta regresiones de portabilidad en tiempo de compilación antes de que lleguen a los usuarios. Ambos son multiplicadores de fuerza.

**10. Documenta los invariantes, no la sintaxis.** Los comentarios explican por qué se tomó una decisión, no lo que hace el código. El código dice qué; los comentarios dicen por qué. La combinación es lo que hace que un driver sobreviva a su autor.

Estos diez principios son el capítulo. Todo lo demás es elaboración, ejemplo y ejercicio. Memorízalos, aplícalos de forma consistente, y los patrones se sentirán naturales en el plazo de un año. Esa es la recompensa.

Una nota más sobre los principios. No son un ranking; son un conjunto. Omitir cualquiera de ellos debilita a los demás. Un driver con un manejo de endianness perfecto pero sin división de archivos sigue siendo difícil de mantener. Un driver con una división de archivos limpia pero sin `MODULE_VERSION` sigue siendo difícil de actualizar. Un driver con abstracciones impecables pero sin pruebas sigue siendo frágil ante los cambios. Los patrones se refuerzan mutuamente, y la ausencia de uno socava el valor del resto. Si tienes que elegir un subconjunto (por ejemplo, porque el driver es pequeño o desechable), elige en función de la vida útil y el alcance que esperas, no del patrón que resulte más barato hoy.

La última palabra de ánimo es la más sencilla: empieza en algún punto. Coge un driver, un fin de semana, un patrón de la lista, y aplícalo. Observa qué cambió. Fíjate en que el driver mejoró, aunque sea ligeramente. Luego repítelo el fin de semana siguiente, con otro patrón. Después de un mes así, el driver estará en mejor estado del que recuerdas, y tu sentido de cómo se aplican los patrones en la práctica será sólido. El camino hacia la maestría no es dramático; es simplemente la paciente acumulación de pequeñas mejoras a lo largo del tiempo. Ahora tienes el mapa.

## Cerrando el capítulo

La portabilidad es más fácil de interiorizar cuando puedes verla en la página, así que cerremos el capítulo con un ejemplo pequeño y concreto de cómo se ve un cambio entre versiones en el árbol real de FreeBSD. Abre `/usr/src/sys/dev/rtsx/rtsx.c`, busca `rtsx_set_tran_settings()` y observa el bloque corto protegido por `#if __FreeBSD_version >= 1300000`. Dentro de ese guard, el driver gestiona el campo `MMC_VCCQ`, que lleva la tensión de I/O de una tarjeta MMC y se añadió en la serie FreeBSD 13. En árboles más antiguos el campo no existe; en 14.x sí existe; y el guard es lo único que permite que un único archivo fuente compile limpiamente en ambos. Eso es la portabilidad expresada como un condicional breve en lugar de como dos bifurcaciones del mismo driver, y es exactamente la disciplina que este capítulo te ha pedido que adoptes.

La lección de ese pequeño bloque es mayor que el bloque en sí. Cada cambio entre versiones, ya sea un nuevo campo en una estructura, una función renombrada, una macro obsoleta o una cabecera reorganizada, puede absorberse con el mismo patrón que acabas de ver: nombra la variación, aíslala detrás de un guard o un accessor, y deja un comentario que explique por qué está ahí el bloque. El driver sigue funcionando tanto en árboles antiguos como nuevos, y tu energía va a la lógica en lugar de a mantener copias paralelas del mismo archivo para cada versión que te importa.

Una historia similar, aunque a una escala mucho mayor, se puede ver en el driver Ethernet de Intel situado en `/usr/src/sys/dev/e1000/if_em.c`. En él, la ruta de attach y detach pasa ahora por el framework `iflib`: `em_if_attach_pre(if_ctx_t ctx)` reemplaza el attach sobre `device_t` propio que este driver implementaba directamente, y la tabla `DEVMETHOD` cerca del inicio del archivo despacha `device_probe`, `device_attach` y `device_detach` a través de `iflib_device_probe`, `iflib_device_attach` e `iflib_device_detach`. No estás mirando un simple condicional; estás viendo el resultado de migrar toda una familia de drivers de NIC a una abstracción compartida para que el código específico de cada chip y el código común de `iflib` puedan evolucionar a distintos ritmos. Es el mismo instinto que el ejemplo de `rtsx`, aplicado a una escala mayor.

Si te llevas una sola cosa de este capítulo, quédate con ese instinto. La portabilidad no es una propiedad que añades al final del proyecto, y tampoco es un tipo especial de código que escribes una vez y olvidas. Es una manera de sentarse ante un archivo fuente y preguntarse: ¿qué puede cambiar, dónde cambiará y cómo me aseguro de que ese cambio aterrice en un único lugar y no en veinte? Responde a esa pregunta de forma consistente y los drivers que escribas en los próximos años sobrevivirán a varias generaciones de hardware, varias versiones del kernel y probablemente varios trabajos.

## Mirando hacia adelante: puente al capítulo 30

Acabas de refactorizar un driver para mejorar su portabilidad. El siguiente capítulo, **Virtualización y contenedorización**, abandona los aspectos internos del propio driver para centrarse en el entorno en que se ejecuta. Los drivers de FreeBSD no solo funcionan sobre hardware físico, sino también dentro de máquinas virtuales `bhyve`, bajo `jail(8)` con VNET, sobre almacenamiento y red respaldados por virtio, y cada vez más dentro de entornos de ejecución de contenedores estilo OCI construidos sobre jails.

Esa pregunta cobra más sentido tras el capítulo 29 que antes. Has aprendido a escribir drivers que absorben las variaciones de hardware, bus y arquitectura como cambios locales. El capítulo 30 añadirá un nuevo tipo de variación: el propio entorno está virtualizado. La máquina que ve tu driver puede no ser una máquina real; los dispositivos que sondea pueden ser paravirtuales; el host puede haber eliminado clases enteras de capacidad porque el guest no tiene permiso para utilizarlas. Aprenderás cómo funcionan los drivers guest de virtio, cómo encajan `vmm(4)` y `pci_passthru(4)` de `bhyve`, cómo los jails con VNET aíslan las pilas de red, y cómo escribir drivers que cooperen con esas formas de contención sin asumir los privilegios habituales.

En el capítulo 30 no escribirás un nuevo tipo de driver. Aprenderás cómo los drivers que ya sabes escribir deben adaptarse cuando el entorno que los rodea es virtual, particionado o contenedorizado. Es un paso de naturaleza diferente, y tiene importancia en el momento en que despliegas un sistema FreeBSD en la nube o en un host multi-tenant.

Antes de continuar, descarga todos los módulos que creaste en este capítulo, limpia todos los directorios de build y asegúrate de que el árbol de código fuente del driver esté en buen estado. Cierra tu cuaderno de laboratorio con una breve nota sobre qué funcionó, qué te sorprendió y qué patrón esperas utilizar con más frecuencia en tus propios drivers. Descansa la vista un momento. Después, cuando estés listo, pasa la página.

## Una última palabra: el arco largo

Vale la pena dar un paso atrás una vez más, porque los capítulos sobre el oficio son fáciles de leer y difíciles de aplicar. Los patrones de este libro no son una lista de tareas que marcar en un único proyecto; son una inversión a largo plazo en tu propia capacidad. El primer driver que escribas con ellos se sentirá como mucho trabajo por un beneficio modesto. El quinto driver te parecerá natural. El décimo será más rápido y fiable que cualquier driver que habrías podido escribir sin los patrones, y ya no recordarás que en su día te resultaron laboriosos.

Ese arco no es exclusivo del desarrollo de drivers en FreeBSD. Es el arco de toda práctica especializada, desde la carpintería hasta la música o la escritura de compiladores. El oficio comienza como un conjunto de disciplinas que se sienten pesadas y deliberadas; con suficiente práctica, se convierte en la forma de tu atención. Cuando te sientes a escribir tu próximo driver y descubres que ya has pensado en la estructura del backend antes de escribir la primera línea, el oficio está trabajando.

El capítulo termina aquí, pero la práctica no. Sigue construyendo. Sigue leyendo. Sigue refactorizando drivers que aún no son tan portables como podrían ser. La recompensa solo es medible a lo largo de años, que es exactamente la escala de tiempo en la que los drivers de FreeBSD suelen vivir. Has elegido un juego largo; los patrones de este capítulo son las reglas que te permiten jugarlo bien.

Te lo has ganado.
