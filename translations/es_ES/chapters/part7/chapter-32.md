---
title: "Árbol de dispositivos y desarrollo embebido"
description: "Desarrollo de drivers para sistemas embebidos usando device tree"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 32
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 195
language: "es-ES"
---
# Device Tree y desarrollo embebido

## Introducción

El capítulo 31 te entrenó a mirar tu driver desde fuera, con los ojos de quien podría intentar abusar de él. Los límites que aprendiste a vigilar eran invisibles para el compilador, pero muy reales para el kernel: el espacio de usuario a un lado, la memoria del kernel al otro; un thread con privilegios y otro sin ellos; un campo de longitud que el llamante afirmaba, y una longitud que el driver debía verificar. Ese capítulo trataba sobre quién tiene permiso para pedirle algo al driver y qué debe comprobar el driver antes de aceptar.

El capítulo 32 cambia la perspectiva por completo. La pregunta deja de ser *quién quiere que este driver funcione* y se convierte en *cómo encuentra este driver su hardware*. En las máquinas en las que nos hemos apoyado hasta ahora, la respuesta era suficientemente sencilla como para ignorarla. Un dispositivo PCI se anunciaba a través de registros de configuración estándar. Un periférico descrito mediante ACPI aparecía en una tabla que el firmware entregaba al kernel. El bus se encargaba de buscar, el kernel exploraba cada candidato, y la función `probe()` de tu driver solo tenía que mirar un identificador y decir sí o no. El descubrimiento era, en su mayor parte, un problema ajeno.

En las plataformas embebidas, esa suposición falla. Una pequeña placa ARM no habla PCI, no lleva una BIOS con ACPI y no dispone de una capa de firmware que entregue al kernel una tabla ordenada de dispositivos. El SoC tiene un controlador I2C en una dirección física fija, tres UART en otras tres direcciones fijas, un banco de GPIO en una cuarta, un temporizador, un watchdog, un árbol de reloj, un multiplexor de pines y una docena de otros periféricos que el diseñador de hardware soldó en la placa con una disposición particular. Nada en el silicio se anuncia por sí solo. Si el kernel va a conectar drivers a estos periféricos, algo tiene que decirle al kernel dónde están, qué son y cómo se relacionan.

Ese algo es el **Device Tree**, y aprender a trabajar con él es de lo que trata el capítulo 32.

Device Tree no es un driver. No es un subsistema en el sentido en que `vfs` o `devfs` lo son. Es una *estructura de datos*, una descripción textual del hardware que el bootloader entrega al kernel en el momento del boot y que el kernel recorre para decidir qué drivers ejecutar y adónde apuntarlos. La estructura tiene su propio formato de archivo, su propio compilador, sus propias convenciones formales y sus propias convenciones tácitas que los desarrolladores embebidos aprenden con el tiempo. Un driver escrito para una plataforma con Device Tree tiene casi el mismo aspecto que los drivers que ya conoces, con unas pocas diferencias importantes en cómo encuentra sus recursos. Esas diferencias son el tema de este capítulo.

También vamos a ampliar el mapa. Hasta ahora, la mayoría de tus drivers han funcionado en amd64, la variante de x86 de 64 bits que alimenta portátiles, estaciones de trabajo y servidores. Esa arquitectura no va a desaparecer, y tu comprensión de ella seguirá siendo útil. Pero FreeBSD funciona en más que amd64. Funciona en arm64, la arquitectura ARM de 64 bits que impulsa la Raspberry Pi 4, la Pine64, la HoneyComb LX2, infinidad de placas industriales y una fracción cada vez mayor de la nube. Funciona en ARM de 32 bits para Pis más antiguas, BeagleBones y dispositivos embebidos. Funciona en RISC-V, una arquitectura más reciente y abierta cuyo primer soporte serio en FreeBSD maduró durante los ciclos de FreeBSD 13 y 14. En todas esas arquitecturas fuera del mundo similar al PC, Device Tree es el mecanismo por el que los drivers encuentran su hardware. Si quieres escribir drivers que funcionen en algo más que un portátil, necesitas saber cómo funciona.

La buena noticia es que la forma de un driver no cambia mucho al dar ese salto. Tus rutinas probe y attach siguen pareciendo rutinas probe y attach. Tu softc sigue viviendo en el mismo tipo de estructura. Tu ciclo de vida sigue siendo el mismo, con carga y descarga, detach y limpieza. Lo que cambia es el conjunto de funciones auxiliares que llamas para descubrir la dirección de tu hardware, leer su especificación de interrupción, encender su reloj, activar su línea de reset y solicitar un pin GPIO. Esas funciones auxiliares guardan un parecido de familia una vez que has visto unas cuantas. El árbol de código fuente de FreeBSD las usa en cientos de lugares, y al final de este capítulo las reconocerás a primera vista y sabrás dónde buscar cuando necesites una que no hayas usado antes.

El capítulo se desarrolla en ocho secciones. La sección 1 presenta el mundo embebido de FreeBSD y las plataformas hardware en las que funciona. La sección 2 explica qué es Device Tree, cómo se organizan sus archivos, cómo el código fuente `.dts` se convierte en blobs binarios `.dtb` y qué significan realmente los nodos y las propiedades que contiene. La sección 3 recorre el soporte de FreeBSD para Device Tree: el framework `fdt(4)`, las interfaces `ofw_bus`, el enumerador `simplebus` y las funciones auxiliares de Open Firmware que los drivers usan para leer propiedades. La sección 4 es la primera sección concreta de escritura de drivers. Verás la forma de un driver consciente de FDT desde el probe hasta el attach y el detach, con su tabla `ofw_compat_data` y sus llamadas a `ofw_bus_search_compatible`, `ofw_bus_get_node` y las funciones auxiliares de lectura de propiedades. La sección 5 se centra en el DTB: cómo compilar un `.dts` con `dtc`, cómo funcionan los overlays, cómo añadir un nodo personalizado a una descripción de placa existente y cómo encaja el toolchain de build de FreeBSD. La sección 6 trata sobre la depuración, tanto en el boot como en tiempo de ejecución, usando `ofwdump(8)`, `dmesg` y el log del kernel para averiguar por qué un nodo no coincidió. La sección 7 reúne las piezas en un driver GPIO práctico que toma sus asignaciones de pines del Device Tree y conmuta un LED. La sección 8 es la sección de refactorización: tomaremos el driver que construiste en la sección 7, reforzaremos su manejo de errores, expondremos un sysctl para la observabilidad y hablaremos sobre cómo se empaqueta una imagen embebida.

Por el camino, tocaremos los frameworks de periféricos en los que todo driver embebido serio termina apoyándose. El framework de reloj, el framework de reguladores y el framework de reset de hardware bajo `/usr/src/sys/dev/extres/` son los tres principales; el framework de control de pines bajo `/usr/src/sys/dev/fdt/` es el cuarto. El framework GPIO bajo `/usr/src/sys/dev/gpio/` es tu primer destino para leer y escribir pines. El encadenamiento `interrupt-parent` enruta las IRQ hacia arriba en el árbol de interrupciones hasta el controlador que puede gestionarlas realmente. Los *overlays* de Device Tree, archivos con extensión `.dtbo`, te permiten añadir o modificar nodos en el boot sin reconstruir el blob base. Y en el nivel más alto, en arm64 en particular, un único binario del kernel puede ejecutarse tanto con FDT como con ACPI; el mecanismo que elige entre los dos merece una mirada breve, porque muestra cómo se factorizan los drivers escritos para ambos.

Una última nota de contexto antes de empezar. El trabajo embebido puede resultar intimidante desde la distancia. El vocabulario es desconocido, las placas son pequeñas y complicadas, la documentación es más escasa que en las plataformas de escritorio, y la primera vez que un desajuste del DTB te mete en un boot silencioso que no avanza es fácil desanimarse. Nada de eso es razón para alejarse. La habilidad central que vas a desarrollar en este capítulo, leer un archivo `.dts` y reconocer qué se está describiendo, se puede aprender en una tarde larga. La segunda habilidad, escribir un driver que coincide con una cadena de compatible y recorre las propiedades que necesita, es el mismo tipo de escritura de drivers que ya conoces con tres o cuatro nuevas llamadas auxiliares. La tercera habilidad, construir y cargar un DTB y ver cómo tu driver se conecta, es exactamente el tipo de bucle de retroalimentación que hace que el trabajo en el kernel sea satisfactorio. Al final del capítulo habrás escrito, compilado, cargado y observado un driver consciente de FDT en una placa ARM real o emulada, y tendrás el modelo mental que te permite leer cualquier driver embebido de FreeBSD y entender cómo encuentra su hardware.

El resto del libro seguirá tratando los drivers como drivers, pero tu kit de herramientas habrá crecido. Comencemos.

## Guía para el lector: cómo usar este capítulo

El capítulo 32 ocupa un lugar particular en el arco del libro. Los capítulos anteriores asumían una máquina similar a un PC donde los buses se autodescubrían y los drivers se enlazaban a dispositivos que el kernel ya conocía. Este capítulo da un paso lateral hacia un mundo donde el descubrimiento es un problema en el que el driver tiene que participar. Ese paso es más fácil de lo que parece, pero sí requiere un pequeño cambio en cómo piensas sobre el hardware y la disposición a leer nuevos tipos de archivos.

Hay dos caminos de lectura, y un tercero opcional para lectores que tengan hardware embebido en casa.

Si eliges el **camino solo de lectura**, planifica entre tres y cuatro horas de concentración. Terminarás el capítulo con un modelo mental claro de qué es Device Tree, cómo lo usa el kernel, qué aspecto tiene un driver consciente de FDT y dónde viven los archivos fuente importantes de FreeBSD. No habrás escrito un driver, pero serás capaz de leer uno y entender cada llamada auxiliar que veas. Para muchos lectores, este es el punto de parada adecuado en una primera lectura.

Si eliges el **camino de lectura más laboratorios**, planifica entre siete y diez horas repartidas en dos o tres sesiones. Los laboratorios giran en torno a un pequeño driver pedagógico llamado `edled`, abreviatura de *embedded LED*. A lo largo del capítulo escribirás un driver FDT mínimo que coincide con una cadena de compatible personalizada, lo ampliarás hasta convertirlo en un driver que lee su número de pin e intervalo de temporizador del Device Tree y, finalmente, lo completarás con un camino de detach ordenado y un sysctl para la observabilidad en tiempo de ejecución. Compilarás un pequeño fragmento `.dts` como un overlay `.dtb`, lo cargarás con el loader de FreeBSD y observarás cómo el driver se conecta en `dmesg`. Ninguno de estos pasos es largo; ninguno requiere más que una familiaridad básica con `make`, `sudo` y un shell.

Si tienes acceso a una Raspberry Pi 3 o 4, una BeagleBone, una Pine64, una Rock64 o una placa ARM compatible, puedes seguir el **camino de lectura más laboratorios más hardware**. En ese caso, el driver `edled` hace parpadear un LED real conectado a un pin real, y obtienes la satisfactoria experiencia de escribir código del kernel que hace que algo físico ocurra. Si no tienes hardware, no te preocupes. Todo el capítulo puede seguirse con una máquina virtual QEMU que emule una plataforma ARM genérica, o incluso mediante simulación en una máquina FreeBSD convencional. No te perderás ningún contenido conceptual.

### Requisitos previos

Deberías estar cómodo con el esqueleto del driver de FreeBSD de los capítulos anteriores: inicialización y salida del módulo, `probe()` y `attach()`, asignación y destrucción del softc, el registro con `DRIVER_MODULE()` y los fundamentos de `bus_alloc_resource_any()` y `bus_setup_intr()`. Si alguno de ellos está poco claro, revisar brevemente los capítulos 6 al 14 antes de este capítulo merecerá la pena. Las funciones auxiliares de este capítulo se sientan *junto a* las conocidas funciones auxiliares `bus_*`; no las reemplazan.

También deberías estar cómodo con la administración básica del sistema FreeBSD: cargar y descargar módulos del kernel con `kldload(8)` y `kldunload(8)`, leer `dmesg(8)`, editar `/boot/loader.conf` y ejecutar un comando como root. Los usarás todos, pero ninguno a un nivel más profundo del que ya han requerido los capítulos anteriores.

No se requiere experiencia previa con hardware embebido. Si nunca has tocado una placa ARM, el capítulo te enseñará lo que necesitas. Si has usado una Raspberry Pi pero solo como una pequeña máquina Linux, el capítulo te dará una nueva perspectiva sobre lo que ocurre bajo su superficie.

### Estructura y ritmo

La sección 1 prepara el escenario: cómo es FreeBSD en entornos embebidos en la práctica, qué arquitecturas soporta FreeBSD, qué tipos de placas es probable que encuentres y por qué Device Tree es la respuesta que las plataformas embebidas han adoptado. La sección 2 es la sección conceptual más larga: presenta los archivos Device Tree, sus formas en código fuente y en binario, su estructura de nodos y propiedades, y el pequeño conjunto de convenciones que necesitas para leerlos con fluidez. La sección 3 retoma la conversación en el contexto específico de FreeBSD, presentando el framework `fdt(4)`, las interfaces `ofw_bus` y el enumerador simplebus. La sección 4 es la primera sección dedicada a la escritura de drivers; recorre la estructura canónica de un driver FDT desde probe hasta attach y detach. La sección 5 te enseña a construir y modificar archivos `.dtb` y cómo funciona el sistema de overlays de FreeBSD. La sección 6 es la sección de depuración. La sección 7 es el ejemplo práctico detallado: el driver GPIO `edled`. La sección 8 es la sección de refactorización y finalización.

Tras la sección 8 encontrarás laboratorios prácticos, ejercicios de desafío, una referencia de resolución de problemas, la revisión del apartado «Cerrando» y el puente hacia el capítulo 33.

Lee las secciones en orden. Se construyen unas sobre otras y las secciones posteriores asumen el vocabulario que establecen las anteriores. Si tienes poco tiempo y quieres el recorrido conceptual mínimo, lee las secciones 1, 2 y 3, y luego repasa la sección 4 para ver su esqueleto de driver; eso te da el mapa del territorio.

### Trabaja sección a sección

Cada sección cubre una parte coherente del tema. Lee una, deja que se asiente y pasa a la siguiente. Si al terminar una sección algún punto todavía resulta confuso, para, vuelve a leer los párrafos finales y abre el archivo fuente de FreeBSD que se cita. Un vistazo rápido al código real suele aclarar en treinta segundos lo que la prosa solo puede rodear.

### Mantén el driver de laboratorio a mano

El driver `edled` que se utiliza en el capítulo vive en `examples/part-07/ch32-fdt-embedded/` dentro del repositorio del libro. Cada directorio de laboratorio contiene el estado del driver en ese paso, con su `Makefile`, un breve `README.md`, el overlay DTS correspondiente cuando procede y los scripts de apoyo necesarios. Clona el directorio, trabaja en él directamente y carga cada versión tras cada cambio. Un módulo del kernel que se engancha (visible en `dmesg`) y hace alternar un pin que puedes observar es el ciclo de retroalimentación más concreto del trabajo embebido; aprovéchalo.

### Abre el árbol de código fuente de FreeBSD

Varias secciones señalan archivos reales de FreeBSD. Los más útiles para mantener abiertos durante este capítulo son `/usr/src/sys/dev/fdt/simplebus.c` y `/usr/src/sys/dev/fdt/simplebus.h`, que definen el enumerador sencillo de bus FDT al que se vincula cada driver hijo; `/usr/src/sys/dev/ofw/ofw_bus.h`, `/usr/src/sys/dev/ofw/ofw_bus_subr.h` y `/usr/src/sys/dev/ofw/ofw_bus_subr.c`, que proporcionan los helpers de compatibilidad y los lectores de propiedades; `/usr/src/sys/dev/ofw/openfirm.h`, que declara las primitivas `OF_*` de nivel inferior; `/usr/src/sys/dev/gpio/gpioled_fdt.c`, un pequeño driver real que imitarás; `/usr/src/sys/dev/gpio/gpiobusvar.h`, que define `gpio_pin_get_by_ofw_idx()` y sus funciones hermanas; y `/usr/src/sys/dev/fdt/fdt_pinctrl.h`, que define la API de pinctrl. Ábrelos cuando el capítulo te los indique. El código fuente es la autoridad; el libro es una guía hacia él.

### Mantén un cuaderno de laboratorio

Continúa el cuaderno de laboratorio de los capítulos anteriores. Para este capítulo, anota una breve nota por cada laboratorio: qué comandos ejecutaste, qué DTB cargaste, qué decía `dmesg`, qué pin utilizaste y cualquier sorpresa. La depuración en sistemas embebidos se beneficia más que en casi cualquier otro contexto de tener un registro escrito, porque las variables que hacen que un driver no se enganche (un overlay que falta, un phandle incorrecto, un número de pin intercambiado) son fáciles de olvidar y costosas de redescubrir.

### Ve a tu ritmo

Los conceptos de este capítulo suelen ser más sencillos la segunda vez que los encuentras que la primera. Las palabras *phandle*, *ranges* e *interrupt-parent* pueden resultar incómodas durante un tiempo antes de encajar. Si una subsección se vuelve confusa, márcala, sigue adelante y vuelve a ella más tarde. La mayoría de los lectores encuentra que la Sección 2 (el formato del Device Tree en sí) es la más difícil del capítulo; después de ella, las Secciones 3 y 4 parecen sencillas porque son principalmente *código de driver* y la forma del código de driver ya resulta familiar.

## Cómo sacarle el máximo partido a este capítulo

Unos pocos hábitos te ayudarán a convertir la prosa del capítulo en una intuición duradera. Son los mismos hábitos que sirven para cualquier subsistema nuevo, ajustados a las particularidades del trabajo embebido.

### Lee código fuente real

La mejor forma de aprender a leer archivos de Device Tree y drivers preparados para FDT es leer unos reales. El árbol de FreeBSD incluye varios cientos. Elige un periférico que te resulte interesante, busca su driver en `/usr/src/sys/dev/`, ábrelo y léelo. Si tiene una tabla `ofw_compat_data` cerca del principio y un `probe()` que llama a `ofw_bus_search_compatible`, estás viendo un driver preparado para FDT. Fíjate en qué propiedades lee. Fíjate en cómo adquiere sus recursos. Fíjate en qué hace y qué no hace al hacer detach.

El lado DTS merece el mismo tratamiento. El árbol de FreeBSD guarda las descripciones de placas personalizadas en `/usr/src/sys/dts/`, los device trees derivados de Linux en `/usr/src/sys/contrib/device-tree/`, y los overlays en `/usr/src/sys/dts/arm/overlays/` y `/usr/src/sys/dts/arm64/overlays/`. Abre un archivo `.dts` que describa una placa que conozcas y léelo como leerías código: de arriba abajo, fijándote en la jerarquía, los nombres de las propiedades y los comentarios.

### Ejecuta lo que construyas

El objetivo de los laboratorios es que terminen en algo que puedas observar. Cuando cargas un módulo y no ocurre nada, eso también es información; normalmente significa que el driver no encontró coincidencia, y el capítulo te enseñará cómo averiguar por qué. El ciclo de retroalimentación es el objetivo en sí. No te saltes el paso de la carga solo porque el código haya compilado.

### Escribe los laboratorios a mano

El driver `edled` es pequeño a propósito. Escribirlo tú mismo te frena lo suficiente para fijarte en qué hace cada línea. La memoria muscular de escribir el código repetitivo de FDT merece la pena. Hacer copy-paste te priva de eso; resiste la tentación aunque estés seguro de que podrías reproducir el archivo de memoria.

### Sigue los nodos

Cuando leas un archivo de Device Tree o un log de arranque de `dmesg` y no reconozcas una propiedad, síguele la pista. Busca la documentación de binding del nodo en `/usr/src/sys/contrib/device-tree/Bindings/` si existe alguna. Busca el nombre de la propiedad en el código fuente de FreeBSD para ver qué drivers se interesan por ella. El trabajo embebido está lleno de pequeñas convenciones, y cada una se vuelve obvia en cuanto la has visto usada en código real.

### Trata `dmesg` como parte del texto

Casi todo lo interesante sobre el descubrimiento FDT aparece en `dmesg`, no en el shell. Cuando un driver se engancha, cuando un nodo se omite porque su estado es disabled, cuando simplebus informa de un hijo sin driver que coincida, esos mensajes los encuentras en el log del kernel y en ningún otro lugar. Mantén `dmesg -a` o `tail -f /var/log/messages` en una segunda terminal durante los laboratorios. Copia las líneas relevantes en tu cuaderno cuando enseñen algo no obvio.

### Rompe las cosas a propósito

Algunas de las lecciones más útiles de este capítulo vienen de observar cómo un driver *falla* al intentar engancharse. Una errata en una cadena compatible, un número de pin incorrecto, un estado disabled o un overlay que falta producirán cada uno un tipo diferente de silencio en `dmesg`. Trabajar con esos fallos en un entorno de laboratorio te enseña el reconocimiento que necesitarás cuando el mismo fallo te sorprenda en trabajo real. No construyas solo drivers que funcionen; rompe unos cuantos a propósito y observa qué dice el kernel.

### Trabaja en parejas cuando puedas

La depuración en sistemas embebidos, como el trabajo en seguridad, se beneficia de un segundo par de ojos. Si tienes un compañero de estudio, uno puede leer el `.dts` mientras el otro lee el driver; podéis intercambiar perspectivas y comparar lo que cada uno creía que estaba haciendo la configuración. La conversación tiende a atrapar los pequeños errores (un recuento de celdas intercambiado, un phandle mal leído, una celda de interrupción en la posición incorrecta) que se escapan a un solo lector.

### Confía en la iteración

No vas a memorizar cada propiedad, cada indicador, cada helper en la primera pasada. Eso está bien. Lo que necesitas en la primera pasada es la *forma* del tema: los nombres de los primitivos, la estructura de un driver que los usa, los lugares donde buscar cuando surge una pregunta concreta. Los identificadores se convierten en reflejo después de haber escrito uno o dos drivers FDT; no son un ejercicio de memorización.

### Tómate descansos

El trabajo embebido, como el trabajo en seguridad, tiene una gran densidad cognitiva. Te pide que mantengas en la cabeza una descripción del hardware físico mientras lees el software que intenta describirlo y controlarlo. Dos horas de trabajo concentrado, un descanso como es debido y otra hora concentrada es casi siempre más productivo que cuatro horas seguidas sin parar.

Con esos hábitos en mente, empecemos con la pregunta general: ¿qué es FreeBSD embebido y qué le exige a un autor de drivers que no le exige FreeBSD en un escritorio?

## Sección 1: Introducción a los sistemas FreeBSD embebidos

La palabra *embebido* se ha usado tan vagamente a lo largo de los años que vale la pena detenerse a explicar qué queremos decir con ella en este libro. Para nuestros propósitos, un sistema embebido es un ordenador diseñado para realizar una tarea específica en lugar de ser una máquina de propósito general. Una Raspberry Pi ejecutando un bucle de control de termostato es un sistema embebido. Una BeagleBone ejecutando un controlador CNC es un sistema embebido. Una pequeña caja ARM con un appliance de cortafuegos, una placa de desarrollo RISC-V con una pasarela de sensores dedicada, un router construido alrededor de un SoC MIPS en los viejos tiempos: todos ellos son sistemas embebidos. Un portátil no es embebido aunque sea pequeño. Un servidor no es embebido aunque esté muy simplificado. La palabra tiene que ver con el propósito y las limitaciones, no con el tamaño.

Desde la perspectiva de un autor de drivers, los sistemas embebidos comparten una serie de características prácticas que determinan el trabajo. El hardware suele ser un SoC con muchos periféricos integrados en lugar de una placa base con tarjetas enchufables. Los periféricos son fijos: no se pueden añadir ni quitar, porque forman literalmente parte del silicio. Por lo general no existe un bus descubrible en el sentido PCI; los periféricos residen en direcciones físicas conocidas y hay que indicarle al kernel dónde están. El consumo de energía es limitado, a veces de forma severa. La RAM es limitada. El almacenamiento es limitado. El flujo de arranque es simple, y a menudo depende de un bootloader como U-Boot o una pequeña implementación EFI. La interfaz de usuario, si la hay, es mínima. El kernel arranca, los drivers se enganchan, la única aplicación para la que existe la máquina arranca y esa es la vida del sistema.

FreeBSD funciona en una familia cada vez más amplia de arquitecturas aptas para sistemas embebidos. La mayor parte de este capítulo asume arm64, porque es el objetivo embebido más ampliamente utilizado para FreeBSD hoy en día, pero la mayor parte de lo que aprendes se aplica directamente a ARM de 32 bits, RISC-V y, en menor medida, a las plataformas MIPS y PowerPC más antiguas. Las diferencias entre estas arquitecturas importan para el compilador y el código de más bajo nivel del kernel, pero para la escritura de drivers las diferencias son prácticamente inexistentes. El mismo framework FDT, los mismos helpers `ofw_bus`, el mismo enumerador simplebus y las mismas APIs de GPIO y reloj funcionan en todos ellos. Un driver que escribas hoy para una Raspberry Pi, con muy pocas modificaciones, compilará y funcionará mañana en una RISC-V SiFive HiFive Unmatched.

### Qué aspecto tiene FreeBSD embebido

Imagina una Raspberry Pi 4 ejecutando FreeBSD 14.3. La placa tiene un CPU ARM Cortex-A72 de cuatro núcleos, 4 GB de RAM, un complejo raíz PCIe con un controlador Ethernet de gigabit y un controlador de host USB conectados a él, una ranura para tarjeta SD para el almacenamiento, una GPU Broadcom VideoCore VI, una salida HDMI, cuatro puertos USB, un conector GPIO de 40 pines y UARTs integrados, buses SPI, buses I2C, moduladores de ancho de pulso y temporizadores. La mayoría de esos periféricos residen dentro del SoC BCM2711 en direcciones mapeadas en memoria fijas. Unos pocos, como el USB y el Ethernet, cuelgan del controlador PCIe interno del SoC. La tarjeta SD la controla un controlador de host dentro del SoC que habla el protocolo SD.

Cuando enciendes la placa, un pequeño firmware en el núcleo GPU lee la tarjeta SD, encuentra el bootloader EFI de FreeBSD y le cede el control. El cargador EFI lee `/boot/loader.conf`, carga el kernel de FreeBSD, carga el blob de Device Tree que describe el hardware, carga cualquier blob de overlay que ajuste la descripción y salta al kernel. El kernel arranca, engancha un simplebus al nodo raíz del árbol, recorre los hijos del árbol, hace coincidir drivers con nodos, engancha los drivers con su hardware y el sistema arranca. Para cuando ves el prompt de inicio de sesión, tu sistema de archivos ya ha sido montado desde la tarjeta SD por el driver del host SD, tu interfaz de red ha sido levantada por el driver Ethernet, tus dispositivos USB han sido sondeados y están disponibles, y las herramientas habituales de userland (`ps`, `dmesg`, `sysctl`, `kldload`) se comportan exactamente igual que en un portátil.

Nada de ese último párrafo habría sido cierto para un PC. En un PC, el firmware es BIOS o UEFI, los periféricos están en PCI y el kernel no necesita un blob separado que describa el hardware porque el propio bus PCI describe a sus hijos. El mundo arm64 no tiene ninguno de esos lujos. Tiene Device Tree en su lugar.

### Por qué las plataformas embebidas dependen de los Device Trees

El mundo embebido eligió los Device Trees por su espacio de problema. Un pequeño SoC tiene decenas de periféricos integrados. Cada variante de SoC, cada revisión de placa, cada decisión de integración que tomó el ingeniero de hardware determina qué periféricos están habilitados, qué pines utilizan, cómo están conectados sus relojes y cuáles son sus prioridades de interrupción. Existen miles de SoCs distintos en el mundo real, y decenas de miles de placas distintas. Un único binario del kernel no puede permitirse tener compiladas todas esas variaciones; tampoco existe ningún protocolo de bus mágico que le permita *preguntarle* al hardware qué hay en la placa: el hardware no lo sabe y la mayor parte de él no puede responder.

La solución antigua, la que el mundo Linux embebido utilizaba antes de los Device Trees, se llamaba *board files* (archivos de descripción de placa). Cada placa tenía un archivo fuente en C compilado dentro del kernel, repleto de estructuras estáticas que describían los periféricos, sus direcciones, sus interrupciones y sus relojes. Un kernel destinado a cinco placas tenía cinco board files. Un kernel destinado a cincuenta tenía cincuenta, cada uno con su propia descripción estática, y cada uno requería recompilar el kernel cada vez que cambiaba el hardware de la placa. El enfoque no escalaba. Cada revisión de placa era una nueva versión del kernel.

Los Device Trees son el enfoque que reemplazó a los board files. La idea central es elegante: en lugar de codificar la descripción del hardware en C dentro del kernel, se codifica en un archivo de texto independiente que vive fuera del kernel, se compila ese archivo en un blob binario compacto y se deja que el bootloader entregue ese blob al kernel durante el arranque. El kernel lee entonces el blob, decide qué drivers conectar y le entrega a cada driver la parte del blob que describe su hardware. Un único binario del kernel puede ejecutarse ahora en cualquier placa cuyo DTB se le proporcione. Una revisión de placa que cambia la asignación de pines o añade un periférico necesita un nuevo DTB, no un nuevo kernel.

El enfoque tiene su origen en el mundo PowerPC de IBM y Apple de los años noventa, donde el concepto se llamaba *Open Firmware* y el árbol formaba parte del propio firmware. El formato moderno Flattened Device Tree (FDT) es un descendiente, simplificado y remodelado para adaptarse a los bootloaders y kernels que no incorporan un intérprete Forth completo como hacía Open Firmware. En FreeBSD encontrarás ambos nombres. El framework se llama `fdt(4)`, los helpers todavía residen bajo `sys/dev/ofw/` a causa de esa herencia de OpenFirmware, y las funciones que leen propiedades se llaman `OF_getprop`, `OF_child` y `OF_hasprop` por la misma razón. Cuando leas *Open Firmware*, *OFW* y *FDT* en el código fuente de FreeBSD, todos hacen referencia a piezas de la misma tradición.

### Visión general de las arquitecturas: FreeBSD en ARM, RISC-V y MIPS

FreeBSD admite varias arquitecturas orientadas a sistemas embebidos en FreeBSD 14.3. Las dos más utilizadas actualmente son arm64 (la arquitectura ARM de 64 bits denominada AArch64 en la documentación de ARM) y armv7 (ARM de 32 bits con punto flotante por hardware, que a veces se escribe `armv7` o `armhf`). El soporte de RISC-V ha madurado a lo largo de FreeBSD 13 y 14, y ya funciona en placas reales como la SiFive HiFive Unmatched y la VisionFive 2. El soporte de MIPS existió durante mucho tiempo y fue la base de muchos routers antiguos y dispositivos embebidos; se ha eliminado del sistema base en versiones recientes, por lo que este capítulo no se detendrá en él, pero las habilidades que adquieras con ARM y RISC-V se trasladarían directamente si alguna vez necesitases volver a una plataforma MIPS heredada.

Todas esas arquitecturas utilizan Device Tree para describir hardware no descubrible. Los flujos de arranque difieren en los detalles, pero la estructura es la misma: un firmware de etapa cero inicia la CPU, un bootloader de etapa uno (U-Boot es habitual; en arm64 un loader EFI es cada vez más estándar) carga el kernel y el DTB, y el kernel toma el control con el árbol en mano. En arm64, el loader EFI de FreeBSD gestiona esto de forma limpia, y el árbol puede provenir de un archivo en disco (`/boot/dtb/*.dtb`) o, en hardware de clase servidor, del propio firmware. En las placas armv7, U-Boot suele proporcionar el DTB. En RISC-V, el panorama es una mezcla de OpenSBI, U-Boot y EFI, según la placa.

Ninguna de estas diferencias importa demasiado al autor de un driver. El árbol que ve el kernel cuando se ejecuta tu driver es un Device Tree, las funciones auxiliares que te proporciona son `OF_*` y `ofw_bus_*`, y los drivers que escribas para trabajar con él son portables entre arquitecturas que usan el mismo framework.

### Limitaciones habituales: sin ACPI, buses limitados y restricciones de alimentación

Vale la pena enumerar las limitaciones de las plataformas embebidas que condicionan el trabajo con drivers, para que no te tomen por sorpresa.

**Sin ACPI, por lo general.** ACPI es la interfaz entre firmware y sistema operativo que usan la mayoría de los PCs para describir su hardware no descubrible. Incluye tablas, un lenguaje de bytecode llamado AML y una extensa especificación. Los servidores ARM a veces usan ACPI, y FreeBSD admite esa vía en arm64 con el subsistema ACPI, pero las placas embebidas de gama baja y media casi siempre usan FDT. Algunos sistemas arm64 de alta gama pueden incluir descripciones *tanto* de ACPI como de FDT y dejar que el firmware elija entre ellas; FreeBSD puede manejar ambas, y en arm64 existe un mecanismo de selección en tiempo de ejecución que decide qué bus conectar. La consecuencia práctica para la mayoría de los drivers embebidos es que escribes para FDT y no te preocupas por ACPI. Volveremos al caso de soporte dual en la Sección 3.

