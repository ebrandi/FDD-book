---
title: "Concurrencia en drivers"
description: "Qué es realmente la concurrencia dentro de un driver, de dónde proviene, cómo puede romper el código y las dos primeras primitivas que el kernel de FreeBSD te ofrece para dominarla: atomics y mutexes."
partNumber: 3
partName: "Concurrency and Synchronization"
chapter: 11
lastUpdated: "2026-04-18"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 210
language: "es-ES"
---
# Concurrencia en drivers

## Orientación para el lector y objetivos

El capítulo 10 concluyó con una nota de optimismo tranquilo. Tu driver `myfirst` ya dispone de un buffer circular real, gestiona correctamente las lecturas y escrituras parciales, se duerme cuando no hay nada que hacer, se despierta cuando sí lo hay, se integra con `select(2)` y `poll(2)`, y puede someterse a pruebas de estrés con `dd`, `cat` y un pequeño arnés en userland. Si lo cargas hoy y lo ejercitas con el kit de pruebas de la etapa 4, se comporta bien. Los recuentos de bytes coinciden. Los checksums concuerdan. `dmesg` permanece en silencio.

Al observar ese comportamiento, resulta tentador concluir que el driver es correcto. Está *casi* correcto. Lo que falta no es código. Lo que falta es una explicación de *por qué* funciona el código que tenemos.

Detente un momento en eso. En todos los capítulos anteriores hemos justificado cada decisión del código mostrando qué hace: `uiomove(9)` mueve bytes a través del límite de confianza, `mtx_sleep(9)` libera el mutex y se duerme, `selwakeup(9)` notifica a los waiters de `poll(2)`. Los capítulos demostraron que el código es razonable. No demostraron que el código sea *seguro bajo acceso concurrente*. El capítulo 10 apuntó hacia el tema: adquirió el mutex alrededor de las lecturas y escrituras del buffer, lo liberó antes de llamar a `uiomove(9)` y respetó la regla de no dormir mientras se mantiene un mutex no durmiente. Pero esos movimientos se presentaron como patrones a imitar, no como conclusiones de un argumento cuidadoso. El argumento es la tarea de este capítulo.

En este capítulo, la concurrencia se convierte en el tema principal.

Al terminar, comprenderás por qué existe el mutex en el softc; qué saldría mal si no estuviera; qué es en realidad una condición de carrera, hasta el nivel de instrucción; qué te aportan las operaciones atómicas y qué no; cómo se combinan los mutexes con el sleep, con las interrupciones y con las herramientas de verificación de corrección del kernel; y cómo determinar, leyendo un driver, si su historia de concurrencia es honesta. También harás lo que el capítulo 11 de este libro siempre ha estado pensado para hacer: tomar el driver que llevas construyendo y llevarlo a una thread safety apropiada, documentada y verificable. No vamos a poner tu driver patas arriba; la forma que tenemos es buena. Lo que haremos es ganarnos cada uno de sus locks.

Este capítulo no pretende enseñar todo el kit de herramientas de sincronización de FreeBSD. Eso sería poco razonable en este punto del aprendizaje. El capítulo 12 está dedicado al resto del kit: variables de condición, locks durmientes compartido-exclusivos, timeouts y los aspectos más profundos del orden de locks. El capítulo 11 es el capítulo conceptual que hace posible el capítulo 12. Nos ceñimos a dos primitivas, las operaciones atómicas y los mutexes, y dedicamos el capítulo a aprenderlas bien.

### Por qué este capítulo tiene entidad propia

En principio, sería posible saltar directamente al capítulo 12 y enseñar de golpe los locks durmientes, las variables de condición y el framework `WITNESS`. Muchos tutoriales de drivers hacen exactamente eso. El resultado es código que funciona en el portátil del autor y falla en el escritorio de cuatro núcleos del lector, porque el lector no entiende para qué sirven las primitivas. Imitan la forma y pierden el fondo.

El coste de ese tipo de malentendido es alto. Los bugs de concurrencia son la clase de bug más difícil que existe. Se reproducen de forma intermitente. Pueden aparecer en una máquina y no en otra. Pueden permanecer latentes en un driver durante años antes de que un cambio en el planificador, una CPU más rápida o una nueva opción del kernel los ponga en evidencia. Cuando golpean, el síntoma casi siempre está en un lugar distinto a la causa. El lector que solo ha leído la sintaxis de `mtx_lock` puede quedarse mirando un panic durante días antes de darse cuenta de que el bug está a dos funciones de distancia, en un lugar donde nadie pensó que la concurrencia fuera un problema.

La única defensa fiable contra esto es un modelo mental claro, construido desde los primeros principios. Construimos ese modelo en este capítulo, paso a paso y con cuidado. Al final, `mtx_lock` y `mtx_unlock` no serán líneas que copies de otro driver. Serán líneas cuya ausencia sabrás detectar.

### El estado en que dejó el driver el capítulo 10

Un repaso rápido del estado desde el que deberías estar trabajando. Si alguno de los siguientes elementos falta en tu driver, detente aquí y vuelve al capítulo 10 antes de continuar.

- Un hijo Newbus registrado bajo `nexus0`, probado y enganchado al cargar el módulo.
- Una `struct myfirst_softc` que contiene una `struct cbuf` (el buffer circular), estadísticas por descriptor y un único `struct mtx` llamado `sc->mtx`.
- Dos campos `struct selinfo` (`sc->rsel` y `sc->wsel`) para la integración con `poll(2)`.
- Manejadores `myfirst_read`, `myfirst_write`, `myfirst_poll` que mantienen `sc->mtx` mientras acceden al estado compartido y lo liberan antes de llamar a `uiomove(9)` o `selwakeup(9)`.
- `mtx_sleep(9)` con `PCATCH` como primitiva de bloqueo, y `wakeup(9)` como llamada complementaria.
- Una ruta de detach que se niega a descargarse mientras hay descriptores abiertos y que despierta a los durmientes antes de liberar el estado.

Ese driver es la base sobre la que razona el capítulo 11. No cambiaremos su comportamiento observable en este capítulo. Entenderemos *por qué* funciona, haremos explícitas sus afirmaciones de seguridad y añadiremos la infraestructura que permite al kernel verificarlas por nosotros en tiempo de ejecución.

### Qué aprenderás

Al terminar este capítulo, deberías ser capaz de:

- Definir la concurrencia en el sentido específico que importa dentro de un driver, y distinguirla del paralelismo de una manera que resiste el contacto con código real.
- Enumerar los contextos de ejecución que encuentra un driver en un sistema FreeBSD moderno y razonar sobre cuáles de ellos pueden ejecutarse simultáneamente.
- Nombrar los cuatro modos de fallo más comunes del estado compartido no sincronizado y reconocer cada uno de ellos en un fragmento de código breve.
- Examinar paso a paso un driver deliberadamente roto que corrompe su propio buffer bajo carga, y explicar la corrupción en términos de qué instrucción de qué thread gana la carrera.
- Realizar una auditoría de concurrencia sobre un fragmento de código de driver: identificar el estado compartido, marcar las secciones críticas, clasificar cada acceso y decidir qué lo protege.
- Explicar qué son las operaciones atómicas al nivel que las proporciona el hardware, qué primitivas atómicas expone FreeBSD a través de `atomic(9)`, cuándo son suficientes para el problema en cuestión y cuándo no lo son.
- Usar la API `counter(9)` para estadísticas por CPU que escalan bajo contención.
- Distinguir entre las dos formas fundamentales de mutex en FreeBSD (`MTX_DEF` y `MTX_SPIN`), explicar cuándo es apropiado cada uno y describir las reglas que se aplican a cada uno.
- Leer e interpretar un aviso de `WITNESS`, entender qué significa la inversión del orden de locks y diseñar una jerarquía de locks que `WITNESS` acepte.
- Enunciar y aplicar la regla sobre dormir con un mutex mantenido, incluidos los casos sutiles que involucran a `uiomove(9)`, `copyin(9)` y `copyout(9)`.
- Describir la inversión de prioridad, la propagación de prioridad y cómo el kernel resuelve la primera mediante la segunda.
- Construir un pequeño conjunto de programas de prueba multihilo y multiproceso capaces de ejercitar tu driver con niveles de concurrencia muy superiores a los que pueden generar `cat`, `dd` y un único shell.
- Usar `INVARIANTS`, `WITNESS`, `mtx_assert(9)`, `KASSERT` y `dtrace(1)` juntos para verificar que las afirmaciones que haces en los comentarios son las que el código realmente impone.
- Refactorizar el driver hasta una forma en la que la disciplina de bloqueo esté documentada, el propietario del lock de cada fragmento de estado compartido sea evidente a partir del código fuente, y un lector futuro (incluido tu yo futuro) pueda auditar la historia de concurrencia en minutos.

Es una lista sustancial. También es el alcance realista de lo que un lector cuidadoso puede absorber en un capítulo en esta etapa del libro. Tómatelo con calma.

### Qué no cubre este capítulo

Varios temas rozan la concurrencia y se posponen deliberadamente:

- **Variables de condición (`cv(9)`).** El capítulo 12 presenta `cv_wait`, `cv_signal`, `cv_broadcast` y `cv_timedwait`, y explica cuándo una variable de condición con nombre es un diseño más limpio que un canal `wakeup(9)` simple. Este capítulo sigue usando `mtx_sleep` y `wakeup`, exactamente igual que el capítulo 10.
- **Locks durmientes compartido-exclusivos (`sx(9)`) y locks de lectura/escritura (`rw(9)`, `rm(9)`).** El capítulo 12 cubre estas primitivas y las situaciones para las que están diseñadas. En el capítulo 11 nos quedamos con el par de mutexes que ya usamos.
- **Estructuras de datos lock-free.** El kernel de FreeBSD usa colas lock-free (`buf_ring(9)`), lectores de lectura mayoritaria basados en `epoch(9)` y patrones similares a los hazard pointers en varios lugares. Son herramientas especializadas que recompensan el estudio cuidadoso y se presentan gradualmente en la parte 4 y en el capítulo de red de la parte 6 (capítulo 28) cuando los drivers reales las necesitan.
- **Corrección de la concurrencia bajo contexto de interrupción.** Nuestro driver todavía no tiene interrupciones de hardware reales. El capítulo 14 presenta los manejadores de interrupción en detalle; ese capítulo también amplía el debate sobre concurrencia para cubrir qué ocurre cuando un manejador puede ejecutarse en cualquier instante y qué implica eso para las decisiones de bloqueo. El capítulo 11 se restringe al contexto de proceso y a los kernel threads.
- **Configuración avanzada de `WITNESS`.** Cargaremos `WITNESS` en un kernel de depuración y leeremos sus avisos. No construiremos clases de lock personalizadas ni exploraremos el conjunto completo de opciones de `WITNESS`.

Mantenernos dentro de esas líneas mantiene el capítulo honesto. Un capítulo sobre concurrencia que intenta enseñar cada primitiva de sincronización de FreeBSD es un capítulo que no enseña ninguna bien. Enseñamos dos primitivas, las enseñamos bien y preparamos el siguiente capítulo para enseñar el resto desde una base sólida.

### Tiempo estimado de dedicación

- **Solo lectura**: dos horas y media o tres horas, según la frecuencia con la que te detengas en los diagramas. Este capítulo tiene una carga conceptual mayor que el capítulo 10.
- **Lectura más realización de los laboratorios y los drivers rotos y corregidos**: entre seis y ocho horas en dos sesiones con un reinicio entre medias.
- **Lectura más todos los laboratorios y desafíos**: entre diez y catorce horas en tres o cuatro sesiones. Los desafíos están diseñados explícitamente para ampliar tu comprensión, no solo para consolidarla.

No te precipites. Si te sientes inseguro a mitad de la sección 4, es normal; las operaciones atómicas son un modelo mental genuinamente nuevo para la mayoría de los lectores. Para, estírate y vuelve. El valor de este capítulo se multiplica cuando el lector lo toma con calma.

### Prerrequisitos

Antes de comenzar este capítulo, comprueba:

- El código fuente de tu driver es equivalente al ejemplo de la etapa 4 del capítulo 10 en `examples/part-02/ch10-handling-io-efficiently/stage4-poll-refactor/`. Si no lo es, vuelve al capítulo 10 y ponlo al día. El código de este capítulo da por supuesto el código fuente de la etapa 4.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con un `/usr/src` correspondiente. Las APIs y las rutas de archivo citadas aquí se alinean con esa versión.
- Dispones de una configuración de kernel de desarrollo que habilita `INVARIANTS`, `WITNESS` y `DDB`. Si no la tienes, la sección 7 te guía paso a paso para construirla.
- Te sientes cómodo cargando y descargando tu propio módulo, observando `dmesg` y leyendo `sysctl dev.myfirst.0.stats` para verificar el estado del driver.
- Leíste el capítulo 10 con atención y comprendiste por qué `mtx_sleep` recibe el mutex como su interlock. Si esa regla todavía te parece arbitraria, detente y vuelve a leer la sección 5 del capítulo 10 antes de comenzar este capítulo.

Si alguno de los puntos anteriores no está bien asentado, corregirlo ahora es una mejor inversión que avanzar por el capítulo 11 con lagunas.

### Cómo sacar el máximo partido de este capítulo

Tres hábitos dan frutos rápidamente.

En primer lugar, ten `/usr/src/sys/kern/kern_mutex.c` y `/usr/src/sys/kern/subr_witness.c` a mano. El primero es donde viven las primitivas mutex; el segundo es donde vive el verificador de orden de locks del kernel. No necesitas leer estos archivos en su totalidad, pero varias veces durante el capítulo señalaremos una función concreta y te pediremos que eches un vistazo al código real durante un minuto. Ese minuto da sus frutos.

En segundo lugar, lee `/usr/src/sys/sys/mutex.h` una vez, ahora, antes de empezar la sección 5. El archivo de cabecera es corto, los comentarios son buenos, y ver la tabla completa de flags `MTX_*` de una sola vez hace que la discusión posterior resulte concreta en lugar de abstracta.

En tercer lugar, construye y arranca un kernel de depuración antes del Laboratorio 1. Los laboratorios de este capítulo dan por sentado que `INVARIANTS` y `WITNESS` están habilitados, y varios de ellos están diseñados para producir avisos específicos que solo se pueden ver con esas opciones activas. Si tienes que reconstruir el kernel a mitad del capítulo, la interrupción te costará más tiempo que haberlo hecho desde el principio.

### Recorrido por el capítulo

Las secciones, en orden, son:

1. Por qué importa la concurrencia. De dónde surge la concurrencia dentro del kernel, por qué no es lo mismo que el paralelismo y cómo son los cuatro modos de fallo fundamentales.
2. Condiciones de carrera y corrupción de datos. Una disección cuidadosa de qué es una condición de carrera, a nivel de instrucción, con un ejemplo trabajado sobre el buffer circular y una versión deliberadamente insegura del driver que vas a ejecutar de verdad.
3. Análisis del código no seguro en tu driver. Un procedimiento de auditoría práctico. Recorremos el código fuente de la Fase 4 del Capítulo 10, identificamos cada pieza de estado compartido, clasificamos su visibilidad y documentamos qué la protege.
4. Operaciones atómicas. Qué es la atomicidad a nivel de hardware, qué ofrece la API `atomic(9)`, cómo el orden de memoria afecta a la corrección y cuándo las operaciones atómicas son suficientes frente a cuándo no lo son. Aplicamos operaciones atómicas a las estadísticas por descriptor del driver.
5. Introducción a los locks (mutexes). Las dos formas fundamentales de mutex, las reglas que se aplican a cada una, la jerarquía de locks, la inversión de prioridad, los deadlocks y la regla de dormir con mutex. Reexaminamos la elección del mutex del Capítulo 10 con el conjunto conceptual completo.
6. Pruebas de acceso multi-thread. Cómo construir programas en el espacio de usuario que ejerciten tu driver de forma concurrente, usando `fork(2)`, `pthread(3)` y arneses personalizados. Cómo registrar los resultados de pruebas concurrentes sin perturbarlas.
7. Depuración y verificación de la seguridad en threads. Lectura de una advertencia de `WITNESS`, uso de `INVARIANTS`, adición de llamadas a `mtx_assert(9)` y `KASSERT(9)`, y rastreo con `dtrace(1)`.
8. Refactorización y versionado. Organización del código para mayor claridad, versionado del driver, documentación de la estrategia de locking en un README, análisis estático y pruebas de regresión.

A continuación vienen laboratorios prácticos y ejercicios de desafío, seguidos de una referencia para resolución de problemas, una sección de cierre y un puente hacia el Capítulo 12.

Si es tu primera lectura, avanza de forma lineal y realiza los laboratorios en orden. Si revisas el material para consolidarlo, la sección de depuración y la sección de refactorización son autónomas.



## Sección 1: Por qué importa la concurrencia

Todos los capítulos hasta este punto han tratado el driver como si viviera en una única línea temporal. Un programa de usuario llama a `read(2)`; el kernel encamina la llamada hacia `myfirst_read`; el manejador realiza su trabajo; el control regresa; el programa continúa. Una segunda llamada a `read(2)` sobre otro descriptor se describe como "otra llamada", como si ocurriera en un momento posterior dentro de la misma historia.

Este enfoque era una aproximación. Nos permitió centrarnos en el camino de datos sin enredarnos en la cuestión de quién llamaba a nuestro manejador y cuándo. La aproximación funcionó bien para los Capítulos 7 al 10, porque las salvaguardas que pusimos en marcha (un único mutex, una adquisición cuidadosa del lock en torno al estado compartido, `mtx_sleep` como único mecanismo de espera) eran lo bastante sólidas como para ocultar la complejidad de la ejecución real del kernel.

Esa complejidad no es algo que hayamos inventado. Es el entorno real en el que vive todo driver de FreeBSD, en todo momento. En esta sección levantamos el telón.

### Qué significa la concurrencia en el kernel

La **concurrencia** es la propiedad de un sistema en el que múltiples flujos de ejecución independientes pueden avanzar durante períodos de tiempo solapados. La palabra clave es *independiente*: cada flujo tiene su propia pila, su propio puntero de instrucción, su propio estado de registros y su propia razón para hacer lo que hace. Pueden compartir algunos datos (por eso nos importa la concurrencia), pero ninguno espera a que otro termine una tarea completa antes de comenzar.

Dentro del kernel de FreeBSD, estos flujos de ejecución independientes provienen de varias fuentes distintas. Un driver suele encontrarse con al menos cuatro de ellos, y posiblemente con todos a la vez:

**Procesos del espacio de usuario que llaman al driver.** Esta es la fuente que has visto con mayor frecuencia en el libro hasta ahora. Un programa de usuario abre `/dev/myfirst`, emite un `read(2)` o un `write(2)`, y el camino del syscall entra en el driver a través de devfs. Cada una de esas llamadas es un flujo de ejecución independiente, con su propia pila del kernel, que se ejecuta en la CPU que el planificador le haya asignado. Dos llamantes concurrentes son dos flujos concurrentes, tan independientes entre sí como dos procesos no relacionados que se ejecutan en el espacio de usuario.

**Threads del kernel creados por el driver o el subsistema.** Algunos drivers crean sus propios threads del kernel con `kproc_create(9)` o `kthread_add(9)` para realizar tareas periódicas, en segundo plano o de larga duración. El driver `myfirst` no hace esto todavía, pero muchos drivers reales sí; un driver de bus USB tiene un thread de sondeo del hub, un driver de red tiene un thread de finalización de TX y un driver de almacenamiento tiene un thread de finalización de I/O. Cada uno de ellos se ejecuta de forma concurrente con los manejadores de syscalls.

**Ejecución de callout y taskqueue.** El mecanismo `callout(9)` del kernel (Capítulo 13) y su mecanismo `taskqueue(9)` (Capítulo 16) ejecutan funciones en momentos no desencadenados directamente por un syscall de usuario. Un callout programado para dentro de un segundo se disparará dentro de un segundo, independientemente de lo que estén haciendo en ese momento los manejadores del driver. El manejador se ejecuta en una pila diferente, a menudo en una CPU diferente, y puede acceder al mismo estado que está tocando el manejador del syscall.

**Contexto de interrupción.** Cuando hay hardware real presente, un manejador de interrupción (Capítulo 14) se ejecuta en respuesta a un evento externo: llegó un paquete de red, se disparó un temporizador, se completó una transferencia USB. Las interrupciones son asíncronas y apropiativas: el manejador de interrupción puede comenzar a ejecutarse en cualquier instante que el hardware decida. El código que desaloja puede estar en medio de una actualización crítica del estado compartido. `myfirst` no tiene hardware real todavía, pero hablaremos del contexto de interrupción porque condiciona algunas de las decisiones que tomamos.

Un driver de FreeBSD en producción interactúa habitualmente con al menos tres de los cuatro. Un desarrollador de drivers que trate cualquiera de ellos como "lo único que se está ejecutando" habrá escrito un driver que fallará en cuanto se enfrente a carga real.

La consecuencia práctica es que cada línea de tu código de driver está rodeada por otro código que también podría estar ejecutándose. Cuando tu `myfirst_read` tiene `sc->mtx` adquirido, es completamente normal que una CPU diferente esté intentando adquirir el mismo mutex desde `myfirst_write`, desde un temporizador o desde otro llamante independiente. El trabajo del kernel es hacer que esa interacción sea segura; el trabajo de tu driver es cooperar. Esa cooperación es lo que enseña este capítulo.

### De dónde vienen los flujos: una imagen concreta

Conviene analizar un escenario concreto. Imagina que tienes una máquina con FreeBSD 14.3 y cuatro núcleos de CPU. Has cargado el driver `myfirst` de la Fase 4 del Capítulo 10. Lanzas los siguientes cinco comandos, cada uno en su propia terminal, aproximadamente al mismo tiempo:

1. `cat /dev/myfirst > /tmp/out1.txt`
2. `cat /dev/myfirst > /tmp/out2.txt`
3. `dd if=/dev/zero of=/dev/myfirst bs=512 count=10000`
4. `sysctl -w dev.myfirst.0.debug=1` (si el debug está activado)
5. `while true; do sysctl dev.myfirst.0.stats; sleep 0.1; done`

En un instante dado, esto es lo que podría estar haciendo el kernel:

- CPU 0 ejecuta el primer `cat`, dentro de `myfirst_read`, manteniendo `sc->mtx` adquirido, realizando un `cbuf_read` en su buffer de rebote en pila.
- CPU 1 ejecuta el segundo `cat`, dentro de `myfirst_read`, esperando en `mtx_sleep(&sc->cb, &sc->mtx, ...)` porque intentó adquirir el mutex después del primer `cat` y se durmió.
- CPU 2 ejecuta `dd`, dentro de `myfirst_write`, habiendo terminado un `uiomove` en su buffer de rebote y a punto de readquirir `sc->mtx` para depositar los bytes en el cbuf.
- CPU 3 ejecuta el bucle `sysctl`, dentro del manejador sysctl de `cb_used`, intentando adquirir `sc->mtx` para obtener un snapshot consistente.

Cuatro CPUs. Cuatro flujos de ejecución. Cuatro partes distintas del driver, todas ejecutándose de forma concurrente, todas compitiendo por el mismo mutex, todas necesitando coincidir en el estado del mismo buffer circular. Esta no es una situación artificial. Así es como luce un sistema con carga moderada durante una prueba ordinaria.

Si el mutex y las reglas que lo rodean no existieran, cada uno de esos cuatro threads sería libre de observar y modificar los índices del cbuf (`cb_head`, `cb_used`) y su memoria de respaldo (`cb_data`) de forma independiente. Los resultados serían, aproximadamente, lo que el sistema de memoria del hardware produjera por casualidad, sin ninguna garantía de corrección. El resto de este capítulo trata de construir una comprensión de por qué eso sería una catástrofe y qué hacen realmente los primitivos que lo evitan.

### Concurrencia frente a paralelismo

Estos dos términos se usan a menudo de forma intercambiable. Están relacionados pero no son lo mismo, y aclararlo desde el principio evita confusiones más adelante.

El **paralelismo** es una propiedad de la ejecución: dos cosas ocurren literalmente al mismo tiempo físico, en piezas de hardware distintas. En un sistema multi-CPU, dos threads que se ejecutan en dos núcleos distintos en el mismo nanosegundo son paralelos.

La **concurrencia** es una propiedad del diseño: el sistema está estructurado de modo que existen múltiples flujos de ejecución independientes que pueden avanzar en cualquier orden. Si se ejecutan físicamente al mismo tiempo es una cuestión aparte.

Una CPU de un solo núcleo puede ejecutar un programa concurrente (el planificador del sistema operativo intercala dos threads en ese único núcleo). No puede ejecutar un programa paralelo (nada ocurre literalmente de forma simultánea). Una máquina de cuatro núcleos puede ejecutar programas tanto concurrentes como paralelos.

¿Por qué importa esta distinción para quien desarrolla drivers? Porque los errores que nos importan están causados por la concurrencia, no por el paralelismo. Un programa de dos threads en un portátil FreeBSD de un solo núcleo puede exhibir todas las condiciones de carrera que encontraremos en este capítulo, siempre que el planificador decida en algún momento desalojar un thread mientras está en medio de una actualización. El paralelismo, en una máquina multi-núcleo, simplemente hace que esos errores sean más frecuentes y más difíciles de suprimir ejecutando el programa lentamente. El problema fundamental es anterior al hardware multi-núcleo y existirá en cualquier máquina que pueda desalojar un thread.

Nuestro enfoque, por tanto, se centra en diseños que toleren cualquier intercalado legal de flujos independientes. Si el diseño lo tolera, también tolera el paralelismo. Si el diseño asume "esto siempre se ejecutará antes que aquello", fallará en algún sistema, probablemente el primer servidor en producción con el que se encuentre.

Esta es también la razón por la que las pruebas por sí solas no pueden garantizarte la corrección en concurrencia. Puedes probar el código y no encontrar ningún error porque el intercalado que lo desencadena nunca se produjo durante la prueba. Puedes desplegar el mismo código y encontrar el error el primer día, porque algún pequeño cambio en la planificación, una CPU ligeramente más rápida o un número diferente de núcleos altera la temporización. La única forma duradera de abordar la concurrencia es razonar sobre cada intercalado legal posible, no intentar enumerar los probables.

### Qué puede salir mal: los cuatro modos de fallo

Cuando múltiples flujos de ejecución comparten datos sin coordinación, pueden ocurrir cuatro tipos de problemas. Todo error de concurrencia que encuentres será una instancia especializada de uno de estos cuatro. Nombrarlos desde el principio es útil, porque más adelante en el capítulo, cuando aparezca un error concreto en un driver concreto, reconocerás a qué familia pertenece.

**Modo de fallo 1: corrupción de datos.** El estado tras el intercalado no es el que ninguno de los threads habría producido por sí solo. Un contador compartido termina con un valor al que ninguna secuencia correcta de incrementos podría haber llegado. Una lista enlazada tiene una entrada que apunta a basura. El índice `head` de un buffer es mayor que su capacidad. Este es el tipo más común de error de concurrencia, y el más difícil de detectar, porque el estado corrupto puede permanecer latente durante mucho tiempo antes de que se utilice.

**Modo de fallo 2: actualizaciones perdidas.** Dos threads leen cada uno un valor compartido, cada uno calcula un nuevo valor a partir de lo que ha leído y cada uno almacena su resultado. La segunda escritura sobreescribe la primera, lo que significa que la actualización del primer thread se descarta como si nunca hubiera ocurrido. Ejemplo clásico: dos sentencias `bytes_read += got;`, ejecutadas por dos threads, pueden incrementar el contador en uno en lugar de en dos si el entrelazado es desafortunado. Las actualizaciones perdidas son un caso especial de corrupción de datos con una firma predecible: el contador es correcto en su forma, pero bajo en magnitud.

**Modo de fallo 3: valores rotos.** Un valor demasiado grande para que la CPU lo lea o escriba en un único acceso a memoria (en sistemas de 32 bits, habitualmente un entero de 64 bits) puede observarse en un estado a medias por un lector concurrente. Un thread está a mitad de escribir los bytes 0 a 3 de un `uint64_t`; otro thread lee los 8 bytes y ve la mitad alta antigua pegada a la mitad baja nueva. El valor resultante carece de sentido. En las plataformas principales de FreeBSD 14.3 (amd64, arm64), los accesos de 64 bits alineados son atómicos; en plataformas de 32 bits y para estructuras más grandes, no lo son. Un valor roto es el lado de «lectura» de una actualización perdida y a veces puede observarse incluso cuando los escritores son correctos.

**Modo de fallo 4: estado compuesto inconsistente.** Una estructura de datos tiene un invariante que relaciona varios campos. Un thread está actualizando un campo pero aún no ha actualizado el otro. Un lector concurrente ve la estructura en un estado que viola el invariante, toma una decisión basada en lo que ha visto y produce un resultado incorrecto. Este es el modo más sutil de los cuatro, porque requiere que el observador conozca el invariante que se supone que debe cumplirse; un thread que no conoce el invariante no puede detectar que se ha violado. La mayoría de los bugs más difíciles de los drivers implican un estado compuesto inconsistente: el head y el tail de un buffer circular se actualizaron en dos pasos, y un lector vio el estado a medias y produjo basura.

Los cuatro modos de fallo comparten una causa: *varios flujos de ejecución accedieron al estado compartido sin acordar un orden*. Las primitivas que introduce este capítulo, los atomics y los mutex, son formas distintas de imponer ese acuerdo. Los atomics hacen que las operaciones individuales sean indivisibles. Los mutex imponen un acceso serializado en regiones de código más extensas.

### Un experimento mental

Considera un contador compartido trivial:

```c
static u_int shared_counter = 0;

/* Called by anything. */
static void
bump(void)
{
        shared_counter = shared_counter + 1;
}
```

Si dos threads llaman a `bump()` al mismo momento, ¿qué ocurre?

El comportamiento correcto es que `shared_counter` aumente en dos. Cada llamada debería sumar uno, así que dos llamadas deberían sumar dos.

El comportamiento real depende de cómo se compila el incremento. En amd64, `shared_counter = shared_counter + 1;` se compila normalmente en tres instrucciones de máquina: una carga del valor actual en un registro, una suma de uno al registro y un almacenamiento del registro de vuelta en memoria.

Imagina dos threads, A y B, ejecutándose en dos CPUs distintas, ambos entrando en `bump()` en esencialmente el mismo instante. Un entrelazado válido tiene este aspecto:

| Tiempo | Thread A                       | Thread B                       | Memoria |
|--------|--------------------------------|--------------------------------|---------|
| t0     |                                |                                | 0       |
| t1     | carga (obtiene 0)              |                                | 0       |
| t2     |                                | carga (obtiene 0)              | 0       |
| t3     | suma 1 (registro local=1)      |                                | 0       |
| t4     |                                | suma 1 (registro local=1)      | 0       |
| t5     | almacena (escribe 1)           |                                | 1       |
| t6     |                                | almacena (escribe 1)           | 1       |

Ambos threads cargan cero. Ambos calculan uno. Ambos almacenan uno. El valor final es uno. El contador se ha incrementado exactamente una vez, aunque dos threads llamaron cada uno a `bump()`.

Esto es una actualización perdida. Es el bug de concurrencia más sencillo del libro. No requiere hardware especial, una estructura de datos complicada ni una decisión poco frecuente del planificador. Solo requiere que dos threads ejecuten la secuencia de tres instrucciones en momentos solapados.

Observa tres cosas sobre este ejemplo.

En primer lugar, el bug no está en el código fuente. El código C dice `shared_counter = shared_counter + 1;`, que es exactamente lo que queríamos. El bug reside en la *interacción entre el código fuente y el modelo de ejecución del hardware*. Por más que mires la línea C, el bug no aparece, porque está en un nivel de abstracción que esa línea no expresa.

En segundo lugar, el bug no es reproducible con pruebas de un solo thread. Un thread que llame a `bump()` mil veces siempre producirá `shared_counter == 1000`. Añade un segundo thread y el resultado se vuelve no determinista. Podrías ejecutar la prueba con dos threads cien veces y obtener siempre resultados correctos; ejecútala de nuevo en otra CPU o con una carga de trabajo distinta y verás un conteo perdido.

En tercer lugar, la solución no es añadir más código cuidadoso en C. La solución es bien hacer el incremento *atómico* (que se ejecute como una unidad indivisible desde la perspectiva de la memoria) o bien imponer *exclusión mutua* (que ningún thread pueda estar ejecutando la secuencia de tres instrucciones al mismo tiempo). Los atómicos nos ofrecen la primera opción. Los mutexes nos ofrecen la segunda. Son formas distintas de solucionar el mismo problema, y elegir entre ellas es el tema de las secciones 4 y 5.

### Vista previa del driver bajo carga

Para dar concreción al resto del capítulo, vamos a describir lo que haremos con el driver.

En la sección 2, construiremos una versión deliberadamente insegura de `myfirst`: una versión a la que se le ha eliminado el mutex de los manejadores de I/O. La cargaremos, ejecutaremos una prueba con dos procesos y observaremos cómo los invariantes del buffer circular se rompen en tiempo real. Será una experiencia desagradable, pero instructiva. Verás la corrupción de datos, los valores rotos y el estado compuesto inconsistente con tus propios ojos, en tu propio terminal, en un driver con el que llevas trabajando varios capítulos.

En la sección 3, volveremos al driver de la etapa 4 del capítulo 10 y lo auditaremos: cada campo compartido, cada acceso, cada afirmación. Anotaremos el código fuente en el propio archivo. Terminaremos la sección con un documento completo de disciplina de locks.

En la sección 4, introduciremos los atómicos. Convertiremos `sc->bytes_read` y `sc->bytes_written` de contadores `uint64_t` protegidos por mutex a contadores `counter(9)` por CPU, que son libres de locks y escalan bien con muchas CPUs. Hablaremos de dónde ahorran locks los atómicos y dónde no.

En la sección 5, reexaminaremos el mutex desde los primeros principios. Explicaremos por qué el mutex es específicamente un mutex `MTX_DEF` (de espera bloqueante), y no un mutex `MTX_SPIN`. Repasaremos la inversión de prioridad y por qué el mecanismo de propagación de prioridades del kernel nos lo resuelve. Introduciremos `WITNESS` y ejecutaremos el driver inseguro bajo un kernel `WITNESS` para ver qué ocurre.

En la sección 6, construiremos un pequeño arnés de pruebas: un tester multi-thread basado en `pthread`, un tester multiproceso basado en `fork` y un generador de carga que somete al driver a niveles que `dd` y `cat` no pueden alcanzar. Ejecutaremos los drivers seguro e inseguro bajo este arnés.

En la sección 7, aprenderemos a leer los avisos de `WITNESS`, a usar `INVARIANTS` y `KASSERT`, y a desplegar sondas de `dtrace(1)` para observar el comportamiento de concurrencia sin modificarlo.

En la sección 8, refactorizaremos el driver para mayor claridad, lo versionaremos como `v0.4-concurrency`, escribiremos un README que documente la estrategia de locks, ejecutaremos `clang --analyze` sobre el código fuente y realizaremos un test de regresión que repita cada prueba que hayamos construido.

Al final, el driver no será más *funcional* de lo que es ahora. Será más *verificable*. La diferencia importa: el código verificable es el único código que se puede ampliar con seguridad.

### El planificador de FreeBSD en una página

Para entender por qué la concurrencia dentro de un driver no es solo una preocupación teórica, conviene conocer, a grandes rasgos, qué hace el planificador de FreeBSD. No necesitas leer ningún código fuente del planificador para escribir un driver, pero tener un modelo mental de cómo interactúa el planificador con tu driver evita varias clases de sorpresas.

El planificador predeterminado de FreeBSD es **ULE**, implementado en `/usr/src/sys/kern/sched_ule.c`. ULE es un planificador basado en prioridades, consciente de SMP y preparado para múltiples núcleos. Su tarea es decidir, en cada decisión de planificación, qué thread debe ejecutarse en cada CPU.

Las propiedades clave que te importan como desarrollador de drivers son las siguientes:

**La apropiación está permitida en casi cualquier punto.** Un thread que ejecuta código del kernel puede ser apropiado por un thread de mayor prioridad que se vuelva ejecutable (por ejemplo, una interrupción despierta un thread de alta prioridad). Las únicas ventanas donde la apropiación está desactivada son las secciones críticas (entradas con `critical_enter`) y las regiones protegidas por spinlock. Dentro de la sección crítica de un mutex `MTX_DEF`, la apropiación sigue siendo posible; el mecanismo de propagación de prioridades limita el daño.

**Los threads migran entre CPUs.** El planificador moverá threads para equilibrar la carga. Un driver no puede asumir que dos llamadas sucesivas del mismo proceso se ejecuten en la misma CPU.

**La latencia de activación es baja, pero no nula.** Un `wakeup` sobre un thread dormido lo hace ejecutable normalmente en microsegundos. El thread se ejecuta realmente cuando el planificador decide hacerlo, lo cual depende de su prioridad y de qué más esté en condiciones de ejecutarse.

**El planificador mantiene sus propios locks.** Cuando llamas a `mtx_sleep`, en algún momento se adquieren los locks internos del planificador. Esta es la razón por la que `WITNESS` a veces notifica violaciones de orden cuando mantienes un lock del driver mientras haces algo que llega al planificador; el orden entre los locks del driver y los del planificador importa.

Para nuestro driver, esto significa:

- Dos llamantes concurrentes de `myfirst_read` pueden estar ejecutándose en dos CPUs distintas, en la misma CPU mediante apropiación, o de forma secuencial. No asumimos cuál de los tres casos se da.
- El mutex que utilizamos funciona en todos estos escenarios; el planificador garantiza que la semántica de adquisición y liberación del lock se respeta independientemente de la asignación de CPU.
- Cuando se llama a `mtx_sleep`, el thread se duerme rápidamente; cuando se llama a `wakeup`, el thread se vuelve ejecutable rápidamente. La latencia no es una preocupación para la corrección.

Saber que el planificador está ahí, haciendo su trabajo en segundo plano, te libera para concentrarte en las decisiones de concurrencia propias del driver. No tienes que «planificar» tus propios threads ni preocuparte por la afinidad; el kernel se encarga de ello.

### Tipos de concurrencia por nivel pedagógico

Para un principiante, el panorama de la concurrencia puede parecer inmenso. Una jerarquía breve ayuda.

**Nivel 0: Un solo thread.** Sin concurrencia en absoluto. Un thread, una línea de ejecución. Este es el modelo que el lector ha utilizado implícitamente durante la primera mitad de este libro.

**Nivel 1: Concurrencia apropiativa en una sola CPU.** Una CPU, pero el planificador puede interrumpir un thread en cualquier momento y ejecutar otro. Las condiciones de carrera siguen siendo posibles, porque un thread puede estar en medio de una secuencia lectura-modificación-escritura cuando es apropiado. Todos los bugs que trata este capítulo son posibles en el nivel 1.

**Nivel 2: Paralelismo real con múltiples CPUs.** Varias CPUs, múltiples threads ejecutándose literalmente al mismo instante físico. Las condiciones de carrera son más frecuentes y más difíciles de suprimir por suerte, pero son los mismos bugs que en el nivel 1.

**Nivel 3: Concurrencia con contexto de interrupción.** Igual que el nivel 2, pero con la incorporación de que los manejadores de interrupciones pueden apropiarse de threads ordinarios en cualquier momento. Los mutexes de spin se vuelven necesarios para el estado compartido entre el código de la mitad superior y los manejadores de interrupciones. El capítulo 14 y los siguientes tratan esto.

**Nivel 4: Concurrencia con patrones de estilo RCU.** Igual que el nivel 3, pero con patrones avanzados como `epoch(9)` que permiten a los lectores avanzar sin ninguna sincronización. Es un nivel especializado y no necesario para la mayoría de los drivers.

En el capítulo 11, nos encontramos en el nivel 2. Tenemos múltiples CPUs, muchos threads y coordinación ordinaria basada en mutexes. Aquí es donde vive la mayoría de los drivers.

### Parte de la concurrencia es invisible

Hay un punto sutil que hace tropezar a los principiantes: la concurrencia en un driver no se limita a «procesos de usuario que realizan syscalls». Varias fuentes producen ejecución concurrente en tus manejadores:

- **Múltiples descriptores abiertos en el mismo dispositivo.** Cada descriptor tiene su propio `struct myfirst_fh`, pero el `struct myfirst_softc` de todo el dispositivo es compartido. Dos threads con descriptores distintos pueden entrar de forma concurrente en los mismos manejadores sobre el mismo softc.

- **Múltiples threads compartiendo un descriptor.** Dos pthreads del mismo proceso pueden llamar ambos a `read(2)` sobre el mismo descriptor de archivo. Ambos entran en los mismos manejadores con el *mismo* `fh`. Este es el caso más sutil, y la auditoría de la sección 3 lo aborda.

- **Lectores de sysctl.** Un usuario que ejecuta `sysctl dev.myfirst.0.stats` entra en los manejadores de sysctl desde un thread distinto al de los manejadores de I/O. Los manejadores observan el mismo estado compartido.

- **Threads del kernel.** Si una versión futura del driver crea su propio thread del kernel (para timeouts, trabajo en segundo plano, etc.), ese thread es una nueva fuente de acceso concurrente.

- **Callouts.** De modo similar, un manejador de callout se ejecuta por su cuenta, de forma independiente a los manejadores de I/O.

Cada uno de estos es un flujo de ejecución concurrente. Cada uno debe contemplarse en la estrategia de locks. El procedimiento de auditoría de la sección 3 los enumera explícitamente como posibles fuentes de acceso para cada campo compartido.

### Cerrando la sección 1

La concurrencia es el entorno en el que vive todo driver de FreeBSD, piense en ello o no quien lo escribe. Los flujos de ejecución provienen de syscalls de usuario, threads del kernel, callouts e interrupciones. En una máquina multinúcleo son literalmente paralelos; en cualquier máquina son concurrentes, que es lo que importa a los bugs. Cuando múltiples flujos tocan estado compartido sin coordinación, emergen cuatro familias de fallos: corrupción de datos, actualizaciones perdidas, valores rotos y estado compuesto inconsistente.

Aún no hemos dicho qué hacer con nada de esto. Las dos secciones siguientes hacen el trabajo negativo: examinar con detalle preciso qué sale mal. La sección 4 comienza el trabajo positivo, introduciendo el primer primitivo que puede ayudar. La sección 5 introduce el segundo, más general.

Tómate un descanso aquí si lo necesitas. La siguiente sección es más densa.



## Sección 2: Condiciones de carrera y corrupción de datos

La sección 1 describió el problema a nivel intuitivo. Esta sección lo hace concreto. Definiremos con precisión qué es una condición de carrera, veremos cómo surge una en el buffer circular del capítulo 10, construiremos una versión deliberadamente insegura del driver que exhibe el bug y la ejecutaremos en un sistema real. Ver el bug con tus propios ojos es la forma más rápida de desarrollar el instinto que necesitarás para el resto del libro.

### Una definición precisa

Una **condición de carrera** es una situación en la que dos o más flujos de ejecución pueden acceder a un fragmento de estado compartido en ventanas de tiempo solapadas, al menos uno de esos accesos es una escritura, y la corrección del sistema depende del orden exacto de esos accesos.

Tres propiedades hacen que esa definición sea operativa. La primera es el *solapamiento*: los accesos no tienen que ser literalmente simultáneos, basta con que sus ventanas se toquen. Una lectura que empieza en el instante t y termina en t+10, y una escritura que empieza en t+5 y termina en t+15, están solapadas. La segunda es *al menos una escritura*: dos lecturas concurrentes del mismo valor no presentan problema, porque ninguna modifica el estado que la otra está leyendo. La tercera es la *corrección dependiente del orden*: si el código está diseñado de forma que cualquier intercalación produzca el mismo resultado, no existe condición de carrera según esta definición, aunque las ventanas se solapen.

La primera propiedad es la razón por la que el razonamiento de tipo "va demasiado rápido para colisionar" nunca funciona a nivel software. Una lectura y una escritura no son dos puntos; son dos intervalos. Cualquier solapamiento es suficiente.

La segunda propiedad es la razón por la que el código de solo lectura es seguro sin sincronización. Si no se escribe nada, ninguna intercalación puede causar inconsistencia. Esto resultará útil más adelante cuando hablemos de `epoch(9)` y de la lectura sin locks, pero la misma observación justifica por qué ciertas operaciones meramente informativas no necesitan protección.

La tercera propiedad es la razón por la que los locks no son siempre la respuesta. Si la operación está diseñada de forma que cualquier intercalación legal sea aceptable, no se necesita ningún lock. Un contador que solo se incrementa mediante operaciones atómicas, por ejemplo, no necesita un lock alrededor de cada incremento. El lock seguiría funcionando, pero solo desperdiciaría ciclos.

### La anatomía de una condición de carrera

Tomemos el ejemplo del contador de la sección 1 y analicémoslo a nivel de instrucción. La sentencia C:

```c
shared_counter = shared_counter + 1;
```

En amd64, con una compilación ordinaria, esto produce algo parecido a:

```text
movl    shared_counter(%rip), %eax   ; load shared_counter into register EAX
addl    $1, %eax                     ; increment EAX by 1
movl    %eax, shared_counter(%rip)   ; store EAX back to shared_counter
```

El acceso a memoria ocurre en dos momentos distintos: la carga y el almacenamiento. Entre ambos, el valor que se está calculando reside en los registros de la CPU, no en memoria. Cualquier otra CPU que observe la memoria durante ese intervalo ve el valor antiguo, no el valor en tránsito.

Si dos threads, A y B, ejecutan esta secuencia en dos CPUs en tiempos solapados, el controlador de memoria observa cuatro accesos, en algún orden:

1. La carga de A
2. La carga de B
3. El almacenamiento de A
4. El almacenamiento de B

El valor final de `shared_counter` depende por completo del orden en que los almacenamientos se confirman. Si el almacenamiento de A ocurre primero y el de B segundo, el valor final es 1 (porque el almacenamiento de B escribe el valor que B calculó, que se basó en la carga de B, la cual leyó 0). Si el orden se invierte, el valor final sigue siendo 1. No existe ninguna intercalación en la que ambos incrementos surtan efecto, porque las lecturas ocurrieron antes de cualquier escritura, y cada escritura sobrescribe a la otra.

La solución es hacer que toda la secuencia sea atómica (de modo que los cuatro accesos se conviertan en dos transacciones inseparables) o mutuamente exclusiva (de modo que un thread termine las tres instrucciones antes de que el otro empiece alguna). Ambas son soluciones válidas. La diferencia está en lo que cuestan y en lo que nos permiten hacer en el resto del código.

### Cómo aparecen las condiciones de carrera en el buffer circular

El contador compartido es el ejemplo más sencillo posible. El buffer circular del capítulo 10 es un caso más rico. Tiene múltiples campos, invariantes interrelacionados y operaciones que afectan a varios campos a la vez. Aquí es donde las condiciones de carrera se vuelven realmente peligrosas.

Recuerda la estructura `cbuf` del capítulo 10:

```c
struct cbuf {
        char    *cb_data;
        size_t   cb_size;
        size_t   cb_head;
        size_t   cb_used;
};
```

Los invariantes de los que dependemos son:

- `cb_head < cb_size` (el índice de cabeza es siempre válido).
- `cb_used <= cb_size` (el recuento de bytes activos nunca supera la capacidad).
- Los bytes en las posiciones `[cb_head, cb_head + cb_used) mod cb_size` son los bytes activos.

Estos invariantes relacionan múltiples campos. Una llamada a `cbuf_write`, por ejemplo, actualiza `cb_used`. Una llamada a `cbuf_read` actualiza tanto `cb_head` como `cb_used`. Si un lector y un escritor se ejecutan de forma concurrente sin coordinación, cada una de esas actualizaciones puede observarse en un estado a medio terminar.

Considera lo que ocurre si `myfirst_read` y `myfirst_write` se ejecutan de forma concurrente en dos CPUs, sin mutex:

1. El thread lector inicia `cbuf_read` sobre un buffer con `cb_head = 1000, cb_used = 2000`. Calcula `first = MIN(n, cb_size - cb_head)` e inicia el primer `memcpy` desde `cb_data + 1000` hacia su buffer temporal de pila.
2. Mientras tanto, el thread escritor llama a `cbuf_write`, que actualiza `cb_used`. El escritor lee `cb_used = 2000`, calcula la cola como `(1000 + 2000) % cb_size = 3000`, realiza su `memcpy` en el buffer y escribe `cb_used = 2100`.
3. El lector termina su primer `memcpy`, pudiendo ver bytes que el escritor acaba de escribir (si el `memcpy` del escritor se superpuso a la posición del lector).
4. A continuación, el lector actualiza `cb_head = (1000 + n) % cb_size` y decrementa `cb_used`.

Varias cosas pueden ir mal aquí:

- **`cb_used` partido**: en una máquina de 32 bits, `cb_used` es de tipo de 32 bits, por lo que su lectura y escritura son atómicas. En amd64, `size_t` es de 64 bits, y los accesos alineados de 64 bits también son atómicos, así que una lectura partida es improbable. Pero si alguna vez usáramos una estructura más grande que el tamaño de palabra, veríamos bytes del valor antiguo mezclados con bytes del nuevo.
- **Violación de invariante observada**: el decremento de `cb_used` por parte del lector y el incremento por parte del escritor ocurren en momentos distintos. Entre ambos, `cb_used` puede reflejar transitoriamente solo una de las dos operaciones. Si un tercer lector (por ejemplo, un manejador de `sysctl`) observa `cb_used` en mitad de esa intercalación, el valor observado no es ni el "antes" ni el "después" de ninguna de las dos operaciones individuales.
- **`memcpy` concurrente en regiones solapadas**: si el `memcpy` del lector y el del escritor tocan el mismo byte, el valor final de ese byte es el que gane la carrera por la memoria. El lector puede ver que la mitad de sus datos previstos ha sido sobrescrita con datos nuevos del escritor. El escritor puede sobrescribir bytes que el lector ya ha copiado en su buffer temporal, lo que significa que el lector devuelve bytes que ya no existen en el buffer.

Cada uno de ellos es un error real. Cada uno es difícil de observar, porque la ventana temporal para la corrupción es minúscula. Cada uno es letal, porque una sola instancia puede corromper los datos de los que depende un programa de usuario.

### Un driver deliberadamente inseguro

La teoría se vuelve visceral cuando ejecutas el código. Vamos a construir una versión de `myfirst` que tenga el mutex eliminado de los manejadores de I/O. La cargarás, ejecutarás una prueba concurrente contra ella y verás cómo el driver produce resultados sin sentido.

**Advertencia:** carga este driver solo en una máquina de laboratorio sin estado importante. Puede corromper su propio estado a voluntad y, en algunos escenarios, dejar la máquina bloqueada. Prepárate para reiniciar si la prueba sale mal. El objetivo es ver el error, no forzarlo más allá.

Crea un nuevo directorio junto a tu código fuente del capítulo 10, etapa 4:

```sh
$ cd examples/part-03/ch11-concurrency
$ mkdir -p stage1-race-demo
$ cd stage1-race-demo
```

Copia `cbuf.c`, `cbuf.h` y `Makefile` desde el directorio del capítulo 10, etapa 4. Después copia `myfirst.c` y edítalo para eliminar el mutex de `myfirst_read` y `myfirst_write`. La forma más sencilla de hacerlo, para que puedas ver exactamente qué ha cambiado, es usar `sed` con un centinela:

```sh
$ sed \
    -e 's/mtx_lock(&sc->mtx);/\/\* RACE: mtx_lock removed *\//g' \
    -e 's/mtx_unlock(&sc->mtx);/\/\* RACE: mtx_unlock removed *\//g' \
    ../../part-02/ch10-handling-io-efficiently/stage4-poll-refactor/myfirst.c \
    > myfirst.c
```

Lo que hace es convertir cada `mtx_lock` y `mtx_unlock` del código fuente en un comentario. Deja el resto del código sin cambios, incluidos `mtx_sleep` y `mtx_assert`. Se trata de una eliminación quirúrgica de los puntos de adquisición del lock; el resto del driver sigue creyendo que el lock está presente.

Antes de construir, abre `myfirst.c` y busca las llamadas a `mtx_assert(&sc->mtx, MA_OWNED)` dentro de `myfirst_buf_read`, `myfirst_buf_write` y los helpers de espera. Estas van a disparar un pánico de `KASSERT` en un kernel con `INVARIANTS`, porque los helpers comprueban que el mutex está tomado y ahora nunca lo está. Esto es una característica, no un error: si por accidente ejecutas este driver en un kernel de depuración, la aserción detectará el problema antes de que se produzca ningún daño real. Para la demostración explícita de la condición de carrera, queremos ver la corrupción en sí, así que comenta las líneas de `mtx_assert` o deshabilita `INVARIANTS` en el kernel de prueba:

```sh
$ sed -i '' -e 's|mtx_assert(&sc->mtx, MA_OWNED);|/* RACE: mtx_assert removed */|g' myfirst.c
```

El `-i ''` es el modo in-place de `sed` en FreeBSD. En Linux, sería `sed -i` sin la cadena vacía entrecomillada.

Elimina también manualmente las llamadas a `mtx_init`, `mtx_destroy` y `mtx_lock`/`mtx_unlock` de `myfirst_attach` y `myfirst_detach`; los comandos `sed` anteriores no capturan los caminos de attach y detach porque esos tienen llamadas a `mtx_lock` que queremos conservar lógicamente (aunque no hagan nada). Un enfoque más sencillo y limpio, si lo prefieres, es introducir una macro al principio de `myfirst.c`:

```c
/* DANGEROUS: deliberately unsafe driver for Chapter 11 Section 2 demo. */
#define RACE_DEMO

#ifdef RACE_DEMO
#define MYFIRST_LOCK(sc)        do { (void)(sc); } while (0)
#define MYFIRST_UNLOCK(sc)      do { (void)(sc); } while (0)
#define MYFIRST_ASSERT(sc)      do { (void)(sc); } while (0)
#else
#define MYFIRST_LOCK(sc)        mtx_lock(&(sc)->mtx)
#define MYFIRST_UNLOCK(sc)      mtx_unlock(&(sc)->mtx)
#define MYFIRST_ASSERT(sc)      mtx_assert(&(sc)->mtx, MA_OWNED)
#endif
```

A continuación, reemplaza globalmente `mtx_lock(&sc->mtx)` por `MYFIRST_LOCK(sc)` y de forma similar para el resto. Con `RACE_DEMO` definida, las macros no hacen nada; sin ella, son las llamadas originales. Este es el enfoque que usa el código fuente de acompañamiento. Te permite activar y desactivar la condición de carrera en tiempo de compilación.

Compila el módulo:

```sh
$ make
$ kldstat | grep myfirst && kldunload myfirst
$ kldload ./myfirst.ko
$ dmesg | tail -5
```

La línea de attach debería aparecer como de costumbre. Nada parece erróneo todavía.

### Ejecutando la condición de carrera

Con el driver inseguro cargado, ejecuta la prueba de productor/consumidor del capítulo 10:

```sh
$ cd ../../part-02/ch10-handling-io-efficiently/userland
$ ./producer_consumer
```

Recuerda que esta prueba produce un patrón fijo en el driver, lo lee de vuelta en otro proceso y compara las sumas de comprobación. Con el driver seguro, las sumas de comprobación coinciden y la prueba informa de cero discrepancias. Ejecútala contra el driver inseguro y el resultado dependerá del momento concreto de ejecución:

- **Resultado probable**: las dos sumas de comprobación difieren y el lector informa de cierto número de discrepancias. El número exacto varía entre ejecuciones.
- **Resultado posible**: la prueba se cuelga indefinidamente, porque el estado del buffer se volvió inconsistente y el lector o el escritor quedaron atrapados esperando una condición que nunca será cierta.
- **Resultado infrecuente**: la prueba pasa por casualidad. Si esto ocurre, ejecútala de nuevo; una sola ejecución de una prueba de concurrencia no te dice casi nada.
- **Resultado desagradable**: el kernel entra en pánico porque se dispararon invariantes dentro de `cbuf_read` o `cbuf_write` (por ejemplo, un `cb_used` negativo que debía ser sin signo, o un `cb_head` fuera de límites). Si esto ocurre, anota el backtrace, reinicia y continúa. Esto es lo que viniste a ver.

En la mayoría de las ejecuciones verás una salida parecida a:

```text
writer: 1048576 bytes, checksum 0x8bb7e44c
reader: 1043968 bytes, checksum 0x3f5a9b21, mismatches 2741
exit: reader=2 writer=0
```

El lector obtuvo menos bytes de los que produjo el escritor. Su suma de comprobación es diferente. Un número determinado de posiciones de byte contenían el valor incorrecto.

Esto no es una prueba inestable. Es un driver funcionando según lo previsto, dado que se ha eliminado el locking. El kernel planifica múltiples threads en múltiples CPUs. Esos threads leen y escriben la misma memoria sin coordinación. El resultado es el estado que produce cualquier intercalación hardware concreta.

Ejecuta la prueba varias veces. Los números variarán. El carácter de la corrupción, sin embargo, no lo hará: el escritor afirmará haber producido cierto número de bytes, y el lector verá un conjunto diferente. Así es como se ve la concurrencia sin sincronización. No es misteriosa ni sutil; es la consecuencia exacta de las definiciones de la parte anterior de esta sección.

### Observando el daño con los logs

La prueba de productor/consumidor informa del daño en conjunto. Para una observación más granular, instrumenta el driver con `device_printf` (protegido por el sysctl de depuración) dentro de `cbuf_write` y `cbuf_read`. Añade algo parecido a:

```c
device_printf(sc->dev,
    "cbuf_write: tail=%zu first=%zu second=%zu cb_used=%zu\n",
    tail, first, second, cb->cb_used);
```

al final de `cbuf_write`, tras la comprobación de `myfirst_debug` que configuraste en el capítulo 10, etapa 2. Lo mismo para `cbuf_read`.

Vuelve a ejecutar el driver inseguro con `sysctl dev.myfirst.debug=1` y observa `dmesg -t` en otro terminal:

```sh
$ sysctl dev.myfirst.debug=1
$ ./producer_consumer
$ dmesg -t | tail -40
```

Verás líneas parecidas a:

```text
myfirst0: cbuf_write: tail=3840 first=256 second=0 cb_used=2048
myfirst0: cbuf_read: head=0 first=256 second=0 cb_used=1792
myfirst0: cbuf_write: tail=1536 first=256 second=0 cb_used=2304
myfirst0: cbuf_read: head=2048 first=256 second=0 cb_used=-536  <-- inconsistent
```

`cb_used` es un `size_t`, que es sin signo, así que "negativo" significa aquí que el campo dio la vuelta: un número muy grande que se formateó con `%zu` y que sigue pareciendo enorme, pero que no debería serlo. Ese wraparound es uno de los modos de fallo que analizamos. Un decremento concurrente de `cb_used` leyó el valor que existía antes de que el incremento de otro thread terminara, restó su propia cantidad y almacenó un resultado sin ningún sentido.

Ver un registro lleno de entradas de ese tipo deja claro lo que las definiciones no consiguieron transmitir: los bugs de concurrencia no son casos límite infrecuentes. Son el caso habitual cuando múltiples threads acceden a los mismos campos sin coordinación alguna. El driver no falla de vez en cuando. Falla constantemente. Solo que a veces, por pura casualidad, produce igualmente una salida que parece plausible.

### Limpieza

Descarga el driver no seguro antes de hacer cualquier otra cosa:

```sh
$ kldunload myfirst
```

Si `kldunload` se queda colgado (porque algún proceso de prueba todavía tiene abierto un descriptor), localiza y mata ese proceso. Si el kernel está en un estado corrupto tras un fallo similar a un pánico, reinicia. A partir de aquí, trabaja únicamente con el driver seguro a menos que estés repitiendo la demostración deliberadamente para comparar.

No dejes el driver no seguro cargado. No hagas commit de la build con `RACE_DEMO` activado y te olvides de ella. El árbol de código de acompañamiento mantiene la demo de la condición de carrera en un directorio hermano precisamente para que no pueda confundirse con el código de producción.

### La lección

Del ejercicio se extraen tres observaciones.

La primera es que el mutex del driver del Capítulo 10 no es una cuestión de estilo. Elimínalo y el driver falla, de inmediato, de una forma que puedes demostrar en cualquier máquina FreeBSD. El mutex es estructural. Protege exactamente la propiedad (la consistencia interna del cbuf) de la que depende la corrección del resto del driver.

La segunda es que las pruebas no pueden detectar esta clase de error de forma fiable. Si no supieras que hay que ejecutar una prueba de estrés con múltiples procesos, si solo probaras con un único `cat` y un único `echo`, el driver parecería estar bien. El error de concurrencia estaría ahí, invisible, esperando al primer usuario cuya carga de trabajo implique dos llamantes concurrentes. Revisar el código con la concurrencia en mente no es un sustituto de las pruebas; es lo que te impide publicar código cuyos tests pasan por casualidad.

La tercera es que la solución no es un parche local. No basta con añadir operaciones atómicas en uno o dos campos y dar la tarea por terminada. El cbuf tiene invariantes compuestos que abarcan múltiples campos; protegerlo requiere una región de código en la que ningún otro thread pueda estar ejecutándose al mismo tiempo. Ese es el trabajo de un mutex, y la Sección 5 es donde analizamos los mutexes en profundidad. Antes de llegar allí, sin embargo, conviene auditar el driver existente (Sección 3) y comprender la herramienta más limitada que ofrecen las operaciones atómicas (Sección 4).

### Una segunda condición de carrera: attach frente a I/O

No todas las condiciones de carrera están en el camino de datos. Veamos una condición de carrera que corregimos en el Capítulo 10 sin nombrarla: la carrera entre `myfirst_attach` y el primer usuario que llama a `open(2)`.

Cuando `myfirst_attach` se ejecuta, hace varias cosas en secuencia:

1. Configura `sc->dev` y `sc->unit`.
2. Inicializa el mutex.
3. Establece `sc->is_attached = 1`.
4. Inicializa el cbuf.
5. Crea el cdev mediante `make_dev_s`.
6. Crea el alias del cdev.
7. Registra los sysctls.
8. Retorna.

El cdev no es visible para el espacio de usuario hasta el paso 5. Un usuario que llame a `open("/dev/myfirst")` antes de que el paso 5 se complete recibirá `ENOENT`, porque el nodo de dispositivo aún no existe. Después del paso 5, el cdev está registrado en devfs y `open` puede tener éxito.

Si el paso 5 ocurriera antes que el paso 3, un usuario podría abrir el dispositivo y llamar a `read` antes de que se hubiera establecido `is_attached`. La comprobación `if (!sc->is_attached) return (ENXIO)` del manejador de lectura fallaría en la llamada, devolviendo ENXIO aunque el attach estuviera en curso. Eso no es catastrófico, pero sí es confuso y evitable.

La solución es el orden que usamos: `is_attached = 1` ocurre antes que `make_dev_s`. En el momento en que cualquier manejador pueda ejecutarse, `is_attached` ya estará establecido.

El matiz sutil es que este orden *solo es correcto* porque el attach, que es de un único thread, no puede ser interrumpido entre las dos escrituras. Si el compilador o el hardware pudieran reordenar las escrituras (lo que no ocurre para almacenamientos de enteros simples en amd64 sin barreras explícitas, pero sí podría ocurrir en algunas arquitecturas de orden débil), necesitaríamos un `atomic_store_rel_int` para la escritura de `is_attached`. El libro se centra en amd64, así que el almacenamiento simple es suficiente.

Esta es una disciplina generalmente útil. Cada vez que escribas código de attach, enumera las precondiciones observables de cada paso siguiente y verifica que están establecidas antes de que el paso sea accesible desde el espacio de usuario. El orden importa, y casi siempre es un error crear el cdev antes de que el softc esté completamente listo.

### Una tercera condición de carrera: sysctl frente a I/O

Otra condición de carrera que el diseño del Capítulo 10 gestiona: el manejador `sysctl` que lee `cb_used` o `cb_free` debe devolver un valor coherente consigo mismo, no un compuesto fragmentado.

Los campos `cb_head` y `cb_used` son ambos de tipo `size_t`. Cualquiera de ellos, leído por separado, proporciona un valor de una sola palabra que es atómico en amd64. Pero `cb_used` y `cb_head` juntos forman un invariante (los bytes activos están en `[cb_head, cb_head + cb_used) mod cb_size`). Un sysctl que lea ambos sin el mutex podría observarlos en momentos inconsistentes.

Para `cb_used` solo, la condición de carrera es tolerable: el contador puede estar ligeramente desactualizado, pero el valor devuelto es al menos *algún* valor que en algún momento fue verdadero. Para `cb_head`, ocurre lo mismo. Para cualquier cosa que combine los dos (como «¿cuántos bytes faltan para que la cola alcance el límite de capacidad?»), la condición de carrera produce números sin sentido.

Protegemos las lecturas del sysctl con el mutex. La sección crítica es mínima (una sola carga), así que la contención es mínima; el beneficio en cuanto a corrección es que el valor devuelto tiene garantía de ser coherente consigo mismo.

La regla que esto ilustra es: **si tu observación combina múltiples campos que juntos expresan un invariante, protege la observación con el mismo primitivo que protege el invariante**.

### La taxonomía de condiciones de carrera, revisada

Con la condición de carrera del cbuf (texto principal de la Sección 2), la del attach y la del sysctl, tenemos ahora tres ejemplos concretos que clasificar bajo los cuatro modos de fallo de la Sección 1.

- **Corrupción de datos del cbuf**: principalmente estado compuesto inconsistente (el invariante `cb_used`/`cb_head`), junto con algunas actualizaciones perdidas en `cb_used` en sí.
- **Condición de carrera en attach**: una forma de estado compuesto inconsistente, donde el invariante «¿está el dispositivo listo?» es transitoriamente falso desde la perspectiva del usuario.
- **Observación fragmentada en sysctl**: una forma de estado compuesto inconsistente, donde el cálculo del lector utiliza campos de diferentes momentos en el tiempo.

Los tres están protegidos por el mismo mecanismo: serializar el acceso al estado compuesto con un único mutex. Esta unidad no es una coincidencia; es la razón por la que un único mutex suele ser la respuesta correcta para un driver pequeño.

### Cerrando la Sección 2

Una condición de carrera es algo muy concreto: acceso solapado a estado compartido, al menos una escritura, y corrección que depende del entrelazado. Los invariantes compuestos del cbuf lo convierten en una fuente rica de condiciones de carrera cuando se elimina el mutex. Un driver deliberadamente no seguro revela la corrupción en cuestión de segundos al ejecutar una prueba concurrente.

Ya has visto el problema en tu propia terminal, en tu propio driver. Has visto tres condiciones de carrera distintas, todas en lugares donde hay estado compuesto, todas resueltas por el mismo mecanismo. Todo lo que sigue es la construcción de las herramientas que hacen desaparecer el problema, capa a capa.



## Sección 3: Análisis del código no seguro en tu driver

La mayoría de los desarrolladores de drivers, la mayor parte del tiempo, no crean condiciones de carrera deliberadamente. Las crean al no darse cuenta de que un fragmento de estado está compartido entre flujos de ejecución. La defensa frente a esto es un hábito, no una herramienta ingeniosa: cada vez que tocas estado que *podría* ser accedido desde fuera del camino de código actual, te detienes y preguntas si realmente es seguro tocarlo sin sincronización. La mayor parte del tiempo la respuesta es «sí, ya está protegido por X», y continúas. En ocasiones la respuesta es «en realidad, no está protegido, y necesito pensarlo». Ese segundo caso es el que esta sección te enseña a reconocer.

El driver de la Etapa 4 del Capítulo 10 es razonablemente cuidadoso. Usa un mutex, documenta lo que protege ese mutex y sigue convenciones consistentes. Pero incluso un driver razonablemente cuidadoso se beneficia de una auditoría explícita. El objetivo de esta sección es recorrer el driver y clasificar cada fragmento de estado compartido: quién lo lee, quién lo escribe, qué lo protege y bajo qué condiciones. El resultado es un pequeño documento que acompaña al código, hace que la narrativa de concurrencia sea auditable, y se convierte en un punto de control que puedes usar cuando cambios futuros corran el riesgo de romper el modelo.

### Qué significa «compartido»

Una variable es **compartida** si más de un flujo de ejecución puede acceder a ella durante la misma ventana de tiempo. «Misma ventana de tiempo» no requiere simultaneidad literal; requiere que los dos accesos pudieran *potencialmente* solaparse.

Esa definición es más amplia de lo que los principiantes suelen esperar. Tres ejemplos la aclaran.

**Una variable local dentro de una función no es compartida.** Cada invocación de la función obtiene su propio marco de pila, y la variable vive en ese marco. Otro thread puede entrar en la misma función, pero obtendrá su propia pila y su propia copia de la variable. No hay memoria compartida.

**Una variable static dentro de una función es compartida.** A pesar de que la palabra clave `static` hace que parezca local, el almacenamiento tiene alcance de archivo: todas las invocaciones de la función ven el mismo byte de memoria. Dos invocaciones concurrentes de la función comparten ese byte.

**Un campo en una estructura asignada dinámicamente es compartido si y solo si más de un flujo de ejecución tiene un puntero a la misma estructura.** El `struct myfirst_softc` de un dispositivo concreto es una de esas estructuras. Toda llamada al driver que pase por `dev->si_drv1` observa el mismo softc. Dos llamadas concurrentes ven el mismo softc.

Una variante más sutil: un campo en una estructura por descriptor (nuestro `struct myfirst_fh`) es compartido solo si más de un flujo tiene acceso al mismo descriptor de archivo. Dos procesos con descriptores distintos no comparten el `struct myfirst_fh` del otro. Dos threads del mismo proceso que comparten un descriptor sí lo hacen. La mayoría de los desarrolladores de drivers tratan el estado por descriptor como «casi» no compartido y usan protección más ligera que para el estado a nivel de dispositivo, pero la palabra «casi» hace un trabajo pesado; volveremos a este caso en la Sección 5.

### El estado compartido de myfirst

Abre el código fuente de la Etapa 4. Vamos a anotar, campo por campo, el contenido de `struct myfirst_softc`. Puedes hacerlo en el propio código fuente (como un bloque de comentarios cerca de la declaración de la estructura) o en un documento de diseño separado. Recomiendo hacer ambas cosas: un resumen breve en el código fuente y una versión detallada en un archivo que puedas ir ampliando.

Aquí está la estructura, tal como está en la Etapa 4 del Capítulo 10:

```c
struct myfirst_softc {
        device_t                dev;

        int                     unit;

        struct mtx              mtx;

        uint64_t                attach_ticks;
        uint64_t                open_count;
        uint64_t                bytes_read;
        uint64_t                bytes_written;

        int                     active_fhs;
        int                     is_attached;

        struct cbuf             cb;

        struct selinfo          rsel;
        struct selinfo          wsel;

        struct cdev            *cdev;
        struct cdev            *cdev_alias;

        struct sysctl_ctx_list  sysctl_ctx;
        struct sysctl_oid      *sysctl_tree;
};
```

Para cada campo nos hacemos cinco preguntas:

1. **¿Quién lo lee?** ¿Qué funciones, y desde qué flujos de ejecución?
2. **¿Quién lo escribe?** La misma pregunta, pero para las escrituras.
3. **¿Qué protege los accesos de las condiciones de carrera?**
4. **¿En qué invariantes participa?**
5. **¿Es suficiente la protección actual?**

Ir campo por campo es tedioso. Y ese es precisamente el objetivo. Un driver cuya narrativa de concurrencia está documentada campo por campo es un driver que casi con toda seguridad funciona. Un driver cuya narrativa de concurrencia se deja implícita es un driver cuyo autor esperaba que todo saliera bien.

**`sc->dev`**: el handle `device_t` de Newbus.
- Lo lee: cada manejador (indirectamente, a través de `sc->dev` en `device_printf` y similares).
- Lo escribe: attach (una vez, durante la carga del módulo).
- Protección: la escritura ocurre antes de que ningún manejador pueda ejecutarse (el cdev no se registra hasta después de establecer `sc->dev`). Una vez que attach termina, el campo es efectivamente inmutable. No se necesita lock.
- Invariantes: `sc->dev != NULL` una vez que attach ha devuelto cero.
- ¿Es suficiente? Sí.

**`sc->unit`**: una copia de conveniencia de `device_get_unit(dev)`.
- El mismo análisis que `sc->dev`. Inmutable después de attach. No se necesita lock.

**`sc->mtx`**: el sleep mutex.
- Lo «acceden» todos los manejadores en las llamadas a `mtx_lock`/`mtx_unlock`/`mtx_sleep`.
- Lo inicializa `mtx_init` en attach, y lo destruye `mtx_destroy` en detach.
- Protección: el mutex se protege a sí mismo; la inicialización y la destrucción están ordenadas respecto a la creación y destrucción del cdev de forma que ningún manejador pueda ver jamás un mutex a medio inicializar.
- Invariantes: el mutex está inicializado después de que `mtx_init` retorne y antes de que se llame a `mtx_destroy`. Los manejadores solo se ejecutan durante esa ventana.
- ¿Es suficiente? Sí.

**`sc->attach_ticks`**: el valor de `ticks` en el momento del attach, con fines informativos.
- Lo lee: el handler de sysctl para `attach_ticks`.
- Lo escribe: attach (una sola vez).
- Protección: la escritura ocurre antes de que ningún handler de sysctl pueda ser invocado. A partir de entonces, es inmutable. No se necesita ningún lock.
- Invariantes: ninguna más allá de "se establece en attach".
- ¿Es suficiente? Sí.

**`sc->open_count`**: contador acumulado de aperturas.
- Lo lee: el handler de sysctl para `open_count`.
- Lo escribe: `myfirst_open`.
- Protección: la escritura se realiza bajo `sc->mtx` en `myfirst_open`. La lectura, actualmente, es una carga sin protección desde el handler `SYSCTL_ADD_U64`.
- Invariantes: monótonamente creciente.
- ¿Es suficiente? En amd64, una carga de 64 bits alineada es atómica, por lo que una lectura rasgada no es posible. El sysctl puede observar un valor antiguo (una carga que ocurrió justo antes de un incremento concurrente), pero no uno corrupto. Esto es aceptable para un contador informativo. En plataformas de 32 bits, la misma carga no sería atómica y una lectura rasgada sería posible. Para los propósitos de este libro (máquina de laboratorio amd64), la protección actual es suficiente. Lo indicaremos en la documentación.

