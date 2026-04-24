---
title: "Gestión de energía"
description: "El capítulo 22 cierra la Parte 4 enseñando cómo el driver myfirst sobrevive a las operaciones de suspend, resume y shutdown. Explica qué es la gestión de energía desde la perspectiva de un driver de dispositivo; cómo los estados de suspensión ACPI (S0-S5) y los estados de energía de dispositivo PCI (D0-D3hot/D3cold) se combinan para formar una transición completa; qué hacen los métodos DEVICE_SUSPEND, DEVICE_RESUME, DEVICE_SHUTDOWN y DEVICE_QUIESCE, y en qué orden los entrega el kernel; cómo detener de forma segura las interrupciones, el DMA, los temporizadores y el trabajo diferido; cómo restaurar el estado al reanudar sin perder datos; en qué se diferencia la gestión de energía en tiempo de ejecución de la suspensión completa del sistema; cómo probar las transiciones de energía desde el espacio de usuario con acpiconf, zzz y devctl; cómo depurar dispositivos bloqueados, interrupciones perdidas y fallos de DMA tras la reanudación; y cómo refactorizar el código de gestión de energía en su propio archivo. El driver evoluciona de 1.4-dma a 1.5-power, incorpora myfirst_power.c y myfirst_power.h, añade un documento POWER.md y cierra la Parte 4 con un driver que gestiona los ciclos de suspend-resume con la misma limpieza con que gestiona el attach-detach."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 22
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 210
language: "es-ES"
---
# Gestión de energía

## Orientación para el lector y resultados

El capítulo 21 terminó con el driver en la versión `1.4-dma`. Ese driver se conecta a un dispositivo PCI, asigna vectores MSI-X, atiende interrupciones a través de un pipeline de filtro más tarea, mueve datos a través de un buffer `bus_dma(9)` y libera sus recursos cuando se le pide que se desconecte. Para un driver que arranca, funciona y eventualmente se descarga, esos mecanismos están completos. Lo que el driver todavía no gestiona es el tercer tipo de evento que un sistema moderno le lanza: el momento en que la propia energía está a punto de cambiar.

Los cambios de energía son distintos del attach y del detach. Un attach parte de cero y termina con un dispositivo funcionando. Un detach parte de un dispositivo funcionando y termina con nada. Ambas son transiciones únicas sobre las que el driver puede tomarse su tiempo. Un suspend no es ninguna de las dos. El driver entra en suspend ya en marcha, con interrupciones activas, transferencias DMA en curso, temporizadores activos y un dispositivo al que el kernel aún espera que responda. El driver tiene que detener todo eso dentro de una ventana de tiempo estrecha, entregar el dispositivo a un estado de menor consumo, sobrevivir a la pérdida de energía sin olvidar lo que necesita saber y, a continuación, reensamblar todo al otro lado como si nada hubiera ocurrido. El usuario, idealmente, no percibe nada en absoluto. La tapa se cierra, un segundo después se abre y la videoconferencia continúa en la misma pestaña del navegador como si la interrupción nunca hubiera ocurrido.

El capítulo 22 enseña cómo el driver consigue esa ilusión. El alcance del capítulo es exactamente este: qué es la gestión de energía a nivel de driver; cómo el kernel permite al driver anticipar una transición de energía; qué significa poner en quiesce un dispositivo para que ninguna actividad se filtre a través de la transición; cómo conservar el estado que el driver necesitará después del resume; cómo restaurar ese estado para que el dispositivo vuelva al mismo comportamiento que el usuario veía antes; cómo extender esa misma disciplina al ahorro de energía en dispositivos ociosos mediante el suspend en tiempo de ejecución; cómo probar las transiciones desde el espacio de usuario; cómo depurar los fallos característicos a los que se enfrenta un driver con soporte de energía; y cómo organizar el nuevo código para que el driver siga siendo legible a medida que crece. El capítulo no llega a los temas posteriores que se apoyan en esta disciplina. El capítulo 23 enseña la depuración y el rastreo en profundidad; el script de regresión del capítulo 22 es un primer aperitivo, no el conjunto completo de herramientas. El capítulo de drivers de red en la Parte 6 (capítulo 28) añade los hooks de energía de iflib y la coordinación del suspend con múltiples colas; el capítulo 22 se centra en el driver `myfirst` de cola única. Los capítulos avanzados del mundo real de la Parte 7 exploran el hotplug y la gestión de dominios de energía en plataformas embebidas; el capítulo 22 se centra en los casos de escritorio y servidor donde dominan ACPI y PCIe.

El arco de la Parte 4 se cierra aquí con una disciplina, no con un nuevo primitivo. El capítulo 16 dio al driver un vocabulario de acceso a registros a través de `bus_space(9)`. El capítulo 17 le enseñó a pensar como un dispositivo simulando uno. El capítulo 18 lo introdujo a un dispositivo PCI real. El capítulo 19 le dio un único par de oídos sobre un solo IRQ. El capítulo 20 le dio varios oídos, uno por cada cola que le importa al dispositivo. El capítulo 21 le dio manos: la capacidad de entregarle al dispositivo una dirección física y dejarle ejecutar una transferencia por sí solo. El capítulo 22 enseña al driver a dejar de hacer todo eso cuando se le pide, a esperar mientras el sistema duerme y a retomar la actividad limpiamente cuando el sistema despierta. Esa disciplina es el último ingrediente que falta para que el driver pueda considerarse listo para producción en el sentido de la Parte 4. Los capítulos posteriores añaden observabilidad, especialización y refinamiento; dan por sentado que la disciplina de energía ya está en su lugar.

### Por qué el suspend y el resume merecen un capítulo propio

Una pregunta natural en este punto es si el suspend y el resume realmente necesitan un capítulo completo, después de la profundidad del capítulo 21. El driver `myfirst` ya tiene una ruta de detach limpia. El detach ya libera interrupciones, vacía las tareas, desmonta el DMA y devuelve el dispositivo a un estado tranquilo. ¿No puede el driver simplemente llamar a detach en el suspend y a attach en el resume, y ya está?

La respuesta es no, por tres razones interrelacionadas.

La primera es que **el suspend no es un detach**. Un detach es permanente. El driver no necesita recordar nada sobre el dispositivo después de que el detach termine; cuando el dispositivo vuelva, será un attach nuevo, desde cero. Un suspend es temporal, y el driver sí necesita recordar cosas a través de él. Necesita recordar su estado software para que la sesión del usuario pueda continuar donde la dejó. Necesita recordar qué vectores de interrupción había asignado. Necesita recordar sus sysctl de configuración. Necesita recordar qué clientes tenían el dispositivo abierto. El detach olvida todo eso; el suspend no debe hacerlo. Las dos rutas comparten pasos de limpieza en su parte intermedia, pero divergen en los extremos. Tratar el suspend como un detach más un attach posterior sería correcto en el sentido mecánico estricto y erróneo en todos los demás: perdería la sesión del usuario, invalidaría los descriptores de archivo abiertos en `/dev/myfirst0`, perdería el estado de los sysctl y pediría al kernel que volviera a hacer el probe del dispositivo desde su identidad PCI en bruto en cada resume. Así no es como funcionan los drivers modernos de FreeBSD, y el capítulo 22 muestra el patrón correcto.

La segunda es que **el presupuesto de tiempo es diferente**. Un detach puede permitirse ser exhaustivo. Un driver que tarda quinientos milisegundos en hacer el detach no tiene ningún impacto visible para el usuario; los detaches ocurren durante el boot, al descargar un módulo o al retirar un dispositivo, y se entiende que esos momentos son lentos. Un suspend tiene que terminar dentro de un presupuesto medido en decenas de milisegundos por dispositivo en un portátil con un centenar de dispositivos, porque la suma es lo que el usuario percibe como latencia al cerrar la tapa. Un driver que realiza una limpieza completa al estilo del detach, espera a que las colas se vacíen a su ritmo natural, deshace cada asignación y reconstruye todo en el resume será perceptiblemente lento en el conjunto de dispositivos del sistema. El patrón del capítulo 22 es detener la actividad rápidamente, guardar lo que hay que guardar, dejar las asignaciones en su sitio y restaurar desde el estado guardado. Ese patrón es lo que mantiene el suspend-resume por debajo de un segundo en un portátil típico.

La tercera es que **el kernel proporciona al driver un contrato específico para las transiciones de energía**, y ese contrato tiene su propio vocabulario, su propio orden de operaciones y sus propios modos de fallo. Los métodos kobj `DEVICE_SUSPEND` y `DEVICE_RESUME` no son simplemente «detach y attach con nombres diferentes». Se invocan en puntos específicos de la secuencia de suspend del sistema, con el árbol de drivers recorrido en un orden determinado, e interactúan con el guardado y restaurado automático del espacio de configuración de la capa PCI, con la maquinaria de estados de reposo de ACPI, con las llamadas de enmascaramiento y desenmascaramiento del subsistema de interrupciones, y con los helpers `bus_generic_suspend` y `bus_generic_resume` que recorren el árbol de dispositivos. Un driver que ignora el contrato puede seguir pareciendo correcto durante el detach, el DMA y la gestión de interrupciones, y fallar únicamente cuando el usuario cierra la tapa. Esa clase de fallos es notoriamente difícil de depurar porque es difícil de reproducir, y el capítulo 22 invierte tiempo en hacer explícito el contrato para que los fallos no ocurran en primer lugar.

El capítulo 22 se gana su lugar enseñando esas tres ideas juntas, de forma concreta, con el driver `myfirst` como ejemplo recurrente. Un lector que termine el capítulo 22 podrá añadir los métodos `device_suspend`, `device_resume` y `device_shutdown` a cualquier driver de FreeBSD, sabrá qué parte de la disciplina del capítulo se aplica en cada caso y comprenderá las interacciones entre la capa ACPI, la capa PCI y el estado propio del driver. Esa habilidad se transfiere directamente a cualquier driver de FreeBSD en el que el lector trabaje en el futuro.

### Dónde dejó el capítulo 21 al driver

Un breve punto de control antes de continuar. El capítulo 22 extiende el driver producido al final de la Etapa 4 del capítulo 21, etiquetado como versión `1.4-dma`. Si alguno de los puntos siguientes resulta incierto, vuelve al capítulo 21 antes de empezar este.

- Tu driver compila sin errores y se identifica como `1.4-dma` en `kldstat -v`.
- El driver asigna uno o tres vectores MSI-X (según la plataforma), registra filtros y tareas por vector, vincula cada vector a una CPU e imprime un banner de interrupción durante el attach.
- El driver asigna un tag `bus_dma`, asigna un buffer DMA de 4 KB, lo carga en un mapa y expone la dirección del bus a través de `dev.myfirst.N.dma_bus_addr`.
- Escribir en `dev.myfirst.N.dma_test_write=0xAA` provoca una transferencia del host al dispositivo; escribir en `dev.myfirst.N.dma_test_read=1` provoca una transferencia del dispositivo al host; ambas registran el éxito en `dmesg`.
- La ruta de detach vacía la tarea rx, vacía el callout de la simulación, espera a que cualquier DMA en vuelo se complete, llama a `myfirst_dma_teardown`, desmonta los vectores MSI-X en orden inverso y libera recursos.
- `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md` y `DMA.md` están actualizados en tu árbol de trabajo.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` están habilitados en tu kernel de prueba.

Ese driver es lo que el capítulo 22 extiende. Las adiciones son modestas en líneas pero importantes en disciplina: un nuevo archivo `myfirst_power.c`, una cabecera `myfirst_power.h` correspondiente, un pequeño conjunto de nuevos campos en el softc para rastrear el estado de suspend y el estado de ejecución guardado, nuevos puntos de entrada `myfirst_suspend` y `myfirst_resume` conectados a la tabla `device_method_t`, un nuevo método `myfirst_shutdown`, una llamada a las primitivas quiesce del capítulo 21 desde la nueva ruta de suspend, una ruta de restauración que reinicializa el dispositivo sin repetir el attach, un salto de versión a `1.5-power`, un nuevo documento `POWER.md` y actualizaciones al test de regresión. El modelo mental también crece: el driver empieza a pensar en su propio ciclo de vida como attach, ejecutar, quiesce, dormir, despertar, ejecutar de nuevo y, finalmente, detach, en lugar de simplemente attach, ejecutar, detach.

### Lo que aprenderás

Al terminar este capítulo, deberías ser capaz de:

- Describir qué significa la gestión de energía para un driver de dispositivo, distinguir el ahorro de energía a nivel de sistema del ahorro a nivel de dispositivo, y señalar la diferencia entre un ciclo completo de suspend-resume y una transición de energía en tiempo de ejecución.
- Reconocer los estados de suspensión de sistema de ACPI (S0, S1, S3, S4, S5) y los estados de energía de dispositivo PCI (D0, D1, D2, D3hot, D3cold), explicar cómo se combinan en una sola transición, e identificar qué partes de cada uno son responsabilidad del driver.
- Explicar el papel de los estados de enlace PCIe (L0, L0s, L1, L1.1, L1.2) y de la gestión de energía en estado activo (ASPM), con el nivel de detalle suficiente para leer un datasheet y reconocer qué controla la plataforma de forma automática frente a qué debe configurar el driver de forma explícita.
- Añadir entradas `DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN` y (opcionalmente) `DEVICE_QUIESCE` a la tabla `device_method_t` de un driver, e implementar cada una de ellas de modo que se compongan con `bus_generic_suspend(9)` y `bus_generic_resume(9)` en el árbol de dispositivos.
- Explicar qué significa realizar el quiesce de un dispositivo de forma segura antes de una transición de energía, y aplicar el patrón: enmascarar las interrupciones en el dispositivo, dejar de enviar nuevo trabajo DMA, vaciar las transferencias en curso, vaciar los callouts y taskqueues, vaciar o descartar buffers según dicte la política, y dejar el dispositivo en un estado definido y silencioso.
- Explicar por qué la capa PCI guarda y restaura automáticamente el espacio de configuración en torno a `device_suspend` y `device_resume`, cuándo el driver tiene que complementar eso con sus propias llamadas a `pci_save_state`/`pci_restore_state`, y cuándo no debería hacerlo.
- Implementar un camino de resume limpio que reactive el bus mastering, restaure los registros del dispositivo desde el estado guardado del driver, reactive la máscara de interrupciones, revalide la identidad del dispositivo y vuelva a admitir clientes al dispositivo sin perder datos ni generar interrupciones espurias.
- Reconocer los casos en que un dispositivo se reinicia silenciosamente durante el suspend, cómo detectar ese reinicio, y cómo reconstruir únicamente el estado que se perdió realmente.
- Implementar un helper de gestión de energía en tiempo de ejecución que ponga un dispositivo inactivo en D3 y lo despierte de vuelta a D0 bajo demanda, y analizar el balance entre latencia y consumo energético.
- Activar un suspend de todo el sistema desde el espacio de usuario con `acpiconf -s 3` o `zzz`, un suspend por dispositivo con `devctl suspend` y `devctl resume`, y observar las transiciones mediante `devinfo -v`, `sysctl hw.acpi.*` y los propios contadores del driver.
- Depurar los fallos característicos del código consciente de la energía: dispositivos bloqueados, interrupciones perdidas, DMA incorrecto tras el resume, eventos de activación PME# perdidos, y quejas de WITNESS sobre dormir con locks retenidos dentro del suspend. Aplicar los patrones de recuperación correspondientes.
- Refactorizar el código de gestión de energía del driver en un par de archivos dedicados `myfirst_power.c`/`myfirst_power.h`, incrementar la versión del driver a `1.5-power`, ampliar el test de regresión para cubrir suspend y resume, y producir un documento `POWER.md` que explique el subsistema al próximo lector.
- Leer el código de gestión de energía de un driver real como `/usr/src/sys/dev/re/if_re.c`, `/usr/src/sys/dev/xl/if_xl.c` o `/usr/src/sys/dev/virtio/block/virtio_blk.c`, y relacionar cada llamada con los conceptos introducidos en el Capítulo 22.

La lista es larga. Los elementos son concretos y acotados. El objetivo del capítulo es la composición, no ningún punto por separado.

### Lo que este capítulo no cubre

Varios temas adyacentes se posponen explícitamente para que el Capítulo 22 permanezca centrado en la disciplina del lado del driver.

- **Los aspectos internos avanzados de ACPI**, como el intérprete AML, las tablas SSDT/DSDT, la semántica de los métodos `_PSW`/`_PRW`/`_PSR` y el subsistema de botones de ACPI. El capítulo usa ACPI únicamente a través de la capa que el kernel expone al driver; los aspectos internos pertenecen a un capítulo posterior orientado a plataformas.
- **La mecánica de hibernación en disco (S4)**. La compatibilidad de FreeBSD con S4 ha sido históricamente parcial en x86, y el contrato del lado del driver es esencialmente una versión más estricta de S3. El capítulo menciona S4 por completitud y lo trata como S3 a efectos del driver.
- **Cpufreq, powerd y el escalado de frecuencia de CPU**. Estos afectan a la energía del CPU, no a la del dispositivo. Un driver cuyo dispositivo está en D0 no se ve afectado por el P-state del CPU; el capítulo no profundiza en la gestión de energía del CPU.
- **La coordinación de suspend en SR-IOV entre PF y VFs**. El suspend de las Virtual Functions tiene sus propias restricciones de ordenación y pertenece a un capítulo especializado.
- **El hotplug y la extracción inesperada**. Retirar un dispositivo desenchufándolo físicamente es similar en espíritu al suspend, pero usa rutas de código diferentes (`BUS_CHILD_DELETED`, `device_delete_child`). La Parte 7 cubre el hotplug en profundidad; el Capítulo 22 menciona la relación y sigue adelante.
- **El suspend de docks Thunderbolt y USB-C**. Estos combinan ACPI, hotplug de PCIe y gestión de energía USB, y pertenecen a una sección posterior dedicada.
- **Los frameworks de dominios de energía y clock-gating en plataformas embebidas**, como las propiedades `power-domains` y `clocks` del device tree en arm64 y RISC-V. El capítulo usa en todo momento las convenciones de x86 con ACPI y PCI, y menciona los equivalentes embebidos de pasada cuando el concepto es paralelo.
- **La política personalizada de wake-on-LAN, wake-on-pattern y las fuentes de wake específicas de cada aplicación**. El capítulo explica cómo se conecta una fuente de wake (PME#, wakeup remoto USB, wake por GPIO) sin intentar enseñar cada variación específica del hardware.
- **Los aspectos internos de las rutas `ksuspend`/`kresume` y la migración de cpusets del kernel en torno al suspend**. El driver no los ve directamente; afectan al enrutamiento de interrupciones y a la desconexión de CPUs, no al contrato visible del driver.

Mantenerse dentro de esos límites convierte el Capítulo 22 en un capítulo sobre la disciplina de energía del lado del driver. El vocabulario se transfiere; las especializaciones añaden detalle en capítulos posteriores sin necesitar una nueva base.

### Estimación del tiempo necesario

- **Solo lectura**: cuatro o cinco horas. El modelo conceptual de gestión de energía no es tan denso como el de DMA ni tan mecánico como el de las interrupciones; gran parte del tiempo se dedica a construir la imagen mental de cómo ACPI, PCI y el driver se componen durante una transición.
- **Lectura y escritura de los ejemplos trabajados**: diez a doce horas en dos o tres sesiones. El driver evoluciona en tres etapas: un esqueleto de suspend y resume con registro, quiesce y restauración completos, y finalmente la refactorización a `myfirst_power.c`. Cada etapa es corta, pero las pruebas son deliberadas: un `bus_dmamap_sync` olvidado o una máscara de interrupción omitida pueden producir corrupción silenciosa que solo se manifiesta en el quinto o sexto ciclo de suspend-resume.
- **Lectura con todos los laboratorios y desafíos**: quince a veinte horas en cuatro o cinco sesiones, incluyendo el laboratorio que somete el driver a ciclos repetidos de suspend-resume, el laboratorio que provoca un fallo intencional tras el resume y lo depura, y el material de desafío que extiende el driver con detección de inactividad en tiempo de ejecución.

Las secciones 3 y 4 son las más densas. Si la disciplina de quiesce o el orden del resume resultan opacos en la primera lectura, es normal. Para, vuelve a leer el diagrama correspondiente, ejecuta el ejercicio equivalente en el dispositivo simulado y continúa cuando la estructura haya quedado clara. La gestión de energía es uno de esos temas en los que un modelo mental funcional da sus frutos repetidamente; vale la pena construirlo con calma.

### Requisitos previos

Antes de comenzar este capítulo, comprueba lo siguiente:

- El código fuente de tu driver coincide con el Capítulo 21, Etapa 4 (`1.4-dma`). El punto de partida asume todas las primitivas del Capítulo 21: la etiqueta DMA y el buffer, el rastreador `dma_in_flight`, la variable de condición `dma_cv` y la ruta de desmontaje limpia.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con `/usr/src` en disco y coincidiendo con el kernel en ejecución.
- Un kernel de depuración con `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` está compilado, instalado y arrancando limpiamente. La opción `WITNESS` es especialmente valiosa para el trabajo de suspend y resume, porque las rutas de código se ejecutan bajo locks no evidentes y la maquinaria de energía del kernel refuerza varios invariantes durante la transición.
- `bhyve(8)` o `qemu-system-x86_64` está disponible. Los laboratorios del Capítulo 22 funcionan en cualquiera de los dos entornos. Las pruebas de suspend-resume no requieren hardware real; `devctl suspend` y `devctl resume` te permiten invocar los métodos de energía del driver directamente sin involucrar a ACPI.
- Los comandos `devinfo(8)`, `sysctl(8)`, `pciconf(8)`, `procstat(1)`, `devctl(8)`, `acpiconf(8)` (si estás en hardware real con ACPI) y `zzz(8)` están en tu PATH.

Si algo de lo anterior no está en orden, corrígelo ahora. La gestión de energía, al igual que DMA, es un tema en el que las debilidades latentes se manifiestan bajo presión. Un driver que casi funciona en el detach suele fallar en el suspend; un driver que gestiona un suspend limpiamente a menudo falla en el décimo ciclo porque un contador se desbordó, un mapa tuvo una fuga o una variable de condición se reinicializó incorrectamente. El kernel de depuración con `WITNESS` activado es lo que hace aflorar esos errores en el momento del desarrollo.

### Cómo sacar el máximo partido a este capítulo

Cuatro hábitos darán sus frutos con rapidez.

Primero, ten `/usr/src/sys/kern/device_if.m` y `/usr/src/sys/kern/subr_bus.c` marcados como referencia. El primer archivo define los métodos `DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN` y `DEVICE_QUIESCE`; el segundo contiene `bus_generic_suspend`, `bus_generic_resume`, `device_quiesce` y la maquinaria de devctl que convierte las peticiones del espacio de usuario en llamadas a métodos. Leer ambos una vez al inicio de la Sección 2 y volver a ellos mientras trabajas cada sección es lo más útil que puedes hacer para ganar fluidez.

Segundo, ten a mano tres ejemplos de drivers reales: `/usr/src/sys/dev/re/if_re.c`, `/usr/src/sys/dev/xl/if_xl.c` y `/usr/src/sys/dev/virtio/block/virtio_blk.c`. Cada uno ilustra un estilo diferente de gestión de energía. `if_re.c` es un driver de red completo con compatibilidad con wake-on-LAN, guardado y restauración del espacio de configuración y una ruta de resume cuidadosa. `if_xl.c` es más sencillo: su `xl_shutdown` simplemente llama a `xl_suspend`, y `xl_suspend` detiene el chip y configura el wake-on-LAN. `virtio_blk.c` es mínimo: `vtblk_suspend` establece un flag y hace quiesce de la cola, `vtblk_resume` borra el flag y reinicia el I/O. El Capítulo 22 hará referencia a cada uno de ellos en el momento en que su patrón ilustre mejor lo que el driver `myfirst` está haciendo.

Tercero, escribe los cambios a mano y prueba cada etapa con `devctl suspend` y `devctl resume`. La gestión de energía es donde las pequeñas omisiones producen fallos característicos: una máscara de interrupción olvidada provoca un resume bloqueado; un `bus_dmamap_sync` olvidado provoca datos obsoletos; una variable de estado olvidada hace que el driver piense que una transferencia sigue en curso. Escribir con cuidado y ejecutar el script de regresión tras cada etapa expone esos errores en el momento en que ocurren.

Cuarto, al terminar la Sección 4, vuelve a leer la ruta de detach del Capítulo 21. La disciplina de quiesce de la Sección 3 y la disciplina de restauración de la Sección 4 comparten infraestructura con el detach del Capítulo 21: `callout_drain`, `taskqueue_drain`, `bus_dmamap_sync`, `pci_release_msi`. Ver suspend-resume y attach-detach uno junto al otro es lo que hace visibles las diferencias. El suspend no es un detach; el resume no es un attach; pero usan los mismos bloques de construcción, compuestos de forma diferente, y ver esa composición en dos pasadas vale la media hora extra.

### Recorrido por el capítulo

Las secciones, en orden, son:

1. **¿Qué es la gestión de energía en los drivers de dispositivo?** El panorama general: por qué le importa la energía a un driver, en qué se diferencian la gestión de energía a nivel de sistema y a nivel de dispositivo, qué significan los S-states de ACPI y los D-states de PCI, qué añaden los link states de PCIe y ASPM, y cómo son las fuentes de wake en los sistemas que el lector tiene más probabilidades de tener. Primero los conceptos, después las APIs.
2. **La interfaz de gestión de energía de FreeBSD.** Los métodos kobj: `DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN`, `DEVICE_QUIESCE`. El orden en que el kernel los entrega. El helper `bus_generic_suspend`, la ruta `pci_suspend_child` y la interacción con ACPI. El primer código en ejecución: Etapa 1 del driver del Capítulo 22 (`1.5-power-stage1`), con manejadores esqueleto que solo registran mensajes.
3. **Hacer quiesce de un dispositivo de forma segura.** Detener la actividad antes de una transición de energía. Enmascarar interrupciones, detener el envío de DMA, drenar el trabajo en vuelo, drenar callouts y taskqueues, vaciar buffers sensibles a políticas. La Etapa 2 (`1.5-power-stage2`) convierte los esqueletos en un quiesce real.
4. **Restaurar el estado en el resume.** Reinicializar el dispositivo a partir del estado guardado. Qué hace y qué no hace el guardado y restauración de PCI. Reactivar el bus-master, restaurar los registros del dispositivo, rearmar las interrupciones, validar la identidad y gestionar el reset del dispositivo. La Etapa 3 (`1.5-power-stage3`) añade la ruta de resume que corresponde al quiesce de la Etapa 2.
5. **Gestión de energía en tiempo de ejecución.** Ahorro de energía cuando el dispositivo está inactivo. Detección de inactividad. Poner el dispositivo en D3 y devolverlo a D0 bajo demanda. Latencia frente a consumo energético. La sección opcional del capítulo, pero con un esquema práctico con el que el lector puede experimentar.
6. **Interactuar con el framework de energía.** Probar transiciones desde el espacio de usuario. `acpiconf -s 3` y `zzz` para el suspend completo del sistema. `devctl suspend` y `devctl resume` para el suspend por dispositivo. `devinfo -v` para observar los estados de energía. El script de regresión que lo engloba todo.
7. **Depurar problemas de gestión de energía.** Los modos de fallo característicos: dispositivo bloqueado, interrupciones perdidas, DMA incorrecto tras el resume, wake por PME# ausente, quejas de WITNESS. Los patrones de depuración que localizan cada uno de ellos.
8. **Refactorizar y versionar tu driver con soporte de energía.** La división final en `myfirst_power.c` y `myfirst_power.h`, el Makefile actualizado, el documento `POWER.md` y la subida de versión. Etapa 4 (`1.5-power`).

Tras las ocho secciones vienen un recorrido extendido por el código de gestión de energía de `if_re.c`, varias miradas más profundas a los sleep states de ACPI, los link states de PCIe, las fuentes de wake y la interfaz de devctl en el espacio de usuario, un conjunto de laboratorios prácticos, un conjunto de ejercicios de desafío, una referencia de resolución de problemas, un apartado de cierre que concluye la historia del Capítulo 22 y abre la del Capítulo 23, un puente y el material habitual de referencia rápida y glosario al final del capítulo. El material de referencia está pensado para volver a leerlo mientras avanzas por los capítulos siguientes; el vocabulario del Capítulo 22 (suspend, resume, quiesce, shutdown, D0, D3, ASPM, PME#) es la base que comparte todo driver de FreeBSD en producción.

Si es tu primera lectura, lee en orden lineal y haz los laboratorios en orden. Si estás revisando, las Secciones 3, 4 y 7 son independientes y se leen bien en una sola sesión.



## Sección 1: ¿Qué es la gestión de energía en los drivers de dispositivo?

Antes del código, la imagen. La Sección 1 enseña qué significa la gestión de energía en el nivel que ve el driver: las capas del sistema que cooperan para ahorrar energía, los sleep states y los estados de energía del dispositivo que esas capas definen, la maquinaria a nivel de link de PCIe que ocurre por debajo de la visibilidad del driver y las fuentes de wake que devuelven el sistema a la actividad. Un lector que termine la Sección 1 podrá leer el resto del capítulo con el vocabulario de la gestión de energía de ACPI y PCI como objetos concretos en lugar de como acrónimos vagos.

### Por qué el driver debe preocuparse por la energía

En los seis capítulos anteriores has enseñado a un driver a comunicarse con un dispositivo. Cada capítulo añadió una capacidad: registros mapeados en memoria, backends simulados, PCI real, interrupciones, interrupciones multivector, DMA. En todos ellos, el dispositivo estaba siempre listo para responder. El BAR estaba siempre mapeado. El vector de interrupción estaba siempre armado. El motor de DMA estaba siempre en marcha. Esa suposición es cómoda para la enseñanza, y también es la suposición que el driver hace durante el funcionamiento normal. Sin embargo, no es la suposición que hace el usuario. El usuario asume que cuando el portátil entra en suspensión, la batería se descarga lentamente; que cuando el NVMe está inactivo, se enfría; que cuando la tarjeta Wi-Fi no tiene nada que transmitir, no está consumiendo vatios de la fuente de alimentación. Esas suposiciones son ingeniería de plataforma real, y el driver es una de las capas que tiene que cooperar para que todo funcione.

Cooperar significa reconocer que el estado de energía del dispositivo puede cambiar mientras el driver está en funcionamiento. El cambio siempre se anuncia: el kernel llama a un método del driver para comunicarle lo que está a punto de ocurrir. Pero el anuncio solo funciona si el driver lo gestiona correctamente. Un driver que ignora el anuncio deja el dispositivo en un estado inconsistente, y el coste se manifiesta de maneras propias de las transiciones de energía: un portátil que no despierta, un servidor cuyo controlador RAID se niega a responder tras un reinicio a nivel de dispositivo, un dock USB que pierde la conectividad al cerrar la tapa. Cada uno de estos fallos remite a un driver que trató un evento de energía como opcional cuando la corrección de la plataforma asumía que era obligatorio.

Lo que está en juego no es solo el consumo en reposo. Un driver que no detiene el DMA antes de que el sistema entre en suspensión puede corromper la memoria en el momento en que el CPU se detiene. Un driver que no enmascara las interrupciones antes de que un bus entre en un estado de menor energía puede provocar eventos de despertar espurios. Un driver que no restaura su configuración tras la reanudación puede leer ceros de un registro que antes contenía una dirección válida. Cada uno de ellos es un bug del kernel cuyo síntoma el usuario describe como "a veces mi máquina no despierta". La disciplina del capítulo 22 es lo que previene esa clase de bug.

### Gestión de energía a nivel de sistema frente a nivel de dispositivo

Dos términos que suenan parecido describen cosas diferentes. Conviene fijarlos desde ahora, porque el capítulo utiliza ambos y la distinción importa a lo largo de todo el texto.

La **gestión de energía a nivel de sistema** es una transición de todo el ordenador. El usuario pulsa el botón de encendido, cierra la tapa o ejecuta `shutdown -p now`. El kernel recorre el árbol de dispositivos, pide a cada driver que suspenda su dispositivo y, a continuación, o bien aparca la CPU en un estado de bajo consumo (S1, S3), escribe el contenido de la memoria en disco (S4) o apaga la alimentación (S5). Todos los drivers del sistema participan en la transición. Si algún driver la rechaza, toda la transición falla; el kernel imprime un mensaje como `DEVICE_SUSPEND(foo0) failed: 16`, el sistema se mantiene activo y el usuario ve un portátil cuya pantalla se oscureció medio segundo y luego volvió.

La **gestión de energía a nivel de dispositivo** es una transición de un único dispositivo. El kernel decide (o se le indica mediante `devctl suspend`) que un dispositivo concreto puede pasar a un estado de menor consumo, con independencia de cualquier otro dispositivo. La tarjeta de red PCIe, por ejemplo, puede pasar a D3 cuando su enlace ha estado inactivo durante unos segundos, y volver a D0 cuando llega un paquete. Todo el sistema permanece en S0 durante ese tiempo. El resto de los dispositivos continúan funcionando. El usuario no nota nada, salvo un ligero aumento de la latencia en el primer paquete tras un período de inactividad, porque la tarjeta de red tuvo que despertar desde D3.

Las transiciones a nivel de sistema y a nivel de dispositivo utilizan los mismos métodos de driver. `DEVICE_SUSPEND` se invoca tanto en una transición completa a S3 (en la que todos los dispositivos se suspenden juntos) como en un `devctl suspend myfirst0` dirigido (en el que solo se suspende el dispositivo `myfirst0`). El driver normalmente no distingue entre ambos casos; la misma disciplina de inactivación funciona en los dos. Lo que difiere es el contexto en torno a la llamada: una suspensión de todo el sistema también desactiva todas las CPUs salvo la CPU de arranque, detiene la mayoría de los threads del kernel y espera que cada driver termine rápidamente; una suspensión por dispositivo deja el resto del sistema en ejecución y se invoca desde el contexto normal del kernel. El driver no necesita preocuparse por esto en la mayoría de los casos. Sin embargo, sí debe tener presente que existen ambos contextos, porque un driver que solo prueba la suspensión por dispositivo puede pasar por alto un error que solo aparece en una suspensión de sistema completo, y viceversa.

El capítulo 22 ejercita ambos caminos. Los laboratorios utilizan `devctl suspend` y `devctl resume` para iterar rápidamente, ya que se ejecutan en milisegundos y no involucran ACPI. La prueba de integración utiliza `acpiconf -s 3` (o `zzz`) para recorrer el camino completo a través de la capa ACPI y la jerarquía de buses. Un driver que supera ambas pruebas tiene muchas más posibilidades de funcionar correctamente en producción que uno que solo supera la primera.

### Estados de suspensión del sistema ACPI (de S0 a S5)

En los portátiles y servidores x86 con los que trabaja la mayoría de los lectores, el estado de energía del sistema está descrito por la especificación ACPI mediante un pequeño conjunto de letras y números denominados **S-states**. Cada S-state define un nivel concreto de qué parte del sistema sigue activa. El driver no elige el S-state (lo elige el usuario, la BIOS o la política del kernel), pero debe conocer cuáles existen y qué implica cada uno para el dispositivo.

**S0** es el estado de trabajo. La CPU está en ejecución, la RAM está alimentada y todos los dispositivos se encuentran en el estado de energía que necesitan. Todo lo que el lector ha hecho hasta ahora ha ocurrido en S0. Es el estado en el que arranca el sistema y del que solo sale cuando se solicita el modo de suspensión.

**S1** se denomina «standby» o «sueño ligero». La CPU deja de ejecutar instrucciones, pero sus registros y cachés se conservan, la RAM permanece alimentada y la mayoría de los dispositivos se mantienen en D0 o D1. El tiempo de despertar es rápido (normalmente un segundo o menos). En el hardware moderno, S1 se usa raramente, porque S3 consume menos energía y tarda casi lo mismo en despertar. FreeBSD admite S1 si la plataforma lo anuncia; la mayoría de las plataformas modernas ya no lo hacen.

**S2** es un estado raramente implementado situado entre S1 y S3. En la mayoría de las plataformas no se anuncia y, cuando lo hace, FreeBSD lo trata de forma similar a S1. El capítulo no vuelve a mencionar S2.

**S3** es el «suspend to RAM», conocido también como «standby» o «sleep» en el lenguaje orientado al usuario. La CPU se detiene, el contexto de la CPU se pierde y debe guardarse antes de la transición, la RAM permanece alimentada mediante su mecanismo de autorrefrescado y la mayoría de los dispositivos pasan a D3 o D3cold. El tiempo de despertar es de uno a tres segundos en un portátil típico. Es el estado en el que entra el usuario al cerrar la tapa de un portátil. En un servidor, S3 es el estado que producen `acpiconf -s 3` o `zzz`. La prueba principal del capítulo 22 es una transición a S3.

**S4** es el «suspend to disk» o hibernación. El contenido completo de la RAM se escribe en una imagen de disco, se retira la alimentación y el sistema regresa leyendo esa imagen en el siguiente arranque. En FreeBSD, el soporte de S4 ha sido históricamente parcial en x86 (la imagen de memoria puede generarse, pero el proceso de restauración no está tan pulido como en Linux o Windows). Desde el punto de vista del driver, S4 se parece a S3 con un paso adicional al final: el driver se suspende exactamente igual que lo haría para S3. La diferencia es invisible para el driver.

**S5** es el «soft off». El sistema está apagado; solo reciben alimentación los circuitos de activación (botón de encendido, wake-on-LAN). Desde el punto de vista del driver, S5 es similar a un apagado del sistema; se invoca el método `DEVICE_SHUTDOWN`, no `DEVICE_SUSPEND`.

En hardware real, el lector puede ver qué estados de suspensión admite la plataforma con:

```sh
sysctl hw.acpi.supported_sleep_state
```

Un portátil típico imprime algo parecido a:

```text
hw.acpi.supported_sleep_state: S3 S4 S5
```

Un servidor podría imprimir solo `S5`, porque la suspensión ACPI rara vez tiene sentido en una máquina de centro de datos. Una VM podría imprimir combinaciones variadas dependiendo del hipervisor. El `sysctl hw.acpi.s4bios` indica si S4 es asistido por la BIOS (raramente lo es en sistemas modernos). El `sysctl hw.acpi.sleep_state` permite al lector entrar manualmente en un estado de suspensión; `acpiconf -s 3` es el envoltorio de línea de comandos preferido.

A los efectos del capítulo 22, el lector debe conocer S3 (el caso habitual) y S5 (el caso de apagado). S1 y S2 los gestiona el driver de la misma manera que S3; S4 es un superconjunto de S3. El capítulo trata S3 como el ejemplo canónico a lo largo de todo el texto.

### Estados de energía de dispositivo PCI y PCIe (de D0 a D3cold)

El estado de energía de un dispositivo está descrito por los **D-states**, que la especificación PCI define con independencia del S-state del sistema. Los métodos del driver controlan directamente el D-state de su dispositivo, y merece la pena entender cada estado en detalle.

**D0** es el estado completamente activo. El dispositivo está alimentado, con reloj activo, accesible a través de su espacio de configuración y sus BARs, y puede realizar cualquier operación que le solicite el driver. Todo el trabajo de los capítulos 16 a 21 se ha realizado con el dispositivo en D0. `PCI_POWERSTATE_D0` es la constante simbólica definida en `/usr/src/sys/dev/pci/pcivar.h`.

**D1** y **D2** son estados intermedios de bajo consumo que la especificación PCI define pero sin restricciones estrictas. Un dispositivo en D1 sigue teniendo sus registros de configuración accesibles y puede responder a cierto I/O; un dispositivo en D2 puede haber perdido más contexto. Estos estados raramente se usan en los PCs modernos porque el salto de D0 a D3 suele ser más conveniente para el ahorro de energía. La mayoría de los drivers no se ocupan de D1 y D2.

**D3hot** es el estado de bajo consumo en el que el dispositivo está efectivamente apagado, pero el bus sigue alimentado, aún se puede acceder al espacio de configuración (las lecturas devuelven principalmente cero o la configuración preservada) y el dispositivo puede emitir una señal PME# si está configurado para ello. La mayoría de los dispositivos entran en D3hot durante la suspensión.

**D3cold** es el estado de aún menor consumo en el que el bus propiamente dicho ha perdido la alimentación. No es posible acceder al dispositivo en absoluto; las lecturas de su espacio de configuración devuelven todos los bits a uno. La única forma de salir de D3cold es que la plataforma restaure la alimentación, lo que normalmente ocurre bajo control de la plataforma (no del driver). D3cold es habitual durante S3 y S4 de sistema completo.

Cuando un driver llama a `pci_set_powerstate(dev, PCI_POWERSTATE_D3)`, la capa PCI hace que el dispositivo pase de su estado actual a D3 (D3hot concretamente; la transición a D3cold es responsabilidad de la plataforma). Cuando el driver llama a `pci_set_powerstate(dev, PCI_POWERSTATE_D0)`, la capa PCI devuelve el dispositivo a D0.

La capa PCI de FreeBSD también gestiona automáticamente estas transiciones durante la suspensión y la reanudación del sistema. La función `pci_suspend_child`, que el driver del bus PCI registra para `bus_suspend_child`, invoca primero el `DEVICE_SUSPEND` del driver y, a continuación (si el sysctl `hw.pci.do_power_suspend` tiene valor verdadero, como ocurre por defecto), hace que el dispositivo pase a D3. En la reanudación, `pci_resume_child` devuelve el dispositivo a D0, restaura el espacio de configuración desde una copia en caché, limpia cualquier señal PME# pendiente y, a continuación, invoca el `DEVICE_RESUME` del driver. El lector puede observar el comportamiento con:

```sh
sysctl hw.pci.do_power_suspend
sysctl hw.pci.do_power_resume
```

Ambos tienen el valor 1 por defecto. Un lector que quiera desactivar la transición automática de D-state (para depuración, o porque el dispositivo se comporta mal en D3) puede establecerlos a 0; en ese caso, el `DEVICE_SUSPEND` y el `DEVICE_RESUME` del driver se ejecutan, pero el dispositivo permanece en D0 durante la transición.

Para el capítulo 22, los hechos importantes son:

- El método `DEVICE_SUSPEND` del driver se ejecuta antes de que cambie el D-state. El driver entra en inactividad mientras el dispositivo sigue en D0.
- El método `DEVICE_RESUME` del driver se ejecuta después de que el dispositivo haya regresado a D0. El driver restaura el estado mientras el dispositivo está accesible.
- El driver no llama (normalmente) a `pci_set_powerstate` directamente durante la suspensión y la reanudación. La capa PCI lo gestiona de forma automática.
- El driver tampoco llama (normalmente) a `pci_save_state` y `pci_restore_state` directamente. La capa PCI también los gestiona de forma automática, a través de `pci_cfg_save` y `pci_cfg_restore`.
- El driver *sí* guarda y restaura su propio estado específico del dispositivo: los contenidos de los registros accesibles por BAR que el hardware puede haber perdido, los campos de softc que siguen la configuración en tiempo de ejecución y los valores de la máscara de interrupciones. La capa PCI no tiene conocimiento de nada de esto.

El límite está donde termina el espacio de configuración PCI y comienzan los registros accesibles por BAR. La capa PCI protege lo primero; el driver protege lo segundo.

### Estados de enlace PCIe y gestión de energía en estado activo (ASPM)

Por debajo del D-state del dispositivo se encuentra el propio enlace PCIe. Un enlace entre un root complex y un endpoint puede estar en uno de varios **L-states**, y las transiciones entre L-states se producen automáticamente cuando el tráfico en el enlace es suficientemente bajo.

**L0** es el estado de enlace completamente activo. Los datos fluyen con normalidad. La latencia está en su mínimo. Es el estado en el que se encuentra el enlace siempre que el dispositivo está activo.

**L0s** es un estado de bajo consumo en el que entra el enlace cuando ha estado inactivo durante unos microsegundos. El transmisor apaga sus etapas de salida en un extremo; el enlace es bidireccional, por lo que el L0s del otro extremo es independiente. La recuperación desde L0s tarda cientos de nanosegundos. Es un ahorro de energía económico que la plataforma puede realizar automáticamente cuando el tráfico es intermitente.

**L1** es un estado de bajo consumo más profundo en el que el enlace entra tras un período de inactividad más prolongado (decenas de microsegundos). Ambos extremos apagan más circuitos de la capa física. La recuperación tarda microsegundos. Se usa durante períodos de carga ligera en los que la penalización de latencia es aceptable.

**L1.1** y **L1.2** son refinamientos de L1 introducidos en PCIe 3.0 y versiones posteriores que añaden un mayor control de la alimentación (power gating), lo que permite una corriente en reposo aún más baja a costa de un despertar más lento.

**L2** es un estado de enlace casi apagado que se utiliza durante D3cold y S3; el enlace está efectivamente desactivado y el despertar requiere una renegociación completa. El driver normalmente no gestiona L2 directamente; es un efecto secundario de que el dispositivo entre en D3cold.

El mecanismo que controla las transiciones entre L0 y L0s/L1 se denomina **Active-State Power Management (ASPM)**. ASPM es una característica por enlace que se configura a través de los registros de capacidad PCIe de ambos extremos del enlace. Puede habilitarse, deshabilitarse o restringirse únicamente a L0s mediante la política de la plataforma. En FreeBSD, ASPM suele estar controlado por el firmware a través de ACPI (el método `_OSC` indica al sistema operativo qué capacidades debe gestionar); el kernel no contradice la política del firmware a menos que se le indique explícitamente.

Para el Capítulo 22 y para la mayoría de los drivers FreeBSD, ASPM es una cuestión de la plataforma, no del driver. El driver no configura ASPM; lo hace la plataforma. El driver no necesita guardar ni restaurar el estado de ASPM durante la suspensión; la capa PCI gestiona los registros de capacidad PCIe como parte del guardado y restauración automáticos del espacio de configuración. Un driver que quiera deshabilitar ASPM para un dispositivo concreto (por ejemplo, porque el dispositivo tiene una errata conocida que hace que L0s no sea seguro) puede hacerlo leyendo y escribiendo el registro PCIe Link Control de forma explícita, pero esto es poco habitual y específico de cada caso.

El lector no necesita añadir código de ASPM al driver `myfirst`. Es suficiente con saber que existen los estados L, que las transiciones entre ellos se producen automáticamente en función del tráfico, que el estado D del driver y el estado L del enlace están relacionados pero son distintos, y que la plataforma se encarga de la configuración de ASPM. Si en el futuro el lector trabaja con un driver cuyo datasheet especifica erratas de ASPM, sabrá dónde buscar.

### Anatomía de un ciclo de suspensión y reanudación

Uniendo todas las piezas, un ciclo completo de suspensión y reanudación del sistema tiene este aspecto desde el punto de vista del driver, siguiendo al driver `myfirst` a través de una transición S3:

1. El usuario cierra la tapa del portátil. El driver del botón ACPI (`acpi_lid`) detecta el evento y, en función de la política del sistema, desencadena una solicitud de suspensión al estado S3.
2. El kernel inicia la secuencia de suspensión. Los demonios del espacio de usuario se pausan; el kernel congela los threads no esenciales.
3. El kernel recorre el árbol de dispositivos y llama a `DEVICE_SUSPEND` en cada dispositivo, en orden inverso al de los hijos. El driver del bus PCI, mediante `bus_suspend_child`, llama al método `device_suspend` del driver `myfirst`.
4. Se ejecuta el método `device_suspend` del driver `myfirst`. Enmascara las interrupciones del dispositivo, deja de aceptar nuevas solicitudes DMA, espera a que se complete cualquier DMA en vuelo, drena su cola de tareas, registra la transición y devuelve 0 para indicar que ha tenido éxito.
5. La capa PCI constata que la suspensión de `myfirst` fue exitosa. Llama a `pci_cfg_save` para guardar en caché el espacio de configuración PCI. Si `hw.pci.do_power_suspend` es 1 (el valor predeterminado), transiciona el dispositivo a D3hot mediante `pci_set_powerstate(dev, PCI_POWERSTATE_D3)`.
6. Más arriba en el árbol, el propio bus PCI, el puente del host y, finalmente, la plataforma pasan por sus propias llamadas a `DEVICE_SUSPEND`. ACPI prepara sus eventos de activación. La CPU entra en el estado de bajo consumo correspondiente a S3. El subsistema de memoria entra en modo de autorefrescado. El enlace PCIe pasa a L2 o un estado similar.
7. Pasa el tiempo. En la escala que el driver puede observar, no transcurre ningún tiempo en absoluto; el kernel no está en ejecución.
8. El usuario abre la tapa. El circuito de activación de la plataforma despierta la CPU. ACPI realiza los pasos iniciales de reanudación: se restaura el contexto de la CPU, se refresca la memoria y el firmware de la plataforma reinicializa lo que sea necesario.
9. Se inicia la secuencia de reanudación del kernel. Recorre el árbol de dispositivos en orden directo, llamando a `DEVICE_RESUME` en cada dispositivo.
10. Para `myfirst`, el driver del bus PCI, mediante `bus_resume_child`, transiciona el dispositivo de vuelta a D0 mediante `pci_set_powerstate(dev, PCI_POWERSTATE_D0)`. Llama a `pci_cfg_restore` para escribir de nuevo el espacio de configuración guardado en el dispositivo. Limpia cualquier señal PME# pendiente con `pci_clear_pme`. A continuación, llama al método `device_resume` del driver.
11. Se ejecuta el método `device_resume` del driver. El dispositivo está en D0, su espacio de configuración está restaurado y sus registros BAR tienen valores nulos o predeterminados. El driver reactiva el bus-mastering si es necesario, escribe de nuevo los registros específicos del dispositivo a partir del estado guardado, reactiva la máscara de interrupción y marca el dispositivo como reanudado. Devuelve 0.
12. La secuencia de reanudación del kernel continúa hacia arriba por el árbol. Los threads del espacio de usuario se descongelan. El usuario ve un sistema en funcionamiento, habitualmente en un plazo de uno a tres segundos.

Los únicos pasos en que el driver tiene trabajo que realizar son el paso 4 y el paso 11. Todo lo demás es maquinaria de la plataforma o del kernel genérico. El trabajo del driver consiste en hacer esos dos pasos correctamente y en comprender lo suficiente los pasos que los rodean para interpretar el comportamiento que observa.

### Fuentes de activación

Un dispositivo suspendido puede ser la razón por la que el sistema se activa. La forma en que ocurre depende del bus:

- En **PCIe**, un dispositivo en D3hot puede activar la señal **PME#** (Power Management Event). El root complex de la plataforma traduce PME# en un evento de activación; el método ACPI `_PRW` identifica qué GPE (General-Purpose Event) utiliza, y el subsistema ACPI traduce la GPE en una activación desde S3. En FreeBSD, la función `pci_enable_pme(dev)` activa la salida PME# del dispositivo; `pci_clear_pme(dev)` limpia cualquier señal pendiente. El helper `pci_has_pm(dev)` indica si el dispositivo tiene alguna capacidad de gestión de energía.
- En **USB**, un dispositivo puede solicitar **remote wakeup** a través de su descriptor USB estándar. El controlador del host (`xhci`, `ohci`, `uhci`) traduce la activación en una señal PME# o equivalente aguas arriba. El driver normalmente no gestiona esto directamente; lo hace la pila USB.
- En **plataformas embebidas**, un dispositivo puede activar un pin **GPIO** que la plataforma conecta a su lógica de activación. Las propiedades `interrupt-extended` o `wakeup-source` del árbol de dispositivos identifican qué pines son fuentes de activación. El framework de interrupciones GPIO de FreeBSD se encarga de esto.
- En **Wake-on-LAN**, un controlador de red vigila paquetes mágicos o coincidencias de patrones mientras está suspendido y activa PME# cuando detecta uno. Tanto el driver como la plataforma deben estar configurados; `re_setwol` en `if_re.c` es un buen ejemplo del lado del driver.

Para el driver `myfirst`, el dispositivo simulado no tiene realmente ninguna fuente de activación (no tiene existencia física fuera de la simulación). El capítulo explica el mecanismo en el punto apropiado de la Sección 4, muestra qué hace `pci_enable_pme` y dónde se llamaría, y deja el desencadenamiento real de la activación a los disparadores manuales del backend de simulación. Un driver para hardware real llamaría a `pci_enable_pme` en su ruta de suspensión cuando se solicite wake-on-X, y a `pci_clear_pme` en su ruta de reanudación para reconocer cualquier señal pendiente.

### Ejemplos del mundo real: Wi-Fi, NVMe, USB

Siempre ayuda anclar las ideas en dispositivos que has utilizado realmente. Consideremos tres.

Un **adaptador Wi-Fi** como los gestionados por `iwlwifi` en Linux o `iwn` en FreeBSD es un ciudadano habitual de la gestión de energía. En S0, pasa la mayor parte de su tiempo en un estado de bajo consumo en el propio chip, asociado al punto de acceso pero sin intercambiar paquetes activamente; cuando se detecta un paquete, el chip se despierta a D0 durante unos milisegundos, intercambia el paquete y vuelve al estado de bajo consumo. En la suspensión del sistema (S3), el kernel pide al driver que guarde su estado; el driver indica al chip que se desasocie de forma limpia (o que configure patrones WoWLAN si el usuario desea activación inalámbrica) y la capa PCI transiciona el chip a D3. En la reanudación, ocurre lo contrario: el chip vuelve a D0, el driver restaura su estado y se vuelve a asociar al punto de acceso. El usuario percibe un retraso de uno o dos segundos tras abrir la tapa antes de que el Wi-Fi vuelva a estar disponible, que corresponde casi íntegramente al tiempo de reasociación y no al tiempo de reanudación del driver.

Un **SSD NVMe** gestiona los estados de energía internamente mediante su propia maquinaria de estados de energía (definida en la especificación NVMe como estados PSx, donde PS0 es potencia completa y los números más altos corresponden a menor consumo). El driver NVMe participa en la suspensión del sistema vaciando sus colas, esperando a que se completen los comandos en vuelo y, a continuación, indicando al controlador que entre en un estado de bajo consumo. En la reanudación, el driver restaura la configuración de colas, indica al controlador que vuelva a PS0 y el sistema reanuda la E/S de disco. Dado que las colas NVMe son grandes y con mucha actividad DMA, la ruta de suspensión de NVMe es un lugar clásico donde un `bus_dmamap_sync` omitido o un vaciado de cola incompleto se manifiesta como corrupción del sistema de archivos tras la reanudación.

Un **dispositivo USB** es gestionado por el driver del controlador del host USB (`xhci`, generalmente). El driver del controlador del host es quien implementa `DEVICE_SUSPEND` y `DEVICE_RESUME`; los drivers USB individuales (para teclados, almacenamiento, audio, etc.) reciben la notificación a través del mecanismo propio de suspensión y reanudación del framework USB. Un driver para un dispositivo USB raramente necesita su propio método `DEVICE_SUSPEND`; el framework USB se encarga de la traducción.

El driver `myfirst` del Capítulo 22 utiliza un modelo de endpoint PCI, que es el caso más habitual y cuyo contrato los demás casos especializan. Aprender primero el patrón PCI te proporciona lo necesario para entender el patrón Wi-Fi, el patrón NVMe y el patrón USB cuando examines esos drivers más adelante.

### Lo que el lector ha aprendido

La Sección 1 es conceptual. No tienes por qué sentirte obligado a recordar cada detalle de cada estado mencionado. Lo que debes extraer de ella es:

- La gestión de energía es un sistema por capas. ACPI define el estado del sistema. PCI define el estado del dispositivo. PCIe define el estado del enlace. El driver percibe el estado de su dispositivo de la forma más directa.
- El estado de cada capa puede transicionar, y las transiciones se componen. Un S3 del sistema implica D3 para cada dispositivo, lo que implica L2 para cada enlace. Un D3 por dispositivo (a partir de la gestión de energía en tiempo de ejecución) no implica S3 del sistema; el sistema permanece en S0.
- El driver tiene un contrato específico con las capas PCI y ACPI. El driver es responsable de detener la actividad de su dispositivo en la suspensión y de restaurar el estado de su dispositivo en la reanudación. La capa PCI gestiona automáticamente el guardado del espacio de configuración, las transiciones de estado D y la señalización de activación mediante PME#. ACPI gestiona la activación de todo el sistema.
- Existen fuentes de activación que se canalizan a través de una cadena específica (PME#, remote wakeup, GPIO). El driver normalmente las activa y desactiva mediante una API de helper; no interactúa directamente con el hardware de activación.
- Las pruebas también son por capas. `devctl suspend`/`devctl resume` ejercita únicamente los métodos del driver. `acpiconf -s 3` ejercita el sistema completo. Un buen script de regresión utiliza ambos.

Con ese panorama claro, la Sección 2 puede presentar las API específicas de FreeBSD que el driver utiliza para integrarse en este sistema.

### Cerrando la Sección 1

La Sección 1 estableció por qué un driver debe preocuparse por la energía, qué significan la gestión de energía a nivel del sistema y a nivel del dispositivo, cómo son los estados S de ACPI y los estados D de PCI, qué añaden los estados L de PCIe, cómo fluye un ciclo de suspensión y reanudación desde el punto de vista del driver y qué son las fuentes de activación. No se mostró ningún código de driver; eso es tarea de la Sección 2. Lo que el lector tiene ahora es un vocabulario y un modelo mental: la suspensión es una transición que anuncia la plataforma; el driver detiene la actividad; la capa PCI lleva el dispositivo a D3; el sistema duerme; al despertar, la capa PCI devuelve el dispositivo a D0; el driver restaura.

Con ese panorama en mente, la siguiente sección presenta la API concreta de FreeBSD para todo esto: los cuatro métodos kobj (`DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN`, `DEVICE_QUIESCE`), cómo los invoca el kernel, cómo se componen con `bus_generic_suspend` y `pci_suspend_child`, y cómo crece la tabla de métodos del driver `myfirst` para incluirlos.



## Sección 2: La interfaz de gestión de energía de FreeBSD

La Sección 1 describió el mundo por capas de ACPI, PCI, PCIe y las fuentes de activación. La Sección 2 reduce la visión a la interfaz del kernel de FreeBSD: los métodos kobj específicos que implementa el driver, la forma en que el kernel los despacha y los helpers genéricos que hacen que todo el esquema sea manejable. Al final de esta sección, el driver `myfirst` tiene una implementación esqueleto de gestión de energía que compila, registra las transiciones y se puede ejercitar con `devctl suspend` y `devctl resume`. El esqueleto todavía no detiene el DMA ni restaura el estado; eso es tarea de la Sección 3 y la Sección 4. El objetivo de la Sección 2 es conseguir que el kernel llame a los métodos del driver, para que el resto del capítulo tenga algo concreto sobre lo que construir.

### Los cuatro métodos kobj

El framework de dispositivos de FreeBSD trata un driver como una implementación de una interfaz kobj definida en `/usr/src/sys/kern/device_if.m`. Ese archivo es un pequeño lenguaje de dominio específico (las reglas de `make -V` lo convierten en una cabecera de punteros a función y wrappers), y define el conjunto de métodos que cualquier driver puede implementar. El trabajo de los Capítulos 16 a 21 ha poblado los métodos comunes: `DEVICE_PROBE`, `DEVICE_ATTACH`, `DEVICE_DETACH`. La gestión de energía añade cuatro más, todos documentados en el mismo archivo con comentarios que puedes leer directamente:

1. **`DEVICE_SUSPEND`** se llama cuando el kernel ha decidido poner el dispositivo en estado suspendido. El método se ejecuta con el dispositivo todavía en D0 y con el driver todavía siendo responsable de él. El trabajo del método es detener la actividad y, si es necesario, guardar cualquier estado que no se restaure automáticamente. Devolver 0 indica éxito. Devolver un valor distinto de cero veta la suspensión.

2. **`DEVICE_RESUME`** se llama después de que el dispositivo haya vuelto a D0 en el camino de vuelta desde la suspensión. El trabajo del método es restaurar cualquier estado que el hardware haya perdido y reanudar la actividad. Devolver 0 indica éxito. Devolver un valor distinto de cero hace que el kernel registre una queja; la reanudación no puede vetarse de forma significativa en esta fase, porque el resto del sistema ya ha vuelto.

3. **`DEVICE_SHUTDOWN`** se llama durante el apagado del sistema para permitir que el driver deje el dispositivo en un estado seguro antes de un reinicio o un apagado total. Muchos drivers implementan este método llamando a su método suspend, porque ambas tareas son similares (detener el dispositivo de forma ordenada). Devolver 0 indica éxito.

4. **`DEVICE_QUIESCE`** se llama cuando el framework quiere que el driver deje de aceptar nuevo trabajo, pero aún no ha decidido hacer el detach. Es una forma más suave del detach: el dispositivo sigue conectado, los recursos siguen asignados, pero el driver debe rechazar nuevas solicitudes y dejar que el trabajo en curso termine de procesarse. Este método es opcional y se implementa con menos frecuencia que los otros tres; `device_quiesce` se llama automáticamente antes de `device_detach` por la capa devctl, de modo que un driver que implementa tanto suspend como quiesce a menudo comparte código entre ellos.

El archivo también contiene implementaciones no-op por defecto: `null_suspend`, `null_resume`, `null_shutdown`, `null_quiesce`. Un driver que no implementa uno de estos métodos recibe el no-op correspondiente, que devuelve 0 y no hace nada. Por eso los capítulos 16 al 21 no mencionaron explícitamente estos métodos: los no-ops se estaban usando de forma silenciosa y, para un driver cuyo dispositivo permanece encendido indefinidamente y cuyo detach solo ocurre al descargar el módulo, los no-ops ofrecen el comportamiento correcto en la mayor parte de los casos.

El primer paso del Capítulo 22 es reemplazar esos no-ops con implementaciones reales.

### Añadir los métodos a la tabla de métodos del driver

El array `device_method_t` en un driver de FreeBSD lista los métodos kobj que el driver implementa. El array de métodos actual del driver `myfirst` (en `myfirst_pci.c`) tiene un aspecto similar a este:

```c
static device_method_t myfirst_pci_methods[] = {
        DEVMETHOD(device_probe,   myfirst_pci_probe),
        DEVMETHOD(device_attach,  myfirst_pci_attach),
        DEVMETHOD(device_detach,  myfirst_pci_detach),

        DEVMETHOD_END
};
```

Añadir la gestión de energía es mecánicamente sencillo: el driver añade tres (o cuatro) líneas `DEVMETHOD`. Los nombres de la izquierda son los nombres de métodos kobj de `device_if.m`; los de la derecha son las implementaciones del driver. Un conjunto completo tiene este aspecto:

```c
static device_method_t myfirst_pci_methods[] = {
        DEVMETHOD(device_probe,    myfirst_pci_probe),
        DEVMETHOD(device_attach,   myfirst_pci_attach),
        DEVMETHOD(device_detach,   myfirst_pci_detach),
        DEVMETHOD(device_suspend,  myfirst_pci_suspend),
        DEVMETHOD(device_resume,   myfirst_pci_resume),
        DEVMETHOD(device_shutdown, myfirst_pci_shutdown),

        DEVMETHOD_END
};
```

Las funciones `myfirst_pci_suspend`, `myfirst_pci_resume` y `myfirst_pci_shutdown` son nuevas; aún no existen. El resto de la sección 2 muestra lo que hace cada una de ellas a nivel de esqueleto.

### Prototipos y valores de retorno

Cada uno de los cuatro métodos tiene la misma firma: un valor de retorno `int` y un argumento `device_t`. El `device_t` es el dispositivo sobre el que se invoca el método, y el driver puede usar `device_get_softc(dev)` para recuperar el puntero al softc.

```c
static int myfirst_pci_suspend(device_t dev);
static int myfirst_pci_resume(device_t dev);
static int myfirst_pci_shutdown(device_t dev);
static int myfirst_pci_quiesce(device_t dev);  /* optional */
```

Los valores de retorno siguen la convención habitual de FreeBSD. Cero indica éxito. Un valor distinto de cero es un valor errno normal que indica qué ha salido mal: `EBUSY` si el driver no puede suspender porque el dispositivo está ocupado, `EIO` si el hardware ha notificado un error, `EINVAL` si el driver fue invocado en un estado imposible. La reacción del kernel varía según el método.

Para `DEVICE_SUSPEND`, un retorno distinto de cero **veta** la suspensión. El kernel aborta la secuencia de suspensión y llama a `DEVICE_RESUME` en los drivers que ya habían suspendido con éxito, deshaciendo la suspensión parcial. Este es el mecanismo que impide que el sistema entre en S3 mientras un dispositivo crítico está en medio de una operación que el driver no puede interrumpir. Debe usarse con moderación; devolver `EBUSY` en cada suspensión cada vez que ocurra algo es una forma segura de hacer la suspensión poco fiable. Un buen driver solo veta cuando el dispositivo está en un estado que realmente no puede suspenderse.

Para `DEVICE_RESUME`, un retorno distinto de cero se registra en el log pero en gran medida se ignora. Cuando se está ejecutando el resume, el sistema está volviendo tanto si le gusta al driver como si no. El driver debe registrar el error, marcar su dispositivo como defectuoso para que las operaciones de I/O posteriores fallen de forma limpia, y retornar. Un veto en el momento del resume llega demasiado tarde para ser útil.

Para `DEVICE_SHUTDOWN`, un retorno distinto de cero es igualmente informativo en su mayor parte. El sistema está apagándose; el driver debe hacer todo lo posible por dejar el dispositivo en un estado seguro, pero un shutdown fallido no es una emergencia.

Para `DEVICE_QUIESCE`, un retorno distinto de cero impide que la operación posterior (normalmente detach) continúe. Un driver que devuelve `EBUSY` desde `DEVICE_QUIESCE` obliga al usuario a esperar o a usar `devctl detach -f` para forzar el detach.

### El orden de entrega de eventos

El kernel no llama a `DEVICE_SUSPEND` en todos los drivers a la vez. Recorre el árbol de dispositivos y llama al método en un orden específico, generalmente **orden inverso de hijos** en la suspensión y **orden directo de hijos** en el resume. Esto se debe a que una suspensión es más segura cuando cada dispositivo se suspende *después* de los dispositivos que dependen de él, y cada dispositivo se reanuda *antes* que los dispositivos que dependen de él.

Considera un árbol simplificado:

```text
nexus0
  acpi0
    pci0
      pcib0
        myfirst0
      pcib1
        em0
      xhci0
        umass0
