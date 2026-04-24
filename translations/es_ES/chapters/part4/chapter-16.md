---
title: "Acceso al hardware"
description: "La Parte 4 comienza con el primer capítulo que enseña al driver a comunicarse directamente con el hardware: qué significa el I/O de hardware, cómo difiere el I/O mapeado en memoria del I/O mapeado en puertos, cómo bus_space(9) proporciona a los drivers de FreeBSD un vocabulario portable para el acceso a registros, cómo simular un bloque de registros en la memoria del kernel para que el lector pueda aprender sin hardware real, cómo integrar el acceso estilo registro en el driver myfirst en evolución, y cómo mantener el MMIO seguro bajo concurrencia."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 16
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 195
language: "es-ES"
---
# Acceso al hardware

## Orientación al lector y objetivos

La Parte 3 concluyó con un driver que sabía coordinarse a sí mismo. El módulo `myfirst` en la versión `0.9-coordination` tiene un mutex que protege su ruta de datos, un par de condition variables que permiten a los lectores y escritores esperar pacientemente el estado del buffer que necesitan, un sx compartido y exclusivo que protege su configuración, tres callouts que le proporcionan tiempo interno, un taskqueue privado con tres tareas que difieren trabajo fuera de contextos restringidos, un semaphore de conteo que limita los escritores concurrentes, un indicador atómico que transmite el relato del apagado en todos los contextos y un pequeño header que nombra cada primitiva de sincronización que utiliza el driver. Los capítulos que lo construyeron introdujeron siete primitivas, las unieron con un único orden de detach y documentaron toda la historia en un `LOCKING.md` vivo.

Lo que el driver aún no tiene es una historia de hardware. Todos los invariantes que coordina son internos. Cada byte que fluye a través de él se origina en el espacio de usuario mediante `write(2)` o es producido por un callout de su propia imaginación. Nada en el driver alcanza el exterior de la memoria propia del kernel. Un driver de FreeBSD real existe generalmente porque hay un dispositivo con el que comunicarse: una tarjeta de red, un controlador de almacenamiento, un puerto serie, un sensor, una FPGA personalizada, una GPU. Esa conversación es de lo que trata la Parte 4, y el Capítulo 16 es donde comienza.

El alcance del Capítulo 16 es deliberadamente estrecho. Enseña el modelo mental de I/O de hardware y el vocabulario de `bus_space(9)`, la abstracción de FreeBSD que permite a un único driver comunicarse con un dispositivo de la misma manera en todas las arquitecturas soportadas. Recorre un bloque de registros simulado para que el lector pueda practicar el acceso estilo registro sin necesidad de tener hardware real, e integra esa simulación en el driver `myfirst` de una manera que evoluciona de forma natural a partir del Capítulo 15, en lugar de descartar el driver. Cubre las reglas de seguridad que exige MMIO (barreras de memoria, ordenación de accesos, locking en torno a registros compartidos) y muestra cómo depurar y rastrear el acceso a nivel de registro. Termina con una pequeña refactorización que separa el código de acceso al hardware de la lógica de negocio del driver, preparando la organización de archivos en la que se apoyarán todos los capítulos posteriores de la Parte 4.

Varias cuestiones se posponen deliberadamente para que el Capítulo 16 pueda detenerse en el propio vocabulario. El comportamiento dinámico de los registros, los cambios de estado dirigidos por callouts y la inyección de fallos corresponden al Capítulo 17. Los dispositivos PCI reales, el mapeo de BAR, la coincidencia de identificadores de fabricante y dispositivo, `pciconf` y `pci(4)`, y el pegamento newbus que une los drivers PCI al subsistema de bus corresponden al Capítulo 18. Las interrupciones y su división entre manejadores de filtro y threads de interrupción corresponden al Capítulo 19. DMA y la programación de bus master corresponden a los Capítulos 20 y 21. El Capítulo 16 se mantiene dentro del terreno que puede cubrir bien, y cede explícitamente cuando un tema merece su propio capítulo.

La Parte 4 comienza aquí, y un comienzo merece una pequeña pausa. La Parte 3 te enseñó cómo comportarte dentro del driver cuando muchos actores tocan estado compartido. La Parte 4 enseña cómo el driver se extiende más allá de sí mismo. Un manejador de interrupciones se ejecuta en un contexto que la Parte 3 te enseñó a razonar, y accede a memoria que la Parte 4 te enseñará a mapear. Una escritura en un registro en la Parte 4 debe respetar un lock que la Parte 3 te enseñó a gestionar. Las disciplinas se superponen. El Capítulo 16 es tu primera práctica con `bus_space(9)`; la disciplina de la Parte 3 es lo que mantiene esa práctica honesta.

### Por qué `bus_space(9)` merece un capítulo propio

Puede que ya te estés preguntando si MMIO realmente necesita un capítulo entero. ¿Por qué no saltar directamente a la simulación del Capítulo 17 o al trabajo con PCI real del Capítulo 18 y aprender el vocabulario de accesores de paso? Si ya has utilizado `bus_space(9)` antes, las llamadas primitivas de este capítulo no te serán nuevas.

Lo que el Capítulo 16 añade es el modelo mental. El I/O de hardware es un tema en el que un pequeño conjunto de ideas bien comprendidas rinde frutos en cada capítulo posterior, y un pequeño conjunto de ideas mal asimiladas produce errores silenciosos y persistentes que son difíciles de encontrar más adelante. La distinción entre I/O mapeado en memoria y I/O por puertos es sencilla una vez que la tienes clara, y confusa hasta entonces. El significado de `bus_space_tag_t` y `bus_space_handle_t` es sencillo una vez que ves lo que representan, y opaco hasta entonces. La razón por la que las barreras de memoria importan en torno al acceso a registros es obvia una vez que has reflexionado sobre la coherencia de caché y la reordenación por parte del compilador, e irrelevante hasta el día en que un driver se comporta mal por razones que no puedes explicar.

El capítulo también se gana su lugar por ser aquel en el que el driver `myfirst` adquiere su primera capa orientada al hardware. Hasta ahora, el softc contenía únicamente estado interno: un buffer circular, algunos locks, algunos contadores, algunos indicadores. Después del Capítulo 16, el softc contendrá un bloque de registros simulado y los accesores que lo leen y escriben. Ese cambio en la forma del driver es pequeño, pero formativo. Establece la organización de archivos y la disciplina de locking que todos los capítulos posteriores de la Parte 4 ampliarán. Saltarse el Capítulo 16 dejaría al lector intentando aprender los modismos de acceso a registros en medio del aprendizaje de PCI, interrupciones o DMA. Hacerlos de uno en uno es más amable.

### Dónde dejó el Capítulo 15 al driver

Un breve punto de control antes de continuar. El Capítulo 16 extiende el driver producido al final de la Etapa 4 del Capítulo 15, etiquetado como versión `0.9-coordination`. Si alguno de los puntos siguientes te resulta incierto, vuelve al Capítulo 15 antes de comenzar este.

