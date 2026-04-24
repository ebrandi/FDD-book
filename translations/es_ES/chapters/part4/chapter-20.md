---
title: "Gestión avanzada de interrupciones"
description: "El capítulo 20 amplía el driver de interrupciones del capítulo 19 con soporte para MSI y MSI-X. Enseña la diferencia entre INTx heredado, MSI y MSI-X; cómo consultar el número de vectores disponibles con pci_msi_count(9) y pci_msix_count(9); cómo asignar vectores con pci_alloc_msi(9) y pci_alloc_msix(9); cómo construir la cadena de respaldo desde MSI-X hasta INTx heredado; cómo registrar manejadores de filtro por vector con funciones driver_filter_t independientes; cómo diseñar estructuras de datos por vector seguras en contexto de interrupción; cómo asignar a cada vector un rol específico y una afinidad de CPU concreta; y cómo desmontar de forma segura un driver multivector. El driver evoluciona de 1.2-intr a 1.3-msi, incorpora un nuevo archivo específico para msix y deja el capítulo 20 preparado para abordar DMA en el capítulo 21."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 20
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 165
language: "es-ES"
---
# Gestión avanzada de interrupciones

## Guía del lector y objetivos

El capítulo 19 finalizó con un driver capaz de escuchar a su dispositivo. El módulo `myfirst` en la versión `1.2-intr` tiene un manejador de filtro registrado en la línea PCI INTx heredada, una tarea de trabajo diferido en un taskqueue, un sysctl de interrupción simulada para pruebas en el entorno de laboratorio bhyve, un orden de desmontaje estricto y un nuevo archivo `myfirst_intr.c` que mantiene el código de interrupción ordenado. La disciplina de locking del capítulo 11 se mantiene: atómicos en el filtro, sleep locks en la tarea, `INTR_MPSAFE` en el manejador y seguridad para IRQ compartida mediante `FILTER_STRAY`. El driver se comporta como un driver real pequeño que, por casualidad, tiene una única fuente de interrupciones.

Lo que el driver todavía no hace es aprovechar todo lo que PCIe ofrece. Un dispositivo PCIe moderno no necesita compartir una única línea con sus vecinos. Puede solicitar una interrupción dedicada a través del mecanismo de señalización por mensaje que introdujo PCI 2.2 (MSI) o la tabla por función más completa que MSI-X añadió en PCIe. Un dispositivo con varias colas (una NIC con colas de recepción y transmisión, un controlador NVMe con colas de administración y de envío de I/O, un controlador de host USB3 moderno con colas de eventos) normalmente necesita una interrupción por cola en lugar de una única interrupción compartida para todo el dispositivo. El capítulo 20 enseña al driver cómo solicitarlo.

El alcance del capítulo es exactamente esta transición: qué son MSI y MSI-X a nivel de hardware, cómo los representa FreeBSD en términos de recursos IRQ adicionales, cómo un driver consulta el número de capacidades y asigna vectores, cómo funciona en la práctica la jerarquía de respaldo desde MSI-X hasta MSI y hasta INTx heredado, cómo registrar varias funciones de filtro diferentes en el mismo dispositivo, cómo diseñar estructuras de datos por vector para que el manejador de cada vector acceda únicamente a su propio estado, cómo asignar a cada vector una afinidad de CPU que coincida con la ubicación NUMA del dispositivo, cómo etiquetar cada vector con `bus_describe_intr(9)` para que `vmstat -i` indique al operador qué hace cada vector, y cómo desmontar todo esto en el orden correcto. El capítulo no llega hasta DMA, que es el capítulo 21; los vectores de recepción y transmisión por cola resultan especialmente valiosos una vez que hay un anillo de descriptores en escena, pero enseñar ambos a la vez diluiría los dos.

El capítulo 20 mantiene varios temas adyacentes a distancia. DMA completo (etiquetas de `bus_dma(9)`, anillos de descriptores, bounce buffers, coherencia de caché) corresponde al capítulo 21. El framework de múltiples colas de Iflib, que envuelve MSI-X con una capa de maquinaria iflib por cola, es un tema de la Parte 6 (capítulo 28) para los lectores que quieran seguir el camino de red al estilo iflib. Las operaciones más completas de la tabla de máscaras MSI-X por función (dirigir direcciones de mensaje específicas a CPUs concretas directamente a través de la tabla MSI-X) se describen pero no se implementan de principio a fin. La reasignación de interrupciones específica de la plataforma a través del IOMMU, el uso compartido de vectores SR-IOV y la recuperación de interrupciones basada en PCIe AER se dejan para capítulos posteriores. El capítulo 20 se ciñe al terreno que puede cubrir bien y cede el paso explícitamente cuando un tema merece su propio capítulo.

El trabajo con múltiples vectores se apoya en todas las capas anteriores de la Parte 4. El capítulo 16 le dio al driver un vocabulario de acceso a registros. El capítulo 17 le enseñó a pensar como un dispositivo. El capítulo 18 lo presentó ante un dispositivo PCI real. El capítulo 19 le dio oídos en un único IRQ. El capítulo 20 le da un conjunto de oídos, uno por cada conversación que el dispositivo quiere mantener. El capítulo 21 enseñará a esos oídos a cooperar con la capacidad propia del dispositivo para acceder a la RAM. Cada capítulo añade una capa. Cada capa depende de las anteriores. El capítulo 20 es donde el driver deja de fingir que el dispositivo tiene solo una cosa que decir y empieza a tratarlo como la máquina de múltiples colas que realmente es.

### Por qué MSI-X merece un capítulo propio

En este punto puede que te estés preguntando por qué MSI y MSI-X necesitan un capítulo propio. El driver del capítulo 19 tiene un manejador de interrupciones funcional en la línea IRQ heredada. Si el pipeline de filtro más tarea ya funciona correctamente, ¿por qué no seguir usándolo? ¿Justifica realmente MSI-X un capítulo completo de material nuevo?

Tres razones.

La primera es la escala. Una única línea IRQ en un sistema compartido obliga a todos los drivers de esa línea a serializar a través de un único `intr_event`. En un host con decenas de dispositivos PCIe, el mecanismo INTx heredado crearía un cuello de botella en todo el sistema si fuera la única opción. MSI-X permite que cada dispositivo (y cada cola dentro de un dispositivo) tenga su propio `intr_event` dedicado, atendido por su propio ithread o manejador de filtro, anclado a su propia CPU. La diferencia entre un servidor moderno gestionando diez millones de paquetes por segundo con MSI-X y la misma carga de trabajo con INTx heredado es la diferencia entre "posible" e "imposible"; MSI-X es lo que hace que lo primero sea una realidad.

La segunda es la localidad. Con una única línea de interrupción, el kernel tiene una sola opción de CPU a la que enrutar la interrupción, y esa elección es global para el dispositivo. Con MSI-X, cada vector puede anclarse a una CPU diferente, y los buenos drivers anclan cada vector a una CPU que es local en NUMA respecto a la cola que atiende. Las ventajas en términos de líneas de caché son reales: una cola de recepción cuya interrupción se dispara en la misma CPU que finalmente consume el paquete evita el tráfico de caché entre sockets que predomina en las configuraciones heredadas.

La tercera es la limpieza. Incluso para un driver que no necesita alto rendimiento, MSI o MSI-X pueden simplificar el manejador. Con una línea dedicada, el filtro no necesita gestionar el caso de IRQ compartida. Con un vector dedicado por clase de evento (administración, recepción, transmisión, error), cada manejador es más pequeño y especializado, y el driver completo resulta más fácil de leer. Los buenos drivers usan MSI-X incluso cuando el rendimiento no lo requiere, porque el código mejora.

El capítulo 20 se gana su lugar enseñando los tres beneficios de manera concreta. El lector termina el capítulo siendo capaz de asignar vectores, enrutarlos, describirlos y desmontarlos, con un driver funcional que demuestra el patrón de principio a fin.

### Dónde dejó el driver el capítulo 19

Un breve repaso del punto en que deberías estar. El capítulo 20 extiende el driver producido al final de la Etapa 4 del capítulo 19, etiquetado como versión `1.2-intr`. Si alguno de los puntos siguientes te resulta incierto, vuelve al capítulo 19 antes de comenzar este.

- Tu driver compila sin errores y se identifica como `1.2-intr` en `kldstat -v`.
- En un guest bhyve o QEMU que expone un dispositivo virtio-rnd, el driver realiza el attach, asigna BAR 0 como `SYS_RES_MEMORY`, asigna el IRQ heredado como `SYS_RES_IRQ` con `rid = 0`, registra un manejador de filtro a través de `bus_setup_intr(9)` con `INTR_TYPE_MISC | INTR_MPSAFE`, crea `/dev/myfirst0` y admite el sysctl `dev.myfirst.N.intr_simulate`.
- El filtro lee `INTR_STATUS`, incrementa contadores por bit, reconoce la interrupción, encola la tarea diferida para `DATA_AV` y devuelve el valor `FILTER_*` correcto.
- La tarea (`myfirst_intr_data_task_fn`) se ejecuta en contexto de thread en un taskqueue llamado `myfirst_intr` con prioridad `PI_NET`, lee `DATA_OUT`, actualiza el softc y difunde `sc->data_cv`.
- La ruta de desconexión borra `INTR_MASK`, llama a `bus_teardown_intr`, drena y libera el taskqueue, libera el recurso IRQ, desconecta la capa de hardware y libera el BAR.
- `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md` e `INTERRUPTS.md` están al día.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` están habilitados en tu kernel de pruebas.

Ese es el driver que el capítulo 20 extiende. Las adiciones son significativas en alcance: un nuevo archivo (`myfirst_msix.c`), un nuevo encabezado (`myfirst_msix.h`), varios nuevos campos en el softc para rastrear el estado por vector, una nueva familia de funciones de filtro por vector, una nueva jerarquía de respaldo en el auxiliar de configuración, llamadas a `bus_describe_intr` por vector, enlace opcional a CPU, una actualización de versión a `1.3-msi`, un nuevo documento `MSIX.md` y actualizaciones de la prueba de regresión. El modelo mental también se amplía: el driver empieza a considerar las interrupciones como un vector de fuentes en lugar de un único flujo de eventos.

### Qué aprenderás

Al pasar al siguiente capítulo, serás capaz de:

- Describir qué es una interrupción MSI y MSI-X a nivel de hardware, cómo se señaliza cada una por PCIe (como una escritura en memoria en lugar de un cambio de nivel eléctrico) y por qué los dos mecanismos coexisten con INTx heredado.
- Explicar las diferencias clave entre MSI y MSI-X: el número de vectores MSI (de 1 a 32 de un bloque contiguo), el número de vectores MSI-X (hasta 2048 vectores con direccionamiento independiente) y las capacidades de dirección y máscara por vector que ofrece MSI-X y que MSI no tiene.
- Consultar las capacidades MSI y MSI-X de un dispositivo a través de `pci_msi_count(9)` y `pci_msix_count(9)`, y saber qué significa el recuento devuelto.
- Asignar vectores MSI o MSI-X a través de `pci_alloc_msi(9)` y `pci_alloc_msix(9)`, gestionar el caso en que el kernel asigna menos vectores de los solicitados y recuperarse de fallos de asignación.
- Construir una jerarquía de respaldo de tres niveles: MSI-X primero (si está disponible), luego MSI (si MSI-X no está disponible o la asignación falla), y después INTx heredado. Cada nivel usa el mismo patrón `bus_setup_intr` en su núcleo, pero el rid y la estructura de manejador por vector difieren.
- Asignar recursos IRQ por vector con el rid correcto (rid=0 para INTx heredado; rid=1, 2, 3, ... para vectores MSI y MSI-X).
- Registrar un manejador de filtro distinto por vector para que cada vector tenga su propio propósito (administración, cola de recepción N, cola de transmisión N, error).
- Diseñar estado por vector (contadores por cola, tarea por cola, lock por cola) para que los manejadores que se ejecutan de forma concurrente en diferentes CPUs no compitan por datos compartidos.
- Describir cada vector con `bus_describe_intr(9)` para que `vmstat -i` y `devinfo -v` muestren cada vector con un nombre significativo.
- Vincular cada vector a una CPU específica con `bus_bind_intr(9)` y consultar el conjunto de CPUs locales NUMA del dispositivo con `bus_get_cpus(9)` usando `LOCAL_CPUS` o `INTR_CPUS`.
- Gestionar fallos de asignación parcial: el dispositivo tiene ocho vectores, el kernel nos dio tres; adaptar el driver para usar los tres y realizar el trabajo restante mediante sondeo o tareas programadas.
- Desmontar correctamente un driver multivector: `bus_teardown_intr` por vector, `bus_release_resource` por vector y después un único `pci_release_msi(9)` al final.
- Registrar una línea de resumen clara en dmesg en el momento del attach indicando el modo de interrupción (MSI-X / N vectores, MSI / K vectores, o INTx heredado), para que el operador vea al instante en qué nivel quedó el driver.
- Dividir el código multivector en `myfirst_msix.c`, actualizar la línea `SRCS` del módulo, etiquetar el driver como `1.3-msi` y producir `MSIX.md` documentando los propósitos por vector y los patrones de contadores observados.

La lista es larga; cada punto es concreto. El objetivo del capítulo es la composición.

### Qué no cubre este capítulo

Varios temas adyacentes se aplazan explícitamente para que el capítulo 20 mantenga su enfoque.

- **DMA.** Las etiquetas de `bus_dma(9)`, `bus_dmamap_load(9)`, las listas scatter-gather, los bounce buffers, la coherencia de caché alrededor de los descriptores DMA y la forma en que el dispositivo escribe las finalizaciones en RAM son materia del Capítulo 21. El Capítulo 20 proporciona al driver múltiples vectores; el Capítulo 21 le otorga al dispositivo la capacidad de mover datos. Cada mitad tiene valor por sí misma; juntas constituyen la columna vertebral de todo driver de alto rendimiento moderno.
- **iflib(9) y el framework de red con múltiples colas.** iflib es un framework amplio y con criterios propios que envuelve MSI-X con ithreads por cola, pools de DMA por cola y mucha maquinaria que un driver genérico no necesita. El Capítulo 20 enseña el patrón en bruto; el capítulo de redes de la Parte 6 (Capítulo 28) lo retoma con el vocabulario de iflib.
- **Recuperación de AER en PCIe mediante vectores MSI-X.** Advanced Error Reporting puede señalizar a través de su propio vector MSI-X en algunos dispositivos. El Capítulo 20 menciona esta posibilidad; el camino completo de recuperación es un tema para capítulos posteriores.
- **SR-IOV e interrupciones por función virtual.** Una función virtual de Single-Root IO Virtualization tiene su propia capacidad MSI-X y sus propios vectores por VF. El driver del Capítulo 20 es una función física; la historia de las funciones virtuales es una especialización para capítulos posteriores.
- **Ajuste de prioridad de thread por vector.** Un driver puede pasar una prioridad diferente a los flags de `bus_setup_intr` de cada vector, o usar `taskqueue_start_threads` con distintas prioridades por vector. El Capítulo 20 usa `INTR_TYPE_MISC | INTR_MPSAFE` para cada vector y no ajusta las prioridades; los capítulos de rendimiento de la Parte 7 (Capítulo 33) cubren el tema del ajuste de prioridades.
- **Transporte modern-virtio-PCI mediante capacidades de PCIe.** El driver `virtio_pci_modern(4)` coloca las notificaciones de virtqueue dentro de estructuras de capacidades y usa vectores MSI-X para las finalizaciones de virtqueue. El driver del Capítulo 20 sigue apuntando a un BAR virtio-rnd heredado; un lector que lo adapte a un dispositivo de producción real seguiría el patrón del Capítulo 20, pero leyendo desde la estructura moderna de virtio PCI.

Mantenerse dentro de esos límites hace que el Capítulo 20 sea un capítulo sobre la gestión de interrupciones con múltiples vectores. El vocabulario adquirido es lo que se transfiere; los capítulos concretos que siguen aplican ese vocabulario a DMA, iflib, AER y SR-IOV.

### Tiempo estimado de dedicación

- **Solo lectura**: cuatro o cinco horas. El modelo conceptual de MSI/MSI-X no es complejo, pero la disciplina por vector, la escalera de reserva y el tema de la afinidad de CPU merecen una lectura atenta.
- **Lectura más escritura de los ejemplos desarrollados**: diez o doce horas repartidas en dos o tres sesiones. El driver evoluciona en cuatro etapas: escalera de reserva, múltiples vectores, manejadores por vector y refactorización. Cada etapa es pequeña, pero requiere atención cuidadosa al estado por vector.
- **Lectura más todos los laboratorios y desafíos**: dieciséis a veinte horas repartidas en cuatro o cinco sesiones, incluyendo la lectura de drivers reales (`virtio_pci.c`, el código MSI-X de `if_em.c` y la separación de vectores admin+IO de `nvme.c`), la configuración de un huésped bhyve o QEMU con MSI-X expuesto, y la ejecución de la prueba de regresión del capítulo.

Las secciones 3, 5 y 6 son las más densas. Si el patrón de manejador por vector resulta poco familiar en la primera lectura, es normal. Detente, vuelve a leer el diagrama de la Sección 3 y continúa cuando el esquema se haya asentado.

### Requisitos previos

Antes de comenzar este capítulo, confirma lo siguiente:

- Tu código fuente del driver coincide con el Capítulo 19 Etapa 4 (`1.2-intr`). El punto de partida asume todos los elementos del Capítulo 19: el pipeline de filtro más tarea, el sysctl de interrupción simulada, los macros de acceso `ICSR_*` y el desmontaje limpio.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con `/usr/src` en disco y coincidiendo con el kernel en ejecución.
- Un kernel de depuración con `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` está compilado, instalado y arrancando limpiamente.
- `bhyve(8)` o `qemu-system-x86_64` está disponible. Para los laboratorios de MSI-X, el huésped debe exponer un dispositivo con la capacidad MSI-X habilitada. El dispositivo `virtio-rng-pci` de QEMU tiene MSI-X; el `virtio-rnd` de bhyve usa virtio heredado y no expone MSI-X en el driver del host como configuración predeterminada. El capítulo indica qué laboratorios requieren cada entorno.
- Las herramientas `devinfo(8)`, `vmstat(8)`, `pciconf(8)` y `cpuset(1)` están en tu PATH.

Si alguno de los puntos anteriores es incierto, resuélvelo ahora. MSI-X tiende a exponer cualquier debilidad latente en la disciplina de contexto de interrupción del driver, porque múltiples manejadores pueden ejecutarse en múltiples CPU al mismo tiempo; el `WITNESS` del kernel de depuración es especialmente valioso durante el desarrollo del Capítulo 20.

### Cómo sacar el máximo provecho de este capítulo

Cuatro hábitos te resultarán rápidamente rentables.

En primer lugar, ten `/usr/src/sys/dev/pci/pcireg.h` y `/usr/src/sys/dev/pci/pcivar.h` como marcadores, junto con los nuevos archivos `/usr/src/sys/dev/pci/pci.c` y `/usr/src/sys/dev/virtio/pci/virtio_pci.c`. Los dos primeros provienen del Capítulo 18 y definen las constantes de capacidad (`PCIY_MSI`, `PCIY_MSIX`, `PCIM_MSIXCTRL_*`) y los envoltorios de acceso. El tercero es la implementación del kernel de `pci_msi_count_method`, `pci_alloc_msi_method`, `pci_alloc_msix_method` y `pci_release_msi_method`. El cuarto es un ejemplo limpio de driver real de la escalera completa de asignación MSI-X con reserva. Cada archivo merece media hora de lectura.

En segundo lugar, ejecuta `pciconf -lvc` en tu host de laboratorio y en un huésped. El flag `-c` indica a `pciconf` que imprima la lista de capacidades de cada dispositivo, y verás qué dispositivos exponen MSI, MSI-X o ambos. Explorar tu propia máquina es la forma más rápida de entender por qué MSI-X es la opción predeterminada en todo PCIe moderno.

En tercer lugar, escribe los cambios a mano y ejecuta cada etapa. El código MSI-X es donde los errores sutiles por vector producen bugs que solo aparecen bajo carga concurrente. Escribir con cuidado, observar `dmesg` en busca de los mensajes del attach y ejecutar la prueba de regresión después de cada etapa detecta estos errores en el momento en que son baratos de corregir.

En cuarto lugar, lee la configuración MSI-X de `/usr/src/sys/dev/nvme/nvme_ctrlr.c` (busca `nvme_ctrlr_allocate_bar` y `nvme_ctrlr_construct_admin_qpair`) después de la Sección 5. `nvme(4)` es un ejemplo limpio de driver real del patrón admin-más-N-colas que enseña el Capítulo 20. El archivo es largo, pero el código MSI-X representa una pequeña fracción; el resto de la lectura es opcional aunque educativa.

### Recorrido por el capítulo

Las secciones en orden son:

1. **¿Qué son MSI y MSI-X?** El esquema hardware: cómo funcionan las interrupciones por mensaje en el bus PCIe, la diferencia entre MSI y MSI-X, y por qué los dispositivos modernos los prefieren.
2. **Habilitación de MSI en tu driver.** El más sencillo de los dos modos. Consulta el recuento, asigna, registra un manejador. Etapa 1 del driver del Capítulo 20 (`1.3-msi-stage1`).
3. **Gestión de múltiples vectores de interrupción.** El núcleo del capítulo. rid por vector, función de filtro por vector, estado softc por vector, `bus_describe_intr` por vector. Etapa 2 (`1.3-msi-stage2`).
4. **Diseño de estructuras de datos seguras en contexto de interrupción.** Por qué múltiples vectores implica múltiples CPU, qué locks puede y no puede tocar el manejador de cada vector, y cómo estructurar el estado por cola. Una disciplina, no un incremento de etapa.
5. **Uso de MSI-X para mayor flexibilidad.** El mecanismo más completo. Diseño de la tabla, vinculación por vector, colocación con conciencia NUMA mediante `bus_get_cpus`. Etapa 3 (`1.3-msi-stage3`).
6. **Gestión de eventos específicos por vector.** Funciones de manejador por vector, trabajo diferido por vector, un patrón que el driver `nvme(4)` usa a escala.
7. **Desmontaje y limpieza con MSI/MSI-X.** Desmontaje por vector, seguido de una única llamada a `pci_release_msi`. Las reglas de orden que mantienen todo seguro.
8. **Refactorización y versionado del driver multi-vector.** La división final en `myfirst_msix.c`, el nuevo `MSIX.md`, el incremento de versión a `1.3-msi` y el paso de regresión. Etapa 4.

Tras las ocho secciones vienen los laboratorios prácticos, los ejercicios de desafío, una referencia para resolución de problemas, un cierre del Capítulo 20 y un puente hacia el Capítulo 21. El material de referencia y hoja de ayuda al final del capítulo está pensado para releerlo mientras trabajas en el Capítulo 21; el vocabulario del Capítulo 20 (vector, rid, softc por vector, afinidad, orden de desmontaje) es la base que asume el trabajo con DMA del Capítulo 21.

Si es tu primera lectura, hazla de forma lineal y realiza los laboratorios en orden. Si estás revisando, las Secciones 3 y 5 son autónomas y constituyen buenas lecturas para una sola sesión.



## Sección 1: ¿Qué son MSI y MSI-X?

Antes del código del driver, el esquema hardware. La Sección 1 enseña qué son las interrupciones por mensaje a nivel del bus PCIe y del controlador de interrupciones, sin vocabulario específico de FreeBSD. Un lector que comprenda la Sección 1 puede leer el resto del capítulo con la ruta MSI/MSI-X del kernel como un objeto concreto en lugar de una abstracción vaga.

### El problema con INTx heredado

El Capítulo 19 enseñó el modelo de interrupción INTx heredado de PCI: cada función PCI tiene una línea de interrupción (habitualmente una de INTA, INTB, INTC, INTD), la línea tiene disparo por nivel, y múltiples dispositivos en la misma línea física la comparten. El driver del Capítulo 19 gestionó correctamente el caso compartido leyendo primero INTR_STATUS y devolviendo `FILTER_STRAY` cuando no había nada activo.

INTx funciona. Pero tiene tres problemas que se agravan a medida que los sistemas escalan.

El primero es la **sobrecarga por compartición**. Una línea compartida con diez dispositivos requiere que se llame a cada driver en cada interrupción, solo para leer su propio registro de estado y descubrir que la interrupción no le corresponde. En un sistema donde la mayoría de las interrupciones son legítimas (la línea está ocupada), esto supone unas pocas llamadas adicionales a `bus_read_4` por evento; en un sistema donde un dispositivo genera una tormenta, el filtro de cada otro driver se ejecuta innecesariamente. El coste en CPU es pequeño por evento, pero se acumula a lo largo de millones de eventos por segundo.

El segundo es la **ausencia de separación por cola**. Una NIC moderna tiene cuatro, ocho, dieciséis o sesenta y cuatro colas de recepción y un número equivalente de colas de transmisión. Cada cola quiere su propia interrupción: cuando la cola de recepción 3 tiene paquetes, solo debe ejecutarse el manejador de esa cola, en una CPU cercana a la memoria que utiliza. Con INTx el dispositivo solo tiene una línea, por lo que o bien el driver sondea todas las colas desde un único manejador (costoso y lento), o bien el dispositivo solo admite una cola (inaceptable para una NIC de diez gigabits).

El tercero es la **ausencia de afinidad de CPU por tipo de evento**. Una línea compartida se dispara en una CPU (la que el controlador de interrupciones le asigne). En un sistema NUMA donde el dispositivo está conectado al socket 0, disparar la interrupción en una CPU del socket 1 es peor que hacerlo en una CPU del socket 0: el manejador se ejecuta en el socket 1, pero la memoria del dispositivo reside en el socket 0, y cada lectura de registro cruza el tejido entre sockets. Con INTx el driver no puede indicar "dispara esta interrupción en la CPU 3"; el kernel decide y el driver no tiene influencia por tipo de evento.

MSI y MSI-X solucionan los tres problemas. El mecanismo es fundamentalmente distinto de INTx: en lugar de señalización eléctrica sobre un cable dedicado, el dispositivo realiza una escritura en memoria a una dirección específica, y el controlador de interrupciones de la CPU trata la escritura como una interrupción. Esto desacopla el número de interrupciones del número de cables físicos, permite que cada interrupción por mensaje tenga su propia dirección de destino (y por tanto su propia CPU), y elimina por completo el problema de la línea compartida.

### Cómo se produce realmente una interrupción disparada por MSI

Físicamente, una interrupción MSI es una transacción de escritura en el tejido PCIe. El dispositivo emite una escritura de un valor específico a una dirección específica. El controlador de memoria reconoce la dirección como perteneciente a la región MSI del controlador de interrupciones y enruta la escritura hacia el APIC (o GIC, o el controlador de interrupciones de la plataforma que corresponda). El controlador de interrupciones decodifica la dirección para determinar qué CPU debe recibir la interrupción, y decodifica el valor escrito para determinar qué vector (qué entrada en el IDT o equivalente) debe ejecutar la CPU. Esta despacha entonces como lo haría con cualquier interrupción: guarda el estado, salta al manejador del vector y ejecuta el despacho de interrupciones del kernel.

Desde la perspectiva del driver, el flujo es casi idéntico al de INTx heredado:

1. El dispositivo tiene un evento.
2. Se dispara la interrupción.
3. El kernel llama al manejador de filtro del driver.
4. El filtro lee el estado, lo confirma, lo gestiona o lo difiere, y devuelve el resultado.
5. Si se difiere, la tarea se ejecuta más tarde.
6. El desmontaje tiene lugar en la ruta de detach.

Lo que difiere es el mecanismo en el paso 2 y el modelo de asignación en el momento de la configuración. El dispositivo no está afirmando un cable; está escribiendo en memoria. El kernel no necesita tener un cable de destino preasignado; dispone de un pool de direcciones de mensaje y valores de mensaje. Cada vector se corresponde con un par (dirección, valor). El dispositivo almacena estos pares en su estructura de capacidad MSI y emite una escritura usando el par correspondiente cuando necesita una interrupción.

### MSI: el más sencillo de los dos

MSI (Message Signalled Interrupts) es el más antiguo y sencillo de los dos mecanismos. Introducido en PCI 2.2 en 1999, MSI permite que un dispositivo solicite entre 1 y 32 vectores de interrupción, asignados como un bloque contiguo de potencia de dos (1, 2, 4, 8, 16 o 32). El dispositivo tiene una única estructura de capacidad MSI en su espacio de configuración, que contiene:

- Un registro de dirección de mensaje (la dirección de destino de la escritura, típicamente la región MSI del APIC).
- Un registro de datos de mensaje (el valor escrito, que codifica el número de vector).
- Un registro de control de mensaje (bit de habilitación, bit de máscara de función, número de vectores solicitados, etc.).

Cuando el dispositivo quiere señalizar el vector N (donde N va de 0 hasta recuento-1), escribe el valor base del registro de datos de mensaje con OR aplicado a N en la dirección del mensaje. El controlador de interrupciones desmultiplexa el valor escrito para despachar el vector correcto.

Propiedades clave de MSI:

- **Bloque de capacidad único.** El dispositivo tiene una sola capacidad MSI, no una por vector.
- **Vectores contiguos.** El bloque es potencia de dos y se asigna como una unidad.
- **Recuento limitado.** Máximo 32 vectores por función.
- **Sin enmascaramiento por vector.** El bloque completo se enmascara o desenmascara como grupo (mediante el bit de máscara de función si está soportado).
- **Sin dirección por vector.** Todos los vectores comparten un único registro de dirección de mensaje; el número de vector va en los bits bajos del valor escrito.

MSI supone una mejora significativa sobre INTx heredado, pero tiene límites: no hay enmascaramiento por vector y el tope es de 32 vectores. La mayoría de los drivers que necesitan múltiples vectores acaban prefiriendo MSI-X.

### MSI-X: El mecanismo más completo

MSI-X, introducido en PCI 3.0 en 2004 y ampliado en PCIe, elimina las limitaciones de MSI. El dispositivo cuenta con una estructura de capacidad MSI-X, una **tabla** MSI-X (un array de entradas por vector) y un **array de bits pendientes** (PBA). La estructura de capacidad apunta a uno o más de los BAR del dispositivo, donde residen la tabla y el PBA.

Cada entrada de la tabla MSI-X contiene:

- Un registro de dirección de mensaje (por vector).
- Un registro de datos de mensaje (por vector).
- Un registro de control de vector (bit de máscara por vector).

Cuando el dispositivo quiere señalizar el vector N, consulta la entrada N de la tabla, lee la dirección y los datos de esa entrada y realiza la escritura. El controlador de interrupciones despacha la interrupción en función de lo que se ha escrito.

Propiedades clave de MSI-X:

- **Dirección y datos por vector.** Cada vector puede enrutarse a una CPU diferente programando una dirección distinta.
- **Máscara por vector.** Es posible deshabilitar vectores individuales sin deshabilitar el bloque completo.
- **Hasta 2048 vectores por función.** Un controlador NVMe con muchas colas encaja perfectamente aquí; una NIC con 64 colas de recepción, 64 colas de transmisión y algunos vectores de administración también cabe sin problema.
- **Tabla en un BAR.** La ubicación de la tabla es descubrible a través de los registros de capacidad MSI-X; `pci_msix_table_bar(9)` y `pci_msix_pba_bar(9)` devuelven en qué BAR reside cada una.
- **Configuración más compleja.** El driver tiene que asignar la tabla, programar cada entrada y después habilitarla.

En la práctica, los dispositivos PCIe modernos prefieren MSI-X para cualquier caso de uso con múltiples vectores y reservan MSI para compatibilidad con hardware antiguo o para dispositivos simples de un único vector. El kernel se encarga de la mayor parte de la programación de la tabla de forma interna; la responsabilidad del driver consiste en consultar el número de vectores, asignarlos y registrar los manejadores por vector.

### Cómo abstrae FreeBSD la diferencia

El kernel oculta la mayor parte de la diferencia entre MSI y MSI-X detrás de un pequeño conjunto de funciones de acceso. En `/usr/src/sys/dev/pci/pcivar.h`:

- `pci_msi_count(dev)` devuelve el número de vectores MSI que anuncia el dispositivo (0 si no tiene capacidad MSI).
- `pci_msix_count(dev)` devuelve el número de vectores MSI-X (0 si no tiene capacidad MSI-X).
- `pci_alloc_msi(dev, &count)` y `pci_alloc_msix(dev, &count)` asignan los vectores. El parámetro `count` es de entrada y salida: como entrada indica el número deseado, como salida refleja el número realmente asignado.
- `pci_release_msi(dev)` libera tanto los vectores MSI como los MSI-X (gestiona ambos casos internamente).

El driver no interactúa directamente con la tabla MSI-X; es el kernel quien lo hace en su nombre. Lo que el driver sí observa es que, tras una asignación correcta, el dispositivo parece tener recursos IRQ adicionales disponibles a través de `bus_alloc_resource_any(9)` con `SYS_RES_IRQ`, usando `rid = 1, 2, 3, ...` para los vectores asignados. El driver registra entonces un manejador de filtro para cada recurso del mismo modo en que el Capítulo 19 registró uno para la línea heredada.

La simetría es intencionada. La misma llamada `bus_setup_intr(9)` que manejaba el IRQ heredado en `rid = 0` se encarga de cada vector MSI o MSI-X en `rid = 1, 2, 3, ...`. Toda regla `INTR_MPSAFE`, toda convención de valor de retorno `FILTER_*`, toda disciplina de IRQ compartido (para MSI, donde los vectores pueden compartir técnicamente un `intr_event` en casos límite) y todo orden de desmontaje del Capítulo 19 se aplican aquí de la misma manera.

### La escalera de reserva

Un driver robusto prueba los mecanismos en orden de preferencia y pasa al siguiente cuando la asignación falla. La escalera canónica es:

1. **MSI-X primero.** Si `pci_msix_count(dev)` es distinto de cero, intenta `pci_alloc_msix(dev, &count)`. Si tiene éxito, usa MSI-X. En un dispositivo PCIe moderno, éste es el camino preferido.
2. **MSI segundo.** Si MSI-X no está disponible o la asignación ha fallado, comprueba `pci_msi_count(dev)`. Si es distinto de cero, intenta `pci_alloc_msi(dev, &count)`. Si tiene éxito, usa MSI.
3. **INTx heredado como último recurso.** Si tanto MSI-X como MSI no están disponibles, recurre al camino heredado del Capítulo 19 con `rid = 0`.

Los drivers reales implementan esta escalera para funcionar en cualquier sistema en el que puedan desplegarse, desde una unidad NVMe de última generación que solo admite MSI-X hasta un chipset antiguo que solo admite INTx. El driver del Capítulo 20 hace lo mismo; la Sección 2 desarrolla el camino MSI, la Sección 5 desarrolla el camino MSI-X y la Sección 8 los ensambla en una única escalera de reserva.

### Ejemplos del mundo real

Un breve recorrido por dispositivos que usan MSI y MSI-X.

**NIC modernas.** Una NIC típica de 10 o 25 Gbps expone entre 16 y 64 vectores MSI-X: uno por cola de recepción, uno por cola de transmisión y un puñado para eventos de administración, errores y cambios de estado del enlace. Los drivers `igc(4)`, `em(4)`, `ix(4)` e `ixl(4)` de Intel siguen este patrón; los de Broadcom (`bnxt(4)`), Mellanox (`mlx4(4)` y `mlx5(4)`) y Chelsio (`cxgbe(4)`) hacen lo mismo. El framework `iflib(9)` envuelve la asignación de MSI-X para muchos de estos drivers.

**Controladores de almacenamiento NVMe.** Un controlador NVMe tiene una cola de administración y hasta 65535 colas de I/O. En la práctica, los drivers asignan un vector MSI-X para la cola de administración y uno por cola de I/O hasta `NCPU`. El driver `nvme(4)` de FreeBSD hace exactamente esto; el código es legible y merece la pena estudiarlo.

**Controladores de host USB modernos.** Un controlador de host xHCI (USB 3) anuncia habitualmente un vector MSI-X para el anillo de eventos de finalización de comandos y varios más para los anillos de eventos por ranura en variantes de alto rendimiento. El camino de configuración del driver `xhci(4)` muestra el patrón de administración más eventos.

**GPU.** Una GPU discreta moderna tiene muchos vectores MSI-X: uno para el buffer de comandos, uno o más para la pantalla, uno por motor, uno para la gestión de energía y otros más. Los drivers drm-kmod, que se instalan fuera del árbol de código fuente, ejercitan MSI-X de forma intensiva.

**Dispositivos Virtio en máquinas virtuales.** Cuando un huésped FreeBSD se ejecuta bajo bhyve, KVM o VMware, el transporte virtio-PCI moderno usa MSI-X: un vector para los eventos de cambio de configuración y uno por virtqueue. El driver `virtio_pci_modern(4)` implementa esto.

Todos estos drivers siguen el mismo patrón que enseña el Capítulo 20: consultar, asignar, registrar manejadores por vector, vincular a CPUs y describir. Los detalles varían (número de vectores, cómo se asignan a los eventos, cómo se vinculan a las CPUs), pero la estructura es constante.

### Por qué MSI-X y no MSI

Un lector podría preguntarse: dado que MSI-X es estrictamente más capaz que MSI, ¿por qué sigue existiendo MSI? Por dos razones.

La primera es la compatibilidad con hardware antiguo. Los dispositivos y las placas base anteriores a PCI 3.0 pueden admitir MSI pero no MSI-X. Un driver que quiera funcionar en hardware antiguo necesita un camino de reserva MSI. La mayor parte del ecosistema ha avanzado, pero la larga cola de dispositivos más antiguos sigue existiendo.

La segunda es la simplicidad. MSI con uno o dos vectores es más sencillo de configurar que MSI-X (no hay tabla que programar ni BAR que consultar). Para dispositivos cuyas necesidades de interrupción caben dentro del límite de 32 vectores de MSI y no necesitan enmascaramiento por vector, MSI es la opción más ligera. Muchos dispositivos PCIe simples exponen solo MSI por este motivo.

La respuesta práctica para el driver del Capítulo 20: intenta siempre MSI-X primero, recurre a MSI si MSI-X no está disponible y cae en INTx heredado si ninguno de los dos lo está. Todos los drivers reales de FreeBSD escritos en la última década usan esta escalera.

### Un diagrama del flujo MSI-X

```text
  Device    Config space    MSI-X table (in BAR)     Interrupt controller     CPU
 --------   ------------   ---------------------    --------------------    -----
   |             |                 |                         |                |
   | event N    |                 |                         |                |
   | occurs     |                 |                         |                |
   |            |                 |                         |                |
   | read       |                 |                         |                |
   | entry N   -+---------------->|                         |                |
   | from table |   address_N,    |                         |                |
   |            |   data_N        |                         |                |
   |<-----------+-----------------|                         |                |
   |                              |                         |                |
   | memory-write to address_N                             |                |
   |-----------------------------+------------------------->|                |
   |                              |                         |                |
   |                              |                         | steer to CPU  |
   |                              |                         |-------------->|
   |                              |                         |               | filter_N
   |                              |                         |               | runs
   |                              |                         |               |
   |                              |                         | EOI           |
   |                              |                         |<--------------|
