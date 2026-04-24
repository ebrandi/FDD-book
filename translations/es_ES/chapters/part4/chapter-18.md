---
title: "Escribir un driver PCI"
description: "El capítulo 18 transforma el driver myfirst simulado en un driver PCI real. Explica la topología PCI, cómo FreeBSD enumera los dispositivos PCI y PCIe, cómo un driver realiza el probe y el attach por identificador de fabricante y de dispositivo, cómo los BAR se convierten en tags y handles de bus_space a través de bus_alloc_resource_any, cómo la inicialización en tiempo de attach avanza sobre un BAR real, cómo la simulación del capítulo 17 permanece inactiva en la ruta PCI y cómo una ruta de detach limpia desmonta el attach completo en orden inverso. El driver pasa de la versión 1.0-simulated a la 1.1-pci, adquiere un nuevo archivo específico de PCI y deja el capítulo 18 listo para albergar un manejador de interrupciones real en el capítulo 19."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 18
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 225
language: "es-ES"
---
# Escribir un driver PCI

## Guía para el lector y objetivos

El Capítulo 17 concluyó con un driver que parecía un dispositivo real desde el exterior y se comportaba como uno desde el interior. El módulo `myfirst` en la versión `1.0-simulated` incorpora un bloque de registros, una capa de acceso basada en `bus_space(9)`, un backend de simulación con callouts que producen cambios de estado autónomos, un framework de inyección de fallos, un protocolo de comando-respuesta, contadores de estadísticas y tres archivos de documentación activos (`HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`). Todos los accesos a registros del driver siguen pasando por `CSR_READ_4(sc, off)`, `CSR_WRITE_4(sc, off, val)` y `CSR_UPDATE_4(sc, off, clear, set)`. La capa de hardware (`myfirst_hw.c` y `myfirst_hw.h`) es una envoltura ligera que produce un tag y un handle, y la capa de simulación (`myfirst_sim.c` y `myfirst_sim.h`) es lo que da vida a esos registros. El propio driver no sabe si el bloque de registros es silicio real o una asignación de `malloc(9)`.

Esa ambigüedad es el regalo que nos hicieron los Capítulos 16 y 17, y el Capítulo 18 es donde finalmente lo aprovechamos. El driver se encontrará ahora con hardware PCI real. El bloque de registros ya no provendrá del heap del kernel; provendrá del Base Address Register de un dispositivo, asignado en el boot por el firmware, mapeado por el kernel en un rango de direcciones virtuales con atributos de memoria de dispositivo, y entregado al driver como un `struct resource *`. La capa de acceso no cambia. Una opción en tiempo de compilación mantiene la simulación del Capítulo 17 disponible como un build separado para los lectores que no dispongan de un entorno de prueba PCI; en el build PCI los callouts de simulación no se ejecutan, de modo que no puedan escribir accidentalmente en los registros del dispositivo real. Lo que cambia es el punto en el que se originan el tag y el handle: en lugar de ser producidos por `malloc(9)`, los producirá `bus_alloc_resource_any(9)` contra un hijo PCI en el árbol de newbus.

El alcance del Capítulo 18 es precisamente esta transición. Enseña qué es PCI, cómo representa FreeBSD un dispositivo PCI en el árbol de newbus, cómo un driver reconoce un dispositivo por vendor ID y device ID, cómo los BARs aparecen en el espacio de configuración y se convierten en recursos `bus_space`, cómo avanza la inicialización en tiempo de attach sobre un BAR real sin perturbar el dispositivo y cómo el detach deshace todo en orden inverso. Cubre los accesores del espacio de configuración `pci_read_config(9)` y `pci_write_config(9)`, los recorredores de capacidades `pci_find_cap(9)` y `pci_find_extcap(9)`, y una breve introducción al PCIe Advanced Error Reporting para que el lector sepa dónde se encuentra sin que se le pida gestionarlo todavía. Concluye con una refactorización pequeña pero significativa que separa el nuevo código específico de PCI en su propio archivo, versiona el driver como `1.1-pci` y ejecuta la batería completa de regresión tanto contra el build de simulación como contra el build PCI real.

El Capítulo 18 se limita deliberadamente a la danza probe-attach y a lo que la alimenta. Los manejadores de interrupciones reales mediante `bus_setup_intr(9)`, la composición de filtro más ithread y las reglas sobre lo que un manejador puede y no puede hacer corresponden al Capítulo 19. MSI y MSI-X, junto con las capacidades PCIe más ricas que exponen, corresponden al Capítulo 20. Los anillos de descriptores, DMA scatter-gather, la coherencia de cache en torno a las escrituras del dispositivo y la historia completa de `bus_dma(9)` corresponden a los Capítulos 20 y 21. Las particularidades del espacio de configuración en chipsets específicos, las máquinas de estados de gestión de energía durante suspend y resume, y SR-IOV corresponden a capítulos posteriores. El capítulo se mantiene dentro del terreno que puede cubrir bien y cede el paso explícitamente cuando un tema merece su propio capítulo.

Las capas de la Parte 4 se acumulan. El Capítulo 16 enseñó el vocabulario del acceso a registros; el Capítulo 17 enseñó a pensar como un dispositivo; el Capítulo 18 enseña cómo encontrarse con uno real. El Capítulo 19 te enseñará cómo reaccionar a lo que dice el dispositivo, y los Capítulos 20 y 21 te enseñarán cómo dejar que el dispositivo acceda directamente a la RAM. Cada capa depende de la anterior. El Capítulo 18 es tu primer encuentro con el árbol de newbus como algo más que un diagrama abstracto, y las disciplinas que construyó la Parte 3 son las que mantienen ese encuentro en terreno sólido.

### Por qué el subsistema PCI merece un capítulo propio

En este punto puede que te preguntes por qué el subsistema PCI necesita un capítulo propio. La simulación ya nos dio registros; el hardware real nos dará los mismos registros. ¿Por qué no decir simplemente "llama a `bus_alloc_resource_any`, pasa el handle devuelto a `bus_read_4` y continúa"?

Dos razones.

La primera es que el subsistema PCI es el bus más ampliamente utilizado en FreeBSD moderno, y las convenciones de newbus que lo rodean son las convenciones que imitan todos los demás drivers de bus. Un lector que comprende la danza probe-attach de PCI puede leer la danza de attach de ACPI, la de USB, la de tarjetas SD y la de virtio sin necesidad de reaprender. Los patrones difieren en los detalles, pero la forma es la de PCI. Dedicar un capítulo entero al bus canónico es dedicar un capítulo al patrón del que toman prestado todos los buses.

La segunda es que PCI introduce conceptos para los que ningún capítulo anterior te ha preparado. El espacio de configuración es un segundo espacio de direcciones por dispositivo, separado de los propios BARs, donde el dispositivo anuncia qué es y qué necesita. Los vendor ID y device ID son una tupla de dieciséis bits más dieciséis bits que el driver compara contra una tabla de dispositivos soportados. Los subvendor ID y subsystem ID son una tupla de segundo nivel que desambigua tarjetas construidas en torno a un chipset común por distintos fabricantes. Los códigos de clase permiten a un driver reconocer categorías amplias (cualquier controlador host USB, cualquier UART) cuando una tabla específica de dispositivos sería demasiado restrictiva. Los BARs existen en el espacio de configuración como direcciones de treinta y dos o sesenta y cuatro bits que el driver nunca desreferencia directamente. Las capacidades PCI son una lista enlazada de metadatos adicionales que el driver lee en tiempo de attach. Cada uno de estos elementos es vocabulario nuevo; cada uno de ellos es la razón por la que el Capítulo 18 no es una simple sección añadida al Capítulo 17.

El capítulo también justifica su lugar al ser el capítulo en el que el driver `myfirst` adquiere su primer hijo de bus real. Hasta ahora, el driver ha vivido como un módulo del kernel con una única instancia implícita, cargada a mano mediante `kldload` y descargada con `kldunload`. Tras el Capítulo 18, el driver será un hijo de bus PCI apropiado, enumerado por el código de newbus del kernel, vinculado automáticamente cuando haya un dispositivo compatible, desvinculado automáticamente cuando el dispositivo desaparezca, y visible en `devinfo -v` como un dispositivo con un padre (`pci0`), una unidad (`myfirst0`, `myfirst1`) y un conjunto de recursos reclamados. Ese cambio es el paso de "un módulo que simplemente existe" a "un driver para un dispositivo que el kernel conoce". Todos los capítulos posteriores de la Parte 4 dan por sentado que lo has completado.

### Dónde dejó el driver el Capítulo 17

Algunos requisitos previos que conviene verificar antes de comenzar. El Capítulo 18 extiende el driver producido al final de la Etapa 5 del Capítulo 17, etiquetado como versión `1.0-simulated`. Si alguno de los puntos siguientes resulta incierto, regresa al Capítulo 17 antes de comenzar este capítulo.

- Tu driver compila sin errores y se identifica como `1.0-simulated` en `kldstat -v`.
- El softc almacena `sc->hw` (un `struct myfirst_hw *` del Capítulo 16) y `sc->sim` (un `struct myfirst_sim *` del Capítulo 17). Todos los accesos a registros pasan por `sc->hw`; todo el comportamiento simulado vive bajo `sc->sim`.
- El mapa de registros de dieciséis registros de 32 bits abarca los desplazamientos `0x00` a `0x3c`, con las adiciones del Capítulo 17 (`SENSOR`, `SENSOR_CONFIG`, `DELAY_MS`, `FAULT_MASK`, `FAULT_PROB`, `OP_COUNTER`) ya incorporadas.
- `CSR_READ_4`, `CSR_WRITE_4` y `CSR_UPDATE_4` envuelven `bus_space_read_4`, `bus_space_write_4` y un helper de lectura-modificación-escritura. Cada acceso aserta que `sc->mtx` está tomado en kernels de depuración.
- El callout del sensor se ejecuta una vez por segundo con una cadencia de diez segundos y hace oscilar `SENSOR`. El callout de comandos se dispara por cada comando con un retardo configurable. El framework de inyección de fallos está activo.
- El módulo no depende de nada externo al kernel base; es un driver autónomo que se puede cargar con `kldload`.
- `HARDWARE.md`, `LOCKING.md` y `SIMULATION.md` están actualizados.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` están habilitados en tu kernel de prueba.

Ese driver es el que extiende el Capítulo 18. Las adiciones son de nuevo modestas en volumen: un nuevo archivo (`myfirst_pci.c`), una nueva cabecera (`myfirst_pci.h`), un nuevo conjunto de rutinas de probe y attach, un pequeño cambio en `myfirst_hw_attach` para aceptar un recurso en lugar de asignar un buffer, un ordenamiento de detach ampliado, un incremento a `1.1-pci`, un nuevo documento `PCI.md` y un script de regresión actualizado. El cambio en el modelo mental es mayor de lo que sugiere el número de líneas.

### Qué aprenderás

Al cerrar este capítulo deberías ser capaz de:

- Describir qué son PCI y PCIe en un párrafo accesible para un principiante, situando con claridad el vocabulario clave: bus, dispositivo, función, BAR, espacio de configuración, vendor ID y device ID.
- Leer la salida de `pciconf -lv` y `devinfo -v` y localizar la información que le importa al autor de un driver: la tupla B:D:F, el vendor ID y el device ID, la clase, la subclase, la interfaz, los recursos reclamados y el bus padre.
- Escribir un driver PCI mínimo que registre una rutina probe contra el bus `pci`, haga coincidir un vendor ID y un device ID específicos, devuelva una prioridad `BUS_PROBE_*` significativa, imprima el dispositivo detectado mediante `device_printf` y se descargue limpiamente.
- Realizar el attach y el detach de un driver PCI siguiendo el ciclo de vida esperado de newbus: probe se ejecuta primero (a veces dos veces), attach se ejecuta una vez por dispositivo coincidente, detach se ejecuta una vez cuando el kernel elimina el dispositivo o el driver, y el softc se libera en el orden correcto.
- Usar `DRIVER_MODULE(9)` y `MODULE_DEPEND(9)` correctamente, nombrando el bus (`pci`) y la dependencia del módulo (`pci`) para que el cargador de módulos del kernel y el enumerador de newbus comprendan la relación.
- Explicar qué son los BARs, cómo los asigna el firmware durante el boot, cómo los descubre el kernel y por qué el driver no elige la dirección.
- Reservar un BAR de memoria PCI con `bus_alloc_resource_any(9)`, extraer su tag y handle de bus_space con `rman_get_bustag(9)` y `rman_get_bushandle(9)`, y pasarlos a la capa de acceso del capítulo 16 sin modificar las macros CSR.
- Reconocer cuándo un BAR es de 64 bits y ocupa dos ranuras del espacio de configuración, cómo funciona `PCIR_BAR(index)` y por qué contar BARs mediante simples incrementos enteros no es siempre seguro con BARs de 64 bits.
- Usar `pci_read_config(9)` y `pci_write_config(9)` para leer campos del espacio de configuración específicos del dispositivo que los accesores genéricos no cubren, y comprender el argumento de anchura (1, 2 o 4) y el contrato de efectos secundarios.
- Recorrer la lista de capacidades PCI del dispositivo con `pci_find_cap(9)` para localizar capacidades estándar (Power Management, MSI, MSI-X, PCI Express), y recorrer la lista de capacidades extendidas de PCIe con `pci_find_extcap(9)` para acceder a capacidades modernas como Advanced Error Reporting.
- Llamar a `pci_enable_busmaster(9)` cuando el dispositivo vaya a iniciar DMA más adelante, reconocer por qué los bits MEMEN y PORTEN del registro de comando suelen estar ya activados por el driver del bus en el momento del attach, y saber cuándo un dispositivo con quirks necesita que se afirmen manualmente.
- Escribir una secuencia de inicialización en attach que mantenga el backend de simulación del capítulo 17 inactivo en la ruta PCI, preservando al mismo tiempo una compilación exclusiva de simulación (mediante un switch en tiempo de compilación) para los lectores sin entorno de prueba PCI.
- Escribir una ruta de detach que libere los recursos en estrictamente el orden inverso al attach, sin perder un recurso ni liberar uno dos veces, incluso ante un fallo parcial en el attach.
- Probar el driver contra un dispositivo PCI real en un huésped de bhyve o QEMU, usando un vendor ID y un device ID que no colisionen con un driver del sistema base, y observar el ciclo completo de attach, operación, detach y descarga.
- Dividir el código específico de PCI en su propio archivo, actualizar la línea `SRCS` del módulo, etiquetar el driver como `1.1-pci` y producir un `PCI.md` breve que documente los vendor ID y device ID que soporta el driver.
- Describir a alto nivel dónde encajan MSI, MSI-X y PCIe AER en el panorama PCI, y saber qué capítulo posterior retoma cada tema.

La lista es larga; cada elemento está acotado. El objetivo del capítulo es la composición de todos ellos.

### Qué no cubre este capítulo

Varios temas adyacentes quedan aplazados de forma explícita para que el Capítulo 18 mantenga el foco.

- **Manejadores de interrupciones reales.** `bus_alloc_resource_any(9)` para `SYS_RES_IRQ`, `bus_setup_intr(9)`, la división entre un manejador de filtro y un manejador ithread, los flags `INTR_TYPE_*`, `INTR_MPSAFE`, y las reglas sobre qué puede y qué no puede ocurrir dentro de un manejador pertenecen al Capítulo 19. El driver del Capítulo 18 sigue haciendo polling mediante escrituras desde el espacio de usuario y los callouts del Capítulo 17; nunca gestiona una interrupción real.
- **MSI y MSI-X.** `pci_alloc_msi(9)`, `pci_alloc_msix(9)`, la asignación de vectores, el enrutamiento de interrupciones por cola y el diseño de la tabla MSI-X pertenecen al Capítulo 20. El Capítulo 18 solo los menciona como trabajo futuro al enumerar las capacidades PCI.
- **DMA.** Los tags de `bus_dma(9)`, `bus_dmamap_create(9)`, `bus_dmamap_load(9)`, las listas scatter-gather, los bounce buffers y los anillos de descriptores coherentes con la caché pertenecen a los Capítulos 20 y 21. El Capítulo 18 trata el BAR como un conjunto de registros mapeados en memoria y nada más.
- **Gestión del AER de PCIe.** La existencia del Advanced Error Reporting se introduce para que el lector sepa que el tema existe. Implementar un manejador de fallos que se suscriba a eventos AER, decodifique el registro de errores irrecuperables y participe en la recuperación del sistema es un tema de capítulos posteriores.
- **Hot plug, extracción de dispositivos y suspend en tiempo de ejecución.** La llegada o salida de un dispositivo PCI en tiempo de ejecución desencadena una secuencia newbus específica que un driver debe respetar; la mayoría de los drivers lo respetan simplemente teniendo una ruta de detach correcta. El Capítulo 18 muestra la ruta de detach correcta y deja la gestión de energía en tiempo de ejecución para el Capítulo 22 y el hotplug para la Parte 7 (Capítulo 32 sobre plataformas embebidas y Capítulo 35 sobre E/S asíncrona y gestión de eventos).
- **Passthrough a máquinas virtuales.** `bhyve(8)` y `vmm(4)` pueden pasar un dispositivo PCI real a un guest, lo que es una técnica útil para hacer pruebas. El Capítulo 18 lo menciona brevemente. Un tratamiento más profundo corresponde al capítulo donde el tema lo justifica.
- **SR-IOV y funciones virtuales.** Una capacidad de PCIe mediante la cual un único dispositivo anuncia múltiples funciones virtuales, cada una con su propio espacio de configuración, está fuera del alcance de un capítulo de nivel principiante.
- **Peculiaridades específicas de chipsets.** Los drivers reales suelen incluir una larga lista de erratas y workarounds para revisiones específicas de silicio concreto. El Capítulo 18 apunta al caso general; los capítulos de resolución de problemas posteriores del libro explican cómo razonar sobre las peculiaridades cuando te las encuentres.

Respetar esos límites mantiene el Capítulo 18 como un capítulo centrado en el subsistema PCI y en el lugar que ocupa el driver dentro de él. El vocabulario es lo que se transfiere; los capítulos específicos que siguen aplican ese vocabulario a las interrupciones, el DMA y la gestión de energía.

### Tiempo estimado de dedicación

- **Solo lectura**: cuatro o cinco horas. La topología PCI y la secuencia newbus son pequeñas en concepto pero densas en detalle, y cada parte merece una lectura pausada.
- **Lectura más escritura de los ejemplos resueltos**: diez a doce horas repartidas en dos o tres sesiones. El driver evoluciona en cuatro etapas; cada etapa es una pequeña pero real refactorización sobre el código del Capítulo 17.
- **Lectura más todos los laboratorios y desafíos**: dieciséis a veinte horas repartidas en cuatro o cinco sesiones, lo que incluye poner en marcha un laboratorio con bhyve o QEMU, leer `uart_bus_pci.c` y `virtio_pci_modern.c` en el árbol real de FreeBSD y ejecutar la pasada de regresión tanto contra la simulación como contra PCI real.

Las secciones 2, 3 y 5 son las más densas. Si la secuencia probe-attach o la ruta de asignación del BAR te resulta desconocida en la primera lectura, es normal. Detente, vuelve a leer el diagrama de la Sección 3 sobre cómo un BAR se convierte en un tag y un handle, y continúa cuando la imagen haya quedado clara.

### Requisitos previos

Antes de comenzar este capítulo, comprueba lo siguiente:

- El código fuente de tu driver coincide con el Capítulo 17, Etapa 5 (`1.0-simulated`). El punto de partida asume la capa de hardware del Capítulo 16, el backend de simulación del Capítulo 17, la familia completa de accesores `CSR_*`, la cabecera de sincronización y todos los primitivos introducidos en la Parte 3.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con `/usr/src` en disco y coincidiendo con el kernel en ejecución.
- Un kernel de depuración con `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` está compilado, instalado y arrancando correctamente.
- `bhyve(8)` o `qemu-system-x86_64` están disponibles en tu host de laboratorio, y puedes arrancar un guest de FreeBSD que inicie tu kernel de depuración. Un guest de bhyve es la elección canónica en este libro; QEMU funcionará de forma equivalente en todos los laboratorios de este capítulo.
- Las herramientas `devinfo(8)` y `pciconf(8)` están en tu PATH. Ambas forman parte del sistema base.

Si alguno de los puntos anteriores no está resuelto, corrígelo ahora en lugar de avanzar por el Capítulo 18 e intentar razonar desde una base inestable. El código PCI es menos tolerante que el código de simulación, porque una discrepancia entre las expectativas de tu driver y el comportamiento real del bus habitualmente se manifiesta como un probe fallido, un attach fallido o un page fault en el kernel.

### Cómo sacar el máximo partido de este capítulo

Cuatro hábitos darán frutos rápidamente.

Primero, ten `/usr/src/sys/dev/pci/pcireg.h` y `/usr/src/sys/dev/pci/pcivar.h` en los marcadores. El primer archivo es el mapa de registros autoritativo del espacio de configuración de PCI y PCIe; todos los macros que comienzan por `PCIR_`, `PCIM_` o `PCIZ_` están definidos allí. El segundo archivo es la lista autoritativa de funciones de acceso PCI (`pci_get_vendor`, `pci_read_config`, `pci_find_cap`, y demás), junto con sus comentarios de documentación. Leer ambos archivos una sola vez lleva aproximadamente una hora y elimina la mayor parte de las conjeturas que el resto del capítulo podría requerir en caso contrario.

> **Una nota sobre los números de línea.** Las declaraciones en las que nos apoyaremos más adelante, como `pci_read_config`, `pci_find_cap` y los macros de desplazamiento de registro `PCIR_*`, residen en `pcivar.h` y `pcireg.h` bajo nombres estables. Siempre que el capítulo te indique un punto de referencia en esos archivos, ese punto de referencia es el símbolo. Los números de línea cambian de versión en versión; los nombres, no. Busca el símbolo con grep y confía en lo que te muestre tu editor.

Segundo, ejecuta `pciconf -lv` en tu host de laboratorio y en tu guest, y mantén la salida visible en un terminal mientras lees. Cada término del vocabulario del capítulo (vendor, device, class, subclass, capabilities, resources) aparece literalmente en esa salida. Un lector que ha ejecutado `pciconf -lv` en su propio hardware encuentra el subsistema PCI mucho menos abstracto que quien no lo ha hecho.

Tercero, escribe los cambios a mano y ejecuta cada etapa. El código PCI es donde los pequeños errores tipográficos se convierten en discrepancias silenciosas. Escribir `0x1af4` como `0x1af5` por error no produce un error de compilación; produce un driver que compila sin problemas y nunca hace attach. Escribir los valores carácter a carácter, comprobarlos con tu objetivo de prueba y confirmar que `kldstat -v` muestra el driver reclamando el dispositivo esperado son los hábitos que evitan un día de depuración confusa.

Cuarto, lee `/usr/src/sys/dev/uart/uart_bus_pci.c` después de la Sección 2 y `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c` después de la Sección 5. El primer archivo es un ejemplo directo del patrón que enseña el Capítulo 18, escrito a un nivel que un principiante puede seguir. El segundo archivo es un ejemplo algo más completo que muestra cómo un driver moderno real compone el patrón con maquinaria adicional. Ninguno de los dos necesita entenderse línea a línea; ambos merecen una primera lectura detenida.

### Recorrido por el capítulo

Las secciones, en orden, son:

1. **Qué es PCI y por qué importa.** El bus, la topología, la tupla B:D:F, cómo FreeBSD representa PCI a través del subsistema `pci(4)`, y cómo un autor de drivers percibe todo esto con `pciconf -lv` y `devinfo -v`. La base conceptual.
2. **Probe y attach de un dispositivo PCI.** El baile newbus desde el lado del driver: `device_method_t`, probe, attach, detach, resume, suspend, `DRIVER_MODULE(9)`, `MODULE_DEPEND(9)`, la coincidencia de vendor e ID de dispositivo, y la primera etapa del driver del Capítulo 18 (`1.1-pci-stage1`).
3. **Comprensión y reclamación de recursos PCI.** Qué es un BAR, cómo lo asigna el firmware, cómo `bus_alloc_resource_any(9)` lo reclama, y cómo el `struct resource *` devuelto se convierte en un `bus_space_tag_t` y un `bus_space_handle_t`. La segunda etapa (`1.1-pci-stage2`).
4. **Acceso a los registros del dispositivo mediante `bus_space(9)`.** Cómo la capa de accesores del Capítulo 16 se conecta al nuevo recurso sin modificaciones, cómo los macros CSR se propagan, y cómo ocurre la primera lectura PCI real del driver.
5. **Inicialización del driver en el momento del attach.** `pci_enable_busmaster(9)`, `pci_read_config(9)`, `pci_find_cap(9)`, `pci_find_extcap(9)`, y el pequeño conjunto de operaciones sobre el espacio de configuración que el driver realiza en el attach. Mantener la simulación del Capítulo 17 inactiva en la ruta PCI. La tercera etapa (`1.1-pci-stage3`).
6. **Soporte del detach y limpieza de recursos.** Liberar recursos en orden inverso, gestionar fallos parciales en el attach, `device_delete_child`, y el script de regresión del detach.
7. **Prueba del comportamiento del driver PCI.** Poner en marcha un guest de bhyve o QEMU que exponga un dispositivo que el driver reconozca, observar el attach, ejercitar el driver contra el BAR real, leer y escribir el espacio de configuración desde el espacio de usuario con `pciconf -r` y `pciconf -w`, y usar `devinfo -v` y `dmesg` para rastrear la visión del driver sobre el sistema.
8. **Refactorización y versionado del driver PCI.** La división final en `myfirst_pci.c`, el nuevo `PCI.md`, el incremento de versión a `1.1-pci`, y la pasada de regresión.

Tras las ocho secciones vienen los laboratorios prácticos, los ejercicios de desafío, una referencia de resolución de problemas, un apartado de cierre que concluye la historia del Capítulo 18 y abre la del Capítulo 19, y un puente hacia el Capítulo 19 con una referencia anticipada al Capítulo 20. El material de referencia y hoja de consulta rápida al final del capítulo está pensado para releerlo mientras avanzas por los capítulos posteriores de la Parte 4; el vocabulario del Capítulo 18 es el vocabulario que reutiliza cada capítulo posterior de la familia PCI.

Si es tu primera lectura, avanza de forma lineal y haz los laboratorios en orden. Si estás revisitando el capítulo, las Secciones 3 y 5 son autónomas y se pueden leer bien en una sola sesión.



## Sección 1: Qué es PCI y por qué importa

Un lector que ha llegado hasta aquí en el libro ha construido un driver completo en torno a un bloque de registros simulado. La capa de accesores, el protocolo de comando-respuesta, la disciplina de locking, el framework de inyección de fallos y el orden de detach están todos en su lugar. La única concesión que hace el driver a la irrealidad es el origen de sus registros: provienen de una asignación `malloc(9)` en memoria del kernel, no de un trozo de silicio al otro lado de un bus. La Sección 1 presenta el subsistema que cambiará eso. PCI es el bus periférico más extendido en la informática moderna. Es también el child canónico de newbus para un driver de FreeBSD. Comprender qué es PCI, cómo llegó hasta aquí y cómo lo representa FreeBSD es la base sobre la que descansan todas las secciones restantes de este capítulo.

### Una breve historia y por qué importa

PCI son las siglas de Peripheral Component Interconnect. Intel lo introdujo a principios de la década de 1990 como sustituto de la generación anterior de buses de expansión para PC (ISA, EISA, VESA local bus y otros), ninguno de los cuales escalaba a las velocidades y anchos de bus que los periféricos modernos exigirían pronto. La especificación original de PCI describía un bus paralelo, compartido y síncrono que transmitía treinta y dos bits de datos, trabajaba a 33 MHz y permitía que un único dispositivo solicitara y mantuviera el bus durante una transacción. Unas pocas revisiones ampliaron el ancho a sesenta y cuatro bits, elevaron el reloj a 66 MHz e introdujeron variantes de señalización para plataformas de servidor (PCI-X), pero la forma básica siguió siendo la de un bus paralelo compartido.

PCI Express, llamado PCIe, es el sucesor moderno. Mantiene el modelo de PCI visible para el software prácticamente sin cambios, pero sustituye el bus físico por un conjunto de enlaces serie punto a punto. Donde PCI tenía muchos dispositivos compartiendo un único conjunto de cables, PCIe tiene cada dispositivo conectado al root complex del chipset a través de su propio lane (o conjunto de lanes, hasta dieciséis en uso habitual y treinta y dos en algunas tarjetas de gama alta). El ancho de banda por lane ha ido aumentando a través de generaciones sucesivas, desde 2,5 Gb/s en PCIe Gen 1 hasta 32 Gb/s en PCIe Gen 5 y más allá.

¿Por qué importa toda esta historia a quien escribe drivers? Porque el modelo de software no cambió con la transición. Desde el punto de vista del driver, un dispositivo PCIe sigue teniendo un espacio de configuración, sigue teniendo BARs, sigue teniendo un ID de fabricante y de dispositivo, sigue teniendo capacidades y sigue el ciclo de vida probe-attach-detach que PCI estableció. La capa física cambió; el vocabulario del software no. Este es uno de los pocos ámbitos de la informática en los que una interfaz de treinta años sigue siendo lo que se lee en el código fuente de FreeBSD, y la continuidad del modelo de software es lo que lo hace posible. El código escrito para PCI en 1995 puede, con pequeñas actualizaciones para las nuevas capacidades, controlar un dispositivo PCIe Gen 5 en 2026.

Hay una consecuencia práctica importante de esta continuidad. Cuando el libro menciona «PCI», casi siempre se refiere a «PCI o PCIe». El subsistema `pci(4)` del kernel gestiona ambos. Cuando la distinción importa, por ejemplo cuando aparece una característica exclusiva de PCIe como MSI-X o AER, el libro lo señalará expresamente. En cualquier otro caso, «PCI» y «PCIe» son lo mismo a nivel del driver.

### Dónde viven los dispositivos PCI en una máquina moderna

Abre casi cualquier portátil, sobremesa o servidor fabricado en los últimos veinte años y encontrarás dispositivos PCIe. Los más evidentes son las tarjetas de expansión: un adaptador de red en un slot, una tarjeta gráfica en otro, una unidad NVMe en el conector M.2 de la placa base, un módulo Wi-Fi en una tarjeta hija Mini-PCIe. Los menos evidentes están integrados: el controlador de almacenamiento que comunica con los puertos SATA es un dispositivo PCI; los controladores USB host son dispositivos PCI; el Ethernet integrado es un dispositivo PCI; el códec de audio es un dispositivo PCI; los gráficos integrados de la plataforma son un dispositivo PCI. Todo lo que aparece en la interconexión chipset-dispositivo entre la CPU y el mundo exterior es casi con toda seguridad un dispositivo PCI.

El kernel enumera estos dispositivos durante el arranque. El firmware (el BIOS del sistema o UEFI) recorre el bus, lee el espacio de configuración de cada dispositivo, asigna los BAR y cede el control al sistema operativo. El sistema operativo vuelve a recorrer el bus, construye su propia representación y conecta los drivers. El driver `pci(4)` de FreeBSD es el encargado de realizar este recorrido. En el momento en que el sistema está en modo multiusuario, todos los dispositivos PCI de la máquina han sido enumerados, a cada BAR se le ha asignado una dirección virtual del kernel y todo dispositivo que encajó con un driver ha sido conectado.

Una demostración práctica: ejecuta `pciconf -lv` en cualquier sistema FreeBSD. Cada entrada muestra un dispositivo con su dirección B:D:F (bus, dispositivo, función), sus identificadores de fabricante y dispositivo, sus identificadores de subfabricante y subsistema, su clase y subclase, el driver asociado en ese momento (si existe) y una descripción legible de lo que es. Las entradas son lo que vio el kernel; las descripciones son lo que `pciconf` consultó en su base de datos interna. Ejecutar este comando en tu máquina de laboratorio es la mejor introducción rápida a la topología PCI de tu equipo.

### La tupla bus-dispositivo-función

La dirección de un dispositivo PCI tiene tres componentes. En conjunto se denominan **tupla bus-dispositivo-función**, o B:D:F, o simplemente «dirección PCI».

El **número de bus** indica en qué bus PCI físico o lógico se encuentra el dispositivo. Una máquina suele tener un bus primario (bus 0), más buses adicionales detrás de puentes PCI a PCI. Un portátil puede tener los buses 0, 2, 3 y 4; un servidor puede tener decenas. Cada bus ocupa ocho bits, por lo que el sistema admite hasta 256 buses en la especificación PCI original. PCIe amplió esto a 16 bits (65.536 buses) mediante el Enhanced Configuration Access Mechanism, ECAM.

El **número de dispositivo** es el slot en el bus. Cada bus puede albergar hasta 32 dispositivos. En PCIe, la naturaleza punto a punto del enlace físico hace que cada puente tenga un único dispositivo en cada uno de sus buses descendentes; en ese caso, el número de dispositivo es prácticamente siempre 0. En PCI clásico, varios dispositivos comparten un bus y cada uno recibe su propio número de dispositivo.

El **número de función** indica qué función de un dispositivo multifunción se está direccionando. Un único dispositivo físico puede exponer hasta 8 funciones, cada una con su propio espacio de configuración y presentándose como un dispositivo PCI independiente. Los dispositivos multifunción son habituales: un chipset x86 típico presenta sus controladores USB host como múltiples funciones de una única unidad física; un controlador de almacenamiento puede exponer SATA, IDE y AHCI en funciones separadas. Los dispositivos de función única (el caso más habitual) utilizan la función 0.

La tupla combinada se escribe en la salida de `pciconf` de FreeBSD como `pciN:D:F`, donde `N` es el valor de dominio más bus. En la máquina de prueba del autor, `pci0:0:2:0` hace referencia al dominio 0, bus 0, dispositivo 2, función 0, que en una plataforma Intel suele ser los gráficos integrados. Esta notación es estable entre versiones de FreeBSD; la encontrarás en los mensajes de arranque del kernel, en `dmesg`, en `devinfo -v` y en la documentación del bus.

Un driver rara vez necesita conocer el valor B:D:F directamente. El subsistema newbus lo oculta detrás de un manejador `device_t`. Pero quien escribe drivers sí debe prestarle atención, porque dos cosas hacen uso del B:D:F: los administradores del sistema (que comparan un B:D:F con un slot o un dispositivo físico al instalar o depurar) y los mensajes del kernel (que lo imprimen en `dmesg` cuando un dispositivo se conecta, se desconecta o presenta algún fallo). Cuando ves `pci0:3:0:0: <Intel Corporation Ethernet Controller ...>` en el registro de arranque, estás leyendo un B:D:F.

### El espacio de configuración y su contenido

PCI distingue dos espacios de direcciones por dispositivo. El primero es el conjunto de BAR, que mapean los registros del dispositivo en el espacio de memoria del sistema (o en los puertos I/O); esto es lo que el capítulo 16 denominó «MMIO» y lo que explorarán la sección 3 y la sección 4 del capítulo 18. El segundo es el **espacio de configuración**, un bloque de memoria pequeño y estructurado por dispositivo que describe al propio dispositivo.

En el espacio de configuración residen el identificador de fabricante, el identificador de dispositivo, el código de clase, la revisión, las direcciones de los BAR, el puntero a la lista de capacidades y otros campos de metadatos. En PCI clásico ocupa 256 bytes; en PCIe se amplía a 4096 bytes. La distribución de los primeros sesenta y cuatro bytes está normalizada en todos los dispositivos PCI; el espacio restante se utiliza para capacidades y capacidades extendidas.

El driver accede al espacio de configuración a través de las interfaces `pci_read_config(9)` y `pci_write_config(9)`. Estas dos funciones reciben un manejador de dispositivo, un desplazamiento en bytes dentro del espacio de configuración y un ancho (1, 2 o 4 bytes), y devuelven o aceptan un valor `uint32_t`. El argumento de ancho permite al driver leer o escribir un byte, un campo de dieciséis bits o un campo de treinta y dos bits; el kernel traduce esto en la primitiva de acceso subyacente adecuada para la plataforma.

La mayor parte de lo que un driver necesita saber sobre el espacio de configuración ya ha sido extraída por la capa newbus y almacenada en los ivars del dispositivo. Por eso el driver puede llamar a `pci_get_vendor(dev)`, `pci_get_device(dev)`, `pci_get_class(dev)` y `pci_get_subclass(dev)` sin necesidad de leer el espacio de configuración manualmente. Estos accesores están definidos en `/usr/src/sys/dev/pci/pcivar.h` y se expanden mediante la macro `PCI_ACCESSOR` como funciones inline que leen un valor almacenado en caché. Los valores se leen una sola vez, durante la enumeración, y se conservan en los ivars del dispositivo a partir de entonces.

Para todo lo que los accesores habituales no cubren, `pci_read_config(9)` y `pci_write_config(9)` son la alternativa. Por ejemplo: si la hoja de datos de un dispositivo indica que «la revisión del firmware está en el desplazamiento 0x48 del espacio de configuración, como un entero de 32 bits en little-endian», el driver lee ese valor llamando a `pci_read_config(dev, 0x48, 4)`. El kernel gestiona el acceso de forma que el valor devuelto sea el valor little-endian que especifica la hoja de datos, en todas las arquitecturas compatibles.

### Identificadores de fabricante y dispositivo: cómo se producen las coincidencias

El núcleo de la identificación de dispositivos PCI es el par de valores de dieciséis bits denominados identificador de fabricante e identificador de dispositivo.

El **identificador de fabricante** lo asigna el PCI Special Interest Group (PCI-SIG) a cada empresa que fabrica dispositivos PCI. Intel tiene el 0x8086. Broadcom tiene varios (originalmente 0x14e4 y otros obtenidos por adquisición). Red Hat y el proyecto de virtualización de la comunidad Linux comparten el 0x1af4 para los dispositivos virtio. Todo dispositivo PCI lleva el identificador de su fabricante en el campo `VENDOR` del espacio de configuración.

El **identificador de dispositivo** lo asigna el fabricante a cada producto concreto. El 0x10D3 de Intel corresponde al controlador Ethernet gigabit 82574L. El 0x165F de Broadcom corresponde a una variante concreta del NetXtreme BCM5719. El 0x1001 de Red Hat en el rango virtio corresponde a virtio-block. Los fabricantes gestionan sus propias asignaciones de identificadores de dispositivo.

Un **identificador de subfabricante** y un **identificador de subsistema** forman juntos una tupla de segundo nivel. Identifican la placa en la que está integrado un chipset, a diferencia del chipset en sí. El mismo chip Ethernet Intel 82574L puede aparecer en un servidor Dell con subfabricante 0x1028, en un servidor HP con subfabricante 0x103c y en una placa OEM genérica con subfabricante 0x8086. Los drivers pueden usar el subfabricante o el subsistema para aplicar ajustes específicos, imprimir una cadena de identificación más útil o elegir entre comportamientos ligeramente distintos a nivel de placa.

La rutina probe de un driver compara contra estos identificadores. En el caso más sencillo, el driver tiene una tabla estática con todos los pares de fabricante y dispositivo compatibles; el probe recorre la tabla y devuelve `BUS_PROBE_DEFAULT` si hay coincidencia o `ENXIO` si no la hay. En casos más complejos, el driver también comprueba el subfabricante y el subsistema, realiza una búsqueda de coincidencia más amplia basada en clase, o combina ambas aproximaciones. El archivo `uart_bus_pci.c` en `/usr/src/sys/dev/uart/` muestra este patrón a una escala legible.

El driver del capítulo 18 utilizará la forma tabular sencilla. La tabla tendrá una o dos entradas. Los identificadores de fabricante y dispositivo que utilizamos son los que un invitado bhyve o QEMU expondrá para un dispositivo de prueba sintético, y en el desarrollo del capítulo se descargará el driver del sistema base que de otro modo reclamaría el mismo identificador, antes de cargar `myfirst`.

### Cómo enumera FreeBSD los dispositivos PCI

Vale la pena comprender en líneas generales los pasos que van desde «el firmware ha configurado el bus» hasta «se ejecuta la rutina attach de un driver», porque entenderlos hace que la secuencia probe-attach resulte natural en lugar de misteriosa.

En primer lugar, se ejecuta el código de enumeración de buses de la plataforma. En x86 reside bajo `/usr/src/sys/dev/pci/` y lo impulsa un código de conexión específico de la plataforma (x86 utiliza puentes ACPI y puentes host clásicos; arm64 utiliza puentes host basados en device tree). La enumeración recorre el bus, lee los identificadores de fabricante y dispositivo de cada dispositivo, lee los BAR de cada dispositivo y registra lo que encuentra.

En segundo lugar, la capa newbus del kernel crea un `device_t` para cada dispositivo descubierto y lo añade como hijo del dispositivo de bus PCI (`pci0`, `pci1`, etcétera). Cada hijo tiene un marcador de posición para la tabla de métodos del dispositivo; el código de newbus aún no sabe qué driver se vinculará. El hijo tiene ivars: el fabricante, el dispositivo, el subfabricante, el subsistema, la clase, la subclase, la interfaz, la revisión, el B:D:F y los descriptores de recursos se almacenan todos en los ivars para acceder a ellos posteriormente.

En tercer lugar, el kernel invita a cada driver registrado a realizar el probe de cada dispositivo. El método `probe` de cada driver se llama en orden de prioridad. Un driver inspecciona el fabricante y el dispositivo, así como cualquier otro dato que necesite, y devuelve uno de un pequeño conjunto de valores:

- Un número negativo: «Reconozco este dispositivo y lo quiero». Los valores más próximos a cero indican mayor prioridad. El nivel estándar para una coincidencia por identificador de fabricante y dispositivo es `BUS_PROBE_DEFAULT`, que vale `-20`. `BUS_PROBE_VENDOR` vale `-10` y lo supera; `BUS_PROBE_GENERIC` vale `-100` y queda por debajo. La sección 2 lista el conjunto completo de niveles.
- `0`: «Reconozco este dispositivo con prioridad absoluta». El nivel `BUS_PROBE_SPECIFIC`. Ningún otro driver puede superarlo.
- Un errno positivo (habitualmente `ENXIO`): «No reconozco este dispositivo».

El kernel elige el driver que devolvió el valor numéricamente más pequeño y lo conecta. Si dos drivers devuelven el mismo valor, gana el que se registró primero. La prioridad por niveles permite que un driver genérico coexista con un driver específico para el dispositivo: el driver genérico devuelve `BUS_PROBE_GENERIC`, el driver específico devuelve `BUS_PROBE_DEFAULT`, y el driver específico gana porque `-20` está más cerca de cero que `-100`.

En cuarto lugar, el kernel llama al método `attach` del driver ganador. El driver asigna su softc (normalmente preasignado por newbus), reclama recursos con `bus_alloc_resource_any(9)`, configura las interrupciones y registra un dispositivo de caracteres, una interfaz de red o lo que sea que el dispositivo exponga al espacio de usuario. Si `attach` devuelve 0, el dispositivo está operativo. Si `attach` devuelve un errno, el kernel desconecta el driver (llamar a `detach` no es estrictamente obligatorio en caso de fallo en attach con newbus moderno; se espera que el driver deshaga limpiamente sus operaciones antes de devolver el error).

En quinto lugar, el kernel pasa al siguiente dispositivo. El proceso se repite hasta que todos los dispositivos PCI han sido sondeados y todos los que encontraron coincidencia han sido conectados.

La desconexión es el proceso inverso: el kernel llama al método `detach` de cada driver cuando el dispositivo se retira (mediante `devctl detach` o al descargar el módulo), y el driver libera todo lo que reclamó en attach, en orden inverso.

Esta es la danza de newbus que el capítulo 18 enseña al driver a seguir. La sección 2 escribe la primera versión de ella; las secciones 3 a 6 añaden cada capacidad adicional; la sección 8 la consolida en un módulo limpio.

### El subsistema `pci(4)` desde la perspectiva del driver

El driver no ve la enumeración del bus. Ve un handle de dispositivo (`device_t dev`) y un conjunto de llamadas de acceso. La cabecera `/usr/src/sys/dev/pci/pcivar.h` define esos accesos. Los más importantes son:

- `pci_get_vendor(dev)` devuelve el vendor ID como `uint16_t`.
- `pci_get_device(dev)` devuelve el device ID como `uint16_t`.
- `pci_get_subvendor(dev)` y `pci_get_subdevice(dev)` devuelven el subvendor y el subsistema.
- `pci_get_class(dev)`, `pci_get_subclass(dev)` y `pci_get_progif(dev)` devuelven los campos del código de clase.
- `pci_get_revid(dev)` devuelve la revisión.
- `pci_read_config(dev, reg, width)` lee del espacio de configuración.
- `pci_write_config(dev, reg, val, width)` escribe en el espacio de configuración.
- `pci_find_cap(dev, cap, &capreg)` busca una capability PCI estándar; devuelve 0 si la encuentra, `ENOENT` si no existe.
- `pci_find_extcap(dev, cap, &capreg)` busca una capability extendida de PCIe; misma convención de retorno.
- `pci_enable_busmaster(dev)` activa el bit Bus Master Enable en el registro de comandos.
- `pci_disable_busmaster(dev)` lo desactiva.

Este es el vocabulario del capítulo 18. Cada sección del capítulo utiliza uno o más de estos accesos. Un lector que se sienta cómodo con la forma de esta lista está listo para empezar a escribir código PCI.

### Dispositivos PCI habituales en el mundo real

Antes de continuar, un breve repaso de los dispositivos que presenta PCI.

**Controladores de interfaz de red.** Las tarjetas de red son prácticamente todas dispositivos PCI. El driver Intel `em(4)` para la familia 8254x, el driver Intel `ix(4)` para la familia 82599, el driver Intel `ixl(4)` para la familia X710, y los drivers Broadcom `bge(4)` / `bnxt(4)` para la familia NetXtreme residen en `/usr/src/sys/dev/e1000/`, `/usr/src/sys/dev/ixl/` o `/usr/src/sys/dev/bge/`. Son drivers grandes, de calidad de producción, que ejercitan prácticamente todos los temas de la Parte 4.

**Controladoras de almacenamiento.** Las controladoras AHCI SATA, las unidades NVMe, los HBA SAS y las controladoras RAID son todas PCI. `ahci(4)`, `nvme(4)`, `mpr(4)`, `mpi3mr(4)` y otros residen bajo `/usr/src/sys/dev/`. Estos se encuentran entre los drivers mejor mantenidos del árbol.

**Controladoras de host USB.** Los controladores xHCI, EHCI y OHCI son PCI. El driver genérico de host controller se enlaza a cada uno de ellos, y el subsistema USB gestiona todo lo que queda por encima. `xhci(4)` es el canónico en los sistemas modernos.

**Tarjetas gráficas y gráficos integrados.** Los drivers de GPU en FreeBSD se mantienen mayoritariamente fuera del árbol base (drivers DRM del port drm-kmod), pero el enlace al bus para todos ellos es PCI estándar.

**Controladoras de audio.** Los codecs HDA, los puentes AC'97 más antiguos y varios dispositivos de audio conectados por USB llegan al sistema a través de PCI de alguna forma. `snd_hda(4)` es el punto de enlace habitual.

**Dispositivos Virtio en máquinas virtuales.** Cuando un guest FreeBSD se ejecuta bajo bhyve, KVM, VMware o Hyper-V, los dispositivos paravirtualizados aparecen como PCI. Los dispositivos virtio-network, virtio-block, virtio-entropy y virtio-console se presentan al guest como dispositivos PCI. El driver `virtio_pci(4)` se enlaza primero y publica nodos hijo para cada uno de los drivers virtio específicos del transporte.

**Los propios componentes del chipset de la máquina.** El puente LPC de la plataforma, la controladora SMBus, la interfaz del sensor térmico y diversas funciones de control misceláneas son PCI.

Si alguna vez te has preguntado por qué el árbol de código fuente de FreeBSD es tan grande, el ecosistema de dispositivos PCI es gran parte de la respuesta. Cada dispositivo de la lista anterior necesita un driver. El driver que construirás en el capítulo 18 es pequeño y hace muy poco; los drivers de los ejemplos son grandes porque implementan protocolos reales sobre el bus PCI. Pero la forma de cada uno de ellos es precisamente lo que enseña el capítulo 18.

### Dispositivos PCI simulados: bhyve y QEMU

Un lector que disponga de un conjunto completo de hardware de prueba puede saltarse este apartado. Todos los demás dependen de la virtualización para disponer de los dispositivos PCI que van a manejar.

El hipervisor `bhyve(8)` de FreeBSD, incluido en el sistema base, puede presentar a un guest un conjunto de dispositivos PCI emulados. Los más comunes son `virtio-net`, `virtio-blk`, `virtio-rnd`, `virtio-console`, `ahci-hd`, `ahci-cd`, `e1000`, `xhci` y un dispositivo de framebuffer. Cada uno tiene un vendor ID y un device ID bien conocidos; el enumerador PCI del guest los ve como dispositivos PCI reales; los drivers del guest se enlazan a ellos igual que lo harían con hardware real. Ejecutar un guest FreeBSD bajo bhyve es la forma canónica, en este libro, de disponer de un dispositivo PCI al que el driver pueda enlazarse.

QEMU con KVM (en hosts Linux) o con el acelerador HVF (en hosts macOS) proporciona un superconjunto de los dispositivos emulados por bhyve, además de algunos diseñados específicamente para pruebas. El dispositivo `pci-testdev` (vendor 0x1b36, device 0x0005) es un dispositivo PCI deliberadamente mínimo pensado para código de prueba del kernel; tiene dos BARs (uno de memoria y uno de I/O), y las escrituras en determinados offsets provocan comportamientos específicos. La Sección 7 del capítulo 18 puede usar tanto un dispositivo virtio-rnd bajo bhyve como un pci-testdev bajo QEMU como objetivo.

Para el camino de aprendizaje del libro, se utilizará un dispositivo virtio-rnd bajo bhyve. La razón es que bhyve viene incluido en cualquier instalación de FreeBSD, mientras que QEMU requiere paquetes adicionales. El coste es pequeño: el dispositivo virtio-rnd tiene un driver real en el sistema base (`virtio_random(4)`), y el capítulo mostrará cómo evitar que ese driver reclame el dispositivo para que pueda hacerlo `myfirst` en su lugar.

Una nota importante sobre la elección del camino de aprendizaje. El driver `myfirst` no es un driver virtio-rnd real. No tiene forma de hablar el protocolo virtio-rnd; trata el BAR como un conjunto de registros opacos y los lee y escribe a modo de demostración. Esto es perfectamente válido para el propósito del capítulo (demostrar que el driver puede enlazarse, leer, escribir y desenlazarse), pero no lo sería en producción. El capítulo 18 es una introducción práctica a la secuencia de enlace PCI, no un tutorial sobre cómo escribir un driver virtio. Al terminar el capítulo, el driver que tendrás sigue siendo el driver educativo `myfirst`, ahora capaz de enlazarse a un bus PCI en lugar de únicamente a través de `kldload`.

### La evolución del driver en el capítulo 18

Un mapa rápido de por dónde ha pasado el driver `myfirst` y adónde se dirige.

- **Versiones 0.1 a 0.8** (Parte 1 a Parte 3): el driver aprendió el ciclo de vida del driver, la maquinaria de cdev, las primitivas de concurrencia y la coordinación.
- **Versión 0.9-coordination** (final del capítulo 15): disciplina de lock completa, variables de condición, sx locks, callouts, taskqueue, semáforo de conteo.
- **Versión 0.9-mmio** (final del capítulo 16): bloque de registros respaldado por `bus_space(9)`, macros CSR, registro de accesos, capa de hardware en `myfirst_hw.c`.
- **Versión 1.0-simulated** (final del capítulo 17): comportamiento dinámico de registros, callouts que cambian de estado, protocolo comando-respuesta, inyección de fallos, capa de simulación en `myfirst_sim.c`.
- **Versión 1.1-pci** (final del capítulo 18, nuestro objetivo): la simulación es conmutable y, cuando el driver se enlaza a un dispositivo PCI real, el BAR se convierte en el bloque de registros, `myfirst_hw_attach` utiliza `bus_alloc_resource_any` en lugar de `malloc`, y la capa de acceso del capítulo 16 apunta a silicio real.
- **Versión 1.2-intr** (capítulo 19): un manejador de interrupciones real registrado a través de `bus_setup_intr(9)`, para que el driver pueda reaccionar a los cambios de estado del propio dispositivo en lugar de hacer polling.
- **Versión 1.3-msi** (capítulo 20): MSI y MSI-X, que dan al driver una historia de enrutamiento de interrupciones más rica.
- **Versión 1.4-dma** (capítulos 20 y 21): un tag `bus_dma(9)`, anillos de descriptores y las primeras transferencias DMA reales.

Cada versión es una capa sobre la anterior. El capítulo 18 es una de esas capas, suficientemente pequeña para enseñarse con claridad y suficientemente grande para ser relevante.

### Ejercicio: lee tu propia topología PCI

Antes de la Sección 2, un ejercicio breve para hacer concreto el vocabulario.

En tu host de laboratorio, ejecuta:

```sh
sudo pciconf -lv
```

Verás una lista con todos los dispositivos PCI que el kernel enumeró. Cada entrada tiene un aspecto aproximado como este:

```text
em0@pci0:0:25:0:        class=0x020000 rev=0x03 hdr=0x00 vendor=0x8086 device=0x15ba subvendor=0x8086 subdevice=0x2000
    vendor     = 'Intel Corporation'
    device     = 'Ethernet Connection (2) I219-LM'
    class      = network
    subclass   = ethernet
```

Elige tres dispositivos de la lista. Para cada uno, identifica:

- El nombre lógico del dispositivo en FreeBSD (la cadena `name@pciN:B:D:F` al principio).
- Los vendor ID y device ID.
- La clase y la subclase (las categorías en inglés legibles, no solo los códigos hexadecimales).
- Si el dispositivo tiene un driver enlazado (`em0`, por ejemplo, está enlazado a `em(4)`; una entrada con solo `none0@...` no tiene driver).

Mantén esta salida visible en un terminal mientras lees el resto del capítulo. Cada elemento de vocabulario introducido en las Secciones 2 a 5 hace referencia a campos que puedes encontrar aquí. El ejercicio consiste en anclar el vocabulario abstracto a un conjunto concreto de dispositivos de tu máquina.

Si estás leyendo el libro sin tener a mano una máquina FreeBSD, el siguiente fragmento es la salida de `pciconf -lv` en el host de laboratorio del autor, truncada a los tres primeros dispositivos:

```text
hostb0@pci0:0:0:0:      class=0x060000 rev=0x00 hdr=0x00 vendor=0x8086 device=0x3e31
    vendor     = 'Intel Corporation'
    device     = '8th Gen Core Processor Host Bridge/DRAM Registers'
    class      = bridge
    subclass   = HOST-PCI
pcib0@pci0:0:1:0:       class=0x060400 rev=0x00 hdr=0x01 vendor=0x8086 device=0x1901
    vendor     = 'Intel Corporation'
    device     = '6th-10th Gen Core Processor PCIe Controller (x16)'
    class      = bridge
    subclass   = PCI-PCI
vgapci0@pci0:0:2:0:     class=0x030000 rev=0x00 hdr=0x00 vendor=0x8086 device=0x3e9b
    vendor     = 'Intel Corporation'
    device     = 'CoffeeLake-H GT2 [UHD Graphics 630]'
    class      = display
    subclass   = VGA
```

Tres dispositivos, tres drivers, tres códigos de clase. El host bridge (`hostb0`) es el puente PCI-a-bus-de-memoria; el PCI bridge (`pcib0`) es un puente PCI-a-PCI que conduce al slot de la GPU; el dispositivo de clase VGA (`vgapci0`) es el gráfico integrado de un chipset Coffee Lake. Todos ellos siguen la misma secuencia de probe-attach-detach que enseña el capítulo 18. Lo que cambia es el driver. La secuencia del bus no cambia.

### Cerrando la Sección 1

PCI es el bus periférico canónico de los sistemas modernos y el hijo canónico de newbus en FreeBSD. Es compartido por PCI y PCIe, que difieren en la capa física pero presentan el mismo modelo visible por software. Todo dispositivo PCI tiene una dirección B:D:F, un espacio de configuración, un conjunto de BARs, un vendor ID, un device ID y un lugar en el árbol newbus del kernel. El trabajo del driver es hacer match con uno o más dispositivos por sus IDs, reclamar sus BARs y exponer su comportamiento a través de alguna interfaz de espacio de usuario. El subsistema `pci(4)` de FreeBSD realiza la enumeración; el driver realiza el enlace.

El vocabulario de la Sección 1 es el vocabulario que utiliza el resto del capítulo: B:D:F, espacio de configuración, BARs, vendor ID y device ID, códigos de clase, capabilities y la secuencia probe-attach-detach de newbus. Si alguno de estos conceptos te resulta poco familiar, vuelve a leer el apartado correspondiente antes de continuar. La Sección 2 toma ese vocabulario y construye la primera versión del driver.



## Sección 2: probe y attach de un dispositivo PCI

La Sección 1 estableció qué es PCI y cómo lo representa FreeBSD. La Sección 2 es donde el driver usa por fin ese vocabulario. El objetivo aquí es poner en marcha el driver PCI mínimo viable: un driver que se registra como candidato para el bus PCI, hace match con un vendor ID y un device ID específicos, imprime un mensaje en `dmesg` cuando el match tiene éxito y se descarga limpiamente. Todavía sin reclamar un BAR. Todavía sin acceso a registros. Solo el esqueleto.

El esqueleto es importante. Introduce la secuencia probe-attach-detach de forma aislada, antes de que los BARs, los recursos y los recorridos del espacio de configuración compliquen el panorama. Un lector que escribe este esqueleto a mano, luego escribe `kldload ./myfirst.ko`, ve cómo `dmesg` informa del probe y el attach del driver, y escribe `kldunload myfirst` para ver cómo se dispara el detach, ha construido el modelo mental correcto para todo lo que viene después. Todos los capítulos posteriores de la Parte 4 asumen ese modelo mental.

### El contrato probe-attach-detach

Todo driver newbus tiene tres métodos en el corazón de su ciclo de vida. `probe` pregunta "¿es este dispositivo algo que sé cómo manejar?". `attach` dice "sí, lo quiero, y así es como lo reclamo". `detach` dice "libera este dispositivo, me voy".

**Probe**. El kernel lo llama una vez por cada dispositivo que el bus ha enumerado, para cada driver que ha registrado interés en ese bus. El driver lee el vendor ID y el device ID del dispositivo (y lo que necesite para decidir), devuelve un valor de prioridad si quiere el dispositivo, y devuelve `ENXIO` si no. El sistema de prioridades es lo que permite que un driver específico gane sobre uno genérico: un driver que devuelve `BUS_PROBE_DEFAULT` gana sobre uno que devuelve `BUS_PROBE_GENERIC` cuando ambos quieren el mismo dispositivo. Si ningún driver devuelve un match, el dispositivo queda sin reclamar (lo verás como entradas `nonea@pci0:...` en `devinfo -v`).

Un matiz importante: **probe puede invocarse más de una vez para un mismo dispositivo**. El mecanismo de reprobe de newbus existe para gestionar dispositivos que aparecen en tiempo de ejecución (hotplug) o que vuelven tras un suspend. Un buen probe es idempotente: lee el mismo estado, toma la misma decisión y devuelve el mismo valor. Probe no debe asignar recursos, configurar timers, registrar interrupciones ni hacer nada que tenga que deshacerse después. Solo inspecciona y decide.

Un segundo matiz: **probe se ejecuta antes que attach, pero después de que el kernel haya asignado los recursos del dispositivo**. Los BARs, el IRQ y el espacio de configuración son accesibles desde probe. Esto significa que probe puede leer registros específicos del dispositivo a través de `pci_read_config` para distinguir variantes de un chipset por revisión o silicon ID si es necesario. Los drivers reales lo hacen en ocasiones. El driver del capítulo 18 no lo necesita; los IDs de fabricante y dispositivo son suficientes.

**Attach**. Se invoca una vez por dispositivo, después de que probe haya seleccionado un ganador. La rutina attach del driver es donde sucede el trabajo real: inicialización del softc, asignación de recursos, mapeo de registros, creación del dispositivo de caracteres y cualquier configuración que el dispositivo necesite al arrancar. Si attach devuelve 0, el dispositivo está operativo; el kernel considera que el driver está vinculado al dispositivo y continúa. Si attach devuelve un valor distinto de cero, el kernel considera que el attach ha fallado. El driver debe liberar todo lo que haya asignado antes de devolver el error; newbus moderno no invoca detach en ese caso (la convención antigua sí lo hacía, por lo que los drivers más antiguos todavía estructuran sus rutas de error para manejarlo).

**Detach**. Se invoca cuando el driver está siendo desvinculado del dispositivo. La llamada es el espejo de attach: todo lo que attach asignó, detach lo libera. Todo lo que attach configuró, detach lo deshace. Todo lo que attach registró, detach lo elimina. El orden es estricto: detach debe deshacer en el orden inverso al que siguió attach. Un error aquí provoca kernel panics al descargar el módulo, pérdidas de recursos en el mejor caso, o errores sutiles de use-after-free en el peor.

**Resume** y **suspend** son métodos opcionales. Se invocan cuando el sistema entra en suspend y vuelve a resume, y dan al driver la oportunidad de guardar y restaurar el estado del dispositivo a lo largo del evento de energía. El driver del capítulo 18 no implementa ninguno de los dos métodos en la primera fase; añadiremos resume en un capítulo posterior cuando el tema lo requiera.

Existen otros métodos (`shutdown`, `quiesce`, `identify`) que raramente son relevantes para un driver PCI básico. El esqueleto del capítulo 18 registra solo los tres métodos principales más `DEVMETHOD_END`.

### La tabla de métodos del dispositivo

El mecanismo newbus de FreeBSD accede a los métodos del driver a través de una tabla. La tabla es un array de entradas `device_method_t`, y cada entrada relaciona el nombre de un método con la función C que lo implementa. La tabla termina con `DEVMETHOD_END`, que es simplemente una entrada a cero que indica a newbus que no hay más métodos.

La tabla se declara en el ámbito de archivo, de la siguiente forma, en el código fuente del driver:

```c
static device_method_t myfirst_pci_methods[] = {
	DEVMETHOD(device_probe,		myfirst_pci_probe),
	DEVMETHOD(device_attach,	myfirst_pci_attach),
	DEVMETHOD(device_detach,	myfirst_pci_detach),
	DEVMETHOD_END
};
```

Cada `DEVMETHOD(name, func)` se expande en un inicializador `{ name, func }`. La capa newbus accede al método de un driver buscando el nombre en esta tabla. Si un método no está registrado (por ejemplo, `device_resume` no aparece en esta tabla), la capa newbus utiliza una implementación por defecto; para `resume` el comportamiento por defecto es no hacer nada, y para `probe` es devolver `ENXIO`.

Los nombres de los métodos están definidos en `/usr/src/sys/sys/bus.h` y se expanden mediante el sistema de build de newbus. Cada uno de ellos corresponde a un prototipo de función que el driver debe respetar. Por ejemplo, el prototipo del método `device_probe` es:

```c
int probe(device_t dev);
```

La implementación del driver debe tener exactamente esa firma. Los desajustes de tipos producen errores de compilación, no misterios en tiempo de ejecución; si la firma de tu probe es incorrecta, el build fallará.

### La estructura del driver

Junto a la tabla de métodos, el driver declara una `driver_t`. Esta estructura vincula la tabla de métodos, el tamaño del softc y un nombre corto:

```c
static driver_t myfirst_pci_driver = {
	"myfirst",
	myfirst_pci_methods,
	sizeof(struct myfirst_softc),
};
```

El nombre (`"myfirst"`) es el que newbus utilizará para numerar las instancias de unidad. El primer dispositivo conectado pasa a llamarse `myfirst0`, el segundo `myfirst1`, y así sucesivamente. Este nombre es el que muestra `devinfo -v` y el que exponen las herramientas del espacio de usuario (como `/dev/myfirst0`, si el driver crea un cdev con ese nombre).

El tamaño del softc indica a newbus cuántos bytes debe asignar para el softc de cada dispositivo. La asignación es automática: en el momento en que se ejecuta attach, `device_get_softc(dev)` devuelve un puntero a un bloque a cero del tamaño solicitado. El driver no llama a `malloc` para el propio softc; utiliza lo que newbus le ha proporcionado. Esta comodidad es algo que el driver `myfirst` ya viene utilizando desde el Capítulo 10; cobra más importancia con PCI porque cada unidad tiene su propio softc y newbus gestiona su ciclo de vida.

### DRIVER_MODULE y MODULE_DEPEND

El driver se vincula al bus PCI mediante dos macros. La primera es `DRIVER_MODULE(9)`:

```c
DRIVER_MODULE(myfirst, pci, myfirst_pci_driver, NULL, NULL);
```

La expansión de esta macro realiza varias acciones. Registra el driver como candidato hijo del bus `pci`, envolviendo la `driver_t` en un descriptor de módulo del kernel. Programa al driver para que participe en el probe de cada dispositivo que enumera el bus `pci`. Proporciona puntos de enganche para manejadores de eventos de módulo opcionales (los dos `NULL` corresponden, respectivamente, a la inicialización y limpieza del módulo; de momento los dejamos vacíos).

El primer argumento es el nombre del módulo, que debe coincidir con el nombre de la `driver_t`. El segundo argumento es el nombre del bus; `pci` es el nombre newbus del driver del bus PCI. El tercer argumento es el propio driver. Los argumentos restantes son para callbacks opcionales.

La macro tiene una consecuencia sutil: el driver participará en el probe de todos los buses PCI del sistema. Si el sistema tiene varios dominios PCI, el driver recibirá la oferta de cada dispositivo en cada dominio. La labor del probe es decir que sí únicamente a los dispositivos que el driver realmente soporta; la labor del kernel es preguntar.

La segunda macro es `MODULE_DEPEND(9)`:

```c
MODULE_DEPEND(myfirst, pci, 1, 1, 1);
```

Esto indica al cargador de módulos que `myfirst.ko` depende del módulo del kernel `pci`. Los tres números son la versión mínima, la preferida y la máxima. Una dependencia de la versión 1 con rango de cero a uno es el caso habitual. El cargador utiliza esta información para rechazar la carga de `myfirst.ko` si el subsistema PCI del kernel no está disponible (algo que prácticamente nunca ocurre en un sistema real, pero la comprobación es una buena práctica).

Sin `MODULE_DEPEND`, el cargador podría cargar `myfirst.ko` antes de que el subsistema PCI estuviese disponible durante el arranque temprano, lo que provocaría un panic cuando `DRIVER_MODULE` intentase registrarse en un bus que aún no existe. Con ella, el cargador serializa la carga correctamente.

### Identificación por vendor ID y device ID

La rutina probe es donde se realiza la comparación de vendor ID y device ID. El patrón consiste en una tabla estática y un bucle. Considera una versión mínima:

```c
static const struct myfirst_pci_id {
	uint16_t	vendor;
	uint16_t	device;
	const char	*desc;
} myfirst_pci_ids[] = {
	{ 0x1af4, 0x1005, "Red Hat / Virtio entropy source (demo target)" },
	{ 0, 0, NULL }
};

static int
myfirst_pci_probe(device_t dev)
{
	uint16_t vendor = pci_get_vendor(dev);
	uint16_t device = pci_get_device(dev);
	const struct myfirst_pci_id *id;

	for (id = myfirst_pci_ids; id->desc != NULL; id++) {
		if (id->vendor == vendor && id->device == device) {
			device_set_desc(dev, id->desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}
```

Hay varios aspectos que conviene destacar. La tabla es pequeña y estática, con una entrada por dispositivo soportado. `pci_get_vendor` y `pci_get_device` leen las ivars almacenadas en caché, por lo que las llamadas son económicas. La comparación es un bucle simple; la tabla es lo bastante corta como para no necesitar una tabla hash. `device_set_desc` instala una descripción legible que `pciconf -lv` y `dmesg` mostrarán cuando el dispositivo se conecte. `BUS_PROBE_DEFAULT` es la prioridad estándar para una coincidencia específica de vendor; tiene prioridad sobre los drivers genéricos basados en clase, pero cede ante cualquier driver que devuelva explícitamente un valor más negativo.

Un punto sutil pero importante: esta rutina probe apunta al dispositivo virtio-rnd (entropía) que el driver `virtio_random(4)` del sistema base normalmente reclama. Si ambos drivers están cargados, las reglas de prioridad del sistema deciden el ganador. `virtio_random` registra `BUS_PROBE_DEFAULT`, al igual que `myfirst`. El desempate lo decide el orden de registro, que puede variar. La forma fiable de garantizar que `myfirst` se conecte es descargar `virtio_random` antes de cargar `myfirst`. La Sección 7 mostrará cómo hacerlo.

Una segunda observación: los IDs de vendor y de dispositivo del ejemplo anterior apuntan a un dispositivo virtio. Los drivers PCI reales para hardware real apuntarían a chips cuyos IDs no estén ya reclamados por los drivers del sistema base. En un driver para producción, la lista incluiría cada variante soportada del chipset objetivo, habitualmente con cadenas descriptivas que identifican la revisión del silicio. `uart_bus_pci.c` tiene más de sesenta entradas; `ix(4)` tiene más de cien.

### Los niveles de prioridad del probe

FreeBSD define varios niveles de prioridad para el probe, definidos en `/usr/src/sys/sys/bus.h`:

- `BUS_PROBE_SPECIFIC` = 0. El driver identifica el dispositivo de forma exacta. Ningún otro driver puede superar esta prioridad.
- `BUS_PROBE_VENDOR` = -10. El driver es proporcionado por el fabricante y debe tener prioridad sobre cualquier driver genérico.
- `BUS_PROBE_DEFAULT` = -20. El nivel estándar para una coincidencia por vendor ID y device ID.
- `BUS_PROBE_LOW_PRIORITY` = -40. Una coincidencia de menor prioridad, habitualmente para drivers que quieren ser el predeterminado solo si ningún otro reclama el dispositivo.
- `BUS_PROBE_GENERIC` = -100. Un driver genérico que se conecta a una clase de dispositivos si no existe ninguno más específico.
- `BUS_PROBE_HOOVER` = -1000000. Último recurso absoluto; un driver que solicita los dispositivos que ningún otro ha reclamado.
- `BUS_PROBE_NOWILDCARD` = -2000000000. Marcador para casos especiales utilizado por el mecanismo identify de newbus.

La mayoría de los drivers que escribas o leas utilizarán `BUS_PROBE_DEFAULT`. Algunos utilizan `BUS_PROBE_VENDOR` cuando se espera que coexistan con drivers genéricos. Unos pocos utilizan `BUS_PROBE_GENERIC` o inferior para su modo de reserva. El driver del Capítulo 18 utiliza `BUS_PROBE_DEFAULT` en todo momento.

Los valores de prioridad son negativos por convención, de modo que gana el valor numéricamente más bajo. Un driver más específico tiene un valor más negativo. Esto resulta contraintuitivo a primera vista; un modelo mental útil es pensar en ello como "distancia respecto a la perfección, medida hacia abajo". `BUS_PROBE_SPECIFIC` tiene distancia cero. `BUS_PROBE_GENERIC` es cien unidades peor.

### Cómo escribir un driver PCI mínimo

Reuniendo todo lo anterior, aquí está el driver de la Etapa 1 del Capítulo 18, presentado como un único archivo autocontenido que surge del esqueleto del Capítulo 17. El archivo se llama `myfirst_pci.c`; es nuevo en el Capítulo 18 y convive con los archivos existentes `myfirst.c`, `myfirst_hw.c` y `myfirst_sim.c`.

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_pci.c -- Chapter 18 Stage 1 PCI probe/attach skeleton.
 *
 * At this stage the driver only probes, attaches, and detaches.
 * It does not yet claim BARs or touch device registers. Section 3
 * adds resource allocation. Section 5 wires the accessor layer to
 * the claimed BAR.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "myfirst.h"
#include "myfirst_pci.h"

static const struct myfirst_pci_id myfirst_pci_ids[] = {
	{ MYFIRST_VENDOR_REDHAT, MYFIRST_DEVICE_VIRTIO_RNG,
	    "Red Hat Virtio entropy source (myfirst demo target)" },
	{ 0, 0, NULL }
};

static int
myfirst_pci_probe(device_t dev)
{
	uint16_t vendor = pci_get_vendor(dev);
	uint16_t device = pci_get_device(dev);
	const struct myfirst_pci_id *id;

	for (id = myfirst_pci_ids; id->desc != NULL; id++) {
		if (id->vendor == vendor && id->device == device) {
			device_set_desc(dev, id->desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	sc->dev = dev;
	device_printf(dev,
	    "attaching: vendor=0x%04x device=0x%04x revid=0x%02x\n",
	    pci_get_vendor(dev), pci_get_device(dev), pci_get_revid(dev));
	device_printf(dev,
	    "           subvendor=0x%04x subdevice=0x%04x class=0x%02x\n",
	    pci_get_subvendor(dev), pci_get_subdevice(dev),
	    pci_get_class(dev));

	/*
	 * Stage 1 has no resources to claim and nothing to initialise
	 * beyond the softc pointer. Stage 2 will add the BAR allocation.
	 */
	return (0);
}

static int
myfirst_pci_detach(device_t dev)
{
	device_printf(dev, "detaching\n");
	return (0);
}

static device_method_t myfirst_pci_methods[] = {
	DEVMETHOD(device_probe,		myfirst_pci_probe),
	DEVMETHOD(device_attach,	myfirst_pci_attach),
	DEVMETHOD(device_detach,	myfirst_pci_detach),
	DEVMETHOD_END
};

static driver_t myfirst_pci_driver = {
	"myfirst",
	myfirst_pci_methods,
	sizeof(struct myfirst_softc),
};

DRIVER_MODULE(myfirst, pci, myfirst_pci_driver, NULL, NULL);
MODULE_DEPEND(myfirst, pci, 1, 1, 1);
MODULE_VERSION(myfirst, 1);
```

El archivo de cabecera complementario, `myfirst_pci.h`:

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_pci.h -- Chapter 18 PCI interface for the myfirst driver.
 */

#ifndef _MYFIRST_PCI_H_
#define _MYFIRST_PCI_H_

#include <sys/types.h>

/* Target vendor and device IDs for the Chapter 18 demo. */
#define MYFIRST_VENDOR_REDHAT		0x1af4
#define MYFIRST_DEVICE_VIRTIO_RNG	0x1005

/* A single entry in the supported-device table. */
struct myfirst_pci_id {
	uint16_t	vendor;
	uint16_t	device;
	const char	*desc;
};

#endif /* _MYFIRST_PCI_H_ */
```

Y el `Makefile` necesita una pequeña actualización:

```makefile
# Makefile for the Chapter 18 Stage 1 myfirst driver.

KMOD=  myfirst
SRCS=  myfirst.c myfirst_hw.c myfirst_sim.c myfirst_pci.c cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.1-pci-stage1\"

.include <bsd.kmod.mk>
```

Tres cosas han cambiado respecto a la Etapa 5 del Capítulo 17. `myfirst_pci.c` se añade a `SRCS`. La cadena de versión se actualiza a `1.1-pci-stage1`. No es necesario cambiar nada más.

### Qué ocurre cuando se carga el driver

Recorrer la secuencia de carga hace que el esqueleto cobre sentido.

El lector invoca `kldload ./myfirst.ko`. El cargador de módulos del kernel lee los metadatos del módulo. Ve la declaración `MODULE_DEPEND(myfirst, pci, ...)` y verifica que el módulo `pci` está cargado. (Siempre lo está en un kernel en ejecución, por lo que esta comprobación siempre pasa.) Ve la declaración `DRIVER_MODULE(myfirst, pci, ...)` y registra el driver como candidato para el probe del bus PCI.

El kernel itera entonces sobre cada dispositivo PCI del sistema y llama a `myfirst_pci_probe` para cada uno. La mayoría de los probes devuelven `ENXIO` porque los IDs de vendor y de dispositivo no coinciden. Un probe, contra el dispositivo virtio-rnd del invitado, devuelve `BUS_PROBE_DEFAULT`. El kernel selecciona `myfirst` como driver para ese dispositivo.

Si el dispositivo virtio-rnd ya está conectado a `virtio_random`, el resultado del probe del nuevo driver compite con el enlace existente. El kernel no vuelve a vincular un dispositivo automáticamente solo porque haya aparecido un nuevo driver; en su lugar, `myfirst` no se conectará. Para forzar la revinculación, el lector debe primero desconectar el driver existente: `devctl detach virtio_random0` o `kldunload virtio_random`. La Sección 7 lo explica paso a paso.

Una vez que el kernel decide que `myfirst` gana, asigna un nuevo softc (el bloque de `sizeof(struct myfirst_softc)` solicitado en la `driver_t`), lo inicializa a cero y llama a `myfirst_pci_attach`. La rutina attach se ejecuta. Imprime un breve mensaje de inicio. Devuelve 0. El kernel marca el dispositivo como conectado.

`dmesg` muestra la secuencia:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> port 0x6040-0x605f mem 0xc1000000-0xc100001f irq 19 at device 5.0 on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
```

`devinfo -v` muestra el dispositivo con su padre, sus recursos y su vinculación al driver. `pciconf -lv` lo muestra con `myfirst0` como nombre vinculado.

Al descargarse, ocurre el proceso inverso. `kldunload myfirst` llama a `myfirst_pci_detach` en cada dispositivo conectado. El detach imprime su propio mensaje, devuelve 0, y el kernel libera el softc. `DRIVER_MODULE` elimina el registro del driver del bus PCI. El cargador de módulos elimina la imagen `myfirst.ko` de la memoria.

### `device_printf` y por qué importa

Hay un pequeño detalle que merece la pena destacar. El esqueleto utiliza `device_printf(dev, ...)` en lugar de `printf(...)`. La diferencia es pequeña pero importante.

`printf` escribe en el registro del kernel sin ningún prefijo. Una línea que dice "attaching" es difícil de asociar con un dispositivo concreto; el registro está lleno de mensajes de todos los drivers del sistema. `device_printf(dev, ...)` antepone al mensaje el nombre del driver y el número de unidad: "myfirst0: attaching". El prefijo hace que el registro sea legible incluso cuando hay varias instancias del driver conectadas a la vez (`myfirst0`, `myfirst1`, etc.).

La convención es estricta en el árbol de código fuente de FreeBSD: todos los drivers utilizan `device_printf` en los flujos de código donde tienen una `device_t` a mano, y recurren a `printf` únicamente en la inicialización muy temprana del módulo o en su descarga, cuando el handle no está disponible. Un lector que utiliza `device_printf` de forma habitual produce registros fáciles de leer y diagnosticar; un lector que usa `printf` en todas partes produce registros que otros colaboradores pedirán que se corrijan.

### El softc y `device_get_softc`

El driver del Capítulo 17 ya tenía una estructura softc. La rutina attach del Capítulo 18 simplemente la reutiliza, con una adición: la capa PCI almacena la `device_t` en `sc->dev` para que el código posterior (incluidos los accesores del Capítulo 16 y la simulación del Capítulo 17) pueda acceder a ella.

Un recordatorio: `device_get_softc(dev)` devuelve un puntero al softc que newbus preasignó. El softc está a cero antes de que se ejecute attach, por lo que todos los campos comienzan en cero, NULL o false. El softc es liberado automáticamente por newbus tras el retorno de detach; el driver no llama a `free` sobre él.

Esto merece destacarse porque difiere del patrón de softc basado en `malloc` que se usaba en drivers más antiguos de FreeBSD y en algunos drivers de Linux. En newbus, el bus es el propietario del ciclo de vida del softc. En los patrones más antiguos, el driver era el propietario y tenía que recordar asignarlo y liberarlo. Olvidar la asignación en el modelo antiguo provoca una desreferencia nula en attach; olvidar la liberación en el modelo antiguo provoca una fuga de memoria en detach. Ninguno de estos fallos existe en el newbus moderno porque el bus se encarga de ambas operaciones.

### Orden de probe-attach-detach al descargar el módulo

Un detalle importante para el autor de un driver es lo que ocurre cuando se ejecuta `kldunload myfirst` mientras hay uno o más dispositivos `myfirst` conectados.

La ruta de descarga del cargador de módulos intenta primero desconectar cada dispositivo vinculado al driver. Para cada dispositivo, llama al método `detach` del driver. Si `detach` devuelve 0, el dispositivo se considera desvinculado y se libera el softc. Si `detach` devuelve un valor distinto de cero (normalmente `EBUSY`), el cargador de módulos cancela la descarga: el módulo permanece cargado, el dispositivo sigue conectado y la operación devuelve un error. Así es como un driver rechaza la descarga cuando todavía tiene trabajo pendiente.

El método `detach` del driver `myfirst` debería completarse con éxito en condiciones normales, porque el estado del driver de cara al usuario está inactivo en el momento en que se solicita la descarga. Sin embargo, un driver que está atendiendo activamente peticiones (por ejemplo, un driver de disco con descriptores de archivo abiertos sobre su cdev) devuelve `EBUSY` desde `detach` y obliga al usuario a cerrar primero esos descriptores.

En el Stage 1 del Capítulo 18, `detach` es una sola línea: imprime un mensaje y devuelve 0. En etapas posteriores, `detach` adquirirá el lock, cancelará los callouts, liberará los recursos y, por último, devolverá 0 una vez que todo esté desmontado.

### Salida del Stage 1: aspecto de una ejecución correcta

Carga el driver. En un guest de bhyve con un dispositivo virtio-rnd conectado y con `virtio_random` descargado previamente, `dmesg` debería mostrar algo parecido a:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
```

`kldstat -v | grep myfirst` muestra el driver cargado. `devinfo -v | grep myfirst` muestra el dispositivo conectado a `pci0`. `pciconf -lv | grep myfirst` confirma la coincidencia.

Al descargar:

```text
myfirst0: detaching
```

El dispositivo vuelve a quedar sin driver asignado. (O pasa a `virtio_random` si ese módulo se vuelve a cargar.)

Si el dispositivo virtio-rnd no está presente, no se produce ningún attach; el driver se carga, pero no aparece ningún `myfirst0` en `devinfo`. Si el driver se carga en un sistema sin el dispositivo, ocurre lo mismo: `probe` se ejecuta para cada dispositivo PCI del sistema, devuelve `ENXIO` para todos ellos y no se produce ningún attach. Este es el comportamiento correcto y esperado; el driver es paciente.

### Errores frecuentes en esta etapa

A continuación se enumeran algunas trampas habituales en las que el autor ha visto caer a los principiantes.

**Olvidar `MODULE_DEPEND`.** El driver se carga, pero durante el arranque provoca un pánico porque el módulo PCI aún no se ha inicializado. Añadir la declaración resuelve el problema. El síntoma es fácil de reconocer una vez que sabes qué buscar.

**Nombre incorrecto en `DRIVER_MODULE`.** El nombre debe coincidir con la cadena `"name"` de la estructura `driver_t`. Las discrepancias producen errores sutiles en los que el driver se carga pero nunca sondea ningún dispositivo. La solución es hacer que ambos coincidan; la convención es que los dos usen el nombre corto del driver.

**Devolver el valor incorrecto desde `probe`.** Un principiante a veces devuelve 0 desde `probe` pensando que «cero significa éxito». El cero es `BUS_PROBE_SPECIFIC`, que es la coincidencia más alta posible; el driver ganará a cualquier otro driver que quiera el mismo dispositivo. Esto casi nunca es lo que se pretende. Devuelve `BUS_PROBE_DEFAULT` para la coincidencia estándar.

**Devolver un código de error positivo.** La convención de newbus es que `probe` devuelva un valor de prioridad negativo o un errno positivo. Devolver el signo incorrecto es un error tipográfico habitual. `ENXIO` es el valor de retorno correcto para indicar «no coincide».

**Dejar recursos asignados en `probe`.** `probe` debe estar libre de efectos secundarios. Si `probe` asigna un recurso, debe liberarlo antes de retornar. El enfoque más limpio es no asignar nunca recursos desde `probe`; hazlo todo en `attach`.

**Confundir `pci_get_vendor` con `pci_read_config`.** Son funciones distintas. `pci_get_vendor` lee una ivar en caché. `pci_read_config(dev, PCIR_VENDOR, 2)` lee el espacio de configuración en tiempo real. Ambas devuelven el mismo valor para este campo, pero una es una función inline de bajo coste y la otra es una transacción de bus. Usa el accessor.

**Olvidar incluir las cabeceras correctas.** `dev/pci/pcireg.h` define las constantes `PCIR_*`. `dev/pci/pcivar.h` define `pci_get_vendor` y funciones relacionadas. Es necesario incluir ambas. El error del compilador suele ser «identificador no definido» para `pci_get_vendor`; la solución es añadir el include que falta.

**Colisión de nombre con `MODULE_VERSION`.** El primer argumento debe coincidir con el nombre del driver. `MODULE_VERSION(myfirst, 1)` es correcto. `MODULE_VERSION(myfirst_pci, 1)` no lo es, porque `myfirst_pci` es un nombre de archivo, no un nombre de módulo. El cargador de módulos busca los módulos por el nombre registrado en `DRIVER_MODULE`.

Todos estos errores tienen solución. El kernel de depuración detecta algunos de ellos (el caso de carga antes de que el módulo PCI esté inicializado produce un pánico que el kernel de depuración muestra de forma legible). Los demás producen comportamientos incorrectos sutiles que se detectan más fácilmente probando con cuidado el ciclo carga-attach-detach-descarga tras cada cambio.

### Punto de control: Stage 1 en funcionamiento

Antes de pasar a la Sección 3, comprueba que el driver del Stage 1 funciona de principio a fin.

En el guest de bhyve o QEMU:

- `kldload virtio_pci` (si no está ya cargado).
- `kldunload virtio_random` (si está cargado; falla sin consecuencias si no lo está).
- `kldload ./myfirst.ko`.
- `kldstat -v | grep myfirst` debería mostrar el módulo cargado.
- `devinfo -v | grep myfirst` debería mostrar `myfirst0` conectado bajo `pci0`.
- `dmesg` debería mostrar el mensaje de attach.
- `kldunload myfirst`.
- `dmesg` debería mostrar el mensaje de detach.
- `devinfo -v | grep myfirst` no debería mostrar nada.

Si todas estas comprobaciones pasan, tienes un driver del Stage 1 que funciona. El siguiente paso es reclamar el BAR.

### Cerrando la Sección 2

La secuencia probe-attach-detach es el esqueleto de todo driver PCI. La Sección 2 la construyó en su forma más reducida: un `probe` que coincide con un par vendedor-dispositivo, un `attach` que imprime un mensaje, un `detach` que imprime otro mensaje, y el pegamento necesario (`DRIVER_MODULE`, `MODULE_DEPEND`, `MODULE_VERSION`) para que el cargador de módulos del kernel y el enumerador de newbus lo acepten.

Lo que el esqueleto del Stage 1 aún no hace: reclamar un BAR, leer un registro, habilitar el bus mastering, recorrer la lista de capacidades, crear un cdev ni coordinar el build de PCI con el build de simulación del Capítulo 17. Todos estos temas se tratan en secciones posteriores de este capítulo. El esqueleto es importante porque todos los temas posteriores se integran en él sin necesidad de reestructurarlo. `attach` pasa de ser una función de dos líneas a una de veinte conforme avanza el capítulo; `probe` permanece exactamente igual.

La Sección 3 presenta los BAR. Explica qué son, cómo se asignan y cómo un driver reclama el rango de memoria que describe un BAR. Al final de la Sección 3, el driver dispondrá de un `struct resource *` para su BAR y de un par etiqueta-handle listo para pasarlo a la capa de accesores del Capítulo 16.



## Sección 3: comprensión y reclamación de recursos PCI

Con un `probe` y un `attach` básico en su lugar, el driver sabe cuándo ha encontrado el dispositivo que quiere controlar. Lo que aún no sabe es cómo acceder a los registros de ese dispositivo. La Sección 3 cierra esa brecha. Comienza explicando qué es un BAR en la especificación PCI, recorre el proceso por el que el firmware y el kernel lo configuran, y termina con el código del driver que reclama un BAR y lo convierte en una etiqueta y un handle de `bus_space` que los accesores del Capítulo 16 pueden usar sin modificaciones.

El objetivo de esta sección es hacer que el término «BAR» sea algo concreto. Quien termine la Sección 3 debería ser capaz de responder en una frase: un BAR es un campo del espacio de configuración en el que un dispositivo indica «esta es la cantidad de memoria (o el número de puertos de I/O) que necesito, y así es como puedes acceder a ella una vez que el firmware la ha mapeado en el espacio de direcciones del host». Todo lo demás de la sección parte de esa frase.

### Qué es exactamente un BAR

Todo dispositivo PCI anuncia los recursos que necesita a través de los Base Address Registers (registros de dirección base). Una cabecera de dispositivo PCI estándar (del tipo no puente) tiene seis BAR, cada uno de cuatro bytes de ancho, en los desplazamientos `0x10`, `0x14`, `0x18`, `0x1c`, `0x20` y `0x24` del espacio de configuración. En el archivo `/usr/src/sys/dev/pci/pcireg.h` de FreeBSD, estos desplazamientos los genera la macro `PCIR_BAR(n)`, donde `n` va de 0 a 5.

Cada BAR describe un rango de direcciones. El bit menos significativo de un BAR indica al software si el rango se encuentra en el espacio de memoria o en el espacio de puertos de I/O. Si ese bit es cero, el rango está mapeado en memoria; si es uno, el rango pertenece al espacio de direcciones de puertos de I/O. Todo lo que está por encima de los bits inferiores es una dirección; la distribución exacta de los campos depende del tipo de BAR.

Para un BAR mapeado en memoria, la distribución es:

- Bit 0: `0` para memoria.
- Bits 2-1: tipo. `0b00` para 32 bits, `0b10` para 64 bits, `0b01` reservado (anteriormente «por debajo de 1 MB»).
- Bit 3: prefetchable. `1` si el dispositivo garantiza que las lecturas no tienen efectos secundarios, de modo que la CPU puede hacer prefetch y combinar accesos.
- Bits 31-4 (o 63-4 para 64 bits): la dirección.

Un BAR de 64 bits ocupa dos ranuras de BAR consecutivas. La ranura baja contiene los 32 bits inferiores de la dirección (junto con los bits de tipo); la ranura alta contiene los 32 bits superiores. Un driver que recorre la lista de BAR debe reconocer cuándo ha encontrado un BAR de 64 bits y saltar la ranura superior consumida.

Para un BAR de puertos de I/O:

- Bit 0: `1` para I/O.
- Bit 1: reservado.
- Bits 31-2: la dirección del puerto.

Los BAR de puertos de I/O son menos habituales en los dispositivos modernos. La mayoría de los dispositivos PCIe modernos utilizan exclusivamente BAR mapeados en memoria. El Capítulo 18 se centra en los BAR mapeados en memoria.

### Cómo recibe una dirección un BAR

Un BAR se escribe en dos pasadas. La primera pasada es la que especificó el diseñador del silicio: una lectura de un BAR devuelve los requisitos del dispositivo. El campo de tipo de los bits bajos es de solo lectura. El campo de dirección es de lectura y escritura, pero con una salvedad: escribir todos unos en el campo de dirección y leer de vuelta el valor resultante indica al firmware cuál es el tamaño del rango. El dispositivo devuelve un valor en el que los bits bajos (los correspondientes al tamaño) son cero y los bits altos (los que el dispositivo no implementa) devuelven lo que se escribió. El firmware interpreta el valor leído como una máscara de tamaño.

La segunda pasada asigna la dirección real. El firmware (BIOS o UEFI) recorre todos los BAR de cada dispositivo PCI, anota el tamaño que requiere cada uno, particiona el espacio de direcciones del host para satisfacerlos todos y escribe la dirección asignada de vuelta en cada BAR. En el momento en que arranca el sistema operativo, cada BAR tiene una dirección real que el SO puede utilizar para acceder al dispositivo.

El SO puede volver a realizar la asignación si lo desea (para dar soporte a la conexión en caliente o si el firmware hizo un trabajo deficiente). FreeBSD acepta en su mayor parte la asignación del firmware; el sysctl `hw.pci.realloc_bars` y la lógica de `bus_generic_probe` se encargan del caso poco habitual en que se necesita reasignar.

Desde el punto de vista del driver, todo esto está hecho antes de que se ejecute `attach`. El BAR tiene una dirección, esa dirección está mapeada en el espacio virtual del kernel, y el driver solo necesita solicitar el recurso por número.

### El argumento `rid` y `PCIR_BAR`

El driver reclama un BAR llamando a `bus_alloc_resource_any(9)` con un ID de recurso (habitualmente denominado `rid`) que identifica qué BAR se desea asignar. Para un BAR mapeado en memoria, el `rid` es el desplazamiento en el espacio de configuración de ese BAR, generado por la macro `PCIR_BAR(n)`:

- `PCIR_BAR(0)` = `0x10` (BAR 0)
- `PCIR_BAR(1)` = `0x14` (BAR 1)
- ...
- `PCIR_BAR(5)` = `0x24` (BAR 5)

Pasar `PCIR_BAR(0)` a `bus_alloc_resource_any` solicita el BAR 0. Pasar `PCIR_BAR(1)` solicita el BAR 1. La macro ocupa una sola línea en `pcireg.h`:

```c
#define	PCIR_BAR(x)	(PCIR_BARS + (x) * 4)
```

donde `PCIR_BARS` es `0x10`.

Los principiantes a veces pasan `0` o `1` como `rid` y se sorprenden cuando la asignación falla. El `rid` no es un índice de BAR; es el desplazamiento. Usa `PCIR_BAR(index)` a menos que tengas una razón específica para pasar un desplazamiento directo.

### El tipo de recurso: `SYS_RES_MEMORY` frente a `SYS_RES_IOPORT`

`bus_alloc_resource_any` recibe un argumento de tipo que indica al kernel qué clase de recurso necesita el driver. Para un BAR de memoria, el tipo es `SYS_RES_MEMORY`. Para un BAR de puertos de I/O, el tipo es `SYS_RES_IOPORT`. Para una interrupción, es `SYS_RES_IRQ`. El reducido conjunto de tipos de recurso está definido en `/usr/src/sys/arm64/include/resource.h` (y en los equivalentes por arquitectura); memoria, puerto de I/O e IRQ son los tres que un driver PCI utiliza habitualmente.

El espacio de configuración PCI en sí no se reserva mediante `bus_alloc_resource_any`. El driver accede a él a través de `pci_read_config(9)` y `pci_write_config(9)`, que enrutan el acceso por el driver del bus PCI sin necesidad de un handle de recurso.

Un driver que no sabe si su BAR es de memoria o de I/O puede inspeccionar el bit más bajo del BAR en el espacio de configuración para averiguarlo. Un driver que sí lo sabe (porque el datasheet lo indica, o porque el dispositivo siempre ha sido MMIO en este capítulo) simplemente pasa el tipo correcto.

La mayoría de los dispositivos PCIe tienen su interfaz principal en el espacio de memoria y una ventana de compatibilidad opcional en el espacio de puertos de I/O. Un driver suele solicitar primero el BAR de memoria y, si eso falla, recurre al BAR de puerto de I/O. El driver del capítulo 18 solicita únicamente memoria; el dispositivo virtio-rnd al que apunta expone sus registros en un BAR de memoria.

### El indicador RF_ACTIVE

`bus_alloc_resource_any` también recibe un argumento de flags. Los dos más comúnmente establecidos son:

- `RF_ACTIVE`: activa el recurso como parte de la asignación. Sin este indicador, la asignación reserva el recurso pero no lo mapea; el driver debe llamar a `bus_activate_resource(9)` por separado. Con él, el recurso se asigna y activa en un solo paso.
- `RF_SHAREABLE`: el recurso puede compartirse con otros drivers. Esto importa para las interrupciones (IRQ compartidas entre múltiples dispositivos); importa menos para los BARs de memoria.

Para un BAR de memoria, el caso habitual es `RF_ACTIVE` solo. Para una IRQ que podría compartirse en un sistema legacy, es `RF_ACTIVE | RF_SHAREABLE`. El capítulo 18 usa únicamente `RF_ACTIVE`.

### bus_alloc_resource_any en detalle

La firma de la función es:

```c
struct resource *bus_alloc_resource_any(device_t dev, int type,
    int *rid, u_int flags);
```

Tres argumentos, más un valor de retorno.

`dev` es el handle del dispositivo. `type` es `SYS_RES_MEMORY`, `SYS_RES_IOPORT` o `SYS_RES_IRQ`. `rid` es un puntero a un entero que contiene el identificador del recurso; el kernel puede actualizarlo (por ejemplo, para indicar al driver qué slot se utilizó realmente cuando el driver pasó un comodín). `flags` es la máscara de bits descrita anteriormente.

El valor de retorno es un `struct resource *`. No es NULL si la operación tiene éxito; es NULL si falla. El handle del recurso es lo que usan todas las operaciones posteriores (lecturas, escrituras, liberación).

Una llamada típica tiene este aspecto:

```c
int rid = PCIR_BAR(0);
struct resource *bar;

bar = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
if (bar == NULL) {
	device_printf(dev, "cannot allocate BAR0\n");
	return (ENXIO);
}
```

Tras la llamada, `bar` apunta a un recurso asignado y activado; `rid` puede haber sido actualizado por el kernel si este eligió un slot distinto al que pidió el driver (en las asignaciones con comodín, aquí es donde el slot elegido queda visible).

### De recurso a tag y handle

El handle del recurso es la conexión del driver con el BAR, pero la capa de acceso del capítulo 16 espera un `bus_space_tag_t` y un `bus_space_handle_t`, no un `struct resource *`. Dos funciones auxiliares convierten uno en el otro:

- `rman_get_bustag(res)` devuelve el `bus_space_tag_t`.
- `rman_get_bushandle(res)` devuelve el `bus_space_handle_t`.

Ambas son funciones auxiliares inline definidas en `/usr/src/sys/sys/rman.h`. El recurso almacena internamente el tag y el handle; las funciones de acceso devuelven los valores almacenados. El driver guarda entonces el tag y el handle en su propio estado (en el capítulo 18, en la capa de hardware `struct myfirst_hw`) para que los accesores del capítulo 16 puedan usarlos.

El patrón es breve:

```c
sc->hw->regs_tag = rman_get_bustag(bar);
sc->hw->regs_handle = rman_get_bushandle(bar);
```

Tras estas dos líneas, `CSR_READ_4(sc, off)` y `CSR_WRITE_4(sc, off, val)` operan sobre el BAR real. Ningún otro código del driver necesita saber que el backend ha cambiado.

### rman_get_size y rman_get_start

Dos funciones auxiliares adicionales extraen el rango de direcciones que cubre el recurso:

- `rman_get_size(res)` devuelve el número de bytes.
- `rman_get_start(res)` devuelve la dirección de inicio física o de bus.

El driver usa `rman_get_size` para comprobar que el BAR es suficientemente grande para los registros que espera el driver. Un dispositivo cuyo BAR es más pequeño de lo que espera el capítulo, o bien está mal identificado (dispositivo incorrecto detrás del par de ID) o es una variante que el driver no admite. En cualquier caso, una comprobación fallida durante el attach es mejor que un acceso corrupto en tiempo de ejecución.

`rman_get_start` es útil principalmente para el registro de diagnóstico. La dirección física del BAR no es algo que el driver desreferencie directamente (lo que el tag y el handle envuelven es el mapeo del kernel), pero imprimirla ayuda durante la depuración porque conecta la salida de `pciconf -lv` con la visión del driver.

### Liberación del BAR

La contraparte de `bus_alloc_resource_any` es `bus_release_resource(9)`. La firma es:

```c
int bus_release_resource(device_t dev, int type, int rid, struct resource *res);
```

`dev`, `type` y `rid` coinciden con la llamada de asignación; `res` es el handle devuelto por la asignación. Si la operación tiene éxito, la función devuelve 0; si falla, devuelve un errno. Los fallos son poco frecuentes porque el recurso acaba de ser asignado por este driver, pero los drivers defensivos comprueban el valor de retorno y registran el fallo.

El driver siempre debe liberar todos los recursos que asignó, en orden inverso al de la asignación. El driver del capítulo 18 en Stage 2 asigna un BAR; liberará ese BAR en detach. Las etapas posteriores, cuando las interrupciones y el DMA entren en escena en los capítulos 19 al 21, asignarán más.

### Fallo parcial en attach

Un punto sutil sobre el attach. Si el driver reclama el BAR con éxito pero falla en un paso posterior (por ejemplo, el registro `DEVICE_ID` esperado del dispositivo no coincide), el driver debe liberar el BAR antes de devolver el error. Olvidar la liberación es una fuga de recurso: el gestor de recursos del kernel sigue creyendo que el BAR está asignado por este driver, aunque el driver haya retornado. El siguiente intento de attach fallará.

El patrón habitual es el conocido patrón de limpieza basado en goto:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int rid, error;

	sc->dev = dev;
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		error = ENXIO;
		goto fail;
	}

	error = myfirst_hw_attach_pci(sc);
	if (error != 0)
		goto fail_release;

	/* ... */
	return (0);

fail_release:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail:
	return (error);
}
```

La cascada de `goto` es un patrón habitual, no un defecto de diseño. Mantiene el código de limpieza en un solo lugar y hace que el emparejamiento asignación-liberación sea simétrico. El patrón se introdujo en el capítulo 15 para la limpieza de mutex y callout; aquí se extiende a la limpieza de recursos. El attach final del capítulo 18 usa una versión más larga de esta cascada para gestionar la inicialización del softc, la asignación del BAR, el attach de la capa de hardware y la creación del cdev como cuatro asignaciones escalonadas.

### Qué vive en el softc

El capítulo 18 añade algunos campos al softc. Se declaran en `myfirst.h` (la cabecera principal del driver, no `myfirst_pci.h`, porque el softc es compartido entre todas las capas).

```c
struct myfirst_softc {
	device_t dev;
	/* ... Chapter 10 through 17 fields ... */

	/* Chapter 18 PCI fields. */
	struct resource	*bar_res;
	int		 bar_rid;
	bool		 pci_attached;
};
```

`bar_res` es el handle del BAR reclamado. `bar_rid` es el identificador de recurso usado para asignarlo (almacenado para que detach pueda pasar el valor correcto a `bus_release_resource`). `pci_attached` es un indicador que el código posterior usa para distinguir el camino de attach PCI real del camino de attach simulado.

Un solo BAR es suficiente para el driver del capítulo 18. Los drivers de dispositivos más complejos tendrían `bar0_res`, `bar0_rid`, `bar1_res`, `bar1_rid`, y así sucesivamente, cada par correspondiendo a un BAR. El dispositivo virtio-rnd tiene solo un BAR, así que el driver tiene solo un par.

### El attach de Stage 2

Integrando la asignación en la rutina attach de Stage 2:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error = 0;

	sc->dev = dev;

	/* Allocate BAR0 as a memory resource. */
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		return (ENXIO);
	}

	device_printf(dev, "BAR0 allocated: %#jx bytes at %#jx\n",
	    (uintmax_t)rman_get_size(sc->bar_res),
	    (uintmax_t)rman_get_start(sc->bar_res));

	sc->pci_attached = true;
	return (error);
}
```

Un attach de Stage 2 exitoso imprime una línea como:

```text
myfirst0: BAR0 allocated: 0x20 bytes at 0xc1000000
```

El tamaño y la dirección dependen del layout del guest. Lo importante es que la asignación tuvo éxito, que el tamaño es el que el driver esperaba (el dispositivo virtio-rnd expone al menos 32 bytes de registros), y que el camino de detach libera el recurso.

### El detach de Stage 2

El detach de Stage 2 debe liberar lo que el attach asignó:

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}
	sc->pci_attached = false;
	device_printf(dev, "detaching\n");
	return (0);
}
```

La comprobación `if` es defensiva: en principio `sc->bar_res` no es NULL cuando detach se llama tras un attach exitoso, pero añadir la comprobación no cuesta nada y hace que el detach sea robusto ante casos de fallo parcial que puedan surgir en refactorizaciones futuras. Asignar NULL a `bar_res` tras la liberación evita una doble liberación si algo llama a detach de nuevo más adelante.

### Lo que Stage 2 aún no hace

Al final de Stage 2, el driver asigna un BAR pero no hace nada con él. El tag y el handle están disponibles pero aún no están conectados a los accesores del capítulo 16. La simulación del capítulo 17 sigue ejecutándose, pero lo hace contra el bloque de registros asignado con `malloc(9)`, no contra el BAR real.

La sección 4 cierra esa brecha. Toma el tag y el handle de Stage 2 y los pasa a `myfirst_hw_attach` para que `CSR_READ_4` y `CSR_WRITE_4` operen sobre el hardware real. Tras la sección 4, la simulación del capítulo 17 pasa a ser una opción en tiempo de ejecución en lugar del único backend.

### Verificación de Stage 2

Antes de pasar a la sección 4, confirma que Stage 2 funciona de extremo a extremo.

```sh
# In the bhyve guest:
sudo kldunload virtio_random  # may not be loaded
sudo kldload ./myfirst.ko
sudo dmesg | grep myfirst | tail -5
```

La salida debería tener este aspecto:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
myfirst0: BAR0 allocated: 0x20 bytes at 0xc1000000
```

`devinfo -v | grep -A 2 myfirst0` debería mostrar la reclamación del recurso:

```text
myfirst0
    pnpinfo vendor=0x1af4 device=0x1005 ...
    resources:
        memory: 0xc1000000-0xc100001f
```

El rango de memoria impreso por `devinfo -v` coincide con el rango impreso por el driver. Esto confirma que la asignación tuvo éxito y que el kernel ve el BAR como reclamado por `myfirst`.

Descarga el módulo y verifica la limpieza:

```sh
sudo kldunload myfirst
sudo devinfo -v | grep myfirst  # should return nothing
```

Sin dispositivos persistentes, sin recursos con fugas. Stage 2 está completo.

### Errores comunes en la asignación de BAR

Una breve lista de las trampas más habituales.

**Pasar `0` como rid para BAR 0.** El rid es `PCIR_BAR(0)` = `0x10`, no `0`. Pasar `0` solicita un recurso en el desplazamiento 0, que es el campo `PCIR_VENDOR`; la asignación falla o produce resultados inesperados. Utiliza siempre `PCIR_BAR(index)`.

**Olvidar `RF_ACTIVE`.** Sin este indicador, `bus_alloc_resource_any` asigna pero no activa. Leer desde el tag y el handle en ese punto es comportamiento indefinido. El síntoma habitual es un fallo de página o valores basura. La solución es pasar `RF_ACTIVE`.

**Usar el tipo de recurso incorrecto.** Pasar `SYS_RES_IOPORT` para un BAR de memoria produce un fallo de asignación inmediato. Pasar `SYS_RES_MEMORY` para un puerto I/O hace lo mismo. El tipo debe coincidir con el tipo real del BAR. Si el driver no lo sabe de antemano (un driver genérico que admite variantes tanto de memoria como de I/O), lee `PCIR_BAR(index)` del espacio de configuración y comprueba el bit bajo.

**No liberar en caso de fallo parcial.** Un error habitual en principiantes: attach reclama el BAR, un paso posterior falla, la función devuelve el error, y el BAR nunca se libera. El recurso queda con fuga. El siguiente intento de attach falla porque el BAR sigue reclamado.

**Liberar el BAR antes de que la capa de acceso haya terminado con él.** El error inverso: detach libera el BAR demasiado pronto, antes de vaciar los callouts o tasks que aún podrían estar leyendo de él. El síntoma es un fallo de página dentro de un callout poco después de `kldunload`. La solución es vaciar todo lo que pueda acceder al BAR y luego liberar.

**Confundir `rman_get_size` con `rman_get_end`.** `rman_get_size(res)` devuelve el número de bytes. `rman_get_end(res)` devuelve la dirección del último byte (inicio más tamaño menos uno). Usa `rman_get_size` para las comprobaciones de sanidad sobre el tamaño del BAR; usa `rman_get_start` y `rman_get_end` para la impresión de diagnóstico.

**Asumir que los BARs están en un orden determinado.** El driver debe nombrar explícitamente el BAR que quiere (pasando `PCIR_BAR(n)`). Algunos dispositivos tienen su BAR principal en el índice 0; otros lo tienen en el índice 2. La hoja de datos (o la salida de `pciconf -lv` para el dispositivo concreto) indica dónde está. Asumir BAR 0 sin verificarlo es un error frecuente.

### Una nota sobre los BARs de 64 bits

El dispositivo virtio-rnd usado en el capítulo 18 tiene un BAR de 32 bits, por lo que la asignación mostrada aquí funciona sin tratamiento especial. Para dispositivos con BARs de 64 bits hay dos detalles importantes:

En primer lugar, el BAR ocupa dos slots consecutivos en la tabla de BARs del espacio de configuración. BAR 0 (en el desplazamiento `0x10`) contiene los 32 bits inferiores; BAR 1 (en el desplazamiento `0x14`) contiene los 32 bits superiores. Un driver que recorra la tabla de BARs con simples incrementos enteros trataría por error BAR 1 como un BAR independiente. El recorrido correcto lee los bits de tipo de cada BAR y salta el slot siguiente si el slot actual es un BAR de 64 bits.

En segundo lugar, el `rid` pasado a `bus_alloc_resource_any` es el desplazamiento del slot inferior. El kernel reconoce el tipo de 64 bits y trata el par como un único recurso. El driver no necesita asignar dos recursos para un BAR de 64 bits; una sola asignación con `rid = PCIR_BAR(0)` gestiona ambos slots.

Para el driver del capítulo 18, esto es académico; el dispositivo objetivo tiene BARs de 32 bits. Pero un lector que trabaje posteriormente con un dispositivo que tenga un BAR de 64 bits necesitará estos detalles. `/usr/src/sys/dev/pci/pcireg.h` define `PCIM_BAR_MEM_TYPE`, `PCIM_BAR_MEM_32` y `PCIM_BAR_MEM_64` para ayudar con la inspección de BARs.

### BARs prefetchable vs no prefetchable

Un detalle relacionado. Un BAR es prefetchable si su bit 3 está activo. Prefetchable significa "las lecturas de este rango no tienen efectos secundarios, por lo que la CPU puede cachear, precargar y fusionar accesos como haría con la RAM normal". No prefetchable significa "las lecturas tienen efectos secundarios, por lo que cada acceso debe llegar al dispositivo; la CPU no debe cachear, precargar ni fusionar".

Los registros de dispositivo son casi siempre no prefetchables. La lectura de un registro de estado puede borrar los flags; una lectura con prefetch sería un error catastrófico. La memoria de dispositivo (un frame buffer en una tarjeta gráfica o un ring buffer en una NIC) suele ser prefetchable.

El driver no controla directamente el atributo de prefetch; es el BAR quien lo declara, y el kernel configura el mapeo en consecuencia. La tarea del driver es usar `bus_space_read_*` y `bus_space_write_*` correctamente. La capa `bus_space` gestiona los detalles de ordenación y caché. Un driver que intenta atajar saltándose `bus_space` y desreferenciando directamente un puntero podría obtener accidentalmente un mapeo cacheado sobre un BAR no prefetchable y producir un driver que funciona en condiciones ideales pero falla misteriosamente bajo carga.

El capítulo 16 argumentó a favor de `bus_space` en términos generales; la sección 3 del capítulo 18 confirma que ese argumento se extiende a dispositivos PCI reales. No hay atajos.

### Cerrando la sección 3

Un BAR es un rango de direcciones donde el dispositivo expone sus registros. El firmware asigna las direcciones de los BAR durante el boot; el kernel las lee en la enumeración PCI; el driver las reclama en attach mediante `bus_alloc_resource_any(9)` con el tipo, el `rid` y los flags correctos. El `struct resource *` devuelto lleva un `bus_space_tag_t` y un `bus_space_handle_t` que `rman_get_bustag(9)` y `rman_get_bushandle(9)` extraen. El detach debe liberar todos los recursos asignados en orden inverso.

El driver de la etapa 2 asigna el BAR 0 pero aún no lo utiliza. La sección 4 conecta el tag y el handle a la capa de acceso del capítulo 16, de modo que `CSR_READ_4` y `CSR_WRITE_4` operen finalmente sobre el BAR real en lugar de sobre un bloque de `malloc(9)`.



## Sección 4: Acceso a registros de dispositivo mediante bus_space(9)

La sección 3 terminó con un tag y un handle en manos del driver. El tag y el handle apuntan al BAR real; los accesores del capítulo 16 esperan exactamente ese par. La sección 4 establece la conexión. Enseña cómo entregar el tag y el handle, obtenidos vía PCI, a la capa de hardware; confirma que las macros `CSR_*` del capítulo 16 funcionan sin cambios frente a un BAR PCI real; lee el primer registro real del driver mediante `bus_space_read_4(9)`, y analiza los patrones de acceso (`bus_space_read_multi`, `bus_space_read_region`, las barreras) que el capítulo 16 introdujo y que la ruta PCI volverá a utilizar.

El tema de la sección 4 es la continuidad. Llevas dos capítulos escribiendo código de acceso a registros. El capítulo 18 no cambia ese código. Lo que cambia es de dónde vienen el tag y el handle. Los accesores son exactamente los mismos accesores; las macros envolventes son exactamente las mismas macros; la disciplina de locks es exactamente la misma disciplina. Este es el beneficio de la abstracción `bus_space(9)`. Las capas superiores del driver no saben, ni deben saber, que el origen del bloque de registros ha cambiado.

### Revisando los accesores del capítulo 16

Un breve recordatorio. `myfirst_hw.c` define tres funciones públicas que el resto del driver llama:

- `myfirst_reg_read(sc, off)` devuelve un valor de 32 bits del registro situado en el desplazamiento dado.
- `myfirst_reg_write(sc, off, val)` escribe un valor de 32 bits en el registro situado en el desplazamiento dado.
- `myfirst_reg_update(sc, off, clear, set)` realiza una operación read-modify-write: lee, borra los bits indicados, activa los bits indicados y escribe.

Las tres están envueltas por las macros `CSR_*` definidas en `myfirst_hw.h`:

- `CSR_READ_4(sc, off)` se expande a `myfirst_reg_read(sc, off)`.
- `CSR_WRITE_4(sc, off, val)` se expande a `myfirst_reg_write(sc, off, val)`.
- `CSR_UPDATE_4(sc, off, clear, set)` se expande a `myfirst_reg_update(sc, off, clear, set)`.

Los accesores llegan a `bus_space` a través de dos campos en `struct myfirst_hw`:

- `hw->regs_tag` de tipo `bus_space_tag_t`
- `hw->regs_handle` de tipo `bus_space_handle_t`

La llamada real dentro de `myfirst_reg_read` es:

```c
value = bus_space_read_4(hw->regs_tag, hw->regs_handle, offset);
```

y dentro de `myfirst_reg_write`:

```c
bus_space_write_4(hw->regs_tag, hw->regs_handle, offset, value);
```

Estas líneas no saben nada de PCI. No saben nada de `malloc`. No saben si `hw->regs_tag` proviene de una configuración de pmap simulada en el capítulo 16 o de una llamada a `rman_get_bustag(9)` en el capítulo 18. Su contrato no ha cambiado.

### Los dos orígenes de un tag y un handle

El capítulo 16 usó un truco para producir un tag y un handle a partir de una asignación de `malloc(9)` en x86. El truco era sencillo: la implementación de `bus_space` para x86 usa `x86_bus_space_mem` como tag para los accesos mapeados en memoria, y un handle no es más que una dirección virtual. Un buffer asignado con `malloc` tiene una dirección virtual, así que hacer un cast del puntero del buffer a `bus_space_handle_t` produce un handle utilizable. El truco es específico de x86; en otras arquitecturas, un bloque simulado necesitaría un enfoque diferente.

El capítulo 18 usa la ruta correcta: `bus_alloc_resource_any(9)` asigna un BAR como recurso, y `rman_get_bustag(9)` y `rman_get_bushandle(9)` extraen el tag y el handle que el kernel ha configurado. El driver no ve la dirección física; no ve el mapeo virtual; ve un tag y un handle opacos que el código de plataforma del kernel ha preparado correctamente. Los accesores los utilizan, y la lectura del registro alcanza el dispositivo real.

Esta es la forma fundamental de la integración PCI. Dos orígenes distintos del tag y el handle. Un único conjunto de accesores que los utiliza. El driver elige qué origen está activo en el momento del attach, y los accesores no necesitan saber cuál se eligió.

### Extendiendo myfirst_hw_attach

El `myfirst_hw_attach` del capítulo 16 asigna un buffer con `malloc(9)` y sintetiza un tag y un handle. El capítulo 18 necesita una segunda ruta de código que tome un tag y un handle ya existentes (del BAR PCI) y los almacene directamente. La forma más sencilla es renombrar la versión del capítulo 16 e introducir una nueva versión para la ruta PCI.

La nueva cabecera, ajustada para el capítulo 18:

```c
/* Chapter 16 behaviour: allocate a malloc-backed register block. */
int myfirst_hw_attach_sim(struct myfirst_softc *sc);

/* Chapter 18 behaviour: use an already-allocated resource. */
int myfirst_hw_attach_pci(struct myfirst_softc *sc,
    struct resource *bar, bus_size_t bar_size);

/* Shared teardown; safe with either backend. */
void myfirst_hw_detach(struct myfirst_softc *sc);
```

El attach de la ruta PCI almacena el tag y el handle directamente:

```c
int
myfirst_hw_attach_pci(struct myfirst_softc *sc, struct resource *bar,
    bus_size_t bar_size)
{
	struct myfirst_hw *hw;

	if (bar_size < MYFIRST_REG_SIZE) {
		device_printf(sc->dev,
		    "BAR is too small: %ju bytes, need at least %u\n",
		    (uintmax_t)bar_size, (unsigned)MYFIRST_REG_SIZE);
		return (ENXIO);
	}

	hw = malloc(sizeof(*hw), M_MYFIRST, M_WAITOK | M_ZERO);

	hw->regs_buf = NULL;			/* no malloc block */
	hw->regs_size = (size_t)bar_size;
	hw->regs_tag = rman_get_bustag(bar);
	hw->regs_handle = rman_get_bushandle(bar);
	hw->access_log_enabled = true;
	hw->access_log_head = 0;

	sc->hw = hw;

	device_printf(sc->dev,
	    "hardware layer attached to BAR: %zu bytes "
	    "(tag=%p handle=%p)\n",
	    hw->regs_size, (void *)hw->regs_tag,
	    (void *)hw->regs_handle);
	return (0);
}
```

Hay varios aspectos que conviene señalar. `hw->regs_buf` es NULL porque no hay ninguna asignación de `malloc` que respalde los registros en este caso; lo que el tag y el handle señalan es el mapeo del kernel sobre el BAR. `hw->regs_size` es el tamaño del BAR, comprobado frente al tamaño mínimo que el driver espera. El tag y el handle provienen del `struct resource *` que la ruta de attach PCI asignó. Todo lo demás en `myfirst_hw` permanece sin cambios.

El detach compartido es donde los dos backends convergen:

```c
void
myfirst_hw_detach(struct myfirst_softc *sc)
{
	struct myfirst_hw *hw;

	if (sc->hw == NULL)
		return;

	hw = sc->hw;
	sc->hw = NULL;

	/*
	 * Free the simulated backing buffer only if the simulation
	 * attach produced one. The PCI path sets regs_buf to NULL and
	 * leaves regs_size as the BAR size; the BAR itself is released
	 * by the PCI layer (see myfirst_pci_detach).
	 */
	if (hw->regs_buf != NULL) {
		free(hw->regs_buf, M_MYFIRST);
		hw->regs_buf = NULL;
	}
	free(hw, M_MYFIRST);
}
```

La separación es limpia. La capa de hardware sabe cómo liberar el buffer de respaldo del capítulo 16, o no hacer nada, según cómo se haya inicializado. El BAR en sí no es responsabilidad de la capa de hardware; la capa PCI lo posee. Esta separación es lo que permite al capítulo 18 reutilizar el código del capítulo 16 sin reescribir el teardown del capítulo 16.

### La primera lectura real de un registro

Con la capa de hardware conectada, la primera lectura real del driver pasa a ser posible. En el capítulo 17, la primera lectura era del registro fijo `DEVICE_ID`, que la simulación había preinicializado con `0x4D594649` ("MYFI" en ASCII, de "MY FIrst"). El dispositivo virtio-rnd no expone un registro `DEVICE_ID` en ese desplazamiento; su espacio de configuración en el offset 0 del BAR sigue un esquema específico de virtio que comienza con un registro de características del dispositivo.

Para la ruta de enseñanza del capítulo 18, no necesitamos implementar el protocolo virtio-rnd. El driver lee la primera palabra de 32 bits del BAR y registra el valor. El valor es lo que sea que contenga el primer registro del dispositivo virtio-rnd (los primeros 32 bits de la configuración del dispositivo virtio heredado, que para un dispositivo virtio-rnd en ejecución no tienen ningún significado especial para nuestro driver). El objetivo de la lectura es demostrar que el acceso al BAR funciona.

El código que lo hace (en `myfirst_pci_attach`, tras la asignación del BAR y el attach de la capa de hardware):

```c
uint32_t first_word;

MYFIRST_LOCK(sc);
first_word = CSR_READ_4(sc, 0x00);
MYFIRST_UNLOCK(sc);

device_printf(dev, "first register read: 0x%08x\n", first_word);
```

El envoltorio de lock y unlock es la disciplina del capítulo 16. La lectura pasa por `bus_space_read_4` internamente. La línea de salida aparece en `dmesg` en el momento del attach:

```text
myfirst0: first register read: 0x10010000
```

El valor exacto depende del estado actual del dispositivo virtio-rnd. Un lector que vea cualquier valor, en lugar de un page fault o una lectura basura que provoque un crash en el invitado, habrá confirmado que la asignación del BAR funcionó, que el tag y el handle son correctos y que la capa de acceso opera sobre hardware real.

### La familia completa de accesores

El driver del capítulo 16 usaba exclusivamente `bus_space_read_4` y `bus_space_write_4` porque el mapa de registros está compuesto íntegramente de registros de 32 bits. Los dispositivos PCI reales a veces necesitan lecturas de 8, 16 o 64 bits, y a veces necesitan operaciones en bloque que leen o escriben muchos registros contiguos de una sola vez. La familia `bus_space` cubre todos estos casos:

- `bus_space_read_1`, `_2`, `_4`, `_8`: lectura de un byte, de 16 bits, de 32 bits o de 64 bits.
- `bus_space_write_1`, `_2`, `_4`, `_8`: escritura de un byte, de 16 bits, de 32 bits o de 64 bits.
- `bus_space_read_multi_*`: lee múltiples valores desde el mismo desplazamiento de registro (útil para lecturas de FIFO).
- `bus_space_write_multi_*`: escribe múltiples valores en el mismo desplazamiento de registro.
- `bus_space_read_region_*`: lee un rango de registros en un buffer de memoria.
- `bus_space_write_region_*`: escribe un buffer de memoria en un rango de registros.
- `bus_space_set_multi_*`: escribe el mismo valor en el mismo registro muchas veces.
- `bus_space_set_region_*`: escribe el mismo valor en un rango de registros.
- `bus_space_barrier`: impone ordenación entre accesos.

Cada variante tiene el sufijo de anchura como una entrada separada. La familia es simétrica y predecible una vez que la has visto.

Para el driver del capítulo 18 solo se necesita `_4`. El mapa de registros es de 32 bits en su totalidad. Si un driver posterior usa un dispositivo con registros de 16 bits, el lector simplemente cambia `_4` por `_2`. Las macros `CSR_*` se pueden extender para cubrir múltiples anchuras si fuera necesario:

```c
#define CSR_READ_1(sc, off)       myfirst_reg_read_1((sc), (off))
#define CSR_READ_2(sc, off)       myfirst_reg_read_2((sc), (off))
#define CSR_WRITE_1(sc, off, val) myfirst_reg_write_1((sc), (off), (val))
#define CSR_WRITE_2(sc, off, val) myfirst_reg_write_2((sc), (off), (val))
```

con las correspondientes funciones de accesor en `myfirst_hw.c`. El capítulo 18 no las necesita, pero quien escribe drivers debe saber que existen.

### bus_space_read_multi vs bus_space_read_region

Dos de las operaciones en bloque merecen un segundo vistazo, porque sus nombres son fáciles de confundir.

`bus_space_read_multi_4(tag, handle, offset, buf, count)` lee `count` valores de 32 bits, todos desde el mismo desplazamiento del BAR, en `buf`. Esta es la operación correcta para un FIFO: el registro en un desplazamiento fijo es el puerto de lectura del FIFO, y cada lectura consume una entrada. Escribir un bucle equivalente a mano con `bus_space_read_4` funcionaría, pero la versión en bloque suele ser más rápida y expresa la intención con mayor claridad.

`bus_space_read_region_4(tag, handle, offset, buf, count)` lee `count` valores de 32 bits desde desplazamientos consecutivos a partir de `offset`, en `buf`. Esta es la operación correcta para un bloque de registros: el driver quiere capturar un rango del mapa de registros en un buffer local. Escribir un bucle con `bus_space_read_4` incrementando el desplazamiento sería equivalente; la versión en bloque expresa la intención con mayor claridad.

La diferencia está en si el desplazamiento en el BAR avanza. `_multi` mantiene el desplazamiento fijo. `_region` lo avanza. Escribir `_multi` cuando se quería decir `_region` lee el mismo registro cuatro veces, no cuatro registros distintos. Es una confusión clásica, y la manera de evitarla es leer con atención el nombre de la variante y recordar que "multi = un puerto, muchos accesos" frente a "region = un rango de puertos, un acceso cada uno".

### Cuándo importan las barreras

El capítulo 16 introdujo `bus_space_barrier(9)` como protección frente al reordenamiento del procesador y del compilador en torno a los accesos a registros. La regla es: cuando el driver tiene un requisito de ordenación entre dos accesos (una escritura que debe preceder a una lectura, por ejemplo, o una escritura que debe preceder a otra escritura), inserta una barrera.

Para el driver del capítulo 18, la capa de acceso ya envuelve una barrera alrededor de las escrituras que tienen efectos secundarios (dentro de `myfirst_reg_write_barrier`, definida en el capítulo 16). El backend de simulación del capítulo 17 no requiere barreras adicionales porque los accesos son a RAM. El backend PCI puede requerir barreras en más lugares de los que requería la simulación, porque la memoria real del dispositivo tiene una semántica de ordenación más débil que la RAM en algunas arquitecturas.

El caso habitual en x86: `bus_space_write_4` a un BAR mapeado en memoria tiene un orden fuerte respecto a otras escrituras al mismo BAR; no se necesita ninguna barrera explícita. En arm64 con atributos de memoria de dispositivo, las escrituras al mismo BAR también están ordenadas. En otras arquitecturas con modelos de memoria más débiles, pueden ser necesarias barreras explícitas. La página de manual `bus_space(9)` especifica las garantías de ordenación por defecto para cada arquitectura; los drivers que se preocupan por la portabilidad incluyen barreras incluso donde x86 no las requeriría.

El driver del capítulo 18 trabaja sobre x86 con fines didácticos y usa barreras de la misma forma que el capítulo 16: después de una escritura de CTRL que tiene efectos secundarios (inicia un comando, provoca un cambio de estado, limpia una interrupción). El helper `myfirst_reg_write_barrier` del capítulo 17 sigue siendo el punto de entrada correcto.

### El registro de accesos en un BAR real

El registro de accesos del Capítulo 16 es un ring buffer que registra cada acceso a un registro junto con una marca de tiempo, un desplazamiento, un valor y una etiqueta de contexto. En el backend de simulación, el registro muestra patrones como "escritura desde espacio de usuario en CTRL, seguida de una lectura de STATUS por parte de un callout". En el backend PCI real, el registro muestra la misma forma: cualquier acceso que el driver realice sobre el BAR pasa por los accesores, y cada accesor escribe una entrada.

Esta continuidad es una característica discreta pero importante. Un desarrollador que depura un problema en la simulación puede consultar el registro de accesos; un desarrollador que depura un problema en hardware real puede consultar el mismo registro de accesos. La técnica se transfiere. El código no cambia. La disciplina de pruebas de la Sección 7 depende de esta continuidad.

Una nota sobre el registro de accesos y los BARs reales: si el dispositivo produce a veces efectos secundarios en las lecturas (limpiar un bit de estado latched, avanzar un puntero de FIFO, desencadenar la finalización de una posted write), el registro recogerá el valor de la lectura y las acciones posteriores del driver. Leer el registro puede revelar problemas de temporización que de otro modo no serían visibles. Un error en el que el driver lee STATUS dos veces en rápida sucesión y la segunda lectura observa bits distintos porque el efecto secundario de la primera lectura interfirió quedará reflejado con claridad en el registro. Para el Capítulo 18 esto todavía no importa; para el Capítulo 19 y los capítulos posteriores sí importa, y mucho.

### Una pequeña sutileza: las macros CSR no conocen PCI

Vale la pena señalarlo. Las macros `CSR_*` no toman ni una etiqueta ni un handle. Solo toman el softc y el offset. Todo lo demás está dentro de las funciones de acceso.

Esto significa que cuando el driver pase de la simulación del capítulo 17 al BAR real del capítulo 18, no cambiará ni un único call site en el driver. `CSR_READ_4(sc, MYFIRST_REG_STATUS)` hace lo correcto antes y después de la transición. Lo mismo ocurre con `CSR_WRITE_4` y `CSR_UPDATE_4`.

El beneficio es concreto. El driver del capítulo 17 tiene probablemente treinta o cuarenta call sites que leen o escriben registros a través de las macros CSR. Si esas macros tomaran una etiqueta y un handle, el capítulo 18 tendría que actualizar cada uno de ellos. Como solo toman el softc, el capítulo 18 únicamente necesita cambiar la rutina attach de la capa de hardware. La disciplina de ocultar los detalles de bajo nivel dentro de los accesores, introducida en el capítulo 16 y mantenida en el capítulo 17, recoge aquí su mayor dividendo.

Este es un patrón que merece recordarse. Cuando escribas un driver, define un pequeño conjunto de funciones de acceso que oculten todo lo que está por encima del nivel de registro: la etiqueta, el handle, el lock, el log, la barrera. Expón al resto del driver solo el softc y el offset. El código que usa los accesores no necesita saber si los registros son simulados, PCI real, USB real, I2C real o cualquier otra cosa. La abstracción funciona para un amplio abanico de transportes. La parte 7 del libro retomará este tema cuando hable de la refactorización de drivers para portabilidad; el capítulo 18 es donde el lector ve por primera vez el dividendo cobrado.

### De la etapa 2 a la etapa 3: conectarlo todo

La attach de la etapa 2 asignaba el BAR pero no se lo pasaba a la capa de hardware. La attach de la etapa 3 hace ambas cosas. El código relevante es el attach completo:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;
	error = myfirst_init_softc(sc);	/* Ch10-15: locks, softc fields */
	if (error != 0)
		return (error);

	/* Allocate BAR0. */
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		error = ENXIO;
		goto fail_softc;
	}

	/* Hand the BAR to the hardware layer. */
	error = myfirst_hw_attach_pci(sc, sc->bar_res,
	    rman_get_size(sc->bar_res));
	if (error != 0)
		goto fail_release;

	/* Read a diagnostic word from the BAR. */
	MYFIRST_LOCK(sc);
	sc->bar_first_word = CSR_READ_4(sc, 0x00);
	MYFIRST_UNLOCK(sc);
	device_printf(dev, "BAR[0x00] = 0x%08x\n", sc->bar_first_word);

	sc->pci_attached = true;
	return (0);

fail_release:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
}
```

Y el detach correspondiente:

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	sc->pci_attached = false;
	myfirst_hw_detach(sc);
	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}
	myfirst_deinit_softc(sc);
	device_printf(dev, "detaching\n");
	return (0);
}
```

La secuencia de attach es estricta: inicializar el softc (locks, campos), asignar el BAR, hacer el attach de la capa de hardware contra el BAR, realizar las lecturas de registro que el attach necesita y marcar el driver como adjunto. El detach deshace cada paso en orden inverso: marcar como no adjunto, hacer el detach de la capa de hardware (que libera su estructura wrapper), liberar el BAR y desinicializar el softc.

La sección 5 extenderá el attach con pasos adicionales específicos de PCI: habilitar el bus mastering, recorrer la lista de capacidades y leer un campo específico de subvendor del espacio de configuración. La forma del attach se mantiene; crece la parte central.

### Errores comunes en la transición de bus_space a PCI

Una lista breve de trampas.

**Hacer un cast del puntero de recurso en lugar de usar `rman_get_bustag` / `rman_get_bushandle`.** Un principiante a veces escribe `hw->regs_tag = (bus_space_tag_t)bar`. Esto no compila en la mayoría de las arquitecturas y compila como algo sin sentido en el resto. Usa los accesores.

**Confundir el handle de recurso con la etiqueta.** La etiqueta identifica el bus (memoria o I/O); el handle es la dirección. `rman_get_bustag` devuelve la etiqueta; `rman_get_bushandle` devuelve el handle. Intercambiarlos produce cuelgues inmediatos o lecturas incorrectas silenciosas. Lee los nombres de las funciones con cuidado.

**No poner a cero el estado de hardware en el attach de PCI.** `malloc(9)` con `M_ZERO` limpia la estructura. Sin `M_ZERO`, campos como `access_log_head` empiezan con basura. El buffer circular avanza hasta un índice arbitrario y el log resulta ilegible.

**No liberar el estado de hardware en el detach.** Un error de simetría: el detach de PCI libera el BAR pero se olvida de llamar a `myfirst_hw_detach`. La estructura wrapper de hardware queda sin liberar. `vmstat -m` muestra la fuga con el tiempo.

**Leer el BAR sin tener el lock.** La disciplina del capítulo 16 es: todo acceso CSR se hace bajo `sc->mtx`. Leer en el momento del attach sin el lock viola el invariante que asume todo acceso posterior. Aunque funcione en un único CPU, `WITNESS` en un kernel de depuración se quejará. Toma el lock incluso para las lecturas en el momento del attach.

**Escribir accidentalmente en un registro de solo lectura.** En el backend de simulación, las escrituras en un registro de solo lectura simplemente actualizan el buffer asignado con `malloc` (el lado de lectura de la simulación ignora la escritura y devuelve el valor fijo). En PCI real, las escrituras en un registro de solo lectura o se ignoran en silencio o causan algún efecto secundario específico del dispositivo. Ninguno de los casos es lo que el autor del driver espera. Lee el datasheet y escribe solo en registros escribibles.

**Llamar a `bus_space_read_multi_4` cuando el driver quería `_region_4`.** Las dos funciones tienen firmas idénticas y semánticas muy diferentes. Leer un rango de registros con `_multi` rellena el buffer con el mismo valor (el valor actual del offset fijo) repetido `count` veces. Leer un rango con `_region` rellena el buffer con los valores de los registros consecutivos. El fallo es silencioso hasta que se inspeccionan los valores.

### Cerrando la sección 4

La capa de accesores del capítulo 16 no cambia con la transición de registros simulados a registros PCI reales. El único cambio está en `myfirst_hw_attach_pci`, que reemplaza el buffer de respaldo de `malloc(9)` por una etiqueta y un handle obtenidos de `rman_get_bustag(9)` y `rman_get_bushandle(9)` sobre el recurso asignado por PCI. Las macros `CSR_*`, el log de acceso, la disciplina de lock, la tarea del ticker y el resto del código base de los capítulos 16 y 17 siguen funcionando sin modificación.

La primera lectura real de un registro PCI del driver ocurre en el momento del attach. El valor leído no tiene significado en el protocolo virtio-rnd; es la prueba de que el mapeo del BAR está activo y los accesores están leyendo el silicio real. La sección 5 lleva la secuencia de attach más lejos: presenta `pci_enable_busmaster(9)` (para uso futuro de DMA), recorre la lista de capacidades PCI con `pci_find_cap(9)` y `pci_find_extcap(9)`, explica cuándo un driver lee campos del espacio de configuración directamente y muestra cómo la simulación del capítulo 17 se mantiene inactiva en la ruta PCI para que sus callouts no escriban valores arbitrarios en los registros del dispositivo real.



## Sección 5: Inicialización del driver en el momento del attach

Las secciones 2 a 4 construyeron la rutina attach desde cero hasta un attach PCI completamente conectado que reclama un BAR, se lo pasa a la capa de hardware y realiza su primer acceso a un registro. La sección 5 cierra la historia del attach. Presenta las pocas operaciones sobre el espacio de configuración que un driver PCI realiza típicamente en el attach, explica cuándo y por qué se necesita cada una, recorre la lista de capacidades PCI para descubrir las características opcionales del dispositivo, muestra cómo la simulación del capítulo 17 se mantiene inactiva en la ruta PCI (para que sus callouts no escriban en el dispositivo real) y crea el cdev que el driver del capítulo 10 ya exponía.

Al final de la sección 5, el driver está completo como driver PCI. Se adjunta al dispositivo real, lleva el dispositivo a un estado en que el driver puede usarlo, expone la misma interfaz de espacio de usuario que expusieron las iteraciones de los capítulos 10 al 17, y está listo para ser extendido en el capítulo 19 con un manejador de interrupciones real.

### La lista de comprobación del attach

La rutina attach de un driver PCI funcional realiza, aproximadamente en este orden:

1. **Inicializar el softc.** Establecer `sc->dev`, inicializar locks, condiciones y callouts, poner a cero los contadores.
2. **Asignar recursos.** Reclamar el BAR (o los BARs), reclamar el recurso IRQ (en el capítulo 19) y cualquier otro recurso de bus.
3. **Activar las características del dispositivo.** Habilitar el bus mastering si el driver usará DMA. Configurar los bits del espacio de configuración que el dispositivo necesita.
4. **Recorrer las capacidades.** Encontrar las capacidades PCI que el driver soporta y registrar sus offsets de registro.
5. **Hacer el attach de la capa de hardware.** Pasar el BAR a la capa de acceso.
6. **Inicializar el dispositivo.** Realizar la secuencia de puesta en marcha específica del dispositivo: reset, negociación de características, configuración de colas. Esto es lo que el datasheet del dispositivo indica como necesario para que el dispositivo sea utilizable.
7. **Registrar las interfaces de espacio de usuario.** Crear cdevs, interfaces de red o lo que el driver exponga.
8. **Habilitar las interrupciones.** Registrar el manejador de interrupciones (capítulo 19) y desenmascarar las interrupciones en el registro INTR_MASK del dispositivo.
9. **Marcar el driver como adjunto.** Establecer un flag que otro código pueda comprobar.

No todos los drivers realizan todos los pasos. Un driver para un dispositivo pasivo (sin DMA, sin interrupciones, solo lecturas y escrituras) omite el bus mastering y la configuración de interrupciones. Un driver para un dispositivo que no necesita interfaz de espacio de usuario omite la creación del cdev. Pero el orden es estable: primero los recursos, después las características del dispositivo, luego la capa de hardware, después la puesta en marcha del dispositivo, después el espacio de usuario y las interrupciones al final. Hacer esto fuera de orden produce condiciones de carrera en las que una interrupción llega antes de que el driver esté listo para gestionarla, o en las que un acceso de espacio de usuario alcanza un driver parcialmente inicializado.

El driver del capítulo 18 realiza los pasos 1 al 7. El paso 8 es el capítulo 19. El paso 9 es un detalle que el driver ya manejó en el capítulo 10.

### pci_enable_busmaster y el registro de comando

El registro de comando PCI reside en el offset `PCIR_COMMAND` (`0x04`) del espacio de configuración, como campo de 16 bits. Tres bits de ese registro importan para la mayoría de los drivers:

- `PCIM_CMD_MEMEN` (`0x0002`): habilita los BARs de memoria del dispositivo. Debe estar activo antes de que el driver pueda leer o escribir en cualquier BAR de memoria.
- `PCIM_CMD_PORTEN` (`0x0001`): habilita los BARs de puertos I/O del dispositivo. Debe estar activo antes de que el driver pueda leer o escribir en cualquier BAR de puerto I/O.
- `PCIM_CMD_BUSMASTEREN` (`0x0004`): permite al dispositivo iniciar DMA como bus master. Debe estar activo antes de que el dispositivo pueda leer o escribir RAM por su cuenta.

El driver de bus PCI activa `MEMEN` y `PORTEN` automáticamente cuando activa un BAR. Un driver que ha llamado con éxito a `bus_alloc_resource_any` con `RF_ACTIVE` y ha recibido un resultado no NULL no necesita activar estos bits manualmente; el driver de bus ya lo ha hecho.

`BUSMASTEREN` es diferente. El driver de bus no lo activa automáticamente, porque no todos los drivers necesitan DMA. Un driver que va a programar su dispositivo para leer o escribir RAM del sistema (una NIC, un controlador de almacenamiento, una GPU) debe activar `BUSMASTEREN` explícitamente. Un driver que solo lee y escribe en los BARs del propio dispositivo (sin DMA) no necesita activarlo.

El helper `pci_enable_busmaster(dev)` activa el bit. Su inversa, `pci_disable_busmaster(dev)`, lo desactiva. El driver del capítulo 18 no usa DMA y no llama a `pci_enable_busmaster`. Los capítulos 20 y 21 sí lo harán.

Una nota sobre la lectura directa del registro de comando. El driver siempre puede leer el registro de comando con `pci_read_config(dev, PCIR_COMMAND, 2)` e inspeccionar bits individuales. Para la mayoría de los drivers esto es innecesario; el kernel ya ha configurado los bits relevantes. Con fines de diagnóstico (un driver que quiere registrar el estado del registro de comando del dispositivo en el momento del attach), es perfectamente válido.

### Lectura de campos del espacio de configuración

La mayoría de los drivers necesitan leer al menos unos pocos campos del espacio de configuración que los accesores genéricos no cubren. Algunos ejemplos:

- Números de revisión de firmware específicos en offsets definidos por el vendedor.
- Campos de estado del enlace PCIe dentro de la estructura de capacidad PCIe.
- Datos de capacidades específicas del vendedor.
- Campos de identificación específicos del subsistema para dispositivos multifunción.

La primitiva es `pci_read_config(dev, offset, width)`. El offset es un desplazamiento en bytes dentro del espacio de configuración. El width es 1, 2 o 4 bytes. El valor de retorno es un `uint32_t` (los valores de menor anchura están alineados a la derecha).

Un ejemplo concreto. El código de clase PCI ocupa los bytes `0x09` al `0x0b` del espacio de configuración:

- Byte `0x09`: interfaz de programación (progIF).
- Byte `0x0a`: subclase.
- Byte `0x0b`: clase.

Leer los tres a la vez como un valor de 32 bits da la clase, la subclase y el progIF en los tres bytes superiores (el byte bajo es el ID de revisión). Los accesores en caché `pci_get_class`, `pci_get_subclass`, `pci_get_progif` y `pci_get_revid` extraen cada campo individualmente; el driver raramente necesita hacerlo a mano.

Para los campos específicos del vendedor, el driver debe leerlos manualmente. El patrón es:

```c
uint32_t fw_rev = pci_read_config(dev, 0x48, 4);
device_printf(dev, "firmware revision 0x%08x\n", fw_rev);
```

El desplazamiento `0x48` es un valor ilustrativo; el desplazamiento real es el que especifique la hoja de datos del dispositivo. Leer desde un desplazamiento que el dispositivo no implementa devuelve `0xffffffff` o un valor por defecto específico del dispositivo; `0xffffffff` es el valor clásico de «sin dispositivo» en PCI.

### pci_write_config y el contrato de efectos secundarios

La función complementaria es `pci_write_config(dev, offset, value, width)`. Escribe `value` en el campo del espacio de configuración en `offset`, truncado a `width` bytes.

Un punto crítico sobre las escrituras en el espacio de configuración: algunos campos son de solo lectura. Escribir en un campo de solo lectura se ignora en silencio (el caso más habitual) o provoca un error específico del dispositivo. El driver debe saber, a partir de la especificación PCI o del datasheet del dispositivo, qué campos admiten escritura antes de emitir una.

Un segundo punto crítico: algunos campos tienen efectos secundarios en la lectura o escritura. El registro de comandos, por ejemplo, tiene efectos secundarios: activar `MEMEN` habilita los BAR de memoria; desactivarlo los deshabilita. La lectura del registro de comandos no tiene efectos secundarios. El driver debe comprender la semántica de cada campo que manipula.

El helper `pci_enable_busmaster` usa `pci_write_config` internamente para activar un bit. El driver siempre puede usar `pci_read_config` y `pci_write_config` directamente para manipular un campo cuando no existe un helper específico.

### pci_find_cap: recorriendo la lista de capacidades

Los dispositivos PCI anuncian funcionalidades opcionales a través de una lista enlazada de capacidades. Cada capacidad es un pequeño bloque en el espacio de configuración, que comienza con un byte de ID de capacidad y un byte de puntero al siguiente elemento ("next pointer"). La lista comienza en el offset almacenado en el campo `PCIR_CAP_PTR` del dispositivo (offset `0x34` del espacio de configuración) y sigue los punteros `next` hasta que un `0` termina la cadena.

Las capacidades estándar que un driver puede encontrar incluyen:

- `PCIY_PMG` (`0x01`): Power Management.
- `PCIY_MSI` (`0x05`): Message Signaled Interrupts.
- `PCIY_EXPRESS` (`0x10`): PCI Express. Cualquier dispositivo PCIe la tiene.
- `PCIY_MSIX` (`0x11`): MSI-X. Un mecanismo de enrutamiento de interrupciones más completo que MSI.
- `PCIY_VENDOR` (`0x09`): capacidad específica del fabricante.

El driver recorre la lista mediante `pci_find_cap(9)`:

```c
int capreg;

if (pci_find_cap(dev, PCIY_EXPRESS, &capreg) == 0) {
	device_printf(dev, "PCIe capability at offset 0x%x\n", capreg);
}
if (pci_find_cap(dev, PCIY_MSI, &capreg) == 0) {
	device_printf(dev, "MSI capability at offset 0x%x\n", capreg);
}
if (pci_find_cap(dev, PCIY_MSIX, &capreg) == 0) {
	device_printf(dev, "MSI-X capability at offset 0x%x\n", capreg);
}
```

La función devuelve 0 en caso de éxito y almacena el offset de la capacidad en `*capreg`. Si falla (la capacidad no está presente), devuelve `ENOENT` y no modifica `*capreg`.

El offset devuelto es el offset en bytes dentro del espacio de configuración donde reside el primer registro de la capacidad. Ese registro suele ser el propio ID de capacidad; el driver puede confirmarlo leyéndolo y comparándolo con el ID esperado. Los bytes siguientes en la capacidad definen los campos específicos de la funcionalidad.

El driver del Capítulo 18 recorre la lista de capacidades en el momento del attach y registra qué capacidades están presentes. La lista da al autor del driver una idea de lo que ofrece el dispositivo. MSI y MSI-X son relevantes para el Capítulo 20; Power Management es relevante para el Capítulo 22. En el Capítulo 18, el driver simplemente registra la presencia y los offsets.

### pci_find_extcap: capacidades extendidas de PCIe

PCIe introduce una segunda lista, denominada capacidades extendidas, que reside a partir del offset `0x100` en el espacio de configuración. Aquí es donde se encuentran funcionalidades modernas como Advanced Error Reporting, Virtual Channel, Access Control Services y SR-IOV. La lista es estructuralmente similar a la lista de capacidades clásica, pero utiliza IDs de 16 bits y offsets de 4 bytes.

La función para recorrerla es `pci_find_extcap(9)`. Su firma es idéntica a la de `pci_find_cap`:

```c
int capreg;

if (pci_find_extcap(dev, PCIZ_AER, &capreg) == 0) {
	device_printf(dev, "AER capability at offset 0x%x\n", capreg);
}
```

Los IDs de capacidades extendidas se definen en `/usr/src/sys/dev/pci/pcireg.h` con nombres que comienzan por `PCIZ_` (frente a `PCIY_` para las capacidades estándar). El prefijo es un recurso mnemotécnico: `PCIY` para "PCI capabilitY" (la lista clásica), `PCIZ` para "PCI eXtended" (Z va después de Y).

El driver del Capítulo 18 no se suscribe a AER ni a ninguna otra capacidad extendida. Recorre la lista extendida en el momento del attach y registra lo que encuentra, del mismo modo que recorre la lista estándar. Esto cumple dos objetivos: ofrece al lector una visión de cómo lucen las capacidades PCIe en la práctica, y ejercita `pci_find_extcap` para que el lector haya visto ambas funciones.

### PCIe AER: una introducción

Advanced Error Reporting (AER) es una capacidad extendida de PCIe que permite al sistema detectar e informar de determinadas clases de errores a nivel PCI: errores de transacción irrecuperables, errores corregibles, TLPs mal formados, tiempos de espera de finalización y otros. La capacidad es opcional; no todos los dispositivos PCIe la implementan.

En FreeBSD, el driver del bus PCI (`pci(4)`, implementado en `/usr/src/sys/dev/pci/pci.c`) recorre la lista de capacidades extendidas de cada dispositivo durante el probe, localiza la capacidad AER cuando está presente y la utiliza para el registro de errores a nivel del sistema. Un driver no suele registrar su propio callback de AER; el bus gestiona AER de forma centralizada y registra los errores corregibles e irrecuperables en el buffer de mensajes del kernel. Un driver que desee un tratamiento personalizado lee los registros de estado de AER mediante `pci_read_config(9)` en el offset devuelto por `pci_find_extcap(dev, PCIZ_AER, &offset)` y los decodifica según los layouts de bits definidos en `/usr/src/sys/dev/pci/pcireg.h`.

En el driver del Capítulo 18, AER se menciona para completar el panorama de capacidades PCIe. El driver no se suscribe a eventos AER. El Capítulo 20 retoma el tema en su discusión sobre "PCIe AER recovery through MSI-X vectors", para explicar dónde engancharía el handler de AER propiedad del driver en la infraestructura MSI-X que construyen los capítulos de interrupciones. Una implementación completa de recuperación AER de extremo a extremo queda fuera del alcance de este libro; los lectores que quieran seguir el lado centrado en el bus pueden estudiar `pci_add_child_clear_aer` y `pcie_apei_error` en `/usr/src/sys/dev/pci/pci.c`, junto con los layouts de bits `PCIR_AER_*` y `PCIM_AER_*` de `/usr/src/sys/dev/pci/pcireg.h`.

Un apunte sobre el nombre: "AER" se pronuncia letra por letra ("ay-ee-ar") en la mayoría de las conversaciones sobre FreeBSD. El ID de la capacidad en la cabecera pcireg es `PCIZ_AER` = `0x0001`.

### Combinando la simulación con el backend PCI real

El driver de la Fase 5 del Capítulo 17 se conectaba al kernel como módulo independiente (`kldload myfirst` disparaba el attach). El driver del Capítulo 18 se conecta a un dispositivo PCI. Ambas rutas de attach necesitan configurar el mismo estado de capa superior (softc, cdev y algunos campos por instancia). La pregunta es cómo combinarlos.

El driver del Capítulo 18 resuelve esto con un único interruptor en tiempo de compilación que selecciona qué rutas de attach están activas, y haciendo que los callouts de la simulación del Capítulo 17 **no** se ejecuten cuando el driver está vinculado a un dispositivo PCI real. La lógica es sencilla:

- Si `MYFIRST_SIMULATION_ONLY` está definido en tiempo de compilación, el driver omite `DRIVER_MODULE` por completo. No hay attach PCI; el módulo se comporta exactamente como el driver del Capítulo 17, y `kldload` crea una instancia simulada a través del manejador de eventos de módulo del Capítulo 17.
- Si `MYFIRST_SIMULATION_ONLY` **no** está definido (el valor por defecto para el Capítulo 18), el driver declara `DRIVER_MODULE(myfirst, pci, ...)`. El módulo es cargable. Cuando existe un dispositivo PCI coincidente, se ejecuta `myfirst_pci_attach`. Los callouts de la simulación del Capítulo 17 no se inician en la ruta PCI; la capa de acceso apunta al BAR real y el backend de simulación permanece inactivo. Un lector que desee la simulación puede reactivarla explícitamente mediante un sysctl o compilando con `MYFIRST_SIMULATION_ONLY`.

El guard en tiempo de compilación en `myfirst_pci.c` es breve:

```c
#ifndef MYFIRST_SIMULATION_ONLY
DRIVER_MODULE(myfirst, pci, myfirst_pci_driver, NULL, NULL);
MODULE_DEPEND(myfirst, pci, 1, 1, 1);
#endif
```

Y `myfirst_pci_attach` omite deliberadamente `myfirst_sim_enable(sc)`. El callout del sensor del Capítulo 17, el callout de comandos y la maquinaria de inyección de fallos permanecen dormidos. Están presentes en el código pero nunca se planifican cuando el backend es un BAR PCI real; esto evita que las escrituras del bit `CTRL.GO` simulado lleguen a los registros del dispositivo real.

Un lector en una máquina sin un dispositivo PCI coincidente todavía tiene la opción de ejecutar directamente la simulación del Capítulo 17: compilar con `MYFIRST_SIMULATION_ONLY=1`, `kldload`, y el driver se comporta exactamente como al final del Capítulo 17. Los dos builds comparten todos los archivos; la selección ocurre en tiempo de compilación.

Una alternativa que el lector puede elegir: dividir el driver en dos módulos. `myfirst_core.ko` contiene la capa de hardware, la simulación, el cdev y los locks. `myfirst_pci.ko` contiene el attach PCI. `myfirst_core.ko` es siempre cargable y proporciona la simulación. `myfirst_pci.ko` depende de `myfirst_core.ko` y añade soporte PCI encima.

Este es el enfoque que utilizan los drivers reales de FreeBSD cuando un chipset tiene múltiples variantes de transporte. El driver `uart(4)` tiene `uart.ko` como núcleo y `uart_bus_pci.ko` como attach PCI; `virtio(4)` tiene `virtio.ko` como núcleo y `virtio_pci.ko` como transporte PCI. El capítulo posterior del libro sobre drivers con múltiples transportes vuelve a este patrón.

Para el Capítulo 18, el enfoque más sencillo (un módulo con un interruptor en tiempo de compilación) es suficiente. Un lector que quiera practicar la división puede intentarlo como desafío al final del capítulo.

### Por qué los callouts de simulación permanecen en silencio en PCI real

Vale la pena explicitar este punto. Cuando el driver del Capítulo 18 se conecta a un dispositivo virtio-rnd real, el BAR no contiene el mapa de registros del Capítulo 17. El offset `0x00` es el registro de características del dispositivo virtio legacy, no `CTRL`. El offset `0x12` es el registro `device_status` de virtio, no el `INTR_STATUS` del Capítulo 17. Permitir que el callout del sensor del Capítulo 17 escriba en `SENSOR_CONFIG` (en el offset `0x2c` del Capítulo 17) o que el callout de comandos escriba en `CTRL` en `0x00` supondría insertar bytes arbitrarios en los registros del dispositivo virtio.

En un invitado de bhyve esto no es catastrófico (el invitado es desechable), pero es una mala práctica. El comportamiento correcto es: los callouts de simulación se ejecutan solo cuando la capa de acceso está respaldada por el buffer simulado. Cuando la capa de acceso está respaldada por un BAR real, la simulación permanece desactivada. El `myfirst_pci_attach` del Capítulo 18 garantiza esto al no llamar nunca a `myfirst_sim_enable`. El cdev sigue funcionando, `CSR_READ_4` sigue leyendo el BAR real y el resto del driver opera con normalidad. Los callouts, sencillamente, no se disparan.

Se trata de una pequeña decisión de diseño con una consecuencia real: el driver es seguro para conectarse a un dispositivo PCI real sin corromper su estado. Un lector que posteriormente adapte el driver a un dispositivo diferente (cuyo mapa de registros coincida con el de la simulación del Capítulo 17) puede reactivar los callouts mediante un sysctl y observar cómo controlan el hardware real. Para el objetivo de enseñanza virtio-rnd, los callouts permanecen dormidos.

### Creación del cdev en un driver PCI

El driver del Capítulo 10 creó un cdev con `make_dev(9)` en el momento de carga del módulo. En el driver PCI del Capítulo 18, `make_dev(9)` se ejecuta en el momento del attach, una vez por dispositivo PCI. El nombre del cdev incorpora el número de unidad: `/dev/myfirst0`, `/dev/myfirst1`, y así sucesivamente.

El código es familiar:

```c
sc->cdev = make_dev(&myfirst_cdevsw, device_get_unit(dev), UID_ROOT,
    GID_WHEEL, 0600, "myfirst%d", device_get_unit(dev));
if (sc->cdev == NULL) {
	error = ENXIO;
	goto fail_hw;
}
sc->cdev->si_drv1 = sc;
```

`device_get_unit(dev)` devuelve el número de unidad asignado por newbus. `"myfirst%d"` con ese número de unidad como argumento produce el nombre del dispositivo por instancia. La asignación de `si_drv1` permite que los puntos de entrada `open`, `close`, `read`, `write` e `ioctl` del cdev recuperen el softc a partir del cdev.

La ruta de detach destruye el cdev con `destroy_dev(9)`:

```c
if (sc->cdev != NULL) {
	destroy_dev(sc->cdev);
	sc->cdev = NULL;
}
```

Este código sigue exactamente el patrón del Capítulo 10; no hay nada nuevo en él. El motivo de incluirlo aquí es que encaja de forma natural en el orden del attach PCI: softc, BAR, capa de hardware, cdev y (más adelante en el Capítulo 19) interrupciones. En el detach se invierte el orden. Listo.

### Un attach completo de la Fase 3

Combinando todos los elementos de la Sección 5, el attach de la Fase 3:

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
	if (pci_find_cap(dev, PCIY_PMG, &capreg) == 0)
		device_printf(dev, "Power Management capability at 0x%x\n",
		    capreg);
	if (pci_find_extcap(dev, PCIZ_AER, &capreg) == 0)
		device_printf(dev, "PCIe AER extended capability at 0x%x\n",
		    capreg);

	/* Step 3: attach the hardware layer against the BAR. */
	error = myfirst_hw_attach_pci(sc, sc->bar_res,
	    rman_get_size(sc->bar_res));
	if (error != 0)
		goto fail_release;

	/* Step 4: create the cdev. */
	sc->cdev = make_dev(&myfirst_cdevsw, sc->unit, UID_ROOT,
	    GID_WHEEL, 0600, "myfirst%d", sc->unit);
	if (sc->cdev == NULL) {
		error = ENXIO;
		goto fail_hw;
	}
	sc->cdev->si_drv1 = sc;

	/* Step 5: read a diagnostic word. */
	MYFIRST_LOCK(sc);
	sc->bar_first_word = CSR_READ_4(sc, 0x00);
	MYFIRST_UNLOCK(sc);
	device_printf(dev, "BAR[0x00] = 0x%08x\n", sc->bar_first_word);

	sc->pci_attached = true;
	return (0);

fail_hw:
	myfirst_hw_detach(sc);
fail_release:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
}
```

La estructura es exactamente la lista de comprobación del attach presentada al inicio de esta sección, con etiquetas (`Step 1`, `Step 2`, etc.) que hacen explícito el orden. La cascada de goto gestiona los fallos parciales de forma limpia. Cada etiqueta de fallo deshace el paso que tuvo éxito más recientemente, encadenándose hacia el anterior.

Un lector que haya visto este patrón antes (en el attach complejo del Capítulo 15 con múltiples primitivas, o en cualquier driver de FreeBSD que asigne más de un recurso) lo reconocerá de inmediato. Un lector que lo vea por primera vez puede beneficiarse de rastrear un fallo hipotético en cada paso y verificar que se produce la limpieza correcta.

### Verificación de la Fase 3

La salida esperada de `dmesg` en el attach de la Fase 3:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
myfirst0: PCIe capability at 0x98
myfirst0: MSI-X capability at 0xa0
myfirst0: hardware layer attached to BAR: 32 bytes (tag=0x... handle=0x...)
myfirst0: BAR[0x00] = 0x10010000
```

Los desplazamientos exactos de las capacidades dependen de la implementación virtio del invitado; los valores mostrados son orientativos. Un lector que vea las cuatro líneas (attach, capabilities walked, hardware attached, BAR read) habrá confirmado que la Etapa 3 está completa.

`ls /dev/myfirst*` debería mostrar `/dev/myfirst0`. Un programa de espacio de usuario que abra ese dispositivo, escriba un byte y lea un byte debería ver el camino de simulación del Capítulo 17 en acción (el protocolo comando-respuesta sigue ejecutándose internamente, aunque el BAR sea ahora real; el Capítulo 17 y el Capítulo 18 no interactúan todavía en el nivel del camino de datos, solo comparten la capa de acceso).

El método detach verifica el proceso inverso:

```text
myfirst0: detaching
```

Y `/dev/myfirst0` desaparece. El BAR se libera. El softc se libera. Sin fugas, sin advertencias, sin estado bloqueado.

### Cerrando la sección 5

La inicialización en el momento del attach es la composición de muchos pasos pequeños. Cada paso asigna o configura una cosa. Los pasos siguen un orden estricto que construye el estado del driver desde el dispositivo hacia afuera: primero los recursos, luego las características, luego el arranque específico del dispositivo, luego las interfaces del espacio de usuario y, en el capítulo 19, las interrupciones. La ruta de detach deshace cada paso en orden inverso.

Las piezas específicas de PCI que el capítulo 18 añade a este patrón son `pci_enable_busmaster` (no necesaria para nuestro driver, reservada para los capítulos 20 y 21), los recorridos de capacidades `pci_find_cap(9)` y `pci_find_extcap(9)`, las lecturas y escrituras del espacio de configuración mediante `pci_read_config(9)` y `pci_write_config(9)`, y una breve introducción a PCIe AER a la que el lector volverá en capítulos posteriores.

La sección 6 cubre en profundidad el lado del detach. El esquema general es familiar, pero los detalles (gestionar fallos parciales del attach, el orden del desmontaje, las interacciones con callouts y tasks que pueden seguir en ejecución) merecen su propia sección.



## Sección 6: Soporte para detach y limpieza de recursos

El attach levanta el driver. El detach lo baja. Las dos rutas son espejos, pero no espejos perfectamente simétricos. El detach tiene algunas preocupaciones que el attach no tiene: la posibilidad de que haya otro código en ejecución (callouts, tasks, descriptores de archivo, manejadores de interrupción), la necesidad de rechazar el detach cuando el driver tiene trabajo que el llamante no ha detenido, y el cuidado de evitar un use-after-free entre el último acceso activo y la liberación del softc. La sección 6 trata de resolver esas preocupaciones correctamente.

El objetivo de la sección 6 es una rutina de detach que sea estricta, completa y fácil de auditar. Libera cada recurso que el attach reclamó. Drena cada callout y task que pueda seguir en ejecución. Destruye el cdev antes de liberar la capa de hardware. Libera el BAR después de que la capa de hardware ya no lo necesita. Y hace todo esto de una manera que los lectores del libro pueden leer y entender paso a paso.

### La regla fundamental: orden inverso

La disciplina más importante para el detach es el orden inverso. Cada paso que tomó el attach, el detach lo deshace en la secuencia inversa. Si el attach asignó A, después B, después C, entonces el detach libera C, después B, después A.

Esta regla parece trivial. En la práctica, olvidarla o equivocarse ligeramente en el orden es una de las causas más comunes de kernel panics en drivers nuevos. El síntoma típico: un callout se dispara durante el detach, lee un campo del softc y el campo ya está liberado. O bien: el cdev todavía existe cuando se libera el BAR, y un proceso del espacio de usuario que tiene el cdev abierto provoca una lectura que desreferencia una dirección no mapeada.

El patrón de detach del capítulo 15 es el modelo correcto para el capítulo 18. El attach construyó estado hacia afuera desde el dispositivo; el detach lo desmonta hacia adentro, hacia el dispositivo. Nada de lo que el detach libera puede seguir en uso por ninguna otra parte.

### El orden del detach del capítulo 18

El orden del detach, correspondiente al attach de la etapa 3:

1. Marcar el driver como ya no conectado (`sc->pci_attached = false`).
2. Cancelar todas las rutas de acceso del espacio de usuario: destruir el cdev para que no pueda iniciarse ningún nuevo `open` o `ioctl` y no se acepten nuevas peticiones.
3. Drenar los callouts y tasks que puedan estar en ejecución (`myfirst_quiesce`).
4. Desconectar el backend de simulación si estaba conectado (libera `sc->sim`). En la ruta PCI esto no hace nada porque la simulación no estaba conectada.
5. Desconectar la capa de hardware (libera `sc->hw`; no libera el BAR).
6. Liberar el recurso BAR mediante `bus_release_resource`.
7. Desmontar el estado del softc: destruir locks, destruir variables de condición, liberar cualquier memoria asignada.
8. (Adición del capítulo 19, mencionada por completitud) Liberar el recurso IRQ.

Para el capítulo 18, el detach es:

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	/* Refuse detach if something is still using the device. */
	if (myfirst_is_busy(sc))
		return (EBUSY);

	sc->pci_attached = false;

	/* Tear down the cdev so no new user-space accesses start. */
	if (sc->cdev != NULL) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}

	/* Drain callouts and tasks. Safe whether or not the simulation
	 * was ever enabled on this instance. */
	myfirst_quiesce(sc);

	/* Release the simulation backend if it was attached. The PCI
	 * path leaves sc->sim == NULL, so this is a no-op. */
	if (sc->sim != NULL)
		myfirst_sim_detach(sc);

	/* Detach the hardware layer (frees the wrapper struct). */
	myfirst_hw_detach(sc);

	/* Release the BAR. */
	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}

	/* Tear down the softc state. */
	myfirst_deinit_softc(sc);

	device_printf(dev, "detached\n");
	return (0);
}
```

El código es más largo que el detach de la etapa 2 porque cada paso es una preocupación propia. La estructura es fácil de leer: cada línea o bloque libera una cosa, en el orden inverso del attach. Un lector que audite el detach puede comprobar cada paso frente al attach y confirmar la simetría.

### myfirst_is_busy: cuándo rechazar el detach

Un driver que tiene un cdev abierto, un comando en vuelo o cualquier otro trabajo en curso no puede desconectarse de forma segura. Devolver `EBUSY` desde el detach le indica al cargador de módulos del kernel que deje el driver en paz.

El driver de los capítulos 10 al 15 tenía una comprobación de ocupación simple: ¿hay descriptores de archivo abiertos en el cdev? El capítulo 17 la extendió para incluir comandos simulados en vuelo. El capítulo 18 reutiliza la misma comprobación:

```c
static bool
myfirst_is_busy(struct myfirst_softc *sc)
{
	bool busy;

	MYFIRST_LOCK(sc);
	busy = (sc->open_count > 0) || sc->command_in_flight;
	MYFIRST_UNLOCK(sc);
	return (busy);
}
```

La comprobación está bajo el lock porque `open_count` y `command_in_flight` pueden ser modificados por otras rutas de código (los puntos de entrada `open` y `close` del cdev, el callout de comando del capítulo 17). Sin el lock, la comprobación podría ver una vista inconsistente, y la decisión de rechazar o permitir el detach competiría con un open o close en curso. Los nombres exactos de los campos provienen del softc del capítulo 10 (`open_count`) y de las adiciones del capítulo 17 (`command_in_flight`); un lector cuyo softc use nombres diferentes sustituye aquí los nombres locales.

Devolver `EBUSY` desde el detach produce un error visible en `kldunload`:

```text
# kldunload myfirst
kldunload: can't unload file: Device busy
```

El usuario entonces cierra el descriptor de archivo abierto, cancela el comando en vuelo o hace lo que sea necesario para drenar el estado ocupado, y lo reintenta. Este es el comportamiento esperado; un driver que nunca rechaza el detach es un driver que puede ser arrancado de debajo de sus usuarios.

### Detener callouts y tasks

La simulación del capítulo 17 ejecuta un callout de sensor cada segundo; un callout de comando se dispara por cada comando; un callout de recuperación de ocupación se dispara ocasionalmente. La capa de hardware del capítulo 16 ejecuta una task de temporizador a través de un taskqueue. En el backend PCI los callouts de simulación no están habilitados (como explicó la sección 5), por lo que su `callout_drain` es una operación nula segura si nunca se ejecutaron. La task de temporizador de la capa de hardware sigue activa y debe ser drenada.

El primitivo correcto para los callouts es `callout_drain(9)`. Espera hasta que el callout no esté en ejecución e impide cualquier disparo futuro. El primitivo correcto para las tasks es `taskqueue_drain(9)`. Espera hasta que la task haya terminado de ejecutarse e impide cualquier puesta en cola posterior.

La API del capítulo 17 expone dos funciones que envuelven el ciclo de vida del callout para la simulación: `myfirst_sim_disable(sc)` detiene la programación de nuevos disparos (requiere `sc->mtx` tomado), y `myfirst_sim_detach(sc)` drena cada callout y libera el estado de simulación (no debe tener `sc->mtx` tomado). Un único helper `myfirst_quiesce` en el driver PCI los combina de forma segura:

```c
static void
myfirst_quiesce(struct myfirst_softc *sc)
{
	if (sc->sim != NULL) {
		MYFIRST_LOCK(sc);
		myfirst_sim_disable(sc);
		MYFIRST_UNLOCK(sc);
	}

	if (sc->tq != NULL && sc->hw != NULL)
		taskqueue_drain(sc->tq, &sc->hw->reg_ticker_task);
}
```

En la ruta PCI `sc->sim` es NULL (el backend de simulación no está conectado), por lo que el primer bloque se omite por completo. En un build solo de simulación donde la simulación está conectada, `myfirst_sim_disable` detiene los callouts bajo el lock, y el posterior `myfirst_sim_detach` (llamado más adelante en la secuencia de detach) los drena sin el lock.

La separación importa porque `callout_drain` debe llamarse **sin** `sc->mtx` tomado: el propio cuerpo del callout puede intentar adquirir el mutex, y tenerlo tomado causaría un deadlock. El capítulo 13 enseñó esta disciplina; el capítulo 18 la respeta enrutando el drenado a través de `myfirst_sim_detach`, que no adquiere ningún lock.

Después de que `myfirst_quiesce` regresa, nada más se está ejecutando contra el softc salvo la propia ruta de detach. Los pasos de desmontaje posteriores pueden acceder a `sc->hw` y al BAR sin temor.

### Liberar el BAR después de la capa de hardware

El orden importa. `myfirst_hw_detach` se llama antes que `bus_release_resource` porque `myfirst_hw_detach` todavía necesita que el tag y el handle sean válidos (por ejemplo, si hubiera lecturas de última oportunidad durante el desmontaje del hardware; la versión del capítulo 18 no hace tales lecturas, pero el código defensivo mantiene el orden por si una extensión posterior las añade).

Después de que `myfirst_hw_detach` regresa, `sc->hw` es NULL. El tag y el handle almacenados en la estructura `myfirst_hw` (ahora liberada) han desaparecido. Ningún código del driver puede leer o escribir el BAR a partir de este punto. El BAR puede entonces liberarse con seguridad.

Si el orden fuera el inverso (liberar el BAR primero y luego `myfirst_hw_detach`), el código de desmontaje del hardware tendría un tag y un handle obsoletos; cualquier acceso sería un use-after-free. En x86 el fallo podría ser silencioso; en arquitecturas con permisos de memoria más estrictos, el acceso produciría un page-fault.

### Fallos durante el detach

A diferencia del attach, del detach generalmente se espera que tenga éxito. La ruta de descarga del kernel llama al detach; si el detach devuelve un valor distinto de cero, la descarga se aborta, pero el propio detach no debe dejar recursos en un estado inconsistente. La convención es que el detach devuelve 0 (éxito) o `EBUSY` (rechazar la descarga porque el driver está en uso). Devolver cualquier otro error es inusual y normalmente indica un fallo del driver.

Si falla una liberación de recursos (por ejemplo, `bus_release_resource` devuelve un error), el driver debería registrar el fallo pero continuar el detach. Dejar un estado parcialmente liberado es peor que registrar el error y continuar; el kernel se quejará del recurso filtrado en el apagado, pero el driver no habrá fallado. El driver del capítulo 18 no comprueba el valor de retorno de `bus_release_resource` por esta razón; la liberación o bien tiene éxito o bien deja un estado del kernel irrecuperable, y en ninguno de los dos casos el driver puede hacer nada al respecto.

### Detach, descarga de módulo y eliminación de dispositivo

Tres eventos diferentes pueden desencadenar el detach.

**Descarga de módulo** (`kldunload myfirst`): el usuario pide eliminar el módulo. La ruta de descarga del kernel llama al detach en cada dispositivo vinculado al módulo, uno a la vez. Si cada detach devuelve 0, el módulo se descarga. Si algún detach devuelve un valor distinto de cero, el módulo permanece cargado y la descarga devuelve un error.

**Eliminación de dispositivo por el usuario** (`devctl detach myfirst0`): el usuario pide desconectar un dispositivo específico de su driver sin descargar el módulo. El detach del driver se ejecuta para ese dispositivo; el módulo permanece cargado y puede seguir conectándose a otros dispositivos.

**Eliminación de dispositivo por hardware** (hotplug, como extraer una tarjeta PCIe de una ranura con capacidad de conexión en caliente, o el hipervisor eliminando un dispositivo virtual): el bus PCI detecta el cambio y llama al detach en el dispositivo. El detach del driver se ejecuta. Si el dispositivo se vuelve a insertar más tarde, el probe y el attach del driver se ejecutan de nuevo.

Las tres rutas ejecutan la misma función `myfirst_pci_detach`. El driver no necesita distinguirlas. El código es el mismo porque las obligaciones son las mismas: liberar todo lo que el attach asignó.

### Fallo parcial de attach y la ruta de detach

Un caso sutil que merece explicación. Si el attach falla a mitad del proceso y devuelve un error, el kernel (en newbus moderno) no llama al detach en el driver parcialmente conectado. La propia cascada goto del driver gestiona la limpieza.

El código de attach de la sección 5 tiene una cascada goto que deshace exactamente los pasos que tuvieron éxito. Si la creación del cdev falla después del attach de la capa de hardware, la cascada libera la capa de hardware y el BAR antes de regresar. Si el attach de la capa de hardware falla después de la asignación del BAR, la cascada libera el BAR antes de regresar. Cada etiqueta de fallo deshace un paso.

Un error común entre los principiantes es escribir una cascada goto que se salta pasos. Por ejemplo:

```c
fail_hw:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
```

Esto omite el paso `myfirst_hw_detach`. Si el attach de la capa de hardware tuvo éxito pero la creación del cdev falló, la cascada libera el BAR sin desmontar la capa de hardware, filtrando la estructura envolvente de la capa de hardware. La cascada correcta llama a cada paso de desenrollado que el attach exitoso hubiera necesitado deshacer.

Una técnica que usan algunos drivers: organizar el attach como una secuencia de helpers `myfirst_init_*` y el detach como una secuencia correspondiente de helpers `myfirst_uninit_*`, y tener una única función `myfirst_fail` que recorre la lista de desenrollado según hasta dónde llegó el attach. Esto es más limpio para drivers muy complejos; para el driver del capítulo 18, la cascada goto es más simple y más legible.

### Un recorrido concreto por la cascada

Veamos qué ocurre si la creación del cdev falla en la etapa 3. El attach tiene:

1. Inicializado el softc (con éxito).
2. Asignado el BAR0 (con éxito).
3. Recorridas las capacidades (siempre con éxito; son solo lecturas).
4. Vinculada la capa de hardware (con éxito).
5. Intentada la creación del cdev: falla por algún error (¿disco lleno? Es poco probable; en las pruebas, el lector puede simularlo devolviendo NULL desde un `make_dev` simulado).

La cascada ejecuta `fail_hw`, que llama a `myfirst_hw_detach` (deshace el paso 4), luego `fail_release`, que libera el BAR (deshace el paso 2), y después `fail_softc`, que desinicializa el softc (deshace el paso 1). La acción de "deshacer" del paso 3 es vacía (los recorridos de capacidades no asignan nada). El attach devuelve el error.

Si el lector traza esto a mano, la limpieza está claramente completa: el softc está desinicializado, el BAR está liberado y la capa de hardware está desvinculada. Sin fugas. Sin estado parcial. La prueba es la misma que la de un attach completo seguido de un detach completo: `vmstat -m | grep myfirst` debería mostrar cero asignaciones después de que el attach fallido retorne.

### Detach vs Resume: una vista previa

Para completar el panorama: las rutas de suspend y resume, que el Capítulo 18 no implementa, se parecen a detach y attach pero preservan más estado. Suspend detiene la actividad del driver (vacía los callouts, detiene el acceso desde el espacio de usuario), registra el estado del dispositivo en el softc y deja que el sistema se apague. Resume reinicializa el dispositivo a partir del estado guardado, reinicia los callouts, reactiva el acceso desde el espacio de usuario y retorna.

Un driver que implementa únicamente attach y detach no puede suspenderse de forma limpia; el kernel rechazará suspender un sistema que tenga un driver sin soporte para suspend. El driver `myfirst` es lo suficientemente sencillo como para que el Capítulo 18 no se preocupe por esto; el Capítulo 22, dedicado a la gestión de energía, retoma el tema.

### Cierre de la sección 6

Detach es attach al revés, con una comprobación de `EBUSY` al inicio y un paso de quiesce antes de cualquier desmontaje. La regla es sencilla; la disciplina está en aplicarla de forma consistente. Cada recurso que attach asigna, detach lo libera. Cada estado que attach establece, detach lo desmonta. Cada callout que attach arranca, detach lo vacía. El orden es el inverso de attach.

En el driver del Capítulo 18, el detach en la Etapa 3 consta de seis pasos: rechazar si está ocupado, destruir el cdev, detener los callouts de simulación y hardware, desconectar la capa de hardware, liberar el BAR y desinicializar el softc. Cada paso tiene su propio cometido. Cada paso es verificable contra el attach. Cada paso puede probarse de forma aislada.

La sección 7 es la sección de pruebas. Levanta el laboratorio en bhyve o QEMU, recorre el ciclo attach-detach sobre silicio PCI real (emulado en este caso) y enseña al lector a verificar el comportamiento del driver con `pciconf`, `devinfo`, `dmesg` y un par de programas pequeños en espacio de usuario.



## Sección 7: Pruebas del comportamiento del driver PCI

Un driver existe para comunicarse con un dispositivo. El driver del Capítulo 18 está escrito y compilado, pero todavía no ha funcionado contra ningún dispositivo real. La sección 7 cierra el ciclo. Guía al lector por el proceso de levantar un sistema invitado FreeBSD en bhyve o QEMU, exponer un dispositivo PCI virtio-rnd al invitado, cargar el driver, observar el ciclo completo attach-operate-detach-unload, ejercitar el cdev desde el espacio de usuario, leer y escribir en el espacio de configuración con `pciconf -r` y `pciconf -w`, y confirmar mediante `devinfo -v` y `dmesg` que la visión del driver coincide con la del kernel.

Las pruebas son el momento en que todos los Capítulos del 2 al 17 dan por fin sus frutos. Cada hábito que el libro ha ido construyendo (escribir el código a mano, leer el código fuente de FreeBSD, ejecutar regresiones después de cada etapa, llevar un registro del laboratorio) sirve a la disciplina de pruebas del Capítulo 18. La sección es extensa porque las pruebas reales de PCI tienen muchas piezas en movimiento; cada una merece un análisis cuidadoso.

### El entorno de pruebas

El entorno de pruebas canónico para el Capítulo 18 es un sistema invitado FreeBSD 14.3 que corre bajo `bhyve(8)` en un host FreeBSD 14.3. El invitado recibe un dispositivo virtio-rnd emulado a través del passthrough `virtio-rnd` de bhyve. El invitado ejecuta el kernel de depuración del lector. El driver `myfirst` se compila y carga dentro del invitado; se conecta al dispositivo virtio-rnd y el lector lo ejercita desde dentro del invitado.

Un entorno equivalente usa `qemu-system-x86_64` en Linux o macOS como host, con un invitado FreeBSD 14.3 que ejecuta un kernel de depuración. La opción `-device virtio-rng-pci` de QEMU hace el mismo trabajo que el virtio-rnd de bhyve. Todo lo demás es idéntico.

El resto de esta sección asume bhyve salvo que se indique lo contrario. Los lectores que usen QEMU sustituyen los comandos equivalentes; los conceptos se trasladan directamente.

### Preparación del invitado bhyve

El script de laboratorio del autor tiene un aspecto similar al siguiente, editado para mayor claridad:

```sh
#!/bin/sh
set -eu

# Load bhyve's kernel modules.
kldload -n vmm nmdm if_bridge if_tap

# Prepare a network bridge.
# ifconfig bridge0 create 2>/dev/null || true
# ifconfig bridge0 addm em0 addm tap0
# ifconfig tap0 up
# ifconfig bridge0 up

# Launch the guest.
bhyve -c 2 -m 2048 -H -w \
    -s 0:0,hostbridge \
    -s 1:0,lpc \
    -s 2:0,virtio-net,tap0 \
    -s 3:0,virtio-blk,/dev/zvol/zroot/vm/freebsd143/disk0 \
    -s 4:0,virtio-rnd \
    -l com1,/dev/nmdm0A \
    -l bootrom,/usr/local/share/uefi-firmware/BHYVE_UEFI.fd \
    vm:fbsd-14.3-lab
```

La línea clave es `-s 4:0,virtio-rnd`. Conecta un dispositivo virtio-rnd en el slot PCI 4, función 0. El enumerador PCI del invitado verá un dispositivo en `pci0:0:4:0` con vendor 0x1af4 y device 0x1005, que es exactamente el par de identificadores que la tabla probe del driver del Capítulo 18 reconoce.

Los otros slots llevan el hostbridge, LPC, la red (con tap bridged) y el almacenamiento (bloque respaldado por zvol). El invitado tiene todo lo necesario para arrancar y funcionar en modo multiusuario, más un dispositivo PCI para nuestro driver.

Una forma más breve para los lectores que prefieren `vm(8)` (la utilidad FreeBSD del port `vm-bhyve`):

```sh
vm create -t freebsd-14.3 fbsd-lab
vm configure fbsd-lab  # edit vm.conf and add:
#   passthru0="0/0/0"        # if using passthrough, not needed here
#   virtio_rnd="1"            # add a virtio-rnd device
vm start fbsd-lab
```

`vm-bhyve` oculta el detalle de la línea de comandos de bhyve. Cualquiera de las dos formas produce un entorno de laboratorio equivalente.

Los lectores que usen QEMU utilizan:

```sh
qemu-system-x86_64 -cpu host -m 2048 -smp 2 \
    -drive file=freebsd-14.3-lab.img,if=virtio \
    -netdev tap,id=net0,ifname=tap0 -device virtio-net,netdev=net0 \
    -device virtio-rng-pci \
    -bios /usr/share/qemu/OVMF_CODE.fd \
    -serial stdio
```

La línea `-device virtio-rng-pci` hace el trabajo equivalente al de bhyve.

### Verificación de que el invitado detecta el dispositivo

Dentro del invitado, tras el primer arranque, el dispositivo virtio-rnd debería ser visible:

```sh
pciconf -lv
```

Busca una entrada similar a:

```text
virtio_random0@pci0:0:4:0: class=0x00ff00 rev=0x00 hdr=0x00 vendor=0x1af4 device=0x1005 subvendor=0x1af4 subdevice=0x0004
    vendor     = 'Red Hat, Inc.'
    device     = 'Virtio entropy'
    class      = old
```

La entrada te dice tres cosas. Primera: el enumerador PCI del invitado encontró el dispositivo. Segunda: el driver `virtio_random(4)` del sistema base lo ha reclamado (el nombre inicial `virtio_random0` es la pista). Tercera: el B:D:F es `0:0:4:0`, coincidiendo con la configuración bhyve `-s 4:0,virtio-rnd`.

Si la entrada falta, o bien la línea de comandos de bhyve no incluyó `virtio-rnd`, o bien el invitado arrancó sin cargar `virtio_pci.ko`. Ambos tienen solución: revisa el comando bhyve, reinicia el invitado o ejecuta `kldload virtio_pci` a mano.

### Preparación del invitado para myfirst

En un kernel `GENERIC` de FreeBSD 14.3 estándar, `virtio_random` no está compilado de forma estática; se distribuye como módulo cargable (`virtio_random.ko`). El hecho de que haya reclamado el dispositivo en el momento en que quieras cargar `myfirst` depende de la plataforma. En un sistema moderno, `devmatch(8)` puede cargar automáticamente `virtio_random.ko` poco después del arranque al detectar un dispositivo PCI coincidente. En un invitado recién arrancado donde `devmatch` aún no ha actuado, el dispositivo virtio-rnd puede seguir sin reclamar.

Comprueba primero:

```sh
kldstat | grep virtio_random
pciconf -lv | grep -B 1 virtio_random
```

Si ninguno de los dos comandos muestra `virtio_random`, el dispositivo está sin reclamar y puedes saltarte el siguiente paso.

Si `virtio_random` ha reclamado el dispositivo, descárgalo:

```sh
sudo kldunload virtio_random
```

Si el módulo es descargable (no está fijado ni en uso), la operación tiene éxito y el dispositivo virtio-rnd queda sin reclamar. `devinfo -v` ahora lo muestra bajo `pci0` sin ningún driver vinculado.

Si deseas una configuración estable que nunca cargue automáticamente `virtio_random` entre reinicios, añade lo siguiente a `/boot/loader.conf`:

```text
hint.virtio_random.0.disabled="1"
```

Esto evita la vinculación en el momento del arranque sin eliminar la imagen del módulo del sistema. Alternativamente, añadir una entrada en `/etc/devd.conf` o en `devmatch.blocklist` (o usar el sysctl `dev.virtio_random.0.%driver` en tiempo de ejecución) impide que el driver se conecte. Para la ruta de aprendizaje del Capítulo 18, un simple `kldunload` una vez por sesión de pruebas es suficiente.

Las pruebas del Capítulo 18 usan el primer enfoque durante el desarrollo (iteración rápida) y el segundo cuando el lector quiere una configuración estable para pruebas repetidas.

Vale la pena mencionar un tercer enfoque: los sysctls `dev.NAME.UNIT.%parent` y `dev.NAME.UNIT.%driver` del kernel describen las vinculaciones pero no las modifican. Para forzar una nueva vinculación del driver, usa `devctl detach` y `devctl set driver`:

```sh
sudo devctl detach virtio_random0
sudo devctl set driver -f pci0:0:4:0 myfirst
```

El flag `-f` fuerza la operación incluso si otro driver ha reclamado el dispositivo. Este es el comando exacto que debes usar en una prueba con script cuando quieras cambiar de driver sin recargar módulos.

### Carga de myfirst y observación del attach

Con `virtio_random` fuera del camino, carga `myfirst`:

```sh
sudo kldload ./myfirst.ko
```

Observa `dmesg` para ver el attach:

```sh
sudo dmesg | tail -20
```

La salida esperada (para la Etapa 3):

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> mem 0xc1000000-0xc100001f at device 4.0 on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
myfirst0: PCIe capability at 0x0
myfirst0: MSI-X capability at 0x0
myfirst0: hardware layer attached to BAR: 32 bytes (tag=... handle=...)
myfirst0: BAR[0x00] = 0x10010000
```

(Los desplazamientos de capacidad para el dispositivo virtio legacy son 0 porque la emulación de bhyve no expone una capacidad PCIe; un lector que pruebe con el virtio-rng-pci de QEMU puede ver desplazamientos distintos de cero.)

El cdev `/dev/myfirst0` existe:

```sh
ls -l /dev/myfirst*
```

y `devinfo -v` muestra el dispositivo:

```sh
devinfo -v | grep -B 1 -A 4 myfirst
```

```text
pci0
    myfirst0
        pnpinfo vendor=0x1af4 device=0x1005 ...
        resources:
            memory: 0xc1000000-0xc100001f
```

Este es el driver conectado al dispositivo, visible desde el espacio de usuario, listo para ser ejercitado.

### Ejercicio del cdev

La ruta cdev del driver `myfirst` es la interfaz de los Capítulos 10 al 17. Acepta las llamadas al sistema `open`, `close`, `read` y `write`. En el build de solo simulación del Capítulo 17, las lecturas extraían datos del ring buffer de comando-respuesta que los callouts de la simulación rellenaban. En el build PCI del Capítulo 18 la simulación no está conectada; el cdev sigue respondiendo a `open`, `read`, `write` y `close`, pero la ruta de datos no tiene callouts activos que la alimenten. Las lecturas devuelven lo que contiene el buffer circular del Capítulo 10 subyacente (normalmente vacío al inicio); las escrituras encolan datos en el mismo buffer.

Este es el comportamiento esperado para el Capítulo 18. El objetivo de las pruebas del capítulo es demostrar que el driver se conecta a un dispositivo PCI real, el BAR está activo, el cdev es accesible desde el espacio de usuario y el detach limpia correctamente. El Capítulo 19 añade la ruta de interrupciones que hará que la ruta de datos del cdev sea significativa contra un dispositivo real.

Un pequeño programa en espacio de usuario para ejercitar el cdev:

```c
/* Minimal read-write test. */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    int fd = open("/dev/myfirst0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    char buf[16];
    ssize_t n = read(fd, buf, sizeof(buf));
    printf("read returned %zd\n", n);

    close(fd);
    return 0;
}
```

Compila y ejecuta:

```sh
cc -o myfirst_test myfirst_test.c
./myfirst_test
```

La salida puede ser una lectura breve, una lectura de cero bytes o `EAGAIN` (dependiendo de si el buffer del Capítulo 10 tiene datos listos). Lo que importa es que la ruta de lectura no provoque un crash en el kernel, no produzca un error en `dmesg` y regrese al espacio de usuario con un resultado definido.

Una prueba de escritura es igualmente sencilla:

```c
char cmd[16] = "hello\n";
write(fd, cmd, 6);
```

Las escrituras introducen datos en el buffer circular del Capítulo 10. En el backend PCI, los datos permanecen en el buffer hasta que un lector los extrae; no hay ningún callout de simulación en ejecución que los procese. La prueba consiste en que el ciclo completo se ejecute sin fallos y que `dmesg` permanezca en silencio.

### Lectura y escritura del espacio de configuración desde el espacio de usuario

`pciconf(8)` tiene dos flags que permiten a los programas en espacio de usuario inspeccionar y modificar directamente el espacio de configuración PCI:

- `pciconf -r <selector> <offset>:<length>` lee bytes del espacio de configuración y los muestra en hexadecimal.
- `pciconf -w <selector> <offset> <value>` escribe un valor en un desplazamiento concreto.

El selector identifica el dispositivo. Puede ser el nombre del driver del dispositivo (`myfirst0`) o su B:D:F (`pci0:0:4:0`).

Un ejemplo de lectura:

```sh
sudo pciconf -r myfirst0 0x00:8
```

Salida:

```text
00: 1a f4 05 10 07 05 10 00
```

Los bytes son los primeros ocho del espacio de configuración, en orden: vendor ID (`1af4`, little-endian), device ID (`1005`), registro de comandos (`0507`, con `MEMEN` y `BUSMASTER` activados), registro de estado (`0010`).

Un ejemplo de escritura (peligroso, no lo hagas a la ligera):

```sh
sudo pciconf -w myfirst0 0x04 0x0503
```

Esto borra `BUSMASTER` y deja `MEMEN` activado. El efecto en el dispositivo depende del propio dispositivo; en un dispositivo en funcionamiento puede provocar fallos en las operaciones DMA. Para un dispositivo contra el que el driver no usa DMA (el caso del Capítulo 18), el cambio es esencialmente inofensivo, pero también esencialmente carente de significado.

Un lector debería usar `pciconf -w` solo en escenarios de diagnóstico deliberados, con pleno conocimiento de las consecuencias. Escribir un valor sin sentido en el campo equivocado puede provocar un deadlock en el dispositivo, el bus o el kernel.

### devinfo -v y lo que revela

`devinfo -v` es el inspector del árbol newbus. Recorre cada dispositivo del sistema e imprime cada uno con sus recursos, su padre, su unidad y sus hijos. Para un autor de drivers, es la referencia canónica para saber qué cree el kernel que pertenece al driver.

Un fragmento de salida para el dispositivo `myfirst0`:

```text
nexus0
  acpi0
    pcib0
      pci0
        myfirst0
            pnpinfo vendor=0x1af4 device=0x1005 subvendor=0x1af4 subdevice=0x0004 class=0x00ff00
            resources:
                memory: 0xc1000000-0xc100001f
        virtio_pci0
            pnpinfo vendor=0x1af4 device=0x1000 ...
            resources: ...
        ... (other pci children)
```

El árbol muestra la ruta desde la raíz (nexus) hasta la plataforma (ACPI en x86), el puente PCI (pcib), el bus PCI (pci0) y finalmente los dispositivos de ese bus. `myfirst0` es hijo de `pci0`. Su lista de recursos muestra el BAR de memoria reclamado.

Usar `devinfo -v | grep -B 1 -A 5 myfirst` para extraer solo el bloque relevante es la técnica estándar cuando el árbol es grande.

### dmesg como herramienta de diagnóstico

`dmesg` es el buffer de mensajes del kernel. Cada `device_printf`, `printf` y fallo de `KASSERT` en el kernel aparece en `dmesg`. Para un autor de drivers, es la superficie de depuración principal.

Monitorizar `dmesg` en tiempo real mientras cargas, operas y descargas el driver es la forma de detectar problemas sutiles con antelación. Una sesión típica:

```sh
# Start a dmesg tail in a second terminal.
dmesg -w
```

Luego, en el terminal principal:

```sh
sudo kldload ./myfirst.ko
```

El terminal de seguimiento muestra los mensajes de attach a medida que se producen. Ejecuta tus pruebas:

```sh
./myfirst_test
```

El terminal de seguimiento muestra los mensajes que el driver emite durante la prueba. Descarga el módulo:

```sh
sudo kldunload myfirst
```

El terminal de seguimiento muestra los mensajes de detach.

Si algún paso produce un aviso o error inesperado, lo ves en tiempo real. Sin un `dmesg` en seguimiento continuo, podrías perderte un único aviso que señale un problema latente.

### Uso de devctl para simular el hotplug

`devctl(8)` permite que un programa en espacio de usuario simule los eventos de newbus que generaría un hotplug real o la extracción de un dispositivo. Las invocaciones más habituales:

```sh
# Force a device to detach (calls the driver's detach method).
sudo devctl detach myfirst0

# Re-attach the device (calls the driver's probe and attach).
sudo devctl attach myfirst0

# Disable a device (prevent future probes from binding).
sudo devctl disable myfirst0

# Re-enable a disabled device.
sudo devctl enable myfirst0

# Rescan a bus (equivalent to a hotplug notification).
sudo devctl rescan pci0
```

Para probar la ruta de detach del Capítulo 18, `devctl detach myfirst0` es la herramienta principal. Ejercita el código de detach sin descargar el módulo. Se ejecuta el detach del driver; el cdev desaparece; se libera el BAR; el dispositivo vuelve a quedar sin reclamar.

Un `devctl attach` posterior vuelve a disparar probe y attach. Si el probe tiene éxito (los IDs de fabricante y dispositivo siguen coincidiendo) y el attach tiene éxito, el dispositivo queda vinculado de nuevo. Este es el ciclo que usa el lector para comprobar que el driver puede hacer attach, detach y reattach sin filtrar recursos.

Ejecutar este ciclo en un bucle es el patrón estándar de regresión:

```sh
for i in 1 2 3 4 5; do
    sudo devctl detach myfirst0
    sudo devctl attach myfirst0
done
sudo vmstat -m | grep myfirst
```

Si `vmstat -m` muestra el tipo malloc `myfirst` con cero asignaciones activas tras el bucle, el driver está limpio: cada attach asignó memoria, cada detach la liberó, y los totales cuadran.

### Un script sencillo de regresión

Reuniéndolo todo, un script que verifica la ruta PCI de la Etapa 3:

```sh
#!/bin/sh
#
# Chapter 18 Stage 3 regression test.
# Run inside a bhyve guest that exposes a virtio-rnd device.

set -eu

echo "=== Unloading virtio_random if present ==="
kldstat | grep -q virtio_random && kldunload virtio_random || true

echo "=== Loading myfirst ==="
kldload ./myfirst.ko
sleep 1

echo "=== Checking attach ==="
devinfo -v | grep -q 'myfirst0' || { echo FAIL: no attach; exit 1; }

echo "=== Checking BAR claim ==="
devinfo -v | grep -A 3 'myfirst0' | grep -q 'memory:' || \
    { echo FAIL: no BAR; exit 1; }

echo "=== Exercising cdev ==="
./myfirst_test
sleep 1

echo "=== Detach-attach cycle ==="
for i in 1 2 3; do
    devctl detach myfirst0
    sleep 0.5
    devctl attach pci0:0:4:0
    sleep 0.5
done

echo "=== Unloading myfirst ==="
kldunload myfirst
sleep 1

echo "=== Checking for leaks ==="
vmstat -m | grep -q myfirst && echo WARN: myfirst malloc type still present

echo "=== Success ==="
```

El script sigue un patrón repetible: preparar, cargar, comprobar el attach, comprobar los recursos, ejercitar, hacer ciclos, descargar y comprobar si hay fugas. Ejecutarlo después de cada cambio en el driver es la forma de detectar regresiones de forma temprana.

### Lectura del espacio de configuración con fines de diagnóstico

Un pequeño ejemplo práctico del uso de `pciconf -r` para verificar que la visión del driver del espacio de configuración coincide con la del espacio de usuario.

Dentro del driver, la ruta de attach lee el ID de fabricante mediante `pci_get_vendor` y el ID de dispositivo mediante `pci_get_device`. El espacio de usuario lee los mismos bytes mediante `pciconf -r myfirst0 0x00:4`.

Salida esperada:

```text
00: f4 1a 05 10
```

Los bytes son el ID de fabricante (`0x1af4`) y el ID de dispositivo (`0x1005`), en orden little-endian. Invirtiendo los bytes se obtiene `0x1af4` para el fabricante y `0x1005` para el dispositivo, lo que coincide con la tabla de probe del driver.

Esta comprobación no es algo que hagas en producción; el subsistema PCI está bien probado y los valores son fiables. Es útil como ejercicio de aprendizaje: demuestra que la visión del driver del espacio de configuración coincide con lo que ve el espacio de usuario, y refuerza la comprensión del lector de cómo `pci_get_vendor` se relaciona con los bytes subyacentes.

### Lo que la Sección 7 no prueba

La Sección 7 verifica que el driver del Capítulo 18 hace attach a un dispositivo PCI real, reclama el BAR, expone el cdev, lee el BAR a través de la capa de acceso del Capítulo 16, se desconecta de forma limpia, libera el BAR y se descarga sin fugas. No prueba:

- Gestión de interrupciones. El driver no registra una interrupción; el Capítulo 19 lo hace.
- MSI ni MSI-X. El Capítulo 20 lo hace.
- DMA. El Capítulo 21 lo hace.
- Protocolo específico del dispositivo. El protocolo de comando-respuesta de la simulación del Capítulo 17 no es el protocolo virtio-rnd, por lo que los resultados de una escritura no son significativos. El driver del Capítulo 18 no es un driver virtio-rnd.

Un lector que quiera un driver que implemente de verdad el protocolo virtio-rnd debería leer `/usr/src/sys/dev/virtio/random/virtio_random.c`. Es un driver limpio y enfocado que un lector que haya terminado el Capítulo 18 debería ser capaz de seguir.

### Cierre de la Sección 7

Probar un driver PCI significa montar un entorno en el que el driver pueda encontrarse con un dispositivo. Para el Capítulo 18, ese entorno es un invitado de bhyve o QEMU con un dispositivo virtio-rnd expuesto al invitado. Las herramientas son `pciconf -lv` (para ver el dispositivo), `kldload` y `kldunload` (para cargar y descargar el driver), `devinfo -v` (para ver el árbol de newbus y los recursos del driver), `devctl` (para simular el hotplug), `dmesg` (para observar los mensajes de diagnóstico) y `vmstat -m` (para comprobar si hay fugas). La disciplina consiste en ejecutar un script repetible después de cada cambio, inspeccionar su salida y corregir cualquier aviso o fallo antes de continuar.

El script de regresión al final de esta sección es la plantilla que cada lector debería adaptar a su propio driver y a su propio laboratorio. Ejecutarlo diez veces seguidas y ver una salida idéntica en cada ocasión es la prueba de que el driver es sólido. Ejecutarlo una vez y ver un fallo es señal de que el driver tiene un error que la disciplina del Capítulo 18 (orden de attach, orden de detach, emparejamiento de recursos) debería haber prevenido; la corrección suele ser pequeña.

La Sección 8 es la sección final del cuerpo instructivo. Refactoriza el código del Capítulo 18 hasta su forma definitiva, sube la versión a `1.1-pci`, escribe el nuevo `PCI.md` y prepara el terreno para el Capítulo 19.



## Sección 8: Refactorización y control de versiones del driver PCI

El driver PCI ya funciona. La Sección 8 es la sección de mantenimiento. Consolida el código del Capítulo 18 en una estructura limpia y mantenible, actualiza el `Makefile` y los metadatos del módulo del driver, escribe el documento `PCI.md` que vivirá junto a `LOCKING.md`, `HARDWARE.md` y `SIMULATION.md`, sube la versión a `1.1-pci` y ejecuta el pase de regresión completo tanto contra la simulación como contra el backend PCI real.

Un lector que haya llegado hasta aquí puede sentirse tentado a saltarse la Sección 8. Es aburrida en comparación con las secciones anteriores. No introduce ningún concepto nuevo de PCI. La tentación es real, y es un error. La refactorización es lo que convierte un driver que funciona en un driver mantenible. Un driver que funciona hoy pero está mal organizado será doloroso de extender en el Capítulo 19 (cuando lleguen las interrupciones), el Capítulo 20 (cuando lleguen MSI y MSI-X), los Capítulos 20 y 21 (cuando llegue DMA) y en todos los capítulos posteriores. Las pocas líneas de mantenimiento que realiza la Sección 8 rinden dividendos en el resto de la Parte 4 y más allá.

### El diseño final de archivos

Al final del Capítulo 18, el driver `myfirst` consta de estos archivos:

```text
myfirst.c       - Main driver: softc, cdev, module events, data path.
myfirst.h       - Shared declarations: softc, lock macros, prototypes.
myfirst_hw.c    - Chapter 16 hardware access layer: CSR_* accessors,
                   access log, sysctl handlers.
myfirst_hw.h    - Chapter 16 register map and accessor declarations,
                   extended in Chapter 17.
myfirst_sim.c   - Chapter 17 simulation backend: callouts, fault
                   injection, command-response.
myfirst_sim.h   - Chapter 17 simulation interface.
myfirst_pci.c   - Chapter 18 PCI attach: probe, attach, detach,
                   DRIVER_MODULE, MODULE_DEPEND.
myfirst_pci.h   - Chapter 18 PCI declarations: ID table entry struct,
                   vendor and device ID constants.
myfirst_sync.h  - Part 3 synchronisation primitives.
cbuf.c / cbuf.h - The Chapter 10 circular buffer, still in use.
Makefile        - kmod build: KMOD, SRCS, CFLAGS.
HARDWARE.md     - Chapter 16/17 documentation of the register map.
LOCKING.md      - Chapter 15 onward documentation of lock discipline.
SIMULATION.md   - Chapter 17 documentation of the simulation backend.
PCI.md          - Chapter 18 documentation of PCI support.
```

La división es la misma que anticipó el Capítulo 17. `myfirst_pci.c` y `myfirst_pci.h` son nuevos. Todos los demás archivos existían antes del Capítulo 18 y han sido extendidos (`myfirst_hw.c` incorporó `myfirst_hw_attach_pci`) o dejados sin cambios. El archivo principal del driver (`myfirst.c`) creció unas pocas líneas para añadir los campos de softc relacionados con PCI y una llamada al helper de detach específico para PCI; no creció de forma sustancial.

Una regla general que merece la pena enunciar: cada archivo debería tener una sola responsabilidad. `myfirst.c` es el punto de integración del driver; une todas las piezas. `myfirst_hw.c` se ocupa del acceso al hardware. `myfirst_sim.c` se ocupa de simular el hardware. `myfirst_pci.c` se ocupa de hacer attach al hardware PCI real. Cuando un lector abre un archivo, debería ser capaz de predecir, por el nombre del archivo, lo que contiene. Cuando el Capítulo 19 añada `myfirst_intr.c`, la predicción se cumplirá: ese archivo trata de interrupciones.

### El Makefile definitivo

```makefile
# Makefile for the Chapter 18 myfirst driver.
#
# Combines the Chapter 10-15 driver, the Chapter 16 hardware layer,
# the Chapter 17 simulation backend, and the Chapter 18 PCI attach.
# The driver is loadable as a standalone kernel module via
# kldload(8); when loaded, it attaches automatically to any PCI
# device whose vendor/device ID matches an entry in
# myfirst_pci_ids[] (see myfirst_pci.c).

KMOD=  myfirst
SRCS=  myfirst.c myfirst_hw.c myfirst_sim.c myfirst_pci.c cbuf.c

# Version string. Update this line alongside any user-visible change.
CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.1-pci\"

# Optional: build without PCI support (simulation only).
# CFLAGS+= -DMYFIRST_SIMULATION_ONLY

# Optional: build without simulation fallback (PCI only).
# CFLAGS+= -DMYFIRST_PCI_ONLY

.include <bsd.kmod.mk>
```

Cuatro SRCS, una cadena de versión, dos opciones de compilación comentadas. El build es un único comando:

```sh
make
```

La salida es `myfirst.ko`, cargable en cualquier kernel de FreeBSD 14.3 con `kldload`.

### La cadena de versión

La cadena de versión pasa de `1.0-simulated` a `1.1-pci`. La subida refleja que el driver ha adquirido una nueva capacidad (soporte PCI real) sin cambiar ningún comportamiento visible para el usuario (el cdev sigue haciendo lo que hacía). Una subida de versión menor es lo apropiado; una subida de versión mayor implicaría un cambio incompatible.

Los capítulos posteriores continuarán la numeración: `1.2-intr` después del Capítulo 19, `1.3-msi` después del Capítulo 20, `1.4-dma` después de los Capítulos 20 y 21, y así sucesivamente. Al final de la Parte 4, el driver estará en `1.4-dma` o similar, con cada versión menor reflejando una adición de capacidad significativa.

La cadena de versión es visible en dos lugares: `kldstat -v` la muestra, y el banner de `dmesg` del driver al cargarse la imprime. Un usuario o administrador de sistema que quiera saber qué versión del driver tiene en ejecución puede buscar el banner en `dmesg` con grep.

### El documento PCI.md

Un nuevo documento se une al corpus del driver. `PCI.md` es breve; su función es describir el soporte PCI que proporciona el driver, en una forma que un futuro lector pueda consultar sin leer el código fuente.

```markdown
# PCI Support in the myfirst Driver

## Supported Devices

As of version 1.1-pci, myfirst attaches to PCI devices matching
the following vendor/device ID pairs:

| Vendor | Device | Description                                    |
| ------ | ------ | ---------------------------------------------- |
| 0x1af4 | 0x1005 | Red Hat/virtio-rnd (demo target; see README)   |

This list is maintained in `myfirst_pci.c` in the static array
`myfirst_pci_ids[]`. Adding a new supported device requires:

1. Adding an entry to `myfirst_pci_ids[]` with the vendor and
   device IDs and a human-readable description.
2. Verifying that the driver's BAR layout and register map are
   compatible with the new device.
3. Testing the driver against the new device.
4. Updating this document.

## Attach Behaviour

The driver's probe routine returns `BUS_PROBE_DEFAULT` on a match
and `ENXIO` otherwise. Attach allocates BAR0 as a memory resource,
walks the PCI capability list (Power Management, MSI, MSI-X, PCIe,
PCIe AER if present), attaches the Chapter 16 hardware layer
against the BAR, and creates `/dev/myfirstN`. The Chapter 17
simulation backend is NOT attached on the PCI path; the driver's
accessors read and write the real BAR without the simulation
callouts running.

## Detach Behaviour

Detach refuses to proceed if the driver has open file descriptors
or in-flight commands (returns `EBUSY`). Otherwise it destroys the
cdev, drains any active callouts and tasks, detaches the hardware
layer, releases the BAR, and deinit the softc.

## Module Dependencies

The driver's `MODULE_DEPEND` declarations:

- `pci`, version 1: the kernel's PCI subsystem.

No other module dependencies are declared.

## Known Limitations

- The driver does not currently handle interrupts. See Chapter 19
  for the interrupt-handling extension.
- The driver does not currently support DMA. See Chapters 20 and 21
  for the DMA extension.
- The Chapter 17 simulation backend is not attached on the PCI
  path. The simulation's callouts and command protocol remain
  available in a simulation-only build (`-DMYFIRST_SIMULATION_ONLY`)
  for readers without matching PCI hardware.

## See Also

- `HARDWARE.md` for the register map.
- `SIMULATION.md` for the simulation backend.
- `LOCKING.md` for the lock discipline.
- `README.md` for how to set up the bhyve test environment.
```

Este documento vive junto al código fuente del driver. Un futuro lector (el propio autor, tres meses después, o un colaborador, o un mantenedor de ports) puede leerlo en cinco minutos y entender la historia PCI del driver sin abrir el código.

### Actualización de LOCKING.md

`LOCKING.md` ya documenta la disciplina de lock de los Capítulos 11 al 17. El Capítulo 18 añade dos elementos pequeños:

1. El orden de detach: nuevos pasos para `destroy_dev`, silenciado de callouts, `myfirst_hw_detach`, `bus_release_resource` y `myfirst_deinit_softc`, en ese orden.
2. La cascada de fallos en el attach: las etiquetas goto (`fail_hw`, `fail_release`, `fail_softc`) y lo que deshace cada una.

La actualización son unas pocas líneas en el documento existente. No se introduce ningún lock nuevo en el Capítulo 18; la jerarquía de locks del Capítulo 15 no cambia.

### Actualización de HARDWARE.md

`HARDWARE.md` ya documenta el mapa de registros de los Capítulos 16 y 17. El Capítulo 18 añade un elemento pequeño:

- El BAR al que hace attach el driver es BAR 0, solicitado con `rid = PCIR_BAR(0)`, asignado como `SYS_RES_MEMORY` con `RF_ACTIVE`. La etiqueta y el handle se extraen con `rman_get_bustag(9)` y `rman_get_bushandle(9)`.

Esa es la única adición. El mapa de registros en sí no cambia en el Capítulo 18; los mismos desplazamientos, los mismos anchos, las mismas definiciones de bits.

### El pase de regresión

Con la refactorización completada, el pase de regresión completo para el Capítulo 18 es:

1. **Compilar sin errores.** `make` produce `myfirst.ko` sin advertencias. Los CFLAGS ya incluyen `-Wall -Werror` desde el Capítulo 4 en adelante; el build falla si aparece alguna advertencia.
2. **Cargar sin errores.** `kldload ./myfirst.ko` tiene éxito y `dmesg` muestra el banner a nivel de módulo.
3. **Hacer attach a un dispositivo PCI real.** En un invitado de bhyve con un dispositivo virtio-rnd, el driver hace attach y `dmesg` muestra la secuencia de attach completa del Capítulo 18.
4. **Crear y ejercitar el cdev.** `/dev/myfirst0` existe, `open` / `read` / `write` / `close` funcionan, ningún mensaje del kernel indica errores.
5. **Recorrer las capacidades.** `dmesg` muestra los desplazamientos de capacidades de las que expone el virtio-rnd del invitado.
6. **Leer el espacio de configuración desde el espacio de usuario.** `pciconf -r myfirst0 0x00:8` produce los bytes esperados.
7. **Desconectarse limpiamente.** `devctl detach myfirst0` produce el banner de detach en `dmesg`; el cdev desaparece; `vmstat -m | grep myfirst` muestra cero asignaciones activas.
8. **Reconectarse limpiamente.** `devctl attach pci0:0:4:0` vuelve a disparar probe y attach; el ciclo completo se ejecuta de nuevo.
9. **Descargar limpiamente.** `kldunload myfirst` tiene éxito; `kldstat -v | grep myfirst` no devuelve nada.
10. **Sin fugas.** `vmstat -m | grep myfirst` no devuelve nada.

El script de regresión de la Sección 7 ejecuta los pasos del 1 al 10 en secuencia e informa del éxito o del primer fallo. Ejecutarlo después de cada cambio es la disciplina que detecta las regresiones de forma temprana.

### Lo que logró la refactorización

Al comienzo del Capítulo 18, el driver `myfirst` era una simulación. Tenía un bloque de registros respaldado por `malloc(9)`, un backend de simulación y un elaborado arnés de pruebas. No hacía attach a hardware real; era un módulo cargado a mano.

Al final del Capítulo 18, el driver es un driver PCI. Hace attach a un dispositivo PCI real cuando hay uno presente. Reclama el BAR del dispositivo a través de la API estándar de asignación de bus de FreeBSD. Usa la capa de acceso del Capítulo 16 para leer y escribir en los registros del dispositivo a través de `bus_space(9)`. La simulación del Capítulo 17 sigue disponible mediante un selector en tiempo de compilación (`-DMYFIRST_SIMULATION_ONLY`) para lectores sin el hardware PCI correspondiente, pero el build por defecto apunta a la ruta PCI y deja los callouts de simulación inactivos. Las rutas de attach y detach siguen las convenciones de newbus que usa cualquier otro driver de FreeBSD.

El código es reconociblemente FreeBSD. La estructura es la que usan los drivers reales cuando tienen responsabilidades diferenciadas de simulación, hardware y bus. El vocabulario es el que comparten los drivers reales. Un colaborador que abre el driver por primera vez encuentra una estructura familiar, lee la documentación y puede navegar por el código por subsistema.

### Una breve nota sobre la visibilidad de símbolos

Un lector que compare el driver del Capítulo 17 con el del Capítulo 18 notará que varias funciones han cambiado de visibilidad. Algunas que eran `static` en el Capítulo 17 son ahora exportadas (no estáticas) porque `myfirst_pci.c` las necesita. Entre los ejemplos están `myfirst_init_softc`, `myfirst_deinit_softc` y `myfirst_quiesce`.

La convención es la siguiente: una función que solo se llama desde su propio archivo es `static`. Una función que se llama desde varios archivos, pero únicamente dentro del mismo driver, es no estática y tiene su declaración en `myfirst.h` u otro encabezado local del proyecto. Una función que puede ser llamada desde otros módulos (algo poco habitual, y que normalmente solo ocurre a través de un KPI) se exporta explícitamente mediante una tabla de símbolos al estilo del kernel; esto no es relevante para el Capítulo 18.

La refactorización no exporta ningún símbolo nuevo fuera del driver; únicamente promueve algunas funciones de ámbito local al archivo a ámbito local al driver. Un lector al que le incomode esta promoción tiene dos opciones: dejar las funciones en `myfirst.c` y llamarlas a través de una pequeña función auxiliar que `myfirst_pci.c` invoque (una capa adicional de indirección), o bien aceptar la promoción y documentarla en los comentarios del código fuente. El libro opta por la segunda opción; el driver es lo suficientemente pequeño como para que una exportación de ámbito local al driver sea fácil de auditar.

### Cerrando la sección 8

La refactorización es, de nuevo, pequeña en código pero significativa en organización. Una división de archivo nueva, un nuevo archivo de documentación, actualizaciones a los archivos de documentación existentes, un incremento de versión y una pasada de regresión. Cada paso es sencillo; juntos transforman un driver funcional en uno mantenible.

El driver del Capítulo 18 está completo. El capítulo cierra con laboratorios, desafíos, resolución de problemas y un puente hacia el Capítulo 19, donde el driver conectado por PCI adquiere un manejador de interrupciones real. El Capítulo 20 añade MSI y MSI-X; los Capítulos 20 y 21 añaden DMA. Cada uno de esos capítulos añadirá un archivo (`myfirst_intr.c`, `myfirst_dma.c`) y extenderá los caminos de attach y detach. La estructura que el Capítulo 18 estableció se mantendrá.



## Leyendo un driver real juntos: uart_bus_pci.c

Las ocho secciones anteriores construyeron el driver del Capítulo 18 paso a paso. Antes de los laboratorios, merece la pena dedicar tiempo a un driver real de FreeBSD que sigue el mismo patrón. `/usr/src/sys/dev/uart/uart_bus_pci.c` es un ejemplo limpio. Es el attach PCI del driver `uart(4)`, que maneja puertos serie conectados por PCI: tarjetas de módem, UART integradas en el chipset, emulación serie del hipervisor y los chips de redirección de consola que usan los servidores empresariales.

Leer este archivo después de escribir el driver del Capítulo 18 es un breve ejercicio de reconocimiento de patrones. Nada en el archivo es nuevo. Cada línea corresponde a un concepto que el Capítulo 18 enseñó. El archivo tiene 366 líneas; esta sección recorre las partes importantes, señalando dónde corresponde cada pieza a un concepto del Capítulo 18.

### La parte superior del archivo

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2006 Marcel Moolenaar All rights reserved.
 * Copyright (c) 2001 M. Warner Losh <imp@FreeBSD.org>
 ...
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_cpu.h>
```

La etiqueta de licencia SPDX es BSD-2-Clause, la licencia estándar de FreeBSD. La lista de includes es casi idéntica a la de `myfirst_pci.c` del Capítulo 18. Los includes de `dev/pci/pcivar.h` y `dev/pci/pcireg.h` son la interfaz del subsistema PCI; los de `dev/uart/uart.h` y similares son las cabeceras internas del driver, para las que el driver del Capítulo 18 no tiene equivalentes.

### Tabla de métodos y estructura del driver

```c
static device_method_t uart_pci_methods[] = {
	DEVMETHOD(device_probe,		uart_pci_probe),
	DEVMETHOD(device_attach,	uart_pci_attach),
	DEVMETHOD(device_detach,	uart_pci_detach),
	DEVMETHOD(device_resume,	uart_bus_resume),
	DEVMETHOD_END
};

static driver_t uart_pci_driver = {
	uart_driver_name,
	uart_pci_methods,
	sizeof(struct uart_softc),
};
```

Cuatro entradas de métodos, no tres: `uart(4)` también implementa `device_resume` para soporte de suspensión y reanudación del sistema. La función resume es `uart_bus_resume`, que reside en el driver central de `uart(4)` y se reutiliza en todas las variantes de attach de UART. El driver del Capítulo 18 omitió `resume`; un driver de calidad de producción normalmente lo implementa.

El nombre de `driver_t` es `uart_driver_name`, definido en otra parte del driver central de UART como `"uart"`. El tamaño de softc es `sizeof(struct uart_softc)`, una estructura definida en `uart_bus.h`.

### La tabla de ID

```c
struct pci_id {
	uint16_t	vendor;
	uint16_t	device;
	uint16_t	subven;
	uint16_t	subdev;
	const char	*desc;
	int		rid;
	int		rclk;
	int		regshft;
};
```

La entrada de la tabla es más rica que la del Capítulo 18. Los campos `subven` y `subdev` permiten que la coincidencia distinga entre tarjetas de distintos fabricantes que comparten un chipset común. El campo `rid` lleva el desplazamiento en el espacio de configuración del BAR (distintas placas usan diferentes BAR). El campo `rclk` lleva la frecuencia del reloj de referencia en Hz, que varía según el fabricante. El campo `regshft` lleva un desplazamiento de registro (algunas placas sitúan sus registros UART en límites de 4 bytes, otras en límites de 8 bytes).

```c
static const struct pci_id pci_ns8250_ids[] = {
	{ 0x1028, 0x0008, 0xffff, 0, "Dell Remote Access Card III", 0x14,
	    128 * DEFAULT_RCLK },
	{ 0x1028, 0x0012, 0xffff, 0, "Dell RAC 4 Daughter Card Virtual UART",
	    0x14, 128 * DEFAULT_RCLK },
	/* ... many more entries ... */
	{ 0xffff, 0, 0xffff, 0, NULL, 0, 0 }
};
```

La tabla tiene decenas de entradas. Cada una es una placa que el driver `uart(4)` soporta. Un valor de subvendedor de `0xffff` significa «coincide con cualquier subvendedor». La última entrada es el centinela.

El driver del Capítulo 18 tiene una entrada porque apunta a un dispositivo de demostración. `uart_bus_pci.c` tiene decenas porque el ecosistema de hardware UART es amplio y los drivers deben enumerar cada variante soportada.

### La rutina de probe

```c
static int
uart_pci_probe(device_t dev)
{
	struct uart_softc *sc;
	const struct pci_id *id;
	struct pci_id cid = {
		.regshft = 0,
		.rclk = 0,
		.rid = 0x10 | PCI_NO_MSI,
		.desc = "Generic SimpleComm PCI device",
	};
	int result;

	sc = device_get_softc(dev);

	id = uart_pci_match(dev, pci_ns8250_ids);
	if (id != NULL) {
		sc->sc_class = &uart_ns8250_class;
		goto match;
	}
	if (pci_get_class(dev) == PCIC_SIMPLECOMM &&
	    pci_get_subclass(dev) == PCIS_SIMPLECOMM_UART &&
	    pci_get_progif(dev) < PCIP_SIMPLECOMM_UART_16550A) {
		id = &cid;
		sc->sc_class = &uart_ns8250_class;
		goto match;
	}
	return (ENXIO);

match:
	result = uart_bus_probe(dev, id->regshft, 0, id->rclk,
	    id->rid & PCI_RID_MASK, 0, 0);
	if (result > 0)
		return (result);
	if (sc->sc_sysdev == NULL)
		uart_pci_unique_console_match(dev);
	if (id->desc)
		device_set_desc(dev, id->desc);
	return (result);
}
```

El probe es más complejo que el del Capítulo 18. Primero busca en la tabla de ID de fabricante y dispositivo. Si eso falla, recurre a una coincidencia basada en clase: cualquier dispositivo cuya clase sea `PCIC_SIMPLECOMM` (Comunicaciones simples) y cuya subclase sea `PCIS_SIMPLECOMM_UART` (controlador UART) y cuya interfaz sea anterior a `PCIP_SIMPLECOMM_UART_16550A` (anterior a 16550A significa «un UART clásico sin características mejoradas»). Este es el probe de reserva que permite al driver manejar controladores UART genéricos incluso cuando su ID de fabricante y de dispositivo no están en la tabla.

La etiqueta `match:` se alcanza desde cualquiera de los dos caminos. Llama a `uart_bus_probe` (el helper de probe del driver central UART) con el desplazamiento de registro, el reloj de referencia y el desplazamiento de BAR de la entrada. El valor de retorno es una prioridad `BUS_PROBE_*` o un código de error positivo. El driver del Capítulo 18 devuelve `BUS_PROBE_DEFAULT` directamente; `uart(4)` delega en `uart_bus_probe` porque el driver central tiene comprobaciones adicionales.

Los accesores `pci_get_class`, `pci_get_subclass` y `pci_get_progif` devuelven los campos de código de clase que el Capítulo 18 describió. Su uso aquí es un ejemplo concreto de coincidencia basada en clase.

### La rutina de attach

```c
static int
uart_pci_attach(device_t dev)
{
	struct uart_softc *sc;
	const struct pci_id *id;
	int count;

	sc = device_get_softc(dev);

	id = uart_pci_match(dev, pci_ns8250_ids);
	if ((id == NULL || (id->rid & PCI_NO_MSI) == 0) &&
	    pci_msi_count(dev) == 1) {
		count = 1;
		if (pci_alloc_msi(dev, &count) == 0) {
			sc->sc_irid = 1;
			device_printf(dev, "Using %d MSI message\n", count);
		}
	}

	return (uart_bus_attach(dev));
}
```

El attach es breve. Vuelve a hacer coincidir el dispositivo (porque el estado de coincidencia del probe no se conserva entre las llamadas de probe/attach), comprueba si el dispositivo soporta MSI (de un solo vector), asigna un vector MSI si está disponible y luego delega en `uart_bus_attach` para el attach real.

Este es un patrón que el Capítulo 18 no utilizó. `uart(4)` aprovecha MSI cuando está disponible, recurriendo a las IRQ heredadas en caso contrario. El Capítulo 20 de este libro introducirá MSI y MSI-X; el attach de `uart(4)` es un anticipo.

La bandera `PCI_NO_MSI` en algunas entradas de la tabla marca placas donde se sabe que MSI es defectuoso o poco fiable; para esas placas el attach omite MSI y se basa en las IRQ heredadas.

### La rutina de detach

```c
static int
uart_pci_detach(device_t dev)
{
	struct uart_softc *sc;

	sc = device_get_softc(dev);

	if (sc->sc_irid != 0)
		pci_release_msi(dev);

	return (uart_bus_detach(dev));
}
```

Ocho líneas, todas significativas. Liberar MSI si fue asignado. Delegar en `uart_bus_detach` para el resto del desmontaje.

El detach del Capítulo 18 es más largo porque el driver `myfirst` no delega en un driver central; todo está en el archivo PCI. `uart(4)` agrupa el desmontaje común en `uart_bus_detach`, llamado desde el detach de cada variante de attach.

### La línea DRIVER_MODULE

```c
DRIVER_MODULE(uart, pci, uart_pci_driver, NULL, NULL);
```

Una línea. El nombre del módulo es `uart` (coincidiendo con el `driver_t`). El bus es `pci`. Los dos `NULL` son para los manejadores de inicialización y limpieza del módulo que `uart(4)` no necesita.

El driver del Capítulo 18 tiene la misma línea con `myfirst` en lugar de `uart`.

### Qué enseña este recorrido

`uart_bus_pci.c` tiene 366 líneas. Unas 60 líneas son código; el resto es la tabla de ID (más de 250 entradas, muchas en varias líneas cada una) y funciones auxiliares específicas del manejo de UART.

El código es casi indistinguible del driver del Capítulo 18 en cuanto a estructura. Una estructura `pci_id`. Una tabla de ID. Un probe que coincide con la tabla. Un attach que reclama el BAR (a través de `uart_bus_attach`). Un detach que libera todo. `DRIVER_MODULE`. `MODULE_DEPEND`. Las diferencias tienen que ver con las características específicas del UART: la coincidencia de subvendedor, el recurso de reserva basado en clase, la asignación de MSI y los campos de desplazamiento de registro y reloj de referencia.

Un lector que encuentre `uart_bus_pci.c` legible después del Capítulo 18 ha comprendido el punto del capítulo. El driver del Capítulo 18 es un driver PCI real de FreeBSD, no un juguete. Le faltan algunas características (MSI, resume, DMA) que capítulos posteriores añadirán, pero su estructura es la misma que la de cualquier driver real del árbol.

Vale la pena leer, como comparación, `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c` después de `uart_bus_pci.c`, que es el attach PCI virtio moderno (no heredado). Es más rico que `uart_bus_pci.c` porque maneja el transporte en capas de virtio, pero la estructura es la misma.



## Una mirada más profunda a la lista de capacidades PCI

La Sección 5 presentó `pci_find_cap(9)` y `pci_find_extcap(9)` como herramientas para descubrir las características opcionales de un dispositivo. Esta subsección profundiza un nivel más, mostrando cómo se estructura la lista de capacidades en el espacio de configuración y cómo un driver podría recorrer la lista completa en lugar de buscar una capacidad específica.

### Estructura de la lista de capacidades heredada

La lista de capacidades heredada reside en los primeros 256 bytes del espacio de configuración. Comienza en un desplazamiento almacenado en el byte `PCIR_CAP_PTR` del dispositivo (en el desplazamiento `0x34`). El byte en ese desplazamiento es el ID de capacidad; el byte inmediatamente siguiente es el desplazamiento de la siguiente capacidad (o cero si es la última); los bytes restantes de la capacidad son específicos de la característica.

La cabecera mínima de capacidad tiene dos bytes:

```text
offset 0: capability ID (one byte, values like PCIY_MSI = 0x05)
offset 1: next pointer (one byte, offset of next capability, 0 means end)
```

Un driver que recorre la lista lee el puntero de capacidad de `PCIR_CAP_PTR`, luego sigue la cadena leyendo el byte `next` de cada capacidad hasta llegar a cero.

Un recorrido concreto, en código:

```c
static void
myfirst_dump_caps(device_t dev)
{
	uint8_t ptr, id;
	int safety = 64;  /* protects against malformed lists */

	ptr = pci_read_config(dev, PCIR_CAP_PTR, 1);
	while (ptr != 0 && safety-- > 0) {
		id = pci_read_config(dev, ptr, 1);
		device_printf(dev,
		    "legacy capability ID 0x%02x at offset 0x%02x\n", id, ptr);
		ptr = pci_read_config(dev, ptr + 1, 1);
	}
}
```

El contador `safety` protege contra un espacio de configuración malformado donde el puntero `next` forma un ciclo. Un dispositivo bien comportado nunca produce esto, pero el código defensivo trata el espacio de configuración como potencialmente adversario.

El recorrido imprime el ID y el desplazamiento de cada capacidad. El driver puede entonces comparar los ID con las constantes `PCIY_*` y manejar las que soporta.

### Estructura de la lista de capacidades extendida

La lista de capacidades extendida de PCIe comienza en el desplazamiento `PCIR_EXTCAP` (`0x100`) y usa cabeceras de 4 bytes. La disposición, tal como se codifica en `/usr/src/sys/dev/pci/pcireg.h`, es:

```text
bits 15:0   capability ID    (PCIM_EXTCAP_ID,       mask 0x0000ffff)
bits 19:16  capability version (PCIM_EXTCAP_VER,     mask 0x000f0000)
bits 31:20  next pointer     (PCIM_EXTCAP_NEXTPTR,  mask 0xfff00000)
```

FreeBSD expone tres macros auxiliares sobre las máscaras brutas:

- `PCI_EXTCAP_ID(header)` devuelve el ID de capacidad.
- `PCI_EXTCAP_VER(header)` devuelve la versión.
- `PCI_EXTCAP_NEXTPTR(header)` devuelve el puntero siguiente (ya desplazado a su rango natural).

El puntero siguiente de 12 bits está siempre alineado a 4 bytes; un puntero siguiente de cero termina la lista.

Un recorrido usando los helpers:

```c
static void
myfirst_dump_extcaps(device_t dev)
{
	uint32_t header;
	int off = PCIR_EXTCAP;
	int safety = 64;

	while (off != 0 && safety-- > 0) {
		header = pci_read_config(dev, off, 4);
		if (header == 0 || header == 0xffffffff)
			break;
		device_printf(dev,
		    "extended capability ID 0x%04x ver %u at offset 0x%03x\n",
		    PCI_EXTCAP_ID(header), PCI_EXTCAP_VER(header), off);
		off = PCI_EXTCAP_NEXTPTR(header);
	}
}
```

El recorrido lee la cabecera de 4 bytes y la desempaqueta con los helpers. Una cabecera de cero o de todos unos significa que no hay capacidades extendidas (lo segundo es lo que devuelve un dispositivo no PCIe para cualquier lectura de capacidad extendida).

### Por qué importa el recorrido

Un driver raramente necesita el recorrido completo. `pci_find_cap` y `pci_find_extcap` son la interfaz habitual: el driver solicita una capacidad específica y obtiene el desplazamiento o recibe `ENOENT`. Un driver que quiera volcar la lista completa de capacidades con fines de diagnóstico utiliza los recorridos mostrados anteriormente.

El valor de entender la estructura está en la lectura de las hojas de datos. Una hoja de datos que dice «el dispositivo implementa la capacidad MSI a partir del desplazamiento 0xa0» está diciendo: el byte en el desplazamiento `0xa0` del espacio de configuración es el ID de capacidad (será igual a `0x05` para MSI), el byte en `0xa1` es el puntero siguiente y los bytes desde `0xa2` en adelante son la estructura de capacidad MSI. `pci_find_cap(dev, PCIY_MSI, &capreg)` devuelve `capreg = 0xa0` porque ahí es donde reside la capacidad.

Un driver que accede a la estructura de capacidad lee desde `capreg + offset`, donde `offset` está definido en la propia estructura de la capacidad MSI. Los campos específicos tienen desplazamientos concretos; la cabecera pcireg.h define los desplazamientos como `PCIR_MSI_*`.

### Recorriendo los campos de una capacidad específica

Un ejemplo. La capacidad MSI tiene varios campos que interesan al driver, en desplazamientos específicos relativos a la cabecera de capacidad:

```text
PCIR_MSI_CTRL (0x02): message control (16 bits, enables, vector count)
PCIR_MSI_ADDR (0x04): message address low (32 bits)
PCIR_MSI_ADDR_HIGH (0x08): message address high (32 bits, 64-bit only)
PCIR_MSI_DATA (0x08 or 0x0c): message data (16 bits)
```

Un driver que tiene `capreg` de `pci_find_cap(dev, PCIY_MSI, &capreg)` lee el registro de control de mensajes con:

```c
uint16_t msi_ctrl = pci_read_config(dev, capreg + PCIR_MSI_CTRL, 2);
```

La macro `PCIR_MSI_CTRL` es `0x02`; el desplazamiento completo es `capreg + 0x02`. Patrones similares se aplican a cada capacidad.

Para el Capítulo 18, este nivel de detalle no es necesario porque el driver no usa MSI. El Capítulo 20 sí lo usa, y emplea funciones auxiliares (`pci_alloc_msi`, `pci_alloc_msix`, `pci_enable_msi`, `pci_enable_msix`) que ocultan el acceso directo a los campos. El recorrido mostrado aquí es útil principalmente para diagnósticos y para leer hojas de datos.



## Una mirada más profunda al espacio de configuración

La Sección 1 y la Sección 5 presentaron el espacio de configuración; esta subsección añade algunos detalles prácticos que un autor de drivers debería conocer.

### Disposición del espacio de configuración

Los primeros 64 bytes de cada espacio de configuración PCI están estandarizados. La disposición es:

| Offset | Anchura | Campo |
|--------|---------|-------|
| 0x00 | 2 | ID de proveedor |
| 0x02 | 2 | ID de dispositivo |
| 0x04 | 2 | Registro de comando |
| 0x06 | 2 | Registro de estado |
| 0x08 | 1 | ID de revisión |
| 0x09 | 3 | Código de clase (progIF, subclase, clase) |
| 0x0c | 1 | Tamaño de línea de caché |
| 0x0d | 1 | Temporizador de latencia |
| 0x0e | 1 | Tipo de cabecera |
| 0x0f | 1 | BIST (autocomprobación integrada) |
| 0x10 | 4 | BAR 0 |
| 0x14 | 4 | BAR 1 |
| 0x18 | 4 | BAR 2 |
| 0x1c | 4 | BAR 3 |
| 0x20 | 4 | BAR 4 |
| 0x24 | 4 | BAR 5 |
| 0x28 | 4 | Puntero CIS de CardBus |
| 0x2c | 2 | ID de proveedor del subsistema |
| 0x2e | 2 | ID de dispositivo del subsistema |
| 0x30 | 4 | Dirección base de la ROM de expansión |
| 0x34 | 1 | Puntero a la lista de capacidades |
| 0x35 | 7 | Reservado |
| 0x3c | 1 | Línea de interrupción |
| 0x3d | 1 | Pin de interrupción |
| 0x3e | 1 | Concesión mínima |
| 0x3f | 1 | Latencia máxima |

Los bytes del 0x40 al 0xff están reservados para uso específico del dispositivo y para la lista de capacidades heredada (que comienza en el desplazamiento almacenado en `PCIR_CAP_PTR`).

PCIe extiende el espacio de configuración hasta 4096 bytes. Los bytes del 0x100 al 0xfff contienen la lista de capacidades extendida, que comienza en el offset `0x100` y sigue su propia cadena de capacidades alineadas a 4 bytes.

### Tipo de cabecera

El byte en `PCIR_HDRTYPE` (`0x0e`) distingue entre tres tipos de cabeceras de configuración PCI:

- `0x00`: dispositivo estándar (el que asume el capítulo 18).
- `0x01`: puente PCI a PCI (un puente que conecta un bus secundario al bus primario).
- `0x02`: puente CardBus (un puente de tarjeta PC; cada vez más obsoleto).

La distribución a partir del offset `0x10` varía según el tipo de cabecera. Un driver para un dispositivo estándar usa los offsets `0x10` a `0x24` como BARs; un driver para un puente usa esos mismos offsets para el número de bus secundario, el número de bus subordinado y los registros específicos del puente.

El bit más significativo de `PCIR_HDRTYPE` indica un dispositivo multifunción: si está activo, el dispositivo tiene funciones adicionales más allá de la función 0. El enumerador PCI del kernel utiliza este bit para decidir si hacer probe de las funciones 1 a la 7.

### Comandos y estado

El registro de comandos (`PCIR_COMMAND`, offset `0x04`) contiene bits de habilitación que controlan el comportamiento del dispositivo a nivel PCI:

- `PCIM_CMD_PORTEN` (0x0001): habilita los BARs de I/O.
- `PCIM_CMD_MEMEN` (0x0002): habilita los BARs de memoria.
- `PCIM_CMD_BUSMASTEREN` (0x0004): permite al dispositivo iniciar transferencias DMA.
- `PCIM_CMD_SERRESPEN` (0x0100): notifica errores del sistema.
- `PCIM_CMD_INTxDIS` (0x0400): deshabilita la señalización legacy INTx (se usa cuando el driver emplea MSI o MSI-X en su lugar).

El kernel establece `MEMEN` y `PORTEN` automáticamente durante la activación de recursos. El driver establece `BUSMASTEREN` mediante `pci_enable_busmaster` si utiliza DMA. El driver establece `INTxDIS` cuando ha asignado correctamente vectores MSI o MSI-X y quiere evitar que el dispositivo también genere interrupciones legacy.

El registro de estado (`PCIR_STATUS`, offset `0x06`) contiene bits persistentes que el driver lee para conocer los eventos a nivel PCI: el dispositivo ha recibido un master abort, un target abort, un error de paridad o un error de sistema señalizado. Un driver que se preocupa por la recuperación ante errores PCI lee el registro de estado periódicamente o en su manejador de errores; un driver que no lo hace (la mayoría, al nivel del capítulo 18) lo ignora.

### Leer con un ancho mayor al disponible

`pci_read_config(dev, offset, width)` acepta un ancho de 1, 2 o 4. Nunca acepta un ancho de 8, aunque existen campos de 64 bits (BARs de 64 bits) en el espacio de configuración. Un driver que lee un BAR de 64 bits lo hace con dos lecturas de 32 bits:

```c
uint32_t bar_lo = pci_read_config(dev, PCIR_BAR(0), 4);
uint32_t bar_hi = pci_read_config(dev, PCIR_BAR(1), 4);
uint64_t bar_64 = ((uint64_t)bar_hi << 32) | bar_lo;
```

Ten en cuenta que esto lee el BAR del *espacio de configuración*, que el driver rara vez necesita una vez que el kernel ha asignado el recurso. La asignación del kernel devuelve la misma información que un `struct resource *` cuya dirección de inicio está disponible a través de `rman_get_start`.

### Alineación en las lecturas del espacio de configuración

Los accesos al espacio de configuración están alineados por diseño. Una lectura de ancho 1 puede comenzar en cualquier offset; una lectura de ancho 2 debe comenzar en un offset par; una lectura de ancho 4 debe comenzar en un offset divisible por 4. Los accesos no alineados (por ejemplo, una lectura de ancho 4 en el offset `0x03`) no están soportados por la transacción de configuración del bus PCI y devolverán valores indefinidos o un error en algunas implementaciones. Todos los campos estándar en los primeros 64 bytes del espacio de configuración están dispuestos de modo que su ancho natural está alineado de forma natural, por lo que un driver que lee cada campo en su offset y ancho documentados nunca tendrá problemas de alineación.

Un driver que lee un campo específico del fabricante cuya distribución no está clara debe leerlo con el ancho que especifica el datasheet. No asumas que una lectura de 32 bits de un campo de 16 bits devuelve valores bien definidos en los bits superiores. La especificación PCI exige que las pistas de bytes no utilizadas devuelvan ceros, pero un driver prudente lee solo el ancho que necesita.

### Escritura en el espacio de configuración: advertencias

Hay tres advertencias sobre las escrituras en el espacio de configuración.

Primera: algunos campos son persistentes (sticky). Una vez establecidos, no se borran. El bit `INTxDIS` del registro de comandos es un ejemplo. Escribir cero en el bit no reactiva las interrupciones legacy en todos los casos; el dispositivo puede retener el estado deshabilitado. Un driver que necesite alternar dicho bit debe escribir el registro completo (lectura-modificación-escritura) y puede que tenga que aceptar que el dispositivo ignore la escritura de borrado.

Segunda: algunos campos son RW1C ("read-write-one-to-clear"). Escribir un 1 en el bit lo borra; escribir 0 no tiene efecto. Los bits de error del registro de estado son todos RW1C. Un driver que quiera borrar un bit de error persistente escribe un 1 en la posición de ese bit.

Tercera: algunas escrituras tienen requisitos de temporización. El registro de control de la capacidad de gestión de energía, por ejemplo, requiere 10 milisegundos de tiempo de asentamiento tras una transición de estado. Un driver que escribe en dicho campo debe respetar los tiempos, normalmente con una llamada a `DELAY(9)` o `pause_sbt(9)`.

En el driver del capítulo 18, solo las lecturas de identificadores del probe y las lecturas del recorrido de capacidades acceden al espacio de configuración. No se realiza ninguna escritura. A partir del capítulo 19 se añadirán escrituras (habilitación de interrupciones, borrado de bits de estado); cada escritura tendrá sus advertencias correspondientes en el momento en que se introduzca.



## Una mirada más profunda a la abstracción bus_space

La sección 4 usó la capa de acceso del capítulo 16 sin modificaciones contra un BAR real. Esta subsección describe, con más detalle, qué hace la capa `bus_space` internamente y por qué es importante.

### Qué es bus_space_tag_t

En x86, `bus_space_tag_t` es un entero que selecciona entre dos espacios de direcciones: memoria (`X86_BUS_SPACE_MEM`) y puerto de I/O (`X86_BUS_SPACE_IO`). La etiqueta indica al acceso qué instrucciones CPU debe emitir: los accesos a memoria utilizan instrucciones normales de carga y almacenamiento; los accesos a puertos de I/O utilizan `in` y `out`.

En arm64, `bus_space_tag_t` es un puntero a una estructura de punteros a funciones (un `struct bus_space`). La etiqueta codifica no solo memoria frente a I/O, sino también propiedades como el orden de bytes (endianness) y la granularidad de acceso.

En todas las plataformas, la etiqueta es opaca para el driver. El driver la almacena, la pasa a `bus_space_read_*` y `bus_space_write_*`, y nunca inspecciona su contenido. La inclusión de `machine/bus.h` incorpora la definición específica de la plataforma.

### Qué es bus_space_handle_t

En x86 para el espacio de memoria, `bus_space_handle_t` es una dirección virtual del kernel. El acceso lo desreferencia como un puntero `volatile` del ancho apropiado.

En x86 para el espacio de puertos de I/O, `bus_space_handle_t` es un número de puerto de I/O (0 a 65535). El acceso usa la instrucción `in` o `out` con el número de puerto.

En arm64, `bus_space_handle_t` es una dirección virtual del kernel, similar al espacio de memoria en x86. El MMU de la plataforma está configurado para mapear el BAR físico al rango virtual con atributos de memoria de dispositivo.

El handle también es opaco para el driver. Junto con la etiqueta, identifica de forma única un rango de direcciones donde reside un recurso específico.

### Qué ocurre dentro de bus_space_read_4

En x86 para el espacio de memoria, `bus_space_read_4(tag, handle, offset)` se expande aproximadamente en:

```c
static inline uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset)
{
	return (*(volatile uint32_t *)(handle + offset));
}
```

Una desreferencia de puntero `volatile`. La palabra clave `volatile` impide que el compilador almacene el valor en caché o reordene el acceso por delante de otros accesos `volatile`.

En x86 para el espacio de puertos de I/O, la implementación usa la instrucción `inl`:

```c
static inline uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset)
{
	uint32_t value;
	__asm volatile ("inl %w1, %0" : "=a"(value) : "Nd"(handle + offset));
	return (value);
}
```

La etiqueta selecciona entre las dos implementaciones. En arm64 y otras plataformas, la etiqueta es más compleja y la implementación se despacha a través de una tabla de punteros a funciones.

### Por qué importa la abstracción

Un driver que usa `bus_space_read_4` y `bus_space_write_4` compila en las instrucciones CPU correctas en todas las plataformas soportadas. El autor del driver no necesita saber si el BAR es de memoria o de I/O; no necesita escribir código específico de la plataforma; no necesita anotar los punteros con los atributos de acceso correctos. La capa `bus_space` se encarga de todo esto.

Un driver que saltea `bus_space` y desreferencia un puntero raw puede funcionar en x86 por casualidad (porque la capa pmap del kernel configura el mapeo de tal manera que los accesos por puntero funcionan). En arm64 fallará: la memoria del dispositivo está mapeada con atributos que impiden que los patrones normales de acceso a memoria funcionen correctamente.

La lección es: usa siempre `bus_space` o los accesos del capítulo 16 que lo envuelven. Nunca desreferencíes un puntero raw a la memoria del dispositivo, aunque conozcas la dirección virtual.

### La nomenclatura bus_read frente a bus_space_read

FreeBSD cuenta con dos familias de funciones de acceso que hacen esencialmente lo mismo. La familia más antigua, `bus_space_read_*`, toma una etiqueta, un handle y un offset. La familia más nueva, `bus_read_*`, toma un `struct resource *` y un offset, y extrae la etiqueta y el handle del recurso internamente.

La familia más nueva es más cómoda; el driver almacena solo el recurso y no necesita guardar la etiqueta y el handle por separado. La familia más antigua es más flexible; el driver puede construir una etiqueta y un handle desde cero (lo que usó la simulación del capítulo 16).

El driver del capítulo 18 usa la familia más antigua porque hereda del capítulo 16. Una reescritura podría usar la familia más nueva sin ningún cambio semántico. Ambas familias producen el mismo resultado. La elección del libro es enseñar la historia de la etiqueta y el handle porque hace explícita la abstracción; la familia más nueva oculta la abstracción, lo que es más cómodo para escribir drivers pero menos didáctico.

Como referencia, los miembros de la familia más nueva tienen nombres como `bus_read_4(res, offset)` y `bus_write_4(res, offset, value)`. Están definidos en `/usr/src/sys/sys/bus.h` como funciones inline que extraen la etiqueta y el handle y delegan en `bus_space_read_*` y `bus_space_write_*`.



## Laboratorios prácticos

Los laboratorios de este capítulo están estructurados como puntos de control progresivos. Cada laboratorio se construye sobre el anterior. Un lector que complete los cinco laboratorios dispondrá de un driver PCI completo, un entorno de pruebas en bhyve, un script de regresión y una pequeña biblioteca de herramientas de diagnóstico. Los laboratorios 1 y 2 pueden realizarse en cualquier máquina FreeBSD sin necesidad de una máquina invitada; los laboratorios 3, 4 y 5 requieren una máquina invitada en bhyve o QEMU con un dispositivo virtio-rnd.

El tiempo estimado para cada laboratorio supone que el lector ya ha leído las secciones correspondientes y ha comprendido los conceptos. Un lector que todavía esté aprendiendo debe reservar más tiempo.

### Laboratorio 1: explora tu topología PCI

Tiempo: de treinta minutos a una hora, según el tiempo que quieras dedicar a la comprensión.

Objetivo: desarrollar una intuición sobre PCI en tu propio sistema.

Pasos:

1. Ejecuta `pciconf -lv` en tu máquina de laboratorio y redirige la salida a un archivo: `pciconf -lv > ~/pci-inventory.txt`.
2. Cuenta los dispositivos: `wc -l ~/pci-inventory.txt`. Divide entre una estimación (normalmente 5 líneas por dispositivo en la salida) para obtener el número aproximado de dispositivos.
3. Identifica las siguientes clases de dispositivos del inventario:
   - Puentes host (class = bridge, subclass = HOST-PCI)
   - Puentes PCI-PCI (class = bridge, subclass = PCI-PCI)
   - Controladores de red (class = network)
   - Controladores de almacenamiento (class = mass storage)
   - Controladores host USB (class = serial bus, subclass = USB)
   - Gráficos (class = display)
4. Para cada uno de los anteriores, anota:
   - La cadena `name@pciN:B:D:F` del dispositivo.
   - Los identificadores de fabricante y dispositivo.
   - El enlace al driver (fíjate en el nombre inicial, antes de `@`).
5. Elige un dispositivo PCI (funciona mejor cualquier dispositivo no reclamado, visible como `none@...`). Anota su B:D:F.
6. Ejecuta `devinfo -v | grep -B 1 -A 5 <B:D:F>` y anota los recursos.
7. Compara la lista de recursos con la información de BAR en la entrada de `pciconf -lv`.

Observaciones esperadas:

- La mayoría de los dispositivos en un sistema moderno están en `pci0` (el bus primario) o en un bus detrás de un puente PCIe. Tu máquina probablemente tenga entre tres y diez buses visibles.
- Todos los dispositivos tienen al menos un identificador de fabricante y un identificador de dispositivo. Muchos tienen también identificadores de subfabricante y de subsistema.
- La mayoría de los dispositivos están enlazados a un driver. Algunos (especialmente en portátiles, donde un fabricante incluye hardware que FreeBSD aún no soporta) no están reclamados.
- Las listas de recursos en `devinfo -v` coinciden con la información de BAR que puedes ver en `pciconf -lv`. Las direcciones son las que asignó el firmware.

Este laboratorio trata de construir vocabulario. Sin código. Sin driver. Solo lectura.

### Laboratorio 2: escribe el esqueleto del probe en papel

Tiempo: una o dos horas.

Objetivo: interiorizar la secuencia probe-attach-detach escribiéndola a mano.

Pasos:

1. Abre un archivo en blanco, `myfirst_pci_sketch.c`, en tu editor.
2. Sin consultar el código terminado de la sección 2, escribe:
   - Una estructura `myfirst_pci_id`.
   - Una tabla `myfirst_pci_ids[]` con una entrada para un vendor hipotético `0x1234` y un dispositivo `0x5678`.
   - Una función `myfirst_pci_probe` que compare contra la tabla.
   - Una función `myfirst_pci_attach` que imprima `device_printf(dev, "attached\n")`.
   - Una función `myfirst_pci_detach` que imprima `device_printf(dev, "detached\n")`.
   - Una tabla `device_method_t` con probe, attach, detach.
   - Una `driver_t` con el nombre del driver.
   - Una línea `DRIVER_MODULE`.
   - Una línea `MODULE_DEPEND`.
   - Una línea `MODULE_VERSION`.
3. Compara tu boceto con el código de la sección 2. Anota cada diferencia.
4. Por cada diferencia, pregúntate: ¿el mío está mal, o funciona de forma distinta por alguna razón?
5. Actualiza tu boceto para que coincida con el código de la sección 2 donde el tuyo era incorrecto.

Resultados esperados:

- Probablemente olvidarás `MODULE_DEPEND` y `MODULE_VERSION` en el primer intento.
- Es posible que uses `0` en lugar de `BUS_PROBE_DEFAULT` en probe (un error habitual de principiante).
- Puede que olvides la llamada a `device_set_desc` en probe.
- Puede que uses `printf` en lugar de `device_printf` en attach y detach.
- Puede que olvides `DEVMETHOD_END` al final de la tabla de métodos.

Cada uno de estos es un error real que produce un bug real. Encontrarlos en tu propio boceto, en lugar de en un driver compilado a las dos de la madrugada, es precisamente el objetivo del laboratorio.

### Laboratorio 3: Cargar el driver de etapa 1 en un guest de bhyve

Tiempo: dos a tres horas, incluyendo la configuración del guest si todavía no tienes uno.

Objetivo: Observar la secuencia probe-attach-detach en acción.

Pasos:

1. Si todavía no tienes un guest de bhyve con FreeBSD 14.3, configura uno. La receta canónica está en `/usr/share/examples/bhyve/` o en el FreeBSD Handbook. Incluye un dispositivo `virtio-rnd` en la línea de comandos de bhyve del guest: `-s 4:0,virtio-rnd`.
2. Dentro del guest, lista los dispositivos PCI: `pciconf -lv | grep -B 1 -A 2 0x1005`. Anota si la entrada virtio-rnd está vinculada a `virtio_random` (con un `virtio_random0@...` al inicio), a `none` (sin reclamar) o si no aparece en absoluto (comprueba tu línea de comandos de bhyve).
3. Copia el código fuente de etapa 1 del capítulo 18 al guest (mediante scp, un sistema de archivos compartido o cualquier método que prefieras).
4. Dentro del guest, en el directorio del código fuente del driver: `make`. Verifica que se genera `myfirst.ko`.
5. Si `virtio_random` reclamó el dispositivo en el paso 2, descárgalo: `sudo kldunload virtio_random`. Si el dispositivo ya estaba sin reclamar (`none`), salta este paso.
6. Carga `myfirst`: `sudo kldload ./myfirst.ko`.
7. Comprueba el attach: `dmesg | tail -10`. Deberías ver el mensaje de attach de etapa 1.
8. Comprueba el dispositivo: `devinfo -v | grep -B 1 -A 3 myfirst`. Deberías ver `myfirst0` como hijo de `pci0`.
9. Comprueba el enlace: `pciconf -lv | grep myfirst`. Deberías ver la entrada con `myfirst0` como nombre del dispositivo.
10. Descarga el driver: `sudo kldunload myfirst`.
11. Comprueba el detach: `dmesg | tail -5`. Deberías ver el mensaje de detach de etapa 1.
12. Si descargaste `virtio_random` en el paso 5 y quieres restaurarlo: `sudo kldload virtio_random`.

Resultados esperados:

- Cada paso produce la salida esperada.
- Si el `dmesg` del paso 7 no muestra el mensaje de attach, el driver no detectó el dispositivo durante el probe. Comprueba que hayas descargado cualquier otro driver que pudiera haberlo reclamado.
- Si el paso 7 muestra el mensaje de attach pero el paso 8 no muestra `myfirst0`, hay un error de contabilidad en newbus; improbable, pero merece reportarse si lo observas.
- Si el paso 10 falla con `Device busy`, el detach del driver devolvió `EBUSY`. En la etapa 1 no hay ningún cdev abierto; el fallo es inesperado. Revisa el código de detach.

Este laboratorio es la primera vez que el driver del lector se encuentra con un dispositivo real. La recompensa emocional es auténtica: ver `myfirst0: attaching` en `dmesg` es la prueba de que el driver funciona.

### Laboratorio 4: Reclamar el BAR y leer un registro

Tiempo: dos a tres horas.

Objetivo: Extender el driver de etapa 1 a la etapa 2 (asignación del BAR) y a la etapa 3 (primera lectura real de un registro).

Pasos:

1. Partiendo del driver de etapa 1 del laboratorio 3, edita `myfirst_pci.c` para añadir la asignación del BAR de la etapa 2. Compila. Carga. Verifica el mensaje de asignación del BAR en `dmesg`.
2. Verifica que el recurso es visible: `devinfo -v | grep -A 3 myfirst0` debería mostrar un recurso de memoria.
3. Descarga. Verifica que el detach libera limpiamente el BAR.
4. Edita `myfirst_pci.c` de nuevo para añadir el recorrido de capacidades de la etapa 3 y la primera lectura de registro. Compila. Carga. Verifica la salida de capacidades en `dmesg`.
5. Verifica que `CSR_READ_4` opera sobre el BAR real leyendo los primeros cuatro bytes del BAR y comparándolos con los primeros cuatro bytes de `pciconf -r myfirst0 0x00:4`. (Son distintos; uno corresponde al espacio de configuración y el otro al BAR. El objetivo de la comparación es confirmar que ambos producen valores plausibles sin provocar un crash.)
6. Ejecuta el script de regresión completo de la sección 7. Verifica que finaliza sin errores.

Resultados esperados:

- La asignación del BAR se completa correctamente y el recurso es visible en `devinfo -v`.
- El recorrido de capacidades puede mostrar offsets cero para el dispositivo virtio-rnd (el diseño heredado no tiene capacidades PCI como los dispositivos modernos); esto es normal.
- La primera lectura de registro devuelve un valor distinto de cero; el valor exacto depende del estado actual del dispositivo.

Si algún paso produce un crash o un fallo de página, consulta los errores comunes de la sección 7 y comprueba cada paso con la disciplina de asignación de la sección 3 y el código de tag y handle de la sección 4.

### Laboratorio 5: Ejercitar el cdev y verificar la limpieza en el detach

Tiempo: dos a tres horas.

Objetivo: Demostrar que el driver completo del capítulo 18 funciona de extremo a extremo.

Pasos:

1. Partiendo del driver de etapa 3 del laboratorio 4, escribe un pequeño programa en espacio de usuario (`myfirst_test.c`) que abra `/dev/myfirst0`, lea hasta 64 bytes, escriba 16 bytes y cierre el dispositivo.
2. Compila y ejecuta el programa. Observa la salida. Asegúrate de que ningún mensaje del kernel reporta un error.
3. En un segundo terminal, sigue `dmesg` con `dmesg -w`.
4. Ejecuta el programa varias veces, vigilando cualquier advertencia o error.
5. Ejecuta el ciclo de detach-attach diez veces con `devctl detach myfirst0; devctl attach pci0:0:4:0`. Verifica que `dmesg` muestra mensajes de attach y detach correctos en cada ciclo.
6. Tras el ciclo, ejecuta `vmstat -m | grep myfirst` y verifica que el tipo malloc `myfirst` tiene cero asignaciones activas.
7. Descarga el driver. Verifica que `kldstat -v | grep myfirst` no devuelve nada.
8. Recarga el driver. Verifica que el attach se dispara de nuevo.

Resultados esperados:

- Cada paso se completa correctamente.
- La comprobación de `vmstat -m` del paso 6 es la más importante. Si muestra asignaciones activas tras el ciclo de detach, hay una fuga que hay que corregir.
- El ciclo de attach-detach-reattach es estable. El driver puede vincularse, desvincularse y revincularse indefinidamente.

Este laboratorio es la prueba de regresión. Un driver que supera el laboratorio 5 diez veces seguidas sin problemas es un driver que el capítulo 19 puede extender con seguridad.

### Resumen de laboratorios

Los cinco laboratorios en conjunto requieren entre diez y quince horas. Producen un driver PCI completo, un entorno de pruebas funcional, un script de regresión y un pequeño conjunto de comandos de diagnóstico que el lector puede reutilizar en capítulos posteriores. Un lector que ha completado los cinco laboratorios ha realizado el equivalente práctico de leer el capítulo dos veces: los conceptos están anclados en código que se ejecutó, fallos que se corrigieron y salidas que se observaron.

Si algún laboratorio ofrece resistencia (la asignación del BAR falla, el recorrido de capacidades produce un error, el detach pierde un recurso), detente y diagnostica. La sección de solución de problemas al final de este capítulo cubre los modos de fallo más comunes. Los laboratorios están calibrados para funcionar; si uno no funciona, o bien el laboratorio contiene un error sutil (poco frecuente) o bien el entorno del lector tiene algún detalle que difiere del del autor (mucho más habitual). En cualquier caso, el diagnóstico es donde ocurre el aprendizaje real.



## Ejercicios de desafío

Los desafíos parten de los laboratorios. Cada desafío es opcional: el capítulo está completo sin ellos. Pero un lector que los trabaje consolidará lo aprendido y extenderá el driver de formas que el capítulo no abordó.

### Desafío 1: Dar soporte a un segundo vendor y device ID

Amplía `myfirst_pci_ids[]` con una segunda entrada. Apunta a un dispositivo emulado por bhyve diferente: `virtio-blk` (vendor `0x1af4`, device `0x1001`) o `virtio-net` (`0x1af4`, `0x1000`). Descarga el driver base del sistema correspondiente (`virtio_blk` o `virtio_net`), carga `myfirst` y verifica que el attach reconoce el nuevo dispositivo.

Este ejercicio es trivial en código (una entrada en la tabla) pero ejercita la comprensión del lector sobre cómo se toma la decisión de probe. Tras el cambio, ambos dispositivos virtio serán elegibles para `myfirst` si sus drivers se descargan.

### Desafío 2: Imprimir la cadena de capacidades completa

Amplía el código de recorrido de capacidades en `myfirst_pci_attach` para imprimir cada capacidad de la lista, no solo las que el driver conoce. Recorre la lista de capacidades heredadas comenzando en `PCIR_CAP_PTR` y siguiendo los punteros `next`; para cada capacidad, imprime el ID y el offset. Haz lo mismo para la lista de capacidades extendidas comenzando en el offset `0x100`.

Este ejercicio va más allá del tratamiento que el capítulo hace de `pci_find_cap`. Requiere leer `/usr/src/sys/dev/pci/pcireg.h` para encontrar el diseño de las cabeceras de capacidades y capacidades extendidas. La salida en un dispositivo virtio-rnd típico puede ser escasa; en un dispositivo PCIe de hardware real es más rica.

### Desafío 3: Implementar un ioctl simple para acceso al espacio de configuración

Amplía el punto de entrada `ioctl` del cdev para aceptar una solicitud de lectura del espacio de configuración. Define un nuevo comando `ioctl` `MYFIRST_IOCTL_PCI_READ_CFG` que tome una entrada `{ offset, width }` y devuelva un valor `uint32_t`. Haz que la implementación llame a `pci_read_config` bajo `sc->mtx`.

Escribe un programa en espacio de usuario que use el nuevo `ioctl` para leer los primeros 16 bytes del espacio de configuración, byte a byte, e imprimirlos.

Este ejercicio introduce al lector en los ioctls personalizados, que son un patrón habitual para exponer el comportamiento específico del driver al espacio de usuario sin añadir nuevas llamadas al sistema.

### Desafío 4: Rechazar el attach si el BAR es demasiado pequeño

El driver del capítulo 18 asume que el BAR 0 tiene al menos `MYFIRST_REG_SIZE` (64) bytes. Un dispositivo diferente con los mismos vendor y device IDs podría exponer un BAR más pequeño. Amplía la ruta de attach para leer `rman_get_size(sc->bar_res)`, compararlo con `MYFIRST_REG_SIZE` y rechazar el attach (devolviendo `ENXIO` tras la limpieza) si el BAR es demasiado pequeño.

Verifica el comportamiento configurando artificialmente `MYFIRST_REG_SIZE` a un valor mayor que el tamaño real del BAR. El driver debería rechazar el attach y `dmesg` debería imprimir un mensaje informativo.

### Desafío 5: Dividir el driver en dos módulos

Usando la técnica esbozada en la sección 5, divide el driver en `myfirst_core.ko` (capa de hardware, simulación, cdev, locks) y `myfirst_pci.ko` (attach PCI). Añade una declaración `MODULE_DEPEND(myfirst_pci, myfirst_core, 1, 1, 1)`. Verifica que `kldload myfirst_pci` carga automáticamente `myfirst_core` como dependencia.

Este ejercicio supone una refactorización moderada. Introduce al lector en la visibilidad de símbolos entre módulos (qué funciones necesitan exportarse de `myfirst_core` a `myfirst_pci`) y en la resolución de dependencias del cargador de módulos. El resultado es una separación limpia entre la maquinaria genérica del driver y su attach específico de PCI.

### Desafío 6: Reimplementar probe usando coincidencia por clase y subclase

En lugar de hacer la coincidencia por vendor y device ID, amplía la rutina probe para que también coincida por clase y subclase. Por ejemplo, haz coincidir cualquier dispositivo de la clase `PCIC_BASEPERIPH` (periférico base) cuya subclase corresponda a un valor elegido. Devuelve `BUS_PROBE_GENERIC` (una coincidencia de menor prioridad) cuando la coincidencia por clase tiene éxito pero ninguna entrada específica de vendor y device ID coincidió.

Este ejercicio enseña al lector cómo coexisten los drivers. La coincidencia específica por vendor gana sobre la coincidencia por clase (devolviendo `BUS_PROBE_DEFAULT` frente a `BUS_PROBE_GENERIC`). Un driver de reserva puede reclamar dispositivos que ningún driver específico reconoce.

### Desafío 7: Añadir un sysctl de solo lectura que informe del estado PCI del driver

Añade un sysctl `dev.myfirst.N.pci_info` que devuelva una cadena corta describiendo el attachment PCI del driver: los vendor y device IDs, el subvendor y subsistema, el B:D:F, y el tamaño y la dirección del BAR. Usa `sbuf_printf` para formatear la cadena.

El resultado es un volcado legible desde el espacio de usuario de la visión que tiene el driver de su dispositivo. Es útil para el diagnóstico y se convierte en un patrón que los drivers de dispositivos más complejos reutilizan.

### Desafío 8: Simular un attach fallido

Introduce un sysctl `hw.myfirst.fail_attach` que, cuando se establece a 1, hace que el attach falle tras reclamar el BAR. Verifica que la cascada de `goto` limpia correctamente y que `vmstat -m | grep myfirst` muestra cero fugas tras el attach fallido.

Este ejercicio pone a prueba la ruta de fallo parcial que describió la sección 6 pero que la secuencia de laboratorios no probó explícitamente. Es la mejor forma de confirmar que la cascada de desenrollado es correcta.

### Resumen de desafíos

Ocho desafíos que cubren un rango de dificultad. Un lector que completa cuatro o cinco de ellos habrá profundizado su comprensión de manera significativa. Un lector que completa los ocho ha escrito en esencia un segundo capítulo 18.

Guarda tus soluciones. Varios de ellos (desafío 1, desafío 3, desafío 7) son puntos de partida naturales para las extensiones del capítulo 19.



## Solución de problemas y errores comunes

Esta sección recopila los fallos más comunes que puedes encontrar en los laboratorios del Capítulo 18. Cada entrada indica el síntoma, la causa probable y la solución.

### "El driver no se adjunta; no aparece ningún mensaje en dmesg"

Síntoma: `kldload ./myfirst.ko` devuelve éxito. `dmesg | tail` no muestra nada de `myfirst`. `devinfo -v` no lista `myfirst0`.

Causas probables:

1. Otro driver ha reclamado el dispositivo objetivo. Comprueba `pciconf -lv` para el dispositivo y observa qué driver (si hay alguno) está enlazado. Si `virtio_random0` tiene el dispositivo virtio-rnd, la disputa de prioridad de probe la gana `virtio_random` y `myfirst` nunca se adjunta. Solución: ejecuta `kldunload virtio_random` primero.

2. El ID de proveedor o de dispositivo en `myfirst_pci_ids[]` es incorrecto. Comprueba el dispositivo real del invitado. Solución: corrige los IDs.

3. La rutina probe tiene un error que siempre devuelve `ENXIO`. Comprueba que la comparación enfrenta `vendor` y `device` con las entradas de la tabla, no consigo mismos. Solución: relee el código de probe con atención.

4. La declaración `DRIVER_MODULE` falta o es incorrecta. Comprueba que el tercer argumento es el `driver_t` y el segundo es `"pci"`. Solución: corrige la declaración.

### "kldload provoca un pánico del kernel"

Síntoma: `kldload ./myfirst.ko` hace que el kernel se bloquee antes de retornar.

Causas probables:

1. Falta `MODULE_DEPEND(myfirst, pci, ...)`. El driver intenta registrarse en un bus que aún no está inicializado. Solución: añade la declaración.

2. La inicialización del driver llama a una función que no existe en el momento de carga del módulo. Es poco frecuente, pero posible si el driver define un manejador `MOD_LOAD` que accede a funciones `device_*` antes de que el bus esté listo.

3. El tamaño del softc declarado en el `driver_t` es incorrecto. Si el código de attach espera campos que no están en la estructura declarada, el kernel escribe más allá del bloque asignado y se bloquea. Solución: asegúrate de que `sizeof(struct myfirst_softc)` coincide con la definición de la estructura.

El kernel de depuración es muy bueno detectando los tres casos; el backtrace en `ddb` nombrará la función donde se produjo el bloqueo.

### "La asignación de BAR falla con NULL"

Síntoma: `bus_alloc_resource_any` devuelve NULL. `dmesg` dice "cannot allocate BAR0".

Causas probables:

1. `rid` incorrecto. Usa `PCIR_BAR(0)` para BAR 0, no `0`. Solución: usa la macro.

2. Tipo incorrecto. Si el BAR 0 del dispositivo es un puerto I/O (el bit 0 del BAR está activo en el espacio de configuración), pasar `SYS_RES_MEMORY` falla. Lee el valor del BAR con `pci_read_config(dev, PCIR_BAR(0), 4)` y comprueba el bit menos significativo. Solución: usa el tipo correcto.

3. El BAR ya ha sido asignado por otro driver o por la BIOS. Es improbable en un invitado bhyve; posible en hardware real con una BIOS mal configurada. Solución: comprueba `devinfo -v` para los recursos reclamados.

4. Falta el indicador `RF_ACTIVE`. El recurso se asigna pero no se activa. El manejador no se puede usar para accesos `bus_space`. Solución: añade `RF_ACTIVE`.

### "CSR_READ_4 devuelve 0xffffffff"

Síntoma: las lecturas de registro devuelven todo unos. El lector espera valores distintos de cero.

Causas probables:

1. El BAR no está activado. Comprueba `RF_ACTIVE` en la llamada a `bus_alloc_resource_any`.

2. El tag y el handle están intercambiados. `rman_get_bustag` devuelve el tag; `rman_get_bushandle` devuelve el handle. Pasarlos a `bus_space_read_4` en orden incorrecto produce comportamiento indefinido.

3. El desplazamiento es incorrecto. El BAR tiene 32 bytes; leer en el desplazamiento 64 va más allá del final. El `KASSERT` del kernel de depuración en `myfirst_reg_read` lo detecta.

4. El dispositivo ha sido reiniciado o apagado. Algunos dispositivos devuelven todo unos cuando están apagados. Lee el registro de comando con `pci_read_config(dev, PCIR_COMMAND, 2)`; si devuelve `0xffff`, el dispositivo no responde.

### "kldunload devuelve Device busy"

Síntoma: `kldunload myfirst` falla con `Device busy`.

Causas probables:

1. Un proceso en espacio de usuario tiene `/dev/myfirst0` abierto. Cierra el proceso. Comprueba con `fstat /dev/myfirst0`.

2. El driver tiene una operación en curso (callout de simulación, trabajo en taskqueue). Espera unos segundos y vuelve a intentarlo.

3. La función detach devuelve `EBUSY` incondicionalmente de forma incorrecta. Comprueba el código de detach.

4. La verificación de ocupación del driver tiene una referencia obsoleta a un campo no inicializado. Comprueba que `sc->open_count` es cero cuando no hay descriptores abiertos.

### "dmesg muestra 'cleanup failed in detach'"

Síntoma: `dmesg` muestra un aviso en la ruta de detach.

Causas probables:

1. Un callout seguía programado cuando se ejecutó detach. Comprueba que se llamó a `callout_drain` antes de la limpieza del softc del driver.

2. Un elemento de trabajo del taskqueue seguía pendiente. Comprueba que se llamó a `taskqueue_drain`.

3. El cdev estaba abierto en el momento del detach. La llamada a `destroy_dev` debería bloquearse hasta que se cierre, pero si el driver libera otros recursos antes, el cierre encontrará estado obsoleto. Corrige el orden: destruye el cdev antes de liberar los recursos dependientes.

### "ioctl o read devuelve un error inesperado"

Síntoma: una llamada al sistema en espacio de usuario devuelve un error inesperado (EINVAL, ENODEV, ENXIO, etc.).

Causas probables:

1. El punto de entrada del cdev comprueba un estado que el driver no estableció. Ejemplo: el driver del Capítulo 10 comprueba `sc->is_attached`; el driver del Capítulo 18 puede haber olvidado establecerlo.

2. El número de comando ioctl en espacio de usuario no coincide con el del driver. Comprueba las macros `_IOR`/`_IOW`/`_IOWR` y confirma que los tipos son iguales.

3. El orden del lock es incorrecto. El punto de entrada del cdev toma un lock en un orden que entra en conflicto con otro código. `WITNESS` en un kernel de depuración lo informa.

### "vmstat -m muestra asignaciones sin liberar"

Síntoma: tras un ciclo de carga y descarga, `vmstat -m | grep myfirst` muestra "Allocations" o "InUse" distintos de cero.

Causas probables:

1. Una asignación malloc en attach que no se libera en detach. Normalmente es la estructura envolvente de la capa de hardware o un buffer sysctl.

2. Un callout que no fue drenado. El callout asigna una pequeña estructura; si se ejecuta después de detach, la estructura queda sin liberar.

3. El tipo malloc `M_MYFIRST` se usa para el softc. Newbus libera el softc automáticamente; el driver no debe llamar a `malloc(M_MYFIRST, sizeof(softc))` en attach. El softc lo asigna newbus.

### "pci_find_cap devuelve ENOENT para una capacidad que el dispositivo tiene"

Síntoma: `pci_find_cap(dev, PCIY_EXPRESS, &capreg)` devuelve `ENOENT`, pero el dispositivo es PCIe y debería tener la capacidad PCI Express.

Causas probables:

1. El dispositivo es un dispositivo PCI legado en una ranura PCIe (funciona porque PCIe es compatible con versiones anteriores de PCI). Los dispositivos legados no tienen la capacidad PCI Express. Comprueba leyendo `pci_get_class(dev)` y comparando con lo que esperabas.

2. La lista de capacidades está corrupta o vacía. Lee `PCIR_CAP_PTR` directamente con `pci_read_config(dev, PCIR_CAP_PTR, 1)`; si devuelve cero, el dispositivo no implementa capacidades.

3. ID de capacidad incorrecto. `PCIY_EXPRESS` es `0x10`, no `0x1f`. Consulta `pcireg.h` para la constante correcta.

4. El bit `PCIM_STATUS_CAPPRESENT` del registro de estado es cero. Este bit indica al subsistema PCI que el dispositivo implementa una lista de capacidades. Sin él, la lista no está presente. El bit se encuentra en `PCIR_STATUS`.

### "El módulo se descarga, pero dmesg muestra un fallo de página durante la descarga"

Síntoma: `kldunload myfirst` parece tener éxito, pero `dmesg` muestra un fallo de página ocurrido durante la descarga.

Causas probables:

1. Un callout se disparó después de `myfirst_hw_detach` pero antes de que el driver retornara. El callout accedió a `sc->hw`, que había sido puesto a NULL. Solución: asegúrate de que se llama a `callout_drain` antes de `myfirst_hw_detach`.

2. Un elemento de trabajo del taskqueue se ejecutó después de liberar los recursos. Solución: asegúrate de que se llama a `taskqueue_drain` antes de liberar cualquier cosa que la tarea utilice.

3. Un proceso en espacio de usuario sigue teniendo `/dev/myfirst0` abierto. La llamada a `destroy_dev` se completa rápidamente, pero cualquier operación I/O pendiente contra el cdev continúa hasta que el proceso cierra el descriptor o termina. Solución: asegúrate de que todos los consumidores en espacio de usuario cierran el cdev antes del detach; en situaciones de emergencia, `devctl detach` seguido de terminar el proceso funciona.

### "devinfo -v muestra el driver adjuntado pero el cdev no aparece"

Síntoma: `devinfo -v | grep myfirst` muestra `myfirst0`, pero `ls /dev/myfirst*` no devuelve nada.

Causas probables:

1. La llamada a `make_dev` falló y el attach no comprobó el valor de retorno. Comprueba `sc->cdev` después de `make_dev`; si es NULL, la llamada falló.

2. El nombre del cdev no es `myfirst%d`. Comprueba la cadena de formato en la llamada a `make_dev`. La ruta del nodo de dispositivo usa exactamente la cadena pasada a `make_dev`.

3. La estructura `cdevsw` no ha sido registrada o tiene métodos incorrectos. Comprueba que `myfirst_cdevsw` está inicializada correctamente.

4. Una entrada obsoleta en `/dev` está ocultando la nueva. Prueba `sudo devfs rule -s 0 apply` o reinicia. Es poco probable en FreeBSD moderno, pero posible en casos extremos.

### "Attach tarda demasiado en retornar"

Síntoma: `kldload ./myfirst.ko` se queda bloqueado durante segundos o minutos.

Causas probables:

1. Una llamada a `DELAY` o `pause_sbt` en attach es demasiado larga. Busca retardos ocultos en los recorridos de capacidades o en el proceso de puesta en marcha del dispositivo.

2. Una llamada a `bus_alloc_resource_any` está bloqueada en un recurso que otro driver ha asignado. Es poco frecuente en PCI; más común en plataformas con espacio de puertos I/O limitado.

3. Un bucle infinito en el recorrido de capacidades. Un dispositivo malformado podría producir un bucle; el contador de seguridad del recorrido protege contra esto.

4. Una llamada a `callout_init_mtx` está esperando un lock que otro camino de código tiene retenido. Deadlock; comprueba la salida de `WITNESS` en `dmesg`.

### "El driver se adjunta en el arranque pero no produce salida durante los primeros segundos"

Síntoma: tras un reinicio del invitado con `myfirst` cargado en el arranque, el driver se adjunta pero tarda segundos en producir cualquier mensaje de registro.

Causas probables:

1. El módulo se cargó en una fase temprana del boot, antes de que la consola estuviera completamente inicializada. Los mensajes están en el buffer del kernel pero aún no se han escrito en la consola. Comprueba `dmesg` para los mensajes; deberían estar presentes.

2. Un callout fue programado pero aún no se ha disparado. El callout del sensor del Capítulo 17 se ejecuta cada segundo; el primer tick llega un segundo después del attach.

3. El driver está esperando una condición que tarda tiempo. No es un problema del Capítulo 18, pero es posible en drivers que esperan a que el dispositivo complete un reset.

### "Un segundo intento de adjuntar después de un primer intento fallido tiene éxito"

Síntoma: `kldload` en un kernel mal configurado falla; un segundo `kldload` tras corregir la configuración tiene éxito. Este es en realidad el comportamiento esperado.

Causa probable: el cargador de módulos del kernel no mantiene estado entre intentos de carga. Una carga fallida elimina cualquier estado parcial. Una carga posterior lo intenta de nuevo con el estado limpio. El síntoma no es un error.

### "vmstat -m InUse crece después de cada ciclo de carga y descarga"

Síntoma: el tipo malloc de `myfirst` muestra unos pocos bytes de memoria `InUse` que aumentan en cada ciclo.

Causas probables:

1. Una fuga en attach o detach demasiado pequeña para notarse en un solo ciclo, pero que se acumula. Ejecuta 100 ciclos y observa el crecimiento.

2. La estructura envolvente `myfirst_hw` o `myfirst_sim` se asigna pero no se libera. Comprueba que la ruta de detach llama a `myfirst_hw_detach` y a `myfirst_sim_detach` (si la simulación está cargada).

3. Una cadena u otra pequeña asignación en un manejador sysctl está filtrando. Comprueba los manejadores sysctl para ver si hay algún `sbuf` que se crea pero no se elimina.

La salida de `vmstat -m` tiene columnas para `Requests`, `InUse` y `MemUse`. `Requests` es el número total de asignaciones realizadas desde siempre. `InUse` es el número actualmente asignado. `MemUse` son los bytes totales. El `InUse` de un driver sano vuelve a cero después del detach y la descarga.

### Resumen de resolución de problemas

Todos estos fallos son recuperables. El kernel de depuración (con `INVARIANTS`, `WITNESS` y `KDB`) detecta la mayoría con un mensaje útil. Un lector que ejecute un kernel de depuración y lea el mensaje con atención corregirá la mayoría de los errores del Capítulo 18 en menos de una hora.

Si un error persiste, el siguiente paso es releer la sección relevante de este capítulo. La lista de resolución de problemas anterior es corta porque la enseñanza del capítulo está diseñada deliberadamente para prevenir estos fallos. Cuando ocurre un fallo, la pregunta suele ser «¿qué disciplina de qué sección rompí?» y la respuesta suele ser obvia en una segunda lectura.

### Lista de comprobación para un kernel de depuración

Si te tomas en serio el desarrollo y la depuración de drivers, compila un kernel de depuración. Las opciones de configuración que detectan de forma fiable los errores de drivers PCI son:

```text
options INVARIANTS
options INVARIANT_SUPPORT
options WITNESS
options WITNESS_SKIPSPIN
options DEBUG_VFS_LOCKS
options DEBUG_MEMGUARD
options DIAGNOSTIC
options DDB
options KDB
options KDB_UNATTENDED
options MALLOC_DEBUG_MAXZONES=8
```

Un driver que supera sus pruebas de regresión con un kernel que tiene todas estas opciones activadas es un driver que raramente producirá errores en producción. El coste en tiempo de ejecución es considerable (el kernel es más lento, y `WITNESS` en particular añade una sobrecarga medible a cada operación con locks), pero el valor para la depuración es enorme.

Construye el kernel de depuración con:

```sh
cd /usr/src
sudo make buildkernel KERNCONF=GENERIC-DEBUG
sudo make installkernel KERNCONF=GENERIC-DEBUG
sudo shutdown -r now
```

Usa el kernel de depuración para todo el desarrollo de drivers; vuelve a `GENERIC` solo cuando vayas a medir el rendimiento.

## En resumen

El capítulo 18 transformó el driver simulado en un driver PCI. El punto de partida fue `1.0-simulated`, un módulo con un bloque de registros respaldado por `malloc(9)` y una simulación del capítulo 17 que hacía respirar los registros. El punto de llegada es `1.1-pci`, el mismo módulo con un nuevo archivo (`myfirst_pci.c`), una nueva cabecera (`myfirst_pci.h`), y un puñado de pequeñas ampliaciones en los archivos existentes. La capa de acceso no cambió. El protocolo de comando-respuesta no cambió. La disciplina de locks no cambió. Lo que cambió es el origen del tag y el handle que utilizan los accesores.

La transición recorrió ocho secciones. La sección 1 introdujo PCI como concepto, cubriendo la topología, la tupla B:D:F, el espacio de configuración, los BARs, los IDs de vendedor y dispositivo, y el subsistema `pci(4)`. La sección 2 escribió el esqueleto probe-attach-detach, vinculado al bus PCI con `DRIVER_MODULE(9)` y `MODULE_DEPEND(9)`. La sección 3 explicó qué son los BARs y reclamó uno mediante `bus_alloc_resource_any(9)`. La sección 4 conectó el BAR reclamado con la capa de acceso del capítulo 16, completando la transición del acceso simulado al acceso real a los registros. La sección 5 añadió la infraestructura del attach: `pci_find_cap(9)` y `pci_find_extcap(9)` para el descubrimiento de capacidades, la creación del cdev, y la disciplina que mantiene inactiva la simulación del capítulo 17 en la ruta PCI. La sección 6 consolidó la ruta de detach con un orden estrictamente inverso, una comprobación de ocupado, el drenado de callouts y tasks, y la recuperación ante fallos parciales. La sección 7 probó el driver en un guest de bhyve o QEMU, ejercitando cada ruta que el driver expone. La sección 8 refactorizó el código hasta su forma definitiva y documentó el resultado.

Lo que el capítulo 18 no hizo es el manejo de interrupciones. El dispositivo virtio-rnd bajo bhyve tiene una línea de interrupción; nuestro driver no registra un handler para ella; los cambios de estado interno del dispositivo no llegan al driver. El cdev sigue siendo accesible, pero la ruta de datos no tiene un productor activo en el build PCI (los callouts de la simulación del capítulo 17 no están en marcha). El capítulo 19 introduce el handler real que proporcionará a la ruta de datos un productor.

Lo que el capítulo 18 sí logró es el cruce de un umbral. Hasta el final del capítulo 17, el driver `myfirst` era un módulo de enseñanza: existía porque lo cargábamos, no porque ningún dispositivo lo requiriese. A partir del capítulo 18, el driver es un driver PCI: existe porque el kernel enumeró un dispositivo y nuestro probe dijo que sí. La maquinaria newbus lleva el driver ahora. Todos los capítulos posteriores de la parte 4 lo amplían sin alterar esta relación fundamental.

El conjunto de archivos ha crecido: `myfirst.c`, `myfirst_hw.c`, `myfirst_hw.h`, `myfirst_sim.c`, `myfirst_sim.h`, `myfirst_pci.c`, `myfirst_pci.h`, `myfirst_sync.h`, `cbuf.c`, `cbuf.h`, `myfirst.h`. La documentación ha crecido: `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`. El conjunto de pruebas ha crecido: los scripts de configuración de bhyve o QEMU, el script de regresión, los pequeños programas de prueba en espacio de usuario. Cada uno de estos elementos es una capa; cada uno fue introducido en un capítulo concreto y forma ya parte permanente de la historia del driver.

### Una reflexión antes del capítulo 19

Una pausa antes del próximo capítulo. El capítulo 18 enseñó el subsistema PCI y la danza de attach de newbus. Los patrones que has practicado aquí (probe-attach-detach, reclamación y liberación de recursos, extracción del tag y el handle, descubrimiento de capacidades) son patrones que utilizarás a lo largo de toda tu vida escribiendo drivers. Son igual de aplicables a la danza de attach de USB del capítulo 21 que a la danza PCI que acabas de escribir, igual de válidos para el driver de NIC que podrías escribir para una tarjeta real que para el driver de demo que acabas de ampliar. La habilidad con PCI es permanente.

El capítulo 18 también enseñó la disciplina del orden estrictamente inverso en detach. La cascada de goto en attach, el detach especular, la comprobación de ocupado, el paso de quiesce: son los patrones que evitan que un driver filtre recursos a lo largo de su ciclo de vida. Se aplican a todo tipo de driver, no solo a PCI. Un lector que haya interiorizado la disciplina de detach del capítulo 18 escribirá código más limpio en los capítulos 19, 20 y 21.

Una observación más. El fruto de la capa de acceso del capítulo 16 es ahora visible. Un lector que escribió los accesores del capítulo 16 y se preguntó "¿vale la pena el esfuerzo?" puede mirar el attach de la etapa 3 del capítulo 18 y ver la respuesta. El código de las capas superiores del driver (cada punto donde se usa `CSR_READ_4`, `CSR_WRITE_4` o `CSR_UPDATE_4`) no cambió en absoluto cuando el backend pasó de simulado a PCI real. Eso es lo que compra una buena abstracción: un cambio importante en la capa inferior tiene coste cero en las capas superiores. Los accesores del capítulo 16 fueron la abstracción. El capítulo 18 fue la prueba.

### Qué hacer si estás atascado

Dos sugerencias.

Primera, concéntrate en el script de regresión de la sección 7. Si el script se ejecuta de principio a fin sin errores, el driver funciona; cualquier confusión sobre los detalles internos es decorativa. Si el script falla, el primer paso fallido es el punto de partida para la depuración.

Segunda, abre `/usr/src/sys/dev/uart/uart_bus_pci.c` y léelo despacio. El archivo tiene 366 líneas. Cada línea es un patrón que el capítulo 18 enseñó o referenció. Leerlo después del capítulo 18 debería resultar familiar: probe, attach, detach, tabla de IDs, `DRIVER_MODULE`, `MODULE_DEPEND`. Un lector que encuentra el archivo legible después del capítulo 18 ha hecho el progreso real del capítulo.

Tercera, deja los desafíos para una segunda pasada. Los laboratorios están calibrados para el capítulo 18; los desafíos asumen que el material del capítulo ya está asentado. Vuelve a ellos después del capítulo 19 si ahora te parecen inalcanzables.

El objetivo del capítulo 18 era dejar que el driver se encontrara con hardware real. Si lo ha hecho, el resto de la parte 4 se sentirá como una progresión natural: el capítulo 19 añade interrupciones, el capítulo 20 añade MSI y MSI-X, los capítulos 20 y 21 añaden DMA. Cada capítulo amplía lo que el capítulo 18 estableció.



## Puente hacia el capítulo 19

El capítulo 19 lleva por título *Manejo de interrupciones*. Su alcance es el tema que el capítulo 18 dejó deliberadamente fuera: la ruta que permite que un dispositivo le comunique al driver, de forma asíncrona, que algo ha ocurrido. La simulación del capítulo 17 utilizaba callouts para producir cambios de estado autónomos. El driver PCI real del capítulo 18 ignora por completo la línea de interrupción del dispositivo. El capítulo 19 registra un handler mediante `bus_setup_intr(9)`, lo vincula a un recurso IRQ asignado a través de `bus_alloc_resource_any(9)` con `SYS_RES_IRQ`, y enseña al driver a reaccionar ante las propias señales del dispositivo.

El capítulo 18 preparó el terreno de cuatro formas concretas.

Primera, **tienes un driver conectado a PCI**. El driver del capítulo 18 en `1.1-pci` asigna un BAR, reclama un recurso de memoria, y tiene todos los hooks de newbus en su sitio. El capítulo 19 añade un recurso más (un IRQ) y otro par de llamadas (`bus_setup_intr` y `bus_teardown_intr`). El resto del flujo de attach y detach permanece intacto.

Segunda, **tienes una capa de acceso que puede invocarse desde un contexto de interrupción**. Los accesores del capítulo 16 toman `sc->mtx`; un handler de interrupción que necesite leer o escribir un registro adquiere `sc->mtx` y llama a `CSR_READ_4` o `CSR_WRITE_4`. El handler del capítulo 19 se compondrá con los accesores sin necesidad de ninguna nueva infraestructura.

Tercera, **tienes un orden de detach que contempla el desmontaje del IRQ**. El detach del capítulo 18 libera el BAR en un punto concreto de la secuencia; el detach del capítulo 19 liberará el recurso IRQ antes de liberar el BAR. La cascada de goto se amplía con una etiqueta más; el patrón no cambia.

Cuarta, **tienes un entorno de prueba que genera interrupciones**. El guest de bhyve o QEMU con un dispositivo virtio-rnd es el mismo entorno que utiliza el capítulo 19; la línea de interrupción del dispositivo virtio-rnd es lo que recibe el handler del capítulo 19. No se necesita ninguna configuración nueva del laboratorio.

Los temas concretos que cubrirá el capítulo 19:

- Qué es una interrupción, en contraste con un callout por sondeo.
- El modelo de dos etapas de los handlers de interrupción en FreeBSD: filter (rápido, en contexto de interrupción) e ithread (lento, en un contexto de thread del kernel).
- `bus_alloc_resource_any(9)` con `SYS_RES_IRQ`.
- `bus_setup_intr(9)` y `bus_teardown_intr(9)`.
- Las banderas `INTR_TYPE_*` e `INTR_MPSAFE`.
- Lo que un handler de interrupción puede y no puede hacer (sin sleep, sin locks bloqueantes, sin `malloc(M_WAITOK)`).
- Leer un registro de estado en el momento de la interrupción para determinar qué ocurrió.
- Limpiar las banderas de interrupción para evitar la reentrada.
- Registrar interrupciones de forma segura.
- Interacción entre las interrupciones y el registro de acceso del capítulo 16.
- Un handler de interrupción mínimo que incrementa un contador y registra el evento.

No es necesario que leas por adelantado. El capítulo 18 es preparación suficiente. Trae tu driver `myfirst` en `1.1-pci`, tu `LOCKING.md`, tu `HARDWARE.md`, tu `SIMULATION.md`, tu nuevo `PCI.md`, tu kernel con `WITNESS` activado, y tu script de regresión. El capítulo 19 empieza donde el capítulo 18 terminó.

El capítulo 20 está dos capítulos más adelante; merece una breve referencia anticipada. MSI y MSI-X reemplazarán la única línea de interrupción heredada por un mecanismo de enrutamiento más rico: vectores separados para tareas separadas, coalescencia de interrupciones, afinidad por cola. Las funciones `pci_alloc_msi(9)` y `pci_alloc_msix(9)` forman parte del subsistema PCI que el capítulo 18 introdujo; las dejamos para el capítulo 20 porque MSI-X en particular requiere una comprensión más profunda del manejo de interrupciones de la que el capítulo 18 estaba listo para introducir. Si el lector ha visto los desplazamientos `PCIY_MSI` y `PCIY_MSIX` en el recorrido de capacidades y se ha preguntado para qué sirven, el capítulo 20 es la respuesta.

La conversación con el hardware se está profundizando. El vocabulario es tuyo; el protocolo es tuyo; la disciplina es tuya. El capítulo 19 añade la siguiente pieza que faltaba.



## Referencia: desplazamientos de cabecera PCI utilizados en este capítulo

Una referencia compacta de los desplazamientos del espacio de configuración que el capítulo 18 menciona, extraídos de `/usr/src/sys/dev/pci/pcireg.h`. Tenla a mano mientras escribes código PCI.

| Offset | Macro | Ancho | Significado |
|--------|-------|-------|-------------|
| 0x00 | `PCIR_VENDOR` | 2 | ID de vendedor |
| 0x02 | `PCIR_DEVICE` | 2 | ID de dispositivo |
| 0x04 | `PCIR_COMMAND` | 2 | Registro de comandos |
| 0x06 | `PCIR_STATUS` | 2 | Registro de estado |
| 0x08 | `PCIR_REVID` | 1 | ID de revisión |
| 0x09 | `PCIR_PROGIF` | 1 | Interfaz de programación |
| 0x0a | `PCIR_SUBCLASS` | 1 | Subclase |
| 0x0b | `PCIR_CLASS` | 1 | Clase |
| 0x0c | `PCIR_CACHELNSZ` | 1 | Tamaño de línea de caché |
| 0x0d | `PCIR_LATTIMER` | 1 | Temporizador de latencia |
| 0x0e | `PCIR_HDRTYPE` | 1 | Tipo de cabecera |
| 0x0f | `PCIR_BIST` | 1 | Autotest integrado |
| 0x10 | `PCIR_BAR(0)` | 4 | BAR 0 |
| 0x14 | `PCIR_BAR(1)` | 4 | BAR 1 |
| 0x18 | `PCIR_BAR(2)` | 4 | BAR 2 |
| 0x1c | `PCIR_BAR(3)` | 4 | BAR 3 |
| 0x20 | `PCIR_BAR(4)` | 4 | BAR 4 |
| 0x24 | `PCIR_BAR(5)` | 4 | BAR 5 |
| 0x2c | `PCIR_SUBVEND_0` | 2 | Vendedor de subsistema |
| 0x2e | `PCIR_SUBDEV_0` | 2 | Dispositivo de subsistema |
| 0x34 | `PCIR_CAP_PTR` | 1 | Inicio de la lista de capacidades |
| 0x3c | `PCIR_INTLINE` | 1 | Línea de interrupción |
| 0x3d | `PCIR_INTPIN` | 1 | Pin de interrupción |

### Bits del registro de comandos

| Bit | Macro | Significado |
|-----|-------|-------------|
| 0x0001 | `PCIM_CMD_PORTEN` | Habilitar espacio de I/O |
| 0x0002 | `PCIM_CMD_MEMEN` | Habilitar espacio de memoria |
| 0x0004 | `PCIM_CMD_BUSMASTEREN` | Habilitar bus master |
| 0x0008 | `PCIM_CMD_SPECIALEN` | Habilitar ciclos especiales |
| 0x0010 | `PCIM_CMD_MWRICEN` | Escritura e invalidación de memoria |
| 0x0020 | `PCIM_CMD_PERRESPEN` | Respuesta a errores de paridad |
| 0x0040 | `PCIM_CMD_SERRESPEN` | Habilitar SERR# |
| 0x0400 | `PCIM_CMD_INTxDIS` | Deshabilitar la generación de INTx |

### IDs de capacidad (legado)

| Valor | Macro | Significado |
|-------|-------|-------------|
| 0x01 | `PCIY_PMG` | Gestión de energía |
| 0x05 | `PCIY_MSI` | Interrupciones con señalización por mensaje |
| 0x09 | `PCIY_VENDOR` | Específico del fabricante |
| 0x10 | `PCIY_EXPRESS` | PCI Express |
| 0x11 | `PCIY_MSIX` | MSI-X |

### IDs de capacidad extendida (PCIe)

| Valor | Macro | Significado |
|-------|-------|---------|
| 0x0001 | `PCIZ_AER` | Notificación avanzada de errores |
| 0x0002 | `PCIZ_VC` | Canal virtual |
| 0x0003 | `PCIZ_SERNUM` | Número de serie del dispositivo |
| 0x0004 | `PCIZ_PWRBDGT` | Presupuesto de energía |
| 0x000d | `PCIZ_ACS` | Servicios de control de acceso |
| 0x0010 | `PCIZ_SRIOV` | Virtualización de I/O con raíz única |

Un lector que necesite otras constantes PCI puede abrir directamente `/usr/src/sys/dev/pci/pcireg.h`. El archivo está bien comentado; encontrar un offset o bit concreto no lleva más de un minuto.

## Referencia: Comparación con los patrones de los capítulos 16 y 17

Una comparación lado a lado de dónde el capítulo 18 extiende los capítulos 16 y 17 y dónde introduce material genuinamente nuevo.

| Patrón | Capítulo 16 | Capítulo 17 | Capítulo 18 |
|---------|-----------|-----------|-----------|
| Acceso a registros | `CSR_READ_4`, etc. | Misma API, sin cambios | Misma API, sin cambios |
| Registro de accesos | Introducido | Extendido con entradas de inyección de fallos | Sin cambios |
| Disciplina de lock | `sc->mtx` en cada acceso | Igual, más callouts | Igual |
| Estructura de archivos | Se añade `myfirst_hw.c` | Se añade `myfirst_sim.c` | Se añade `myfirst_pci.c` |
| Mapa de registros | 10 registros, 40 bytes | 16 registros, 60 bytes | Igual |
| Rutina de attach | Simple (bloque `malloc`) | Simple (bloque `malloc` más configuración sim) | Reclamación real de BAR PCI |
| Rutina de detach | Simple | Igual más vaciado de callout | Igual más liberación de BAR |
| Carga del módulo | `kldload` activa la carga | Igual | `kldload` más probe PCI |
| Instancia de dispositivo | Global (implícita) | Global | Por dispositivo PCI, numerada |
| BAR | N/A | N/A | BAR 0, `SYS_RES_MEMORY`, `RF_ACTIVE` |
| Recorrido de capacidades | N/A | N/A | `pci_find_cap` / `pci_find_extcap` |
| cdev | Creado al cargar el módulo | Igual | Creado por cada attach |
| Versión | 0.9-mmio | 1.0-simulated | 1.1-pci |
| Documentación | Se introduce `HARDWARE.md` | Se introduce `SIMULATION.md` | Se introduce `PCI.md` |

El capítulo 18 se construye sobre los capítulos 16 y 17 sin romper nada. Cada capacidad de los capítulos anteriores se conserva; el attach PCI real se añade como un nuevo backend que se integra en la estructura existente. El driver en `1.1-pci` es un superconjunto estricto del driver en `1.0-simulated`.



## Referencia: Patrones de drivers PCI reales de FreeBSD

Un breve recorrido por los patrones que aparecen de forma recurrente en el árbol `/usr/src/sys/dev/`. Cada patrón es un fragmento concreto de un driver real, ligeramente reescrito para mayor legibilidad, con una referencia al archivo y una nota breve sobre por qué importa el patrón. Leer estos patrones tras el capítulo 18 consolida el vocabulario.

### Patrón: Recorrido de BARs por tipo

De `/usr/src/sys/dev/e1000/if_em.c`:

```c
for (rid = PCIR_BAR(0); rid < PCIR_CIS;) {
	val = pci_read_config(dev, rid, 4);
	if (EM_BAR_TYPE(val) == EM_BAR_TYPE_IO) {
		break;
	}
	rid += 4;
	if (EM_BAR_MEM_TYPE(val) == EM_BAR_MEM_TYPE_64BIT)
		rid += 4;
}
```

Este bucle recorre la tabla de BARs en busca del BAR de puertos I/O. Lee el valor en el espacio de configuración de cada BAR, comprueba su bit de tipo y avanza 4 bytes (una ranura de BAR) u 8 bytes (dos ranuras, para un BAR de memoria de 64 bits). El bucle termina en `PCIR_CIS` (el puntero CardBus, que queda justo más allá de la tabla de BARs) o cuando encuentra un BAR de tipo I/O.

Por qué importa: en los drivers que admiten una combinación de BARs de memoria y de I/O en una gama de revisiones de hardware, la disposición de BARs no es fija. El recorrido dinámico es el enfoque correcto. El driver del capítulo 18 apunta a un dispositivo con una disposición de BARs conocida y no necesita este recorrido; un driver como `em(4)` que cubre una familia de chips sí lo necesita.

### Patrón: Coincidencia por clase, subclase y progIF

De `/usr/src/sys/dev/uart/uart_bus_pci.c`:

```c
if (pci_get_class(dev) == PCIC_SIMPLECOMM &&
    pci_get_subclass(dev) == PCIS_SIMPLECOMM_UART &&
    pci_get_progif(dev) < PCIP_SIMPLECOMM_UART_16550A) {
	id = &cid;
	sc->sc_class = &uart_ns8250_class;
	goto match;
}
```

Este fragmento es un mecanismo de respaldo basado en clase. Si la coincidencia por fabricante y dispositivo falla, el probe recurre a la coincidencia con cualquier dispositivo que anuncie «simple communications / UART / pre-16550A» en su código de clase. El campo progIF distingue las variantes 16450, 16550A y posteriores; el fragmento apunta específicamente a las más antiguas.

Por qué importa: los códigos de clase permiten que un driver haga attach a familias de dispositivos que la tabla de coincidencias específica no enumera. Un chip UART de un fabricante que no figura en la tabla de `uart(4)` sigue siendo gestionado mientras el código de clase sea estándar. El patrón funciona bien para tipos de dispositivos estandarizados (AHCI, xHCI, UART, NVMe, HD Audio) cuya interfaz de programación está definida por la clase.

### Patrón: Asignación condicional de MSI

De `/usr/src/sys/dev/uart/uart_bus_pci.c`:

```c
id = uart_pci_match(dev, pci_ns8250_ids);
if ((id == NULL || (id->rid & PCI_NO_MSI) == 0) &&
    pci_msi_count(dev) == 1) {
	count = 1;
	if (pci_alloc_msi(dev, &count) == 0) {
		sc->sc_irid = 1;
		device_printf(dev, "Using %d MSI message\n", count);
	}
}
```

Este fragmento asigna MSI si el dispositivo lo admite y el driver no ha marcado la entrada con `PCI_NO_MSI`. La llamada `pci_msi_count(dev)` devuelve el número de vectores MSI que anuncia el dispositivo; `pci_alloc_msi` los asigna. La línea `sc->sc_irid = 1` refleja el rid asignado al recurso MSI (los recursos MSI comienzan en rid 1; las IRQs heredadas usan rid 0).

Por qué importa: MSI es preferible a las IRQs heredadas en los sistemas modernos porque evita los problemas de compartición de IRQ del pin INTx. Un driver que admite MSI y recurre a las IRQs heredadas cuando MSI no está disponible es el patrón correcto. El capítulo 20 trata MSI con detalle; el fragmento aquí es un anticipo.

### Patrón: Liberación de IRQ en el detach

De `/usr/src/sys/dev/uart/uart_bus_pci.c`:

```c
static int
uart_pci_detach(device_t dev)
{
	struct uart_softc *sc;

	sc = device_get_softc(dev);

	if (sc->sc_irid != 0)
		pci_release_msi(dev);

	return (uart_bus_detach(dev));
}
```

El detach libera MSI (si se asignó) y delega el resto en `uart_bus_detach`. La comprobación `sc->sc_irid != 0` protege contra llamar a `pci_release_msi` en un driver que usó IRQs heredadas; liberar MSI cuando no fue asignado es un error.

Por qué importa: todo recurso asignado en el attach debe liberarse en el detach. El driver lleva la cuenta de lo que asignó a través de su estado (aquí, `sc_irid != 0` significa que se usó MSI) y lo libera en consecuencia. Los capítulos 19 y 20 extenderán el detach del capítulo 18 con un patrón similar.

### Patrón: Lectura de campos de configuración específicos del fabricante

De `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c` (simplificado):

```c
cap_offset = 0;
while (pci_find_next_cap(dev, PCIY_VENDOR, cap_offset, &cap_offset) == 0) {
	uint8_t cap_type = pci_read_config(dev,
	    cap_offset + VIRTIO_PCI_CAP_TYPE, 1);
	if (cap_type == VIRTIO_PCI_CAP_COMMON_CFG) {
		/* This is the capability we're looking for. */
		break;
	}
}
```

Este código recorre todas las capacidades específicas del fabricante en la lista (ID = `PCIY_VENDOR` = `0x09`), comprobando el byte de tipo definido por el fabricante de cada una hasta encontrar la que busca el driver. La función `pci_find_next_cap` es la versión iterativa de `pci_find_cap`, que continúa donde quedó la llamada anterior.

Por qué importa: cuando varias capacidades comparten el mismo ID (como ocurre con las capacidades específicas del fabricante de virtio), el driver debe recorrerlas y distinguirlas leyendo el campo de tipo de cada una. La función `pci_find_next_cap` existe específicamente para este caso.

### Patrón: Un manejador de resume con gestión de energía

De varios drivers:

```c
static int
myfirst_pci_resume(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	/* Restore the device to its pre-suspend state. */
	MYFIRST_LOCK(sc);
	CSR_WRITE_4(sc, MYFIRST_REG_CTRL, sc->saved_ctrl);
	CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, sc->saved_intr_mask);
	MYFIRST_UNLOCK(sc);

	/* Re-enable the user-space interface. */
	return (0);
}
```

Un manejador de suspend guarda el estado del dispositivo; el manejador de resume lo restaura. El patrón es importante para los sistemas que admiten suspensión a RAM (S3) o suspensión a disco (S4); un driver que no implementa suspend y resume impide que el sistema entre en esos estados.

El driver del capítulo 18 no implementa suspend ni resume. El capítulo 22 los añade.

### Patrón: Respuesta a un estado de error específico del dispositivo

De `/usr/src/sys/dev/e1000/if_em.c`:

```c
if (reg_icr & E1000_ICR_RXO)
	sc->rx_overruns++;
if (reg_icr & E1000_ICR_LSC)
	em_handle_link(ctx);
if (reg_icr & E1000_ICR_INT_ASSERTED) {
	/* ... */
}
```

Tras una interrupción, el driver lee el registro de causa de interrupción (`reg_icr`) y despacha según los bits que estén activos. Cada bit corresponde a un evento diferente: desbordamiento de recepción, cambio de estado del enlace, interrupción general. El driver toma una acción diferente para cada uno.

Por qué importa: un driver real gestiona muchos tipos de eventos. El patrón de despacho resulta familiar a partir de la inyección de fallos del capítulo 17, donde la simulación podía inyectar diferentes tipos de fallos. El capítulo 19 introducirá la versión de este patrón para el manejo de interrupciones.

### Patrón: Uso de sysctl para exponer la configuración del driver

De cualquier número de drivers:

```c
SYSCTL_ADD_U32(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO,
    "max_retries", CTLFLAG_RW,
    &sc->max_retries, 0,
    "Maximum retry attempts");
```

Los drivers exponen parámetros ajustables a través de sysctl. El parámetro puede leerse o escribirse desde el espacio de usuario con `sysctl dev.myfirst.0.max_retries`. Un driver que expone un puñado de tales parámetros da a sus operadores una forma de ajustar el comportamiento sin recompilar el driver.

Por qué importa: sysctl es el lugar adecuado para los parámetros ajustables por driver. Las opciones de línea de comandos del kernel (parámetros establecidos en el momento del boot) son exclusivamente para parámetros de la fase inicial del arranque; el ajuste en tiempo de ejecución se hace a través de sysctl.

### Patrón: Registro de funcionalidades admitidas en una estructura de capacidades

De `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`:

```c
sc->vtpci_modern_res.vtprm_common_cfg_cap_off = common_cfg_off;
sc->vtpci_modern_res.vtprm_notify_cap_off = notify_off;
sc->vtpci_modern_res.vtprm_isr_cfg_cap_off = isr_cfg_off;
sc->vtpci_modern_res.vtprm_device_cfg_cap_off = device_cfg_off;
```

El driver almacena los offsets de cada capacidad en una estructura de estado por dispositivo. El código posterior que necesite acceder a los registros de una capacidad los alcanza a través del offset almacenado.

Por qué importa: tras el attach, el driver no debería necesitar recorrer de nuevo la lista de capacidades. Guardar los offsets en el momento del attach ahorra un recorrido en cada acceso. El driver del capítulo 18 recorre las capacidades con fines informativos pero no almacena los offsets porque no los utiliza. Un driver real que necesita una capacidad almacena su offset.

### Resumen de patrones

Los patrones anteriores son la moneda común de los drivers PCI de FreeBSD. Un lector que los reconoce en código desconocido es un lector que puede aprender de cualquier driver del árbol. El capítulo 18 enseñó los patrones base; los drivers reales añaden variaciones específicas encima. Las variaciones específicas son siempre pequeñas (una coincidencia por clase aquí, una asignación MSI allá); los patrones base son los que se repiten.

Tras terminar el capítulo 18 y los laboratorios, elige un driver de `/usr/src/sys/dev/` que te resulte interesante (quizás para un dispositivo que tengas, o simplemente uno cuyo nombre reconozcas) y lee su PCI attach. Usa esta sección como lista de comprobación: ¿qué patrones usa el driver? ¿Cuáles omite? ¿Por qué? Un autor de drivers que haya realizado este ejercicio tres o cuatro veces con diferentes drivers habrá construido un enorme caudal de reconocimiento de patrones.



## Referencia: Una nota final sobre la filosofía del driver PCI

Un párrafo con el que cerrar el capítulo, que vale la pena releer tras los laboratorios.

El trabajo de un driver PCI no es entender el dispositivo. El trabajo de un driver PCI es presentar el dispositivo al kernel en una forma que el kernel pueda usar. La comprensión del dispositivo (qué significan sus registros, qué protocolo habla, qué invariantes mantiene) pertenece a las capas superiores del driver: la abstracción de hardware, la implementación del protocolo, la interfaz con el espacio de usuario. La capa PCI es algo estrecho. Coincide con un ID de fabricante y de dispositivo. Reclama un BAR. Entrega el BAR a las capas superiores. Registra un manejador de interrupciones. Cede el control a las capas superiores. Existe para conectar dos mitades de la identidad del driver: la mitad del dispositivo, que pertenece al hardware, y la mitad del software, que pertenece al kernel.

Un lector que ha escrito el driver del capítulo 18 ha escrito una capa PCI. Es pequeña. El resto del driver es lo que la hace útil. En el capítulo 19, la capa PCI del driver ganará una responsabilidad más (el registro de interrupciones). En el capítulo 20 ganará MSI y MSI-X. En los capítulos 20 y 21 gestionará DMA tags. Cada uno de estos es una extensión estrecha del rol existente de la capa PCI. Ninguno cambia el carácter fundamental de la capa PCI.

Para este lector y para los futuros lectores de este libro, la capa PCI del capítulo 18 es una pieza permanente de la arquitectura del driver `myfirst`. Todos los capítulos posteriores la dan por supuesta. Todos los capítulos posteriores la extienden. La complejidad global del driver crecerá, pero la capa PCI seguirá siendo lo que el capítulo 18 hizo de ella: un conector entre el dispositivo y el resto del driver, pequeño y predecible.

La habilidad que enseña el capítulo 18 no es «cómo escribir un driver para virtio-rnd». Es «cómo conectar un driver a un dispositivo PCI, independientemente de qué sea el dispositivo». Esa habilidad es transferible, y es la que te servirá en cada driver PCI que escribas.



## Referencia: Tarjeta de referencia rápida del capítulo 18

Un resumen compacto del vocabulario, las APIs, las macros y los procedimientos que introdujo el capítulo 18. Útil como repaso de una sola página mientras se trabaja en el capítulo 19 y los capítulos posteriores.

### Vocabulario

- **PCI**: Peripheral Component Interconnect, el bus paralelo compartido introducido por Intel a principios de la década de 1990.
- **PCIe**: PCI Express, el sucesor serie moderno. El mismo modelo visible por software que PCI.
- **B:D:F**: Bus, Device, Function. La dirección de un dispositivo PCI. Se escribe `pciN:B:D:F` en la salida de FreeBSD.
- **Configuration space**: la pequeña área de metadatos que expone cada dispositivo PCI. 256 bytes en PCI, 4096 bytes en PCIe.
- **BAR**: Base Address Register. Un campo del configuration space donde el dispositivo anuncia el rango de direcciones que necesita.
- **Vendor ID**: identificador de 16 bits asignado por el PCI-SIG a un fabricante.
- **Device ID**: identificador de 16 bits asignado por el fabricante a un producto específico.
- **Subvendor/subsystem ID**: tupla secundaria de 16+16 bits que identifica la tarjeta.
- **Capability list**: lista enlazada de bloques de características opcionales en el configuration space.
- **Extended capability list**: la lista específica de PCIe, que comienza en el offset `0x100`.

### API esenciales

- `pci_get_vendor(dev)` / `pci_get_device(dev)`: leen los campos de identificación en caché.
- `pci_get_class(dev)` / `pci_get_subclass(dev)` / `pci_get_progif(dev)` / `pci_get_revid(dev)`: leen los campos de clasificación en caché.
- `pci_get_subvendor(dev)` / `pci_get_subdevice(dev)`: leen la identificación de subsistema en caché.
- `pci_read_config(dev, offset, width)` / `pci_write_config(dev, offset, val, width)`: acceso directo al espacio de configuración (anchura 1, 2 o 4).
- `pci_find_cap(dev, cap, &offset)` / `pci_find_next_cap(dev, cap, start, &offset)`: recorren la lista de capacidades heredada.
- `pci_find_extcap(dev, cap, &offset)` / `pci_find_next_extcap(dev, cap, start, &offset)`: recorren la lista de capacidades extendidas de PCIe.
- `pci_enable_busmaster(dev)` / `pci_disable_busmaster(dev)`: activan y desactivan el bit de habilitación de bus master.
- `pci_msi_count(dev)` / `pci_msix_count(dev)`: informan del número de vectores MSI y MSI-X disponibles.
- `pci_alloc_msi(dev, &count)` / `pci_alloc_msix(dev, &count)`: asignan vectores MSI o MSI-X (capítulo 20).
- `pci_release_msi(dev)`: libera los recursos MSI o MSI-X.
- `bus_alloc_resource_any(dev, type, &rid, flags)`: reserva un recurso (BAR, IRQ, etc.).
- `bus_release_resource(dev, type, rid, res)`: libera un recurso previamente reservado.
- `rman_get_bustag(res)` / `rman_get_bushandle(res)`: extraen el tag y el handle de `bus_space`.
- `rman_get_start(res)` / `rman_get_size(res)` / `rman_get_end(res)`: inspeccionan el rango de un recurso.
- `bus_space_read_4(tag, handle, off)` / `bus_space_write_4(tag, handle, off, val)`: los accesores de bajo nivel.
- `bus_read_4(res, off)` / `bus_write_4(res, off, val)`: forma abreviada basada en recursos.

### Macros esenciales

- `DEVMETHOD(device_probe, probe_fn)` y similares: rellenan una tabla de métodos.
- `DEVMETHOD_END`: termina una tabla de métodos.
- `DRIVER_MODULE(name, bus, driver, modev_fn, modev_arg)`: registra un driver contra un bus.
- `MODULE_DEPEND(name, dep, minver, prefver, maxver)`: declara una dependencia de módulo.
- `MODULE_VERSION(name, version)`: declara la versión del driver.
- `PCIR_BAR(n)`: calcula el desplazamiento en el espacio de configuración del BAR `n`.
- `BUS_PROBE_DEFAULT`, `BUS_PROBE_GENERIC`, `BUS_PROBE_VENDOR`, `BUS_PROBE_SPECIFIC`: valores de prioridad para probe.
- `SYS_RES_MEMORY`, `SYS_RES_IOPORT`, `SYS_RES_IRQ`: tipos de recursos.
- `RF_ACTIVE`, `RF_SHAREABLE`: indicadores de asignación de recursos.

### Procedimientos habituales

**Vincular un driver PCI a un identificador de dispositivo concreto:**

1. Escribe un probe que lea `pci_get_vendor(dev)` y `pci_get_device(dev)`, compare contra una tabla, devuelva `BUS_PROBE_DEFAULT` si hay coincidencia y `ENXIO` en caso contrario.
2. Escribe un attach que llame a `bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE)` con `rid = PCIR_BAR(0)`.
3. Extrae el tag y el handle con `rman_get_bustag` y `rman_get_bushandle`.
4. Guárdalos donde la capa de acceso pueda encontrarlos.

**Liberar un recurso PCI durante el detach:**

1. Vacía cualquier callout o tarea que pudiera acceder al recurso.
2. Libera el recurso con `bus_release_resource(dev, type, rid, res)`.
3. Pon el puntero al recurso almacenado a NULL.

**Descargar un driver del sistema base que entre en conflicto antes de cargar el tuyo propio:**

```sh
sudo kldunload virtio_random   # or whatever driver owns the device
sudo kldload ./myfirst.ko
```

**Forzar que un dispositivo se vincule de un driver a otro:**

```sh
sudo devctl detach <driver0_name>
sudo devctl set driver -f <pci_selector> <new_driver_name>
```

### Comandos útiles

- `pciconf -lv`: lista todos los dispositivos PCI con sus identificadores, clase y vinculación de driver.
- `pciconf -r <selector> <offset>:<length>`: vuelca los bytes del espacio de configuración.
- `pciconf -w <selector> <offset> <value>`: escribe un valor en el espacio de configuración.
- `devinfo -v`: vuelca el árbol de newbus con recursos y vinculaciones.
- `devctl detach`, `attach`, `disable`, `enable`, `rescan`: controlan las vinculaciones del bus en tiempo de ejecución.
- `dmesg`, `dmesg -w`: visualizan (y siguen en tiempo real) el buffer de mensajes del kernel.
- `kldstat -v`: lista los módulos cargados con información detallada.
- `kldload`, `kldunload`: cargan y descargan módulos del kernel.
- `vmstat -m`: informa de las asignaciones de memoria por tipo de malloc.

### Archivos que conviene tener a mano

- `/usr/src/sys/dev/pci/pcireg.h`: definiciones de registros PCI (`PCIR_*`, `PCIM_*`, `PCIY_*`, `PCIZ_*`).
- `/usr/src/sys/dev/pci/pcivar.h`: declaraciones de las funciones de acceso PCI.
- `/usr/src/sys/sys/bus.h`: macros de métodos y recursos de newbus.
- `/usr/src/sys/sys/rman.h`: accesores del gestor de recursos.
- `/usr/src/sys/sys/module.h`: macros de registro de módulos.
- `/usr/src/sys/dev/uart/uart_bus_pci.c`: un ejemplo de driver PCI limpio y legible.
- `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`: un ejemplo de transporte moderno.



## Referencia: glosario de términos del capítulo 18

Un glosario breve para los lectores que quieran un recordatorio compacto del vocabulario del capítulo 18.

**AER (Advanced Error Reporting)**: una capacidad extendida de PCIe que notifica al sistema operativo los errores de la capa de transacciones.

**Attach**: el método de newbus que un driver implementa para tomar posesión de una instancia de dispositivo concreta. Se invoca una vez por dispositivo, tras el éxito de probe.

**Bar (Base Address Register)**: un campo del espacio de configuración donde un dispositivo anuncia un rango de direcciones que necesita que se le mapee.

**Bus Master**: un dispositivo que inicia sus propias transacciones en el bus PCI. Necesario para DMA. Se habilita mediante el bit `BUSMASTEREN` del registro de comando.

**Capability**: un bloque de funcionalidad opcional dentro del espacio de configuración. Se descubre recorriendo la lista de capacidades.

**Class code**: una clasificación de tres bytes (clase, subclase, interfaz de programación) que categoriza la función del dispositivo.

**cdev**: un nodo de dispositivo de caracteres en `/dev/`, creado por `make_dev(9)`.

**Configuration space**: el área de metadatos por dispositivo. 256 bytes en PCI, 4096 bytes en PCIe.

**Detach**: el método de newbus que deshace todo lo que hizo attach. Se invoca una vez por dispositivo, cuando se desvincula el driver.

**device_t**: el handle opaco que la capa de newbus pasa a los métodos del driver.

**DRIVER_MODULE**: una macro que registra un driver contra un bus y lo empaqueta como módulo del kernel.

**ENXIO**: el errno que devuelve probe para indicar "no soy compatible con este dispositivo".

**EBUSY**: el errno que devuelve detach para indicar "me niego a desconectarme; el driver está en uso".

**IRQ**: una solicitud de interrupción. En PCI, el campo `PCIR_INTLINE` del espacio de configuración contiene el número de IRQ heredado.

**Interrupción heredada (INTx)**: el mecanismo de interrupción basado en pines heredado de PCI. Sustituido por MSI y MSI-X en los sistemas modernos.

**MMIO (Memory-Mapped I/O)**: el patrón de acceso a los registros de dispositivo mediante instrucciones de carga y almacenamiento similares a las de memoria.

**MSI / MSI-X**: Message Signaled Interrupts; mecanismos de interrupción que utilizan escrituras en direcciones de memoria específicas en lugar de señales de pin. Capítulo 20.

**Newbus**: la abstracción del árbol de dispositivos de FreeBSD. Cada dispositivo tiene un bus padre y un driver.

**PCI**: el antiguo estándar de bus paralelo.

**PCIe**: el sucesor serie moderno de PCI. Compatible con PCI a nivel de software.

**PIO (Port-Mapped I/O)**: el patrón de acceso a los registros de dispositivo mediante las instrucciones `in` y `out` de x86. En gran medida obsoleto.

**Probe**: el método de newbus que comprueba si el driver puede gestionar un dispositivo concreto. Debe ser idempotente.

**Resource**: el nombre genérico para un recurso de dispositivo gestionado por el kernel (rango de memoria, rango de puertos de I/O, IRQ). Se asigna mediante `bus_alloc_resource_any(9)`.

**Softc (software context)**: la estructura de estado por dispositivo que mantiene un driver. Su tamaño se define en `driver_t` y la asigna newbus.

**Subclass**: el byte intermedio del class code; refina la clase.

**Subvendor / subsystem ID**: una tupla de identificación de segundo nivel que refina el par vendedor/dispositivo primario para diseños de placa distintos.

**Vendor ID**: el identificador de fabricante de 16 bits asignado por el PCI-SIG.
