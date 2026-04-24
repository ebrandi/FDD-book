---
title: "Gestión de interrupciones"
description: "El capítulo 19 convierte el driver PCI del capítulo 18 en un driver con soporte de interrupciones. Explica qué son las interrupciones, cómo las modela y encamina FreeBSD, cómo un driver reclama un recurso IRQ y registra un manejador mediante `bus_setup_intr(9)`, cómo dividir el trabajo entre un filtro rápido y un ithread diferido, cómo gestionar IRQs compartidas de forma segura con `FILTER_STRAY` y `FILTER_HANDLED`, cómo simular interrupciones para pruebas sin eventos IRQ reales y cómo desmontar el manejador en detach. El driver evoluciona de 1.1-pci a 1.2-intr, incorpora un nuevo archivo específico para interrupciones y deja el capítulo 19 preparado para MSI y MSI-X en el capítulo 20."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 19
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 210
language: "es-ES"
---
# Manejo de interrupciones

## Orientación para el lector y resultados esperados

El capítulo 18 concluyó con un driver que por fin había encontrado hardware PCI real. El módulo `myfirst` en la versión `1.1-pci` detecta un dispositivo PCI por identificador de fabricante e identificador de dispositivo, se conecta como hijo legítimo de newbus en `pci0`, reclama el BAR del dispositivo mediante `bus_alloc_resource_any(9)` con `SYS_RES_MEMORY` y `RF_ACTIVE`, entrega el BAR a la capa de acceso del capítulo 16 para que `CSR_READ_4` y `CSR_WRITE_4` lean y escriban el silicio real, crea un cdev por instancia y desmonta todo en orden estrictamente inverso durante el detach. La simulación del capítulo 17 sigue en el árbol pero no se ejecuta en la ruta PCI; sus callouts permanecen en silencio para no escribir en los registros del dispositivo real.

Lo que el driver todavía no hace es reaccionar al dispositivo. Todos los accesos a registros realizados hasta ahora han sido iniciados por el driver: una llamada desde el espacio de usuario a `read` o `write` llega al cdev, el manejador del cdev adquiere `sc->mtx`, el acceso lee o escribe en el BAR, y el control regresa al espacio de usuario. Si el propio dispositivo tiene algo que comunicar, como "mi cola de recepción tiene un paquete", "un comando ha terminado" o "se ha superado un umbral de temperatura", el driver no tiene forma de escucharlo. El driver sondea; no escucha.

Eso es lo que el capítulo 19 corrige. Los dispositivos reales se comunican **interrumpiendo** la CPU. El tejido del bus lleva una señal del dispositivo al controlador de interrupciones, el controlador de interrupciones despacha la señal a una CPU, la CPU da un pequeño rodeo desde lo que estuviera haciendo, y un handler que el driver registró se ejecuta durante unos pocos microsegundos. La tarea del handler es pequeña: averiguar qué quiere el dispositivo, reconocer la interrupción en el dispositivo, realizar el pequeño trabajo que es seguro hacer aquí, y delegar el resto a un thread que tenga la libertad de bloquearse, dormir o tomar locks lentos. La delegación es la segunda mitad de la disciplina moderna de interrupciones; la primera mitad es simplemente conseguir que el handler se ejecute.

El alcance del capítulo 19 es precisamente la ruta central de interrupciones: qué son las interrupciones a nivel hardware, cómo las modela FreeBSD en el kernel, cómo un driver asigna un recurso IRQ y registra un handler mediante `bus_setup_intr(9)`, cómo funciona la división entre un handler de filtro rápido y un handler de ithread diferido, a qué compromete al driver el flag `INTR_MPSAFE`, cómo simular interrupciones para pruebas cuando no es fácil producir eventos reales, cómo comportarse correctamente en una línea IRQ compartida que otros drivers también escuchan, y cómo desmontar todo esto en detach sin perder recursos ni ejecutar handlers obsoletos. El capítulo se detiene antes de llegar a MSI y MSI-X, que pertenecen al capítulo 20; esos mecanismos se construyen sobre el handler central que el lector escribe aquí, y enseñar ambos a la vez diluiría los dos.

El capítulo 19 deja de lado territorio que el trabajo con interrupciones toca de forma natural. MSI y MSI-X, handlers por vector, coalescencia de interrupciones y enrutamiento de interrupciones por cola pertenecen al capítulo 20. DMA y la interacción entre interrupciones y anillos de descriptores DMA corresponden a los capítulos 20 y 21. Las estrategias avanzadas de afinidad de interrupciones en plataformas NUMA se mencionan brevemente, pero el tratamiento en profundidad corresponde al capítulo 20 y siguientes. El enrutamiento de interrupciones específico de cada plataforma (GICv3 en arm64, APIC en x86, NVIC en objetivos embebidos) se menciona únicamente para establecer vocabulario; el enfoque del libro es la API visible para el driver que oculta esas diferencias. El capítulo 19 permanece dentro del terreno que puede cubrir bien y transfiere el control explícitamente cuando un tema merece su propio capítulo.

El modelo filter-plus-ithread que enseña el capítulo 19 no está solo. El capítulo 16 dio al driver un vocabulario de acceso a registros. El capítulo 17 le enseñó a pensar como un dispositivo. El capítulo 18 lo presentó a un dispositivo PCI real. El capítulo 19 le da oídos. Los capítulos 20 y 21 le darán piernas: acceso directo a memoria para que el dispositivo pueda alcanzar la RAM sin que el driver intervenga. Cada capítulo añade una capa. Cada capa depende de las anteriores. El capítulo 19 es donde el driver deja de sondear y empieza a escuchar, y las disciplinas que construyó la parte 3 son las que garantizan que esa escucha sea correcta.

### Por qué el manejo de interrupciones merece un capítulo propio

Una preocupación que surge aquí es si `bus_setup_intr(9)` y el modelo filter-plus-ithread justifican realmente un capítulo completo. La simulación del capítulo 17 usaba callouts para producir cambios de estado autónomos; el driver del capítulo 18 corre sobre PCI real pero ignora la línea de interrupción por completo. ¿No podríamos seguir sondeando mediante callouts y evitar el tema?

Dos razones.

La primera es el rendimiento. Un callout que sondea el dispositivo diez veces por segundo desperdicia tiempo de CPU cuando no hay nada que hacer y se pierde eventos que ocurren entre sondeos. Un dispositivo real puede producir muchos eventos por milisegundo; un intervalo de sondeo de 100 milisegundos se pierde casi todos. Las interrupciones invierten el coste: no se consume CPU cuando no pasa nada, y el handler se ejecuta a los pocos microsegundos del evento. Todos los drivers serios de FreeBSD usan interrupciones por la misma razón; un driver que sondea es un driver con una excusa especial.

La segunda es la corrección. Algunos dispositivos requieren que el driver responda dentro de una ventana de tiempo ajustada. El FIFO de recepción de una tarjeta de red se llena en unos pocos microsegundos; si el driver no lo vacía, la tarjeta descarta paquetes. El FIFO de transmisión de un puerto serie se vacía a la velocidad del cable; si el driver no lo rellena, el transmisor se queda sin datos. Cualquier intervalo de sondeo suficientemente largo como para ser económico es también suficientemente largo como para perder plazos. Las interrupciones son el único mecanismo que permite a un driver cumplir los requisitos en tiempo real del dispositivo sin consumir una CPU a pleno rendimiento.

El capítulo también gana su lugar enseñando una disciplina que se aplica mucho más allá de PCI. El modelo de interrupciones de FreeBSD (filter más ithread, `INTR_MPSAFE`, `bus_setup_intr(9)`, desmontaje limpio en detach) es el mismo modelo que usan los drivers USB, el mismo que usan los drivers SDIO, el mismo que usan los drivers virtio y el mismo que usan los drivers SoC de arm64. Un lector que entiende el modelo del capítulo 19 puede leer el handler de interrupciones de cualquier driver de FreeBSD y entender lo que hace. Esa generalidad es lo que hace que valga la pena leer el capítulo con atención, incluso para lectores que no trabajarán con PCI.

### Dónde dejó el driver el capítulo 18

Un breve punto de control antes de continuar. El capítulo 19 extiende el driver producido al final de la etapa 4 del capítulo 18, etiquetado como versión `1.1-pci`. Si alguno de los puntos siguientes parece incierto, vuelve al capítulo 18 antes de comenzar este.

- Tu driver compila sin errores y se identifica como `1.1-pci` en `kldstat -v`.
- En un invitado bhyve o QEMU que exponga un dispositivo virtio-rnd (fabricante `0x1af4`, dispositivo `0x1005`), el driver se conecta a través de `myfirst_pci_probe` y `myfirst_pci_attach`, imprime su banner, reclama el BAR 0 como `SYS_RES_MEMORY` con `RF_ACTIVE`, recorre la lista de capacidades PCI y crea `/dev/myfirst0`.
- El softc contiene el puntero al recurso BAR (`sc->bar_res`), el ID de recurso (`sc->bar_rid`) y el flag `pci_attached`.
- La ruta de detach destruye el cdev, detiene los callouts y tasks activos, desconecta la capa de hardware, libera el BAR y desinicializa el softc.
- El script de regresión completo del capítulo 18 pasa: attach, ejercicio del cdev, detach, descarga del módulo, sin pérdidas de recursos.
- `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md` y `PCI.md` están actualizados.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` están habilitados en tu kernel de prueba.

Ese driver es el que extiende el capítulo 19. Las adiciones son de nuevo modestas en volumen: un nuevo archivo (`myfirst_intr.c`), una nueva cabecera (`myfirst_intr.h`), un pequeño conjunto de nuevos campos en el softc (`irq_res`, `irq_rid`, `intr_cookie`, uno o dos contadores), tres nuevas funciones en el archivo de interrupciones (configuración, desmontaje, el handler de filtro), un sysctl para interrupciones simuladas, un incremento de versión a `1.2-intr` y un breve documento `INTERRUPTS.md`. El cambio en el modelo mental es de nuevo mayor de lo que sugiere el número de líneas: el driver tiene por fin dos hilos de control en lugar de uno, y la disciplina que evita que se interfieran entre sí es nueva.

### Lo que aprenderás

Al finalizar este capítulo serás capaz de:

- Explicar qué es una interrupción a nivel hardware, la diferencia entre señalización por flanco y por nivel, y cómo el flujo de manejo de interrupciones de una CPU va desde el dispositivo hasta el handler del driver.
- Describir cómo FreeBSD representa los eventos de interrupción: qué es un evento de interrupción (`intr_event`), qué es un interrupt thread (`ithread`), qué es un handler de filtro y por qué importa la división entre filtros e ithreads.
- Leer la salida de `vmstat -i` y `devinfo -v` e identificar las interrupciones que gestiona tu sistema, sus contadores y los drivers asociados a cada una.
- Asignar un recurso IRQ mediante `bus_alloc_resource_any(9)` con `SYS_RES_IRQ`, usando `rid = 0` en una línea PCI heredada y (en el capítulo 20) RIDs distintos de cero para vectores MSI y MSI-X.
- Registrar un handler de interrupciones mediante `bus_setup_intr(9)`, eligiendo entre un handler de filtro (`driver_filter_t`), un handler de ithread (`driver_intr_t`) o la combinación filter-plus-ithread, y seleccionando el flag `INTR_TYPE_*` correcto para la clase de trabajo del dispositivo.
- Escribir un handler de filtro mínimo que lea el registro de estado del dispositivo, reconozca la interrupción en el dispositivo, devuelva `FILTER_HANDLED` o `FILTER_STRAY` de forma apropiada y coopere con la maquinaria de interrupciones del kernel.
- Saber qué es seguro dentro de un filtro (solo spin locks, sin `malloc`, sin dormir, sin locks de bloqueo) y qué relaja el ithread (sleep mutexes, variables de condición, `malloc(M_WAITOK)`), y por qué existen esas restricciones.
- Establecer `INTR_MPSAFE` solo cuando realmente se desee, y comprender a qué compromete ese flag al driver (sincronización propia, sin adquisición implícita de Giant, el derecho a ejecutarse en cualquier CPU de forma concurrente).
- Delegar trabajo diferido desde un handler de filtro a una task de taskqueue o al ithread, y preservar la disciplina de que el trabajo pequeño y urgente ocurre en el filtro mientras el trabajo en bloque ocurre en contexto de thread.
- Simular interrupciones mediante un sysctl que invoca el handler directamente bajo las reglas normales de locking, para poder ejercitar la máquina de estados del handler sin necesidad de que se dispare un IRQ real.
- Manejar correctamente líneas IRQ compartidas: leer primero el registro INTR_STATUS del dispositivo, decidir si esta interrupción pertenece a nuestro dispositivo, devolver `FILTER_STRAY` si no es así, y evitar apropiarse del trabajo de otro driver.
- Desmontar el handler de interrupciones en detach con `bus_teardown_intr(9)` antes de liberar el IRQ con `bus_release_resource(9)`, y estructurar la ruta de detach para que ninguna interrupción pueda dispararse contra un estado ya liberado.
- Reconocer qué es una tormenta de interrupciones, saber cómo la maquinaria `hw.intr_storm_threshold` de FreeBSD la detecta y comprender las causas comunes en el lado del dispositivo (no borrar INTR_STATUS, líneas por flanco mal configuradas como por nivel).
- Vincular una interrupción a una CPU específica mediante `bus_bind_intr(9)` cuando la afinidad importa, y describir la interrupción a `devinfo -v` mediante `bus_describe_intr(9)` para que los operadores puedan ver qué handler está en qué CPU.
- Separar el código relacionado con interrupciones en su propio archivo, actualizar la línea `SRCS` del módulo, etiquetar el driver como `1.2-intr` y producir un breve documento `INTERRUPTS.md` que describa el comportamiento del handler y la disciplina de trabajo diferido.

La lista es larga; cada elemento es específico. El propósito del capítulo es la composición.

### Lo que este capítulo no cubre

Varios temas adyacentes se difieren explícitamente para que el capítulo 19 permanezca centrado.

- **MSI y MSI-X.** `pci_alloc_msi(9)`, `pci_alloc_msix(9)`, la asignación de vectores, los manejadores por vector y la distribución de la tabla MSI-X son materia del Capítulo 20. El Capítulo 19 se centra en la línea PCI INTx heredada, asignada con `rid = 0`; el vocabulario se transfiere, pero la mecánica por vector no.
- **DMA.** Las etiquetas de `bus_dma(9)`, las listas scatter-gather, los bounce buffers, la coherencia de caché alrededor de los descriptores DMA y la forma en que las interrupciones señalan la finalización de las transferencias en anillos de descriptores corresponden a los Capítulos 20 y 21. El manejador del Capítulo 19 lee un registro de una BAR y decide qué hacer; no toca DMA.
- **Redes con múltiples colas.** Los NICs modernos tienen colas de recepción y transmisión separadas, con vectores MSI-X independientes y manejadores de interrupción independientes. El framework `iflib(9)` se apoya en esto; `em(4)`, `ix(4)` e `ixl(4)` lo utilizan. El driver del Capítulo 19 tiene una sola interrupción; a partir del Capítulo 20 se desarrolla la historia de las múltiples colas.
- **Afinidad de interrupciones profunda en hardware NUMA.** Se presenta `bus_bind_intr`; las estrategias elaboradas para anclar interrupciones a CPUs próximas al puerto raíz PCIe del dispositivo quedan para capítulos posteriores dedicados a la escalabilidad.
- **Suspend y resume del driver en torno a las interrupciones.** `bus_suspend_intr(9)` y `bus_resume_intr(9)` existen; se mencionan por completitud, pero no se ejercitan en el driver del Capítulo 19.
- **Manipulación de prioridad de interrupciones en tiempo real.** `intr_priority(9)` de FreeBSD y los flags `INTR_TYPE_*` influyen en la prioridad del ithread, pero el libro trata el sistema de prioridades como una caja negra fuera de los capítulos de temas avanzados.
- **Interrupciones exclusivamente software (SWI).** `swi_add(9)` crea una interrupción software pura que un driver puede programar desde cualquier contexto. El capítulo menciona los SWI al tratar el trabajo diferido, pero el patrón moderno preferido (un taskqueue) cubre los mismos casos de uso con menos riesgos.

Mantenerse dentro de esos límites convierte el Capítulo 19 en un capítulo centrado en el manejo fundamental de interrupciones. El vocabulario es lo que se transfiere; los capítulos concretos que siguen aplican ese vocabulario a MSI/MSI-X, DMA y diseños multi-cola.

### Tiempo estimado de dedicación

- **Solo lectura**: cuatro o cinco horas. El modelo de interrupciones es conceptualmente pequeño, pero requiere una lectura cuidadosa, especialmente en lo que respecta a la distinción entre filter e ithread y las reglas de seguridad dentro de un filter.
- **Lectura más escritura de los ejemplos desarrollados**: diez a doce horas distribuidas en dos o tres sesiones. El driver evoluciona en cuatro etapas; cada etapa es una extensión pequeña pero real sobre la base de código del Capítulo 18.
- **Lectura más todos los laboratorios y desafíos**: dieciséis a veinte horas distribuidas en cuatro o cinco sesiones, incluyendo poner en marcha el laboratorio de bhyve (si el entorno del Capítulo 18 no está ya disponible), leer la ruta de interrupción de `if_em.c` y el filter handler de `if_mgb.c`, y ejecutar la regresión del Capítulo 19 tanto contra la ruta de interrupción simulada como, cuando sea posible, contra una ruta de interrupción real.

Las secciones 3, 4 y 6 son las más densas. Si la distinción entre filter e ithread resulta poco familiar en la primera lectura, es completamente normal. Para, vuelve a leer el árbol de decisión de la Sección 3 y continúa cuando el esquema se haya asentado.

### Requisitos previos

Antes de empezar este capítulo, comprueba:

- El código fuente de tu driver coincide con el Stage 4 del Capítulo 18 (`1.1-pci`). El punto de partida asume la capa de hardware del Capítulo 16, el backend de simulación del Capítulo 17, el attach PCI del Capítulo 18, la familia completa de accesores `CSR_*`, la cabecera de sincronización y todos los primitivos introducidos en la Parte 3.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con `/usr/src` en disco y coincidente con el kernel en ejecución.
- Un kernel de depuración con `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` está compilado, instalado y arrancando sin errores.
- `bhyve(8)` o `qemu-system-x86_64` está disponible, y el entorno de laboratorio del Capítulo 18 (un guest FreeBSD con un dispositivo virtio-rnd en `-s 4:0,virtio-rnd`) es reproducible a demanda.
- Las herramientas `devinfo(8)`, `vmstat(8)` y `pciconf(8)` están en tu path. Las tres forman parte del sistema base.

Si alguno de los puntos anteriores es inestable, corrígelo ahora en lugar de seguir avanzando por el Capítulo 19 e intentar razonar desde una base inestable. Los bugs de interrupción suelen manifestarse como kernel panics o corrupciones sutiles bajo carga; el `WITNESS` del kernel de depuración en particular detecta las clases más comunes de errores de lock desde el principio.

### Cómo sacar el máximo partido a este capítulo

Cuatro hábitos darán sus frutos rápidamente.

En primer lugar, ten `/usr/src/sys/sys/bus.h` y `/usr/src/sys/kern/kern_intr.c` a mano. El primero define `driver_filter_t`, `driver_intr_t`, `INTR_TYPE_*`, `INTR_MPSAFE` y los valores de retorno `FILTER_*` que usarás en cada handler. El segundo es la maquinaria de eventos de interrupción del kernel: el código que recibe el IRQ de bajo nivel, despacha a los filters, despierta ithreads y detecta tormentas de interrupciones. No necesitas leer `kern_intr.c` en profundidad, pero recorrer las primeras mil líneas una vez te da una imagen clara de lo que ocurre entre «el dispositivo afirma el IRQ 19» y «se llama a tu filter».

En segundo lugar, ejecuta `vmstat -i` en tu host de laboratorio y en tu guest, y mantén la salida abierta en un terminal mientras lees. Cada concepto que introducen las secciones 2 y 3 (contadores por handler, afinidad por CPU, convenciones de nomenclatura de interrupciones) es visible en esa salida. Un lector que ha observado detenidamente `vmstat -i` en su propia máquina encuentra el enrutamiento de interrupciones mucho menos abstracto.

En tercer lugar, escribe los cambios a mano y ejecuta cada etapa. El código de interrupciones es donde los pequeños errores se convierten en bugs silenciosos. Olvidar `FILTER_HANDLED` convierte tu handler en técnicamente ilegal; olvidar `INTR_MPSAFE` adquiere Giant en silencio alrededor de tu handler; olvidar limpiar INTR_STATUS produce una tormenta de interrupciones cinco milisegundos después. Escribir cada línea, comprobar la salida de `dmesg` tras cada `kldload` y observar `vmstat -i` entre iteraciones es la forma de detectar estos errores en el momento en que tienen poco coste.

En cuarto lugar, lee `/usr/src/sys/dev/mgb/if_mgb.c` (busca `mgb_legacy_intr` y `mgb_admin_intr`) después de la Sección 4. `mgb(4)` es el driver para los controladores Ethernet Gigabit LAN743x de Microchip. Su ruta de interrupción es un ejemplo limpio y legible de diseño filter-plus-ithread, y se sitúa aproximadamente en el nivel de complejidad que enseña el Capítulo 19. Setecientas líneas de lectura cuidadosa dan sus frutos a lo largo del resto de la Parte 4.

### Itinerario por el capítulo

Las secciones, en orden, son:

1. **¿Qué son las interrupciones?** La imagen del hardware: qué es una interrupción, el disparo por flanco frente al disparo por nivel, el flujo de despacho de la CPU y lo mínimo que debe hacer un driver cuando llega una. Los cimientos conceptuales.
2. **Las interrupciones en FreeBSD.** Cómo representa el kernel los eventos de interrupción, qué es un ithread, cómo se cuentan y muestran las interrupciones mediante `vmstat -i` y `devinfo -v`, y qué ocurre desde la línea IRQ hasta el handler del driver.
3. **Registro de un handler de interrupción.** El código que escribe el driver: `bus_alloc_resource_any(9)` con `SYS_RES_IRQ`, `bus_setup_intr(9)`, flags `INTR_TYPE_*`, `INTR_MPSAFE`, `bus_describe_intr(9)`. La primera etapa del driver del Capítulo 19 (`1.2-intr-stage1`).
4. **Escritura de un handler de interrupción real.** La forma del filter handler: leer INTR_STATUS, decidir la propiedad, confirmar el dispositivo, devolver el valor `FILTER_*` correcto. La forma del ithread handler: adquirir sleep locks, delegar en un taskqueue, hacer el trabajo lento. Etapa 2 (`1.2-intr-stage2`).
5. **Uso de interrupciones simuladas para pruebas.** Un sysctl que invoca el handler de forma síncrona con la disciplina de lock real, para que puedas ejercitar el handler sin un IRQ real. Etapa 3 (`1.2-intr-stage3`).
6. **Gestión de interrupciones compartidas.** Por qué `RF_SHAREABLE` es importante en una línea PCI legacy, cómo un filter handler debe decidir la propiedad frente a otros handlers en el mismo IRQ, y cómo evitar la inanición. Sin cambio de etapa; esto es una disciplina, no un nuevo artefacto de código.
7. **Limpieza de recursos de interrupción.** Primero `bus_teardown_intr(9)`, luego `bus_release_resource(9)`. El orden de detach ahora tiene dos pasos más, y la cascada de fallo parcial recibe una etiqueta más.
8. **Refactorización y versionado de tu driver con soporte de interrupciones.** La división final en `myfirst_intr.c`, un nuevo `INTERRUPTS.md`, el salto de versión a `1.2-intr` y el pase de regresión. Etapa 4.

Tras las ocho secciones vienen los laboratorios prácticos, los ejercicios de desafío, una referencia de resolución de problemas, un cierre que clausura la historia del Capítulo 19 y abre la del Capítulo 20, y un puente hacia el Capítulo 20. El material de referencia y hoja de consulta rápida al final del capítulo está pensado para ser releído mientras avanzas por los capítulos 20 y 21; el vocabulario del Capítulo 19 es la base sobre la que se construyen ambos.

Si es tu primera lectura, avanza de forma lineal y haz los laboratorios en orden. Si estás repasando, las secciones 3 y 4 son autocontenidas y se prestan bien a una lectura en una sola sesión.



## Sección 1: ¿Qué son las interrupciones?

Antes del código del driver, la imagen del hardware. La Sección 1 enseña qué es una interrupción al nivel de la CPU y el bus, sin ningún vocabulario específico de FreeBSD. Un lector que comprende la Sección 1 puede leer el resto del capítulo con la ruta de interrupción del kernel como un objeto concreto en lugar de una abstracción vaga. La recompensa es que cada sección posterior resulta más fácil.

El resumen en una frase, que puedes llevar contigo a lo largo del resto del capítulo: una interrupción es una forma de que un dispositivo interrumpa el trabajo actual de la CPU, ejecute el handler de un driver durante un breve tiempo y luego deje que la CPU vuelva a lo que estaba haciendo. Todo lo demás es mecanismo alrededor de esa frase.

### El problema que resuelven las interrupciones

Una CPU ejecuta un flujo de instrucciones en orden. Cada instrucción se completa, el contador de programa avanza, la siguiente instrucción se ejecuta, y así sucesivamente. Abandonada a sí misma, la CPU ejecutaría un programa hasta que terminase, luego otro, y así sucesivamente, sin prestar atención a nada que ocurriese fuera de su propio flujo de instrucciones.

Así no es como funcionan los ordenadores. Una tecla pulsada durante medio segundo genera cuatro o cinco eventos separados; un paquete de red llega a escasos microsegundos del anterior; un disco termina una lectura, el controlador del ventilador supera un umbral de temperatura, el valor de un sensor se actualiza, un temporizador expira. Cada uno de estos eventos ocurre fuera del control directo de la CPU, en un momento que la CPU no eligió. La CPU tiene que enterarse.

Una forma de enterarse es mediante polling. La CPU puede consultar periódicamente el registro de estado del dispositivo. Si el registro de estado indica «tengo datos», la CPU los lee. Si indica «no tengo nada», la CPU continúa. El polling funciona bien para dispositivos cuyos eventos son poco frecuentes, predecibles y no sensibles al tiempo. Funciona mal para todo lo demás. Un teclado sondeado cada cien milisegundos se nota lento. Una tarjeta de red sondeada cada milisegundo sigue perdiendo la mayoría de sus paquetes. Y el polling consume tiempo de CPU proporcional a la frecuencia de sondeo, incluso cuando no ocurre nada.

La otra forma de enterarse es dejar que el dispositivo se lo diga a la CPU. Eso es una interrupción. El dispositivo eleva una señal en un cable o envía un mensaje por el bus. La CPU interrumpe su trabajo actual, recuerda dónde estaba, ejecuta un pequeño fragmento de código que pregunta al dispositivo qué ocurrió, responde adecuadamente y después reanuda el trabajo que estaba haciendo. La disciplina para escribir ese «pequeño fragmento de código» es lo que enseña el resto del Capítulo 19.

### Qué es realmente una interrupción de hardware

Físicamente, una interrupción de hardware comienza como una señal en un cable (o, más habitualmente en los sistemas modernos, como un mensaje en un bus). Un dispositivo afirma la señal cuando ha ocurrido algo que el sistema operativo necesita saber. Algunos ejemplos:

- Una tarjeta de red afirma su línea IRQ cuando ha llegado un paquete y está esperando en su FIFO de recepción.
- Un UART serie afirma su línea IRQ cuando ha llegado un byte al receptor, o cuando el FIFO de transmisión ha caído por debajo de un umbral.
- Un controlador SATA afirma su línea IRQ cuando ha completado una entrada de la cola de comandos.
- Un chip temporizador afirma su línea IRQ cuando ha transcurrido un intervalo programado.
- Un sensor de temperatura afirma su línea IRQ cuando se supera un umbral programado.

La afirmación es la forma del dispositivo de decir «algo que necesito que sepas». La CPU y el sistema operativo deben estar preparados para responder. La ruta desde «señal afirmada» hasta «handler invocado» pasa por el controlador de interrupciones, el mecanismo de despacho de interrupciones de la CPU y la maquinaria de eventos de interrupción del kernel. La Sección 2 recorre toda la ruta; esta subsección se mantiene en el nivel del hardware.

Conviene conocer algunos datos útiles sobre la señalización en sí.

En primer lugar, **las líneas de interrupción suelen ser compartidas**. Una CPU tiene un número reducido de entradas de interrupción, generalmente entre dieciséis y veinticuatro en los PC legacy y más en las plataformas modernas a través de APICs y GICs. Un sistema suele tener más dispositivos que entradas de interrupción, por lo que varios dispositivos comparten una sola línea. Cuando se dispara una interrupción en una línea compartida, cada driver cuyo dispositivo podría ser el origen debe comprobarlo: ¿es esta mi interrupción? Si no, devolver una indicación de «stray»; si sí, gestionarla. La Sección 6 cubre el protocolo de interrupciones compartidas.

En segundo lugar, **la señalización de interrupciones tiene dos modalidades**. La señalización por flanco significa que la interrupción se señaliza mediante una transición en el cable (de bajo a alto, o de alto a bajo). La señalización por nivel significa que la interrupción se señaliza manteniendo el cable en un nivel específico (alto o bajo) mientras la interrupción esté pendiente. Las dos modalidades tienen consecuencias operativas distintas, que analiza la siguiente subsección.

En tercer lugar, **la interrupción es asíncrona respecto a la CPU**. La CPU no sabe cuándo el dispositivo elevará la señal. El handler del driver debe tolerar ser invocado en cualquier punto del propio trabajo del driver, y debe sincronizarse adecuadamente con su propio código fuera de interrupción. La disciplina de locking del Capítulo 11 es lo que usa el driver para conseguirlo.

En cuarto lugar, **la interrupción no lleva prácticamente ninguna información por sí misma**. El cable dice «algo ocurrió»; no dice qué. El driver descubre qué ocurrió leyendo el registro de estado del dispositivo. Una sola línea IRQ puede notificar muchos eventos distintos (datos de recepción listos, FIFO de transmisión vacío, cambio de estado del enlace, error, etc.), y es responsabilidad del driver decodificar los bits de estado y decidir qué hacer.

### Disparado por flanco vs disparado por nivel

La diferencia merece comprenderse bien porque explica por qué ciertos bugs producen tormentas de interrupciones, otros producen interrupciones perdidas silenciosamente, y otros producen sistemas bloqueados.

Una interrupción **disparada por flanco** se activa una sola vez cuando la señal realiza una transición. El dispositivo pone el cable en nivel bajo (en una línea activa baja); el controlador de interrupciones detecta la transición y encola una interrupción para el CPU. Si el dispositivo continúa manteniendo el cable en nivel bajo, no se generan interrupciones adicionales, porque la señal no está transitando, sino que simplemente sigue asertada. Para que se genere una nueva interrupción, el dispositivo debe liberar el cable y volver a asertarlo.

Las interrupciones disparadas por flanco son eficientes. El controlador de interrupciones solo necesita rastrear transiciones, no señales continuas. El inconveniente es su fragilidad: si una interrupción se activa mientras el controlador no está atento (porque, por ejemplo, está gestionando otra interrupción), la transición puede perderse. Los controladores de interrupciones modernos encolan las interrupciones disparadas por flanco para evitar la mayoría de estos casos, pero el riesgo es real, y algunos drivers (o algunos bugs de dispositivo) producen configuraciones de flanco que ocasionalmente pierden un evento.

Una interrupción **disparada por nivel** se activa de forma continua mientras la señal está asertada. Mientras el dispositivo mantiene el cable en el nivel asertado, el controlador de interrupciones sigue notificando una interrupción. Cuando el dispositivo libera el cable, el controlador deja de notificar. El CPU recibe la interrupción, el handler del driver se ejecuta, el handler lee el estado del dispositivo y borra la condición pendiente, el dispositivo deja de asertar la señal y el controlador de interrupciones deja de notificar. Si el handler no logra borrar la condición pendiente, la señal permanece asertada, el controlador sigue notificando y el handler del driver se llama de nuevo de inmediato, en un bucle que consume el CPU. Esta es la clásica **tormenta de interrupciones**.

Las interrupciones disparadas por nivel son robustas. Mientras el dispositivo tenga algo que reportar, el sistema operativo lo sabrá; no existe ninguna ventana temporal en la que pueda perderse un evento. El costo es que un driver con bugs puede producir una tormenta. FreeBSD dispone de detección de tormentas para mitigar esto (el apéndice *A Deeper Look at Interrupt Storm Detection*, más adelante en el capítulo, lo cubre); otros sistemas operativos tienen protecciones similares. La regla práctica habitual es que el disparo por nivel es el valor predeterminado más seguro, y las líneas INTx heredadas de PCI son disparadas por nivel precisamente por eso.

La distinción importa al autor de drivers en algunos casos concretos:

- Un driver que no borra el registro INTR_STATUS del dispositivo antes de retornar del handler producirá una tormenta de interrupciones en una línea disparada por nivel. En una línea disparada por flanco, el mismo bug produce una interrupción perdida en su lugar.
- Un driver que lee y escribe INTR_STATUS correctamente funciona en ambos tipos sin necesidad de conocimiento especial.
- Un driver que manipula directamente el modo de disparo del controlador de interrupciones (algo raro; mayormente en hardware heredado) debe comprender la distinción.

Para el driver PCI del Capítulo 19, la señalización es INTx disparada por nivel en la ruta heredada. Con MSI y MSI-X (Capítulo 20), la señalización es por mensaje y no corresponde directamente ni al disparo por flanco ni al disparo por nivel, pero el patrón del driver es el mismo: leer el estado, reconocer el dispositivo y retornar.

### El flujo de gestión de interrupciones del CPU, simplificado

¿Qué ocurre, paso a paso, cuando se aserta la línea IRQ de un dispositivo? Una traza simplificada en un sistema x86 moderno:

1. El dispositivo aserta su IRQ en el bus (o envía un paquete MSI, en el caso de PCIe con MSI habilitado).
2. El controlador de interrupciones del sistema (APIC en x86, GIC en arm64) recibe la señal y determina qué CPU debe gestionarla, según la afinidad configurada. En sistemas multi-CPU, esta es una decisión configurable.
3. El hardware de interrupciones del CPU elegido detecta la interrupción pendiente. Antes de completar la instrucción en curso, el CPU guarda suficiente estado (el contador de programa, el registro de flags y algunos otros campos) para retomar el trabajo interrumpido más tarde.
4. El CPU salta a un vector en su tabla de descriptores de interrupciones. La entrada para ese vector es un pequeño fragmento de código del kernel denominado **trap stub** que hace la transición al modo supervisor, guarda el conjunto de registros del thread interrumpido y llama al código de despacho de interrupciones del kernel.
5. El código de despacho de interrupciones del kernel encuentra el `intr_event` asociado al IRQ (esta es la estructura de FreeBSD que cubre la Sección 2) y llama a los handlers del driver asociados a él.
6. El handler filter del driver se ejecuta. Lee el registro de estado del dispositivo, decide qué tipo de evento ha ocurrido, escribe el registro INTR_STATUS del dispositivo para reconocer el evento (de modo que el dispositivo deje de asertar la línea, en el caso del disparo por nivel) y devuelve un valor que indica al kernel qué hacer a continuación.
7. Si el filter devolvió `FILTER_SCHEDULE_THREAD`, el kernel planifica el ithread asociado a esta interrupción. El ithread es un thread del kernel que se despierta, ejecuta el handler secundario del driver y vuelve a dormirse.
8. Tras ejecutarse todos los handlers, el kernel envía una señal End-of-Interrupt (EOI) al controlador de interrupciones, lo que reactiva la línea IRQ.
9. El CPU retorna de la interrupción. El conjunto de registros del thread interrumpido se restaura y el thread reanuda la ejecución en la instrucción que estaba a punto de ejecutarse cuando llegó la interrupción.

Los pasos 3 al 9 tardan unos pocos microsegundos en hardware moderno para un handler simple. Todo el flujo es invisible para el thread interrumpido: el código que no fue diseñado para ser seguro frente a interrupciones (por ejemplo, un cálculo en coma flotante en espacio de usuario) se ejecuta correctamente a ambos lados de la interrupción, porque el CPU guarda y restaura su estado durante toda la secuencia.

Desde la perspectiva del autor de drivers, los pasos 1 al 5 son responsabilidad del kernel; los pasos 6 y 7 son donde se ejecuta el código del driver. El handler del driver debe ser rápido (el EOI del paso 8 espera a que termine), no debe dormir (el thread interrumpido mantiene recursos del CPU) y no debe tomar locks que pudieran bloquearse indirectamente sobre el thread interrumpido. La Sección 2 hará estas restricciones precisas en términos de FreeBSD; la Sección 1 ha establecido ya el modelo mental.

### Qué debe hacer un driver cuando llega una interrupción

Las obligaciones del driver ante una interrupción son pocas en número, pero no en detalle:

1. **Identificar la causa.** Leer el registro de estado de interrupción del dispositivo. Si no hay ningún bit activo (el dispositivo no tiene ninguna interrupción pendiente), se trata de una llamada espuria por IRQ compartido; devolver `FILTER_STRAY` y dejar que el kernel pruebe el siguiente handler en la línea.
2. **Reconocer la interrupción en el dispositivo.** Escribir de nuevo los bits de estado (típicamente escribiendo un 1 en cada bit, ya que la mayoría de los registros INTR_STATUS son RW1C) para que el dispositivo desaserte la línea y el controlador de interrupciones pueda reactivar el IRQ. No todos los dispositivos requieren que el reconocimiento se realice dentro del filter, pero hacerlo aquí es la opción segura por defecto; la historia de la tormenta disparada por nivel depende de un reconocimiento oportuno.
3. **Decidir qué trabajo hay que hacer.** Leer suficiente información del dispositivo para decidir. ¿Fue un evento de recepción? ¿Una finalización de transmisión? ¿Un error? ¿Un cambio de enlace? Los bits de estado lo indican.
4. **Realizar el trabajo urgente y pequeño.** Actualizar un contador. Copiar un byte desde un FIFO hacia una cola. Cambiar un bit de control. Cualquier tarea que pueda realizarse en microsegundos sin tomar un sleep lock es válida aquí.
5. **Diferir el trabajo voluminoso.** Si el evento desencadena una operación larga (procesar un paquete recibido, decodificar un flujo de datos, enviar un comando al espacio de usuario), planificar un ithread o una tarea de taskqueue y retornar. El trabajo diferido se ejecuta en contexto de thread, donde puede tomar sleep locks, asignar memoria y tomarse su tiempo.
6. **Devolver el valor `FILTER_*` apropiado.** `FILTER_HANDLED` significa que la interrupción se ha gestionado por completo; no se necesita ningún ithread. `FILTER_SCHEDULE_THREAD` significa que debe ejecutarse el ithread. `FILTER_STRAY` significa que la interrupción no era para este driver. Estos tres valores son el vocabulario que el kernel usa para despachar el trabajo posterior.

Un driver que hace correctamente estas seis cosas en cada interrupción tiene la forma que enseña el resto del Capítulo 19. Un driver que omite cualquiera de ellas tiene un bug.

### Ejemplos del mundo real

Un breve recorrido por los eventos que cubrirá el vocabulario del Capítulo 19.

**Pulsaciones de teclas.** Un controlador de teclado PS/2 activa una interrupción cuando llega un scancode. El driver lee el scancode, lo pasa al subsistema de teclado y lo reconoce. El handler completo se ejecuta en pocos microsegundos; habitualmente no se necesita taskqueue.

**Paquetes de red.** Una NIC activa una interrupción cuando se acumulan paquetes en la cola de recepción. El filter del driver lee un registro de estado para confirmar los eventos de recepción, planifica el ithread y retorna. El ithread recorre el anillo de descriptores, construye paquetes `mbuf` y los pasa hacia la pila de red. La separación entre filter e ithread es importante aquí porque el procesamiento de la pila es suficientemente lento como para que ejecutarlo en el filter alargaría demasiado la ventana de interrupción.

**Lecturas de sensores.** Un sensor de temperatura conectado por I2C activa una interrupción cuando hay una nueva medición disponible. El driver lee el valor, actualiza una caché de sysctl, opcionalmente despierta a cualquier lector pendiente en espacio de usuario y reconoce. Sencillo y rápido.

**Puerto serie.** Un UART activa una interrupción en condiciones de recepción o de FIFO de transmisión vacío. El driver vacía o rellena el FIFO, actualiza un buffer circular y reconoce. A baudios elevados, esto puede ocurrir decenas de miles de veces por segundo, por lo que el handler debe ser muy ajustado.

**Finalización de disco.** Un controlador SATA o NVMe activa una interrupción cuando se completa un comando encolado. El driver recorre la cola de finalizaciones, empareja cada finalización con una solicitud de I/O pendiente, despierta el thread en espera y reconoce. El emparejamiento y el despertar a veces se dividen entre el filter y el ithread.

Cada uno de estos dispositivos llega al vocabulario del Capítulo 19 del mismo modo: el filter lee el estado, el filter decide qué ha ocurrido, el filter reconoce y el filter gestiona o difiere. El mapa de registros específico varía; el patrón no.

### Un ejercicio rápido: encuentra dispositivos controlados por interrupciones en tu máquina de laboratorio

Antes de pasar a la Sección 2, un breve ejercicio para concretar la imagen del hardware.

En tu máquina de laboratorio, ejecuta:

```sh
vmstat -i
```

La salida es una lista de fuentes de interrupción con sus contadores desde el arranque. Cada línea tiene un aspecto aproximadamente así:

```text
interrupt                          total       rate
cpu0:timer                      1234567        123
cpu1:timer                      1234568        123
irq9: acpi0                          42          0
irq19: uhci0+                     12345         12
irq21: ahci0                      98765         99
irq23: em0                       123456        123
```

Elige tres líneas de tu propia salida. Para cada una, identifica:

- El nombre de la interrupción (una combinación del número de IRQ y la descripción que establece el driver).
- El contador total (cuántas veces ha saltado la interrupción desde el arranque).
- La tasa (interrupciones por segundo; una tasa elevada significa que el dispositivo está ocupado).

Ejecuta `vmstat -i` una segunda vez diez segundos después. Compara los contadores. ¿Qué interrupciones siguen incrementándose activamente? ¿Cuáles están prácticamente inactivas?

Ahora empareja las interrupciones con dispositivos usando `devinfo -v`:

```sh
devinfo -v | grep -B 2 irq
```

Cada coincidencia muestra un dispositivo que reclama un IRQ. Compara con la salida de `vmstat -i` para ver qué driver sirve cada línea.

Mantén esta salida abierta mientras lees la Sección 2. La entrada `em0` del ejemplo es el controlador Ethernet de Intel; si estás usando un sistema basado en Intel con FreeBSD, es probable que `em0`, `igc0` o `ix0` ejecute una versión del mismo patrón que enseña el Capítulo 19. Un NUC moderno con FreeBSD 14.3 muestra una o dos docenas de fuentes de interrupción; un servidor muestra muchas más. El sistema que tienes realmente es más interesante de analizar que cualquier diagrama.

### Una breve historia de las interrupciones

Las interrupciones son una de las ideas más antiguas de la arquitectura de computadores. El PDP-1 original las admitía en 1961 como forma de que los dispositivos de I/O señalizaran al CPU sin necesidad de que este realizara sondeos. El IBM 704 las tenía aproximadamente en la misma época. Los primeros sistemas de tiempo compartido utilizaban interrupciones para el tic de reloj que controlaba la planificación, y para cada finalización de I/O.

A lo largo de los años 1970 y 1980, los ordenadores personales heredaron este patrón. El IBM PC original utilizaba el 8259 Programmable Interrupt Controller (PIC), que admitía ocho líneas IRQ; el PC/AT amplió esto a quince líneas utilizables mediante la cascada de dos PICs. El conjunto de instrucciones x86 añadió instrucciones específicas para el manejo de interrupciones (`CLI`, `STI`, `INT`, `IRET`) que persisten hoy en formas extendidas.

PCI introdujo el concepto de que un dispositivo anuncia su interrupción a través del espacio de configuración (los campos `INTLINE` e `INTPIN` que el capítulo 18 analizó). PCIe añadió MSI y MSI-X, que sustituyen la línea IRQ física por un mensaje de escritura en memoria. Los tres coexisten en los sistemas modernos; el INTx heredado del capítulo 19 es el más antiguo de los tres y el único que comparte líneas.

Los sistemas operativos han evolucionado en paralelo. El Unix primitivo era monolítico y de un único thread en el kernel; las interrupciones adelantaban a lo que estuviera ejecutándose en ese momento. Los kernels modernos (FreeBSD incluido) cuentan con un locking de granularidad fina, estructuras de datos por CPU y despacho diferido basado en ithread. La disciplina de handler que enseña el capítulo 19 es la destilación de esa evolución: rápido en el filtro, lento en la tarea, MP-safe por defecto, compartible y depurable.

Conocer la historia no es un requisito para escribir un driver. Pero el vocabulario (IRQ, PIC, EOI, INTx) proviene de momentos concretos de esa historia, y el autor de un driver que sabe de dónde vienen esas palabras encuentra menos misterio en este campo.

### Cerrando la sección 1

Una interrupción es el mecanismo que permite a un dispositivo interrumpir el trabajo actual de la CPU, ejecutar una pequeña porción de código del driver y dejar que la CPU continúe. El mecanismo pasa por el controlador de interrupciones, el despacho de interrupciones de la CPU, la maquinaria de eventos de interrupción del kernel y, finalmente, el manejador del driver. La señalización por flanco y la señalización por nivel tienen consecuencias operativas distintas, las más visibles en forma de tormentas de interrupciones cuando una línea activada por nivel no se acusa de recibo correctamente.

El manejador de un driver tiene seis obligaciones: identificar la causa, acusar recibo al dispositivo, decidir el trabajo, realizar la parte urgente, diferir la mayor parte y devolver el valor `FILTER_*` correcto. Cada una de ellas es sencilla por sí sola, pero exigente en conjunto; el resto del capítulo 19 trata de realizarlas correctamente en los términos de FreeBSD.

La sección 2 recorre ahora el modelo de interrupciones del kernel de FreeBSD: qué es un `intr_event`, qué es un ithread, cómo `vmstat -i` y `devinfo -v` muestran la visión del kernel sobre las interrupciones y qué restricciones impone el modelo a los manejadores de drivers.



## Sección 2: Interrupciones en FreeBSD

La sección 1 estableció el modelo hardware. La sección 2 presenta el modelo software. La maquinaria de interrupciones del kernel es la capa que se sitúa entre el controlador de interrupciones y el manejador del driver; entenderla con claridad es lo que convierte el modelo hardware en algo que un driver puede implementar. Un lector que termine la sección 2 debería ser capaz de responder a tres preguntas con palabras sencillas: qué se ejecuta cuando se dispara una interrupción, qué se ejecuta después como trabajo diferido y qué tiene que garantizar el driver para que ambas cosas ocurran de forma segura.

### El modelo de interrupciones de FreeBSD de un vistazo

El manejador de un driver no se ejecuta de forma aislada. Se ejecuta dentro de un pequeño ecosistema de objetos del kernel que en conjunto hacen que el manejo de interrupciones sea ordenado y depurable. Ese ecosistema consta de tres elementos que conviene nombrar desde el principio.

El primero es el **evento de interrupción**, representado por `struct intr_event` en `/usr/src/sys/sys/interrupt.h` (con el código de despacho en `/usr/src/sys/kern/kern_intr.c`). Existe un `intr_event` por cada línea IRQ (o por cada vector MSI, en el mundo del capítulo 20). Es el coordinador central: contiene una lista de manejadores (las funciones filter y las funciones ithread del driver), un nombre legible para humanos, flags, un contador de bucle usado para detectar tormentas (`ie_count`), un limitador de velocidad para los mensajes de aviso (`ie_warntm`) y un binding de CPU. Cuando el controlador de interrupciones comunica una IRQ al kernel, este busca el `intr_event` correspondiente y recorre su lista de manejadores. Las interrupciones extraviadas se contabilizan de forma global, no por evento; aparecen a través de la contabilidad separada de `vmstat -i` y de los mensajes del log del kernel, no a través de un campo del evento.

El segundo es el **manejador de interrupción**, representado por `struct intr_handler`. Existe un `intr_handler` por cada manejador registrado en un `intr_event`. Una sola línea IRQ puede tener varios manejadores (uno por cada driver que comparte la línea). El manejador lleva la función filter proporcionada por el driver (si la hay), la función ithread proporcionada por el driver (si la hay), los flags `INTR_*` (el más importante, `INTR_MPSAFE`) y un puntero cookie que el kernel guarda como referencia para el driver.

El tercero es el **thread de interrupción**, habitualmente llamado **ithread**, representado por `struct intr_thread`. A diferencia del evento y del manejador, el ithread es un thread real del kernel con su propia pila, su propia prioridad de planificación y su propia estructura `proc`. Cuando un filter devuelve `FILTER_SCHEDULE_THREAD` (o cuando un driver ha registrado un manejador solo con ithread y sin filter), el ithread se programa para ejecutarse. El ithread llama entonces a la función manejadora del driver en contexto de thread, donde se permiten los sleep mutexes ordinarios y el bloqueo.

Los tres juntos producen el patrón clásico de interrupciones en dos fases que FreeBSD ha usado durante más de una década: un filter rápido se ejecuta en el contexto de interrupción primario para realizar el trabajo urgente, y un manejador en contexto de thread se ejecuta después para realizar el trabajo lento. El driver del capítulo 19 usará ambas fases.

### Asignación y enrutamiento de IRQ

Un sistema x86 moderno tiene más de un controlador de interrupciones. El PIC 8259 heredado ha sido sustituido por el Local APIC (uno por CPU, que gestiona interrupciones por CPU como el temporizador local) y el I/O APIC (una unidad compartida que recibe IRQs del bus de I/O y las enruta a las CPU). En arm64, el equivalente es el Generic Interrupt Controller (GIC), con redistribuidores por CPU y un distribuidor compartido. En plataformas embebidas existen otros controladores. FreeBSD abstrae todos ellos detrás de la interfaz `intr_pic(9)`; un autor de drivers rara vez interactúa directamente con el controlador de interrupciones.

Lo que el driver ve es un número de IRQ (en las rutas PCI heredadas, el número que una BIOS asignó en el espacio de configuración) o un índice de vector (en las rutas MSI y MSI-X). El driver solicita un recurso IRQ por ese número, el kernel asigna un `struct resource *`, y el driver entrega el recurso a `bus_setup_intr(9)` para adjuntar un manejador. El kernel realiza el trabajo de conectar el manejador al `intr_event` correcto, configurar el controlador de interrupciones para enrutar la IRQ a una CPU y armar la línea.

Desde la perspectiva de un autor de drivers, el enrutamiento de IRQ es habitualmente una caja negra. El kernel se encarga de ello; el driver ve un handle y un manejador. Hay una excepción: en plataformas con muchas CPU y varios dispositivos, la **afinidad** importa. Una interrupción que se dispara en una CPU alejada del dispositivo produce fallos de cache y tráfico entre sockets; una interrupción que se dispara en una CPU cercana al dispositivo resulta más económica. `bus_bind_intr(9)` permite al driver solicitar una CPU concreta; los operadores usan `cpuset -x <irq> -l <cpu>` para cambiar la afinidad en tiempo de ejecución, y `cpuset -g -x <irq>` para consultarla. El apéndice *A Deeper Look at CPU Affinity for Interrupts* que aparece más adelante en el capítulo cubre ambas rutas con más detalle.

### SYS_RES_IRQ: el recurso de interrupción

El capítulo 18 presentó tres tipos de recurso: `SYS_RES_MEMORY` para los BARs mapeados en memoria, `SYS_RES_IOPORT` para los BARs de puertos de I/O y (mencionado de pasada) `SYS_RES_IRQ` para las interrupciones. La sección 3 usará el tercero por primera vez. El vocabulario es el mismo que para los BARs:

```c
int rid = 0;                  /* legacy PCI INTx */
struct resource *irq_res;

irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
    RF_SHAREABLE | RF_ACTIVE);
```

Hay tres aspectos que merece la pena destacar sobre esta asignación.

Primero, **`rid = 0`** es la convención para una línea INTx de PCI heredado. El driver del bus PCI trata el recurso IRQ de índice cero como la interrupción heredada del dispositivo, configurada a partir del campo `PCIR_INTLINE` del espacio de configuración. Para MSI y MSI-X (capítulo 20), el rid es 1, 2, 3, y así sucesivamente, correspondiendo a los vectores asignados.

Segundo, **`RF_SHAREABLE`** pide al kernel que permita compartir la línea IRQ con otros drivers. En PCI heredado es el caso habitual: una sola línea física puede dar servicio a varios dispositivos. Sin `RF_SHAREABLE`, la asignación falla si otro driver ya tiene un manejador en la misma línea. Pasar `RF_SHAREABLE` no significa que tu driver deba gestionar las interrupciones extraviadas; significa que debe tolerarlas. La sección 6 trata precisamente de esa tolerancia.

Tercero, **`RF_ACTIVE`** activa el recurso en un solo paso, igual que con la asignación de BARs. Sin él, el driver tendría que llamar a `bus_activate_resource(9)` por separado. El capítulo 19 siempre usa `RF_ACTIVE`.

Si la asignación tiene éxito, el `struct resource *` devuelto es un handle a la IRQ. No es el número de IRQ; el kernel no lo expone directamente. El driver pasa el handle a `bus_setup_intr(9)`, a `bus_teardown_intr(9)` y a `bus_release_resource(9)`.

### Manejadores filter frente a manejadores ithread

Este es el núcleo conceptual de la sección 2 y del capítulo. Un lector que interiorice la distinción filter-frente-a-ithread leerá el código de interrupciones de cualquier driver de FreeBSD con plena comprensión.

Un **manejador filter** es una función C registrada por el driver que se ejecuta en el contexto de interrupción primario. El contexto de interrupción primario significa: la CPU ha saltado directamente desde lo que estuviera haciendo, se ha guardado un estado mínimo y el filter se está ejecutando con el contexto del thread interrumpido todavía parcialmente en vigor. En concreto:

- El filter no puede dormir. No hay ningún thread que bloquear; el kernel está en medio del despacho de una interrupción.
- El filter no puede adquirir un sleep mutex (`mtx(9)` es por defecto un mutex adaptativo con comportamiento de sleep, que puede girar brevemente pero acabará durmiendo). Los spin mutexes (`mtx_init` con `MTX_SPIN`) son seguros.
- El filter no puede asignar memoria con `M_WAITOK`; puede usar `M_NOWAIT`, que puede fallar.
- El filter no puede llamar a código que use ninguno de los anteriores.

Se supone que el filter debe ser rápido (microsegundos), realizar el trabajo urgente (leer el estado, acusar recibo, actualizar un contador) y retornar. La convención de valores de retorno del kernel es:

- `FILTER_HANDLED`: la interrupción es mía, está totalmente gestionada, no se necesita ithread.
- `FILTER_SCHEDULE_THREAD`: la interrupción es mía, parte del trabajo está hecho, programa el ithread para el resto.
- `FILTER_STRAY`: la interrupción no es mía; prueba el siguiente manejador en esta línea.

Un driver puede también especificar `FILTER_HANDLED | FILTER_SCHEDULE_THREAD` para indicar a la vez «he gestionado parte» y «programa el thread para el resto».

Un **manejador ithread** es una función C distinta que se ejecuta en contexto de thread. El thread es planificado por el kernel cuando un filter devuelve `FILTER_SCHEDULE_THREAD`, o si el manejador está registrado solo como ithread (sin filter), el ithread se planifica automáticamente cuando el kernel despacha la interrupción.

En contexto de ithread, las restricciones se relajan considerablemente:

- El ithread puede dormir brevemente en un mutex o en una variable de condición.
- El ithread puede usar `malloc(M_WAITOK)`.
- El ithread puede llamar a la mayoría de las API del kernel que usen locks con posibilidad de sleep.
- El ithread no puede dormir un tiempo arbitrariamente largo (es un thread con características de tiempo real), pero puede realizar el trabajo normal de un driver.

Esta división permite a un driver separar el trabajo urgente y breve (en el filter) del trabajo más lento y voluminoso (en el ithread). El filter de un driver de red puede leer el registro de estado y acusar recibo; su ithread recorre el anillo de descriptores de recepción y pasa los paquetes hacia la pila. El filter de un driver de disco puede registrar qué completaciones han ocurrido y acusar recibo; su ithread relaciona las completaciones con las solicitudes pendientes y despierta los threads en espera.

### Cuándo usar únicamente el filter

Un driver usa únicamente el filter cuando todo el trabajo que realiza en una interrupción puede ejecutarse en el contexto de interrupción primario. Ejemplos:

- **Un driver de prueba mínimo** que incrementa un contador en cada interrupción y nada más.
- **Un driver sencillo de sensor** que lee un registro, almacena el valor en cache y despierta a un lector de sysctl. (Si `selwakeup` o un broadcast de variable de condición requiere un sleep mutex, esto pasa a filter más ithread.)
- **Un driver de temporizador** cuyo trabajo consiste en actualizar algún contador interno del kernel.

El driver del capítulo 19 en la etapa 1 usa únicamente el filter: lee INTR_STATUS, acusa recibo, actualiza un contador y devuelve `FILTER_HANDLED`. Es suficiente para demostrar que el manejador está correctamente conectado.

### Cuándo usar filter más ithread

Un driver usa filter más ithread cuando la interrupción requiere un trabajo urgente y breve seguido de un trabajo más lento y voluminoso. Ejemplos:

- **Un driver de NIC.** El filter acusa recibo y marca qué colas tienen eventos. El ithread recorre los anillos de descriptores, construye mbufs y sube los paquetes a la pila.
- **Un controlador de disco.** El filter lee el estado de completación y acusa recibo. El ithread relaciona las completaciones con las solicitudes de I/O y despierta a los threads en espera.
- **Un controlador host USB.** El filter lee el estado y acusa recibo. El ithread recorre la lista de descriptores de transferencia y completa los URBs pendientes.

El driver del capítulo 19 en la etapa 2 pasa a filter más ithread cuando se añaden eventos simulados de «trabajo solicitado»; el filter registra el evento, y un trabajador diferido (a través de un taskqueue; la primitiva del capítulo 14) gestiona el trabajo.

### Cuándo usar únicamente el ithread

Un driver usa únicamente el ithread cuando todo el trabajo debe ejecutarse en contexto de thread. Es el caso menos habitual; la razón más frecuente es que el driver necesita adquirir un sleep mutex en cada interrupción y no puede hacer nada útil en el contexto primario.

Registrar un handler exclusivo de ithread es sencillo: pasa `NULL` como argumento filter de `bus_setup_intr(9)`. El kernel planifica el ithread cada vez que se produce la interrupción.

El driver del capítulo 19 no utiliza el modo exclusivo de ithread; el filter siempre es rápido de ejecutar.

### INTR_MPSAFE: qué promete este flag

`INTR_MPSAFE` es un bit en el argumento de flags de `bus_setup_intr(9)`. Activarlo le promete al kernel dos cosas:

1. Tu handler se encarga de su propia sincronización. El kernel no adquirirá el lock Giant a su alrededor.
2. Tu handler es seguro para ejecutarse de forma concurrente en múltiples CPUs (para un handler compartido por varios CPUs, lo que ocurre en escenarios MSI-X y en algunas configuraciones de PIC).

Si **no** activas `INTR_MPSAFE`, el kernel adquiere Giant antes de llamar a tu handler. Este es el comportamiento predeterminado clásico de BSD, conservado por compatibilidad con drivers anteriores a SMP que dependían de la protección implícita de Giant. Los drivers modernos siempre activan `INTR_MPSAFE`.

No activar `INTR_MPSAFE` tiene un síntoma visible: el banner de `dmesg` al ejecutar `kldload` incluye una línea como `myfirst0: [GIANT-LOCKED]`. Esto es el kernel informándote de que Giant se está adquiriendo alrededor de tu handler. En sistemas en producción, serializa todas las interrupciones a través de un único lock, lo que es desastroso para la escalabilidad. La línea es un aviso deliberado de `bus_setup_intr` para que lo notes.

Activar `INTR_MPSAFE` cuando en realidad todavía dependes de Giant también es un error, aunque más silencioso. El kernel no adquirirá Giant, por lo que cualquier ruta de código que antes estuviera serializada por Giant dejará de estarlo. Aparecen condiciones de carrera donde antes no las había. La solución no es eliminar `INTR_MPSAFE` (eso enmascara el error); la solución es añadir el locking correcto al handler y al código que toca.

El driver del Capítulo 19 siempre activa `INTR_MPSAFE` y se apoya en el `sc->mtx` existente para la sincronización. La disciplina del Capítulo 11 se mantiene.

### Flags INTR_TYPE_*

Además de `INTR_MPSAFE`, `bus_setup_intr` acepta un flag de categoría que indica la clase de la interrupción:

- `INTR_TYPE_TTY`: dispositivos tty y serie.
- `INTR_TYPE_BIO`: I/O de bloque (disco, CD-ROM).
- `INTR_TYPE_NET`: red.
- `INTR_TYPE_CAM`: SCSI (framework CAM).
- `INTR_TYPE_MISC`: miscelánea.
- `INTR_TYPE_CLK`: interrupciones de reloj y temporizador.
- `INTR_TYPE_AV`: audio y vídeo.

La categoría influye en la prioridad de planificación del ithread. Históricamente, cada categoría tenía una prioridad distinta; en el FreeBSD moderno, solo `INTR_TYPE_CLK` obtiene una prioridad elevada, y el resto son aproximadamente iguales. Aun así, merece la pena establecer la categoría correcta porque aparece en la salida de `devinfo -v` y `vmstat -i`, haciendo que la interrupción se identifique por sí misma.

Para el driver del Capítulo 19, `INTR_TYPE_MISC` es apropiado porque el objetivo de demostración no encaja en ninguna de las categorías más específicas. El Capítulo 20 usará `INTR_TYPE_NET` cuando el driver empiece a trabajar con NICs en los laboratorios.

### Interrupciones compartidas frente a exclusivas

En el PCI clásico, varios dispositivos pueden compartir una única línea INTx. El kernel gestiona esto con dos flags de recurso:

- `RF_SHAREABLE`: este driver está dispuesto a compartir la línea con otros drivers.
- `RF_SHAREABLE` ausente: este driver quiere la línea para sí solo; la asignación falla si otro driver ya la tiene.

Un driver que desea interrupciones compartibles usa `RF_SHAREABLE | RF_ACTIVE` en su llamada a `bus_alloc_resource_any`. Un driver que quiere acceso exclusivo (quizás por razones de latencia) usa `RF_ACTIVE` solo, pero la solicitud puede fallar en sistemas con muchos dispositivos.

El kernel nunca impide que un driver comparta; lo que impide es que otro driver se una si este solicita exclusividad. En el PCIe moderno con MSI-X, el uso compartido es mucho menos frecuente porque cada dispositivo tiene su propio vector de señalización por mensaje.

El driver del Capítulo 19 activa `RF_SHAREABLE` porque virtio-rnd en bhyve puede o no compartir su línea con otros dispositivos emulados por bhyve, dependiendo de la topología de slots. Ser compartible es el valor predeterminado seguro.

El flag `INTR_EXCL` pasado mediante el campo de flags de `bus_setup_intr` (que no debe confundirse con el flag de asignación de recursos) es un concepto relacionado pero distinto: solicita al bus que otorgue al handler acceso exclusivo a nivel del interrupt-event. Los drivers PCI clásicos raramente lo necesitan. Algunos drivers de bus lo utilizan internamente. Para el driver del Capítulo 19, no activamos `INTR_EXCL`.

### Qué muestra vmstat -i

`vmstat -i` imprime los contadores de interrupciones del kernel. Cada línea corresponde a un `intr_event`. Las columnas son:

- **interrupt**: un identificador legible por humanos. Para las interrupciones hardware, el nombre se deriva del número de IRQ y la descripción del driver. Los nombres con el estilo de `devinfo -v` (como `em0:rx 0`) aparecen cuando se usan vectores MSI-X.
- **total**: el número de veces que esta interrupción ha disparado desde el arranque.
- **rate**: la tasa de interrupciones por segundo, promediada sobre una ventana reciente.

Algunas notas de interpretación. Una columna `total` que crece rápidamente para un dispositivo inactivo es una señal de alarma (tormenta de interrupciones). Una columna `rate` a cero para un dispositivo que debería estar gestionando tráfico sugiere que el handler no está conectado correctamente. Cuando varios dispositivos comparten una línea INTx clásica, `vmstat -i` muestra una línea por `intr_event` (por fuente de IRQ), y el nombre del driver en esa línea es la descripción del primer handler registrado; los demás drivers que comparten la línea no obtienen sus propias líneas. Cuando un dispositivo tiene sus propios vectores MSI o MSI-X, cada vector es su propio `intr_event` y obtiene su propia línea. Las interrupciones por CPU, como el temporizador local, aparecen como líneas distintas por CPU (`cpu0:timer`, `cpu1:timer`) porque el kernel crea un evento por CPU para ellas.

El kernel expone los mismos contadores a través de `sysctl hw.intrcnt` y `sysctl hw.intrnames`, que son los datos en bruto que `vmstat -i` formatea. Un autor de drivers raramente los lee directamente; `vmstat -i` es la vista amigable.

### Qué muestra devinfo -v sobre las interrupciones

`devinfo -v` recorre el árbol newbus e imprime cada dispositivo con sus recursos. Para un driver PCI con una interrupción, la lista de recursos incluye una entrada `irq:` junto a la entrada `memory:`:

```text
myfirst0
    pnpinfo vendor=0x1af4 device=0x1005 ...
    resources:
        memory: 0xc1000000-0xc100001f
        irq: 19
```

El número que sigue a `irq:` es el identificador de IRQ del kernel. En x86 suele ser un número de pin del I/O APIC; en arm64 es un vector GIC; el significado exacto depende de la plataforma, pero el número es estable entre reinicios del mismo sistema.

Relacionar `irq: 19` con la entrada `irq19: ` de `vmstat -i` confirma que el driver está conectado a la línea de interrupción esperada.

Para las interrupciones MSI-X (Capítulo 20), cada vector tiene su propia entrada `irq:`, y `devinfo -v` las lista individualmente.

### Un diagrama simple de la ruta de una interrupción

Reuniendo todo lo anterior, esto es lo que ocurre desde el dispositivo hasta el driver:

```text
  Device        IRQ line          Interrupt         CPU        intr_event         Handler
 --------     -----------       -controller-      --------   --------------      ---------
   |              |                  |                |             |                 |
   | asserts     |                  |                |             |                 |
   | IRQ line    | signal           |                |             |                 |
   |------------>|                  |                |             |                 |
   |             | latch            |                |             |                 |
   |             |----------------->|                |             |                 |
   |             |                  | steer to CPU   |             |                 |
   |             |                  |--------------->|             |                 |
   |             |                  |                | save state  |                 |
   |             |                  |                | jump vector |                 |
   |             |                  |                |             | look up         |
   |             |                  |                |------------>|                 |
   |             |                  |                |             | for each        |
   |             |                  |                |             | handler         |
   |             |                  |                |             |---------------->|
   |             |                  |                |             |                 | filter runs
   |             |                  |                |             |<----------------|
   |             |                  |                |             | FILTER_HANDLED  |
   |             |                  |                |             | or              |
   |             |                  |                |             | FILTER_SCHEDULE |
   |             |                  |                | EOI         |                 |
   |             |                  |<---------------|             |                 |
   |             |                  |                | restore     |                 |
   |             |                  |                | state       |                 |
   |             |                  |                | resume thread                 |
   |             |                  |                |             | ithread wakeup  |
   |             |                  |                |             | (if scheduled)  |
   |             |                  |                |             |                 | ithread runs
   |             |                  |                |             |                 | slower work
```

El diagrama omite varios detalles (la coalescencia de interrupciones, el intercambio de la pila del thread interrumpido por la pila del ithread, la planificación propia del ithread), pero captura la forma general. Un filter se ejecuta en contexto de interrupción, un ithread (si se planifica) se ejecuta después en contexto de thread, el EOI ocurre después de que los filters han terminado, y el thread interrumpido se reanuda cuando la CPU queda libre.

### Restricciones sobre lo que los handlers pueden hacer

A continuación, una lista consolidada de lo que un filter handler puede y no puede hacer. Es la lista más consultada del Capítulo 19; conviene tenerla a mano para revisitarla.

**Los filter handlers PUEDEN:**

- Leer y escribir registros del dispositivo a través de la capa de acceso.
- Adquirir spin mutexes (`struct mtx` inicializado con `MTX_SPIN`).
- Leer campos del softc protegidos únicamente por spin locks.
- Llamar a las operaciones atómicas del kernel (`atomic_add_int`, etc.).
- Llamar a `taskqueue_enqueue(9)` para planificar trabajo en contexto de thread.
- Llamar a `wakeup_one(9)` para despertar un thread dormido en un canal, si el contexto lo permite (la mayoría lo permiten).
- Devolver `FILTER_HANDLED`, `FILTER_SCHEDULE_THREAD`, `FILTER_STRAY`, o combinaciones de estos.

**Los filter handlers NO PUEDEN:**

- Adquirir un sleep mutex (`struct mtx` inicializado con la configuración predeterminada, `struct sx`, `struct rwlock`).
- Llamar a ninguna función que pueda dormir: `malloc(M_WAITOK)`, `tsleep`, `pause`, `cv_wait`, etc.
- Adquirir Giant.
- Llamar a código que pueda hacer indirectamente cualquiera de lo anterior.
- Tardar demasiado (los microsegundos están bien; los milisegundos son un error).

**Los ithread handlers PUEDEN:**

- Todo lo que puede hacer un filter handler, más:
- Adquirir sleep mutexes, sx locks, rwlocks.
- Llamar a `malloc(M_WAITOK)`.
- Llamar a `cv_wait`, `tsleep`, `pause`, y otras primitivas bloqueantes.
- Tardar más tiempo (decenas o cientos de microsegundos son normales).
- Realizar trabajo acotado con tiempo de finalización impredecible.

**Los ithread handlers NO DEBERÍAN:**

- Dormir durante tiempos arbitrariamente largos. El ithread tiene una prioridad de planificación que asume capacidad de respuesta; un handler que duerme durante segundos priva de recursos al resto del trabajo en el mismo ithread.
- Bloquear el ithread esperando eventos externos sin límite.

El filter del Capítulo 19 respeta la primera lista de forma rigurosa; cualquier violación es un error que el kernel de depuración suele detectar.

### Una nota sobre los ithreads por CPU frente a los compartidos

Para las líneas INTx del PCI clásico, el kernel generalmente asigna un ithread por `intr_event`, compartido entre todos los handlers de ese evento. Para MSI-X (Capítulo 20), cada vector tiene su propio ithread. La diferencia importa cuando varios handlers necesitan ejecutarse de forma concurrente en el mismo IRQ: en el ithread compartido se serializan; en vectores MSI-X separados pueden ejecutarse en paralelo.

El driver del Capítulo 19 usa PCI clásico. Un IRQ, un ithread (si es que hay algún ithread), una cola de trabajo diferido. La serialización es generalmente lo que se desea en un driver de un único dispositivo.

### Cerrando la Sección 2

El modelo de interrupciones de FreeBSD se centra en tres objetos: el `intr_event` (uno por línea de IRQ o vector MSI), el `intr_handler` (uno por driver registrado en ese evento), y el ithread (uno por evento, compartido entre los handlers). Un driver registra una función filter, una función de ithread, o ambas, a través de `bus_setup_intr(9)`, y promete el cumplimiento de `INTR_MPSAFE` mediante el argumento de flags. El kernel despacha los filter handlers en el contexto primario de interrupción y planifica los ithreads cuando el filter devuelve el control.

Las restricciones sobre los filter handlers son estrictas (sin dormir, sin sleep locks, sin llamadas lentas); las restricciones sobre los ithread handlers son laxas en comparación. Las líneas INTx PCI compartidas permiten que muchos drivers compartan un IRQ, por lo que los filters deben identificar si la interrupción pertenece a su dispositivo y devolver `FILTER_STRAY` cuando no es así. `vmstat -i` y `devinfo -v` exponen la visión del kernel para que los operadores y autores de drivers puedan ver lo que está ocurriendo.

La Sección 3 es donde el driver por fin escribe código contra este modelo. Asigna un recurso IRQ con `SYS_RES_IRQ`, registra un filter handler a través de `bus_setup_intr(9)`, activa `INTR_MPSAFE`, y registra un breve mensaje en cada llamada. La Etapa 1 es la primera vez que el driver recibe una interrupción de verdad.



## Sección 3: registrar un handler de interrupción

Las Secciones 1 y 2 establecieron los modelos hardware y del kernel. La Sección 3 pone el driver a trabajar. La tarea es concreta: extender la ruta de attach del Capítulo 18 para que, tras asignar el BAR y recorrer la lista de capacidades, también asigne un recurso IRQ, registre un filter handler y active `INTR_MPSAFE`. La ruta de detach crece en sentido inverso: primero se desmonta el handler y luego se libera el IRQ. Al final de la Sección 3, el driver está en la versión `1.2-intr-stage1` y dispara un pequeño filter handler que incrementa un contador cada vez que su línea de IRQ se activa.

### Qué produce la Etapa 1

El handler de la Etapa 1 es deliberadamente mínimo. El driver necesita un filter que sea correcto en todos los aspectos formales (devuelve el valor `FILTER_*` correcto, respeta la regla de "no dormir", es `INTR_MPSAFE`) pero que todavía no realice trabajo real. El objetivo es demostrar que el handler está conectado correctamente antes de introducir las complicaciones de la decodificación de estado y el trabajo diferido.

El comportamiento del handler en la Etapa 1:

1. Adquirir un lock de contador seguro para spin (en nuestro caso, una simple operación atómica).
2. Incrementar un contador en el softc.
3. Devolver `FILTER_HANDLED`.

Eso es todo. Sin lectura de registro de estado, sin acuse de recibo, sin trabajo diferido. El contador permite al lector observar si el handler se dispara y, en ese caso, con qué frecuencia. La salida de `dmesg` es silenciosa por defecto; el recuento es visible a través de un sysctl que la etapa expone.

La sección 4 añade el trabajo real (decodificación de estado, reconocimiento e ithread scheduling). La sección 5 añade interrupciones simuladas para las pruebas. La sección 6 extiende el filtro para que sea seguro con IRQ compartido, comprobando si la interrupción es realmente para nuestro dispositivo. Pero el andamiaje es la contribución de esta etapa, y debe estar bien construido antes de que cualquier otra cosa se añada encima.

### La asignación del recurso IRQ

La primera línea nueva de attach, justo tras la asignación del BAR:

```c
sc->irq_rid = 0;   /* legacy PCI INTx */
sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid,
    RF_SHAREABLE | RF_ACTIVE);