```

El diagrama omite las lecturas de la tabla MSI-X (que el dispositivo realiza internamente antes de emitir la escritura) y la lógica de demultiplexación del controlador de interrupciones, pero captura la esencia del mecanismo: el evento del dispositivo desencadena una escritura en memoria, esa escritura se convierte en una interrupción y la interrupción se despacha a un filtro. El filtro realiza el mismo trabajo que el filtro del Capítulo 19. La única diferencia es que, con MSI-X, hay un filtro distinto para cada vector.

### Ejercicio: encontrar dispositivos con capacidad MSI en tu sistema

Antes de pasar a la Sección 2, un ejercicio breve para hacer concreta la imagen de las capacidades.

En tu máquina de laboratorio, ejecuta:

```sh
sudo pciconf -lvc
```

El flag `-c` le indica a `pciconf(8)` que imprima la lista de capacidades de cada dispositivo. Verás entradas como:

```text
vgapci0@pci0:0:2:0: ...
    ...
    cap 05[d0] = MSI supports 1 message, 64 bit
    cap 10[a0] = PCI-Express 2 endpoint max data 128(128)
em0@pci0:0:25:0: ...
    ...
    cap 01[c8] = powerspec 2  supports D0 D3  current D0
    cap 05[d0] = MSI supports 1 message, 64 bit
    cap 11[e0] = MSI-X supports 4 messages
```

Cada `cap 05` es una capacidad MSI. Cada `cap 11` es una capacidad MSI-X. La descripción que aparece tras el signo igual indica cuántos mensajes (vectores) admite el dispositivo en ese modo.

Elige tres dispositivos de tu salida. Para cada uno, anota:

- El número de vectores MSI (si los tiene).
- El número de vectores MSI-X (si los tiene).
- Cuál de los dos está usando actualmente el driver. (Puedes deducirlo a partir de las entradas `vmstat -i` del dispositivo: si ves varias líneas `name:queueN`, el driver está usando MSI-X.)

Un sistema sin muchos dispositivos PCIe puede mostrar solo capacidades MSI; los portátiles suelen tener un uso limitado de MSI-X. Un servidor moderno con varias NIC y una unidad NVMe muestra muchas capacidades MSI-X con recuentos de vectores elevados (64 o más en algunas NIC).

Mantén esta salida visible mientras lees la Sección 2. El vocabulario de "cap 11[XX] = MSI-X supports N messages" es lo que `pci_msix_count(9)` del kernel devuelve al driver y lo que la escalera de asignación consulta en el momento del attach.

### Cerrando la Sección 1

MSI y MSI-X son los sucesores modernos, basados en mensajes, del INTx heredado. MSI ofrece hasta 32 vectores asignados como un bloque contiguo con una única dirección de destino; MSI-X ofrece hasta 2048 vectores con direcciones por vector, datos por vector y enmascaramiento por vector. Ambos se señalizan a través de PCIe como escrituras en memoria que el controlador de interrupciones decodifica en despachos de vector.

El kernel abstrae la diferencia detrás de `pci_msi_count(9)`, `pci_msix_count(9)`, `pci_alloc_msi(9)`, `pci_alloc_msix(9)` y `pci_release_msi(9)`. Cada vector asignado se convierte en un recurso IRQ en `rid = 1, 2, 3, ...` para el que el driver registra un manejador de filtro mediante `bus_setup_intr(9)`, exactamente como hizo el Capítulo 19 con el IRQ heredado en `rid = 0`.

Un driver robusto implementa una escalera de reserva de tres niveles: MSI-X como primera opción, MSI como reserva y INTx heredado como último recurso. La Sección 2 desarrolla la parte MSI de esa escalera. La Sección 5 desarrolla la parte MSI-X. La Sección 8 ensambla la escalera completa.



## Sección 2: Habilitar MSI en tu driver

La Sección 1 estableció el modelo hardware. La Sección 2 pone el driver a trabajar. La tarea es concreta: ampliar el camino de attach del Capítulo 19 para que, antes de recurrir al IRQ heredado en `rid = 0`, el driver intente asignar un vector MSI. Si la asignación tiene éxito, el driver usa el vector MSI en lugar de la línea heredada. Si falla (ya sea porque el dispositivo no admite MSI o porque el kernel no puede asignarlo), el driver cae en el camino heredado exactamente como lo hacía el código del Capítulo 19.

El objetivo de la Sección 2 es introducir la API MSI de forma aislada, antes de que las complicaciones de múltiples vectores de MSI-X hagan el panorama más complejo. Un camino MSI de vector único es esencialmente igual que un camino INTx heredado de vector único; solo cambian la llamada de asignación y el rid. Ese cambio mínimo constituye una buena primera etapa.

### Lo que produce la Etapa 1

La Etapa 1 amplía el driver de la Etapa 4 del Capítulo 19 con una reserva de dos niveles: MSI primero, INTx heredado como reserva. El manejador de filtro es el mismo filtro del Capítulo 19. La taskqueue es la misma. Los sysctl son los mismos. Lo que cambia es el camino de asignación: `myfirst_intr_setup` comprueba primero `pci_msi_count(9)` y, si es distinto de cero, llama a `pci_alloc_msi(9)` para un vector. Si tiene éxito, el recurso IRQ está en `rid = 1`; si falla, el driver cae al `rid = 0` del INTx heredado.

El driver también registra el modo de interrupción en una única línea de `dmesg` para que el operador sepa de un vistazo en qué nivel ha terminado. Es una característica de observabilidad pequeña pero importante que todos los drivers reales de FreeBSD implementan; el Capítulo 20 sigue esta convención.

### La consulta del número de vectores MSI

El primer paso consiste en preguntar al dispositivo cuántos vectores MSI anuncia:

```c
int msi_count = pci_msi_count(sc->dev);
```

El valor de retorno es 0 si el dispositivo no tiene capacidad MSI; en caso contrario, es el número de vectores que anuncia el dispositivo en el registro de control de su capacidad MSI. Los valores habituales son 1, 2, 4, 8, 16 o 32 (MSI exige un número que sea potencia de dos, hasta 32).

Un retorno de 0 no significa que el dispositivo carezca de interrupciones; significa que el dispositivo no expone MSI. El driver debe pasar al siguiente nivel.

### La llamada de asignación MSI

El segundo paso consiste en pedir al kernel que asigne los vectores:

```c
int count = 1;
int error = pci_alloc_msi(sc->dev, &count);
```

El parámetro `count` es de entrada y salida. En la entrada, indica el número de vectores que solicita el driver. En la salida, indica el número que el kernel ha asignado realmente. El kernel puede asignar menos del número solicitado; un driver que necesite al menos una cantidad específica deberá comprobar el valor devuelto.

En la Etapa 1 del Capítulo 20, el driver solicita un único vector. Si el kernel devuelve 1, el driver continúa. Si el kernel devuelve 0 (poco frecuente, pero posible en un sistema con contención) o devuelve un error, el driver libera cualquier asignación realizada y recurre al modo INTx clásico.

Un punto importante: incluso cuando `pci_alloc_msi` devuelve un valor distinto de cero, el driver **debe** llamar a `pci_release_msi(dev)` para deshacer la asignación durante el desmontaje. A diferencia de `bus_alloc_resource_any` / `bus_release_resource`, la familia MSI utiliza una única llamada a `pci_release_msi` que deshace todos los vectores asignados mediante `pci_alloc_msi` o `pci_alloc_msix` en el dispositivo.

### Asignación de recursos por vector

Con los vectores MSI asignados a nivel de dispositivo, el driver debe ahora asignar un recurso `SYS_RES_IRQ` para cada vector. Para un único vector MSI, el rid es 1:

```c
int rid = 1;  /* MSI vectors start at rid 1 */
struct resource *irq_res;

irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
if (irq_res == NULL) {
	/* Release the MSI allocation and fall back. */
	pci_release_msi(sc->dev);
	goto fallback;
}
```

Observa dos diferencias respecto a la asignación legacy del capítulo 19:

Primera, **el rid es 1, no 0**. Los vectores MSI se numeran a partir de 1, dejando el rid 0 para el INTx legacy. Si el driver usara ambos (lo que no debería hacer), los rids no se solaparían.

Segunda, **`RF_SHAREABLE` no se activa**. Los vectores MSI son por función; no se comparten con otros drivers. El flag `RF_SHAREABLE` solo es relevante para el INTx legacy. Activarlo en una asignación de recurso MSI no causa ningún daño, pero no tiene ningún significado.

### El manejador de filtro en un vector MSI

La función de manejador de filtro es idéntica a la del capítulo 19:

```c
int myfirst_intr_filter(void *arg);
```

El kernel invoca el filtro cuando el vector se activa, exactamente como invocaba el filtro del capítulo 19 cuando la línea legacy se afirmaba. El filtro lee `INTR_STATUS`, reconoce la interrupción, encola la tarea para `DATA_AV` y devuelve `FILTER_HANDLED` (o `FILTER_STRAY` si no hay ningún bit activo). No es necesario cambiar nada en el cuerpo del filtro.

`bus_setup_intr(9)` se invoca de forma idéntica:

```c
error = bus_setup_intr(sc->dev, irq_res,
    INTR_TYPE_MISC | INTR_MPSAFE,
    myfirst_intr_filter, NULL, sc,
    &sc->intr_cookie);
```

La firma de la función, los flags, el argumento (`sc`) y el out-cookie siguen exactamente el patrón del capítulo 19.

Una pequeña mejora: `bus_describe_intr(9)` puede etiquetar ahora el vector con un nombre específico según el modo:

```c
bus_describe_intr(sc->dev, irq_res, sc->intr_cookie, "msi");
```

Tras esto, `vmstat -i` muestra el manejador como `irq<N>: myfirst0:msi` (para algún N que elige el kernel). El operador ve de inmediato que el driver está usando MSI.

### Construcción del mecanismo de respaldo

Reuniendo todo, la función `myfirst_intr_setup` de la etapa 1 se convierte en una escalera de respaldo con dos niveles: intentar MSI primero y caer de vuelta a INTx legacy. El código:

```c
int
myfirst_intr_setup(struct myfirst_softc *sc)
{
	int error, msi_count, count;

	TASK_INIT(&sc->intr_data_task, 0, myfirst_intr_data_task_fn, sc);
	sc->intr_tq = taskqueue_create("myfirst_intr", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->intr_tq);
	taskqueue_start_threads(&sc->intr_tq, 1, PI_NET,
	    "myfirst intr taskq");

	/*
	 * Tier 1: attempt MSI.
	 */
	msi_count = pci_msi_count(sc->dev);
	if (msi_count > 0) {
		count = 1;
		if (pci_alloc_msi(sc->dev, &count) == 0 && count == 1) {
			sc->irq_rid = 1;
			sc->irq_res = bus_alloc_resource_any(sc->dev,
			    SYS_RES_IRQ, &sc->irq_rid, RF_ACTIVE);
			if (sc->irq_res != NULL) {
				error = bus_setup_intr(sc->dev, sc->irq_res,
				    INTR_TYPE_MISC | INTR_MPSAFE,
				    myfirst_intr_filter, NULL, sc,
				    &sc->intr_cookie);
				if (error == 0) {
					bus_describe_intr(sc->dev,
					    sc->irq_res, sc->intr_cookie,
					    "msi");
					sc->intr_mode = MYFIRST_INTR_MSI;
					device_printf(sc->dev,
					    "interrupt mode: MSI, 1 vector\n");
					goto enabled;
				}
				bus_release_resource(sc->dev,
				    SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
				sc->irq_res = NULL;
			}
			pci_release_msi(sc->dev);
		}
	}

	/*
	 * Tier 2: fall back to legacy INTx.
	 */
	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &sc->irq_rid, RF_SHAREABLE | RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(sc->dev, "cannot allocate legacy IRQ\n");
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (ENXIO);
	}
	error = bus_setup_intr(sc->dev, sc->irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    myfirst_intr_filter, NULL, sc,
	    &sc->intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    sc->irq_rid, sc->irq_res);
		sc->irq_res = NULL;
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (error);
	}
	bus_describe_intr(sc->dev, sc->irq_res, sc->intr_cookie, "legacy");
	sc->intr_mode = MYFIRST_INTR_LEGACY;
	device_printf(sc->dev,
	    "interrupt mode: legacy INTx (rid=0)\n");

enabled:
	/* Enable interrupts at the device. */
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
		    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR |
		    MYFIRST_INTR_COMPLETE);
	MYFIRST_UNLOCK(sc);

	return (0);
}
```

El código tiene tres bloques diferenciados:

1. El bloque de intento MSI (las líneas dentro del guard `if (msi_count > 0)`).
2. El bloque de respaldo legacy.
3. El bloque de habilitación en `enabled:`, que se ejecuta independientemente del nivel.

El intento MSI realiza la secuencia completa: consulta del contador, asignación, asignación del recurso IRQ en rid 1, registro del manejador. Si algún paso falla, el código libera lo que hubiera tenido éxito (el recurso si se asignó, el MSI si se asignó correctamente) y continúa hacia el bloque siguiente.

El respaldo legacy es esencialmente la configuración del capítulo 19, sin cambios.

El bloque `enabled:` escribe `INTR_MASK` en el dispositivo. Tanto si se obtuvo MSI como legacy, la máscara en el lado del dispositivo es la misma.

La estructura de respaldo es lo que hacen los drivers reales. Un lector que examine el código de configuración de `virtio_pci.c` verá el mismo patrón a mayor escala: varios intentos con respaldos sucesivos.

### El campo intr_mode y el resumen de dmesg

El softc incorpora un nuevo campo:

```c
enum myfirst_intr_mode {
	MYFIRST_INTR_LEGACY = 0,
	MYFIRST_INTR_MSI = 1,
	MYFIRST_INTR_MSIX = 2,
};

struct myfirst_softc {
	/* ... existing fields ... */
	enum myfirst_intr_mode intr_mode;
};
```

El campo registra qué nivel utilizó finalmente el driver. El `device_printf` en el momento del attach lo imprime:

```text
myfirst0: interrupt mode: MSI, 1 vector
```

o bien:

```text
myfirst0: interrupt mode: legacy INTx (rid=0)
```

Los operadores que leen `dmesg` ven esta línea y saben qué camino está activo. El lector que depura su driver también la ve; si el driver está cayendo al modo legacy cuando el lector esperaba MSI, la línea indica el problema de inmediato.

El campo `intr_mode` también se expone a través de un sysctl de solo lectura para que las herramientas en espacio de usuario puedan consultarlo:

```c
SYSCTL_ADD_INT(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "intr_mode",
    CTLFLAG_RD, &sc->intr_mode, 0,
    "Interrupt mode: 0=legacy, 1=MSI, 2=MSI-X");
```

Un script que quiera saber si alguna instancia de `myfirst` usa MSI-X puede sumar los valores de `intr_mode` de todas las unidades.

### Qué debe cambiar en el teardown

El camino de teardown del capítulo 19 invocaba `bus_teardown_intr`, vaciaba y liberaba el taskqueue, y liberaba el recurso IRQ. Para la etapa 1, se necesita una llamada adicional: si el driver usó MSI, debe llamar a `pci_release_msi` después de liberar el recurso IRQ:

```c
void
myfirst_intr_teardown(struct myfirst_softc *sc)
{
	/* Disable at the device. */
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

	/* Release MSI if used. */
	if (sc->intr_mode == MYFIRST_INTR_MSI ||
	    sc->intr_mode == MYFIRST_INTR_MSIX)
		pci_release_msi(sc->dev);

	sc->intr_mode = MYFIRST_INTR_LEGACY;
}
```

La llamada a `pci_release_msi` es condicional: solo se invoca si el driver asignó realmente MSI o MSI-X. Llamarla cuando el driver usó únicamente INTx legacy es una operación vacía en FreeBSD moderno, pero la condición resulta más clara.

Observa el orden: primero se libera el recurso IRQ y luego se llama a `pci_release_msi`. Es el orden inverso al de la asignación (donde `pci_alloc_msi` precedía a `bus_alloc_resource_any`). La regla es la regla general de teardown de los capítulos 18 y 19: deshacer en orden inverso al de la configuración.

### Verificación de la etapa 1

En un huésped donde el dispositivo admite MSI (el `virtio-rng-pci` de QEMU lo hace; el `virtio-rnd` de bhyve no), el driver de la etapa 1 debería hacer el attach con MSI:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: BAR0 allocated: 0x20 bytes at 0xfebf1000
myfirst0: hardware layer attached to BAR: 32 bytes
myfirst0: interrupt mode: MSI, 1 vector
```

En un huésped donde el dispositivo admite únicamente el modo legacy (el `virtio-rnd` de bhyve, habitualmente):

```text
myfirst0: BAR0 allocated: 0x20 bytes at 0xc1000000
myfirst0: hardware layer attached to BAR: 32 bytes
myfirst0: interrupt mode: legacy INTx (rid=0)
```

Ambos casos son correctos. El driver funciona en cualquiera de los dos modos; el comportamiento (filtro, tarea, contadores, sysctl de interrupción simulada) es idéntico.

`sysctl dev.myfirst.0.intr_mode` devuelve 0 (legacy), 1 (MSI) o 2 (MSI-X, una vez que la sección 5 lo incorpore). El script de regresión utiliza este valor para verificar el modo esperado.

### Lo que la etapa 1 no hace

La etapa 1 añade MSI con un único vector pero no aprovecha aún el potencial multivector de MSI. Un único vector MSI es funcionalmente casi idéntico a un único IRQ legacy (solo obtiene las ventajas de escalabilidad en un sistema con muchos dispositivos, lo que rara vez se aprecia en un laboratorio de un solo dispositivo). El valor de la etapa 1 radica en que introduce el patrón de escalera de respaldo y establece la observabilidad del campo `intr_mode`; las etapas 2 y siguientes utilizan esas bases para añadir el manejo multivector.

### Errores frecuentes en esta etapa

Una lista breve.

**Usar rid = 0 para MSI.** El rid del vector MSI es 1, no 0. Solicitar `rid = 0` en un dispositivo que ha asignado vectores MSI devuelve el recurso INTx legacy, que no es el vector MSI. El driver termina con un manejador en la línea equivocada. Corrección: `rid = 1` para el primer vector MSI o MSI-X.

**Olvidar `pci_release_msi` en el teardown.** El estado de asignación MSI del kernel sobrevive a `bus_release_resource` sobre el recurso IRQ. Sin `pci_release_msi`, el siguiente intento de attach fallará porque el kernel aún cree que el driver es propietario de los vectores MSI. Corrección: llamar siempre a `pci_release_msi` en el teardown cuando se usó MSI o MSI-X.

**Olvidar el respaldo INTx.** Un driver que solo intenta MSI y devuelve un error en caso de fallo funciona en sistemas que admiten MSI pero falla en sistemas más antiguos. Corrección: proporcionar siempre un respaldo INTx legacy.

**Olvidar restaurar sc->intr_mode en el teardown.** El campo `intr_mode` registra el nivel. Sin restablecerlo, un futuro reattach podría leer un valor obsoleto. No es un error grave (el attach siempre lo establece), pero la limpieza del código importa. Corrección: restablecer a `LEGACY` (o a un valor neutro) en el teardown.

**Discordancia en el contador.** `pci_alloc_msi` puede asignar menos vectores de los solicitados; si el driver asume que `count == 1` cuando es 0, el código desreferencia un recurso no asignado. Corrección: comprobar siempre el contador devuelto.

**Llamar a `pci_alloc_msi` dos veces sin liberar.** Solo puede haber una asignación MSI (o MSI-X) activa por dispositivo en un momento dado. Intentar una segunda asignación sin liberar la primera devuelve un error. Corrección: si el driver quiere cambiar su asignación (de MSI a MSI-X, por ejemplo), llamar primero a `pci_release_msi`.

### Punto de control: etapa 1 en funcionamiento

Antes de la sección 3, confirma que la etapa 1 está en su sitio:

- `kldstat -v | grep myfirst` muestra la versión `1.3-msi-stage1`.
- `dmesg | grep myfirst` muestra el banner de attach con una línea `interrupt mode:` que indica MSI o legacy.
- `sysctl dev.myfirst.0.intr_mode` devuelve 0 o 1.
- `vmstat -i | grep myfirst` muestra el manejador con `myfirst0:msi` o `myfirst0:legacy` como descriptor.
- `sudo sysctl dev.myfirst.0.intr_simulate=1` sigue activando el pipeline del capítulo 19.
- `kldunload myfirst` se ejecuta de forma limpia, sin fugas.

Si el camino MSI falla en tu huésped, prueba con QEMU en lugar de bhyve. Si el camino MSI funciona en uno y no en el otro, verifica que la capacidad MSI del dispositivo está expuesta mediante `pciconf -lvc`.

### Cerrando la sección 2

Habilitar MSI en un driver supone tres nuevas llamadas (`pci_msi_count`, `pci_alloc_msi`, `pci_release_msi`), un cambio en la asignación del recurso IRQ (rid = 1 en lugar de 0) y un nuevo campo en el softc (`intr_mode`). La escalera de respaldo añade un segundo nivel: intentar MSI y caer de vuelta al modo legacy. Cada `bus_setup_intr`, cada filtro, cada tarea del taskqueue y cada paso del teardown del capítulo 19 se mantienen sin cambios.

