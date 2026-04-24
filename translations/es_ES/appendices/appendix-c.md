---
title: "Conceptos de hardware para desarrolladores de drivers"
description: "Una guía de referencia conceptual sobre memoria, buses, interrupciones y DMA que aparecen de forma recurrente en el desarrollo de drivers de dispositivo para FreeBSD."
appendix: "C"
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 30
language: "es-ES"
---
# Apéndice C: Conceptos de hardware para desarrolladores de drivers

## Cómo usar este apéndice

Los capítulos principales enseñan el lado del driver en FreeBSD: cómo escribir un softc, cómo declarar un `cdev`, cómo configurar una interrupción, cómo mapear un BAR de PCI en un handle `bus_space`, cómo domar un motor DMA con `bus_dma(9)`. Debajo de cada uno de esos temas hay una capa silenciosa de conocimiento de hardware que el libro utiliza constantemente sin siempre nombrarla. Qué es realmente una dirección física. Por qué la CPU no puede simplemente leer un registro de un dispositivo a través de un puntero ordinario. Por qué PCI e I2C se sienten como mundos completamente diferentes aunque ambos transportan bytes entre una CPU y un periférico. Por qué una interrupción no es una llamada a función. Por qué DMA cambia la forma en que escribes un driver de arriba abajo.

Este apéndice es esa capa silenciosa, puesta por escrito. Es una guía de conceptos, no un libro de texto de electrónica. Está diseñado en función de lo que un autor de drivers realmente necesita creer sobre la máquina para escribir código correcto, y se detiene deliberadamente antes de llegar a la enseñanza completa de arquitectura de computadores. Si alguna vez has escrito una llamada a `bus_space_read_4` y te has preguntado qué representan realmente el tag y el handle, o has configurado un vector MSI-X y te has preguntado qué se está señalizando a través de qué cables, o has depurado una transferencia DMA que funcionaba en tu escritorio y se rompía en otra placa, estás en el lugar correcto.

### Qué encontrarás aquí

El apéndice está organizado por relevancia para el driver, no por taxonomía abstracta de hardware. Cada tema sigue el mismo ritmo compacto:

- **Qué es.** Una o dos frases de definición sencilla.
- **Por qué esto importa a los autores de drivers.** El lugar concreto donde el concepto aparece en tu código.
- **Cómo aparece en FreeBSD.** La API, la cabecera o la convención que nombra la idea.
- **Trampa habitual.** El malentendido que realmente le cuesta tiempo a la gente.
- **Dónde lo enseña el libro.** Una referencia al capítulo que lo usa en contexto.
- **Qué leer a continuación.** Una página de manual, una cabecera o un driver real que puedes abrir.

No todos los temas usan todas las etiquetas; la estructura es una guía, no una plantilla.

### Qué no es este apéndice

No es un primer curso de electrónica. Da por supuesto que puedes aceptar "un cable lleva una señal" y seguir adelante. Tampoco es un libro de texto de arquitectura de computadores; no hay discusión sobre pipelines, cachés más allá de lo que un autor de drivers debe saber, ni predicción de ramas. No es un sustituto de los capítulos especializados: el Capítulo 16 enseña `bus_space(9)`, el Capítulo 18 enseña PCI, el Capítulo 19 enseña las interrupciones, el Capítulo 20 enseña MSI y MSI-X, y el Capítulo 21 enseña DMA en profundidad. Este apéndice es lo que mantienes abierto junto a esos capítulos cuando quieres un anclaje al modelo mental en lugar de un recorrido completo.

Tampoco se solapa con los demás apéndices. El Apéndice A es la referencia de la API; el Apéndice B es la guía de campo de patrones algorítmicos; el Apéndice D es el complemento de conceptos de sistemas operativos; el Apéndice E es la referencia de subsistemas del kernel. Si la pregunta que quieres responder es "qué hace esta llamada a macro" o "qué algoritmo encaja con este problema" o "cómo planifica el planificador", necesitas un apéndice diferente.

## Orientación para el lector

Hay tres formas de usar este apéndice, cada una con una estrategia diferente.

Si estás **aprendiendo los capítulos principales**, abre el apéndice junto a ellos. Cuando el Capítulo 16 mencione las barreras de memoria, busca la sección de memoria aquí. Cuando el Capítulo 18 hable de los BAR, busca la sección de PCI. Cuando el Capítulo 19 introduzca el disparo por flanco frente al disparo por nivel, busca la sección de interrupciones. El apéndice está diseñado para ser un compañero del modelo mental, no una lectura lineal. Treinta minutos de principio a fin es un tiempo realista para una primera pasada; unos pocos minutos cada vez es lo más habitual en el uso diario.

Si estás **leyendo código de drivers reales**, usa el apéndice como traductor de terminología desconocida. Cuando el comentario de cabecera diga "memoria DMA coherente" o "interrupción por mensaje" o "BAR en el desplazamiento 0x10", encuentra el concepto aquí y sigue adelante. La comprensión completa puede llegar más tarde; el primer objetivo es formarse una imagen mental de lo que hace el driver frente a qué tipo de hardware.

Si estás **diseñando un nuevo driver**, lee las secciones que corresponden a tu hardware. Una NIC PCI tocará el modelo de memoria, PCI, interrupciones (probablemente MSI-X) y DMA, así que esas cuatro secciones son tu calentamiento. Un sensor I2C tocará la sección de I2C, la sección de interrupciones y casi nada más. Un driver embebido en un system-on-chip tocará todo excepto PCI. Haz coincidir el hardware con las secciones y lee solo esas.

A lo largo del apéndice se aplican algunas convenciones:

- Las rutas del código fuente se muestran en la forma orientada al lector, `/usr/src/sys/...`, que coincide con un sistema FreeBSD estándar. Puedes abrir cualquiera de ellas en tu máquina de laboratorio.
- Las páginas de manual se citan con el estilo habitual de FreeBSD. Las páginas orientadas al kernel están en la sección 9: `bus_space(9)`, `bus_dma(9)`, `pci(9)`, `malloc(9)`, `intr_event(9)`. Las descripciones generales de dispositivos suelen estar en la sección 4: `pci(4)`, `usb(4)`, `iicbus(4)`.
- Cuando una entrada señala código fuente real como material de lectura, el archivo es uno que un principiante puede recorrer en una sola sesión. Existen subsistemas más grandes que también usan el patrón; estos solo se mencionan cuando son el ejemplo canónico.

Con eso en mente, empezamos con la memoria: el único concepto que separa un driver que funciona de uno desconcertante.

## La memoria en el kernel

Un driver que malentiende la memoria escribirá código que parece perfecto pero que falla en hardware real. La memoria en el kernel no es lo mismo que la memoria en un tutorial de C. Al menos hay dos espacios de direcciones en juego (físico y virtual), existen restricciones que el hardware impone (límites de direccionamiento DMA, límites de página) y hay regiones donde incluso una lectura ordinaria tiene efectos secundarios (los registros de dispositivo). Esta sección construye el modelo mental del que dependen todos los temas posteriores del apéndice.

### Memoria física frente a memoria virtual

**Qué es.** Una dirección física es el número que usa el controlador de memoria para seleccionar una ubicación real en la RAM, o el número que usa la estructura de I/O para seleccionar un registro en un periférico. Una dirección virtual es el número que ve un thread en ejecución cuando desreferencia un puntero; la unidad de gestión de memoria (MMU) lo traduce a una dirección física al vuelo, usando las tablas de páginas que mantiene el kernel.

**Por qué esto importa a los autores de drivers.** Cada puntero de tu driver es virtual. La CPU nunca ve una dirección física cuando ejecuta tu código C. El kernel y la capa `pmap(9)` mantienen las tablas de páginas para que las direcciones virtuales del kernel apunten adonde el kernel haya decidido colocar las páginas correspondientes en la memoria física. Un driver que "conoce" una dirección física no puede simplemente hacer un cast a `(void *)` y desreferenciarla; puede que la traducción no exista, que los permisos lo prohíban o que los atributos sean incorrectos para el tipo de acceso que necesitas.