if (sc->irq_res == NULL) {
	device_printf(dev, "cannot allocate IRQ\n");
	error = ENXIO;
	goto fail_hw;
}
```

Hay algunas cosas que merece la pena observar.

Primero, `rid = 0` es la convención PCI INTx heredada. Cada dispositivo PCI tiene una única línea IRQ heredada anunciada a través de los campos `PCIR_INTLINE` y `PCIR_INTPIN` del espacio de configuración; el driver del bus PCI expone esto como el recurso rid 0. El Capítulo 20 utilizará rids distintos de cero para vectores MSI y MSI-X, pero el driver del Capítulo 19 usa la ruta heredada.

Segundo, la variable `rid` es actualizada por `bus_alloc_resource_any` si el kernel eligió un rid diferente al solicitado. Para `rid = 0` el kernel siempre devuelve `rid = 0`, así que la actualización no tiene efecto, pero el patrón es coherente con la asignación del BAR del Capítulo 18.

Tercero, `RF_SHAREABLE | RF_ACTIVE` es el conjunto de banderas estándar. `RF_SHAREABLE` permite al kernel colocar nuestro manejador en un `intr_event` compartido con otros drivers. `RF_ACTIVE` activa el recurso en un solo paso.

Cuarto, la asignación puede fallar. La razón más habitual en un sistema real es que los campos de interrupción del espacio de configuración PCI del dispositivo son cero (el firmware no enrutó ninguna interrupción hacia el dispositivo). En bhyve con un dispositivo virtio-rnd, la asignación normalmente tiene éxito; en algunas configuraciones antiguas de QEMU con `intx=off`, puede fallar. Si la asignación falla, la ruta de attach se deshace a través de la cascada goto.

### Almacenamiento del recurso en el softc

El softc gana tres nuevos campos:

```c
struct myfirst_softc {
	/* ... existing fields ... */

	/* Chapter 19 interrupt fields. */
	struct resource	*irq_res;
	int		 irq_rid;
	void		*intr_cookie;     /* for bus_teardown_intr */
	uint64_t	 intr_count;      /* handler invocation count */
};
```

`irq_res` es el identificador del recurso IRQ reclamado. `irq_rid` es el ID del recurso (para la llamada de liberación correspondiente). `intr_cookie` es la cookie opaca que `bus_setup_intr(9)` devuelve y que `bus_teardown_intr(9)` consume; identifica el manejador específico para que el kernel pueda retirarlo de forma limpia más adelante. `intr_count` es un contador de diagnóstico que el manejador de la Etapa 1 incrementa en cada llamada.

Los tres campos son paralelos a los tres campos del BAR (`bar_res`, `bar_rid`, `pci_attached`) añadidos en el Capítulo 18. El paralelismo no es accidental: cada clase de recurso obtiene un identificador, un ID y la contabilidad que el driver necesite.

### La firma del manejador de filtro

El filtro del driver es una función con la siguiente firma:

```c
static int myfirst_intr_filter(void *arg);
```

El argumento es el puntero que el driver pasa al parámetro `arg` de `bus_setup_intr`; por convención, un puntero al softc del driver. El valor de retorno es una OR a nivel de bits de `FILTER_STRAY`, `FILTER_HANDLED` y `FILTER_SCHEDULE_THREAD`, tal como describió la Sección 2.

La implementación de la Etapa 1:

```c
static int
myfirst_intr_filter(void *arg)
{
	struct myfirst_softc *sc = arg;

	atomic_add_64(&sc->intr_count, 1);
	return (FILTER_HANDLED);
}
```

Una sola línea de trabajo real. El contador se incrementa de forma atómica porque el manejador puede ejecutarse concurrentemente en múltiples CPUs (en escenarios MSI-X) o en paralelo con código que no sea de interrupción y que lea el contador a través del sysctl. Un sleep lock estaría fuera de lugar en un filtro; una operación atómica es el primitivo ligero que resulta seguro en este contexto.

El valor de retorno es `FILTER_HANDLED` porque no tenemos trabajo para el ithread y no hay razón para devolver `FILTER_STRAY` (la Sección 6 añade la comprobación de stray; la Etapa 1 asume que la IRQ es nuestra).

### Registro del manejador con bus_setup_intr

Tras la asignación de la IRQ, el driver llama a `bus_setup_intr(9)`:

```c
error = bus_setup_intr(dev, sc->irq_res,
    INTR_TYPE_MISC | INTR_MPSAFE,
    myfirst_intr_filter, NULL, sc,
    &sc->intr_cookie);
if (error != 0) {
	device_printf(dev, "bus_setup_intr failed (%d)\n", error);
	goto fail_release_irq;
}
```

Los siete argumentos:

1. **`dev`**: el identificador del dispositivo.
2. **`sc->irq_res`**: el recurso IRQ que acabamos de asignar.
3. **`INTR_TYPE_MISC | INTR_MPSAFE`**: las banderas. `INTR_TYPE_MISC` categoriza la interrupción (Sección 2). `INTR_MPSAFE` promete que el manejador gestiona su propia sincronización.
4. **`myfirst_intr_filter`**: nuestro manejador de filtro. Distinto de NULL.
5. **`NULL`**: el manejador del ithread. NULL porque la Etapa 1 es solo de filtro.
6. **`sc`**: el argumento que se pasa a ambos manejadores.
7. **`&sc->intr_cookie`**: el parámetro de salida donde el kernel almacena la cookie para la posterior desregistración.

El valor de retorno es 0 en caso de éxito, o un errno en caso de error. Un fallo en este punto es poco habitual; la causa más frecuente es una restricción del controlador de interrupciones o de la plataforma.

Una llamada exitosa a `bus_setup_intr` combinada con el `device_printf` que le sigue produce un breve mensaje en `dmesg` cuando se carga el driver:

```text
myfirst0: attached filter handler on IRQ resource
```

El número de IRQ en sí no aparece en esta línea; `devinfo -v` y `vmstat -i` lo muestran (el número de IRQ depende de la configuración del invitado). Si ves una línea adicional `myfirst0: [GIANT-LOCKED]`, significa que el argumento de banderas no incluye `INTR_MPSAFE` y el kernel está avisando de que Giant se está adquiriendo alrededor del manejador; corrígelo.

### Descripción del manejador para devinfo

Un paso opcional pero recomendable. `bus_describe_intr(9)` permite al driver adjuntar un nombre legible por humanos al manejador, que `devinfo -v` y los diagnósticos del kernel utilizarán:

```c
bus_describe_intr(dev, sc->irq_res, sc->intr_cookie, "legacy");
```

Tras esta llamada, `vmstat -i` muestra la línea del manejador como `irq19: myfirst0:legacy` en lugar del simple `irq19: myfirst0`. El sufijo es el nombre que proporcionó el driver. Para el driver de interrupción única del Capítulo 19, el sufijo es principalmente decorativo; para el driver MSI-X del Capítulo 20 con múltiples vectores, resulta esencial para distinguir `rx0`, `rx1`, `tx0`, `admin`, etc.

### La cascada attach extendida

Integrando las nuevas piezas en el attach de la Etapa 3 del Capítulo 18:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error, capreg;

	sc->dev = dev;
	sc->unit = device_get_unit(dev);
	error = myfirst_init_softc(sc);
	if (error != 0)
		return (error);

	/* Step 1: allocate BAR0. */
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		error = ENXIO;
		goto fail_softc;
	}

	/* Step 2: walk PCI capabilities (informational). */
	if (pci_find_cap(dev, PCIY_EXPRESS, &capreg) == 0)
		device_printf(dev, "PCIe capability at 0x%x\n", capreg);
	if (pci_find_cap(dev, PCIY_MSI, &capreg) == 0)
		device_printf(dev, "MSI capability at 0x%x\n", capreg);
	if (pci_find_cap(dev, PCIY_MSIX, &capreg) == 0)
		device_printf(dev, "MSI-X capability at 0x%x\n", capreg);

	/* Step 3: attach the hardware layer against the BAR. */
	error = myfirst_hw_attach_pci(sc, sc->bar_res,
	    rman_get_size(sc->bar_res));
	if (error != 0)
		goto fail_release_bar;

	/* Step 4: allocate the IRQ. */
	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->irq_rid, RF_SHAREABLE | RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "cannot allocate IRQ\n");
		error = ENXIO;
		goto fail_hw;
	}

	/* Step 5: register the filter handler. */
	error = bus_setup_intr(dev, sc->irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    myfirst_intr_filter, NULL, sc,
	    &sc->intr_cookie);
	if (error != 0) {
		device_printf(dev, "bus_setup_intr failed (%d)\n", error);
		goto fail_release_irq;
	}
	bus_describe_intr(dev, sc->irq_res, sc->intr_cookie, "legacy");
	device_printf(dev, "attached filter handler on IRQ resource\n");

	/* Step 6: create the cdev. */
	sc->cdev = make_dev(&myfirst_cdevsw, sc->unit, UID_ROOT,
	    GID_WHEEL, 0600, "myfirst%d", sc->unit);
	if (sc->cdev == NULL) {
		error = ENXIO;
		goto fail_teardown_intr;
	}
	sc->cdev->si_drv1 = sc;

	/* Step 7: read a diagnostic word from the BAR. */
	MYFIRST_LOCK(sc);
	sc->bar_first_word = CSR_READ_4(sc, 0x00);
	MYFIRST_UNLOCK(sc);
	device_printf(dev, "BAR[0x00] = 0x%08x\n", sc->bar_first_word);

	sc->pci_attached = true;
	return (0);

fail_teardown_intr:
	bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
	sc->intr_cookie = NULL;
fail_release_irq:
	bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
	sc->irq_res = NULL;
fail_hw:
	myfirst_hw_detach(sc);
fail_release_bar:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
}
```

La secuencia de attach ahora tiene siete pasos en lugar de cinco. Dos nuevas etiquetas goto (`fail_teardown_intr`, `fail_release_irq`) extienden la cascada. El patrón es el mismo que en el Capítulo 18: cada paso deshace el anterior, encadenándose hasta la inicialización del softc.

### El detach extendido

La ruta de detach refleja la de attach, con la desinstalación de la interrupción situada entre la desinstalación del cdev y el detach de la capa de hardware:

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	if (myfirst_is_busy(sc))
		return (EBUSY);

	sc->pci_attached = false;

	/* Destroy the cdev so no new user-space access starts. */
	if (sc->cdev != NULL) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}

	/* Tear down the interrupt handler before anything it depends on. */
	if (sc->intr_cookie != NULL) {
		bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
		sc->intr_cookie = NULL;
	}

	/* Quiesce callouts and tasks (includes Chapter 17 simulation if
	 * attached; includes any deferred taskqueue work). */
	myfirst_quiesce(sc);

	/* Release the Chapter 17 simulation if attached. */
	if (sc->sim != NULL)
		myfirst_sim_detach(sc);

	/* Detach the hardware layer. */
	myfirst_hw_detach(sc);

	/* Release the IRQ resource. */
	if (sc->irq_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
		sc->irq_res = NULL;
	}

	/* Release the BAR. */
	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}

	myfirst_deinit_softc(sc);

	device_printf(dev, "detached\n");
	return (0);
}
```

Dos cambios respecto al Capítulo 18: la llamada a `bus_teardown_intr` y la llamada a `bus_release_resource(..., SYS_RES_IRQ, ...)`. El orden importa. `bus_teardown_intr` debe ejecutarse antes de que se libere cualquier elemento que el manejador lea o escriba; en particular, antes de `myfirst_hw_detach` (que libera `sc->hw`). Cuando `bus_teardown_intr` retorna, el kernel garantiza que el manejador no está en ejecución y no volverá a ser invocado; el driver puede entonces liberar todo aquello que el manejador tocara.

La liberación del recurso IRQ ocurre tras la desinstalación del manejador y tras el detach de la capa de hardware. La posición exacta entre el detach del hardware y la liberación del BAR es una cuestión de criterio: el BAR y la IRQ no dependen entre sí, por lo que cualquier orden es válido. El driver del Capítulo 19 libera primero la IRQ porque ese es el orden inverso al de attach (el attach asignó la IRQ después del BAR; el detach la libera antes que el BAR).

La Sección 7 tiene más que decir sobre el orden.

### El sysctl para el contador de interrupciones

Un pequeño diagnóstico: un sysctl que expone el campo `intr_count` para que el lector pueda observar cómo crece el contador:

```c
SYSCTL_ADD_U64(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "intr_count",
    CTLFLAG_RD, &sc->intr_count, 0,
    "Number of times the interrupt filter has run");
```

Tras la carga, `sysctl dev.myfirst.0.intr_count` devuelve el valor actual. Para el dispositivo virtio-rnd sin interrupciones activas (el dispositivo todavía no tiene nada que señalizar), el contador se mantiene en cero. Las interrupciones simuladas de la Sección 5 harán crecer el contador sin necesidad de eventos IRQ reales.

El `sysctl` es legible por cualquier usuario (`CTLFLAG_RD` lo hace accesible en el nivel del sysctl; los permisos de archivo sobre el MIB del sysctl se configuran en otro lugar). Se accede mediante:

```sh
sysctl dev.myfirst.0.intr_count
```

### Lo que demuestra la Etapa 1

Cargar el driver de la Etapa 1 en un invitado bhyve con un dispositivo virtio-rnd produce:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0: BAR0 allocated: 0x20 bytes at 0xc1000000
myfirst0: hardware layer attached to BAR: 32 bytes
myfirst0: attached filter handler on IRQ resource
myfirst0: BAR[0x00] = 0x10010000
```

`vmstat -i | grep myfirst` muestra el evento de interrupción que creó el kernel:

```text
irq19: myfirst0:legacy              0          0
```

(El número de IRQ y la tasa dependen del entorno.)

El contador inicial es cero porque el dispositivo virtio-rnd no está generando interrupciones todavía (no lo hemos programado para ello). El driver está correctamente conectado, el manejador está registrado y el filtro está listo para dispararse. El trabajo de la Etapa 1 está hecho.

### Lo que la Etapa 1 no hace

Varios elementos están deliberadamente ausentes de la Etapa 1 y aparecen en etapas posteriores:

- **Lectura del registro de estado.** El filtro no lee el INTR_STATUS del dispositivo; simplemente incrementa un contador. La Sección 4 añade la lectura del estado.
- **Acuse de recibo.** El filtro no escribe en INTR_STATUS para confirmar la interrupción. En una línea disparada por nivel con un dispositivo que realmente está generando interrupciones, esto es un error. En nuestro objetivo virtio-rnd en bhyve, el dispositivo no está disparando, por lo que la ausencia es invisible. La Sección 4 añade el acuse de recibo y explica por qué importa.
- **Manejador de ithread.** No hay trabajo diferido por ahora. La Sección 4 introduce una ruta diferida basada en taskqueue y conecta el filtro para que la planifique.
- **Interrupciones simuladas.** No hay forma de hacer que el manejador se dispare sin una IRQ real del dispositivo. La Sección 5 añade un sysctl que invoca el filtro directamente bajo las reglas de lock habituales del driver.
- **Disciplina de IRQ compartida.** El filtro asume que toda interrupción pertenece a nuestro dispositivo. La Sección 6 añade la comprobación `FILTER_STRAY` para dispositivos que comparten una línea.

Estos son los temas de las Secciones 4, 5 y 6. La Etapa 1 está deliberadamente incompleta; cada sección posterior añade algo específico que la Etapa 1 dejó pendiente.

### Errores comunes en esta etapa

Una breve lista de trampas en las que los principiantes caen en la Etapa 1.

**Olvidar `INTR_MPSAFE`.** El manejador queda envuelto en Giant. La escalabilidad desaparece. `dmesg` imprime `[GIANT-LOCKED]`. Solución: añadir `INTR_MPSAFE` al argumento de banderas.

**Pasar el argumento incorrecto al filtro.** Un puntero a función en C es estricto; pasar `&sc` en lugar de `sc` produce un doble puntero que el filtro desreferencia de forma incorrecta. El resultado suele ser un kernel panic. Solución: el `arg` en `bus_setup_intr` es `sc`; el filtro recibe el mismo valor como `void *`.

**Devolver 0 desde el filtro.** El valor de retorno es una OR a nivel de bits de los valores `FILTER_*`. Cero equivale a «sin banderas», lo cual es ilegal (el kernel exige al menos uno de `FILTER_STRAY`, `FILTER_HANDLED` o `FILTER_SCHEDULE_THREAD`). El kernel de depuración provoca una aserción. Solución: devolver `FILTER_HANDLED`.

**Usar un sleep lock en el filtro.** El filtro toma `sc->mtx` (un mutex de espera normal). `WITNESS` se queja; el kernel de depuración entra en pánico. Solución: usar operaciones atómicas, o mover el trabajo a un ithread.

**Desinstalar la IRQ antes de consumir la cookie.** Llamar a `bus_release_resource` sobre la IRQ antes de `bus_teardown_intr` es un error: el recurso desaparece, pero el manejador sigue registrado en él. La siguiente interrupción se dispara y el kernel desreferencia estado liberado. Solución: ejecutar siempre `bus_teardown_intr` primero.

**rid equivocado.** El rid pasado a `bus_release_resource` debe coincidir con el rid que devolvió `bus_alloc_resource_any` (o con el rid pasado inicialmente, para `rid = 0`). Una discrepancia suele manifestarse como «Resource not found» o un mensaje del kernel. Solución: almacenar el rid en el softc junto al identificador del recurso.

**Olvidar vaciar el trabajo diferido pendiente antes del desmontaje.** Esto aplica más en la Etapa 2, pero vale la pena señalarlo aquí: si el filtro ha planificado un elemento en el taskqueue, dicho elemento debe completarse antes de que el softc desaparezca. Un desmontaje que libera la IRQ pero deja un elemento pendiente en el taskqueue produce un use-after-free cuando el elemento se ejecuta.

### Punto de verificación: Etapa 1 funcionando

Antes de la Sección 4, confirma que la Etapa 1 está en su sitio:

- `kldstat -v | grep myfirst` muestra el driver en la versión `1.2-intr-stage1`.
- `dmesg | grep myfirst` muestra el banner de attach con la línea `attached filter handler on IRQ resource`.
- No aparece ninguna advertencia `[GIANT-LOCKED]`.
- `devinfo -v | grep -A 5 myfirst` muestra tanto el BAR como el recurso IRQ.
- `vmstat -i | grep myfirst` muestra la fila del manejador.
- `sysctl dev.myfirst.0.intr_count` devuelve `0` (o un número pequeño, según si el dispositivo genera interrupciones de forma espontánea).
- `kldunload myfirst` se ejecuta sin problemas; sin panic, sin advertencias.

Si algún paso falla, regresa a la subsección correspondiente. Los fallos se diagnostican de la misma manera que en el Capítulo 18: comprueba `dmesg` para ver los banners, `devinfo -v` para los recursos y la salida de `WITNESS` para detectar problemas de orden de lock.

### Cerrando la Sección 3

Registrar un manejador de interrupciones supone tres nuevas llamadas (`bus_alloc_resource_any` con `SYS_RES_IRQ`, `bus_setup_intr`, `bus_describe_intr`), tres nuevos campos de softc (`irq_res`, `irq_rid`, `intr_cookie`) y un nuevo contador (`intr_count`). La cascada de attach crece en dos etiquetas; la ruta de detach crece con la llamada a `bus_teardown_intr`. El manejador en sí es un incremento atómico de una sola línea que devuelve `FILTER_HANDLED`.

El objetivo de la Etapa 1 no es el trabajo que realiza el manejador. El objetivo es que el manejador esté correctamente registrado, que `INTR_MPSAFE` esté activo, que el contador se incremente cuando se dispara una interrupción y que el teardown se ejecute limpiamente al descargar el módulo. Cada etapa posterior se construye sobre este andamiaje; hacerlo bien ahora es la inversión que rinde sus frutos a lo largo del resto del capítulo.

La sección 4 hace que el manejador realice trabajo real: leer `INTR_STATUS`, decidir qué hacer, reconocer la interrupción en el dispositivo y delegar el trabajo pesado a un taskqueue. Este es el núcleo de un manejador de interrupciones real, y el contenido más importante para el resto de la Parte 4.

## Sección 4: Cómo escribir un manejador de interrupciones real

La fase 1 demostró que el cableado del manejador es correcto. La fase 2 hace que el manejador realice el trabajo que realiza el filtro de un driver real. La estructura de la sección 4 es un recorrido detallado por el modelo hardware de la simulación del capítulo 17, donde el filtro ahora lee y reconoce el registro `INTR_STATUS` del dispositivo, toma decisiones en función de los bits activos, maneja el trabajo urgente y pequeño en línea y delega el trabajo pesado a un taskqueue. Al final de la sección 4, el driver está en la versión `1.2-intr-stage2` y cuenta con un pipeline filtro-más-tarea que se comporta como un pequeño driver real.

### El mapa de registros

Un breve repaso de la disposición de los registros de interrupción de la simulación del capítulo 17 (consulta `HARDWARE.md` para todos los detalles). El desplazamiento `0x14` contiene `INTR_STATUS`, un registro de 32 bits con estos bits definidos:

- `MYFIRST_INTR_DATA_AV` (`0x00000001`): se ha producido un evento de datos disponibles.
- `MYFIRST_INTR_ERROR` (`0x00000002`): se ha detectado una condición de error.
- `MYFIRST_INTR_COMPLETE` (`0x00000004`): un comando ha finalizado.

El registro tiene semántica "write-one-to-clear" (RW1C): escribir un 1 en un bit lo borra; escribir un 0 no lo modifica. Esta es la convención estándar del estado de interrupción en PCI y es lo que el manejador del capítulo 19 espera.

El desplazamiento `0x10` contiene `INTR_MASK`, un registro paralelo que controla qué bits de `INTR_STATUS` afirman realmente la línea IRQ. Establecer un bit en `INTR_MASK` habilita esa clase de interrupción; borrarlo la deshabilita. El driver establece `INTR_MASK` en el momento del attach para habilitar las interrupciones que desea recibir.

La simulación del capítulo 17 puede activar estos bits de forma autónoma. El driver PCI del capítulo 18 se ejecuta contra un BAR real de virtio-rnd, donde los desplazamientos tienen un significado diferente (configuración virtio heredada, no el mapa de registros del capítulo 17). La sección 4 escribe el manejador siguiendo la semántica del capítulo 17; la sección 5 muestra cómo ejercitar el manejador sin eventos IRQ reales; el dispositivo virtio-rnd no implementa esta disposición de registros, así que en el laboratorio de bhyve el manejador se ejercita principalmente a través del camino de interrupción simulada.

Esta es una limitación honesta del objetivo didáctico. Un lector que adapte el driver a un dispositivo real que implemente los registros del estilo del capítulo 17 vería dispararse el manejador con interrupciones reales directamente. Para el objetivo bhyve virtio-rnd, el sysctl de interrupción simulada de la sección 5 es la forma de ejercitar el filtro de la fase 2 en la práctica.

### El filtro en la fase 2

El filtro de la fase 2 lee `INTR_STATUS`, decide qué ocurrió, reconoce los bits que está manejando y bien realiza el trabajo urgente en línea, bien programa una tarea para el trabajo pesado.

```c
int
myfirst_intr_filter(void *arg)
{
	struct myfirst_softc *sc = arg;
	uint32_t status;
	int rv = 0;

	/*
	 * Read the raw status. The filter runs in primary interrupt
	 * context and cannot take sc->mtx (a sleep mutex), so the access
	 * goes through the specialised accessor that asserts the correct
	 * context. We use a local, spin-safe helper for the BAR access;
	 * Stage 2 uses a small inline instead of the lock-asserting
	 * CSR_READ_4 macro.
	 */
	status = bus_read_4(sc->bar_res, MYFIRST_REG_INTR_STATUS);
	if (status == 0)
		return (FILTER_STRAY);

	atomic_add_64(&sc->intr_count, 1);

	/* Handle the DATA_AV bit: small urgent work only. */
	if (status & MYFIRST_INTR_DATA_AV) {
		atomic_add_64(&sc->intr_data_av_count, 1);
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_DATA_AV);
		taskqueue_enqueue(sc->intr_tq, &sc->intr_data_task);
		rv |= FILTER_HANDLED;
	}

	/* Handle the ERROR bit: log and acknowledge. */
	if (status & MYFIRST_INTR_ERROR) {
		atomic_add_64(&sc->intr_error_count, 1);
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_ERROR);
		rv |= FILTER_HANDLED;
	}

	/* Handle the COMPLETE bit: wake any pending waiters. */
	if (status & MYFIRST_INTR_COMPLETE) {
		atomic_add_64(&sc->intr_complete_count, 1);
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_COMPLETE);
		rv |= FILTER_HANDLED;
	}

	/* If we didn't recognise any bit, this wasn't our interrupt. */
	if (rv == 0)
		return (FILTER_STRAY);

	return (rv);
}
```

Hay varias cosas que merece la pena leer con atención.

**El acceso directo.** El filtro usa `bus_read_4` y `bus_write_4` (los accesores más nuevos basados en recursos) de forma directa, en lugar de las macros `CSR_READ_4` y `CSR_WRITE_4` del capítulo 16. La razón es sutil. Las macros del capítulo 16 toman `sc->mtx` mediante `MYFIRST_ASSERT`, que es un sleep mutex. Un filtro no debe tomar un sleep mutex. El enfoque correcto es usar los accesores `bus_space` directamente (como se muestra) o introducir una familia paralela de macros CSR que no impongan ningún requisito de lock. El refactor de la sección 8 introduce `ICSR_READ_4` e `ICSR_WRITE_4` ("I" de contexto de interrupción) para hacer la distinción explícita; la fase 2 usa los accesores directos.

**La comprobación anticipada de stray.** Un estado de cero significa que ningún bit está activo; se trata de una llamada de IRQ compartida proveniente de otro driver. Devolver `FILTER_STRAY` permite al kernel probar el siguiente manejador. La comprobación también es una defensa frente a una carrera hardware real: si el controlador de interrupciones afirma la línea pero el dispositivo ya ha borrado el estado en el momento en que lo leemos, no debemos reclamar la interrupción.

**El manejo bit a bit.** Cada bit de interés se comprueba, se contabiliza y se reconoce. El orden no importa (los bits son independientes), pero la estructura es convencional: un `if` por bit.

**El reconocimiento.** Escribir el bit de nuevo en `INTR_STATUS` lo borra (RW1C). Esto es lo que hace que la línea de interrupción se desactive. No reconocer una línea con disparo por nivel produce una tormenta de interrupciones.

**El encolado en el taskqueue.** El bit `DATA_AV` desencadena trabajo diferido. El filtro encola una tarea; el thread trabajador del taskqueue ejecuta la tarea más tarde en contexto de thread, donde puede tomar sleep locks y realizar trabajo lento. El encolado es seguro para llamarse desde un filtro (los taskqueues usan spin locks internamente para este camino).

**El valor de retorno final.** Un OR bit a bit de `FILTER_HANDLED` por cada bit reconocido, o `FILTER_STRAY` si nada coincidió. Si tuviéramos trabajo para un ithread, incorporaríamos `FILTER_SCHEDULE_THREAD` mediante OR; pero la fase 2 usa un taskqueue en lugar del ithread, de modo que el valor de retorno es simplemente `FILTER_HANDLED`.

### ¿Por qué un taskqueue y no un ithread?

FreeBSD permite a un driver registrar un manejador ithread a través del quinto argumento de `bus_setup_intr(9)`. ¿Por qué la fase 2 usa un taskqueue en su lugar?

Dos razones.

En primer lugar, el taskqueue es más flexible. Un ithread está ligado al `intr_event` específico; ejecuta la función ithread del driver después del filtro. Un taskqueue permite al driver programar una tarea desde cualquier contexto (filtro, ithread, otras tareas, caminos de ioctl desde el espacio de usuario) y que se ejecute en un thread trabajador compartido. Para el driver del capítulo 19, que ejercita el manejador a través de interrupciones simuladas además de las reales, el taskqueue es una primitiva de trabajo diferido más uniforme.

En segundo lugar, el taskqueue desacopla la prioridad del tipo de interrupción. La prioridad del ithread se deriva de `INTR_TYPE_*`; la prioridad del taskqueue se controla mediante `taskqueue_start_threads(9)`. Para los drivers que quieren que su trabajo diferido tenga una prioridad diferente a la que implica la categoría de interrupción, el taskqueue proporciona ese control.

Los drivers reales de FreeBSD usan ambos patrones. Los drivers simples con interrupciones de tipo "dispara y olvida" suelen usar el ithread (menos código). Los drivers con patrones de trabajo diferido más ricos usan taskqueues. El framework `iflib(9)` usa una especie de híbrido.

El capítulo 19 enseña el patrón del taskqueue porque se integra mejor con el resto del libro. El capítulo 17 ya tiene un taskqueue; el capítulo 14 introdujo el patrón; la disciplina de trabajo diferido es un tema recurrente a lo largo del libro.

### La tarea diferida

El filtro encoló `sc->intr_data_task` cuando vio `DATA_AV`. Esa tarea es:

```c
static void
myfirst_intr_data_task_fn(void *arg, int npending)
{
	struct myfirst_softc *sc = arg;

	MYFIRST_LOCK(sc);

	/*
	 * The data-available event has fired. Read the device's data
	 * register through the Chapter 16 accessor (which takes sc->mtx
	 * implicitly), update the driver's state, and wake any waiting
	 * readers.
	 */
	uint32_t data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
	sc->intr_last_data = data;
	sc->intr_task_invocations++;

	/* Wake any thread sleeping on the data-ready condition. */
	cv_broadcast(&sc->data_cv);

	MYFIRST_UNLOCK(sc);
}
```

Varias propiedades notables.

**La tarea se ejecuta en contexto de thread.** Puede tomar `sc->mtx`, usar `cv_broadcast`, llamar a `malloc(M_WAITOK)` y realizar trabajo lento.

**La tarea respeta la disciplina de locking del capítulo 11.** Se adquiere el mutex; el acceso al CSR usa la macro estándar del capítulo 16; el broadcast de la variable de condición usa la primitiva del capítulo 12.

**El argumento de la tarea es el softc.** Igual que en el filtro. Una implicación sutil: la tarea no puede asumir que el driver no ha sido liberado mediante detach. Si el detach se produce después de que el filtro haya encolado la tarea pero antes de que la tarea se ejecute, la tarea podría ejecutarse contra un softc liberado. La sección 7 cubre la disciplina que evita esto (drain antes de liberar).

**El argumento `npending`** es el número de veces que la tarea fue encolada desde su última ejecución. Para la mayoría de los drivers esto es útil como pista de coalescencia: si `npending` vale 5, el dispositivo señalizó cinco eventos de datos disponibles que se fusionan en una sola ejecución. La tarea de la fase 2 lo ignora; los drivers más grandes lo usan para dimensionar operaciones por lotes.

### Declaración e inicialización de la tarea

El softc incorpora campos relacionados con la tarea:

```c
struct myfirst_softc {
	/* ... existing fields ... */

	/* Chapter 19 interrupt-related fields. */
	struct resource		*irq_res;
	int			 irq_rid;
	void			*intr_cookie;
	uint64_t		 intr_count;
	uint64_t		 intr_data_av_count;
	uint64_t		 intr_error_count;
	uint64_t		 intr_complete_count;
	uint64_t		 intr_task_invocations;
	uint32_t		 intr_last_data;

	struct taskqueue	*intr_tq;
	struct task		 intr_data_task;
};
```

En `myfirst_init_softc` (o en el camino de inicialización):

```c
TASK_INIT(&sc->intr_data_task, 0, myfirst_intr_data_task_fn, sc);
sc->intr_tq = taskqueue_create("myfirst_intr", M_WAITOK,
    taskqueue_thread_enqueue, &sc->intr_tq);
taskqueue_start_threads(&sc->intr_tq, 1, PI_NET,
    "myfirst intr taskq");
```

El taskqueue se crea con un thread trabajador con prioridad `PI_NET` (una prioridad de interrupción; consulta `/usr/src/sys/sys/priority.h`). El nombre `"myfirst intr taskq"` aparece en `top -H` para facilitar el diagnóstico. El `M_WAITOK` durante la creación es correcto porque `myfirst_init_softc` se ejecuta en el contexto de attach, antes de que se produzca ninguna interrupción.

### Habilitación de las interrupciones en el dispositivo

Un detalle que a menudo se olvida: hay que decirle al propio dispositivo que entregue interrupciones. Para la disposición de registros de la simulación del capítulo 17, esto se hace estableciendo bits en el registro `INTR_MASK`:

```c
/* After attaching the hardware layer, enable the interrupts we care
 * about. */
MYFIRST_LOCK(sc);
CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR |
    MYFIRST_INTR_COMPLETE);
MYFIRST_UNLOCK(sc);
```

El registro `INTR_MASK` controla qué bits de `INTR_STATUS` afirman realmente la línea IRQ. Sin él, el dispositivo puede establecer bits de `INTR_STATUS` internamente pero nunca elevar la línea, por lo que el manejador nunca se dispara. Establecer los tres bits habilita las tres clases de interrupción.

Esta es otra limitación honesta del objetivo didáctico. El desplazamiento `0x10` en el BAR heredado de virtio-rnd no es en absoluto un registro de máscara de interrupciones. En la disposición virtio heredada (consulta `/usr/src/sys/dev/virtio/pci/virtio_pci_legacy_var.h`), el dword que comienza en `0x10` está compartido por tres campos pequeños: `queue_notify` en el desplazamiento `0x10` (16 bits), `device_status` en el desplazamiento `0x12` (8 bits) e `isr_status` en el desplazamiento `0x13` (8 bits). Una escritura de 32 bits con el patrón `DATA_AV | ERROR | COMPLETE` (`0x00000007`) en ese desplazamiento escribe `0x0007` en `queue_notify` (notificando a un índice de virtqueue que el dispositivo no tiene) y `0x00` en `device_status` (que la especificación virtio define como un **reinicio del dispositivo**). Escribir cero en `device_status` es la forma en que el driver virtio debe reiniciar el dispositivo antes de la reinicialización.

Por esa razón, la llamada `CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, ...)` tal como está escrita es **segura pero inútil** en el objetivo bhyve virtio-rnd: reinicia la máquina de estados virtio del dispositivo (que nuestro driver no usaba de todas formas) y nunca habilita ninguna interrupción real porque el registro `INTR_MASK` del capítulo 17 no existe en ese dispositivo. Si planeas llevar al lector por este camino en bhyve, mantén la escritura en el código para dar continuidad con un dispositivo real compatible con el capítulo 17, y apóyate en el sysctl de interrupción simulada de la sección 5 para las pruebas en lugar de esperar eventos IRQ reales. Un lector que adapte el driver a un dispositivo real que coincida con el mapa de registros del capítulo 17 vería la escritura de la máscara cumplir su función.

### Deshabilitación de las interrupciones en detach

El paso simétrico en detach:

```c
/* Disable all interrupts at the device before tearing down. */
MYFIRST_LOCK(sc);
if (sc->hw != NULL)
	CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0);
MYFIRST_UNLOCK(sc);
```

Esta escritura ocurre antes de `bus_teardown_intr` para que el dispositivo deje de afirmar la línea antes de que se retire el manejador. La guardia frente a `sc->hw == NULL` protege contra casos de attach parcial en los que la capa hardware falló; la deshabilitación se omite si el hardware no está conectado.

### Un flujo de ejemplo

Un rastreo concreto de lo que ocurre cuando se produce un evento `DATA_AV` en un dispositivo que implementa realmente la semántica del capítulo 17:

1. El dispositivo establece `INTR_STATUS.DATA_AV`. Como `INTR_MASK.DATA_AV` está activo, el dispositivo afirma su línea IRQ.
2. El controlador de interrupciones enruta la IRQ a una CPU.
3. La CPU toma la interrupción y salta al código de despacho del kernel.
4. El kernel encuentra el `intr_event` de nuestra IRQ y llama a `myfirst_intr_filter`.
5. El filtro lee `INTR_STATUS`, ve `DATA_AV`, incrementa los contadores, escribe `DATA_AV` de nuevo en `INTR_STATUS` (borrándolo), encola `intr_data_task` y devuelve `FILTER_HANDLED`.
6. El dispositivo desactiva su línea IRQ (porque `INTR_STATUS.DATA_AV` está ahora borrado).
7. El kernel envía EOI y regresa al thread interrumpido.
8. Algunos milisegundos después, el thread trabajador del taskqueue se despierta, ejecuta `myfirst_intr_data_task_fn`, lee `DATA_OUT`, actualiza el softc y hace un broadcast de la variable de condición.
9. Cualquier thread que estuviera esperando en la variable de condición se despierta y continúa.

Los pasos 1 a 7 duran unos pocos microsegundos. El paso 8 puede tardar cientos de microsegundos o más, que es la razón por la que se realiza en contexto de thread. La separación es lo que permite que el camino de interrupción permanezca rápido.

