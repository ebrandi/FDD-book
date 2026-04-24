---
title: "Taskqueues y trabajo diferido"
description: "Cómo los drivers de FreeBSD mueven trabajo fuera de contextos que no pueden dormir y lo llevan a un thread que sí puede: encolar tareas de forma segura desde temporizadores e interrupciones, estructurar un taskqueue privado, consolidar ráfagas de trabajo, vaciarlo limpiamente en el detach y depurar los resultados."
partNumber: 3
partName: "Concurrency and Synchronization"
chapter: 14
lastUpdated: "2026-04-18"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 195
language: "es-ES"
---
# Taskqueues y trabajo diferido

## Orientación al lector y objetivos

Al final del Capítulo 13, tu driver `myfirst` adquirió una noción pequeña pero real del tiempo interno. Podía planificar trabajo periódico con `callout(9)`, emitir un latido, detectar un drenaje bloqueado con un watchdog e inyectar bytes sintéticos con una fuente de ticks. Cada callback obedecía una disciplina estricta: adquirir el mutex registrado, comprobar `is_attached`, realizar trabajo breve y acotado, posiblemente rearmar, liberar el mutex. Esa disciplina es lo que hacía seguros los temporizadores. Y también lo que los hacía estrechos.

El Capítulo 14 aborda esa estrechez directamente. Un callback de callout se ejecuta en un contexto que no puede dormir. No puede llamar a `uiomove(9)`, no puede llamar a `copyin(9)`, no puede adquirir un lock dormible de tipo `sx(9)`, no puede asignar memoria con `M_WAITOK`, no puede llamar a `selwakeup(9)` mientras el sleep mutex está retenido. Si el trabajo que un temporizador quiere desencadenar necesita alguna de esas cosas, el temporizador debe delegarlo. La misma limitación se aplica a los manejadores de interrupciones que conocerás en la Parte 4, y a varios otros contextos restringidos que aparecen a lo largo del kernel. El kernel expone un único primitivo para esa delegación: `taskqueue(9)`.

Un taskqueue es, en su forma más simple, una cola de pequeños elementos de trabajo asociada a uno o varios threads del kernel que consumen esa cola. Tu contexto restringido encola una tarea; el thread del taskqueue se despierta y ejecuta el callback de esa tarea en contexto de proceso, donde se aplican las reglas ordinarias del kernel. La tarea puede dormir, puede asignar memoria libremente, puede tocar locks dormibles. El subsistema taskqueue también sabe cómo fusionar ráfagas de encolados, cancelar trabajo pendiente, esperar a que el trabajo en curso termine durante el desmontaje y planificar una tarea para un momento futuro específico. Todo eso cabe en una superficie de API reducida, y todo ello es exactamente lo que necesita un driver que use callouts o interrupciones.

Este capítulo enseña `taskqueue(9)` con el mismo cuidado que el Capítulo 13 dedicó a `callout(9)`. Empezamos con la forma del problema, recorremos la API y luego evolucionamos el driver `myfirst` en cuatro etapas que añaden aplazamiento basado en tareas a la infraestructura de temporizadores existente. Al final, el driver usará un taskqueue privado para sacar de esos contextos todo el trabajo que no puede ejecutarse dentro de un callout o un manejador de interrupciones, y desmontará ese taskqueue en el detach sin filtrar una tarea obsoleta, despertar un thread muerto ni corromper nada.

### Por qué este capítulo merece su propio lugar

Podrías fingir que los taskqueues no existen. En lugar de encolar una tarea, tu callout podría intentar hacer el trabajo diferido directamente, aceptar las consecuencias de que el kernel entre en pánico la primera vez que `WITNESS` detecte un sleep con un spinlock retenido, y esperar que nadie cargue tu driver en un kernel de depuración. Esa no es una opción real, y no vamos a entretenernos con ella. El propósito de este capítulo es darte la alternativa real, la que usa el resto del kernel.

También podrías construir tu propio framework de trabajo diferido con `kproc_create(9)` y una variable de condición propia. Eso es técnicamente posible y en ocasiones inevitable, pero casi siempre es la elección equivocada como primera opción. Un thread propio es un recurso más pesado que una tarea, y renuncia a la observabilidad que se obtiene gratuitamente al usar el framework compartido. `ps(1)`, `procstat(1)`, `dtrace(1)`, `ktr(4)`, las trazas de `wchan` y `ddb(4)` entienden todos los threads de taskqueue. No entienden tu helper de un solo uso a menos que lo instrumentes tú mismo.

Los taskqueues son la respuesta correcta en casi todos los casos en que un driver necesita sacar trabajo de un contexto restringido. El coste de no conocerlos es mayor que el de aprenderlos, y el coste de aprenderlos es modesto: la API es más pequeña que la de `callout(9)`, las reglas son regulares y los patrones se transfieren directamente entre drivers. Una vez que el modelo mental encaje, empezarás a reconocer el patrón en casi todos los drivers bajo `/usr/src/sys/dev/`.

### Dónde dejó el driver el Capítulo 13

Un breve punto de control antes de continuar. El Capítulo 14 extiende el driver producido al final de la Etapa 4 del Capítulo 13, no ninguna etapa anterior. Si alguno de los puntos siguientes te resulta inseguro, vuelve al Capítulo 13 antes de comenzar este.

- Tu driver `myfirst` compila sin errores y se identifica como versión `0.7-timers`.
- Tiene tres callouts declarados en el softc: `heartbeat_co`, `watchdog_co` y `tick_source_co`.
- Cada callout se inicializa con `callout_init_mtx(&co, &sc->mtx, 0)` en `myfirst_attach` y se drena con `callout_drain` en `myfirst_detach` después de que `is_attached` ha sido borrado.
- Cada callout tiene un sysctl de intervalo (`heartbeat_interval_ms`, `watchdog_interval_ms`, `tick_source_interval_ms`) que por defecto es cero (deshabilitado) y refleja las transiciones de habilitación y deshabilitación en su manejador.
- La ruta de detach sigue el orden documentado: rechazar si hay `active_fhs`, borrar `is_attached`, hacer broadcast en ambas cvs, drenar `selinfo`, drenar todos los callouts, destruir los dispositivos, liberar los sysctls, destruir cbuf, contadores, cvs, el sx y el mutex.
- Tu `LOCKING.md` tiene una sección Callouts que nombra cada callout, su callback, su lock y su tiempo de vida.
- El kit de estrés del Capítulo 13 (los probadores del Capítulo 12 más los ejercitadores de temporizadores del Capítulo 13) compila y se ejecuta sin errores con `WITNESS` e `INVARIANTS` habilitados.

Ese driver es la base que extendemos. El Capítulo 14 no rehace ninguna de esas estructuras. Añade una nueva columna al softc, una nueva llamada de inicialización, una nueva llamada de desmontaje y pequeños cambios en los tres callbacks de callout y en uno o dos lugares más donde el driver se beneficiaría de mover trabajo fuera de un contexto restringido.

### Lo que aprenderás

Al final de este capítulo serás capaz de:

- Explicar por qué determinado trabajo no puede realizarse dentro de un callback de callout o un manejador de interrupciones, y reconocer las operaciones que obligan a delegar a un contexto diferente.
- Describir las tres cosas que es un taskqueue: una cola de `struct task`, un thread (o un pequeño grupo de threads) que consume la cola, y una política de encolado y despacho que une ambos elementos.
- Inicializar una tarea con `TASK_INIT(&sc->foo_task, 0, myfirst_foo_task, sc)`, entender qué significa cada argumento y colocar la llamada en la etapa correcta del attach.
- Encolar una tarea con `taskqueue_enqueue(tq, &sc->foo_task)` desde un callback de callout, desde un manejador de sysctl, desde una ruta de lectura o escritura, o desde cualquier otro código del driver donde el aplazamiento sea la respuesta correcta.
- Elegir entre los taskqueues de sistema predefinidos (`taskqueue_thread`, `taskqueue_swi`, `taskqueue_swi_giant`, `taskqueue_fast`, `taskqueue_bus`) y un taskqueue privado que crees con `taskqueue_create` y rellenes con `taskqueue_start_threads`.
- Entender el contrato de fusión: cuando se encola una tarea que ya está pendiente, el kernel incrementa `ta_pending` en lugar de enlazarla dos veces, y el callback recibe el contador final de pendientes para que pueda procesar en lote.
- Usar la variante `struct timeout_task` con `taskqueue_enqueue_timeout` para planificar una tarea para un momento futuro específico, y drenarla correctamente con `taskqueue_drain_timeout`.
- Bloquear y desbloquear un taskqueue alrededor de pasos delicados de apagado, y poner un taskqueue en quiescencia cuando necesitas garantizar que ninguna tarea está ejecutándose en ningún punto de la cola.
- Drenar cada tarea que posee el driver en el detach, en el orden correcto, sin provocar un deadlock contra los callouts y las cvs que ya drenas.
- Separar las responsabilidades del código de temporizadores y del código de tareas dentro del código fuente del driver, de modo que un nuevo lector pueda determinar qué trabajo se ejecuta en qué contexto con solo mirar el archivo.
- Reconocer y aplicar el patrón de driver de red que usa `epoch(9)` y grouptaskqueues para rutas de lectura sin locks, al nivel de saber cuándo usarlos y cuándo no.
- Depurar un driver que usa taskqueue con `procstat -t`, `ps ax`, `dtrace -l` y `ktr(4)`, e interpretar lo que cada herramienta te muestra.
- Etiquetar el driver como versión `0.8-taskqueues` y documentar la política de aplazamiento en `LOCKING.md` para que la próxima persona que herede el driver pueda leerla.

Es una lista larga. La mayoría de los elementos se construyen unos sobre otros, por lo que la progresión dentro del capítulo es el camino natural.

### Lo que este capítulo no cubre

Varios temas adyacentes se aplazan explícitamente para que el Capítulo 14 mantenga el foco.

- **Los manejadores de interrupciones como tema principal.** La Parte 4 introduce `bus_setup_intr(9)` y la separación entre los manejadores `FILTER` e `ITHREAD`. El Capítulo 14 menciona el contexto de interrupción al explicar por qué el trabajo diferido importa, y los patrones que enseña se transfieren directamente de los callouts a los manejadores de interrupciones reales, pero la API de interrupciones en sí es tarea de la Parte 4.
- **La historia completa de variables de condición y semáforos.** El Capítulo 15 amplía el vocabulario de sincronización con semáforos contadores, bloqueo interrumpible por señal y sincronizaciones entre componentes que coordinan temporizadores, tareas y threads de usuario. El Capítulo 14 usa la infraestructura de cv existente tal como está y no añade nuevos primitivos de sincronización más allá de lo que los propios taskqueues aportan.
- **Cobertura profunda de grouptaskqueues e iflib.** La familia `taskqgroup` existe y este capítulo explica cuándo es la respuesta correcta, pero la historia completa pertenece a los drivers de red de la Parte 6 (Capítulo 28). La introducción aquí es intencionalmente ligera.
- **Las rutas de finalización de DMA impulsadas por hardware.** Un taskqueue es el lugar natural para finalizar una transferencia DMA después de que el hardware haya señalado su finalización mediante una interrupción, y mencionamos ese patrón, pero la mecánica de la gestión de buffers DMA espera hasta los capítulos de bus-space y DMA.
- **Workloops, kthreads para sondeo por CPU y los ganchos avanzados del planificador.** Son partes reales del panorama de trabajo diferido del kernel, pero son especializadas y un driver raramente las necesita. Cuando importan, los capítulos que las necesitan las introducen.

Mantenerse dentro de esos límites conserva la coherencia del modelo mental del capítulo. El Capítulo 14 te da una buena herramienta, enseñada con cuidado. Los capítulos posteriores te dan las herramientas adyacentes y los contextos de hardware reales que justifican su uso.

### Tiempo estimado de dedicación

- **Solo lectura**: unas tres horas. La superficie de API es más pequeña que la de `callout(9)`, pero la interacción entre un taskqueue y el resto de la historia de locking del driver tarda un poco en asentarse.
- **Lectura más escritura de los ejemplos trabajados**: de seis a ocho horas en dos sesiones. El driver evoluciona en cuatro etapas; cada etapa modifica aproximadamente una preocupación.
- **Lectura más todos los laboratorios y desafíos**: de diez a catorce horas en tres o cuatro sesiones, incluyendo el tiempo necesario para observar los threads del taskqueue bajo carga con `procstat`, `dtrace` y una carga de estrés.

Si las reglas de ordenación al inicio de la Sección 4 te resultan desorientadoras, eso es normal. La secuencia de detach con callouts, cvs, manejadores sel y ahora tareas tiene cuatro piezas que deben componerse. Recorreremos el orden una vez, lo enunciaremos, lo justificaremos y lo reutilizaremos.

### Requisitos previos

Antes de comenzar este capítulo, confirma:

- Tu código fuente del driver coincide con la Etapa 4 del Capítulo 13 (`stage4-final`). El punto de partida asume los tres callouts, los tres sysctls de intervalo, la disciplina `is_attached` en cada callback y el orden de detach documentado.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con `/usr/src` en disco y coincidiendo con el kernel en ejecución. Varias de las referencias al código fuente de este capítulo son cosas que deberías abrir y leer realmente.
- Un kernel de depuración con `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` está construido, instalado y arrancando sin problemas.
- El Capítulo 13 te resulta cómodo. El callout consciente del lock, la disciplina `is_attached` en los callbacks y el orden de detach son conocimiento asumido aquí.
- Has ejecutado el kit de estrés del Capítulo 13 al menos una vez con todos los temporizadores habilitados y has visto que pasa sin problemas.

Si alguno de los puntos anteriores no está del todo sólido, corregirlo ahora es una mejor inversión que avanzar a la fuerza hasta el Capítulo 14 e intentar depurar desde una base que no está firme. Los patrones del Capítulo 14 están diseñados específicamente para combinarse con los patrones del Capítulo 13; partir de un driver del Capítulo 13 que no está del todo correcto hace que cada paso del Capítulo 14 sea más difícil.

### Cómo aprovechar al máximo este capítulo

Tres hábitos darán sus frutos muy rápido.

En primer lugar, ten a mano `/usr/src/sys/kern/subr_taskqueue.c` y `/usr/src/sys/sys/taskqueue.h`. El archivo de cabecera es breve, unas doscientas líneas, y es el resumen canónico de la API. El archivo de implementación tiene unas mil líneas, bien comentado, y leer `taskqueue_run_locked` con atención compensa la primera vez que tengas que razonar sobre qué significa realmente el contador `pending` de una tarea. Diez minutos con el archivo de cabecera ahora te dan diez horas de confianza después.

En segundo lugar, ejecuta todos los cambios de código bajo `WITNESS`. El subsistema de taskqueue tiene su propio lock (un spin mutex o un sleep mutex, según si la cola se creó con `taskqueue_create` o con `taskqueue_create_fast`), y este interactúa con los locks de tu driver de maneras que `WITNESS` comprende. Una adquisición de lock mal colocada dentro de una tarea callback es exactamente el tipo de error que `WITNESS` detecta al instante en un kernel de depuración y que corrompe en silencio en un kernel de producción. No ejecutes el código del Capítulo 14 en el kernel de producción hasta que supere las pruebas en el kernel de depuración.

En tercer lugar, escribe los cambios a mano. El código de acompañamiento en `examples/part-03/ch14-taskqueues-and-deferred-work/` es la versión canónica, pero la memoria muscular vale más que la lectura. El capítulo introduce ediciones pequeñas e incrementales; refleja ese ritmo de pasos pequeños en tu propia copia del driver. Cuando el entorno de prueba supere una etapa, haz commit de esa versión y continúa; si un paso falla, el commit anterior es tu punto de recuperación.

### Hoja de ruta del capítulo

Las secciones, en orden, son:

1. Por qué usar trabajo diferido en un driver. La forma del problema: qué no puede hacerse en callouts, interrupciones y otros contextos restringidos; los casos reales que obligan a la transferencia.
2. Introducción a `taskqueue(9)`. Las estructuras, la API, las colas predefinidas y la comparación con callouts.
3. Diferir trabajo desde un temporizador o una interrupción simulada. La primera refactorización, Etapa 1: añadir una única tarea que un callout encola.
4. Configuración y limpieza del taskqueue. Etapa 2: crear un taskqueue privado, conectar la secuencia de detach y verificar el resultado con `WITNESS`.
5. Priorización y fusión de trabajo. Etapa 3: usar el comportamiento de fusión de `ta_pending` deliberadamente para el procesamiento por lotes, introducir `taskqueue_enqueue_timeout` para tareas programadas y debatir sobre prioridades.
6. Patrones reales con taskqueues. Un recorrido por los patrones que se repiten en drivers reales de FreeBSD, presentados como pequeñas recetas que puedes incorporar a tu propio código.
7. Depuración de taskqueues. Las herramientas, los errores más comunes y un ejercicio guiado de romper y reparar sobre un escenario realista.
8. Refactorización y versionado. Etapa 4: consolidar el driver en un todo coherente, actualizar la versión a `0.8-taskqueues` y ampliar `LOCKING.md`.

Tras las ocho secciones principales, se tratan `epoch(9)`, los grouptaskqueues y los taskqueues por CPU a un nivel introductorio y ligero; después vienen los laboratorios prácticos, los ejercicios de desafío, una referencia de resolución de problemas, una sección de cierre y un puente hacia el Capítulo 15.

Si es tu primera lectura, avanza de forma lineal y realiza los laboratorios en orden. Si estás repasando, las Secciones 4, 6 y 8 son independientes y se leen bien en una sola sesión.



## Sección 1: ¿Por qué usar trabajo diferido en un driver?

El Capítulo 13 concluyó con un driver cuyos callouts realizaban todo el trabajo que la función callback podía hacer con seguridad. El heartbeat imprimía un informe de estado de una línea. El watchdog registraba un único contador e imprimía opcionalmente un aviso. La fuente de ticks escribía un solo byte en el buffer circular y señalaba una variable de condición. Cada callback tardaba microsegundos, mantenía el mutex durante esos microsegundos y retornaba. Eso es el contrato del callout en su mejor versión: pequeño, predecible, consciente del lock y de bajo coste.

El trabajo real de un driver no siempre cabe dentro de ese contrato. Algunas tareas quieren ejecutarse al mismo ritmo que el temporizador que detecta la necesidad, pero quieren hacer cosas que el temporizador no puede hacer con seguridad. Otras tareas son activadas por un contexto restringido diferente (un manejador de interrupción, por ejemplo, o una rutina de filtro en la pila de red) pero presentan el mismo problema de "aquí no se puede hacer eso". Esta sección traza el contorno del problema: qué no se puede hacer en contextos restringidos, qué tipos de trabajo desean diferir los drivers y qué opciones ofrece el kernel para llevar ese trabajo a un lugar donde pueda ejecutarse realmente.

### El contrato del callout, revisado

Una breve relectura de la regla del callout, porque la historia del taskqueue gira en torno a las cosas que los callouts no pueden hacer.

Una función callback de `callout(9)` se ejecuta en uno de dos modos. El modo predeterminado es el despacho desde el thread del callout: el thread del callout dedicado del kernel para ese CPU se activa en un límite del reloj de hardware, recorre la rueda de callouts, encuentra los callbacks cuyo plazo ha llegado y los llama uno a uno. El modo alternativo, `C_DIRECT_EXEC`, ejecuta el callback directamente dentro del propio manejador de interrupciones del reloj de hardware. Tu driver raramente elige la alternativa; el modo predeterminado es el que usan casi todos los drivers.

En ambos modos, el callback se ejecuta con el lock registrado del callout retenido (para la familia `callout_init_mtx`) y no puede cruzar ciertos límites de contexto. No puede dormir. Dormir significa llamar a cualquier primitiva que pueda desplanificar el thread y bloquearlo durante un período indefinido esperando una condición. `mtx_sleep`, `cv_wait`, `msleep`, `sx_slock`, `malloc(..., M_WAITOK)`, `uiomove`, `copyin` y `copyout` duermen todas en su camino lento. `selwakeup(9)` no duerme en sí misma, pero adquiere el mutex por selinfo, que puede ser el mutex equivocado para el contexto en el que se ejecuta el callout, y la práctica habitual es llamarla sin ningún mutex del driver retenido. Ninguna de esas llamadas pertenece al interior de una función callback de callout.

Estas son reglas estrictas a nivel del kernel. `INVARIANTS` y `WITNESS` detectan muchas de las violaciones en tiempo de ejecución. Algunas de ellas corrumpen el kernel en silencio, de maneras difíciles de depurar después. En todos los casos, un driver que quiera el efecto de una de esas operaciones debe realizar la llamada desde un contexto que lo permita. Ese contexto es el que proporciona un taskqueue.

El resto de esta sección amplía la misma observación desde diferentes ángulos: qué tipo de trabajo desea diferir un driver, por qué los contextos restringidos merecen sus restricciones y qué mecanismos de FreeBSD compiten por el trabajo.

### Contextos restringidos, no solo callouts

El callout es el primer contexto restringido con el que se encuentra un driver al estilo `myfirst`, pero no es el único. Hay varios otros lugares en el kernel donde se ejecuta código que no puede dormir o que no puede realizar ciertos tipos de asignación. Un driver que quiera actuar desde cualquiera de ellos se enfrenta a la misma decisión de "diferirlo".

**Filtros de interrupciones de hardware.** Cuando un dispositivo real genera una interrupción, el kernel ejecuta una rutina de filtro de forma sincrónica en el CPU que recibió la interrupción. Los filtros no pueden dormir, no pueden adquirir sleep mutexes y no pueden llamar a la mayoría de las APIs normales del kernel. Normalmente se dividen en un pequeño filtro (leer un registro de estado, decidir si la interrupción es nuestra) que se ejecuta en contexto de hardware, más un ithread asociado que realiza el trabajo real en un contexto de thread completo. Encontraremos la división exacta filtro/ithread cuando la Parte 4 introduzca `bus_setup_intr(9)`, pero la lección estructural ya está clara: los filtros de interrupción son otro lugar donde el trabajo debe transferirse a algún otro sitio.

**Rutas de entrada de paquetes de red.** Algunas partes de la ruta de recepción de `ifnet(9)` se ejecutan bajo la protección de `epoch(9)`, lo que restringe los tipos de adquisiciones de lock y operaciones de sleep que son seguras. Los drivers de red con frecuencia encolan una tarea cuando quieren realizar un trabajo no trivial que pertenece al contexto de proceso.

**Callbacks de `taskqueue_fast` y `taskqueue_swi`.** Incluso cuando ya estás dentro de una tarea callback, si la tarea se ejecuta en una cola respaldada por spin mutex (`taskqueue_fast`) o en una cola de interrupciones de software (`taskqueue_swi`), se aplica la misma regla de no dormir que en el contexto de origen. Las tareas callback en el `taskqueue_thread` predeterminado no tienen esa restricción; se ejecutan en contexto de thread completo y pueden dormir libremente. Esta distinción es importante y volveremos a ella en la Sección 2.

**Secciones de lectura de `epoch(9)`.** Un camino de código delimitado por `epoch_enter()` y `epoch_exit()` no puede dormir. Los drivers de red usan este patrón ampliamente para hacer que las rutas de lectura sean libres de lock; el trabajo del lado de escritura se difiere a fuera del epoch. El Capítulo 14 cubre epoch a un nivel introductorio en la parte "Temas adicionales" posterior del capítulo.

El hilo conductor de todos esos contextos es que algo en el entorno circundante prohíbe las operaciones en contexto de thread. Ese "algo" varía (un spin lock, un contexto de filtro, una sección de epoch, un despacho de interrupción de software), pero el remedio es el mismo: encolar una tarea para que la ejecute después un thread que no esté en el contexto restringido.

### Razones reales para diferir

Un breve repaso de los tipos de trabajo que los drivers desplazan fuera de los contextos restringidos. Reconocer estos patrones ahora te proporciona vocabulario para los que desarrollará la Sección 6.

**`selwakeup(9)` no trivial.** `selwakeup` es la llamada del kernel para notificar a todos los waiters de select/poll. La sabiduría tradicional dice que debe llamarse sin ningún mutex del driver retenido, y nunca desde un contexto que tenga un spin lock retenido. Una función callback de callout retiene un mutex; un filtro de interrupción no retiene nada, pero se encuentra en un lugar inadecuado. Los drivers que quieren notificar a los pollers desde esos contextos suelen encolar una tarea cuyo único trabajo es llamar a `selwakeup`.

**`copyin` y `copyout` tras un evento de hardware.** Después de que una interrupción señale que una transferencia DMA ha completado, el driver puede querer copiar datos hacia o desde un buffer en el espacio de usuario cuya dirección se registró con un ioctl anterior. Ni `copyin` ni `copyout` son legales en contexto de interrupción. El driver programa una tarea cuya callback realiza la copia en contexto de proceso.

**Reconfiguración que requiere un lock dormible.** La configuración del driver suele estar protegida por un lock `sx(9)`, que puede dormir. Un callout o una interrupción no pueden adquirir un lock dormible directamente. Si una decisión basada en un temporizador implica un cambio de configuración, el temporizador encola una tarea; la tarea adquiere el lock sx y realiza el cambio.

**Reintentar una operación fallida tras un backoff.** Las operaciones de hardware a veces fallan de forma transitoria. La respuesta sensata es esperar un intervalo y reintentar. Un manejador de interrupciones no puede bloquearse; encola un `timeout_task` con un retardo igual al intervalo de backoff. La tarea de timeout se ejecuta después en contexto de thread, reintenta la operación y, si falla de nuevo, se reprograma con un retardo mayor.

**Registro de un evento no trivial.** El `printf(9)` del kernel es sorprendentemente tolerante con contextos extraños, pero `log(9)` y sus funciones relacionadas no lo son. Un driver que quiere emitir un diagnóstico de varias líneas desde un contexto de interrupción escribe el mínimo imprescindible en el manejador (un indicador, un incremento de contador) y programa una tarea para realizar el registro real después.

**Vaciado o reconfiguración de una cola de hardware larga.** Un driver de red que detecta un bloqueo de cabeza de línea puede querer recorrer su anillo de transmisión, liberar los descriptores completados y reiniciar el estado por descriptor. El trabajo es acotado pero no trivial. Hacerlo en línea en la ruta de interrupción monopoliza un CPU en un contexto inadecuado. Hacerlo en una tarea permite que la interrupción retorne inmediatamente y que el trabajo real suceda en un thread.

**Desmontaje diferido.** Cuando un driver se desconecta mientras algún objeto tiene referencias pendientes, el driver no puede liberar el objeto inmediatamente. Un patrón habitual: diferir la liberación a una tarea que se ejecuta después de que se sabe que el contador de referencias es cero, o después de un período de gracia suficientemente largo para que se agoten las referencias en vuelo.

Todos esos casos comparten la misma estructura: el contexto restringido detecta una necesidad, posiblemente registra una pequeña cantidad de estado y encola una tarea. La tarea se ejecuta después en contexto de thread, realiza el trabajo real y, opcionalmente, se vuelve a encolar o programa una continuación.

### Polling frente a ejecución diferida

Una pregunta razonable llegados a este punto: si el contexto restringido no puede realizar el trabajo, ¿por qué no organizar ese trabajo para que ocurra en un lugar completamente ajeno a ese contexto restringido? ¿Por qué no tener un único kernel thread dedicado que compruebe periódicamente si hay algo que hacer y se despierte cuando detecte un estado que requiera acción?

Eso es, en la práctica, lo que hace un taskqueue. Un thread de taskqueue duerme hasta que hay trabajo disponible y se despierta para procesarlo. La diferencia entre un taskqueue y un thread de polling escrito a mano es que el framework de taskqueue resuelve toda la logística por ti. Encolar una tarea es una única operación atómica. La estructura de tarea contiene directamente el callback y el contexto, de modo que no tienes que diseñar un tipo "entrada de cola de trabajo". La coalescencia de encolados redundantes es automática. Drenar una tarea es una única llamada. El desmontaje también se reduce a una sola llamada. La observabilidad mediante las herramientas estándar se obtiene de serie.

Un thread de polling escrito a mano puede realizar el mismo trabajo y, en casos extremos, es la elección correcta (por ejemplo, si el trabajo tiene restricciones de tiempo real estrictas, o si forma parte de un subsistema que necesita una prioridad dedicada). Para el trabajo habitual de un driver, prescindir de `taskqueue(9)` es casi siempre un error.

Una pregunta distinta pero relacionada: ¿por qué no crear un nuevo kernel thread para cada operación diferida? Eso es extremadamente costoso: crear un thread lleva tiempo, asigna un stack completo del kernel y entrega el nuevo thread al planificador. Para trabajo que ocurre de forma repetida, el diseño sensato es reutilizar un thread, que es exactamente lo que proporciona un taskqueue. Para trabajo que ocurre una sola vez, podrías usar `kproc_create(9)` y hacer que el nuevo thread finalice al terminar, pero incluso en ese caso un taskqueue con `taskqueue_drain` suele ser más sencillo y casi igual de económico.

### Las soluciones de FreeBSD

El kernel ofrece una pequeña familia de mecanismos para trabajo diferido. El capítulo 14 se centra en uno de ellos (`taskqueue(9)`) y menciona los demás con el nivel de detalle adecuado para que un desarrollador de drivers sepa cuándo es apropiado cada uno. Un breve recorrido por ahora; las secciones siguientes profundizan en cada uno a medida que resulte relevante.

**`taskqueue(9)`.** Una cola de entradas `struct task` y uno o más threads del kernel (o un contexto de interrupción por software) que consumen la cola. La opción predominante para el trabajo diferido en drivers. Se trata en profundidad a lo largo de este capítulo.

**`epoch(9)`.** Un mecanismo de sincronización de lectura sin locks que los drivers de red utilizan para permitir que los lectores recorran estructuras de datos compartidas sin necesidad de locks. Los escritores difieren la limpieza mediante `epoch_call` o `epoch_wait`. No es un mecanismo de trabajo diferido de uso general para cualquier driver, pero es lo suficientemente importante como para introducirlo más adelante en este capítulo, de modo que lo reconozcas cuando lo veas en el código de drivers de red.

**Grouptaskqueues.** Una variación escalable de los taskqueues en la que un grupo de tareas relacionadas comparte un pool de threads de trabajo por CPU. Los drivers de red hacen un uso intensivo de esto; la mayoría de los demás drivers no. Se introduce más adelante en este capítulo.

**`kproc_create(9)` / `kthread_add(9)`.** Creación directa de un thread del kernel. Resulta útil cuando el trabajo diferido es un bucle de larga duración que no encaja en el molde de «tarea corta», y cuando el trabajo merece una prioridad dedicada o afinidad de CPU. Casi siempre es excesivo para un diferimiento sencillo; se prefiere un taskqueue.

**Manejadores SWI (interrupción por software) dedicados mediante `swi_add(9)`.** Una forma de registrar una función que se ejecuta en un contexto de interrupción por software. Los taskqueues del sistema (`taskqueue_swi`, `taskqueue_swi_giant`, `taskqueue_fast`) se construyen sobre este mecanismo. El código de los drivers raramente llama a `swi_add` directamente; la capa del taskqueue es la abstracción correcta.

**El propio callout, reprogramado a «cero a partir de ahora».** Un patrón que no funciona: no puedes «escapar» del contexto de callout programando otro callout, porque el siguiente callout sigue ejecutándose en contexto de callout. Reconocer que esto es un callejón sin salida ya es en sí mismo útil. Los callouts programan el momento; los taskqueues proporcionan el contexto.

Para el resto del capítulo 14, salvo que se indique lo contrario, «diferir a una tarea» o «encolar una tarea» significa «encolar un `struct task` en un `struct taskqueue`».

### Cuándo diferir no es la respuesta correcta

El diferimiento es una herramienta, no el comportamiento predeterminado. Hay varias situaciones en las que conviene más hacer el trabajo en el momento que diferirlo.

**El trabajo es genuinamente corto y seguro para el contexto actual.** Registrar una estadística de una línea con `device_printf(9)` desde un callout está bien. También lo es incrementar un contador, o señalizar un cv. Diferir estas trivialidades a una tarea cuesta más que hacerlas. Solo difiere cuando el trabajo realmente no pertenece al contexto actual.

**El tiempo importa y el diferimiento introduce varianza.** Una tarea no se ejecuta al instante. Se ejecuta cuando el thread del taskqueue sea planificado la próxima vez, lo que puede estar a microsegundos o milisegundos de distancia, dependiendo de la carga del sistema. Si el trabajo tiene requisitos estrictos de temporización (como confirmar un evento de hardware dentro de un plazo determinado, por ejemplo), el diferimiento puede no cumplir ese plazo. Para ese tipo de trabajo necesitas un mecanismo más rápido (una confirmación a nivel de hardware, un callout `C_DIRECT_EXEC`, o un SWI) o un diseño diferente.

**El diferimiento añadiría un salto sin ningún beneficio.** Si el único trabajo de tu manejador de interrupciones ya es seguro de realizar en contexto de interrupción, añadir un viaje de ida y vuelta con una tarea duplica la latencia sin mejorar nada. Solo difiere las partes del trabajo que realmente necesiten ser diferidas.

**El trabajo requiere un thread específico.** Si el trabajo necesita ejecutarse como un proceso de usuario específico (por ejemplo, para utilizar la tabla de descriptores de archivo de ese proceso), un thread genérico de taskqueue no es el lugar adecuado. Esa situación es poco frecuente en los drivers, pero existe.

Para todo lo demás, el diferimiento mediante un taskqueue es la respuesta correcta, y el resto del capítulo trata sobre cómo hacerlo bien.

### Un ejemplo desarrollado: por qué la fuente de tick no puede despertar a los pollers

Un ejemplo concreto del driver del capítulo 13, sobre el que vale la pena detenerse, porque es el primer lugar donde el capítulo 14 introduce un cambio real.

La callback `tick_source` del capítulo 13, extraída de `stage4-final/myfirst.c`, tiene este aspecto:

```c
static void
myfirst_tick_source(void *arg)
{
        struct myfirst_softc *sc = arg;
        size_t put;
        int interval;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        if (cbuf_free(&sc->cb) > 0) {
                put = cbuf_write(&sc->cb, &sc->tick_source_byte, 1);
                if (put > 0) {
                        counter_u64_add(sc->bytes_written, put);
                        cv_signal(&sc->data_cv);
                        /* selwakeup omitted: cannot be called from a
                         * callout callback while sc->mtx is held. */
                }
        }
        ...
}
```