El hardware está en el lado opuesto del espejo. Un dispositivo periférico vive en algún lugar del espacio de direcciones físicas (sus registros son direcciones físicas y, cuando hace DMA, emite direcciones físicas en el bus). Un dispositivo nunca desreferencia un puntero virtual. Si tu driver le pasa al dispositivo una dirección virtual del kernel y espera que lea los datos allí, el dispositivo leerá alguna ubicación física no relacionada y pasarás una tarde larga intentando entender por qué.

**Cómo aparece en FreeBSD.** En dos lugares. Primero, `bus_space(9)` codifica la traducción para el acceso a registros: un tag describe el tipo de espacio de direcciones físicas, un handle describe el mapeo virtual que el kernel ha establecido, y los accesores `bus_space_read_*` y `bus_space_write_*` usan el handle para llegar al dispositivo a través de ese mapeo. Segundo, `bus_dma(9)` media el DMA: la llamada a `bus_dmamap_load` recorre un buffer y produce la lista de direcciones de bus que debe usar el dispositivo, que no son necesariamente las mismas que las direcciones virtuales ni las físicas, porque puede haber un IOMMU en medio.

**Trampa habitual.** Hacer un cast de una dirección física devuelta por el firmware o un registro BAR a un puntero y leer desde él directamente. En algunas arquitecturas y algunas compilaciones esto compila e incluso puede devolver algo, pero nunca es correcto. Mapea siempre a través de `bus_space`, `pmap_mapdev` o una API establecida de FreeBSD.

**Dónde lo enseña el libro.** El Capítulo 16 establece la distinción y muestra por qué la aritmética de punteros en bruto no es suficiente para la memoria de dispositivo. El Capítulo 21 vuelve a ello cuando DMA hace visible el lado físico.

**Qué leer a continuación.** `bus_space(9)`, `pmap(9)` y las primeras partes de `/usr/src/sys/kern/subr_bus_dma.c`.

### La MMU, las tablas de páginas y los mapeos de dispositivo

**Qué es.** La MMU es la unidad de hardware que traduce direcciones virtuales a direcciones físicas usando un árbol de tablas de páginas. Cada entrada de tabla de páginas contiene un número de página física y un pequeño conjunto de bits de atributo: legible, escribible, ejecutable, cacheable, accesible por el usuario. La misma página física puede estar mapeada en varias direcciones virtuales con atributos diferentes; la misma dirección virtual puede apuntar a diferentes páginas físicas en diferentes momentos.

**Por qué esto importa a los autores de drivers.** La memoria de dispositivo no es RAM ordinaria, y la CPU debe saberlo. Un registro puede cambiar por sí solo (un bit de estado que se activa cuando el dispositivo termina una transferencia), puede tener efectos secundarios al leerlo (un indicador que se borra con la lectura) y puede requerir que las escrituras se hagan visibles en un orden específico. Si la CPU almacena en caché un registro de dispositivo del mismo modo que un entero en RAM, el código lee un valor obsoleto para siempre. La MMU soluciona esto permitiendo que el kernel mapee las regiones de dispositivo con atributos diferentes: habitualmente sin caché, con orden estricto y a veces con escritura combinada. Normalmente un driver no elige esos atributos a mano. `bus_space` y `bus_alloc_resource` lo hacen en tu nombre.

**Cómo aparece en FreeBSD.** Cuando `bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE)` retorna, el kernel ya ha mapeado la región del dispositivo con los atributos correctos para MMIO. El `struct resource *` que obtienes lleva un par `bus_space_tag_t` y `bus_space_handle_t` que usan los accesores. Nunca debes manipular la tabla de páginas subyacente tú mismo. En la práctica, el acceso son dos líneas:

```c
uint32_t val = bus_space_read_4(rman_get_bustag(res),
    rman_get_bushandle(res), STATUS_REG_OFFSET);
```

Cada llamada a `bus_space_read_4` / `bus_space_write_4` pasa por el mapeo MMIO que el kernel estableció para ti; sin desreferencia de puntero, sin carga desde caché.

**Trampa habitual.** Mezclar vistas cacheables y no cacheables de la misma región física. Un buffer DMA asignado con `bus_dmamem_alloc` y luego convertido a otro mapeo virtual te dará dos vistas incoherentes de los mismos bytes. Usa el mapeo que devolvió el asignador, no uno que construyas por tu cuenta.

**Dónde lo enseña el libro.** El Capítulo 16 toca la MMU indirectamente cuando explica por qué existe la abstracción `bus_space`. El Capítulo 21 lo hace explícito cuando la coherencia DMA se convierte en el tema.

**Qué leer a continuación.** `pmap(9)`, `bus_space(9)` y `/usr/src/sys/vm/vm_page.h` para la representación del kernel de una página física (`vm_page_t`).

### Por qué el hardware no puede ver simplemente los punteros del kernel

**Qué es.** Una forma breve de enunciar la consecuencia más importante de los dos apartados anteriores. El kernel vive en memoria virtual. Los periféricos viven en memoria física (y a veces en memoria virtual de I/O). Un puntero del kernel no tiene ningún significado para un dispositivo.

**Por qué esto importa a los autores de drivers.** Cada vez que le das a un dispositivo una dirección de memoria, debes darle una dirección de bus, no un puntero virtual. La dirección de bus es lo que ve el dispositivo; puede ser la dirección física, o puede ser una dirección virtual de I/O remapeada por un IOMMU. No la calculas tú mismo. La infraestructura DMA la calcula por ti cuando cargas un mapa.

**Cómo se manifiesta en FreeBSD.** `bus_dmamap_load` recibe un buffer virtual y produce entradas `bus_dma_segment_t` cuyo campo `ds_addr` es la dirección que el dispositivo debe usar. Copia ese valor en el descriptor que estás a punto de entregar al hardware. En el momento en que te veas tentado a pasar la salida de `vtophys` o el valor bruto de un BAR al dispositivo, detente y recurre a `bus_dma` en su lugar.

**Trampa habitual.** Escribir `(uint64_t)buffer` en un descriptor de dispositivo porque funciona en una prueba trivial sobre una sola máquina. Fallará en cualquier máquina donde la dirección del kernel no coincida con la dirección de bus, que es la situación en la mayoría de las máquinas reales.

**Dónde lo enseña el libro.** El capítulo 21. El capítulo completo está dedicado a respetar esta separación.

**Qué leer a continuación.** `bus_dma(9)`, y la sección sobre DMA más adelante.

### Asignación de memoria en el kernel en una página

El kernel ofrece tres formas principales de obtener memoria, cada una con un propósito diferente. Los drivers recurren a ellas constantemente, y elegir la incorrecta es un error clásico de principiante. El objetivo aquí es entender la diferencia conceptual; la referencia completa de la API está en el apéndice A.

- **`malloc(9)`** es el asignador de propósito general. Devuelve memoria virtual del kernel perfectamente válida para estructuras softc, listas, buffers de comandos para tu propia contabilidad y cualquier otra cosa que permanezca dentro de los datos del driver. No es automáticamente contigua en memoria física ni automáticamente apta para DMA. Úsalo para datos de control.

- **`uma(9)`** es el asignador por zonas. Es un cache rápido para objetos de tamaño fijo que el driver asigna y libera con frecuencia, como estructuras por petición o estado por paquete. Es una especialización de la asignación de propósito general; no cambia la historia de físico frente a virtual.