Para el objetivo bhyve virtio-rnd, los pasos 1 a 6 no ocurren (el dispositivo no coincide con la disposición de registros del capítulo 17). Los pasos 4 a 9 pueden seguir ejercitándose a través del camino de interrupción simulada de la sección 5.

### Trabajo urgente en línea frente a trabajo diferido

Una forma útil de decidir qué va en el filtro y qué va en la tarea: el filtro maneja lo que debe hacerse **por interrupción**, la tarea maneja lo que debe hacerse **por evento**.

Por interrupción (filter):
- Leer `INTR_STATUS` para identificar el evento.
- Reconocer el evento en el dispositivo (escribir de vuelta en `INTR_STATUS`).
- Actualizar los contadores.
- Tomar una única decisión de planificación (encolar el task).

Por evento (task):
- Leer datos de los registros del dispositivo o de los buffers DMA.
- Actualizar la máquina de estados interna del driver.
- Despertar los threads en espera.
- Pasar los datos a la pila de red, la pila de almacenamiento o la cola cdev.
- Gestionar los errores que requieren una recuperación lenta.

La regla general: si el filter invierte más de cien ciclos de CPU en trabajo real (sin contar los accesos a registros, que son baratos por sí solos), probablemente está haciendo demasiado.

### FILTER_SCHEDULE_THREAD vs Taskqueue

Un lector podría preguntar: ¿cuándo usaría `FILTER_SCHEDULE_THREAD` en lugar de un taskqueue?

Usa `FILTER_SCHEDULE_THREAD` cuando:
- Quieres que el ithread por evento del kernel (uno por `intr_event`) ejecute el trabajo lento.
- No necesitas planificar el trabajo desde ningún lugar excepto el filter.
- Quieres que la prioridad de planificación siga el `INTR_TYPE_*` de la interrupción.

Usa un taskqueue cuando:
- Quieres planificar el mismo trabajo desde múltiples rutas (filter, ioctl, sysctl, timeout basado en sleep).
- Quieres compartir el thread de trabajo entre varios dispositivos.
- Quieres control explícito sobre la prioridad mediante `taskqueue_start_threads`.

Para el driver del Capítulo 19, el taskqueue es la opción más limpia porque la Sección 5 planificará la misma tarea desde una ruta de interrupción simulada. Un ithread no sería alcanzable desde allí.

### Cuando el propio taskqueue es la respuesta equivocada

Una advertencia. El taskqueue es excelente para trabajo diferido de corta duración. No lo es tanto para operaciones de larga duración. Si el driver necesita ejecutar una máquina de estados durante varios segundos, bloquearse esperando una transferencia USB o procesar una cadena de buffer larga, un thread de trabajo dedicado es la mejor opción. El thread de trabajo del taskqueue se comparte entre todas las tareas; una sola tarea que se bloquee durante mucho tiempo retrasa a todas las demás que tiene detrás.

La tarea del Capítulo 19 se ejecuta en microsegundos. El taskqueue es adecuado. El driver MSI-X del Capítulo 20, con procesamiento de recepción por cola, puede querer threads de trabajo por cola. La transferencia masiva impulsada por DMA del Capítulo 21 puede querer un thread dedicado. Cada capítulo elige el primitivo correcto para su carga de trabajo; el Capítulo 19 usa el más sencillo que se ajusta a sus necesidades.

### Errores frecuentes en esta etapa

Una lista breve.

**Leer INTR_STATUS sin acusar recibo.** El handler lee, decide y retorna sin escribir de vuelta. En una línea con disparo por nivel, el dispositivo sigue afirmando la señal; el handler se dispara de nuevo de inmediato; tormenta de interrupciones. Solución: acusa recibo de cada bit que hayas gestionado.

**Acusar recibo de demasiados bits.** Un handler descuidado escribe `0xffffffff` en `INTR_STATUS` en cada llamada para «borrar todos los bits». Esto también borra eventos que el handler no ha procesado, con lo que se pierden datos o se confunde la máquina de estados. Solución: acusa recibo únicamente de los bits que hayas gestionado.

**Tomar sleep locks en el filter.** `MYFIRST_LOCK(sc)` toma `sc->mtx`, que es un sleep mutex. En el filter, esto es un error; `WITNESS` provoca un pánico. Solución: usa operaciones atómicas en el filter y toma el sleep mutex únicamente en la tarea (que se ejecuta en contexto de thread).

**Planificar una tarea después de que el softc ha sido destruido.** Si la tarea se planifica desde el filter pero este se ejecuta después de que detach ha desmontado el driver parcialmente, la tarea opera sobre estado obsoleto. Solución: la Sección 7 cubre el orden correcto. En resumen: `bus_teardown_intr` debe ocurrir antes de liberar la capa de hardware, y `taskqueue_drain` debe ocurrir antes de liberar el taskqueue.

**Usar `CSR_READ_4`/`CSR_WRITE_4` directamente en el filter.** Si el accessor del Capítulo 16 afirma que `sc->mtx` está tomado (algo que ocurre en kernels de depuración), el filter provoca un pánico. Solución: usa `bus_read_4`/`bus_write_4` directamente o introduce un conjunto paralelo de macros CSR seguros para interrupciones. La Sección 8 gestiona esto con `ICSR_READ_4`.

**Encolar la tarea sin `TASK_INIT`.** Una tarea encolada antes de `TASK_INIT` tiene un puntero de función corrupto. La primera ejecución de la tarea salta a basura. Solución: inicializa la tarea en la ruta de attach antes de habilitar las interrupciones.

**Olvidar habilitar las interrupciones en el dispositivo.** El handler está registrado y `bus_setup_intr` ha tenido éxito; `vmstat -i` sigue mostrando cero disparos. El problema es que el registro `INTR_MASK` del dispositivo sigue valiendo cero (o cualquier valor que tenga tras el reset), así que el dispositivo nunca afirma la línea. Solución: escribe en `INTR_MASK` durante attach.

**Olvidar deshabilitar las interrupciones en detach.** El handler ha sido desmontado pero el dispositivo sigue afirmando la línea. El kernel acabará quejándose de una interrupción espuria o, peor aún, otro driver que comparta la línea verá actividad misteriosa. Solución: borra `INTR_MASK` antes de `bus_teardown_intr`.

### Salida de la Etapa 2: cómo se ve el éxito

Después de cargar la Etapa 2 en un dispositivo real que produce interrupciones, `dmesg` muestra:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0: BAR0 allocated: 0x20 bytes at 0xc1000000
myfirst0: hardware layer attached to BAR: 32 bytes
myfirst0: attached filter handler on IRQ resource
myfirst0: interrupts enabled (mask=0x7)
myfirst0: BAR[0x00] = 0x10010000
```

La línea `interrupts enabled` es nueva. Confirma que el driver ha escrito en `INTR_MASK`.

En un dispositivo real que genere interrupciones, `sysctl dev.myfirst.0.intr_count` irá aumentando. En el target virtio-rnd de bhyve, el contador permanece en cero porque el dispositivo no dispara las interrupciones que esperamos. La ruta de interrupción simulada de la Sección 5 es la forma de ejercitar el handler desde ese entorno.

### Cerrando la Sección 4

Un handler de interrupción real lee `INTR_STATUS` para identificar la causa, gestiona cada bit de interés, acusa recibo de los bits gestionados escribiendo de vuelta en `INTR_STATUS`, y devuelve la combinación correcta de valores `FILTER_*`. El trabajo urgente (acceso a registros, actualizaciones de contadores, acuses de recibo) ocurre en el filter. El trabajo lento (lecturas de datos a través de los accessors del Capítulo 16 que toman `sc->mtx`, broadcasts de variable de condición, notificaciones al espacio de usuario) ocurre en una tarea de taskqueue que el filter encola.

El filter es corto (entre veinte y cuarenta líneas de código real para un dispositivo típico). La tarea también lo es (entre diez y treinta líneas). La composición es lo que hace funcional al driver: el filter gestiona las interrupciones a la tasa de interrupción; la tarea gestiona los eventos a la tasa de thread; la separación mantiene la ventana de interrupción reducida y el trabajo diferido libre de bloquearse.

La Sección 5 es la que permite al lector ejercitar este mecanismo en un target de bhyve donde la ruta IRQ real no coincide con la semántica de registros del Capítulo 17. Añade un sysctl que invoca el filter bajo las reglas de lock normales del driver, permite al lector disparar interrupciones simuladas a voluntad, y confirma que los contadores, la tarea y el broadcast de variable de condición se comportan según lo diseñado.



## Sección 5: Uso de interrupciones simuladas para las pruebas

El filter y la tarea de la Sección 4 son código real de driver. Están listos para gestionar interrupciones reales en un dispositivo que coincida con el esquema de registros del Capítulo 17. El problema que presenta el target de laboratorio del Capítulo 19 es que el dispositivo que tenemos (virtio-rnd bajo bhyve) no coincide con ese esquema; escribir los bits de máscara de interrupción del Capítulo 17 en el BAR de virtio-rnd tiene efectos definidos pero sin relación, y leer los bits de estado de interrupción del Capítulo 17 desde el BAR de virtio-rnd devuelve valores específicos de virtio que no tienen nada que ver con nuestra semántica simulada. En este target, el filter, si llega a dispararse, verá basura.

La Sección 5 resuelve esto enseñando al lector a simular interrupciones. La idea central es sencilla: exponer un sysctl que, cuando se escribe en él, invoca el handler del filter directamente bajo las reglas de lock normales del driver, exactamente como lo haría el kernel desde una interrupción real. El filter lee el registro `INTR_STATUS` (que el lector también puede escribir a través de otro sysctl, o a través del backend de simulación del Capítulo 17 en una build solo de simulación), toma las mismas decisiones que tomaría con una interrupción real, y maneja el pipeline completo de extremo a extremo.

### Por qué la simulación merece una sección propia

Un lector que haya completado el Capítulo 17 podría preguntar razonablemente: ¿no era ya toda la simulación del Capítulo 17 una forma de simular interrupciones? Sí y no.

El Capítulo 17 simulaba un **dispositivo autónomo**. Sus callouts cambiaban los valores de los registros según su propio calendario, su callout de comandos se disparaba cuando el driver escribía en `CTRL.GO`, y su marco de inyección de fallos hacía que el dispositivo simulado se comportara incorrectamente. El driver del Capítulo 17 era un driver solo de simulación; no había `bus_setup_intr` porque no había un bus real.

El Capítulo 19 es diferente. El driver ahora tiene un handler real de `bus_setup_intr` registrado en una línea IRQ real. Los callouts del Capítulo 17 no intervienen; en el build PCI, la simulación del Capítulo 17 no se ejecuta. Lo que queremos es una forma de disparar el **handler del filter** directamente, con la semántica de lock exacta que produciría una interrupción real, para poder validar el pipeline de filter y tarea de la Sección 4 sin depender de un dispositivo que produzca las interrupciones correctas.

La forma más limpia de hacerlo, y la que muchos drivers de FreeBSD usan para propósitos similares, es una escritura en un sysctl que invoca la función del filter directamente. El filter se ejecuta en el contexto del llamador (contexto de thread, donde se origina la escritura del sysctl), pero el código del filter no tiene en cuenta el contexto externo siempre que la disciplina de lock interna sea correcta. Un incremento atómico, una lectura del BAR, una escritura en el BAR, un `taskqueue_enqueue`: todo esto funciona desde el contexto de thread también. La llamada simulada ejercita los mismos caminos de código que el kernel ejercitaría con una interrupción real.

Existe una distinción sutil. Con una interrupción real, el kernel hace que los filters del mismo `intr_event` se ejecuten en serie en una sola CPU. Una llamada simulada activada por sysctl no tiene esa garantía; otro thread podría invocar el filter al mismo tiempo. Para el driver del Capítulo 19, esto no es un problema porque el estado del filter está protegido por operaciones atómicas (y no por la garantía del kernel de una sola CPU por IRQ). Para un driver que dependa de la serialización implícita de una sola CPU, la simulación mediante sysctl no sería una prueba fiel. La lección es: los drivers `INTR_MPSAFE` que usan atómicos y spin locks se traducen limpiamente a simulación.

### El sysctl de interrupción simulada

El mecanismo es un sysctl de solo escritura que invoca el filter:

```c
static int
myfirst_intr_simulate_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	uint32_t mask;
	int error;

	mask = 0;
	error = sysctl_handle_int(oidp, &mask, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	/*
	 * "mask" is the INTR_STATUS bits the caller wants to pretend
	 * the device has set. Set them in the real register, then call
	 * the filter directly.
	 */
	MYFIRST_LOCK(sc);
	if (sc->hw == NULL) {
		MYFIRST_UNLOCK(sc);
		return (ENODEV);
	}
	bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS, mask);
	MYFIRST_UNLOCK(sc);

	/* Invoke the filter directly. */
	(void)myfirst_intr_filter(sc);

	return (0);
}
```

Y la declaración del sysctl en `myfirst_intr_add_sysctls`:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "intr_simulate",
    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE,
    sc, 0, myfirst_intr_simulate_sysctl, "IU",
    "Simulate an interrupt by setting INTR_STATUS bits and "
    "invoking the filter");
```

Escribir en `dev.myfirst.0.intr_simulate` hace que el handler se ejecute con los bits INTR_STATUS especificados activados.

### Ejercitar la simulación

Una vez que el sysctl está en su lugar, el lector puede dirigir el pipeline completo desde el espacio de usuario:

```sh
# Simulate a DATA_AV event.
sudo sysctl dev.myfirst.0.intr_simulate=1

# Check counters.
sysctl dev.myfirst.0.intr_count
sysctl dev.myfirst.0.intr_data_av_count
sysctl dev.myfirst.0.intr_task_invocations

# Simulate an ERROR event.
sudo sysctl dev.myfirst.0.intr_simulate=2

# Simulate a COMPLETE event.
sudo sysctl dev.myfirst.0.intr_simulate=4

# Simulate all three at once.
sudo sysctl dev.myfirst.0.intr_simulate=7
```

La primera llamada incrementa `intr_count` (el filter se ha disparado), `intr_data_av_count` (bit DATA_AV reconocido) y, finalmente, `intr_task_invocations` (la tarea del taskqueue se ha ejecutado). La segunda incrementa `intr_count` e `intr_error_count`. La tercera incrementa `intr_count` e `intr_complete_count`. La cuarta activa los tres.

Un lector puede verificar el pipeline completo:

```sh
# Watch counters in a loop.
while true; do
    sudo sysctl dev.myfirst.0.intr_simulate=1
    sleep 0.5
    sysctl dev.myfirst.0 | grep intr_
done
```

Los contadores avanzan al ritmo esperado. El driver se comporta como si estuvieran llegando interrupciones reales.

### Por qué esto no es un juguete

Podría pensarse que esta ruta simulada es un artificio pedagógico. No lo es. Muchos drivers reales mantienen una ruta similar para propósitos de diagnóstico. Vale la pena enumerar las razones:

**Pruebas de regresión.** Una ruta de interrupción simulada permite a un pipeline de CI ejercitar el handler sin necesitar hardware real. El Capítulo 17 presentó el mismo argumento para simular el comportamiento del dispositivo; la Sección 5 lo hace para simular la ruta de interrupción.

**Inyección de fallos.** Un sysctl de interrupción simulada permite a una prueba inyectar patrones específicos de `INTR_STATUS` para ejercitar el código de gestión de errores. La respuesta del driver a `INTR_STATUS = ERROR | COMPLETE` (ambos bits activados simultáneamente) es difícil de provocar con hardware real; un sysctl que activa ambos bits y llama al handler lo hace sencillo.

**Productividad del desarrollador.** Cuando el autor de un driver está depurando la lógica del handler, disponer de un sysctl que lo dispare bajo demanda es enormemente útil. `dtrace -n 'fbt::myfirst_intr_filter:entry'` combinado con `sudo sysctl dev.myfirst.0.intr_simulate=1` ofrece una vista paso a paso del handler bajo demanda.

**Puesta en marcha de hardware nuevo.** El autor de un driver tiene con frecuencia un dispositivo prototipo que aún no produce interrupciones correctamente. Una ruta de interrupción simulada permite probar las capas superiores del driver antes de que el hardware funcione, lo que significa que el driver y el hardware pueden desarrollarse en paralelo en lugar de hacerlo de forma secuencial.

**Enseñanza.** Para los propósitos de este libro, la ruta simulada hace que el filter y la tarea sean observables en un target de laboratorio que no produce de forma natural las interrupciones esperadas. El lector puede ver el pipeline en funcionamiento aunque el hardware no coopere.

### El sistema de lock en la ruta simulada

Un detalle que vale la pena analizar con detenimiento. El sysctl escribe `INTR_STATUS` mientras mantiene `sc->mtx`. El handler del filtro, cuando se invoca a través del camino de interrupción real del kernel, se ejecuta sin que `sc->mtx` esté adquirido (el filtro usa `bus_read_4` / `bus_write_4` directamente, sin los macros CSR que verifican el lock). Cuando se invoca a través del sysctl, ¿cuál es el contexto de llamada?

El handler del sysctl se ejecuta en contexto de thread. `MYFIRST_LOCK(sc)` adquiere el sleep mutex. Entre la adquisición del lock y su liberación, el thread mantiene el mutex. A continuación se libera el lock y se llama a `myfirst_intr_filter(sc)`. El filtro no toma ningún lock, usa solo operaciones atómicas y `bus_read_4`/`bus_write_4`, encola una tarea y retorna. Toda la secuencia es segura.

¿Sería seguro llamar al filtro con `sc->mtx` adquirido? Sí, en realidad: el filtro no intenta adquirir el mismo mutex, y se ejecuta en un contexto en el que mantener el lock no es en sí mismo ilegal (contexto de thread). Sin embargo, el filtro fue diseñado para ser agnóstico al contexto; llamarlo con un sleep lock adquirido oscurecería ese contrato. El sysctl libera el lock antes de invocar el filtro por claridad.

### Uso de la simulación del capítulo 17 para producir interrupciones

Una técnica complementaria que merece la pena mencionar. El backend de simulación del capítulo 17, si está conectado, produce cambios de estado autónomos a su propio ritmo. En particular, su callout que produce `DATA_AV` establece `INTR_STATUS.DATA_AV`. En una compilación solo de simulación (`MYFIRST_SIMULATION_ONLY` definido en tiempo de compilación), la simulación está activa, el callout se dispara, y el driver del capítulo 17 puede incluso llamar al filtro desde el propio callout.

El capítulo 19 no modifica el comportamiento del capítulo 17 en compilaciones solo de simulación. Un lector que quiera ver el filtro controlado por la simulación del capítulo 17 puede compilar con `-DMYFIRST_SIMULATION_ONLY`, cargar el módulo y observar cómo los callouts establecen los bits de `INTR_STATUS`. La ruta activada por sysctl de la sección 5 sigue disponible en ambas compilaciones.

En la compilación PCI, la simulación del capítulo 17 no está conectada (según la disciplina del capítulo 18), de modo que los callouts del capítulo 17 no se ejecutan. La ruta de interrupción simulada es la única forma de controlar el filtro en la compilación PCI.

### Extensión del sysctl para programar a una tasa determinada

Una extensión útil para pruebas de carga: un sysctl que programa interrupciones simuladas periódicamente a través de un callout. El callout se dispara cada N milisegundos, establece un bit en `INTR_STATUS` e invoca el filtro. El lector puede ajustar la tasa y observar el pipeline bajo carga.

```c
static void
myfirst_intr_sim_callout_fn(void *arg)
{
	struct myfirst_softc *sc = arg;

	MYFIRST_LOCK(sc);
	if (sc->intr_sim_period_ms > 0 && sc->hw != NULL) {
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_DATA_AV);
		MYFIRST_UNLOCK(sc);
		(void)myfirst_intr_filter(sc);
		MYFIRST_LOCK(sc);
		callout_reset_sbt(&sc->intr_sim_callout,
		    SBT_1MS * sc->intr_sim_period_ms, 0,
		    myfirst_intr_sim_callout_fn, sc, 0);
	}
	MYFIRST_UNLOCK(sc);
}
```

El callout se reprograma a sí mismo mientras `intr_sim_period_ms` sea distinto de cero. Un sysctl expone el período:

```sh
# Fire a simulated interrupt every 100 ms.
sudo sysctl hw.myfirst.intr_sim_period_ms=100

# Stop simulating.
sudo sysctl hw.myfirst.intr_sim_period_ms=0
```

Observa cómo los contadores crecen a la tasa esperada:

```sh
sleep 10
sysctl dev.myfirst.0.intr_count
```

Tras diez segundos con un período de 100 ms, el contador debería indicar aproximadamente 100. Si indica bastante menos, el filtro o la tarea es el cuello de botella (poco probable a esta escala; más preocupante en pruebas de alta tasa). Si indica bastante más, algo está disparando el filtro desde otro lugar.

### Lo que la simulación no captura

Límites reales de la técnica.

**Disparos concurrentes.** El sysctl serializa las interrupciones simuladas a razón de una por escritura. Una ruta de interrupción real puede ver dos interrupciones consecutivas en distintas CPU, algo que una prueba con sysctl no produciría. Para las pruebas de estrés de concurrencia, resulta más eficaz una prueba separada que lance múltiples threads, cada uno escribiendo en el sysctl.

**Comportamiento del controlador de interrupciones.** La simulación omite por completo el controlador de interrupciones. Las pruebas que dependen de la temporización del EOI, el enmascaramiento o la detección de tormentas no pueden realizarse de este modo.

**Afinidad de CPU.** El filtro simulado se ejecuta en la CPU en la que se encuentre el thread que escribe en el sysctl. Una interrupción real se dispara en la CPU que seleccione la configuración de afinidad. Las pruebas de comportamiento por CPU necesitan interrupciones reales u otro mecanismo.

**Contención con la ruta de interrupción real.** Si también se disparan interrupciones reales (quizás porque el dispositivo realmente genera algunas), la ruta simulada puede competir con la ruta real. Los contadores atómicos lo gestionan correctamente; un estado compartido más complejo podría no hacerlo.

Estos son límites, no impedimentos definitivos. Para la mayoría de las pruebas del capítulo 19, la ruta simulada es suficiente. Para las pruebas de estrés avanzadas se aplican técnicas adicionales (rt-threads, invocaciones multi-CPU, hardware real).

### Observación de la tarea en ejecución

Un diagnóstico que merece la pena exponer. El contador `intr_task_invocations` de la tarea avanza cada vez que esta se ejecuta. El lector puede compararlo con `intr_data_av_count` para comprobar si el taskqueue sigue el ritmo:

```sh
sudo sysctl dev.myfirst.0.intr_simulate=1    # fire DATA_AV
sleep 0.1
sysctl dev.myfirst.0.intr_data_av_count       # should be 1
sysctl dev.myfirst.0.intr_task_invocations    # should also be 1
```

Si el contador de tareas va por detrás del contador DATA_AV, el worker del taskqueue tiene trabajo acumulado. A esta escala no debería ocurrir; a tasas más altas (miles por segundo) podría ocurrir.

Una sonda más sensible: añadir una ruta `cv_signal` en la que espere un programa del espacio de usuario. El sysctl dispara la interrupción simulada; el filtro encola la tarea; la tarea actualiza `sc->intr_last_data` y emite un broadcast; el thread del espacio de usuario que esperaba en la variable de condición (a través del `read` del cdev) se despierta. La latencia de ida y vuelta desde la escritura en el sysctl hasta el despertar es, aproximadamente, la latencia de interrupción a espacio de usuario del driver, un dato útil que conviene conocer.

### Integración con el framework de fallos del capítulo 17

Una idea que merece la pena mencionar. El framework de inyección de fallos del capítulo 17 (los registros `FAULT_MASK` y `FAULT_PROB`) se aplica a los comandos, no a las interrupciones. El capítulo 19 puede ampliar el framework añadiendo una opción de «fallo en la siguiente interrupción»: un sysctl que haga que la siguiente llamada al filtro omita el reconocimiento, provocando una tormenta en una línea disparada por nivel.

Se trata de una extensión opcional. Los ejercicios de desafío la mencionan; el cuerpo principal del capítulo no la requiere.

### Cerrando la sección 5

Simular interrupciones es una técnica sencilla pero eficaz. Un sysctl escribe en `INTR_STATUS`, invoca el filtro directamente, y el filtro conduce el pipeline completo: actualizaciones de contadores, reconocimiento, encaje en cola de la tarea y ejecución de la tarea. La técnica permite ejercitar un driver de extremo a extremo en un objetivo de laboratorio que no produce de manera natural las interrupciones esperadas, y resulta económica de mantener en los drivers en producción para pruebas de regresión y acceso de diagnóstico.

La sección 6 es la última pieza conceptual del manejo básico de interrupciones. Cubre las interrupciones compartidas: qué ocurre cuando múltiples drivers escuchan en la misma línea IRQ, cómo un manejador de filtro debe identificar si la interrupción pertenece a su dispositivo, y qué significa `FILTER_STRAY` en la práctica.



## Sección 6: Manejo de interrupciones compartidas

El filtro de la etapa 2 de la sección 4 ya tiene la forma correcta de valor de retorno para IRQs compartidas: devuelve `FILTER_STRAY` cuando `INTR_STATUS` es cero. La sección 6 explora por qué esa comprobación constituye toda la disciplina, qué sale mal cuando un manejador lo hace incorrectamente, y cuándo vale la pena establecer el flag `RF_SHAREABLE`.

### ¿Por qué compartir una IRQ?

Dos razones.

Primera, **restricciones de hardware**. La arquitectura PC clásica tenía 16 líneas IRQ de hardware; el I/O APIC amplió este número a 24 en muchos chipsets. Un sistema con 30 dispositivos necesariamente tiene algunos de ellos compartiendo líneas. En los sistemas x86 modernos, la escasez es menos acuciante (cientos de vectores), pero en el PCI heredado y en muchos SoC arm64, el uso compartido es habitual.

Segunda, **portabilidad del driver**. Un driver que gestiona correctamente las interrupciones compartidas también gestiona correctamente las interrupciones exclusivas (la ruta compartida es un superconjunto). Un driver que asume interrupciones exclusivas falla cuando el hardware cambia o cuando otro driver llega a la misma línea. Escribir para el caso compartido no tiene prácticamente ningún coste y hace al driver a prueba de futuro.

En PCIe con MSI o MSI-X habilitados (capítulo 20), cada dispositivo tiene sus propios vectores y el uso compartido raramente es necesario. Pero incluso en ese caso, un driver que gestiona correctamente una interrupción extraña (devolviendo `FILTER_STRAY`) es mejor driver que uno que no lo hace. La disciplina sigue siendo válida.

### El flujo en una IRQ compartida

Cuando se dispara una IRQ compartida, el kernel recorre la lista de manejadores de filtro asociados al `intr_event` en orden de registro. Cada filtro se ejecuta, comprueba si la interrupción pertenece a su dispositivo y devuelve en consecuencia:

- Si el filtro reclama la interrupción (devuelve `FILTER_HANDLED` o `FILTER_SCHEDULE_THREAD`), el kernel continúa con el siguiente filtro, si lo hay, y agrega los resultados. En los kernels modernos, un filtro que devuelve `FILTER_HANDLED` no impide que los filtros posteriores se ejecuten; el kernel recorre siempre la lista completa.
- Si el filtro devuelve `FILTER_STRAY`, el kernel prueba el siguiente filtro.

Tras ejecutarse todos los filtros, si alguno reclamó la interrupción, el kernel la reconoce en el controlador de interrupciones y retorna. Si todos los filtros devolvieron `FILTER_STRAY`, el kernel incrementa un contador de interrupciones extrañas; si el recuento supera un umbral, el kernel deshabilita la IRQ (el último recurso drástico).

Un filtro que devuelve `FILTER_STRAY` cuando la interrupción era realmente para su dispositivo es un bug: la línea permanece asertada (disparada por nivel), la maquinaria de tormenta entra en acción y el dispositivo no recibe servicio. Un filtro que devuelve `FILTER_HANDLED` cuando la interrupción no era para su dispositivo también es un bug: la interrupción de otro driver queda marcada como servida, su manejador nunca se ejecuta, sus datos se quedan en el FIFO y la red o el disco del usuario dejan de funcionar.

La disciplina consiste en decidir la propiedad con precisión, basándose en el estado del dispositivo, y devolver el valor correcto.

### La prueba de INTR_STATUS

La forma estándar de decidir la propiedad es leer un registro del dispositivo que indique si la interrupción está pendiente. En un dispositivo con un registro INTR_STATUS por dispositivo, la pregunta es «¿hay algún bit establecido en INTR_STATUS?». Si es así, la interrupción es mía. Si no, no lo es.

El diseño de registros del capítulo 17 hace que esto sea sencillo:

```c
status = bus_read_4(sc->bar_res, MYFIRST_REG_INTR_STATUS);
if (status == 0)
	return (FILTER_STRAY);
```

Esto es exactamente lo que ya hace el filtro de la etapa 2. El patrón es robusto: si el registro de estado lee cero, no hay ningún evento pendiente de este dispositivo, por lo que la interrupción no es nuestra.

Un detalle sutil: la lectura de `INTR_STATUS` debe ocurrir antes de cualquier cambio de estado que pueda enmascarar o restablecer los bits. Leer `INTR_STATUS` con el dispositivo en un estado intermedio está bien (el registro refleja la vista actual del dispositivo); escribir primero en otros registros y después leer `INTR_STATUS` podría perder bits que las escrituras borraron inadvertidamente.

### Cómo se ve «¿Es mía?» en hardware real

La prueba de INTR_STATUS es de manual porque el diseño de registros del capítulo 17 también lo es. Los dispositivos reales se presentan en distintas variantes.

**Dispositivos con un INTR_STATUS limpio.** La mayoría de los dispositivos modernos tienen un registro que, cuando se lee como cero, dice definitivamente «no es mío». La forma del filtro del driver del capítulo 19 se aplica directamente.

**Dispositivos con bits siempre establecidos.** Algunos dispositivos tienen bits de interrupción pendiente que permanecen establecidos entre interrupciones (a la espera de que el driver los restablezca). El filtro debe enmascararlos o comprobarlos frente a una máscara por clase de interrupción. El diseño de registros del capítulo 17 evita esta complicación; los drivers reales se enfrentan a ella ocasionalmente.

**Dispositivos sin ningún INTR_STATUS.** Unos pocos dispositivos más antiguos requieren que el driver lea una secuencia de registros separada (o deduzca a partir de los registros de estado) si la interrupción está pendiente. Estos drivers son más complejos; el filtro puede necesitar adquirir un spinlock y leer varios registros. El código fuente de FreeBSD tiene ejemplos en algunos drivers embebidos.

**Dispositivos con un INTR_STATUS global y registros por fuente.** Un patrón habitual en NIC: un registro de nivel superior informa de qué cola tiene un evento pendiente, y los registros por cola contienen los detalles del evento. El filtro lee el registro de nivel superior para decidir la propiedad; el ithread o la tarea leen los registros por cola para procesar los eventos.

El driver del capítulo 19 usa la primera variante. La disciplina para el resto de variantes es la misma: leer un registro, decidir.

### Devolución correcta de FILTER_STRAY

La regla es sencilla: si el filtro no reconoce ningún bit como perteneciente a una clase que gestiona, devuelve `FILTER_STRAY`.

```c
if (rv == 0)
	return (FILTER_STRAY);
```

La variable `rv` acumula `FILTER_HANDLED` de cada bit reconocido. Si no se reconoció ningún bit, `rv` es cero, y el filtro no tiene nada que devolver salvo `FILTER_STRAY`.

Un corolario sutil: un filtro que reconoce algunos bits pero no otros devuelve `FILTER_HANDLED` para los bits reconocidos y no devuelve `FILTER_STRAY` para los que no reconoció. Establecer un bit en `INTR_MASK` que el driver no va a gestionar es un bug del driver; el kernel no puede ayudar.

Un caso límite interesante: un bit está establecido en `INTR_STATUS` pero el driver no lo reconoce (quizás una nueva revisión del dispositivo añadió un bit que el código del driver no contempla). El driver tiene dos opciones:

1. Ignorar el bit. No reconocerlo. Dejarlo establecido. En una línea disparada por nivel, esto produce una tormenta porque el bit aserta la línea indefinidamente. Malo.

2. Reconocer el bit sin hacer ningún trabajo. Escribirlo de nuevo en `INTR_STATUS`. El dispositivo deja de asertar para ese bit, no hay tormenta, pero el evento se pierde. En un evento esencial esto es un bug funcional; en un evento de diagnóstico puede ser aceptable.

El patrón recomendado es la opción 2 con un mensaje de log: reconocer los bits no identificados, registrarlos a una tasa reducida (para evitar inundar el log si el bit se activa de forma continua) y continuar. Esto hace que el driver sea robusto frente a nuevas revisiones de hardware, a costa de perder potencialmente información sobre eventos desconocidos.

```c
uint32_t unknown = status & ~(MYFIRST_INTR_DATA_AV |
    MYFIRST_INTR_ERROR | MYFIRST_INTR_COMPLETE);
if (unknown != 0) {
	atomic_add_64(&sc->intr_unknown_count, 1);
	bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS, unknown);
	rv |= FILTER_HANDLED;
}
```

Este fragmento no forma parte del filtro de la Etapa 2; es una extensión útil para la Etapa 3 o posterior.

### Qué ocurre cuando varios drivers comparten una IRQ

Un escenario concreto. Supón que el dispositivo virtio-rnd en un guest de bhyve comparte la IRQ 19 con el controlador AHCI. Ambos drivers han registrado sus handlers. Llega una interrupción en la IRQ 19.

El kernel recorre la lista de handlers en orden de registro. Supón que AHCI se registró primero, así que su filtro se ejecuta primero:

1. Filtro AHCI: lee su INTR_STATUS, ve bits activados (AHCI tiene I/O pendiente), lo reconoce y devuelve `FILTER_HANDLED`.
2. Filtro `myfirst`: lee su INTR_STATUS, lee cero y devuelve `FILTER_STRAY`.

El kernel ve "al menos un `FILTER_HANDLED`" y no marca la interrupción como extraña.

Ahora el caso inverso. El dispositivo virtio-rnd tiene un evento:

1. Filtro AHCI: lee su INTR_STATUS, ve cero y devuelve `FILTER_STRAY`.
2. Filtro `myfirst`: lee su INTR_STATUS, ve `DATA_AV`, lo reconoce y devuelve `FILTER_HANDLED`.

El kernel ve un `FILTER_HANDLED` y queda satisfecho.

La propiedad clave es que cada filtro solo comprueba su propio dispositivo. Ningún filtro asume que la interrupción le pertenece; cada uno decide a partir del estado de su dispositivo.

### Qué ocurre cuando un driver lo hace mal

Un filtro AHCI defectuoso que devuelva `FILTER_HANDLED` cada vez que se dispara (sin comprobar el estado) reclamaría nuestra interrupción de `myfirst`. El filtro de `myfirst` nunca se ejecutaría, `DATA_AV` nunca se reconocería y la línea entraría en tormenta.

La corrección no está en el lado de `myfirst`; está en el lado de AHCI. En la práctica, todos los drivers principales de FreeBSD hacen la comprobación correctamente porque el código lleva años auditado y probado. La lección es que el protocolo de IRQ compartida exige cooperación: cada driver en la línea debe comprobar su propio estado correctamente.

La protección contra un único driver defectuoso es `hw.intr_storm_threshold`. Cuando el kernel detecta una serie de interrupciones marcadas todas como extrañas (o todas devolviendo `FILTER_HANDLED` sin que ningún dispositivo tenga trabajo real), eventualmente enmascarará la línea. El mecanismo es de detección, no de prevención.

### Coexistencia con drivers que no comparten

Un driver que asigna su IRQ con `RF_SHAREABLE` puede coexistir con drivers que asignan sin compartir, siempre que el kernel pueda satisfacer ambas peticiones. Si nuestro driver `myfirst` asigna primero con `RF_SHAREABLE` y luego AHCI intenta asignar en modo exclusivo, la asignación de AHCI fallará (la línea ya está en manos de un driver que podría no ser exclusivo). Si AHCI asigna primero sin compartir, nuestra asignación de `myfirst` (con `RF_SHAREABLE`) fallará.

En la práctica, los drivers modernos casi siempre usan `RF_SHAREABLE`. Los drivers heredados a veces lo omiten; si el driver de un lector no puede cargarse debido a un conflicto de asignación de interrupción, la solución suele ser añadir `RF_SHAREABLE` a la asignación.

La asignación exclusiva es adecuada para:

- Drivers con requisitos estrictos de latencia que no pueden tolerar otros handlers en la línea.
- Drivers que usan `INTR_EXCL` por alguna razón específica del kernel.
- Algunos drivers heredados que se escribieron antes de que el soporte de IRQ compartida madurara.

Para el driver del capítulo 19, `RF_SHAREABLE` es la opción predeterminada y nunca incorrecta.

### La topología IRQ de virtio en bhyve

Un detalle práctico sobre el entorno de laboratorio del capítulo 19. El emulador bhyve asigna a cada dispositivo PCI emulado una línea IRQ basándose en el pin INTx del slot. Varias funciones del mismo slot comparten una línea; slots distintos suelen tener líneas distintas. El dispositivo virtio-rnd en el slot 4, función 0, tiene su propio pin.