```

En una suspensión S3, el recorrido por el subárbol bajo `pci0` suspende `myfirst0` antes que `pcib0`, `em0` antes que `pcib1` y `umass0` antes que `xhci0`. Después se suspenden `pcib0`, `pcib1` y `xhci0`. Luego `pci0`. Luego `acpi0`. Y finalmente `nexus0`. Cada padre se suspende solo después de que todos sus hijos estén suspendidos.

En el resume, el orden se invierte. `nexus0` se reanuda primero, luego `acpi0`, luego `pci0`, luego `pcib0`, `pcib1` y `xhci0`. Cada uno de ellos llama a `pci_resume_child` en sus hijos, lo que hace la transición del hijo de vuelta a D0 antes de llamar al `DEVICE_RESUME` del driver hijo. Así, el `device_resume` de `myfirst0` se ejecuta con `pcib0` ya activo y `pci0` ya reconfigurado.

La consecuencia práctica para el driver es que durante `DEVICE_SUSPEND` puede seguir accediendo al dispositivo con normalidad (el bus padre sigue en funcionamiento), y durante `DEVICE_RESUME` también puede acceder al dispositivo con normalidad (el bus padre ya se ha reanudado primero). El driver no necesita gestionar el caso extremo de un padre suspendido.

Hay un matiz: si un bus padre indica que sus hijos deben suspenderse en un orden específico (ACPI puede hacer esto para expresar dependencias implícitas), el helper genérico `bus_generic_suspend` respeta ese orden. El driver `myfirst`, cuyo padre es un bus PCI, no necesita preocuparse por el orden más allá de «hijos antes que padre»; el bus PCI no impone un orden estricto entre sus dispositivos hijos.

### bus_generic_suspend, bus_generic_resume y el bus PCI

Un **driver de bus** es en sí mismo un driver de dispositivo, y cuando el kernel llama a `DEVICE_SUSPEND` en un bus, el bus generalmente tiene que suspender a todos sus hijos antes de poder quedarse él mismo en silencio. Implementar eso a mano sería repetitivo, de modo que el kernel proporciona dos helpers en `/usr/src/sys/kern/subr_bus.c`:

```c
int bus_generic_suspend(device_t dev);
int bus_generic_resume(device_t dev);
```

El primero itera sobre los hijos del bus en orden inverso y llama a `BUS_SUSPEND_CHILD` en cada uno. El segundo itera hacia adelante y llama a `BUS_RESUME_CHILD`. Si la suspensión de algún hijo falla, `bus_generic_suspend` deshace la operación reanudando los hijos que ya habían suspendido.

Un driver de bus típico usa estos helpers directamente:

```c
static device_method_t mybus_methods[] = {
        /* ... */
        DEVMETHOD(device_suspend, bus_generic_suspend),
        DEVMETHOD(device_resume,  bus_generic_resume),
        DEVMETHOD_END
};
```

El driver de bus `virtio_pci_modern` hace exactamente esto en `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`, donde `vtpci_modern_suspend` y `vtpci_modern_resume` simplemente llaman a `bus_generic_suspend(dev)` y `bus_generic_resume(dev)` respectivamente.

El **propio bus PCI** hace algo más sofisticado: su `bus_suspend_child` es `pci_suspend_child`, y su `bus_resume_child` es `pci_resume_child`. Estos helpers (en `/usr/src/sys/dev/pci/pci.c`) hacen exactamente lo que describía la sección 1: en la suspensión llaman a `pci_cfg_save` para almacenar en caché el espacio de configuración, luego llaman al `DEVICE_SUSPEND` del driver, y después, si `hw.pci.do_power_suspend` es verdadero, llaman a `pci_set_powerstate(child, PCI_POWERSTATE_D3)`. En el resume invierten la secuencia: transición de vuelta a D0, restaurar la configuración desde la caché, limpiar el PME# pendiente y llamar al `DEVICE_RESUME` del driver.

El driver `myfirst`, que se enlaza directamente a un dispositivo PCI, no implementa métodos de bus por sí mismo; es un driver hoja. Sus métodos de energía son los que importan para el estado de su propio dispositivo. Sin embargo, conviene que el lector sea consciente de que el bus PCI ya ha realizado trabajo en ambos lados de los métodos del driver: en la suspensión, cuando se ejecuta el `DEVICE_SUSPEND` del driver, la capa PCI ya ha guardado la configuración; en el resume, cuando se ejecuta el `DEVICE_RESUME` del driver, la capa PCI ya ha restaurado la configuración y ha devuelto el dispositivo a D0.

### pci_save_state y pci_restore_state: cuándo los llama el driver

El guardado y restaurado automáticos gestionados por `pci_cfg_save`/`pci_cfg_restore` cubren los registros de configuración PCI estándar: las asignaciones de BAR, el registro de comando, el tamaño de línea de caché, la línea de interrupción y el estado MSI/MSI-X. Para la mayoría de los drivers, esto es suficiente y el driver no necesita llamar a `pci_save_state` ni a `pci_restore_state` explícitamente.

Sin embargo, hay situaciones en las que un driver sí desea guardar la configuración manualmente. La API PCI expone dos funciones helper para esto:

```c
void pci_save_state(device_t dev);
void pci_restore_state(device_t dev);
```

`pci_save_state` es un envoltorio sobre `pci_cfg_save` que almacena en caché la configuración actual. `pci_restore_state` vuelve a escribir la configuración almacenada en caché; si el dispositivo no está en D0 cuando se llama a `pci_restore_state`, el helper hace la transición a D0 antes de restaurar.

Un driver normalmente llama a estas funciones en dos escenarios:

1. **Antes y después de un `pci_set_powerstate` manual que el propio driver inicia**, por ejemplo en un helper de gestión de energía en tiempo de ejecución. Si el driver decide poner un dispositivo inactivo en D3 mientras el sistema está en S0, llama a `pci_save_state`, luego a `pci_set_powerstate(dev, PCI_POWERSTATE_D3)`. Cuando vuelve a despertar el dispositivo, llama a `pci_set_powerstate(dev, PCI_POWERSTATE_D0)` seguido de `pci_restore_state`.

2. **Dentro de `DEVICE_SUSPEND` y `DEVICE_RESUME`, cuando el guardado/restaurado automático está desactivado**. Algunos drivers establecen `hw.pci.do_power_suspend` a 0 para dispositivos que se comportan mal en D3, y gestionan el estado de energía ellos mismos. En ese caso, el driver también es responsable de guardar y restaurar la configuración. Este es un patrón poco habitual.

El driver `myfirst` del capítulo 22 usa el escenario 1 en la sección 5 (PM en tiempo de ejecución), donde el driver decide aparcar el dispositivo en D3 mientras el sistema permanece en S0. Para la suspensión del sistema, el driver no llama a estos helpers directamente; la capa PCI se encarga de ello.

### El helper pci_has_pm

No todos los dispositivos PCI tienen la capacidad de gestión de energía PCI. Los dispositivos más antiguos y algunos de propósito especial no anuncian esta capacidad, lo que significa que el driver no puede dar por sentado que `pci_set_powerstate` o `pci_enable_pme` funcionen. El kernel proporciona un helper para comprobarlo:

```c
bool pci_has_pm(device_t dev);
```

Devuelve true si el dispositivo expone una capacidad de gestión de energía, false en caso contrario. La mayoría de los dispositivos PCIe modernos devuelven true. Los drivers que quieren ser robustos ante hardware inusual protegen sus llamadas relacionadas con la energía:

```c
if (pci_has_pm(sc->dev))
        pci_enable_pme(sc->dev);
```

El driver Realtek en `/usr/src/sys/dev/re/if_re.c` usa este patrón en sus funciones `re_setwol` y `re_clrwol`: si el dispositivo no tiene capacidad PM, la función retorna anticipadamente sin intentar tocar la gestión de energía.

### PME#: activar, desactivar y limpiar

En un dispositivo que sí tiene la capacidad PM, el driver puede pedir al hardware que active PME# cuando se produzca un evento relevante para el wake. La API consta de tres funciones breves:

```c
void pci_enable_pme(device_t dev);
void pci_clear_pme(device_t dev);
/* there is no explicit pci_disable_pme; pci_clear_pme both clears pending
 * events and disables the PME_En bit. */
```

`pci_enable_pme` establece el bit PME_En en el registro de estado/control de gestión de energía del dispositivo, de modo que el siguiente evento de gestión de energía que el dispositivo detecta hace que afirme PME#. `pci_clear_pme` borra cualquier bit de estado PME pendiente y limpia PME_En.

Un driver que quiere habilitar wake-on-LAN, por ejemplo, normalmente:

1. Configura la lógica de wake propia del dispositivo (configurar filtros de patrones, establecer el flag de magic-packet, etc.).
2. Llama a `pci_enable_pme(dev)` en la ruta de suspensión para que el dispositivo pueda realmente afirmar PME#.
3. En la ruta de resume, llama a `pci_clear_pme(dev)` para confirmar el evento de wake.

Si no se llama a `pci_enable_pme`, el dispositivo no activará PME# aunque su propia lógica de wake se dispare. Si no se llama a `pci_clear_pme` en el resume, un bit de estado PME obsoleto puede provocar futuros eventos de wake espurios.

El driver `myfirst` no implementa wake-on-X (el dispositivo simulado no tiene nada por lo que despertar), por lo que estas llamadas no aparecen en el código principal del driver. La sección 4 incluye un breve esquema que muestra dónde irían en un driver real.

### Un primer esqueleto: fase 1

Con toda la base teórica en su lugar, podemos escribir la primera versión de los métodos suspend, resume y shutdown de `myfirst`. La fase 1 del driver del capítulo 22 no hace nada sustancial; solo registra en el log y retorna con éxito. El objetivo es conseguir que el kernel llame a los métodos para que el resto del capítulo pueda probar de forma progresiva.

Primero, añade los prototipos cerca de la parte superior de `myfirst_pci.c`:

```c
static int myfirst_pci_suspend(device_t dev);
static int myfirst_pci_resume(device_t dev);
static int myfirst_pci_shutdown(device_t dev);
```

A continuación, amplía la tabla de métodos:

```c
static device_method_t myfirst_pci_methods[] = {
        DEVMETHOD(device_probe,    myfirst_pci_probe),
        DEVMETHOD(device_attach,   myfirst_pci_attach),
        DEVMETHOD(device_detach,   myfirst_pci_detach),
        DEVMETHOD(device_suspend,  myfirst_pci_suspend),
        DEVMETHOD(device_resume,   myfirst_pci_resume),
        DEVMETHOD(device_shutdown, myfirst_pci_shutdown),

        DEVMETHOD_END
};
```

Luego implementa las tres funciones al final del archivo:

```c
static int
myfirst_pci_suspend(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        device_printf(dev, "suspend (stage 1 skeleton)\n");
        atomic_add_64(&sc->power_suspend_count, 1);
        return (0);
}