**`sc->bytes_read`, `sc->bytes_written`**: contadores acumulados de bytes.
- El mismo análisis que `open_count`. Se escriben bajo el mutex, se leen sin protección desde sysctl, lo cual es aceptable en amd64 pero no en plataformas de 32 bits. Son candidatos a migrar a contadores por CPU de `counter(9)`, lo que eliminaría la preocupación por lecturas rasgadas y también escalaría mejor bajo contención. La sección 4 realiza esta migración.

**`sc->active_fhs`**: recuento actual de descriptores abiertos.
- Los leen: el handler de sysctl, `myfirst_detach`.
- Los escriben: `myfirst_open`, `myfirst_fh_dtor`.
- Protección: todas las lecturas y escrituras ocurren bajo `sc->mtx`.
- Invariantes: `active_fhs >= 0`. `active_fhs == 0` cuando se invoca detach y este tiene éxito.
- ¿Es suficiente? Sí.

**`sc->is_attached`**: flag que indica si el driver está en estado attached.
- Lo leen: todos los handlers al entrar (para fallar de inmediato con `ENXIO` si el dispositivo ha sido desmontado).
- Lo escriben: attach (lo pone a 1), detach (lo pone a 0, bajo el mutex).
- Protección: la escritura en detach está bajo el mutex. Las lecturas en los handlers al entrar *no* están bajo el mutex (son cargas simples sin protección).
- Invariantes: `is_attached == 1` mientras el dispositivo está operativo.
- ¿Es suficiente? Hay una cuestión sutil aquí. Un handler que lee `is_attached == 1` sin el mutex podría a continuación proceder a adquirir el mutex, y para cuando lo haya obtenido, detach puede haber ejecutado y puesto el flag a 0. Nuestros handlers tratan este caso volviendo a comprobar `is_attached` después de cada sleep (el patrón `if (!sc->is_attached) return (ENXIO);` tras `mtx_sleep`). La comprobación en la entrada es una optimización que evita tomar el mutex en el caso común; la corrección del comportamiento no depende de ella. Lo documentaremos explícitamente.

**`sc->cb`**: el buffer circular (todos sus campos).
- Lo leen: `myfirst_read`, `myfirst_write`, `myfirst_poll`, los handlers de sysctl para `cb_used` y `cb_free`.
- Lo escriben: `myfirst_read`, `myfirst_write`, attach (a través de `cbuf_init`), detach (a través de `cbuf_destroy`).
- Protección: todos los accesos están bajo `sc->mtx`, incluidos los handlers de sysctl (que toman el mutex alrededor de una breve lectura). `cbuf_init` se ejecuta en attach antes de que el cdev esté registrado, por lo que es efectivamente monohilo. `cbuf_destroy` se ejecuta en detach tras confirmar que todos los descriptores están cerrados, por lo que también es efectivamente monohilo.
- Invariantes: `cb_used <= cb_size`, `cb_head < cb_size`, los bytes en `[cb_head, cb_head + cb_used) mod cb_size` están activos.
- ¿Es suficiente? Sí. Es el campo en el que el trabajo del Capítulo 10 invirtió más tiempo para hacerlo correctamente.

**`sc->rsel`, `sc->wsel`**: estructuras `selinfo` para la integración con `poll(2)`.
- Los leen/escriben: `selrecord(9)`, `selwakeup(9)`, `seldrain(9)`. Estas funciones gestionan su propio locking internamente; la responsabilidad del invocante es simplemente llamarlas en los momentos adecuados.
- Protección: interna a la maquinaria de `selinfo`. El invocante mantiene `sc->mtx` alrededor de `selrecord` (porque se llama dentro de `myfirst_poll`, que toma el mutex) y suelta `sc->mtx` alrededor de `selwakeup` (porque puede bloquearse).
- Invariantes: el `selinfo` se inicializa una vez (se inicializa a cero con la asignación del softc) y se drena en detach.
- ¿Es suficiente? Sí.

**`sc->cdev`, `sc->cdev_alias`**: punteros al cdev y a su alias.
- Los leen: nada crítico; se almacenan para su posterior destrucción.
- Los escriben: attach (los establece), detach (los destruye y limpia).
- Protección: las escrituras ocurren en puntos conocidos del ciclo de vida, antes o después de que los handlers puedan ejecutarse.
- Invariantes: `sc->cdev != NULL` durante el período en que el dispositivo está attached.
- ¿Es suficiente? Sí.

**`sc->sysctl_ctx`, `sc->sysctl_tree`**: el contexto de sysctl y el nodo raíz.
- Gestionados por el framework de sysctl. El framework gestiona su propio locking.
- Protección: interna al framework.
- ¿Es suficiente? Sí.

### El estado por descriptor

Ahora el estado por descriptor, en `struct myfirst_fh`:

```c
struct myfirst_fh {
        struct myfirst_softc   *sc;
        uint64_t                reads;
        uint64_t                writes;
};
```

**`fh->sc`**: puntero de retorno al softc.
- Lo leen: los handlers que lo recuperan mediante `devfs_get_cdevpriv`.
- Lo escribe: `myfirst_open`, una sola vez, antes de que el fh se entregue a devfs.
- Protección: la escritura queda secuenciada antes de que ningún handler pueda ver el fh.
- Invariantes: inmutable tras `myfirst_open`.
- ¿Es suficiente? Sí.

**`fh->reads`, `fh->writes`**: contadores de bytes por descriptor.
- Los leen: el destructor `myfirst_fh_dtor` (para un mensaje de log final) y, potencialmente, futuros handlers de sysctl por descriptor (no expuestos actualmente).
- Los escriben: `myfirst_read`, `myfirst_write`.
- Protección: las escrituras ocurren bajo `sc->mtx` (dentro de los handlers de lectura/escritura). Dos threads que mantengan el mismo descriptor abierto podrían, en principio, escribir de forma concurrente; ambos tomarían `sc->mtx`, por lo que los accesos se serializan.

Este es el caso sutil mencionado antes. Dos threads del mismo proceso pueden compartir un descriptor de archivo. Ambos pueden llamar a `read(2)` sobre él. Ambos entrarán en `myfirst_read`, ambos recuperarán el mismo `fh` mediante `devfs_get_cdevpriv` y ambos querrán actualizar `fh->reads`. Como ambos mantienen `sc->mtx` durante la actualización, las operaciones se serializan y no se produce ninguna condición de carrera. Si en algún momento quisiéramos mantener algo menos que el `sc->mtx` completo (por ejemplo, si usáramos una actualización de contador sin lock), el caso por descriptor nos obligaría a ser más cuidadosos.

### Clasificación de las secciones críticas

Con los campos inventariados, podemos identificar ahora cada **sección crítica** del código. Una sección crítica es una región contigua de código que accede a estado compartido y debe ejecutarse sin interferencias de flujos concurrentes.

Recorre `myfirst.c` y localiza cada región delimitada por `mtx_lock(&sc->mtx)` y `mtx_unlock(&sc->mtx)`. Cada una de esas regiones es, por construcción, una sección crítica. Después busca regiones que deberían ser secciones críticas pero no lo son. En el driver de la etapa 4 no debería haber ninguna, pero la auditoría sigue siendo valiosa para confirmar esa ausencia.

Las secciones críticas de la etapa 4 son:

1. **`myfirst_open`**: actualiza `open_count` y `active_fhs` bajo el mutex.
2. **`myfirst_fh_dtor`**: decrementa `active_fhs` bajo el mutex.
3. **`myfirst_read`**: varias secciones críticas, cada una delimitando un acceso al cbuf, separadas por las llamadas a `uiomove` fuera del lock. El helper de espera se ejecuta dentro de la sección crítica; los contadores de bytes se actualizan dentro; `wakeup` y `selwakeup` se llaman con el mutex suelto.
4. **`myfirst_write`**: el espejo de `myfirst_read`.
5. **`myfirst_poll`**: una sección crítica que comprueba el estado del cbuf y o bien establece `revents` o bien llama a `selrecord`.
6. **Los dos handlers de sysctl del cbuf**: cada uno toma el mutex brevemente para leer `cbuf_used` o `cbuf_free`.
7. **`myfirst_detach`**: adquiere el mutex para comprobar `active_fhs` y para establecer `is_attached = 0` antes de despertar a los durmientes.

Cada una de estas secciones debería hacer el menor trabajo posible mientras mantiene el mutex. El trabajo que no necesite el mutex (el `uiomove`, por ejemplo, o cualquier cómputo sobre valores locales a la pila) debe ocurrir fuera. El driver de la etapa 4 ya es cuidadoso al respecto; la auditoría lo confirma.

### Anotación del código fuente

Un resultado útil de la auditoría es volver al código fuente y añadir comentarios de una línea encima de cada sección crítica que nombren el estado compartido que se está protegiendo. Esto no es decorativo; es documentación para la próxima persona que lea el código.

Por ejemplo, el handler `myfirst_read` queda así:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* ... entry logic ... */
        while (uio->uio_resid > 0) {
                /* critical section: cbuf state + bytes_read + fh->reads */
                mtx_lock(&sc->mtx);
                error = myfirst_wait_data(sc, ioflag, nbefore, uio);
                if (error != 0) {
                        mtx_unlock(&sc->mtx);
                        return (error == -1 ? 0 : error);
                }
                take = MIN((size_t)uio->uio_resid, sizeof(bounce));
                got = myfirst_buf_read(sc, bounce, take);
                fh->reads += got;
                mtx_unlock(&sc->mtx);

                /* not critical: wake does its own locking */
                wakeup(&sc->cb);
                selwakeup(&sc->wsel);

                /* not critical: uiomove may sleep; mutex must be dropped */
                error = uiomove(bounce, got, uio);
                if (error != 0)
                        return (error);
        }
        return (0);
}
```

Tres comentarios. Treinta segundos de trabajo. Transforman el handler de un trozo de código que el lector tiene que analizar en un trozo de código cuya historia de concurrencia se explica por sí sola.

### Un documento de estrategia de locking

Junto a las anotaciones del código fuente, mantén un documento breve que describa la estrategia general. El archivo `LOCKING.md` en el árbol de código fuente de tu driver es un buen lugar. Una versión mínima tiene este aspecto:

```markdown
# myfirst Locking Strategy

## Overview
The driver uses a single sleep mutex (sc->mtx) to serialize all
accesses to the circular buffer, the sleep channel, the selinfo
structures, and the per-descriptor byte counters.