La etapa 1 maneja un único vector MSI. La sección 3 avanza hacia el manejo multivector: varias funciones de filtro distintas, varios estados del softc por vector y el comienzo del patrón de manejador por cola que los drivers modernos usan de forma intensiva.



## Sección 3: Gestión de múltiples vectores de interrupción

La etapa 1 añadió MSI con un único vector. La sección 3 extiende el driver para manejar múltiples vectores, cada uno con su propia función. El ejemplo motivador es el dispositivo que tiene más de una cosa que comunicar: una NIC con una cola de recepción y una cola de transmisión, un controlador NVMe con colas de administración y de I/O, un UART con eventos de receptor listo y transmisor vacío.

El driver del capítulo 20 no dispone de un dispositivo multi-cola real; el objetivo virtio-rnd tiene como máximo una clase de evento por interrupción. Con fines pedagógicos, simulamos el comportamiento multivector del mismo modo que el capítulo 19 simulaba las interrupciones: la interfaz sysctl permite al lector lanzar interrupciones simuladas contra vectores específicos, y la maquinaria de filtros y tareas del driver demuestra cómo los drivers reales manejan los casos multivector.

Al final de la sección 3, el driver está en la versión `1.3-msi-stage2` y dispone de tres vectores MSI-X: un vector de administración, un vector «rx» y un vector «tx». Cada vector tiene su propia función de filtro, su propia tarea diferida y sus propios contadores. El filtro lee `INTR_STATUS` y reconoce únicamente los bits relevantes para su vector; la tarea realiza el trabajo específico de ese vector.

Una nota importante sobre el recuento de tres vectores. MSI está limitado a un número de vectores que sea potencia de dos (1, 2, 4, 8, 16 o 32), por lo que una solicitud de exactamente 3 vectores es rechazada por `pci_alloc_msi(9)` con `EINVAL` (véase `pci_alloc_msi_method` en `/usr/src/sys/dev/pci/pci.c`). MSI-X no tiene tal restricción y asigna 3 vectores sin problemas. El nivel MSI de la escalera de respaldo solicita por tanto un único vector MSI y cae de vuelta al patrón de manejador único del capítulo 19; solo el nivel MSI-X otorga al driver sus tres filtros por vector. La sección 5 hace esto explícito y la refactorización de la sección 8 mantiene el nivel MSI simple.

### El diseño por vector

El diseño tiene tres vectores:

- **Vector de administración (vector 0, rid 1).** Maneja eventos `ERROR` y de cambio de configuración. Tasa baja; se activa raramente.
- **Vector RX (vector 1, rid 2).** Maneja eventos `DATA_AV` (receptor listo). Se activa a la velocidad del camino de datos.
- **Vector TX (vector 2, rid 3).** Maneja eventos `COMPLETE` (transmisión completada). Se activa a la velocidad del camino de datos.

Cada vector dispone de:

- Un `struct resource *` propio (el recurso IRQ de ese vector).
- Un `void *intr_cookie` propio (el manejador opaco del kernel para ese manejador).
- Una función de filtro propia (`myfirst_admin_filter`, `myfirst_rx_filter`, `myfirst_tx_filter`).
- Un conjunto de contadores propio (para que las ejecuciones de filtros concurrentes por CPU no compitan por un único contador compartido).
- Un nombre de `bus_describe_intr` propio (`admin`, `rx`, `tx`).
- Una tarea diferida propia (para RX; los vectores admin y TX manejan su trabajo de forma inline).

El estado por vector vive en un array de estructuras por vector dentro del softc:

```c
#define MYFIRST_MAX_VECTORS 3

enum myfirst_vector_id {
	MYFIRST_VECTOR_ADMIN = 0,
	MYFIRST_VECTOR_RX,
	MYFIRST_VECTOR_TX,
};

struct myfirst_vector {
	struct resource		*irq_res;
	int			 irq_rid;
	void			*intr_cookie;
	enum myfirst_vector_id	 id;
	struct myfirst_softc	*sc;
	uint64_t		 fire_count;
	uint64_t		 stray_count;
	const char		*name;
	driver_filter_t		*filter;
	struct task		 task;
	bool			 has_task;
};

struct myfirst_softc {
	/* ... existing fields ... */
	struct myfirst_vector	vectors[MYFIRST_MAX_VECTORS];
	int			num_vectors;   /* actually allocated */
};
```

Merece la pena desarrollar algunas notas de diseño.

**Puntero de retroceso `struct myfirst_softc *sc` por vector.** El argumento que recibe cada filtro a través de `bus_setup_intr` es la estructura por vector (`struct myfirst_vector *`), no el softc global. La estructura por vector contiene un puntero de retroceso al softc para que el filtro pueda acceder al estado compartido cuando sea necesario. Este es el patrón que `nvme(4)` usa para los vectores por cola y el que siguen todos los drivers multi-cola.

**Contadores por vector.** Cada vector tiene su propio `fire_count` y `stray_count`. Dos filtros que se ejecutan en dos CPU pueden incrementar sus propios contadores sin contención atómica; se siguen usando operaciones atómicas, pero cada una accede a una cache line diferente.

**Puntero de filtro por vector.** El campo `filter` almacena un puntero a la función de filtro del vector. Esto no es estrictamente necesario (podríamos tener un switch en un único filtro genérico), pero hace que la especialización por vector sea explícita: el filtro de cada vector se conoce de forma estática.

**Tarea por vector.** No todo vector necesita una tarea. Admin y TX realizan su trabajo de forma inline (incrementan un contador, actualizan un flag y, quizás, despiertan a un waiter). RX delega en una tarea porque necesita hacer broadcast sobre una variable de condición, lo que requiere contexto de thread. El flag `has_task` hace explícita esa diferencia por vector.

### Las funciones de filtro

Tres funciones de filtro distintas, una por vector:

```c
int
myfirst_admin_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & (MYFIRST_INTR_ERROR)) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_ERROR);
	atomic_add_64(&sc->intr_error_count, 1);
	return (FILTER_HANDLED);
}

int
myfirst_rx_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & MYFIRST_INTR_DATA_AV) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_DATA_AV);
	atomic_add_64(&sc->intr_data_av_count, 1);
	if (sc->intr_tq != NULL)
		taskqueue_enqueue(sc->intr_tq, &vec->task);
	return (FILTER_HANDLED);
}

int
myfirst_tx_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & MYFIRST_INTR_COMPLETE) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_COMPLETE);
	atomic_add_64(&sc->intr_complete_count, 1);
	return (FILTER_HANDLED);
}
```

Cada filtro tiene la misma forma: leer el estado, comprobar el bit que le corresponde a este vector, confirmar la interrupción, actualizar los contadores, opcionalmente encolar una tarea y retornar. Las diferencias son qué bit comprueba cada filtro y qué contadores incrementa.

Hay varios detalles que merece la pena destacar.

**La comprobación de stray es por vector.** Cada filtro comprueba su propio bit, no cualquier bit. Si el filtro es invocado para un evento que no gestiona (porque el bit activo corresponde a un vector diferente), el filtro devuelve `FILTER_STRAY`. Esto importa menos con MSI-X (donde cada vector tiene su propio mensaje dedicado, de modo que el dispositivo nunca activa el vector «equivocado»), pero importa más con MSI cuando múltiples vectores comparten una única capability.

**Compartición de contadores.** Los contadores por vector (`vec->fire_count`, `vec->stray_count`) son específicos de cada vector. Los contadores globales (`sc->intr_data_av_count`, etc.) son compartidos y se siguen usando para la observabilidad por bit del capítulo. Disponer de ambos ofrece al lector una forma de contraste: el recuento de disparos del filtro RX debería aproximarse al `data_av_count` global.

**El filtro no duerme.** Todas las reglas del contexto de filtro del Capítulo 19 se mantienen: sin sleep locks, sin `malloc(M_WAITOK)`, sin bloqueos. El filtro utiliza únicamente operaciones atómicas y accesos directos al BAR.

### La tarea por vector

Solo RX tiene una tarea; admin y TX gestionan su trabajo en el filtro. La tarea RX es esencialmente la tarea del Capítulo 19:

```c
static void
myfirst_rx_task_fn(void *arg, int npending)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->pci_attached) {
		sc->intr_last_data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
		sc->intr_task_invocations++;
		cv_broadcast(&sc->data_cv);
	}
	MYFIRST_UNLOCK(sc);
}
```

La tarea se ejecuta en contexto de thread en el taskqueue compartido `intr_tq` (el Capítulo 19 lo creó con prioridad `PI_NET`). El mismo taskqueue sirve a todas las tareas por vector; en un driver con trabajo verdaderamente independiente por cola, cada vector podría tener su propio taskqueue, pero el Capítulo 20 utiliza uno solo.

### Asignación de múltiples vectores

El código de configuración de la Etapa 2 es más largo que el de la Etapa 1 porque gestiona múltiples vectores:

```c
int
myfirst_intr_setup(struct myfirst_softc *sc)
{
	int error, wanted, allocated, i;

	TASK_INIT(&sc->vectors[MYFIRST_VECTOR_RX].task, 0,
	    myfirst_rx_task_fn, &sc->vectors[MYFIRST_VECTOR_RX]);
	sc->vectors[MYFIRST_VECTOR_RX].has_task = true;
	sc->vectors[MYFIRST_VECTOR_ADMIN].filter = myfirst_admin_filter;
	sc->vectors[MYFIRST_VECTOR_RX].filter = myfirst_rx_filter;
	sc->vectors[MYFIRST_VECTOR_TX].filter = myfirst_tx_filter;
	sc->vectors[MYFIRST_VECTOR_ADMIN].name = "admin";
	sc->vectors[MYFIRST_VECTOR_RX].name = "rx";
	sc->vectors[MYFIRST_VECTOR_TX].name = "tx";
	for (i = 0; i < MYFIRST_MAX_VECTORS; i++) {
		sc->vectors[i].id = i;
		sc->vectors[i].sc = sc;
	}

	sc->intr_tq = taskqueue_create("myfirst_intr", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->intr_tq);
	taskqueue_start_threads(&sc->intr_tq, 1, PI_NET,
	    "myfirst intr taskq");

	/*
	 * Try to allocate a single MSI vector. MSI requires a power-of-two
	 * count (PCI specification and /usr/src/sys/dev/pci/pci.c's
	 * pci_alloc_msi_method enforces this), so we cannot request the
	 * MYFIRST_MAX_VECTORS = 3 we want; we ask for 1 and fall back to
	 * the Chapter 19 single-handler pattern at rid=1, the same way
	 * sys/dev/virtio/pci/virtio_pci.c's vtpci_alloc_msi() does.
	 *
	 * MSI-X, covered in Section 5, is the tier where we actually
	 * obtain three distinct vectors; MSI-X is not constrained to
	 * power-of-two counts.
	 */
	allocated = 1;
	if (pci_msi_count(sc->dev) >= 1 &&
	    pci_alloc_msi(sc->dev, &allocated) == 0 && allocated >= 1) {
		sc->vectors[MYFIRST_VECTOR_ADMIN].filter = myfirst_intr_filter;
		sc->vectors[MYFIRST_VECTOR_ADMIN].name = "msi";
		error = myfirst_intr_setup_vector(sc, MYFIRST_VECTOR_ADMIN, 1);
		if (error == 0) {
			sc->intr_mode = MYFIRST_INTR_MSI;
			sc->num_vectors = 1;
			device_printf(sc->dev,
			    "interrupt mode: MSI, 1 vector "
			    "(single-handler fallback)\n");
			goto enabled;
		}
		pci_release_msi(sc->dev);
	}

	/*
	 * MSI allocation failed or was unavailable. Fall back to legacy
	 * INTx with a single vector-0 handler that handles every event
	 * class in one place.
	 */

fallback_legacy:
	sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid = 0;
	sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res = bus_alloc_resource_any(
	    sc->dev, SYS_RES_IRQ,
	    &sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res == NULL) {
		device_printf(sc->dev, "cannot allocate legacy IRQ\n");
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (ENXIO);
	}
	error = bus_setup_intr(sc->dev,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    myfirst_intr_filter, NULL, sc,
	    &sc->vectors[MYFIRST_VECTOR_ADMIN].intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid,
		    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res);
		sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res = NULL;
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (error);
	}
	bus_describe_intr(sc->dev,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].intr_cookie, "legacy");
	sc->intr_mode = MYFIRST_INTR_LEGACY;
	sc->num_vectors = 1;
	device_printf(sc->dev,
	    "interrupt mode: legacy INTx (1 handler for all events)\n");

enabled:
	/* Enable interrupts at the device. */
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
		    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR |
		    MYFIRST_INTR_COMPLETE);
	MYFIRST_UNLOCK(sc);

	return (0);
}
```

El código tiene tres fases: intento MSI, limpieza del fallback MSI y fallback a legado. El intento MSI recorre los vectores en bucle, llamando a una función auxiliar (`myfirst_intr_setup_vector`) para asignar y registrar cada uno. Si falla en algún vector, el código deshace los pasos en orden inverso y cae al modo legado.

La función auxiliar:

```c
static int
myfirst_intr_setup_vector(struct myfirst_softc *sc, int idx, int rid)
{
	struct myfirst_vector *vec = &sc->vectors[idx];
	int error;

	vec->irq_rid = rid;
	vec->irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &vec->irq_rid, RF_ACTIVE);
	if (vec->irq_res == NULL)
		return (ENXIO);

	error = bus_setup_intr(sc->dev, vec->irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    vec->filter, NULL, vec, &vec->intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, vec->irq_rid,
		    vec->irq_res);
		vec->irq_res = NULL;
		return (error);
	}

	bus_describe_intr(sc->dev, vec->irq_res, vec->intr_cookie,
	    "%s", vec->name);
	return (0);
}
```

La función auxiliar es pequeña y simétrica: asignar el recurso, configurar el handler y describirlo. El argumento de `bus_setup_intr` es la estructura por vector (`vec`), no el softc. El filtro recibe `vec` como su `void *arg` y usa `vec->sc` cuando necesita el softc.

La función auxiliar de desmontaje por vector:

```c
static void
myfirst_intr_teardown_vector(struct myfirst_softc *sc, int idx)
{
	struct myfirst_vector *vec = &sc->vectors[idx];

	if (vec->intr_cookie != NULL) {
		bus_teardown_intr(sc->dev, vec->irq_res, vec->intr_cookie);
		vec->intr_cookie = NULL;
	}
	if (vec->irq_res != NULL) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, vec->irq_rid,
		    vec->irq_res);
		vec->irq_res = NULL;
	}
}
```

El desmontaje es la operación inversa de la configuración: desmontar el handler y liberar el recurso.

### El proceso completo de desmontaje

El desmontaje multivector llama a la función auxiliar por vector para cada vector activo y luego libera la asignación MSI una sola vez:

```c
void
myfirst_intr_teardown(struct myfirst_softc *sc)
{
	int i;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);

	/* Per-vector teardown. */
	for (i = 0; i < sc->num_vectors; i++)
		myfirst_intr_teardown_vector(sc, i);

	/* Drain tasks. */
	if (sc->intr_tq != NULL) {
		for (i = 0; i < sc->num_vectors; i++) {
			if (sc->vectors[i].has_task)
				taskqueue_drain(sc->intr_tq,
				    &sc->vectors[i].task);
		}
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
	}

	/* Release MSI if used. */
	if (sc->intr_mode == MYFIRST_INTR_MSI ||
	    sc->intr_mode == MYFIRST_INTR_MSIX)
		pci_release_msi(sc->dev);

	sc->num_vectors = 0;
	sc->intr_mode = MYFIRST_INTR_LEGACY;
}
```

El orden es el ya conocido: enmascarar en el dispositivo, desmontar los handlers, vaciar las tareas, liberar el MSI. El bucle por vector realiza el trabajo por vector.

### Simulación de interrupciones por vector

El sysctl de interrupción simulada del Capítulo 19 activa un handler a la vez. La Etapa 2 extiende el concepto: un sysctl por vector, o un único sysctl con un campo de índice de vector. El código del capítulo opta por la forma más sencilla de un único sysctl por vector:

```c
SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_admin",
    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE,
    &sc->vectors[MYFIRST_VECTOR_ADMIN], 0,
    myfirst_intr_simulate_vector_sysctl, "IU",
    "Simulate admin vector interrupt");
SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_rx", ...);
SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_tx", ...);
```

El handler:

```c
static int
myfirst_intr_simulate_vector_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_vector *vec = arg1;
	struct myfirst_softc *sc = vec->sc;
	uint32_t mask = 0;
	int error;

	error = sysctl_handle_int(oidp, &mask, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	MYFIRST_LOCK(sc);
	if (sc->hw == NULL || sc->bar_res == NULL) {
		MYFIRST_UNLOCK(sc);
		return (ENODEV);
	}
	bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS, mask);
	MYFIRST_UNLOCK(sc);

	/*
	 * Invoke this vector's filter if it has one (MSI-X). On single-
	 * handler tiers (MSI with 1 vector, or legacy INTx) only slot 0
	 * has a registered filter, so we fall through to it. The Chapter 19
	 * myfirst_intr_filter handles all three status bits in one pass.
	 */
	if (vec->filter != NULL)
		(void)vec->filter(vec);
	else if (sc->vectors[MYFIRST_VECTOR_ADMIN].filter != NULL)
		(void)sc->vectors[MYFIRST_VECTOR_ADMIN].filter(
		    &sc->vectors[MYFIRST_VECTOR_ADMIN]);
	return (0);
}
```

Desde el espacio de usuario:

```sh
sudo sysctl dev.myfirst.0.intr_simulate_admin=2  # ERROR bit, admin vector
sudo sysctl dev.myfirst.0.intr_simulate_rx=1     # DATA_AV bit, rx vector
sudo sysctl dev.myfirst.0.intr_simulate_tx=4     # COMPLETE bit, tx vector
```

Los contadores `intr_count` por vector se incrementan de forma independiente. El lector puede verificar el comportamiento por vector activando cada sysctl y observando cómo sube el contador `vec->fire_count` correspondiente.

### Qué ocurre en el fallback a legado

Cuando el driver recurre al modo legado INTx (porque MSI no estaba disponible o falló), solo hay un handler para cubrir las tres clases de eventos. El código asigna el `myfirst_intr_filter` del Capítulo 19 a la ranura del vector admin y usa ese único filtro en `rid = 0`. El filtro del vector admin se convierte en un handler multievento que examina los tres bits de estado y despacha en consecuencia.

Este es un detalle pequeño pero importante: el filtro del Capítulo 19 sigue existiendo y se reutiliza en el camino de legado, mientras que los filtros por vector solo se usan cuando MSI o MSI-X está disponible. Un lector que inspeccione el driver verá ambos, y la diferencia se explica en los comentarios de la Sección 3.

### El banner de dmesg de la Etapa 2

En un guest donde el driver alcanza el nivel MSI-X (este es el único nivel que entrega tres vectores; el nivel MSI recurre a una configuración de handler único por las razones explicadas anteriormente):

```text
myfirst0: BAR0 allocated: 0x20 bytes at 0xfebf1000
myfirst0: hardware layer attached to BAR: 32 bytes
myfirst0: interrupt mode: MSI-X, 3 vectors
```

`vmstat -i | grep myfirst` muestra tres líneas separadas:

```text
irq256: myfirst0:admin                 12         1
irq257: myfirst0:rx                    98         8
irq258: myfirst0:tx                    45         4
```

(Los números exactos de IRQ varían según la plataforma; las IRQ asignadas por MSI en x86 comienzan en el rango 256 una vez que se agota el rango del I/O APIC.)

En un guest donde solo MSI está disponible, el driver informa de un fallback de handler único:

```text
myfirst0: interrupt mode: MSI, 1 vector (single-handler fallback)
```

y `vmstat -i` muestra una sola línea, porque el driver utiliza el patrón del Capítulo 19 en ese único vector MSI.

El desglose por vector (tres líneas) es lo que hace observable el driver multivector. Un operador que supervise los contadores puede saber qué vector está activo y con qué frecuencia.

### Errores comunes en la Etapa 2

Una lista breve.

**Pasar el softc (en lugar del vector) como argumento del filtro.** Si pasas `sc` en lugar de `vec`, el filtro no puede determinar a qué vector está sirviendo. Solución: pasa `vec` a `bus_setup_intr`; el filtro accede a `sc` a través de `vec->sc`.

**Olvidar inicializar `vec->sc`.** Las estructuras por vector se inicializan a cero mediante `myfirst_init_softc`; `vec->sc` permanece NULL a menos que se establezca explícitamente. Sin ello, el acceso del filtro a `vec->sc->mtx` es una desreferencia nula. Solución: establece `vec->sc = sc` durante la configuración, antes de registrar ningún handler.

**Usar el mismo rid para múltiples vectores.** Los rids de MSI son 1, 2, 3, ...; reutilizar el rid 1 tanto para los vectores admin como RX significa que solo se registra un handler. Solución: asigna rids en secuencia por vector.

**Handler por vector que accede a estado compartido sin locking.** Dos filtros ejecutándose en diferentes CPU intentan escribir en un único `sc->counter`. Sin operaciones atómicas ni un spin lock, el incremento pierde actualizaciones. Solución: usa contadores por vector donde sea posible, y operaciones atómicas para cualquier contador compartido.

**Tarea por vector almacenada en el lugar equivocado.** Si la tarea está en el softc en lugar de en la estructura del vector, dos vectores que encolen la «misma» tarea colisionan. Solución: almacena la tarea en la estructura del vector y pasa el vector como argumento de la tarea.

**Falta de desmontaje por vector en caso de fallo parcial de la configuración.** La cascada de goto debe deshacer exactamente los vectores que tuvieron éxito. Una limpieza incompleta deja recursos IRQ asignados. Solución: usa la función auxiliar de desmontaje por vector e itera hacia atrás en el caso de fallo parcial.

### Cierre de la Sección 3

Gestionar múltiples vectores de interrupción implica tres patrones nuevos: estado por vector (en un array de `struct myfirst_vector`), funciones de filtro por vector y nombres `bus_describe_intr` por vector. Cada vector tiene su propio recurso IRQ en su propio rid, su propia función de filtro que lee solo los bits de estado relevantes para ese vector, sus propios contadores y, opcionalmente, su propia tarea diferida. La única llamada de asignación MSI o MSI-X gestiona el estado en el lado del dispositivo; las llamadas `bus_alloc_resource_any` y `bus_setup_intr` por vector gestionan cada handler individualmente.

La escalera de fallback de la Sección 2 se extiende de forma natural: intentar primero MSI con N vectores; en caso de fallo parcial, liberar e intentar INTx legado con un único handler. La función auxiliar de desmontaje por vector hace que el desenrollado por fallo parcial y el desmontaje limpio sean simétricos.

La Sección 4 es la sección de locking y estructuras de datos. Examina qué ocurre cuando múltiples filtros se ejecutan en múltiples CPU al mismo tiempo, y qué disciplina de sincronización mantiene el estado compartido en un estado coherente.



## Sección 4: Diseño de estructuras de datos seguras ante interrupciones

La Sección 3 añadió múltiples vectores, cada uno con su propio handler. La Sección 4 examina las consecuencias: múltiples handlers pueden ejecutarse de forma concurrente en múltiples CPU, y cualquier dato que compartan los handlers debe estar protegido en consecuencia. La disciplina no es nueva; es la especialización multi-CPU del modelo de locking del Capítulo 11. Lo que es nuevo es que el driver del Capítulo 20 tiene tres (o más) caminos concurrentes en contexto de filtro en lugar de uno.

La Sección 4 es donde el uso multivector cambia la forma del estado del driver, no solo el número de sus handlers.

### El nuevo panorama de concurrencia

El driver del Capítulo 19 tenía un filtro y una tarea. El filtro se ejecutaba en la CPU a la que el kernel enrutaba la interrupción; la tarea se ejecutaba en el thread trabajador del taskqueue. En principio, dos de ellos podían ejecutarse simultáneamente: el filtro en la CPU 0 y la tarea en la CPU 3, por ejemplo. Los contadores atómicos y el `sc->mtx` (adquirido por la tarea, no por el filtro) proporcionaban la sincronización necesaria.

El driver multivector del Capítulo 20 tiene tres filtros y una tarea. En un sistema MSI-X, cada filtro tiene su propio `intr_event`, de modo que cada uno puede activarse en una CPU diferente de forma independiente. Una ráfaga de tres interrupciones que lleguen en un microsegundo puede hacer que tres filtros se ejecuten en tres CPU al mismo tiempo. La tarea única sigue siendo serializada a través del taskqueue, pero los filtros no.

Los datos que tocan los filtros se dividen en tres categorías:

1. **Estado por vector.** Los contadores propios de cada vector, su propio cookie, su propio recurso. Sin compartición entre vectores. Sin sincronización necesaria.
2. **Contadores compartidos.** Contadores actualizados por cualquier filtro (el `intr_data_av_count` global, `intr_error_count`, etc.). Deben ser atómicos.
3. **Estado compartido del dispositivo.** El BAR en sí, el puntero `sc->hw` del softc, `sc->pci_attached`, los campos protegidos por mutex. Las reglas de acceso dependen del contexto.

La disciplina consiste en mantener el estado por vector verdaderamente por vector, usar operaciones atómicas para los contadores compartidos y respetar las reglas de locking del Capítulo 11 para todo lo que requiera un sleep mutex.

### Estado por vector: el comportamiento por defecto

La sincronización más sencilla es no tener sincronización. Si un fragmento de estado solo es tocado por el filtro de un vector (y por nada más), no se necesita ningún lock. Este es el caso de:

- `vec->fire_count`: incrementado solo por el filtro de este vector, leído por los handlers de sysctl a través del camino de lectura del sysctl. Una suma atómica es suficiente; no se necesita lock entre el filtro y el sysctl porque el sysctl lee atómicamente.
- `vec->stray_count`: mismo patrón.
- `vec->intr_cookie`: escrito una vez en la configuración, leído en el desmontaje. Un único escritor, acceso ordenado.
- `vec->irq_res`: mismo patrón.

La mayor parte del estado por vector cae en esta categoría. El array `struct myfirst_vector` en el softc es el patrón clave: el estado de cada vector vive en su propia ranura, y solo lo toca su propio filtro.

### Contadores compartidos: operaciones atómicas

Los contadores globales por bit (el Capítulo 19 introdujo `sc->intr_data_av_count`, etc.) son actualizados por el filtro del vector correspondiente. Solo un filtro actualiza cada contador, de modo que técnicamente son por vector aunque el nombre no lo indique. Pero el lector puede imaginar un escenario en el que un patrón de bits aparece en `INTR_STATUS` que requiere que tanto los vectores RX como admin incrementen contadores compartidos. El enfoque más seguro es hacer que cada actualización sea atómica.

El Capítulo 20 usa `atomic_add_64` en todos los caminos del filtro:

```c
atomic_add_64(&sc->intr_data_av_count, 1);
```

Esto es barato (una instrucción con lock en x86, una barrera más una suma en arm64), y permite que el filtro se ejecute en cualquier CPU sin preocuparse por las actualizaciones perdidas.

El coste de `atomic_add_64` en un contador muy compartido es el rebote de línea de caché: cada incremento desde una CPU diferente invalida la línea de caché en las demás CPU. Para un contador que se incrementa un millón de veces por segundo desde varias CPU, esto supone un impacto de rendimiento medible. La mitigación consiste en hacer los contadores verdaderamente por CPU (usando `counter(9)` o `DPCPU_DEFINE`) y sumarlos solo al leerlos; el driver del Capítulo 20 no está a esa escala, así que las operaciones atómicas sencillas son suficientes.

### Estado compartido del dispositivo: la disciplina del mutex

`sc->hw`, `sc->pci_attached`, `sc->bar_res`: se establecen durante el attach y se liberan durante el detach. En estado estable, son de solo lectura. Los filtros acceden a ellos sin un lock porque la disciplina de ciclo de vida (attach antes de habilitar, deshabilitar antes del detach) garantiza que los punteros son válidos siempre que el filtro pueda ejecutarse.

La regla: un filtro que accede a `sc->hw` o `sc->bar_res` sin un lock debe tener la certeza de que el orden attach-detach garantiza la validez del puntero. La sección 7 del capítulo 20 recorre ese orden en detalle. Para los propósitos de la sección 4, confía en la disciplina: cuando el filtro se ejecuta, el dispositivo está en estado attached y los punteros son válidos.

### El lock por vector: cuándo lo necesitas

A veces el estado por vector es más rico que un simple contador. Un vector que lee de una cola de recepción y actualiza una estructura de datos por cola (un anillo de mbufs, por ejemplo) necesita un spinlock para proteger el anillo frente a dos disparos simultáneos del mismo vector. ¿Puede el mismo vector dispararse dos veces simultáneamente en un sistema MSI-X?

En MSI-X, el kernel garantiza que cada `intr_event` se entrega a una sola CPU a la vez; un único vector no se reentra en sí mismo. Dos vectores distintos pueden ejecutarse en dos CPU a la vez, pero el vector N no puede ejecutarse en la CPU 3 y en la CPU 5 simultáneamente.

Esto significa: **el estado por vector no necesita un lock por vector** para el acceso concurrente desde el mismo vector. Puede necesitarlo para la comunicación entre el filtro y la tarea (la tarea se ejecuta en una CPU distinta, posiblemente de forma concurrente con el filtro), pero un spinlock es suficiente en ese caso, y la comunicación se realiza habitualmente mediante operaciones atómicas.

Un spinlock resulta útil cuando:

- Un driver usa una única función de filtro para varios vectores, y el kernel puede despachar el filtro de dos vectores de forma concurrente. (El Stage 2 del Capítulo 20 tiene filtros separados por vector, por lo que esto no aplica.)
- Un driver comparte un anillo de recepción entre el filtro (que lo llena) y una tarea (que lo vacía). Un spinlock protege el índice del anillo; el filtro adquiere el spinlock, añade al anillo y lo libera. La tarea adquiere, vacía y libera.

El driver del Capítulo 20 no usa spinlocks en el filtro; los contadores por vector son atómicos y el estado compartido se gestiona a través del `sc->mtx` existente en la tarea. Los drivers reales pueden necesitar spinlocks en escenarios más complejos.