En la práctica, en un guest de bhyve con apenas unos pocos dispositivos emulados, cada dispositivo suele tener su propia línea IRQ (sin compartir). El driver `myfirst` con `RF_SHAREABLE` asignado en una línea no compartida se comporta de forma idéntica a una asignación no compartible; el flag no tiene ningún efecto negativo.

Para probar deliberadamente el comportamiento de IRQ compartida en bhyve, un lector puede apilar varios dispositivos virtio en el mismo slot (distintas funciones), forzándolos a compartir una línea. Esto es avanzado y no es necesario para los laboratorios básicos del capítulo 19.

### El problema de la inanición

Una línea IRQ compartida tiene un posible problema de inanición: un único driver que tarde demasiado en su filtro puede retrasar a todos los demás drivers de la línea. Cada filtro ve el estado de su dispositivo como "sin cambios" durante el tiempo que dura el filtro lento, y los eventos pueden acumularse sin ser detectados.

La disciplina es la misma que cubrió la sección 4: los filtros deben ser rápidos. Decenas o centenares de microsegundos de trabajo real es normalmente el máximo que hace un filtro bien comportado; cualquier cosa más lenta pasa a la tarea. Un filtro que hace trabajo largo no solo priva de servicio a las capas superiores de su propio driver, sino también a todos los demás drivers de la línea.

Con MSI-X (capítulo 20), cada vector tiene su propio `intr_event`, así que el problema de inanición desaparece para los pares de drivers específicos que usan MSI-X. Pero la disciplina sigue aplicando: un filtro que tarda un milisegundo perjudica la latencia de todas las interrupciones posteriores.

### Falsos positivos y gestión defensiva

Una propiedad útil de la comprobación del registro de estado es que es naturalmente tolerante con los falsos positivos del kernel. En ocasiones, los controladores de interrupción notifican una interrupción espuria cuando ningún dispositivo está realmente señalizando (ruido en la línea, una condición de carrera entre el disparo por flanco y el enmascaramiento, una peculiaridad específica de la plataforma). El kernel despacha, el filtro lee INTR_STATUS, está a cero, el filtro devuelve `FILTER_STRAY` y el kernel sigue adelante.

Esto no tiene ningún efecto en el driver. El contador de interrupciones extrañas sube; nada más cambia.

Algunos drivers añaden un mensaje de registro con frecuencia limitada para hacer visibles las interrupciones espurias. Un comportamiento razonable por defecto es registrar solo si la frecuencia supera un umbral:

```c
static struct timeval last_stray_log;
static int stray_rate_limit = 5;  /* messages per second */
if (rv == 0) {
	if (ppsratecheck(&last_stray_log, &stray_rate_limit, 1))
		device_printf(sc->dev, "spurious interrupt\n");
	return (FILTER_STRAY);
}
```

La utilidad `ppsratecheck(9)` limita la frecuencia del mensaje. Sin ella, una línea en tormenta inundaría `dmesg` con mensajes idénticos.

El driver del capítulo 19 no incluye el registro con frecuencia limitada en su filtro de la etapa 2; se añade en un ejercicio de desafío.

### Cuándo el filtro debe gestionar y la tarea no debe ejecutarse

Un experimento mental. Imagina que el filtro reconoce `ERROR` pero no `DATA_AV`. El filtro gestiona `ERROR` (lo reconoce, incrementa el contador) y devuelve `FILTER_HANDLED`. No se encola ninguna tarea. El dispositivo queda satisfecho; la línea deja de señalizar.

Pero `INTR_STATUS.DATA_AV` podría seguir activado, porque el filtro no lo reconoció (el filtro no identificó el bit como perteneciente a una clase que el driver gestiona). En una línea con disparo por nivel, el dispositivo sigue señalizando por `DATA_AV`, se dispara una nueva interrupción y el ciclo se repite.

Esto es una variante del problema de la "tormenta por bit desconocido". La solución es reconocer todos los bits que el driver es capaz de ver, aunque no haga nada con algunos de ellos. Establecer `INTR_MASK` solo con los bits que el driver gestiona es la medida preventiva; reconocer los bits no identificados en el filtro es la medida defensiva.

### Cerrando la sección 6

Las interrupciones compartidas son el caso habitual en PCI heredado y siguen siendo la suposición correcta al escribir para hardware moderno. Un filtro en una línea compartida debe comprobar si la interrupción pertenece a su dispositivo (normalmente leyendo el registro INTR_STATUS del dispositivo), gestionar los bits que reconoce, reconocerlos y devolver `FILTER_STRAY` si no reconoció nada. La disciplina es pequeña en código y grande en fiabilidad: un driver que lo hace bien coexiste con cualquier otro driver bien comportado en su línea, y `RF_SHAREABLE` en la asignación es la única línea de código adicional que necesita.

La sección 7 es la sección de desmontaje. Es breve: `bus_teardown_intr` antes de `bus_release_resource`, drenar el taskqueue antes de liberar cualquier elemento que la tarea use, borrar `INTR_MASK` para que el dispositivo deje de señalizar, y verificar que los contadores tienen sentido. Pero el orden es estricto, y el camino de detach del capítulo 19 se extiende exactamente con estos pasos.



## Sección 7: Limpieza de recursos de interrupción

El camino de attach ganó tres nuevas operaciones (asignar la IRQ, registrar el handler, habilitar las interrupciones en el dispositivo); el camino de detach debe deshacer cada una de ellas en orden estrictamente inverso. La sección 7 es breve porque el patrón ya es familiar; pero el orden importa de formas concretas que las secciones anteriores no abordaron, y un error aquí produce kernel panics que el kernel de depuración detecta muy bien y que resultan muy confusos de diagnosticar.

### El orden obligatorio

De más específico a más general, la secuencia de detach en la etapa 2 del capítulo 19 es:

1. **Rechazar si está ocupado.** `myfirst_is_busy(sc)` devuelve true si el cdev está abierto o hay un comando en curso.
2. **Marcar como ya no conectado** para que los caminos del espacio de usuario rechacen nuevas operaciones.
3. **Destruir el cdev** para que no comience ningún acceso nuevo desde el espacio de usuario.
4. **Deshabilitar las interrupciones en el dispositivo.** Borrar `INTR_MASK` para que el dispositivo deje de señalizar.
5. **Desmontar el handler de interrupción.** `bus_teardown_intr(9)` sobre `irq_res` con la cookie guardada. Tras retornar, el kernel garantiza que el filtro no volverá a ejecutarse.
6. **Drenar el taskqueue.** `taskqueue_drain(9)` espera a que cualquier tarea pendiente complete e impide que se ejecuten nuevas encoladas.
7. **Destruir el taskqueue.** `taskqueue_free(9)` cierra los threads de trabajo.
8. **Detener los callouts de simulación del capítulo 17** si `sc->sim` no es NULL.
9. **Desconectar la simulación del capítulo 17** si está conectada.
10. **Desconectar la capa de hardware** para que `sc->hw` se libere.
11. **Liberar el recurso IRQ** con `bus_release_resource(9)`.
12. **Liberar el BAR** con `bus_release_resource(9)`.
13. **Deinicializar el softc.**

Trece pasos. Cada uno hace una sola cosa. Los peligros están en el orden.

### Por qué deshabilitar en el dispositivo antes de bus_teardown_intr

Borrar `INTR_MASK` antes de desmontar el handler es una medida defensiva. Si desmontáramos el handler primero, una interrupción pendiente en el dispositivo podría dispararse sin handler; el kernel la marcaría como extraña y eventualmente deshabilitaría la línea. Borrar `INTR_MASK` primero detiene al dispositivo para que no señalice, luego el desmontaje elimina el handler, y entre medias no puede dispararse ninguna interrupción.

Para MSI-X (capítulo 20), la lógica es ligeramente distinta porque cada vector es independiente. Pero el principio se transfiere: detener la fuente antes de eliminar el handler.

En hardware real, esta ventana dura microsegundos; una interrupción extraña durante ella es rara. En bhyve, donde la tasa de eventos es baja, en la práctica nunca ocurre. Aun así, los drivers cuidadosos cierran la ventana de todas formas, porque los drivers cuidadosos son los que uno quiere leer en producción.

### Por qué bus_teardown_intr antes de liberar el recurso

`bus_teardown_intr` elimina el handler del driver del `intr_event`. Tras retornar, el kernel garantiza que el filtro no volverá a ejecutarse. Pero el recurso IRQ (el `struct resource *`) sigue siendo válido; el kernel aún no lo ha liberado. `bus_release_resource` es lo que lo libera.

Si liberáramos el recurso primero, la contabilidad interna del kernel en torno al `intr_event` vería un handler registrado contra un recurso que ya no existe. Según el momento, esto produce bien un fallo inmediato durante `bus_release_resource` (el kernel detecta que el handler sigue adjunto), bien un problema diferido más adelante cuando la línea intente dispararse.

El orden seguro es siempre `bus_teardown_intr` primero. La página de manual de `bus_setup_intr(9)` lo deja claro.

### Por qué drenar el taskqueue antes de liberar el softc

El filtro puede haber encolado una tarea que aún no se ha ejecutado. El puntero de función de la tarea se almacena en la `struct task`, y el puntero de argumento es el softc. Si liberáramos el softc antes de que se ejecutara la tarea, la tarea desreferenciaría un puntero liberado y produciría un panic.

`taskqueue_drain(9)` sobre una tarea específica espera a que esa tarea complete e impide que futuras encoladas de esa tarea se ejecuten. Llamar a `taskqueue_drain` sobre `&sc->intr_data_task` es exactamente lo correcto: espera a que la tarea de datos disponibles termine.

Tras retornar `taskqueue_drain`, no hay ninguna ejecución de tarea en curso. El softc puede liberarse con seguridad.

Un error habitual: drenar una sola tarea con `taskqueue_drain(tq, &task)` es diferente de drenar todo el taskqueue con `taskqueue_drain_all(tq)`. Para un driver con varias tareas en el mismo taskqueue, cada tarea necesita su propio drain, o bien `taskqueue_drain_all` las gestiona como grupo.

Para el driver del Capítulo 19 solo hay una tarea, por lo que un único `taskqueue_drain` es suficiente.

### Por qué bus_teardown_intr va antes que taskqueue_drain

El filtro puede encolar una tarea entre el momento en que se borra `INTR_MASK` y el momento en que `bus_teardown_intr` retorna. Si drenamos el taskqueue antes de eliminar el handler, un filtro todavía en ejecución podría encolar una tarea después del drenado, y la garantía de este quedaría anulada.

El orden correcto es: borrar `INTR_MASK` (detiene las nuevas interrupciones), eliminar el handler (impide que el filtro vuelva a ejecutarse), drenar el taskqueue (impide que cualquier tarea encolada previamente llegue a ejecutarse). Cada paso reduce el conjunto de rutas de código que pueden tocar el estado.

### El código de limpieza

Aplicando el orden al detach del Capítulo 19, Etapa 2:

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	if (myfirst_is_busy(sc))
		return (EBUSY);

	sc->pci_attached = false;

	/* Destroy the cdev so no new user-space access starts. */
	if (sc->cdev != NULL) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}

	/* Disable interrupts at the device. */
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);

	/* Tear down the interrupt handler. */
	if (sc->intr_cookie != NULL) {
		bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
		sc->intr_cookie = NULL;
	}

	/* Drain and destroy the interrupt taskqueue. */
	if (sc->intr_tq != NULL) {
		taskqueue_drain(sc->intr_tq, &sc->intr_data_task);
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
	}

	/* Quiesce Chapter 17 callouts (if sim attached). */
	myfirst_quiesce(sc);

	/* Detach Chapter 17 simulation if attached. */
	if (sc->sim != NULL)
		myfirst_sim_detach(sc);

	/* Detach the hardware layer. */
	myfirst_hw_detach(sc);

	/* Release the IRQ resource. */
	if (sc->irq_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
		sc->irq_res = NULL;
	}

	/* Release the BAR. */
	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}

	myfirst_deinit_softc(sc);

	device_printf(dev, "detached\n");
	return (0);
}
```

Trece acciones distintas, cada una sencilla. El código es más largo que en etapas anteriores solo porque cada nueva capacidad añade su propio paso de limpieza.

### Gestión de fallos en attach parcial

La cascada de goto del camino de attach del Apartado 3 tenía etiquetas para cada paso de asignación. Con el handler de interrupciones registrado en la Etapa 2, la cascada crece un paso más:

```c
fail_teardown_intr:
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);
	bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
	sc->intr_cookie = NULL;
fail_release_irq:
	bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
	sc->irq_res = NULL;
fail_hw:
	myfirst_hw_detach(sc);
fail_release_bar:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
```

Cada etiqueta de la cascada deshace el paso que tuvo éxito antes que ella. Un `bus_setup_intr` fallido salta a `fail_release_irq` (omite el teardown porque el handler no quedó registrado). Un `make_dev` fallido (creación del cdev) salta a `fail_teardown_intr` (elimina el handler antes de liberar el IRQ).

El taskqueue se inicializa en `myfirst_init_softc` y se destruye en `myfirst_deinit_softc`, de modo que la cascada no necesita gestionarlo explícitamente; cualquier etiqueta que llegue a `fail_softc` se encargará de limpiar el taskqueue mediante deinit.

### Verificación del teardown

Tras `kldunload myfirst`, el kernel debería quedar en un estado limpio. Comprobaciones concretas:

- `kldstat -v | grep myfirst` no devuelve nada (módulo descargado).
- `devinfo -v | grep myfirst` no devuelve nada (dispositivo desconectado).
- `vmstat -i | grep myfirst` no devuelve nada (evento de interrupción limpiado).
- `vmstat -m | grep myfirst` no devuelve nada o muestra `InUse` en cero (tipo malloc drenado).
- `dmesg | tail` muestra el mensaje de detach y ninguna advertencia ni pánico.

Que cualquiera de estas comprobaciones falle es un bug. El fallo más habitual es que `vmstat -i` muestre una entrada obsoleta; esto suele significar que no se llamó a `bus_teardown_intr`. El segundo más habitual es que `vmstat -m` muestre asignaciones vivas; esto suele significar que se encoló una tarea y no se drenó, o que la simulación quedó conectada sin desconectarse.

### Gestión del caso «el handler se dispara durante el detach»

Vale la pena analizar un caso sutil. Supón que una interrupción real llega a una línea IRQ compartida entre la destrucción del cdev y la escritura a `INTR_MASK`. El dispositivo de otro driver está activando la línea, nuestro filtro se ejecuta (porque la línea es compartida), nuestro filtro lee `INTR_STATUS` (que vale cero en nuestro dispositivo) y retorna `FILTER_STRAY`. No se toca ningún estado, no se encola ninguna tarea.

Supón ahora que la interrupción proviene de nuestro dispositivo. Nuestro `INTR_STATUS` tiene un bit activo. El filtro lo reconoce, lo confirma, encola una tarea y retorna. El encolado ocurre sobre un taskqueue que todavía no ha sido drenado. La tarea se ejecuta más adelante, adquiere `sc->mtx`, lee `DATA_OUT` a través de la capa de hardware (que sigue conectada porque todavía no hemos llamado a `myfirst_hw_detach`). Todo es seguro.

Supón que la interrupción llega después de `INTR_MASK = 0` pero antes de `bus_teardown_intr`. El dispositivo ha dejado de activar los bits que hemos borrado, pero una interrupción ya en vuelo (encolada en el controlador de interrupciones) puede aún ejecutar el filtro. El filtro lee `INTR_STATUS`, ve cero (porque la escritura a la máscara fue anterior al estado interno del dispositivo) y retorna `FILTER_STRAY`. La interrupción se contabiliza como stray; el kernel la ignora.

Supón que la interrupción llega después de `bus_teardown_intr`. El handler ha desaparecido. La contabilidad de interrupciones stray del kernel lo detecta. Tras suficientes strays, el kernel deshabilita la línea. Este es el escenario que el paso `INTR_MASK = 0` está diseñado para evitar: si se borra la máscara primero, no pueden acumularse strays.

Todas las rutas de código son defensivas. Las aserciones del kernel de debug detectan los errores más comunes. Un driver que sigue el orden del Capítulo 19 realiza el teardown de forma fiable y limpia.

### Qué sale mal cuando se omite el teardown

Algunos escenarios que producen síntomas concretos.

**Handler no eliminado, recurso liberado.** `kldunload` llama a `bus_release_resource` sobre el IRQ sin haber llamado antes a `bus_teardown_intr`. El kernel detecta un handler activo sobre un recurso que se está liberando y provoca un pánico con un mensaje similar a "releasing allocated IRQ with active handler". El kernel de debug es fiable en este caso.

**Handler eliminado, taskqueue no drenado.** La tarea se encola en el filtro, la última llamada al filtro ocurre justo antes del teardown, la tarea aún no ha ejecutado. El driver libera `sc` (mediante deinit del softc) y se descarga. El thread trabajador del taskqueue despierta, ejecuta la función de tarea, desreferencia el softc liberado y provoca un pánico por puntero nulo o uso tras liberación. El `WITNESS` o el `MEMGUARD` del kernel de debug pueden detectarlo; si no, el crash se produce en el primer acceso a memoria de la función de tarea.

**Taskqueue drenado, pero no liberado.** `taskqueue_drain` tiene éxito, pero se omite `taskqueue_free`. El thread trabajador del taskqueue sigue ejecutándose (inactivo). Un `vmstat -m` muestra la asignación. No es un bug funcional, pero sí una fuga que se acumula con cada ciclo de carga y descarga.

**Callouts de simulación no desactivados.** Si la simulación del Capítulo 17 está conectada (en un build solo de simulación), sus callouts están en marcha. Sin desactivarlos, se disparan después de que detach haya liberado el bloque de registros y acceden a memoria basura. La detección por parte de `WITNESS` o `MEMGUARD` varía según el impacto; a veces el síntoma es una desreferencia de puntero nulo.

**INTR_MASK no borrado.** Las interrupciones reales llegan después de que empiece el detach. El filtro las gestiona (brevemente, hasta el teardown); después del teardown, son strays que el kernel termina deshabilitando en la línea. El estado deshabilitado de la línea es visible en `vmstat -i` (contador stray creciente) y en `dmesg` (advertencias del kernel).

Cada uno de estos problemas se resuelve corrigiendo el orden del teardown. El código del Capítulo 19 está diseñado correctamente; los riesgos descritos son para el lector que modifique el orden.

### Prueba de cordura del teardown

Una prueba de cordura sencilla que puedes ejecutar después de escribir el código de detach:

```sh
# Load.
sudo kldload ./myfirst.ko

# Fire a few simulated interrupts, make sure tasks run.
for i in 1 2 3 4 5; do
    sudo sysctl dev.myfirst.0.intr_simulate=1
done
sleep 1
sysctl dev.myfirst.0.intr_task_invocations  # should be 5

# Unload.
sudo kldunload myfirst

# Check nothing leaked.
vmstat -m | grep myfirst  # should be empty
devinfo -v | grep myfirst   # should be empty
vmstat -i | grep myfirst    # should be empty
```

Ejecutar esta secuencia en bucle (veinte iteraciones en un bucle de shell) es una prueba de regresión razonable: cualquier fuga se acumula, cualquier crash se manifiesta y cualquier patrón de fallo queda al descubierto.

### Cerrando el Apartado 7

Limpiar los recursos de interrupción son seis operaciones pequeñas en el camino de detach: deshabilitar `INTR_MASK`, eliminar el handler, drenar y liberar el taskqueue, desconectar la capa de hardware, liberar el IRQ y liberar el BAR. Cada operación deshace exactamente una operación del camino de attach. El orden es el inverso del attach. El drenado del taskqueue es una preocupación nueva e importante, específica de los drivers con filtro más tarea; un driver que lo omite tiene un bug de uso tras liberación esperando el próximo ciclo de carga y descarga.

El Apartado 8 es la sección de limpieza de código: separar el código de interrupciones en su propio archivo, subir la versión a `1.2-intr`, escribir `INTERRUPTS.md` y ejecutar el pase de regresión. El driver es funcionalmente completo tras el Apartado 7; el Apartado 8 lo hace mantenible.



## Apartado 8: Refactorización y versionado del driver con soporte de interrupciones

El handler de interrupciones funciona. El Apartado 8 es la sección de limpieza de código. Separa el código de interrupciones en su propio archivo, actualiza los metadatos del módulo, añade un nuevo documento `INTERRUPTS.md`, introduce un pequeño conjunto de macros CSR para el contexto de interrupción de modo que el filtro pueda acceder a los registros sin las macros que aseveran el lock, sube la versión a `1.2-intr` y ejecuta el pase de regresión.

Un lector que haya llegado hasta aquí puede sentir de nuevo la tentación de saltarse este apartado. Es la misma tentación ante la que el Apartado 8 del Capítulo 18 ya advirtió, y la misma negativa: un driver cuyo código de interrupciones está mezclado en el archivo PCI, cuyo filtro usa `bus_read_4` en bruto de forma ad hoc y cuya configuración del taskqueue está repartida entre tres archivos resulta muy difícil de extender. El Capítulo 20 añade MSI y MSI-X; el Capítulo 21 añade DMA. Ambos se apoyan en el código de interrupciones del Capítulo 19. Una estructura limpia ahora ahorra esfuerzo en ambos.

### La distribución final de archivos

Al final del Capítulo 19, el driver se compone de los siguientes archivos:

```text
myfirst.c         - Main driver: softc, cdev, module events, data path.
myfirst.h         - Shared declarations: softc, lock macros, prototypes.
myfirst_hw.c      - Ch16 hardware access layer: CSR_* accessors,
                     access log, sysctl handlers.
myfirst_hw_pci.c  - Ch18 hardware layer extension: myfirst_hw_attach_pci.
myfirst_hw.h      - Register map and accessor declarations.
myfirst_sim.c     - Ch17 simulation backend.
myfirst_sim.h     - Ch17 simulation interface.
myfirst_pci.c     - Ch18 PCI attach: probe, attach, detach,
                     DRIVER_MODULE, MODULE_DEPEND, ID table.
myfirst_pci.h     - Ch18 PCI declarations.
myfirst_intr.c    - Ch19 interrupt handler: filter, task, setup, teardown.
myfirst_intr.h    - Ch19 interrupt interface.
myfirst_sync.h    - Part 3 synchronisation primitives.
cbuf.c / cbuf.h   - Ch10 circular buffer.
Makefile          - kmod build.
HARDWARE.md       - Ch16/17 register map.
LOCKING.md        - Ch15 onward lock discipline.
SIMULATION.md     - Ch17 simulation.
PCI.md            - Ch18 PCI support.
INTERRUPTS.md     - Ch19 interrupt handling.
```

`myfirst_intr.c` y `myfirst_intr.h` son nuevos. `INTERRUPTS.md` es nuevo. Todos los demás archivos existían antes o se han extendido ligeramente (el softc ha ganado campos; el attach PCI delega en `myfirst_intr.c`).

La regla de oro se mantiene: cada archivo tiene una responsabilidad. `myfirst_intr.c` es responsable del handler de interrupciones, la tarea diferida y el sysctl de interrupción simulada. `myfirst_pci.c` es responsable del attach PCI, pero delega la configuración y el teardown de interrupciones en las funciones exportadas por `myfirst_intr.c`.

### El Makefile final

```makefile
# Makefile for the Chapter 19 myfirst driver.

KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.2-intr\"

# CFLAGS+= -DMYFIRST_SIMULATION_ONLY
# CFLAGS+= -DMYFIRST_PCI_ONLY

.include <bsd.kmod.mk>
```

Un archivo fuente adicional en la lista de SRCS, la cadena de versión actualizada y el resto sin cambios.

### La cadena de versión

De `1.1-pci` a `1.2-intr`. La subida refleja que el driver ha adquirido una nueva capacidad significativa (gestión de interrupciones) sin cambiar ninguna interfaz visible para el usuario (el cdev sigue haciendo lo mismo que hacía). Una subida de versión menor es adecuada.

Los capítulos posteriores continúan la progresión: `1.3-msi` tras el trabajo con MSI y MSI-X del Capítulo 20; `1.4-dma` tras añadir DMA en los Capítulos 20 y 21. Cada versión menor refleja la adición de una capacidad significativa.

### La cabecera myfirst_intr.h

La cabecera exporta la interfaz pública de la capa de interrupciones al resto del driver:

```c
#ifndef _MYFIRST_INTR_H_
#define _MYFIRST_INTR_H_

#include <sys/types.h>
#include <sys/taskqueue.h>

struct myfirst_softc;

/* Interrupt setup and teardown, called from the PCI attach path. */
int  myfirst_intr_setup(struct myfirst_softc *sc);
void myfirst_intr_teardown(struct myfirst_softc *sc);

/* Register sysctl nodes specific to the interrupt layer. */
void myfirst_intr_add_sysctls(struct myfirst_softc *sc);

/* Interrupt-context accessor macros. These do not acquire sc->mtx
 * and therefore are safe in the filter. They are NOT a replacement
 * for CSR_READ_4 / CSR_WRITE_4 in other contexts. */
#define ICSR_READ_4(sc, off) \
	bus_read_4((sc)->bar_res, (off))
#define ICSR_WRITE_4(sc, off, val) \
	bus_write_4((sc)->bar_res, (off), (val))

#endif /* _MYFIRST_INTR_H_ */
```

La API pública son tres funciones (`myfirst_intr_setup`, `myfirst_intr_teardown`, `myfirst_intr_add_sysctls`) y dos macros de acceso (`ICSR_READ_4`, `ICSR_WRITE_4`). El prefijo «I» corresponde a «interrupt-context» (contexto de interrupción); estas macros no adquieren `sc->mtx`, por lo que son seguras dentro del filtro.

### El archivo myfirst_intr.c

El archivo completo está en el árbol de ejemplos del libro; aquí se muestra la estructura central:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_intr.h"

/* Deferred task for data-available events. */
static void myfirst_intr_data_task_fn(void *arg, int npending);

/* The filter handler. Exported so the simulated-interrupt sysctl can
 * call it directly. */
int myfirst_intr_filter(void *arg);

int
myfirst_intr_setup(struct myfirst_softc *sc)
{
	int error;

	TASK_INIT(&sc->intr_data_task, 0, myfirst_intr_data_task_fn, sc);
	sc->intr_tq = taskqueue_create("myfirst_intr", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->intr_tq);
	taskqueue_start_threads(&sc->intr_tq, 1, PI_NET,
	    "myfirst intr taskq");

	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &sc->irq_rid, RF_SHAREABLE | RF_ACTIVE);
	if (sc->irq_res == NULL)
		return (ENXIO);

	error = bus_setup_intr(sc->dev, sc->irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    myfirst_intr_filter, NULL, sc,
	    &sc->intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
		sc->irq_res = NULL;
		return (error);
	}

	bus_describe_intr(sc->dev, sc->irq_res, sc->intr_cookie, "legacy");

	/* Enable the interrupts we care about at the device. */
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
		    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR |
		    MYFIRST_INTR_COMPLETE);
	MYFIRST_UNLOCK(sc);

	return (0);
}

void
myfirst_intr_teardown(struct myfirst_softc *sc)
{
	/* Disable interrupts at the device. */
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);

	/* Tear down the handler. */
	if (sc->intr_cookie != NULL) {
		bus_teardown_intr(sc->dev, sc->irq_res, sc->intr_cookie);
		sc->intr_cookie = NULL;
	}

	/* Drain and destroy the taskqueue. */
	if (sc->intr_tq != NULL) {
		taskqueue_drain(sc->intr_tq, &sc->intr_data_task);
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
	}

	/* Release the IRQ resource. */
	if (sc->irq_res != NULL) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
		sc->irq_res = NULL;
	}
}

int
myfirst_intr_filter(void *arg)
{
	/* ... as in Section 4 ... */
}

static void
myfirst_intr_data_task_fn(void *arg, int npending)
{
	/* ... as in Section 4 ... */
}

void
myfirst_intr_add_sysctls(struct myfirst_softc *sc)
{
	/* ... counters and intr_simulate sysctl ... */
}
```

El archivo tiene unas 250 líneas en la Etapa 4. `myfirst_pci.c` se reduce correspondientemente: la asignación y configuración de interrupciones se trasladan fuera de él.

### El attach PCI refactorizado

Tras mover el código de interrupciones a `myfirst_intr.c`, `myfirst_pci_attach` queda así:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;
	sc->unit = device_get_unit(dev);
	error = myfirst_init_softc(sc);
	if (error != 0)
		return (error);

	/* Step 1: allocate BAR 0. */
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		error = ENXIO;
		goto fail_softc;
	}

	/* Step 2: attach the hardware layer. */
	error = myfirst_hw_attach_pci(sc, sc->bar_res,
	    rman_get_size(sc->bar_res));
	if (error != 0)
		goto fail_release_bar;

	/* Step 3: set up interrupts. */
	error = myfirst_intr_setup(sc);
	if (error != 0) {
		device_printf(dev, "interrupt setup failed (%d)\n", error);
		goto fail_hw;
	}

	/* Step 4: create cdev. */
	sc->cdev = make_dev(&myfirst_cdevsw, sc->unit, UID_ROOT,
	    GID_WHEEL, 0600, "myfirst%d", sc->unit);
	if (sc->cdev == NULL) {
		error = ENXIO;
		goto fail_intr;
	}
	sc->cdev->si_drv1 = sc;

	/* Step 5: register sysctls. */
	myfirst_intr_add_sysctls(sc);

	sc->pci_attached = true;
	return (0);

fail_intr:
	myfirst_intr_teardown(sc);
fail_hw:
	myfirst_hw_detach(sc);
fail_release_bar:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
}
```

El attach PCI es más corto; los detalles de interrupción quedan ocultos tras `myfirst_intr_setup`. La cascada de goto tiene cuatro etiquetas en lugar de seis (las etiquetas específicas de interrupción se han trasladado a `myfirst_intr.c`).

### El detach refactorizado

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	if (myfirst_is_busy(sc))
		return (EBUSY);

	sc->pci_attached = false;

	if (sc->cdev != NULL) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}

	myfirst_intr_teardown(sc);

	if (sc->sim != NULL)
		myfirst_sim_detach(sc);

	myfirst_hw_detach(sc);

	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}

	myfirst_deinit_softc(sc);

	device_printf(dev, "detached\n");
	return (0);
}
```

El teardown específico de interrupciones es una sola llamada a `myfirst_intr_teardown`, que encapsula los pasos de borrado de máscara, teardown, drenado y liberación del recurso.

### El documento INTERRUPTS.md

El nuevo documento reside junto al código fuente del driver. Su función es describir el manejo de interrupciones del driver a un futuro lector sin obligarle a leer `myfirst_intr.c`:

```markdown
# Interrupt Handling in the myfirst Driver

## Allocation and Setup

The driver allocates a single legacy PCI IRQ through
`bus_alloc_resource_any(9)` with `SYS_RES_IRQ`, `rid = 0`,
`RF_SHAREABLE | RF_ACTIVE`. The filter handler is registered through
`bus_setup_intr(9)` with `INTR_TYPE_MISC | INTR_MPSAFE`. A taskqueue
named "myfirst_intr" is created with one worker thread at `PI_NET`
priority.

On successful setup, `INTR_MASK` is written with
`DATA_AV | ERROR | COMPLETE` so the device will assert the line for
those three event classes.

## Filter Handler

`myfirst_intr_filter(sc)` reads `INTR_STATUS`. If zero, it returns
`FILTER_STRAY` (shared-IRQ defence). Otherwise it inspects each of
the three recognised bits, increments a per-bit counter atomically,
writes the bit back to `INTR_STATUS` to acknowledge the device, and
(for `DATA_AV`) enqueues `intr_data_task` on the taskqueue.

The filter returns `FILTER_HANDLED` if any bit was recognised, or
`FILTER_STRAY` otherwise.

## Deferred Task

`myfirst_intr_data_task_fn(sc, npending)` runs in thread context on
the taskqueue's worker thread. It acquires `sc->mtx`, reads
`DATA_OUT`, stores the value in `sc->intr_last_data`, broadcasts
`sc->data_cv` to wake pending readers, and releases the lock.

## Simulated Interrupt sysctl

`dev.myfirst.N.intr_simulate` is write-only; writing a bitmask to it
sets the corresponding bits in `INTR_STATUS` and invokes
`myfirst_intr_filter` directly. This exercises the full pipeline
without needing real IRQ events.

## Teardown

`myfirst_intr_teardown(sc)` runs during detach. It clears
`INTR_MASK`, calls `bus_teardown_intr`, drains and destroys the
taskqueue, and releases the IRQ resource. The order is strict:
mask-clear before teardown (so strays do not accumulate), teardown
before drain (so no new task enqueues happen), drain before free
(so no task runs against freed state).

## Interrupt-Context Accessor Macros

Since the filter runs in primary interrupt context, it cannot take
`sc->mtx`. Two macros in `myfirst_intr.h` hide the raw
`bus_read_4`/`bus_write_4` calls without asserting any lock: `ICSR_READ_4`
and `ICSR_WRITE_4`. Use them only in contexts where a sleep lock
would be illegal.

## Known Limitations

- Only the legacy PCI INTx line is handled. MSI and MSI-X are
  Chapter 20.
- The filter coalesces per-bit counters via atomic ops; the task
  runs at a single priority. Per-queue or per-priority designs are
  later-chapter topics.
- Interrupt storm detection is managed by the kernel
  (`hw.intr_storm_threshold`); the driver does not implement its
  own storm mitigation.
- Chapter 17 simulation callouts are not active on the PCI build;
  the simulated-interrupt sysctl is the way to drive the pipeline
  on a bhyve lab target.
```

Cinco minutos de lectura; una imagen clara de la forma de la capa de interrupciones.

### El pase de regresión

La regresión del Capítulo 19 es un superconjunto de la del Capítulo 18:

1. Compilación limpia. `make` tiene éxito; sin advertencias.
2. Carga. `kldload ./myfirst.ko` tiene éxito; `dmesg` muestra la secuencia de attach.
3. Conexión a un dispositivo PCI real. `devinfo -v` muestra el BAR y el IRQ.
4. Sin advertencia `[GIANT-LOCKED]`.
5. `vmstat -i | grep myfirst` muestra el `intr_event`.
6. `sysctl dev.myfirst.0.intr_count` comienza en cero.
7. Interrupción simulada. `sudo sysctl dev.myfirst.0.intr_simulate=1`; los contadores se incrementan; la tarea se ejecuta.
8. Prueba de frecuencia. Establece `intr_sim_period_ms` a 100; comprueba los contadores tras 10 segundos.
9. Desconexión. `devctl detach myfirst0`; `dmesg` muestra un detach limpio.
10. Reconexión. `devctl attach pci0:0:4:0`; se ejecuta el ciclo completo de attach.
11. Descarga. `kldunload myfirst`; `vmstat -m | grep myfirst` muestra cero asignaciones vivas; `vmstat -i | grep myfirst` no devuelve nada.

Ejecutar la regresión completa lleva uno o dos minutos por iteración. Un job de CI que la ejecute veinte veces en bucle es el tipo de salvaguarda que detecta regresiones introducidas por las extensiones de los Capítulos 20 y 21.

### Lo que aportó la refactorización

El código del Capítulo 19 tiene un archivo menos de los que habría tenido sin la refactorización; existe un nuevo documento, y el número de versión ha avanzado una posición. El driver es reconociblemente FreeBSD, estructuralmente paralelo a un driver de producción en `/usr/src/sys/dev/`, y está listo para incorporar la maquinaria MSI-X del Capítulo 20 y la maquinaria DMA del Capítulo 21 sin necesidad de otra reorganización.

### Cerrando la sección 8

La refactorización sigue la misma forma que establecieron los capítulos 16 a 18. Un nuevo archivo asume la nueva responsabilidad. Un nuevo header exporta la interfaz pública. Un nuevo documento explica el comportamiento. La versión sube; la regresión pasa; el driver sigue siendo mantenible. Nada dramático; un poco de mantenimiento; una base de código limpia sobre la que seguir construyendo.

El cuerpo instructivo del capítulo 19 está completo. A continuación vienen los laboratorios, los ejercicios de desafío, la solución de problemas, un cierre y el puente hacia el capítulo 20.



## Lectura conjunta de un driver real: el camino de interrupciones de mgb(4)

Antes de los laboratorios, un recorrido breve por un driver real que usa el mismo patrón filter más tarea que enseña el capítulo 19. `/usr/src/sys/dev/mgb/if_mgb.c` es el driver de FreeBSD para los controladores Ethernet gigabit LAN743x de Microchip. Es legible, es de calidad de producción, y su manejo de interrupciones está aproximadamente al nivel de complejidad que cubre el vocabulario del capítulo 19.

Esta sección recorre las partes relevantes para interrupciones de `mgb_legacy_intr` y del código de configuración, e indica dónde encaja cada pieza con un concepto del capítulo 19.

### El filter de IRQ legacy

El manejador filter para el camino de IRQ legacy de `mgb(4)`:

```c
int
mgb_legacy_intr(void *xsc)
{
	struct mgb_softc *sc;
	if_softc_ctx_t scctx;
	uint32_t intr_sts, intr_en;
	int qidx;

	sc = xsc;
	scctx = iflib_get_softc_ctx(sc->ctx);

	intr_sts = CSR_READ_REG(sc, MGB_INTR_STS);
	intr_en = CSR_READ_REG(sc, MGB_INTR_ENBL_SET);
	intr_sts &= intr_en;

	/* TODO: shouldn't continue if suspended */
	if ((intr_sts & MGB_INTR_STS_ANY) == 0)
		return (FILTER_STRAY);

	if ((intr_sts &  MGB_INTR_STS_TEST) != 0) {
		sc->isr_test_flag = true;
		CSR_WRITE_REG(sc, MGB_INTR_STS, MGB_INTR_STS_TEST);
		return (FILTER_HANDLED);
	}
	if ((intr_sts & MGB_INTR_STS_RX_ANY) != 0) {
		for (qidx = 0; qidx < scctx->isc_nrxqsets; qidx++) {
			if ((intr_sts & MGB_INTR_STS_RX(qidx))){
				iflib_rx_intr_deferred(sc->ctx, qidx);
			}
		}
		return (FILTER_HANDLED);
	}
	if ((intr_sts & MGB_INTR_STS_TX_ANY) != 0) {
		for (qidx = 0; qidx < scctx->isc_ntxqsets; qidx++) {
			if ((intr_sts & MGB_INTR_STS_RX(qidx))) {
				CSR_WRITE_REG(sc, MGB_INTR_ENBL_CLR,
				    MGB_INTR_STS_TX(qidx));
				CSR_WRITE_REG(sc, MGB_INTR_STS,
				    MGB_INTR_STS_TX(qidx));
				iflib_tx_intr_deferred(sc->ctx, qidx);
			}
		}
		return (FILTER_HANDLED);
	}

	return (FILTER_SCHEDULE_THREAD);
}
```

Recorrámoslo. El filter lee dos registros (`INTR_STS` y `INTR_ENBL_SET`), los combina con AND para obtener el subconjunto de interrupciones pendientes que están habilitadas, y comprueba si hay algún bit activado. Si no hay ninguno, devuelve `FILTER_STRAY`, la disciplina del capítulo 19 para IRQs compartidas.

Para cada clase de interrupción (prueba, recepción, transmisión), el filter reconoce los bits correspondientes en `INTR_STS` escribiéndolos de vuelta y programa el procesamiento diferido. `iflib_rx_intr_deferred` es la forma que tiene el framework iflib de programar el trabajo de la cola de recepción; conceptualmente es equivalente al `taskqueue_enqueue` del capítulo 19.

Hay una línea que merece atención: el manejador de interrupción de prueba escribe en `INTR_STS` pero también activa un flag (`sc->isr_test_flag = true`). Esta es la forma que tiene el driver de señalizar al código del espacio de usuario (a través de un sysctl o un ioctl) que se ha disparado una interrupción de prueba. El equivalente del capítulo 19 es el contador `intr_count`.

El último retorno es `FILTER_SCHEDULE_THREAD`. Este se activa si ninguna de las clases de bits específicas coincidió pero sí lo hizo `MGB_INTR_STS_ANY`. El ithread gestiona el caso residual. El driver del capítulo 19 no tiene esta caída particular porque no tiene un ithread registrado; `mgb(4)` sí lo tiene.

### Lo que enseña el filter de mgb

Tres lecciones son directamente aplicables al filter del capítulo 19:

1. **Lectura con máscara AND.** `intr_sts & intr_en` garantiza que el filter solo informa de interrupciones que estaban realmente habilitadas. Un dispositivo puede informar internamente de eventos que el driver ha enmascarado; el AND los filtra.
2. **Reconocimiento bit a bit.** Cada clase de bits se reconoce individualmente escribiendo de vuelta los bits específicos. El filter no escribe `0xffffffff`; escribe únicamente los bits que ha gestionado.
3. **Diferir el trabajo por cola.** Cada cola de recepción y de transmisión tiene su propio camino diferido. El driver más sencillo del capítulo 19 tiene una sola tarea; el driver multicola de `mgb(4)` tiene muchas.

### Configuración de interrupciones en mgb

Buscar `bus_setup_intr` en `if_mgb.c` muestra varios puntos de llamada, uno para el camino de IRQ legacy y uno para cada vector MSI-X:

```c
if (bus_setup_intr(sc->dev, sc->irq[0], INTR_TYPE_NET | INTR_MPSAFE,
    mgb_legacy_intr, NULL, sc, &sc->irq_tag[0]) != 0) {
	/* ... */
}
```

El patrón es exactamente el del capítulo 19: manejador filter, sin ithread, `INTR_MPSAFE`, softc como argumento, cookie devuelta. La única diferencia es `INTR_TYPE_NET` en lugar de `INTR_TYPE_MISC`, ya que el driver está destinado a redes.

### Desmontaje de interrupciones en mgb

El patrón de desmontaje está distribuido entre los helpers de `iflib(9)`, que gestionan el vaciado y la liberación en nombre del driver. Un driver a medida fuera del framework iflib realiza el desmontaje explícitamente; el driver del capítulo 19 lo hace explícitamente.

### Lo que enseña este recorrido

El camino de interrupciones de `mgb(4)` no es un juguete. Es una implementación de calidad de producción del mismo patrón que sigue el driver del capítulo 19. Un lector que pueda leer `mgb_legacy_intr` con comprensión habrá interiorizado el vocabulario del capítulo 19. El archivo está disponible libremente; leer el código que lo rodea (el camino attach, el ithread, la integración con iflib) profundiza aún más la comprensión.

Vale la pena leer después de `mgb(4)`: `/usr/src/sys/dev/e1000/em_txrx.c` para los patrones multicola MSI-X (material del capítulo 20), `/usr/src/sys/dev/usb/controller/xhci_pci.c` para el camino de interrupciones de un controlador USB host (capítulo 21 y posteriores), y `/usr/src/sys/dev/ahci/ahci_pci.c` para el camino de interrupciones de un controlador de almacenamiento.



## Una mirada más profunda al contexto de interrupción

La sección 2 enumeró lo que puede y no puede ocurrir en un filter. Esta sección va un nivel más allá y explica por qué existe cada regla. Un lector que comprende el porqué puede razonar sobre restricciones desconocidas (nuevos locks, nuevas APIs del kernel) y predecir si son seguras en un filter.

### La situación de la pila

Cuando la CPU atiende una interrupción, guarda los registros del thread interrumpido y cambia a una pila de interrupción del kernel. La pila de interrupción es pequeña (unos pocos KB, dependiendo de la plataforma), es por CPU y se comparte entre todas las interrupciones de esa CPU. El filter se ejecuta en esta pila.

Dos implicaciones:

Primera: el filter dispone de un espacio de pila limitado. Un filter que asigne un array grande en la pila (cientos de bytes o más) puede desbordar la pila de interrupción. El síntoma suele ser un pánico, a veces una corrupción silenciosa de lo que haya junto a la pila en memoria. La regla es: los filtros tienen un presupuesto de pila pequeño. Los arrays grandes pertenecen a la tarea.

Segunda: la pila se comparte entre interrupciones. Un filter que durmiera (hipotéticamente, aunque no puede) dejaría la pila ocupada; otras interrupciones de la misma CPU no podrían reutilizarla. Aunque el sueño estuviera permitido, no sería gratuito. La restricción del tamaño de pila es una de las razones por las que el filter debe ser breve.

### Por qué no hay sleep locks

Un sleep mutex (el `mtx(9)` por defecto) puede bloquearse: si otro thread posee el mutex, `mtx_lock` pone a dormir al thread que llama en el mutex. En el contexto del filter:

- No hay ningún thread que llame en el sentido habitual. El thread interrumpido fue suspendido a mitad de una instrucción; el filter es una excursión en el lado del kernel sobre la CPU.
- Dormir desde la interrupción bloquearía la CPU: la pila de interrupción está ocupada, ninguna otra interrupción puede ejecutarse en esa CPU, y el planificador no puede fácilmente programar otro thread sin un estado válido del kernel.

El kernel podría, en principio, gestionar esto (algunos kernels lo hacen). El diseño de FreeBSD es prohibirlo. La prohibición la hace cumplir `WITNESS` en kernels de depuración: cualquier intento de adquirir un lock que admita sueño en contexto de interrupción produce un pánico inmediato.

Los spin mutexes (mutexes `MTX_SPIN`) son seguros porque no duermen; simplemente giran en espera. Un filter que adquiera un spin mutex es perfectamente válido.

### Por qué no existe `malloc` con espera

`malloc(M_WAITOK)` llama al asignador de páginas de la VM y puede dormir si el sistema tiene poca memoria. El mismo problema que con los locks: el llamador no puede ser suspendido. `malloc(M_NOWAIT)` es la alternativa; puede fallar, pero nunca duerme.

En el filter, las únicas opciones seguras son `M_NOWAIT`, las zonas `UMA` (que tienen sus propios asignadores acotados), o los buffers preasignados. El driver del capítulo 19 no asigna memoria en el filter en absoluto; toda la memoria que el filter necesita está en el softc, preasignada durante el attach.

### Por qué no hay variables de condición

`cv_wait` y `cv_timedwait` duermen. El filter no puede dormir. `cv_signal` y `cv_broadcast` no duermen, pero en la mayoría de los usos adquieren internamente un sleep mutex; un filter que las use debe tener cuidado. La tarea del capítulo 19 gestiona el `cv_broadcast`; el filter solo encola la tarea.

### Por qué el filter no puede reentrar en sí mismo

El despachador de interrupciones del kernel deshabilita nuevas interrupciones en la CPU mientras el filter está en ejecución (en la mayoría de las plataformas; algunas arquitecturas usan niveles de prioridad en su lugar). Esto significa que el filter no puede dispararse recursivamente a sí mismo, aunque su dispositivo afirme la línea durante la ejecución. Cualquier afirmación de ese tipo se encola y se dispara después de que el filter retorne.

Una consecuencia: el filter no necesita protección interna contra reentrancia. Un simple `sc->intr_count++` es seguro desde la perspectiva del filter frente a llamadas simultáneas al filter en la misma CPU. Puede seguir habiendo una condición de carrera con otro código (la tarea, las lecturas desde el espacio de usuario), por eso se usa la operación atómica, pero el filter no compite consigo mismo.

### Por qué las operaciones atómicas son seguras

Las operaciones atómicas en FreeBSD se implementan como instrucciones de CPU que son, por definición, atómicas. No toman un lock; no duermen; no bloquean. Son seguras en cualquier contexto, incluido el filter.

El capítulo 19 usa `atomic_add_64` de forma intensiva: para el contador de interrupciones, para cada contador por bit y para los contadores de invocaciones de la tarea. Las operaciones son baratas (unos pocos ciclos) y predecibles (no interviene la planificación).

### Por qué el ithread tiene más libertad

El ithread se ejecuta en contexto de thread. Dispone de:

- Su propia pila (la pila de thread de kernel normal, mucho más grande que la pila de interrupción).
- Su propia prioridad de planificación (elevada, pero sigue siendo un thread normal).
- La capacidad de dormir si el planificador lo decide.

Sus restricciones son las reglas habituales del contexto de thread: mantener los sleep locks en orden (para evitar deadlocks), evitar sostener un lock mientras se llama a código sin límites, usar `M_WAITOK` si la asignación podría fallar de otro modo, etcétera. Se aplican las disciplinas de los capítulos 13 y 15.

La prioridad elevada del ithread significa que no debe bloquearse durante periodos arbitrariamente largos. Bloquearse durante microsegundos (una breve contención de mutex) es aceptable; bloquearse durante segundos deja sin recursos a todos los demás ithreads del sistema.

### Por qué los threads de taskqueue tienen aún más libertad

El thread de trabajo de un taskqueue es un thread de kernel normal, generalmente a prioridad normal (o a la prioridad que el driver especificó en `taskqueue_start_threads`). Puede dormir, puede bloquearse en cualquier sleep lock y puede asignar memoria sin restricciones. Es el más flexible de los tres contextos.

La contrapartida es que el trabajo del taskqueue es menos oportuno que el trabajo del ithread. El thread de trabajo del taskqueue puede no ejecutarse de inmediato; decide el planificador. Para el trabajo crítico en cuanto a latencia, el ithread es mejor; para el trabajo en volumen, el taskqueue es más sencillo.

El driver del capítulo 19 usa el taskqueue porque el trabajo en volumen que realiza (leer `DATA_OUT`, actualizar el softc, difundir una variable de condición) no es crítico en cuanto a latencia. Los drivers de los capítulos 20 y 21 pueden elegir ithreads o taskqueues de forma diferente en función de su carga de trabajo.

### Cómo interactúa el contexto de interrupción con el locking

La disciplina de locking introducida en la parte 3 sigue siendo aplicable, con un añadido: saber qué locks puedes tomar en cada contexto.

**Contexto del filter.** Solo spin locks, operaciones atómicas y algoritmos sin lock. No hay mutexes que admitan sueño, sx locks, rwlocks, ni `mtx` inicializado sin `MTX_SPIN`.

**Contexto del ithread.** Todos los tipos de lock. Respeta el orden de locking del proyecto tal como se define en `LOCKING.md`.

**Contexto del worker de taskqueue.** Todos los tipos de lock. Respeta el orden de locking del proyecto. Puede dormir de forma arbitraria si es necesario, aunque el autor del driver no debería hacerlo.

**Contexto de thread en general (open/read/write/ioctl de cdev, manejadores sysctl).** Todos los tipos de lock. Respeta el orden de locking del proyecto.

Para el driver del capítulo 19, el filter no toma ningún lock (usa operaciones atómicas), la tarea toma `sc->mtx` (un sleep mutex) a través de `MYFIRST_LOCK`, y los manejadores sysctl también toman `sc->mtx`. La disciplina se mantiene.

### La nota al pie sobre "Giant"

Las versiones antiguas de BSD usaban un único lock global llamado Giant para serializar todo el kernel. Cuando FreeBSD introdujo SMPng (locking de grano fino) a finales de los años noventa y principios de los dos mil, la mayor parte del kernel fue convertida, pero algunos caminos legacy siguen manteniendo Giant. Los drivers que no establecen `INTR_MPSAFE` se envuelven automáticamente con la adquisición de Giant en torno a sus manejadores; `WITNESS` puede quejarse de problemas de orden de locking que involucren a Giant.

El driver del capítulo 19 establece `INTR_MPSAFE` y no toca Giant. Las convenciones modernas de FreeBSD para drivers deprecan Giant en el código del driver. Esta nota existe porque un lector que busque "Giant" en `kern_intr.c` encontrará referencias; son artefactos de compatibilidad con versiones anteriores.



## Una mirada más profunda a la afinidad de CPU para interrupciones

Un breve apéndice sobre la afinidad de interrupciones, que la Sección 2 introdujo de forma somera. El tratamiento en profundidad corresponde al Capítulo 20 (cuando se dispone de varios vectores MSI-X) y a los capítulos posteriores dedicados a la escalabilidad; la cobertura del Capítulo 19 es únicamente un punto de partida.

### Qué significa la afinidad

La afinidad de interrupciones es el conjunto de CPUs en el que una interrupción puede dispararse. En un sistema con una sola CPU, la afinidad es trivial (una CPU). En sistemas con varias CPUs, la afinidad se vuelve interesante: enrutar una interrupción hacia una CPU concreta (en lugar de dejar que lo decida el controlador de interrupciones) puede mejorar la localidad de caché, reducir el tráfico entre sockets y alinear el manejo de interrupciones con la ubicación de los threads.

En x86, el I/O APIC dispone de un campo de destino programable por IRQ; el kernel lo usa para enrutar los IRQs. En arm64, el GIC ofrece facilidades similares. `bus_bind_intr(9)` de FreeBSD es la API portable que configura la afinidad para un recurso IRQ concreto.

### Comportamiento por defecto

Sin una vinculación explícita, FreeBSD distribuye las interrupciones entre las CPUs mediante un algoritmo round-robin o específico de la plataforma. Para un driver de una sola interrupción como el del Capítulo 19, esto significa habitualmente que la interrupción se dispara en la CPU que el kernel eligió durante el boot. La afinidad actual es visible mediante `cpuset -g -x <irq>`; el desglose por CPU de los disparos de un IRQ determinado no forma parte de la salida predeterminada de `vmstat -i` (que agrega todos los disparos del `intr_event` en un único contador), pero puede reconstruirse con herramientas del kernel cuando la plataforma lo permite.

Para muchos drivers, el valor por defecto es suficiente. La tasa de interrupciones es lo bastante baja como para que la afinidad no importe, o el trabajo es tan breve que los costes de mover trabajo entre CPUs son despreciables. El driver del Capítulo 19 se encuentra en esta categoría.

### Cuándo importa la afinidad

Tres escenarios en los que el autor de un driver desea una afinidad explícita:

1. **Tasa de interrupciones elevada.** Una NIC que gestiona diez gigabits de tráfico dispara decenas de miles de interrupciones por segundo. El coste de mover el trabajo de interrupción entre CPUs se convierte en un gasto real. Vincular el vector MSI-X de cada cola de recepción a una CPU concreta mantiene sus líneas de caché calientes.
2. **Localidad NUMA.** En sistemas multisocket, el root complex PCIe del dispositivo está conectado físicamente a un socket concreto. Las interrupciones de ese dispositivo son más baratas de gestionar en CPUs del mismo nodo NUMA que el root complex. La ubicación influye tanto en la latencia como en el rendimiento.
3. **Restricciones de tiempo real.** Un sistema que necesita respuesta de baja latencia en CPUs concretas (para aplicaciones de tiempo real) puede anclar las interrupciones de mantenimiento lejos de esas CPUs. `bus_bind_intr` permite al driver participar en esta distribución.

### La API bus_bind_intr

La signatura de la función:

```c
int bus_bind_intr(device_t dev, struct resource *r, int cpu);
```

`cpu` es un identificador entero de CPU en el rango de 0 a `mp_ncpus - 1`. Si la llamada tiene éxito, la interrupción se enruta hacia esa CPU. Si falla, la función devuelve un errno (habitualmente `EINVAL` si la plataforma no permite la revinculación o la CPU no es válida).

La llamada se realiza después de `bus_setup_intr`:

```c
error = bus_setup_intr(dev, irq_res, flags, filter, ihand, arg,
    &cookie);
if (error == 0)
	bus_bind_intr(dev, irq_res, preferred_cpu);
```

El driver del Capítulo 19 no vincula su interrupción. Un ejercicio de desafío añade un sysctl que permite al operador establecer la CPU preferida.

### La abstracción de conjuntos de CPUs del kernel

Una API más sofisticada: `bus_get_cpus(9)` permite al driver consultar qué CPUs se consideran «locales» para un dispositivo, algo útil para drivers multikola que desean distribuir las interrupciones entre un subconjunto de CPUs local al nodo NUMA. Los cpusets `LOCAL_CPUS` e `INTR_CPUS` de `/usr/src/sys/sys/bus.h` exponen esta información.

El trabajo con MSI-X del Capítulo 20 usará `bus_get_cpus(9)` para ubicar las interrupciones por cola en diferentes CPUs del nodo NUMA local del dispositivo. El driver de interrupción única del Capítulo 19 no necesita esta complejidad.

### Observar la afinidad

El comando `cpuset -g -x <irq>` muestra la máscara de CPU actual para un IRQ. Para el driver `myfirst` en un sistema con varias CPUs, obtén el número de IRQ con `devinfo -v | grep -A 5 myfirst0`, vincula la interrupción a (por ejemplo) la CPU 1 con `cpuset -l 1 -x <irq>` y confirma el resultado con `cpuset -g -x <irq>`.

Los detalles son específicos de la plataforma. En x86, el I/O APIC (o el enrutado MSI) implementa la solicitud; en arm64 lo hace el redistribuidor del GIC. Algunas arquitecturas rechazan la revinculación y devuelven un error; la llamada a `bus_bind_intr` de un driver bien escrito trata ese caso como una sugerencia no fatal.



## Una mirada más profunda a la detección de tormentas de interrupciones

El kernel de FreeBSD dispone de protección integrada frente a un modo de fallo concreto: un IRQ disparado por nivel que se activa continuamente porque el driver no lo reconoce. Esta protección se denomina detección de tormenta de interrupciones, está implementada en `/usr/src/sys/kern/kern_intr.c` y se controla con un único sysctl.

### El sysctl hw.intr_storm_threshold

```c
static int intr_storm_threshold = 0;
SYSCTL_INT(_hw, OID_AUTO, intr_storm_threshold, CTLFLAG_RWTUN,
    &intr_storm_threshold, 0,
    "Number of consecutive interrupts before storm protection is enabled");
```

El valor por defecto es cero (detección desactivada). Establecer el sysctl a un valor positivo activa la detección: si un `intr_event` entrega más de N interrupciones seguidas sin que se produzca ninguna otra interrupción en la misma CPU, el kernel asume que hay una tormenta y regula el evento.

Regular significa que el kernel hace una pausa (mediante `pause("istorm", 1)`) antes de volver a ejecutar los manejadores. La pausa dura un único tic del reloj, que en la mayoría de los sistemas equivale a aproximadamente un milisegundo. El efecto es limitar la tasa a la que una fuente en tormenta puede consumir CPU.

### Cuándo activar la detección

Desactivada por defecto es la configuración de producción. Activar la detección de tormentas significa que el kernel pausa las interrupciones cuando cree que se está produciendo una tormenta; si la detección es incorrecta (una interrupción legítima de alta frecuencia, por ejemplo la de una NIC de 10 gigabits), la pausa se convierte en un problema de rendimiento.

Para el desarrollo de drivers, activar la detección es útil: un reconocimiento olvidado en el filtro produce una tormenta de interrupciones que el kernel detecta y regula (y registra en `dmesg`). Sin detección, la tormenta consume una CPU indefinidamente; con ella, la tormenta es visible y está regulada.

Un valor razonable durante el desarrollo es `hw.intr_storm_threshold=1000`. Mil interrupciones consecutivas en el mismo evento sin intercalado resulta inusual para tráfico legítimo y señala una tormenta de forma fiable.

### Cómo se ve una tormenta

En `dmesg`:

```text
interrupt storm detected on "irq19: myfirst0:legacy"; throttling interrupt source
```

Se repite a intervalos con limitación de tasa (una vez por segundo por defecto, controlado por el `ppsratecheck` dentro del código de detección de tormentas del kernel). La fuente de interrupción tiene nombre; el driver puede identificarse a partir de él.

El kernel no desactiva la línea permanentemente; regula el manejador. Una vez que la tormenta termina (quizás porque el driver se descargó o el dispositivo dejó de afirmar la señal), el manejador se reanuda a plena velocidad.

### Mitigación de tormentas en el lado del driver

Un driver puede implementar su propia mitigación de tormentas. La técnica clásica es:

1. Contar las interrupciones en una ventana deslizante.
2. Si la tasa supera un umbral, enmascarar las interrupciones del dispositivo (mediante `INTR_MASK`) y planificar una tarea que las reactive más adelante.
3. En la tarea, inspeccionar el dispositivo, eliminar la causa de la tormenta y reactivar las interrupciones.

Este enfoque es más invasivo que el comportamiento por defecto del kernel. La mayoría de los drivers no lo implementan. El driver del Capítulo 19 tampoco; el umbral del kernel es suficiente para los escenarios que el capítulo ejercita.

### La relación con los IRQs compartidos

En una línea IRQ compartida, la tormenta de un driver puede interferir con las interrupciones legítimas de otro. La detección de tormentas del kernel opera por evento, no por manejador, de modo que si el manejador de un driver es lento o incorrecto, el evento completo queda regulado. Este es un argumento sólido para escribir filtros correctos: el impacto de la tormenta no se limita al driver defectuoso.



## Un modelo mental para elegir entre filter e ithread

Los principiantes suelen tener dificultades con la decisión entre filter-only, filter-plus-ithread, filter-plus-taskqueue e ithread-only. Esta sección ofrece un marco de decisión útil en la mayoría de los casos, basado en preguntas que el autor de un driver puede responder sobre su dispositivo concreto.

### Cuatro preguntas

Hazte estas preguntas sobre el trabajo de la interrupción:

1. **¿Puede realizarse todo el trabajo en contexto primario?** Si la respuesta es sí (todo acceso al estado mediante spinlocks o atómicas; todos los reconocimientos mediante escrituras en BAR; sin bloqueos que duerman), filter-only es la opción más limpia.
2. **¿Requiere alguna parte del trabajo un sleep lock o una difusión de variable de condición?** Si la respuesta es sí, el grueso del trabajo debe realizarse en contexto de thread. La elección es entre ithread y taskqueue.
3. **¿Se planifica el trabajo diferido desde algún lugar que no sea la interrupción?** Si la respuesta es sí (manejadores de sysctl, ioctl, callouts de temporizador, otras tareas), un taskqueue es mejor. El mismo trabajo puede planificarse desde cualquier contexto.
4. **¿Es el trabajo diferido sensible a la clase de prioridad de la interrupción?** Si la respuesta es sí (deseas prioridad ithread `INTR_TYPE_NET` para trabajo de red), registra un manejador ithread. El ithread hereda la prioridad de la interrupción; un taskqueue se ejecuta con la prioridad que su thread trabajador recibió al crearse.

### Aplicar el marco de decisión

**Filter-only es adecuado para:**
- Un driver de demostración que incrementa contadores.
- Un driver que solo lee un registro del dispositivo y pasa el valor mediante una atómica.
- Un sensor muy sencillo cuyos datos se generan raramente y se leen directamente.

**Filter-plus-ithread es adecuado para:**
- Un driver sencillo en el que el trabajo diferido solo es relevante en la interrupción.
- Un driver que se beneficia de la clase de prioridad de la interrupción.
- Un driver que desea el ithread gestionado por el kernel sin la maquinaria adicional del taskqueue.

**Filter-plus-taskqueue es adecuado para:**
- Un driver en el que el mismo trabajo diferido puede dispararse desde múltiples fuentes (interrupción, sysctl, ioctl).
- Un driver que necesita agrupar interrupciones (el contador `npending` del taskqueue indica cuántos encolados se produjeron desde la última ejecución).
- Un driver que desea un número de threads trabajadores o una prioridad específicos, independientes de la categoría de interrupción.
- El caso objetivo del Capítulo 19: el driver `myfirst` planifica la misma tarea desde el filtro y desde el sysctl de interrupción simulada.

**ithread-only es adecuado para:**
- Un driver en el que no hay trabajo urgente y cada acción necesita un sleep lock.
- Un driver en el que el filtro sería trivial (simplemente «planificar el thread»); no registrar ningún filtro y dejar que el kernel planifique el ithread ahorra una llamada a función.

### Ejemplo práctico: un driver de almacenamiento hipotético

Supón que estás escribiendo un driver para un pequeño controlador de almacenamiento. El dispositivo tiene una línea IRQ. Cuando se completa una operación de I/O, activa `INTR_STATUS.COMPLETION` y lista los IDs de los comandos completados en un registro de cola de finalización.

Las decisiones:

- **¿Puede realizarse todo el trabajo en contexto primario?** No. Despertar el thread que emitió la operación de I/O requiere una difusión de variable de condición, que a su vez requiere el lock del thread. El filtro no puede tomar ese lock.
- **¿Qué mecanismo diferido usar?** El trabajo de gestión de finalización solo lo planifica la interrupción, por lo que filter-plus-ithread es la opción limpia. La clase de prioridad es `INTR_TYPE_BIO`, que el ithread hereda.
- **Diseño final.** El filtro lee `INTR_STATUS`, extrae los IDs de comandos completados en una cola por contexto de interrupción, realiza el reconocimiento y devuelve `FILTER_SCHEDULE_THREAD`. El ithread recorre la cola por contexto, empareja los IDs de comandos con las solicitudes pendientes y despierta el thread de cada solicitud.

### Ejemplo práctico: un driver de red hipotético

Una NIC con cuatro vectores MSI-X (dos colas de recepción, dos colas de transmisión). Cada vector tiene su propio filtro.

Las decisiones:

- **¿Trabajo en el filtro?** Por cola: realizar el reconocimiento y registrar que la cola tiene eventos.
- **¿Trabajo diferido?** Por cola: recorrer el anillo de descriptores, construir mbufs y pasarlos a la pila.
- **¿Múltiples fuentes?** Solo la interrupción en operación normal; el modo polling (para la descarga bajo carga alta) es una segunda fuente. El taskqueue es mejor: tanto el filtro como el temporizador del modo polling pueden encolar.
- **¿Prioridad?** `INTR_TYPE_NET`, que coincide con la prioridad `PI_NET` del thread trabajador del taskqueue.
- **Diseño final.** El filtro por vector devuelve `FILTER_HANDLED` tras encolar la tarea por cola. Un taskqueue por cola de recepción, un trabajador cada uno. Taskqueues configurados con prioridad `PI_NET`.

### Ejemplo práctico: el driver del Capítulo 19

Una línea IRQ, tipos de evento sencillos, trabajo diferido basado en taskqueue.

- **Trabajo en el filtro:** Leer `INTR_STATUS`, contadores por bit, realizar el reconocimiento y encolar la tarea para DATA_AV.
- **Trabajo diferido:** Leer `DATA_OUT`, actualizar el softc y difundir `data_cv`.
- **¿Múltiples fuentes?** Tanto el filtro como el sysctl de interrupción simulada necesitan la tarea. El taskqueue es la opción correcta.
- **¿Prioridad?** `PI_NET` es un valor por defecto razonable aunque el driver no sea una NIC; el marco de simulación espera capacidad de respuesta.

### Cuándo reconsiderar la decisión

La decisión no es permanente. Un driver que comienza como solo de filtro puede incorporar un taskqueue cuando adquiere una nueva capacidad; un driver que comienza con taskqueue puede migrar a ithread cuando la flexibilidad adicional del taskqueue no es necesaria. La refactorización suele ser pequeña (media hora de reorganización del código).

El esquema ayuda a evitar una elección inicial claramente equivocada. Los detalles son una decisión de criterio que el autor del driver toma en función del dispositivo concreto.

## Orden de locks y la ruta de interrupción

La disciplina de locks de la Parte 3 introdujo la idea de que el driver tiene un orden fijo de locks: `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx`. El Capítulo 19 no añade nuevos locks, pero sí añade nuevos contextos en los que se accede a los locks existentes. Este subapartado examina si las incorporaciones del Capítulo 19 respetan el orden existente.

### El filter no adquiere locks

El filter lee `INTR_STATUS`, actualiza contadores atómicos y encola una tarea. No se adquiere ningún sleep lock. El acceso del filter a `INTR_STATUS` utiliza `ICSR_READ_4` e `ICSR_WRITE_4`, que no exigen ningún lock. Por lo tanto, el filter no participa en el orden de locks; es libre de locks.

Esta es la elección más sencilla posible. Un filter más sofisticado podría usar un spinlock (para proteger una pequeña estructura de datos compartida); el filter del Capítulo 19 es más sencillo que eso.

### La tarea adquiere sc->mtx

La función de la tarea `myfirst_intr_data_task_fn` adquiere `sc->mtx` (a través de `MYFIRST_LOCK`), realiza su trabajo y la libera. No adquiere ningún otro lock. Por lo tanto, la tarea respeta el orden de locks existente al no introducir ningún patrón nuevo de adquisición de locks.

### El sysctl de interrupción simulada adquiere y libera sc->mtx

El manejador del sysctl adquiere `sc->mtx` para establecer `INTR_STATUS`, libera el lock y luego invoca el filter. Esto no constituye una violación del orden de locks porque el filter no adquiere ningún lock; no se añade ningún arco nuevo al grafo de locks.

### Las rutas de attach y detach

La ruta de attach adquiere `sc->mtx` brevemente para establecer `INTR_MASK` y realizar la lectura de diagnóstico inicial. No mantiene el lock durante `bus_setup_intr` (que podría, en principio, llamar a otras partes del kernel que adquieren sus propios locks; `bus_setup_intr` está documentada como lockable, lo que significa que el llamador no puede mantener ningún lock propio). La ruta de detach, de forma similar, mantiene `sc->mtx` brevemente alrededor del borrado de `INTR_MASK`, y la libera antes de llamar a `bus_teardown_intr`.

### Una preocupación sutil sobre el orden: bus_teardown_intr puede bloquearse

Conviene señalar un detalle. `bus_teardown_intr` espera a que cualquier invocación en curso del filter o del ithread finalice antes de retornar. Si el driver mantiene un lock que el filter necesita (por ejemplo, un spinlock que el filter adquiere brevemente), `bus_teardown_intr` puede bloquearse indefinidamente porque el filter no puede ejecutarse hasta su fin.

El filter del Capítulo 19 no adquiere ningún spinlock, por lo que esta preocupación es meramente académica. Pero un driver que use spinlocks en el filter debe tener cuidado: nunca se debe mantener el spinlock del filter mientras se llama a `bus_teardown_intr`.

### WITNESS y la ruta de interrupción

El `WITNESS` del kernel de depuración rastrea el orden de los locks en todos los contextos, incluido el filter. Un filter que adquiere un spinlock crea un arco de ordenación en el grafo de `WITNESS`. Si algún código en contexto de thread adquiere el mismo spinlock mientras mantiene otro spinlock diferente, `WITNESS` señala un posible deadlock.

Para el driver del Capítulo 19, no se añaden arcos. `WITNESS` permanece en silencio.

### Qué documentar en LOCKING.md

El `LOCKING.md` de un buen driver documenta el orden de los locks de forma clara. Las incorporaciones del Capítulo 19 son menores:

- El filter no adquiere locks (solo operaciones atómicas).
- La tarea adquiere `sc->mtx` (hoja del orden existente).
- El sysctl de interrupción simulada adquiere `sc->mtx` brevemente para establecer el estado, lo libera y luego invoca el filter (fuera de cualquier lock).

Un párrafo breve en `LOCKING.md` recoge estos hechos. El orden en sí no cambia.



## Observabilidad: qué expone el Capítulo 19 a los operadores

Un capítulo sobre interrupciones es también, de manera indirecta, un capítulo sobre observabilidad. El usuario de un driver (un operador de sistema o un autor de driver que depura un problema) quiere ver qué está haciendo el driver. El Capítulo 19 expone una cantidad modesta de observabilidad a través de contadores y del sysctl de interrupción simulada; este subapartado consolida qué es visible y cómo.

### El conjunto de contadores

Tras la Etapa 4, el driver expone estos sysctls de solo lectura:

- `dev.myfirst.N.intr_count`: invocaciones totales del filter.
- `dev.myfirst.N.intr_data_av_count`: eventos DATA_AV.
- `dev.myfirst.N.intr_error_count`: eventos ERROR.
- `dev.myfirst.N.intr_complete_count`: eventos COMPLETE.
- `dev.myfirst.N.intr_task_invocations`: ejecuciones de la tarea.
- `dev.myfirst.N.intr_last_data`: el valor DATA_OUT más reciente leído por la tarea.

Los contadores ofrecen una vista concisa de la actividad de interrupciones. Observarlos en el tiempo (mediante `watch sysctl dev.myfirst.0` o un bucle de shell) muestra la actividad del driver en directo.

### Los sysctls de escritura

- `dev.myfirst.N.intr_simulate`: escribe una máscara de bits para simular una interrupción.

(El driver del Capítulo 19 solo expone este sysctl de escritura para interrupciones. Los ejercicios de desafío añaden `intr_sim_period_ms` para la simulación basada en frecuencia e `intr_cpu` para la afinidad.)

### La vista a nivel del kernel

`vmstat -i` y `devinfo -v` ya muestran la vista del kernel:

- `vmstat -i` muestra el recuento total y la frecuencia del `intr_event`.
- `devinfo -v` muestra el recurso IRQ del dispositivo.

Estos no son específicos de `myfirst`; están disponibles para cualquier driver. Aprender a leerlos forma parte de las habilidades generales de un operador de FreeBSD.

### Correlacionar las vistas

Un operador que intente diagnosticar un problema podría contrastar los contadores:

```sh
# The kernel's count of interrupts delivered to our handler.
vmstat -i | grep myfirst

# The driver's count of times the filter was invoked.
sysctl dev.myfirst.0.intr_count
```

Si estos números coinciden, la ruta del kernel y el filter del driver están en consonancia. Si el recuento del kernel supera al del driver, algunas interrupciones están siendo gestionadas pero no reconocidas (quizás por otro manejador en una línea compartida). Si el recuento del driver supera al del kernel, algo falla (el driver está contando invocaciones que el kernel no entregó; el sysctl de interrupción simulada es el culpable más probable si se ha activado recientemente).

Una discrepancia de uno o dos durante un ciclo de carga y descarga es normal (por el momento del unload). Una discrepancia que crece de manera constante indica un bug.

### DTrace

El proveedor `fbt` del kernel permite rastrear la entrada y salida de cualquier función del kernel, incluida `myfirst_intr_filter`:

```sh
sudo dtrace -n 'fbt::myfirst_intr_filter:entry { @[probefunc] = count(); }'
```

Esto imprime el recuento de invocaciones del filter vistas por DTrace. Contrástalo con `intr_count`.

Más interesante aún, un script de DTrace puede agregar la temporización por llamada:

```sh
sudo dtrace -n '
fbt::myfirst_intr_filter:entry { self->t = timestamp; }
fbt::myfirst_intr_filter:return /self->t/ {
    @["filter_ns"] = quantize(timestamp - self->t);
    self->t = 0;
}'
```

La salida es un histograma de los tiempos de ejecución del filter en nanosegundos. Un filter saludable consume entre unos pocos cientos de nanosegundos y unos pocos microsegundos; cualquier valor mayor indica un bug o un dispositivo extremadamente lento.

### ktrace y kgdb

