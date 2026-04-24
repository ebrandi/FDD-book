---
title: "Navegando por las entrañas del kernel de FreeBSD"
description: "Un mapa orientado a la navegación de los subsistemas del kernel de FreeBSD que rodean el trabajo de desarrollo de drivers, con las estructuras, las ubicaciones en el árbol de código fuente y los puntos de contacto del driver que ayudan al lector a orientarse rápidamente."
appendix: "E"
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 40
language: "es-ES"
---
# Apéndice E: Navegando por los componentes internos del kernel de FreeBSD

## Cómo usar este apéndice

Los capítulos principales te enseñan a construir un driver de dispositivo FreeBSD desde el primer módulo con `printf("hello")` hasta un driver PCI funcional con DMA e interrupciones. Por debajo de esa progresión hay un kernel amplio con muchas partes en movimiento, y el libro no puede enseñar cada una de ellas desde cero sin perder el hilo de lo que estás intentando hacer realmente. La mayor parte del tiempo no necesitas conocer cada rincón del kernel. Solo necesitas saber dónde estás, qué subsistema está tocando la línea de código que tienes delante, qué estructura te da la respuesta cuando te detienes a mirar, y dónde vive la evidencia en `/usr/src`.

Este apéndice es ese mapa. No pretende enseñar cada subsistema desde los primeros principios. Toma los siete subsistemas con los que un autor de drivers se encuentra más a menudo y, para cada uno, te da la versión breve: para qué sirve, qué estructuras importan, qué APIs es probable que tu driver cruce, dónde abrir un archivo y mirar, y qué leer a continuación. Puedes considerarlo la guía de campo que tienes junto a los capítulos, no un sustituto de ellos.

### Qué encontrarás aquí

Cada subsistema se cubre con el mismo patrón breve, de modo que puedas ojear uno y saber dónde buscar en el siguiente.

- **Para qué sirve el subsistema.** Una declaración de un párrafo sobre la responsabilidad del subsistema.
- **Por qué le importa al autor de un driver.** La razón concreta por la que tu código se encuentra con este subsistema.
- **Estructuras, interfaces o conceptos clave.** La lista breve de nombres que realmente importan.
- **Puntos de contacto típicos del driver.** Los lugares específicos donde un driver llama, se registra o recibe un callback del subsistema.
- **Dónde buscar en `/usr/src`.** Los dos o tres archivos que vale la pena abrir primero.
- **Páginas de manual y archivos para leer a continuación.** El siguiente paso cuando quieres más profundidad.
- **Confusión habitual para principiantes.** El malentendido que le cuesta tiempo a la gente.
- **Dónde lo enseña el libro.** Referencias cruzadas a los capítulos que usan el subsistema en contexto.

No todas las entradas necesitan todas las etiquetas, y ninguna entrada pretende ser exhaustiva. El objetivo es el reconocimiento de patrones, no un manual completo del subsistema.

### Qué no es este apéndice

No es una referencia de API. El Apéndice A es la referencia de API y profundiza en los flags, las fases del ciclo de vida y las advertencias de cada llamada. Cuando la pregunta es *qué hace esta función* o *qué flag es el correcto*, el Apéndice A es el lugar donde buscar.

Tampoco es un tutorial de conceptos. El Apéndice D cubre el modelo mental del sistema operativo (kernel frente a userland, tipos de driver, la ruta de boot hasta init), el Apéndice C cubre el modelo de hardware (memoria física, MMIO, interrupciones, DMA), y el Apéndice B cubre los patrones algorítmicos (`<sys/queue.h>`, ring buffers, máquinas de estados, escaleras de liberación). Si la pregunta que quieres responder es «qué es un proceso», «qué es un BAR» o «qué macro de lista debo usar», uno de esos apéndices es el destino adecuado.

Tampoco es una referencia completa del subsistema. Un recorrido completo por el VFS, la VM o la pila de red daría para un libro entero. Lo que obtienes aquí es el diez por ciento de cada subsistema con el que un autor de drivers se encuentra realmente, en el orden en que lo encuentra.

## Orientación para el lector

Hay tres formas de usar este apéndice, cada una de las cuales requiere una estrategia de lectura diferente.

Si estás **leyendo los capítulos principales**, mantén el apéndice abierto en una segunda ventana. Cuando el Capítulo 5 presente los asignadores de memoria del kernel, echa un vistazo a la sección del Subsistema de memoria aquí para ver dónde encajan esos asignadores en relación con UMA y el sistema VM. Cuando el Capítulo 6 recorra `device_t`, softc y el ciclo de vida de probe/attach, la sección de Infraestructura del driver muestra dónde encajan esos tipos en la capa Newbus. Cuando el Capítulo 24 trate `SYSINIT`, `eventhandler(9)` y taskqueues, las secciones del Sistema de boot y módulos y de Servicios del kernel te dan el contexto circundante en una página cada una.

Si estás **leyendo código del kernel desconocido**, trata el apéndice como un traductor. Cuando veas `struct mbuf` en la firma de una función, ve a la sección del Subsistema de red. Cuando veas `struct bio`, ve a Archivo y VFS. Cuando veas `kobj_class_t` o `device_method_t`, ve a Infraestructura del driver. El objetivo durante la exploración no es dominar el subsistema, sino nombrarlo.

Si estás **diseñando un driver nuevo**, repasa los subsistemas que va a tocar tu driver antes de empezar. Un driver de caracteres para un periférico pequeño se apoyará en Infraestructura del driver y en Archivo y VFS. Un driver de red añadirá el Subsistema de red. Un driver de almacenamiento añadirá las capas GEOM y de caché de buffer. Un driver de arranque temprano en una placa embebida añadirá la sección del Sistema de boot y módulos. Saber qué subsistemas vas a tocar te ayuda a intuir las cabeceras correctas y los capítulos adecuados antes de escribir una sola línea de código.

A lo largo del apéndice se aplican algunas convenciones:

- Las rutas de código fuente se muestran en la forma usada en el libro, `/usr/src/sys/...`, que coincide con la distribución de un sistema FreeBSD estándar. Puedes abrir cualquiera de ellas en tu máquina de laboratorio.
- Las páginas de manual se citan al estilo FreeBSD habitual. Las páginas orientadas al kernel viven en la sección 9: `kthread(9)`, `malloc(9)`, `uma(9)`, `bus_space(9)`, `eventhandler(9)`. Las interfaces de userland viven en la sección 2 o 3 y se mencionan donde corresponde.
- Cuando una entrada apunta a un ejemplo de lectura, el archivo es uno que un principiante puede leer de una sola vez. Existen archivos más grandes que también usan cada patrón; esos se mencionan solo cuando son la referencia canónica.

Con eso en mente, empezamos con una orientación de una página sobre el kernel completo antes de profundizar en los subsistemas uno a uno.

## En qué se diferencia este apéndice del Apéndice A

Un autor de drivers acaba consultando dos tipos de referencia muy distintos mientras trabaja. Un tipo responde a la pregunta *cuál es el nombre exacto de la función o el flag que necesito*. Ese es el Apéndice A. El otro tipo responde a la pregunta *en qué subsistema estoy y dónde encaja esta pieza*. Ese es este apéndice.

Concretamente, la diferencia se manifiesta así. Cuando quieres conocer la firma de `malloc(9)`, el significado de `M_WAITOK` frente a `M_NOWAIT`, y qué página de manual abrir, eso es el Apéndice A. Cuando quieres saber que `malloc(9)` es una capa de conveniencia fina sobre UMA, que a su vez se construye sobre la capa `vm_page_t`, que a su vez depende de `pmap(9)` específico de cada arquitectura, eso es este apéndice.

Ambos apéndices citan rutas de código fuente reales y páginas de manual reales. La separación es deliberada. Mantener la búsqueda de API separada del mapa de subsistemas hace que cada uno sea lo suficientemente breve para usarlo de verdad. Si una entrada aquí empieza a parecerse al Apéndice A, se ha alejado de su función, y la decisión correcta es leer el Apéndice A en su lugar.

## Un mapa de los principales subsistemas

Antes de adentrarte en cualquier subsistema concreto, vale la pena nombrar la forma del kernel completo. El kernel de FreeBSD es amplio, pero las piezas con las que se encuentra un autor de drivers encajan en un conjunto pequeño de familias. El diagrama siguiente es la imagen más sencilla y fiel.

