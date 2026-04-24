---
title: "DMA y transferencia de datos de alta velocidad"
description: "El capítulo 21 amplía el driver multi-vector del capítulo 20 con soporte para acceso directo a memoria a través de la interfaz bus_dma(9) de FreeBSD. Enseña qué es DMA a nivel de hardware; por qué el acceso directo de un dispositivo a la RAM no es seguro sin una capa de abstracción; cómo las etiquetas, los mapas, las asignaciones de memoria y la sincronización de bus_dma trabajan juntos para hacer DMA portable entre arquitecturas; cómo asignar memoria DMA coherente y cargarla a través de un callback de mapeo; cómo los pares de sincronización PRE/POST mantienen de forma coherente la vista de memoria del CPU y del dispositivo; cómo ampliar el backend simulado con un motor DMA que acepta una dirección física, ejecuta la transferencia bajo un callout y lanza una interrupción de finalización; cómo consumir las finalizaciones en el pipeline filter-plus-task del capítulo 20; cómo recuperarse de fallos de mapeo, buffers no alineados, timeouts y transferencias parciales; y cómo refactorizar el código DMA en su propio archivo y documentarlo. El driver evoluciona de 1.3-msi a 1.4-dma, incorpora myfirst_dma.c y myfirst_dma.h, incorpora un documento DMA.md, y deja el capítulo 21 preparado para el trabajo de gestión de energía del capítulo 22."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 21
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 225
language: "es-ES"
---
# DMA y transferencia de datos de alta velocidad

## Orientación para el lector y resultados de aprendizaje

El capítulo 20 cerró con un driver que gestiona las interrupciones correctamente. El módulo `myfirst` en la versión `1.3-msi` cuenta con una escalera de tres niveles de reserva (MSI-X primero, luego MSI y, por último, INTx legacy); tres manejadores de filtro por vector asignados a roles distintos (administración, recepción y transmisión); contadores por vector; vinculación por vector a CPU mediante `bus_bind_intr(9)`; un desmontaje limpio que libera cada vector en orden inverso; un nuevo archivo `myfirst_msix.c` que mantiene ordenado el código multi-vector; y un documento `MSIX.md` que describe el diseño por vector. El manejador de cada vector se ejecuta con la disciplina del capítulo 19: un filtro corto en el contexto de interrupción primaria que lee el estado, realiza el reconocimiento y, o bien lo gestiona o bien lo difiere; y una tarea en el contexto de thread que realiza el grueso del trabajo bajo el sleep-mutex.

Lo que el driver todavía no hace es mover datos. Cada byte que el dispositivo ha producido hasta ahora se ha leído registro a registro desde el punto de vista del CPU. El dispositivo simulado escribe una palabra en `DATA_OUT`; el CPU la lee con `bus_read_4`. Eso funciona cuando hay una palabra que leer. No funciona cuando hay sesenta y cuatro descriptores de recepción que recorrer, o cuando un controlador de almacenamiento ha completado una transferencia de cuatro kilobytes, o cuando una NIC acaba de colocar una trama jumbo de nueve kilobytes en la memoria del sistema. A esas velocidades, leer palabra a palabra hace colapsar el rendimiento que el hardware fue diseñado para entregar. El dispositivo necesita acceder directamente a la RAM por sí mismo, escribir los datos allí y luego indicarle al driver dónde encontrarlos. Eso es lo que significa DMA, y eso es lo que enseña el capítulo 21.

El alcance del capítulo es exactamente esta transición: qué es DMA a nivel del tejido PCIe; por qué un dispositivo que escribe en memoria arbitraria sería una pesadilla de corrección y seguridad sin una capa de abstracción; cómo la interfaz `bus_dma(9)` de FreeBSD proporciona esa capa de forma portable; cómo crear etiquetas DMA que describan las restricciones de direccionamiento de un dispositivo; cómo asignar memoria con capacidad DMA y cargarla en un mapeo; cómo sincronizar la vista del CPU con la vista del dispositivo en los momentos adecuados; cómo extender el backend simulado del capítulo 17 con un motor DMA; cómo procesar las interrupciones de finalización a través del camino por vector del capítulo 20; cómo recuperarse de los fallos que el código DMA real debe manejar; y cómo refactorizar todo esto en un archivo que el lector pueda leer, probar y extender. El capítulo se detiene antes de los subsistemas que se construyen sobre DMA. El capítulo de drivers de red de la Parte 6 (capítulo 28) vuelve a tratar el DMA dentro del framework `iflib(9)`; el capítulo de gestión de energía que le sigue (capítulo 22) enseñará a apagar el DMA de forma segura durante la suspensión; el capítulo de rendimiento de la Parte 7 (capítulo 33) retomará el DMA con una perspectiva de ajuste. El capítulo 21 se centra en los primitivos que todos los capítulos posteriores presuponen.

El capítulo 21 se detiene antes de varios caminos de carga que se basan en el mismo fundamento. El DMA scatter-gather para cadenas mbuf (`bus_dmamap_load_mbuf_sg`), los bloques de control CAM (`bus_dmamap_load_ccb`), las estructuras de I/O de usuario (`bus_dmamap_load_uio`) y las operaciones de criptografía (`bus_dmamap_load_crp`) utilizan el mismo mecanismo subyacente y la misma disciplina de sincronización, pero cada uno viene con su propio contexto (redes, almacenamiento, VFS, OpenCrypto) que pertenece a su propio capítulo. El DMA de espacio de usuario de copia cero, en el que un driver permite que el espacio de usuario mapee un buffer DMA y coordine su sincronización mediante `sys_msync(2)` u otros primitivos similares, queda fuera del alcance del capítulo 21; la sección 8 del capítulo 10 introdujo el camino `d_mmap(9)` sobre el que se construiría, y el capítulo 31 analiza las implicaciones de seguridad de exponer memoria del kernel a través de interfaces tipo `mmap(2)`. El remapeo asistido por IOMMU (`busdma_iommu`) se menciona a modo de contexto pero no se configura manualmente. El lector que termine el capítulo 21 entenderá los primitivos lo suficientemente bien como para que los caminos de carga especializados parezcan variaciones del caso base; ese es el objetivo.

La historia de `bus_dma(9)` se asienta sobre cada capa de la Parte 4 que hemos construido. El capítulo 16 dotó al driver de un vocabulario de acceso a registros. El capítulo 17 le enseñó a pensar como un dispositivo. El capítulo 18 lo introdujo a un dispositivo PCI real. El capítulo 19 le dio oídos en una IRQ. El capítulo 20 le dio varios oídos, uno por cada cola que el dispositivo quiere. El capítulo 21 le da manos: la capacidad de entregarle al dispositivo una dirección física y decirle «pon tus datos aquí, avísame cuando hayas terminado y déjame procesarlos sin molestarte por cada byte». Ese es el último primitivo que falta antes de que se cierre la Parte 4.

### Por qué bus_dma(9) merece un capítulo propio

Antes de continuar, merece la pena detenerse en lo que `bus_dma(9)` nos ofrece y que un bucle de llamadas a `bus_read_4` no puede. El driver del capítulo 20 tiene una cadena de interrupciones completa. Si el patrón de filtro más tarea ya es correcto, y la capa de acceso a registros ya gestiona lecturas y escrituras correctamente, ¿por qué no leer bloques más grandes palabra a palabra y dejarlo así?

Tres razones.

La primera es el **ancho de banda**. Una sola llamada a `bus_read_4` es una transacción MMIO en el bus PCIe, y cada transacción cuesta cientos de nanosegundos una vez que se tienen en cuenta la sobrecarga de la decodificación de direcciones, el tiempo de ida y vuelta de la confirmación de lectura y el orden de memoria del CPU. En un enlace PCIe 3.0 x4, un driver que usa `bus_read_4` palabra a palabra llega como máximo a unos veinte megabytes por segundo de rendimiento efectivo; un driver que hace que el dispositivo transfiera por DMA a un buffer contiguo alcanza gigabytes por segundo en el mismo enlace. La diferencia de un orden de magnitud es lo que separa una NIC de diez gigabits de una NIC inutilizable, un NVMe moderno de un controlador IDE de finales de los noventa. El DMA no es una optimización para drivers que podrían funcionar sin él; es el único mecanismo que permite a los dispositivos modernos entregar el rendimiento para el que fueron diseñados.

La segunda es el **coste de CPU**. Cada llamada a `bus_read_4` o `bus_write_4` mantiene ocupado a un CPU durante el tiempo que dura la transacción. Un driver que mueve un megabyte de datos una palabra a la vez consume entre diez y veinte milisegundos de tiempo de CPU solo en trasladar bytes por MMIO. El DMA delega ese coste al motor bus master propio del dispositivo: el CPU proporciona una dirección y una longitud, el dispositivo ejecuta la transferencia de forma independiente y el CPU queda libre para realizar otro trabajo, incluida la gestión de interrupciones de otros dispositivos. En un servidor que procesa millones de paquetes por segundo a través de varias NICs a la vez, el CPU no puede permitirse tocar cada byte; el DMA es lo que hace posible el rendimiento agregado.

La tercera es la **corrección bajo concurrencia**. Un driver que lee un anillo de descriptores palabra a palabra está compitiendo contra el dispositivo: el dispositivo puede estar escribiendo nuevas entradas mientras el driver lee las antiguas, y el driver ve lecturas truncadas de campos a medio actualizar a menos que tome un lock global que serialice toda la transferencia. El DMA con la sincronización adecuada reemplaza esa condición de carrera por un protocolo limpio de productor-consumidor: el dispositivo escribe entradas en orden, señala la finalización mediante una sola escritura en un registro o una interrupción de finalización, y el CPU procesa las entradas como un lote con la garantía de que cada byte está disponible. La llamada a `bus_dmamap_sync` hace explícita la cesión; la llamada a `bus_dmamap_unload` hace explícita la limpieza. El driver resulta más fácil de razonar, no más difícil, aunque el mecanismo sea más sofisticado.

El capítulo 21 se gana su lugar enseñando los tres beneficios de forma concreta. Un lector termina el capítulo siendo capaz de crear una etiqueta, asignar un buffer, cargarlo en un mapa, desencadenar una transferencia, sincronizarla, esperar la finalización, verificar el resultado y desmontar todo. Con esos primitivos en mano, el lector puede abrir cualquier driver FreeBSD con capacidad DMA y reconocer su estructura, del mismo modo que el graduado del capítulo 20 puede leer cualquier driver multi-vector.

### Dónde dejó el capítulo 20 al driver

Algunos requisitos previos que verificar antes de comenzar. El capítulo 21 extiende el driver producido al final de la etapa 4 del capítulo 20, etiquetado como versión `1.3-msi`. Si alguno de los puntos siguientes resulta incierto, regresa al capítulo 20 antes de empezar este.

- Tu driver compila sin errores y se identifica como `1.3-msi` en `kldstat -v`.
- En un guest QEMU que expone `virtio-rng-pci` con MSI-X, el driver se conecta; elige MSI-X; asigna tres vectores (admin, rx, tx); registra un filtro distinto por vector; vincula cada vector a un CPU; imprime un banner `interrupt mode: MSI-X, 3 vectors`; y crea `/dev/myfirst0`.
- En un guest bhyve con `virtio-rnd`, el driver se conecta; cae a MSI con un solo vector, o más abajo aún a INTx legacy; imprime el banner correspondiente.
- Los contadores por vector (`dev.myfirst.0.vec0_fire_count` hasta `vec2_fire_count`) se incrementan cuando se escribe en el sysctl de simulación correspondiente (`dev.myfirst.0.intr_simulate_admin`, `intr_simulate_rx` o `intr_simulate_tx`).
- El camino de detach desmonta los vectores en orden inverso, drena las tareas por vector, libera los recursos y llama a `pci_release_msi` exactamente una vez.
- `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md` y `MSIX.md` están actualizados en tu árbol de trabajo.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` están habilitados en tu kernel de pruebas.

Ese driver es el que extiende el capítulo 21. Las adiciones son notables en alcance: un nuevo archivo (`myfirst_dma.c`), una nueva cabecera (`myfirst_dma.h`), varios nuevos campos en el softc para rastrear la etiqueta DMA, el mapa, el puntero de memoria y el estado simulado del motor DMA; un nuevo par de funciones auxiliares (`myfirst_dma_setup` y `myfirst_dma_teardown`); un nuevo camino de finalización en el filtro de recepción; una actualización de versión a `1.4-dma`; un nuevo documento `DMA.md`; y actualizaciones a la prueba de regresión. El modelo mental también crece: el driver empieza a pensar en la propiedad de la memoria como algo que pasa de un lado a otro entre el CPU y el dispositivo, y cada cesión se convierte en una llamada deliberada a `bus_dmamap_sync`.

### Qué aprenderás

Tras terminar este capítulo serás capaz de:

- Describir qué es DMA a nivel de hardware, cómo un dispositivo PCIe realiza una escritura bus-master en la memoria del host y por qué ese mecanismo ofrece ventajas de ancho de banda y descarga de CPU que MMIO no puede igualar.
- Explicar por qué el acceso directo de un dispositivo a memoria arbitraria sería inseguro sin una capa de abstracción, y nombrar las tres realidades de hardware que esa capa debe ocultar (límites de direccionamiento del dispositivo, remapeo por IOMMU, coherencia de caché).
- Reconocer dónde encajan los bounce buffers en este panorama: cuándo el kernel los inserta de forma silenciosa, cuál es su coste y cuándo una restricción de dirección explícita puede evitarlos.
- Leer y escribir el vocabulario central de `bus_dma(9)`: `bus_dma_tag_t`, `bus_dmamap_t`, `bus_dma_segment_t`, las operaciones de sincronización PRE/POST, las restricciones de la tag (`alignment`, `boundary`, `lowaddr`, `highaddr`, `maxsize`, `nsegments`, `maxsegsz`) y el conjunto de flags habituales (`BUS_DMA_WAITOK`, `BUS_DMA_NOWAIT`, `BUS_DMA_COHERENT`, `BUS_DMA_ZERO`).
- Crear una DMA tag con ámbito de dispositivo mediante `bus_dma_tag_create`, heredando las restricciones del bridge padre a través de `bus_get_dma_tag(9)`, y eligiendo los límites de alineación, boundary y dirección que coincidan con el datasheet del dispositivo.
- Asignar memoria apta para DMA con `bus_dmamem_alloc`, obtener su dirección virtual en el kernel y comprender por qué el asignador devuelve un único segmento con `bus_dmamem_alloc` pero puede devolver varios segmentos para memoria arbitraria cargada posteriormente.
- Cargar un buffer del kernel en un mapa DMA con `bus_dmamap_load`, extraer la dirección de bus del callback de segmento único y comprender los casos en que el callback se difiere y qué implica eso para la disciplina de locking del driver.
- Usar `bus_dmamap_sync` con los flags PRE/POST correctos alrededor de cada acceso a memoria visible por el dispositivo: PREWRITE antes de que el dispositivo lea, POSTREAD después de que el dispositivo escriba, y los pares combinados para anillos de descriptores en que ocurren ambas direcciones.
- Extender el backend simulado del Capítulo 17 con un pequeño motor DMA que tome una dirección de bus, una longitud y una dirección de transferencia del softc, ejecute la transferencia bajo un `callout(9)` para emular la latencia y genere una interrupción de finalización a través del camino de filtro de los Capítulos 19 y 20.
- Procesar las interrupciones de finalización leyendo un registro de estado dentro del filtro rx, reconociéndolo, encolando la tarea y dejando que la tarea ejecute `bus_dmamap_sync` y acceda al buffer.
- Recuperarse de cada modo de fallo recuperable: `bus_dma_tag_create` devolviendo `ENOMEM`, `bus_dmamap_load` devolviendo `EINVAL` o `EFBIG`, el motor informando de una transferencia parcial, un timeout que expira antes de que el dispositivo señalice la finalización y un detach que se dispara mientras hay una transferencia en curso.
- Refactorizar el código DMA en un par de archivos dedicado `myfirst_dma.c` / `myfirst_dma.h`, con `myfirst_dma_setup` y `myfirst_dma_teardown` como los únicos puntos de entrada utilizados por el resto del driver.
- Versionar el driver como `1.4-dma`, actualizar la línea `SRCS` del Makefile, ejecutar el test de regresión ampliado y generar `DMA.md` documentando el flujo DMA, los layouts de buffer y los contadores observables.
- Leer el código DMA de un driver real (`/usr/src/sys/dev/re/if_re.c` es la referencia continua del capítulo) y relacionar cada llamada con los conceptos presentados en el Capítulo 21.

La lista es larga; cada elemento es concreto. El propósito del capítulo es la composición.

### Lo que este capítulo no cubre

Varios temas adyacentes se posponen explícitamente para que el Capítulo 21 permanezca centrado.

- **DMA scatter-gather para buffers heterogéneos.** `bus_dmamap_load_mbuf_sg` (redes), `bus_dmamap_load_ccb` (almacenamiento CAM), `bus_dmamap_load_uio` (VFS) y `bus_dmamap_load_crp` (OpenCrypto) se asientan sobre la misma base de `bus_dma(9)`, pero se usan de maneras específicas a cada subsistema. La Parte 6 (Capítulo 27, almacenamiento; Capítulo 28, redes) los trata en contexto. El Capítulo 21 usa `bus_dmamap_load` sobre un único buffer contiguo; el resto es una especialización.
- **iflib(9) y sus pools de DMA ocultos.** El framework de redes envuelve `bus_dma` con funciones auxiliares por cola que asignan, cargan y sincronizan automáticamente los anillos de recepción y transmisión. El framework es el tema de su propio capítulo en la Parte 6 (Capítulo 28); el Capítulo 21 enseña la capa en bruto que iflib usa internamente.
- **DMA asistido por IOMMU en amd64 con Intel VT-d o AMD-Vi.** El mecanismo `busdma_iommu` se integra de forma transparente con la API de `bus_dma`, de modo que un driver escrito para la ruta genérica se beneficia automáticamente del remapeo IOMMU cuando el kernel se compila con `DEV_IOMMU`. El capítulo menciona la presencia del IOMMU, explica qué hace y muestra cómo observarlo; no lo configura de forma manual.
- **Colocación de memoria DMA con conciencia de NUMA.** `bus_dma_tag_set_domain(9)` permite que un driver vincule las asignaciones de un tag a un dominio NUMA concreto. La función se nombra y menciona; la historia completa de la colocación es un tema de rendimiento de la Parte 7 (Capítulo 33).
- **Detención ordenada de DMA con conciencia de energía.** Detener las transferencias DMA en vuelo antes de la suspensión es el tema del Capítulo 22. El Capítulo 21 organiza las primitivas que el Capítulo 22 usará; la ruta `myfirst_dma_teardown` está diseñada para que el manejador de suspensión del Capítulo 22 pueda invocarla limpiamente.
- **DMA sin copia hacia y desde buffers en espacio de usuario.** Mapear páginas de usuario en un mapa DMA requiere `vm_fault_quick_hold_pages(9)` más `bus_dmamap_load_ma_triv` o un equivalente, y toca el anclaje de memoria, los conteos de referencia de VM y el cumplimiento de capacidades. Estos temas pertenecen a un capítulo posterior.
- **Anillos de descriptores DMA con registros de cabeza/cola de hardware.** Un diseño completo de anillo (índices productor/consumidor, desbordamiento circular, escrituras de doorbell) es un patrón de siguiente nivel construido sobre las primitivas del Capítulo 21. El capítulo muestra un patrón de transferencia única; los anillos de descriptores son una extensión natural que el lector puede construir como desafío.

Mantenerse dentro de esos límites hace que el Capítulo 21 sea un capítulo sobre primitivas DMA. El vocabulario es lo que se transfiere; los capítulos posteriores lo aplican a redes, almacenamiento, energía y rendimiento, por ese orden.

### Tiempo estimado de dedicación

- **Solo lectura**: cinco o seis horas. El modelo conceptual de DMA es el más denso de la Parte 4; la disciplina de sincronización en particular se beneficia de una lectura cuidadosa y de una segunda pasada una vez que los ejemplos de código lo hayan concretado.
- **Lectura más transcripción de los ejemplos resueltos**: de doce a quince horas en dos o tres sesiones. El driver evoluciona en cuatro etapas: tag y asignación, motor simulado con polling, motor simulado con finalización por interrupción y refactorización final. Cada etapa es pequeña, pero se apoya en la anterior, y cada una requiere una atención cuidadosa a los pares PRE/POST.
- **Lectura más todos los laboratorios y desafíos**: de dieciocho a veinticuatro horas en cuatro o cinco sesiones, incluyendo la lectura de drivers reales (`if_re.c`, el código de asignación de `nvme_qpair.c` y las fuentes de `busdma_bufalloc.c` si la curiosidad te lleva tan lejos), la ejecución de la prueba de regresión del capítulo en los destinos bhyve y QEMU, y el intento de uno o dos de los desafíos de estilo de producción.

Las secciones 3, 4 y 5 son las más densas. Si la disciplina de sincronización o las restricciones del tag resultan opacas en la primera pasada, eso es normal. Para, relee el diagrama de la Sección 4, ejecuta el ejercicio de la Sección 4 y continúa cuando la forma se haya asentado. DMA es uno de los temas en los que un modelo mental funcional rinde sus frutos muchas veces; vale la pena construirlo despacio.

### Requisitos previos

Antes de empezar este capítulo, comprueba:

- El código fuente de tu driver coincide con la Etapa 4 del Capítulo 20 (`1.3-msi`). El punto de partida asume todas las primitivas del Capítulo 20: la escalera de fallback de tres niveles, los filtros por vector, la vinculación de CPU por vector y el desmontaje limpio de múltiples vectores.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con `/usr/src` en disco y correspondiente al kernel en ejecución.
- Un kernel de depuración con `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` está compilado, instalado y arrancando limpiamente. La opción `WITNESS` es especialmente valiosa para el trabajo con DMA porque varias funciones de `bus_dma` no deben llamarse con locks no dormibles retenidos, y `WITNESS` detecta las violaciones de forma temprana.
- `bhyve(8)` o `qemu-system-x86_64` está disponible. Los laboratorios del Capítulo 21 funcionan en cualquiera de los dos destinos; la simulación DMA ocurre dentro del driver `myfirst`, por lo que no se requiere ningún dispositivo invitado específico con capacidad DMA. La comprobación de cordura de DMA real al final de la Sección 5 usa cualquier dispositivo PCI que el anfitrión virtio del capítulo exponga, sin depender de él para DMA.
- Las herramientas `devinfo(8)`, `vmstat(8)`, `pciconf(8)`, `sysctl(8)` y `procstat(1)` están en tu path. `procstat -kke` es útil para observar los threads del driver durante las transferencias DMA.

Si cualquiera de los puntos anteriores no está en orden, corrígelo ahora. DMA tiende a exponer cualquier debilidad latente en la disciplina de lock del driver (las llamadas de sincronización se encuentran dentro del contexto de la tarea, las llamadas de asignación dentro de attach y las de descarga dentro de detach; cada contexto tiene reglas de lock diferentes), y `WITNESS` en un kernel de depuración es lo que detecta los errores en tiempo de desarrollo, no en producción.

### Cómo sacar el máximo partido de este capítulo

Cuatro hábitos rendirán sus frutos rápidamente.

En primer lugar, ten marcados `/usr/src/sys/sys/bus_dma.h` y `/usr/src/share/man/man9/bus_dma.9`. La cabecera es compacta (unas cuatrocientas líneas) y enumera cada API que usa el capítulo, con un breve comentario sobre cada una. La página del manual es extensa (más de mil cien líneas) y es la referencia autoritativa sobre cada parámetro. Leer ambos una vez al inicio de la Sección 2 y volver a ellos mientras trabajas cada sección es lo más útil que puedes hacer para ganar soltura.

En segundo lugar, ten marcado `/usr/src/sys/dev/re/if_re.c` como ejemplo de driver real en funcionamiento. `if_re(4)` es un driver de red razonablemente compacto que usa `bus_dma` de manera ejemplar: un tag padre que hereda del puente PCI, un tag por anillo para los descriptores, un tag por buffer para las cargas útiles de mbuf, una callback de segmento único y un desmontaje limpio. La mayoría de los patrones del Capítulo 21 tienen un análogo directo en `if_re.c`. Cuando el capítulo muestra un patrón, con frecuencia dirá «mira `re_allocmem`» y dará una línea concreta.

> **Una nota sobre los números de línea.** Cuando el capítulo fija una referencia a una línea dentro de `if_re.c`, trata el número como un puntero al árbol de FreeBSD 14.3 en el momento de la escritura, no como una coordenada estable. `re_allocmem` y `re_dma_map_addr` son nombres duraderos; la línea en la que se encuentra cada uno, no. Abre el archivo, busca el nombre de la función y deja que tu editor te indique dónde vive realmente en el sistema que tienes delante.

En tercer lugar, escribe los cambios a mano y ejecuta cada etapa. DMA es donde el coste de un flag incorrecto o una sincronización omitida se paga en corrupción sutil en lugar de un fallo obvio, y escribir con cuidado hace visibles los errores comunes en el momento en que ocurren. La prueba de regresión del capítulo detectará muchos de los errores, pero la escritura en sí es lo que construye el modelo mental.

En cuarto lugar, después de terminar la Sección 4, relee las Secciones 2 y 3 del Capítulo 17. El mapa de registros MMIO del backend simulado es la base que extiende el motor DMA simulado, y ver los dos superpuestos es lo que hace que el diseño del motor parezca inevitable en lugar de arbitrario. La simulación del Capítulo 17 se construyó pensando en DMA; el Capítulo 21 es donde ese diseño da sus frutos.

### Mapa del capítulo

Las secciones en orden son:

1. **¿Qué es DMA y por qué usarlo?** La imagen hardware: escrituras de bus-master, el argumento de ancho de banda y coste de CPU, ejemplos concretos de dispositivos y por qué el desarrollo moderno de drivers asume DMA.
2. **Entendiendo la interfaz `bus_dma(9)` de FreeBSD.** La capa de abstracción: qué oculta la capa (límites de direccionamiento, IOMMU, coherencia), qué es un tag, qué es un map, qué es un segmento y cómo encajan las piezas. Primero los conceptos, luego el código.
3. **Asignación y mapeo de memoria DMA.** Los parámetros del tag en detalle, `bus_dmamem_alloc`, el patrón de callback de `bus_dmamap_load`, el caso de segmento único y el primer código en funcionamiento: la Etapa 1 del driver del Capítulo 21 (`1.4-dma-stage1`).
4. **Sincronización y uso de buffers DMA.** La disciplina de sincronización: PREWRITE, POSTREAD, los pares combinados, la distinción coherente frente a no coherente y el modelo mental del traspaso de propiedad de la memoria entre la CPU y el dispositivo. El ejercicio al final es un recorrido en papel antes de ningún código.
5. **Construcción de un motor DMA simulado.** Los registros DMA del backend simulado, la máquina de estados, la transferencia impulsada por `callout(9)` y la ruta de finalización. La Etapa 2 (`1.4-dma-stage2`) hace que el driver entregue una dirección física al motor y haga polling de la finalización.
6. **Manejo de la finalización de DMA en el driver.** La ruta filtro más tarea del Capítulo 19/20 consume la interrupción de finalización. La tarea realiza `bus_dmamap_sync(..., POSTREAD)` y lee el buffer. La Etapa 3 (`1.4-dma-stage3`) conecta la ruta de interrupción al motor DMA.
7. **Manejo de errores DMA y casos extremos.** Cada modo de fallo que aborda el capítulo, con el patrón de recuperación correspondiente: fallos de mapeo, transferencias parciales, tiempos de espera agotados y detach durante una transferencia en vuelo.
8. **Refactorización y versionado de tu driver con capacidad DMA.** La división final en `myfirst_dma.c` y `myfirst_dma.h`, el Makefile actualizado, el documento `DMA.md` y el incremento de versión. La Etapa 4 (`1.4-dma`).

Tras las ocho secciones llegan un recorrido ampliado por el código DMA de `if_re.c`, varias miradas más profundas a los bounce buffers, los dispositivos de 32 bits, el IOMMU, los flags de coherencia y la herencia de tags padre, un conjunto de laboratorios prácticos, un conjunto de ejercicios de desafío, una referencia de resolución de problemas, un Wrapping Up que cierra la historia del Capítulo 21 y abre la del Capítulo 22, un puente y el habitual material de referencia rápida y glosario al final del capítulo. El material de referencia está pensado para releerlo mientras avanzas por los capítulos siguientes; el vocabulario del Capítulo 21 (tag, map, segmento, PRE/POST, coherente, bounce, callback) es la base que asume cada capítulo posterior.

Si esta es tu primera pasada, lee linealmente y haz los laboratorios en orden. Si estás revisando, las Secciones 3, 4 y 5 se sostienen solas y son buenas lecturas para una sola sesión.



## Sección 1: ¿Qué es DMA y por qué usarlo?

Antes del código del driver, la imagen hardware. La Sección 1 enseña qué es DMA al nivel del bus PCIe, qué le aporta al driver respecto a las lecturas y escrituras MMIO, cómo son los ejemplos del mundo real y por qué el desarrollo moderno de drivers toma DMA como una suposición base en lugar de una optimización. Un lector que termina la Sección 1 puede leer el resto del capítulo con la capa `bus_dma` del kernel como un objeto concreto en lugar de una abstracción vaga.

### El problema del movimiento de datos solo mediante MMIO

El Capítulo 16 introdujo la I/O mapeada en memoria: los registros de un dispositivo residen dentro de una región de memoria física, el kernel mapea esa región en el espacio de direcciones virtuales del kernel, y los envoltorios `bus_read_4` y `bus_write_4` convierten las lecturas y escrituras en transacciones de bus. El Capítulo 17 construyó un backend simulado que tiene el mismo aspecto desde la perspectiva del driver. El Capítulo 18 trasladó ese backend a un BAR PCI real. Cada palabra de datos que ha manejado el driver del Capítulo 19 y del Capítulo 20 ha llegado a través de `bus_read_4` desde el registro `DATA_OUT` del dispositivo.

Para un driver que gestiona una palabra por evento, ese modelo funciona. Para un driver que tiene que gestionar un megabyte de datos, no. Cada `bus_read_4` es una transacción MMIO; cada transacción MMIO es un ciclo de bus posted o de completion con su propio overhead. En un enlace PCIe 3.0 x4 típico, el tiempo de ida y vuelta por transacción es de unos pocos cientos de nanosegundos, una vez que se suman la cabecera PCIe, el completion de lectura y las instrucciones de ordenación de memoria del CPU. Un driver que mueve datos palabra a palabra alcanza como máximo unas pocas decenas de megabytes por segundo, independientemente de lo que el dispositivo sea realmente capaz de hacer.

Una NIC moderna es capaz de decenas de gigabytes por segundo. Un NVMe moderno es capaz de varios gigabytes por segundo de I/O sostenida. Un controlador host USB3 gestiona miles de descriptores por segundo. Una GPU gestiona cientos de megabytes de buffers de comandos por fotograma. El hardware no es el cuello de botella en ninguno de estos casos; el modelo MMIO sí lo es. Todos los drivers para estos dispositivos deben usar un mecanismo diferente para mover el grueso de los datos, y ese mecanismo es DMA.

### Qué significa DMA a nivel de hardware

DMA son las siglas de Direct Memory Access (acceso directo a memoria). La palabra «directo» es la clave. En una transferencia MMIO, la CPU actúa como maestra: la CPU emite una lectura o escritura en la región MMIO del dispositivo, y el dispositivo responde. En una transferencia DMA, el dispositivo actúa como maestro: el dispositivo emite una lectura o escritura en la memoria del sistema, y el controlador de memoria responde como si la petición la hubiera hecho la CPU. La CPU no interviene en cada palabra transferida; el dispositivo mueve los bytes según su propio ritmo.

Físicamente, una transferencia DMA es una transacción de memoria en el mismo bus que la CPU usa para llegar a la RAM. En PCIe esto se denomina transacción **bus-master**, y los dispositivos PCI que pueden realizarla se dice que son dispositivos de **bus-mastering**. El dispositivo mantiene un estado interno (una dirección de origen, una dirección de destino, una longitud, una dirección y un estado) y avanza en la transferencia bajo su propio reloj. La CPU configura la transferencia escribiendo los parámetros correspondientes en los registros de control del dispositivo y, a continuación, emite una escritura de confirmación (a menudo llamada «doorbell»), tras lo cual puede dedicarse a otra cosa hasta que el dispositivo señale la finalización.

Desde el punto de vista de la CPU, una transferencia DMA atraviesa más o menos los siguientes pasos:

1. El driver asigna una región de memoria del sistema con las propiedades que el dispositivo puede utilizar (direccionable por el dispositivo, alineada según sus requisitos, contigua si el dispositivo lo exige).
2. El driver hace que el dispositivo vea esa región en alguna **dirección de bus** (que puede o no coincidir con la dirección física del sistema, dependiendo de la plataforma).
3. El driver escribe la dirección de bus, la longitud y la dirección en los registros del dispositivo.
4. El driver escribe en un registro doorbell que indica al dispositivo que comience.
5. El dispositivo realiza la transferencia por su cuenta, leyendo de o escribiendo en la memoria del sistema en la dirección de bus, a través del controlador de memoria, mientras la CPU queda libre.
6. El dispositivo señala la finalización, normalmente escribiendo un bit de estado en un registro MMIO, generando una interrupción, o ambas cosas.
7. El driver realiza la sincronización de caché necesaria entre la visión que tiene la CPU de la memoria y la visión que tiene el dispositivo.
8. El driver lee o utiliza los datos transferidos.

Cada paso parece sencillo. La sutileza reside en el paso 2 (qué dirección de bus usar y cómo hacer que el dispositivo vea el buffer) y en el paso 7 (qué significa «necesaria» y cómo sabe el driver cuándo debe realizar cada tipo de sincronización). La mayor parte del Capítulo 21 trata precisamente de esos dos pasos.

### Lo que gana el driver

Tres ventajas concretas importan en cualquier driver con capacidad DMA.

La primera es el **rendimiento**. En PCIe, una transferencia bus-master utiliza el ancho de banda y el reloj propios del dispositivo. El enlace PCIe 3.0 x4 de una NIC de diez gigabits puede sostener aproximadamente tres gigabytes y medio por segundo de datos transferidos con bus-mastering. El mismo enlace utilizado únicamente para MMIO ronda los veinte megabytes por segundo: la NIC no puede alcanzar el ancho de banda anunciado sin DMA. La proporción es peor en dispositivos de mayor rendimiento. Una GPU con PCIe 4.0 x16 tiene un presupuesto de enlace teórico de más de treinta gigabytes por segundo; ninguna cantidad de llamadas a `bus_write_4` puede acercarse a esa cifra.

La segunda es la **descarga de CPU**. Las transferencias bus-master consumen muy poco tiempo de CPU una vez iniciadas. La CPU escribe unos pocos registros, activa el doorbell y queda libre hasta que llega la interrupción de finalización. En un sistema con muchos dispositivos con capacidad DMA, la CPU puede orquestar decenas de transferencias concurrentes siempre que el dispositivo disponga de suficientes motores DMA. En una NIC que admite transmisión multi-cola, la CPU puede encolar descriptores de recepción, entregar buffers de transmisión al dispositivo y procesar las finalizaciones en ambas direcciones al mismo tiempo, mientras el movimiento real de bytes ocurre en los motores DMA internos del dispositivo. El trabajo de la CPU se convierte en política y contabilidad; el movimiento de datos ocurre en otro lugar.

La tercera es el **determinismo bajo carga**. Un driver basado en MMIO que recorre en bucle un buffer grande puede ser expulsado por una interrupción, el ithread que la gestiona puede monopolizar la CPU durante un período prolongado, y el rendimiento se convierte en una función del momento en que el kernel realiza la apropiación en lugar de depender de la capacidad del dispositivo. Un driver basado en DMA deja que el dispositivo haga el trabajo; el propio código del driver se ejecuta durante un número de ciclos predecible y reducido por transferencia, y cede el paso al planificador entre medias. Las distribuciones de latencia se ajustan, las latencias de cola disminuyen y el rendimiento del driver se vuelve más fácil de razonar.

Estas tres ventajas se potencian mutuamente. Un driver que usa DMA consume menos CPU por byte, mueve más bytes por unidad de tiempo y hace ambas cosas de forma más predecible. El coste es el código de configuración de `bus_dma`, que añade algunas centenas de líneas al driver. El capítulo trata sobre cómo escribir bien esas líneas.

### Ejemplos del mundo real

DMA es omnipresente en el hardware moderno. Algunos ejemplos concretos ilustran la escala de su uso.

Una **tarjeta de red** utiliza DMA tanto para recepción como para transmisión. El dispositivo tiene un **anillo de recepción** en la memoria del sistema: un array de descriptores, cada uno de los cuales contiene una dirección de bus y una longitud que apuntan a un buffer de paquetes. El dispositivo copia los paquetes entrantes desde el cable a los buffers mediante escrituras bus-master, actualiza el campo de estado de cada descriptor cuando el paquete está completo y genera una interrupción. El driver recorre el anillo, procesa cada descriptor completado y rellena el anillo con buffers nuevos. La transmisión funciona en sentido inverso: el driver coloca las cabeceras y los datos de los paquetes salientes en buffers, escribe descriptores con las direcciones de bus y activa el dispositivo; el dispositivo lee los buffers, transmite y actualiza el estado del descriptor. Todo el protocolo se ejecuta en la memoria del sistema, sincronizado mediante llamadas a `bus_dmamap_sync` e interrupciones de finalización. El driver del Capítulo 21 es una simplificación de transferencia única de este patrón; la Parte 6 (Capítulo 28) lo generaliza a anillos completos.

Un **controlador de almacenamiento** (SATA, SAS, NVMe) utiliza DMA para cada transferencia de bloque. El driver emite un bloque de comandos que contiene una lista de direcciones físicas (una scatter-gather list) que describe las páginas que componen el buffer del sistema. El dispositivo recorre la lista, lee o escribe cada página y señala la finalización. Los controladores NVMe modernos utilizan una estructura **Physical Region Page (PRP)** o **Scatter Gather List (SGL)** para describir la transferencia, y el controlador escribe la finalización en una cola de finalización que también reside en la memoria del sistema mediante DMA. El driver `nvme(4)` consta de unas cuatro mil líneas de código con soporte DMA; la estructura es cómoda de leer una vez que los primitivos del Capítulo 21 están asimilados.

Un **controlador de host USB3** utiliza DMA para los descriptores de transferencia, los eventos de finalización y los datos masivos. El driver entrega al dispositivo un puntero a un descriptor de transferencia en la memoria del sistema; el dispositivo lo recupera mediante DMA, realiza la transferencia, escribe la finalización en un anillo de eventos y genera una interrupción. USB es especialmente interesante porque los datos de cada dispositivo USB pasan por el motor DMA del controlador de host, de modo que el driver de una NIC USB es en realidad dos drivers apilados: el controlador de host USB realiza el DMA, y el driver del dispositivo USB se ejecuta por encima.

Una **GPU** utiliza DMA para los buffers de comandos, las cargas de texturas y a veces el scanout de pantalla. Un único fotograma puede implicar decenas de megabytes de datos moviéndose entre la RAM del sistema y la VRAM de la GPU, orquestados por el motor DMA propio de la GPU bajo la dirección del flujo de comandos del driver. Los ports drm-kmod de FreeBSD, adaptaciones de los drivers DRM de Linux, utilizan `bus_dma` para configurar las asignaciones de buffer de comandos; la traducción desde la API DMA de Linux es una capa delgada porque las abstracciones son similares.

Una **tarjeta de sonido** utiliza DMA para el buffer de audio. El driver asigna un buffer circular, programa el dispositivo con su dirección de bus y su longitud, y el dispositivo lee muestras en tiempo real comenzando de nuevo al llegar al final. El driver rellena el buffer por delante del puntero de lectura del dispositivo y depende de una interrupción de posición (o un temporizador periódico) para planificar el relleno. `sound(4)` utiliza `bus_dma` para este propósito; el patrón es un punto intermedio adecuado entre el DMA de transferencia única y los anillos scatter-gather completos.

Cada uno de estos ejemplos sigue el mismo patrón abstracto: tag, map, memory, sync, trigger, complete, sync, consume, unload. Los detalles difieren; la forma es constante. El Capítulo 21 enseña esa forma con un dispositivo simulado; los capítulos posteriores aplican esa forma a los subsistemas reales.

### Por qué el acceso directo a memoria no puede ser irrestricto

A primera vista, «el dispositivo escribe en memoria» parece algo sencillo. El dispositivo tiene un registro interno; el registro contiene una dirección de bus; el dispositivo escribe en esa dirección. ¿Por qué necesita el driver hacer algo más que indicar una dirección al dispositivo?

Tres realidades del hardware hacen que el DMA sin restricciones sea inseguro, y esas realidades son lo que `bus_dma(9)` existe para ocultar.

**Realidad uno: es posible que el dispositivo no pueda acceder a toda la memoria del sistema.** Los dispositivos PCI más antiguos son bus-master de 32 bits: solo pueden direccionar los cuatro gigabytes inferiores del espacio de direcciones de bus. En un sistema con dieciséis gigabytes de RAM, un buffer en la dirección física 0x4_0000_0000 es invisible para ese dispositivo; entregarle esa dirección provocaría una corrupción silenciosa (la escritura va a algún lugar, pero no donde esperaba el driver). Algunos dispositivos más modernos tienen motores DMA de 36 o 40 bits y pueden acceder a más RAM, pero no a toda. El driver tiene que describir el rango del dispositivo al kernel, y el kernel tiene que garantizar que todos los buffers que ve el dispositivo estén dentro de ese rango. Cuando un buffer resulta estar fuera del rango, el kernel inserta silenciosamente un **bounce buffer**: una región dentro del rango en la que el kernel copia los datos antes de la lectura DMA (o desde la que copia después de la escritura DMA). Los bounce buffers son correctos pero costosos; un driver que asigna sus buffers dentro del rango del dispositivo los evita.

**Realidad dos: la dirección de bus que ve el dispositivo no siempre coincide con la dirección física que utiliza la CPU.** En los sistemas amd64 modernos con una IOMMU activada (Intel VT-d, AMD-Vi), el controlador de memoria inserta una capa de traducción entre la dirección de bus del dispositivo y la dirección física del sistema. El dispositivo escribe en la dirección de bus X; la IOMMU la traduce a la dirección física Y; el controlador de memoria escribe en la dirección física Y. La traducción es por dispositivo, y de forma predeterminada la IOMMU solo permite que un dispositivo acceda a la memoria que el driver ha asignado explícitamente. Esto supone una ventaja en corrección y seguridad (un dispositivo con errores o comprometido no puede escribir en memoria arbitraria), pero exige la participación del driver: el driver le dice al kernel «este buffer debe ser visible para este dispositivo», el kernel programa la IOMMU en consecuencia y solo entonces puede el dispositivo acceder al buffer. Sin `bus_dma`, el driver tendría que saber si hay una IOMMU presente, cómo son sus tablas de páginas y cómo programarlas.

**Realidad tres: la CPU y el dispositivo pueden ver la misma memoria de forma diferente en el mismo instante.** Cada CPU tiene cachés; los cachés almacenan copias de la memoria con la granularidad de las líneas de caché (normalmente sesenta y cuatro bytes en amd64, a veces más en otras plataformas). Cuando la CPU escribe un valor, la escritura va primero al caché; la línea de caché se marca como sucia, y la escritura solo llega al controlador de memoria cuando la línea es desalojada o un protocolo de coherencia la sincroniza. Cuando un dispositivo con capacidad DMA escribe un valor, la escritura va directamente al controlador de memoria (a menos que la plataforma sea totalmente coherente, como suele ser el caso en amd64 pero no siempre en ARM), por lo que la copia en caché de la CPU puede estar obsoleta. A la inversa, cuando el dispositivo lee un valor que la CPU ha escrito recientemente y que no ha volcado, el dispositivo lee un valor obsoleto. En plataformas totalmente coherentes, el hardware lo gestiona de forma automática; en plataformas parcialmente coherentes o no coherentes, el driver tiene que indicarle al kernel «la CPU está a punto de leer este buffer, por favor asegúrate de que los cachés se invalidan primero» o «la CPU acaba de escribir este buffer, por favor vuelca los cachés antes de que el dispositivo lea». Esto es precisamente lo que hace `bus_dmamap_sync`.

Estas tres realidades no son visibles para los autores de drivers en amd64 con un IOMMU moderno y un dispositivo completamente coherente; la API `bus_dma` sigue siendo obligatoria, pero la mayor parte de su trabajo resulta transparente. En hardware de 32 bits, en plataformas ARM no coherentes, o en sistemas donde el IOMMU está configurado de forma restrictiva, la API realiza operaciones reales en cada llamada. Un driver escrito correctamente con `bus_dma` funciona en todos ellos; un driver que elude la API solo funciona en el subconjunto de plataformas donde el modelo ingenuo resulta ser correcto.

### Por qué existe bus_dma(9)

La interfaz `bus_dma(9)` de FreeBSD es la capa de portabilidad que oculta estas tres realidades detrás de una única API. Fue heredada y adaptada del equivalente de NetBSD, y refinada en FreeBSD a lo largo de las versiones 5.x y posteriores. Las decisiones de diseño que la distinguen merecen comprenderse a nivel conceptual antes de pasar a los ejemplos de código.

La API **separa la descripción de la ejecución**. Una **tag** de DMA describe las restricciones de un grupo de transferencias: qué rango de direcciones es accesible, qué alineación se requiere, qué tamaño máximo puede tener una transferencia, en cuántos segmentos puede dividirse y cuáles son las reglas de contorno. Un **map** de DMA representa la correspondencia de una transferencia específica con esa tag: qué direcciones de bus ocupa el buffer concreto y qué bounce buffers (si los hay) están activos. El driver crea la tag una sola vez durante el attach; crea o carga maps en el momento de la transferencia. Esta separación permite que el kernel guarde en caché la configuración costosa (tags padre, verificaciones de restricciones) mientras mantiene barato el trabajo por transferencia.

La API **utiliza callbacks para devolver la información de mapping**. `bus_dmamap_load` no devuelve directamente la lista de direcciones de bus; en su lugar, invoca una función callback que el driver proporciona, pasándole la lista de segmentos como un array. El motivo es histórico y práctico: en algunas plataformas, la carga puede necesitar esperar a que haya bounce buffers disponibles, y en ese caso el callback se ejecuta más tarde, cuando los buffers están libres. El código de carga del driver retorna inmediatamente con `EINPROGRESS`; el callback acaba ejecutándose y completa el mapping. Este patrón es el que confunde a la mayoría de los lectores primerizos, y la Sección 3 lo recorre con cuidado. Para casos sencillos (anillos de descriptores asignados durante el attach, en los que el driver puede esperar), la carga se completa de forma síncrona y el callback se ejecuta antes de que `bus_dmamap_load` retorne.

La API **hace explícita la sincronización**. `bus_dmamap_sync` con un flag PRE indica: "la CPU está a punto de dejar de acceder a este buffer y dárselo al dispositivo; por favor, haz el flush". Con un flag POST indica: "el dispositivo ha dejado de acceder a este buffer y la CPU está a punto de acceder a él; por favor, invalida". En plataformas coherentes, `bus_dmamap_sync` es a veces una no-operación; en plataformas no coherentes, ejecuta el flush o la invalidación de caché. El driver escribe el mismo código en ambos casos; la API gestiona la diferencia.

La API **admite jerarquías de restricciones**. Una tag puede heredar de una tag padre, y las restricciones de la hija son un subconjunto de las del padre. Esto refleja el hardware: las capacidades DMA de un dispositivo PCI están limitadas por las capacidades del puente padre, que a su vez están limitadas por el controlador de memoria de la plataforma. `bus_get_dma_tag(9)` devuelve la tag padre del dispositivo, y el driver la pasa a `bus_dma_tag_create` como padre de cualquier tag que cree. El kernel combina las restricciones automáticamente; el driver solo describe los requisitos de su propio dispositivo.

Estas cuatro decisiones de diseño (separación, callbacks, sincronización explícita, jerarquías) son visibles en todos los drivers que usan `bus_dma`, y el código del capítulo las sigue fielmente. La ventaja de entender el diseño es que los drivers reales se vuelven mucho más fáciles de leer; el driver `nvme(4)` de seis mil líneas, por ejemplo, sigue el mismo patrón que el driver de juguete del Capítulo 21.

### El flujo concreto que construirá el Capítulo 21

En concreto, el driver del Capítulo 21 aprenderá a realizar la siguiente secuencia, en orden:

1. **Crear una tag que herede del padre.** El driver llama a `bus_get_dma_tag(sc->dev)` para obtener la tag padre del dispositivo y la pasa a `bus_dma_tag_create` junto con las restricciones propias del dispositivo `myfirst` (alineación de 4 KB, tamaño de buffer de 4 KB, 1 segmento, `BUS_SPACE_MAXADDR` para el rango de direcciones, ya que la simulación no tiene límite arquitectónico).
2. **Asignar memoria DMA.** El driver llama a `bus_dmamem_alloc` con la tag. El kernel devuelve una dirección virtual del kernel que apunta a un buffer de cuatro kilobytes, mapeado tanto para la CPU como apto para el dispositivo. La llamada también devuelve un handle `bus_dmamap_t` que representa el mapping.
3. **Cargar la memoria en el map.** El driver llama a `bus_dmamap_load` con la dirección virtual del kernel y un callback. El callback recibe la lista de segmentos (un único segmento en este caso sencillo) y guarda la dirección de bus en el softc.
4. **Programar el dispositivo.** El driver escribe la dirección de bus, la longitud y la dirección de la transferencia en los registros del motor DMA simulado.
5. **Sincronizar con PREWRITE (de host a dispositivo) o PREREAD (de dispositivo a host).** El driver llama a `bus_dmamap_sync` con el flag apropiado, indicando que la CPU ha terminado de tocar el buffer y que el dispositivo está a punto de acceder a él.
6. **Arrancar el motor.** El driver escribe el bit START del registro DMA_CTRL; el motor simulado programa un `callout(9)` para dentro de unos pocos milisegundos.
7. **Esperar la finalización.** A través del filtro rx del Capítulo 20, que se activa cuando el motor simulado eleva `DMA_COMPLETE`. El filtro encola la tarea; la tarea se ejecuta.
8. **Sincronizar con POSTREAD o POSTWRITE.** La tarea llama a `bus_dmamap_sync` con el flag POST antes de acceder al buffer.
9. **Leer y verificar.** La tarea compara el contenido del buffer con el patrón esperado y actualiza un contador en el softc.
10. **Desmontar.** Durante el detach, el driver descarga el map, libera la memoria y destruye la tag. El orden es el inverso al de la configuración inicial.

Cada paso se corresponde con una única llamada a `bus_dma`. El objetivo del capítulo es enseñar cada llamada en su contexto, mostrar cómo encaja en el ciclo de vida existente del driver y explicar qué hace el propio kernel en cada llamada, de modo que el lector pueda depurar cuando algo falle. Un driver que haya interiorizado estos diez pasos puede pasar a anillos de descriptores, listas de scatter-gather y `bus_dmamap_load_mbuf_sg` sin aprender un modelo nuevo; cada uno de ellos es una variación sobre los mismos diez pasos.

### Ejercicio: identificar dispositivos con DMA en tu sistema

Antes de la siguiente sección, dedica cinco minutos a explorar tu propio sistema. El ejercicio es sencillo y ayuda a desarrollar la intuición: identifica tres dispositivos en tu máquina de laboratorio que utilicen DMA y anota una característica de cada uno.

Empieza con `pciconf -lv`. La herramienta lista todas las funciones PCI, y la mayoría de ellas admiten DMA de alguna forma. Para cada función, observa a qué subsistema pertenece (red, almacenamiento, gráficos, audio, USB). Luego busca la línea que comienza con `cmdreg:` en la salida de `pciconf -c`; si el bit que indica `BUSMASTEREN` está activo, el dispositivo tiene habilitado el bus mastering y está usando DMA activamente.

```sh
pciconf -lvc | grep -B1 BUSMASTEREN
```

Elige una función de red y examínala con más detalle:

```sh
pciconf -lvbc <devname>
```

`pciconf -lvbc` muestra las regiones BAR, la lista de capacidades y si el espacio de configuración del dispositivo PCIe reporta alguna capacidad relevante para DMA (MSI/MSI-X, gestión de energía, PCIe DevCtl, ASPM). En la mayoría de los sistemas modernos, la salida revela que el dispositivo tiene un BAR MMIO grande (para acceso a registros) y BARs MMIO más pequeños (para la tabla MSI-X y el PBA), pero ningún puerto I/O; la mayor parte de la memoria del dispositivo está en RAM, accedida por DMA, no en el propio BAR del dispositivo.

A continuación, examina un dispositivo `nvme` si tienes uno, o busca en `dmesg` mensajes de "mapped DMA". La mayoría de los drivers de almacenamiento muestran un breve mensaje de configuración DMA durante el attach; `nvme_ctrlr_setup` es un buen término para buscar con grep.

Anota en un cuaderno de laboratorio:

1. Un dispositivo de red con bus mastering habilitado y una hipótesis sobre para qué usa DMA.
2. Un dispositivo de almacenamiento (si lo hay) con su mensaje de configuración DMA.
3. Un dispositivo que no esperabas que usara DMA, pero que lo usa. Un hallazgo sorprendente tiene mucho valor: una vez que sabes qué buscar, la imagen cambia.

El ejercicio lleva unos diez minutos y te proporciona un mapa del paisaje DMA de tu propio objetivo de laboratorio antes de que comience el trabajo del Capítulo 21.

### En resumen: Sección 1

La Sección 1 estableció la visión del hardware. El DMA permite a los dispositivos escribir directamente en la memoria del host, eludiendo el cuello de botella de MMIO palabra a palabra. Los beneficios son el rendimiento, la descarga de la CPU y el determinismo; los costes son la complejidad de la configuración y la necesidad de sincronizar entre la vista en caché de la CPU y la vista visible en el bus del dispositivo. La interfaz `bus_dma(9)` de FreeBSD existe para ocultar tres realidades hardware (límites de direccionamiento del dispositivo, remapping mediante IOMMU, coherencia de caché) detrás de una única API, y los cuatro principios de diseño de la API (separación tag/map, carga basada en callbacks, sincronización explícita, herencia de restricciones) aparecen en todos los drivers que la utilizan. El ejemplo progresivo del Capítulo 21 ejercitará cada principio por turnos en un dispositivo simulado, con suficiente base real de FreeBSD para que los patrones sean directamente aplicables a drivers en producción.

La Sección 2 es el siguiente paso: el vocabulario de la API en detalle. Qué contiene realmente una tag, qué es exactamente un map, qué es un segmento, qué significan los flags PRE y POST a un nivel más fino y cómo encajan todas las piezas.



## Sección 2: La interfaz bus_dma(9) de FreeBSD

La Sección 1 estableció que `bus_dma(9)` es la capa de portabilidad entre el driver y las realidades DMA de la plataforma. La Sección 2 abre esa capa y examina sus piezas. El objetivo es dar al lector el vocabulario necesario para hablar de una DMA tag, un DMA map, un segmento, una operación de sincronización y un callback sin misterios. Aún no se escribe código de la Etapa 1; eso lo hará la Sección 3. Esta sección es el mapa mental.

### Las cuatro piezas de la API

Todo driver que use `bus_dma` trabaja con cuatro objetos. Comprender los cuatro hace que el resto de la API encaje.

**La tag** es una descripción. Es un objeto opaco del kernel de tipo `bus_dma_tag_t`, creado una sola vez y reutilizado para muchas transferencias. Una tag contiene:

- Una tag padre opcional, heredada del puente padre mediante `bus_get_dma_tag(dev)`.
- Una restricción de alineación en bytes (la dirección de inicio de cada mapping realizado a través de esta tag debe ser múltiplo de este valor).
- Una restricción de contorno en bytes (no se permite un mapping que cruce un límite de dirección de este tamaño).
- Una dirección baja y una dirección alta que juntas describen una ventana del espacio de direcciones de bus que el dispositivo **no puede** alcanzar.
- Un tamaño máximo de mapping (la suma de las longitudes de los segmentos en un mapping).
- Un número máximo de segmentos (cuántas piezas discontinuas puede abarcar un mapping).
- Un tamaño máximo de segmento (la pieza individual más grande).
- Un conjunto de flags, principalmente `BUS_DMA_WAITOK`, `BUS_DMA_NOWAIT` y `BUS_DMA_COHERENT`.
- Una función lock opcional y su argumento, utilizados cuando el kernel necesita invocar el callback de carga del driver desde un contexto diferido.

La tag es el mecanismo por el que el driver comunica al kernel: "este dispositivo tiene estas restricciones; por favor, respétalas en cada mapping que realice a través de esta tag". El kernel consulta la tag durante cada operación de map y garantiza que el mapping respete las restricciones (o informa de un error si el buffer solicitado no puede satisfacerlas).

**El map** es un contexto de mapping. Es un objeto opaco del kernel de tipo `bus_dmamap_t`, creado (a menudo implícitamente) por transferencia. Un map guarda suficiente estado para describir un mapping específico: qué direcciones de bus ocupa el buffer, si hay páginas bounce en uso y si el mapping está actualmente cargado o inactivo. Los maps son baratos de crear y baratos de cargar; el trabajo costoso de configuración vive en la tag.

**La memoria** es el rango de direcciones virtuales del kernel que usa la CPU para acceder al buffer. Para regiones DMA estáticas (asignadas durante el attach y reutilizadas en muchas transferencias), la memoria se asigna con `bus_dmamem_alloc`, que devuelve una dirección virtual del kernel y un map cargado implícitamente. Para DMA dinámico (mapping de buffers arbitrarios del kernel, mbufs o datos de usuario), la memoria ya existe en otro lugar y el driver usa `bus_dmamap_create` junto con `bus_dmamap_load` para asociar un map a ella.

**El segmento** es el par dirección de bus/longitud que el dispositivo ve realmente. `bus_dma_segment_t` es una estructura pequeña con dos campos: `ds_addr` (un `bus_addr_t` que indica la dirección de bus) y `ds_len` (un `bus_size_t` que indica la longitud). Un único mapping puede constar de un segmento (físicamente contiguo) o de varios (scatter-gather). El driver programa el dispositivo con la lista de segmentos; esa es la entrega concreta.

El driver del capítulo 21 utiliza los cuatro. La etiqueta se crea en `myfirst_dma_setup`. El mapa lo devuelve `bus_dmamem_alloc`. La memoria es el buffer de cuatro kilobytes que devuelve esa misma llamada. El segmento es el único `bus_dma_segment_t` que retorna el callback pasado a `bus_dmamap_load`. Los capítulos posteriores amplían esto con mapas por transferencia y scatter-gather de múltiples segmentos, pero los mismos cuatro objetos están siempre presentes.

### Transacciones estáticas y dinámicas

La página del manual `bus_dma(9)` establece una distinción entre transacciones **estáticas** y **dinámicas**. Esta distinción importa porque determina qué llamadas de API utiliza un driver.

Las **transacciones estáticas** utilizan regiones de memoria asignadas por el propio `bus_dma`, normalmente en el momento del attach, y las reutilizan durante toda la vida del driver. Los anillos de descriptores son el ejemplo clásico: un driver de NIC asigna el anillo de recepción una sola vez y lo usa para cada paquete, sin volver a cargarlo ni descargarlo. El driver llama a:

- `bus_dma_tag_create` una vez, para describir las restricciones del anillo.
- `bus_dmamem_alloc` una vez, para asignar la memoria del anillo y obtener un mapa cargado implícitamente.
- `bus_dmamap_load` una vez, para obtener la dirección de bus del anillo. (La página del manual lo expresa como «an initial load operation is required to obtain the bus address»; se trata de una peculiaridad de la API que simplemente hay que recordar.)
- `bus_dmamap_sync` muchas veces, alrededor de cada uso del anillo.

En el desmontaje, el driver llama a `bus_dmamap_unload`, `bus_dmamem_free` y `bus_dma_tag_destroy`. No se necesita `bus_dmamap_create` ni `bus_dmamap_destroy`, porque `bus_dmamem_alloc` devuelve el mapa y `bus_dmamem_free` lo libera.

Las **transacciones dinámicas** utilizan regiones de memoria asignadas por otra parte (un mbuf de `m_getcl`, un buffer del kernel de `malloc`, una página de usuario fijada por `vm_fault_quick_hold_pages`), y el driver las mapea en el espacio de direcciones del dispositivo en cada transferencia. Un driver de NIC que transmite paquetes hace esto para cada paquete saliente: el mbuf del paquete ya existe, el driver lo mapea, programa el dispositivo, espera a que concluya la transmisión y lo desmapea. El driver llama a:

- `bus_dma_tag_create` una vez, para describir las restricciones por buffer.
- `bus_dmamap_create` una vez por ranura de buffer, para obtener un mapa.
- `bus_dmamap_load_mbuf_sg` (o `bus_dmamap_load`) cada vez que se transmite un paquete, para mapear el mbuf específico.
- `bus_dmamap_sync` alrededor de cada uso.
- `bus_dmamap_unload` después de que cada transmisión concluya.
- `bus_dmamap_destroy` una vez por ranura de buffer, en el detach.
- `bus_dma_tag_destroy` una vez, en el detach.

El driver del Capítulo 21 utiliza el patrón estático: un único buffer, asignado una sola vez, reutilizado en cada transferencia DMA simulada. El patrón dinámico se presenta brevemente en la Sección 7 como contraste; el capítulo de red de la Parte 6 (Capítulo 28) lo utiliza en profundidad.

Saber qué patrón utiliza un driver es lo primero que hay que identificar cuando se lee código DMA. La secuencia de llamadas es distinta, el orden de desmontaje es distinto y la interpretación del mapa es distinta. El comportamiento de «asignación estática única» de `bus_dmamem_alloc` es lo que hace que la ruta estática sea más corta y sencilla.

### La disciplina de sincronización

La Sección 1 describió `bus_dmamap_sync` en términos generales; la Sección 2 es el lugar donde precisar las cuatro operaciones con exactitud, porque la sección siguiente asumirá este vocabulario.

Las cuatro operaciones son:

- `BUS_DMASYNC_PREREAD`. Se llama **antes** de que el dispositivo escriba en el buffer (desde el punto de vista del host, antes de que el host lea el buffer). Le indica al kernel «la CPU ha terminado lo que estaba haciendo con este buffer; el dispositivo está a punto de escribir en él». En plataformas no coherentes, esto invalida la copia del cache de la CPU para que una lectura posterior vea lo que el dispositivo ha escrito. En plataformas coherentes, suele ser un no-op.
- `BUS_DMASYNC_PREWRITE`. Se llama **antes** de que el dispositivo lea el buffer (desde el punto de vista del host, antes de que el host escriba en el buffer). Le indica al kernel «la CPU acaba de escribir en el buffer; por favor, descarga las líneas de cache sucias para que el dispositivo lea el contenido actual». En plataformas no coherentes, esto es un cache flush; en plataformas coherentes, suele ser una barrera de memoria o un no-op.
- `BUS_DMASYNC_POSTREAD`. Se llama **después** de que el dispositivo haya escrito en el buffer y **antes** de que la CPU lo lea. Le indica al kernel «el dispositivo ha terminado de escribir; la CPU está a punto de leer». En plataformas con bounce buffers, este es el punto en el que los datos se copian desde la región de rebote de vuelta al buffer del driver.
- `BUS_DMASYNC_POSTWRITE`. Se llama **después** de que el dispositivo haya leído el buffer. Le indica al kernel «el dispositivo ha terminado de leer; la CPU puede reutilizar el buffer». Suele ser un no-op en plataformas coherentes; en sistemas con bounce buffers, es el momento en que la región de rebote puede liberarse.

Vale la pena interiorizar los nombres. «PRE» y «POST» hacen referencia a la transacción DMA: PRE es antes, POST es después. «READ» y «WRITE» se expresan desde la perspectiva del **host**: READ significa que el host leerá el resultado (el dispositivo escribe), WRITE significa que el host ha escrito lo que el dispositivo leerá.

Los pares se combinan en cuatro secuencias habituales:

- **Del host al dispositivo (el driver envía datos al dispositivo):** escribir datos → `PREWRITE` → el dispositivo lee → el dispositivo termina → `POSTWRITE`.
- **Del dispositivo al host (el dispositivo envía datos al driver):** `PREREAD` → el dispositivo escribe → el dispositivo termina → `POSTREAD` → el host lee.
- **Anillo de descriptores en el que el driver actualiza una entrada, el dispositivo la lee, actualiza el estado y el driver lee el estado:** el driver escribe la entrada → `PREWRITE` → el dispositivo lee la entrada → el dispositivo actualiza el estado → el dispositivo termina → `POSTREAD | POSTWRITE` (flag combinado) → el host lee el estado.
- **Anillo completo compartido entre el host y el dispositivo:** en la configuración, `PREREAD | PREWRITE` marca el anillo entero como transferido al dispositivo, con ambas direcciones de flujo de datos abiertas.

Los drivers reales utilizan los flags combinados con frecuencia porque los anillos de descriptores son bidireccionales. El Capítulo 21 comienza con los casos simples de una sola dirección y muestra el flag combinado en el esquema del anillo de descriptores al final de la Sección 4.

### El patrón de callback de carga

La parte más sorprendente de `bus_dma` para quien lo lee por primera vez es que `bus_dmamap_load` no devuelve la lista de segmentos directamente. Recibe una **función de callback** como argumento, llama al callback con la lista de segmentos y (normalmente) regresa después de que el callback haya terminado. ¿Por qué esta indirección?

La razón es que en plataformas donde el kernel puede necesitar esperar a que haya bounce buffers disponibles, la operación de carga puede **diferirse**. Si los bounce buffers escasean en el momento de la llamada, el kernel encola la solicitud, devuelve `EINPROGRESS` al llamador y ejecuta el callback más tarde cuando los buffers estén libres. El driver debe estar preparado para este caso: el callback puede ejecutarse en un contexto diferente, posiblemente en un thread distinto, después de que la llamada de carga ya haya retornado.

Para la mayoría de los drivers en la mayoría de las plataformas, el caso diferido es poco frecuente. Un driver que asigna su anillo de descriptores en el momento del attach, en un sistema con suficientes bounce buffers y sin restricciones de IOMMU, verá que el callback se ejecuta de forma síncrona dentro de la llamada de carga, mucho antes de que esta retorne. El callback simplemente guarda la dirección de bus en una variable local que el driver lee inmediatamente después de que la carga retorne.

Un callback mínimo tiene este aspecto:

```c
static void
myfirst_dma_single_map(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *addr = arg;

	if (error)
		return;

	KASSERT(nseg == 1, ("unexpected DMA segment count %d", nseg));
	*addr = segs[0].ds_addr;
}
```

El driver llama así:

```c
bus_addr_t bus_addr = 0;
int err = bus_dmamap_load(sc->dma_tag, sc->dma_map,
    sc->dma_vaddr, DMA_BUFFER_SIZE,
    myfirst_dma_single_map, &bus_addr, BUS_DMA_NOWAIT);
if (err != 0) {
	device_printf(sc->dev, "bus_dmamap_load failed: %d\n", err);
	return (err);
}
KASSERT(bus_addr != 0, ("single-segment callback did not set address"));
sc->dma_bus_addr = bus_addr;
```

El patrón es tan habitual que muchos drivers definen exactamente este helper y lo nombran de forma similar. `/usr/src/sys/dev/re/if_re.c` lo llama `re_dma_map_addr`. `/usr/src/sys/dev/nvme/nvme_private.h` lo llama `nvme_single_map`. El driver del Capítulo 21 lo llamará `myfirst_dma_single_map`. El cuerpo de la función es casi idéntico en todos los drivers.

El flag `BUS_DMA_NOWAIT` en la llamada de carga le dice al kernel «no diferir; si los bounce buffers no están disponibles, devuelve `ENOMEM` inmediatamente». Este es el caso habitual para los drivers que prefieren aceptar un error antes que esperar. Un driver que quiere esperar pasa `BUS_DMA_WAITOK` (o `0`, ya que `WAITOK` tiene el valor cero por defecto); en ese caso, si la carga se difiere, el driver debe retornar del llamador y asumir que el callback se ejecutará más tarde.

Cuando la carga se difiere y se ejecuta más tarde, el kernel puede necesitar mantener un lock a nivel de driver mientras el callback se ejecuta. Para eso sirven los parámetros `lockfunc` y `lockfuncarg` de `bus_dma_tag_create`: el driver proporciona una función a la que el kernel llama con `BUS_DMA_LOCK` antes del callback y `BUS_DMA_UNLOCK` después. FreeBSD proporciona `busdma_lock_mutex` como implementación lista para usar en drivers que protegen su estado DMA con un mutex estándar; el driver pasa el puntero al mutex como `lockfuncarg`. Para los drivers que nunca difieren cargas (porque utilizan `BUS_DMA_NOWAIT` en todas partes, o porque solo cargan en el momento del attach sin retener ningún lock), el kernel proporciona `_busdma_dflt_lock`, que provoca un panic si se llama; ese es el valor predeterminado más seguro porque convierte un bug silencioso de threading en uno ruidoso.

El driver del Capítulo 21 carga exactamente una vez en el momento del attach, sin contención de locks, con `BUS_DMA_NOWAIT`. La lockfunc no importa porque el callback siempre se ejecuta de forma síncrona. La Sección 3 pasa `NULL` tanto para `lockfunc` como para `lockfuncarg`; el kernel sustituye `_busdma_dflt_lock` automáticamente, y el panic ante una llamada inesperada actúa como red de seguridad.

### Jerarquías de tags y herencia

La Sección 1 mencionó que un tag hereda de un tag padre y que la jerarquía refleja el hardware. La Sección 2 es el lugar donde concretar esto.

Cuando un dispositivo PCI está conectado a un puente PCI, el propio tag del puente lleva las restricciones que el puente impone al tráfico DMA: las direcciones DMA que puede enrutar, el alineamiento que exige, si hay un IOMMU delante. El kernel crea este tag en el momento del attach del bus, y un driver puede recuperarlo para su propio dispositivo con:

```c
bus_dma_tag_t parent = bus_get_dma_tag(sc->dev);
```

El tag devuelto es el **tag DMA padre** del dispositivo. Cuando el driver crea su propio tag, pasa el padre:

```c
int err = bus_dma_tag_create(parent,
    /* alignment */    4,
    /* boundary */     0,
    /* lowaddr */      BUS_SPACE_MAXADDR,
    /* highaddr */     BUS_SPACE_MAXADDR,
    /* filtfunc */     NULL,
    /* filtfuncarg */  NULL,
    /* maxsize */      DMA_BUFFER_SIZE,
    /* nsegments */    1,
    /* maxsegsize */   DMA_BUFFER_SIZE,
    /* flags */        0,
    /* lockfunc */     NULL,
    /* lockfuncarg */  NULL,
    &sc->dma_tag);
```

El hijo hereda las restricciones del padre; el hijo solo puede añadir más restricciones, nunca eliminarlas. Si el padre dice «este puente no puede direccionar por encima de 4 GB», el hijo no puede decir «en realidad mi dispositivo sí puede»; el kernel toma la intersección. Esto es lo que permite a un driver escribir código portable: el driver describe únicamente las restricciones propias de su dispositivo, y el kernel las compone con lo que imponga la plataforma.

Los drivers que necesitan describir múltiples grupos de transferencias (por ejemplo, un alineamiento para los anillos de descriptores y otro distinto para los buffers de datos) crean múltiples tags, todos heredando del mismo padre. El driver `if_re(4)` es un ejemplo claro: crea `rl_parent_tag` a partir del puente, y luego `rl_tx_mtag` y `rl_rx_mtag` (para las cargas útiles de mbuf, alineadas a 1 byte, multisegmento) y `rl_tx_list_tag` y `rl_rx_list_tag` (para los anillos de descriptores, alineados según `RL_RING_ALIGN`, de un solo segmento), todos heredando de `rl_parent_tag`. Cada tag describe un conjunto diferente de buffers; la jerarquía los compone.

El driver del Capítulo 21 utiliza un único tag porque tiene un único tipo de buffer. Cuando el lector extienda el driver con anillos de descriptores, la evolución natural es añadir un segundo tag; los ejercicios de desafío de la Sección 8 esbozan ese paso.

### Una línea de tiempo simple de hardware a software

Para afianzar el vocabulario, aquí tienes una línea de tiempo para una transferencia DMA completa en el dispositivo del Capítulo 21, con cada línea anotada con el objeto y la llamada de API involucrados.

1. **En el attach, una sola vez.** Crear el tag mediante `bus_dma_tag_create`. Asignar memoria y mapa implícito mediante `bus_dmamem_alloc`. Cargar el mapa mediante `bus_dmamap_load` con un callback de segmento único. Registrar la dirección de bus en el softc. Resultado: `sc->dma_tag`, `sc->dma_vaddr`, `sc->dma_map`, `sc->dma_bus_addr`, todos inicializados.

2. **Primera transferencia, del host al dispositivo:**
   - Rellenar `sc->dma_vaddr` con el patrón a enviar.
   - Llamar a `bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_PREWRITE)`.
   - Escribir `DMA_ADDR_LOW`, `DMA_ADDR_HIGH`, `DMA_LEN`, `DMA_DIR=WRITE` mediante llamadas a `bus_write_4`.
   - Escribir `DMA_CTRL = START`.
   - Esperar la interrupción de finalización.
   - En el filtro rx, leer `DMA_STATUS`; si es `DONE`, encolar la tarea.
   - En la tarea, llamar a `bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_POSTWRITE)`.
   - Actualizar las estadísticas.

3. **Segunda transferencia, de dispositivo a host:**
   - Llama a `bus_dmamap_sync(..., BUS_DMASYNC_PREREAD)`.
   - Escribe `DMA_DIR=READ`, `DMA_CTRL=START`.
   - Espera la interrupción.
   - En la tarea, llama a `bus_dmamap_sync(..., BUS_DMASYNC_POSTREAD)`.
   - Lee `sc->dma_vaddr` y verifica el patrón.

4. **En el momento del detach, una sola vez.** Descarga el mapa mediante `bus_dmamap_unload`. Libera la memoria mediante `bus_dmamem_free` (que también libera el mapa implícito). Destruye el tag mediante `bus_dma_tag_destroy`.

La línea de tiempo tiene cuatro ciclos de sincronización: PREWRITE/POSTWRITE para las transferencias de host a dispositivo, y PREREAD/POSTREAD para las de dispositivo a host. Cada ciclo es una transferencia. Cada ciclo tiene exactamente una llamada de sincronización antes y otra después; ninguna puede omitirse. Entre el sync PRE y el sync POST, la CPU no puede tocar el buffer (el dispositivo es su dueño). Antes del sync PRE y después del sync POST, la CPU es dueña del buffer con total libertad.

El modelo mental de que «la propiedad pasa entre la CPU y el dispositivo, y las llamadas de sincronización marcan cada traspaso» es la intuición más útil que puedes llevarte de la Sección 2. Todos los drivers reales respetan esta disciplina de propiedad, incluso en plataformas coherentes donde las llamadas de sincronización resultan ser baratas o no-ops.

### Ejercicio: recorre una configuración DMA de ejemplo sin un dispositivo real

Un ejercicio en papel antes de la sección 3. Imagina un dispositivo sencillo con capacidad DMA y las siguientes características:

- Un dispositivo PCI con un BAR MMIO.
- Un motor DMA con estos registros: `DMA_ADDR_LOW` en el desplazamiento 0x20, `DMA_ADDR_HIGH` en 0x24, `DMA_LEN` en 0x28, `DMA_DIR` en 0x2C (0 = host a dispositivo, 1 = dispositivo a host), `DMA_CTRL` en 0x30 (escribir 1 = iniciar), `DMA_STATUS` en 0x34 (bit 0 = completado, bit 1 = error).
- Se necesita un buffer de transferencia de 4 KB.
- Motor DMA de 32 bits (no puede acceder a memoria por encima de 4 GB).
- Se requiere alineación de 16 bytes.
- El buffer no debe cruzar un límite de 64 KB.

Escribe en papel la llamada exacta a `bus_dma_tag_create` que haría el driver. Identifica cada uno de los catorce parámetros e indica qué valor concreto usarías para cada uno. Luego escribe las llamadas a `bus_dmamem_alloc` y `bus_dmamap_load` que vienen a continuación. No escribas en C; escribe en prosa.

Una respuesta de ejemplo (para comparar):

- Etiqueta padre: `bus_get_dma_tag(sc->dev)`, la etiqueta padre PCI.
- Alineación: 16.
- Límite: 65536.
- Dirección baja (`lowaddr`): `BUS_SPACE_MAXADDR_32BIT`. Recuerda el confuso nombre: `lowaddr` es la dirección más alta a la que el dispositivo puede llegar, no un límite inferior. Para un motor de 32 bits, cualquier dirección por encima de `BUS_SPACE_MAXADDR_32BIT` debe quedar excluida, por lo que ese valor va aquí.
- Dirección alta (`highaddr`): `BUS_SPACE_MAXADDR`. Es el límite superior de la ventana de exclusión; pasar `BUS_SPACE_MAXADDR` extiende la exclusión hasta el infinito, lo que expresa correctamente que nada por encima de 4 GB es accesible.
- Función de filtro: NULL (no se necesita, el código moderno las evita).
- Argumento del filtro: NULL.
- Tamaño máximo: 4096.
- Nsegmentos: 1 (se requiere un único segmento).
- Tamaño máximo de segmento: 4096.
- Flags: 0.
- Lockfunc: NULL.
- Lockfuncarg: NULL.
- Puntero a la etiqueta: `&sc->dma_tag`.

Luego `bus_dmamem_alloc` con `BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO` para asignar el buffer como coherente, inicializado a cero y con bloqueo del proceso permitido (momento de attach). Luego `bus_dmamap_load` con `BUS_DMA_NOWAIT`, el callback de segmento único y `&sc->dma_bus_addr` como argumento del callback.

Escribir esto en papel antes de ver el código hace que la explicación detallada de la sección 3 parezca una confirmación en lugar de una introducción. El ejercicio lleva diez minutos y merece la pena.

### Cerrando la sección 2

La sección 2 le ha dado al lector el vocabulario: tag, map, memoria, segmento, PRE, POST, READ, WRITE, estático, dinámico, padre, callback. Las piezas encajan siguiendo un patrón concreto: en el momento de attach, crear una etiqueta, asignar memoria, cargar un map, registrar la dirección de bus; en el momento de la transferencia, sincronizar PRE, activar el dispositivo, esperar, sincronizar POST, leer los datos; en el momento de detach, descargar el map, liberar la memoria, destruir la etiqueta. El patrón es el mismo en todos los drivers con capacidad DMA de FreeBSD; el vocabulario es el mismo en todos los capítulos de este libro a partir de ahora.

La sección 3 es el primer código funcional. El driver crea una etiqueta, asigna memoria, la carga y verifica que la dirección de bus sea correcta. Todavía no se producen transferencias; el objetivo es ejercitar los caminos de configuración y desmontaje bajo `WITNESS` y confirmar que el driver puede levantar y desmontar una región DMA de forma limpia a lo largo de muchos ciclos de kldload/kldunload. Ese par de operaciones es la base sobre la que cada sección posterior construye.



## Sección 3: asignación y mapeo de memoria DMA

La sección 2 construyó el vocabulario. La sección 3 escribe el código. La primera etapa funcional del driver del capítulo 21 crea una etiqueta DMA, asigna un único buffer de cuatro kilobytes, lo carga en un map, registra la dirección de bus y añade el camino de configuración y desmontaje a las secuencias de attach y detach. Todavía no se producen transferencias DMA. El objetivo es ejercitar el camino de asignación, observar cómo los nuevos campos del softc se rellenan con `kldload`, confirmar que `kldunload` limpia sin quejas y repetir el proceso varias veces bajo `WITNESS` para detectar cualquier violación de orden de lock.

La etiqueta de versión de la etapa 1 es `1.4-dma-stage1`. El objetivo de compilación tras la etapa es `myfirst.ko`. Todavía no se añaden archivos nuevos; la creación de la etiqueta, la asignación y el desmontaje residen en `myfirst.c` y `myfirst_pci.c` junto a la lógica de attach existente. La sección 8 mueve el código DMA a su propio archivo; aquí mantenemos todo en un único lugar para que el alcance de la etapa sea visible de un solo vistazo.

### Nuevos campos del softc

El softc del driver crece en cuatro campos:

```c
/* In myfirst.h, inside struct myfirst_softc. */
bus_dma_tag_t       dma_tag;
bus_dmamap_t        dma_map;
void               *dma_vaddr;
bus_addr_t          dma_bus_addr;
```

`dma_tag` es la etiqueta creada en el momento de attach. `dma_map` es el map devuelto por `bus_dmamem_alloc`. `dma_vaddr` es la dirección virtual del kernel que usa la CPU para acceder al buffer. `dma_bus_addr` es la dirección de bus que usa el dispositivo.

Los campos siguen la convención de nombres de los capítulos 17 y 18: minúsculas, guiones bajos, nombres cortos. El driver consigue su inicialización a cero apoyándose en la garantía de `device_get_softc` de rellenar con ceros al asignar el softc en el momento de attach. No se necesitan asignaciones explícitas `= 0`.

El softc incluye `<machine/bus.h>` y `<sys/bus_dma.h>` (este último a través de `<machine/bus.h>`); el capítulo 18 ya los incorporó para el trabajo con `bus_space`, así que los includes ya están presentes y no se necesitan nuevos.

### El callback de segmento único

Antes de la función de configuración, hay una función auxiliar. Esta función auxiliar es el callback de segmento único introducido en la sección 2:

```c
static void
myfirst_dma_single_map(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *addr = arg;

	if (error != 0) {
		printf("myfirst: dma load callback error %d\n", error);
		return;
	}

	KASSERT(nseg == 1, ("myfirst: unexpected DMA segment count %d", nseg));
	*addr = segs[0].ds_addr;
}
```

La función sigue el mismo patrón que `re_dma_map_addr` en `/usr/src/sys/dev/re/if_re.c`. Pásale un puntero a un `bus_addr_t`; si tiene éxito, el callback escribe la dirección de bus del único segmento en el destino. El `KASSERT` confirma que hay exactamente un segmento; `bus_dmamem_alloc` siempre devuelve un único segmento, así que esta aserción es una red de seguridad más que una comprobación en tiempo de ejecución.

El callback está marcado como `static` porque no se usa fuera de `myfirst.c`. No toma ningún estado más allá del puntero a la dirección de bus, por lo que es thread-safe por construcción y puede ejecutarse en cualquier contexto que elija el kernel.

### La función de configuración

La función de configuración de la etapa 1, llamada `myfirst_dma_setup`, reside en `myfirst.c`. Realiza las cuatro operaciones en orden: crear etiqueta, asignar memoria, cargar map, registrar la dirección de bus. Cada paso comprueba si hay errores y deshace los pasos anteriores en caso de fallo. La función devuelve `0` en caso de éxito, un código de error distinto de cero en caso de fallo, y deja los campos DMA del softc en un estado consistente en ambos casos.

```c
#define	MYFIRST_DMA_BUFFER_SIZE	4096u

int
myfirst_dma_setup(struct myfirst_softc *sc)
{
	int err;

	err = bus_dma_tag_create(bus_get_dma_tag(sc->dev),
	    /* alignment */    4,
	    /* boundary */     0,
	    /* lowaddr */      BUS_SPACE_MAXADDR,
	    /* highaddr */     BUS_SPACE_MAXADDR,
	    /* filtfunc */     NULL,
	    /* filtfuncarg */  NULL,
	    /* maxsize */      MYFIRST_DMA_BUFFER_SIZE,
	    /* nsegments */    1,
	    /* maxsegsz */     MYFIRST_DMA_BUFFER_SIZE,
	    /* flags */        0,
	    /* lockfunc */     NULL,
	    /* lockfuncarg */  NULL,
	    &sc->dma_tag);
	if (err != 0) {
		device_printf(sc->dev, "bus_dma_tag_create failed: %d\n", err);
		return (err);
	}

	err = bus_dmamem_alloc(sc->dma_tag, &sc->dma_vaddr,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO,
	    &sc->dma_map);
	if (err != 0) {
		device_printf(sc->dev, "bus_dmamem_alloc failed: %d\n", err);
		bus_dma_tag_destroy(sc->dma_tag);
		sc->dma_tag = NULL;
		return (err);
	}

	sc->dma_bus_addr = 0;
	err = bus_dmamap_load(sc->dma_tag, sc->dma_map,
	    sc->dma_vaddr, MYFIRST_DMA_BUFFER_SIZE,
	    myfirst_dma_single_map, &sc->dma_bus_addr,
	    BUS_DMA_NOWAIT);
	if (err != 0 || sc->dma_bus_addr == 0) {
		device_printf(sc->dev, "bus_dmamap_load failed: %d\n", err);
		bus_dmamem_free(sc->dma_tag, sc->dma_vaddr, sc->dma_map);
		sc->dma_vaddr = NULL;
		bus_dma_tag_destroy(sc->dma_tag);
		sc->dma_tag = NULL;
		return (err != 0 ? err : ENOMEM);
	}

	device_printf(sc->dev,
	    "DMA buffer %zu bytes at KVA %p bus addr %#jx\n",
	    (size_t)MYFIRST_DMA_BUFFER_SIZE,
	    sc->dma_vaddr, (uintmax_t)sc->dma_bus_addr);

	return (0);
}
```

Algunos detalles merecen atención.

**Las constantes.** `MYFIRST_DMA_BUFFER_SIZE` es un `#define` con el prefijo del capítulo, siguiendo la convención de nombres del capítulo 17. Se usa tres veces en la función de configuración (en el `maxsize` de la etiqueta, en el `maxsegsz` de la etiqueta y en la longitud del buffer de la carga), por lo que un nombre simbólico vale la pena aunque el valor sea pequeño. El capítulo 21 usa 4 KB porque coincide con el tamaño de una página y evita casos esquina de los bounce buffers; la sección 7 retoma esta elección.

**La etiqueta padre.** `bus_get_dma_tag(sc->dev)` devuelve la etiqueta DMA padre heredada del puente PCI. El kernel combina las restricciones del padre con las del propio driver, de modo que la etiqueta hija respeta automáticamente cualquier límite del nivel del puente. En amd64 con IOMMU, la etiqueta padre también lleva los requisitos de mapeo del IOMMU; la etiqueta del driver los hereda de forma transparente.

**La alineación.** 4 bytes es suficiente para la dirección de inicio DMA de un motor de 32 bits de ancho. La hoja de datos de un dispositivo real especificaría un valor (NVMe usa el tamaño de página, `if_re` usa el tamaño del descriptor); el motor simulado del capítulo 21 se conforma con 4.

**El límite.** 0 significa que no hay restricción de límite. Un dispositivo que no puede cruzar límites de 64 KB (algunos motores DMA tienen esta limitación porque sus contadores internos son de 16 bits) pasaría 65536.

**Las direcciones.** `BUS_SPACE_MAXADDR` tanto para la dirección baja como para la alta significa sin ventana de exclusión. La simulación no tiene límite de rango de direcciones; un dispositivo real limitado a 32 bits pasaría `BUS_SPACE_MAXADDR_32BIT` para `lowaddr` y así excluiría las direcciones por encima de 4 GB. La sección 7 retoma esto con una explicación detallada y concreta.

**El tamaño y el número de segmentos.** Un único buffer de 4 KB que debe ser físicamente contiguo. `nsegments = 1` junto con `maxsegsz = MYFIRST_DMA_BUFFER_SIZE` implica un mapeo de un solo segmento; `bus_dmamem_alloc` siempre devuelve un único segmento, así que esto encaja.

**Los flags.** `0` en el momento de crear la etiqueta (sin flags especiales de etiqueta). `BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO` en el momento de `bus_dmamem_alloc`: el asignador puede bloquear el proceso si es necesario, la memoria debe ser coherente con la caché del dispositivo (importante en arm y arm64; solo una sugerencia en amd64) y el buffer debe inicializarse a cero. `BUS_DMA_NOWAIT` en el momento de `bus_dmamap_load`: la carga no debe diferirse; si los bounce buffers no están disponibles, la carga debe fallar de inmediato. Para el uso del capítulo 21 de una sola carga en el momento de attach, la distinción no importa en la práctica; la convención es usar `NOWAIT` en cualquier lugar donde el driver tenga un camino de error limpio, porque hace que el flujo de control sea más fácil de razonar.

**El lockfunc.** `NULL` significa por defecto. Con la única carga en el momento de attach del capítulo 21, el callback siempre se ejecuta de forma síncrona, así que el lockfunc nunca se invoca; el valor por defecto (`_busdma_dflt_lock`, que entra en pánico si se llama) es la red de seguridad correcta.

**El manejo de errores.** Cada paso comprueba el valor de retorno y deshace los pasos anteriores en caso de fallo. El orden importa: la etiqueta se creó primero, por lo que se destruye al final; la memoria se asignó en segundo lugar, por lo que se libera en penúltimo lugar. Los campos del softc se ponen a NULL tras liberar para que detach pueda recorrerlos de forma segura tanto si la configuración falló parcialmente como si se completó.

**El `device_printf` final.** Una única línea de aviso registra el tamaño del buffer, la dirección virtual del kernel y la dirección de bus. La línea es útil en `dmesg` y sirve también como marcador de prueba de regresión: un driver funcional la imprime; una configuración rota no.

### La función de desmontaje

La función de desmontaje asociada es:

```c
void
myfirst_dma_teardown(struct myfirst_softc *sc)
{
	if (sc->dma_bus_addr != 0) {
		bus_dmamap_unload(sc->dma_tag, sc->dma_map);
		sc->dma_bus_addr = 0;
	}
	if (sc->dma_vaddr != NULL) {
		bus_dmamem_free(sc->dma_tag, sc->dma_vaddr, sc->dma_map);
		sc->dma_vaddr = NULL;
		sc->dma_map = NULL;
	}
	if (sc->dma_tag != NULL) {
		bus_dma_tag_destroy(sc->dma_tag);
		sc->dma_tag = NULL;
	}
}
```

La función está protegida por comprobaciones de valor cero para que sea seguro llamarla incluso cuando la configuración no se completó. Es el mismo patrón que usa el desmontaje de interrupciones del capítulo 19, y por la misma razón: detach puede ejecutarse tras un fallo parcial de attach, y el código de limpieza no debe asumir que ningún prefijo concreto de la configuración tuvo éxito.

El orden es el inverso al de la configuración:

1. Descargar el map. Es seguro llamarlo incluso si la carga tuvo éxito; es idempotente.
2. Liberar la memoria y el map implícito. `bus_dmamem_free` libera tanto el buffer como el objeto map devuelto por `bus_dmamem_alloc`; el driver nunca llama a `bus_dmamap_destroy` en este camino.
3. Destruir la etiqueta. Devuelve `EBUSY` si algún map sigue asociado; la descarga anterior lo impide.

La llamada a `bus_dmamap_unload` es sutil. Tras `bus_dmamem_alloc`, el map está cargado implícitamente (esto es lo que la página de manual llama "An initial load operation is required to obtain the bus address"). La carga ocurre bien implícitamente, bien mediante la llamada explícita a `bus_dmamap_load` que ya hizo el driver. En cualquier caso, el map está en estado cargado tras la configuración; debe descargarse antes de llamar a `bus_dmamem_free`. El patrón del capítulo usa una llamada explícita a `bus_dmamap_load` para exponer el callback y la dirección de bus; `bus_dmamap_unload` es su desmontaje asociado.

El lector que tenga curiosidad sobre por qué hay que llamar manualmente a `bus_dmamap_load` después de `bus_dmamem_alloc` puede leer la respuesta en la página de manual `bus_dma(9)`: la sección "STATIC TRANSACTIONS" indica explícitamente "An initial load operation is required to obtain the bus address of the allocated memory". El asignador devuelve una KVA válida y un objeto map, pero la dirección de bus en sí solo está disponible a través del callback tras la carga. Es una peculiaridad histórica; la recuerdas una vez y el patrón encaja para cualquier driver estático.

### Integración de la configuración y el desmontaje en attach y detach

La función de attach del capítulo 20 terminaba con la configuración de MSI-X. El capítulo 21 inserta la configuración DMA justo antes, de modo que la etiqueta y el buffer están listos cuando el dispositivo empieza a generar interrupciones:

```c
/* ... earlier attach work: BAR allocation, hw layer, cdev ... */

err = myfirst_dma_setup(sc);
if (err != 0)
	goto fail_dma;

err = myfirst_msix_setup(sc);
if (err != 0)
	goto fail_msix;

/* ... remaining attach work ... */

return (0);

fail_msix:
	myfirst_dma_teardown(sc);
fail_dma:
	/* ... earlier failure labels ... */
```

Las etiquetas exactas dependen de la estructura actual del driver, que es la estructura del capítulo 20 tras la etapa 4. El principio es el siguiente: la configuración del DMA se realiza después de que el BAR está mapeado (porque los registros del dispositivo deben ser accesibles para poder desencadenar transferencias en algún momento), después de que se construye la capa de hardware, pero antes de que se ejecute la configuración de interrupciones. En caso de fallo, el driver deshace los pasos saltando a las etiquetas en orden inverso.

La función detach sigue el orden inverso de attach. El desmontaje de MSI-X se ejecuta primero (para detener cualquier interrupción adicional), luego se ejecuta el desmontaje de DMA y, después, la limpieza de los elementos anteriores. Este orden garantiza que ninguna interrupción pueda dispararse en el camino del DMA una vez destruido el tag, lo que constituiría un use-after-free.

### Ejecución del Stage 1

Compila y carga:

```sh
make clean && make
kldload ./myfirst.ko
```

Observa `dmesg`:

```text
myfirst0: <myfirst DMA test device> ...
myfirst0: DMA buffer 4096 bytes at KVA 0xfffffe0010af9000 bus addr 0x10af9000
myfirst0: interrupt mode: MSI-X, 3 vectors
```

La dirección KVA y la dirección de bus variarán en tu máquina; lo que importa es el formato. La KVA es una dirección virtual del kernel típica (muy por encima de la base del mapa directo en amd64). La dirección de bus en amd64 con IOMMU deshabilitado suele ser igual a la dirección física; con IOMMU habilitado, es una dirección reasignada por el IOMMU, a menudo mucho más baja que cualquier dirección física que pudiera respaldarla.

Verifica los campos del softc mediante sysctl o un `device_printf` en una compilación de depuración:

```sh
sysctl dev.myfirst.0
```

Descarga el módulo:

```sh
kldunload myfirst
```

`dmesg` debería mostrar el detach:

```text
myfirst0: detaching
```

Sin pánico, sin queja de `WITNESS`, sin aviso de `VFS` por pérdida de memoria. Si `WITNESS` está habilitado y la configuración o el desmontaje del driver adquieren un lock que `bus_dma` también adquiere, la salida mostraría un aviso de orden de lock; un `kldunload` limpio es la confirmación de que el locking es correcto.

Repite el ciclo de carga/descarga varias veces. Las fugas de memoria relacionadas con DMA son raras, pero catastróficas cuando ocurren (una fuga en `bus_dma` pierde páginas, no solo bytes); merece la pena ejecutar la comprobación de regresión cincuenta veces seguidas con un bucle `for` y confirmar que `vmstat -m` muestra que `bus_dmamap` y `bus_dma_tag` regresan a sus conteos en reposo.

### Qué hace y qué no hace el Stage 1

El Stage 1 ejercita las rutas de asignación y limpieza. No realiza ninguna transferencia DMA; `dma_vaddr` es un buffer sin usar (salvo su inicialización a cero), y `dma_bus_addr` es una dirección sin usar. Las siguientes secciones pondrán ambos en funcionamiento.

Lo que demuestra el Stage 1 es que:

- El driver puede crear un tag que hereda del padre PCI.
- El driver puede asignar un buffer de 4 KB coherente e inicializado a cero.
- El driver puede obtener la dirección de bus mediante el callback.
- El driver puede desmontar todo de forma limpia.
- El ciclo puede ejecutarse muchas veces sin fugas de recursos.

Es una base sólida. El resto del capítulo se construye sobre ella.

### Errores comunes en esta etapa

Cinco errores son comunes en el Stage 1. Cada uno es fácil de cometer y fácil de corregir una vez identificado.

**Error uno: olvidar llamar a `bus_dmamap_load` después de `bus_dmamem_alloc`.** El síntoma es que `dma_bus_addr` se queda en cero; programar el dispositivo con la dirección de bus cero o bien corrompe la memoria silenciosamente o provoca un fallo del IOMMU. La solución es llamar a `bus_dmamap_load` explícitamente, como hace el código del Stage 1. El `KASSERT(bus_addr != 0, ...)` del driver detecta esto durante el desarrollo.

**Error dos: pasar un `maxsize` no alineado a `bus_dma_tag_create`.** El parámetro `alignment` debe ser una potencia de dos, y `maxsize` debe ser al menos igual a `alignment`. Un tamaño de 3 bytes con alineación 4 produce un error. El tamaño de 4096 bytes del capítulo con alineación 4 está correctamente alineado.

**Error tres: usar `BUS_DMA_NOWAIT` con `bus_dmamem_alloc` e ignorar el error.** `BUS_DMA_NOWAIT` hace que el asignador devuelva `ENOMEM` si la memoria escasea; un driver que ignora el error y continúa con desreferencias `NULL` provoca un pánico inmediatamente. Usa `BUS_DMA_WAITOK` en el momento del attach (donde dormir es seguro) y comprueba siempre el valor de retorno.

**Error cuatro: olvidar poner a cero la dirección de bus antes de la llamada de carga.** Si la carga falla, el callback nunca se llama y `dma_bus_addr` retiene cualquier basura que hubiera allí. El código del capítulo asigna `sc->dma_bus_addr = 0` antes de llamar a `bus_dmamap_load`, de modo que la comprobación posterior a la carga (`if (err != 0 || sc->dma_bus_addr == 0)`) es fiable.

**Error cinco: omitir la destrucción del tag en el desmontaje por la ruta de error.** El desmontaje en el código del capítulo destruye el tag en cada rama de fallo. Un driver que falla en `bus_dmamem_alloc` y retorna sin destruir el tag pierde un tag en cada attach fallido; tras muchos reintentos, esto se acumula. El patrón es: ante cualquier fallo que ocurra después de que `bus_dma_tag_create` haya tenido éxito, destruye el tag antes de retornar.

Ninguno de estos errores provoca un pánico inmediato; todos producen bugs sutiles que se manifiestan bajo carga o tras ciclos repetidos de carga/descarga. Ejecutar el Stage 1 cincuenta veces con `WITNESS` e `INVARIANTS` habilitados detecta la mayoría de ellos.

### Una nota sobre la API de plantillas de `bus_dma`

FreeBSD 13 y versiones posteriores también proporcionan una API basada en plantillas para crear tags DMA, documentada en el manual `bus_dma(9)` bajo `bus_dma_template_*`. La API de plantillas es una alternativa ergonómica a la lista de catorce parámetros de `bus_dma_tag_create`: el driver inicializa una plantilla a partir de un tag padre, sobreescribe campos individuales con macros `BD_*` y construye el tag con `bus_dma_template_tag`.

Una configuración de Stage 1 basada en plantillas tendría este aspecto:

```c
bus_dma_template_t t;

bus_dma_template_init(&t, bus_get_dma_tag(sc->dev));
BUS_DMA_TEMPLATE_FILL(&t,
    BD_ALIGNMENT(4),
    BD_MAXSIZE(MYFIRST_DMA_BUFFER_SIZE),
    BD_NSEGMENTS(1),
    BD_MAXSEGSIZE(MYFIRST_DMA_BUFFER_SIZE));
err = bus_dma_template_tag(&t, &sc->dma_tag);
```

La API de plantillas es preferible en drivers nuevos porque hace explícitos los campos que se modifican. El capítulo 21 usa el clásico `bus_dma_tag_create` en su ejemplo continuo porque es lo que usa la mayor parte del árbol de código fuente de FreeBSD existente, y si sabes leer una llamada de catorce parámetros, siempre podrás traducirla a la forma de plantilla. El ejercicio de refactorización de la sección 8 sugiere probar la API de plantillas como variación estilística.

### Cerrando la sección 3

La sección 3 llevó el driver del capítulo 21 al Stage 1: un tag, un map, un buffer, una dirección de bus y un desmontaje limpio. Aún no se producen transferencias; la ruta de asignación es lo único que se ejercita. Es suficiente para confirmar que las piezas encajan, que el tag padre se hereda correctamente, que el callback de segmento único puebla la dirección de bus y que los ciclos de `kldload`/`kldunload` no producen fugas.

La sección 4 es la disciplina de sincronización. Antes de que el driver entregue el buffer a un motor DMA, necesita saber exactamente qué llamadas de sincronización ocurren en qué momento, por qué existe cada una y qué saldría mal si se omitiera alguna. La sección 4 es un recorrido detallado por `bus_dmamap_sync`, con diagramas concretos y el modelo mental que la sección 5 utiliza después para construir el motor simulado.



## Sección 4: Sincronización y uso de buffers DMA

La sección 3 produjo un driver capaz de asignar y liberar un buffer DMA. La sección 4 enseña la disciplina que toda transferencia real debe respetar: cuándo llamar a `bus_dmamap_sync`, con qué flag y por qué. La disciplina es lo que mantiene coherente la visión de la memoria del CPU y del dispositivo. En esta sección no se escribe código nuevo (el motor simulado que consumirá las llamadas de sincronización se construye en la sección 5), pero el modelo mental que aquí se establece es del que depende cada sección posterior.

### El modelo de propiedad

La intuición más útil para el DMA es esta: en cualquier momento, o bien el CPU es propietario del buffer o bien lo es el dispositivo. La propiedad va y viene a través de las llamadas a `bus_dmamap_sync`. Entre una sincronización PRE y la siguiente sincronización POST, el dispositivo es propietario del buffer y el CPU no debe tocarlo. Entre una sincronización POST y la siguiente sincronización PRE, el CPU es propietario del buffer y el dispositivo no debe tocarlo. El trabajo del driver es hacer explícita cada frontera de propiedad con una llamada de sincronización.

La propiedad es física, no lógica. No significa que el CPU o el dispositivo "conozcan" el buffer en algún sentido abstracto; significa que las cachés del CPU y el motor de bus del dispositivo pueden observar realmente contenidos diferentes para la misma memoria en el mismo instante, y las llamadas de sincronización son la oportunidad del kernel de reconciliar esa diferencia. En una plataforma totalmente coherente (amd64 moderno con un root complex PCIe coherente y sin pares DMA no coherentes), el hardware reconcilia la diferencia de forma transparente y `bus_dmamap_sync` es a menudo una barrera de memoria o un no-op. En una plataforma parcialmente coherente (algunos sistemas arm64, algunas configuraciones RISC-V, cualquier cosa con `BUS_DMA_COHERENT` no respetado en la asignación), las llamadas de sincronización realizan vaciados e invalidaciones de caché. El driver escribe el mismo código independientemente; el kernel hace lo correcto.

Un driver que respeta las fronteras de propiedad es portable. Un driver que omite las llamadas de sincronización "porque mi sistema amd64 funciona sin ellas por casualidad" no lo es; el mismo driver corromperá datos en arm64 o en un entorno virtualizado donde el IOMMU del hipervisor impone su propia política de coherencia.

### Los cuatro flags de sincronización en profundidad

La sección 2 nombró los cuatro flags de sincronización. La sección 4 determina exactamente qué significa cada uno, qué hace el kernel con cada uno en diferentes plataformas y cuándo lo llama el driver en la práctica.

**`BUS_DMASYNC_PREREAD`.** La interpretación de que "el CPU ha terminado de tocar el buffer y el dispositivo está a punto de leer de la memoria del host" **no** es la intención aquí; el flag se refiere a las **escrituras del dispositivo en la memoria del host**, que el host después **lee**. El flag se llama PREREAD porque el *host* va a leer después de la operación; se llama PRE porque esto es antes del DMA. En plataformas no coherentes, el kernel invalida las copias en caché del CPU de las líneas de caché del buffer. La razón es que, después de que el dispositivo escriba datos nuevos en memoria, las líneas de caché obsoletas en el CPU harían que las lecturas posteriores vieran los valores antiguos. La invalidación garantiza que la siguiente lectura se obtenga desde memoria, donde aterrizarán las escrituras del dispositivo. En plataformas coherentes, la invalidación es innecesaria porque el hardware realiza el snooping de la caché automáticamente; el kernel implementa `PREREAD` como una barrera de memoria o un no-op.

**`BUS_DMASYNC_PREWRITE`.** El CPU acaba de escribir en el buffer y el dispositivo está a punto de leerlo. En plataformas no coherentes, el kernel vacía las líneas de caché sucias del CPU a memoria. La razón es que las escrituras del CPU podrían seguir estando en su caché; el dispositivo lee directamente desde memoria y vería contenidos obsoletos. El vaciado garantiza que el controlador de memoria tenga los datos actuales antes de la lectura del dispositivo. En plataformas coherentes, una barrera de memoria suele ser suficiente para imponer el orden; el protocolo de coherencia de caché gestiona el vaciado de forma implícita.

**`BUS_DMASYNC_POSTREAD`.** El dispositivo ha terminado de escribir en la memoria del host y el CPU está a punto de leer. En plataformas no coherentes, esta es a menudo la invalidación real de caché (en algunas implementaciones la invalidación ocurre en POST en lugar de en PRE); en plataformas coherentes, es una barrera o un no-op. En plataformas con bounce buffers, aquí es cuando el kernel copia los datos desde la región bounce de vuelta al buffer del driver. Por eso, incluso en plataformas coherentes no puede omitirse la llamada POSTREAD: el mecanismo de coherencia puede ser coherente, pero el mecanismo de bounce buffer no lo es, y la llamada POSTREAD es el gancho que el mecanismo bounce utiliza para hacer su trabajo.

**`BUS_DMASYNC_POSTWRITE`.** El dispositivo ha terminado de leer desde la memoria del host. En la mayoría de las plataformas esto es un no-op porque no hay ningún estado visible para el host que necesite cambiar (el CPU ya tenía los datos que escribió; el dispositivo ha terminado de leer y no volverá a leer; nada está desincronizado). En plataformas con bounce buffer, este es el gancho donde la región bounce puede devolverse al pool.

Tres observaciones que emergen tras las definiciones:

- El PRE y el POST de un par siempre se llaman ambos. `PREREAD` va seguido más adelante por `POSTREAD`; `PREWRITE` va seguido más adelante por `POSTWRITE`. Omitir cualquiera de ellos rompe el contrato.
- En plataformas coherentes, muchas de estas llamadas son baratas o gratuitas. Ese es un detalle de implementación del que el driver no depende. El driver las escribe porque son necesarias para la portabilidad y porque el soporte de bounce buffer solo es transparente si las llamadas están presentes.
- La semántica de PRE y POST es "desde el punto de vista del dispositivo": PRE significa "antes de que comience la operación del dispositivo"; POST significa "después de que se complete la operación del dispositivo". READ y WRITE son "desde el punto de vista del host": READ significa "el host va a leer el resultado"; WRITE significa "el host escribió el contenido".

### Los cuatro patrones de transferencia más comunes

Cuatro patrones cubren casi todas las transferencias DMA que realiza un driver. Cada uno tiene su propia secuencia PRE/POST, y la secuencia es predecible una vez que se reconoce el patrón.

**Patrón A: transferencia de datos del host al dispositivo.** El driver llena un buffer con datos y pide al dispositivo que los envíe.

```text
CPU fills buffer at KVA
bus_dmamap_sync(..., BUS_DMASYNC_PREWRITE)
program device with bus_addr, length, direction
write doorbell
wait for completion
(device has read the buffer)
bus_dmamap_sync(..., BUS_DMASYNC_POSTWRITE)
CPU may now reuse buffer
```

Ejemplo: un driver que transmite un paquete a una NIC. El driver copia el paquete en un buffer DMA, emite la sincronización PREWRITE, escribe el descriptor, activa el doorbell y espera la interrupción de transmisión completada. En la tarea de la interrupción, el driver emite la sincronización POSTWRITE y devuelve el buffer al pool libre.

**Patrón B: Transferencia de datos del dispositivo al host.** El driver solicita al dispositivo que deposite datos en un buffer; a continuación, el driver lee ese buffer.

```text
bus_dmamap_sync(..., BUS_DMASYNC_PREREAD)
program device with bus_addr, length, direction
write doorbell
wait for completion
(device has written the buffer)
bus_dmamap_sync(..., BUS_DMASYNC_POSTREAD)
CPU reads buffer at KVA
```

Ejemplo: un driver que recibe un paquete de una NIC. El driver entrega a la NIC la dirección de bus del buffer, emite la sincronización PREREAD, activa el doorbell y espera la interrupción de recepción completada. En la tarea de la interrupción, el driver emite la sincronización POSTREAD y procesa los datos recibidos.

**Patrón C: Entrada en el anillo de descriptores.** El driver escribe un descriptor que el dispositivo lee (detecta una nueva solicitud) y, más adelante, el dispositivo actualiza ese mismo descriptor con un campo de estado que el driver lee.

```text
CPU writes descriptor fields (bus_addr, length, status = PENDING)
bus_dmamap_sync(..., BUS_DMASYNC_PREWRITE)
device reads descriptor
device processes the transfer
device writes descriptor status = DONE
bus_dmamap_sync(..., BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE)
CPU reads descriptor status
```

El indicador POST combinado es la forma idiomática de emparejar la escritura del host con su posterior lectura cuando el host actúa a la vez como productor y consumidor de distintos campos en la misma región de memoria. El kernel realiza ambas operaciones en una sola llamada.

**Patrón D: Anillo compartido bidireccional.** El driver y el dispositivo comparten un anillo de descriptores de forma indefinida. Al configurar el anillo, ambas direcciones del flujo de datos deben sincronizarse.

```text
bus_dmamem_alloc returns the ring's memory
bus_dmamap_sync(..., BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE)
(ring is now live; both sides may read and write)
for each ring use:
    bus_dmamap_sync with the appropriate PRE
    doorbell / completion
    bus_dmamap_sync with the appropriate POST
```

Los drivers de producción como `if_re(4)` y `nvme(4)` emplean este patrón; la sincronización PRE combinada al configurar el anillo va seguida de pares PRE/POST por transacción. El driver de buffer único del capítulo 21 no utiliza este patrón; se menciona aquí por completitud y porque el lector lo encontrará en código real de drivers.

### La secuencia de sincronización de la transferencia simulada

El motor DMA simulado del capítulo 21 ejecutará transferencias tanto de host a dispositivo como de dispositivo a host. La secuencia de prueba que el driver realiza en la etapa 2 es:

```c
/* Host-to-device transfer: driver writes a pattern, engine reads it. */
memset(sc->dma_vaddr, 0xAA, MYFIRST_DMA_BUFFER_SIZE);
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_PREWRITE);
/* program engine */
CSR_WRITE_4(sc, MYFIRST_REG_DMA_DIR, MYFIRST_DMA_DIR_WRITE);
CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_START);
/* wait for completion interrupt */
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_POSTWRITE);

/* Device-to-host transfer: engine writes a pattern, driver reads it. */
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_PREREAD);
CSR_WRITE_4(sc, MYFIRST_REG_DMA_DIR, MYFIRST_DMA_DIR_READ);
CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_START);
/* wait for completion interrupt */
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_POSTREAD);
/* read sc->dma_vaddr and verify */
```

Dos transferencias, cuatro llamadas de sincronización, cuatro flags. El orden es el contrato de la prueba. La sección 5 integrará esto en el código del driver.

### Qué significan y qué no significan los flags de coherencia

`BUS_DMA_COHERENT` es un flag que aceptan tanto `bus_dma_tag_create` como `bus_dmamem_alloc`. Su significado difiere sutilmente entre ambos, y vale la pena entender esa diferencia para saber cuándo usar cada uno.

En `bus_dma_tag_create`, `BUS_DMA_COHERENT` es una **sugerencia arquitectónica** que indica que el driver está dispuesto a asumir un coste adicional en la asignación para reducir el coste en la sincronización. En arm64, el flag indica al asignador que coloque el buffer en un dominio de memoria que es inherentemente coherente con la ruta DMA, que puede ser una región reservada separada.

En `bus_dmamem_alloc`, `BUS_DMA_COHERENT` indica al asignador que produzca un buffer que no necesita vaciados de caché para la sincronización. En arm y arm64, esto utiliza una ruta de asignación diferente (memoria sin caché o de escritura combinada). En amd64, donde el root complex PCIe es coherente y el DMA se supervisa automáticamente mediante snooping, el flag tiene poco efecto pero se sigue pasando por portabilidad.

La regla: pasa siempre `BUS_DMA_COHERENT` en la asignación para anillos de descriptores y estructuras de control pequeñas que se encuentran en la ruta crítica tanto para el CPU como para el dispositivo. No te fíes del flag para eliminar la necesidad de `bus_dmamap_sync`; la sincronización sigue siendo obligatoria por el contrato de la API, aunque el flag la haga barata. Algunas implementaciones de arm64 sí requieren la sincronización incluso con `BUS_DMA_COHERENT` (el flag se denomina «sugerencia» en la página de manual por algo); el driver solo es portable si siempre se llama a la sincronización.

Para buffers de datos masivos (cargas útiles de paquetes, bloques de almacenamiento), generalmente no se pasa `BUS_DMA_COHERENT` porque la tasa de aciertos de caché es alta cuando el CPU está a punto de procesar los datos de todos modos, y el asignador coherente puede utilizar un dominio de memoria más lento. El capítulo 21 pasa `BUS_DMA_COHERENT` en su único buffer pequeño porque el objetivo del capítulo es mostrar el caso más habitual; un driver de producción tomaría la decisión por buffer en función del patrón de acceso.

### Qué ocurre si se omite una sincronización

Un escenario de fallo concreto aclara la abstracción. Supón que el driver realiza una transferencia de host a dispositivo pero olvida la sincronización `PREWRITE`:

```c
memset(sc->dma_vaddr, 0xAA, MYFIRST_DMA_BUFFER_SIZE);
/* MISSING: bus_dmamap_sync(..., BUS_DMASYNC_PREWRITE); */
CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_START);
```

En amd64 con un root complex coherente, la transferencia funciona: el hardware inspecciona mediante snooping la caché del CPU y el dispositivo lee el contenido actual. El fallo es invisible.

En arm64 sin un tejido PCIe coherente (algunos sistemas embebidos), la transferencia lee datos obsoletos: la escritura del CPU sigue en la caché, el controlador de memoria tiene datos antiguos y el dispositivo lee 0x00 o basura en lugar de 0xAA. El fallo es visible de inmediato como corrupción de datos.

En amd64 con bounce buffers (porque el dispositivo tenía un límite de dirección de 32 bits y el buffer resultó estar por encima de 4 GB), la transferencia lee datos obsoletos de la región de bounce: el mecanismo de bounce copia el buffer del driver en la región de bounce en el momento de PREWRITE, pero sin la llamada PREWRITE la región de bounce no está inicializada (cero, o con datos obsoletos de una transferencia anterior). El fallo es visible de inmediato como corrupción de datos.

El patrón se mantiene: la omisión hace que el driver funcione en la plataforma que probó el autor y falle en plataformas con semánticas de coherencia o direccionamiento diferentes. Las llamadas de sincronización son la garantía de portabilidad. Un driver que supera su suite de pruebas en amd64 y nunca ejercita las llamadas de sincronización tiene un fallo latente a la espera de un objetivo diferente.

### Contexto de lock y llamadas de sincronización

La disciplina de lock de los capítulos 19/20 se extiende de forma natural a las llamadas de sincronización. La sincronización en sí no adquiere ningún lock dentro de `bus_dma`; es seguro llamarla desde cualquier contexto siempre que quien llama tenga la disciplina de concurrencia apropiada para el mapa.

Las reglas relevantes:

- El mismo mapa no debe ser tocado por dos threads simultáneamente. Si dos filtros en dos vectores pueden sincronizar el mismo mapa, el driver debe mantener un mutex durante la sincronización. El capítulo 21 utiliza un único mapa y la tarea de un único vector para la finalización, por lo que no hay contención en el caso de buffer único.
- La llamada de sincronización puede realizarse desde un filtro (contexto de interrupción primaria), desde una tarea (contexto de thread) o desde el contexto de proceso (manejadores de attach, detach, sysctl). Todos son seguros.
- La llamada de sincronización no debe realizarse desde dentro del callback de `bus_dmamap_load`. El callback en sí es la señal del kernel de que la carga ha completado; el driver sincroniza después, cuando el callback retorna y después de que el driver haya programado el dispositivo.
- La llamada de sincronización debe emparejarse con el flag PRE/POST que corresponda a la dirección de la transferencia, no con un flag que coincida con lo que el driver «planea hacer a continuación». `BUS_DMASYNC_POSTREAD` después de una transferencia de host a dispositivo es un fallo, aunque el CPU esté a punto de leer el buffer (para verificación, por ejemplo) justo después. Usa `POSTWRITE` después de una transferencia de host a dispositivo para que coincida con la dirección de la transferencia.

El driver del capítulo 21 llama a sync en tres contextos: desde `myfirst_dma_do_transfer` (llamado desde el contexto de proceso o desde un manejador de sysctl), desde el filtro rx (solo después de confirmar que la transferencia está completa, aunque el capítulo aplaza la sincronización a la tarea para mantener el filtro mínimo) y desde la tarea rx (bajo el mutex del softc, antes y después de tocar el buffer). La sección 5 muestra las ubicaciones exactas.

### La primera sincronización: cuándo hacerla

Una pregunta sutil: ¿necesita el driver una llamada de sincronización antes del primer uso de un buffer recién asignado?

La respuesta es sí para los anillos bidireccionales, y la llamada necesaria es el flag PRE combinado en el momento de la configuración. La razón es que el asignador puede no haber puesto a cero las líneas de caché que cubren el buffer (incluso con `BUS_DMA_ZERO` poniendo la memoria a cero, las líneas de caché pueden seguir conteniendo lo que había antes de que se asignara la página), y sin una sincronización inicial el dispositivo podría leer basura.

Para el patrón del capítulo 21 (transferencia de host a dispositivo en la que el driver rellena el buffer antes de PREWRITE), la sincronización inicial no es necesaria: el driver escribe el buffer y luego emite PREWRITE, de modo que el estado de la caché antes de la escritura es irrelevante; PREWRITE se encarga de cualquier problema de coherencia.

Para las transferencias de dispositivo a host, PREREAD cumple el mismo propósito: el kernel realiza la invalidación necesaria antes de que el dispositivo escriba, de modo que cualquier estado de caché obsoleto queda eliminado.

La regla general: mientras cada traspaso al dispositivo esté precedido de una sincronización PRE, el estado inicial del buffer no importa para la corrección. La sincronización PRE es tanto una invalidación (para READ) como un vaciado (para WRITE), por lo que gestiona ambas direcciones. Esta es una de las pequeñas elegancias de la API; el driver no necesita llamadas de sincronización especiales de «init».

### Cerrando la sección 4

La sección 4 fijó la disciplina de sincronización. Cuatro flags (PREREAD, PREWRITE, POSTREAD, POSTWRITE), un modelo mental (la propiedad pasa entre el CPU y el dispositivo), cuatro patrones comunes (host a dispositivo, dispositivo a host, anillo de descriptores, anillo compartido) y una regla (cada transferencia tiene una llamada PRE y una POST, ambas en el código del driver aunque una sea un no-op en la plataforma actual). El flag `BUS_DMA_COHERENT` reduce el coste de sincronización pero no elimina el requisito de sincronización; `bus_dmamap_sync` se llama siempre.

La sección 5 construye el motor DMA simulado que consumirá estas llamadas de sincronización. El motor acepta una dirección de bus y una longitud, ejecuta una transferencia programada con `callout(9)` unos pocos milisegundos después y establece un bit de finalización en el registro de estado. El trabajo del driver es programar el motor, esperar la finalización y verificar el resultado. Cada etapa de la sección 5 corresponde a uno de los cuatro patrones de la sección 4.



## Sección 5: construyendo un motor DMA simulado

La sección 4 estableció la disciplina de sincronización. La sección 5 pone esa disciplina a trabajar contra un motor DMA concreto. El motor vive dentro del backend simulado del capítulo 17 y está construido de modo que el driver pueda manejarlo exactamente igual que lo haría con un dispositivo real. Tag, mapa, memoria, sincronización, registros, finalización, sincronización, verificación: el bucle completo se ejecuta de principio a fin, todo en el simulador, dentro del módulo del kernel `myfirst`, sin hardware real.

La etiqueta de versión de la etapa 2 es `1.4-dma-stage2`. El motor simulado se añade a `myfirst_sim.c`. El uso del motor por parte del driver se añade a `myfirst.c`. El filtro rx del capítulo 20 no cambia en esta etapa; la sección 6 conecta la ruta de finalización a través de él. Aquí, el driver sondea el registro de estado del motor en una tarea porque el sondeo es la ruta más sencilla y aísla la mecánica DMA de la maquinaria de interrupciones durante una etapa.

### El conjunto de registros DMA

El mapa de registros MMIO del backend simulado del capítulo 17 ya incluye `INTR_STATUS`, `INTR_MASK`, `DATA_OUT`, `DATA_AV` y `ERROR` en desplazamientos fijos. La etapa 2 extiende el mapa con cinco nuevos registros DMA en los desplazamientos 0x20 a 0x34. Los desplazamientos y significados se eligen para que coincidan con el bloque de control DMA de un dispositivo real típico:

| Desplazamiento | Nombre           | R/W | Significado                                                       |
|----------------|------------------|-----|-------------------------------------------------------------------|
| 0x20           | `DMA_ADDR_LOW`   | RW  | 32 bits bajos de la dirección de bus DMA.                         |
| 0x24           | `DMA_ADDR_HIGH`  | RW  | 32 bits altos (cero en el motor de 32 bits de este dispositivo).  |
| 0x28           | `DMA_LEN`        | RW  | Longitud de la transferencia en bytes.                            |
| 0x2C           | `DMA_DIR`        | RW  | 0 = host a dispositivo (el motor lee), 1 = dispositivo a host.    |
| 0x30           | `DMA_CTRL`       | RW  | Escribir 1 = iniciar, escribir 2 = abortar, bit 31 = reiniciar.   |
| 0x34           | `DMA_STATUS`     | RO  | Bit 0 = DONE, bit 1 = ERR, bit 2 = RUNNING.                      |

Las constantes `MYFIRST_REG_DMA_*` viven en `myfirst.h`:

```c
#define	MYFIRST_REG_DMA_ADDR_LOW	0x20
#define	MYFIRST_REG_DMA_ADDR_HIGH	0x24
#define	MYFIRST_REG_DMA_LEN		0x28
#define	MYFIRST_REG_DMA_DIR		0x2C
#define	MYFIRST_REG_DMA_CTRL		0x30
#define	MYFIRST_REG_DMA_STATUS		0x34

#define	MYFIRST_DMA_DIR_WRITE		0u
#define	MYFIRST_DMA_DIR_READ		1u

#define	MYFIRST_DMA_CTRL_START		(1u << 0)
#define	MYFIRST_DMA_CTRL_ABORT		(1u << 1)
#define	MYFIRST_DMA_CTRL_RESET		(1u << 31)

#define	MYFIRST_DMA_STATUS_DONE		(1u << 0)
#define	MYFIRST_DMA_STATUS_ERR		(1u << 1)
#define	MYFIRST_DMA_STATUS_RUNNING	(1u << 2)
```

La nomenclatura sigue la convención del capítulo 17: `MYFIRST_REG_*` para desplazamientos, `MYFIRST_DMA_*` para bits con nombre. Las constantes son utilizadas tanto por el backend de simulación (que implementa el motor) como por el driver (que programa el motor); el encabezado compartido es la única fuente de verdad.

El motor también establece `MYFIRST_INTR_COMPLETE` en `INTR_STATUS` cuando finaliza una transferencia, utilizando el mecanismo de interrupciones existente de los capítulos 19/20. Eso es lo que conecta la sección 6; para la etapa 2, el driver sondea `DMA_STATUS` directamente desde una tarea y no utiliza la interrupción todavía.

### Ampliando el estado de la simulación

El estado `struct myfirst_sim` del backend simulado crece con nuevos campos para rastrear el motor DMA:

```c
struct myfirst_sim_dma {
	uint32_t	addr_low;
	uint32_t	addr_high;
	uint32_t	len;
	uint32_t	dir;
	uint32_t	ctrl;
	uint32_t	status;
	struct callout	done_co;
	bool		armed;
};
```

Los primeros seis campos reflejan el contenido de los registros. El callout `done_co` es el mecanismo que usa el motor para simular la latencia de transferencia: cuando el driver escribe `DMA_CTRL_START`, el motor programa `done_co` para que se dispare unos milisegundos después, lo que establece `DMA_STATUS_DONE` y activa `MYFIRST_INTR_COMPLETE`. El bool `armed` rastrea si el callout está programado para que la ruta de aborto sepa si debe llamar a `callout_stop`.

El nuevo estado se incrusta en la estructura `struct myfirst_sim` existente:

```c
struct myfirst_sim {
	/* ... existing Chapter 17 fields ... */
	struct myfirst_sim_dma	dma;

	/* Backing store for the DMA engine's source or sink. */
	void			*dma_scratch;
	size_t			dma_scratch_size;

	/* Simulation back-channel: the host-visible KVA and bus address
	 * the driver registers at myfirst_dma_setup time. A real device
	 * never needs these; the simulator needs the KVA because it is
	 * software and cannot reach RAM through the memory controller. */
	void			*dma_host_kva;
	bus_addr_t		 dma_host_bus_addr;
	size_t			 dma_host_size;
};
```

`dma_scratch` es un buffer que el motor usa como el otro extremo de la transferencia: cuando el driver programa una transferencia de host a dispositivo, el motor lee del buffer del driver y escribe en `dma_scratch`; cuando el driver programa una transferencia de dispositivo a host, el motor lee de `dma_scratch` y escribe en el buffer del driver. En un dispositivo real, el «otro extremo» es el propio hardware del dispositivo (el cable, el soporte de almacenamiento, la salida de audio); en la simulación, es un buffer dentro del kernel que actúa como sustituto.

Los campos `dma_host_*` son el canal de retorno mencionado anteriormente. Se rellenan mediante un nuevo helper `myfirst_sim_register_dma_buffer(sim, kva, bus_addr, size)` al que se llama desde `myfirst_dma_setup`, y se borran mediante `myfirst_sim_unregister_dma_buffer`, llamado desde `myfirst_dma_teardown`. Los helpers residen en `myfirst_sim.c` y exponen el estado interno del sim únicamente en la medida que el simulador necesita; son el único acoplamiento explícito entre la capa DMA y la capa sim.

El scratch buffer se asigna en el momento de inicialización del sim:

```c
sc->sim.dma_scratch_size = 4096;
sc->sim.dma_scratch = malloc(sc->sim.dma_scratch_size,
    M_MYFIRST, M_WAITOK | M_ZERO);
```

y se libera al desmantelar el sim:

```c
free(sc->sim.dma_scratch, M_MYFIRST);
```

El scratch buffer no es en sí mismo un buffer DMA (el motor es simulado; no ocurre ningún DMA real). Es simplemente memoria del kernel que la simulación usa para modelar el almacenamiento interno del dispositivo.

### El hook de acceso a registros

El backend de simulación del Capítulo 17 gestiona escrituras y lecturas sobre la BAR simulada interceptando las operaciones `bus_read_4` y `bus_write_4`. El cambio de la Etapa 2 extiende el hook de escritura para reconocer los nuevos registros DMA:

```c
static void
myfirst_sim_write_4(struct myfirst_sim *sim, bus_size_t off, uint32_t val)
{
	switch (off) {
	/* ... existing registers from Chapter 17 ... */
	case MYFIRST_REG_DMA_ADDR_LOW:
		sim->dma.addr_low = val;
		break;
	case MYFIRST_REG_DMA_ADDR_HIGH:
		sim->dma.addr_high = val;
		break;
	case MYFIRST_REG_DMA_LEN:
		sim->dma.len = val;
		break;
	case MYFIRST_REG_DMA_DIR:
		sim->dma.dir = val;
		break;
	case MYFIRST_REG_DMA_CTRL:
		sim->dma.ctrl = val;
		myfirst_sim_dma_ctrl_written(sim, val);
		break;
	default:
		/* ... existing default handling ... */
		break;
	}
}
```

El registro `MYFIRST_REG_DMA_STATUS` es de solo lectura, por lo que el hook de lectura lo gestiona; el hook de escritura ignora los intentos de escribirlo (o, en una compilación defensiva, registra una advertencia y rechaza la operación). La lectura de `DMA_STATUS` devuelve directamente el valor actual de `sim->dma.status`.

`myfirst_sim_dma_ctrl_written` es el punto de decisión principal del motor:

```c
static void
myfirst_sim_dma_ctrl_written(struct myfirst_sim *sim, uint32_t val)
{
	if ((val & MYFIRST_DMA_CTRL_RESET) != 0) {
		if (sim->dma.armed) {
			callout_stop(&sim->dma.done_co);
			sim->dma.armed = false;
		}
		sim->dma.status = 0;
		sim->dma.addr_low = 0;
		sim->dma.addr_high = 0;
		sim->dma.len = 0;
		sim->dma.dir = 0;
		return;
	}
	if ((val & MYFIRST_DMA_CTRL_ABORT) != 0) {
		if (sim->dma.armed) {
			callout_stop(&sim->dma.done_co);
			sim->dma.armed = false;
		}
		sim->dma.status &= ~MYFIRST_DMA_STATUS_RUNNING;
		sim->dma.status |= MYFIRST_DMA_STATUS_ERR;
		return;
	}
	if ((val & MYFIRST_DMA_CTRL_START) != 0) {
		if ((sim->dma.status & MYFIRST_DMA_STATUS_RUNNING) != 0) {
			/* New START while an old transfer is in flight. */
			sim->dma.status |= MYFIRST_DMA_STATUS_ERR;
			return;
		}
		sim->dma.status = MYFIRST_DMA_STATUS_RUNNING;
		sim->dma.armed = true;
		callout_reset(&sim->dma.done_co, hz / 100,
		    myfirst_sim_dma_done_co, sim);
		return;
	}
}
```

El motor trata RESET, ABORT y START como tres comandos distintos y los gestiona en consecuencia. El valor `hz / 100` programa la finalización aproximadamente diez milisegundos en el futuro en un kernel de 1000 Hz; el retardo hace que la transferencia sea observablemente asíncrona y le da al driver una ventana realista para hacer polling o esperar la interrupción.

### El manejador del callout

Cuando el callout se dispara, el motor realiza la transferencia simulada y establece el estado:

```c
static void
myfirst_sim_dma_done_co(void *arg)
{
	struct myfirst_sim *sim = arg;
	bus_addr_t bus_addr;
	uint32_t len;
	void *kva;

	bus_addr = ((bus_addr_t)sim->dma.addr_high << 32) | sim->dma.addr_low;
	len = sim->dma.len;

	/* Back-channel lookup: find the KVA for this bus address. A real
	 * device would not need this; the device's own DMA engine would
	 * perform the memory-controller access. */
	if (sim->dma_host_kva == NULL ||
	    bus_addr != sim->dma_host_bus_addr ||
	    len == 0 || len > sim->dma_host_size ||
	    len > sim->dma_scratch_size) {
		sim->dma.status = MYFIRST_DMA_STATUS_ERR;
		sim->dma.armed = false;
		myfirst_sim_dma_raise_complete(sim);
		return;
	}
	kva = sim->dma_host_kva;

	if (sim->dma.dir == MYFIRST_DMA_DIR_WRITE) {
		/* Host-to-device: sim reads host KVA, writes scratch. */
		memcpy(sim->dma_scratch, kva, len);
	} else {
		/* Device-to-host: sim reads scratch, writes host KVA.
		 * Fill scratch with a recognisable pattern first so the
		 * test can verify the transfer. */
		memset(sim->dma_scratch, 0x5A, len);
		memcpy(kva, sim->dma_scratch, len);
	}

	sim->dma.status = MYFIRST_DMA_STATUS_DONE;
	sim->dma.armed = false;
	myfirst_sim_dma_raise_complete(sim);
}
```

Las dos funciones auxiliares `myfirst_sim_dma_copy_from_host` y `myfirst_sim_dma_copy_to_host` realizan el movimiento real de bytes. Aquí la simulación se topa con un límite fundamental: es software ejecutándose dentro del kernel, no un dispositivo real. Un dispositivo real utiliza la dirección de bus para acceder a la RAM a través del controlador de memoria; un simulador de software no puede tomar ese camino físicamente. El simulador necesita una dirección virtual del kernel que pueda desreferenciar, no la dirección de bus.

Resolvemos esto con un canal auxiliar deliberado. En el momento de `myfirst_dma_setup`, el driver llama a una nueva función auxiliar `myfirst_sim_register_dma_buffer(sc, sc->dma_vaddr, sc->dma_bus_addr, MYFIRST_DMA_BUFFER_SIZE)`. La función auxiliar almacena la tripleta (KVA, dirección de bus, tamaño) en el estado del simulador. Cuando el callout se dispara más adelante, lee la dirección de bus que el driver programó en `DMA_ADDR_LOW`/`DMA_ADDR_HIGH`, la busca en la tripleta almacenada y recupera la KVA. El `memcpy` opera entonces sobre la KVA.

Este canal auxiliar no es más que una muleta propia de la simulación. El hardware real nunca recibe una KVA; el dispositivo únicamente utiliza la dirección de bus, y el controlador de memoria la resuelve a través del mapa de direcciones de la plataforma (posiblemente mediante una IOMMU). El único propósito del canal auxiliar es permitir al simulador fingir que realiza el DMA sin programar realmente el controlador de memoria. Mantener el mecanismo explícito y con nombre es la mejor defensa contra la confusión: el lector ve un puente de simulación claramente etiquetado en lugar de un cast de puntero mágico, y la división entre "lo que ve el dispositivo" (la dirección de bus) y "lo que ve la CPU" (la KVA) permanece clara.

`myfirst_sim_dma_raise_complete` establece `MYFIRST_INTR_COMPLETE` en `INTR_STATUS` y, si `INTR_MASK` tiene el bit activo, dispara la interrupción simulada a través del camino existente del Capítulo 19/20:

```c
static void
myfirst_sim_dma_raise_complete(struct myfirst_sim *sim)
{
	sim->intr_status |= MYFIRST_INTR_COMPLETE;
	if ((sim->intr_mask & MYFIRST_INTR_COMPLETE) != 0)
		myfirst_sim_raise_intr(sim);
}
```

En la Etapa 2 todavía no habilitamos el bit `MYFIRST_INTR_COMPLETE` en `INTR_MASK`; el driver hará polling. La Sección 6 habilita el bit y conecta el camino de finalización a través del filtro.

### La parte del driver: programar el motor

En la parte del driver, una única función auxiliar programa el motor y espera:

```c
int
myfirst_dma_do_transfer(struct myfirst_softc *sc, int direction,
    size_t length)
{
	uint32_t status;
	int timeout;

	if (length == 0 || length > MYFIRST_DMA_BUFFER_SIZE)
		return (EINVAL);

	if (direction == MYFIRST_DMA_DIR_WRITE) {
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREWRITE);
	} else {
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREREAD);
	}

	CSR_WRITE_4(sc, MYFIRST_REG_DMA_ADDR_LOW,
	    (uint32_t)(sc->dma_bus_addr & 0xFFFFFFFF));
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_ADDR_HIGH,
	    (uint32_t)(sc->dma_bus_addr >> 32));
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_LEN, (uint32_t)length);
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_DIR, direction);
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_START);

	/* Poll for completion. Stage 2 is polling-only. */
	for (timeout = 500; timeout > 0; timeout--) {
		pause("dma", hz / 1000); /* 1 ms. */
		status = CSR_READ_4(sc, MYFIRST_REG_DMA_STATUS);
		if ((status & MYFIRST_DMA_STATUS_DONE) != 0)
			break;
		if ((status & MYFIRST_DMA_STATUS_ERR) != 0) {
			if (direction == MYFIRST_DMA_DIR_WRITE)
				bus_dmamap_sync(sc->dma_tag, sc->dma_map,
				    BUS_DMASYNC_POSTWRITE);
			else
				bus_dmamap_sync(sc->dma_tag, sc->dma_map,
				    BUS_DMASYNC_POSTREAD);
			return (EIO);
		}
	}

	if (timeout == 0) {
		CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_ABORT);
		if (direction == MYFIRST_DMA_DIR_WRITE)
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTWRITE);
		else
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTREAD);
		return (ETIMEDOUT);
	}

	/* Acknowledge the completion. */
	CSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_COMPLETE);

	if (direction == MYFIRST_DMA_DIR_WRITE)
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_POSTWRITE);
	else
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_POSTREAD);

	return (0);
}
```

La función tiene sesenta líneas, y vale la pena leer su estructura dos veces. La sincronización PRE ocurre una vez al inicio; la sincronización POST ocurre exactamente una vez en cada camino de retorno (éxito, error, timeout). El bucle de polling usa `pause("dma", hz / 1000)` para suspensiones de un milisegundo; en un kernel de 1000 Hz esto da quinientos intentos de un milisegundo antes de rendirse en quinientos milisegundos en total. `pause(9)` toma brevemente el mutex de suspensión global pero es seguro desde una función auxiliar de driver en contexto de proceso. Un dispositivo real con un camino de interrupción no haría polling; la Sección 6 reemplaza el polling con una espera de interrupción.

La elección de los flags PRE y POST corresponde a la dirección. Para host a dispositivo (DIR_WRITE), el driver acaba de escribir en el buffer, por lo que se requiere PREWRITE; tras la transferencia, POSTWRITE libera cualquier región de rebote. Para dispositivo a host (DIR_READ), el driver va a leer el buffer, por lo que se requiere PREREAD; tras la transferencia, POSTREAD completa la copia desde la región de rebote (si la hay) e invalida la caché (si es necesario).

### Poner a prueba el motor desde el espacio de usuario

El driver ya expone un árbol sysctl bajo `dev.myfirst.N.` desde el Capítulo 20. La Etapa 2 añade dos nuevos sysctls de solo escritura que desencadenan una transferencia:

```c
SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "dma_test_write",
    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE, sc, 0,
    myfirst_dma_sysctl_test_write, "IU",
    "Trigger a host-to-device DMA transfer");
SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "dma_test_read",
    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE, sc, 0,
    myfirst_dma_sysctl_test_read, "IU",
    "Trigger a device-to-host DMA transfer");
```

El manejador de `dma_test_write`:

```c
static int
myfirst_dma_sysctl_test_write(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	unsigned int pattern;
	int err;

	err = sysctl_handle_int(oidp, &pattern, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	memset(sc->dma_vaddr, (int)(pattern & 0xFF),
	    MYFIRST_DMA_BUFFER_SIZE);
	err = myfirst_dma_do_transfer(sc, MYFIRST_DMA_DIR_WRITE,
	    MYFIRST_DMA_BUFFER_SIZE);
	if (err != 0)
		device_printf(sc->dev, "dma_test_write: error %d\n", err);
	else
		device_printf(sc->dev,
		    "dma_test_write: pattern 0x%02x transferred\n",
		    pattern & 0xFF);
	return (err);
}
```

El usuario escribe un entero en `dev.myfirst.0.dma_test_write`, el byte bajo se convierte en un patrón de relleno, el driver rellena el buffer con ese patrón, programa el motor, espera la finalización y registra el resultado. El manejador de `dma_test_read` es simétrico: desencadena una transferencia de dispositivo a host, espera la finalización, lee los primeros bytes del buffer y los registra.

Una sesión en espacio de usuario tiene este aspecto:

```sh
sudo sysctl dev.myfirst.0.dma_test_write=0xAA
sudo dmesg | tail -1
# myfirst0: dma_test_write: pattern 0xaa transferred

sudo sysctl dev.myfirst.0.dma_test_read=1
sudo dmesg | tail -1
# myfirst0: dma_test_read: first bytes 5A 5A 5A 5A 5A 5A 5A 5A
```

La primera transferencia envía 0xAA al dispositivo simulado; la segunda recibe de vuelta el patrón 0x5A del dispositivo. Ambas transferencias ejercitan la disciplina PRE/POST completa.

### Observabilidad: contadores por transferencia

La Etapa 2 añade cuatro contadores al estado DMA en el softc:

```c
uint64_t dma_transfers_write;
uint64_t dma_transfers_read;
uint64_t dma_errors;
uint64_t dma_timeouts;
```

Cada uno es incrementado por `myfirst_dma_do_transfer` en el camino de salida correspondiente, y cada uno se expone como un sysctl de solo lectura. Los contadores son el equivalente en la Etapa 2 del driver a los contadores por vector que el Capítulo 20 añadió al camino de interrupciones; permiten confirmar de un vistazo que las transferencias se producen como se esperaba y detectar fallos silenciosos.

Los contadores se leen con operaciones atómicas (`atomic_add_64`) para que puedan actualizarse de forma segura incluso si el camino de finalización por interrupciones de la Sección 6 termina ejecutándose desde un contexto de filtro. La Sección 4 del Capítulo 20 cubrió la disciplina atómica; los contadores de la Etapa 2 siguen el mismo patrón.

### Verificar la Etapa 2

Compila y carga:

```sh
make clean && make
sudo kldload ./myfirst.ko
```

`dmesg` muestra el banner DMA de la Etapa 1 más el nuevo banner con DMA habilitado:

```text
myfirst0: DMA buffer 4096 bytes at KVA 0xfffffe... bus addr 0x...
myfirst0: DMA engine present, scratch 4096 bytes
myfirst0: interrupt mode: MSI-X, 3 vectors
```

Ejecuta la prueba:

```sh
sudo sysctl dev.myfirst.0.dma_test_write=0x33
sudo sysctl dev.myfirst.0.dma_test_read=1
sudo sysctl dev.myfirst.0.dma_transfers_write
sudo sysctl dev.myfirst.0.dma_transfers_read
```

Resultado esperado:

```text
dev.myfirst.0.dma_transfers_write: 1
dev.myfirst.0.dma_transfers_read: 1
```

Ejecútalo mil veces en un bucle:

```sh
for i in $(seq 1 1000); do
  sudo sysctl dev.myfirst.0.dma_test_write=$((i & 0xFF)) >/dev/null
  sudo sysctl dev.myfirst.0.dma_test_read=1 >/dev/null
done
sudo sysctl dev.myfirst.0.dma_transfers_write
sudo sysctl dev.myfirst.0.dma_transfers_read
sudo sysctl dev.myfirst.0.dma_errors
sudo sysctl dev.myfirst.0.dma_timeouts
```

Conteos esperados: 1000 transferencias de escritura, 1000 transferencias de lectura, 0 errores, 0 timeouts. Cualquier timeout o error durante la prueba indica un bug en la simulación o una condición de carrera en el driver; ambos merecen detectarse cuanto antes.

Descarga el módulo y verifica que el callout está detenido, el buffer de trabajo está liberado y la etiqueta está destruida:

```sh
sudo kldunload myfirst
vmstat -m | grep myfirst || true
```

La línea debería estar ausente o mostrar cero asignaciones.

### Lo que hace y no hace la Etapa 2

La Etapa 2 es la primera etapa en la que se realizan transferencias DMA reales. El driver programa el motor; el motor copia bytes a través del camino simulado; el driver observa el resultado. La disciplina de sincronización se ejercita en ambas direcciones.

Lo que la Etapa 2 no hace es usar el camino de interrupciones. El polling es aceptable para una etapa didáctica, pero inapropiado para un driver real: el driver mantiene un thread del kernel en `pause` durante toda la transferencia. La Sección 6 reemplaza el polling con el mecanismo de interrupciones del Capítulo 19/20; el filtro rx ve `MYFIRST_INTR_COMPLETE`, lo reconoce, encola la tarea, y la tarea realiza la sincronización POST y la verificación. La Etapa 3 es la versión completamente dirigida por interrupciones.

La Etapa 2 tampoco gestiona todos los errores de forma limpia. Los caminos de timeout y abort están presentes pero son mínimos; la Sección 7 los recorre cuidadosamente y amplía la lógica de recuperación. El patrón de transferencia única también es limitado; la refactorización de la Sección 8 expone funciones auxiliares que facilitan añadir patrones de transferencia múltiple.

### Errores comunes en la Etapa 2

Merece la pena señalar cuatro errores comunes.

**Error uno: llamar a `bus_dmamap_sync` dentro del callout.** El callout se ejecuta en contexto de softclock, y la sincronización en sí misma es segura desde ahí, pero los accesos al buffer que realiza la simulación (`myfirst_sim_dma_copy_from_host`) necesitan que el mapa del driver esté cargado. Si el driver hace detach mientras hay un callout armado, el mapa se descarga bajo los pies del callout. La solución es el patrón de detach del Capítulo 21: detener el callout (`callout_drain`) antes de descargar el mapa. La Sección 7 lo revisita con detalle.

**Error dos: olvidar reconocer `MYFIRST_INTR_COMPLETE`.** La simulación activa el bit; el driver debe borrarlo escribiendo el bit de vuelta en `INTR_STATUS`. Si el driver no lo hace, la siguiente transferencia ve un bit DONE obsoleto y el bucle de polling retorna inmediatamente con el estado correcto pero antes de que la nueva transferencia se haya ejecutado. El reconocimiento está en la versión del bucle de polling (`CSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_COMPLETE);`); la Sección 6 mueve el reconocimiento al camino del filtro cuando se gestiona por interrupciones.

**Error tres: usar `DELAY` o `cpu_spinwait` en lugar de `pause`.** El bucle de polling de la Etapa 2 usa `pause("dma", hz / 1000)` para ceder el control. Un bucle ajustado con `DELAY(1000)` bloquea una CPU durante toda la ventana de transferencia, lo que es aceptable en una prueba de un solo procesador pero inaceptable en producción. La llamada a `pause` pone el thread a dormir y permite que otro trabajo se ejecute. Esto importa más en la versión de la Sección 6, donde el driver llama a `cv_timedwait` en lugar de hacer polling, pero el principio es el mismo: cede la CPU siempre que estés esperando eventos externos.

**Error cuatro: olvidar limitar `length` al `maxsize` de la etiqueta.** La función auxiliar del driver acepta un argumento `length`; si el llamador pasa un valor mayor que `MYFIRST_DMA_BUFFER_SIZE`, el motor escribe más allá del final del buffer de trabajo (en la simulación) o más allá del final del buffer del host (en hardware real). El código del Capítulo 21 protege contra esto en las primeras líneas de la función auxiliar; olvidar la protección es una fuente habitual de corrupción sutil.

### Cerrando la Sección 5

La Sección 5 hizo real el motor DMA simulado. El mapa de registros, la máquina de estados, la latencia basada en callout, la función auxiliar de polling del driver, la interfaz de prueba sysctl y los contadores están todos en su sitio. El driver ejecuta transferencias completas en ambas direcciones; la disciplina de sincronización de la Sección 4 se ejercita en cada una de ellas. La Etapa 2 es un driver DMA funcional aunque haga polling en lugar de gestionar interrupciones.

La Sección 6 reemplaza el polling con el camino de interrupciones del Capítulo 19/20. La interrupción de finalización llega a través del filtro rx, el filtro encola la tarea, y la tarea realiza la sincronización POST y la verificación. El comportamiento visible para el usuario no cambia; los internos del driver sí. Ese cambio es lo que permite escalar al driver: un driver de polling bloquea una CPU por transferencia, mientras que un driver dirigido por interrupciones gestiona las mismas transferencias con un coste de CPU despreciable.



## Sección 6: gestionar la finalización de DMA en el driver

La Sección 5 produjo un driver DMA basado en polling. La Sección 6 reescribe el camino de finalización para usar el mecanismo de interrupciones del Capítulo 19/20. Los objetivos son: ninguna CPU permanece en `pause` mientras se ejecuta una transferencia; la finalización se entrega a través de un filtro que se ejecuta en contexto de interrupción primario; la tarea en contexto de thread realiza la sincronización POST y la verificación; y el comportamiento visible para el usuario no cambia respecto a la Etapa 2. La etiqueta de versión pasa a ser `1.4-dma-stage3`.

El mecanismo por vector del Capítulo 20 ya tiene la forma adecuada para esto. Uno de los tres vectores (el vector rx, o en el caso del fallback heredado el único vector admin) gestiona `MYFIRST_INTR_COMPLETE`. El filtro reconoce; la tarea procesa. El cambio del Capítulo 21 consiste en hacer que el procesamiento de la tarea incluya una sincronización DMA POST y una lectura del buffer.

### Por qué el diseño dirigido por interrupciones es el correcto

El camino de polling de la Sección 5 funciona, pero tiene cuatro debilidades que la Etapa 3 corrige.

**Una CPU queda ocupada por cada transferencia en curso.** La llamada a `pause` pone el thread a dormir en el canal de espera `dma`, lo cual está bien, pero la pila del thread, el estado de la caché y la sobrecarga de planificación siguen existiendo. Un driver con muchas transferencias en curso tendría muchos threads ocupados. La finalización dirigida por interrupciones usa una tarea por vector, reutilizada en todas las transferencias.

**La latencia es de grano grueso.** El bucle de polling se activa cada milisegundo. Un motor real podría completar la operación en microsegundos; el driver espera hasta un milisegundo completo antes de detectarlo. La finalización controlada por interrupciones se entrega con la latencia natural del hardware.

**El bucle de polling no es composable con múltiples transferencias en curso.** Para emitir dos transferencias consecutivas, el driver debe esperar a que la primera complete (serializando la operación) o mantener su propio estado por transferencia (registro ad hoc). La finalización por interrupciones se compone de forma natural: cada finalización dispara su propia interrupción, el filter la enruta hacia la task y la task la gestiona.

**El polling no puede ejecutarse en contexto de interrupción.** Si el driver alguna vez necesita completar una transferencia desde dentro de un manejador de interrupciones (por ejemplo, una interrupción de recepción completada que inmediatamente desencadena un nuevo relleno de descriptor), el bucle de polling no funcionará porque `pause` no puede llamarse desde el filter. La ruta controlada por interrupciones es el único diseño verdaderamente composable.

La fase 3 conserva el helper de polling como fallback para las pruebas controladas por sysctl (resulta útil para pruebas deterministas) y añade un helper controlado por interrupciones para el caso de uso real.

### Las modificaciones al camino de las interrupciones

El filtro rx del capítulo 20 gestionaba `MYFIRST_INTR_DATA_AV`. La etapa 3 lo extiende (o extiende el filtro de administración de respaldo legacy) para gestionar también `MYFIRST_INTR_COMPLETE`:

```c
int
myfirst_msix_rx_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;
	bool handled = false;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);

	if ((status & MYFIRST_INTR_DATA_AV) != 0) {
		atomic_add_64(&vec->fire_count, 1);
		ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_DATA_AV);
		atomic_add_64(&sc->intr_data_av_count, 1);
		handled = true;
	}

	if ((status & MYFIRST_INTR_COMPLETE) != 0) {
		atomic_add_64(&sc->dma_complete_intrs, 1);
		ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_COMPLETE);
		handled = true;
	}

	if (!handled) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	if (sc->intr_tq != NULL)
		taskqueue_enqueue(sc->intr_tq, &vec->task);
	return (FILTER_HANDLED);
}
```

El filtro lee `INTR_STATUS` una sola vez, comprueba los dos bits, confirma cada uno que detecta, incrementa los contadores por bit y encola la tarea si se ha activado algún bit. El patrón es exactamente el del capítulo 19, generalizado a dos bits por vector; lo único nuevo es el contador `MYFIRST_INTR_COMPLETE`.

La tarea recoge el estado que registró el filtro:

```c
static void
myfirst_msix_rx_task_fn(void *arg, int npending)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	bool did_complete;

	MYFIRST_LOCK(sc);
	if (sc->hw == NULL || !sc->pci_attached) {
		MYFIRST_UNLOCK(sc);
		return;
	}

	/* Data-available path (Chapter 19/20). */
	sc->intr_last_data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
	sc->intr_task_invocations++;
	cv_broadcast(&sc->data_cv);

	/* DMA-complete path (Chapter 21). */
	did_complete = false;
	if (sc->dma_in_flight) {
		sc->dma_in_flight = false;
		did_complete = true;
		if (sc->dma_last_direction == MYFIRST_DMA_DIR_WRITE)
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTWRITE);
		else
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTREAD);
		sc->dma_last_status = CSR_READ_4(sc,
		    MYFIRST_REG_DMA_STATUS);
		atomic_add_64(&sc->dma_complete_tasks, 1);
		cv_broadcast(&sc->dma_cv);
	}
	MYFIRST_UNLOCK(sc);

	(void)did_complete;
}
```

La tarea adquiere el mutex de softc (la disciplina del capítulo 11; el sleep mutex se mantiene mientras se accede al estado compartido). El flag `dma_in_flight`, establecido por el helper de transferencia antes de escribir el doorbell, indica a la tarea que hay una finalización de DMA pendiente. Si está activo, la tarea lo limpia, emite la sincronización POST que coincide con la dirección registrada, lee el estado final y hace un broadcast sobre `dma_cv` para que cualquier hilo en espera pueda despertar.

La llamada de sincronización desde la tarea es segura: la tarea se ejecuta en contexto de thread bajo el mutex de softc, y la llamada de sincronización no adquiere ningún lock adicional. La disciplina de locking del capítulo 19 se mantiene.

### El nuevo helper de transferencia dirigido por interrupciones

La versión de la etapa 3 del helper inicia la transferencia, arma el callout o el hardware real, y duerme hasta que la tarea de finalización emite la señal:

```c
int
myfirst_dma_do_transfer_intr(struct myfirst_softc *sc, int direction,
    size_t length)
{
	int err;

	if (length == 0 || length > MYFIRST_DMA_BUFFER_SIZE)
		return (EINVAL);

	MYFIRST_LOCK(sc);
	if (sc->dma_in_flight) {
		MYFIRST_UNLOCK(sc);
		return (EBUSY);
	}

	/* Issue the PRE sync before touching the device. */
	if (direction == MYFIRST_DMA_DIR_WRITE)
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREWRITE);
	else
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREREAD);

	sc->dma_last_direction = direction;
	sc->dma_in_flight = true;

	CSR_WRITE_4(sc, MYFIRST_REG_DMA_ADDR_LOW,
	    (uint32_t)(sc->dma_bus_addr & 0xFFFFFFFF));
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_ADDR_HIGH,
	    (uint32_t)(sc->dma_bus_addr >> 32));
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_LEN, (uint32_t)length);
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_DIR, direction);
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_START);

	/* Wait for the task to set dma_in_flight = false. */
	err = cv_timedwait(&sc->dma_cv, &sc->mtx, hz); /* 1 s timeout */
	if (err == EWOULDBLOCK) {
		/* Abort the engine and issue the POST sync so we do not
		 * leave the map in an inconsistent state. */
		CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_ABORT);
		if (direction == MYFIRST_DMA_DIR_WRITE)
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTWRITE);
		else
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTREAD);
		sc->dma_in_flight = false;
		atomic_add_64(&sc->dma_timeouts, 1);
		MYFIRST_UNLOCK(sc);
		return (ETIMEDOUT);
	}

	/* The task has issued the POST sync and recorded dma_last_status. */
	if ((sc->dma_last_status & MYFIRST_DMA_STATUS_ERR) != 0) {
		atomic_add_64(&sc->dma_errors, 1);
		MYFIRST_UNLOCK(sc);
		return (EIO);
	}

	if (direction == MYFIRST_DMA_DIR_WRITE)
		atomic_add_64(&sc->dma_transfers_write, 1);
	else
		atomic_add_64(&sc->dma_transfers_read, 1);

	MYFIRST_UNLOCK(sc);
	return (0);
}
```

El helper es más largo que el de la etapa 2 porque coordina con la tarea: la tarea emite la sincronización POST; el helper solo lo hace en el camino de timeout, donde la tarea nunca verá la finalización. La coordinación es el objetivo del rediseño; el helper ha pasado de «hacer polling hasta terminar» a «señalizar al dispositivo, esperar en una variable de condición, dejar que la tarea me complete».

La llamada a `cv_timedwait` toma el mutex de softc como segundo argumento; ese es el contrato estándar de `cv_timedwait` (el mutex se mantiene al entrar, se libera para la espera y se readquiere al despertar). El ámbito del mutex cubre todos los accesos al estado DMA compartido (`dma_in_flight`, `dma_last_direction`, `dma_last_status`).

Un punto sutil: la sincronización PRE se ejecuta antes de liberar el mutex para la espera, y la sincronización POST se ejecuta después del despertar. Ambas están dentro de la sección crítica de la transferencia, por lo que ningún otro thread puede ver el buffer en un estado parcialmente sincronizado. Este es el beneficio del locking del capítulo 11.

### El bit `MYFIRST_INTR_COMPLETE` entra en acción

La etapa 2 enmascaraba `MYFIRST_INTR_COMPLETE` en `INTR_MASK`. La etapa 3 lo habilita:

```c
CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR | MYFIRST_INTR_COMPLETE);
```

El cambio es una sola línea en la configuración de interrupciones. El motor de la simulación activa el bit al finalizar; el filtro del driver lo detecta y encola la tarea; la tarea lo gestiona. Todo el pipeline se activa con este único cambio.

### Verificación de la etapa 3

Construye y carga:

```sh
make clean && make
sudo kldload ./myfirst.ko
```

Ejecuta la misma prueba controlada por sysctl que en la etapa 2, pero conéctala al helper dirigido por interrupciones:

```c
static int
myfirst_dma_sysctl_test_write(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	unsigned int pattern;
	int err;

	err = sysctl_handle_int(oidp, &pattern, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	memset(sc->dma_vaddr, (int)(pattern & 0xFF),
	    MYFIRST_DMA_BUFFER_SIZE);
	err = myfirst_dma_do_transfer_intr(sc, MYFIRST_DMA_DIR_WRITE,
	    MYFIRST_DMA_BUFFER_SIZE);
	if (err != 0)
		device_printf(sc->dev,
		    "dma_test_write: intr err %d\n", err);
	return (err);
}
```

Ejecuta mil transferencias y observa los contadores:

```sh
for i in $(seq 1 1000); do
  sudo sysctl dev.myfirst.0.dma_test_write=$((i & 0xFF)) >/dev/null
  sudo sysctl dev.myfirst.0.dma_test_read=1 >/dev/null
done
sudo sysctl dev.myfirst.0.dma_complete_intrs
sudo sysctl dev.myfirst.0.dma_complete_tasks
sudo sysctl dev.myfirst.0.dma_transfers_write
sudo sysctl dev.myfirst.0.dma_transfers_read
```

Lo esperado: `dma_complete_intrs` es igual a 2000 (una por transferencia en ambas direcciones), `dma_complete_tasks` es igual a 2000 (la tarea procesó cada finalización), y `dma_transfers_write` y `dma_transfers_read` son iguales a 1000 cada uno.

Ejecuta `vmstat -i` durante la prueba para ver el vector que recibe interrupciones. Con MSI-X, el contador del vector rx debería aumentar de forma visible. Con el respaldo legacy, es el contador del vector único el que sube.

### Cómo detener las transferencias en vuelo durante el detach

La etapa 3 tiene un nuevo riesgo en el detach: la tarea puede estar ejecutando la sincronización POST en el momento en que se llama a `myfirst_dma_teardown`. El proceso de teardown no debe descargar el mapa mientras la tarea todavía mantiene referencias a él.

La solución es añadir una línea en el camino de detach antes de `myfirst_dma_teardown`:

```c
taskqueue_drain(sc->intr_tq, &sc->vectors[MYFIRST_VECTOR_RX].task);
```

`taskqueue_drain` espera a que la tarea termine de ejecutarse; una vez que retorna, el driver tiene la garantía de que no habrá más invocaciones de esa tarea (con la salvedad de que otro filtro podría volver a encolarla; el camino de detach ya ha enmascarado `INTR_MASK`, por lo que no se activarán más interrupciones). Con el drain en su lugar, el teardown es seguro.

El teardown de MSI-X del capítulo 20 ya drena la tarea; el capítulo 21 extiende el teardown de la etapa 3 para también detener el callout de la simulación antes de descargar el mapa:

```c
/* In myfirst_sim_destroy, called from detach: */
callout_drain(&sc->sim.dma.done_co);
```

`callout_drain` espera a que el callout termine de ejecutarse; una vez que retorna, se garantiza que el motor no generará más finalizaciones. Combinado con el drain del taskqueue, el detach es seguro frente a cualquier condición de carrera.

### Qué produce la etapa 3

La etapa 3 es un driver DMA completamente dirigido por interrupciones. Cada transferencia programa el motor, duerme en una variable de condición, despierta cuando la tarea de finalización emite la señal y retorna con los datos transferidos sincronizados y verificados. El driver nunca retiene el CPU mientras se ejecuta una transferencia. La interfaz de prueba mediante sysctl funciona como antes; los mecanismos internos son equivalentes a los del código DMA de producción.

La etapa 3 no cubre aún la recuperación de errores en profundidad. La sección 7 recorre los modos de fallo específicos y los patrones que gestiona cada uno.

### Errores comunes en la etapa 3

Merece la pena señalar cuatro errores más.

**Error uno: olvidar el `cv_broadcast` en la tarea.** Si la tarea realiza la sincronización POST pero no hace un broadcast sobre `dma_cv`, el helper espera el timeout completo y devuelve `ETIMEDOUT` aunque la transferencia haya tenido éxito. La solución es la única línea de `cv_broadcast`; omitirla es un bug sutil que se manifiesta como un timeout en todas las transferencias.

**Error dos: llamar a `cv_timedwait` sin mantener el mutex.** `cv_timedwait` requiere que el mutex esté adquirido al entrar; el kernel entra en pánico bajo `INVARIANTS` si no lo está. La estructura del helper (adquirir el lock, hacer el trabajo, esperar, gestionar el resultado, liberar el lock) mantiene el mutex durante todo el proceso; la espera lo libera brevemente durante el sleep y lo readquiere al despertar. Romper este patrón (liberar el mutex antes de `cv_timedwait`) es una condición de carrera que `WITNESS` detecta.

**Error tres: gestionar el camino de timeout sin la sincronización POST.** El camino de timeout en el helper aborta el motor y luego emite igualmente la sincronización POST. La sincronización es necesaria porque `BUS_DMA_NOWAIT` puede haber insertado bounce buffers en el momento PRE; sin POST, la región de bounce no se libera. Olvidarla provoca una fuga lenta de páginas de bounce.

**Error cuatro: dejar `dma_in_flight` activo en caso de error.** Todos los caminos de salida del helper limpian `dma_in_flight`. Olvidar limpiarlo en el camino de error significa que la siguiente transferencia devuelve `EBUSY` aunque no haya ninguna transferencia en curso. La estructura del helper (se activa al principio, se limpia por la tarea o explícitamente en caso de timeout o error) es el patrón robusto.

### Cerrando la sección 6

La sección 6 sustituyó el polling de la etapa 2 por la finalización dirigida por interrupciones de la etapa 3. El filtro detecta el bit de finalización, la tarea emite la sincronización POST y señaliza, el helper despierta y retorna. El rendimiento y la latencia mejoran; el coste de CPU disminuye; el driver se compone con otro trabajo de forma natural. El código es más largo que el de la etapa 2 porque coordina entre dos contextos (la tarea y el helper), pero el patrón es una aplicación directa de la disciplina de locking del capítulo 11 y el diseño de tareas por vector del capítulo 20; no se necesitan ideas nuevas más allá de la ubicación de la sincronización específica del DMA.

La sección 7 es el recorrido por la gestión de errores. Cada modo de fallo que trata el capítulo recibe una explicación detallada: qué ocurre, cuál es el síntoma y qué hace el driver al respecto. Los patrones son los mismos que todo driver DMA de producción debe gestionar; el entorno simulado del capítulo es un buen lugar para practicarlos.

## Sección 7: gestión de errores y casos extremos en DMA

La sección 6 produjo un driver DMA funcional dirigido por interrupciones. La sección 7 es la parte del capítulo donde el driver deja de asumir el éxito. Cada paso del pipeline DMA puede fallar: la creación del tag puede fallar, la asignación de memoria puede fallar, la carga puede fallar, la carga puede diferirse, el motor puede reportar un error, el motor puede no terminar nunca (timeout), el detach puede competir con una transferencia en curso. Cada modo de fallo tiene un patrón que lo gestiona; la sección 7 los recorre uno a uno.

El objetivo no es hacer que el driver del capítulo 21 sea resistente a todos los fallos concebibles; es enseñar los patrones para que el lector los reconozca en el código de producción y los aplique al escribir drivers reales. Muchos de los patrones son breves; la explicación es el objeto de aprendizaje, no el código.

### Modo de fallo 1: `bus_dma_tag_create` devuelve `ENOMEM`

`bus_dma_tag_create` puede devolver `ENOMEM` si el kernel no puede asignar la estructura del tag en sí. La asignación es pequeña (el tag ocupa unos pocos cientos de bytes) y solo ocurre una vez en el attach, por lo que el fallo es poco frecuente en la práctica, pero el driver debe gestionarlo igualmente.

El patrón: comprobar el valor de retorno, registrar el error, devolverlo al llamador y no tocar ningún estado DMA posterior. El código de la etapa 1 ya hace esto:

```c
err = bus_dma_tag_create(bus_get_dma_tag(sc->dev), ...);
if (err != 0) {
    device_printf(sc->dev, "bus_dma_tag_create failed: %d\n", err);
    return (err);
}
```

La función de attach de nivel superior detecta el error, ejecuta el desenrollado del camino de error y el kernel informa del fallo del probe de forma limpia. No se filtran recursos DMA porque no se asignó ninguno.

El flag `BUS_DMA_ALLOCNOW` indica al kernel que preasigne los recursos de bounce buffer en el momento de crear el tag, de modo que algunos tipos de fallo de asignación se trasladan del momento de carga al momento de creación del tag. Esto es útil para drivers que no pueden tolerar un fallo de carga posterior; el driver del capítulo 21 no lo usa (la simulación no necesita bounce buffers), pero los drivers de producción que interactúan con hardware de 32 bits suelen hacerlo.

### Modo de fallo 2: `bus_dmamem_alloc` devuelve `ENOMEM`

`bus_dmamem_alloc` puede devolver `ENOMEM` si el asignador no puede satisfacer las restricciones del tag en el momento de la llamada. Para buffers grandes, esto es más probable de lo que parece: una petición de un buffer contiguo de cuatro megabytes con alineación de 4 KB en un sistema fragmentado puede fallar realmente. Para el buffer de 4 KB del capítulo 21, el fallo es extremadamente improbable, pero el código lo comprueba igualmente.

El patrón: comprobar, registrar el error, destruir el tag recién creado y propagarlo:

```c
err = bus_dmamem_alloc(sc->dma_tag, &sc->dma_vaddr,
    BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->dma_map);
if (err != 0) {
    device_printf(sc->dev, "bus_dmamem_alloc failed: %d\n", err);
    bus_dma_tag_destroy(sc->dma_tag);
    sc->dma_tag = NULL;
    return (err);
}
```

El punto clave es la destrucción del tag en el camino de error. Olvidarla filtra el tag en cada attach fallido; tras muchos reintentos se convierte en una fuga perceptible. El código de la etapa 1 sigue este patrón; cada nueva llamada en el camino de asignación en secciones posteriores también debe hacerlo.

### Modo de fallo 3: `bus_dmamap_load` devuelve `EINVAL`

`bus_dmamap_load` devuelve `EINVAL` en dos casos relacionados:

1. El buffer es más grande que el `maxsize` del tag. Por ejemplo, cargar un buffer de 8 KB con un tag cuyo `maxsize` es 4 KB falla inmediatamente con `EINVAL`. El callback se llama con `error = EINVAL` como confirmación.
2. Las propiedades del buffer violan las restricciones del tag de una forma que el kernel puede detectar estáticamente (alineación, límite). Estos casos son más raros porque las restricciones del tag suelen coincidir con las características del buffer por diseño.

El patrón: comprobar, registrar el error, liberar la memoria, destruir el tag y propagarlo:

```c
err = bus_dmamap_load(sc->dma_tag, sc->dma_map,
    sc->dma_vaddr, MYFIRST_DMA_BUFFER_SIZE,
    myfirst_dma_single_map, &sc->dma_bus_addr, BUS_DMA_NOWAIT);
if (err != 0) {
    device_printf(sc->dev, "bus_dmamap_load failed: %d\n", err);
    bus_dmamem_free(sc->dma_tag, sc->dma_vaddr, sc->dma_map);
    sc->dma_vaddr = NULL;
    bus_dma_tag_destroy(sc->dma_tag);
    sc->dma_tag = NULL;
    return (err);
}
```

El código de la etapa 1 tiene este patrón. Hay que tener en cuenta que `EINVAL` es distinto de `EFBIG`: `EINVAL` se pasa al callback si los argumentos son inválidos; `EFBIG` regresa en el callback cuando el mapeo no puede lograrse dentro de las restricciones de segmento del tag. La carga de segmento único del capítulo 21 es demasiado simple para ver `EFBIG`; las cargas scatter-gather de buffers grandes sí pueden verlo.

### Modo de fallo 4: `bus_dmamap_load` devuelve `EINPROGRESS`

`EINPROGRESS` significa que el kernel ha encolado la carga porque los bounce buffers (u otros recursos de mapeo) no están disponibles en ese momento. El callback se ejecutará más adelante, en un contexto diferente, cuando los recursos queden libres. El capítulo 21 usa `BUS_DMA_NOWAIT`, que prohíbe este comportamiento (el kernel devuelve `ENOMEM` en su lugar), por lo que `EINPROGRESS` no aparece en el driver del capítulo. Un driver que usa `BUS_DMA_WAITOK` o flags en cero y recibe `EINPROGRESS` tiene que hacer más trabajo:

```c
err = bus_dmamap_load(sc->dma_tag, sc->dma_map, buf, len,
    my_callback, sc, 0);
if (err == EINPROGRESS) {
    /* Do not free buf or destroy the tag here; the callback will
     * run later. The caller must be prepared to handle the load
     * completing at any time. */
    return (0);
}
```

El driver debe entonces encargarse de que el callback registre el resultado en un lugar que el resto del driver pueda consultar (normalmente un campo de softc protegido por la lockfunc del tag), y el llamador que está "esperando" la carga debe bloquearse en una variable de condición sobre la que el callback hace broadcast. Este es el patrón de carga diferida; es lo suficientemente complejo como para que la mayoría de los drivers prefieran `BUS_DMA_NOWAIT` con reintentos ante fallos sobre la ruta diferida.

El driver del capítulo 21 no utiliza este patrón. La sección 7 lo menciona por completitud; el lector que se encuentre con `EINPROGRESS` en otro driver sabrá qué significa y dónde buscar.

### Modo de fallo 5: el motor informa de una transferencia parcial

El motor simulado no informa de transferencias parciales en su comportamiento base; o completa el `DMA_LEN` completo o activa `DMA_STATUS_ERR`. Los motores reales a veces informan de una transferencia parcial: el motor copió menos bytes de los solicitados y señaló la finalización con el recuento parcial en un registro de longitud.

Un driver que detecta una transferencia parcial debe decidir qué hacer: reintentar, tratarlo como un error fatal o pasar el resultado parcial al espacio de usuario con un indicador de error. La simulación del capítulo puede ampliarse para modelar transferencias parciales haciendo que el callout establezca un campo de longitud transferida menor:

```c
uint32_t actual_len = len;
/* For the lab in Section 7, force a partial transfer every 100 tries. */
if ((sim->dma_transfer_count++ % 100) == 0)
    actual_len = len / 2;
/* ... perform memcpy of actual_len bytes ... */
sim->dma.transferred_len = actual_len;
sim->dma.status = MYFIRST_DMA_STATUS_DONE;
```

El driver lee la longitud tras la sincronización:

```c
uint32_t xferred;
xferred = CSR_READ_4(sc, MYFIRST_REG_DMA_XFERRED);
if (xferred < expected_len) {
    device_printf(sc->dev,
        "partial DMA: requested %u, got %u\n",
        expected_len, xferred);
    atomic_add_64(&sc->dma_partials, 1);
    /* Decide: retry, report, ignore. */
}
```

El código base del capítulo 21 no implementa la notificación de transferencias parciales; el motor simulado siempre completa la longitud total. El laboratorio 5 de los ejercicios de desafío de la sección 7 añade el comportamiento de transferencia parcial y recorre una estrategia de reintento. Lo que importa es el patrón, no el registro `xferred` concreto; toda transferencia parcial tiene la misma forma en el lado del driver.

### Modo de fallo 6: el motor nunca señala la finalización (timeout)

El motor puede activar `STATUS_RUNNING` y no avanzar nunca a `DONE`. En la simulación esto ocurre si el callout se descarta (por ejemplo, el pool de callouts se agota o el callout es cancelado por otro camino de ejecución). En hardware real ocurre si el dispositivo se bloquea, el enlace PCIe cae o el firmware tiene un error.

El helper de interrupciones del capítulo 21 (`myfirst_dma_do_transfer_intr`) utiliza `cv_timedwait` con un timeout de un segundo. Si la espera devuelve `EWOULDBLOCK`, la transferencia se considera bloqueada. La respuesta del driver:

```c
if (err == EWOULDBLOCK) {
    CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_ABORT);
    if (direction == MYFIRST_DMA_DIR_WRITE)
        bus_dmamap_sync(sc->dma_tag, sc->dma_map,
            BUS_DMASYNC_POSTWRITE);
    else
        bus_dmamap_sync(sc->dma_tag, sc->dma_map,
            BUS_DMASYNC_POSTREAD);
    sc->dma_in_flight = false;
    atomic_add_64(&sc->dma_timeouts, 1);
    MYFIRST_UNLOCK(sc);
    return (ETIMEDOUT);
}
```

Ocurren cuatro cosas: el motor se aborta (para que no complete después de que el helper retorne), se emite la sincronización POST (para que el mapa no quede en estado PRE), `dma_in_flight` se borra (para que pueda iniciarse la siguiente transferencia) y se incrementa un contador (para la observabilidad). El helper devuelve entonces `ETIMEDOUT` al código que lo invocó.

Un driver real también debería considerar si restablecer el dispositivo en este punto. Si los timeouts son infrecuentes, abortar y continuar es lo apropiado; si son frecuentes, puede que sea necesario un restablecimiento completo. El patrón del capítulo 21 es abortar y continuar; el camino de restablecimiento es un ejercicio de refactorización de la sección 8.

La simulación del capítulo puede ampliarse para que no finalice nunca, permitiendo al lector ejercitar el camino del timeout:

```c
/* Comment out the callout_reset to make transfers hang: */
// callout_reset(&sim->dma.done_co, hz / 100, myfirst_sim_dma_done_co, sim);
```

Tras este cambio, cada transferencia alcanza el timeout. Los contadores del driver suben, el código que realiza la llamada recibe `ETIMEDOUT`, y `kldunload` sigue teniendo éxito (porque el camino de aborto borró `dma_in_flight` y el desmontaje no tiene transferencias en vuelo que esperar). Ejecutar este experimento una vez proporciona al lector una idea concreta de cómo se ve desde fuera un driver con un "dispositivo bloqueado".

### Modo de fallo 7: detach mientras hay una transferencia en vuelo

El camino de detach debe manejar el caso en que hay una transferencia en vuelo en el momento en que se descarga el driver. Los riesgos son:

1. El callout se dispara después de que `myfirst_dma_teardown` haya descargado el mapa, escribiendo en memoria liberada.
2. La tarea se ejecuta después de que `myfirst_dma_teardown` haya destruido el tag, sincronizando contra un tag liberado.
3. El helper está esperando en `dma_cv` en el momento en que el dispositivo está desapareciendo.

El orden de desmontaje del capítulo 20 ya aborda varios de estos casos. El capítulo 21 añade dos barreras más:

```c
void
myfirst_detach_dma_path(struct myfirst_softc *sc)
{
    /* 1. Tell callers no new transfers may start. */
    MYFIRST_LOCK(sc);
    sc->detaching = true;
    MYFIRST_UNLOCK(sc);

    /* 2. If a transfer is in flight, wait for it to complete or time out. */
    MYFIRST_LOCK(sc);
    while (sc->dma_in_flight) {
        cv_timedwait(&sc->dma_cv, &sc->mtx, hz);
    }
    MYFIRST_UNLOCK(sc);

    /* 3. Mask the completion interrupt at the device. */
    CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
        CSR_READ_4(sc, MYFIRST_REG_INTR_MASK) &
        ~MYFIRST_INTR_COMPLETE);

    /* 4. Drain callouts and tasks. */
    callout_drain(&sc->sim.dma.done_co);
    taskqueue_drain(sc->intr_tq, &sc->vectors[MYFIRST_VECTOR_RX].task);

    /* 5. Tear down the DMA resources. */
    myfirst_dma_teardown(sc);
}
```

Los cinco pasos están en el orden que mantiene cada paso subsiguiente seguro. Establecer `detaching = true` impide que comiencen nuevas transferencias (el helper lo comprueba antes de armar). Esperar a que las transferencias en vuelo finalicen o agoten el timeout garantiza que no hay ningún callout programado ni ninguna tarea pendiente. Enmascarar la interrupción impide que cualquier finalización tardía dispare el filtro. Drenar el callout y la tarea garantiza que cualquier callback en curso ha terminado. Desmontar los recursos DMA es seguro porque nada más puede acceder a ellos en ese punto.

El primer paso (establecer `detaching = true`) requiere que el helper de transferencia compruebe el indicador:

```c
if (sc->detaching) {
    MYFIRST_UNLOCK(sc);
    return (ENXIO);
}
```

Esta es una pequeña adición al helper y evita la condición de carrera poco frecuente en la que una prueba del espacio de usuario lanza una transferencia mientras se está ejecutando el detach.

### Modo de fallo 8: agotamiento del bounce buffer

En sistemas de 32 bits, o en sistemas de 64 bits con dispositivos que solo soportan 32 bits, `bus_dma` puede necesitar asignar bounce buffers para satisfacer la restricción de direcciones. El pool de bounce buffers tiene un tamaño fijo; si se agota, `bus_dmamap_load` con `BUS_DMA_NOWAIT` devuelve `ENOMEM`.

La simulación del capítulo 21 no tiene restricciones de dirección reales y el camino principal del capítulo no alcanza este modo. Los drivers de producción para dispositivos con capacidad de 32 bits en sistemas de 64 bits deben manejarlo:

```c
err = bus_dmamap_load(sc->dma_tag, map, buf, len,
    my_callback, sc, BUS_DMA_NOWAIT);
if (err == ENOMEM) {
    /* The bounce pool is exhausted. Options:
     * - Retry later (queue the request).
     * - Fail this transfer and let the caller retry.
     * - Allocate a fresh buffer inside the device's address range. */
    ...
}
```

La mitigación práctica suele ser asignar todos los buffers dentro del rango de direcciones del dispositivo (para que los bounce buffers nunca sean necesarios). Eso es exactamente lo que hace `bus_dmamem_alloc` con un tag apropiado de forma automática: el asignador observa el `highaddr` del tag (o `lowaddr`, dependiendo de en qué lado de la ventana se expresa la restricción de dirección) y asigna dentro del rango. Un driver que usa `bus_dmamem_alloc` para sus buffers DMA nunca ve el agotamiento de bounce buffers; un driver que usa `bus_dmamap_load` sobre memoria del kernel arbitraria (por ejemplo, un mbuf de la pila de red) sí puede verlo.

El patrón estático del capítulo 21 es inmune. El patrón dinámico que se trata en la parte 6 (capítulo 28) no lo es, y el capítulo sobre drivers de red cubre allí las estrategias de reintento en detalle.

### Modo de fallo 9: buffer no alineado

El parámetro `alignment` del tag describe la alineación requerida de las direcciones DMA. Si la dirección del buffer no está alineada, la carga falla.

Para el buffer asignado con `bus_dmamem_alloc` del capítulo 21, el asignador siempre devuelve un buffer alineado; el fallo no ocurre en el camino estático. Para cargas dinámicas de buffers arbitrarios, el driver debe o bien asegurarse de que el buffer esté alineado (copiándolo en un buffer temporal alineado) o bien confiar en la maquinaria de bounce buffers para realizar la alineación automáticamente. El kernel gestiona esto de forma transparente con `bus_dmamap_load_mbuf_sg` (la maquinaria de mbuf produce segmentos alineados mediante bounce); para `bus_dmamap_load` en bruto, el driver es responsable de ello.

La simulación del capítulo no modela restricciones de alineación más allá de aceptar `alignment = 4`. Un ejercicio de desafío de la sección 7 invita al lector a establecer `alignment = 64`, observar que `bus_dmamem_alloc` sigue devolviendo un buffer alineado (porque los asignadores son conscientes de la alineación) y, a continuación, intentar cargar un buffer deliberadamente desalineado para ver el fallo.

### Modo de fallo 10: el callback de carga se ejecuta con `error` distinto de cero

`bus_dmamap_load` puede llamar al callback con `error = EFBIG` si la carga es lógicamente válida pero no puede lograrse con las restricciones de segmento del tag. El callback debe manejar esto:

```c
static void
myfirst_dma_single_map(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    bus_addr_t *addr = arg;

    if (error != 0) {
        printf("myfirst: dma load callback error %d\n", error);
        /* Do not write *addr; the caller checks for zero. */
        return;
    }

    KASSERT(nseg == 1, ("myfirst: unexpected DMA segment count %d", nseg));
    *addr = segs[0].ds_addr;
}
```

El código que realiza la llamada detecta entonces el fallo comprobando la dirección de bus de salida:

```c
sc->dma_bus_addr = 0;
err = bus_dmamap_load(...);
if (err != 0 || sc->dma_bus_addr == 0) {
    /* Failure: either the load returned non-zero, or the callback
     * ran with error != 0 and did not populate the address. */
    ...
}
```

Las dos comprobaciones juntas cubren todos los modos de fallo: error síncrono de la carga (`err != 0`), error del callback con la carga devolviendo cero (`dma_bus_addr == 0`). El código del capítulo 21 en la etapa 1 tiene ambas comprobaciones y maneja ambos casos de forma uniforme.

### Modo de fallo 11: se omite una llamada a sync

Un driver que omite una llamada a sync no falla de inmediato en plataformas coherentes; el error es latente. La sección 4 ya cubrió esto. El punto clave para la sección 7 es que no existe detección automática para las sincronizaciones faltantes; el driver debe estar estructurado de forma que sea imposible olvidar las sincronizaciones. El patrón del capítulo 21 (el PRE siempre es la primera acción en el helper; el POST siempre es la última) es una forma de lograrlo. Otra es usar una función contenedora que haga la sincronización alrededor de un callback específico del dispositivo; esto es lo que hacen internamente iflib y otros frameworks.

`WITNESS` detecta ciertos errores relacionados con la sincronización. Si el driver mantiene el lock incorrecto al llamar a una función `bus_dmamap_*` que el `lockfunc` del tag espera, `WITNESS` avisa. Si el driver llama a un sync desde un contexto en el que `busdma_lock_mutex` intentaría adquirir un lock, `WITNESS` detecta la discrepancia. Ejecutar el driver bajo `WITNESS` con regularidad es la mejor defensa contra los errores latentes relacionados con la sincronización.

### Modo de fallo 12: el callback establece `*addr` a cero y el driver continúa

Una variante sutil del modo de fallo 10. El callback se ejecuta, la carga devuelve cero, el argumento `error` del callback resulta ser cero, pero `nseg == 0`. Esto no puede ocurrir con buffers respaldados por `bus_dmamem_alloc` (que siempre producen un segmento), pero sí puede ocurrir en algunos casos límite de cargas arbitrarias. El patrón es:

```c
static void
myfirst_dma_single_map(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    bus_addr_t *addr = arg;

    if (error != 0) {
        printf("myfirst: dma load callback error %d\n", error);
        *addr = 0;
        return;
    }

    if (nseg != 1) {
        printf("myfirst: unexpected DMA segment count %d\n", nseg);
        *addr = 0;
        return;
    }

    *addr = segs[0].ds_addr;
}
```

El poner a cero de forma explícita en cada camino de fallo mantiene la detección de errores del código invocador fiable. La versión más estricta es la que debe usarse por defecto; el `KASSERT` en el código de la etapa 1 es una comprobación solo para builds de depuración, y la comprobación en tiempo de ejecución es lo que debe usar el código de producción. El código de ejemplo del capítulo usa ambas: `KASSERT` en builds de depuración y el puesto a cero explícito como fallback.

### Un patrón: el par `dma_setup` / `dma_teardown`

La sección 8 mueve el código DMA a su propio archivo, pero la sección 7 es el lugar adecuado para nombrar el patrón que motiva la refactorización. Todo driver con capacidad DMA acaba teniendo un par de funciones: una que realiza todas las operaciones de tag, mapa, memoria y carga en el orden correcto con un deshacimiento completo de errores, y otra que las invierte en el orden correcto. El par es el ABI DMA del driver: el resto del driver solo necesita llamar a `dma_setup` en el attach y a `dma_teardown` en el detach. Las funciones en sí manejan todos los caminos de error intermedios.

El par del capítulo 21 es `myfirst_dma_setup` y `myfirst_dma_teardown`. Los drivers de producción suelen tener pares por subsistema (`re_dma_alloc` y `re_dma_free` para `if_re`, `nvme_qpair_dma_create` y `nvme_qpair_dma_destroy` para NVMe). La forma es siempre la misma; la profundidad es lo que varía según la complejidad del driver.

### En resumen de la sección 7

La sección 7 recorrió doce modos de fallo, cada uno con su patrón. Los patrones son: comprobar cada valor de retorno, deshacer cada asignación previa, poner a cero explícitamente los campos de salida ante fallos, drenar callouts y tareas antes de desmontar los recursos, enmascarar las interrupciones antes de destruir el estado DMA asociado y mantener el par setup/teardown simétrico para que el camino de detach sea la inversión del camino de attach. Todo driver DMA de producción sigue estos patrones; el código del capítulo proporciona una plantilla para aplicarlos.

La sección 8 es la refactorización final. El código DMA se mueve a `myfirst_dma.c` y `myfirst_dma.h`; el Makefile se actualiza; la versión sube a `1.4-dma`; el documento `DMA.md` captura el diseño. El driver en ejecución del capítulo queda entonces listo para el trabajo de gestión de energía del capítulo 22 y la extensión del anillo de descriptores del capítulo 28.



## Sección 8: refactorización y versionado de tu driver con capacidad DMA

La sección 7 cerró el ámbito funcional. La sección 8 es el paso de limpieza. El código DMA se ha acumulado en `myfirst.c`, la extensión DMA del backend de simulación ha crecido dentro de `myfirst_sim.c`, el softc ha ganado varios campos nuevos, los sysctls han crecido y el filtro de interrupciones ha ganado una rama `MYFIRST_INTR_COMPLETE`. Esta sección reúne el código específico de DMA en `myfirst_dma.c` y `myfirst_dma.h`, limpia la nomenclatura, actualiza el Makefile, incrementa la versión, añade el documento `DMA.md` y ejecuta la pasada de regresión final. Etiqueta de versión `1.4-dma`.

La refactorización es pequeña en términos absolutos (aproximadamente 200 líneas de código desplazadas, más una nueva cabecera con los prototipos de funciones públicas y las macros), pero el beneficio estructural es grande: la arquitectura del driver muestra ahora, de un vistazo, que DMA es un subsistema de primer nivel con su propio archivo, su propia API pública y su propia documentación.

### La disposición final de archivos

Tras la sección 8, la disposición de archivos del driver es:

```text
myfirst.c          # Top-level: attach/detach, cdev, ioctl
myfirst.h          # Shared macros, softc struct, public prototypes
myfirst_hw.c       # Chapter 16: register accessor layer
myfirst_hw.h
myfirst_hw_pci.c   # Chapter 18: real PCI backend
myfirst_sim.c      # Chapter 17: simulated backend (now includes DMA engine)
myfirst_sim.h
myfirst_pci.c      # Chapter 18: PCI attach/detach
myfirst_pci.h
myfirst_intr.c     # Chapter 19: legacy interrupt path
myfirst_intr.h
myfirst_msix.c     # Chapter 20: MSI/MSI-X path
myfirst_msix.h
myfirst_dma.c      # Chapter 21: DMA setup/teardown/transfer
myfirst_dma.h
myfirst_sync.h     # Chapter 11: locking macros
cbuf.c             # Chapter 15: circular buffer
cbuf.h
```

Quince archivos fuente más cabeceras compartidas. Cada archivo tiene una responsabilidad concreta. La refactorización mantiene esta separación: `myfirst_dma.c` es autocontenido y solo depende de `myfirst.h` y las cabeceras públicas del kernel.

### La cabecera `myfirst_dma.h`

La cabecera declara la API pública de DMA y las constantes compartidas:

```c
/* myfirst_dma.h */
#ifndef _MYFIRST_DMA_H_
#define _MYFIRST_DMA_H_

/* DMA buffer size used by myfirst. Matches the Chapter 21 simulated
 * engine's scratch size. A real device would use a value from the
 * hardware's documented capabilities. */
#define	MYFIRST_DMA_BUFFER_SIZE		4096u

/* DMA register offsets (relative to the BAR base). */
#define	MYFIRST_REG_DMA_ADDR_LOW	0x20
#define	MYFIRST_REG_DMA_ADDR_HIGH	0x24
#define	MYFIRST_REG_DMA_LEN		0x28
#define	MYFIRST_REG_DMA_DIR		0x2C
#define	MYFIRST_REG_DMA_CTRL		0x30
#define	MYFIRST_REG_DMA_STATUS		0x34

/* DMA_DIR values. */
#define	MYFIRST_DMA_DIR_WRITE		0u	/* host-to-device */
#define	MYFIRST_DMA_DIR_READ		1u	/* device-to-host */

/* DMA_CTRL bits. */
#define	MYFIRST_DMA_CTRL_START		(1u << 0)
#define	MYFIRST_DMA_CTRL_ABORT		(1u << 1)
#define	MYFIRST_DMA_CTRL_RESET		(1u << 31)

/* DMA_STATUS bits. */
#define	MYFIRST_DMA_STATUS_DONE		(1u << 0)
#define	MYFIRST_DMA_STATUS_ERR		(1u << 1)
#define	MYFIRST_DMA_STATUS_RUNNING	(1u << 2)

/* Public API. */
struct myfirst_softc;

int	myfirst_dma_setup(struct myfirst_softc *sc);
void	myfirst_dma_teardown(struct myfirst_softc *sc);
int	myfirst_dma_do_transfer(struct myfirst_softc *sc,
	    int direction, size_t length);
void	myfirst_dma_handle_complete(struct myfirst_softc *sc);
void	myfirst_dma_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_DMA_H_ */
```

Cinco funciones públicas. `myfirst_dma_setup` se llama una vez desde attach; `myfirst_dma_teardown` se llama una vez desde detach; `myfirst_dma_do_transfer` es invocada por los manejadores de sysctl o por otras partes del driver que deseen iniciar una transferencia DMA; `myfirst_dma_handle_complete` se llama desde la tarea rx cuando se observa `MYFIRST_INTR_COMPLETE`; `myfirst_dma_add_sysctls` registra los contadores de DMA y los sysctls de prueba.

La cabecera utiliza una declaración anticipada (`struct myfirst_softc`) para evitar inclusiones circulares. La implementación accede a la definición completa del softc a través de `myfirst.h`.

### El archivo `myfirst_dma.c`

El archivo reúne el código DMA que se ha ido acumulando a lo largo de las secciones 3 a 7. La cabecera del archivo incluye las cabeceras estándar que necesita la API de DMA:

```c
/* myfirst_dma.c */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_dma.h"
```

La implementación define a continuación el callback de segmento único, `myfirst_dma_setup`, `myfirst_dma_teardown`, `myfirst_dma_do_transfer`, `myfirst_dma_handle_complete` y `myfirst_dma_add_sysctls`. Cada función se toma de su sección anterior sin ningún cambio de comportamiento; la refactorización es puramente un cambio de ubicación.

La única adición destacable es `myfirst_dma_handle_complete`, que centraliza la sincronización POST que la tarea de la sección 6 realizaba de forma inline:

```c
void
myfirst_dma_handle_complete(struct myfirst_softc *sc)
{
	MYFIRST_ASSERT_LOCKED(sc);

	if (!sc->dma_in_flight)
		return;

	sc->dma_in_flight = false;
	if (sc->dma_last_direction == MYFIRST_DMA_DIR_WRITE)
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_POSTWRITE);
	else
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_POSTREAD);

	sc->dma_last_status = CSR_READ_4(sc, MYFIRST_REG_DMA_STATUS);
	atomic_add_64(&sc->dma_complete_tasks, 1);
	cv_broadcast(&sc->dma_cv);
}
```

El cuerpo de la tarea rx queda así:

```c
static void
myfirst_msix_rx_task_fn(void *arg, int npending)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;

	MYFIRST_LOCK(sc);
	if (sc->hw == NULL || !sc->pci_attached) {
		MYFIRST_UNLOCK(sc);
		return;
	}
	sc->intr_last_data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
	sc->intr_task_invocations++;
	cv_broadcast(&sc->data_cv);
	myfirst_dma_handle_complete(sc);
	MYFIRST_UNLOCK(sc);
}
```

La tarea es cuatro líneas más corta, y la lógica específica de DMA reside íntegramente en `myfirst_dma.c`. Si en un capítulo posterior (o un futuro colaborador) se quiere modificar el comportamiento de la sincronización POST o la lógica de finalización, el cambio afecta a un único archivo.

### El Makefile actualizado

El Makefile del capítulo 20 era:

```makefile
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

El Makefile de la etapa 4 del capítulo 21 añade `myfirst_dma.c` y actualiza la cadena de versión:

```makefile
KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       myfirst_msix.c \
       myfirst_dma.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.4-dma\"

.include <bsd.kmod.mk>
```

El cambio son dos líneas: el nuevo archivo fuente en `SRCS` y la cadena de versión actualizada. Ambas son necesarias; un Makefile al que le falte el nuevo fuente no enlazará correctamente porque `myfirst.c` llama a `myfirst_dma_setup`, que no existe en ningún objeto compilado.

### La cadena de versión en `kldstat`

El macro `MYFIRST_VERSION_STRING` se usa en `MODULE_VERSION` y en un `SYSCTL_STRING` que expone la versión. Tras la refactorización, `kldstat -v | grep myfirst` muestra:

```text
myfirst 1.4-dma (pseudo-device)
```

y `sysctl dev.myfirst.0.version` devuelve la misma cadena. Los operadores que vean el driver cargado pueden consultar la versión de un vistazo; la cadena resulta muy útil como diagnóstico cuando se mezclan versiones de desarrollo y de producción del driver en el mismo sistema de pruebas.

### El documento `DMA.md`

El capítulo 20 introdujo `MSIX.md`. El capítulo 21 añade `DMA.md`, un único archivo de referencia que documenta la API pública del subsistema DMA, el mapa de registros, los diagramas de flujo y los contadores. Un esquema de ejemplo:

```markdown
# DMA Subsystem

## Purpose

The DMA layer allows the driver to transfer data between host memory
and the device without CPU-per-byte involvement. It is used for every
transfer larger than a few words; smaller register reads and writes
still use MMIO directly.

## Public API

- `myfirst_dma_setup(sc)`: called from attach. Creates the tag,
  allocates the buffer, loads the map, populates `sc->dma_bus_addr`.
- `myfirst_dma_teardown(sc)`: called from detach. Reverses setup.
- `myfirst_dma_do_transfer(sc, dir, len)`: triggers one DMA transfer
  and waits for completion.
- `myfirst_dma_handle_complete(sc)`: called from the rx task when
  `MYFIRST_INTR_COMPLETE` was observed.
- `myfirst_dma_add_sysctls(sc)`: registers the DMA counters and test
  sysctls under `dev.myfirst.N.`.

## Register Layout

... (table from Section 5) ...

## Flow Diagrams

Host-to-device:
    ... (diagram from Section 4, Pattern A) ...

Device-to-host:
    ... (diagram from Section 4, Pattern B) ...

## Counters

- `dev.myfirst.N.dma_transfers_write`: successful host-to-device transfers.
- `dev.myfirst.N.dma_transfers_read`: successful device-to-host transfers.
- `dev.myfirst.N.dma_errors`: transfers that returned EIO.
- `dev.myfirst.N.dma_timeouts`: transfers that hit the 1-second timeout.
- `dev.myfirst.N.dma_complete_intrs`: completion-bit observations in the filter.
- `dev.myfirst.N.dma_complete_tasks`: completion processing in the task.

## Observability

`sysctl dev.myfirst.N.dma_*` returns the full counter set. A healthy
driver has `dma_complete_intrs == dma_complete_tasks` and both equal
to `dma_transfers_write + dma_transfers_read + dma_errors + dma_timeouts`.

## Testing

The sysctls `dma_test_write` and `dma_test_read` trigger transfers
from user space. Writing any value to `dma_test_write` fills the
buffer with the low byte of the value and runs a host-to-device
transfer; writing any value to `dma_test_read` runs a device-to-host
transfer and logs the first eight bytes to `dmesg`.

## Known Limitations

- Single buffer, single transfer at a time.
- No descriptor-ring support (Part 6, Chapter 28).
- No per-NUMA-node allocation (Part 7, Chapter 33).
- No partial-transfer reporting (exercise for the reader).

## See Also

- `bus_dma(9)`, `/usr/src/sys/sys/bus_dma.h`.
- `/usr/src/sys/dev/re/if_re.c` for a production descriptor-ring driver.
- `INTERRUPTS.md`, `MSIX.md` for the interrupt path the completion uses.
```

El documento tiene aproximadamente una página y resulta útil tanto como referencia para colaboradores como repaso para el lector. Los drivers de producción reales suelen tener sus archivos `README` estructurados de forma similar: subsistema a subsistema, con un breve diagrama de flujo y una lista de contadores.

### La pasada de regresión

La prueba de regresión del capítulo 20 se amplía con comprobaciones específicas de DMA. La prueba completa (llámala `full_regression_ch21.sh`) ejecuta estos pasos en un arranque limpio:

1. El kernel está en el estado esperado (`uname -v` muestra `1.4-dma` en la línea de `myfirst`, y `INVARIANTS`/`WITNESS` en la configuración).
2. Se carga el módulo (`kldload ./myfirst.ko`). `dmesg` muestra el banner de DMA y el banner del modo de interrupción.
3. Se verifica el estado inicial de los contadores (`dma_transfers_write == 0`, etc.).
4. Se ejecutan 1000 transferencias de escritura mediante el sysctl. Se comprueba que los contadores se incrementan.
5. Se ejecutan 1000 transferencias de lectura mediante el sysctl. Se comprueba que los contadores se incrementan.
6. Se ejecutan 100 transferencias con la inyección de errores simulada activada. Se comprueba que el contador `dma_errors` aumenta.
7. Se ejecutan 10 transferencias con el callout desactivado (el motor se bloquea). Se comprueba que el contador `dma_timeouts` aumenta y que `dma_in_flight` se limpia tras cada timeout.
8. Se descarga el módulo (`kldunload myfirst`). Se verifica una descarga limpia sin avisos de `WITNESS`.
9. Se repite el ciclo de carga y descarga 50 veces para detectar fugas.

El script completo reside en `examples/part-04/ch21-dma/labs/full_regression_ch21.sh`. Una ejecución exitosa imprime una línea por paso con los contadores observados y un `PASS` final. Cualquier paso que falle imprime una línea `FAIL` con los valores reales frente a los esperados.

### Lo que consiguió la refactorización

Tras la sección 8:

- El código DMA reside en su propio archivo con una API pública documentada.
- El Makefile conoce el nuevo fuente.
- La etiqueta de versión refleja el trabajo del capítulo.
- El documento `DMA.md` sirve de referencia para colaboradores y capítulos futuros.
- La prueba de regresión cubre cada ruta DMA que enseña el capítulo.

El driver es ahora `1.4-dma`, y el diff respecto a `1.3-msi` supone unas cuatrocientas líneas añadidas más cincuenta líneas movidas. Cada línea es trazable: o bien implementa un concepto de alguna de las secciones anteriores, o bien es mantenimiento (cabeceras, Makefile, documentación).

### Ejercicio: crea una utilidad que verifique los datos DMA

Un ejercicio práctico para la sección 8. Escribe una pequeña utilidad en espacio de usuario que ejercite la ruta DMA y verifique los datos de extremo a extremo.

La misión de la utilidad:
1. Abrir el sysctl `dev.myfirst.0.dma_test_write`, escribir un patrón conocido (por ejemplo, 0xA5).
2. Abrir `dev.myfirst.0.dma_test_read`, disparar una lectura.
3. Comprobar en `dmesg` (o en un sysctl personalizado que exponga los primeros bytes del buffer) el patrón esperado (0x5A del simulador).
4. Repetir 100 veces con patrones distintos e informar de un resumen.

La utilidad es un script de shell por simplicidad:

```sh
#!/bin/sh
fail=0
for pat in $(jot 100 1); do
    hex=$(printf "0x%02x" $pat)
    sysctl -n dev.myfirst.0.dma_test_write=$pat >/dev/null
    sysctl -n dev.myfirst.0.dma_test_read=1 >/dev/null
    # ... check the result ...
done
echo "failures: $fail"
```

El ejercicio es abierto: puedes extenderlo con mediciones de tiempos, estimaciones de tasas de error o una comparación con los valores del contador `dma_errors`. La lista de desafíos de la sección 8 sugiere algunas direcciones.

### Cerrando la sección 8

La sección 8 es la línea de meta. El código DMA tiene su propio archivo; el Makefile lo compila; la etiqueta de versión dice `1.4-dma`; la documentación recoge el diseño; la prueba de regresión ejercita cada ruta. El driver del capítulo está ahora listo para el trabajo de gestión de energía del capítulo 22, que usará la ruta de desmontaje DMA para detener las transferencias durante la suspensión, y listo para el capítulo 28, que extenderá el patrón de un único buffer a un anillo de descriptores completo.

Las secciones del capítulo están completas. El material restante es de referencia y práctica: un recorrido por un driver DMA de producción (`if_re`), vistas más detalladas sobre bounce buffers, dispositivos de 32 bits, IOMMU, flags de coherencia y herencia de etiquetas padre, los laboratorios, los desafíos, la referencia de resolución de problemas y el cierre final.



## Lectura conjunta de un driver real: if_re.c

Un recorrido por `/usr/src/sys/dev/re/if_re.c` que relaciona su código DMA con los conceptos del capítulo 21. `if_re(4)` es el driver para la familia de chips Gigabit Ethernet RealTek 8139C+ / 8169 / 8168, suficientemente común como para estar presente en muchos entornos de laboratorio y lo bastante compacto (unas cuatro mil líneas en total) como para leerlo en una semana de trayectos de duración media. Su código DMA ocupa unas cuatrocientas líneas y reside en unas pocas funciones con nombres claros, por lo que resulta una buena primera lectura de un driver real.

> **Cómo leer este recorrido.** Los listados de las subsecciones siguientes son extractos abreviados de `re_allocmem()`, `re_dma_map_addr()`, `re_encap()` y `re_detach()` en `/usr/src/sys/dev/re/if_re.c`. Hemos conservado la lista de argumentos de cada llamada y el flujo de control circundante, pero mostramos únicamente los fragmentos que ilustran una idea de `bus_dma(9)` que se está tratando; las funciones reales incluyen más gestión de errores, más etiquetas hijo y más contabilidad. Cada símbolo que nombran los listados, desde `bus_dma_tag_create` hasta `bus_dmamap_load` pasando por `re_dma_map_addr`, es un identificador real de FreeBSD que puedes encontrar con una búsqueda de símbolos. La misma convención de abreviación aparece en el callback `nvme_single_map` del driver `nvme(4)` en `/usr/src/sys/dev/nvme/nvme_private.h` y en cualquier driver que empaquete un callback de segmento único como función auxiliar.

### La etiqueta padre

`re_allocmem` comienza creando la etiqueta padre:

```c
lowaddr = BUS_SPACE_MAXADDR;
if ((sc->rl_flags & RL_FLAG_PCIE) == 0)
    lowaddr = BUS_SPACE_MAXADDR_32BIT;
error = bus_dma_tag_create(bus_get_dma_tag(dev), 1, 0,
    lowaddr, BUS_SPACE_MAXADDR, NULL, NULL,
    BUS_SPACE_MAXSIZE_32BIT, 0, BUS_SPACE_MAXSIZE_32BIT, 0,
    NULL, NULL, &sc->rl_parent_tag);
```

Hay dos aspectos que merece la pena destacar.

El primero es la decisión sobre `lowaddr`. En las variantes PCIe del chip, el DMA puede alcanzar toda la memoria (`BUS_SPACE_MAXADDR`). En las variantes antiguas sin PCIe, el chip es solo de 32 bits (`BUS_SPACE_MAXADDR_32BIT`, es decir, el dispositivo no puede acceder a memoria por encima de 4 GB). El driver detecta esto en el momento del attach y establece `lowaddr` en consecuencia. Cualquier etiqueta hijo creada a partir de `rl_parent_tag` hereda este límite automáticamente. En un sistema amd64 de 64 bits con 16 GB de RAM, las etiquetas del driver garantizan que los buffers DMA se asignen por debajo de 4 GB en las variantes sin PCIe; los bounce buffers solo entran en juego si el kernel no puede satisfacer esa asignación.

El segundo aspecto es `maxsize = BUS_SPACE_MAXSIZE_32BIT` y `nsegments = 0`. Estos valores producen una etiqueta padre muy permisiva: cualquier hijo con cualquier número de segmentos es válido. Es un idioma habitual; la etiqueta padre solo lleva la restricción de direccionamiento, y los hijos llevan los detalles específicos.

### Las etiquetas hijo por buffer

El driver crea cuatro etiquetas hijo:

- `rl_tx_mtag`: para las cargas útiles de mbuf de transmisión. Alineación 1, tamaño máximo `MCLBYTES * RL_NTXSEGS`, hasta `RL_NTXSEGS` segmentos por mapeo.
- `rl_rx_mtag`: para las cargas útiles de mbuf de recepción. Alineación 8, tamaño máximo `MCLBYTES`, 1 segmento por mapeo.
- `rl_tx_list_tag`: para el anillo de descriptores de transmisión. Alineación `RL_RING_ALIGN` (256 bytes), tamaño máximo `tx_list_size`, 1 segmento.
- `rl_rx_list_tag`: para el anillo de descriptores de recepción. Alineación `RL_RING_ALIGN`, tamaño máximo `rx_list_size`, 1 segmento.

Cada hijo pasa `sc->rl_parent_tag` como padre. Cada uno lleva sus propias restricciones de alineación y de número de segmentos. El kernel combina el límite de direccionamiento de 32 bits del padre con las restricciones propias de cada hijo; el driver no necesita repetir el límite de direccionamiento en cada etiqueta hijo.

Las etiquetas del anillo de descriptores son de un único segmento: el anillo se asigna de forma contigua. Las etiquetas de carga útil de mbuf son de múltiples segmentos: un único paquete recibido puede ser una cadena de mbufs, y el motor DMA de la NIC admite perfectamente una lista de dispersión-recolección de segmentos siempre que el total quepa dentro del tamaño máximo.

### La asignación del anillo

La asignación del anillo de descriptores usa `bus_dmamem_alloc` con `BUS_DMA_COHERENT`:

```c
error = bus_dmamem_alloc(sc->rl_ldata.rl_tx_list_tag,
    (void **)&sc->rl_ldata.rl_tx_list,
    BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO,
    &sc->rl_ldata.rl_tx_list_map);
```

Los flags son `WAITOK | COHERENT | ZERO`: se permite dormir, se prefiere memoria coherente, y se inicializa a cero. El flag de coherencia es apropiado porque el anillo está en la ruta crítica tanto para la CPU (que produce entradas TX) como para la NIC (que las consume y escribe el estado de vuelta). La inicialización a cero es importante porque el estado inicial del anillo debe tener todas las entradas marcadas como «no en uso»; la memoria sin inicializar podría provocar que la NIC intentara hacer DMA basándose en contenidos de descriptores basura.

Tras `bus_dmamem_alloc`, el driver llama a `bus_dmamap_load` con un callback de segmento único:

```c
sc->rl_ldata.rl_tx_list_addr = 0;
error = bus_dmamap_load(sc->rl_ldata.rl_tx_list_tag,
     sc->rl_ldata.rl_tx_list_map, sc->rl_ldata.rl_tx_list,
     tx_list_size, re_dma_map_addr,
     &sc->rl_ldata.rl_tx_list_addr, BUS_DMA_NOWAIT);
```

Y el callback:

```c
static void
re_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    bus_addr_t *addr;

    if (error)
        return;

    KASSERT(nseg == 1, ("too many DMA segments, %d should be 1", nseg));
    addr = arg;
    *addr = segs->ds_addr;
}
```

El callback es casi idéntico al `myfirst_dma_single_map` del capítulo 21. El código del driver es portable en todas las plataformas FreeBSD con capacidad DMA porque el callback, la etiqueta y el asignador respetan la disciplina de `bus_dma`.

### El patrón por transferencia

Para cada paquete transmitido, el driver llama a `bus_dmamap_load_mbuf_sg` sobre el mbuf del paquete:

```c
error = bus_dmamap_load_mbuf_sg(sc->rl_ldata.rl_tx_mtag, txd->tx_dmamap,
    *m_head, segs, &nsegs, BUS_DMA_NOWAIT);
```

La variante `_mbuf_sg` rellena un array de segmentos preasignado, evitando el callback en el caso común. Si la carga devuelve `EFBIG`, el driver intenta compactar la cadena de mbufs con `m_collapse` y vuelve a intentarlo.

Una vez que la carga tiene éxito, el driver sincroniza:

```c
bus_dmamap_sync(sc->rl_ldata.rl_tx_mtag, txd->tx_dmamap,
    BUS_DMASYNC_PREWRITE);
```

Luego escribe los descriptores con las direcciones de los segmentos, activa el doorbell y deja que la NIC transmita. Al completarse, el driver sincroniza de nuevo:

```c
bus_dmamap_sync(sc->rl_ldata.rl_tx_list_tag, sc->rl_ldata.rl_tx_list_map,
    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
```

La sincronización combinada POSTREAD|POSTWRITE es la variante del anillo de descriptores de la sección 4: el driver tanto escribió el descriptor (la dirección, la longitud, los flags) como quiere leer la actualización de estado del dispositivo (transmitido, error, etc.).

### La limpieza

La ruta de detach (`re_dma_free`) refleja el orden de asignación, pero a la inversa:

```c
if (sc->rl_ldata.rl_rx_list_tag) {
    if (sc->rl_ldata.rl_rx_list_addr)
        bus_dmamap_unload(sc->rl_ldata.rl_rx_list_tag,
            sc->rl_ldata.rl_rx_list_map);
    if (sc->rl_ldata.rl_rx_list)
        bus_dmamem_free(sc->rl_ldata.rl_rx_list_tag,
            sc->rl_ldata.rl_rx_list, sc->rl_ldata.rl_rx_list_map);
    bus_dma_tag_destroy(sc->rl_ldata.rl_rx_list_tag);
}
```

Los dmamaps por buffer creados con `bus_dmamap_create` se destruyen con `bus_dmamap_destroy` en un bucle, y finalmente se destruyen las etiquetas.

### Lo que enseña el recorrido

El código DMA de `if_re` es una aplicación casi textual de los conceptos del capítulo 21. Cada llamada que el capítulo enseña aparece en el driver. Los únicos conceptos que no están en el capítulo son la variante de dispersión-recolección (`bus_dmamap_load_mbuf_sg`) y los mapeos dinámicos por buffer (`bus_dmamap_create`/`bus_dmamap_destroy`), y ambos son extensiones naturales del patrón estático.

Un lector que termina el capítulo 21 y después lee `if_re.c` durante media hora ve las mismas formas. La inversión del capítulo rinde sus frutos frente al código real de un driver.

## Un análisis más detallado de los bounce buffers

La sección 1 introdujo los bounce buffers; la sección 7 describió el modo de error. Este análisis más detallado fija con precisión qué son los bounce buffers, cuándo se utilizan y cómo se manifiesta su coste.

### Qué son los bounce buffers

Un bounce buffer es una región de memoria física dentro del rango direccionable del dispositivo, que se usa para preparar datos cuando el buffer real del driver queda fuera de ese rango. La capa `bus_dma` gestiona un pool de páginas de rebote como recurso interno; un driver que asigna buffers con `bus_dmamem_alloc` dentro del rango del dispositivo nunca toca ese pool.

Cuando un driver carga un buffer mediante `bus_dmamap_load` y el buffer queda fuera del rango del dispositivo (por ejemplo, un mbuf asignado por encima de 4 GB en un dispositivo que solo soporta 32 bits), el kernel:

1. Asigna una página de rebote dentro del rango del dispositivo.
2. En el momento `PREWRITE`, copia el buffer del driver en la página de rebote.
3. Programa el dispositivo con la dirección de la página de rebote.
4. En el momento `POSTREAD`, copia la página de rebote de vuelta al buffer del driver.
5. Libera la página de rebote.

El driver nunca sabe que se ha producido un rebote. El `bus_addr_t` que devuelve el callback es la dirección de la página de rebote; las operaciones de sincronización mueven los datos en ambas direcciones.

### Cuándo importan los bounce buffers

Los bounce buffers son necesarios en tres situaciones:

1. **Dispositivos que solo soportan 32 bits en sistemas con más de 4 GB de RAM.** El dispositivo no puede acceder por encima de 4 GB; cualquier buffer situado allí necesita un rebote por debajo de 4 GB.
2. **Dispositivos con huecos de direccionamiento.** Algunos dispositivos heredados tienen regiones de direcciones inutilizables (por ejemplo, el límite de DMA de 16 MB al estilo ISA). Un buffer dentro del hueco necesita rebotar.
3. **Sistemas con IOMMU activo en los que algunos dispositivos no están registrados en el IOMMU.** Si el IOMMU está en modo de aplicación estricta y un dispositivo no ha sido mapeado, el destino de DMA se redirige mediante rebote a una región mapeada.

En sistemas amd64 modernos con dispositivos y drivers capaces de direccionar 64 bits (que es la mayoría), los bounce buffers son poco frecuentes. En sistemas embebidos o en contextos de compatibilidad, son habituales.

### El coste de rendimiento

Cada rebote implica un `memcpy` del buffer del driver a la región de rebote (en `PREWRITE`) y otro `memcpy` de vuelta (en `POSTREAD`). Para un mbuf de 1500 bytes, esto supone unos 3 KB de tráfico de memoria por paquete; en una NIC de 10 Gbps que recibe un millón de paquetes por segundo, eso representa 3 GB/s de tráfico de rebote, lo que carga el bus de memoria de forma apreciable.

El pool de bounce buffers también tiene un tamaño limitado (ajustable mediante los sysctls `hw.busdma.*`, con valores predeterminados que escalan con la memoria física). En un sistema donde muchos drivers están generando rebotes, el pool puede agotarse; en ese punto, las cargas con `BUS_DMA_NOWAIT` devuelven `ENOMEM` y las cargas sin `NOWAIT` se posponen.

### Cómo evitar los rebotes

Tres estrategias:

1. **Asignar memoria DMA con `bus_dmamem_alloc`.** El asignador respeta por construcción las restricciones de direccionamiento del tag.
2. **Establecer los campos `highaddr`/`lowaddr` del tag para que coincidan con el rango real del dispositivo.** No pases `BUS_SPACE_MAXADDR` para un dispositivo que solo soporta 32 bits; el valor incorrecto hace que el asignador entregue buffers demasiado altos y fuerza los rebotes.
3. **Usar `BUS_DMA_ALLOCNOW`.** Esto preasigna el pool de rebote en el momento de crear el tag, convirtiendo un posible fallo de asignación posterior en un fallo inmediato que el driver puede gestionar durante el attach.

La simulación del capítulo 21 no ejercita los rebotes, pero el concepto es importante para entender el comportamiento real de los drivers. Un driver que "funciona en mi portátil" pero corrompe datos en un servidor con 64 GB de RAM casi con toda seguridad tiene una configuración incorrecta del rango de direcciones que está desencadenando rebotes silenciosos en el portátil y fallando de forma diferente en el servidor.

### Observabilidad

El árbol sysctl `hw.busdma` expone contadores relacionados con los rebotes:

```sh
sysctl hw.busdma
```

Las líneas de interés incluyen `total_bpages`, `free_bpages`, `total_bounced`, `total_deferred` y `lowpriority_bounces`. `total_bounced` es el número total de páginas que han sufrido rebote desde el arranque; un valor distinto de cero en un sistema donde no debería producirse ningún rebote es una pista de que el tag de algún driver está mal configurado.



## Un análisis más detallado de los dispositivos con capacidad de 32 bits

La sección 1 y la discusión sobre bounce buffers ya rozaron este tema; este análisis más detallado reúne la orientación práctica.

### El contexto

Algunos dispositivos PCI y PCIe tienen motores de DMA de 32 bits. Estos motores solo aceptan direcciones de bus de 32 bits, por lo que el tráfico DMA queda confinado a los 4 GB inferiores del espacio de direcciones del bus. En un host de 64 bits con más de 4 GB de RAM, cada buffer DMA debe estar por debajo del límite de 4 GB.

### La configuración del tag

Las constantes relevantes son:

- `BUS_SPACE_MAXADDR`: sin límite de direccionamiento. Úsalo para dispositivos con capacidad de 64 bits.
- `BUS_SPACE_MAXADDR_32BIT`: 0xFFFFFFFF. Úsalo como `lowaddr` para dispositivos que solo soportan 32 bits.
- `BUS_SPACE_MAXADDR_24BIT`: 0x00FFFFFF. Úsalo como `lowaddr` para dispositivos heredados con un límite de 16 MB (poco frecuente).

Juntos, `lowaddr` y `highaddr` describen la ventana *excluida* del espacio de direcciones del bus. El texto de la página del manual dice: "La ventana contiene todas las direcciones mayores que `lowaddr` y menores o iguales que `highaddr`". Las direcciones dentro de la ventana no son accesibles para el dispositivo y deben rebotar; las direcciones fuera de la ventana son accesibles.

Para un dispositivo de 32 bits, la ventana excluida es todo lo que está por encima de 4 GB, por lo que `lowaddr = BUS_SPACE_MAXADDR_32BIT` y `highaddr = BUS_SPACE_MAXADDR`. La ventana se convierte en `(0xFFFFFFFF, BUS_SPACE_MAXADDR]`, que captura exactamente "cualquier cosa por encima de 4 GB".

Para un dispositivo con capacidad de 64 bits sin restricciones, la ventana excluida debe estar vacía. La forma idiomática de expresar esto es `lowaddr = BUS_SPACE_MAXADDR` y `highaddr = BUS_SPACE_MAXADDR`, lo que colapsa la ventana en `(BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR]`, un rango vacío.

El nombre resulta confuso al principio: `lowaddr` suena como un límite inferior, pero en realidad es el *extremo superior* del rango alcanzable por el dispositivo (y el *extremo inferior* de la ventana excluida). El nombre es histórico: `lowaddr` es el extremo inferior de la región excluida, y la región excluida está por encima del rango del dispositivo. El truco mnemotécnico: `lowaddr` es "la última dirección que el dispositivo puede alcanzar".

### Double-Addressing-Cycle (DAC)

PCI dispone de un mecanismo llamado Double Address Cycle (DAC) que permite a los slots de 32 bits direccionar 64 bits emitiendo dos ciclos. Algunos dispositivos que aparentan ser de 32 bits soportan en realidad DAC y pueden alcanzar direcciones de 64 bits. El driver `if_re` comprueba un indicador de familia de chip para decidirlo: las variantes PCIe usan 64 bits completos; las variantes no PCIe solo usan 32 bits. Un driver que no sabe si su dispositivo soporta DAC debe usar 32 bits como valor predeterminado (la opción más segura) y habilitar 64 bits si la hoja de datos confirma el soporte.

### El impacto en el tráfico de rebote

Un dispositivo que solo soporta 32 bits en un servidor de 64 GB:
- La mayoría de los buffers los asigna el asignador del kernel predeterminado por encima de 4 GB (dado que la mayor parte de la memoria está allí).
- Cada carga desencadena un rebote.
- El pool de rebote limita el rendimiento del driver a lo que el pool pueda procesar.

La solución es siempre usar `bus_dmamem_alloc` para los buffers estáticos del driver y asegurarse de que el `lowaddr` del tag está configurado correctamente para que el asignador coloque los buffers por debajo de 4 GB. Para el caso dinámico (mbufs, buffers de usuario), la pila de red proporciona clústeres de mbuf preasignados por debajo de 4 GB para los drivers que los solicitan mediante tipos de mbuf compatibles con `bus_dma`, aunque esta ruta está dentro de iflib y no es relevante para el capítulo 21.



## Un análisis más detallado de la integración con el IOMMU

La sección 1 mencionó el IOMMU. Este análisis más detallado cubre qué hace el IOMMU, cómo saber si está activo y por qué la API `bus_dma` está diseñada para ser transparente a él.

### Qué hace el IOMMU

Un IOMMU (Input-Output Memory Management Unit, unidad de gestión de memoria de entrada/salida) es una capa de traducción entre las direcciones de bus de un dispositivo y las direcciones físicas del host. Con el IOMMU activo:

- El dispositivo ve la dirección de bus X.
- El IOMMU traduce X a la dirección física Y.
- El controlador de memoria escribe en la dirección física Y.

La traducción es por dispositivo (cada dispositivo tiene su propia tabla de mapeo), y por defecto el IOMMU solo permite a un dispositivo acceder a la memoria que el kernel ha mapeado explícitamente para él. Esto proporciona dos ventajas:

1. **Seguridad.** Un dispositivo comprometido no puede hacer DMA a memoria arbitraria; solo puede acceder a la memoria que el kernel ha mapeado explícitamente.
2. **Flexibilidad.** El kernel puede presentar a un dispositivo con capacidad de 32 bits una vista de 32 bits de cualquier región de memoria, incluidas las regiones por encima de 4 GB, configurando un mapeo adecuado. La estrategia de bounce buffer deja de ser necesaria cuando el IOMMU puede simplemente reasignar la dirección.

### Cómo se integra el kernel

El `busdma_iommu` de FreeBSD (compilado cuando se activa `DEV_IOMMU`, habitualmente en x86 con VT-d o AMD-Vi) se integra con la API `bus_dma` de forma transparente. Cuando un driver llama a `bus_dmamap_load`, la capa busdma pide al IOMMU que asigne espacio de direcciones IOVA (I/O virtual address, dirección virtual de E/S) y programe el mapeo; el `bus_addr_t` que se devuelve al driver es la IOVA, no la dirección física. El driver programa el dispositivo con la IOVA; el dispositivo emite un DMA hacia la IOVA; el IOMMU traduce a la dirección física; el controlador de memoria completa la transferencia.

Desde la perspectiva del driver, nada cambia. Las mismas llamadas `bus_dma` hacen lo correcto tanto en sistemas con IOMMU como en sistemas sin él. Esta es la promesa de portabilidad de `bus_dma`: los drivers escritos contra la API funcionan independientemente de lo que ofrezca la plataforma.

### Detectar el IOMMU

```sh
sysctl hw.vmm.ppt.devices  # shows pass-through devices (bhyve)
sysctl hw.dmar             # Intel VT-d if enabled
sysctl hw.iommu            # generic IOMMU flag
```

En un sistema donde el kernel arrancó con el IOMMU activo, `dmesg` muestra una línea similar a `dmar0: <DMAR>` cerca del inicio.

### Implicaciones de rendimiento

La traducción del IOMMU no es gratuita. Cada traducción pasa por las tablas de páginas del IOMMU; el IOMMU almacena en caché las traducciones usadas recientemente en una estructura similar al TLB, pero los fallos tienen un coste real. Para drivers que realizan millones de pequeños mapeos por segundo, el IOMMU puede convertirse en un cuello de botella.

Las mitigaciones son:

1. **Agrupar mapeos.** Un driver que mantiene un buffer de larga duración mapeado una sola vez no tiene coste recurrente de IOMMU. El patrón estático del capítulo 21 es ideal.
2. **Usar el soporte de superpáginas del IOMMU.** Las páginas más grandes implican menos entradas en el TLB.
3. **Deshabilitar el IOMMU para dispositivos de confianza.** No se recomienda en general, pero es posible en despliegues específicos.

El driver del capítulo 21 no se ve afectado en ningún caso; el buffer estático se mapea una vez durante el attach y nunca se vuelve a mapear. Los drivers con tasas de reasignación elevadas (controladores de almacenamiento que procesan muchas transferencias pequeñas por segundo) se ven más afectados.



## Un análisis más detallado de BUS_DMA_COHERENT frente a la sincronización explícita

La sección 4 trató el indicador `BUS_DMA_COHERENT` a nivel de API. Este análisis más detallado explica la interacción con la disciplina de sincronización, que resulta sutil en algunas plataformas.

### La relación

`BUS_DMA_COHERENT` es una sugerencia que se pasa a `bus_dma_tag_create` (preferencia arquitectónica) y a `bus_dmamem_alloc` (modo de asignación). Le pide al asignador que coloque el buffer en un dominio de memoria coherente con la ruta DMA: en arm64 con regiones de escritura combinada o sin caché, esto corresponde a un tipo de memoria específico; en amd64, el asignador elige memoria normal porque el complejo raíz PCIe ya es coherente.

El indicador no elimina el requisito de llamar a `bus_dmamap_sync`. El contrato de la API exige la sincronización; `BUS_DMA_COHERENT` hace que la sincronización sea barata o gratuita, no innecesaria.

### Por qué la sincronización sigue siendo necesaria

Por dos razones. En primer lugar, el driver es portable: el mismo archivo fuente se compila en todas las plataformas que soporta FreeBSD. En una plataforma donde `BUS_DMA_COHERENT` se respeta pero hay bounce buffers en juego (por una restricción de direccionamiento, por ejemplo), la sincronización es el gancho que activa la copia de rebote. Sin ella, los datos del rebote quedan obsoletos.

En segundo lugar, el driver es compatible hacia adelante: una plataforma futura puede no respetar el indicador, o puede respetarlo solo para ciertos asignadores. La sincronización explícita es lo que garantiza que el driver sea correcto en cualquier plataforma futura.

La regla es: pasa siempre `BUS_DMA_COHERENT` cuando el patrón de acceso lo justifique, y llama siempre a `bus_dmamap_sync` independientemente. El indicador hace barata la sincronización; la sincronización hace correcto al driver.

### Cuándo usar el indicador

Para buffers estáticos (anillos de descriptores, estructuras de control): pasa siempre `BUS_DMA_COHERENT`. El buffer lo acceden con frecuencia tanto la CPU como el dispositivo; la memoria coherente reduce el coste por acceso.

Para buffers dinámicos (cargas útiles de paquetes, bloques de disco): normalmente no lo pases. La tasa de aciertos de caché cuando la CPU procesa los datos suele ser lo bastante alta como para que los patrones de acceso más lentos de la memoria coherente perjudiquen más de lo que ayudan.

El driver `if_re` sigue exactamente este criterio: los tags de descriptores usan `BUS_DMA_COHERENT`, los tags de carga útil no. El tag por cola de NVMe usa `BUS_DMA_COHERENT` por la misma razón.

## Un análisis más profundo de la herencia entre etiquetas padre e hijo

La función de configuración de la sección 3 pasó `bus_get_dma_tag(sc->dev)` como etiqueta padre. Este análisis más profundo explica por qué la herencia importa y cuándo un driver debería crear una etiqueta padre explícita en lugar de heredar del bridge.

### Semántica de herencia

Una etiqueta hija hereda de su padre cada restricción que sea más estricta que la suya propia. La semántica de intersección significa que:

- El `lowaddr` de la hija es `min(parent_lowaddr, child_lowaddr)`. Gana el límite más restrictivo.
- La alineación de la hija es `max(parent_alignment, child_alignment)`. Gana la mayor alineación.
- El `maxsize` de la hija es `min(parent_maxsize, child_maxsize)`. Gana el valor más pequeño.
- El `nsegments` de la hija es `min(parent_nsegments, child_nsegments)`.
- Los flags se componen: `BUS_DMA_COHERENT` se propaga, `BUS_DMA_ALLOCNOW` no.

El kernel aplica estas reglas en el momento de crear la etiqueta; las restricciones internas de la hija son la intersección de las de su padre y las propias.

### Por qué los drivers crean etiquetas padre explícitas

Un driver que tiene varias etiquetas por subsistema a menudo encuentra más limpio crear una etiqueta padre explícita que lleve las restricciones del nivel de dispositivo (límites de direccionamiento, alineación a nivel de plataforma) y, a partir de ella, crear etiquetas hijas para cada propósito específico (rings, payloads). La etiqueta padre se destruye en el detach una vez que todas las hijas han sido destruidas.

`if_re` hace exactamente esto: `rl_parent_tag` es la etiqueta padre del nivel de dispositivo, que hereda del bridge PCI y añade el límite de 32 bits del dispositivo (para los chips que no son PCIe). Las cuatro etiquetas hijas (`rl_tx_mtag`, `rl_rx_mtag`, `rl_tx_list_tag`, `rl_rx_list_tag`) heredan de `rl_parent_tag`. Destruir `rl_parent_tag` es el último paso de la limpieza DMA, porque las cuatro hijas deben destruirse antes (`bus_dma_tag_destroy` devuelve `EBUSY` si alguna hija sigue existiendo).

### Cuándo omitir la etiqueta padre explícita

Para drivers con una sola etiqueta (como el del capítulo 21), crear una etiqueta padre explícita es excesivo. La única etiqueta del driver hereda de `bus_get_dma_tag(sc->dev)` directamente y aplica sus propias restricciones; la etiqueta del bridge actúa como padre de facto.

Para drivers con dos o tres etiquetas relacionadas que comparten restricciones, un padre explícito reduce la duplicación y hace el diseño más claro.

Para drivers con muchas etiquetas (descriptor rings, varios pools de buffers, regiones de memoria compartida), un padre explícito es siempre la elección correcta.



## Un análisis más profundo de los descriptor rings como tema futuro

El driver del capítulo 21 utiliza un único buffer DMA. Los drivers de producción utilizan descriptor rings. Este análisis más profundo anticipa qué es un descriptor ring, para que el lector que quiera ampliar el trabajo del capítulo tenga una forma a la que apuntar, pero la enseñanza detallada queda aplazada.

Un descriptor ring es un array de entradas de tamaño fijo en memoria DMA-coherente. Cada entrada contiene al menos una dirección de bus y una longitud, además de flags que describen la transferencia (dirección, tipo, estado). El driver y el dispositivo se comunican a través del ring: el driver escribe las entradas, el dispositivo las lee, realiza la transferencia y escribe el estado de vuelta.

El ring tiene dos índices: un índice de **productor** (la siguiente entrada que llenará el escritor) y un índice de **consumidor** (la siguiente entrada que el consumidor procesará). Para un ring de transmisión, el driver es el productor y el dispositivo es el consumidor. Para un ring de recepción, los roles se invierten. Ambos índices son circulares y avanzan en módulo del tamaño del ring.

El driver señala las nuevas entradas escribiendo en un registro de tipo **doorbell** (una escritura MMIO que el dispositivo interpreta como «mira el ring»). El dispositivo señala las completaciones generando una interrupción; el driver recorre entonces el ring desde el último índice consumidor hasta el actual, procesa cada entrada y avanza el índice consumidor.

Las complicaciones que hacen de los rings un tema propio son: el bloqueo de cabecera de línea cuando el ring está lleno, el manejo de transferencias parciales a través de las entradas del ring, la tolerancia al reordenamiento (¿completa el dispositivo las entradas en orden o fuera de orden?), el control de flujo entre el driver y el dispositivo, la corrección en los desbordamientos circulares y la interacción con el hardware de múltiples colas en el que cada cola tiene su propio ring.

Ampliar el driver del capítulo 21 para usar un ring pequeño es un ejercicio natural. Los ejercicios de desafío al final del capítulo incluyen un boceto. El tema completo de los descriptor rings es el capítulo de red de la parte 6 (capítulo 28).



## Un modelo mental para la disciplina de sincronización del capítulo 21

Un modelo mental de un párrafo que captura la disciplina de sincronización en una frase: *las llamadas de sincronización PRE y POST marcan cada momento en que un buffer DMA cambia de propietario entre la CPU y el dispositivo, y el driver las escribe en su código incluso cuando la plataforma actual las convierte en operaciones sin coste.* El driver del capítulo 21 respeta este modelo en todo momento. Cada llamada a `bus_dmamap_sync` en `myfirst_dma.c` está emparejada con su opuesta: PRE marca que la CPU cede la propiedad, POST marca que la CPU la recupera. Entre ambas, el buffer pertenece al dispositivo, y ningún código del driver lo lee ni lo escribe. La disciplina parece absoluta porque lo es; todos los drivers de producción la siguen; cualquier excepción que el lector pueda imaginar es en realidad un driver con errores que por casualidad funciona en la plataforma de pruebas.



## Patrones de drivers FreeBSD reales

Un breve catálogo de patrones DMA tal como aparecen en el árbol real. Cada patrón tiene un archivo representativo; leer las funciones relacionadas con DMA de ese archivo después de este capítulo es la práctica de seguimiento recomendada.

### Patrón: `bus_dmamem_alloc` para descriptor rings estáticos

**Dónde:** `/usr/src/sys/dev/re/if_re.c`, `re_allocmem`; `/usr/src/sys/dev/nvme/nvme_qpair.c`, `nvme_qpair_construct`.

El driver asigna una región contigua en el momento del attach, la carga con un callback de segmento único, la usa durante toda la vida del driver y la descarga en el detach. Este es el patrón estático del capítulo 21 y el fundamento de casi todos los drivers con capacidad DMA.

### Patrón: `bus_dmamap_load_mbuf_sg` para mapeos por paquete

**Dónde:** `/usr/src/sys/dev/re/if_re.c`, `re_encap`; `/usr/src/sys/dev/e1000/if_em.c`, `em_xmit`.

Para cada paquete saliente, el driver carga el mbuf del paquete en un mapa dinámico, programa el descriptor, transmite y descarga. Este es el patrón dinámico que el capítulo 21 menciona pero no implementa.

### Patrón: jerarquía de etiquetas padre-hijo

**Dónde:** `/usr/src/sys/dev/re/if_re.c`, `re_allocmem`; `/usr/src/sys/dev/bce/if_bce.c`, `bce_dma_alloc`.

El driver crea una etiqueta padre con las restricciones del nivel de dispositivo y etiquetas hijas para las asignaciones por subsistema. La jerarquía compone las restricciones automáticamente y separa la limpieza del driver en pasos claros.

### Patrón: `BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE` para rings compartidos

**Dónde:** `/usr/src/sys/dev/nvme/nvme_qpair.c`, alrededor de `nvme_qpair_reset` y la cola de envío; `/usr/src/sys/dev/ahci/ahci.c`, alrededor de la tabla de comandos.

Para los rings en los que tanto el driver como el dispositivo leen y escriben, el flag PRE combinado en la configuración y el flag POST combinado en los límites de transacción hacen explícita la disciplina de sincronización.

### Patrón: callback de segmento único como helper universal

**Dónde:** `/usr/src/sys/dev/nvme/nvme_private.h`, `nvme_single_map`; `/usr/src/sys/dev/re/if_re.c`, `re_dma_map_addr`; y muchos otros.

Casi todos los drivers definen un callback de segmento único con un nombre propio de su estilo. El `myfirst_dma_single_map` del capítulo 21 es un ejemplo limpio; las variantes en los drivers reales son todas funcionalmente idénticas.

### Patrón: etiqueta coherente para rings, no coherente para payloads

**Dónde:** `/usr/src/sys/dev/re/if_re.c` y muchos otros.

El driver pasa `BUS_DMA_COHERENT` para las etiquetas de los rings de descriptores y lo omite para las etiquetas de payload. El asignador gestiona ambos casos; el driver toma la decisión correcta por etiqueta.



## Laboratorios prácticos

Cinco laboratorios prácticos guían al lector por las etapas del capítulo en un sistema de laboratorio. Cada laboratorio es independiente; completar los cinco produce un driver `1.4-dma` en funcionamiento.

### Laboratorio 1: Identificar dispositivos con capacidad DMA en tu sistema

**Objetivo:** construir un mapa del entorno DMA de tu máquina de laboratorio antes de comenzar el trabajo de programación del capítulo.

**Tiempo:** 20 minutos.

**Pasos:**

1. Ejecuta `pciconf -lv` en tu host de laboratorio y anota los dispositivos.
2. Ejecuta `pciconf -lvc | grep -B1 BUSMASTEREN` para identificar los dispositivos con bus mastering habilitado.
3. Elige un dispositivo, ejecuta `pciconf -lvbc <devname>` e identifica su distribución de BAR y su lista de capacidades.
4. Ejecuta `sysctl hw.busdma` y anota los contadores del bounce buffer.
5. Escribe en tu cuaderno de laboratorio: tres dispositivos, una propiedad de cada uno y tu uso actual de bounce buffer.

**Resultado esperado:** una breve lista de dispositivos con su estado de bus mastering y una comprensión de si tu sistema está usando bounce.

### Laboratorio 2: Etapa 1, asignar y mapear un buffer DMA

**Objetivo:** construir el driver de la etapa 1 que crea una etiqueta, asigna un buffer, carga un mapa y realiza la limpieza.

**Tiempo:** 1,5 horas.

**Pasos:**

1. Parte del driver de la etapa 4 del capítulo 20 (`1.3-msi`).
2. Añade los cuatro campos en softc (`dma_tag`, `dma_map`, `dma_vaddr`, `dma_bus_addr`).
3. Añade el callback de segmento único (`myfirst_dma_single_map`).
4. Añade `myfirst_dma_setup` y `myfirst_dma_teardown` a `myfirst.c`.
5. Llama a estas funciones desde attach y detach.
6. Compila y carga. Verifica que el banner en `dmesg` muestra la KVA y la dirección de bus.
7. Descarga y recarga 50 veces; comprueba que `vmstat -m` no muestra fugas.

**Resultado esperado:** un driver que levanta y desmonta una región DMA de forma limpia. `dmesg` muestra el banner en cada carga; sin pánico al descargar.

**Pista:** si la carga falla inmediatamente con `EINVAL`, comprueba que `MYFIRST_DMA_BUFFER_SIZE` es tanto el `maxsize` de la etiqueta como el argumento de `bus_dmamap_load`; una discrepancia es la causa más frecuente.

### Laboratorio 3: Etapa 2, motor DMA simulado con polling

**Objetivo:** ampliar el driver con el motor DMA simulado y el helper de transferencia basado en polling.

**Tiempo:** 2 horas.

**Pasos:**

1. Añade las constantes `MYFIRST_REG_DMA_*` a `myfirst.h`.
2. Amplía `struct myfirst_sim` con el estado del motor DMA.
3. Implementa el manejador del callout del motor y el hook de escritura.
4. Implementa `myfirst_dma_do_transfer` (versión de polling).
5. Añade los sysctl `dma_test_write` y `dma_test_read`.
6. Compila y carga.
7. Ejecuta `sudo sysctl dev.myfirst.0.dma_test_write=0xAA` y verifica la salida en `dmesg`.
8. Ejecuta la prueba de 1000 iteraciones de la sección 5 y comprueba los contadores.

**Resultado esperado:** 1000 transferencias exitosas en cada dirección; cero errores; cero timeouts.

**Pista:** si la transferencia agota el tiempo en cada intento, probablemente el callout de la simulación no se está programando. Comprueba que se llama a `callout_reset` y que `hz / 100` coincide con el `hz` de tu kernel (usa `sysctl kern.hz` para verificarlo).

### Laboratorio 4: Etapa 3, completación por interrupciones

**Objetivo:** sustituir el helper de polling por el helper basado en interrupciones.

**Tiempo:** 2 horas.

**Pasos:**

1. Amplía el filtro rx para gestionar `MYFIRST_INTR_COMPLETE`.
2. Amplía la tarea rx para llamar a `myfirst_dma_handle_complete`.
3. Añade `myfirst_dma_do_transfer_intr` a `myfirst.c`.
4. Habilita `MYFIRST_INTR_COMPLETE` en la escritura de `INTR_MASK`.
5. Reconecta los manejadores de sysctl para usar la versión basada en interrupciones.
6. Compila y carga.
7. Ejecuta la prueba de 1000 iteraciones. Verifica que `dma_complete_intrs` y `dma_complete_tasks` suben juntos.
8. Verifica que ninguna CPU está saturada durante la prueba (`top` o `vmstat` deben mostrar una CPU de sistema baja).

**Resultado esperado:** 1000 interrupciones, 1000 tareas, 1000 completaciones. Sin CPU saturada.

**Pista:** si el contador de completaciones se incrementa pero el helper se queda bloqueado en `cv_timedwait` hasta el timeout de 1 segundo, la tarea no está llamando a `cv_broadcast` en `dma_cv`. El desbloqueo es lo que libera el helper.

### Laboratorio 5: Etapa 4, refactorización y regresión

**Objetivo:** completar la refactorización en `myfirst_dma.c` y `myfirst_dma.h`; ejecutar la prueba de regresión final.

**Tiempo:** 1,5 horas.

**Pasos:**

1. Crea `myfirst_dma.h` con la API pública.
2. Crea `myfirst_dma.c` con el código DMA extraído de `myfirst.c`.
3. Actualiza el Makefile para incluir `myfirst_dma.c` y sube la versión a `1.4-dma`.
4. Crea `DMA.md` con la referencia del subsistema.
5. Ejecuta `make clean && make` y confirma que la compilación termina sin errores.
6. Carga el driver y verifica la cadena de versión.
7. Ejecuta el script de regresión completo (`labs/full_regression_ch21.sh`) y confirma que imprime `PASS`.

**Resultado esperado:** compilación limpia, carga limpia, regresión completada con éxito, descarga limpia.

**Pista:** si el enlazado falla con un símbolo indefinido que hace referencia a una de las funciones DMA, la línea `SRCS` del Makefile no se ha actualizado para incluir `myfirst_dma.c`. Ejecuta `make clean` después de modificar el Makefile para asegurarte de que el nuevo archivo fuente se compila correctamente.

## Ejercicios de desafío

Ejercicios de desafío que amplían el alcance del Capítulo 21 sin introducir fundamentos de capítulos posteriores. Elige los que te resulten más interesantes; la lista es un menú, no una lista de tareas obligatorias.

### Desafío 1: Rotación con múltiples buffers

Extiende el driver para mantener un pool de cuatro buffers DMA en lugar de uno. Cada transferencia toma el siguiente buffer en rotación. El programa de prueba lanza cuatro transferencias consecutivas sin esperar a que finalicen y observa que las cuatro se completan antes de enviar la quinta.

**Qué ejercita:** las rutas de asignación y carga en un bucle; el estado por buffer en el softc; la interacción con un único vector de finalización cuando hay múltiples transferencias en vuelo.

### Desafío 2: Carga scatter-gather

Extiende el driver para aceptar una lista scatter-gather de hasta tres segmentos por transferencia. Usa `bus_dmamap_load` con un callback multi-segmento; almacena la lista de segmentos en el softc; programa el motor simulado con tres pares (dirección, longitud).

**Qué ejercita:** el callback de segmentos no singulares; el parámetro `nsegments` del tag; la capacidad del motor para gestionar múltiples segmentos.

### Desafío 3: Esquema de anillo de descriptores

Extiende el driver con un pequeño anillo de descriptores (ocho entradas). Cada entrada es un struct con una dirección de bus, una longitud, una dirección de transferencia y un estado. El driver rellena las entradas, escribe el doorbell de cabecera del anillo, y el motor simulado recorre el anillo y actualiza el estado.

**Qué ejercita:** el patrón productor-consumidor completo con anillo; el flag de sincronización combinado para anillos bidireccionales; el protocolo de doorbell de cabecera/cola.

### Desafío 4: Notificación de transferencia parcial

Modifica el motor simulado para que, de forma ocasional (por ejemplo, cada quinta transferencia), notifique una transferencia parcial. Modifica el driver para detectar el resultado parcial, registrarlo en el log y reintentar el resto.

**Qué ejercita:** el patrón de transferencia parcial de la Sección 7; la lógica de reintento; el seguimiento de estado por transferencia.

### Desafío 5: Observabilidad del IOMMU

En un sistema con el IOMMU activo, añade sysctls que expongan la diferencia entre la dirección de bus del driver y la dirección física subyacente. Usa `pmap_kextract` para obtener la dirección física del KVA del buffer y compárala con `sc->dma_bus_addr`.

**Qué ejercita:** la comprensión del remapeo transparente del IOMMU; la observabilidad de la abstracción.

### Desafío 6: Observabilidad del bounce buffer

Crea un tag con `lowaddr = BUS_SPACE_MAXADDR_32BIT` para forzar el direccionamiento exclusivo a 32 bits. Reserva buffers por encima de 4 GB (mediante `contigmalloc` con los flags adecuados) y observa que `bus_dmamap_load` activa el bouncing. Expón contadores de `total_bounced` desde la perspectiva del driver.

**Qué ejercita:** la comprensión de cuándo ocurre el bouncing; la observabilidad de la ruta de bounce.

### Desafío 7: Refactorización con plantilla de tag

Reescribe la función de configuración de la Fase 4 usando la API `bus_dma_template_*` en lugar de `bus_dma_tag_create`. El comportamiento debe ser idéntico; solo cambia la sintaxis.

**Qué ejercita:** la API de plantillas moderna; la equivalencia entre los dos estilos de creación de tags.

### Desafío 8: Perfilado de transferencias con DTrace

Escribe un script de DTrace que mida la latencia de las transferencias DMA desde la perspectiva del helper. Engancha la entrada y la salida de `myfirst_dma_do_transfer_intr`; imprime un histograma de tiempos transcurridos; compara con el retardo de un milisegundo del motor.

**Qué ejercita:** las sondas FBT de DTrace; la observabilidad de los tiempos DMA; la comprensión del desglose de costes.



## Resolución de problemas y errores frecuentes

Una guía de referencia sobre los problemas más habituales y sus soluciones. Cada entrada indica el síntoma, la causa probable y la corrección.

### "bus_dma_tag_create falla con EINVAL"

**Síntoma:** `device_printf` informa de `bus_dma_tag_create failed: 22`.

**Causa probable:** los parámetros del tag son inconsistentes. El valor de `alignment` debe ser una potencia de dos; `boundary` debe ser una potencia de dos y al menos tan grande como `maxsegsz`; `maxsize` debe ser al menos igual a `alignment`.

**Corrección:** comprueba los parámetros frente a las descripciones de restricciones de la página de manual `bus_dma(9)`. Errores frecuentes: un alignment de 3 (no es potencia de dos); un boundary menor que maxsegsz.

### "El callback de bus_dmamap_load se ejecuta con error != 0"

**Síntoma:** el argumento `error` del callback de segmento único es distinto de cero; `dma_bus_addr` vale cero.

**Causa probable:** el buffer es mayor que el `maxsize` del tag, o el mapeo no puede satisfacer las restricciones de segmento del tag (`EFBIG`).

**Corrección:** comprueba que el `buflen` de la carga coincide con el `maxsize` del tag; comprueba que el buffer es contiguo si el tag permite un único segmento.

### "dma_bus_addr es cero después de que bus_dmamap_load retornó cero"

**Síntoma:** la carga devuelve éxito, pero la dirección de bus no se ha rellenado.

**Causa probable:** el callback no fue invocado (poco probable, pero posible si el estado interno del kernel es inconsistente), o el callback no escribió en `*addr` debido a un error interno que silenció.

**Corrección:** revisa el callback en busca de retornos tempranos que no populan la salida. El patrón del Capítulo 21 es poner `dma_bus_addr` a cero antes de la carga y comprobar si sigue siendo cero después.

### "Las transferencias tienen éxito, pero el contenido del buffer es incorrecto"

**Síntoma:** la transferencia finaliza con éxito según los contadores, pero los datos en el buffer no son los que envió el emisor.

**Causa probable:** una llamada a `bus_dmamap_sync` ausente o con el tipo incorrecto. En una plataforma coherente el error puede ser invisible; en una plataforma no coherente o con bounce buffers activos es visible.

**Corrección:** verifica que cada transferencia tiene exactamente un sync PRE y un sync POST que coinciden con la dirección de la transferencia. `PREWRITE`/`POSTWRITE` para host a dispositivo; `PREREAD`/`POSTREAD` para dispositivo a host.

### "bus_dmamap_unload genera un panic con 'map not loaded'"

**Síntoma:** `kldunload` genera un panic (o una advertencia de `WITNESS`) en `bus_dmamap_unload`.

**Causa probable:** el path de teardown del driver ejecuta `bus_dmamap_unload` dos veces, o lo ejecuta cuando el mapa nunca fue cargado.

**Corrección:** protege la llamada con `if (sc->dma_bus_addr != 0)` y pon el campo a cero después del unload. El patrón de teardown del Capítulo 21 es correcto; verifica que el teardown de tu driver coincide.

### "bus_dmamem_free genera un panic con 'no allocation'"

**Síntoma:** `kldunload` genera un panic en `bus_dmamem_free`.

**Causa probable:** `dma_vaddr` tiene un valor obsoleto (fue liberado pero no puesto a cero), y el teardown se ejecuta de nuevo en un segundo path de descarga.

**Corrección:** pon `dma_vaddr = NULL` inmediatamente después de `bus_dmamem_free`. El patrón de teardown del Capítulo 21 es correcto; verifícalo.

### "dma_complete_intrs se queda en cero aunque las transferencias tienen éxito"

**Síntoma:** el helper de transferencia devuelve éxito (por la ruta de polling), pero el contador de interrupciones de finalización se queda en cero.

**Causa probable:** `MYFIRST_INTR_COMPLETE` no está habilitado en la escritura de `INTR_MASK`, o el filtro no comprueba ese bit.

**Corrección:** verifica que la escritura de la máscara habilita `MYFIRST_INTR_COMPLETE`; verifica que el filtro tiene el segundo `if` para el bit de finalización. Los cambios de la Fase 3 incluyen ambos.

### "cv_timedwait siempre devuelve EWOULDBLOCK"

**Síntoma:** cada transferencia con interrupciones agota el tiempo de espera de un segundo.

**Causa probable:** la tarea nunca llama a `cv_broadcast(&sc->dma_cv)`, o la tarea no se ejecuta porque el filtro no la encoló.

**Corrección:** verifica que la tarea llama a `cv_broadcast(&sc->dma_cv)` en `myfirst_dma_handle_complete`. Verifica que el filtro llama a `taskqueue_enqueue` tras detectar el bit de finalización.

### "El driver se cuelga en kldunload"

**Síntoma:** `kldunload myfirst` se bloquea indefinidamente.

**Causa probable:** hay una transferencia en vuelo y el helper está esperando en `dma_cv`; el path de detach espera a que `dma_in_flight` se limpie; pero el dispositivo no está finalizando.

**Corrección:** el path de detach debe llamar a `callout_drain` antes de esperar a que `dma_in_flight` se limpie. Si el callout no ha sido drenado, la simulación puede completar la transferencia y el detach avanza. Verifica el orden: poner `detaching = true`, drenar el callout, esperar a que el vuelo en curso se limpie, realizar el teardown.

### "WITNESS advierte sobre inversión del orden de locks"

**Síntoma:** `dmesg` muestra una advertencia de `WITNESS` relacionada con `sc->mtx`, `dma_cv` o el lock del taskqueue.

**Causa probable:** el helper mantiene `sc->mtx` mientras llama a `cv_timedwait`, lo cual es correcto; pero puede activarse una aserción si otro path también mantiene un lock en conflicto.

**Corrección:** revisa el orden de locks. La disciplina de los Capítulos 11, 19 y 20 es: `sc->mtx` antes de `dma_cv`; ningún lock del taskqueue mantenido por el driver; ninguna espera en `dma_cv` mientras se mantiene un lock atómico. Los patrones del Capítulo 21 son correctos; la advertencia probablemente indica un problema local del driver.

### "Los contadores aumentan pero el buffer no se rellena"

**Síntoma:** el contador `dma_transfers_read` aumenta, pero `sc->dma_vaddr` contiene ceros después de la transferencia.

**Causa probable:** la función `dma_scratch` de la simulación no está copiando el patrón en el buffer del host, o el sync POST se omite.

**Corrección:** verifica que `myfirst_sim_dma_copy_to_host` del motor simulado se llama con la longitud correcta; verifica que el sync POST se ejecuta. Si la simulación usa un mapeo simplificado (dirección de bus == KVA), verifica que el KVA del host corresponde realmente al buffer reservado por el tag.

### "El ciclo de carga y descarga pierde memoria"

**Síntoma:** tras 50 ciclos de carga y descarga, `vmstat -m` muestra `bus_dmamap` o `bus_dma_tag` con contadores distintos de cero.

**Causa probable:** un path de fallo en la configuración no destruyó el tag ni liberó la memoria antes de retornar.

**Corrección:** revisa cada retorno de fallo en `myfirst_dma_setup`. El patrón del Capítulo 21 destruye el tag ante cualquier fallo posterior a su creación, libera la memoria ante cualquier fallo posterior a la asignación y descarga el mapa ante cualquier fallo posterior a la carga. Verifica que cada path de fallo deshace correctamente los pasos anteriores.

### "Las transferencias son lentas en comparación con lo esperado"

**Síntoma:** la prueba de 1000 transferencias tarda muchos segundos en lugar de milisegundos.

**Causa probable:** el `pause` del bucle de polling es demasiado grueso, o el path de interrupciones está mal configurado y cada transferencia alcanza el tiempo de espera de un segundo.

**Corrección:** si usas el helper de polling, reduce el intervalo de `pause`; si usas el helper con interrupciones, verifica que el bit de finalización llega al filtro (el `dma_complete_intrs` de la Fase 3 debe coincidir exactamente con el número de transferencias).



## Ejemplo resuelto: trazado de una transferencia DMA de extremo a extremo

Un recorrido detallado por una única transferencia DMA de host a dispositivo en el driver de la Fase 4 del Capítulo 21, anotado en cada paso con qué línea de código se ejecuta, qué está haciendo el kernel por debajo, qué comprobaría `WITNESS` y qué vería un operador en `dmesg` o en un contador. El objetivo es dar al lector una imagen mental de toda la cadena de procesamiento como una secuencia de eventos concretos.

### El estado inicial

El driver ha realizado el attach. `dma_tag`, `dma_map`, `dma_vaddr` y `dma_bus_addr` están populados. La máscara de interrupciones tiene `MYFIRST_INTR_COMPLETE` activado. La tarea del vector rx está inicializada. Todos los contadores están en cero. Un usuario se conecta y escribe:

```sh
sudo sysctl dev.myfirst.0.dma_test_write=0xAA
```

### Evento 1: el handler del sysctl se ejecuta

El framework de sysctl llama a `myfirst_dma_sysctl_test_write`. El handler analiza el valor (0xAA), rellena `sc->dma_vaddr` con bytes 0xAA y llama a `myfirst_dma_do_transfer_intr(sc, MYFIRST_DMA_DIR_WRITE, MYFIRST_DMA_BUFFER_SIZE)`.

Contexto: contexto de proceso, en el lado del kernel del binario `sysctl` del usuario. Aún no se mantiene ningún lock. `WITNESS` no tiene nada que comprobar.

### Evento 2: el helper toma el lock del softc

`myfirst_dma_do_transfer_intr` adquiere `sc->mtx` mediante `MYFIRST_LOCK(sc)`. `WITNESS` comprueba el orden de locks: el mutex del softc es un lock `MTX_DEF`, no se mantiene ningún lock de orden superior, la adquisición es válida.

El helper comprueba `sc->dma_in_flight`. Está en false (no hay ninguna transferencia en curso). El helper continúa.

### Evento 3: el sync PRE

`bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_PREWRITE)` se ejecuta. En amd64, esto es una barrera de memoria (`mfence`). En arm64 con `BUS_DMA_COHERENT` respetado, es una barrera de memoria de datos. En arm más antiguo sin DMA coherente, sería un vaciado de caché de las líneas del buffer.

El estado interno del kernel: el rastreador de mapas del tag marca el mapa como pendiente de PRE. Si hubiera bounce pages en uso (no las hay en la plataforma amd64 de prueba del Capítulo 21), los datos de bounce se copiarían en este momento.

### Evento 4: La programación del dispositivo

El helper escribe cuatro registros en secuencia: `DMA_ADDR_LOW`, `DMA_ADDR_HIGH`, `DMA_LEN`, `DMA_DIR`. Cada escritura pasa por `bus_write_4`, que despacha a través del accessor del Capítulo 16 hasta el hook de escritura de la simulación. La simulación registra los valores en `sim->dma`.

El helper escribe `DMA_CTRL = MYFIRST_DMA_CTRL_START`. La escritura dispara `myfirst_sim_dma_ctrl_written` en la simulación.

### Evento 5: La simulación arma el callout

Dentro de la simulación, `myfirst_sim_dma_ctrl_written` ve `MYFIRST_DMA_CTRL_START`, comprueba que no haya ninguna transferencia ya en curso, establece `status = RUNNING`, establece `armed = true` y llama a `callout_reset(&sim->dma.done_co, hz / 100, myfirst_sim_dma_done_co, sim)`.

El callout se programa para diez milisegundos en el futuro. La simulación retorna; el `CSR_WRITE_4` del helper retorna.

### Evento 6: El helper espera

El helper establece `sc->dma_last_direction = MYFIRST_DMA_DIR_WRITE`, establece `sc->dma_in_flight = true` y llama a `cv_timedwait(&sc->dma_cv, &sc->mtx, hz)`.

`cv_timedwait` libera `sc->mtx`, pone el thread a dormir en `dma_cv` y programa un despertar en `hz` (1 segundo) en el futuro. El thread queda fuera de la cola de ejecución.

Contexto: el planificador del kernel es libre de ejecutar otro trabajo. `WITNESS` comprueba que el thread se fue a dormir sin mantener ningún lock no durmiente; esto es correcto porque el mutex fue liberado.

### Evento 7: El callout se dispara

Diez milisegundos después, el subsistema de callouts ejecuta `myfirst_sim_dma_done_co` en un thread softclock. La función extrae la dirección de bus (`((sim->dma.addr_high << 32) | sim->dma.addr_low)`), confirma que `len <= scratch_size` y llama a `myfirst_sim_dma_copy_from_host` para copiar el patrón 0xAA desde el buffer del host al buffer de scratch.

La función establece `sim->dma.status = DONE` y llama a `myfirst_sim_dma_raise_complete`.

Contexto: thread softclock. Sin lock del driver retenido. `WITNESS` comprueba que el callback del callout no adquiere locks del driver de forma que conflicte con el `callout_mtx`; los datos de la simulación están protegidos por el estado por simulación o mediante actualizaciones atómicas.

### Evento 8: La simulación activa el bit de finalización

`myfirst_sim_dma_raise_complete` establece `MYFIRST_INTR_COMPLETE` en `sim->intr_status` y, dado que el bit está habilitado en `intr_mask`, llama a `myfirst_sim_raise_intr`. Este último encola el evento de interrupción simulada a través del camino del Capítulo 19/20.

### Evento 9: El filtro se ejecuta

La maquinaria de interrupciones del kernel despacha el vector rx. El filtro (`myfirst_msix_rx_filter`) se ejecuta en contexto de interrupción primaria. Lee `INTR_STATUS`, ve `MYFIRST_INTR_COMPLETE` activo, reconoce el bit, incrementa `sc->dma_complete_intrs` mediante `atomic_add_64` y encola la tarea rx.

El filtro retorna `FILTER_HANDLED`. El despacho de interrupciones del kernel finaliza.

Contexto: interrupción primaria. Sin sleep, sin malloc, sin lock por encima del nivel de spinlock. `WITNESS` comprueba que el incremento atómico no implica ningún lock.

### Evento 10: La tarea se ejecuta

La maquinaria del taskqueue programa la tarea rx en su thread. El thread adquiere `sc->mtx` mediante `MYFIRST_LOCK(sc)`. Lee `DATA_OUT` (del pipeline del Capítulo 19/20) y hace broadcast de `data_cv`. Luego llama a `myfirst_dma_handle_complete(sc)`.

### Evento 11: La sincronización POST

`myfirst_dma_handle_complete` comprueba que `sc->dma_in_flight == true`. Limpia el flag, verifica la dirección (`DIR_WRITE`), emite `bus_dmamap_sync(..., BUS_DMASYNC_POSTWRITE)`, lee `DMA_STATUS` (que muestra `DONE`), incrementa `dma_complete_tasks` y llama a `cv_broadcast(&sc->dma_cv)`.

La sincronización POST en amd64 es una barrera. En arm64 con memoria coherente, también es una barrera. En sistemas con bounce buffers, los datos del bounce se copiarían de vuelta en este momento (para un POSTREAD; POSTWRITE generalmente solo libera la página de bounce).

### Evento 12: La tarea libera el lock

El cuerpo de la tarea termina. Libera `sc->mtx` mediante `MYFIRST_UNLOCK(sc)`. La tarea retorna.

### Evento 13: El helper despierta

`cv_broadcast(&sc->dma_cv)` del Evento 11 volvió a poner el thread del helper en la cola de ejecución. El helper despierta dentro de `cv_timedwait`, readquiere `sc->mtx` y retorna de la espera con `err = 0` (no `EWOULDBLOCK`).

### Evento 14: El helper examina el estado

El helper lee `sc->dma_last_status`. Es `DONE` con `ERR = 0`. El helper incrementa `dma_transfers_write` mediante `atomic_add_64`. Libera `sc->mtx` y retorna 0.

### Evento 15: El manejador del sysctl retorna

`myfirst_dma_sysctl_test_write` recibe el retorno exitoso, imprime un mensaje (`dma_test_write: pattern 0xaa transferred`) y retorna 0 al framework del sysctl.

### Evento 16: El usuario ve el resultado

El comando `sysctl` del usuario retorna. Un `dmesg | tail` posterior muestra el mensaje. Los contadores muestran ahora:

```text
dma_transfers_write: 1
dma_complete_intrs: 1
dma_complete_tasks: 1
```

### El tiempo total

El tiempo de reloj en el camino crítico es aproximadamente: 10 ms del retardo del callout, más unos pocos microsegundos para el despacho del filtro, más unos pocos microsegundos para la tarea, más la latencia del cambio de contexto para el despertar del helper. Total: unos 10-11 ms.

Si el helper hubiese utilizado el camino de polling de la Etapa 2, el tiempo de reloj sería similar (el retardo del callout domina), pero el helper mantendría una CPU ocupada haciendo polling durante los diez milisegundos completos. La versión basada en interrupciones libera la CPU para hacer otro trabajo.

### La lección

Dieciséis eventos a lo largo de cinco contextos: proceso (helper), softclock (callout), interrupción primaria (filtro), thread del taskqueue (tarea), proceso (despertar del helper). Cada contexto tiene su propia disciplina de locking; las transiciones entre contextos se producen a través de la maquinaria de planificación e interrupciones del kernel, no a través del código del driver. El código del driver vive dentro de cada contexto; es el kernel quien mueve la ejecución entre ellos.

Un lector que pueda rastrear esta secuencia de principio a fin comprende el Capítulo 21. Un lector que no pueda hacerlo puede releer las Secciones 5 y 6 con la secuencia en mente; los pasos individuales se vuelven concretos cuando se mapean a la narrativa.



## Una mirada más profunda al locking y a DMA

El Capítulo 11 y el Capítulo 19 establecieron la disciplina de locking del driver. El Capítulo 21 la aplica al camino de DMA, que introduce algunos aspectos específicos que merece la pena destacar.

### El mutex del softc cubre el estado de DMA

El mutex del softc (`sc->mtx`) protege todos los campos de DMA que se leen o escriben fuera de attach/detach: `dma_in_flight`, `dma_last_direction`, `dma_last_status`, los contadores por transferencia. Todos los caminos de código que tocan estos campos adquieren `sc->mtx` primero.

Los contadores se actualizan con `atomic_add_64` para que el filtro (que no puede adquirir un mutex durmiente) pueda incrementarlos sin tomar el lock. Esos mismos contadores se leen con `atomic_load_64` (implícitamente a través de la copia del manejador del sysctl) cuando el lector quiere una instantánea consistente. Usar atómicas en lugar del mutex para los contadores es el patrón del Capítulo 11; es más rápido y seguro para el filtro.

La variable de condición `dma_cv` se espera y se notifica bajo `sc->mtx`. Esta es la disciplina estándar de `cv_*`.

### El lockfunc de la etiqueta

El parámetro `lockfunc` de `bus_dma_tag_create` es para las cargas diferidas. El Capítulo 21 pasa `NULL`, lo que sustituye `_busdma_dflt_lock`, que genera un panic si se llama. El panic es la red de seguridad: le indica al desarrollador que se produjo una carga diferida (lo que el uso de `BUS_DMA_NOWAIT` en el Capítulo 21 debería impedir), momento en el que el desarrollador añadiría un lockfunc apropiado.

Para los drivers que sí usan cargas diferidas, el patrón habitual es:

```c
err = bus_dma_tag_create(parent, align, bdry, lowaddr, highaddr,
    NULL, NULL, maxsize, nseg, maxsegsz, 0,
    busdma_lock_mutex, &sc->mtx, &sc->tag);
```

`busdma_lock_mutex` es una implementación proporcionada por el kernel que adquiere y libera el mutex pasado como último argumento. El kernel la llama antes y después del callback diferido.

### Sin locks en el camino de sincronización

`bus_dmamap_sync` no adquiere internamente ningún lock del driver. Es seguro llamarlo desde el contexto del filtro, de la tarea o del proceso, siempre que el llamador mantenga la disciplina de concurrencia adecuada para el mapa. El Capítulo 21 llama a sync desde el helper (con `sc->mtx` retenido) y desde la tarea (con `sc->mtx` retenido, dentro de `myfirst_dma_handle_complete`). Ambos son seguros porque ningún thread fuera del ámbito del lock puede hacer sync concurrentemente sobre el mismo mapa.

### La condición de carrera en detach

La condición de carrera en el camino de detach se da entre "la transferencia está finalizando" y "el driver está siendo descargado". El patrón del Capítulo 21 usa el flag `detaching` más la espera en `dma_in_flight` para garantizar que detach no pueda desmontar recursos mientras una transferencia está en curso. El `callout_drain` y el `taskqueue_drain` aseguran que no quede ningún callback pendiente.

Este patrón es la disciplina de detach del Capítulo 11 generalizada a un tipo más de trabajo en vuelo (las transferencias DMA). El patrón se compone: las interrupciones se drenaron en el Capítulo 19, las tareas por vector en el Capítulo 20, las transferencias DMA en el Capítulo 21. El Capítulo 22 añadirá uno más (los callbacks de transición de energía).



## Una mirada más profunda a la observabilidad en los drivers de DMA

El driver del Capítulo 21 expone varios puntos de observabilidad. Esta mirada más detallada cubre qué proporciona cada uno y cómo los usaría un operador.

### Los contadores por transferencia

```text
dev.myfirst.N.dma_transfers_write
dev.myfirst.N.dma_transfers_read
dev.myfirst.N.dma_errors
dev.myfirst.N.dma_timeouts
dev.myfirst.N.dma_complete_intrs
dev.myfirst.N.dma_complete_tasks
```

Seis contadores, cada uno un atómico de 64 bits. Los invariantes:

- `dma_complete_intrs == dma_complete_tasks`: cada finalización observada por el filtro produjo una invocación de la tarea.
- `dma_complete_intrs == dma_transfers_write + dma_transfers_read + dma_errors + dma_timeouts`: cada interrupción de finalización produjo eventualmente un resultado de transferencia.

La violación de cualquiera de estos invariantes indica un bug. El test de regresión comprueba ambos.

### El buffer circular de transferencias recientes

Un ejercicio de desafío añadiría un pequeño buffer circular de transferencias recientes, cada una con la dirección, la longitud, el resultado y el tiempo. Un sysctl de solo lectura expone las últimas N transferencias como una cadena formateada por el kernel. Esto es útil para la depuración post-mortem (cuando el driver se ha colgado, el buffer circular muestra qué estaba ocurriendo justo antes).

### Sondas DTrace

El driver no tiene sondas DTrace explícitas, pero FBT (function boundary tracing) funciona automáticamente. Algunos one-liners útiles:

```console
# Count DMA transfer calls
dtrace -n 'fbt::myfirst_dma_do_transfer_intr:entry { @[probefunc] = count(); }'

# Histogram of transfer durations
dtrace -n 'fbt::myfirst_dma_do_transfer_intr:entry { self->start = vtimestamp; } fbt::myfirst_dma_do_transfer_intr:return /self->start/ { @[probefunc] = quantize(vtimestamp - self->start); self->start = 0; }'

# Interrupt rate per vector
dtrace -n 'fbt::myfirst_msix_rx_filter:entry { @ = count(); } tick-1s { printa(@); clear(@); }'
```

DTrace es la mejor herramienta para entender el comportamiento del driver bajo carga. Las sondas se ejecutan con una sobrecarga baja (decenas de nanosegundos por activación en amd64) y pueden habilitarse en un sistema en producción.

### `vmstat -i` y `systat -vmstat`

`vmstat -i` muestra el recuento de interrupciones por vector. Para el driver del Capítulo 21 con MSI-X, aparecen tres vectores con sus etiquetas de `bus_describe_intr` (`admin`, `rx`, `tx`). El recuento de la línea `rx` debería ser igual a `dma_complete_intrs` más `data_av_count` (la actividad combinada en ese vector).

`systat -vmstat` ofrece una vista en tiempo real. Ejecutarlo en un segundo terminal mientras corre el test muestra cómo cambia la tasa del vector en tiempo real.

### `procstat -kke`

`procstat -kke` muestra la pila del kernel de cada thread. El thread de la tarea rx, cuando ejecuta el manejador de DMA, tiene una pila que contiene `myfirst_dma_handle_complete` y `bus_dmamap_sync`. Observar la pila confirma que el thread está en el lugar esperado; una pila inesperada sugiere un cuelgue o un deadlock.

### `sysctl hw.busdma`

El árbol sysctl `hw.busdma` expone contadores a nivel del subsistema DMA:

```text
hw.busdma.total_bpages
hw.busdma.free_bpages
hw.busdma.reserved_bpages
hw.busdma.active_bpages
hw.busdma.total_bounced
hw.busdma.total_deferred
```

`total_bounced` es el más útil a los efectos del Capítulo 21: en un sistema donde ningún driver debería estar haciendo bounce, este valor permanece en cero. Un valor que sube indica que las restricciones de etiqueta de algún driver están forzando el bounce; esto suele ser un problema de configuración.



## Una mirada más profunda al rendimiento de las transferencias DMA

La simulación del Capítulo 21 ejecuta cada transferencia en unos diez milisegundos (el retardo del callout). El hardware real es mucho más rápido. Esta mirada más detallada cubre qué nos dicen las mediciones y qué no nos dicen.

### El presupuesto de latencia

La latencia de DMA de un dispositivo PCIe 3.0 moderno está dominada por:

1. **La latencia del posted write de PCIe.** El dispositivo escribe en el controlador de memoria de la CPU mediante un posted write, lo que supone un tiempo de ida y vuelta de unos pocos cientos de nanosegundos.
2. **La sobrecarga del flush/invalidate de caché.** En sistemas coherentes, son decenas de nanosegundos mediante snooping; en sistemas no coherentes, son microsegundos.
3. **La latencia de entrega de la interrupción.** La entrega de MSI-X a una CPU tarda microsegundos, incluido el wakeup del ithread.
4. **La latencia de despacho de tarea.** El wakeup y el despacho de la tarea rx tardan microsegundos.

Total: una transferencia DMA de un buffer de 4 KB en un sistema amd64 moderno tarda entre 5 y 10 microsegundos desde el inicio hasta la finalización, para un rendimiento sostenido de varios gigabytes por segundo.

### El techo de throughput

Con un buffer de 4 KB y 10 microsegundos por transferencia, el throughput es de 400 MB/s. Para alcanzar los 3,5 GB/s del enlace, el driver tiene que procesar por lotes: múltiples transferencias en curso simultáneamente. Un anillo de descriptores con N entradas puede tener N transferencias en curso y, si cada una tarda 10 microsegundos pero el driver lanza una por microsegundo, el throughput agregado es de 4 GB/s.

El driver de buffer único del Capítulo 21 no puede procesar por lotes; emite una transferencia cada vez. La extensión de anillo de descriptores (ejercicio de desafío) sí puede. El driver de red de la Parte 6 (Capítulo 28) utiliza este patrón en su totalidad.

### Midiendo el driver del capítulo

Una prueba de temporización sencilla:

```c
struct timespec start, end;
nanouptime(&start);
for (int i = 0; i < 1000; i++) {
    myfirst_dma_do_transfer_intr(sc, DIR_WRITE, 4096);
}
nanouptime(&end);
elapsed = timespec_sub(end, start);
```

Con el callout de diez milisegundos de la simulación, 1000 transferencias deberían tardar unos 10 segundos en total, lo que equivale a un throughput de 400 KB/s. Es un valor pequeño, porque la simulación está diseñada para ser observablemente asíncrona más que rápida. El hardware real sería órdenes de magnitud más rápido.

### Lo que nos dice la medición

La medición confirma que el pipeline es funcional de extremo a extremo. El número absoluto no es relevante (es una simulación, no hardware real). Lo que importa es que 1000 transferencias se completen sin error, sin timeout, sin que el helper se quede colgado y sin que los contadores diverjan de sus invariantes. Ese es el criterio de regresión para el Capítulo 21.

En un dispositivo real con capacidad DMA, sustituir el backend de simulación por el driver de hardware real intercambiaría el retardo del callout de 10 ms por la latencia real del hardware. El código del Capítulo 21 no cambia en nada más; este es el beneficio de portabilidad de construir sobre `bus_dma`.



## Resolución de problemas adicional: diez patrones de fallo más

Diez problemas comunes más que aparecen en drivers DMA al estilo del Capítulo 21, cada uno con su diagnóstico y solución.

### "El kernel entra en pánico en la primera transferencia con 'null pointer dereference'"

La causa probable es que `sc->dma_tag` o `sc->dma_map` es NULL porque la inicialización falló silenciosamente. Los caminos de error de la inicialización en la Etapa 1 deben propagar el fallo hacia arriba para que attach falle de forma limpia; si no lo hacen, attach parece tener éxito pero el camino de transferencia encuentra el puntero NULL.

La solución: revisa cada camino de retorno en `myfirst_dma_setup`. Cada fallo posterior a una asignación debe liberar las asignaciones anteriores y establecer los campos del softc a NULL.

### "La primera transferencia funciona, las siguientes se quedan colgadas"

Causa probable: `dma_in_flight` no se está limpiando al finalizar la primera transferencia. El `dma_handle_complete` de la tarea debe limpiarlo; el camino de timeout del helper también debe limpiarlo.

La solución: verifica que `dma_in_flight = false` se establece tanto en `dma_handle_complete` como en la rama `EWOULDBLOCK` del helper.

### "Las transferencias tienen éxito pero dma_complete_intrs es cero"

Causa probable: se está usando el helper de polling, no el de interrupciones. El camino de polling no incrementa `dma_complete_intrs`.

La solución: verifica que el manejador sysctl llama a `myfirst_dma_do_transfer_intr`, no a la versión de polling de la Etapa 2. La refactorización de la Etapa 3 debería haber reemplazado a los invocadores.

### "El contenido del buffer es correcto pero la verificación falla"

Causa probable: un desajuste de stride entre lo que escribió el simulador y lo que esperaba el driver. El driver espera bytes 0x5A; el simulador escribe 0x5A; pero el driver comprueba un desplazamiento diferente.

La solución: verifica que el `memset` del simulador coincide con la verificación del driver. Esto es casi siempre un error en el código de prueba, no un error de DMA.

### "El contador dma_errors aumenta pero el driver no propaga el error"

Causa probable: el helper incrementa el contador pero devuelve 0 en lugar de EIO.

La solución: verifica que la rama de error devuelve EIO y que el manejador sysctl propaga el error al invocador.

### "taskqueue_drain se queda colgado en kldunload"

Causa probable: hay una tarea pendiente, pero algo la vuelve a encolar continuamente. Lo más frecuente es que el filtro siga ejecutándose porque la máscara de interrupción no se estableció a cero.

La solución: enmascara la interrupción antes de vaciar el taskqueue. El orden de detach importa: enmascarar, vaciar callout, vaciar tarea, descargar el mapa.

### "bus_dmamem_alloc devuelve un buffer por encima de 4 GB aunque lowaddr = MAXADDR_32BIT"

Causa probable: el `highaddr` del tag es `BUS_SPACE_MAXADDR_32BIT` en lugar de `BUS_SPACE_MAXADDR`. El significado de `lowaddr` es "la dirección más baja de la ventana excluida"; `highaddr` es "la dirección más alta de la ventana excluida". Para excluir direcciones por encima de 4 GB, establece `lowaddr = BUS_SPACE_MAXADDR_32BIT` y `highaddr = BUS_SPACE_MAXADDR`.

La solución: intercambia las direcciones. El patrón habitual es: `lowaddr` es "la última dirección a la que puede llegar el dispositivo"; `highaddr` es habitualmente `BUS_SPACE_MAXADDR`.

### "El script de prueba funciona bien una vez, falla tras recargar el módulo"

Causa probable: el estado a nivel de módulo no se está reiniciando al recargar. Una variable static en `myfirst_sim.c` que conserva el estado del simulador entre cargas causaría esto.

La solución: verifica que cada variable a nivel de módulo se inicializa en el momento de `module_init` y se limpia en `module_fini`. El backend de simulación del Capítulo 17 debería estar haciendo esto correctamente.

### "WITNESS advierte sobre 'DMA map used while not loaded' o algo similar"

Causa probable: la llamada de sincronización se ejecuta después de que el mapa ha sido descargado, o antes de que haya sido cargado. El orden de operaciones debe ser: cargar, sincronizar, descargar.

La solución: revisa el camino de desmontaje; la descarga debe producirse después de la última sincronización y antes de destruir el tag.

### "Las transferencias funcionan en mi VM de prueba amd64 pero fallan en el servidor de compilación arm64"

Causa probable: comportamiento de coherencia dependiente de la plataforma. El driver puede estar omitiendo una llamada de sincronización que es un no-op en amd64 pero necesaria en arm64.

La solución: verifica que cada transferencia tiene sincronizaciones PRE y POST; vuelve a ejecutar la prueba con `INVARIANTS` en arm64 e inspecciona cualquier pánico.



## Referencia: recorrido completo por `myfirst_dma.c` de la Etapa 4

Un recorrido sección por sección del archivo `myfirst_dma.c` refactorizado. El archivo tiene unas 250 líneas; este recorrido explica la forma de cada función.

### Las inclusiones y macros

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_dma.h"
```

Conjunto estándar. `<sys/bus_dma.h>` se incluye de forma transitiva a través de `<machine/bus.h>`; no es necesario incluirlo explícitamente.

### El callback de segmento único

Tratado en la Sección 3. Diez líneas.

### La función de inicialización

Tratada en la Sección 3. Unas 50 líneas con una gestión de errores exhaustiva.

### La función de desmontaje

Tratada en la Sección 3. Unas 15 líneas.

### El helper de transferencia por polling

Tratado en la Sección 5. Unas 60 líneas. Se mantiene en la Etapa 4 como fallback o como camino de depuración; los manejadores sysctl pueden apuntar a él para pruebas comparativas.

### El helper de transferencia por interrupciones

Tratado en la Sección 6. Unas 70 líneas.

### El manejador de finalización

Tratado en la Sección 8. Unas 20 líneas.

### El registro sysctl

```c
void
myfirst_dma_add_sysctls(struct myfirst_softc *sc)
{
    struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
    struct sysctl_oid_list *kids = SYSCTL_CHILDREN(sc->sysctl_tree);

    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_transfers_write",
        CTLFLAG_RD, &sc->dma_transfers_write, 0,
        "Successful host-to-device DMA transfers");
    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_transfers_read",
        CTLFLAG_RD, &sc->dma_transfers_read, 0,
        "Successful device-to-host DMA transfers");
    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_errors",
        CTLFLAG_RD, &sc->dma_errors, 0,
        "DMA transfers that returned EIO");
    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_timeouts",
        CTLFLAG_RD, &sc->dma_timeouts, 0,
        "DMA transfers that timed out");
    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_complete_intrs",
        CTLFLAG_RD, &sc->dma_complete_intrs, 0,
        "DMA completion interrupts observed");
    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_complete_tasks",
        CTLFLAG_RD, &sc->dma_complete_tasks, 0,
        "DMA completion task invocations");
    SYSCTL_ADD_UQUAD(ctx, kids, OID_AUTO, "dma_bus_addr",
        CTLFLAG_RD, &sc->dma_bus_addr,
        "Bus address of the DMA buffer");

    SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "dma_test_write",
        CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE, sc, 0,
        myfirst_dma_sysctl_test_write, "IU",
        "Trigger a host-to-device DMA transfer");
    SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "dma_test_read",
        CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE, sc, 0,
        myfirst_dma_sysctl_test_read, "IU",
        "Trigger a device-to-host DMA transfer");
}
```

Siete contadores de solo lectura, una dirección de bus UQUAD (para visibilidad del operador) y dos sysctls de prueba de solo escritura.

### Los manejadores sysctl

```c
static int
myfirst_dma_sysctl_test_write(SYSCTL_HANDLER_ARGS)
{
    struct myfirst_softc *sc = arg1;
    unsigned int pattern;
    int err;

    err = sysctl_handle_int(oidp, &pattern, 0, req);
    if (err != 0 || req->newptr == NULL)
        return (err);

    memset(sc->dma_vaddr, (int)(pattern & 0xFF),
        MYFIRST_DMA_BUFFER_SIZE);
    err = myfirst_dma_do_transfer_intr(sc, MYFIRST_DMA_DIR_WRITE,
        MYFIRST_DMA_BUFFER_SIZE);
    if (err != 0)
        device_printf(sc->dev,
            "dma_test_write: err %d\n", err);
    else
        device_printf(sc->dev,
            "dma_test_write: pattern 0x%02x transferred\n",
            pattern & 0xFF);
    return (err);
}

static int
myfirst_dma_sysctl_test_read(SYSCTL_HANDLER_ARGS)
{
    struct myfirst_softc *sc = arg1;
    unsigned int ignore;
    int err;
    uint8_t *bytes;

    err = sysctl_handle_int(oidp, &ignore, 0, req);
    if (err != 0 || req->newptr == NULL)
        return (err);

    err = myfirst_dma_do_transfer_intr(sc, MYFIRST_DMA_DIR_READ,
        MYFIRST_DMA_BUFFER_SIZE);
    if (err != 0) {
        device_printf(sc->dev, "dma_test_read: err %d\n", err);
        return (err);
    }

    bytes = (uint8_t *)sc->dma_vaddr;
    device_printf(sc->dev,
        "dma_test_read: first bytes %02x %02x %02x %02x "
        "%02x %02x %02x %02x\n",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7]);
    return (0);
}
```

El manejador de escritura rellena el buffer y dispara la transferencia del host al dispositivo. El manejador de lectura dispara la transferencia del dispositivo al host y registra los primeros ocho bytes. Ambos son pequeños; ambos hacen que el subsistema DMA sea accesible desde el espacio de usuario sin necesidad de un ioctl personalizado.

### Líneas de código

El archivo completo `myfirst_dma.c` tiene unas 280 líneas. A modo de comparación, `myfirst_msix.c` del Capítulo 20 tenía 330 líneas; `myfirst_intr.c` del Capítulo 19 tenía 250 líneas. El archivo del Capítulo 21 está en la mitad de ese rango, lo que se corresponde con la complejidad: la configuración DMA es más intrincada que la configuración de interrupciones de un solo vector, pero menos que el enrutamiento de múltiples vectores.

Junto con las 330 líneas de `myfirst_msix.c` y las 250 de `myfirst_intr.c`, los tres archivos de los Capítulos 19-21 suman unas 860 líneas. Un lector que los haya escrito y comprendido habrá escrito en miniatura tres subsistemas de driver de calidad de producción.



## Referencia: páginas del manual de FreeBSD para el Capítulo 21

Las páginas del manual en las que se ha apoyado directamente el Capítulo 21. Cada una merece una lectura una vez que el material del capítulo se haya asentado.

- `bus_dma(9)`: la referencia central para la API bus-DMA.
- `bus_space(9)`: la capa de acceso sobre la que se construye el driver (Capítulo 16).
- `contigmalloc(9)`: el asignador contiguo que usa `bus_dma` por debajo.
- Referencias cruzadas de `busdma(9)`, incluida la entrada `bus_dma_tag_template(9)`.
- `callout(9)`: el subsistema de temporizadores que usa el motor de la simulación.
- `condvar(9)`: la disciplina `cv_*` que usa el helper.
- `device(9)`: el framework general de dispositivos.

Las páginas del manual se mantienen junto con el kernel y registran los cambios en la API. Leerlas una vez por versión mayor (o tras un cambio significativo en la cobertura del libro) es un hábito útil.



## Una mirada más profunda a cómo encajan DMA e interrupciones

Los Capítulos 19, 20 y 21 han construido tres piezas interconectadas: interrupciones, múltiples vectores y DMA. Esta mirada más profunda explica cómo encajan en un driver completo de alto rendimiento, de modo que el lector tenga un modelo mental del conjunto antes de que llegue la disciplina de gestión de energía del Capítulo 22.

### La arquitectura de tres piezas

Un driver moderno con capacidad DMA tiene tres subsistemas que cooperan entre sí:

1. **El subsistema DMA** (Capítulo 21) es el propietario del camino de datos. Configura los buffers, programa el dispositivo, sincroniza las cachés y gestiona los datos de la transferencia. Siempre que el driver mueve datos de tamaño no trivial, DMA está involucrado.

2. **El subsistema de interrupciones** (Capítulos 19 y 20) es el propietario del camino de señalización. El dispositivo señala eventos (llegada de datos, finalización de transferencia, errores) mediante interrupciones. El filtro gestiona la señal en contexto de interrupción primaria; la tarea gestiona el seguimiento en contexto de thread.

3. **La capa de coordinación** (el softc y su mutex junto con las variables de condición) es la propietaria del estado entre ambos. El subsistema DMA escribe `dma_in_flight` antes de disparar una transferencia; el subsistema de interrupciones lo lee durante el procesamiento de finalización. La variable de condición permite que el subsistema DMA espere a que el subsistema de interrupciones señale la finalización.

Las tres piezas están diseñadas para componerse. Un driver puede tener muchas operaciones DMA en curso, cada una señalizando su finalización a través de su propio vector de interrupción; la maquinaria por vector del Capítulo 20 gestiona esto de forma natural, y el estado por transferencia del Capítulo 21 encaja en las tareas por vector.

### El flujo común

Para cada transferencia DMA en un driver de producción, el flujo es:

1. El código del driver quiere transferir datos.
2. El driver bloquea el softc y configura el estado por transferencia.
3. El driver emite la sincronización PRE sobre el mapa.
4. El driver programa los registros del dispositivo con la dirección de bus, la longitud, la dirección y los indicadores.
5. El driver escribe el doorbell.
6. El driver libera el lock (o duerme sobre una variable de condición bajo el lock).
7. El kernel realiza otras tareas.
8. El dispositivo completa la transferencia y genera su interrupción.
9. El filtro se ejecuta, reconoce la interrupción y encola una tarea.
10. La tarea se ejecuta, adquiere el lock del softc y procesa la finalización: sincronización POST, lectura de estado, registro del resultado.
11. La tarea difunde la variable de condición y libera el lock.
12. El código del driver (o quien lo espera) se despierta, lee el resultado y continúa.

El flujo es el mismo para un driver sencillo de buffer único y para un driver de red multi-cola con miles de transferencias en curso. La diferencia es de escala: el driver de red tiene muchas transferencias concurrentes, cada una con su propio estado por transferencia, pero cada una sigue el mismo flujo de doce pasos.

### La historia de la escalabilidad

En la simulación de buffer único del Capítulo 21, el flujo se ejecuta una vez cada diez milisegundos. En una NIC moderna, el flujo se ejecuta millones de veces por segundo a través de muchas colas. La maquinaria escala porque:

- Las interrupciones son por cola mediante MSI-X (Capítulo 20).
- Los buffers DMA son por cola mediante anillos de descriptores (un capítulo futuro).
- El filtro es corto y seguro para el contexto de filtro (Capítulo 19).
- La tarea es por cola y está vinculada a un núcleo NUMA-local (Capítulo 20).
- El estado del softc está particionado por cola para evitar la contención.

Las decisiones de diseño acumuladas que enseñan las Partes 4 y 5 son las que hacen posible esta escalabilidad. Un driver que respeta la disciplina escala de forma natural; un driver que la viola (un lock global en lugar de locks por cola, un mapa DMA compartido en lugar de mapas por transferencia) alcanza los límites de escalabilidad pronto.

### Los patrones de interacción

Tres patrones de interacción específicos que merece la pena reconocer:

**Patrón A: la interrupción entrega «hay nuevos datos en el ring DMA».** El filter lee un registro de índice de finalización, lo confirma y encola la tarea. La tarea recorre el ring desde el último índice hasta el actual, procesa cada entrada y actualiza el estado por cola. Así funciona la ruta de recepción de una NIC.

**Patrón B: la interrupción entrega «la transferencia DMA N ha finalizado».** El filter busca la transferencia N en un array de estado por transferencia y encola la tarea. La tarea examina el estado registrado de la transferencia N, realiza la sincronización POST y llama al callback de finalización. Así funciona la ruta de finalización de comandos de un controlador de almacenamiento.

**Patrón C: la interrupción entrega «se ha producido un error en el motor DMA».** Aquí el filter puede requerir más cuidado, porque los errores suelen exigir un reset a nivel de dispositivo, que no puede realizarse en el contexto del filter. El filter encola una tarea con un payload específico del error; la tarea detiene el motor, registra el error y decide si es necesario reiniciarlo.

El driver del capítulo 21 implementa una versión simplificada del Patrón B. Los drivers reales implementan los tres, a veces sobre vectores separados.

## Una mirada más profunda: comparaciones con otras APIs de DMA del kernel

Para los lectores que provienen de otros contextos de kernel, se presenta una breve comparación entre `bus_dma(9)` de FreeBSD y las APIs equivalentes en otros sistemas. No es exhaustiva; basta con orientar al lector que necesite traducir entre modelos mentales.

### La API de DMA de Linux

Linux usa `dma_alloc_coherent` / `dma_map_single` / `dma_sync_single_for_cpu` / `dma_sync_single_for_device` / `dma_unmap_single` para lo que FreeBSD denomina `bus_dmamem_alloc` / `bus_dmamap_load` / `bus_dmamap_sync` (POST) / `bus_dmamap_sync` (PRE) / `bus_dmamap_unload`.

El modelo semántico es casi idéntico:

- `dma_alloc_coherent` devuelve una dirección virtual visible para la CPU y una dirección de bus visible para DMA en un buffer coherente; `bus_dmamem_alloc` más `bus_dmamap_load` hace lo mismo en dos pasos.
- `dma_map_single` mapea un buffer arbitrario a una dirección de bus, de forma similar a `bus_dmamap_load` con un callback de segmento único.
- `dma_sync_single_for_cpu` corresponde a `bus_dmamap_sync(..., POSTREAD)`; `for_device` corresponde a `PREWRITE` o `PREREAD`.
- `dma_unmap_single` es la versión de Linux de `bus_dmamap_unload`.

Las características distintivas de la API de FreeBSD son el tag (un descriptor de restricciones explícito; el enfoque de Linux es más implícito), el callback (para loads diferidos; Linux usa las APIs DMA fence en su lugar) y la herencia jerárquica de tags (que Linux no formaliza de la misma manera).

Para código de driver de Linux que se está portando a FreeBSD, la traducción aproximada es:

- Define un tag `bus_dma_tag_t` con las restricciones del dispositivo.
- Sustituye `dma_alloc_coherent` por `bus_dma_tag_create` (una vez) más `bus_dmamem_alloc` más `bus_dmamap_load`.
- Sustituye `dma_map_single` por transferencia por `bus_dmamap_create` más `bus_dmamap_load` (para maps dinámicos).
- Sustituye cada `dma_sync_*_for_cpu` por `bus_dmamap_sync(..., POSTREAD)` o `POSTWRITE`.
- Sustituye cada `dma_sync_*_for_device` por `bus_dmamap_sync(..., PREREAD)` o `PREWRITE`.
- Sustituye `dma_unmap_single` por `bus_dmamap_unload`.

Los ports de DRM-kmod de drivers de GPU de Linux a FreeBSD hacen un uso extensivo de esta traducción; un driver portado de Linux a FreeBSD generalmente gana un setup explícito de tag y pares PRE/POST explícitos donde Linux tenía implícitos.

### El bus_dma de NetBSD

NetBSD tiene la API `bus_dma` original de la que deriva la de FreeBSD. Los nombres de las funciones son casi idénticos; la semántica es casi idéntica. Las diferencias están principalmente en las APIs periféricas (soporte de plantillas, integración IOMMU, extensiones específicas de plataforma).

Un driver escrito para el `bus_dma` de NetBSD generalmente compila en FreeBSD con ajustes menores. La portabilidad no es accidental; es el objetivo de diseño de la API.

### Las abstracciones DMA de Windows

Windows utiliza una abstracción diferente (`AllocateCommonBuffer`, `IoMapTransfer`, `FlushAdapterBuffers`) con semántica distinta. Portar un driver de Windows a FreeBSD es una tarea más compleja porque el modelo de Windows no tiene el concepto de tag de FreeBSD y su sincronización es menos explícita.

Un lector que trabaje en ambos ecosistemas se beneficia de comprender que las realidades del hardware subyacente son las mismas en todas partes; solo difiere la superficie de la API. La disciplina de `bus_dma` del Capítulo 21 se traslada, aunque con una capa de traducción, a cualquier entorno de kernel.



## Referencia: un recorrido breve por `/usr/src/sys/kern/subr_bus_dma.c`

La implementación del kernel de la API `bus_dma` reside en `/usr/src/sys/kern/subr_bus_dma.c` y en los archivos `busdma_*.c` específicos de la arquitectura. Un recorrido breve por el archivo central da al lector curioso una idea de dónde se ejecuta realmente el mecanismo.

El archivo contiene helpers genéricos a los que llaman los backends específicos de la arquitectura. Los puntos de entrada principales:

- `bus_dmamap_load_uio`: carga un `struct uio` (I/O de usuario).
- `bus_dmamap_load_mbuf_sg`: carga una cadena mbuf con un array de segmentos preasignado.
- `bus_dmamap_load_ccb`: carga un bloque de control CAM.
- `bus_dmamap_load_bio`: carga un `struct bio`.
- `bus_dmamap_load_crp`: carga una operación criptográfica.

Cada una de ellas es un wrapper fino alrededor de `bus_dmamap_load_ma` (carga un array de mappings) o `bus_dmamap_load_phys` (carga una dirección física) de la plataforma, que son las primitivas específicas de la arquitectura.

El archivo también contiene la API de plantilla (`bus_dma_template_*`) y sus helpers. El código de la plantilla tiene unas 200 líneas y es sencillo de leer.

El archivo genérico no contiene la lógica de sync ni de bounce por arquitectura; esta reside en `/usr/src/sys/x86/x86/busdma_bounce.c` para amd64 e i386, o en el directorio de plataforma equivalente. El lector que quiera entender qué hace realmente `bus_dmamap_sync` en amd64 puede leer la función `bounce_bus_dmamap_sync` de `busdma_bounce.c`; tiene unas 100 líneas y muestra la lógica de copia bounce junto con las barreras de memoria.



## Referencia: frases memorables del driver

Una lista breve de frases que vale la pena memorizar porque condensan la disciplina del capítulo en pocas palabras.

- "El tag describe, el map es específico, sync señala la propiedad, unload revierte load."
- "Todo PRE tiene su POST; todo setup tiene su teardown."
- "`BUS_DMA_COHERENT` hace que sync sea barato, no innecesario."
- "La dirección de bus no es siempre la dirección física."
- "`BUS_SPACE_MAXADDR_32BIT` es el `lowaddr` para dispositivos de 32 bits; `highaddr` se mantiene en `BUS_SPACE_MAXADDR`."
- "El callback puede ejecutarse más tarde; usa `BUS_DMA_NOWAIT` para evitar ese caso."
- "La lockfunc del tag es para loads diferidos; `NULL` provoca un panic si se difiere, lo que lo convierte en el valor por defecto más seguro."
- "Los callbacks de segmento único tienen el mismo aspecto en todos los drivers; el patrón es universal."
- "Vacía los callouts antes de hacer unload de los maps; vacía las tasks antes de destruir los tags."



## Referencia: tabla comparativa de la Parte 4

Una tabla que resume qué añadió cada capítulo de la Parte 4, cómo cambió la etiqueta de versión del driver y a qué nuevo tipo de recurso ganó acceso.

| Capítulo | Versión      | Subsistema añadido                              | Nuevos tipos de recurso                               |
|----------|--------------|-------------------------------------------------|-------------------------------------------------------|
| 16       | 0.9-mmio     | Acceso a registros mediante bus_space           | bus_space_tag_t, bus_space_handle_t                  |
| 17       | 1.0-sim      | Backend de hardware simulado                    | backend sim, mapa de registros simulado               |
| 18       | 1.1-pci      | attach PCI real                                 | recurso BAR, acceso a configuración PCI               |
| 19       | 1.2-intr     | Gestión de interrupciones legacy                | recurso IRQ, filter, task                             |
| 20       | 1.3-msi      | MSI/MSI-X multivector                           | recursos IRQ por vector, tasks por vector             |
| 21       | 1.4-dma      | DMA con bus_dma(9)                              | tag DMA, map DMA, memoria DMA, dirección de bus       |

Cada capítulo añade exactamente un subsistema; la complejidad del driver crece de forma monótona con sus capacidades. Un lector que siga el historial de versiones puede ver de un vistazo qué es capaz de hacer el driver en cada etapa.

La Parte 4 cierra con el trabajo de gestión de energía del Capítulo 22, que no añade un nuevo subsistema sino que impone disciplina en todos los existentes: cada subsistema debe ser capaz de quiescerse y reanudarse.



## Referencia: lo que el lector debería ser capaz de hacer

Una comprobación de una página. Tras finalizar el Capítulo 21 y completar los laboratorios, el lector debería ser capaz de:

1. Abrir cualquier driver con capacidad DMA en `/usr/src/sys/dev/` e identificar su creación de tag, asignación de memoria, carga del map, patrón de sync y teardown.
2. Explicar por qué un driver llama a `bus_dmamap_sync` con un flag PRE antes de activar el dispositivo y un flag POST tras la finalización del dispositivo.
3. Escribir una llamada de creación de tag para un dispositivo hipotético con restricciones documentadas (alineación, tamaño, rango de direccionamiento).
4. Escribir un callback de segmento único y usarlo para extraer la dirección de bus de una llamada a `bus_dmamap_load`.
5. Reconocer cuándo un driver usa el patrón estático frente al dinámico.
6. Identificar y explicar las tres realidades de portabilidad que `bus_dma(9)` oculta: límites de direccionamiento, remapeo IOMMU y coherencia de caché.
7. Depurar una transferencia que tiene éxito en términos de contadores pero produce datos corruptos, comprobando la ausencia o el tipo incorrecto de syncs.
8. Construir un camino de detach limpio que vacíe los callouts, vacíe las tasks, enmascare las interrupciones, haga unload de los maps, libere memoria y destruya los tags en el orden correcto.
9. Distinguir entre `BUS_DMA_COHERENT` en el momento de creación del tag (sugerencia arquitectónica) y en el momento de `bus_dmamem_alloc` (modo de asignación).
10. Explicar por qué `BUS_DMA_NOWAIT` es el valor por defecto más seguro para la mayoría de los drivers y cuándo es apropiado `BUS_DMA_WAITOK`.
11. Escribir desde cero un driver DMA de buffer único correcto, a partir de un datasheet que especifique el mapa de registros y el comportamiento del motor.
12. Explicar por qué existe la capa `bus_dma(9)` y qué ocurre cuando un driver la evita.

Un lector que pueda marcar diez o más de estos puntos ha interiorizado el capítulo. Los elementos restantes llegan con la práctica en drivers reales.



## Referencia: cuándo usar cada variante de la API

La API `bus_dma(9)` ofrece varias variantes de carga; a continuación se presenta una referencia rápida para ayudar al lector a elegir correctamente en trabajos futuros:

- **`bus_dmamap_load`**: un buffer genérico del kernel en un KVA y longitud conocidos. La llamada más sencilla y común. Es la que usa el Capítulo 21.
- **`bus_dmamap_load_mbuf_sg`**: una cadena mbuf, habitual en drivers de red. La variante `_sg` rellena un array de segmentos preasignado y evita el callback.
- **`bus_dmamap_load_ccb`**: un bloque de control CAM, utilizado por drivers de almacenamiento.
- **`bus_dmamap_load_uio`**: un `struct uio`, utilizado para mapear buffers suministrados por el usuario.
- **`bus_dmamap_load_bio`**: una petición de I/O de bloque, utilizada en consumidores GEOM.
- **`bus_dmamap_load_crp`**: una operación criptográfica, utilizada por las transformaciones OpenCrypto.

El capítulo solo utiliza la primera. El lector que avance hacia redes, almacenamiento o criptografía más adelante verá las variantes especializadas; cada una es un wrapper fino alrededor del mismo mecanismo subyacente, con un helper que desempaqueta la representación de buffer preferida del subsistema en segmentos.

Para transacciones dinámicas en las que el driver mantiene su propio pool de maps, `bus_dmamap_create` y `bus_dmamap_destroy` complementan las llamadas de carga. El patrón estático del Capítulo 21 no las usa; el patrón dinámico del capítulo de redes de la Parte 6 (Capítulo 28) sí.

Un detalle sutil que conviene recordar: `bus_dmamem_alloc` tanto asigna memoria como crea un map implícito. Ese map no necesita `bus_dmamap_create` ni `bus_dmamap_destroy`. El driver sigue llamando a `bus_dmamap_load` una vez para obtener la dirección de bus (a través del callback) y a `bus_dmamap_unload` una vez en el teardown; pero el map en sí lo gestiona el asignador, no el driver. Un driver que llama a `bus_dmamap_destroy` sobre un map devuelto por el asignador provoca un panic. Mantén los dos ciclos de vida bien separados.



## Cerrando

El Capítulo 21 dio al driver la capacidad de mover datos. Al principio, `myfirst` en la versión `1.3-msi` podía escuchar a su dispositivo a través de múltiples vectores de interrupción, pero accedía a cada byte mediante `bus_read_4`. Al final, `myfirst` en la versión `1.4-dma` dispone de un camino de setup y teardown con `bus_dma(9)`, un motor DMA simulado con finalización controlada por callout, un helper de transferencia dirigido por interrupciones que duerme en una variable de condición mientras el dispositivo trabaja, una disciplina de sync PRE/POST completa en cada transferencia, contadores por transferencia para observabilidad, un archivo `myfirst_dma.c` refactorizado, un documento `DMA.md` y una prueba de regresión que ejercita cada camino de código que enseña el capítulo.

Las ocho secciones recorrieron la progresión completa. La sección 1 estableció el panorama del hardware: qué es DMA, por qué importa y cómo son los ejemplos del mundo real. La sección 2 fijó el vocabulario de `bus_dma(9)`: tag, map, memoria, segmento, PRE, POST, estático, dinámico, padre, callback. La sección 3 escribió el primer código funcional: creación del tag, asignación de memoria, carga del map y un teardown limpio. La sección 4 estableció la disciplina de sincronización: el modelo de propiedad, las cuatro flags y los cuatro patrones comunes. La sección 5 construyó el motor DMA simulado con mapa de registros, máquina de estados, callout y una función auxiliar del driver basada en polling. La sección 6 reescribió el camino de completado para usar la maquinaria de interrupciones por vector del Capítulo 20. La sección 7 recorrió doce modos de fallo y los patrones que permiten gestionar cada uno. La sección 8 refactorizó el código en `myfirst_dma.c`, actualizó el Makefile, incrementó la versión, añadió documentación y cerró el alcance del capítulo.

El Capítulo 21 no cubrió scatter-gather, anillos de descriptores, integración con iflib ni mapeo de buffers en espacio de usuario. Cada uno de ellos es una extensión natural construida sobre las primitivas del Capítulo 21, y cada uno pertenece a un capítulo posterior (la Parte 6 para los detalles de red y la Parte 7 para el ajuste de rendimiento). La base está en su lugar; las especializaciones añaden vocabulario sin necesidad de reconstruir la base.

La estructura de archivos ha crecido: 15 archivos fuente (incluido `cbuf`), 7 archivos de documentación (`HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`, `DMA.md`) y una suite de regresión extendida. El driver es estructuralmente paralelo a los drivers de FreeBSD en producción; un lector que haya completado los Capítulos del 16 al 21 puede abrir `if_re.c`, `nvme_qpair.c` o `ahci.c` y reconocer las partes arquitectónicas: accesores de registros, backend de simulación, PCI attach, filtro de interrupción y tarea, maquinaria por vector, setup y teardown de DMA, disciplina de sincronización y detach limpio.

### Una reflexión antes del capítulo 22

El capítulo 21 fue el último capítulo de la Parte 4 que introdujo una nueva primitiva de hardware. Los capítulos 16 a 21 llevaron al driver desde la ausencia de conciencia del hardware hasta un driver DMA completamente funcional respaldado por hardware. El capítulo 22 es el capítulo de la disciplina: cómo hacer que este driver sobreviva a los ciclos de suspend y resume, cómo guardar y restaurar el estado en torno a las transiciones de energía, cómo detener de forma segura todos los subsistemas (interrupciones, DMA, temporizadores, tareas) antes de que el dispositivo pierda alimentación y reanudarlos limpiamente después. La ruta de desmontaje de DMA que construyó el capítulo 21 es lo que llamará el manejador de suspend del capítulo 22; la ruta de inicialización de DMA es lo que llamará el manejador de resume.

La enseñanza del capítulo 21 también generaliza. Un lector que haya interiorizado la disciplina de tag-map-memoria-sync, el modelo de propiedad PRE/POST, el callback de segmento único y el patrón limpio de inicialización y desmontaje encontrará formas similares en cada driver de FreeBSD con capacidad DMA. El dispositivo específico varía; la estructura no.

### Qué hacer si te quedas atascado

Tres sugerencias.

Primero, céntrate en la ruta de sondeo de la etapa 2. Si `sudo sysctl dev.myfirst.0.dma_test_write=0xAA` produce un mensaje correcto en `dmesg`, la ruta de sondeo funciona. Cada otra parte del capítulo es opcional en el sentido de que decora el pipeline, pero si el pipeline falla, el capítulo no funciona en su conjunto y la sección 5 es el lugar adecuado para diagnosticar.

Segundo, abre `/usr/src/sys/dev/re/if_re.c` y relee `re_allocmem` despacio. Contiene aproximadamente ciento cincuenta líneas de código de configuración de tags y memoria. Cada línea corresponde a un concepto del capítulo 21. Leerlo una vez después de completar el capítulo debería resultar terreno familiar; los patrones del driver real parecerán elaboraciones de los más simples del capítulo.

Tercero, omite los desafíos en la primera lectura. Los laboratorios están calibrados para el ritmo del capítulo 21; los desafíos dan por sentado que el material del capítulo está bien asimilado. Vuelve a ellos después del capítulo 22 si ahora los ves fuera de tu alcance.

El objetivo del capítulo 21 era dotar al driver de la capacidad de mover datos. Si lo ha logrado, la maquinaria de gestión de energía del capítulo 22 se convierte en una especialización y no en un tema completamente nuevo.



## Puente hacia el capítulo 22

El capítulo 22 se titula *Gestión de energía*. Su ámbito es la disciplina de guardar y restaurar el estado de un driver en torno a las operaciones de suspend y resume del sistema. Los sistemas modernos suspenden de forma agresiva: los portátiles suspenden cuando se cierra la tapa; los servidores suspenden dispositivos individuales que están inactivos (los estados de energía D0, D1, D2 y D3 de PCIe); las máquinas virtuales migran entre hosts. Un driver que no gestiona estas transiciones correctamente deja el dispositivo en un estado inconsistente, provoca que el resume se quede bloqueado o corrompe datos en tránsito.

El capítulo 21 preparó el terreno de tres maneras concretas.

Primero, **dispones de una ruta de desmontaje completa**. Los recursos de DMA pueden desmontarse y reconstruirse limpiamente; el manejador de suspend del capítulo 22 llamará a `myfirst_dma_teardown` y el manejador de resume llamará a `myfirst_dma_setup`. El versionado del estado (attaching, detaching y ahora suspending) es una extensión de los patrones que el capítulo 21 ya introdujo.

Segundo, **dispones de un rastreador de transferencias en tránsito**. El campo `dma_in_flight` del capítulo 21 y el protocolo de espera con `cv_timedwait` son exactamente lo que el manejador de suspend del capítulo 22 necesita para garantizar que no haya ninguna transferencia pendiente cuando el dispositivo pierda alimentación. Reutilizar el rastreador mantiene el código uniforme.

Tercero, **dispones de una API de enmascaramiento de interrupciones limpia**. El driver enmascara y desenmascara `MYFIRST_INTR_COMPLETE` mediante la maquinaria de los capítulos 19 y 20. El manejador de suspend del capítulo 22 enmascarará todas las interrupciones antes de la transición de energía; el manejador de resume las desenmascarará una vez que el dispositivo se haya estabilizado.

Temas específicos que cubrirá el capítulo 22:

- Qué significan los estados de suspend de ACPI (S1, S3, S4) y qué requieren de los drivers.
- Los estados de energía de los dispositivos PCIe (D0, D1, D2, D3hot, D3cold) y cómo FreeBSD realiza las transiciones entre ellos.
- Los métodos `device_suspend` y `device_resume`; cómo implementarlos.
- Detener el DMA de forma segura: cómo garantizar que no haya ninguna transferencia en tránsito pendiente cuando se pierde la alimentación.
- Reconexión tras el resume: reinicializar el estado del hardware, recargar tablas, restaurar interrupciones.
- Gestionar dispositivos que se reinician durante el suspend: detectar el reinicio y reconstruir el estado.
- Integrar con el resto de la maquinaria de los capítulos 16-21 (bus_space, simulación, PCI, interrupciones, DMA).

No necesitas leer por adelantado. El capítulo 21 es preparación suficiente. Trae tu driver `myfirst` en la versión `1.4-dma`, tu `LOCKING.md`, tu `INTERRUPTS.md`, tu `MSIX.md`, tu `DMA.md`, tu kernel con `WITNESS` habilitado y tu script de regresión. El capítulo 22 comienza donde terminó el capítulo 21.

La Parte 4 está casi completa. El capítulo 22 cierra la parte añadiendo la última disciplina que separa un driver de prototipo de un driver de producción: la capacidad de sobrevivir a las transiciones de energía que los sistemas reales imponen.

El vocabulario es tuyo; la estructura es tuya; la disciplina es tuya. El capítulo 22 añade la siguiente pieza que faltaba: la capacidad del driver de detenerse y arrancar de forma ordenada, en respuesta a eventos que el propio driver no inició.



## Referencia: ficha de consulta rápida del capítulo 21

Un resumen compacto del vocabulario, las APIs, los flags y los procedimientos que introdujo el capítulo 21.

### Vocabulario

- **DMA (Direct Memory Access):** lectura o escritura de memoria del host por parte del dispositivo sin intervención del CPU byte a byte.
- **Bus-master:** dispositivo capaz de iniciar transacciones de bus hacia la memoria del host.
- **Tag (`bus_dma_tag_t`):** descripción de las restricciones de DMA para un grupo de transferencias.
- **Map (`bus_dmamap_t`):** contexto de mapeo para una transferencia específica.
- **Segment (`bus_dma_segment_t`):** par (bus_addr, longitud) que describe una parte contigua de un mapeo.
- **Bounce buffer:** región de almacenamiento provisional gestionada por el kernel que se utiliza cuando el dispositivo no puede acceder al buffer real del driver.
- **Memoria coherente:** memoria asignada con `BUS_DMA_COHERENT`; las operaciones de sync son económicas.
- **Callback:** función a la que llama `bus_dmamap_load` con la lista de segmentos.
- **Transacción estática:** mapeo de larga duración asignado en el momento del attach.
- **Transacción dinámica:** mapeo por transferencia que se crea y destruye en cada uso.
- **Tag padre:** tag heredado del puente padre; las restricciones se componen por intersección.
- **PREWRITE/POSTWRITE/PREREAD/POSTREAD:** los cuatro flags de sync.
- **IOMMU:** MMU de entrada-salida; remapeo transparente entre los espacios de direcciones del dispositivo y del host.

### APIs esenciales

- `bus_dma_tag_create(parent, align, bdry, low, high, filt, filtarg, maxsz, nseg, maxsegsz, flags, lockfn, lockarg, &tag)`: crear un tag de DMA.
- `bus_dma_tag_destroy(tag)`: destruir un tag de DMA; falla si quedan hijos.
- `bus_get_dma_tag(dev)`: devolver el tag de DMA padre del dispositivo.
- `bus_dmamem_alloc(tag, &vaddr, flags, &map)`: asignar memoria con capacidad de DMA y obtener un map.
- `bus_dmamem_free(tag, vaddr, map)`: liberar la memoria asignada por `bus_dmamem_alloc`.
- `bus_dmamap_create(tag, flags, &map)`: crear un map para cargas dinámicas.
- `bus_dmamap_destroy(tag, map)`: destruir un map.
- `bus_dmamap_load(tag, map, buf, len, callback, cbarg, flags)`: cargar un buffer en un map.
- `bus_dmamap_unload(tag, map)`: descargar un map.
- `bus_dmamap_sync(tag, map, op)`: sincronizar cachés para `op` (uno de los flags PRE/POST).

### Flags esenciales

- `BUS_DMA_WAITOK`: el asignador puede dormir.
- `BUS_DMA_NOWAIT`: el asignador no debe dormir; devuelve `ENOMEM` si no hay recursos disponibles.
- `BUS_DMA_COHERENT`: preferir memoria coherente con caché.
- `BUS_DMA_ZERO`: inicializar a cero la memoria asignada.
- `BUS_DMA_ALLOCNOW`: pre-asignar recursos de bounce en el momento de crear el tag.

### Operaciones de sync esenciales

- `BUS_DMASYNC_PREREAD`: antes de que el dispositivo escriba, el host leerá.
- `BUS_DMASYNC_PREWRITE`: antes de que el dispositivo lea, el host ha escrito.
- `BUS_DMASYNC_POSTREAD`: después de que el dispositivo escriba, antes de que el host lea.
- `BUS_DMASYNC_POSTWRITE`: después de que el dispositivo lea, el host puede reutilizar.

### Procedimientos habituales

**Asignar buffer de DMA estático:**

```c
bus_dma_tag_create(bus_get_dma_tag(dev), ..., &sc->tag);
bus_dmamem_alloc(sc->tag, &sc->vaddr, BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->map);
bus_dmamap_load(sc->tag, sc->map, sc->vaddr, size, single_map_cb, &sc->bus_addr, BUS_DMA_NOWAIT);
```

**Liberar buffer de DMA estático:**

```c
bus_dmamap_unload(sc->tag, sc->map);
bus_dmamem_free(sc->tag, sc->vaddr, sc->map);
bus_dma_tag_destroy(sc->tag);
```

**Transferencia del host al dispositivo:**

```c
/* Fill sc->vaddr. */
bus_dmamap_sync(sc->tag, sc->map, BUS_DMASYNC_PREWRITE);
/* Program device, trigger, wait for completion. */
bus_dmamap_sync(sc->tag, sc->map, BUS_DMASYNC_POSTWRITE);
```

**Transferencia del dispositivo al host:**

```c
bus_dmamap_sync(sc->tag, sc->map, BUS_DMASYNC_PREREAD);
/* Program device, trigger, wait for completion. */
bus_dmamap_sync(sc->tag, sc->map, BUS_DMASYNC_POSTREAD);
/* Read sc->vaddr. */
```

### Comandos útiles

- `sysctl hw.busdma`: estadísticas del subsistema DMA.
- `vmstat -m | grep bus_dma`: uso de memoria.
- `pciconf -lvbc <dev>`: listado de capacidades del dispositivo.
- `procstat -kke`: estados de thread y pilas.

### Archivos para tener a mano

- `/usr/src/sys/sys/bus_dma.h`: la cabecera pública.
- `/usr/src/share/man/man9/bus_dma.9`: la página de manual.
- `/usr/src/sys/dev/re/if_re.c`: driver de anillo de descriptores en producción.
- `/usr/src/sys/dev/nvme/nvme_qpair.c`: construcción de cola NVMe.



## Referencia: glosario de términos del capítulo 21

Un breve glosario de los términos nuevos del capítulo.

- **Restricción de alineación:** requisito de que la dirección inicial de un buffer sea múltiplo de un valor específico.
- **Restricción de frontera:** requisito de que un buffer no cruce un límite de dirección específico.
- **Bounce page:** una única página de bounce buffer utilizada para preparar datos de cara a un mapeo.
- **Dirección de bus:** dirección que utiliza un dispositivo para acceder a una región de memoria; puede diferir de la dirección física.
- **Bus-master:** dispositivo que puede iniciar transacciones en el bus (es decir, realizar DMA).
- **Callback (DMA):** función a la que llama `bus_dmamap_load` con la lista de segmentos cuando la carga se completa.
- **Memoria coherente:** memoria en un dominio donde el CPU y el DMA ven los mismos datos sin sincronización explícita.
- **Carga diferida:** carga que devolvió `EINPROGRESS` y cuyo callback se llamará más adelante.
- **Descriptor:** estructura pequeña (en memoria DMA) que describe una transferencia: dirección, longitud, flags, estado.
- **Anillo de descriptores:** array de descriptores utilizado para la comunicación productor-consumidor.
- **Tag DMA:** véase `bus_dma_tag_t`.
- **Doorbell:** registro MMIO en el que el driver escribe para señalar al dispositivo la existencia de nuevas entradas en el anillo.
- **Transacción dinámica:** véase la sección 2.
- **IOMMU:** MMU de entrada-salida; traduce las direcciones del lado del dispositivo a direcciones físicas del host.
- **KVA:** dirección virtual del kernel; el puntero que utiliza el CPU.
- **Load callback:** sinónimo de Callback (DMA).
- **Mapping:** vinculación entre un buffer del kernel y un conjunto de segmentos visibles en el bus.
- **Tag padre:** tag del que otro hereda las restricciones.
- **PREREAD, PREWRITE, POSTREAD, POSTWRITE:** flags de sync; véase la sección 4.
- **Scatter-gather:** mapeo de múltiples segmentos discontinuos como una única transferencia lógica.
- **Segment:** véase `bus_dma_segment_t`.
- **Transacción estática:** véase la sección 2.
- **Sync:** `bus_dmamap_sync`; el traspaso de la propiedad de un buffer entre el CPU y el dispositivo.



## Referencia: nota final sobre la filosofía del DMA

Un párrafo para cerrar el capítulo.

El DMA es la primitiva que convierte un driver de un controlador byte a byte en un subsistema de movimiento de datos. Antes del DMA, cada byte de datos que manejaba el driver pasaba por las manos del CPU, una transacción MMIO a la vez; después del DMA, el CPU solo se encarga de la política y la contabilidad, y es el dispositivo quien mueve los bytes. La diferencia es la que hay entre un driver capaz de seguir el ritmo de una línea de 10 Mbit y uno capaz de seguirlo en una línea de 100 Gbit.

La lección del capítulo 21 es que el DMA es disciplinado, no mágico. La API `bus_dma(9)` oculta tres realidades del hardware (límites de direccionamiento, remapeo IOMMU, coherencia de caché) detrás de un único conjunto de llamadas, y esas llamadas siguen un patrón predecible: crear un tag, asignar memoria, cargar un map, hacer sync PRE, activar el dispositivo, esperar, hacer sync POST, leer el resultado y, finalmente, descargar y liberar. El patrón es el mismo en todos los drivers con capacidad DMA de FreeBSD; interiorizarlo una vez rinde dividendos a lo largo de docenas de capítulos posteriores y miles de líneas de código real de drivers.

Para este lector y para los futuros lectores de este libro, el patrón DMA del Capítulo 21 es una parte permanente de la arquitectura del driver `myfirst` y una herramienta permanente en el arsenal del lector. El Capítulo 22 lo da por sentado: suspend necesita detener el DMA; resume necesita reinicializarlo. Los capítulos de redes de la Parte 6 también lo asumen: cada ruta de paquetes utiliza DMA. Los capítulos de rendimiento de la Parte 7 (Capítulo 33) también lo asumen: cada medición de ajuste tiene como referencia el throughput DMA.

El vocabulario es el vocabulario que comparte todo driver de FreeBSD de alto rendimiento; los patrones son los patrones por los que se rigen los drivers de producción; la disciplina es la disciplina que mantiene coherentes las plataformas coherentes y correctas las plataformas no coherentes.

La habilidad que enseña el Capítulo 21 no es «cómo configurar un único buffer DMA de 4 KB». Es «cómo pensar en la propiedad de la memoria entre el CPU y el dispositivo, cómo describir las restricciones de un dispositivo al kernel, y cómo mover datos bajo una disciplina de sincronización que funcione de forma portable en todas las plataformas que FreeBSD soporta». Esa habilidad es aplicable a cualquier dispositivo con capacidad DMA con el que el lector trabaje en el futuro.