El comentario al final no es hipotético. `selwakeup(9)` adquiere el mutex propio de selinfo y puede llamar al subsistema kqueue, lo que no es seguro hacer dentro de una callback de callout que ya retiene un mutex diferente del driver. Un programa de usuario que espera en `/dev/myfirst` mediante `select(2)`/`poll(2)` para detectar disponibilidad de lectura, por tanto, no recibe notificación cuando la fuente de tick deposita un byte. El programa solo se despierta cuando alguna otra ruta llama a `selwakeup`, por ejemplo cuando llega un `write(2)` de otro thread.

Este es un error real en el driver del capítulo 13. Lo dejamos sin corregir en el capítulo 13 porque su corrección requería una primitiva que aún no habíamos introducido. El capítulo 14 introduce esa primitiva y corrige el error.

La corrección es pequeña. Añade un `struct task` al softc. Inicialízalo en attach. En lugar de omitir `selwakeup` de la callback `tick_source`, encola la tarea; esta se ejecuta en contexto de thread, sin ningún mutex del driver retenido, y llama a `selwakeup` de forma segura. Drena la tarea en detach después de haber limpiado `is_attached`, antes de liberar el selinfo.

Recorreremos cada paso de ese cambio en la sección 3. Por ahora, lo importante es que el cambio es mecánico y su necesidad no es artificial. El primer trabajo real del capítulo 14 es darte la herramienta que este tipo de error necesita.

### Un pequeño modelo mental

Una imagen útil, que se ofrece aquí una sola vez y a la que se hará referencia más adelante.

Imagina tu driver compuesto por dos tipos de código. El primer tipo es el código que se ejecuta porque alguien lo ha solicitado: el manejador de `read(2)`, el de `write(2)`, el de `ioctl(2)`, los manejadores de sysctl, y los manejadores de apertura y cierre. Ese código se ejecuta en contexto de thread, con las reglas ordinarias, y puede dormir, asignar memoria y tocar cualquier lock. Llámalo «código de contexto de thread».

El segundo tipo es el código que se ejecuta porque el tiempo o el hardware lo ha ordenado: una callback de callout, un filtro de interrupción, una lectura protegida por epoch. Ese código se ejecuta en un contexto restringido, con un conjunto más limitado de reglas, y debe mantener su trabajo breve y sin dormir. Llámalo «código de contexto de borde».

La mayor parte del trabajo real pertenece al código de contexto de thread. La mayor parte de lo que el código de contexto de borde realmente necesita hacer es: detectar el evento, registrar una pequeña cantidad de estado, y pasar el trabajo al código de contexto de thread. Un taskqueue es el mecanismo de traspaso. La callback de la tarea se ejecuta en código de contexto de thread, porque el thread del taskqueue está en contexto de thread. Todo lo que hace la callback sigue las reglas ordinarias.

Este modelo mental te permite leer cada sección posterior como variaciones sobre una única idea: el código de contexto de borde detecta, el código de contexto de thread actúa, y el taskqueue es la costura entre ambos. Una vez que veas el driver de esa manera, el resto del capítulo son detalles de ingeniería.

### Cerrando la sección 1

Cierto trabajo debe ejecutarse en un contexto restringido (callouts, filtros de interrupciones, secciones de epoch). Las reglas de esos contextos prohíben dormir, realizar asignaciones de memoria costosas, adquirir sleepable locks, y varias otras operaciones comunes. Los drivers con responsabilidades reales necesitan con frecuencia realizar exactamente esas operaciones en respuesta a eventos que llegan en contextos restringidos. El remedio es encolar una tarea y dejar que un thread de trabajo realice el trabajo real en un contexto donde las reglas lo permiten.

El kernel expone ese patrón como `taskqueue(9)`. La API es pequeña, los patrones son regulares, y la herramienta se combina limpiamente con los callouts y las primitivas de sincronización que ya conoces. La sección 2 introduce la primitiva.



## Sección 2: Introducción a `taskqueue(9)`

`taskqueue(9)` es, como la mayoría de los subsistemas bien maduros del kernel, una API pequeña sobre una implementación cuidadosa. Las estructuras de datos son concisas, el ciclo de vida es regular (init, enqueue, run, drain, free), y las reglas son lo suficientemente explícitas como para que puedas verificar tu uso leyendo el código fuente. Esta sección recorre las estructuras, nombra la API, enumera las colas predefinidas que el kernel proporciona gratuitamente, y compara los taskqueues con los callouts del capítulo 13 para que puedas ver cuándo es la herramienta adecuada en cada caso.

### La estructura task

La estructura de datos está en `/usr/src/sys/sys/_task.h`:

```c
typedef void task_fn_t(void *context, int pending);

struct task {
        STAILQ_ENTRY(task) ta_link;     /* (q) link for queue */
        uint16_t ta_pending;            /* (q) count times queued */
        uint8_t  ta_priority;           /* (c) Priority */
        uint8_t  ta_flags;              /* (c) Flags */
        task_fn_t *ta_func;             /* (c) task handler */
        void    *ta_context;            /* (c) argument for handler */
};
```

Los campos se dividen en dos grupos. Los campos `(q)` son gestionados por el taskqueue bajo su propio lock interno; el código del driver no los toca directamente. Los campos `(c)` son constantes tras la inicialización; el código del driver los establece una vez mediante un inicializador y nunca los modifica de nuevo.

`ta_link` es el enlace de lista que se usa cuando la tarea está encolada. No se usa cuando la tarea está inactiva.

`ta_pending` es el contador de coalescencia. Cuando la tarea se encola por primera vez, pasa de cero a uno y la tarea se coloca en la lista. Si se encola de nuevo antes de que se ejecute la callback, el contador simplemente se incrementa y la tarea permanece en la lista una sola vez. Cuando la callback finalmente se ejecuta, el recuento de pendientes final se pasa como segundo argumento a la callback, y el contador se restablece a cero. El peor error que puedes cometer con `ta_pending` es asumir que una tarea se ejecutará N veces si la encolas N veces; no lo hará. Se ejecutará una vez y la callback sabrá que fue encolada N veces. La sección 5 trata las implicaciones de diseño en detalle.

`ta_priority` ordena las tareas dentro de una sola cola. Las tareas de mayor prioridad se ejecutan antes que las de menor prioridad. Para la mayoría de los drivers, el valor es cero (prioridad ordinaria) y la cola es efectivamente FIFO.

`ta_flags` es un pequeño campo de bits. El kernel lo usa para registrar si la tarea está actualmente encolada y, para las tareas de red, si la tarea debe ejecutarse dentro del epoch de red. El código del driver no lo toca después de que `TASK_INIT` o `NET_TASK_INIT` lo hayan establecido.

`ta_func` es la función de callback. Su firma es `void (*)(void *context, int pending)`. El primer argumento es lo que hayas almacenado en `ta_context` en el momento de la inicialización; el segundo es el recuento de coalescencia.

`ta_context` es el argumento de la callback. Para una tarea de driver de dispositivo, casi siempre es el puntero al softc.

La estructura ocupa 32 bytes en amd64, más o menos el relleno. Incorporas una por cada patrón de trabajo diferido en tu softc. Un driver con tres rutas de diferimiento tiene tres miembros `struct task`.

### Inicialización de una tarea

La macro canónica es `TASK_INIT`, en `/usr/src/sys/sys/taskqueue.h`:

```c
#define TASK_INIT_FLAGS(task, priority, func, context, flags) do {      \
        (task)->ta_pending = 0;                                         \
        (task)->ta_priority = (priority);                               \
        (task)->ta_flags = (flags);                                     \
        (task)->ta_func = (func);                                       \
        (task)->ta_context = (context);                                 \
} while (0)

#define TASK_INIT(t, p, f, c)    TASK_INIT_FLAGS(t, p, f, c, 0)
```

Una llamada típica desde la rutina attach de un driver tiene este aspecto:

```c
TASK_INIT(&sc->selwake_task, 0, myfirst_selwake_task, sc);
```

Los argumentos se leen como: «inicializa esta tarea, con prioridad ordinaria cero, para ejecutar `myfirst_selwake_task(sc, pending)` cuando se dispare». Ese es todo el ritual de inicialización. No existe una llamada «destroy» correspondiente; una tarea vuelve a estar inactiva cuando termina su callback, y queda fuera de ámbito cuando el softc que la contiene se libera.

Para las tareas en la ruta de red existe una variante, `NET_TASK_INIT`, que establece el indicador `TASK_NETWORK` para que el taskqueue sepa que debe ejecutar la callback dentro del epoch `net_epoch_preempt`:

```c
#define NET_TASK_INIT(t, p, f, c) TASK_INIT_FLAGS(t, p, f, c, TASK_NETWORK)
```

A menos que estés escribiendo un driver de red, `TASK_INIT` es la que debes usar. El capítulo 14 usa `TASK_INIT` en todo el texto y vuelve a `NET_TASK_INIT` únicamente en la sección de «Temas adicionales».

### La estructura taskqueue, desde el punto de vista del driver

Desde el punto de vista de un driver, un taskqueue es un `struct taskqueue *`. El puntero es o bien un global predefinido (`taskqueue_thread`, `taskqueue_swi`, `taskqueue_bus`, etc.) o bien uno que el driver creó con `taskqueue_create` y almacenó en su softc. En ambos casos el puntero es opaco. Todas las interacciones se realizan mediante llamadas a la API. El único detalle interno que nos importa en este capítulo es que el taskqueue tiene su propio lock, que adquiere al encolar y cuando su thread de trabajo extrae tareas de la lista.

Para mayor completitud, la definición (de `/usr/src/sys/kern/subr_taskqueue.c`):

```c
struct taskqueue {
        STAILQ_HEAD(, task)     tq_queue;
        LIST_HEAD(, taskqueue_busy) tq_active;
        struct task            *tq_hint;
        u_int                   tq_seq;
        int                     tq_callouts;
        struct mtx_padalign     tq_mutex;
        taskqueue_enqueue_fn    tq_enqueue;
        void                   *tq_context;
        char                   *tq_name;
        struct thread         **tq_threads;
        int                     tq_tcount;
        int                     tq_spin;
        int                     tq_flags;
        ...
};
```

`tq_queue` es la lista de tareas pendientes. `tq_active` registra qué tareas están ejecutándose en ese momento, información que la lógica de drain utiliza para esperar a que finalicen. `tq_mutex` es el lock propio del taskqueue. `tq_threads` es el array de worker threads, de tamaño `tq_tcount`. `tq_spin` indica si el mutex es un spin mutex (para taskqueues creados con `taskqueue_create_fast`) o un sleep mutex (para taskqueues creados con `taskqueue_create`). `tq_flags` registra el estado de shutdown.

No toques ninguno de esos campos desde el código del driver. Se muestran aquí una sola vez para que las llamadas a la API del resto de la sección tengan un referente concreto. El resto del capítulo trata el taskqueue como opaco.

### La API, función por función

Las funciones públicas se declaran en `/usr/src/sys/sys/taskqueue.h`. Un driver suele utilizar menos de una docena de ellas. Las recorremos a continuación, agrupadas por propósito.

**Creación y destrucción de un taskqueue.**

```c
struct taskqueue *taskqueue_create(const char *name, int mflags,
    taskqueue_enqueue_fn enqueue, void *context);

struct taskqueue *taskqueue_create_fast(const char *name, int mflags,
    taskqueue_enqueue_fn enqueue, void *context);

int taskqueue_start_threads(struct taskqueue **tqp, int count, int pri,
    const char *name, ...);

void taskqueue_free(struct taskqueue *queue);
```

`taskqueue_create` crea un taskqueue que usa internamente un sleep mutex. Las tareas encoladas en él se ejecutan en un contexto en el que dormir es legal (siempre que se despachen mediante `taskqueue_thread_enqueue` y `taskqueue_start_threads`). Es la opción correcta para casi cualquier taskqueue de driver.

`taskqueue_create_fast` crea un taskqueue que usa internamente un spin mutex. Solo es necesaria si pretendes encolar desde un contexto en el que un sleep mutex sería incorrecto (por ejemplo, desde dentro de un spin mutex o de una interrupción de filtro). El código de driver rara vez lo necesita; el `taskqueue_fast` predefinido existe para los casos en que sí se requiere.

El callback `enqueue` es invocado por la capa taskqueue cuando se añade una tarea a una cola que estaba vacía, y es la forma en que dicha capa "despierta" al consumidor. Para las colas atendidas por threads del kernel, la función de enqueue es `taskqueue_thread_enqueue`, que el propio kernel proporciona. Para las colas atendidas por interrupciones de software, el kernel proporciona `taskqueue_swi_enqueue`. El código de driver casi siempre pasa aquí `taskqueue_thread_enqueue`.

El argumento `context` se pasa de vuelta al callback de enqueue. Al usar `taskqueue_thread_enqueue`, la convención es pasar `&your_taskqueue_pointer`, de modo que la función pueda localizar el taskqueue al que está despertando. Los ejemplos del capítulo 14 siguen esta convención al pie de la letra.

`taskqueue_start_threads` crea `count` threads del kernel que ejecutan el dispatcher `taskqueue_thread_loop`, cada uno durmiendo en la cola hasta que llega una tarea. El argumento `pri` es la prioridad del thread. `PWAIT` (definido en `/usr/src/sys/sys/priority.h`, con valor numérico 76) es la opción habitual para taskqueues de driver; los drivers de red suelen pasar `PI_NET` (valor numérico 4) para ejecutarse con una prioridad próxima a la de las interrupciones. Los threads de trabajo del capítulo 14 utilizan `PWAIT`.

`taskqueue_free` cierra el taskqueue. Drena todas las tareas pendientes y en ejecución, termina los threads de trabajo y libera el estado interno. Debe llamarse sin tareas pendientes que no hayan sido drenadas todavía; una vez que retorna, el `struct taskqueue *` es inválido y no debe usarse.

**Inicialización de una tarea.** `TASK_INIT` tal como se mostró antes. No existe una función "destroy" equivalente porque la estructura de la tarea es propiedad del que la llama.

**Encolado de una tarea.**

```c
int taskqueue_enqueue(struct taskqueue *queue, struct task *task);
int taskqueue_enqueue_flags(struct taskqueue *queue, struct task *task,
    int flags);
int taskqueue_enqueue_timeout(struct taskqueue *queue,
    struct timeout_task *timeout_task, int ticks);
int taskqueue_enqueue_timeout_sbt(struct taskqueue *queue,
    struct timeout_task *timeout_task, sbintime_t sbt, sbintime_t pr,
    int flags);
```

`taskqueue_enqueue` es la función principal. Enlaza la tarea en la cola y despierta el thread de trabajo. Si la tarea ya está pendiente, incrementa `ta_pending` y retorna. Devuelve cero en caso de éxito; rara vez falla.

`taskqueue_enqueue_flags` es equivalente, con flags opcionales:

- `TASKQUEUE_FAIL_IF_PENDING` hace que el enqueue devuelva `EEXIST` en lugar de fusionar si la tarea ya está pendiente.
- `TASKQUEUE_FAIL_IF_CANCELING` hace que el enqueue devuelva `EAGAIN` si la tarea está siendo cancelada en ese momento.

El `taskqueue_enqueue` por defecto fusiona silenciosamente; la variante con flags permite detectar la situación cuando eso importa.

`taskqueue_enqueue_timeout` programa un `struct timeout_task` para que se active transcurrido el número de ticks indicado. Internamente utiliza un `callout` cuyo callback encola la tarea subyacente en el taskqueue cuando vence el retardo. La variante `sbt` acepta sbintime para precisión subtick.

**Cancelación de una tarea.**

```c
int taskqueue_cancel(struct taskqueue *queue, struct task *task,
    u_int *pendp);
int taskqueue_cancel_timeout(struct taskqueue *queue,
    struct timeout_task *timeout_task, u_int *pendp);
```

`taskqueue_cancel` elimina una tarea pendiente de la cola si todavía no ha empezado a ejecutarse, y escribe el recuento de pendientes anterior en `*pendp` si ese puntero no es NULL. Si la tarea está ejecutándose en ese momento, la función devuelve `EBUSY` y no espera; debes hacer una llamada posterior a `taskqueue_drain` si necesitas esperar.

`taskqueue_cancel_timeout` es lo equivalente para tareas con timeout.

**Drenado de una tarea.**

```c
void taskqueue_drain(struct taskqueue *queue, struct task *task);
void taskqueue_drain_timeout(struct taskqueue *queue,
    struct timeout_task *timeout_task);
void taskqueue_drain_all(struct taskqueue *queue);
```

`taskqueue_drain(tq, task)` bloquea hasta que la tarea indicada deja de estar pendiente y deja de ejecutarse. Si la tarea estaba pendiente, el drenado espera a que se ejecute y complete. Si la tarea estaba ejecutándose, el drenado espera a que la invocación en curso retorne. Si la tarea estaba inactiva, el drenado retorna de inmediato. Esta es la llamada que debes usar en detach para cada tarea que pertenezca a tu driver.

`taskqueue_drain_timeout` es lo equivalente para tareas con timeout.

`taskqueue_drain_all` drena todas las tareas y todas las tareas con timeout del taskqueue. Es útil cuando eres propietario de un taskqueue privado y quieres asegurarte de que está completamente inactivo antes de liberarlo. El propio `taskqueue_free` realiza internamente el trabajo equivalente, por lo que `taskqueue_drain_all` no es estrictamente necesario antes de `taskqueue_free`, pero resulta útil cuando quieres silenciar un taskqueue sin destruirlo.

**Bloqueo y desbloqueo.**

```c
void taskqueue_block(struct taskqueue *queue);
void taskqueue_unblock(struct taskqueue *queue);
void taskqueue_quiesce(struct taskqueue *queue);
```

`taskqueue_block` impide que la cola ejecute nuevas tareas. Las tareas ya en ejecución se completan; las recién encoladas se acumulan pero no se ejecutan hasta que se llama a `taskqueue_unblock`. El par es útil para congelar temporalmente una cola durante una transición delicada sin necesidad de destruirla.

`taskqueue_quiesce` espera a que la tarea que se está ejecutando en ese momento (si existe) finalice y a que la cola quede vacía de tareas pendientes. Equivale a "drena todo, pero no destruyas". Es seguro llamarla con la cola en ejecución.

**Comprobación de pertenencia.**

```c
int taskqueue_member(struct taskqueue *queue, struct thread *td);
```

Devuelve true si el thread dado es uno de los threads de trabajo del taskqueue. Es útil dentro de un callback de tarea cuando quieres ramificar en función de si te estás ejecutando en tu propio taskqueue, aunque el idioma más habitual es comparar `curthread` con un puntero de thread almacenado.

Esta es la totalidad de la API que un driver utiliza habitualmente. Existen unas pocas funciones menos comunes (`taskqueue_set_callback` para hooks de inicio y cierre, `taskqueue_poll_is_busy` para comprobaciones de tipo polling), pero la mayoría de los drivers no las utilizan nunca.

### Los taskqueues predefinidos

El kernel proporciona un pequeño conjunto de taskqueues preconfigurados para drivers que no necesitan uno privado. Se declaran en `/usr/src/sys/sys/taskqueue.h` con `TASKQUEUE_DECLARE`, que se expande a un puntero extern. Un driver los usa por nombre:

```c
TASKQUEUE_DECLARE(thread);
TASKQUEUE_DECLARE(swi);
TASKQUEUE_DECLARE(swi_giant);
TASKQUEUE_DECLARE(fast);
TASKQUEUE_DECLARE(bus);
```

**`taskqueue_thread`** es la cola genérica de contexto thread. Un único thread del kernel, prioridad `PWAIT`. El nombre del thread aparece en `ps` como `thread taskq`. Es seguro para cualquier tarea que necesite un contexto de thread completo y no requiera propiedades especiales. Es la cola predefinida más sencilla de usar; una primera elección muy razonable si no estás seguro de qué cola necesitas.

**`taskqueue_swi`** es despachado por un manejador de interrupción de software, no por un thread del kernel. Las tareas de esta cola se ejecutan sin ningún mutex de driver adquirido, pero en contexto SWI, que aún tiene restricciones (no se puede dormir). Es útil para trabajo breve que no requiere dormir y que quiere ejecutarse de inmediato tras el enqueue, sin la latencia de planificación que implica despertar un thread del kernel. Su uso en drivers es poco habitual.

**`taskqueue_swi_giant`** es igual que `taskqueue_swi` pero se ejecuta con el lock histórico `Giant` adquirido. No se usa prácticamente nunca en código nuevo. Se menciona únicamente por completitud.

**`taskqueue_fast`** es una cola de interrupción de software respaldada por spin mutex, utilizada para tareas que deben poder encolarse desde contextos en los que un sleep mutex sería incorrecto (por ejemplo, desde dentro de otro spin mutex). El propio taskqueue usa un spin mutex para su lista interna, de modo que el enqueue es válido desde cualquier contexto. Sin embargo, el callback de la tarea se ejecuta en contexto SWI, que aún tiene la restricción de no poder dormir. Su uso en drivers es escaso; los contextos de interrupción de filtro que necesitan encolar trabajo suelen usar `taskqueue_fast` o, más habitualmente hoy en día, una cola `taskqueue_create_fast` privada.

**`taskqueue_bus`** es una cola dedicada a los eventos de dispositivo de `newbus(9)` (inserción por hot-plug, extracción, notificaciones de bus hijo). Los drivers ordinarios no encolan en esta cola.

Para un driver como `myfirst`, las opciones realistas son `taskqueue_thread` (la cola compartida) o un taskqueue privado del que eres propietario y que desmantelas en detach. La sección 4 analiza las ventajas e inconvenientes de cada opción; la etapa 1 del refactor usa `taskqueue_thread` por simplicidad y la etapa 2 pasa a una cola privada.

### Comparación entre taskqueues y callouts

Una breve comparación en paralelo, porque es la primera pregunta que hace un lector nuevo.

| Propiedad | `callout(9)` | `taskqueue(9)` |
|---|---|---|
| Se activa en | Un momento concreto | En cuanto un thread de trabajo lo recoge |
| Contexto del callback | Thread de callout (por defecto) o IRQ hardclock (`C_DIRECT_EXEC`) | Thread del kernel (para `taskqueue_thread` y colas privadas) o SWI (para `taskqueue_swi`, `taskqueue_fast`) |
| Puede dormir | No | Sí, en colas respaldadas por threads; no, en colas respaldadas por SWI |
| Puede adquirir locks durmientes | No | Sí, en colas respaldadas por threads |
| Puede llamar a `uiomove`, `copyin`, `copyout` | No | Sí, en colas respaldadas por threads |
| Fusiona envíos redundantes | No; cada reset reemplaza el plazo anterior | Sí; `ta_pending` se incrementa |
| Cancelable antes de dispararse | `callout_stop(co)` | `taskqueue_cancel(tq, task, &pendp)` |
| Espera al callback en vuelo | `callout_drain(co)` | `taskqueue_drain(tq, task)` |
| Periódico | El callback se reprograma a sí mismo | No; encola de nuevo desde otro lugar, o usa un callout para encolar |
| Programado para el futuro | `callout_reset(co, ticks, ...)` | `taskqueue_enqueue_timeout(tq, tt, ticks)` |
| Coste por activación | Microsegundos | Microsegundos más el despertar del thread (puede ser mayor bajo carga) |

La tabla ilustra la división. Un callout es el primitivo correcto cuando necesitas activarte en un momento concreto y el trabajo es seguro para contexto de callout. Un taskqueue es el primitivo correcto cuando necesitas trabajo en contexto de thread y estás dispuesto a aceptar la latencia de planificación que el taskqueue introduce. Muchos drivers usan ambos conjuntamente: un callout se activa en el plazo, el callout encola una tarea, y la tarea realiza el trabajo real en contexto de thread.

### Comparación entre taskqueues y un thread del kernel privado

Otra comparación que el capítulo te debe, porque un lector que pregunta "por qué no crear simplemente un thread del kernel" merece una respuesta directa.

Un thread del kernel creado con `kproc_create(9)` es una entidad planificada completa: su propia pila (típicamente 16 KB en amd64), su propia prioridad, su propia entrada `proc`, su propio estado. Un driver que quiera ejecutar un bucle "cada segundo, haz X" podría crear tal thread y hacer que avance por el bucle mediante `kproc_kthread_add` y `cv_timedwait`. El código funciona, pero tiene un coste mayor del que el trabajo suele merecer. Un taskqueue con un thread que permanece inactivo la mayor parte del tiempo y se despierta al encolar es más barato por elemento de trabajo pendiente y más sencillo de desmantelar.

Existen casos legítimos para `kproc_create`. Un subsistema de larga ejecución con su propia configuración (prioridad, afinidad de CPU, grupo de procesos) es uno de ellos. Un trabajo periódico que genuinamente necesita un thread propio para observabilidad es otro. El patrón de trabajo diferido de un driver casi nunca lo es. Usa un taskqueue hasta que un requisito concreto te obligue a hacer otra cosa.

### La regla del encolado con tarea ya pendiente

Hay una regla que merece mencionarse pronto, porque es la fuente de sorpresa más habitual para los recién llegados a la API: una tarea no puede estar pendiente dos veces. Si llamas a `taskqueue_enqueue(tq, &sc->t)` mientras `sc->t` ya está pendiente, el kernel incrementa `sc->t.ta_pending` y devuelve éxito sin enlazar la tarea una segunda vez.

Esto tiene dos implicaciones. Primera, tu callback se ejecutará una vez, no dos, aunque hayas encolado dos veces. Segunda, el argumento `pending` que recibe el callback es el número de veces que la tarea fue encolada antes de que el callback fuera despachado; tu callback puede usar ese recuento para procesar trabajo acumulado en lote.

Si quieres ejecutar el callback N veces por N encolados, una sola tarea no es el modelo correcto. Usa N tareas separadas, o encola un centinela en una cola propiedad del driver y procesa cada centinela en el callback. Casi siempre el comportamiento de fusión es lo que quieres; la sección 5 explica cómo aprovecharlo deliberadamente.

### Un ejemplo mínimo, de principio a fin

Una tarea de hola mundo, para concretar. Si escribes esto en un módulo de prueba y lo cargas, verás la línea de `device_printf` en `dmesg`:

```c
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>

static struct task example_task;

static void
example_task_fn(void *context, int pending)
{
        printf("example_task_fn: pending=%d\n", pending);
}

static int
example_modevent(module_t m, int event, void *arg)
{
        int error = 0;

        switch (event) {
        case MOD_LOAD:
                TASK_INIT(&example_task, 0, example_task_fn, NULL);
                taskqueue_enqueue(taskqueue_thread, &example_task);
                break;
        case MOD_UNLOAD:
                taskqueue_drain(taskqueue_thread, &example_task);
                break;
        default:
                error = EOPNOTSUPP;
                break;
        }
        return (error);
}

static moduledata_t example_mod = {
        "example_task", example_modevent, NULL
};
DECLARE_MODULE(example_task, example_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(example_task, 1);
```

El módulo hace cinco cosas, cada una en una sola línea. En la carga, `TASK_INIT` prepara la estructura de la tarea. `taskqueue_enqueue` pide al `taskqueue_thread` compartido que ejecute el callback. El callback imprime un mensaje. En la descarga, `taskqueue_drain` espera a que el callback termine si no lo ha hecho ya. Todo el ciclo de vida es compacto.

Si escribes esto y lo cargas, `dmesg` muestra:

```text
example_task_fn: pending=1
```

El `pending=1` refleja el hecho de que la tarea se encoló una vez antes de que el callback se ejecutara.

Ahora prueba una demostración del coalescing: cambia `MOD_LOAD` para encolar la tarea cinco veces seguidas y añade una pequeña espera para que el thread del taskqueue tenga oportunidad de activarse:

```c
for (int i = 0; i < 5; i++)
        taskqueue_enqueue(taskqueue_thread, &example_task);
pause("example", hz / 10);
```

Ejecútalo de nuevo y `dmesg` muestra:

```text
example_task_fn: pending=5
```

Una sola invocación, con un pending de cinco. Esa es la regla del coalescing en acción.

Con esto hay suficiente estructura para que las refactorizaciones detalladas en las secciones siguientes tengan sentido. El resto de este capítulo escala la misma estructura hasta el driver `myfirst` real, reemplaza el módulo de prueba con cuatro etapas de integración, añade el teardown, añade un taskqueue privado y recorre el proceso de depuración.

### En resumen: sección 2

Una `struct task` almacena un callback y su contexto. Una `struct taskqueue` gestiona una cola de tareas de este tipo y uno o más threads (o un contexto SWI) que las consumen. La API es pequeña: crear, arrancar threads, encolar (opcionalmente con un retardo), cancelar, vaciar, liberar, bloquear, desbloquear, quiesce. El kernel proporciona un conjunto de colas predefinidas que cualquier driver puede usar sin necesidad de crear la suya propia. La regla de encolar cuando ya está pendiente agrupa las solicitudes redundantes en una única invocación cuyo contador de pendientes refleja el recuento final.

La sección 3 lleva esas herramientas al driver `myfirst` y añade la primera tarea al código, bajo el callout `tick_source` que venía omitiendo silenciosamente `selwakeup`. El cambio es pequeño; lo importante es el modelo mental.



## Sección 3: diferir trabajo desde un temporizador o una interrupción simulada

El capítulo 13 dejó el driver `myfirst` con tres callouts que obedecían estrictamente el contrato del callout. Ninguno intentaba hacer nada que no correspondiese a un callback de callout. El callback `tick_source` en particular omitía la llamada a `selwakeup` que un driver real querría realizar cuando aparecen nuevos bytes en el buffer, y el archivo incluso llevaba un comentario que lo indicaba. El capítulo 14 elimina esa omisión.

La sección 3 es la primera refactorización guiada. Introduce la Etapa 1 del driver del capítulo 14: el driver gana una `struct task`, un callback de tarea, un encolado desde `tick_source` y un vaciado en el detach. El trabajo con la taskqueue privada queda para la sección 4; en la Etapa 1 usamos la `taskqueue_thread` compartida. Usar la cola compartida en primer lugar mantiene el primer paso pequeño y aísla el cambio al patrón de trabajo diferido en sí.

### El cambio en una sola frase

Cuando `tick_source` acaba de depositar un byte en el buffer circular, en lugar de omitir silenciosamente `selwakeup`, encola una tarea cuyo callback ejecuta `selwakeup` en contexto de thread.

Ese es todo el cambio. Todo lo demás es la infraestructura de apoyo.

### La adición al softc

Añade dos miembros a `struct myfirst_softc`:

```c
struct task             selwake_task;
int                     selwake_pending_drops;
```

La `selwake_task` es la tarea que encolaremos. El `selwake_pending_drops` es un contador de depuración que incrementaremos cada vez que la tarea agrupe dos o más encolados en una única ejecución; la diferencia entre el «número de llamadas de encolado» y el «número de invocaciones del callback» nos indica con qué frecuencia el tick source produjo datos más rápido de lo que los vaciaba el thread de la taskqueue. Es puramente diagnóstico; puedes omitirlo si lo prefieres, pero ver un contador de coalescencia real en acción tiene mucho valor.

Añade un sysctl de solo lectura para poder observar el contador desde el espacio de usuario sin necesidad de una build de depuración:

```c
SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "selwake_pending_drops", CTLFLAG_RD,
    &sc->selwake_pending_drops, 0,
    "Times selwake_task coalesced two or more enqueues into one firing");
```

La ubicación solo importa en el sentido de que debe aparecer después de que `sc->sysctl_tree` haya sido creado y antes de que la función devuelva éxito; la secuencia de attach del capítulo 13 ya tiene la estructura correcta, así que la adición encaja de forma natural junto a las demás estadísticas.

### El callback de la tarea

Añade una función:

```c
static void
myfirst_selwake_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        if (pending > 1) {
                MYFIRST_LOCK(sc);
                sc->selwake_pending_drops++;
                MYFIRST_UNLOCK(sc);
        }

        /*
         * No driver mutex held. Safe to call selwakeup(9) here.
         */
        selwakeup(&sc->rsel);
}
```

Hay varias cosas que observar.

El callback recibe el softc a través del puntero `arg`, exactamente igual que los callbacks de callout. No necesita `MYFIRST_ASSERT` al inicio porque el callback de la tarea no se ejecuta con ningún lock del driver adquirido; el framework de taskqueue no adquiere el lock por ti. Esto difiere del patrón de callout con conocimiento del lock del capítulo 13, y merece la pena detenerse en ello. Un callout inicializado con `callout_init_mtx(&co, &sc->mtx, 0)` se ejecuta con `sc->mtx` adquirido. Una tarea nunca lo hace. Dentro del callback de la tarea, si quieres acceder al estado que protege el mutex, debes adquirir el mutex tú mismo, realizar el trabajo, liberarlo y continuar.

El callback actualiza condicionalmente `selwake_pending_drops` bajo el mutex. La condición `pending > 1` significa «este callback está gestionando al menos dos encolados agrupados». Incrementar un contador bajo el mutex es rápido y seguro; hacerlo incondicionalmente haría que el caso común (`pending == 1`, sin agrupación) pagase el coste del lock innecesariamente.

La llamada a `selwakeup(&sc->rsel)` es la razón por la que estamos aquí. Se ejecuta sin ningún lock del driver adquirido, que es lo que `selwakeup` requiere, y se ejecuta en contexto de thread, que es lo que `selwakeup` necesita. El bug del capítulo 13 queda corregido.

El callback no comprueba `is_attached`. No es necesario. La ruta de detach vacía la tarea antes de liberar el selinfo; cuando `is_attached` sea cero, se garantiza que el callback de la tarea no estará ejecutándose, y `selwakeup` encontrará un estado válido. El orden de vaciado es lo que hace que la omisión sea segura, razón por la que tratamos el orden con tanta atención en la sección 4.

### La modificación de `tick_source`

Cambia el callback `tick_source` de:

```c
static void
myfirst_tick_source(void *arg)
{
        struct myfirst_softc *sc = arg;
        size_t put;
        int interval;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        if (cbuf_free(&sc->cb) > 0) {
                put = cbuf_write(&sc->cb, &sc->tick_source_byte, 1);
                if (put > 0) {
                        counter_u64_add(sc->bytes_written, put);
                        cv_signal(&sc->data_cv);
                        /* selwakeup omitted: cannot be called from a
                         * callout callback while sc->mtx is held. */
                }
        }

        interval = sc->tick_source_interval_ms;
        if (interval > 0)
                callout_reset(&sc->tick_source_co,
                    (interval * hz + 999) / 1000,
                    myfirst_tick_source, sc);
}
```