static int
myfirst_pci_resume(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        device_printf(dev, "resume (stage 1 skeleton)\n");
        atomic_add_64(&sc->power_resume_count, 1);
        return (0);
}

static int
myfirst_pci_shutdown(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        device_printf(dev, "shutdown (stage 1 skeleton)\n");
        atomic_add_64(&sc->power_shutdown_count, 1);
        return (0);
}
```

Añade los campos de contador al softc en `myfirst.h`:

```c
struct myfirst_softc {
        /* ... existing fields ... */

        uint64_t power_suspend_count;
        uint64_t power_resume_count;
        uint64_t power_shutdown_count;
};
```

Expónlos mediante sysctls junto a los contadores del capítulo 21, en la función que ya añade el árbol sysctl de `myfirst`:

```c
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "power_suspend_count",
    CTLFLAG_RD, &sc->power_suspend_count, 0,
    "Number of times DEVICE_SUSPEND has been called");
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "power_resume_count",
    CTLFLAG_RD, &sc->power_resume_count, 0,
    "Number of times DEVICE_RESUME has been called");
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "power_shutdown_count",
    CTLFLAG_RD, &sc->power_shutdown_count, 0,
    "Number of times DEVICE_SHUTDOWN has been called");
```

Actualiza la cadena de versión en el Makefile:

```make
CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.5-power-stage1\"
```

Compila, carga y prueba:

```sh
cd /path/to/driver
make clean && make
sudo kldload ./myfirst.ko
sudo devctl suspend myfirst0
sudo devctl resume myfirst0
sysctl dev.myfirst.0.power_suspend_count
sysctl dev.myfirst.0.power_resume_count
dmesg | tail -6
```

La salida esperada en `dmesg` es:

```text
myfirst0: suspend (stage 1 skeleton)
myfirst0: resume (stage 1 skeleton)
```

Y los contadores deberían mostrar cada uno el valor 1. Si no es así, uno de estos tres problemas está ocurriendo: el driver no se ha reconstruido, la tabla de métodos no incluía las nuevas entradas, o `devctl` informó de un error porque el kernel no encuentra el dispositivo con ese nombre.

### Lo que demuestra el esqueleto

La fase 1 puede parecer trivial, pero demuestra tres cosas que importan para el resto del capítulo:

1. **El kernel está entregando los métodos.** Si los contadores se incrementan, el despacho kobj desde `devctl` a través del bus PCI hasta el driver `myfirst` está correctamente conectado. Cada fase posterior se apoya en esto, y es más fácil detectar errores de conexión ahora que después de añadir código de quiesce real.

2. **La tabla de métodos está correctamente definida.** Las líneas `DEVMETHOD` con el nombre del método y el puntero a la función del driver están escritas de forma correcta, los includes de cabecera son los adecuados y el terminador `DEVMETHOD_END` está en su lugar. Un error aquí produce un kernel panic en el momento de carga, no un fallo sutil en tiempo de ejecución.

3. **El driver cuenta las transiciones.** Los contadores serán útiles a lo largo del capítulo como una comprobación de invariante sencilla. `power_suspend_count` debería ser siempre igual a `power_resume_count` una vez que el sistema está en reposo; cualquier desviación indica un error en uno de los dos métodos.

Con el esqueleto en su lugar, la sección 3 puede convertir el método suspend de una simple llamada de registro en una auténtica detención de la actividad del dispositivo.

### Una nota sobre Detach, Quiesce y Suspend

Es posible que el lector se pregunte cómo se relacionan `DEVICE_DETACH`, `DEVICE_QUIESCE` y `DEVICE_SUSPEND`. Se parecen entre sí; cada uno le pide al driver que deje de hacer algo. Esta es la distinción práctica, tal como la impone el kernel:

- **`DEVICE_QUIESCE`** es el más suave. Le pide al driver que deje de aceptar trabajo nuevo y que drene el trabajo en vuelo, pero el dispositivo permanece conectado, sus recursos siguen asignados, y otra solicitud puede reactivarlo. El kernel llama a este método antes de `DEVICE_DETACH` para darle al driver la oportunidad de negarse si el dispositivo está ocupado.
- **`DEVICE_SUSPEND`** es intermedio. Le pide al driver que detenga la actividad pero deja los recursos asignados, porque el driver los necesitará de nuevo al reanudar. El estado del dispositivo se preserva (en parte por el kernel mediante el guardado de la configuración PCI, y en parte por el driver a través de su propio estado guardado).
- **`DEVICE_DETACH`** es el más drástico. Le pide al driver que detenga la actividad, libere todos los recursos y olvide el dispositivo. La única manera de volver es mediante un nuevo proceso de attach.

Muchos drivers implementan `DEVICE_QUIESCE` reutilizando partes del camino de suspend (detener interrupciones, detener DMA, vaciar colas) e implementan `DEVICE_SHUTDOWN` llamando directamente al método suspend. `/usr/src/sys/dev/xl/if_xl.c` hace exactamente eso: `xl_shutdown(dev)` simplemente llama a `xl_suspend(dev)`. La relación es la siguiente:

- `shutdown` ≈ `suspend` (para la mayoría de los drivers que no distinguen un comportamiento específico de shutdown, como configurar el wake-on-LAN de forma diferente)
- `quiesce` ≈ la mitad de `suspend` (detener la actividad, no guardar el estado)
- `suspend` = quiesce + guardar estado
- `resume` = restaurar estado + reactivar el dispositivo
- `detach` = quiesce + liberar recursos

El capítulo 22 implementa suspend, resume y shutdown de forma completa. No implementa `DEVICE_QUIESCE` para el driver `myfirst`, porque el camino de detach del capítulo 21 ya realiza el quiesce correctamente, y `device_quiesce` sería redundante. Un driver que quisiera admitir un estado de «detener I/O pero mantener el dispositivo conectado» (por ejemplo, para dar soporte a `devctl detach -f` de forma elegante) añadiría `DEVICE_QUIESCE` como método independiente. El capítulo lo menciona por completitud y continúa avanzando.

### El camino de detach como referencia

El camino de detach del capítulo 21 es una referencia útil porque ya realiza la mayor parte de lo que necesita el suspend. El camino de detach enmascara las interrupciones, vacía la task rx y el callout de simulación, espera a que cualquier DMA en vuelo se complete, y llama a `myfirst_dma_teardown`. El camino de suspend realizará los tres primeros pasos (enmascarar, vaciar, esperar) y omitirá el último (teardown). Un driver bien estructurado extrae esos pasos comunes en funciones auxiliares compartidas para que ambos caminos usen el mismo código.

La sección 3 introduce exactamente esas funciones auxiliares: `myfirst_stop_io`, `myfirst_drain_workers`, `myfirst_mask_interrupts`. Cada una es una pequeña función extraída del camino de detach del capítulo 21. Suspend las usa sin desmantelar los recursos; detach las usa y luego los desmantela. La reutilización mantiene los dos caminos correctos por construcción.

### Observar el esqueleto con WITNESS

Un lector que haya compilado el kernel de depuración con `WITNESS` puede ahora ejecutar el esqueleto y observar si aparece alguna advertencia sobre el orden de los locks. No debería aparecer ninguna, porque el esqueleto no adquiere ningún lock. La sección 3 añadirá la adquisición de locks en el camino de suspend, y `WITNESS` notará de inmediato si el orden difiere del usado en detach. Esa es una de las ventajas de trabajar por etapas: la línea de base está limpia, por lo que cualquier advertencia posterior se puede atribuir claramente a la etapa que la introdujo.

### Cerrando la sección 2

La sección 2 estableció la API específica de FreeBSD para la gestión de energía: los cuatro métodos kobj (`DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN`, `DEVICE_QUIESCE`), la semántica de sus valores de retorno, el orden en que el kernel los entrega, las funciones auxiliares genéricas (`bus_generic_suspend`, `bus_generic_resume`) que recorren el árbol de dispositivos, y las funciones auxiliares específicas de PCI (`pci_suspend_child`, `pci_resume_child`) que guardan y restauran automáticamente el espacio de configuración alrededor de los métodos del driver. El esqueleto de la etapa 1 dotó al driver de implementaciones que solo registran mensajes para los tres métodos principales, añadió contadores al softc, y verificó que `devctl suspend` y `devctl resume` los invocan correctamente.

Lo que el esqueleto no hace es interactuar con el dispositivo en absoluto. Los manejadores de interrupciones siguen disparándose; los transfers de DMA pueden seguir en vuelo cuando suspend retorna; el dispositivo no queda en silencio. La sección 3 corrige todo eso: introduce la disciplina de quiesce, extrae las funciones auxiliares comunes de parada de I/O del camino de detach del capítulo 21, y convierte el esqueleto de la etapa 1 en un driver de etapa 2 que realmente realiza el quiesce del dispositivo antes de informar de éxito.



## Sección 3: Realizar el quiesce de un dispositivo de forma segura

La sección 2 dotó al esqueleto del driver de métodos suspend, resume y shutdown que solo registran mensajes. La sección 3 convierte el esqueleto de suspend en un suspend real: uno que detiene las interrupciones, detiene el DMA, vacía el trabajo diferido y deja el dispositivo en un estado de quietud definido antes de retornar. Hacerlo correctamente es la parte más difícil de la gestión de energía, y es donde se amortizan las primitivas del capítulo 21. Si tienes un camino de teardown de DMA limpio, un vaciado de taskqueue limpio y un vaciado de callout limpio, tienes casi todo lo que necesitas; el quiesce es el arte de aplicarlos en el orden correcto sin desmantelar los recursos que necesitarás de nuevo al reanudar.

### Qué significa realmente el quiesce

La palabra «quiesce» aparece en varios lugares de FreeBSD (`DEVICE_QUIESCE`, `device_quiesce`, `pcie_wait_for_pending_transactions`), y tiene un significado concreto: **llevar un dispositivo a un estado en el que no haya actividad en curso, no pueda iniciarse ninguna actividad nueva, y el hardware no vaya a generar más interrupciones ni realizar más DMA**. El dispositivo sigue completamente conectado, conserva todos sus recursos y tiene sus manejadores de interrupciones registrados, pero no está haciendo nada y no va a hacer nada hasta que algo le indique que empiece de nuevo.

El quiesce es diferente del detach porque el detach deshace las asignaciones de recursos. El quiesce también es diferente de simplemente «establecer un indicador que diga que el dispositivo está ocupado, para que las solicitudes futuras se bloqueen», porque ese indicador solo protege contra la entrada de trabajo nuevo al driver; no detiene el hardware en sí ni la infraestructura del lado del kernel (tasks, callouts) de hacer nada.

Un dispositivo en estado de quiesce, en el sentido del capítulo 22, tiene estas propiedades:

1. El dispositivo no está generando interrupciones y no se le puede provocar para que genere una. Cualquier máscara de interrupción del dispositivo está configurada para suprimir todas las fuentes. El manejador de interrupciones, si se le llama, no tiene nada que hacer.
2. El dispositivo no está realizando DMA. Cualquier transfer de DMA en vuelo ha completado o ha sido abortado. El registro de control del motor está en estado inactivo o ha sido restablecido explícitamente.
3. El trabajo diferido del driver ha sido vaciado. Cualquier task encolada en el taskqueue ha sido ejecutada o se ha esperado explícitamente a ella. Cualquier callout ha sido vaciado y no se disparará.
4. Los campos del softc del driver reflejan el estado de quiesce. El indicador `dma_in_flight` es false. Cualquier contador de elementos en vuelo que lleve el driver es cero. El indicador `suspended` es true, por lo que cualquier nueva solicitud que envíe el espacio de usuario u otro driver recibe un error.

Solo cuando las cuatro propiedades son ciertas está el dispositivo realmente en silencio. Un driver que enmascara las interrupciones pero olvida vaciar el taskqueue todavía tiene una task ejecutándose en segundo plano. Un driver que vacía el taskqueue pero olvida el callout todavía tiene un temporizador que puede dispararse. Un driver que vacía ambos pero olvida detener el DMA puede tener un transfer que escriba bytes en memoria después de que la CPU haya dejado de prestarle atención. Cada omisión produce su propio fallo característico, y la forma más eficaz de evitarlos todos es tener una función que aplique toda la disciplina en un orden conocido.

### El orden importa

Los cuatro pasos anteriores no son independientes. Tienen un orden de dependencia que el driver debe respetar, porque la infraestructura del lado del kernel y el dispositivo interactúan. Considera qué ocurre si el orden es incorrecto.

**Si el DMA se detiene antes de enmascarar las interrupciones**, puede llegar una interrupción entre la parada del DMA y el enmascaramiento. El filtro se ejecuta, ve un bit de estado obsoleto y programa una task. La task se ejecuta, espera que un buffer de DMA esté relleno, encuentra datos obsoletos y puede corromper el estado interno del driver. Es mejor enmascarar las interrupciones primero para que no lleguen interrupciones nuevas mientras se realiza la parada.

**Si el taskqueue se vacía antes de que el dispositivo deje de producir interrupciones**, puede dispararse una nueva interrupción tras el retorno del vaciado y programar una task en una cola que acaba de ser vaciada. La task se ejecuta después, fuera de sincronía con la secuencia de suspend. Es mejor detener las interrupciones primero para que no se programen nuevas tasks.

**Si el callout se vacía antes de detener el DMA**, un motor simulado accionado por un callout podría ver cómo se desmantela su callout mientras un transfer aún está en curso. El transfer nunca se completa; `dma_in_flight` permanece en true; el driver se queda colgado esperando una finalización que no puede llegar. Es mejor detener primero el DMA, esperar a su finalización y luego vaciar el callout.

El orden seguro, usado por el camino de detach del capítulo 21 y adaptado para el suspend del capítulo 22, es:

1. Marcar el driver como suspendido (establecer un indicador en el softc para que las nuevas solicitudes sean rechazadas).
2. Enmascarar todas las interrupciones en el dispositivo (escribir en el registro de máscara de interrupciones).
3. Detener el DMA: si hay un transfer en vuelo, abortarlo y esperar a que alcance un estado terminal.
4. Vaciar el taskqueue (cualquier task que ya esté en ejecución puede terminar; no se inician nuevas tasks).
5. Vaciar los callouts (cualquier disparo en vuelo puede terminar; no se producen nuevos disparos).
6. Verificar el invariante (`dma_in_flight == false`, `softc->busy == 0`, etc.).

Cada paso se apoya en el anterior. En el paso 6, el dispositivo está en silencio.

### Funciones auxiliares, no código inline

Una implementación ingenua pondría los seis pasos inline en `myfirst_pci_suspend`. Eso funciona, pero duplica código que ya tiene el camino de detach y dificulta el mantenimiento de ambos caminos. El patrón preferido del capítulo es extraer los pasos en pequeñas funciones auxiliares que ambos caminos llaman.

Tres funciones auxiliares son suficientes para cubrir toda la disciplina:

```c
static void myfirst_mask_interrupts(struct myfirst_softc *sc);
static int  myfirst_drain_dma(struct myfirst_softc *sc);
static void myfirst_drain_workers(struct myfirst_softc *sc);
```

Cada una tiene un único cometido:

- `myfirst_mask_interrupts` escribe en el registro de máscara de interrupciones del dispositivo para deshabilitar cada vector que le importa al driver. Cuando retorna, no puede llegar ninguna interrupción de este dispositivo.
- `myfirst_drain_dma` pide a cualquier transfer de DMA en vuelo que se detenga (establece el bit ABORT) y espera hasta que `dma_in_flight` sea false. Devuelve 0 en caso de éxito, o un errno distinto de cero si el dispositivo no se detuvo dentro del tiempo de espera.
- `myfirst_drain_workers` llama a `taskqueue_drain` sobre el taskqueue del driver y a `callout_drain` sobre el callout de simulación. Cuando retorna, no queda ningún trabajo diferido pendiente.

El camino de suspend llama a las tres en orden. El camino de detach también llama a las tres, más `myfirst_dma_teardown` y las llamadas de liberación de recursos. Los dos caminos comparten los pasos de quiesce y difieren únicamente al final.

Este es el punto de entrada de quiesce que usa el camino de suspend:

```c
static int
myfirst_quiesce(struct myfirst_softc *sc)
{
        int err;

        MYFIRST_LOCK(sc);
        if (sc->suspended) {
                MYFIRST_UNLOCK(sc);
                return (0);  /* already quiet, nothing to do */
        }
        sc->suspended = true;
        MYFIRST_UNLOCK(sc);

        myfirst_mask_interrupts(sc);

        err = myfirst_drain_dma(sc);
        if (err != 0) {
                device_printf(sc->dev,
                    "quiesce: DMA did not stop cleanly (err %d)\n", err);
                /* Do not fail the quiesce; we still have to drain workers. */
        }

        myfirst_drain_workers(sc);

        return (err);
}
```

Observa la decisión de diseño: `myfirst_quiesce` no deshace los cambios ante un fallo en el vaciado de DMA. Un DMA que no se detiene es un problema de hardware, y el driver no puede deshacer la máscara ni desmarcar el indicador de suspend como respuesta. El driver registra el problema, informa del error al llamante y continúa vaciando los workers para que el resto del estado siga siendo coherente. El llamante (`myfirst_pci_suspend`) decide qué hacer con el error.

### Implementación de myfirst_mask_interrupts

Para el driver `myfirst`, enmascarar las interrupciones significa escribir en el registro de máscara de interrupciones del dispositivo. El backend de simulación del capítulo 19 ya tiene un registro `INTR_MASK` en un offset conocido; el driver escribe todo unos en él para deshabilitar todas las fuentes.

```c
static void
myfirst_mask_interrupts(struct myfirst_softc *sc)
{
        MYFIRST_ASSERT_UNLOCKED(sc);

        /*
         * Disable all interrupt sources at the device. After this write,
         * the hardware will not assert any interrupt vector. Any already-
         * pending status bits remain, but the filter will not be called
         * to notice them.
         */
        CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0xFFFFFFFF);

        /*
         * For real hardware: also clear any pending status bits so we
         * don't see a stale interrupt on resume.
         */
        CSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, 0xFFFFFFFF);
}
```

La función no mantiene el lock de softc. Se comunica con el dispositivo únicamente a través de `CSR_WRITE_4`, que es un wrapper sobre `bus_write_4` y que no requiere ninguna disciplina de lock en particular. La llamada a `MYFIRST_ASSERT_UNLOCKED` es un invariante habilitado por WITNESS que detecta a cualquier llamador que, por error, mantenga el lock al invocar esta función; es barato y útil.

El valor de máscara `0xFFFFFFFF` asume que el registro INTR_MASK de la simulación utiliza la semántica de que 1 equivale a enmascarado. La simulación del Capítulo 19 usa esa convención; un driver real debe consultar el datasheet de su dispositivo. El mapa de registros de `myfirst` está documentado en `INTERRUPTS.md`; el lector puede verificar allí la convención.

Una sutileza: en algunos dispositivos reales, enmascarar las interrupciones solo impide que el dispositivo aserte nuevas interrupciones; la que está activa en ese momento continúa asercionada hasta que el driver la reconoce a través del registro de estado. Por eso la función también borra INTR_STATUS: para asegurarse de que no quede ningún bit residual asercionado que pueda dispararse de nuevo después de la reanudación. En la simulación, el registro de estado se comporta de forma similar, por lo que la misma escritura es correcta.

### Implementación de myfirst_drain_dma

Drenar el DMA es la más delicada de las tres funciones auxiliares, porque tiene que esperar al dispositivo. El driver del Capítulo 21 rastrea el DMA en curso con `dma_in_flight` y notifica la finalización a través de `dma_cv`. La ruta de suspend reutiliza exactamente ese mecanismo.

```c
static int
myfirst_drain_dma(struct myfirst_softc *sc)
{
        int err = 0;

        MYFIRST_LOCK(sc);
        if (sc->dma_in_flight) {
                /*
                 * Tell the engine to abort. The abort bit produces an
                 * ERR status that the filter will translate into a task
                 * that wakes us via cv_broadcast.
                 */
                CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL,
                    MYFIRST_DMA_CTRL_ABORT);

                /*
                 * Wait up to one second for the abort to land. The
                 * cv_timedwait drops the lock while we sleep.
                 */
                err = cv_timedwait(&sc->dma_cv, &sc->mtx, hz);
                if (err == EWOULDBLOCK) {
                        device_printf(sc->dev,
                            "drain_dma: timeout waiting for abort\n");
                        /*
                         * Force the state forward. The device is
                         * beyond our reach at this point; treat the
                         * transfer as failed.
                         */
                        sc->dma_in_flight = false;
                }
        }
        MYFIRST_UNLOCK(sc);

        return (err == EWOULDBLOCK ? ETIMEDOUT : 0);
}
```

La función solicita al motor DMA que aborte y luego duerme en la variable de condición que usa la ruta de finalización del Capítulo 21. Si llega la finalización, el filtro la reconoce y encola una tarea; la tarea llama a `myfirst_dma_handle_complete`, que realiza la sincronización POSTREAD/POSTWRITE, limpia `dma_in_flight` y emite un broadcast en la CV. El `cv_timedwait` de la ruta de suspend retorna, la función de drenado retorna 0 y el suspend continúa.

Si la finalización no llega en un segundo (un segundo es un timeout generoso para un dispositivo simulado cuyo callout se dispara cada pocos milisegundos), la función registra una advertencia y fuerza `dma_in_flight` a false. Es una decisión defensiva: un dispositivo real que no atiende un abort en un segundo tiene un comportamiento incorrecto, y el driver tiene que continuar. Dejar `dma_in_flight` en true provocaría un deadlock en el suspend. El coste de este borrado defensivo es que una transferencia muy lenta podría, en principio, completarse después de que el suspend haya retornado, escribiendo en un buffer que el driver ya no espera que esté activo. En la simulación, eso no puede ocurrir porque el callout se drena en el siguiente paso. En hardware real, el riesgo es específico del hardware y un driver real añadiría aquí una recuperación específica del dispositivo.

El valor de retorno es 0 en un drenado limpio (incluido el caso de timeout, que ha sido forzado a limpiarse) y `ETIMEDOUT` si el llamador necesita saber que se produjo un timeout. La ruta de suspend registra el error, pero no veta el suspend; cuando el drenado ha superado el timeout, el dispositivo está prácticamente averiado de todas formas.

### Implementación de myfirst_drain_workers

Drenar el trabajo diferido es más sencillo porque no implica al dispositivo. La tarea rx y el callout de la simulación tienen, cada uno, primitivas de drenado bien conocidas.

```c
static void
myfirst_drain_workers(struct myfirst_softc *sc)
{
        /*
         * Drain the per-vector rx task. Any task currently running is
         * allowed to finish; no new tasks will be scheduled because
         * interrupts are already masked.
         */
        if (sc->rx_vector.has_task)
                taskqueue_drain(taskqueue_thread, &sc->rx_vector.task);

        /*
         * Drain the simulation's DMA callout. Any fire in flight is
         * allowed to finish; no new firings will happen.
         *
         * This is a simulation-only call; a real-hardware driver would
         * omit it.
         */
        if (sc->sim != NULL)
                myfirst_sim_drain_dma_callout(sc->sim);
}
```

La función es segura para llamar con el lock liberado. `taskqueue_drain` está documentado para realizar su propia sincronización; `callout_drain` (que `myfirst_sim_drain_dma_callout` envuelve internamente) es igualmente seguro.

Una propiedad importante de ambas llamadas de drenado: esperan a que el trabajo en ejecución finalice, pero no lo cancelan a mitad. Una tarea que esté a mitad de `myfirst_dma_handle_complete` completará su trabajo, incluida cualquier operación `bus_dmamap_sync` y actualización de contador, antes de que el drenado retorne. Ese es el comportamiento que queremos: el suspend no debe interrumpir una tarea a mitad, porque los invariantes de la tarea deben mantenerse para que la ruta de resume sea correcta.

### Actualización del método suspend

Con las tres funciones auxiliares implementadas, el método suspend es breve:

```c
static int
myfirst_pci_suspend(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        int err;

        device_printf(dev, "suspend: starting\n");

        err = myfirst_quiesce(sc);
        if (err != 0) {
                device_printf(dev,
                    "suspend: quiesce returned %d; continuing anyway\n",
                    err);
        }

        atomic_add_64(&sc->power_suspend_count, 1);
        device_printf(dev,
            "suspend: complete (dma in flight=%d, suspended=%d)\n",
            sc->dma_in_flight, sc->suspended);
        return (0);
}
```

El método suspend no devuelve el error de quiesce al kernel. Es una decisión de política, y merece una explicación.

Devolver un valor distinto de cero desde `DEVICE_SUSPEND` veta el suspend, lo que tiene un efecto significativo en cadena: el kernel deshace el suspend parcial, notifica el fallo al usuario y deja el sistema en S0. Para el driver del Capítulo 22, un timeout de quiesce no justifica ese nivel de interrupción. El dispositivo sigue siendo accesible; enmascarar la interrupción y marcar el flag de suspended es suficiente para prevenir cualquier actividad. Un driver real podría elegir de otra forma: un controlador de almacenamiento con una escritura pendiente podría vetar el suspend hasta que la escritura se complete, porque perder esa escritura durante la transición corrompería el sistema de archivos. Cada driver toma su propia decisión.

El registro de mensajes es detallado en esta etapa. La Sección 7 introducirá la capacidad de desactivar el registro detallado (o activarlo al máximo para depuración) a través de un sysctl. Por ahora, el detalle adicional ayuda cuando se recorre la secuencia de suspend por primera vez.

### Guardar el estado en tiempo de ejecución que el hardware pierde

Hasta ahora, la ruta de suspend solo ha detenido la actividad. No ha guardado ningún estado. Para la mayoría de los dispositivos, eso es correcto: la capa PCI guarda el espacio de configuración automáticamente, y los registros en tiempo de ejecución del dispositivo (los que el driver escribe a través de BARs) se restauran desde el softc del driver al reanudar o se regeneran a partir del estado software del driver. La simulación `myfirst` no tiene ningún estado local de BAR que el driver necesite preservar; la simulación empieza de cero al reanudar, y el driver escribe los registros que necesita en ese momento.

Un driver real puede tener más. Considera el driver `re(4)`: su función `re_setwol` escribe los registros relacionados con wake-on-LAN en el espacio de configuración respaldado por EEPROM de la NIC antes del suspend. Esos valores son privados del dispositivo; la capa PCI no los conoce. Si el driver no los escribiera en el suspend, la NIC no sabría que debe despertar ante un magic packet, y wake-on-LAN no funcionaría.

Para el Capítulo 22, el único estado que guarda el driver `myfirst` es el valor previo al suspend de la máscara de interrupciones. El suspend de la Etapa 2 escribe `0xFFFFFFFF` en el registro de máscara, pero el resume de la Etapa 2 necesita saber qué valor había antes (que determina qué vectores están habilitados en funcionamiento normal). El driver lo almacena en un campo del softc:

```c
struct myfirst_softc {
        /* ... */
        uint32_t saved_intr_mask;
};
```

Y la función auxiliar de máscara lo guarda:

```c
static void
myfirst_mask_interrupts(struct myfirst_softc *sc)
{
        sc->saved_intr_mask = CSR_READ_4(sc, MYFIRST_REG_INTR_MASK);

        CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0xFFFFFFFF);
        CSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, 0xFFFFFFFF);
}
```

La ruta de resume escribirá `sc->saved_intr_mask` de vuelta en el registro de máscara después de que el dispositivo se haya reinicializado. Este es un ejemplo mínimo de guardar y restaurar el estado; la Sección 4 lo desarrolla cuando muestra el flujo completo de resume.

### El flag suspended como invariante visible para el usuario

Establecer `sc->suspended = true` durante el quiesce tiene un segundo propósito más allá de suprimir nuevas peticiones: hace el estado observable para el espacio de usuario. El driver puede exponer el flag a través de un sysctl:

```c
SYSCTL_ADD_BOOL(ctx, kids, OID_AUTO, "suspended",
    CTLFLAG_RD, &sc->suspended, 0,
    "Whether the driver is in the suspended state");
```

Tras ejecutar `devctl suspend myfirst0`, verás:

```sh
# sysctl dev.myfirst.0.suspended
dev.myfirst.0.suspended: 1
```

Tras ejecutar `devctl resume myfirst0`, el valor debería volver a 0 (la Sección 4 conecta la ruta de resume para borrarlo). Es una forma rápida de comprobar el estado del driver sin tener que inferirlo a partir de otros contadores.

### Gestión del caso en que no hay DMA en curso

La función auxiliar `myfirst_drain_dma` gestiona el caso en que una transferencia está activamente en ejecución. También debe gestionar el caso mucho más habitual en que no hay nada en curso en el momento del suspend, sin hacer nada innecesario.

El pseudocódigo anterior sí lo gestiona: el guard `if (sc->dma_in_flight)` omite completamente el abort y la espera cuando el flag es false. La función retorna 0 de inmediato y el suspend continúa.

Esa ruta es rápida: en un dispositivo inactivo, `myfirst_drain_dma` consiste en adquirir el lock, comprobar el flag y liberar el lock. El coste del quiesce está dominado por `taskqueue_drain` (que realiza un viaje de ida y vuelta completo por el thread del taskqueue) y `callout_drain` (de forma similar). Un suspend típico con dispositivo inactivo tarda de unos cientos de microsegundos a unos pocos milisegundos, dominado por los drenados del trabajo diferido, no por el dispositivo.

### Prueba de la Etapa 2

Con el código de quiesce implementado, la prueba de la Etapa 2 es más interesante. Ejecutas una transferencia, luego suspendes de inmediato y observas:

```sh
# Start with no activity.
sysctl dev.myfirst.0.dma_transfers_read
# 0

# Trigger a transfer. The transfer should complete quickly.
sudo sysctl dev.myfirst.0.dma_test_read=1
sysctl dev.myfirst.0.dma_transfers_read
# 1

# Now stress the path: start a transfer and immediately suspend.
sudo sysctl dev.myfirst.0.dma_test_read=1 &
sudo devctl suspend myfirst0

# Check the state.
sysctl dev.myfirst.0.suspended
# 1

sysctl dev.myfirst.0.power_suspend_count
# 1

sysctl dev.myfirst.0.dma_in_flight
# 0 (the transfer completed or was aborted)

dmesg | tail -8
```

La salida esperada de `dmesg` muestra el registro del suspend, el DMA en curso siendo abortado y su finalización. Si `dma_in_flight` sigue siendo 1 después de que el suspend haya retornado, el abort no surtió efecto y debes comprobar el manejo del abort en la simulación.

A continuación, reanuda:

```sh
sudo devctl resume myfirst0

sysctl dev.myfirst.0.suspended
# 0 (after Section 4 is implemented)

sysctl dev.myfirst.0.power_resume_count
# 1