- Tu driver `myfirst` compila sin errores y se identifica como versión `0.9-coordination`.
- El softc contiene un mutex de ruta de datos (`sc->mtx`), un sx de configuración (`sc->cfg_sx`), un sx de caché de estadísticas (`sc->stats_cache_sx`), dos condition variables (`sc->data_cv`, `sc->room_cv`), tres callouts (`heartbeat_co`, `watchdog_co`, `tick_source_co`), un taskqueue privado (`sc->tq`) con cuatro tareas (`selwake_task`, `bulk_writer_task`, `reset_delayed_task`, `recovery_task`) y un semaphore de conteo (`writers_sema`) que limita los escritores concurrentes.
- Un header `myfirst_sync.h` encapsula cada operación de sincronización bajo funciones inline con nombre.
- El orden de lock `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx` está documentado en `LOCKING.md` y es aplicado por `WITNESS`.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` están habilitados en tu kernel de pruebas, y lo has compilado y arrancado.
- El kit de pruebas de estrés del Capítulo 15 funciona correctamente bajo el kernel de depuración.

Ese driver es lo que el Capítulo 16 extiende. Las adiciones son, de nuevo, modestas en volumen: un nuevo header (`myfirst_hw.h`), una nueva estructura dentro del softc (un bloque de registros simulado), un puñado de accesores auxiliares, una pequeña tarea impulsada por hardware y un conjunto de barreras y locks en torno al acceso a registros. El cambio en el modelo mental es mayor de lo que sugiere el número de líneas.

### Qué aprenderás

Al terminar este capítulo, deberías ser capaz de:

- Describir qué significa I/O de hardware en el contexto de un driver y por qué un driver normalmente no puede acceder a la memoria de un dispositivo simplemente desreferenciando un puntero.
- Distinguir entre I/O mapeado en memoria (MMIO) e I/O por puertos (PIO), y explicar por qué MMIO domina los drivers modernos de FreeBSD en plataformas actuales.
- Explicar qué es un registro, qué es un desplazamiento y qué significa un campo de control o estado, utilizando el vocabulario que emplean las hojas de datos de dispositivos reales.
- Leer una tabla simple de mapa de registros y traducirla a un header C de desplazamientos y máscaras de bits.
- Describir los roles de `bus_space_tag_t` y `bus_space_handle_t` y por qué son una abstracción y no simplemente un puntero.
- Reconocer la forma de `bus_space_read_*`, `bus_space_write_*`, `bus_space_barrier`, `bus_space_read_multi_*`, `bus_space_read_region_*` y sus equivalentes abreviados `bus_*` definidos sobre un `struct resource *`.
- Simular un bloque de registros en memoria del kernel, envolver el acceso al mismo detrás de accesores auxiliares y usar esos accesores para construir un pequeño dispositivo visible desde el driver que se comporta como un dispositivo MMIO real sin tocar hardware real.
- Integrar el acceso a registros simulados en el driver `myfirst` en evolución, de modo que la ruta de datos lea y escriba a través de accesores de registro en lugar de acceder a un buffer en bruto.
- Identificar cuándo una lectura de registro tiene efectos secundarios y cuándo los tiene una escritura, y por qué eso importa para la caché, la reordenación del compilador y la depuración.
- Usar `bus_space_barrier` correctamente para imponer el orden de acceso cuando sea necesario, y reconocer cuándo no lo es.
- Proteger el estado compartido de registros con el tipo correcto de lock, y evitar bucles de espera activa que priven de recursos a otros threads.
- Registrar el acceso a registros de una manera que ayude a depurar un driver sin inundar al lector con ruido.
- Refactorizar un driver de modo que su capa de acceso al hardware sea una unidad de código con nombre, documentada y comprobable.
- Etiquetar el driver como versión `0.9-mmio`, actualizar `LOCKING.md` y `HARDWARE.md`, y ejecutar la suite de regresión completa con el acceso al hardware habilitado.

La lista es larga; cada elemento es concreto. El objetivo del capítulo es la composición.

### Qué no cubre este capítulo

Varios temas adyacentes se posponen explícitamente para que el Capítulo 16 se mantenga centrado.

- **Simulación completa de hardware con comportamiento dinámico.** La simulación de este capítulo es suficientemente estática para enseñar el vocabulario de acceso a registros. El Capítulo 17 hace la simulación dinámica, con temporizadores que cambian registros de estado, eventos que invierten bits de listo e inyección de fallos.
- **El subsistema PCI.** `pci(4)`, la coincidencia de identificadores de fabricante y dispositivo, `pciconf`, `pci_enable_busmaster`, el mapeo de BAR, MSI y MSI-X, y las particularidades de la gestión de energía corresponden al Capítulo 18. El Capítulo 16 menciona PCI solo cuando es útil decir "aquí es de donde vendría tu BAR si esto fuera real".
- **Manejadores de interrupciones.** `bus_setup_intr(9)`, los manejadores de filtro, los threads de interrupción, `INTR_MPSAFE` y la división filtro más tarea corresponden al Capítulo 19. El Capítulo 16 los menciona solo para explicar por qué importa una lectura con efectos secundarios.
- **DMA.** `bus_dma(9)`, `bus_dma_tag_create`, `bus_dmamap_load`, los bounce buffers, el vaciado de caché en torno a DMA y las listas scatter-gather corresponden a los Capítulos 20 y 21.
- **Particularidades de acceso a registros específicas de arquitectura.** Los modelos de memoria débilmente ordenados, los atributos de memoria de dispositivo en arm64, el intercambio de bytes big-endian en MIPS y PowerPC, y las cachés no coherentes en algunas plataformas embebidas se mencionan de pasada; un tratamiento profundo corresponde a los capítulos de portabilidad.
- **Casos de estudio de drivers del mundo real.** El Capítulo 16 señala `if_ale.c`, `if_em.c` y `uart_bus_pci.c` como ejemplos de los patrones enseñados aquí, pero no los disecciona en profundidad. Los capítulos posteriores hacen ese trabajo donde encaja con sus propios temas.

Mantenerse dentro de esos límites convierte el Capítulo 16 en un capítulo sobre el vocabulario del acceso al hardware. El vocabulario es lo que se transfiere; los subsistemas específicos son aquello a lo que los Capítulos 17 al 22 aplican ese vocabulario.

### Tiempo estimado de dedicación

- **Solo lectura**: de tres a cuatro horas. El vocabulario es reducido, pero requiere reflexionar cuidadosamente sobre cada término.
- **Lectura más escritura de los ejemplos trabajados**: de siete a nueve horas en dos sesiones. El driver evoluciona en cuatro etapas y cada etapa es una refactorización pequeña pero real.
- **Lectura más todos los laboratorios y desafíos**: de doce a quince horas en tres o cuatro sesiones, incluyendo pruebas de estrés y la lectura de algunos drivers reales de FreeBSD.

Las secciones 2 y 3 son las más densas. Si la abstracción de un tag y un handle resulta opaca en la primera lectura, eso es normal. Para, vuelve a leer el mapeo trabajado en la Sección 3 y continúa cuando la estructura haya asentado.

### Requisitos previos

Antes de comenzar este capítulo, confirma:

- El código fuente de tu driver corresponde a la Etapa 4 del Capítulo 15 (`stage4-final`). El punto de partida da por supuesto que conoces cada primitiva del Capítulo 15, cada taskqueue del Capítulo 14, cada callout del Capítulo 13, cada cv y sx del Capítulo 12 y el modelo de IO concurrente del Capítulo 11.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con `/usr/src` en disco y sincronizado con el kernel en ejecución.
- Un kernel de depuración con `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` está compilado, instalado y arrancando sin problemas.
- Comprendes el orden de detach del Capítulo 15 con la suficiente solidez como para extenderlo sin perderte.
- Te sientes cómodo leyendo offsets hexadecimales y máscaras de bits.

Si alguno de los puntos anteriores no está del todo claro, arréglalo ahora en lugar de avanzar hasta el Capítulo 16 e intentar razonar sobre una base inestable. El código de acceso al hardware es muy sensible a los pequeños errores, y un kernel de depuración detecta la mayoría de ellos en el primer contacto.

### Cómo sacar el máximo partido de este capítulo

Tres hábitos darán sus frutos rápidamente.

En primer lugar, ten a mano `/usr/src/sys/sys/bus.h` y `/usr/src/sys/x86/include/bus.h`. El archivo `bus.h` en `/usr/src/sys/sys/` define las macros abreviadas `bus_read_*` y `bus_write_*` sobre un `struct resource *`. El archivo `bus.h` específico de arquitectura en `/usr/src/sys/x86/include/` (o su equivalente para tu plataforma) define las funciones de nivel inferior `bus_space_read_*` y `bus_space_write_*` y muestra exactamente en qué se compilan. Leer esos dos archivos una sola vez lleva unos treinta minutos y disipa casi todo el misterio del capítulo.

En segundo lugar, compara cada nuevo accessor con lo que habrías escrito en C puro. Plantearse la pregunta «¿cómo expresaría esta lectura de registro si no tuviera `bus_space_read_4`?» es muy instructivo. La respuesta en x86 es normalmente «una desreferencia de puntero `volatile` más una barrera de compilador». Ver el contraste es como el valor de la abstracción se vuelve concreto: la abstracción es el mismo código, envuelto en un nombre que aporta portabilidad, trazabilidad y documentación.

En tercer lugar, escribe los cambios a mano y ejecuta cada etapa. El código de acceso al hardware es código en el que la memoria muscular importa. Escribir `sc->regs[MYFIRST_REG_CONTROL]` y `bus_write_4(sc->res, MYFIRST_REG_CONTROL, value)` una docena de veces es la manera en que la diferencia entre el acceso directo y el acceso abstraído se hace visible de un vistazo. El código de acompañamiento en `examples/part-04/ch16-accessing-hardware/` es la versión de referencia, pero la memoria muscular viene de escribir.

### Hoja de ruta del capítulo

Las secciones, en orden, son:

1. **¿Qué es la I/O de hardware?** El modelo mental del acceso a registros, cómo el driver habla con el dispositivo sin tocar sus internos, y qué tipos de recursos interesan a los drivers.
2. **Comprensión de la I/O mapeada en memoria (MMIO).** Cómo los dispositivos aparecen como regiones de memoria, por qué una conversión de puntero directa no es la forma en que un driver los alcanza, y qué significan aquí la alineación y la endianness.
3. **Introducción a `bus_space(9)`.** La abstracción de tag y handle, la forma de las funciones de lectura y escritura, y la diferencia entre la familia `bus_space_*` y la abreviatura `bus_*` sobre un `struct resource *`.
4. **Simulación de hardware para pruebas.** Un bloque de registros asignado con `malloc(9)`, diseñado para parecerse a un dispositivo real, con accessors que imitan la semántica de `bus_space`. Etapa 1 del driver del capítulo 16.
5. **Uso de `bus_space` en un contexto real de driver.** Integración del bloque simulado en `myfirst`, exposición de una pequeña interfaz de registro de lectura y escritura a través del driver, y demostración de cómo una tarea puede cambiar el estado del registro con el tiempo. Aquí comienza la etapa 2.
6. **Seguridad y sincronización con MMIO.** Barreras de memoria, ordenación de accesos, locking alrededor de registros, y por qué los bucles de espera activa son un error. La etapa 3 del driver añade la disciplina de seguridad.
7. **Depuración y trazado del acceso al hardware.** Registro de eventos, DTrace, sondas sysctl, y la pequeña capa de observabilidad que hace visible el acceso a registros sin saturar el driver.
8. **Refactorización y versionado del driver preparado para MMIO.** La división final en `myfirst_hw.h` y `myfirst.c`, la actualización de la documentación y la subida de versión a `0.9-mmio`. Etapa 4 del driver.

Tras las ocho secciones vienen los laboratorios prácticos, los ejercicios de desafío, una referencia para la resolución de problemas, un cierre que concluye los hábitos de la Parte 3 y abre la Parte 4, y un puente hacia el capítulo 17. El material de referencia y hoja de consulta rápida al final del capítulo está pensado para releerlo mientras avanzas por los capítulos posteriores de la Parte 4; el vocabulario del capítulo 16 se reutiliza en todos ellos.

Si es tu primera lectura, lee en orden y haz los laboratorios en secuencia. Si estás revisando el capítulo, las secciones 3 y 6 funcionan de forma independiente y son buenas lecturas para una sola sesión.



## Sección 1: ¿Qué es la I/O de hardware?

El mundo del driver del capítulo 15 es pequeño. Todo lo que le importa, lo asigna por sí mismo. El buffer circular es un `cbuf_t` dentro del softc. El latido es un `struct callout` dentro del softc. El semáforo de escritores es un `struct sema` dentro del softc. Cada parte del estado que el driver toca es memoria que el asignador del kernel le ha proporcionado. Leer o escribir en ese estado es un acceso de memoria C puro: una asignación de campo, una desreferencia de puntero, o una llamada a una función auxiliar que hace una de esas cosas bajo un lock.

El hardware cambia ese mundo. Un dispositivo de hardware no es memoria del kernel. Es un trozo de silicio separado, normalmente en un chip distinto al CPU, con sus propios registros, sus propios buffers, su propio estado interno y sus propias reglas sobre cómo puede comunicarse con él el CPU. El trabajo de un driver es traducir la visión del mundo del kernel, que es software, a la visión del mundo del dispositivo, que es hardware. El primer paso en esa traducción es aprender cómo se comunican entre sí el CPU y el dispositivo.

Esta sección introduce el modelo mental. Las secciones posteriores parten de él. El objetivo de la sección 1 no es que escribas código todavía. El objetivo es establecer el vocabulario y el modelo con suficiente claridad para que todo lo que sigue encaje de forma natural.

### El dispositivo como socio colaborador

Una primera imagen útil es pensar en un dispositivo de hardware no como un objeto que el driver controla, sino como un socio colaborador con el que el driver conversa. El dispositivo realiza una cantidad fija de trabajo de forma autónoma. Un disco gira independientemente de que el driver esté atento o no. Una tarjeta de red recibe paquetes del cable sin pedir permiso. Un sensor de temperatura mide la temperatura de forma continua. Un controlador de teclado escanea la matriz del teclado con su propio temporizador. En todos los casos, el dispositivo tiene un comportamiento interno en el que el driver no puede influir directamente.

Lo que el driver puede hacer es enviar comandos al dispositivo y recibir su estado y sus datos. El dispositivo expone una pequeña interfaz: un conjunto de registros, cada uno con un significado específico, cada uno con un protocolo específico sobre cómo el driver puede leerlo o escribirlo. El driver escribe un valor en un registro de control para indicarle al dispositivo qué debe hacer. El driver lee un valor de un registro de estado para averiguar qué está haciendo el dispositivo. El driver lee un valor de un registro de datos para obtener los datos que el dispositivo ha recibido. El driver escribe un valor en un registro de datos para enviar datos que el dispositivo debe transmitir.

Los registros, en esta imagen, son la conversación. El driver no llama a un método en el dispositivo ni pasa argumentos en el sentido de C. El driver escribe un valor específico en un desplazamiento específico, y el dispositivo lee esa escritura y responde. El dispositivo escribe un valor específico en un desplazamiento específico, y el driver lee esa escritura para ver qué le está diciendo el dispositivo. El protocolo está enteramente definido por la documentación del dispositivo, que suele ser una hoja de datos. El trabajo del driver es seguir el protocolo.

Esa palabra «socio» hace mucho trabajo. El hardware es notoriamente implacable. Un dispositivo no documenta todas las cosas incorrectas que el driver podría hacer; simplemente hace algo incorrecto, o indefinido, si el driver rompe el protocolo. Un driver que escribe el valor incorrecto en un registro de control puede dejar el dispositivo inservible hasta el próximo ciclo de encendido. Un driver que lee un registro de estado antes de que el dispositivo haya terminado un comando anterior puede ver datos obsoletos y tomar una mala decisión. Un driver que no borra un indicador de interrupción antes de volver del manejador de interrupciones puede dejar al dispositivo convencido de que la interrupción sigue pendiente. La metáfora del socio es cooperativa en intención; la relación real es aquella en la que el driver debe ser muy cuidadoso y muy atento, porque el dispositivo no tiene forma de protestar excepto portándose mal.

### Lo que significa realmente «acceder al hardware»

La expresión «acceder al hardware» aparece en todos los textos de programación del kernel, y merece una mirada más atenta. ¿Qué hace realmente el CPU cuando un driver lee un registro de un dispositivo?

En las plataformas modernas, la respuesta más habitual es: el CPU emite un acceso de memoria a una dirección física específica, y el controlador de memoria enruta ese acceso al dispositivo en lugar de a la RAM. Desde el punto de vista del CPU, parece una carga o almacenamiento ordinario. Desde el punto de vista del dispositivo, es un mensaje del CPU dirigido a un registro interno específico. El cableado intermedio, el controlador de memoria y la estructura del bus, son los que hacen que el enrutamiento funcione.

Eso es la I/O mapeada en memoria. El CPU utiliza instrucciones normales de carga y almacenamiento. La dirección resulta enrutada a un dispositivo en lugar de a la RAM. El dispositivo expone sus registros como un rango de direcciones físicas, y el driver lee y escribe dentro de ese rango del mismo modo que leería o escribiría cualquier otra memoria.

Una respuesta más antigua y menos habitual en los CPU x86 es: el CPU emite una instrucción de I/O especial (`in` o `out`) a un número de puerto de I/O específico, y el chipset enruta esa instrucción al dispositivo. El CPU no utiliza una carga ni un almacenamiento; utiliza una instrucción dedicada que opera en un espacio de direcciones separado, denominado espacio de puertos de I/O o I/O mapeada en puertos. Este era el mecanismo original en x86 y aún se usa en algunos dispositivos heredados, pero los drivers modernos rara vez lo encuentran salvo en rutas de compatibilidad.

FreeBSD abstrae ambos mecanismos detrás de una única API. Al driver no le importa, la mayor parte del tiempo, si se llega a un registro mediante un acceso de memoria o mediante una instrucción de I/O. La abstracción es `bus_space(9)`, que la sección 3 presenta. Por ahora, observa que «acceder al hardware» es una operación física en el CPU que el sistema operativo oculta detrás de una interfaz de software. El driver escribe `bus_space_write_4(tag, handle, offset, value)`, y el kernel hace lo correcto según la plataforma y el tipo de recurso.

### Por qué una conversión de puntero directa no es la forma en que los drivers acceden al hardware

Un lector nuevo podría preguntarse razonablemente: si un dispositivo aparece como un rango de direcciones físicas, ¿por qué no tomar simplemente la dirección de los registros del dispositivo, convertirla a `volatile uint32_t *` y desreferenciarla? Técnicamente, en algunas plataformas, eso funciona. En la práctica, ningún driver de FreeBSD hace esto, y varias razones reales hacen que la conversión directa sea una mala elección.

En primer lugar, el driver no conoce la dirección física del dispositivo en tiempo de compilación. La dirección la asigna el código de enumeración del bus en el arranque o durante la conexión en caliente, basándose en los Registros de Dirección Base (BARs) que el dispositivo anuncia. La rutina attach del driver solicita el recurso al subsistema de bus; el subsistema de bus devuelve un handle que contiene el mapeo. El driver utiliza entonces ese handle a través de la API `bus_space`. No existe ningún lugar en el código fuente del driver donde la dirección física sea una constante.

En segundo lugar, las direcciones físicas no son direcciones virtuales. El CPU funciona en modo de dirección virtual; desreferenciar un puntero lee de la memoria virtual, no de la memoria física. La capa `pmap(9)` del kernel mantiene la traducción. Un driver que quiere desreferenciar los registros de un dispositivo necesita un mapeo virtual en el rango físico del dispositivo, con los atributos de caché y acceso correctos. `bus_space_map` hace esto. Una conversión de puntero directa no.

En tercer lugar, las distintas arquitecturas requieren atributos de acceso diferentes para la memoria del dispositivo que para la RAM. La memoria del dispositivo debe marcarse normalmente como no cacheada, o con caché débil, o como «memoria de dispositivo» en las tablas de páginas de la MMU, para que el CPU no reordene ni cachee los accesos de formas que confundan al dispositivo. En arm64, las páginas de memoria del dispositivo utilizan los atributos `nGnRnE` o `nGnRE` que deshabilitan la captación previa especulativa. En x86, los mecanismos `PAT` y `MTRR` marcan las regiones del dispositivo como no cacheadas o de escritura combinada. Una conversión de puntero directa usa los atributos que tenga el mapeo virtual circundante, que suele ser cacheado, lo que habitualmente es incorrecto para los registros del dispositivo.

En cuarto lugar, `bus_space` transporta información adicional más allá de simplemente dónde leer y escribir. El tag codifica qué espacio de direcciones (memoria o puerto de I/O) está en uso. En arquitecturas donde ambos son distintos, el tag selecciona la instrucción de CPU correcta. En arquitecturas donde un driver puede mapear una región con intercambio de bytes por cuestiones de endianness, el tag también codifica eso. La interfaz tag más handle es una forma portable de expresar "accede a este dispositivo con la semántica correcta", mientras que un puntero directo es simplemente "toca esta dirección virtual y espera lo mejor".

En quinto lugar, `bus_space` admite trazado, auditoría de accesos al hardware y comprobaciones de integridad opcionales a través de la capa `bus_san(9)` cuando el kernel se compila con sanitizadores activados. Una conversión mediante puntero directo resulta invisible para esas herramientas. Si alguna vez necesitas saber cuándo tu driver leyó un registro concreto, `bus_space` puede decírtelo; un desreferenciado directo no puede.

En pocas palabras: los drivers de FreeBSD usan `bus_space` porque abstrae un problema real que el driver necesita resolver, y la conversión mediante puntero directo funciona en algunas plataformas por accidente, no por diseño. Aceptar la abstracción tiene un coste mínimo; rechazarla genera errores que afloran semanas después de la puesta en producción.

### Categorías de recursos que interesan a un driver

La mayoría de los drivers trabajan con un pequeño conjunto de categorías de recursos. Cada una tiene un patrón de acceso diferente. El capítulo 16 se centra en una de ellas (los registros con mapeado en memoria), y las demás se tratan en capítulos posteriores. Conocer el catálogo completo te ayuda a situar el tema actual.

**Registros MMIO (I/O con mapeado en memoria).** Un rango de direcciones físicas del dispositivo, mapeadas en el espacio de direcciones virtuales del kernel, que se utiliza para enviar comandos y recibir estado. Todos los dispositivos modernos tienen al menos una región MMIO; la mayoría tiene varias. El foco del capítulo 16.

**Registros PIO (I/O con mapeado de puertos).** Un rango de números de puerto I/O en x86, al que se accede mediante las instrucciones de CPU `in` y `out`. Los dispositivos más antiguos utilizaban esto como su mecanismo principal. Los dispositivos más nuevos a veces exponen una pequeña ventana de compatibilidad a través de puertos (un controlador serie heredado, por ejemplo) mientras colocan la interfaz principal en MMIO. La API `bus_space` abstrae ambas opciones tras las mismas llamadas de lectura y escritura, razón por la que este capítulo las trata de forma conjunta.

**Interrupciones.** Una señal del dispositivo a la CPU que indica que algo ha ocurrido. El driver registra un manejador de interrupción mediante `bus_setup_intr(9)`, y el kernel se encarga de que el manejador se ejecute cuando la línea de interrupción se activa. El capítulo 19 trata las interrupciones.

**Canales DMA.** El dispositivo lee o escribe directamente en la RAM del sistema, sin pasar por la CPU. El driver prepara un descriptor DMA que indica al dispositivo qué direcciones de RAM puede utilizar. La API `bus_dma(9)` de FreeBSD gestiona los mapeos, la coherencia de caché y la sincronización. Los capítulos 20 y 21 tratan DMA.

**Espacio de configuración.** En PCI, un espacio de direcciones separado por dispositivo, que sirve para describir el dispositivo al sistema operativo. Aquí residen los BARs, los identificadores de fabricante y dispositivo, y el estado de gestión de energía. La mayoría de los drivers leen el espacio de configuración solo una vez, durante el attach, para descubrir las capacidades del dispositivo. El capítulo 18 trata el espacio de configuración PCI.

**Capacidades específicas del bus.** MSI, MSI-X, capacidades extendidas de PCIe, eventos de conexión en caliente y soluciones a erratas. Los capítulos dedicados a cada bus las tratan en detalle.

Este capítulo se sitúa en el apartado de MMIO. La abstracción `bus_space` también cubre PIO, por lo que veremos rutas con mapeado de puertos de pasada, pero el ejemplo de trabajo es MMIO en todo momento.

### El registro, de cerca

Un registro, en el lenguaje del dispositivo, es una unidad de comunicación. Tiene un nombre, un desplazamiento, un ancho, un conjunto de campos y un protocolo.

El **nombre** es como el datasheet hace referencia a él. `CONTROL`, `STATUS`, `DATA_IN`, `DATA_OUT`, `INTR_MASK`. Los nombres son para los humanos; el dispositivo no los conoce.

El **desplazamiento** es la distancia desde el inicio del bloque de registros del dispositivo hasta el inicio de este registro concreto. Los desplazamientos se expresan habitualmente en hexadecimal. `CONTROL` en `0x00`. `STATUS` en `0x04`. `DATA_IN` en `0x08`. `DATA_OUT` en `0x0c`. `INTR_MASK` en `0x10`. El driver utiliza desplazamientos en cada lectura y escritura.

El **ancho** es el número de bits que lleva el registro. Los anchos habituales son 8, 16, 32 y 64 bits. A un registro de 32 bits se accede con `bus_space_read_4` y `bus_space_write_4`, donde el `4` es el ancho en bytes. No respetar el ancho es un error sorprendentemente frecuente: leer un registro de 32 bits con un acceso de 8 bits lee solo un byte, y en algunas plataformas con restricciones de byte-lane puede devolver el byte incorrecto o ningún byte en absoluto.

Los **campos** dentro de un registro son subrangos de bits que tienen cada uno un significado concreto. Un registro `CONTROL` de 32 bits podría tener un bit `ENABLE` en el bit 0, un bit `RESET` en el bit 1, un campo `MODE` de 4 bits en los bits 4 a 7, y un campo `THRESHOLD` de 16 bits en los bits 16 a 31, con los bits restantes reservados. El driver utiliza máscaras de bits y desplazamientos para extraer o establecer campos concretos, y el datasheet define cada máscara y desplazamiento.

El **protocolo** son las reglas que el driver debe seguir al leer o escribir el registro. Algunos protocolos son triviales («escribe en este registro el valor que quieras»). Algunos son sutiles («activa el bit `ENABLE`, espera a que el bit `READY` de `STATUS` se ponga a 1 durante un máximo de 100 microsegundos y luego escribe el registro `DATA_IN`»). Algunos tienen efectos secundarios que el driver debe conocer («leer `STATUS` borra los bits de error»). Implementar correctamente el protocolo supone la mayor parte del tiempo de desarrollo de un driver en muchos proyectos de hardware.

En el capítulo 16 los registros son sencillos, porque el dispositivo está simulado. Pero el vocabulario es el de los datasheets reales, y cada término que se introduce aquí se traslada directamente a cualquier dispositivo real que encuentres más adelante.

### Un primer modelo mental: el panel de control

Una analogía útil, siempre que la mantengas con cierta disciplina, es la de un panel de control en una máquina industrial. La máquina realiza su trabajo a su propio ritmo. El panel expone mandos que el operario puede girar para indicar a la máquina qué debe hacer, indicadores que puede leer para ver qué está haciendo la máquina, y algunas luces que se encienden y apagan para señalar eventos. El operario no accede al interior de la máquina; accede a la máquina a través del panel.

El driver es el operario. El bloque de registros es el panel de control. Un mando del panel es un campo de control en un registro. Un indicador es un campo de estado. Una luz es un bit de estado. El cableado que hay detrás del panel es la lógica interna del dispositivo, que el driver no ve y no puede influir directamente. El cable entre el operario y el panel es `bus_space`: transporta los giros del mando del operario y las lecturas de los indicadores de ida y vuelta, en un lenguaje que tanto el operario como el panel comprenden.

La analogía se rompe rápidamente si se lleva demasiado lejos. El hardware real tiene restricciones de temporización que el panel no tiene. El hardware real tiene efectos secundarios que el panel no tiene. El hardware real habla un protocolo que cambia cuando cambia el estado interno de la máquina. Pero para la primera pasada, el panel es suficiente: el driver escribe en un campo de control, el dispositivo reacciona, el driver lee un campo de estado y el dispositivo le dice qué ocurrió.

Las secciones posteriores sustituyen la analogía por modelos mentales más precisos: el bloque de registros como una ventana al estado del dispositivo, la región MMIO como un rango de memoria con efectos secundarios, y la interfaz `bus_space` como un mensajero consciente de la plataforma. Por ahora, el panel es la rampa de entrada.

### Por qué simular hardware es una buena primera práctica

La estrategia didáctica del capítulo 16 es simular un dispositivo en lugar de exigir al lector que disponga de un hardware real concreto. Las razones son prácticas e intencionadas.

Un lector que practica el acceso de estilo registro por primera vez se beneficia enormemente de un entorno en el que puede ver los valores de los registros directamente. Los registros de un dispositivo PCI real están ocultos detrás de un BAR; el lector puede leerlos con `pciconf -r`, pero solo para desplazamientos conocidos y concretos, y los valores cambian en función del estado del dispositivo de formas que un datasheet puede no documentar completamente. Un dispositivo simulado, en cambio, es un struct en memoria del kernel. El lector puede imprimir su contenido con un sysctl. El lector puede modificarlo desde el espacio de usuario mediante un ioctl. El lector puede inspeccionarlo en ddb. La simulación cierra el bucle entre acción y observación, que es lo que hace que la práctica sea eficaz.

Un dispositivo simulado también es seguro. Un dispositivo real que recibe un valor de registro incorrecto puede bloquearse, corromper datos o requerir un ciclo de apagado físico. Un dispositivo simulado que recibe un valor de registro incorrecto no hace nada peor que establecer un bit incorrecto en memoria del kernel; si el driver lo deja escapar, `INVARIANTS` se quejará. Los principiantes se benefician de esta red de seguridad.

Un dispositivo simulado es reproducible. Todos los lectores que ejecuten el código del capítulo 16 verán los mismos valores de registro en el mismo orden. El comportamiento de un dispositivo real depende de la versión del firmware, la revisión del hardware y las condiciones del entorno. Enseñar sobre un objetivo reproducible es mucho más fácil que enseñar sobre el conjunto de todos los posibles objetivos.

El capítulo 17 amplía la simulación con temporizadores, eventos e inyección de fallos. El capítulo 18 presenta un dispositivo PCI real (habitualmente un dispositivo virtio en una VM) para que el lector pueda practicar la ruta con hardware real. El capítulo 16 sienta las bases enseñando el vocabulario con una simulación estática, que es la versión más accesible del material.

### Un vistazo a drivers reales que usan I/O de hardware

Antes de continuar, hagamos un breve recorrido por drivers reales de FreeBSD que ponen en práctica los patrones que enseña el capítulo 16. No necesitas leer estos archivos todavía; son puntos de referencia a los que puedes volver a medida que el capítulo avanza.

`/usr/src/sys/dev/uart/uart_bus_pci.c` es una capa de adaptación PCI para controladores UART (serie). Muestra cómo un driver encuentra su dispositivo PCI, reclama un recurso MMIO y entrega el recurso a una capa inferior que conduce realmente el hardware. Es pequeño y legible, y utiliza `bus_space` solo de forma indirecta.

`/usr/src/sys/dev/uart/uart_dev_ns8250.c` es el driver UART real para el clásico controlador serie de la familia 8250. Es el archivo donde se producen las lecturas y escrituras de registros. La disposición de los registros se define en `uart_bus.h` y `uart_dev_ns8250.h`. Las lecturas utilizan la abstracción que enseña el capítulo.

`/usr/src/sys/dev/ale/if_ale.c` es un driver Ethernet para el chipset Attansic L1E. Su archivo `if_alevar.h` define las macros `CSR_READ_4` y `CSR_WRITE_4` sobre `bus_read_4` y `bus_write_4`, que es un patrón que adoptarás en tu propio driver en la etapa 4 de este capítulo.

`/usr/src/sys/dev/e1000/if_em.c` es el driver para los controladores Ethernet Gigabit de Intel (familia e1000). Es más grande y complejo que `if_ale.c`, pero utiliza el mismo vocabulario de `bus_space`. Su ruta de attach es una buena referencia para ver cómo un driver no trivial asigna recursos MMIO.

`/usr/src/sys/dev/led/led.c` es el driver LED. Es un driver de pseudodispositivo que no se comunica con hardware real en absoluto; expone una pequeña interfaz a través de `/dev/led.NAME` y delega el control real del LED en el driver que lo registró. El dispositivo simulado del capítulo 16 toma prestada la forma de este driver: un módulo pequeño y autocontenido con una interfaz clara y sin dependencia de hardware externo.

Estos archivos reaparecerán a lo largo de la parte 4. El capítulo 16 los utiliza como puntos de recorrido; los capítulos posteriores los diseccionan allí donde sus patrones son el foco del capítulo.

### Qué viene a continuación en este capítulo

La sección 2 pasa del panorama abstracto al mecanismo concreto de I/O con mapeado en memoria. Explica qué es un mapeo, por qué se accede a la memoria del dispositivo con reglas diferentes a las de la memoria ordinaria, y cómo puede pensar el driver sobre alineación, endianness y caché. La sección 3 presenta `bus_space(9)` en sí. La sección 4 construye el dispositivo simulado. La sección 5 lo integra en `myfirst`. La sección 6 añade la disciplina de seguridad. Las secciones 7 y 8 cierran el capítulo con depuración, refactorización y control de versiones.

El ritmo a partir de aquí es más lento que en la sección 1. La sección 1 está pensada para leerse de forma lineal y asimilarse como un todo; las secciones posteriores están pensadas para leerse sección a sección, con pausas para escribir el código y ejecutarlo.

### Cerrando la sección 1

La I/O de hardware es la actividad mediante la cual un driver se comunica con un dispositivo. El driver no puede acceder al interior del dispositivo; solo puede enviar comandos y leer estado a través de una interfaz de registro definida. En las plataformas modernas la interfaz suele estar mapeada en memoria; en x86 también existe una ruta heredada con mapeado de puertos. La abstracción de FreeBSD `bus_space(9)` oculta la diferencia al driver la mayor parte del tiempo. Un registro es una unidad de comunicación con nombre, ubicada por un desplazamiento y con un ancho específico, que tiene campos y un protocolo definidos por el datasheet del dispositivo.

La simulación del capítulo 16 te permite practicar el vocabulario sin hardware real. Los capítulos posteriores de la parte 4 aplican el vocabulario a subsistemas reales. El vocabulario es lo que se transfiere, y el resto de este capítulo existe para darte ese vocabulario con suficiente profundidad para utilizarlo con soltura.

La sección 2 comienza examinando de cerca la I/O con mapeado en memoria.



## Sección 2: Comprendiendo la I/O con mapeado en memoria (MMIO)

La sección 1 presentó la idea de que los registros de un dispositivo pueden alcanzarse mediante accesos a memoria de apariencia ordinaria. Esa idea merece que nos detengamos en ella. La I/O mapeada en memoria es el mecanismo dominante en las plataformas modernas de FreeBSD, y comprenderla bien es lo que hace que cada capítulo posterior de la Parte 4 resulte abordable en lugar de misterioso.

Esta sección responde a tres preguntas estrechamente relacionadas. ¿Cómo aparece un dispositivo en memoria? ¿Por qué debe la CPU acceder a esa memoria con reglas distintas a las de la memoria ordinaria? ¿Qué debe tener en cuenta un driver al leer y escribir un registro?

La sección parte desde los fundamentos: direcciones físicas, mapeos virtuales, atributos de caché, alineación y endianness. Cada pieza es pequeña. La sutileza reside en la composición.

### Direcciones físicas y memoria de dispositivo

El CPU ejecuta instrucciones. Cada instrucción de carga y almacenamiento nombra una dirección virtual, que la unidad de gestión de memoria (MMU) traduce a una dirección física. Las direcciones físicas son lo que ve el controlador de memoria. El trabajo del controlador de memoria es enrutar el acceso al destino correcto.

Para la mayoría de las direcciones físicas, el destino es DRAM. El controlador lee o escribe una posición en la RAM del sistema y devuelve el resultado al CPU. Este es el caso habitual. Cada asignación `malloc(9)` que realiza el driver devuelve memoria del kernel cuya dirección física está respaldada por DRAM.

Sin embargo, algunos rangos de direcciones físicas se enrutan a dispositivos en su lugar. El controlador de memoria se configura en el boot (por el firmware, normalmente por la BIOS o UEFI en x86, o por el device tree en arm y las tablas `acpi` en todo lo demás) para enviar los accesos en ciertos rangos a dispositivos específicos. Un dispositivo PCI podría residir en la dirección física `0xfebf0000` hasta `0xfebfffff`, una región de 64 KiB. Un UART embebido podría residir en `0x10000000` hasta `0x10000fff`, una región de 4 KiB. Independientemente del rango, un acceso dentro de él se enruta al dispositivo en lugar de a la RAM.

Desde el punto de vista del CPU, el acceso es idéntico a un acceso a la RAM. La instrucción es la misma; la dirección simplemente apunta a otro lugar. Desde el punto de vista del dispositivo, el acceso parece un mensaje entrante: una lectura en el desplazamiento X del archivo de registros interno del dispositivo, o una escritura de algún valor en el desplazamiento Y.

La propiedad clave es que la misma instrucción del CPU (una carga o un almacenamiento) se reutiliza para un propósito diferente. De ahí viene el término «memory-mapped» en MMIO: la interfaz del dispositivo se mapea en el espacio de direcciones de memoria del CPU, de modo que las instrucciones de acceso a memoria la alcanzan.

El I/O mapeado por puertos, la alternativa en x86, utiliza instrucciones separadas (`in`, `out` y sus variantes más anchas) que apuntan a un espacio de direcciones diferente. El espacio de puertos tiene su propio rango de direcciones de 16 bits en x86. Los drivers modernos de FreeBSD rara vez acceden directamente al espacio de puertos, porque los dispositivos modernos prefieren MMIO, pero la abstracción es la misma: el driver escribe un valor en una dirección, y esa dirección resulta enrutarse a un dispositivo.

### El mapeo virtual

El CPU no accede directamente a la memoria física. Todos los accesos a memoria pasan por la MMU, que traduce una dirección virtual a una dirección física mediante tablas de páginas. El kernel mantiene esas tablas de páginas en su capa `pmap(9)`. Para que un driver lea los registros del dispositivo, necesita un mapeo virtual hacia el rango físico del dispositivo.

Cuando el subsistema de bus del kernel descubre un dispositivo y la rutina attach del driver solicita un recurso MMIO, la capa de bus hace dos cosas. En primer lugar, encuentra el rango de direcciones físicas que ocupa el dispositivo, que describe el Base Address Register (BAR) del dispositivo o el device tree de la plataforma. En segundo lugar, establece un mapeo virtual desde un rango de direcciones virtuales del kernel recién asignado hacia ese rango físico, con los atributos de caché y acceso apropiados. El resultado es una dirección virtual que, cuando se desreferencia, produce un acceso en la dirección física correspondiente, que el controlador de memoria enruta entonces al dispositivo.

El handle que devuelve `bus_alloc_resource` es (en la mayoría de las plataformas) un envoltorio alrededor de esa dirección virtual del kernel. El driver no suele ver la dirección directamente; pasa el handle del recurso a `bus_space_read_*` y `bus_space_write_*`, que extraen la dirección virtual internamente. Pero el mecanismo subyacente es un mapeo virtual a físico sencillo, establecido una vez en attach y desmontado en detach.

Esto importa por dos razones. En primer lugar, explica por qué `bus_alloc_resource` no es algo que un driver pueda omitir. Sin la asignación del recurso, no hay mapeo virtual; sin un mapeo virtual, cualquier intento de acceder al dispositivo producirá un fallo o accederá a memoria aleatoria. En segundo lugar, explica por qué la dirección virtual no es una constante: el kernel la elige en el momento del attach, y dos arranques del mismo sistema pueden producir direcciones diferentes.

### Los atributos de caché importan

Las páginas de memoria tienen atributos de caché. La RAM ordinaria utiliza caché de tipo «write-back»: el CPU almacena en caché lecturas y escrituras en sus cachés L1, L2 y L3, escribiendo de vuelta a la RAM solo cuando se desaloja la línea de caché o se vacía explícitamente. La caché write-back es excelente para el rendimiento en RAM, donde el trabajo del controlador de memoria es preservar el valor que el CPU escribió más recientemente.

La memoria de dispositivo es diferente. Los registros de un dispositivo suelen tener efectos secundarios en la lectura y en la escritura. Leer un registro `STATUS` puede consumir un evento que el dispositivo ha señalizado. Escribir en un registro `DATA_IN` puede encolar datos para transmisión. Almacenar en caché una lectura de un registro de estado significa que el CPU devuelve un valor obsoleto en la segunda lectura; almacenar en caché una escritura en un registro de datos significa que la escritura va a la caché y nunca llega al dispositivo hasta que la caché eventualmente desaloja la línea.

Por estas razones, las páginas de memoria de dispositivo se marcan con atributos de caché diferentes a los de la memoria ordinaria. En x86, los atributos se controlan mediante el PAT (Page Attribute Table) y el MTRR (Memory Type Range Registers). La memoria de dispositivo se marca típicamente como `UC` (sin caché) o `WC` (write-combining). En arm64, las páginas de memoria de dispositivo utilizan los atributos `Device-nGnRnE` o `Device-nGnRE`, que deshabilitan la caché y la especulación. Los nombres concretos dependen de la arquitectura; el principio es el mismo: el CPU debe tratar la memoria de dispositivo de forma diferente a la RAM.

`bus_space_map` (o la ruta equivalente dentro de `bus_alloc_resource`) sabe cómo solicitar los atributos de caché correctos al establecer el mapeo virtual. Un driver que desreferencia un puntero crudo hacia una región de dispositivo sin pasar por `bus_space` omite este paso y obtiene los atributos que tenga el mapeo circundante, que habitualmente son incorrectos.

Esta es una de las razones más concretas para utilizar la abstracción de FreeBSD: la abstracción codifica un requisito de corrección (acceso sin caché a los dispositivos) que una conversión de puntero crudo no puede garantizar.

### Alineación

Los registros de hardware tienen requisitos de alineación. Un registro de 32 bits debe accederse con una carga o almacenamiento de 32 bits en un desplazamiento que sea múltiplo de 4. Un registro de 64 bits debe accederse con una carga o almacenamiento de 64 bits en un desplazamiento que sea múltiplo de 8. En la mayoría de las arquitecturas, un acceso no alineado a la memoria de dispositivo es o bien más lento (descompuesto en varios accesos más pequeños por el hardware) o directamente ilegal (generando un fallo de alineación).

La regla para los drivers es sencilla: al leer o escribir un registro, usa la función cuyo ancho coincida con el del registro y utiliza el desplazamiento correcto. Si el registro tiene 32 bits de ancho en el desplazamiento `0x10`, el acceso es `bus_space_read_4(tag, handle, 0x10)` o `bus_space_write_4(tag, handle, 0x10, value)`. Si el registro tiene 16 bits de ancho en el desplazamiento `0x08`, es `bus_space_read_2(tag, handle, 0x08)` o `bus_space_write_2(tag, handle, 0x08, value)`. Las variantes de un byte `bus_space_read_1` y `bus_space_write_1` existen para registros de 8 bits.

No hacer coincidir el ancho es un error habitual en las primeras fases del desarrollo y a menudo silencioso en x86, que tiene reglas de alineación muy permisivas. En arm64 el mismo código puede fallar al primer contacto. Los drivers que se desarrollan en x86 y luego se portan a arm64 tropiezan frecuentemente con este problema exacto, por eso la guía de estilo de FreeBSD recomienda hacer coincidir los anchos estrictamente desde el principio.

También existe una regla de alineación de desplazamientos. El desplazamiento debe ser múltiplo del ancho de acceso. Una lectura de 32 bits en el desplazamiento `0x10` es correcta (`0x10` es múltiplo de 4). Una lectura de 32 bits en el desplazamiento `0x11` es incorrecta, aunque el dispositivo nominalmente tenga un registro que comience ahí; el hardware normalmente lo rechazará o devolverá basura. Esta regla es fácil de seguir cuando los desplazamientos provienen de un archivo de cabecera con nombres claros; se convierte en una trampa cuando los desplazamientos se calculan aritméticamente y la aritmética es incorrecta.

### Endianness

La memoria de dispositivo y el orden de bytes nativo del CPU pueden no coincidir. Un dispositivo que se originó en un contexto de PowerPC o de red puede presentar registros de 32 bits en orden big-endian, lo que significa que el byte más significativo del registro se encuentra en la dirección de byte más baja dentro del registro. Un CPU x86 es little-endian, por lo que la dirección de byte más baja contiene el byte menos significativo. Cuando el CPU lee el registro big-endian del dispositivo y lo interpreta con semántica little-endian, los bytes están en el orden incorrecto.

La familia `bus_space` de FreeBSD tiene variantes de flujo (`bus_space_read_stream_*`) y variantes ordinarias (`bus_space_read_*`). En arquitecturas donde el bus tag codifica un intercambio de endianness, las variantes ordinarias intercambian los bytes para producir un valor en el orden del host. Las variantes de flujo no realizan el intercambio; devuelven los bytes en el orden del dispositivo. Un driver que lee un dispositivo cuyos registros tienen una endianness diferente a la del CPU usará las variantes ordinarias la mayor parte del tiempo, confiando en el tag para gestionar el intercambio. Un driver que lee una carga útil de datos (una secuencia de bytes cuya interpretación depende del protocolo, no del diseño de los registros) puede usar las variantes de flujo.

En x86 la distinción a menudo no importa porque el bus tag no codifica un intercambio de endianness por defecto. Las variantes de flujo son alias de las ordinarias en `/usr/src/sys/x86/include/bus.h`:

```c
#define bus_space_read_stream_1(t, h, o)  bus_space_read_1((t), (h), (o))
#define bus_space_read_stream_2(t, h, o)  bus_space_read_2((t), (h), (o))
#define bus_space_read_stream_4(t, h, o)  bus_space_read_4((t), (h), (o))
```

El comentario en ese archivo explica: «Stream accesses are the same as normal accesses on x86; there are no supported bus systems with an endianess different from the host one.» En otras arquitecturas, las dos familias pueden diferir, y un driver que se preocupa por la endianness elige la variante apropiada en función de lo que espera el dispositivo.

Para el Capítulo 16, la simulación está diseñada para ser host-endian. Los drivers del capítulo utilizan los `bus_space_read_*` y `bus_space_write_*` ordinarios sin preocuparse por los intercambios de bytes. Los capítulos posteriores que traten con controladores de red reales volverán a abordar el tema de la endianness.

### Efectos secundarios de lectura y escritura

Una de las propiedades más importantes de la memoria de dispositivo, y una que hace tropezar a los drivers que la tratan como memoria ordinaria, es que las lecturas y escrituras pueden tener efectos secundarios.

Una escritura en un registro de control es, por diseño, un efecto secundario: escribir `1` en el bit `ENABLE` le dice al dispositivo que empiece a funcionar. El driver espera ese efecto secundario, porque para eso sirve el registro. La sutileza es que la escritura tiene un efecto secundario en el dispositivo aunque el valor escrito también se recuerde; un driver que escribe `0x00000001` en `CONTROL` y luego lee `CONTROL` puede ver `0x00000001` (si el registro devuelve el valor escrito) o algún otro valor (si el registro devuelve el estado actual del dispositivo, que puede diferir del último valor escrito).

Una lectura de un registro de estado también puede ser un efecto secundario. Algunos dispositivos implementan semántica de «read-to-clear», en la que leer el registro devuelve el estado actual y, como parte de la lectura, borra bits de error pendientes o indicadores de interrupción. Un driver que lee el estado dos veces seguidas puede ver valores diferentes en las dos lecturas, porque la primera lectura cambió el estado interno del dispositivo. Esto es intencional; el datasheet lo especifica así.

Algunos registros son de **solo escritura**. Leerlos devuelve un valor fijo (a menudo todo ceros) y no revela nada sobre el dispositivo. Escribir en ellos tiene el efecto previsto. Un driver que intente leer un registro de solo escritura para comprobar su valor actual se verá inducido a error.

Algunos registros son de **solo lectura**. Escribir en ellos se ignora o resulta peligroso. Un driver que escribe en un registro de solo lectura puede no hacer nada (si el hardware es defensivo) o puede causar un comportamiento indefinido (si no lo es).

Algunos registros son **inseguros para read-modify-write**. Un patrón de actualización ingenuo (leer el valor actual, modificar un campo, escribir el valor de vuelta) es seguro en un registro donde la lectura devuelve el contenido actual y la escritura lo reemplaza. Es inseguro en un registro donde la lectura tiene efectos secundarios, donde la escritura tiene efectos secundarios en campos no deseados, o donde otro agente (otro CPU, un motor DMA, un manejador de interrupción) puede modificar el registro entre la lectura y la escritura.

En el capítulo 16, el dispositivo simulado tiene un protocolo sencillo: las lecturas y escrituras afectan únicamente al campo específico que modifica el llamador, y ninguna lectura produce efectos secundarios. Esto no es realista; el capítulo 17 introduce los comportamientos de tipo read-to-clear y write-only. Por ahora, la simplicidad es una ventaja: el lector puede concentrarse en la mecánica del acceso sin tener que lidiar también con las particularidades del protocolo del dispositivo.

### Una imagen concreta: el bloque de registros de un dispositivo

Un ejemplo concreto, aunque inventado, ayuda a fijar la idea. Imagina un sencillo controlador de temperatura y ventilador expuesto como una región MMIO de 64 bytes. El mapa de registros podría tener este aspecto:

| Desplazamiento | Ancho | Nombre | Acceso | Descripción |
|--------|--------|-----------------|-----------|-------------------------------------------|
| 0x00   | 32 bits | `CONTROL`       | Lectura/Escritura | Bits de habilitación global, reset y modo. |
| 0x04   | 32 bits | `STATUS`        | Solo lectura | Dispositivo listo, fallo, datos disponibles. |
| 0x08   | 32 bits | `TEMP_SAMPLE`   | Solo lectura | Última lectura de temperatura. |
| 0x0c   | 32 bits | `FAN_PWM`       | Lectura/Escritura | Ciclo de trabajo PWM del ventilador (0-255). |
| 0x10   | 32 bits | `INTR_MASK`     | Lectura/Escritura | Bits de habilitación por interrupción. |
| 0x14   | 32 bits | `INTR_STATUS`   | Lectura/Borrado | Flags de interrupción pendientes (borrado al leer). |
| 0x18   | 32 bits | `DEVICE_ID`     | Solo lectura | Identificador fijo; código de fabricante. |
| 0x1c   | 32 bits | `FIRMWARE_REV`  | Solo lectura | Revisión del firmware del dispositivo. |
| 0x20-0x3f | 32 bytes | reservado  | -         | Debe escribirse como cero; lectura indefinida. |

Un driver para este dispositivo leería `DEVICE_ID` durante el attach para confirmar que el hardware es el que el driver espera, escribiría en `CONTROL` para habilitarlo, haría polling de `STATUS` para confirmar que el dispositivo está listo, leería `TEMP_SAMPLE` periódicamente para informar de la temperatura y escribiría en `FAN_PWM` periódicamente para ajustar el ventilador. La ruta de interrupciones leería `INTR_STATUS` para ver qué eventos están pendientes (lo que también los borra) y escribiría en `INTR_MASK` durante la configuración inicial para elegir qué interrupciones habilitar.

El dispositivo simulado del Capítulo 16 toma prestada en gran medida esta estructura. La simulación tiene un `CONTROL`, un `STATUS`, un `DATA_IN`, un `DATA_OUT`, un `INTR_MASK` y un `INTR_STATUS`. Es deliberadamente un juguete; los campos y el protocolo se han elegido para que el lector pueda manipularlos fácilmente desde el espacio de usuario a través de las rutas `read(2)` y `write(2)` existentes del driver. El mapa de registros se mantiene sencillo porque el Capítulo 17 introducirá la complejidad que los dispositivos reales añaden sobre esta base.

### La forma de un acceso a registro

Reuniendo todas las piezas, un acceso a registro consiste en:

1. El driver dispone de un `bus_space_tag_t` y un `bus_space_handle_t` que juntos describen una región específica del dispositivo con atributos de caché concretos.
2. El driver selecciona un desplazamiento dentro de la región, correspondiente a un registro definido en la hoja de datos del dispositivo.
3. El driver selecciona un ancho de acceso que coincide con el ancho del registro.
4. El driver llama a `bus_space_read_*` o a `bus_space_write_*` con el tag, el handle, el desplazamiento y (en el caso de escrituras) el valor.
5. La implementación de `bus_space` del kernel para la arquitectura actual traduce la llamada a la instrucción CPU apropiada (un `mov` en MMIO x86, un `inb`/`outb` en PIO x86, un `ldr`/`str` en arm64, etc.).
6. El controlador de memoria o la estructura de I/O enruta el acceso al dispositivo.
7. El dispositivo responde: en una lectura, devuelve el valor solicitado; en una escritura, realiza la acción que define el protocolo del registro.

La abstracción oculta todo esto al driver la mayor parte del tiempo. El driver escribe `bus_space_read_4(tag, handle, 0x04)` y obtiene un valor de 32 bits como respuesta. La maquinaria entre la llamada en C y el dispositivo es responsabilidad del kernel y del hardware.

Lo que el driver debe tener presente es el conjunto de reglas de corrección: alineación, ancho, efectos secundarios y orden de los accesos. El capítulo retoma el tema de la ordenación en la Sección 6.

### Lo que MMIO no es

Una breve lista de lo que MMIO no es, para aclarar confusiones habituales.

**MMIO no es DMA.** DMA es cuando el dispositivo lee o escribe en la RAM del sistema por su cuenta. MMIO es cuando la CPU lee o escribe en los registros del dispositivo. Ambos pueden usarse en el mismo driver para propósitos distintos. DMA es más rápido para datos masivos; MMIO es necesario para comandos y estado. Los Capítulos 20 y 21 tratan el DMA.

**MMIO no es memoria compartida.** La memoria compartida (en el sentido POSIX) es RAM accesible para múltiples procesos. MMIO es memoria del dispositivo accesible únicamente para el kernel. El espacio de usuario no puede (ni debe) acceder a MMIO directamente; el driver actúa como intermediario.

**MMIO no es un bloque de RAM con el dispositivo detrás.** MMIO es una interfaz directa con los registros internos del dispositivo. Leer MMIO no devuelve memoria del kernel; devuelve lo que el dispositivo decida retornar en ese desplazamiento. Escribir en MMIO no almacena un valor en la memoria del kernel; envía un mensaje al dispositivo en ese desplazamiento.

**MMIO no es gratuito.** Cada acceso es una transacción en el bus de la CPU. En una jerarquía de caché profunda con alta latencia de memoria, una sola lectura MMIO sin caché puede tardar cientos de ciclos, porque la CPU no puede utilizar la caché y debe esperar a que el dispositivo responda. Los drivers que emiten miles de accesos MMIO por operación suelen estar haciendo algo mal; la mayoría de las operaciones pueden agruparse o eliminarse.

### Resumen de la Sección 2

La E/S mapeada en memoria es el mecanismo por el que una CPU moderna accede a un dispositivo mediante instrucciones ordinarias de carga y almacenamiento, con la dirección enrutada al dispositivo en lugar de a la RAM. La capa de mapeo virtual del kernel y la abstracción `bus_space` ocultan juntas la fontanería, pero el driver debe seguir siendo consciente de la alineación, el orden de bytes, los atributos de caché y los efectos secundarios. Se accede a un registro con una lectura o escritura del ancho correcto en el desplazamiento correcto; el kernel traduce la llamada a la instrucción CPU apropiada para la arquitectura actual.

La Sección 3 presenta `bus_space(9)` en sí: el tag, el handle, las funciones de lectura y escritura, y la forma de la API tal como aparece en cada driver de FreeBSD que se comunica con el hardware. Después de la Sección 3, estarás listo para simular un bloque de registros en la Sección 4 y empezar a escribir código.

## Sección 3: Introducción a `bus_space(9)`

`bus_space(9)` es la abstracción de FreeBSD para el acceso portátil al hardware. Todo driver que se comunica con hardware mapeado en memoria o en puertos lo utiliza, directamente o a través de una pequeña capa envolvente. La abstracción es pequeña: dos tipos opacos, una docena de funciones de lectura y escritura en varios anchos, una función de barrera y algunos auxiliares para accesos a múltiples registros y regiones. La Sección 3 recorre todo ello en el orden en que el lector lo encontraría de forma natural.

La sección comienza con los dos tipos, avanza hacia las funciones de lectura y escritura, cubre los auxiliares para accesos múltiples y de región, presenta la función de barrera y termina con las macros abreviadas `bus_*` definidas sobre un `struct resource *` que la mayoría de los drivers reales utilizan en la práctica. Al final reconocerás cada llamada a `bus_space` que encuentres en `/usr/src/sys/dev/`, y tendrás un modelo mental para escribir las tuyas propias.

### Los dos tipos: `bus_space_tag_t` y `bus_space_handle_t`

Toda llamada a `bus_space` toma un tag y un handle como sus dos primeros argumentos, en ese orden. Entender lo que representa cada uno es el primer paso.

Un **`bus_space_tag_t`** identifica un espacio de direcciones. «Espacio de direcciones» aquí tiene un significado más restringido que su uso general; se refiere específicamente a la combinación de un bus y un método de acceso. En x86 existen dos espacios de direcciones: memoria y puerto de I/O. Cada uno tiene su propio valor de tag. En otras arquitecturas puede haber más: un espacio de memoria con acceso en el orden de bytes del host, un espacio de memoria con acceso en orden de bytes invertido, etc. El tag indica a las funciones `bus_space` qué reglas aplicar.

El tag es específico de la arquitectura. En x86, el tag es un entero: `0` para el espacio de puerto de I/O (`X86_BUS_SPACE_IO`) y `1` para el espacio de memoria (`X86_BUS_SPACE_MEM`). En arm64, el tag es un puntero a una estructura que describe el comportamiento endian y de acceso del bus. En MIPS tiene todavía otra forma. Los drivers normalmente no ven estos detalles de arquitectura; obtienen el tag del subsistema de bus (a través de `rman_get_bustag(resource)` o equivalente) y lo pasan sin inspeccionarlo.

Un **`bus_space_handle_t`** identifica una región específica dentro del espacio de direcciones. Es efectivamente un puntero, pero el significado del puntero depende del tag. Para un tag de memoria en x86, el handle es la dirección virtual del kernel en la que se ha mapeado el rango físico del dispositivo. Para un tag de puerto de I/O en x86, el handle es la dirección base del puerto de I/O. Para tags más elaborados, el handle puede ser una estructura o un valor codificado. Los drivers tratan el handle como opaco y lo pasan sin modificación.

El emparejamiento es importante. Un tag por sí solo no identifica un dispositivo específico; solo identifica el espacio de direcciones. Un handle por sí solo no contiene las reglas de acceso. El par (tag, handle) juntos identifican una región mapeada específica con reglas de acceso concretas, y ese par es sobre el que operan las funciones `bus_space_read_*` y `bus_space_write_*`.

En la práctica, un driver obtiene un `struct resource *` del subsistema de bus durante el attach y extrae el tag y el handle de él con `rman_get_bustag` y `rman_get_bushandle`. Almacena el par en el softc, o almacena el puntero de recurso y utiliza las macros abreviadas `bus_read_*` y `bus_write_*` que extraen el tag y el handle internamente. La Sección 5 recorre el patrón real.

### Desplazamientos

Cada función de lectura y escritura toma un **desplazamiento** dentro de la región. El desplazamiento es un `bus_size_t`, que normalmente es un entero sin signo de 64 bits, medido en bytes desde el inicio de la región. Un registro de 32 bits al inicio de la región MMIO de un dispositivo tiene desplazamiento 0. Un registro de 32 bits en la siguiente posición tiene desplazamiento 4. Un registro de 32 bits en el desplazamiento `0x10` está 16 bytes dentro de la región.

Los desplazamientos se expresan en bytes independientemente del ancho de acceso. `bus_space_read_4(tag, handle, 0x10)` lee un valor de 32 bits comenzando en el desplazamiento de byte `0x10`. `bus_space_read_2(tag, handle, 0x12)` lee un valor de 16 bits comenzando en el desplazamiento de byte `0x12`. El sufijo de la función indica el ancho en bytes, no la granularidad del desplazamiento.

El driver es responsable de asegurarse de que el desplazamiento se encuentra dentro de la región mapeada. `bus_space` no comprueba los límites; un acceso fuera de rango es un error del driver que lee o escribe lo que haya más allá del mapeo del dispositivo, que en la mayoría de las plataformas es memoria no mapeada (lo que provoca un fallo en el kernel) o la memoria de otro dispositivo (corrompiendo el estado de ese dispositivo). Guarda los desplazamientos en archivos de cabecera, obtenlos de la hoja de datos y nunca los calcules aritméticamente sin comprobar los límites del resultado.

### Las funciones de lectura

Las funciones de lectura básicas vienen en cuatro anchos:

```c
u_int8_t  bus_space_read_1(bus_space_tag_t tag, bus_space_handle_t handle,
                           bus_size_t offset);
u_int16_t bus_space_read_2(bus_space_tag_t tag, bus_space_handle_t handle,
                           bus_size_t offset);
u_int32_t bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
                           bus_size_t offset);
uint64_t  bus_space_read_8(bus_space_tag_t tag, bus_space_handle_t handle,
                           bus_size_t offset);
```

Los sufijos `_1`, `_2`, `_4`, `_8` son anchos de acceso en bytes. `_1` es una lectura de 8 bits, `_2` de 16 bits, `_4` de 32 bits y `_8` de 64 bits. El tipo de retorno es el entero sin signo correspondiente.

No todos los anchos son compatibles con todas las plataformas. En x86, `bus_space_read_8` se define únicamente para `__amd64__` (el x86 de 64 bits) y solo para el espacio de memoria, no para el espacio de puerto de I/O. La definición en `/usr/src/sys/x86/include/bus.h` es explícita:

```c
#ifdef __amd64__
static __inline uint64_t
bus_space_read_8(bus_space_tag_t tag, bus_space_handle_t handle,
                 bus_size_t offset)
{
        if (tag == X86_BUS_SPACE_IO)
                return (BUS_SPACE_INVALID_DATA);
        return (*(volatile uint64_t *)(handle + offset));
}
#endif
```

Un acceso a puerto de I/O de 64 bits devuelve `BUS_SPACE_INVALID_DATA` (todos los bits a 1). Un acceso a memoria de 64 bits desreferencia el handle más el desplazamiento como un `volatile uint64_t *`. El calificador `volatile` es lo que impide al compilador almacenar en caché o reordenar el acceso.

El caso de 32 bits es similar:

```c
static __inline u_int32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
                 bus_size_t offset)
{
        if (tag == X86_BUS_SPACE_IO)
                return (inl(handle + offset));
        return (*(volatile u_int32_t *)(handle + offset));
}
```

El espacio de memoria se traduce a una desreferencia `volatile`. El espacio de puerto de I/O se traduce a una instrucción `inl` que lee un long de un puerto de I/O. Los casos de 16 bits (`inw`, `*(volatile u_int16_t *)`) y 8 bits (`inb`, `*(volatile u_int8_t *)`) siguen el mismo patrón. En un x86 de 64 bits, `bus_space_read_4` sobre una región de memoria se traduce a una única instrucción `mov` desde la dirección mapeada. En esta plataforma habitual, el coste de la abstracción en tiempo de ejecución es literalmente el de configurar el marco de llamada para una única instrucción, dado que el inline se expande en las compilaciones de versión final.

### Las funciones de escritura

Las funciones de escritura son el espejo de las de lectura:

```c
void bus_space_write_1(bus_space_tag_t tag, bus_space_handle_t handle,
                       bus_size_t offset, u_int8_t value);
void bus_space_write_2(bus_space_tag_t tag, bus_space_handle_t handle,
                       bus_size_t offset, u_int16_t value);
void bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
                       bus_size_t offset, u_int32_t value);
void bus_space_write_8(bus_space_tag_t tag, bus_space_handle_t handle,
                       bus_size_t offset, uint64_t value);
```

En el espacio de memoria x86, una escritura se traduce a un almacenamiento `volatile`:

```c
static __inline void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t bsh,
                  bus_size_t offset, u_int32_t value)
{
        if (tag == X86_BUS_SPACE_IO)
                outl(bsh + offset, value);
        else
                *(volatile u_int32_t *)(bsh + offset) = value;
}
```

El I/O mapeado por puertos se compila en un `outl`. El driver escribe la misma línea de código fuente independientemente de la plataforma; el `bus.h` específico de cada arquitectura del kernel se encarga del resto.

Al igual que con las lecturas, `bus_space_write_8` hacia el espacio de puertos I/O en x86 no está soportado; la función retorna silenciosamente sin emitir ninguna escritura. Esto refleja el hardware: los puertos I/O de x86 son de 32 bits como máximo.

### Los helpers Multi y Region

A veces un driver necesita leer o escribir muchos valores desde o hacia un único registro, o muchos valores a lo largo de un rango de registros. La API `bus_space` proporciona dos familias de helpers para ello.

Los **accesos multi** acceden repetidamente a un único registro, transfiriendo un buffer de valores a través de él. El registro permanece en un desplazamiento fijo; el buffer se consume o se produce. El caso de uso típico es un registro de tipo FIFO, donde la cola interna del dispositivo se expone a través de una sola dirección y leer o escribir en esa dirección extrae o inserta una entrada.

```c
void bus_space_read_multi_1(bus_space_tag_t tag, bus_space_handle_t handle,
                            bus_size_t offset, u_int8_t *buf, size_t count);
void bus_space_read_multi_2(bus_space_tag_t tag, bus_space_handle_t handle,
                            bus_size_t offset, u_int16_t *buf, size_t count);
void bus_space_read_multi_4(bus_space_tag_t tag, bus_space_handle_t handle,
                            bus_size_t offset, u_int32_t *buf, size_t count);
```

`bus_space_read_multi_4(tag, handle, 0x20, buf, 16)` lee un valor de 32 bits desde el desplazamiento `0x20` dieciséis veces, almacenando cada valor en entradas sucesivas de `buf`. El desplazamiento no cambia entre lecturas; solo avanza el puntero al buffer.

Las variantes de escritura son el reflejo de las lecturas:

```c
void bus_space_write_multi_1(bus_space_tag_t tag, bus_space_handle_t handle,
                             bus_size_t offset, const u_int8_t *buf, size_t count);
void bus_space_write_multi_2(bus_space_tag_t tag, bus_space_handle_t handle,
                             bus_size_t offset, const u_int16_t *buf, size_t count);
void bus_space_write_multi_4(bus_space_tag_t tag, bus_space_handle_t handle,
                             bus_size_t offset, const u_int32_t *buf, size_t count);
```

Los **accesos region** transfieren datos a lo largo de un rango de desplazamientos. El desplazamiento avanza en cada paso; el buffer avanza en cada paso. El caso de uso es una región similar a memoria dentro del dispositivo, como un bloque de datos de configuración o un fragmento de framebuffer.

```c
void bus_space_read_region_1(bus_space_tag_t tag, bus_space_handle_t handle,
                             bus_size_t offset, u_int8_t *buf, size_t count);
void bus_space_read_region_4(bus_space_tag_t tag, bus_space_handle_t handle,
                             bus_size_t offset, u_int32_t *buf, size_t count);
void bus_space_write_region_1(bus_space_tag_t tag, bus_space_handle_t handle,
                              bus_size_t offset, const u_int8_t *buf, size_t count);
void bus_space_write_region_4(bus_space_tag_t tag, bus_space_handle_t handle,
                              bus_size_t offset, const u_int32_t *buf, size_t count);
```

`bus_space_read_region_4(tag, handle, 0x100, buf, 16)` lee 16 valores consecutivos de 32 bits comenzando en el desplazamiento `0x100` y terminando en `0x13c`, almacenándolos en `buf[0]` hasta `buf[15]`.

La distinción entre multi y region responde a dos patrones de hardware diferentes. Un registro FIFO en un único desplazamiento es un multi; un bloque de configuración que ocupa múltiples desplazamientos es un region. Usar la familia incorrecta hace que el driver realice la operación equivocada, aunque el contador del bucle coincida, así que ten cuidado de elegir bien.

El dispositivo simulado del capítulo 16 no utiliza accesos multi ni region; el driver direcciona los registros de uno en uno. La simulación más completa del capítulo 17 y los capítulos posteriores basados en PCI introducen los patrones multi y region donde corresponde.

### La función Barrier

`bus_space_barrier` es la función que la mayoría de los drivers olvida que existe hasta que la necesitan, y su uso correcto es una de las disciplinas silenciosas de la programación de hardware sólida.

```c
void bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
                       bus_size_t offset, bus_size_t length, int flags);