**Sin descubrimiento de estilo PCI para periféricos integrados en el SoC.** Un dispositivo PCI o PCIe se anuncia mediante identificadores de fabricante y dispositivo en un espacio de configuración estandarizado. El kernel escanea el bus, encuentra el dispositivo y lo despacha a un driver que reclama esos identificadores. Los periféricos integrados en el SoC de un chip ARM no tienen ese mecanismo de anuncio. La única forma de que el kernel sepa que hay un UART en la dirección `0x7E201000` de una Raspberry Pi 4 es porque el DTB lo indica. Esto cambia el modelo mental del autor del driver: no esperas a que el bus te entregue un dispositivo detectado; esperas a que simplebus (o el equivalente) encuentre tu nodo en el árbol y despache tu probe con el contexto de ese nodo.

**Las restricciones de alimentación importan.** Una placa embebida puede funcionar con batería, con un pequeño adaptador USB o con alimentación Power-over-Ethernet. Un driver que deja un reloj en marcha cuando el dispositivo está inactivo, o que mantiene un regulador habilitado más allá de las necesidades del periférico, perjudica a todo el sistema. FreeBSD proporciona frameworks de reloj y regulador precisamente para que los drivers puedan apagar los recursos cuando corresponda. Los trataremos en las Secciones 3, 4 y 7.

**RAM y almacenamiento limitados.** Las placas embebidas suelen tener entre 256 MB y 8 GB de RAM, y entre unos pocos cientos de megabytes y varias decenas de gigabytes de almacenamiento. Un driver que realiza asignaciones de memoria excesivas o que imprime en la consola una pantalla completa de salida de depuración en cada evento consumirá recursos que el sistema puede no tener. Escribe pensando en las limitaciones que esperas encontrar.

**Un flujo de arranque más sencillo, para bien o para mal.** El proceso de arranque en una placa embebida es generalmente más breve y menos tolerante a errores que en un PC. Si el kernel no encuentra el sistema de archivos raíz, puede que no dispongas de un entorno de rescate a mano. Si el DTB es incorrecto y el controlador de interrupciones adecuado no se conecta, el sistema se colgará en silencio. Esta es la principal razón por la que el trabajo con sistemas embebidos se beneficia de un cable de depuración funcional, una consola serie y el hábito de hacer cambios pequeños y comprobables en lugar de cambios heroicos.

### Flujo de arranque: del firmware a la conexión del driver

Para ilustrar concretamente cómo encajan todas estas piezas, recorramos lo que ocurre desde el encendido hasta la primera conexión de un driver en una placa arm64 representativa. Los detalles varían según la placa, pero la estructura es consistente.

1. Encendido: el primer núcleo de CPU comienza a ejecutar código desde una boot ROM grabada en el SoC. Esta ROM está fuera de tu control; su función es cargar la siguiente etapa.
2. La boot ROM lee un loader de etapa uno desde una ubicación fija (normalmente la tarjeta SD, la eMMC o la flash SPI). En las placas Raspberry Pi es el firmware VideoCore; en las placas arm64 genéricas suele ser U-Boot.
3. El loader de etapa uno realiza la inicialización de la plataforma: configura la DRAM, establece los relojes iniciales, inicializa un UART para la salida de depuración y carga la siguiente etapa. En las placas con FreeBSD instalado, esa siguiente etapa suele ser el loader EFI de FreeBSD (`loader.efi`).
4. El loader EFI de FreeBSD lee su configuración desde la partición ESP del medio de arranque, consulta `/boot/loader.conf` y carga tres cosas: el propio kernel, un DTB de `/boot/dtb/`, y los overlays listados en el tunable `fdt_overlays` de `/boot/dtb/overlays/`.
5. El loader transfiere el control al kernel junto con punteros al DTB cargado.
6. El kernel arranca. Su código de inicio dependiente de la máquina analiza el DTB para encontrar el mapa de memoria, la topología de CPU y el nodo raíz del árbol. Basándose en la cadena `compatible` de nivel superior del nodo raíz, arm64 decide si seguir la vía FDT o la vía ACPI.
7. En la vía FDT, el kernel conecta un `ofwbus` a la raíz del árbol y un `simplebus` al nodo `/soc` (o el equivalente en esa placa). Simplebus recorre sus hijos y crea un `device_t` para cada nodo con una cadena compatible válida.
8. Para cada uno de esos `device_t`, el kernel ejecuta el bucle de probe habitual de newbus. Cada driver que se ha registrado con `DRIVER_MODULE(mydrv, simplebus, ...)` tiene la oportunidad de hacer el probe. El driver cuyo `probe()` devuelve la mejor coincidencia gana y se invoca su `attach()`. El driver lee entonces las propiedades, asigna recursos y se inicializa.
9. El proceso de conexión se repite recursivamente para los buses anidados (un hijo `simple-bus` de `/soc`, un controlador I2C con sus propios dispositivos hijos, un banco GPIO con sus propios consumidores de pines), produciendo el árbol de dispositivos completo visible en `dmesg` y `devinfo -r`.
10. Para cuando se ejecuta init, todos los dispositivos que el sistema necesita para arrancar (UART, host SD, Ethernet, USB, GPIO) se han conectado, y el userland arranca.

Esa secuencia es el telón de fondo en el que opera cada driver de este capítulo. Cuando escribes un driver consciente de FDT, estás escribiendo el código que se ejecuta en el paso 8 para tu nodo concreto. El mecanismo circundante funciona con o sin ti; la parte que controlas es la lectura de tu nodo y la asignación de tus recursos.

### Cómo verlo por ti mismo

La forma más rápida de ver un arranque real basado en FDT es ejecutar FreeBSD 14.3 en una Raspberry Pi 3 o 4. Las imágenes están disponibles en el proyecto FreeBSD en el área de descarga estándar de arm64, y la configuración está bien documentada. Si no dispones del hardware, la segunda opción más rápida es usar la plataforma `virt` de QEMU, que emula una máquina arm64 genérica con un pequeño conjunto de periféricos descritos mediante FDT. El kernel y el loader de una versión normal de FreeBSD arm64 funcionan dentro de ella. Un ejemplo de invocación de QEMU se encuentra en las notas de laboratorio más adelante en este capítulo.

Una tercera opción, útil incluso en una estación de trabajo amd64, es leer archivos `.dts` y los drivers de FreeBSD que los consumen. Abre `/usr/src/sys/contrib/device-tree/src/arm/bcm2835-rpi-b.dts` o `/usr/src/sys/contrib/device-tree/src/arm64/broadcom/bcm2711-rpi-4-b.dts`. Sigue la estructura de nodos de arriba hacia abajo. Observa cómo el nodo de nivel superior tiene una propiedad `compatible` que nombra la placa. Observa cómo los nodos hijos describen las CPUs, la memoria, el controlador de reloj, el controlador de interrupciones y el bus de periféricos. Luego abre `/usr/src/sys/arm/broadcom/bcm2835/` en el árbol de FreeBSD y examina los drivers que consumen esos nodos. Empezarás a ver cómo se relacionan las descripciones y el código.

### Cerrando esta sección

La Sección 1 ha establecido el escenario. FreeBSD embebido no es un nicho exótico dentro del proyecto; es el proyecto en plataformas que no se parecen a los PCs. La arquitectura hacia la que te empuja el trabajo embebido, pensar en el hardware como un conjunto de periféricos fijos en un SoC en lugar de dispositivos descubribles en un bus, es exactamente la que admite Device Tree. Device Tree es el puente entre la descripción de hardware que conoce el diseñador de la placa y el código del driver que ejecuta el kernel. El resto de este capítulo consiste en aprender ese puente lo suficientemente bien como para cruzarlo con confianza.

En la siguiente sección nos detendremos a leer los propios archivos Device Tree. Antes de poder escribir un driver que los consuma, necesitamos saber cómo están estructurados, qué significan sus propiedades y cómo un archivo fuente `.dts` se convierte en el binario `.dtb` que el kernel realmente ve.

## Sección 2: ¿Qué es el Device Tree?

Un Device Tree es una descripción textual del hardware organizada como un árbol de nodos, donde cada nodo representa un dispositivo o un bus, y cada nodo lleva propiedades con nombre que describen sus direcciones, sus interrupciones, sus relojes, sus relaciones con otros nodos y cualquier otra cosa que un driver pueda necesitar saber. Esa descripción es lo que el mundo embebido utiliza en lugar de la enumeración PCI o las tablas ACPI. En el mundo de FreeBSD embebido pasarás gran parte de tu tiempo leyendo, escribiendo y razonando sobre estos archivos. Cuanto antes te resulten familiares, más fácil te parecerá cada capítulo posterior.

La mejor forma de empezar es ver uno.

### Un Device Tree mínimo

Este es el archivo fuente Device Tree más pequeño que resulta interesante, en formato `.dts`:

```dts
/dts-v1/;

/ {
    compatible = "acme,trivial-board";
    #address-cells = <1>;
    #size-cells = <1>;

    chosen {
        bootargs = "console=ttyS0,115200";
    };

    memory@80000000 {
        device_type = "memory";
        reg = <0x80000000 0x10000000>;
    };

    uart0: serial@10000000 {
        compatible = "ns16550a";
        reg = <0x10000000 0x100>;
        interrupts = <5>;
        clock-frequency = <24000000>;
        status = "okay";
    };
};
```

Es un Device Tree completo y válido, aunque diminuto. Describe una placa ficticia llamada `acme,trivial-board` que tiene 256 MB de RAM en la dirección física `0x80000000`, y un UART compatible con 16550 en `0x10000000` que entrega la interrupción número 5 y funciona a 24 MHz. Aunque la sintaxis no te resulte familiar todavía, la intención es legible: el archivo es una descripción de hardware escrita en un formato compacto específico del dominio.

Analicémoslo parte por parte.

### La estructura del árbol

La primera línea, `/dts-v1/;`, es una directiva obligatoria que declara la versión de la sintaxis DTS. Todos los archivos DTS de FreeBSD que alguna vez escribas o leas comienzan con ella. Todo lo que aparezca antes de esta línea convierte el archivo en un DTS no válido; trátala como `#!/bin/sh` al principio de un script de shell.

El resto del archivo está encerrado en un bloque único que comienza con `/ {` y termina con `};`. Ese bloque exterior es el **nodo raíz** del árbol. Todo Device Tree tiene exactamente un nodo raíz. Sus hijos son periféricos y sub-buses; los hijos de estos son más periféricos y sub-buses, y así sucesivamente.

Los nodos se identifican por **nombres**. Un nombre como `serial@10000000` consta de un nombre base (`serial`) y una **dirección de unidad** (`10000000`, que es la dirección inicial de la primera región de registros del nodo escrita en hexadecimal sin el prefijo `0x`). La dirección de unidad es una convención, no un requisito estricto; existe para que los nodos con el mismo nombre base puedan distinguirse (puedes tener varios nodos `serial` en distintas direcciones), y además sirve como una pista legible sobre lo que describe el nodo.

Una **etiqueta** (label), como `uart0:` en `uart0: serial@10000000`, es un identificador que permite a otras partes del árbol referirse a este nodo. Las etiquetas te permiten escribir `&uart0` en cualquier otra parte del archivo para indicar *el nodo con etiqueta `uart0`*. Utilizaremos etiquetas en la sección sobre overlays.

Los nodos contienen **propiedades**. Una propiedad es un nombre seguido de un signo igual, seguido de un valor, seguido de un punto y coma:

```dts
compatible = "ns16550a";
```

Algunas propiedades no tienen valor; existen únicamente como indicadores:

```dts
interrupt-controller;
```

El valor de una propiedad puede ser una cadena de texto (`"ns16550a"`), una lista de cadenas (`"brcm,bcm2711", "brcm,bcm2838"`), una lista de enteros entre corchetes angulares (`<0x10000000 0x100>`), una lista de referencias a otros nodos (`<&gpio0>`) o una cadena de bytes en formato binario (`[01 02 03]`). La mayoría de las propiedades habituales son cadenas de texto, listas de cadenas o listas de enteros. El árbol utiliza *celdas* de 32 bits como unidad entera fundamental; los enteros dentro de `<...>` son celdas, y los valores de 64 bits se expresan como dos celdas consecutivas (la parte alta y la parte baja).

Volvamos al ejemplo mínimo y leamos cada propiedad con ojos nuevos.

### Leyendo el ejemplo mínimo

El nodo raíz tiene tres propiedades:

```dts
compatible = "acme,trivial-board";
#address-cells = <1>;
#size-cells = <1>;
```

La propiedad **`compatible`** es la forma en que el nodo raíz (y cualquier nodo) le indica al sistema qué es. Es la propiedad más importante de Device Tree. Un driver se empareja con ella. El valor es una cadena con prefijo de fabricante (`"acme,trivial-board"`) o, más habitualmente, una lista de ellas en orden decreciente de especificidad. El nodo raíz de una Raspberry Pi 4, por ejemplo, podría ser `compatible = "raspberrypi,4-model-b", "brcm,bcm2711";`. La primera cadena dice «exactamente esta placa»; la segunda dice «en la familia general de placas que usan el chip BCM2711». Los drivers que conocen la placa específica pueden emparejarse con la primera; los drivers que solo conocen el chip pueden emparejarse con la segunda. La especificación DTS lo denomina *lista de compatibilidad*, y tanto FreeBSD como Linux la respetan.

Las propiedades **`#address-cells`** y **`#size-cells`** en el nodo raíz describen cuántas celdas de 32 bits usa un nodo hijo para su dirección y tamaño en las propiedades `reg`. En el nodo raíz de una placa de 32 bits, ambas suelen valer 1. En una placa de 64 bits con más de 4 GB de memoria direccionable, ambas serían 2. Cuando ves `reg = <0x80000000 0x10000000>;` bajo el nodo de memoria, sabes por el recuento de celdas del padre que hay una celda de dirección y una celda de tamaño, lo que significa que la región está en `0x80000000` con tamaño `0x10000000`. Si `#address-cells` fuera 2, la región se escribiría como `reg = <0x0 0x80000000 0x0 0x10000000>;`.

El nodo **`chosen`** es un hermano especial de los hijos hardware del nodo raíz. Contiene los parámetros que el bootloader quiere pasar al kernel: los argumentos de arranque, el dispositivo de consola y, a veces, la ubicación del initrd. FreeBSD lee `/chosen/bootargs` y lo usa para poblar el entorno del kernel.

El nodo **`memory@80000000`** describe una región de memoria física. Los nodos de memoria llevan `device_type = "memory"` y una propiedad `reg` que indica su rango. El arranque temprano de FreeBSD los lee para construir su mapa de memoria física.

El nodo **`serial@10000000`** es el más interesante. Su `compatible = "ns16550a"` le indica al kernel que *esto es un UART ns16550a*, un chip de puerto serie muy común del estilo PC. Su `reg = <0x10000000 0x100>` dice *mis registros residen en la dirección física `0x10000000` y ocupan `0x100` bytes*. Su `interrupts = <5>` dice *entrego la interrupción número 5 a mi padre de interrupciones*. Su `clock-frequency = <24000000>` dice *mi reloj de referencia funciona a 24 MHz, por lo que los divisores deben calcularse a partir de este valor*. Su `status = "okay"` dice *estoy habilitado*; si dijera `"disabled"`, el driver saltaría este nodo.

Eso es, en esencia, lo que hace un Device Tree: describe cada periférico mediante unas pocas propiedades cuyos significados están definidos por convenciones denominadas **bindings**. Un binding para un UART te indica qué propiedades debe tener un nodo UART. Un binding para un controlador I2C te indica qué propiedades debe tener un nodo de controlador I2C. Y así sucesivamente. Los bindings se documentan por separado, y el árbol de FreeBSD incluye una extensa biblioteca de ellos bajo `/usr/src/sys/contrib/device-tree/Bindings/`.

### Fuente frente a binario: .dts, .dtsi y .dtb

Los archivos de Device Tree se presentan en tres tipos que al principio es fácil confundir.

**`.dts`** es la forma fuente principal. Un archivo `.dts` describe una placa o plataforma completa y es lo que se le pasa al compilador.

**`.dtsi`** es un fragmento de inclusión; la `i` corresponde a *include*. Una familia de SoC típica tiene un archivo `.dtsi` extenso que describe el propio SoC (su controlador de interrupciones, su árbol de relojes, sus periféricos en chip), y cada placa que usa ese SoC tiene un archivo `.dts` pequeño que incluye el `.dtsi` con `#include` y luego describe las adiciones específicas de la placa (dispositivos externos soldados en la placa, configuración de pines, nodo chosen). Encontrarás muchos archivos `.dtsi` bajo `/usr/src/sys/contrib/device-tree/src/arm/` y `/usr/src/sys/contrib/device-tree/src/arm64/`.

**`.dtb`** es la forma binaria compilada. El kernel y el bootloader trabajan con archivos `.dtb`, no con archivos `.dts`. Un `.dtb` es la salida del compilador `dtc` cuando se le proporciona una fuente `.dts`. Es compacto, no tiene espacios en blanco ni comentarios, y está diseñado para ser analizado por un bootloader con unos pocos kilobytes de código. Un archivo `.dtb` para una Raspberry Pi 4 tiene normalmente unos 30 KB.

Y un cuarto tipo, menos habitual:

**`.dtbo`** es un *overlay* compilado. Los overlays son fragmentos que modifican un `.dtb` base existente en el momento de la carga: pueden habilitar o deshabilitar nodos, añadir nuevos nodos o cambiar propiedades. Son el mecanismo que FreeBSD y muchas distribuciones de Linux usan para que los usuarios puedan personalizar un DTB estándar sin reconstruirlo. Los archivos `.dtbo` se compilan a partir de archivos `.dtso` (device-tree-source-overlay) y los carga el bootloader mediante el parámetro `fdt_overlays`. Los veremos en la sección 5.

Cuando trabajas con DTS, casi siempre estás escribiendo un archivo `.dts` o `.dtso`. Cualquiera de los dos se compila con el compilador de Device Tree, `dtc`, que en FreeBSD reside en el port `devel/dtc` y se instala como `/usr/local/bin/dtc`. El sistema de construcción del kernel de FreeBSD invoca `dtc` a través de los scripts bajo `/usr/src/sys/tools/fdt/`, concretamente `make_dtb.sh` y `make_dtbo.sh`.

### Nodos, propiedades, direcciones y el phandle

Hay algunos conceptos adicionales que aparecen con tanta frecuencia que merece la pena definirlos de una vez por todas.

Los **nodos** son las unidades jerárquicas. Cada nodo tiene un nombre, opcionalmente una etiqueta, posiblemente una dirección de unidad y cero o más propiedades. Los nodos se anidan; los hijos de un nodo se describen dentro de sus llaves.

Las **propiedades** son pares clave-valor. Las claves son cadenas. Los valores tienen tipo por convención: `compatible` es una lista de cadenas, `reg` es una lista de enteros, `status` es una cadena, `interrupts` es una lista de enteros cuya longitud depende del padre de interrupciones, y así sucesivamente.

Las **direcciones de unidad** en los nombres de nodo (`serial@10000000`) reflejan la primera celda de la propiedad `reg`. El compilador DTC advierte si no coinciden. Debes escribir ambas de forma coherente.

La propiedad **`reg`** describe las regiones de registros mapeados en memoria del dispositivo. Su formato es `<address size address size ...>`, siendo cada par dirección-tamaño una región contigua. La mayoría de los periféricos simples tienen exactamente una región. Algunos tienen varias (un periférico con un área de registros principal y un bloque de registros de interrupciones separado, por ejemplo).

Las **celdas de dirección y de tamaño** son el par de propiedades `#address-cells` y `#size-cells` que residen en un nodo padre y describen el formato de `reg` en sus hijos. Un bus de SoC con `#address-cells = <1>; #size-cells = <1>;` permite a sus hijos usar una celda para la dirección y otra para el tamaño. Un bus I2C suele tener `#address-cells = <1>; #size-cells = <0>;` porque un nodo hijo de I2C tiene una dirección pero no tiene tamaño.

Las **interrupciones** se describen mediante una o más propiedades según el estilo utilizado. El estilo más antiguo es `interrupts = <...>;` con las celdas interpretadas según la convención del padre de interrupciones. En plataformas ARM basadas en GIC, esto adopta una forma de tres celdas: tipo de interrupción, número de interrupción y flags de interrupción. El estilo más reciente, mezclado con el antiguo a lo largo del kernel, es `interrupts-extended = <&gic 0 15 4>;`, que nombra explícitamente al padre de interrupciones. En cualquier caso, las celdas le indican al kernel qué interrupción de hardware genera el dispositivo y bajo qué condiciones.

Un **phandle** es un entero único que el compilador asigna a cada nodo. Los phandles permiten que otros nodos hagan referencia a este. Cuando escribes `<&gpio0>`, el compilador sustituye el phandle del nodo etiquetado como `gpio0`. Cuando escribes `<&gpio0 17 0>`, estás pasando tres celdas: el phandle de `gpio0`, el número de pin `17` y una celda de flags `0`. El significado de las celdas que siguen al phandle lo define el binding del *proveedor*. Este es el patrón mediante el cual los consumidores de GPIO, de relojes, de resets y de interrupciones se comunican con sus proveedores: la primera celda nombra al proveedor y las celdas siguientes indican qué recurso y cómo.

La propiedad **`status`** es pequeña pero crítica. Un nodo con `status = "okay";` está habilitado y los drivers lo explorarán. Un nodo con `status = "disabled";` se omite. Los overlays suelen cambiar esta propiedad para activar o desactivar un periférico sin eliminar el nodo. La función `ofw_bus_status_okay()` de FreeBSD es la función auxiliar que devuelve true cuando el estado de un nodo es okay.

Los mecanismos de **`label`** y **`alias`** te permiten referirte a un nodo por un nombre corto en lugar de por una ruta. Una etiqueta como `uart0:` es un identificador local al archivo; un alias definido bajo el nodo especial `/aliases` (`serial0 = &uart0;`) es un nombre visible para el kernel. FreeBSD hace uso de los aliases para algunos dispositivos, como la consola.

Eso es la mayor parte de lo que necesitas para leer un Device Tree típico. Algunos elementos más especializados aparecen en bindings específicos (por ejemplo, `clock-names` y `clocks` para consumidores del framework de relojes, `reset-names` y `resets` para consumidores de hwreset, `dma-names` y `dmas` para consumidores de motor DMA, `pinctrl-0` y `pinctrl-names` para el control de pines), pero todos siguen el mismo patrón de *lista indexada por nombre*.

### Un ejemplo más realista

Para darte una idea de un fragmento real a nivel de SoC, aquí tienes un nodo abreviado de la descripción de un BCM2711 (Raspberry Pi 4). El archivo completo se encuentra bajo `/usr/src/sys/contrib/device-tree/src/arm/bcm2711.dtsi`.

```dts
soc {
    compatible = "simple-bus";
    #address-cells = <1>;
    #size-cells = <1>;
    ranges = <0x7e000000 0x0 0xfe000000 0x01800000>,
             <0x7c000000 0x0 0xfc000000 0x02000000>,
             <0x40000000 0x0 0xff800000 0x00800000>;
    dma-ranges = <0xc0000000 0x0 0x00000000 0x40000000>;

    gpio: gpio@7e200000 {
        compatible = "brcm,bcm2711-gpio", "brcm,bcm2835-gpio";
        reg = <0x7e200000 0xb4>;
        interrupts = <GIC_SPI 113 IRQ_TYPE_LEVEL_HIGH>,
                     <GIC_SPI 114 IRQ_TYPE_LEVEL_HIGH>;
        gpio-controller;
        #gpio-cells = <2>;
        interrupt-controller;
        #interrupt-cells = <2>;
    };

    spi0: spi@7e204000 {
        compatible = "brcm,bcm2835-spi";
        reg = <0x7e204000 0x200>;
        interrupts = <GIC_SPI 118 IRQ_TYPE_LEVEL_HIGH>;
        clocks = <&clocks BCM2835_CLOCK_VPU>;
        #address-cells = <1>;
        #size-cells = <0>;
        status = "disabled";
    };
};
```

Leyéndolo de arriba abajo:

- El nodo `soc` es el bus de periféricos en chip. Su `compatible = "simple-bus"` es el token mágico que le indica a FreeBSD que debe asociar el driver simplebus aquí.
- Su propiedad `ranges` define la traducción de direcciones desde las direcciones del bus (las direcciones «locales» que el interconectado de periféricos del CPU usa internamente, a partir de `0x7E000000`) a las direcciones físicas del CPU (a partir de `0xFE000000`). FreeBSD la lee y la aplica al mapear las propiedades `reg` de los nodos hijos.
- El nodo `gpio` es el controlador GPIO. Reclama dos interrupciones, se declara como gpio-controller (de modo que otros nodos puedan referenciarlo) y usa dos celdas por referencia GPIO (la primera celda es el número de pin y la segunda es una palabra de flags).
- El nodo `spi0` es el controlador SPI en la dirección de bus `0x7E204000`. Tiene `status = "disabled"` en la descripción base, lo que significa que no se asocia hasta que un overlay lo habilite.

Toda descripción de placa embebida tiene un aspecto similar: un árbol de periféricos en chip, cada uno con una cadena compatible que identifica qué driver asociar, una propiedad `reg` para sus registros mapeados en memoria, una propiedad `interrupts` para su línea IRQ y, posiblemente, referencias a relojes, resets, reguladores y pines.

### Cómo se carga el DTB

Por completitud, conviene saber cómo pasa el DTB del disco al kernel durante el arranque.

En una placa arm64 con FreeBSD 14.3, el flujo típico es el siguiente:

1. El firmware EFI o el bootloader lee el cargador EFI de FreeBSD desde la ESP.
2. El cargador de FreeBSD carga el kernel desde `/boot/kernel/kernel` y el DTB desde `/boot/dtb/<board>.dtb`. El nombre del archivo DTB se selecciona en función de la familia del SoC.
3. Si `/boot/loader.conf` establece `fdt_overlays="overlay1,overlay2"`, el cargador lee `/boot/dtb/overlays/overlay1.dtbo` y `/boot/dtb/overlays/overlay2.dtbo`, los aplica al DTB base en memoria y entrega el resultado combinado al kernel.
4. El kernel toma el DTB combinado como su descripción de hardware autoritativa.

En placas gestionadas por U-Boot (habitual en armv7), el flujo es similar, pero el cargador es el propio U-Boot. Las variables de entorno `fdt_file` y `fdt_addr` de U-Boot le indican qué DTB cargar y dónde colocarlo. Cuando U-Boot ejecuta finalmente `bootefi` o `booti`, pasa el DTB al cargador de FreeBSD o directamente al kernel.

En sistemas EFI que incluyen FDT en su firmware (poco habitual en placas pequeñas, pero común en servidores ARM que usan la Server Base System Architecture), el firmware almacena el DTB como una tabla de configuración EFI y el kernel lo lee desde allí.

Para un autor de drivers, los detalles del arranque no importan la mayor parte del tiempo. Lo que importa es que en el momento en que se llama al probe de tu driver, el árbol ya ha sido cargado, analizado y presentado al kernel; lo lees con los mismos helpers `OF_*` sin importar cómo llegó hasta allí.

### Cerrando esta sección

La sección 2 presentó Device Tree como un lenguaje de descripción de hardware. Has visto un ejemplo mínimo, has conocido los conceptos fundamentales de nodos y propiedades, sabes la diferencia entre los archivos `.dts`, `.dtsi`, `.dtb` y `.dtbo`, y has recorrido un fragmento realista de la descripción de un SoC. También sabes cómo llega un DTB al kernel durante el boot.

Lo que aún *no* sabes es cómo el kernel de FreeBSD consume el árbol una vez cargado: qué subsistema se enlaza a él, qué funciones auxiliares llaman los drivers para leer propiedades, y cómo el `probe()` y el `attach()` de un driver encuentran su nodo. De eso trata la sección 3.

## Sección 3: Soporte de Device Tree en FreeBSD

FreeBSD gestiona Device Tree a través de un framework cuyo diseño es anterior al propio FDT. El framework recibe el nombre de Open Firmware, abreviado **OFW** en todo el árbol de código fuente, porque la API de la que surgió servía originalmente a las máquinas PowerPC de Apple y a los sistemas IBM que usaban la especificación real de Open Firmware. Cuando el mundo ARM se estandarizó en torno a Flattened Device Tree a finales de la década de 2000, FreeBSD mapeó FDT sobre la misma API interna. Un driver en FreeBSD 14.3 llama, por tanto, al mismo `OF_getprop()` independientemente de si se ejecuta en un Mac PowerPC, en una placa ARM con un blob FDT o en una placa RISC-V con un blob FDT. La implementación subyacente difiere; la interfaz de nivel superior es uniforme.

Esta sección presenta las piezas de ese framework que necesitas conocer: la interfaz `fdt(4)` tal como se usa en la práctica, los helpers de `ofw_bus` que los drivers invocan por encima de los primitivos `OF_*` sin procesar, el driver de bus `simplebus(4)` que enumera los hijos, y los patrones de lectura de propiedades que usarás constantemente. Al final de la sección sabrás qué código ya existe en el lado de FreeBSD y dónde reside; la sección 4 construirá un driver sobre él.

### Visión general del framework fdt(4)

`fdt(4)` es el soporte del kernel para Flattened Device Tree. Proporciona el código que analiza el archivo binario `.dtb`, lo recorre para encontrar nodos, extrae propiedades, aplica overlays y presenta el resultado a través de la API `OF_*`. Puedes pensar en `fdt(4)` como la mitad inferior y en `ofw_bus` como la mitad superior, con las funciones `OF_*` abarcando ambas.

El código que implementa el lado FDT de la interfaz OFW reside en `/usr/src/sys/dev/ofw/ofw_fdt.c`. Es una instancia específica de la interfaz kobj `ofw_if.m`. Cuando el kernel llama a `OF_getprop()`, la llamada pasa a través de la interfaz y termina en la implementación FDT, que recorre el blob aplanado. En un Mac PowerPC terminaría en la implementación real de Open Firmware; los drivers que están por encima no necesitan saber ni preocuparse por ello.

Para tus propósitos como autor de drivers, casi nunca tocarás `ofw_fdt.c` directamente. Usarás los helpers del nivel superior.

### OF_*: Los lectores de propiedades sin procesar

La API de nivel más bajo que invocan los drivers es la familia de funciones `OF_*` declarada en `/usr/src/sys/dev/ofw/openfirm.h`. Las que más usarás son un conjunto reducido.

`OF_getprop(phandle_t node, const char *prop, void *buf, size_t len)` lee los bytes sin procesar de una propiedad en un buffer proporcionado por el llamador. Devuelve el número de bytes leídos en caso de éxito, o `-1` en caso de fallo. El buffer debe ser lo suficientemente grande para la longitud esperada.

`OF_getencprop(phandle_t node, const char *prop, pcell_t *buf, size_t len)` lee una propiedad cuyas celdas están en big-endian y las convierte al orden de bytes del host a medida que las copia. Casi cualquier propiedad que contenga enteros debería leerse con esta variante en lugar de con `OF_getprop()`.

`OF_getprop_alloc(phandle_t node, const char *prop, void **buf)` lee una propiedad de longitud desconocida. El kernel asigna un buffer y devuelve el puntero a través del tercer argumento. Cuando hayas terminado, llama a `OF_prop_free(buf)` para liberarlo.

`OF_hasprop(phandle_t node, const char *prop)` devuelve un valor distinto de cero si la propiedad con ese nombre existe, y cero en caso contrario. Es útil para propiedades opcionales en las que la mera presencia tiene significado.

`OF_child(phandle_t node)` devuelve el primer hijo de un nodo. `OF_peer(phandle_t node)` devuelve el siguiente hermano. `OF_parent(phandle_t node)` devuelve el padre. Combinadas, permiten recorrer el árbol.

`OF_finddevice(const char *path)` devuelve un phandle para el nodo en una ruta determinada, como `"/chosen"` o `"/soc/gpio@7e200000"`. La mayoría de los drivers no necesitan esto porque el framework ya les entrega su nodo.

`OF_decode_addr(phandle_t dev, int regno, bus_space_tag_t *tag, bus_space_handle_t *hp, bus_size_t *sz)` es una rutina de conveniencia que usa el código más temprano en el arranque (principalmente los drivers de consola serie) para configurar un mapeo de bus-space para el registro `regno` de un nodo dado sin pasar por newbus. Los drivers normales usan `bus_alloc_resource_any()` en su lugar, que lee la propiedad `reg` a través de la lista de recursos configurada durante el probe.

Estos primitivos son la base. En la práctica los llamarás indirectamente a través de los helpers `ofw_bus_*`, que resultan algo más convenientes, pero los anteriores son lo que esos helpers usan internamente, y merece la pena reconocerlos cuando leas código real de drivers.

### ofw_bus: Los helpers de compatibilidad

Lo más habitual que hace un driver con soporte FDT es preguntarse: *¿es este dispositivo compatible con algo que sé manejar?* FreeBSD proporciona una pequeña capa de helpers sobre `OF_getprop` para que esas comprobaciones sean idiomáticas. Están en `/usr/src/sys/dev/ofw/ofw_bus.h` y `/usr/src/sys/dev/ofw/ofw_bus_subr.h`, con sus implementaciones en `/usr/src/sys/dev/ofw/ofw_bus_subr.c`.

Los helpers que merece la pena conocer, en el orden en que los encontrarás:

`ofw_bus_get_node(device_t dev)` devuelve el phandle asociado a un `device_t`. Está implementado como un inline que llama al método `OFW_BUS_GET_NODE` del bus padre. Para un hijo de simplebus, esto devuelve el phandle del nodo DTS que dio origen a este dispositivo.