### Datos por CPU: la opción avanzada

Para drivers de muy alta tasa de transferencia, incluso los contadores atómicos sobre datos compartidos se convierten en un cuello de botella. La solución es el uso de datos por CPU: cada CPU tiene su propia copia del contador, el filtro incrementa la copia de su propia CPU (sin tráfico entre CPU), y el lector de sysctl suma los valores de todas las CPU.

La API `counter(9)` de FreeBSD proporciona esto: un `counter_u64_t` es un manejador de un array por CPU, `counter_u64_add(c, 1)` incrementa la ranura de la CPU actual, y `counter_u64_fetch(c)` suma las ranuras de todas las CPU en la lectura. La implementación usa regiones de datos por CPU (`DPCPU_DEFINE` internamente) y es tan barata como un incremento no atómico normal en el camino caliente.

El driver del Capítulo 20 no usa `counter(9)`; las operaciones atómicas simples son suficientes para la escala de la demo. Los drivers reales de alto rendimiento (tarjetas de red de diez gigabits, controladores NVMe a un millón de IOPS) usan `counter(9)` de forma intensiva. Un lector que escriba ese tipo de driver debería estudiar `counter(9)` después del Capítulo 20.

### Orden de locks y complicaciones con múltiples vectores

El Capítulo 15 estableció el orden de locks del driver: `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx`. El filtro del Capítulo 19 no tomaba ningún lock (solo atómicos); la tarea tomaba `sc->mtx`. Los filtros por vector del Capítulo 20 siguen sin tomar locks (solo atómicos), por lo que el camino del filtro no añade nuevos ejes de orden de locks. Las tareas por vector siguen tomando `sc->mtx`, igual que la tarea única del Capítulo 19.

El orden de locks con múltiples tareas ejecutándose de forma concurrente requiere una pequeña ampliación. Cuando la tarea de administración y la tarea RX adquieren `sc->mtx`, se serializan en el mutex. Eso está bien siempre que cada tarea libere el mutex con prontitud; si la tarea de administración mantuviera `sc->mtx` mientras espera algo lento, la tarea RX quedaría bloqueada. La regla del Capítulo 15 de no mantener mutexes durante mucho tiempo también se aplica aquí.

WITNESS detecta la mayoría de los problemas de orden de locks. En el Capítulo 20, la situación del orden de locks es esencialmente la misma que en el Capítulo 19, porque los caminos del filtro no tienen locks y los caminos de las tareas adquieren todos el mismo `sc->mtx`.

### El modelo de memoria: por qué importan los atómicos

Hay un detalle sutil que vale la pena explicitar. En un sistema con múltiples CPU, las escrituras de una CPU no son visibles inmediatamente para las demás. Una escritura en la CPU 0 sobre `sc->intr_count++` (sin atómicos) podría quedar en el store buffer de la CPU 0 y tardar nanosegundos o microsegundos en propagarse a la vista de la misma memoria desde la CPU 3. En ese intervalo, la CPU 3 podría leer el valor anterior a la escritura.

`atomic_add_64` incluye una barrera de memoria que fuerza a que la escritura sea globalmente visible antes de que la instrucción retorne. Esto es lo que hace que el valor del contador sea «consistente» entre las CPU: cualquier lector que acceda después del incremento ve el nuevo valor.

Para el estado de un contador, este nivel de consistencia es suficiente. El valor absoluto del contador en un instante dado no importa; lo que importa es que el valor crezca de forma monótona y llegue al total correcto. `atomic_add_64` garantiza ambas cosas.

Para un estado compartido más rico (por ejemplo, un índice de estructura de datos compartido que varios filtros actualizan), el modelo de memoria se vuelve más sutil. El driver necesitaría un spinlock, que proporciona tanto exclusión mutua como una barrera de memoria. El driver del Capítulo 20 no necesita este nivel de mecanismo; la disciplina de atómicos del Capítulo 19 se mantiene.

### Observabilidad: contadores por vector en sysctl

Cada vector tiene su propio subárbol de sysctl para que el operador pueda consultarlo:

```c
char name[32];
for (int i = 0; i < MYFIRST_MAX_VECTORS; i++) {
	snprintf(name, sizeof(name), "vec%d_fire_count", i);
	SYSCTL_ADD_U64(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, name,
	    CTLFLAG_RD, &sc->vectors[i].fire_count, 0,
	    "Fire count for this vector");
}
```

Desde el espacio de usuario:

```sh
sysctl dev.myfirst.0 | grep vec
```

```text
dev.myfirst.0.vec0_fire_count: 42    # admin
dev.myfirst.0.vec0_stray_count: 0
dev.myfirst.0.vec1_fire_count: 9876  # rx
dev.myfirst.0.vec1_stray_count: 0
dev.myfirst.0.vec2_fire_count: 4523  # tx
dev.myfirst.0.vec2_stray_count: 0
```

El operador puede ver de un vistazo qué vectores están disparándose y a qué velocidad. Los contadores de disparos inesperados deberían permanecer en cero en MSI-X (cada vector tiene su propio mensaje dedicado), pero pueden incrementarse en MSI o en modo legado cuando un filtro compartido recibe un evento destinado a un vector diferente.

### Cerrando la sección 4

Los drivers con múltiples vectores cambian el panorama de la concurrencia: varios filtros pueden ejecutarse en varias CPU de forma simultánea. La disciplina consiste en diseñar el estado por vector siempre que sea posible, usar atómicos para los contadores compartidos, y respetar el orden de locks del Capítulo 11 para todo lo que requiera un mutex de tipo sleep. Los contadores por CPU (`counter(9)`) están disponibles para drivers de muy alta tasa, pero son excesivos para el Capítulo 20.

El orden de locks del driver no gana nuevos ejes porque el camino del filtro sigue sin locks (solo atómicos) y las tareas toman todas `sc->mtx`. WITNESS sigue detectando los problemas de orden de locks; la disciplina de atómicos sigue detectando el resto.

La Sección 5 pasa al mecanismo más potente: MSI-X. La API es muy similar (`pci_msix_count` + `pci_alloc_msix` en lugar del par de MSI), pero las opciones de escalabilidad y afinidad de CPU son más ricas.



## Sección 5: uso de MSI-X para mayor flexibilidad

La Sección 2 introdujo MSI con un único vector, la Sección 3 lo amplió a múltiples vectores MSI, la Sección 4 analizó las implicaciones de concurrencia. La Sección 5 pasa a MSI-X: el mecanismo más completo que usan los dispositivos PCIe modernos cuando tienen más de unos pocos vectores de interrupción que gestionar. La API es paralela a la de MSI, por lo que el cambio en el código es pequeño; el cambio conceptual es que MSI-X permite al driver vincular cada vector a una CPU específica, mediante `bus_bind_intr(9)` y `bus_get_cpus(9)`, y eso importa para el rendimiento real.

### La API de recuento y asignación de MSI-X

La API es un espejo de la de MSI:

```c
int msix_count = pci_msix_count(sc->dev);
```

`pci_msix_count(9)` devuelve el número de vectores MSI-X que anuncia el dispositivo (0 si no tiene capacidad MSI-X). El recuento proviene del campo `Table Size` de la capacidad MSI-X más uno; un dispositivo con `Table Size = 7` anuncia 8 vectores.

La asignación es similar:

```c
int count = desired;
int error = pci_alloc_msix(sc->dev, &count);
```

El mismo parámetro `count` de entrada/salida, la misma semántica: el kernel puede asignar menos de los solicitados. A diferencia de MSI, MSI-X permite un recuento que no sea potencia de dos, por lo que si el driver solicita 3 vectores, el kernel puede dar 3.

La misma llamada `pci_release_msi(9)` libera los vectores MSI-X; no existe una función `pci_release_msix` separada. El nombre de la función es un vestigio histórico; gestiona tanto MSI como MSI-X.

### La escalera de fallback ampliada

La escalera de fallback completa del driver del Capítulo 20 es:

1. **MSI-X** con el número de vectores deseado.
2. **MSI** con el número de vectores deseado, si MSI-X no está disponible o la asignación ha fallado.
3. **INTx legado** con un único manejador para todo, si tanto MSI-X como MSI fallan.

La estructura del código es paralela a la escalera de dos niveles de la Sección 3, ampliada con un tercer nivel en la cima:

```c
/* Tier 0: MSI-X. */
wanted = MYFIRST_MAX_VECTORS;
if (pci_msix_count(sc->dev) >= wanted) {
	allocated = wanted;
	if (pci_alloc_msix(sc->dev, &allocated) == 0 &&
	    allocated == wanted) {
		for (i = 0; i < wanted; i++) {
			error = myfirst_intr_setup_vector(sc, i, i + 1);
			if (error != 0)
				goto fail_msix;
		}
		sc->intr_mode = MYFIRST_INTR_MSIX;
		sc->num_vectors = wanted;
		device_printf(sc->dev,
		    "interrupt mode: MSI-X, %d vectors\n", wanted);
		myfirst_intr_bind_vectors(sc);
		goto enabled;
	}
	if (allocated > 0)
		pci_release_msi(sc->dev);
}

/* Tier 1: MSI. */
/* ... Section 3 MSI code ... */

fail_msix:
for (i -= 1; i >= 0; i--)
	myfirst_intr_teardown_vector(sc, i);
pci_release_msi(sc->dev);
/* fallthrough to MSI attempt, then legacy. */
```

La estructura es sencilla: cada nivel sigue el mismo patrón (consultar el recuento, asignar, configurar los vectores, marcar el modo, describir). El código sigue la cascada ya conocida.

### Vinculación de vectores con bus_bind_intr

Una vez asignado MSI-X, el driver tiene la opción de vincular cada vector a una CPU específica. La API es:

```c
int bus_bind_intr(device_t dev, struct resource *r, int cpu);
```

El parámetro `cpu` es un identificador entero de CPU de 0 a `mp_ncpus - 1`. Si tiene éxito, la interrupción se enruta a esa CPU. Si falla, la función devuelve un errno; el driver lo trata como una sugerencia no fatal y continúa sin la vinculación.

Para el driver de tres vectores del Capítulo 20, una vinculación razonable es:

- **Vector de administración**: CPU 0 (trabajo de control; cualquier CPU es válida).
- **Vector RX**: CPU 1 (la localidad de caché beneficia a una cola RX real).
- **Vector TX**: CPU 2 (beneficio de localidad similar).

En un sistema de dos CPU, las vinculaciones se colapsarían; en un sistema con muchas CPU, el driver debería usar `bus_get_cpus(9)` para consultar qué CPU son locales al nodo NUMA del dispositivo y distribuir los vectores en consecuencia.

La función auxiliar de vinculación:

```c
static void
myfirst_intr_bind_vectors(struct myfirst_softc *sc)
{
	int i, cpu, ncpus;
	int err;

	if (mp_ncpus < 2)
		return;  /* nothing to bind */

	ncpus = mp_ncpus;
	for (i = 0; i < sc->num_vectors; i++) {
		cpu = i % ncpus;
		err = bus_bind_intr(sc->dev, sc->vectors[i].irq_res, cpu);
		if (err != 0) {
			device_printf(sc->dev,
			    "bus_bind_intr vector %d to CPU %d: %d\n",
			    i, cpu, err);
		}
	}
}
```

El código realiza una vinculación round-robin: el vector 0 a la CPU 0, el vector 1 a la CPU 1, y así sucesivamente, con vuelta al inicio mediante módulo respecto al número de CPU. En un sistema de dos CPU con tres vectores, el vector 0 y el vector 2 van ambos a la CPU 0; en un sistema de cuatro CPU, cada vector tiene su propia CPU.

Un driver más sofisticado usa `bus_get_cpus(9)`:

```c
cpuset_t local_cpus;
int ncpus_local;

if (bus_get_cpus(sc->dev, LOCAL_CPUS, sizeof(local_cpus),
    &local_cpus) == 0) {
	/* Use only CPUs in local_cpus for binding. */
	ncpus_local = CPU_COUNT(&local_cpus);
	/* ... pick from local_cpus ... */
}
```

El argumento `LOCAL_CPUS` devuelve las CPU que están en el mismo dominio NUMA que el dispositivo. El argumento `INTR_CPUS` devuelve las CPU adecuadas para gestionar interrupciones de dispositivo (normalmente excluyendo las CPU fijas a trabajo crítico). Un driver que se preocupa por el rendimiento NUMA usa estos argumentos para colocar los vectores en CPU locales al dominio NUMA.

El driver del Capítulo 20 no usa `bus_get_cpus(9)` por defecto; la vinculación round-robin más simple es suficiente para el laboratorio. Un ejercicio de desafío añade la vinculación con conciencia de NUMA.

### El resumen de MSI-X en dmesg

El driver del Capítulo 20 imprime una línea como esta:

```text
myfirst0: interrupt mode: MSI-X, 3 vectors
```

Con las vinculaciones de CPU por vector visibles en `vmstat -i` (los totales por CPU en vmstat -i no son por vector; están agregados) y en la salida de `cpuset -g -x <irq>` (una consulta por vector):

```sh
for irq in 256 257 258; do
    echo "IRQ $irq:"
    cpuset -g -x $irq
done
```

Salida típica:

```text
IRQ 256:
irq 256 mask: 0
IRQ 257:
irq 257 mask: 1
IRQ 258:
irq 258 mask: 2
```

(Los números de IRQ dependen de la asignación de la plataforma.)

Un operador que inspeccione la configuración de interrupciones del driver puede ver qué vectores se disparan y dónde.

### bus_describe_intr por vector

Cada vector MSI-X debería tener una descripción. El código de la Sección 3 ya las establece mediante `bus_describe_intr(9)`:

```c
bus_describe_intr(sc->dev, vec->irq_res, vec->intr_cookie,
    "%s", vec->name);
```

Después de esto, `vmstat -i` muestra cada vector con su rol:

```text
irq256: myfirst0:admin                 42         4
irq257: myfirst0:rx                 12345      1234
irq258: myfirst0:tx                  5432       543
```

El operador ve qué vector es el de administración, cuál es el RX, cuál es el TX, y cuán ocupado está cada uno. Esta es una observabilidad esencial para un driver con múltiples vectores.

### Consideraciones sobre la tabla MSI-X y el BAR

Hay un detalle que merece la pena mencionar, aunque el driver no interactúa directamente con él. La estructura de capacidad MSI-X apunta a una **tabla** y a un **array de bits pendientes** (PBA), cada uno alojado en uno de los BAR del dispositivo. El BAR que alberga cada uno se puede descubrir mediante `pci_msix_table_bar(9)` y `pci_msix_pba_bar(9)`:

```c
int table_bar = pci_msix_table_bar(sc->dev);
int pba_bar = pci_msix_pba_bar(sc->dev);
```

Cada una devuelve el índice del BAR (de 0 a 5) o -1 si el dispositivo no tiene capacidad MSI-X. En la mayoría de los dispositivos, la tabla y el PBA están en el BAR 0 o el BAR 1; en algunos dispositivos, comparten un BAR con los registros mapeados en memoria (el BAR 0 del driver).

El kernel gestiona la programación de la tabla internamente. La única interacción del driver es:

- Asegurarse de que el BAR que contiene la tabla está asignado (para que el kernel pueda acceder a él). En algunos dispositivos, esto requiere que el driver asigne BARs adicionales.
- Llamar a `pci_alloc_msix` y dejar que el kernel haga el resto.

En el driver del Capítulo 20, el objetivo virtio-rnd (o su equivalente en QEMU con MSI-X) tiene la tabla en el BAR 1 o en una región dedicada. El código del Capítulo 18 asignó el BAR 0; el kernel gestiona el BAR de la tabla MSI-X de forma implícita a través de la infraestructura de asignación.

Un driver que quiera inspeccionar el BAR de la tabla:

```c
device_printf(sc->dev, "MSI-X table in BAR %d, PBA in BAR %d\n",
    pci_msix_table_bar(sc->dev), pci_msix_pba_bar(sc->dev));
```

Esto resulta útil con fines de diagnóstico.

### Asignación de menos vectores de los solicitados

Un caso sutil: el dispositivo anuncia 3 vectores MSI-X y el driver solicita 3, pero el kernel solo asigna 2. ¿Qué hace el driver?

La respuesta depende del diseño del driver. Las opciones son:

1. **Fallar en el attach.** Si el driver no puede funcionar con menos vectores, devuelve un error. Esto es poco habitual en drivers flexibles, pero posible en drivers con requisitos de hardware estrictos.
2. **Usar lo que se obtuvo.** Si el driver puede funcionar con 2 vectores (combinando RX y TX en uno, por ejemplo), usar los 2 y ajustar la configuración. Esto es habitual en drivers diseñados para distintas variantes de hardware.
3. **Liberar y recurrir al modo alternativo.** Si 2 vectores resulta peor que 1 vector MSI por alguna razón, liberar MSI-X e intentar con MSI. Esto es poco habitual.

El driver del Capítulo 20 opta por la opción 1: si no obtiene exactamente `MYFIRST_MAX_VECTORS` (3) vectores, libera MSI-X y recurre a MSI. Un driver más sofisticado usaría la opción 2; el Capítulo 20 se centra en el patrón más sencillo.

Los drivers reales de FreeBSD suelen usar la opción 2 con una función auxiliar que determina cómo distribuir los vectores asignados entre los roles deseados. El driver `nvme(4)` es un ejemplo: si solicita vectores para N colas de I/O y obtiene menos, reduce el número de colas de I/O en consecuencia.

### Prueba de MSI-X en bhyve frente a QEMU

Un detalle práctico sobre el laboratorio. El dispositivo virtio-rnd heredado de bhyve (el utilizado en los capítulos 18 y 19) no expone MSI-X; es un transporte virtio exclusivamente heredado. Para ejercitar MSI-X en un huésped, el lector necesita una de estas opciones:

- **QEMU con `-device virtio-rng-pci`** (no `-device virtio-rng`, que es la versión heredada). El `virtio-rng-pci` moderno expone MSI-X.
- **Emulación moderna de bhyve** de un dispositivo no virtio-rnd que disponga de MSI-X. El capítulo 20 no sigue esta ruta.
- **Hardware real** que soporte MSI-X (la mayoría de los dispositivos PCIe modernos).

QEMU es la opción práctica para los laboratorios del capítulo 20. La escalera de alternativas del driver garantiza que siga funcionando en bhyve (recurriendo al modo heredado); probar MSI-X en concreto requiere QEMU o hardware real.

### Errores comunes en la configuración de MSI-X

Una breve lista.

**Usar `pci_release_msix`.** Esta función no existe en FreeBSD; la liberación la gestiona `pci_release_msi(9)`, que funciona tanto para MSI como para MSI-X. Solución: usa `pci_release_msi`.

**Asignar a una CPU a la que el dispositivo no puede acceder.** Algunas plataformas (raramente) tienen CPUs que no pertenecen al conjunto enrutable del controlador de interrupciones. La llamada a `bus_bind_intr` devuelve un error; ignóralo y continúa. Solución: registra el error en el log pero no hagas que el attach falle.

**Esperar que `vmstat -i` muestre el desglose por CPU.** `vmstat -i` agrega los conteos por evento. El desglose por CPU está disponible mediante `cpuset -g -x <irq>` (o `sysctl hw.intrcnt` en formato sin procesar). El operador debe buscar en el lugar adecuado. Solución: documenta la ruta de observabilidad de tu driver.

**No comprobar `allocated` frente a `wanted`.** Aceptar una asignación parcial cuando el driver no puede gestionarla provoca bugs sutiles (vectores que deberían dispararse nunca lo hacen). Solución: decide la estrategia de antemano (fallar, adaptarse o liberar) y codifícala en consecuencia.

### Cerrando la sección 5

MSI-X es el mecanismo más completo: una tabla direccionable por vector que el kernel programa en nombre del driver, con afinidad de CPU por vector y enmascaramiento por vector disponibles para los drivers que los necesiten. La API es muy similar a la de MSI (`pci_msix_count` + `pci_alloc_msix` + `pci_release_msi`), y la asignación de recursos por vector es la misma que el código MSI de la sección 3. La novedad es `bus_bind_intr(9)` para la afinidad de CPU y `bus_get_cpus(9)` para consultas de CPUs locales por NUMA.

En el driver del capítulo 20, MSI-X es el nivel preferido; la escalera de alternativas prueba primero MSI-X, recurre a MSI y, finalmente, a INTx heredado. Los manejadores, contadores y tasks por vector de la sección 3 funcionan sin cambios en MSI-X; solo cambia la llamada de asignación.

En la sección 6 es donde los eventos específicos de cada vector se vuelven explícitos. Cada vector tiene su propio propósito, su propia lógica de filtro y su propio comportamiento observable. La etapa 3 del capítulo 20 es la etapa en la que el driver parece un dispositivo real de múltiples colas, aunque el hardware subyacente (el dispositivo virtio-rnd) sea más sencillo.



## Sección 6: Gestión de eventos específicos por vector

Las secciones 2 a 5 construyeron la infraestructura para la gestión de múltiples vectores. La sección 6 es la sección en la que los roles por vector se vuelven explícitos. Cada vector gestiona una clase de evento específica; cada filtro realiza una comprobación concreta; cada task ejecuta un despertar concreto. El driver en la etapa 3 trata los vectores como entidades con nombre y propósito definido, no como ranuras intercambiables.

Los tres vectores del driver del capítulo 20 tienen responsabilidades diferenciadas:

- **El vector de administración** gestiona eventos `ERROR`. El filtro lee el estado, lo confirma y, ante errores reales, registra un mensaje. El trabajo de administración es poco frecuente, pero no debe descartarse.
- **El vector RX** gestiona eventos `DATA_AV` (datos disponibles para recibir). El filtro confirma y delega el trabajo de gestión de datos a un task por vector que hace broadcast de una variable de condición.
- **El vector TX** gestiona eventos `COMPLETE` (transmisión completada). El filtro confirma y, opcionalmente, despierta un thread que espera la finalización de la transmisión. El filtro gestiona la contabilidad de forma inline.

Cada vector es comprobable de forma independiente mediante el sysctl de interrupción simulada, observable de forma independiente a través de sus contadores, y asignable de forma independiente a una CPU. El driver empieza a parecerse a un pequeño dispositivo real de múltiples colas.

### El vector de administración

El vector de administración gestiona eventos poco frecuentes pero importantes: cambios de configuración, errores, cambios de estado del enlace (en una NIC) o alertas de temperatura (en un sensor). Su trabajo suele ser pequeño: registrar el evento, actualizar un indicador de estado y despertar a un proceso en espacio de usuario que sondea el estado.

En el driver del capítulo 20, el vector de administración gestiona el bit `ERROR` del capítulo 17. El filtro:

```c
int
myfirst_admin_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & MYFIRST_INTR_ERROR) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_ERROR);
	atomic_add_64(&sc->intr_error_count, 1);
	return (FILTER_HANDLED);
}
```

En un dispositivo real, el filtro de administración también podría examinar un registro secundario (un registro de código de error, por ejemplo) y decidir en función de la gravedad si programar un task de recuperación. El driver del capítulo 20 lo mantiene sencillo: contar y confirmar.

### El vector RX

El vector RX es el vector de la ruta de datos. En una NIC, gestionaría los paquetes recibidos. En una unidad NVMe, las finalizaciones de peticiones de lectura. En el driver del capítulo 20, gestiona el bit `DATA_AV` del capítulo 17.

El filtro es pequeño (confirmar y encolar un task); el task realiza el trabajo real. La sección 3 mostró ambos. El task:

```c
static void
myfirst_rx_task_fn(void *arg, int npending)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->pci_attached) {
		sc->intr_last_data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
		sc->intr_task_invocations++;
		cv_broadcast(&sc->data_cv);
	}
	MYFIRST_UNLOCK(sc);
}
```

En un driver real, el task recorrería un anillo de descriptores de recepción, construiría mbufs y los pasaría a la pila de red. En la demo del capítulo 20, lee `DATA_OUT`, almacena el valor, hace broadcast de la variable de condición y permite que cualquier lector cdev que espere se despierte.

El argumento `npending` es el número de veces que el task fue encolado desde su última ejecución. En una ruta RX de alta velocidad, un task que se ejecutó una vez y vio `npending = 5` sabe que va con retraso (5 interrupciones coalescidas en 1 ejecución del task) y puede dimensionar su lote en consecuencia. El task del capítulo 20 ignora `npending`; los drivers reales lo usan para el procesamiento por lotes.

### El vector TX

El vector TX es el vector de finalización de transmisión. En una NIC, indica que un paquete que el driver entregó al hardware ha sido transmitido y que el buffer puede recuperarse. En una unidad NVMe, indica que una petición de escritura se ha completado.

En el driver del capítulo 20, gestiona el bit `COMPLETE` del capítulo 17. El filtro realiza el trabajo de forma inline (no se necesita ningún task):

```c
int
myfirst_tx_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & MYFIRST_INTR_COMPLETE) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_COMPLETE);
	atomic_add_64(&sc->intr_complete_count, 1);
	return (FILTER_HANDLED);
}
```

El diseño de solo inline del filtro TX es una decisión deliberada. En una ruta de finalización de TX real, el filtro podría registrar el conteo de finalizaciones y el task podría recorrer el anillo de descriptores TX para recuperar buffers. En la demo del capítulo 20, la finalización simplemente se cuenta.

Un diseño alternativo haría que TX también usase un task. Si conviene hacerlo depende del trabajo que haría el task: si es considerable (recorrer un anillo, recuperar decenas de buffers), un task vale la pena; si es trivial (un simple decremento de un contador en vuelo), inline en el filtro es suficiente. El capítulo 20 elige inline para TX con el fin de ilustrar que no todos los vectores necesitan un task.

### Asignación de vectores a eventos

En MSI-X, cada vector es independiente; disparar el vector 1 se entrega al filtro RX, disparar el vector 2 se entrega al filtro TX. La correspondencia entre vector y evento es parte del diseño del driver, no una decisión del kernel.

En MSI con múltiples vectores, el kernel puede en principio despachar múltiples vectores en rápida sucesión si varios eventos se disparan simultáneamente. Los filtros del driver deben leer cada uno el registro de estado y reclamar únicamente los bits pertenecientes a su vector.

En INTx heredado, solo hay un vector y un filtro. El filtro gestiona las tres clases de eventos en un único paso.

El código del capítulo 20 gestiona los tres casos: el filtro por vector en MSI-X lee únicamente su propio bit, el filtro por vector en MSI hace lo mismo (con la misma lógica de comprobación de bits), y el filtro único en INTx heredado gestiona los tres bits.

### Eventos simulados por vector

El sysctl de simulación de la sección 3 permite al lector ejercitar cada vector de forma independiente. Desde el espacio de usuario:

```sh
# Simulate an admin interrupt (ERROR).
sudo sysctl dev.myfirst.0.intr_simulate_admin=2

# Simulate an RX interrupt (DATA_AV).
sudo sysctl dev.myfirst.0.intr_simulate_rx=1

# Simulate a TX interrupt (COMPLETE).
sudo sysctl dev.myfirst.0.intr_simulate_tx=4
```

Cada sysctl escribe su bit específico en `INTR_STATUS` e invoca el filtro del vector correspondiente. En el nivel MSI-X existen los tres filtros, por lo que cada sysctl accede a su filtro por vector propio y su contador por vector se incrementa. En el nivel MSI (vector único en la ranura 0) y en el nivel INTx heredado (vector único en la ranura 0), las ranuras 1 y 2 no tienen ningún filtro registrado, por lo que el auxiliar de simulación enruta la llamada a través del filtro de la ranura 0. El `myfirst_intr_filter` del capítulo 19 gestiona los tres bits de forma inline, por lo que los contadores globales `intr_count`, `intr_data_av_count`, `intr_error_count` e `intr_complete_count` siguen actualizándose correctamente. Los contadores por vector en las ranuras 1 y 2 permanecen en cero en los niveles de manejador único, lo cual es la señal de observabilidad correcta que indica que el driver no está funcionando con tres vectores.

El lector puede observar el pipeline desde el espacio de usuario:

```sh
while true; do
    sudo sysctl dev.myfirst.0.intr_simulate_rx=1
    sleep 0.1
done &
watch sysctl dev.myfirst.0 | grep -E "vec|intr_"
```

Los contadores se incrementan aproximadamente 10 veces por segundo. El contador del vector RX coincide con `intr_data_av_count`; el conteo de invocaciones del task también coincide.

### Asignación dinámica de vectores

Un punto sutil pero importante. El diseño del driver tiene tres vectores en un array fijo con roles fijos. Un driver más flexible podría descubrir el número de vectores disponibles en tiempo de ejecución y asignar roles de forma dinámica. El patrón tiene este aspecto:

```c
/* Discover how many vectors we got. */
int nvec = actually_allocated_msix_vectors(sc);

/* Assign roles based on nvec. */
if (nvec >= 3) {
	/* Full design: admin, rx, tx. */
	sc->vectors[0].filter = myfirst_admin_filter;
	sc->vectors[1].filter = myfirst_rx_filter;
	sc->vectors[2].filter = myfirst_tx_filter;
	sc->num_vectors = 3;
} else if (nvec == 2) {
	/* Compact: admin+tx share one vector, rx has its own. */
	sc->vectors[0].filter = myfirst_admin_tx_filter;
	sc->vectors[1].filter = myfirst_rx_filter;
	sc->num_vectors = 2;
} else if (nvec == 1) {
	/* Minimal: one filter handles everything. */
	sc->vectors[0].filter = myfirst_intr_filter;
	sc->num_vectors = 1;
}
```

Esta adaptación dinámica es lo que hacen los drivers en producción. El driver del capítulo 20 usa el enfoque fijo más sencillo; un ejercicio de desafío añade la variante dinámica.

### Un patrón de nvme(4)

Como ejemplo real, el driver `nvme(4)` gestiona la cola de administración de forma separada a las colas de I/O. Sus funciones de filtro difieren por tipo de cola; sus conteos de interrupciones se rastrean por cola. El patrón es:

```c
/* In nvme_ctrlr_construct_admin_qpair: */
qpair->intr_idx = 0;  /* vector 0 for admin */
qpair->intr_rid = 1;
qpair->res = bus_alloc_resource_any(ctrlr->dev, SYS_RES_IRQ,
    &qpair->intr_rid, RF_ACTIVE);
bus_setup_intr(ctrlr->dev, qpair->res, INTR_TYPE_MISC | INTR_MPSAFE,
    NULL, nvme_qpair_msix_handler, qpair, &qpair->tag);

/* For each I/O queue: */
for (i = 0; i < ctrlr->num_io_queues; i++) {
	ctrlr->ioq[i].intr_rid = i + 2;  /* I/O vectors at rid 2, 3, ... */
	/* ... similar bus_alloc_resource_any + bus_setup_intr ... */
}
```

Cada cola tiene su propio `intr_rid`, su propio recurso, su propia etiqueta (cookie) y su propio argumento de manejador. La cola de administración usa un vector; cada cola de I/O usa su propio vector. El patrón escala linealmente con el número de colas.

El driver del capítulo 20 es una versión reducida de esto: tres vectores fijos en lugar de uno de administración más N de I/O. La idea de escalado se transfiere directamente.

### Observabilidad: tasa por vector

Un diagnóstico útil: calcula la tasa de cada vector en una ventana deslizante:

```sh
#!/bin/sh
prev_admin=$(sysctl -n dev.myfirst.0.vec0_fire_count)
prev_rx=$(sysctl -n dev.myfirst.0.vec1_fire_count)
prev_tx=$(sysctl -n dev.myfirst.0.vec2_fire_count)
sleep 1
curr_admin=$(sysctl -n dev.myfirst.0.vec0_fire_count)
curr_rx=$(sysctl -n dev.myfirst.0.vec1_fire_count)
curr_tx=$(sysctl -n dev.myfirst.0.vec2_fire_count)

echo "admin: $((curr_admin - prev_admin)) /s"
echo "rx:    $((curr_rx    - prev_rx   )) /s"
echo "tx:    $((curr_tx    - prev_tx   )) /s"
```

La salida muestra las tasas por vector durante el último segundo. Un lector que ejecute el sysctl de interrupción simulada en bucle puede ver cómo suben las tasas; un lector que observe una carga de trabajo real ve qué vector está ocupado.

### Cerrando la sección 6

Gestionar eventos específicos por vector significa que cada vector tiene su propia función de filtro, sus propios contadores, su task propio (opcional) y su propio comportamiento observable. El patrón escala: tres vectores para la demo del capítulo 20, decenas para una NIC en producción, cientos para un controlador NVMe. La separación por vector hace que cada pieza sea pequeña, específica y mantenible.

La sección 7 es la sección de desmontaje. Los drivers de múltiples vectores necesitan desmontar cada vector individualmente, en el orden correcto, y luego llamar a `pci_release_msi` una sola vez al final. El orden es estricto pero no complejo; la sección 7 lo recorre paso a paso.



## Sección 7: Desmontaje y limpieza con MSI/MSI-X

El desmontaje del capítulo 19 era un único par: `bus_teardown_intr` sobre el vector único y, a continuación, `bus_release_resource` sobre el único recurso IRQ. El desmontaje del capítulo 20 es el mismo par repetido por vector, seguido de una única llamada a `pci_release_msi` que deshace la asignación a nivel de dispositivo de MSI o MSI-X.

La sección 7 precisa el orden, recorre los casos de fallo parcial y destaca las comprobaciones de observabilidad que confirman un desmontaje limpio.

### El orden requerido

Para un driver de múltiples vectores, la secuencia de detach es:

1. **Rechazar si está ocupado.** Igual que en el capítulo 19: devolver `EBUSY` si el driver tiene descriptores abiertos o trabajo en curso.
2. **Marcar como ya no conectado.**
3. **Destruir el cdev.**
4. **Deshabilitar las interrupciones en el dispositivo.** Limpiar `INTR_MASK` para que el dispositivo deje de activar la señal de interrupción.
5. **Para cada vector en orden inverso:**
   a. `bus_teardown_intr` sobre el cookie del vector.
   b. `bus_release_resource` sobre el recurso IRQ del vector.
6. **Drenar todas las tareas por vector.** Cada tarea que haya sido inicializada.
7. **Destruir el taskqueue.**
8. **Llamar a `pci_release_msi`** una vez, incondicionalmente si `intr_mode` es MSI o MSI-X.
9. **Separar la capa de hardware y liberar el BAR** como de costumbre.
10. **Deinicializar el softc.**

Los pasos nuevos son el 5 (bucle por vector en lugar de un único par) y el 8 (`pci_release_msi`). Los pasos 1-4 y 9-10 no cambian respecto al capítulo 19.

### Por qué el orden inverso por vector

El bucle de orden inverso por vector es una medida defensiva contra las dependencias entre vectores. En un driver sencillo como el del Capítulo 20, los vectores son independientes: desmontar el vector 2 antes que el vector 1 no causa ningún problema. En un driver donde el filtro del vector 2 lee estado que el filtro del vector 1 escribe, el orden sí importa: hay que desmontar primero el escritor (vector 1) y luego el lector (vector 2).

Para la corrección del driver del Capítulo 20, tanto el orden directo como el inverso son seguros. Para mayor robustez ante cambios futuros, se prefiere el orden inverso.

### El código de desmontaje por vector

Del Section 3, el helper de desmontaje por vector:

```c
static void
myfirst_intr_teardown_vector(struct myfirst_softc *sc, int idx)
{
	struct myfirst_vector *vec = &sc->vectors[idx];

	if (vec->intr_cookie != NULL) {
		bus_teardown_intr(sc->dev, vec->irq_res, vec->intr_cookie);
		vec->intr_cookie = NULL;
	}
	if (vec->irq_res != NULL) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, vec->irq_rid,
		    vec->irq_res);
		vec->irq_res = NULL;
	}
}
```

El helper es robusto frente a una configuración parcial: si el vector nunca tuvo cookie (la configuración falló antes de `bus_setup_intr`), la comprobación `if` omite la llamada de desmontaje. Si el recurso nunca fue asignado, la segunda comprobación `if` omite la liberación. El mismo helper sirve tanto para el deshacer de fallo parcial durante la configuración como para el desmontaje completo durante el detach.

### El desmontaje completo

```c
void
myfirst_intr_teardown(struct myfirst_softc *sc)
{
	int i;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);

	/* Tear down each vector's handler, in reverse. */
	for (i = sc->num_vectors - 1; i >= 0; i--)
		myfirst_intr_teardown_vector(sc, i);

	/* Drain and destroy the taskqueue, including per-vector tasks. */
	if (sc->intr_tq != NULL) {
		for (i = 0; i < sc->num_vectors; i++) {
			if (sc->vectors[i].has_task)
				taskqueue_drain(sc->intr_tq,
				    &sc->vectors[i].task);
		}
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
	}

	/* Release the MSI/MSI-X allocation if used. */
	if (sc->intr_mode == MYFIRST_INTR_MSI ||
	    sc->intr_mode == MYFIRST_INTR_MSIX)
		pci_release_msi(sc->dev);

	sc->num_vectors = 0;
	sc->intr_mode = MYFIRST_INTR_LEGACY;
}
```

La estructura del código es directa: deshabilitar en el dispositivo, desmontaje por vector en orden inverso, drenado de tarea por vector, liberar el taskqueue, liberar MSI si se usó. El patrón se transfiere directamente a cualquier driver multi-vector.

### Deshacer un fallo parcial en attach

Durante la configuración, si el vector N falla al registrarse, el código debe deshacer los vectores 0 a N-1 que sí tuvieron éxito. El patrón:

```c
for (i = 0; i < MYFIRST_MAX_VECTORS; i++) {
	error = myfirst_intr_setup_vector(sc, i, i + 1);
	if (error != 0)
		goto fail_vectors;
}

/* Success, continue. */

fail_vectors:
	/* Undo vectors 0 through i-1. */
	for (i -= 1; i >= 0; i--)
		myfirst_intr_teardown_vector(sc, i);
	pci_release_msi(sc->dev);
	/* Fall through to next tier or final failure. */
```

El `i -= 1` es importante: tras el `goto`, `i` es el vector que falló (está más allá de las configuraciones exitosas). Deshacemos los vectores 0 a i-1, que son los que se registraron correctamente. El helper de desmontaje por vector también es seguro de llamar en el slot del vector fallido, porque sus campos son NULL (la configuración no llegó lo suficientemente lejos como para rellenarlos).

### Observabilidad: verificar un desmontaje limpio

Después de `kldunload myfirst`, debe cumplirse lo siguiente:

- `kldstat -v | grep myfirst` no devuelve nada.
- `devinfo -v | grep myfirst` no devuelve nada.
- `vmstat -i | grep myfirst` no devuelve nada.
- `vmstat -m | grep myfirst` muestra cero asignaciones activas.

Cualquier fallo apunta a un bug de limpieza:

- Una entrada restante en `vmstat -i` significa que no se llamó a `bus_teardown_intr` para ese vector.
- Una fuga en `vmstat -m` significa que alguna tarea por vector no fue drenada o que el taskqueue no fue liberado.
- Una entrada restante en `devinfo -v` (poco frecuente) significa que el detach del dispositivo no se completó.

### Fuga de recursos MSI entre ciclos de carga y descarga

Una preocupación específica de los drivers MSI/MSI-X: olvidar `pci_release_msi` deja el estado MSI del dispositivo asignado. El siguiente `kldload` del mismo driver (o de un driver distinto para el mismo dispositivo) fallará al asignar vectores MSI porque el kernel cree que ya están en uso.

El síntoma en `dmesg`:

```text
myfirst0: pci_alloc_msix returned EBUSY
```

o similar. La corrección es asegurarse de que `pci_release_msi` se ejecute en cada ruta de desmontaje, incluido el deshacer de fallo parcial.

Una prueba útil: cargar, descargar, cargar. Si la segunda carga tiene éxito con el mismo modo MSI, el desmontaje es correcto. Si la segunda carga retrocede a un nivel inferior, el desmontaje tuvo una fuga.

### Errores comunes en el desmontaje

Una lista breve.

**Olvidar `pci_release_msi`.** El error más frecuente. Síntoma: los siguientes intentos de asignación MSI fallan. Solución: llamarlo siempre que se haya usado MSI o MSI-X.

**Llamar a `pci_release_msi` en un driver que sólo usó INTx tradicional.** Técnicamente es un no-op, pero una comprobación explícita deja más claro el propósito. Solución: comprobar `intr_mode` antes de llamarlo.

**Orden incorrecto en el desmontaje por vector.** En drivers con dependencias entre vectores, el bucle de orden inverso es importante. En el driver del Capítulo 20, el orden no es crítico para las dependencias, pero mantener la disciplina del orden inverso es barato y vale la pena.

**Drenar una tarea que nunca fue inicializada.** Si un vector no tiene `has_task`, drenar su campo `task` no inicializado produce basura. Solución: comprobar `has_task` antes de drenar.

**No liberar el taskqueue.** `taskqueue_drain` no libera el taskqueue; `taskqueue_free` sí lo hace. Se necesitan ambas llamadas. Solución: llamar a las dos.

**Un deshacer de configuración parcial que deshace demasiado.** Si el vector 2 falla y el código de deshacer también desmonta el vector 2 (que nunca fue configurado), se producen desreferencias NULL. Las comprobaciones NULL del helper por vector protegen contra esto, pero la lógica en cascada también debe ser cuidadosa. Solución: usar `i -= 1` para comenzar el deshacer en el vector correcto.

### Cerrando la Sección 7

El desmontaje de un driver multi-vector se realiza vector a vector en un bucle, seguido de una única llamada a `pci_release_msi` al final. El helper por vector se comparte entre el desmontaje completo y el deshacer de fallo parcial. Las comprobaciones de observabilidad tras la descarga son las mismas que usó el Capítulo 19; cualquier fuga apunta a un bug concreto.

La Sección 8 es la sección de refactorización: dividir el código multi-vector en `myfirst_msix.c`, actualizar `INTERRUPTS.md` para reflejar las nuevas capacidades, subir la versión a `1.3-msi` y ejecutar el pase de regresión. El driver está funcionalmente completo después de la Sección 7; la Sección 8 lo hace mantenible.



## Sección 8: Refactorización y versionado de tu driver multi-vector

El manejador de interrupciones multi-vector ya está funcionando. La Sección 8 es la sección de organización. Divide el código MSI/MSI-X en su propio archivo, actualiza los metadatos del módulo, amplía el documento `INTERRUPTS.md` con los nuevos detalles multi-vector, sube la versión a `1.3-msi` y ejecuta el pase de regresión.

Este es el cuarto capítulo consecutivo que cierra con una sección de refactorización. Las refactorizaciones se acumulan: el Capítulo 16 separó la capa de hardware, el Capítulo 17 la simulación, el Capítulo 18 el attach PCI, el Capítulo 19 la interrupción tradicional. El Capítulo 20 añade la capa MSI/MSI-X. Cada responsabilidad tiene su propio archivo; el `myfirst.c` principal permanece aproximadamente constante en tamaño; el driver escala.

### La estructura final de archivos

Al final del Capítulo 20:

```text
myfirst.c           - Main driver
myfirst.h           - Shared declarations
myfirst_hw.c        - Ch16 hardware access layer
myfirst_hw_pci.c    - Ch18 hardware-layer extension
myfirst_hw.h        - Register map
myfirst_sim.c       - Ch17 simulation backend
myfirst_sim.h       - Simulation interface
myfirst_pci.c       - Ch18 PCI attach
myfirst_pci.h       - PCI declarations
myfirst_intr.c      - Ch19 interrupt handler (legacy + filter+task)
myfirst_intr.h      - Ch19 interrupt interface + ICSR macros
myfirst_msix.c      - Ch20 MSI/MSI-X multi-vector layer (NEW)
myfirst_msix.h      - Ch20 multi-vector interface (NEW)
myfirst_sync.h      - Part 3 synchronisation
cbuf.c / cbuf.h     - Ch10 circular buffer
Makefile            - kmod build
HARDWARE.md, LOCKING.md, SIMULATION.md, PCI.md, INTERRUPTS.md, MSIX.md (NEW)
```

`myfirst_msix.c` y `myfirst_msix.h` son nuevos. `MSIX.md` es nuevo. El `myfirst_intr.c` del Capítulo 19 permanece; ahora gestiona el fallback INTx tradicional mientras que `myfirst_msix.c` gestiona la ruta MSI y MSI-X.

### La cabecera myfirst_msix.h

```c
#ifndef _MYFIRST_MSIX_H_
#define _MYFIRST_MSIX_H_

#include <sys/taskqueue.h>

struct myfirst_softc;

enum myfirst_intr_mode {
	MYFIRST_INTR_LEGACY = 0,
	MYFIRST_INTR_MSI = 1,
	MYFIRST_INTR_MSIX = 2,
};

enum myfirst_vector_id {
	MYFIRST_VECTOR_ADMIN = 0,
	MYFIRST_VECTOR_RX,
	MYFIRST_VECTOR_TX,
	MYFIRST_MAX_VECTORS
};

struct myfirst_vector {
	struct resource		*irq_res;
	int			 irq_rid;
	void			*intr_cookie;
	enum myfirst_vector_id	 id;
	struct myfirst_softc	*sc;
	uint64_t		 fire_count;
	uint64_t		 stray_count;
	const char		*name;
	driver_filter_t		*filter;
	struct task		 task;
	bool			 has_task;
};

int  myfirst_msix_setup(struct myfirst_softc *sc);
void myfirst_msix_teardown(struct myfirst_softc *sc);
void myfirst_msix_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_MSIX_H_ */
```

La API pública son tres funciones: setup, teardown, add_sysctls. Los tipos enum y la estructura por vector se exportan para que `myfirst.h` pueda incluirlos y el softc pueda tener el array por vector.

### El Makefile completo

```makefile
# Makefile for the Chapter 20 myfirst driver.

KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       myfirst_msix.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.3-msi\"

.include <bsd.kmod.mk>
```

Un archivo fuente adicional en la lista SRCS; la cadena de versión actualizada.

### La cadena de versión

De `1.2-intr` a `1.3-msi`. La subida refleja una adición significativa de capacidades: el manejo de interrupciones multi-vector. Una subida de versión menor es apropiada; la interfaz visible para el usuario (el cdev) no cambió.

### El documento MSIX.md

Un nuevo documento convive junto al código fuente:

```markdown
# MSI and MSI-X Support in the myfirst Driver

## Summary

The driver probes the device's MSI-X, MSI, and legacy INTx capabilities
in that order, and uses the first one that allocates successfully. The
driver's interrupt counters, data path, and cdev behaviour are
independent of which tier the driver ends up using.

## Setup Sequence

`myfirst_msix_setup()` tries three tiers:

1. MSI-X with MYFIRST_MAX_VECTORS (3) vectors. On success:
   - Allocates per-vector IRQ resources at rid=1, 2, 3.
   - Registers a distinct filter function per vector.
   - Calls bus_describe_intr with the per-vector name.
   - Binds each vector to a CPU (round-robin or NUMA-aware).
2. MSI with MYFIRST_MAX_VECTORS vectors. Same per-vector pattern.
3. Legacy INTx with a single handler that covers all three event
   classes at rid=0.

## Per-Vector Assignment

| Vector | Purpose | Handles                         | Inline/Deferred |
|--------|---------|---------------------------------|-----------------|
| 0      | admin   | INTR_STATUS.ERROR                | Inline          |
| 1      | rx      | INTR_STATUS.DATA_AV              | Deferred (task) |
| 2      | tx      | INTR_STATUS.COMPLETE             | Inline          |

On MSI-X, each vector has its own intr_event, its own CPU affinity
(via bus_bind_intr), and its own bus_describe_intr label ("admin",
"rx", "tx"). On MSI, the driver obtains a single vector and falls
back to the Chapter 19 single-handler pattern because MSI requires
a power-of-two vector count (pci_alloc_msi rejects count=3 with
EINVAL). On legacy INTx, a single filter covers all three bits.

## sysctls

- `dev.myfirst.N.intr_mode`: 0 (legacy), 1 (MSI), 2 (MSI-X).
- `dev.myfirst.N.vec{0,1,2}_fire_count`: per-vector fire counts.
- `dev.myfirst.N.vec{0,1,2}_stray_count`: per-vector stray counts.
- `dev.myfirst.N.intr_simulate_admin`, `.intr_simulate_rx`,
  `.intr_simulate_tx`: simulate per-vector interrupts.

## Teardown Sequence

1. Disable interrupts at the device (clear INTR_MASK).
2. Per-vector in reverse: bus_teardown_intr, bus_release_resource.
3. Drain and free per-vector tasks and the taskqueue.
4. If intr_mode is MSI or MSI-X, call pci_release_msi once.

## dmesg Summary Line

A single line on attach:

- "interrupt mode: MSI-X, 3 vectors"
- "interrupt mode: MSI, 1 vector (single-handler fallback)"
- "interrupt mode: legacy INTx (1 handler for all events)"

## Known Limitations

- MYFIRST_MAX_VECTORS is hardcoded at 3. A dynamic design that
  adapts to the allocated count is a Chapter 20 challenge exercise.
- CPU binding is round-robin. NUMA-aware binding via bus_get_cpus
  is a challenge exercise.
- DMA is Chapter 21.
- iflib integration is out of scope.

## See Also

- `INTERRUPTS.md` for the Chapter 19 legacy path details.
- `HARDWARE.md` for the register map.
- `LOCKING.md` for the full lock discipline.
- `PCI.md` for the PCI attach behaviour.
```

El documento ofrece a un lector futuro el panorama completo del diseño multi-vector en una sola página.

### El pase de regresión

La regresión del Capítulo 20 es un superconjunto de la del Capítulo 19:

1. Compilar sin errores. `make` produce `myfirst.ko` sin advertencias.
2. Cargar. `kldload` muestra el banner de attach incluyendo la línea `interrupt mode:`.
3. Verificar el modo. `sysctl dev.myfirst.0.intr_mode` devuelve 0, 1 o 2 (dependiendo del huésped).
4. Attach por vector. `vmstat -i | grep myfirst` muestra N líneas (1 para legacy, 3 para MSI o MSI-X).
5. Descripción por vector. Cada entrada tiene el nombre correcto (`admin`, `rx`, `tx` o `legacy`).
6. Interrupciones simuladas. El contador de cada vector incrementa de forma independiente.
7. Ejecución de tarea. La interrupción simulada del vector RX actualiza `intr_task_invocations`.
8. Detach limpio. `devctl detach myfirst0` desmonta todos los vectores.
9. Carga tras descarga. Un segundo `kldload` usa el mismo nivel (comprueba que `pci_release_msi` funcionó).
10. `vmstat -m` no muestra fugas. Tras la descarga, no quedan asignaciones de myfirst.

El script de regresión ejecuta las diez comprobaciones. En QEMU con virtio-rng-pci, la prueba ejercita la ruta MSI-X; en bhyve con virtio-rnd, ejercita el fallback INTx tradicional. La escalera de fallback del driver garantiza que funciona en ambos casos.

### Lo que logró la refactorización

Al inicio del Capítulo 20, `myfirst` en la versión `1.2-intr` tenía un único manejador de interrupciones en la línea tradicional. Al final del Capítulo 20, `myfirst` en la versión `1.3-msi` tiene una escalera de fallback de tres niveles (MSI-X → MSI → legacy), tres filtros por vector en MSI o MSI-X, contadores por vector, afinidad de CPU por vector y una única ruta de desmontaje limpia. El número de archivos del driver ha crecido en dos; su documentación ha crecido en uno; sus capacidades funcionales han crecido de forma sustancial.

El código es reconociblemente FreeBSD. Un colaborador que abre el driver por primera vez encuentra una estructura familiar: un array por vector, funciones de filtro por vector, una escalera de configuración de tres niveles, un bus_describe_intr para cada vector y un único `pci_release_msi` en el desmontaje. Estos patrones aparecen en todos los drivers FreeBSD multi-cola.

### Cerrando la Sección 8

La refactorización sigue la forma establecida: un nuevo archivo para la nueva capa, una nueva cabecera que exporta la interfaz pública, un nuevo documento que explica el comportamiento, una subida de versión y un pase de regresión. La capa del Capítulo 20 es el manejo de interrupciones multi-vector; la del Capítulo 19 sigue siendo el fallback de un solo vector en modo legacy. Juntas forman la historia completa de interrupciones que el driver necesita.

El cuerpo instructivo del Capítulo 20 está completo. A continuación vienen los laboratorios, los desafíos, la solución de problemas, el cierre y el puente hacia el Capítulo 21.



## Lectura de un driver real: virtio_pci.c

Antes de los laboratorios, haremos un recorrido breve por un driver FreeBSD real que usa MSI-X de forma extensiva. `/usr/src/sys/dev/virtio/pci/virtio_pci.c` es el núcleo compartido de los transportes virtio-PCI tanto legacy como moderno; contiene la escalera de asignación de interrupciones que utiliza todo dispositivo virtio. Leer este archivo tras el Capítulo 20 es un breve ejercicio de reconocimiento de patrones; casi todo lo que aparece en la sección de interrupciones se corresponde con algo que el Capítulo 20 acaba de enseñar.

### La escalera de asignación

`virtio_pci.c` tiene un helper llamado `vtpci_alloc_intr_resources` (el nombre exacto varía ligeramente según la versión de FreeBSD). Su estructura es:

```c
static int
vtpci_alloc_intr_resources(struct vtpci_common *cn)
{
	int error;

	/* Tier 0: MSI-X. */
	error = vtpci_alloc_msix(cn, nvectors);
	if (error == 0) {
		cn->vtpci_flags |= VTPCI_FLAG_MSIX;
		return (0);
	}

	/* Tier 1: MSI. */
	error = vtpci_alloc_msi(cn);
	if (error == 0) {
		cn->vtpci_flags |= VTPCI_FLAG_MSI;
		return (0);
	}

	/* Tier 2: legacy INTx. */
	return (vtpci_alloc_intx(cn));
}
```

Los tres niveles son exactamente la escalera del Capítulo 20. Cada nivel, si tiene éxito, activa un indicador en el estado común y devuelve 0. Si falla, se prueba el siguiente nivel.

### El helper de asignación MSI-X

`vtpci_alloc_msix` consulta el número de vectores, decide cuántos solicitar (en función del número de virtqueues que usa el dispositivo) y llama a `pci_alloc_msix`:

```c
static int
vtpci_alloc_msix(struct vtpci_common *cn, int nvectors)
{
	int error, count;

	if (pci_msix_count(cn->vtpci_dev) < nvectors)
		return (ENOSPC);

	count = nvectors;
	error = pci_alloc_msix(cn->vtpci_dev, &count);
	if (error != 0)
		return (error);
	if (count != nvectors) {
		pci_release_msi(cn->vtpci_dev);
		return (ENXIO);
	}
	return (0);
}
```

El patrón: comprobar el número, asignar, verificar que la asignación coincide con la solicitud, liberar si hay discrepancia. Si el dispositivo anuncia menos vectores de los deseados, se devuelve `ENOSPC` inmediatamente. Si `pci_alloc_msix` asigna menos vectores de los solicitados, el código los libera y devuelve `ENXIO`.

El código del Capítulo 20 sigue esta lógica exacta (la Sección 5 mostró la versión completa).

### La asignación de recursos por vector

Una vez asignado MSI-X, virtio recorre los vectores y registra un manejador por vector:

```c
static int
vtpci_register_msix_vectors(struct vtpci_common *cn)
{
	int i, rid, error;

	rid = 1;  /* MSI-X vectors start at rid 1 */
	for (i = 0; i < cn->vtpci_num_vectors; i++) {
		cn->vtpci_vectors[i].res = bus_alloc_resource_any(
		    cn->vtpci_dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
		if (cn->vtpci_vectors[i].res == NULL)
			/* ... fail ... */;
		rid++;
		error = bus_setup_intr(cn->vtpci_dev,
		    cn->vtpci_vectors[i].res,
		    INTR_TYPE_MISC | INTR_MPSAFE,
		    NULL, vtpci_vq_handler,
		    &cn->vtpci_vectors[i], &cn->vtpci_vectors[i].cookie);
		if (error != 0)
			/* ... fail ... */;
	}
	return (0);
}
```

Dos aspectos coinciden con el Capítulo 20:

- `rid = 1` para el primer vector, incrementando por cada vector.
- El patrón de filtro (aquí `NULL`) y manejador (`vtpci_vq_handler`). Obsérvese que virtio usa un manejador solo en ithread (filter=NULL), no una tubería de filtro más tarea. Esta es una opción más sencilla que funciona para el trabajo por vector de virtio.

La función `vtpci_vq_handler` es el trabajador por vector. Cada vector recibe su propio argumento (`&cn->vtpci_vectors[i]`), y el manejador usa ese argumento para identificar qué virtqueue atender.

### El desmontaje

El desmontaje de virtio sigue el patrón del Capítulo 20:

```c
static void
vtpci_release_intr_resources(struct vtpci_common *cn)
{
	int i;

	for (i = 0; i < cn->vtpci_num_vectors; i++) {
		if (cn->vtpci_vectors[i].cookie != NULL) {
			bus_teardown_intr(cn->vtpci_dev,
			    cn->vtpci_vectors[i].res,
			    cn->vtpci_vectors[i].cookie);
		}
		if (cn->vtpci_vectors[i].res != NULL) {
			bus_release_resource(cn->vtpci_dev, SYS_RES_IRQ,
			    rman_get_rid(cn->vtpci_vectors[i].res),
			    cn->vtpci_vectors[i].res);
		}
	}

	if (cn->vtpci_flags & (VTPCI_FLAG_MSI | VTPCI_FLAG_MSIX))
		pci_release_msi(cn->vtpci_dev);
}
```

Desmontaje por vector (bus_teardown_intr + bus_release_resource), seguido de un único `pci_release_msi` al final. El orden coincide con el `myfirst_msix_teardown` del Capítulo 20.

Un detalle que merece atención: virtio usa `rman_get_rid` para recuperar el rid del recurso, en lugar de almacenarlo por separado. El driver del Capítulo 20 almacena el rid en la estructura por vector; ambas opciones son válidas, pero la del almacenamiento es más clara y más fácil de depurar.

### Lo que enseña el recorrido por virtio

Tres lecciones se transfieren directamente al diseño del Capítulo 20:

1. **La escalera de reserva de tres niveles es el patrón estándar**. Cualquier driver que necesite funcionar con una gama de hardware la implementa de la misma forma.
2. **La gestión de recursos por vector utiliza rids incrementales empezando por 1**. Esto es universal en la infraestructura PCI de FreeBSD.
3. **`pci_release_msi` se llama una sola vez, independientemente del número de vectores**. El desmontaje por vector libera los recursos IRQ; la liberación a nivel de dispositivo gestiona el estado MSI.

Un lector que pueda seguir `vtpci_alloc_intr_resources` de principio a fin habrá interiorizado el vocabulario del Capítulo 20. Para un ejemplo más completo, `/usr/src/sys/dev/nvme/nvme_ctrlr.c` muestra el mismo patrón a mayor escala, con un vector de administración más hasta `NCPU` vectores de I/O.

## Una mirada más profunda a la asignación de vectores a CPUs

La sección 5 presentó brevemente `bus_bind_intr(9)`. Esta sección profundiza un nivel más en por qué importa la asignación de CPUs, cómo los drivers reales eligen las CPUs y cuáles son las compensaciones.

### El panorama NUMA

En un sistema de un solo socket, todas las CPUs comparten un único controlador de memoria y una única jerarquía de caché. La asignación entre CPUs solo importa para la afinidad de caché (el código y los datos del handler estarán en caché caliente en cualquier CPU que lo haya ejecutado por última vez). La diferencia de rendimiento entre «CPU 0» y «CPU 3» es pequeña.

En un sistema NUMA de múltiples sockets, el panorama cambia. Cada socket tiene su propio controlador de memoria, su propia caché L3 y su propio PCIe root complex. Un dispositivo PCIe conectado al socket 0 se encuentra en el root complex de ese socket; sus registros están mapeados en memoria en un rango de direcciones gestionado por el controlador del socket 0. Se dispara una interrupción de ese dispositivo; el handler lee `INTR_STATUS`; la lectura va al BAR del dispositivo, que está en el socket 0; la CPU que ejecuta el handler debe estar en el socket 0, o la lectura cruzará la interconexión entre sockets.