# Try another transfer to check the device is back.
sudo sysctl dev.myfirst.0.dma_test_read=1
dmesg | tail -4
```

La última transferencia debería completarse correctamente; si no lo hace, el suspend dejó el dispositivo en un estado que el resume no recuperó. La Sección 4 enseña la ruta de resume que hace esto correcto.

### Una nota de precaución sobre el locking

El código de quiesce se ejecuta desde el método `DEVICE_SUSPEND`, al que llama la ruta de gestión de energía del kernel. Esa ruta no mantiene ningún lock del driver cuando llama al método; el driver es responsable de su propia sincronización. Las funciones auxiliares de esta sección siguen una disciplina específica:

- `myfirst_mask_interrupts` no mantiene ningún lock. Solo escribe registros de hardware, que son atómicos en PCIe.
- `myfirst_drain_dma` toma el lock del softc para leer `dma_in_flight` y usa `cv_timedwait` para dormir mientras mantiene el lock (que es el uso correcto de una CV con sleep-mutex).
- `myfirst_drain_workers` no mantiene ningún lock. `taskqueue_drain` y `callout_drain` realizan su propia sincronización y deben llamarse sin el lock para evitar deadlock (la tarea que se está drenando puede intentar adquirir el mismo lock).

La secuencia completa de quiesce adquiere y libera el lock varias veces: una vez brevemente al inicio de `myfirst_quiesce` para establecer `suspended`, una vez dentro de `myfirst_drain_dma` para el sleep, y nunca dentro de `myfirst_drain_workers`. Es intencional. Mantener el lock durante `taskqueue_drain` provocaría un deadlock, porque la tarea drenada adquiere el mismo lock al inicio.

Si ejecutas este código bajo `WITNESS`, no verás ninguna advertencia de orden de locks, porque el lock solo se mantiene durante el sleep de la CV y no se adquiere ningún otro lock durante ese intervalo. Si un trabajo posterior añade más locks al driver (por ejemplo, un lock por vector), el código de quiesce debe seguir siendo cuidadoso con qué locks se mantienen alrededor de las llamadas de drenado.

### Integración con el método shutdown

El método shutdown comparte casi toda su lógica con el suspend. Una implementación razonable es:

```c
static int
myfirst_pci_shutdown(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        device_printf(dev, "shutdown: starting\n");
        (void)myfirst_quiesce(sc);
        atomic_add_64(&sc->power_shutdown_count, 1);
        device_printf(dev, "shutdown: complete\n");
        return (0);
}
```

La única diferencia respecto al suspend es la ausencia de llamadas para guardar el estado (el shutdown es definitivo; no hay un resume para el que guardar el estado) y la ausencia de comprobación del valor de retorno del quiesce (el shutdown no puede vetarse de forma significativa). Muchos drivers reales siguen el mismo patrón; el `xl_shutdown` de `/usr/src/sys/dev/xl/if_xl.c` simplemente llama a `xl_suspend`. El driver `myfirst` puede usar cualquiera de los dos estilos; el capítulo prefiere la versión ligeramente más explícita anterior porque deja la intención más clara en el código.

### Cerrando la Sección 3

La Sección 3 ha convertido el esqueleto de la Etapa 1 en un driver de Etapa 2 que realmente pone en reposo el dispositivo al suspender. Ha introducido la disciplina de quiesce (marcar como suspended, enmascarar interrupciones, detener el DMA, drenar los workers, verificar), ha factorizado los pasos en tres funciones auxiliares (`myfirst_mask_interrupts`, `myfirst_drain_dma`, `myfirst_drain_workers`), ha explicado el orden en que deben ejecutarse y por qué, ha mostrado cómo integrarlas en los métodos suspend y shutdown, y ha analizado la disciplina de locking.

Lo que el driver de la Etapa 2 todavía no hace es volver correctamente. El método resume sigue siendo el esqueleto de la Etapa 1; registra un mensaje y retorna 0 sin restaurar nada. Si el dispositivo suspendido tenía algún estado que el hardware perdió, ese estado ha desaparecido y una transferencia posterior fallará. La Sección 4 corrige la ruta de resume para que un ciclo completo de suspend-resume deje el dispositivo en el mismo estado en que estaba antes.



## Sección 4: Restauración del estado al reanudar

La Sección 3 proporcionó al driver una ruta de suspend correcta. La Sección 4 escribe el resume correspondiente. El resume es el complemento del suspend: todo lo que el suspend detuvo, el resume lo reinicia; todo valor que el suspend guardó, el resume lo escribe de vuelta; todo flag que el suspend estableció, el resume lo limpia. La secuencia no es un espejo exacto (el resume se ejecuta en un contexto de kernel diferente, con la capa PCI habiendo realizado ya parte del trabajo, y el dispositivo en un estado distinto al que dejó el suspend), pero los contenidos se corresponden uno a uno. Hacer el resume correctamente es cuestión de respetar el contrato que la capa PCI ya ha cumplido en parte y de completar el resto.

### Lo que la capa PCI ya ha hecho

Cuando el método `DEVICE_RESUME` del kernel se llama en el driver, varias cosas ya han ocurrido:

1. La CPU ha salido del estado S (ha reanudado desde S3 o S4 de vuelta a S0).
2. La memoria ha sido restaurada y el kernel ha restablecido su propio estado.
3. El bus padre ha sido reanudado. Para `myfirst`, esto significa que el driver del bus PCI ya ha gestionado el host bridge y el root complex PCIe.
4. La capa PCI ha llamado a `pci_set_powerstate(dev, PCI_POWERSTATE_D0)` sobre el dispositivo, llevándolo desde el estado de bajo consumo en el que se encontraba (normalmente D3hot) de vuelta al pleno funcionamiento.
5. La capa PCI ha llamado a `pci_cfg_restore(dev, dinfo)`, que vuelve a escribir en el dispositivo los valores del espacio de configuración almacenados en caché (BARs, registro de comandos, tamaño de línea de caché, etc.).
6. La capa PCI ha llamado a `pci_clear_pme(dev)` para borrar cualquier bit de evento de gestión de energía que esté pendiente.
7. La configuración MSI o MSI-X, que forma parte del estado almacenado en caché, ha sido restaurada. Los vectores de interrupción del driver vuelven a estar disponibles.

En este punto, el driver del bus PCI llama al método `DEVICE_RESUME` de `myfirst`. El dispositivo se encuentra en D0, con sus BARs mapeados, su tabla MSI/MSI-X restaurada y su estado PCI genérico intacto. Lo que el driver debe restaurar es el estado específico del dispositivo que la capa PCI desconoce: los registros locales de los BARs que el driver escribió durante el attach o después de él.

En la simulación de `myfirst`, los registros locales de los BARs relevantes son la máscara de interrupciones (que la ruta de suspensión estableció deliberadamente con todas las interrupciones enmascaradas) y los registros DMA (que pueden haber quedado en un estado abortado). El driver debe devolverlos a los valores que corresponden al funcionamiento normal.

### La disciplina del resume

Una ruta de resume correcta hace cuatro cosas, en orden:

1. **Volver a habilitar el bus-mastering**, por si la restauración del espacio de configuración no lo hizo o si la restauración automática de la capa PCI estaba desactivada. Esto es `pci_enable_busmaster(dev)`. En versiones modernas de FreeBSD suele ser redundante pero inofensivo; en rutas de código antiguas o con BIOSes defectuosas, a veces el bus-mastering queda deshabilitado. Llamarlo de forma defensiva tiene un coste ínfimo.

2. **Restaurar el estado específico del dispositivo** que el driver guardó durante suspend. En el caso de `myfirst`, eso significa volver a escribir `saved_intr_mask` en el registro INTR_MASK. Un driver real también restauraría bits de configuración específicos del fabricante, la programación del motor DMA, temporizadores hardware, etc.

3. **Desenmascarar las interrupciones y borrar la marca de suspensión**, para que el dispositivo pueda reanudar su actividad. Este es el punto de inflexión: antes de él, el dispositivo permanece en silencio; después, puede generar interrupciones y aceptar trabajo.

4. **Registrar la transición y actualizar los contadores**, para observabilidad y pruebas de regresión.

Así es como luce este patrón en código:

```c
static int
myfirst_pci_resume(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        int err;

        device_printf(dev, "resume: starting\n");

        err = myfirst_restore(sc);
        if (err != 0) {
                device_printf(dev,
                    "resume: restore failed (err %d)\n", err);
                atomic_add_64(&sc->power_resume_errors, 1);
                /*
                 * Continue anyway. By the time we're here, the system
                 * is coming back whether we like it or not.
                 */
        }

        atomic_add_64(&sc->power_resume_count, 1);
        device_printf(dev, "resume: complete\n");
        return (0);
}
```

El helper `myfirst_restore` realiza los tres pasos reales:

```c
static int
myfirst_restore(struct myfirst_softc *sc)
{
        /* Step 1: re-enable bus-master (defensive). */
        pci_enable_busmaster(sc->dev);

        /* Step 2: restore device-specific state.
         *
         * For myfirst, this is just the interrupt mask. A real driver
         * would restore more: DMA engine programming, hardware timers,
         * vendor-specific configuration, etc.
         */
        if (sc->saved_intr_mask == 0xFFFFFFFF) {
                /*
                 * Suspend saved a fully-masked mask, which means the
                 * driver had no idea what the mask should be. Use the
                 * default: enable DMA completion, disable everything
                 * else.
                 */
                sc->saved_intr_mask = ~MYFIRST_INTR_COMPLETE;
        }
        CSR_WRITE_4(sc->dev, MYFIRST_REG_INTR_MASK, sc->saved_intr_mask);

        /* Step 3: clear the suspended flag and unmask the device. */
        MYFIRST_LOCK(sc);
        sc->suspended = false;
        MYFIRST_UNLOCK(sc);

        return (0);
}
```

La función devuelve 0 porque ninguno de los pasos anteriores puede fallar en la simulación de `myfirst`. Un driver real comprobaría los valores de retorno de sus llamadas de inicialización de hardware y propagaría cualquier error.

### Por qué importa pci_enable_busmaster

El bus-mastering es un bit del registro de comandos PCI que controla si el dispositivo puede emitir transacciones DMA. Sin él, el dispositivo no puede leer ni escribir en la memoria del host; cualquier petición DMA sería ignorada en silencio por el puente PCI.

El capítulo 18 habilitó el bus-mastering durante attach. La restauración automática del espacio de configuración de la capa PCI escribe de nuevo el registro de comandos con su valor guardado, que incluye el bit de bus-master. Por tanto, en principio el driver no necesita volver a llamar a `pci_enable_busmaster` en el resume. En la práctica, pueden ocurrir varias cosas que lo hagan necesario:

- El firmware de la plataforma puede restablecer el registro de comandos como parte del proceso de despertar el dispositivo.
- El sysctl `hw.pci.do_power_suspend` puede ser 0, en cuyo caso la capa PCI no guarda ni restaura el espacio de configuración.
- Una particularidad específica del dispositivo podría borrar el bus-mastering como efecto secundario de la transición de D3 a D0.

Llamar a `pci_enable_busmaster` incondicionalmente en el resume, de forma defensiva, es una red de seguridad de bajo coste. Varios drivers de FreeBSD en producción siguen este patrón; la ruta de resume de `if_re.c` es uno de los ejemplos. La llamada es idempotente: si el bus-mastering ya está activado, simplemente lo reaserta.

### Restaurar el estado específico del dispositivo

La simulación de `myfirst` no tiene mucho estado que el driver necesite restaurar manualmente. Los registros locales del BAR son:

- La máscara de interrupciones (restaurada desde `saved_intr_mask`).
- Los bits de estado de interrupción (se borraron durante suspend; deben permanecer borrados hasta que llegue nueva actividad).
- Los registros del motor DMA (DMA_ADDR_LOW, DMA_ADDR_HIGH, DMA_LEN, DMA_DIR, DMA_CTRL, DMA_STATUS). Estos son transitorios: contienen los parámetros de la transferencia en curso. Tras el resume, no hay ninguna transferencia en curso, por lo que los valores no importan; la siguiente transferencia los sobreescribirá.

Un driver real tendría más. Considera algunos ejemplos:

- Un driver de almacenamiento podría tener un anillo de descriptores DMA cuya dirección base el dispositivo aprendió durante attach. Tras el resume, el registro del nivel BAR que contiene esa dirección base puede haberse restablecido; el driver necesita reprogramarlo.
- Un driver de red podría tener tablas de filtrado (direcciones MAC, listas de multidifusión, etiquetas VLAN) programadas en registros del dispositivo. Tras el resume, esas tablas pueden estar vacías; el driver las reconstruye a partir de las copias guardadas en el softc.
- Un driver de GPU podría tener estado de registro para la sincronización de pantalla, tablas de color y cursores hardware. Tras el resume, el driver restaura el modo activo.

Para `myfirst`, la máscara de interrupciones es el único estado local del BAR que necesita restauración. El patrón mostrado anteriormente es la plantilla que un driver real adaptaría a su dispositivo.

### Validar la identidad del dispositivo tras el resume

Algunos dispositivos se reinician por completo durante un ciclo de suspend a D3-cold. El dispositivo que vuelve es funcionalmente el mismo, pero todo su estado se ha reinicializado como si acabara de encenderse. Un driver que asumiera que nada ha cambiado tendría un comportamiento incorrecto sin advertirlo.

Una ruta de resume defensiva puede detectar esto leyendo un valor de registro conocido y comparándolo con el que leyó durante attach. Para un dispositivo PCI, el vendor ID y el device ID en el espacio de configuración son siempre los mismos (la capa PCI los restauró), pero se puede verificar algún registro privado del dispositivo (un ID de revisión, un registro de autocomprobación, una versión de firmware):

```c
static int
myfirst_validate_device(struct myfirst_softc *sc)
{
        uint32_t magic;

        magic = CSR_READ_4(sc->dev, MYFIRST_REG_MAGIC);
        if (magic != MYFIRST_MAGIC_VALUE) {
                device_printf(sc->dev,
                    "resume: device identity mismatch (got %#x, "
                    "expected %#x)\n", magic, MYFIRST_MAGIC_VALUE);
                return (EIO);
        }
        return (0);
}
```

Para la simulación de `myfirst`, no existe un registro mágico (la simulación no se construyó pensando en la validación posterior al resume). Un lector que quiera añadir uno como desafío puede extender el mapa de registros del backend de simulación con un registro `MAGIC` de solo lectura y hacer que el driver lo compruebe. El laboratorio 3 del capítulo incluye esto como opción.

Un driver real cuyo dispositivo se reinicia de verdad durante D3cold necesita esta comprobación, porque sin ella puede producirse un fallo sutil: el driver asume que la máquina de estados interna del dispositivo está en el estado `IDLE`, pero tras el reinicio la máquina de estados está en realidad en el estado `RESETTING`. Cualquier comando que el driver envíe es rechazado, el driver interpreta el rechazo como un fallo de hardware y el dispositivo queda marcado como defectuoso. Detectar el reinicio explícitamente y reconstruir el estado es una solución más limpia.

### Detectar y recuperarse del reinicio de un dispositivo

Si la validación encuentra una discrepancia, las opciones de recuperación del driver dependen del hardware. Para la simulación de `myfirst`, la respuesta más sencilla es registrar el evento, marcar el dispositivo como defectuoso y hacer que las operaciones posteriores fallen:

```c
if (myfirst_validate_device(sc) != 0) {
        MYFIRST_LOCK(sc);
        sc->broken = true;
        MYFIRST_UNLOCK(sc);
        return (EIO);
}
```

El softc añade una marca `broken`, y cualquier petición del usuario comprueba esa marca y falla con un error. La ruta de detach sigue funcionando (detach siempre tiene éxito, incluso en un dispositivo defectuoso), así que el usuario puede descargar el driver y volver a cargarlo.

Un driver real que detecta un reinicio tiene más opciones. Un driver de red podría volver a ejecutar su secuencia de attach desde el punto posterior a `pci_alloc_msi` (que ha sido restaurado por la capa PCI). Un driver de almacenamiento podría reinicializar su controlador usando el mismo camino de código que utilizó attach. La implementación depende en gran medida del dispositivo; el patrón es «detectar y luego realizar toda la inicialización de attach que aún sea necesaria».

El driver `myfirst` del capítulo adopta el enfoque más sencillo: no implementa la detección de reinicio para la simulación, y la ruta de resume no incluye la llamada de validación por defecto. El código anterior se proporciona como referencia para el lector que quiera extender el driver como ejercicio.

### Restaurar el estado DMA

La configuración DMA del capítulo 21 asigna un tag, reserva memoria, carga el mapa y retiene la dirección de bus en el softc. Nada de eso es visible en el mapa de registros local del BAR; el motor DMA aprende la dirección de bus solo cuando el driver la escribe en `DMA_ADDR_LOW` y `DMA_ADDR_HIGH` como parte del inicio de una transferencia.

Esto significa que el estado DMA no necesita restauración en el sentido de «escribir registros». El tag, el mapa y la memoria son estructuras de datos del lado del kernel; sobreviven al suspend intactos. La siguiente transferencia programará los registros DMA como parte de su envío normal.

Lo que podría necesitar restauración en un dispositivo real es:

- **La dirección base del anillo de descriptores DMA**, si el dispositivo mantiene un puntero persistente. Una NIC real escribe un registro de dirección base una vez durante attach y apunta el dispositivo a un anillo de descriptores; tras D3cold, ese registro puede haberse restablecido y el driver debe reprogramarlo.
- **El bit de habilitación del motor DMA**, si es independiente de las transferencias individuales.
- **Cualquier configuración por canal** (tamaño de ráfaga, prioridad, etc.) que se almacene en registros que la capa PCI no haya guardado en caché.

Para `myfirst`, nada de esto aplica. El motor DMA se programa por transferencia. El resume no necesita ninguna restauración específica de DMA más allá de lo que ya cubre la restauración genérica del estado.

### Rearmar las interrupciones

Enmascarar las interrupciones fue el paso 2 del suspend. Desenmascarar las interrupciones es el paso 3 del resume. El resume de la etapa 3 vuelve a escribir `saved_intr_mask` en el registro `INTR_MASK`, lo cual (por convención) escribe 0 en los bits correspondientes a los vectores habilitados y 1 en los bits de los vectores deshabilitados. Tras la escritura, el dispositivo está listo para generar interrupciones en los vectores habilitados en cuanto haya motivo.

Existe una sutileza en cuanto al orden. La ruta de resume desenmascara las interrupciones antes de borrar la marca `suspended`. Esto significa que podría llegar una interrupción en un momento muy inoportuno, llamar al filtro y encontrar `suspended == true`. El filtro se negaría a procesarla y devolvería `FILTER_STRAY`, lo que dejaría la interrupción aún activa.

Para evitarlo, la ruta de resume adquiere el lock del softc alrededor del cambio de estado y realiza el desenmascarado y el borrado de la marca en el orden inverso: borra `suspended` primero y luego desenmascara. De este modo, cualquier interrupción que el dispositivo genere tras borrar la máscara encontrará `suspended == false` y se procesará con normalidad.

El código del fragmento anterior lo hace correctamente: `myfirst_restore` escribe la máscara, luego adquiere el lock, borra la marca y lo libera. El orden importa; invertirlo crea una ventana estrecha en la que las interrupciones podrían perderse.

### Limpiar la fuente de despertar

Si el driver habilitó una fuente de despertar durante suspend (`pci_enable_pme`), la ruta de resume debe limpiar cualquier evento de despertar pendiente (`pci_clear_pme`). El helper `pci_resume_child` de la capa PCI ya llama a `pci_clear_pme(child)` antes del `DEVICE_RESUME` del driver, por lo que el driver normalmente no necesita volver a llamarlo.

El único caso en que el driver podría querer llamar a `pci_clear_pme` explícitamente es en un contexto de runtime-PM, en el que el driver reanuda el dispositivo mientras el sistema permanece en S0. En ese caso, `pci_resume_child` no interviene y el driver es el responsable de limpiar el estado PME.

Un esbozo hipotético para un driver con wake-on-X:

```c
static int
myfirst_pci_resume(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        if (pci_has_pm(dev))
                pci_clear_pme(dev);  /* defensive; PCI layer already did this */

        /* ... rest of the resume path ... */
}
```

Para `myfirst`, no hay fuente de despertar, por lo que la llamada no haría nada útil; el capítulo la omite del código principal y menciona el patrón aquí a modo de referencia.

### Actualizar el driver de la etapa 3

La etapa 3 reúne todo lo anterior en un resume funcional. Los cambios respecto a la etapa 2 son:

- `myfirst.h` añade un campo `saved_intr_mask` (incorporado en la etapa 2) y una marca `broken`.
- `myfirst_pci.c` incorpora el helper `myfirst_restore` y una versión reescrita de `myfirst_pci_resume`.
- La versión del Makefile sube a `1.5-power-stage3`.

Compila y prueba:

```sh
cd /path/to/driver
make clean && make
sudo kldunload myfirst     # unload any previous version
sudo kldload ./myfirst.ko

# Quiet baseline.
sysctl dev.myfirst.0.dma_transfers_read
# 0
sysctl dev.myfirst.0.suspended
# 0

# Full cycle.
sudo devctl suspend myfirst0
sysctl dev.myfirst.0.suspended
# 1

sudo devctl resume myfirst0
sysctl dev.myfirst.0.suspended
# 0

# A transfer after resume should work.
sudo sysctl dev.myfirst.0.dma_test_read=1
sysctl dev.myfirst.0.dma_transfers_read
# 1

# Do it several times to make sure the path is stable.
for i in 1 2 3 4 5; do
  sudo devctl suspend myfirst0
  sudo devctl resume myfirst0
  sudo sysctl dev.myfirst.0.dma_test_read=1
done
sysctl dev.myfirst.0.dma_transfers_read
# 6 (1 + 5)

sysctl dev.myfirst.0.power_suspend_count dev.myfirst.0.power_resume_count
# should be equal, around 6 each
```

Si los contadores divergen (el contador de suspend no es igual al de resume) o si `dma_test_read` empieza a fallar tras un suspend, algo en la ruta de restauración no está devolviendo el dispositivo a un estado utilizable. El primer paso de depuración es leer INTR_MASK y compararlo con `saved_intr_mask`; el segundo es rastrear el registro de estado del motor DMA y comprobar si está reportando un error.

### Interacción con la configuración MSI-X del capítulo 20

El driver `myfirst` del capítulo 20 usa MSI-X cuando está disponible, con una distribución de tres vectores (admin, rx, tx). La configuración MSI-X reside en los registros de capacidad MSI-X del dispositivo y en una tabla del lado del kernel. El guardado y restauración del espacio de configuración de la capa PCI cubre los registros de capacidad; el estado del lado del kernel no se ve afectado por la transición de estado D.

Esto significa que el driver `myfirst` no necesita hacer nada especial para restaurar sus vectores MSI-X. Los recursos de interrupción (`irq_res`) permanecen asignados, las cookies permanecen registradas y los enlaces de CPU permanecen activos. Cuando el dispositivo genera un vector MSI-X en el resume, el kernel lo entrega al filtro que se registró durante attach.

Un lector que quiera verificar esto puede escribir en uno de los sysctls de simulación tras el resume y observar que el contador por vector correspondiente se incrementa:

```sh
sudo devctl suspend myfirst0
sudo devctl resume myfirst0
sudo sysctl dev.myfirst.0.intr_simulate_admin=1
sysctl dev.myfirst.0.vec0_fire_count
# should be incremented
```

Si el contador no se incrementa, la ruta MSI-X se ha visto alterada. La causa más probable es un error en la gestión de estado del propio driver (el flag `suspended` no se limpió, o el filtro está rechazando la interrupción por otra razón). La sección de resolución de problemas del capítulo contiene más detalles.

### Gestión controlada de un fallo en el resume

Si algún paso del resume falla, el driver tiene opciones limitadas. No puede vetar el resume (el kernel no dispone de una ruta de reversión en este punto). Normalmente tampoco puede reintentar (el estado del hardware es incierto). Lo mejor que puede hacer es:

1. Registrar el fallo de forma prominente con `device_printf` para que el usuario lo vea en dmesg.
2. Incrementar un contador (`power_resume_errors`) que un script de regresión o una herramienta de observabilidad pueda consultar.
3. Marcar el dispositivo como roto para que las solicitudes posteriores fallen de forma limpia en lugar de corromper datos silenciosamente.
4. Mantener el driver asociado, de modo que el estado del árbol de dispositivos permanezca consistente y el usuario pueda finalmente descargar y recargar el driver.
5. Devolver 0 desde `DEVICE_RESUME`, porque el kernel espera que tenga éxito.

El patrón "marcar como roto, mantener asociado" es habitual en los drivers de producción. Convierte el fallo de "corrupción misteriosa posterior" en "error visible por el usuario de forma inmediata", lo que mejora la experiencia de depuración.

### Un breve desvío: pci_save_state / pci_restore_state en la gestión de energía en tiempo de ejecución

La sección 2 mencionó que `pci_save_state` y `pci_restore_state` a veces los llama el propio driver, típicamente desde un helper de gestión de energía en tiempo de ejecución. Vale la pena esbozar un ejemplo concreto antes de que la sección 5 lo desarrolle en detalle.

Un helper de gestión de energía en tiempo de ejecución que pone un dispositivo inactivo en D3 tiene este aspecto:

```c
static int
myfirst_runtime_suspend(struct myfirst_softc *sc)
{
        int err;

        err = myfirst_quiesce(sc);
        if (err != 0)
                return (err);

        pci_save_state(sc->dev);
        err = pci_set_powerstate(sc->dev, PCI_POWERSTATE_D3);
        if (err != 0) {
                /* roll back */
                pci_restore_state(sc->dev);
                myfirst_restore(sc);
                return (err);
        }

        return (0);
}

static int
myfirst_runtime_resume(struct myfirst_softc *sc)
{
        int err;

        err = pci_set_powerstate(sc->dev, PCI_POWERSTATE_D0);
        if (err != 0)
                return (err);
        pci_restore_state(sc->dev);

        return (myfirst_restore(sc));
}
```

El patrón es similar al del suspend/resume del sistema, pero utiliza los helpers PCI explícitos porque la capa PCI no está involucrada en el proceso. La sección 5 convertirá este esbozo en una implementación real y lo conectará a una política de detección de inactividad.

### Contraste con un driver real

Antes de continuar, conviene hacer una pausa y observar la ruta de resume de un driver real. La función `re_resume` de `/usr/src/sys/dev/re/if_re.c` tiene unas treinta líneas. Su estructura es la siguiente:

1. Adquirir el lock del softc.
2. Si hay una bandera de MAC-sleep activa, sacar el chip del modo sleep escribiendo en un registro GPIO.
3. Borrar cualquier patrón de wake-on-LAN para que el filtrado normal de recepción no se vea interferido.
4. Si la interfaz está administrativamente activa, reinicializarla mediante `re_init_locked`.
5. Borrar la bandera `suspended`.
6. Liberar el lock del softc.
7. Devolver 0.

La llamada a `re_init_locked` es el trabajo sustancial: reprograma la dirección MAC, reinicia los anillos de descriptores de recepción y transmisión, reactiva las interrupciones en la NIC y arranca los motores DMA. Para `myfirst`, el trabajo equivalente es mucho más corto porque el dispositivo es mucho más sencillo, pero la forma es la misma: adquirir estado, realizar la reinicialización específica del hardware, liberar el lock, devolver.

Un lector que lea `re_resume` después de implementar el resume de `myfirst` reconocerá la estructura de inmediato. El vocabulario es el mismo; solo difieren los detalles.

### Cerrando la sección 4

La sección 4 completó la ruta de resume. Mostró qué ha hecho ya la capa PCI cuando se llama a `DEVICE_RESUME` (transición a D0, restauración del espacio de configuración, limpieza de PME#, restauración de MSI-X), qué tiene que hacer todavía el driver (reactivar el bus-master, restaurar los registros específicos del dispositivo, borrar la bandera suspended, desenmascarar las interrupciones) y por qué es importante cada paso. El driver de la etapa 3 ya puede realizar un ciclo completo de suspend-resume y continuar operando con normalidad; el test de regresión puede ejecutar varios ciclos seguidos y verificar que los contadores son consistentes.

Con las secciones 3 y 4 juntas, el driver es consciente de la energía en el sentido del suspend del sistema: gestiona las transiciones S3 y S4 de forma limpia. Lo que todavía no hace es ahorrar energía a nivel de dispositivo mientras el sistema está en funcionamiento. Eso es la gestión de energía en tiempo de ejecución, y la sección 5 la explica.



## Sección 5: Gestión de la energía en tiempo de ejecución

El suspend del sistema es una transición grande y visible: se cierra la tapa, la pantalla se apaga y la batería ahorra energía durante horas. La gestión de energía en tiempo de ejecución es lo contrario: docenas de pequeñas transiciones invisibles por segundo, cada una ahorrando un poco, y juntas ahorrando gran parte de la energía que un sistema moderno consume en reposo. El usuario nunca las nota; el ingeniero de plataforma vive o muere en función de su corrección.

Esta sección está marcada como opcional en el esquema del capítulo porque no todos los drivers necesitan runtime PM. Un driver para un dispositivo siempre activo (una NIC en un servidor ocupado, un controlador de disco para el sistema de archivos raíz) no ahorra energía intentando suspender su dispositivo; el dispositivo está ocupado, y tratar de suspenderlo malgasta ciclos configurando transiciones que nunca se completan. Un driver para un dispositivo frecuentemente inactivo (una webcam, un lector de huellas dactilares, una tarjeta WLAN en un portátil) sí se beneficia. Añadir o no runtime PM es una decisión de política determinada por el perfil de uso del dispositivo.

Para el capítulo 22, implementamos runtime PM en el driver `myfirst` como ejercicio de aprendizaje. El dispositivo ya es simulado; podemos simular que está inactivo siempre que no se haya escrito en ningún sysctl en los últimos segundos, y observar cómo el driver realiza las transiciones. La implementación es corta y enseña las primitivas de nivel PCI que usa un driver real de runtime PM.

### Qué significa el runtime PM en FreeBSD

FreeBSD no dispone actualmente de un framework centralizado de runtime PM como el que tiene Linux. No existe ningún mecanismo en el kernel del tipo "si el dispositivo ha estado inactivo N milisegundos, llama a su hook idle". En cambio, el runtime PM es local al driver: el driver decide cuándo suspender y reanudar su dispositivo, usando las mismas primitivas de la capa PCI (`pci_set_powerstate`, `pci_save_state`, `pci_restore_state`) que usaría dentro de `DEVICE_SUSPEND` y `DEVICE_RESUME`.

Esto tiene dos consecuencias. Primera, cada driver que quiere runtime PM implementa su propia política: cuánto tiempo debe estar inactivo el dispositivo antes de suspenderse, qué cuenta como inactividad, con qué rapidez debe despertar el dispositivo cuando se le demanda. Segunda, el driver debe integrar su runtime PM con su PM del sistema; ambas rutas comparten mucho código y no deben interferir entre sí.

El patrón que utiliza el capítulo 22 es sencillo:

1. El driver añade una pequeña máquina de estados con los estados `RUNNING` y `RUNTIME_SUSPENDED`.
2. Cuando el driver detecta inactividad (la sección 5 usa una política basada en callout de "ninguna solicitud en los últimos 5 segundos"), llama a `myfirst_runtime_suspend`.
3. Cuando el driver detecta una nueva solicitud estando en `RUNTIME_SUSPENDED`, llama a `myfirst_runtime_resume` antes de procesar la solicitud.
4. En el suspend del sistema, si el dispositivo está en `RUNTIME_SUSPENDED`, la ruta de suspend del sistema se adapta a ello (el dispositivo ya está inactivo; el quiesce del suspend del sistema es un no-op, pero el resume del sistema tiene que volver a llevar el dispositivo a D0).
5. En el resume del sistema, el driver vuelve a `RUNNING` a menos que haya estado explícitamente en runtime-suspended y quiera permanecer así.

Esto es más sencillo que el framework de runtime PM de Linux, que tiene conceptos más ricos (conteo de referencias padre/hijo, temporizadores de autosuspensión, barreras). Para un driver único en hardware simple, el enfoque de FreeBSD es suficiente.

### La máquina de estados del runtime PM

El softc incorpora una variable de estado y una marca de tiempo:

```c
enum myfirst_runtime_state {
        MYFIRST_RT_RUNNING = 0,
        MYFIRST_RT_SUSPENDED = 1,
};

struct myfirst_softc {
        /* ... */
        enum myfirst_runtime_state runtime_state;
        struct timeval             last_activity;
        struct callout             idle_watcher;
        int                        idle_threshold_seconds;
        uint64_t                   runtime_suspend_count;
        uint64_t                   runtime_resume_count;
};
```

El `idle_threshold_seconds` es un parámetro de política expuesto a través de un sysctl; el valor predeterminado de cinco segundos permite una observabilidad rápida sin ser tan agresivo como para provocar activaciones innecesarias durante el uso normal. Un driver de producción ajustaría este valor por dispositivo; cinco segundos es un valor cómodo para el aprendizaje que hace visibles las transiciones sin necesidad de esperar horas.

El callout `idle_watcher` se dispara una vez por segundo para comprobar el tiempo de inactividad. Si el dispositivo ha estado inactivo más tiempo del que indica `idle_threshold_seconds` y se encuentra en `RUNNING`, el callout activa `myfirst_runtime_suspend`.

### Implementación

La ruta de attach arranca el observador de inactividad:

```c
static void
myfirst_start_idle_watcher(struct myfirst_softc *sc)
{
        sc->idle_threshold_seconds = 5;
        microtime(&sc->last_activity);
        callout_init_mtx(&sc->idle_watcher, &sc->mtx, 0);
        callout_reset(&sc->idle_watcher, hz, myfirst_idle_watcher_cb, sc);
}
```

El callout se inicializa con el mutex del softc, de modo que lo adquiere automáticamente al dispararse. Esto simplifica el callback: se ejecuta bajo el lock.

El callback comprueba el tiempo desde la última actividad y suspende si es necesario:

```c
static void
myfirst_idle_watcher_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct timeval now, diff;

        MYFIRST_ASSERT_LOCKED(sc);

        if (sc->runtime_state == MYFIRST_RT_RUNNING && !sc->suspended) {
                microtime(&now);
                timersub(&now, &sc->last_activity, &diff);

                if (diff.tv_sec >= sc->idle_threshold_seconds) {
                        /*
                         * Release the lock while suspending. The
                         * runtime_suspend helper acquires it again as
                         * needed.
                         */
                        MYFIRST_UNLOCK(sc);
                        (void)myfirst_runtime_suspend(sc);
                        MYFIRST_LOCK(sc);
                }
        }

        /* Reschedule. */
        callout_reset(&sc->idle_watcher, hz, myfirst_idle_watcher_cb, sc);
}
```

Hay que tener en cuenta la liberación del lock en torno a `myfirst_runtime_suspend`. El helper de suspensión llama a `myfirst_quiesce`, que adquiere el lock por sí mismo. Mantener el lock durante esa llamada provocaría un deadlock.

La actividad se registra cada vez que el driver atiende una solicitud. La ruta DMA del capítulo 21 es un buen punto de enganche: cada vez que un usuario escribe en `dma_test_read` o `dma_test_write`, el handler del sysctl registra la actividad:

```c
static int
myfirst_dma_sysctl_test_write(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        /* ... existing code ... */

        /* Mark the device active before processing. */
        myfirst_mark_active(sc);

        /* If runtime-suspended, bring the device back before running. */
        if (sc->runtime_state == MYFIRST_RT_SUSPENDED) {
                int err = myfirst_runtime_resume(sc);
                if (err != 0)
                        return (err);
        }

        /* ... proceed with the transfer ... */
}
```

El helper `myfirst_mark_active` es una función de una sola línea:

```c
static void
myfirst_mark_active(struct myfirst_softc *sc)
{
        MYFIRST_LOCK(sc);
        microtime(&sc->last_activity);
        MYFIRST_UNLOCK(sc);
}
```

### Los helpers de runtime-suspend y runtime-resume

Estos fueron esbozados en la sección 4. Aquí están las versiones completas:

```c
static int
myfirst_runtime_suspend(struct myfirst_softc *sc)
{
        int err;

        device_printf(sc->dev, "runtime suspend: starting\n");

        err = myfirst_quiesce(sc);
        if (err != 0) {
                device_printf(sc->dev,
                    "runtime suspend: quiesce failed (err %d)\n", err);
                /* Undo the suspended flag the quiesce set. */
                MYFIRST_LOCK(sc);
                sc->suspended = false;
                MYFIRST_UNLOCK(sc);
                return (err);
        }

        pci_save_state(sc->dev);
        err = pci_set_powerstate(sc->dev, PCI_POWERSTATE_D3);
        if (err != 0) {
                device_printf(sc->dev,
                    "runtime suspend: set_powerstate(D3) failed "
                    "(err %d)\n", err);
                pci_restore_state(sc->dev);
                myfirst_restore(sc);
                return (err);
        }

        MYFIRST_LOCK(sc);
        sc->runtime_state = MYFIRST_RT_SUSPENDED;
        MYFIRST_UNLOCK(sc);
        atomic_add_64(&sc->runtime_suspend_count, 1);

        device_printf(sc->dev, "runtime suspend: device in D3\n");
        return (0);
}

static int
myfirst_runtime_resume(struct myfirst_softc *sc)
{
        int err;

        MYFIRST_LOCK(sc);
        if (sc->runtime_state != MYFIRST_RT_SUSPENDED) {
                MYFIRST_UNLOCK(sc);
                return (0);  /* nothing to do */
        }
        MYFIRST_UNLOCK(sc);

        device_printf(sc->dev, "runtime resume: starting\n");

        err = pci_set_powerstate(sc->dev, PCI_POWERSTATE_D0);
        if (err != 0) {
                device_printf(sc->dev,
                    "runtime resume: set_powerstate(D0) failed "
                    "(err %d)\n", err);
                return (err);
        }
        pci_restore_state(sc->dev);

        err = myfirst_restore(sc);
        if (err != 0) {
                device_printf(sc->dev,
                    "runtime resume: restore failed (err %d)\n", err);
                return (err);
        }

        MYFIRST_LOCK(sc);
        sc->runtime_state = MYFIRST_RT_RUNNING;
        MYFIRST_UNLOCK(sc);
        atomic_add_64(&sc->runtime_resume_count, 1);

        device_printf(sc->dev, "runtime resume: device in D0\n");
        return (0);
}
```

La forma es idéntica a la del suspend/resume del sistema, salvo que el driver llama explícitamente a `pci_set_powerstate` y `pci_save_state`/`pci_restore_state`. Las transiciones automáticas de la capa PCI no intervienen en el runtime PM porque el kernel no coordina un cambio de energía a nivel de todo el sistema; el driver actúa por su cuenta.

### Interacción entre el runtime PM y el PM del sistema

Ambas rutas tienen que cooperar. Considera qué ocurre si el dispositivo está en runtime-suspended (en D3) cuando el usuario cierra la tapa del portátil:

1. El kernel inicia el suspend del sistema.
2. El bus PCI llama a `myfirst_pci_suspend`.
3. Dentro de `myfirst_pci_suspend`, el driver detecta que el dispositivo ya está en runtime-suspended. El quiesce es un no-op (no está ocurriendo nada). El guardado automático del espacio de configuración por parte de la capa PCI se ejecuta; lee el espacio de configuración (que sigue siendo accesible en D3) y lo almacena en caché.
4. La capa PCI hace la transición del dispositivo de D3 a... espera, ya está en D3. La transición a D3 es un no-op.
5. El sistema entra en reposo.
6. Al despertar, la capa PCI hace la transición del dispositivo de vuelta a D0. Se ejecuta `myfirst_pci_resume`. El driver restaura el estado. Pero ahora el driver cree que el dispositivo está en `RUNNING` (porque el resume del sistema borró la bandera `suspended`), mientras que conceptualmente estaba en runtime-suspended antes. La siguiente actividad usará el dispositivo con normalidad y establecerá `last_activity`; el observador de inactividad lo suspenderá de nuevo eventualmente si sigue inactivo.

La interacción es en gran medida inocua; lo peor que puede ocurrir es que el dispositivo realice un viaje adicional por D0 antes de que el observador de inactividad lo suspenda de nuevo. Una implementación más pulida recordaría el estado de runtime-suspended durante el suspend del sistema y lo restauraría, pero para un driver de aprendizaje el enfoque simple es suficiente.

El caso inverso (suspender el sistema con un dispositivo que ya está en runtime-suspended) ya es correcto en nuestra implementación porque `myfirst_quiesce` comprueba `suspended` y devuelve 0 si ya está establecida. La ruta de runtime-suspended estableció `suspended = true` como parte de su quiesce, por lo que el quiesce del suspend del sistema ve la bandera y la omite.

### Exposición de los controles del runtime PM mediante sysctl

La política de runtime PM del driver se puede controlar y observar mediante sysctls:

```c
SYSCTL_ADD_INT(ctx, kids, OID_AUTO, "idle_threshold_seconds",
    CTLFLAG_RW, &sc->idle_threshold_seconds, 0,
    "Runtime PM idle threshold (seconds)");
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "runtime_suspend_count",
    CTLFLAG_RD, &sc->runtime_suspend_count, 0,
    "Runtime suspends performed");
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "runtime_resume_count",
    CTLFLAG_RD, &sc->runtime_resume_count, 0,
    "Runtime resumes performed");
SYSCTL_ADD_INT(ctx, kids, OID_AUTO, "runtime_state",
    CTLFLAG_RD, (int *)&sc->runtime_state, 0,
    "Runtime state: 0=running, 1=suspended");
```

Ahora puedes hacer esto:

```sh
# Watch the device idle out.
while :; do
        sysctl dev.myfirst.0.runtime_state dev.myfirst.0.runtime_suspend_count
        sleep 1