`ofw_bus_status_okay(device_t dev)` devuelve 1 si la propiedad `status` del nodo está ausente, vacía o es `"okay"`; 0 en caso contrario. Cada probe con soporte FDT debería llamarlo al principio para saltar los nodos desactivados.

`ofw_bus_is_compatible(device_t dev, const char *string)` devuelve 1 si alguna entrada de la propiedad `compatible` del nodo coincide exactamente con `string`. Es breve, preciso y la herramienta habitual cuando un driver solo necesita una cadena de compatibilidad.

`ofw_bus_search_compatible(device_t dev, const struct ofw_compat_data *table)` recorre una tabla proporcionada por el driver y devuelve la entrada coincidente si alguna de sus cadenas de compatibilidad está en la lista `compatible` del nodo. Esta es la forma estándar en que un driver que admite múltiples chips registra su compatibilidad. La tabla es un array de entradas, cada una con una cadena y una cookie `uintptr_t` que el driver puede usar para recordar qué chip coincidió; la tabla termina con una entrada centinela cuya cadena es `NULL`. Veremos el patrón completo en la sección 4.

`ofw_bus_has_prop(device_t dev, const char *prop)` es un envoltorio de conveniencia sobre `OF_hasprop(ofw_bus_get_node(dev), prop)`.

`ofw_bus_get_name(device_t dev)`, `ofw_bus_get_compat(device_t dev)`, `ofw_bus_get_type(device_t dev)` y `ofw_bus_get_model(device_t dev)` devuelven las cadenas correspondientes del nodo, o `NULL` si están ausentes.

Estos son los helpers fundamentales. Los verás al principio de las rutinas probe y attach de casi todo driver con soporte FDT.

### simplebus: El enumerador predeterminado

El driver simplebus es la pieza que hace que todo esto funcione en la práctica. Reside en `/usr/src/sys/dev/fdt/simplebus.c` y su cabecera está en `/usr/src/sys/dev/fdt/simplebus.h`. Simplebus tiene dos funciones.

Su primera función es enumerar los hijos. Cuando el kernel conecta simplebus a un nodo cuyo `compatible` incluye `"simple-bus"` (o cuyo `device_type` es `"soc"`, por razones históricas), simplebus recorre los hijos del nodo, crea un `device_t` por cada hijo que tenga una propiedad `compatible`, y los introduce en newbus. Esto es lo que hace que se llame al probe de tu driver; simplebus es el bus con el que tu driver se registra mediante `DRIVER_MODULE(mydrv, simplebus, ...)`.

Su segunda función es traducir las direcciones de los hijos a direcciones físicas de la CPU. La propiedad `ranges` del nodo padre codifica cómo las direcciones locales al bus que aparecen en las propiedades `reg` de los hijos se mapean a direcciones físicas de la CPU. Simplebus lee `ranges` en `simplebus_fill_ranges()` y lo aplica al configurar la lista de recursos de cada hijo, de modo que cuando tu driver solicita su recurso de memoria, la región ya está en el espacio físico de la CPU.

El código central de probe que decide si simplebus debe conectarse a un nodo determinado se encuentra cerca del principio de `/usr/src/sys/dev/fdt/simplebus.c`. Aquí está, sin comentarios por brevedad:

```c
if (!ofw_bus_status_okay(dev))
    return (ENXIO);

if (ofw_bus_is_compatible(dev, "syscon") ||
    ofw_bus_is_compatible(dev, "simple-mfd"))
    return (ENXIO);

if (!(ofw_bus_is_compatible(dev, "simple-bus") &&
      ofw_bus_has_prop(dev, "ranges")) &&
    (ofw_bus_get_type(dev) == NULL ||
     strcmp(ofw_bus_get_type(dev), "soc") != 0))
    return (ENXIO);

device_set_desc(dev, "Flattened device tree simple bus");
return (BUS_PROBE_GENERIC);
```

Este fragmento es un ejemplo compacto de todo el estilo de probe. Comprueba `status`, descarta las excepciones conocidas, confirma que el nodo parece un simple bus, describe el dispositivo y devuelve una confianza de probe. Todo probe con soporte FDT en el árbol sigue alguna variación de esta forma.

Simplebus se registra con dos buses padre. En la raíz ofw primaria se registra mediante `EARLY_DRIVER_MODULE(simplebus, ofwbus, ...)`; y de forma recursiva, en sí mismo, mediante `EARLY_DRIVER_MODULE(simplebus, simplebus, ...)`. La recursión es el mecanismo por el que se enumeran los nodos simple-bus anidados: un padre simplebus que encuentra un hijo cuyo compatible es `"simple-bus"` conecta otra instancia de simplebus a él, que luego enumera *sus* hijos.

Para la mayoría del trabajo con drivers no necesitas saber más de simplebus que el hecho de que existe y de que te registras con él. El registro del módulo de tu driver tendrá la forma `DRIVER_MODULE(mydrv, simplebus, mydrv_driver, 0, 0);` y todo lo que viene después ocurrirá automáticamente.

### Cómo encajan las piezas en una llamada de probe

Para unir todas las piezas, tracemos lo que ocurre desde que se carga el DTB hasta que se llama al probe de tu driver.

1. El cargador entrega al kernel un DTB.
2. El código temprano arm64 del kernel analiza el DTB para encontrar información sobre la memoria y la CPU.
3. El seudodispositivo `ofwbus0` se conecta a la raíz del árbol.
4. `ofwbus0` crea un `device_t` para el nodo `/soc` (o el nodo que tenga `compatible = "simple-bus"`) y lanza el bucle de probe habitual de newbus.
5. El probe del driver simplebus se ejecuta, devuelve `BUS_PROBE_GENERIC` y es seleccionado.
6. El attach de simplebus recorre los hijos del nodo `/soc` y crea un `device_t` para cada uno. La lista de recursos de cada hijo se rellena a partir de sus propiedades `reg` e `interrupts`, traducidas a través del `ranges` del padre.
7. Para cada hijo se ejecuta el bucle de probe de newbus. Cada driver registrado en simplebus tiene la oportunidad de hacer el probe.
8. Se llama al probe de tu driver. Llama a `ofw_bus_status_okay()`, a `ofw_bus_search_compatible()`, y (si hay coincidencia) devuelve `BUS_PROBE_DEFAULT`.
9. Si tu driver gana el concurso de probe para este nodo, se llama a su attach. En ese punto el `device_t` ya tiene una lista de recursos rellena con la información de memoria e interrupciones del nodo.
10. Tu driver llama a `bus_alloc_resource_any()` para su región de memoria y para su interrupción, configura un manejador de interrupciones si lo tiene, mapea la memoria, inicializa el hardware y devuelve 0 para indicar éxito.

Desde la perspectiva del autor de drivers, los seis primeros pasos son maquinaria; los pasos del 7 al 10 son el código que escribes tú. El capítulo se adentrará ahora en esos cuatro pasos.

### Registrar un driver con ofw_bus

Cuando tu driver se registra con simplebus, está optando implícitamente por el despacho OFW. La línea de registro del módulo es:

```c
DRIVER_MODULE(mydrv, simplebus, mydrv_driver, 0, 0);
```

Esto le indica a newbus: *conecta el driver descrito por `mydrv_driver` como hijo de simplebus*. Tu array `device_method_t` debe proporcionar como mínimo un método `device_probe` y un método `device_attach`. Si tienes un detach, añade `device_detach`. Si tu driver también implementa métodos de la interfaz OFW (lo cual raramente es necesario en el nivel hoja), añádelos también.

En algunas plataformas, y para drivers que quieren conectarse tanto a simplebus como a la raíz `ofwbus` (en caso de que no haya simplebus entre ellos y la raíz), es habitual añadir un segundo registro:

```c
DRIVER_MODULE(mydrv, ofwbus, mydrv_driver, 0, 0);
```

Esto es lo que hace `gpioled_fdt.c`, por ejemplo. Cubre las plataformas en las que el nodo `gpio-leds` está directamente bajo la raíz en lugar de bajo un `simple-bus`.

### Escribir una tabla de compatibilidad

Un driver que admite más de una variante de un chip normalmente declara una tabla de cadenas de compatibilidad:

```c
static const struct ofw_compat_data compat_data[] = {
    { "brcm,bcm2711-gpio",   1 },
    { "brcm,bcm2835-gpio",   2 },
    { NULL,                  0 }
};
```

Luego, en el probe:

```c
static int
mydrv_probe(device_t dev)
{
    if (!ofw_bus_status_okay(dev))
        return (ENXIO);

    if (ofw_bus_search_compatible(dev, compat_data)->ocd_str == NULL)
        return (ENXIO);

    device_set_desc(dev, "My FDT-Aware Driver");
    return (BUS_PROBE_DEFAULT);
}
```

Y en el attach, la entrada coincidente está disponible para consultarla de nuevo:

```c
static int
mydrv_attach(device_t dev)
{
    const struct ofw_compat_data *match;
    ...
    match = ofw_bus_search_compatible(dev, compat_data);
    if (match == NULL || match->ocd_str == NULL)
        return (ENXIO);

    sc->variant = match->ocd_data; /* 1 for BCM2711, 2 for BCM2835 */
    ...
}
```

El campo `ocd_data` es la cookie que definiste en la tabla. Es un `uintptr_t` simple, por lo que puedes usarlo como discriminador entero, como puntero a una estructura por variante, o de la manera que mejor se adapte a las necesidades de tu driver.

### Lectura de propiedades

Una vez que tienes un `device_t`, leer las propiedades de su nodo es sencillo. Un patrón típico:

```c
phandle_t node = ofw_bus_get_node(dev);
uint32_t val;

if (OF_getencprop(node, "clock-frequency", &val, sizeof(val)) <= 0) {
    device_printf(dev, "missing clock-frequency\n");
    return (ENXIO);
}
```

Los helpers son `OF_getencprop` para propiedades enteras (con el endianness ya gestionado), `OF_getprop` para buffers en bruto y `OF_getprop_alloc` para cadenas de longitud desconocida. Para las propiedades booleanas (aquellas cuya presencia es la señal y cuyo valor está vacío), el patrón habitual es `OF_hasprop`:

```c
bool want_rts = OF_hasprop(node, "uart-has-rtscts");
```

Para las propiedades de lista, `OF_getprop_alloc` o las variantes de buffer fijo permiten extraer la lista completa e iterar sobre ella.

### Adquisición de recursos

Los recursos de memoria e interrupción se obtienen mediante las llamadas estándar a `bus_alloc_resource_any()` que ya conoces:

```c
sc->mem_rid = 0;
sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
    &sc->mem_rid, RF_ACTIVE);
if (sc->mem_res == NULL) {
    device_printf(dev, "cannot allocate memory\n");
    return (ENXIO);
}

sc->irq_rid = 0;
sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
    &sc->irq_rid, RF_ACTIVE | RF_SHAREABLE);
```

Esto es posible porque simplebus ya ha leído las propiedades `reg` e `interrupts` de tu nodo, las ha traducido a través de `ranges` del padre y las ha almacenado en la lista de recursos. Los índices `0`, `1`, `2` hacen referencia a la primera, segunda y tercera entrada de la lista correspondiente. Un dispositivo con varias regiones `reg` tendrá múltiples recursos de memoria con rids `0`, `1`, `2`, etcétera.

Para cualquier cosa que vaya más allá de la memoria y las interrupciones simples, debes recurrir a los frameworks de periféricos.

### Frameworks de periféricos: reloj, regulador, reset, control de pines y GPIO

Los periféricos embebidos suelen necesitar más que una simple región de memoria. Necesitan que se active un reloj, que se habilite un regulador, que se quite el reset, quizás que se multiplejen pines y, a veces, un GPIO para controlar una señal de chip-select o de habilitación. FreeBSD proporciona un conjunto coherente de frameworks para cada uno de estos casos.

**Framework de reloj.** Declarado en `/usr/src/sys/dev/extres/clk/clk.h`. Un consumidor llama a `clk_get_by_ofw_index(dev, node, idx, &clk)` para obtener un handle al N-ésimo reloj de la propiedad `clocks` del nodo, o a `clk_get_by_ofw_name(dev, node, "fck", &clk)` para obtener el reloj cuya entrada en `clock-names` es `"fck"`. Con un handle en mano, el consumidor llama a `clk_enable(clk)` para activarlo, a `clk_get_freq(clk, &freq)` para consultar su frecuencia y a `clk_disable(clk)` para desactivarlo durante el apagado.

**Framework de regulador.** Declarado en `/usr/src/sys/dev/extres/regulator/regulator.h`. `regulator_get_by_ofw_property(dev, node, "vdd-supply", &reg)` obtiene un regulador mediante la propiedad indicada; `regulator_enable(reg)` lo activa; `regulator_disable(reg)` lo desactiva.

**Framework de reset de hardware.** Declarado en `/usr/src/sys/dev/extres/hwreset/hwreset.h`. `hwreset_get_by_ofw_name(dev, node, "main", &rst)` obtiene una línea de reset; `hwreset_deassert(rst)` saca el periférico del estado de reset; `hwreset_assert(rst)` lo devuelve a ese estado.

**Framework de control de pines.** Declarado en `/usr/src/sys/dev/fdt/fdt_pinctrl.h`. `fdt_pinctrl_configure_by_name(dev, "default")` aplica la configuración de pines asociada al slot `pinctrl-names = "default"` del nodo. La mayoría de los drivers que necesitan control de pines simplemente hacen esta llamada una vez desde attach.

**Framework GPIO.** El lado consumidor está declarado en `/usr/src/sys/dev/gpio/gpiobusvar.h`. `gpio_pin_get_by_ofw_idx(dev, node, idx, &pin)` obtiene el N-ésimo GPIO de la propiedad `gpios` del nodo. `gpio_pin_setflags(pin, GPIO_PIN_OUTPUT)` establece su dirección. `gpio_pin_set_active(pin, value)` controla su nivel. `gpio_pin_release(pin)` lo libera.

Estos frameworks son la razón por la que los drivers embebidos en FreeBSD suelen ser más cortos que sus equivalentes en Linux. No tienes que escribir el árbol de relojes, la lógica del regulador ni el controlador GPIO: los consumes a través de una API de consumidor uniforme, y los drivers proveedores son problema de otro. El ejemplo práctico de la Sección 7 utiliza la API de consumidor GPIO de principio a fin.

### Enrutamiento de interrupciones: un vistazo rápido a interrupt-parent

Las interrupciones en plataformas FDT utilizan un esquema de búsqueda encadenada. La propiedad `interrupts` de un nodo proporciona el especificador de interrupción en bruto, y la propiedad `interrupt-parent` del nodo (o la del ancestro más cercano) nombra al controlador que debe interpretarla. Ese controlador, a su vez, puede ser hijo de otro controlador (un redistribuidor GIC secundario, un PLIC anidado, un puente similar al I/O APIC en algunos SoCs), que enruta la interrupción hacia arriba hasta llegar a un controlador de nivel superior vinculado a un vector real de la CPU.

Como autor de un driver, normalmente no tienes que preocuparte por esta cadena. El recurso de interrupción del kernel ya vive en tu lista de recursos como un identificador que el controlador de interrupciones sabe cómo interpretar, y `bus_setup_intr()` devuelve ese identificador al controlador cuando solicitas un IRQ. Lo que importa es que tu nodo tenga el valor correcto en `interrupts = <...>;` para su interrupt parent inmediato y que la cadena `interrupt-parent` o `interrupts-extended` del árbol llegue a un controlador real. Si no es así, tu interrupción será descartada silenciosamente durante el boot.

Los internos residen en `/usr/src/sys/dev/ofw/ofw_bus_subr.c`, con `ofw_bus_lookup_imap()`, `ofw_bus_setup_iinfo()` y funciones auxiliares relacionadas. Probablemente nunca los llamarás directamente a menos que estés escribiendo un driver de bus.

### Un breve vistazo a los overlays

Hemos mencionado los overlays en varias ocasiones. La versión corta es que un overlay es un pequeño fragmento DTB que hace referencia a nodos del árbol base por etiqueta (por ejemplo, `&i2c0` o `&gpio0`) y añade o modifica propiedades o nodos hijos. El bootloader fusiona los overlays en el blob base antes de que el kernel lo vea. Volveremos a los overlays en la Sección 5 y los usaremos en el ejemplo práctico de la Sección 7; por ahora conviene saber que FreeBSD los soporta a través del parámetro `fdt_overlays` del bootloader y los archivos bajo `/boot/dtb/overlays/`.

### ACPI frente a FDT en arm64

El port arm64 de FreeBSD soporta tanto FDT como ACPI como mecanismos de descubrimiento. El camino que toma el kernel se decide durante el arranque temprano en función de lo que proporcionó el firmware. Si se suministró un DTB y el `compatible` de nivel superior no sugiere una ruta ACPI, el kernel conecta el bus FDT. Si se suministró un RSDP de ACPI y el firmware indica conformidad SBSA, el kernel conecta el bus ACPI. El código relevante está en `/usr/src/sys/arm64/arm64/nexus.c`, que gestiona ambas rutas; la variable `arm64_bus_method` registra cuál fue elegida.

La consecuencia práctica para los autores de drivers es que un driver escrito para ambos mecanismos debe conectarse a ambos buses. Los drivers que solo se preocupan por FDT (la mayoría de los pequeños drivers embebidos) se registran únicamente con simplebus. Los drivers que sirven hardware genérico que puede aparecer tanto en servidores ARM (ACPI) como en placas embebidas ARM (FDT) se registran con ambos. El driver `ahci_generic` en `/usr/src/sys/dev/ahci/ahci_generic.c` es uno de estos drivers de soporte dual; su código fuente merece la pena leerlo cuando eventualmente necesites escribir uno. Durante la mayor parte de este capítulo nos quedaremos en el lado puramente FDT.

### Cerrando esta sección

La Sección 3 te ha dado el mapa del soporte FDT de FreeBSD. Ahora sabes dónde vive el código principal, qué funciones auxiliares usar para cada tarea y cómo se conectan las piezas: `fdt(4)` analiza el árbol, las primitivas `OF_*` leen las propiedades, las funciones auxiliares `ofw_bus_*` envuelven esas primitivas en comprobaciones idiomáticas, simplebus enumera los nodos hijos y los frameworks de periféricos te proporcionan relojes, resets, reguladores, pines y GPIOs.

En la siguiente sección tomaremos esas piezas y escribiremos un driver real con ellas. La Sección 4 recorre el esqueleto completo de un driver con soporte FDT de arriba a abajo, con suficiente detalle como para que puedas copiar la estructura en tu propio proyecto y empezar a rellenar la lógica específica del hardware.

## Sección 4: Escritura de un driver para un sistema basado en FDT

Esta sección recorre la forma completa de un driver FreeBSD con soporte FDT. La forma es sencilla; la razón para exponerla en detalle es que, una vez que la hayas visto, todos los drivers FDT del árbol se vuelven legibles. Empezarás a notar los mismos patrones en cientos de archivos bajo `/usr/src/sys/dev/` y `/usr/src/sys/arm/`, y cada uno de esos drivers se convierte en otra plantilla que puedes adaptar.

Construiremos el esqueleto en seis pasadas. Primero las inclusiones de cabeceras. Luego el softc. Luego la tabla de compatibilidad. Luego `probe()`. Luego `attach()`. Por último, `detach()` y el registro del módulo. Cada pasada es breve; al final tendrás un driver mínimo completo y compilable que imprime un mensaje cuando el kernel lo asocia a un nodo del Device Tree.

### Las inclusiones de cabeceras

Los drivers FDT se apoyan en un puñado de cabeceras de los directorios `ofw` y `fdt`, además de las cabeceras habituales del kernel y del bus. Un conjunto típico:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/resource.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
```

Nada exótico. Las tres cabeceras `ofw` aportan los lectores de propiedades y las funciones auxiliares de compatibilidad. Si tu driver es consumidor de GPIOs, relojes, reguladores o hwreset, añade también sus cabeceras:

```c
#include <dev/gpio/gpiobusvar.h>
#include <dev/extres/clk/clk.h>
#include <dev/extres/regulator/regulator.h>
#include <dev/extres/hwreset/hwreset.h>
```

Control de pines (pinctrl):

```c
#include <dev/fdt/fdt_pinctrl.h>
```

Y `simplebus.h` si tu driver realmente extiende simplebus en lugar de simplemente vincularse a él como nodo hoja:

```c
#include <dev/fdt/simplebus.h>
```

La mayoría de los drivers hoja no necesitan la cabecera simplebus. Inclúyela solo cuando estés implementando un driver de tipo bus que enumera nodos hijos.

### El softc

Un softc para un driver con soporte FDT es un softc normal con algunos campos adicionales para rastrear los recursos y referencias que obtuviste mediante las funciones auxiliares OFW:

```c
struct mydrv_softc {
    device_t        dev;
    struct resource *mem_res;   /* memory region (bus_alloc_resource) */
    int             mem_rid;
    struct resource *irq_res;   /* interrupt resource, if any */
    int             irq_rid;
    void            *irq_cookie;

    /* FDT-specific state. */
    phandle_t       node;
    uintptr_t       variant;    /* matched ocd_data */

    /* Example: acquired GPIO pin for driving a chip-select. */
    gpio_pin_t      cs_pin;

    /* Example: acquired clock handle. */
    clk_t           clk;

    /* Usual driver state: mutex, buffers, etc. */
    struct mtx      sc_mtx;
};
```

Los campos que difieren respecto a un driver PCI o ISA son `node`, `variant` y los handles de consumidor (`cs_pin`, `clk` y otros). Todo lo demás es estándar.

### La tabla de compatibilidad

La tabla de compatibilidad es la declaración del driver sobre un conjunto de nodos del Device Tree. Por convención se declara con ámbito de archivo e inmutable:

```c
static const struct ofw_compat_data mydrv_compat_data[] = {
    { "acme,trivial-timer",    1 },
    { "acme,fancy-timer",      2 },
    { NULL,                    0 }
};
```

El segundo campo, `ocd_data`, es un identificador de tipo `uintptr_t`. Me gusta usarlo como discriminador entero (1 para la variante básica, 2 para la variante avanzada); también puedes usarlo como puntero a una estructura de configuración por variante. La tabla termina con una entrada centinela cuyo primer campo es `NULL`.

### La rutina probe

Un probe canónico para un driver con soporte FDT:

```c
static int
mydrv_probe(device_t dev)
{

    if (!ofw_bus_status_okay(dev))
        return (ENXIO);

    if (ofw_bus_search_compatible(dev, mydrv_compat_data)->ocd_str == NULL)
        return (ENXIO);

    device_set_desc(dev, "ACME Trivial Timer");
    return (BUS_PROBE_DEFAULT);
}
```

Tres líneas de lógica. Primero, salir si el nodo está deshabilitado. Segundo, salir si ninguna de nuestras cadenas de compatibilidad coincide. Tercero, establecer un nombre descriptivo y devolver `BUS_PROBE_DEFAULT`. El valor de retorno exacto importa cuando más de un driver puede reclamar el mismo nodo; un driver más especializado puede devolver `BUS_PROBE_SPECIFIC` para tener prioridad sobre uno genérico, y un fallback genérico puede devolver `BUS_PROBE_GENERIC` para dejar que cualquier opción mejor gane. Para la mayoría de los drivers, `BUS_PROBE_DEFAULT` es lo correcto.

La llamada a `ofw_bus_search_compatible(dev, compat_data)` devuelve un puntero a la entrada que coincide, o a la entrada centinela si no se encontró ninguna coincidencia. El `ocd_str` de la centinela es `NULL`, de modo que comprobar si es `NULL` es la forma idiomática de decir *no coincidimos con nada*. Algunos drivers guardan alternativamente el puntero devuelto en una variable local y lo reutilizan; haremos eso en attach.

### La rutina attach

Attach es donde ocurre el trabajo real. Un attach FDT canónico:

```c
static int
mydrv_attach(device_t dev)
{
    struct mydrv_softc *sc;
    const struct ofw_compat_data *match;
    phandle_t node;
    uint32_t freq;
    int err;

    sc = device_get_softc(dev);
    sc->dev = dev;
    sc->node = ofw_bus_get_node(dev);
    node = sc->node;

    /* Remember which variant we matched. */
    match = ofw_bus_search_compatible(dev, mydrv_compat_data);
    if (match == NULL || match->ocd_str == NULL)
        return (ENXIO);
    sc->variant = match->ocd_data;

    /* Pull any required properties. */
    if (OF_getencprop(node, "clock-frequency", &freq, sizeof(freq)) <= 0) {
        device_printf(dev, "missing clock-frequency property\n");
        return (ENXIO);
    }

    /* Allocate memory and interrupt resources. */
    sc->mem_rid = 0;
    sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
        &sc->mem_rid, RF_ACTIVE);
    if (sc->mem_res == NULL) {
        device_printf(dev, "cannot allocate memory resource\n");
        err = ENXIO;
        goto fail;
    }

    sc->irq_rid = 0;
    sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
        &sc->irq_rid, RF_ACTIVE | RF_SHAREABLE);
    if (sc->irq_res == NULL) {
        device_printf(dev, "cannot allocate IRQ resource\n");
        err = ENXIO;
        goto fail;
    }

    /* Enable clock, if one is described. */
    if (clk_get_by_ofw_index(dev, node, 0, &sc->clk) == 0) {
        err = clk_enable(sc->clk);
        if (err != 0) {
            device_printf(dev, "could not enable clock: %d\n", err);
            goto fail;
        }
    }

    /* Apply pinctrl default, if any. */
    (void)fdt_pinctrl_configure_by_name(dev, "default");

    /* Initialise locks and driver state. */
    mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

    /* Hook up the interrupt handler. */
    err = bus_setup_intr(dev, sc->irq_res,
        INTR_TYPE_MISC | INTR_MPSAFE, NULL, mydrv_intr, sc,
        &sc->irq_cookie);
    if (err != 0) {
        device_printf(dev, "could not setup interrupt: %d\n", err);
        goto fail;
    }

    device_printf(dev, "variant %lu at %s, clock %u Hz\n",
        (unsigned long)sc->variant, device_get_nameunit(dev), freq);

    return (0);

fail:
    mydrv_detach(dev);
    return (err);
}
```

Hay mucho que desgranar aquí. Vayamos paso a paso.

1. `device_get_softc(dev)` devuelve el softc del driver, que FreeBSD asignó en tu nombre cuando el driver se conectó.
2. `ofw_bus_get_node(dev)` devuelve el phandle de nuestro nodo DT. Lo guardamos en el softc porque detach también lo necesitará.
3. Volvemos a ejecutar la búsqueda de compatibilidad y registramos qué variante coincidió.
4. Leemos una propiedad entera escalar con `OF_getencprop`. La llamada devuelve el número de bytes leídos, `-1` si la propiedad no existe, o un número menor si la propiedad es demasiado corta. Tratamos cualquier valor no positivo como un fallo.
5. Asignamos nuestros recursos de memoria e IRQ. Simplebus ya ha rellenado la lista de recursos a partir de las propiedades `reg` e `interrupts` del nodo, por lo que los índices 0 y 0 son correctos.
6. Intentamos obtener un reloj. Este driver trata el reloj como opcional, por lo que la ausencia de la propiedad `clocks` no es fatal. Si está presente, lo activamos.
7. Aplicamos el control de pines por defecto.
8. Inicializamos un mutex del driver.
9. Configuramos el manejador de interrupciones, que despachará a `mydrv_intr`.
10. Registramos un mensaje.
11. Ante cualquier error, hacemos goto a una única ruta de limpieza que llama a detach.

La ruta de limpieza única merece su propia explicación. Es un patrón adecuado para entornos embebidos porque los drivers embebidos adquieren muchos recursos de muchos frameworks diferentes, y escribir código de limpieza en cada punto de fallo se vuelve rápidamente ilegible. En su lugar, escribe un detach que gestione el estado del softc parcialmente inicializado y llámalo desde la ruta de fallo. Este es el patrón que el árbol de FreeBSD utiliza de forma consistente; tu driver será más fácil de leer si lo sigues.

### La rutina detach

Un detach correcto desmonta todo lo que attach pudo haber configurado, y lo hace en orden inverso:

```c
static int
mydrv_detach(device_t dev)
{
    struct mydrv_softc *sc;

    sc = device_get_softc(dev);

    if (sc->irq_cookie != NULL) {
        bus_teardown_intr(dev, sc->irq_res, sc->irq_cookie);
        sc->irq_cookie = NULL;
    }

    if (sc->irq_res != NULL) {
        bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
            sc->irq_res);
        sc->irq_res = NULL;
    }

    if (sc->mem_res != NULL) {
        bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid,
            sc->mem_res);
        sc->mem_res = NULL;
    }

    if (sc->clk != NULL) {
        clk_disable(sc->clk);
        clk_release(sc->clk);
        sc->clk = NULL;
    }

    if (mtx_initialized(&sc->sc_mtx))
        mtx_destroy(&sc->sc_mtx);

    return (0);
}
```

Hay dos cosas que conviene destacar. En primer lugar, cada paso del teardown está protegido por una comprobación de si el recurso fue realmente adquirido. Esta comprobación permite que detach funcione correctamente tanto desde una ruta de descarga normal (todos los recursos fueron adquiridos) como desde una ruta de attach fallido (solo algunos lo fueron). En segundo lugar, el orden es el inverso al de la adquisición. El manejador de interrupciones se desactiva antes de liberar el recurso de interrupción. El reloj se deshabilita antes de liberarlo. El mutex se destruye al final.

### El manejador de interrupciones

El manejador de interrupciones es una rutina de interrupción normal de FreeBSD. No hay nada específico de FDT en él:

```c
static void
mydrv_intr(void *arg)
{
    struct mydrv_softc *sc = arg;

    mtx_lock(&sc->sc_mtx);
    /* Handle the hardware event. */
    mtx_unlock(&sc->sc_mtx);
}
```

Lo que *sí* es específico de FDT es la forma en que el recurso de interrupción se configuró en attach. El recurso provino de la propiedad `interrupts` del nodo a través de simplebus, que lo tradujo a lo largo de la cadena interrupt-parent de modo que, cuando tu driver llamó a `bus_alloc_resource_any(SYS_RES_IRQ, ...)`, el recurso ya representaba una interrupción de hardware real en un controlador real.

### Registro del módulo

El registro del módulo del driver vincula los métodos del dispositivo con el driver y registra el driver en un bus padre:

```c
static device_method_t mydrv_methods[] = {
    DEVMETHOD(device_probe,  mydrv_probe),
    DEVMETHOD(device_attach, mydrv_attach),
    DEVMETHOD(device_detach, mydrv_detach),

    DEVMETHOD_END
};

static driver_t mydrv_driver = {
    "mydrv",
    mydrv_methods,
    sizeof(struct mydrv_softc)
};

DRIVER_MODULE(mydrv, simplebus, mydrv_driver, 0, 0);
DRIVER_MODULE(mydrv, ofwbus,   mydrv_driver, 0, 0);
MODULE_VERSION(mydrv, 1);
SIMPLEBUS_PNP_INFO(mydrv_compat_data);
```

Las dos llamadas a `DRIVER_MODULE` registran el driver tanto con simplebus como con la raíz ofwbus. Esta última cubre plataformas o placas cuyo nodo se encuentra directamente bajo la raíz en lugar de bajo un simple-bus explícito. `MODULE_VERSION` declara la versión del driver para `kldstat` y el seguimiento de dependencias. `SIMPLEBUS_PNP_INFO` emite un descriptor pnpinfo que `kldstat -v` puede mostrar; es un pequeño detalle de cortesía para el operador, pero el driver funcionará sin él.

### El esqueleto completo ensamblado

Aquí tienes el esqueleto ensamblado en un único archivo mínimo que compila como módulo del kernel. No hace nada útil; solo demuestra la conexión y registra un mensaje cuando encuentra una coincidencia:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

struct fdthello_softc {
    device_t dev;
    phandle_t node;
};

static const struct ofw_compat_data compat_data[] = {
    { "freebsd,fdthello",  1 },
    { NULL,                0 }
};

static int
fdthello_probe(device_t dev)
{
    if (!ofw_bus_status_okay(dev))
        return (ENXIO);

    if (ofw_bus_search_compatible(dev, compat_data)->ocd_str == NULL)
        return (ENXIO);

    device_set_desc(dev, "FDT Hello Example");
    return (BUS_PROBE_DEFAULT);
}

static int
fdthello_attach(device_t dev)
{
    struct fdthello_softc *sc;

    sc = device_get_softc(dev);
    sc->dev = dev;
    sc->node = ofw_bus_get_node(dev);

    device_printf(dev, "attached, node phandle 0x%x\n", sc->node);
    return (0);
}

static int
fdthello_detach(device_t dev)
{
    device_printf(dev, "detached\n");
    return (0);
}

static device_method_t fdthello_methods[] = {
    DEVMETHOD(device_probe,  fdthello_probe),
    DEVMETHOD(device_attach, fdthello_attach),
    DEVMETHOD(device_detach, fdthello_detach),

    DEVMETHOD_END
};

static driver_t fdthello_driver = {
    "fdthello",
    fdthello_methods,
    sizeof(struct fdthello_softc)
};

DRIVER_MODULE(fdthello, simplebus, fdthello_driver, 0, 0);
DRIVER_MODULE(fdthello, ofwbus,   fdthello_driver, 0, 0);
MODULE_VERSION(fdthello, 1);
```

Y su `Makefile` correspondiente:

```make
KMOD=	fdthello
SRCS=	fdthello.c

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
```