- **`contigmalloc(9)`** devuelve un rango físicamente contiguo, con opciones de alineación y un límite de dirección superior. Un driver lo llama directamente solo cuando realmente necesita un bloque físico contiguo y la capa `bus_dma` no es una opción mejor. En los drivers modernos, `bus_dma` casi siempre lo es. La forma de la llamada ya dice mucho por sí sola:

  ```c
  void *buf = contigmalloc(size, M_DEVBUF, M_WAITOK | M_ZERO,
      0, BUS_SPACE_MAXADDR, PAGE_SIZE, 0);
  ```

  Los argumentos `low`, `high`, `alignment` y `boundary` son exactamente las restricciones DMA que expresaría una llamada a `bus_dma_tag_create`, razón por la que `bus_dma(9)` es casi siempre el lugar más adecuado para codificarlas.

- **`bus_dmamem_alloc(9)`** es el asignador con soporte DMA. Devuelve un buffer utilizable para DMA bajo una etiqueta dada, correctamente alineado, opcionalmente coherente y emparejado con un `bus_dmamap_t` que puedes cargar. Esta es la llamada que necesitas en casi cualquier escenario DMA.

**Por qué esto importa a quienes escriben drivers.** La pregunta que debes hacerte siempre es "¿quién va a leer o escribir esta memoria?". Si solo la CPU la toca, `malloc(9)` es suficiente. Si el dispositivo necesita leerla o escribirla, quieres `bus_dmamem_alloc(9)` bajo una etiqueta `bus_dma(9)` que exprese las restricciones de direccionamiento del dispositivo. Mezclar ambas es el origen de todo un género de bugs en drivers.

**Trampa habitual.** Asignar un buffer con `malloc(9)`, pasar su dirección virtual al dispositivo y preguntarse por qué el dispositivo lee basura. La solución rara vez es parchear los síntomas; consiste en reescribir la asignación a través de `bus_dma(9)`.

**Dónde lo enseña el libro.** El capítulo 5 presenta `malloc(9)` en un contexto de C en el kernel, y los capítulos sobre drivers a partir del capítulo 7 lo usan repetidamente para la memoria de softc y contabilidad. El capítulo 21 presenta `bus_dmamem_alloc`. El apéndice A lista la API completa y los flags.

**Qué leer a continuación.** `malloc(9)`, `contigmalloc(9)`, `uma(9)`, `bus_dma(9)`.

### IOMMUs y direcciones de bus

**Qué es.** Un IOMMU es una unidad de hardware (a veces la unidad `amdvi` o `intel-iommu`; en ARM, la SMMU) que se sitúa entre los dispositivos y la memoria principal y traduce las direcciones que emiten los dispositivos en direcciones físicas. Es el análogo, en el lado del dispositivo, de la MMU.

**Por qué esto importa a quienes escriben drivers.** Cuando hay un IOMMU presente, la dirección que el dispositivo usa en el bus (una dirección de bus o dirección virtual de I/O) no es la misma que la dirección física de la memoria subyacente. El IOMMU permite que el kernel ofrezca a cada dispositivo una vista restringida de la memoria, lo que es beneficioso para la seguridad y el aislamiento, y permite al kernel usar scatter-gather donde el dispositivo, de otro modo, exigiría páginas físicas contiguas.

**Cómo aparece en FreeBSD.** De forma transparente, para la mayoría de los drivers. Cuando el kernel se construye con la opción `IOMMU` y uno de los backends de arquitectura (los drivers de Intel y AMD bajo `/usr/src/sys/x86/iommu/` en amd64, el backend SMMU bajo `/usr/src/sys/arm64/iommu/` en arm64), la capa `busdma_iommu` en `/usr/src/sys/dev/iommu/` hace de puente entre `bus_dma(9)` y el IOMMU automáticamente, sin modificar el código del driver. Sigues asignando una etiqueta, creando un mapa, cargando el mapa y leyendo el `ds_addr` de cada segmento; el número que obtienes es la dirección que debe usar el dispositivo, con o sin IOMMU. No necesitas saber si hay un IOMMU presente.

**Trampa habitual.** Asumir que `ds_addr` es una dirección física y registrarla como tal. En sistemas con IOMMU, el número es una dirección virtual de I/O y no coincidirá con nada de lo que veas en `/dev/mem`. El modelo mental correcto es "la dirección que usa el dispositivo", no "la dirección física".

**Dónde lo enseña el libro.** El capítulo 21 menciona brevemente la integración con IOMMU y se centra en el caso habitual en que `bus_dma` lo oculta.

**Qué leer a continuación.** `bus_dma(9)`, `/usr/src/sys/dev/iommu/busdma_iommu.c` para el código del puente, y `/usr/src/sys/x86/iommu/` o `/usr/src/sys/arm64/iommu/` para los backends por arquitectura.

### Barreras de memoria en un párrafo

En las CPU modernas, las operaciones de memoria pueden hacerse visibles para otros agentes (otras CPU, otros dispositivos) en un orden diferente al que el código las emitió. Para la RAM ordinaria, las primitivas de bloqueo del kernel te ocultan esto. Para los registros de dispositivo, en cambio, el orden importa: escribir en un registro "start" antes de escribir en el registro "data" produce un resultado diferente al orden inverso. El kernel proporciona barreras explícitas para ambos casos. Para el acceso a registros, `bus_space_barrier(tag, handle, offset, length, flags)` es la herramienta adecuada; para buffers DMA, `bus_dmamap_sync(tag, map, op)` es la herramienta adecuada. Volverás a encontrarte con ambas más adelante en este apéndice. Por ahora, interioriza la regla: cuando el orden de dos accesos importa al hardware, dilo explícitamente con una barrera. No confíes únicamente en el orden del código.

## Buses e interfaces

Un bus es un camino físico y lógico que transporta comandos, direcciones y datos entre una CPU y los periféricos. FreeBSD trata cada bus como un árbol especializado dentro del framework Newbus: el bus tiene un driver que enumera sus hijos, cada hijo es un `device_t` y cada hijo puede a su vez ser un bus para más hijos. El concepto de "un bus" es uniforme a nivel de Newbus. Los conceptos de "las reglas eléctricas, de protocolo y de enumeración de un bus concreto" no son uniformes en absoluto, y quien escribe un driver debe entender para cuál está escribiendo.

Esta sección proporciona los modelos mentales para los cuatro buses que aborda el libro: PCI (y PCIe), USB, I2C y SPI. Sus capítulos llegan más adelante; este es el vocabulario compartido que necesitas primero.

### PCI y PCIe en el nivel relevante para el driver

**Qué es.** PCI es el bus canónico para conectar periféricos a un complejo CPU. El PCI clásico era paralelo y compartido; el PCI Express moderno es un conjunto de líneas serie punto a punto que una malla de conmutadores organiza en árbol. Desde la perspectiva de un driver, la diferencia eléctrica es en gran medida invisible: sigues viendo un espacio de configuración, sigues viendo Base Address Registers, sigues viendo interrupciones y sigues escribiendo código de probe y attach. Salvo que se indique lo contrario, "PCI" en este libro significa "PCI y PCIe juntos".

Un dispositivo PCI se identifica mediante una tupla Bus:Device:Function (B:D:F) y un espacio de configuración que rellenan el firmware y el kernel. El espacio de configuración contiene los identificadores de fabricante y dispositivo, el código de clase que describe qué tipo de periférico es, los Base Address Registers (BARs) que describen las ventanas de memoria e I/O del dispositivo, y una lista enlazada de estructuras de capacidad que anuncian características opcionales como MSI, MSI-X y gestión de energía.

**Por qué esto importa a quienes escriben drivers.** Todo driver PCI hace más o menos el mismo baile. Lee `pci_get_vendor(dev)` y `pci_get_device(dev)` durante probe, compáralos con una tabla de dispositivos que el driver soporta y devuelve un valor `BUS_PROBE_*` si hay coincidencia. En attach, asigna cada BAR con `bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE)`, extrae el `bus_space_tag_t` y el `bus_space_handle_t` con `rman_get_bustag` y `rman_get_bushandle`, asigna la interrupción con `bus_alloc_resource_any(dev, SYS_RES_IRQ, ...)` (o `pci_alloc_msix`/`pci_alloc_msi` para interrupciones señaladas por mensaje), conecta el manejador con `bus_setup_intr` y ya estás en marcha.