a:

```c
static void
myfirst_tick_source(void *arg)
{
        struct myfirst_softc *sc = arg;
        size_t put;
        int interval;
        bool wake_sel = false;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        if (cbuf_free(&sc->cb) > 0) {
                put = cbuf_write(&sc->cb, &sc->tick_source_byte, 1);
                if (put > 0) {
                        counter_u64_add(sc->bytes_written, put);
                        cv_signal(&sc->data_cv);
                        wake_sel = true;
                }
        }

        if (wake_sel)
                taskqueue_enqueue(taskqueue_thread, &sc->selwake_task);

        interval = sc->tick_source_interval_ms;
        if (interval > 0)
                callout_reset(&sc->tick_source_co,
                    (interval * hz + 999) / 1000,
                    myfirst_tick_source, sc);
}
```

Dos modificaciones. Un indicador local `wake_sel` registra si se ha escrito un byte; la llamada a `taskqueue_enqueue` ocurre después del trabajo sobre el cbuf. El comentario sobre «selwakeup omitido» queda obsoleto y se elimina.

¿Por qué usar un indicador en lugar de llamar a `taskqueue_enqueue` directamente dentro del bloque `if (put > 0)`? Porque `taskqueue_enqueue` es seguro de llamar mientras se mantiene `sc->mtx` (adquiere su propio mutex interno; no hay problema de orden de locks para el mutex de la taskqueue porque no está ordenado respecto a `sc->mtx`), pero es buena práctica mantener las secciones bajo mutex ajustadas y nombrar el motivo del encolado con una variable local. La versión con el indicador es más fácil de leer y más fácil de extender si etapas posteriores añaden más condiciones que deberían disparar el wakeup.

¿Es realmente seguro llamar a `taskqueue_enqueue` desde un callback de callout con `sc->mtx` adquirido? Sí. La taskqueue usa su propio mutex interno (`tq_mutex`), completamente separado de `sc->mtx`; no se establece ningún orden de lock entre ellos, por lo que `WITNESS` no tiene nada que objetar. Lo verificaremos en el laboratorio al final de esta sección. Como referencia futura, la garantía relevante en `/usr/src/sys/kern/subr_taskqueue.c` es que `taskqueue_enqueue` adquiere `TQ_LOCK(tq)` (un sleep mutex para `taskqueue_create`, un spin mutex para `taskqueue_create_fast`), realiza la manipulación de la lista y libera el lock. Sin dormir, sin recursión en el lock del llamador, sin dependencias cruzadas de locks.

### La modificación del attach

En `myfirst_attach`, añade una línea después de las inicializaciones de callout existentes:

```c
TASK_INIT(&sc->selwake_task, 0, myfirst_selwake_task, sc);
```

Colócala junto a las llamadas de inicialización de callout. La agrupación conceptual («aquí es donde preparamos las primitivas de trabajo diferido del driver») hace que el archivo sea más fácil de recorrer visualmente.

Inicializa `selwake_pending_drops` a cero en el mismo bloque donde se ponen a cero los demás contadores:

```c
sc->selwake_pending_drops = 0;
```

### La modificación del detach

Esta es la parte crítica de la etapa. La secuencia de detach del capítulo 13, simplificada, es:

1. Rechazar el detach si `active_fhs > 0`.
2. Limpiar `is_attached`.
3. Hacer broadcast en `data_cv` y `room_cv`.
4. Vaciar `rsel` y `wsel` mediante `seldrain`.
5. Vaciar los tres callouts.
6. Destruir dispositivos, liberar sysctls, destruir cbuf, liberar contadores, destruir cvs, destruir sx, destruir mtx.

La Etapa 1 del capítulo 14 añade un paso: vaciar `selwake_task` entre los vaciados de callout (paso 5) y las llamadas a `seldrain` (paso 4). En realidad, la sutileza del orden es más precisa que eso. Analicémoslo con detenimiento.

El callback `selwake_task` llama a `selwakeup(&sc->rsel)`. Si `sc->rsel` está siendo vaciado de forma concurrente, el callback podría generar una condición de carrera. La regla es: asegurarse de que el callback de la tarea no esté ejecutándose antes de llamar a `seldrain`. Eso significa que `taskqueue_drain(taskqueue_thread, &sc->selwake_task)` debe ocurrir antes de `seldrain(&sc->rsel)`.

Sin embargo, la tarea puede seguir siendo encolada por un callback de callout en vuelo hasta que hayamos vaciado los callouts. Si vaciamos la tarea primero y después los callouts, un callout en vuelo podría volver a encolar la tarea después de haberla vaciado, y la tarea re-encolada intentaría ejecutarse después de `seldrain`.

El único orden seguro es: vaciar los callouts primero (lo que garantiza que no habrá más encolados), luego vaciar la tarea (lo que garantiza que el último encolado se ha completado), y después llamar a `seldrain`. Pero también debemos limpiar `is_attached` antes de vaciar los callouts para que un callback en vuelo salga anticipadamente en lugar de re-armarse.

Juntando todo, el orden de detach de la Etapa 1 es:

1. Rechazar el detach si `active_fhs > 0`.
2. Limpiar `is_attached` (bajo el mutex).
3. Hacer broadcast en `data_cv` y `room_cv` (liberar el mutex primero).
4. Vaciar los tres callouts (sin mutex adquirido; `callout_drain` puede dormir).
5. Vaciar `selwake_task` (sin mutex adquirido; `taskqueue_drain` puede dormir).
6. Vaciar `rsel` y `wsel` mediante `seldrain`.
7. Destruir dispositivos, liberar sysctls, destruir cbuf, liberar contadores, destruir cvs, destruir sx, destruir mtx.

Los pasos 4 y 5 son la nueva restricción de orden. Callouts primero, tareas segundo, sel tercero. Violar ese orden en un kernel de depuración suele disparar una aserción dentro de `seldrain`; en un kernel de producción es un use-after-free esperando ocurrir.

El código en `myfirst_detach` queda así:

```c
/* Chapter 13: drain every callout. No lock held; safe to sleep. */
MYFIRST_CO_DRAIN(&sc->heartbeat_co);
MYFIRST_CO_DRAIN(&sc->watchdog_co);
MYFIRST_CO_DRAIN(&sc->tick_source_co);

/* Chapter 14: drain every task. No lock held; safe to sleep. */
taskqueue_drain(taskqueue_thread, &sc->selwake_task);

seldrain(&sc->rsel);
seldrain(&sc->wsel);
```

Dos líneas de código más un comentario. El orden es visible en el código fuente.

### El Makefile

Sin cambios. `bsd.kmod.mk` recoge las cabeceras de la API de taskqueue del árbol del sistema; no se necesitan archivos fuente adicionales para la Etapa 1.

### Compilar y cargar

En este punto, tu copia de trabajo debería tener:

- Los dos nuevos miembros del softc (`selwake_task`, `selwake_pending_drops`).
- La función `myfirst_selwake_task`.
- La modificación de `myfirst_tick_source`.
- La llamada a `TASK_INIT` y el contador puesto a cero en el attach.
- La llamada a `taskqueue_drain` en el detach.
- El nuevo sysctl `selwake_pending_drops`.

Compila desde el directorio de la Etapa 1:

```text
# cd /path/to/examples/part-03/ch14-taskqueues-and-deferred-work/stage1-first-task
# make clean && make
```

Carga:

```text
# kldload ./myfirst.ko
```

Verifica:

```text
# kldstat | grep myfirst
 7    1 0xffffffff82f30000    ... myfirst.ko
# sysctl dev.myfirst.0
dev.myfirst.0.stats.selwake_pending_drops: 0
...
```

### Observar la corrección

Para observar la Etapa 1 en acción, inicia un programa de sondeo sobre el dispositivo con `poll(2)` y haz que el tick source genere datos. Un sencillo programa de este tipo se encuentra en `examples/part-03/ch14-taskqueues-and-deferred-work/labs/poll_waiter.c`:

```c
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <err.h>

int
main(int argc, char **argv)
{
        int fd, n;
        struct pollfd pfd;
        char c;

        fd = open("/dev/myfirst", O_RDONLY);
        if (fd < 0)
                err(1, "open");
        pfd.fd = fd;
        pfd.events = POLLIN;

        for (;;) {
                n = poll(&pfd, 1, -1);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        err(1, "poll");
                }
                if (pfd.revents & POLLIN) {
                        n = read(fd, &c, 1);
                        if (n > 0)
                                write(STDOUT_FILENO, &c, 1);
                }
        }
}
```

Compílalo con `cc poll_waiter.c -o poll_waiter` (sin bibliotecas especiales). Ejecútalo en un terminal:

```text
# ./poll_waiter
```

En un segundo terminal, activa el tick source a un ritmo lento para que la salida sea fácil de observar:

```text
# sysctl dev.myfirst.0.tick_source_interval_ms=500
```

El driver del capítulo 13, sin la corrección de la Etapa 1, dejaría a `poll_waiter` bloqueado. Los bytes depositados se acumularían en el buffer, pero `poll(2)` nunca retornaría porque `selwakeup` nunca se invocaba. No verías nada.

El driver de la Etapa 1 sí llama a `selwakeup`, a través de la tarea. Deberías ver caracteres `t` apareciendo en el terminal de `poll_waiter` cada medio segundo. Cuando detengas la prueba, `poll_waiter` sale limpiamente con `Ctrl-C`.

Ahora acelera el tick source para poner bajo presión la taskqueue:

```text
# sysctl dev.myfirst.0.tick_source_interval_ms=1
```

Deberías ver un flujo continuo de caracteres `t`. Comprueba el contador de coalescencia:

```text
# sysctl dev.myfirst.0.stats.selwake_pending_drops
dev.myfirst.0.stats.selwake_pending_drops: <some number, growing slowly>
```

El número es la cantidad de veces que el callback de la tarea gestionó un contador de pendientes mayor que uno. En una máquina con poca carga puede permanecer pequeño (el thread de la taskqueue se despierta con suficiente rapidez para gestionar cada encolado individualmente). Bajo contención el número crece, y puedes observar directamente el comportamiento de la coalescencia.

Si el contador permanece a cero incluso bajo carga, la máquina es lo suficientemente rápida como para que cada encolado se vacíe antes de que llegue el siguiente. Eso no es un bug; es señal de que la coalescencia existe pero no se activa. La sección 5 introduce una carga de trabajo deliberada que fuerza la coalescencia.

### Descargar el módulo

Detén el tick source:

```text
# sysctl dev.myfirst.0.tick_source_interval_ms=0
```

Cierra `poll_waiter` con `Ctrl-C`. Descarga:

```text
# kldunload myfirst
```

La descarga debería ser limpia. Si falla con `EBUSY`, todavía tienes algún descriptor abierto; el cierre y el siguiente `kldunload` deberían funcionar.

Si la descarga se queda colgada, algo en la ruta de detach está bloqueado. La causa más probable es que `taskqueue_drain` esté esperando una tarea que no puede completarse. Eso indicaría un bug, y la sección de depuración (sección 7) muestra cómo identificarlo. En el flujo normal, la descarga se completa en milisegundos.

### Lo que acabamos de hacer

Un breve resumen antes de que la sección 4 escale.

La etapa 1 añadió una tarea al driver, la inicializó en attach, la encoló desde un callback de callout, la drenó en detach en el orden correcto y observó la coalescencia en acción. La tarea se ejecuta en el `taskqueue_thread` compartido; comparte esa cola con todos los demás drivers del sistema que también la utilizan. Para una carga de trabajo de baja frecuencia, eso está perfectamente bien. Para un driver que vaya a realizar trabajo considerable en sus tareas, o que quiera aislar la latencia de procesamiento de tareas de cualquier otra cosa que esté haciendo el sistema, un taskqueue privado es la respuesta correcta. La sección 4 da ese paso.

### Errores comunes que debes evitar

Una breve lista de errores que cometen los principiantes al escribir su primera tarea. Cada uno ha afectado a drivers reales; cada uno tiene una regla sencilla que lo previene.

**Olvidar el drain en el detach.** Si encolas tareas sin drenarlas, una tarea en curso puede ejecutarse después de que el softc haya sido liberado, y el kernel se bloqueará en el callback de la tarea con una desreferencia de memoria liberada. Haz siempre drain de cada tarea que pertenezca a tu driver antes de liberar cualquier cosa que esa tarea utilice.

**Hacer el drain en el orden incorrecto respecto al estado que usa la tarea.** El orden tarea-luego-sel que comentamos antes es un caso concreto. La regla general: haz drain de cada productor de encolados, luego haz drain de la tarea y, por último, libera el estado que usa la tarea. Violar el orden es una condición de carrera, aunque sea poco frecuente.

**Asumir que la tarea se ejecuta inmediatamente tras el encolado.** No es así. El thread del taskqueue se despierta al encolar y luego el planificador decide cuándo ejecutarla. Bajo carga, esto puede suponer milisegundos. Los drivers que asumen latencia cero fallan bajo carga.

**Asumir que la tarea se ejecuta una vez por encolado.** No es así. La coalescencia fusiona envíos redundantes. Si necesitas la semántica de "exactamente una vez por evento", necesitas un estado por evento dentro del softc (una cola de elementos de trabajo, por ejemplo), no una tarea por evento.

**Adquirir el lock del driver en el orden incorrecto en el callback de la tarea.** El callback de la tarea es código ordinario de contexto de thread. Obedece el orden de lock establecido por tu driver. Si el orden del driver es `sc->mtx -> sc->cfg_sx`, el callback de la tarea debe tomar el mutex antes que el sx. Las violaciones de este orden son errores de `WITNESS`, igual que en cualquier otro lugar.

**Usar `taskqueue_enqueue` desde un contexto de interrupción de filtro sin un fast taskqueue.** `taskqueue_enqueue(taskqueue_thread, ...)` adquiere un sleep mutex sobre el lock interno del taskqueue. Esto es ilegal desde un contexto de interrupción de filtro. Las interrupciones de filtro deben encolarse en `taskqueue_fast` o en una cola `taskqueue_create_fast`. Los callbacks de callout no tienen esta restricción porque se ejecutan en contexto de thread; el problema es específico de las interrupciones de filtro. La Parte 4 retomará este tema cuando presente `bus_setup_intr`.

Cada uno de estos errores puede detectarse mediante una revisión, con `WITNESS` o con una prueba de estrés bien escrita. Los dos primeros en particular son el tipo de bug que parece correcto hasta el primer detach bajo carga.

### Cerrando la sección 3

El driver `myfirst` tiene ahora una tarea. La usa para sacar `selwakeup` del callback de callout y llevarlo al contexto de thread, corrigiendo un bug real del Capítulo 13. La tarea se inicializa en el attach, se encola desde el callback `tick_source` y se drena en el detach en el orden correcto respecto a los callouts y al drain del selinfo.

El `taskqueue_thread` compartido es el primer taskqueue que usamos porque ya estaba disponible. Para un driver que va a incorporar más tareas y más responsabilidades, un taskqueue privado ofrece mejor aislamiento y una secuencia de teardown más limpia. La sección 4 crea ese taskqueue privado.



## Sección 4: Configuración y limpieza del taskqueue

La etapa 1 usó el `taskqueue_thread` compartido. Esa elección mantuvo el primer cambio pequeño: una tarea, un encolado, un drain y un orden de detach a respetar. La etapa 2 crea un taskqueue privado propiedad del driver. El cambio es pequeño en términos de código, pero aporta una serie de propiedades que se vuelven importantes conforme el driver crece.

Esta sección enseña la etapa 2 de la refactorización, recorre la configuración y el teardown de un taskqueue privado, audita con cuidado el orden del detach y termina con una lista de comprobación previa a producción que puedes reutilizar en cada driver que escribas con taskqueue.

### Por qué usar un taskqueue privado

Hay tres razones para usar un taskqueue privado.

La primera es el **aislamiento**. El thread de un taskqueue privado ejecuta únicamente las tareas de tu driver. Si algún otro driver del sistema se comporta mal en `taskqueue_thread` (bloqueándose demasiado tiempo en su callback de tarea, por ejemplo), las tareas de tu driver no se ven afectadas. A la inversa, si tu driver se comporta mal, el problema queda contenido.

La segunda es la **observabilidad**. `procstat -t` y `ps ax` muestran cada thread de taskqueue con un nombre distinto. Una cola privada es fácil de identificar: aparece con el nombre que le hayas dado (por convención, `myfirst taskq`). El `taskqueue_thread` compartido aparece simplemente como `thread taskq`, compartido con todos los demás drivers.

La tercera es que el **teardown es autónomo**. Al hacer el detach, drenas y liberas tu propio taskqueue. No tienes que razonar sobre si algún otro driver tiene una tarea pendiente por la que tu drain pudiera esperar. (En realidad no esperarías la tarea de otro driver en la cola compartida, pero el modelo mental de "somos dueños de nuestro teardown" es más fácil de razonar.)

El coste es pequeño. Un taskqueue y un kernel thread, creados en el attach y destruidos en el detach. Unas pocas páginas de memoria y un par de entradas en el planificador. Nada medible en ningún sistema real.

Para un driver que acabará teniendo múltiples tareas, un taskqueue privado es la opción correcta por defecto. Para un driver con una única tarea trivial en un camino de código poco frecuente, la cola compartida es suficiente. `myfirst` es el primero: ya tenemos una tarea, y el capítulo añadirá más.

### La adición al softc

Añade un miembro a `struct myfirst_softc`:

```c
struct taskqueue       *tq;
```

No hay más cambios en el softc para la etapa 2.

### Creación del taskqueue en el attach

En `myfirst_attach`, entre las inicializaciones de mutex / cv / sx y las inicializaciones de callout, añade:

```c
sc->tq = taskqueue_create("myfirst taskq", M_WAITOK,
    taskqueue_thread_enqueue, &sc->tq);
if (sc->tq == NULL) {
        error = ENOMEM;
        goto fail_sx;
}
error = taskqueue_start_threads(&sc->tq, 1, PWAIT,
    "%s taskq", device_get_nameunit(dev));
if (error != 0)
        goto fail_tq;
```

La llamada se lee así: crea un taskqueue llamado `"myfirst taskq"`, asigna memoria con `M_WAITOK` para que la asignación no pueda fallar (estamos en el attach, que es un contexto que puede dormir), usa `taskqueue_thread_enqueue` como dispatcher para que la cola sea atendida por kernel threads, y pasa `&sc->tq` como contexto para que el dispatcher pueda encontrar la cola.

El nombre `"myfirst taskq"` es la etiqueta legible por humanos que aparece en `procstat -t`. La convención en los ejemplos del Capítulo 14 es `"<driver> taskq"` para un driver con una única cola privada; los drivers con múltiples colas deben usar nombres más específicos como `"myfirst rx taskq"` y `"myfirst tx taskq"`.

`taskqueue_start_threads` crea los threads de trabajo. El primer argumento es `&sc->tq`, un puntero doble para que la función pueda encontrar el taskqueue. El segundo argumento es el número de threads; usamos uno para `myfirst`. Un driver con trabajo intenso y paralelizable podría usar más. El tercer argumento es la prioridad; `PWAIT` es la opción habitual y equivalente a la que usa el `taskqueue_thread` predefinido. El nombre variádico es una cadena de formato para el nombre de cada thread; `device_get_nameunit(dev)` proporciona un nombre por instancia para que múltiples instancias de `myfirst` tengan threads distinguibles.

Los caminos de error merecen atención. Si `taskqueue_create` devuelve NULL (normalmente no lo hace con `M_WAITOK`, pero sé defensivo), saltamos a `fail_sx`. Si `taskqueue_start_threads` falla, saltamos a `fail_tq`, que debe llamar a `taskqueue_free` antes de continuar con el resto de la limpieza. El código fuente de la etapa 2 del Capítulo 14 (véase el árbol de ejemplos) tiene las etiquetas en el orden correcto.

### Actualización de los puntos de llamada al encolado

Cada llamada `taskqueue_enqueue(taskqueue_thread, ...)` pasa a ser `taskqueue_enqueue(sc->tq, ...)`. Lo mismo para los drains: `taskqueue_drain(taskqueue_thread, ...)` se convierte en `taskqueue_drain(sc->tq, ...)`.

Tras la etapa 1, el driver tiene dos de estos puntos de llamada: el encolado en `myfirst_tick_source` y el drain en `myfirst_detach`. Ambos cambian en una única pasada de búsqueda y reemplazo.

### La secuencia de teardown

El orden del detach crece en dos líneas. La secuencia completa para la etapa 2 es:

1. Rechazar el detach si `active_fhs > 0`.
2. Limpiar `is_attached` bajo el mutex, hacer broadcast de ambas cvs y soltar el mutex.
3. Drenar los tres callouts.
4. Drenar `selwake_task` en el taskqueue privado.
5. Drenar `rsel` y `wsel` mediante `seldrain`.
6. Liberar el taskqueue privado con `taskqueue_free`.
7. Destruir los dispositivos, liberar los sysctls, destruir el cbuf, liberar los contadores, destruir las cvs, destruir el sx y destruir el mtx.

Los nuevos pasos son el 4 (que ya existía en la etapa 1, ahora apuntando a `sc->tq`) y el 6 (que es nuevo en la etapa 2).

Una pregunta natural: ¿necesitamos el `taskqueue_drain` explícito en el paso 4 si el paso 6 va a drenar todo igualmente? Técnicamente, no. `taskqueue_free` drena todas las tareas pendientes antes de destruir la cola. Pero mantener el drain explícito tiene dos ventajas. La primera es que hace el orden explícito: ves que el drain de la tarea ocurre antes de `seldrain`, que es el orden que nos importa. La segunda es que separa la pregunta "esperar a que esta tarea específica termine" de la pregunta "desmontar la cola completa". Si etapas posteriores añaden más tareas a la misma cola, cada una obtiene su propio drain explícito, y el código le indica al lector qué está ocurriendo.

El código relevante en `myfirst_detach`:

```c
/* Chapter 13: drain every callout. No lock held; safe to sleep. */
MYFIRST_CO_DRAIN(&sc->heartbeat_co);
MYFIRST_CO_DRAIN(&sc->watchdog_co);
MYFIRST_CO_DRAIN(&sc->tick_source_co);

/* Chapter 14 Stage 1: drain every task. */
taskqueue_drain(sc->tq, &sc->selwake_task);

seldrain(&sc->rsel);
seldrain(&sc->wsel);

/* Chapter 14 Stage 2: destroy the private taskqueue. */
taskqueue_free(sc->tq);
sc->tq = NULL;
```

Asignar `NULL` a `sc->tq` después de liberarlo es una medida defensiva: un bug posterior que intente usar un puntero tras la liberación desreferenciará `NULL` y provocará un pánico en el punto de llamada, en lugar de corromper memoria no relacionada. No tiene ningún coste y de vez en cuando te ahorra una tarde entera de depuración.

### El camino de error del attach

Recorre con cuidado el camino de error del attach. El attach del Capítulo 13 tenía etiquetas para los caminos de error del cbuf y el mutex. La etapa 2 añade etiquetas relacionadas con el taskqueue:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;
        struct make_dev_args args;
        int error;

        sc = device_get_softc(dev);
        sc->dev = dev;
        sc->unit = device_get_unit(dev);

        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);
        cv_init(&sc->data_cv, "myfirst data");
        cv_init(&sc->room_cv, "myfirst room");
        sx_init(&sc->cfg_sx, "myfirst cfg");

        sc->tq = taskqueue_create("myfirst taskq", M_WAITOK,
            taskqueue_thread_enqueue, &sc->tq);
        if (sc->tq == NULL) {
                error = ENOMEM;
                goto fail_sx;
        }
        error = taskqueue_start_threads(&sc->tq, 1, PWAIT,
            "%s taskq", device_get_nameunit(dev));
        if (error != 0)
                goto fail_tq;

        MYFIRST_CO_INIT(sc, &sc->heartbeat_co);
        MYFIRST_CO_INIT(sc, &sc->watchdog_co);
        MYFIRST_CO_INIT(sc, &sc->tick_source_co);

        TASK_INIT(&sc->selwake_task, 0, myfirst_selwake_task, sc);

        /* ... rest of attach as in Chapter 13 ... */

        return (0);

fail_cb:
        cbuf_destroy(&sc->cb);
fail_tq:
        taskqueue_free(sc->tq);
fail_sx:
        cv_destroy(&sc->data_cv);
        cv_destroy(&sc->room_cv);
        sx_destroy(&sc->cfg_sx);
        mtx_destroy(&sc->mtx);
        sc->is_attached = 0;
        return (error);
}
```

Las etiquetas de error se encadenan: `fail_cb` llama a `cbuf_destroy` y cae en `fail_tq`, que llama a `taskqueue_free` y cae en `fail_sx`, que destruye las cvs, el sx y el mutex. Cada etiqueta deshace todo lo hecho hasta el punto en que la llamada de inicialización correspondiente tuvo éxito. Si `taskqueue_start_threads` falla, vamos directamente a `fail_tq` (el taskqueue se asignó pero no tiene threads; `taskqueue_free` lo gestiona correctamente porque un taskqueue recién creado y no iniciado tiene cero threads que recoger).

Nota también: `TASK_INIT` no tiene modo de fallo (es una macro que establece campos) y no necesita una contraparte de destrucción. La tarea queda inactiva una vez que se ha llamado a `taskqueue_drain` sobre ella, y el almacenamiento simplemente se recupera con el softc.

### Convenciones de nomenclatura de threads

`taskqueue_start_threads` acepta una cadena de formato y una lista de argumentos variables, por lo que cada thread obtiene su propio nombre. La convención de nomenclatura tiene un efecto real en la depurabilidad, así que merece la pena dedicar un breve párrafo a las convenciones.

La cadena de formato que usamos es `"%s taskq"` con `device_get_nameunit(dev)` como argumento. Para una primera instancia de `myfirst`, el thread aparece como `myfirst0 taskq`. Para una segunda instancia, aparece como `myfirst1 taskq`. Eso hace que el thread sea identificable en `procstat -t` y `ps ax`.

Un driver con múltiples colas privadas debe elegir nombres que distingan las colas:

```c
taskqueue_start_threads(&sc->tx_tq, 1, PWAIT,
    "%s tx", device_get_nameunit(dev));
taskqueue_start_threads(&sc->rx_tq, 1, PWAIT,
    "%s rx", device_get_nameunit(dev));
```

Los drivers de red suelen nombrar los threads por cola de forma todavía más específica (`"%s tx%d"` con el índice de la cola) para que `procstat -t` muestre el trabajador dedicado de cada cola de hardware.

### Elección del número de threads

La mayoría de los drivers crean un taskqueue privado con un único thread. Un solo trabajador significa que las tareas se ejecutan secuencialmente, lo que simplifica el razonamiento sobre los locks: dentro del callback de la tarea puedes asumir que ninguna otra invocación del mismo callback se está ejecutando de forma concurrente, sin necesidad de exclusión explícita.

Un driver con múltiples canales de hardware que necesiten procesamiento paralelo podría crear varios threads de trabajo en el mismo taskqueue. El taskqueue garantiza que una única tarea se ejecuta en como máximo un thread a la vez (eso es lo que rastrea `tq_active`), pero tareas diferentes en la misma cola pueden ejecutarse en paralelo en threads distintos. Para `myfirst`, la configuración con un único thread es la correcta.

Los taskqueues con múltiples threads tienen implicaciones para la contención de locks: dos trabajadores en la misma cola, cada uno ejecutando una tarea diferente, pueden competir por el mismo mutex del driver. Si la carga de trabajo es naturalmente paralelizable, una cola con múltiples threads la acelera. Si la carga de trabajo está serializada de todas formas en el mutex del driver, los threads adicionales añaden complejidad sin beneficio. Para un primer taskqueue, el modelo de un único thread es el valor por defecto correcto.

### Elección de la prioridad del thread

El argumento `pri` de `taskqueue_start_threads` es la prioridad de planificación del thread. En los ejemplos del Capítulo 14 usamos `PWAIT`. Las opciones disponibles en la práctica son:

- `PWAIT` (valor numérico 76): prioridad ordinaria de driver, equivalente a la prioridad de `taskqueue_thread`.
- `PI_NET` (valor numérico 4): prioridad asociada a la red, utilizada por muchos drivers ethernet.
- `PI_DISK`: constante histórica; territorio de `PRI_MIN_KERN`. Utilizada por drivers de almacenamiento.
- `PRI_MIN_KERN` (valor numérico 48): prioridad genérica de thread del kernel, utilizada cuando las constantes anteriores no se ajustan.

Para un driver cuyo trabajo en las tareas no sea sensible a la latencia, `PWAIT` es suficiente. Para un driver que deba ejecutar sus callbacks de tarea con prontitud incluso bajo carga, a veces está justificado elevar la prioridad acercándola a la de los threads de interrupción. `myfirst` utiliza `PWAIT`.

Si estás escribiendo un driver y no sabes con certeza qué prioridad exige la convención, examina los drivers del mismo tipo en `/usr/src/sys/dev/`. Un driver de almacenamiento que use taskqueues probablemente utiliza `PRI_MIN_KERN` o `PI_DISK`; un driver de red probablemente utiliza `PI_NET`. Seguir el patrón de los drivers existentes es siempre mejor que inventarse una prioridad.

### Un extracto real del código fuente: `ale(4)`

Un driver de ethernet real que usa exactamente el patrón que enseña esta sección. De `/usr/src/sys/dev/ale/if_ale.c`:

```c
/* Create local taskq. */
sc->ale_tq = taskqueue_create_fast("ale_taskq", M_WAITOK,
    taskqueue_thread_enqueue, &sc->ale_tq);
taskqueue_start_threads(&sc->ale_tq, 1, PI_NET, "%s taskq",
    device_get_nameunit(sc->ale_dev));
```

El driver de ethernet `ale` crea un taskqueue rápido (`taskqueue_create_fast`) con un spin mutex, porque necesita poder encolar desde su manejador de interrupción de filtro. Ejecuta un thread con prioridad `PI_NET`, siguiendo la convención de nombre por unidad. La estructura es exactamente la que usamos en `myfirst`, con la elección de fast frente a regular y la prioridad reflejando el contexto del driver.

Del mismo archivo, el camino de desmontaje correspondiente:

```c
taskqueue_drain(sc->ale_tq, &sc->ale_int_task);
/* ... */
taskqueue_free(sc->ale_tq);
```

`taskqueue_drain` sobre la tarea específica y luego `taskqueue_free` sobre la cola. El mismo idioma que usamos nosotros.

Vale la pena leer al menos una vez el código de inicialización y desmontaje de `ale(4)`. Es un driver real, que hace trabajo real, usando el patrón que estás a punto de escribir en tu propio driver. Todos los drivers bajo `/usr/src/sys/dev/` que usan taskqueues tienen una estructura muy similar.

### Prueba de regresión del capítulo 13

La fase 2 no debe romper nada de lo que estableció el capítulo 13. Antes de continuar, vuelve a ejecutar el kit de pruebas de estrés del capítulo 13 con el driver de la fase 2 cargado:

```text
# cd /path/to/examples/part-03/ch13-timers-and-delayed-work/stage4-final
# ./test-all.sh
```

El test debe pasar exactamente igual que al final del capítulo 13. Si no lo hace, la regresión está en algo que la fase 2 cambió; vuelve al código anterior a la fase 2 y busca la diferencia. La causa más común es un punto de llamada a enqueue que no se actualizó (un enqueue que todavía apunta a `taskqueue_thread` en lugar de a `sc->tq`). Esos compilan sin problemas porque la API es la misma, pero producen fallos no relacionados en tiempo de ejecución.

### Observando el taskqueue privado

Con el driver de la fase 2 cargado, `procstat -t` muestra el nuevo thread:

```text
# procstat -t | grep myfirst
  <PID> <THREAD>      0 100 myfirst0 taskq      sleep   -      -   0:00
```

El nombre `myfirst0 taskq` es el nombre de thread por instancia que pedimos en `taskqueue_start_threads`. El estado es `sleep` porque el thread está bloqueado esperando una tarea. El wchan está vacío porque el thread duerme sobre su propia cv, que `procstat` puede mostrar de forma diferente según la versión.

Activa la fuente de tick y observa de nuevo:

```text
# sysctl dev.myfirst.0.tick_source_interval_ms=100
# procstat -t | grep myfirst
  <PID> <THREAD>      0 100 myfirst0 taskq      run     -      -   0:00
```

En breves instantes puede que veas el thread en estado `run` mientras procesa una tarea. La mayor parte del tiempo permanece en `sleep`. Ambos estados son lo esperado.

`ps ax` muestra el mismo thread:

```text
# ps ax | grep 'myfirst.*taskq'
   50  -  IL      0:00.00 [myfirst0 taskq]