```text
+-----------------------------------------------------------------+
|                            USER SPACE                           |
|     applications, daemons, shells, tools, the libraries         |
+-----------------------------------------------------------------+
                               |
                  system-call trap (the boundary)
                               |
+-----------------------------------------------------------------+
|                           KERNEL SPACE                          |
|                                                                 |
|   +-----------------------+   +-----------------------------+   |
|   |   VFS / devfs / GEOM  |   |        Network stack        |   |
|   |  struct vnode, buf,   |   |   struct mbuf, socket,      |   |
|   |  bio, vop_vector      |   |   ifnet, route, VNET        |   |
|   +-----------------------+   +-----------------------------+   |
|                 \                     /                         |
|                  \                   /                          |
|                 Driver infrastructure (Newbus)                  |
|           device_t, driver_t, devclass_t, softc, kobj           |
|           bus_alloc_resource, bus_space, bus_dma                |
|                               |                                 |
|      Process/thread subsystem  |  Memory / VM subsystem          |
|      struct proc, thread       |  vm_map, vm_object, vm_page    |
|      ULE scheduler, kthreads   |  pmap, UMA, pagers             |
|                               |                                 |
|         Boot and module system (SYSINIT, KLD, modules)          |
|         Kernel services (eventhandler, taskqueue, callout)      |
+-----------------------------------------------------------------+
                               |
                      hardware I/O boundary
                               |
+-----------------------------------------------------------------+
|                             HARDWARE                            |
|     MMIO registers, interrupt controllers, DMA-capable memory   |
+-----------------------------------------------------------------+
```

Cada uno de los recuadros etiquetados arriba tiene una sección más abajo. El recuadro de infraestructura del driver, en el centro, es donde empieza todo driver. Los dos recuadros de arriba son los puntos de entrada al subsistema que un driver publica hacia el resto del kernel (caracteres o almacenamiento a la izquierda, red a la derecha). Los dos recuadros de las filas intermedias son los servicios horizontales de los que depende todo driver. Los recuadros del fondo son la fontanería que hace arrancar el kernel en primer lugar.

La mayoría de los drivers solo toca tres o cuatro de estos recuadros con detalle. El apéndice está organizado para que puedas leer únicamente los que tu driver usa de verdad.

## Subsistema de proceso y thread

### Para qué sirve el subsistema

El subsistema de proceso y thread gestiona cada unidad de ejecución dentro de FreeBSD. Posee las estructuras de datos que describen un programa en ejecución, el planificador que decide qué thread se ejecuta a continuación y en qué CPU, la maquinaria que crea y destruye threads del kernel, y las reglas que rigen cómo un thread puede bloquearse, dormirse o ser desalojado. Cada línea de código del kernel, incluido tu driver, es ejecutada por algún thread, y la disciplina que impone el subsistema es una restricción directa sobre lo que tu driver tiene permitido hacer.

### Por qué le importa al autor de un driver

Hay tres razones prácticas. Primera: el contexto en que se ejecuta tu código (filtro de interrupción, ithread, trabajador de taskqueue, thread de syscall desde userland, thread del kernel dedicado que tú has creado) determina si puedes dormirte, si puedes asignar memoria con `M_WAITOK` o si puedes mantener un sleep lock. Segunda: cualquier driver que necesite trabajo en segundo plano (un bucle de sondeo, un watchdog de recuperación, un manejador diferido de comandos) creará un thread del kernel o un kproc para alojarlo. Tercera: cualquier driver que examine las credenciales del proceso de quien llama (por ejemplo, para comprobaciones de seguridad en `d_ioctl`) accede a la estructura del proceso.

### Estructuras, interfaces o conceptos clave

- **`struct proc`** es el descriptor por proceso. Registra el id del proceso, las credenciales, la tabla de descriptores de archivo, el estado de señales, el espacio de direcciones y la lista de threads pertenecientes al proceso.
- **`struct thread`** es el descriptor por thread. Registra el id del thread, la prioridad, el estado de ejecución, el contexto de registros guardado, el puntero a su `struct proc` propietario y los locks que mantiene en ese momento. Un thread del kernel de FreeBSD también se describe mediante un `struct thread`; simplemente no tiene lado en userland.
- **El planificador ULE** es el planificador multiprocesador predeterminado de FreeBSD. Asigna threads a CPUs, implementa clases de prioridad (tiempo real, tiempo compartido, inactivo) y tiene en cuenta las sugerencias de interactividad y afinidad. Desde el punto de vista de un autor de drivers, el hecho más importante sobre ULE es que ejecuta el thread que corresponda cada vez que se libera un lock, termina un reposo o finaliza una interrupción; no puedes asumir el control de la CPU a través de esos eventos.
- **`kthread_add(9)`** crea un nuevo thread del kernel dentro de un proceso de kernel existente. Úsalo cuando quieras un trabajador ligero que comparta estado con un kproc existente (por ejemplo, threads trabajadores adicionales dentro de un kproc específico de un driver).
- **`kproc_create(9)`** crea un nuevo proceso de kernel, que viene con su propio `struct proc` y un thread inicial. Úsalo cuando quieras un trabajador independiente de nivel superior que `ps -axH` muestre con un nombre diferenciado (por ejemplo, `g_event`, `usb`, `bufdaemon`).

### Puntos de contacto típicos del driver

- Los manejadores de interrupciones y los callbacks de `bus_setup_intr(9)` se ejecutan en el contexto de thread del kernel creado por el framework de interrupciones.
- Un driver que necesita trabajo en segundo plano de larga duración llama a `kproc_create(9)` o `kthread_add(9)` desde su ruta de attach y espera a que el thread finalice desde detach.
- Un driver que actúa en nombre de un proceso de usuario lee `curthread` o `td->td_proc` para examinar las credenciales, el id del proceso o el directorio raíz a efectos de validación.
- Un driver que se duerme esperando una condición usa las primitivas de reposo, que registran el thread dormido y ceden el control al planificador hasta que se le despierte.

### Dónde buscar en `/usr/src`

- `/usr/src/sys/sys/proc.h` define `struct proc` y `struct thread` y las macros que permiten navegar entre ellos.
- `/usr/src/sys/sys/kthread.h` declara la API de creación de threads del kernel.
- `/usr/src/sys/kern/kern_kthread.c` contiene la implementación.
- `/usr/src/sys/kern/sched_ule.c` es el código fuente del planificador ULE.

### Páginas de manual y archivos para leer a continuación

`kthread(9)`, `kproc(9)`, `curthread(9)`, `proc(9)` y la cabecera `/usr/src/sys/sys/proc.h`. Si quieres ver un driver que posee un thread del kernel, `/usr/src/sys/dev/random/random_harvestq.c` es un ejemplo legible.

### Confusión habitual para principiantes

La trampa más frecuente es suponer que el thread que ejecuta el código de tu driver es un thread propiedad del driver. No lo es. La mayor parte del tiempo se trata de un thread de usuario que entró al kernel a través de un syscall, o de un ithread que el framework de interrupciones creó por ti. Tu driver solo es propietario de los threads que crea de forma explícita. Otra trampa recurrente es acceder a `curthread->td_proc` desde un contexto en el que `curthread` no tiene ninguna relación con el dispositivo (un ithread de interrupción, por ejemplo); el proceso que encuentras ahí no es el proceso que solicitó la operación.

### Dónde se enseña esto en el libro

El capítulo 5 presenta el contexto de ejecución del kernel y la distinción entre operaciones que pueden dormir y operaciones atómicas. El capítulo 11 retoma el tema cuando la concurrencia se vuelve real. El capítulo 14 usa taskqueues como forma de delegar trabajo a un contexto en el que dormir es seguro. El capítulo 24 muestra el ciclo de vida completo de un kproc dentro de un driver.

### Lecturas adicionales

- **En este libro**: capítulo 11 (Concurrencia en drivers), capítulo 14 (Taskqueues y trabajo diferido), capítulo 24 (Integración con el kernel).
- **Páginas de manual**: `kthread(9)`, `kproc(9)`, `scheduler(9)`.
- **Externo**: McKusick, Neville-Neil y Watson, *The Design and Implementation of the FreeBSD Operating System* (2.ª ed.), capítulos sobre gestión de procesos y threads.

## Subsistema de memoria

### Para qué sirve este subsistema

El subsistema de memoria virtual (VM) gestiona cada byte de memoria que el kernel puede direccionar. Es responsable de la correspondencia entre direcciones virtuales y páginas físicas, de la asignación de páginas a procesos y al kernel, de la política de paginación que recupera páginas bajo presión, y de los almacenes de respaldo de páginas que las suministran desde disco, desde dispositivos o desde cero. Un driver que asigna memoria, expone memoria al espacio de usuario a través de `mmap` o realiza DMA está interactuando con el subsistema VM, lo nombre o no.

### Por qué le importa esto al autor de un driver

Hay cuatro razones prácticas. Primera: toda asignación en el kernel pasa por este subsistema, directa o indirectamente. Segunda: cualquier driver que exporte una vista mapeada en memoria de un dispositivo (o de un buffer de software) lo hace a través de un pager VM. Tercera: el DMA implica direcciones físicas, y solo el subsistema VM sabe cómo se traducen las direcciones virtuales del kernel a esas direcciones físicas. Cuarta: el subsistema define las reglas de suspensión para las asignaciones: `M_WAITOK` puede recorrer la ruta de recuperación de páginas del VM, algo que no puedes hacer desde un filtro de interrupciones.