Encontrarás ambos en `examples/part-07/ch32-fdt-embedded/lab01-fdthello/`. Compílalos en cualquier sistema FreeBSD que tenga las fuentes del kernel instaladas. El módulo solo se conectará en una plataforma cuyo DTB contenga un nodo con `compatible = "freebsd,fdthello"`, pero al menos compilará y cargará sin problemas incluso en amd64.

### Qué hace el kernel a continuación

Cuando cargas ese módulo en un sistema arm64 cuyo DTB contiene un nodo coincidente, la secuencia es:

1. El módulo registra `fdthello_driver` con simplebus y ofwbus.
2. Newbus itera sobre todos los dispositivos existentes que tienen `simplebus` u `ofwbus` como padre y llama al probe del driver recién registrado.
3. Para cada dispositivo cuyo nodo tiene `compatible = "freebsd,fdthello"`, el probe devuelve `BUS_PROBE_DEFAULT`. Si ningún otro driver está ya conectado (o si lo superamos en prioridad), se llama a nuestro attach.
4. Attach registra un mensaje; el dispositivo está ahora conectado.

Cuando descargas el módulo, detach se ejecuta para cada instancia conectada y luego el módulo se descarga. En el caso simple, `kldunload fdthello` limpia todo por completo.

### Comprobando el resultado

Tres formas rápidas de verificar si tu driver encontró una coincidencia:

1. **`dmesg`** debería mostrar una línea como:
   ```
   fdthello0: <FDT Hello Example> on simplebus0
   fdthello0: attached, node phandle 0x8f
   ```
2. **`devinfo -r`** debería mostrar tu dispositivo conectado en algún lugar bajo `simplebus`.
3. **`sysctl dev.fdthello.0.%parent`** debería confirmar el bus padre.

Si tu módulo carga pero no se conecta ningún dispositivo, el probe no encontró ninguna coincidencia. Las causas más habituales son una errata en la cadena compatible, un nodo ausente o deshabilitado, o un nodo que se encuentra en algún lugar al que los drivers simplebus/ofwbus no llegaron. La sección 6 trata el proceso de depuración en detalle.

### Una nota sobre nombres y prefijos de fabricante

Los drivers reales de FreeBSD buscan coincidencias con cadenas de compatibilidad como `"brcm,bcm2711-gpio"`, `"allwinner,sun50i-a64-mmc"` o `"st,stm32-uart"`. El prefijo antes de la coma es el nombre del fabricante o de la comunidad; el resto es el chip o la familia específica. Esta convención se respeta ampliamente tanto en Linux como en FreeBSD. Cuando inventas una cadena compatible nueva para un experimento (como hicimos antes con `"freebsd,fdthello"`), sigue el mismo formato de prefijo de fabricante seguido de coma e identificador. No inventes cadenas de una sola palabra; colisionan con las ya existentes y confunden a los futuros lectores.

### Cerrando esta sección

La sección 4 ha recorrido la estructura de un driver FDT. Has visto las cabeceras que hay que incluir, el softc que hay que definir, la tabla de compatibilidad que hay que declarar, las rutinas probe, attach y detach que hay que escribir, y el registro del módulo que lo une todo. Tienes un driver mínimo y completo que podrías compilar y cargar ahora mismo. No hace gran cosa, pero su estructura es la misma que utiliza cualquier driver FDT de FreeBSD.

En la siguiente sección abordamos la otra mitad de la historia. Un driver no sirve de nada sin un nodo de Device Tree que coincida con él. La sección 5 te enseña cómo construir y modificar archivos `.dtb`, cómo funciona el sistema de overlays de FreeBSD, y cómo añadir tu propio nodo a la descripción de una placa existente para que tu driver tenga algo con lo que conectarse.

## 5. Creación y modificación de Device Tree Blobs

Ahora tienes un driver que espera pacientemente un nodo de Device Tree con `compatible = "freebsd,fdthello"`. Nada en el sistema en ejecución proporciona ese nodo, así que nada lanza el probe. En esta sección aprendemos cómo cambiar eso. Veremos el pipeline de fuente a binario, el mecanismo de overlays que nos permite añadir nodos sin reconstruir el `.dtb` completo, y los tunables del loader que deciden qué blob ve realmente el kernel en el momento del boot.

Crear Device Tree Blobs no es un rito de iniciación reservado a hackers experimentados del kernel. Es una tarea de edición ordinaria. El archivo es texto, el compilador es estándar y la salida es un pequeño binario que vive en `/boot`. Lo que lo hace parecer poco familiar es únicamente que muy pocos proyectos de aficionados se topan con él. En FreeBSD para sistemas embebidos es algo rutinario.

### El pipeline de fuente a binario

Cada `.dtb` que arranca un sistema FreeBSD comenzó su vida como uno o más archivos fuente. El pipeline es simple:

```text
.dtsi  .dtsi  .dtsi
   \    |    /
    \   |   /
     .dts (top-level source)
       |
       | cpp (C preprocessor)
       v
     .dts (preprocessed)
       |
       | dtc (device tree compiler)
       v
     .dtb (binary blob)
```

El preprocesador C se ejecuta primero. Expande las directivas `#include`, las definiciones de macros procedentes de archivos de cabecera como `dt-bindings/gpio/gpio.h`, y las expresiones aritméticas en las propiedades. El compilador `dtc` transforma después la fuente preprocesada en el formato compacto y aplanado que el kernel puede analizar.

Los archivos de overlay siguen el mismo pipeline, salvo que su archivo fuente lleva la extensión `.dtso` y su salida lleva `.dtbo`. La única diferencia sintáctica real es la fórmula especial que aparece al principio de los archivos fuente de overlay, que veremos en breve.

El sistema de build de FreeBSD envuelve este pipeline en dos pequeños scripts de shell que puedes estudiar en `/usr/src/sys/tools/fdt/make_dtb.sh` y `/usr/src/sys/tools/fdt/make_dtbo.sh`. Estos encadenan `cpp` y `dtc`, añaden las rutas de inclusión correctas para las cabeceras propias del kernel en `dt-bindings`, y escriben los blobs resultantes en el árbol de build. Cuando ejecutas `make buildkernel` para una plataforma embebida, estos scripts son los que producen los archivos `.dtb` que acaban en `/boot/dtb/` en el sistema instalado.

### Instalación de las herramientas

En FreeBSD, `dtc` está disponible como port:

```console
# pkg install dtc
```

El paquete instala el binario `dtc` junto con su compañero `fdtdump`, que imprime la estructura decodificada de un blob existente. Si piensas trabajar con overlays, instala ambos. El árbol base de FreeBSD también incluye una copia de `dtc` en `/usr/src/sys/contrib/device-tree/`, pero la versión del port es más fácil de usar desde el espacio de usuario.

Para comprobar la versión:

```console
$ dtc --version
Version: DTC 1.7.0
```

Cualquier versión a partir de la 1.6 admite overlays. Las versiones anteriores carecen de la directiva `/plugin/;`, así que si heredas un entorno de build antiguo, actualiza antes de continuar.

### Escritura de un archivo .dts independiente

Empezaremos con un archivo fuente de Device Tree completo e independiente, para que la sintaxis tenga tiempo de asentarse antes de añadir las complejidades de los overlays. Crea un archivo llamado `tiny.dts` en algún lugar fuera del árbol del kernel:

```dts
/dts-v1/;

/ {
    compatible = "example,tiny-board";
    model = "Tiny Example Board";
    #address-cells = <1>;
    #size-cells = <1>;

    chosen {
        bootargs = "-v";
    };

    cpus {
        #address-cells = <1>;
        #size-cells = <0>;

        cpu0: cpu@0 {
            device_type = "cpu";
            reg = <0>;
            compatible = "arm,cortex-a53";
        };
    };

    memory@0 {
        device_type = "memory";
        reg = <0x00000000 0x10000000>;
    };

    soc {
        compatible = "simple-bus";
        #address-cells = <1>;
        #size-cells = <1>;
        ranges;

        hello0: hello@10000 {
            compatible = "freebsd,fdthello";
            reg = <0x10000 0x100>;
            status = "okay";
        };
    };
};
```

La primera línea, `/dts-v1/;`, indica a `dtc` qué versión del formato fuente estamos usando. La versión 1 es la única en uso actualmente, pero la directiva sigue siendo obligatoria.

A continuación tenemos el nodo raíz, que contiene varios nodos hijo esperados. El nodo `cpus` describe la topología del procesador, el nodo `memory@0` declara una región de 256 MB de DRAM en la dirección física cero, y el nodo `soc` agrupa los periféricos integrados bajo un `simple-bus`. Dentro de `soc`, nuestro nodo `hello@10000` proporciona la coincidencia de Device Tree para el driver `fdthello` que escribimos en la sección 4.

Hay varias cosas que vale la pena observar incluso en este pequeño archivo.

En primer lugar, `#address-cells` y `#size-cells` vuelven a aparecer dentro del nodo `soc`. Los valores que establece un padre se aplican únicamente a los hijos directos de ese padre, por lo que cada nivel del árbol que se preocupa por las direcciones tiene que declararlos. Aquí el `soc` usa una celda para las direcciones y una para los tamaños, razón por la cual `reg = <0x10000 0x100>;` dentro de `hello@10000` lista exactamente dos valores `u32`.

En segundo lugar, la propiedad `ranges;` del nodo `soc` está vacía. Un `ranges` vacío significa "las direcciones dentro de este bus coinciden con las del exterior de forma directa". Si el `soc` estuviera, por ejemplo, mapeado en una dirección base diferente a la que declaran sus hijos, usarías una lista `ranges` más larga para expresar la traducción.

En tercer lugar, `status = "okay"` es explícito aquí. Sin él, cada árbol implícitamente tiene el valor okay por defecto, pero muchos archivos de placa establecen `status = "disabled"` en los periféricos opcionales y esperan que los overlays o los archivos específicos de la placa lo activen. Adquiere el hábito de comprobar esta propiedad siempre que un driver falle misteriosamente en el probe.

### Compilación de un archivo .dts

Compila el pequeño ejemplo:

```console
$ dtc -I dts -O dtb -o tiny.dtb tiny.dts
```

El flag `-I dts` indica a `dtc` que la entrada es fuente textual, y `-O dtb` solicita una salida en formato blob binario. Una compilación correcta no imprime nada. Un error de sintaxis te indica el archivo y la línea.

Puedes verificar el resultado con `fdtdump`:

```console
$ fdtdump tiny.dtb | head -30
**** fdtdump is a low-level debugging tool, not meant for general use. ****
    Use the fdtput/fdtget/dtc tools to manipulate .dtb files.

/dts-v1/;
// magic:               0xd00dfeed
// totalsize:           0x214 (532)
// off_dt_struct:       0x38
// off_dt_strings:      0x184
// off_mem_rsvmap:      0x28
// version:             17
// last_comp_version:   16
// boot_cpuid_phys:     0x0
// boot_cpuid_phys:     0x0
// size_dt_strings:     0x90
// size_dt_strings:     0x90
// size_dt_structs:     0x14c
// size_dt_structs:     0x14c

/ {
    compatible = "example,tiny-board";
    model = "Tiny Example Board";
    ...
```

Ese ciclo de ida y vuelta confirma que el blob es válido y analizable. Podrías ahora colocarlo en una ejecución de QEMU con `-dtb tiny.dtb` y el kernel intentaría arrancar con él. En la práctica, rara vez escribes un `.dts` completo a mano para una placa real. Partes del archivo fuente propio del fabricante (por ejemplo, `/usr/src/sys/contrib/device-tree/src/arm64/broadcom/bcm2711-rpi-4-b.dts` para la Raspberry Pi 4) y modificas un subconjunto de nodos con un overlay.

### El papel de los archivos de inclusión .dtsi

La extensión `.dtsi` se usa para los archivos de *inclusión* de Device Tree. Estos archivos contienen fragmentos del árbol pensados para ser incorporados en otro `.dts` o `.dtsi`. El compilador los trata de forma idéntica a los archivos `.dts`, pero el sufijo del nombre de archivo indica a otros lectores humanos (y al sistema de build) que el archivo no es independiente.

Un patrón habitual en las descripciones modernas de SoC es:

```text
arm/bcm283x.dtsi          <- SoC definition
arm/bcm2710.dtsi          <- Family refinement (Pi 3 lineage)
arm/bcm2710-rpi-3-b.dts   <- Specific board top-level file, includes both
```

Cada `.dtsi` añade y refina nodos. Las etiquetas declaradas en un archivo de nivel inferior pueden referenciarse desde un archivo de nivel superior con la sintaxis `&label` para sobreescribir propiedades sin reescribir el nodo. Este es el mecanismo que hace posible dar soporte a decenas de placas relacionadas a partir de un puñado de descripciones de SoC compartidas.

### Comprensión de los overlays

Un `.dts` completo para una SBC real como la Raspberry Pi 4 tiene decenas de kilobytes. Si solo quieres habilitar SPI, o añadir un único periférico controlado por GPIO, reconstruir todo el blob es costoso e introduce posibilidades de error. El mecanismo de overlays existe exactamente para esta situación.

Un overlay es un `.dtb` pequeño y especial que apunta a un árbol existente. En el momento de la carga, el loader de FreeBSD fusiona el overlay en el árbol base en memoria, produciendo una vista combinada que el kernel ve como un único Device Tree. El `.dtb` base en disco nunca se modifica. Esto significa que el mismo overlay puede habilitar una característica en varios sistemas con una sola copia en cada uno.

La sintaxis de un archivo fuente de overlay usa dos directivas especiales al principio:

```dts
/dts-v1/;
/plugin/;
```

Después de estas, la fuente hace referencia a los nodos del árbol base mediante etiqueta. El compilador registra las referencias de forma simbólica, y el loader las resuelve en el momento de la fusión con respecto a las etiquetas que el árbol base exporta realmente. Por eso el overlay puede escribirse y compilarse de forma independiente del árbol base concreto con el que se fusionará posteriormente.

Aquí tienes un overlay mínimo que conecta un nodo `fdthello` a un bus `soc` existente:

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2711";

    fragment@0 {
        target = <&soc>;
        __overlay__ {
            hello0: hello@20000 {
                compatible = "freebsd,fdthello";
                reg = <0x20000 0x100>;
                status = "okay";
            };
        };
    };
};
```

El `compatible` exterior indica que este overlay está destinado a un árbol BCM2711. El cargador usa esa cadena para rechazar overlays que no coincidan con la placa actual. En el interior encontramos un único nodo `fragment@0`. Cada fragmento apunta a un nodo existente en el árbol base mediante la propiedad `target`. El contenido bajo `__overlay__` es el conjunto de propiedades e hijos que se fusionan en ese destino.

En este ejemplo, añadimos un hijo `hello@20000` bajo el nodo al que resuelva `&soc` en el momento de la fusión. El árbol base en una Raspberry Pi 4 declara la etiqueta `soc` sobre el nodo del bus SoC de nivel superior, de modo que, tras la fusión, el nodo `soc` base ganará un nuevo hijo `hello@20000` y se disparará el probe de nuestro driver.

También puedes usar overlays para *modificar* nodos existentes. Si estableces una propiedad con el mismo nombre que una ya existente, el valor del overlay reemplaza al original. Si añades una propiedad nueva, simplemente aparece. Si añades un hijo nuevo, se injerta en el árbol. El mecanismo es aditivo, salvo en el caso del reemplazo de valores de propiedad.

### Compilación y despliegue de overlays

Compila el overlay con:

```console
$ dtc -I dts -O dtb -@ -o fdthello.dtbo fdthello-overlay.dts
```

El flag `-@` indica al compilador que emita la información de etiquetas simbólicas necesaria en el momento de la fusión. Sin él, los overlays que referencian etiquetas fallan silenciosamente o producen errores poco descriptivos.

En un sistema FreeBSD en ejecución, los overlays residen en `/boot/dtb/overlays/`. El nombre de archivo debe terminar en `.dtbo` por convención. El loader busca en `/boot/dtb/overlays` de forma predeterminada; la ruta puede sobreescribirse mediante tunables del loader si deseas preparar los overlays en otro lugar.

Para indicar al loader qué overlays debe aplicar, añade una línea a `/boot/loader.conf`:

```ini
fdt_overlays="fdthello,sunxi-i2c1,spigen-rpi4"
```

El valor es una lista separada por comas de los nombres base de los overlays, sin la extensión `.dtbo`. El orden solo importa cuando los overlays interactúan entre sí. Durante el boot, el loader lee la lista, carga cada overlay, los fusiona con el árbol base en secuencia y entrega el blob combinado al kernel.

Una buena comprobación de cordura es observar la salida del loader en una consola serie o por HDMI. Cuando `fdt_overlays` está configurado, el loader imprime una línea similar a:

```text
Loading DTB overlay 'fdthello' (0x1200 bytes)
```

Si el archivo no existe o el destino no coincide, el loader imprime una advertencia y continúa. Tu driver entonces no llega a hacer probe porque el overlay nunca se aplicó. Revisar la salida por consola del loader es la forma más rápida de detectar este tipo de fallo.

### Un recorrido práctico: añadir un nodo al árbol de una Raspberry Pi 4

Pongamos toda la maquinaria en práctica con un escenario realista. Imagina que estás poniendo en marcha una placa hija personalizada para la Raspberry Pi 4. Contiene un LED indicador controlado por GPIO en el GPIO18 del conector de la Pi. Quieres que FreeBSD gestione el LED a través de tu propio driver `edled` (que construimos en la Sección 7). Para eso necesitas un nodo en el Device Tree.

Primero, examinas lo que ya declara el `.dtb` base de la Pi 4. En una Pi en ejecución con FreeBSD instalado, `ofwdump -ap | less` o `fdtdump /boot/dtb/broadcom/bcm2711-rpi-4-b.dtb | less` te muestra el árbol completo. Tu principal interés son los nodos `soc` y `gpio`, donde verás una etiqueta `gpio = <&gpio>;` exportada desde el controlador GPIO.

A continuación, escribes el fuente del overlay:

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2711";

    fragment@0 {
        target-path = "/soc";
        __overlay__ {
            edled0: edled@0 {
                compatible = "example,edled";
                status = "okay";
                leds-gpios = <&gpio 18 0>;
                label = "daughter-indicator";
            };
        };
    };
};
```

Aquí usamos `target-path` en lugar de `target` porque el destino es una ruta existente y no una etiqueta. Ambas formas son válidas: `target` acepta una referencia phandle y `target-path` acepta una cadena de texto.

La propiedad `leds-gpios` es la manera convencional de describir una referencia GPIO en el Device Tree. Es un phandle al controlador GPIO, seguido del número de GPIO en ese controlador, seguido de una palabra de flags. El valor `0` en el flag significa activo en nivel alto; `1` significa activo en nivel bajo. Normalmente no es necesario mencionar explícitamente el pinmux en la Pi porque el controlador GPIO de Broadcom gestiona tanto la dirección como la función a través del mismo conjunto de registros.

Compila e instala el overlay:

```console
$ dtc -I dts -O dtb -@ -o edled.dtbo edled-overlay.dts
$ sudo cp edled.dtbo /boot/dtb/overlays/
```

Añade el overlay a la configuración del loader:

```console
# echo 'fdt_overlays="edled"' >> /boot/loader.conf
```

Reinicia el sistema. Durante el arranque, el loader imprime su línea de carga del overlay, el DT base incorpora el nuevo nodo `edled@0` bajo `/soc`, se ejecuta el probe del driver `edled` y el LED de la placa hija queda bajo control software.

### Inspección del resultado

Una vez que el kernel está en ejecución, tres herramientas verifican que todo quedó donde debía:

```console
# ofwdump -p /soc/edled@0
```

imprime las propiedades del nodo recién añadido.

```console
# sysctl dev.edled.0.%parent
```

confirma que el driver se enganchó y muestra su bus padre.

```console
# devinfo -r | less
```

muestra el árbol de dispositivos completo tal como lo ve FreeBSD, con tu driver en su lugar.

Si alguno de estos resultados no coincide con el contenido del overlay, la Sección 6 te ayuda a diagnosticar la causa.

### Solución de problemas de compilación

La mayoría de los errores de compilación de DT pertenecen a un número reducido de categorías.

**Referencias sin resolver.** Si el overlay referencia una etiqueta como `&gpio` que no está exportada por el árbol base, el loader imprime `no symbol for <label>` y se niega a aplicar el overlay. La solución es usar `target-path` con una ruta absoluta, o reconstruir el `.dtb` base con `-@` para incluir sus símbolos.

**Errores de sintaxis.** Aparecen como errores de `dtc` con número de línea. Los culpables más habituales son los puntos y coma que faltan al final de las asignaciones de propiedades, las llaves sin cerrar y los valores de propiedades que mezclan tipos de unidad (por ejemplo, una mezcla de enteros entre corchetes angulares y cadenas entre comillas en la misma línea).

**Discrepancias en el recuento de celdas.** Si el nodo padre declara `#address-cells = <2>` y el `reg` de un hijo solo aporta una celda, el compilador lo tolera pero el kernel interpreta el valor de forma incorrecta. `ofwdump -p node` y una lectura cuidadosa de los recuentos de celdas del padre suelen revelar la discrepancia.

**Nombres de nodo duplicados.** Dos nodos al mismo nivel no pueden compartir el mismo nombre más dirección de unidad. El compilador lo señala, pero los overlays que intentan añadir un nodo cuyo nombre colisiona con uno ya existente producen un fallo de fusión críptico en el boot. Elige nombres únicos o apunta a una ruta diferente.

### El proceso de carga del dtb en el kernel

Para tener contexto, conviene saber qué ocurre con el blob fusionado final después de que el loader se lo entrega al kernel.

En arm64 y otras plataformas, el loader coloca el blob en una dirección física fija y pasa el puntero al kernel en su bloque de argumentos de boot. El código más temprano del kernel, en `/usr/src/sys/arm64/arm64/machdep.c`, valida el número mágico y el tamaño, mapea el blob en la memoria virtual del kernel y lo registra con el subsistema FDT. Para cuando Newbus empieza a engancharse a los dispositivos, el blob ya está completamente analizado y la API OFW puede recorrerlo.

En sistemas embebidos amd64 (poco comunes, pero existen), el flujo es similar: UEFI pasa el blob a través de una tabla de configuración, el loader lo descubre y el kernel lo consume a través de la misma API FDT.

El blob es de solo lectura desde la perspectiva del kernel. Nunca se modifica en tiempo de ejecución. Si el valor de una propiedad necesita cambiar, el lugar correcto para hacerlo es en el fuente, no en un árbol en vivo.

### Cerrando esta sección

La Sección 5 te ha enseñado a pasar del fuente del Device Tree al binario, cómo los overlays apuntan a un árbol existente y cómo `fdt_overlays` conecta todo con el proceso de boot de FreeBSD. Ahora ya puedes escribir un `.dts`, compilarlo, colocar el resultado en `/boot/dtb/overlays/`, listarlo en `loader.conf` y ver cómo el kernel recoge tu nodo. El driver que escribiste en la Sección 4 ya tiene algo a lo que engancharse.

En la Sección 6 daremos la vuelta al objetivo y analizaremos las herramientas para inspeccionar lo que realmente llegó al kernel. Cuando las cosas salgan mal, y saldrán, una buena observación es el camino más corto hacia un sistema que funcione.

## 6. Pruebas y depuración de drivers FDT

Algún día, cada driver que escribas fallará en el probe. El Device Tree parecerá correcto en el fuente, tu cadena de compatibilidad se leerá bien, `kldload` terminará sin queja alguna, pero `dmesg` estará en silencio. Depurar este tipo de fallo es una habilidad en sí misma, y cuanto antes adquieras el hábito, menos tiempo te costará cada problema.

Esta sección cubre las herramientas y técnicas para inspeccionar el Device Tree en ejecución, diagnosticar fallos de probe, observar el comportamiento de attach en detalle y rastrear problemas al descargar módulos. Gran parte del material aquí es aplicable a drivers de bus, drivers de periféricos y pseudodispositivos por igual. Lo que es verdaderamente específico de FDT es el conjunto de herramientas para leer el árbol en sí.

### La herramienta `ofwdump(8)`

En un sistema FreeBSD en ejecución, `ofwdump` es tu ventana principal al Device Tree. Imprime nodos y propiedades del árbol que está en el kernel, de modo que lo que muestra es exactamente lo que los drivers ven durante el probe. Si el árbol está mal en el kernel, lo estará también en `ofwdump`, lo que te evita compilar y reiniciar solo para comprobar una edición.

La invocación más sencilla imprime el árbol completo:

```console
# ofwdump -a
```

Usa `less` en cualquier sistema no trivial; la salida puede ocupar miles de líneas.

Una ejecución más enfocada vuelca un nodo y sus propiedades:

```console
# ofwdump -p /soc/hello@10000
Node 0x123456: /soc/hello@10000
    compatible:  freebsd,fdthello
    reg:         00 01 00 00 00 00 01 00
    status:      okay
```

El flag `-p` imprime las propiedades junto al nombre del nodo. Los valores enteros aparecen como cadenas de bytes porque `ofwdump` no puede saber, en general, cuántas celdas se supone que tiene una propiedad. Interpretas los bytes usando los `#address-cells` y `#size-cells` del padre.

Para leer una propiedad concreta:

```console
# ofwdump -P compatible /soc/hello@10000
```

Añade `-R` para recursar en los hijos del nodo indicado. Añade `-S` para imprimir phandles y `-r` para imprimir binario sin procesar si quieres pasar los datos a otra herramienta.

Familiarízate con `ofwdump`. Cuando alguien dice "comprueba el árbol", esta es la herramienta a la que se refieren.

### Leer el blob sin procesar a través de sysctl

FreeBSD expone el blob base sin fusionar a través de un sysctl:

```console
# sysctl -b hw.fdt.dtb | fdtdump
```

El flag `-b` le indica a sysctl que imprima binario sin procesar; pasarlo por `fdtdump` lo decodifica. Esto resulta útil cuando sospechas que un overlay ha alterado el árbol y quieres comparar el blob antes de la fusión con la vista posterior a ella. `ofwdump` muestra la vista post-fusión; `hw.fdt.dtb` muestra la base pre-fusión.

### Confirmar el modo FDT en arm64

FreeBSD no expone un sysctl dedicado que indique "estás ejecutándote sobre FDT" o "estás ejecutándote sobre ACPI". La decisión se toma muy al principio del boot mediante la variable del kernel `arm64_bus_method`, y la forma más sencilla de observarla desde el espacio de usuario es mirar lo que `dmesg` imprime durante el arranque. Una máquina que eligió la ruta FDT muestra una línea raíz como:

```text
ofwbus0: <Open Firmware Device Tree>
simplebus0: <Flattened device tree simple bus> on ofwbus0
```

seguida del resto de hijos FDT. Una máquina que eligió la ruta ACPI muestra `acpi0: <...>` en su lugar, y nunca verás una línea `ofwbus0`.

En un sistema en vivo también puedes ejecutar `devinfo -r` y buscar `ofwbus0` en la jerarquía, o confirmar que el sysctl `hw.fdt.dtb` está presente. Ese sysctl solo se registra cuando se analizó un DTB durante el boot, de modo que su mera existencia ya es una señal:

```console
# sysctl -N hw.fdt.dtb 2>/dev/null && echo "FDT is active" || echo "ACPI or neither"
```

El flag `-N` le pide a sysctl solo el nombre, por lo que el comando tiene éxito sin imprimir los bytes del blob.

En placas que admiten ambos mecanismos, el mecanismo que elige entre ellos es el tunable del loader `kern.cfg.order`. Establecer `kern.cfg.order="fdt"` en `/boot/loader.conf` obliga al kernel a intentar FDT primero y a recurrir a ACPI solo si no encuentra ningún DTB; `kern.cfg.order="acpi"` hace lo contrario. En plataformas x86, `hint.acpi.0.disabled="1"` deshabilita completamente el enganche de ACPI y a veces es útil cuando el firmware se comporta de forma incorrecta. La Sección 3 trató esta dualidad con más detalle; si alguna vez te encuentras mirando un driver FDT que se niega a engancharse en una plataforma de servidor ARM, una de las primeras cosas que debes verificar es qué método de bus eligió realmente el kernel.

### Depurar un probe que no se ejecuta

El síntoma más común es el silencio: el módulo se carga, `kldstat` lo muestra, pero ningún dispositivo se engancha. El probe o bien nunca se ejecutó o bien devolvió `ENXIO`. Sigue esta lista de comprobación.

**1. ¿Está el nodo presente en el árbol del kernel?**

```console
# ofwdump -p /soc/your-node
```

Si el nodo no aparece, tu overlay no se aplicó. Revisa la salida del loader durante el boot. Comprueba que existe una línea `fdt_overlays=` en `/boot/loader.conf`. Confirma que el archivo `.dtbo` está en `/boot/dtb/overlays/`. Reconstruye el overlay si sospechas que hay una copia desactualizada.

**2. ¿Está la propiedad status configurada como okay?**

```console
# ofwdump -P status /soc/your-node
```

Un valor de `"disabled"` impide que el nodo sea sondeado. Los archivos de placa base suelen declarar los periféricos opcionales como deshabilitados y dejan que sean los overlays quienes los habiliten.

**3. ¿Es la cadena compatible exactamente lo que espera el driver?**

Una errata en el overlay o en la tabla de compatibilidad del driver es la causa más común de fallo de probe, con diferencia. Compáralas carácter a carácter:

```console
# ofwdump -P compatible /soc/your-node
```

Con la línea correspondiente en el driver:

```c
{"freebsd,fdthello", 1},
```

Si incluso el prefijo de vendedor difiere (por ejemplo, `free-bsd,` frente a `freebsd,`), no se producirá ninguna coincidencia.

**4. ¿Admite el bus padre el sondeo?**

Los drivers FDT se enganchan a `simplebus` o a `ofwbus`. Si el padre de tu nodo es otra cosa (por ejemplo, un nodo de bus `i2c`), tu driver debe registrarse con ese padre en su lugar. Comprueba el padre examinando un nivel más arriba en `ofwdump`.

**5. ¿Tiene el driver una prioridad menor que otro driver que ya coincidió?**

Si un driver más genérico devolvió `BUS_PROBE_GENERIC` primero, tu nuevo driver necesita devolver algo más específico, como `BUS_PROBE_DEFAULT` o `BUS_PROBE_SPECIFIC`. `devinfo -r` muestra qué driver terminó adjuntándose.

### Cómo añadir salida de depuración temporal

Cuando ninguna de las opciones anteriores revela la causa, añade llamadas a `device_printf` en probe y attach para observar el flujo directamente. En probe:

```c
static int
fdthello_probe(device_t dev)
{
    device_printf(dev, "probe: node=%ld compat=%s\n",
        ofw_bus_get_node(dev),
        ofw_bus_get_compat(dev) ? ofw_bus_get_compat(dev) : "(none)");

    if (!ofw_bus_status_okay(dev)) {
        device_printf(dev, "probe: status not okay\n");
        return (ENXIO);
    }

    if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0) {
        device_printf(dev, "probe: compat mismatch\n");
        return (ENXIO);
    }

    device_set_desc(dev, "FDT Hello Example");
    return (BUS_PROBE_DEFAULT);
}
```

Esto imprime en cada llamada a probe, así que es normal que haya mucho ruido. Elimina las impresiones antes de distribuir el módulo. El objetivo es la visibilidad transitoria en lo que devuelven los helpers `ofw_bus_*`.

En attach, imprime los rids de recursos y las direcciones que has asignado:

```c
device_printf(dev, "attach: mem=%#jx size=%#jx\n",
    (uintmax_t)rman_get_start(sc->mem),
    (uintmax_t)rman_get_size(sc->mem));
```

Esto confirma que `bus_alloc_resource_any` devolvió un rango válido. Un probe que coincide pero un attach que falla aquí normalmente significa que `reg` en el DT es incorrecto.

### Cómo observar el orden de attach y las dependencias

En sistemas embebidos, el orden de attach no es siempre intuitivo. Un driver que consume GPIO tiene que esperar a que el controlador GPIO haga el attach primero. Si tu driver intenta adquirir una línea GPIO antes de que el controlador esté listo, `gpio_pin_get_by_ofw_idx` devuelve `ENXIO` y tu attach falla. FreeBSD gestiona el orden mediante dependencias explícitas expresadas en el momento del registro del driver, y mediante el recorrido de `interrupt-parent` para los árboles de interrupciones.

Usa `devinfo -rv` para observar el orden:

```console
# devinfo -rv | grep -E '(gpio|edled|simplebus)'
```

Si `edled` aparece antes que `gpio`, algo en el orden necesita corrección. La solución habitual es una línea `MODULE_DEPEND` en el driver consumidor:

```c
MODULE_DEPEND(edled, gpiobus, 1, 1, 1);
```

Esto obliga a que `gpiobus` se cargue primero, asegurando que el controlador GPIO esté disponible cuando `edled` haga el attach.

### Cómo depurar detach y la descarga del módulo

Depurar el detach es más fácil que depurar el probe porque detach se ejecuta con el sistema en marcha y la salida de `printf` llega a `dmesg` de inmediato. Los dos problemas con los que es más probable que te encuentres son:

**La descarga devuelve EBUSY.** Algún recurso sigue en manos del driver. La causa habitual es un pin GPIO o un handle de interrupción que no se liberó. Revisa cada llamada `_get_` y asegúrate de que existe una `_release_` correspondiente en detach.