```

Los corchetes indican un thread del kernel. El thread está siempre presente mientras el driver está adjunto; desaparece al hacer el detach.

### La lista de comprobación previa a producción para la fase 2

Una breve lista de comprobación para repasar antes de dar la fase 2 por terminada. Cada elemento es una pregunta que deberías poder responder con confianza.

- [ ] ¿Crea `attach` el taskqueue antes que cualquier código que pueda encolar en él?
- [ ] ¿Inicia `attach` al menos un thread de trabajo en el taskqueue antes de que haya código que espere que una tarea se ejecute realmente?
- [ ] ¿Tiene `attach` etiquetas de fallo que llamen a `taskqueue_free` sobre la cola si falla una inicialización posterior?
- [ ] ¿Vacía `detach` cada tarea que posee el driver antes de liberar cualquier estado que esas tareas usen?
- [ ] ¿Llama `detach` a `taskqueue_free` después de vaciar todas las tareas y antes de destruir el mutex?
- [ ] ¿Establece `detach` `sc->tq = NULL` después de liberar, como medida de claridad defensiva?
- [ ] ¿Se ha elegido deliberadamente la prioridad del thread del taskqueue, con una justificación que encaje con el tipo de driver?
- [ ] ¿Es el nombre del thread del taskqueue suficientemente informativo como para que la salida de `procstat -t` sea útil?
- [ ] ¿Apunta cada llamada a `taskqueue_enqueue` a `sc->tq`, y no a `taskqueue_thread` (salvo que la encolada sea en un camino genuinamente compartido por una razón específica)?
- [ ] ¿Coincide cada llamada a `taskqueue_drain` con una `taskqueue_enqueue` sobre la misma cola con la misma tarea?

Un driver que responde afirmativamente a todos los puntos es un driver que gestiona correctamente su taskqueue privado. Uno que no puede hacerlo está probablemente a un paso de un use-after-free en el detach.

### Errores comunes en la fase 2

Tres errores que cometen los principiantes al añadir un taskqueue privado. Cada uno se puede prevenir con un buen hábito.

**Crear el taskqueue después de algo que encola.** Si la encolada ocurre antes de que `taskqueue_create` haya devuelto el control, se desreferencia un puntero `NULL`. Coloca siempre `taskqueue_create` al principio del attach, antes de cualquier código que pueda desencadenar una encolada.

**Olvidar `taskqueue_start_threads`.** Un taskqueue sin threads de trabajo es una cola que acepta encoladas pero nunca ejecuta las callbacks. Las tareas se acumulan en silencio. Si crees que «mi tarea nunca se dispara», comprueba que llamaste a `taskqueue_start_threads`.

**Llamar a `taskqueue_free` sin haber borrado antes `is_attached`.** Si el taskqueue se libera mientras una callback de callout todavía se está ejecutando y podría encolar, la encolada del callout fallará sobre el taskqueue ya liberado. Borra siempre `is_attached`, vacía los callouts, vacía la tarea y luego libera. El orden es lo que lo hace seguro.

La sección 7 recorrerá cada uno de estos errores en vivo, con un ejercicio de romper y reparar sobre un driver deliberadamente mal ordenado. Por ahora, la regla es: sigue el orden de esta sección y el ciclo de vida del taskqueue será correcto.

### Cerrando la sección 4

El driver ahora posee su propio taskqueue. Un thread, un nombre, un ciclo de vida. El attach lo crea e inicia un trabajador; el detach vacía la tarea, libera el taskqueue y deshace la inicialización. Se respeta el orden relativo a los callouts y a selinfo. `procstat -t` muestra el thread con un nombre reconocible. El driver es autosuficiente en su historia de trabajo diferido.

La sección 5 da el siguiente paso: explotar deliberadamente el comportamiento de coalescing para el procesamiento por lotes, introducir la variante `timeout_task` para tareas programadas y analizar cómo se aplica la prioridad cuando una cola contiene varios tipos de tarea.



## Sección 5: Priorización y coalescing del trabajo

Cada tarea que posee tu driver entra en una cola. La cola tiene una política para decidir el orden en que se ejecutan las tareas y qué ocurre cuando la misma tarea se encola dos veces. La sección 5 hace explícita esa política, después muestra cómo usar el contrato de coalescing deliberadamente para procesar trabajo por lotes y, por último, introduce `timeout_task` como el equivalente del callout en el lado del taskqueue.

Esta sección es densa en ideas pero breve en código. Los dos cambios del driver para la fase 3 son una nueva tarea sobre el mismo taskqueue y una timeout task que impulsa una escritura periódica por lotes. El valor está en las reglas que interiorices.

### La regla de ordenación por prioridad

El campo `ta_priority` de `struct task` ordena las tareas dentro de una misma cola. Las tareas con mayor prioridad se ejecutan antes que las de menor prioridad. Una tarea con prioridad 5 que se encola después de una tarea con prioridad 0 se ejecuta antes que la de prioridad 0, aunque esta última haya sido encolada primero.

La prioridad es un entero pequeño sin signo (`uint8_t`, con rango de 0 a 255). La mayoría de los drivers usan la prioridad 0 para todo, con lo cual la cola funciona efectivamente como FIFO. Un driver con urgencias genuinamente distintas para diferentes tareas puede asignar prioridades distintas y dejar que el taskqueue reordene.

Un ejemplo rápido. Supón que un driver tiene dos tareas: una `reset_task` que se recupera de un error de hardware y una `stats_task` que consolida estadísticas acumuladas. Si ambas se encolan en un intervalo corto, el reset debería ejecutarse primero. Asignar a `reset_task` la prioridad 10 y a `stats_task` la prioridad 0 lo consigue. La tarea de reset se ejecuta primero aunque haya sido encolada la última.

Usa las prioridades con moderación. Un driver con diez tipos de tarea distintos y diez prioridades diferentes es más difícil de razonar que un driver con diez tipos de tarea que se ejecutan todos en orden de encolada. Las prioridades existen para diferenciación real, no para ordenar estéticamente.

### La regla del coalescing, revisada

Tal como se vio en la sección 2, y merece repetirse: si una tarea se encola mientras ya está pendiente, el kernel incrementa `ta_pending` y no la enlaza una segunda vez. La callback se ejecuta una sola vez, con el contador de pendientes en el segundo argumento.

El código exacto, de `/usr/src/sys/kern/subr_taskqueue.c`:

```c
if (task->ta_pending) {
        if (__predict_false((flags & TASKQUEUE_FAIL_IF_PENDING) != 0)) {
                TQ_UNLOCK(queue);
                return (EEXIST);
        }
        if (task->ta_pending < USHRT_MAX)
                task->ta_pending++;
        TQ_UNLOCK(queue);
        return (0);
}
```

El contador se satura en `USHRT_MAX` (65535), que es el límite máximo del contador de coalescing. A partir de ahí, las encoladas repetidas se pierden desde el punto de vista del contador, aunque siguen devolviendo éxito. En la práctica nadie alcanza ese límite, porque una tarea que acumula 65535 repeticiones tiene problemas más serios.

La regla del coalescing tiene tres consecuencias que debes tener en cuenta en el diseño.

En primer lugar, **una tarea maneja como máximo «una ejecución por despertar del planificador»**. Si tu modelo de trabajo necesita «una callback por evento», una sola tarea no es adecuada. Necesitas estado por evento.

En segundo lugar, **la callback debe ser capaz de gestionar múltiples eventos en una sola ejecución**. Escribir la callback como si `pending` fuera siempre 1 es un bug que solo aparece bajo carga. Usa el argumento pending deliberadamente, o estructura la callback para que procese todo lo que haya en una cola propiedad del driver hasta que esté vacía.

En tercer lugar, **puedes aprovechar el coalescing para el procesamiento por lotes**. Si un productor encola una tarea una vez por evento y el consumidor procesa un lote por ejecución, el sistema converge naturalmente a la tasa que el consumidor puede sostener. Con carga ligera, el coalescing nunca se activa (un evento, una ejecución). Con carga alta, el coalescing pliega las ráfagas en ejecuciones únicas con lotes más grandes. El comportamiento es autoajustable.

### Un patrón de procesamiento por lotes deliberado: fase 3

La fase 3 añade una segunda tarea al driver: una `bulk_writer_task` que escribe un número fijo de bytes de tick en el buffer en una sola ejecución, impulsada por un callout que encola la tarea periódicamente. El patrón es forzado (el driver real simplemente usaría una fuente de tick más rápida), pero es la demostración más sencilla del procesamiento por lotes deliberado.

La adición al softc:

```c
struct task             bulk_writer_task;
int                     bulk_writer_batch;      /* bytes per firing */
```

El valor predeterminado de `bulk_writer_batch` es cero (desactivado). Un sysctl lo expone para su ajuste.

La callback:

```c
static void
myfirst_bulk_writer_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;
        int batch, written;
        char buf[64];

        MYFIRST_LOCK(sc);
        batch = sc->bulk_writer_batch;
        MYFIRST_UNLOCK(sc);

        if (batch <= 0)
                return;

        batch = MIN(batch, (int)sizeof(buf));
        memset(buf, 'B', batch);

        MYFIRST_LOCK(sc);
        written = (int)cbuf_write(&sc->cb, buf, batch);
        if (written > 0) {
                counter_u64_add(sc->bytes_written, written);
                cv_signal(&sc->data_cv);
        }
        MYFIRST_UNLOCK(sc);

        if (written > 0)
                selwakeup(&sc->rsel);
}
```

Algunas observaciones.

La callback adquiere `sc->mtx`, lee el tamaño del lote y lo libera. Adquirir y liberar dos veces está bien; el trabajo intermedio (memset) no necesita el lock. La segunda adquisición envuelve la operación sobre el cbuf y la actualización del contador. El selwakeup ocurre sin lock, como siempre.

El argumento `pending` no se usa en esta callback sencilla. En un diseño de batching diferente, `pending` informaría a la callback cuántas veces se encoló la tarea y, por tanto, cuánto trabajo se acumuló. Aquí la política de batching es «escribir siempre exactamente `bulk_writer_batch` bytes por ejecución, independientemente de cuántas veces se haya encolado», así que `pending` no interviene.

La callback no comprueba `is_attached`. No necesita hacerlo. El detach vacía la tarea antes de liberar nada de lo que la tarea toca, y `sc->mtx` protege `sc->cb` hasta que el vaciado se completa.

### El coalescing en acción

Para demostrar el coalescing deliberadamente, la fase 3 añade un sysctl `bulk_writer_flood` cuyo escritor intenta encolar `bulk_writer_task` mil veces en un bucle cerrado:

```c
static int
myfirst_sysctl_bulk_writer_flood(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int flood = 0;
        int error, i;

        error = sysctl_handle_int(oidp, &flood, 0, req);
        if (error || req->newptr == NULL)
                return (error);
        if (flood < 1 || flood > 10000)
                return (EINVAL);

        for (i = 0; i < flood; i++)
                taskqueue_enqueue(sc->tq, &sc->bulk_writer_task);
        return (0);
}
```

Ejecútalo:

```text
# sysctl dev.myfirst.0.bulk_writer_batch=32
# sysctl dev.myfirst.0.bulk_writer_flood=1000
```

Inmediatamente después, observa el recuento de bytes. Sin coalescing, mil encoladas de 32 bytes cada una producirían 32000 bytes. Con coalescing, el número real es una sola ejecución de 32 bytes, porque las mil encoladas colapsaron en una única tarea pendiente. El contador `bytes_written` del driver debería aumentar en 32, no en 32000.

Así es como funciona el contrato de coalescing tal como se diseñó. El productor pidió mil ejecuciones de la tarea; el taskqueue entregó una. La única ejecución de la callback reflejó las mil peticiones pero realizó la cantidad fija de trabajo que especificaba la política de batching.

### Uso de `pending` para batching adaptativo

Un patrón más sofisticado usa el argumento `pending` para adaptar el tamaño del lote a la profundidad de la cola. Supón que un driver quiere escribir `pending` bytes por ejecución: un byte por encolada, plegado en ejecuciones con coalescing. La callback pasa a ser:

```c
static void
myfirst_adaptive_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;
        char buf[64];
        int n;

        n = MIN(pending, (int)sizeof(buf));
        memset(buf, 'A', n);

        MYFIRST_LOCK(sc);
        (void)cbuf_write(&sc->cb, buf, n);
        counter_u64_add(sc->bytes_written, n);
        cv_signal(&sc->data_cv);
        MYFIRST_UNLOCK(sc);

        selwakeup(&sc->rsel);
}
```

La callback escribe `pending` bytes (hasta el tamaño del buffer). Con carga baja, `pending` vale 1 y la callback escribe un byte. Con carga alta, `pending` es la profundidad de la cola en el momento en que arrancó la callback, y esta escribe esa cantidad de bytes en una sola pasada. El batching escala naturalmente con la carga.

Este diseño es útil cuando cada encolado corresponde a un evento real que requiere una unidad de trabajo, y el batching es una optimización de rendimiento en lugar de un cambio semántico. El handler de "transmit completion" de un driver de red es un ejemplo clásico: cada paquete transmitido genera una interrupción que encola una task; el cometido de la task es recuperar los descriptores completados; a tasas de paquetes elevadas, muchas interrupciones se funden en una única ejecución de la task que recupera muchos descriptores de una sola vez.

No añadiremos la task de batching adaptativo a `myfirst` en la Etapa 3, porque la versión de batch fijo ya demuestra la coalescencia. El patrón adaptativo merece tenerse en cuenta para el trabajo real con drivers; los drivers de FreeBSD reales que leerás más adelante lo utilizan con frecuencia.

### Los flags de enqueue

`taskqueue_enqueue_flags` extiende `taskqueue_enqueue` con dos bits de flag:

- `TASKQUEUE_FAIL_IF_PENDING`: si la tarea ya está pendiente, devuelve `EEXIST` en lugar de fusionarla.
- `TASKQUEUE_FAIL_IF_CANCELING`: si la tarea está siendo cancelada en ese momento, devuelve `EAGAIN` en lugar de esperar.

`TASKQUEUE_FAIL_IF_PENDING` es útil cuando quieres saber si el enqueue produjo realmente un nuevo estado pendiente, a efectos de contabilidad o depuración. Un driver que cuenta "cuántas veces se ha encolado esta tarea" puede usar el flag, recibir `EEXIST` en las llamadas redundantes y contar solo los enqueues no redundantes.

`TASKQUEUE_FAIL_IF_CANCELING` es útil durante el apagado. Si estás desmontando el driver y algún camino de código intentaría encolar una tarea, puedes pasar el flag y comprobar si se devuelve `EAGAIN` para evitar añadir de nuevo una tarea que está siendo cancelada. La mayoría de los drivers no necesitan esto en la práctica; la comprobación de `is_attached` habitualmente se encarga del caso equivalente.

Ninguno de los dos flags se usa en `myfirst`. Ambos existen, y un driver con necesidades concretas puede recurrir a ellos. Para el trabajo ordinario, el `taskqueue_enqueue` simple es suficiente.

### La variante `timeout_task`

A veces quieres que una tarea se dispare después de un retardo concreto. El callout sería el primitivo natural para ello, pero si el trabajo que quiere realizar el callback diferido requiere contexto de thread, necesitas el contexto de la tarea y no el del callout. El kernel ofrece `struct timeout_task` exactamente para este caso.

`timeout_task` está definida en `/usr/src/sys/sys/_task.h`:

```c
struct timeout_task {
        struct taskqueue *q;
        struct task t;
        struct callout c;
        int    f;
};
```

La estructura envuelve un `struct task`, un `struct callout` y un flag interno. Cuando programas una timeout task con `taskqueue_enqueue_timeout`, el kernel arranca el callout; cuando el callout se dispara, su callback encola la tarea subyacente en el taskqueue. La tarea se ejecuta entonces en contexto de thread, con todas las garantías habituales.

La inicialización usa `TIMEOUT_TASK_INIT`:

```c
TIMEOUT_TASK_INIT(queue, timeout_task, priority, func, context);
```

La macro se expande a una llamada a la función `_timeout_task_init` que inicializa tanto la tarea como el callout con el enlace adecuado. Debes pasar el taskqueue en el momento de la inicialización porque el callout está configurado para encolar en esa cola específica.

La programación usa `taskqueue_enqueue_timeout(tq, &tt, ticks)`:

```c
int taskqueue_enqueue_timeout(struct taskqueue *queue,
    struct timeout_task *timeout_task, int ticks);
```

El argumento `ticks` sigue el mismo convenio que `callout_reset`: `hz` ticks equivale a un segundo.

El drenado usa `taskqueue_drain_timeout(tq, &tt)`, que espera a que el callout expire (o lo cancela si aún está pendiente) y después espera a que la tarea subyacente finalice. El drenado es una sola llamada, pero gestiona tanto la fase del callout como la de la tarea.

La cancelación usa `taskqueue_cancel_timeout(tq, &tt, &pendp)`:

```c
int taskqueue_cancel_timeout(struct taskqueue *queue,
    struct timeout_task *timeout_task, u_int *pendp);
```

Devuelve cero si el timeout se canceló correctamente, o `EBUSY` si la tarea está en ejecución en ese momento. En el caso `EBUSY` lo habitual es continuar con `taskqueue_drain_timeout`.

### Una timeout_task en Stage 3: reinicio diferido

Stage 3 añade una timeout task al driver: un reinicio diferido que se dispara `reset_delay_ms` milisegundos después de que se escriba el sysctl de reinicio. El sysctl de reinicio existente se ejecuta de forma síncrona; la variante diferida programa el reinicio para más adelante. Resulta útil para pruebas y para situaciones en que el reinicio no debe producirse hasta que el I/O en curso haya finalizado.

La adición al softc:

```c
struct timeout_task     reset_delayed_task;
int                     reset_delay_ms;
```

La inicialización en attach:

```c
TIMEOUT_TASK_INIT(sc->tq, &sc->reset_delayed_task, 0,
    myfirst_reset_delayed_task, sc);
sc->reset_delay_ms = 0;
```

`TIMEOUT_TASK_INIT` recibe el taskqueue como primer argumento porque el callout que hay dentro del timeout_task necesita saber en qué cola encolar cuando se dispare.

El callback:

```c
static void
myfirst_reset_delayed_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_LOCK(sc);
        MYFIRST_CFG_XLOCK(sc);

        cbuf_reset(&sc->cb);
        sc->cfg.debug_level = 0;
        counter_u64_zero(sc->bytes_read);
        counter_u64_zero(sc->bytes_written);

        MYFIRST_CFG_XUNLOCK(sc);
        MYFIRST_UNLOCK(sc);

        cv_broadcast(&sc->room_cv);
        device_printf(sc->dev, "delayed reset fired (pending=%d)\n", pending);
}
```

La misma lógica que el reinicio síncrono del capítulo 13, pero en contexto de tarea. Puede adquirir el `cfg_sx` durmiente sin las complicaciones que tendría un callout. El contador `pending` se registra con fines diagnósticos.

El manejador sysctl que arma el reinicio diferido:

```c
static int
myfirst_sysctl_reset_delayed(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int ms = 0;
        int error;

        error = sysctl_handle_int(oidp, &ms, 0, req);
        if (error || req->newptr == NULL)
                return (error);
        if (ms < 0)
                return (EINVAL);
        if (ms == 0) {
                (void)taskqueue_cancel_timeout(sc->tq,
                    &sc->reset_delayed_task, NULL);
                return (0);
        }

        sc->reset_delay_ms = ms;
        taskqueue_enqueue_timeout(sc->tq, &sc->reset_delayed_task,
            (ms * hz + 999) / 1000);
        return (0);
}
```

Una escritura de cero cancela el reinicio diferido pendiente. Cualquier valor positivo programa la tarea para que se dispare tras el número de milisegundos indicado. La conversión de ticks `(ms * hz + 999) / 1000` es la misma conversión de techo que usamos para los callouts.

El camino de detach drena el timeout task:

```c
taskqueue_drain_timeout(sc->tq, &sc->reset_delayed_task);
```

La ubicación del drenado es la misma que la del drenado de una tarea simple: después de que los callouts estén drenados, después de que `is_attached` esté a cero, antes de `seldrain` y antes de `taskqueue_free`.

### Observando el reinicio diferido

Con Stage 3 cargado, arma el reinicio diferido para dentro de tres segundos:

```text
# sysctl dev.myfirst.0.reset_delayed=3000
```

Tres segundos después `dmesg` muestra:

```text
myfirst0: delayed reset fired (pending=1)
```

El `pending=1` confirma que el timeout task se disparó una vez. Ahora ármalo varias veces en rápida sucesión:

```text
# sysctl dev.myfirst.0.reset_delayed=1000
# sysctl dev.myfirst.0.reset_delayed=1000
# sysctl dev.myfirst.0.reset_delayed=1000
```

Un segundo después, solo se produce un reinicio. `dmesg` muestra:

```text
myfirst0: delayed reset fired (pending=1)
```

¿Por qué solo un disparo? Porque `taskqueue_enqueue_timeout` se comporta de forma coherente con `callout_reset`: armar un timeout task pendiente sustituye el plazo anterior. Los tres armados sucesivos producen un único disparo programado. El mismo comportamiento se produciría si usáramos `callout_reset` en un callout simple.

### Cuándo usar `timeout_task` frente a callout más tarea

Un timeout task es el primitivo adecuado cuando quieres una acción diferida en contexto de thread y el retardo es el parámetro principal. Un callout simple con un enqueue de tarea es el primitivo adecuado cuando quieres una acción diferida y el retardo es un detalle de implementación: por ejemplo, cuando el retardo se recalcula dinámicamente en cada ocasión. Ambos funcionan.

Los dos patrones tienen formas ligeramente distintas en el código fuente:

```c
/* timeout_task pattern */
TIMEOUT_TASK_INIT(tq, &tt, 0, fn, ctx);
...
taskqueue_enqueue_timeout(tq, &tt, ticks);
...
taskqueue_drain_timeout(tq, &tt);
```

```c
/* callout + task pattern */
callout_init_mtx(&co, &sc->mtx, 0);
TASK_INIT(&t, 0, fn, ctx);
...
callout_reset(&co, ticks, myfirst_co_fn, sc);
/* in the callout callback: taskqueue_enqueue(tq, &t); */
...
callout_drain(&co);
taskqueue_drain(tq, &t);
```

La versión con timeout_task es más corta porque el kernel ha empaquetado el patrón por ti. La versión con callout+task es más flexible porque el callback del callout puede decidir dinámicamente si encolar la tarea (por ejemplo, en función de condiciones de estado que no existen en el momento de programarla).

Para el reinicio diferido de `myfirst`, el timeout_task es la elección correcta porque la decisión de dispararse se toma en el momento de programar (el escritor del sysctl lo ha solicitado) y nada entre medias cambia esa decisión.

### Ordenación por prioridad entre tipos de tarea

Un driver con múltiples tareas en el mismo taskqueue puede usar prioridades para ordenarlas. Para `myfirst` no lo necesitamos; todas las tareas tienen la misma prioridad. Pero merece la pena entender el patrón para cuando sí lo necesites.

Supongamos que tenemos un `high_priority_reset_task` que debe ejecutarse antes que cualquier otra tarea pendiente. Lo inicializaríamos con una prioridad superior a cero:

```c
TASK_INIT(&sc->high_priority_reset_task, 10,
    myfirst_high_priority_reset_task, sc);
```

Y lo encolaríamos de forma normal:

```c
taskqueue_enqueue(sc->tq, &sc->high_priority_reset_task);
```

Si la cola tiene varias tareas pendientes, entre ellas la nueva y varias tareas de prioridad 0, la nueva se ejecuta primero por su mayor prioridad. La prioridad es una propiedad de la tarea (establecida en la inicialización), no del enqueue (establecida en cada llamada); si una tarea debe ser urgente a veces y otras no, necesitas dos estructuras de tarea con dos prioridades, no una sola tarea que reajustes.

### Una nota sobre la equidad

Un taskqueue con un único thread trabajador ejecuta las tareas estrictamente por orden de prioridad, y los empates se resuelven por orden de enqueue. Un taskqueue con varios threads trabajadores puede ejecutar varias tareas en paralelo; la prioridad sigue ordenando la lista, pero los trabajadores paralelos pueden despachar tareas fuera del orden estricto en situaciones límite. Para la mayoría de los drivers esto no importa.

Si se requiere equidad estricta u ordenación estricta por prioridad, un único trabajador es la elección correcta. Si el rendimiento a costa de un reordenamiento ocasional es aceptable, varios trabajadores son perfectamente válidos. `myfirst` utiliza un único trabajador.

### Cerrando la sección 5

Stage 3 añadió una tarea de agrupación deliberada y un timeout task. La tarea de agrupación demuestra la fusión colapsando mil enqueues en un único disparo; el timeout task demuestra la ejecución diferida en contexto de thread. Ambas comparten el taskqueue privado de Stage 2, ambas se drenan en detach en el orden establecido y ambas obedecen la disciplina de locking que usa el resto del driver.

Las reglas de prioridad y fusión son ahora explícitas. La prioridad de una tarea la ordena dentro de la cola; el contador `ta_pending` de una tarea pliega los enqueues redundantes en un único disparo cuyo argumento `pending` lleva la cuenta.

La sección 6 se aleja del proceso de refactorización de `myfirst` para examinar los patrones que aparecen en los drivers reales de FreeBSD. Los modelos mentales se acumulan; el driver no vuelve a cambiar hasta la sección 8.



## Sección 6: patrones del mundo real con taskqueues

Hasta ahora el capítulo 14 ha desarrollado un único driver a través de tres etapas. Los drivers reales de FreeBSD usan taskqueues en un conjunto de formas recurrentes. Esta sección cataloga los patrones, muestra dónde aparece cada uno en `/usr/src/sys/dev/` y explica cuándo recurrir a cada uno. Reconocer los patrones transforma la lectura del código fuente de un driver de un rompecabezas en un ejercicio de vocabulario.

Cada patrón se presenta como una receta breve: el problema, la forma de taskqueue que lo resuelve, un esquema de código y una referencia a un driver real que puedes leer para ver la versión de producción.

### Patrón 1: registro o notificación diferidos desde un contexto límite

**Problema.** Un callback de contexto límite (callout, filtro de interrupción, sección epoch) detecta una condición que debería producir un mensaje de registro o una notificación al espacio de usuario. La llamada de registro es demasiado pesada para el contexto límite: `selwakeup`, `log(9)`, `kqueue_user_event` o un `printf` de varias líneas que mantiene un lock que el contexto límite no puede permitirse.

**Solución.** Un `struct task` por condición, inicializado en attach con un callback que realiza la llamada pesada en contexto de thread. El callback de contexto límite registra la condición en el estado del softc (un flag, un contador, un pequeño dato), encola la tarea y retorna. La tarea se ejecuta en contexto de thread, lee la condición del estado del softc, realiza la llamada y limpia la condición.

**Esquema de código.**

```c
struct my_softc {
        struct task log_task;
        int         log_flags;
        struct mtx  mtx;
        ...
};

#define MY_LOG_UNDERRUN  0x01
#define MY_LOG_OVERRUN   0x02

static void
my_log_task(void *arg, int pending)
{
        struct my_softc *sc = arg;
        int flags;

        mtx_lock(&sc->mtx);
        flags = sc->log_flags;
        sc->log_flags = 0;
        mtx_unlock(&sc->mtx);

        if (flags & MY_LOG_UNDERRUN)
                log(LOG_WARNING, "%s: buffer underrun\n",
                    device_get_nameunit(sc->dev));
        if (flags & MY_LOG_OVERRUN)
                log(LOG_WARNING, "%s: buffer overrun\n",
                    device_get_nameunit(sc->dev));
}

/* In an interrupt or callout callback: */
if (some_condition) {
        sc->log_flags |= MY_LOG_UNDERRUN;
        taskqueue_enqueue(sc->tq, &sc->log_task);
}
```

El campo de flags permite que el contexto límite acumule múltiples condiciones distintas antes de que se ejecute la tarea. Cuando la tarea se dispara, captura una instantánea de los flags, los limpia y emite una línea de registro por condición. La fusión pliega los enqueues de la misma condición repetida en una única invocación del callback, que es lo que se desea para evitar el spam en el registro.

**Ejemplo real.** `/usr/src/sys/dev/ale/if_ale.c` usa una tarea de interrupción (`sc->ale_int_task`) para gestionar el trabajo diferido del filtro de interrupción, incluidas las condiciones que quieren registrar o notificar.

### Patrón 2: reinicio o reconfiguración diferidos

**Problema.** El driver detecta una condición que requiere un reinicio del hardware o un cambio de configuración, pero el reinicio no debe producirse de inmediato. Las razones del retardo incluyen "dar al I/O en vuelo la oportunidad de completarse", "agrupar múltiples causas en un único reinicio" o "limitar los reinicios para evitar una tormenta de reinicios".

**Solución.** Un `struct timeout_task` (o un `struct callout` emparejado con un `struct task`). El detector encola el timeout task con el retardo elegido. Si la condición desaparece antes de que transcurra el retardo, el detector cancela el timeout task. Si la condición persiste, la tarea se dispara en contexto de thread y realiza el reinicio.

**Esquema de código.** La misma forma que la tarea de reinicio diferido de `myfirst` en Stage 3. La única variación es que el detector habitualmente cancela la tarea pendiente cada vez que cambia el estado "necesita reiniciarse", de modo que el reinicio solo ocurre cuando la condición ha persistido durante todo el retardo.

**Ejemplo real.** Muchos drivers de almacenamiento y de red usan este patrón para la recuperación. El driver Broadcom `/usr/src/sys/dev/bge/if_bge.c` usa timeout tasks para la reevaluación del estado del enlace tras un evento de capa física.

### Patrón 3: procesamiento posterior a la interrupción (la división filtro + tarea)

**Problema.** Llega una interrupción de hardware. El trabajo del filtro de interrupción es decidir "¿es esta nuestra interrupción y el hardware realmente necesitaba atención?". El filtro debe ejecutarse rápidamente y no debe dormir. El procesamiento real (leer registros, atender colas de finalización, posiblemente volcar resultados al espacio de usuario con `copyout`) no pertenece al filtro.

**Solución.** Una división en dos niveles. El handler del filtro se ejecuta de forma síncrona, lee un registro de estado, decide si la interrupción nos pertenece y, en caso afirmativo, encola una tarea. La tarea se ejecuta en contexto de thread y realiza el trabajo real. Esta es la división estándar filtro más ithread que `bus_setup_intr(9)` soporta de forma nativa, pero la variante con taskqueue resulta útil cuando el driver quiere más control sobre el contexto diferido del que proporciona ithread.

**Esbozo de código.**

```c
static int
my_intr_filter(void *arg)
{
        struct my_softc *sc = arg;
        uint32_t status;

        status = CSR_READ_4(sc, STATUS_REG);
        if (status == 0)
                return (FILTER_STRAY);

        /* Mask further interrupts from the hardware. */
        CSR_WRITE_4(sc, INTR_MASK_REG, 0);

        taskqueue_enqueue(sc->tq, &sc->intr_task);
        return (FILTER_HANDLED);
}

static void
my_intr_task(void *arg, int pending)
{
        struct my_softc *sc = arg;

        mtx_lock(&sc->mtx);
        my_process_completions(sc);
        mtx_unlock(&sc->mtx);

        /* Unmask interrupts again. */
        CSR_WRITE_4(sc, INTR_MASK_REG, ALL_INTERRUPTS);
}
```

Hay varios detalles importantes. El filtro enmascara la interrupción a nivel de hardware antes de encolar la tarea, de modo que el hardware no continúa disparando mientras la tarea está pendiente. La tarea se ejecuta en contexto de thread, procesa las finalizaciones y vuelve a habilitar las interrupciones al terminar. La coalescencia agrupa varias interrupciones en un único disparo de tarea; el enmascaramiento evita que el hardware dispare de forma ilimitada. La Parte 4 recorrerá en detalle la configuración real de interrupciones; el patrón mostrado aquí es la forma que este capítulo te prepara para reconocer.

**Ejemplo real.** `/usr/src/sys/dev/ale/if_ale.c`, `/usr/src/sys/dev/age/if_age.c` y la mayoría de los drivers Ethernet utilizan este patrón o una variante muy próxima.

### Patrón 4: `copyin`/`copyout` asíncrono tras la finalización del hardware

**Problema.** El driver tiene una petición de espacio de usuario en cola que proporcionó direcciones para los datos de entrada o salida. La finalización del hardware llega como una interrupción. El driver debe copiar los datos entre el espacio de usuario y los buffers del kernel para finalizar la petición. `copyin` y `copyout` duermen en su camino lento, por lo que no pueden ejecutarse en contexto de interrupción.

**Solución.** El camino de la interrupción registra el identificador de la petición y encola una tarea. La tarea se ejecuta en contexto de thread, identifica las direcciones del espacio de usuario a partir del estado de la petición almacenado, realiza el `copyin` o `copyout` y despierta al thread de usuario que espera.

**Esquema de código.**

```c
struct my_request {
        struct task finish_task;
        struct proc *proc;
        void *uaddr;
        void *kaddr;
        size_t len;
        int done;
        struct cv cv;
        /* ... */
};

static void
my_finish_task(void *arg, int pending)
{
        struct my_request *req = arg;

        (void)copyout(req->kaddr, req->uaddr, req->len);

        mtx_lock(&req->sc->mtx);
        req->done = 1;
        cv_broadcast(&req->cv);
        mtx_unlock(&req->sc->mtx);
}

/* In the interrupt task: */
taskqueue_enqueue(sc->tq, &req->finish_task);
```

El thread de espacio de usuario espera en `req->cv` tras enviar la petición; se despierta cuando la tarea marca `done` y emite un broadcast.

**Ejemplo real.** Los drivers de dispositivos de caracteres que implementan ioctls con transferencias de datos grandes emplean a veces este patrón. La finalización de transferencias bulk en USB en `/usr/src/sys/dev/usb/` pospone con frecuencia la copia de datos del espacio de usuario mediante tareas.

### Patrón 5: reintento con backoff tras un fallo transitorio

**Problema.** Una operación de hardware ha fallado, pero el fallo se sabe que es transitorio. El driver quiere reintentar tras un intervalo de backoff, con un backoff creciente en fallos repetidos.

**Solución.** Un `struct timeout_task` que se rearma con un retardo creciente en cada fallo. El callback de la tarea realiza el reintento; en caso de éxito, el driver borra el backoff; en caso de fallo, la tarea se reencola con un retardo mayor.

**Esquema de código.**

```c
struct my_softc {
        struct timeout_task retry_task;
        int retry_interval_ms;
        int retry_attempts;
        /* ... */
};

static void
my_retry_task(void *arg, int pending)
{
        struct my_softc *sc = arg;
        int err;

        err = my_attempt_operation(sc);
        if (err == 0) {
                sc->retry_attempts = 0;
                sc->retry_interval_ms = 10;
                return;
        }

        sc->retry_attempts++;
        if (sc->retry_attempts > MAX_RETRIES) {
                device_printf(sc->dev, "giving up after %d attempts\n",
                    sc->retry_attempts);
                return;
        }

        sc->retry_interval_ms = MIN(sc->retry_interval_ms * 2, 5000);
        taskqueue_enqueue_timeout(sc->tq, &sc->retry_task,
            (sc->retry_interval_ms * hz + 999) / 1000);
}
```

El intervalo inicial es de 10 ms, se duplica en cada fallo y tiene un tope de 5 segundos, con un número máximo de intentos. El reintento sigue disparándose hasta que hay éxito o se abandona. Un camino de código independiente puede cancelar el reintento (con `taskqueue_cancel_timeout`) si cambian las condiciones que lo motivaron.

**Ejemplo real.** `/usr/src/sys/dev/iwm/if_iwm.c` y otros drivers inalámbricos usan timeout tasks para reintentos de carga de firmware y recalibración de enlace.

### Patrón 6: desmontaje diferido

**Problema.** Un objeto dentro del driver debe liberarse, pero algún otro camino de código puede seguir manteniendo una referencia. Liberarlo de inmediato sería un use-after-free. El driver necesita liberarlo más tarde, una vez que se sabe que las referencias han desaparecido.

**Solución.** Un `struct task` cuyo callback libera el objeto. El camino de código que quiere liberar el objeto encola la tarea; la tarea se ejecuta en contexto de thread, después de que las referencias pendientes hayan tenido la oportunidad de completarse.

En formas más elaboradas, el patrón usa conteo de referencias: la tarea decrementa un contador de referencias y libera el objeto solo cuando el contador llega a cero. En formas más simples, el orden FIFO del taskqueue es suficiente: todas las tareas encoladas antes se completan antes de que se ejecute la tarea de desmontaje, de modo que si las referencias siempre se toman dentro de tareas, todas habrán desaparecido cuando se dispare la tarea de desmontaje.

**Esquema de código.**

```c
static void
my_free_task(void *arg, int pending)
{
        struct my_object *obj = arg;

        /* All earlier tasks on this queue have completed. */
        free(obj, M_DEVBUF);
}