### Estructuras, interfaces y conceptos clave

- **`vm_map_t`** representa una colección contigua de correspondencias de direcciones virtuales pertenecientes a un espacio de direcciones. El kernel tiene su propio `vm_map_t`, y cada proceso de usuario tiene el suyo. Los drivers casi nunca recorren un `vm_map_t` directamente; las APIs de mayor nivel lo hacen por ellos.
- **`vm_object_t`** representa un almacén de respaldo: un conjunto de páginas que pueden mapearse en un `vm_map_t`. Los objetos se tipan según el pager que produce sus páginas (anónimo, respaldado por vnode, respaldado por swap, respaldado por dispositivo).
- **`vm_page_t`** representa una página física de RAM, junto con su estado actual (wired, activa, inactiva, libre) y el objeto al que pertenece en ese momento. Toda la memoria física del sistema está registrada en un array de registros `vm_page_t`.
- **La capa del pager** es el conjunto de estrategias intercambiables que rellenan páginas con datos. Los tres más importantes para los autores de drivers son el swap pager (memoria anónima), el vnode pager (memoria respaldada por archivos) y el device pager (memoria cuyo contenido produce un driver). Cuando un driver implementa `d_mmap` o `d_mmap_single`, está publicando una porción del device pager.
- **`pmap(9)`** es el gestor de tablas de páginas dependiente de la arquitectura. Sabe cómo traducir direcciones virtuales a físicas para la arquitectura CPU actual. Los drivers raramente llaman directamente a `pmap`. La forma portable de obtener una vista física es a través de `bus_dma(9)` (para DMA) o `bus_space(9)` (para registros MMIO).
- **UMA** es el slab allocator de FreeBSD para objetos de tamaño fijo, con caches por CPU para evitar el uso de locks en la ruta rápida. `malloc(9)` está implementado sobre UMA para los tamaños más habituales. Los drivers que asignan y liberan millones de objetos pequeños idénticos por segundo (descriptores de red, contextos por petición) crean su propia zona UMA con `uma_zcreate` y reutilizan objetos en lugar de recurrir al asignador general.

### Puntos de contacto habituales en un driver

- `malloc(9)`, `free(9)`, `contigmalloc(9)` para la memoria general del plano de control.
- `uma_zcreate(9)`, `uma_zalloc(9)`, `uma_zfree(9)` para objetos de tamaño fijo con tasas de asignación elevadas.
- `bus_dmamem_alloc(9)` y el resto de la interfaz `bus_dma(9)` para memoria compatible con DMA; es el envoltorio orientado al driver sobre el lado físico del VM.
- `d_mmap(9)` o `d_mmap_single(9)` en la tabla de métodos `cdevsw` para publicar una vista del device pager de la memoria hardware al espacio de usuario.
- `vm_page_wire(9)` y `vm_page_unwire(9)` solo en los casos excepcionales en que el driver necesita anclar páginas de un buffer de usuario durante una operación de I/O prolongada.

### Dónde buscar en `/usr/src`

- `/usr/src/sys/vm/vm.h` declara los typedefs `vm_map_t`, `vm_object_t` y `vm_page_t`.
- `/usr/src/sys/vm/vm_map.h`, `/usr/src/sys/vm/vm_object.h` y `/usr/src/sys/vm/vm_page.h` contienen las definiciones completas de tipos.
- `/usr/src/sys/vm/swap_pager.c`, `/usr/src/sys/vm/vnode_pager.c` y `/usr/src/sys/vm/device_pager.c` son los tres pagers más relevantes para los drivers.
- `/usr/src/sys/vm/uma.h` es la interfaz pública de UMA; `/usr/src/sys/vm/uma_core.c` es la implementación.
- `/usr/src/sys/vm/pmap.h` es la interfaz pmap independiente de la arquitectura; el lado específico de cada máquina se encuentra en `/usr/src/sys/amd64/amd64/pmap.c`, `/usr/src/sys/arm64/arm64/pmap.c` y archivos similares para cada arquitectura.

### Páginas de manual y archivos que leer a continuación

`malloc(9)`, `uma(9)`, `contigmalloc(9)`, `bus_dma(9)`, `pmap(9)` y el encabezado `/usr/src/sys/vm/uma.h`. Para ver un driver legible que publica un device pager, inspecciona `/usr/src/sys/dev/drm2/` o el código de framebuffer en `/usr/src/sys/dev/fb/`.

### Confusión habitual entre los principiantes

Hay dos trampas. La primera es confundir los tres tipos de dirección que ve un driver compatible con DMA: la dirección virtual del kernel (a la que apunta tu puntero cuando lo desreferencias), la dirección física (la que ve el controlador de memoria) y la dirección de bus (la que ve el dispositivo, que puede pasar por un IOMMU). `bus_dma(9)` existe precisamente para mantenerlas separadas. La segunda es asumir que una asignación con `bus_dmamem_alloc(9)` es una asignación de memoria genérica; en realidad es una asignación especializada con reglas más estrictas de alineación, límites y segmentos, determinadas por el tag que hayas proporcionado.

### Dónde se enseña esto en el libro

El capítulo 5 presenta la memoria del kernel y los flags del asignador. El capítulo 10 retoma los buffers en las rutas de lectura y escritura. El capítulo 17 presenta `bus_space` para el acceso a registros MMIO. El capítulo 21 es el capítulo completo sobre DMA y el lugar donde la distinción entre dirección de bus y dirección física se vuelve concreta.

### Lecturas adicionales

- **En este libro**: capítulo 5 (Comprensión de C para la programación del kernel de FreeBSD), capítulo 21 (DMA y transferencia de datos de alta velocidad).
- **Páginas de manual**: `malloc(9)`, `uma(9)`, `contigmalloc(9)`, `bus_dma(9)`, `pmap(9)`.
- **Externo**: McKusick, Neville-Neil y Watson, *The Design and Implementation of the FreeBSD Operating System* (2.ª ed.), capítulo sobre gestión de memoria.

## Subsistema de archivos y VFS

### Para qué sirve este subsistema

El subsistema de archivos y VFS (Virtual File System) es propietario de todo lo que el espacio de usuario ve a través de `open(2)`, `read(2)`, `write(2)`, `ioctl(2)`, `mmap(2)` y la jerarquía del sistema de archivos en general. Despacha operaciones al sistema de archivos correcto a través del vector de operaciones vnode, gestiona el buffer cache situado entre los sistemas de archivos y los drivers de almacenamiento, y aloja el framework GEOM que permite a los drivers de almacenamiento componerse en pilas. Para el autor de un driver, este subsistema es el punto de entrada principal (si escribes un driver de caracteres o de almacenamiento) o una capa intermedia silenciosa de la que puedes alegrarte de dejar que otros se preocupen (si escribes un driver de red o embebido).

### Por qué le importa esto al autor de un driver

Hay tres razones prácticas. Primera: todo driver de caracteres se publica en el VFS a través de un `cdevsw` y un nodo `/dev` creado por devfs. Segunda: todo driver de almacenamiento se conecta en la parte inferior de una pila que las capas VFS y GEOM ensamblan encima, y la unidad de trabajo que recibes es un `struct bio`, no un puntero de usuario. Tercera: incluso un driver que no sea de almacenamiento puede necesitar comprender los vnodes y `mmap` si publica su memoria al espacio de usuario.

### Estructuras, interfaces y conceptos clave