Para una depuración profunda, `ktrace` puede rastrear la actividad de las llamadas al sistema; `kgdb` puede inspeccionar el volcado de núcleo de un panic del kernel. El Capítulo 19 no los utiliza directamente, pero un lector cuyo driver cause un panic en la ruta de interrupción los necesitaría.



Cada laboratorio se basa en el anterior y corresponde a una de las etapas del capítulo. Un lector que complete los cinco dispondrá de un driver completo con soporte de interrupciones, un pipeline de interrupción simulada y un script de regresión que valida todo el conjunto.

Los presupuestos de tiempo asumen que el lector ya ha leído las secciones correspondientes.

### Laboratorio 1: explora las fuentes de interrupción de tu sistema

Tiempo: treinta minutos.

Objetivo: desarrollar intuición sobre qué interrupciones gestiona tu sistema y a qué frecuencia.

Pasos:

1. Ejecuta `vmstat -i > /tmp/intr_before.txt`.
2. Haz algo que ejercite el sistema durante treinta segundos: ejecuta `dd if=/dev/urandom of=/dev/null bs=1m count=1000`, abre una página del navegador (en un sistema con sesión gráfica) o copia un archivo desde otro host con scp.
3. Ejecuta `vmstat -i > /tmp/intr_after.txt`.
4. Calcula la diferencia con `diff`:

```sh
paste /tmp/intr_before.txt /tmp/intr_after.txt
```

5. Para cada fuente que haya cambiado, anota:
   - El nombre de la interrupción.
   - El recuento antes y después.
   - La frecuencia inferida durante los treinta segundos.
6. Elige una fuente e identifica su driver con `devinfo -v` o `pciconf -lv`.

Observaciones esperadas:

- Las interrupciones de temporizador (`cpu0:timer` y similares) son elevadas y constantes, una por CPU.
- Las interrupciones de red (`em0`, `igc0`, etc.) son elevadas durante la actividad de `dd` o scp, y cercanas a cero en caso contrario.
- Las interrupciones de almacenamiento (`ahci0`, `nvme0`, etc.) son elevadas durante la actividad de disco, y bajas en caso contrario.
- Algunas interrupciones nunca cambian; esos son los dispositivos que permanecen inactivos durante tu prueba.

Este laboratorio consiste en leer la realidad. Sin código. El beneficio es que la salida de `vmstat -i` en todos los laboratorios posteriores será un terreno conocido.

### Laboratorio 2: Etapa 1, registrar y activar el manejador

Tiempo: dos o tres horas.

Objetivo: añadir la asignación de la interrupción, el registro del filter y la limpieza al driver del Capítulo 18. Versión objetivo: `1.2-intr-stage1`.

Pasos:

1. Partiendo de la Etapa 4 del Capítulo 18, copia el código fuente del driver a un nuevo directorio de trabajo.
2. Edita `myfirst.h` y añade los cuatro campos del softc (`irq_res`, `irq_rid`, `intr_cookie`, `intr_count`).
3. En `myfirst_pci.c`, añade el manejador de filter mínimo (`atomic_add_64`; return `FILTER_HANDLED`).
4. Extiende la ruta de attach con las llamadas de asignación de IRQ, `bus_setup_intr` y `bus_describe_intr`. Añade las etiquetas goto correspondientes.
5. Extiende la ruta de detach con `bus_teardown_intr` y `bus_release_resource` para el IRQ.
6. Añade un sysctl de solo lectura `dev.myfirst.N.intr_count`.
7. Actualiza la cadena de versión a `1.2-intr-stage1`.
8. Compila: `make clean && make`.
9. Carga en un guest de bhyve. Comprueba:
   - Que no aparezca la advertencia `[GIANT-LOCKED]` en `dmesg`.
   - Que `devinfo -v | grep -A 5 myfirst0` muestre tanto `memory:` como `irq:`.
   - Que `vmstat -i | grep myfirst` muestre el manejador.
   - Que `sysctl dev.myfirst.0.intr_count` devuelva un valor razonable (cero si el dispositivo está en silencio).
10. Descarga. Comprueba que `vmstat -m | grep myfirst` muestre cero asignaciones activas.

Fallos habituales:

- Falta de `INTR_MPSAFE`: comprueba si aparece `[GIANT-LOCKED]` en `dmesg`.
- Valor de `rid` incorrecto: `bus_alloc_resource_any` devuelve NULL. Confirma que `sc->irq_rid = 0`.
- Sleep lock en el filter: `WITNESS` provoca un panic.
- Falta de teardown: `kldunload` provoca un panic o el kernel de depuración se queja de manejadores activos.

### Laboratorio 3: Etapa 2, filter real y tarea diferida

Tiempo: tres o cuatro horas.

Objetivo: ampliar el filter para que lea INTR_STATUS, lo reconozca y encole una tarea diferida. Versión objetivo: `1.2-intr-stage2`.

Pasos:

1. Partiendo del Laboratorio 2, añade los contadores por bit (`intr_data_av_count`, `intr_error_count`, `intr_complete_count`, `intr_task_invocations`, `intr_last_data`) al softc.
2. Añade los campos del taskqueue (`intr_tq`) y de la tarea (`intr_data_task`).
3. En `myfirst_init_softc`, inicializa la tarea y crea el taskqueue.
4. En `myfirst_deinit_softc`, drena la tarea y libera el taskqueue.
5. Reescribe el filter para leer `INTR_STATUS`, comprobar cada bit, reconocerlo, encolar la tarea para `DATA_AV` y devolver el valor `FILTER_*` correcto.
6. Escribe la función de la tarea (`myfirst_intr_data_task_fn`) que lee `DATA_OUT`, actualiza el softc y difunde la variable de condición.
7. En la ruta de attach, tras el registro del filter, habilita `INTR_MASK` en el dispositivo.
8. En la ruta de detach, deshabilita `INTR_MASK` antes de `bus_teardown_intr`.
9. Añade sysctls de solo lectura para los nuevos contadores.
10. Actualiza la versión a `1.2-intr-stage2`.
11. Compila, carga y verifica el cableado básico (igual que en el Laboratorio 2).

Para la observación, espera unos segundos tras la carga: si el dispositivo está produciendo interrupciones reales que coinciden con nuestro esquema de bits, los contadores aumentarán. En el objetivo bhyve virtio-rnd, no llegan interrupciones reales del tipo adecuado; verifica los contadores pasando al Laboratorio 4.

### Laboratorio 4: Etapa 3, interrupciones simuladas mediante sysctl

Tiempo: dos o tres horas.

Objetivo: añadir el sysctl `intr_simulate` y usarlo para activar el pipeline. Versión objetivo: `1.2-intr-stage3`.

Pasos:

1. Partiendo del Laboratorio 3, añade el manejador del sysctl `intr_simulate` (el de la Sección 5).
2. Regístralo en `myfirst_init_softc` o en la configuración del sysctl.
3. Compila y carga.
4. Simula un único evento `DATA_AV`:

```sh
sudo sysctl dev.myfirst.0.intr_simulate=1
sleep 0.1
sysctl dev.myfirst.0.intr_count
sysctl dev.myfirst.0.intr_data_av_count
sysctl dev.myfirst.0.intr_task_invocations
```

Los tres contadores deberían mostrar 1.

5. Simula diez eventos `DATA_AV` en un bucle:

```sh
for i in 1 2 3 4 5 6 7 8 9 10; do
    sudo sysctl dev.myfirst.0.intr_simulate=1
done
sleep 0.5
sysctl dev.myfirst.0.intr_task_invocations
```

El recuento de la tarea debería ser cercano a 10 (puede ser menor si el taskqueue fusionó varias entregas en una única ejecución; cada ejecución registra solo una invocación, pero `npending` sería mayor).

6. Simula los tres bits juntos:

```sh
sudo sysctl dev.myfirst.0.intr_simulate=7
```

Los tres contadores por bit se incrementan.

7. Comprueba que `intr_error_count` e `intr_complete_count` se incrementan correctamente:

```sh
sudo sysctl dev.myfirst.0.intr_simulate=2  # ERROR
sudo sysctl dev.myfirst.0.intr_simulate=4  # COMPLETE
sysctl dev.myfirst.0 | grep intr_
```

8. Implementa el callout opcional basado en frecuencia (`intr_sim_period_ms`), verifica la frecuencia:

```sh
sudo sysctl hw.myfirst.intr_sim_period_ms=100
sleep 10
sysctl dev.myfirst.0.intr_count  # around 100
sudo sysctl hw.myfirst.intr_sim_period_ms=0
```

### Lab 5: Fase 4, refactorización, regresión, versión

Tiempo: tres a cuatro horas.

Objetivo: Mover el código de interrupciones a `myfirst_intr.c`/`.h`, introducir los macros `ICSR_*`, escribir `INTERRUPTS.md` y ejecutar la regresión. Versión objetivo: `1.2-intr`.

Pasos:

1. Partiendo del Lab 4, crea `myfirst_intr.c` y `myfirst_intr.h`.
2. Mueve el filter, la tarea, el setup, el teardown y el registro de sysctl a `myfirst_intr.c`.
3. Añade los macros `ICSR_READ_4` e `ICSR_WRITE_4` a `myfirst_intr.h`.
4. Actualiza el filter para que use `ICSR_READ_4`/`ICSR_WRITE_4` en lugar de `bus_read_4`/`bus_write_4` directamente.
5. En `myfirst_pci.c`, sustituye el código de interrupción en línea por llamadas a `myfirst_intr_setup` y `myfirst_intr_teardown`.
6. Actualiza el `Makefile` para añadir `myfirst_intr.c` a SRCS. Actualiza la versión a `1.2-intr`.
7. Escribe `INTERRUPTS.md` documentando el diseño del manejador de interrupciones.
8. Compila.
9. Ejecuta el script de regresión completo (diez ciclos de attach/detach/unload con comprobaciones de contadores; consulta el ejemplo complementario).
10. Confirma: sin advertencias, sin fugas, los contadores coinciden con lo esperado.

Resultados esperados:

- El driver en `1.2-intr` tiene el mismo comportamiento que la Fase 3, pero con una estructura de archivos más limpia.
- `myfirst_pci.c` es entre 50 y 80 líneas más corto.
- `myfirst_intr.c` tiene aproximadamente entre 200 y 300 líneas.
- El script de regresión pasa diez veces seguidas.



## Ejercicios de desafío

Los desafíos son opcionales. Cada uno toma como base uno de los laboratorios y extiende el driver en una dirección que el capítulo no tomó. Consolidan el material del capítulo y son una buena preparación para el Capítulo 20.

### Desafío 1: Añadir un manejador filter más ithread

Reescribe el filter de la Fase 2 para que devuelva `FILTER_SCHEDULE_THREAD` en lugar de encolar una tarea en el taskqueue. Registra un manejador ithread a través del quinto argumento de `bus_setup_intr(9)` que realice el trabajo que hacía la tarea. Compara los dos enfoques.

Este ejercicio es el mejor modo de interiorizar la diferencia entre el trabajo diferido basado en ithread y el basado en taskqueue. Tras completarlo, deberías ser capaz de explicar cuándo es apropiado cada uno.

### Desafío 2: Implementar la mitigación de tormentas en el driver

Añade un contador que registre el número de interrupciones gestionadas en el milisegundo actual. Si el conteo supera un umbral (por ejemplo, 10.000), enmascara las interrupciones del dispositivo y programa una tarea para volver a habilitarlas 10 ms después.

Este ejercicio demuestra que la mitigación en el lado del driver es posible, y muestra por qué el comportamiento predeterminado del kernel (no hacer nada) suele ser suficiente.

### Desafío 3: Vincular la interrupción a una CPU específica

Añade un sysctl `dev.myfirst.N.intr_cpu` que acepte un identificador de CPU. Cuando se escriba en él, llama a `bus_bind_intr(9)` para enrutar la interrupción a esa CPU. Verifica con `cpuset -g` o con los contadores por CPU de `vmstat -i`.

Este ejercicio introduce la API de afinidad de CPU y muestra cómo la elección es visible en las herramientas del sistema.

### Desafío 4: Ampliar las interrupciones simuladas con tasas por tipo

Modifica el callout `intr_sim_period_ms` para que acepte una máscara de bits que indique qué clases de eventos simular, no solo `DATA_AV`. Deberías poder simular eventos `ERROR` y `COMPLETE` alternos a diferentes tasas.

El ejercicio pone a prueba tu comprensión del manejo bit a bit del filter de la Fase 2.

### Desafío 5: Añadir un registro de interrupciones extraviadas con limitación de tasa

Implementa el registro de interrupciones extraviadas basado en `ppsratecheck(9)` mencionado en la Sección 6. Verifica que el registro aparece a la tasa esperada cuando el driver recibe interrupciones extraviadas (puedes inducirlas deshabilitando `INTR_MASK` mientras el dispositivo genera eventos, o llamando manualmente al filter con un estado de cero).

### Desafío 6: Implementar la asignación de MSI (adelanto del Capítulo 20)

Añade código a la ruta de attach que intente primero `pci_alloc_msi(9)` y recurra al IRX legado si MSI no está disponible. El filter permanece igual. Este es un adelanto del Capítulo 20; hacerlo ahora te familiarizará con la API de asignación de MSI.

Ten en cuenta que en el objetivo bhyve virtio-rnd, MSI no suele estar disponible (el transporte virtio legado de bhyve usa INTx). El dispositivo `virtio-rng-pci` de QEMU expone MSI-X; puede que quieras cambiar los laboratorios a QEMU para este desafío.

### Desafío 7: Escribir una prueba de latencia

Usa la ruta de interrupción simulada para medir la latencia del driver desde la interrupción hasta el espacio de usuario. Un programa en espacio de usuario abre `/dev/myfirst0` y emite un `read(2)` que duerme esperando en la variable de condición; un segundo programa escribe en el sysctl `intr_simulate`, arrancando un temporizador de reloj de pared; el `read` del primer programa retorna, deteniendo el temporizador. Traza la distribución a lo largo de muchas iteraciones.

Este ejercicio te introduce a la medición del rendimiento en la ruta diferida del driver. Las latencias típicas son del orden de decenas de microsegundos en un sistema bien ajustado.

### Desafío 8: Compartir el IRQ de forma deliberada

Si tienes un guest bhyve configurado con varios dispositivos en las funciones del mismo slot, fuérzalos deliberadamente a compartir un IRQ. Carga ambos drivers (el driver del sistema base para el otro dispositivo; nuestro `myfirst` para virtio-rnd). Verifica con `vmstat -i` que comparten la línea. Observa el comportamiento cuando cualquiera de ellos dispara.

Este ejercicio es la demostración más clara de la corrección al compartir un IRQ. Un driver que no aplique correctamente la disciplina de la Sección 6 fallará aquí.



## Solución de problemas y errores comunes

Una lista consolidada de modos de fallo, síntomas y soluciones específicos de interrupciones. Consérvala como referencia a la que puedes volver.

### "El driver carga pero no se cuentan interrupciones"

Síntoma: `kldload` tiene éxito, `dmesg` muestra el mensaje de attach, pero `sysctl dev.myfirst.0.intr_count` se mantiene en cero indefinidamente.

Causas probables:

1. El dispositivo no está generando interrupciones. En el objetivo bhyve virtio-rnd esto es normal porque el dispositivo no genera eventos del estilo del Capítulo 17. Usa el sysctl de interrupción simulada para alimentar el pipeline.
2. `INTR_MASK` no se estableció. El manejador está registrado, pero el dispositivo no está afirmando la línea porque la máscara es cero. Comprueba en la ruta de attach la llamada `CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, ...)`.
3. El dispositivo está enmascarado por otros medios. Comprueba en el registro de comandos el bit `PCIM_CMD_INTxDIS` (bit de deshabilitación de interrupción); si está establecido, bórralo.
4. Se asignó el IRQ incorrecto. `rid = 0` debería producir el INTx legado del dispositivo. Comprueba que `devinfo -v | grep -A 5 myfirst0` muestra una entrada `irq:`.

### "Aviso GIANT-LOCKED en dmesg"

Síntoma: `dmesg` tras `kldload` muestra `myfirst0: [GIANT-LOCKED]`.

Causa: `INTR_MPSAFE` no se pasó al argumento de flags de `bus_setup_intr`.

Solución: Añade `INTR_MPSAFE` a los flags. Verifica que el filter utiliza únicamente operaciones que no duermen (operaciones atómicas, spin mutexes). Verifica que la disciplina de lock en el softc permite la operación segura en entornos SMP.

### "Pánico del kernel en el filter"

Síntoma: Un pánico del kernel cuyo backtrace muestra `myfirst_intr_filter`.

Causas probables:

1. `sc` es NULL o ha quedado obsoleto. Comprueba el argumento de `bus_setup_intr`; debe ser `sc`, no `&sc`.
2. Se está adquiriendo un sleep lock. `WITNESS` entra en pánico por esto. La solución es eliminar el sleep lock o mover el trabajo a un taskqueue.
3. El filter se invoca contra un softc ya liberado. Esto normalmente significa que detach no desmontó el manejador antes de liberar el estado. Comprueba el orden de detach.
4. `sc->bar_res` es NULL. Una condición de carrera entre el desenrollado de un fallo parcial en attach y la ejecución del filter. Protege el primer acceso del filter con una comprobación.

### "La tarea se ejecuta pero accede a estado liberado"

Síntoma: Pánico del kernel en la función de tarea; el backtrace muestra `myfirst_intr_data_task_fn`.

Causa: La tarea se encoló durante o justo antes del detach, y detach liberó el softc antes de que la tarea se ejecutase.

Solución: Añade `taskqueue_drain` a la ruta de detach, antes de liberar cualquier cosa que la tarea use. Consulta la Sección 7.

### "Los bits de INTR_STATUS siguen disparándose; tormenta detectada"

Síntoma: `dmesg` muestra `interrupt storm detected`.

Causa: El filter no está reconociendo `INTR_STATUS` correctamente. Posibilidades:

1. El filter no escribe en `INTR_STATUS` en absoluto. Añade la escritura.
2. El filter escribe un valor incorrecto. Escribe los bits específicos que gestionaste, no `0` ni `0xffffffff`.
3. El filter solo gestiona algunos bits; los bits no reconocidos permanecen establecidos y siguen afirmando la línea. O bien reconoce todos los bits, o bien reconoce explícitamente los no reconocidos, o bien bórralos en `INTR_MASK`.

### "La interrupción simulada no ejecuta la tarea"

Síntoma: `sudo sysctl dev.myfirst.0.intr_simulate=1` incrementa `intr_count` pero no `intr_task_invocations`.

Causas probables:

1. El bit simulado no coincide con lo que busca el filter. El filter de la Fase 2 encola la tarea para `DATA_AV` (bit 0x1). Escribir `2` o `4` establece ERROR o COMPLETE; esos no encolan. Escribe `1` o `7`.
2. La función de tarea no está registrada. Comprueba que `TASK_INIT` se llama en `myfirst_init_softc`.
3. El taskqueue no está creado. Comprueba `taskqueue_create` y `taskqueue_start_threads` en `myfirst_init_softc`.

### "kldunload falla con Device busy"

Síntoma: `kldunload myfirst` falla con `Device busy`.

Causas: Las mismas que en el Capítulo 18. Un proceso del espacio de usuario tiene el cdev abierto; un comando en curso no ha terminado; la comprobación de ocupado del driver tiene un error. Ejecuta `fstat /dev/myfirst0` para ver quién lo tiene abierto.

### "vmstat -m muestra asignaciones vivas tras la descarga"

Síntoma: `vmstat -m | grep myfirst` devuelve un valor `InUse` distinto de cero tras `kldunload`.

Causas probables:

1. El taskqueue no se drenó. Comprueba `taskqueue_drain` en la ruta de detach.
2. El backend de simulación se inicializó (build solo de simulación) y no se liberó. Comprueba `myfirst_sim_detach` en la ruta de detach.
3. Una fuga en `myfirst_init_softc` / `myfirst_deinit_softc`. Comprueba que cada asignación tiene una liberación correspondiente.

### "El manejador dispara en la CPU incorrecta"

Síntoma: `cpuset -g` muestra que la interrupción disparó en la CPU X; tú querías que fuera en la CPU Y.

Causa: `bus_bind_intr` no se llamó, o se llamó con el argumento de CPU incorrecto.

Solución: Añade un sysctl que permita al operador establecer la CPU deseada y llama a `bus_bind_intr`. Consulta el Desafío 3.

### "La escritura en INTR_MASK tiene efectos secundarios no deseados"

Síntoma: En el objetivo bhyve virtio-rnd, escribir en el desplazamiento 0x10 (el desplazamiento `INTR_MASK` del Capítulo 17) cambia el estado del dispositivo de formas inesperadas.

Causa: El mapa de registros del Capítulo 17 no coincide con el de virtio-rnd. El desplazamiento 0x10 en virtio-rnd es `queue_notify`, no `INTR_MASK`.

Solución: Esto es un desajuste con el objetivo de prueba, no un error del driver. El capítulo reconoce el problema. En un dispositivo real con el mapa de registros del Capítulo 17, la escritura es correcta. En el objetivo de enseñanza bhyve, la escritura es inocua (notifica a una virtqueue inactiva) pero no tiene significado.

### "Mensajes de interrupciones extraviadas en dmesg"

Síntoma: `dmesg` muestra periódicamente mensajes sobre interrupciones extraviadas en la línea IRQ.

Causas probables:

1. El manejador no está enmascarando `INTR_MASK` correctamente en el dispositivo durante el detach (interrupción extraviada por nivel).
2. El dispositivo está generando interrupciones que el driver no ha habilitado. Comprueba la configuración de `INTR_MASK`.
3. Otro driver que comparte la línea está devolviendo un valor `FILTER_*` incorrecto. Ese es el error de ese driver, no el nuestro.

### "El manejador se invoca concurrentemente en múltiples CPUs"

Síntoma: Los contadores atómicos se incrementan de forma no monótona, lo que sugiere invocaciones concurrentes del filter.

Causa: Con MSI-X (Capítulo 20), el mismo manejador puede ejecutarse concurrentemente en diferentes CPUs. Esto es por diseño. Para IRQs legados esto es poco frecuente, pero posible en algunas configuraciones.

Solución: Asegúrate de que todo acceso al estado del filter sea atómico o esté protegido por un spin-lock. El driver del Capítulo 19 usa `atomic_add_64` en todo momento; no se necesitan cambios.

### "bus_setup_intr devuelve EINVAL"

Síntoma: El valor de retorno de `bus_setup_intr` es `EINVAL` y el driver no logra cargarse.

Causas probables:

1. Ambos argumentos `filter` e `ihand` son `NULL`. Al menos uno debe ser distinto de `NULL`; de lo contrario el kernel no tiene nada que llamar.
2. El flag `INTR_TYPE_*` se omitió del argumento de flags. Exactamente una categoría debe estar establecida.
3. El recurso IRQ no se asignó con `RF_ACTIVE`. Un recurso no activado no puede tener un manejador asociado.
4. El argumento de flags contiene bits mutuamente excluyentes (poco frecuente; un autor de driver tendría que haberlo introducido deliberadamente).

Corrección: lee la página del manual de `bus_setup_intr(9)`; el caso más habitual es que falte el argumento filter o ithread, o que falte el flag de categoría.

### "bus_setup_intr devuelve EEXIST"

Síntoma: `bus_setup_intr` devuelve `EEXIST` en una carga posterior.

Causa: La línea IRQ ya tiene un handler exclusivo asociado. O bien este driver se cargó antes y no realizó su desmontaje correctamente, o bien otro driver ha reclamado la línea en exclusiva.

Solución: En primer lugar, intenta descargar cualquier instancia anterior (`kldunload myfirst`). Si el problema persiste, comprueba `devinfo -v` para ver si algún driver está usando actualmente la IRQ.

### El kernel de depuración entra en pánico en `taskqueue_drain`

Síntoma: `taskqueue_drain` provoca un pánico en el kernel de depuración.

Posibles causas:

1. El taskqueue nunca se creó. `sc->intr_tq` es NULL. Revisa `myfirst_init_softc`.
2. El taskqueue ya fue liberado. Comprueba si hay un double-free en el camino de desmontaje.
3. `TASK_INIT` nunca se llamó. El puntero a la función de la tarea contiene basura.

Solución: Asegúrate de que `TASK_INIT` se ejecute antes de que `taskqueue_enqueue` se llame nunca; asegúrate de que `taskqueue_free` se ejecute como máximo una vez.

### El filter se invoca pero `INTR_STATUS` devuelve `0xffffffff`

Síntoma: El filter se ejecuta, lee `INTR_STATUS` y obtiene `0xffffffff`.

Posibles causas:

1. El dispositivo no responde (puede que el invitado bhyve se haya detenido o que el dispositivo se haya desconectado en caliente).
2. El mapeo del BAR es incorrecto. Comprueba el camino de attach.
3. Un error PCI ha dejado el dispositivo en un estado degradado.

Solución: Si el dispositivo está activo, la lectura devuelve bits de estado reales. Si devuelve `0xffffffff`, algo más falla. El filter debe seguir devolviendo `FILTER_STRAY` (porque `0xffffffff` es poco probable que sea un valor de estado legítimo; compruébalo con el datasheet del dispositivo para determinar las combinaciones de bits válidas).

### Las interrupciones se cuentan pero el dispositivo no avanza

Síntoma: `intr_count` se incrementa, pero la operación del dispositivo (transferencia de datos, finalización de tareas, etc.) no avanza.

Posibles causas:

1. El filter reconoce todos los bits pero la tarea no se ejecuta. Comprueba `intr_task_invocations`; si es cero, el camino de `taskqueue_enqueue` está roto.
2. La tarea se ejecuta pero no despierta a los procesos en espera. Comprueba `cv_broadcast` en la tarea.
3. El dispositivo está señalando una condición inusual. Comprueba el contenido de `INTR_STATUS` (léelo mediante el camino sysctl o en DDB).

Solución: Añade registros en la tarea (mediante `device_printf`); comprueba si la lógica de la tarea coincide con el comportamiento real del dispositivo.

### `kldunload` se queda bloqueado

Síntoma: `kldunload myfirst` no retorna. No hay pánico ni salida alguna.

Posibles causas:

1. `bus_teardown_intr` está bloqueado esperando a que termine un handler en curso (filter o ithread). El handler está atascado.
2. `taskqueue_drain` está bloqueado esperando una tarea atascada.
3. La función detach está esperando en una variable de condición que nunca se emite por broadcast.

Solución: Si el sistema responde en otros aspectos, cambia a DDB (pulsa la tecla NMI o ejecuta `sysctl debug.kdb.enter=1`) y usa `ps` para localizar los threads bloqueados. El backtrace suele señalar directamente la función atascada.

### Acceso a memoria no alineada en el filter

Síntoma: Un pánico del kernel en una arquitectura sensible al alineamiento (arm64, MIPS, SPARC), con un backtrace que apunta al filter.

Causa: El filter está leyendo o escribiendo un registro en un desplazamiento no alineado. Las lecturas y escrituras en los BAR de PCI requieren alineamiento natural (4 bytes para lecturas de 32 bits, 2 bytes para lecturas de 16 bits).

Solución: Usa `bus_read_4` / `bus_write_4` en desplazamientos alineados a 4 bytes. El mapa de registros del capítulo 17 está alineado a 4 bytes en toda su extensión.

### `device_printf` desde el filter ralentiza el sistema

Síntoma: Añadir llamadas a `device_printf` en el filter hace que el sistema sea notablemente lento a frecuencias de interrupción elevadas.

Causa: `device_printf` adquiere un lock y realiza una impresión formateada. Con diez mil interrupciones por segundo, la sobrecarga es apreciable.

Solución: Elimina las impresiones de depuración del filter antes de las pruebas en producción. Usa contadores y DTrace para la observabilidad.

### El driver pasa todas las pruebas pero falla bajo carga

Síntoma: Las pruebas en un solo thread pasan, pero las pruebas de carga con muchos procesos concurrentes provocan errores ocasionales o corrupción de estado.

Posibles causas:

1. Una condición de carrera entre el filter y la tarea. El filter establece un indicador que la tarea lee; la tarea actualiza un estado que el filter lee. Sin una sincronización adecuada, uno puede perderse la actualización del otro.
2. Una condición de carrera entre la tarea y otro camino en contexto de thread (handler de cdev, sysctl). La tarea toma `sc->mtx`; el otro camino también debería hacerlo.
3. Una variable atómica usada en una operación compuesta sin lock. `atomic_add_64` por sí solo es atómico; `atomic_load_64` seguido de un cálculo seguido de `atomic_store_64` no es atómico como secuencia.

Solución: Revisa la disciplina de locking. `WITNESS` no detecta las condiciones de carrera puramente sobre variables atómicas; solo lo hace una revisión cuidadosa del código. Ejecuta bajo carga elevada con `INVARIANTS` activado y vigila si aparecen fallos de aserción.

### `vmstat -i` muestra muchos strays en una línea que no es mía

Síntoma: Un driver en una línea compartida observa que el contador de strays de la línea crece de forma constante.

Posibles causas:

1. Otro driver en la línea devuelve `FILTER_STRAY` incorrectamente (la interrupción es para él pero afirma lo contrario).
2. Un dispositivo en la línea está señalando eventos que los drivers no reconocen, produciendo strays fantasma.
3. Ruido del hardware o un modo de disparo mal configurado.

Solución: La solución suele estar en el driver que devuelve `FILTER_STRAY` incorrectamente. El comportamiento de tu propio driver es correcto siempre que su comprobación del registro de estado sea la adecuada.



## Observabilidad avanzada: integración con DTrace

DTrace en FreeBSD puede observar el camino de la interrupción en múltiples niveles. Esta subsección muestra algunos one-liners y scripts DTrace útiles que el autor de un driver puede emplear durante el desarrollo.

### Contar invocaciones del filter

```sh
sudo dtrace -n '
fbt::myfirst_intr_filter:entry { @invocations = count(); }'
```

Muestra el número total de veces que se ha llamado al filter desde que DTrace arrancó. Compáralo con `sysctl dev.myfirst.0.intr_count`; deberían coincidir.

### Medir la latencia del filter

```sh
sudo dtrace -n '
fbt::myfirst_intr_filter:entry { self->ts = vtimestamp; }
fbt::myfirst_intr_filter:return /self->ts/ {
    @["filter_ns"] = quantize(vtimestamp - self->ts);
    self->ts = 0;
}'
```

`vtimestamp` mide el tiempo de CPU (no el tiempo de reloj), por lo que el histograma representa verdaderamente el tiempo de CPU del filter. Un filter sano se sitúa en el rango de cientos de nanosegundos a unos pocos microsegundos.

### Observar la cola de tareas

```sh
sudo dtrace -n '
fbt::myfirst_intr_data_task_fn:entry {
    @["task_runs"] = count();
    self->ts = vtimestamp;
}
fbt::myfirst_intr_data_task_fn:return /self->ts/ {
    @["task_ns"] = quantize(vtimestamp - self->ts);
    self->ts = 0;
}'
```

Muestra el número de invocaciones de la tarea y el tiempo de ejecución por invocación. La tarea es típicamente un orden de magnitud más lenta que el filter (porque toma un sleep lock y realiza más trabajo).

### Correlacionar el filter y la tarea

```sh
sudo dtrace -n '
fbt::myfirst_intr_filter:entry /!self->in_filter/ {
    self->in_filter = 1;
    self->filter_start = vtimestamp;
    @["filter_enters"] = count();
}
fbt::myfirst_intr_filter:return /self->in_filter/ {
    self->in_filter = 0;
}
fbt::myfirst_intr_data_task_fn:entry {
    @["task_starts"] = count();
}'
```

Si `filter_enters` es 100 y `task_starts` es 80, algunas invocaciones del filter no programaron una tarea (porque el evento era ERROR o COMPLETE, no DATA_AV).

### Rastrear las decisiones de planificación del taskqueue

La infraestructura del taskqueue también tiene sondas DTrace; se puede observar cómo se encola la tarea y cuándo se ejecuta el thread trabajador:

```sh
sudo dtrace -n '
fbt::taskqueue_enqueue:entry /arg0 == $${tq_addr}/ {
    @["enqueues"] = count();
}'
```

donde `$${tq_addr}` es la dirección numérica de `sc->intr_tq`, que se puede obtener mediante combinaciones de `kldstat` / `kgdb`. Este nivel de detalle suele ser excesivo para el driver del capítulo 19.

### DTrace y el camino de la interrupción simulada

Las interrupciones simuladas se distinguen de las reales porque el camino simulado pasa por el handler del sysctl:

```sh
sudo dtrace -n '
fbt::myfirst_intr_simulate_sysctl:entry { @["simulate"] = count(); }
fbt::myfirst_intr_filter:entry { @["filter"] = count(); }'
```

La diferencia entre los dos contadores es el número de interrupciones reales (llamadas al filter no precedidas por una llamada al sysctl).



## Recorrido detallado: la etapa 2 de principio a fin

Para que el driver del capítulo 19 sea concreto, aquí tienes un recorrido completo de lo que ocurre cuando se simula un evento `DATA_AV` mediante el sysctl, trazado paso a paso.

### La secuencia

1. El usuario ejecuta `sudo sysctl dev.myfirst.0.intr_simulate=1`.
2. La maquinaria sysctl del kernel enruta la escritura hacia `myfirst_intr_simulate_sysctl`.
3. El handler analiza el valor (1), adquiere `sc->mtx` mediante `MYFIRST_LOCK`.
4. El handler escribe `1` en `INTR_STATUS` dentro del BAR.
5. El handler libera `sc->mtx`.
6. El handler llama directamente a `myfirst_intr_filter(sc)`.
7. El filter lee `INTR_STATUS` mediante `ICSR_READ_4`. El valor es `1` (DATA_AV).
8. El filter incrementa `intr_count` de forma atómica.
9. El filter detecta el bit DATA_AV activado e incrementa `intr_data_av_count`.
10. El filter reconoce la interrupción escribiendo `1` de vuelta en `INTR_STATUS` mediante `ICSR_WRITE_4`.
11. El filter encola `intr_data_task` en `intr_tq` mediante `taskqueue_enqueue`.
12. El filter devuelve `FILTER_HANDLED`.
13. El handler sysctl devuelve `0` a la capa sysctl del kernel.
14. El comando `sysctl` del usuario retorna con éxito.

Mientras tanto, en el taskqueue:

15. El thread trabajador del taskqueue (despertado por `taskqueue_enqueue`) se planifica.
16. El thread trabajador llama a `myfirst_intr_data_task_fn(sc, 1)`.
17. La tarea adquiere `sc->mtx`.
18. La tarea lee `DATA_OUT` mediante `CSR_READ_4`.
19. La tarea almacena el valor en `sc->intr_last_data`.
20. La tarea incrementa `intr_task_invocations`.
21. La tarea emite un broadcast en `sc->data_cv` (no hay procesos en espera en este ejemplo).
22. La tarea libera `sc->mtx`.
23. El thread trabajador vuelve a esperar más trabajo.

Los pasos 1-14 toman microsegundos; los pasos 15-23 toman decenas o cientos de microsegundos según la planificación.

### Lo que muestran los contadores

Tras una interrupción simulada:

```text
dev.myfirst.0.intr_count: 1
dev.myfirst.0.intr_data_av_count: 1
dev.myfirst.0.intr_error_count: 0
dev.myfirst.0.intr_complete_count: 0
dev.myfirst.0.intr_task_invocations: 1
```

Si `intr_task_invocations` sigue siendo 0, la tarea aún no se ha ejecutado (normalmente porque el sysctl retornó antes de que el thread trabajador fuera planificado). Un breve `sleep 0.01` es suficiente.

### Lo que muestra `dmesg`

Por defecto, nada. El driver de la etapa 4 no es verboso. Un lector que quiera ver el filter en acción puede añadir llamadas a `device_printf` para depuración, pero los drivers de calidad para producción no suelen imprimir en cada interrupción.

### Lo que muestra `vmstat -i`

`vmstat -i | grep myfirst` muestra el recuento total del `intr_event`. Solo cuenta las interrupciones reales entregadas por el kernel a nuestro filter. Las interrupciones simuladas invocadas mediante el sysctl no pasan por el despachador de interrupciones del kernel, por lo que no aparecen en el recuento de `vmstat -i`.

Esta es una distinción útil: la simulación entregada mediante sysctl es un mecanismo complementario, no un sustituto. Las interrupciones reales se cuentan; las simuladas no.

### Rastreo con sentencias de impresión

Para una depuración rápida, añadir llamadas a `device_printf` al filter y a la tarea ofrece una imagen en tiempo real:

```c
/* In the filter, temporarily for debugging: */
device_printf(sc->dev, "filter: status=0x%x\n", status);

/* In the task: */
device_printf(sc->dev, "task: data=0x%x npending=%d\n",
    data, npending);
```

Esto produce una salida en `dmesg` similar a:

```text
myfirst0: filter: status=0x1
myfirst0: task: data=0xdeadbeef npending=1
```

Elimina estas impresiones antes de pasar a producción; el ruido generado a frecuencias de interrupción elevadas tiene un coste significativo.



## Patrones de drivers reales de FreeBSD

Un recorrido compacto por los patrones de interrupción que aparecen repetidamente en `/usr/src/sys/dev/`. Cada patrón es un fragmento concreto de un driver real (ligeramente reescrito para mejorar la legibilidad) junto con una nota sobre su relevancia. Leerlos después del capítulo 19 consolida el vocabulario.