/* When we want to free the object: */
static struct task free_task;
TASK_INIT(&free_task, 0, my_free_task, obj);
taskqueue_enqueue(sc->tq, &free_task);
```

Advertencia: el propio `struct task` debe vivir hasta que se dispare el callback, lo que significa embeberlo en el objeto (y liberar la estructura contenedora) o asignarlo por separado.

**Ejemplo real.** `/usr/src/sys/dev/usb/usb_hub.c` usa el desmontaje diferido cuando se elimina un dispositivo USB mientras todavía está siendo usado por un driver en la parte superior de la pila.

### Patrón 7: renovación de estadísticas programada

**Problema.** El driver mantiene estadísticas acumuladas que deben convertirse en una tasa por intervalo en límites regulares. La renovación implica tomar una instantánea de los contadores, calcular el delta y almacenar el resultado en un buffer circular. Esto puede hacerse en un callback de timer, pero el cálculo toca estructuras de datos protegidas por un lock sleepable.

**Solución.** Un `timeout_task` periódico que gestiona la renovación en contexto de thread. La tarea se reencola al final de cada disparo para el siguiente intervalo.

Esto es, en efecto, un callout basado en taskqueue. Es algo más pesado que un callout simple porque monta sobre la combinación de callout y tarea, pero puede hacer cosas que un callout simple no puede. Útil solo cuando el callout por sí solo no es suficiente.

**Esquema de código.**

```c
static void
my_stats_rollover_task(void *arg, int pending)
{
        struct my_softc *sc = arg;

        sx_xlock(&sc->stats_sx);
        my_rollover_stats(sc);
        sx_xunlock(&sc->stats_sx);

        taskqueue_enqueue_timeout(sc->tq, &sc->stats_task, hz);
}
```

El reencole propio al final mantiene la tarea disparándose una vez por segundo. Un camino de control que quiera detener la renovación cancela el timeout task.

**Ejemplo real.** Varios drivers de red usan este patrón para timers adyacentes al watchdog cuyo trabajo necesita un lock sleepable.

### Patrón 8: `taskqueue_block` durante una configuración delicada

**Problema.** El driver está realizando un cambio de configuración que no debe ser interrumpido por la ejecución de tareas. Una tarea que se dispare en mitad de la configuración podría observar un estado inconsistente.

**Solución.** `taskqueue_block(sc->tq)` antes del cambio de configuración; `taskqueue_unblock(sc->tq)` después. Mientras está bloqueado, los nuevos encolados se acumulan pero no se despacha ninguna tarea. La tarea que ya está en ejecución (si la hay) se completa de forma natural antes de que el bloqueo surta efecto.

**Esquema de código.**

```c
taskqueue_block(sc->tq);
/* ... reconfigure ... */
taskqueue_unblock(sc->tq);
```

`taskqueue_block` es rápido. No drena las tareas en ejecución; solo impide el despacho de nuevas. Para garantizar que ninguna tarea esté ejecutándose en ese momento, se combina con `taskqueue_quiesce`:

```c
taskqueue_block(sc->tq);
taskqueue_quiesce(sc->tq);
/* ... reconfigure ... */
taskqueue_unblock(sc->tq);
```

`taskqueue_quiesce` espera a que la tarea en ejecución termine y a que la cola pendiente se vacíe. Combinado con `block`, tienes la garantía de que ninguna tarea está en ejecución y ninguna tarea comenzará hasta que desbloquees.

**Ejemplo real.** Algunos drivers Ethernet usan este patrón durante las transiciones de estado de la interfaz (enlace activo, enlace caído, cambio de medio).

### Patrón 9: `taskqueue_drain_all` en un límite de subsistema

**Problema.** Un subsistema complejo quiere estar completamente en reposo en un punto específico. Todas sus tareas pendientes, incluidas las que hayan podido ser encoladas por otras tareas pendientes, deben completarse antes de que el subsistema continúe.

**Solución.** `taskqueue_drain_all(tq)` drena cada tarea de la cola, espera a que cada tarea en vuelo se complete y retorna cuando la cola está en reposo.

`taskqueue_drain_all` no sustituye a `taskqueue_drain` por tarea en el detach (porque la cola puede tener tareas de otros caminos que no deben drenarse), pero es útil para puntos de sincronización internos donde se quiere "todo está hecho, punto final".

**Ejemplo real.** `/usr/src/sys/dev/wg/if_wg.c` usa `taskqueue_drain_all` en su taskqueue por peer durante la limpieza del peer.

### Patrón 10: generación de eventos sintéticos de nivel de simulación

**Problema.** Durante las pruebas, el driver quiere generar eventos sintéticos que ejerciten el camino completo de procesamiento de eventos. Una llamada a función directa omitiría el planificador, pasaría por alto las condiciones de carrera y no pondría a prueba el mecanismo del taskqueue. Un evento de hardware real no está disponible en un banco de pruebas.

**Solución.** Un handler de sysctl que encola una tarea. El callback de la tarea invoca la misma rutina del driver que habría invocado un evento real. Dado que la tarea pasa por el taskqueue, el evento sintético tiene la misma forma de ejecución que uno real: se ejecuta en contexto de thread, observa el mismo locking y pasa por el mismo mecanismo de fusión.

Esto es exactamente lo que hace el sysctl `bulk_writer_flood` de `myfirst`. El patrón se transfiere a cualquier driver que quiera autoprobarse los caminos de trabajo diferido sin necesitar hardware real que genere el evento disparador.

### Una selección de drivers reales

Los patrones anteriores no se han inventado para el capítulo. Un breve recorrido por `/usr/src/sys/dev/` que deberías explorar por tu cuenta, con un orden sugerido:

- **`/usr/src/sys/dev/ale/if_ale.c`**: Un driver Ethernet pequeño y legible que usa un taskqueue privado, una división filter-plus-task y una única interrupt task. Buena primera lectura.
- **`/usr/src/sys/dev/age/if_age.c`**: Patrón similar, familia de drivers ligeramente diferente. Leer ambos refuerza el patrón.
- **`/usr/src/sys/dev/bge/if_bge.c`**: Un driver Ethernet más grande con múltiples tareas (interrupt task, link task, reset task). Muestra cómo se componen varias tareas en una sola cola.
- **`/usr/src/sys/dev/usb/usb_process.c`**: La cola de proceso por dispositivo de USB (`usb_proc_*`). Demuestra cómo un subsistema envuelve el trabajo diferido estilo tarea para su propio dominio.
- **`/usr/src/sys/dev/wg/if_wg.c`**: WireGuard usa grouptaskqueues para el cifrado por peer. Lectura avanzada, pero útil una vez que los patrones básicos encajan.
- **`/usr/src/sys/dev/iwm/if_iwm.c`**: Driver inalámbrico con múltiples timeout tasks para calibración, escaneo y gestión de firmware.
- **`/usr/src/sys/kern/subr_taskqueue.c`**: La implementación en sí. Leer `taskqueue_run_locked` una vez hace que todo lo demás sea concreto.

Veinte minutos con cualquiera de esos archivos equivalen a una hora de explicaciones del capítulo que puedes ahorrarte. Los patrones son visibles de un vistazo una vez que sabes qué buscar.

### Cerrando la sección 6

La misma API pequeña se compone en una gran familia de patrones. Registro diferido, interrupciones filter-plus-task, `copyin`/`copyout` asíncrono, reintento con backoff, desmontaje diferido, renovación de estadísticas, `block` durante la reconfiguración, `drain_all` en los límites del subsistema, generación de eventos sintéticos: cada patrón es una variación de "el borde detecta, la tarea actúa", y cada uno resulta productivo cuando el driver que estás escribiendo o leyendo encaja en esa forma.

La sección 7 aborda el otro lado de la misma moneda: cuando el patrón falla, ¿cómo lo ves? ¿Qué herramientas proporciona FreeBSD para inspeccionar el estado de un taskqueue y cuáles son los errores comunes que esas herramientas ayudan a diagnosticar?



## Sección 7: Depuración de taskqueues

La mayor parte del código de taskqueue es breve. Los errores comunes no son sutiles: tareas que nunca se ejecutan, tareas que se ejecutan demasiado a menudo, tareas que se ejecutan después de que se haya liberado el softc, deadlocks contra el mutex del driver y drenado en el punto incorrecto de la secuencia de detach. Esta sección nombra los errores, muestra cómo observarlos y recorre una ruptura y corrección deliberada en `myfirst` para que puedas practicar el flujo de trabajo de depuración con algo delante.

### Las herramientas

Un breve repaso de las herramientas a las que recurrirás.

**`procstat -t`**: lista cada thread del kernel con su nombre, prioridad, estado y canal de espera. El thread trabajador del taskqueue privado aparece como `<nombre> taskq`, donde `<nombre>` es lo que pasaste a `taskqueue_start_threads`. Un thread atascado en un canal de espera no trivial es una pista: el nombre del canal suele indicar qué está esperando el thread.

**`ps ax`**: equivalente para la mayor parte de lo que muestra `procstat -t`, con una salida menos específica del taskqueue. El nombre del thread del kernel aparece entre corchetes.

**`sysctl dev.<driver>`**: el árbol de sysctl propio del driver. Si añadiste un contador como `selwake_pending_drops`, su valor es visible aquí. Los sysctls de diagnóstico son la forma más barata de observabilidad; añádelos siempre que la pregunta "¿con qué frecuencia se dispara este camino?" pueda ser relevante más adelante.

**`dtrace(1)`**: el framework de trazado del kernel. La actividad del taskqueue se puede trazar mediante sondas FBT (function boundary tracing) en `taskqueue_enqueue` y `taskqueue_run_locked`. Un script D breve puede contar los encolados, medir los retardos entre el encolado y el despacho, etcétera.

**`ktr(4)`**: el trazador de eventos del kernel. Habilitado en tiempo de compilación en kernels de depuración, proporciona un buffer circular de eventos del kernel que puede volcarse tras un crash o inspeccionarse en vivo. Útil para el análisis post-mortem.

**`ddb(4)`**: el depurador integrado en el kernel. Puntos de interrupción, trazas de pila, inspección de memoria. Accesible mediante `kgdb` tras un kernel panic, o de forma interactiva tras un `sysctl debug.kdb.enter=1` si compilaste un kernel con KDB habilitado.

**`INVARIANTS` y `WITNESS`**: aserciones en tiempo de compilación y comprobador del orden de los locks. No son herramientas que se invoquen, sino la primera línea de defensa. Un kernel de depuración detecta la mayoría de los errores de taskqueue la primera vez que los encuentras.

Los laboratorios del capítulo 14 ejercitan explícitamente `procstat -t`, `sysctl` y `dtrace`. `ktr` y `ddb` se mencionan por completitud.

### Error común 1: la tarea nunca se ejecuta

**Síntomas.** Encolas una tarea desde un handler de sysctl o un callback de callout; el `device_printf` del callback de la tarea nunca aparece; el driver por lo demás parece funcionar.

**Causa probable.** `taskqueue_start_threads` no fue llamado, o el puntero al taskqueue sobre el que encolaste es `NULL`.

**Cómo comprobarlo.**

```text
# procstat -t | grep myfirst
```

Si no aparece ningún thread llamado `myfirst taskq`, el taskqueue no existe o no tiene threads asignados. Revisa la ruta de attach: ¿se llama a `taskqueue_create`? ¿Se almacena su valor de retorno? ¿Se llama a `taskqueue_start_threads` después?

```text
# dtrace -n 'fbt::taskqueue_enqueue:entry /arg0 != 0/ { @[stack()] = count(); }'
```

Si el stack trace muestra encolados en el taskqueue del driver, la tarea se está enviando. Si no aparece nada, la ruta de código que debería encolar no se está alcanzando. Remóntate por el código y averigua por qué.

### Error frecuente 2: La tarea se ejecuta demasiadas veces

**Síntomas.** El callback de la tarea hace más trabajo del esperado, o el driver registra un contador extraño.

**Causa probable.** El callback no respeta el argumento `pending`, o el callback se pone en cola a sí mismo sin condición, de modo que una vez iniciado entra en un bucle infinito.

**Cómo comprobarlo.** Añade un contador al callback:

```c
static void
myfirst_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;
        static int invocations;

        atomic_add_int(&invocations, 1);
        if ((invocations % 1000) == 0)
                device_printf(sc->dev, "task invocations=%d\n", invocations);
        /* ... */
}
```

Si el contador crece más rápido que la tasa de disparo esperada, la tarea está entrando en bucle o no se está produciendo la coalescencia. Inspecciona los lugares del código donde se encola.

### Error frecuente 3: Uso después de liberación en el callback de la tarea

**Síntomas.** El kernel entra en pánico con una traza de pila que termina en el callback de tu tarea, en un punto donde se accede al estado del softc. El pánico puede ocurrir durante el detach o poco después.

**Causa probable.** La ruta de detach liberó el softc (o algo que la tarea utiliza) antes de drenar la tarea. Una encola tardía desde un callout u otro contexto marginal se disparó después del drenado, y la tarea se ejecutó contra memoria ya liberada.

**Cómo comprobarlo.** Revisa la ruta de detach comparándola con el orden descrito en la Sección 4. En concreto:

1. `is_attached` debe ponerse a cero antes de drenar los callouts, para que los callbacks de callout salgan sin volver a encolar.
2. Los callouts deben drenarse antes que las tareas, para que no se produzcan más encolas después de drenar las tareas.
3. Las tareas deben drenarse antes de liberar el estado que utilizan.
4. `taskqueue_free` debe llamarse después de que todas las tareas de la cola hayan sido drenadas.

Cualquier discrepancia en alguno de esos puntos supone un potencial uso después de liberación.

El kernel de depuración detecta muchos de estos casos mediante las aserciones de `INVARIANTS` en las rutinas `cbuf_*`, `cv_*` y `mtx_*`. Ejecuta la ruta de detach bajo carga con `WITNESS` activado; un error suele aflorar de inmediato.

### Error frecuente 4: Deadlock entre la tarea y el mutex del driver

**Síntomas.** Una llamada `read(2)` o `write(2)` al dispositivo se bloquea indefinidamente. El callback de la tarea está esperando la adquisición de un lock. El mutex del driver está en manos de un thread diferente.

**Causa probable.** El callback de la tarea intenta adquirir un lock que el thread que encola la tarea ya posee, creando un ciclo. Por ejemplo:

- El thread A posee `sc->mtx` y llama a una función que encola una tarea.
- El callback de la tarea adquiere `sc->mtx` antes de hacer su trabajo.
- La tarea no puede avanzar porque el thread A sigue poseyendo el mutex.
- El thread A espera a que la tarea termine.

La parte de «el thread A espera a que la tarea termine» no encaja en la arquitectura de `myfirst` (el driver no espera explícitamente tareas desde rutas que mantienen el mutex), pero es un patrón habitual en otros drivers. Evítalo no drenando tareas mientras se mantiene un lock que necesitan.

**Cómo comprobarlo.**

```text
# procstat -kk <pid of stuck read/write thread>
# procstat -kk <pid of taskqueue thread>
```

Compara las trazas de pila. Si una muestra `mtx_lock`/`sx_xlock` en el callback de la tarea y la otra muestra `msleep_sbt`/`sleepqueue` en un punto que mantiene el mismo lock, tienes un deadlock.

### Error frecuente 5: El drenado se bloquea indefinidamente

**Síntomas.** El detach se bloquea, `kldunload` no retorna. El thread del taskqueue está atascado en algún punto.

**Causa probable.** El callback de la tarea está esperando una condición que no puede satisfacerse porque la ruta de drenado bloquea al productor. O el callback de la tarea está esperando un lock que la ruta de detach posee.

**Cómo comprobarlo.**

```text
# procstat -kk <pid of kldunload>
# procstat -kk <pid of taskqueue thread>
```

El drenado está en `taskqueue_drain`, que está en `msleep`. La tarea está en alguna espera. Identifica el canal de espera; el nombre suele indicar en qué está bloqueada la tarea. Si la tarea está bloqueada en algo que la ruta de detach posee, el diseño tiene un ciclo.

Un caso concreto frecuente: el callback de la tarea llama a `seldrain`, la ruta de detach también llama a `seldrain`, y ambas colisionan. Evítalo asegurándote de que `seldrain` se llama exactamente una vez, en la ruta de detach, después de drenar las tareas.

### El ejercicio de romper y arreglar

Un recorrido deliberado por la introducción y corrección de errores. El driver de la Etapa 1 es correcto; lo modificamos para introducir cada uno de los errores anteriores, observamos el síntoma y lo corregimos.

#### Variante rota 1: falta `taskqueue_start_threads`

Elimina la llamada a `taskqueue_start_threads` del attach. Recompila, carga, activa el tick source y ejecuta el `poll_waiter`. Observarás que no aparece ningún dato en `poll_waiter`, aunque `sysctl dev.myfirst.0.tick_source_interval_ms` esté configurado.

Comprueba `procstat -t`:

```text
# procstat -t | grep myfirst
```

No aparece ningún thread `myfirst taskq`. El taskqueue existe (tú lo creaste) pero no tiene trabajadores. La `selwake_task` encolada permanece en la cola indefinidamente.

Corrección: devuelve la llamada a `taskqueue_start_threads`. Recompila. Confirma que el thread aparece en `procstat -t` y que el `poll_waiter` recibe datos.

#### Variante rota 2: drenar en el orden equivocado

Mueve la llamada a `taskqueue_drain` en el detach para que ocurra antes del drenado de los callouts:

```c
/* WRONG ORDER: */
taskqueue_drain(sc->tq, &sc->selwake_task);
MYFIRST_CO_DRAIN(&sc->heartbeat_co);
MYFIRST_CO_DRAIN(&sc->watchdog_co);
MYFIRST_CO_DRAIN(&sc->tick_source_co);
seldrain(&sc->rsel);
seldrain(&sc->wsel);
```

Recompila, carga, activa el tick source a una tasa alta, deja fluir datos durante unos segundos y después descarga. La mayoría de las veces la descarga funciona. Ocasionalmente, la descarga provoca un pánico con una traza en `selwakeup` siendo llamado después de `seldrain`. La condición de carrera es poco frecuente, pero real.

El problema: `taskqueue_drain` retornó, pero luego un callout `tick_source` en vuelo se disparó (aún no había sido drenado) y volvió a encolar la tarea. La nueva tarea se disparó después de que `seldrain` hubiera corrido, e intentó llamar a `selwakeup` sobre un selinfo ya drenado.

Corrección: restaura el orden correcto (primero los callouts, luego las tareas y después `seldrain`). Recompila y verifica que la condición de carrera ha desaparecido bajo la misma carga.

#### Variante rota 3: el callback de la tarea mantiene el mutex demasiado tiempo

Cambia `myfirst_selwake_task` para que mantenga el mutex durante `selwakeup`:

```c
static void
myfirst_selwake_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_LOCK(sc);        /* WRONG: holds mutex across selwakeup */
        selwakeup(&sc->rsel);
        MYFIRST_UNLOCK(sc);
}
```

Recompila. Carga con un kernel de depuración. Activa el tick source. En cuestión de segundos el kernel entra en pánico con una queja de `WITNESS` sobre el orden de los locks (o en algunas configuraciones con un fallo de aserción en el propio `selwakeup`).

El problema: `selwakeup` adquiere un lock que no está en el orden de locks documentado del driver. `WITNESS` lo detecta y emite una queja.

Corrección: la versión correcta de `myfirst_selwake_task` llama a `selwakeup` sin mantener ningún mutex del driver. Restáuralo, recompila y verifica que no aparecen avisos de WITNESS.

#### Variante rota 4: olvidar drenar la tarea en el detach

Elimina la línea `taskqueue_drain(sc->tq, &sc->selwake_task)` del detach. Recompila. Carga, activa el tick source a una tasa alta, ejecuta el `poll_waiter` y descarga el driver inmediatamente.

La mayoría de las veces la descarga se completa. Ocasionalmente, una tarea que estaba en vuelo en el momento de la descarga se ejecuta contra un softc cuyo selinfo ya ha sido drenado y liberado. El síntoma es habitualmente un pánico del kernel o una corrupción de memoria que aparece más tarde como un fallo aparentemente no relacionado.

Corrección: restaura el drenado. Recompila y verifica que cargar y descargar repetidamente bajo carga es estable.

#### Variante rota 5: puntero de taskqueue incorrecto

Un error sutil de la Etapa 2. Tras pasar al taskqueue privado, se olvida actualizar la llamada a `taskqueue_drain` en el detach. Todavía apunta a `taskqueue_thread`:

```c
/* WRONG: enqueue on sc->tq but drain on taskqueue_thread */
taskqueue_enqueue(sc->tq, &sc->selwake_task);
/* ... in detach ... */
taskqueue_drain(taskqueue_thread, &sc->selwake_task);
```

Recompila. Carga, activa el tick source, ejecuta el waiter y descarga. La descarga normalmente se completa sin error, pero `taskqueue_drain(taskqueue_thread, ...)` no espera realmente a la tarea que se está ejecutando en `sc->tq`. Si la tarea está en vuelo cuando el detach avanza, se produce uso después de liberación.

Corrección: usa el mismo puntero de taskqueue tanto para encolar como para drenar. Recompila y prueba.

### Un one-liner de DTrace

Un one-liner útil para cualquier driver que use taskqueue. Mide el tiempo entre la encola y el despacho de cada tarea en el sistema:

```text
# dtrace -n '
  fbt::taskqueue_enqueue:entry { self->t = timestamp; }
  fbt::taskqueue_run_locked:entry /self->t/ {
        @[execname] = quantize(timestamp - self->t);
        self->t = 0;
  }
'
```

La salida es una distribución de la latencia entre encola y despacho por proceso. Ejecútalo mientras tu driver produce tareas y pulsa Ctrl-C para ver el histograma cuantizado. Resultados típicos en una máquina con poca carga: decenas de microsegundos. Bajo carga: milisegundos. Si ves segundos, algo va mal.

Un segundo one-liner útil mide la duración del callback de la tarea:

```text
# dtrace -n '
  fbt::taskqueue_run_locked:entry { self->t = timestamp; }
  fbt::taskqueue_run_locked:return /self->t/ {
        @[execname] = quantize(timestamp - self->t);
        self->t = 0;
  }
'
```

Misma estructura, temporización diferente. Indica cuánto tarda cada invocación de `taskqueue_run_locked` (que es la duración del callback más una pequeña sobrecarga constante).

### Sysctls de diagnóstico a añadir

Contadores útiles para añadir a cualquier driver que use taskqueue, con un coste mínimo y un alto valor de diagnóstico.

```c
int enqueues;           /* Total enqueues attempted. */
int pending_drops;      /* Enqueues that coalesced. */
int callback_runs;      /* Total callback invocations. */
int largest_pending;    /* Peak pending count observed. */
```

Actualiza los contadores en la ruta de encola y en el callback:

```c
static void
myfirst_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        sc->callback_runs++;
        if (pending > sc->largest_pending)
                sc->largest_pending = pending;
        if (pending > 1)
                sc->pending_drops += pending - 1;
        /* ... */
}

/* Enqueue site: */
sc->enqueues++;
taskqueue_enqueue(sc->tq, &sc->task);
```

Expón cada uno como un sysctl de solo lectura. Bajo carga normal, `enqueues == callback_runs + pending_drops`. `largest_pending` indica el peor momento de coalescencia; si crece, el taskqueue está perdiendo el ritmo respecto al productor.

Estos contadores cuestan unas pocas sumas atómicas por encola. En cualquier carga de trabajo realista el coste es imperceptible. El valor de diagnóstico es considerable.

### La obligación de usar el kernel de depuración

Una advertencia que vale la pena repetir: ejecuta cada cambio del Capítulo 14 bajo un kernel con `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED`. La mayoría de los errores de taskqueue que son difíciles de encontrar en un kernel de producción se detectan de inmediato en un kernel de depuración. El coste de ejecutar un kernel de depuración es una pequeña penalización en el rendimiento y un build algo mayor; el coste de no hacerlo es una tarde de depuración cada vez que algo sale mal.

### Cerrando la Sección 7

Depurar taskqueues es una habilidad pequeña que se combina con las herramientas que ya tienes para depurar callouts, mutexes y cvs. `procstat -t` y `ps ax` muestran el thread. `sysctl` expone contadores de diagnóstico. `dtrace` mide la latencia entre encola y despacho, y la duración del callback. `WITNESS` detecta violaciones del orden de adquisición de locks en tiempo de ejecución. Los errores más comunes (la tarea nunca se ejecuta, orden de drenado incorrecto, disciplina de locks incorrecta en el callback, drenado olvidado) son todos detectables con una lista de comprobación y un kernel de depuración.

La Sección 8 consolida el trabajo del Capítulo 14 en la Etapa 4, el driver final. Ampliaremos `LOCKING.md`, subiremos la cadena de versión y auditaremos el driver con una pasada completa de pruebas de estrés.



## Sección 8: Refactorización y control de versiones de tu driver con taskqueue

La Etapa 4 es la etapa de consolidación. No añade nueva funcionalidad más allá de lo que estableció la Etapa 3; afina la organización del código, actualiza la documentación, sube la versión y ejecuta la pasada de regresión completa. Si las Etapas 1 a 3 son donde construiste el driver, la Etapa 4 es donde lo dejas listo para usar.

Esta sección recorre la consolidación. El código fuente del driver se unifica en un único archivo bien estructurado; `LOCKING.md` obtiene una sección de Tasks; la cadena de versión avanza a `0.8-taskqueues`; y la pasada de regresión final confirma que todos los comportamientos del Capítulo 12 y el Capítulo 13 siguen funcionando correctamente junto con las nuevas incorporaciones del Capítulo 14.

### Organización de archivos

El capítulo no divide el driver en múltiples archivos `.c`. `myfirst.c` permanece como la única unidad de traducción, con una responsabilidad añadida (las tareas) agrupada junto a los callouts correspondientes. Si el driver creciese mucho más, una división natural sería `myfirst_timers.c` para el código de callout y `myfirst_tasks.c` para el código de tareas, con declaraciones compartidas en `myfirst.h`. Para el tamaño actual, el archivo único es más fácil de leer.

Dentro de `myfirst.c`, la organización de la Etapa 4 es:

1. Includes y macros globales.
2. Estructura softc.
3. Estructura de manejador de archivo.
4. Declaración de cdevsw.
5. Funciones auxiliares de buffer.
6. Funciones auxiliares de espera en variable de condición.
7. Manejadores de sysctl, agrupados:
   - Sysctls de configuración (nivel de depuración, límite blando de bytes, apodo).
   - Sysctls de intervalo de temporizador (heartbeat, watchdog, tick source).
   - Sysctls de tareas (reset retrasado, lote del escritor masivo, inundación del escritor masivo).
   - Sysctls de estadísticas de solo lectura.
8. Callbacks de callout.
9. Callbacks de tarea.
10. Manejadores de cdev (open, close, read, write, poll, destructor de manejador).
11. Métodos de dispositivo (identify, probe, attach, detach).
12. Código de enlace del módulo (driver, `DRIVER_MODULE`, versión).

Un comentario de bloque al principio del archivo lista las secciones principales, para que un nuevo lector pueda saltar al área correcta sin usar grep. Dentro de cada sección, el orden es el establecido primero: heartbeat antes que watchdog, y watchdog antes que tick source, para los callouts que comparten ese orden en el esquema.

### La actualización de `LOCKING.md`

El `LOCKING.md` del Capítulo 13 tenía secciones para el mutex, las cvs, el sx y los callouts. El Capítulo 14 añade una sección de Tasks.

```markdown
## Tasks

The driver owns one private taskqueue (`sc->tq`) and three tasks:

- `selwake_task` (plain): calls `selwakeup(&sc->rsel)`. Enqueued from
  `myfirst_tick_source` when a byte is written. Drained at detach after
  callouts are drained and before `seldrain`.
- `bulk_writer_task` (plain): writes a configured number of bytes to the
  cbuf, signals `data_cv`, calls `selwakeup(&sc->rsel)`. Enqueued from
  sysctl handlers and from the tick_source callback when
  `bulk_writer_batch` is non-zero. Drained at detach after callouts.
- `reset_delayed_task` (timeout_task): performs a delayed reset of the
  cbuf, counters, and configuration. Enqueued by the
  `reset_delayed` sysctl. Drained at detach.

The taskqueue is created in `myfirst_attach` with `taskqueue_create`
and one worker thread started at `PWAIT` priority via
`taskqueue_start_threads`. It is freed in `myfirst_detach` via
`taskqueue_free` after every task has been drained.

All task callbacks run in thread context. Each callback acquires
`sc->mtx` explicitly if it needs state protected by the mutex; the
taskqueue framework does not acquire driver locks automatically.

All task callbacks call `selwakeup(9)` (when they call it at all) with
no driver lock held. The rule is the same as for the `myfirst_read` /
`myfirst_write` paths: drop the mutex before `selwakeup`.

## Detach Ordering

The detach sequence is:

1. Refuse detach if `sc->active_fhs > 0` (EBUSY).
2. Clear `sc->is_attached` under `sc->mtx`.
3. Broadcast `data_cv` and `room_cv`.
4. Release `sc->mtx`.
5. Drain `heartbeat_co`, `watchdog_co`, `tick_source_co`.
6. Drain `selwake_task`, `bulk_writer_task`, `reset_delayed_task`
   (the last via `taskqueue_drain_timeout`).
7. `seldrain(&sc->rsel)`, `seldrain(&sc->wsel)`.
8. `taskqueue_free(sc->tq)`.
9. Destroy cdev and cdev alias.
10. Free sysctl context.
11. Destroy cbuf, free counters.
12. Destroy `data_cv`, `room_cv`, `cfg_sx`, `mtx`.