done &
```

Tras cinco segundos de inactividad, `runtime_state` pasa de 0 a 1 y `runtime_suspend_count` se incrementa. Una escritura en cualquier sysctl activo activa un resume y vuelve a cambiar el estado:

```sh
sudo sysctl dev.myfirst.0.dma_test_read=1
# The log shows: runtime resume, then the test read
```

### Compensaciones

El runtime PM intercambia latencia de activación por energía en reposo. Cada transición de D3 a D0 tiene un coste de tiempo (decenas de microsegundos en un enlace PCIe, incluyendo la salida de ASPM) y, en algunos dispositivos, un coste de energía (la propia transición consume corriente). Para un dispositivo que está inactivo la mayor parte del tiempo con ráfagas de actividad poco frecuentes, el intercambio es favorable. Para un dispositivo que está activo la mayor parte del tiempo con períodos de inactividad poco frecuentes, el coste de las transiciones domina.

El parámetro `idle_threshold_seconds` permite ajustar esto según la plataforma. Un valor de 0 o 1 es agresivo y adecuado para una webcam que se usa durante segundos y permanece inactiva durante minutos. Un valor de 60 es conservador y adecuado para una NIC cuyos períodos de inactividad son cortos pero frecuentes. Un valor de 0 (si se permite) desactivaría el runtime PM por completo, lo que es apropiado para dispositivos que deben permanecer siempre encendidos.

La segunda contrapartida está en la complejidad del código. El runtime PM añade una máquina de estados, un callout, un monitor de inactividad, dos helpers adicionales de tipo kobj, y consideraciones de orden adicionales entre las rutas de runtime PM y de PM del sistema. Cada uno de ellos es pequeño por sí solo, pero en conjunto aumentan la superficie expuesta a bugs. Muchos drivers de FreeBSD omiten deliberadamente el runtime PM por esta razón; dejan el dispositivo en `D0` y confían en los estados internos de bajo consumo del dispositivo (clock gating, PCIe ASPM) para ahorrar energía. Es una elección defendible y, en los drivers donde la corrección importa más que los milivatios, es la elección adecuada.

El driver `myfirst` del Capítulo 22 mantiene el runtime PM como una característica opcional, controlada por un flag de compilación:

```make
CFLAGS+= -DMYFIRST_ENABLE_RUNTIME_PM
```

El lector puede compilar con o sin el flag; el código de la Sección 5 solo se compila cuando el flag está definido. El comportamiento predeterminado en la Etapa 3 es dejar el runtime PM desactivado; la Etapa 4 lo activa en el driver consolidado.

### Una nota sobre el runtime PM en plataformas

Algunas plataformas proporcionan su propio mecanismo de runtime PM junto al que ofrece el propio driver. En sistemas embebidos arm64 y RISC-V, el árbol de dispositivos puede describir propiedades `power-domains` y `clocks` que el driver utiliza para apagar dominios de energía y desactivar relojes. Los subsistemas `ext_resources/clk`, `ext_resources/regulator` y `ext_resources/power` de FreeBSD se encargan de esto.

El runtime PM en este tipo de plataformas es más capaz que el runtime PM exclusivo de PCI, porque la plataforma puede apagar bloques enteros de SoC (un controlador USB, un motor de visualización, una GPU) en lugar de limitarse a llevar el dispositivo PCI a D3. El driver utiliza el mismo patrón (marcar como inactivo, apagar recursos al estar inactivo, volver a encenderlos para la actividad), pero a través de APIs diferentes.

El capítulo 22 se ciñe a la ruta PCI porque ahí es donde vive el driver `myfirst`. Un lector que trabaje más adelante con una plataforma embebida encontrará la misma estructura conceptual con APIs específicas de la plataforma. El capítulo menciona esta distinción aquí para que el lector sepa que ese territorio existe.

### Cerrando la sección 5

La sección 5 añadió la gestión de energía en tiempo de ejecución al driver. Definió una máquina de dos estados (`RUNNING`, `RUNTIME_SUSPENDED`), un observador de inactividad basado en callout, un par de funciones auxiliares (`myfirst_runtime_suspend`, `myfirst_runtime_resume`) que utilizan las APIs de estado de energía explícito y guardado de estado de la capa PCI, los hooks de registro de actividad en los manejadores de sysctl de DMA, y los controles sysctl que exponen la política al espacio de usuario. También abordó la interacción entre el runtime PM y el PM del sistema, el compromiso entre latencia y consumo energético, y la alternativa del runtime PM a nivel de plataforma en sistemas embebidos.

Con las secciones 2 a 5 en su lugar, el driver ya gestiona la suspensión del sistema, la reanudación del sistema, el apagado del sistema, la suspensión en tiempo de ejecución y la reanudación en tiempo de ejecución. Lo que todavía no hace de forma completa es mostrar cómo el lector puede probar todo esto desde el espacio de usuario. La sección 6 se dedica a la interfaz del espacio de usuario: `acpiconf`, `zzz`, `devctl suspend`, `devctl resume`, `devinfo -v`, y el test de regresión que los combina.



## Sección 6: Interacción con el framework de energía

Un driver que gestiona la suspensión y la reanudación de forma correcta es solo la mitad de lo que se necesita. La otra mitad consiste en poder *probar* esa corrección, de manera repetida y deliberada, desde el espacio de usuario. La sección 6 repasa las herramientas que FreeBSD ofrece con ese propósito, explica cómo encaja cada una en el modelo de estados del driver y muestra cómo combinarlas en un script de regresión que ejercita cada ruta que las secciones 2 a 5 construyeron.

### Los cuatro puntos de entrada del espacio de usuario

Cuatro comandos cubren casi todo lo que necesita un desarrollador de drivers:

- **`acpiconf -s 3`** (y sus variantes) solicita a ACPI que lleve todo el sistema al estado de suspensión S3. Esta es la prueba más realista; ejercita la ruta completa desde el espacio de usuario, a través de la maquinaria de suspensión del kernel, la capa PCI, hasta los métodos del driver.
- **`zzz`** es un ligero envoltorio sobre `acpiconf -s 3`. Lee `hw.acpi.suspend_state` (con S3 como valor por defecto) y entra en el estado de suspensión correspondiente. Para la mayoría de los usuarios es la forma más cómoda de suspender desde una shell.
- **`devctl suspend myfirst0`** y **`devctl resume myfirst0`** desencadenan la suspensión y reanudación por dispositivo a través de los ioctls `DEV_SUSPEND` y `DEV_RESUME` en `/dev/devctl2`. Estos solo invocan los métodos del driver; el resto del sistema permanece en S0. Es el objetivo de iteración más rápido y el que el capítulo 22 utiliza para la mayor parte del desarrollo.
- **`devinfo -v`** lista todos los dispositivos del árbol de dispositivos con su estado actual. Muestra si un dispositivo está conectado, suspendido o desconectado.

Cada uno tiene ventajas e inconvenientes. `acpiconf` es realista pero lento (entre uno y tres segundos por ciclo en hardware típico) y perturbador (el sistema duerme de verdad). `devctl` es rápido (milisegundos por ciclo) pero solo ejercita el driver, no el código de ACPI ni el de la plataforma. `devinfo -v` es pasivo y económico; observa sin cambiar el estado.

Una buena estrategia de regresión utiliza los tres: `devctl` para las pruebas unitarias de los métodos del driver, `acpiconf` para las pruebas de integración de la ruta completa de suspensión, y `devinfo -v` como comprobación rápida de cordura.

### Uso de acpiconf para suspender el sistema

En una máquina con ACPI funcional, `acpiconf -s 3` es lo que la sección 1 denominó suspensión completa del sistema. El comando:

```sh
sudo acpiconf -s 3
```

hace lo siguiente:

1. Abre `/dev/acpi` y comprueba que la plataforma admite S3 mediante el ioctl `ACPIIO_ACKSLPSTATE`.
2. Envía el ioctl `ACPIIO_REQSLPSTATE` para solicitar S3.
3. El kernel inicia la secuencia de suspensión: userland pausado, threads congelados, recorrido del árbol de dispositivos con `DEVICE_SUSPEND` en cada dispositivo.
4. Si ningún driver veta la operación, el kernel entra en S3. La máquina se duerme.
5. Un evento de activación (se abre la tapa, se pulsa el botón de encendido, un dispositivo USB envía una señal de remote-wakeup) despierta la plataforma.
6. El kernel ejecuta la secuencia de reanudación: `DEVICE_RESUME` en cada dispositivo, descongelación de threads, reanudación del userland.
7. El prompt de la shell regresa. La máquina vuelve a estar en S0.

Para que el driver `myfirst` sea ejercitado, el driver debe estar cargado antes de la suspensión. La secuencia completa desde la perspectiva del usuario tiene este aspecto:

```sh
sudo kldload ./myfirst.ko
sudo sysctl dev.myfirst.0.dma_test_read=1  # exercise it a bit
sudo acpiconf -s 3
# [laptop sleeps; user opens lid]
dmesg | grep myfirst
```

La salida de `dmesg` debería mostrar dos líneas del registro del capítulo 22:

```text
myfirst0: suspend: starting
myfirst0: suspend: complete (dma in flight=0, suspended=1)
myfirst0: resume: starting
myfirst0: resume: complete
```

Si esas líneas están presentes y en ese orden, los métodos del driver fueron invocados correctamente por la ruta completa del sistema.

Si la máquina no regresa, la ruta de suspensión falló en alguna capa por debajo de `myfirst`. Si la máquina regresa pero el driver está en un estado extraño (los sysctls devuelven errores, los contadores tienen valores inesperados, las transferencias DMA fallan), el problema está en la implementación de suspensión o reanudación de `myfirst`.

### Uso de zzz

En FreeBSD, `zzz` es un pequeño script de shell que lee `hw.acpi.suspend_state` y llama a `acpiconf -s <state>`. No es un binario; generalmente se instala en `/usr/sbin/zzz` y tiene unas pocas líneas. Una invocación típica es:

```sh
sudo zzz
```

El valor por defecto de `hw.acpi.suspend_state` es `S3` en las máquinas que lo admiten. Un lector que quiera probar S4 (hibernación) puede hacer lo siguiente:

```sh
sudo sysctl hw.acpi.suspend_state=S4
sudo zzz
```

El soporte de S4 en FreeBSD ha sido históricamente parcial; si funciona o no depende del firmware de la plataforma y de la disposición del sistema de archivos. Para los fines del capítulo 22, S3 es suficiente, y `zzz` es la abreviatura más cómoda.

### Uso de devctl para la suspensión por dispositivo

El comando `devctl(8)` fue creado para permitir que un usuario manipule el árbol de dispositivos desde el espacio de usuario. Admite las operaciones attach, detach, enable, disable, suspend, resume y otras. Para el capítulo 22, `suspend` y `resume` son las dos que importan.

```sh
sudo devctl suspend myfirst0
sudo devctl resume myfirst0
```

El primer comando emite `DEV_SUSPEND` a través de `/dev/devctl2`; el kernel lo traduce en una llamada a `BUS_SUSPEND_CHILD` en el bus padre, que para un dispositivo PCI acaba llamando a `pci_suspend_child`, el cual guarda el espacio de configuración, pone el dispositivo en D3 y llama al `DEVICE_SUSPEND` del driver. Lo inverso ocurre para la reanudación.

Las diferencias clave respecto a `acpiconf`:

- Solo el dispositivo objetivo y sus hijos pasan por la transición. El resto del sistema permanece en S0.
- La CPU no se detiene. El userland no se congela. El kernel no duerme.
- El dispositivo PCI pasa realmente a D3hot (asumiendo que `hw.pci.do_power_suspend` vale 1). El lector puede verificarlo con `pciconf`:

```sh
# Before suspend: device should be in D0
pciconf -lvbc | grep -A 2 myfirst

# After devctl suspend myfirst0: device should be in D3
sudo devctl suspend myfirst0
pciconf -lvbc | grep -A 2 myfirst
```

El estado de energía suele mostrarse en la línea `powerspec` de `pciconf -lvbc`. Pasar de `D0` a `D3` es la señal observable de que la transición realmente se produjo.

### Uso de devinfo para inspeccionar el estado del dispositivo

La utilidad `devinfo(8)` lista el árbol de dispositivos con distintos niveles de detalle. El indicador `-v` muestra información detallada, incluido el estado del dispositivo (conectado, suspendido o no presente).

```sh
devinfo -v | grep -A 5 myfirst
```

Salida típica:

```text
myfirst0 pnpinfo vendor=0x1af4 device=0x1005 subvendor=0x1af4 subdevice=0x0004 class=0x008880 at slot=5 function=0 dbsf=pci0:0:5:0
    Resource: <INTERRUPT>
        10
    Resource: <MEMORY>
        0xfeb80000-0xfeb80fff
```

El estado es implícito en la salida: si el dispositivo está suspendido, la línea muestra el dispositivo y sus recursos sin el marcador "active". Se puede hacer una consulta explícita del estado a través del sysctl del softc; las claves `dev.myfirst.0.%parent` y `dev.myfirst.0.%desc` indican al usuario dónde se encuentra el dispositivo en el árbol.

Para el capítulo 22, `devinfo -v` es más útil como comprobación de cordura tras una transición fallida: si el dispositivo no aparece en la salida, se ejecutó la ruta de detach; si el dispositivo está presente pero los recursos son incorrectos, la ruta de attach o de reanudación dejó el dispositivo en un estado inconsistente.

### Inspección de estados de energía mediante sysctl

La capa PCI expone información sobre el estado de energía a través de `sysctl` bajo `hw.pci`. Dos variables son las más relevantes:

```sh
sysctl hw.pci.do_power_suspend
sysctl hw.pci.do_power_resume
```

Ambas tienen el valor 1 por defecto, lo que significa que la capa PCI lleva los dispositivos a D3 en la suspensión y de vuelta a D0 en la reanudación. Establecer cualquiera de ellas a 0 desactiva la transición automática, lo cual es útil para la depuración.

La capa ACPI expone información sobre el estado del sistema:

```sh
sysctl hw.acpi.supported_sleep_state
sysctl hw.acpi.suspend_state
sysctl hw.acpi.s4bios
```

La primera lista qué estados de suspensión admite la plataforma (normalmente algo como `S3 S4 S5`). La segunda es el estado al que entra `zzz` (generalmente `S3`). La tercera indica si S4 está implementado mediante asistencia del BIOS.

Para la observación por dispositivo, el driver expone su propio estado a través de `dev.myfirst.N.*`. El driver del capítulo 22 añade:

- `dev.myfirst.N.suspended`: 1 si el driver se considera suspendido, 0 en caso contrario.
- `dev.myfirst.N.power_suspend_count`: número de veces que se ha llamado a `DEVICE_SUSPEND`.
- `dev.myfirst.N.power_resume_count`: número de veces que se ha llamado a `DEVICE_RESUME`.
- `dev.myfirst.N.power_shutdown_count`: número de veces que se ha llamado a `DEVICE_SHUTDOWN`.
- `dev.myfirst.N.runtime_state`: 0 para `RUNNING`, 1 para `RUNTIME_SUSPENDED`.
- `dev.myfirst.N.runtime_suspend_count`, `dev.myfirst.N.runtime_resume_count`: contadores de runtime PM.
- `dev.myfirst.N.idle_threshold_seconds`: umbral de inactividad del runtime PM.

Con estos sysctls y `dmesg`, el lector puede ver con todo detalle qué hizo el driver durante cualquier transición.

### Un script de regresión

El directorio de laboratorios incorpora un nuevo script: `ch22-suspend-resume-cycle.sh`. El script:

1. Registra los valores de referencia de todos los contadores.
2. Ejecuta una transferencia DMA para confirmar que el dispositivo funciona.
3. Llama a `devctl suspend myfirst0`.
4. Verifica que `dev.myfirst.0.suspended` vale 1.
5. Verifica que `dev.myfirst.0.power_suspend_count` se ha incrementado en 1.
6. Llama a `devctl resume myfirst0`.
7. Verifica que `dev.myfirst.0.suspended` vale 0.
8. Verifica que `dev.myfirst.0.power_resume_count` se ha incrementado en 1.
9. Ejecuta otra transferencia DMA para confirmar que el dispositivo sigue funcionando.
10. Imprime un resumen PASS/FAIL.

El script completo está en el directorio de ejemplos; un breve esquema de la lógica:

```sh
#!/bin/sh
set -e

DEV="dev.myfirst.0"

if ! sysctl -a | grep -q "^${DEV}"; then
    echo "FAIL: ${DEV} not present"
    exit 1
fi

before_s=$(sysctl -n ${DEV}.power_suspend_count)
before_r=$(sysctl -n ${DEV}.power_resume_count)
before_xfer=$(sysctl -n ${DEV}.dma_transfers_read)

# Baseline: run one transfer.
sysctl -n ${DEV}.dma_test_read=1 > /dev/null

# Suspend.
devctl suspend myfirst0
[ "$(sysctl -n ${DEV}.suspended)" = "1" ] || {
    echo "FAIL: device did not mark suspended"
    exit 1
}

# Resume.
devctl resume myfirst0
[ "$(sysctl -n ${DEV}.suspended)" = "0" ] || {
    echo "FAIL: device did not clear suspended"
    exit 1
}

# Another transfer.
sysctl -n ${DEV}.dma_test_read=1 > /dev/null

after_s=$(sysctl -n ${DEV}.power_suspend_count)
after_r=$(sysctl -n ${DEV}.power_resume_count)
after_xfer=$(sysctl -n ${DEV}.dma_transfers_read)

if [ $((after_s - before_s)) -ne 1 ]; then
    echo "FAIL: suspend count did not increment by 1"
    exit 1
fi
if [ $((after_r - before_r)) -ne 1 ]; then
    echo "FAIL: resume count did not increment by 1"
    exit 1
fi
if [ $((after_xfer - before_xfer)) -ne 2 ]; then
    echo "FAIL: expected 2 transfers (pre+post), got $((after_xfer - before_xfer))"
    exit 1
fi

echo "PASS: one suspend-resume cycle completed cleanly"
```

Ejecutar el script repetidamente (por ejemplo, cien veces en un bucle ajustado) es una buena prueba de estrés. Un driver que pasa un ciclo pero falla en el ciclo cincuenta suele tener una fuga de recursos o un caso límite que solo se manifiesta bajo repetición. Ese tipo de bug es exactamente lo que un script de regresión está diseñado para encontrar.

### Ejecución de la prueba de estrés

El directorio `labs/` del capítulo también incluye `ch22-suspend-stress.sh`, que ejecuta el script de ciclo cien veces:

```sh
#!/bin/sh
N=100
i=0
while [ $i -lt $N ]; do
    if ! sh ./ch22-suspend-resume-cycle.sh > /dev/null; then
        echo "FAIL on iteration $i"
        exit 1
    fi
    i=$((i + 1))
done
echo "PASS: $N cycles"
```

En una máquina moderna con el driver myfirst de solo simulación, cien ciclos tardan aproximadamente un segundo. Si alguna iteración falla, el script se detiene e informa del número de iteración. Ejecutar esto tras cada cambio durante el desarrollo detecta las regresiones de inmediato.

### Combinación del runtime PM con las pruebas del espacio de usuario

La ruta del runtime PM necesita una prueba diferente, porque no se activa mediante comandos del usuario, sino por la inactividad. La prueba tiene este aspecto:

```sh
# Ensure runtime_state is running.
sysctl dev.myfirst.0.runtime_state
# 0

# Do nothing for 6 seconds.
sleep 6

# The callout should have fired and runtime-suspended the device.
sysctl dev.myfirst.0.runtime_state
# 1

# Counter should have incremented.
sysctl dev.myfirst.0.runtime_suspend_count
# 1

# Any activity should bring it back.
sysctl dev.myfirst.0.dma_test_read=1
sysctl dev.myfirst.0.runtime_state
# 0

sysctl dev.myfirst.0.runtime_resume_count
# 1
```

Un lector que observe `dmesg` durante esta secuencia verá las líneas "runtime suspend: starting" y "runtime suspend: device in D3" tras unos cinco segundos de inactividad, y luego "runtime resume: starting" cuando llegue la escritura del sysctl.

El directorio de laboratorio del capítulo incluye `ch22-runtime-pm.sh` para automatizar esta secuencia.

### Interpretación de los modos de fallo

Cuando una prueba del espacio de usuario falla, la ruta de diagnóstico depende de qué capa falló:

- **Si `devctl suspend` devuelve un código de salida distinto de cero**: el driver devolvió un valor distinto de cero en `DEVICE_SUSPEND`, vetando la suspensión. Comprueba `dmesg` para ver la salida del log del driver; el método suspend debería registrar qué salió mal.
- **Si `devctl suspend` tiene éxito pero `dev.myfirst.0.suspended` vale 0 después**: el quiesce del driver estableció el indicador brevemente, pero algo lo borró. Esto suele significar que el quiesce está reentrando en sí mismo, o que la ruta de detach está en condición de carrera con el suspend.
- **Si `devctl resume` tiene éxito pero la siguiente transferencia falla**: la ruta de restauración no reinicializó completamente el dispositivo. Lo más habitual es que no se haya escrito una máscara de interrupción o un registro DMA; comprueba los contadores de disparo por vector antes y después del resume para ver si las interrupciones están llegando al driver.
- **Si `acpiconf -s 3` tiene éxito pero el sistema no vuelve**: algún driver por debajo de `myfirst` en el árbol está bloqueando el resume. Esto es poco habitual en una VM de prueba; es el modo de fallo clásico en hardware real con drivers nuevos.
- **Si `acpiconf -s 3` devuelve `EOPNOTSUPP`**: la plataforma no soporta S3. Comprueba `sysctl hw.acpi.supported_sleep_state`.

En todos los casos, la primera fuente de información es `dmesg`. El driver del Capítulo 22 registra cada transición; si las líneas del log no aparecen, el método no fue invocado y el problema está en una capa por debajo del driver.

### Un flujo mínimo de resolución de problemas

Un diagrama de flujo compacto para un ciclo de suspend-resume fallido:

1. ¿Está cargado el driver? `kldstat | grep myfirst`.
2. ¿El dispositivo está attached? `sysctl dev.myfirst.0.%driver`.
3. ¿Registran los métodos suspend y resume? `dmesg | tail`.
4. ¿Conmutó correctamente `dev.myfirst.0.suspended`? `sysctl dev.myfirst.0.suspended`.
5. ¿Se incrementan los contadores? `sysctl dev.myfirst.0.power_suspend_count dev.myfirst.0.power_resume_count`.
6. ¿Tiene éxito una transferencia tras el resume? `sudo sysctl dev.myfirst.0.dma_test_read=1; dmesg | tail -2`.
7. ¿Se incrementan los contadores de interrupción por vector? `sysctl dev.myfirst.0.vec0_fire_count dev.myfirst.0.vec1_fire_count dev.myfirst.0.vec2_fire_count`.

Cualquier respuesta negativa apunta a una capa específica de la implementación. La sección 7 profundiza en los modos de fallo más habituales y en cómo depurarlos.

### Cerrando la sección 6

La sección 6 exploró la interfaz de espacio de usuario con la maquinaria de gestión de energía del kernel: `acpiconf`, `zzz`, `devctl suspend`, `devctl resume`, `devinfo -v` y las variables `sysctl` pertinentes. Mostró cómo combinar estas herramientas en un script de regresión que ejercita un ciclo de suspend-resume y un script de estrés que ejecuta cien ciclos seguidos. Trató el flujo de prueba de runtime-PM, la interpretación de los modos de fallo más habituales y el diagrama de flujo mínimo de resolución de problemas que el lector puede seguir cuando falla una prueba.

Con las herramientas de espacio de usuario en la mano, la siguiente sección profundiza en los modos de fallo característicos que el lector probablemente encontrará al escribir código con gestión de energía y en cómo depurar cada uno de ellos.



## Sección 7: Depuración de problemas de gestión de energía

El código de gestión de energía tiene una clase especial de bugs. La máquina se duerme; la máquina despierta; el bug aparece un tiempo indeterminado después del wake y parece un fallo genérico, no algo relacionado con la transición de energía. La cadena de causa y efecto es más larga que con la mayoría de los bugs de driver, la reproducción es más lenta y el informe de bug del usuario suele ser «mi portátil a veces no se despierta», que no aporta casi ninguna información útil al desarrollador del driver.

La sección 7 trata de reconocer los síntomas característicos, rastrearlos hasta sus causas probables y aplicar los patrones de depuración correspondientes. Se apoya en el driver `myfirst` del capítulo 22 para dar ejemplos concretos, pero los patrones se aplican a cualquier driver de FreeBSD.

### Síntoma 1: Dispositivo congelado tras la reanudación

El bug de gestión de energía más habitual, tanto en drivers de aprendizaje como en producción, es un dispositivo que deja de responder tras la reanudación. El driver se añade correctamente en el boot, funciona con normalidad en S0, gestiona un ciclo de suspend-resume sin errores visibles y, en el siguiente comando, queda en silencio. Las interrupciones no se disparan. Las transferencias DMA no se completan. Cualquier lectura de un registro del dispositivo devuelve valores obsoletos o ceros.

La causa habitual es que los registros del dispositivo no se escribieron tras la reanudación. El dispositivo regresó a su estado por defecto (máscara de interrupción totalmente enmascarada, motor DMA desactivado, los registros que el hardware reinicia al entrar en D0), el driver no los reprogramó y, desde la perspectiva del dispositivo, nada está configurado para funcionar.

**Patrón de depuración.** Compara los valores de los registros del dispositivo antes y después del suspend. El driver `myfirst` expone varios de sus registros mediante sysctls (si el lector los añade); de lo contrario, el lector puede escribir un pequeño helper en espacio del kernel que lea cada registro y lo imprima. Tras un ciclo de suspend-resume:

1. Lee el registro de máscara de interrupción. Si vale `0xFFFFFFFF` (todo enmascarado), la ruta de resume no restauró la máscara.
2. Lee el registro de control DMA. Si tiene el bit ABORT activo, el abort del quiesce nunca se limpió.
3. Lee el espacio de configuración del dispositivo mediante `pciconf -lvbc`. El registro de comandos debe tener activo el bit de bus master; si no es así, falta `pci_enable_busmaster` en la ruta de resume.

**Patrón de corrección.** La ruta de resume debe incluir una reprogramación incondicional de todos los registros específicos del dispositivo de los que depende el funcionamiento normal del driver. Guardarlos en el softc durante el suspend y restaurarlos en el resume es un enfoque; recalcularlos a partir del estado del softc (el enfoque que adopta `re_resume`) es otro. Ambos son válidos; la elección depende de cuál sea más fácil de demostrar correcto para el dispositivo concreto.

### Síntoma 2: Interrupciones perdidas

Una variante más sutil del problema del dispositivo congelado son las interrupciones perdidas: el dispositivo responde a algunas llamadas, pero sus interrupciones no llegan al driver. El motor DMA acepta un comando START, realiza la transferencia, lanza la interrupción de finalización... y el contador de interrupciones no se incrementa. La taskqueue no recibe ninguna entrada. La CV no emite la señal de difusión. La transferencia acaba agotando su tiempo de espera y el driver notifica EIO.

Son varias las cosas que pueden provocar esto:

- La **máscara de interrupción** del dispositivo sigue totalmente enmascarada. El dispositivo quiere lanzar la interrupción, pero la máscara la suprime. (Bug en la ruta de resume.)
- La **configuración MSI o MSI-X** no se restauró. El dispositivo lanza la interrupción, pero el kernel no la enruta al manejador del driver. (Poco habitual; la capa PCI debería gestionarlo automáticamente.)
- El **puntero a la función de filtro** fue corrompido. Extremadamente inusual; normalmente indica corrupción de memoria en otra parte del driver.
- El **indicador de suspended** sigue siendo verdadero y el filtro devuelve el control prematuramente. (Bug en la ruta de resume: indicador no limpiado.)

**Patrón de depuración.** Lee los contadores de disparo por vector antes y después del ciclo de suspend-resume. Si el contador no se incrementa, la interrupción no está llegando al filtro. A continuación, comprueba, en este orden:

1. ¿Está limpio el indicador suspended? `sysctl dev.myfirst.0.suspended`.
2. ¿Es correcta la máscara de interrupción del dispositivo? Lee el registro.
3. ¿Es correcta la tabla MSI-X del dispositivo? `pciconf -c` vuelca los registros de capacidad.
4. ¿Es consistente el estado de despacho MSI del kernel? `procstat -t` muestra los threads de interrupción.

**Patrón de corrección.** Asegúrate de que la ruta de resume (a) limpie el indicador suspended bajo el lock, (b) desenmascare el registro de interrupción del dispositivo tras limpiar el indicador, (c) no dependa de la restauración de MSI-X que el driver debe realizar por sí mismo (salvo que esté desactivada expresamente mediante sysctl).

### Síntoma 3: DMA incorrecto tras la reanudación

Una clase de bug más peligrosa es la DMA que parece funcionar pero produce datos incorrectos. El driver programa el motor, el motor se ejecuta, la interrupción de finalización se dispara, la tarea se ejecuta, se llama al sync, el driver lee el buffer... y los bytes son incorrectos. No son ceros ni basura, sino datos sutilmente incorrectos: el patrón escrito anteriormente, el patrón de hace dos ciclos, o un patrón que indica que la DMA accedió a la página equivocada.

Causas:

- La **dirección de bus almacenada en el softc** está obsoleta. Esto es poco habitual en una asignación estática (la dirección se establece una vez en el attach y no debería cambiar), pero puede ocurrir si el driver reasigna el buffer DMA en el momento del resume (una mala idea; véase más adelante).
- El **registro de dirección base del motor DMA** no se reprogramó tras la reanudación y tiene un valor obsoleto que apunta a otro lugar.
- Las **llamadas a `bus_dmamap_sync` faltan o están en orden incorrecto**. Este es el bug clásico de corrección DMA, y merece la pena estar alerta en las rutas de resume porque el código del driver adyacente a las llamadas sync suele modificarse durante una refactorización.
- La **tabla de traducción IOMMU** no se restauró. Muy infrecuente en FreeBSD porque la configuración del IOMMU es por sesión y sobrevive al suspend en la mayoría de plataformas; pero si el driver corre en un sistema donde `DEV_IOMMU` es inusual, puede aparecer.

**Patrón de depuración.** Añade una escritura de patrón conocido antes de cada DMA, una verificación después de cada DMA y registra ambos. Reducir el ciclo a «escribir 0xAA, sync, leer, esperar 0xAA» hace que los bugs de corrupción de datos sean visibles de inmediato.

```c
memset(sc->dma_vaddr, 0xAA, MYFIRST_DMA_BUFFER_SIZE);
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_PREWRITE);
/* run transfer */
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_POSTWRITE);
if (((uint8_t *)sc->dma_vaddr)[0] != 0xAA) {
        device_printf(sc->dev,
            "dma: corruption detected after transfer\n");
}
```

En la simulación, esto debería tener siempre éxito porque la simulación no modifica el buffer en una transferencia de escritura. En hardware real, el patrón depende del dispositivo. Un lector que depura un bug en hardware real adapta la prueba.

**Patrón de corrección.** Si la dirección de bus es el problema, reconstrúyela en el resume:

```c
/* In resume, after PCI restore is complete. */
err = bus_dmamap_load(sc->dma_tag, sc->dma_map,
    sc->dma_vaddr, MYFIRST_DMA_BUFFER_SIZE,
    myfirst_dma_single_map, &sc->dma_bus_addr,
    BUS_DMA_NOWAIT);
```

Haz esto solo si la dirección de bus realmente cambió, lo cual es infrecuente. Lo más habitual es que la corrección consista en escribir el registro de dirección base al inicio de cada transferencia (en lugar de depender de un valor persistente) y en asegurarse de que las llamadas sync están en el orden correcto.

### Síntoma 4: Eventos de wake PME# perdidos

En un dispositivo que admite wake-on-X, el síntoma es «el dispositivo debería haber despertado al sistema pero no lo hizo». El driver notificó un suspend satisfactorio; el sistema entró en S3; ocurrió el evento esperado (paquete mágico, pulsación de botón, temporizador); y el sistema permaneció dormido.

Causas:

- **`pci_enable_pme` no se llamó** en la ruta de suspend. El bit PME_En del dispositivo vale 0, de modo que incluso cuando el dispositivo activaría normalmente PME#, el bit está suprimido.
- **La lógica de wake propia del dispositivo no está configurada**. En una NIC, los registros de wake-on-LAN deben programarse antes del suspend. En un controlador host USB, la capacidad de remote-wakeup debe habilitarse por puerto.
- **El GPE de wake de la plataforma no está habilitado**. Esto suele ser cuestión del firmware; el método ACPI `_PRW` debería haber registrado el GPE, pero en algunas máquinas el BIOS lo desactiva por defecto.
- **El bit de estado PME está activo en el momento del suspend**, y un PME# obsoleto es lo que desencadena el wake (en lugar del evento esperado). El sistema parece despertar inmediatamente después de dormirse.

**Patrón de depuración.** Lee el espacio de configuración PCI mediante `pciconf -lvbc`. El registro de estado/control de la capacidad de gestión de energía muestra PME_En y el bit PME_Status. Antes de suspender, PME_Status debe ser 0 (sin wake pendiente). Tras suspender con wake habilitado, PME_En debe ser 1.

En una máquina donde el wake no se produce, comprueba los ajustes del BIOS para «wake on LAN», «wake on USB», etc. El driver puede ser perfecto y el sistema seguir sin despertar si la plataforma no está configurada.

**Patrón de corrección.** En la ruta de suspend de un driver con capacidad de wake:

```c
static int
myfirst_pci_suspend(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        int err;

        /* ... quiesce as before ... */

        if (sc->wake_enabled && pci_has_pm(dev)) {
                /* Program device-specific wake logic here. */
                myfirst_program_wake(sc);
                pci_enable_pme(dev);
        }

        /* ... rest of suspend ... */
}
```

En la ruta de resume:

```c
static int
myfirst_pci_resume(device_t dev)
{
        if (pci_has_pm(dev))
                pci_clear_pme(dev);
        /* ... rest of resume ... */
}
```

El driver `myfirst` del capítulo 22 no implementa wake (la simulación no tiene lógica de wake). El patrón anterior se muestra a modo de referencia.

### Síntoma 5: Avisos de WITNESS durante el suspend

Un kernel de depuración con `WITNESS` habilitado produce con frecuencia mensajes como:

```text
witness: acquiring sleepable lock foo_mtx @ /path/to/driver.c:123
witness: sleeping with non-sleepable lock bar_mtx @ /path/to/driver.c:456
```

Son violaciones de orden de locks o violaciones de sleep-while-locked, y aparecen con frecuencia en el código de suspend porque suspend hace cosas que el driver no suele hacer: adquirir locks, dormir y coordinar múltiples threads.

Causas:

- La ruta de suspend adquiere un lock y luego llama a una función que duerme sin tolerancia explícita a hacerlo con ese lock retenido.
- La ruta de suspend adquiere locks en un orden diferente al del resto del driver, y `WITNESS` detecta la inversión.
- La ruta de suspend llama a `taskqueue_drain` o `callout_drain` mientras retiene el lock del softc, lo que provoca un deadlock si la tarea o el callout intenta adquirir el mismo lock.

**Patrón de depuración.** Lee el mensaje de `WITNESS` con atención. Incluye los nombres de los locks y los números de línea del código fuente donde se adquirió cada uno. Traza la ruta desde la adquisición hasta el sleep o la inversión de lock.

**Patrón de corrección.** El `myfirst_quiesce` del capítulo 22 suelta el lock del softc antes de llamar a `myfirst_drain_workers` precisamente por esta razón. Al extender el driver:

- No llames a `taskqueue_drain` con ningún lock del driver retenido.
- No llames a `callout_drain` con el lock que el callout adquiere.
- Las primitivas de sleep (`pause`, `cv_wait`) deben llamarse solo con sleep-mutexes retenidos (no spin-mutexes).
- Si necesitas soltar un lock para dormir, hazlo explícitamente y vuelve a adquirirlo después.

### Síntoma 6: Contadores que no coinciden

El script de regresión del capítulo espera que `power_suspend_count == power_resume_count` tras cada ciclo. Cuando divergen, algo está mal.

Causas:

- Se invocó `DEVICE_SUSPEND` en el driver, pero este salió antes de incrementar el contador. (Ocurre con frecuencia porque se activó una comprobación de cordura.)
- No se invocó `DEVICE_RESUME` porque `DEVICE_SUSPEND` devolvió un valor distinto de cero y el kernel deshizo la secuencia.
- Los contadores no son atómicos y una actualización concurrente perdió un incremento. (Poco probable si el código usa `atomic_add_64`.)
- El driver se descargó y se volvió a cargar entre las mediciones, lo que reinició los contadores.

**Patrón de depuración.** Ejecuta el script de regresión tras limpiar el buffer con `dmesg -c` y consulta `dmesg` después de cada ciclo. El registro muestra cada invocación de método; contar las líneas del registro es una alternativa a contar los contadores, y cualquier diferencia indica un bug.

### Síntoma 7: Bloqueos durante la suspensión

Un bloqueo durante la suspensión es el peor escenario de diagnóstico: el kernel sigue en ejecución (la consola aún responde a la señal de break hacia DDB), pero la secuencia de suspensión se ha quedado atascada en el `DEVICE_SUSPEND` de algún driver. Entra en DDB y ejecuta `ps` para ver en qué estado se encuentra cada thread:

```text
db> ps
...  0 myfirst_drain_dma+0x42 myfirst_pci_suspend+0x80 ...
```

**Patrón de depuración.** Identifica el thread bloqueado y la función en la que está atascado. Por lo general se trata de un `cv_wait` o un `cv_timedwait` que nunca terminó, o de un `taskqueue_drain` esperando una tarea que no llega a completarse.

**Patrón de corrección.** Añade un timeout a cualquier espera que realice la ruta de suspensión. La función `myfirst_drain_dma` utiliza `cv_timedwait` con un timeout de un segundo; una variante que use `cv_wait` (sin timeout) puede bloquearse indefinidamente. La implementación del capítulo utiliza siempre variantes temporizadas por este motivo.

### Uso de DTrace para trazar la suspensión y la reanudación

DTrace es una herramienta excelente para observar la ruta de gestión de energía con gran precisión sin necesidad de añadir instrucciones de impresión. Un script D sencillo que mide el tiempo de cada llamada:

```d
fbt::device_suspend:entry,
fbt::device_resume:entry
{
    self->ts = timestamp;
    printf("%s: %s %s\n", probefunc,
        args[0] != NULL ? stringof(args[0]->name) : "?",
        args[0] != NULL ? stringof(args[0]->desc) : "?");
}

fbt::device_suspend:return,
fbt::device_resume:return
/self->ts/
{
    printf("%s: returned %d after %d us\n",
        probefunc, arg1,
        (timestamp - self->ts) / 1000);
    self->ts = 0;
}
```

Guárdalo como `trace-devpower.d` y ejecútalo con `dtrace -s trace-devpower.d`. Cualquier `devctl suspend` o `acpiconf -s 3` producirá una salida que muestra los tiempos de suspensión y reanudación de cada dispositivo, junto con sus valores de retorno.

Para el driver `myfirst` en particular, `fbt::myfirst_pci_suspend:entry` y `fbt::myfirst_pci_resume:entry` son las sondas. Un script D centrado en el driver:

```d
fbt::myfirst_pci_suspend:entry {
    self->ts = timestamp;
    printf("myfirst_pci_suspend: entered\n");
    stack();
}

fbt::myfirst_pci_suspend:return
/self->ts/ {
    printf("myfirst_pci_suspend: returned %d after %d us\n",
        arg1, (timestamp - self->ts) / 1000);
    self->ts = 0;
}
```

La llamada `stack()` imprime la pila de llamadas en la entrada, lo cual es útil para confirmar que el método se invoca desde donde se espera (el `bus_suspend_child` del bus PCI, por ejemplo).

### Una nota sobre la disciplina en el registro de eventos

El código del Capítulo 22 registra información de forma generosa durante la suspensión y la reanudación: cada método registra su entrada y su salida, y cada función auxiliar registra sus propios eventos. Esa verbosidad resulta útil durante el desarrollo, pero molesta en producción (cada suspensión del portátil imprime media docena de líneas en dmesg).

Un buen driver de producción expone un sysctl que controla el nivel de verbosidad del registro:

```c
static int myfirst_power_verbose = 1;
SYSCTL_INT(_dev_myfirst, OID_AUTO, power_verbose,
    CTLFLAG_RWTUN, &myfirst_power_verbose, 0,
    "Verbose power-management logging (0=off, 1=on, 2=debug)");
```

Y el registro de eventos se vuelve condicional:

```c
if (myfirst_power_verbose >= 1)
        device_printf(dev, "suspend: starting\n");
```

Un lector que quiera habilitar la depuración en un sistema de producción puede establecer `dev.myfirst.power_verbose=2` temporalmente, reproducir el problema y restablecer la variable. El driver del Capítulo 22 no implementa esta estratificación; el driver de aprendizaje lo registra todo y acepta el ruido.

### Uso del kernel INVARIANTS para la cobertura de aserciones

Un kernel de depuración con `INVARIANTS` compilado hace que las macros `KASSERT` evalúen realmente sus condiciones y provoquen un panic si se incumplen. El código de `myfirst_dma.c` y `myfirst_pci.c` utiliza varios KASSERTs; el código de gestión de energía añade algunos más. Por ejemplo, el invariante de quiesce:

```c
static int
myfirst_quiesce(struct myfirst_softc *sc)
{
        /* ... */

        KASSERT(sc->dma_in_flight == false,
            ("myfirst: dma_in_flight still true after drain"));

        return (0);
}
```

En un kernel `INVARIANTS`, un bug que deje `dma_in_flight` como verdadero provoca un panic inmediato con un mensaje informativo. En un kernel de producción, la aserción se elimina en la compilación y no ocurre nada. El driver de aprendizaje se ejecuta deliberadamente en un kernel `INVARIANTS` para detectar esta clase de errores.

Del mismo modo, la ruta de reanudación puede incluir una aserción:

```c
KASSERT(sc->suspended == true,
    ("myfirst: resume called but not suspended"));