Las capacidades son el punto donde el driver PCI se encuentra con las características de la familia de dispositivo concreta. `pci_find_cap(dev, PCIY_MSIX, &offset)` encuentra la capacidad MSI-X; `pci_find_extcap(dev, PCIZ_AER, &offset)` encuentra la capacidad extendida para Advanced Error Reporting. Un driver normalmente solo recorre las capacidades que le interesan.

**Cómo aparece en FreeBSD.** El driver del bus PCI vive bajo `/usr/src/sys/dev/pci/`. La cabecera clave es `/usr/src/sys/dev/pci/pcireg.h` para las constantes de registro y `/usr/src/sys/dev/pci/pcivar.h` para las funciones que llama un driver cliente. Todo driver PCI que escribas comienza con un `device_probe_t` y un `device_attach_t`, con `DRIVER_MODULE(name, pci, driver, 0, 0)` al final del archivo. Los dos últimos argumentos son el manejador de eventos por módulo y su argumento, que normalmente se dejan como `0, 0` (o `NULL, NULL`); la ranura `devclass_t` separada que mencionan libros más antiguos fue eliminada de la macro hace algunas versiones.

**Trampa habitual.** Olvidar llamar a `pci_enable_busmaster(dev)` antes de esperar que el dispositivo haga DMA. El chip puede estar en un estado silencioso donde el MMIO funciona y el DMA no hace nada sin avisar. Habilitar el bus-mastering en attach forma parte de la secuencia habitual.

**Dónde lo enseña el libro.** El capítulo 18 es el capítulo completo sobre escritura de drivers PCI. El capítulo 20 lo amplía con MSI-X.

**Qué leer a continuación.** `pci(4)`, `pci(9)`, y un ejemplo real compacto como `/usr/src/sys/dev/uart/uart_bus_pci.c`. El propio bus PCI está implementado en `/usr/src/sys/dev/pci/pci.c`.

### USB en el nivel relevante para el driver

**Qué es.** USB es un bus jerárquico controlado por el host, en el que un controlador host se comunica con los dispositivos a través de un árbol de hubs. A diferencia del PCI, el bus es estrictamente jerárquico y similar al sondeo: el host planifica cada transacción. Un dispositivo no "habla" en el bus sin que se lo pidan. Las clases de velocidad USB (baja, plena, alta, super) comparten el mismo modelo conceptual; los detalles de protocolo cambian.

Un dispositivo USB expone un conjunto de endpoints. Cada endpoint es un canal de datos unidireccional con un tipo de transferencia específico: control, bulk, interrupt o isochronous. El control es el canal de configuración que tiene todo dispositivo. El bulk ofrece alto rendimiento sin garantías de temporización. El interrupt es pequeño, periódico y de baja latencia (ratones, teclados, sensores). El isochronous es streaming en tiempo real (audio, vídeo) que sacrifica fiabilidad por temporización.

**Por qué esto importa a quienes escriben drivers.** Un driver USB de FreeBSD hace attach a un `device_t` cuyo padre es un bus USB, no un bus PCI. El modelo de identificación es diferente: en lugar de IDs de fabricante/dispositivo y códigos de clase en el espacio de configuración, un driver USB hace concordancia por USB Vendor ID (VID), Product ID (PID), clase de dispositivo, subclase y protocolo. La pila USB de FreeBSD te ofrece un probe estructurado contra esos campos. En tiempo de ejecución, un driver USB abre endpoints específicos a través del framework USB y emite transferencias cuyo ciclo de vida gestiona el controlador host; no escribes en registros como hace un driver PCI.

**Cómo aparece en FreeBSD.** La pila USB vive bajo `/usr/src/sys/dev/usb/`. Un driver de clase usa `usbd_transfer_setup` para describir los endpoints que desea, `usbd_transfer_start` para iniciar transferencias, y funciones de callback para gestionar la finalización. Las constantes de tipo de transferencia se encuentran en `/usr/src/sys/dev/usb/usb.h` (`UE_CONTROL`, `UE_ISOCHRONOUS`, `UE_BULK`, `UE_INTERRUPT`).

**Trampa habitual.** Pensar en términos de registros e interrupciones. Un driver de dispositivo USB está orientado a eventos alrededor de callbacks de finalización de transferencia, no alrededor de lecturas de registros de dispositivo. Una mentalidad orientada a registros lleva a luchar contra el framework en lugar de usarlo.

**Dónde lo enseña el libro.** El capítulo 26 es el capítulo completo sobre drivers USB y serie. Este apéndice es la introducción conceptual; el capítulo es donde vive tu primer driver USB.

**Qué leer a continuación.** `usb(4)`, `usbdi(9)`, y las cabeceras bajo `/usr/src/sys/dev/usb/`.

### I2C en el nivel relevante para el driver

**Qué es.** I2C (Inter-Integrated Circuit) es un bus lento de dos hilos con arquitectura maestro-esclavo, diseñado para conectar periféricos de bajo ancho de banda en una placa: sensores de temperatura, controladores de gestión de energía, EEPROMs y pequeñas pantallas. Cada esclavo tiene una dirección de siete o diez bits. El maestro inicia cada transacción enviando una condición de inicio, la dirección, los bytes de datos (en escritura o lectura) y, finalmente, una condición de parada.

Las velocidades habituales son 100 kHz (estándar), 400 kHz (rápido), 1 MHz (fast-plus) y variantes superiores en dispositivos más modernos. El bus admite varios maestros, aunque la mayoría de los sistemas embebidos usan uno solo; en ese caso, el driver solo tiene que gestionar la interacción maestro-esclavo.

**Por qué esto importa a los autores de drivers.** Un driver I2C en FreeBSD adopta dos variantes. Un driver *controlador* I2C implementa el lado maestro de un chip controlador específico y se registra en el árbol Newbus como un bus I2C. Un driver de *dispositivo* I2C es un cliente que usa la capa `iicbus` para hablar con un dispositivo esclavo sin conocer los detalles del controlador. La segunda variante es la que escribe la mayoría de los autores de drivers. Basta con llamar a `iicbus_transfer` para ejecutar una breve secuencia de operaciones `struct iic_msg`, y la capa `iicbus` se encarga del arbitraje y la temporización.

En el driver cliente no hay interrupciones ni BARs que asignar. No hay DMA. Cada transferencia es corta y bloqueante, de unos pocos bytes como máximo; las lecturas largas se dividen en una serie de transferencias.

**Cómo aparece esto en FreeBSD.** El framework reside en `/usr/src/sys/dev/iicbus/`. La interfaz cliente gira en torno a `iicbus_transfer(dev, msgs, nmsgs)` y el tipo `struct iic_msg`. FreeBSD descubre los buses I2C a través de FDT en plataformas embebidas y mediante ACPI en portátiles x86 que exponen un controlador I2C.

**Trampa habitual.** Escribir un driver I2C que realice espera activa. Incluso a 1 MHz, una transferencia dura varias decenas de microsegundos; un driver que mantiene la CPU ocupada en un bucle de sondeo malgasta recursos. La capa `iicbus` gestiona la espera por ti.

**Dónde lo enseña el libro.** El libro no dedica un capítulo completo a I2C; este apéndice es el tratamiento conceptual principal, y el capítulo sobre sistemas embebidos (Capítulo 32) vuelve sobre el tema en el contexto de los device-tree bindings. Para un cliente concreto que puedas leer de principio a fin, abre `/usr/src/sys/dev/iicbus/icee.c`.

**Qué leer a continuación.** `iicbus(4)`, `/usr/src/sys/dev/iicbus/iicbus.h`, y un cliente de ejemplo como `/usr/src/sys/dev/iicbus/icee.c` (un driver de EEPROM).