Violating the order risks use-after-free in task callbacks, selinfo
accesses after drain, or taskqueue teardown while a task is still
running.
```

La actualización es explícita respecto al orden porque el orden es lo que con mayor facilidad puede salir mal. Un lector que herede tu driver y quiera añadir una nueva tarea encontrará la disciplina existente bien detallada.

### El cambio de versión

La cadena de versión en el código fuente pasa de `0.7-timers` a `0.8-taskqueues`:

```c
#define MYFIRST_VERSION "0.8-taskqueues"
```

Y la cadena probe del driver se actualiza:

```c
device_set_desc(dev, "My First FreeBSD Driver (Chapter 14 Stage 4)");
```

La versión es visible a través del sysctl `hw.myfirst.version`, que se estableció en el Capítulo 12.

### El pase de regresión final

La Etapa 4 debe superar todas las pruebas que superaron las Etapas 1 a 3, además de los conjuntos de pruebas de los Capítulos 12 y 13. Un orden de ejecución compacto:

1. **Construir sin errores** bajo el kernel de depuración (`make clean && make`).
2. **Cargar** con `kldload ./myfirst.ko`.
3. **Pruebas unitarias del Capítulo 11**: lectura, escritura, apertura, cierre y reset básicos.
4. **Pruebas de sincronización del Capítulo 12**: lecturas bloqueantes acotadas, escrituras bloqueantes acotadas, lecturas con timeout, configuración protegida con sx, broadcasts de cv en detach.
5. **Pruebas de timers del Capítulo 13**: el heartbeat dispara a la tasa configurada, el watchdog detecta un drenaje paralizado, la fuente de ticks inyecta bytes.
6. **Pruebas de tasks del Capítulo 14**:
   - `poll_waiter` ve datos cuando la fuente de ticks está activa.
   - El contador `selwake_pending_drops` crece bajo carga.
   - `bulk_writer_flood` activa la fusión en un único callback.
   - `reset_delayed` dispara tras el retardo configurado.
   - Rearmar `reset_delayed` reemplaza el plazo (un único disparo).
7. **Detach bajo carga**: fuente de ticks a 1 ms, `poll_waiter` en ejecución, `bulk_writer_flood` enviando inundaciones, y a continuación descarga inmediata. Debe ser limpio.
8. **Pase WITNESS**: todas las pruebas anteriores, sin advertencias de `WITNESS` en `dmesg`.
9. **Pase lockstat**: ejecutar el conjunto de pruebas bajo `lockstat -s 5` para medir la contención de locks. El mutex interno del taskqueue solo debería aparecer brevemente.

Todas las pruebas deben pasar. Si alguna falla, la causa casi con toda seguridad es una regresión introducida entre la Etapa 3 y la Etapa 4, no un problema preexistente; las Etapas 1 a 3 se validan cada una de forma independiente antes de que comience la Etapa 4.

### Mantener la documentación sincronizada

Hay tres lugares donde la documentación debe reflejar el Capítulo 14:

- El comentario al inicio del archivo en `myfirst.c`. Actualiza el bloque "locking strategy" para mencionar el taskqueue.
- `LOCKING.md`. Actualiza según la subsección anterior.
- Cualquier `README.md` por capítulo bajo `examples/part-03/ch14-taskqueues-and-deferred-work/`. Describe los entregables de cada etapa y cómo construirlos.

Actualizar la documentación puede parecer una carga extra. No lo es. El lector del próximo año (a menudo tu yo futuro) depende de la documentación para reconstruir el diseño. Escribirla ahora, mientras el diseño está fresco, es un orden de magnitud más barato que escribirla después.

### Una auditoría final

Antes de cerrar la Etapa 4, realiza una auditoría breve.

- [ ] ¿Todo drenaje de callout ocurre antes que todo drenaje de task en detach?
- [ ] ¿Todo drenaje de task ocurre antes que `seldrain`?
- [ ] ¿`taskqueue_free` ocurre después de que se hayan drenado todas las tasks?
- [ ] ¿El camino de fallo en attach llama a `taskqueue_free` sobre la cola si un paso de init posterior falla?
- [ ] ¿Cada lugar de enqueue apunta al puntero de taskqueue correcto (privado, no compartido)?
- [ ] ¿Cada lugar de drenaje coincide con su lugar de enqueue correspondiente (mismo taskqueue, misma task)?
- [ ] ¿Todo callback de task está libre de suposiciones del tipo "ejecuto exactamente una vez por cada enqueue"?
- [ ] ¿Todo callback de task está libre de suposiciones del tipo "mantengo un driver lock al entrar"?
- [ ] ¿`LOCKING.md` lista cada task y su callback, ciclo de vida y rutas de enqueue?
- [ ] ¿La cadena de versión refleja la nueva etapa?

Un driver que supera esta auditoría, más la auditoría del Capítulo 13 de la Sección 7 de ese capítulo, es un driver que puedes entregar a otro ingeniero con confianza.

### Cerrando la Sección 8

La Etapa 4 es la consolidación. El código del driver está organizado. `LOCKING.md` está actualizado. La cadena de versión refleja la nueva capacidad. El conjunto completo de regresiones pasa bajo un kernel de depuración. La lista de verificación de la auditoría está limpia.

El driver `myfirst` ha recorrido un largo camino. Comenzó el Capítulo 10 como un dispositivo de caracteres de apertura única que movía bytes a través de un buffer circular. El Capítulo 11 le dio acceso concurrente. El Capítulo 12 le dio bloqueo acotado, canales cv y configuración protegida con sx. El Capítulo 13 le dio callouts para trabajo periódico y de watchdog. El Capítulo 14 le da trabajo diferido, que es el puente entre los contextos de borde y el contexto de thread, y la pieza que faltaba para un driver que eventualmente se enfrentará a interrupciones de hardware reales.

El resto de este capítulo amplía la vista ligeramente. La sección de temas adicionales introduce `epoch(9)`, los grouptaskqueues y los taskqueues por CPU a nivel introductorio. Los laboratorios prácticos consolidan el material de las Secciones 3 a 8. Los ejercicios de desafío amplían los conocimientos del lector. Una referencia de resolución de problemas agrupa los problemas más comunes en un solo lugar. A continuación, el cierre y el puente hacia el Capítulo 15.



## Temas adicionales: `epoch(9)`, grouptaskqueues y taskqueues por CPU

El cuerpo principal del Capítulo 14 enseñó los patrones de `taskqueue(9)` que necesita un driver típico. Tres temas adyacentes merecen mención para los lectores que eventualmente escribirán o leerán drivers de red, o cuyos drivers crezcan hasta una escala en la que el simple taskqueue privado no sea suficiente. Cada tema se introduce al nivel de "saber cuándo recurrir a él". La mecánica completa pertenece a capítulos posteriores, especialmente aquellos que cubren los drivers de red en la Parte 6 (Capítulo 28).

### `epoch(9)` en una página

`epoch(9)` es un mecanismo de sincronización de lectura sin locks. Su propósito es permitir que muchos lectores recorran una estructura de datos compartida de forma concurrente, sin adquirir ningún lock exclusivo, garantizando al mismo tiempo que la estructura de datos no desaparecerá bajo sus pies.

La forma es esta. El código que lee datos compartidos entra en una "sección de epoch" con `epoch_enter(epoch)` y la abandona con `epoch_exit(epoch)`. Dentro de la sección, los lectores pueden desreferenciar punteros libremente. Los escritores que quieren modificar o liberar un objeto compartido no lo hacen directamente; en su lugar, o bien llaman a `epoch_wait(epoch)` para bloquearse hasta que todos los lectores actuales hayan abandonado la sección de epoch, o bien registran un callback mediante `epoch_call(epoch, cb, ctx)` que se ejecuta de forma asíncrona una vez que todos los lectores actuales han salido.

El beneficio es la escalabilidad. Los lectores no pagan ningún coste de operación atómica; simplemente registran estado local al thread al entrar y al salir. Los escritores pagan el coste de sincronización, pero las escrituras son poco frecuentes en comparación con las lecturas, por lo que el coste amortizado es bajo. Para estructuras de datos recorridas por muchos threads y modificadas solo ocasionalmente, `epoch(9)` supera con creces a los locks de lectura-escritura.

El coste es la disciplina. El código dentro de una sección de epoch no debe dormir, no debe adquirir locks dormibles, y no debe llamar a funciones que puedan hacer ninguna de las dos cosas. Los escritores que usan `epoch_wait` se bloquean hasta que todos los lectores actuales han salido, lo que significa que el escritor puede estar esperando a muchos lectores.

Los drivers de red usan `epoch(9)` de forma intensiva. El epoch `net_epoch_preempt` protege las lecturas del estado de red (listas de ifnet, entradas de enrutamiento, flags de interfaz). Un camino de entrada de paquetes entra en el epoch, recorre el estado y sale del epoch. Un escritor que quiere eliminar una interfaz aplaza la liberación mediante `NET_EPOCH_CALL`, y la liberación ocurre en un mecanismo similar a un taskqueue una vez que todos los lectores han terminado.

En cuanto a la conexión con el taskqueue: cuando una task se inicializa con `NET_TASK_INIT` en lugar de `TASK_INIT`, el taskqueue ejecuta el callback dentro del epoch `net_epoch_preempt`. El callback de la task puede por tanto recorrer el estado de red sin entrar en el epoch de forma explícita. De la implementación en `/usr/src/sys/kern/subr_taskqueue.c`:

```c
if (!in_net_epoch && TASK_IS_NET(task)) {
        in_net_epoch = true;
        NET_EPOCH_ENTER(et);
} else if (in_net_epoch && !TASK_IS_NET(task)) {
        NET_EPOCH_EXIT(et);
        in_net_epoch = false;
}
task->ta_func(task->ta_context, pending);
```

El despachador del taskqueue detecta el flag `TASK_NETWORK` y entra o sale del epoch alrededor del callback según sea necesario. Las tasks de red consecutivas comparten una única entrada de epoch, lo cual es una pequeña optimización que el framework realiza de forma gratuita.

Para `myfirst`, esto no es relevante. El driver no toca el estado de red. Pero si más adelante escribes un driver de red o lees código de drivers de red, `NET_TASK_INIT` y `TASK_IS_NET` son las macros que te indican que una task es consciente del epoch.

### Grouptaskqueues en una página

Un grouptaskqueue es una generalización escalable de un taskqueue. La idea básica: en lugar de una única cola con un único grupo (o pequeño grupo) de workers, distribuir las tasks entre múltiples colas por CPU, cada una atendida por su propio thread worker. Un "grouptask" es una task que se vincula a una de esas colas.

El archivo de cabecera es `/usr/src/sys/sys/gtaskqueue.h`:

```c
#define GROUPTASK_INIT(gtask, priority, func, context)   \
    GTASK_INIT(&(gtask)->gt_task, 0, priority, func, context)

#define GROUPTASK_ENQUEUE(gtask)                         \
    grouptaskqueue_enqueue((gtask)->gt_taskqueue, &(gtask)->gt_task)

void    taskqgroup_attach(struct taskqgroup *qgroup,
            struct grouptask *grptask, void *uniq, device_t dev,
            struct resource *irq, const char *name);
int     taskqgroup_attach_cpu(struct taskqgroup *qgroup,
            struct grouptask *grptask, void *uniq, int cpu, device_t dev,
            struct resource *irq, const char *name);
void    taskqgroup_detach(struct taskqgroup *qgroup, struct grouptask *gtask);
```

Un driver que usa grouptasks hace lo siguiente en attach:

1. Inicializar cada grouptask con `GROUPTASK_INIT`.
2. Asociar cada grouptask a un `taskqgroup` con `taskqgroup_attach` o `taskqgroup_attach_cpu`. La asociación asigna el grouptask a una cola y un thread worker específicos por CPU.
3. En el momento del evento, encolar con `GROUPTASK_ENQUEUE`.
4. En detach, `taskqgroup_detach` desasocia el grouptask.

¿Por qué usar grouptasks en lugar de tasks simples? Hay dos razones.

Primero, **escalabilidad con el número de CPUs**. Un taskqueue con un solo thread es un cuello de botella cuando muchos productores en diferentes CPUs encolan de forma concurrente. El mutex interno del taskqueue se vuelve contendido. Un grouptaskqueue con colas por CPU permite que cada CPU encole en su propia cola sin contención entre CPUs.

Segundo, **localidad de caché**. Cuando una interrupción se dispara en la CPU N y encola un grouptask vinculado a la CPU N, la task se ejecuta en la misma CPU que vio la interrupción. Los datos de la task ya están en las cachés de esa CPU. Para los drivers de red de alta velocidad, esto supone una ganancia de rendimiento sustancial.

El coste es la complejidad. Los grouptaskqueues requieren más configuración inicial, más desmontaje y más reflexión sobre a qué cola pertenece cada task. Para la mayoría de los drivers, este coste no está justificado. Para un driver Ethernet de alta gama que procesa millones de paquetes por segundo, el coste se amortiza solo.

`myfirst` no usa grouptasks. No se beneficiaría de ello. Los mencionamos para que cuando leas un driver como `/usr/src/sys/dev/wg/if_wg.c` o `/usr/src/sys/net/iflib.c`, las macros te resulten familiares.

### Taskqueues por CPU en una página

Un taskqueue por CPU es la versión simple de la idea del grouptaskqueue: un taskqueue por CPU, cada uno con su propio thread worker. El driver crea N taskqueues (uno por CPU), vincula cada uno a una CPU específica con `taskqueue_start_threads_cpuset`, y despacha las tasks a la cola apropiada según la regla de localidad que el driver desee.

La primitiva clave es `taskqueue_start_threads_cpuset`:

```c
int taskqueue_start_threads_cpuset(struct taskqueue **tqp, int count,
    int pri, cpuset_t *mask, const char *name, ...);
```

Es como `taskqueue_start_threads` pero con un `cpuset_t` que describe en qué CPUs pueden ejecutarse los threads. Para una vinculación a una sola CPU, la máscara tiene exactamente un bit activo. Para una flexibilidad multi-CPU, la máscara tiene múltiples bits.

Un driver que usa taskqueues por CPU normalmente mantiene un array de punteros a taskqueue indexado por CPU:

```c
struct my_softc {
        struct taskqueue *per_cpu_tq[MAXCPU];
        ...
};

for (int i = 0; i < mp_ncpus; i++) {
        CPU_SETOF(i, &mask);
        sc->per_cpu_tq[i] = taskqueue_create("per_cpu", M_WAITOK,
            taskqueue_thread_enqueue, &sc->per_cpu_tq[i]);
        taskqueue_start_threads_cpuset(&sc->per_cpu_tq[i], 1, PWAIT,
            &mask, "%s cpu%d", device_get_nameunit(sc->dev), i);
}
```

Y en el momento del enqueue, seleccionar la cola correspondiente a la CPU actual:

```c
int cpu = curcpu;
taskqueue_enqueue(sc->per_cpu_tq[cpu], &task);
```

Los beneficios son los mismos que los de los grouptasks, sin el framework de grouptask: el trabajo permanece en la CPU donde fue producido, se elimina la contención local de CPU y las cachés se mantienen calientes. El coste es que el driver gestiona su propia estructura de datos por CPU.

Para `myfirst`, esto sería un exceso. Para un driver cuya tasa de eventos supera las decenas de miles de eventos por segundo, los taskqueues por CPU merecen consideración. Los grouptaskqueues son más generales y suelen preferirse cuando la escalabilidad importa; los taskqueues por CPU son la alternativa más ligera.

### Cuándo usar cuál

Un árbol de decisión breve.

- **Tasa baja, contexto de thread, la cola compartida es suficiente**: usa `taskqueue_thread`. La opción más sencilla.
- **Tasa baja, contexto de thread, el aislamiento importa**: taskqueue privado con `taskqueue_create` y `taskqueue_start_threads`. Lo que usa `myfirst`.
- **Tasa alta, la contención es el cuello de botella**: taskqueues por CPU o grouptaskqueues. Empieza con los de por CPU; recurre a los grouptasks si necesitas las funcionalidades de escalabilidad adicionales.
- **Datos en el camino de red**: `NET_TASK_INIT` y grouptaskqueues, siguiendo los patrones de los drivers de red.
- **Contexto de interrupción de filtro, hay que encolar sin dormir**: `taskqueue_create_fast` o `taskqueue_fast`, ya que las interrupciones de filtro no pueden usar un sleep mutex.

La mayoría de los drivers que escribas o leas encajarán en una de las dos primeras líneas. El resto son casos especializados que sus capítulos correspondientes irán explicando.

### Cerrando los temas adicionales

`epoch(9)`, los grouptaskqueues y los taskqueues por CPU son la historia de escalabilidad de los taskqueues. Comparten el mismo modelo mental que la API básica: encolar desde un productor, despachar en un worker, respetar la disciplina de locks y drenar al desmontar. Las diferencias están en cuántas colas hay y cómo se despachan las tareas entre ellas. Para la mayoría de los drivers la API básica es suficiente; estas variantes avanzadas existen para cuando no lo es.

El capítulo pasa ahora a los laboratorios prácticos.



## Laboratorios prácticos

Los laboratorios consolidan el material del capítulo en cuatro ejercicios prácticos. Cada laboratorio utiliza el driver que has ido desarrollando a lo largo de las Etapas 1 a 4, junto con algunos pequeños helpers de espacio de usuario incluidos en `examples/part-03/ch14-taskqueues-and-deferred-work/labs/`.

Dedica una sesión a cada laboratorio. Si el tiempo es limitado, los Laboratorios 1 y 2 son los más importantes; los Laboratorios 3 y 4 merecen la pena, pero requieren más trabajo.

### Laboratorio 1: Observar un thread worker de taskqueue

**Objetivo.** Confirmar que el taskqueue privado de tu driver tiene un thread worker, que el thread duerme cuando no hay trabajo y que se despierta y ejecuta el callback cuando se encola trabajo.

**Configuración.** Carga el driver de la Etapa 2 (o el de la Etapa 4; cualquiera de los dos sirve). Asegúrate de que ningún otro proceso que use taskqueues esté inundando el sistema; cuanto más tranquilo esté el sistema, más fácil será la observación.

**Pasos.**

1. Ejecuta `procstat -t | grep myfirst`. Anota el PID y el TID mostrados. El thread debería estar en estado `sleep`.
2. Ejecuta `sysctl dev.myfirst.0.heartbeat_interval_ms=1000`. Espera unos segundos.
3. Ejecuta `procstat -t | grep myfirst` de nuevo. El thread puede aparecer brevemente en estado `run` durante un disparo del heartbeat; la mayor parte del tiempo seguirá en `sleep` porque el heartbeat no encola ninguna tarea. Confirma que esto es lo que ves. Ten en cuenta que el heartbeat se ejecuta en el thread del callout, no en el thread del taskqueue del driver.
4. Ejecuta `sysctl dev.myfirst.0.tick_source_interval_ms=100`. Espera unos segundos.
5. Ejecuta `procstat -t | grep myfirst` de nuevo. El thread debería oscilar ahora entre `sleep` y `run` porque el tick source está encolando una tarea diez veces por segundo.
6. Detén el tick source con `sysctl dev.myfirst.0.tick_source_interval_ms=0`. Confirma que el thread regresa a `sleep` permanente.
7. Detén el heartbeat. Descarga el driver. Confirma que el thread desaparece de `procstat -t`.

**Resultado esperado.** Has observado directamente el ciclo de vida del thread: se crea en el attach, duerme cuando está inactivo, se ejecuta cuando es despachado y se destruye en el detach. La observación vale más que las dos páginas de explicación que la precedieron.

### Laboratorio 2: Medir la fusión bajo carga

**Objetivo.** Generar una carga de trabajo que estrese el taskqueue lo suficiente como para activar la fusión (coalescing), y medir la tasa de fusión mediante el sysctl `selwake_pending_drops`.

**Configuración.** Carga el driver de la Etapa 4. Compila `poll_waiter` como se indica en la Sección 3.

**Pasos.**

1. Inicia `poll_waiter` en un terminal: `./poll_waiter > /dev/null`. Redirigir a `/dev/null` evita que el terminal se convierta en un cuello de botella.
2. En un segundo terminal, configura el tick source a una velocidad alta: `sysctl dev.myfirst.0.tick_source_interval_ms=1`.
3. Espera diez segundos.
4. Lee `sysctl dev.myfirst.0.stats.selwake_pending_drops`. Anota el valor.
5. Espera diez segundos más y vuelve a leer. Calcula la tasa por segundo.
6. Aumenta la tasa del tick source para ver si la fusión se incrementa: el intervalo mínimo del tick source es 1 ms, pero puedes combinarlo con el sysctl bulk_writer_flood para generar una carga más en ráfagas:
   ```text
   # for i in $(seq 1 100); do sysctl dev.myfirst.0.bulk_writer_flood=1000; done
   ```
7. Lee `selwake_pending_drops` tras la inundación.

**Resultado esperado.** El número crece con el tiempo, más bajo carga en ráfagas y menos bajo carga constante. Si el número se mantiene en cero incluso bajo carga agresiva, el thread del taskqueue es suficientemente rápido para mantenerse al día; esto es un buen estado, no un error.

**Variación.** Ejecuta la misma carga con un kernel de depuración (`WITNESS` activado) y observa si `dmesg` muestra avisos de `WITNESS`. No debería mostrar ninguno.

### Laboratorio 3: Verificar el orden del detach

**Objetivo.** Confirmar que la ruta de detach drena correctamente las tareas antes de liberar el estado que dichas tareas utilizan. Introduce deliberadamente el error de la Sección 7 (drenar las tareas después de `seldrain`) y observa la condición de carrera.

**Configuración.** Parte de la Etapa 4. Haz una copia de trabajo de `myfirst.c`.

**Pasos.**

1. En tu copia de trabajo, reordena los drains en `myfirst_detach` de modo que `seldrain` aparezca antes que `taskqueue_drain`:
   ```c
   /* BROKEN ORDER: */
   MYFIRST_CO_DRAIN(&sc->heartbeat_co);
   MYFIRST_CO_DRAIN(&sc->watchdog_co);
   MYFIRST_CO_DRAIN(&sc->tick_source_co);
   seldrain(&sc->rsel);
   seldrain(&sc->wsel);
   taskqueue_drain(sc->tq, &sc->selwake_task);
   /* ... rest ... */
   ```
   Esto es intencionadamente incorrecto.
2. Vuelve a compilar con el orden incorrecto.
3. Carga el driver. Activa el tick source a 1 ms. Ejecuta `poll_waiter`.
4. Tras unos segundos de flujo de datos, descarga el driver: `kldunload myfirst`.
5. La mayoría de las veces la descarga tiene éxito. Ocasionalmente, sobre todo bajo carga, el kernel entra en pánico. La pila del pánico suele incluir `selwakeup` llamado desde `myfirst_selwake_task`, después de que `seldrain` haya finalizado.
6. Restaura el orden correcto. Vuelve a compilar. Aplica la misma carga y repite la descarga muchas veces.
7. Confirma que el orden correcto nunca provoca pánico.

**Resultado esperado.** Has experimentado la condición de carrera directamente. La lección es que "funciona la mayoría de las veces" no equivale a "funciona". El orden correcto es una invariante que debes preservar incluso cuando el orden incorrecto parece funcionar en pruebas superficiales.

**Nota.** En un kernel de producción el pánico puede no producirse; la corrupción de memoria puede permanecer oculta hasta que algo más falle. Realiza siempre este tipo de experimentos en un kernel de depuración con `INVARIANTS` y `WITNESS` activados.

### Laboratorio 4: Fusión frente a agrupación adaptativa

**Objetivo.** Construir una pequeña modificación que use el argumento `pending` para guiar la agrupación adaptativa, y comparar su comportamiento con el bulk_writer_task de lote fijo de la Etapa 3.

**Configuración.** Parte de la Etapa 4.

**Pasos.**

1. Añade una nueva tarea al driver: `adaptive_writer_task`. Su callback escribe `pending` bytes (con un máximo de 64) en el buffer. Usa el patrón de la Sección 5.
2. Añade un sysctl que encole `adaptive_writer_task` bajo demanda:
   ```c
   static int
   myfirst_sysctl_adaptive_enqueue(SYSCTL_HANDLER_ARGS)
   {
           struct myfirst_softc *sc = arg1;
           int n = 0, i, error;

           error = sysctl_handle_int(oidp, &n, 0, req);
           if (error || req->newptr == NULL)
                   return (error);
           for (i = 0; i < n; i++)
                   taskqueue_enqueue(sc->tq, &sc->adaptive_writer_task);
           return (0);
   }
   ```
3. Inicializa la tarea en el attach y drénala en el detach.
4. Vuelve a compilar y carga el módulo.
5. Encola 1000 entradas mediante el sysctl: `sysctl dev.myfirst.0.adaptive_enqueue=1000`.
6. Lee `sysctl dev.myfirst.0.stats.bytes_written`. Observa cuántos bytes se han escrito.
7. Compara con el `bulk_writer_flood` de la Etapa 3 con `bulk_writer_batch=1`. El lote fijo escribiría 1 byte (fusionado en un solo disparo). El lote adaptativo escribe lo que sea que `pending` fuera, hasta 64.

**Resultado esperado.** La tarea adaptativa escribe más bytes bajo carga en ráfagas porque utiliza la información de fusión que el kernel ya ha calculado. Para cargas de trabajo en las que el trabajo por evento debe escalar con el número de eventos, este patrón es preferible a un tamaño de lote fijo.

**Variación.** Añade un contador que registre el mayor valor de `pending` visto hasta el momento. Exponlo como un sysctl. Bajo carga, verás que el pico de pending crece a medida que aumenta la carga.



## Ejercicios de desafío

Los desafíos son extensiones opcionales. Llevan los patrones establecidos en este capítulo a un territorio que el texto principal no ha cubierto. Tómatelos con calma; su propósito es consolidar la comprensión, no introducir material nuevo.

### Desafío 1: Tarea por file handle

Modifica el driver para que cada file handle abierto tenga su propia tarea. La función de la tarea, cuando se encola, es emitir una línea de log que identifique el handle. Escribe un sysctl que encole la tarea de cada handle simultáneamente.

Pistas:
- `struct myfirst_fh`, asignada en `myfirst_open`, es el lugar natural para la tarea por handle.
- Inicializa la tarea en `myfirst_open` después de `malloc`.
- Drena la tarea en `myfirst_fh_dtor` antes de `free`.
- Encolar "la tarea de cada handle" requiere una lista de handles abiertos. `devfs_set_cdevpriv` no mantiene dicha lista; tendrás que construirla en el softc, protegida por el mutex.

Resultado esperado: una demostración de propiedad de tareas a una granularidad más fina que la del driver. El desafío pone a prueba tu comprensión del orden de vida útil.

### Desafío 2: Pipeline de tareas de dos niveles

Añade un pipeline de dos tareas. La Tarea A recibe datos del handler de `write(2)`, los transforma (por simplicidad, convierte cada byte a mayúsculas) y encola la Tarea B. La Tarea B escribe los datos transformados en un buffer secundario y notifica a los waiters.

Pistas:
- El trabajo de transformación ocurre en el callback de la tarea, en contexto de thread. El handler de `write(2)` no debe bloquearse esperando la transformación.
- Necesitarás una pequeña cola de transformaciones pendientes, protegida por el mutex.
- La Tarea A extrae de la cola, transforma y encola la Tarea B con el estado por elemento. Como alternativa, la Tarea B se ejecuta una vez por cada encolado de A y procesa lo que haya en la cola.

Resultado esperado: un modelo mental de cómo los taskqueues pueden formar pipelines, con cada etapa ejecutándose en su propia invocación. Así es como los drivers complejos dividen el trabajo.

### Desafío 3: Ordenación de tareas por prioridad

Añade dos tareas con prioridades diferentes al driver. La `urgent_task` tiene prioridad 10 e imprime "URGENT". La `normal_task` tiene prioridad 0 e imprime "normal". Escribe un handler de sysctl que encole ambas tareas: primero la normal y luego la urgente.

Resultado esperado: la salida de `dmesg` muestra `URGENT` antes que `normal`, lo que confirma que la prioridad tiene precedencia sobre el orden de encolado dentro de la cola.

### Desafío 4: Reconfiguración con bloqueo

Implementa una ruta de reconfiguración que use `taskqueue_block` y `taskqueue_quiesce`. La ruta debe:

1. Bloquear el taskqueue.
2. Hacer quiesce (esperar a que las tareas en ejecución terminen).
3. Realizar la reconfiguración (por ejemplo, redimensionar el buffer circular).
4. Desbloquear.

Verifica con `dtrace` que ninguna tarea se ejecuta durante la ventana de reconfiguración.

Resultado esperado: experiencia con `taskqueue_block` y `taskqueue_quiesce`, y comprensión de cuándo son apropiados esos primitivos.

### Desafío 5: Taskqueue multihilo

Modifica la Etapa 4 para usar un taskqueue privado multihilo (por ejemplo, cuatro worker threads en lugar de uno). Ejecuta el test de fusión del Laboratorio 2. Observa qué cambia.

Resultado esperado: bajo carga, la tasa de fusión disminuye porque varios worker threads drenan la cola más rápido. Bajo carga muy ligera, nada cambia visiblemente. El desafío muestra cómo la configuración del taskqueue implica compromisos bajo diferentes cargas de trabajo.

### Desafío 6: Implementar un watchdog con una tarea de timeout

Reimplementa el watchdog del Capítulo 13 usando un `timeout_task` en lugar de un callout simple. Cada disparo del watchdog se vuelve a encolar con el intervalo configurado. Una operación de "kick" (otro sysctl, quizás `watchdog_kick`) cancela y vuelve a encolar la tarea de timeout para reiniciar el temporizador.

Resultado esperado: comprensión de cómo el primitivo `timeout_task` puede reemplazar a un callout para trabajo periódico, y cuándo es preferible cada uno. (Respuesta: `timeout_task` cuando el trabajo necesita contexto de thread; callout en caso contrario.)

### Desafío 7: Cargar un driver real y leer su código

Elige uno de los drivers listados en la Sección 6 (`/usr/src/sys/dev/ale/if_ale.c`, `/usr/src/sys/dev/age/if_age.c`, `/usr/src/sys/dev/bge/if_bge.c` o `/usr/src/sys/dev/iwm/if_iwm.c`). Lee detenidamente su uso del taskqueue. Identifica:

- Qué tareas posee el driver.
- Dónde se inicializa cada tarea.
- Dónde se encola cada tarea.
- Dónde se drena cada tarea.
- Si el driver usa `taskqueue_create` o `taskqueue_create_fast`.
- Qué prioridad de thread usa el driver.

Escribe un breve resumen (de aproximadamente una página) sobre cómo el driver usa la API de taskqueue. Guárdalo como referencia.

Resultado esperado: leer un driver real convierte el reconocimiento de patrones de algo abstracto en algo concreto. Después de hacerlo una vez con un driver, leer el siguiente es mucho más rápido.



## Referencia de resolución de problemas

Una lista plana de síntomas y remedios, para el momento en que aflore un error y necesites la respuesta rápidamente. Combina esta referencia con las listas de errores comunes dentro de cada sección; juntas cubren la mayoría de los problemas reales.

### La tarea nunca se ejecuta

- **¿Has llamado a `taskqueue_start_threads` después de `taskqueue_create`?** Sin threads, la cola acepta tareas encoladas pero nunca las despacha.
- **¿Es el puntero al taskqueue `NULL` en el momento de encolar?** Comprueba la ruta del attach; `taskqueue_create` puede haber fallado silenciosamente si no verificaste su valor de retorno.
- **¿Es false el valor de `is_attached` del driver cuando se dispara el encolado?** Algunas rutas de código (como los callout callbacks del capítulo 13) salen antes si `is_attached` es false; si la salida ocurre antes de encolar, la tarea no se ejecuta.
- **¿Está el taskqueue bloqueado mediante `taskqueue_block`?** En ese caso, acepta tareas encoladas pero no las despacha. Desbloquéalo.

### La tarea se ejecuta dos veces cuando esperabas una

- **¿La tarea se está reencolando a sí misma?** Un callback de tarea que llama a `taskqueue_enqueue` sobre sí mismo entrará en un bucle indefinido a menos que el callback salga antes bajo alguna condición.
- **¿Hay otro camino de código que también está encolando?** Comprueba todos los puntos de llamada de `taskqueue_enqueue` para la tarea. Dos fuentes encolando producirán la doble ejecución esperada con cierta sincronización.

### La tarea se ejecuta una vez cuando esperabas dos o más

- **¿Se han fusionado tus entregas?** La regla de encolado-ya-pendiente fusiona las entregas redundantes. Si necesitas semántica de exactamente-uno-por-evento, usa tareas separadas o una cola por evento.
- **¿El argumento `pending` se reporta como mayor que uno?** Si es así, el framework ha realizado la coalescencia.

### Kernel panic en el callback de la tarea

- **¿Está el callback accediendo a estado liberado?** La causa más común de un panic en un callback de tarea es un use-after-free. Comprueba el orden de detach: todos los productores de entregas deben drenarse antes que la tarea; el estado al que la tarea accede no debe liberarse hasta que la tarea esté drenada.
- **¿Está el callback manteniendo un lock que no debería?** `WITNESS` detecta la mayoría de estos casos. Ejecuta bajo un kernel de depuración y lee `dmesg`.
- **¿Está el callback llamando a `selwakeup` con un mutex del driver tomado?** No lo hagas. `selwakeup` adquiere su propio lock y no debe llamarse con locks del driver no relacionados tomados.

### El detach se queda colgado

- **¿Está `taskqueue_drain` esperando a una tarea que no puede completarse?** Comprueba con `procstat -kk` el estado del worker del taskqueue. Si está en una espera que depende de algo que el camino de detach mantiene, el diseño tiene un ciclo.
- **¿Está `taskqueue_free` esperando tareas que siguen siendo encoladas?** Comprueba `is_attached`: si los callouts siguen ejecutándose y encolando, el drenaje no terminará. Asegúrate de drenar los callouts primero.

### `kldunload` devuelve EBUSY inmediatamente

- **¿Hay algún descriptor de archivo aún abierto?** El camino de detach en `myfirst_detach` rechaza con `EBUSY` si `active_fhs > 0`. Cierra los descriptores abiertos e inténtalo de nuevo.

### El contador de coalescencia se queda a cero

- **¿Es la carga de trabajo demasiado ligera?** La coalescencia ocurre solo cuando el productor supera en velocidad al consumidor. En una máquina con poca carga esto raramente ocurre.
- **¿Es correcta tu medición?** La coalescencia se cuenta en el callback, no en el camino de encolado. Comprueba la lógica de tu contador.
- **¿Es el taskqueue multihilo?** Más threads significan un consumo más rápido y menos coalescencia.

### El thread del taskqueue privado no aparece en `procstat -t`

- **¿Devolvió cero `taskqueue_start_threads`?** Si devolvió un error, los threads no se crearon. Comprueba el valor de retorno.
- **¿Está el driver realmente cargado?** `kldstat` lo confirma.
- **¿Es el nombre del thread diferente al que esperabas?** La cadena de formato pasada a `taskqueue_start_threads` controla el nombre; asegúrate de que estás haciendo grep del nombre correcto.

### Deadlock entre la tarea y el mutex del driver

- **¿Adquiere el callback de la tarea un lock que otro thread mantiene mientras espera la tarea?** Esa es la forma clásica del deadlock. Resuélvelo moviendo el encolado de la tarea fuera de la sección con el lock tomado, o reestructurando la espera para que no bloquee sobre la tarea.

### `taskqueue_enqueue` falla con `EEXIST`

- **Pasaste `TASKQUEUE_FAIL_IF_PENDING` y la tarea ya estaba pendiente.** El fallo es intencional; comprueba si ese flag es lo que necesitas.

### `taskqueue_enqueue_timeout` no parece dispararse

- **¿Está el taskqueue bloqueado?** Los taskqueues bloqueados tampoco despachan tareas de timeout.
- **¿Es razonable el número de ticks?** Un número de ticks de cero se dispara inmediatamente, pero una conversión de milisegundos a ticks no entera puede producir retrasos inesperadamente largos. Usa `(ms * hz + 999) / 1000` para el techo.
- **¿Ha sido cancelada la tarea de timeout por un `taskqueue_cancel_timeout`?** Si es así, reencólala.

### Rearmar un `timeout_task` no reemplaza el plazo

- **Cada `taskqueue_enqueue_timeout` reemplaza el plazo pendiente.** Si tu driver lo llama varias veces y solo parece surtir efecto la primera, puede que tengas un problema de ordenación: ¿estás seguro de que las llamadas posteriores se están realizando?

### WITNESS se queja del orden de locks que involucra `tq_mutex`

- **El mutex interno del taskqueue está entrando en el orden de locks de tu driver.** Generalmente porque un callback de tarea adquiere un lock del driver, y algún otro camino de código adquiere ese lock del driver primero y luego encola.
- **La solución habitual es encolar antes de adquirir el lock del driver, o reestructurar el código de modo que los dos locks nunca se mantengan en el mismo thread en el orden incorrecto.**

### `procstat -kk` muestra el thread del taskqueue durmiendo sobre un lock

- **Un callback de tarea está bloqueado sobre un lock que permite dormir.** Identifica el lock a partir del canal de espera. Comprueba si el poseedor de ese lock también está esperando algo; si es así, tienes una cadena de dependencias.

### Los callbacks de tareas son lentos

- **Perfila con `dtrace`.** El one-liner de la Sección 7 mide la duración del callback.
- **¿Está el callback manteniendo un lock durante operaciones largas?** Mueve la operación larga fuera del lock.
- **¿Está el callback realizando IO síncrona?** Eso pertenece a los manejadores de `read(2)` / `write(2)` / `ioctl(2)`, no en callbacks de taskqueue, a menos que la IO sea genuinamente el propósito de la tarea.

### Deadlock del taskqueue durante el boot

- **¿Estás encolando tareas desde un `SI_SUB` que se ejecuta antes que `SI_SUB_TASKQ`?** Los taskqueues predefinidos se inicializan en `SI_SUB_TASKQ`. Los manejadores de `SI_SUB` anteriores no pueden encolar en ellos.



## En resumen

El capítulo 14 enseñó una sola primitiva en profundidad. La primitiva es `taskqueue(9)`. Su propósito es trasladar trabajo fuera de contextos que no pueden realizarlo y llevarlo a un contexto que sí puede. Su API es pequeña: inicializar una tarea, encolarla, drenarla y liberar la cola cuando hayas terminado. El modelo mental es igualmente sencillo: los contextos de borde detectan, las tareas en contexto de thread actúan.

El driver `myfirst` absorbió el nuevo mecanismo con naturalidad porque cada capítulo anterior había preparado el andamiaje. El capítulo 11 le dio concurrencia. El capítulo 12 le dio canales cv y configuración sx. El capítulo 13 le dio callouts y la disciplina de drenar-al-hacer-detach. El capítulo 14 añadió tareas como una quinta primitiva con la misma forma: inicializar en attach, drenar en detach, respetar las reglas de locking establecidas y componer con lo que ya existía. El driver está ahora en la versión `0.8-taskqueues`, con tres tareas y un taskqueue privado, y se desmonta limpiamente bajo carga.

Bajo esos cambios concretos, el capítulo planteó varios puntos más amplios. Un breve repaso de cada uno.

**El trabajo diferido es el puente entre los contextos de borde y los contextos de thread.** Los callouts, los filtros de interrupción y las secciones epoch enfrentan la misma restricción: no pueden realizar trabajo que requiera dormir o adquirir locks que permitan dormir. Un taskqueue resuelve el problema de manera uniforme: acepta una pequeña entrega del contexto de borde y ejecuta el trabajo real en un thread.

**El framework taskqueue gestiona la logística para que tú no tengas que hacerlo.** Asignación, locking interno de la cola, despacho, coalescencia, cancelación, drenaje: todo eso lo gestiona el framework. Tu driver proporciona un callback y un punto de encolado. El resto es una breve configuración en attach y un breve desmontaje en detach.

**La coalescencia es una característica, no un defecto.** Una tarea fusiona las entregas redundantes en un único disparo cuyo argumento `pending` lleva el recuento. Esto permite que ráfagas de eventos se reduzcan a una única invocación del callback, que es casi siempre lo que quieres para el rendimiento. Un diseño que necesite invocación por evento necesita una tarea por evento o una cola por evento, no una sola tarea encolada muchas veces.

**El orden de detach es la mayor disciplina nueva que el capítulo añadió.** Callouts primero, tareas después, selinfo a continuación, `taskqueue_free` al final. Violar el orden es una condición de carrera que puede no aparecer en pruebas tranquilas y que sí aparecerá bajo carga. El documento `LOCKING.md` es donde escribes ese orden; seguirlo es como evitas la condición de carrera.

**Los drivers reales utilizan el mismo puñado de patrones.** Registro diferido desde el contexto de borde; separación de interrupción entre filtro y tarea; `copyin`/`copyout` asíncronos; reintento con backoff; desmontaje diferido; rotación programada; bloqueo durante la reconfiguración. Todos ellos son variaciones del esquema borde-detecta, tarea-actúa. Leer `/usr/src/sys/dev/` es la manera más rápida de absorber los patrones en profundidad.

**Los taskqueues escalan a escenarios más exigentes.** `epoch(9)`, los grouptaskqueues y los taskqueues por CPU manejan los casos de escalabilidad que un taskqueue privado simple no puede. Comparten el modelo mental de la API básica; las diferencias están en el número de colas, la estrategia de despacho y el andamiaje alrededor de los workers. Para la mayoría de los drivers la API básica es suficiente; para los casos de alto rendimiento, las variantes avanzadas están disponibles cuando las necesites.

### Una reflexión antes del capítulo 15

Empezaste el capítulo 14 con un driver capaz de actuar por sí solo a lo largo del tiempo (callouts), pero cuyas acciones estaban limitadas a lo que los callouts permiten. Lo abandonas con un driver que puede actuar a lo largo del tiempo y también actuar a través de trabajo delegado a un thread worker. Esas dos capacidades combinadas cubren casi todo tipo de acción diferida que un driver puede necesitar. La tercera pieza es la sincronización que coordina a los productores y consumidores de manera segura, y esa pieza es la que desarrolla el capítulo 15.

El modelo mental es acumulativo. El capítulo 12 introdujo cvs, mutexes y sx. El capítulo 13 introdujo callouts y el contexto de no-dormir. El capítulo 14 introdujo tareas y el contexto de diferir-al-thread. El capítulo 15 introducirá las primitivas de coordinación avanzadas (semáforos, `cv_timedwait_sig`, handshakes entre componentes) que componen las piezas anteriores en patrones más ricos. Cada capítulo añade una pequeña primitiva y la disciplina que la acompaña.

La forma acumulativa del driver es visible en `LOCKING.md`. Un driver del capítulo 10 no tenía `LOCKING.md`. Un driver del capítulo 11 tenía un único párrafo. Un driver del capítulo 14 tiene un documento de varias páginas con secciones para el mutex, los cvs, el sx, los callouts y las tareas, más una sección de orden de detach que nombra cada paso de drenaje en el orden correcto. Ese documento es un artefacto que llevas contigo en cada capítulo futuro. Cuando el capítulo 15 añada semáforos, `LOCKING.md` crecerá con una sección de Semáforos. Cuando la Parte 4 añada interrupciones, crecerá con una sección de Interrupciones. La vida del driver es su `LOCKING.md`.

### Una segunda reflexión: la disciplina

Un hábito que el capítulo quiere que interiorices por encima de todos los demás: cada nueva primitiva en tu driver se merece una entrada en `LOCKING.md`, un par destructor en attach/detach, y un lugar en el orden de detach documentado. Saltarse cualquiera de ellos crea un bug que espera ocurrir. La disciplina se paga a sí misma la primera vez que entregas el driver a otra persona.

Lo inverso también es cierto: cada vez que leas el driver de otra persona, mira primero su `LOCKING.md`. Si falta, lee las funciones attach y detach para reconstruir el orden a partir del código. Si ves una primitiva en attach sin el drenaje correspondiente en detach, eso es un bug. Si ves un drenaje sin un predecesor claro, probablemente sea un error de orden. La disciplina es la misma para escribir que para leer.

### Una nota breve sobre la simplicidad

Los taskqueues parecen simples. Lo son. La API es pequeña, los patrones son regulares y los mismos esquemas se transfieren de un driver a otro. La simplicidad es deliberada; es lo que hace que la API sea utilizable en la práctica. Esa misma simplicidad también hace que las reglas no sean negociables: una regla que se salta produce una condición de carrera difícil de depurar. Sigue la disciplina y los taskqueues seguirán siendo simples para ti. Improvisa, y no lo serán.

### Qué hacer si estás atascado

Si algo en tu driver no se comporta como se espera, consulta la referencia de resolución de problemas de la Sección 7 en orden. Comprueba el primer elemento que coincida con tu síntoma. Si ningún elemento coincide, vuelve a leer la Sección 4 (configuración y limpieza) y audita tu orden de detach comparándolo con `LOCKING.md`. Si el orden es correcto, usa `dtrace` para trazar el camino de encolado y comprueba si los eventos esperados están ocurriendo.

Si el driver entra en pánico, usa `gdb` sobre el volcado de memoria. Un `bt` muestra la pila. Una pila que incluye tu callback de taskqueue es un buen punto de partida; compárala con los patrones de la Sección 7.

Si nada de lo anterior funciona, vuelve a leer el driver real que elegiste para el Ejercicio de Desafío 7 de la sección anterior. A veces el patrón que resulta confuso en tu propio código se lee con naturalidad en un driver escrito por otra persona. Los patrones son universales; el driver que estás leyendo no tiene nada de especial.

## Puente al capítulo 15

El capítulo 15 se titula *More Synchronization: Conditions, Semaphores, and Coordination*. Su alcance abarca los primitivos de coordinación avanzados que componen los mutexes, las cvs, los sx locks, los callouts y los tasks que ya conoces en patrones más sofisticados.

El capítulo 14 preparó el terreno de cuatro maneras concretas.

En primer lugar, **ahora tienes un par productor/consumidor funcional en el driver**. El callout `tick_source` (y los demás puntos de encolado del capítulo 14) es un productor; el thread del taskqueue es un consumidor. Los canales cv del capítulo 12 forman otro par productor/consumidor: `write(2)` produce, `read(2)` consume. El capítulo 15 generaliza el patrón y añade primitivos (semáforos de conteo, esperas cv interrumpibles) que gestionan versiones más elaboradas de esa misma forma.

En segundo lugar, **conoces la disciplina del drain en detach**. Cada primitivo que has añadido hasta ahora tiene su correspondiente drain. El capítulo 15 introduce los semáforos, que tienen su propio patrón de "drain" (liberar a todos los que esperan y luego destruir), y la disciplina se transfiere de forma directa.

En tercer lugar, **sabes cómo pensar en los límites de contexto**. El contexto del callout, el del task, el de la syscall: cada uno tiene sus propias reglas, y el diseño de tu driver las respeta. El capítulo 15 añade las esperas interrumpibles por señal, que incorporan el contexto de interacción del usuario a la mezcla. El hábito de "¿en qué contexto estoy y qué puedo hacer aquí?" se transfiere sin problemas.

En cuarto lugar, **tu `LOCKING.md` sigue el ritmo de "una sección por primitivo, más una sección de ordenación al final"**. El capítulo 15 añadirá una sección de semáforos y posiblemente una de coordinación. La estructura ya está establecida; solo cambia el contenido.

Los temas concretos que cubrirá el capítulo 15:

- Semáforos de conteo y semáforos binarios mediante la API `sema(9)`.
- `cv_timedwait_sig` y bloqueo interrumpible por señal.
- Patrones de lectores-escritores mediante `sx(9)` en su forma más general.
- Protocolos de sincronización entre componentes que coordinan timers, tasks y threads de usuario.
- Indicadores de estado y barreras de memoria para la coordinación sin lock (a nivel introductorio).
- Entornos de prueba para concurrencia: scripts de estrés, inyección de fallos y reproducción de condiciones de carrera.

No necesitas adelantarte. El capítulo 14 es preparación suficiente. Trae tu driver `myfirst` en la Etapa 4 del capítulo 14, tu `LOCKING.md`, tu kernel con `WITNESS` activado y tu kit de pruebas. El capítulo 15 empieza donde terminó el 14.

Una pequeña reflexión final. El driver con el que empezaste la Parte 3 entendía una syscall a la vez. El driver que tienes ahora dispone de tres callouts, tres tasks, dos cvs, dos locks y un detach completo con orden definido. Gestiona lectores y escritores concurrentes, eventos temporizados, trabajo diferido entre límites de contexto, ráfagas de eventos coalescentes y un cierre limpio bajo carga. Tiene una historia de locking documentada y una suite de regresión validada. Empieza a parecerse a un driver real de FreeBSD.

El capítulo 15 cierra la Parte 3 añadiendo los primitivos de coordinación que permiten que las piezas se combinen en patrones más ricos. Luego comienza la Parte 4: integración con hardware y plataforma. Interrupciones reales. Registros mapeados en memoria reales. Hardware real que puede fallar, comportarse de forma inesperada o negarse a cooperar. La disciplina que has construido a lo largo de la Parte 3 es lo que te llevará hasta el final.

Tómate un momento antes de continuar. El salto del capítulo 13 al 14 fue cualitativo: el driver adquirió la capacidad de diferir trabajo a un contexto de thread, y los patrones que aprendiste en el camino (orden de detach, coalescencia, el modelo mental de borde/thread) son patrones que reutilizarás en todos los capítulos que siguen. A partir de aquí, el capítulo 15 consolida la historia de sincronización, y la Parte 4 comienza la historia del hardware. El trabajo que has hecho no se pierde; se acumula.

### Una última reflexión sobre la forma del kernel

Un último pensamiento antes del capítulo 15. Hasta ahora has conocido cinco de los primitivos de sincronización y diferimiento del kernel: el mutex, la condition variable, el sx lock, el callout y el taskqueue. Cada uno existe porque los primitivos anteriores, más simples, no podían abordar el mismo problema. Un mutex no puede expresar "espera una condición"; para eso están las cvs. Un sleep mutex no puede mantenerse a través de un sleep; para eso están los sx locks. Un callout no puede ejecutar trabajo que necesite dormir; para eso sirven los taskqueues.

El patrón es reconocible en todo el kernel. Cada primitivo de sincronización existe por una carencia específica de los anteriores. Cuando lees código del kernel, a menudo puedes adivinar por qué se eligió un primitivo concreto preguntando qué no pueden hacer sus vecinos. Un descriptor de archivo no puede sufrir un use-after-free porque el primitivo de conteo de referencias lo impide. Un paquete de red no puede liberarse mientras un lector recorre la lista porque el primitivo epoch lo impide. Un task no puede ejecutarse después del detach porque el primitivo drain lo impide.

El kernel es un catálogo de estos primitivos, cada uno deliberado, cada uno una respuesta a una clase concreta de problema. Tu driver acumula su propio catálogo a medida que crece. El capítulo 14 añadió un elemento más a la lista. El capítulo 15 añade varios. La Parte 4 comienza el catálogo orientado al hardware. A partir de aquí los primitivos se multiplican, pero la forma de la disciplina no cambia. Define el problema, elige el primitivo, inicializa y destruye limpiamente, documenta el orden, verifica bajo carga.

Ese es el oficio. El resto del libro te guía a través de él.


## Referencia: auditoría previa a producción de taskqueues

Una breve auditoría que conviene realizar antes de pasar un driver que usa taskqueues de desarrollo a producción. Cada elemento es una pregunta; cada una debería poder responderse con confianza.

### Inventario de tasks

- [ ] ¿He listado en `LOCKING.md` todos los tasks que posee el driver?
- [ ] Por cada task, ¿he nombrado su función de callback?
- [ ] Por cada task, ¿he documentado su ciclo de vida (init en attach, drain en detach)?
- [ ] Por cada task, ¿he documentado su disparador (qué provoca que se encole)?
- [ ] Por cada task, ¿he documentado si se vuelve a encolar a sí mismo o si se ejecuta una vez por cada disparador externo?
- [ ] Por cada timeout task, ¿he nombrado el intervalo para el que está programado y el camino de cancelación?

### Inventario de taskqueues

- [ ] ¿Es el taskqueue una cola privada o una predefinida? ¿Está justificada la elección?
- [ ] Si es privada, ¿llama attach a `taskqueue_create` (o `taskqueue_create_fast`) antes de cualquier código que pueda encolar?
- [ ] Si es privada, ¿llama attach a `taskqueue_start_threads` antes de cualquier código que espere que se ejecute un callback?
- [ ] ¿Es adecuado el número de threads de trabajo para la carga de trabajo?
- [ ] ¿Es adecuada la prioridad de los threads de trabajo para la carga de trabajo?
- [ ] ¿Es suficientemente informativo el nombre del thread de trabajo como para que la salida de `procstat -t` sea útil?

### Inicialización

- [ ] ¿Se realiza cada `TASK_INIT` después de poner a cero el softc y antes de que el task pueda encolarse?
- [ ] ¿Hace referencia cada `TIMEOUT_TASK_INIT` al taskqueue correcto y a un callback válido?
- [ ] ¿Gestiona attach el fallo de `taskqueue_create` deshaciendo las inicializaciones anteriores?
- [ ] ¿Gestiona attach el fallo de `taskqueue_start_threads` liberando el taskqueue?

### Puntos de encolado

- [ ] ¿Apunta cada punto de encolado al puntero de taskqueue correcto?
- [ ] ¿Confirma cada encolado desde un contexto de borde (callout, filtro de interrupción) que el taskqueue existe antes de encolar?
- [ ] ¿Es segura la llamada de encolado en el contexto desde el que se realiza (por ejemplo, no dentro de un spin mutex si el taskqueue es `taskqueue_create`)?
- [ ] ¿Es intencional el comportamiento de coalescencia en cada punto de encolado?

### Higiene del callback

- [ ] ¿Tiene cada callback la firma correcta `(void *context, int pending)`?
- [ ] ¿Adquiere cada callback los locks del driver de forma explícita donde sea necesario?
- [ ] ¿Libera cada callback los locks del driver antes de llamar a `selwakeup`, `log` u otras funciones que adquieren locks no relacionados?
- [ ] ¿Evita cada callback las asignaciones con `M_NOWAIT` cuando `M_WAITOK` es seguro?
- [ ] ¿Está acotado el tiempo total de trabajo del callback?

### Cancelación

- [ ] ¿Se realiza cada llamada a `taskqueue_cancel` / `taskqueue_cancel_timeout` bajo el mutex correcto si importa la condición de carrera en la cancelación?
- [ ] ¿Se gestionan los casos en que cancel devuelve `EBUSY` (normalmente mediante un drain posterior)?

### Detach

- [ ] ¿Borra detach `is_attached` antes de hacer drain a los callouts?
- [ ] ¿Hace detach drain a todos los callouts antes de hacer drain a cualquier task?
- [ ] ¿Hace detach drain a todos los tasks antes de llamar a `seldrain`?
- [ ] ¿Llama detach a `seldrain` antes de `taskqueue_free`?
- [ ] ¿Llama detach a `taskqueue_free` antes de destruir el mutex?
- [ ] ¿Asigna detach `sc->tq = NULL` después de liberarlo?

### Documentación

- [ ] ¿Está documentado cada task en `LOCKING.md`?
- [ ] ¿Están documentadas las reglas de disciplina (seguridad del encolado, lock del callback, orden del drain)?
- [ ] ¿Se menciona el subsistema taskqueue en el README?
- [ ] ¿Hay sysctls expuestos que permitan a los usuarios observar el comportamiento?

### Pruebas

- [ ] ¿He ejecutado la suite de regresión con `WITNESS` activado?
- [ ] ¿He probado el detach con todos los tasks en vuelo?
- [ ] ¿He ejecutado una prueba de estrés de larga duración con tasas de encolado elevadas?
- [ ] ¿He usado `dtrace` para verificar que la latencia entre el encolado y el despacho está dentro de las expectativas?
- [ ] ¿He usado `procstat -kk` bajo carga para confirmar que el thread del taskqueue no está bloqueado?

Un driver que supera esta auditoría es un driver en el que puedes confiar bajo carga.



## Referencia: estandarización de tasks en un driver

En un driver con varios tasks, la coherencia importa más que el ingenio. Una disciplina breve.

### Una sola convención de nomenclatura

Elige una convención y síguela. La convención del capítulo:

- La estructura del task se llama `<purpose>_task` (p. ej., `selwake_task`, `bulk_writer_task`).
- La estructura del timeout task se llama `<purpose>_delayed_task` (p. ej., `reset_delayed_task`).
- El callback se llama `myfirst_<purpose>_task` (p. ej., `myfirst_selwake_task`, `myfirst_bulk_writer_task`).
- El sysctl que encola el task (si existe alguno) se llama `<purpose>_enqueue` o `<purpose>_flood` en las variantes masivas.
- El sysctl que configura el task (si existe alguno) se llama `<purpose>_<parameter>` (p. ej., `bulk_writer_batch`).

Un nuevo colaborador puede añadir un task siguiendo la convención sin tener que pensar en los nombres. Por el contrario, una revisión de código detecta de inmediato cualquier desviación.

### Un solo patrón de init/drain

Cada task usa la misma inicialización y drain:

```c
/* In attach, after taskqueue_start_threads: */
TASK_INIT(&sc-><purpose>_task, 0, myfirst_<purpose>_task, sc);