**La descarga tiene éxito pero el módulo vuelve a hacer attach al siguiente `kldload`.** Esto es casi siempre porque detach dejó un campo del softc apuntando a memoria liberada, y el segundo attach siguió ese puntero. Trata detach como el desmontaje cuidadoso de todo lo que attach construyó, en orden inverso.

Un truco útil es añadir:

```c
device_printf(dev, "detach: entered\n");
...teardown...
device_printf(dev, "detach: complete\n");
```

Si la segunda línea nunca aparece, algo en detach se bloqueó o provocó un panic.

### Cómo usar DTrace para una visibilidad más profunda

Para investigaciones más sofisticadas, DTrace puede trazar `device_probe` y `device_attach` en todo el kernel sin tocar el código fuente del driver. Una instrucción de una sola línea que muestra cada llamada a attach:

```console
# dtrace -n 'fbt::device_attach:entry { printf("%s", stringof(args[0]->softc)); }'
```

La salida es ruidosa durante el boot, pero ejecutarla de forma interactiva mientras haces `kldload` de tu driver la filtra de manera natural. El uso de DTrace queda fuera del alcance de este capítulo, pero saber que existe vale la media página que llevaría configurarlo.

### Pruebas con QEMU

No todos los lectores disponen de una Raspberry Pi o una BeagleBone para hacer pruebas. QEMU puede emular una máquina virtual arm64, arrancar FreeBSD en ella y permitirte cargar drivers y overlays sin hardware real. La máquina virtual usa su propio Device Tree, que QEMU genera automáticamente; tu overlay puede apuntar a ese árbol exactamente igual que apuntaría a una placa real. La única salvedad es que los GPIO y periféricos similares de bajo nivel son limitados o inexistentes en la máquina virtual. Para experimentar puramente con DT y módulos es completamente adecuado.

La invocación básica tiene este aspecto:

```console
$ qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a72 \
    -m 2G \
    -kernel /path/to/kernel \
    -drive if=virtio,file=disk.img \
    -serial mon:stdio \
    -append "console=comconsole"
```

Una vez que el sistema esté en marcha, haz `kldload` de tu módulo y observa los mensajes de probe en la consola serie.

### Cuándo dejar de depurar y reconstruir desde cero

A veces un bug es más fácil de corregir desmontando el driver y reconstruyéndolo a partir de un esqueleto conocido y funcional. El ejemplo `fdthello` de la Sección 4 es exactamente ese esqueleto. Si llevas más de una hora persiguiendo un fallo de probe, copia `fdthello`, renómbralo, añade tu cadena de compatibilidad y verifica que el caso trivial hace el attach. Después porta la funcionalidad real de a una pieza a la vez. Casi siempre encontrarás el bug en el proceso.

### Cerrando esta sección

La Sección 6 te ha proporcionado las herramientas y los hábitos de un depurador de drivers embebidos. Dispones de `ofwdump` para el árbol, `hw.fdt.dtb` para el blob en bruto, `devinfo -r` para la vista de dispositivos adjuntos, `MODULE_DEPEND` para el orden, y `device_printf` para visibilidad ad hoc. También tienes una lista de comprobación mental para los fallos habituales de probe y detach.

La Sección 7 reúne ahora toda la teoría del capítulo en un único ejemplo práctico: un driver de LED respaldado por GPIO que construyes, compilas, cargas y controlas desde un overlay `.dts`. Si has seguido este capítulo de forma secuencial, el ejemplo te parecerá una síntesis directa de las piezas que ya hemos visto.

## 7. Ejemplo práctico: driver GPIO para una placa embebida

Esta sección recorre la construcción completa de un driver pequeño pero real llamado `edled` (LED embebido). El driver:

1. Empareja un nodo del Device Tree con `compatible = "example,edled"`.
2. Adquiere un pin GPIO listado en la propiedad `leds-gpios` del nodo.
3. Expone un parámetro `sysctl` que el usuario puede activar para cambiar el estado del LED.
4. Libera el GPIO limpiamente en el detach.

El driver es deliberadamente mínimo. Una vez que funcione, podrás adaptarlo para controlar cualquier cosa que se encuentre detrás de un único GPIO, y los patrones escalan cuando necesitas gestionar múltiples pines, interrupciones o periféricos más elaborados.

### Qué necesitas

Para seguir los pasos necesitas:

- Un sistema FreeBSD con kernel 14.3 o posterior.
- Las fuentes del kernel instaladas en `/usr/src`.
- `dtc` del port `devel/dtc` o similar.
- Una placa con al menos un pin GPIO libre y un LED (o puedes probar el interruptor sysctl sin un LED real; el driver igualmente hace el attach y registra los cambios de estado).

Si tienes una Raspberry Pi 4 con FreeBSD, GPIO 18 es una elección conveniente porque no colisiona con la consola por defecto ni con el controlador de tarjeta SD. Una Pi 3 o Pi Zero 2 funciona de la misma manera con números GPIO ajustados. En una BeagleBone Black, elige cualquiera de los numerosos pines libres del conector de 46 vías.

### La disposición general de archivos

Produciremos cinco archivos:

```text
edled.c            <- C source
Makefile           <- Kernel module Makefile
edled.dts          <- DT overlay source
edled.dtbo         <- Compiled overlay (output)
README             <- Notes for the reader
```

La disposición correspondiente en el repositorio bajo el árbol `examples/` es:

```text
examples/part-07/ch32-fdt-embedded/lab04-edled/
    edled.c
    edled.dts
    Makefile
    README.md
```

Puedes copiar los archivos de ese árbol una vez que llegues a la sección de laboratorio al final del capítulo.

### El softc

Cada instancia del driver necesita un pequeño bloque de estado. El softc de `edled` contiene:

- El handle del propio dispositivo.
- El descriptor del pin GPIO.
- El estado actual de encendido/apagado.
- El oid sysctl para el control.

```c
struct edled_softc {
    device_t        sc_dev;
    gpio_pin_t      sc_pin;
    int             sc_on;
    struct sysctl_oid *sc_oid;
};
```

`gpio_pin_t` está definido en `/usr/src/sys/dev/gpio/gpiobusvar.h`. Es un handle opaco que contiene la referencia al controlador GPIO, el número de pin y el flag de activo-alto/activo-bajo. Nunca lo desreferencias directamente; lo pasas a `gpio_pin_setflags`, `gpio_pin_set_active` y `gpio_pin_release`.

### Cabeceras

La parte superior de `edled.c` incluye las definiciones que necesitamos:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <dev/gpio/gpiobusvar.h>
```

En comparación con el esqueleto `fdthello` de la Sección 4, añadimos `<sys/sysctl.h>` para el parámetro y `<dev/gpio/gpiobusvar.h>` para las API de consumidor GPIO.

### La tabla de compatibilidad

Una pequeña tabla con una sola entrada es todo lo que necesita este driver:

```c
static const struct ofw_compat_data compat_data[] = {
    {"example,edled", 1},
    {NULL,            0}
};
```

Un proyecto real elegiría un prefijo de proveedor propio. Usar `"example,"` marca el compatible como ilustrativo. Cuando distribuyas un producto, reemplázalo con el prefijo de tu empresa o proyecto.

### El probe

El probe usa el mismo patrón que `fdthello`:

```c
static int
edled_probe(device_t dev)
{
    if (!ofw_bus_status_okay(dev))
        return (ENXIO);

    if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
        return (ENXIO);

    device_set_desc(dev, "Example embedded LED");
    return (BUS_PROBE_DEFAULT);
}
```

No hay nada nuevo aquí. La única razón para copiar el probe literalmente de la Sección 4 es destacar lo repetitivo que es este paso entre drivers; las diferencias significativas entre drivers casi siempre residen en attach, detach y la capa de operaciones.

### El attach

El attach es donde ocurre el trabajo real. Asignamos e inicializamos el softc, adquirimos el pin GPIO, lo configuramos como salida, lo ponemos en «apagado», publicamos el sysctl e imprimimos una confirmación.

```c
static int
edled_attach(device_t dev)
{
    struct edled_softc *sc = device_get_softc(dev);
    phandle_t node = ofw_bus_get_node(dev);
    int error;

    sc->sc_dev = dev;
    sc->sc_on = 0;

    error = gpio_pin_get_by_ofw_property(dev, node,
        "leds-gpios", &sc->sc_pin);
    if (error != 0) {
        device_printf(dev, "cannot get GPIO pin: %d\n", error);
        return (error);
    }

    error = gpio_pin_setflags(sc->sc_pin, GPIO_PIN_OUTPUT);
    if (error != 0) {
        device_printf(dev, "cannot set pin flags: %d\n", error);
        gpio_pin_release(sc->sc_pin);
        return (error);
    }

    error = gpio_pin_set_active(sc->sc_pin, 0);
    if (error != 0) {
        device_printf(dev, "cannot set pin state: %d\n", error);
        gpio_pin_release(sc->sc_pin);
        return (error);
    }

    sc->sc_oid = SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
        SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
        OID_AUTO, "state",
        CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_NEEDGIANT,
        sc, 0, edled_sysctl_state, "I", "LED state (0=off, 1=on)");

    device_printf(dev, "attached, GPIO pin acquired, state=0\n");
    return (0);
}
```

Hay varias cosas que merece la pena examinar en este código.

La llamada `gpio_pin_get_by_ofw_property(dev, node, "leds-gpios", &sc->sc_pin)` analiza la propiedad `leds-gpios` del nodo DT, resuelve el phandle al controlador GPIO, consume el número de pin y produce un handle listo para usar. Si el controlador aún no ha hecho el attach, esta llamada devuelve `ENXIO`, razón por la que expresamos una `MODULE_DEPEND` sobre `gpiobus` durante el registro.

`gpio_pin_setflags(sc->sc_pin, GPIO_PIN_OUTPUT)` configura la dirección del pin. Otros flags válidos incluyen `GPIO_PIN_INPUT`, `GPIO_PIN_PULLUP` y `GPIO_PIN_PULLDOWN`. Puedes combinarlos, por ejemplo `GPIO_PIN_INPUT | GPIO_PIN_PULLUP`.

`gpio_pin_set_active(sc->sc_pin, 0)` pone el pin en su nivel inactivo. «Activo» aquí tiene en cuenta la polaridad, de modo que en un pin configurado como activo-bajo, un valor de `1` lleva la línea a nivel bajo y `0` la lleva a nivel alto. La celda de flags del DT que comentamos anteriormente es lo que determina esto.

`SYSCTL_ADD_PROC` crea un nodo en `dev.edled.<unit>.state` cuyo handler es nuestra propia función `edled_sysctl_state`. El flag `CTLFLAG_NEEDGIANT` es apropiado para un driver pequeño que aún no tiene un lock adecuado; un driver de producción usaría un mutex dedicado y eliminaría el flag Giant.

Si algún paso falla, liberamos lo que ya habíamos adquirido y devolvemos el error. Dejar escapar un pin GPIO en la ruta de error impediría que otros drivers usasen alguna vez la misma línea.

### El handler sysctl

El handler sysctl lee o escribe el estado del LED:

```c
static int
edled_sysctl_state(SYSCTL_HANDLER_ARGS)
{
    struct edled_softc *sc = arg1;
    int val = sc->sc_on;
    int error;

    error = sysctl_handle_int(oidp, &val, 0, req);
    if (error != 0 || req->newptr == NULL)
        return (error);

    if (val != 0 && val != 1)
        return (EINVAL);

    error = gpio_pin_set_active(sc->sc_pin, val);
    if (error == 0)
        sc->sc_on = val;
    return (error);
}
```

`SYSCTL_HANDLER_ARGS` se expande a la firma estándar del handler sysctl. Leemos el valor actual en una variable local, llamamos a `sysctl_handle_int` para hacer la copia en el espacio de usuario y, si el usuario proporcionó un valor nuevo, lo validamos y lo aplicamos a través de la API GPIO. El estado actual se mantiene en el softc, de modo que una lectura sin escritura devuelve el último valor que establecimos.

### El detach

El detach debe liberar todo lo que adquirió el attach, en orden inverso:

```c
static int
edled_detach(device_t dev)
{
    struct edled_softc *sc = device_get_softc(dev);

    if (sc->sc_pin != NULL) {
        (void)gpio_pin_set_active(sc->sc_pin, 0);
        gpio_pin_release(sc->sc_pin);
        sc->sc_pin = NULL;
    }
    device_printf(dev, "detached\n");
    return (0);
}
```

Apagamos el LED antes de liberar el pin. Dejarlo encendido durante una descarga del módulo es una mala práctica para el siguiente driver; peor aún, el pin se libera mientras está activo, y lo que esté controlando permanece encendido hasta que algo más reclame la línea. El contexto sysctl es propiedad del sistema newbus a través de `device_get_sysctl_ctx`, por lo que no liberamos el oid explícitamente; newbus lo destruye por nosotros.

### La tabla de métodos y el registro del driver

Nada sorprendente aquí:

```c
static device_method_t edled_methods[] = {
    DEVMETHOD(device_probe,  edled_probe),
    DEVMETHOD(device_attach, edled_attach),
    DEVMETHOD(device_detach, edled_detach),
    DEVMETHOD_END
};

static driver_t edled_driver = {
    "edled",
    edled_methods,
    sizeof(struct edled_softc)
};

DRIVER_MODULE(edled, simplebus, edled_driver, 0, 0);
DRIVER_MODULE(edled, ofwbus,    edled_driver, 0, 0);
MODULE_DEPEND(edled, gpiobus, 1, 1, 1);
MODULE_VERSION(edled, 1);
```

La única adición respecto a `fdthello` es `MODULE_DEPEND(edled, gpiobus, 1, 1, 1)`. Los tres argumentos enteros son la versión mínima, preferida y máxima de `gpiobus` que `edled` puede tolerar. Un triplete de valores `1, 1, 1` significa «cualquier versión en 1 o superior». En la práctica, esto es casi siempre lo que deseas.

### El código fuente completo

Reuniéndolo todo:

```c
/*
 * edled.c - Example Embedded LED Driver
 *
 * Demonstrates a minimal FDT-driven GPIO consumer on FreeBSD 14.3.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <dev/gpio/gpiobusvar.h>

struct edled_softc {
    device_t        sc_dev;
    gpio_pin_t      sc_pin;
    int             sc_on;
    struct sysctl_oid *sc_oid;
};

static const struct ofw_compat_data compat_data[] = {
    {"example,edled", 1},
    {NULL,            0}
};

static int edled_sysctl_state(SYSCTL_HANDLER_ARGS);

static int
edled_probe(device_t dev)
{
    if (!ofw_bus_status_okay(dev))
        return (ENXIO);
    if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
        return (ENXIO);
    device_set_desc(dev, "Example embedded LED");
    return (BUS_PROBE_DEFAULT);
}

static int
edled_attach(device_t dev)
{
    struct edled_softc *sc = device_get_softc(dev);
    phandle_t node = ofw_bus_get_node(dev);
    int error;

    sc->sc_dev = dev;
    sc->sc_on = 0;

    error = gpio_pin_get_by_ofw_property(dev, node,
        "leds-gpios", &sc->sc_pin);
    if (error != 0) {
        device_printf(dev, "cannot get GPIO pin: %d\n", error);
        return (error);
    }

    error = gpio_pin_setflags(sc->sc_pin, GPIO_PIN_OUTPUT);
    if (error != 0) {
        device_printf(dev, "cannot set pin flags: %d\n", error);
        gpio_pin_release(sc->sc_pin);
        return (error);
    }

    error = gpio_pin_set_active(sc->sc_pin, 0);
    if (error != 0) {
        device_printf(dev, "cannot set pin state: %d\n", error);
        gpio_pin_release(sc->sc_pin);
        return (error);
    }

    sc->sc_oid = SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
        SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
        OID_AUTO, "state",
        CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_NEEDGIANT,
        sc, 0, edled_sysctl_state, "I", "LED state (0=off, 1=on)");

    device_printf(dev, "attached, GPIO pin acquired, state=0\n");
    return (0);
}

static int
edled_detach(device_t dev)
{
    struct edled_softc *sc = device_get_softc(dev);

    if (sc->sc_pin != NULL) {
        (void)gpio_pin_set_active(sc->sc_pin, 0);
        gpio_pin_release(sc->sc_pin);
        sc->sc_pin = NULL;
    }
    device_printf(dev, "detached\n");
    return (0);
}

static int
edled_sysctl_state(SYSCTL_HANDLER_ARGS)
{
    struct edled_softc *sc = arg1;
    int val = sc->sc_on;
    int error;

    error = sysctl_handle_int(oidp, &val, 0, req);
    if (error != 0 || req->newptr == NULL)
        return (error);

    if (val != 0 && val != 1)
        return (EINVAL);

    error = gpio_pin_set_active(sc->sc_pin, val);
    if (error == 0)
        sc->sc_on = val;
    return (error);
}

static device_method_t edled_methods[] = {
    DEVMETHOD(device_probe,  edled_probe),
    DEVMETHOD(device_attach, edled_attach),
    DEVMETHOD(device_detach, edled_detach),
    DEVMETHOD_END
};

static driver_t edled_driver = {
    "edled",
    edled_methods,
    sizeof(struct edled_softc)
};

DRIVER_MODULE(edled, simplebus, edled_driver, 0, 0);
DRIVER_MODULE(edled, ofwbus,    edled_driver, 0, 0);
MODULE_DEPEND(edled, gpiobus, 1, 1, 1);
MODULE_VERSION(edled, 1);
```

Alrededor de 140 líneas de C, incluyendo cabeceras y líneas en blanco. Eso es un driver FDT GPIO funcional, con forma de producción.

### El Makefile

Como con cada módulo del kernel en este libro, el Makefile es trivial:

```makefile
KMOD=   edled
SRCS=   edled.c

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
```

`bsd.kmod.mk` se encarga del resto. Escribir `make` en el directorio produce `edled.ko` y `edled.ko.debug`.

### El código fuente del overlay

El overlay `.dts` complementario tiene este aspecto (ajustado para una Raspberry Pi 4; modifícalo según tu placa):

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2711";

    fragment@0 {
        target-path = "/soc";
        __overlay__ {
            edled0: edled@0 {
                compatible = "example,edled";
                status = "okay";
                leds-gpios = <&gpio 18 0>;
                label = "lab-indicator";
            };
        };
    };
};
```

Compílalo con:

```console
$ dtc -I dts -O dtb -@ -o edled.dtbo edled.dts
```

y cópialo en `/boot/dtb/overlays/`.

### Compilación y carga

En el sistema de destino, pon los cuatro archivos en un directorio de trabajo y después:

```console
$ make
$ sudo cp edled.dtbo /boot/dtb/overlays/
$ sudo sh -c 'echo fdt_overlays=\"edled\" >> /boot/loader.conf'
$ sudo reboot
```

Tras el reinicio, deberías ver:

```console
# dmesg | grep edled
edled0: <Example embedded LED> on simplebus0
edled0: attached, GPIO pin acquired, state=0
```

Si has conectado un LED al GPIO 18, está actualmente apagado. Compruébalo con:

```console
# sysctl dev.edled.0.state
dev.edled.0.state: 0
```

Enciéndelo:

```console
# sysctl dev.edled.0.state=1
dev.edled.0.state: 0 -> 1
```

El LED se enciende. Apágalo:

```console
# sysctl dev.edled.0.state=0
dev.edled.0.state: 1 -> 0
```

Hecho. Tienes un driver embebido completamente funcional de extremo a extremo: desde el código fuente del Device Tree, pasando por el módulo del kernel, hasta el control en el espacio de usuario.

### Cómo inspeccionar el dispositivo resultante

Algunas consultas útiles para confirmar que el driver está bien integrado:

```console
# devinfo -r | grep -A1 simplebus
# sysctl dev.edled.0
# ofwdump -p /soc/edled@0
```

El primero muestra tu driver en el árbol de Newbus. El segundo lista todos los sysctls registrados por tu driver. El tercero confirma que el nodo DT tiene las propiedades esperadas.

### Errores habituales que conviene conocer

Los siguientes errores son clásicos al escribir tu primer driver consumidor de GPIO. Cada uno es fácil de cometer y, una vez que sabes dónde buscarlo, fácil de evitar.

**Olvidarse de liberar el pin en detach.** `kldunload` tiene éxito, pero el pin queda bloqueado. La siguiente carga notifica "pin busy". Empareja siempre cada `gpio_pin_get_*` con un `gpio_pin_release` en detach.

**Leer la propiedad DT antes de que el bus padre haya completado su attach.** El análisis de `leds-gpios` devuelve `ENXIO`. La solución es garantizar que el módulo del controlador GPIO se cargue primero, mediante `MODULE_DEPEND`. Durante el arranque esto ocurre de forma automática porque el kernel estático tiene ambos residentes; en experimentos de carga manual con `kldload`, puede que necesites cargar `gpiobus` explícitamente primero.

**Confundir el indicador de nivel activo.** En placas que cablean el LED de manera que el GPIO drena la corriente (LED entre `3V3` y el pin), "encendido" corresponde a una salida en nivel bajo. En ese caso, `leds-gpios = <&gpio 18 1>` es correcto y `gpio_pin_set_active(sc->sc_pin, 1)` llevará el pin a nivel bajo, encendiendo el LED. Si el LED se comporta de forma inversa, invierte el indicador.

**Sysctls que modifican el estado sin un lock.** Este driver usa `CTLFLAG_NEEDGIANT` como atajo. En un driver real, asignas un `struct mtx`, lo tomas en el handler del sysctl alrededor de la llamada GPIO, y publicas el sysctl sin el indicador Giant. Para un LED con un único GPIO no tiene demasiada importancia en la práctica, pero el patrón es importante en cuanto amplías el driver para gestionar interrupciones o estado compartido.

### Cerrando esta sección

La sección 7 ha cumplido la promesa del capítulo. Has construido un consumidor GPIO completo orientado por FDT, lo has desplegado mediante un overlay, lo has cargado en un sistema en ejecución y lo has ejercitado desde el espacio de usuario. Los componentes que has utilizado (tablas de compatibilidad, helpers de OFW, adquisición de recursos a través de frameworks de consumo, registro de sysctls, registro en newbus) son los mismos de los que depende cualquier driver embebido en FreeBSD.

La sección 8 explica cómo convertir un driver funcional como `edled` en uno robusto. Funcionar es el primer hito. Ser robusto es el que le gana a un driver su lugar en el árbol del kernel.

## 8. Refactorización y finalización de tu driver embebido

El driver de la sección 7 funciona. Cargas el módulo, cambias un sysctl y el LED responde. Eso es un logro real, y si el objetivo es un experimento puntual sobre un banco de pruebas, puedes detenerte ahí. Para cualquier cosa más seria, un driver funcional debe convertirse en un driver *terminado*: uno que pueda leer un desconocido, auditarlo un revisor, y en el que se pueda confiar en un sistema que lleva meses funcionando.

Esta sección recorre las pasadas de refactorización que llevan a `edled` de funcional a terminado. Los cambios no consisten en añadir funcionalidad. Consisten en hacerlo bien. El mismo proceso se aplica a cualquier driver que escribas, incluidos los que adaptes de otros proyectos o portes desde Linux.

### Qué significa refactorizar aquí

"Refactorizar" es una de esas palabras que a menudo abarca cualquier cambio que se le antoje al que la usa. A efectos de esta sección, refactorizar significa:

1. Eliminar errores latentes que no se manifiestan en el camino feliz.
2. Añadir el locking y los caminos de error que requiere un driver en producción.
3. Mejorar nombres, disposición y comentarios para que el siguiente lector no tenga que adivinar.
4. Mover infraestructura fuera de attach hacia funciones auxiliares cuando el cuerpo ha crecido demasiado.

Nada de esto modifica el comportamiento externo del driver. El sysctl sigue leyendo y escribiendo el mismo entero, el LED sigue encendiéndose y apagándose, y el enlace DT no cambia. Lo que cambia es lo fiable que resulta el driver cuando ocurre algo inesperado.

### Primera pasada: ajustar los caminos de error en attach

La función attach original acumuló un grupo de manejadores de error que cada uno llama a `gpio_pin_release` y devuelve. Funciona, pero duplica la limpieza. Una forma más limpia usa un único bloque de salida con etiquetas:

```c
static int
edled_attach(device_t dev)
{
    struct edled_softc *sc = device_get_softc(dev);
    phandle_t node = ofw_bus_get_node(dev);
    int error;

    sc->sc_dev = dev;
    sc->sc_on = 0;

    error = gpio_pin_get_by_ofw_property(dev, node,
        "leds-gpios", &sc->sc_pin);
    if (error != 0) {
        device_printf(dev, "cannot get GPIO pin: %d\n", error);
        goto fail;
    }

    error = gpio_pin_setflags(sc->sc_pin, GPIO_PIN_OUTPUT);
    if (error != 0) {
        device_printf(dev, "cannot set pin flags: %d\n", error);
        goto fail;
    }

    error = gpio_pin_set_active(sc->sc_pin, 0);
    if (error != 0) {
        device_printf(dev, "cannot set pin state: %d\n", error);
        goto fail;
    }

    sc->sc_oid = SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
        SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
        OID_AUTO, "state",
        CTLTYPE_INT | CTLFLAG_RW,
        sc, 0, edled_sysctl_state, "I", "LED state (0=off, 1=on)");

    device_printf(dev, "attached, state=0\n");
    return (0);

fail:
    if (sc->sc_pin != NULL) {
        gpio_pin_release(sc->sc_pin);
        sc->sc_pin = NULL;
    }
    return (error);
}
```

El patrón `goto fail` es el estilo idiomático del kernel FreeBSD. Concentra la lógica de limpieza en un único lugar, de modo que resulta imposible que una edición futura pierda un recurso por olvidarse de una de varias llamadas `release` idénticas.

### Segunda pasada: añadir locking correcto

`CTLFLAG_NEEDGIANT` era un atajo. El enfoque correcto es un mutex por softc tomado alrededor del acceso al hardware:

```c
struct edled_softc {
    device_t        sc_dev;
    gpio_pin_t      sc_pin;
    int             sc_on;
    struct sysctl_oid *sc_oid;
    struct mtx      sc_mtx;
};
```

Inicializa el mutex en attach:

```c
mtx_init(&sc->sc_mtx, device_get_nameunit(dev), "edled", MTX_DEF);
```

Destrúyelo en detach:

```c
mtx_destroy(&sc->sc_mtx);
```

Tómalo en el handler del sysctl alrededor de la llamada al hardware:

```c
static int
edled_sysctl_state(SYSCTL_HANDLER_ARGS)
{
    struct edled_softc *sc = arg1;
    int val, error;

    mtx_lock(&sc->sc_mtx);
    val = sc->sc_on;
    mtx_unlock(&sc->sc_mtx);

    error = sysctl_handle_int(oidp, &val, 0, req);
    if (error != 0 || req->newptr == NULL)
        return (error);

    if (val != 0 && val != 1)
        return (EINVAL);

    mtx_lock(&sc->sc_mtx);
    error = gpio_pin_set_active(sc->sc_pin, val);
    if (error == 0)
        sc->sc_on = val;
    mtx_unlock(&sc->sc_mtx);

    return (error);
}
```

Observa que soltamos el mutex alrededor de `sysctl_handle_int`. Esa llamada puede copiar datos hacia o desde el espacio de usuario, lo que puede bloquearse, y no debes mantener un mutex durante un bloqueo. El valor que pasamos a `sysctl_handle_int` es una copia local, de modo que soltarlo es seguro.

Elimina `CTLFLAG_NEEDGIANT` de la llamada a `SYSCTL_ADD_PROC`. Con un lock real en su lugar, Giant ya no es necesario.

### Tercera pasada: gestionar explícitamente el rail de alimentación

En muchos periféricos reales, el driver es responsable de activar el rail de alimentación y el reloj de referencia antes de acceder al dispositivo. FreeBSD proporciona APIs de consumo en `/usr/src/sys/dev/extres/regulator/` y `/usr/src/sys/dev/extres/clk/` exactamente para este fin. Aunque un LED discreto no necesita un regulador, los periféricos más complejos (por ejemplo, un acelerómetro conectado por SPI) sí lo necesitan. Para que `edled` siga siendo una plantilla de enseñanza útil, mostramos cómo encaja este mecanismo.

Bajo un nodo DT hipotético:

```dts
edled0: edled@0 {
    compatible = "example,edled";
    status = "okay";
    leds-gpios = <&gpio 18 0>;
    vled-supply = <&ldo_led>;
    clocks = <&clks 42>;
    label = "lab-indicator";
};
```

Dos propiedades adicionales: `vled-supply` hace referencia a un phandle de regulador, y `clocks` hace referencia a un phandle de reloj. Attach los recoge así:

```c
#include <dev/extres/clk/clk.h>
#include <dev/extres/regulator/regulator.h>

struct edled_softc {
    ...
    regulator_t     sc_reg;
    clk_t           sc_clk;
};

...

    error = regulator_get_by_ofw_property(dev, node, "vled-supply",
        &sc->sc_reg);
    if (error == 0) {
        error = regulator_enable(sc->sc_reg);
        if (error != 0) {
            device_printf(dev, "cannot enable regulator: %d\n",
                error);
            goto fail;
        }
    } else if (error != ENOENT) {
        device_printf(dev, "regulator lookup failed: %d\n", error);
        goto fail;
    }

    error = clk_get_by_ofw_index(dev, node, 0, &sc->sc_clk);
    if (error == 0) {
        error = clk_enable(sc->sc_clk);
        if (error != 0) {
            device_printf(dev, "cannot enable clock: %d\n", error);
            goto fail;
        }
    } else if (error != ENOENT) {
        device_printf(dev, "clock lookup failed: %d\n", error);
        goto fail;
    }
```

Detach los libera en orden inverso:

```c
    if (sc->sc_clk != NULL) {
        clk_disable(sc->sc_clk);
        clk_release(sc->sc_clk);
    }
    if (sc->sc_reg != NULL) {
        regulator_disable(sc->sc_reg);
        regulator_release(sc->sc_reg);
    }
```

La comprobación de `ENOENT` es importante. Si el DT no declara un regulador o un reloj, `regulator_get_by_ofw_property` y `clk_get_by_ofw_index` devuelven `ENOENT`. Un driver que admite múltiples placas, algunas con y algunas sin un rail dedicado, trata `ENOENT` como "no necesario aquí" en lugar de como un error fatal.

### Cuarta pasada: configuración de pinmux

En SoCs donde los pines GPIO pueden reasignarse como UART, SPI, I2C u otras funciones, el controlador de multiplexación de pines debe programarse antes de que el driver pueda usar un pin. FreeBSD gestiona esto a través del framework `pinctrl` en `/usr/src/sys/dev/fdt/fdt_pinctrl.h`. Un nodo Device Tree que solicita una configuración específica usa las propiedades `pinctrl-names` y `pinctrl-N`:

```dts
edled0: edled@0 {
    compatible = "example,edled";
    pinctrl-names = "default";
    pinctrl-0 = <&edled_pins>;
    ...
};

&pinctrl {
    edled_pins: edled_pins {
        brcm,pins = <18>;
        brcm,function = <1>;  /* GPIO output */
    };
};
```

En attach, llama a:

```c
fdt_pinctrl_configure_by_name(dev, "default");
```

antes de cualquier acceso a pines. El framework recorre el handle `pinctrl-0`, encuentra el nodo referenciado y aplica su configuración a través del driver pinctrl específico del SoC.

El ejemplo del LED no necesita estrictamente pinmux porque el driver GPIO de Broadcom configura los pines como parte de `gpio_pin_setflags`, pero en OMAP, Allwinner y muchos otros SoCs es imprescindible. Incluye el patrón en tu plantilla de enseñanza para que los lectores vean dónde encaja.

### Quinta pasada: auditoría de estilo y nomenclatura

Lee el código fuente final con calma. Aspectos a revisar:

- **Nomenclatura coherente.** Todas las funciones empiezan por `edled_`, todos los campos por `sc_`, todas las constantes en mayúsculas. Un desconocido que lea el código fuente nunca debe preguntarse a qué driver pertenece un símbolo.
- **Sin código muerto.** Elimina cualquier `device_printf` o función de relleno que fuera útil durante la puesta en marcha inicial y no tenga finalidad en producción.
- **Sin números mágicos.** Si escribes `sc->sc_on = 0` en diez sitios, define un enum o al menos un `#define EDLED_OFF 0`.
- **Comentarios breves solo donde la intención del código no sea evidente.** Intentar añadir un docstring a cada función tiende a saturar los fuentes de FreeBSD; la brevedad es el estilo de la casa.
- **Orden correcto de includes.** Por convención, `<sys/param.h>` va primero, seguido de otros headers `<sys/...>`, luego `<machine/...>` y después los headers específicos del subsistema, como `<dev/ofw/...>`.
- **Longitud de línea.** Limítate a líneas de 80 columnas. Las llamadas a funciones largas usan el estilo de indentación FreeBSD para las continuaciones.
- **Cabecera de licencia.** Todo archivo fuente de FreeBSD comienza con el bloque de licencia de estilo BSD del proyecto. Para drivers fuera del árbol, incluye tu propio aviso de copyright y licencia.

### Sexta pasada: análisis estático

Ejecuta el compilador con los avisos al máximo:

```console
$ make CFLAGS="-Wall -Wextra -Werror"
```

Corrige cada aviso. Los avisos indican o un error real o un fragmento de código poco claro. En ambos casos, la corrección mejora el driver.

Considera ejecutar scan-build:

```console
$ scan-build make
```

`scan-build` forma parte del analizador clang de LLVM. Detecta desreferencias de puntero nulo y errores de uso después de liberación que el compilador pasa por alto.

### Séptima pasada: documentación

Un driver no está terminado hasta que puede entenderse sin leer el código. Escribe un README de una página que cubra:

- Qué hace el driver.
- Qué enlace DT espera.
- Qué dependencias de módulo tiene.
- Cualquier limitación conocida o nota específica de la placa.
- Cómo compilarlo, cargarlo y probarlo.

Incluye también una página man breve para el árbol de material complementario. Incluso una página `edled(4)` de relleno es valiosa; puedes refinarla más adelante.

### Empaquetado y distribución

Los drivers fuera del árbol residen en unos pocos lugares canónicos:

- Como un port informal de `devel/`, para que los usuarios lo instalen sobre FreeBSD.
- Como un repositorio de GitHub siguiendo la disposición convencional del proyecto FreeBSD.
- Como un archivo `.tar.gz` publicado junto con un README y un archivo INSTALL.

El árbol de ports de FreeBSD acepta paquetes de drivers que sean estables. Presentar un port `devel/edled-kmod` cuando el driver lleve un tiempo rodando es un objetivo razonable.

Si tu driver es lo suficientemente general como para beneficiar a otros usuarios, considera contribuirlo upstream. El proceso de revisión es minucioso pero positivo, y la lista de correo `freebsd-drivers@freebsd.org` es el punto de partida natural.

### Comparación con drivers reales de FreeBSD

Una vez que `edled` esté pulido, compáralo con `/usr/src/sys/dev/gpio/gpioled_fdt.c`, que es el driver que inspiró el ejemplo. El driver real es algo mayor porque admite varios LEDs por nodo padre, pero su forma general coincide con la tuya. Observa cómo:

- Usa `for (child = OF_child(leds); child != 0; child = OF_peer(child))` para iterar los hijos DT.
- Llama a `OF_getprop_alloc` para leer una cadena de etiqueta de longitud variable.
- Se registra tanto en `simplebus` como en `ofwbus` mediante `DRIVER_MODULE`.
- Declara su enlace DT mediante `SIMPLEBUS_PNP_INFO` para que la correspondencia de IDs de dispositivo funcione con `devmatch(8)`.

Leer drivers reales en detalle después de terminar el tuyo es una de las cosas más productivas que puedes hacer en este campo. Encontrarás técnicas que nunca has visto y reconocerás patrones que ahora comprendes desde dentro.

### Cerrando esta sección

La sección 8 ha recorrido las pasadas de finalización que necesita cualquier driver. Caminos de error ajustados, locking corregido, gestión de alimentación y reloj hecha explícita, pinmux considerado, estilo auditado, análisis ejecutado, documentación redactada. Lo que tienes ahora ya no es un experimento; es un driver que podrías entregar a otra persona con la frente alta.

En este punto el material técnico del capítulo está completo. Las piezas que quedan son los laboratorios prácticos que te permiten ejecutar todo tú mismo, los ejercicios de desafío que amplían lo aprendido, una breve lista de errores habituales a tener en cuenta, y el resumen final que cierra el círculo con el arco más amplio del libro.

## 9. Lectura de drivers FDT reales de FreeBSD

Hemos construido `fdthello` y `edled`, dos drivers que existen con fines didácticos. Son reales en el sentido de que puedes cargarlos en un sistema FreeBSD y ver cómo completan su attach, pero son pequeños y no llevan consigo la sabiduría acumulada de drivers que han vivido en el árbol durante años y han sido modificados por decenas de colaboradores. Para completar tu aprendizaje como escritor de drivers FDT, necesitas leer drivers que no nacieron como material didáctico.

Esta sección selecciona unos pocos drivers de `/usr/src/sys` y recorre lo que revelan. El objetivo no es que memorices su código fuente, sino desarrollar el hábito de leer código real como fuente principal de aprendizaje. Los ejemplos didácticos del libro se desvanecerán de la memoria en pocos meses; leer drivers reales es una habilidad que puedes usar durante el resto de tu carrera.

### gpioled_fdt.c: un pariente cercano de edled

Nuestro driver `edled` se modeló deliberadamente sobre `/usr/src/sys/dev/gpio/gpioled_fdt.c`. Leer el original con `edled` en mente hace que la comparación sea instructiva. El driver real tiene unas 150 líneas, casi el mismo tamaño que el nuestro, pero gestiona varios detalles que nosotros elegimos simplificar.

La tabla de compatibilidad del driver lista una sola entrada:

```c
static struct ofw_compat_data compat_data[] = {
    {"gpio-leds", 1},
    {NULL,        0}
};
```

Observa que `gpio-leds` es una cadena sin prefijo de fabricante. Esto refleja un binding de larga tradición en la comunidad que es anterior a la convención actual de prefijos de fabricante. Los nuevos bindings deben usar siempre un prefijo, pero los establecidos permanecen como están para mantener la compatibilidad.

El probe es casi idéntico al nuestro:

```c
static int
gpioled_fdt_probe(device_t dev)
{
    if (!ofw_bus_status_okay(dev))
        return (ENXIO);
    if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
        return (ENXIO);
    device_set_desc(dev, "OFW GPIO LEDs");
    return (BUS_PROBE_DEFAULT);
}
```

La función attach es donde los drivers divergen. `gpioled_fdt.c` soporta varios LEDs por nodo DT, siguiendo el binding `gpio-leds` que lista cada LED como nodo hijo de un único padre. El patrón es:

```c
static int
gpioled_fdt_attach(device_t dev)
{
    struct gpioled_softc *sc = device_get_softc(dev);
    phandle_t leds, child;
    ...

    leds = ofw_bus_get_node(dev);
    sc->sc_dev = dev;
    sc->sc_nleds = 0;

    for (child = OF_child(leds); child != 0; child = OF_peer(child)) {
        if (!OF_hasprop(child, "gpios"))
            continue;
        ...
    }
}
```

`OF_child` y `OF_peer` son los recorridos clásicos para iterar los hijos de un Device Tree. `OF_child(parent)` devuelve el primer hijo o cero. `OF_peer(node)` devuelve el siguiente hermano o cero cuando se llega al final. Este modismo de iteración de dos líneas es la columna vertebral de todo driver que procesa un número variable de subentradas.

Dentro del bucle, el driver lee las propiedades de cada LED:

```c
    name = NULL;
    len = OF_getprop_alloc(child, "label", (void **)&name);
    if (len <= 0) {
        OF_prop_free(name);
        len = OF_getprop_alloc(child, "name", (void **)&name);
    }
```

`OF_getprop_alloc` asigna memoria para una propiedad y devuelve la longitud. El código que llama es responsable de liberar el buffer con `OF_prop_free`. Observa el fallback: si no existe la propiedad `label`, el driver usa en su lugar el `name` del nodo. Este tipo de fallback elegante merece atención; hace que los drivers sean más tolerantes ante variaciones del binding.

Cada GPIO se adquiere a continuación mediante una llamada a `gpio_pin_get_by_ofw_idx` con un índice explícito de cero, ya que la propiedad `gpios` de cada LED está indexada desde cero dentro del ámbito de ese hijo. El driver llama a `gpio_pin_setflags(pin, GPIO_PIN_OUTPUT)` y registra cada LED en el framework `led(4)` para que aparezca como `/dev/led/<name>` en el espacio de usuario.

### Registro con DRIVER_MODULE

Las líneas de registro del módulo tienen este aspecto:

```c
static driver_t gpioled_driver = {
    "gpioled",
    gpioled_methods,
    sizeof(struct gpioled_softc)
};

DRIVER_MODULE(gpioled, ofwbus,    gpioled_driver, 0, 0);
DRIVER_MODULE(gpioled, simplebus, gpioled_driver, 0, 0);
MODULE_VERSION(gpioled, 1);
MODULE_DEPEND(gpioled, gpiobus, 1, 1, 1);
SIMPLEBUS_PNP_INFO(compat_data);
```

Destacan dos adiciones. `MODULE_DEPEND(gpioled, gpiobus, 1, 1, 1)` ya lo vimos. La nueva línea es `SIMPLEBUS_PNP_INFO(compat_data)`. Esta macro se expande en un conjunto de metadatos del módulo que herramientas como `devmatch(8)` usan para decidir qué driver cargar automáticamente para un nodo DT dado. El argumento es la misma tabla `compat_data` que usa el probe, de modo que existe una única fuente de verdad.

Cuando escribas drivers de nivel de producción, incluye `SIMPLEBUS_PNP_INFO` para que la carga automática funcione. Sin ella, el driver no se detectará automáticamente y el usuario tendrá que añadirlo explícitamente a `loader.conf`.

### Qué extraer de gpioled_fdt.c

Léelo junto a `edled` y verás:

- Cómo iterar múltiples hijos en un nodo DT.
- Cómo hacer fallback entre nombres de propiedad.
- Cómo usar `OF_getprop_alloc` y `OF_prop_free` para cadenas de longitud variable.
- Cómo registrarse en el framework `led(4)` además de en Newbus.
- Cómo declarar información PNP para el auto-matching.

Estos son cinco patrones que aparecen una y otra vez en los drivers de FreeBSD. Habiéndolos visto una vez en un archivo fuente real, los reconocerás al instante en el próximo driver que abras.

### bcm2835_gpio.c: un proveedor de bus

`edled` y `gpioled_fdt.c` son consumidores de GPIO. *Usan* pines GPIO proporcionados por otro driver. El driver que *proporciona* esos pines en la Raspberry Pi es `/usr/src/sys/arm/broadcom/bcm2835/bcm2835_gpio.c`. Leerlo muestra la otra cara de la transacción.

El attach del driver hace considerablemente más que el nuestro:

- Asigna el recurso MMIO para el bloque de registros del controlador GPIO.
- Asigna todos los recursos de interrupción (el BCM2835 enruta dos líneas de interrupción por banco).
- Inicializa un mutex y una estructura de datos del driver que lleva el seguimiento del estado de cada pin.
- Registra un hijo de bus GPIO para que los consumidores puedan conectarse a él.
- Registra funciones de pinmux para todos los pines con capacidad de multiplexación.

Lo más importante que hay que observar, desde nuestra perspectiva como consumidores, es la forma en que se expone. En lo profundo del attach:

```c
if ((sc->sc_busdev = gpiobus_attach_bus(dev)) == NULL) {
    device_printf(dev, "could not attach GPIO bus\n");
    return (ENXIO);
}
```

`gpiobus_attach_bus(dev)` es lo que crea la instancia de gpiobus contra la que los consumidores realizan probe posteriormente. Sin esta llamada, ningún driver consumidor podría adquirir nunca un pin, porque no habría ningún bus contra el que resolver los phandles.

Al final del archivo, las entradas `DEVMETHOD` mapean los métodos del bus GPIO a las funciones propias del driver:

```c
DEVMETHOD(gpio_pin_set,    bcm_gpio_pin_set),
DEVMETHOD(gpio_pin_get,    bcm_gpio_pin_get),
DEVMETHOD(gpio_pin_toggle, bcm_gpio_pin_toggle),
DEVMETHOD(gpio_pin_getcaps, bcm_gpio_pin_getcaps),
DEVMETHOD(gpio_pin_setflags, bcm_gpio_pin_setflags),
```

Estas son las funciones que nuestro consumidor termina llamando, indirectamente, cada vez que ejecuta `gpio_pin_set_active`. La API de consumidor en `gpiobusvar.h` es una capa delgada sobre esta tabla de DEVMETHOD.

### ofw_iicbus.c: un bus que es padre e hijo a la vez

Muchos controladores I2C se conectan como hijos de `simplebus` (su padre DT) y luego actúan ellos mismos como padre de los drivers individuales de dispositivos I2C. `/usr/src/sys/dev/iicbus/ofw_iicbus.c` es un buen ejemplo para ojear. Muestra cómo un driver puede a la vez:

- Hacer probe y attach en su propio nodo DT como cualquier driver FDT.
- Registrar sus propios dispositivos hijos a partir de los hijos DT de su nodo.

La iteración sobre los hijos usa el mismo modismo `OF_child`/`OF_peer`, pero para cada hijo crea un nuevo dispositivo Newbus con `device_add_child`, configura sus propios metadatos OFW y confía en Newbus para ejecutar el probe de un driver capaz de gestionarlo (por ejemplo, un sensor de temperatura o una EEPROM).

Leer este driver te da una idea de cómo se encadenan las relaciones bus-consumidor. El FDT es un árbol; también lo es la jerarquía Newbus. Un driver en medio del árbol desempeña al mismo tiempo los roles de padre e hijo.

### ofw_bus_subr.c: los propios helpers

Cuando te encuentres buscando constantemente qué hace exactamente `ofw_bus_search_compatible`, la respuesta está en `/usr/src/sys/dev/ofw/ofw_bus_subr.c`. Leer los helpers que usas es una forma subestimada de entender lo que tu driver realmente hace.

Un breve recorrido por los helpers que encontrarás con mayor frecuencia:

- `ofw_bus_is_compatible(dev, str)` devuelve true si la lista `compatible` del nodo contiene `str`. Itera por todas las entradas de la lista de compatibilidad, no solo la primera.
- `ofw_bus_search_compatible(dev, table)` recorre la misma lista `compatible` comparándola con las entradas de una tabla `struct ofw_compat_data` y devuelve un puntero a la entrada coincidente (o a un centinela).
- `ofw_bus_status_okay(dev)` comprueba la propiedad `status`. La ausencia de status equivale a okay; `"okay"` u `"ok"` es aceptable; cualquier otra cosa (`"disabled"`, `"fail"`) no lo es.
- `ofw_bus_has_prop(dev, prop)` comprueba la existencia sin leer el valor.
- `ofw_bus_parse_xref_list_alloc` y los helpers relacionados leen listas de referencias phandle (el formato usado por `clocks`, `resets`, `gpios`, etc.) y devuelven un array asignado que el código que llama debe liberar.

Leer estos helpers confirma que no hay nada mágico en el sistema. Son código C legible que recorre el mismo blob que el kernel interpretó durante el boot.

### simplebus.c: el driver que ejecuta tu driver

Si quieres entender por qué tu driver FDT llega a ser probado, abre `/usr/src/sys/dev/fdt/simplebus.c`. El probe y el attach del propio `simplebus` son breves y, una vez que sabes qué buscar, sorprendentemente concretos.

`simplebus_probe` comprueba que el nodo tiene `compatible = "simple-bus"` (o que es un nodo de clase SoC) y que no tiene peculiaridades específicas del padre. `simplebus_attach` recorre entonces los hijos del nodo, crea un nuevo dispositivo por cada uno, analiza el `reg` y las interrupciones de cada hijo, y llama a `device_probe_and_attach` en el nuevo dispositivo. Esa última llamada es lo que desencadena el probe de tu driver.

Las líneas clave son algo así:

```c
for (node = OF_child(parent); node > 0; node = OF_peer(node)) {
    ...
    child = simplebus_add_device(bus, node, 0, NULL, -1, NULL);
    if (child == NULL)
        continue;
}
```

Esta iteración es lo que convierte un árbol de nodos DT en un árbol de dispositivos Newbus. Todo driver FDT existente entra en el sistema a través de este bucle.

Leer `simplebus.c` desmitifica la pregunta «¿por qué se llama a mi driver?». Ves, en C plano, exactamente cómo el kernel recorre desde el blob en memoria hasta una llamada al probe de tu driver. Si alguna vez necesitas depurar por qué tu probe no se ejecuta, el primer paso es instrumentar este archivo con `device_printf` en el lugar adecuado.

### Un repaso a los drivers que vale la pena leer

Más allá de los drivers específicos anteriores, aquí tienes una breve lista de drivers FDT en `/usr/src/sys` que merecen tu tiempo como objetos de estudio. Cada uno es representativo de un patrón que es probable que encuentres.

- `/usr/src/sys/dev/gpio/gpioiic.c`: Un driver que implementa un bus I2C sobre pines GPIO. Muestra patrones de bit-banging.
- `/usr/src/sys/dev/gpio/gpiokeys.c`: Consume entradas GPIO como si fueran un teclado. Muestra el manejo de interrupciones desde GPIOs.
- `/usr/src/sys/dev/uart/uart_dev_ns8250.c`: Un driver UART independiente de la plataforma con ganchos FDT. Muestra cómo un driver genérico puede aceptar rutas de attach FDT junto con otros tipos de bus.
- `/usr/src/sys/dev/sdhci/sdhci_fdt.c`: Un driver FDT de gran tamaño para controladores de host SD. Muestra cómo los drivers de producción gestionan clocks, resets, reguladores y pinmux de forma conjunta.
- `/usr/src/sys/arm/allwinner/aw_gpio.c`: Un controlador GPIO moderno y completo para la familia de SoC Allwinner. Vale la pena compararlo con `bcm2835_gpio.c` para ver dos enfoques del mismo problema.
- `/usr/src/sys/arm/freescale/imx/imx_gpio.c`: El driver GPIO de i.MX6/7/8, otra referencia bien mantenida.
- `/usr/src/sys/dev/extres/syscon/syscon.c`: Un pseudo-bus de «controlador del sistema» que expone bloques de registros compartidos a múltiples drivers. Útil para ver cómo FreeBSD gestiona patrones DT que no encajan limpiamente en el esquema «un nodo, un driver».

No necesitas leerlos de principio a fin. Un hábito saludable es escoger uno cada semana o dos, hojear la estructura y luego centrarte en cualquier pequeño detalle que llame tu atención. Con el tiempo, estas lecturas construirán en tu mente una biblioteca de código real que has visto funcionar.

### Usar grep como herramienta de estudio

Cuando encuentres una función nueva en un driver que estás leyendo y no sepas con certeza qué hace, un buen primer paso es:

```console
$ grep -rn "function_name" /usr/src/sys | head
```

Esto muestra todos los lugares donde la función está definida y llamada. A menudo la declaración en un header es suficiente, combinada con dos o tres lugares de llamada representativos, para entender para qué sirve la función. Esto supera a buscar en la web, que devuelve documentación desactualizada y publicaciones de foros recordadas a medias.

Para un binding DT concreto, el mismo truco funciona:

```console
$ grep -rn '"gpio-leds"' /usr/src/sys
```

La salida te indica todos los archivos que hacen referencia a esa cadena compat, incluido el driver que la implementa, los overlays que la usan y los tests que la ejercitan.

### Cerrando esta sección

La sección 9 te ha proporcionado una lista de lectura y un método. Los drivers reales de FreeBSD son el recurso más rico disponible, y aprender a leerlos con eficacia es una habilidad tan importante como escribir los propios. Los drivers de la lista anterior muestran los patrones que nuestros ejemplos didácticos simplificaron. Son a los que debes recurrir cuando estés atascado, cuando necesites inspiración y cuando quieras saber cómo un driver de calidad de producción maneja los casos límite que tu propio código aún no ha encontrado.

El material restante del capítulo es práctico: los laboratorios que puedes ejecutar en tu propio hardware, los ejercicios de desafío que amplían el driver didáctico, la referencia de resolución de problemas y el puente de cierre hacia el siguiente capítulo.

## 10. El cableado de interrupciones en sistemas basados en FDT

Hasta ahora hemos tratado las interrupciones como algo opaco. En esta sección abrimos la caja y vemos cómo Device Tree describe la conectividad de interrupciones, cómo el framework de interrupciones de FreeBSD (`intrng`) consume esa descripción y cómo un driver solicita un IRQ que realmente se disparará cuando el hardware necesite atención.

La razón por la que este tema merece su propia sección es que el cableado de interrupciones en los SoC modernos puede volverse complicado. Las plataformas simples tienen un controlador, un conjunto de líneas y una asignación plana. Las plataformas complejas tienen un controlador raíz, varios controladores subsidiarios que multiplexan fuentes IRQ más amplias en salidas más estrechas, y controladores basados en pines (como GPIO usadas como interrupciones) cuyas líneas se encadenan a través del árbol de multiplexación. Un desarrollador de drivers que comprende la cadena puede depurar fallos extraños de interrupción en cuestión de minutos; uno que no la comprende puede pasar horas comprobando las cosas equivocadas.

### El árbol de interrupciones

El Device Tree expresa las interrupciones como un árbol lógico independiente que discurre en paralelo al árbol de direcciones principal. Cada nodo tiene un padre de interrupción (su controlador), y el árbol asciende a través de los controladores hasta llegar al controlador raíz desde el que la CPU recibe efectivamente las excepciones.

Tres propiedades describen el árbol:

- **`interrupts`**: La descripción de la interrupción para este nodo. Su formato depende del controlador al que se conecta.
- **`interrupt-parent`**: Un phandle al controlador, si no es el ancestro más cercano que ya sea un controlador de interrupciones.
- **`interrupt-controller`**: Una propiedad sin valor que marca un nodo como controlador. El `interrupt-parent` de un consumidor debe apuntar a tal nodo.

Un fragmento de ejemplo:

```dts
&soc {
    gic: interrupt-controller@10000 {
        compatible = "arm,gic-v3";
        interrupt-controller;
        #interrupt-cells = <3>;
        reg = <0x10000 0x1000>, <0x11000 0x20000>;
    };

    uart0: serial@20000 {
        compatible = "arm,pl011";
        reg = <0x20000 0x100>;
        interrupts = <0 42 4>;
        interrupt-parent = <&gic>;
    };
};
```

El GIC (Generic Interrupt Controller, el controlador raíz estándar de arm64) declara `#interrupt-cells = <3>`. Cada dispositivo que se conecta a él debe proporcionar tres celdas en su propiedad `interrupts`. Para un GICv3, las tres celdas son *tipo, número, flags*: `<0 42 4>` significa «interrupción de periférico compartido 42, activada por nivel, activa alta».

Si se omite `interrupt-parent`, el padre es el ancestro más cercano que tenga `interrupt-parent` o la propiedad `interrupt-controller` establecida. Esta cadena puede no resultar evidente cuando los drivers se encuentran varios niveles por debajo.

### Encadenamiento de interrupt-parent

Considera un ejemplo más realista. En un BCM2711 (Raspberry Pi 4), el controlador GPIO es su propio controlador de interrupciones: agrega las interrupciones de los pines individuales en un puñado de salidas que alimentan el GIC. Un botón conectado a un pin GPIO aparece en el DT así:

```dts
&gpio {
    button_pins: button_pins {
        brcm,pins = <23>;
        brcm,function = <0>;       /* GPIO input */
        brcm,pull = <2>;           /* pull-up */
    };
};

button_node: button {
    compatible = "gpio-keys";
    pinctrl-names = "default";
    pinctrl-0 = <&button_pins>;
    key_enter {
        label = "enter";
        linux,code = <28>;
        gpios = <&gpio 23 0>;
        interrupt-parent = <&gpio>;
        interrupts = <23 3>;       /* edge trigger */
    };
};
```

Dos propiedades nombran el controlador padre: `gpios = <&gpio ...>` designa el controlador GPIO como proveedor de pines, e `interrupt-parent = <&gpio>` designa el mismo controlador como proveedor de interrupciones. Los dos roles son distintos y deben declararse de forma independiente.

El controlador GPIO agrega entonces sus líneas de interrupción y las reporta al GIC. Dentro del driver GPIO, cuando llega una interrupción procedente del GIC, identifica qué pin se activó y despacha el evento al driver que haya registrado un manejador para el recurso IRQ de ese pin.

Cuando tu driver solicita una interrupción para este nodo, FreeBSD recorre la cadena: el driver del botón pide el IRQ, la lógica intrng del controlador GPIO asigna un número de IRQ virtual y, finalmente, el kernel hace que el IRQ ascendente del GIC llame al dispatcher del driver GPIO, que a su vez llama al manejador del driver del botón. No escribes nada de esta fontanería tú mismo; simplemente solicitas el IRQ y lo manejas.

### El framework intrng

El subsistema `intrng` (interrupt next-generation) de FreeBSD es lo que unifica todo esto. Un controlador de interrupciones implementa los métodos `pic_*`:

```c
static device_method_t gpio_methods[] = {
    ...
    DEVMETHOD(pic_map_intr,      gpio_pic_map_intr),
    DEVMETHOD(pic_setup_intr,    gpio_pic_setup_intr),
    DEVMETHOD(pic_teardown_intr, gpio_pic_teardown_intr),
    DEVMETHOD(pic_enable_intr,   gpio_pic_enable_intr),
    DEVMETHOD(pic_disable_intr,  gpio_pic_disable_intr),
    ...
};
```

`pic_map_intr` es el que lee la propiedad del DT y devuelve una representación interna del IRQ. `pic_setup_intr` asocia un manejador. Los métodos restantes controlan el enmascaramiento y el reconocimiento.

Un driver consumidor nunca llama directamente a estos métodos. Llama a `bus_alloc_resource_any(dev, SYS_RES_IRQ, ...)`, y Newbus, junto con el código de recursos OFW, recorre el DT y el framework intrng para resolver el IRQ.

### Solicitar un IRQ en la práctica

La forma completa del manejo de interrupciones en un driver FDT es la siguiente:

```c
struct driver_softc {
    ...
    struct resource *irq_res;
    void *irq_cookie;
    int irq_rid;
};

static int
driver_attach(device_t dev)
{
    struct driver_softc *sc = device_get_softc(dev);
    int error;

    sc->irq_rid = 0;
    sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
        &sc->irq_rid, RF_ACTIVE);
    if (sc->irq_res == NULL) {
        device_printf(dev, "cannot allocate IRQ\n");
        return (ENXIO);
    }

    error = bus_setup_intr(dev, sc->irq_res,
        INTR_TYPE_MISC | INTR_MPSAFE,
        NULL, driver_intr, sc, &sc->irq_cookie);
    if (error != 0) {
        device_printf(dev, "cannot setup interrupt: %d\n", error);
        bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
            sc->irq_res);
        return (error);
    }
    ...
}
```

El RID para las interrupciones comienza en cero y se incrementa por cada IRQ en la lista `interrupts` del nodo. Un nodo con dos IRQs usaría los RIDs 0 y 1 de forma sucesiva.

`bus_setup_intr` registra el manejador. El cuarto argumento es una función de filtro (se ejecuta en contexto de interrupción); el quinto es un thread handler (se ejecuta en un thread del kernel dedicado). Pasas `NULL` para el que no vayas a usar. El flag `INTR_MPSAFE` indica al framework que el manejador no requiere el lock Giant.

Desmontaje en detach:

```c
static int
driver_detach(device_t dev)
{
    struct driver_softc *sc = device_get_softc(dev);

    if (sc->irq_cookie != NULL)
        bus_teardown_intr(dev, sc->irq_res, sc->irq_cookie);
    if (sc->irq_res != NULL)
        bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
            sc->irq_res);
    return (0);
}
```

No llamar a `bus_teardown_intr` es un error clásico al descargar el módulo: el IRQ queda conectado a memoria ya liberada, y la próxima vez que se activa el kernel entra en pánico.

### Filtros frente a thread handlers

La distinción entre filtros y thread handlers es uno de los temas que los nuevos desarrolladores del kernel suelen encontrar confusos. Una breve introducción ayuda.

Un *filtro* se ejecuta en el contexto de la propia interrupción, a un IPL elevado, con restricciones estrictas sobre lo que puede llamar. No puede dormir, no puede asignar memoria y no puede adquirir un mutex ordinario con capacidad de espera. Solo puede adquirir spin mutexes. Su propósito es decidir si la interrupción es para este dispositivo, reconocer la condición del hardware y bien manejar el evento de forma trivial o bien programar un thread handler para que haga el resto.

Un *thread* handler se ejecuta en un thread del kernel dedicado. Puede dormir, asignar memoria y adquirir locks con capacidad de espera. Muchos drivers realizan todo su trabajo en un thread handler y dejan el filtro vacío.

Para un driver tan sencillo como `edled`, nunca manejamos una interrupción. Si lo extendiéramos para gestionar un pulsador, comenzaríamos con un thread handler e introduciríamos un filtro solo cuando el perfilado lo mostrara necesario.

### Disparo por flanco frente a disparo por nivel

La tercera celda de un triple `interrupts` del GIC es el tipo de disparo. Valores habituales:

- `1`: flanco de subida
- `2`: flanco de bajada
- `3`: cualquier flanco
- `4`: nivel activo alto
- `8`: nivel activo bajo

Los nodos GPIO utilizados como interrupciones usan un número de celdas diferente (normalmente dos) y una codificación similar. La elección importa. Las interrupciones disparadas por flanco se activan una vez por transición; las interrupciones disparadas por nivel siguen activándose mientras la línea esté afirmada. Un driver que reconoce demasiado tarde en una línea disparada por nivel puede acabar en una tormenta de interrupciones.

La documentación de binding del DT para cada controlador especifica el número exacto de celdas y la semántica de los flags. Ante la duda, busca con grep en `/usr/src/sys/contrib/device-tree/Bindings/` la familia del controlador.

### Depurar interrupciones que no se activan

Los síntomas de las interrupciones mal configuradas suelen ser evidentes: el hardware funciona la primera vez y las interrupciones siguientes nunca llegan; o el sistema arranca pero el manejador del driver no se ejecuta nunca.

Comprobaciones, en orden:

1. **¿Muestra `vmstat -i` que la interrupción se está contando?** Si es así, el hardware la está afirmando pero el driver no la está reconociendo. Revisa tu filtro o thread handler.
2. **¿Coincide `interrupts` en el DT con el formato esperado por el controlador?** El número de celdas y los valores son los culpables habituales.
3. **¿Apunta `interrupt-parent` al controlador correcto?** Si el origen es un controlador basado en pines pero el DT indica el GIC, la solicitud fallará porque el formato de celdas del GIC no coincide.
4. **¿Devolvió `bus_setup_intr` cero?** Si no, lee el código de error. `EINVAL` normalmente significa que el recurso IRQ no se mapeó completamente; `ENOENT` significa que ningún controlador ha reclamado el número de IRQ.

La sonda DTrace `intr_event_show` puede ayudar en la depuración avanzada, pero la comprobación de cuatro pasos anterior resuelve la mayoría de los problemas sin necesidad de DTrace.

### Ejemplo real: gpiokeys.c

`/usr/src/sys/dev/gpio/gpiokeys.c` merece la pena leerlo como ejemplo funcional de un driver consumidor de GPIO que usa interrupciones. Para cada nodo hijo, adquiere un pin, lo configura como entrada y engancha una interrupción mediante `gpio_alloc_intr_resource` y `bus_setup_intr`. El filtro es muy breve: simplemente llama a `taskqueue_enqueue` con un elemento de trabajo. El procesamiento real de las teclas se ejecuta en la taskqueue del kernel, no en contexto de interrupción.

Es un patrón limpio para un driver pequeño dirigido por interrupciones. Un filtro que solo señaliza y un worker que realiza el trabajo. Cuando necesites implementar algo similar para un periférico de placa personalizada, el driver gpiokeys es una buena plantilla.

### Cerrando esta sección

La sección 10 desveló la maquinaria de interrupciones que nuestros ejemplos anteriores mantenían oculta. Ahora sabes cómo el Device Tree describe la conectividad de interrupciones, cómo intrng de FreeBSD resuelve una solicitud de IRQ en un registro concreto de manejador, cómo los filtros y los thread handlers dividen el trabajo y cómo depurar la clase de fallos que producen las interrupciones mal configuradas.

La cobertura técnica del capítulo está ahora verdaderamente completa. A continuación vienen los laboratorios, los ejercicios y el material de resolución de problemas.

## Laboratorios prácticos

Nada de lo que se explica en este capítulo quedará asentado sin ejecutarlo de verdad. Los laboratorios que siguen están ordenados por dificultad. El laboratorio 1 es un calentamiento que puedes completar en cualquier sistema FreeBSD con las fuentes del kernel instaladas, incluso en un portátil amd64 genérico bajo QEMU. El laboratorio 2 introduce los overlays, lo que significa que necesitarás un objetivo arm64, ya sea real (Raspberry Pi 4, BeagleBone, Pine64) o emulado. El laboratorio 3 es un ejercicio de depuración en el que romperás deliberadamente un DT y aprenderás los síntomas. El laboratorio 4 construye el driver `edled` completo y controla un LED a través de él.

Todos los archivos de los laboratorios están publicados en `examples/part-07/ch32-fdt-embedded/`. Cada laboratorio tiene su propio subdirectorio con un `README.md` y todos los fuentes que necesitas. El texto siguiente es autocontenido para que puedas trabajar directamente desde el libro, pero el árbol de ejemplos está ahí como red de seguridad cuando quieras comparar tu trabajo con una referencia conocida.

### Laboratorio 1: compilar y cargar el esqueleto fdthello

**Objetivo:** Compilar el driver mínimo con soporte FDT de la sección 4, cargarlo en un sistema FreeBSD en ejecución y confirmar que se registra en el kernel incluso cuando no existe ningún nodo DT coincidente.

**Lo que aprenderás:**

- Cómo funciona el Makefile de un módulo del kernel.
- Cómo interactúan `kldload` y `kldunload` con el registro del módulo.
- Cómo Newbus ejecuta las sondas en el momento en que se introduce un driver.

**Pasos:**

1. Crea un directorio de trabajo llamado `lab01-fdthello` en un sistema FreeBSD 14.3 con las fuentes del kernel instaladas.

2. Guarda el listado completo del código fuente de `fdthello.c` de la sección 4 en ese directorio.