### SPI a nivel relevante para el driver

**Qué es.** SPI (Serial Peripheral Interface) es un bus simple, rápido, full-duplex y maestro-esclavo. No tiene direccionamiento en el propio bus; cada esclavo dispone de una línea chip-select dedicada, y el maestro activa esa línea para iniciar una transacción. Cuatro hilos son los típicos: SCLK (reloj), MOSI (master-out slave-in), MISO (master-in slave-out), y SS/CS (slave-select). Las velocidades van habitualmente desde unos pocos MHz hasta decenas de MHz.

A diferencia del I2C, SPI no tiene protocolo más allá del eléctrico: el maestro desplaza bits por MOSI mientras simultáneamente desplaza bits de entrada por MISO. Lo que significan esos bits depende enteramente del dispositivo.

**Por qué esto importa a quienes escriben drivers.** Al igual que con I2C, FreeBSD separa el controlador del cliente. Un driver de controlador sabe cómo se programa el maestro SPI en un chip concreto; un driver cliente utiliza la interfaz `spibus` para enviar y recibir bytes sin necesidad de conocer el controlador. El cliente llama a `spibus_transfer` o `spibus_transfer_ext` con un `struct spi_command` que describe una única transacción, incluidos el chip-select a activar y los buffers a desplazar.

**Cómo aparece esto en FreeBSD.** El framework reside bajo `/usr/src/sys/dev/spibus/`. El driver cliente típico cuelga bajo un padre `spibus` en Newbus y utiliza la misma estructura probe/attach que cualquier otro dispositivo. `/usr/src/sys/dev/spibus/spigen.c` es un cliente genérico de dispositivo de caracteres que expone SPI en crudo al userland; leerlo es una buena forma de ver el lado cliente del framework.

**Error frecuente.** Esperar que SPI transporte datos con algún tipo de encuadrado incorporado. No lo hace. La hoja de datos del dispositivo indica qué significan los bytes; el bus no dice nada.

**Dónde lo enseña el libro.** El libro no dedica un capítulo completo a SPI; este apéndice es el tratamiento conceptual principal, y el capítulo sobre hardware empotrado (capítulo 32) lo retoma en el contexto de los bindings del árbol de dispositivos. Como ejemplo concreto que puedes leer de principio a fin, abre `/usr/src/sys/dev/spibus/spigen.c`.

**Qué leer a continuación.** `/usr/src/sys/dev/spibus/spibus.c`, `/usr/src/sys/dev/spibus/spigen.c`, y la hoja de datos del dispositivo del periférico SPI que estés controlando.

### Una comparación compacta de los cuatro buses

Una pequeña referencia es a menudo más útil que una larga descripción. Esta tabla compara los buses en los ejes que realmente afectan al diseño del driver. No es una comparación eléctrica exhaustiva.

| Aspecto | PCI / PCIe | USB | I2C | SPI |
| :-- | :-- | :-- | :-- | :-- |
| Topología | Árbol (switches PCIe) | Árbol estricto host/hub | Multi-drop, direccionado | Multi-drop, chip-select |
| Velocidad típica | Gigabytes/s por carril | Kilobytes/s a gigabytes/s | 100 kHz - 1 MHz | 1 MHz - decenas de MHz |
| Identificación | Vendor/Device + clase | VID/PID + clase | Dirección de esclavo de 7 o 10 bits | Ninguna (chip-select) |
| Tramas del protocolo | PCI TLPs, invisible | Paquetes + tipos de transferencia | Start/address/data/stop | Bits en crudo |
| Interrupciones | INTx o MSI / MSI-X | Callbacks de transferencia | Poco habitual; el bus se sondea | Poco habitual; el bus se sondea |
| DMA | Sí, iniciado por el dispositivo | Lo gestiona el controlador host | No | No |
| Rol del driver | A nivel de registro | Cliente del framework | Cliente `iicbus` | Cliente `spibus` |
| Enumeración | Firmware + `pci(4)` | Descriptores de hub/dispositivo | FDT / ACPI / cableado fijo | FDT / ACPI / cableado fijo |

El patrón que debes retener es que PCI y USB son lo bastante complejos como para necesitar sus propias pilas de enumeración y maquinaria de transacciones, mientras que I2C y SPI son lo suficientemente simples como para que una breve struct de transacción constituya toda la API. La carga de trabajo de quien desarrolla drivers tiene un aspecto muy diferente a cada lado de esa división.

## Interrupciones

Una interrupción es la forma en que un dispositivo le indica al CPU que algo ha ocurrido. Sin interrupciones, los drivers tendrían que hacer sondeo (polling), consumiendo ciclos de CPU para descubrir eventos que el hardware ya conoce. Con interrupciones, el CPU realiza otro trabajo hasta que el hardware señaliza; entonces el kernel despacha un handler en un contexto estrecho y bien definido. Esta sección explica en qué consiste realmente esa señal, qué distingue las formas más comunes y por qué la disciplina en el uso de interrupciones es una de las habilidades más exigentes en el desarrollo de drivers.

### Qué es realmente una interrupción

**Qué es.** A nivel del hardware, una interrupción es una señal que el dispositivo eleva para solicitar la atención del CPU. En los buses paralelos clásicos, la señal es una línea que el dispositivo pone a nivel alto o bajo. En PCIe moderno, la señal es una transacción de escritura en memoria que transporta una pequeña carga útil que el controlador de interrupciones reconoce. En ambos casos, el controlador de interrupciones asigna la señal a un vector del CPU, el CPU guarda el estado mínimo necesario para cambiar de contexto, y el código de entrada de bajo nivel del kernel decide qué driver llamar.

Lo que el driver percibe es una llamada a función: el handler de interrupción se ejecuta con una pila pequeña, en un contexto de planificación especial, habitualmente con la preemption desactivada o muy restringida. El modelo hardware es "un dispositivo elevó una línea"; el modelo software es "el kernel ejecutó tu callback".

**Por qué esto importa a quienes escriben drivers.** Las interrupciones no son llamadas a función ordinarias. Ocurren de forma asíncrona, posiblemente en un CPU diferente al que está ejecutando tu driver, y pueden llegar en medio de casi cualquier ruta de código. Por eso los handlers de interrupción deben ser breves, evitar dormir y no acceder a estado compartido sin un lock que sea seguro en ese contexto. La mayor parte de la disciplina que un driver aprende sobre locking, ordenación y trabajo diferido existe a causa de este único hecho.

**Cómo aparece esto en FreeBSD.** Las interrupciones llegan a los drivers a través de `intr_event(9)`. Un driver llama a `bus_setup_intr(dev, irq_resource, flags, filter, ithread, arg, &cookie)` para registrar hasta dos callbacks: un *filter* que se ejecuta primero, en contexto de interrupción, con solo spin locks disponibles, y opcionalmente un *ithread* (thread de interrupción) que se ejecuta como thread del kernel y puede usar sleep locks. El valor de retorno del filter decide qué ocurre a continuación: `FILTER_HANDLED` (gestionado por completo, confirmar al controlador), `FILTER_STRAY` (no es mío, no hacer nada), o `FILTER_SCHEDULE_THREAD` (el ithread debe ejecutarse ahora). Esas constantes se encuentran en `/usr/src/sys/sys/bus.h`.

**Error frecuente.** Hacer trabajo real en el filter. El filter está ahí para decidir la pertenencia, confirmar el dispositivo rápidamente si es posible, y gestionar una cantidad de trabajo trivialmente pequeña o programar el thread. El procesamiento largo corresponde al ithread o a un taskqueue, no al filter.

**Dónde lo enseña el libro.** El capítulo 19 es el capítulo dedicado a las interrupciones. El capítulo 20 añade MSI y MSI-X. El capítulo 14 (taskqueues) explica cómo diferir el trabajo pesado fuera de la ruta de interrupción.