/* In detach, after callout drains, before seldrain: */
taskqueue_drain(sc->tq, &sc-><purpose>_task);
```

Para los timeout tasks:

```c
/* In attach: */
TIMEOUT_TASK_INIT(sc->tq, &sc-><purpose>_delayed_task, 0,
    myfirst_<purpose>_delayed_task, sc);

/* In detach: */
taskqueue_drain_timeout(sc->tq, &sc-><purpose>_delayed_task);
```

Los puntos de llamada son breves y uniformes. Un revisor puede buscar el patrón y señalar las desviaciones al instante.

### Un solo patrón de callback

Cada callback de task sigue la misma estructura:

```c
static void
myfirst_<purpose>_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        /* Optional: record coalescing for diagnostics. */
        if (pending > 1) {
                MYFIRST_LOCK(sc);
                sc-><purpose>_drops += pending - 1;
                MYFIRST_UNLOCK(sc);
        }

        /* ... do the work, acquiring locks as needed ... */
}
```

El registro de coalescencia opcional hace visible el comportamiento de coalescencia a través de sysctls. Omítelo si el task raramente experimenta coalescencia o si el contador no resultaría útil.

### Un solo patrón de documentación

Cada task está documentado en `LOCKING.md` con los mismos campos:

- Nombre del task y tipo (plain o timeout_task).
- Función de callback.
- Qué rutas de código lo encolan.
- Qué rutas de código lo cancelan (si las hay).
- Dónde se drena en el detach.
- Qué lock, si hay alguno, adquiere el callback.
- Por qué se difiere este task (es decir, por qué no puede ejecutarse en el contexto de encolado).

La documentación de un nuevo task es mecánica. Una revisión de código puede verificar la documentación frente al código.

### Por qué estandarizar

La estandarización tiene costes: un nuevo colaborador debe aprender las convenciones; las desviaciones requieren una razón especial. Los beneficios son mayores:

- Menor carga cognitiva. Un lector que conoce el patrón entiende al instante cada task.
- Menos errores. El patrón estándar gestiona correctamente los casos habituales (init en attach, drain en detach, soltar el lock antes de `selwakeup`); una desviación tiene más probabilidades de ser incorrecta.
- Revisión más fácil. Los revisores pueden analizar la forma en lugar de leer cada línea.
- Traspaso más sencillo. Un colaborador que no ha visto el driver puede añadir un nuevo task siguiendo la plantilla existente.

El coste de la estandarización se paga una vez, en el momento del diseño. Los beneficios se acumulan para siempre. Siempre vale la pena.



## Referencia: lecturas adicionales sobre taskqueues

Para los lectores que quieran profundizar.

### Páginas de manual

- `taskqueue(9)`: la referencia canónica de la API.
- `epoch(9)`: el framework de sincronización por épocas, relevante para tareas de red.
- `callout(9)`: la primitiva complementaria; timeout_task se construye sobre ella.
- `swi_add(9)`: el registro de interrupciones software usado por `taskqueue_swi` y similares.
- `kproc(9)`, `kthread(9)`: creación directa de threads del kernel, para cuando un taskqueue no es suficiente.

### Archivos fuente

- `/usr/src/sys/kern/subr_taskqueue.c`: la implementación del taskqueue. Lee `taskqueue_run_locked` con detenimiento; es el corazón del subsistema.
- `/usr/src/sys/sys/taskqueue.h`, `/usr/src/sys/sys/_task.h`: la API pública y las estructuras.
- `/usr/src/sys/kern/subr_gtaskqueue.c`, `/usr/src/sys/sys/gtaskqueue.h`: la capa grouptaskqueue.
- `/usr/src/sys/sys/epoch.h`, `/usr/src/sys/kern/subr_epoch.c`: el framework epoch.
- `/usr/src/sys/dev/ale/if_ale.c`: un driver de Ethernet limpio que usa taskqueues.
- `/usr/src/sys/dev/bge/if_bge.c`: un driver de Ethernet más grande con múltiples tareas.
- `/usr/src/sys/dev/wg/if_wg.c`: el uso de grouptaskqueue en WireGuard.
- `/usr/src/sys/dev/iwm/if_iwm.c`: un driver inalámbrico con tareas de timeout.
- `/usr/src/sys/dev/usb/usb_process.c`: la cola de proceso dedicada por dispositivo de USB (`usb_proc_*`).

### Páginas de manual que leer en orden

Para un lector nuevo en el subsistema de trabajo diferido de FreeBSD:

1. `taskqueue(9)`: la API canónica.
2. `epoch(9)`: el framework de sincronización de lectura sin locks.
3. `callout(9)`: la primitiva hermana de ejecución temporizada.
4. `swi_add(9)`: la capa de interrupción por software que subyace a algunos taskqueues.
5. `kthread(9)`: la alternativa de creación directa de threads.

Cada una construye sobre la anterior; leerlas en orden lleva un par de horas y te proporciona un modelo mental sólido de la infraestructura de trabajo diferido del kernel.

### Material externo

El capítulo sobre sincronización en *The Design and Implementation of the FreeBSD Operating System* (McKusick et al.) cubre la evolución histórica de los subsistemas de trabajo diferido. Útil como contexto; no es imprescindible.

La lista de correo de desarrolladores de FreeBSD (`freebsd-hackers@`) discute ocasionalmente mejoras y casos límite de taskqueue. Buscar en el archivo con el término "taskqueue" devuelve contexto histórico relevante.

Para comprender en mayor profundidad el uso de `epoch(9)` y los grouptaskqueues en la pila de red, merece la pena leer la documentación del framework `iflib(9)` y el código fuente en `/usr/src/sys/net/iflib.c`. Están más allá del nivel de este capítulo, pero explican por qué los drivers de red modernos están estructurados como lo están.

Por último, el código fuente real de los drivers. Escoge cualquier driver de `/usr/src/sys/dev/` que use taskqueues (la mayoría lo hacen), lee su código relacionado con taskqueues y compáralo con los patrones de este capítulo. La traducción es directa; reconocerás las formas de inmediato. Ese tipo de lectura convierte las abstracciones del capítulo en conocimiento funcional.



## Referencia: análisis de coste de los taskqueues

Una breve discusión sobre lo que los taskqueues cuestan realmente, útil a la hora de decidir si diferir trabajo o si crear una cola privada.

### Coste en reposo

Un `struct task` que no ha sido encolado no cuesta nada más allá del `sizeof` de la estructura (32 bytes en amd64). El kernel no sabe que existe. Reside en tu softc sin hacer nada.

Un `struct taskqueue` asignado e inactivo cuesta:
- La propia estructura del taskqueue (unos pocos cientos de bytes).
- Uno o más threads de trabajo (pila de 16 KB cada uno en amd64, más el estado del planificador).
- Ningún coste por encolado en reposo.

### Coste por encolado

Cuando llamas a `taskqueue_enqueue(tq, &task)`, el kernel realiza lo siguiente:

1. Adquiere el mutex interno del taskqueue. Microsegundos.
2. Comprueba si la tarea ya está pendiente. Tiempo constante.
3. Si no está pendiente, la enlaza en la lista y despierta al worker (mediante `wakeup` sobre la cola). Tiempo constante más un evento del planificador.
4. Si ya está pendiente, incrementa `ta_pending`. Una sola operación aritmética.
5. Libera el mutex.

El coste total es de microsegundos en una cola sin contención. Bajo contención, la adquisición del mutex puede tardar más, pero el framework usa un mutex con alineación de relleno (padalign) para minimizar el false sharing, y el mutex rara vez se mantiene durante más de unas pocas instrucciones.

### Coste por despacho

Cuando el thread de trabajo se despierta y ejecuta `taskqueue_run_locked`, el coste por tarea es:

1. Avanzar hasta la cabeza de la cola. Tiempo constante.
2. Extraer la tarea. Tiempo constante.
3. Registrar el contador de pendientes y reiniciarlo. Tiempo constante.
4. Liberar el mutex.
5. Entrar en el epoch correspondiente (para tareas de red).
6. Llamar al callback. El coste depende del callback.
7. Salir del epoch si se entró.
8. Readquirir el mutex para la siguiente iteración.

Para un callback típico corto (microsegundos de trabajo), el overhead por despacho está dominado por el propio callback más un ciclo de mutex y un ciclo de wakeup.

### Coste al cancelar o drenar

`taskqueue_cancel` es rápido: adquisición del mutex, eliminación de la lista si está pendiente, liberación del mutex. Microsegundos.

`taskqueue_drain` es rápido si la tarea está inactiva. Si la tarea está pendiente, el drenaje espera a que se ejecute y complete; la duración depende de la profundidad de la cola y de la duración del callback. Si la tarea está en ejecución, el drenaje espera a que la invocación en curso retorne.

`taskqueue_drain_all` es más costoso: debe esperar a que finalicen todas las tareas de la cola. La duración es proporcional al trabajo total pendiente.

`taskqueue_free` drena la cola, termina los threads y libera el estado. La terminación de los threads implica señalizar a cada uno para que salga y esperar a que termine su tarea en curso. Microsegundos a milisegundos en función de la profundidad de la cola.

### Implicaciones prácticas

Algunas notas prácticas.

**Los taskqueues de un único thread son baratos.** El coste por instancia es unos pocos cientos de bytes más una pila de thread de 16 KB. En cualquier sistema real, esto es despreciable.

**Los taskqueues compartidos son más baratos por driver, pero están sujetos a contención.** `taskqueue_thread` lo usan todos los drivers que no crean el suyo propio. Bajo carga elevada se convierte en un cuello de botella serial. Para drivers con un tráfico de tareas significativo, una cola privada evita la contención.

**Los taskqueues multihilo intercambian memoria por paralelismo.** Cuatro threads equivalen a cuatro pilas de 16 KB más cuatro entradas del planificador. Vale la pena cuando la carga de trabajo es naturalmente paralela; es un desperdicio cuando la carga de trabajo se serializa en el mutex de un único driver.

**El coalescing es rendimiento gratuito.** Cuando los encolados llegan más rápido de lo que el taskqueue puede despachar, el coalescing agrupa las ráfagas en una única activación. El driver paga una invocación de callback por todo el trabajo que implica el contador `pending`.

### Comparación con otros enfoques

Un thread del kernel creado con `kproc_create` y gestionado por el driver cuesta:
- Pila de 16 KB más entrada del planificador (igual que un worker de taskqueue).
- Ningún framework integrado de encolado y despacho: el driver implementa su propia cola y su propio mecanismo de despertar.
- Ningún coalescing ni cancelación integrados.

Para trabajo que encaja en el modelo de tarea (encolar, despachar, drenar), un taskqueue es siempre la elección correcta. Para trabajo que no encaja (un bucle de larga duración con su propio ritmo), un thread creado con `kproc_create` puede ser más adecuado.

Un callout que encola una tarea combina los costes de ambas primitivas. Vale la pena cuando el trabajo necesita tanto una fecha límite específica como contexto de thread.

### Cuándo preocuparse por el coste

La mayoría de los drivers no necesitan hacerlo. Los taskqueues son baratos; el kernel está bien ajustado. Preocúpate por el coste solo cuando:

- La perfilación (profiling) muestre que las operaciones de taskqueue dominan el uso de CPU. (Usa `dtrace` para confirmarlo.)
- Estés escribiendo un driver de alta frecuencia (miles de eventos por segundo o más) y el taskqueue sea el punto de serialización.
- El sistema tenga muchos drivers compitiendo por `taskqueue_thread` y la contención sea medible.

En todos los demás casos, usa el taskqueue con naturalidad y confía en que el kernel gestionará la carga.



## Referencia: la semántica de coalescing de tareas, con precisión

El coalescing es la característica que más sorprende a los recién llegados. Una descripción precisa de la semántica, con ejemplos detallados, merece su propia subsección de referencia.

### La regla

Cuando se llama a `taskqueue_enqueue(tq, &task)` sobre una tarea que ya está pendiente (`task->ta_pending > 0`), el kernel incrementa `task->ta_pending` y devuelve éxito. La tarea no se enlaza en la cola una segunda vez. Cuando el callback finalmente se ejecuta, lo hace exactamente una vez, recibiendo el valor acumulado de `ta_pending` como segundo argumento (y el campo se reinicia a cero antes de que se invoque el callback).

La regla tiene casos límite que conviene nombrar.

**El límite.** `ta_pending` es un `uint16_t`. Se satura en `USHRT_MAX` (65535). Los encolados más allá de ese punto siguen devolviendo éxito, pero el contador no crece más. En la práctica, alcanzar 65535 encolados coalesced es un problema de diseño, no de rendimiento.

**El flag `TASKQUEUE_FAIL_IF_PENDING`.** Si pasas este flag a `taskqueue_enqueue_flags`, la función devuelve `EEXIST` en lugar de hacer coalescing. Útil cuando quieres saber si el encolado produjo un nuevo estado pendiente.

**El momento.** El coalescing ocurre en el momento del encolado. Si el encolado A y el encolado B se producen ambos mientras la tarea está pendiente, ambos se coalesce. Si el encolado A hace que la tarea comience a ejecutarse y el encolado B se produce mientras el callback está en ejecución, el encolado B vuelve a poner la tarea en estado pendiente (pending=1) y el callback se invocará de nuevo tras retornar la invocación en curso. La segunda invocación ve `pending=1` porque solo B ha acumulado. Ambas invocaciones se producen; ningún encolado se pierde.

**La prioridad.** Si dos tareas distintas están pendientes en la misma cola y una tiene mayor prioridad, la de mayor prioridad se ejecuta primero independientemente del orden de encolado. Dentro de una única tarea, la prioridad no influye; todas las invocaciones de una tarea dada se ejecutan en secuencia.

### Ejemplos detallados

**Ejemplo 1: un único encolado simple.**

```c
taskqueue_enqueue(tq, &task);
/* Worker fires the callback. */
/* Callback sees pending == 1. */
```

**Ejemplo 2: encolado coalesced antes del despacho.**

```c
taskqueue_enqueue(tq, &task);
taskqueue_enqueue(tq, &task);
taskqueue_enqueue(tq, &task);
/* (Worker has not yet woken up.) */
/* Worker fires the callback. */
/* Callback sees pending == 3. */
```

**Ejemplo 3: encolado durante la ejecución del callback.**

```c
taskqueue_enqueue(tq, &task);
/* Callback starts; pending is reset to 0. */
/* While callback is running: */
taskqueue_enqueue(tq, &task);
/* Callback finishes its first invocation. */
/* Worker notices pending == 1; fires callback again. */
/* Second callback invocation sees pending == 1. */
```

**Ejemplo 4: cancelación antes del despacho.**

```c
taskqueue_enqueue(tq, &task);
taskqueue_enqueue(tq, &task);
/* Cancel: */
taskqueue_cancel(tq, &task, &pendp);
/* pendp == 2; callback does not run. */
```

**Ejemplo 5: cancelación durante la ejecución.**

```c
taskqueue_enqueue(tq, &task);
/* Callback starts. */
/* During callback: */
taskqueue_cancel(tq, &task, &pendp);
/* Returns EBUSY; pending (if any future enqueues came in) may or may not be zeroed. */
/* The currently executing invocation completes; the cancellation affects only future runs. */
```

### Implicaciones de diseño

De la regla se derivan algunas implicaciones de diseño.

**Tu callback debe ser idempotente respecto a `pending`.** Escribir un callback que asume `pending==1` falla bajo carga. Usa siempre `pending` de forma deliberada: bien iterando `pending` veces, bien haciendo una única pasada que gestione todo el estado acumulado.

**No uses el número de invocaciones del callback como contador de eventos.** Usa `pending` de cada invocación, sumado. O, mejor, usa una estructura de estado por evento (una cola dentro del softc) que el callback drene.

**El coalescing transforma el trabajo por evento en trabajo por ráfaga.** Un callback que realiza trabajo O(1) por invocación, descartando `pending`, gestiona la misma cantidad de trabajo independientemente de la tasa de encolado. Eso suele estar bien para trabajos del tipo "notificar a los que esperan"; es incorrecto para trabajos del tipo "procesar cada evento".

**El coalescing permite encolar libremente desde contextos de borde.** Un callout que se activa cada milisegundo puede encolar una tarea cada milisegundo; si el callback tarda 10 ms en ejecutarse, nueve encolados se coalesce en una única invocación por callback. El sistema converge de forma natural en el throughput que el callback puede sostener.



## Referencia: el diagrama de estados del taskqueue

Un breve diagrama de estados para una única tarea, como ayuda para razonar sobre su ciclo de vida.

```text
        +-----------+
        |   IDLE    |
        | pending=0 |
        +-----+-----+
              |
              | taskqueue_enqueue
              v
        +-----------+           +--------+
        |  PENDING  | <--- enq--|  any   |
        | pending>=1|          +--------+
        +-----+-----+
              |
              | worker picks up
              v
        +-----------+
        |  RUNNING  |
        | (callback |
        | executing)|
        +-----+-----+
              |
              | callback returns
              v
        +-----------+
        |   IDLE    |
        | pending=0 |
        +-----------+