La interconexión entre sockets (en sistemas Intel: UPI o el anterior QPI; en AMD: Infinity Fabric) es mucho más lenta que el acceso a la caché dentro del mismo socket. Un handler que se ejecuta en el socket equivocado verá lecturas de registros que tardan decenas de nanosegundos en lugar de unos pocos; una cola de recepción cuyos datos residen en el socket equivocado verá cada paquete cruzar la interconexión de camino al espacio de usuario.

Los vectores bien asignados mantienen el trabajo del handler en el socket donde reside el dispositivo.

### Consultar la localidad NUMA

FreeBSD expone la topología NUMA a los drivers a través de `bus_get_cpus(9)`. La API:

```c
int bus_get_cpus(device_t dev, enum cpu_sets op, size_t setsize,
    struct _cpuset *cpuset);
```

El argumento `op` selecciona qué conjunto consultar:

- `LOCAL_CPUS`: CPUs del mismo dominio NUMA que el dispositivo.
- `INTR_CPUS`: CPUs adecuadas para gestionar las interrupciones del dispositivo (normalmente `LOCAL_CPUS`, salvo que el operador haya excluido alguna).

El parámetro `cpuset` es de salida; en caso de éxito, contiene el mapa de bits de las CPUs del conjunto consultado.

Ejemplo de uso:

```c
cpuset_t local_cpus;
int num_local;

if (bus_get_cpus(sc->dev, INTR_CPUS, sizeof(local_cpus),
    &local_cpus) == 0) {
	num_local = CPU_COUNT(&local_cpus);
	device_printf(sc->dev, "device has %d interrupt-suitable CPUs\n",
	    num_local);
}
```

El driver usa `CPU_FFS(&local_cpus)` para encontrar la primera CPU del conjunto, `CPU_CLR(cpu, &local_cpus)` para marcarla como usada, y así continúa iterando.

Un bind round-robin que respeta la localidad NUMA:

```c
static void
myfirst_msix_bind_vectors_numa(struct myfirst_softc *sc)
{
	cpuset_t local_cpus;
	int cpu, i;

	if (bus_get_cpus(sc->dev, INTR_CPUS, sizeof(local_cpus),
	    &local_cpus) != 0) {
		/* No NUMA info; round-robin across all CPUs. */
		myfirst_msix_bind_vectors_roundrobin(sc);
		return;
	}

	if (CPU_EMPTY(&local_cpus))
		return;

	for (i = 0; i < sc->num_vectors; i++) {
		if (CPU_EMPTY(&local_cpus))
			bus_get_cpus(sc->dev, INTR_CPUS,
			    sizeof(local_cpus), &local_cpus);
		cpu = CPU_FFS(&local_cpus) - 1;  /* FFS returns 1-based */
		CPU_CLR(cpu, &local_cpus);
		(void)bus_bind_intr(sc->dev,
		    sc->vectors[i].irq_res, cpu);
	}
}
```

El código obtiene el conjunto de CPUs locales, selecciona la CPU con el número más bajo, vincula el vector 0 a ella, la elimina del conjunto, selecciona la siguiente de menor número, vincula el vector 1 a ella, y así sucesivamente. Si el conjunto se agota (hay más vectores que CPUs locales), lo reinicia y continúa.

El driver del capítulo 20 no incluye este binding con conciencia NUMA; un ejercicio de desafío pide al lector que lo añada.

### La perspectiva del operador

Un operador puede anular la asignación del kernel con `cpuset`:

```sh
# Get current placement for IRQ 257.
sudo cpuset -g -x 257

# Bind IRQ 257 to CPU 3.
sudo cpuset -l 3 -x 257

# Bind to a set of CPUs (kernel picks one when the interrupt fires).
sudo cpuset -l 2,3 -x 257
```

Estos comandos anulan cualquier asignación que el driver haya realizado con `bus_bind_intr`. Un operador podría hacer esto para fijar interrupciones críticas alejadas de las CPUs de la carga de trabajo de usuario (en aplicaciones de tiempo real) o para concentrar el tráfico en CPUs específicas con fines diagnósticos.

La llamada a `bus_bind_intr` del driver establece la asignación inicial; el operador puede anularla. Un driver bien diseñado fija un valor predeterminado razonable y respeta los cambios del operador (lo que ocurre de forma automática, porque `bus_bind_intr` simplemente escribe en un estado de afinidad de CPU gestionado por el sistema operativo que el operador modifica posteriormente).

### Medir el efecto

Una forma concreta de comprobar el valor de la localidad NUMA es ejecutar una carga de trabajo con una tasa alta de interrupciones con el handler fijado a una CPU local y, después, a una CPU remota, y comparar las latencias. En un sistema de dos sockets, el handler en la CPU remota suele tardar entre 1,5x y 3x más por interrupción, medido en ciclos de CPU.

El proveedor DTrace de FreeBSD puede medirlo:

```sh
sudo dtrace -n '
fbt::myfirst_intr_filter:entry { self->ts = vtimestamp; }
fbt::myfirst_intr_filter:return /self->ts/ {
    @[cpu] = quantize(vtimestamp - self->ts);
    self->ts = 0;
}'
```

La salida es un histograma por CPU de las latencias del filtro. El lector puede ejecutar esto mientras observa las asignaciones de vectores y confirmar la diferencia de latencia.

### Cuándo importa la asignación de vectores

- Tasas de interrupción elevadas (más de unos pocos miles por segundo por vector).
- Una huella de líneas de caché grande en el handler (el código y los datos del handler ocupan múltiples líneas de caché).
- Rutas de recepción compartidas con procesamiento posterior en el mismo socket.
- Sistemas NUMA con más de un socket y dispositivos PCIe conectados a sockets específicos.

### Cuándo no importa la asignación de vectores

- Interrupciones de baja frecuencia (decenas por segundo o menos).
- Sistemas de un solo socket.
- Handlers que realizan un trabajo mínimo (el vector admin del capítulo 20).
- Drivers que se ejecutan en una sola CPU independientemente (sistemas embebidos de una sola CPU).

El driver del capítulo 20 se encuentra en la categoría de «no importa realmente» para las pruebas normales, pero los patrones que enseña el capítulo se aplican directamente a los drivers en los que sí importa.



## Una mirada más profunda a las estrategias de asignación de vectores

La sección 6 mostró el patrón de asignación fija (vector 0 = admin, 1 = rx, 2 = tx). Esta sección explora otras estrategias de asignación que utilizan los drivers reales.

### Un vector por cola

La estrategia más sencilla y la más común. Cada cola (cola rx, cola tx, cola admin, etc.) tiene su propio vector dedicado. El driver asigna `N+M+1` vectores para `N` colas de recepción, `M` colas de transmisión y 1 vector admin.

Ventajas:
- Lógica de handler sencilla por vector.
- La tasa de interrupción de cada cola es independiente.
- La afinidad de CPU es por cola (fácil de fijar a la CPU local en NUMA).

Desventajas:
- Consume muchos vectores para drivers con muchas colas.
- El ithread de cada cola añade sobrecarga en colas de baja frecuencia.

Este es el patrón que utiliza `nvme(4)`.

### Vector RX+TX fusionado

Algunos drivers fusionan los canales RX y TX de un par de colas en un único vector. Una NIC con 8 pares de colas usaría 8 vectores fusionados más unos pocos para admin. Cuando el vector se dispara, el filtro comprueba tanto los bits de estado RX como TX y despacha en consecuencia.

Ventajas:
- La mitad de vectores por par de colas.
- Los canales RX y TX del mismo par de colas tienden a ser locales entre sí en NUMA (comparten la misma memoria de anillo de descriptores).

Desventajas:
- El filtro es algo más complejo.
- RX y TX pueden interferirse bajo carga (una ráfaga de RX ocupa el tiempo del handler, retrasando las finalizaciones de TX).

Es un diseño intermedio, utilizado por algunas NICs de consumo.

### Un vector para todas las colas

Algunos dispositivos muy limitados (NICs de bajo coste, pequeños dispositivos embebidos) tienen en total solo uno o dos vectores MSI-X. El driver utiliza un único vector para todas las colas y despacha a cada cola basándose en un registro de estado.

Ventajas:
- Funciona en hardware con pocos vectores.
- Asignación sencilla.

Desventajas:
- Sin afinidad por cola.
- El filtro realiza más trabajo para decidir qué despachar.

Este es el patrón que utiliza un driver en hardware de gama muy baja.

### Asignación dinámica por CPU

Un diseño inteligente: asignar un vector por CPU y asignar colas a vectores de forma dinámica. Una cola RX es «propiedad» de una CPU en un momento dado; se procesa en el vector de esa CPU. Si la carga de trabajo cambia, el driver puede reasignar colas a diferentes CPUs.

Ventajas:
- Afinidad de caché por CPU óptima.
- Se adapta a los cambios en la carga de trabajo.

Desventajas:
- Lógica compleja de asignación y reasignación.
- Difícil de razonar sobre ella.

Algunos drivers de NICs de alta gama (series Mellanox ConnectX, Intel 800 Series) utilizan variantes de este diseño.

### La estrategia del capítulo 20

El driver del capítulo 20 utiliza la estrategia de asignación fija con tres vectores. Es la estrategia más sencilla que ilustra el diseño multivector sin entrar en detalles de NUMA ni en reasignaciones dinámicas. Los drivers reales suelen comenzar con este diseño y evolucionar hacia patrones más sofisticados a medida que los requisitos lo exigen.

Un ejercicio de desafío pide al lector que implemente la estrategia de asignación dinámica por CPU como extensión.



## Una mirada más profunda a la moderación y la fusión de interrupciones

Un concepto próximo a MSI-X que merece una breve mención. Los dispositivos modernos de alto rendimiento suelen admitir la **moderación de interrupciones** o **coalescing** (fusión): el dispositivo acumula eventos (paquetes entrantes, finalizaciones) y dispara una única interrupción para múltiples eventos, ya sea superado un umbral de tiempo o un umbral de cuenta.

### Por qué importa la moderación

Una NIC que recibe diez millones de paquetes por segundo dispararía diez millones de interrupciones si cada paquete generara una. Eso es demasiado; la CPU pasaría todo su tiempo entrando y saliendo de los handlers de interrupción. La solución es agrupar: la NIC dispara una interrupción cada 50 microsegundos, y durante esos 50 microsegundos acumula todos los paquetes que llegan. El handler procesa todos los paquetes acumulados de una sola vez.

El coalescing intercambia latencia por rendimiento: cada paquete tarda hasta 50 microsegundos más en llegar al espacio de usuario, pero la CPU gestiona millones de paquetes por segundo con una tasa de interrupciones manejable.

### Cómo controlan los drivers la moderación

El mecanismo es específico de cada dispositivo. Las formas más comunes son:

- **Basado en tiempo:** el dispositivo dispara tras un intervalo configurado (p. ej., 50 microsegundos).
- **Basado en cuenta:** el dispositivo dispara tras N eventos (p. ej., 16 paquetes).
- **Combinado:** el que se alcance primero.
- **Adaptativo:** el dispositivo (o el driver) ajusta los umbrales según las tasas observadas.

El driver normalmente programa los umbrales a través de los registros del dispositivo. El propio mecanismo MSI-X no proporciona moderación; es una característica del dispositivo que funciona junto con MSI-X porque MSI-X permite la asignación por vector.

### El driver del capítulo 20 no modera

El driver del capítulo 20 no tiene moderación. Cada interrupción simulada produce una llamada al filtro. En hardware real esto sería un problema a tasas elevadas; en el laboratorio no hay inconveniente.

Los drivers reales como `em(4)`, `ix(4)`, `ixl(4)` y `mgb(4)` tienen parámetros de moderación. La interfaz `sysctl` los expone como valores ajustables:

```sh
sysctl dev.em.0 | grep itr
```

El lector que adapte el driver del capítulo a un dispositivo real debería estudiar los controles de moderación de ese dispositivo. El mecanismo es ortogonal a MSI-X; los dos se combinan para proporcionar un manejo de interrupciones de alto rendimiento.



## Patrones de drivers reales de FreeBSD

Un recorrido por los patrones multivector que aparecen en `/usr/src/sys/dev/`. Cada patrón es un fragmento breve de un driver real, con una nota sobre lo que enseña para el capítulo 20.

### Patrón: división de vectores admin + I/O en `nvme(4)`

`/usr/src/sys/dev/nvme/nvme_ctrlr.c` tiene el patrón canónico admin más N:

```c
/* Allocate one vector for admin + N for I/O. */
num_trackers = MAX(1, MIN(mp_ncpus, ctrlr->max_io_queues));
num_vectors_requested = num_trackers + 1;  /* +1 for admin */
num_vectors_allocated = num_vectors_requested;
pci_alloc_msix(ctrlr->dev, &num_vectors_allocated);

/* Admin queue uses vector 0 (rid 1). */
ctrlr->adminq.intr_rid = 1;
ctrlr->adminq.res = bus_alloc_resource_any(ctrlr->dev, SYS_RES_IRQ,
    &ctrlr->adminq.intr_rid, RF_ACTIVE);
bus_setup_intr(ctrlr->dev, ctrlr->adminq.res,
    INTR_TYPE_MISC | INTR_MPSAFE,
    NULL, nvme_qpair_msix_handler, &ctrlr->adminq, &ctrlr->adminq.tag);

/* I/O queues use vectors 1..N (rid 2..N+1). */
for (i = 0; i < ctrlr->num_io_queues; i++) {
	ctrlr->ioq[i].intr_rid = i + 2;
	/* same pattern ... */
}
```

Por qué importa: el patrón admin más N es la elección correcta cuando un vector gestiona trabajo infrecuente y de alta prioridad (errores, eventos asíncronos) y N vectores gestionan trabajo limitado en frecuencia, por cola. La división admin/rx/tx del capítulo 20 es una versión en miniatura de este patrón.

### Patrón: vector de par de colas en ixgbe

`/usr/src/sys/dev/ixgbe/ix_txrx.c` utiliza un diseño de par de colas donde cada vector gestiona tanto el RX como el TX de un par de colas:

```c
/* One vector per queue pair + 1 for link. */
for (i = 0; i < num_qpairs; i++) {
	que[i].rid = i + 1;
	/* Filter checks both RX and TX status bits and dispatches. */
	bus_setup_intr(..., ixgbe_msix_que, &que[i], ...);
}
/* Link-state vector is the last one. */
link.rid = num_qpairs + 1;
bus_setup_intr(..., ixgbe_msix_link, sc, ...);
```

Por qué importa: el diseño de RX+TX fusionado por par de colas reduce a la mitad el número de vectores sin sacrificar la afinidad por cola. Es adecuado cuando el dispositivo tiene muchas colas pero pocos vectores.

### Patrón: vector por virtqueue en virtio_pci

`/usr/src/sys/dev/virtio/pci/virtio_pci.c` tiene un vector por virtqueue:

```c
int nvectors = ... /* count of virtqueues + 1 for config */;
pci_alloc_msix(dev, &nvectors);
for (i = 0; i < nvectors; i++) {
	vec[i].rid = i + 1;
	/* Each vector gets the per-virtqueue data as its arg. */
	bus_setup_intr(dev, vec[i].res, ..., virtio_vq_intr, &vec[i], ...);
}
```

Por qué importa: la asignación por virtqueue de virtio es el modelo para cualquier dispositivo paravirtualizado. El número de vectores es igual al número de virtqueues más admin/config.

### Patrón: vector por puerto en ahci

`/usr/src/sys/dev/ahci/ahci_pci.c` utiliza un vector por puerto SATA:

```c
for (i = 0; i < ahci->nports; i++) {
	ahci->ports[i].rid = i + 1;
	/* ... */
}
```

Por qué importa: los controladores de almacenamiento suelen utilizar asignaciones de vectores por puerto para que las finalizaciones de I/O en diferentes puertos puedan procesarse de forma concurrente en diferentes CPUs.

### Patrón: gestión de vectores oculta en iflib

Los drivers que usan `iflib(9)` (como `em(4)`, `igc(4)`, `ix(4)`, `ixl(4)`, `mgb(4)`) no gestionan los vectores directamente. En su lugar, registran funciones de handler por cola en la tabla de registro de iflib, y es iflib quien realiza la asignación y el binding:

```c
static struct if_shared_ctx em_sctx_init = {
	/* ... */
	.isc_driver = &em_if_driver,
	.isc_tx_maxsize = EM_TSO_SIZE,
	/* ... */
};

static int
em_if_msix_intr_assign(if_ctx_t ctx, int msix)
{
	struct e1000_softc *sc = iflib_get_softc(ctx);
	int error, rid, i, vector = 0;

	/* iflib has already called pci_alloc_msix; sc knows the count. */
	for (i = 0; i < sc->rx_num_queues; i++, vector++) {
		rid = vector + 1;
		error = iflib_irq_alloc_generic(ctx, ..., rid, IFLIB_INTR_RXTX,
		    em_msix_que, ...);
	}
	return (0);
}
```

Por qué importa: iflib abstrae la asignación MSI-X y el binding por cola detrás de una API limpia. Los drivers que usan iflib son más sencillos que los drivers MSI-X directos, pero sacrifican algo de flexibilidad. El patrón iflib es la elección correcta para los nuevos drivers de red de FreeBSD; el patrón MSI-X directo es la elección correcta para dispositivos que no son de red o cuando iflib no encaja.

### Lo que enseñan los patrones

Todos estos drivers siguen el mismo patrón estructural que enseña el capítulo 20:

1. Consultar el número de vectores.
2. Asignar los vectores.
3. Para cada vector: asignar el recurso de IRQ con rid=i+1, registrar el handler, describir.
4. Vincular los vectores a las CPUs.
5. Al desmontar: desmontaje por vector en orden inverso, y después `pci_release_msi`.

Las diferencias entre los drivers son:

- Cuántos vectores se usan (1, unos pocos, decenas o centenares).
- Cómo se asignan los vectores (admin+N, por par de colas, por puerto, por virtqueue).
- Si iflib gestiona la asignación.
- Qué hace cada función de filtro (admin frente a data-path).

Un lector que domine el vocabulario del Capítulo 20 puede reconocer estas diferencias de inmediato.

## Una observación sobre el rendimiento: midiendo el beneficio de MSI-X

Una sección que ancla las afirmaciones de rendimiento del capítulo en una medición concreta.

### La configuración del banco de pruebas

Supón que tienes el driver del Capítulo 20 ejecutándose en QEMU con `virtio-rng-pci` (con MSI-X activo) y un invitado con varios CPU. El sysctl `intr_simulate_rx` te permite disparar interrupciones desde un bucle en espacio de usuario:

```sh
# In one shell, drive simulated RX interrupts as fast as possible.
while true; do
    sudo sysctl dev.myfirst.0.intr_simulate_rx=1 >/dev/null 2>&1
done
```

### Medición con DTrace

En otro shell, mide el tiempo de CPU del filtro por invocación y en qué CPU se ejecuta:

```sh
sudo dtrace -n '
fbt::myfirst_rx_filter:entry { self->ts = vtimestamp; self->c = cpu; }
fbt::myfirst_rx_filter:return /self->ts/ {
    @lat[self->c] = quantize(vtimestamp - self->ts);
    self->ts = 0;
    self->c = 0;
}'
```

La salida es un histograma por CPU de las latencias del filtro. Si `bus_bind_intr` colocó el vector RX en el CPU 1, el histograma debería mostrar todas las invocaciones en el CPU 1, con latencias de unos cientos de nanosegundos a pocos microsegundos.

### Lo que muestran los resultados

Con un vector MSI-X bien asignado:

- Cada invocación se ejecuta en el mismo CPU (el CPU vinculado).
- Las latencias son consistentemente cortas (las líneas de caché calientes permanecen en un solo CPU).
- No hay rebote de caché entre CPUs.

Con una línea compartida INTx legacy:

- Las invocaciones se reparten entre CPUs (el kernel las enruta aleatoriamente).
- Las latencias son más variables (líneas de caché frías en cada nuevo CPU).
- El tráfico de caché entre CPUs aparece en los contadores de rendimiento.

La diferencia puede medirse en nanosegundos por invocación. Para un driver que gestiona unos cientos de interrupciones por segundo, la diferencia es imperceptible. Para un driver que gestiona un millón de interrupciones por segundo, la diferencia es la que separa «funciona» de «no funciona».

### La lección general

Los mecanismos del Capítulo 20 son excesivos para drivers de baja tasa. Son esenciales para drivers de alta tasa. Los patrones que enseña el capítulo escalan desde «un driver de demostración con cien interrupciones por segundo» hasta «una NIC de producción con diez millones». Saber en qué punto de esa escala se sitúa un driver concreto determina cuánto de lo que aconseja el Capítulo 20 importa en la práctica.



## Un análisis más profundo del diseño del árbol de sysctl para drivers multivector

El driver del Capítulo 20 expone sus contadores por vector como sysctls planos (`vec0_fire_count`, `vec1_fire_count`, `vec2_fire_count`). Para un driver con muchos vectores, un espacio de nombres plano resulta difícil de manejar. Esta sección muestra cómo usar `SYSCTL_ADD_NODE` para construir un árbol de sysctls por vector.

### La disyuntiva entre espacio plano y árbol

Espacio de nombres plano (el que usa el Capítulo 20):

```text
dev.myfirst.0.vec0_fire_count: 42
dev.myfirst.0.vec1_fire_count: 9876
dev.myfirst.0.vec2_fire_count: 4523
dev.myfirst.0.vec0_stray_count: 0
dev.myfirst.0.vec1_stray_count: 0
dev.myfirst.0.vec2_stray_count: 0
```

Ventajas: simple, sin llamadas a `SYSCTL_ADD_NODE`.
Inconvenientes: muchos elementos en el nivel superior; sin agrupamiento.

Espacio de nombres en árbol:

```text
dev.myfirst.0.vec.admin.fire_count: 42
dev.myfirst.0.vec.admin.stray_count: 0
dev.myfirst.0.vec.rx.fire_count: 9876
dev.myfirst.0.vec.rx.stray_count: 0
dev.myfirst.0.vec.tx.fire_count: 4523
dev.myfirst.0.vec.tx.stray_count: 0
```

Ventajas: agrupa el estado por vector; escala a muchos vectores; nombrado en lugar de numerado.
Inconvenientes: más código de configuración.

### El código de construcción del árbol

```c
void
myfirst_msix_add_sysctls(struct myfirst_softc *sc)
{
	struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
	struct sysctl_oid *parent = sc->sysctl_tree;
	struct sysctl_oid *vec_node;
	struct sysctl_oid *per_vec_node;
	int i;

	/* Create the "vec" parent node. */
	vec_node = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(parent),
	    OID_AUTO, "vec", CTLFLAG_RD, NULL,
	    "Per-vector interrupt statistics");

	for (i = 0; i < MYFIRST_MAX_VECTORS; i++) {
		/* Create "vec.<name>" node. */
		per_vec_node = SYSCTL_ADD_NODE(ctx,
		    SYSCTL_CHILDREN(vec_node),
		    OID_AUTO, sc->vectors[i].name,
		    CTLFLAG_RD, NULL,
		    "Per-vector statistics");

		/* Add fire_count under it. */
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(per_vec_node),
		    OID_AUTO, "fire_count", CTLFLAG_RD,
		    &sc->vectors[i].fire_count, 0,
		    "Times this vector's filter was called");

		/* Add stray_count. */
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(per_vec_node),
		    OID_AUTO, "stray_count", CTLFLAG_RD,
		    &sc->vectors[i].stray_count, 0,
		    "Stray returns from this vector");

		/* Other per-vector fields... */
	}
}
```

Las llamadas a `SYSCTL_ADD_NODE` crean los nodos intermedios; las llamadas posteriores a `SYSCTL_ADD_U64` adjuntan contadores hoja bajo ellos. La estructura en árbol resulta visible en la salida de `sysctl` de forma automática.

### Consulta del árbol

```sh
# Show all per-vector stats.
sysctl dev.myfirst.0.vec

# Show just the rx vector.
sysctl dev.myfirst.0.vec.rx

# Show only fire counts.
sysctl -n dev.myfirst.0.vec.admin.fire_count dev.myfirst.0.vec.rx.fire_count dev.myfirst.0.vec.tx.fire_count
```

La estructura en árbol hace que el espacio de nombres de sysctl sea mucho más legible, especialmente para drivers con muchos vectores (NVMe con 32 colas de I/O, o una NIC con 16 pares de colas).

### Cuándo usar el árbol

Para el driver de tres vectores del Capítulo 20, el espacio de nombres plano es suficiente. Para un driver con ocho o más vectores, el árbol resulta valioso. Un lector que escriba un driver de producción debería usar el árbol.

### Errores frecuentes

- **Pérdida del nodo padre.** `SYSCTL_ADD_NODE` registra el nodo en `sc->sysctl_ctx`; se libera junto con el resto del contexto. No es necesario un `free` explícito.
- **Olvidar `NULL` en el argumento del manejador.** `SYSCTL_ADD_NODE` no es un CTLPROC; es un nodo de agrupamiento puro. El argumento del manejador es `NULL`.
- **Padre incorrecto en las llamadas hijas a `SYSCTL_ADD_*`.** Usa `SYSCTL_CHILDREN(vec_node)` para los hijos de `vec_node`, no `SYSCTL_CHILDREN(parent)`.

Este patrón de diseño en árbol es la forma más limpia de exponer el estado multivector. El ejercicio de desafío del Capítulo 20 sugiere implementarlo como extensión.



## Un análisis más profundo de los caminos de error en la configuración de MSI-X

La Sección 3 y la Sección 5 mostraron el código de configuración del camino feliz. Esta sección recorre qué puede salir mal y cómo diagnosticarlo.

### Modo de fallo 1: pci_msix_count devuelve 0

Síntoma: el intento de MSI-X se omite porque el contador es 0.

Causa: el dispositivo no tiene capacidad MSI-X, o el driver del bus PCI no la ha detectado.

Solución: confirma con `pciconf -lvc`. Si el dispositivo anuncia MSI-X pero `pci_msix_count` devuelve 0, la configuración PCI del dispositivo está rota o el probe del kernel no la encontró; es un caso poco frecuente y difícil de solucionar desde el driver.

### Modo de fallo 2: pci_alloc_msix devuelve EINVAL

Síntoma: la asignación falla con `EINVAL`.

Causa: el driver solicita una cantidad mayor que el máximo anunciado por el dispositivo, o está solicitando 0.

Solución: limita la cantidad solicitada al valor devuelto por `pci_msix_count`. Solicita siempre al menos 1.

### Modo de fallo 3: pci_alloc_msix devuelve menos vectores de los solicitados

Síntoma: el valor de `count` tras la llamada es menor que el solicitado.

Causa: el pool de vectores del kernel estaba parcialmente agotado; la asignación del dispositivo recibió lo que quedaba.

Solución: decide de antemano si aceptar, adaptar o liberar. El driver del Capítulo 20 libera y cae de nuevo a MSI.

### Modo de fallo 4: bus_alloc_resource_any devuelve NULL para un vector MSI-X

Síntoma: tras el éxito de `pci_alloc_msix`, la asignación del IRQ por vector falla.

Causas:
- rid incorrecto (usando 0 en lugar de i+1).
- Ya liberado previamente (doble liberación).
- Sin recursos IRQ en la capa del bus.

Solución: comprueba que el rid es i+1. Revisa el código de liberación. Registra el error.

### Modo de fallo 5: bus_setup_intr devuelve EINVAL para un manejador por vector

Síntoma: `bus_setup_intr` falla.

Causas:
- Filtro e ithread ambos NULL.
- Falta el flag `INTR_TYPE_*`.
- Ya configurado previamente (doble configuración).

Solución: asegúrate de que el argumento del filtro no sea NULL. Incluye un flag `INTR_TYPE_*`. Revisa el código de configuración en busca de dobles registros.

### Modo de fallo 6: bus_bind_intr devuelve un error

Síntoma: `bus_bind_intr` devuelve un valor distinto de cero.

Causas:
- La plataforma no admite reasignación.
- CPU fuera de rango.
- Configuración del kernel (NO_SMP, NUMA desactivado).

Solución: trátalo como no fatal (muestra una advertencia con `device_printf` y continúa). El driver sigue funcionando sin la vinculación.

### Modo de fallo 7: vmstat -i muestra los vectores pero los contadores no se incrementan

Síntoma: el kernel ve los vectores pero los filtros nunca se disparan.

Causas:
- El `INTR_MASK` del dispositivo es cero (problema del Capítulo 19).
- El dispositivo reinició su estado de interrupción.
- Un bug de hardware o un problema de configuración de bhyve/QEMU.

Solución: verifica el INTR_MASK del dispositivo. Usa el sysctl de interrupción simulada para confirmar que el filtro funciona en absoluto.

### Modo de fallo 8: el segundo kldload cae a un nivel inferior

Síntoma: la primera carga usa MSI-X; se descarga; la segunda carga usa legacy o MSI.

Causa: `pci_release_msi` no se llama durante el desmontaje.

Solución: revisa el camino de desmontaje. Asegúrate de que `pci_release_msi` se ejecuta en cada camino de asignación exitoso.

### Modo de fallo 9: WITNESS genera un pánico en la configuración multivector

Síntoma: `WITNESS` reporta una violación del orden de locks o un «lock held during sleep» en la configuración por vector.

Causa: mantener `sc->mtx` a lo largo de una llamada a `bus_setup_intr`. Los hooks del bus pueden dormir, y mantener un mutex mientras se duerme es ilegal.

Solución: libera `sc->mtx` antes de llamar a `bus_setup_intr`. Adquiérelo de nuevo después si es necesario.

### Modo de fallo 10: la configuración parcial no limpia correctamente

Síntoma: el attach falla; el segundo attach falla con «resource in use».

Causa: la cascada de `goto` de fallo parcial no deshace todo el camino. Queda algún estado por vector.

Solución: asegúrate de que la cascada se deshace hasta el vector que falló, sin pasarlo. Usa el helper por vector de forma consistente.



## Resolución de problemas adicionales

Unos cuantos modos de fallo adicionales que los lectores del Capítulo 20 podrían encontrar.