```

La función impone un orden sobre las lecturas y escrituras de `bus_space` emitidas antes de la llamada, en relación con las emitidas después. El argumento `flags` es una máscara de bits:

- `BUS_SPACE_BARRIER_READ` hace que las lecturas anteriores se completen antes de las lecturas posteriores.
- `BUS_SPACE_BARRIER_WRITE` hace que las escrituras anteriores se completen antes de las escrituras posteriores.
- Ambos pueden combinarse con OR para imponer el orden en ambas direcciones.

Los parámetros `offset` y `length` describen la región a la que se aplica la barrera. En x86 se ignoran; la barrera se aplica a toda la CPU. En otras arquitecturas, un puente de bus puede ser capaz de imponer barreras de forma más acotada, y los parámetros son informativos.

En x86 concretamente, `bus_space_barrier` compila a una secuencia pequeña y bien definida. De `/usr/src/sys/x86/include/bus.h`:

```c
static __inline void
bus_space_barrier(bus_space_tag_t tag __unused, bus_space_handle_t bsh __unused,
                  bus_size_t offset __unused, bus_size_t len __unused, int flags)
{
        if (flags & BUS_SPACE_BARRIER_READ)
#ifdef __amd64__
                __asm __volatile("lock; addl $0,0(%%rsp)" : : : "memory");
#else
                __asm __volatile("lock; addl $0,0(%%esp)" : : : "memory");
#endif
        else
                __compiler_membar();
}
```

Una barrera de lectura en amd64 emite un `lock addl` sobre la pila, que es una forma económica de emitir una valla de memoria completa en x86. Una barrera de escritura emite únicamente una barrera de compilador (`__compiler_membar()`), porque el hardware x86 retira las escrituras en orden de programa y el único reordenamiento que un driver puede experimentar en escrituras es el del compilador. La distinción entre "la CPU podría reordenar esto" y "el compilador podría reordenar esto" importa, y el `bus_space_barrier` de x86 la codifica con el mínimo coste.

En arm64, la barrera compila a una instrucción `dsb` o `dmb` según los flags, porque el modelo de memoria de arm64 es más débil y el reordenamiento real por parte de la CPU es posible. El código fuente del driver no cambia; la misma llamada a `bus_space_barrier` elige la instrucción correcta para cada plataforma.

¿Cuándo se requiere una barrera? La regla general es: cuando la corrección de un acceso a registro depende de que otro acceso se haya completado primero. Algunos ejemplos:

- Un driver escribe un comando en `CONTROL` y lee el resultado desde `STATUS`. La lectura no debe especularse antes de la escritura. Un `bus_space_barrier(tag, handle, 0, 0, BUS_SPACE_BARRIER_WRITE | BUS_SPACE_BARRIER_READ)` entre ambas impone el orden.
- Un driver limpia un flag de interrupción en `INTR_STATUS` y espera que el borrado llegue al dispositivo antes de volver a habilitar las interrupciones. Una barrera de escritura después del borrado, antes de la reactivación, es la disciplina correcta.
- Un driver publica un descriptor DMA en memoria y luego escribe en un registro "doorbell" para indicar al dispositivo que lo procese. En plataformas con orden de memoria débil, se requiere una barrera de escritura entre la escritura en memoria y la escritura en el doorbell.

En x86, muchos de estos casos los gestiona el fuerte modelo de orden de la plataforma, y un driver escrito sin barreras explícitas suele funcionar. El mismo driver portado a arm64 puede fallar de forma sutil. La regla "usa barreras cuando el orden importa" produce código portable; la regla "las barreras no hacen nada en x86, así que omítelas" produce código que falla en la mitad de las plataformas soportadas por FreeBSD.

La sección 6 de este capítulo retoma las barreras con ejemplos elaborados en el driver simulado.

### El shorthand `bus_*` sobre un `struct resource *`

La familia `bus_space_*` toma un tag y un handle. En la práctica, los drivers no suelen transportar esos valores por separado; transportan un `struct resource *`, que es lo que devuelve `bus_alloc_resource_any`. La estructura resource contiene el tag y el handle, entre otras cosas. Pasarlos por separado sería ruido innecesario.

Para eliminar ese ruido, `/usr/src/sys/sys/bus.h` define una familia de macros shorthand que toman un `struct resource *` y extraen internamente el tag y el handle:

```c
#define bus_read_1(r, o) \
    bus_space_read_1((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_2(r, o) \
    bus_space_read_2((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_4(r, o) \
    bus_space_read_4((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_write_1(r, o, v) \
    bus_space_write_1((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_4(r, o, v) \
    bus_space_write_4((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_barrier(r, o, l, f) \
    bus_space_barrier((r)->r_bustag, (r)->r_bushandle, (o), (l), (f))
```

Existen equivalentes para las variantes `_multi` y `_region`, variantes stream y la barrera. Las macros cubren la misma funcionalidad que la familia `bus_space_*` subyacente, pero con una sintaxis de llamada más compacta.

La mayoría de los drivers en `/usr/src/sys/dev/` usan el shorthand. Un uso típico tiene este aspecto, adaptado de `if_alevar.h`:

```c
#define CSR_READ_4(sc, reg)       bus_read_4((sc)->res[0], (reg))
#define CSR_WRITE_4(sc, reg, val) bus_write_4((sc)->res[0], (reg), (val))
```

El driver define sus propias macros `CSR_READ_4` y `CSR_WRITE_4` en términos de `bus_read_4` y `bus_write_4`, añadiendo una capa de abstracción adicional. El softc almacena un array de punteros `struct resource *`, y las macros acceden al primero (la región MMIO principal) sin que el driver tenga que escribir la desreferencia del resource cada vez.

Este es un patrón deliberado. Hace que las sentencias de acceso a registros sean cortas y fáciles de leer de un vistazo. Centraliza la referencia al resource en un único lugar, de modo que si el driver mapea más adelante una segunda región, solo hay que modificar las macros. Y otorga al código del driver un aspecto consistente que cualquier persona familiarizada con `/usr/src/sys/dev/` reconocerá de inmediato.

El driver simulado del capítulo 16 adopta este patrón en la Etapa 4. Las etapas anteriores usan la familia `bus_space_*` directamente, para mantener visible el mecanismo; la refactorización final envuelve los accesos en macros `CSR_READ_*` y `CSR_WRITE_*` tal como haría un driver de producción.

### Configuración y limpieza

Un driver que usa `bus_space` no llama directamente a `bus_space_map` en la mayoría de los casos. En su lugar, solicita un resource al subsistema de bus a través de `bus_alloc_resource_any`:

```c
int rid = 0;
struct resource *res;

res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
if (res == NULL) {
        device_printf(dev, "cannot allocate MMIO resource\n");
        return (ENXIO);
}
```

Los argumentos son:

- `dev` es el `device_t` del dispositivo del driver.
- `SYS_RES_MEMORY` selecciona un resource mapeado en memoria. `SYS_RES_IOPORT` selecciona un resource mapeado por puertos. `SYS_RES_IRQ` selecciona una IRQ (se usa en el capítulo 19).
- `rid` es el "identificador de resource", el índice del resource dentro de los resources del dispositivo. El primer BAR de un dispositivo PCI suele tener rid 0 (que en un dispositivo PCI legacy corresponde al BAR en el desplazamiento de configuración PCI `0x10`). `rid` es un puntero porque el subsistema de bus puede actualizarlo para reflejar el rid real que utilizó, aunque en las asignaciones `_any` con un rid conocido, el valor pasado suele devolverse sin cambios.
- `RF_ACTIVE` indica al bus que active el resource inmediatamente, lo que incluye establecer el mapeo virtual. Sin `RF_ACTIVE`, el resource queda reservado pero no mapeado.

Si tiene éxito, `res` es un `struct resource *` válido cuyo tag y handle pueden extraerse con `rman_get_bustag(res)` y `rman_get_bushandle(res)`, o cuyo tag y handle son utilizados implícitamente por las macros shorthand `bus_read_*` y `bus_write_*`.

En el detach, el driver libera el resource:

```c
bus_release_resource(dev, SYS_RES_MEMORY, rid, res);
```

La liberación deshace la asignación, incluyendo el desmontaje del mapeo virtual y la marcación del rango como disponible para reutilización.

Este es el código repetitivo que sigue todo driver para los resources MMIO. El dispositivo simulado del capítulo 16 lo omite por completo, porque no hay ningún bus del que asignar; el "resource" es un trozo de memoria del kernel que el driver asignó con `malloc(9)`. El capítulo 17 introduce una simulación algo más sofisticada que imita el camino de asignación. El capítulo 18, cuando aparece PCI real, usa el flujo completo de `bus_alloc_resource_any`.

### Un primer ejemplo independiente

Incluso sin hardware real, un programa independiente sencillo ilustra la forma de una llamada a `bus_space`. Imagina un driver que quiere leer el registro `DEVICE_ID` de 32 bits en el desplazamiento `0x18` de un dispositivo cuya región MMIO ha sido asignada como `res`:

```c
uint32_t devid = bus_read_4(sc->res, 0x18);
```

Una sola línea. `sc->res` contiene el `struct resource *`. El desplazamiento `0x18` proviene de la hoja de datos. El valor de retorno es el contenido de 32 bits del registro.

Para escribir un valor de control:

```c
bus_write_4(sc->res, 0x00, 0x00000001); /* set ENABLE bit */
```

Para imponer el orden entre la escritura y una lectura posterior:

```c
bus_write_4(sc->res, 0x00, 0x00000001);
bus_barrier(sc->res, 0, 0, BUS_SPACE_BARRIER_WRITE | BUS_SPACE_BARRIER_READ);
uint32_t status = bus_read_4(sc->res, 0x04);
```

La barrera garantiza que la escritura llega al dispositivo antes de que se emita la lectura. En x86 la barrera es económica; en arm64 emite una instrucción de valla. El driver no sabe ni le importa cuál de las dos; la abstracción lo gestiona.

Estas son formas de tres líneas que aparecerán, con pequeñas variaciones, en cada driver que escribas en la Parte 4 y más adelante. Los patrones tienen el mismo aspecto tanto si el objetivo es una tarjeta de red real, un controlador USB, un adaptador de almacenamiento o un dispositivo simulado.

### Un vistazo al uso de `bus_space` en un driver real

Para conectar el vocabulario con código real, abre `/usr/src/sys/dev/ale/if_alevar.h` y desplázate hasta el bloque de macros `CSR_WRITE_*` / `CSR_READ_*`. Encontrarás:

```c
#define CSR_WRITE_4(_sc, reg, val)    \
        bus_write_4((_sc)->ale_res[0], (reg), (val))
#define CSR_WRITE_2(_sc, reg, val)    \
        bus_write_2((_sc)->ale_res[0], (reg), (val))
#define CSR_WRITE_1(_sc, reg, val)    \
        bus_write_1((_sc)->ale_res[0], (reg), (val))
#define CSR_READ_2(_sc, reg)          \
        bus_read_2((_sc)->ale_res[0], (reg))
#define CSR_READ_4(_sc, reg)          \
        bus_read_4((_sc)->ale_res[0], (reg))
```

El softc almacena un array `ale_res[]` de resources; las macros acceden al primer elemento. En todo el resto del driver, un acceso a registro se lee como `CSR_READ_4(sc, ALE_SOME_REG)` y resulta natural.

O abre `/usr/src/sys/dev/e1000/if_em.c` y busca `bus_alloc_resource_any`. Encontrarás:

```c
sc->memory = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
```

El resource va al campo `memory` del softc; el resto del driver usa macros sobre `sc->memory`. El patrón se repite en cada driver que encontrarás en la Parte 4.

El capítulo 16 construye este patrón gradualmente. La Etapa 1 usa acceso directo a struct para enfatizar la mecánica. La Etapa 2 introduce `bus_space_*` directamente contra un handle simulado. La Etapa 3 añade barreras y locking. La Etapa 4 envuelve todo en macros `CSR_*` sobre un puntero compatible con `struct resource *`, emulando el idioma de los drivers reales.

> **Una nota sobre los números de línea.** El capítulo cita el código fuente de FreeBSD por nombre de función, macro o estructura en lugar de por número de línea, porque los números de línea cambian entre versiones mientras que los nombres de símbolo perduran. Para orientación aproximada en FreeBSD 14.3, solo como referencia: las macros `CSR_WRITE_*` en `if_alevar.h` están cerca de la línea 228, `em_allocate_pci_resources` en `if_em.c` cerca de la línea 2415, `ale_attach` en `if_ale.c` cerca de la línea 451, y el bloque de asignación de resources y lectura de registros de `ale_attach` abarca aproximadamente las líneas 463 a 580. Abre el archivo y salta al símbolo; la línea es la que te indique tu editor.

### Cerrando la sección 3

`bus_space(9)` es una abstracción pequeña y enfocada sobre el acceso al hardware. Un tag identifica un espacio de direcciones; un handle identifica una región específica dentro de él. Las funciones de lectura y escritura están disponibles en anchos de 8, 16, 32 y 64 bits. Los accesos multi se repiten sobre un único desplazamiento; los accesos region recorren desplazamientos sucesivos. Las barreras imponen el orden cuando importa. El shorthand `bus_*` sobre un `struct resource *` es lo que la mayoría de los drivers usa en el día a día.

El mecanismo subyacente se compila en instrucciones de CPU que corresponden a la plataforma: un `mov` en x86 MMIO, un `in` o `out` en x86 PIO, un `ldr` o `str` en arm64. El driver escribe código portable; el compilador se encarga de la traducción.

La sección 4 te lleva ahora del vocabulario a la práctica. Construimos un bloque de registros simulado en memoria del kernel, lo envolvemos con funciones auxiliares de acceso y comenzamos la Fase 1 de la refactorización del driver del capítulo 16.

## Sección 4: Simulación de hardware para pruebas

El hardware real es un maestro exigente. Es caro de adquirir, frágil ante el mal manejo, inconsistente entre revisiones y poco amable con los principiantes. Para los propósitos del Capítulo 16 queremos algo diferente: un entorno en el que el lector pueda practicar el acceso al estilo de registro, ver los resultados, romper cosas sin riesgo y observar lo que ocurre. La respuesta es simular un dispositivo en la memoria del kernel.

Esta sección construye esa simulación desde cero. Primero un modelo mental (¿qué significa "simular un dispositivo"?), luego un mapa de registros para el dispositivo que vamos a imitar, después la asignación, los accesores y la primera integración con el driver `myfirst`. Al final de la Sección 4, el driver tiene la Etapa 1: un softc que lleva un bloque de registros, accesores que leen y escriben en él, y un par de sysctls que te permiten manipular la simulación desde el espacio de usuario.

### Qué significa "simular hardware" en este contexto

La simulación es deliberadamente mínima en la Sección 4: un bloque de memoria del kernel, asignado una sola vez, con un tamaño igual al del bloque de registros, y al que se accede mediante funciones que se parecen a llamadas de `bus_space`. Las lecturas obtienen valores del bloque; las escrituras almacenan valores en él. Todavía no hay comportamiento dinámico: ningún temporizador modifica un registro de estado, ningún evento activa el bit de listo, no hay inyección de fallos. El Capítulo 17 añade todo eso. La Sección 4 te proporciona el esqueleto.

Esta limitación es deliberada. El trabajo del Capítulo 16 es enseñar el mecanismo de acceso. Una simulación más rica, en la que el lector tuviese que razonar tanto sobre el mecanismo como sobre el comportamiento del dispositivo, competiría por la atención con el vocabulario que el lector aún está aprendiendo. La simulación de la Sección 4 existe para que cada lectura y escritura de registro devuelva un resultado predecible, lo que permite al lector centrarse en la corrección del acceso en lugar de en si el dispositivo aceptó o no ese acceso.

Un punto pequeño pero importante sobre la simulación: como el "dispositivo" es memoria del kernel, el lector puede inspeccionarla, manipularla y volcarla mediante mecanismos que el kernel ya proporciona (`sysctl`, `ddb`, `gdb` sobre un volcado de memoria). Esta transparencia es una característica pedagógica. Los registros de un dispositivo real solo son visibles a través de la interfaz de registro; los registros simulados son visibles a través de la interfaz *y* a través del asignador. Cuando algo sale mal en el driver, el lector puede comparar "lo que el driver cree que contiene el registro" con "lo que el registro contiene realmente". Esa vía de depuración es muy instructiva y se perderá cuando finalmente dirijamos el driver hacia hardware real.

### El mapa de registros del dispositivo simulado

Antes de asignar nada, decide qué aspecto tiene el dispositivo. Elegir un mapa de registros de antemano es exactamente lo que hace un datasheet para el hardware real, y hacerlo antes de escribir código es un hábito que merece la pena adquirir.

El dispositivo simulado del Capítulo 16 es un "widget" mínimo: puede aceptar un comando, informar de un estado, recibir un solo byte de datos y enviar un solo byte de vuelta. El mapa de registros es:

| Desplazamiento | Ancho  | Nombre          | Dirección         | Descripción                                                                 |
|----------------|--------|-----------------|-------------------|-----------------------------------------------------------------------------|
| 0x00           | 32 bit | `CTRL`          | Lectura/Escritura | Control: bits de habilitación, reset y modo.                                |
| 0x04           | 32 bit | `STATUS`        | Solo lectura      | Estado: listo, ocupado, error, datos disponibles.                           |
| 0x08           | 32 bit | `DATA_IN`       | Solo escritura    | Datos escritos en el dispositivo para su procesamiento.                     |
| 0x0c           | 32 bit | `DATA_OUT`      | Solo lectura      | Datos producidos por el dispositivo.                                        |
| 0x10           | 32 bit | `INTR_MASK`     | Lectura/Escritura | Máscara de habilitación de interrupciones.                                  |
| 0x14           | 32 bit | `INTR_STATUS`   | Lectura/Borrado   | Indicadores de interrupción pendientes (lectura para borrar, Capítulo 17).  |
| 0x18           | 32 bit | `DEVICE_ID`     | Solo lectura      | Identificador fijo: 0x4D594649 ('MYFI').                                   |
| 0x1c           | 32 bit | `FIRMWARE_REV`  | Solo lectura      | Revisión de firmware: codificada como major<<16 \| minor.                   |
| 0x20           | 32 bit | `SCRATCH_A`     | Lectura/Escritura | Registro de borrador libre. Siempre devuelve lo que se escribe.             |
| 0x24           | 32 bit | `SCRATCH_B`     | Lectura/Escritura | Registro de borrador libre. Siempre devuelve lo que se escribe.             |

El tamaño total es de 40 bytes de espacio de registros, que redondeamos a 64 bytes para tener margen de crecimiento en el Capítulo 17.

En el Capítulo 16, todo el acceso a registros se simplifica a lectura y escritura directa en la memoria del kernel. La semántica de lectura para borrar en `INTR_STATUS`, la semántica de solo escritura en `DATA_IN` y el comportamiento de `CTRL` al resetear quedan pospuestos al Capítulo 17. Por ahora, `DATA_IN` devuelve lo que el driver escribió; `INTR_STATUS` conserva el último valor que el driver estableció; y el bloque completo se comporta como un bloque simple de ranuras de 32 bits.

Esto es deliberado. El Capítulo 16 enseña el acceso a registros. El Capítulo 17 introduce la capa de protocolo. Separar ambos mantiene el foco en cada capítulo.

### La cabecera de desplazamientos de registro

Un driver real separa los desplazamientos de registro en una cabecera para que el mapeado del datasheet quede en un único lugar. El driver del Capítulo 16 sigue la misma disciplina. Crea un archivo `myfirst_hw.h` junto a `myfirst.c`:

```c
/* myfirst_hw.h -- Chapter 16 simulated register definitions. */
#ifndef _MYFIRST_HW_H_
#define _MYFIRST_HW_H_

/* Register offsets for the simulated myfirst widget. */
#define MYFIRST_REG_CTRL         0x00
#define MYFIRST_REG_STATUS       0x04
#define MYFIRST_REG_DATA_IN      0x08
#define MYFIRST_REG_DATA_OUT     0x0c
#define MYFIRST_REG_INTR_MASK    0x10
#define MYFIRST_REG_INTR_STATUS  0x14
#define MYFIRST_REG_DEVICE_ID    0x18
#define MYFIRST_REG_FIRMWARE_REV 0x1c
#define MYFIRST_REG_SCRATCH_A    0x20
#define MYFIRST_REG_SCRATCH_B    0x24

/* Total size of the register block. */
#define MYFIRST_REG_SIZE         0x40

/* CTRL register bits. */
#define MYFIRST_CTRL_ENABLE      0x00000001u   /* bit 0: device enabled      */
#define MYFIRST_CTRL_RESET       0x00000002u   /* bit 1: reset (write 1 to)  */
#define MYFIRST_CTRL_MODE_MASK   0x000000f0u   /* bits 4..7: operating mode  */
#define MYFIRST_CTRL_MODE_SHIFT  4
#define MYFIRST_CTRL_LOOPBACK    0x00000100u   /* bit 8: loopback DATA_IN -> OUT */

/* STATUS register bits. */
#define MYFIRST_STATUS_READY     0x00000001u   /* bit 0: device ready        */
#define MYFIRST_STATUS_BUSY      0x00000002u   /* bit 1: device busy         */
#define MYFIRST_STATUS_ERROR     0x00000004u   /* bit 2: error latch         */
#define MYFIRST_STATUS_DATA_AV   0x00000008u   /* bit 3: DATA_OUT has data   */

/* INTR_MASK and INTR_STATUS bits. */
#define MYFIRST_INTR_DATA_AV     0x00000001u   /* bit 0: data available      */
#define MYFIRST_INTR_ERROR       0x00000002u   /* bit 1: error condition     */
#define MYFIRST_INTR_COMPLETE    0x00000004u   /* bit 2: operation complete  */

/* Fixed identifier values. */
#define MYFIRST_DEVICE_ID_VALUE  0x4D594649u   /* 'MYFI' in little-endian    */
#define MYFIRST_FW_REV_MAJOR     1
#define MYFIRST_FW_REV_MINOR     0
#define MYFIRST_FW_REV_VALUE \
        ((MYFIRST_FW_REV_MAJOR << 16) | MYFIRST_FW_REV_MINOR)

#endif /* _MYFIRST_HW_H_ */
```

Cada desplazamiento es una constante con nombre. Cada máscara de bits tiene un nombre. Cada valor fijo tiene una constante. Los capítulos posteriores añaden más registros y más bits; la cabecera crece de forma incremental. La disciplina de "sin números mágicos dentro del código del driver" comienza aquí y da sus frutos a lo largo de la Parte 4.

Una nota sobre el sufijo `u` en las constantes numéricas. El sufijo `u` convierte cada constante en un `unsigned int`, lo cual es importante cuando el valor tiene el bit más significativo activado (los registros de 32 bits usan el bit `0x80000000` completo, que una constante `int` simple no puede representar de forma portable). Usar `u` en todas partes mantiene el driver coherente; adquirir el hábito previene la clase de error en la que un desajuste entre tipos con signo y sin signo conduce a una comparación con extensión de signo que pasa o falla silenciosamente.

### Asignación del bloque de registros

Con los desplazamientos definidos, el driver necesita un bloque de registros. Para la simulación, el bloque es memoria del kernel. Añade lo siguiente al softc (en `myfirst.c`, donde se declara el softc):

```c
struct myfirst_softc {
        /* ... all existing Chapter 15 fields ... */

        /* Chapter 16: simulated MMIO register block. */
        uint8_t         *regs_buf;      /* malloc'd register storage */
        size_t           regs_size;     /* size of the register region */
};
```

`regs_buf` es un puntero a byte hacia una asignación. Usar `uint8_t *` en lugar de `uint32_t *` simplifica la aritmética de desplazamiento por byte en los accesores; convertimos al ancho apropiado en cada acceso.

Antes de la asignación en sí, una pequeña pero útil mejora. El driver del Capítulo 15 usa `M_DEVBUF`, el cubo genérico de memoria de driver del kernel, para sus asignaciones. Funciona, pero mezcla la huella de nuestro driver con la de todos los demás drivers del sistema: `vmstat -m` informa del uso agregado bajo `devbuf`, sin forma de distinguir qué proviene de `myfirst`. El Capítulo 16 es un buen momento para introducir un tipo malloc específico del driver. Cerca de la parte superior de `myfirst.c`, junto a las demás declaraciones de ámbito de archivo:

```c
static MALLOC_DEFINE(M_MYFIRST, "myfirst", "myfirst driver allocations");
```

`MALLOC_DEFINE` registra un nuevo cubo malloc llamado `myfirst`, con la descripción larga que usa `vmstat -m`. Toda asignación que el driver realice a partir de este capítulo lleva la etiqueta `M_MYFIRST`, de modo que `vmstat -m` puede informar directamente del uso total de memoria del driver. Las asignaciones del Capítulo 15 que antes usaban `M_DEVBUF` pueden migrarse a `M_MYFIRST` en el mismo paso, o dejarse como están; la diferencia práctica es pequeña y la migración es puramente cosmética.

Con el tipo definido, la asignación ocurre en `myfirst_attach`, antes de cualquier código que pueda acceder a los registros:

```c
/* In myfirst_attach, after softc initialisation, before registering /dev nodes. */
sc->regs_size = MYFIRST_REG_SIZE;
sc->regs_buf = malloc(sc->regs_size, M_MYFIRST, M_WAITOK | M_ZERO);

/* Initialise fixed registers to their documented values. */
*(uint32_t *)(sc->regs_buf + MYFIRST_REG_DEVICE_ID)   = MYFIRST_DEVICE_ID_VALUE;
*(uint32_t *)(sc->regs_buf + MYFIRST_REG_FIRMWARE_REV) = MYFIRST_FW_REV_VALUE;
*(uint32_t *)(sc->regs_buf + MYFIRST_REG_STATUS)       = MYFIRST_STATUS_READY;
```

`M_WAITOK | M_ZERO` produce una asignación rellena de ceros que puede suspenderse para completarse si la memoria es escasa, lo cual es adecuado en el momento del attach. `M_WAITOK` es la elección correcta porque el llamador es la ruta de attach del kernel, que es un contexto de proceso y puede bloquearse; `M_NOWAIT` solo sería necesario desde un callout o un contexto de interrupción de filtro.

La inicialización escribe tres valores fijos: el ID del dispositivo, la revisión de firmware y un `STATUS` inicial con el bit `READY` activado. Un dispositivo real establecería estos valores mediante la lógica de hardware en el encendido; la simulación lo hace explícitamente en el código.

El desmontaje es simétrico, en `myfirst_detach`:

```c
/* In myfirst_detach, after all consumers of regs_buf have quiesced. */
if (sc->regs_buf != NULL) {
        free(sc->regs_buf, M_MYFIRST);
        sc->regs_buf = NULL;
        sc->regs_size = 0;
}
```

Como siempre en la tradición de los Capítulos 11 a 15, la liberación ocurre después de que hayan finalizado todas las rutas de código que pudieran tocar la memoria. Cuando llegamos a este punto en detach, los callouts están drenados, el taskqueue está drenado, el cdev está destruido y ningún syscall puede alcanzar al driver.

Un punto sutil pero importante: la asignación usa `malloc(9)` en lugar de `contigmalloc(9)` o `bus_dmamem_alloc(9)`. Para la simulación, cualquier memoria del kernel es válida. Para hardware real con requisitos de DMA, la asignación necesitaría ser físicamente contigua, alineada en páginas y con bounce-buffer según corresponda; ese es el tema del Capítulo 20, no el nuestro.

### Los primeros accesores auxiliares

El acceso directo a la estructura mediante conversiones de tipo en bruto (`*(uint32_t *)(sc->regs_buf + MYFIRST_REG_CTRL)`) funciona, pero es feo, inseguro (sin comprobación de límites) e incoherente con el estilo de `bus_space` que enseña el capítulo. Sustitúyelo por accesores con nombre.

En `myfirst_hw.h`, añade prototipos de función y definiciones inline:

```c
/* Simulated accessor helpers. Stage 1: direct memory, no barriers. */

static __inline uint32_t
myfirst_reg_read(uint8_t *regs_buf, size_t regs_size, bus_size_t offset)
{
        KASSERT(offset + 4 <= regs_size,
            ("myfirst: register read past end of register block: "
             "offset=%#x size=%zu", (unsigned)offset, regs_size));
        return (*(volatile uint32_t *)(regs_buf + offset));
}

static __inline void
myfirst_reg_write(uint8_t *regs_buf, size_t regs_size, bus_size_t offset,
    uint32_t value)
{
        KASSERT(offset + 4 <= regs_size,
            ("myfirst: register write past end of register block: "
             "offset=%#x size=%zu", (unsigned)offset, regs_size));
        *(volatile uint32_t *)(regs_buf + offset) = value;
}
```

Dos helpers: uno de lectura y otro de escritura. Cada uno comprueba los límites del desplazamiento con `KASSERT` para que un acceso fuera de rango se detecte inmediatamente en un kernel de depuración. Cada uno usa `volatile` para evitar que el compilador almacene en caché o reordene el acceso. `bus_size_t` es el mismo tipo que usa `bus_space` para los desplazamientos; usarlo mantiene los accesores compatibles con la transición posterior.

Un driver que quiera leer `STATUS` de su softc ahora escribe:

```c
uint32_t status = myfirst_reg_read(sc->regs_buf, sc->regs_size, MYFIRST_REG_STATUS);
```

Dos argumentos de código repetitivo por llamada parece mucho. Los drivers reales envuelven sus accesores en macros más cortas que toman el softc directamente. Hagamos lo mismo:

```c
#define MYFIRST_REG_READ(sc, offset) \
        myfirst_reg_read((sc)->regs_buf, (sc)->regs_size, (offset))
#define MYFIRST_REG_WRITE(sc, offset, value) \
        myfirst_reg_write((sc)->regs_buf, (sc)->regs_size, (offset), (value))
```

Ahora el acceso al registro queda así:

```c
uint32_t status = MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS);
```

Corto, con nombre, legible de un vistazo. Las macros no añaden coste más allá de la expansión inline que el compilador habría hecho de todas formas.

Un helper más que merece la pena introducir para la Etapa 1. Una operación habitual es "leer un registro, modificar un campo, escribirlo de vuelta":

```c
static __inline void
myfirst_reg_update(struct myfirst_softc *sc, bus_size_t offset,
    uint32_t clear_mask, uint32_t set_mask)
{
        uint32_t value;

        value = MYFIRST_REG_READ(sc, offset);
        value &= ~clear_mask;
        value |= set_mask;
        MYFIRST_REG_WRITE(sc, offset, value);
}
```

El helper lee el registro, borra los bits indicados en `clear_mask`, activa los bits indicados en `set_mask` y escribe el resultado de vuelta. Un uso típico:

```c
/* Clear the ENABLE bit in CTRL. */
myfirst_reg_update(sc, MYFIRST_REG_CTRL, MYFIRST_CTRL_ENABLE, 0);

/* Set the ENABLE bit in CTRL. */
myfirst_reg_update(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_ENABLE);

/* Change MODE to 0x3, keeping other bits intact. */
myfirst_reg_update(sc, MYFIRST_REG_CTRL, MYFIRST_CTRL_MODE_MASK,
    3 << MYFIRST_CTRL_MODE_SHIFT);
```

Una advertencia: `myfirst_reg_update` tal como está escrito no es atómico. Entre la lectura y la escritura, otro contexto podría leer el mismo registro, modificarlo y escribirlo de vuelta; nuestra escritura sobrescribiría entonces la actualización del otro contexto. Para la Etapa 1 esto es aceptable, porque los registros simulados solo se acceden desde el contexto de syscall y aún no se comparten con interrupciones ni con tareas. La Sección 6 retoma el tema de la atomicidad e introduce locking alrededor de la actualización.

### Exposición de los registros mediante sysctls

Para hacer observable el bloque de registros de la Etapa 1 sin necesidad de escribir una herramienta en espacio de usuario, expón cada registro como un sysctl de solo lectura. En `myfirst_attach`, junto a las demás definiciones de sysctl:

```c
/* Chapter 16, Stage 1: sysctls that read the simulated registers. */
SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "reg_ctrl",
    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, MYFIRST_REG_CTRL,
    myfirst_sysctl_reg, "IU", "Control register (read-only view)");

SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "reg_status",
    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, MYFIRST_REG_STATUS,
    myfirst_sysctl_reg, "IU", "Status register (read-only view)");

SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "reg_device_id",
    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, MYFIRST_REG_DEVICE_ID,
    myfirst_sysctl_reg, "IU", "Device ID register (read-only view)");
```

(Las entradas equivalentes para cada registro de interés siguen el mismo patrón. El código fuente en examples/part-04 tiene la lista completa.)

El manejador de sysctl traduce el par arg1/arg2 en una lectura de registro:

```c
static int
myfirst_sysctl_reg(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        bus_size_t offset = arg2;
        uint32_t value;

        if (sc->regs_buf == NULL)
                return (ENODEV);
        value = MYFIRST_REG_READ(sc, offset);
        return (sysctl_handle_int(oidp, &value, 0, req));
}
```

Con estos sysctls instalados, el lector puede escribir:

```text
# sysctl dev.myfirst.0.reg_ctrl
dev.myfirst.0.reg_ctrl: 0
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 1
# sysctl dev.myfirst.0.reg_device_id
dev.myfirst.0.reg_device_id: 1298498121
```

`1298498121` en decimal es `0x4D594649`, el ID de dispositivo fijo. `1` en `reg_status` es el bit `READY`. Estos son los valores que estableció la ruta de attach; el lector puede verlos desde el espacio de usuario. El bucle que va desde "el driver escribe un registro" hasta "el lector observa el valor del registro" queda cerrado.

### Un sysctl de escritura para `CTRL` y `DATA_IN`

La lectura es solo la mitad de la historia. La simulación de la Etapa 1 también se beneficia de un sysctl de escritura que permite al lector manipular los valores de los registros:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "reg_ctrl_set",
    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, MYFIRST_REG_CTRL,
    myfirst_sysctl_reg_write, "IU",
    "Control register (writeable, Stage 1 test aid)");
```

Con el manejador de escritura:

```c
static int
myfirst_sysctl_reg_write(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        bus_size_t offset = arg2;
        uint32_t value;
        int error;

        if (sc->regs_buf == NULL)
                return (ENODEV);
        value = MYFIRST_REG_READ(sc, offset);
        error = sysctl_handle_int(oidp, &value, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);
        MYFIRST_REG_WRITE(sc, offset, value);
        return (0);
}
```

El handler lee el valor actual, acepta un nuevo valor del caller y lo escribe de vuelta. Por ahora las escrituras son sin restricciones; secciones posteriores añaden validación y efectos secundarios.

Ahora puedes experimentar:

```text
# sysctl dev.myfirst.0.reg_ctrl_set
dev.myfirst.0.reg_ctrl_set: 0
# sysctl dev.myfirst.0.reg_ctrl_set=1
dev.myfirst.0.reg_ctrl_set: 0 -> 1
# sysctl dev.myfirst.0.reg_ctrl
dev.myfirst.0.reg_ctrl: 1
```

Al establecer el sysctl `ctrl_set` en `1` se habilita el dispositivo hipotético, activando el bit `ENABLE` en `CTRL`. Leer `reg_ctrl` lo confirma. El ciclo queda completo: el espacio de usuario escribe, el registro se actualiza, el espacio de usuario lee y el valor coincide.

### El patrón observador: acoplando registros al estado del driver

En este punto, el driver tiene un bloque de registros inerte. Escribir en `CTRL` no hace que el driver haga nada. Leer de `STATUS` devuelve lo que se escribió por última vez. Los registros existen; el driver los ignora.

El paso final de la Etapa 1 acopla dos pequeñas piezas del estado del driver al bloque de registros, para que la observación de registros desde el espacio de usuario refleje algo real.

El primer acoplamiento: limpiar el bit `READY` en `STATUS` mientras el driver está en estado de reset, y activarlo cuando el driver está en attach y operativo. En `myfirst_attach`, inmediatamente después de asignar `regs_buf`:

```c
MYFIRST_REG_WRITE(sc, MYFIRST_REG_STATUS, MYFIRST_STATUS_READY);
```

En `myfirst_detach`, antes de liberar `regs_buf`:

```c
MYFIRST_REG_WRITE(sc, MYFIRST_REG_STATUS, 0);
```

(En la práctica, el `free(regs_buf)` de detach hace que el borrado sea innecesario, pero el borrado explícito documenta la intención y refleja cómo un driver real señalaría al dispositivo que el driver se va a descargar.)

El segundo acoplamiento: si el código en espacio de usuario activa `CTRL.ENABLE`, se establece un indicador en el softc que el driver usa para decidir si emitir salida de heartbeat. Si el usuario lo desactiva, el indicador se borra. Esto requiere un pequeño cambio en el manejador del sysctl de escritura y una breve rutina que aplica el cambio:

```c
static void
myfirst_ctrl_update(struct myfirst_softc *sc, uint32_t old, uint32_t new)
{
        if ((old & MYFIRST_CTRL_ENABLE) != (new & MYFIRST_CTRL_ENABLE)) {
                device_printf(sc->dev, "CTRL.ENABLE now %s\n",
                    (new & MYFIRST_CTRL_ENABLE) ? "on" : "off");
        }
        /* Other fields will grow in later stages. */
}
```

El manejador del sysctl de escritura lo llama después de actualizar el registro:

```c
static int
myfirst_sysctl_reg_write(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        bus_size_t offset = arg2;
        uint32_t oldval, newval;
        int error;

        if (sc->regs_buf == NULL)
                return (ENODEV);
        oldval = MYFIRST_REG_READ(sc, offset);
        newval = oldval;
        error = sysctl_handle_int(oidp, &newval, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);
        MYFIRST_REG_WRITE(sc, offset, newval);

        /* Apply side effects of specific registers. */
        if (offset == MYFIRST_REG_CTRL)
                myfirst_ctrl_update(sc, oldval, newval);

        return (0);
}
```

Ahora, escribir `1` en `reg_ctrl_set` produce un `device_printf` en `dmesg` que registra la transición. Escribir `0` en `reg_ctrl_set` produce otro. El registro ya no es inerte; impulsa un comportamiento observable.

Este es un pequeño ejemplo de un patrón que se repetirá: el registro es una superficie de control, el driver reacciona a los cambios de registro, y el espacio de usuario (o en drivers reales, el dispositivo) desencadena esos cambios. En el Capítulo 17 automatizamos la parte del dispositivo con un callout que cambia los registros según un temporizador; en el Capítulo 18 apuntamos el driver a un dispositivo PCI real.

### Lo que logró la Etapa 1

Al final de la Sección 4, el driver cuenta con:

- Una cabecera `myfirst_hw.h` con offsets de registro, máscaras de bits y valores fijos.
- Un `regs_buf` en el softc, asignado en el attach y liberado en el detach.
- Funciones auxiliares de acceso (`myfirst_reg_read`, `myfirst_reg_write`, `myfirst_reg_update`) y macros (`MYFIRST_REG_READ`, `MYFIRST_REG_WRITE`) que encapsulan el acceso.
- Sysctls que exponen varios registros para lectura y un sysctl de escritura para uno de ellos.
- Un pequeño acoplamiento entre `CTRL.ENABLE` y un printf a nivel de driver.

La etiqueta de versión pasa a ser `0.9-mmio-stage1`. El driver sigue haciendo todo lo que el Capítulo 15 le proporcionó; simplemente ha crecido con un apéndice en forma de registros.

Compila, carga y prueba:

```text
# cd examples/part-04/ch16-accessing-hardware/stage1-register-map
# make clean && make
# kldload ./myfirst.ko
# sysctl dev.myfirst.0 | grep reg_
# sysctl dev.myfirst.0.reg_ctrl_set=1
# dmesg | tail
# sysctl dev.myfirst.0.reg_ctrl_set=0
# dmesg | tail
# kldunload myfirst
```

Deberías ver las transiciones de ENABLE en `dmesg` y los valores de los registros cambiando a través de `sysctl`. Si algún paso falla, las entradas de resolución de problemas de la Sección 4 al final del capítulo cubren los problemas más comunes.

### Una nota sobre lo que no es la Etapa 1

La Etapa 1 es un contenedor de estado con forma de registros. Todavía no usa `bus_space(9)` a nivel de API. Los accesores son accesos de memoria C simples detrás de una función auxiliar con nombre. La Sección 5 da el siguiente paso: reemplazar esas funciones auxiliares con llamadas reales a `bus_space_*` que operan sobre la misma memoria del kernel, para que el patrón de acceso del driver coincida con el aspecto de un driver real con un recurso real.

La razón para introducir la abstracción en dos pasadas es pedagógica. La Etapa 1 hace visible el mecanismo: puedes ver exactamente qué hace `MYFIRST_REG_READ` internamente. La Etapa 2 reemplaza el mecanismo visible con la API portable, y puedes comparar ambas versiones y ver que la API no hace nada que el auxiliar no hiciera ya, solo de forma más portable y con barreras apropiadas para la plataforma donde sea necesario. La estructura de dos pasos enseña ambas cosas.

### Cerrando la Sección 4

Simular hardware comienza con un mapa de registros y un fragmento de memoria del kernel. Una pequeña cabecera declara los offsets, las máscaras de bits y los valores fijos. Un campo del softc almacena la asignación. Las funciones auxiliares de acceso encapsulan la lectura y escritura. Los sysctls exponen los registros para que el lector pueda observarlos y manipularlos desde el espacio de usuario. Los pequeños acoplamientos entre los registros y el comportamiento a nivel de driver hacen concreta la abstracción.

El driver se encuentra ahora en la Etapa 1 del Capítulo 16. La Sección 5 integra `bus_space(9)` en esta configuración, reemplazando los accesores directos con la API portable que usa el resto de FreeBSD.



## Sección 5: usando `bus_space` en un contexto de driver real

La Sección 4 dotó al driver de un bloque de registros simulado y accesores directos. La Sección 5 reemplaza los accesores directos con la API `bus_space(9)` e integra el acceso a registros en la ruta de datos de `myfirst`. La forma del driver cambia en unos pocos aspectos pequeños pero deliberados, todos los cuales se parecen más a un driver de hardware real que la versión de la Etapa 1.

Esta sección comienza con el cambio más pequeño posible (reemplazar los cuerpos de los accesores con llamadas a `bus_space_*`) y va aumentando. Al final, la ruta `write(2)` del driver produce un acceso a registro como efecto secundario, la ruta `read(2)` del driver refleja el estado de los registros, y una tarea puede cambiar los valores de los registros mediante un temporizador para que el lector pueda ver al dispositivo «respirar» sin interacción desde el espacio de usuario.

### El truco pedagógico: usar `bus_space` sobre memoria del kernel

Un pequeño truco que hace posible la Sección 5: en x86, las funciones `bus_space_read_*` y `bus_space_write_*` para espacio de memoria se compilan como una simple desreferencia `volatile` de `handle + offset`. El handle es simplemente un valor del tipo `uintptr_t` que las funciones castean a puntero. Si establecemos el handle a `(bus_space_handle_t)sc->regs_buf` y el tag a `X86_BUS_SPACE_MEM`, la llamada `bus_space_read_4(tag, handle, offset)` ejecutará `*(volatile u_int32_t *)(regs_buf + offset)`, que es exactamente lo que hacía nuestro accesor de la Etapa 1.

Eso significa que, al menos en x86, podemos controlar nuestro bloque de registros simulado a través de la API real de `bus_space` proporcionando un tag y un handle que apunten a nuestra memoria asignada con `malloc`. El código del driver se vuelve entonces indistinguible, a nivel de código fuente, de un driver que accede a MMIO real. Ese es el objetivo: el vocabulario se transfiere.

En plataformas que no son x86, el truco es algo menos limpio. En arm64 y otras arquitecturas, `bus_space_tag_t` es un puntero a una estructura que describe el bus, y usar un tag fabricado requiere más configuración. Para el Capítulo 16, la ruta de simulación está centrada en x86; el capítulo reconoce la limitación arquitectónica y pospone la portabilidad entre arquitecturas al capítulo de portabilidad posterior. Las lecciones que enseña el Capítulo 16 son universalmente aplicables; solo este atajo específico para la simulación es propio de x86.

### Etapa 2: configurando el tag y el handle simulados

Añade dos campos al softc:

```c
struct myfirst_softc {
        /* ... all existing fields ... */

        /* Chapter 16 Stage 2: simulated bus_space tag and handle. */
        bus_space_tag_t  regs_tag;
        bus_space_handle_t regs_handle;
};
```

En `myfirst_attach`, después de asignar `regs_buf`:

```c
#if defined(__amd64__) || defined(__i386__)
sc->regs_tag = X86_BUS_SPACE_MEM;
#else
#error "Chapter 16 simulation path supports x86 only; see text for portability note."
#endif
sc->regs_handle = (bus_space_handle_t)(uintptr_t)sc->regs_buf;
```

Eso es toda la configuración. El handle es la dirección virtual de la asignación casteada a través de `uintptr_t`; el tag es la constante de la arquitectura para el espacio de memoria.

El `#error` para plataformas que no son x86 es una señal pedagógica deliberada: el capítulo indica explícitamente qué es portable (el vocabulario) y qué no lo es (este atajo de simulación específico). El Capítulo 17 y el capítulo de portabilidad enseñarán una alternativa más limpia. Hasta entonces, x86 es la plataforma compatible para los ejercicios del capítulo.

### Reemplazando los accesores

Con el tag y el handle configurados, los accesores se convierten en una sola línea sobre `bus_space_*`:

```c
static __inline uint32_t
myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset)
{
        KASSERT(offset + 4 <= sc->regs_size,
            ("myfirst: register read past end of block: offset=%#x size=%zu",
             (unsigned)offset, sc->regs_size));
        return (bus_space_read_4(sc->regs_tag, sc->regs_handle, offset));
}

static __inline void
myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset, uint32_t value)
{
        KASSERT(offset + 4 <= sc->regs_size,
            ("myfirst: register write past end of block: offset=%#x size=%zu",
             (unsigned)offset, sc->regs_size));
        bus_space_write_4(sc->regs_tag, sc->regs_handle, offset, value);
}
```

La firma cambia: las funciones auxiliares ahora reciben un `struct myfirst_softc *` en lugar de `regs_buf` y `regs_size` por separado. La comprobación interna de límites se conserva; el KASSERT se dispara si un bug en el driver produce un offset fuera de rango. El cuerpo usa `bus_space_read_4` y `bus_space_write_4` en lugar de acceso directo a memoria.

Las macros `MYFIRST_REG_READ` y `MYFIRST_REG_WRITE` se simplifican en consecuencia:

```c
#define MYFIRST_REG_READ(sc, offset)        myfirst_reg_read((sc), (offset))
#define MYFIRST_REG_WRITE(sc, offset, value) myfirst_reg_write((sc), (offset), (value))
```

Todos los accesos a registros en el driver, incluidos los manejadores de sysctl y `myfirst_reg_update`, siguen usando estas macros. Ninguno de los puntos de llamada cambia. El comportamiento del driver es idéntico al de la Etapa 1, pero la ruta de acceso ahora pasa por `bus_space`, y dicha ruta funcionaría igual de bien si `regs_tag` y `regs_handle` proviniesen de `rman_get_bustag` y `rman_get_bushandle` sobre un recurso real.

Compila el driver y confirma que sigue superando los ejercicios de sysctl de la Etapa 1:

```text
# cd examples/part-04/ch16-accessing-hardware/stage2-bus-space
# make clean && make
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.reg_device_id
dev.myfirst.0.reg_device_id: 1298498121
# sysctl dev.myfirst.0.reg_ctrl_set=1
# dmesg | tail
# kldunload myfirst
```

La salida coincide con la de la Etapa 1. El driver ahora usa `bus_space` de la misma forma que lo hace un driver real.

### Exponiendo `DATA_IN` a través de la ruta de escritura

Con la capa de accesores en su lugar, la Etapa 2 acopla el syscall `write(2)` del driver al registro `DATA_IN`. Cada byte escrito en el archivo de dispositivo `/dev/myfirst0` termina ahora en el registro `DATA_IN`, donde el lector puede observarlo.

Modifica `myfirst_write` (el callback `d_write`). El manejador existente lee bytes del uio, los copia en el buffer circular, notifica a los esperadores y retorna. El nuevo manejador hace lo mismo, y además: justo antes de retornar, escribe el byte copiado más recientemente en `DATA_IN` y activa el bit `DATA_AV` en `STATUS`:

```c
static int
myfirst_write(struct cdev *cdev, struct uio *uio, int flag)
{
        struct myfirst_softc *sc = cdev->si_drv1;
        uint8_t buf[MYFIRST_BOUNCE];
        size_t n;
        int error = 0;
        uint8_t last_byte = 0;
        bool wrote_any = false;

        /* ... existing writer-cap and lock acquisition ... */

        while (uio->uio_resid > 0) {
                n = MIN(uio->uio_resid, sizeof(buf));
                error = uiomove(buf, n, uio);
                if (error != 0)
                        break;

                /* Remember the most recent byte for the register update. */
                if (n > 0) {
                        last_byte = buf[n - 1];
                        wrote_any = true;
                }

                /* ... existing copy into the ring buffer ... */
        }

        /* ... existing unlock and cv_signal ... */

        /* Chapter 16 Stage 2: reflect the last byte in DATA_IN. */
        if (wrote_any) {
                MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_IN,
                    (uint32_t)last_byte);
                myfirst_reg_update(sc, MYFIRST_REG_STATUS,
                    0, MYFIRST_STATUS_DATA_AV);
        }

        return (error);
}
```

Ahora, tras cualquier `echo foo > /dev/myfirst0`, el registro `DATA_IN` contiene el valor en bytes de `'o'` (el último carácter de `"foo\n"` es en realidad `\n`, que equivale a `0x0a`), y el bit `DATA_AV` en `STATUS` está activado. El lector puede observar esto a través de sysctl:

```text
# echo -n "Hello" > /dev/myfirst0
# sysctl dev.myfirst.0.reg_data_in
dev.myfirst.0.reg_data_in: 111
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 9
```

`111` es el código ASCII de `'o'`, el último byte de «Hello». `9` es `MYFIRST_STATUS_READY | MYFIRST_STATUS_DATA_AV` (`1 | 8 = 9`). El driver ha producido, por primera vez, un efecto secundario observable externamente a nivel de registro en respuesta a una acción desde el espacio de usuario.

### Exponiendo `DATA_OUT` a través de la ruta de lectura

De forma simétrica, cada byte leído de `/dev/myfirst0` puede actualizar `DATA_OUT` para reflejar lo que se leyó por última vez. Modifica `myfirst_read`:

```c
static int
myfirst_read(struct cdev *cdev, struct uio *uio, int flag)
{
        struct myfirst_softc *sc = cdev->si_drv1;
        uint8_t buf[MYFIRST_BOUNCE];
        size_t n;
        int error = 0;
        uint8_t last_byte = 0;
        bool read_any = false;

        /* ... existing blocking logic and lock acquisition ... */

        while (uio->uio_resid > 0) {
                /* ... existing ring-buffer extraction ... */

                if (n > 0) {
                        last_byte = buf[n - 1];
                        read_any = true;
                }

                error = uiomove(buf, n, uio);
                if (error != 0)
                        break;
        }

        /* ... existing unlock and cv_signal ... */

        /* Chapter 16 Stage 2: reflect the last byte in DATA_OUT. */
        if (read_any) {
                MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_OUT,
                    (uint32_t)last_byte);
                /* If the ring buffer is now empty, clear DATA_AV. */
                if (cbuf_is_empty(&sc->cb))
                        myfirst_reg_update(sc, MYFIRST_REG_STATUS,
                            MYFIRST_STATUS_DATA_AV, 0);
        }

        return (error);
}
```

Ahora `DATA_OUT` refleja el último byte que leyó el lector, y `DATA_AV` se borra cuando el buffer circular se vacía. El ciclo desde «el usuario escribe un byte» hasta «el driver actualiza el registro», pasando por «el usuario lee un byte» y «el driver actualiza los registros», queda cerrado.

Prueba:

```text
# echo -n "ABC" > /dev/myfirst0
# sysctl dev.myfirst.0.reg_data_in dev.myfirst.0.reg_status
dev.myfirst.0.reg_data_in: 67
dev.myfirst.0.reg_status: 9
# dd if=/dev/myfirst0 bs=1 count=3 of=/dev/null
# sysctl dev.myfirst.0.reg_data_out dev.myfirst.0.reg_status
dev.myfirst.0.reg_data_out: 67
dev.myfirst.0.reg_status: 1
```

`67` es `'C'`, el último byte escrito. Después de que `dd` consume los tres bytes, `DATA_OUT` contiene `'C'` (el último byte leído) y `STATUS` vuelve a ser solo `READY` porque `DATA_AV` se borró.

### Controlando el estado de los registros desde una tarea

El bloque de registros hasta ahora refleja acciones del driver desencadenadas por syscalls del espacio de usuario. Para ilustrar un patrón controlado por tareas, añade una pequeña tarea que incrementa `SCRATCH_A` periódicamente. Este es un ejemplo artificial; existe para que el lector pueda ver cómo los valores de los registros cambian de forma autónoma en respuesta a eventos desencadenados por tareas, preparando el terreno para el Capítulo 17, donde los callouts y los temporizadores impulsan cambios más realistas.

En el softc:

```c
struct task     reg_ticker_task;
int             reg_ticker_enabled;
```

El callback de la tarea:

```c
static void
myfirst_reg_ticker_cb(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        if (!myfirst_is_attached(sc))
                return;

        MYFIRST_REG_WRITE(sc, MYFIRST_REG_SCRATCH_A,
            MYFIRST_REG_READ(sc, MYFIRST_REG_SCRATCH_A) + 1);
}
```

La tarea se encola desde el callout tick_source existente (el callout del Capítulo 14 que ya se dispara según un temporizador). En el callback del callout, junto con el encole de la tarea selwake:

```c
if (sc->reg_ticker_enabled)
        taskqueue_enqueue(sc->tq, &sc->reg_ticker_task);
```

Y un sysctl para habilitarla:

```c
SYSCTL_ADD_INT(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "reg_ticker_enabled",
    CTLFLAG_RW, &sc->reg_ticker_enabled, 0,
    "Enable the periodic register ticker (increments SCRATCH_A each tick)");
```

Inicialización en el attach:

```c
TASK_INIT(&sc->reg_ticker_task, 0, myfirst_reg_ticker_cb, sc);
sc->reg_ticker_enabled = 0;
```

Vaciado en el detach, en el bloque de vaciado de tareas existente:

```c
taskqueue_drain(sc->tq, &sc->reg_ticker_task);
```

Con esto en su lugar, habilitar el ticker produce un efecto visible en los registros:

```text
# sysctl dev.myfirst.0.reg_ticker_enabled=1
# sleep 5
# sysctl dev.myfirst.0.reg_scratch_a
dev.myfirst.0.reg_scratch_a: 5
# sleep 5
# sysctl dev.myfirst.0.reg_scratch_a
dev.myfirst.0.reg_scratch_a: 10
# sysctl dev.myfirst.0.reg_ticker_enabled=0
```

El valor del registro sube de uno en uno por segundo, a medida que se dispara el callout tick_source. El driver exhibe ahora un comportamiento autónomo a nivel de registro, desencadenado por una tarea y mediado por `bus_space`.

### El árbol de sysctl completo de la Etapa 2

Tras la Etapa 2, el árbol de sysctl completo bajo `dev.myfirst.0` tiene un aspecto similar al siguiente:

```text
dev.myfirst.0.debug_level
dev.myfirst.0.soft_byte_limit
dev.myfirst.0.nickname
dev.myfirst.0.heartbeat_interval_ms
dev.myfirst.0.watchdog_interval_ms
dev.myfirst.0.tick_source_interval_ms
dev.myfirst.0.bulk_writer_batch
dev.myfirst.0.reset_delayed
dev.myfirst.0.writers_limit
dev.myfirst.0.writers_sema_value
dev.myfirst.0.writers_trywait_failures
dev.myfirst.0.stats_cache_refresh_count
dev.myfirst.0.reg_ctrl
dev.myfirst.0.reg_status
dev.myfirst.0.reg_data_in
dev.myfirst.0.reg_data_out
dev.myfirst.0.reg_intr_mask
dev.myfirst.0.reg_intr_status
dev.myfirst.0.reg_device_id
dev.myfirst.0.reg_firmware_rev
dev.myfirst.0.reg_scratch_a
dev.myfirst.0.reg_scratch_b
dev.myfirst.0.reg_ctrl_set
dev.myfirst.0.reg_ticker_enabled
```

Diez vistas de registro, un registro de escritura, un conmutador del ticker, más todos los sysctls anteriores de los Capítulos 11 al 15. El driver ha crecido, pero cada adición es pequeña y tiene nombre.

### Una nota sobre las lecturas de `STATUS` mientras el driver está en ejecución

En las configuraciones de Stage 1 y Stage 2, leer `STATUS` mediante sysctl devuelve los bits que el driver estableció por última vez. Ninguna lectura tiene efectos secundarios. Esto es intencional para el capítulo 16. Pero observa una consecuencia sutil: el driver puede activar el bit `STATUS.DATA_AV` en la ruta de escritura y borrarlo en la ruta de lectura, y el lector en espacio de usuario puede observar cómo cambia ese bit a lo largo del tiempo. Es posible ejecutar `sysctl -w dev.myfirst.0.reg_status=0` mediante el sysctl escribible, pero las actualizaciones automáticas del driver volverán a activar el bit en la siguiente escritura al archivo de dispositivo.

Así es como funciona un driver de dispositivo de "polling" a nivel conceptual: el driver consulta el registro de estado periódicamente, reacciona a los cambios de estado y actualiza el estado visible por el driver en consecuencia. Los bits `STATUS` de un dispositivo real cambian por razones de hardware; los del dispositivo simulado cambian por razones propias del driver simulado. El mecanismo es el mismo.

El capítulo 19 introduce las interrupciones, que sustituyen el modelo de polling por uno basado en eventos. Hasta entonces, el polling es un patrón razonable para el dispositivo simulado.

### La ruta de detach actualizada

Cada capítulo de la Parte 3 añadió unas pocas líneas al orden de detach. El Capítulo 16, Etapa 2, añade dos: vaciar `reg_ticker_task` y liberar el buffer de registros. El orden completo de detach en la Etapa 2:

1. Rechazar el detach si `active_fhs > 0`.
2. Limpiar `is_attached` (de forma atómica) y hacer broadcast de las cvs.
3. Vaciar todos los callouts (heartbeat, watchdog, tick_source).
4. Vaciar todas las tasks (selwake, bulk_writer, reset_delayed, recovery, reg_ticker).
5. `seldrain(&sc->rsel)`, `seldrain(&sc->wsel)`.
6. `taskqueue_free(sc->tq)`.
7. Destruir el cdev.
8. Liberar el contexto sysctl.
9. **Liberar `regs_buf`.** (Nuevo en la Etapa 2.)
10. Destruir cbuf, contadores, cvs, locks sx, el semáforo y el mutex.

La liberación de `regs_buf` se produce después de desmantelar el contexto sysctl, porque un manejador sysctl podría estar ejecutándose en otro CPU durante el detach. Tras `sysctl_ctx_free`, ningún manejador sysctl puede alcanzar el softc, y la liberación es segura. Los drivers reales siguen la misma disciplina para sus liberaciones de recursos.

### Actualización de `LOCKING.md` (y ahora también `HARDWARE.md`)

La Parte 3 estableció `LOCKING.md` como el mapa de sincronización del driver. El Capítulo 16 abre un documento hermano: `HARDWARE.md`. Reside junto a `LOCKING.md` y documenta la interfaz de registros, los patrones de acceso y las reglas de propiedad para el estado orientado al hardware.

Un primer borrador:

```text
# myfirst Hardware Interface

## Register Block

Size: 64 bytes (MYFIRST_REG_SIZE).
Access: 32-bit reads and writes on 32-bit-aligned offsets.
Allocated in attach, freed in detach.

### Register Map

| Offset | Name          | Direction | Owner      |
|--------|---------------|-----------|------------|
| 0x00   | CTRL          | R/W       | driver    |
| 0x04   | STATUS        | R/W       | driver    |
| 0x08   | DATA_IN       | W         | syscall   |
| 0x0c   | DATA_OUT      | R         | syscall   |
| 0x10   | INTR_MASK     | R/W       | driver    |
| 0x14   | INTR_STATUS   | R/W       | driver    |
| 0x18   | DEVICE_ID     | R         | attach    |
| 0x1c   | FIRMWARE_REV  | R         | attach    |
| 0x20   | SCRATCH_A     | R/W       | ticker    |
| 0x24   | SCRATCH_B     | R/W       | free      |

## Write Protections

Stage 2 does not lock register access. A sysctl writer, a syscall
writer, and the ticker task can each access the same register without
a lock. See Section 6 for the locking story.

## Access Paths

- Sysctl read handlers:  MYFIRST_REG_READ
- Sysctl write handlers: MYFIRST_REG_WRITE, with side-effect call
- Syscall write path:    MYFIRST_REG_WRITE(DATA_IN), myfirst_reg_update(STATUS)
- Syscall read path:     MYFIRST_REG_WRITE(DATA_OUT), myfirst_reg_update(STATUS)
- Ticker task:           MYFIRST_REG_WRITE(SCRATCH_A)
```

El documento es breve por ahora y crecerá a medida que los capítulos posteriores añadan más registros y más rutas de acceso. La disciplina de documentar la interfaz de registros junto al código es la misma que la disciplina de la Parte 3 de documentar los locks.

### Etapa 2 completa

Al final de la Sección 5, el driver se encuentra en `0.9-mmio-stage2`. Dispone de:

- Una ruta de acceso real con `bus_space_*` sobre un tag y un handle simulados.
- `DATA_IN` reflejando el último byte escrito, con `DATA_AV` haciendo seguimiento del estado del ring buffer.
- `DATA_OUT` reflejando el último byte leído.
- Una task que incrementa de forma autónoma un registro de prueba mediante un temporizador.
- Visibilidad sysctl completa de todos los registros.
- Un documento `HARDWARE.md` que describe la interfaz.

Compilar, cargar, probar, observar, descargar:

```text
# cd examples/part-04/ch16-accessing-hardware/stage2-bus-space
# make clean && make
# kldload ./myfirst.ko
# echo -n "hello" > /dev/myfirst0
# sysctl dev.myfirst.0.reg_data_in dev.myfirst.0.reg_status
# dd if=/dev/myfirst0 bs=1 count=5 of=/dev/null
# sysctl dev.myfirst.0.reg_data_out dev.myfirst.0.reg_status
# sysctl dev.myfirst.0.reg_ticker_enabled=1 ; sleep 3
# sysctl dev.myfirst.0.reg_scratch_a
# sysctl dev.myfirst.0.reg_ticker_enabled=0
# kldunload myfirst
```

Las salidas deben contar una historia coherente sobre lo que el driver y los registros están haciendo.

### Un vistazo a un patrón real: la ruta de attach del driver `em`

Ahora que la Etapa 2 refleja la estructura de un driver real, veamos brevemente qué aspecto tendría si el bloque de registros fuera una región MMIO PCI real, solo para fijar la expectativa. En `/usr/src/sys/dev/e1000/if_em.c`, dentro de `em_allocate_pci_resources`, encontrarás:

```c
sc->memory = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
if (sc->memory == NULL) {
        device_printf(dev, "Unable to allocate bus resource: memory\n");
        return (ENXIO);
}
sc->osdep.mem_bus_space_tag = rman_get_bustag(sc->memory);
sc->osdep.mem_bus_space_handle = rman_get_bushandle(sc->memory);
sc->hw.hw_addr = (uint8_t *)&sc->osdep.mem_bus_space_handle;
```

El recurso se asigna, el tag y el handle se extraen en la estructura `osdep` del softc, y se configura un puntero adicional `hw_addr` para el código de la capa de abstracción de hardware que Intel comparte entre drivers. El resto del driver utiliza macros (`E1000_READ_REG`, `E1000_WRITE_REG`) definidas sobre `bus_space_*` para comunicarse con el hardware.

La forma es la misma que la de nuestra Etapa 2. La diferencia es exactamente una llamada a función: `bus_alloc_resource_any` para un driver real, y `malloc(9)` más un tag construido a mano para el nuestro. Todo lo que está por encima de la capa de asignación es idéntico.

El Capítulo 18 sustituirá nuestro `malloc` por `bus_alloc_resource_any` y apuntará el driver a un dispositivo PCI real. Las capas superiores del driver no cambiarán.

### Cerrando la Sección 5

La Etapa 2 reemplaza los accesores directos de la Etapa 1 por llamadas reales a `bus_space(9)` que operan sobre el tag y el handle simulados. Las rutas `write(2)` y `read(2)` del driver producen ahora efectos secundarios a nivel de registro. Una task actualiza un registro de prueba mediante un temporizador. El documento `HARDWARE.md` describe la interfaz de registros. La forma del driver se asemeja mucho a la de un driver real como `if_em`.

La Sección 6 introduce la disciplina de seguridad que exige el MMIO real: barreras de memoria, locking alrededor de registros compartidos, y las razones por las que los bucles de espera activa son un error. La Etapa 3 del driver incorpora esa disciplina.



## Sección 6: Seguridad y sincronización con MMIO

La Etapa 2 funciona, pero es insegura en tres aspectos concretos que la Sección 6 nombra y corrige. El primero es que el acceso a los registros no es atómico. El segundo es que el orden de los registros no está garantizado. El tercero es que no existe ninguna disciplina sobre qué contexto puede acceder a qué registros. Cada uno de estos aspectos es una categoría de bug que puede dañar un driver real; y cada uno es corregible con la disciplina que el capítulo ya enseñó en la Parte 3, aplicada ahora al nuevo estado orientado al hardware.

Esta sección recorre cada problema, explica por qué importa y produce la Etapa 3 del driver: una versión correcta bajo acceso concurrente, segura en cuanto al orden de operaciones en las plataformas que lo requieren, y claramente particionada por contexto.

### Por qué un acceso a un registro puede ser inseguro sin un lock

Imagina dos threads de espacio de usuario escribiendo bytes distintos en `/dev/myfirst0` de forma concurrente. En la Etapa 2, ambos llaman a `myfirst_write`, que a su vez llama a `MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_IN, last_byte)`. Sin un lock, las dos escrituras compiten: una termina primero, la otra después, y `DATA_IN` acaba con el valor que se escribió en último lugar. Eso no es exactamente incorrecto; ambos bytes eran realmente el último byte de sus respectivas escrituras. Pero el driver no tiene manera de saber qué valor de `DATA_IN` provino de qué escritor.

De manera más sutil, considera `myfirst_reg_update`, que realiza una secuencia de lectura-modificación-escritura sobre un registro. Dos threads que la llaman sobre el mismo registro en paralelo pueden producir una pérdida de actualización clásica. El thread A lee `CTRL = 0`. El thread B lee `CTRL = 0`. El thread A activa el bit `ENABLE` y escribe `CTRL = 1`. El thread B activa el bit `RESET` y escribe `CTRL = 2`. El resultado es `CTRL = 2`, con `ENABLE` perdido. En cualquier registro donde varios contextos realizan operaciones de lectura-modificación-escritura, esto es un bug de condición de carrera que puede provocar fallos de protocolo reales.

La solución es conocida: un lock. La única pregunta es cuál. El `sc->mtx` del Capítulo 11 protege la ruta de datos; es la elección natural para los accesos a registros que ocurren dentro de la ruta de datos. Se puede introducir un mutex separado, `sc->reg_mtx`, para los accesos a registros que ocurren fuera de la ruta de datos (manejadores sysctl, la ticker task). Ambos pueden ser el mismo lock o locks distintos, según los patrones de acceso del driver.

Para la Etapa 3, tomamos el camino más sencillo: usar `sc->mtx` para todos los accesos a registros. Esto aplica la regla "ningún acceso a registros sin el mutex del driver" con un único primitivo. El coste es que los manejadores sysctl y la ticker task deben adquirir brevemente el mutex del driver, lo que los serializa con la ruta de datos. Para un driver tan pequeño como este, el coste es insignificante.

### Añadir el lock

Modifica `myfirst_reg_read` y `myfirst_reg_write` para que verifiquen que el lock del driver está adquirido, y modifica sus llamadores para que lo adquieran. Una aserción es más barata que adquirir el lock dentro del accesor, y hace visible la regla de locking en cada punto de llamada.

```c
static __inline uint32_t
myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset)
{
        MYFIRST_ASSERT(sc);   /* Chapter 11: mtx_assert(&sc->mtx, MA_OWNED). */
        KASSERT(offset + 4 <= sc->regs_size, (...));
        return (bus_space_read_4(sc->regs_tag, sc->regs_handle, offset));
}

static __inline void
myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset, uint32_t value)
{
        MYFIRST_ASSERT(sc);
        KASSERT(offset + 4 <= sc->regs_size, (...));
        bus_space_write_4(sc->regs_tag, sc->regs_handle, offset, value);
}
```

La macro `MYFIRST_ASSERT` del Capítulo 11 verifica que `sc->mtx` está adquirido en modo `MA_OWNED`. Un kernel de depuración detecta cualquier llamador que olvidó adquirir el lock; un kernel de producción elimina la comprobación.

Ahora cada punto de llamada debe adquirir el lock. El manejador sysctl queda así:

```c
static int
myfirst_sysctl_reg(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        bus_size_t offset = arg2;
        uint32_t value;

        if (!myfirst_is_attached(sc))
                return (ENODEV);

        MYFIRST_LOCK(sc);
        if (sc->regs_buf == NULL) {
                MYFIRST_UNLOCK(sc);
                return (ENODEV);
        }
        value = MYFIRST_REG_READ(sc, offset);
        MYFIRST_UNLOCK(sc);

        return (sysctl_handle_int(oidp, &value, 0, req));
}
```

El lock se adquiere antes de la lectura del registro, se mantiene brevemente y se libera antes de llamar a `sysctl_handle_int` del framework sysctl. El framework puede dormir (copia el valor al espacio de usuario), por lo que el lock no puede mantenerse durante esa llamada.

De manera similar, el manejador sysctl con escritura:

```c
static int
myfirst_sysctl_reg_write(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        bus_size_t offset = arg2;
        uint32_t oldval, newval;
        int error;

        if (!myfirst_is_attached(sc))
                return (ENODEV);

        MYFIRST_LOCK(sc);
        if (sc->regs_buf == NULL) {
                MYFIRST_UNLOCK(sc);
                return (ENODEV);
        }
        oldval = MYFIRST_REG_READ(sc, offset);
        MYFIRST_UNLOCK(sc);

        newval = oldval;
        error = sysctl_handle_int(oidp, &newval, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);

        MYFIRST_LOCK(sc);
        if (sc->regs_buf == NULL) {
                MYFIRST_UNLOCK(sc);
                return (ENODEV);
        }
        MYFIRST_REG_WRITE(sc, offset, newval);
        if (offset == MYFIRST_REG_CTRL)
                myfirst_ctrl_update(sc, oldval, newval);
        MYFIRST_UNLOCK(sc);

        return (0);
}
```

El manejador adquiere el lock dos veces: una para leer el valor actual y otra para aplicar el nuevo valor. Entre ambas adquisiciones, el lock se libera y se ejecuta `sysctl_handle_int`. El patrón es ligeramente incómodo, pero es estándar en FreeBSD: adquieres un lock para una operación breve, lo liberas para una llamada que puede dormir, lo vuelves a adquirir para la siguiente operación breve, y aceptas que el estado puede haber cambiado entretanto.

La llamada a `myfirst_ctrl_update` ocurre ahora bajo el lock, de modo que su printf sigue funcionando, pero cualquier cambio de estado futuro que realice puede confiar en la propiedad del lock.

El callback de la ticker task también adquiere el lock:

```c
static void
myfirst_reg_ticker_cb(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        if (!myfirst_is_attached(sc))
                return;

        MYFIRST_LOCK(sc);
        if (sc->regs_buf != NULL) {
                uint32_t v = MYFIRST_REG_READ(sc, MYFIRST_REG_SCRATCH_A);
                MYFIRST_REG_WRITE(sc, MYFIRST_REG_SCRATCH_A, v + 1);
        }
        MYFIRST_UNLOCK(sc);
}
```

Y las rutas `myfirst_write` y `myfirst_read`, que ya mantenían `sc->mtx` alrededor del acceso al ring buffer, necesitan extender esa retención para cubrir también las actualizaciones de registros, o liberar y readquirir brevemente. El cambio más sencillo es mantener las actualizaciones de registros dentro de la región con lock ya existente:

```c
/* In myfirst_write, while still holding sc->mtx after the ring-buffer update: */
if (wrote_any) {
        MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_IN, (uint32_t)last_byte);
        myfirst_reg_update(sc, MYFIRST_REG_STATUS, 0, MYFIRST_STATUS_DATA_AV);
}
```

Como `myfirst_reg_update` ahora verifica que el mutex está adquirido, y lo está, la llamada tiene éxito. El tiempo de retención del lock crece ligeramente, pero solo en unas pocas llamadas a `bus_space_write_4`, que se compilan en instrucciones `mov` individuales; el coste es insignificante.

### Por qué las barreras importan incluso en x86

Con el locking en su lugar, el driver es correcto bajo concurrencia en x86. En plataformas con orden débil de memoria (arm64, RISC-V, algunos MIPS más antiguos), la historia no está del todo terminada. Una secuencia como:

```c
MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_IN, value);
MYFIRST_REG_WRITE(sc, MYFIRST_REG_CTRL, CTRL_GO);
```

implica que la escritura en `DATA_IN` llega al dispositivo antes del disparo de `CTRL.GO`. En x86, el hardware preserva el orden del programa para las escrituras, y la reordenación del compilador está limitada por el calificador `volatile` en `bus_space_write_4`. En arm64, la CPU puede reordenar las dos escrituras, y el dispositivo podría ver `CTRL.GO` antes de que `DATA_IN` esté listo, lo que rompe el protocolo.

La solución es una barrera de escritura:

```c
MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_IN, value);
bus_space_barrier(sc->regs_tag, sc->regs_handle, 0, sc->regs_size,
    BUS_SPACE_BARRIER_WRITE);
MYFIRST_REG_WRITE(sc, MYFIRST_REG_CTRL, CTRL_GO);
```

En x86, esta barrera es una valla de compilador. En arm64, es una instrucción DSB o DMB que fuerza a que la primera escritura se complete antes de que se emita la segunda.

Para el Capítulo 16, el protocolo que estamos simulando no requiere realmente este orden, porque nuestro "dispositivo" es memoria del kernel cuyos observadores adquieren el mismo lock y no reordenan dentro de una sección crítica. Pero el hábito merece la pena desarrollarlo. Cuando el código hable finalmente con un dispositivo real, las barreras estarán ahí, y el driver será portable entre arquitecturas.

Como vehículo pedagógico, introduce un helper que facilite las escrituras anotadas con barreras:

```c
static __inline void
myfirst_reg_write_barrier(struct myfirst_softc *sc, bus_size_t offset,
    uint32_t value, int flags)
{
        MYFIRST_ASSERT(sc);
        MYFIRST_REG_WRITE(sc, offset, value);
        bus_space_barrier(sc->regs_tag, sc->regs_handle, 0, sc->regs_size,
            flags);
}
```

Los flags son `BUS_SPACE_BARRIER_READ`, `BUS_SPACE_BARRIER_WRITE`, o la OR de ambos. Un driver que lee estado justo después de escribir un comando usa el flag combinado. Uno que solo quiere que las escrituras posteriores vean el efecto de esta escritura usa simplemente `WRITE`.

El driver de la Etapa 3 no usa `myfirst_reg_write_barrier` en muchos sitios; está definido y se usa en una única ruta de demostración (dentro de la ticker, tras el incremento del registro de prueba, para ilustrar el uso). Los capítulos posteriores que traten protocolos reales lo usarán con más intensidad.

### Particionado del acceso a registros por contexto

Con el locking uniforme, la siguiente pregunta es: ¿qué contextos acceden a qué registros, y esa combinación es intencionada?

Una auditoría de la Etapa 3 sobre el driver muestra:

- Contexto syscall (escritura): accede a `DATA_IN`, `STATUS`.
- Contexto syscall (lectura): accede a `DATA_OUT`, `STATUS`.
- Contexto sysctl: accede a todos los registros (lectura) y a `CTRL`, `SCRATCH_A`, `SCRATCH_B`, etc. (escritura) a través del sysctl con escritura.
- Contexto task (ticker): accede a `SCRATCH_A`.

Todos los accesos están protegidos por lock. Todos los accesos tocan un registro que el driver ha asignado explícitamente para ese propósito. La disciplina de acceso es sencilla: los syscalls leen y escriben los registros de datos y el bit de datos disponibles; los sysctls son para inspección y configuración puntual; la ticker escribe un único registro específico. Una regla del tipo "los contextos no solapan sus responsabilidades de registro" es fácil de enunciar y fácil de mantener.

Esto es precisamente para lo que existe `HARDWARE.md`. Actualiza el documento para incluir la propiedad por registro:

```text
## Per-Register Owners

CTRL:          sysctl writer, driver (via myfirst_ctrl_update)
STATUS:        driver (via write/read paths)
DATA_IN:       syscall write path
DATA_OUT:      syscall read path
INTR_MASK:     sysctl writer only (Stage 3); driver attach (Chapter 19)
INTR_STATUS:   sysctl writer only (Stage 3)
DEVICE_ID:     attach only (initialised once, never written thereafter)
FIRMWARE_REV:  attach only (initialised once, never written thereafter)
SCRATCH_A:     ticker task; sysctl writer
SCRATCH_B:     sysctl writer only
```

Un colaborador futuro puede echar un vistazo a esta tabla y ver inmediatamente dónde se espera una escritura en un registro. Un cambio futuro que añada un nuevo propietario deberá actualizar la tabla, lo que mantiene la documentación honesta.

### Evitar los bucles de espera activa

Un tipo de bug en el que caen los autores de drivers noveles es el bucle de espera activa para el estado de un registro. El ejemplo canónico:

```c
/* BAD: busy-waits forever if the device never becomes ready. */
while ((MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_READY) == 0)
        ;
```

El bucle gira leyendo el registro hasta que el bit `READY` queda activado. En hardware real, el tiempo entre «no listo» y «listo» puede ser de microsegundos. En un sistema sobrecargado, puede ser mayor. Durante el giro, la CPU queda consumida por el bucle; ningún otro thread en esa CPU puede ejecutarse; los propios threads del driver no pueden siquiera desbloquear el mutex que el bucle esté sosteniendo en ese momento.

Existen varios patrones mejores.

**Giro acotado con `DELAY(9)` para esperas cortas.** Si la espera prevista es corta (normalmente menos de unos pocos cientos de microsegundos), utiliza un bucle con un número máximo de iteraciones y un `DELAY` entre lecturas. `DELAY(usec)` realiza una espera activa de al menos `usec` microsegundos, permitiendo que la CPU atienda interrupciones durante ese tiempo.

```c
for (i = 0; i < 100; i++) {
        if (MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_READY)
                break;
        DELAY(10);
}
if ((MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_READY) == 0) {
        device_printf(sc->dev, "timeout waiting for READY\n");
        return (ETIMEDOUT);
}
```

El bucle se ejecuta como máximo 100 veces, esperando 10 microsegundos entre lecturas, con un límite total de 1 milisegundo. Si la operación concluye con éxito, el bucle sale antes de agotar todas las iteraciones. Al superar el tiempo límite, el driver abandona y devuelve un error.

**Espera con suspensión mediante `msleep`.** Para esperas previstas más largas (de milisegundos a segundos), evita por completo la espera activa. Suspende el thread hasta que llegue una señal de activación o hasta que expire el tiempo de espera. El ejemplo siguiente es hipotético (nuestro dispositivo simulado nunca limpia `READY` en el Capítulo 16), pero muestra la forma a la que recurrirás cuando el hardware real comience a cambiar bits de estado:

```c
/* Hypothetical; assumes sc->status_wait is a dummy address the driver
 * uses as a sleep channel and a matching wakeup(&sc->status_wait) fires
 * when the ready bit is expected to change. */
while ((MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_READY) == 0) {
        error = msleep(&sc->status_wait, &sc->mtx, PCATCH, "myfready", hz / 10);
        if (error == EWOULDBLOCK) {
                /* Timeout: return to caller with ETIMEDOUT. */
                return (ETIMEDOUT);
        }
        if (error != 0)
                return (error);
}
```

El thread se suspende sobre `&sc->status_wait`, usando el mutex del driver como interlock, durante un máximo de 100 ms. Una señal de activación procedente de otro contexto (normalmente un manejador de interrupciones o una tarea que observó el cambio de registro) interrumpe la suspensión. En arm64, donde el sondeo de registros resultaría costoso e impreciso, este patrón es claramente preferible. El Capítulo 17 concreta este patrón: un callout activa `READY` mediante un temporizador, y una ruta de syscall se suspende en el canal correspondiente hasta que el callout la despierte.

**Orientado a eventos con `cv_wait`.** Igual que el anterior, pero usando una variable de condición, lo que resulta más natural en el estilo de la Parte 3:

```c
MYFIRST_LOCK(sc);
while ((MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_READY) == 0) {
        cv_timedwait_sig(&sc->status_cv, &sc->mtx, hz / 10);
}
MYFIRST_UNLOCK(sc);
```

Con un `cv_signal` correspondiente en el contexto que activa el bit.

El dispositivo simulado del Capítulo 16 no necesita ninguno de estos patrones todavía, ya que el bit `READY` se activa en el attach y nunca se limpia. Sin embargo, estos patrones se presentan aquí porque la Sección 6 es el lugar adecuado para introducirlos, y el Capítulo 17 los empleará cuando los bits de estado empiecen a cambiar de forma dinámica.

### Interrupciones y MMIO: una referencia anticipada

Una breve nota sobre un tema que le corresponde al Capítulo 19. Cuando un driver real tiene un manejador de interrupciones que se ejecuta en contexto filter o ithread, el acceso a registros desde ese manejador tiene restricciones adicionales. Los manejadores filter se ejecutan en un contexto de interrupción con primitivas muy limitadas disponibles; habitualmente confirman la interrupción escribiendo en un registro, registran el evento de algún modo y difieren el trabajo real a una tarea. La escritura de confirmación es exactamente el tipo de operación para la que existe `bus_space_write_*`, y se ejecuta bajo reglas de locking específicas que difieren del mutex habitual del driver.

El driver del Capítulo 16 no tiene manejador de interrupciones, así que esta preocupación no aplica todavía. El Capítulo 19 introduce el manejador y los cambios en el tipo de lock que este impone. Por ahora, trata «el acceso a registros en contexto de interrupción» como un tema que ya sabes que existe y que aprenderás más adelante; el mecanismo de acceso a registros (`bus_space_*`) es el mismo, pero el locking que lo rodea cambia.

### Stage 3 completado

Con el locking añadido, las barreras introducidas, la propiedad de contexto documentada y los patrones de espera activa desaconsejados, el driver se encuentra en `0.9-mmio-stage3`. La estructura del driver sigue siendo la de Stage 2, pero cada acceso a registro está ahora protegido por lock, el patrón de acceso de cada contexto está documentado y el driver está preparado para manejar protocolos de hardware más sofisticados en capítulos posteriores.

Compila, prueba y somete a estrés:

```text
# cd examples/part-04/ch16-accessing-hardware/stage3-synchronized
# make clean && make
# kldload ./myfirst.ko
# examples/part-04/ch16-accessing-hardware/labs/reg_stress.sh
```

El script de estrés lanza varios escritores, lectores, lectores de sysctl y operaciones de alternancia del ticker de forma concurrente, y comprueba que el estado final del registro sea consistente. Con WITNESS activo, cualquier violación de locking produce un aviso inmediato; con INVARIANTS activo, cualquier acceso fuera de límites provoca un panic. Si el script termina sin errores, la disciplina de registro del driver es correcta.

### Cerrando la Sección 6

La seguridad en MMIO descansa en tres disciplinas: locking (cada acceso a registro ocurre bajo el lock apropiado), ordering (barreras donde el protocolo las requiere, incluso si la plataforma es x86) y partición de contexto (cada registro tiene un propietario nombrado y una ruta de acceso definida). El archivo `HARDWARE.md` del driver recoge las dos últimas; las aserciones de mutex en los accesores imponen la primera.

La Sección 7 da el siguiente paso: hacer el acceso a registros observable para la depuración. Logs, sysctls, sondas de DTrace y la pequeña capa de observabilidad que detecta errores antes de que se conviertan en crashes.



## Sección 7: Depuración y trazado del acceso al hardware

Un acceso a registro es, por diseño, invisible. Se compila en una instrucción de CPU que lee o escribe unos pocos bytes. No hay marco de pila, ni registro de llamadas, ni valor de retorno que puedas imprimir con `printf` sin añadir código. Cuando un driver funciona, la invisibilidad es una virtud. Cuando no funciona, la invisibilidad es el problema.

La Sección 7 cubre las herramientas e idiomas que hacen el acceso a registros suficientemente visible para depurar, sin ser tan ruidoso que el driver se vuelva ilegible. El objetivo es una pequeña capa de observabilidad: instrumentación suficiente para detectar errores pronto, colocada donde un principiante pueda activarla y desactivarla, e integrada con el logging que el resto del driver ya realiza.

### Qué quieres observar

Hay tres cosas que merece la pena ver cuando un driver se comporta de forma incorrecta.

**El valor en un registro concreto, ahora mismo.** Los manejadores de sysctl del Stage 2 ya te proporcionan esto. Un `sysctl dev.myfirst.0.reg_ctrl` devuelve el valor actual en cualquier momento, y ningún otro aspecto del comportamiento del driver cambia a causa de la lectura.

**La secuencia de accesos a registro que el driver realizó recientemente.** Cuando un error involucra el orden de los registros o una manipulación de bits incorrecta, saber «el driver escribió 0x1, luego 0x2 y después 0x4 en ese orden» es exactamente lo que necesitas. La secuencia bruta no puede reconstruirse solo a partir del contenido de los registros.

**La pila y el contexto de un acceso a registro concreto.** Cuando se produce una escritura desde una ruta de código inesperada, quieres saber qué función la realizó, qué thread estaba en ejecución y qué había en la pila. DTrace es bueno en esto.

El resto de esta sección recorre cada tipo de observación y muestra qué añadir al driver para habilitarla.

### Un log de acceso sencillo

La herramienta de observabilidad más sencilla es un log de los últimos N accesos a registro, almacenado en un buffer circular dentro del softc. Cada lectura y escritura de registro almacena una entrada; el sysctl expone el buffer circular.

Define la entrada del log:

```c
#define MYFIRST_ACCESS_LOG_SIZE 64

struct myfirst_access_log_entry {
        uint64_t      timestamp_ns;
        uint32_t      value;
        bus_size_t    offset;
        uint8_t       is_write;
        uint8_t       width;
        uint8_t       context_tag;
        uint8_t       _pad;
};
```

Cada entrada ocupa 24 bytes y almacena el tiempo (nanosegundos desde el arranque), el valor, el desplazamiento, si fue una lectura o escritura, el ancho del acceso y una etiqueta que identifica el contexto del llamante (syscall, tarea, sysctl). El relleno redondea a 24; un log de 64 entradas ocupa 1,5 KiB, lo cual es trivialmente pequeño.

Añade el buffer circular al softc:

```c
struct myfirst_access_log_entry access_log[MYFIRST_ACCESS_LOG_SIZE];
unsigned int access_log_head;   /* index of next write */
bool          access_log_enabled;
```

Registra una entrada en los accesores. El `myfirst_reg_write` del Stage 3 se convierte en:

```c
static __inline void
myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset, uint32_t value)
{
        MYFIRST_ASSERT(sc);
        KASSERT(offset + 4 <= sc->regs_size, (...));
        bus_space_write_4(sc->regs_tag, sc->regs_handle, offset, value);

        if (sc->access_log_enabled) {
                unsigned int idx = sc->access_log_head++ % MYFIRST_ACCESS_LOG_SIZE;
                sc->access_log[idx].timestamp_ns = nanouptime_ns();
                sc->access_log[idx].value = value;
                sc->access_log[idx].offset = offset;
                sc->access_log[idx].is_write = 1;
                sc->access_log[idx].width = 4;
                sc->access_log[idx].context_tag = myfirst_current_context_tag();
        }
}
```

(`nanouptime_ns()` es un pequeño helper que envuelve `nanouptime()` y devuelve un `uint64_t`. `myfirst_current_context_tag()` devuelve un código breve como `'S'` para syscall, `'T'` para tarea, `'C'` para sysctl; su implementación es un switch de pocas líneas sobre la identidad del thread actual.)

El accesor de lectura registra la lectura (el valor es el valor leído). El registro de acceso ocurre bajo el mutex del driver (Stage 3 requiere el mutex para cada acceso a registro), por lo que el buffer circular en sí no necesita locking adicional.

Actívalo con un sysctl:

```c
SYSCTL_ADD_BOOL(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "access_log_enabled",
    CTLFLAG_RW, &sc->access_log_enabled, 0,
    "Record every register access in a ring buffer");
```

Expón el log a través de un manejador de sysctl especial que vuelca el buffer circular:

```c
static int
myfirst_sysctl_access_log(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        struct sbuf *sb;
        int error;
        unsigned int i, start;

        sb = sbuf_new_for_sysctl(NULL, NULL, 256 * MYFIRST_ACCESS_LOG_SIZE, req);
        if (sb == NULL)
                return (ENOMEM);

        MYFIRST_LOCK(sc);
        start = sc->access_log_head;
        for (i = 0; i < MYFIRST_ACCESS_LOG_SIZE; i++) {
                unsigned int idx = (start + i) % MYFIRST_ACCESS_LOG_SIZE;
                struct myfirst_access_log_entry *e = &sc->access_log[idx];
                if (e->timestamp_ns == 0)
                        continue;
                sbuf_printf(sb, "%16ju ns  %s%1d  off=%#04x  val=%#010x  ctx=%c\n",
                    (uintmax_t)e->timestamp_ns,
                    e->is_write ? "W" : "R", e->width,
                    (unsigned)e->offset, e->value, e->context_tag);
        }
        MYFIRST_UNLOCK(sc);

        error = sbuf_finish(sb);
        sbuf_delete(sb);
        return (error);
}
```

El manejador recorre el buffer circular desde la entrada más antigua hasta la más reciente, saltándose los huecos vacíos, y formatea cada entrada como una línea. La salida tiene este aspecto:

```text
  123456789 ns  W4  off=0x00  val=0x00000001  ctx=C
  123567890 ns  R4  off=0x00  val=0x00000001  ctx=C
  124001234 ns  W4  off=0x08  val=0x00000041  ctx=S
  124001567 ns  W4  off=0x04  val=0x00000009  ctx=S
```

Cuatro entradas: una escritura por sysctl modificable en `CTRL`, luego su lectura inmediata de vuelta (ambas con `ctx=C`), y después una escritura de syscall que estableció `DATA_IN` a `0x41` ('A') y actualizó `STATUS` a `0x9` (READY | DATA_AV). La etiqueta de contexto hace evidente el origen.

Para la depuración, este log es invaluable. Ves exactamente lo que hizo el driver, en orden y con marcas de tiempo.

### Kernel Printf: una avalancha controlada

A veces el log no es suficiente y quieres un mensaje impreso por cada acceso a registro, quizá durante una prueba específica que falla. El driver debería tener un control para eso.

Añade un sysctl de nivel de depuración (si no existe ya del `debug_level` del Capítulo 12) y úsalo en los accesores:

```c
#define MYFIRST_DBG_REGS  0x10u

static __inline void
myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset, uint32_t value)
{
        MYFIRST_ASSERT(sc);
        KASSERT(offset + 4 <= sc->regs_size, (...));

        if ((sc->debug_level & MYFIRST_DBG_REGS) != 0)
                device_printf(sc->dev, "W%d reg=%#04x val=%#010x\n",
                    4, (unsigned)offset, value);

        bus_space_write_4(sc->regs_tag, sc->regs_handle, offset, value);

        /* ... access log update ... */
}
```

Cuando `debug_level` tiene el bit `MYFIRST_DBG_REGS` activado, cada escritura de registro se imprime en la consola. Activarlo durante una prueba y borrarlo inmediatamente después proporciona un log enfocado sin inundar el sistema durante toda la vida del driver.

El campo de bits de nivel de depuración es un patrón común en FreeBSD. Muchos drivers reales usan un sysctl de `debug` o `verbose` con bits para diferentes subsistemas: `DBG_PROBE`, `DBG_ATTACH`, `DBG_INTR`, `DBG_REGS`, etc. El usuario activa solo el subconjunto que necesita.

### Sondas de DTrace

DTrace es la herramienta adecuada cuando quieres observar patrones de acceso a registros sin modificar el driver. El proveedor `fbt` (rastreo de fronteras de función) de FreeBSD instrumenta automáticamente cada función no inlineada del kernel. Si `myfirst_reg_read` y `myfirst_reg_write` se compilan como funciones fuera de línea (no inlineadas), DTrace puede engancharse a ellas.

Por defecto, las funciones `static __inline` son candidatas a inlinearse, y las funciones inlineadas no tienen sondas fbt. Para hacer los accesores visibles a DTrace en builds de depuración, divide las declaraciones:

```c
#ifdef MYFIRST_DEBUG_REG_TRACE
static uint32_t myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset);
static void     myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset,
                    uint32_t value);
#else
static __inline uint32_t myfirst_reg_read(struct myfirst_softc *sc,
                             bus_size_t offset);
static __inline void     myfirst_reg_write(struct myfirst_softc *sc,
                             bus_size_t offset, uint32_t value);
#endif
```

Con `MYFIRST_DEBUG_REG_TRACE` definido en tiempo de compilación, los accesores son funciones regulares con sondas de frontera de función. DTrace puede entonces mostrar cada llamada:

```text
# dtrace -n 'fbt::myfirst_reg_write:entry { printf("off=%#x val=%#x", arg1, arg2); }'
```

La salida lista cada escritura de registro con su desplazamiento y valor, en tiempo real, en todo el sistema. DTrace puede agregar, contar, filtrar por pila y combinar con información de proceso de formas que un log artesanal no puede igualar.

Para un build de producción, deja `MYFIRST_DEBUG_REG_TRACE` sin definir; los accesores se inlinean y no tienen coste en tiempo de ejecución. Para un build de depuración, define el macro y obtén visibilidad completa.

### Sondas de DTrace especializadas: `sdt(9)`

Una alternativa más precisa a las sondas fbt es registrar puntos de rastreo definidos estáticamente (SDT) en puntos específicos del driver. La API `sdt(9)` de FreeBSD te permite declarar sondas a las que DTrace puede engancharse por nombre, sin la sobrecarga de un rastreo completo de fronteras de función.

Una sonda para cada escritura de registro:

```c
#include <sys/sdt.h>

SDT_PROVIDER_DEFINE(myfirst);
SDT_PROBE_DEFINE3(myfirst, , , reg_write,
    "struct myfirst_softc *", "bus_size_t", "uint32_t");
SDT_PROBE_DEFINE2(myfirst, , , reg_read,
    "struct myfirst_softc *", "bus_size_t");
```

Y en el accesor:

```c
SDT_PROBE3(myfirst, , , reg_write, sc, offset, value);
```

DTrace recoge la sonda por nombre:

```text
# dtrace -n 'sdt::myfirst:::reg_write { printf("off=%#x val=%#x", arg1, arg2); }'
```

La sonda es visible en DTrace independientemente del inlining, porque el kernel la registra estáticamente. Cuando DTrace no está ejecutando la sonda, es una operación nula en x86 moderno (una única instrucción NOP en la expansión inlineada).

Las sondas SDT son apropiadas para código de producción. Son partes permanentes, nombradas y documentadas de la interfaz del driver. Los usuarios de un driver pueden depender de nombres de sondas SDT específicos para sus propias herramientas de monitorización; eliminarlos rompe esas herramientas.

El Capítulo 16 introduce SDT de forma superficial. Los capítulos posteriores (especialmente el Capítulo 23, sobre depuración y trazado) profundizan en ello.

### El log de latido desde Stage 1 en adelante

Una pieza de instrumentación que el driver ya tiene es el callout de latido del Capítulo 13. Con el estado de registro del Capítulo 16 añadido, el latido se vuelve más informativo si imprime una instantánea de los registros:

```c
static void
myfirst_heartbeat_cb(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);
        if (!myfirst_is_attached(sc))
                return;

        if (sc->debug_level & MYFIRST_DBG_HEARTBEAT) {
                uint32_t ctrl, status;
                ctrl = sc->regs_buf != NULL ?
                    MYFIRST_REG_READ(sc, MYFIRST_REG_CTRL) : 0;
                status = sc->regs_buf != NULL ?
                    MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) : 0;
                device_printf(sc->dev,
                    "heartbeat: ctrl=%#x status=%#x open=%d writers=%d\n",
                    ctrl, status, sc->open_count,
                    sema_value(&sc->writers_sema));
        }

        /* ... existing heartbeat work (stall detection, etc.) ... */

        callout_reset(&sc->heartbeat_co,
            msecs_to_ticks(sc->heartbeat_interval_ms),
            myfirst_heartbeat_cb, sc);
}
```

Con el bit de latido activado y un intervalo de 1 segundo, el driver registra su estado de registros cada segundo. Durante una prueba que falla, la salida de latido muestra frecuentemente el momento exacto en que el estado se descarriló.

### Uso de `kgdb` en un core dump

Cuando un driver provoca un panic, el kernel genera un core dump. `kgdb` puede leer el dump e inspeccionar el softc. Con el bloque de registros dentro del softc, un único comando puede imprimir los valores actuales de los registros:

```text
(kgdb) print *(struct myfirst_softc *)0xfffff8000a123400
(kgdb) x/16xw ((struct myfirst_softc *)0xfffff8000a123400)->regs_buf
```

El comando `x/16xw` vuelca 16 palabras en hexadecimal en la dirección del buffer de registros. La salida son literalmente los 64 bytes del estado de registros en el momento del panic. Un desarrollador mirando esos bytes puede a menudo detectar el valor incorrecto que llevó al panic.

El motivo por el que esto funciona es la simulación: `regs_buf` es memoria del kernel, visible para kgdb. Los registros de un dispositivo real no serían visibles en un core dump, porque el core dump captura solo la RAM, no el estado del dispositivo. Para dispositivos simulados y descriptores DMA, un core dump es una mina de oro.

### Extensiones de DDB

Para depuración en vivo sin un panic, `ddb` puede extenderse con comandos específicos del driver. El macro `DB_COMMAND` registra un nuevo comando que ddb reconoce en el prompt:

```c
#include <ddb/ddb.h>

DB_COMMAND(myfirst_regs, myfirst_ddb_regs)
{
        struct myfirst_softc *sc;

        /* ... find the softc, e.g., via devclass ... */
        if (sc == NULL || sc->regs_buf == NULL) {
                db_printf("myfirst: no device or no regs\n");
                return;
        }

        db_printf("CTRL    %#010x\n", *(uint32_t *)(sc->regs_buf + 0x00));
        db_printf("STATUS  %#010x\n", *(uint32_t *)(sc->regs_buf + 0x04));
        db_printf("DATA_IN %#010x\n", *(uint32_t *)(sc->regs_buf + 0x08));
        /* ... and so on ... */
}
```

En el prompt `db>` durante una interrupción:

```text
db> myfirst_regs
CTRL    0x00000001
STATUS  0x00000009
DATA_IN 0x0000006f
...
```

Los comandos de ddb son una herramienta de nicho. El Capítulo 16 los introduce para mostrar que existen. Los capítulos posteriores (especialmente el Capítulo 23) los usan más. Por ahora, el log de acceso y DTrace cubren la mayor parte de la depuración diaria.

### Qué hacer cuando una lectura de registro devuelve basura

Una breve guía de campo sobre los errores más comunes que producen un valor de registro «basura», con el diagnóstico a aplicar en cada caso.

**Desplazamiento incorrecto.** El desplazamiento en el código no coincide con el desplazamiento en el datasheet. Diagnóstico: contrasta el desplazamiento con el datasheet y revisa el header en busca de errores de transcripción.

**Ancho incorrecto.** El código lee 32 bits de un registro de 16 bits, o lee 8 bits de un registro de 32 bits. Diagnóstico: verifica el ancho en el datasheet y ajusta la llamada.

**Mapeo virtual ausente.** El recurso se asignó sin `RF_ACTIVE`, o el driver está leyendo desde un puntero guardado que apunta a memoria liberada. Diagnóstico: confirma que se llamó a `bus_alloc_resource_any` con `RF_ACTIVE`; comprueba que `sc->regs_buf != NULL` antes de leer en la ruta de simulación.

**Condición de carrera con otro escritor.** Otro contexto escribió un valor diferente entre la lectura y la observación esperada. Diagnóstico: activa el log de acceso, reproduce el problema e inspecciona el log.

**Efecto secundario de borrado en lectura que el código no esperaba.** La lectura anterior borró los bits que ahora esperas ver. Diagnóstico: comprueba el datasheet en busca de efectos secundarios de lectura; considera si el código de lectura debería cachear el valor.

**Desajuste de atributos de caché.** En plataformas donde esto importa, el mapeo virtual se configuró con atributos de caché incorrectos. Diagnóstico: no suele ser un problema en x86 con `bus_alloc_resource_any`; en otras plataformas, comprueba `pmap_mapdev_attr` y el proveedor del bus. Es poco frecuente en la práctica si usas la ruta estándar de asignación del bus.

**Desajuste de endianness.** En un dispositivo big-endian accedido desde una CPU little-endian sin el tag o accesor stream adecuado, los bytes llegan en orden incorrecto. Diagnóstico: compara el valor con los bytes invertidos; si ahora tiene sentido, necesitas los accesores `_stream_` o un tag sensible al orden de bytes.

Cada diagnóstico apunta a una solución diferente. Tenerlos en mente puede ahorrarte horas.

### Qué hacer cuando una escritura en un registro no tiene efecto

Guía de campo complementaria para las escrituras.

**El registro es de solo lectura o de escritura única.** El datasheet define el registro como de solo lectura, o como escribible únicamente hasta la primera escritura exitosa. Diagnóstico: comprueba la columna de dirección en el datasheet.

**La escritura fue enmascarada.** El registro tiene bits que solo son escribibles bajo condiciones específicas (dispositivo deshabilitado, chip en modo de prueba, un campo específico activo). Diagnóstico: activa la impresión de debug_level; confirma que la escritura se produjo; luego comprueba el estado del dispositivo.

**La escritura fue reordenada.** Se emitió una secuencia que requería barreras sin incluirlas, y el dispositivo vio las escrituras en un orden diferente al que el código pretendía. Diagnóstico: añade llamadas explícitas a `bus_space_barrier` y repite la prueba.

**La escritura fue sobreescrita por un read-modify-write concurrente.** Otro contexto sobrescribió el nuevo valor. Diagnóstico: registro de accesos; auditoría de locking.

**La escritura fue al offset incorrecto.** Un error de transcripción o un error aritmético dirigió la escritura a un registro diferente. Diagnóstico: registro de accesos; comparación con el datasheet.

El solapamiento con el diagnóstico de lecturas es alto: la mayoría de los problemas se reducen a "el código no está haciendo lo que crees", y el registro de accesos es la forma más directa de ver qué está haciendo el código.

### Un laboratorio práctico: el registro de accesos cumple su función

Un pequeño ejercicio que demuestra el valor del registro de accesos. Actívalo, ejercita el driver y vuelca el registro.

```text
# sysctl dev.myfirst.0.access_log_enabled=1
# sysctl dev.myfirst.0.reg_ticker_enabled=1
# echo hello > /dev/myfirst0
# dd if=/dev/myfirst0 bs=1 count=6 of=/dev/null
# sysctl dev.myfirst.0.reg_ticker_enabled=0
# sysctl dev.myfirst.0.access_log_enabled=0
# sysctl dev.myfirst.0.access_log
```

El último sysctl emite varias decenas de líneas: los incrementos de `SCRATCH_A` del ticker, la actualización de `DATA_IN` de la escritura y la operación OR sobre `STATUS`, la actualización de `DATA_OUT` de la lectura y la operación AND sobre `STATUS`, y cada lectura de un registro realizada a través de sysctl por el camino. El registro se lee como una transcripción de la conversación del driver consigo mismo.

Un principiante que ve esta transcripción por primera vez suele tener un momento de revelación: "ah, *eso* es lo que el driver hace por debajo". Ese momento es precisamente el objetivo del ejercicio.

### Cerrando la sección 7

La ruta de acceso a los registros es invisible por defecto. Una pequeña capa de observabilidad la hace visible: un ring buffer de accesos recientes, un campo de bits de debug que controla el printf por acceso, sondas DTrace a través de fbt o sdt, un heartbeat mejorado que registra instantáneas de registros, y acceso a la softc desde ddb o kgdb para inspección post-mortem. Cada herramienta encaja en un caso de uso diferente, y juntas cubren casi cualquier bug a nivel de registro que un driver pueda tener.

La sección 8 consolida todo lo que el capítulo 16 ha añadido en un driver refactorizado, documentado y versionado. La etapa final.



## Sección 8: Refactorización y versionado de tu driver preparado para MMIO

La etapa 3 produjo un driver correcto. La sección 8 produce uno mantenible. Los cambios que realiza la etapa 4 son organizativos: separar el código de acceso al hardware de `myfirst.c` en su propio archivo, envolver los accesos a registros restantes en macros que imitan el idioma `CSR_*` que usan los drivers reales, actualizar `HARDWARE.md` a su forma definitiva, actualizar la versión a `0.9-mmio` y ejecutar la pasada de regresión completa.

Un driver que funciona es valioso. Un driver que funciona *y* resulta fácil de leer para la siguiente persona que lo abre es mucho más valioso. La sección 8 trata sobre ese segundo paso.

### La separación en archivos

Hasta el capítulo 15, el driver `myfirst` vivía en un único archivo C más un header. El capítulo 16 añade unas 200 o 300 líneas de código de acceso al hardware, lo que es suficiente para que quien abra `myfirst.c` se encuentre ahora con una mezcla de "lógica de negocio del driver" y "mecánica de registros de hardware" que compiten por su atención.

La etapa 4 los separa. Crea un nuevo archivo `myfirst_hw.c` junto a `myfirst.c`. Mueve a él:

- Las implementaciones de los accesores (`myfirst_reg_read`, `myfirst_reg_write`, `myfirst_reg_update`, `myfirst_reg_write_barrier`).
- Los helpers de efectos secundarios controlados por registro (`myfirst_ctrl_update`).
- La función de callback de la tarea ticker (`myfirst_reg_ticker_cb`).
- Los helpers de rotación del registro de accesos.
- Los manejadores de sysctl para las vistas de registros (`myfirst_sysctl_reg`, `myfirst_sysctl_reg_write`, `myfirst_sysctl_access_log`).

Mueve a `myfirst_hw.h`:

- Los offsets de registro y las máscaras de bits (ya están ahí).
- Las constantes de valor fijo (ya están ahí).
- Los prototipos de función de la API de acceso al hardware (`myfirst_hw_attach`, `myfirst_hw_detach`, `myfirst_hw_set_ctrl`, `myfirst_hw_add_sysctls`, etc.).
- Una pequeña struct que define el estado del hardware (menos campos en la softc, más agrupación).

El archivo `myfirst.c` restante conserva:

- La declaración de la softc (incluyendo un puntero `struct myfirst_hw *hw` a una sub-estructura para el estado del hardware).
- El ciclo de vida del driver (attach, detach, inicialización del módulo).
- Los manejadores de syscall (open, close, read, write, ioctl, poll, kqfilter).
- Los callbacks de callout (heartbeat, watchdog, tick_source).
- Las tareas no relacionadas con el hardware (selwake, bulk_writer, reset_delayed, recovery).
- Los sysctls no relacionados con el hardware.

Esta separación refleja cómo se organizan los drivers reales con múltiples subsistemas. Un driver de red podría tener `foo.c` para el ciclo de vida principal, `foo_hw.c` para el acceso al hardware, `foo_rx.c` para la ruta de recepción y `foo_tx.c` para la ruta de transmisión. El principio es que cada archivo alberga código de un solo tipo, y las llamadas entre archivos pasan a través de una API con nombre.

### La estructura del estado del hardware

Dentro de `myfirst_hw.h`, agrupa los campos relacionados con el hardware en su propia estructura:

```c
struct myfirst_hw {
        uint8_t                *regs_buf;
        size_t                  regs_size;
        bus_space_tag_t         regs_tag;
        bus_space_handle_t      regs_handle;

        struct task             reg_ticker_task;
        int                     reg_ticker_enabled;

        struct myfirst_access_log_entry access_log[MYFIRST_ACCESS_LOG_SIZE];
        unsigned int            access_log_head;
        bool                    access_log_enabled;
};
```

Añade un puntero a ella en la softc:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct myfirst_hw      *hw;
};
```

Asigna la struct hw en `myfirst_hw_attach`:

```c
int
myfirst_hw_attach(struct myfirst_softc *sc)
{
        struct myfirst_hw *hw;

        hw = malloc(sizeof(*hw), M_MYFIRST, M_WAITOK | M_ZERO);

        hw->regs_size = MYFIRST_REG_SIZE;
        hw->regs_buf = malloc(hw->regs_size, M_MYFIRST, M_WAITOK | M_ZERO);
#if defined(__amd64__) || defined(__i386__)
        hw->regs_tag = X86_BUS_SPACE_MEM;
#else
#error "Chapter 16 simulation supports x86 only"
#endif
        hw->regs_handle = (bus_space_handle_t)(uintptr_t)hw->regs_buf;

        TASK_INIT(&hw->reg_ticker_task, 0, myfirst_hw_ticker_cb, sc);

        /* Initialise fixed registers. */
        bus_space_write_4(hw->regs_tag, hw->regs_handle, MYFIRST_REG_DEVICE_ID,
            MYFIRST_DEVICE_ID_VALUE);
        bus_space_write_4(hw->regs_tag, hw->regs_handle, MYFIRST_REG_FIRMWARE_REV,
            MYFIRST_FW_REV_VALUE);
        bus_space_write_4(hw->regs_tag, hw->regs_handle, MYFIRST_REG_STATUS,
            MYFIRST_STATUS_READY);

        sc->hw = hw;
        return (0);
}
```

Libérala en `myfirst_hw_detach`:

```c
void
myfirst_hw_detach(struct myfirst_softc *sc)
{
        struct myfirst_hw *hw;

        if (sc->hw == NULL)
                return;
        hw = sc->hw;
        sc->hw = NULL;

        taskqueue_drain(sc->tq, &hw->reg_ticker_task);
        if (hw->regs_buf != NULL) {
                free(hw->regs_buf, M_MYFIRST);
                hw->regs_buf = NULL;
        }
        free(hw, M_MYFIRST);
}
```

Las funciones `myfirst_attach` y `myfirst_detach` ahora llaman a `myfirst_hw_attach(sc)` y `myfirst_hw_detach(sc)` en los puntos apropiados de su secuencia. El sub-attach del hardware encaja entre "locks de la softc inicializados" y "cdev registrado"; el sub-detach del hardware encaja entre "tareas vaciadas" y "contexto sysctl liberado".

### Las macros CSR

Envuelve los accesos a registros en macros que siguen el idioma de los drivers reales:

```c
#define CSR_READ_4(sc, off) \
        myfirst_reg_read((sc), (off))
#define CSR_WRITE_4(sc, off, val) \
        myfirst_reg_write((sc), (off), (val))
#define CSR_UPDATE_4(sc, off, clear, set) \
        myfirst_reg_update((sc), (off), (clear), (set))
```

El cuerpo del driver ahora queda así:

```c
/* In myfirst_write: */
CSR_WRITE_4(sc, MYFIRST_REG_DATA_IN, (uint32_t)last_byte);
CSR_UPDATE_4(sc, MYFIRST_REG_STATUS, 0, MYFIRST_STATUS_DATA_AV);

/* In the ticker: */
uint32_t v = CSR_READ_4(sc, MYFIRST_REG_SCRATCH_A);
CSR_WRITE_4(sc, MYFIRST_REG_SCRATCH_A, v + 1);

/* In the heartbeat: */
uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
```

Los puntos de llamada se leen como si estuviesen hablando con el hardware, porque eso es exactamente lo que la abstracción representa. Quien abra el driver por primera vez y lea cualquiera de estas líneas entiende de inmediato lo que ocurre: el driver está leyendo o escribiendo un registro identificado por una constante del header de hardware.

### Traslado de los sysctls

Los sysctls de vista de registros se trasladan a `myfirst_hw_add_sysctls`:

```c
void
myfirst_hw_add_sysctls(struct myfirst_softc *sc)
{
        /* ... SYSCTL_ADD_PROC calls for every register ... */
        /* ... SYSCTL_ADD_BOOL for ticker_enabled ... */
        /* ... SYSCTL_ADD_PROC for access_log ... */
}
```

La función se llama desde `myfirst_attach` en el punto habitual donde se registran los sysctls. El archivo principal ya no necesita preocuparse por qué sysctls existen para el hardware; delega esa responsabilidad.

### `HARDWARE.md` definitivo

Con la separación en archivos y la API estabilizada, `HARDWARE.md` queda definitivo:

```text
# myfirst Hardware Interface

## Version

0.9-mmio.  Chapter 16 complete.

## Register Block

- Size: 64 bytes (MYFIRST_REG_SIZE)
- Access: 32-bit reads and writes on 32-bit-aligned offsets
- Storage: malloc(9), M_WAITOK|M_ZERO, allocated in myfirst_hw_attach,
  freed in myfirst_hw_detach
- bus_space_tag:    X86_BUS_SPACE_MEM (x86 only, simulation shortcut)
- bus_space_handle: pointer to the malloc'd block

## API

All register access goes through:

- CSR_READ_4(sc, offset):           read a 32-bit register
- CSR_WRITE_4(sc, offset, value):   write a 32-bit register
- CSR_UPDATE_4(sc, offset, clear, set): read-modify-write

The driver's main mutex (sc->mtx) must be held for every register
access.  Accessor macros assert this via MYFIRST_ASSERT.

## Register Map

(table as in Section 4 ...)

## Per-Register Owners

(table as in Section 6 ...)

## Observability

- dev.myfirst.0.reg_*:      read each register (sysctl)
- dev.myfirst.0.reg_ctrl_set:  write CTRL (sysctl, Stage 1 demo aid)
- dev.myfirst.0.access_log_enabled: record access ring
- dev.myfirst.0.access_log: dump recorded accesses
- Debug bit MYFIRST_DBG_REGS in debug_level: printf per access

## Architecture Portability

The simulation path uses X86_BUS_SPACE_MEM as the tag and a kernel
virtual address as the handle.  On non-x86 platforms, bus_space_tag_t
is a pointer to a structure and this shortcut does not compile;
Chapter 17 introduces a portable alternative.  Real-hardware
Chapter 18 drivers use rman_get_bustag and rman_get_bushandle on a
resource from bus_alloc_resource_any, which is portable by design.
```

El documento es ahora la única fuente de verdad sobre cómo el driver accede al hardware. Un futuro colaborador lo lee una vez para entender la interfaz y nunca más necesita reconstruirla a partir del código.

### La actualización de versión

En `myfirst.c`:

```c
#define MYFIRST_VERSION "0.9-mmio"
```

La cadena aparece en la salida de `kldstat -v` (a través de `MODULE_VERSION`) y en el `device_printf` en el momento del attach. Actualizarla es un cambio pequeño con un gran valor de señalización: cualquiera que examine el driver en ejecución sabe exactamente qué características del capítulo tiene.

Actualiza el comentario al inicio del archivo para indicar las incorporaciones:

```c
/*
 * myfirst: a beginner-friendly device driver tutorial vehicle.
 *
 * Version 0.9-mmio (Chapter 16): adds a simulated MMIO register
 * block with bus_space(9) access, lock-protected register updates,
 * barrier-aware writes, an access log, and a refactored layout that
 * splits hardware-access code into myfirst_hw.c and myfirst_hw.h.
 *
 * ... (previous version notes preserved) ...
 */
```

El comentario al inicio del archivo es el camino más corto para que alguien nuevo entienda la historia del driver. Mantenerlo actualizado es una pequeña disciplina con una gran recompensa.

### La pasada de regresión final

El capítulo 15 estableció la disciplina de regresión: tras cada actualización de versión, ejecuta el conjunto de pruebas de estrés completo de todos los capítulos anteriores, confirma que WITNESS está silencioso, confirma que INVARIANTS está silencioso y confirma que `kldunload` termina de forma limpia.

Para la etapa 4 eso significa:

- Las pruebas de concurrencia del capítulo 11 (múltiples escritores, múltiples lectores) pasan.
- Las pruebas de bloqueo del capítulo 12 (el lector espera datos, el escritor espera espacio) pasan.
- Las pruebas de callout del capítulo 13 (heartbeat, watchdog, fuente de tick) pasan.
- Las pruebas de tareas del capítulo 14 (selwake, escritor en masa, reset diferido) pasan.
- Las pruebas de coordinación del capítulo 15 (semáforo de escritores, caché de estadísticas, esperas interrumpibles) pasan.
- Las pruebas de registros del capítulo 16 (véanse los laboratorios prácticos más abajo) pasan.
- `kldunload myfirst` termina de forma limpia tras el conjunto completo.

No se omite ninguna prueba. Una regresión en la prueba de cualquier capítulo anterior es un bug, no un problema aplazado. La disciplina es la misma que ha sido a lo largo de toda la Parte 3.

### Ejecución de la etapa final

```text
# cd examples/part-04/ch16-accessing-hardware/stage4-final
# make clean && make
# kldstat | grep myfirst
# kldload ./myfirst.ko
# kldstat -v | grep -i myfirst
# dmesg | tail -5
# sysctl dev.myfirst.0 | head -40
```

La salida de `kldstat -v` debe mostrar `myfirst` en la versión `0.9-mmio`. La cola de `dmesg` debe mostrar el probe y el attach del dispositivo sin errores. La salida de `sysctl` debe listar todos los sysctls desde el capítulo 11 hasta el capítulo 16, incluyendo los sysctls de registros.

Ejecuta el conjunto de pruebas de estrés:

```text
# ../labs/full_regression.sh
```

Si todas las pruebas pasan, el capítulo 16 está completo.

### Una pequeña regla para la refactorización del capítulo 16

Una regla general que encarna la etapa 4: cuando un módulo adquiere una nueva responsabilidad, dale su propio archivo antes de que la responsabilidad crezca lo suficiente como para necesitarlo. El capítulo 16 añade el acceso a registros como nueva responsabilidad. La responsabilidad es actualmente pequeña: entre 200 y 300 líneas en todo el código. Separarla en `myfirst_hw.c` ahora, mientras es pequeña, tiene un coste mínimo. Hacerlo más adelante, cuando el capítulo 18 añada lógica de attach PCI, el capítulo 19 añada un manejador de interrupciones y el capítulo 20 añada DMA, requeriría desenredar tres subsistemas entrelazados a la vez, lo que es costoso.

La misma regla se aplicó al cbuf del capítulo 10: el ring buffer obtuvo su propio `cbuf.c` en cuanto tuvo cualquier lógica más allá de "declarar una struct", lo que dio sus frutos cuando la concurrencia y las máquinas de estados entraron en escena. Se aplica a cada futuro subsistema que este driver incorpore.

### Lo que consiguió la etapa 4

El driver está ahora en `0.9-mmio`. Comparado con `0.9-coordination`, tiene:

- Una capa de acceso al hardware separada en `myfirst_hw.c` y `myfirst_hw.h`.
- Un mapa de registros completo, documentado en `myfirst_hw.h` y `HARDWARE.md`.
- Accesores de registro basados en `bus_space(9)` envueltos en macros `CSR_*`.
- Acceso a registros protegido por lock y consciente de las barreras.
- Un registro de accesos para depuración a posteriori.
- Propiedad de contexto por registro documentada en `HARDWARE.md`.
- Una tarea ticker que demuestra el comportamiento autónomo de los registros.
- Una ruta de extremo a extremo desde la escritura en espacio de usuario hasta la actualización del registro y la lectura en espacio de usuario.

El código del driver es inconfundiblemente FreeBSD. La estructura es la que usan los drivers reales. El vocabulario es el que comparten los drivers reales. Quien abra el driver por primera vez encontrará una estructura familiar, leerá los headers para entender los registros y podrá navegar por el código por subsistema.

### Cerrando la sección 8

La refactorización es pequeña en código pero grande en organización. Una separación en archivos, una agrupación de estructuras, una capa de macros, una interfaz documentada, una actualización de versión y una pasada de regresión. Cada una es unos minutos de trabajo. Juntas convierten un driver correcto en uno mantenible.

El driver del capítulo 16 está terminado. El capítulo cierra con laboratorios, desafíos, resolución de problemas y un puente hacia el capítulo 17, donde el dispositivo simulado adquiere comportamiento dinámico.



## Laboratorios prácticos

Los laboratorios del capítulo 16 se centran en dos cosas: observar el acceso a los registros mientras se ejercita el driver, y romper el contrato de registro para ver cómo reacciona el driver. Cada laboratorio lleva entre 15 y 45 minutos.

### Laboratorio 1: observa el baile de registros

Activa el registro de accesos. Ejercita el driver a través de toda su interfaz. Vuelca el registro. Lee la transcripción.

```text
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.access_log_enabled=1

# echo -n "hello" > /dev/myfirst0
# dd if=/dev/myfirst0 bs=1 count=5 of=/dev/null 2>/dev/null
# sysctl dev.myfirst.0.reg_ctrl_set=1
# sysctl dev.myfirst.0.reg_ticker_enabled=1
# sleep 2
# sysctl dev.myfirst.0.reg_ticker_enabled=0

# sysctl dev.myfirst.0.access_log
```

Deberías ver, en orden:

- Cinco escrituras en `DATA_IN` (una por cada byte de "hello").
- Actualizaciones de `STATUS` que activan el bit `DATA_AV`.
- Cinco escrituras en `DATA_OUT` (una por cada byte leído).
- Actualizaciones de `STATUS` que desactivan el bit `DATA_AV` conforme el buffer se vacía.
- La escritura en `CTRL` iniciada por sysctl para activar, más la lectura de verificación.
- Dos incrementos de `SCRATCH_A` procedentes del ticker.

Lee cada línea. Cada valor debería tener sentido. Si algún valor no lo tiene, el driver tiene un bug, la prueba contiene una errata o tu comprensión del protocolo de registro tiene una laguna.

### Laboratorio 2: Provocar una violación de lock (kernel de depuración)

Este laboratorio solo funciona con un kernel compilado con `WITNESS` habilitado. Si no estás ejecutando uno, omite este laboratorio.

Elimina temporalmente el `MYFIRST_LOCK(sc)` del manejador de lectura del sysctl. Recompila y recarga el driver. Ejecuta:

```text
# sysctl dev.myfirst.0.reg_ctrl
```

La consola debería emitir una advertencia de `WITNESS` sobre un acceso no protegido al registro (a través de `MYFIRST_ASSERT` en `myfirst_reg_read`). La salida del sysctl puede seguir devolviendo un valor plausible, porque la falta de locking no siempre produce resultados incorrectos, pero la aserción hace visible la violación.

Restaura el lock. Recompila. Verifica que la advertencia haya desaparecido.

Este laboratorio demuestra el valor de `MYFIRST_ASSERT` como red de seguridad. Un driver de producción sin la aserción cargaría el bug en silencio hasta que algo saliera mal.

### Laboratorio 3: Simular una condición de carrera entre escritores concurrentes

Dos procesos escribiendo simultáneamente en `/dev/myfirst0` ejercitan el código de actualización del registro dos veces a la vez. Ejecuta:

```text
# for i in 1 2 3 4; do
    (for j in $(seq 1 100); do echo -n "$i"; done > /dev/myfirst0) &
done
# wait

# sysctl dev.myfirst.0.reg_data_in
# sysctl dev.myfirst.0.reg_status
```

El registro `DATA_IN` debería contener el código ASCII del escritor que ejecutó en último lugar (`'1'` = 49, `'2'` = 50, `'3'` = 51, `'4'` = 52). El resultado es no determinista, que es precisamente la cuestión: el estado del registro con escritores concurrentes refleja al último ganador.

Con el locking de la Etapa 3, el driver es correcto (sin actualizaciones perdidas, sin lecturas parciales). Sin el locking de la Etapa 3 (intenta revertir a la Etapa 2 y volver a ejecutar), podrías observar inconsistencias o advertencias de WITNESS.

### Laboratorio 4: Observar el log de registros del heartbeat

Habilita el bit de depuración del heartbeat, aumenta el intervalo y déjalo correr.

```text
# sysctl dev.myfirst.0.debug_level=0x8     # MYFIRST_DBG_HEARTBEAT
# sysctl dev.myfirst.0.heartbeat_interval_ms=1000
# sysctl dev.myfirst.0.reg_ticker_enabled=1
# sleep 5
# dmesg | tail -10

# sysctl dev.myfirst.0.reg_ticker_enabled=0
# sysctl dev.myfirst.0.debug_level=0
# sysctl dev.myfirst.0.heartbeat_interval_ms=0
```

La cola del dmesg debería contener cinco líneas, una por heartbeat, cada una mostrando los valores actuales de los registros. `SCRATCH_A` debería incrementarse en uno por cada heartbeat porque el ticker está ejecutándose en paralelo.

Este laboratorio demuestra cómo un driver de producción podría usar un log de depuración para observar el comportamiento en vivo sin interrumpir el funcionamiento normal del driver.

### Laboratorio 5: Añadir un nuevo registro

Un ejercicio práctico. Añade un nuevo registro `SCRATCH_C` en el desplazamiento `0x28`. Extiende el header, extiende la lista de sysctls y actualiza `HARDWARE.md`. Recompila, recarga y verifica que el nuevo registro sea legible y escribible mediante sysctl.

Esto ejercita el flujo de trabajo completo para añadir un registro: cambio en el header, incorporación del sysctl, actualización de la documentación y prueba. Un driver que hace sencillos los cuatro pasos es un driver bien organizado.

### Laboratorio 6: Inyectar un acceso inválido (kernel de depuración)

Un ejercicio deliberado de romper y observar.

Modifica el callback del ticker para leer desde un desplazamiento fuera de rango: `MYFIRST_REG_READ(sc, 0x80)`. Recompila. Habilita el ticker. En un kernel de depuración con INVARIANTS, el KASSERT en `myfirst_reg_read` debería provocar un panic del kernel en pocos segundos, con la cadena del panic indicando el desplazamiento.

Restaura el callback. Recompila. Verifica que el driver funcione correctamente de nuevo.

Este laboratorio muestra el valor de las aserciones de rango: un acceso fuera de rango se detecta de inmediato en lugar de corromper silenciosamente la memoria adyacente. El código de producción nunca debería eliminar estas aserciones; amortizan su coste muchas veces a lo largo de la vida del driver.

### Laboratorio 7: Trazar con DTrace

Compila el driver con `CFLAGS+=-DMYFIRST_DEBUG_REG_TRACE` para que los accesores no sean inlineados. Recompila y recarga.

Ejecuta DTrace:

```text
# dtrace -n 'fbt::myfirst_reg_write:entry {
    printf("off=%#x val=%#x", arg1, arg2);
}'
```

En otro terminal:

```text
# echo hi > /dev/myfirst0
```

DTrace debería imprimir dos líneas, una por cada escritura de registro (`DATA_IN` y la actualización de `STATUS`).

Prueba consultas más avanzadas:

```text
# dtrace -n 'fbt::myfirst_reg_write:entry /arg1 == 0/ { @ = count(); }'
```

Cuenta las escrituras en `CTRL` (desplazamiento 0) durante el tiempo de ejecución. Déjalo correr mientras provocas varias operaciones y luego pulsa Ctrl-C para ver el total.

El poder de DTrace proviene de la combinación de bajo overhead, filtrado flexible y agregación rica. Un principiante que se familiarice con él desde el principio ahorrará horas en cada sesión de depuración posterior.

### Laboratorio 8: El escenario donde el watchdog se encuentra con los registros

El callout del watchdog del Capítulo 13 se introdujo para detectar bloqueos en el buffer circular. La integración de registros del Capítulo 16 añade un segundo modo de fallo: el watchdog podría detectar un registro en un estado imposible. Extiende el callback del watchdog para que se queje si `STATUS.ERROR` está activado:

```c
if (MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_ERROR) {
        device_printf(sc->dev, "watchdog: STATUS.ERROR is set\n");
}
```

Activa el bit de error desde un manejador de sysctl:

```text
# sysctl dev.myfirst.0.reg_ctrl_set=??  # use your writeable-register sysctl
```

(También puedes crear un sysctl escribible para `STATUS` que provoque la comprobación del watchdog.)

En el siguiente tick del watchdog (por defecto, 5 segundos), debería aparecer el mensaje. Limpia el bit; el mensaje debería desaparecer.

Este laboratorio integra el camino de los registros con el camino de monitorización basado en callout, mostrando cómo se combinan ambos subsistemas.



## Ejercicios de desafío

Los desafíos van más allá de los laboratorios. Cada uno debería requerir entre una y cuatro horas y pone a prueba el criterio, no solo la escritura de código.

### Desafío 1: Instantánea de registros por descriptor de archivo

Cada descriptor de archivo abierto obtiene su propia instantánea del bloque de registros, capturada en el momento de la apertura y accesible mediante un ioctl personalizado. Modifica `myfirst_open` para guardar la instantánea de los registros en una estructura por descriptor; implementa un ioctl que devuelva la instantánea; escribe un programa en espacio de usuario que abra `/dev/myfirst0`, obtenga la instantánea y la imprima.

Piensa en: ¿cuánta memoria consume la instantánea por apertura? ¿Cuándo debería actualizarse la instantánea? ¿Habría que añadir un segundo ioctl para actualizarla?

### Desafío 2: Registro diferencial de registros

Extiende el log de acceso para registrar únicamente los *cambios* en los registros (cuando el nuevo valor difiere del valor anterior en el mismo desplazamiento). Las escrituras que no cambian el valor no se registran. Esto comprime el log considerablemente y lo centra en las transiciones de estado significativas.

Piensa en: ¿cómo haces un seguimiento del "valor anterior"? ¿Es por desplazamiento o lo almacenas junto a cada entrada del log?

### Desafío 3: Modo loopback

Añade un bit `CTRL.LOOPBACK` (ya definido en `myfirst_hw.h`). Cuando el bit está activado, las escrituras en `DATA_IN` también se copian en `DATA_OUT`, haciendo que el driver devuelva las escrituras del espacio de usuario sin necesidad de una lectura. Implementa la lógica, añade una prueba en el laboratorio y confirma que las lecturas del espacio de usuario devuelven los bytes recién escritos.

Piensa en: ¿en qué punto del camino de escritura debe hacerse la copia? ¿Sigue siendo correcto si se escriben varios bytes en una sola llamada? ¿Activas `DATA_AV` de forma diferente en modo loopback?

### Desafío 4: Lectura-para-limpiar en `INTR_STATUS`

La simulación del Capítulo 16 tiene `INTR_STATUS` como un registro ordinario. El hardware real suele usar semántica de lectura-para-limpiar. Impleméntala: haz que la lectura del sysctl de `reg_intr_status` devuelva el valor actual y lo limpie a continuación, de modo que la siguiente lectura devuelva cero. Añade una forma de activar bits pendientes (un sysctl escribible que aplique un OR al registro).

Piensa en: ¿es el comportamiento de lectura-para-limpiar seguro para el sysctl de depuración? ¿Cómo manejas el caso en que el sysctl se usa para observar el valor?

### Desafío 5: Una prueba de estrés de corrección de barreras

Escribe un arnés de estrés que ejercite un patrón específico: escribir en `CTRL`, emitir una barrera, leer `STATUS` y verificar que la lectura refleje la escritura. Ejecútalo miles de veces y mide con qué frecuencia falla la verificación. En x86 con barreras correctas, los fallos deberían ser cero.

Después elimina la barrera y vuelve a ejecutarlo. En x86, los fallos podrían seguir siendo cero (modelo de memoria fuerte). En arm64 (si tienes acceso), eliminar la barrera puede producir fallos.

Piensa en: ¿qué te dice este ejercicio sobre el coste y el valor de las barreras en diferentes arquitecturas? ¿Debería un driver incluirlas siempre?

### Desafío 6: Una ejecución de lockstat con conciencia de registros

Usa `lockstat` para perfilar tu driver de la Etapa 3 bajo carga. Identifica los locks más activos. ¿Está saturado el mutex del driver (`sc->mtx`) por los accesos a registros, por el camino del buffer circular o por ninguno de los dos? Genera un informe e interpreta los números.

Piensa en: ¿cambia el resultado si separas un `sc->reg_mtx` dedicado para el acceso a registros? ¿Aparecen advertencias de WITNESS? ¿Es el driver más rápido o más lento?

### Desafío 7: Leer la interfaz de registros de un driver real

Elige un driver real en `/usr/src/sys/dev/` y lee su header de registros. Algunos candidatos son `/usr/src/sys/dev/ale/if_alereg.h`, `/usr/src/sys/dev/e1000/e1000_regs.h` y `/usr/src/sys/dev/uart/uart_dev_ns8250.h`. Responde:

- ¿Cuántos registros define el driver?
- ¿Qué anchura tienen (8, 16, 32, 64 bits)?
- ¿Qué registros tienen macros de campos de bits? ¿Hay alguna macro de campo de bits que corresponda a campos que abarquen varios bytes?
- ¿Cómo envuelve el driver `bus_read_*` y `bus_write_*` (si es que lo hace)?
- ¿Cómo están documentados los desplazamientos de los registros (comentarios, referencias a especificaciones externas)?

Redactar las respuestas como un análisis de una página es una excelente forma de consolidar el material del Capítulo 16. Probablemente también encontrarás patrones que querrás aplicar a tu propio driver.



## Referencia de solución de problemas

Una referencia rápida para los problemas que es más probable que surjan con el código del Capítulo 16.

### El driver falla al cargar

- **"resolve_symbol failed"**: Falta un include o hay una errata en el nombre de una función. Comprueba `/var/log/messages` para ver el símbolo exacto; añade el include; vuelve a intentarlo.
- **"undefined reference to bus_space_read_4"**: Falta `#include <machine/bus.h>`. Este archivo incorpora el header de bus específico de la arquitectura.
- **"invalid KMOD Makefile"**: Una errata en el Makefile. Compara con el Makefile correcto de la etapa correspondiente.

### El driver carga pero `dmesg` muestra `myfirst: cannot allocate register block`

`malloc(9)` devolvió NULL en attach. Normalmente significa que se pasó `M_WAITOK` pero el sistema estaba bajo presión de memoria; algo poco frecuente para una asignación de 64 bytes. Comprueba las estadísticas de malloc del kernel (`vmstat -m`) para `myfirst`. Intenta reiniciar.

### `sysctl dev.myfirst.0.reg_ctrl` devuelve ENOENT

El sysctl no fue registrado. Confirma que `myfirst_hw_add_sysctls` se llama en attach. Confirma que el contexto y el árbol del sysctl son los mismos que los del resto de los sysctls del driver. Busca una errata en `OID_AUTO` o en el nombre de la hoja.

### `sysctl dev.myfirst.0.reg_ctrl` devuelve un valor plausible, pero los cambios no se producen

El sysctl escribible `reg_ctrl_set` podría carecer de `CTLFLAG_RW`. Sin `_RW`, el sysctl es de solo lectura. Comprueba también que el manejador no esté cortocircuitando por un retorno anticipado de ENODEV.

### Panic del kernel en la primera escritura de registro: "page fault in kernel mode"

`sc->regs_buf` es NULL o apunta a memoria liberada. Confirma que `myfirst_hw_attach` se ejecutó correctamente y estableció `sc->hw->regs_buf`. Confirma que nada liberó el buffer de forma prematura (detach ejecutándose en paralelo, o un `free` en un camino de error).

### Panic del kernel: "myfirst: register read past end of register block"

El KASSERT se disparó. Un bug en el driver está pasando un desplazamiento fuera de rango. Usa la pila del crash para encontrar el punto de llamada. Causa habitual: una expresión aritmética para el desplazamiento que supera `MYFIRST_REG_SIZE`.

### Advertencia de `WITNESS`: "acquiring duplicate lock"

Generalmente es señal de que una cadena de llamadas está adquiriendo `sc->mtx` de forma recursiva. El mutex del Capítulo 11 es un sleep mutex sin el flag `MTX_RECURSE`, lo cual es correcto. Traza la pila; uno de los llamadores está volviendo a entrar.

### Advertencia de `WITNESS`: "lock order reversal"

El driver mantiene `sc->mtx` mientras adquiere otro lock (o viceversa) en un orden que viola el orden documentado. Compara `LOCKING.md` con la traza de la pila y corrige el punto de llamada.

### El ticker no se dispara

El intervalo del callout de la fuente de tick es cero (deshabilitado). Confirma que `dev.myfirst.0.tick_source_interval_ms` es positivo. Confirma que `reg_ticker_enabled` es 1. Mira el log de acceso para ver escrituras en SCRATCH_A; si no hay ninguna, el callout es el problema. Si las hay, el sysctl podría estar desactualizado (vuelve a leerlo).

### El log de acceso devuelve vacío

Confirma que `access_log_enabled` es 1. Confirma que el mutex del driver se está adquiriendo en los caminos de acceso (la actualización del log se produce bajo el lock). Si el log está genuinamente vacío pero debería haberse accedido a los registros, comprueba si faltan llamadas a los accesores en los caminos de acceso.

### `dmesg` no muestra salida de `myfirst_ctrl_update`

`debug_level` es 0 o el bit específico no está activado. Establece `debug_level` para incluir los bits correctos y vuelve a intentarlo.

### `kldunload myfirst` devuelve EBUSY

Todavía existen descriptores de archivo abiertos en el dispositivo. Ciérralos (o usa `fstat -f /dev/myfirst0` para encontrar quién los tiene) y vuelve a intentarlo.

### `kldunload myfirst` se bloquea

El detach se ha quedado atascado vaciando una primitiva. Usa `procstat -kk <pid-of-kldunload>` para ver en qué punto. Normalmente el vaciado atascado corresponde a una tarea o callout que no se está cancelando. Comprueba el orden del detach frente a `LOCKING.md`.

### La prueba de estrés informa de avisos de WITNESS

Cada aviso señala un error real. Corrige uno, vuelve a probar, continúa. No desactives WITNESS de forma masiva y des por resuelto el problema; los avisos están apuntando al fallo.

## Cerrando

El capítulo 16 abrió la Parte 4 dándole al driver `myfirst` su primera historia de hardware. El driver tiene ahora un bloque de registros, aunque ese bloque sea simulado. Utiliza `bus_space(9)` de la misma manera que lo haría un driver real. Protege el acceso a los registros con el mutex del capítulo 11, inserta barreras donde importa el orden y documenta cada registro y cada ruta de acceso. Cuenta con un registro de accesos para la depuración post-hoc, sondas DTrace para la observación en tiempo real y un comando ddb para la inspección en vivo. Está organizado en dos archivos: el ciclo de vida principal del driver y la capa de acceso al hardware.

Lo que el capítulo 16 ha omitido deliberadamente: PCI real (capítulo 18), interrupciones reales (capítulo 19), DMA real (capítulos 20 y 21), simulación completa de hardware con comportamiento dinámico (capítulo 17). Cada uno de esos temas merece su propio capítulo; todos ellos se apoyan en el vocabulario del capítulo 16.

La versión es `0.9-mmio`. La disposición de archivos es `myfirst.c` más `myfirst_hw.c` más `myfirst_hw.h` más `myfirst_sync.h` más `cbuf.c` más `cbuf.h`. La documentación comprende `LOCKING.md` y el nuevo `HARDWARE.md`. La suite de pruebas ha crecido con los laboratorios del capítulo 16. Todas las pruebas anteriores de la Parte 3 siguen pasando.

### Una reflexión antes del capítulo 17

Una pausa antes del siguiente capítulo. El capítulo 16 fue una introducción cuidadosa a un conjunto de ideas que se repetirán durante el resto de la Parte 4 y más allá. La lectura y escritura de registros, la barrera, el bus tag y el handle, la disciplina de locking en torno a MMIO: estas son las piezas que todo capítulo posterior orientado al hardware utiliza sin volver a explicarlas. Las has conocido todas una vez, en un entorno donde podías observar, experimentar y cometer errores con seguridad.

El mismo patrón que definió la Parte 3 define la Parte 4: introducir una primitiva, aplicarla al driver en una pequeña refactorización, documentarla, probarla, avanzar. La diferencia es que las primitivas de la Parte 4 miran hacia afuera. El driver ya no es un mundo autocontenido; es un participante en una conversación con el hardware. Esa conversación tiene reglas que el driver debe respetar, y las reglas tienen consecuencias cuando se incumplen.

El capítulo 17 hace que el lado del hardware de la conversación sea más interesante. El dispositivo simulado ganará un callout que cambia los bits de `STATUS` con el tiempo. Señalará «datos disponibles» tras una escritura al activar un bit con cierto retraso. Fallará de vez en cuando para enseñar las rutas de manejo de errores. El vocabulario de registros permanece igual; el comportamiento del dispositivo se vuelve más rico.

### Qué hacer si estás atascado

Si el material del capítulo 16 te resulta abrumador en una primera lectura, aquí van algunas sugerencias.

Primero, vuelve a leer la sección 3. El vocabulario de `bus_space` es la base; si es inestable, todo lo demás también lo es.

Segundo, escribe la Etapa 1 a mano, de principio a fin, y ejecútala. La memoria muscular genera comprensión de una manera que la lectura no logra.

Tercero, abre `/usr/src/sys/dev/ale/if_alevar.h` y busca las macros `CSR_*`. El idioma del driver real es el mismo que el de tu Etapa 4. Ver el patrón en un driver de producción hace que la abstracción parezca menos arbitraria.

Cuarto, omite los desafíos en la primera lectura. Los laboratorios están calibrados para el capítulo 16; los desafíos dan por sentado que el material del capítulo ya está bien asimilado. Vuelve a ellos después del capítulo 17 si ahora te parecen inalcanzables.

El objetivo del capítulo 16 era la claridad del vocabulario. Si la tienes, el resto de la Parte 4 te parecerá navegable.



## Puente hacia el capítulo 17

El capítulo 17 se titula *Simulando hardware*. Su alcance es la simulación más profunda que el capítulo 16 ha evitado deliberadamente: un bloque de registros cuyos contenidos cambian con el tiempo, cuyo protocolo tiene efectos secundarios y cuyos fallos pueden inyectarse deliberadamente para hacer pruebas. El driver en `0.9-mmio` tiene un bloque de registros que se comporta de forma estática; el capítulo 17 le da vida.

El capítulo 16 preparó el terreno de cuatro maneras concretas.

Primero, **tienes un mapa de registros**. Los desplazamientos, las máscaras de bits y la semántica de los registros están documentados en `myfirst_hw.h` y en `HARDWARE.md`. El capítulo 17 amplía el mapa con algunos registros nuevos y enriquece el protocolo de los existentes; la estructura ya está establecida.

Segundo, **tienes accesores protegidos por lock y conscientes de las barreras**. El capítulo 17 introduce un callout que actualiza registros periódicamente desde su propio contexto. Sin la disciplina de locking del capítulo 16, el callout entraría en condición de carrera con la ruta del syscall. Con ella, el callout encaja en el mutex existente sin trabajo adicional.

Tercero, **tienes un registro de accesos**. El capítulo 17 usa actualizaciones de registros más elaboradas (lectura que borra `INTR_STATUS`, `DATA_AV` diferido activado por escritura, errores simulados). El registro de accesos es la herramienta con la que verás esas actualizaciones en acción, y el capítulo 17 lo utiliza extensamente.

Cuarto, **tienes una disposición de archivos dividida**. La lógica de simulación del capítulo 17 se aloja en `myfirst_hw.c`, junto a los accesores del capítulo 16. El archivo principal del driver permanece centrado en el ciclo de vida del driver. La división mantiene el código de simulación contenido.

Temas específicos que cubrirá el capítulo 17:

- Un callout que actualiza los bits de `STATUS` según un horario, simulando actividad autónoma del dispositivo.
- Un patrón de escritura que desencadena un evento diferido: escribir `CTRL.GO` programa un callout que, tras un retraso, activa `STATUS.DATA_AV`.
- Semántica de lectura que borra en `INTR_STATUS`, con el sysctl del driver tomando precauciones para no borrar bits inadvertidamente.
- Inyección de errores simulados: un sysctl que hace que la siguiente operación «falle» con un bit de fallo activado en `STATUS`.
- Timeouts: el driver reacciona correctamente cuando el dispositivo simulado no llega a estar listo.
- Una ruta de simulación de latencia con `DELAY(9)` y `callout_reset_sbt` para distintas granularidades.

No necesitas adelantarte. El capítulo 16 es preparación suficiente. Trae tu driver `myfirst` en `0.9-mmio`, tu `LOCKING.md`, tu `HARDWARE.md`, tu kernel con WITNESS habilitado y tu kit de pruebas. El capítulo 17 empieza donde el capítulo 16 terminó.

Una pequeña reflexión de cierre. La Parte 3 te enseñó el vocabulario de sincronización y un driver que se coordinaba consigo mismo. El capítulo 16 añadió un vocabulario de registros y un driver que ahora tiene una superficie de hardware. El capítulo 17 dará a esa superficie un comportamiento dinámico; el capítulo 18 sustituirá la simulación por hardware PCI real; el capítulo 19 añadirá interrupciones; los capítulos 20 y 21 añadirán DMA. Cada uno de esos capítulos es más estrecho de lo que su tema sugiere porque el capítulo 16 hizo primero el trabajo de vocabulario.

La conversación con el hardware ha comenzado. El vocabulario es tuyo. El capítulo 17 abre la siguiente ronda.



## Referencia: hoja de consulta rápida de `bus_space(9)`

Un resumen de una página de la API de `bus_space(9)`, para consulta rápida mientras programas.

### Tipos

| Tipo                 | Significado                                                  |
|----------------------|--------------------------------------------------------------|
| `bus_space_tag_t`    | Identifica el espacio de direcciones (memoria o I/O).        |
| `bus_space_handle_t` | Identifica una región concreta en el espacio de direcciones. |
| `bus_size_t`         | Entero sin signo para desplazamientos dentro de una región.  |

### Lecturas

| Función                         | Ancho | Notas                        |
|---------------------------------|-------|------------------------------|
| `bus_space_read_1(t, h, o)`     | 8     | Devuelve `u_int8_t`          |
| `bus_space_read_2(t, h, o)`     | 16    | Devuelve `u_int16_t`         |
| `bus_space_read_4(t, h, o)`     | 32    | Devuelve `u_int32_t`         |
| `bus_space_read_8(t, h, o)`     | 64    | Solo en memoria amd64        |

### Escrituras

| Función                            | Ancho | Notas                        |
|------------------------------------|-------|------------------------------|
| `bus_space_write_1(t, h, o, v)`    | 8     | `v` es `u_int8_t`            |
| `bus_space_write_2(t, h, o, v)`    | 16    | `v` es `u_int16_t`           |
| `bus_space_write_4(t, h, o, v)`    | 32    | `v` es `u_int32_t`           |
| `bus_space_write_8(t, h, o, v)`    | 64    | Solo en memoria amd64        |

### Accesos múltiples (mismo desplazamiento, distintas posiciones de buffer)

| Función                                          | Finalidad                                   |
|--------------------------------------------------|---------------------------------------------|
| `bus_space_read_multi_1(t, h, o, buf, count)`    | Lee `count` bytes desde `o`.               |
| `bus_space_read_multi_4(t, h, o, buf, count)`    | Lee `count` valores de 32 bits desde `o`.  |
| `bus_space_write_multi_4(t, h, o, buf, count)`   | Escribe `count` valores de 32 bits en `o`. |

### Accesos a región (desplazamiento y buffer avanzan juntos)

| Función                                           | Finalidad                                    |
|---------------------------------------------------|----------------------------------------------|
| `bus_space_read_region_4(t, h, o, buf, count)`    | Lee `count` valores de 32 bits desde `o..`  |
| `bus_space_write_region_4(t, h, o, buf, count)`   | Escribe `count` valores de 32 bits en `o..` |

### Barrera

| Función                                      | Finalidad                                                             |
|----------------------------------------------|-----------------------------------------------------------------------|
| `bus_space_barrier(t, h, o, len, flags)`     | Impone el orden de lectura/escritura sobre el rango de desplazamiento. |

Indicadores:

| Indicador                     | Significado                                                      |
|-------------------------------|------------------------------------------------------------------|
| `BUS_SPACE_BARRIER_READ`      | Las lecturas previas se completan antes que las posteriores.     |
| `BUS_SPACE_BARRIER_WRITE`     | Las escrituras previas se completan antes que las posteriores.   |

### Acceso abreviado por recurso (`/usr/src/sys/sys/bus.h`)

| Función                           | Equivalente                                                          |
|-----------------------------------|----------------------------------------------------------------------|
| `bus_read_4(r, o)`                | `bus_space_read_4(r->r_bustag, r->r_bushandle, o)`                  |
| `bus_write_4(r, o, v)`            | `bus_space_write_4(r->r_bustag, r->r_bushandle, o, v)`              |
| `bus_barrier(r, o, l, f)`         | `bus_space_barrier(r->r_bustag, r->r_bushandle, o, l, f)`           |

### Asignación

| Función                                                              | Finalidad           |
|----------------------------------------------------------------------|---------------------|
| `bus_alloc_resource_any(dev, type, &rid, flags)`                     | Asigna un recurso.  |
| `bus_release_resource(dev, type, rid, res)`                          | Libera un recurso.  |
| `rman_get_bustag(res)`                                               | Extrae la etiqueta. |
| `rman_get_bushandle(res)`                                            | Extrae el handle.   |

### Tipos de recurso

| Constante          | Significado                           |
|--------------------|---------------------------------------|
| `SYS_RES_MEMORY`   | Región de I/O mapeada en memoria.     |
| `SYS_RES_IOPORT`   | Rango de puertos de I/O.              |
| `SYS_RES_IRQ`      | Línea de interrupción.                |

### Indicadores

| Constante      | Significado                                        |
|----------------|----------------------------------------------------|
| `RF_ACTIVE`    | Activa el recurso (establece el mapeo).            |
| `RF_SHAREABLE` | El recurso puede compartirse con otros drivers.    |



## Referencia: lecturas adicionales

### Páginas de manual

- `bus_space(9)`: la referencia completa de la API.
- `bus_dma(9)`: la API de DMA (referencia del capítulo 20).
- `bus_alloc_resource(9)`: referencia de asignación de recursos.
- `rman(9)`: el gestor de recursos subyacente.
- `pci(9)`: visión general del subsistema PCI (introducción al capítulo 18).
- `device(9)`: la API de identidad de dispositivo.
- `memguard(9)`: depuración de memoria del kernel.

### Archivos fuente

- `/usr/src/sys/sys/bus.h`: las macros abreviadas del bus y la API de recursos.
- `/usr/src/sys/x86/include/bus.h`: la implementación de `bus_space` para x86.
- `/usr/src/sys/arm64/include/bus.h`: el equivalente para arm64 (a modo de comparación).
- `/usr/src/sys/dev/ale/if_alevar.h`: un ejemplo limpio de macros `CSR_*`.
- `/usr/src/sys/dev/e1000/if_em.c`: el flujo de asignación de un driver en producción.
- `/usr/src/sys/dev/uart/uart_dev_ns8250.c`: un driver con uso intensivo de registros para un dispositivo clásico.
- `/usr/src/sys/dev/led/led.c`: un driver de pseudodispositivo sin hardware real.

### Orden de lectura

Si quieres profundizar antes del Capítulo 17, lee en este orden:

1. `/usr/src/sys/sys/bus.h`, el bloque de macros abreviadas `bus_read_*` / `bus_write_*` (busca `#define bus_read_1`).
2. `/usr/src/sys/x86/include/bus.h` completo (la implementación).
3. `/usr/src/sys/dev/ale/if_alevar.h` completo (el softc, macros, constantes).
4. La ruta de attach en `/usr/src/sys/dev/ale/if_ale.c` (busca `bus_alloc_resource`).
5. La ruta de attach en `/usr/src/sys/dev/e1000/if_em.c` (la misma búsqueda).

Cada lectura lleva entre 15 y 45 minutos. El efecto acumulativo es un sólido dominio de los patrones que introdujo el Capítulo 16.



## Referencia: Glosario de términos del Capítulo 16

### Términos introducidos en este capítulo

**access log**: Un ring buffer de accesos recientes a registros, guardado en el softc para depuración.

**alignment** (alineación): El requisito de que el desplazamiento de un acceso a registro sea múltiplo del ancho del acceso.

**barrier** (barrera): Una función o instrucción que impone un orden entre los accesos a memoria anteriores y posteriores.

**BAR (Base Address Register)**: En PCI, un registro del dispositivo que anuncia la dirección física de su región MMIO. El Capítulo 18 trata los BARs directamente.

**bus_space_handle_t**: Un identificador opaco de una región específica dentro de un espacio de direcciones de bus.

**bus_space_tag_t**: Un identificador opaco de un espacio de direcciones de bus (normalmente memoria o puerto I/O).

**macro CSR**: Una macro envolvente específica del driver (p. ej., `CSR_READ_4`) que abstrae el acceso a registros detrás de un nombre corto.

**endianness** (orden de bytes): El orden de bytes en que se distribuye un registro multibyte. Little-endian coloca el byte menos significativo primero; big-endian, el más significativo.

**field** (campo): Un subrango de bits de un registro, con su propio nombre y significado.

**registro de revisión de firmware**: Un registro de solo lectura que informa de la versión del firmware del dispositivo.

**puerto I/O**: Un espacio de direcciones específico de x86, al que se accede con las instrucciones `in` y `out`. Contrasta con MMIO.

**MMIO (memory-mapped I/O)**: El mecanismo por el que los registros del dispositivo se exponen como un rango de direcciones físicas, accesibles mediante instrucciones de carga y almacenamiento ordinarias.

**offset** (desplazamiento): La distancia, en bytes, desde el inicio de la región de un dispositivo hasta un registro específico.

**PIO (port-mapped I/O)**: En x86, la alternativa al MMIO, que utiliza instrucciones de puerto I/O separadas.

**region** (región): Un rango contiguo del espacio de direcciones del dispositivo, o la familia de API que recorre sus desplazamientos.

**registro**: Una unidad de comunicación entre el driver y un dispositivo, identificada por nombre, desplazamiento y ancho.

**mapa de registros**: Una tabla que describe todos los registros de la interfaz de un dispositivo: desplazamiento, ancho, dirección y significado.

**resource (recurso en FreeBSD)**: Una asignación con nombre procedente del subsistema de bus, que encapsula un tag, un handle y la propiedad de un rango específico.

**sbuf**: La API de kernel `sbuf(9)` para construir cadenas de longitud variable, utilizada por el manejador sysctl del access log.

**efecto secundario (de registro)**: Un cambio en el estado del dispositivo que una lectura o escritura provoca como parte de su semántica, más allá de devolver o almacenar el valor.

**simulación**: En el Capítulo 16, el uso de memoria del kernel asignada con `malloc(9)` como sustituto de la región MMIO de un dispositivo.

**stream accessor**: Una variante `bus_space_*_stream_*` que no aplica conversiones de orden de bytes.

**asignación virtual**: La traducción que realiza la MMU de una dirección virtual a una dirección física, con atributos específicos de caché y acceso.

**ancho (width)**: El número de bits de un registro o del operando de una función de acceso (8, 16, 32, 64).

### Términos introducidos anteriormente (recordatorio)

- **softc**: La estructura de estado del driver por instancia (Capítulo 6).
- **device_t**: La identidad del kernel para una instancia de dispositivo (Capítulo 6).
- **malloc(9)**: El asignador del kernel (Capítulo 5).
- **WITNESS**: El comprobador de orden de locks del kernel (Capítulo 11).
- **INVARIANTS**: El marco de aserciones defensivas del kernel (Capítulo 11).
- **callout**: Una primitiva de temporizador que invoca un callback tras un retardo (Capítulo 13).
- **taskqueue**: Una primitiva de trabajo diferido (Capítulo 14).
- **cv_wait / cv_timedwait_sig**: Esperas sobre variables de condición (Capítulo 12, Capítulo 15).



## Referencia: Resumen de cambios del driver en el Capítulo 16

Una vista compacta de lo que el Capítulo 16 añadió al driver `myfirst`, etapa por etapa, para los lectores que quieran ver el conjunto completo en una sola página.

### Etapa 1 (Sección 4)

- Archivo nuevo: `myfirst_hw.h` con desplazamientos de registros, máscaras y valores fijos.
- `regs_buf` y `regs_size` en el softc; asignados y liberados en attach/detach.
- Funciones auxiliares de acceso: `myfirst_reg_read`, `myfirst_reg_write`, `myfirst_reg_update`.
- Macros: `MYFIRST_REG_READ`, `MYFIRST_REG_WRITE`.
- Sysctls: `reg_ctrl`, `reg_status`, `reg_device_id`, `reg_firmware_rev` (lectura), `reg_ctrl_set` (escritura).
- Acoplamiento: `myfirst_ctrl_update` al escribir en CTRL.
- Etiqueta de versión: `0.9-mmio-stage1`.

### Etapa 2 (Sección 5)

- Se añaden `regs_tag` y `regs_handle` en el softc.
- El cuerpo de los accesores se reescribe para usar `bus_space_read_4` y `bus_space_write_4`.
- La ruta de escritura actualiza `DATA_IN` y `STATUS.DATA_AV`.
- La ruta de lectura actualiza `DATA_OUT` y borra `STATUS.DATA_AV` cuando el buffer se vacía.
- Se añade `reg_ticker_task`; incrementa `SCRATCH_A` en cada tick.
- Nuevos sysctls: `reg_data_in`, `reg_data_out`, `reg_intr_mask`, `reg_intr_status`, `reg_scratch_a`, `reg_scratch_b`, `reg_ticker_enabled`.
- Nuevo documento: `HARDWARE.md`.
- Etiqueta de versión: `0.9-mmio-stage2`.

### Etapa 3 (Sección 6)

- Se añade `MYFIRST_ASSERT` a los accesores.
- Todas las rutas de acceso a registros adquieren `sc->mtx`.
- Se añaden el access log y sus sysctls (`access_log_enabled`, `access_log`).
- Función auxiliar `myfirst_reg_write_barrier` para escrituras con soporte de barreras.
- `HARDWARE.md` ampliado con la propiedad por registro.
- Etiqueta de versión: `0.9-mmio-stage3`.

### Etapa 4 (Sección 8)

- División de archivos: `myfirst_hw.c`, `myfirst_hw.h`, `myfirst.c`.
- Agrupación del estado del hardware en `struct myfirst_hw`.
- APIs `myfirst_hw_attach`, `myfirst_hw_detach`, `myfirst_hw_add_sysctls`.
- Macros `CSR_READ_4`, `CSR_WRITE_4`, `CSR_UPDATE_4`.
- `HARDWARE.md` finalizado.
- Pasada de regresión completa.
- Etiqueta de versión: `0.9-mmio`.

### Líneas de código

- La Etapa 1 añade aproximadamente 80 líneas (cabecera, funciones auxiliares de acceso, sysctls).
- La Etapa 2 añade aproximadamente 90 líneas (reescritura de accesores, acoplamiento de la ruta de datos, tarea del ticker).
- La Etapa 3 añade aproximadamente 70 líneas (locking, access log, función auxiliar de barrera).
- La Etapa 4 es una reorganización neta: las líneas se mueven entre archivos, pero el total es aproximadamente el mismo.

Total de adiciones, Capítulo 16: aproximadamente 240 a 280 líneas distribuidas en cuatro pequeñas etapas.



## Referencia: Comparación con el acceso a registros de dispositivo en Linux

Dado que muchos lectores llegan a FreeBSD desde Linux, una breve comparación del vocabulario de acceso a registros aclara qué se traslada directamente y qué no.

### Linux: `ioremap` + `readl` / `writel`

Linux usa una forma diferente. Un driver obtiene una dirección virtual mediante `ioremap` (para MMIO) o utiliza directamente el número de puerto I/O (para PIO). El acceso a registros se realiza a través de `readl(addr)` y `writel(value, addr)`, con variantes para distintos anchos (`readb`, `readw`, `readl`, `readq`). `addr` es un puntero virtual del kernel convertido a un tipo marcador específico.

### FreeBSD: `bus_alloc_resource` + `bus_read_*` / `bus_write_*`

FreeBSD usa la abstracción de tag y handle. Un driver obtiene un `struct resource *` mediante `bus_alloc_resource_any` y luego usa `bus_read_4` y `bus_write_4` sobre él. La macro extrae el tag y el handle del recurso; el driver no los ve directamente en la mayor parte del código.

### Qué se traslada

- El modelo mental: registros en desplazamientos fijos, accedidos por ancho, con barreras para garantizar el orden.
- La idea de definir una cabecera de desplazamientos de registros y máscaras de bits.
- La idea de envolver el acceso en macros específicas del driver (el `read_reg32` de Linux, el `CSR_READ_4` de FreeBSD).
- La disciplina de proteger con lock el acceso al estado compartido.

### Qué difiere

- FreeBSD lleva un tag explícito que codifica el tipo de espacio de direcciones. Linux no lo hace; las variantes de función eligen el espacio de direcciones de forma implícita.
- La abstracción de recursos de FreeBSD es más explícita en cuanto a propiedad y ciclo de vida. El `ioremap` de Linux es un envoltorio más fino.
- La función de barrera de FreeBSD acepta argumentos de desplazamiento y longitud que los puentes de bus pueden usar para barreras estrechas. Las macros `mb`, `rmb`, `wmb` y `mmiowb` de Linux son de ámbito global para la CPU.
- La `bus_space` de FreeBSD es utilizable para simulación (como en este capítulo); el camino equivalente en Linux es menos amigable para ese uso.

Portar un driver de Linux a FreeBSD o viceversa implica reescribir la capa de acceso a registros, pero no el mapa de registros, ya que este lo define el dispositivo, no el sistema operativo. Un driver bien organizado que mantiene su acceso a registros detrás de macros al estilo CSR puede tener esas macros reemplazadas con cambios mínimos en el resto del código.



## Referencia: Ejemplo completo: el `myfirst_hw.h` final

Para consulta, la cabecera completa de la Etapa 4. Esta es la que reside en `examples/part-04/ch16-accessing-hardware/stage4-final/myfirst_hw.h`.

```c
/* myfirst_hw.h -- Chapter 16 Stage 4 simulated hardware interface. */
#ifndef _MYFIRST_HW_H_
#define _MYFIRST_HW_H_

#include <sys/types.h>
#include <sys/bus.h>
#include <machine/bus.h>

/* Register offsets. */
#define MYFIRST_REG_CTRL         0x00
#define MYFIRST_REG_STATUS       0x04
#define MYFIRST_REG_DATA_IN      0x08
#define MYFIRST_REG_DATA_OUT     0x0c
#define MYFIRST_REG_INTR_MASK    0x10
#define MYFIRST_REG_INTR_STATUS  0x14
#define MYFIRST_REG_DEVICE_ID    0x18
#define MYFIRST_REG_FIRMWARE_REV 0x1c
#define MYFIRST_REG_SCRATCH_A    0x20
#define MYFIRST_REG_SCRATCH_B    0x24

#define MYFIRST_REG_SIZE         0x40

/* CTRL bits. */
#define MYFIRST_CTRL_ENABLE      0x00000001u
#define MYFIRST_CTRL_RESET       0x00000002u
#define MYFIRST_CTRL_MODE_MASK   0x000000f0u
#define MYFIRST_CTRL_MODE_SHIFT  4
#define MYFIRST_CTRL_LOOPBACK    0x00000100u

/* STATUS bits. */
#define MYFIRST_STATUS_READY     0x00000001u
#define MYFIRST_STATUS_BUSY      0x00000002u
#define MYFIRST_STATUS_ERROR     0x00000004u
#define MYFIRST_STATUS_DATA_AV   0x00000008u

/* INTR bits. */
#define MYFIRST_INTR_DATA_AV     0x00000001u
#define MYFIRST_INTR_ERROR       0x00000002u
#define MYFIRST_INTR_COMPLETE    0x00000004u

/* Fixed values. */
#define MYFIRST_DEVICE_ID_VALUE  0x4D594649u
#define MYFIRST_FW_REV_MAJOR     1
#define MYFIRST_FW_REV_MINOR     0
#define MYFIRST_FW_REV_VALUE     ((MYFIRST_FW_REV_MAJOR << 16) | MYFIRST_FW_REV_MINOR)

/* Access log. */
#define MYFIRST_ACCESS_LOG_SIZE  64

struct myfirst_access_log_entry {
        uint64_t   timestamp_ns;
        uint32_t   value;
        bus_size_t offset;
        uint8_t    is_write;
        uint8_t    width;
        uint8_t    context_tag;
        uint8_t    _pad;
};

/* Hardware state, grouped. */
struct myfirst_hw {
        uint8_t                *regs_buf;
        size_t                  regs_size;
        bus_space_tag_t         regs_tag;
        bus_space_handle_t      regs_handle;

        struct task             reg_ticker_task;
        int                     reg_ticker_enabled;

        struct myfirst_access_log_entry access_log[MYFIRST_ACCESS_LOG_SIZE];
        unsigned int            access_log_head;
        bool                    access_log_enabled;
};

/* API. */
struct myfirst_softc;

int  myfirst_hw_attach(struct myfirst_softc *sc);
void myfirst_hw_detach(struct myfirst_softc *sc);
void myfirst_hw_add_sysctls(struct myfirst_softc *sc);

uint32_t myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset);
void     myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset,
             uint32_t value);
void     myfirst_reg_update(struct myfirst_softc *sc, bus_size_t offset,
             uint32_t clear_mask, uint32_t set_mask);
void     myfirst_reg_write_barrier(struct myfirst_softc *sc, bus_size_t offset,
             uint32_t value, int flags);

#define CSR_READ_4(sc, off)        myfirst_reg_read((sc), (off))
#define CSR_WRITE_4(sc, off, val)  myfirst_reg_write((sc), (off), (val))
#define CSR_UPDATE_4(sc, off, clear, set) \
        myfirst_reg_update((sc), (off), (clear), (set))

#endif /* _MYFIRST_HW_H_ */
```

Esta única cabecera es lo que incluye el resto del driver para acceder a la interfaz del hardware. Un lector que la lea una vez comprende qué registros existen, cómo se accede a ellos y qué macros usa el cuerpo del driver.



## Referencia: Ejemplo completo: las funciones de acceso de `myfirst_hw.c`

Como complemento a la cabecera, las implementaciones. Para consulta y como plantilla.

```c
/* myfirst_hw.c -- Chapter 16 Stage 4 hardware access layer. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/taskqueue.h>
#include <sys/sysctl.h>
#include <sys/sbuf.h>
#include <machine/bus.h>

#include "myfirst.h"      /* struct myfirst_softc, MYFIRST_LOCK, ... */
#include "myfirst_hw.h"

MALLOC_DECLARE(M_MYFIRST);

uint32_t
myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset)
{
        struct myfirst_hw *hw = sc->hw;
        uint32_t value;

        MYFIRST_ASSERT(sc);
        KASSERT(hw != NULL, ("myfirst: hw is NULL in reg_read"));
        KASSERT(offset + 4 <= hw->regs_size,
            ("myfirst: register read past end: offset=%#x size=%zu",
             (unsigned)offset, hw->regs_size));

        value = bus_space_read_4(hw->regs_tag, hw->regs_handle, offset);

        if (hw->access_log_enabled) {
                unsigned int idx = hw->access_log_head++ % MYFIRST_ACCESS_LOG_SIZE;
                struct myfirst_access_log_entry *e = &hw->access_log[idx];
                struct timespec ts;
                nanouptime(&ts);
                e->timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
                e->value = value;
                e->offset = offset;
                e->is_write = 0;
                e->width = 4;
                e->context_tag = 'd';
        }

        return (value);
}

void
myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset, uint32_t value)
{
        struct myfirst_hw *hw = sc->hw;

        MYFIRST_ASSERT(sc);
        KASSERT(hw != NULL, ("myfirst: hw is NULL in reg_write"));
        KASSERT(offset + 4 <= hw->regs_size,
            ("myfirst: register write past end: offset=%#x size=%zu",
             (unsigned)offset, hw->regs_size));

        bus_space_write_4(hw->regs_tag, hw->regs_handle, offset, value);

        if (hw->access_log_enabled) {
                unsigned int idx = hw->access_log_head++ % MYFIRST_ACCESS_LOG_SIZE;
                struct myfirst_access_log_entry *e = &hw->access_log[idx];
                struct timespec ts;
                nanouptime(&ts);
                e->timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
                e->value = value;
                e->offset = offset;
                e->is_write = 1;
                e->width = 4;
                e->context_tag = 'd';
        }
}

void
myfirst_reg_update(struct myfirst_softc *sc, bus_size_t offset,
    uint32_t clear_mask, uint32_t set_mask)
{
        uint32_t v;

        MYFIRST_ASSERT(sc);
        v = myfirst_reg_read(sc, offset);
        v &= ~clear_mask;
        v |= set_mask;
        myfirst_reg_write(sc, offset, v);
}

void
myfirst_reg_write_barrier(struct myfirst_softc *sc, bus_size_t offset,
    uint32_t value, int flags)
{
        struct myfirst_hw *hw = sc->hw;

        MYFIRST_ASSERT(sc);
        myfirst_reg_write(sc, offset, value);
        bus_space_barrier(hw->regs_tag, hw->regs_handle, 0, hw->regs_size, flags);
}
```

Este es un archivo completo y funcional. Las funciones `myfirst_hw_attach`, `myfirst_hw_detach` y `myfirst_hw_add_sysctls` continúan en el mismo archivo; son más largas pero siguen los mismos patrones que el texto desarrollado en la Sección 4.



## Referencia: Un módulo de prueba mínimo independiente

Para los lectores que quieran practicar `bus_space(9)` de forma aislada del driver `myfirst`, aquí hay un módulo del kernel mínimo e independiente que asigna un "dispositivo" con memoria del kernel, lo expone a través de sysctls y permite al lector experimentar. Guárdalo como `hwsim.c`:

```c
/* hwsim.c -- Chapter 16 stand-alone bus_space(9) practice module. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <machine/bus.h>

MALLOC_DEFINE(M_HWSIM, "hwsim", "hwsim test module");

#define HWSIM_SIZE 0x40

static uint8_t            *hwsim_buf;
static bus_space_tag_t     hwsim_tag;
static bus_space_handle_t  hwsim_handle;

static SYSCTL_NODE(_dev, OID_AUTO, hwsim,
    CTLFLAG_RW | CTLFLAG_MPSAFE, NULL, "hwsim practice module");

static int
hwsim_sysctl_reg(SYSCTL_HANDLER_ARGS)
{
        bus_size_t offset = arg2;
        uint32_t value;
        int error;

        if (hwsim_buf == NULL)
                return (ENODEV);
        value = bus_space_read_4(hwsim_tag, hwsim_handle, offset);
        error = sysctl_handle_int(oidp, &value, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);
        bus_space_write_4(hwsim_tag, hwsim_handle, offset, value);
        return (0);
}

static int
hwsim_modevent(module_t mod, int event, void *arg)
{
        switch (event) {
        case MOD_LOAD:
                hwsim_buf = malloc(HWSIM_SIZE, M_HWSIM, M_WAITOK | M_ZERO);
#if defined(__amd64__) || defined(__i386__)
                hwsim_tag = X86_BUS_SPACE_MEM;
#else
                free(hwsim_buf, M_HWSIM);
                hwsim_buf = NULL;
                return (EOPNOTSUPP);
#endif
                hwsim_handle = (bus_space_handle_t)(uintptr_t)hwsim_buf;

                SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_dev_hwsim),
                    OID_AUTO, "reg0",
                    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
                    NULL, 0x00, hwsim_sysctl_reg, "IU",
                    "Offset 0x00");
                SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_dev_hwsim),
                    OID_AUTO, "reg4",
                    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
                    NULL, 0x04, hwsim_sysctl_reg, "IU",
                    "Offset 0x04");
                return (0);
        case MOD_UNLOAD:
                if (hwsim_buf != NULL) {
                        free(hwsim_buf, M_HWSIM);
                        hwsim_buf = NULL;
                }
                return (0);
        default:
                return (EOPNOTSUPP);
        }
}

static moduledata_t hwsim_mod = {
        "hwsim",
        hwsim_modevent,
        NULL
};

DECLARE_MODULE(hwsim, hwsim_mod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(hwsim, 1);
```

Un `Makefile` de dos líneas:

```text
KMOD=  hwsim
SRCS=  hwsim.c

.include <bsd.kmod.mk>
```

Compila, carga y experimenta:

```text
# make clean && make
# kldload ./hwsim.ko
# sysctl dev.hwsim.reg0
# sysctl dev.hwsim.reg0=0xdeadbeef
# sysctl dev.hwsim.reg0
# sysctl dev.hwsim.reg4=0x12345678
# sysctl dev.hwsim.reg4
# kldunload hwsim
```

El módulo demuestra `bus_space(9)` en su forma más simple posible: un buffer de memoria, un tag, un handle, dos ranuras de registro y un par de sysctls. Un lector que lo escriba y lo ejecute tendrá todo el vocabulario de la Sección 3 en unas 80 líneas de C.



## Referencia: Por qué `volatile` importa en `bus_space`

Una nota sobre un detalle sutil que el capítulo mencionó pero no desarrolló.

Cuando `bus_space_read_4` en x86 se expande a `*(volatile u_int32_t *)(handle + offset)`, el calificador `volatile` no es decorativo. Es esencial.

Sin `volatile`, el compilador asume que leer una posición de memoria dos veces seguidas, sin ningún almacenamiento intermedio en esa posición, debe devolver el mismo valor. Tiene libertad para reordenar, fusionar o eliminar lecturas basándose en esa suposición. Para la memoria ordinaria, la suposición es válida: la RAM no cambia por debajo de ti. Para la memoria del dispositivo, la suposición es incorrecta. Una lectura podría consumir un evento; una escritura podría tener efectos inmediatos y visibles que una lectura posterior detecta.

El calificador `volatile` le dice al compilador: trata este acceso como si tuviera efectos secundarios observables. No lo reordenes con otros accesos volatile. No lo elimines. No guardes su resultado en caché. Emite una carga (o almacenamiento) cada vez que aparezca, exactamente como está escrito.

En x86, esto es suficiente. El modelo de memoria de la CPU es lo bastante fuerte como para que, una vez emitida la carga en el orden del programa, se ejecute en ese mismo orden. En arm64 son necesarias barreras adicionales para imponer el orden del programa frente a las reordenaciones a nivel de CPU, razón por la cual `bus_space_barrier` en arm64 emite instrucciones DMB o DSB y en x86 emite únicamente una barrera de compilador.

La regla breve: cada vez que escribas un accesor manual para memoria de dispositivo, usa `volatile`. Cada vez que uses `bus_space_*` directamente, el `volatile` ya está incluido. Cada vez que conviertas un puntero a memoria de dispositivo mediante un tipo no volatile, tienes un bug esperando a manifestarse.



## Referencia: Una breve comparación de patrones de acceso en drivers reales de FreeBSD

Un repaso informal de los patrones utilizados en drivers reales. Cada ejemplo cita el archivo y el patrón característico; lee los archivos directamente para ver el patrón en contexto.

**`/usr/src/sys/dev/ale/if_ale.c`**: Usa las macros `CSR_READ_4(sc, reg)` y `CSR_WRITE_4(sc, reg, val)` definidas en `if_alevar.h` sobre `bus_read_4` y `bus_write_4`. El softc contiene `ale_res[]`, un array de recursos. El patrón es limpio y escala bien.

**`/usr/src/sys/dev/e1000/if_em.c`**: Usa `E1000_READ_REG(hw, reg)` y `E1000_WRITE_REG(hw, reg, val)` que envuelven `bus_space_read_4` y `bus_space_write_4` sobre el tag y el handle de la estructura `osdep`. Mayor nivel de indirección que `ale`, justificado por el modelo de código compartido entre sistemas operativos de Intel.

**`/usr/src/sys/dev/uart/uart_bus_pci.c`**: Un driver de adaptación que asigna recursos y los cede al subsistema UART genérico. El acceso a los registros ocurre en el código del subsistema (`uart_dev_ns8250.c`), no en el código de adaptación PCI.

**`/usr/src/sys/dev/uart/uart_dev_ns8250.c`**: Acceso directo con `bus_read_1` y `bus_write_1` sobre un `struct uart_bas *` que contiene el tag y el handle. Disposición de registros de 8 bits heredada.

**`/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`**: Usa `bus_read_4` y `bus_write_4` a través de campos `struct resource *` en el softc. El capítulo 18 usa virtio como objetivo de prueba para los ejercicios PCI reales.

**`/usr/src/sys/dev/random/ivy.c`** (Intel Ivy Bridge RDRAND): Usa instrucciones de CPU directamente (`rdrand`) en lugar de `bus_space`; este es un caso inusual porque el «dispositivo» es la propia CPU, accesible mediante ensamblador en línea.

En todos estos casos, el patrón es «envolver `bus_*` o `bus_space_*` en macros específicas del driver, mantener los desplazamientos de registro en un archivo de cabecera y acceder a los registros a través de las macros en el cuerpo del código». La Etapa 4 del capítulo 16 sigue esta convención.

## Referencia: El camino por recorrer en la Parte 4

Una vista previa de cómo el material del Capítulo 16 se integra en los capítulos siguientes, para los lectores que prefieren tener un mapa en una sola página.

**Capítulo 17 (Simulación de hardware)**: Extiende la simulación con comportamiento dinámico. Los temporizadores modifican bits de `STATUS`. Escribir en `CTRL.GO` desencadena una actualización diferida. Se pueden inyectar errores. El vocabulario de registros se mantiene igual; la simulación se vuelve más rica.

**Capítulo 18 (Escritura de un driver PCI)**: Reemplaza la simulación con PCI real. `bus_alloc_resource_any` entra en escena de verdad. IDs de fabricante y dispositivo, mapeo de BAR, `pci_enable_busmaster`, `pciconf`. La ruta de simulación sigue disponible tras una bandera de compilación para continuar las pruebas.

**Capítulo 19 (Gestión de interrupciones)**: Añade `bus_setup_intr`, filter frente a ithread, reconocimiento de la interrupción y la semántica de lectura-para-borrar del registro `INTR_STATUS` en un contexto real. El registro de accesos del Capítulo 16 resulta inestimable para depurar secuencias de interrupción.

**Capítulos 20 y 21 (DMA)**: Añaden `bus_dma(9)`. Los accesos a registros se convierten en la superficie de control de las operaciones DMA: configurar un descriptor, escribir en un registro doorbell, esperar a que finalice. La historia de `bus_space_barrier` se vuelve imprescindible.

**Capítulo 22 (Gestión de energía)**: Suspend, resume y estados de energía dinámicos. Registros que guardan y restauran el estado del dispositivo. La mayor parte del vocabulario de la Parte 4 se aplica aquí; la gestión de energía añade unos pocos modismos más.

Cada capítulo introduce una nueva capa; la capa del Capítulo 16 (el registro) es el fundamento de todas ellas. Un lector que termina el Capítulo 16 sintiéndose cómodo con `bus_space(9)` descubrirá que cada capítulo siguiente añade un nuevo vocabulario sobre terreno ya conocido.



## Referencia: Cómo leer una hoja de datos

Todo driver real comienza con una hoja de datos (datasheet): el documento que el fabricante del dispositivo publica describiendo la interfaz de registros, el modelo de programación y el comportamiento operativo. El Capítulo 16 trabaja con un dispositivo simulado, así que no hay ninguna hoja de datos que consultar. Los capítulos posteriores apuntan a dispositivos reales, y un autor de drivers que se sienta cómodo con las hojas de datos aprenderá más rápido.

A continuación se ofrece una breve introducción. Puedes saltártela en la primera lectura y volver a ella cuando el Capítulo 18 o un capítulo posterior te lleve a la especificación de un dispositivo real.

### La estructura de una hoja de datos

Una hoja de datos suele ser un PDF de entre cincuenta y mil quinientas páginas. Contiene:

- Una visión funcional general (qué hace el dispositivo, a alto nivel).
- Descripción del pinout o de la interfaz física (qué señales tiene el dispositivo y qué significan).
- Referencia de registros (el mapeo que el Capítulo 16 te ha enseñado a leer).
- Modelo de programación (la secuencia de operaciones que un driver debe realizar para cada acción de alto nivel).
- Características eléctricas (tensiones, tiempos, rangos de temperatura).
- Dimensiones del encapsulado (datos mecánicos para el diseñador de la placa de circuito impreso).

Los autores de drivers se interesan principalmente por la referencia de registros y el modelo de programación. Todo lo demás es para los diseñadores de hardware.

### Cómo leer la referencia de registros

La referencia de registros es habitualmente una serie de tablas, una por registro, con las siguientes columnas:

- Offset.
- Anchura.
- Valor de reset (el valor que tiene el registro tras un encendido o un reset).
- Tipo de acceso (R, W, RW, R/W1C para lectura con escritura-uno-para-borrar, etc.).
- Nombres de campo y rangos de bits.
- Descripciones de campo.

Un autor de drivers experimentado lee primero esta tabla, anota cualquier tipo de acceso inusual y traduce el mapa de registros a un header de C con offsets y máscaras de bits con nombre. La traducción es mecánica; el cuidado está en conseguir que cada bit sea correcto.

Una nota especial sobre los **valores de reset**. El valor de reset te indica qué devuelve el registro inmediatamente después de que el dispositivo se haya encendido o reseteado. Si el driver escribe en un campo y luego lo lee de vuelta, la lectura debe devolver el valor escrito, no el valor de reset. Pero si el driver no ha escrito en el registro, la lectura devuelve el valor de reset. Confundir esto genera bugs sorprendentes: el driver «ve» un registro que no inicializó e interpreta erróneamente el valor de reset como un cambio de estado.

### Cómo leer el modelo de programación

La sección de modelo de programación describe las secuencias de operaciones sobre registros necesarias para controlar el dispositivo. Una entrada típica tiene este aspecto:

> **Transmitir un paquete.**
> 1. Confirmar que `STATUS.TX_READY` está activo.
> 2. Escribir los datos del paquete en `TX_BUF[0..n-1]` en orden.
> 3. Escribir la longitud del paquete en `TX_LEN`.
> 4. Escribir `CTRL.TX_START`.
> 5. Esperar a que `STATUS.TX_DONE` se active (puede tardar hasta 100 us).
> 6. Borrar `STATUS.TX_DONE` escribiendo 1 en el mismo bit.

Esta secuencia es lo que implementa la ruta de transmisión de un driver. El orden de los pasos es fijo; alterarlo puede dejar el dispositivo en un estado inconsistente. El trabajo del driver consiste en traducir cada paso a una llamada a `bus_space_write_*` o `bus_space_read_*`, con las barreras y el locking adecuados.

La mayoría de las hojas de datos contienen varias secuencias de este tipo. Un dispositivo de red puede tener secuencias de recepción, transmisión, inicialización de enlace, recuperación de errores y apagado. Cada una está documentada de forma independiente.

### Extracción del header de C

Un autor de drivers experimentado lee la referencia de registros una vez y produce un header de C similar a este:

```c
/* foo_regs.h -- derived from Foo Corp. Foo-9000 datasheet, rev 3.2. */

#define FOO_REG_CTRL     0x0000
#define FOO_REG_STATUS   0x0004
#define FOO_REG_TX_LEN   0x0010
#define FOO_REG_TX_BUF   0x0100  /* base of 4 KiB TX buffer region */

#define FOO_CTRL_TX_START 0x00000001u
#define FOO_CTRL_RX_ENABLE 0x00000002u

#define FOO_STATUS_TX_READY 0x00000001u
#define FOO_STATUS_TX_DONE  0x00000002u

/* ... and so on ... */
```

El header es donde vive el conocimiento del driver sobre la interfaz de registros del dispositivo. Mantenlo actualizado con la hoja de datos; referencia la revisión de la hoja de datos en el header para que los futuros colaboradores sepan a qué versión corresponden los offsets.

### Un patrón para cada tipo de registro

Los diferentes tipos de acceso implican diferentes patrones de código.

**Solo lectura, sin efecto secundario.** Lee cuando quieras. Guarda en cache si resulta conveniente. No se necesita locking más allá de «no leer un registro de una región que ya ha sido liberada».

**Solo lectura, con efecto secundario (lectura-para-borrar).** Lee exactamente con la frecuencia que exige el protocolo. No añadas lecturas de depuración que borren estado. No vuelvas a leer para «confirmar» un valor.

**Solo escritura.** Escribe con el valor que requiera el protocolo. No lo leas de vuelta; la lectura devuelve basura.

**Lectura/escritura, sin efecto secundario.** Secuencias seguras de lectura-modificación-escritura bajo lock.

**Lectura/escritura, con efecto secundario en la escritura.** Ten cuidado con las secuencias de lectura-modificación-escritura: escribir un bit con el mismo valor que tenía puede seguir desencadenando el efecto secundario. A veces una hoja de datos documenta esto indicando «las escrituras de 0 en el bit X no tienen efecto»; otras veces no lo hace, y el driver debe ser conservador.

**Escritura-uno-para-borrar (W1C).** Habitual en los registros de estado de interrupción. Escribir 1 en un bit lo borra; escribir 0 no tiene efecto. Usa `CSR_WRITE_4(sc, REG, mask_of_bits_to_clear)`, no una secuencia de lectura-modificación-escritura.

### Un ejercicio: imagina que el dispositivo del Capítulo 16 tiene una hoja de datos

Para cerrar esta referencia, practica extrayendo un header de registros a partir de una «hoja de datos» para el dispositivo simulado del Capítulo 16. Escribe una hoja de datos ficticia en prosa describiendo cada registro, su valor de reset, su tipo de acceso y la disposición de sus campos. A continuación produce el correspondiente `myfirst_hw.h`. Compara tu versión con la que proporcionó el Capítulo 16.

El ejercicio desarrolla el músculo que necesitarás para cada dispositivo real que aparezca más adelante en el libro.



## Referencia: Un caso práctico sobre la ausencia de barreras

Un breve relato aleccionador para hacer concreto el tema de las barreras, usando únicamente el vocabulario de MMIO que el Capítulo 16 ya ha introducido.

Imagina un dispositivo real cuya hoja de datos dice: «Para enviar un comando, escribe la palabra de comando de 32 bits en `CMD_DATA` y luego escribe el código de comando de 32 bits en `CMD_GO`. El dispositivo recoge la palabra de comando cuando se escribe en `CMD_GO`.» El driver expresa esta secuencia de forma ingenua:

```c
/* Step 1: write the command payload. */
CSR_WRITE_4(sc, MYFIRST_REG_CMD_DATA, payload);

/* Step 2: write the command code to trigger execution. */
CSR_WRITE_4(sc, MYFIRST_REG_CMD_GO, opcode);
```

En x86 esto funciona. El modelo de memoria de x86 garantiza que los stores se completan en orden de programa desde el punto de vista de la CPU, y la escritura anotada con `volatile` dentro de `bus_space_write_4` impide que el compilador reordene los dos enunciados. Cuando `CMD_GO` llega al dispositivo, `CMD_DATA` ya ha sido escrito.

En arm64, el mismo código puede fallar. La CPU tiene libertad para reordenar los dos stores a nivel del subsistema de memoria. El dispositivo puede observar `CMD_GO` primero, capturar el valor obsoleto que todavía hay en `CMD_DATA` y ejecutar un comando que el driver no pretendía. El síntoma es intermitente, depende de la carga y solo aparece en hardware arm64. Un driver probado únicamente en x86 saldría al mercado con este bug sin que nadie lo detectara.

La corrección es un cambio de una sola línea:

```c
CSR_WRITE_4(sc, MYFIRST_REG_CMD_DATA, payload);

/* Ensure the payload write reaches the device before the doorbell. */
bus_space_barrier(sc->hw->regs_tag, sc->hw->regs_handle, 0, sc->hw->regs_size,
    BUS_SPACE_BARRIER_WRITE);

CSR_WRITE_4(sc, MYFIRST_REG_CMD_GO, opcode);
```

En x86, `bus_space_barrier` con `BUS_SPACE_BARRIER_WRITE` emite únicamente una barrera de compilador, que no cuesta instrucciones y preserva el orden de programa que la CPU x86 ya iba a mantener. En arm64, emite un `dmb` o `dsb` que obliga a la CPU a vaciar su store buffer antes de emitir el siguiente store. El mismo código fuente hace lo correcto en ambas arquitecturas.

El relato plantea tres conclusiones.

**Primera: x86 da a los autores de drivers una falsa sensación de seguridad.** El código probado únicamente en x86 puede pasar todas las pruebas y, sin embargo, estar roto en arm64 de formas que solo se manifiestan bajo patrones de carga específicos.

**Segunda: los costes de portabilidad son mínimos si se incorporan desde el principio.** Añadir un `bus_space_barrier` en el lugar correcto es un cambio de una sola línea. Diagnosticar el bug un año después en un despliegue arm64 es una semana de trabajo.

**Tercera: el coste de las barreras en x86 es despreciable para los drivers típicos.** Una barrera de compilador no cuesta instrucciones; restringe el reordenamiento del compilador, lo cual para las rutas frías de un driver no tiene ninguna importancia.

Una familia relacionada de bugs de ordenación aparece cuando el driver escribe en memoria que el dispositivo lee a través de DMA (un anillo de descriptores, por ejemplo) y luego activa un registro doorbell. Ese patrón necesita una llamada a `bus_dmamap_sync`, no solo un `bus_space_barrier`; el Capítulo 20 enseña la ruta DMA en profundidad. El vocabulario es diferente, pero la intuición (las escrituras deben completarse antes de activar el doorbell) es la misma.

La disciplina que fomenta el Capítulo 16, incluso cuando el beneficio inmediato no es visible, rinde sus frutos cuando el código se ejecuta en hardware que el autor nunca vio.



## Referencia: Lectura de `if_ale.c` paso a paso

Un recorrido guiado por la ruta attach de un driver real, para que el vocabulario del Capítulo 16 aterrice en código de producción. Abre `/usr/src/sys/dev/ale/if_ale.c`, salta a `ale_attach` y sigue adelante.

### Paso 1: El punto de entrada attach

La función `ale_attach` comienza así:

```c
static int
ale_attach(device_t dev)
{
        struct ale_softc *sc;
        if_t ifp;
        uint16_t burst;
        int error, i, msic, msixc;
        uint32_t rxf_len, txf_len;

        error = 0;
        sc = device_get_softc(dev);
        sc->ale_dev = dev;
```

`device_get_softc(dev)` es el mismo patrón que introdujo el Capítulo 6. No hay nada nuevo aquí.

### Paso 2: Configuración inicial del locking

El driver inicializa su mutex de ruta de datos, su callout y su primera tarea:

```c
mtx_init(&sc->ale_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
    MTX_DEF);
callout_init_mtx(&sc->ale_tick_ch, &sc->ale_mtx, 0);
NET_TASK_INIT(&sc->ale_int_task, 0, ale_int_task, sc);
```

Cada línea de aquí procede directamente de la Parte 3. El mutex es el primitivo del Capítulo 11; el callout con soporte de locking es el primitivo del Capítulo 13; la tarea es el primitivo del Capítulo 14. Un lector que haya trabajado la Parte 3 reconoce los tres de inmediato.

### Paso 3: Bus-mastering PCI y asignación de recursos

```c
pci_enable_busmaster(dev);
sc->ale_res_spec = ale_res_spec_mem;
sc->ale_irq_spec = ale_irq_spec_legacy;
error = bus_alloc_resources(dev, sc->ale_res_spec, sc->ale_res);
if (error != 0) {
        device_printf(dev, "cannot allocate memory resources.\n");
        goto fail;
}
```

`pci_enable_busmaster` es específico de PCI; el Capítulo 18 lo cubre. `ale_res_spec` es un array de `struct resource_spec` (definido antes en el archivo) que describe los recursos que quiere el driver. `bus_alloc_resources` (en plural) toma la especificación y rellena el array `sc->ale_res` con los recursos asignados. Es un envoltorio de conveniencia sobre llamadas a `bus_alloc_resource_any` en un bucle; ambos patrones son habituales, y la discusión del Capítulo 18 sobre asignación de recursos PCI cubre los dos.

Tras esta llamada, `sc->ale_res[0]` contiene un `struct resource *` para la región MMIO del dispositivo, y las macros `CSR_READ_*` / `CSR_WRITE_*` (definidas en `/usr/src/sys/dev/ale/if_alevar.h`, justo después de la estructura softc) pueden utilizarse para acceder a los registros a través de él.

### Paso 4: Lectura del primer registro

El driver lee el registro `PHY_STATUS` para decidir con qué variante del chip está trabajando:

```c
if ((CSR_READ_4(sc, ALE_PHY_STATUS) & PHY_STATUS_100M) != 0) {
        /* L1E AR8121 */
        sc->ale_flags |= ALE_FLAG_JUMBO;
} else {
        /* L2E Rev. A. AR8113 */
        sc->ale_flags |= ALE_FLAG_FASTETHER;
}
```

Este es el primer acceso a un registro en `ale_attach`. Es una única llamada a `CSR_READ_4` que devuelve un valor de 32 bits, enmascarado con el bit `PHY_STATUS_100M`, y que se usa para seleccionar una ruta de código. La constante `ALE_PHY_STATUS` es un offset de registro definido en `/usr/src/sys/dev/ale/if_alereg.h`. La máscara de bits `PHY_STATUS_100M` está definida en el mismo header.

Cada elemento de esa línea es vocabulario del capítulo 16. `CSR_READ_4` se expande a `bus_read_4` sobre el primer recurso; `bus_read_4` se expande a `bus_space_read_4` sobre el tag y el handle del recurso; en espacio de memoria x86, `bus_space_read_4` se compila en una única instrucción `mov`.

### Paso 5: Lectura de más registros

Unas líneas más adelante, el driver lee tres registros más para recopilar datos de identificación del chip:

```c
sc->ale_chip_rev = CSR_READ_4(sc, ALE_MASTER_CFG) >>
    MASTER_CHIP_REV_SHIFT;
/* ... */
txf_len = CSR_READ_4(sc, ALE_SRAM_TX_FIFO_LEN);
rxf_len = CSR_READ_4(sc, ALE_SRAM_RX_FIFO_LEN);
```

El mismo patrón, tres registros más. Fíjate en la verificación de coherencia del hardware no inicializado unas líneas más abajo, protegida por `sc->ale_chip_rev == 0xFFFF`: si alguno de los valores devueltos se parece a `0xFFFFFFFF`, el driver asume que el hardware no está correctamente inicializado y abandona con `ENXIO`. Este tipo de comprobación es un hábito habitual y silencioso en los drivers de producción: un hardware que devuelve todos los bits a uno en cada registro normalmente indica que el mapeo es incorrecto, que el dispositivo no responde, o que el dispositivo fue apagado por gestión de energía y nunca se inicializó.

### Paso 6: Configuración de IRQ

Más adelante:

```c
error = bus_alloc_resources(dev, sc->ale_irq_spec, sc->ale_irq);
```

Los recursos IRQ tienen su propia asignación. El capítulo 19 cubre lo que sucede después: `bus_setup_intr`, la división filter-ithread y el manejador de interrupción.

### Paso 7: Lectura del archivo completo

Tras la asignación de IRQ, `ale_attach` continúa con la creación del tag DMA (capítulo 20), el registro de ifnet (capítulo 28 en la parte 6), la vinculación del PHY y así sucesivamente. Cada paso utiliza patrones que se introducirán en capítulos posteriores de este libro. Lo que el capítulo 16 te ha dado es el vocabulario para leer cada llamada a la macro `CSR_*` sin detenerte.

El ejercicio que consolida el recorrido: elige tres llamadas a `CSR_READ_4` o `CSR_WRITE_4` de cualquier parte de `/usr/src/sys/dev/ale/if_ale.c`, busca el desplazamiento del registro en `if_alereg.h`, decodifica la máscara de bits en esa misma cabecera y escribe una frase explicando qué está haciendo el driver en ese punto de llamada. Si puedes hacerlo para tres llamadas arbitrarias, habrás interiorizado el vocabulario que este capítulo enseñó.



## Referencia: Un balance honesto de las simplificaciones del capítulo 16

Un capítulo que enseña una pequeña parte de un tema amplio simplifica inevitablemente. Para ser honestos con el lector, a continuación se presenta un catálogo de lo que el capítulo 16 simplificó y cómo es la historia completa.

### La etiqueta simulada

La simulación del capítulo 16 usa `X86_BUS_SPACE_MEM` como etiqueta y una dirección virtual del kernel como handle. En x86 esto funciona porque las funciones `bus_space_read_*` de x86 se reducen a una desreferencia `volatile` de `handle + offset` para espacio de memoria. En otras arquitecturas el truco falla porque la etiqueta no es un entero; es un puntero a una estructura, y fabricarla manualmente requiere reproducir la estructura que espera `bus_space` de la plataforma.

La historia completa: los drivers reales nunca fabrican una etiqueta; la reciben del subsistema de bus a través de `rman_get_bustag`. El atajo de simulación del capítulo 16 es pedagógico, y el capítulo lo marca explícitamente como exclusivo de x86. La simulación más completa del capítulo 17 introduce una alternativa portátil, y la ruta PCI real del capítulo 18 elimina el atajo por completo.

### El protocolo de registros

Los registros del dispositivo simulado no tienen efectos secundarios en lectura ni en escritura. `STATUS` es establecido por el driver; no cambia de forma autónoma. `DATA_IN` es escrito por el driver; no reenvía la escritura a ningún consumidor imaginario aguas abajo. `INTR_STATUS` es un registro simple, no de tipo leer-para-borrar.

La historia completa: los dispositivos reales tienen protocolos. Leer un registro de estado puede consumir un evento. Escribir en un registro de comandos puede desencadenar una operación de varios ciclos dentro del dispositivo. La tarea del driver es seguir el protocolo con exactitud; un solo paso omitido produce un dispositivo que se comporta de forma incorrecta. El capítulo 17 introduce parte de esta complejidad añadiendo un protocolo basado en callout: una escritura desencadena un cambio de estado diferido.

### Granularidad del locking

El capítulo 16 usa un único mutex del driver (`sc->mtx`) para todo el acceso a registros. En la práctica, los drivers reales a veces dividen los locks: un lock de camino rápido para las escrituras de registro por paquete (en un driver de red) y un lock más lento para los cambios de configuración. Dividirlos aumenta la concurrencia a costa de una mayor disciplina de locking.

La historia completa: dividir los locks es una decisión de ajuste de rendimiento que pertenece a capítulos posteriores sobre escalado y profiling. El capítulo 16 usa un único lock porque es el diseño correcto más simple y porque los requisitos de rendimiento del driver están muy lejos del punto donde la contención de locks importa.

### Endianness

El capítulo 16 asume el orden de bytes del host para todos los valores de registro. Los dispositivos reales a veces usan un orden de bytes diferente al de la CPU del host. Las variantes `_stream_` de `bus_space` se ocupan de esto; el capítulo 16 no las usa.

La historia completa: la API `bus_space` de FreeBSD admite semánticas de intercambio de bytes por etiqueta. Un driver cuyo dispositivo es big-endian en una CPU little-endian usa una etiqueta con intercambio de bytes o las variantes `_stream_` más conversiones explícitas de `htobe32`/`be32toh`. La simulación del capítulo 16 es host-endian, por lo que el problema no surge; los drivers reales para dispositivos big-endian lo gestionan explícitamente.

### Atributos de caché

La asignación con `malloc(9)` del capítulo 16 produce memoria del kernel ordinaria con soporte de caché. La memoria de los dispositivos reales se mapea con diferentes atributos de caché (sin caché, write-combining, device-strongly-ordered) según la plataforma y los requisitos del dispositivo.

La historia completa: `bus_alloc_resource_any` con `RF_ACTIVE` en un BAR PCI real produce un mapeo con los atributos de caché correctos. La simulación no pasa por esta ruta; usa memoria con caché simple. Con los patrones del capítulo 16 (acceso serializado, accesos `volatile`), la diferencia de atributos de caché no se manifiesta. En la ruta PCI real del capítulo 18, el flujo de asignación se encarga de ello.

### Gestión de errores

El dispositivo simulado del capítulo 16 nunca devuelve un error en un acceso a registros. El hardware real a veces sí lo hace: una lectura puede agotar el tiempo de espera, una escritura puede ser rechazada, un bus puede bloquearse. El driver debe gestionar estos casos.

La historia completa: FreeBSD proporciona las variantes `bus_peek_*` y `bus_poke_*` (desde FreeBSD 13) que devuelven un error si el acceso falla. El capítulo 16 no las usa porque la simulación no puede fallar. El capítulo 19 las introduce en el contexto de manejadores de interrupción que pueden acceder a un dispositivo en un estado incierto.

### Interrupciones

El driver del capítulo 16 sondea registros a través de callouts y rutas de syscall. Los drivers reales suelen usar interrupciones para saber cuándo leer un registro.

La historia completa: las interrupciones son el tema del capítulo 19. El patrón de sondeo del capítulo 16 es un peldaño; después del capítulo 19 el driver tendrá un manejador de interrupción que reemplazará gran parte de la lógica de sondeo.

### DMA

El driver del capítulo 16 no usa DMA. Cada byte que fluye por el driver es copiado por la CPU, registro a registro.

La historia completa: los dispositivos reales de alto rendimiento usan DMA para datos en bloque. El driver programa el motor DMA del dispositivo a través de registros; después el dispositivo lee o escribe directamente en la RAM del sistema. El capítulo 20 y el capítulo 21 cubren la API de DMA.

### Resumen

El capítulo 16 es una rampa de entrada. Cada simplificación que realiza es deliberada, está identificada y es retomada por un capítulo posterior. El vocabulario que enseña el capítulo 16 es el vocabulario que todos los capítulos posteriores amplían; la disciplina que construye el capítulo 16 es la disciplina en la que todos los capítulos posteriores se apoyan. El capítulo no cubre la historia completa del hardware a propósito. Los capítulos posteriores rellenan el resto.



## Referencia: Guía rápida de los errores de MMIO más comunes

Cuando un driver se comporta incorrectamente, el error suele pertenecer a un pequeño conjunto de categorías recurrentes. A continuación, una guía rápida para reconocer cada una.

### 1. Error de uno en el mapa de registros

**Síntoma**: Una lectura devuelve un valor plausible pero incorrecto, o una escritura no tiene ningún efecto.

**Causa**: Un desplazamiento en la cabecera del driver difiere en uno, dos o cuatro bytes del valor de la hoja de datos.

**Diagnóstico**: Compara la cabecera con la hoja de datos, registro a registro.

**Solución**: Corrige el desplazamiento.

### 2. Ancho de acceso incorrecto

**Síntoma**: Una lectura devuelve un valor que parece ser solo una parte del registro, o una escritura afecta solo a una parte del registro.

**Causa**: El driver usa `bus_read_4` en un registro de 16 bits o viceversa.

**Diagnóstico**: Compara la columna de ancho de la hoja de datos con el sufijo del acceso.

**Solución**: Usa el ancho correcto.

### 3. Calificador `volatile` ausente en un acceso escrito a mano

**Síntoma**: El compilador elimina un acceso a registro mediante optimización y el driver no detecta un cambio de estado.

**Causa**: Un driver que envuelve `bus_space_*` en una variable intermedia no `volatile` pierde la anotación `volatile`.

**Diagnóstico**: Revisa cualquier acceso personalizado que no sea una llamada directa a `bus_space_*`.

**Solución**: Mantén los accesos como envolturas simples alrededor de `bus_space_*`; no introduzcas variables intermedias sin `volatile`.

### 4. Actualización perdida en lectura-modificación-escritura

**Síntoma**: Un bit establecido por el driver desaparece; otro bit establecido por un segundo contexto desaparece también.

**Causa**: Dos contextos realizan RMW en el mismo registro sin un lock; uno sobreescribe al otro.

**Diagnóstico**: Usa el registro de accesos o DTrace para observar dos escrituras en rápida sucesión.

**Solución**: Protege el RMW con el mutex del driver, o usa el idioma de escribir-uno-para-borrar si el hardware lo admite.

### 5. Barrera ausente antes de un doorbell

**Síntoma**: El dispositivo lee a veces datos de descriptor obsoletos o buffers de comandos incorrectos.

**Causa**: Las escrituras en el descriptor son reordenadas más allá de la escritura en el registro doorbell (en arm64 u otras plataformas de ordenamiento débil).

**Diagnóstico**: El síntoma suele ser transitorio y dependiente de la carga.

**Solución**: Inserta `bus_barrier` con `BUS_SPACE_BARRIER_WRITE` entre las escrituras en el descriptor y el doorbell.

### 6. Lectura de un registro de solo escritura

**Síntoma**: El driver lee un registro y obtiene cero o datos basura; basándose en ese valor, toma la acción incorrecta.

**Causa**: El registro está marcado como de solo escritura en la hoja de datos; las lecturas devuelven un valor fijo no relacionado con el estado.

**Diagnóstico**: Comprueba el tipo de acceso en la hoja de datos.

**Solución**: No leas registros de solo escritura. Si necesitas recordar el último valor escrito, guárdalo en caché en el softc.

### 7. Efecto secundario inesperado en la lectura

**Síntoma**: Una lectura de depuración cambia el comportamiento del driver.

**Causa**: El registro tiene semántica de leer-para-borrar y la lectura de depuración consume un evento.

**Diagnóstico**: Desactiva la lectura de depuración; si el problema desaparece, la lectura era la causa.

**Solución**: Guarda el valor en caché en el softc durante la lectura dictada por el protocolo; expón el valor en caché a través de la interfaz de depuración.

### 8. Etiqueta o handle colgante

**Síntoma**: Kernel panic en el primer acceso a registro, con un fallo en una dirección que no parece pertenecer a la región mapeada.

**Causa**: El driver almacenó una etiqueta y un handle antes de que la asignación se completara, o los conservó después de la liberación.

**Diagnóstico**: `MYFIRST_ASSERT` activándose; `regs_buf == NULL` en el panic.

**Solución**: Establece la etiqueta y el handle solo tras una asignación satisfactoria; bórralos (o pon a null el puntero `sc->hw`) antes de la liberación.

### 9. Manejador de sysctl sin lock

**Síntoma**: Aviso de WITNESS sobre un acceso a registro no protegido, o valores incorrectos observados en raras ocasiones desde el espacio de usuario.

**Causa**: Un manejador de sysctl lee o escribe en un registro sin adquirir el lock del driver.

**Diagnóstico**: El `MYFIRST_ASSERT` dentro del acceso genera una entrada de WITNESS.

**Solución**: Envuelve el acceso al registro entre `MYFIRST_LOCK`/`MYFIRST_UNLOCK`.

### 10. Condiciones de carrera en detach

**Síntoma**: Kernel panic durante `kldunload` con una pila que incluye un acceso a registro.

**Causa**: Un callout o una tarea accede a registros después de que el buffer de registros haya sido liberado.

**Diagnóstico**: `regs_buf == NULL` en el panic; el llamante es una tarea o callout que no fue drenado antes de la liberación.

**Solución**: Revisa el orden de detach; drena todos los callouts y tareas antes de liberar `regs_buf`.

Cada uno de estos errores tiene una ruta de diagnóstico corta y una solución bien definida. Tener esta lista a mano durante el desarrollo permite detectar la mayoría de los problemas en el primer contacto.