**Qué leer a continuación.** `intr_event(9)`, `bus_setup_intr(9)`, y la documentación de filter/ithread en `/usr/src/sys/sys/bus.h` en torno a las constantes `FILTER_` e `INTR_`.

### Disparado por flanco frente a disparado por nivel

**Qué es.** Dos formas en que la señal hardware puede indicar "hay algo nuevo".

- **Disparado por flanco.** El dispositivo genera una transición (un flanco ascendente o descendente) para señalizar un evento. Si el driver no registra que se produjo una interrupción, el evento se pierde. El flanco es un momento, no un estado.
- **Disparado por nivel.** El dispositivo mantiene la línea (o la señal) activa mientras persista la condición. El controlador de interrupciones sigue disparando hasta que el driver confirma el dispositivo y la línea se desactiva. El nivel es un estado, no un momento.

**Por qué esto importa a quienes escriben drivers.** Las interrupciones disparadas por flanco son inflexibles: si el driver tiene una condición de carrera y un segundo flanco llega mientras se procesa el primero sin haberlo registrado, el segundo evento se pierde. Las interrupciones disparadas por nivel se autocorrigen: si la línea sigue activa al retornar, el controlador disparará de nuevo. Las líneas INTx legacy de PCI están disparadas por nivel y son compartidas, razón por la cual los handlers de driver deben leer el registro de estado del dispositivo y comprobar si "esta interrupción" es realmente suya antes de reclamarla. MSI y MSI-X son efectivamente similares a los flancos: cada mensaje es un evento discreto.

**Cómo aparece esto en FreeBSD.** La distinción se gestiona principalmente por debajo de tu driver. El registro de INTx legacy con `SYS_RES_IRQ` y rid `0` te proporciona interrupciones disparadas por nivel y compartidas; `pci_alloc_msi` y `pci_alloc_msix` te dan interrupciones señalizadas por mensaje, que son por vector y no se comparten con drivers no relacionados. Tu filter sigue teniendo que comportarse correctamente, pero la preocupación por el uso compartido desaparece.

**Error frecuente.** Escribir un filter que devuelve `FILTER_HANDLED` sin comprobar realmente el dispositivo. Con INTx disparado por nivel, esto bloquea el sistema porque la línea permanece activa y el controlador dispara indefinidamente.

**Dónde lo enseña el libro.** El capítulo 19 enseña el caso clásico; el capítulo 20 enseña el caso de interrupciones señalizadas por mensaje.

**Qué leer a continuación.** La discusión sobre interrupciones legacy en `/usr/src/sys/dev/pci/pci.c` en torno a `pci_alloc_msi`, y los ejemplos de filter en el capítulo 19.

### MSI y MSI-X sin las siglas

**Qué es.** Interrupciones señalizadas por mensaje (Message-Signalled Interrupts). En lugar de activar una línea de interrupción compartida, un dispositivo PCI Express envía una pequeña transacción de escritura en memoria a una dirección especial que el controlador de interrupciones monitoriza. La carga útil de la escritura identifica qué interrupción se ha disparado. MSI admite un pequeño número de vectores por dispositivo (normalmente hasta 32); MSI-X admite miles, cada uno con su propia dirección y datos, y afinidad de CPU por vector.

**Por qué esto importa a quienes escriben drivers.** Dos cosas cambian cuando un driver usa MSI o MSI-X en lugar de INTx legacy. En primer lugar, las interrupciones ya no son compartidas, por lo que el filter no necesita coexistir con drivers no relacionados (aunque sigue teniendo que confirmar que el evento provino de su propio hardware). En segundo lugar, puedes tener más de una interrupción por dispositivo. Una NIC puede tener una interrupción por cola de recepción, otra por cola de transmisión, otra para la ruta de gestión, y puedes vincular cada una a un CPU diferente. Eso cambia cómo estructuras el driver: cada cola tiene su propia subestructura softc, su propio lock y su propio handler.

**Cómo aparece esto en FreeBSD.** `pci_msi_count(dev)` y `pci_msix_count(dev)` indican cuántos vectores están disponibles. `pci_alloc_msi(dev, &count)` y `pci_alloc_msix(dev, &count)` los reservan; la llamada actualiza `count` con el número realmente asignado. A continuación, asignas cada vector con `bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE)` usando `rid = 1, 2, ..., count`. La afinidad de CPU se establece con `bus_bind_intr(dev, res, cpu_id)`. El desmontaje sigue el orden inverso: `bus_teardown_intr` y `bus_release_resource` por vector, y luego un único `pci_release_msi(dev)`.

**Error frecuente.** Solicitar demasiados vectores y asumir que el sistema los proporcionará todos. `pci_alloc_msix` puede devolver menos de los solicitados, incluso en hardware que anuncia tener muchos; el driver debe adaptar su estructura de colas al número realmente asignado. Una escalera de alternativas (MSI-X primero, luego MSI y después INTx) es la forma estándar.

**Dónde lo enseña el libro.** El capítulo 20.

**Qué leer a continuación.** `pci(9)` para la lista de funciones, y `/usr/src/sys/dev/pci/pci.c` para la implementación.

### Por qué importa la disciplina en el uso de interrupciones

**Qué es.** El conjunto de reglas que mantienen un driver correcto bajo el efecto de las interrupciones. La mayor parte se deriva de tres hechos: el filter se ejecuta con muy pocas primitivas disponibles, el filter puede competir en condición de carrera con cualquier otra ruta del driver, y un reconocimiento tardío produce sistemas bloqueados.

**Reglas fundamentales.**

- Mantén el filter breve y sin asignaciones de memoria. Sin `malloc`, sin sleep, sin llamadas que puedan dormir, sin lógica compleja.
- Confirma el reconocimiento al dispositivo antes de devolver `FILTER_HANDLED`, o asegúrate de que el dispositivo siga activo hasta que se ejecute tu ithread.
- Usa spin locks (`MTX_SPIN`) para proteger el estado que el filter y una ruta de CPU comparten. Los sleep locks no pueden tomarse en el filter.
- Difiere el trabajo real a un ithread, un taskqueue o un callout. La función del filter es mover los datos fuera de la ruta crítica.
- Lleva la cuenta de las interrupciones. Un contador `sysctl` por vector es barato y suele ser la primera pista cuando el driver se comporta incorrectamente.

**Trampa habitual.** Mantener un sleep lock activo en un camino de código que podría volver a invocarse desde el filter. Un thread que posee un sleep mutex no puede ser desalojado por una interrupción en el mismo CPU; el diagnóstico `witness(9)` del kernel detectará muchos de estos casos antes de llegar a producción, pero solo si el escenario en cuestión se ejecuta realmente.

**Dónde lo trata este libro.** El Capítulo 11 presenta la disciplina de forma general; el Capítulo 19 la concreta para hardware real.

**Qué leer a continuación.** El resumen de locking del Apéndice A y la división filter/ithread en `/usr/src/sys/sys/bus.h`.

## DMA

DMA (Direct Memory Access) es el mecanismo por el que un periférico lee o escribe en memoria sin que la CPU copie cada byte. Existe porque la alternativa (que la CPU lea un registro, coloque el byte en RAM y repita el proceso en bucle) es demasiado lenta para cualquier dispositivo moderno. DMA es también la parte de un driver más ligada al hardware, porque es donde el driver debe hablar el lenguaje de direcciones del bus en lugar del lenguaje de punteros virtuales del kernel. Un driver que no sea disciplinado con DMA producirá corrupción de datos casi imposible de diagnosticar desde el espacio de usuario.

### Qué es DMA y por qué existe

**Qué es.** Una transferencia en la que el dispositivo lee o escribe en la memoria del sistema bajo su propio control, usando direcciones de bus que el driver le ha proporcionado con antelación. La CPU configura la transferencia y continúa con otras tareas; el dispositivo señaliza la finalización, normalmente elevando una interrupción. No hay intervención de la CPU byte a byte durante el movimiento de datos en sí.