```

Esto detecta un bug en el que el driver recibe de algún modo una llamada a resume sin que haya ocurrido la suspensión correspondiente (habitualmente un bug en el driver del bus padre, no en el propio driver `myfirst`).

### Un caso de estudio de depuración

Para reunir todos los patrones, considera un escenario concreto. El lector escribe la suspensión de la etapa 2, ejecuta un ciclo de regresión y observa:

```text
myfirst0: suspend: starting
myfirst0: drain_dma: timeout waiting for abort
myfirst0: suspend: complete (dma in flight=0, suspended=1)
myfirst0: resume: starting
myfirst0: resume: complete
```

Luego:

```sh
sudo sysctl dev.myfirst.0.dma_test_read=1
# Returns EBUSY after a long delay
```

El síntoma visible para el usuario es que la transferencia posterior a la reanudación no funciona. El registro muestra un timeout de drenaje durante la suspensión, que es la primera anomalía.

**Hipótesis.** El motor DMA no respetó el bit ABORT. El driver borró `dma_in_flight` a la fuerza, pero el motor sigue en ejecución; cuando el usuario inicia una nueva transferencia, el motor no está listo.

**Prueba.** Comprueba el registro de estado del motor antes y después del abort:

```c
/* In myfirst_drain_dma, after writing ABORT: */
uint32_t pre_status = CSR_READ_4(sc->dev, MYFIRST_REG_DMA_STATUS);
DELAY(100);  /* let the engine notice */
uint32_t post_status = CSR_READ_4(sc->dev, MYFIRST_REG_DMA_STATUS);
device_printf(sc->dev, "drain: status %#x -> %#x\n", pre_status, post_status);
```

Ejecutar el ciclo de nuevo produce:

```text
myfirst0: drain: status 0x4 -> 0x4
```

El estado 0x4 es RUNNING. El motor ignoró el ABORT. Esto apunta al backend de simulación: el motor simulado podría no implementar el abort, o podría hacerlo únicamente cuando se ejecuta el callout de la simulación.

**Corrección.** Examina el código del motor DMA de la simulación y verifica la semántica del abort. En este caso, el motor de la simulación gestiona el abort en su callback de callout, que no se ejecuta hasta pasados unos milisegundos. Amplía el timeout de drenaje de 1 segundo (suficiente) a... espera, 1 segundo es más que suficiente para un callout que se dispara cada pocos milisegundos. El problema real está en otro lugar.

Una investigación más profunda revela que el callout de la simulación fue drenado *antes* de que el drenaje DMA se completara. El orden en `myfirst_drain_workers` (tarea primero, callout después) era incorrecto; debería ser callout primero, tarea después, porque el callout es lo que impulsa la finalización del abort.

**Resolución.** Reordena el drenaje:

```c
static void
myfirst_drain_workers(struct myfirst_softc *sc)
{
        /*
         * Drain the callout first: it runs the simulated engine's
         * completion logic, and the drain-DMA path waits on that
         * completion. Draining the callout after drain_dma would let
         * drain_dma time out and force-clear the in-flight flag.
         *
         * Wait - actually, drain_dma has already completed by the time
         * we get here, because myfirst_quiesce calls it first. So the
         * order of the two drains inside this function does not matter
         * for that reason. But drain_workers is also called from detach,
         * where drain_dma may not have been called, and the order there
         * does matter.
         */
        if (sc->sim != NULL)
                myfirst_sim_drain_dma_callout(sc->sim);

        if (sc->rx_vector.has_task)
                taskqueue_drain(taskqueue_thread, &sc->rx_vector.task);
}
```

Pero espera: para cuando `myfirst_drain_workers` es llamado desde `myfirst_quiesce`, `myfirst_drain_dma` ya ha terminado. La espera de drain-dma está dentro de la llamada a drain-dma; la llamada a drain-workers solo limpia el estado residual. El orden dentro de drain-workers es, en su mayor parte, estético para la suspensión.

La corrección real está antes: `myfirst_drain_dma` no debería haber agotado el tiempo de espera. El timeout de 1 segundo debería haber sido suficiente. La causa real es diferente: quizás el callout de la simulación no se disparaba porque el driver mantenía un lock de sysctl que lo bloqueaba. O la escritura del bit ABORT no llegó a la simulación porque el manejador MMIO de la simulación también estaba bloqueado.

**Lección.** La depuración de problemas de gestión de energía es un proceso iterativo. Cada síntoma sugiere una hipótesis; cada prueba la va acotando; la corrección se encuentra a menudo en una capa diferente a la que apuntaba el síntoma. La paciencia para seguir la cadena es lo que distingue el buen código con soporte de energía del código que simplemente funciona casi siempre.

### Cerrando la sección 7

La sección 7 ha recorrido los modos de fallo característicos de los drivers con soporte de energía: dispositivos congelados, interrupciones perdidas, DMA defectuoso, eventos de despertar omitidos, quejas de WITNESS, desviación de contadores y bloqueos absolutos. Para cada uno ha mostrado la causa típica, un patrón de depuración que acota el problema y el patrón de corrección que lo elimina. También ha presentado DTrace para la medición, ha tratado la disciplina de registro y ha mostrado cómo `INVARIANTS` y `WITNESS` detectan la clase de errores que solo aparecen en condiciones específicas.

La disciplina de depuración de la sección 7, al igual que la disciplina de quiesce de la sección 3 y la disciplina de restauración de la sección 4, pretende acompañar al lector más allá del driver `myfirst`. Todo driver con soporte de energía tiene alguna variante de estos errores acechando en su implementación; los patrones anteriores son la forma de encontrarlos antes de que lleguen al usuario.

La sección 8 pone fin al Capítulo 22 consolidando el código de las secciones 2 a 7 en un archivo `myfirst_power.c` refactorizado, subiendo la versión a `1.5-power`, añadiendo un documento `POWER.md` y conectando una prueba de integración final.



## Sección 8: Refactorización y versionado de tu driver con soporte de energía

Las etapas 1 a 3 añadieron el código de gestión de energía de forma inline en `myfirst_pci.c`. Eso resultaba conveniente para la enseñanza, porque cada cambio aparecía junto al código de attach y detach que el lector ya conocía. Es menos conveniente para la legibilidad: `myfirst_pci.c` tiene ahora attach, detach, tres métodos de energía y varios helpers, y el archivo es lo suficientemente largo como para que un lector que lo ve por primera vez tenga que desplazarse para encontrar las cosas.

La etapa 4, la versión final del driver del Capítulo 22, extrae todo el código de gestión de energía de `myfirst_pci.c` y lo traslada a un nuevo par de archivos, `myfirst_power.c` y `myfirst_power.h`. Esto sigue el mismo patrón que la separación de `myfirst_msix.c` en el Capítulo 20 y la de `myfirst_dma.c` en el Capítulo 21: el nuevo archivo tiene una API reducida y bien documentada, y el código que llama en `myfirst_pci.c` utiliza únicamente esa API.

### La estructura objetivo

Tras la etapa 4, los archivos fuente del driver son:

- `myfirst.c` - pegamento de nivel superior, estado compartido, árbol de sysctl.
- `myfirst_hw.c`, `myfirst_hw_pci.c` - helpers de acceso a registros.
- `myfirst_sim.c` - backend de simulación.
- `myfirst_pci.c` - attach y detach PCI, tabla de métodos y reenvío liviano a los módulos de subsistema.
- `myfirst_intr.c` - interrupción de vector único (ruta heredada del Capítulo 19).
- `myfirst_msix.c` - configuración de interrupciones multivector (Capítulo 20).
- `myfirst_dma.c` - configuración, desmontaje y transferencia DMA (Capítulo 21).
- `myfirst_power.c` - gestión de energía (Capítulo 22, nuevo).
- `cbuf.c` - soporte de buffer circular.

El nuevo archivo `myfirst_power.h` declara la API pública del subsistema de energía:

```c
#ifndef _MYFIRST_POWER_H_
#define _MYFIRST_POWER_H_

struct myfirst_softc;

int  myfirst_power_setup(struct myfirst_softc *sc);
void myfirst_power_teardown(struct myfirst_softc *sc);

int  myfirst_power_suspend(struct myfirst_softc *sc);
int  myfirst_power_resume(struct myfirst_softc *sc);
int  myfirst_power_shutdown(struct myfirst_softc *sc);

#ifdef MYFIRST_ENABLE_RUNTIME_PM
int  myfirst_power_runtime_suspend(struct myfirst_softc *sc);
int  myfirst_power_runtime_resume(struct myfirst_softc *sc);
void myfirst_power_mark_active(struct myfirst_softc *sc);
#endif

void myfirst_power_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_POWER_H_ */
```

El par `_setup` y `_teardown` inicializa y desmonta el estado a nivel de subsistema (el callout, los sysctls). Las funciones por transición encapsulan la misma lógica que construyó el código de las secciones 3 a 5. Las funciones de runtime PM se compilan únicamente cuando el flag de tiempo de compilación está definido.

### El archivo myfirst_power.c

El nuevo archivo tiene unas trescientas líneas. Su estructura refleja la de `myfirst_dma.c`: inclusiones de cabeceras, helpers estáticos, funciones públicas, manejadores de sysctl y `_add_sysctls`.

Los helpers son los tres de la sección 3:

- `myfirst_mask_interrupts`
- `myfirst_drain_dma`
- `myfirst_drain_workers`

Más uno de la sección 4:

- `myfirst_restore`

Y, si el runtime PM está habilitado, dos de la sección 5:

- `myfirst_idle_watcher_cb`
- `myfirst_start_idle_watcher`

Las funciones públicas `myfirst_power_suspend`, `myfirst_power_resume` y `myfirst_power_shutdown` se convierten en envoltorios ligeros que llaman a los helpers en el orden correcto y actualizan los contadores. Los manejadores de sysctl exponen los controles de política y los contadores de observabilidad.

### Actualización de myfirst_pci.c

El archivo `myfirst_pci.c` es ahora mucho más corto. Sus tres métodos de energía simplemente reenvían cada uno al subsistema de energía:

```c
static int
myfirst_pci_suspend(device_t dev)
{
        return (myfirst_power_suspend(device_get_softc(dev)));
}

static int
myfirst_pci_resume(device_t dev)
{
        return (myfirst_power_resume(device_get_softc(dev)));
}

static int
myfirst_pci_shutdown(device_t dev)
{
        return (myfirst_power_shutdown(device_get_softc(dev)));
}
```

La tabla de métodos permanece igual a como la configuró la etapa 1. Los tres prototipos anteriores son ahora el único código relacionado con la energía en `myfirst_pci.c`, aparte de la llamada a `myfirst_power_setup` desde attach y a `myfirst_power_teardown` desde detach.

La ruta de attach incorpora una llamada adicional:

```c
static int
myfirst_pci_attach(device_t dev)
{
        /* ... existing attach code ... */

        err = myfirst_power_setup(sc);
        if (err != 0) {
                device_printf(dev, "power setup failed\n");
                /* unwind */
                myfirst_dma_teardown(sc);
                /* ... rest of unwind ... */
                return (err);
        }

        myfirst_power_add_sysctls(sc);

        return (0);
}
```

La ruta de detach incorpora la llamada correspondiente:

```c
static int
myfirst_pci_detach(device_t dev)
{
        /* ... existing detach code ... */

        myfirst_power_teardown(sc);

        /* ... rest of detach ... */

        return (0);
}
```

`myfirst_power_setup` inicializa `saved_intr_mask`, el flag `suspended`, los contadores y (si el runtime PM está habilitado) el callout del vigilante de inactividad. `myfirst_power_teardown` drena el callout y limpia cualquier estado a nivel de subsistema. El teardown debe realizarse antes que el teardown DMA porque el callout puede seguir referenciando el estado DMA.

### Actualización del Makefile

El nuevo archivo fuente se añade a la lista `SRCS` y se incrementa la versión:

```make
KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       myfirst_msix.c \
       myfirst_dma.c \
       myfirst_power.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.5-power\"

# Optional: enable runtime PM.
# CFLAGS+= -DMYFIRST_ENABLE_RUNTIME_PM

.include <bsd.kmod.mk>
```

El flag `MYFIRST_ENABLE_RUNTIME_PM` está desactivado por defecto en la etapa 4; el código de runtime PM se compila pero queda envuelto en `#ifdef`. Un lector que quiera experimentar activa el flag en tiempo de compilación.

### Escritura de POWER.md

El patrón del Capítulo 21 estableció el precedente: cada subsistema recibe un documento markdown que describe su propósito, su API, su modelo de estados y su descripción del proceso de pruebas. `POWER.md` es el siguiente.

Un buen `POWER.md` tiene estas secciones:

1. **Propósito**: un párrafo que explica qué hace el subsistema.
2. **API pública**: una tabla de prototipos de funciones con descripciones de una línea.
3. **Modelo de estados**: un diagrama o descripción en texto de los estados y transiciones.
4. **Contadores y sysctls**: los sysctls de solo lectura y lectura/escritura que expone el subsistema.
5. **Flujos de transición**: qué ocurre durante cada una de las fases de suspensión, reanudación y apagado.
6. **Interacción con otros subsistemas**: cómo se relaciona la gestión de energía con el DMA, las interrupciones y la simulación.
7. **Runtime PM (opcional)**: cómo funciona el runtime PM y cuándo se habilita.
8. **Pruebas**: los scripts de regresión y estrés.
9. **Limitaciones conocidas**: qué no hace todavía el subsistema.
10. **Véase también**: referencias cruzadas a `bus(9)`, `pci(9)` y el texto del capítulo.

El documento completo está en el directorio de ejemplos (`examples/part-04/ch22-power/stage4-final/POWER.md`); el capítulo no lo reproduce de forma inline, pero un lector que quiera comprobar la estructura esperada puede abrirlo.

### Script de regresión

El script de regresión de la etapa 4 ejercita cada camino de ejecución:

```sh
#!/bin/sh
# ch22-full-regression.sh

set -e

# 1. Basic sanity.
sudo kldload ./myfirst.ko

# 2. One suspend-resume cycle.
sudo sh ./ch22-suspend-resume-cycle.sh

# 3. One hundred cycles in a row.
sudo sh ./ch22-suspend-stress.sh

# 4. A transfer before, during, and after a cycle.
sudo sh ./ch22-transfer-across-cycle.sh

# 5. If runtime PM is enabled, test it.
if sysctl -N dev.myfirst.0.runtime_state >/dev/null 2>&1; then
    sudo sh ./ch22-runtime-pm.sh
fi

# 6. Unload.
sudo kldunload myfirst

echo "FULL REGRESSION PASSED"
```

Cada sub-script tiene unas pocas decenas de líneas y comprueba una sola cosa. Ejecutar la regresión completa tras cada cambio detecta regresiones de inmediato.

### Integración con las pruebas de regresión existentes

El script de regresión del Capítulo 21 comprobaba:

- `dma_complete_intrs == dma_complete_tasks` (la tarea siempre ve cada interrupción).
- `dma_complete_intrs == dma_transfers_write + dma_transfers_read + dma_errors + dma_timeouts`.

El script del Capítulo 22 añade:

- `power_suspend_count == power_resume_count` (cada suspensión tiene su correspondiente reanudación).
- El flag `suspended` vale 0 fuera de una transición.
- Tras un ciclo de suspensión y reanudación, los contadores DMA siguen sumando el total esperado (sin transferencias fantasma).

La regresión combinada es el script completo del Capítulo 22. Ejercita DMA, interrupciones, MSI-X y gestión de energía de forma conjunta. Un driver que la supera está en buenas condiciones.

### Historial de versiones

El driver ha evolucionado a través de varias versiones:

- `1.0` - Capítulo 16: driver únicamente MMIO, backend de simulación.
- `1.1` - Capítulo 18: attach PCI, BAR real.
- `1.2-intx` - Capítulo 19: interrupción de vector único con filtro y tarea.
- `1.3-msi` - Capítulo 20: MSI-X multivector con fallback.
- `1.4-dma` - Capítulo 21: configuración de `bus_dma`, motor DMA simulado, finalización dirigida por interrupciones.
- `1.5-power` - Capítulo 22: suspensión/reanudación/apagado, refactorizado en `myfirst_power.c`, PM en tiempo de ejecución opcional.

Cada versión se construye sobre la anterior. Un lector que tenga el driver del Capítulo 21 funcionando puede aplicar los cambios del Capítulo 22 de forma incremental y llegar a `1.5-power` sin necesidad de reescribir ningún código anterior.

### Una prueba de integración final en hardware real

Si el lector tiene acceso a hardware real (una máquina con una implementación S3 funcional), el driver del Capítulo 22 puede probarse mediante una suspensión completa del sistema:

```sh
sudo kldload ./myfirst.ko
sudo sh ./ch22-suspend-resume-cycle.sh
sudo acpiconf -s 3
# [laptop sleeps; user opens lid]
# After resume, the DMA test should still work.
sudo sysctl dev.myfirst.0.dma_test_read=1
```

En la mayoría de las plataformas donde funciona ACPI S3, el driver supera el ciclo completo. La salida de `dmesg` muestra las líneas de suspensión y reanudación igual que lo haría `devctl`, lo que confirma que el mismo código del método se ejecuta en ambos contextos.

Si la prueba del sistema completo falla donde la prueba por dispositivo tuvo éxito, el trabajo extra que realiza la suspensión del sistema (transiciones de estados de reposo ACPI, aparcamiento de CPUs, autorrecarga de RAM) ha expuesto algo que la prueba por dispositivo no detectó. Los culpables habituales son valores de registros específicos del dispositivo que el estado de bajo consumo del sistema reinicia, pero que el D3 por dispositivo no hace. Un driver que solo se prueba con `devctl` puede pasar por alto estos casos; un driver que se prueba con `acpiconf -s 3` al menos una vez antes de declararse correcto es más fiable.

### El código del Capítulo 22 en un solo lugar

Un resumen compacto de lo que añadió el driver de la Fase 4:

- **Un nuevo archivo**: `myfirst_power.c`, unas trescientas líneas.
- **Un nuevo archivo de cabecera**: `myfirst_power.h`, unas treinta líneas.
- **Un nuevo documento markdown**: `POWER.md`, unas doscientas líneas.
- **Cinco nuevos campos en el softc**: `suspended`, `saved_intr_mask`, `power_suspend_count`, `power_resume_count`, `power_shutdown_count`, más los campos de PM en tiempo de ejecución cuando esa característica está habilitada.
- **Tres nuevas líneas `DEVMETHOD`**: `device_suspend`, `device_resume`, `device_shutdown`.
- **Tres nuevas funciones auxiliares**: `myfirst_mask_interrupts`, `myfirst_drain_dma`, `myfirst_drain_workers`.
- **Dos nuevos puntos de entrada del subsistema**: `myfirst_power_setup`, `myfirst_power_teardown`.
- **Tres nuevas funciones de transición**: `myfirst_power_suspend`, `myfirst_power_resume`, `myfirst_power_shutdown`.
- **Seis nuevos sysctls**: los nodos de contadores y el flag de suspensión.
- **Varios scripts de laboratorio nuevos**: cycle, stress, transfer-across-cycle, runtime-PM.

El incremento total de líneas es de unas setecientas líneas de código, más un par de cientos de líneas de documentación y scripts. Para la capacidad que añadió el capítulo (un driver que gestiona correctamente cada transición de energía que el kernel pueda plantearle), es una inversión proporcionada.

### Cerrando la sección 8

La sección 8 cerró la construcción del driver del Capítulo 22 dividiendo el código de gestión de energía en su propio archivo, incrementando la versión a `1.5-power`, añadiendo un documento `POWER.md` y conectando la prueba de regresión final. El patrón era familiar de los Capítulos 20 y 21: tomar el código en línea, extraerlo en un subsistema con una API pequeña, documentar el subsistema e integrarlo en el resto del driver mediante llamadas a función en lugar de acceso directo a los campos.

El driver resultante es consciente de la energía en todos los sentidos que introdujo el capítulo: gestiona `DEVICE_SUSPEND`, `DEVICE_RESUME` y `DEVICE_SHUTDOWN`; detiene el dispositivo de forma limpia; restaura el estado correctamente en la reanudación; implementa opcionalmente la gestión de energía en tiempo de ejecución; expone su estado a través de sysctls; tiene una prueba de regresión; y sobrevive a la suspensión completa del sistema en hardware real cuando la plataforma lo admite.



## Análisis en profundidad: gestión de energía en /usr/src/sys/dev/re/if_re.c

El driver `re(4)` gestiona las NIC Gigabit Realtek 8169 y compatibles. Es un driver muy instructivo para leer con el propósito del Capítulo 22, porque implementa el trío completo suspensión-reanudación-apagado con soporte para wake-on-LAN, y su código ha sido suficientemente estable como para representar un patrón canónico de FreeBSD. Un lector que haya completado el Capítulo 22 puede abrir `/usr/src/sys/dev/re/if_re.c` y reconocer la estructura de inmediato.

> **Cómo leer este recorrido.** Los listados emparejados de `re_suspend()` y `re_resume()` en las subsecciones siguientes proceden de `/usr/src/sys/dev/re/if_re.c`, y el extracto de la tabla de métodos abrevia el array `re_methods[]` completo con un comentario `/* ... other methods ... */` para que las tres entradas `DEVMETHOD` relacionadas con la energía resulten más visibles. Se han mantenido intactas las signaturas, el patrón de adquisición y liberación del lock, y el orden de las llamadas específicas del dispositivo (`re_stop`, `re_setwol`, `re_clrwol`, `re_init_locked`); la tabla de métodos real tiene muchas más entradas y el archivo circundante contiene las implementaciones auxiliares. Todos los símbolos que mencionan los listados son identificadores reales de FreeBSD en `if_re.c` que puedes encontrar con una búsqueda de símbolos.

### La tabla de métodos

La tabla de métodos del driver `re(4)` incluye los tres métodos de energía cerca del principio:

```c
static device_method_t re_methods[] = {
        DEVMETHOD(device_probe,     re_probe),
        DEVMETHOD(device_attach,    re_attach),
        DEVMETHOD(device_detach,    re_detach),
        DEVMETHOD(device_suspend,   re_suspend),
        DEVMETHOD(device_resume,    re_resume),
        DEVMETHOD(device_shutdown,  re_shutdown),
        /* ... other methods ... */
};
```

Este es exactamente el patrón que enseña el Capítulo 22. La tabla de métodos del driver `myfirst` tiene el mismo aspecto.

### re_suspend

La función de suspensión ocupa unas doce líneas:

```c
static int
re_suspend(device_t dev)
{
        struct rl_softc *sc;

        sc = device_get_softc(dev);

        RL_LOCK(sc);
        re_stop(sc);
        re_setwol(sc);
        sc->suspended = 1;
        RL_UNLOCK(sc);

        return (0);
}
```

Tres llamadas hacen el trabajo: `re_stop` detiene la NIC (deshabilita las interrupciones, para el DMA, detiene los motores RX y TX), `re_setwol` programa la lógica de wake-on-LAN y llama a `pci_enable_pme` si WoL está habilitado, y `sc->suspended = 1` establece el flag en el softc.

Compara con `myfirst_power_suspend`:

```c
int
myfirst_power_suspend(struct myfirst_softc *sc)
{
        int err;

        device_printf(sc->dev, "suspend: starting\n");
        err = myfirst_quiesce(sc);
        /* ... error handling ... */
        atomic_add_64(&sc->power_suspend_count, 1);
        return (0);
}
```

La estructura es idéntica. `re_stop` y `re_setwol` juntos son el equivalente de `myfirst_quiesce`; el driver del capítulo no tiene wake-on-X, por lo que no hay ningún análogo de `re_setwol`.

### re_resume

La función de reanudación ocupa unas treinta líneas:

```c
static int
re_resume(device_t dev)
{
        struct rl_softc *sc;
        if_t ifp;

        sc = device_get_softc(dev);

        RL_LOCK(sc);

        ifp = sc->rl_ifp;
        /* Take controller out of sleep mode. */
        if ((sc->rl_flags & RL_FLAG_MACSLEEP) != 0) {
                if ((CSR_READ_1(sc, RL_MACDBG) & 0x80) == 0x80)
                        CSR_WRITE_1(sc, RL_GPIO,
                            CSR_READ_1(sc, RL_GPIO) | 0x01);
        }

        /*
         * Clear WOL matching such that normal Rx filtering
         * wouldn't interfere with WOL patterns.
         */
        re_clrwol(sc);

        /* reinitialize interface if necessary */
        if (if_getflags(ifp) & IFF_UP)
                re_init_locked(sc);

        sc->suspended = 0;
        RL_UNLOCK(sc);

        return (0);
}
```

Los pasos se corresponden claramente con la disciplina del Capítulo 22:

1. **Sacar el controlador del modo de reposo** (bit de reposo del MAC en algunas versiones de Realtek). Es un paso de restauración específico del dispositivo.
2. **Borrar los patrones WOL** mediante `re_clrwol`, que invierte lo que hizo `re_setwol`. Esto también llama a `pci_clear_pme` de forma implícita a través del borrado.
3. **Reinicializar la interfaz** si estaba activa antes de la suspensión. `re_init_locked` es la misma función que llama attach para levantar la NIC; reprograma el MAC, reinicia los anillos de descriptores, habilita las interrupciones y arranca los motores DMA.
4. **Borrar el flag de suspensión** bajo el lock.

El equivalente en `myfirst_power_resume`:

```c
int
myfirst_power_resume(struct myfirst_softc *sc)
{
        int err;

        device_printf(sc->dev, "resume: starting\n");
        err = myfirst_restore(sc);
        /* ... */
        atomic_add_64(&sc->power_resume_count, 1);
        return (0);
}
```

De nuevo la estructura es idéntica. `myfirst_restore` corresponde a la combinación de la salida del reposo del MAC, `re_clrwol`, `re_init_locked` y el borrado del flag.

### re_shutdown

La función de apagado es:

```c
static int
re_shutdown(device_t dev)
{
        struct rl_softc *sc;

        sc = device_get_softc(dev);

        RL_LOCK(sc);
        re_stop(sc);
        /*
         * Mark interface as down since otherwise we will panic if
         * interrupt comes in later on, which can happen in some
         * cases.
         */
        if_setflagbits(sc->rl_ifp, 0, IFF_UP);
        re_setwol(sc);
        RL_UNLOCK(sc);

        return (0);
}
```

Similar a `re_suspend`, más el borrado del flag de interfaz (el apagado es definitivo; marcar la interfaz como caída evita actividad espuria). El patrón es prácticamente idéntico; `re_shutdown` es esencialmente una versión más defensiva de `re_suspend`.

### re_setwol

Vale la pena examinar la configuración de wake-on-LAN porque muestra cómo un driver real llama a las APIs PCI PM:

```c
static void
re_setwol(struct rl_softc *sc)
{
        if_t ifp;
        uint8_t v;

        RL_LOCK_ASSERT(sc);

        if (!pci_has_pm(sc->rl_dev))
                return;

        /* ... programs device-specific wake registers ... */

        /* Request PME if WOL is requested. */
        if ((if_getcapenable(ifp) & IFCAP_WOL) != 0)
                pci_enable_pme(sc->rl_dev);
}
```

Aquí aparecen tres patrones clave que merece la pena copiar en cualquier driver consciente de la energía que admita wake-on-X:

1. **Guardia `pci_has_pm(dev)`.** La función retorna anticipadamente si el dispositivo no admite gestión de energía. Esto evita escrituras en registros que no existen.
2. **Programación de wake específica del dispositivo.** La mayor parte de la función escribe registros específicos de Realtek a través de `CSR_WRITE_1`. Un driver para un dispositivo diferente escribiría registros distintos, pero la ubicación (dentro del camino de suspensión, antes de `pci_enable_pme`) es la misma.
3. **`pci_enable_pme` condicional.** Solo se habilita PME# si el usuario ha pedido expresamente wake-on-X. Si no lo ha hecho, la función igualmente establece los bits de configuración relevantes (por coherencia con las capacidades de la interfaz del driver), pero no llama a `pci_enable_pme`.

La función inversa es `re_clrwol`:

```c
static void
re_clrwol(struct rl_softc *sc)
{
        uint8_t v;

        RL_LOCK_ASSERT(sc);

        if (!pci_has_pm(sc->rl_dev))
                return;

        /* ... clears the wake-related config bits ... */
}
```

Observa que `re_clrwol` no llama explícitamente a `pci_clear_pme`; la capa PCI, a través de `pci_resume_child`, ya lo ha llamado antes del `DEVICE_RESUME` del driver. `re_clrwol` es responsable de deshacer el lado visible para el driver de la configuración WoL, no el estado PME visible para el kernel.

### Lo que muestra el análisis en profundidad

El driver de Realtek es más complejo que `myfirst` en cualquier medida (más registros, más estado, más variantes de dispositivo), y sin embargo su disciplina de gestión de energía es menos compleja. Ello se debe a que la complejidad del *dispositivo* no se corresponde directamente con la complejidad del *código de gestión de energía*. La disciplina del Capítulo 22 escala tanto hacia abajo como hacia arriba: un dispositivo sencillo tiene un camino de energía sencillo; un dispositivo complejo tiene un camino de energía moderadamente más complejo. La estructura es la misma.

Un lector que haya terminado el Capítulo 22 puede ahora abrir `if_re.c`, reconocer cada función y cada patrón, y entender por qué existe cada uno. Esa comprensión se transfiere: el mismo reconocimiento se aplica a `if_xl.c`, `virtio_blk.c` y cientos de otros drivers de FreeBSD. El Capítulo 22 no enseña una API específica de `myfirst`; enseña el idioma de gestión de energía de FreeBSD, y el driver `myfirst` es el vehículo que lo hizo concreto.



## Análisis en profundidad: patrones más simples en if_xl.c y virtio_blk.c

A modo de contraste, otros dos drivers de FreeBSD implementan la gestión de energía de formas todavía más simples.

### if_xl.c: el apagado llama a la suspensión

El driver 3Com EtherLink III en `/usr/src/sys/dev/xl/if_xl.c` tiene la configuración mínima de tres métodos:

```c
static int
xl_shutdown(device_t dev)
{
        return (xl_suspend(dev));
}

static int
xl_suspend(device_t dev)
{
        struct xl_softc *sc;

        sc = device_get_softc(dev);

        XL_LOCK(sc);
        xl_stop(sc);
        xl_setwol(sc);
        XL_UNLOCK(sc);

        return (0);
}

static int
xl_resume(device_t dev)
{
        struct xl_softc *sc;
        if_t ifp;

        sc = device_get_softc(dev);
        ifp = sc->xl_ifp;

        XL_LOCK(sc);

        if (if_getflags(ifp) & IFF_UP) {
                if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
                xl_init_locked(sc);
        }

        XL_UNLOCK(sc);

        return (0);
}
```

Dos cosas destacan:

1. `xl_shutdown` es una sola línea: simplemente llama a `xl_suspend`. Para este driver, el apagado y la suspensión hacen el mismo trabajo, y el código no necesita dos copias.
2. No hay ningún flag `suspended` en el softc. El driver asume el ciclo de vida normal de attach → ejecución → suspensión → reanudación, y usa el flag `IFF_DRV_RUNNING` (que la ruta TX ya comprueba) como equivalente. Es un enfoque perfectamente válido para una NIC cuyo principal estado visible para el usuario es el estado de ejecución de la interfaz.

Para el driver `myfirst`, se prefiere el flag `suspended` explícito porque el driver no tiene ningún equivalente natural de `IFF_DRV_RUNNING`. Un driver de NIC puede reutilizar lo que ya tiene; un driver didáctico declara lo que necesita.

### virtio_blk.c: detención mínima

El driver de bloque virtio en `/usr/src/sys/dev/virtio/block/virtio_blk.c` tiene un camino de suspensión todavía más corto:

```c
static int
vtblk_suspend(device_t dev)
{
        struct vtblk_softc *sc;
        int error;

        sc = device_get_softc(dev);

        VTBLK_LOCK(sc);
        sc->vtblk_flags |= VTBLK_FLAG_SUSPEND;
        /* XXX BMV: virtio_stop(), etc needed here? */
        error = vtblk_quiesce(sc);
        if (error)
                sc->vtblk_flags &= ~VTBLK_FLAG_SUSPEND;
        VTBLK_UNLOCK(sc);

        return (error);
}

static int
vtblk_resume(device_t dev)
{
        struct vtblk_softc *sc;

        sc = device_get_softc(dev);

        VTBLK_LOCK(sc);
        sc->vtblk_flags &= ~VTBLK_FLAG_SUSPEND;
        vtblk_startio(sc);
        VTBLK_UNLOCK(sc);

        return (0);
}
```

El comentario `/* XXX BMV: virtio_stop(), etc needed here? */` es un reconocimiento honesto de que el autor no estaba seguro de cuán exhaustiva debía ser la detención. El código existente establece un flag, espera a que la cola se vacíe (eso es lo que hace `vtblk_quiesce`) y retorna. Al reanudar, borra el flag y reinicia el I/O.

Para un dispositivo de bloque virtio, esto es suficiente porque el host virtio (el hipervisor) implementa su propia detención cuando el huésped indica que se está suspendiendo. El driver solo necesita dejar de enviar nuevas peticiones; el host se encarga del resto.

Esto muestra un patrón importante: **la profundidad del quiesce del driver depende de cuánta parte del estado del hardware es responsabilidad del driver**. Un driver bare-metal (como `re(4)`) debe programar los registros de hardware con cuidado, porque el hardware no tiene ningún otro aliado. Un driver virtio tiene al hipervisor como aliado; el host puede gestionar la mayor parte del estado en nombre del guest. El driver `myfirst`, que se ejecuta sobre un backend simulado, se encuentra en una posición similar: la simulación es un aliado, y el quiesce del driver puede ser proporcionalmente más sencillo.

### Lo que muestra la comparación

Leer el código de gestión de energía de varios drivers en paralelo es una de las mejores formas de adquirir soltura. Cada driver adapta el patrón del Capítulo 22 a su contexto: `re(4)` gestiona el wake-on-LAN, `xl(4)` reutiliza `xl_shutdown = xl_suspend`, `virtio_blk(4)` confía en el hipervisor. El hilo conductor es la estructura: detener la actividad, guardar el estado, marcar como suspendido, devolver 0 desde suspend; al reanudar, limpiar el marcador, restaurar el estado, reiniciar la actividad, devolver 0.

Un lector que tenga el Capítulo 22 fresco en la memoria puede abrir cualquier driver de FreeBSD, encontrar sus `device_suspend` y `device_resume` en la tabla de métodos, y leer las dos funciones. En pocos minutos, la política de gestión de energía del driver queda clara. Esa habilidad se aplica a todos los drivers con los que trabajes en el futuro; es el aprendizaje más valioso del capítulo.



## Análisis en profundidad: los estados de suspensión ACPI con más detalle

La sección 1 presentó los estados S de ACPI como una lista. Merece la pena revisarlos con la perspectiva del driver en primer plano, porque el driver percibe cosas ligeramente distintas según el estado S al que entre el kernel.

### S0: en funcionamiento

S0 es el estado en el que el lector ha trabajado a lo largo de los capítulos 16 al 21. El CPU ejecuta instrucciones, la RAM se refresca y los enlaces PCIe están activos. Desde el punto de vista del driver, S0 es continuo; todo funciona con normalidad.

Dentro de S0, sin embargo, pueden producirse transiciones de energía más finas. El CPU puede entrar en estados inactivos (C1, C2, C3, etc.) entre los ticks del planificador. El enlace PCIe puede entrar en L0s o L1 en función de ASPM. Los dispositivos pueden entrar en D3 en función de la gestión de energía en tiempo de ejecución. Ninguna de estas situaciones exige que el driver haga nada más allá de su propia lógica de gestión de energía en tiempo de ejecución; son transparentes.

### S1: en espera

S1 es históricamente el estado de suspensión más ligero. El CPU deja de ejecutar instrucciones, pero sus registros se conservan; la RAM permanece alimentada; la alimentación del dispositivo se mantiene en D0 o D1. La latencia de activación es rápida (inferior a un segundo).

En el hardware moderno, S1 rara vez está soportado. El BIOS de la plataforma solo anuncia S3 y estados más profundos. Si la plataforma sí anuncia S1 y el usuario entra en él, se sigue llamando a `DEVICE_SUSPEND` del driver; el driver realiza su parada habitual. La diferencia es que la capa PCI normalmente no transita a D3 en S1 (porque el bus permanece alimentado), por lo que el dispositivo se mantiene en D0 durante la transición. Las operaciones de guardado y restauración del driver quedan en gran medida sin utilizar.

Un driver que gestiona S1 correctamente también gestiona S3, porque el trabajo del lado del driver es un subconjunto. Ningún driver escrito para el Capítulo 22 necesita tratar S1 de forma especial.

### S2: reservado

S2 está definido en la especificación ACPI, pero casi nunca se implementa. Un driver puede ignorarlo con seguridad; la capa ACPI de FreeBSD trata S2 como S1 o S3, según el soporte de la plataforma.

### S3: suspensión en RAM

S3 es el estado de suspensión canónico al que se dirige el Capítulo 22. Cuando el usuario entra en S3:

1. La secuencia de suspensión del kernel recorre el árbol de dispositivos y llama a `DEVICE_SUSPEND` en cada driver.
2. La función `pci_suspend_child` de la capa PCI almacena en caché el espacio de configuración de cada dispositivo PCI.
3. La capa PCI transiciona cada dispositivo PCI a D3hot.
4. Los subsistemas de nivel superior (ACPI, la maquinaria de inactividad del CPU) entran en sus propios estados de suspensión.
5. El contexto del CPU se guarda en RAM; el CPU se detiene.
6. La RAM entra en modo de autorrefrescado; el controlador de memoria mantiene el contenido con una alimentación mínima.
7. El circuito de activación de la plataforma queda armado: el botón de encendido, el interruptor de la tapa y cualquier fuente de activación configurada.
8. El sistema espera un evento de activación.

Cuando llega un evento de activación:

1. El CPU se reanuda; su contexto se restaura desde RAM.
2. Los subsistemas de nivel superior se reanudan.
3. La capa PCI recorre el árbol de dispositivos y llama a `pci_resume_child` para cada dispositivo.
4. Cada dispositivo pasa a D0; su configuración se restaura; el PME# pendiente se limpia.
5. Se llama a `DEVICE_RESUME` de cada driver.
6. El espacio de usuario se descongela.

El driver solo ve los pasos 1 (suspend) y 5 (resume) de cada secuencia. El resto es maquinaria del kernel y de la plataforma.

Un detalle sutil: durante S3, la RAM se refresca, pero el kernel no está en ejecución. Esto significa que cualquier estado del lado del kernel (el softc, el buffer de DMA, las tareas pendientes) sobrevive a S3 sin cambios. Lo único que puede perderse es el estado del hardware: los registros de configuración del dispositivo pueden reiniciarse; los registros mapeados en los BAR pueden volver a sus valores por defecto. La tarea del driver al reanudar es reprogramar el hardware a partir del estado del kernel conservado.

### S4: suspensión en disco (hibernación)

S4 es el estado de hibernación. El kernel escribe el contenido completo de la RAM en una imagen de disco y, a continuación, entra en S5. Al activarse, la plataforma arranca, el kernel lee la imagen y el sistema continúa desde donde lo dejó.

En FreeBSD, S4 ha sido históricamente parcial. El kernel puede generar la imagen de hibernación en algunas plataformas, pero la ruta de restauración no es tan madura como la de Linux. A efectos del driver, S4 es igual a S3: se llama a los métodos `DEVICE_SUSPEND` y `DEVICE_RESUME`; las rutas de parada y restauración del driver funcionan sin cambios. El trabajo adicional a nivel de plataforma (escribir la imagen) es transparente.

La única diferencia que podría notar el driver es que, tras la reanudación desde S4, el espacio de configuración PCI siempre se restaura desde cero (la plataforma ha arrancado completamente), por lo que, aunque el driver dependiera de que `hw.pci.do_power_suspend` sea 0 para mantener el dispositivo en D0, después de S4 el dispositivo habrá pasado igualmente por un ciclo de alimentación completo. Esto solo importa para los drivers que realizan ajustes específicos de plataforma durante la suspensión; la mayoría de los drivers no se ven afectados.

### S5: apagado por software

S5 es el apagado del sistema. El botón de encendido, la batería (si la hay) y el circuito de activación siguen recibiendo alimentación; todo lo demás está apagado.

Desde el punto de vista del driver, S5 se parece a un apagado: se llama a `DEVICE_SHUTDOWN` (no a `DEVICE_SUSPEND`), el driver coloca el dispositivo en un estado seguro para el apagado y el sistema se detiene. No existe ninguna reanudación correspondiente a S5; si el usuario pulsa el botón de encendido, el sistema arranca desde cero.

El apagado no es una transición de energía en el sentido reversible; es una terminación. El método `DEVICE_SHUTDOWN` del driver se llama una vez, y el driver no espera volver a ejecutarse hasta el siguiente arranque. La función `myfirst_power_shutdown` del capítulo gestiona esto correctamente deteniendo el dispositivo (igual que en suspend) y sin intentar guardar ningún estado (porque no hay reanudación para la que guardar).

### Cómo observar qué estados admite la plataforma

En cualquier sistema FreeBSD 14.3 con ACPI, los estados soportados se exponen mediante un sysctl:

```sh
sysctl hw.acpi.supported_sleep_state
```

Salidas típicas:

- Un portátil moderno: `S3 S4 S5`
- Un servidor: `S5` (la suspensión no está soportada en muchas plataformas de servidor)
- Una VM en bhyve: varía; normalmente solo `S5`
- Una VM en QEMU/KVM con `-machine q35`: a menudo `S3 S4 S5`

Si un driver está pensado para funcionar en una plataforma específica, la lista de estados soportados te indica qué transiciones debes probar. Un driver que solo se ejecuta en servidores no necesita pruebas de S3; uno pensado para portátiles, sí.

### Qué probar

Para los propósitos del Capítulo 22, las pruebas mínimas son:

- `devctl suspend` / `devctl resume`: siempre posible; prueba la ruta de código del lado del driver.
- `acpiconf -s 3` (si está soportado): prueba la suspensión completa del sistema.
- Apagado del sistema (`shutdown -p now`): prueba el método `DEVICE_SHUTDOWN`.

S4 y la gestión de energía en tiempo de ejecución son opcionales; ejercitan rutas de código menos utilizadas. Un driver que supera las pruebas mínimas en una plataforma que soporta S3 está en buena forma; las extensiones son la guinda del pastel.

### Correspondencia entre los estados de suspensión y los métodos del driver

Una tabla compacta de qué método kobj se llama para cada transición:

| Transición              | Método                  | Acción del driver                                              |
|-------------------------|-------------------------|----------------------------------------------------------------|
| S0 → S1                 | DEVICE_SUSPEND          | Detener actividad; guardar estado                              |
| S0 → S3                 | DEVICE_SUSPEND          | Detener actividad; guardar estado (el dispositivo probablemente pasa a D3) |
| S0 → S4                 | DEVICE_SUSPEND          | Detener actividad; guardar estado (seguido de hibernación)     |
| S0 → S5 (apagado)       | DEVICE_SHUTDOWN         | Detener actividad; dejar en estado seguro para el apagado      |
| S1/S3 → S0              | DEVICE_RESUME           | Restaurar estado; desenmascarar interrupciones                 |
| S4 → S0 (reanudación)   | (attach desde arranque) | attach normal, porque el kernel arrancó desde cero             |
| devctl suspend          | DEVICE_SUSPEND          | Detener actividad; guardar estado (el dispositivo pasa a D3)   |
| devctl resume           | DEVICE_RESUME           | Restaurar estado; desenmascarar interrupciones                 |

El driver no distingue entre S1, S3 y S4 desde su propio código; siempre realiza el mismo trabajo. Las diferencias se encuentran a nivel de plataforma y de kernel. Esa uniformidad es lo que hace que el patrón sea escalable: una ruta de suspend, una ruta de resume, múltiples contextos.



## Análisis en profundidad: los estados del enlace PCIe y ASPM en la práctica

La sección 1 esbozó los estados del enlace PCIe (L0, L0s, L1, L1.1, L1.2, L2). Merece la pena ver cómo se comportan en la práctica, porque comprenderlos ayuda al desarrollador de drivers a interpretar las medidas de latencia y las observaciones de consumo.

### Por qué el enlace tiene sus propios estados

Un enlace PCIe es un par de carriles diferenciales de alta velocidad entre dos extremos (root complex y dispositivo, o root complex y switch). Cada carril tiene un transmisor y un receptor; el transmisor de cada carril consume energía para mantener el canal en un estado conocido. Cuando el tráfico es bajo, los transmisores pueden apagarse en distintos grados, y el enlace puede restablecerse rápidamente cuando el tráfico se reanuda. Los estados L describen esos grados.

El estado del enlace es independiente del estado D del dispositivo. Un dispositivo en D0 puede tener su enlace en L1 (el enlace está inactivo; el dispositivo no transmite ni recibe). Un dispositivo en D3 tiene su enlace en L2 o similar (el enlace está apagado). Un dispositivo en D0 con el enlace ocupado se encuentra en L0.

### L0: activo

L0 es el estado de funcionamiento normal. Ambos lados del enlace están activos; los datos pueden fluir en cualquier dirección; la latencia es mínima (unos pocos cientos de nanosegundos de ida y vuelta en un PCIe moderno).

Cuando se está ejecutando una transferencia DMA o hay una lectura MMIO pendiente, el enlace está en L0. La lógica propia del dispositivo y el PCIe host bridge requieren L0 para la transacción.

### L0s: transmisor en espera

L0s es un estado de bajo consumo en el que se apaga el transmisor de un lado del enlace. El receptor permanece activo; el enlace puede volver a L0 en menos de un microsegundo.

L0s se activa automáticamente por la lógica del enlace cuando no se ha enviado tráfico durante unos pocos microsegundos. El PCIe host bridge de la plataforma y la interfaz PCIe del dispositivo cooperan: cuando el FIFO de transmisión está vacío y ASPM está habilitado, el transmisor se apaga. Cuando llega nuevo tráfico, el transmisor vuelve a activarse.

L0s es asimétrico: cada lado entra y sale del estado de forma independiente. El transmisor de un dispositivo puede estar en L0s mientras el transmisor del root complex está en L0. Esto resulta útil porque el tráfico es típicamente en ráfagas: el CPU envía un disparador DMA y luego no envía nada más durante un tiempo; el transmisor del CPU entra en L0s rápidamente, mientras que el transmisor del dispositivo permanece en L0 porque está enviando activamente la respuesta DMA.

### L1: ambos lados en espera

L1 es un estado más profundo en el que ambos transmisores están apagados. Ninguno de los dos lados puede enviar nada hasta que el enlace regrese a L0; la latencia se mide en microsegundos (de 5 a 65, según la plataforma).

L1 se activa tras un periodo de inactividad más prolongado que L0s. El umbral exacto es configurable a través de los ajustes de ASPM; los valores típicos son decenas de microsegundos de inactividad. L1 ahorra más energía que L0s, pero tiene un coste de salida mayor.

### L1.1 y L1.2: subestados más profundos de L1

PCIe 3.0 y versiones posteriores definen subestados de L1 que apagan partes adicionales de la capa física. L1.1 (también llamado "L1 PM Substate 1") mantiene el reloj en funcionamiento pero apaga más circuitería; L1.2 también apaga el reloj. Las latencias de activación aumentan (decenas de microsegundos para L1.1; cientos para L1.2), pero el consumo en inactividad disminuye.

La mayoría de los portátiles modernos utilizan L1.1 y L1.2 de forma intensiva para prolongar la duración de la batería. Un portátil que permanece en L1.2 la mayor parte del tiempo en inactividad puede tener un consumo PCIe de un solo dígito en milivatios, frente a cientos de milivatios en L0.

### L2: casi apagado

L2 es el estado al que entra el enlace cuando el dispositivo se encuentra en D3cold. El enlace está efectivamente apagado; restablecerlo requiere una secuencia de link training completa (decenas de milisegundos). El sistema entra en L2 como parte de la secuencia de suspensión general; el driver no lo gestiona directamente.

### Quién controla el ASPM

ASPM es una característica por enlace que se configura a través de los registros PCIe Link Capability y Link Control, tanto en el root complex como en el dispositivo. La configuración especifica:

- Si L0s está habilitado (campo de un bit).
- Si L1 está habilitado (campo de un bit).
- Los umbrales de latencia de salida que la plataforma considera aceptables.

En FreeBSD, ASPM está habitualmente controlado por el firmware de la plataforma a través del método `_OSC` de ACPI. El firmware indica al sistema operativo qué capacidades debe gestionar; si el firmware retiene el control de ASPM, el sistema operativo no lo toca. Si el firmware cede el control, el sistema operativo puede habilitar o deshabilitar ASPM por enlace según la política.

Para el driver `myfirst` del Capítulo 22, ASPM es responsabilidad de la plataforma. El driver no configura ASPM; no necesita saber si el enlace está en L0 o L1 en ningún momento. El estado del enlace es invisible para el driver desde un punto de vista funcional (la latencia es el único efecto observable).

### Cuándo importa ASPM al driver

Hay situaciones específicas en las que un driver sí tiene que preocuparse por ASPM:

1. **Erratas conocidas.** Algunos dispositivos PCIe tienen bugs en su implementación de ASPM que provocan que el enlace se bloquee o produzca transacciones corruptas. Es posible que el driver necesite deshabilitar ASPM explícitamente para esos dispositivos. El kernel proporciona acceso al registro PCIe Link Control a través de `pcie_read_config` y `pcie_write_config` para este propósito.

2. **Dispositivos sensibles a la latencia.** Un dispositivo de audio o vídeo en tiempo real puede no tolerar la latencia a escala de microsegundos de L1. El driver puede deshabilitar L1 manteniendo L0s habilitado.

3. **Dispositivos sensibles al consumo.** Un dispositivo alimentado por batería puede querer L1.2 siempre habilitado. El driver puede forzar L1.2 si el valor predeterminado de la plataforma es menos agresivo.

Para el driver `myfirst`, ninguna de estas situaciones aplica. El dispositivo simulado no tiene un enlace en absoluto; el enlace PCIe real (si existe) está gestionado por la plataforma. El capítulo menciona ASPM por completitud y continúa.

### Observar los estados del enlace

En un sistema en el que la plataforma admite la observación de ASPM, el estado del enlace se expone a través de `pciconf -lvbc`:

```sh
pciconf -lvbc | grep -A 20 myfirst
```

Busca líneas como:

```text
cap 10[ac] = PCI-Express 2 endpoint max data 128(512) FLR NS
             link x1(x1) speed 5.0(5.0)
             ASPM disabled(L0s/L1)
             exit latency L0s 1us/<1us L1 8us/8us
             slot 0
```

El "ASPM disabled" en esta línea indica que ASPM no está activo en este momento. "disabled(L0s/L1)" significa que el dispositivo admite tanto L0s como L1, pero ninguno está habilitado. En un sistema con ASPM agresivo, la línea mostraría "ASPM L1" o algo similar.

Las latencias de salida le indican al driver cuánto tarda la transición de vuelta a L0; un driver sensible a la latencia puede decidir si L1 es tolerable consultando este valor.

### Estado del enlace y consumo de energía

Una tabla aproximada de consumos de energía PCIe (valores típicos; los valores reales dependen de la implementación):

| Estado | Potencia (enlace x1) | Latencia de salida |
|--------|----------------------|--------------------|
| L0     | 100-200 mW           | 0                  |
| L0s    | 50-100 mW            | <1 µs              |
| L1     | 10-30 mW             | 5-65 µs            |
| L1.1   | 1-5 mW               | 10-100 µs          |
| L1.2   | <1 mW                | 50-500 µs          |
| L2     | cerca de 0           | 1-100 ms           |

En un portátil con una docena de enlaces PCIe todos en L1.2 durante el estado inactivo, el ahorro agregado respecto a todos en L0 puede ser de varios vatios. En un servidor con enlaces de alto rendimiento siempre en L0, ASPM está deshabilitado y el ahorro de energía es nulo.

El Capítulo 22 no implementa ASPM para `myfirst`. El capítulo lo menciona porque entender la máquina de estados del enlace forma parte de comprender el panorama completo de la gestión de energía. Un lector que más adelante trabaje en un driver con erratas ASPM conocidas sabrá dónde buscar.



## Análisis en profundidad: fuentes de activación explicadas

Las fuentes de activación son los mecanismos que devuelven a activo un sistema o dispositivo suspendido. El Capítulo 1 las mencionó brevemente; este análisis más detallado recorre las más comunes.

### PME# en PCIe

La especificación PCI define la señal `PME#` (Power Management Event). Cuando se activa, indica al root complex aguas arriba que el dispositivo tiene un evento que justifica despertar al sistema. El root complex convierte PME# en un GPE o interrupción de ACPI, que el kernel gestiona.

Un dispositivo que admite PME# tiene una capacidad de gestión de energía PCI (comprobada a través de `pci_has_pm`). El registro de control de la capacidad incluye:

- **PME_En** (bit 8): habilita la generación de PME#.
- **PME_Status** (bit 15): lo establece el dispositivo cuando se activa PME#; el software lo limpia.
- **PME_Support** (solo lectura, bits 11-15 del registro PMC): los estados D desde los que el dispositivo puede activar PME# (D0, D1, D2, D3hot, D3cold).

La responsabilidad del driver es establecer PME_En en el momento adecuado (habitualmente antes de la suspensión) y limpiar PME_Status en el momento oportuno (habitualmente tras la reanudación). Los helpers `pci_enable_pme(dev)` y `pci_clear_pme(dev)` realizan ambas tareas.

En un portátil típico, el root complex enruta PME# a un GPE de ACPI, que el driver ACPI del kernel captura como evento de activación. La cadena tiene el siguiente aspecto:

```text
device asserts PME#
  → root complex receives PME
  → root complex sets GPE status bit
  → ACPI hardware interrupts CPU
  → kernel wakes from S3
  → kernel's ACPI driver services the GPE
  → eventually: DEVICE_RESUME on the device that woke
```

Toda la cadena tarda entre uno y tres segundos. El papel del driver es mínimo: habilitó PME# antes de la suspensión, y limpiará PME_Status tras la reanudación. Todo lo demás es responsabilidad de la plataforma.

### Activación remota por USB

USB tiene su propio mecanismo de activación llamado "remote wakeup". Un dispositivo USB solicita la capacidad de activación a través de su descriptor estándar; el controlador host la habilita en el momento de la enumeración; cuando el dispositivo activa una señal de reanudación en su puerto aguas arriba, el controlador host la propaga.

Desde el punto de vista del driver de FreeBSD, el remote wakeup por USB está gestionado casi en su totalidad por el driver del controlador host USB (`xhci`, `ohci`, `uhci`). Los drivers de dispositivos USB individuales (para teclados, almacenamiento, audio, etc.) participan a través de los callbacks de suspensión y reanudación del framework USB, pero no tratan con PME# directamente. El PME# del propio controlador host USB es lo que en realidad despierta el sistema.

Para los propósitos del Capítulo 22, la activación por USB es una caja negra que funciona a través del driver del controlador host USB. Un lector que eventualmente escriba un driver de dispositivo USB aprenderá entonces las convenciones del framework.

### Activación basada en GPIO en plataformas embebidas

En plataformas embebidas (arm64, RISC-V), las fuentes de activación son típicamente pines GPIO conectados a la lógica de activación del SoC. El árbol de dispositivos describe qué pines son fuentes de activación mediante propiedades `wakeup-source` e `interrupts-extended` que apuntan al controlador de activación.

El framework GPIO intr de FreeBSD gestiona todo esto. Un driver cuyo hardware es capaz de activar el sistema lee la propiedad `wakeup-source` del árbol de dispositivos durante el attach, registra el GPIO como fuente de activación en el framework, y el framework se encarga del resto. El mecanismo es muy diferente al de PCIe PME#, pero la API del lado del driver (marcar la activación como habilitada, limpiar el estado de activación) es conceptualmente similar.

El Capítulo 22 no ejercita la activación por GPIO; el driver `myfirst` es un dispositivo PCI. La Parte 7 revisita las plataformas embebidas y cubre el camino a través de GPIO en detalle.

### Wake on LAN (WoL)

Wake on LAN es un patrón de implementación específico para un controlador de red. El controlador vigila los paquetes entrantes en busca de un "magic packet" (un patrón específico que contiene la dirección MAC del controlador repetida muchas veces) o de patrones configurados por el usuario. Cuando se detecta una coincidencia, el controlador activa PME# aguas arriba.

Desde el punto de vista del driver, WoL requiere:

1. Configurar la lógica de activación de la NIC (filtro de magic packet, filtros de patrones) antes de la suspensión.
2. Habilitar PME# mediante `pci_enable_pme`.
3. Al reanudar, deshabilitar la lógica de activación (para que el procesamiento normal de paquetes no se vea influenciado por los filtros).

La función `re_setwol` del driver `re(4)` es el ejemplo canónico en FreeBSD. Un lector que construya un driver de NIC copia su estructura y adapta la programación de los registros específicos del dispositivo.

### Activación por tapa, botón de encendido y similares

El interruptor de tapa del portátil, el botón de encendido, el teclado (en algunos casos) y otras entradas de la plataforma están conectados a la lógica de activación de la plataforma a través de ACPI. El driver ACPI gestiona la activación; los drivers de dispositivos individuales no están involucrados.

El método ACPI `_PRW` en el objeto de un dispositivo dentro del espacio de nombres ACPI declara qué GPE utiliza el evento de activación de ese dispositivo. El sistema operativo lee `_PRW` durante el boot para configurar el enrutamiento de la activación. El driver `myfirst`, al ser un endpoint PCI simple sin fuente de activación específica de la plataforma, no tiene un método `_PRW`; su capacidad de activación (si la tiene) se realiza exclusivamente a través de PME#.

### Cuándo debe el driver habilitar la activación

Una heurística sencilla: el driver debe habilitar la activación si el usuario lo ha solicitado (a través de un indicador de capacidad de interfaz como `IFCAP_WOL` para NICs) y el hardware lo admite (`pci_has_pm` devuelve true y la propia lógica de activación del dispositivo es operativa). En caso contrario, el driver deja la activación deshabilitada.

Un driver que habilita la activación para todos los dispositivos por defecto malgasta la energía de la plataforma; el circuito de activación y el enrutamiento de PME# consumen algunos milivatios de forma continua. Un driver que nunca habilita la activación frustra a los usuarios que quieren que su portátil despierte ante un paquete de red. La política es "habilitar solo cuando se solicite".

Las capacidades de interfaz de FreeBSD (configuradas mediante `ifconfig em0 wol wol_magic`) son la forma estándar en la que los usuarios expresan esa preferencia. El driver de la NIC lee los indicadores y configura WoL en consecuencia.

### Probar las fuentes de activación

Probar la activación es más difícil que probar la suspensión y la reanudación, porque requiere que el sistema realmente entre en suspensión y que un evento externo lo despierte. Los enfoques más habituales son:

- **Magic packet desde otra máquina.** Envía un magic packet de WoL a la dirección MAC de la máquina suspendida. Si WoL funciona, la máquina despertará en pocos segundos.
- **Interruptor de tapa.** Cierra la tapa, espera y ábrela. Si el enrutamiento de activación de la plataforma funciona, la máquina despertará al abrirla.
- **Botón de encendido.** Pulsa brevemente el botón de encendido mientras el sistema está suspendido. La máquina debería despertar.

Para un driver de aprendizaje como `myfirst`, no hay una fuente de activación significativa contra la que probar. El capítulo menciona los mecanismos de activación por completitud pedagógica, no porque el driver los ejercite.



## Análisis en profundidad: el parámetro ajustable hw.pci.do_power_suspend

Uno de los parámetros ajustables más importantes para la depuración de la gestión de energía es `hw.pci.do_power_suspend`. Controla si la capa PCI realiza automáticamente la transición de los dispositivos a D3 durante la suspensión del sistema. Entender qué hace y cuándo cambiarlo merece una mirada detallada.

### Qué hace el comportamiento predeterminado

Con `hw.pci.do_power_suspend=1` (el valor predeterminado), el helper `pci_suspend_child` de la capa PCI, tras llamar a `DEVICE_SUSPEND` del driver, realiza la transición del dispositivo a D3hot llamando a `pci_set_power_child(dev, child, PCI_POWERSTATE_D3)`. Al reanudar, `pci_resume_child` realiza la transición de vuelta a D0.

Este es el modo de "ahorro de energía". Un dispositivo que admite D3 utiliza su estado inactivo de menor consumo durante la suspensión. Un portátil se beneficia de ello porque aumenta la duración de la batería durante el sueño; un dispositivo que puede dormir a unos pocos milivatios en lugar de varios cientos justifica con creces la transición adicional de estado D.

### Qué hace hw.pci.do_power_suspend=0

Con el parámetro ajustable establecido en 0, la capa PCI no realiza la transición del dispositivo a D3. El dispositivo permanece en D0 durante toda la suspensión. Se ejecuta `DEVICE_SUSPEND` del driver; el driver detiene la actividad; el dispositivo permanece alimentado.

Desde el punto de vista del ahorro de energía, esto es peor: el dispositivo sigue consumiendo su presupuesto de energía de D0 durante el sueño. Desde el punto de vista de la corrección, puede ser mejor para algunos dispositivos:

- Un dispositivo con una implementación rota de D3 puede comportarse de forma incorrecta cuando se realiza la transición. Permanecer en D0 evita el bug de la transición.
- Un dispositivo cuyo contexto es costoso de guardar y restaurar puede preferir permanecer en D0 durante una suspensión breve. Si la suspensión dura solo unos segundos, el coste del guardado del contexto supera el beneficio del ahorro de energía.
- Un dispositivo crítico para la función central de la máquina (un teclado de consola, por ejemplo) puede necesitar permanecer activo incluso durante la suspensión.

### Cuándo cambiarlo

Para el desarrollo y la depuración, establecer `hw.pci.do_power_suspend=0` puede aislar bugs:

- Si un bug de reanudación aparece solo con el parámetro ajustable en 1, el bug está en la transición de D3 a D0 (ya sea en la restauración de la configuración de la capa PCI, o en el manejo por parte del driver de un dispositivo que ha sido reiniciado).
- Si un bug de reanudación aparece también con el parámetro ajustable en 0, el bug está en el código `DEVICE_SUSPEND` o `DEVICE_RESUME` del driver, no en la maquinaria de estados D.

En producción, el valor predeterminado (1) casi siempre es correcto. Cambiarlo globalmente afecta a todos los dispositivos PCI del sistema; una mejor alternativa es un ajuste por dispositivo cuando se necesita, que normalmente reside en el propio driver.

### Verificación de que el parámetro configurable está activo

Una forma rápida de verificarlo es comprobar el estado de energía del dispositivo con `pciconf` antes y después de una suspensión:

```sh
# Before suspend (device should be in D0):
pciconf -lvbc | grep -A 5 myfirst

# With hw.pci.do_power_suspend=1 (default):
sudo devctl suspend myfirst0
pciconf -lvbc | grep -A 5 myfirst
# "powerspec" should show D3

# With hw.pci.do_power_suspend=0:
sudo sysctl hw.pci.do_power_suspend=0
sudo devctl resume myfirst0
sudo devctl suspend myfirst0
pciconf -lvbc | grep -A 5 myfirst
# "powerspec" should show D0

# Reset to default.
sudo sysctl hw.pci.do_power_suspend=1
sudo devctl resume myfirst0
```

La línea `powerspec` en la salida de `pciconf -lvbc` muestra el estado de energía actual. Observar cómo cambia entre D0 y D3 confirma que la transición automática está ocurriendo.

### Interacción con pci_save_state

Cuando `hw.pci.do_power_suspend` vale 1, la capa PCI llama automáticamente a `pci_cfg_save` antes de hacer la transición a D3. Cuando vale 0, la capa PCI no llama a `pci_cfg_save`.

Esto tiene una implicación sutil: si el driver quiere guardar la configuración de forma explícita cuando el valor es 0, debe llamar a `pci_save_state` él mismo. El patrón del Capítulo 22 asume el valor predeterminado (1) y no llama a `pci_save_state` de forma explícita; un driver que quiera admitir ambos modos necesitaría lógica adicional.

### ¿Afecta el parámetro configurable a la suspensión del sistema o a devctl suspend?

A ambos. `pci_suspend_child` se llama tanto para `acpiconf -s 3` como para `devctl suspend`, y el parámetro configurable controla la transición de estado D en ambos casos. Un lector que depure con `devctl suspend` verá el mismo comportamiento que con una suspensión completa del sistema, excepto por el resto del trabajo de plataforma (aparcamiento de CPU, entrada al estado de reposo ACPI).

### Un escenario de depuración concreto

Supón que el resume del driver `myfirst` falla de forma intermitente: a veces funciona, a veces `dma_test_read` tras el resume devuelve EIO. Los contadores son coherentes (suspend count = resume count), los registros de log muestran que ambos métodos se ejecutaron, pero el DMA posterior al resume falla.

**Hipótesis 1.** La transición de D3 a D0 está produciendo un estado inconsistente en el dispositivo. Verifica esto estableciendo `hw.pci.do_power_suspend=0` y volviendo a intentarlo.

Si el error desaparece con el parámetro en 0, la maquinaria de estado D está involucrada. La corrección podría estar en la ruta de resume del driver (añadir un retardo tras la transición para que el dispositivo se estabilice), en la restauración de configuración de la capa PCI o en el propio dispositivo.

**Hipótesis 2.** El error está en el propio código suspend/resume del driver, independientemente de D3. Verifica esto estableciendo el parámetro en 0 y volviendo a intentarlo.

Si el error persiste con el parámetro en 0, el código del driver es el problema. La transición D3 es inocente.

Este tipo de bisección es habitual en la depuración de la gestión de energía. El parámetro configurable es la herramienta que permite aislar la variable.



## Análisis en profundidad: DEVICE_QUIESCE y cuándo lo necesitas

La Sección 2 mencionó brevemente `DEVICE_QUIESCE` como el tercer método de gestión de energía, junto con `DEVICE_SUSPEND` y `DEVICE_SHUTDOWN`. Rara vez se implementa de forma explícita en los drivers de FreeBSD; una búsqueda en `/usr/src/sys/dev/` muestra que solo un puñado de drivers definen su propio `device_quiesce`. Vale la pena dedicar una breve sección a entender cuándo sí lo necesitas y cuándo no.

### Para qué sirve DEVICE_QUIESCE

El envoltorio `device_quiesce` en `/usr/src/sys/kern/subr_bus.c` se llama en varios lugares:

- `devclass_driver_deleted`: cuando se está descargando un driver, el framework llama a `device_quiesce` en cada instancia antes de llamar a `device_detach`.
- `DEV_DETACH` mediante devctl: cuando el usuario ejecuta `devctl detach myfirst0`, el kernel llama a `device_quiesce` antes de `device_detach`, salvo que se indique la opción `-f` (forzar).
- `DEV_DISABLE` mediante devctl: cuando el usuario ejecuta `devctl disable myfirst0`, el kernel llama a `device_quiesce` de forma similar.

En cada caso, el quiesce es una comprobación previa: «¿puede el driver detener de forma segura lo que está haciendo?». Un driver que devuelve EBUSY desde `DEVICE_QUIESCE` impide el detach o disable posterior. El usuario recibe un error y el driver permanece conectado.

### Qué hace la implementación predeterminada

Si un driver no implementa `DEVICE_QUIESCE`, la implementación predeterminada (`null_quiesce` en `device_if.m`) devuelve 0 de forma incondicional. El kernel procede con el detach o disable.

Para la mayoría de los drivers, esto es suficiente. La ruta de detach del driver gestiona cualquier trabajo en curso, por lo que no hay nada que el quiesce haría que el detach no haga también.

### Cuándo implementarlo

Un driver implementa `DEVICE_QUIESCE` de forma explícita cuando:

1. **Devolver EBUSY es más informativo que esperar.** Si el driver tiene el concepto de «ocupado» (una transferencia en curso, un contador de descriptores de archivo abiertos, un montaje de sistema de archivos) y el usuario puede esperar a que deje de estar ocupado, el driver puede rechazar el quiesce hasta que el indicador de ocupado sea cero. `DEVICE_QUIESCE` devolviendo EBUSY le dice al usuario «el dispositivo está ocupado; espera y vuelve a intentarlo».

2. **El quiesce puede realizarse más rápido que un detach completo.** Si el detach es costoso (libera grandes tablas de recursos, vacía colas lentas) pero el dispositivo puede detenerse de forma económica, `DEVICE_QUIESCE` permite al kernel comprobar la disponibilidad sin pagar el coste del detach.

3. **El driver quiere distinguir el quiesce del suspend.** Si el driver desea detener la actividad pero no guardar el estado (porque no vendrá ningún resume), implementar el quiesce por separado del suspend es una forma de expresar esa distinción en el código.

Para el driver `myfirst`, ninguno de estos casos aplica. La ruta de detach del Capítulo 21 ya gestiona el trabajo en curso; la ruta de suspend del Capítulo 22 gestiona el quiesce en el sentido de la gestión de energía. Añadir un `DEVICE_QUIESCE` separado sería redundante.

### Un ejemplo de bce(4)

El driver Broadcom NetXtreme en `/usr/src/sys/dev/bce/if_bce.c` tiene una entrada `DEVMETHOD(device_quiesce, bce_quiesce)` comentada en su tabla de métodos. El comentario sugiere que el autor consideró implementar el quiesce, pero no lo hizo. Esto es habitual: muchos drivers mantienen la línea comentada como un TODO que nunca llega a implementarse, porque la implementación predeterminada cubre su caso de uso.

La implementación, si el driver la habilitara, detendría las rutas TX y RX de la tarjeta de red sin liberar los recursos de hardware. Un `device_detach` posterior se encargaría entonces de la liberación real. La separación entre «detener» y «liberar» es lo que expresaría `DEVICE_QUIESCE`.

### Relación con DEVICE_SUSPEND

`DEVICE_QUIESCE` y `DEVICE_SUSPEND` hacen cosas similares: detienen la actividad del dispositivo. Las diferencias son:

- **Ciclo de vida**: el quiesce se produce entre la ejecución y el detach; el suspend, entre la ejecución y el eventual resume.
- **Recursos**: el quiesce no requiere que el driver guarde ningún estado; el suspend sí.
- **Capacidad de veto**: ambos pueden devolver EBUSY; las consecuencias difieren (el quiesce impide el detach; el suspend impide la transición de energía).

Un driver que implementa ambos suele compartir código: `foo_quiesce` podría hacer «detener la actividad» y `foo_suspend` podría hacer «llamar a quiesce; guardar estado; devolver». La función auxiliar `myfirst_quiesce` del driver `myfirst` es el código compartido; el capítulo no la conecta a un método `DEVICE_QUIESCE`, pero hacerlo sería una pequeña adición.

### Una adición opcional a myfirst

Como desafío, el lector puede añadir `DEVICE_QUIESCE` a `myfirst`:

```c
static int
myfirst_pci_quiesce(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        device_printf(dev, "quiesce: starting\n");
        (void)myfirst_quiesce(sc);
        atomic_add_64(&sc->power_quiesce_count, 1);
        device_printf(dev, "quiesce: complete\n");
        return (0);
}
```

Y la entrada correspondiente en la tabla de métodos:

```c
DEVMETHOD(device_quiesce, myfirst_pci_quiesce),
```

Para probarlo: `devctl detach myfirst0` llama a quiesce antes del detach; el lector puede verificarlo leyendo `dev.myfirst.0.power_quiesce_count` justo antes de que el detach tenga efecto.

El desafío es breve y no cambia la estructura general del driver; simplemente conecta un método más. La Etapa 4 consolidada del Capítulo 22 no lo incluye por defecto, pero el lector que quiera el método puede añadirlo en unas pocas líneas.



## Laboratorios prácticos

El Capítulo 22 incluye tres laboratorios prácticos que ejercitan la ruta de gestión de energía con dificultad progresiva. Cada laboratorio tiene un script en `examples/part-04/ch22-power/labs/` que el lector puede ejecutar tal cual, además de ideas de ampliación.

### Laboratorio 1: ciclo único de suspend-resume

El primer laboratorio es el más sencillo: un ciclo limpio de suspend-resume con verificación de contadores.

**Preparación.** Carga el driver de la Etapa 4 del Capítulo 22:

```sh
cd examples/part-04/ch22-power/stage4-final
make clean && make
sudo kldload ./myfirst.ko
```

Verifica el attach:

```sh
sysctl dev.myfirst.0.%driver
# Should return: myfirst
sysctl dev.myfirst.0.suspended
# Should return: 0
```

**Ejecución.** Ejecuta el script de ciclo:

```sh
sudo sh ../labs/ch22-suspend-resume-cycle.sh
```

Salida esperada:

```text
PASS: one suspend-resume cycle completed cleanly
```

**Verificación.** Inspecciona los contadores:

```sh
sysctl dev.myfirst.0.power_suspend_count
# Should return: 1
sysctl dev.myfirst.0.power_resume_count
# Should return: 1
```

Comprueba `dmesg`:

```sh
dmesg | tail -6
```

Debería mostrar cuatro líneas (inicio de suspend, suspend completo, inicio de resume, resume completo) más las líneas de log de transferencia anterior y posterior.

**Ampliación.** Modifica el script de ciclo para ejecutar dos ciclos de suspend-resume en lugar de uno, y verifica que los contadores se incrementan en exactamente 2 cada vez.

### Laboratorio 2: prueba de estrés de cien ciclos

El segundo laboratorio ejecuta el script de ciclo cien veces seguidas y comprueba que nada se desvía.

**Ejecución.**

```sh
sudo sh ../labs/ch22-suspend-stress.sh
```

Salida esperada tras unos pocos segundos:

```text
PASS: 100 cycles
```

**Verificación.** Tras la prueba de estrés, los contadores deberían ser 100 cada uno (o 100 más lo que hubiera antes):

```sh
sysctl dev.myfirst.0.power_suspend_count
# 100 (or however many cycles were added)
```

**Observaciones a realizar.**

- ¿Cuánto tarda un ciclo? En la simulación, debería ser unos pocos milisegundos. En hardware real con transiciones de estado D, espera entre unos pocos cientos de microsegundos y unos pocos milisegundos.
- ¿Cambia la carga media del sistema durante la prueba de estrés? La simulación es económica; cien ciclos en una máquina moderna apenas deberían notarse.
- ¿Qué ocurre si ejecutas la prueba de DMA durante la prueba de estrés? (`sudo sysctl dev.myfirst.0.dma_test_read=1` de forma concurrente con el bucle de ciclos.) Un driver bien escrito debería gestionar esto con elegancia; la prueba de DMA tiene éxito si ocurre durante una ventana `RUNNING` y falla con EBUSY o similar si ocurre durante una transición.

**Ampliación.** Ejecuta el script de estrés con `dmesg -c` antes para limpiar el log y luego:

```sh
dmesg | wc -l
```

Debería estar cerca de 400 (cuatro líneas de log por ciclo multiplicado por 100 ciclos). El recuento de líneas de log por ciclo permite verificar que cada ciclo se ejecutó realmente a través del driver.

### Laboratorio 3: transferencia a través de un ciclo

El tercer laboratorio es el más difícil: inicia una transferencia DMA e inmediatamente suspende en medio de ella, luego reanuda y verifica que el driver se recupera.

**Preparación.** El script del laboratorio es `ch22-transfer-across-cycle.sh`. Ejecuta una transferencia DMA en segundo plano, espera unos milisegundos, llama a `devctl suspend`, espera, llama a `devctl resume` y después inicia otra transferencia.

**Ejecución.**

```sh
sudo sh ../labs/ch22-transfer-across-cycle.sh
```

**Observaciones a realizar.**

- ¿Completa la primera transferencia, falla con error o agota el tiempo de espera? El comportamiento esperado es que el quiesce la aborte de forma limpia; la transferencia informa EIO o ETIMEDOUT.
- ¿Se incrementa el contador `dma_errors` o `dma_timeouts`? Uno de ellos debería hacerlo.
- ¿Vuelve `dma_in_flight` a false tras el suspend?
- ¿Tiene éxito la transferencia posterior al resume con normalidad? Si es así, el estado del driver es coherente y el ciclo funcionó.