- **`struct vnode`** es la abstracción del kernel de un archivo o dispositivo. Contiene un tipo (archivo regular, directorio, dispositivo de caracteres, dispositivo de bloques, tubería con nombre, socket, enlace simbólico), un puntero al `vop_vector` de su sistema de archivos, el montaje al que pertenece, un contador de referencias y un lock. Todo descriptor de archivo del espacio de usuario se resuelve, en última instancia, en un vnode.
- **`struct vop_vector`** es la tabla de despacho de operaciones vnode: un puntero por operación (`VOP_LOOKUP`, `VOP_READ`, `VOP_WRITE`, `VOP_IOCTL` y docenas más) que el sistema de archivos o devfs implementa. El vector se declara conceptualmente en `/usr/src/sys/sys/vnode.h` y se genera a partir de la lista de operaciones en `/usr/src/sys/kern/vnode_if.src`.
- **El framework GEOM** es la capa de almacenamiento apilable de FreeBSD. Un *provider* GEOM es una superficie de almacenamiento; un *consumer* es algo que se conecta a un provider. Los drivers de hardware de almacenamiento se registran como providers; clases como `g_part`, `g_mirror` o los sistemas de archivos se conectan como consumers. El grafo de topología es dinámico y visible en tiempo de ejecución a través de `gpart show`, `geom disk list` y `sysctl kern.geom`.
- **devfs** es el pseudosistema de archivos que puebla `/dev`. Cuando tu driver de caracteres llama a `make_dev_s(9)`, devfs asigna una entrada respaldada por vnode que reenvía las operaciones VFS a tus callbacks `cdevsw`. devfs es la única capa entre `open(2)` sobre una ruta `/dev` y `d_open` en tu driver.
- **`struct buf`** es el descriptor de buffer cache tradicional, utilizado por la antigua ruta de dispositivo de bloques y por los sistemas de archivos que se apilan sobre el buffer cache. Sigue siendo importante porque muchos sistemas de archivos conducen la I/O a través de objetos `buf` antes de que `buf_strategy()` los encauce hacia GEOM.
- **`struct bio`** es el descriptor moderno por operación que fluye a través de GEOM. Toda lectura o escritura de bloque en GEOM es un `bio` con un comando (`BIO_READ`, `BIO_WRITE`, `BIO_FLUSH`, `BIO_DELETE`), un rango, un puntero a buffer y un callback de finalización. Tu driver de almacenamiento recibe `bio`s en su rutina de inicio y llama a `biodone()` (o al equivalente GEOM) cuando los completa.

### Puntos de contacto habituales en un driver

- Un driver de caracteres rellena un `struct cdevsw` con callbacks (`d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl`, opcionalmente `d_poll`, `d_mmap`) y llama a `make_dev_s(9)` para adjuntarlo a `/dev`.
- Un driver de almacenamiento registra una clase GEOM, implementa una rutina de inicio que acepta `bio`s y llama a `g_io_deliver()` cuando los termina.
- Un driver que quiera ser visible como archivo (para leer telemetría, por ejemplo) puede exponer un dispositivo de caracteres cuyo `d_read` copie los datos del driver hacia fuera.
- Un driver que publica memoria del dispositivo al espacio de usuario implementa `d_mmap` o `d_mmap_single` para devolver un objeto del device pager.

### Dónde buscar en `/usr/src`

- `/usr/src/sys/sys/vnode.h` declara `struct vnode` y la infraestructura de las operaciones vnode.
- `/usr/src/sys/kern/vnode_if.src` es la fuente de verdad de cada VOP del kernel; léelo para ver la lista de operaciones y el protocolo de bloqueo.
- `/usr/src/sys/fs/devfs/` contiene la implementación de devfs; `devfs_devs.c` y `devfs_vnops.c` son los puntos de entrada más legibles.
- `/usr/src/sys/geom/geom.h` declara providers, consumers y la interfaz de clases GEOM.
- `/usr/src/sys/sys/buf.h` y `/usr/src/sys/sys/bio.h` declaran las estructuras de I/O de bloque.
- `/usr/src/sys/dev/null/null.c` es el driver de caracteres más sencillo del árbol y la primera lectura adecuada.

### Páginas de manual y archivos que leer a continuación

`vnode(9)`, `VOP_LOOKUP(9)` y el resto de la familia VOP, `devfs(4)`, `devfs(5)`, `cdev(9)`, `make_dev(9)`, `g_attach(9)`, `geom(4)` y el encabezado `/usr/src/sys/sys/bio.h`.

### Confusión habitual entre los principiantes

Tres trampas. La primera, esperar que un driver de caracteres trabaje con `struct buf` o `struct bio`. No lo hace; esas estructuras pertenecen a la ruta de almacenamiento. Un driver de caracteres recibe un `struct uio` en sus callbacks `d_read` y `d_write`, y nada más. La segunda, esperar que un driver de almacenamiento cree por sí mismo un nodo en `/dev`. En el FreeBSD moderno, la capa GEOM crea las entradas de `/dev` para los dispositivos de bloques; tu driver de almacenamiento se registra con GEOM, y devfs hace el resto al otro lado de GEOM. La tercera, asumir que el vnode y el cdev son el mismo objeto. No lo son. El vnode es el handle del lado de VFS para el archivo abierto; el cdev es la identidad del lado del driver. `open(2)` sobre `/dev/foo` produce un vnode cuyas operaciones se reenvían hacia tu `cdevsw`.

### Dónde lo enseña este libro

El capítulo 7 escribe el primer driver de caracteres y el primer `cdevsw`. El capítulo 8 recorre `make_dev_s(9)` y la creación de nodos devfs. El capítulo 9 conecta `d_read` y `d_write` con `uio`. El capítulo 27 es el capítulo de almacenamiento e introduce `struct bio`, los proveedores y consumidores GEOM, y el buffer cache.

### Lecturas adicionales

- **En este libro**: Capítulo 7 (Tu primer driver), Capítulo 8 (Trabajando con archivos de dispositivo), Capítulo 27 (Trabajando con dispositivos de almacenamiento y la capa VFS).
- **Páginas de manual**: `vnode(9)`, `make_dev(9)`, `devfs(5)`, `geom(4)`, `g_bio(9)`.
- **Recursos externos**: McKusick, Neville-Neil y Watson, *The Design and Implementation of the FreeBSD Operating System* (2.ª ed.), capítulos sobre el sistema de I/O y los sistemas de archivos locales.

## El subsistema de red

### Para qué sirve este subsistema

El subsistema de red mueve paquetes. Es el dueño de las estructuras de datos que representan un paquete en tránsito (`mbuf` y sus estructuras asociadas), el estado por interfaz que representa un dispositivo de red ante el resto de la pila (`ifnet`), las tablas de enrutamiento que deciden hacia dónde debe ir un paquete, la capa de sockets que ve el userland y la infraestructura VNET que permite que múltiples pilas de red independientes coexistan en un único kernel. Un driver de red es la capa inferior de esta pila: entrega los paquetes recibidos hacia la pila y la pila los entrega al driver para transmitirlos.

### Por qué debe importarle a quien escribe drivers

Hay dos razones. Si escribes un driver de red, casi cada byte que tocas es un campo de una de las estructuras que se describen a continuación, y la forma de tu código viene determinada por los protocolos que estas imponen. Si escribes cualquier otro tipo de driver, también te beneficia reconocer `struct mbuf` y `struct ifnet` cuando los encuentres en el código, porque aparecen en muchos subsistemas adyacentes (filtros de paquetes, auxiliares de balanceo de carga, interfaces virtuales).

### Estructuras, interfaces y conceptos clave

- **`struct mbuf`** es el fragmento de paquete. Los paquetes se representan como cadenas de mbufs, enlazadas mediante `m_next` para un único paquete y mediante `m_nextpkt` para paquetes consecutivos en una cola. Un mbuf lleva una pequeña cabecera y, o bien una pequeña área de datos en línea, o un puntero a un clúster de almacenamiento externo. El diseño está optimizado para anteponer cabeceras de forma eficiente.
- **`struct m_tag`** es una etiqueta de metadatos extensible que se adjunta a un mbuf. Permite que la pila y los drivers asocien información tipada a un paquete (por ejemplo, offload de checksum de transmisión por hardware, hash de receive-side-scaling, decisión de filtro) sin ampliar el propio mbuf.
- **`ifnet`** (escrito como `if_t` en las APIs modernas) es el descriptor por interfaz. Contiene el nombre y el índice de la interfaz, los flags, la MTU, una función de transmisión (`if_transmit`), los contadores que la pila incrementa y los hooks que permiten a las capas superiores entregar paquetes al driver.
- **VNET** es el contenedor por pila de red virtual. Cuando se compila `VIMAGE`, cada jail que habilita VNET tiene su propia tabla de enrutamiento, su propio conjunto de interfaces y sus propios bloques de control de protocolo. Los drivers de red deben ser conscientes de VNET: usan `VNET_DEFINE` y `VNET_FOREACH` para que el estado por VNET resida en el lugar correcto.
- **Enrutamiento** es el subsistema que selecciona el siguiente salto para un paquete saliente. Es el dueño de la base de información de reenvío (FIB), un árbol radix por VNET de rutas. Los drivers raramente interactúan con el enrutamiento de forma directa; la pila ya ha elegido la interfaz antes de llegar al driver.
- **La capa de sockets** es la parte del kernel de la familia de syscalls `socket(2)`. Para quienes escriben drivers, lo relevante es que un socket genera en última instancia llamadas a `ifnet`, que a su vez producen llamadas a tu driver. Tú no implementas sockets directamente.

### Puntos de contacto típicos del driver