**Por qué esto importa a los escritores de drivers.** Todo lo relacionado con cómo asignas, cargas, sincronizas y liberas memoria cambia en el momento en que DMA entra en escena. Ya no eres el único propietario del buffer; el dispositivo es quien lo posee durante la transferencia. Debes indicarle al kernel qué partes del buffer se están leyendo o escribiendo y cuándo, para que la coherencia de caché, la alineación y la reasignación de direcciones se gestionen correctamente.

**Cómo aparece esto en FreeBSD.** `bus_dma(9)` es toda la maquinaria. El flujo de trabajo del autor del driver es consistente en todos los dispositivos con capacidad DMA:

1. Crea un `bus_dma_tag_t` en `attach`, describiendo las restricciones de direccionamiento del dispositivo (alineación, límite, `lowaddr`, `highaddr`, tamaño máximo de segmento, número de segmentos, etc.). La etiqueta captura la verdad del dispositivo.
2. Asigna un buffer con capacidad DMA con `bus_dmamem_alloc(tag, &vaddr, flags, &map)`, o asigna un buffer ordinario y cárgalo después.
3. Carga el buffer con `bus_dmamap_load(tag, map, vaddr, length, callback, arg, flags)`. El callback recibe una o más entradas `bus_dma_segment_t` cuyos campos `ds_addr` son las direcciones de bus que se le pasan al dispositivo.
4. Antes de que el dispositivo lea, llama a `bus_dmamap_sync(tag, map, BUS_DMASYNC_PREWRITE)`. Antes de que la CPU lea lo que escribió el dispositivo, llama a `bus_dmamap_sync(tag, map, BUS_DMASYNC_POSTREAD)`. Las flags están definidas en `/usr/src/sys/sys/bus_dma.h`.
5. Cuando la transferencia se completa, `bus_dmamap_unload(tag, map)` libera el mapeo; `bus_dmamem_free(tag, vaddr, map)` libera el buffer; `bus_dma_tag_destroy(tag)` libera la etiqueta (normalmente en `detach`).

**Error frecuente.** Olvidar la sincronización `PREWRITE` o `POSTREAD`. En arquitecturas coherentes (amd64 con IOMMU, por ejemplo), el driver suele funcionar sin ella; en arquitecturas no coherentes (algunos sistemas ARM), corrompe los datos en silencio. Escribe siempre las llamadas de sincronización, incluso cuando estés desarrollando en una plataforma que perdona estos errores.

**Dónde lo enseña el libro.** El capítulo 21 es el tratamiento completo y dedicado.

**Qué leer a continuación.** `bus_dma(9)` y `/usr/src/sys/kern/subr_bus_dma.c`.

### El modelo mental: etiquetas, mapas, segmentos y sincronización

Un breve repaso con forma de diagrama. Las piezas encajan así:

```text
+----------------------------------------------------------+
| Device constraints  ->  bus_dma_tag_t     (made once)    |
+----------------------------------------------------------+
         |
         v
+----------------------------------------------------------+
| Buffer in kernel memory  ->  virtual address             |
+----------------------------------------------------------+
         |
         v  bus_dmamap_load()
+----------------------------------------------------------+
| bus_dmamap_t  ->  one or more bus_dma_segment_t entries  |
|                    (ds_addr, ds_len)                     |
+----------------------------------------------------------+
         |
         v  bus_dmamap_sync(PREWRITE) / PREREAD
+----------------------------------------------------------+
| Device reads or writes memory at ds_addr                 |
+----------------------------------------------------------+
         |
         v  bus_dmamap_sync(POSTWRITE) / POSTREAD
+----------------------------------------------------------+
| CPU reads the buffer; kernel knows ordering is valid     |
+----------------------------------------------------------+
```

Cada flecha es una responsabilidad. La etiqueta expresa lo que el dispositivo puede tolerar. El mapa expresa un conjunto concreto de segmentos que el dispositivo tocará realmente. Las llamadas de sincronización delimitan el uso que el dispositivo hace del buffer. Si falta cualquiera de ellas, la transferencia queda indefinida.

### Por qué la sincronización y el mapeo son importantes

**Qué es.** Dos preocupaciones que juntas explican por qué `bus_dma` no es simplemente un traductor de direcciones.

- **Mapeo.** El dispositivo necesita una dirección de bus, no una virtual. En sistemas con IOMMU, el mapeo es un rango I/O-virtual real; en sistemas sin IOMMU, es la dirección física, posiblemente a través de un bounce buffer si el dispositivo no puede acceder a donde reside realmente la memoria. El driver no elige cuál; `bus_dma` elige en función de la etiqueta.
- **Sincronización.** La CPU tiene cachés. El dispositivo puede tener las suyas propias. El kernel tiene reglas de ordenación. Las llamadas de sincronización traducen la intención del driver ("voy a entregar este buffer al dispositivo para que lo lea") en los vaciados de caché, invalidaciones o barreras de memoria que requiera la arquitectura.

**Por qué esto importa a los escritores de drivers.** Omitir la sincronización es la fuente más común de corrupción sutil de datos en un driver DMA. El código parece funcionar porque otra actividad vacía por casualidad las cachés de la CPU; luego una actualización del kernel cambia ligeramente el comportamiento de la caché y el driver empieza a fallar. Escribir siempre cada llamada de sincronización es el único hábito fiable.

**Cómo aparece esto en FreeBSD.** Las cuatro flags de sincronización son `BUS_DMASYNC_PREREAD`, `BUS_DMASYNC_PREWRITE`, `BUS_DMASYNC_POSTREAD` y `BUS_DMASYNC_POSTWRITE` (valores 1, 4, 2, 8 en `/usr/src/sys/sys/bus_dma.h`). "PRE" se ejecuta antes de que el dispositivo toque el buffer; "POST" se ejecuta después. Los componentes "READ" y "WRITE" siguen la terminología de `bus_dma(9)`: `PREREAD` significa "el dispositivo está a punto de actualizar la memoria del host, y la CPU leerá después lo que se escribió"; `PREWRITE` significa "la CPU ha actualizado la memoria del host, y el dispositivo está a punto de acceder a ella". En el par de direcciones sobre el que suele razonar un driver, `PREWRITE` cubre el caso en que la CPU escribe el buffer y luego el dispositivo lo lee, y `PREREAD` cubre el caso en que el dispositivo escribe el buffer y luego la CPU lo lee.

**Error frecuente.** Interpretar los nombres de las flags como si describieran la acción del propio dispositivo. Si el dispositivo está a punto de escribir en memoria, y la CPU leerá después lo que el dispositivo escribió, debes llamar primero a `BUS_DMASYNC_PREREAD` (porque la CPU leerá esa memoria más adelante), no a `BUS_DMASYNC_PREWRITE`. Los pares de flags siguen la operación de memoria en la que participa la CPU, no la dirección en que el dispositivo mueve los datos por el bus.

**Dónde lo enseña el libro.** El capítulo 21 dedica un espacio considerable a la semántica de sincronización, porque es fácil equivocarse en ella.

**Qué leer a continuación.** `bus_dma(9)` y los comentarios de cabecera en `/usr/src/sys/sys/bus_dma.h`.

### Buffers DMA frente a buffers de control

**Qué es.** Una distinción final que mantiene en sintonía la sección de memoria y la sección de DMA. No todos los buffers de un driver con capacidad DMA son buffers DMA.

- **Los buffers DMA** son aquellos de los que el dispositivo lee o en los que escribe. Pasan por `bus_dma(9)`: se asignan con `bus_dmamem_alloc` o `malloc` más `bus_dmamap_load`, se cargan, se sincronizan y se descargan.
- **Los buffers de control** son aquellos que solo toca la CPU: índices de anillo, contabilidad por petición, estructuras de comando que el driver inspecciona pero el hardware no. Residen en memoria ordinaria de `malloc(9)`.