### «El invitado de QEMU no expone MSI-X»

Causas: versión de QEMU demasiado antigua, o el invitado arranca con virtio legacy.

Solución: actualiza QEMU a una versión reciente. En el invitado, comprueba:

```sh
pciconf -lvc | grep -B 1 -A 2 'cap 11'
```

Si no aparecen líneas `cap 11`, MSI-X no está disponible. Cambia al virtio-rng-pci moderno de QEMU con `-device virtio-rng-pci,disable-legacy=on`.

### «intr_simulate_rx incrementa fire_count pero la tarea nunca se ejecuta»

Causa: no se llamó a `TASK_INIT` de la tarea, o el taskqueue no se inició.

Solución: verifica `TASK_INIT(&vec->task, 0, myfirst_rx_task_fn, vec)` en la configuración. Verifica `taskqueue_start_threads(&sc->intr_tq, ...)`.

### «Los contadores por vector se incrementan pero el recuento de strays sube proporcionalmente»

Causa: la comprobación de estado del filtro es incorrecta, o varios vectores están disparándose sobre el mismo bit.

Solución: cada filtro debe comprobar sus bits específicos. Si dos filtros intentan manejar `DATA_AV`, uno ganará y el otro verá un stray.

### «cpuset -g -x $irq reporta mask 0 en todos los vectores»

Causa: `bus_bind_intr` no se ha llamado, o se llamó con el CPU 0 (máscara 1).

Solución: si la desvinculación es intencionada, «mask 0» puede ser específico de la plataforma. Si se intentó la vinculación, comprueba el valor de retorno de `bus_bind_intr`.

### «La carga del driver tiene éxito pero dmesg no muestra el banner de attach»

Causa: el `device_printf` se ejecutó antes del vaciado del banner, o el banner está en un buffer de arranque muy temprano.

Solución: `dmesg -a` muestra el buffer de mensajes completo. Verifica `dmesg -a | grep myfirst`.

### «El detach se bloquea tras la configuración multivector»

Causa: el manejador de un vector sigue ejecutándose cuando el desmontaje intenta continuar. `bus_teardown_intr` se bloquea esperándolo.

Solución: asegúrate de que el `INTR_MASK` del dispositivo se borra *antes* de `bus_teardown_intr`, de modo que no se puedan despachar nuevos manejadores. Asegúrate de que el filtro no entra en bucle indefinido; disciplina de tiempo de ejecución corto.

### «pci_alloc_msix tiene éxito pero solo algunos vectores se disparan»

Causa: el dispositivo no está señalizando realmente en los vectores que debería. Puede ser un bug del driver (olvidó habilitarlos) o una particularidad del dispositivo.

Solución: usa el sysctl de interrupción simulada para confirmar que el filtro funciona para cada vector. Si el camino simulado funciona pero los eventos reales no disparan el vector, el problema está en el lado del dispositivo.



## Ejemplo práctico: trazando un evento a través de los tres niveles

Para hacer concreto el esquema de fallback, aquí tienes un recorrido completo del mismo evento (una interrupción DATA_AV simulada) en cada uno de los tres niveles.

### Nivel 3: INTx legacy

En bhyve con virtio-rnd (sin MSI-X expuesto), el driver cae al INTx legacy con un manejador en rid 0.

1. El usuario ejecuta `sudo sysctl dev.myfirst.0.intr_simulate_admin=1` (o `intr_simulate_rx=1`, etc.).
2. El manejador del sysctl adquiere `sc->mtx`, escribe el bit en INTR_STATUS, lo libera y llama al filtro.
3. Se ejecuta el único `myfirst_intr_filter` (del Capítulo 19). Lee INTR_STATUS, ve el bit, lo reconoce y o bien encola la tarea (para DATA_AV) o la maneja en línea (para ERROR/COMPLETE).
4. `intr_count` se incrementa.
5. En legacy, solo hay un vector, así que los tres sysctls de interrupción simulada pasan por el mismo filtro.

Observaciones:
- `sysctl dev.myfirst.0.intr_mode` devuelve 0.
- `vmstat -i | grep myfirst` muestra una línea.
- Los contadores por vector no existen (el modo legacy usa los contadores del Capítulo 19).

### Nivel 2: MSI

En un sistema que admite MSI pero no MSI-X, el driver asigna un único vector MSI. MSI requiere una cantidad de vectores que sea potencia de dos, por lo que el driver no puede pedir 3 aquí; solicita 1 y usa el patrón de manejador único del Capítulo 19.

1. El usuario ejecuta `sudo sysctl dev.myfirst.0.intr_simulate_admin=1` (o `intr_simulate_rx=1`, o `intr_simulate_tx=4`).
2. Como solo hay un vector configurado en el nivel MSI, los tres sysctls de simulación por vector se enrutan a través del mismo `myfirst_intr_filter` del Capítulo 19.
3. El filtro lee INTR_STATUS, ve el bit, lo reconoce y o bien lo maneja en línea o encola la tarea.

Observaciones:
- `sysctl dev.myfirst.0.intr_mode` devuelve 1.
- `vmstat -i | grep myfirst` muestra una línea (el único manejador MSI en rid=1, etiquetado como «msi»).
- Los contadores por vector en los slots 1 y 2 permanecen en 0 porque solo el slot 0 está en uso; los contadores globales del Capítulo 19 (`intr_count`, `intr_data_av_count`, etc.) son los que cambian.

### Nivel 1: MSI-X

En QEMU con virtio-rng-pci, el driver asigna MSI-X con 3 vectores, cada uno vinculado a un CPU.

1. El usuario ejecuta `sudo sysctl dev.myfirst.0.intr_simulate_rx=1`.
2. El sysctl invoca el filtro rx directamente (la ruta simulada no pasa por el hardware).
3. `myfirst_rx_filter` se ejecuta (en el CPU desde el que se invocó el sysctl, porque la simulación no pasa por el despacho de interrupciones del kernel).
4. Los contadores se incrementan y la tarea se ejecuta.

Observaciones:
- `sysctl dev.myfirst.0.intr_mode` devuelve 2.
- `vmstat -i | grep myfirst` muestra tres líneas; cada una tiene un número de IRQ diferente.
- `cpuset -g -x <irq>` para cada IRQ muestra máscaras de CPU distintas.

Una interrupción MSI-X real (no simulada) se despacharía en el CPU al que está vinculada; el bypass de la simulación la hace ejecutarse en el CPU del thread que realizó la llamada. Esta es una limitación de la técnica de simulación, pero no afecta a la corrección del resultado.

### La lección

Los tres niveles ejecutan la misma lógica de filter y la misma task. Las únicas diferencias son:

- El rid que usa el recurso IRQ (0 para legacy, 1+ para MSI/MSI-X).
- Si `pci_alloc_msi` o `pci_alloc_msix` tuvieron éxito.
- Cuántas funciones filter se registran (1 para legacy, 3 para MSI/MSI-X).
- En qué CPU se despachan las interrupciones reales.

Un driver bien escrito funciona de forma idéntica en los tres niveles. La escalera de alternativas del Capítulo 20 lo garantiza.



## Un laboratorio práctico: pruebas de regresión en los tres niveles

Un laboratorio que ejercita la escalera de alternativas para confirmar que los tres niveles funcionan.

### Configuración

Necesitas dos entornos de prueba:

- **Entorno A**: bhyve con virtio-rnd. El driver recurre al modo legacy INTx.
- **Entorno B**: QEMU con virtio-rng-pci. El driver usa MSI-X.

(Un tercer entorno con solo MSI y sin MSI-X es difícil de construir de forma fiable en plataformas modernas. La ruta MSI solo se ejercita si el lector dispone de un sistema en el que MSI-X falla pero MSI funciona.)

### Procedimiento

1. En el Entorno A, carga `myfirst.ko`. Verifica:

```sh
sysctl dev.myfirst.0.intr_mode   # returns 0
vmstat -i | grep myfirst          # one line
```

2. Ejercita el pipeline mediante los sysctls de interrupción simulada. Los tres deberían funcionar, aunque en modo legacy todos pasan por el mismo filter.

```sh
sudo sysctl dev.myfirst.0.intr_simulate_admin=2
sudo sysctl dev.myfirst.0.intr_simulate_rx=1
sudo sysctl dev.myfirst.0.intr_simulate_tx=4
sysctl dev.myfirst.0.intr_count   # should be 3
```

3. Descarga el módulo. Verifica que no haya fugas.

4. En el Entorno B, repite:

```sh
sysctl dev.myfirst.0.intr_mode   # returns 2
vmstat -i | grep myfirst          # three lines
for irq in <IRQs>; do cpuset -g -x $irq; done
```

5. Ejercita el pipeline por vector. Cada sysctl debería incrementar el contador de su propio vector.

```sh
sudo sysctl dev.myfirst.0.intr_simulate_admin=2
sysctl dev.myfirst.0.vec.admin.fire_count  # 1
sysctl dev.myfirst.0.vec.rx.fire_count     # 0
sysctl dev.myfirst.0.vec.tx.fire_count     # 0
```

6. Descarga el módulo. Verifica que no haya fugas.

### Observaciones esperadas

- Ambos entornos realizan el attach sin problemas.
- La línea de resumen de dmesg muestra el modo correcto para cada uno.
- Los contadores por vector se incrementan de forma independiente en MSI-X.
- En modo legacy, un único contador cubre todos los eventos.
- No hay fugas tras la descarga en ninguno de los dos entornos.

### Qué hacer si un nivel falla

Si el nivel MSI-X falla en el Entorno B:

1. Comprueba que QEMU sea lo suficientemente reciente. Las versiones anteriores a la 5.0 tienen problemas conocidos.
2. Comprueba `pciconf -lvc` en el huésped; la capacidad MSI-X debería ser visible.
3. Comprueba `dmesg` para ver si hay errores de `pci_alloc_msix`.

Si el nivel legacy falla en el Entorno A:

1. Comprueba `pciconf -lvc` para ver la configuración de la línea de interrupción del dispositivo.
2. Asegúrate de que `virtio_rnd` no esté ya en estado attach (advertencia del Capítulo 18).
3. Busca fallos de `pci_alloc_resource` en `dmesg`.



## Desafío extendido: construir un driver de calidad de producción

Un ejercicio opcional para lectores que quieran practicar el diseño multivector a una escala realista.

### El objetivo

Toma el driver del Capítulo 20 y extiéndelo para gestionar N queues de forma dinámica, donde N se descubre en el momento del attach en función del número de vectores MSI-X asignados. Cada queue tiene:

- Su propio vector (vector MSI-X 1+queue_id).
- Su propia función filter (o una compartida que identifica la queue a partir del argumento del vector).
- Sus propios contadores.
- Su propia task en su propio taskqueue.
- Su propio enlace a una CPU local en NUMA.

### Esquema de implementación

1. Sustituye `MYFIRST_MAX_VECTORS` por un recuento elegido en tiempo de ejecución.
2. Asigna el array `vectors[]` de forma dinámica (usando `malloc`).
3. Asigna un taskqueue independiente por vector.
4. Usa `bus_get_cpus(INTR_CPUS, ...)` para distribuir los vectores entre las CPUs locales de NUMA.
5. Añade sysctls que escalen con el número de vectores.

### Pruebas

Ejecuta el driver en un huésped con distintos números de vectores MSI-X. Para cada número, verifica:
- Los contadores de disparo se incrementan con las interrupciones simuladas.
- La afinidad de CPU respeta la localidad NUMA.
- El teardown es limpio.

### Qué ejercita esto

- Gestión dinámica de memoria en un driver.
- La API `bus_get_cpus`.
- Taskqueues por queue (desafío 3 de los anteriores).
- Construcción del árbol de sysctls en tiempo de ejecución (desafío 7 de los anteriores).

Este es un ejercicio significativo que probablemente llevará varias horas. El resultado es un driver reconociblemente similar a los drivers de producción para NIC y NVMe.



## Referencia: valores de prioridad para interrupciones y tareas

Como referencia rápida, las constantes de prioridad que podría usar un driver del Capítulo 20 (de `/usr/src/sys/sys/priority.h`):

```text
PI_REALTIME  = PRI_MIN_ITHD + 0   (highest; rarely used)
PI_INTR      = PRI_MIN_ITHD + 4   (common hardware interrupt level)
PI_AV        = PI_INTR            (audio/video)
PI_NET       = PI_INTR            (network)
PI_DISK      = PI_INTR            (block storage)
PI_TTY       = PI_INTR            (terminal/serial)
PI_DULL      = PI_INTR            (low-priority hardware)
PI_SOFT      = PRI_MIN_ITHD + 8   (soft interrupts)
```

Las prioridades de hardware habituales se mapean todas a `PI_INTR`; los nombres son distinciones de intención, no de prioridad de planificación. El driver del Capítulo 20 usa `PI_NET` para su taskqueue; cualquier prioridad a nivel de hardware funcionaría de forma equivalente.



## Referencia: comandos DTrace de una línea útiles para drivers MSI-X

Para los lectores que quieran observar el comportamiento del driver del Capítulo 20 de forma dinámica.

### Contar invocaciones de filter por CPU

```sh
sudo dtrace -n '
fbt::myfirst_admin_filter:entry, fbt::myfirst_rx_filter:entry,
fbt::myfirst_tx_filter:entry { @[probefunc, cpu] = count(); }'
```

Muestra qué filter se ejecuta en qué CPU.

### Tiempo empleado en cada filter

```sh
sudo dtrace -n '
fbt::myfirst_rx_filter:entry { self->ts = vtimestamp; }
fbt::myfirst_rx_filter:return /self->ts/ {
    @[probefunc] = quantize(vtimestamp - self->ts);
    self->ts = 0;
}'
```

Histograma del tiempo de CPU del filter RX.

### Tasa de interrupciones simuladas frente a reales

```sh
sudo dtrace -n '
fbt::myfirst_intr_simulate_vector_sysctl:entry { @sims = count(); }
fbt::myfirst_rx_filter:entry { @filters = count(); }'
```

Si `filters > sims`, algunas interrupciones reales están disparándose.

### Latencia de las tasks

```sh
sudo dtrace -n '
fbt::myfirst_rx_filter:entry { self->ts = vtimestamp; }
fbt::myfirst_rx_task_fn:entry /self->ts/ {
    @lat = quantize(vtimestamp - self->ts);
    self->ts = 0;
}'
```

Histograma del tiempo desde el filter hasta la invocación de la task. Muestra la latencia de planificación del taskqueue.



## Referencia: una nota de cierre antes del final de la Parte 4

Los Capítulos 16 al 20 construyeron la historia completa de interrupciones y hardware para el driver `myfirst`. Cada capítulo añadió una capa:

- Capítulo 16: acceso a registros.
- Capítulo 17: simulación del comportamiento del dispositivo.
- Capítulo 18: attach PCI.
- Capítulo 19: gestión de interrupciones con un solo vector.
- Capítulo 20: MSI/MSI-X multivector.

El Capítulo 21 añadirá DMA, completando la capa de hardware de la Parte 4. En ese momento, el driver `myfirst` será estructuralmente un driver real: un dispositivo PCI con interrupciones MSI-X y transferencia de datos basada en DMA. Lo que lo distingue de un driver de producción es el protocolo específico que implementa (ninguno, en realidad; es una demostración) y el dispositivo al que se dirige (una abstracción de virtio-rnd).

Un lector que haya interiorizado estos cinco capítulos puede abrir cualquier driver de FreeBSD en `/usr/src/sys/dev/` y reconocer los patrones. Ese reconocimiento es la recompensa más profunda de la Parte 4.



## Laboratorios prácticos

Los laboratorios son puntos de control graduados. Cada laboratorio se basa en el anterior y corresponde a una de las etapas del capítulo. Un lector que complete los cinco dispondrá de un driver multivector completo, un entorno de prueba QEMU funcional para MSI-X y un script de regresión que valida los tres niveles de la escalera de alternativas.

Los tiempos estimados asumen que el lector ya ha leído las secciones correspondientes.

### Laboratorio 1: descubrir las capacidades MSI y MSI-X

Tiempo: treinta minutos.

Objetivo: desarrollar la intuición sobre qué dispositivos del sistema admiten MSI y MSI-X.

Pasos:

1. Ejecuta `sudo pciconf -lvc > /tmp/pci_caps.txt`. El indicador `-c` incluye las listas de capacidades.
2. Busca capacidades MSI: `grep -B 1 "cap 05" /tmp/pci_caps.txt`.
3. Busca capacidades MSI-X: `grep -B 1 "cap 11" /tmp/pci_caps.txt`.
4. Para tres dispositivos que admitan MSI-X, anota:
   - El nombre del dispositivo (`pci0:B:D:F`).
   - El número de mensajes MSI-X admitidos.
   - Si el driver está usando MSI-X en este momento (comprueba `vmstat -i` para ver si hay varias líneas con el mismo nombre de dispositivo).
5. Compara el número total de dispositivos con capacidad MSI con el número total de dispositivos con capacidad MSI-X. Los sistemas modernos suelen tener más dispositivos MSI-X que dispositivos solo MSI.

Observaciones esperadas:

- Las NIC suelen anunciar MSI-X con muchos vectores (de 4 a 64).
- Los controladores SATA y NVMe anuncian MSI-X (NVMe a menudo con decenas de vectores).
- Algunos dispositivos legacy (un chip de audio, un controlador USB) anuncian solo MSI.
- Unos pocos dispositivos muy antiguos no anuncian ninguno y dependen del modo legacy INTx.

Este laboratorio trata sobre vocabulario. Sin código. La recompensa es que las llamadas de asignación de las Secciones 2 y 5 se vuelven concretas.

### Laboratorio 2: etapa 1, escalera de alternativas MSI

Tiempo: de dos a tres horas.

Objetivo: extender el driver del Capítulo 19 con la escalera de alternativas MSI-primero. Versión objetivo: `1.3-msi-stage1`.

Pasos:

1. Partiendo de la Etapa 4 del Capítulo 19, copia el código fuente del driver a un nuevo directorio de trabajo.
2. Añade el campo `intr_mode` y el enum a `myfirst.h`.
3. Modifica `myfirst_intr_setup` (en `myfirst_intr.c`) para intentar la asignación MSI primero, recurriendo al modo legacy INTx si falla.
4. Modifica `myfirst_intr_teardown` para llamar a `pci_release_msi` cuando se haya usado MSI.
5. Añade el sysctl `dev.myfirst.N.intr_mode`.
6. Actualiza la cadena de versión del `Makefile` a `1.3-msi-stage1`.
7. Compila (`make clean && make`).
8. Carga en un huésped. Anota el modo que informa el driver:

```sh
sudo kldload ./myfirst.ko
sudo dmesg | tail -5
sysctl dev.myfirst.0.intr_mode
```

En QEMU con virtio-rng-pci, el driver debería informar `MSI, 1 vector` (o similar). En bhyve con virtio-rnd, debería informar `legacy INTx`.

9. Descarga el módulo y verifica que no haya fugas.

Fallos habituales:

- Falta de `pci_release_msi`: la siguiente carga falla o recurre al modo legacy.
- rid incorrecto (usar 0 para MSI): `bus_alloc_resource_any` devuelve NULL.
- No comprobar el recuento devuelto: el driver continúa con menos vectores de los esperados.

### Laboratorio 3: etapa 2, asignación multivector (MSI)

Tiempo: de tres a cuatro horas.

Objetivo: extender a tres vectores MSI con manejadores por vector. Versión objetivo: `1.3-msi-stage2`.

Pasos:

1. Partiendo del Laboratorio 2, añade la estructura `myfirst_vector` y el array por vector a `myfirst.h`.
2. Escribe tres funciones filter: `myfirst_admin_filter`, `myfirst_rx_filter`, `myfirst_tx_filter`.
3. Escribe las funciones auxiliares `myfirst_intr_setup_vector` y `myfirst_intr_teardown_vector`.
4. Modifica `myfirst_intr_setup` para intentar `pci_alloc_msi` con `MYFIRST_MAX_VECTORS` vectores, configurando cada vector de forma independiente.
5. Modifica `myfirst_intr_teardown` para iterar por vector.
6. Añade sysctls de contador por vector (`vec0_fire_count`, `vec1_fire_count`, `vec2_fire_count`).
7. Añade sysctls de interrupción simulada por vector (`intr_simulate_admin`, `intr_simulate_rx`, `intr_simulate_tx`).
8. Actualiza la versión a `1.3-msi-stage2`.
9. Compila, carga y verifica:

```sh
sysctl dev.myfirst.0.intr_mode   # should be 1 on QEMU
vmstat -i | grep myfirst          # should show 3 lines
```

10. Ejercita cada vector:

```sh
sudo sysctl dev.myfirst.0.intr_simulate_admin=2  # ERROR
sudo sysctl dev.myfirst.0.intr_simulate_rx=1     # DATA_AV
sudo sysctl dev.myfirst.0.intr_simulate_tx=4     # COMPLETE
sysctl dev.myfirst.0 | grep vec
```

El contador de cada vector debería incrementarse de forma independiente.

11. Descarga el módulo, verifica que no haya fugas.

### Laboratorio 4: etapa 3, MSI-X con enlace a CPU

Tiempo: de tres a cuatro horas.

Objetivo: dar preferencia a MSI-X sobre MSI, enlazar cada vector a una CPU. Versión objetivo: `1.3-msi-stage3`.

Pasos:

1. Partiendo del Laboratorio 3, cambia la escalera de alternativas para intentar MSI-X primero (mediante `pci_msix_count` y `pci_alloc_msix`), MSI como segundo nivel y legacy como último.
2. Añade la función auxiliar `myfirst_msix_bind_vectors` que llama a `bus_bind_intr` para cada vector.
3. Llama a la función auxiliar de enlace después de registrar todos los vectores.
4. Actualiza la línea de resumen de dmesg para distinguir MSI-X de MSI.
5. Actualiza la versión a `1.3-msi-stage3`.
6. Compila, carga en QEMU con `virtio-rng-pci`. Verifica:

```sh
sysctl dev.myfirst.0.intr_mode   # should be 2 on QEMU
sudo dmesg | grep myfirst | grep MSI-X
```

La línea de attach debería mostrar `interrupt mode: MSI-X, 3 vectors`.

7. Comprueba los enlaces de CPU por vector:

```sh
# For each myfirst IRQ, show its CPU binding.
vmstat -i | grep myfirst
# (Note the IRQ numbers, then:)
for irq in <IRQ1> <IRQ2> <IRQ3>; do
    echo "IRQ $irq:"
    cpuset -g -x $irq
done
```

En un huésped con varias CPUs, cada vector debería estar enlazado a una CPU diferente.

8. Ejercita cada vector (igual que en el Laboratorio 3).

9. Realiza detach y attach de nuevo:

```sh
sudo devctl detach myfirst0
sudo devctl attach pci0:0:4:0
sysctl dev.myfirst.0.intr_mode  # should still be 2
```

10. Descarga el módulo, verifica que no haya fugas.

### Laboratorio 5: etapa 4, refactorización, regresión y versión

Tiempo: de tres a cuatro horas.

Objetivo: mover el código multivector a `myfirst_msix.c`, escribir `MSIX.md`, ejecutar la regresión. Versión objetivo: `1.3-msi`.

Pasos:

1. Partiendo del Laboratorio 4, crea `myfirst_msix.c` y `myfirst_msix.h`.
2. Mueve las funciones filter por vector, las funciones auxiliares, el setup, el teardown y el registro de sysctls a `myfirst_msix.c`.
3. Mantén la alternativa legacy INTx en `myfirst_intr.c` (el archivo del Capítulo 19).
4. En `myfirst_pci.c`, reemplaza las llamadas antiguas de setup/teardown de interrupciones con llamadas a `myfirst_msix.c`.
5. Actualiza el `Makefile` para añadir `myfirst_msix.c` a SRCS. Actualiza la versión a `1.3-msi`.
6. Escribe `MSIX.md` documentando el diseño multivector.
7. Compila, carga y ejecuta el script de regresión completo (desde los ejemplos complementarios).
8. Confirma que los tres niveles funcionan (probando en bhyve con virtio-rnd para el modo legacy y en QEMU con virtio-rng-pci para MSI-X).

Resultados esperados:

- El driver en versión `1.3-msi` funciona tanto en bhyve (alternativa legacy) como en QEMU (MSI-X).
- `myfirst_intr.c` ahora solo contiene la ruta de alternativa del Capítulo 19 con un único manejador.
- `myfirst_msix.c` contiene la lógica multivector del Capítulo 20.
- `MSIX.md` documenta el diseño con claridad.



## Ejercicios de desafío

Los desafíos se basan en los laboratorios y extienden el driver en direcciones que el capítulo no tomó.

### Desafío 1: adaptación dinámica del número de vectores

Modifica la configuración para adaptarse al número de vectores que el kernel asigne realmente. Si se solicitan 3 pero solo se asignan 2, el driver debe seguir funcionando con 2 (combinando los vectores admin y tx en un único vector compartido). Si se asigna 1, combínalos todos en uno solo.

Este ejercicio enseña la estrategia «adapt» de la escalera de fallback.

### Desafío 2: vinculación de CPU consciente de NUMA

Sustituye la vinculación de CPU en round-robin por una vinculación consciente de NUMA usando `bus_get_cpus(dev, INTR_CPUS, ...)`. Verifica con `cpuset -g -x <irq>` que los vectores se asignan a CPUs en el mismo dominio NUMA que el dispositivo.

En un sistema con un único socket, el ejercicio es académico; en un host de prueba con múltiples sockets, es medible.

### Desafío 3: taskqueues por vector

Actualmente todos los vectores comparten un único taskqueue. Modifica el driver para que cada vector tenga su propio taskqueue (con su propio thread trabajador). Mide el impacto en la latencia con DTrace.

Este ejercicio introduce trabajadores por vector y muestra cuándo ayudan y cuándo perjudican.

### Desafío 4: control de máscara MSI-X por vector

El registro de control de vector de la tabla MSI-X tiene un bit de máscara por vector. Añade un sysctl que permita al operador enmascarar un vector individual en tiempo de ejecución. Verifica que un vector enmascarado deja de recibir interrupciones.

Pista: el bit de máscara se programa mediante acceso directo a la tabla MSI-X, que es un tema más avanzado de lo que cubre el Capítulo 20. La implementación MSI-X de FreeBSD puede o no exponer esto directamente; el lector podría necesitar usar `bus_teardown_intr` y luego `bus_setup_intr` como una "soft mask" de nivel superior.

### Desafío 5: implementar moderación de interrupciones

Para un driver simulado, la moderación es fácil de prototipar: un sysctl que agrupe N interrupciones simuladas en una sola ejecución de tarea. Implementa la agrupación y mide el compromiso entre latencia y rendimiento.

### Desafío 6: reasignación de vectores en tiempo de ejecución

Añade un sysctl que permita al operador reasignar qué vector gestiona qué clase de evento (por ejemplo, intercambiar RX y TX). Demuestra que tras la reasignación, la interrupción simulada de RX activa el filtro de TX y viceversa.

### Desafío 7: árbol sysctl por cola

Reestructura los sysctls por vector en un árbol propio: `dev.myfirst.N.vec.admin.fire_count`, `dev.myfirst.N.vec.rx.fire_count`, etc. Usa `SYSCTL_ADD_NODE` para crear los nodos del árbol.

### Desafío 8: instrumentación con DTrace

Escribe un script de DTrace que muestre la distribución por CPU de las invocaciones al filtro de cada vector. Representa el desglose por CPU como un histograma. Este es el diagnóstico que confirma que la vinculación de CPU funciona correctamente.



## Solución de problemas y errores frecuentes

### "pci_alloc_msix devuelve EBUSY o ENXIO"

Posibles causas:

1. El dispositivo no está conectado de una manera que admita MSI-X (por ejemplo, el virtio-rnd legado en bhyve). Comprueba con `pciconf -lvc`.
2. Una carga anterior del driver no llamó a `pci_release_msi` durante el desmontaje. Reinicia o prueba de nuevo con `kldunload` + `kldload`.
3. El kernel se quedó sin vectores de interrupción. Es poco frecuente en x86 moderno, pero posible en plataformas con pocos vectores.

### "vmstat -i muestra solo una línea en el guest con MSI-X"

Causa probable: `pci_alloc_msix` tuvo éxito pero asignó solo 1 vector. Comprueba el recuento devuelto frente al solicitado. O bien acepta la situación (concentra el trabajo en uno) o libera y usa el fallback.

### "El filtro se activa pero vec->fire_count no cambia"

Causa probable: el argumento `sc` se confunde con `vec`. El manejador recibe `vec`, no `sc`. Comprueba el argumento de `bus_setup_intr`.

### "El driver entra en panic en kldunload tras varios ciclos de carga y descarga"

Causa probable: `pci_release_msi` no se llama durante el desmontaje. El estado MSI a nivel de dispositivo se filtra entre cargas; con el tiempo, la contabilidad interna del kernel se corrompe.

### "Todos los vectores diferentes se activan en la misma CPU"

Causa probable: `bus_bind_intr` falló silenciosamente. Comprueba el valor de retorno y registra los resultados distintos de cero.

### "La asignación MSI-X tiene éxito pero vmstat -i no muestra eventos"

Causa probable: la escritura de `INTR_MASK` del dispositivo apuntó al registro incorrecto o se omitió. Verifica que la máscara está establecida (diagnóstico del Capítulo 17/Capítulo 19).

### "Las interrupciones extraviadas se acumulan en el vector admin de MSI-X"

Causa probable: la comprobación de estado del filtro admin es incorrecta; el filtro devuelve `FILTER_STRAY` cuando debería gestionarlo. Comprueba la comprobación `status & MYFIRST_INTR_ERROR`.

### "El comportamiento de IRQ compartida en el fallback legado difiere del MSI-X"

Es lo esperado. En INTx legado, el único manejador ve todos los bits de evento; en MSI-X, cada vector ve solo su propio evento. Las pruebas que ejercitan los contadores de interrupciones extraviadas por vector difieren entre los dos modos.

### "La etapa 2 compila pero la etapa 3 falla con un error de enlace en `bus_get_cpus`"

Causa: `bus_get_cpus` puede no estar disponible en versiones anteriores de FreeBSD o puede requerir una ubicación específica de `#include <sys/bus.h>`. Comprueba el orden de las inclusiones.