- El driver asigna y rellena un `ifnet` en `attach`, registra una función de transmisión y llama a `ether_ifattach(9)` o `if_attach(9)` para anunciarse a la pila.
- La función de transmisión del driver recibe una cadena de mbufs, escribe descriptores, activa el hardware y retorna.
- La ruta de recepción gestiona una interrupción o un sondeo, envuelve los bytes recibidos en mbufs y llama a `if_input(9)` sobre la interfaz para entregarlos a la pila.
- Al hacer detach, el driver llama a `ether_ifdetach(9)` o `if_detach(9)` antes de liberar sus recursos.
- El driver se registra para `ifnet_arrival_event` o `ifnet_departure_event` si necesita reaccionar cuando otras interfaces aparecen o desaparecen (véase `/usr/src/sys/net/if_var.h` para las declaraciones).

### Dónde buscar en `/usr/src`

- `/usr/src/sys/sys/mbuf.h` declara `struct mbuf` y `struct m_tag`.
- `/usr/src/sys/net/if.h` declara `if_t` y la API pública de la interfaz.
- `/usr/src/sys/net/if_var.h` declara los eventhandlers de eventos de interfaz y el estado interno.
- `/usr/src/sys/net/if_private.h` contiene la definición completa de `struct ifnet` utilizada dentro de la pila.
- `/usr/src/sys/net/vnet.h` declara la infraestructura VNET.
- `/usr/src/sys/net/route.h` y `/usr/src/sys/net/route/` contienen las tablas de enrutamiento.
- `/usr/src/sys/sys/socketvar.h` declara `struct socket`.

### Páginas de manual y archivos recomendados

`mbuf(9)`, `ifnet(9)`, `ether_ifattach(9)`, `vnet(9)`, `route(4)` y `socket(9)`. Para un driver de red real, pequeño y fácil de leer, `/usr/src/sys/net/if_tuntap.c` es el ejemplo canónico.

### Confusiones frecuentes en principiantes

Hay dos trampas. La primera es esperar que un driver de red se publique a través de `/dev`. No lo hace; se publica a través de `ifnet` y se hace visible como `bge0`, `em0`, `igb0`, etc., no a través de devfs. La segunda es conservar un mbuf después de entregarlo a la pila. En cuanto llamas a `if_input` o retornas de `if_transmit`, el mbuf ya no te pertenece; usarlo después corrompe la pila en silencio.

### Dónde lo enseña este libro

El capítulo 28 es el capítulo completo sobre drivers de red y el lugar adecuado para los detalles. Los capítulos 11 y 14 proporcionan la disciplina de locking y trabajo diferido que necesita la ruta de recepción. El capítulo 24 cubre `ifnet_arrival_event` y los hooks de eventos relacionados a nivel de integración del driver.

### Lecturas adicionales

- **En este libro**: Capítulo 28 (Escribiendo un driver de red), Capítulo 11 (Concurrencia en drivers), Capítulo 14 (Taskqueues y trabajo diferido).
- **Páginas de manual**: `mbuf(9)`, `ifnet(9)`, `vnet(9)`, `socket(9)`.
- **Recursos externos**: McKusick, Neville-Neil y Watson, *The Design and Implementation of the FreeBSD Operating System* (2.ª ed.), capítulos sobre el subsistema de red.

## La infraestructura de drivers (Newbus)

### Para qué sirve este subsistema

Newbus es el framework de drivers de FreeBSD. Es el dueño del árbol de dispositivos del sistema, empareja drivers con dispositivos mediante probe, gestiona el ciclo de vida de cada attachment, encamina las asignaciones de recursos al bus correcto y proporciona el despacho orientado a objetos que permite a los buses sobrescribir y extender el comportamiento de los demás. Todo driver de caracteres, driver de almacenamiento, driver de red y driver embebido en el árbol es un participante de Newbus. Si los demás subsistemas de este apéndice son las habitaciones, Newbus es el pasillo que las conecta.

### Por qué debe importarle a quien escribe drivers

En la práctica, no existe ningún driver de FreeBSD sin Newbus. Los tipos que encuentras primero (`device_t`, softc, `driver_t`, `devclass_t`), las APIs que usas en `attach` (`bus_alloc_resource_any`, `bus_setup_intr`) y los macros con los que envuelves todo el driver (`DRIVER_MODULE`, `DEVMETHOD`, `DEVMETHOD_END`) pertenecen todos a este subsistema. Aprender a navegar por Newbus es lo mismo que aprender a navegar por el código fuente de drivers de FreeBSD.

### Estructuras, interfaces y conceptos clave

- **`device_t`** es un manejador opaco a un nodo en el árbol de dispositivos de Newbus. Recibes uno en `probe` y `attach`, lo pasas a casi todas las APIs del bus y lo usas para obtener el softc con `device_get_softc(9)`.
- **`driver_t`** es el descriptor de un driver: su nombre, su tabla de métodos y el tamaño de su softc. Construyes uno para tu driver y lo pasas a `DRIVER_MODULE(9)`, que lo registra bajo el nombre del bus padre.
- **`devclass_t`** es el registro por clase de driver: la colección de instancias `device_t` a las que un driver se ha adjuntado. Así es como el kernel asigna un número de unidad a cada instancia.
- **`kobj(9)`** es el mecanismo orientado a objetos que subyace a Newbus. Las tablas de métodos, el despacho de métodos y la capacidad de un bus para heredar los métodos de otro bus son características de kobj. Como autor de drivers, usas los macros `DEVMETHOD`, que se expanden en metadatos de kobj; raramente llamas directamente a los primitivos de kobj.
- **softc** es el estado por instancia de tu driver, asignado por el kernel cuando asigna el `device_t`. El kernel sabe el tamaño de tu softc porque tú se lo indicaste en el `driver_t`. `device_get_softc(9)` te devuelve un puntero a él.
- **`bus_alloc_resource(9)`** y sus funciones relacionadas asignan ventanas de memoria, puertos I/O y líneas de interrupción del bus padre en nombre de tu driver. Son la forma portable de acceder a los recursos de un dispositivo sin importar en qué bus se encuentra.

### Puntos de contacto típicos del driver

- Declara un array `device_method_t` con `DEVMETHOD(device_probe, ...)`, `DEVMETHOD(device_attach, ...)`, `DEVMETHOD(device_detach, ...)`, terminado con `DEVMETHOD_END`.
- Declara un `driver_t` con el nombre de tu driver, los métodos y `sizeof(struct mydev_softc)`.
- Usa `DRIVER_MODULE(mydev, pci, mydev_driver, ...)` (o `usbus`, `iicbus`, `spibus`, `simplebus`, `acpi`, `nexus`) para registrar el driver bajo su bus padre.
- En `probe`, decide si este dispositivo es tuyo y, de ser así, retorna `BUS_PROBE_DEFAULT` (o un valor más débil o más fuerte) y una descripción.
- En `attach`, asigna recursos con `bus_alloc_resource_any(9)`, mapea registros con `bus_space(9)`, configura las interrupciones con `bus_setup_intr(9)` y solo entonces exponte al resto del kernel.
- En `detach`, deshaz todo en orden inverso.

### Dónde buscar en `/usr/src`

- `/usr/src/sys/sys/bus.h` declara `device_t`, `driver_t`, `devclass_t`, `DEVMETHOD`, `DEVMETHOD_END`, `DRIVER_MODULE`, `bus_alloc_resource_any`, `bus_setup_intr` y la mayor parte de lo demás.
- `/usr/src/sys/sys/kobj.h` declara el mecanismo de despacho de métodos.
- `/usr/src/sys/kern/subr_bus.c` contiene la implementación de Newbus.
- `/usr/src/sys/kern/subr_kobj.c` contiene la implementación de kobj.
- `/usr/src/sys/dev/null/null.c` y `/usr/src/sys/dev/led/led.c` son drivers reales muy pequeños que puedes leer en una sola sesión.

### Páginas de manual y archivos recomendados

`device(9)`, `driver(9)`, `DEVMETHOD(9)`, `DRIVER_MODULE(9)`, `bus_alloc_resource(9)`, `bus_setup_intr(9)`, `kobj(9)` y `devinfo(8)` para ver el árbol de Newbus en ejecución.

### Confusiones frecuentes en principiantes

Hay dos trampas. La primera es pensar que `device_t` y softc son el mismo objeto. `device_t` es el manejador de Newbus; softc es el estado privado de tu driver. Obtienes el softc a partir de `device_t` con `device_get_softc(9)`. La segunda es olvidar que el segundo argumento de `DRIVER_MODULE` es el nombre del bus padre. Un driver declarado con `DRIVER_MODULE(..., pci, ...)` solo puede hacer attach bajo un bus PCI, independientemente de cuántas placas similares a PCI existan en otro lugar. Si un driver debe hacer attach bajo múltiples buses (por ejemplo, un chip que aparece tanto como PCI como ACPI), lo registras dos veces.

### Dónde lo enseña este libro