## What sc->mtx Protects
- sc->cb (the circular buffer's internal state)
- sc->bytes_read, sc->bytes_written
- sc->open_count, sc->active_fhs
- sc->is_attached (writes; reads may be unprotected as an optimization)
- sc->rsel, sc->wsel (through the selinfo API; the mutex is held
  around selrecord, dropped around selwakeup)

## Locking Discipline
- Acquire with mtx_lock, release with mtx_unlock.
- Wait with mtx_sleep(&sc->cb, &sc->mtx, PCATCH, wmesg, 0).
- Wake with wakeup(&sc->cb).
- NEVER hold sc->mtx across uiomove, copyin, copyout, selwakeup,
  or wakeup. Each of these may sleep or take other locks.
- All four myfirst_buf_* helpers assert mtx_assert(MA_OWNED).

## Known Non-Locked Accesses
- sc->is_attached at handler entry: unprotected read. Safe because
  a stale true is re-checked after every sleep; a stale false is
  harmless (the handler returns ENXIO, which is also what it would
  do with a fresh false).
- sc->open_count, sc->bytes_read, sc->bytes_written at sysctl
  read time: unprotected load. Safe on amd64 (aligned 64-bit loads
  are atomic). Would be unsafe on 32-bit platforms.

## Lock Order
sc->mtx is currently the only lock the driver holds. No ordering
concerns arise. Future versions that add additional locks must
specify their order here.
```

Ese documento es la respuesta definitiva a "¿es seguro este driver?". Es revisable. Puede cotejarse con el código. Es el resultado de la auditoría que acabamos de realizar.

### Un procedimiento de auditoría práctico

El procedimiento que acabamos de recorrer puede sistematizarse:

1. Lista cada campo de cada estructura compartida (softc, por descriptor, cualquier estado global del módulo).
2. Para cada campo, identifica todos los puntos de lectura y todos los puntos de escritura.
3. Para cada campo, identifica el mecanismo de protección (lock adquirido, garantía de ciclo de vida, interno al framework, tipo atómico, intencionalmente desprotegido).
4. Para cada campo, identifica los invariantes en los que participa y los demás campos que esos invariantes implican.
5. Para cada invariante, confirma que el mecanismo de protección cubre todos los accesos relevantes.
6. Para cada acceso deliberadamente desprotegido, documenta por qué es aceptable.
7. Anota el código fuente para nombrar cada sección crítica y cada acceso desprotegido deliberado.
8. Escribe el resumen en un archivo `LOCKING.md`.

Suena laborioso. Para un driver del tamaño de `myfirst`, lleva menos de una hora. Para un driver diez veces mayor, lleva un día. En cualquier caso, se amortiza la primera vez que detecta un bug que todavía no había sido escrito.

### Descubrimientos habituales en las auditorías

Tres patrones se repiten. Si tu auditoría encuentra alguno de ellos, trátalo como una señal de alarma.

El primero es el **sharing silencioso**: un campo que creías local pero que en realidad es accesible desde fuera del flujo de código actual. La causa habitual es un puntero que se filtra desde una estructura que creías privada. `devfs_set_cdevpriv` le da a un cdev un puntero a una estructura por descriptor; si esa estructura contiene un puntero de retorno, otro thread puede acceder a través de él. El estado por descriptor al que solo accede el thread actual es más seguro que el estado alcanzable desde el softc.

El segundo es la **protección irregular**: un campo que está protegido en la mayoría de los flujos de código pero no en uno. Con frecuencia, el flujo desprotegido es el añadido más recientemente. La auditoría lo detecta; las pruebas unitarias no.

El tercero es la **deriva de invariantes**: con el tiempo, un campo adquiere nuevas correlaciones con otros campos y la protección deja de cubrir el invariante compuesto. La auditoría lo revela al preguntar en qué invariantes participa cada campo.

Una auditoría disciplinada, repetida periódicamente, detecta los tres.

### Una segunda auditoría trabajada: un driver hipotético de múltiples colas

Para ejercitar el procedimiento de auditoría con algo ligeramente más interesante, imagina un driver que no es `myfirst`. Supón que estamos auditando un pseudodispositivo hipotético adyacente a la red, `netsim`, que simula una fuente de paquetes. El softc tiene más estado compartido:

```c
struct netsim_softc {
        device_t                dev;
        struct mtx              global_mtx;
        struct mtx              queue_mtx;

        struct netsim_queue     rx_queue;
        struct netsim_queue     tx_queue;

        struct callout          tick_callout;
        int                     tick_rate_hz;

        uint64_t                packets_rx;
        uint64_t                packets_tx;
        uint64_t                dropped;

        int                     is_up;
        int                     is_attached;
        int                     active_fhs;

        struct cdev            *cdev;
};
```

Esta es una estructura más rica. Apliquemos el procedimiento de auditoría.

**Dos locks.** `global_mtx` y `queue_mtx`. Necesitamos decidir qué protege cada uno y establecer un orden.

Una división razonable: `global_mtx` protege el estado de configuración y de ciclo de vida (`is_up`, `is_attached`, `tick_rate_hz`, `active_fhs`, `tick_callout`). `queue_mtx` protege las colas RX y TX, que forman el hot path de alta frecuencia.

**Orden de los locks.** Como los cambios de configuración son poco frecuentes y las operaciones sobre las colas son intensas, establecemos el orden: `global_mtx` antes que `queue_mtx`. Un thread que mantiene `global_mtx` puede adquirir `queue_mtx`. Un thread que mantiene `queue_mtx` no puede adquirir `global_mtx`.

Esta es una elección, no un mandato. Un driver diferente podría invertirlo. Lo que importa es la consistencia. `WITNESS` detectará cualquier violación.

**Campos de contadores.** `packets_rx`, `packets_tx`, `dropped` son contadores actualizados en el camino de datos. `counter(9)` es la herramienta adecuada.

**El callout.** `tick_callout` se dispara periódicamente para generar paquetes. Su handler se ejecuta en un thread del kernel, de forma concurrente con las operaciones de lectura/escritura iniciadas por el usuario. El handler del callout toma `queue_mtx` para encolar paquetes. Como el callout se inicializa antes de que se registre el cdev y se detiene en el detach, el callout y los handlers del usuario están correctamente acotados.

**Los invariantes.**

- `is_attached == 1` durante la ventana en la que el driver está conectado; el detach lo borra bajo `global_mtx`.
- Las estructuras de cola tienen sus propios invariantes internos (índices de cabeza y cola); `queue_mtx` los protege.
- `tick_rate_hz` se lee en el callout; se escribe en el attach y en los ioctls; las lecturas se realizan bajo `global_mtx`, por lo que la tasa no puede cambiar a mitad de un tick.

**Qué escribir en LOCKING.md**:

```markdown
## Locks

### global_mtx (MTX_DEF)
Protects: is_up, is_attached, active_fhs, tick_rate_hz, tick_callout.

### queue_mtx (MTX_DEF)
Protects: rx_queue, tx_queue internal state.

## Lock Order
global_mtx -> queue_mtx

A thread holding global_mtx may acquire queue_mtx.
A thread holding queue_mtx may NOT acquire global_mtx.

## Lock-Free Fields
packets_rx, packets_tx, dropped: counter_u64_t. No lock required.

## Callout
tick_callout fires every 1/tick_rate_hz seconds. It acquires
queue_mtx to enqueue packets. It does not acquire global_mtx.
Stopping the callout is done in detach, under global_mtx, with
callout_drain.
```

Observa que la auditoría produjo un documento muy similar al de `myfirst`, a pesar de que el driver es mayor. La estructura es la misma: locks, qué protegen, el orden, las reglas, los campos sin lock, los subsistemas notables.

Esa repetibilidad es precisamente el punto de tener un procedimiento de auditoría. El `LOCKING.md` de cada driver tiene una forma similar, porque las preguntas que se hacen durante la auditoría son siempre las mismas. Solo difieren las respuestas.

### Cómo detectan bugs las auditorías

Tres ejemplos concretos de bugs que una auditoría cuidadosa habría detectado:

**Ejemplo 1: lock ausente en una actualización.** Se añade un nuevo handler de sysctl que permite al usuario cambiar `tick_rate_hz` en tiempo de ejecución. El handler escribe el campo sin tomar `global_mtx`. La auditoría lo detecta: el campo está protegido por `global_mtx`, pero un camino de escritura no toma el lock.

**Ejemplo 2: inversión del orden de los locks.** Se añade una nueva función, `netsim_rebalance`. Toma `queue_mtx` para inspeccionar las longitudes de las colas, y luego toma `global_mtx` para actualizar la configuración basándose en esas longitudes. Este es el orden incorrecto. La auditoría lo detecta al preguntar: "¿para cada función, coincide su adquisición de locks con el orden global?".

**Ejemplo 3: lectura fragmentada de `packets_rx`.** Un handler de sysctl lee `packets_rx` con una carga de 64 bits simple. En amd64 esto es correcto; en plataformas de 32 bits no lo es. La auditoría lo detecta al documentar qué arquitecturas soporta el driver y señalar las suposiciones dependientes de la plataforma.

Cada uno de estos bugs es del tipo que evade las pruebas de un solo thread. La auditoría los detecta por ser sistemática.

### Las auditorías como requisito previo al cambio

En un proyecto de driver maduro, la auditoría se convierte en un requisito previo al cambio: cualquier commit que modifique estado compartido, añada un nuevo campo o cambie una adquisición de lock debe actualizar `LOCKING.md` en el mismo commit. Los revisores comprueban que la actualización es coherente con el cambio de código.

Esta disciplina puede parecer burocrática. En la práctica, es rápida (actualizar el documento lleva un minuto) y es la defensa más eficaz contra las regresiones de concurrencia. Un driver que se entrega con un `LOCKING.md` correcto es un driver cuya historia de concurrencia puede auditar cualquier revisor sin leer cada línea del código.

Para nuestro `myfirst`, el `LOCKING.md` es breve porque el driver es pequeño. Para drivers más grandes, el `LOCKING.md` crece en proporción. El valor también crece.

### Cerrando la sección 3

Tienes ahora, para tu propio driver, un registro campo a campo de cada pieza de estado compartido, cada sección crítica y cada mecanismo de protección. Tienes un archivo `LOCKING.md` que documenta la estrategia de locking. Tienes anotaciones en el código fuente que hacen visible la intención de concurrencia para cualquier lector. Has visto el mismo procedimiento aplicado a un driver hipotético más grande para demostrar que escala.

El resto del capítulo presenta las primitivas que te permiten añadir o relajar la protección a medida que tu diseño evoluciona. La sección 4 aborda los atómicos: qué ofrecen, qué no ofrecen y dónde encajan en tu driver. La sección 5 es el tratamiento completo de los mutexes, incluyendo los matices que el capítulo 10 apenas esbozó.



## Sección 4: Introducción a las operaciones atómicas

Las operaciones atómicas son una de las dos herramientas que presenta este capítulo. Son útiles cuando el estado compartido que necesitas proteger es pequeño y la operación que necesitas realizar sobre él es simple. No reemplazan a los mutexes para nada más complejo, pero a menudo son la herramienta adecuada para los casos más sencillos y siempre son más rápidas. Esta sección construye la intuición de qué son los atómicos, presenta la API `atomic(9)` que expone FreeBSD, analiza el ordenamiento de memoria en un nivel apropiado para nuestros propósitos y aplica la API de contadores por CPU `counter(9)` a las estadísticas del driver.

### Qué significa «atómico» en el nivel del hardware

Recuerda la carrera del contador de la sección 2. Dos threads cargaban, incrementaban y almacenaban la misma posición de memoria. Como la carga, el incremento y el almacenamiento eran tres instrucciones separadas, otro thread podía colarse entre ellas. La solución, en el nivel más sencillo, consiste en usar una única instrucción que realice las tres operaciones de forma indivisible: ningún otro thread puede observar la memoria a mitad de la operación.

Los procesadores modernos proporcionan este tipo de instrucciones. En amd64, la instrucción que suma de forma atómica un valor a una posición de memoria es `LOCK XADD`. El prefijo `LOCK` indica a la CPU que bloquee la línea de caché pertinente durante la instrucción, de modo que ninguna otra CPU pueda acceder a esa misma línea hasta que la instrucción termine. La propia instrucción `XADD` realiza un intercambio-suma: añade su operando fuente al destino y devuelve el valor antiguo del destino, todo en una única transacción. Tras `LOCK XADD`, la posición de memoria contiene la suma correcta y ninguna otra CPU ha podido observar una actualización parcial.

La primitiva C que se compila a esta instrucción es `atomic_fetchadd_int`. La API `atomic(9)` de FreeBSD la expone, junto con muchas otras de la misma familia, en `/usr/src/sys/sys/atomic_common.h` y en los cabeceros `atomic.h` por arquitectura.

**Atomicidad**, pues, es una propiedad de una *operación de memoria*: si la operación se completa, lo hace en una única transacción indivisible desde el punto de vista de la memoria. Ninguna otra CPU puede observar una versión a medio terminar. Es una garantía de hardware, no una abstracción de software. Cuando usas una primitiva atómica en C, le dices al compilador que emita una instrucción que el propio hardware garantiza que es indivisible.

Tres aspectos importan sobre esta garantía.

El primero es que cubre *una* operación. `atomic_fetchadd_int` realiza una suma. Dos operaciones atómicas consecutivas no son conjuntamente atómicas: otro thread puede observar la memoria entre ellas. Si necesitas actualizar dos campos a la vez, sigue siendo necesario un mutex.

El segundo es que cubre operaciones *del tamaño de una palabra*, para cierta definición de "palabra". En amd64, las primitivas atómicas cubren 8, 16, 32 y 64 bits. Las operaciones sobre estructuras más grandes (arrays, structs compuestos) no pueden ser atómicas en el nivel del hardware; requieren un mutex o un mecanismo especializado como `atomic128_cmpset` en las plataformas que lo ofrecen. La API `atomic(9)` expone las granularidades que soporta el hardware.

El tercero es que es *barata*. Un incremento atómico es, normalmente, unos pocos ciclos más lento que uno no atómico, porque la línea de caché debe adquirirse en modo exclusivo. Comparado con adquirir y liberar un mutex (que a su vez usa atómicos internamente y puede además consumir turnos del planificador bajo contención), una operación atómica suele ser un orden de magnitud más rápida. Cuando el trabajo que necesitas hacer es simple, los atómicos te dan corrección con un coste mínimo.

### La API atomic(9) de FreeBSD

El kernel expone una familia de operaciones atómicas mediante macros con forma de función. El conjunto completo está documentado en `atomic(9)` (ejecuta `man 9 atomic`). Para los fines de este capítulo nos interesa un subconjunto reducido.

Las cuatro operaciones más habituales son:

```c
void   atomic_add_int(volatile u_int *p, u_int v);
void   atomic_subtract_int(volatile u_int *p, u_int v);
u_int  atomic_fetchadd_int(volatile u_int *p, u_int v);
int    atomic_cmpset_int(volatile u_int *p, u_int expect, u_int new);
```

- `atomic_add_int(p, v)` calcula `*p += v` de forma atómica. El valor de retorno no es significativo; la actualización es indivisible.
- `atomic_subtract_int(p, v)` calcula `*p -= v` de forma atómica. Misma forma.
- `atomic_fetchadd_int(p, v)` calcula `*p += v` de forma atómica y devuelve el valor que contenía `*p` *antes* de la suma. Útil cuando quieres tanto actualizar como observar el valor anterior en la misma transacción.
- `atomic_cmpset_int(p, expect, new)` realiza un compare-and-swap de forma atómica: si `*p == expect`, escribe `new` en `*p` y devuelve 1; en caso contrario deja `*p` sin cambios y devuelve 0. Es la primitiva fundamental para construir estructuras de datos lock-free más complejas.

Cada una de estas tiene variantes para distintos anchos de entero (`_long`, `_ptr`, `_64`, `_32`, `_16`, `_8`) y para distintos requisitos de ordenación de memoria (`_acq`, `_rel`, `_acq_rel`). Las variantes de ancho son evidentes: operan sobre distintos tipos enteros. Las variantes de ordenación de memoria merecen su propia subsección.

También existen primitivas de lectura y escritura:

```c
u_int  atomic_load_acq_int(volatile u_int *p);
void   atomic_store_rel_int(volatile u_int *p, u_int v);
```

Son cargas y almacenamientos atómicos con garantías específicas de ordenación de memoria. Para tipos del tamaño de una palabra máquina alineados en nuestras plataformas, los accesos ordinarios con `*p` son atómicos en el sentido de carga/almacenamiento, pero las formas `atomic_load_acq_int` / `atomic_store_rel_int` actúan además como barreras de memoria: impiden que el compilador y la CPU reordenen las cargas y almacenamientos circundantes más allá de ellas. Veremos por qué eso importa en breve.

Por último, unas pocas primitivas especializadas:

```c
void   atomic_set_int(volatile u_int *p, u_int v);
void   atomic_clear_int(volatile u_int *p, u_int v);
```

Son OR y AND-NOT bit a bit respectivamente: `atomic_set_int(p, FLAG)` activa el bit `FLAG` en `*p`; `atomic_clear_int(p, FLAG)` lo desactiva. Son útiles para palabras de flags en las que múltiples threads pueden estar activando y desactivando bits distintos.

### Una breve introducción al orden de memoria

He aquí una sutileza que suele sorprender a los principiantes: incluso con operaciones atómicas, el *orden en que los accesos a memoria se hacen visibles para otras CPUs* no siempre coincide con el orden en que el código los ejecutó. Los procesadores modernos y los compiladores pueden reordenar instrucciones por razones de rendimiento, siempre que la reordenación sea invisible para el propio thread que las emitió. En un programa multihilo, la reordenación a veces sí es visible, y puede importar.

El ejemplo clásico es un productor que prepara una carga útil y luego activa un flag de listo, mientras un consumidor gira sobre el flag y después lee la carga útil:

```c
/* Thread A (producer): */
data = compute_payload();
atomic_store_rel_int(&ready_flag, 1);

/* Thread B (consumer): */
while (atomic_load_acq_int(&ready_flag) == 0)
        ;
use_payload(data);
```

Para que el patrón funcione, los dos almacenamientos del lado del thread A deben hacerse visibles para el thread B en orden: el thread B no debe ver `ready_flag == 1` mientras sigue viendo el `data` antiguo. Sin barreras de memoria, la CPU o el compilador pueden reordenar esos almacenamientos, y el consumidor leería datos obsoletos.

El sufijo `_rel` en `atomic_store_rel_int` es una barrera **release**: toda escritura que haya ocurrido antes del almacenamiento, en el orden del programa, se hace visible para otras CPUs antes que el propio almacenamiento. El sufijo `_acq` en `atomic_load_acq_int` es una barrera **acquire**: toda lectura que ocurra después de la carga, en el orden del programa, ve valores al menos tan recientes como el valor que vio la carga. Usados en pareja (release en el publicador y acquire en el consumidor), se garantiza que el consumidor observa todo lo que el publicador hizo antes del release.

En este capítulo no construiremos estructuras de datos lock-free; la subsección «Ordenación de memoria en una máquina multinúcleo» más adelante en la sección 4 profundiza algo más en las garantías de ordenación, y los patrones lock-free reales pertenecen a la Parte 6 (capítulo 28) cuando drivers específicos los necesiten. Lo importante por ahora es que el mismo patrón `_rel` / `_acq` es la razón por la que `mtx_lock` y `mtx_unlock` se componen correctamente con la memoria: `mtx_lock` tiene semántica acquire (nada dentro de la sección crítica se escapa por encima) y `mtx_unlock` tiene semántica release (nada dentro de la sección crítica se escapa por debajo).

Para un principiante, la conclusión práctica es la siguiente: usa la primitiva atómica que corresponde a la intención de tu operación. Un incremento de contador usa la forma simple. Un flag que publica la finalización de otro trabajo usa el par `_rel` / `_acq`. Ante la duda, usa la forma más fuerte (`_acq_rel`); cuesta algo más pero es correcta en más situaciones.

### Cuándo bastan los atómicos

Los atómicos son la herramienta adecuada cuando se cumplen los cuatro criterios siguientes:

1. La operación que necesitas es simple (un incremento de contador, la activación de un flag, el intercambio de un puntero).
2. El estado compartido es un único campo del tamaño de una palabra (o un número pequeño de campos independientes).
3. La corrección del código no depende de que más de una posición de memoria sea consistente con otra (sin invariantes compuestos).
4. La operación es suficientemente barata para que sustituir un mutex por un atómico suponga una mejora observable.

Para `myfirst`, los contadores `bytes_read` y `bytes_written` cumplen los cuatro criterios. Son incrementos simples, campos individuales, independientes entre sí y actualizados con frecuencia en la ruta de datos. Convertirlos de campos protegidos por mutex a contadores atómicos (o por CPU) es una mejora limpia.

El cbuf, en cambio, no cumple los criterios. Su corrección depende de un invariante compuesto (`cb_used <= cb_size`, los bytes en `[cb_head, cb_head + cb_used)` están activos) que abarca varios campos. Ninguna operación atómica única puede preservar ese invariante. El cbuf necesita un mutex, y ninguna astucia con atómicos cambiará eso. Vale la pena decirlo con claridad, porque los principiantes a veces intentan «actualizar atómicamente cb_used y cb_head» y acaban con un driver que compila, parece inteligente y sigue estando roto.

### Contadores por CPU: la API counter(9)

FreeBSD proporciona, para el caso concreto de "un contador que se incrementa con frecuencia y se lee con poca frecuencia", una API especializada: `counter(9)`. Un `counter_u64_t` es un contador por CPU, donde cada CPU dispone de su propia memoria privada para el contador, y una lectura combina todos ellos.

La API es:

```c
counter_u64_t   counter_u64_alloc(int flags);
void            counter_u64_free(counter_u64_t c);
void            counter_u64_add(counter_u64_t c, int64_t v);
uint64_t        counter_u64_fetch(counter_u64_t c);
void            counter_u64_zero(counter_u64_t c);
```

`counter_u64_alloc(M_WAITOK)` devuelve un identificador para un nuevo contador por CPU. `counter_u64_add(c, 1)` suma 1 de forma atómica a la copia privada de la CPU que hace la llamada (no se requiere sincronización entre CPUs). `counter_u64_fetch(c)` suma los valores de todas las CPUs y devuelve el total. `counter_u64_free(c)` libera el contador.

El diseño por CPU tiene dos consecuencias. La primera es que las sumas son muy rápidas: solo tocan la línea de caché de la CPU que hace la llamada, por lo que no hay contención entre CPUs. Incluso en un sistema de 32 núcleos, una llamada a `counter_u64_add` no paga el coste de sincronizarse con los demás núcleos. La segunda es que las lecturas son costosas: `counter_u64_fetch` suma los valores de todas las CPUs, lo que cuesta aproximadamente un fallo de caché por CPU. Por eso las lecturas deben ser poco frecuentes y las actualizaciones, frecuentes.

Esta forma es exactamente la adecuada para los contadores `bytes_read` y `bytes_written`. Se actualizan en cada llamada de I/O (tasa alta). Solo se leen cuando el usuario ejecuta `sysctl` o cuando detach emite una línea de log final (tasa baja). Migrarlos a `counter_u64_t` nos proporciona tanto corrección en arquitecturas de 32 y 64 bits como escalabilidad en sistemas con muchas CPUs.

### Migración de los contadores del driver

Así es como queda la migración. Primero, cambia los campos en `struct myfirst_softc`:

```c
struct myfirst_softc {
        /* ... */
        counter_u64_t   bytes_read;
        counter_u64_t   bytes_written;
        /* ... */
};
```

Actualiza `myfirst_attach` para asignarlos:

```c
static int
myfirst_attach(device_t dev)
{
        /* ... existing setup ... */
        sc->bytes_read = counter_u64_alloc(M_WAITOK);
        sc->bytes_written = counter_u64_alloc(M_WAITOK);
        /* ... rest of attach ... */
}
```

Actualiza `myfirst_detach` para liberarlos:

```c
static int
myfirst_detach(device_t dev)
{
        /* ... existing teardown ... */
        counter_u64_free(sc->bytes_read);
        counter_u64_free(sc->bytes_written);
        /* ... rest of detach ... */
}
```

Actualiza los manejadores de lectura y escritura para usar `counter_u64_add` en lugar del incremento directo:

```c
counter_u64_add(sc->bytes_read, got);
```

Actualiza los manejadores de sysctl para usar `counter_u64_fetch`:

```c
static int
myfirst_sysctl_bytes_read(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        uint64_t v = counter_u64_fetch(sc->bytes_read);
        return (sysctl_handle_64(oidp, &v, 0, req));
}
```

Y registra el sysctl con un manejador en lugar de un puntero directo:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "bytes_read",
    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    sc, 0, myfirst_sysctl_bytes_read, "QU",
    "Total bytes drained from the FIFO");
```

Una vez completada la migración, puedes eliminar las entradas `sc->bytes_read` y `sc->bytes_written` de la lista de campos protegidos por `sc->mtx` en tu `LOCKING.md`. Ahora se protegen solos, de una forma que escala mejor y es correcta en cualquier arquitectura que soporte FreeBSD.

### Un ejemplo sencillo de flag atómico

Veamos también un uso más pequeño y concreto de los atómicos. Supón que queremos saber si el driver está «actualmente ocupado» en un sentido amplio, para depuración. No necesitamos corrección estricta; solo queremos un flag que podamos activar y leer.

Añade un campo:

```c
volatile u_int busy_flag;
```

Actualízalo en los manejadores:

```c
atomic_set_int(&sc->busy_flag, 1);
/* ... do the work ... */
atomic_clear_int(&sc->busy_flag, 1);
```

Léelo en otro lugar:

```c
u_int is_busy = atomic_load_acq_int(&sc->busy_flag);
```

Esto *no* es una forma correcta de implementar la exclusión mutua. Dos threads podrían leer ambos `busy_flag == 0`, ambos ponerlo a 1, ambos hacer su trabajo, ambos borrarlo. El flag no impide la ejecución concurrente; es puramente informativo. Para la exclusión mutua real, necesitamos un mutex, que es el tema de la siguiente sección.

El propósito del ejemplo del flag es más limitado: las operaciones atómicas permiten activar y leer un valor de una sola palabra sin desgarro. Eso es útil para un campo informativo que no participa en un invariante. En cuanto el campo necesita participar en un invariante, los atómicos solos no te salvarán.

### El patrón compare-and-swap

La primitiva atómica más interesante es `atomic_cmpset_int`, que implementa el patrón **compare-and-swap**. Te permite escribir código optimista: «actualiza el campo si nadie más lo ha modificado desde la última vez que lo comprobaste».

El patrón es:

```c
u_int old, new;
do {
        old = atomic_load_acq_int(&sc->field);
        new = compute_new(old);
} while (!atomic_cmpset_int(&sc->field, old, new));
```

El bucle lee el valor actual, calcula el nuevo valor que debería tener a partir de él, y trata de intercambiar atómicamente ese nuevo valor, condicionado a que el campo no haya cambiado. Si otro thread ha modificado el campo entre la lectura y el cmpset, el cmpset falla y el bucle reintenta con el nuevo valor. Cuando tiene éxito, la actualización se confirma de forma atómica, y `*sc->field` contiene entonces el valor derivado del estado que realmente tenía en el momento de la actualización.

El compare-and-swap es el bloque de construcción de la mayoría de las estructuras de datos sin locks. Puedes implementar una pila sin locks, una cola sin locks, un contador sin locks (que es como atomic_fetchadd suele implementarse internamente) y muchas otras estructuras usando únicamente compare-and-swap.

Para nuestros propósitos, el compare-and-swap merece conocerse aunque no lo utilicemos con frecuencia. Cuando leas código fuente de FreeBSD más adelante y veas `atomic_cmpset_*` en un bucle compacto, reconocerás el patrón de inmediato: reintento optimista.

### Cuándo los atómicos no son suficientes

Hay tres situaciones que requieren un mutex aunque el estado parezca sencillo.

Primera: cuando la operación involucra más de una posición de memoria. Si `cb_head` y `cb_used` deben actualizarse de forma conjunta, ninguna operación atómica por sí sola puede hacerlo. O bien aceptamos que la actualización consiste en dos atómicos (con un estado transitorio a medio actualizar visible para los lectores), o bien mantenemos un mutex a lo largo de toda la actualización.

Segunda: cuando la operación es costosa (incluye una llamada a función, un bucle o asigna memoria). Una operación atómica es barata solo si se trata de una única instrucción rápida. Una sección crítica larga no puede reducirse a un solo atómico; necesita un mutex.

Tercera: cuando el código debe bloquearse. Ninguna operación atómica puede poner al invocador a dormir. Si necesitas esperar a una condición («el buffer tiene datos»), necesitas `mtx_sleep` o una variable de condición, que requieren un mutex.

El cbuf activa las tres. Los contadores no activan ninguna. Por eso los contadores pueden migrar a atómicos o a `counter(9)` y el cbuf no.

### Un ejemplo desarrollado: un contador de «versión del driver»

A continuación tienes un ejercicio para afianzar la teoría. Supón que queremos contar cuántas veces se ha cargado y descargado el driver durante el uptime actual. Se trata de estado a nivel de módulo, no por softc, y se actualiza exactamente dos veces por ciclo de carga/descarga: una en el gestor de eventos del módulo cuando la carga tiene éxito, y otra cuando se descarga.

Podríamos protegerlo con un mutex global. Eso sería excesivo: la actualización es un único entero, ocurre pocas veces y no se compone con ninguna otra cosa. Un atómico es la elección correcta:

```c
static volatile u_int myfirst_generation = 0;

static int
myfirst_modevent(module_t m, int event, void *arg)
{
        switch (event) {
        case MOD_LOAD:
                atomic_add_int(&myfirst_generation, 1);
                return (0);
        case MOD_UNLOAD:
                return (0);
        default:
                return (EOPNOTSUPP);
        }
}
```

Un lector (por ejemplo un sysctl que exponga `hw.myfirst.generation`) puede usar:

```c
u_int gen = atomic_load_acq_int(&myfirst_generation);
```

No se necesita lock. No hay contención. Correcto en todas las arquitecturas que FreeBSD soporta. Este es el caso apropiado para un atómico: un campo, una operación, sin invariante compuesta.

### Visibilidad, ordenamiento y el driver cotidiano

Antes de cerrar esta sección, una observación más sobre el ordenamiento de memoria y los mutexes.

Puede que te preguntes, dado que existen `atomic_store_rel_*` y `atomic_load_acq_*`, por qué el código del driver que escribimos en el capítulo 10 no los usaba. La razón es que `mtx_lock` lleva incorporada la semántica de adquisición, y `mtx_unlock` lleva incorporada la semántica de liberación. Toda escritura dentro de una sección crítica se hace visible al siguiente thread que adquiere el mutex, en el momento en que lo adquiere. Toda lectura dentro de una sección crítica ve las escrituras que se produjeron antes de cualquier `mtx_unlock` previo sobre el mismo mutex. El mutex es, entre otras cosas, una barrera de memoria.

Así que cuando escribes:

```c
mtx_lock(&sc->mtx);
sc->field = new_value;
mtx_unlock(&sc->mtx);
```

no necesitas recurrir a `atomic_store_rel_int(&sc->field, new_value)`. El mutex ya realiza el ordenamiento necesario. Esta es una propiedad importante: significa que el código dentro de una sección crítica de mutex no necesita razonar sobre el ordenamiento de memoria. La corrección depende de la exclusión mutua, punto.

Fuera de las secciones críticas (y solo fuera de ellas), tienes que razonar tú mismo sobre el ordenamiento. Ahí es donde las variantes atómicas `_acq` / `_rel` justifican su existencia.

En el driver `myfirst`, el único estado al que accedemos fuera de cualquier lock es `sc->is_attached` (la optimización en la entrada del gestor) y los contadores por CPU (que gestionan su propio ordenamiento). Todo lo demás es inmutable tras el attach o está protegido por `sc->mtx`. Eso supone una huella reducida, y es la razón por la que nuestra gestión de la concurrencia sigue siendo manejable.

### El ordenamiento de memoria en una máquina multinúcleo

La anterior «Introducción suave al ordenamiento de memoria» esbozó qué hacen `_acq` y `_rel`; esta sección concreta un poco más el mecanismo subyacente, porque una vez que has implementado un patrón de publicación a mano, el resto de los primitivos atómicos del kernel resultan mucho más fáciles de leer.

El ejemplo es el mismo patrón productor/consumidor con dos variables compartidas, `payload` y `ready`. Sin barreras, dos ordenamientos pueden fallar:

- **En el lado del productor**, la escritura en `payload` podría hacerse visible para otros CPU *después* de la escritura en `ready`. El consumidor ve entonces `ready == 1` mientras sigue viendo el valor antiguo de `payload`.
- **En el lado del consumidor**, la lectura de `payload` podría elevarse *antes* de la lectura de `ready`. El consumidor se compromete con el payload antiguo antes de inspeccionar el flag.

¿Por qué es legal cualquiera de estos reordenamientos? El compilador tiene permitido mover cargas y almacenamientos entre sí siempre que el resultado sea coherente para un observador de un solo thread. La CPU tiene permitido reordenar accesos a través de su store buffer, su unidad de prefetch y su motor de ejecución fuera de orden, siempre que el thread local no pueda detectarlo. La visibilidad entre múltiples threads es una propiedad que ni el compilador ni la CPU garantizan por defecto.

Los sufijos `_rel` y `_acq` imponen exactamente las restricciones que necesitas:

- `atomic_store_rel_int(&ready, 1)`: todo almacenamiento que apareció antes de esta línea en el orden del programa se hace visible antes de que el almacenamiento en `ready` sea visible. La *release* publica todo lo previo.
- `atomic_load_acq_int(&ready)`: toda carga que aparece después de esta línea ve la memoria al menos tan actualizada como la propia carga. La *acquire* delimita las lecturas posteriores.

La combinación de release y acquire crea una relación de *happens-before*: si el consumidor observa `ready == 1` a través de `atomic_load_acq_int`, todo almacenamiento que el productor realizó antes de su `atomic_store_rel_int` es visible para el consumidor.

En tu driver, el mutex proporciona la misma propiedad. `mtx_unlock(&sc->mtx)` lleva incorporada la semántica de release. `mtx_lock(&sc->mtx)` tiene semántica de acquire. Toda escritura realizada mientras se mantenía el mutex es visible para el siguiente thread que adquiera el mismo mutex. Por eso, dentro de una sección crítica, la asignación simple en C está bien: los límites del mutex gestionan el ordenamiento por ti.

Fuera de cualquier lock, si coordinas múltiples campos con atómicos, tienes que pensar explícitamente en qué escrituras deben ser visibles conjuntamente y recurrir a las variantes `_rel` y `_acq` correspondientes. En el driver del capítulo 11, el único lugar donde esto importa son los contadores por CPU, y `counter(9)` gestiona el ordenamiento internamente.

### Intercambios atómicos y más variantes

Hemos mencionado `atomic_add`, `atomic_fetchadd`, `atomic_cmpset`, `atomic_load_acq`, `atomic_store_rel`, `atomic_set` y `atomic_clear`. Merece la pena nombrar dos más, porque los encontrarás en el código fuente de FreeBSD.

**`atomic_swap_int(p, v)`**: intercambia atómicamente el valor en `*p` por `v` y devuelve el valor antiguo. Es útil cuando quieres «reclamar» un recurso: el thread que consiga intercambiar el flag de «libre» a «ocupado» es el nuevo propietario.

**`atomic_readandclear_int(p)`**: lee `*p` atómicamente y lo pone a cero, devolviendo el valor antiguo. Es útil para patrones de vaciado en los que un thread recoge periódicamente y reinicia un contador que otros threads han ido incrementando.

Ambos se construyen sobre el mismo primitivo de compare-and-swap que proporciona el hardware, y ambos tienen el mismo perfil de coste que `atomic_cmpset`.

### Un ejemplo práctico: conteo de referencias atómico

Un patrón habitual en los drivers es el conteo de referencias: se asigna un objeto, varios threads toman referencias a él y el objeto se libera cuando se descarta la última referencia. Los atómicos son la herramienta adecuada.

```c
struct myobj {
        volatile u_int refcount;
        /* ... other fields ... */
};

static void
myobj_ref(struct myobj *obj)
{
        atomic_add_int(&obj->refcount, 1);
}

static void
myobj_release(struct myobj *obj)
{
        u_int old = atomic_fetchadd_int(&obj->refcount, -1);
        KASSERT(old > 0, ("refcount went negative"));
        if (old == 1) {
                /* We were the last reference; free it. */
                free(obj, M_DEVBUF);
        }
}
```

Merece la pena destacar dos detalles.

El primero es el uso de `atomic_fetchadd_int` para el decremento. ¿Por qué no `atomic_subtract_int`? Porque necesitamos saber si *nuestro* decremento fue el que llevó el contador a cero, para poder liberar el objeto. `atomic_fetchadd_int` devuelve el valor anterior a la suma, que nos dice exactamente eso. Un `atomic_subtract_int` no devuelve ningún valor.

El segundo es que la liberación debe producirse *solo si* nuestro decremento fue el último. De lo contrario, si dos threads coinciden en liberar de forma concurrente, ambos podrían intentar liberar el objeto. Al condicionar la liberación a «old == 1», garantizamos que exactamente un thread (el que llevó el contador de 1 a 0) libera el objeto. El resto de threads simplemente decrementa y retorna.

Este patrón se usa en todo el kernel de FreeBSD. Es la razón por la que `refcount(9)` existe como una pequeña API de envoltura (`refcount_init`, `refcount_acquire`, `refcount_release`). Por ahora no usamos el conteo de referencias; el ejemplo es para tu caja de herramientas.

### counter(9) en mayor profundidad

La API `counter(9)` se usa en todo el kernel de FreeBSD para contadores de alta frecuencia. Vale la pena entender cómo funciona, porque las decisiones de diseño explican la forma de la API.

Cada `counter_u64_t` es, internamente, un array de contadores por CPU. El array tiene el tamaño del número de CPU del sistema. Una suma se dirige al slot de la CPU invocante. Una lectura itera sobre todos los slots y los suma.

El diseño por CPU implica:

- **Las sumas no generan contención.** Dos CPU que suman de forma concurrente tocan líneas de caché distintas. No hay tráfico entre CPUs.
- **Las lecturas son costosas.** Tocan la línea de caché de cada CPU, lo que en una máquina de 32 núcleos supone 32 fallos de caché.
- **Las lecturas no son atómicas con las sumas.** Una lectura puede pillar a una CPU en mitad de una actualización. El total devuelto puede diferir ligeramente del total real instantáneo. Esto está bien para contadores informativos; sería incorrecto para contadores que alimentan decisiones.

En nuestro driver, las lecturas ocurren en los gestores de sysctl, que son poco frecuentes, por lo que la asimetría juega a nuestro favor. Si alguna vez necesitáramos un contador cuyo valor deba ser exacto en cada lectura, `counter(9)` no sería el primitivo adecuado.

Un matiz: `counter_u64_zero` reinicia el contador a cero. Hacerlo mientras se están produciendo sumas *no* es atómico con ellas. Si un lector lee, ve un valor grande y pone a cero, es posible que se pierdan algunas sumas en curso. Para contadores meramente informativos, esto es aceptable. Para contadores que rastrean presupuesto o cuota, reinicia con cuidado o no lo hagas en absoluto.

### Estructuras de datos sin lock en contexto

Un tratamiento completo de las estructuras de datos sin lock llenaría su propio libro. El kernel de FreeBSD las usa en lugares concretos donde la contención sería de otro modo un cuello de botella:

- **`buf_ring(9)`** es una cola multi-productor sin lock que usan los drivers de red y almacenamiento para los caminos calientes. El driver elige el modo de consumidor único o multi-consumidor a través de los flags pasados a `buf_ring_alloc()`, por lo que el mismo primitivo sirve para cualquiera de las dos formas según las necesidades de concurrencia del subsistema. Para la superficie completa de la API, consulta la entrada `buf_ring(9)` del apéndice B.
- **`epoch(9)`** proporciona un patrón predominantemente de lectura en el que los lectores avanzan sin ninguna sincronización y los escritores se coordinan entre sí.
- **Los caminos rápidos de `mi_switch`** en el planificador usan operaciones atómicas para evitar mutexes por completo en el caso común.

Leer cualquiera de ellos es buen material de estudio, pero son especializados. Para un driver principiante, la combinación de `atomic` para campos individuales y `mutex` para estado compuesto cubre el 99% de los drivers reales. Nos quedamos con esos para el capítulo 11 y dejamos que el capítulo 12 y los posteriores introduzcan los patrones más sofisticados cuando los drivers específicos los necesiten.

### Cuándo usar un atómico (guía de decisión)

Como guía de decisión, usa este diagrama de flujo para decidir si convertir un campo en atómico o protegerlo con un lock:

1. ¿Es el campo una única palabra (8, 16, 32 o 64 bits en una plataforma donde ese tamaño es atómico)? Si no, debe estar protegido con lock.
2. ¿Depende la corrección de mi código de que este campo sea coherente con otro campo? Si es así, debe estar protegido con lock junto al otro campo.
3. ¿Implica mi operación sobre el campo más de una secuencia de lectura-modificación-escritura? Si es así, usa `atomic_cmpset` en un bucle o protege con lock.
4. ¿Necesita el código dormir mientras mantiene el estado? Si es así, usa un mutex y `mtx_sleep`; los atómicos no pueden dormir.
5. En caso contrario, una operación atómica es probablemente la herramienta adecuada.

Para el driver `myfirst`, este diagrama apunta a los atómicos para los contadores de bytes (ahora `counter(9)`), el contador de «generación» (el contador de carga/descarga de la sección 4) y los flags informativos. Apunta al mutex para todo lo que implica el cbuf.

### Cerrando la sección 4

Las operaciones atómicas son una herramienta precisa para un trabajo preciso: actualizar un único campo del tamaño de una palabra sin condiciones de carrera. Son rápidas, se combinan con primitivas de ordenamiento de memoria cuando es necesario y escalan bien. No reemplazan a los mutex para nada más complicado que un único campo.

En el driver, migramos los contadores de bytes a contadores por CPU de `counter(9)`, lo que los eliminó del conjunto de campos protegidos por `sc->mtx` y nos dio un mejor rendimiento bajo alta carga. El cbuf, con sus invariantes compuestas, sigue protegido por `sc->mtx`. La sección 5 retoma ese mutex y explica, en detalle, qué es y cómo funciona.

## Sección 5: Introducción a los locks (mutex)

Esta es la sección central del capítulo. Todo lo anterior ha sido preparación; todo lo que viene después refuerza y amplía. Al finalizar esta sección, comprenderás qué es un mutex, qué tipos ofrece FreeBSD, qué reglas aplica cada uno, por qué el capítulo 10 eligió el tipo concreto que utilizó y cómo verificar que tu driver las cumple.

### Qué es un mutex

Un **mutex** (abreviatura de "mutual exclusion", exclusión mutua) es una primitiva de sincronización que permite que un único thread lo posea a la vez. Los threads que intentan adquirir un mutex mientras está ocupado esperan hasta que el poseedor lo libere; entonces, uno de los que están en espera lo adquiere. La garantía es que, entre dos adquisiciones consecutivas del mismo mutex, exactamente un thread ha ejecutado la región de código comprendida entre su adquisición y su liberación.

Esa garantía es lo que convierte secuencias de instrucciones en **secciones críticas**: regiones de código cuya ejecución se serializa entre todos los threads que comparten el mutex. Un thread lector que posee el mutex puede leer cualquier número de campos con la certeza de que ningún escritor concurrente está produciendo valores inconsistentes. Un thread escritor que posee el mutex puede actualizar cualquier número de campos con la certeza de que ningún lector concurrente está observando un estado parcialmente actualizado.

Los mutex no impiden la concurrencia; la moldean. Múltiples threads pueden ejecutarse fuera de la sección crítica al mismo tiempo. Incluso pueden estar en cola para entrar en la sección crítica al mismo tiempo. Lo que no pueden hacer es ejecutar la sección crítica de forma concurrente. La serialización es la propiedad que obtenemos; el precio es el coste de adquirir y liberar, más cualquier tiempo de espera.

### Las dos formas de mutex en FreeBSD

FreeBSD distingue dos formas fundamentales de mutex, porque el kernel se ejecuta en dos conjuntos de contextos bien diferenciados y cada uno impone restricciones distintas.

**`MTX_DEF`** es el mutex por defecto, conocido habitualmente como **sleep mutex**. Cuando un thread intenta adquirir uno que ya está ocupado, el sistema lo pone a dormir (lo añade a una cola de espera, con su estado establecido en `TDS_SLEEPING`) hasta que el poseedor libere el mutex. Un sleep mutex puede bloquearse durante un tiempo arbitrariamente largo, por lo que el código que lo usa debe encontrarse en un contexto donde dormir esté permitido: un contexto de proceso ordinario, un kernel thread o un callout marcado como mpsafe. La mayor parte del código de un driver se ejecuta en contextos donde los sleep mutex son apropiados.

**`MTX_SPIN`** es un **spin mutex**. Cuando un thread intenta adquirir uno que ya está ocupado, *gira* (spin): ejecuta un bucle cerrado de comprobaciones atómicas hasta que el poseedor lo libera. Un spin mutex nunca duerme. Los spin mutex existen porque ciertos contextos (en concreto, los manejadores de interrupciones hardware en determinados sistemas) no pueden dormir en absoluto. Un thread que no puede dormir no puede esperar a un sleep mutex; necesita una primitiva que avance sin ceder el procesador. Los spin mutex tienen reglas adicionales: deshabilitan las interrupciones en la CPU que los adquiere, deben mantenerse durante duraciones muy cortas, y el código que los posee no puede llamar a ninguna función que pueda dormir.

Para el driver `myfirst`, `MTX_DEF` es la elección correcta. Nuestros manejadores se ejecutan en contexto de proceso, en nombre de syscalls de usuario. No se ejecutan en contexto de interrupción. Dormir está permitido. El mutex puede ser un sleep mutex, lo que resulta más sencillo e impone menos restricciones sobre lo que puede hacer la sección crítica.

La llamada a `mtx_init` del capítulo 10 especifica `MTX_DEF`:

```c
mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);
```

Ese último argumento es el tipo de lock. Si hubiéramos querido un spin mutex, sería `MTX_SPIN`, y el resto del driver habría estado más restringido.

### El ciclo de vida de un mutex

Todo mutex tiene un ciclo de vida: crear, usar, destruir. Las funciones son `mtx_init` y `mtx_destroy`:

```c
void mtx_init(struct mtx *m, const char *name, const char *type, int opts);
void mtx_destroy(struct mtx *m);
```

`mtx_init` inicializa los campos internos de la estructura del mutex y lo registra en `WITNESS` (si está habilitado) para que pueda aplicarse la comprobación de orden de locks. `mtx_destroy` lo destruye.

Los cuatro argumentos de `mtx_init` son:

- `m`: la estructura del mutex (normalmente un campo de una estructura mayor, no una asignación independiente).
- `name`: un identificador legible que aparece en `ps`, `dmesg` y los mensajes de `WITNESS`. Para nuestro driver, este valor es `device_get_nameunit(dev)` (por ejemplo, `"myfirst0"`).
- `type`: una cadena corta que clasifica el mutex; `WITNESS` agrupa los mutex con la misma cadena `type` como relacionados. Para nuestro driver, este valor es `"myfirst"`.
- `opts`: los flags, incluyendo `MTX_DEF` o `MTX_SPIN`, y flags opcionales como `MTX_RECURSE` (permite que el mismo thread adquiera el mutex varias veces) o `MTX_NEW` (garantiza que la memoria no está inicializada). Para nuestro driver, basta con `MTX_DEF`.

Los argumentos `name` y `type` son importantes para la observabilidad. Cuando un usuario ejecuta `procstat -kk`, ve `myfrd` (el mensaje de espera de `mtx_sleep`) y `myfirst0` (el nombre del mutex) en la información de espera del proceso. Cuando `WITNESS` detecta una inversión en el orden de locks, identifica el mutex por su `name`. Unos buenos nombres facilitan enormemente la depuración de problemas de concurrencia.

### Adquirir y liberar

Las dos operaciones fundamentales son `mtx_lock` y `mtx_unlock`. Ambas reciben un puntero al mutex:

```c
void mtx_lock(struct mtx *m);
void mtx_unlock(struct mtx *m);
```

`mtx_lock` adquiere el mutex. Si el mutex no está ocupado, la adquisición es inmediata (el coste es una operación atómica). Si el mutex está ocupado, el thread que llama se duerme en la lista de espera del mutex. Cuando el poseedor actual lo libera, uno de los que están en espera se despierta y adquiere el mutex.

`mtx_unlock` libera el mutex. Si hay threads en espera, uno se despierta y se planifica. Si no hay threads en espera, la liberación se completa con el coste de una simple escritura en memoria.

Estas dos operaciones se combinan en el patrón de sección crítica:

```c
mtx_lock(&sc->mtx);
/* critical section: mutual exclusion is guaranteed here */
mtx_unlock(&sc->mtx);
```

Dentro de la sección crítica, el thread que posee el mutex es el único que puede estar en cualquier sección crítica protegida por ese mismo mutex. Los demás threads que intentan entrar en la sección crítica están dormidos, esperando.

### Las demás operaciones

Hay otras operaciones que resultan útiles en determinadas situaciones:

**`mtx_trylock(&m)`**: intenta adquirir el mutex. Devuelve un valor distinto de cero si lo consigue (mutex adquirido) y cero si falla (mutex en posesión de otro thread). El thread que llama nunca duerme. Es útil cuando solo quieres hacer algo si el mutex está disponible, o cuando quieres evitar mantener un lock durante una operación potencialmente larga.

**`mtx_assert(&m, what)`**: comprueba que el mutex está o no está retenido, con fines de depuración. El argumento `what` puede ser `MA_OWNED` (el thread actual posee el mutex), `MA_NOTOWNED` (el thread actual no lo posee), `MA_OWNED | MA_RECURSED` (retenido de forma recursiva) o `MA_OWNED | MA_NOTRECURSED` (retenido, pero no de forma recursiva). En un kernel con `INVARIANTS`, si la comprobación falla se produce un panic. En un kernel sin `INVARIANTS`, no hace nada. Úsala con generosidad; no tiene coste en producción y detecta errores durante el desarrollo.

**`mtx_sleep(&chan, &mtx, pri, wmesg, timo)`**: la primitiva de reposo que utilizamos en el capítulo 10. Libera `mtx` de forma atómica, duerme en `chan` y vuelve a adquirir `mtx` antes de retornar. La atomicidad es fundamental: la liberación del lock y la entrada en el estado de reposo no pueden ser interrumpidas por un `wakeup` concurrente.

**`mtx_initialized(&m)`**: devuelve un valor distinto de cero si el mutex ha sido inicializado. Es útil en rutas de desmontaje poco frecuentes donde quieres verificar si necesitas llamar a `mtx_destroy`.

### Revisando el diseño del capítulo 10

Con el vocabulario ya establecido, vamos a releer las decisiones de diseño del capítulo 10 y confirmar que son correctas.

**Un único mutex que protege todo el estado compartido de softc.** El diseño más sencillo posible es un mutex que protege todo lo que se comparte. Eso es exactamente lo que tenemos. Es el punto de partida correcto para cualquier driver. Casi nunca es la respuesta equivocada, aunque un diseño más granular pudiera ofrecer mejor rendimiento. El bloqueo más granular tiene costes (ordenación de locks, complejidad conceptual, errores al añadir nuevos campos); un único mutex tiene la ventaja de ser obviamente correcto.

**`MTX_DEF` (sleep mutex).** El driver se ejecuta en contexto de proceso. Todo el trabajo se realiza en nombre de syscalls de usuario. No hay manejadores de interrupciones. `MTX_DEF` es la elección correcta.

**Lock adquirido para cada acceso al estado compartido.** Toda lectura y escritura de los campos protegidos se encuentra dentro de un par `mtx_lock` / `mtx_unlock`. La auditoría de la sección 3 lo confirmó.

**Lock liberado antes de llamar a funciones que pueden dormir.** `uiomove(9)` puede dormir (ante un fallo de página en espacio de usuario). `selwakeup(9)` puede adquirir sus propios locks y no debe llamarse mientras se posee nuestro mutex. Nuestros manejadores liberan el mutex antes de estas llamadas. Esta es la regla del capítulo 10; ahora podemos explicar *por qué* es importante, algo que hace la sección 5.6 a continuación.

**`mtx_sleep` usa el mutex como interlock.** La operación atómica de liberación y reposo es lo que evita una condición de carrera entre la comprobación de la condición y la entrada en reposo. Si no usáramos `mtx_sleep` y en su lugar liberáramos el lock y luego durmiéramos, un `wakeup` concurrente podría dispararse en la ventana entre la liberación y el inicio del reposo, y perderíamos ese `wakeup` para siempre. `mtx_sleep` existe precisamente para cerrar esa ventana.

Cada decisión tomada en el capítulo 10 es ahora una que podemos defender desde los principios fundamentales. El mutex no está ahí por costumbre; está ahí porque cada aspecto del diseño lo requiere.

### La regla del reposo con mutex

Una regla reaparece con suficiente frecuencia como para merecer un tratamiento propio: **no retengas un lock que no admite reposo durante una operación que puede dormir**. Para los mutex `MTX_DEF`, esto se traduce en: no retengas el mutex durante ninguna llamada que pueda dormir, a menos que estés usando `mtx_sleep` (que libera el mutex de forma atómica durante el tiempo que dura el reposo).

¿Por qué es importante esto?

En primer lugar, dormir con un mutex retenido bloquea a cualquier otro thread que necesite ese mismo mutex durante toda la duración del reposo. Si el reposo es largo, el rendimiento se desploma; si es indefinido, el sistema entra en deadlock.

En segundo lugar, dormir en el kernel implica al planificador, que puede necesitar sus propios locks. Si esos locks tienen un orden definido con respecto a tu mutex, y el mutex que retienes está en una posición superior en ese orden, puedes desencadenar una violación del orden de locks.

En tercer lugar, en kernels con `WITNESS` habilitado, dormir con un mutex retenido genera una advertencia. En kernels con `INVARIANTS` habilitado, ciertos casos específicos de esta regla (las primitivas de reposo más conocidas) provocarán un panic.

El alcance de esta regla es más amplio de lo que parece a primera vista. Una llamada que "puede dormir" incluye:

- `malloc(9)` con `M_WAITOK`.
- `uiomove(9)`, `copyin(9)`, `copyout(9)` (cada una puede fallar en memoria de usuario y esperar a que la página sea traída desde disco).
- La mayoría de las funciones de la capa `vfs(9)`.
- La mayoría de las funciones de la capa `file(9)`.
- `taskqueue_enqueue(9)` en algunos caminos de ejecución.
- Cualquier función cuya cadena de implementación incluya alguna de las anteriores.

La técnica práctica consiste en identificar cada función a la que llamas desde dentro de una sección crítica y preguntarte: «¿puede esta función dormir?». Si la respuesta es sí, o si no estás seguro, libera el lock antes de la llamada. Los manejadores del capítulo 10 siguen esta regla: liberan `sc->mtx` antes de cada llamada a `uiomove`, y lo liberan también antes de cada llamada a `selwakeup`.

### Inversión de prioridad y propagación de prioridad

Un problema sutil de concurrencia es la **inversión de prioridad**. Supón que el thread L (baja prioridad) adquiere el mutex M. El thread H (alta prioridad) quiere adquirir M y tiene que esperar a L. Mientras tanto, el thread M (prioridad media, sin relación con la variable mutex) está realizando trabajo no relacionado con el mutex. El planificador, al ver que H está bloqueado y que M está listo para ejecutarse, planificará M por encima de L. L no avanza. M impide que L se ejecute. H sigue esperando a L. Un thread de prioridad media ha bloqueado efectivamente a uno de alta prioridad, aunque no comparten ningún recurso.

Esto es la inversión de prioridad. Es un error bien conocido; la misión Mars Pathfinder experimentó brevemente una variante de él en la década de 1990.

El kernel de FreeBSD gestiona la inversión de prioridad mediante **propagación de prioridad** (también llamada herencia de prioridad). Cuando un thread de alta prioridad se bloquea en un mutex que posee un thread de menor prioridad, el kernel eleva temporalmente la prioridad del poseedor hasta igualar la del thread en espera. L se ejecuta ahora con la prioridad de H, de modo que M no puede interrumpirlo, y L termina la sección crítica rápidamente. Cuando L libera el mutex, su prioridad vuelve a su valor original, H adquiere el mutex y la inversión queda resuelta.

La consecuencia práctica para quien escribe drivers es que, en la mayoría de los casos, no necesitas preocuparte por la inversión de prioridad. El kernel lo gestiona por ti en cualquier mutex `MTX_DEF`. Sin embargo, el mecanismo tiene un coste: el poseedor de un mutex en disputa puede ejecutarse con una prioridad más alta de la que tendría en otras circunstancias, potencialmente durante toda la duración de la sección crítica. Esa es otra razón más para mantener las secciones críticas lo más breves posible.

### Orden de locks y deadlocks

La inversión de prioridad es un problema dentro de un único mutex. El **deadlock** es un problema que afecta a múltiples mutexes.

Imagina dos mutexes, A y B. El thread 1 adquiere A y luego quiere adquirir B. El thread 2 adquiere B y luego quiere adquirir A. Cada thread retiene lo que el otro necesita. Ninguno libera lo que tiene hasta que consigue lo que quiere. Ninguno puede avanzar. El sistema ha llegado a un deadlock.

La defensa clásica contra el deadlock es el **orden de locks**: todos los threads adquieren sus locks en el mismo orden global. Si todos los threads que necesitan tanto A como B siempre adquieren A antes que B, este tipo de deadlock es imposible. El thread 2 adquiriría A antes que B, no al revés; si no pudiera obtener A, esperaría; una vez que lo tuviera, adquiriría B; no retendría B esperando a A.

En FreeBSD, `WITNESS` impone el orden de locks. La primera vez que el kernel observa un thread que retiene el lock A y adquiere el lock B, registra el orden A-antes-que-B como válido. Si más adelante detecta que otro thread (o incluso el mismo) retiene B e intenta adquirir A, eso es una inversión del orden de locks, y `WITNESS` imprime una advertencia. Si `INVARIANTS` también está habilitado y la configuración lo exige, la advertencia se convierte en un panic.

En el driver `myfirst`, solo tenemos un mutex. No hay ningún orden de locks que gestionar (un mutex tiene un orden trivial consigo mismo: o lo tienes o no lo tienes, y `mtx_lock` sin `MTX_RECURSE` provocará un panic si el thread que ya retiene el mutex intenta adquirirlo de nuevo). A medida que el driver crezca y se introduzcan mutexes adicionales, habrá que definir y documentar un orden de locks. El capítulo 12 trata esto en profundidad para el caso de múltiples clases de locks (por ejemplo, un mutex para la ruta de control y un lock `sx` para la ruta de datos).

### WITNESS: qué detecta

`WITNESS` es el comprobador de orden de locks y disciplina de bloqueo del kernel. Se habilita con `options WITNESS` en la configuración del kernel y suele combinarse con `options INVARIANTS` para obtener la máxima cobertura.

Lo que `WITNESS` detecta:

- **Inversiones del orden de locks**: un thread adquiere locks en un orden que no coincide con el observado anteriormente.
- **Dormir con un lock no dormible retenido**: un thread llama a `msleep`, `tsleep`, `cv_wait` o cualquier otra primitiva de espera mientras retiene un mutex `MTX_SPIN`.
- **Adquisición recursiva de un lock no recursivo**: un thread intenta adquirir un mutex que ya retiene y el mutex no fue inicializado con `MTX_RECURSE`.
- **Liberación de un lock no retenido**: un thread llama a `mtx_unlock` sobre un mutex que no retiene.
- **Dormir con determinados mutexes de espera nominados retenidos**: más concretamente, si `INVARIANTS` también está habilitado, se comprueba `mtx_assert(MA_NOTOWNED)` antes de dormir.

Lo que `WITNESS` no detecta:

- **Locks ausentes**: si olvidaste tomar un lock, `WITNESS` no tiene forma de saber que deberías haberlo hecho.
- **Uso de un mutex después de liberarlo**: si destruyes un mutex y luego lo usas, `WITNESS` puede o no detectarlo dependiendo de la rapidez con que se reutilice la memoria.
- **Carreras de datos sin locks**: dos threads que acceden a la misma variable sin protección son invisibles para `WITNESS`.

Por este motivo, `WITNESS` es una herramienta de lint, no una prueba definitiva. Un driver puede superar `WITNESS` y seguir siendo incorrecto. Pero un driver que falla en `WITNESS` casi con certeza está mal escrito, y las advertencias suelen ser lo suficientemente específicas como para señalar la línea exacta.

Activa `WITNESS` e `INVARIANTS` en tu kernel de desarrollo. Ejecuta el conjunto de pruebas del driver sobre el kernel de depuración. Si aparecen advertencias, investiga cada una de ellas. Este es el hábito más eficaz en la depuración de drivers.

### Ejecución del driver bajo WITNESS

Si no lo has hecho todavía, construye un kernel de depuración. En un sistema FreeBSD 14.3, el kernel `GENERIC` instalado no tiene `WITNESS` ni `INVARIANTS` habilitados, porque esas opciones tienen un coste en tiempo de ejecución no deseable en producción. Tienes que construir un kernel que sí los incluya. La forma más sencilla es copiar `GENERIC` y añadir las opciones correspondientes. Las líneas que necesitas son:

```text
options         INVARIANTS
options         INVARIANT_SUPPORT
options         WITNESS
options         WITNESS_SKIPSPIN
```

La sección «Construcción y arranque de un kernel de depuración», más adelante en este capítulo, describe el procedimiento completo de build, instalación y reinicio paso a paso. La versión resumida es:

```sh
# cd /usr/src
# make buildkernel KERNCONF=MYFIRSTDEBUG
# make installkernel KERNCONF=MYFIRSTDEBUG
# shutdown -r now
```

donde `MYFIRSTDEBUG` es el nombre del archivo de configuración del kernel que creaste.

Tras reiniciar, carga el driver y ejecuta una prueba. Si `WITNESS` se activa, `dmesg` mostrará algo parecido a:

```text
lock order reversal:
 1st 0xfffffe00020b8a30 myfirst0 (myfirst, sleep mutex) @ ...:<line>
 2nd 0xfffffe00020b8a38 foo_lock (foo, sleep mutex) @ ...:<line>
lock order foo -> myfirst established at ...
```

La advertencia nombra ambos mutexes, sus direcciones, sus tipos y las ubicaciones en el código fuente implicadas. A partir de ahí, trazas el código y corriges el orden.

Para el driver de la etapa 4 del capítulo 10, `WITNESS` debería permanecer en silencio: tenemos un único mutex, usado de forma consistente, y no hay ninguna violación de dormir con mutex retenido. Si ves advertencias, hay un bug que merece investigación.

### Cuándo un mutex no es la solución adecuada

Hay tres situaciones que requieren algo distinto a un mutex `MTX_DEF` simple. El capítulo 12 cubre cada una en profundidad; las mencionamos aquí para que el lector conozca el panorama.

**Muchos lectores, pocos escritores.** Si la sección crítica es mayoritariamente de solo lectura, con escrituras ocasionales, un mutex serializa a los lectores innecesariamente. Un lock de lector/escritor (`sx(9)` o `rw(9)`) permite que muchos lectores retengan el lock simultáneamente y serializa únicamente a los escritores. El coste es un mayor overhead por adquisición/liberación y más complejidad en las reglas; el beneficio es la escalabilidad en cargas de trabajo intensas en lectura.

**Espera bloqueante sobre una condición, no sobre un lock.** Cuando un thread necesita esperar hasta que una condición específica se cumpla (por ejemplo, «hay datos disponibles en el cbuf»), `mtx_sleep` con un canal es una forma de expresarlo. Una **variable de condición** nominada (`cv(9)`) es otra forma, a menudo más limpia y explícita. El capítulo 12 cubre `cv_wait`, `cv_signal` y `cv_broadcast`.

**Operaciones muy cortas sobre un único campo.** Operaciones atómicas, como vimos en la sección 4. No se necesita ningún mutex.

Para `myfirst`, ninguna de estas situaciones aplica todavía. Las secciones críticas son cortas, las operaciones implican invariantes compuestos y ningún patrón de lectura intensiva ni de variable de condición encaja mejor que el mutex que tenemos. El capítulo 12 presentará las alternativas cuando el driver evolucione hasta el punto en que estén justificadas.

### Un recorrido paso a paso: trazar un lock

Para concretar la mecánica, vamos a trazar lo que ocurre cuando dos threads compiten por el mutex.

El thread A llama a `myfirst_read`. Llega a `mtx_lock(&sc->mtx)`. El mutex no está retenido (el estado inicial es «owner = NULL, waiters = none»). `mtx_lock` ejecuta una operación atómica de comparar e intercambiar: «si owner es NULL, establecer owner como curthread». La operación tiene éxito. A está ahora en la sección crítica.

El thread B llama a `myfirst_read` en una CPU diferente. Llega a `mtx_lock(&sc->mtx)`. El mutex está retenido (owner = A). La operación de comparar e intercambiar falla. `mtx_lock` tiene que organizar la espera de B.

B entra en el camino lento. Se añade a la lista de espera del mutex. Establece su estado de thread como bloqueado. Llama al planificador, que elige otro thread ejecutable (posiblemente ninguno, en cuyo caso la CPU queda inactiva).

Pasa el tiempo. A termina su sección crítica y llama a `mtx_unlock(&sc->mtx)`. `mtx_unlock` detecta que hay threads en espera. Elige uno (normalmente el de mayor prioridad, con FIFO entre prioridades iguales) y lo despierta. Ese thread en espera, probablemente B, se convierte en ejecutable.

El planificador ve a B como ejecutable y lo programa. B reanuda su ejecución dentro del camino lento de `mtx_lock`. `mtx_lock` registra ahora que el mutex está retenido por B y retorna. B está en la sección crítica.

Entre el `mtx_unlock` de A y el retorno de `mtx_lock` de B, el mutex estuvo sin propietario durante un instante breve. Ningún otro thread podría haberse colado, porque el desbloqueo y el despertar están organizados de forma que quien despierte a continuación sea el siguiente propietario. Esto es una de las cosas que `mtx_lock` hace correctamente y que una implementación artesanal de «comprobar un flag, dormir, comprobar de nuevo» no garantizaría.

Todo esto ocurre dentro de `/usr/src/sys/kern/kern_mutex.c`. Si abres ese archivo y buscas `__mtx_lock_sleep`, puedes ver el código del camino lento. Es más elaborado que el esquema anterior; gestiona la propagación de prioridades, el spinning adaptativo y varios casos límite. La idea central, sin embargo, es la que describe el esquema.

### Lectura de la historia de contención de un lock

Cuando un driver rinde mal bajo carga, una de las primeras cosas que hay que comprobar es si el mutex está en contención: con qué frecuencia los threads tienen que esperar y durante cuánto tiempo. FreeBSD proporciona esta información a través del árbol sysctl `debug.lock.prof.*`, que se habilita con la opción de kernel `LOCK_PROFILING`, y a través de la herramienta de espacio de usuario `lockstat(1)`, que forma parte del conjunto de herramientas DTrace.

No construiremos un análisis de rendimiento completo en este capítulo; eso corresponde al capítulo 12 y a la parte 4. Pero si tienes curiosidad, en un kernel construido con `options LOCK_PROFILING`, prueba:

```sh
# sysctl debug.lock.prof.enable=1
# ./producer_consumer
# sysctl debug.lock.prof.enable=0
# sysctl debug.lock.prof.stats
```

La salida lista todos los locks que el kernel ha observado, el tiempo máximo y total de espera, el tiempo máximo y total de retención, el número de adquisiciones y el archivo fuente con el número de línea donde se tocó el lock por última vez. Para nuestro driver, `myfirst0` debería aparecer con números modestos, porque las secciones críticas son cortas. Si los números fueran grandes, tendríamos una señal de que el mutex está en contención y un diseño más granular podría ayudar. Para los objetivos del capítulo 11, no estamos optimizando; estamos garantizando la corrección.

### Aplicando lo aprendido

Hagamos un resumen. En el driver de la etapa 4 del capítulo 10, el mutex `sc->mtx`:

- Es un mutex de espera `MTX_DEF`, creado en `myfirst_attach` y destruido en `myfirst_detach`.
- Se llama `"myfirst0"` (y nombres similares para otros números de unidad), con tipo `"myfirst"`.
- Es retenido por los manejadores de I/O en cada acceso al cbuf, a los contadores de bytes y a los demás campos protegidos.
- Se libera antes de cada llamada que pueda dormir (`uiomove`, `selwakeup`).
- Se usa como argumento interlock de `mtx_sleep`.
- Se verifica que está retenido dentro de las funciones auxiliares mediante `mtx_assert(MA_OWNED)`.
- Está documentado en el comentario de locking al principio del archivo y en `LOCKING.md`.

Con los conceptos de la sección 4 y la sección 5, cada una de esas propiedades se puede justificar desde los principios fundamentales. El mutex no es un mero trámite. Es la infraestructura que transforma un driver que funciona por casualidad en un driver que es correcto.

### Los spin mutexes en detalle

Hemos dicho que los spin mutexes (`MTX_SPIN`) existen porque algunos contextos no pueden dormir. Veamos con más detalle por qué.

El kernel de FreeBSD tiene varios contextos de ejecución. La mayor parte del código de un driver se ejecuta en **contexto de thread**: el kernel está ejecutando en nombre de algún thread (un thread de usuario que realizó un syscall, un thread del kernel dedicado a alguna tarea o un callout que se ejecuta en modo mpsafe). En el contexto de thread, dormir es legal: el planificador puede aparcar el thread y programar otro.

Un conjunto pequeño pero crítico de contextos no puede dormir. El más importante es el **contexto de interrupción hardware**: el código que se ejecuta cuando se dispara una interrupción hardware. Una interrupción puede apropiarse de cualquier thread en cualquier instante, ejecutar un manejador breve (llamado ithread o filtro) y retornar. Mientras el manejador se ejecuta, el thread que fue interrumpido no puede avanzar. El manejador debe terminar rápidamente y no debe bloquearse. Dormir significaría llamar al planificador, y llamar al planificador desde dentro de una interrupción no es seguro en las plataformas que FreeBSD soporta.

Otro contexto sin sueño es el de las **secciones críticas** abiertas con `critical_enter(9)`. Estas deshabilitan la apropiación en la CPU actual; el código interno se ejecuta hasta su finalización sin que el planificador pueda elegir un thread diferente. Las secciones críticas rara vez son usadas directamente por quien escribe drivers; aparecen más en el código del kernel de bajo nivel.

Para el código en cualquier contexto sin sueño, un mutex de espera es la herramienta equivocada. Adquirir un mutex de espera que ya está retenido requeriría dormir, y eso no es posible. Se necesita un mutex que haga spinning: que intente en un bucle ajustado hasta tener éxito.

Los mutexes `MTX_SPIN` hacen exactamente eso. Cuando llamas a `mtx_lock_spin(&m)`, el código:

1. Deshabilita las interrupciones en la CPU actual (porque de lo contrario, un manejador de interrupciones podría interrumpirte mientras tienes el spin mutex, lo que llevaría a un deadlock si el manejador necesita el mismo mutex).
2. Intenta una operación atómica de compare-and-swap para adquirir el mutex.
3. Si falla, gira en un bucle reintentándolo periódicamente.
4. Una vez adquirido, continúa con la sección crítica. Las interrupciones permanecen deshabilitadas.
5. `mtx_unlock_spin(&m)` libera el mutex y vuelve a habilitar las interrupciones.

Las reglas para los spin mutexes son estrictas:

- La sección crítica debe ser **muy corta**: mantener un spin mutex bloquea a todas las demás CPU que lo quieran y, además, deshabilita las interrupciones en la CPU actual. Los microsegundos importan.
- **No puedes dormir** mientras tienes un spin mutex. `malloc(9)` con `M_WAITOK` está prohibido. `mtx_sleep(9)` está prohibido. Incluso un fallo de página está prohibido (no debes acceder a memoria de usuario ni a memoria del kernel paginada bajo un spin mutex).
- **No puedes adquirir un sleep mutex** mientras tienes un spin mutex. El sleep mutex intentaría dormir si hay contención, y bajo un spin mutex no puedes dormir.

Para el driver `myfirst`, los spin mutexes son la elección equivocada. Nunca ejecutamos en contexto de interrupción. Nuestras secciones críticas pueden llamar a los helpers de cbuf, que contienen bucles de memcpy que son cortos pero no están en el orden de los microsegundos. `MTX_DEF` es la opción correcta.

En un driver que *sí* se ejecuta en contexto de interrupción (el capítulo 14 lo tratará en detalle), los spin mutexes aparecen habitualmente en las secciones críticas más cortas: las que se encuentran entre el manejador de interrupciones y el código del top-half. Las secciones críticas más largas pueden protegerse con un mutex `MTX_DEF` que el manejador de interrupciones no adquiere; el manejador simplemente encola trabajo para que el top-half lo ejecute bajo su sleep mutex.

### El flag MTX_RECURSE y la recursión de locks

Un flag sutil de `mtx_init` es `MTX_RECURSE`. Sin él, un thread que ya posee un mutex e intenta adquirirlo de nuevo provocará un panic (con `INVARIANTS` activado) o un deadlock (sin `INVARIANTS`). Con `MTX_RECURSE`, la segunda adquisición se contabiliza; el mutex solo se libera cuando cada adquisición ha encontrado su correspondiente desbloqueo.

La mayoría de los drivers no necesitan `MTX_RECURSE`. El hecho de que una función intente adquirir un lock que ya posee suele indicar que el código tiene una estructura deficiente: un helper se está llamando desde contextos en los que se posee el lock y desde contextos en los que no, y el helper no sabe en cuál de ellos se encuentra.

Corrige la estructura, no el mutex. Divide el helper en una versión "con lock" y otra "sin lock". Nómbralos `_locked` y `_unlocked` respectivamente, siguiendo la convención de FreeBSD. Ejemplo:

```c
static size_t
myfirst_buf_read_locked(struct myfirst_softc *sc, void *dst, size_t n)
{
        mtx_assert(&sc->mtx, MA_OWNED);
        /* ... buffer logic ... */
}

static size_t
myfirst_buf_read(struct myfirst_softc *sc, void *dst, size_t n)
{
        size_t got;

        mtx_lock(&sc->mtx);
        got = myfirst_buf_read_locked(sc, dst, n);
        mtx_unlock(&sc->mtx);
        return (got);
}
```

Ahora los dos puntos de llamada son explícitos: uno toma el lock y el otro no. Ninguno necesita `MTX_RECURSE`. Ninguno confunde al lector. Así es como debes estructurar el código a medida que tu driver crece.

Existen excepciones poco frecuentes en las que `MTX_RECURSE` resulta legítimamente útil. Una estructura de datos compleja con recursión interna (por ejemplo, un árbol que utiliza el mismo lock en cada nodo) puede necesitarlo. La operación de drenado de `buf_ring` lo utiliza. Son casos especializados; para el driver ordinario, estructura tu código para evitar la recursión y no añadas el flag.

### Spinning adaptativo

Los sleep mutexes de FreeBSD incluyen una optimización llamada **spinning adaptativo**. Cuando `mtx_lock` no puede adquirir un mutex `MTX_DEF` porque otro CPU lo posee, el lock no pone al thread a dormir de inmediato. Primero realiza un spinning durante un breve intervalo, con la esperanza de que el poseedor libere el mutex rápidamente. Solo si el spinning supera un umbral, el lock cae hacia la ruta de sleep.

La razón es que la mayoría de los intervalos en los que se posee un mutex son cortos (microsegundos), y dormir más despertar es costoso. Hacer spinning durante unos pocos microsegundos suele ser preferible a pasar por el planificador. El spinning adaptativo recupera la mayor parte del beneficio de rendimiento de los spin mutexes sin sus restricciones.

Puedes ver la implementación en `/usr/src/sys/kern/kern_mutex.c`, en la función `__mtx_lock_sleep`. El código comprueba si el poseedor está ejecutándose actualmente en otro CPU y realiza spinning mientras sea así. Si el poseedor se ha desplanificado (se ha dormido él mismo), el spinning carece de sentido y el thread que espera también se duerme.

Para quien escribe drivers, el spinning adaptativo significa que el rendimiento del mutex es mejor de lo que un análisis superficial sugeriría. Una sección crítica corta sobre un mutex con poca contención cuesta aproximadamente el compare-and-swap atómico, nada más. Solo bajo contención real se paga el coste completo de dormir y despertar.

### Una mirada más detallada a la atomicidad de mtx_sleep

El capítulo 10 explicó que `mtx_sleep` abandona el mutex y coloca al llamador en la cola de sleep de forma atómica. La atomicidad importa porque la alternativa, abandonar primero y luego dormir, tiene una ventana durante la cual un `wakeup` puede escapar sin ser escuchado.

Considera la secuencia alternativa:

```c
mtx_unlock(&sc->mtx);
sleep_on(&sc->cb);
mtx_lock(&sc->mtx);
```

Entre el desbloqueo y el sleep, otro CPU podría adquirir el mutex, observar que la condición que esperábamos se ha cumplido, llamar a `wakeup(&sc->cb)` y retornar. Nuestro wakeup se ha entregado a una cola de sleep a la que todavía no nos hemos unido. Dormimos sobre una condición que, desde la perspectiva de nuestro thread, nunca volverá a ser verdadera: la señal ya se ha perdido.

Esta es la clásica condición de carrera del **wakeup perdido**. Es una de las razones fundamentales por las que existe la operación atómica de abandonar-y-dormir. `mtx_sleep` cierra la ventana al encolar al llamador en la cola de sleep *antes* de abandonar el mutex externo. La sección "Referencia: una mirada más detallada a las colas de sleep", más adelante en este capítulo, recorre la secuencia de locks y transiciones de estado con más detalle para los lectores que quieran ver exactamente cómo el kernel organiza esto. Para el cuerpo del capítulo, la regla es suficiente: usa `mtx_sleep` con el mutex externo como interlock, y la condición de carrera del wakeup perdido desaparece.

### Un recorrido guiado por kern_mutex.c

Si tienes una hora de paciencia, abrir `/usr/src/sys/kern/kern_mutex.c` merece la pena. No necesitas entender cada línea. Tres funciones son especialmente esclarecedoras:

**`__mtx_lock_flags`**: la ruta rápida para adquirir un mutex. Lo interesante es lo breve que es. En el caso sin contención, adquirir un mutex es esencialmente un compare-and-swap atómico más cierta contabilidad de perfilado de locks. Eso es todo.

**`__mtx_lock_sleep`**: la ruta lenta, a la que solo se llega cuando el compare-and-swap de la ruta rápida falla. Aquí es donde ocurren el spinning adaptativo, la propagación de prioridades y el trabajo real con la cola de sleep. El código es elaborado, pero la estructura es: intentar algunos spins, ceder a la cola de sleep, volver a entrar en el planificador y, finalmente, adquirir.

**`__mtx_unlock_flags`** y **`__mtx_unlock_sleep`**: las rutas de liberación. También son mayoritariamente rápidas: liberación atómica y, si existen threads en espera, despertar uno.

No se espera que leas cada línea. Se espera que puedas decir: "aquí es donde vive realmente el atómico que uso, y esto es lo que hace". Veinte minutos de lectura superficial son suficientes para eso.

### Comparación entre mutex, semáforo, monitor y flag binario

Para quienes ya conocen otras primitivas de sincronización, conviene situar el mutex de FreeBSD en contexto.

Un **semáforo** es un contador. Los threads lo "P" (decrementan) y "V" (incrementan); una P que lo llevaría a negativo bloquea. Un semáforo binario (con valores 0 o 1) es similar a un mutex, pero los semáforos suelen permitir que la "liberación" la realice un thread distinto al que adquirió. Los mutexes exigen que sea el mismo thread que adquirió quien libere.

Un **monitor** es una construcción a nivel de lenguaje que combina un mutex y una o más variables de condición, con el mutex adquiriéndose automáticamente a la entrada y liberándose a la salida. C no tiene monitores como característica del lenguaje, pero el patrón "mutex + variable de condición" es la misma idea.

Un **flag binario** (un `volatile int` que los threads activan y desactivan) es lo que los programadores inexpertos utilizan a veces para implementar la exclusión mutua. No funciona: dos threads pueden ver el flag como cero y ambos ponerlo a uno, procediendo los dos como si tuvieran exclusividad. Esta es la condición de carrera que vimos con el contador en la sección 2. Los mutexes reales utilizan compare-and-swap atómico, no flags desnudos.

El mutex de FreeBSD es un mutex tradicional: solo el thread que adquirió puede liberar, la adquisición recursiva requiere habilitación explícita, la primitiva se integra con el planificador para el bloqueo, y la propagación de prioridades está incorporada. Es la herramienta más sencilla y habitualmente más apropiada para la sincronización en drivers.

### Reglas de ciclo de vida del mutex

Todo mutex tiene un ciclo de vida. Las reglas son:

1. Llama a `mtx_init` exactamente una vez antes de cualquier uso.
2. Adquiere y libera el mutex tantas veces como sea necesario.
3. Llama a `mtx_destroy` exactamente una vez tras el último uso. Llamar a `mtx_destroy` mientras algún thread está bloqueado en el mutex produce un comportamiento indefinido.
4. Tras `mtx_destroy`, la memoria puede reutilizarse. No vuelvas a acceder al mutex.

La tercera regla explica por qué nuestra ruta de detach es cuidadosa con el orden: primero se destruye el cdev (lo que impide que nuevos handlers se inicien y espera a que los que están en ejecución terminen), y luego se destruye el mutex. Si destruyéramos el mutex primero, los handlers en ejecución podrían seguir dentro de `mtx_lock` o `mtx_unlock` sobre un mutex ya destruido.

Los errores de ciclo de vida con mutexes tienden a ser catastróficos (corrupción de memoria, panics por use-after-free). Son más fáciles de evitar que de depurar, así que se aplica el consejo habitual: piensa siempre con cuidado en el orden de desmontaje.

### La opción MTX_NEW

El último argumento de `mtx_init` puede incluir `MTX_NEW`, que le dice a la función "esta memoria es nueva; no es necesario comprobar si hubo una inicialización previa". En un kernel con `INVARIANTS`, `mtx_init` verifica que el mutex no se haya inicializado previamente sin un `mtx_destroy` correspondiente. `MTX_NEW` omite esta comprobación.

Usa `MTX_NEW` cuando estés inicializando un mutex en memoria que sabes que no se ha utilizado para este propósito antes. Usa el valor predeterminado (sin `MTX_NEW`) cuando el mutex pueda reinicializarse (por ejemplo, en un handler que realiza attach y detach repetidamente sobre el mismo softc). En nuestro driver, el softc se reasigna en cada attach, por lo que la memoria del mutex siempre es nueva; `MTX_NEW` es inocuo pero no necesario.

### Locks anidados y patrones de deadlock

Un thread que posee el lock A y quiere adquirir el lock B está realizando una **adquisición de locks anidada**. Las adquisiciones anidadas son donde viven los deadlocks y las violaciones de orden de locks. Cuatro patrones cubren casi todos ellos.

**Patrón 1: deadlock simple con dos locks.** El thread 1 posee A y quiere B. El thread 2 posee B y quiere A. Ninguno avanza. Solución: establecer un orden global de locks para que todos los threads adquieran A antes que B.

**Patrón 2: orden de locks entre subsistemas.** El subsistema X siempre adquiere su lock primero; el subsistema Y también adquiere el suyo primero. Si alguna vez necesitan los locks del otro, el orden depende de cuál de los dos llame al otro. Solución: documentar un orden de subsistemas que sea consistente en todas las rutas entre subsistemas.

**Patrón 3: inversión de lock a través de un callback.** Una función se llama con el lock A adquirido. Dentro de ella, llama a un callback que intenta adquirir A de nuevo. Si A no es recursivo, deadlock. Solución: liberar A antes del callback, o dividir la función para que el callback se invoque fuera del lock.

**Patrón 4: deadlock por dormir con lock adquirido.** Una función posee un mutex y luego llama a algo que duerme. Si quien duerme necesita el mismo mutex (quizás a través de una ruta de código diferente), deadlock. Solución: liberar el mutex antes de dormir, o usar `mtx_sleep` para que el abandono ocurra de forma atómica.

Para el driver `myfirst`, tenemos un único lock y no hay adquisiciones anidadas; ninguno de los patrones se aplica. El capítulo 12 introduce los locks `sx`; en cuanto tengamos una segunda clase de lock, los patrones 1 y 3 pasan a ser relevantes, y `WITNESS` se vuelve crítico.

### Cerrando la sección 5

Un mutex es una herramienta para serializar el acceso al estado compartido. FreeBSD ofrece dos variantes (`MTX_DEF` para sleep mutexes y `MTX_SPIN` para spin mutexes), y `MTX_DEF` es la elección correcta para nuestro driver. La API es pequeña: `mtx_init`, `mtx_lock`, `mtx_unlock`, `mtx_destroy`, `mtx_assert`, `mtx_sleep`. Las reglas son precisas: mantenerlo brevemente, no dormir mientras se posee un lock que no admite sleep, adquirir los locks en un orden consistente, liberar lo que se adquiere, y usar `WITNESS` para comprobar.

La sección 6 toma la teoría que hemos construido y la pone en práctica: cómo escribir programas en espacio de usuario que realmente estresen el driver, y cómo observar los resultados.



## Sección 6: pruebas de acceso multithreaded

Construir un driver correcto es la mitad del trabajo. La otra mitad consiste en desarrollar pruebas que puedan detectar los errores que se escapan en la revisión del diseño. El capítulo 10 presentó `producer_consumer.c`, una prueba de ida y vuelta con dos procesos que es la mejor prueba individual del kit de este libro para detectar problemas de corrección a nivel de buffer. El capítulo 11 toma esa base y añade las herramientas que necesitas específicamente para la concurrencia: pruebas multithreaded con `pthread(3)`, pruebas multiproceso con `fork(2)`, y un arnés de carga que puede mantener presión sobre el driver durante el tiempo suficiente para exponer errores de temporización poco frecuentes.

El objetivo no es probar exhaustivamente todos los entrelazados posibles. Eso es imposible. El objetivo es aumentar la tasa a la que el planificador visita entrelazados que el driver no ha visto, de modo que, si existe un error, sea probable que observemos sus efectos.

### La escalera de pruebas

Las pruebas de concurrencia escalan por una escalera. En la base está la prueba de un único thread: un proceso, un thread, una llamada a la vez. Eso es lo que hacen `cat` y `echo`. En lo alto está una prueba distribuida, multiproceso, multithreaded, con tiempos variables y de larga duración. Cuanto más alto escalas, más probable es que detectes errores de concurrencia; pero también más caro resulta preparar e interpretar cada prueba.

Los peldaños, de abajo a arriba:

1. **Pruebas de humo con un solo thread.** Un `cat`, un `echo`. Útiles para confirmar que el driver está vivo; inútiles para la concurrencia.
2. **Recorrido de ida y vuelta con dos procesos.** `producer_consumer` del Capítulo 10. Un escritor, un lector, verificación del contenido. Detecta la mayoría de los problemas con un solo lock.
3. **Múltiples threads dentro de un proceso.** Pruebas basadas en `pthread` en las que varios threads del mismo proceso acceden al mismo descriptor o a descriptores distintos. Pone de manifiesto la concurrencia intra-proceso.
4. **Estrés con múltiples procesos.** Pruebas basadas en `fork` con N productores y M consumidores. Expone la concurrencia entre procesos y la contención de locks.
5. **Estrés de larga duración.** Cualquiera de los anteriores, ejecutado durante horas. Expone los fallos sensibles al tiempo que son raros operación a operación, pero inevitables si se les da suficiente tiempo.

En esta sección construiremos los peldaños 3, 4 y 5. El peldaño 2 ya existe desde el Capítulo 10.

### Múltiples threads en un mismo proceso

Un proceso con múltiples threads es útil porque los threads comparten los descriptores de archivo. Dos threads pueden leer desde el mismo descriptor; ambas llamadas entran en `myfirst_read` con el mismo `dev` y, lo que es importante, con el mismo `fh` por descriptor.

Aquí tienes un lector multithreaded mínimo:

```c
/* mt_reader.c: multiple threads reading from one descriptor. */
#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH         "/dev/myfirst"
#define NTHREADS        4
#define BYTES_PER_THR   (256 * 1024)
#define BLOCK           4096

static int      g_fd;
static uint64_t total[NTHREADS];
static uint32_t sum[NTHREADS];

static uint32_t
checksum(const char *p, size_t n)
{
        uint32_t s = 0;
        for (size_t i = 0; i < n; i++)
                s = s * 31u + (uint8_t)p[i];
        return (s);
}

static void *
reader(void *arg)
{
        int tid = *(int *)arg;
        char buf[BLOCK];
        uint64_t got = 0;
        uint32_t sm = 0;

        while (got < BYTES_PER_THR) {
                ssize_t n = read(g_fd, buf, sizeof(buf));
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        warn("thread %d: read", tid);
                        break;
                }
                if (n == 0)
                        break;
                sm += checksum(buf, n);
                got += n;
        }
        total[tid] = got;
        sum[tid] = sm;
        return (NULL);
}

int
main(void)
{
        pthread_t tids[NTHREADS];
        int ids[NTHREADS];

        g_fd = open(DEVPATH, O_RDONLY);
        if (g_fd < 0)
                err(1, "open %s", DEVPATH);

        for (int i = 0; i < NTHREADS; i++) {
                ids[i] = i;
                if (pthread_create(&tids[i], NULL, reader, &ids[i]) != 0)
                        err(1, "pthread_create");
        }
        for (int i = 0; i < NTHREADS; i++)
                pthread_join(tids[i], NULL);

        uint64_t grand = 0;
        for (int i = 0; i < NTHREADS; i++) {
                printf("thread %d: %" PRIu64 " bytes, checksum 0x%08x\n",
                    i, total[i], sum[i]);
                grand += total[i];
        }
        printf("grand total: %" PRIu64 "\n", grand);

        close(g_fd);
        return (0);
}
```

El archivo compañero en `examples/part-03/ch11-concurrency/userland/mt_reader.c` es idéntico a este listado; puedes teclearlo desde el libro o copiarlo del árbol de ejemplos.

Compila con:

```sh
$ cc -Wall -Wextra -pthread -o mt_reader mt_reader.c
```

Arranca un escritor en otro terminal (o bifurca uno antes de crear los threads) y ejecuta este verificador. Cada thread extrae bytes del driver. Como el driver tiene un mutex, las lecturas se serializan; no hay beneficio de concurrencia, pero tampoco hay incorrección. Cada thread ve un subconjunto del flujo, y la concatenación de los bytes de todos los threads es el flujo completo.

Esta es una propiedad importante. El driver no garantiza qué thread ve qué bytes; solo garantiza que el total se conserva. Si necesitas flujos por lector, necesitas un diseño de driver diferente (el ejercicio de desafío 3 del Capítulo 10 exploró esto). Por ahora, el test confirma que múltiples lectores en un mismo proceso se comportan correctamente.

### Muchos procesos en paralelo

Un test basado en `fork` lanza N procesos hijos, cada uno haciendo su propia operación contra el dispositivo. Esto nos da procesos independientes, descriptores de archivo independientes y decisiones de planificación independientes. Es más probable que el kernel los intercale de formas novedosas.

Aquí tienes el esqueleto:

```c
/* mp_stress.c: N processes hammering the driver concurrently. */
#include <sys/types.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH         "/dev/myfirst"
#define NWRITERS        2
#define NREADERS        2
#define SECONDS         30

static volatile sig_atomic_t stop;

static void
sigalrm(int s __unused)
{
        stop = 1;
}

static int
child_writer(int id)
{
        int fd;
        char buf[1024];
        unsigned long long written = 0;

        fd = open(DEVPATH, O_WRONLY);
        if (fd < 0)
                err(1, "writer %d: open", id);
        memset(buf, 'a' + id, sizeof(buf));

        while (!stop) {
                ssize_t n = write(fd, buf, sizeof(buf));
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        break;
                }
                written += n;
        }
        close(fd);
        printf("writer %d: %llu bytes\n", id, written);
        return (0);
}

static int
child_reader(int id)
{
        int fd;
        char buf[1024];
        unsigned long long got = 0;

        fd = open(DEVPATH, O_RDONLY);
        if (fd < 0)
                err(1, "reader %d: open", id);

        while (!stop) {
                ssize_t n = read(fd, buf, sizeof(buf));
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        break;
                }
                got += n;
        }
        close(fd);
        printf("reader %d: %llu bytes\n", id, got);
        return (0);
}

int
main(void)
{
        pid_t pids[NWRITERS + NREADERS];
        int n = 0;

        signal(SIGALRM, sigalrm);

        for (int i = 0; i < NWRITERS; i++) {
                pid_t pid = fork();
                if (pid < 0)
                        err(1, "fork");
                if (pid == 0) {
                        signal(SIGALRM, sigalrm);
                        alarm(SECONDS);
                        _exit(child_writer(i));
                }
                pids[n++] = pid;
        }
        for (int i = 0; i < NREADERS; i++) {
                pid_t pid = fork();
                if (pid < 0)
                        err(1, "fork");
                if (pid == 0) {
                        signal(SIGALRM, sigalrm);
                        alarm(SECONDS);
                        _exit(child_reader(i));
                }
                pids[n++] = pid;
        }

        for (int i = 0; i < n; i++) {
                int status;
                waitpid(pids[i], &status, 0);
        }
        return (0);
}
```

El código fuente compañero en `examples/part-03/ch11-concurrency/userland/mp_stress.c` coincide con este listado. El `signal(SIGALRM, sigalrm)` que se reinstala después de `fork(2)` es intencional; `fork(2)` hereda la disposición de señales del padre, pero reinstalarlo deja la intención explícita y sobrevive al caso (infrecuente) de que el manejador del padre hubiese cambiado en el ínterin.

Compila y ejecuta:

```sh
$ cc -Wall -Wextra -o mp_stress mp_stress.c
$ ./mp_stress
writer 0: 47382528 bytes
writer 1: 48242688 bytes
reader 0: 47669248 bytes
reader 1: 47956992 bytes
```

Observa los totales. La suma de los lectores debería ser igual a la suma de los escritores (más o menos lo que quede en el buffer al final de la ventana de test). Si no coinciden, hay un bug en el driver o un bug en el informe; ninguno de los dos es aceptable.

Este test, ejecutado durante treinta segundos, produce aproximadamente cien millones de bytes de tráfico a través del driver con cuatro procesos concurrentes. En una máquina de cuatro núcleos, los cuatro pueden estar avanzando en cualquier instante. Si hay un bug de concurrencia que nuestra revisión pasó por alto, tiene treinta segundos de cómputo real para manifestarse.

### Tests de larga duración

Para los bugs más esquivos, ejecuta los tests durante horas. Un envoltorio sencillo:

```sh
$ for i in $(seq 1 100); do
      echo "iteration $i" >> /tmp/mp_stress.log
      ./mp_stress >> /tmp/mp_stress.log 2>&1
      sleep 1
  done
```

Tras cincuenta iteraciones (aproximadamente veinticinco minutos de estrés acumulado sobre el driver), revisa el log. Si los recuentos de bytes de cada iteración son internamente consistentes, tienes una señal sólida de que el driver no está sufriendo bugs de concurrencia frecuentes. Si alguna iteración muestra inconsistencia, hay un bug; guarda el log, reprodúcelo con valores menores de `NWRITERS`/`NREADERS` e investiga.

Un script como el anterior es un sustituto barato de una pipeline de integración continua adecuada. Para trabajo serio con drivers, una CI que ejecute la suite de estrés cada noche sobre múltiples configuraciones de hardware es el estándar de referencia. Para aprender, el script anterior es suficiente.

### Observar sin perturbar

Un problema sutil: añadir logging a un test concurrente puede en sí mismo alterar los tiempos y enmascarar el bug. Si el test imprime una línea por cada operación, la impresión se convierte en un cuello de botella, los threads se serializan alrededor de stdout y los entrelazados interesantes dejan de producirse.

Técnicas que reducen el efecto observador:

- **Almacena los logs en memoria y vuelca al final.** Cada thread añade a su propio array local; `main` imprime los arrays cuando todos los threads han terminado.
- **Usa `ktrace(1)` en lugar de impresión dentro del proceso.** `ktrace` captura las syscalls de un proceso en ejecución sin modificarlo; el volcado puede analizarse después.
- **Usa sondas de `dtrace(1)`.** `dtrace` está diseñado para tener un impacto mínimo sobre el camino de código observado.
- **Mantén contadores, no logs línea a línea.** Un recuento de discrepancias es un entero único; comprime mucha información en algo barato de actualizar.

`producer_consumer` del Capítulo 10 usa el enfoque de contadores: actualiza un checksum por bloque y un recuento total de bytes, y reporta ambos al final. El test es prácticamente invisible para los tiempos del driver.

### Un flujo de trabajo de testing

Reuniéndolo todo, aquí tienes un flujo de trabajo razonable para un nuevo cambio en el driver:

1. **Smoke test.** Carga el driver, `printf 'hello' > /dev/myfirst`, `cat /dev/myfirst`. Confirma la operación básica.
2. **Test de ida y vuelta.** Ejecuta `producer_consumer`. Confirma cero discrepancias.
3. **Multithreaded en un solo proceso.** Ejecuta `mt_reader` contra un `cat /dev/zero > /dev/myfirst` en ejecución continua. Confirma que el total coincide.
4. **Estrés multiproceso.** Ejecuta `mp_stress` durante treinta segundos. Confirma que los recuentos de bytes son consistentes.
5. **Estrés de larga duración.** Ejecuta el envoltorio de bucle durante treinta minutos a una hora.
6. **Regresión con kernel de depuración.** Repite los pasos 1-4 en un kernel con `WITNESS` activado. Confirma que no hay advertencias.

Si cada paso pasa, es probable que el cambio sea seguro. Si algún paso falla, el modo de fallo te indica dónde buscar.

### Un verificador de latencia

A veces lo que quieres saber no es si el driver funciona, sino con qué rapidez responde bajo carga. El coste del mutex, la latencia de despertar de la cola de sleep y las decisiones del planificador se combinan para producir una distribución de tiempos de respuesta que vale la pena observar directamente.

Aquí tienes un verificador de latencia sencillo. Abre el dispositivo, mide cuánto tarda cada `read(2)` y muestra un histograma.

```c
/* lat_tester.c: measure read latency against /dev/myfirst. */
#include <sys/types.h>
#include <sys/time.h>
#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH "/dev/myfirst"
#define NSAMPLES 10000
#define BLOCK 1024

static uint64_t
nanos(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ((uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec);
}

int
main(void)
{
        int fd = open(DEVPATH, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
                err(1, "open");
        char buf[BLOCK];
        uint64_t samples[NSAMPLES];
        int nvalid = 0;

        for (int i = 0; i < NSAMPLES; i++) {
                uint64_t t0 = nanos();
                ssize_t n = read(fd, buf, sizeof(buf));
                uint64_t t1 = nanos();
                if (n > 0)
                        samples[nvalid++] = t1 - t0;
                else
                        usleep(100);
        }
        close(fd);

        /* Simple bucketed histogram. */
        uint64_t buckets[10] = {0};
        const char *labels[10] = {
                "<1us   ", "<10us  ", "<100us ", "<1ms   ",
                "<10ms  ", "<100ms ", "<1s    ", ">=1s   ",
                "", ""
        };
        for (int i = 0; i < nvalid; i++) {
                uint64_t us = samples[i];
                int b = 0;
                if (us < 1000) b = 0;
                else if (us < 10000) b = 1;
                else if (us < 100000) b = 2;
                else if (us < 1000000) b = 3;
                else if (us < 10000000) b = 4;
                else if (us < 100000000) b = 5;
                else if (us < 1000000000) b = 6;
                else b = 7;
                buckets[b]++;
        }

        printf("Latency histogram (%d samples):\n", nvalid);
        for (int i = 0; i < 8; i++)
                printf("  %s %6llu\n",
                    labels[i], (unsigned long long)buckets[i]);
        return (0);
}
```

Ejecuta con un escritor concurrente:

```sh
$ dd if=/dev/zero of=/dev/myfirst bs=1k &
$ ./lat_tester
Latency histogram (10000 samples):
  <1us       3421
  <10us      6124
  <100us      423
  <1ms         28
  <10ms         4
  <100ms        0
  <1s           0
  >=1s          0
```

La mayoría de las lecturas se completan en un microsegundo o dos. Unas pocas tardan más, normalmente porque el lector tuvo que esperar al mutex mientras el escritor estaba dentro. Muy ocasionalmente, una lectura tarda milisegundos; eso suele deberse a que el thread fue interrumpido por el planificador o tuvo que dormir.

Esta es la distribución de la capacidad de respuesta de tu driver bajo carga. Si la cola larga es inaceptablemente larga, el driver necesita un locking más granular o menos operaciones en la sección crítica. Para nuestro `myfirst`, la cola es lo suficientemente corta como para que no sea necesario cambiar nada.

### Un verificador que sondea lecturas rotas

Si tu driver tiene lecturas de múltiples campos sin protección (la recomendación del libro es que nunca ocurra, pero los drivers reales a veces las tienen), puedes detectar lecturas rotas con un verificador específico.

Imaginemos que tuviésemos un campo que podría romperse en una plataforma de 32 bits. El verificador:

1. Bifurcaría un escritor que actualiza continuamente el campo con patrones conocidos.
2. Bifurcaría un lector que lee continuamente el campo y comprueba el patrón.
3. Reportaría cada lectura que no coincida con ningún patrón válido.

Para nuestro driver, ese campo no existe (todos los contadores `uint64_t` son por CPU, todo el estado compuesto está bajo el mutex). Pero construir el verificador es un ejercicio valioso, porque te entrena para escribir sondas específicas para clases concretas de bug.

### Escalado multinúcleo

Un test que todavía no hemos escrito: ¿escala el driver a medida que añades más núcleos?

La idea es sencilla: ejecuta `mp_stress` con valores crecientes de `NWRITERS`/`NREADERS` y observa el throughput total. Idealmente, el throughput debería crecer de forma lineal con el número de núcleos hasta algún punto de saturación. En la práctica, los drivers de mutex único se saturan pronto, porque cada operación se serializa en ese único mutex.

Construye un test de escalado:

```sh
$ for n in 1 2 4 8 16; do
    NWRITERS=$n NREADERS=$n ./mp_stress > /tmp/scale_$n.txt
    total=$(grep ^writer /tmp/scale_$n.txt | awk '{s+=$3} END {print s}')
    echo "$n writers, $n readers: $total bytes"
  done
```

Para que esto funcione, el programa `mp_stress` necesita aceptar `NWRITERS` y `NREADERS` desde variables de entorno; queda como ejercicio para el lector.

En una máquina de cuatro núcleos, un driver de mutex único suele saturarse alrededor de 2-4 escritores más 2-4 lectores. A partir de ahí, el throughput se estabiliza o cae, porque el mutex se convierte en el cuello de botella. Esta es la señal de que un locking más granular (los locks `sx` del Capítulo 12, por ejemplo) podría estar justificado. Para `myfirst` en esta etapa, la saturación no es una preocupación; la pedagogía de enseñanza importa más que el throughput absoluto.

### Cobertura de los modos de fallo

Una comprobación final: ¿cubre el kit de tests todos los modos de fallo que enumeramos en la Sección 1?

- **Corrupción de datos**: `producer_consumer` lo detecta directamente comparando checksums.
- **Actualizaciones perdidas**: si un contador es incorrecto, el total de producer_consumer no coincidirá.
- **Valores rotos**: no se prueba directamente, pero cualquier lectura rota que afecte a la corrección se propagará a un checksum incorrecto.
- **Estado compuesto inconsistente**: si los índices del cbuf se vuelven inconsistentes, el driver corrupcioará los datos (detectado por el checksum) o entrará en deadlock (detectado porque el test se cuelga y el timeout lo captura).

El kit de tests es exhaustivo frente a los modos de fallo que conocemos. No es exhaustivo frente a bugs desconocidos, pero ningún kit de tests puede serlo. La combinación de ejecuciones en kernel de depuración, tests de regresión y tests de estrés de larga duración es lo más cerca que podemos llegar.

### Cerrando la Sección 6

El testing es la parte del trabajo con drivers que ofrece retroalimentación más rápida. Los tests que acabamos de construir cubren tres órdenes de magnitud más entrelazados que un simple `cat`. Ejecutarlos tanto en un kernel de producción como en uno de depuración te da verificación desde ambos ángulos.

La Sección 7 da el siguiente paso: ¿qué haces cuando un test falla?



## Sección 7: Depuración y verificación de la seguridad ante threads

La firma más fiable de un bug de concurrencia es que no se reproduce a voluntad. Ejecutas el test; falla. Lo ejecutas de nuevo; pasa. Lo ejecutas cien veces; falla dos, en líneas diferentes. Esta sección trata sobre las herramientas y técnicas que te permiten acorralar dichos bugs a pesar de su falta de fiabilidad.

### Síntomas típicos

Antes de llegar a las herramientas, cataloguemos los síntomas. Cada síntoma sugiere una clase específica de bug.

**Síntoma: el test reporta intermitentemente datos corruptos, pero el driver no entra en pánico.** Esto suele ser un lock omitido: un campo al que se accede sin sincronización desde más de un flujo. La corrupción es el resultado de la condición de carrera; el driver sobrevive porque el valor corrupto no provoca inmediatamente un fallo derivable.

**Síntoma: el driver entra en pánico con un backtrace dentro de `mtx_lock` o `mtx_unlock`.** Esto suele ser un uso después de liberación (use-after-free) del propio mutex. La causa más común es que se llamó a `mtx_destroy` mientras otro thread todavía usaba el mutex. La solución es revisar el camino de detach: el mutex debe sobrevivir a todos sus usos.

**Síntoma: `WITNESS` reporta una inversión del orden de adquisición de locks.** Un thread adquirió los locks en un orden inconsistente con un orden observado anteriormente. La solución es definir un orden global de locks y hacer que cada camino de código lo respete.

**Síntoma: `KASSERT` se dispara dentro de `mtx_assert(MA_OWNED)`.** Una función auxiliar esperaba que el mutex estuviese tomado y no lo estaba. La solución es encontrar el punto de llamada de la función auxiliar y añadir el `mtx_lock` que falta.

**Síntoma: el test se cuelga para siempre.** Un thread está durmiendo en un canal y nadie llama nunca a `wakeup` en ese canal. La solución suele ser un `wakeup(&sc->cb)` (o equivalente) que falta en el camino de I/O. La Sección 5 del Capítulo 10 enumeró las reglas.

**Síntoma: el driver tiene un rendimiento muy bajo bajo carga.** El mutex está más disputado de lo necesario. La solución suele ser acortar las secciones críticas (mover más trabajo fuera del lock) o dividir el lock en piezas más finas (material del Capítulo 12).

**Síntoma: el driver falla semanas después de su despliegue, con un backtrace que no involucra obviamente a la concurrencia.** Este es el peor escenario y casi siempre es un bug de concurrencia cuyos efectos finalmente se han acumulado. La solución es auditar como en la Sección 3 y ejecutar tests de estrés como en la Sección 6.

### INVARIANTS y KASSERT

`INVARIANTS` es una opción de compilación del kernel que activa aserciones en todo el kernel. Cuando `INVARIANTS` está activado, `KASSERT(cond, args)` evalúa `cond` y provoca un pánico con `args` si la condición es falsa. Cuando `INVARIANTS` no está activado, `KASSERT` se compila a nada.

Esto hace que `KASSERT` sea prácticamente gratuito en producción y enormemente valioso durante el desarrollo. Cada invariante del que dependa tu código debería ser un `KASSERT`. Por ejemplo:

```c
KASSERT(sc->cb.cb_used <= sc->cb.cb_size,
    ("cbuf used exceeds size: %zu > %zu",
    sc->cb.cb_used, sc->cb.cb_size));
```

Esto dice: «Creo que `cb_used` nunca supera `cb_size`. Si el código que garantiza esta verdad alguna vez falla, quiero saberlo de inmediato, no horas después cuando algo más empiece a fallar».

Añade llamadas a `KASSERT` con generosidad en tu driver. Cada precondición, cada invariante, cada rama de «esto no puede ocurrir». El coste en producción es cero; el beneficio es que los bugs durante el desarrollo se detectan en el momento en que aparecen, no más adelante.

Para el driver `myfirst`, algunas adiciones útiles:

```c
/* In cbuf.c, at the top of cbuf_write: */
KASSERT(cb->cb_used <= cb->cb_size,
    ("cbuf_write: cb_used %zu exceeds cb_size %zu",
    cb->cb_used, cb->cb_size));
KASSERT(cb->cb_head < cb->cb_size,
    ("cbuf_write: cb_head %zu not less than cb_size %zu",
    cb->cb_head, cb->cb_size));

/* In myfirst_buf_read and myfirst_buf_write: */
mtx_assert(&sc->mtx, MA_OWNED);
```

Cada una de estas comprobaciones es una pequeña apuesta que atrapa un error futuro.

### WITNESS en acción

Cuando `WITNESS` emite una advertencia, la salida es detallada. Una advertencia típica por una inversión de orden de locks tiene este aspecto:

```text
lock order reversal:
 1st 0xfffffe000123a000 foo_lock (foo, sleep mutex) @ /usr/src/sys/dev/foo/foo.c:100
 2nd 0xfffffe000123a080 bar_lock (bar, sleep mutex) @ /usr/src/sys/dev/bar/bar.c:200
lock order reversal detected for lock group "bar" -> "foo"
stack backtrace:
...
```

Los elementos clave son:

- Los dos locks involucrados, con sus nombres, tipos y las ubicaciones en el código fuente donde fueron adquiridos.
- El orden conflictivo (bar -> foo), que es el inverso de un orden establecido previamente (foo -> bar).
- Un backtrace de la pila que muestra el camino que condujo a la inversión.

La solución es una de dos cosas: cambiar uno de los puntos de adquisición para que coincida con el orden existente, o reconocer que los dos caminos de código no deberían mantener ambos locks a la vez. El backtrace te indica qué camino de código está involucrado; leer el código fuente te dice cuál es el cambio correcto.

Para el driver `myfirst`, que tiene un solo lock, `WITNESS` no puede reportar una inversión de orden de locks. Las únicas advertencias de `WITNESS` que podríamos ver están relacionadas con dormir con un lock adquirido o con la adquisición recursiva. Ambos casos merecen probarse deliberadamente, como hacemos en el Laboratorio 7.3.

### Cómo leer un backtrace de kernel panic

A veces un bug produce un panic. El kernel imprime un backtrace, entra en el depurador (si `DDB` está configurado) o reinicia el sistema (si no lo está). Tu tarea es extraer la mayor cantidad de información posible del backtrace antes de que el sistema desaparezca.

Las primeras líneas de un panic suelen identificar el fallo:

```text
panic: mtx_lock() of spin mutex @ ...: recursed on non-recursive mutex myfirst0 @ ...
cpuid = 2
...
```

A partir de aquí:

- El mensaje de panic es la línea más importante. `mtx_lock of spin mutex` apunta a un tipo de bug; `sleeping with mutex held` a otro; `general protection fault` a una clase completamente distinta.
- El campo `cpuid` te indica qué CPU experimentó el panic, lo que puede ser relevante si el bug es específico de un entorno de planificación determinado.
- El backtrace de la pila muestra las funciones en el camino descendente. Léelo de arriba a abajo: la parte inferior es donde se detectó el panic; la parte superior es donde comenzó la ejecución. La función justo por encima de la función de panic suele ser la que cometió el error.

Si `DDB` está configurado, puedes interactuar con el depurador en el momento del panic: `bt` (backtrace), `show mutex <addr>` (inspeccionar un mutex), `show alllocks` (todos los locks mantenidos por todos los threads). Este es el modo ninja de la depuración de drivers; el Capítulo 12 y las referencias de depuración lo abordan.

### dtrace(1) como observador silencioso

`dtrace(1)` es el framework de rastreo dinámico de FreeBSD. Te permite asociar sondas a funciones del kernel (y a funciones del espacio de usuario, con las bibliotecas adecuadas) y recopilar datos con un impacto mínimo sobre el código observado.

Un comando `dtrace` sencillo para contar las adquisiciones de mutex sobre el mutex de `myfirst`:

```sh
# dtrace -n 'fbt::__mtx_lock_flags:entry /arg0 != 0/ { @[execname] = count(); }'
```

Ejecuta el driver bajo carga y luego interrumpe `dtrace` con Ctrl-C. Obtendrás una tabla con los recuentos de adquisición de locks por proceso. Si un proceso domina, ahí está la fuente de contención.

Otro one-liner útil: rastrear cuándo los threads entran y salen de nuestro manejador de lectura:

```sh
# dtrace -n 'fbt::myfirst_read:entry { printf("%d reading %d bytes", tid, arg1); }'
```

Los índices exactos de `arg` dependen del ABI de la función; `dtrace -l | grep myfirst` lista las sondas disponibles.

`dtrace` no es magia: utiliza mecanismos reales del kernel (habitualmente el proveedor de rastreo de límites de función) que tienen un coste no nulo. Pero ese coste es drásticamente inferior al del registro basado en `printf`, y puede activarse y desactivarse sin modificar el driver.

El Capítulo 15 cubrirá `dtrace` con más detalle. Para el Capítulo 11, la idea clave es que `dtrace` es tu aliado para observar un driver en vivo bajo carga.

### Lista de comprobación para la depuración

Cuando aparece un bug de concurrencia, recorre esta lista en orden:

1. **¿Puedes reproducirlo de forma fiable?** Si la respuesta es sí, perfecto; si no, ejecuta la prueba en bucle y determina la tasa de fallo.
2. **¿`WITNESS` reporta algo?** Arranca con `options WITNESS INVARIANTS`. Vuelve a ejecutar. Recoge cada advertencia.
3. **¿Hay fallos de `KASSERT`?** Estos se disparan como panics; el mensaje identifica el invariante.
4. **¿Es el bug determinista con suficiente carga?** Si aparece siempre con cuatro escritores y cuatro lectores, tienes una carga de trabajo que biseccionar; si aparece solo bajo condiciones específicas, empieza a aislar esas condiciones.
5. **¿Qué campo está corrompido?** Añade registro selectivo o sondas de `dtrace` alrededor de los accesos sospechosos.
6. **¿Qué sincronización falta?** Audita los caminos de acceso según las reglas de la Sección 3.
7. **¿Es la solución coherente con el resto de la estrategia de locking del driver?** Documenta el cambio en `LOCKING.md`.
8. **¿Pasa ahora la prueba de forma fiable?** Ejecuta las pruebas de estrés el doble de tiempo del habitual para confirmarlo.

La mayoría de los bugs de concurrencia ceden ante este proceso. Algunos requieren profundizar en el driver específico o en el kernel; para esos te preparan el Capítulo 12 y el material posterior.

### Un ejemplo paso a paso: diagnóstico de un wakeup ausente

Para hacer concreto el proceso de depuración, recorramos un bug hipotético. Supón que has modificado `myfirst_write` para actualizar algún estado auxiliar y que, tras el cambio, una prueba en dos terminales (un `cat` y un `echo`) se cuelga. El `cat` está dormido, el `echo` ha terminado y los bytes no salen.

Paso 1: confirmar el síntoma.

```sh
$ ps -AxH -o pid,wchan,command | grep cat
12345  myfrd  cat /dev/myfirst
```

El `cat` está durmiendo sobre `myfrd`. Ese es el nombre de nuestro canal de espera. Está esperando datos.

Paso 2: inspeccionar el estado del driver.

```sh
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 5
```

El buffer tiene 5 bytes. El lector debería poder drenar esos bytes y devolvérselos al `cat`. ¿Por qué no lo hace?

Paso 3: examinar el código. El thread del `cat` está en `mtx_sleep(&sc->cb, ..., "myfrd", ...)`. Está esperando a `wakeup(&sc->cb)`. ¿Quién lo llama?

Busca en el código fuente con grep:

```sh
$ grep -n 'wakeup(&sc->cb' myfirst.c
180:        wakeup(&sc->cb);
220:        wakeup(&sc->cb);
```

Dos puntos de llamada. Uno está en `myfirst_read` (tras una lectura correcta, para despertar a los escritores que esperan espacio). El otro está en `myfirst_write` (tras una escritura correcta, para despertar a los lectores que esperan datos).

Paso 4: inspeccionar el camino de escritura. ¿Ejecutó realmente la escritura el `wakeup`?

```c
mtx_lock(&sc->mtx);
put = myfirst_buf_write(sc, bounce, want);
/* ... update aux state ... */
mtx_unlock(&sc->mtx);

/* new code: do some bookkeeping */
update_stats(sc, put);

wakeup(&sc->cb);
selwakeup(&sc->rsel);
```

Parece correcto. El `wakeup` está ahí. El `selwakeup` también.

Paso 5: profundizar en el rastreo. Quizá el `wakeup` se llama antes de que los bytes estén realmente en el buffer. En el código original, el orden era: añadir bytes con el lock adquirido, liberar el lock, wakeup. En el código modificado, insertaste `update_stats` entre la liberación del lock y el `wakeup`. Eso debería estar bien; los bytes están en el buffer, solo esperan que alguien llame a `wakeup`.

Pero espera: `update_stats` es nuevo. ¿Qué hace?

```c
static void
update_stats(struct myfirst_softc *sc, size_t n)
{
        mtx_lock(&sc->mtx);
        sc->aux_counter++;
        mtx_unlock(&sc->mtx);
}
```

Adquiere el mutex. Y luego lo libera. Es un ciclo completo de adquisición y liberación. ¿Podría cambiar algo?

De hecho, observa con más atención. En el camino de escritura, la secuencia es:

1. Adquirir el mutex.
2. Escribir bytes en cbuf.
3. Liberar el mutex.
4. Llamar a update_stats, que vuelve a adquirirlo y a liberarlo.
5. Llamar a wakeup.

Entre los pasos 3 y 5 existe una ventana en la que un lector podría adquirir el mutex, observar los nuevos bytes y continuar. En la mayoría de los casos esto está bien: al lector no le importan las estadísticas auxiliares. Pero si el lector está actualmente dentro de `mtx_sleep`, esperando el `wakeup`, permanece bloqueado hasta el paso 5.

Eso parece normal; el paso 5 siempre se ejecuta. Así que el `wakeup` se dispara. ¿Pero por qué no despierta el lector?

Paso 6: añadir un `device_printf` al inicio de `update_stats` y en la llamada a `wakeup`:

```c
device_printf(sc->dev, "update_stats called\n");
/* ... */
device_printf(sc->dev, "wakeup(&sc->cb) called\n");
wakeup(&sc->cb);
```

Recarga, vuelve a probar. Observa `dmesg`:

```text
myfirst0: update_stats called
```

Solo un mensaje. El `wakeup` nunca se llama.

Paso 7: examinar `update_stats` de nuevo. ¿Podría tener un camino que retorne antes de tiempo?

```c
static void
update_stats(struct myfirst_softc *sc, size_t n)
{
        if (n == 0)
                return;
        mtx_lock(&sc->mtx);
        sc->aux_counter++;
        mtx_unlock(&sc->mtx);
}
```

Un cortocircuito para `n == 0`. ¿Y si `put` fuera cero por algún motivo? Entonces `update_stats` retorna. El llamante continúa, pero espera, el `wakeup` sigue estando después de `update_stats`, así que debería dispararse igualmente.

A menos que el código del llamante también tenga un cortocircuito:

```c
update_stats(sc, put);
if (put < want)
        break;
wakeup(&sc->cb);
```

Ahí está el bug. El `break` prematuro omite el `wakeup`. En la mayoría de las condiciones `put == want` y el `wakeup` se ejecuta. En la condición poco frecuente en que `put < want` (por ejemplo, el buffer se llenó durante esta iteración), el `wakeup` se omite. Un lector esperando datos nunca ve los bytes que sí llegaron.

Paso 8: corregirlo. Mover el `wakeup` antes del `break`:

```c
update_stats(sc, put);
wakeup(&sc->cb);        /* must happen even on short write */
selwakeup(&sc->rsel);
if (put < want)
        break;
```

Prueba de nuevo. El bloqueo ha desaparecido.

Esta es una historia de depuración realista. El bug no está en la maquinaria de concurrencia; está en la lógica de negocio que controla cuándo se dispara el wakeup. La solución es local. El proceso de depuración fue: observar, acotar, rastrear, inspeccionar, corregir.

### Patrones para añadir trazas de depuración

Cuando añades trazas de depuración a un driver, hay varios patrones que dan resultado.

**Trazas tras un indicador de depuración.** Lo vimos en el Capítulo 10: un entero `myfirst_debug` controlado mediante sysctl, y una macro `MYFIRST_DBG` que compila a nada cuando el indicador es cero. Con el indicador desactivado, las trazas no tienen coste; con él activado, emiten a `dmesg`. Esto permite distribuir el driver con las trazas incluidas y activarlas solo cuando sea necesario.

**Una línea por evento significativo.** La tentación es trazar cada byte transferido. Resístela. Traza una vez por invocación del manejador, no una vez por byte. Traza una vez por adquisición de lock durante la depuración de un bug específico y elimínala después. Un registro inundado de líneas no te dice nada.

**Incluye el ID del thread.** `curthread->td_tid` es el ID del thread actual. Imprimirlo en las trazas te permite distinguir actividades concurrentes en el registro. Formato útil: `device_printf(dev, "tid=%d got=%zu\n", curthread->td_tid, got)`.

**Incluye el estado antes y después.** Para un cambio de estado, registra tanto el valor antiguo como el nuevo. `device_printf(dev, "cb_used: %zu -> %zu\n", before, after)` es más útil que `cb_used: %zu`, porque puedes ver la transición.

**Elimina o protege las trazas antes de distribuir.** Las líneas de traza son herramientas de desarrollo. Elimínalas o ponlas tras `MYFIRST_DBG` antes de desplegar el código. Un driver en producción que emite salida de depuración por cada I/O está desperdiciando el buffer de registro.

### Depuración de los casos difíciles

Algunos bugs de concurrencia resisten todas las técnicas que hemos visto. Se reproducen en una máquina específica, con una carga de trabajo específica, sobre un kernel específico, y en ningún otro sitio. Cuando te encuentres con un bug así, las opciones son:

**Bisecciona la carga de trabajo.** Si el bug aparece con `X` lectores concurrentes e `Y` escritores concurrentes, intenta reducir `X` e `Y` hasta que el bug deje de reproducirse. El reproductor mínimo es el más fácil de razonar.

**Bisecciona el kernel.** Si el bug aparece en la versión N pero no en la N-1, encuentra el cambio en el kernel que lo introdujo. `git bisect` es la herramienta para ello. Es lento, pero efectivo.

**Inspecciona el hardware.** Algunos bugs están causados por características específicas de la CPU (peculiaridades de coherencia de caché, comportamiento de TSO, modelos de memoria con orden débil). Si el bug aparece en ARM64 pero no en amd64, el orden de memoria es sospechoso.

**Pregunta en las listas de correo.** La comunidad FreeBSD incluye muchas personas que han visto bugs similares. Las listas `freebsd-hackers` y `freebsd-current` agradecen informes de bugs detallados. Cuanta más información proporciones (versión exacta del kernel, carga de trabajo, modo de fallo, qué has probado), más probable es que alguien reconozca el patrón.

La depuración de concurrencia es, en última instancia, una habilidad. Las herramientas ayudan, pero la habilidad se forja haciéndolo: escribiendo pruebas que fallen, escribiendo correcciones y confirmando que las pruebas pasan. Los capítulos que vienen te darán más oportunidades.

### Cerrando la Sección 7

Depurar la concurrencia es un intercambio de paciencia por herramientas. `INVARIANTS`, `WITNESS`, `KASSERT`, `mtx_assert` y `dtrace` son las herramientas que FreeBSD te proporciona. Usadas en combinación, pueden acorralar bugs que de otro modo pasarían desapercibidos hasta llegar a producción.

La Sección 8 cierra el capítulo con las tareas de mantenimiento: refactorizar el driver, versionarlo, documentarlo, ejecutar análisis estático y someter a pruebas de regresión el conjunto completo.



## Sección 8: Refactorización y versionado de tu driver concurrente

El driver tiene ahora semánticas de concurrencia bien comprendidas y una estrategia de locking que puede defenderse desde los primeros principios. Esta sección final del capítulo es la pasada de higiene: organizar el código para mayor claridad, versionar el driver para que el tú del futuro pueda saber qué cambió y cuándo, escribir un README que documente el diseño, ejecutar análisis estático y someter el conjunto a pruebas de regresión.

Este no es un trabajo vistoso. Pero es también lo que separa un driver que se entrega una sola vez de un driver que perdura y resulta útil durante años.

### 8.1: Organización del código para mayor claridad

El driver de la Etapa 4 del Capítulo 10 ya estaba bien organizado. El cbuf tiene su propio archivo. Los manejadores de I/O usan funciones auxiliares. La disciplina de locking está documentada en un comentario al principio. Para el Capítulo 11, el trabajo de organización es marginal: mejorar los comentarios, agrupar el código relacionado y garantizar la coherencia en los nombres.

Vale la pena realizar tres mejoras.

**Agrupa el código relacionado.** Reordena las funciones en `myfirst.c` de modo que las relacionadas queden juntas. El ciclo de vida (attach, detach, modevent) al principio; los manejadores de I/O (read, write, poll) en el centro; las funciones auxiliares y las devoluciones de llamada de sysctl al final. El orden de compilación no importa; lo que importa es que un lector que recorre el archivo encuentre las funciones relacionadas juntas.

**Aísla el locking en wrappers inline.** En lugar de repetir `mtx_lock(&sc->mtx)` y `mtx_unlock(&sc->mtx)` por todo el código, define:

```c
#define MYFIRST_LOCK(sc)        mtx_lock(&(sc)->mtx)
#define MYFIRST_UNLOCK(sc)      mtx_unlock(&(sc)->mtx)
#define MYFIRST_ASSERT(sc)      mtx_assert(&(sc)->mtx, MA_OWNED)
```

Usa las macros en todo el código. Si alguna vez necesitas cambiar el tipo de lock (por ejemplo, a un lock `sx` en una refactorización futura), cambias un solo lugar, no veinte.

**Nombra las cosas de forma coherente.** Todas las funciones auxiliares de buffer a nivel de driver son `myfirst_buf_*`. Todas las funciones auxiliares de espera son `myfirst_wait_*`. Todos los manejadores de sysctl son `myfirst_sysctl_*`. Un lector que recorra los nombres de las funciones puede saber a qué categoría pertenece cada función sin necesidad de leer el cuerpo.

Ninguna de estas es una mejora de corrección. Todas son mejoras de legibilidad que dan sus frutos cuando vuelves al código seis meses después.

### 8.2: Control de versiones del driver

El driver debe exponer su versión para que puedas saber en el momento de la carga qué estás ejecutando. Añade una cadena de versión:

```c
#define MYFIRST_VERSION "0.4-concurrency"
```

Imprímela en el momento del attach:

```c
device_printf(dev,
    "Attached; version %s, node /dev/%s (alias /dev/myfirst), "
    "cbuf=%zu bytes\n",
    MYFIRST_VERSION, devtoname(sc->cdev), cbuf_size(&sc->cb));
```

Expónla como un sysctl de solo lectura:

```c
SYSCTL_STRING(_hw_myfirst, OID_AUTO, version, CTLFLAG_RD,
    MYFIRST_VERSION, 0, "Driver version");
```

Un usuario puede ahora consultar `sysctl hw.myfirst.version` o revisar `dmesg` para confirmar qué versión está cargada. Al depurar, esto elimina cualquier duda sobre qué código está en ejecución.

Elige un esquema de versiones y cíñete a él. El versionado semántico (major.minor.patch) funciona perfectamente. El versionado por fecha (2026.04) también. El versionado basado en capítulos del libro (0.1 tras el Capítulo 7, 0.2 tras el Capítulo 8, 0.3 tras el Capítulo 9, 0.4 tras el Capítulo 10, 0.5 tras el Capítulo 11) también funciona y es el que usa el código fuente companion. La propiedad importante es la coherencia, no los detalles concretos.

Para el Capítulo 11 en concreto, el incremento de versión adecuado es a `0.5-concurrency`. Los cambios son: migración a `counter(9)`, KASSERTs añadidos, `LOCKING.md` añadido, anotaciones añadidas. Todos son cambios de seguridad y claridad; ninguno es un cambio de comportamiento visible desde el espacio de usuario. Documéntalos en un `CHANGELOG.md`:

```markdown
# myfirst Changelog

## 0.5-concurrency (Chapter 11)
- Migrated bytes_read, bytes_written to counter_u64_t (lock-free).
- Added KASSERTs throughout cbuf_* helpers.
- Added LOCKING.md documenting the locking strategy.
- Added source annotations naming each critical section.
- Added MYFIRST_LOCK/UNLOCK/ASSERT macros for future lock changes.

## 0.4-poll-refactor (Chapter 10, Stage 4)
- Added d_poll and selinfo.
- Refactored I/O handlers to use wait helpers.
- Added locking-strategy comment.

## 0.3-blocking (Chapter 10, Stage 3)
- Added mtx_sleep-based blocking read/write paths.
- Added IO_NDELAY -> EAGAIN handling.

## 0.2-circular (Chapter 10, Stage 2)
- Replaced linear FIFO with cbuf circular buffer.

## 0.1 (Chapter 9)
- Initial read/write via uiomove.
```

Un `CHANGELOG.md` que puedas consultar supera al historial de git cuando quieres la respuesta rápida. Mantenlo actualizado con cada cambio.

### 8.3: README y revisión de comentarios

Junto a `LOCKING.md`, escribe un `README.md` para el driver. El público objetivo es un mantenedor futuro (posiblemente tú mismo) que acaba de descargar el código fuente y necesita saber en qué consiste el proyecto.

Una versión mínima:

```markdown
# myfirst

A FreeBSD 14.3 pseudo-device driver that demonstrates buffered I/O,
concurrency, and modern driver conventions. Developed as the running
example for the book "FreeBSD Device Drivers: From First Steps to
Kernel Mastery."

## Status

Version 0.5-concurrency (Chapter 11).

## Features

- A Newbus pseudo-device under nexus0.
- A primary device node at /dev/myfirst/0 (alias: /dev/myfirst).
- A circular buffer (cbuf) as the I/O buffer.
- Blocking and non-blocking reads and writes.
- poll(2) support via d_poll and selinfo.
- Per-CPU byte counters via counter(9).
- A single sleep mutex protects composite state; see LOCKING.md.

## Build and Load

    $ make
    # kldload ./myfirst.ko
    # dmesg | tail
    # ls -l /dev/myfirst
    # printf 'hello' > /dev/myfirst
    # cat /dev/myfirst
    # kldunload myfirst

## Tests

See ../../userland/ for the test programs. The most useful one is
producer_consumer, which exercises the round-trip correctness of
the circular buffer.

## License

BSD 2-Clause. See individual source files for SPDX headers.
```

Todo driver se beneficia de un README como este. Sin él, un nuevo mantenedor (posiblemente tú, dentro de seis meses) tiene que deducir por ingeniería inversa en qué consiste el proyecto. Con él, la incorporación al proyecto se reduce a minutos.

Al mismo tiempo, realiza una revisión de comentarios en el propio código fuente. Céntrate en las secciones críticas: cada `mtx_lock` debe tener un breve comentario que nombre el estado compartido que está a punto de proteger. Cada función auxiliar debe tener una descripción de una línea encima de la definición de la función. Cada operación aritmética no obvia (el wrap-around del cbuf, la comparación de `nbefore`, etc.) debe tener una frase de explicación.

El objetivo no es un comentario exhaustivo. Se trata de que el código sea autoexplicativo para un lector que no lo ha visto antes.

### 8.4: Análisis estático

El sistema base de FreeBSD incluye `clang`, que tiene un modo `--analyze` que realiza análisis estático sin compilar. Para un módulo del kernel, invócalo mediante:

```sh
$ make WARNS=6 CFLAGS+="-Weverything -Wno-unknown-warning-option" clean all
```

O, de forma más directa:

```sh
$ clang --analyze -I/usr/src/sys -I/usr/src/sys/amd64/conf/GENERIC \
    -D_KERNEL myfirst.c
```

La salida es una lista de posibles problemas con anotaciones de archivo y línea. Clasifícalos: los falsos positivos (clang no entiende algunos modismos del kernel) se pueden ignorar sin problema; los problemas genuinos (variables no inicializadas, desreferencias de puntero nulo, fugas de memoria) merecen corrección.

Añade un objetivo `lint` a tu `Makefile`:

```makefile
.PHONY: lint
lint:
	cd ${.CURDIR}; clang --analyze -D_KERNEL *.c
```

Ejecuta `make lint` periódicamente. Una ejecución limpia es la línea de base; cualquier nuevo aviso merece atención antes de ser integrado.

### 8.5: Pruebas de regresión

Ensambla una prueba de regresión que ejecute todas las verificaciones que has construido. Un script de shell en el subdirectorio `tests/`:

```sh
#!/bin/sh
# regression.sh: run every test from Chapters 7-11 in sequence.

set -eu

die() { echo "FAIL: $*" >&2; exit 1; }
ok()  { echo "PASS: $*"; }

# Preconditions
[ $(id -u) -eq 0 ] || die "must run as root"
kldstat | grep -q myfirst && kldunload myfirst
[ -f ./myfirst.ko ] || die "myfirst.ko not built; run make first"

kldload ./myfirst.ko
trap 'kldunload myfirst 2>/dev/null || true' EXIT

sleep 1
[ -c /dev/myfirst ] || die "device node not created"
ok "load"

printf 'hello' > /dev/myfirst || die "write failed"
cat /dev/myfirst >/tmp/out.$$
[ "$(cat /tmp/out.$$)" = "hello" ] || die "round-trip content mismatch"
rm -f /tmp/out.$$
ok "round-trip"

cd ../userland && make -s clean && make -s && cd -

../userland/producer_consumer || die "producer_consumer failed"
ok "producer_consumer"

../userland/mp_stress || die "mp_stress failed"
ok "mp_stress"

sysctl dev.myfirst.0.stats >/dev/null || die "sysctl not accessible"
ok "sysctl"

echo "ALL TESTS PASSED"
```

Ejecútalo tras cada cambio:

```sh
# ./tests/regression.sh
PASS: load
PASS: round-trip
PASS: producer_consumer
PASS: mp_stress
PASS: sysctl
ALL TESTS PASSED
```

Una regresión en verde es la prueba de que el cambio no ha roto nada. Combínalo con las ejecuciones de estrés en el kernel de depuración y tendrás una barrera de calidad razonable para un driver en esta etapa del libro.

### Una plantilla completa de LOCKING.md

Para los lectores que quieran una plantilla de inicio más completa que la mínima de la Sección 3, aquí tienes un `LOCKING.md` más detallado que puede adaptarse a cualquier driver:

```markdown
# <driver-name> Locking Strategy

## Overview

One sentence describing the overall approach. For example:
"The driver uses a single sleep mutex to serialize all access to
shared state, with separate per-CPU counters for hot-path
statistics."

## Locks Owned by This Driver

### sc->mtx (mutex(9), MTX_DEF)

**Protects:**
- sc->cb (circular buffer state: cb_head, cb_used, cb_data)
- sc->active_fhs
- sc->is_attached (writes)
- sc->rsel, sc->wsel (indirectly: selrecord inside a critical
  section; selwakeup outside)

**Wait channels used under this mutex:**
- &sc->cb: data available / space available

## Locks Owned by Other Subsystems

- selinfo internal locks: handled by the selinfo API; we must
  call selrecord under our lock and selwakeup outside.

## Unprotected Accesses

### sc->is_attached (reads at handler entry)

**Why it's safe:**
A stale "true" is re-checked after every sleep; a stale "false"
merely causes the handler to return ENXIO early, which is
harmless since the device really is gone.

### sc->bytes_read, sc->bytes_written (counter(9) fetches)

**Why it's safe:**
counter(9) handles its own consistency internally. Fetches may
be slightly stale but are never torn.

## Lock Order

The driver currently holds only one lock. No ordering rules apply.

When new locks are added, document the order here before merging
the change. The format is:

  sc->mtx -> sc->other_mtx

meaning: a thread holding sc->mtx may acquire sc->other_mtx,
but not the reverse.

## Rules

1. Never sleep while holding sc->mtx except via mtx_sleep, which
   atomically drops and reacquires.
2. Never call uiomove, copyin, copyout, selwakeup, or wakeup
   while holding sc->mtx.
3. Every cbuf_* call must happen with sc->mtx held (the helpers
   assert this with mtx_assert).
4. The detach path clears sc->is_attached under the mutex and
   then wakes any sleepers before destroying the mutex.

## History

- 0.5-concurrency (Chapter 11): migrated byte counters to
  counter(9); added mtx_assert calls; formalized the strategy.
- 0.4-poll-refactor (Chapter 10): added d_poll, refactored into
  helpers, documented the initial strategy inline in the source.
- Earlier versions: strategy was implicit in the code.
```

Un documento con esta estructura es auditable. Cualquier cambio futuro en la estrategia de concurrencia del driver toca este archivo en el mismo commit. El diff de revisión muestra el cambio de regla, no solo el cambio de código.

### Una lista de comprobación antes de confirmar cambios

Antes de confirmar un cambio que afecte a la concurrencia, recorre esta lista de comprobación:

1. **¿He actualizado `LOCKING.md`?** Si el cambio añade un campo al estado compartido, nombra un nuevo lock, cambia el orden de locking, añade un canal de espera o modifica una regla, actualiza el documento.
2. **¿He ejecutado la suite de regresión completa?** En un kernel de depuración, con `WITNESS` e `INVARIANTS`. Una regresión en verde es el mínimo.
3. **¿He ejecutado las pruebas de estrés?** No solo una vez; durante el tiempo suficiente para que tenga sentido. Treinta minutos es un mínimo razonable para un cambio significativo.
4. **¿He ejecutado `clang --analyze`?** Trata los nuevos avisos como errores.
5. **¿He añadido un `KASSERT` para cualquier nuevo invariante?** Todo invariante que tu código asuma debe ser un `KASSERT`.
6. **¿He incrementado la cadena de versión y actualizado `CHANGELOG.md`?** Aunque el cambio sea pequeño. El tú del futuro se lo agradecerá al tú del presente.
7. **¿He verificado que el kit de pruebas compila?** Los programas de prueba forman parte del cambio; no deben degradarse.

La mayoría de los cambios superan la lista de comprobación en minutos. Los que no lo hacen son los que deben ser detectados por la lista: esos son los que tienen más probabilidades de causar regresiones.

### Estrategia de versiones: ¿por qué 0.x?

Usamos un esquema 0.x para el driver porque el driver aún no está completo en cuanto a funcionalidades. Una versión 1.0 implica estabilidad y completitud que no podemos afirmar. Las versiones 0.x siguen nuestro progreso a lo largo del libro:

- 0.1: andamiaje básico (Capítulo 7).
- 0.2: archivo de dispositivo y estado por apertura (Capítulo 8).
- 0.3: lectura/escritura básica mediante `uiomove` (Capítulo 9).
- 0.4: I/O con buffer, sin bloqueo, poll (Capítulo 10).
- 0.5: consolidación de la concurrencia (Capítulo 11).

El Capítulo 12 introducirá la 0.6. Cuando el driver esté completo y estable, lo llamaremos 1.0.

Este no es el único esquema sensato. El versionado semántico (`MAJOR.MINOR.PATCH`) es apropiado para drivers con una API estable. El versionado por fecha (`2026.04`) es apropiado para drivers cuya cadencia de versiones es temporal. La elección importa menos que el compromiso: elige uno, aplícalo de forma coherente y documéntalo en el README.

### Una nota sobre la higiene de Git

El árbol de código fuente companion está organizado de modo que la etapa de cada capítulo vive en su propio directorio. Puedes construir cualquier etapa de forma independiente; puedes comparar etapa por etapa para ver qué ha cambiado. Si usas git, un historial de commits limpio lo mejora aún más:

- Un commit por cambio lógico.
- Mensajes de commit que expliquen el *porqué*, no solo el *qué*.
- Un commit separado para las actualizaciones de `LOCKING.md` cuando cambia el código.

Un log de git como:

```text
0.5 add KASSERT for cbuf_write overflow invariant
0.5 migrate bytes_read, bytes_written to counter(9)
0.5 document locking strategy in LOCKING.md
0.5 add MYFIRST_LOCK/UNLOCK/ASSERT macros
0.4 refactor I/O handlers to use wait helpers
```

es legible años después. Un log como:

```text
fix stuff
more fixes
wip
```

no lo es. Tu driver es un artefacto vivo; trata su historial como documentación.

### Cerrando la Sección 8

El driver es ahora no solo correcto, sino *verificablemente* correcto y *manteniblemente* organizado. Cuenta con:

- Una estrategia de locking documentada en `LOCKING.md`.
- Un esquema de versiones y un changelog.
- Un README para futuros mantenedores.
- Anotaciones en el código fuente para cada sección crítica.
- Análisis estático mediante `clang --analyze`.
- Una prueba de regresión que ejecuta todo.

Eso es una cantidad considerable de infraestructura. La mayoría de los drivers de principiantes no tienen nada de esto. El tuyo sí lo tiene, y el trabajo para mantenerlo es rutinario, no heroico.



## Laboratorios prácticos

Estos laboratorios consolidan los conceptos del Capítulo 11 mediante experiencia directa y práctica. Están ordenados de menor a mayor dificultad. Cada uno está diseñado para completarse en una sola sesión de laboratorio.

### Lista de comprobación previa al laboratorio

Antes de comenzar cualquier laboratorio, confirma los cuatro puntos siguientes. Dos minutos de preparación aquí te ahorrarán mucho tiempo cuando algo falle dentro de un laboratorio y no puedas determinar si el problema es tu código, tu entorno o tu kernel.

1. **Kernel de depuración en ejecución.** Confirma con `sysctl kern.ident` que el kernel arrancado es el que tiene `INVARIANTS` y `WITNESS` activados. La sección de referencia «Compilación y arranque de un kernel de depuración» más adelante en este capítulo explica el proceso de compilación, instalación y reinicio si aún no lo has hecho.
2. **WITNESS activo.** Ejecuta `sysctl debug.witness.watch` y confirma un valor distinto de cero. Si el sysctl no existe, tu kernel no tiene `WITNESS` compilado; reconstrúyelo antes de continuar.
3. **El código fuente del driver coincide con la Etapa 4 del Capítulo 10.** Desde el directorio de tu driver, `make clean && make` debe compilar sin errores. El artefacto `myfirst.ko` debe existir antes de que cualquier laboratorio intente cargarlo.
4. **Un dmesg limpio.** Ejecuta `dmesg -c >/dev/null` una vez antes del primer laboratorio para que los avisos anteriores no te confundan. Las comprobaciones posteriores de `dmesg` mostrarán solo lo que haya producido tu laboratorio.

Si alguno de los cuatro puntos es incierto, corrígelo ahora. Los laboratorios asumen los cuatro.

### Lab 11.1: Observar una condición de carrera

**Objetivo.** Construye el driver race-demo de la Sección 2, ejecútalo y observa la corrupción. El propósito no es producir un driver correcto; es ver con tus propios ojos qué significa «sin sincronización».

**Pasos.**

1. Crea `examples/part-03/ch11-concurrency/stage1-race-demo/`.
2. Copia `cbuf.c`, `cbuf.h` y el `Makefile` del directorio de la Etapa 4 del Capítulo 10.
3. Copia `myfirst.c` y añade las macros `RACE_DEMO` al principio como se muestra en la Sección 2. Sustituye cada `mtx_lock`, `mtx_unlock` y `mtx_assert` en los caminos de I/O por las macros `MYFIRST_LOCK`, `MYFIRST_UNLOCK`, `MYFIRST_ASSERT`. Compila con `-DRACE_DEMO` (que es el valor predeterminado en el `Makefile` de este directorio).
4. Construye y carga.
5. En un terminal, ejecuta `../../part-02/ch10-handling-io-efficiently/userland/producer_consumer`.
6. Observa la salida. Registra el número de discrepancias.
7. Ejecútalo varias veces. Observa que el número de discrepancias varía.
8. Descarga el driver.

**Verificación.** Cada ejecución produce al menos una discrepancia. El número exacto varía. Esto demuestra que los errores de concurrencia no son una posibilidad teórica; son una certeza cuando los locks están ausentes.

**Objetivo adicional.** Añade un `device_printf` dentro de `cbuf_write` que registre cuándo `cb_used` supera `cb_size`. Carga el driver, ejecuta la prueba y observa los mensajes de registro. Cada mensaje es una violación de invariante capturada en tiempo de ejecución.

### Lab 11.2: Verificar la disciplina de locking con INVARIANTS

**Objetivo.** Convierte tu driver de la Etapa 4 del Capítulo 10 para que use las macros `MYFIRST_LOCK` y añade llamadas a `mtx_assert` por todo el código. Ejecútalo bajo un kernel con `WITNESS` activado y confirma que no aparecen avisos.

**Pasos.**

1. Copia el driver de la etapa 4 en `examples/part-03/ch11-concurrency/stage2-concurrent-safe/`.
2. Añade las macros `MYFIRST_LOCK`, `MYFIRST_UNLOCK`, `MYFIRST_ASSERT` al comienzo del archivo. Haz que se expandan a las llamadas reales `mtx_*` (sin `RACE_DEMO`).
3. Sustituye las llamadas a `mtx_lock`, `mtx_unlock`, `mtx_assert` en las rutas de I/O por las macros correspondientes.
4. Añade `MYFIRST_ASSERT(sc)` al comienzo de cada función auxiliar que deba invocarse con el lock adquirido.
5. Compila con `WARNS=6`.
6. Carga el módulo en un kernel con `WITNESS` activado.
7. Ejecuta el kit de pruebas del capítulo 10 (`producer_consumer`, `rw_myfirst_nb`, `mp_stress`).
8. Confirma que no aparece ningún aviso de `WITNESS` en `dmesg`.

**Verificación.** `dmesg | grep -i witness` no muestra ninguna salida relacionada con `myfirst`. Todas las pruebas pasan.

**Objetivo adicional.** Introduce un error deliberado: elimina una llamada a `mtx_unlock`, dejando el lock adquirido al final del manejador. Observa el aviso de `WITNESS` que aparece, confirma que identifica el lock exacto y la línea aproximada y, a continuación, restaura la llamada a `mtx_unlock`.

### Laboratorio 11.3: Migrar a counter(9)

**Objetivo.** Sustituye los campos `sc->bytes_read` y `sc->bytes_written` por contadores por CPU de `counter(9)`.

**Pasos.**

1. Copia el código fuente del Laboratorio 11.2 en `stage3-counter9/`.
2. Cambia la definición de la estructura para que los dos campos sean `counter_u64_t`.
3. Actualiza `myfirst_attach` para asignarlos y `myfirst_detach` para liberarlos.
4. Actualiza `myfirst_buf_read` y `myfirst_buf_write` para usar `counter_u64_add`.
5. Elimina las actualizaciones de esos campos del interior de la sección crítica (ya no necesitan el mutex).
6. Actualiza los manejadores de `sysctl` para usar `counter_u64_fetch`.
7. Actualiza `LOCKING.md` para indicar que `bytes_read` y `bytes_written` ya no están bajo el mutex.
8. Compila, carga y ejecuta el kit de pruebas.

**Verificación.** `sysctl dev.myfirst.0.stats.bytes_read` y `bytes_written` siguen devolviendo valores correctos. Ninguna prueba falla.

**Reto adicional.** Ejecuta `mp_stress` con 8 escritores y 8 lectores durante 60 segundos en una máquina de varios núcleos. Compara el rendimiento con el Laboratorio 11.2 (contadores protegidos por mutex). Los contadores por CPU deberían ser mensurablemente más rápidos si la máquina tiene al menos 4 núcleos.

### Laboratorio 11.4: Construir un tester multihilo

**Objetivo.** Construye `mt_reader` del apartado 6. Úsalo para ejercitar el driver con múltiples threads en un único proceso.

**Pasos.**

1. En `examples/part-03/ch11-concurrency/userland/`, escribe `mt_reader.c` del apartado 6, o abre el archivo fuente complementario que ya está en ese directorio y léelo con atención.
2. Desde el mismo directorio, ejecuta `make mt_reader` (el Makefile ya incluye el enlazado correcto con `-pthread`). El comando independiente es `cc -Wall -Wextra -pthread -o mt_reader mt_reader.c`.
3. Inicia un escritor en otro terminal: `dd if=/dev/zero of=/dev/myfirst bs=4k &`.
4. Ejecuta `./mt_reader`.
5. Confirma que cada thread ve un recuento de bytes distinto de cero y que la suma es coherente con lo que el escritor produjo.
6. Cuando termines, detén el `dd` en segundo plano con `kill %1` (o el número de trabajo que tenga).

**Verificación.** Los cuatro threads informan de un recuento distinto de cero. El total general es igual al recuento de bytes del escritor menos el relleno del buffer al final.

**Reto adicional.** Aumenta `NTHREADS` a 8, 16 y 32. Observa el comportamiento de escalado. ¿Aumenta el rendimiento, disminuye o se mantiene constante? Explica por qué (pista: la discusión de la contención de lock del apartado 5).

### Laboratorio 11.5: Provocar deliberadamente un aviso de WITNESS

**Objetivo.** Introduce un error de concurrencia deliberado y observa cómo `WITNESS` lo detecta.

**Pasos.**

1. Copia el directorio de trabajo de la Etapa 3 en un directorio provisional como `stage-lab11-5/`. *No* edites la Etapa 3 directamente; el objetivo del laboratorio es deshacer los cambios limpiamente.
2. En `myfirst_read`, mueve la llamada `MYFIRST_UNLOCK(sc)` de antes de `uiomove` a después de `uiomove`. El código sigue compilando, pero ahora el mutex se mantiene durante la ejecución de `uiomove`, que puede dormirse.
3. Compila y carga en un kernel de depuración con `WITNESS` habilitado (consulta la referencia «Compilar e iniciar un kernel de depuración» más adelante en este capítulo).
4. Ejecuta `producer_consumer` en un terminal y vigila `dmesg` en otro.
5. Observa el aviso emitido por `WITNESS` cuando `uiomove` está a punto de dormirse mientras se mantiene `sc->mtx`. Anota el mensaje exacto y la línea de código fuente que cita.
6. Descarga el módulo, elimina el directorio provisional y confirma que el código fuente de la Etapa 3 sigue siendo correcto.

**Verificación.** `dmesg` muestra un aviso similar a "acquiring duplicate lock of same type" o "sleeping thread (pid N) owns a non-sleepable lock" apuntando a la línea donde solía estar el desbloqueo.

**Reto adicional.** Repite con otro error deliberado: cambia el tipo de mutex de `MTX_DEF` a `MTX_SPIN` en `mtx_init`. Observa los avisos resultantes, que deberían ser sobre dormirse mientras se mantiene un spinlock. Deshaz los cambios limpiamente cuando hayas terminado.

### Laboratorio 11.6: Añadir KASSERTs en todo el código

**Objetivo.** Añade llamadas `KASSERT` a los helpers de cbuf y al código del driver. Confirma que se activan cuando se provocan deliberadamente.

**Pasos.**

1. Copia el código fuente del Laboratorio 11.3 en `stage5-kasserts/`.
2. En `cbuf_write`, añade `KASSERT(cb->cb_used + n <= cb->cb_size, ...)` al principio.
3. En `cbuf_read`, añade `KASSERT(n <= cb->cb_used, ...)`.
4. En los helpers `myfirst_buf_*`, añade `mtx_assert(&sc->mtx, MA_OWNED)`.
5. Compila y carga en un kernel con `INVARIANTS`.
6. Ejecuta el kit de pruebas. No debería activarse ninguna aserción en operación normal.
7. Introduce un error: en `cbuf_write`, escribe `cb->cb_used += n + 1` en lugar de `cb->cb_used += n`. Confirma que el `KASSERT` en la siguiente llamada detecta el desbordamiento.
8. Deshaz el error.

**Verificación.** Toda aserción en el código fuente revertido compila y el driver funciona con normalidad. El error deliberado produce un pánico con un backtrace claro que nombra el `KASSERT` que falló.

### Laboratorio 11.7: Stress de larga duración

**Objetivo.** Ejecuta el kit de pruebas completo del Capítulo 11 durante una hora y verifica que no hay fallos, avisos del kernel ni crecimiento de memoria.

**Pasos.**

1. Carga el driver de la Etapa 3 o la Etapa 5 del Capítulo 11.
2. En un terminal, ejecuta:
   ```sh
   for i in $(seq 1 60); do
     ./producer_consumer
     ./mp_stress
   done
   ```
3. En un segundo terminal, ejecuta `vmstat 10` en segundo plano y comprueba `sysctl dev.myfirst.0.stats` periódicamente.
4. Vigila `dmesg` en busca de avisos.
5. Cuando termine el bucle, comprueba `vmstat -m | grep cbuf` y confirma que la memoria no ha crecido.

**Verificación.** Todos los bucles se completan. No hay avisos de `WITNESS`. `vmstat -m | grep cbuf` muestra la asignación constante esperada. Los contadores de bytes en `sysctl` han aumentado; su ratio entre `bytes_read` y `bytes_written` es aproximadamente 1.

### Laboratorio 11.8: Ejecutar clang --analyze

**Objetivo.** Ejecuta análisis estático sobre el driver y clasifica los resultados.

**Pasos.**

1. Desde el directorio de la Etapa 3 o la Etapa 5 del Capítulo 11, ejecuta:
   ```sh
   clang --analyze -D_KERNEL -I/usr/src/sys \
       -I/usr/src/sys/amd64/conf/GENERIC myfirst.c
   ```
2. Anota todos los avisos.
3. Para cada aviso, clasifícalo como (a) un verdadero positivo (error real), (b) un falso positivo que entiendes, o (c) un aviso que no entiendes.
4. Para (a), corrige el error. Para (b), documenta por qué es un falso positivo. Para (c), investiga hasta que pase a (a) o a (b).

**Verificación.** Tras el análisis, el driver tiene cero avisos sin clasificar. `LOCKING.md` o `README.md` documentan los falsos positivos conocidos.



## Ejercicios de desafío

Los desafíos amplían los conceptos del Capítulo 11 más allá de los laboratorios básicos. Todos son opcionales; todos están diseñados para profundizar tu comprensión.

### Desafío 1: Un productor/consumidor sin lock

El mutex que usamos protege estado compuesto. Una cola de productor único y consumidor único puede implementarse sin lock, utilizando únicamente operaciones atómicas, porque el escritor toca solo la cola y el lector toca solo la cabeza, y ninguno toca los campos que actualiza el otro. `buf_ring(9)` parte de esta idea y va más lejos: usa compare-and-swap para que varios productores puedan encolar de forma concurrente, y ofrece rutas de desencolar de consumidor único o consumidor múltiple que se eligen en el momento de la asignación.

Lee `/usr/src/sys/sys/buf_ring.h`, donde las rutas de encolar y desencolar sin lock viven como funciones inline, y `/usr/src/sys/kern/subr_bufring.c`, que contiene `buf_ring_alloc()` y `buf_ring_free()`. Comprende el primitivo `buf_ring(9)`. A continuación, como ejercicio, construye una variante del driver en la que el buffer circular se sustituya por un `buf_ring`. Los manejadores de lectura y escritura ya no necesitan un mutex para el anillo en sí; puede que aún lo necesiten para otro estado.

Este desafío es más difícil de lo que parece. Intenta abordarlo solo si dominas el material del Capítulo 11 y buscas un reto mayor.

### Desafío 2: Diseño multi-mutex

Divide `sc->mtx` en dos locks: `sc->state_mtx` (protege `is_attached`, `active_fhs`, `open_count`) y `sc->buf_mtx` (protege el cbuf y los contadores de bytes). Define un orden de lock (por ejemplo, state antes que buf) y documéntalo en `LOCKING.md`. Actualiza todos los manejadores.

¿Qué se gana? ¿Qué se pierde? ¿Hay alguna carga de trabajo que se beneficie de forma mensurable?

### Desafío 3: WITNESS bajo presión

Construye un test que provoque un aviso de `WITNESS` incluso en un driver correctamente bloqueado. (Pista: los locks `sx` tienen reglas distintas; mezclar un mutex y un lock `sx` en órdenes conflictivos produce avisos. El Capítulo 12 cubre `sx`.) Este desafío es un anticipo del material del Capítulo 12.

### Desafío 4: Instrumentar con dtrace

Escribe un script de `dtrace` que:

- Cuente el número de llamadas a `mtx_lock(&sc->mtx)` por segundo.
- Imprima un histograma del tiempo empleado manteniendo el lock.
- Correlacione el tiempo de mantenimiento del lock con las tasas de `bytes_read` y `bytes_written`.

Ejecuta el script mientras se ejecuta `mp_stress`. Elabora un informe.

### Desafío 5: Bloqueo acotado

Modifica `myfirst_read` para devolver `EAGAIN` si la lectura fuera a bloquearse durante más de N milisegundos, donde N es un parámetro ajustable mediante sysctl. (Pista: el argumento `timo` de `mtx_sleep` se expresa en ticks, donde `hz` ticks equivalen a un segundo. Por ejemplo, `hz / 10` es aproximadamente 100 milisegundos. Para precisión subtick, la familia `msleep_sbt(9)` usa `sbintime_t` y las constantes `SBT_1S`, `SBT_1MS` y `SBT_1US` de `sys/time.h`.) Este desafío anticipa parte del material del Capítulo 12 sobre tiempos de espera.

### Desafío 6: Escribir un benchmark de contención de lock

Construye un benchmark que mida la tasa de adquisición del mutex bajo distintas cargas:

- 1 escritor, 1 lector.
- 1 escritor, 4 lectores.
- 4 escritores, 4 lectores.
- 8 escritores, 8 lectores.

Informa de las adquisiciones por segundo, el tiempo medio de mantenimiento y el tiempo máximo de espera. Usa `dtrace` o el sysctl de perfilado de mutex.

### Desafío 7: Portar a una plataforma de 32 bits

FreeBSD todavía admite arquitecturas de 32 bits (i386, armv7). Compila tu driver para una de ellas y ejecuta el kit de pruebas. ¿Se manifiesta alguno de los problemas de lectura fragmentada del apartado 3? ¿Es la migración a contadores por CPU del Laboratorio 11.3 necesaria para la corrección, o solo para el rendimiento?

(Este desafío es genuinamente difícil y requiere acceso a hardware de 32 bits o a un entorno de compilación cruzada.)

### Desafío 8: Añadir un mutex por descriptor

La estructura `struct myfirst_fh` por descriptor tiene contadores `reads` y `writes` que actualmente se actualizan bajo el `sc->mtx` a nivel de dispositivo. Una alternativa es dar a cada `fh` su propio mutex. ¿Ayudaría? ¿Perjudicaría? Escribe el cambio, ejecuta el benchmark e informa.



## Resolución de problemas

Esta referencia recoge los errores que es más probable que encuentres mientras trabajas en el Capítulo 11 y más adelante. Cada entrada tiene un síntoma, una causa y una solución.

### Síntoma: pánico de `mtx_assert` en un driver que antes funcionaba

**Causa.** Se llamó a un helper sin mantener el mutex. Esto suele ocurrir cuando refactorizas y mueves una llamada fuera de la sección crítica.

**Solución.** Observa el backtrace; el frame superior nombra el helper. Encuentra el llamador. Asegúrate de que el llamador mantiene `sc->mtx` en el punto de la llamada.

### Síntoma: aviso `sleepable after non-sleepable acquired`

**Causa.** Se realizó una llamada que puede dormirse (normalmente `uiomove`, `copyin`, `copyout`, `malloc(... M_WAITOK)`) mientras se mantenía un lock no durmiente. En FreeBSD, `MTX_DEF` es un lock durmiente; el aviso suele ser más específico.

**Solución.** Suelta el mutex antes de la llamada bloqueante. Si hay que preservar el estado, toma una instantánea bajo el mutex, realiza la llamada bloqueante fuera y vuelve a adquirirlo para confirmar la actualización.

### Síntoma: inversión del orden de lock

**Causa.** Se observaron dos órdenes de adquisición; uno es el inverso del otro. Solo es posible con dos o más locks.

**Solución.** Define un orden global. Actualiza la ruta conflictiva. Documenta en `LOCKING.md`.

### Síntoma: deadlock (cuelgue)

**Causa.** O falta un `wakeup(&sc->cb)` o hay un sleep con un canal distinto al del wake.

**Solución.** Comprueba cada canal de `mtx_sleep` frente a cada canal de `wakeup`. Deben coincidir exactamente. Dormirse en `&sc->cb` y despertar en `&sc->buf` son canales distintos.

### Síntoma: lectura fragmentada de un `uint64_t`

**Causa.** En una plataforma de 32 bits, una lectura de 64 bits son dos lecturas de 32 bits, que pueden intercalarse con una escritura concurrente. En amd64, los accesos de 64 bits alineados son atómicos; en plataformas de 32 bits, no lo son.

**Solución.** O protege con el mutex (sencillo), o migra a `counter(9)` (escalable), o usa los primitivos `atomic_*_64` (preciso).

### Síntoma: pánico por doble liberación en el cbuf

**Causa.** `cbuf_destroy` se llama dos veces, normalmente porque la ruta de error en `myfirst_attach` ejecuta `cbuf_destroy` y luego `myfirst_detach` lo ejecuta de nuevo.

**Solución.** Tras llamar a `cbuf_destroy`, asigna `sc->cb.cb_data = NULL` para que una segunda llamada sea una operación nula. O protege la destrucción con un indicador explícito.

### Síntoma: Rendimiento lento bajo carga en sistemas con muchos núcleos

**Causa.** Contención de mutex. Cada CPU se está serializando en `sc->mtx`.

**Solución.** Migra los contadores de bytes a `counter(9)` (Laboratorio 11.3). Si se necesita reducir aún más la contención, divide el mutex (Desafío 2) o usa primitivas de grano más fino (Capítulo 12).

### Síntoma: `producer_consumer` se bloquea ocasionalmente

**Causa.** Una condición de carrera entre el escritor que termina y el lector que decide que el buffer está «vacío de forma permanente». Nuestro driver devuelve cero bytes en una lectura no bloqueante sobre un buffer vacío, lo que `producer_consumer` puede interpretar como EOF.

**Solución.** Usa lecturas bloqueantes en `producer_consumer` (elimina `O_NONBLOCK` si se añadió). Asegúrate de que el escritor cierre el descriptor o de que el lector detecte el fin de la prueba por el recuento total de bytes, no por una lectura de cero bytes.

### Síntoma: `WITNESS` notifica "unowned lock released"

**Causa.** Se llama a `mtx_unlock` sin un `mtx_lock` correspondiente anterior en el mismo thread.

**Solución.** Audita cada punto donde aparece `mtx_unlock`. Rastrea hacia atrás y confirma que cada uno tiene un `mtx_lock` correspondiente antes, a lo largo de cada camino de ejecución posible.

### Síntoma: Compila sin errores, supera las pruebas monohilo, falla en `mp_stress`

**Causa.** Un lock omitido en algún lugar del camino de datos. Las pruebas monohilo nunca ejercitan la condición de carrera; las pruebas concurrentes sí.

**Solución.** Audita cada acceso al estado compartido, como en la Sección 3. El culpable habitual es un campo que se actualiza fuera de la sección crítica porque «es solo un contador» (algo que no se sostiene bajo concurrencia).

### Síntoma: `kldunload` se bloquea con descriptores abiertos

**Causa.** El camino de detach se niega a continuar mientras `active_fhs > 0`.

**Solución.** Usa `fstat | grep myfirst` para encontrar los procesos problemáticos. Ejecuta `kill` sobre ellos. Después, vuelve a intentar `kldunload`.

### Síntoma: Pánico del kernel con un backtrace limpio en `mtx_lock`

**Causa.** El mutex fue destruido y su memoria reutilizada. La operación atómica en `mtx_lock` opera sobre lo que resulte estar en esa memoria en ese momento.

**Solución.** Asegúrate de que `mtx_destroy` solo se llame después de que todos los manejadores que pudieran usar el mutex hayan retornado. Nuestro camino de detach hace esto destruyendo primero el cdev, lo que espera a que los manejadores en vuelo terminen.

### Síntoma: `WITNESS` nunca advierte, incluso cuando esperarías que lo hiciera

**Causa.** Hay tres posibilidades. Primera: el kernel en ejecución no tiene `WITNESS` compilado; comprueba `sysctl debug.witness.watch` y confirma un valor distinto de cero. Segunda: `WITNESS` está habilitado pero no emite mensajes porque todavía no se ha violado ninguna regla; no toda refactorización produce una violación. Tercera: el camino de código problemático no se ha ejercitado; `WITNESS` solo comprueba los órdenes que el kernel observa realmente en tiempo de ejecución.

**Solución.** Confirma que el kernel tiene `WITNESS` (el `sysctl` anterior es la comprobación más sencilla). Luego ejercita el camino de código sospechoso con los evaluadores multiproceso y multithread de la Sección 6 para que el kernel observe las adquisiciones en cuestión.

### Síntoma: `dmesg` no muestra nada pero la prueba sigue fallando

**Causa.** O bien el fallo está en el espacio de usuario (el programa de prueba), o bien el kernel sí detectó un problema pero no imprimió nada porque el buffer de mensajes dio la vuelta. Esto último es poco habitual en un sistema recién arrancado, pero frecuente en máquinas de prueba que llevan mucho tiempo en ejecución.

**Solución.** Aumenta el buffer para el próximo arranque añadiendo `kern.msgbufsize="4194304"` a `/boot/loader.conf`; el sysctl es de solo lectura en tiempo de ejecución. Para los fallos en espacio de usuario, ejecuta la prueba bajo `truss(1)` para ver las syscalls y sus valores de retorno; los errores de concurrencia en espacio de usuario suelen manifestarse como `EAGAIN`, `EINTR` o lecturas cortas que el programa de prueba no gestiona correctamente.



## Cerrando

El Capítulo 11 tomó el driver que construiste en el Capítulo 10 y lo situó sobre una base de comprensión de la concurrencia. No cambiamos su comportamiento; construimos la comprensión que te permite cambiar ese comportamiento de forma segura en el futuro.

Comenzamos con la premisa de que la concurrencia es el entorno predeterminado en el que vive todo driver, y que un driver que ignora ese hecho es un driver esperando fallar en la máquina más rápida, la carga de trabajo más intensa o el mayor número de núcleos de algún usuario. Los cuatro modos de fallo (corrupción de datos, actualizaciones perdidas, valores rotos, estado compuesto inconsistente) no son casos extremos infrecuentes; son una certeza cuando múltiples flujos tocan estado compartido sin coordinación.

Después construimos las herramientas que previenen esos fallos. Los atómicos, para los casos pequeños y sencillos. Los mutex, para todo lo demás. Aplicamos las herramientas al driver: los contadores por CPU sustituyeron a los contadores de bytes protegidos por mutex, el mutex único cubre el cbuf y sus invariantes compuestos, y `mtx_sleep` proporciona el camino bloqueante que el Capítulo 10 usaba sin explicación.

Aprendimos a verificar. `INVARIANTS` y `WITNESS` detectan, en tiempo de desarrollo, los casos de dormir con un mutex activo y las violaciones de orden de los locks. `KASSERT` y `mtx_assert` documentan los invariantes y detectan sus violaciones. `dtrace` observa sin perturbar. Un conjunto de pruebas multithread y multiproceso ejercita el driver a niveles que las herramientas del sistema base no pueden alcanzar.

Refactorizamos para mejorar la mantenibilidad. La estrategia de locking está documentada en `LOCKING.md`. El versionado es explícito. El README presenta el proyecto a un nuevo lector. La prueba de regresión ejecuta todas las pruebas que hemos construido.

El driver es ahora el driver de dispositivo de caracteres más robusto que muchos principiantes habrán escrito jamás. Es también pequeño, comprensible y con referencias al código fuente. Cada decisión de locking tiene una justificación desde los principios básicos. Cada prueba tiene un propósito específico. Cada pieza de infraestructura sirve de apoyo al trabajo del siguiente capítulo.

Tres recordatorios finales antes de continuar.

El primero es *ejecutar las pruebas*. Los errores de concurrencia no se manifiestan solos. Si no has ejecutado `mp_stress` durante una hora en un kernel de depuración, no has confirmado que tu driver es correcto; solo has confirmado que tus pruebas no han podido encontrar un error en el tiempo que les diste.

El segundo es *mantener `LOCKING.md` actualizado*. Todo cambio futuro que toque estado compartido debe actualizar el documento. Un driver cuya historia de concurrencia se aleja de su documentación es un driver que acumula errores sutiles.

El tercero es *confiar en las primitivas*. Las primitivas mutex, atómicas y de sleep del kernel son el resultado de décadas de ingeniería y están ampliamente probadas. Las reglas parecen arbitrarias al principio; no lo son. Aprende las reglas, aplícalas de forma coherente y el kernel hará el trabajo duro por ti.



## Material de referencia del Capítulo 11

Las secciones que siguen son material de referencia para este capítulo. No forman parte de la secuencia de enseñanza principal; están aquí porque volverás a ellas cada vez que necesites recordar una primitiva específica, verificar un invariante o revisar un concepto que el capítulo introdujo brevemente. Léelas en orden la primera vez; después vuelve a las secciones individuales cuando las necesites.

Las secciones de referencia llevan nombre en lugar de letra, para evitar confusión con los apéndices A a E del libro. Cada sección es independiente.

## Referencia: Construcción y arranque de un kernel de depuración

Varios laboratorios de este capítulo asumen un kernel construido con `INVARIANTS` y `WITNESS`. En un sistema FreeBSD 14.3 de lanzamiento, el kernel `GENERIC` estándar no incluye estas opciones, porque conllevan un coste en tiempo de ejecución inadecuado para producción. Este recorrido muestra cómo construir e instalar un kernel de depuración que sí las incluya.

### Paso 1: Preparar una configuración personalizada

Haz una copia de la configuración genérica para no modificar el original:

```sh
# cd /usr/src/sys/amd64/conf
# cp GENERIC MYFIRSTDEBUG
```

Edita `MYFIRSTDEBUG` y añade las opciones de depuración si aún no están presentes:

```text
ident           MYFIRSTDEBUG
options         INVARIANTS
options         INVARIANT_SUPPORT
options         WITNESS
options         WITNESS_SKIPSPIN
options         DDB
options         KDB
options         KDB_UNATTENDED
```

`DDB` habilita el depurador integrado en el kernel, al que puedes entrar durante un pánico para inspeccionar el estado. `KDB` es el marco del depurador del kernel. `KDB_UNATTENDED` hace que el sistema reinicie tras un pánico en lugar de esperar a una persona en la consola, que es la configuración adecuada para una máquina de laboratorio sin consola física.

### Paso 2: Construir el kernel

```sh
# cd /usr/src
# make buildkernel KERNCONF=MYFIRSTDEBUG -j 4
```

El `-j 4` paraleliza en 4 núcleos. Ajústalo a tu máquina.

En una máquina razonablemente rápida, el build tarda entre 15 y 30 minutos. En una máquina lenta, puede tardar una hora o más. Este es un coste que se paga una sola vez; las reconstrucciones incrementales son mucho más rápidas.

### Paso 3: Instalar y reiniciar

```sh
# make installkernel KERNCONF=MYFIRSTDEBUG
# shutdown -r now
```

La instalación coloca el nuevo kernel en `/boot/kernel/` y el antiguo en `/boot/kernel.old/`. Si el nuevo kernel entra en pánico durante el boot, puedes arrancar desde `/boot/kernel.old` en el prompt del cargador.

### Paso 4: Confirmar el arranque

Tras el reinicio, confirma que las opciones de depuración están activas:

```sh
$ sysctl kern.ident
kern.ident: MYFIRSTDEBUG
```

Ahora estás ejecutando un kernel de depuración. Las llamadas `KASSERT` de tu driver se activarán cuando se produzcan fallos. Las advertencias de `WITNESS` aparecerán en `dmesg`.

### Paso 5: Reconstruir el driver

Los módulos deben reconstruirse con el código fuente del kernel en ejecución:

```sh
$ cd your-driver-directory
$ make clean && make
```

Carga como de costumbre:

```sh
# kldload ./myfirst.ko
```

### Volver al kernel de producción

Si quieres volver al kernel sin depuración:

```sh
# shutdown -r now
```

En el prompt del cargador, escribe `unload` y luego `boot /boot/kernel.old/kernel` (o renombra los directorios como describe el handbook de FreeBSD).

El kernel de depuración es más lento que el kernel de producción. No ejecutes cargas de trabajo de producción en él. Sí ejecuta todas las pruebas del driver en él.



## Referencia: Lecturas adicionales sobre concurrencia en FreeBSD

Para los lectores que quieran profundizar más allá de lo que cubre este capítulo, aquí hay referencias a las fuentes canónicas:

### Páginas de manual

- `mutex(9)`: la referencia definitiva de la API de mutex.
- `locking(9)`: una visión general de las primitivas de locking de FreeBSD.
- `lock(9)`: la infraestructura común de objetos de lock.
- `atomic(9)`: la API de operaciones atómicas.
- `counter(9)`: la API de contadores por CPU.
- `msleep(9)` y `mtx_sleep(9)`: las primitivas de sleep con interlock.
- `condvar(9)`: variables de condición (material del Capítulo 12).
- `sx(9)`: locks compartidos/exclusivos que permiten sleep (material del Capítulo 12).
- `rwlock(9)`: locks de lector/escritor (material del Capítulo 12).
- `witness(4)`: el comprobador de orden de locks WITNESS.

### Archivos fuente

- `/usr/src/sys/kern/kern_mutex.c`: la implementación del mutex.
- `/usr/src/sys/kern/subr_sleepqueue.c`: el mecanismo de la cola de sleep.
- `/usr/src/sys/kern/subr_witness.c`: la implementación de WITNESS.
- `/usr/src/sys/sys/mutex.h`: el archivo de cabecera del mutex con las definiciones de flags.
- `/usr/src/sys/sys/_mutex.h`: la estructura del mutex.
- `/usr/src/sys/sys/lock.h`: declaraciones comunes de objetos de lock.
- `/usr/src/sys/sys/atomic_common.h` y `/usr/src/sys/amd64/include/atomic.h`: primitivas atómicas.
- `/usr/src/sys/sys/counter.h`: el archivo de cabecera de la API de `counter(9)`.

### Material externo

El handbook de FreeBSD cubre la programación del kernel a nivel de visión general. Para mayor profundidad, *The Design and Implementation of the FreeBSD Operating System* (McKusick et al.) es el libro de texto canónico. Para la teoría de concurrencia aplicable a cualquier OS, *The Art of Multiprocessor Programming* (Herlihy y Shavit) es excelente. Ninguno de los dos es lectura obligatoria para nuestros propósitos, pero ambos son referencias útiles.



## Referencia: Autoevaluación del Capítulo 11

Antes de pasar al Capítulo 12, usa esta rúbrica para confirmar que has interiorizado el material del Capítulo 11. Deberías ser capaz de responder a cada pregunta sin releer el capítulo. Si alguna pregunta resulta difícil, vuelve a la sección correspondiente.

### Preguntas conceptuales

1. **Nombra los cuatro modos de fallo del estado compartido sin sincronización.** Corrupción de datos, actualizaciones perdidas, valores rotos, estado compuesto inconsistente.

2. **¿Qué es una condición de carrera?** Acceso solapado a estado compartido, al menos una escritura, y corrección que depende del entrelazado.

3. **¿Por qué `mtx_sleep` recibe el mutex como argumento de interlock, en lugar de que el llamador lo desbloquee y luego duerma?** Porque desbloquear y luego dormir tiene una ventana de wakeup perdido: un `wakeup` concurrente podría dispararse después del desbloqueo pero antes del sleep, entregarse a nadie y dejar al durmiente esperando para siempre.

4. **¿Por qué está prohibido mantener un mutex no durmiente durante una llamada a `uiomove`?** Porque `uiomove` puede dormir en un fallo de página del espacio de usuario, y dormir con un lock no durmiente activo está prohibido por el kernel (detectado por `WITNESS` en kernels de depuración, comportamiento indefinido en kernels de producción).

5. **¿Cuál es la diferencia entre `MTX_DEF` y `MTX_SPIN`?** `MTX_DEF` duerme cuando hay contención; `MTX_SPIN` realiza una espera activa y deshabilita las interrupciones. Usa `MTX_DEF` para código en contexto de thread; usa `MTX_SPIN` solo cuando dormir está prohibido (por ejemplo, dentro de manejadores de interrupciones de hardware).

6. **¿Cuándo es suficiente una operación atómica para reemplazar un mutex?** Cuando el estado compartido es un único campo del tamaño de una palabra y la operación no se compone con otros campos ni requiere bloqueo.

7. **¿Qué detecta `WITNESS` que las pruebas en un único thread no detectan?** Inversiones en el orden de adquisición de locks, dormir con el lock incorrecto retenido, liberar un lock que no se posee, adquisición recursiva de un lock no recursivo.

8. **¿Por qué es más rápido el contador por CPU (`counter(9)`) que un contador protegido por mutex?** Porque los contadores por CPU no generan contención entre CPUs. Cada CPU actualiza su propia línea de caché; la suma solo ocurre en el momento de la lectura.

### Preguntas de lectura de código

Abre el código fuente de tu driver del Capítulo 11 y responde:

1. Cada llamada a `mtx_lock(&sc->mtx)` debería ir seguida, en cualquier camino de ejecución, de exactamente una llamada a `mtx_unlock(&sc->mtx)`. Verifica esto por inspección.

2. Cada llamada a `mtx_sleep(&chan, &sc->mtx, ...)` debería tener una llamada `wakeup(&chan)` correspondiente en algún lugar del driver. Encuéntralas.

3. Las llamadas a `mtx_assert(&sc->mtx, MA_OWNED)` afirman que el mutex está tomado. Cualquier llamada a esas funciones auxiliares debe tener el mutex en posesión. Verifica esto por inspección.

4. El camino de detach establece `sc->is_attached = 0` antes de llamar a `wakeup(&sc->cb)`. ¿Por qué es importante el orden?

5. Los manejadores de I/O liberan `sc->mtx` antes de llamar a `uiomove`. ¿Por qué?

6. La llamada a `selwakeup` se coloca fuera del mutex. ¿Por qué?

### Preguntas prácticas

1. Carga el driver del Capítulo 11 en un kernel con `WITNESS` habilitado y ejecuta el kit de pruebas. ¿Hay alguna advertencia en `dmesg`? Si la hay, investígala; si no, considera el driver verificado.

2. Introduce un fallo deliberado: comenta una llamada a `wakeup(&sc->cb)`. Ejecuta el kit de pruebas. ¿Qué ocurre?

3. Revierte el fallo. Introduce otro fallo diferente: comenta una llamada a `mtx_unlock` y añádela en su lugar después de la llamada a `uiomove`. Ejecuta en `WITNESS`. ¿Qué dice `WITNESS`?

4. Ejecuta `mp_stress` durante 60 segundos. ¿Converge el recuento de bytes?

5. Ejecuta `producer_consumer` 100 veces en un bucle. Cada ejecución debería pasar. ¿Es así?

Si las cinco preguntas prácticas se superan, el trabajo del Capítulo 11 es sólido. Estás listo para el Capítulo 12.



## Referencia: una mirada más detallada a las colas de espera

Esta sección de referencia explica, a un nivel adecuado para un principiante curioso, qué ocurre dentro del kernel cuando un thread llama a `mtx_sleep` o `wakeup`. No necesitas este material para escribir un driver correcto; llevas varios capítulos haciéndolo. Esta sección existe porque, una vez que has visto funcionar las primitivas, puede que quieras saber *cómo* funcionan. Este conocimiento resulta útil cuando lees otros drivers y cuando depuras problemas de concurrencia poco frecuentes.

### La cola de espera

Una **cola de espera** (sleep queue) es una estructura de datos del kernel que registra los threads que esperan una condición. Cada espera activa corresponde exactamente a una cola de espera. Cuando un thread llama a `mtx_sleep(chan, mtx, pri, wmesg, timo)`, el kernel busca (o crea) la cola de espera asociada a `chan` (el canal de espera) y añade el thread a ella. Cuando se llama a `wakeup(chan)`, el kernel busca la cola y despierta a todos los threads que contiene.

La propia estructura de datos se define en `/usr/src/sys/kern/subr_sleepqueue.c`. Una tabla hash indexada por la dirección del canal asigna canales a colas. Cada cola tiene un spinlock que protege su lista de waiters. El spinlock es interno al mecanismo de colas de espera; nunca lo ves directamente.

¿Por qué una tabla hash? Porque un canal puede ser cualquier valor de puntero, y el kernel puede estar gestionando miles de esperas simultáneas. Un mapa directo (una cola por canal) sería demasiado disperso; una lista global única generaría demasiada contención. La tabla hash distribuye la carga entre muchos cubos, manteniendo las búsquedas rápidas.

### La atomicidad de mtx_sleep

Esto es lo que hace `mtx_sleep(chan, mtx, pri, wmesg, timo)` en realidad, a grandes rasgos:

1. Tomar el spinlock de la cola de espera para `chan`.
2. Añadir el thread actual a la cola de espera bajo ese lock.
3. Liberar `mtx` (el mutex externo que mantiene el llamante).
4. Llamar al planificador, que elige otro thread para ejecutar.
5. Cuando se despierte, el planificador restaura este thread. El control regresa dentro de `mtx_sleep`.
6. Liberar el spinlock de la cola de espera.
7. Volver a adquirir `mtx`.
8. Regresar al llamante.

El punto clave es que los pasos 1 y 2 ocurren antes del paso 3. Una vez que el thread está en la cola de espera, cualquier llamada posterior a `wakeup(chan)` lo eliminará. Si `wakeup(chan)` se dispara entre el paso 3 y el paso 4, elimina el thread de la cola y lo marca como ejecutable; el paso 4 (llamar al planificador) seguirá produciéndose, pero encontrará el thread inmediatamente ejecutable, por lo que la espera es de duración cero.

La ventana que habría sido «mutex liberado, aún no en la cola de espera» no existe. La entrada en la cola de espera ocurre *primero*, mientras el mutex todavía está tomado; después el mutex se libera *mientras el lock de la cola de espera está tomado*. Ningún wakeup puede colarse.

Esta es la condición de carrera que la operación atómica de liberación y espera previene, visible a nivel del código fuente.

### El camino de wakeup

`wakeup(chan)` hace lo siguiente:

1. Tomar el spinlock de la cola de espera para `chan`.
2. Eliminar todos los threads de la cola.
3. Marcar cada thread eliminado como ejecutable.
4. Liberar el spinlock de la cola de espera.
5. Si alguno de los threads eliminados tiene mayor prioridad que el thread actual, llamar al planificador para que realice una expulsión.

Los threads eliminados no se ejecutan de inmediato; simplemente son ejecutables. El planificador los recogerá a su debido tiempo. Si ninguno tiene mayor prioridad que el llamante, el llamante continúa ejecutándose; si alguno la tiene, el llamante cede.

`wakeup_one(chan)` es lo mismo excepto que elimina solo un thread (el de mayor prioridad, con orden FIFO entre prioridades iguales). Esto es preferible cuando solo un thread puede consumir la señal de forma útil. Para un buffer de consumidor único, `wakeup_one` evita el problema del thundering herd; para una señal que puede beneficiar a múltiples consumidores, `wakeup` es la opción adecuada.

Para nuestro driver, cualquiera de las dos funcionaría. Usamos `wakeup` porque tanto un lector como un escritor pueden estar esperando en el mismo canal, y ambos deben ser notificados si cambia el estado del buffer.

### Propagación de prioridad dentro de mtx_sleep

La Sección 5 presentó la propagación de prioridad como la defensa del kernel contra la inversión de prioridades. El mecanismo reside en el propio código del mutex: cuando `mtx_sleep` añade el thread llamante a la cola de espera, inspecciona el propietario del mutex (almacenado como un puntero de thread dentro de la palabra del mutex). Si la prioridad del thread en espera es mayor que la del propietario, la prioridad del propietario se eleva. La elevación persiste hasta que el propietario libera el mutex, momento en el que se restaura la prioridad original del propietario y el thread en espera lo adquiere.

Puedes observar este efecto en tiempo real en un sistema en ejecución con `top -SH` durante una carga de trabajo con contención: la prioridad del thread que mantiene un lock en disputa fluctúa a medida que llegan y se van distintos threads en espera. Es uno de los servicios del kernel de los que te beneficias de forma silenciosa; la elevación es automática y correcta, y no tienes que activarla explícitamente.

### El spinning adaptativo, revisitado

La Sección 5 presentó el spinning adaptativo como la optimización que permite a `mtx_lock` evitar dormirse cuando el propietario está a punto de liberar el mutex. El mecanismo de colas de espera es donde se toma esa decisión. En esencia, el camino lento hace aproximadamente lo siguiente:

```text
while (!try_acquire()) {
        if (holder is running on another CPU)
                spin briefly;
        else
                enqueue on the sleep queue and yield;
}
```

La comprobación de «el propietario se está ejecutando en otro CPU» lee la palabra del mutex para obtener el puntero al propietario y luego pregunta al planificador si ese thread está actualmente en ejecución en algún CPU. Si es así, es probable que el mutex se libere en microsegundos y una breve espera activa es menos costosa que un cambio de contexto. Si no (el propietario está dormido o ha sido retirado del planificador), el spinning no tiene sentido y el thread en espera se une a la cola de espera de inmediato. El umbral y el número exacto de iteraciones de spinning son parámetros ajustables del kernel; sus valores precisos no importan para el escritor de drivers, siempre que confíes en que `mtx_lock` es rápido en el caso habitual y entra a dormir de forma adecuada cuando es necesario.

### El argumento de prioridad

`mtx_sleep` toma un argumento de prioridad. En el código del Capítulo 10, pasamos `PCATCH` (permite la interrupción por señal) sin prioridad explícita. La prioridad predeterminada, en FreeBSD, es la que tenga el thread llamante en ese momento; `PCATCH` por sí solo no cambia la prioridad.

A veces quieres dormir con una prioridad específica. El argumento tiene la forma `PRIORITY | FLAGS`, donde `PRIORITY` es un número (cuanto más bajo, mejor) y `FLAGS` incluye `PCATCH`. Para nuestro driver, la prioridad predeterminada es suficiente. Drivers especializados pueden pasar `PUSER | PCATCH` para garantizar que el thread duerma con una prioridad adecuada para tareas visibles al usuario; los drivers de tiempo real pueden pasar una prioridad más baja (mejor).

Este es un detalle que puedes ignorar sin problemas hasta que un driver concreto lo requiera. El valor predeterminado es la respuesta correcta la mayor parte del tiempo.

### Convenciones del mensaje de espera (wmesg)

El argumento `wmesg` de `mtx_sleep` es una cadena corta que aparece en `ps`, `procstat` y herramientas similares. Convenciones:

- Entre cinco y siete caracteres. Las cadenas más largas se truncan.
- En minúsculas.
- Indica tanto el subsistema como la operación.
- `myfrd` = «myfirst read». `myfwr` = «myfirst write». `tunet` = «tun network».

Un buen `wmesg` permite a un desarrollador que revisa la salida de `ps -AxH` saber de inmediato qué espera un thread dormido. Los pocos segundos que se tardan en inventar un buen nombre se recuperan la primera vez que alguien necesita entender un sistema bloqueado.

### Espera con tiempo límite

El argumento `timo` de `mtx_sleep` es el tiempo máximo de espera en ticks. El valor cero significa «indefinido»; un valor positivo significa «despertar tras este número de ticks aunque nadie haya llamado a `wakeup`».

Cuando expira el tiempo límite, `mtx_sleep` devuelve `EWOULDBLOCK`. Se espera que el llamante compruebe el valor de retorno y decida qué hacer.

Los tiempos límite son la forma de implementar el bloqueo acotado. El Capítulo 12 cubre `msleep_sbt(9)`, que toma un plazo de tipo `sbintime_t` en lugar de un contador de ticks entero y resulta más conveniente para precisión sub-milisegundo. Para el Capítulo 11, el tiempo límite sencillo basado en ticks es suficiente.

Ejemplo de uso, para un driver que desea un tiempo máximo de espera de 5 segundos:

```c
error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", hz * 5);
if (error == EWOULDBLOCK) {
        /* Timed out; return EAGAIN or retry. */
}
```

La constante `hz` es la frecuencia de ticks del kernel (normalmente 1000 en FreeBSD 14.3, configurable mediante `kern.hz`). Multiplicar por 5 da 5 segundos. Esta es una técnica práctica para un driver que necesita garantías de progreso.



## Referencia: lectura de kern_mutex.c

El código fuente de la implementación de mutex de FreeBSD se encuentra en `/usr/src/sys/kern/kern_mutex.c`. Esta sección de referencia recorre las partes más interesantes a un nivel adecuado para un escritor de drivers que quiere saber qué ocurre cuando llama a `mtx_lock`.

No necesitas entender cada línea. Sí necesitas poder abrir el archivo y reconocer la forma de lo que está ocurriendo.

### La estructura mutex

Un mutex es una estructura pequeña. Su definición está en `/usr/src/sys/sys/_mutex.h`:

```c
struct mtx {
        struct lock_object      lock_object;
        volatile uintptr_t      mtx_lock;
};
```

`lock_object` es la cabecera común utilizada por todas las clases de lock (mutexes, sx locks, rw locks, lockmgr locks, rmlocks). Contiene el nombre, los indicadores del lock y el estado de seguimiento de WITNESS.

`mtx_lock` es la palabra de lock real. Su valor codifica el estado:

- `0`: desbloqueado.
- Un puntero de thread: el identificador del thread que mantiene el lock.
- Puntero de thread con los bits bajos establecidos: el lock está tomado con modificadores (`MTX_CONTESTED`, `MTX_RECURSED`, etc.).

Adquirir el mutex es, en esencia, una operación compare-and-swap: cambiar `mtx_lock` de `0` (desbloqueado) a `curthread` (el puntero del thread llamante).

### El camino rápido de mtx_lock

Abre `kern_mutex.c` y busca `__mtx_lock_flags`. Verás algo como:

```c
void
__mtx_lock_flags(volatile uintptr_t *c, int opts, const char *file, int line)
{
        struct mtx *m;
        uintptr_t tid, v;

        m = mtxlock2mtx(c);
        /* ... WITNESS setup ... */

        tid = (uintptr_t)curthread;
        v = MTX_UNOWNED;
        if (!_mtx_obtain_lock_fetch(m, &v, tid))
                _mtx_lock_sleep(m, v, opts, file, line);
        /* ... lock acquired; lock-profiling bookkeeping ... */
}
```

La operación crítica es `_mtx_obtain_lock_fetch`, que es un compare-and-swap. Intenta cambiar `mtx->mtx_lock` de `MTX_UNOWNED` (cero) a `tid` (el puntero del thread actual). Si tiene éxito, el mutex está en nuestra posesión y la función retorna sin más trabajo. Si falla, vamos a `_mtx_lock_sleep`.

El camino rápido es, por tanto, un compare-and-swap más cierta contabilidad de WITNESS. En un mutex sin contención, `mtx_lock` cuesta esencialmente una operación atómica.

### El camino lento

`_mtx_lock_sleep` es el camino lento. Gestiona el caso en que el mutex ya está en posesión de otro thread. La función tiene varios centenares de líneas; las partes críticas son:

```c
for (;;) {
        /* Try to acquire. */
        if (_mtx_obtain_lock_fetch(m, &v, tid))
                break;

        /* Adaptive spin if holder is running. */
        if (owner_running(v)) {
                for (i = 0; i < spin_count; i++) {
                        if (_mtx_obtain_lock_fetch(m, &v, tid))
                                goto acquired;
                }
        }

        /* Go to sleep. */
        enqueue_on_sleep_queue(m);
        schedule_out();
        /* When we resume, loop back and try again. */
}
acquired:
        /* ... priority propagation bookkeeping ... */
```

La estructura es: intentar el compare-and-swap, hacer spinning si el propietario está ejecutándose, de lo contrario dormir. Despertar y reintentar. Eventualmente adquirir.

Varios aspectos del código real complican este esqueleto: la gestión del indicador `MTX_CONTESTED`, la propagación de prioridad, el perfilado del lock, el seguimiento de WITNESS y los casos especiales de recursión. No te preocupes por ellos en una primera lectura; la estructura anterior es la esencia.

### El camino de desbloqueo

`__mtx_unlock_flags` es la liberación. Su camino rápido:

```c
void
__mtx_unlock_flags(volatile uintptr_t *c, int opts, const char *file, int line)
{
        struct mtx *m;
        uintptr_t tid;

        m = mtxlock2mtx(c);
        tid = (uintptr_t)curthread;

        if (!_mtx_release_lock(m, tid))
                _mtx_unlock_sleep(m, opts, file, line);
}
```

`_mtx_release_lock` intenta cambiar atómicamente `mtx->mtx_lock` de `tid` (el thread actual) a `MTX_UNOWNED`. Si lo consigue, el mutex queda liberado sin complicaciones. Si falla, es porque surgieron problemas (el flag `MTX_CONTESTED` estaba activado porque habían llegado threads en espera), y `_mtx_unlock_sleep` se encarga de resolverlos.

El caso habitual: un único store atómico. El caso poco habitual: extraer threads en espera de la cola y despertar uno.

### La conclusión

Leer `kern_mutex.c` una sola vez, aunque sea de manera superficial, te proporciona una intuición muy útil. El mutex no es magia. Es un compare-and-swap con un mecanismo de respaldo hacia una cola de sleep. Todo lo demás es contabilidad interna y optimización.

Las primitivas que utilizas (`mtx_lock`, `mtx_unlock`) son envoltorios ligeros sobre estos mecanismos internos. Comprenderlos significa que puedes razonar sobre su coste, sus garantías y sus límites. Esa es la recompensa.



## Referencia: mini-glosario de vocabulario de concurrencia

La terminología importa en concurrencia más que en casi cualquier otro tema, porque quienes discrepan en la terminología suelen terminar discrepando también sobre si su código es correcto. Un breve glosario de los términos utilizados en este capítulo:

**Atomic operation**: una operación de memoria que es indivisible desde la perspectiva de otros CPUs. Se completa como una sola transacción; ningún otro CPU puede observar un resultado parcial.

**Barrier / fence**: un punto en el código más allá del cual el compilador y la CPU no pueden mover determinados tipos de accesos a memoria. Las barreras existen en formas de adquisición (acquire), liberación (release) y completas (full).

**Critical section**: una región contigua de código que accede a estado compartido y debe ejecutarse sin interferencias de otros threads concurrentes.

**Deadlock**: una situación en la que dos o más threads esperan cada uno un recurso que tiene otro, sin que ninguno pueda avanzar.

**Invariant**: una propiedad de una estructura de datos que se cumple fuera de las secciones críticas. El código dentro de una sección crítica puede violar temporalmente el invariant; el código fuera no debe hacerlo.

**Livelock**: una situación en la que los threads avanzan en el sentido de ejecutar instrucciones, pero el estado útil del sistema no progresa. Suele producirse por reintentos en bucle que fallan sistemáticamente.

**Lock-free**: describe un algoritmo o estructura de datos que evita las primitivas de exclusión mutua, confiando típicamente en operaciones atómicas. Los algoritmos lock-free garantizan que al menos un thread siempre avanza; no evitan necesariamente la inanición de threads específicos.

**Memory ordering**: las reglas que gobiernan cuándo una escritura en un CPU se hace visible para las lecturas en otros CPUs. Distintas arquitecturas tienen modelos diferentes (fuertemente ordenados, débilmente ordenados, release-acquire).

**Mutual exclusion (mutex)**: una primitiva de sincronización que garantiza que solo un thread a la vez puede ejecutar una región protegida.

**Priority inversion**: una situación en la que un thread de baja prioridad posee un recurso que necesita un thread de alta prioridad, y un thread de prioridad intermedia expulsa al poseedor de baja prioridad, bloqueando en la práctica al thread de alta prioridad que espera.

**Priority propagation (inheritance)**: el mecanismo del kernel para resolver la inversión de prioridad elevando temporalmente la prioridad del poseedor de baja prioridad hasta igualar la del thread en espera de mayor prioridad.

**Race condition**: condición de carrera. Una situación en la que la corrección de un programa depende del entrelazado exacto de accesos concurrentes a estado compartido, donde al menos uno de esos accesos es una escritura.

**Sleep mutex (`MTX_DEF`)**: un mutex que pone a dormir a los threads contendientes. Se puede usar en contextos donde está permitido dormir.

**Spin mutex (`MTX_SPIN`)**: un mutex que hace que los threads contendientes esperen en un bucle activo (busy loop). Es obligatorio en contextos donde no está permitido dormir, como los manejadores de interrupciones de hardware.

**Starvation**: una situación en la que un thread está en estado ejecutable pero nunca llega a ejecutarse porque otros threads siempre son planificados antes. La inversión de prioridad es una causa específica de inanición.

**Wait-free**: una propiedad más estricta que lock-free. Se garantiza que cada thread avanzará en un número acotado de pasos, independientemente de lo que hagan los demás threads.

**WITNESS**: el comprobador de orden de locks y disciplina de bloqueo de FreeBSD. Una opción del kernel que registra las adquisiciones de locks y advierte sobre violaciones.



## Referencia: patrones de concurrencia en drivers reales de FreeBSD

Antes de que el Capítulo 12 presente primitivas de sincronización adicionales, conviene examinar cómo se gestiona la concurrencia en los drivers reales de `/usr/src/sys/dev/`. Los patrones que se describen a continuación son los más habituales. Cada uno muestra que las herramientas que has aprendido (un mutex, un canal de sleep, selinfo) son las mismas que lleva FreeBSD.

### Patrón: mutex de softc por driver

Casi todos los drivers de dispositivos de caracteres del árbol de código fuente utilizan un único mutex por softc. Algunos ejemplos:

- `/usr/src/sys/dev/evdev/evdev_private.h` declara `ec_buffer_mtx` para el estado por cliente.
- `/usr/src/sys/dev/random/randomdev.c` utiliza `sysctl_lock` para su configuración privada.
- `/usr/src/sys/dev/null/null.c` no usa mutex porque su cbuf es prácticamente sin estado; esta es la excepción que confirma la regla.

El patrón de mutex por softc no es novedoso, ni sofisticado, ni opcional. Es la columna vertebral de la concurrencia de drivers en FreeBSD. Nuestro driver `myfirst` lo sigue porque todos los drivers pequeños lo siguen.

### Patrón: canal de sleep sobre un campo de softc

La convención para el argumento `chan` de `mtx_sleep` es usar la dirección de un campo del softc relacionado con la condición de espera. Algunos ejemplos:

- `evdev_read` en `/usr/src/sys/dev/evdev/cdev.c` duerme sobre la estructura por cliente (mediante `mtx_sleep(client, ...)`) y se despierta cuando llegan nuevos eventos.
- Nuestro `myfirst_read` duerme sobre `&sc->cb` y se despierta correspondientemente.

La dirección puede ser cualquier cosa; lo que importa es que los threads que esperan y los que despiertan usen el mismo puntero. Usar la dirección de un campo de estructura tiene la ventaja de que el canal es visible en el código fuente y se autodocumenta: quien lea el código sabe qué está esperando el thread.

### Patrón: selinfo para soporte de poll()

Todo driver que soporte `poll(2)` o `select(2)` tiene al menos un `struct selinfo`, a veces dos (para la disponibilidad de lectura y de escritura). Las mismas dos llamadas aparecen siempre:

- `selrecord(td, &si)` dentro del manejador d_poll.
- `selwakeup(&si)` cuando cambia el estado de disponibilidad.

Esto es exactamente lo que hace `myfirst`. El driver evdev lo hace. Los drivers tty lo hacen. Es la forma estándar.

### Patrón: buffer de rebote con uiomove

Muchos drivers con buffers circulares usan el patrón de buffer de rebote que tratamos en el Capítulo 10: copiar bytes del anillo a un buffer local en la pila, soltar el lock, ejecutar uiomove hacia el espacio de usuario con ese buffer, y volver a adquirir el lock. Algunos ejemplos:

- `evdev_read` copia una única estructura de evento en una variable local antes de llamar a `uiomove`.
- `ucom_put_data` en `/usr/src/sys/dev/usb/serial/usb_serial.c` tiene una forma similar para su buffer circular.
- Nuestro `myfirst_read` tiene la misma forma para el cbuf.

Esta es la respuesta idiomática a «¿cómo ejecuto uiomove desde un buffer circular mientras tengo un mutex?». El rebote es pequeño (a menudo en la pila, a veces en un buffer pequeño dedicado), y el lock se suelta alrededor del uiomove.

### Patrón: Counter(9) para estadísticas

Los drivers modernos usan `counter(9)` para los contadores de estadísticas. Algunos ejemplos:

- `/usr/src/sys/net/if.c` usa `if_ierrors`, `if_oerrors` y muchos otros contadores como `counter_u64_t`.
- Muchos drivers de red (ixgbe, mlx5, etc.) usan `counter(9)` para sus estadísticas por cola.

La migración de `myfirst` a `counter(9)` nos alinea con este patrón moderno. Los drivers más antiguos que todavía usan contadores atómicos simples suelen ser anteriores a la API `counter(9)`; el código más nuevo usa `counter(9)`.

### Patrón: detach con usuarios activos

Un desafío recurrente es el detach cuando aún hay procesos de usuario que tienen el dispositivo abierto. Dos enfoques habituales:

**Rechazar el detach mientras está en uso.** Nuestro driver hace esto: detach devuelve `EBUSY` si `active_fhs > 0`. Es simple y seguro, pero significa que el módulo no puede descargarse mientras alguien lo está usando.

**Detach forzado con destroy_dev_drain.** El kernel proporciona `destroy_dev_drain(9)`, que espera a que todos los manejadores retornen e impide que se inicien nuevos. Combinado con una bandera en el softc que indica que el dispositivo está siendo eliminado, permite un detach forzado con finalización ordenada de las operaciones en vuelo. El Capítulo 12 y posteriores cubren esto con más detalle.

Para `myfirst`, el enfoque más simple de rechazar el detach es el correcto. Un driver de producción que deba admitir hot-unplug (USB, por ejemplo) utilizará el enfoque más complejo.

### Patrón: orden de locks documentado con WITNESS

Los drivers con más de un lock documentan el orden. Ejemplo:

```c
/*
 * Locks in this driver, in acquisition order:
 *   sc->sc_mtx
 *   sc->sc_listmtx
 *
 * A thread holding sc->sc_mtx may acquire sc->sc_listmtx, but
 * not the reverse.  See sc_add_entry() for the canonical example.
 */
```

WITNESS valida este orden en tiempo de ejecución; el comentario lo hace legible para los humanos. Nuestro driver tiene un solo lock y no tiene reglas de orden, pero en cuanto añadamos un segundo (el Capítulo 12 puede introducir uno), añadiremos este tipo de documentación.

### Patrón: aserciones en todas partes

Los drivers reales están repletos de `KASSERT` y `mtx_assert`. Algunos ejemplos:

- `if_alloc` en `/usr/src/sys/net/if.c` verifica invariantes sobre las estructuras de entrada.
- El código GEOM (en `/usr/src/sys/geom/`) tiene `g_topology_assert` y construcciones similares en la mayoría de las funciones.
- Los drivers de red a menudo tienen `NET_EPOCH_ASSERT()` para confirmar que el llamador está en el contexto correcto.

Las aserciones son baratas en producción y salvan la vida durante el desarrollo. El trabajo del Capítulo 11 añadió varias; los capítulos futuros añadirán más.

### Lo que los drivers reales hacen y el nuestro todavía no

Hay tres patrones que aparecen en drivers reales y que nuestro driver todavía no utiliza:

**Locking de granularidad fina.** Un driver de red podría tener un lock por cola; un driver de almacenamiento podría tener un lock por petición. Nuestro driver tiene un solo lock para todo. Para drivers pequeños esto es correcto; para drivers con rutas calientes en muchos núcleos, no es suficiente. El lock `sx` del Capítulo 12 y la discusión de patrones de tipo RCU del Capítulo 15 cubren los casos de mayor granularidad.

**Rutas calientes sin locks.** `buf_ring(9)` proporciona colas multi-productor con dequeue de consumidor único o múltiple, sin locks en la ruta caliente. `epoch(9)` permite a los lectores avanzar sin locks. Estas son optimizaciones para cargas de trabajo específicas. Un driver para principiantes no debería usarlas primero; la justificación debería ser un cuello de botella medido, no uno especulativo.

**Locking jerárquico con múltiples clases.** Un driver real podría usar un spin mutex para el contexto de interrupción, un sleep mutex para el trabajo normal y un lock sx para la configuración. Cada uno tiene su lugar; las interacciones están regidas por reglas de orden estrictas. Los introduciremos en el Capítulo 12 y los revisaremos en capítulos posteriores cuando drivers específicos los necesiten.

### La conclusión

Los patrones de `/usr/src/sys/dev/` no son exóticos. Son los que hemos venido construyendo. Un `evdev_client` con un mutex, un canal de sleep y un selinfo es estructuralmente igual que un `myfirst_softc` con los mismos tres elementos. Leer algunos de estos drivers con las herramientas que ha construido el Capítulo 11 debería resultar casi familiar. Cuando algo parezca extraño, normalmente es una especialización (por ejemplo, el registro de filtros kqueue) que cubren capítulos futuros.

Esta es la recompensa acumulativa del libro: los patrones de cada capítulo hacen que una fracción mayor del código fuente real sea legible.



## Referencia: modismos habituales de mutex

Esta es una colección de consulta rápida de los modismos que más utilizarás en el código de drivers de FreeBSD.

### Sección crítica básica

```c
mtx_lock(&sc->mtx);
/* access shared state */
mtx_unlock(&sc->mtx);
```

### Verificación de posesión del lock

```c
mtx_assert(&sc->mtx, MA_OWNED);
```

Colócalo al inicio de cada función auxiliar que asuma que el mutex está adquirido. En kernels sin `INVARIANTS` se compila como nada; en kernels con `INVARIANTS` provoca un panic si el mutex no está adquirido.

### Try-lock

```c
if (mtx_trylock(&sc->mtx)) {
        /* got it */
        mtx_unlock(&sc->mtx);
} else {
        /* someone else has it; back off */
}
```

Úsalo cuando quieras evitar el bloqueo. Es útil en callbacks de temporizador que no deben contender con los manejadores de I/O; si el lock está tomado, salta este ciclo y reintenta en el siguiente.

### Sleep esperando una condición

```c
mtx_lock(&sc->mtx);
while (!condition)
        mtx_sleep(&chan, &sc->mtx, PCATCH, "wmesg", 0);
/* condition is true now; do work */
mtx_unlock(&sc->mtx);
```

El bucle `while` es imprescindible. Los despertares espurios están permitidos y son frecuentes; el thread debe volver a comprobar la condición tras cada retorno de `mtx_sleep`.

### Despertar a los threads dormidos

```c
wakeup(&chan);              /* wake all */
wakeup_one(&chan);          /* wake highest-priority one */
```

Llámalo sin tener el mutex. El kernel gestiona el orden.

### Sleep con tiempo límite

```c
error = mtx_sleep(&chan, &sc->mtx, PCATCH, "wmesg", hz * 5);
if (error == EWOULDBLOCK) {
        /* timed out */
}
```

Limita la espera a 5 segundos. Resulta conveniente para latidos (heartbeats) y operaciones con fecha límite.

### Liberar y readquirir alrededor de una llamada que puede dormir

```c
mtx_lock(&sc->mtx);
/* snapshot what we need */
snap = sc->field;
mtx_unlock(&sc->mtx);

/* do the sleeping call */
error = uiomove(buf, snap, uio);

mtx_lock(&sc->mtx);
/* update shared state */
sc->other_field = /* ... */;
mtx_unlock(&sc->mtx);
```

Este es el patrón que usan nuestros manejadores de I/O. El mutex nunca se mantiene adquirido a través de una llamada que puede dormir.

### Atómicos en lugar de locks para contadores simples

```c
counter_u64_add(sc->bytes_read, got);
```

Más rápido y escalable que `mtx_lock; sc->bytes_read += got; mtx_unlock`.

### Verificar y establecer de forma atómica

```c
if (atomic_cmpset_int(&sc->flag, 0, 1)) {
        /* we set it from 0 to 1; we are the "first" */
}
```

No hace falta ningún mutex. La propia operación compare-and-swap es atómica.



## Mirando hacia adelante: puente al capítulo 12

El capítulo 12 se titula *Mecanismos de sincronización*. Su alcance abarca el resto del conjunto de herramientas de sincronización de FreeBSD: variables de condición, locks compartidos-exclusivos con capacidad de sleep, locks de lectura/escritura, esperas con tiempo límite y las técnicas más avanzadas para depurar deadlocks. Todo lo que contiene el capítulo 12 se construye sobre la base de mutexes que el capítulo 11 ha establecido.

El puente lo forman tres observaciones concretas del trabajo del capítulo 11.

En primer lugar, ya usas un "canal" (`&sc->cb`) con `mtx_sleep` y `wakeup`. El capítulo 12 presenta las **variables de condición** (`cv(9)`), que son una alternativa con nombre y estructura al patrón de canal anónimo. Verás que `cv_wait_sig(&cb_has_data, &sc->mtx)` suele ser más claro que `mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", 0)`, especialmente cuando hay varias condiciones en las que el thread podría quedar en espera.

En segundo lugar, el único mutex de tu driver serializa todo, incluidas las lecturas que en teoría podrían producirse en paralelo. El capítulo 12 presenta los locks compartidos-exclusivos con capacidad de sleep de `sx(9)`, que permiten que varios lectores mantengan el lock de forma simultánea mientras los escritores se serializan. Razonarás sobre cuándo ese diseño es adecuado (cargas de trabajo con muchas lecturas, estado de configuración) y cuándo resulta innecesariamente complejo (secciones críticas pequeñas donde el mutex ya funciona bien).

En tercer lugar, tu ruta de bloqueo bloquea indefinidamente. El capítulo 12 presenta las **esperas con tiempo límite**: `mtx_sleep` con un valor `timo` distinto de cero, `cv_timedwait` y `sx_sleep` con tiempos de espera máximos. Aprenderás a acotar el tiempo de espera para que el usuario pueda pulsar Ctrl-C y el driver responda en un intervalo razonable.

Entre los temas concretos que abordará el capítulo 12 se incluyen:

- Las primitivas `cv_wait`, `cv_signal`, `cv_broadcast` y `cv_timedwait`.
- La API de `sx(9)`: `sx_init`, `sx_slock`, `sx_xlock`, `sx_sunlock`, `sx_xunlock`, `sx_downgrade`, `sx_try_upgrade`.
- Los locks de lectura/escritura (`rw(9)`) como variante de spin-mutex hermana de `sx`.
- La gestión de tiempos de espera y la familia de primitivas de tiempo `sbintime_t`.
- La depuración de deadlocks en profundidad, incluida la lista de orden de locks de `WITNESS`.
- Patrones de diseño de orden de locks y las convenciones `LORD_*`.
- Una aplicación concreta: actualizar partes del driver para usar `sx` y `cv` donde corresponda.

No necesitas adelantarte en la lectura. El material del capítulo 11 es preparación suficiente. Trae tu driver de la fase 3 o la fase 5, tu kit de pruebas y tu kernel con `WITNESS` activado. El capítulo 12 arranca justo donde el capítulo 11 terminó.

Una pequeña reflexión de cierre. Acabas de hacer algo poco habitual. La mayoría de los tutoriales sobre drivers enseñan el uso de mutexes como una fórmula: pon un `mtx_lock` aquí y un `mtx_unlock` allá, y el driver funcionará. Tú has hecho algo más difícil: has comprendido qué significa esa fórmula, cuándo es necesaria, por qué es suficiente y cómo verificarlo. Esa comprensión es lo que distingue a quien escribe un driver de quien lo crea con verdadero dominio. Cada capítulo a partir de aquí se construye sobre ella.

Tómate un momento. Aprecia lo lejos que has llegado desde el "Hello, kernel!" inicial del capítulo 7. Después, pasa la página.