**Ampliación.** Reduce el tiempo de espera entre el inicio de la transferencia y el suspend para dar con el caso límite en el que la transferencia está a medias en el momento del suspend. Ahí es donde viven las condiciones de carrera; un driver que supera esta prueba bajo una temporización agresiva tiene una implementación de quiesce sólida.

### Laboratorio 4: Runtime PM (opcional)

Para los lectores que compilan con `MYFIRST_ENABLE_RUNTIME_PM`, un cuarto laboratorio ejercita la ruta de runtime PM.

**Preparación.** Reconstruye con el runtime PM habilitado:

```sh
cd examples/part-04/ch22-power/stage4-final
# Uncomment the CFLAGS line in the Makefile:
#   CFLAGS+= -DMYFIRST_ENABLE_RUNTIME_PM
make clean && make
sudo kldload ./myfirst.ko
```

**Ejecución.**

```sh
sudo sh ../labs/ch22-runtime-pm.sh
```

El script:

1. Establece el umbral de inactividad en 3 segundos (en lugar del valor predeterminado de 5).
2. Registra los contadores de referencia.
3. Espera 5 segundos sin ninguna actividad.
4. Verifica que `runtime_state` es `RUNTIME_SUSPENDED`.
5. Desencadena una transferencia DMA.
6. Verifica que `runtime_state` ha vuelto a `RUNNING`.
7. Imprime PASS.

**Observaciones a realizar.**

- Durante la espera en inactividad, `dmesg` debería mostrar la línea de log «runtime suspend» aproximadamente a los 3 segundos.
- `runtime_suspend_count` y `runtime_resume_count` deberían ser 1 cada uno al final.
- La transferencia DMA debería tener éxito con normalidad tras el runtime resume.

**Ampliación.** Establece el umbral de inactividad en 1 segundo. Ejecuta la prueba de DMA de forma repetida en un bucle cerrado. No deberías ver ninguna transición de runtime suspend durante el bucle (porque cada prueba reinicia el temporizador de inactividad), pero en cuanto el bucle se detenga, el runtime suspend se disparará.

### Notas sobre los laboratorios

Todos los laboratorios asumen que el driver está cargado y el sistema está suficientemente inactivo como para que las transiciones ocurran a demanda. Si otro proceso está usando activamente el dispositivo (poco probable con `myfirst`, pero habitual en entornos reales), los contadores se desvían en cantidades inesperadas y las comprobaciones de incremento exacto de los scripts fallan. Los scripts están diseñados para un entorno de prueba tranquilo, no para uno ruidoso.

Para realizar pruebas realistas con el driver `re(4)` o con otros drivers de producción, la misma estructura de script se aplica ajustando el nombre del dispositivo. La secuencia `devctl suspend`/`devctl resume` funciona con cualquier dispositivo PCI que gestione el kernel.

## Ejercicios de desafío

Los ejercicios de desafío del capítulo 22 llevan al lector más allá del driver base, hacia terreno que los drivers del mundo real tienen que afrontar tarde o temprano. Cada ejercicio está diseñado para ser alcanzable con el material del capítulo y unas pocas horas de trabajo.

### Desafío 1: Implementa un mecanismo de activación mediante sysctl

Extiende el driver `myfirst` con una fuente de activación simulada. La simulación ya tiene un callout que puede dispararse; añade una nueva función a la simulación que establezca un bit de «activación» en el dispositivo mientras está en D3, y haz que el camino de `DEVICE_RESUME` del driver registre el evento de activación.

**Pistas.**

- Añade un registro `MYFIRST_REG_WAKE_STATUS` al backend de la simulación.
- Añade un registro `MYFIRST_REG_WAKE_ENABLE` que el driver escribe durante la suspensión.
- Haz que el callout de la simulación establezca el bit de estado de activación tras un retardo aleatorio.
- Al reanudar, el driver lee el registro y registra en el log si se observó una activación.

**Verificación.** Tras ejecutar `devctl suspend; sleep 1; devctl resume`, el log debe mostrar el estado de activación. Una llamada posterior a `sysctl dev.myfirst.0.wake_events` debe incrementar el contador.

**Por qué importa.** La gestión de fuentes de activación es una de las partes más complicadas de la gestión de energía en hardware real. Integrarla en la simulación permite al lector ejercitar el contrato completo sin necesitar hardware.

### Desafío 2: Guarda y restaura un anillo de descriptores

La simulación del capítulo 21 aún no usa un anillo de descriptores (las transferencias son de una en una). Extiende la simulación con un pequeño anillo de descriptores, programa su dirección base a través de un registro durante el attach, y haz que el camino de suspensión guarde la dirección base del anillo en el estado del softc. Haz que el camino de reanudación escriba de nuevo la dirección base guardada.

**Pistas.**

- La dirección base del anillo es un `bus_addr_t` almacenado en el softc.
- El registro es `MYFIRST_REG_RING_BASE_LOW`/`_HIGH`.
- Guardar y restaurar es trivial; el objetivo es verificar que *no* hacerlo rompe las cosas.

**Verificación.** Tras suspend-resume, el registro de dirección base del anillo debe contener el mismo valor que antes. Sin la restauración, debe contener cero.

**Por qué importa.** Los anillos de descriptores son lo que usan los drivers de alto rendimiento del mundo real; un driver consciente del consumo energético que utiliza un anillo tiene que restaurar la dirección base en cada reanudación. Este ejercicio es un peldaño hacia el tipo de gestión de estado que realizan drivers de producción como `re(4)` y `em(4)`.

### Desafío 3: Implementa una política de veto

Extiende el camino de suspensión con un control de política que permita al usuario especificar si el driver debe vetar una suspensión cuando el dispositivo está ocupado. En concreto:

- Añade `dev.myfirst.0.suspend_veto_if_busy` como sysctl de lectura-escritura.
- Si el sysctl vale 1 y hay una transferencia DMA en curso, `myfirst_power_suspend` devuelve EBUSY sin detener la actividad.
- Si el sysctl vale 0 (valor por defecto), la suspensión siempre tiene éxito.

**Pistas.** Establece `suspend_veto_if_busy` a 1. Inicia una transferencia DMA larga (añade un `DELAY` al motor de la simulación para que dure uno o dos segundos). Llama a `devctl suspend myfirst0` durante la transferencia. Verifica que la suspensión devuelve un error y que `dev.myfirst.0.suspended` se mantiene en 0.

**Verificación.** Se ejecuta el camino de retroceso del kernel; el driver sigue en `RUNNING`; la transferencia se completa con normalidad.

**Por qué importa.** El veto es una herramienta eficaz y también peligrosa. Las decisiones de política del mundo real sobre si vetar son matizadas (los drivers de almacenamiento suelen vetar; los drivers NIC generalmente no lo hacen). Implementar el mecanismo hace tangible la pregunta sobre la política.

### Desafío 4: Añade una autocomprobación posterior a la reanudación

Tras la reanudación, realiza una prueba mínima viable del dispositivo: escribe un patrón conocido en el buffer de DMA, dispara una transferencia de escritura, léelo de vuelta con una transferencia de lectura y verifica el resultado. Si la prueba falla, marca el dispositivo como defectuoso y rechaza las operaciones posteriores.

**Pistas.**

- Añade la autocomprobación como una función auxiliar que se ejecuta desde `myfirst_power_resume` después de `myfirst_restore`.
- Usa un patrón conocido como `0xDEADBEEF`.
- Usa el camino DMA existente; la autocomprobación es simplemente una escritura y una lectura.

**Verificación.** En condiciones normales, la autocomprobación siempre pasa. Para verificar que detecta fallos, añade un mecanismo artificial de «fallo una vez» a la simulación y dispáralo; el driver debe registrar el fallo y marcarse como defectuoso.

**Por qué importa.** Las autocomprobaciones son una forma ligera de ingeniería de fiabilidad. Un driver que detecta sus propios fallos en puntos bien definidos es más fácil de depurar que uno que corrompe datos en silencio hasta que un usuario se da cuenta.

### Desafío 5: Implementa pci_save_state / pci_restore_state de forma manual

La mayoría de los drivers dejan que la capa PCI gestione automáticamente el guardado y la restauración del espacio de configuración. Extiende el driver del capítulo 22 para hacerlo de forma manual de manera opcional, controlado por el sysctl `dev.myfirst.0.manual_pci_save`.

**Pistas.**

- Lee `hw.pci.do_power_suspend` y `hw.pci.do_power_resume` y ponlos a 0 cuando el modo manual esté habilitado.
- Llama a `pci_save_state` explícitamente en el camino de suspensión, y a `pci_restore_state` en el camino de reanudación.
- Verifica que el dispositivo sigue funcionando tras suspend-resume.

**Verificación.** El dispositivo debe funcionar de forma idéntica tanto si el modo manual está habilitado como si no. Activa el sysctl antes de una prueba de estrés y verifica que no hay desviaciones.

**Por qué importa.** Algunos drivers reales necesitan el guardado y la restauración manuales porque la gestión automática de la capa PCI interfiere con las particularidades específicas del dispositivo. Saber cuándo y cómo asumir el control del guardado y la restauración es una habilidad intermedia muy útil.



## Referencia para la resolución de problemas

Esta sección recoge los problemas habituales que el lector puede encontrar al trabajar con el capítulo 22, con un diagnóstico breve y una solución para cada uno. La lista está pensada para hojearla; si un problema coincide, ve directamente a la entrada correspondiente.

### "devctl: DEV_SUSPEND failed: Operation not supported"

El driver no implementa `DEVICE_SUSPEND`. O bien la tabla de métodos no tiene la línea `DEVMETHOD(device_suspend, ...)`, o bien el driver no ha sido reconstruido y recargado.

**Solución.** Comprueba la tabla de métodos. Reconstruye con `make clean && make`. Descarga y vuelve a cargar.

### "devctl: DEV_SUSPEND failed: Device busy"

El driver devolvió `EBUSY` desde `DEVICE_SUSPEND`, probablemente por la lógica de veto del desafío 3, o porque el dispositivo está genuinamente ocupado (DMA en curso, tarea en ejecución) y el driver decidió vetar.

**Solución.** Comprueba si el control `suspend_veto_if_busy` está activado. Revisa `dma_in_flight`. Espera a que la actividad termine antes de suspender.

### "devctl: DEV_RESUME failed"

`DEVICE_RESUME` devolvió un valor distinto de cero. El log debería tener más detalle.

**Solución.** Comprueba `dmesg | tail`. La línea del log de reanudación debería indicar qué falló. Normalmente es un paso de inicialización específico del hardware que no tuvo éxito.

### El dispositivo está suspendido pero `dev.myfirst.0.suspended` muestra 0

El indicador del driver está desincronizado con el estado del kernel. Probablemente hay un error en el camino de quiesce: el indicador nunca se estableció, o se borró de forma prematura.

**Solución.** Añade un `KASSERT(sc->suspended == true)` al principio del camino de reanudación; ejecuta con `INVARIANTS` para detectar el error.

### `power_suspend_count != power_resume_count`

Un ciclo completó uno de los lados pero no el otro. Revisa `dmesg` en busca de errores; el log debería mostrar dónde se rompió la secuencia.

**Solución.** Corrige el camino de código que falta. Normalmente es un retorno prematuro sin la actualización del contador.

### Las transferencias DMA fallan tras la reanudación

El camino de restauración no reinicializó el motor DMA. Comprueba el registro INTR_MASK, los registros de control DMA y el valor `saved_intr_mask`. Activa el registro detallado para ver la secuencia de restauración del camino de reanudación.

**Solución.** Añade la escritura de registro que falta en `myfirst_restore`.

### WITNESS se queja de un lock retenido durante la suspensión

El camino de suspensión adquirió un lock y después llamó a una función que duerme o intenta adquirir otro lock. Lee el mensaje de WITNESS para obtener los nombres de los locks implicados.

**Solución.** Libera el lock antes de la llamada que duerme, o reestructura el código para que el lock se adquiera solo cuando sea necesario.

### El sistema no se activa desde S3

Algún driver por debajo de `myfirst` está bloqueando la reanudación. Es poco probable que sea el propio `myfirst` a menos que los logs muestren un error específico de ese driver.

**Solución.** Arranca en modo monousuario, o carga menos drivers, y localiza el problema por bisección. Comprueba `dmesg` en el sistema en ejecución para identificar el driver problemático.

### El runtime PM nunca se activa

El callout del monitor de inactividad no está en ejecución, o el timestamp `last_activity` se está actualizando con demasiada frecuencia.

**Solución.** Verifica que `callout_reset` se llama desde el camino de attach. Verifica que `myfirst_mark_active` no se llama desde caminos de código inesperados. Añade registro al callback del callout para confirmar que se dispara.

### Pánico del kernel durante la suspensión

Un KASSERT falló (en un kernel con `INVARIANTS`) o un lock se retiene de forma incorrecta. El mensaje de pánico identifica el archivo y la línea implicados.

**Solución.** Lee el mensaje de pánico. Localiza el archivo y la línea en el código. La solución suele ser sencilla una vez identificado el punto exacto.



## En resumen

El capítulo 22 cierra la Parte 4 dotando al driver `myfirst` de la disciplina de la gestión de energía. Al comienzo, `myfirst` en la versión `1.4-dma` era un driver capaz: se enganchaba a un dispositivo PCI, gestionaba interrupciones multivector, movía datos mediante DMA y limpiaba sus recursos durante el detach. Lo que le faltaba era la capacidad de participar en las transiciones de energía del sistema. Caería, perdería recursos o fallaría en silencio si el usuario cerraba la tapa del portátil o pedía al kernel que suspendiera el dispositivo. Al final, `myfirst` en la versión `1.5-power` gestiona todas las transiciones de energía que el kernel pueda lanzarle: suspensión del sistema a S3 o S4, suspensión por dispositivo mediante `devctl`, apagado del sistema y gestión de energía en tiempo de ejecución opcional.

Las ocho secciones recorrieron la progresión completa. La sección 1 estableció el panorama general: por qué un driver se preocupa por la energía, qué son los estados S de ACPI y los estados D de PCI, qué añaden los estados L de PCIe y ASPM, y cómo son las fuentes de activación. La sección 2 presentó las APIs concretas de FreeBSD: los métodos `DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN` y `DEVICE_QUIESCE`, las funciones auxiliares `bus_generic_suspend` y `bus_generic_resume`, y el guardado y la restauración automáticos del espacio de configuración por parte de la capa PCI. El esqueleto de la etapa 1 hizo que los métodos registraran y contaran transiciones sin realizar ningún trabajo real. La sección 3 convirtió el esqueleto de suspensión en un quiesce real: enmascarar interrupciones, vaciar el DMA, vaciar los workers, en ese orden, con funciones auxiliares compartidas entre la suspensión y el detach. La sección 4 escribió el camino de reanudación correspondiente: reactivar bus-master, restaurar el estado específico del dispositivo, limpiar el indicador de suspensión, desenmascarar las interrupciones. La sección 5 añadió gestión de energía en tiempo de ejecución opcional con un callout monitor de inactividad y transiciones explícitas de `pci_set_powerstate`. La sección 6 repasó la interfaz del espacio de usuario: `acpiconf`, `zzz`, `devctl suspend`, `devctl resume`, `devinfo -v`, y los sysctls correspondientes. La sección 7 catalogó los modos de fallo característicos y sus patrones de depuración. La sección 8 refactorizó el código en `myfirst_power.c`, incrementó la versión a `1.5-power`, añadió `POWER.md` y conectó la prueba de regresión final.

Lo que el capítulo 22 no hizo es la gestión de energía scatter-gather para drivers de múltiples colas (ese es un tema de la Parte 6, capítulo 28), la integración de hotplug y extracción inesperada (un tema de la Parte 7), los dominios de energía de plataformas embebidas (Parte 7 de nuevo), ni el funcionamiento interno del intérprete AML de ACPI (nunca tratado en este libro). Cada uno de ellos es una extensión natural construida sobre las primitivas del capítulo 22, y cada uno pertenece a un capítulo posterior donde el alcance encaja. Los cimientos están puestos; las especializaciones añaden vocabulario sin necesitar unos nuevos cimientos.

La estructura de archivos ha crecido: 16 archivos fuente (incluido `cbuf`), 8 archivos de documentación (`HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`, `DMA.md`, `POWER.md`) y una suite de regresión ampliada que cubre todos los subsistemas. El driver es estructuralmente paralelo a los drivers de FreeBSD en producción; un lector que haya trabajado los capítulos del 16 al 22 puede abrir `if_re.c`, `if_xl.c` o `virtio_blk.c` y reconocer cada parte arquitectónica: accesores de registros, backend de simulación, PCI attach, filtro de interrupción y tarea, maquinaria por vector, configuración y liberación de DMA, disciplina de sincronización, power suspend, power resume, detach limpio.

### Una reflexión antes del capítulo 23

El capítulo 22 es el último capítulo de la parte 4, y la parte 4 es la parte que enseñó al lector cómo un driver habla con el hardware. Los capítulos 16 al 21 presentaron los primitivos: MMIO, simulación, PCI, interrupciones, interrupciones multivector, DMA. El capítulo 22 introdujo la disciplina: cómo esos primitivos sobreviven las transiciones de energía. En conjunto, los siete capítulos llevan al lector desde "no tengo idea de qué es un driver" hasta "un driver multisistema funcional que gestiona cualquier evento de hardware que el kernel pueda lanzarle".

La enseñanza del capítulo 22 se generaliza. Un lector que haya interiorizado el patrón suspend-quiesce-save-restore, la interacción entre el driver y la capa PCI, la máquina de estados del runtime PM y los patrones de depuración encontrará formas similares en cualquier driver FreeBSD que gestione la energía. El hardware específico difiere; la estructura no. Un driver para una NIC, un controlador de almacenamiento, una GPU o un controlador USB aplica el mismo vocabulario a su propio hardware.

La parte 5, que comienza con el capítulo 23, cambia el enfoque. La parte 4 trataba la dirección driver-a-hardware: cómo el driver habla con el dispositivo. La parte 5 trata la dirección driver-a-kernel: cómo el driver es depurado, trazado, instrumentado y sometido a pruebas de estrés por los programadores que lo mantienen. El capítulo 23 inicia ese cambio con técnicas de depuración y trazado que se aplican a todos los subsistemas de drivers.

### Qué hacer si estás atascado

Tres sugerencias.

Primera: céntrate en las rutas de suspend de la etapa 2 y de resume de la etapa 3. Si `devctl suspend myfirst0` seguido de `devctl resume myfirst0` tiene éxito y una transferencia DMA posterior funciona, el núcleo del capítulo está funcionando. Cada otra pieza del capítulo es opcional en el sentido de que decora el pipeline, pero si el pipeline falla, el capítulo no funciona y la sección 3 o la sección 4 son el lugar adecuado para diagnosticar.

Segunda: abre `/usr/src/sys/dev/re/if_re.c` y vuelve a leer `re_suspend`, `re_resume` y `re_setwol`. Cada función tiene unas treinta líneas. Cada línea se corresponde con un concepto del capítulo 22. Leerlas una vez después de completar el capítulo debería resultar familiar; los patrones del driver real parecerán elaboraciones de los más sencillos del capítulo.

Tercera: omite los desafíos en el primer repaso. Los laboratorios están calibrados para el ritmo del capítulo 22; los desafíos asumen que el material del capítulo está asentado. Vuelve a ellos después del capítulo 23 si ahora te parecen fuera de alcance.

El objetivo del capítulo 22 era dar al driver la disciplina de gestión de energía. Si lo ha conseguido, la maquinaria de depuración y trazado del capítulo 23 se convierte en una generalización de lo que ya haces de forma instintiva, en lugar de ser un tema nuevo.

## Punto de control de la parte 4

La parte 4 ha sido el trecho más largo y denso del libro hasta ahora. Siete capítulos cubrieron recursos de hardware, E/S de registros, attach PCI, interrupciones, MSI y MSI-X, DMA y gestión de energía. Antes de que la parte 5 cambie el modo de "escribir drivers" a "depurarlos y trazarlos", confirma que la historia del lado del hardware está interiorizada.

Al final de la parte 4 deberías ser capaz de hacer cada uno de los siguientes sin necesidad de buscarlo:

- Reclamar un recurso de hardware con `bus_alloc_resource_any` o `bus_alloc_resource_anywhere`, acceder a él a través de los primitivos de lectura/escritura y barrera de `bus_space(9)`, y liberarlo correctamente en detach.
- Leer y escribir registros del dispositivo a través de la abstracción `bus_space(9)` en lugar de desreferenciar punteros crudos, con la disciplina de barrera correcta en torno a secuencias que no deben reordenarse.
- Hacer coincidir un dispositivo PCI mediante los IDs de vendor, device, subvendor y subdevice; reclamar sus BARs; y sobrevivir a un detach forzado sin fugas de recursos.
- Registrar un filtro de la mitad superior junto con una tarea de la mitad inferior o un ithread mediante `bus_setup_intr`, en el orden que requiere el kernel, y desmontarlos en orden inverso durante el detach.
- Configurar vectores MSI o MSI-X con una escalera de retroceso elegante desde MSI-X hasta MSI y hasta INTx legacy, y vincular vectores a CPUs específicas cuando la carga de trabajo lo requiera.
- Asignar, mapear, sincronizar y liberar buffers DMA usando `bus_dma(9)`, incluido el caso de bounce-buffer.
- Implementar `device_suspend` y `device_resume` con guardado y restauración de registros, quiescing de E/S y una autoprueba posterior al resume.

Si alguno de esos todavía requiere una búsqueda, los laboratorios a revisar son:

- Registros y barreras: Lab 1 (Observa la danza de registros) y Lab 8 (El escenario watchdog con registros) en el capítulo 16.
- Hardware simulado bajo carga: Lab 6 (Inyecta un Stuck-Busy y observa cómo espera el driver) y Lab 10 (Inyecta un ataque de corrupción de memoria) en el capítulo 17.
- Attach y detach PCI: Lab 4 (Reclama el BAR y lee un registro) y Lab 5 (Ejercita el cdev y verifica la limpieza del detach) en el capítulo 18.
- Manejo de interrupciones: Lab 3 (Etapa 2, filtro real y tarea diferida) en el capítulo 19.
- MSI y MSI-X: Lab 4 (Etapa 3, MSI-X con vinculación a CPU) en el capítulo 20.
- DMA: Lab 4 (Etapa 3, finalización impulsada por interrupciones) y Lab 5 (Etapa 4, refactorización y regresión) en el capítulo 21.
- Gestión de energía: Lab 2 (Estrés de cien ciclos) y Lab 3 (Transferencia a través de un ciclo) en el capítulo 22.

La parte 5 esperará lo siguiente como base:

- Un driver con capacidad de hardware y observabilidad ya incorporada: contadores, sysctls y llamadas a `devctl_notify` en las transiciones importantes. La maquinaria de depuración del capítulo 23 funciona mejor cuando el driver ya se informa a sí mismo.
- Un script de regresión que pueda ciclar el driver de forma fiable, ya que la parte 5 convierte la reproducibilidad en una habilidad de primer orden.
- Un kernel construido con `INVARIANTS` y `WITNESS`. La parte 5 se apoya en ambos incluso con más intensidad que la parte 4, especialmente en el capítulo 23.
- La comprensión de que un bug en el código de un driver es un bug en el código del kernel, lo que significa que los depuradores del espacio de usuario solos no serán suficientes y la parte 5 enseñará las herramientas del espacio del kernel.

Si todo eso se cumple, la parte 5 está lista para ti. Si algo todavía parece inestable, un repaso rápido del laboratorio correspondiente te devolverá con creces el tiempo invertido.

## Puente hacia el capítulo 23

El capítulo 23 se titula *Depuración y trazado*. Su alcance es la práctica profesional de encontrar bugs en los drivers: herramientas como `ktrace`, `ddb`, `kgdb`, `dtrace` y `procstat`; técnicas para analizar pánicas del kernel, deadlocks y corrupción de datos; estrategias para convertir informes vagos de usuarios en casos de prueba reproducibles; y la mentalidad de un desarrollador de drivers que tiene que depurar código que se ejecuta en el espacio del kernel con visibilidad limitada.

El capítulo 22 preparó el terreno de cuatro maneras específicas.

Primera, **tienes contadores de observabilidad en todas partes**. El driver del capítulo 22 expone contadores de suspend, resume, shutdown y runtime PM a través de sysctls. Las técnicas de depuración del capítulo 23 dependen de la observabilidad; un driver que ya rastrea su propio estado es mucho más fácil de depurar que uno que no lo hace.

Segunda, **tienes una prueba de regresión**. Los scripts de ciclo y estrés de la sección 6 son un primer indicio de lo que el capítulo 23 amplía: la capacidad de reproducir un bug a demanda. Un bug que no puedes reproducir es un bug que no puedes corregir; los scripts del capítulo 22 son una base para las pruebas más exhaustivas que el capítulo 23 añade.

Tercera, **tienes un kernel de depuración con `INVARIANTS` / `WITNESS` funcionando**. El capítulo 22 se apoyó en ambos a lo largo de todo el capítulo; el capítulo 23 parte del mismo kernel para sesiones de `ddb`, análisis post-mortem y reproducción de caídas del kernel.

Cuarta, **comprendes que los bugs en el código de un driver son bugs en el código del kernel**. El capítulo 22 se encontró con cuelgues, dispositivos congelados, interrupciones perdidas y quejas de WITNESS. Cada uno de ellos es un bug del kernel en el sentido visible para el usuario; cada uno requiere un enfoque de depuración en el espacio del kernel. El capítulo 23 enseña ese enfoque de forma sistemática.

Temas específicos que cubrirá el capítulo 23:

- Usar `ktrace` y `kdump` para observar en tiempo real la traza de llamadas al sistema de un proceso.
- Usar `ddb` para entrar en el depurador del kernel para un análisis post-mortem o una inspección en vivo.
- Usar `kgdb` con un volcado de memoria para recuperar el estado de un kernel que ha caído.
- Usar `dtrace` para el trazado dentro del kernel sin modificar el código fuente.
- Usar `procstat`, `top`, `pmcstat` y herramientas relacionadas para la observación del rendimiento.
- Estrategias para minimizar un bug: simplificar el reproductor, biseccionar una regresión, formular hipótesis y probarlas.
- Patrones para instrumentar un driver en producción sin alterar su comportamiento.

No necesitas leer por adelantado. El capítulo 22 es preparación suficiente. Trae tu driver `myfirst` en la versión `1.5-power`, tu `LOCKING.md`, tu `INTERRUPTS.md`, tu `MSIX.md`, tu `DMA.md`, tu `POWER.md`, tu kernel habilitado con `WITNESS` y tu script de regresión. El capítulo 23 empieza donde terminó el capítulo 22.

La parte 4 está completa. El capítulo 23 abre la parte 5 añadiendo la disciplina de observabilidad y depuración que separa un driver que escribiste la semana pasada de uno que puedes mantener durante años.

El vocabulario es tuyo; la estructura es tuya; la disciplina es tuya. El capítulo 23 añade la siguiente pieza que faltaba: la capacidad de encontrar y corregir bugs que solo aparecen en producción.

---

## Referencia: tarjeta de referencia rápida del capítulo 22

Un resumen compacto del vocabulario, las APIs, los flags y los procedimientos que introdujo el capítulo 22.

### Vocabulario

- **Suspend:** una transición desde D0 (operación completa) a un estado de menor consumo desde el que se puede restaurar el dispositivo.
- **Resume:** la transición de vuelta desde el estado de menor consumo a D0.
- **Shutdown:** la transición a un estado final desde el que el dispositivo no volverá.
- **Quiesce:** llevar un dispositivo a un estado sin actividad ni trabajo pendiente.
- **Estado de reposo del sistema (S0, S1, S3, S4, S5):** niveles de energía del sistema definidos por ACPI.
- **Estado de energía del dispositivo (D0, D1, D2, D3hot, D3cold):** niveles de energía del dispositivo definidos por PCI.
- **Estado del enlace (L0, L0s, L1, L1.1, L1.2, L2):** niveles de energía del enlace definidos por PCIe.
- **ASPM (Active-State Power Management):** transiciones automáticas entre L0 y L0s/L1.
- **PME# (Power Management Event):** señal que el dispositivo activa cuando quiere despertar el sistema.
- **Fuente de wakeup:** mecanismo por el que un dispositivo suspendido puede solicitar el despertar.
- **Runtime PM:** ahorro de energía a nivel de dispositivo mientras el sistema permanece en S0.

### Métodos Kobj esenciales

- `DEVMETHOD(device_suspend, foo_suspend)`: se llama para poner en quiesce el dispositivo antes de una transición de energía.
- `DEVMETHOD(device_resume, foo_resume)`: se llama para restaurar el dispositivo después de la transición de energía.
- `DEVMETHOD(device_shutdown, foo_shutdown)`: se llama para dejar el dispositivo en un estado seguro para el reinicio.
- `DEVMETHOD(device_quiesce, foo_quiesce)`: se llama para detener la actividad sin desmontar los recursos.

### APIs PCI esenciales

- `pci_has_pm(dev)`: devuelve verdadero si el dispositivo tiene una capacidad de gestión de energía.
- `pci_set_powerstate(dev, state)`: transiciona a `PCI_POWERSTATE_D0`, `D1`, `D2` o `D3`.
- `pci_get_powerstate(dev)`: estado de energía actual.
- `pci_save_state(dev)`: guarda en caché el espacio de configuración.
- `pci_restore_state(dev)`: escribe de nuevo en el espacio de configuración el contenido guardado en caché.
- `pci_enable_pme(dev)`: habilita la generación de PME#.
- `pci_clear_pme(dev)`: limpia el estado PME pendiente.
- `pci_enable_busmaster(dev)`: vuelve a habilitar el bus-master después de un reset.

### Helpers de bus esenciales

- `bus_generic_suspend(dev)`: suspende todos los hijos en orden inverso.
- `bus_generic_resume(dev)`: reanuda todos los hijos en orden directo.
- `device_quiesce(dev)`: llama al `DEVICE_QUIESCE` del driver.

### Sysctls esenciales

- `hw.acpi.supported_sleep_state`: lista de S-states que soporta la plataforma.
- `hw.acpi.suspend_state`: S-state por defecto para `zzz`.
- `hw.pci.do_power_suspend`: transición automática D0->D3 en suspend.
- `hw.pci.do_power_resume`: transición automática D3->D0 en resume.
- `dev.N.M.suspended`: flag de suspensión propio del driver.
- `dev.N.M.power_suspend_count`, `power_resume_count`, `power_shutdown_count`.
- `dev.N.M.runtime_state`, `runtime_suspend_count`, `runtime_resume_count`.

### Comandos útiles

- `acpiconf -s 3`: entra en S3.
- `zzz`: wrapper de `acpiconf`.
- `devctl suspend <device>`: suspend por dispositivo.
- `devctl resume <device>`: resume por dispositivo.
- `devinfo -v`: árbol de dispositivos con estado.
- `pciconf -lvbc`: dispositivos PCI con estado de energía.
- `sysctl -a | grep acpi`: todas las variables relacionadas con ACPI.

### Procedimientos comunes

**Adición a la tabla de métodos:**

```c
DEVMETHOD(device_suspend,  foo_suspend),
DEVMETHOD(device_resume,   foo_resume),
DEVMETHOD(device_shutdown, foo_shutdown),
```

**Esqueleto de suspend:**

```c
int foo_suspend(device_t dev) {
    struct foo_softc *sc = device_get_softc(dev);
    FOO_LOCK(sc);
    sc->suspended = true;
    FOO_UNLOCK(sc);
    foo_mask_interrupts(sc);
    foo_drain_dma(sc);
    foo_drain_workers(sc);
    return (0);
}
```

**Esqueleto de resume:**

```c
int foo_resume(device_t dev) {
    struct foo_softc *sc = device_get_softc(dev);
    pci_enable_busmaster(dev);
    foo_restore_registers(sc);
    FOO_LOCK(sc);
    sc->suspended = false;
    FOO_UNLOCK(sc);
    foo_unmask_interrupts(sc);
    return (0);
}
```

**Función auxiliar de Runtime-PM:**

```c
int foo_runtime_suspend(struct foo_softc *sc) {
    foo_quiesce(sc);
    pci_save_state(sc->dev);
    return (pci_set_powerstate(sc->dev, PCI_POWERSTATE_D3));
}

int foo_runtime_resume(struct foo_softc *sc) {
    pci_set_powerstate(sc->dev, PCI_POWERSTATE_D0);
    pci_restore_state(sc->dev);
    return (foo_restore(sc));
}
```

### Archivos que conviene tener marcados

- `/usr/src/sys/kern/device_if.m`: las definiciones de métodos kobj.
- `/usr/src/sys/kern/subr_bus.c`: `bus_generic_suspend`, `bus_generic_resume`, `device_quiesce`.
- `/usr/src/sys/dev/pci/pci.c`: `pci_suspend_child`, `pci_resume_child`, `pci_save_state`, `pci_restore_state`.
- `/usr/src/sys/dev/pci/pcivar.h`: las constantes `PCI_POWERSTATE_*` y la API inline.
- `/usr/src/sys/dev/re/if_re.c`: referencia de producción para suspend/resume con WoL.
- `/usr/src/sys/dev/xl/if_xl.c`: patrón mínimo de suspend/resume.
- `/usr/src/sys/dev/virtio/block/virtio_blk.c`: quiesce al estilo virtio.



## Referencia: Glosario de términos del capítulo 22

Un breve glosario de los nuevos términos del capítulo.

- **ACPI (Advanced Configuration and Power Interface):** la interfaz estándar del sector entre el sistema operativo y el firmware de la plataforma para la gestión de energía.
- **ASPM (Active-State Power Management):** transiciones automáticas de estado de enlace PCIe.
- **D-state:** un estado de energía del dispositivo (D0 a D3cold).
- **DEVICE_QUIESCE:** el método kobj que detiene la actividad sin desmantelar los recursos.
- **DEVICE_RESUME:** el método kobj que se invoca para restaurar el dispositivo a su estado operativo.
- **DEVICE_SHUTDOWN:** el método kobj que se invoca durante el apagado del sistema.
- **DEVICE_SUSPEND:** el método kobj que se invoca para llevar el dispositivo a quiesce antes de una transición de energía.
- **GPE (General-Purpose Event):** una fuente de eventos de activación de ACPI.
- **L-state:** un estado de energía de enlace PCIe.
- **Link state machine:** las transiciones automáticas entre L0 y L0s/L1.
- **PME# (Power Management Event):** la señal PCI que un dispositivo activa para solicitar su reactivación.
- **Power management capability:** la estructura de capacidad PCI que contiene los registros PM.
- **Quiesce:** llevar un dispositivo a un estado sin actividad ni trabajo pendiente.
- **Runtime PM:** ahorro de energía a nivel de dispositivo mientras el sistema permanece en S0.
- **S-state:** un estado de reposo del sistema ACPI (S0 a S5).
- **Shutdown:** apagado definitivo, que lleva típicamente a un reinicio o a la desconexión de la corriente.
- **Sleep state:** véase S-state.
- **Suspend:** apagado temporal del que el sistema o el dispositivo puede recuperarse.
- **Suspended flag:** un flag local del driver que indica que el dispositivo se encuentra en estado suspendido.
- **Wake source:** un mecanismo mediante el cual un sistema o dispositivo suspendido puede reactivarse.
- **WoL (Wake on LAN):** una fuente de activación desencadenada por un paquete de red.



## Referencia: Una nota final sobre la filosofía de la gestión de energía

Un párrafo para cerrar el capítulo.

La gestión de energía es la disciplina que separa un driver prototipo de un driver de producción. Antes de incorporarla, un driver asume que su dispositivo está siempre encendido y siempre disponible. Después de incorporarla, el driver sabe que el dispositivo puede ponerse en reposo y sabe cómo hacerlo correctamente, y puede confiarse en los entornos que usan los usuarios reales: portátiles que se abren y cierran decenas de veces al día, servidores que suspenden dispositivos inactivos para ahorrar energía, VMs que migran entre hosts, sistemas embebidos que apagan dominios de alimentación enteros para prolongar la duración de la batería.

La lección del capítulo 22 es que la gestión de energía es disciplinada, no mágica. El kernel de FreeBSD ofrece al driver un contrato específico (los cuatro métodos kobj, el orden de invocación, la interacción con la capa PCI), y respetar ese contrato constituye la mayor parte del trabajo. El resto es específico del hardware: entender qué registros pierde el dispositivo durante una transición de D-state, qué wake sources admite el hardware y qué política debe aplicar el driver para el runtime PM. El patrón es el mismo en todos los drivers con gestión de energía de FreeBSD; interiorizarlo una sola vez resulta rentable a lo largo de decenas de capítulos posteriores y miles de líneas de código real de drivers.

Para ti y para los futuros lectores de este libro, el patrón de gestión de energía del capítulo 22 es una parte permanente de la arquitectura del driver `myfirst` y una herramienta permanente en tu caja de herramientas. El capítulo 23 lo da por supuesto: depurar un driver implica que ese driver ya cuenta con los contadores de observabilidad y el ciclo de vida estructurado que el capítulo 22 introdujo. Los capítulos de especialización de la Parte 6 también lo dan por supuesto: todos los drivers de producción tienen una ruta de gestión de energía. Los capítulos de rendimiento de la Parte 7 (capítulo 33) también lo asumen: cualquier medición de ajuste debe tener en cuenta las transiciones de estado de energía. El vocabulario es el vocabulario que comparten todos los drivers de producción de FreeBSD; los patrones son los patrones que siguen los drivers de producción; y la disciplina es la disciplina que mantiene correctas las plataformas con gestión de energía.

La habilidad que enseña el capítulo 22 no es «cómo añadir métodos suspend y resume a un único driver PCI». Es «cómo pensar en el ciclo de vida de un driver como attach, run, quiesce, sleep, wake, run y, finalmente, detach, en lugar de simplemente attach, run, detach». Esa habilidad se aplica a cada driver en el que alguna vez trabajes.

La Parte 4 está completa. El driver `myfirst` está en la versión `1.5-power`, estructuralmente equivalente a un driver de producción de FreeBSD, y listo para los capítulos de depuración, herramientas y especialización que siguen en las Partes 5 y 6.