**Por qué esto importa a los escritores de drivers.** Ambos tipos de buffer suelen coexistir en el mismo driver, a menudo uno junto al otro dentro del mismo softc. Mantenerlos bien diferenciados hace que el código sea mucho más fácil de revisar. Un puntero que el dispositivo lee debe etiquetarse y asignarse sin ambigüedad; un puntero que el dispositivo nunca ve debe etiquetarse y asignarse con la misma claridad. Confundir ambos tipos suele producir errores difíciles de reproducir.

**Dónde lo enseña el libro.** El capítulo 21 retoma esta distinción cuando recorre un driver completo de anillo de descriptores.

## Tablas de referencia rápida

Las tablas compactas que siguen están pensadas para una consulta rápida. No reemplazan las secciones anteriores; te ayudan a localizar la sección adecuada con rapidez.

### Espacios de direcciones de un vistazo

| Tienes... | Reside en... | Cómo lo obtienes | Quién puede desreferenciarlo |
| :-- | :-- | :-- | :-- |
| Un puntero C en código del kernel | Memoria virtual del kernel | `malloc`, `uma_zalloc`, stack, softc | CPU |
| Un registro de dispositivo | Espacio físico de I/O | `bus_alloc_resource(SYS_RES_MEMORY,...)` + `bus_space_handle_t` | CPU mediante `bus_space_*` |
| Un buffer DMA | Virtual del kernel, más una vista de bus | `bus_dmamem_alloc` | CPU mediante virtual, dispositivo mediante `ds_addr` |
| Una dirección de bus en el lado del dispositivo | Espacio de direcciones de bus (posiblemente mapeado con IOMMU) | Callback de `bus_dmamap_load` | Solo el dispositivo |

### Cuándo usar cada asignador

| Propósito | Llamada |
| :-- | :-- |
| softc, listas, estructuras de control | `malloc(9)` con `M_DRIVER_NAME` |
| Objetos de tamaño fijo frecuentes | Zona `uma(9)` |
| Buffer DMA con restricciones de etiqueta | `bus_dmamem_alloc(9)` bajo una etiqueta `bus_dma` |
| Bloque físicamente contiguo sin etiqueta DMA | `contigmalloc(9)` (raramente) |
| Variable temporal en pila, pequeña y acotada | Pila C ordinaria |

### Interrupción frente a polling

| Debes usar... | Cuando... |
| :-- | :-- |
| Una interrupción hardware | La tasa de eventos es moderada y la latencia importa |
| Polling dentro de un ithread | La tasa de eventos es muy alta y las interrupciones dominarían |
| Un taskqueue activado por interrupciones | El trabajo es intenso y no puede ejecutarse en el filtro |
| Un temporizador (callout) más interrupciones ocasionales | El dispositivo es lento y el estado es sencillo |

### El bus de un vistazo (perspectiva del escritor de drivers)

| Bus | Forma del driver | Identificación | Llamada principal a la API |
| :-- | :-- | :-- | :-- |
| PCI / PCIe | Driver Newbus completo con registros | ID de fabricante/dispositivo + clase | `bus_alloc_resource_any`, `bus_setup_intr` |
| USB | Driver de clase del framework | VID/PID + clase/subclase/protocolo | `usbd_transfer_setup`, `usbd_transfer_start` |
| I2C | Cliente `iicbus` | Dirección esclava | `iicbus_transfer` |
| SPI | Cliente `spibus` | Línea chip-select | `spibus_transfer` |

### Comprobación rápida de sincronización DMA

| Estás a punto de... | Llama a esto |
| :-- | :-- |
| Entregar un buffer al dispositivo para que el dispositivo lo *lea* | `bus_dmamap_sync(..., BUS_DMASYNC_PREWRITE)` |
| Entregar un buffer al dispositivo para que el dispositivo *escriba* en él | `bus_dmamap_sync(..., BUS_DMASYNC_PREREAD)` |
| Leer de un buffer en el que el dispositivo acaba de escribir | `bus_dmamap_sync(..., BUS_DMASYNC_POSTREAD)` |
| Reutilizar un buffer después de que el dispositivo lo haya leído | `bus_dmamap_sync(..., BUS_DMASYNC_POSTWRITE)` |

Las llamadas "PRE" establecen una barrera para las escrituras de la CPU o las líneas de caché antes de que el dispositivo examine el buffer. Las llamadas "POST" establecen una barrera para la finalización del dispositivo antes de que la CPU examine lo que ha cambiado.

## En resumen: cómo mantener la relación entre los conceptos de hardware y el código del driver

Los conceptos de hardware de este apéndice no son un tema separado de la escritura de drivers. Todos ellos aparecen en el código. La secuencia que utilizarás, una y otra vez, es siempre la misma:

1. El dispositivo existe en el espacio de direcciones físico. Sus registros están en direcciones físicas.
2. El kernel le proporciona al driver un handle `bus_space` que mapea esos registros con los atributos adecuados.
3. El dispositivo eleva interrupciones ante eventos, y el driver las gestiona a través de un filtro (rápido, pequeño, seguro con spinlock) y opcionalmente un ithread (más lento, seguro con sleep-lock).
4. Cuando el dispositivo necesita mover datos, el driver usa `bus_dma(9)` para producir direcciones de bus que el dispositivo pueda usar, y enmarca cada transferencia con llamadas de sincronización.
5. El driver nunca le pasa al dispositivo un puntero virtual del kernel, y nunca trata un `ds_addr` como si fuera una dirección física que la CPU puede leer directamente.

Si mantienes esa secuencia en mente, los conceptos de hardware dejan de ser un tema separado y se convierten en un comentario continuo sobre el código que estás escribiendo. Cuando estés a punto de convertir un puntero y pasárselo al hardware, detente y traduce la acción a la secuencia: en qué espacio de direcciones está esto, quién va a leerlo, qué atributos necesita esa vista. La secuencia nunca cambia. Los detalles cambian con cada familia de dispositivos.

Tres hábitos que refuerzan el modelo.

El primero es leer el comentario de cabecera de un driver real antes de leer su código. La mayoría de los drivers de FreeBSD comienzan con un bloque que explica la disciplina de locking, la estructura MSI-X o la disposición del anillo. Ese comentario está ahí porque el código por sí solo no puede comunicarlo. Cuando el comentario dice "el anillo de recepción usa el lock por cola; el filtro confirma el dispositivo y planifica un taskqueue", léelo con atención. Es un mapa condensado del puente hardware-software para ese driver.

El segundo consiste en identificar el bus desde el primer momento. Cuando leas un driver desconocido, busca la línea `DRIVER_MODULE`. El nombre del bus en esa línea (`pci`, `usb`, `iicbus`, `spibus`, `acpi`, `simplebus`) te indica qué modelo de hardware aplica antes de que leas una sola función. Un driver conectado a `pci` está orientado a registros e impulsado por interrupciones; un driver conectado a `iicbus` está orientado a transacciones y se sondea a nivel del bus. El mismo código C se lee de manera distinta cuando sabes qué bus hay detrás.

El tercero consiste en mantener una hoja de referencia breve junto a tus notas sobre el driver. Los archivos en `examples/appendices/appendix-c-hardware-concepts-for-driver-developers/` están pensados exactamente para eso. Una tabla comparativa de buses que puedes consultar de un vistazo cuando abres un nuevo datasheet. Una lista de verificación del modelo mental de DMA que puedes recorrer cuando sospechas que falta una sincronización. Un diagrama de físico frente a virtual que puedes anotar con el par concreto de tag y handle con el que estés trabajando en ese momento. La enseñanza está en este apéndice; la aplicación está en esas hojas.

Con esto, la parte de hardware del libro tiene ya un lugar consolidado. Los capítulos siguen enseñando; el apéndice sigue nombrando; los ejemplos siguen recordando. Cuando el lector cierra este apéndice y abre un driver real, el vocabulario ya está listo.