3. Guarda un Makefile con el siguiente contenido:

   ```
   KMOD=   fdthello
   SRCS=   fdthello.c

   SYSDIR?= /usr/src/sys

   .include <bsd.kmod.mk>
   ```

4. Construye el módulo:

   ```
   $ make
   ```

   Una compilación limpia produce `fdthello.ko` y `fdthello.ko.debug` en el directorio actual.

5. Carga el módulo:

   ```
   # kldload ./fdthello.ko
   ```

   En un sistema sin nodo DT coincidente, ninguna sonda tiene éxito. Esto es lo esperado. El módulo está residente, pero no aparece ningún dispositivo `fdthello0`.

6. Verifica que el módulo está cargado:

   ```
   # kldstat -m fdthello
   ```

7. Descarga el módulo:

   ```
   # kldunload fdthello
   ```

**Resultado esperado:**

La compilación finaliza sin advertencias. El módulo se carga y descarga limpiamente. `kldstat` muestra `fdthello.ko` entre los dos pasos.

**Si encuentras un problema:**

- **`kldload` informa de "module not found":** Asegúrate de pasar `./fdthello.ko` con el `./` inicial para que `kldload` no intente buscar en la ruta de módulos del sistema.
- **La compilación falla con "no such file `bsd.kmod.mk`":** Instala `/usr/src` mediante `pkgbase` o clónalo desde git.
- **La compilación falla porque faltan símbolos del kernel:** Confirma que `/usr/src/sys` coincide con la versión del kernel en ejecución. Una discrepancia entre el kernel en ejecución y el árbol de fuentes es la causa habitual.

Los archivos de inicio se encuentran en `examples/part-07/ch32-fdt-embedded/lab01-fdthello/`.

### Laboratorio 2: compilar y desplegar un overlay

**Objetivo:** Añadir un nodo DT que coincida con el driver `fdthello`, desplegarlo como overlay en una placa FreeBSD arm64 y observar cómo el driver se engancha.

**Lo que aprenderás:**

- Cómo escribir un archivo fuente de overlay.
- Cómo `dtc -@` produce la salida `.dtbo` lista para ser usada como overlay.
- Cómo el loader de FreeBSD aplica los overlays a través de `fdt_overlays` en `loader.conf`.
- Cómo verificar que un overlay se aplicó correctamente.

**Pasos:**

1. En un sistema FreeBSD arm64 en ejecución (el objetivo de referencia es Raspberry Pi 4), instala `dtc`:

   ```
   # pkg install dtc
   ```

2. En un directorio de trabajo, guarda la siguiente fuente de overlay como `fdthello-overlay.dts`:

   ```
   /dts-v1/;
   /plugin/;

   / {
       compatible = "brcm,bcm2711";

       fragment@0 {
           target-path = "/soc";
           __overlay__ {
               hello@20000 {
                   compatible = "freebsd,fdthello";
                   reg = <0x20000 0x100>;
                   status = "okay";
               };
           };
       };
   };
   ```

3. Compila el overlay:

   ```
   $ dtc -I dts -O dtb -@ -o fdthello.dtbo fdthello-overlay.dts
   ```

4. Copia el resultado al directorio de overlays del loader:

   ```
   # cp fdthello.dtbo /boot/dtb/overlays/
   ```

5. Edita `/boot/loader.conf` (créalo si no existe) para incluir:

   ```
   fdt_overlays="fdthello"
   ```

6. Copia el `fdthello.ko` que compilaste en el laboratorio 1 a `/boot/modules/`:

   ```
   # cp /path/to/fdthello.ko /boot/modules/
   ```

7. Asegúrate de que `fdthello_load="YES"` está en `/boot/loader.conf`:

   ```
   fdthello_load="YES"
   ```

8. Reinicia:

   ```
   # reboot
   ```

9. Tras el reinicio, confirma:

   ```
   # dmesg | grep fdthello
   fdthello0: <FDT Hello Example> on simplebus0
   fdthello0: attached, node phandle 0x...
   ```

**Resultado esperado:**

El driver se conecta durante el boot y su mensaje aparece en `dmesg`. `ofwdump -p /soc/hello@20000` muestra las propiedades del nodo.

**Si te encuentras con algún problema:**

- **El loader imprime «error loading overlay»:** Normalmente el archivo `.dtbo` falta o está en el directorio equivocado. Comprueba que esté en `/boot/dtb/overlays/` y que tenga la extensión `.dtbo`.
- **El driver no se conecta:** Usa la lista de comprobación de la Sección 6: nodo presente, status okay, compatible exacto, padre `simplebus`.
- **Estás en una placa que no es una Pi:** Cambia el `compatible` de nivel superior en el overlay para que coincida con el compatible base de tu placa. `ofwdump -p /` muestra el valor actual.

Los archivos de inicio están en `examples/part-07/ch32-fdt-embedded/lab02-overlay/`.

### Laboratorio 3: Depura un Device Tree roto

**Objetivo:** Dado un overlay deliberadamente roto, identifica tres modos de fallo distintos y corrige cada uno.

**Lo que aprenderás:**

- Cómo usar `dtc`, `fdtdump` y `ofwdump` para leer un blob.
- Cómo correlacionar el contenido del árbol con el comportamiento del probe del kernel.
- Cómo usar mensajes de diagnóstico con `device_printf` para detectar un fallo de probe.

**Pasos:**

1. Copia el siguiente overlay roto en `lab03-broken.dts`:

   ```
   /dts-v1/;
   /plugin/;

   / {
       compatible = "brcm,bcm2711";

       fragment@0 {
           target-path = "/soc";
           __overlay__ {
               hello@20000 {
                   compatible = "free-bsd,fdthello";
                   reg = <0x20000 0x100>;
                   status = "disabled";
               };
           };
       };

       fragment@1 {
           target-path = "/soc";
           __overlay__ {
               hello@30000 {
                   compatible = "freebsd,fdthello";
                   reg = <0x30000>;
                   status = "okay";
               };
           };
       };
   };
   ```

2. Compila e instala el overlay:

   ```
   $ dtc -I dts -O dtb -@ -o lab03-broken.dtbo lab03-broken.dts
   # cp lab03-broken.dtbo /boot/dtb/overlays/
   ```

3. Edita `/boot/loader.conf` para cargar este overlay en lugar de `fdthello`:

   ```
   fdt_overlays="lab03-broken"
   ```

4. Reinicia. Observa:

   - Ningún dispositivo `fdthello0` se conecta.
   - `dmesg` puede estar en silencio o puede mostrar una advertencia de análisis FDT sobre `hello@30000`.

5. Diagnóstico. Usa las siguientes técnicas en orden:

   **a) Compara las cadenas compatible:**

   ```
   # ofwdump -P compatible /soc/hello@20000
   # ofwdump -P compatible /soc/hello@30000
   ```

   La primera imprime `free-bsd,fdthello`, que el driver no reconoce. El guion después de `free` es el error tipográfico. La corrección consiste en cambiar la cadena a `freebsd,fdthello`.

   **b) Comprueba el estado:**

   ```
   # ofwdump -P status /soc/hello@20000
   ```

   Devuelve `disabled`. Aunque la cadena compatible fuera correcta, el driver seguiría ignorando este nodo. La corrección es establecer `status = "okay"`.

   **c) Comprueba la propiedad `reg`:**

   ```
   # ofwdump -P reg /soc/hello@30000
   ```

   Observa que `reg` tiene solo una celda donde el nodo padre espera dirección más tamaño. Bajo nodos padre con `#address-cells = <1>` y `#size-cells = <1>`, `reg` debe tener dos celdas. El driver se conecta, pero si alguna vez asigna el recurso, leerá el tamaño de forma incorrecta a partir de lo que haya a continuación. La corrección es `reg = <0x30000 0x100>;`.

6. Aplica las correcciones, recompila, reinstala el overlay y reinicia. El driver debería conectarse a uno o ambos nodos hello.

**Resultado esperado:**

Tras las tres correcciones, `dmesg | grep fdthello` muestra dos dispositivos conectados, `hello@20000` y `hello@30000`, cada uno notificado a través de `simplebus`.

**Si te encuentras con algún problema:**

- **`ofwdump` informa "no such node":** El overlay no se aplicó. Comprueba en la salida del cargador si hay un mensaje de carga del overlay, y verifica que el `.dtbo` esté donde el cargador lo espera.
- **Solo se conecta un dispositivo hello:** Uno de los tres fallos sigue presente.
- **El kernel entra en pánico:** Es casi seguro que estás leyendo más allá del final de `reg` porque el número de celdas sigue siendo incorrecto. Vuelve al overlay que sabes que funciona mientras diagnosticas.

Los archivos de inicio se encuentran en `examples/part-07/ch32-fdt-embedded/lab03-debug-broken/`.

### Laboratorio 4: Construye el driver edled de principio a fin

**Objetivo:** Construir el driver `edled` completo a partir de la Sección 7, compilarlo, conectarlo a través de un overlay DT al GPIO18 en una Raspberry Pi 4 y activar el LED desde el espacio de usuario.

**Lo que aprenderás:**

- Cómo integrar la adquisición de recursos GPIO en un driver FDT.
- Cómo exponer un sysctl que controla hardware.
- Cómo verificar el driver en un sistema en ejecución usando `dmesg`, `sysctl`, `ofwdump` y `devinfo -r`.

**Pasos:**

1. Conecta un LED entre el GPIO18 (pin 12 del conector) y tierra a través de una resistencia de 330 ohmios. Si no tienes hardware físico, puedes continuar igualmente; el driver se conecta y alterna su estado lógico, pero no habrá nada que ilumine.

2. En un directorio de trabajo, guarda:

   - `edled.c` del listado completo de la Sección 7.
   - `Makefile` con `KMOD=edled`, `SRCS=edled.c`, `SYSDIR?=/usr/src/sys` e `.include <bsd.kmod.mk>`.
   - El overlay `edled.dts` de la Sección 7.

3. Compila el módulo:

   ```
   $ make
   ```

4. Compila el overlay:

   ```
   $ dtc -I dts -O dtb -@ -o edled.dtbo edled.dts
   ```

5. Instala:

   ```
   # cp edled.ko /boot/modules/
   # cp edled.dtbo /boot/dtb/overlays/
   ```

6. Edita `/boot/loader.conf`:

   ```
   edled_load="YES"
   fdt_overlays="edled"
   ```

7. Reinicia.

8. Confirma la conexión:

   ```
   # dmesg | grep edled
   edled0: <Example embedded LED> on simplebus0
   edled0: attached, GPIO pin acquired, state=0
   ```

9. Prueba el sysctl:

   ```
   # sysctl dev.edled.0.state
   dev.edled.0.state: 0

   # sysctl dev.edled.0.state=1
   dev.edled.0.state: 0 -> 1
   ```

   El LED se ilumina. Vuelve a leer y confirma:

   ```
   # sysctl dev.edled.0.state
   dev.edled.0.state: 1
   ```

10. Apaga el LED y descarga el driver:

    ```
    # sysctl dev.edled.0.state=0
    # kldunload edled
    ```

**Resultado esperado:**

El driver se carga, se conecta, alterna el LED y se descarga sin dejar recursos en uso. `gpioctl -l` muestra que el pin ha vuelto al estado no configurado tras la descarga.

**Si te encuentras con algún problema:**

- **`dmesg` muestra "cannot get GPIO pin":** El módulo del controlador GPIO aún no se ha conectado. Verifica que `gpiobus` esté cargado: `kldstat -m gpiobus`. Si no lo está, ejecuta `kldload gpiobus` antes de volver a intentarlo.
- **El LED no se ilumina:** Comprueba la polaridad. Si el flag en el DT es `0` (activo en alto), el pin entrega 3.3V cuando está activo. Si el cátodo del LED va al pin, necesitas `1` (activo en bajo).
- **`kldunload` falla con EBUSY:** Algún proceso todavía tiene `dev.edled.0` abierto, o la ruta de detach del driver dejó un recurso adquirido. Revisa detach.

Los archivos de inicio se encuentran en `examples/part-07/ch32-fdt-embedded/lab04-edled/`.

### Laboratorio 5: Amplía edled para consumir una interrupción GPIO

**Objetivo:** Modificar el driver `edled` del Laboratorio 4 de modo que un segundo GPIO, configurado como entrada con resistencia de pull-up, se convierta en fuente de interrupciones. Cuando el pin se lleva a tierra (un pulsador que lo pone a nivel bajo), el manejador alterna el LED.

**Lo que aprenderás:**

- Cómo adquirir un recurso de interrupción GPIO mediante `gpio_alloc_intr_resource`.
- Cómo configurar un manejador en contexto de thread con `bus_setup_intr`.
- Cómo coordinar la ruta de interrupción y la ruta del sysctl mediante estado compartido.
- Cómo desmontar un manejador de interrupciones correctamente en detach.

**Pasos:**

1. Parte de `edled.c` del Laboratorio 4. Cópialo como `edledi.c` en un directorio de trabajo limpio.

2. Añade un segundo GPIO al softc y al binding DT. El nuevo nodo DT tiene este aspecto:

   ```
   edledi0: edledi@0 {
       compatible = "example,edledi";
       status = "okay";
       leds-gpios = <&gpio 18 0>;
       button-gpios = <&gpio 23 1>;
       interrupt-parent = <&gpio>;
       interrupts = <23 3>;
   };
   ```

   El pulsador usa el GPIO 23, cableado con el esquema habitual: una pata al pin, la otra a tierra, y una resistencia de pull-up a 3.3V.

3. Actualiza la tabla de cadenas compat a `"example,edledi"` para que el driver reconozca el nuevo binding.

4. En el softc, añade:

   ```c
   gpio_pin_t      sc_button;
   struct resource *sc_irq;
   void            *sc_irq_cookie;
   int             sc_irq_rid;
   ```

5. En attach, después de adquirir el pin del LED, adquiere el pin del pulsador y su interrupción:

   ```c
   error = gpio_pin_get_by_ofw_property(dev, node,
       "button-gpios", &sc->sc_button);
   if (error != 0) {
       device_printf(dev, "cannot get button pin: %d\n", error);
       goto fail;
   }

   error = gpio_pin_setflags(sc->sc_button,
       GPIO_PIN_INPUT | GPIO_PIN_PULLUP);
   if (error != 0) {
       device_printf(dev, "cannot configure button: %d\n", error);
       goto fail;
   }

   sc->sc_irq_rid = 0;
   sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
       &sc->sc_irq_rid, RF_ACTIVE);
   if (sc->sc_irq == NULL) {
       device_printf(dev, "cannot allocate IRQ\n");
       goto fail;
   }

   error = bus_setup_intr(dev, sc->sc_irq,
       INTR_TYPE_MISC | INTR_MPSAFE,
       NULL, edledi_intr, sc, &sc->sc_irq_cookie);
   if (error != 0) {
       device_printf(dev, "cannot setup interrupt: %d\n", error);
       goto fail;
   }
   ```

6. Escribe el manejador de interrupciones:

   ```c
   static void
   edledi_intr(void *arg)
   {
       struct edled_softc *sc = arg;

       mtx_lock(&sc->sc_mtx);
       sc->sc_on = !sc->sc_on;
       (void)gpio_pin_set_active(sc->sc_pin, sc->sc_on);
       mtx_unlock(&sc->sc_mtx);
   }
   ```

   Este es un manejador de thread (pasado como quinto argumento a `bus_setup_intr`, con `NULL` como cuarto para el filtro). Es seguro tomar un mutex y llamar al framework GPIO.

7. En detach, añade el desmontaje en orden inverso:

   ```c
   if (sc->sc_irq_cookie != NULL)
       bus_teardown_intr(dev, sc->sc_irq, sc->sc_irq_cookie);
   if (sc->sc_irq != NULL)
       bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irq_rid,
           sc->sc_irq);
   if (sc->sc_button != NULL)
       gpio_pin_release(sc->sc_button);
   ```

8. Recompila, redistribuye el overlay, reinicia y pulsa el botón.

**Resultado esperado:**

Cada pulsación alterna el LED. El sysctl sigue funcionando para el control programático. El driver se descarga correctamente.

**Si te encuentras con algún problema:**

- **La interrupción nunca se activa:** Confirma que la resistencia de pull-up mantiene el pin en alto cuando está inactivo; comprueba la celda de disparo en el DT (3 = cualquier flanco); observa `vmstat -i` para ver si se cuenta alguna IRQ para tu dispositivo.
- **Interrupciones repetidas en una sola pulsación (rebote):** Los pulsadores mecánicos rebotan. Se puede implementar un antirrebote software sencillo ignorando las interrupciones dentro de una ventana breve tras la primera. Usa `sbintime()` y un campo de estado en el softc.
- **`kldunload` falla con EBUSY:** Te falta un `bus_teardown_intr` o un `gpio_pin_release`.

Los archivos de inicio se encuentran en `examples/part-07/ch32-fdt-embedded/lab05-edledi/`.

### Tras los laboratorios

Al terminar el Laboratorio 4 habrás recorrido todo el arco del trabajo con drivers embebidos: código fuente, overlay, módulo del kernel, acceso desde espacio de usuario y desmontaje. Las secciones restantes del capítulo te ofrecen formas de ampliar lo construido y un último vistazo a los errores más comunes.

## Ejercicios de desafío

Los ejercicios que aparecen a continuación van más allá de los laboratorios guiados. No incluyen instrucciones paso a paso, porque el objetivo es que apliques lo aprendido a problemas no estructurados. Si te quedas atascado, el material de referencia de las Secciones 5 a 8 y los drivers reales bajo `/usr/src/sys/dev/gpio/` y `/usr/src/sys/dev/fdt/` son tus recursos más valiosos.

### Desafío 1: Varios LEDs por nodo

Modifica `edled` para aceptar un nodo DT que declare varios GPIOs, como hace el driver real `gpioled_fdt.c`. El binding debería tener este aspecto:

```dts
edled0: edled@0 {
    compatible = "example,edled-multi";
    led-red  = <&gpio 18 0>;
    led-amber = <&gpio 19 0>;
    led-green = <&gpio 20 0>;
};
```

Expón un sysctl por LED: `dev.edled.0.red`, `dev.edled.0.amber`, `dev.edled.0.green`. Cada uno debe comportarse de forma independiente.

*Pista:* Recorre las propiedades DT en attach. Para una estructura más clara, guarda un array de manejadores de pin en el softc e itera sobre él tanto en attach como en detach. `gpio_pin_get_by_ofw_property` acepta el nombre de la propiedad como tercer argumento, por lo que el mismo driver puede gestionar distintos nombres de propiedad con una pequeña tabla de búsqueda.

### Desafío 2: Soporte para un temporizador de parpadeo

Amplía `edled` con un segundo sysctl `dev.edled.0.blink_ms` que, al establecerse a un valor distinto de cero, inicie un callout del kernel que alterne el pin cada `blink_ms` milisegundos. Escribir `0` detiene el parpadeo y deja el LED en su estado actual.

*Pista:* Usa `callout_init_mtx` para asociar el callout con el mutex del softc, y `callout_reset` para programarlo. Recuerda hacer `callout_drain` en detach para que el sistema no deje un evento programado apuntando a memoria ya liberada.

### Desafío 3: Generalización a cualquier salida GPIO

Renombra y generaliza `edled` como `edoutput`, un driver capaz de controlar cualquier línea de salida GPIO con una interfaz sysctl. Acepta una propiedad `label` desde el DT y úsala como parte de la ruta del sysctl para que varias instancias no colisionen. Añade un sysctl `dev.edoutput.0.pulse_ms` que activa la línea durante el número de milisegundos indicado y luego la devuelve al estado inactivo.

*Pista:* `device_get_unit(dev)` te da el número de unidad; usa `device_get_nameunit(dev)` para obtener una cadena combinada de nombre más unidad cuando la necesites.

### Desafío 4: Consumir una interrupción

Si tu placa expone un pulsador conectado a una entrada GPIO (o puedes usar una resistencia de pull-up y un cable puente como simulación de un solo disparo), modifica el driver para vigilar las transiciones de flanco en un pin de entrada y registrarlas con `device_printf`. Necesitarás adquirir el recurso IRQ con `bus_alloc_resource_any(dev, SYS_RES_IRQ, ...)`, configurar el manejador de interrupciones con `bus_setup_intr` y liberarlo correctamente en detach.

*Pista:* Consulta `/usr/src/sys/dev/gpio/gpiokeys.c` como referencia de un driver que consume interrupciones GPIO disparadas desde FDT.

### Desafío 5: Produce un Device Tree personalizado para QEMU

Escribe un `.dts` completo para una placa embebida hipotética que incluya:

- Un único núcleo ARM Cortex-A53.
- 256 MB de RAM.
- Un simplebus.
- Una UART en la dirección que elijas.
- Un nodo `edled` bajo simplebus que haga referencia a un controlador GPIO.
- Un nodo de controlador GPIO mínimo que inventes tú.

Compila el resultado, arranca un kernel FreeBSD arm64 contra él bajo QEMU con `-dtb` y observa cómo el driver se conecta. El controlador GPIO fallará porque nada maneja el hardware inventado, pero verás el recorrido completo desde el DT hasta Newbus con tu propio código fuente.

*Pista:* Usa `/usr/src/sys/contrib/device-tree/src/arm64/arm/juno.dts` como referencia estructural.

### Desafío 6: Porta el binding DT de un driver real a una nueva placa

Elige cualquier driver FDT de FreeBSD existente (por ejemplo, `sys/dev/iicbus/pmic/act8846.c`), lee su binding DT estudiando el código fuente del driver y escribe un fragmento DT completo que lo conectaría en una Raspberry Pi 4. No necesitas ejecutar el driver realmente; el ejercicio consiste en leer el binding desde el código fuente y producir un fragmento `.dtsi` correcto.

*Pista:* Lee la tabla compat del driver, su probe y cualquier llamada a `of_` para descubrir qué propiedades espera. Los árboles de código fuente del kernel del fabricante suelen documentar los bindings DT en comentarios al inicio del archivo.

### Desafío 7: Escribe a mano un DT completo para un objetivo embebido bajo QEMU

El Desafío 5 te invitaba a escribir un DT parcial para una placa hipotética. El Desafío 7 va más lejos: escribe un `.dts` completo para un objetivo QEMU arm64 que definas enteramente tú. Incluye memoria, temporizador, una UART PL011, una GIC, un controlador GPIO de estilo PL061 y una instancia de un periférico de tu propio diseño. Arranca un kernel FreeBSD arm64 sin modificar bajo QEMU con tu `.dtb`. Verifica que la consola aparece en la UART que hayas elegido y que el probe de dispositivos del kernel recorre el árbol.

*Pista:* El comando `qemu-system-aarch64` soporta `-dtb` más `-machine virt,dumpdtb=out.dtb` para emitir un DT de referencia que puedes estudiar y adaptar.

### Desafío 8: Implementa un driver simple para un periférico MMIO

Escribe un driver para un periférico MMIO hipotético que también simules en QEMU. El periférico expone un registro de 32 bits en una dirección fija. Leer el registro devuelve un contador libre; escribir cero reinicia el contador. Tu driver debe exponer un sysctl `dev.counter.0.value` que lea y escriba este registro. Verifica que `bus_read_4` y `bus_write_4` funcionan como se espera. Simula el hardware escribiendo un pequeño modelo de dispositivo para QEMU, o reutilizando una región simulada existente cuyo valor puedas observar.

*Pista:* `bus_read_4(sc->mem, 0)` devuelve el valor u32 en el desplazamiento 0 dentro de tu recurso de memoria asignado. La página de manual `bus_space(9)` es la referencia autoritativa.

### Después de los desafíos

Estos ejercicios son intencionalmente abiertos. Si completas cualquiera de ellos, has interiorizado el material de este capítulo. Si quieres más, `/usr/src/sys/dev/` tiene decenas de drivers FDT de todos los tamaños. Leer un driver a la semana es uno de los mejores hábitos que puede desarrollar un programador de FreeBSD embebido.

## Errores comunes y resolución de problemas

Esta sección es una referencia concentrada de los problemas que con mayor frecuencia hacen tropezar a quienes escriben drivers FDT. Todo lo que aparece aquí ya se mencionó en las secciones 3 a 8, pero reunir los puntos en un solo lugar te da algo que puedes repasar rápidamente cuando aparece un síntoma concreto.

### El módulo carga pero ningún dispositivo se conecta

La mayoría de los fallos en probe se reducen a una de estas cinco causas:

1. **Error tipográfico en `compatible`.** Ya sea en el código fuente del DT o en la tabla de compatibilidad del driver. La cadena debe coincidir byte a byte.
2. **El nodo tiene `status = "disabled"`.** O bien corriges el árbol base o escribes un overlay que establezca el estado en okay.
3. **Bus padre incorrecto.** Si el nodo está bajo un nodo de controlador I2C o SPI, el driver debe registrarse con el tipo de driver de ese controlador, no con `simplebus`.
4. **El overlay no se aplicó.** Comprueba la salida del cargador durante el boot en busca de mensajes de error. Confirma que el `.dtbo` está en `/boot/dtb/overlays/` y aparece en `loader.conf`.
5. **Otro driver tiene mayor prioridad.** Usa `devinfo -r` para ver qué driver se conectó realmente. Aumenta el valor de retorno del probe (`BUS_PROBE_DEFAULT` es el valor base habitual; `BUS_PROBE_SPECIFIC` indica una coincidencia más exacta).

### El overlay no se aplica en el boot

El cargador imprime sus intentos en la consola. Fíjate en líneas como:

```text
Loading DTB overlay 'edled' (0x1200 bytes)
```

Si esa línea no aparece, el cargador no encontró el archivo o lo saltó. Posibles causas:

- El nombre del archivo termina en `.dtbo` pero `fdt_overlays` lo escribe mal.
- El archivo está en un directorio incorrecto. El directorio por defecto es `/boot/dtb/overlays/`.
- El blob base y el overlay difieren en el `compatible` de nivel superior. El cargador rechaza aplicar un overlay cuyo compatible de nivel superior no coincide con el del base.
- El `.dtbo` se compiló sin `-@` y referencia etiquetas que el cargador no puede resolver.

### No se pueden asignar recursos

Si attach llama a `bus_alloc_resource_any(dev, SYS_RES_MEMORY, ...)` y devuelve `NULL`, las causas más probables son:

- `reg` ausente o malformado en el nodo DT.
- Discrepancia en el número de celdas entre el `reg` del nodo y los valores `#address-cells`/`#size-cells` del padre.
- Otro driver ya reclamó la misma región.
- El `ranges` del bus padre no cubre la dirección solicitada.

Imprime el inicio y el tamaño del recurso en attach durante la depuración:

```c
if (sc->mem == NULL) {
    device_printf(dev, "cannot allocate memory resource (rid=%d)\n",
        sc->mem_rid);
    goto fail;
}
device_printf(dev, "memory at %#jx len %#jx\n",
    (uintmax_t)rman_get_start(sc->mem),
    (uintmax_t)rman_get_size(sc->mem));
```

### La adquisición de GPIO falla

`gpio_pin_get_by_ofw_*` devuelve `ENXIO` cuando:

- El controlador GPIO referenciado en la propiedad DT no se ha conectado todavía.
- El número de pin está fuera del rango de ese controlador.
- El phandle en el DT es incorrecto.

La primera causa es, con diferencia, la más común. La solución es `MODULE_DEPEND(your_driver, gpiobus, 1, 1, 1)` para que el cargador dinámico inicialice `gpiobus` primero.

### El manejador de interrupción no se activa

Si el hardware debería generar una interrupción y no ocurre nada:

- Confirma que la propiedad `interrupts` del DT es correcta. El formato depende del valor `#interrupt-cells` del controlador de interrupciones padre.
- Confirma que `bus_alloc_resource_any(SYS_RES_IRQ, ...)` devolvió un recurso válido.
- Confirma que `bus_setup_intr` devolvió cero.
- Confirma que el valor de retorno de tu manejador es `FILTER_HANDLED` o `FILTER_STRAY` para un filtro, o `FILTER_SCHEDULE_THREAD` si utilizas un manejador con thread.

Usa `vmstat -i` para ver si tu interrupción se está contabilizando. Si el contador se queda en cero, la interrupción ni siquiera está siendo enrutada hacia tu manejador.

### La descarga devuelve EBUSY

detach olvidó liberar algo. Revisa tu attach con cuidado y confirma que cada `_get_` tiene su correspondiente llamada `_release_` en detach. Los sospechosos habituales son:

- Pines GPIO obtenidos con `gpio_pin_get_by_*`.
- Manejadores de interrupción configurados con `bus_setup_intr`.
- Manejadores de reloj obtenidos con `clk_get_by_*`.
- Manejadores de regulador obtenidos con `regulator_get_by_*`.
- Recursos de memoria de `bus_alloc_resource_any`.

Imprime una marca de seguimiento en cada liberación:

```c
device_printf(dev, "detach: releasing GPIO pin\n");
gpio_pin_release(sc->sc_pin);
```

Si las marcas de seguimiento se detienen antes del final esperado, el recurso que queda sin liberar es el que viene después de la última línea impresa.

### Pánico durante el boot

Los pánicos que ocurren durante el análisis del FDT generalmente significan que el blob en sí está malformado, o que un driver está desreferenciando un resultado de `OF_getprop` que no comprobó. Dos salvaguardas:

- Comprueba siempre el valor de retorno de `OF_getprop`, `OF_getencprop` y `OF_getprop_alloc`. Una propiedad ausente devuelve `-1` o `ENOENT`; tratarla como presente conduce a leer lo que sea que esté a continuación en la pila.
- Usa `OF_hasprop(node, "prop")` antes de llamar a `OF_getprop` cuando una propiedad es opcional.

### Errores de compilación del DT

Los mensajes de error de `dtc` son bastante claros. Algunos patrones que conviene reconocer:

- **`syntax error`**: Punto y coma ausente, llave sin cerrar o sintaxis incorrecta en el valor de una propiedad.
- **`Warning (simple_bus_reg)`**: Un nodo bajo `simple-bus` tiene `reg` pero no tiene traducción `ranges`, o su `reg` no coincide con el número de celdas del padre.
- **`FATAL ERROR: Unable to parse input tree`**: El archivo tiene errores sintácticos graves. Comprueba si falta `/dts-v1/;` o si hay cadenas mal entrecomilladas.
- **`ERROR (phandle_references): Reference to non-existent node or label`**: Una referencia `&label` que el compilador no puede resolver. Aquí es donde importa `-@`; sin él, los overlays que dependen de etiquetas del árbol base no pueden validarse.

### El kernel ve el hardware incorrecto

Si tu driver se conecta pero lee basura en los registros:

- Comprueba de nuevo el valor de `reg` en el DT.
- Confirma que `#address-cells` y `#size-cells` coinciden con lo que esperas en el nivel del padre.
- Usa técnicas del estilo `hexdump /dev/mem` únicamente si estás seguro de que la dirección es segura; leer el rango MMIO incorrecto puede bloquear el bus.

### Cambiaste el driver pero nada cambió

Comprueba lo siguiente:

- ¿Ejecutaste `make` después de editar?
- ¿Copiaste el nuevo `.ko` a `/boot/modules/` (o cargaste el local explícitamente)?
- ¿Descargaste el módulo antiguo antes de cargar el nuevo? `kldstat -m driver` muestra los módulos actualmente residentes.

Un hábito sencillo para evitar el último problema es usar siempre `kldunload` explícitamente antes de `kldload`, o usar `kldload -f` para forzar la sustitución.

### Referencia rápida: las llamadas OFW más usadas

Para mayor comodidad, aquí tienes una tabla compacta de los helpers de OFW y `ofw_bus` que los drivers FDT usan con más frecuencia. Cada uno se declara en `<dev/ofw/openfirm.h>` o en `<dev/ofw/ofw_bus_subr.h>`.

| Llamada                                      | Qué hace                                                                   |
|----------------------------------------------|----------------------------------------------------------------------------|
| `ofw_bus_get_node(dev)`                      | Devuelve el phandle del nodo DT de este dispositivo.                       |
| `ofw_bus_get_compat(dev)`                    | Devuelve la primera cadena compatible del nodo, o NULL.                    |
| `ofw_bus_get_name(dev)`                      | Devuelve la parte del nombre del nodo (antes de '@').                      |
| `ofw_bus_status_okay(dev)`                   | Verdadero si el status está ausente, es "okay" o "ok".                     |
| `ofw_bus_is_compatible(dev, s)`              | Verdadero si alguna entrada compatible coincide con s.                     |
| `ofw_bus_search_compatible(dev, tbl)`        | Devuelve la entrada coincidente en una tabla compat_data.                  |
| `ofw_bus_has_prop(dev, s)`                   | Verdadero si la propiedad está presente en el nodo.                        |
| `OF_getprop(node, name, buf, len)`           | Copia los bytes sin procesar de la propiedad.                              |
| `OF_getencprop(node, name, buf, len)`        | Copia la propiedad, convirtiendo las celdas u32 al endianness del host.    |
| `OF_getprop_alloc(node, name, bufp)`         | Asigna memoria y devuelve la propiedad; el llamador la libera con OF_prop_free. |
| `OF_hasprop(node, name)`                     | Devuelve un valor distinto de cero si la propiedad existe.                 |
| `OF_child(node)`                             | phandle del primer hijo, o 0.                                              |
| `OF_peer(node)`                              | phandle del siguiente hermano, o 0.                                        |
| `OF_parent(node)`                            | phandle del padre, o 0.                                                    |
| `OF_finddevice(path)`                        | Busca un nodo por su ruta absoluta.                                        |