### "El guest de QEMU no expone MSI-X a pesar de usar virtio-rng-pci"

Causa probable: las versiones más antiguas de QEMU usan virtio legado por defecto. Comprueba `pciconf -lvc` en el guest; si MSI-X no aparece en la lista, el guest está usando el modo legado. Actualiza QEMU o usa `-device virtio-rng-pci,disable-modern=off,disable-legacy=on`.



## En resumen

El Capítulo 20 dotó al driver de la capacidad de gestionar múltiples vectores de interrupción. El punto de partida fue `1.2-intr` con un único manejador en la línea INTx legada. El punto de llegada es `1.3-msi` con una escalera de fallback de tres niveles (MSI-X, MSI, legado), tres manejadores de filtro por vector, contadores y tareas por vector, vinculación de CPU por vector, un desmontaje limpio de múltiples vectores, y un nuevo archivo `myfirst_msix.c` junto con el documento `MSIX.md`.

Las ocho secciones recorrieron la progresión completa. La Sección 1 introdujo MSI y MSI-X a nivel de hardware. La Sección 2 añadió MSI como alternativa de vector único a INTx legado. La Sección 3 extendió el soporte a MSI de múltiples vectores. La Sección 4 examinó las implicaciones de concurrencia de múltiples filtros en múltiples CPUs. La Sección 5 pasó a MSI-X con vinculación de CPU por vector. La Sección 6 codificó los roles de eventos por vector. La Sección 7 consolidó el desmontaje. La Sección 8 refactorizó hasta la disposición final.

Lo que el Capítulo 20 no hizo es DMA. El manejador de cada vector todavía solo accede a registros; el dispositivo aún no tiene la capacidad de alcanzar la RAM. El Capítulo 21 es donde eso cambia. El DMA introduce nuevas complicaciones (coherencia, scatter-gather, mapeo) que interactúan con las interrupciones (las interrupciones de finalización señalan que una transferencia DMA ha concluido). La maquinaria de interrupciones del Capítulo 20 ya está preparada para gestionar interrupciones de finalización; el Capítulo 21 escribe la parte de DMA.

La disposición de archivos ha crecido: 14 archivos fuente (incluido `cbuf`), 6 archivos de documentación (`HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`), y una suite de pruebas de regresión en expansión. El driver es estructuralmente paralelo a los drivers de producción de FreeBSD en este punto.

### Una reflexión antes del Capítulo 21

El Capítulo 20 fue el último capítulo de la Parte 4 que trata exclusivamente de interrupciones. El Capítulo 21 pasa a DMA, que trata sobre el movimiento de datos. Ambos son complementarios: las interrupciones señalan eventos; el DMA mueve los datos relacionados con esos eventos. Un driver de alto rendimiento usa ambos conjuntamente: los descriptores de recepción se llenan mediante DMA desde el dispositivo hacia la RAM y, después, una interrupción de finalización indica al driver que procese los descriptores.

Los manejadores por vector del Capítulo 20 ya tienen la forma adecuada para esto. La interrupción de finalización de cada cola de recepción activa su propio vector; el filtro de cada vector reconoce y difiere; la tarea recorre el anillo de recepción (llenado por DMA, Capítulo 21) y propaga los paquetes hacia arriba. El Capítulo 21 escribe la parte de DMA; la parte de interrupciones del Capítulo 20 ya está en su lugar.

La enseñanza del Capítulo 20 también se generaliza. Un lector que haya interiorizado la escalera de fallback de tres niveles del Capítulo 20, el diseño de estado por vector, la vinculación de CPU y el desmontaje limpio encontrará patrones similares en todos los drivers de FreeBSD con múltiples colas. El dispositivo específico cambia; la estructura, no.

### Qué hacer si estás bloqueado

Tres sugerencias.

En primer lugar, lee `/usr/src/sys/dev/virtio/pci/virtio_pci.c` con detenimiento, centrándote en la familia de funciones `vtpci_alloc_intr_resources`. El patrón coincide exactamente con el del Capítulo 20 y el código es lo bastante compacto para leerlo de una sentada.

En segundo lugar, ejecuta las pruebas de regresión del capítulo tanto en un guest de bhyve (fallback legado) como en un guest de QEMU (MSI-X). Ver que el mismo driver se comporta correctamente en ambos entornos confirma que la escalera de fallback es correcta.

En tercer lugar, omite los desafíos en la primera lectura. Los laboratorios están calibrados para el ritmo del Capítulo 20; los desafíos asumen que el material está bien asentado. Vuelve a ellos después del Capítulo 21 si ahora te parecen fuera de alcance.

El objetivo del Capítulo 20 era dotar al driver de un camino de interrupciones con múltiples vectores. Si lo ha conseguido, el trabajo de DMA del Capítulo 21 se convierte en un complemento y no en un tema completamente nuevo.



## Puente hacia el Capítulo 21

El Capítulo 21 lleva por título *DMA and High-Speed Data Transfer*. Su alcance es el tema que el Capítulo 20 evitó deliberadamente: la capacidad del dispositivo para leer y escribir RAM directamente, sin que el driver intervenga en cada palabra. Una NIC con un anillo de descriptores de recepción de 64 entradas llena esas entradas por DMA desde el cable; una única interrupción señala "N entradas están listas". El manejador del driver recorre el anillo y procesa las entradas. Sin DMA, el driver tendría que leer cada byte de un registro del dispositivo, lo que no escala.

El Capítulo 20 preparó el terreno de tres maneras concretas.

En primer lugar, **tienes interrupciones de finalización por vector**. Las finalizaciones de recepción y de transmisión de cada cola pueden activar un vector dedicado. El trabajo con el anillo DMA del Capítulo 21 se conecta con el filtro y la tarea por vector del Capítulo 20; el filtro detecta "las finalizaciones N a M están listas" y la tarea las procesa.

En segundo lugar, **tienes ubicación del manejador por CPU**. La memoria de un anillo DMA se encuentra en un nodo NUMA específico; el manejador que la procesa debe ejecutarse en una CPU de ese nodo. El trabajo con `bus_bind_intr` del Capítulo 20 es el mecanismo. El Capítulo 21 lo amplía: la memoria DMA también se asigna con conciencia de NUMA, de modo que el anillo, el manejador y el procesamiento terminan en el mismo nodo.

En tercer lugar, **tienes la disciplina de desmontaje**. El DMA añade más recursos (etiquetas DMA, mapas DMA, regiones de memoria DMA), y cada uno necesita su propio paso de desmontaje. El patrón de desmontaje por vector de los Capítulos 19 y 20 se extiende de forma natural a la limpieza DMA por cola.

Temas concretos que cubrirá el Capítulo 21:

- Qué es DMA y la diferencia entre I/O mapeada en memoria y DMA.
- `bus_dma(9)`: etiquetas, mapas y la máquina de estados DMA.
- `bus_dma_tag_create` para describir los requisitos DMA (alineación, límites, rango de direcciones).
- `bus_dmamap_create` y `bus_dmamap_load` para configurar las transferencias DMA.
- Sincronización: `bus_dmamap_sync` alrededor de DMA.
- Bounce buffers: qué son y cuándo se usan.
- Coherencia de caché: por qué las CPUs y los dispositivos ven memoria diferente en distintos momentos.
- Listas scatter-gather: direcciones físicas que no son contiguas.
- Ring buffers: el patrón de anillo de descriptores productor-consumidor.

No necesitas leer por adelantado. El Capítulo 20 es preparación suficiente. Trae tu driver `myfirst` en `1.3-msi`, tu `LOCKING.md`, tu `INTERRUPTS.md`, tu `MSIX.md`, tu kernel con `WITNESS` activado y tu script de regresión. El Capítulo 21 comienza donde el Capítulo 20 terminó.

La conversación con el hardware se profundiza. El vocabulario es tuyo; la estructura es tuya; la disciplina es tuya. El Capítulo 21 añade la pieza que falta: la capacidad del dispositivo para mover datos sin pedir permiso.



## Referencia: tarjeta de referencia rápida del Capítulo 20

Un resumen compacto del vocabulario, las APIs, los macros y los procedimientos que el Capítulo 20 introdujo.

### Vocabulario

- **MSI (Message Signalled Interrupts)**: mecanismo PCI 2.2. De 1 a 32 vectores, contiguos, dirección única.
- **MSI-X**: mecanismo PCIe. Hasta 2048 vectores, dirección por vector, máscara por vector, tabla en un BAR.
- **vector**: una única fuente de interrupción identificada por un índice.
- **rid**: el identificador de recurso usado con `bus_alloc_resource_any`. 0 para INTx legado, 1 o más para MSI y MSI-X.
- **intr_mode**: el registro del driver sobre qué nivel está usando (legado, MSI o MSI-X).
- **fallback ladder**: probar primero MSI-X, luego MSI, luego INTx legado.
- **per-vector state**: contadores, filtro, tarea, cookie y recurso por vector.
- **CPU binding**: enrutar un vector a una CPU específica mediante `bus_bind_intr`.
- **LOCAL_CPUS / INTR_CPUS**: consultas de conjuntos de CPU para ubicación consciente de NUMA.

### APIs esenciales

- `pci_msi_count(dev)`: consulta el número de vectores MSI.
- `pci_msix_count(dev)`: consulta el número de vectores MSI-X.
- `pci_alloc_msi(dev, &count)`: asigna vectores MSI.
- `pci_alloc_msix(dev, &count)`: asigna vectores MSI-X.
- `pci_release_msi(dev)`: libera los vectores MSI o MSI-X.
- `pci_msix_table_bar(dev)`, `pci_msix_pba_bar(dev)`: identifica los BARs de tabla y PBA.
- `bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE)`: asigna el recurso IRQ correspondiente a cada vector.
- `bus_setup_intr(dev, res, flags, filter, ihand, arg, &cookie)`: registra el manejador asociado a cada vector.
- `bus_teardown_intr(dev, res, cookie)`: cancela el registro del manejador de cada vector.
- `bus_describe_intr(dev, res, cookie, "name")`: etiqueta el manejador de cada vector.
- `bus_bind_intr(dev, res, cpu)`: vincula un vector a una CPU concreta.
- `bus_get_cpus(dev, op, size, &set)`: consulta las CPUs locales NUMA (op = `LOCAL_CPUS` o `INTR_CPUS`).

### Macros esenciales

- `PCIY_MSI = 0x05`: ID de capacidad MSI.
- `PCIY_MSIX = 0x11`: ID de capacidad MSI-X.
- `PCIM_MSIXCTRL_TABLE_SIZE = 0x07FF`: máscara para el recuento de vectores.
- `PCI_MSIX_MSGNUM(ctrl)`: macro para extraer el recuento de vectores del registro de control.
- `MYFIRST_MAX_VECTORS`: constante definida por el driver (3 en el Capítulo 20).

### Procedimientos comunes

**Implementa la escalera de fallback de tres niveles:**

1. `pci_msix_count(dev)`; si > 0, intenta `pci_alloc_msix`.
2. En caso de fallo, `pci_msi_count(dev)`; si > 0, intenta `pci_alloc_msi`.
3. En caso de fallo, recurre a INTx legacy con `rid = 0` y `RF_SHAREABLE`.

**Registra los manejadores por vector (MSI-X):**

1. Itera desde `i = 0` hasta `num_vectors - 1`.
2. Para cada uno: `bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE)` con `rid = i + 1`.
3. `bus_setup_intr(dev, vec->irq_res, INTR_TYPE_MISC | INTR_MPSAFE, vec->filter, NULL, vec, &vec->intr_cookie)`.
4. `bus_describe_intr(dev, vec->irq_res, vec->intr_cookie, vec->name)`.
5. `bus_bind_intr(dev, vec->irq_res, target_cpu)`.

**Desmonta un driver multivector:**

1. Limpia `INTR_MASK` en el dispositivo.
2. Para cada vector (en orden inverso): `bus_teardown_intr`, `bus_release_resource`.
3. Vacía cada tarea por vector.
4. Libera el taskqueue.
5. `pci_release_msi(dev)` si se utilizó MSI o MSI-X.

### Comandos útiles

- `pciconf -lvc`: lista los dispositivos con sus listas de capacidades.
- `vmstat -i`: muestra el recuento de interrupciones por manejador.
- `cpuset -g -x <irq>`: consulta la afinidad de CPU para una IRQ.
- `cpuset -l <cpu> -x <irq>`: establece la afinidad de CPU para una IRQ.
- `sysctl dev.myfirst.0.intr_mode`: consulta el modo de interrupción del driver.

### Archivos que conviene tener marcados

- `/usr/src/sys/dev/pci/pcivar.h`: envoltorios inline de MSI/MSI-X.
- `/usr/src/sys/dev/pci/pcireg.h`: IDs de capacidad y campos de bits.
- `/usr/src/sys/dev/pci/pci.c`: implementación en el lado del kernel de `pci_alloc_msi`/`msix`.
- `/usr/src/sys/dev/virtio/pci/virtio_pci.c`: ejemplo limpio de escalera de fallback MSI-X.
- `/usr/src/sys/dev/nvme/nvme_ctrlr.c`: patrón MSI-X por cola a escala.



## Referencia: glosario de términos del Capítulo 20

**afinidad**: la asignación de un vector de interrupción a una CPU específica (o a un conjunto de CPUs).

**bus_bind_intr(9)**: función para enrutar un vector de interrupción a una CPU específica.

**bus_get_cpus(9)**: función para consultar los conjuntos de CPUs asociados a un dispositivo (local, aptas para interrupciones).

**lista de capacidades**: la lista enlazada de capacidades de un dispositivo PCI en el espacio de configuración.

**coalescencia**: acumulación de varios eventos en una única interrupción para reducir su frecuencia.

**cookie**: el manejador opaco devuelto por `bus_setup_intr(9)`, utilizado por `bus_teardown_intr(9)`.

**escalera de fallback**: la secuencia MSI-X → MSI → INTx legacy que implementan los drivers.

**intr_mode**: enumeración del lado del driver que registra qué nivel de interrupción está activo.

**INTR_CPUS**: valor del enum cpu_sets; CPUs aptas para gestionar las interrupciones del dispositivo.

**LOCAL_CPUS**: valor del enum cpu_sets; CPUs en el mismo dominio NUMA que el dispositivo.

**MSI**: interrupciones señalizadas por mensaje, PCI 2.2.

**MSI-X**: el mecanismo más completo, PCIe.

**moderación**: acumulación de interrupciones a nivel de dispositivo para intercambiar latencia por rendimiento.

**NUMA**: acceso a memoria no uniforme; arquitectura de sistema multisocket.

**estado por vector**: los campos del softc específicos de un vector (contadores, filter, tarea, cookie, recurso).

**pci_msi_count(9) / pci_msix_count(9)**: consultas del recuento de capacidades.

**pci_alloc_msi(9) / pci_alloc_msix(9)**: asignación de vectores.

**pci_release_msi(9)**: liberación de MSI/MSI-X (gestiona ambos).

**rid**: ID de recurso. 0 para INTx legacy, 1+ para vectores MSI/MSI-X.

**interrupción errante**: una interrupción que ningún filter reclama.

**taskqueue**: la primitiva de trabajo diferido de FreeBSD.

**vector**: una única fuente de interrupción en el mecanismo MSI o MSI-X.

**vmstat -i**: herramienta de diagnóstico que muestra el recuento de interrupciones por manejador.



## Referencia: recorrido completo del Stage 4 de myfirst_msix.c

Para los lectores que quieran consultar en un solo lugar la capa multivector final comentada, este apéndice recorre `myfirst_msix.c` de los ejemplos complementarios, mostrando cada función y explicando las decisiones de diseño.

### El encabezado del archivo

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/types.h>
#include <sys/smp.h>

#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_intr.h"
#include "myfirst_msix.h"
```

La lista de includes es más larga que la de `myfirst_intr.c`: `<dev/pci/pcireg.h>` y `<dev/pci/pcivar.h>` para la API MSI/MSI-X, `<sys/smp.h>` para `mp_ncpus` y `<machine/atomic.h>` para los incrementos del contador por vector. Ten en cuenta que `<dev/pci/pcireg.h>` se incluye aunque el archivo no utilice directamente `PCIY_MSI` ni constantes similares; las funciones inline de acceso en `pcivar.h` dependen de él.

### Las funciones auxiliares por vector

```c
static int
myfirst_msix_setup_vector(struct myfirst_softc *sc, int idx, int rid)
{
	struct myfirst_vector *vec = &sc->vectors[idx];
	int error;

	vec->irq_rid = rid;
	vec->irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &vec->irq_rid, RF_ACTIVE);
	if (vec->irq_res == NULL)
		return (ENXIO);

	error = bus_setup_intr(sc->dev, vec->irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    vec->filter, NULL, vec, &vec->intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    vec->irq_rid, vec->irq_res);
		vec->irq_res = NULL;
		return (error);
	}

	bus_describe_intr(sc->dev, vec->irq_res, vec->intr_cookie,
	    "%s", vec->name);
	return (0);
}

static void
myfirst_msix_teardown_vector(struct myfirst_softc *sc, int idx)
{
	struct myfirst_vector *vec = &sc->vectors[idx];

	if (vec->intr_cookie != NULL) {
		bus_teardown_intr(sc->dev, vec->irq_res, vec->intr_cookie);
		vec->intr_cookie = NULL;
	}
	if (vec->irq_res != NULL) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    vec->irq_rid, vec->irq_res);
		vec->irq_res = NULL;
	}
}
```

Estas funciones auxiliares forman el par simétrico de la Sección 3. Cada una recibe un índice de vector y opera sobre el `vec` en esa posición. La función auxiliar de configuración es idempotente en el sentido de que deja el vector en un estado limpio ante cualquier fallo; la función auxiliar de teardown es segura de invocar incluso si la configuración no se completó.

### Las funciones de filter por vector

Los tres filters solo difieren en el bit que comprueban. Su forma común es:

```c
int
myfirst_msix_rx_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & MYFIRST_INTR_DATA_AV) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_DATA_AV);
	atomic_add_64(&sc->intr_data_av_count, 1);
	if (sc->intr_tq != NULL)
		taskqueue_enqueue(sc->intr_tq, &vec->task);
	return (FILTER_HANDLED);
}
```

El filter de administración comprueba `MYFIRST_INTR_ERROR`, el filter de tx comprueba `MYFIRST_INTR_COMPLETE`. Cada uno incrementa el contador global correspondiente y el contador por vector. Solo el filter de rx encola una tarea.

### La tarea RX

```c
static void
myfirst_msix_rx_task_fn(void *arg, int npending)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->pci_attached) {
		sc->intr_last_data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
		sc->intr_task_invocations++;
		cv_broadcast(&sc->data_cv);
	}
	MYFIRST_UNLOCK(sc);
}
```

La tarea se ejecuta en contexto de thread y adquiere `sc->mtx` de forma segura. Comprueba `sc->pci_attached` antes de acceder al estado compartido, protegiéndose frente a la condición de carrera en la que la tarea se ejecuta durante el detach.

### La función principal de configuración

La función de configuración orquesta la escalera de fallback:

```c
int
myfirst_msix_setup(struct myfirst_softc *sc)
{
	int error, wanted, allocated, i;

	/* Initialise per-vector state common to all tiers. */
	for (i = 0; i < MYFIRST_MAX_VECTORS; i++) {
		sc->vectors[i].id = i;
		sc->vectors[i].sc = sc;
	}
	TASK_INIT(&sc->vectors[MYFIRST_VECTOR_RX].task, 0,
	    myfirst_msix_rx_task_fn,
	    &sc->vectors[MYFIRST_VECTOR_RX]);
	sc->vectors[MYFIRST_VECTOR_RX].has_task = true;
	sc->vectors[MYFIRST_VECTOR_ADMIN].filter = myfirst_msix_admin_filter;
	sc->vectors[MYFIRST_VECTOR_RX].filter = myfirst_msix_rx_filter;
	sc->vectors[MYFIRST_VECTOR_TX].filter = myfirst_msix_tx_filter;
	sc->vectors[MYFIRST_VECTOR_ADMIN].name = "admin";
	sc->vectors[MYFIRST_VECTOR_RX].name = "rx";
	sc->vectors[MYFIRST_VECTOR_TX].name = "tx";

	sc->intr_tq = taskqueue_create("myfirst_intr", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->intr_tq);
	taskqueue_start_threads(&sc->intr_tq, 1, PI_NET,
	    "myfirst intr taskq");

	wanted = MYFIRST_MAX_VECTORS;

	/* Tier 0: MSI-X. */
	if (pci_msix_count(sc->dev) >= wanted) {
		allocated = wanted;
		if (pci_alloc_msix(sc->dev, &allocated) == 0 &&
		    allocated == wanted) {
			for (i = 0; i < wanted; i++) {
				error = myfirst_msix_setup_vector(sc, i,
				    i + 1);
				if (error != 0) {
					for (i -= 1; i >= 0; i--)
						myfirst_msix_teardown_vector(
						    sc, i);
					pci_release_msi(sc->dev);
					goto try_msi;
				}
			}
			sc->intr_mode = MYFIRST_INTR_MSIX;
			sc->num_vectors = wanted;
			myfirst_msix_bind_vectors(sc);
			device_printf(sc->dev,
			    "interrupt mode: MSI-X, %d vectors\n", wanted);
			goto enabled;
		}
		if (allocated > 0)
			pci_release_msi(sc->dev);
	}

try_msi:
	/*
	 * Tier 1: MSI with a single vector. MSI requires a power-of-two
	 * count, so we cannot request MYFIRST_MAX_VECTORS (3) here. We
	 * request 1 vector and fall back to the Chapter 19 single-handler
	 * pattern, matching the approach sys/dev/virtio/pci/virtio_pci.c
	 * takes in vtpci_alloc_msi().
	 */
	allocated = 1;
	if (pci_msi_count(sc->dev) >= 1 &&
	    pci_alloc_msi(sc->dev, &allocated) == 0 && allocated >= 1) {
		sc->vectors[MYFIRST_VECTOR_ADMIN].filter = myfirst_intr_filter;
		sc->vectors[MYFIRST_VECTOR_ADMIN].name = "msi";
		error = myfirst_msix_setup_vector(sc, MYFIRST_VECTOR_ADMIN, 1);
		if (error == 0) {
			sc->intr_mode = MYFIRST_INTR_MSI;
			sc->num_vectors = 1;
			device_printf(sc->dev,
			    "interrupt mode: MSI, 1 vector "
			    "(single-handler fallback)\n");
			goto enabled;
		}
		pci_release_msi(sc->dev);
	}

try_legacy:
	/* Tier 2: legacy INTx. */
	sc->vectors[MYFIRST_VECTOR_ADMIN].filter = myfirst_intr_filter;
	sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid = 0;
	sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res = bus_alloc_resource_any(
	    sc->dev, SYS_RES_IRQ,
	    &sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res == NULL) {
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (ENXIO);
	}
	error = bus_setup_intr(sc->dev,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    myfirst_intr_filter, NULL, sc,
	    &sc->vectors[MYFIRST_VECTOR_ADMIN].intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid,
		    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res);
		sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res = NULL;
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (error);
	}
	bus_describe_intr(sc->dev,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].intr_cookie, "legacy");
	sc->intr_mode = MYFIRST_INTR_LEGACY;
	sc->num_vectors = 1;
	device_printf(sc->dev,
	    "interrupt mode: legacy INTx (1 handler for all events)\n");

enabled:
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
		    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR |
		    MYFIRST_INTR_COMPLETE);
	MYFIRST_UNLOCK(sc);

	return (0);
}
```

La función es extensa porque gestiona tres niveles, cada uno con su propia asignación, su bucle de configuración por vector y su limpieza ante fallos parciales. Un lector que siga el flujo verá que se intenta primero MSI-X, se pasa a MSI ante cualquier fallo y se recurre a legacy ante cualquier fallo en ese nivel. La etiqueta `enabled:` se alcanza desde cualquier nivel que tenga éxito.

El nivel legacy es el recorrido del Capítulo 19: un filter (`myfirst_intr_filter` de `myfirst_intr.c`), `rid = 0`, `RF_SHAREABLE`. Los contadores por vector no se usan realmente en este nivel; el código del Capítulo 19 realiza su propio recuento.

### La función de teardown

```c
void
myfirst_msix_teardown(struct myfirst_softc *sc)
{
	int i;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);

	for (i = sc->num_vectors - 1; i >= 0; i--)
		myfirst_msix_teardown_vector(sc, i);

	if (sc->intr_tq != NULL) {
		for (i = 0; i < sc->num_vectors; i++) {
			if (sc->vectors[i].has_task)
				taskqueue_drain(sc->intr_tq,
				    &sc->vectors[i].task);
		}
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
	}

	if (sc->intr_mode == MYFIRST_INTR_MSI ||
	    sc->intr_mode == MYFIRST_INTR_MSIX)
		pci_release_msi(sc->dev);

	sc->num_vectors = 0;
	sc->intr_mode = MYFIRST_INTR_LEGACY;
}
```

La función sigue el orden estricto: deshabilitar en el dispositivo, teardown por vector en orden inverso, vaciado de tareas por vector, liberar taskqueue, liberar MSI. Sin sorpresas; la simetría es la recompensa.

### La función de bind

```c
static void
myfirst_msix_bind_vectors(struct myfirst_softc *sc)
{
	int i, cpu;
	int err;

	if (mp_ncpus < 2)
		return;

	for (i = 0; i < sc->num_vectors; i++) {
		cpu = i % mp_ncpus;
		err = bus_bind_intr(sc->dev, sc->vectors[i].irq_res, cpu);
		if (err != 0)
			device_printf(sc->dev,
			    "bus_bind_intr vec %d: %d\n", i, err);
	}
}
```

Asignación round-robin. Solo se invoca con MSI-X (la función no es útil con MSI ni legacy; la escalera de configuración la omite en esos niveles). En sistemas con una sola CPU, la función retorna de forma anticipada sin realizar la asignación.

### La función sysctl

```c
void
myfirst_msix_add_sysctls(struct myfirst_softc *sc)
{
	struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
	struct sysctl_oid_list *kids = SYSCTL_CHILDREN(sc->sysctl_tree);
	char name[32];
	int i;

	SYSCTL_ADD_INT(ctx, kids, OID_AUTO, "intr_mode",
	    CTLFLAG_RD, &sc->intr_mode, 0,
	    "0=legacy, 1=MSI, 2=MSI-X");

	for (i = 0; i < MYFIRST_MAX_VECTORS; i++) {
		snprintf(name, sizeof(name), "vec%d_fire_count", i);
		SYSCTL_ADD_U64(ctx, kids, OID_AUTO, name,
		    CTLFLAG_RD, &sc->vectors[i].fire_count, 0,
		    "Times this vector's filter was called");
		snprintf(name, sizeof(name), "vec%d_stray_count", i);
		SYSCTL_ADD_U64(ctx, kids, OID_AUTO, name,
		    CTLFLAG_RD, &sc->vectors[i].stray_count, 0,
		    "Stray returns from this vector");
	}

	SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_admin",
	    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE,
	    &sc->vectors[MYFIRST_VECTOR_ADMIN], 0,
	    myfirst_intr_simulate_vector_sysctl, "IU",
	    "Simulate admin vector interrupt");
	SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_rx",
	    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE,
	    &sc->vectors[MYFIRST_VECTOR_RX], 0,
	    myfirst_intr_simulate_vector_sysctl, "IU",
	    "Simulate rx vector interrupt");
	SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_tx",
	    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE,
	    &sc->vectors[MYFIRST_VECTOR_TX], 0,
	    myfirst_intr_simulate_vector_sysctl, "IU",
	    "Simulate tx vector interrupt");
}
```

La función construye tres sysctls de solo lectura para los contadores por vector y tres sysctls de solo escritura para interrupciones simuladas. El estilo de árbol (Desafío 7) se deja como ejercicio.

### Líneas de código

El archivo completo `myfirst_msix.c` tiene unas 330 líneas. Es una adición sustancial al driver, pero aporta todas las capacidades del Capítulo 20: fallback de tres niveles, manejadores por vector, contadores por vector, asignación de CPU y teardown limpio.

Compáralo con el `myfirst_intr.c` del Capítulo 19, que tenía unas 250 líneas. El archivo del Capítulo 20 no es mucho más largo en términos absolutos; la lógica por vector añade complejidad, pero cada pieza es pequeña.



## Referencia: nota final sobre la filosofía multivector

Un párrafo para cerrar el capítulo.

Un driver multivector no difiere fundamentalmente de un driver de vector único. Tiene la misma estructura de filter, el mismo patrón de tarea, el mismo orden de teardown y la misma disciplina de lock. Lo que cambia es la cantidad: N filters en lugar de uno, N teardowns en lugar de uno, N tareas en lugar de una. La calidad del diseño reside en la limpieza con que esas N piezas coexisten.

La lección del Capítulo 20 es que el manejo multivector es un ejercicio de simetría. Cada vector es estructuralmente idéntico a los demás; cada uno tiene su propio contador, su propio filter y su propia descripción. El código que asigna, el código que maneja, el código que desmonta: todos iteran sobre los vectores y hacen lo mismo N veces. La sencillez del bucle es lo que hace que el driver de N vectores sea manejable; un driver en el que cada vector es especial es un driver que no escala.

Para el lector actual y para los futuros lectores de este libro, el patrón multivector del Capítulo 20 es una parte permanente de la arquitectura del driver `myfirst` y una herramienta permanente en su repertorio. El Capítulo 21 lo da por sentado: anillos DMA por cola, interrupciones de finalización por cola y asignación de CPU por cola. El vocabulario es el que comparten todos los drivers de alto rendimiento de FreeBSD; los patrones son los que utilizan los propios drivers de prueba del kernel; la disciplina es la que rige los drivers de producción.

La habilidad que enseña el Capítulo 20 no es «cómo asignar MSI-X para virtio-rng-pci». Es «cómo diseñar un driver multivector, asignar sus vectores, ubicarlos en CPUs, enrutar eventos por vector y desmontarlo todo de forma limpia». Esa habilidad es aplicable a cualquier dispositivo de múltiples colas con el que el lector trabaje en el futuro.