El capítulo 6 es el capítulo completo de anatomía de un driver y el lugar de enseñanza canónico para todo lo anterior. El capítulo 7 escribe el primer driver funcional usando estas APIs. El capítulo 18 amplía el panorama para PCI. El capítulo 24 vuelve a `DRIVER_MODULE`, `MODULE_VERSION` y `MODULE_DEPEND` cuando la integración en el kernel se convierte en el tema central.

### Lecturas adicionales

- **En este libro**: Capítulo 6 (La anatomía de un driver de FreeBSD), Capítulo 7 (Tu primer driver), Capítulo 18 (Escribiendo un driver PCI).
- **Páginas de manual**: `device(9)`, `driver(9)`, `DRIVER_MODULE(9)`, `bus_alloc_resource(9)`, `bus_setup_intr(9)`, `kobj(9)`, `rman(9)`.

## El sistema de boot y módulos

### Para qué sirve este subsistema

El sistema de boot y módulos es el mecanismo mediante el cual el kernel se carga en memoria, inicializa los cientos de subsistemas de los que depende antes de que cualquier cosa pueda ejecutarse y gestiona la incorporación, la conexión y, finalmente, la eliminación del código que no está compilado directamente en el kernel (los módulos cargables). Desde la perspectiva del autor de un driver, este subsistema determina cuándo se ejecuta tu código de inicialización en relación con el resto del kernel, y cómo los eventos `MOD_LOAD` y `MOD_UNLOAD` a nivel de módulo interactúan con el orden de inicialización interno del kernel.

### Por qué le importa esto a un autor de drivers

Tres razones. Primera, si tu driver puede cargarse como módulo, puede ejecutarse en un kernel cuyo orden de subsistemas difiere del que esperas, y necesitas declarar de qué dependes. Segunda, si tu driver debe ejecutarse de forma temprana (por ejemplo, un driver de consola o un driver de almacenamiento en tiempo de arranque), debes comprender los identificadores de subsistema de `SYSINIT(9)` para que tu código se ejecute en la ranura correcta. Tercera, incluso un driver ordinario depende del sistema de módulos para registrarse, declarar compatibilidad ABI y fallar limpiamente si falta una dependencia.

### Estructuras, interfaces y conceptos clave

- **La secuencia de arranque** sigue un arco fijo: el loader lee el kernel del disco, cede el control al punto de entrada del kernel, que configura el estado inicial de la CPU y llama a `mi_startup()`. `mi_startup()` recorre una lista ordenada de entradas `SYSINIT`, invocando cada una en orden. Cuando la lista se agota, el kernel cuenta con los servicios suficientes para iniciar `init(8)` como proceso de usuario 1.
- **`SYSINIT(9)`** es la macro que registra una función para ser llamada en una fase específica de la inicialización del kernel. Cada entrada tiene un ID de subsistema (`SI_SUB_*`, ordenación gruesa) y un orden dentro del subsistema (`SI_ORDER_*`, ordenación fina). La lista completa de IDs de subsistema válidos está en `/usr/src/sys/sys/kernel.h` y merece una lectura rápida al menos una vez. `SYSUNINIT(9)` es el correspondiente teardown.
- **La carga de módulos** está controlada por el framework KLD. `kldload(8)` invoca el enlazador, que reubica el módulo, resuelve sus símbolos frente al kernel en ejecución e invoca el manejador de eventos del módulo con `MOD_LOAD`. Un `MOD_UNLOAD` correspondiente se ejecuta al retirar el módulo. Los drivers rara vez escriben manejadores de eventos de módulo a mano; `DRIVER_MODULE(9)` genera uno por ti.
- **`MODULE_DEPEND(9)`** declara que tu módulo requiere otro módulo (`usb`, `miibus`, `pci`, `iflib`) y en qué rango de versiones. El kernel se niega a cargar tu módulo si la dependencia no está presente.
- **`MODULE_VERSION(9)`** declara la versión ABI que exporta tu módulo, de modo que otros módulos puedan depender de él mediante `MODULE_DEPEND`.

### Puntos de contacto típicos en un driver

- `DRIVER_MODULE(mydev, pci, mydev_driver, ...)` genera un manejador de eventos de módulo que registra el driver con `MOD_LOAD` y lo da de baja con `MOD_UNLOAD`.
- `MODULE_VERSION(mydev, 1);` anuncia la versión ABI de tu módulo.
- `MODULE_DEPEND(mydev, pci, 1, 1, 1);` declara una dependencia sobre pci.
- Un driver que debe ejecutarse antes de que Newbus esté disponible usa `SYSINIT(9)` para registrar un gancho de configuración puntual en un ID de subsistema temprano.
- Un driver que conecta un gancho de teardown en el último momento posible usa `SYSUNINIT(9)` con la ordenación correspondiente.

### Dónde buscar en `/usr/src`

- `/usr/src/sys/sys/kernel.h` define `SYSINIT`, `SYSUNINIT`, `SI_SUB_*` y `SI_ORDER_*`.
- `/usr/src/sys/kern/init_main.c` contiene `mi_startup()` y el recorrido por la lista SYSINIT.
- `/usr/src/sys/sys/module.h` declara `MODULE_VERSION` y `MODULE_DEPEND`.
- `/usr/src/sys/sys/linker.h` y `/usr/src/sys/kern/kern_linker.c` implementan el enlazador KLD.
- `/usr/src/stand/` aloja el loader y el código de arranque. (En versiones antiguas de FreeBSD esto vivía bajo `/usr/src/sys/boot/`; FreeBSD 14 lo aloja íntegramente bajo `/usr/src/stand/`.)

### Páginas de manual y archivos para leer a continuación

`SYSINIT(9)`, `kld(9)`, `kldload(9)`, `kldload(8)`, `kldstat(8)`, `module(9)`, `MODULE_VERSION(9)` y `MODULE_DEPEND(9)`. Para un ejemplo real y breve de `SYSINIT`, busca cerca del inicio de `/usr/src/sys/dev/random/random_harvestq.c`.

### Confusión habitual en principiantes

Dos trampas. Primera, asumir que `MOD_LOAD` es el momento en que se ejecuta tu función `attach`. No lo es. `MOD_LOAD` es el momento en que tu *driver* se registra con Newbus; `attach` se ejecuta más tarde, por dispositivo, cuando un bus ofrece un hijo que coincide. Segunda, usar los niveles de `SYSINIT` como si fueran arbitrarios. Cada `SI_SUB_*` corresponde a una fase bien definida del arranque del kernel, y registrar tu gancho en la fase incorrecta hace que se ejecute demasiado pronto (con la mitad del kernel sin inicializar) o demasiado tarde (después de que el evento que te importaba haya pasado).

### Dónde se explica en el libro

El capítulo 6 introduce `DRIVER_MODULE`, `MODULE_VERSION` y `MODULE_DEPEND` como parte de la anatomía de un driver. El capítulo 24 cubre temas de integración con el kernel, entre ellos `SYSINIT`, los IDs de subsistema y el orden de teardown de módulos. El capítulo 32 vuelve a las consideraciones de arranque en plataformas embebidas.

### Lectura adicional

- **En este libro**: capítulo 24 (Integración con el kernel), capítulo 32 (Device Tree y desarrollo embebido).
- **Páginas de manual**: `SYSINIT(9)`, `module(9)`, `MODULE_VERSION(9)`, `MODULE_DEPEND(9)`, `kldload(8)`, `kldstat(8)`.

## Servicios del kernel

### Para qué sirve este subsistema

El kernel incluye un pequeño conjunto de servicios de propósito general que no están ligados a ningún subsistema concreto, pero que aparecen con frecuencia en los drivers: notificaciones de eventos, colas de trabajo diferido, callbacks temporizados y ganchos de suscripción. Ninguno de ellos te enseña a escribir un driver, pero todos aparecen en código de driver real, y reconocerlos acelera cualquier sesión de lectura de código. Esta sección reúne los más habituales.

### Por qué le importa esto a un autor de drivers

Los drivers a menudo necesitan reaccionar a eventos del sistema (apagado, poca memoria, llegada de una interfaz, montaje del sistema de archivos raíz), o realizar trabajo fuera del contexto que entregó el evento (fuera de un filtro de interrupción, fuera de una sección crítica con spinlock). Los servicios del kernel que se describen a continuación son las respuestas estándar de FreeBSD a ambas necesidades. Usarlos significa que tu driver se integra limpiamente con el resto del sistema; reimplementarlos significa que tarde o temprano colisionarás con algún subsistema que espera que tus ganchos existan.

### Estructuras, interfaces y conceptos clave