### Patrón: filter rápido con tarea lenta

De `/usr/src/sys/dev/mgb/if_mgb.c`:

```c
int
mgb_legacy_intr(void *xsc)
{
	struct mgb_softc *sc = xsc;
	uint32_t intr_sts = CSR_READ_REG(sc, MGB_INTR_STS);
	uint32_t intr_en = CSR_READ_REG(sc, MGB_INTR_ENBL_SET);

	intr_sts &= intr_en;
	if ((intr_sts & MGB_INTR_STS_ANY) == 0)
		return (FILTER_STRAY);

	/* Acknowledge and defer per-queue work. */
	if ((intr_sts & MGB_INTR_STS_RX_ANY) != 0) {
		for (int qidx = 0; qidx < scctx->isc_nrxqsets; qidx++) {
			if (intr_sts & MGB_INTR_STS_RX(qidx))
				iflib_rx_intr_deferred(sc->ctx, qidx);
		}
		return (FILTER_HANDLED);
	}
	return (FILTER_SCHEDULE_THREAD);
}
```

Por qué es relevante: el filter es breve, el trabajo diferido se hace por cola y se mantiene la disciplina de IRQ compartida. El filter del capítulo 19 sigue la misma forma.

### Patrón: handler solo con ithread

De `/usr/src/sys/dev/ath/if_ath_pci.c`:

```c
bus_setup_intr(dev, psc->sc_irq,
    INTR_TYPE_NET | INTR_MPSAFE,
    NULL, ath_intr, sc, &psc->sc_ih);
```

El argumento del filter es `NULL`; `ath_intr` es el handler del ithread. El kernel planifica `ath_intr` en cada interrupción sin ningún filter de por medio.

Por qué es relevante: a veces todo el trabajo necesita contexto de thread. Registrar `NULL` para el filter es más sencillo que escribir un filter trivial que simplemente devuelva `FILTER_SCHEDULE_THREAD`.

### Patrón: `INTR_EXCL` para acceso exclusivo

Algunos drivers necesitan acceso exclusivo a una línea de interrupción:

```c
bus_setup_intr(dev, irq,
    INTR_TYPE_BIO | INTR_MPSAFE | INTR_EXCL,
    NULL, driver_intr, sc, &cookie);
```

Por qué es relevante: en casos poco frecuentes, un driver necesita la línea en exclusiva (la asunción del handler de ser el único oyente está integrada en su lógica). `INTR_EXCL` pide al kernel que rechace a otros drivers en el mismo evento.

### Patrón: registro de depuración breve

Algunos drivers tienen un modo verboso opcional que registra cada llamada al filter:

```c
if (sc->sc_debug > 0)
	device_printf(sc->sc_dev, "interrupt: status=0x%x\n", status);
```

Por qué es relevante: un driver en desarrollo se beneficia del registro; un driver en producción quiere tenerlo suprimido. Un sysctl (`dev.driver.N.debug`) activa o desactiva el modo.

### Patrón: vincular a una CPU específica

Los drivers que conocen su topología vinculan la interrupción a una CPU local:

```c
/* After bus_setup_intr: */
error = bus_bind_intr(dev, irq, local_cpu);
if (error != 0)
	device_printf(dev, "bus_bind_intr: %d\n", error);
/* Non-fatal: some platforms do not support binding. */
```

Por qué es relevante: los handlers con localidad NUMA son más rápidos. Un driver que se toma la molestia de vincular ofrece mejor escalabilidad en sistemas multisocket.

### Patrón: describir el handler para diagnóstico

Todo driver debería llamar a `bus_describe_intr`:

```c
bus_describe_intr(dev, irq, cookie, "rx-%d", queue_id);
```

Por qué importa: `vmstat -i` y `devinfo -v` utilizan la descripción para distinguir los manejadores en eventos compartidos. Un driver con N colas y N vectores MSI-X realiza N llamadas a `bus_describe_intr`.

### Patrón: detener la actividad antes del detach

```c
mtx_lock(&sc->mtx);
sc->shutting_down = true;
mtx_unlock(&sc->mtx);

/* Let the interrupt handler drain. */
bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
```

Por qué importa: el flag `shutting_down` le da al handler un camino de salida rápida (el handler comprueba el flag antes de realizar su trabajo normal). El `bus_teardown_intr` es el drenado definitivo, pero el flag hace que ese drenado sea más rápido.

El driver del Capítulo 19 usa `sc->pci_attached` con un propósito similar.



## Referencia: lista rápida de errores comunes

Una lista compacta de errores específicos de las interrupciones y sus correcciones en una línea. Resulta útil como lista de comprobación al revisar tu propio driver.

1. **Sin INTR_MPSAFE.** Corrección: `flags = INTR_TYPE_MISC | INTR_MPSAFE`.
2. **Sleep lock en el filter.** Corrección: usa operaciones atómicas o un spin mutex.
3. **Falta de confirmación (acknowledgment).** Corrección: `bus_write_4(res, INTR_STATUS, bits_handled);`.
4. **Confirmando demasiados bits.** Corrección: escribe solo los bits que hayas gestionado.
5. **Falta el retorno `FILTER_STRAY`.** Corrección: si el estado es cero o no reconocido, devuelve `FILTER_STRAY`.
6. **Falta el retorno `FILTER_HANDLED`.** Corrección: `rv |= FILTER_HANDLED;` para cada bit reconocido.
7. **La tarea usa un softc obsoleto.** Corrección: añade `taskqueue_drain` al detach.
8. **Falta `bus_teardown_intr`.** Corrección: antes de `bus_release_resource(SYS_RES_IRQ, ...)`.
9. **Falta `INTR_MASK = 0` en el detach.** Corrección: limpia la máscara antes del desmontaje.
10. **Falta `taskqueue_drain`.** Corrección: drena antes de liberar el estado del softc.
11. **Valor de retorno incorrecto del filter.** Corrección: debe ser `FILTER_HANDLED`, `FILTER_STRAY`, `FILTER_SCHEDULE_THREAD`, o un OR bit a bit.
12. **Encolar una tarea antes de `TASK_INIT`.** Corrección: inicializa la tarea en attach.
13. **No establecer `INTR_MASK` en el attach.** Corrección: escribe los bits que quieras habilitar.
14. **rid incorrecto para IRQ legacy.** Corrección: usa `rid = 0`.
15. **Tipo de recurso incorrecto.** Corrección: usa `SYS_RES_IRQ` para interrupciones.
16. **Falta `RF_SHAREABLE` en una línea compartida.** Corrección: incluye el flag en la asignación.
17. **Mantener sc->mtx durante `bus_setup_intr`.** Corrección: libera el lock antes de la llamada.
18. **Mantener un spin lock durante `bus_teardown_intr`.** Corrección: nunca mantengas el spin lock de un filter al desmontar.
19. **Taskqueue destruido mientras una tarea sigue en cola.** Corrección: ejecuta `taskqueue_drain` antes de `taskqueue_free`.
20. **Falta la llamada a `bus_describe_intr`.** Corrección: añádela después de `bus_setup_intr` para mayor claridad en el diagnóstico.



## Referencia: prioridades de ithread y taskqueue

El código del Capítulo 19 usa `PI_NET` para el taskqueue. FreeBSD define varias constantes de prioridad en `/usr/src/sys/sys/priority.h`. Una vista simplificada:

```text
PI_REALTIME  = PRI_MIN_ITHD + 0   (highest ithread priority)
PI_INTR      = PRI_MIN_ITHD + 4   (the common "hardware interrupt" level)
PI_AV        = PI_INTR            (audio/video)
PI_NET       = PI_INTR            (network)
PI_DISK      = PI_INTR            (block storage)
PI_TTY       = PI_INTR            (terminal/serial)
PI_DULL      = PI_INTR            (low-priority hardware ithreads)
PI_SOFT      = PRI_MIN_ITHD + 8   (soft interrupts)
PI_SOFTCLOCK = PI_SOFT            (soft clock)
PI_SWI(c)    = PI_SOFT            (per-category SWI)
```

Un lector que observe esta lista notará que la mayoría de los alias de "interrupción de hardware" (`PI_AV`, `PI_NET`, `PI_DISK`, `PI_TTY`, `PI_DULL`) se resuelven al mismo valor numérico (`PI_INTR`). El comentario al inicio de ese bloque en `priority.h` lo explica de forma explícita: "Most hardware interrupt threads run at the same priority, but can decay to lower priorities if they run for full time slices". Los nombres de categoría existen porque cada uno resulta natural en el lugar donde se usa, no porque las prioridades numéricas difieran.

Solo `PI_REALTIME` (ligeramente por encima de `PI_INTR`) y `PI_SOFT` (por debajo de `PI_INTR`) son realmente distintos del nivel común de interrupción de hardware.

La prioridad del ithread proviene del flag `INTR_TYPE_*`; la prioridad del taskqueue se establece de forma explícita. Pasar `PI_NET` a `taskqueue_start_threads` sitúa al worker en el mismo nivel nominal que un ithread de red, que es la elección correcta para trabajo que coopera con la gestión de interrupciones a frecuencia de red. Un driver de almacenamiento pasaría `PI_DISK`; un driver de fondo de baja prioridad pasaría `PI_DULL`. Dado que todas las constantes se asignan al mismo valor numérico, los nombres son intercambiables en la práctica en cuanto a corrección. Sin embargo, siguen siendo relevantes para la legibilidad y para cualquier kernel futuro en el que la distinción se vuelva real.



## Referencia: un breve recorrido por /usr/src/sys/kern/kern_intr.c

Un lector curioso sobre lo que ocurre detrás de `bus_setup_intr(9)` y `bus_teardown_intr(9)` puede abrir `/usr/src/sys/kern/kern_intr.c`. El archivo tiene unas 1800 líneas y cuenta con secciones bien diferenciadas:

- **Gestión de intr_event** (`intr_event_create`, `intr_event_destroy`): creación y limpieza de alto nivel de la estructura `intr_event`.
- **Gestión de handlers** (`intr_event_add_handler`, `intr_event_remove_handler`): las operaciones subyacentes a las que llaman `bus_setup_intr` y `bus_teardown_intr`.
- **Despacho** (`intr_event_handle`, `intr_event_schedule_thread`): el código que se ejecuta realmente cuando se produce una interrupción.
- **Detección de tormentas de interrupciones** (`intr_event_handle`): la lógica de `intr_storm_threshold`.
- **Creación y planificación de ithreads** (`ithread_create`, `ithread_loop`, `ithread_update`): la maquinaria de ithread por evento.
- **Gestión de SWI (interrupciones software)** (`swi_add`, `swi_sched`, `swi_remove`): interrupciones software.

No es necesario entender el archivo completo para escribir un driver. Recorrer la lista de funciones de alto nivel y leer los comentarios sobre `intr_event_handle` (la función de despacho) es media hora bien invertida.

### Funciones clave en kern_intr.c

| Función | Propósito |
|----------|---------|
| `intr_event_create` | Asigna un nuevo `intr_event`. |
| `intr_event_destroy` | Libera un `intr_event`. |
| `intr_event_add_handler` | Conecta un handler de filter/ithread. |
| `intr_event_remove_handler` | Desconecta un handler. |
| `intr_event_handle` | Despacho: se llama en cada interrupción. |
| `intr_event_schedule_thread` | Despierta el ithread. |
| `ithread_loop` | El cuerpo de un ithread. |
| `swi_add` | Registra una interrupción software. |
| `swi_sched` | Planifica una interrupción software. |

Las funciones BUS_* expuestas a los drivers (`bus_setup_intr`, `bus_teardown_intr`, `bus_bind_intr`, `bus_describe_intr`) invocan estas funciones internas del kernel tras los hooks específicos de plataforma del bus driver.







## En resumen

El Capítulo 19 le dio oídos al driver. Al principio, `myfirst` en la versión `1.1-pci` estaba conectado a un dispositivo PCI real pero no lo escuchaba: cada acción que realizaba el driver era iniciada desde el espacio de usuario, y los propios eventos asíncronos del dispositivo (si existían) pasaban desapercibidos. Al final, `myfirst` en la versión `1.2-intr` dispone de un handler de filter conectado a la línea IRQ del dispositivo, un pipeline de tareas diferidas que gestiona el trabajo principal en contexto de thread, un camino de interrupción simulada para pruebas en entornos de laboratorio, una disciplina de IRQ compartida que coexiste con otros drivers en la misma línea, un desmontaje limpio que libera cada recurso en el orden correcto, y un nuevo archivo `myfirst_intr.c` junto con el documento `INTERRUPTS.md`.

La transición recorrió ocho secciones. La sección 1 introdujo las interrupciones a nivel de hardware, cubriendo la señalización por flanco y por nivel, el flujo de despacho de la CPU y las seis obligaciones del handler de un driver. La sección 2 presentó el modelo de kernel de FreeBSD: `intr_event`, `intr_handler`, ithread, la división filter más ithread, `INTR_MPSAFE` y las restricciones del contexto de filter. La sección 3 escribió el filter mínimo y el cableado de attach/detach. La sección 4 amplió el filter con decodificación de estado, confirmación bit a bit y trabajo diferido basado en taskqueue. La sección 5 añadió el sysctl de interrupción simulada que permite al lector ejercitar el pipeline sin eventos IRQ reales. La sección 6 codificó la disciplina de IRQ compartida: comprobar la propiedad, devolver `FILTER_STRAY` correctamente, gestionar los bits no reconocidos de forma defensiva. La sección 7 consolidó el desmontaje: enmascarar en el dispositivo, desmontar el handler, drenar el taskqueue, liberar recursos. La sección 8 refactorizó el conjunto en una estructura mantenible.

Lo que el Capítulo 19 no cubrió es MSI, MSI-X ni DMA. El camino de interrupción del driver es un único IRQ legacy; el camino de datos no usa DMA; el trabajo diferido es una única tarea de taskqueue. El Capítulo 20 introduce MSI y MSI-X (múltiples vectores, filters por vector, enrutamiento de interrupciones más rico). Los Capítulos 20 y 21 introducen DMA y la interacción entre interrupciones y anillos de descriptores DMA.

Lo que el Capítulo 19 logró es la división entre dos hilos de control. El filter del driver es breve, se ejecuta en el contexto primario de interrupción y gestiona el trabajo urgente por interrupción. La tarea diferida del driver es más larga, se ejecuta en contexto de thread y gestiona el trabajo principal por evento. La disciplina que los mantiene cooperando (atómicos para el estado del filter, sleep locks para el estado de la tarea, orden estricto para el desmontaje) es la disciplina que asume el código de interrupciones de todos los capítulos posteriores.

La estructura de archivos ha crecido: `myfirst.c`, `myfirst_hw.c`, `myfirst_hw_pci.c`, `myfirst_hw.h`, `myfirst_sim.c`, `myfirst_sim.h`, `myfirst_pci.c`, `myfirst_pci.h`, `myfirst_intr.c`, `myfirst_intr.h`, `myfirst_sync.h`, `cbuf.c`, `cbuf.h`, `myfirst.h`. La documentación ha crecido: `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`. La suite de pruebas ha crecido: el pipeline de interrupciones simuladas, el script de regresión de la Etapa 4 y un puñado de ejercicios de desafío para que el lector siga practicando.

### Una reflexión antes del Capítulo 20

Una pausa antes del siguiente capítulo. El Capítulo 19 enseñó el patrón filter más tarea, la promesa de `INTR_MPSAFE`, las restricciones del contexto de interrupción y la disciplina de IRQ compartida. Los patrones que has practicado aquí (leer el estado, confirmar, diferir el trabajo, devolver el `FILTER_*` correcto, desmontar limpiamente) son patrones que usa todo handler de interrupciones de FreeBSD. El Capítulo 20 añadirá MSI-X encima; el Capítulo 21 añadirá DMA encima. Ninguno de esos capítulos reemplaza los patrones del Capítulo 19; ambos los amplían.

Vale la pena hacer una segunda observación. La combinación de la simulación del Capítulo 17, el attach PCI real del Capítulo 18 y la gestión de interrupciones del Capítulo 19 constituye ahora un driver completo en el sentido arquitectónico. Un lector que comprenda las tres capas puede abrir cualquier driver PCI de FreeBSD y reconocer las partes: el mapa de registros, el attach PCI, el filter de interrupciones. Los detalles varían; la estructura es constante. Ese reconocimiento es lo que hace que la inversión del libro rinda frutos a lo largo de todo el árbol de código fuente de FreeBSD.

Tercera observación: el rendimiento de la capa de acceso del Capítulo 16 continúa. Los macros `CSR_*` no cambiaron en el Capítulo 19; los macros `ICSR_*` se añadieron para su uso en el contexto de filter, pero invocan el mismo `bus_read_4` y `bus_write_4` subyacentes. La abstracción ha rendido frutos tres veces: frente al backend de simulación del Capítulo 17, frente al BAR PCI real del Capítulo 18 y frente al contexto de filter del Capítulo 19. Un lector que construya capas de acceso similares en sus propios drivers encontrará el mismo dividendo.

### Qué hacer si te has atascado

Tres sugerencias.

En primer lugar, céntrate en el camino de interrupción simulada. Si `sudo sysctl dev.myfirst.0.intr_simulate=1` hace que los contadores aumenten y la tarea se ejecute, el pipeline está funcionando. Todas las demás partes del capítulo son opcionales en el sentido de que decoran el pipeline, pero si el pipeline falla, el capítulo entero no está funcionando y la Sección 5 es el lugar adecuado para diagnosticar.

En segundo lugar, abre `/usr/src/sys/dev/mgb/if_mgb.c` y vuelve a leer despacio la función `mgb_legacy_intr`. Son unas sesenta líneas de código de filter. Cada línea se corresponde con un concepto del Capítulo 19. Leerlo una vez tras completar el capítulo debería resultar terreno conocido.

En tercer lugar, omite los desafíos en la primera lectura. Los laboratorios están calibrados para el ritmo del Capítulo 19; los desafíos suponen que el material del capítulo está bien asentado. Vuelve a ellos después del Capítulo 20 si ahora te parecen inalcanzables.

El objetivo del Capítulo 19 era dar al driver una forma de escuchar a su dispositivo. Si lo ha conseguido, la maquinaria MSI-X del Capítulo 20 se convierte en una especialización en lugar de un tema completamente nuevo, y el DMA del Capítulo 21 pasa a ser una cuestión de conectar las finalizaciones de descriptor al camino de interrupción que ya tienes.



## Puente hacia el Capítulo 20

El Capítulo 20 se titula *Gestión avanzada de interrupciones*. Su alcance es la especialización que el Capítulo 19 deliberadamente no abordó: MSI (Message Signaled Interrupts) y MSI-X, los mecanismos modernos de interrupción de PCIe que reemplazan la línea INTx legacy por vectores por dispositivo (o por cola) entregados como escrituras en memoria.

El Capítulo 19 preparó el terreno de cuatro formas concretas.

Primero, **tienes un filter handler funcional**. El filter del capítulo 19 lee el estado, gestiona los bits, acusa recibo y difiere el trabajo. El filter del capítulo 20 es similar, pero se replica por vector: cada vector MSI-X tiene su propio filter, y cada uno gestiona un subconjunto específico de los eventos del dispositivo.

Segundo, **comprendes la cascada de attach/detach**. El capítulo 19 amplió la cascada con dos etiquetas (`fail_release_irq`, `fail_teardown_intr`). El capítulo 20 la amplía aún más: un par de etiquetas por vector. El patrón no cambia; lo que cambia es la cantidad.

Tercero, **tienes una disciplina de teardown de interrupciones**. El capítulo 20 reutiliza el orden del capítulo 19: limpiar las interrupciones en el dispositivo, `bus_teardown_intr` para cada vector, `bus_release_resource` para cada recurso IRQ. La naturaleza por vector añade un pequeño bucle; el orden es el mismo.

Cuarto, **tienes un entorno de laboratorio que expone MSI-X**. En QEMU con `virtio-rng-pci`, MSI-X está disponible; en bhyve con `virtio-rnd`, solo se expone la interrupción INTx heredada. Los laboratorios del capítulo 20 pueden requerir cambiar a QEMU o a un dispositivo de bhyve con emulación más completa para ejercitar la ruta MSI-X.

Los temas concretos que cubrirá el capítulo 20 son:

- Por qué MSI y MSI-X suponen una mejora respecto a la interrupción INTx heredada.
- En qué se diferencia MSI de MSI-X (vector único frente a tabla de vectores).
- `pci_alloc_msi(9)`, `pci_alloc_msix(9)`: asignación de vectores.
- `pci_msi_count(9)`, `pci_msix_count(9)`: consulta de capacidades.
- `pci_release_msi(9)`: la contrapartida en el proceso de teardown.
- Handlers de interrupción con múltiples vectores: filters por cola.
- La distribución de la tabla MSI-X y cómo acceder a entradas específicas.
- Afinidad de CPU entre vectores para sistemas NUMA.
- Coalescencia de interrupciones: reducir la tasa de interrupciones cuando el dispositivo lo permite.
- Interacción entre MSI-X e iflib (el framework moderno de drivers de red).
- Migración del driver `myfirst` del capítulo 19 desde la ruta heredada a una ruta MSI-X, con un fallback a la ruta heredada para dispositivos que no soportan MSI-X.

No es necesario que leas más adelante. El capítulo 19 es preparación suficiente. Trae tu driver `myfirst` en la versión `1.2-intr`, tu `LOCKING.md`, tu `INTERRUPTS.md`, tu kernel con WITNESS activado y tu script de regresión. El capítulo 20 comienza donde terminó el capítulo 19.

El capítulo 21 está un capítulo más adelante; merece una breve referencia hacia adelante. DMA introducirá otra interacción con las interrupciones: interrupciones de completación que señalizan que la entrada N del anillo de descriptores está lista. La disciplina de filter más tarea que enseñó el capítulo 19 se mantiene; el trabajo de la tarea consiste ahora en recorrer un anillo de descriptores en lugar de leer un único registro.

El vocabulario es tuyo; la estructura es tuya; la disciplina es tuya. El capítulo 20 aporta precisión a los tres.

## Referencia: Tarjeta de consulta rápida del capítulo 19

Un resumen compacto del vocabulario, las APIs, las macros y los procedimientos que introdujo el capítulo 19.

### Vocabulario

- **Interrupt**: un evento asíncrono señalizado por hardware.
- **IRQ (Interrupt Request)**: el identificador de una línea de interrupción.
- **Edge-triggered**: señalizado por una transición; una interrupción por transición.
- **Level-triggered**: señalizado por un nivel mantenido; una interrupción se activa mientras se mantiene el nivel.
- **intr_event**: la estructura del kernel de FreeBSD para una fuente de interrupción.
- **ithread**: el thread del kernel de FreeBSD que ejecuta los manejadores de interrupción diferidos.
- **filter handler**: una función que se ejecuta en el contexto primario de interrupción.
- **ithread handler**: una función que se ejecuta en contexto de thread tras el filtro.
- **FILTER_HANDLED**: el filtro gestionó la interrupción; no se necesita ithread.
- **FILTER_SCHEDULE_THREAD**: el filtro gestionó parcialmente la interrupción; ejecutar el ithread.
- **FILTER_STRAY**: la interrupción no era para este driver.
- **INTR_MPSAFE**: un indicador que promete que el manejador realiza su propia sincronización.
- **INTR_TYPE_*** (TTY, BIO, NET, CAM, MISC, CLK, AV): indicaciones de categoría del manejador.
- **INTR_EXCL**: interrupción exclusiva.

### APIs esenciales

- `bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, flags)`: reclamar un IRQ.
- `bus_release_resource(dev, SYS_RES_IRQ, rid, res)`: liberar el IRQ.
- `bus_setup_intr(dev, res, flags, filter, ihand, arg, &cookie)`: registrar un manejador.
- `bus_teardown_intr(dev, res, cookie)`: dar de baja el manejador.
- `bus_describe_intr(dev, res, cookie, "name")`: nombrar el manejador para las herramientas.
- `bus_bind_intr(dev, res, cpu)`: enrutar la interrupción a una CPU.
- `pci_msi_count(dev)`, `pci_msix_count(dev)` (capítulo 20).
- `pci_alloc_msi(dev, &count)`, `pci_alloc_msix(dev, &count)` (capítulo 20).
- `pci_release_msi(dev)` (capítulo 20).
- `taskqueue_create("name", M_WAITOK, taskqueue_thread_enqueue, &tq)`: crear un taskqueue.
- `taskqueue_start_threads(&tq, n, PI_pri, "thread name")`: iniciar los workers.
- `taskqueue_enqueue(tq, &task)`: encolar una tarea.
- `taskqueue_drain(tq, &task)`: esperar a que una tarea termine e impedir nuevas encoladas.
- `taskqueue_free(tq)`: liberar el taskqueue.
- `TASK_INIT(&task, pri, fn, arg)`: inicializar una tarea.

### Macros esenciales

- `FILTER_HANDLED`, `FILTER_STRAY`, `FILTER_SCHEDULE_THREAD`.
- `INTR_TYPE_TTY`, `INTR_TYPE_BIO`, `INTR_TYPE_NET`, `INTR_TYPE_CAM`, `INTR_TYPE_MISC`, `INTR_TYPE_CLK`, `INTR_TYPE_AV`.
- `INTR_MPSAFE`, `INTR_EXCL`.
- `RF_SHAREABLE`, `RF_ACTIVE`.
- `SYS_RES_IRQ`.

### Procedimientos habituales

**Asignar una interrupción PCI legacy y registrar un filter handler:**

1. `sc->irq_rid = 0;`
2. `sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid, RF_SHAREABLE | RF_ACTIVE);`
3. `bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC | INTR_MPSAFE, filter, NULL, sc, &sc->intr_cookie);`
4. `bus_describe_intr(dev, sc->irq_res, sc->intr_cookie, "name");`

**Desmontar un manejador de interrupción:**

1. Deshabilitar las interrupciones en el dispositivo (borrar `INTR_MASK`).
2. `bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);`
3. `taskqueue_drain(sc->intr_tq, &sc->intr_data_task);`
4. `taskqueue_free(sc->intr_tq);`
5. `bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);`

**Escribir un filter handler:**

1. Leer `INTR_STATUS`; si es cero, devolver `FILTER_STRAY`.
2. Para cada bit reconocido, incrementar un contador, confirmar la interrupción escribiendo de vuelta y, opcionalmente, encolar una tarea.
3. Devolver `FILTER_HANDLED` (o `FILTER_SCHEDULE_THREAD`), o `FILTER_STRAY` si no se reconoció nada.

### Comandos útiles

- `vmstat -i`: listar fuentes de interrupción con sus contadores.
- `devinfo -v`: listar dispositivos con sus recursos IRQ.
- `sysctl hw.intrcnt` y `sysctl hw.intrnames`: contadores en bruto.
- `sysctl hw.intr_storm_threshold`: activar la detección de tormentas de interrupciones del kernel.
- `cpuset -g`: consultar la afinidad de CPU de las interrupciones (depende de la plataforma).
- `sudo sysctl dev.myfirst.0.intr_simulate=1`: lanzar una interrupción simulada.

### Archivos que conviene tener a mano

- `/usr/src/sys/sys/bus.h`: `driver_filter_t`, `driver_intr_t`, `FILTER_*`, `INTR_*`.
- `/usr/src/sys/kern/kern_intr.c`: la maquinaria de eventos de interrupción del kernel.
- `/usr/src/sys/sys/taskqueue.h`: la API de taskqueue.
- `/usr/src/sys/dev/mgb/if_mgb.c`: un ejemplo legible de filtro más tarea.
- `/usr/src/sys/dev/ath/if_ath_pci.c`: una configuración mínima de interrupción solo con ithread.



## Referencia: Tabla comparativa del conjunto de la parte 4

Un resumen compacto de dónde encaja cada capítulo de la parte 4, qué aporta y qué presupone. Es útil para los lectores que se incorporan en mitad de la parte o que vuelven a ella.

| Tema | Cap. 16 | Cap. 17 | Cap. 18 | Cap. 19 | Cap. 20 (vista previa) | Cap. 21 (vista previa) |
|-------|-------|-------|-------|-------|------------------|------------------|
| Acceso a BAR | Simulado con malloc | Extendido con la capa de simulación | BAR PCI real | Igual | Igual | Igual |
| Simulación del cap. 17 | N/D | Introducida | Inactiva en PCI | Inactiva en PCI | Inactiva en PCI | Inactiva en PCI |
| PCI attach | N/D | N/D | Introducido | Igual + IRQ | Opción MSI-X | Inicialización DMA añadida |
| Gestión de interrupciones | N/D | N/D | N/D | Introducida | MSI-X por vector | Basada en completaciones |
| DMA | N/D | N/D | N/D | N/D | Vista previa | Introducido |
| Versión | 0.9-mmio | 1.0-simulated | 1.1-pci | 1.2-intr | 1.3-msi | 1.4-dma |
| Nuevo archivo | `myfirst_hw.c` | `myfirst_sim.c` | `myfirst_pci.c` | `myfirst_intr.c` | `myfirst_msix.c` | `myfirst_dma.c` |
| Disciplina clave | Abstracción de accesores | Dispositivo simulado | Newbus attach | División filtro/tarea | Manejadores por vector | Mapas DMA |

La tabla hace visible de un vistazo la estructura acumulativa del libro. Un lector que comprenda la fila correspondiente a un tema determinado puede anticipar cómo encaja el trabajo del capítulo 19 en el conjunto.



## Referencia: Páginas del manual de FreeBSD para el capítulo 19

Una lista de las páginas del manual más útiles para el material del capítulo 19. Ábrelas con `man 9 <name>` (para las APIs del kernel) o `man 4 <name>` (para los resúmenes de subsistemas) en un sistema FreeBSD.

### Páginas del manual de la API del kernel

- **`bus_setup_intr(9)`**: registro de un manejador de interrupción.
- **`bus_teardown_intr(9)`**: desmontaje de un manejador.
- **`bus_bind_intr(9)`**: vinculación a una CPU.
- **`bus_describe_intr(9)`**: etiquetado de un manejador.
- **`bus_alloc_resource(9)`**: asignación de recursos (genérica).
- **`bus_release_resource(9)`**: liberación de recursos.
- **`atomic(9)`**: operaciones atómicas, incluida `atomic_add_64`.
- **`taskqueue(9)`**: primitivas de taskqueue.
- **`ppsratecheck(9)`**: auxiliar de registro con límite de frecuencia.
- **`swi_add(9)`**: interrupciones software (mencionadas como alternativa).
- **`intr_event(9)`**: maquinaria de eventos de interrupción (si está disponible; algunas APIs son internas).

### Páginas del manual del subsistema de dispositivos

- **`pci(4)`**: subsistema PCI.
- **`vmstat(8)`**: `vmstat -i` para observar las interrupciones.
- **`devinfo(8)`**: árbol de dispositivos y recursos.
- **`devctl(8)`**: control de dispositivos en tiempo de ejecución.
- **`sysctl(8)`**: lectura y escritura de sysctls.
- **`dtrace(1)`**: trazado dinámico.

La mayoría de estas páginas se han mencionado en el cuerpo del capítulo. Esta lista consolidada está pensada para los lectores que quieren tenerlas todas en un solo lugar.



## Referencia: Frases memorables del driver

Algunos aforismos que resumen la disciplina del capítulo 19. Son útiles tanto para la lectura como para la revisión de código.

- **"Lee, confirma, difiere, retorna."** Las cuatro cosas que hace un filtro.
- **"FILTER_STRAY si no reconociste nada."** El protocolo de IRQ compartida.
- **"Enmascara antes del desmontaje; desmonta antes de liberar."** El orden en detach.
- **"El contexto del filtro es solo para spinlocks."** La regla de los locks sin suspensión.
- **"Toda encolada necesita un drain antes del free."** El ciclo de vida del taskqueue.
- **"Un filtro, un dispositivo, un estado."** El aislamiento que mantiene coherente el código por dispositivo.
- **"Si WITNESS entra en pánico, créelo."** El kernel de depuración detecta errores sutiles.
- **"PROD primero, interrupción después."** Programa el dispositivo (`INTR_MASK`) antes de habilitar el manejador.
- **"Poco en el filtro; mucho en la tarea."** La disciplina sobre el tamaño del trabajo.
- **"La detección de tormentas es una red de seguridad, no una herramienta de diseño."** No dependas del limitador del kernel.

Ninguno de estos aforismos es una especificación completa. Cada uno es un recordatorio compacto que se despliega en el tratamiento detallado del capítulo.



## Referencia: Glosario de términos del capítulo 19

**ack (acknowledge)**: la operación de escribir de vuelta en INTR_STATUS para borrar el bit pendiente y desactivar la línea IRQ.

**driver_filter_t**: el typedef de C para una función filter-handler: `int f(void *)`.

**driver_intr_t**: el typedef de C para una función ithread handler: `void f(void *)`.

**edge-triggered**: un modo de señalización de interrupción en el que la interrupción se activa por una transición de nivel.

**FILTER_HANDLED**: valor de retorno de un filtro que significa "esta interrupción está gestionada; no se necesita ithread".

**FILTER_SCHEDULE_THREAD**: valor de retorno que significa "planificar la ejecución del ithread".

**FILTER_STRAY**: valor de retorno que significa "esta interrupción no es para este driver".

**filter handler**: una función C que se ejecuta en el contexto primario de interrupción.

**Giant**: el lock global único del kernel heredado; los drivers modernos lo evitan estableciendo INTR_MPSAFE.

**IE (interrupt event)**: abreviatura de `intr_event`.

**INTR_MPSAFE**: un indicador que promete que el manejador realiza su propia sincronización y es seguro sin Giant.

**INTR_STATUS**: el registro del dispositivo que registra las causas de interrupción pendientes (RW1C).

**INTR_MASK**: el registro del dispositivo que habilita clases específicas de interrupción.

**intr_event**: estructura del kernel que representa una fuente de interrupción.

**ithread**: thread de interrupción del kernel; ejecuta los manejadores diferidos en contexto de thread.

**level-triggered**: un modo de señalización de interrupción en el que la interrupción se activa mientras se mantiene el nivel.

**MSI**: Message Signaled Interrupts; un mecanismo PCIe (capítulo 20).

**MSI-X**: la variante más completa de MSI con una tabla de vectores (capítulo 20).

**primary interrupt context**: el contexto de un filter handler; sin suspensión ni sleep locks.

**PCIR_INTLINE / PCIR_INTPIN**: campos del espacio de configuración PCI que especifican la línea IRQ legacy y el pin.

**RF_ACTIVE**: indicador de asignación de recursos; activa el recurso en un solo paso.

**RF_SHAREABLE**: indicador de asignación de recursos; permite compartir el recurso con otros drivers.

**stray interrupt**: una interrupción para la que ningún filtro devolvió una reclamación; el kernel la contabiliza por separado.

**storm**: una situación en la que una interrupción level-triggered se activa de forma continua porque el driver no la confirma.

**SYS_RES_IRQ**: tipo de recurso para interrupciones.

**taskqueue**: una primitiva del kernel para ejecutar trabajo diferido en contexto de thread.

**trap stub**: el pequeño fragmento de código del kernel que se ejecuta cuando la CPU toma un vector de interrupción.

**EOI (End of Interrupt)**: la señal enviada al controlador de interrupciones para rearmar la línea IRQ.



## Referencia: Una nota final sobre la filosofía del manejo de interrupciones

Un párrafo pensado para cerrar el capítulo, que merece ser revisado tras los laboratorios.

El trabajo de un manejador de interrupción no es hacer el trabajo del dispositivo. El trabajo del dispositivo (procesar un paquete, terminar una operación de I/O, leer un sensor) lo realiza el resto del driver, en contexto de thread y bajo el conjunto completo de locks del driver. El trabajo del manejador es más acotado: advertir que el dispositivo tiene algo que comunicar, confirmar el dispositivo para que la conversación pueda continuar, planificar el trabajo real que se llevará a cabo más adelante, y retornar con la suficiente rapidez para que la CPU quede libre para el thread interrumpido o para la siguiente interrupción.

Un lector que haya escrito el driver del capítulo 19 habrá escrito un manejador de interrupción. Es pequeño. Lo que lo hace útil es el resto del driver. El capítulo 20 especializará el manejador para el trabajo por vector en MSI-X. El capítulo 21 especializará la tarea para recorrer un anillo de descriptores DMA. Cada uno de ellos es una extensión, no un reemplazo. El manejador del capítulo 19 es el esqueleto sobre el que ambos se construyen.

La habilidad que enseña el capítulo 19 no es "cómo gestionar interrupciones para el dispositivo virtio-rnd". Es "cómo dividir el trabajo entre el contexto primario y el contexto de thread, cómo respetar las restricciones del filtro, cómo desmontar de forma limpia y cómo cooperar con otros drivers en una línea compartida". Cada una de ellas es una habilidad transferible. Todos los drivers del árbol de FreeBSD ejercitan algunas de ellas; la mayoría las ejercitan todas.

Para este lector y para los futuros lectores de este libro, el filtro y la tarea del Capítulo 19 son una parte permanente de la arquitectura del driver `myfirst`. Todos los capítulos posteriores los dan por sentado. Todos los capítulos posteriores los amplían. La complejidad global del driver irá creciendo, pero la ruta de la interrupción seguirá siendo lo que el Capítulo 19 hizo de ella: un fragmento de código breve, rápido y correctamente ordenado que cede el paso para que el resto del driver pueda hacer su trabajo.