```

Una tarea se encuentra siempre en exactamente uno de tres estados: IDLE, PENDING o RUNNING.

**IDLE.** No está en ninguna cola. `ta_pending == 0`. El encolado la mueve a PENDING.

**PENDING.** En la lista de pendientes del taskqueue. `ta_pending >= 1`. El coalescing incrementa `ta_pending` sin salir de PENDING. La cancelación la devuelve a IDLE.

**RUNNING.** En `tq_active`, con el callback en ejecución. `ta_pending` ha sido reiniciado a cero y el callback ha recibido el valor anterior. Los nuevos encolados hacen la transición de vuelta a PENDING (por lo que, tras retornar el callback, el worker la activa de nuevo). La cancelación devuelve `EBUSY` en este estado.

Las transiciones de estado están todas serializadas por `tq_mutex`. En cualquier instante, el kernel puede decirte en qué estado se encuentra una tarea, y las transiciones son atómicas.

`taskqueue_drain(tq, &task)` espera hasta que la tarea esté en IDLE y no lleguen nuevos encolados antes de retornar. Esa es la garantía precisa que ofrece el drenaje.



## Referencia: hoja de referencia rápida de observabilidad

Para consulta rápida durante la depuración.

### Listar todos los threads de taskqueue

```text
# procstat -t | grep taskq
```

### Listar la tasa de envío de cada tarea con DTrace

```text
# dtrace -n 'fbt::taskqueue_enqueue:entry { @[(caddr_t)arg1] = count(); }' -c 'sleep 10'
```

El script cuenta los enqueues por puntero de tarea durante diez segundos. Los punteros de tarea se mapean a los drivers correspondientes mediante `addr2line` o `kgdb`.

### Medir la latencia de despacho

```text
# dtrace -n '
  fbt::taskqueue_enqueue:entry { self->t = timestamp; }
  fbt::taskqueue_run_locked:entry /self->t/ {
        @[execname] = quantize(timestamp - self->t);
        self->t = 0;
  }
' -c 'sleep 10'
```

### Medir la duración del callback

```text
# dtrace -n '
  fbt::taskqueue_run_locked:entry { self->t = timestamp; }
  fbt::taskqueue_run_locked:return /self->t/ {
        @[execname] = quantize(timestamp - self->t);
        self->t = 0;
  }
' -c 'sleep 10'
```

### Stack trace del thread de taskqueue cuando está bloqueado

```text
# procstat -kk <pid>
```

### Tareas activas dentro de ddb

Desde el prompt de `ddb`:

```text
db> show taskqueues
```

Lista todos los taskqueues, su tarea activa (si la hay) y la cola pendiente.

### Parámetros sysctl que tu driver debería exponer

Para cada tarea que el driver gestiona, considera exponer:

- `<purpose>_enqueues`: total de intentos de encolado.
- `<purpose>_coalesced`: número de veces que se activó la coalescencia.
- `<purpose>_runs`: total de invocaciones del callback.
- `<purpose>_largest_pending`: valor máximo del contador pendiente.

En condiciones normales: `enqueues == runs + coalesced`. Con coalescencia: `runs < enqueues`. Sin carga: `largest_pending == 1`. Con carga elevada: `largest_pending` crece.

Estos contadores transforman el comportamiento opaco del driver en una visualización sysctl legible. El coste son unos pocos incrementos atómicos; el valor es alto.



## Referencia: plantilla mínima de tarea funcional

Para facilitar la copia y adaptación. Cada elemento ha sido presentado en el capítulo; la plantilla los ensambla en un esqueleto listo para usar.

```c
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/taskqueue.h>
#include <sys/mutex.h>
#include <sys/lock.h>

struct example_softc {
        device_t          dev;
        struct mtx        mtx;
        struct taskqueue *tq;
        struct task       work_task;
        int               is_attached;
};

static void
example_work_task(void *arg, int pending)
{
        struct example_softc *sc = arg;

        mtx_lock(&sc->mtx);
        /* ... do work under the mutex if state protection is needed ... */
        mtx_unlock(&sc->mtx);

        /* ... do lock-free work or calls like selwakeup here ... */
}

static int
example_attach(device_t dev)
{
        struct example_softc *sc = device_get_softc(dev);
        int error;

        sc->dev = dev;
        mtx_init(&sc->mtx, device_get_nameunit(dev), "example", MTX_DEF);

        sc->tq = taskqueue_create("example taskq", M_WAITOK,
            taskqueue_thread_enqueue, &sc->tq);
        if (sc->tq == NULL) {
                error = ENOMEM;
                goto fail_mtx;
        }
        error = taskqueue_start_threads(&sc->tq, 1, PWAIT,
            "%s taskq", device_get_nameunit(dev));
        if (error != 0)
                goto fail_tq;

        TASK_INIT(&sc->work_task, 0, example_work_task, sc);
        sc->is_attached = 1;
        return (0);

fail_tq:
        taskqueue_free(sc->tq);
fail_mtx:
        mtx_destroy(&sc->mtx);
        return (error);
}

static int
example_detach(device_t dev)
{
        struct example_softc *sc = device_get_softc(dev);

        mtx_lock(&sc->mtx);
        sc->is_attached = 0;
        mtx_unlock(&sc->mtx);

        taskqueue_drain(sc->tq, &sc->work_task);
        taskqueue_free(sc->tq);
        mtx_destroy(&sc->mtx);
        return (0);
}

/* Elsewhere, a code path that wants to defer work: */
static void
example_trigger_work(struct example_softc *sc)
{
        if (sc->is_attached)
                taskqueue_enqueue(sc->tq, &sc->work_task);
}
```

Cada elemento es esencial. Eliminar cualquiera de ellos vuelve a introducir un error contra el que este capítulo advirtió.



## Referencia: comparación con las workqueues de Linux

Una breve comparación para lectores que vienen del desarrollo del kernel de Linux. Ambos sistemas resuelven el mismo problema; las diferencias están en los nombres, la granularidad y los valores por defecto.

### Nomenclatura

| Concepto | FreeBSD | Linux |
|---|---|---|
| Unidad de trabajo diferido | `struct task` | `struct work_struct` |
| Cola | `struct taskqueue` | `struct workqueue_struct` |
| Cola compartida | `taskqueue_thread` | `system_wq` |
| Cola sin afinidad | `taskqueue_thread` con múltiples threads | `system_unbound_wq` |
| Crear una cola | `taskqueue_create` | `alloc_workqueue` |
| Encolar | `taskqueue_enqueue` | `queue_work` |
| Encolar con retardo | `taskqueue_enqueue_timeout` | `queue_delayed_work` |
| Esperar a que el trabajo termine | `taskqueue_drain` | `flush_work` |
| Destruir una cola | `taskqueue_free` | `destroy_workqueue` |
| Prioridad | `ta_priority` | flag `WQ_HIGHPRI` |
| Comportamiento de coalescencia | Automático, contador `pending` expuesto | comprobación `work_pending`, sin contador |

### Diferencias semánticas

**Visibilidad de la coalescencia.** FreeBSD expone el contador pendiente al callback; Linux no lo hace. Un callback de Linux sabe que el trabajo se disparó, pero no cuántas veces fue solicitado.

**Timeout task frente a delayed work.** El `timeout_task` de FreeBSD incorpora un callout; el `delayed_work` de Linux incorpora una `timer_list`. Ambos se comportan igual desde el punto de vista del usuario.

**Grouptaskqueues frente a percpu workqueues.** El `taskqgroup` de FreeBSD es explícito e independiente; el `alloc_workqueue(..., WQ_UNBOUND | WQ_CPU_INTENSIVE)` de Linux tiene semántica similar con opciones distintas.

**Integración con epoch.** FreeBSD dispone de `NET_TASK_INIT` para tareas que se ejecutan dentro del epoch de red; Linux no tiene un equivalente directo (el framework RCU es similar pero no idéntico).

Un driver portado de Linux a FreeBSD (o viceversa) puede trasladar el patrón de trabajo diferido casi de forma directa. Las diferencias estructurales están en las APIs circundantes (registro de dispositivos, asignación de memoria, locking) más que en el taskqueue en sí.



## Referencia: cuándo no usar un taskqueue

Una breve lista de escenarios en los que es preferible otro primitivo.

**El trabajo tiene requisitos temporales estrictos.** Los taskqueues añaden latencia de planificación. Para plazos a escala de microsegundos, un callout `C_DIRECT_EXEC` o un `taskqueue_swi` es más rápido. Para plazos a escala de nanosegundos, ninguno de los mecanismos de trabajo diferido es suficientemente rápido; el trabajo debe ocurrir de forma directa.

**El trabajo es una limpieza única que no tiene productor asociado.** Un simple `free` dentro de una ruta de teardown no necesita un taskqueue; basta con llamarlo directamente. Diferir por diferir no aporta ningún valor.

**El trabajo debe ejecutarse con una prioridad del planificador específica, superior a `PWAIT`.** Si el trabajo es genuinamente de alta prioridad (un driver en tiempo real, una tarea en el umbral de interrupción), usa `kthread_add` con una prioridad explícita en lugar de un taskqueue genérico.

**El trabajo requiere un contexto de thread específico que un worker genérico no puede proporcionar.** Las tareas se ejecutan en un thread del kernel sin ningún contexto de proceso de usuario concreto. El trabajo que necesita las credenciales de un usuario específico, la tabla de descriptores de archivo o el espacio de direcciones debe ocurrir dentro de ese proceso, no en una tarea.

**El driver solo tiene una tarea y se ejecuta con poca frecuencia.** Un único `kthread_add` con un bucle `cv_timedwait` puede ser más claro que la configuración completa del taskqueue. Usa el criterio adecuado; para tres o más tareas, los taskqueues son casi siempre más claros.

Para todo lo demás, usa un taskqueue. La norma predeterminada es «usar `taskqueue(9)`»; las excepciones son escasas.



## Referencia: lectura guiada de `subr_taskqueue.c`

Un ejercicio de lectura más, porque entender la implementación hace predecible el comportamiento de la API.

El archivo es `/usr/src/sys/kern/subr_taskqueue.c`. La estructura, brevemente:

**`struct taskqueue`.** Definida cerca del inicio del archivo. Contiene la cola pendiente (`tq_queue`), la lista de tareas activas (`tq_active`), el mutex interno (`tq_mutex`), el callback de encolado (`tq_enqueue`), los worker threads (`tq_threads`) y los flags.

**Macros `TQ_LOCK` / `TQ_UNLOCK`.** Justo después de la estructura. Adquieren el mutex (spin o sleep, según `tq_spin`).

**`taskqueue_create` y `_taskqueue_create`.** Asignan memoria a la estructura, inicializan el mutex (`MTX_DEF` o `MTX_SPIN`) y retornan.

**`taskqueue_enqueue` y `taskqueue_enqueue_flags`.** Adquieren el mutex, comprueban `task->ta_pending`, coalescen o enlazan, despiertan al worker mediante el callback `enqueue` y liberan el mutex.

**`taskqueue_enqueue_timeout`.** Programa el callout interno; el callback del callout llamará más tarde a `taskqueue_enqueue` sobre la tarea subyacente.

**`taskqueue_cancel` y `taskqueue_cancel_timeout`.** Eliminan de la cola si la tarea está pendiente; devuelven `EBUSY` si está en ejecución.

**`taskqueue_drain` y variantes.** Realizan un `msleep` sobre una condición que se activa cuando la tarea está inactiva y no pendiente.

**`taskqueue_run_locked`.** El núcleo del subsistema. En un bucle: toma una tarea de la cola pendiente, registra `ta_pending`, la borra, la mueve a activa, suelta el mutex, entra opcionalmente en el epoch de red, llama al callback, vuelve a adquirir el mutex y señaliza a los drainers. Repite hasta que la cola está vacía.

**`taskqueue_thread_loop`.** El bucle principal del worker thread. Adquiere el mutex del taskqueue, espera trabajo (`msleep`) si la cola está vacía, llama a `taskqueue_run_locked` cuando llega trabajo y repite.

**`taskqueue_free`.** Establece el flag de "draining", despierta a cada worker, espera a que cada worker termine, drena las tareas restantes y libera la estructura.

Esta lectura cita cada función por nombre en lugar de por número de línea, porque los números de línea cambian entre versiones de FreeBSD mientras que los nombres de símbolo permanecen. Si quieres coordenadas aproximadas para `subr_taskqueue.c` en FreeBSD 14.3, los puntos de entrada principales se encuentran cerca de estas líneas: `_taskqueue_create` 141, `taskqueue_create` 178, `taskqueue_free` 217, `taskqueue_enqueue_flags` 305, `taskqueue_enqueue` 317, `taskqueue_enqueue_timeout` 382, `taskqueue_run_locked` 485, `taskqueue_cancel` 579, `taskqueue_cancel_timeout` 591, `taskqueue_drain` 612, `taskqueue_thread_loop` 820. Toma esos números como una guía de desplazamiento; abre el archivo y salta al símbolo.

Leer estas funciones una vez es una buena inversión. Todo lo que el Capítulo 14 enseñó sobre el comportamiento de la API es visible en la implementación.



## Un recorrido final: cinco formas habituales

Cinco formas que representan la mayor parte del uso de taskqueues en el árbol de FreeBSD. Reconocerlas transforma la lectura del código fuente de un driver de un análisis línea a línea en reconocimiento de patrones.

### Forma A: la tarea en solitario

Una tarea, encolada desde un único lugar, drenada en detach. La forma más sencilla. La usan los drivers que necesitan diferir exactamente un tipo de trabajo.

```c
TASK_INIT(&sc->task, 0, sc_task, sc);
/* ... */
taskqueue_enqueue(sc->tq, &sc->task);
/* ... */
taskqueue_drain(sc->tq, &sc->task);
```

### Forma B: la división filtro más tarea

El filtro de interrupción hace lo mínimo indispensable y encola una tarea para el resto.

```c
static int
sc_filter(void *arg)
{
        struct sc *sc = arg;
        taskqueue_enqueue(sc->tq, &sc->intr_task);
        return (FILTER_HANDLED);
}
```

### Forma C: la tarea periódica dirigida por callout

El callout se dispara periódicamente y encola una tarea que realiza el trabajo.

```c
static void
sc_periodic_callout(void *arg)
{
        struct sc *sc = arg;
        taskqueue_enqueue(sc->tq, &sc->periodic_task);
        callout_reset(&sc->co, hz, sc_periodic_callout, sc);
}
```

### Forma D: la timeout task

`timeout_task` para trabajo diferido en contexto de thread.

```c
TIMEOUT_TASK_INIT(sc->tq, &sc->delayed, 0, sc_delayed, sc);
/* ... */
taskqueue_enqueue_timeout(sc->tq, &sc->delayed, delay_ticks);
/* ... */
taskqueue_drain_timeout(sc->tq, &sc->delayed);
```

### Forma E: la tarea que se vuelve a encolar a sí misma

Una tarea que se vuelve a programar a sí misma desde su propio callback.

```c
static void
sc_self(void *arg, int pending)
{
        struct sc *sc = arg;
        /* work */
        if (sc->keep_running)
                taskqueue_enqueue_timeout(sc->tq, &sc->self_tt, hz);
}
```

Cada driver que leas usará alguna combinación de estas cinco formas. Una vez que te resulten familiares, el resto son detalles de implementación.



## Resumen final: lo que entregó este capítulo

Un inventario breve, para el lector que quiere la versión comprimida tras haber trabajado con el capítulo completo.

**Conceptos introducidos.**

- El trabajo diferido como puente entre contextos de borde y el contexto de thread.
- Las estructuras de datos `struct task` / `struct timeout_task` y su ciclo de vida.
- Los taskqueues como par de cola más worker thread.
- Taskqueues privados frente a predefinidos y cuándo corresponde usar cada uno.
- Coalescencia mediante `ta_pending` y el argumento `pending`.
- Ordenación por prioridad dentro de una cola.
- Los primitivos `block`/`unblock`/`quiesce`/`drain_all`.
- El orden de detach con callouts, tareas, selinfo y el teardown del taskqueue.
- Depuración mediante `procstat`, `dtrace`, contadores sysctl y `WITNESS`.
- Introducción a `epoch(9)`, grouptaskqueues y taskqueues por CPU.

**Cambios en el driver.**

- Etapa 1: una tarea encolada desde `tick_source`, drenada en detach.
- Etapa 2: un taskqueue privado propiedad del driver.
- Etapa 3: una tarea de escritura masiva que demuestra coalescencia deliberada y un `timeout_task` para reset diferido.
- Etapa 4: consolidación, actualización de versión a `0.8-taskqueues`, regresión completa.

**Cambios en la documentación.**

- Una sección de Tasks en `LOCKING.md`.
- Una sección de orden de detach que enumera cada paso de drenado.
- Documentación por tarea con la lista del callback, el ciclo de vida, las rutas de encolado y las rutas de cancelación.

**Patrones catalogados.**

- Registro de eventos diferido.
- Reset diferido.
- División de interrupción en filtro más tarea.
- `copyin`/`copyout` asíncrono.
- Reintento con backoff.
- Teardown diferido.
- Renovación de estadísticas.
- Bloqueo durante la reconfiguración.
- Drain-all en el límite del subsistema.
- Generación de eventos sintéticos.

**Herramientas de depuración utilizadas.**

- `procstat -t` para el estado del thread del taskqueue.
- `ps ax` para el inventario de threads del kernel.
- `sysctl dev.<driver>` para los contadores expuestos por el driver.
- `dtrace` para la latencia de encolado y la duración del callback.
- `procstat -kk` para el diagnóstico de threads bloqueados.
- `WITNESS` e `INVARIANTS` como red de seguridad del kernel de depuración.

**Entregables.**

- `content/chapters/part3/chapter-14.md` (este archivo).
- `examples/part-03/ch14-taskqueues-and-deferred-work/stage1-first-task/`.
- `examples/part-03/ch14-taskqueues-and-deferred-work/stage2-private-taskqueue/`.
- `examples/part-03/ch14-taskqueues-and-deferred-work/stage3-coalescing/`.
- `examples/part-03/ch14-taskqueues-and-deferred-work/stage4-final/`.
- `examples/part-03/ch14-taskqueues-and-deferred-work/labs/` con `poll_waiter.c` y pequeños scripts auxiliares.
- `examples/part-03/ch14-taskqueues-and-deferred-work/LOCKING.md` con la sección Tasks.
- `examples/part-03/ch14-taskqueues-and-deferred-work/README.md` con instrucciones de compilación y prueba por etapa.

Esto es el final del Capítulo 14. El Capítulo 15 continúa la historia de la sincronización.


## Referencia: lectura línea por línea de `taskqueue_run_locked`

El núcleo del subsistema taskqueue es un bucle corto dentro de `taskqueue_run_locked` en `/usr/src/sys/kern/subr_taskqueue.c`. Leerlo una vez, despacio, resulta útil siempre que necesites razonar sobre el comportamiento del subsistema. A continuación se ofrece una lectura narrada.

La función es llamada desde el bucle principal del worker thread, `taskqueue_thread_loop`, con el mutex del taskqueue ya adquirido. Su cometido es procesar cada tarea pendiente, liberar el mutex alrededor del callback y retornar con el mutex todavía adquirido cuando la cola está vacía.

```c
static void
taskqueue_run_locked(struct taskqueue *queue)
{
        struct epoch_tracker et;
        struct taskqueue_busy tb;
        struct task *task;
        bool in_net_epoch;
        int pending;

        KASSERT(queue != NULL, ("tq is NULL"));
        TQ_ASSERT_LOCKED(queue);
        tb.tb_running = NULL;
        LIST_INSERT_HEAD(&queue->tq_active, &tb, tb_link);
        in_net_epoch = false;
```

La función comienza verificando que el mutex está tomado e insertando una estructura local `taskqueue_busy` en la lista de invocaciones activas. La estructura `tb` representa esta invocación de `taskqueue_run_locked`; el código posterior la utiliza para registrar qué está ejecutando esta invocación en cada momento. El indicador `in_net_epoch` registra si nos encontramos actualmente dentro del epoch de red, de modo que no lo entremos de nuevo cuando varias tareas consecutivas tienen el flag de red activado.

```c
        while ((task = STAILQ_FIRST(&queue->tq_queue)) != NULL) {
                STAILQ_REMOVE_HEAD(&queue->tq_queue, ta_link);
                if (queue->tq_hint == task)
                        queue->tq_hint = NULL;
                pending = task->ta_pending;
                task->ta_pending = 0;
                tb.tb_running = task;
                tb.tb_seq = ++queue->tq_seq;
                tb.tb_canceling = false;
                TQ_UNLOCK(queue);
```

El bucle principal. Se extrae la cabeza de la cola pendiente. Se copia el recuento de pending en una variable local y se resetea el campo a cero (para que los nuevos encolados que lleguen durante el callback incrementen desde cero). Se registra la tarea en la estructura `tb` para que los llamadores de drain puedan ver qué se está ejecutando. Se incrementa un contador de secuencia para la detección de drains obsoletos. Se libera el mutex.

Observa que entre este punto y el siguiente `TQ_LOCK`, el mutex no está tomado. Esta es la ventana durante la cual se ejecuta el callback; el resto del kernel puede encolar más tareas (que se fusionarán o se añadirán a la cola), drenar otras tareas (que verán `tb.tb_running == task` y esperarán), o continuar con su propio trabajo.

```c
                KASSERT(task->ta_func != NULL, ("task->ta_func is NULL"));
                if (!in_net_epoch && TASK_IS_NET(task)) {
                        in_net_epoch = true;
                        NET_EPOCH_ENTER(et);
                } else if (in_net_epoch && !TASK_IS_NET(task)) {
                        NET_EPOCH_EXIT(et);
                        in_net_epoch = false;
                }
                task->ta_func(task->ta_context, pending);

                TQ_LOCK(queue);
                wakeup(task);
        }
        if (in_net_epoch)
                NET_EPOCH_EXIT(et);
        LIST_REMOVE(&tb, tb_link);
}
```

La gestión del epoch: se entra en el epoch de red si esta tarea tiene el flag de red y todavía no estamos en él; se sale del epoch si habíamos entrado para una tarea anterior pero esta no tiene el flag de red. Esto permite que tareas de red consecutivas compartan una única entrada al epoch, una optimización que el framework realiza de forma transparente.

Se llama al callback con el contexto y el recuento de pending. Se vuelve a adquirir el mutex. Se despierta a cualquier llamador de drain que esté esperando esta tarea concreta. Se repite el bucle.

Tras el bucle, si aún se está en el epoch de red, se sale de él. Se elimina la estructura `tb` de la lista activa.

Siete observaciones tras leer esta función.

**Observación 1.** El mutex se libera exactamente durante el tiempo que dura la ejecución del callback. Ningún código interno del taskqueue se ejecuta junto con el callback; si el callback tarda milisegundos, el mutex del taskqueue permanece libre durante esos milisegundos.

**Observación 2.** `ta_pending` se resetea antes de ejecutar el callback, no después. Un nuevo encolado durante el callback vuelve a marcar la tarea como pendiente (pending=1). Cuando el callback retorna, el bucle detecta el nuevo pending, extrae la tarea y ejecuta el callback una segunda vez con pending=1. Ningún encolado se pierde.

**Observación 3.** El valor `pending` que se pasa al callback es el recuento en el momento en que la tarea fue extraída de la cola, no en el momento en que se produjeron las llamadas de encolado. Si llegan encolados durante el callback, no contribuyen al `pending` de esta invocación, sino al de la siguiente.

**Observación 4.** El wakeup al final del bucle despierta a los llamadores de drain que duermen sobre la dirección de la tarea. Drain utiliza `msleep(&task, &tq->mutex, ...)` y espera a que la tarea no esté en la cola y no se esté ejecutando. El wakeup en este punto es lo que hace que el drain finalice.

**Observación 5.** El contador de secuencia `tq_seq` y `tb.tb_seq` permiten que drain-all detecte si se han añadido nuevas tareas desde que comenzó el drain. Sin la secuencia, drain-all podría tener una condición de carrera con nuevos encolados.

**Observación 6.** `tb.tb_canceling` es un indicador que `taskqueue_cancel` activa para informar a quien espera de que la tarea está siendo cancelada en ese momento; su propósito es permitir la coordinación entre llamadas concurrentes de cancelación y drain. No lo hemos tratado en el texto principal porque la mayoría de los drivers nunca lo utilizan.

**Observación 7.** Varios threads de trabajo pueden estar dentro de `taskqueue_run_locked` simultáneamente, cada uno despachando una tarea distinta. La lista `tq_active` contiene todas sus estructuras `tb`. Tareas distintas de la misma cola se ejecutan en paralelo; la misma tarea no puede ejecutarse en paralelo consigo misma, porque solo un worker la extrae a la vez.

Estas observaciones en conjunto describen con exactitud qué garantiza el taskqueue y qué no. Todo el comportamiento descrito anteriormente en el capítulo es consecuencia de este breve bucle.

## Referencia: un recorrido por `taskqueue_drain`

Igual de ilustrativo, igual de breve. En `/usr/src/sys/kern/subr_taskqueue.c`, aproximadamente:

```c
void
taskqueue_drain(struct taskqueue *queue, struct task *task)
{
        if (!queue->tq_spin)
                WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL, ...);

        TQ_LOCK(queue);
        while (task->ta_pending != 0 || task_is_running(queue, task))
                TQ_SLEEP(queue, task, "taskqueue_drain");
        TQ_UNLOCK(queue);
}
```

La función adquiere el mutex del taskqueue y luego itera en bucle hasta que la tarea no está ni pendiente ni en ejecución. Cada iteración duerme en la dirección de la tarea; cada despertar al final de `taskqueue_run_locked` despierta al drenador para que vuelva a comprobarlo.

`task_is_running(queue, task)` recorre la lista activa (`tq_active`) y devuelve true si algún `tb.tb_running == task`. Es O(N) respecto al número de workers, pero para la mayoría de los drivers N es 1 y resulta O(1).

La función no mantiene el lock durante el sueño; `TQ_SLEEP` (que se expande a `msleep` o `msleep_spin`) libera el mutex durante el sueño y lo readquiere al despertar, que es el patrón estándar de variable de condición.

Observaciones al leer `taskqueue_drain`.

**Observación 1.** Drain es una espera sobre una variable de condición que usa el puntero de la tarea como canal de despertar. El despertar proviene del `wakeup(task)` al final de `taskqueue_run_locked`.

**Observación 2.** Drain no impide que se produzcan nuevas encolas. Si la tarea se encola de nuevo mientras drain está esperando, drain seguirá esperando hasta que esa nueva encola se ejecute y finalice. Por eso la disciplina de detach exige drenar todos los productores (callouts, otras tareas, manejadores de interrupción) antes de drenar la tarea objetivo.

**Observación 3.** Drain sobre una tarea ociosa (nunca encolada, o encolada y ya completada) retorna de inmediato. Es seguro llamar a drain de forma incondicional en detach.

**Observación 4.** Drain mantiene el mutex del taskqueue durante la comprobación inicial y antes del sueño, lo que significa que drain no puede generar una condición de carrera con la encola de forma que se pierda una tarea recién pendiente. Si llega una encola entre la comprobación y el sueño, `ta_pending` se vuelve distinto de cero y el bucle de drain reitera.

**Observación 5.** El `WITNESS_WARN` al inicio comprueba que el llamador se encuentra en un contexto donde dormir es válido. Si intentas llamar a `taskqueue_drain` desde un contexto que no puede dormir (el callback de un callout, por ejemplo), `WITNESS` se quejará.

Dos funciones complementarias son `taskqueue_cancel` (que elimina la tarea de la cola si está pendiente y devuelve `EBUSY` si está en ejecución) y `taskqueue_drain_timeout` (que también cancela el callout integrado). Vale la pena leer sus implementaciones al menos una vez; son breves.



## Referencia: el ciclo de vida visto desde el softc

Una perspectiva más, para tenerlo todo cubierto. La misma información, organizada en torno al softc en lugar de en torno a la API.

En el momento del **attach**, el softc adquiere:

- Un puntero al taskqueue (`sc->tq`), creado por `taskqueue_create` y poblado por `taskqueue_start_threads`.
- Una o más estructuras de tarea (`sc->foo_task`), inicializadas por `TASK_INIT` o `TIMEOUT_TASK_INIT`.
- Contadores e indicadores para observabilidad (opcionales pero recomendados).

En **tiempo de ejecución**, el estado del taskqueue en el softc es:

- `sc->tq` es un puntero opaco; los drivers nunca leen sus campos.
- `sc->foo_task` puede estar en estado IDLE, PENDING o RUNNING en cualquier instante.
- Los threads worker del taskqueue duermen la mayor parte del tiempo, despiertan al encolar una tarea, ejecutan los callbacks y vuelven a dormir.

En el momento del **detach**, el softc se desmonta en este orden:

1. Borrar `sc->is_attached` bajo el mutex, hacer broadcast de los cvs, liberar el mutex.
2. Drenar cada callout.
3. Drenar cada tarea.
4. Drenar selinfo.
5. Liberar el taskqueue.
6. Destruir el cdev y su alias.
7. Liberar el contexto de sysctl.
8. Destruir el cbuf y los contadores.
9. Destruir los cvs, sx, mutex.

Tras `taskqueue_free`, `sc->tq` queda inválido. Tras el drain de la tarea, las estructuras `sc->foo_task` están ociosas y su almacenamiento puede recuperarse junto con el softc.

El tiempo de vida del softc viene determinado por el attach/detach del dispositivo. Las tareas no pueden sobrevivir a su softc. El drain en detach es lo que garantiza esa propiedad.



## Referencia: glosario de términos

Para consulta rápida.

**Task.** Una instancia de `struct task`; un callback más su contexto, empaquetados para encolarlos en un taskqueue.

**Taskqueue.** Una instancia de `struct taskqueue`; una cola de tareas pendientes asociada a uno o más threads worker.

**Timeout task.** Una instancia de `struct timeout_task`; una tarea más un callout interno, utilizada para trabajo programado para un momento futuro.

**Enqueue.** Añadir una tarea a un taskqueue. Si la tarea ya está pendiente, incrementar su contador de pendientes en su lugar.

**Drain.** Esperar hasta que una tarea no esté ni pendiente ni en ejecución.

**Dispatch.** El acto por el que el worker del taskqueue extrae una tarea de la lista de pendientes y ejecuta su callback.

**Coalesce.** Fusionar encolas redundantes en un único incremento del estado pendiente en lugar de dos entradas en la lista.

**Pending count.** El valor de `ta_pending`, que representa cuántas encolas fusionadas se han acumulado en esta tarea.

**Idle task.** Una tarea que no está pendiente ni en ejecución. `ta_pending == 0` y ningún worker la tiene.

**Worker thread.** Un thread del kernel (habitualmente uno por taskqueue) cuyo trabajo es esperar trabajo y ejecutar los callbacks de las tareas.

**Edge context.** Un contexto restringido (callout, filtro de interrupción, sección epoch) en el que algunas operaciones no están permitidas.

**Thread context.** Un contexto ordinario de thread del kernel en el que están permitidos dormir, adquirir locks que pueden dormir y todas las operaciones estándar.

**Detach ordering.** La secuencia en la que las primitivas se drenan y liberan durante el detach del dispositivo, de modo que ninguna primitiva se libera mientras algo pueda seguir referenciándola.

**Drain race.** Un error por el que una primitiva se libera mientras un callback o manejador puede estar todavía en ejecución, causado por un orden de detach incorrecto.

**Pending-drop counter.** Un contador de diagnóstico que se incrementa cuando el argumento `pending` del callback es mayor que uno, lo que indica que se produjo fusión.

**Private taskqueue.** Un taskqueue propiedad del driver, creado y liberado junto con el attach/detach, no compartido con otros drivers.

**Shared taskqueue.** Un taskqueue proporcionado por el kernel (`taskqueue_thread`, `taskqueue_swi`, etc.) utilizado simultáneamente por múltiples drivers.

**Fast taskqueue.** Un taskqueue creado con `taskqueue_create_fast` que usa un spin mutex internamente, seguro para encolar desde un contexto de filtro de interrupción.

**Grouptaskqueue.** Una variante escalable en la que las tareas se distribuyen entre colas por CPU. Utilizada por drivers de red de alta tasa.

**Epoch.** Un mecanismo de sincronización de lecturas sin lock. El epoch `net_epoch_preempt` protege el estado de la red.



Aquí termina el capítulo 14. El siguiente capítulo lleva la historia de la sincronización más lejos.