### Referencia rápida: las llamadas de periféricos más usadas

De `<dev/gpio/gpiobusvar.h>`:

| Llamada                                         | Qué hace                                                              |
|-------------------------------------------------|-----------------------------------------------------------------------|
| `gpio_pin_get_by_ofw_idx(dev, node, idx, &pin)` | Adquiere el pin por índice en `gpios`.                                |
| `gpio_pin_get_by_ofw_name(dev, node, n, &pin)`  | Adquiere el pin por referencia con nombre (p. ej., `led-gpios`).     |
| `gpio_pin_get_by_ofw_property(dev, n, p, &pin)` | Adquiere el pin desde una propiedad DT con nombre.                    |
| `gpio_pin_setflags(pin, flags)`                 | Configura la dirección, las resistencias de pull y similares.         |
| `gpio_pin_set_active(pin, val)`                 | Lleva la salida al estado activo o inactivo.                          |
| `gpio_pin_get_active(pin, &val)`                | Lee el nivel actual de entrada o salida.                              |
| `gpio_pin_release(pin)`                         | Devuelve el pin al grupo sin propietario.                             |

De `<dev/extres/clk/clk.h>`:

| Llamada                                   | Qué hace                                                              |
|-------------------------------------------|-----------------------------------------------------------------------|
| `clk_get_by_ofw_index(dev, node, i, &c)`  | Adquiere el n-ésimo reloj de la propiedad `clocks`.                   |
| `clk_get_by_ofw_name(dev, node, n, &c)`   | Adquiere el reloj por nombre.                                         |
| `clk_enable(c)` / `clk_disable(c)`        | Activa o desactiva el reloj.                                          |
| `clk_get_freq(c, &f)`                     | Lee la frecuencia actual en Hz.                                       |
| `clk_release(c)`                          | Libera el manejador del reloj.                                        |

De `<dev/extres/regulator/regulator.h>`:

| Llamada                                               | Qué hace                                                    |
|-------------------------------------------------------|-------------------------------------------------------------|
| `regulator_get_by_ofw_property(dev, node, p, &r)`     | Adquiere el regulador desde una propiedad DT.               |
| `regulator_enable(r)` / `regulator_disable(r)`        | Activa o desactiva la línea de alimentación.                |
| `regulator_set_voltage(r, min, max)`                  | Solicita un voltaje dentro del rango min..max.              |
| `regulator_release(r)`                                | Libera el manejador del regulador.                          |

De `<dev/extres/hwreset/hwreset.h>`:

| Llamada                                      | Qué hace                                                  |
|----------------------------------------------|-----------------------------------------------------------|
| `hwreset_get_by_ofw_name(dev, node, n, &h)`  | Adquiere la línea de reset por nombre.                    |
| `hwreset_get_by_ofw_idx(dev, node, i, &h)`   | Adquiere la línea de reset por índice.                    |
| `hwreset_assert(h)` / `hwreset_deassert(h)`  | Coloca el periférico en estado de reset o lo saca de él.  |
| `hwreset_release(h)`                         | Libera el handle de reset.                                |

De `<dev/fdt/fdt_pinctrl.h>`:

| Llamada                                       | Qué hace                                                 |
|-----------------------------------------------|----------------------------------------------------------|
| `fdt_pinctrl_configure_by_name(dev, name)`    | Aplica el estado pinctrl indicado por nombre.            |
| `fdt_pinctrl_configure_tree(dev)`             | Aplica `pinctrl-0` de forma recursiva a los hijos.       |
| `fdt_pinctrl_register(dev, mapper)`           | Registra un nuevo proveedor pinctrl.                     |

Imprime esta tabla, colócala cerca de tu puesto de trabajo y consúltala cada vez que empieces un nuevo driver. Estas llamadas se vuelven algo natural tras unos pocos proyectos.

### Lista de comprobación final

Antes de declarar un driver terminado, repasa esta lista:

- [ ] El módulo compila sin advertencias con `-Wall -Wextra`.
- [ ] `kldload` produce el mensaje de attach esperado.
- [ ] `kldunload` finaliza correctamente sin EBUSY.
- [ ] Repetir ciclos de carga y descarga una docena de veces no produce fugas de recursos.
- [ ] `devinfo -r` muestra el driver en la posición esperada dentro del árbol.
- [ ] Los sysctls están presentes, son legibles y modificables donde corresponde.
- [ ] Todas las propiedades DT que el driver utiliza están documentadas en el README.
- [ ] El overlay de acompañamiento compila con `dtc -@`.
- [ ] Un lector que empieza de cero puede leer el código fuente y entender qué hace.

## Glosario de términos de Device Tree y sistemas embebidos

Este glosario reúne los términos que utiliza este capítulo, definidos brevemente para que puedas consultar cualquiera de ellos en el primer momento en que lo encuentres sin necesidad de buscar en el texto. Las referencias cruzadas a la sección correspondiente aparecen entre paréntesis cuando resultan útiles.

**ACPI**: Advanced Configuration and Power Interface. Alternativa a FDT utilizada en equipos de clase PC y en algunos servidores arm64. Un kernel FreeBSD elige uno u otro durante el boot. (Sección 3.)

**amd64**: Arquitectura x86 de 64 bits de FreeBSD. Normalmente utiliza ACPI en lugar de FDT, aunque FDT puede emplearse en casos embebidos x86 especializados.

**arm64**: Arquitectura ARM de 64 bits de FreeBSD. Utiliza FDT por defecto en placas embebidas; utiliza ACPI en servidores compatibles con SBSA.

**Bindings**: Convenciones documentadas sobre cómo se escriben las propiedades de un periférico en el Device Tree. Por ejemplo, el binding `gpio-leds` documenta qué propiedades debe tener el nodo DT de un controlador LED.

**Blob**: Término informal para un archivo `.dtb`, ya que es un bloque binario opaco desde la perspectiva de cualquier elemento que no sea un analizador FDT.

**BSP**: Board Support Package. El conjunto de archivos (configuración del kernel, Device Tree, pistas para el loader y, en ocasiones, drivers) necesarios para ejecutar un sistema operativo en una placa concreta.

**Cell**: Un valor de 32 bits en big-endian que constituye la unidad atómica de una propiedad DT. Los valores de las propiedades son secuencias de cells.

**Compatible string**: El identificador con el que el probe de un driver busca coincidencia, almacenado en la propiedad `compatible` de un nodo DT. Normalmente tiene la forma prefijo-de-fabricante-barra-modelo: `"brcm,bcm2711-gpio"`.

**Compat data table**: Un array de entradas `struct ofw_compat_data` que un driver recorre durante el probe mediante `ofw_bus_search_compatible`. (Sección 4.)

**dtb**: Binario compilado del Device Tree. Salida de `dtc`; el formato que el kernel analiza durante el boot.

**dtbo**: Overlay compilado del Device Tree. Un pequeño binario que el loader fusiona con el `.dtb` principal antes de entregarlo al kernel.

**dtc**: El compilador del Device Tree. Traduce el código fuente a binario.

**dts**: Device Tree Source. Entrada de texto para `dtc`.

**dtsi**: Device Tree Source Include. Un fragmento de código fuente pensado para ser incluido con `#include`.

**Edge triggered**: Una interrupción que se dispara en una transición de nivel (flanco de subida, de bajada o ambos). Contrasta con level-triggered.

**FDT**: Flattened Device Tree. El formato binario y el framework que FreeBSD utiliza para los sistemas basados en DT. También se usa de forma coloquial para referirse al concepto en su conjunto.

**fdt_overlays**: Un parámetro configurable de loader.conf que lista los nombres de los overlays que deben aplicarse durante el boot.

**fdtdump**: Una utilidad que decodifica un archivo `.dtb` en una aproximación legible de su código fuente.

**Fragment**: Una entrada de nivel superior en un overlay que nombra un target y declara el contenido a fusionar. (Sección 5.)

**GPIO**: General-Purpose Input/Output. Un pin digital programable que puede activar o leer una línea.

**intrng**: El framework de interrupciones de nueva generación de FreeBSD. Unifica los controladores de interrupciones y los consumidores. (Sección 10.)

**kldload**: El comando que carga un módulo del kernel en un sistema en ejecución.

**kldunload**: El comando que descarga un módulo del kernel ya cargado.

**Level triggered**: Una interrupción que permanece activa mientras una condición sea verdadera. Debe eliminarse en el origen para dejar de dispararse.

**Loader**: El boot loader de FreeBSD. Lee la configuración, carga módulos, fusiona los overlays DT y cede el control al kernel.

**MMIO**: Memory-Mapped IO. Un conjunto de registros de hardware expuesto a través de un rango de direcciones físicas.

**Newbus**: El framework de drivers de dispositivo de FreeBSD. Cada driver se registra en un árbol de relaciones padre-hijo de Newbus.

**Node**: Un punto en el Device Tree. Tiene un nombre (y una unit address opcional) y un conjunto de propiedades.

**OFW**: Open Firmware. Un estándar histórico cuya API reutiliza el código FDT de FreeBSD.

**ofwbus**: El bus de nivel superior de FreeBSD para la enumeración de dispositivos derivada de Open Firmware.

**ofwdump**: Una utilidad de espacio de usuario que muestra los nodos y propiedades del DT del kernel en ejecución. (Sección 6.)

**Overlay**: Un `.dtb` parcial que modifica un árbol existente. Apunta a nodos por etiqueta o ruta y fusiona contenido bajo ellos. (Sección 5.)

**phandle**: Phantom handle. Un identificador entero de 32 bits para un nodo DT, utilizado para hacer referencias cruzadas entre nodos dentro del árbol.

**pinctrl**: Framework de control de pines. Gestiona la multiplexación de los pines del SoC entre sus posibles funciones. (Sección 8.)

**PNP info**: Metadatos que un driver publica para identificar las compatible strings DT que soporta. Utilizado por `devmatch(8)` para la carga automática. (Sección 9.)

**Probe**: El método del driver que inspecciona un dispositivo candidato e informa de si puede controlarlo. Devuelve una puntuación de idoneidad o un error.

**Property**: Un valor con nombre en un nodo DT. Los valores pueden ser cadenas, listas de cells o cadenas de bytes opacas.

**Reg**: Una propiedad que lista uno o más pares (dirección, tamaño) que describen los rangos MMIO que ocupa el periférico.

**Root controller**: El controlador de interrupciones en la cima de la jerarquía. En sistemas arm64, suele ser un GIC.

**SBC**: Single-Board Computer. Una placa embebida con CPU, memoria y periféricos en una única PCB. Ejemplos: Raspberry Pi, BeagleBone.

**SIMPLEBUS_PNP_INFO**: Una macro que exporta la tabla de compatibilidad de un driver como metadatos del módulo. (Sección 9.)

**Simplebus**: El driver de FreeBSD que hace probe de los hijos DT cuyo padre tiene `compatible = "simple-bus"`. Convierte los nodos DT en dispositivos Newbus.

**softc**: Abreviación de "soft context" (contexto software). Una estructura de estado por dispositivo asignada por Newbus y pasada a los métodos del driver. (Sección 4.)

**SoC**: System on Chip. Un circuito integrado que contiene CPU, controlador de memoria y numerosos bloques periféricos.

**Status**: Una propiedad en un nodo DT que indica si el dispositivo está habilitado (`"okay"`) o no. Si falta, el valor predeterminado es okay.

**sysctl**: La interfaz de control del sistema de FreeBSD. Un driver puede publicar parámetros configurables que el espacio de usuario lee y escribe.

**Target**: En un overlay, el nodo del árbol base que un fragment modifica.

**Unit address**: La parte numérica tras el `@` en el nombre de un nodo. Indica dónde vive el nodo dentro del espacio de direcciones del padre.

**Vendor prefix**: La parte de una compatible string anterior a la coma. Identifica a la organización responsable del binding.

## Preguntas frecuentes

Estas son preguntas que surgen repetidamente cuando alguien escribe por primera vez drivers FDT para FreeBSD. La mayoría ya están respondidas en algún lugar del capítulo; el formato de preguntas frecuentes simplemente pone la respuesta breve en un único lugar.

**¿Necesito saber ensamblador ARM para escribir un driver FDT?**

No. El objetivo del framework de drivers es precisamente ese: trabajar en C contra una API uniforme. Es posible que leas desensamblado si estás depurando un crash de muy bajo nivel, pero eso es la excepción, no la norma.

**¿Puedo escribir drivers FDT en amd64, o necesito hardware arm64?**

Puedes desarrollar en amd64 y hacer cross-build para arm64. También puedes ejecutar FreeBSD arm64 bajo QEMU en un host amd64, que es el flujo de trabajo más habitual para quien no quiere esperar a que una Pi reinicie. Para la validación final necesitarás hardware real o un emulador fiel, pero la iteración diaria cabe en un portátil.

**¿Cuál es la diferencia entre simplebus y ofwbus?**

`ofwbus` es el bus raíz de nivel superior para la enumeración de dispositivos derivada de Open Firmware. `simplebus` es un driver de bus genérico que cubre los nodos DT compatibles con `"simple-bus"` y enumeraciones simples similares. La mayoría de tus drivers se registrarán con ambos; `ofwbus` gestiona la raíz y los casos especializados, mientras que `simplebus` gestiona la inmensa mayoría de los buses de periféricos.

**¿Por qué necesita mi driver registrarse tanto en ofwbus como en simplebus?**

Algunos nodos aparecen bajo `simplebus`, otros directamente bajo `ofwbus` (especialmente en sistemas donde la estructura del árbol es inusual). Registrarse en ambos significa que el driver hace attach dondequiera que acabe estando el nodo.

**¿Por qué no se aplica mi overlay?**

Repasa la lista de comprobación de la Sección 6. La causa más habitual, por orden de frecuencia: nombre de archivo o directorio incorrecto, error tipográfico en `fdt_overlays`, incompatibilidad de la compatible base, falta de `-@` al compilar el overlay.

**¿Puede un driver abarcar varios nodos DT?**

Sí. Una única instancia de driver normalmente coincide con un nodo, pero la función attach puede recorrer hijos o referencias phandle para recopilar estado de varios nodos. Consulta la discusión de `gpioled_fdt.c` en la Sección 9 para el caso de los hijos.

**¿Cómo gestiono un dispositivo que tiene tanto descripción ACPI como FDT?**

Escribe dos rutas de compatibilidad. La mayoría de los drivers grandes de FreeBSD que soportan ambas plataformas hacen exactamente esto: funciones probe separadas se registran en cada bus, y el código compartido vive en el attach común. Consulta `sdhci_acpi.c` y `sdhci_fdt.c` como ejemplo resuelto.

**¿Qué pasa con los DT bindings que no encuentro en el árbol de FreeBSD?**

La fuente de verdad para los DT bindings es la especificación upstream del device-tree más la documentación del kernel Linux. FreeBSD utiliza los mismos bindings cuando es práctico. Si necesitas un binding que FreeBSD aún no soporta, normalmente puedes portar el driver Linux correspondiente o escribir un driver nativo de FreeBSD que consuma el mismo binding.

**¿Necesito modificar el kernel para añadir un nuevo driver?**

No. Los módulos fuera del árbol se compilan contra las cabeceras de `/usr/src/sys` y se cargan en tiempo de ejecución. Solo modificas el árbol del kernel cuando contribuyes un driver upstream o cuando necesitas cambiar una parte genérica de la infraestructura.

**¿Cómo hago cross-compilación para arm64 desde un host amd64?**

Utiliza la cadena de herramientas cruzada incluida en el sistema de build de FreeBSD:

```console
$ make TARGET=arm64 TARGET_ARCH=aarch64 buildworld buildkernel
```

Esto construye una imagen completa del sistema arm64 en tu estación de trabajo amd64. Las compilaciones de solo módulos siguen el mismo patrón con targets más específicos.

**¿Hay alguna forma de cargar mi driver automáticamente cuando aparece un nodo DT coincidente?**

Sí, mediante `devmatch(8)` y la macro `SIMPLEBUS_PNP_INFO`. Declara tu tabla de compatibilidad, incluye `SIMPLEBUS_PNP_INFO(compat_data)` en el código fuente del driver, y `devmatch` la detectará.

**¿Puedo usar C++ para un driver FDT?**

No. El código del kernel FreeBSD es estrictamente C (y una pequeñísima cantidad de ensamblador). No se soportan otros lenguajes, y la API del kernel asume las convenciones de C.

**¿Cómo depuro un crash durante el análisis de DT en el boot?**

Los crashes tempranos son difíciles de depurar. Las técnicas habituales: habilita los mensajes de boot detallados (`-v` en `loader.conf`), compila el kernel con `KDB` y `DDB`, usa una consola serie y conecta un depurador JTAG si dispones de uno. Considera también añadir sentencias `printf` temporales directamente en el código de análisis FDT bajo `/usr/src/sys/dev/ofw/`.

**¿Se interfieren entre sí los módulos de drivers que comparten FDT bindings?**

No. Cada módulo registra su tabla de compatibilidad, y la puntuación de probe de los drivers coincidentes decide cuál gana. Si dos drivers reclaman el mismo compatible con la misma puntuación, el orden en que fueron cargados determina el ganador. Asigna a cada driver una puntuación distintiva para evitar sorpresas.

**¿Cómo preservo el estado entre ciclos de kldload/kldunload?**

No puedes. Un módulo que se descarga pierde todo su estado. Si tu driver necesita persistencia, escribe en un archivo, en la forma configurable de un sysctl, en un tunable del kernel, o en una ubicación de NVRAM o EEPROM que sobreviva al módulo. Para depuración, imprimir el estado en `dmesg` antes de descargar y volver a analizarlo tras la siguiente carga es un atajo viable.

**¿El orden de la lista `compatible` es significativo?**

Sí. Un nodo puede listar varias cadenas `compatible`, de la más específica a la menos específica. `compatible = "brcm,bcm2711-gpio", "brcm,bcm2835-gpio";` declara que el nodo es principalmente una variante del 2711, pero es compatible con el binding del 2835 más antiguo como alternativa. Un driver que declare soporte para el compatible del 2835 coincidirá con este nodo si no hay ningún driver para el 2711 cargado. Este orden permite que el firmware describa un dispositivo en varios niveles de detalle, de modo que los kernels más nuevos puedan sacar partido de las mejoras sin dejar de funcionar con los más antiguos.

**¿Por qué FreeBSD usa a veces nombres de propiedades distintos a los de Linux?**

La mayoría de las propiedades DT son compartidas, pero algunas difieren intencionalmente allí donde el comportamiento de FreeBSD se separa de las expectativas de Linux. Cuando portes un driver, lee con atención los bindings existentes en el lado de FreeBSD; asumir en silencio la semántica de Linux es una fuente habitual de errores de portado.

**¿Cuál es la relación entre Newbus e intrng?**

Newbus gestiona el probe, el attach y la asignación de recursos del dispositivo. intrng gestiona el registro del controlador de interrupciones y el enrutamiento de IRQ. Ambos subsistemas interoperan: la asignación de recursos de Newbus para `SYS_RES_IRQ` pasa por intrng para localizar el controlador correcto, e intrng despacha las interrupciones de vuelta al manejador del driver que Newbus registró.

## Cerrando

Este capítulo te ha llevado desde «FreeBSD es un sistema operativo de propósito general» hasta «puedo escribir un driver que encaje en el Device Tree de un sistema embebido». Las dos afirmaciones no son equivalentes. La primera trata de usar el kernel que ya conoces; la segunda trata de entender un vocabulario completamente nuevo para describir el hardware sobre el que corre ese kernel.

Empezamos por ver qué es realmente el FreeBSD embebido: un sistema compacto y capaz que corre en SBCs, placas industriales y hardware construido con un propósito específico. Vimos cómo esos sistemas no se describen a sí mismos mediante la enumeración PCI o las tablas ACPI, sino mediante un Device Tree estático que el firmware entrega al kernel en el momento del boot.

A continuación aprendimos el lenguaje del Device Tree en sí: nodos con nombres y direcciones de unidad, propiedades con valores estructurados en celdas, phandles para referencias cruzadas y la sintaxis `/plugin/;` de overlay que nos permite añadir o modificar nodos sin reconstruir el blob base. El lenguaje requiere una tarde para acostumbrarse; los hábitos que construye, pensar en el hardware como un árbol con padres, direcciones y tipos, duran toda una carrera.

Con el lenguaje en mano, estudiamos la maquinaria que FreeBSD utiliza para consumir un DT. El subsistema `fdt(4)` carga el blob. La API OFW lo recorre. Los helpers `ofw_bus_*` exponen ese recorrido en términos de cadenas de compatibilidad y comprobaciones de estado. El driver `simplebus(4)` enumera los hijos. Y los frameworks de consumo (`clk`, `regulator`, `hwreset`, `pinctrl`, GPIO) se integran todos con las referencias de phandle del DT para que los drivers puedan adquirir sus recursos mediante un patrón uniforme.

Con esa maquinaria como base, construimos un driver. Primero `fdthello`, el esqueleto mínimo, que muestra la forma requerida de un driver FDT en su expresión más pura. Después `edled`, un driver completo de LED controlado por GPIO que ilustra attach, detach, sysctl y una gestión correcta de recursos. Por el camino vimos cómo compilar overlays, desplegarlos a través de `loader.conf`, inspeccionar el árbol en tiempo de ejecución con `ofwdump` y depurar la clase de fallos que los drivers embebidos presentan de forma exclusiva.

Por último, llevamos `edled` a través de las pasadas de refactorización que convierten un driver que funciona en uno terminado: rutas de error ajustadas, un mutex real, gestión opcional de alimentación y reloj, conciencia de pinctrl, una auditoría de estilo. Es el trabajo que distingue un juguete de un driver que estarías cómodo ejecutando en producción.

Los laboratorios del capítulo te dieron la oportunidad de ejecutar todo tú mismo. Los ejercicios de desafío te dejaron margen para extenderlo. La sección de resolución de problemas te dio un sitio al que acudir cuando algo falla.

### Qué deberías ser capaz de hacer ahora

Si trabajaste los laboratorios y leíste las explicaciones con atención, ahora puedes:

- Leer un `.dts` desconocido y explicar qué hardware describe.
- Escribir un `.dts` o `.dtso` nuevo que añada un periférico a una placa existente.
- Compilar ese fuente en un binario y desplegarlo en un sistema en ejecución.
- Escribir un driver FreeBSD con conocimiento de FDT desde cero.
- Adquirir recursos de memoria, IRQ, GPIO, reloj y regulador a través de las API de consumo estándar.
- Depurar las clases de problemas en que el probe no se dispara, el attach falla y el detach se bloquea.
- Identificar cuándo una placa está corriendo en modo FDT frente a modo ACPI en arm64, y qué implica eso para tu driver.

### Lo que este capítulo deja implícito

Hay tres temas que el capítulo deja para otras fuentes. No tienen forma de libro; tienen forma de referencia, y el libro se atrancaría si intentara cubrirlos por completo.

El primero, la especificación completa de bindings del DT. Cubrimos las propiedades que con mayor probabilidad vas a usar. El catálogo completo de bindings, mantenido por la comunidad upstream de device-tree, puede consultarse en la documentación en línea del árbol `Documentation/devicetree/bindings/` del kernel Linux. FreeBSD sigue la mayoría de esos bindings, con las excepciones indicadas en `/usr/src/sys/contrib/device-tree/Bindings/` para el subconjunto del que FreeBSD depende.

El segundo, el enrutamiento de interrupciones en SoCs complejos. Tocamos el encadenamiento de `interrupt-parent`. Una placa con múltiples controladores de estilo GIC, interrupciones gestionadas por pinctrl y nodos gpio-as-interrupt anidados puede llegar a ser intrincada. El subsistema `intrng` de FreeBSD es el lugar al que acudir cuando el caso simple ya no es suficiente.

El tercero, el soporte FDT para periféricos que no pasan por simplebus: PHYs USB, árboles de reloj en SoCs con PLLs jerárquicos, escalado de voltaje para DVFS. Cada uno es su propio subtema. El apéndice del libro apunta a las fuentes canónicas.

### Puntos clave

Si el capítulo puede destilarse en una sola página, estos son los puntos que merece la pena recordar:

1. **Los sistemas embebidos se describen a sí mismos mediante Device Tree, no mediante enumeración en tiempo de ejecución.** El blob que el firmware entrega al kernel es la descripción autoritativa del hardware presente.

2. **Un driver hace match mediante cadenas de compatibilidad.** Tu probe consulta una tabla de compatibilidad que compara contra la propiedad `compatible` del nodo. Escribe esta cadena exactamente bien.

3. **`simplebus(4)` es el enumerador.** El padre de todo driver FDT es `simplebus` o `ofwbus`. Regístralos con ambos en el momento de cargar el módulo.

4. **Los recursos vienen del framework.** `bus_alloc_resource_any` para MMIO e IRQ, `gpio_pin_get_by_ofw_*` para GPIO, y las llamadas `clk_*`, `regulator_*`, `hwreset_*` para alimentación y reset. Libera cada uno en detach.

5. **Los overlays modifican el árbol sin reconstruirlo.** Un pequeño `.dtbo` colocado bajo `/boot/dtb/overlays/` y listado en `fdt_overlays` es una forma limpia de añadir o habilitar periféricos en una placa concreta.

6. **El cableado de interrupciones sigue una cadena de padres.** Las propiedades `interrupt-parent` e `interrupts` de un nodo se conectan a través de intrng hasta el controlador raíz. Entender esa cadena es fundamental para depurar interrupciones silenciosas.

7. **Los drivers reales son la mejor referencia.** `/usr/src/sys/dev/gpio/`, `/usr/src/sys/dev/fdt/` y el árbol de plataforma de cada SoC contienen docenas de drivers FDT que demuestran todos los patrones que probablemente vayas a necesitar.

8. **Las rutas de error y el desmontaje importan.** Un driver que carga y funciona una vez es el caso fácil. Un driver que carga, descarga y recarga limpiamente cien veces es el caso robusto.

9. **Las herramientas son simples pero efectivas.** `dtc`, `ofwdump`, `fdtdump`, `devinfo -r`, `sysctl dev.<name>.<unit>` y `kldstat` cubren juntos casi toda inspección que vayas a necesitar.

10. **FDT es solo uno de varios sistemas de descripción de hardware.** En servidores arm64, ACPI ocupa el mismo papel. Los patrones de driver que aprendiste aquí se transfieren, pero la capa de match cambia.

### Antes de continuar

Antes de dar este capítulo por terminado y pasar al Capítulo 33, tómate un momento para verificar lo siguiente. Estas son las comprobaciones que distinguen a quien ha visto el material de quien lo ha hecho suyo.

- Puedes esbozar, sin mirar, el esqueleto de un driver con conocimiento de FDT, incluyendo la tabla de compatibilidad, probe, attach, detach, la tabla de métodos y el registro con `DRIVER_MODULE`.
- Puedes tomar un párrafo de fuente DT y narrar qué describe, nodo a nodo, propiedad a propiedad.
- Puedes distinguir un phandle de una referencia por ruta y explicar cuándo usarías cada uno.
- Puedes explicar por qué `MODULE_DEPEND(your_driver, gpiobus, 1, 1, 1)` importa y qué falla sin él.
- Sabes qué tunable del loader controla la carga de overlays y dónde viven los archivos `.dtbo`.
- Ante un probe que no se dispara, tienes una lista mental de cuatro o cinco cosas que verificar antes de recurrir a un depurador.
- Sabes qué hace `SIMPLEBUS_PNP_INFO` y por qué importa en drivers de producción.
- Puedes nombrar al menos tres drivers FDT reales en `/usr/src/sys` y describir, en una frase cada uno, qué demuestran.

Si alguno de estos puntos todavía se siente inestable, vuelve a la sección correspondiente y léela de nuevo. El material del capítulo se acumula; el Capítulo 33 asumirá que has interiorizado la mayor parte de lo que hay aquí.

### Una nota sobre la práctica sostenida

El FreeBSD embebido recompensa la práctica regular. La primera vez que lees un `.dts` de una placa real, parece abrumador. La décima vez, ojeas buscando los nodos que te interesan. La centésima vez, editas directamente. Si tienes una SBC de repuesto sobre tu escritorio, ponla a trabajar. Elige un sensor, escribe un driver, añade una línea a un `loader.conf`. La habilidad se acumula. Cada pequeño driver que terminas es una plantilla para el siguiente, y los patrones se trasladan de placa en placa y de vendedor en vendedor.

### Conectando con el resto del libro

El Capítulo 32 se sitúa al final de la Parte 7, y el material de aquí descansa sobre capas que encontraste antes. Un breve recorrido por lo que ahora ves con nueva luz:

De las Partes 1 y 2, el esqueleto del módulo del kernel, el registro con `DRIVER_MODULE` y el ciclo de vida `kldload`/`kldunload`. La forma de un driver FDT es la misma forma, con una estrategia de probe diferente.

De la Parte 3, el framework Newbus. `simplebus` es un driver de bus que obtiene sus hijos de un Device Tree en lugar de un protocolo de bus. Todos los patrones de Newbus que aprendiste antes aplican aquí, sin cambios.

De la Parte 4, las interfaces del driver hacia el espacio de usuario: `cdev`, `sysctl`, `ioctl`. Nuestro driver `edled` usó sysctl como interfaz de control; en un proyecto mayor podría añadir un dispositivo de caracteres o incluso un socket netlink.

De las Partes 5 y 6, los capítulos prácticos sobre concurrencia, pruebas y depuración del kernel. Todas esas herramientas aplican plenamente a los drivers embebidos. La única diferencia es que el hardware suele ser más difícil de alcanzar, por lo que las herramientas importan aún más.

De la Parte 7, el Capítulo 29 sobre plataformas de 32 bits y el Capítulo 30 sobre virtualización tocan ambos ángulos embebidos. El Capítulo 31, dedicado a la seguridad, aplica directamente: un driver en un dispositivo embebido suele estar expuesto a lo que sea que corra en el espacio de usuario del producto, y los mismos patrones defensivos aplican.

El panorama tras el Capítulo 32 es que tienes un kit de herramientas de inicio completo para la mayoría de las clases de trabajo de dispositivos FreeBSD. Puedes escribir drivers de caracteres, drivers de bus, drivers de red y ahora drivers embebidos basados en FDT. Los capítulos restantes de la Parte 7 refinan y perfeccionan esas habilidades.

### Lo que viene

El próximo capítulo, el Capítulo 33, pasa del «¿funciona?» al «¿qué tal funciona?». El ajuste de rendimiento y el profiling son el arte de medir lo que el kernel hace realmente, bajo carga real, con datos reales. En un sistema embebido, la pregunta cobra especial fuerza. El hardware suele ser pequeño, la carga de trabajo suele ser fija, y el margen entre «funciona bien» y «funciona mal» se mide en microsegundos. En el Capítulo 33 estudiamos las herramientas que FreeBSD pone a tu disposición para medir: `hwpmc`, `pmcstat`, DTrace, flame graphs y las propias sondas de contadores de rendimiento del kernel. También estudiamos el modelo mental que necesitas para interpretar lo que dicen las herramientas, y los errores que conlleva ajustar lo que no toca.

Más allá del Capítulo 33, los Capítulos 34 al 38 completan el arco de maestría: estilo de código y legibilidad, contribuir a FreeBSD, portar drivers entre plataformas, documentar tu trabajo y un proyecto final que une varios hilos anteriores. Cada uno construye sobre lo que has hecho aquí. El driver `edled` que escribiste en este capítulo, extendido a través de los ejercicios de desafío, es un candidato perfectamente razonable para llevarlo adelante a esos últimos capítulos como ejemplo continuado.

El trabajo con el driver ha terminado. El trabajo de medición comienza.

### Una última palabra

Un último aliento antes de cerrar. El FreeBSD embebido tiene una comunidad tranquila y constante. Las placas son baratas, el código es abierto, las listas de correo son pacientes. Si perseveras, descubrirás que cada driver que escribes te enseña algo que el siguiente necesita. El driver `edled` de este capítulo es algo pequeño, y sin embargo al escribirlo tocaste el parser FDT, la API OFW, el framework de consumo GPIO, el árbol Newbus y el mecanismo sysctl. Eso no es poca cosa. Es la columna vertebral de todo driver FDT del árbol, ejercitada en miniatura.

Mantén la práctica. El campo recompensa la curiosidad con progreso constante. El próximo capítulo te da las herramientas para asegurarte de que ese progreso avanza en la dirección correcta.

Cuando finalmente te sientes frente a una placa desconocida, el panorama ya no te resultará ajeno. Sabrás pedir su `.dts`, repasar los periféricos enumerados bajo `soc`, comprobar qué controlador GPIO aloja el pin que te interesa y buscar el compatible string que necesitas hacer coincidir. Los hábitos que acabas de construir te acompañan. Se aplican a una Raspberry Pi, a una BeagleBone, a una placa industrial personalizada, a una máquina QEMU virt. Se aplicarán dentro de diez años en placas que aún no existen, porque el lenguaje subyacente de descripción del hardware es estable.

Esa portabilidad es, en definitiva, para lo que fue diseñado el Device Tree, y para lo que fue diseñado este capítulo. Bienvenido al FreeBSD embebido.

Con el Capítulo 32 en la mano, tu conjunto de herramientas está casi completo. Pasemos al Capítulo 33, y al trabajo de medición que te dirá si el driver que acabas de escribir es tan eficiente como necesita serlo.

Buena lectura, y buen desarrollo de drivers.