- **`eventhandler(9)`** es el sistema de publicación/suscripción para eventos del kernel. Un publicador declara un evento con `EVENTHANDLER_DECLARE`, un suscriptor se registra con `EVENTHANDLER_REGISTER`, y la invocación con `EVENTHANDLER_INVOKE` se propaga a todos los suscriptores. Las etiquetas de eventos estándar definidas en `/usr/src/sys/sys/eventhandler.h` incluyen `shutdown_pre_sync`, `shutdown_post_sync`, `shutdown_final`, `vm_lowmem` y `mountroot`; los eventos de interfaz (`ifnet_arrival_event`, `ifnet_departure_event`) están declarados en `/usr/src/sys/net/if_var.h`. Los drivers los usan para limpiar recursos, liberar memoria, reaccionar cuando aparece una interfaz hermana o retrasar el trabajo inicial hasta que el sistema de archivos raíz esté disponible.
- **`taskqueue(9)`** es una cola de elementos de trabajo diferido. Un driver encola una tarea desde un contexto que no puede dormir (por ejemplo, un filtro de interrupción), y la tarea se ejecuta más tarde en un thread de trabajo dedicado donde el bloqueo y la espera están permitidos. El kernel incluye un pequeño conjunto de taskqueues de sistema (`taskqueue_swi`, `taskqueue_thread`, `taskqueue_fast`) y permite crear los propios.
- **Las taskqueues agrupadas (`gtaskqueue`)** amplían `taskqueue` con afinidad de CPU y reequilibrio; se usan de forma intensiva en `iflib` y en la pila de red de alto rendimiento. Las declaraciones se encuentran en `/usr/src/sys/sys/gtaskqueue.h`.
- **`callout(9)`** es el temporizador de disparo único y periódico del kernel. Un driver arma un callout con un instante futuro y recibe un callback cuando ese instante llega. `callout(9)` sustituye casi cualquier bucle de "esperar N ticks" que un driver pudiera escribir de otra forma.
- **Puntos de extensión del subsistema al estilo `hooks(9)`.** Varios subsistemas de FreeBSD publican APIs de registro que se comportan como eventhandlers pero son específicos del subsistema (por ejemplo, los filtros de paquetes se registran con `pfil(9)`; los drivers de disco pueden registrarse en eventos de `disk(9)`). No existe una interfaz unificada, pero el patrón es el mismo: una lista de callbacks que el subsistema invoca en un momento bien definido.

### Puntos de contacto típicos en un driver

- `EVENTHANDLER_REGISTER(shutdown_pre_sync, mydev_shutdown, softc, SHUTDOWN_PRI_DEFAULT);` en `attach`, para que el driver vacíe el hardware antes de un reinicio; `EVENTHANDLER_DEREGISTER` en `detach`. (Las tres constantes de prioridad estándar para los ganchos de apagado son `SHUTDOWN_PRI_FIRST`, `SHUTDOWN_PRI_DEFAULT` y `SHUTDOWN_PRI_LAST`, declaradas en `/usr/src/sys/sys/eventhandler.h`.)
- `taskqueue_create("mydev", M_WAITOK, ...); taskqueue_start_threads(...);` en `attach` para crear un worker por dispositivo; `taskqueue_drain_all` y `taskqueue_free` en `detach`.
- `callout_init_mtx(&sc->sc_watchdog, &sc->sc_mtx, 0)` en `attach` para armar un watchdog; `callout_drain` en `detach`.
- Las taskqueues agrupadas son más visibles dentro de los drivers de red basados en `iflib`; un driver independiente típico rara vez las usa directamente.

### Dónde buscar en `/usr/src`

- `/usr/src/sys/sys/eventhandler.h` y `/usr/src/sys/kern/subr_eventhandler.c` para los manejadores de eventos.
- `/usr/src/sys/sys/taskqueue.h` y `/usr/src/sys/kern/subr_taskqueue.c` para las taskqueues.
- `/usr/src/sys/sys/gtaskqueue.h` y `/usr/src/sys/kern/subr_gtaskqueue.c` para las taskqueues agrupadas.
- `/usr/src/sys/sys/callout.h` y `/usr/src/sys/kern/kern_timeout.c` para los callouts.

### Páginas de manual y archivos para leer a continuación

`eventhandler(9)`, `taskqueue(9)`, `callout(9)` y la cabecera `/usr/src/sys/sys/eventhandler.h`. Echa un vistazo a `/usr/src/sys/dev/random/random_harvestq.c` para ver un driver que usa `SYSINIT` y un kproc dedicado de forma limpia; es un buen complemento al leer sobre servicios del kernel, aunque él mismo no ejercita `taskqueue(9)` ni `callout(9)`.

### Confusión habitual en principiantes

Una trampa importante: olvidar que el registro es solo la mitad del contrato. Cada `EVENTHANDLER_REGISTER` necesita un `EVENTHANDLER_DEREGISTER` en el momento correspondiente del ciclo de vida, cada `taskqueue_create` necesita un `taskqueue_free`, y cada callout armado necesita un `callout_drain` antes de liberar su memoria. Un registro que se deja sin deshacer mantiene un puntero colgante a memoria liberada; la siguiente invocación del evento provocará un crash del kernel en un subsistema que no tiene nada que ver con tu driver.

### Dónde se explica en el libro

El capítulo 13 introduce `callout(9)`. El capítulo 14 es el capítulo dedicado a las taskqueues. El capítulo 24 es el capítulo de integración con el kernel y cubre `eventhandler(9)` y la cooperación entre SYSINIT y los módulos en contexto.

### Lectura adicional

- **En este libro**: capítulo 13 (Temporizadores y trabajo diferido), capítulo 14 (Taskqueues y trabajo diferido), capítulo 24 (Integración con el kernel).
- **Páginas de manual**: `eventhandler(9)`, `taskqueue(9)`, `callout(9)`.

## Referencias cruzadas: estructuras y sus subsistemas

La tabla siguiente es la forma más rápida de asociar un tipo desconocido con su subsistema correspondiente. Úsala cuando estés leyendo código fuente de un driver, te encuentres con un nombre de struct que no reconoces y quieras saber qué sección de este apéndice abrir.

| Estructura o tipo         | Subsistema                        | Dónde se declara                                   |
| :------------------------ | :-------------------------------- | :------------------------------------------------- |
| `struct proc`, `thread`   | Proceso y thread                  | `/usr/src/sys/sys/proc.h`                          |
| `vm_map_t`                | Memoria (VM)                      | `/usr/src/sys/vm/vm.h` y `/usr/src/sys/vm/vm_map.h` |
| `vm_object_t`             | Memoria (VM)                      | `/usr/src/sys/vm/vm.h` y `/usr/src/sys/vm/vm_object.h` |
| `vm_page_t`               | Memoria (VM)                      | `/usr/src/sys/vm/vm.h` y `/usr/src/sys/vm/vm_page.h` |
| `uma_zone_t`              | Memoria (VM)                      | `/usr/src/sys/vm/uma.h`                            |
| `struct vnode`            | Archivo y VFS                     | `/usr/src/sys/sys/vnode.h`                         |
| `struct vop_vector`       | Archivo y VFS                     | generado a partir de `/usr/src/sys/kern/vnode_if.src` |
| `struct buf`              | Archivo y VFS                     | `/usr/src/sys/sys/buf.h`                           |
| `struct bio`              | Archivo y VFS (GEOM)              | `/usr/src/sys/sys/bio.h`                           |
| `struct g_provider`       | Archivo y VFS (GEOM)              | `/usr/src/sys/geom/geom.h`                         |
| `struct cdev`             | Archivo y VFS (devfs)             | `/usr/src/sys/sys/conf.h`                          |
| `struct cdevsw`           | Archivo y VFS (devfs)             | `/usr/src/sys/sys/conf.h`                          |
| `struct mbuf`, `m_tag`    | Red                               | `/usr/src/sys/sys/mbuf.h`                          |
| `if_t`, `struct ifnet`    | Red                               | `/usr/src/sys/net/if.h`, `/usr/src/sys/net/if_private.h` |
| `struct socket`           | Red                               | `/usr/src/sys/sys/socketvar.h`                     |
| `device_t`                | Infraestructura de drivers        | `/usr/src/sys/sys/bus.h`                           |
| `driver_t`, `devclass_t`  | Infraestructura de drivers        | `/usr/src/sys/sys/bus.h`                           |
| `device_method_t`         | Infraestructura de drivers (kobj) | `/usr/src/sys/sys/bus.h` (kobj en `sys/kobj.h`)    |
| `struct resource`         | Infraestructura de drivers        | `/usr/src/sys/sys/rman.h`                          |
| `SYSINIT`, `SI_SUB_*`     | Boot y módulos                    | `/usr/src/sys/sys/kernel.h`                        |
| `MODULE_VERSION`, `MODULE_DEPEND` | Boot y módulos            | `/usr/src/sys/sys/module.h`                        |
| `eventhandler_tag`        | Servicios del kernel              | `/usr/src/sys/sys/eventhandler.h`                  |
| `struct taskqueue`        | Servicios del kernel              | `/usr/src/sys/sys/taskqueue.h`                     |
| `struct callout`          | Servicios del kernel              | `/usr/src/sys/sys/callout.h`                       |

Cuando un tipo no aparece en la tabla, búscalo en `/usr/src/sys/sys/` o en `/usr/src/sys/<subsystem>/`; el comentario que acompaña a la definición suele indicar el subsistema directamente.

## Listas de comprobación para navegar por el árbol de código fuente

El árbol de código fuente de FreeBSD está organizado por responsabilidad, y una vez que conoces el patrón puedes adivinar dónde vive casi cualquier cosa. Las listas que siguen son las cinco preguntas rápidas que convierten "¿dónde está en el árbol?" en "abre este archivo".

### Cuando tienes un nombre de estructura

1. ¿Es un primitivo de bajo nivel (`proc`, `thread`, `vnode`, `buf`, `bio`, `mbuf`, `callout`, `taskqueue`, `eventhandler`)? Busca primero en `/usr/src/sys/sys/`.
2. ¿Es un tipo de VM (`vm_*`, `uma_*`)? Busca en `/usr/src/sys/vm/`.
3. ¿Es un tipo de red (`ifnet`, `if_*`, `m_tag`, `route`, `socket`, `vnet`)? Busca en `/usr/src/sys/net/`, `/usr/src/sys/netinet/` o `/usr/src/sys/netinet6/`.
4. ¿Es un tipo de dispositivo o bus (`device_t`, `driver_t`, `resource`, `rman`, `pci_*`, `usbus_*`)? Busca en `/usr/src/sys/sys/bus.h`, `/usr/src/sys/sys/rman.h` o el directorio de bus correspondiente dentro de `/usr/src/sys/dev/`.
5. ¿Es algo completamente distinto? `grep -r 'struct NAME {' /usr/src/sys/sys/ /usr/src/sys/kern/ /usr/src/sys/vm/ /usr/src/sys/net/` suele encontrarlo en una sola pasada.

### Cuando tienes un nombre de función

1. Si el nombre empieza por `vm_`, vive en `/usr/src/sys/vm/`.
2. Si empieza por `bus_`, `device_`, `driver_`, `devclass_`, `resource_`, vive en `/usr/src/sys/kern/subr_bus.c`, `/usr/src/sys/kern/subr_rman.c` o en uno de los directorios específicos de bus.
3. Si empieza por `vfs_`, `vn_` o el prefijo `VOP_`, vive en `/usr/src/sys/kern/vfs_*.c` o en uno de los sistemas de archivos bajo `/usr/src/sys/fs/`.
4. Si empieza por `g_`, pertenece a GEOM; busca en `/usr/src/sys/geom/`.
5. Si empieza por `if_`, `ether_` o `in_`, pertenece a la red; busca en `/usr/src/sys/net/` o `/usr/src/sys/netinet/`.
6. Si empieza por `kthread_`, `kproc_`, `sched_` o `proc_`, pertenece al subsistema de procesos y threads bajo `/usr/src/sys/kern/`.
7. Si empieza por `uma_` o `malloc`, está relacionado con la memoria; busca en `/usr/src/sys/vm/uma_core.c` o `/usr/src/sys/kern/kern_malloc.c`.
8. Cuando nada coincide, `grep -rl '\bFUNC_NAME\s*(' /usr/src/sys/` es más lento pero exhaustivo.

### Cuando tienes un nombre de macro

1. `SYSINIT`, `SYSUNINIT`, `SI_SUB_*`, `SI_ORDER_*`: `/usr/src/sys/sys/kernel.h`.
2. `DRIVER_MODULE`, `DEVMETHOD`, `DEVMETHOD_END`, `MODULE_VERSION`, `MODULE_DEPEND`: `/usr/src/sys/sys/bus.h` y `/usr/src/sys/sys/module.h`.
3. `EVENTHANDLER_*`: `/usr/src/sys/sys/eventhandler.h`.
4. `VNET_*`, `CURVNET_*`: `/usr/src/sys/net/vnet.h`.
5. `TAILQ_*`, `LIST_*`, `STAILQ_*`, `SLIST_*`: `/usr/src/sys/sys/queue.h`.
6. `VOP_*`: generado a partir de `/usr/src/sys/kern/vnode_if.src`, visible en `sys/vnode_if.h` una vez compilado el kernel.

### Cuando tienes una pregunta sobre un subsistema

1. ¿Qué inicializa el kernel y en qué orden? `/usr/src/sys/kern/init_main.c`.
2. ¿Qué drivers contiene el árbol? `ls /usr/src/sys/dev/` y sus subdirectorios.
3. ¿Dónde están los puntos de entrada de la pila de red? `/usr/src/sys/net/if.c`, `/usr/src/sys/netinet/` y sus archivos hermanos.
4. ¿Cómo llega una syscall concreta hasta un driver? Comienza en `/usr/src/sys/kern/syscalls.master`, sigue el dispatcher hasta el código VFS o de sockets correspondiente, y sigue leyendo hasta que el despacho aterrice en un `cdevsw`, un `vop_vector` o un `ifnet`.

## Páginas de manual e itinerario de lectura del código fuente

El reconocimiento de patrones en el kernel llega leyéndolo, no solo leyendo sobre él. Un plan de autoestudio que cubra los subsistemas de este apéndice podría tener este aspecto:

1. `intro(9)` más un repaso de los nombres de archivo de `/usr/src/sys/sys/`, quince minutos en total.
2. `kthread(9)`, `kproc(9)` y `/usr/src/sys/sys/proc.h`.
3. `malloc(9)`, `uma(9)`, `bus_dma(9)` y `/usr/src/sys/vm/uma.h`.
4. `vnode(9)`, `cdev(9)`, `make_dev(9)`, `devfs(4)` y `/usr/src/sys/dev/null/null.c`.
5. `mbuf(9)`, `ifnet(9)`, `ether_ifattach(9)` y `/usr/src/sys/net/if_tuntap.c`.
6. `device(9)`, `DRIVER_MODULE(9)`, `bus_alloc_resource(9)` y `/usr/src/sys/dev/led/led.c`.
7. `SYSINIT(9)`, `kld(9)`, `module(9)` y el inicio de `/usr/src/sys/kern/init_main.c`.
8. `eventhandler(9)`, `taskqueue(9)`, `callout(9)` y `/usr/src/sys/dev/random/random_harvestq.c`.

Los archivos complementarios en `examples/appendices/appendix-e-navigating-freebsd-kernel-internals/` recogen el mismo itinerario en un formato que puedes imprimir, anotar y tener junto a la máquina.

## Cerrando: cómo seguir explorando el kernel con seguridad

Explorar un árbol de código fuente del kernel puede parecer interminable, y es fácil perder un fin de semana persiguiendo un hilo interesante a través de diez subsistemas. Un pequeño conjunto de hábitos mantiene la exploración productiva.

Lee en sesiones cortas con una pregunta concreta. 'Qué hace realmente `bus_setup_intr` por dentro' es una buena sesión. 'Leer la VM' no lo es.

Mantén el mapa a la vista. Cuando saltes de un driver a la VFS, recuérdate que estás en la VFS y que se aplican sus reglas. Cuando vuelvas al driver, recuérdate que la VFS se detuvo en el límite de la función. Cada subsistema tiene sus propios invariantes y su propia disciplina de locking, y raramente se transfieren de uno a otro.

Anota lo que encuentres. Una nota breve como '`bus_alloc_resource_any` en subr_bus.c llama a `BUS_ALLOC_RESOURCE` mediante el despacho kobj, que el método del bus PCI implementa en `pci.c`' vale más que una tarde de lectura pasiva. El apéndice y sus archivos complementarios están ahí para darte puntos de anclaje para exactamente este tipo de toma de notas.

Usa los raíles de seguridad. `/usr/src/sys/dev/null/null.c` y `/usr/src/sys/dev/led/led.c` son diminutos. `/usr/src/sys/net/if_tuntap.c` es lo bastante pequeño para leerlo en una sentada. `/usr/src/sys/dev/random/random_harvestq.c` usa servicios reales del kernel sin ocultarlos tras capas de abstracción. Parte de estos archivos siempre que un subsistema te parezca demasiado grande para abordarlo directamente.

Y recuerda que el objetivo no es memorizar el kernel. Es desarrollar suficiente reconocimiento de patrones como para que, la próxima vez que abras un driver desconocido o un nuevo subsistema, las estructuras, las funciones y las rutas del código fuente te resulten como barrios por los que ya has paseado. Este apéndice, junto con los apéndices A hasta D y los capítulos que enseñan las piezas en contexto, está diseñado para que esa sensación llegue antes.

Cuando el mapa de aquí no sea suficiente, lo será el libro. Cuando el libro no sea suficiente, lo será el código fuente. Y el código fuente ya está en tu máquina FreeBSD, esperando ser leído.
