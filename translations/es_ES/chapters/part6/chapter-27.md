---
title: "Trabajo con dispositivos de almacenamiento y la capa VFS"
description: "Desarrollo de drivers de dispositivos de almacenamiento e integración con VFS"
partNumber: 6
partName: "Writing Transport-Specific Drivers"
chapter: 27
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 225
language: "es-ES"
---
# Trabajar con dispositivos de almacenamiento y la capa VFS

## Introducción

En el capítulo anterior, recorrimos con detenimiento el ciclo de vida de un driver USB serie. Seguimos el dispositivo desde el momento en que el kernel lo detectó en el bus, a través de probe y attach, hasta su vida activa como dispositivo de caracteres, y finalmente hasta su detach cuando el hardware fue desconectado. Ese recorrido nos enseñó cómo viven los drivers específicos de transporte dentro de FreeBSD. Participan en un bus, exponen una abstracción orientada al usuario y aceptan que pueden desaparecer en cualquier momento porque el hardware subyacente es extraíble.

Los drivers de almacenamiento viven en un país muy diferente. El hardware sigue siendo real, y muchos dispositivos de almacenamiento también pueden extraerse de forma inesperada, pero el papel del driver cambia de manera importante. Un adaptador USB serie ofrece un flujo de bytes a un proceso a la vez. Un dispositivo de almacenamiento ofrece una superficie direccionable por bloques, duradera y estructurada, sobre la que se construyen sistemas de archivos. Cuando un usuario conecta un adaptador USB serie, puede abrir `/dev/cuaU0` inmediatamente y comenzar una sesión. Cuando un usuario conecta un disco, rara vez lo lee como un flujo sin procesar. Lo monta, y a partir de ese momento el disco desaparece detrás de un sistema de archivos, detrás de una caché, detrás de la capa del Sistema de Archivos Virtual, y detrás de los muchos procesos que comparten archivos en él.

Este capítulo te enseña lo que ocurre en el lado del driver de ese esquema. Aprenderás qué es la capa VFS, en qué se diferencia de `devfs`, y cómo los drivers de almacenamiento se conectan al framework GEOM en lugar de hablar directamente con la capa VFS. Escribirás un pequeño pseudo dispositivo de bloques desde cero, lo expondrás como un proveedor GEOM, le darás un almacén de respaldo funcional, observarás cómo `newfs_ufs` lo formatea, montarás el resultado, crearás archivos en él, lo desmontarás limpiamente y lo desconectarás sin dejar rastro en el kernel. Al final del capítulo tendrás un modelo mental funcional de la pila de almacenamiento y un driver de ejemplo concreto que ejercita todas las capas que tratamos.

El capítulo es extenso porque el tema es multicapa. A diferencia de un driver de caracteres, donde la unidad principal de interacción es una sola llamada `read` o `write` de un proceso, un driver de almacenamiento vive dentro de una cadena de frameworks. Las solicitudes viajan desde un proceso a través de VFS, a través del buffer cache, a través del sistema de archivos, a través de GEOM, y solo entonces llegan al driver. Las respuestas viajan en sentido contrario. Entender esa cadena es esencial antes de escribir cualquier código de almacenamiento real, y lo es también cuando se diagnostican los tipos de fallos sutiles que solo aparecen bajo carga o durante el desmontaje. Avanzaremos despacio a través de los fundamentos y luego iremos incorporando más capas progresivamente.

Como en el Capítulo 26, el objetivo aquí no es entregar un driver de bloques para producción. El objetivo es darte un primer driver de bloques sólido, correcto y legible que entiendas completamente. Los drivers de almacenamiento de producción reales, para discos SATA, unidades NVMe, controladores SCSI, tarjetas SD o dispositivos de bloques virtuales, se construyen sobre los mismos patrones. Una vez que los fundamentos son claros, el paso de lo pseudo a lo real es en su mayor parte cuestión de reemplazar el almacén de respaldo con código que habla con registros de hardware y motores DMA, y de gestionar la superficie de error y recuperación mucho más rica que exponen los discos reales.

También verás cómo los drivers de almacenamiento interactúan con herramientas que el lector ya conoce del lado del usuario de FreeBSD. `mdconfig(8)` aparecerá como un pariente cercano de nuestro driver, ya que el RAM disk `md(4)` del kernel es exactamente el tipo de cosa que estamos construyendo. `newfs_ufs(8)`, `mount(8)`, `umount(8)`, `diskinfo(8)`, `gstat(8)` y `geom(8)` se convertirán en herramientas de verificación, no solo en herramientas que otras personas usan. El capítulo está estructurado de modo que, cuando termines, puedas leer la salida de `gstat -I 1` mientras ejecutas `dd` contra tu dispositivo y entenderla con pleno conocimiento.

Por último, una nota sobre lo que no cubriremos aquí. No escribiremos un driver de bus real que hable con un controlador de almacenamiento físico. No trataremos los internos de UFS, ZFS, FUSE ni otros sistemas de archivos específicos más allá de lo necesario para entender cómo se encuentran con un dispositivo de bloques en la frontera. No cubriremos DMA, PCIe, colas NVMe ni conjuntos de comandos SCSI. Todos esos temas merecen su propio tratamiento y, donde sea relevante, aparecerán en capítulos posteriores que cubren buses específicos y subsistemas específicos. Lo que haremos aquí es darte una experiencia completa y autocontenida con la capa de bloques, representativa de cómo todos los drivers de almacenamiento en FreeBSD se integran con el kernel.

Tómate tu tiempo con este capítulo. Lee despacio, escribe el código, carga el módulo, fórmatealo, móntalo, rómpelo a propósito, observa lo que ocurre. La pila de almacenamiento premia la paciencia y castiga los atajos. No hay prisa.

## En qué se diferencia la Parte 6 de las Partes 1 a 5

Una breve nota de encuadre antes de que comience el capítulo. El Capítulo 27 se sitúa dentro de una Parte que te pide cambiar un hábito específico, y ese cambio es más fácil de asimilar cuando se nombra desde el principio.

Las Partes 1 a 5 construyeron un único driver en ejecución, `myfirst`, a lo largo de veinte capítulos consecutivos, cada uno añadiendo una disciplina al mismo árbol de código fuente. El Capítulo 26 extendió esa familia con `myfirst_usb` como variante de transporte, para que el paso al hardware real no fuera también un paso hacia código fuente desconocido. **A partir del Capítulo 27, el driver `myfirst` en ejecución hace una pausa como columna vertebral del libro.** La Parte 6 da paso a demos nuevas y autocontenidas que se adaptan a cada subsistema que enseña: un pseudo dispositivo de bloques para almacenamiento aquí en el Capítulo 27, y una pseudo interfaz de red para redes en el Capítulo 28. Estas demos son paralelas a `myfirst` en espíritu, pero distintas en código, porque los patrones que definen un driver de almacenamiento o un driver de red no encajan en el molde de dispositivo de caracteres del que creció `myfirst`.

La **disciplina y la forma didáctica continúan sin cambios**. Cada capítulo sigue guiándote a través de probe, attach, la ruta de datos principal, la ruta de limpieza, los laboratorios, los ejercicios de desafío, la resolución de problemas y un puente hacia el siguiente capítulo. Cada capítulo sigue fundamentando sus ejemplos en código fuente real de FreeBSD bajo `/usr/src`. Los hábitos que construiste en los Capítulos 25 y anteriores, la cadena de limpieza con goto etiquetado, el logging con limitación de frecuencia, `INVARIANTS` y `WITNESS`, la lista de verificación de preparación para producción, se mantienen sin modificación. Lo que cambia es el artefacto de código frente a ti: un driver pequeño y enfocado cuya forma se ajusta al subsistema bajo estudio, en lugar de una etapa más en la línea temporal de `myfirst`.

Esta es una elección didáctica deliberada, no un accidente de alcance. Un driver de almacenamiento y un driver de red tienen cada uno su propio ciclo de vida, su propio flujo de datos, sus propios idiomas preferidos y sus propios frameworks a los que conectarse. Enseñarlos como drivers nuevos, en lugar de como mutaciones adicionales de `myfirst`, mantiene el foco en lo que hace distintivo a cada subsistema. Un lector que intenta convertir `myfirst` en un dispositivo de bloques o en una interfaz de red acaba rápidamente con código que no enseña nada sobre almacenamiento o redes. Las demos nuevas son el camino más limpio, y es el camino que toma esta Parte.

La Parte 7 regresa al aprendizaje acumulativo, pero en lugar de retomar un único driver en ejecución, revisita los drivers que ya has escrito (`myfirst`, `myfirst_usb` y las demos de la Parte 6) y enseña los temas orientados a producción que importan una vez que existe una primera versión de un driver: portabilidad entre arquitecturas, depuración avanzada, ajuste de rendimiento, revisión de seguridad y contribución al proyecto upstream. El hábito de construir de forma acumulativa permanece contigo; solo cambia el artefacto concreto frente a ti.

Ten presente este encuadre a medida que se despliega el Capítulo 27. Si el cambio de `myfirst` a un nuevo pseudo dispositivo de bloques te resulta desconcertante después de veinte capítulos con el mismo árbol de código fuente, esa reacción es normal y desaparece rápido, por lo general antes de terminar la Sección 3.

## Guía para el lector: cómo usar este capítulo

Este capítulo está diseñado como un curso guiado por el lado del almacenamiento del kernel de FreeBSD. Es uno de los capítulos más largos del libro porque el tema es multicapa y cada capa tiene su propio vocabulario, sus propias preocupaciones y sus propios modos de fallo. No hay necesidad de apresurarse.

Si eliges la **ruta de solo lectura**, espera dedicar unas dos o tres horas a recorrer el capítulo con detenimiento. Saldrás con una imagen clara de cómo encajan VFS, el buffer cache, los sistemas de archivos, GEOM y la frontera del dispositivo de bloques, y tendrás un driver concreto frente a ti como ancla para tu modelo mental. Esta es una forma legítima de usar el capítulo, especialmente en una primera lectura.

Si eliges la **ruta de lectura más laboratorios**, planifica de cuatro a seis horas repartidas entre una o dos tardes, dependiendo de tu comodidad con los módulos del kernel del Capítulo 26. Construirás el driver, lo formatearás, lo montarás, lo observarás bajo carga y lo desmontarás de forma segura. Espera que la mecánica de `kldload`, `kldunload`, `newfs_ufs` y `mount` se vuelva algo natural al final.

Si eliges la **ruta de lectura más laboratorios más desafíos**, planifica un fin de semana o dos tardes repartidas a lo largo de una semana. Los desafíos amplían el driver en pequeñas direcciones que importan en la práctica: añadir semántica de vaciado opcional, responder a `BIO_DELETE` con puesta a cero, dar soporte a múltiples unidades, exportar atributos adicionales a través de `disk_getattr`, y hacer cumplir el modo de solo lectura de forma limpia. Cada desafío es autocontenido y usa únicamente lo que el capítulo ya ha cubierto.

Sea cual sea el camino que elijas, no te saltes la sección de resolución de problemas. Los bugs de almacenamiento tienden a parecerse desde fuera, y la capacidad de reconocerlos por sus síntomas es mucho más útil en la práctica que memorizar los nombres de cada función de GEOM. El material de resolución de problemas está ubicado cerca del final por razones de legibilidad, pero puede que te descubras volviendo a él mientras trabajas en los laboratorios.

Una nota sobre los requisitos previos. Este capítulo se construye directamente sobre el Capítulo 26, así que como mínimo deberías sentirte cómodo escribiendo un pequeño módulo del kernel, declarando un softc, asignando y liberando recursos, y recorriendo la ruta de carga y descarga. También deberías sentirte suficientemente cómodo con la shell para ejecutar `kldload`, `kldstat`, `dmesg`, `mount` y `umount` sin tener que buscar las opciones. Si algo de eso te resulta desconocido, vale la pena revisar los Capítulos 5, 14 y 26 antes de continuar.

Deberías trabajar en un sistema FreeBSD 14.3 de pruebas, una máquina virtual o una rama donde no te importe un posible kernel panic. Un panic es improbable si sigues el texto con cuidado, pero el coste de un error en tu portátil de desarrollo es mucho mayor que el coste de un error en una instantánea de VM que puedes revertir. Lo hemos dicho antes y lo seguiremos diciendo: el trabajo con el kernel es seguro cuando se trabaja en un lugar seguro.

### Trabaja sección por sección

El capítulo está organizado como una progresión. La Sección 1 presenta VFS. La Sección 2 contrasta `devfs` con VFS y sitúa nuestro driver en ese contraste. La Sección 3 registra un pseudo dispositivo de bloques mínimo. La Sección 4 lo expone como un proveedor GEOM. La Sección 5 implementa las rutas de lectura y escritura reales. La Sección 6 monta un sistema de archivos encima. La Sección 7 da persistencia al dispositivo. La Sección 8 enseña el desmontaje seguro y la limpieza. La Sección 9 habla de refactorización, versiones y qué hacer a medida que el driver crece.

Están pensadas para leerse en orden. Cada sección da por sentado que las anteriores están frescas en tu memoria, y los laboratorios se construyen unos sobre otros. Si saltas al medio, las piezas parecerán extrañas.

### Escribe el código

Escribir el código a mano sigue siendo la forma más efectiva de interiorizar los idiomas del kernel. Los archivos de acompañamiento bajo `examples/part-06/ch27-storage-vfs/` existen para que puedas comprobar tu trabajo, no para que te saltes la escritura. Leer código no es lo mismo que escribirlo.

### Abre el árbol de código fuente de FreeBSD

A lo largo de este capítulo se te pedirá que abras archivos del árbol de código fuente real de FreeBSD, no solo los ejemplos complementarios. Los archivos de interés son `/usr/src/sys/geom/geom.h`, `/usr/src/sys/sys/bio.h`, `/usr/src/sys/geom/geom_disk.h`, `/usr/src/sys/dev/md/md.c` y `/usr/src/sys/geom/zero/g_zero.c`. Cada uno de ellos es una referencia primaria, y el texto de este capítulo volverá a ellos con frecuencia. Si todavía no has clonado ni instalado el árbol de código fuente de la versión 14.3, este es un buen momento para hacerlo.

### Usa tu cuaderno de laboratorio

Mantén abierto tu cuaderno de laboratorio del Capítulo 26 mientras trabajas. Querrás registrar la salida de `gstat -I 1`, los mensajes que emite `dmesg` cuando cargas y descargas el módulo, el tiempo que tarda el formateo del dispositivo y cualquier advertencia o panic que observes. El trabajo en el kernel es mucho más sencillo cuando tomas notas, porque muchos síntomas parecen similares a primera vista y el cuaderno te permite comparar entre sesiones.

### Ve a tu propio ritmo

Si sientes que tu comprensión se difumina en una sección concreta, detente. Léela de nuevo. Prueba un pequeño experimento con el módulo en ejecución. No te fuerces a avanzar por una sección que aún no has asimilado. Los drivers de almacenamiento castigan la confusión con más severidad que los drivers de caracteres, porque la confusión en la capa de bloques se convierte con frecuencia en corrupción del sistema de archivos en la capa superior, y la corrupción del sistema de archivos requiere tiempo y cuidado para repararse, incluso en una VM desechable.

## Cómo sacar el máximo partido a este capítulo

El capítulo está estructurado de modo que cada sección añade exactamente un concepto nuevo sobre lo que vino antes. Para aprovechar al máximo esa estructura, trata el capítulo como un taller práctico, no como una referencia. No estás aquí para encontrar una respuesta rápida. Estás aquí para construir un modelo mental correcto.

### Trabaja por secciones

No leas el capítulo entero de principio a fin sin detenerte. Lee una sección y luego haz una pausa. Prueba el experimento o laboratorio que la acompaña. Consulta el código fuente de FreeBSD relacionado. Escribe unas líneas en tu cuaderno. Solo entonces avanza. La programación de almacenamiento en el kernel es fuertemente acumulativa, y saltarse secciones suele significar que te confundirás con el siguiente tema por un motivo que se explicó dos secciones antes.

### Mantén el driver en ejecución

Una vez que hayas cargado el driver en la Sección 3, mantenlo cargado tanto como sea posible mientras lees. Modifícalo, recárgalo, inspecciónalo con `gstat`, ejecuta `dd` contra él, llama a `diskinfo` sobre él. Tener un ejemplo vivo y observable es mucho más valioso que cualquier cantidad de lectura. Notarás cosas que ningún capítulo podría contarte jamás, porque ningún capítulo puede mostrarte tiempos reales, jitter real ni casos límite reales en tu configuración particular.

### Consulta las páginas de manual

Las páginas de manual de FreeBSD forman parte del material didáctico, no son un formalismo aparte. La sección 9 del manual es donde viven las interfaces del kernel. Haremos referencia varias veces a páginas como `g_bio(9)`, `geom(4)`, `DEVICE_IDENTIFY(9)`, `disk(9)`, `bus_dma(9)` y `devstat(9)`. Léelas junto a este capítulo. Son más breves de lo que parecen y las ha escrito la misma comunidad que escribió el kernel en el que estás trabajando.

### Escribe el código y luego modifícalo

Cuando construyas el driver a partir de los ejemplos complementarios, escríbelo primero tú mismo. Una vez que funcione, empieza a cambiar cosas. Cambia el nombre de un método y observa cómo falla el build. Elimina una rama `if` y observa qué ocurre cuando cargas el módulo. Fija un tamaño de medio más pequeño en el código y observa cómo reacciona `newfs_ufs`. El código del kernel se comprende mejor a través de la mutación deliberada que a través de la lectura pura.

### Confía en las herramientas

FreeBSD te ofrece una gran cantidad de herramientas para inspeccionar la pila de almacenamiento: `geom`, `gstat`, `diskinfo`, `dd`, `mdconfig`, `dmesg`, `kldstat`, `sysctl`. Úsalas. Cuando algo va mal, el primer movimiento casi nunca es leer más código fuente. Es preguntar al sistema en qué estado se encuentra. `geom disk list` y `geom part show` suelen ser más informativos que cinco minutos de grep.

### Tómate descansos

El trabajo en el kernel es cognitivamente denso. Dos o tres horas de trabajo concentrado suelen ser más productivas que un sprint de siete horas. Si te sorprendes cometiendo el mismo error tipográfico tres veces, o copiando y pegando sin leer, esa es tu señal para levantarte durante diez minutos.

Con esos hábitos establecidos, comencemos.

## Sección 1: ¿Qué es la capa del Sistema de Archivos Virtual?

Cuando un proceso abre un archivo en FreeBSD, llama a `open(2)` con una ruta. Esa ruta puede resolverse en un archivo en UFS, un archivo en ZFS, un archivo en un recurso compartido NFS montado remotamente, un pseudo-archivo en `devfs`, un archivo en `procfs`, o incluso un archivo dentro de un sistema de archivos de userland montado con FUSE. El proceso no puede saberlo. El proceso recibe un descriptor de archivo y luego lee y escribe como si solo existiera un tipo de archivo en el mundo. Esa uniformidad no es un accidente. Es la obra de la capa del Sistema de Archivos Virtual.

### El problema que resuelve VFS

Antes de VFS, los kernels de UNIX generalmente solo sabían comunicarse con un único sistema de archivos. Si querías un nuevo sistema de archivos, modificabas los caminos de código para `open`, `read`, `write`, `stat`, `unlink`, `rename` y cualquier otra llamada al sistema que tocara archivos. Ese enfoque funcionó durante un tiempo, pero no escalaba. Llegaron nuevos sistemas de archivos: NFS, para acceso remoto. MFS, para espacio temporal en memoria. Procfs, para exponer el estado de los procesos. ISO 9660, para medios CD-ROM. FAT, para interoperabilidad. Cada adición suponía nuevas bifurcaciones en cada llamada al sistema relacionada con archivos.

Sun Microsystems introdujo la arquitectura del Sistema de Archivos Virtual a mediados de los años 80 como salida a este caos. La idea es sencilla. El kernel habla con una única interfaz abstracta, definida en términos de operaciones genéricas sobre objetos de archivo genéricos. Cada sistema de archivos concreto registra implementaciones de esas operaciones, y el kernel las invoca a través de punteros a función. Cuando el kernel necesita leer un archivo, no sabe ni le importa si ese archivo reside en UFS, NFS o ZFS. Sabe que hay un nodo con un método `VOP_READ`, y llama a ese método.

FreeBSD adoptó esta arquitectura y la ha extendido significativamente a lo largo de las décadas. El resultado es que añadir un sistema de archivos a FreeBSD ya no requiere modificar las llamadas al sistema centrales. Un sistema de archivos es un módulo del kernel independiente que registra un conjunto de operaciones con VFS, y desde ese momento VFS encamina las solicitudes adecuadas hacia él.

### El modelo de objetos de VFS

VFS define tres tipos principales de objetos.

El primero es el **punto de montaje**, representado en el kernel por `struct mount`. Cada sistema de archivos montado tiene uno, y registra dónde está anclado el sistema de archivos en el espacio de nombres, qué indicadores tiene y qué código de sistema de archivos es responsable de él.

El segundo es el **vnode**, representado por `struct vnode`. Un vnode es el manejador del kernel para un único archivo o directorio dentro de un sistema de archivos montado. No es el archivo en sí. Es la representación en tiempo de ejecución que el kernel mantiene de ese archivo mientras algo en el kernel tenga interés en él. Todo archivo que un proceso tenga abierto tiene un vnode. Todo directorio que el kernel esté recorriendo tiene un vnode. Cuando nada mantiene una referencia a un vnode, este puede ser reclamado, y el kernel mantiene un pool de ellos para evitar la presión de asignación en casos de inodos pequeños.

El tercero es el **vector de operaciones de vnode**, representado por `struct vop_vector`, que enumera las operaciones que cada sistema de archivos debe implementar sobre vnodes. Las operaciones tienen nombres como `VOP_LOOKUP`, `VOP_READ`, `VOP_WRITE`, `VOP_CREATE`, `VOP_REMOVE`, `VOP_GETATTR` y `VOP_SETATTR`. Cada sistema de archivos proporciona un puntero a su propio vector, y el kernel invoca operaciones a través de estos vectores siempre que necesita hacer algo con un archivo.

Lo elegante de este diseño es que, desde el lado de las llamadas al sistema del kernel, solo importa la interfaz abstracta. La capa de llamadas al sistema invoca `VOP_READ(vp, uio, ioflag, cred)` y no le importa si `vp` pertenece a UFS, ZFS, NFS o tmpfs. Desde el lado del sistema de archivos, la interfaz abstracta también es lo único que importa. UFS implementa las operaciones de vnode y nunca ve el código de las llamadas al sistema.

### El lugar de los drivers de almacenamiento

Esta es la pregunta que importa para este capítulo. Si VFS es donde viven los sistemas de archivos, ¿dónde viven los drivers de almacenamiento?

La respuesta es: no directamente dentro de VFS. Un driver de almacenamiento no implementa `VOP_READ`. Implementa una abstracción de mucho más bajo nivel que se parece a un disco. Los sistemas de archivos se sitúan por encima, consumiendo esa abstracción de tipo disco, traduciendo operaciones a nivel de archivo en operaciones a nivel de bloque, y llamando hacia abajo.

La cadena de capas entre un proceso y un dispositivo de bloques en FreeBSD tiene típicamente este aspecto.

```text
       +------------------+
       |   user process   |
       +--------+---------+
                |
                |  read(fd, buf, n)
                v
       +--------+---------+
       |   system calls   |  sys_read, sys_write, sys_open, ...
       +--------+---------+
                |
                v
       +--------+---------+
       |       VFS        |  vfs_read, VOP_READ, vnode cache
       +--------+---------+
                |
                v
       +--------+---------+
       |    filesystem    |  UFS, ZFS, NFS, tmpfs, ...
       +--------+---------+
                |
                v
       +--------+---------+
       |   buffer cache   |  bufcache, bwrite, bread, getblk
       +--------+---------+
                |
                |  struct bio
                v
       +--------+---------+
       |      GEOM        |  classes, providers, consumers
       +--------+---------+
                |
                v
       +--------+---------+
       |  storage driver  |  disk_strategy, bio handler
       +--------+---------+
                |
                v
       +--------+---------+
       |    hardware      |  real disk, SSD, or memory buffer
       +------------------+
```

Cada capa de esta pila tiene una tarea. VFS oculta las diferencias entre sistemas de archivos a las llamadas al sistema. El sistema de archivos traduce archivos en bloques. El buffer cache retiene en RAM los bloques usados recientemente. GEOM encamina las solicitudes de bloques a través de transformaciones, particiones y espejos. El driver de almacenamiento convierte las solicitudes de bloques en I/O real. El hardware hace el trabajo.

En este capítulo, casi todo lo que hacemos ocurre en las dos capas inferiores: GEOM y el driver de almacenamiento. Tocaremos brevemente la capa del sistema de archivos cuando montemos UFS en nuestro dispositivo, y tocaremos VFS solo en el sentido de que `mount(8)` la invoca. Las capas por encima de GEOM no son nuestro código.

### VFS en el código fuente del kernel

Si quieres ver VFS directamente, los puntos de entrada están en `/usr/src/sys/kern/vfs_*.c`. La capa de vnode reside en `vfs_vnops.c` y `vfs_subr.c`. El lado del montaje reside en `vfs_mount.c`. El vector de operaciones de vnode se define y gestiona en `vfs_default.c`. UFS, nuestro sistema de archivos principal en este capítulo, reside en `/usr/src/sys/ufs/ufs/` y `/usr/src/sys/ufs/ffs/`. No necesitas leer ninguno de esos archivos para seguir este capítulo. Debes saber dónde están para entender qué hay por encima del código que estás a punto de escribir.

### Qué significa esto para nuestro driver

Como VFS no es nuestro llamador directo, no necesitamos implementar métodos `VOP_`. Necesitamos implementar la interfaz de la capa de bloques que el sistema de archivos finalmente invoca. Esa interfaz está definida por GEOM y, en particular para los dispositivos de tipo disco, por el subsistema `g_disk`. Nuestro driver expondrá un proveedor GEOM. Un sistema de archivos lo consumirá. El flujo de I/O pasará por `struct bio` en lugar de por `struct uio`, y la unidad de trabajo será un bloque en lugar de un rango de bytes.

Por eso también los drivers de almacenamiento rara vez interactúan directamente con `cdevsw` o `make_dev` como hacen los drivers de caracteres. El nodo `/dev` de un disco lo crea GEOM, no el driver. El driver se describe a sí mismo a GEOM, y GEOM publica un proveedor, que luego aparece en `/dev` con un nombre generado automáticamente.

### La cadena de llamadas de VFS en la práctica

Sigamos el rastro de lo que ocurre cuando un usuario ejecuta `cat /mnt/myfs/hello.txt`, suponiendo que `/mnt/myfs` está montado en nuestro futuro dispositivo de bloques.

Primero, el proceso llama a `open("/mnt/myfs/hello.txt", O_RDONLY)`. Esto llega a `sys_openat` en la capa de llamadas al sistema, que pide a VFS que resuelva la ruta. VFS recorre la ruta componente a componente, llamando a `VOP_LOOKUP` sobre cada vnode de directorio. Cuando llega a `myfs`, detecta que el vnode es un punto de montaje y cruza hacia el sistema de archivos montado. Finalmente llega al vnode de `hello.txt` y devuelve un descriptor de archivo.

Segundo, el proceso llama a `read(fd, buf, 64)`. Esto llega a `sys_read`, que llama a `vn_read`, que llama a `VOP_READ` sobre el vnode. La implementación de `VOP_READ` en UFS consulta su inodo, determina qué bloques de disco contienen los bytes solicitados y pide esos bloques al buffer cache. Si los bloques no están en caché, el buffer cache llama a `bread`, que en última instancia construye un `struct bio` y lo entrega a GEOM.

Tercero, GEOM examina el proveedor que el sistema de archivos está consumiendo. A través de una cadena de proveedores y consumidores, el `bio` llega al proveedor inferior, que es el proveedor de nuestro driver. Nuestra función de estrategia recibe el `bio`, lee los bytes solicitados de nuestro almacenamiento de respaldo y llama a `biodone` o `g_io_deliver` para completar la solicitud.

Cuarto, la respuesta viaja de vuelta en sentido contrario. El buffer cache obtiene sus datos, el sistema de archivos vuelve a `vn_read`, `vn_read` copia los datos en el buffer del usuario, y `sys_read` retorna.

Nada de ese código es nuestro excepto el último salto. Pero entender la cadena completa es lo que te permite tomar decisiones de diseño sensatas cuando escribes ese último salto.

### Cerrando la Sección 1

VFS es la capa que unifica los sistemas de archivos en FreeBSD. Se sitúa entre la interfaz de llamadas al sistema y los distintos sistemas de archivos concretos, y proporciona la abstracción que hace que los archivos parezcan idénticos independientemente de dónde residan. Los drivers de almacenamiento no viven dentro de VFS. Viven en la parte inferior de la pila, muy por debajo de VFS, detrás de GEOM y el buffer cache. Nuestro trabajo en este capítulo es escribir un driver que participe correctamente en esa capa inferior, y entender lo suficiente sobre las capas superiores para evitar confusiones al diagnosticar problemas.

En la siguiente sección, afinaremos la distinción entre `devfs` y VFS, porque esa distinción determina qué modelo mental se aplica cuando piensas en un nodo de dispositivo concreto.

## Sección 2: devfs frente a VFS

Los principiantes suelen asumir que `devfs` y la capa del sistema de archivos virtual son dos nombres para la misma cosa. No lo son. Están relacionados, pero desempeñan papeles muy distintos. Tener clara esta distinción desde el principio evita mucha confusión más adelante, especialmente cuando se piensa en drivers de almacenamiento, porque los drivers de almacenamiento se sitúan en ambos lados.

### Qué es devfs

`devfs` es un sistema de archivos. Puede parecer circular, pero es cierto. `devfs` está implementado como un módulo de sistema de archivos, registrado en VFS y montado en `/dev` en todo sistema FreeBSD. Cuando lees un archivo en `/dev`, lo haces a través de VFS, que pasa la petición a `devfs`, el cual reconoce que el «archivo» que estás leyendo es en realidad un nodo de dispositivo del kernel y enruta la llamada al driver correspondiente.

`devfs` tiene varias propiedades especiales que lo distinguen de un sistema de archivos ordinario como UFS.

En primer lugar, su contenido no se almacena en disco. Los «archivos» en `devfs` son sintetizados por el kernel en función de qué drivers están cargados en ese momento y qué dispositivos están presentes. Cuando un driver llama a `make_dev(9)` para crear `/dev/mybox`, `devfs` añade el nodo correspondiente a su vista. Cuando el driver destruye ese dispositivo con `destroy_dev(9)`, `devfs` elimina el nodo. El usuario ve cómo `/dev/mybox` aparece y desaparece en tiempo real.

En segundo lugar, las rutas de lectura y escritura de los nodos de `devfs` no son rutas de datos de archivo. Cuando escribes en `/dev/myserial0`, no estás añadiendo bytes a un archivo almacenado. Estás invocando la función `d_write` del driver a través de `cdevsw`, y esa función decide qué significan esos bytes. En el caso de un driver serie USB, significan bytes a transmitir por el cable. En el caso de un pseudodispositivo como `/dev/null`, significan bytes a descartar.

En tercer lugar, los metadatos de los nodos de `devfs`, como permisos y propietario, son gestionados por una capa de políticas en el kernel en lugar de por el propio sistema de archivos. `devfs_ruleset(8)` y el framework `devd` configuran esa política.

En cuarto lugar, `devfs` admite clonación (cloning), que los drivers de caracteres como `pty`, `tun` y `bpf` utilizan para crear un nuevo dispositivo menor cada vez que un proceso abre el nodo. Así es como `/dev/ptyp0`, `/dev/ptyp1` y sus sucesores cobran existencia bajo demanda.

### Qué es VFS

VFS, como vimos en la sección 1, es la capa abstracta del sistema de archivos. Todos los sistemas de archivos de un sistema FreeBSD, incluido `devfs`, están registrados en VFS e invocados a través de VFS. VFS no es un sistema de archivos. Es el framework en el que los sistemas de archivos se conectan.

Cuando abres un archivo en UFS, la cadena es: llamada al sistema -> VFS -> UFS -> buffer cache -> GEOM -> driver. Cuando abres un nodo en `devfs`, la cadena es: llamada al sistema -> VFS -> devfs -> driver. Ambas pasan por VFS. Solo la cadena de UFS involucra a GEOM.

### Por qué los drivers de almacenamiento viven en ambos lados

Aquí es donde los drivers de almacenamiento se vuelven interesantes.

Un driver de almacenamiento expone un dispositivo de bloques, y ese dispositivo de bloques acaba apareciendo como nodo bajo `/dev`. Por ejemplo, si registramos nuestro driver e informamos a GEOM sobre él, puede aparecer en `devfs` un nodo llamado `/dev/myblk0`. Cuando un usuario ejecuta `dd if=image.iso of=/dev/myblk0`, está escribiendo a través de `devfs` hacia una interfaz de caracteres especial que GEOM proporciona sobre nuestro disco. Las peticiones fluyen como BIO a través de GEOM y llegan a nuestra función strategy.

Pero cuando un usuario ejecuta `newfs_ufs /dev/myblk0` y luego `mount /dev/myblk0 /mnt`, el patrón de uso cambia. El kernel monta ahora UFS sobre el dispositivo. Cuando un proceso lee posteriormente un archivo en `/mnt`, la ruta es: llamada al sistema -> VFS -> UFS -> buffer cache -> GEOM -> driver. El nodo `/dev/myblk0` en `devfs` ni siquiera interviene en la ruta activa (hot path). UFS y el buffer cache hablan directamente con el proveedor GEOM. El nodo de `devfs` es esencialmente un identificador que las herramientas usan para referirse al dispositivo, no la tubería por la que fluyen los datos de archivo durante el funcionamiento normal.

### El buffer cache de cerca

Entre el sistema de archivos y GEOM en la ruta de almacenamiento se encuentra el buffer cache. Lo hemos mencionado varias veces sin detenernos a describirlo. Hagamos una pausa ahora, porque explica varios de los comportamientos que observarás al probar tu driver.

El buffer cache es un conjunto de buffers de tamaño fijo en memoria del kernel, cada uno de los cuales contiene un bloque del sistema de archivos. Cuando un sistema de archivos lee un bloque, el buffer cache interviene: el sistema de archivos solicita el bloque a la caché, y esta devuelve un acierto (el bloque ya está en memoria) o registra un fallo (la caché asigna un buffer, llama hacia abajo a través de GEOM para obtener los datos y devuelve el buffer una vez completada la lectura). Cuando un sistema de archivos escribe un bloque, se aplica la misma ruta de caché a la inversa: la escritura rellena un buffer, el buffer se marca como sucio (dirty) y la caché programa la escritura diferida (write-back) en algún momento posterior.

El buffer cache es la razón por la que las lecturas consecutivas de los mismos datos de archivo no siempre llegan al driver. La primera lectura produce un fallo de caché, lo que provoca que un BIO viaje hasta el driver. La segunda lectura acierta en la caché y retorna inmediatamente. Esto es una gran ventaja para el rendimiento. Puede resultar algo desconcertante cuando depuras un driver por primera vez, porque tu `printf` en la función strategy no se dispara en cada lectura del espacio de usuario.

El buffer cache es también la razón por la que las escrituras pueden parecer más rápidas que el driver subyacente. Un `dd if=/dev/zero of=/mnt/myblk/big bs=1m count=16` puede parecer que se completa en una fracción de segundo porque las escrituras aterrizan en la caché y esta difiere los BIOs reales durante un tiempo. El sistema de archivos emite las escrituras reales a GEOM durante el siguiente segundo o dos. Si el sistema falla antes de que eso ocurra, el archivo en disco queda incompleto. `sync(2)` fuerza a la caché a volcar sus datos al dispositivo subyacente. `fsync(2)` vacía únicamente los buffers asociados a un único descriptor de archivo.

El buffer cache es distinto de la caché de páginas (page cache). FreeBSD tiene ambos y cooperan entre sí. La caché de páginas mantiene páginas de memoria que respaldan archivos mapeados en memoria y memoria anónima. El buffer cache mantiene buffers que respaldan las operaciones de bloque del sistema de archivos. El FreeBSD moderno los ha unificado en gran medida para muchas rutas de datos, pero la distinción todavía aparece en el árbol de código fuente, en particular en torno a `bread`, `bwrite`, `getblk` y `brelse`, que son el lado del buffer cache en la interfaz.

El buffer cache tiene una implicación fundamental para nuestro driver: casi nunca veremos tráfico BIO completamente síncrono. Cuando un sistema de archivos quiere leer un bloque, un BIO llega a nuestra función strategy; cuando quiere escribir un bloque, llega otro BIO, pero generalmente un tiempo después de la llamada al sistema de escritura que lo originó. Los BIOs también llegan en ráfagas cuando la caché se vacía. Esto es normal, y tu driver no debe hacer suposiciones sobre la temporización ni el orden de los BIOs más allá de lo que está estrictamente documentado. Cada BIO es una petición independiente.

### Las rutas de lectura y escritura

Tracemos un ejemplo concreto a través de toda la cadena.

Cuando un usuario ejecuta `cat /mnt/myblk/hello.txt`, el shell ejecuta `cat`, que llama a `open("/mnt/myblk/hello.txt", O_RDONLY)`. El `open` llega a `sys_openat`, que pasa el control a VFS. VFS llama a `namei` para recorrer la ruta. Para cada componente de la ruta, VFS llama a `VOP_LOOKUP` sobre el vnode del directorio actual. Cuando VFS alcanza el punto de montaje de `myblk`, cruza hacia UFS, que recorre la estructura de directorios de UFS para encontrar `hello.txt`. UFS devuelve el vnode de ese archivo y VFS devuelve un descriptor de archivo.

El usuario llama entonces a `read(fd, buf, 64)`. `sys_read` llama a `vn_read`, que llama a `VOP_READ` sobre el vnode. El `VOP_READ` de UFS consulta el inodo para encontrar la dirección de bloque de los bytes solicitados y luego llama a `bread` en el buffer cache para obtener el bloque. El buffer cache devuelve un acierto o emite un BIO.

Si se produce un fallo de caché, el buffer cache asigna un buffer nuevo, construye un BIO que solicita el bloque relevante al proveedor GEOM subyacente y lo entrega. El BIO viaja hacia abajo a través de GEOM, por nuestra función strategy, y regresa. Cuando el BIO se completa, el buffer cache desbloquea la llamada `bread` que estaba en espera. UFS copia entonces los bytes solicitados del buffer al `buf` del usuario. `read` retorna.

Para las escrituras, la cadena es simétrica pero la temporización es diferente. El `VOP_WRITE` de UFS llama a `bread` o `getblk` para obtener el buffer de destino, copia los datos del usuario en el buffer, lo marca como sucio y llama a `bdwrite` o `bawrite` para programar la escritura diferida. La llamada `write` del usuario retorna mucho antes de que el BIO se emita al driver. Más tarde, el thread sincronizador del buffer cache recoge los buffers sucios y emite peticiones BIO_WRITE al driver.

El efecto neto es que la función strategy de nuestro driver ve un flujo de BIOs que está relacionado, pero no es idéntico, con el flujo de lecturas y escrituras del espacio de usuario. El buffer cache media entre ambos.

En otras palabras, el mismo driver de almacenamiento puede alcanzarse de dos maneras distintas.

1. **Acceso directo a través de `/dev`**: un programa del espacio de usuario abre `/dev/myblk0` y emite llamadas `read(2)` o `write(2)`. Esas llamadas pasan por `devfs` y la interfaz de caracteres de GEOM, terminando en nuestra función strategy.
2. **Acceso a través de un sistema de archivos montado**: el kernel monta un sistema de archivos sobre el dispositivo. Las operaciones de E/S de archivos fluyen a través de VFS, el sistema de archivos, el buffer cache y GEOM. `devfs` no forma parte de la ruta activa para esas peticiones.

Ambas rutas convergen en el proveedor GEOM, razón por la que GEOM es la abstracción correcta para los drivers de almacenamiento, aunque los drivers de caracteres traten con `devfs` de forma más directa.

### Por qué importa esta distinción

Esto importa por dos razones.

En primer lugar, aclara por qué no usaremos `make_dev` para nuestro driver de bloques. `make_dev` es la llamada correcta para los drivers de caracteres que quieren publicar un `cdevsw` bajo `/dev`. Es la llamada incorrecta para un dispositivo de bloques, porque GEOM crea el nodo `/dev` por nosotros en cuanto publicamos un proveedor. Si llamas a `make_dev` en un driver de almacenamiento, acabarás normalmente con dos nodos `/dev` compitiendo por el mismo dispositivo, uno de los cuales no está conectado a la topología GEOM, lo que genera un comportamiento confuso.

En segundo lugar, la distinción explica por qué el kernel dispone de dos conjuntos de herramientas para inspeccionar el estado del dispositivo. `devfs_ruleset(8)`, `devfs.rules` y los permisos por nodo pertenecen a `devfs`. `geom(8)`, `gstat(8)`, `diskinfo(8)` y el árbol de clases GEOM pertenecen a GEOM. Cuando diagnosticas un problema de permisos, miras en `devfs`. Cuando diagnosticas un problema de E/S, miras en GEOM.

### Un ejemplo concreto: /dev/null y /dev/ada0

Compara dos ejemplos que ya conoces.

`/dev/null` es un dispositivo de caracteres clásico. Vive bajo `/dev` porque `devfs` lo crea. El driver es `null(4)`, y su código fuente está en `/usr/src/sys/dev/null/null.c`. Cuando escribes en `/dev/null`, `devfs` enruta la petición a través de `cdevsw` hacia la función de escritura del driver `null`, que simplemente descarta los bytes. No hay GEOM, no hay buffer cache, no hay sistema de archivos. Es un nodo de caracteres puro de `devfs`.

`/dev/ada0` es un dispositivo de bloques. También vive bajo `/dev`. Pero el nodo lo crea GEOM, no una llamada directa a `make_dev` en el driver `ada`. Cuando lees bytes directos de `/dev/ada0`, esos bytes fluyen a través de la capa de interfaz de caracteres de GEOM y llegan a la función strategy del driver `ada`. Cuando montas UFS en `/dev/ada0` y luego lees un archivo, los datos fluyen a través de VFS, UFS, el buffer cache y GEOM, y acaban en la misma función strategy, sin pasar por `devfs` para cada petición.

El nodo en `devfs` es el mismo. El patrón de uso es diferente. El driver debe manejar ambos.

### Cómo procederemos

No escribiremos un driver de caracteres en este capítulo. Ya escribimos uno en el capítulo 26. En su lugar, escribiremos un driver que se registre en GEOM como disco y dejaremos que GEOM cree el nodo `/dev` por nosotros. La integración con devfs será automática.

Este es el patrón dominante para los drivers de bloque en FreeBSD 14.3. Puedes verlo en `md(4)`, en `ata(4)`, en `nvme(4)` y en casi todos los demás drivers de almacenamiento. Cada uno de ellos se registra con GEOM, cada uno recibe peticiones `bio` y cada uno deja que GEOM gestione el nodo `/dev`.

### Cerrando la sección 2

`devfs` y VFS son capas distintas. `devfs` es un sistema de archivos montado en `/dev`, y VFS es el marco abstracto en el que se conectan todos los sistemas de archivos, incluido `devfs`. Los drivers de almacenamiento interactúan con ambos, pero a través de GEOM, que se encarga de crear el nodo `/dev` y de enrutar las peticiones tanto desde la vía de acceso directo como desde la de acceso a través del sistema de archivos. En este capítulo usaremos GEOM como punto de entrada y dejaremos que gestione `devfs` en nuestro nombre.

En la siguiente sección empezaremos a construir el driver. Comenzaremos con lo mínimo necesario para registrar un dispositivo de bloques pseudo en GEOM, sin implementar todavía I/O real. Una vez que esté en marcha, añadiremos el almacenamiento de respaldo, el manejador de `bio` y todo lo demás en secciones posteriores.

## Sección 3: Registrar un dispositivo de bloques pseudo

En esta sección crearemos un driver esqueleto que registra un dispositivo de bloques pseudo en el kernel. Todavía no implementaremos lectura ni escritura. Todavía no lo conectaremos a un almacenamiento de respaldo. Nuestro objetivo es más modesto, y más importante: queremos entender exactamente qué hace falta para que el kernel reconozca nuestro código como un driver de almacenamiento, publique un nodo `/dev` para él y permita que herramientas como `geom(8)` lo vean.

Una vez que eso funcione, todo lo que añadamos después será puramente incremental. El registro en sí es el paso que parece más misterioso, y es el que sirve de base al resto del driver.

### La API g_disk

FreeBSD ofrece a los drivers de almacenamiento una API de registro de alto nivel llamada `g_disk`. Se encuentra en `/usr/src/sys/geom/geom_disk.c` y `/usr/src/sys/geom/geom_disk.h`. La API envuelve la maquinaria de clases GEOM de nivel inferior y expone una interfaz más sencilla que se ajusta a lo que los drivers de disco suelen necesitar.

Usar `g_disk` nos evita tener que implementar una `g_class` completa a mano. Con `g_disk`, asignamos una `struct disk`, rellenamos un puñado de campos y punteros a callbacks, y llamamos a `disk_create`. La API se encarga de construir la clase GEOM, crear el geom, publicar el proveedor, conectar la interfaz de caracteres, iniciar la contabilidad de devstat y hacer visible nuestro dispositivo en userland a través de `/dev`.

No todos los drivers de almacenamiento usan `g_disk`. Las clases GEOM que realizan transformaciones sobre otros proveedores, como `g_nop`, `g_mirror`, `g_stripe` o `g_eli`, se construyen directamente sobre la maquinaria `g_class` de nivel inferior porque no tienen forma de disco. Pero para cualquier cosa que parezca un disco, y desde luego para un pseudo-disco como el nuestro, `g_disk` es el punto de partida adecuado.

Puedes ver la estructura pública en `/usr/src/sys/geom/geom_disk.h`. La forma es aproximadamente la siguiente, abreviada por claridad.

```c
struct disk {
    struct g_geom    *d_geom;
    struct devstat   *d_devstat;

    const char       *d_name;
    u_int            d_unit;

    disk_open_t      *d_open;
    disk_close_t     *d_close;
    disk_strategy_t  *d_strategy;
    disk_ioctl_t     *d_ioctl;
    disk_getattr_t   *d_getattr;
    disk_gone_t      *d_gone;

    u_int            d_sectorsize;
    off_t            d_mediasize;
    u_int            d_fwsectors;
    u_int            d_fwheads;
    u_int            d_maxsize;
    u_int            d_flags;

    void             *d_drv1;

    /* other fields elided */
};
```

Los campos se dividen en tres grupos.

**Identificación**: `d_name` es una cadena corta como `"myblk"` que da nombre a la clase de disco, y `d_unit` es un entero pequeño que distingue varias instancias. Juntos forman el nombre del nodo `/dev`. Un driver con `d_name = "myblk"` y `d_unit = 0` publica `/dev/myblk0`.

**Callbacks**: los punteros `d_open`, `d_close`, `d_strategy`, `d_ioctl`, `d_getattr` y `d_gone` son las funciones que el kernel invocará en nuestro driver. De ellos, solo `d_strategy` es estrictamente obligatorio, porque es la función que gestiona el I/O real. Los demás son opcionales y los iremos viendo a medida que resulten relevantes.

**Geometría**: `d_sectorsize`, `d_mediasize`, `d_fwsectors`, `d_fwheads` y `d_maxsize` describen la forma física y lógica del disco. `d_sectorsize` es el tamaño de un sector en bytes, normalmente 512 o 4096. `d_mediasize` es el tamaño total del dispositivo en bytes. `d_fwsectors` y `d_fwheads` son sugerencias informativas que utilizan las herramientas de particionado. `d_maxsize` es el mayor I/O individual que el driver puede aceptar; GEOM lo usará para dividir las peticiones grandes.

**Estado del driver**: `d_drv1` es un puntero genérico para que el driver guarde su propio contexto. Es el equivalente más cercano a `device_get_softc(dev)` en el mundo Newbus.

### Un esqueleto mínimo

Esbocemos ahora un esqueleto mínimo. Lo colocaremos en `examples/part-06/ch27-storage-vfs/myfirst_blk.c`. Esta versión inicial no hace casi nada útil. Registra un disco, devuelve éxito en cada operación y cancela el registro limpiamente al descargar el módulo. Pero es suficiente para aparecer en `/dev`, para ser visible en `geom disk list` y para que `newfs_ufs` o `fdisk` lo examinen sin que el kernel se bloquee.

```c
/*
 * myfirst_blk.c - a minimal pseudo block device driver.
 *
 * This driver registers a single pseudo disk called myblk0 with
 * the g_disk subsystem. It is intentionally not yet capable of
 * performing real I/O. Sections 4 and 5 of Chapter 27 will add
 * the BIO handler and the backing store.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bio.h>

#include <geom/geom.h>
#include <geom/geom_disk.h>

#define MYBLK_NAME       "myblk"
#define MYBLK_SECTOR     512
#define MYBLK_MEDIASIZE  (1024 * 1024)   /* 1 MiB to start */

struct myblk_softc {
    struct disk     *disk;
    struct mtx       lock;
    u_int            unit;
};

static MALLOC_DEFINE(M_MYBLK, "myblk", "myfirst_blk driver state");

static struct myblk_softc *myblk_unit0;

static void
myblk_strategy(struct bio *bp)
{

    /*
     * No real I/O yet. Mark every request as successful
     * but unimplemented so the caller does not hang.
     */
    bp->bio_error = ENXIO;
    bp->bio_flags |= BIO_ERROR;
    bp->bio_resid = bp->bio_bcount;
    biodone(bp);
}

static int
myblk_attach_unit(struct myblk_softc *sc)
{

    sc->disk = disk_alloc();
    sc->disk->d_name       = MYBLK_NAME;
    sc->disk->d_unit       = sc->unit;
    sc->disk->d_strategy   = myblk_strategy;
    sc->disk->d_sectorsize = MYBLK_SECTOR;
    sc->disk->d_mediasize  = MYBLK_MEDIASIZE;
    sc->disk->d_maxsize    = MAXPHYS;
    sc->disk->d_drv1       = sc;

    disk_create(sc->disk, DISK_VERSION);
    return (0);
}

static void
myblk_detach_unit(struct myblk_softc *sc)
{

    if (sc->disk != NULL) {
        disk_destroy(sc->disk);
        sc->disk = NULL;
    }
}

static int
myblk_loader(struct module *m, int what, void *arg)
{
    struct myblk_softc *sc;
    int error;

    switch (what) {
    case MOD_LOAD:
        sc = malloc(sizeof(*sc), M_MYBLK, M_WAITOK | M_ZERO);
        mtx_init(&sc->lock, "myblk lock", NULL, MTX_DEF);
        sc->unit = 0;
        error = myblk_attach_unit(sc);
        if (error != 0) {
            mtx_destroy(&sc->lock);
            free(sc, M_MYBLK);
            return (error);
        }
        myblk_unit0 = sc;
        printf("myblk: loaded, /dev/%s%u size=%jd bytes\n",
            MYBLK_NAME, sc->unit,
            (intmax_t)sc->disk->d_mediasize);
        return (0);

    case MOD_UNLOAD:
        sc = myblk_unit0;
        if (sc == NULL)
            return (0);
        myblk_detach_unit(sc);
        mtx_destroy(&sc->lock);
        free(sc, M_MYBLK);
        myblk_unit0 = NULL;
        printf("myblk: unloaded\n");
        return (0);

    default:
        return (EOPNOTSUPP);
    }
}

static moduledata_t myblk_mod = {
    "myblk",
    myblk_loader,
    NULL
};

DECLARE_MODULE(myblk, myblk_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(myblk, 1);
```

Dedica un momento a leerlo con atención. Solo hay un puñado de piezas en movimiento, pero cada una de ellas hace un trabajo real.

La estructura `myblk_softc` es el contexto local del driver. Contiene un puntero a nuestra `struct disk`, un mutex para uso futuro y el número de unidad. La asignamos al cargar el módulo y la liberamos al descargarlo.

La función `myblk_strategy` es el callback que GEOM invocará cada vez que se dirija un `bio` a nuestro dispositivo. En esta primera versión, simplemente fallamos cada petición con `ENXIO`. No es lo más cortés, pero es correcto como marcador de posición: el kernel no se quedará bloqueado esperándonos, y no fingiremos que el I/O tuvo éxito cuando no lo tuvo. En la sección 5 sustituiremos esto por un manejador funcional.

La función `myblk_attach_unit` asigna una `struct disk`, rellena los campos de identificación, callbacks y geometría, y la publica con `disk_create`. La llamada a `disk_create` es lo que realmente produce el nodo `/dev` y registra el disco en la topología GEOM.

La función `myblk_detach_unit` invierte ese proceso. `disk_destroy` pide a GEOM que retire el proveedor, cancele cualquier I/O pendiente y elimine el nodo `/dev`. Ponemos `sc->disk` a `NULL` para que intentos de descarga posteriores no traten de liberar una estructura ya liberada, aunque en la secuencia de carga/descarga que seguimos eso no puede ocurrir.

El cargador del módulo es el código repetitivo estándar de `moduledata_t` que ya viste en el capítulo 26. Con `MOD_LOAD` asigna el softc y llama a `myblk_attach_unit`. Con `MOD_UNLOAD` llama a `myblk_detach_unit`, libera el softc y retorna.

Una línea merece atención especial.

La llamada `disk_create(sc->disk, DISK_VERSION)` pasa la versión ABI actual de la estructura del disco. `DISK_VERSION` está definida en `/usr/src/sys/geom/geom_disk.h` y se incrementa cada vez que el ABI de `g_disk` cambia de forma incompatible. Si compilas un driver contra el árbol equivocado, el kernel se negará a registrar el disco y mostrará un mensaje de diagnóstico. Este versionado es lo que permite que el kernel evolucione sin romper silenciosamente los drivers externos al árbol.

Puede que te preguntes por qué no usamos `MODULE_DEPEND` para declarar una dependencia de `g_disk`. La razón es que `g_disk` no es un módulo del kernel cargable en el sentido habitual. Es una clase GEOM declarada en el kernel mediante `DECLARE_GEOM_CLASS(g_disk_class, g_disk)` en `/usr/src/sys/geom/geom_disk.c`, y siempre está presente cuando el propio GEOM está compilado en el kernel. No existe un archivo `g_disk.ko` separado que puedas descargar o recargar de forma independiente, y `MODULE_DEPEND(myblk, g_disk, ...)` no resolvería a un módulo real. Los símbolos que llamamos (`disk_alloc`, `disk_create`, `disk_destroy`) provienen del kernel en sí.

### El Makefile

El Makefile para este módulo es casi idéntico al del capítulo 26.

```make
# Makefile for myfirst_blk.
#
# Companion file for Chapter 27 of
# "FreeBSD Device Drivers: From First Steps to Kernel Mastery".

KMOD    = myblk
SRCS    = myfirst_blk.c

# Where the kernel build machinery lives.
.include <bsd.kmod.mk>
```

Colócalo en el mismo directorio que `myfirst_blk.c`. Ejecutar `make` construirá `myblk.ko`. Ejecutar `make load` lo cargará si tienes las fuentes del kernel instaladas en el lugar habitual. Ejecutar `make unload` lo descargará.

### Cargar e inspeccionar el esqueleto

Una vez cargado el módulo, el kernel habrá creado un pseudo-disco y un nodo `/dev` para él. Veamos qué deberías observar.

```console
# kldload ./myblk.ko
# dmesg | tail -n 1
myblk: loaded, /dev/myblk0 size=1048576 bytes
# ls -l /dev/myblk0
crw-r-----  1 root  operator  0x8b Apr 19 18:04 /dev/myblk0
# diskinfo -v /dev/myblk0
/dev/myblk0
        512             # sectorsize
        1048576         # mediasize in bytes (1.0M)
        2048            # mediasize in sectors
        0               # stripesize
        0               # stripeoffset
        myblk0          # Disk ident.
```

La `c` al inicio de la cadena de permisos nos indica que GEOM ha creado un nodo de dispositivo de caracteres, que es como FreeBSD expone los dispositivos orientados a bloques en `/dev` en el kernel moderno. El número mayor del dispositivo, aquí `0x8b`, se asigna dinámicamente.

Veamos ahora la topología GEOM.

```console
# geom disk list myblk0
Geom name: myblk0
Providers:
1. Name: myblk0
   Mediasize: 1048576 (1.0M)
   Sectorsize: 512
   Mode: r0w0e0
   descr: (null)
   ident: (null)
   rotationrate: unknown
   fwsectors: 0
   fwheads: 0
```

`Mode: r0w0e0` significa cero lectores, cero escritores y cero titulares exclusivos. Nadie está usando el disco.

Prueba ahora algo inofensivo.

```console
# dd if=/dev/myblk0 of=/dev/null bs=512 count=1
dd: /dev/myblk0: Device not configured
0+0 records in
0+0 records out
0 bytes transferred in 0.000123 secs (0 bytes/sec)
```

El error `Device not configured` es el `ENXIO` que devolvimos deliberadamente. Nuestra función strategy ejecutó, marcó el BIO como fallido, y `dd` informó fielmente del fallo. Esta es la primera evidencia real de que el kernel está alcanzando nuestro driver a través del código de la capa de bloques.

Prueba una lectura que espera éxito para que falle de forma llamativa.

```console
# newfs_ufs /dev/myblk0
newfs: /dev/myblk0: read-only
# newfs_ufs -N /dev/myblk0
/dev/myblk0: 1.0MB (2048 sectors) block size 32768, fragment size 4096
        using 4 cylinder groups of 0.31MB, 10 blks, 40 inodes.
super-block backups (for fsck_ffs -b #) at:
192, 832, 1472, 2112
```

La opción `-N` indica a `newfs` que planifique la disposición del sistema de archivos sin escribir nada. Vemos que considera nuestro dispositivo como un disco pequeño con 2048 sectores de 512 bytes cada uno. Eso coincide con la geometría que declaramos. Todavía no escribe nada porque nuestra función strategy seguiría fallando, pero la planificación funciona.

Por último, descarguemos el módulo limpiamente.

```console
# kldunload myblk
# dmesg | tail -n 1
myblk: unloaded
# ls /dev/myblk0
ls: /dev/myblk0: No such file or directory
```

Ese es el ciclo de vida completo del esqueleto.

### Por qué los fallos son esperados

En esta etapa, cualquier herramienta en espacio de usuario que intente leer o escribir datos fallará. Eso es correcto. Nuestra función strategy todavía no sabe hacer nada, y no debemos fingir éxito. Fingir éxito llevaría a corrupción en el momento en que un sistema de archivos intentara leer lo que creía haber escrito.

El hecho de que el kernel y las herramientas gestionen nuestro fallo con elegancia es una prueba de que la capa de bloques está haciendo su trabajo. Un `bio` bajó, el driver lo rechazó, el error se propagó de vuelta al espacio de usuario y nadie se bloqueó. Ese es el comportamiento que queremos.

### Cómo encajan las piezas

Antes de continuar, nombremos las piezas para poder referirnos a ellas más adelante sin ambigüedad.

Nuestro **módulo driver** es `myblk.ko`. Es lo que el usuario carga con `kldload`.

Nuestro **softc** es `struct myblk_softc`. Contiene el estado local del driver. Hay exactamente una instancia en esta primera versión.

Nuestro **disco** es una `struct disk` asignada por `disk_alloc` y registrada con `disk_create`. El kernel posee su memoria. No la liberamos directamente. Le pedimos al kernel que la libere llamando a `disk_destroy`.

Nuestro **geom** es el objeto GEOM que el subsistema `g_disk` crea en nuestro nombre. No lo vemos directamente en nuestro código. Existe en la topología GEOM como padre de nuestro proveedor.

Nuestro **proveedor** es la cara del dispositivo orientada al productor. Es lo que otras clases GEOM consumen cuando se conectan a nosotros. GEOM crea automáticamente un nodo de dispositivo de caracteres para nuestro proveedor en `/dev`.

Nuestro **consumidor** está todavía vacío. Nadie está conectado a nosotros aún. Los consumidores son la forma en que las clases GEOM que se sitúan por encima de nosotros, como una capa de particionado o el consumidor GEOM de un sistema de archivos, se conectan.

Nuestro **nodo /dev** es `/dev/myblk0`. Es un manejador activo que las herramientas en espacio de usuario pueden usar para emitir I/O directo. Cuando más adelante se monte un sistema de archivos sobre el dispositivo, también se referirá a él por este nombre, aunque la vía de I/O activa no pasará por `devfs` para cada petición.

### Cerrando la sección 3

Hemos construido el driver más pequeño posible que participa en la pila de almacenamiento de FreeBSD. Registra un pseudo-disco en el subsistema `g_disk`, publica un nodo `/dev` a través de GEOM, acepta peticiones BIO y las declina con cortesía. Carga, aparece en `geom disk list` y se descarga sin fugas.

En la siguiente sección estudiaremos GEOM de forma más directa. Entenderemos qué es realmente un proveedor, qué es realmente un consumidor y cómo el diseño basado en clases permite que transformaciones como el particionado, la duplicación, el cifrado y la compresión se compongan con nuestro driver de forma gratuita. Ese entendimiento nos preparará para la sección 5, donde sustituiremos la función strategy de marcador de posición por una que realmente sirva lecturas y escrituras desde un almacenamiento de respaldo.

## Sección 4: Exponer un proveedor respaldado por GEOM

La sección anterior nos permitió registrar un disco con `g_disk` y confiar en que el framework se encargaría de lo que ocurre bajo el capó. Es un primer paso razonable, y para muchos drivers es todo el contacto con GEOM que van a necesitar. Pero el trabajo con almacenamiento premia a quien entiende la capa sobre la que está trabajando. Cuando falla el montaje de un sistema de archivos, cuando `gstat` muestra peticiones acumulándose, o cuando un `kldunload` se bloquea más tiempo del esperado, querrás conocer el vocabulario de GEOM lo suficientemente bien como para hacerte las preguntas correctas.

Esta sección es un recorrido por GEOM desde la perspectiva del driver de almacenamiento. No es una referencia exhaustiva. El FreeBSD Developer's Handbook dedica capítulos enteros a GEOM, y no vamos a duplicarlos aquí. Lo que haremos es describir los conceptos y objetos que importan a quien escribe un driver, y mostrar cómo `g_disk` encaja en ese panorama.

### GEOM en una página

GEOM es un framework de almacenamiento. Se sitúa entre los sistemas de archivos y los drivers de bloque que hablan con el hardware real, y está diseñado para componerse. Esa composición es precisamente su razón de ser.

La idea es que una pila de almacenamiento se construye a partir de pequeñas transformaciones. Una transformación presenta un disco en bruto. Otra lo divide en particiones. Otra replica dos discos en uno. Otra cifra una partición. Otra comprime un sistema de archivos. Cada transformación es un pequeño fragmento de código que recibe peticiones de I/O desde arriba, hace algo con ellas, y o bien devuelve un resultado directamente o las pasa a la siguiente capa inferior.

En el vocabulario de GEOM, cada transformación es una **clase**. Cada instancia de una clase es un **geom**. Cada geom tiene un número determinado de **providers** (salidas) y un número determinado de **consumers** (entradas). Los providers apuntan hacia arriba, hacia la siguiente capa. Los consumers apuntan hacia abajo, hacia la capa anterior. Un geom sin consumer está en la parte inferior de la pila: debe producir I/O por sí mismo. Un geom sin provider está en la parte superior de la pila: debe terminar el I/O y entregarlo fuera de GEOM, normalmente a un sistema de archivos o a un dispositivo de caracteres de `devfs`.

Las peticiones fluyen de los providers a los consumers a través de la pila. Las respuestas fluyen de vuelta. La unidad de I/O es un `struct bio`, que estudiaremos en detalle en la Sección 5.

### Un ejemplo concreto de composición

Imagina que tienes un SSD SATA de 1 TB. El driver `ada(4)` del kernel se ejecuta sobre el controlador SATA y publica un provider de disco llamado `ada0`. Ese es un geom sin consumer en la parte inferior y un provider en la parte superior.

Divides el SSD con `gpart`. La clase `PART` crea un geom cuyo único consumer está conectado a `ada0`, y que publica múltiples providers, uno por partición: `ada0p1`, `ada0p2`, `ada0p3`, etc.

Cifras `ada0p2` con `geli`. La clase `ELI` crea un geom cuyo único consumer está conectado a `ada0p2`, y que publica un único provider llamado `ada0p2.eli`.

Montas UFS en `ada0p2.eli`. UFS abre ese provider, lee su superbloque y comienza a servir archivos.

Cuando un proceso lee un archivo, la petición viaja desde UFS hasta `ada0p2.eli`, atraviesa el geom `geli` que descifra los bloques pertinentes, llega a `ada0p2`, pasa por el geom `PART` que ajusta las direcciones de bloque, y alcanza `ada0`, donde el driver `ada` habla con el controlador SATA.

En ningún momento UFS sabe que su almacenamiento subyacente está cifrado, particionado o siquiera es un disco físico. Solo ve un provider. Las capas por debajo pueden ser tan simples o tan elaboradas como el administrador decida.

Esa composición es la razón por la que existe GEOM. Un driver de almacenamiento solo necesita saber cómo ser un productor fiable de I/O en la parte inferior de la pila. Todo lo que está por encima es reutilizable.

### Providers y consumers en el código

En el kernel, un provider es un `struct g_provider` y un consumer es un `struct g_consumer`. Ambos están definidos en `/usr/src/sys/geom/geom.h`. Como autor de un driver de disco, casi nunca los asignarás directamente. `g_disk` asigna un provider en tu nombre cuando llamas a `disk_create`, y nunca necesitas un consumer, porque un driver de disco no se conecta a nada por debajo.

Lo que sí necesitas es un modelo mental de lo que significan.

Un provider es una superficie con nombre, posicionable y direccionable por bloques sobre la que algo puede leer y escribir. Tiene un tamaño, un tamaño de sector, un nombre y algunos contadores de acceso. GEOM publica los providers en `/dev` mediante su integración con dispositivos de caracteres, de modo que el administrador puede referirse a ellos por nombre.

Un consumer es un canal desde un geom hacia el provider de otro geom. El consumer es donde el geom superior emite peticiones de I/O, y es donde registra sus derechos de acceso. Cuando montas UFS en `ada0p2.eli`, la operación de montaje hace que se conecte un consumer dentro del hook GEOM de UFS, y ese consumer adquiere derechos de acceso sobre el provider `ada0p2.eli`.

### Derechos de acceso

Los providers tienen tres contadores de acceso: lectura (`r`), escritura (`w`) y exclusivo (`e`). Son visibles en `gstat` y `geom disk list` como `r0w0e0` o similar. Cada número se incrementa cuando un consumer solicita ese tipo de acceso y se decrementa cuando el consumer lo libera.

Un acceso exclusivo es lo que `mount`, `newfs` y herramientas administrativas similares adquieren cuando necesitan asegurarse de que ningún otro proceso está escribiendo en el dispositivo. Un contador exclusivo de cero significa que no hay ningún acceso exclusivo activo. Un contador exclusivo mayor que cero significa que el provider está en uso.

Los contadores de acceso no son un detalle trivial. Son una herramienta real de sincronización. Cuando llamas a `disk_destroy` para eliminar un disco, el kernel se negará a destruir el provider si todavía tiene usuarios activos, porque destruirlo mientras hay un sistema de archivos montado sería catastrófico. Este es el mismo mecanismo que hace que `kldunload` se bloquee si el módulo está en uso, pero opera en la capa GEOM, un nivel por encima del subsistema de módulos.

Puedes observar cómo cambian los contadores de acceso en tiempo real.

```console
# geom disk list myblk0 | grep Mode
   Mode: r0w0e0
# dd if=/dev/myblk0 of=/dev/null bs=512 count=1 &
# geom disk list myblk0 | grep Mode
   Mode: r1w0e0
```

Cuando `dd` termina, el modo vuelve a `r0w0e0`.

### El objeto BIO y su ciclo de vida

La unidad de trabajo en GEOM es el BIO, definido como `struct bio` en `/usr/src/sys/sys/bio.h`. Un BIO representa una petición de I/O. Tiene un comando (`bio_cmd`), un desplazamiento (`bio_offset`), una longitud (`bio_length`), un puntero a datos (`bio_data`), un recuento de bytes (`bio_bcount`), un residuo (`bio_resid`), un error (`bio_error`), indicadores (`bio_flags`) y otros campos que iremos conociendo a medida que los necesitemos.

Los valores de `bio_cmd` indican al driver qué tipo de I/O se está solicitando. Los valores más comunes son `BIO_READ`, `BIO_WRITE`, `BIO_DELETE`, `BIO_GETATTR` y `BIO_FLUSH`. `BIO_READ` y `BIO_WRITE` hacen lo que esperas. `BIO_DELETE` pide al driver que libere los bloques del rango indicado, de la misma manera que hace `TRIM` en los SSD o `mdconfig -d` en un disco de memoria. `BIO_GETATTR` consulta un atributo por nombre y es el mecanismo mediante el cual las capas GEOM descubren tipos de partición, etiquetas de medios y otros metadatos. `BIO_FLUSH` pide al driver que confirme las escrituras pendientes en almacenamiento estable.

Un BIO viaja hacia abajo de un geom al siguiente mediante `g_io_request`. Cuando llega al fondo de la pila, se invoca la función strategy del driver. Cuando el driver termina, completa el BIO llamando a `biodone` o, a nivel de clase GEOM, a `g_io_deliver`. La llamada de completado libera el BIO de vuelta hacia arriba en la pila.

Los drivers que usan `g_disk` obtienen una vista algo más sencilla, porque la infraestructura de `g_disk` traduce el manejo de BIO a nivel GEOM en un estilo de completado basado en `biodone`. Cuando implementas `d_strategy`, recibes un `struct bio` y debes llamar eventualmente a `biodone(bp)` para completarlo. No llamas a `g_io_deliver` directamente. Lo hace el framework.

### El lock de topología de GEOM

GEOM tiene un lock global llamado lock de topología. Protege las modificaciones al árbol de geoms, providers y consumers. Cuando se crea o destruye un provider, cuando se conecta o desconecta un consumer, cuando cambian los contadores de acceso o cuando GEOM recorre el árbol para enrutar una petición, se toma el lock de topología.

El lock de topología se mantiene a lo largo de operaciones que pueden tardar un tiempo, lo cual es inusual para los locks del kernel, por lo que GEOM ejecuta gran parte de su trabajo real de forma asíncrona mediante un thread dedicado llamado cola de eventos. Cuando examinas las definiciones de `g_class` en el árbol de código fuente, los métodos `init`, `fini`, `access` y similares se invocan en el contexto del thread de eventos de GEOM, no en el contexto del proceso de usuario que desencadenó la operación.

Para un driver que usa `g_disk`, esto importa en un aspecto concreto. No debes mantener el lock de tu driver mientras realizas una llamada a funciones a nivel GEOM, porque GEOM puede adquirir el lock de topología dentro de esas funciones, y el anidamiento de locks en el orden incorrecto provoca deadlock. `g_disk` está escrito con suficiente cuidado como para que no tengas que pensar en esto habitualmente, siempre que sigas los patrones que mostramos. Pero conviene tener ese dato presente.

### La cola de eventos de GEOM

GEOM procesa muchos eventos en un único thread del kernel dedicado llamado `g_event`. Si tienes el kernel en ejecución con la depuración activada, puedes verlo en `procstat -kk`. Este thread recoge los eventos colocados en su cola y los procesa uno a uno. Los eventos típicos incluyen crear un geom, destruirlo, conectar un consumer, desconectarlo y volver a sondear un provider.

Una consecuencia práctica es que algunas acciones que realizas desde tu driver, como `disk_destroy`, no ocurren de forma síncrona en el contexto del thread que las invoca. Se encolan para el thread de eventos, y la destrucción real ocurre un momento después. `disk_destroy` gestiona la espera correctamente, de modo que cuando retorna, el disco ya ha desaparecido. Pero si estás rastreando un sutil error de ordenación, recordar que GEOM tiene su propio thread puede ayudarte.

### Cómo g_disk envuelve todo esto

Con ese vocabulario ya asimilado, podemos describir con más precisión qué hace `g_disk` por nosotros.

Cuando llamamos a `disk_alloc`, recibimos un `struct disk` suficientemente preinicializado para ser completado. Establecemos el nombre, la unidad, los callbacks y la geometría, y luego llamamos a `disk_create`.

`disk_create` hace lo siguiente por nosotros, a través de la cola de eventos:

1. crea una clase GEOM si todavía no existe ninguna para este nombre de disco,
2. crea un geom bajo esa clase,
3. crea un provider asociado al geom,
4. configura la contabilidad de devstat para que `iostat` y `gstat` tengan datos,
5. conecta la interfaz de dispositivo de caracteres de GEOM para que aparezca `/dev/<nombre><unidad>`,
6. organiza el flujo de peticiones BIO hacia nuestro callback `d_strategy`.

También configura algunos comportamientos opcionales. Si proporcionamos un `d_ioctl`, el kernel enruta las llamadas `ioctl` del espacio de usuario sobre el nodo `/dev` hacia nuestra función. Si proporcionamos un `d_getattr`, GEOM enruta las peticiones `BIO_GETATTR` hacia él. Si proporcionamos un `d_gone`, el kernel lo llama si algo externo a nuestro driver decide que el disco ha desaparecido, como un evento de extracción en caliente.

En el lado del desmantelamiento, `disk_destroy` encola la eliminación, espera a que todo el I/O pendiente se drene, libera el provider, destruye el geom y libera el `struct disk`. No llamamos a `free` sobre el disco nosotros mismos. Lo hace el framework.

### Dónde leer el código fuente

Ahora tienes suficiente vocabulario para sacar provecho de leer directamente el código fuente de `g_disk`. Abre `/usr/src/sys/geom/geom_disk.c` y busca lo siguiente.

La función `disk_alloc` aparece al principio del archivo. Es un asignador sencillo que devuelve un `struct disk` inicializado a cero. Nada llamativo.

La función `disk_create` es más larga. Recórrela por encima y observa el enfoque basado en eventos: la mayor parte del trabajo real se encola en lugar de ejecutarse de forma inmediata. Observa también las comprobaciones de validez sobre los campos del disco, que detectan drivers que olvidan establecer el tamaño de sector, el tamaño del medio o la función strategy.

La función `disk_destroy` está igualmente basada en la cola de eventos. Protege el desmantelamiento con una comprobación del contador de acceso, porque destruir un disco que todavía está abierto sería un error.

La función `g_disk_start` es la función strategy interna. Valida un BIO, actualiza devstat y llama al `d_strategy` del driver.

Tómate un momento para leer el código. No necesitas entender cada rama. Lo que sí necesitas es reconocer la forma general: eventos para los cambios estructurales, trabajo directo para el I/O. Esa es la forma de la mayoría del código basado en GEOM.

### Comparando md(4) y g_zero

Dos drivers reales son buenas lecturas como contrapunto a `g_disk`. El primero es el driver `md(4)`, en `/usr/src/sys/dev/md/md.c`. Es un driver de disco en memoria que utiliza tanto `g_disk` como estructuras GEOM gestionadas directamente. Es el ejemplo más completo de un driver de almacenamiento en el árbol, ya que admite múltiples tipos de almacenamiento subyacente, redimensionamiento, volcados y muchas otras funcionalidades. Es un archivo extenso, pero es el pariente más cercano de lo que estamos construyendo.

El segundo es `g_zero`, en `/usr/src/sys/geom/zero/g_zero.c`. Se trata de una clase GEOM mínima en la que las lecturas siempre devuelven memoria inicializada a cero y las escrituras se descartan. Tiene aproximadamente 145 líneas y utiliza directamente la API de nivel inferior `DECLARE_GEOM_CLASS`, en lugar de `g_disk`. Es un contrapunto excelente porque muestra la mecánica de las clases GEOM sin ninguno de los adornos propios de los discos. Cuando quieras entender qué oculta `g_disk`, lee `g_zero`.

### Por qué nuestro driver usa g_disk

Puede que te preguntes si deberíamos construir nuestro driver directamente sobre la API de nivel inferior `g_class`, como hace `g_zero`, para exponer más de la maquinaria interna. No lo haremos, y hay tres razones para ello.

En primer lugar, `g_disk` es la opción idiomática para cualquier cosa que se parezca a un disco, que es exactamente lo que hace nuestro pseudodispositivo de bloques. Los revisores de parches de driver reales de FreeBSD rechazarían un driver que usara `g_class` directamente cuando `g_disk` es suficiente.

En segundo lugar, `g_disk` nos proporciona integración con devstat, ioctls estándar y gestión de nodos `/dev` de forma gratuita. Reimplementar todo eso a mano sería una distracción considerable del objetivo pedagógico de este capítulo.

En tercer lugar, cuanto más sencillo sea el primer driver funcional, más fácil será razonarlo. Tenemos mucho código por escribir en las próximas secciones. No necesitamos dedicar páginas a la fontanería GEOM a nivel de clase que `g_disk` ya resuelve correctamente.

Dicho esto, si tienes curiosidad, deberías leer sin duda `g_zero.c`. Es un archivo pequeño y revela la mecánica que `g_disk` abstrae. El cierre de esta sección te lo señalará una última vez.

### Un recorrido por g_class

Para quienes quieran conocer un poco más de la maquinaria subyacente, recorramos cómo se ve una estructura `g_class` en el código, sin construir aún una propia.

Lo siguiente se reproduce (ligeramente simplificado) de `/usr/src/sys/geom/zero/g_zero.c`.

```c
static struct g_class g_zero_class = {
    .name = G_ZERO_CLASS_NAME,
    .version = G_VERSION,
    .start = g_zero_start,
    .init = g_zero_init,
    .fini = g_zero_fini,
    .destroy_geom = g_zero_destroy_geom
};

DECLARE_GEOM_CLASS(g_zero_class, g_zero);
```

`.name` es el nombre de la clase, que aparece en la salida de `geom -t`. `.version` debe coincidir con `G_VERSION` del kernel en ejecución; las versiones que no coincidan serán rechazadas en el momento de la carga. `.start` es la función que se llama cuando un BIO llega a un proveedor de esta clase. `.init` se llama cuando la clase se instancia por primera vez, normalmente para crear el geom inicial y su proveedor. `.fini` es la contraparte de desmontaje de `.init`. `.destroy_geom` se llama cuando se está eliminando un geom específico de esta clase.

`DECLARE_GEOM_CLASS` es una macro que se expande en una declaración de módulo que carga esta clase en el kernel al cargarse el módulo. Oculta el `moduledata_t`, el `SYSINIT` y el cableado de `g_modevent` tras una sola línea.

Nuestro driver no usa `g_class` directamente. `g_disk` lo hace por nosotros, y la clase que declara internamente es la clase `DISK` universal que comparten todos los drivers con forma de disco. Pero entender la estructura es útil porque, si alguna vez escribes una clase de transformación (una capa de cifrado, compresión o particionado a nivel GEOM), deberás definir tu propia `g_class`.

### El ciclo de vida de un BIO, en detalle

Repasamos el ciclo de vida del BIO brevemente antes. Aquí lo vemos con más detalle, porque todo fallo en un driver de almacenamiento afecta a este ciclo de vida en algún punto.

Un BIO se origina en algún lugar por encima del driver. Para nuestro driver, los orígenes más habituales son:

1. **La escritura diferida del buffer-cache de un sistema de archivos**. UFS llama a `bwrite` o `bawrite` sobre un buffer, lo que construye un BIO y lo entrega a GEOM mediante `g_io_request`.
2. **Una lectura del buffer-cache de un sistema de archivos**. UFS llama a `bread`, que comprueba la caché y, si no hay acierto, emite un BIO.
3. **Un acceso directo a través de `/dev/myblk0`**. Un programa llama a `read(2)` o `write(2)` sobre el nodo. `devfs` y la integración del dispositivo de caracteres de GEOM construyen un BIO y lo emiten.
4. **Una operación emitida por una herramienta**. `newfs_ufs`, `diskinfo`, `dd` y herramientas similares emiten BIOs de la misma forma que un acceso directo.

Una vez construido, el BIO se enruta a través de la topología de GEOM. Cada salto consumidor -> proveedor a lo largo del camino puede transformar o validar el BIO. Para una pila simple (nuestro driver sin geoms intermedios), no hay saltos intermedios; el BIO llega a nuestro proveedor y se despacha a nuestra función strategy.

Dentro de `g_disk`, la función strategy va precedida de tres pequeñas tareas de contabilidad:

1. Algunas comprobaciones de sanidad (por ejemplo, verificar que el desplazamiento y la longitud del BIO están dentro del medio).
2. Una llamada a `devstat_start_transaction_bio` para iniciar la medición del tiempo de la solicitud.
3. Una llamada al `d_strategy` del driver.

Al completarse, `g_disk` intercepta la llamada a `biodone`, registra el tiempo de fin con `devstat_end_transaction_bio` y reenvía la finalización hacia arriba en la pila.

Desde el punto de vista del driver, lo único que importa es que se llame a `d_strategy` y que se llame a `biodone` una vez por BIO. Todo lo demás es fontanería.

### Propagación de errores

Cuando un BIO falla, el driver establece `bio_error` con un valor `errno` y activa el indicador `BIO_ERROR` en `bio_flags`. A continuación se llama a `biodone` con normalidad.

Por encima del driver, el código de finalización de GEOM comprueba si hay error. Si está activo, el error se propaga hacia arriba en la pila. El sistema de archivos ve el error y decide qué hacer; normalmente, un error de lectura en metadatos es fatal y el sistema de archivos informa de EIO al espacio de usuario. Un error de escritura suele retrasarse; el sistema de archivos puede reintentar o puede marcar el buffer asociado como pendiente de atención en la próxima sincronización.

Valores de `errno` habituales en el camino del BIO:

- `EIO`: un error genérico de I/O. El kernel asume que el dispositivo tiene problemas.
- `ENXIO`: el dispositivo no está configurado o ha desaparecido.
- `EOPNOTSUPP`: el driver no soporta esta operación.
- `EROFS`: el medio es de solo lectura.
- `ENOSPC`: no hay espacio disponible.
- `EFAULT`: una dirección de la solicitud no es válida. Muy raro en el camino del BIO.

Para nuestro driver en memoria, los únicos errores que deberían aparecer son el error de comprobación de límites (`EIO`) y el error de comando desconocido (`EOPNOTSUPP`).

### Lo que hace g_disk sin que lo veas

Hemos mencionado que `g_disk` se ocupa de varias cosas en nuestro nombre. Aquí tienes una lista más completa.

- Crea la clase GEOM para el tipo `DISK` si aún no existe, y comparte esta clase entre todos los drivers de disco.
- Crea un geom bajo esa clase cuando llamamos a `disk_create`.
- Crea un proveedor en el geom y lo publica en `/dev`.
- Conecta la contabilidad devstat automáticamente.
- Gestiona el protocolo de acceso de GEOM, convirtiendo las llamadas `open` y `close` del espacio de usuario sobre `/dev/myblk0` en cambios en el contador de accesos del proveedor.
- Gestiona la interfaz del dispositivo de caracteres de GEOM, convirtiendo las lecturas y escrituras sobre `/dev/myblk0` en BIOs hacia nuestra función strategy.
- Gestiona los casos por defecto de BIO_GETATTR (la mayoría de los atributos tienen valores predeterminados razonables).
- Gestiona el mecanismo de desmantelamiento en `disk_destroy`, esperando a que terminen los BIOs en vuelo.
- Reenvía las llamadas a `d_ioctl` para los ioctls que no gestiona por sí mismo.

Cada uno de estos elementos es un fragmento de código que tendrías que escribir si construyeras directamente sobre `g_class`. Leer `/usr/src/sys/geom/geom_disk.c` es una buena forma de apreciar todo lo que `g_disk` hace por nosotros.

### Inspeccionando nuestro proveedor

Tomemos nuestro driver esqueleto de la Sección 3, carguémoslo e inspeccionémoslo con los ojos de GEOM.

```console
# kldload ./myblk.ko
# geom disk list myblk0
Geom name: myblk0
Providers:
1. Name: myblk0
   Mediasize: 1048576 (1.0M)
   Sectorsize: 512
   Mode: r0w0e0
   descr: (null)
   ident: (null)
   rotationrate: unknown
   fwsectors: 0
   fwheads: 0
```

`geom disk list` nos muestra únicamente los geoms de la clase `DISK`. Cada uno de esos geoms tiene un proveedor. También podemos ver el árbol completo de clases.

```console
# geom -t | head -n 40
Geom        Class      Provider
ada0        DISK       ada0
 ada0p1     PART       ada0p1
 ada0p2     PART       ada0p2
 ada0p3     PART       ada0p3
myblk0      DISK       myblk0
```

Nuestro geom es un hermano de los discos reales, sin ninguna clase de capa superior conectada aún. En secciones posteriores veremos qué ocurre cuando se conecta un sistema de archivos.

```console
# geom stats myblk0
```

`geom stats` devuelve contadores de rendimiento detallados. En un dispositivo inactivo y sin uso como el nuestro, todos los contadores son cero.

```console
# gstat -I 1
dT: 1.002s  w: 1.000s
 L(q)  ops/s    r/s   kBps   ms/r    w/s   kBps   ms/w    %busy Name
    0      0      0      0    0.0      0      0    0.0    0.0| ada0
    0      0      0      0    0.0      0      0    0.0    0.0| myblk0
```

`gstat` es una vista más compacta que se actualiza en tiempo real. La usaremos intensamente en secciones posteriores.

### Cerrando la Sección 4

GEOM es un framework de capa de bloques componible formado por clases, geoms, proveedores y consumidores. Las solicitudes fluyen por él como objetos `struct bio`, con `BIO_READ`, `BIO_WRITE` y un puñado de otros comandos. Los derechos de acceso, el lock de topología y la gestión de estructuras basada en eventos son los mecanismos que mantienen el framework seguro para evolucionar bajo carga. `g_disk` envuelve todo esto para los drivers con forma de disco y les ofrece una interfaz más amigable con escasa pérdida de expresividad.

Nuestro driver esqueleto es ya un participante de primera clase en GEOM, aunque todavía no puede realizar I/O real. En la próxima sección le daremos esa pieza que le falta. Asignaremos un buffer de respaldo, implementaremos una función strategy que realmente lea y escriba, y observaremos cómo la pila de almacenamiento del kernel ejercita nuestro código tanto desde el acceso directo como desde el acceso por sistema de archivos.

## Sección 5: implementando lectura y escritura básicas

En la Sección 3 devolvíamos `ENXIO` para cada BIO. En la Sección 4 aprendimos lo suficiente sobre GEOM para saber exactamente qué tipo de solicitud recibe nuestra función strategy y cuáles son sus obligaciones. En esta sección reemplazaremos ese marcador de posición por un manejador funcional que lea y escriba bytes reales en un almacén de respaldo en memoria. Al final, nuestro driver atenderá tráfico a través de `dd`, devolverá datos coherentes y sobrevivirá a ser formateado por `newfs_ufs`.

### El almacén de respaldo

Nuestro almacén de respaldo por ahora es simplemente un array de bytes en memoria del kernel, dimensionado para coincidir con `d_mediasize`. Es la representación más sencilla posible de un disco: un buffer plano. Los drivers de almacenamiento reales reemplazan esto con DMA de hardware, con un archivo respaldado por un vnode o con un objeto VM respaldado por swap, pero un buffer plano es suficiente para enseñar todos los demás conceptos de este capítulo sin distracciones.

Para 1 MiB podemos simplemente hacer `malloc` del buffer. Para tamaños mayores necesitaríamos un asignador diferente, porque el heap del kernel no escala bien ante asignaciones contiguas de decenas o cientos de megabytes. `md(4)` evita el problema en discos de memoria grandes usando asignación página a página y una estructura de indirección personalizada. Aún no necesitamos ese nivel de sofisticación, pero anotaremos la limitación en el código.

Actualicemos `myblk_softc` para incluir el almacén de respaldo.

```c
struct myblk_softc {
    struct disk     *disk;
    struct mtx       lock;
    u_int            unit;
    uint8_t         *backing;
    size_t           backing_size;
};
```

Dos campos nuevos: `backing` es el puntero a la memoria del kernel que asignamos, y `backing_size` es el número de bytes que asignamos. Deberían ser siempre iguales a `d_mediasize`, pero almacenar el tamaño explícitamente es más limpio que depender de la indirección a través de `disk->d_mediasize`.

Ahora, en `myblk_attach_unit`, asigna el buffer de respaldo.

```c
static int
myblk_attach_unit(struct myblk_softc *sc)
{

    sc->backing_size = MYBLK_MEDIASIZE;
    sc->backing = malloc(sc->backing_size, M_MYBLK, M_WAITOK | M_ZERO);

    sc->disk = disk_alloc();
    sc->disk->d_name       = MYBLK_NAME;
    sc->disk->d_unit       = sc->unit;
    sc->disk->d_strategy   = myblk_strategy;
    sc->disk->d_sectorsize = MYBLK_SECTOR;
    sc->disk->d_mediasize  = MYBLK_MEDIASIZE;
    sc->disk->d_maxsize    = MAXPHYS;
    sc->disk->d_drv1       = sc;

    disk_create(sc->disk, DISK_VERSION);
    return (0);
}
```

`malloc` con `M_WAITOK | M_ZERO` devuelve un buffer inicializado a cero o duerme hasta que haya uno disponible. No puede fallar en asignaciones pequeñas en un sistema en buen estado, por eso no comprobamos el valor de retorno aquí. Si estuviéramos asignando un buffer muy grande podríamos querer `M_NOWAIT` y manejo explícito de errores, pero para 1 MiB `M_WAITOK` es la opción idiomática.

`myblk_detach_unit` debe liberar el almacén de respaldo después de destruir el disco.

```c
static void
myblk_detach_unit(struct myblk_softc *sc)
{

    if (sc->disk != NULL) {
        disk_destroy(sc->disk);
        sc->disk = NULL;
    }
    if (sc->backing != NULL) {
        free(sc->backing, M_MYBLK);
        sc->backing = NULL;
        sc->backing_size = 0;
    }
}
```

El orden importa aquí. Primero destruimos el disco, lo que garantiza que no haya más BIOs en vuelo. Solo entonces liberamos el buffer de respaldo. Si liberáramos el buffer primero, un BIO en vuelo podría intentar hacer `memcpy` hacia o desde un puntero que ya no apunta a nuestra memoria, y el kernel se bloquearía en la siguiente operación de I/O.

### La función strategy

Llegamos ahora al núcleo del cambio. Reemplaza el marcador de posición `myblk_strategy` por una función que realmente atienda BIOs.

```c
static void
myblk_strategy(struct bio *bp)
{
    struct myblk_softc *sc;
    off_t offset;
    size_t len;

    sc = bp->bio_disk->d_drv1;
    offset = bp->bio_offset;
    len = bp->bio_bcount;

    if (offset < 0 ||
        offset > sc->backing_size ||
        len > sc->backing_size - offset) {
        bp->bio_error = EIO;
        bp->bio_flags |= BIO_ERROR;
        bp->bio_resid = len;
        biodone(bp);
        return;
    }

    switch (bp->bio_cmd) {
    case BIO_READ:
        mtx_lock(&sc->lock);
        memcpy(bp->bio_data, sc->backing + offset, len);
        mtx_unlock(&sc->lock);
        bp->bio_resid = 0;
        break;

    case BIO_WRITE:
        mtx_lock(&sc->lock);
        memcpy(sc->backing + offset, bp->bio_data, len);
        mtx_unlock(&sc->lock);
        bp->bio_resid = 0;
        break;

    case BIO_DELETE:
        mtx_lock(&sc->lock);
        memset(sc->backing + offset, 0, len);
        mtx_unlock(&sc->lock);
        bp->bio_resid = 0;
        break;

    case BIO_FLUSH:
        /*
         * In-memory backing store is always "flushed".
         * Nothing to do.
         */
        bp->bio_resid = 0;
        break;

    default:
        bp->bio_error = EOPNOTSUPP;
        bp->bio_flags |= BIO_ERROR;
        bp->bio_resid = len;
        break;
    }

    biodone(bp);
}
```

Leamos esto con atención. No es una función larga, pero cada línea hace algo que importa.

La primera línea encuentra nuestro softc. GEOM nos entrega el BIO con un puntero al disco en `bp->bio_disk`. Guardamos nuestro softc en `d_drv1` durante `disk_create`, así que lo recuperamos de ahí. Esto es el equivalente en el mundo del driver de bloques de `device_get_softc(dev)` en el mundo de Newbus.

El segundo par de líneas extrae el desplazamiento y la longitud de la solicitud. `bio_offset` es un desplazamiento en bytes dentro del medio. `bio_bcount` es el número de bytes a transferir. GEOM ya ha traducido las operaciones a nivel de archivo, a través de las capas que estén por encima de nosotros, a un rango de bytes lineal.

La comprobación de límites que sigue es programación defensiva. GEOM normalmente no nos enviará una solicitud que supere el tamaño del medio, porque divide y valida los BIOs en nuestro nombre. Pero los drivers defensivos comprueban de todas formas, porque una escritura fuera de límites aceptada silenciosamente puede corromper la memoria del kernel, y porque el coste de la comprobación es unas pocas instrucciones por solicitud. También nos protegemos contra el desbordamiento aritmético reescribiendo la comprobación obvia `offset + len > backing_size` como `len > backing_size - offset`, que no puede desbordarse porque `offset <= backing_size` en este punto.

El switch es donde ocurre el trabajo real. Cada comando BIO tiene su propio caso.

`BIO_READ` copia `len` bytes de nuestro almacén de respaldo a partir de `offset` hacia `bp->bio_data`. GEOM ha reservado `bp->bio_data` por nosotros, y se liberará cuando el BIO se complete. Nuestro trabajo es simplemente rellenarlo.

`BIO_WRITE` copia `len` bytes de `bp->bio_data` hacia nuestro almacén de respaldo a partir de `offset`. Es simétrico al caso de lectura.

`BIO_DELETE` pone a cero el rango. En un disco real, `BIO_DELETE` es la forma en que los sistemas de archivos señalan que un rango de bloques ya no está en uso y que el disco puede reclamarlo. Los SSD lo utilizan para activar TRIM. Para nuestro driver en memoria, no hay nada que reclamar, pero poner a cero el rango es una respuesta razonable, ya que refleja la semántica de "los datos han desaparecido".

`BIO_FLUSH` es una solicitud para confirmar las escrituras pendientes en almacenamiento estable. Nuestro almacenamiento nunca es volátil en el sentido en que un FLUSH sería de ayuda: cada `memcpy` ya es visible para el siguiente `memcpy` en el mismo orden en que se emitió. Devolvemos éxito sin hacer nada.

Cualquier otro comando que no reconozcamos recibe `EOPNOTSUPP`. Las capas de GEOM por encima de nosotros verán esto y reaccionarán en consecuencia.

Al final, `biodone(bp)` completa el BIO. Esto no es opcional. Cada BIO que entra en la función strategy debe salir a través de `biodone` exactamente una vez; de lo contrario, el BIO quedará perdido, el llamador se bloqueará indefinidamente y te resultará muy difícil diagnosticar el problema.

### El papel de bio_resid

Observa el manejo de `bp->bio_resid`. Este campo representa el número de bytes que quedan por transferir una vez que el driver ha terminado. Cuando la transferencia completa tiene éxito, `bio_resid` es cero. Cuando la transferencia falla por completo, `bio_resid` es igual a `bio_bcount`. Cuando la transferencia tiene éxito parcialmente, `bio_resid` es el número de bytes que no se transfirieron.

Nuestro driver transfiere todo o nada, así que establecemos `bio_resid` en `0` (éxito) o en `len` (error). Un driver de hardware real podría establecerlo en un valor intermedio si la transferencia se detiene a mitad de camino. Los sistemas de archivos y las herramientas del espacio de usuario usan `bio_resid` para determinar cuántos datos se movieron realmente.

### El lock

Tomamos `sc->lock` alrededor del `memcpy`. Para un driver en memoria que atiende una solicitud a la vez, el lock no hace mucho trabajo visible: la planificación de BIO del kernel hace que las solicitudes verdaderamente concurrentes sean poco probables en nuestro dispositivo de juguete. Pero el lock es una buena práctica de higiene. GEOM no garantiza que tu función strategy se invocará de forma serial, y aunque lo hiciera, un cambio futuro en el driver para añadir un worker thread asíncrono requeriría el lock de todas formas. Añadirlo ahora es más barato que añadirlo después.

Un driver más sofisticado podría usar un lock de grano fino, o podría usar un enfoque MPSAFE que se apoye en operaciones atómicas. Por ahora, un mutex grueso alrededor del `memcpy` es suficiente. Es correcto, sencillo de razonar y no perjudica el rendimiento en un pseudodispositivo.

### Reconstrucción y recarga

Tras actualizar el código fuente y descargar la versión anterior con `kldunload`, reconstruye y recarga.

```console
# make
cc -O2 -pipe -fno-strict-aliasing ...
# kldunload myblk
# kldload ./myblk.ko
# dmesg | tail -n 1
myblk: loaded, /dev/myblk0 size=1048576 bytes
```

Probemos ahora algo de I/O real.

```console
# dd if=/dev/zero of=/dev/myblk0 bs=4096 count=16
16+0 records in
16+0 records out
65536 bytes transferred in 0.001104 secs (59 MB/sec)
# dd if=/dev/myblk0 of=/dev/null bs=4096 count=16
16+0 records in
16+0 records out
65536 bytes transferred in 0.000512 secs (128 MB/sec)
```

Escribimos 64 KiB de ceros y los leímos de vuelta. Las velocidades que veas dependerán de tu hardware y de cuánto ayude el buffer cache, pero cualquier velocidad por encima de unos pocos MB/seg es válida para una primera prueba.

```console
# dd if=/dev/random of=/dev/myblk0 bs=4096 count=16
16+0 records in
16+0 records out
65536 bytes transferred in 0.001233 secs (53 MB/sec)
# dd if=/dev/myblk0 of=pattern.bin bs=4096 count=16
16+0 records in
16+0 records out
# dd if=/dev/myblk0 of=pattern2.bin bs=4096 count=16
16+0 records in
16+0 records out
# cmp pattern.bin pattern2.bin
#
```

Escribimos datos aleatorios, los leímos dos veces y confirmamos que ambas lecturas devuelven el mismo contenido. Nuestro driver es ahora un almacén coherente.

### Una ojeada rápida bajo carga

Ejecutemos una prueba de estrés breve y observemos `gstat`.

En una terminal:

```console
# while true; do dd if=/dev/urandom of=/dev/myblk0 bs=4096 \
    count=256 2>/dev/null; done
```

En otra terminal:

```console
# gstat -I 1 -f myblk0
dT: 1.002s  w: 1.000s
 L(q)  ops/s    r/s   kBps   ms/r    w/s   kBps   ms/w    %busy Name
    0    251      0      0    0.0    251   1004    0.0    2.0| myblk0
```

Unas 250 operaciones de escritura por segundo de 4 KiB cada una, aproximadamente 1 MB/seg. La latencia es muy baja porque el almacén subyacente es RAM. Para un disco real los números serían muy distintos, pero la estructura de lo que estás observando es la misma.

Detén la prueba de estrés con `Ctrl-C` en la primera terminal.

### Refinando el driver con soporte de ioctl

Muchas herramientas de almacenamiento envían un ioctl al dispositivo para consultar la geometría o emitir comandos. GEOM gestiona los más comunes por nosotros, pero si proporcionamos un callback `d_ioctl`, el kernel enrutará los ioctls desconocidos hacia nuestra función. Por ahora no implementamos ningún ioctl personalizado. Solo señalamos que el hook existe.

```c
static int
myblk_ioctl(struct disk *d, u_long cmd, void *data, int flag,
    struct thread *td)
{

    (void)d; (void)data; (void)flag; (void)td;

    switch (cmd) {
    /* No custom ioctls yet. */
    default:
        return (ENOIOCTL);
    }
}
```

Registramos el callback asignando `sc->disk->d_ioctl = myblk_ioctl;` antes de llamar a `disk_create`. Devolver `ENOIOCTL` en el caso por defecto le indica a GEOM que no gestionamos el comando y le da la oportunidad de pasar la solicitud a su propio manejador por defecto.

### Refinando el driver con soporte de getattr

GEOM usa `BIO_GETATTR` para solicitar a los dispositivos de almacenamiento atributos con nombre. Un sistema de archivos podría preguntar por `GEOM::rotation_rate` para saber si está sobre medios giratorios. La capa de particionado podría preguntar por `GEOM::ident` para obtener un identificador estable. Un callback `d_getattr` es el hook que nos permite responder.

```c
static int
myblk_getattr(struct bio *bp)
{
    struct myblk_softc *sc;

    sc = bp->bio_disk->d_drv1;

    if (strcmp(bp->bio_attribute, "GEOM::ident") == 0) {
        if (bp->bio_length < sizeof("MYBLK0"))
            return (EFAULT);
        strlcpy(bp->bio_data, "MYBLK0", bp->bio_length);
        bp->bio_completed = strlen("MYBLK0") + 1;
        return (0);
    }

    /* Let g_disk fall back to default behaviour. */
    (void)sc;
    return (-1);
}
```

Merece la pena detenerse en la convención de valores de retorno de `d_getattr`, porque confunde a muchos lectores la primera vez. Devolver `0` con `bio_completed` establecido le indica a `g_disk` que gestionamos el atributo correctamente. Devolver un valor errno positivo (como `EFAULT` para un buffer demasiado pequeño) le indica a `g_disk` que gestionamos el atributo pero la operación falló. Devolver `-1` le indica a `g_disk` que no reconocimos el atributo y que debe intentarlo con su manejador por defecto integrado. Por eso devolvemos `-1` al final: queremos que `g_disk` responda a los atributos estándar como `GEOM::fwsectors` en nuestro nombre. Para nuestro driver, responder a `GEOM::ident` con una cadena corta es suficiente para que aparezca en `diskinfo -v`. Registra esto con `sc->disk->d_getattr = myblk_getattr;` antes de `disk_create`.

### Escrituras parciales y lecturas cortas

Nuestro driver no produce en realidad escrituras parciales ni lecturas cortas, porque el almacén subyacente está en RAM y cada transferencia tiene éxito completo o falla por completo. Pero para un driver de hardware real, las transferencias parciales son normales: un disco puede devolver varios sectores correctamente y luego fallar en un sector defectuoso. El framework BIO admite esto mediante `bio_resid`, y un driver debe establecer `bio_resid` en el número de bytes que no se completaron.

La recomendación práctica es establecer siempre `bio_resid` explícitamente antes de llamar a `biodone`. Si la transferencia tuvo éxito completo, establécelo en cero. Si tuvo éxito parcial, establécelo en el residuo. Si falló por completo, establécelo en `bio_bcount`. Olvidar establecer `bio_resid` deja la basura que hubiera en el campo cuando se asignó el BIO, lo que puede confundir a los llamantes.

### Errores comunes en las funciones strategy

Antes de continuar, nombremos tres errores comunes que aparecen en las funciones strategy de los principiantes.

**Olvidar `biodone`.** Cada camino de salida de la función strategy debe llamar a `biodone(bp)` sobre el BIO. Si lo olvidas, el BIO se pierde y el llamante se bloquea indefinidamente. Esta es la fuente más habitual de problemas del tipo «mi montaje se cuelga».

**Mantener un lock a través de `biodone`.** `biodone` puede llamar hacia arriba hacia GEOM o hacia el manejador de completado de un sistema de archivos. Esos manejadores pueden tomar otros locks, o pueden necesitar adquirir locks que ya tienes, lo que da lugar a inversión del orden de locks y a un potencial deadlock. El patrón más seguro es soltar el lock antes de llamar a `biodone`. Nuestra versión simple lo hace implícitamente: el `mtx_unlock` siempre está dentro del switch, y `biodone` se ejecuta después del switch.

**Retornar de la función strategy con un código de error.** `d_strategy` es una función `void`. Los errores se notifican estableciendo `bio_error` y el flag `BIO_ERROR` en el BIO, no mediante el retorno. Los compiladores detectan esto si declaras la función correctamente, pero los principiantes a veces la escriben como si devolviera `int`, lo que genera advertencias del compilador que no deben ignorarse.

### BIOs encadenados y jerarquías de BIO

Un BIO puede tener un hijo. GEOM usa esto cuando una clase de transformación necesita dividir, combinar o transformar una solicitud en una o más solicitudes descendentes. Por ejemplo, una clase mirror podría tomar un BIO_WRITE y emitir dos BIOs hijos, uno a cada miembro del mirror. Una clase de partición podría tomar un BIO_READ y emitir un único BIO hijo con el desplazamiento ajustado al espacio de direcciones del proveedor subyacente.

La relación padre-hijo se registra en `bio_parent`. Cuando un hijo completa, su error se propaga al padre mediante `biodone`, que acumula errores y entrega al padre cuando todos los hijos han completado.

Nuestro driver no produce BIOs hijos. Los recibe como hojas de la cadena. Desde la perspectiva del driver, cada BIO es autónomo: tiene un desplazamiento, una longitud y un buffer de datos, y nuestra tarea es atenderlo.

Pero si alguna vez necesitas dividir un BIO dentro de tu driver (por ejemplo, si una solicitud cruza un límite que tu almacén subyacente gestiona en fragmentos separados), puedes usar `g_clone_bio` para crear un BIO hijo, `g_io_request` para despacharlo, y `g_std_done` o un manejador de completado personalizado para reconstruir al padre. El patrón es visible en varios lugares del kernel, incluyendo `g_mirror` y `g_raid`.

### El contexto de thread de la función strategy

La función strategy se ejecuta en cualquier thread que haya enviado el BIO. Para BIOs originados en el sistema de archivos, suele ser el thread syncer del sistema de archivos o un worker del buffer cache. Para acceso directo desde el espacio de usuario, es el thread de usuario que llamó a `read` o `write` en `/dev/myblk0`. Para transformaciones GEOM, podría ser el thread de eventos de GEOM o un worker thread específico de la clase.

Lo que esto significa para tu driver es que `d_strategy` puede ejecutarse en muchos contextos de thread distintos. No puedes asumir que `curthread` pertenece a ningún proceso en particular, y no puedes bloquearte durante mucho tiempo o el sistema de archivos llamante (o el programa de usuario) se detendrá.

Si tu función strategy necesita hacer algo lento (I/O contra un vnode, esperar al hardware o un locking complejo), el patrón correcto es poner en cola el BIO en una cola interna y dejar que un worker thread dedicado lo procese. Esto es lo que hace `md(4)` para todos los tipos de almacén subyacente, porque el I/O de vnode (por ejemplo) puede bloquearse durante un tiempo arbitrariamente largo.

Nuestro driver es completamente en memoria y solo hace `memcpy`, así que no necesitamos un worker thread. Pero entender el patrón es importante para el futuro.

### Un ejemplo detallado: lectura a través de un límite

Supón que un sistema de archivos emite un BIO_READ con desplazamiento 100000 y longitud 8192. Eso abarca los bytes del 100000 al 108191. Veamos paso a paso cómo nuestra función strategy lo gestiona.

1. `bp->bio_cmd` es `BIO_READ`.
2. `bp->bio_offset` es 100000.
3. `bp->bio_bcount` es 8192.
4. `bp->bio_data` apunta a un buffer del kernel (o a un buffer de usuario mapeado en el kernel) donde deben ir los 8192 bytes.

Nuestro código calcula `offset = 100000` y `len = 8192`. La verificación de límites pasa: `100000 + 8192 = 108192`, que es menor que nuestro `backing_size` de 32 MiB (33554432).

El switch entra en el caso `BIO_READ`. Adquirimos el lock, hacemos `memcpy` de 8192 bytes desde `sc->backing + 100000` hacia `bp->bio_data` y soltamos el lock. Establecemos `bp->bio_resid = 0` para indicar una transferencia completa. Caemos hacia `biodone(bp)`, que completa el BIO.

El sistema de archivos recibe el completado, observa que el error es cero y usa los 8192 bytes. La lectura está completa.

Supón ahora, en cambio, que el desplazamiento era 33554431 y la longitud era de 2 bytes. Eso es un byte dentro del almacén subyacente y un byte más allá del final.

1. `offset = 33554431`.
2. `len = 2`.

La verificación de límites: `offset > sc->backing_size` evalúa `33554431 > 33554432`, que es falso. `len > sc->backing_size - offset` evalúa `2 > 33554432 - 33554431`, lo que da `2 > 1`, que es verdadero. La verificación falla y caemos en el camino de error: establecemos `bio_error = EIO`, activamos el flag `BIO_ERROR`, establecemos `bio_resid = 2` y llamamos a `biodone`. El sistema de archivos ve el error y lo gestiona.

Observa cómo usamos la resta para evitar el riesgo de desbordamiento. Si hubiéramos escrito `offset + len > sc->backing_size`, y tanto `offset` como `len` hubieran estado cerca del máximo de `off_t`, la suma podría desbordarse hacia un valor pequeño y la verificación pasaría silenciosamente para una solicitud malformada. Las verificaciones de límites defensivas siempre reorganizan la aritmética para evitar el desbordamiento.

### El efecto secundario de devstat

Una característica agradable de usar `g_disk` es que la contabilidad de devstat es automática. Cada BIO que atendemos es contabilizado por `iostat` y `gstat`. No se necesita código adicional.

Puedes verificar esto con `iostat -x 1` en otra terminal mientras ejecutas el bucle de estrés.

```text
                        extended device statistics
device     r/s     w/s    kr/s    kw/s  ms/r  ms/w  ms/o  ms/t qlen  %b
ada0         0       2       0      48   0.0   0.1   0.0   0.1    0   0
myblk0       0     251       0    1004   0.0   0.0   0.0   0.0    0   2
```

Si nuestro driver estuviera construido sobre la API `g_class` directa en lugar de `g_disk`, tendríamos que conectar devstat nosotros mismos. Esta es una de las pequeñas mejoras de calidad de vida que `g_disk` nos proporciona gratuitamente.

### Cerrando la sección 5

Reemplazamos la función strategy de marcador de posición por un manejador funcional. Nuestro driver ahora atiende `BIO_READ`, `BIO_WRITE`, `BIO_DELETE` y `BIO_FLUSH` correctamente contra un almacén subyacente en memoria. Participa en devstat, coopera con `gstat` y acepta tráfico real de `dd`.

En la próxima sección, cruzaremos la frontera del acceso de bloque crudo al acceso de sistema de archivos. Formatearemos el dispositivo con `newfs_ufs`, lo montaremos, crearemos archivos en él y observaremos cómo cambia el camino de solicitudes cuando un sistema de archivos real se sitúa por encima del proveedor.

## Sección 6: Montaje de un sistema de archivos en el dispositivo

Hasta este punto, nuestro driver ha sido utilizado mediante acceso raw: `dd`, `diskinfo` y herramientas similares leen y escriben toda la superficie como un rango de bytes plano. Es un modo valioso, pero no es el modo en que vive la mayoría de los dispositivos de almacenamiento en el mundo real. Los dispositivos de almacenamiento sirven, en la práctica, a sistemas de archivos. Esta sección lleva nuestro driver hasta el final: lo formatearemos, montaremos un sistema de archivos real sobre él, crearemos archivos y observaremos cómo la fontanería de la capa de bloques del kernel enruta las peticiones cuando hay un sistema de archivos de por medio.

Esta es también la primera sección en la que la distinción teórica entre acceso raw y acceso mediante sistema de archivos se vuelve concreta. Entender esa diferencia, y poder verla en acción, es uno de los conocimientos más útiles que un autor de drivers de almacenamiento puede adquirir.

### El plan

Haremos lo siguiente en esta sección, en orden.

1. Aumentar el tamaño del soporte de nuestro driver de 1 MiB a algo suficientemente grande para albergar un sistema de archivos UFS funcional.
2. Compilar y cargar el driver actualizado.
3. Ejecutar `newfs_ufs` sobre el dispositivo para crear un sistema de archivos.
4. Montar el sistema de archivos en un directorio temporal.
5. Crear algunos archivos y verificar que los datos se leen correctamente.
6. Desmontar el sistema de archivos.
7. Recargar el módulo y observar qué ocurre.

Al terminar, habrás visto un sistema de archivos completo funcionando sobre tu propio driver de bloques.

### Aumentar el tamaño del soporte

UFS tiene un tamaño mínimo práctico. Es posible crear sistemas de archivos UFS muy pequeños, pero la sobrecarga del superbloque, los grupos de cilindros y las tablas de inodos consume una fracción notable del espacio en cualquier tamaño inferior a unos pocos megabytes. Para nuestros propósitos, 32 MiB es un tamaño cómodo: suficientemente pequeño para que el almacén de respaldo quepa en un simple `malloc`, y suficientemente grande para que UFS tenga espacio de sobra.

Actualiza las definiciones de tamaño al comienzo de `myfirst_blk.c`.

```c
#define MYBLK_SECTOR     512
#define MYBLK_MEDIASIZE  (32 * 1024 * 1024)   /* 32 MiB */
```

Vuelve a compilar.

```console
# make clean
# make
# kldunload myblk
# kldload ./myblk.ko
# diskinfo -v /dev/myblk0
/dev/myblk0
        512             # sectorsize
        33554432        # mediasize in bytes (32M)
        65536           # mediasize in sectors
        0               # stripesize
        0               # stripeoffset
```

32 MiB es suficiente.

### Formatear con newfs_ufs

`newfs_ufs` es el formateador UFS estándar en FreeBSD. Escribe el superbloque, los grupos de cilindros, el inodo raíz y todas las demás estructuras que requiere un sistema de archivos UFS. Vamos a ejecutarlo sobre nuestro dispositivo.

```console
# newfs_ufs /dev/myblk0
/dev/myblk0: 32.0MB (65536 sectors) block size 32768, fragment size 4096
        using 4 cylinder groups of 8.00MB, 256 blks, 1280 inodes.
super-block backups (for fsck_ffs -b #) at:
192, 16576, 32960, 49344
```

Bajo la superficie ocurrieron varias cosas.

`newfs_ufs` abrió `/dev/myblk0` para escritura, lo que hizo que el contador de accesos de GEOM aumentara. Nuestra función strategy recibió entonces una serie de escrituras: primero el superbloque, después los grupos de cilindros, luego el directorio raíz vacío y, por último, las copias de seguridad del superbloque. Cada una de esas escrituras es un BIO, y cada BIO fue gestionado por nuestro driver.

Puedes verificar que `newfs_ufs` realmente escribió en el dispositivo leyendo algunos bytes.

```console
# dd if=/dev/myblk0 bs=1 count=16 2>/dev/null | hexdump -C
00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
```

Los primeros bytes de una partición UFS son cero intencionadamente, porque el superbloque no reside en el desplazamiento cero: se encuentra en el desplazamiento 65536 (bloque 128) para dejar espacio a los bloques de boot y otros preámbulos. Vamos a mirar allí.

```console
# dd if=/dev/myblk0 bs=512 count=2 skip=128 2>/dev/null | hexdump -C | head
00010000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
00010010  80 00 00 00 80 00 00 00  a0 00 00 00 00 00 00 00
...
```

Ahora deberías ver bytes distintos de cero. Eso es el superbloque que `newfs_ufs` escribió en nuestro almacén de respaldo.

### Montar el sistema de archivos

Crea un punto de montaje y monta el sistema de archivos.

```console
# mkdir -p /mnt/myblk
# mount /dev/myblk0 /mnt/myblk
# mount | grep myblk
/dev/myblk0 on /mnt/myblk (ufs, local)
# df -h /mnt/myblk
Filesystem    Size    Used   Avail Capacity  Mounted on
/dev/myblk0    31M    8.0K     28M     0%    /mnt/myblk
```

Nuestro pseudodispositivo es ahora un sistema de archivos real. Observa los contadores de acceso de GEOM.

```console
# geom disk list myblk0 | grep Mode
   Mode: r1w1e1
```

`r1w1e1` significa un lector, un escritor y un titular exclusivo. El titular exclusivo es UFS: le ha indicado a GEOM que es la única autoridad sobre las escrituras en el dispositivo hasta que se desmonte.

### Crear y leer archivos

Vamos a usar el sistema de archivos de verdad.

```console
# echo "hello from myblk" > /mnt/myblk/hello.txt
# ls -l /mnt/myblk
total 4
-rw-r--r--  1 root  wheel  17 Apr 19 18:17 hello.txt
# cat /mnt/myblk/hello.txt
hello from myblk
```

Observa lo que acaba de ocurrir. La llamada `echo "hello from myblk" > /mnt/myblk/hello.txt` viajó a través de la capa de syscalls hasta `sys_openat`, luego hasta VFS y después hasta UFS, que abrió el inodo del directorio raíz, creó un nuevo inodo para `hello.txt`, asignó un bloque de datos, copió los 17 bytes en el buffer cache y programó una escritura diferida. El buffer cache acabó llamando a GEOM, que a su vez llamó a nuestra función strategy, que copió esos bytes en nuestro almacén de respaldo.

Cuando después ejecutaste `cat`, la solicitud recorrió la misma pila. Sin embargo, como los datos seguían en el buffer cache por la escritura reciente, UFS no necesitó realmente leer desde nuestro dispositivo. El buffer cache sirvió la lectura directamente desde RAM. Si desmontaras y volvieras a montar, verías una lectura real.

```console
# umount /mnt/myblk
# mount /dev/myblk0 /mnt/myblk
# cat /mnt/myblk/hello.txt
hello from myblk
```

Ese segundo `cat` probablemente sí generó solicitudes `BIO_READ` que llegaron a nuestro driver, porque el ciclo de desmontaje y remontaje invalidó el buffer cache de ese sistema de archivos.

### Observar el tráfico

`gstat` nos muestra el tráfico de BIOs en tiempo real. Abre otro terminal y ejecuta `gstat -I 1 -f myblk0`. Luego, en el primer terminal, crea un archivo grande.

```console
# dd if=/dev/zero of=/mnt/myblk/big bs=1m count=16
16+0 records in
16+0 records out
16777216 bytes transferred in 0.150 secs (112 MB/sec)
```

En el terminal de `gstat`, deberías ver una ráfaga de escrituras, quizás distribuida a lo largo de uno o dos segundos dependiendo de la velocidad con que el buffer cache se vacíe.

```text
 L(q)  ops/s    r/s   kBps   ms/r    w/s   kBps   ms/w    %busy Name
    0    128      0      0    0.0    128  16384    0.0   12.0| myblk0
```

Estas son las escrituras de 4 KiB o 32 KiB (según el tamaño de bloque de UFS) que UFS emite para llenar el archivo. Podemos verificar la presencia del archivo.

```console
# ls -lh /mnt/myblk
total 16460
-rw-r--r--  1 root  wheel    16M Apr 19 18:19 big
-rw-r--r--  1 root  wheel    17B Apr 19 18:17 hello.txt
# du -ah /mnt/myblk
 16M    /mnt/myblk/big
4.5K    /mnt/myblk/hello.txt
 16M    /mnt/myblk
```

Y podemos borrarlo de nuevo para observar el tráfico `BIO_DELETE`.

```console
# rm /mnt/myblk/big
```

UFS no emite `BIO_DELETE` por defecto a menos que el sistema de archivos se haya montado con la opción `trim`, de modo que en un montaje básico verás prácticamente ningún tráfico de BIOs al borrar: UFS simplemente marca los bloques como libres en sus propios metadatos. Para ver `BIO_DELETE`, necesitaríamos montar con `-o trim`, algo que trataremos brevemente en los laboratorios.

### Desmontar

Desmonta el sistema de archivos antes de descargar el módulo.

```console
# umount /mnt/myblk
# geom disk list myblk0 | grep Mode
   Mode: r0w0e0
```

El contador de accesos volvió a cero en cuanto UFS liberó su titularidad exclusiva. Nuestro driver ya está libre para ser descargado o para seguir trasteando con él.

### Intentar descargar con el sistema de archivos montado

¿Qué ocurre si olvidas el `umount` e intentas descargar el módulo?

```console
# mount /dev/myblk0 /mnt/myblk
# kldunload myblk
kldunload: can't unload file: Device busy
```

El kernel se niega. El subsistema `g_disk` sabe que nuestro provider todavía tiene un titular exclusivo activo y no permitirá que `disk_destroy` continúe hasta que se libere. Es el mismo mecanismo que vimos en el capítulo 26 protegiendo el dispositivo serie USB durante una sesión activa, pero elevado a la capa de GEOM.

Esta es una característica de seguridad. Descargar el módulo mientras un sistema de archivos está montado sobre el dispositivo de respaldo provocaría un pánico del kernel en el siguiente BIO: la función strategy ya no existiría, pero UFS seguiría intentando llamarla.

Desmonta primero y luego descarga.

```console
# umount /mnt/myblk
# kldunload myblk
# kldstat | grep myblk
# 
```

Limpio.

### Una breve anatomía de UFS sobre nuestro driver

Ahora que tenemos UFS montado sobre nuestro dispositivo, vale la pena detenerse a observar qué hay realmente en el almacén de respaldo. UFS es un sistema de archivos bien documentado, y ver sus estructuras en su lugar en un dispositivo que controlamos es muy instructivo.

Los primeros 65535 bytes de un sistema de archivos UFS están reservados para el área de boot. En nuestro dispositivo, todos esos bytes son cero porque `newfs_ufs` no escribe un sector de boot por defecto.

En el desplazamiento 65536 reside el superbloque. El superbloque es una estructura de tamaño fijo que describe la geometría del sistema de archivos: el tamaño de bloque, el tamaño de fragmento, el número de grupos de cilindros, la ubicación del inodo raíz y muchos otros invariantes. `newfs_ufs` escribe primero el superbloque, y también escribe copias de seguridad en desplazamientos predecibles por si el primario resulta dañado.

Tras el superbloque vienen los grupos de cilindros. Cada grupo de cilindros contiene inodos, bloques de datos y metadatos para una porción del espacio de direcciones del sistema de archivos. El número y el tamaño de los grupos de cilindros dependen del tamaño del sistema de archivos. Nuestro sistema de archivos de 32 MiB tiene cuatro grupos de cilindros de 8 MiB cada uno.

Dentro de cada grupo de cilindros se encuentran los bloques de inodos. Cada inodo es una estructura pequeña (256 bytes en FreeBSD UFS2) que describe un único archivo o directorio: su tipo, propietario, permisos, marcas de tiempo, tamaño y las direcciones de bloque de sus datos.

Por último, los propios bloques de datos contienen el contenido de los archivos. Estos se asignan a partir del mapa de bloques libres del grupo de cilindros.

Cuando escribimos `"hello from myblk"` en `/mnt/myblk/hello.txt`, el kernel hizo aproximadamente lo siguiente:

1. VFS pidió a UFS que creara un nuevo archivo `hello.txt` en el directorio raíz.
2. UFS asignó un inodo de la tabla de inodos del grupo de cilindros raíz.
3. UFS actualizó el inodo del directorio raíz para incluir una entrada para `hello.txt`.
4. UFS asignó un bloque de datos para el archivo.
5. UFS escribió los 17 bytes de contenido en ese bloque de datos.
6. UFS escribió de vuelta el inodo actualizado.
7. UFS escribió de vuelta la entrada de directorio actualizada.
8. UFS actualizó su contabilidad interna.

Cada uno de esos pasos se convirtió en uno o más BIOs hacia nuestro driver. La mayoría fueron escrituras pequeñas en bloques de metadatos. El contenido del archivo en sí fue un único BIO. La función Soft Updates de UFS ordena las escrituras para garantizar la consistencia ante caídas.

Si quieres ver estos BIOs en acción, ejecuta tu línea de DTrace del laboratorio 7 mientras creas un archivo. Verás una pequeña ráfaga de escrituras en torno al momento del `echo`.

### Cómo funciona mount en realidad

El comando `mount(8)` es un envoltorio de la syscall `mount(2)`. Esa syscall toma un tipo de sistema de archivos, un dispositivo origen y un punto de montaje destino, y le pide al kernel que realice el montaje.

La respuesta del kernel es localizar el código de sistema de archivos apropiado según el tipo (UFS, ZFS, tmpfs, etc.) y llamar a su manejador de montaje, que en el caso de UFS es `ufs_mount` en `/usr/src/sys/ufs/ffs/ffs_vfsops.c`. El manejador de montaje valida el origen, lo abre como consumidor de GEOM, lee el superbloque, verifica que esté bien formado, asigna una estructura de montaje en memoria y la instala en el espacio de nombres.

Desde el punto de vista de nuestro driver, nada de esto es visible. Nosotros vemos una serie de BIOs: primero unas pocas lecturas del superbloque, luego lo que UFS necesite para inicializar su estado en memoria. Una vez que el montaje ha tenido éxito, UFS emite BIOs según su propio ritmo a medida que se usa el sistema de archivos.

Si el montaje falla, UFS informa del error y el código de montaje del kernel limpia. El consumidor de GEOM se desvincula, el contador de accesos baja y el espacio de nombres queda intacto. Nuestro driver no necesita hacer nada especial ante un fallo de montaje.

### La interfaz de caracteres de GEOM

Anteriormente en el capítulo dijimos que el acceso directo a través de `/dev/myblk0` pasa por la «interfaz de caracteres de GEOM». Aquí explicamos con más detalle qué significa eso.

GEOM publica un dispositivo de caracteres para cada provider. No es lo mismo que un `cdev` creado con `make_dev`; es una ruta especializada dentro de GEOM que presenta un provider como dispositivo de caracteres a devfs. El código correspondiente reside en `/usr/src/sys/geom/geom_dev.c`.

Cuando un programa de usuario abre `/dev/myblk0`, devfs enruta la llamada `open` al código de la interfaz de caracteres de GEOM, que adjunta un consumidor a nuestro provider con el modo de acceso solicitado. Cuando el programa escribe, el código de la interfaz de caracteres de GEOM construye un BIO y lo emite a nuestro provider, que lo enruta a nuestra función strategy. Cuando el programa cierra el descriptor de archivo, GEOM desvincula el consumidor y libera el acceso.

La capa de interfaz de caracteres traduce entre `struct uio` (el descriptor de I/O del espacio de usuario) y `struct bio` (el descriptor de I/O de la capa de bloques). Divide las operaciones de I/O de usuario grandes en múltiples BIOs cuando es necesario, respetando el `d_maxsize` que especificamos.

Todo esto es invisible para nuestro driver. Nosotros simplemente recibimos BIOs. Pero saber que existe la interfaz de caracteres te ayuda a entender por qué ciertas operaciones del espacio de usuario se traducen en determinados patrones de BIOs, y por qué `d_maxsize` importa.

### Lo que los sistemas de archivos necesitan de un driver de bloques

Ahora que hemos montado de verdad un sistema de archivos sobre nuestro driver, podemos describir con más precisión qué requiere un sistema de archivos del driver de bloques subyacente.

Un sistema de archivos necesita **lecturas y escrituras correctas**. Si una escritura en el desplazamiento X va seguida de una lectura en el desplazamiento X, la lectura debe devolver lo que la escritura puso allí, con la granularidad del tamaño de sector. Garantizamos esto con `memcpy` hacia y desde nuestro almacén de respaldo.

Un sistema de archivos necesita **límites correctos**. El driver de bloques no debe aceptar lecturas ni escrituras que superen el tamaño del soporte. Comprobamos esto explícitamente en la función strategy.

Un sistema de archivos necesita **un tamaño de soporte estable**. El tamaño del dispositivo no debe cambiar bajo los pies del sistema de archivos una vez montado, porque los metadatos del sistema de archivos codifican desplazamientos y conteos que asumen un tamaño fijo. Nuestro driver mantiene el tamaño del soporte constante.

Un sistema de archivos necesita **seguridad ante caídas**, en la medida en que el almacenamiento subyacente la proporcione. UFS puede recuperarse de un apagado incorrecto si el almacén de respaldo no pierde las escrituras confirmadas previamente. Nuestro driver respaldado en RAM lo pierde todo al reiniciar, pero al menos es coherente consigo mismo mientras está en funcionamiento. En la sección 7, presentaremos opciones para la persistencia.

Un sistema de archivos a veces necesita **semántica de vaciado**. Una llamada a `BIO_FLUSH` debe garantizar que todas las escrituras emitidas previamente sean duraderas antes de retornar. Nuestro driver respaldado en RAM satisface esto de manera trivial, porque no hay escritura diferida en su camino.

Por último, un sistema de archivos se beneficia de un **acceso secuencial rápido**. Se trata de una cuestión de calidad de servicio, no de corrección, y nuestro driver no tiene ningún problema en este sentido porque `memcpy` es rápido.

### Acceso raw frente a acceso mediante sistema de archivos, de forma visual

Dibujemos los dos caminos de acceso en paralelo, usando nuestro driver real como punto de referencia.

```text
Raw access:                          Filesystem access:

  dd(1)                                cat(1)
   |                                    |
   v                                    v
  open("/dev/myblk0")                  open("/mnt/myblk/hello.txt")
   |                                    |
   v                                    v
  read(fd, ...)                        read(fd, ...)
   |                                    |
   v                                    v
  sys_read                             sys_read
   |                                    |
   v                                    v
  devfs                                VFS
   |                                    |
   v                                    v
  GEOM character                       UFS
  interface                            (VOP_READ, bmap)
   |                                    |
   |                                    v
   |                                   buffer cache
   |                                    |
   v                                    v
  GEOM topology  <--------------------- GEOM topology
   |                                    |
   v                                    v
  myblk_strategy (BIO_READ)            myblk_strategy (BIO_READ)
```

Los dos últimos saltos son idénticos. Nuestra función de estrategia se invoca exactamente de la misma forma independientemente de si la petición proviene de `dd` o de `cat` sobre un archivo montado. Esta es la gran ventaja de operar en la capa de bloques: no necesitamos distinguir entre los dos caminos. Las capas superiores se encargan de traducir las operaciones a nivel de archivo en operaciones a nivel de bloque, y nosotros trabajamos con bloques.

### Observar el camino de las peticiones con DTrace

Si quieres ver el camino de las peticiones de forma explícita, DTrace puede ayudarte.

```console
# dtrace -n 'fbt::myblk_strategy:entry { printf("cmd=%d off=%lld len=%u", \
    args[0]->bio_cmd, args[0]->bio_offset, args[0]->bio_bcount); }'
```

Con la sonda en ejecución, realiza alguna operación en el sistema de archivos montado desde otro terminal y observa cómo llegan los BIOs. Verás cómo las lecturas llegan en fragmentos de entre 512 bytes y 32 KiB, según el tamaño de bloque de UFS y la operación que hayas realizado. Ejecutar `dd if=/dev/zero of=/mnt/myblk/test bs=1m count=1` produce una ráfaga de escrituras de 32 KiB.

DTrace es una de las herramientas de observabilidad más potentes que proporciona FreeBSD, y cobra vida especialmente con el trabajo de almacenamiento porque el camino BIO está muy instrumentado. Lo usaremos más en capítulos posteriores, pero incluso un comando de una sola línea como el anterior es suficiente para hacer concreto el camino abstracto.

### Cerrando la sección 6

Nuestro pseudo dispositivo de bloques ya desempeña el papel completo de un dispositivo de almacenamiento: acceso raw a través de `dd`, acceso mediante sistema de archivos a través de UFS, y convivencia segura con las protecciones de desmontaje del kernel. La función de estrategia que escribimos en la sección 5 no necesitó cambio alguno para que UFS funcionara, porque UFS y `dd` comparten el mismo protocolo de capa de bloques por debajo.

También hemos visto el flujo de extremo a extremo: VFS en la cima, UFS justo debajo, el buffer cache entre ambos, GEOM por debajo de eso, y nuestro driver al final de todo. Ese flujo es el mismo para cada driver de almacenamiento en FreeBSD. Ahora sabes cómo ocupar la parte inferior de ese flujo.

En la siguiente sección, centraremos nuestra atención en la persistencia. Un dispositivo respaldado por RAM es cómodo para pruebas, pero pierde su contenido en cada recarga. Discutiremos las opciones para hacer persistente el almacén de respaldo, los compromisos que implica cada opción, y cómo añadir una de ellas a nuestro driver.

## Sección 7: Persistencia y almacenes de respaldo en memoria

Nuestro driver es autocoherente mientras está en ejecución. Si escribes un byte en el desplazamiento X, puedes leerlo de nuevo en ese mismo desplazamiento momentos después. Si creas un archivo en el sistema de archivos montado, puedes leerlo de nuevo hasta que desmontes o descargues el módulo. Esto ya es útil para pruebas y para cargas de trabajo de corta duración.

Sin embargo, no es duradero. Descarga el módulo y el buffer de respaldo queda liberado. Reinicia la máquina y cada byte desaparece. Para un driver de enseñanza esto es, podría decirse, una característica: arranca limpio, no acumula estado entre ejecuciones y no puede corromper silenciosamente una sesión anterior. Pero entender las opciones para hacer persistente el almacenamiento es esencial para el trabajo real con drivers, así que esta sección repasa las principales elecciones y luego muestra cómo añadir el tipo más sencillo de persistencia a nuestro driver.

### Por qué la persistencia es difícil

La persistencia del almacenamiento no consiste únicamente en dónde residen los bytes. Se trata de tres propiedades entrelazadas.

**Durabilidad** significa que, una vez que una escritura retorna, los datos están a salvo frente a un fallo del sistema. En un disco físico, la durabilidad está típicamente ligada a la política de caché del propio disco: la escritura llega al buffer interno de la unidad, luego al plato, y entonces la unidad notifica la finalización. `BIO_FLUSH` es el gancho que proporciona a los sistemas de archivos un mecanismo para exigir semántica de volcado hasta el plato.

**Consistencia** significa que una lectura en el desplazamiento X devuelve la escritura más reciente en ese desplazamiento, no una versión anterior o parcial. La consistencia la proporciona normalmente el hardware o un locking cuidadoso en el driver.

**Seguridad ante caídas** significa que, tras un apagado no limpio, el estado del almacenamiento es utilizable. O bien refleja todas las escrituras confirmadas, o bien refleja un prefijo bien definido de ellas. UFS dispone de SU+J (Soft Updates con Journaling) para facilitar la recuperación tras una caída; ZFS utiliza copy-on-write y transacciones atómicas. Todo eso depende de una capa de bloques que se comporte de forma predecible.

Para un driver de enseñanza, no necesitamos abordar las tres con todo el rigor. Necesitamos entender cuáles son las opciones y elegir una que se adapte a nuestros objetivos.

### Las opciones

Hay cuatro formas habituales de respaldar un pseudo dispositivo de bloques.

**Respaldo en memoria (nuestra elección actual)**. Rápido, sencillo, se pierde al recargar. Implementado como un buffer asignado con `malloc`. Escala mal más allá de unos pocos MiB porque exige memoria contigua del kernel.

**Respaldo en memoria página a página**. `md(4)` lo utiliza internamente para discos de memoria grandes. En lugar de un único buffer grande, el driver mantiene una tabla de indirección de asignaciones del tamaño de una página y las rellena bajo demanda. Esto escala hasta tamaños muy grandes y evita desperdiciar memoria en regiones dispersas, pero es más complejo.

**Respaldo por vnode**. El driver abre un archivo en el sistema de archivos anfitrión y lo usa como almacén de respaldo. `mdconfig -t vnode` es el ejemplo clásico. Las lecturas y escrituras pasan a través del sistema de archivos anfitrión, lo que proporciona persistencia a costa de velocidad y de una dependencia de la corrección del sistema de archivos anfitrión. Así es como FreeBSD arranca a menudo desde una imagen de disco de memoria embebida en el kernel: el kernel carga la imagen, la presenta como `/dev/md0`, y el sistema de archivos raíz se ejecuta sobre ella.

**Respaldo por swap**. El driver usa un objeto de VM respaldado por swap como almacén de respaldo. `mdconfig -t swap` utiliza este enfoque. Proporciona persistencia entre reinicios solo en la medida en que el swap sea persistente, lo que en la mayoría de los sistemas no es el caso. Pero proporciona un espacio de direcciones disperso muy grande sin consumir memoria física hasta que se accede a él, lo que es útil para almacenamiento temporal.

Para este capítulo, nos quedaremos con la opción en memoria. Es la más sencilla, es suficiente para los laboratorios y demuestra con claridad todos los demás conceptos. Discutiremos cómo cambiar al almacenamiento respaldado por vnode como ejercicio, y señalaremos `md(4)` para quienes quieran ver una implementación completa.

### Guardar y restaurar el buffer

Si queremos que nuestro dispositivo recuerde su contenido entre recargas, sin cambiar el enfoque de respaldo, podemos guardar el buffer en un archivo al descargar y restaurarlo al cargar. Esto no es elegante, pero es directo, e ilustra el contrato con claridad: el driver es responsable de cargar los bytes de respaldo en memoria antes de que llegue el primer BIO y de volcarlos a un lugar seguro antes de que se emita el último BIO.

En nuestro caso, la mecánica tendría este aspecto.

Al cargar el módulo, después de asignar el buffer de respaldo pero antes de llamar a `disk_create`, leer opcionalmente un archivo del sistema de archivos anfitrión en el buffer. Al descargar el módulo, después de que `disk_destroy` haya finalizado, escribir opcionalmente el buffer de vuelta en ese archivo.

Hacer esto de forma limpia desde dentro del kernel requiere la API de vnode. El kernel proporciona `vn_open`, `vn_rdwr` y `vn_close`, que juntas permiten a un módulo leer o escribir una ruta en el sistema de archivos anfitrión. No son APIs que queramos usar a la ligera, porque no están diseñadas para I/O de alto rendimiento desde dentro de un driver, y porque se ejecutan sobre el sistema de archivos que esté montado en esa ruta en ese momento, lo que no siempre es seguro. Pero para un guardado y restauración puntuales en el momento de carga y descarga, son aceptables.

Por razones didácticas no lo implementaremos. La forma correcta de persistir el contenido de un dispositivo de bloques es usar un almacén de respaldo real, no capturar una instantánea de un buffer en RAM. Pero entender la técnica ayuda a clarificar el contrato.

### El contrato con las capas superiores

Independientemente de tu almacén de respaldo, el contrato con las capas superiores está claramente definido.

**Un BIO_WRITE que completa con éxito debe ser visible para todas las peticiones BIO_READ posteriores**, independientemente de las capas de buffering. Nuestro driver en memoria cumple esto porque `memcpy` es el efecto visible.

**Un BIO_FLUSH que completa con éxito debe haber hecho duraderas todas las peticiones BIO_WRITE anteriores que completaron con éxito**. Nuestro driver en memoria cumple esto de forma trivial porque no hay ninguna capa inferior entre nuestro `memcpy` y la memoria de respaldo; todas las escrituras son «duraderas» en el sentido que podemos ofrecer. Un driver de disco real normalmente emite un comando de vaciado de caché al hardware en respuesta a `BIO_FLUSH`.

**Un BIO_DELETE puede descartar datos pero no debe corromper los bloques adyacentes**. Nuestro driver en memoria cumple esto poniendo a cero solo el rango solicitado. Un driver de SSD real podría emitir TRIM para el rango; un driver de HDD real normalmente no tiene soporte hardware para DELETE y puede ignorarlo de forma segura.

**Un BIO_READ debe devolver el contenido del medio o un error; no debe devolver memoria sin inicializar, datos en caché obsoletos de una transacción diferente ni bytes aleatorios**. Nuestro driver en memoria cumple esto poniendo a cero el respaldo en el momento de la asignación y escribiendo únicamente a través de la función de estrategia.

Si tienes presentes estas cuatro reglas al diseñar un nuevo driver, evitarás casi todos los errores de corrección que acechan a los nuevos drivers de almacenamiento.

### Lo que hace `md(4)` de forma diferente

El driver `md(4)` del kernel es un driver de disco en memoria maduro y con soporte para múltiples tipos. Soporta cinco tipos de respaldo: malloc, preload, swap, vnode y null. Cada tipo tiene su propia función de estrategia que sabe cómo atender las peticiones para ese tipo de respaldo. Leer `/usr/src/sys/dev/md/md.c` es un valioso complemento a este capítulo porque muestra cómo un driver real maneja todos los casos que aquí estamos pasando por alto.

Hay algunas cosas concretas que hace `md(4)` y que nosotros no hacemos.

`md(4)` usa un thread trabajador dedicado por unidad. Los BIOs entrantes se encolan en el softc, y el thread trabajador los desencola uno a uno y los despacha. Esto permite que la función de estrategia sea muy sencilla: simplemente encolar y señalizar. También aísla el trabajo bloqueante en el thread trabajador, lo que es importante para el tipo de respaldo vnode porque `vn_rdwr` puede bloquear.

`md(4)` usa `DEV_BSHIFT` (que vale `9`, es decir, sectores de 512 bytes) de forma coherente y usa aritmética entera en lugar de coma flotante para manejar los desplazamientos. Esta es la práctica estándar en la capa de bloques.

`md(4)` tiene una superficie ioctl completa para la configuración. La herramienta `mdconfig` se comunica con el kernel a través de ioctls sobre `/dev/mdctl`, y el driver admite `MDIOCATTACH`, `MDIOCDETACH`, `MDIOCQUERY` y `MDIOCRESIZE`. Nosotros no hemos implementado nada comparable, porque para nuestro pseudo dispositivo la configuración está fijada en tiempo de compilación.

`md(4)` usa `DISK_VERSION_06`, que es la versión actual del ABI de `g_disk`. Nuestro driver hace lo mismo, a través de la macro `DISK_VERSION`.

Si quieres ver un pseudo dispositivo de bloques de calidad de producción, `md(4)` es la referencia canónica. Casi todo lo que estamos construyendo terminaría, en un driver real, pareciéndose a la forma de `md(4)` con el tiempo.

### Una nota sobre la memoria respaldada por swap

Una técnica que merece ser mencionada, aunque no la usaremos aquí, es la memoria respaldada por swap. En lugar de un buffer asignado con `malloc`, un driver puede asignar un objeto de VM de tipo `OBJT_SWAP` y mapear páginas de él bajo demanda. Las páginas están respaldadas por espacio de swap, lo que significa que pueden enviarse al swap cuando el sistema está bajo presión de memoria y volver a cargarse cuando se accede a ellas. Esto te proporciona un almacén de respaldo muy grande, disperso y bajo demanda que se comporta como RAM cuando está caliente y como disco cuando está frío.

`md(4)` usa exactamente este enfoque para sus discos de memoria respaldados por swap. El objeto de VM de swap actúa como almacén de respaldo que el subsistema de VM del kernel gestiona por nosotros, sin que el driver necesite asignar memoria física contigua de antemano. El objeto `OBJT_SWAP` puede albergar terabytes de espacio direccionable en un sistema con solo gigabytes de RAM, porque la mayor parte de ese espacio nunca se toca.

Si alguna vez necesitas crear un prototipo de un dispositivo de bloques de más de unos pocos cientos de MiB, la memoria respaldada por swap es probablemente la herramienta adecuada. La API de VM para ello se encuentra en `/usr/src/sys/vm/swap_pager.c`. Leerlo no es tarea ligera, pero es instructivo.

### Una nota sobre las imágenes precargadas

FreeBSD dispone de un mecanismo llamado **módulos precargados**. Durante el arranque, el cargador puede incorporar no solo módulos del kernel sino también blobs de datos arbitrarios, que quedan a disposición del kernel a través de `preload_fetch_addr` y `preload_fetch_size`. `md(4)` aprovecha esto para exponer imágenes de sistema de archivos precargadas como dispositivos `/dev/md*`, que es una de las formas en que FreeBSD puede arrancar completamente desde una raíz en disco de memoria.

Las imágenes precargadas no son en sí mismas un mecanismo de persistencia. Son una manera de incluir datos junto a un módulo del kernel. Sin embargo, se utilizan con frecuencia en sistemas embebidos, donde el sistema de archivos raíz es demasiado valioso para residir en almacenamiento escribible.

### Una pequeña ampliación: persistencia entre recargas del módulo

No vamos a añadir persistencia real a nuestro driver, pero este es un buen momento para hablar de qué se necesitaría realmente para que un backing store sobreviviera a una descarga y recarga del módulo dentro del mismo arranque del kernel. La primera idea ingenua, a la que los principiantes suelen recurrir rápidamente, es colocar el puntero al backing store en una variable `static` de ámbito de archivo y simplemente no liberarla en el manejador de descarga. Veamos por qué eso no funciona y qué sí funciona.

Considera este esquema:

```c
static uint8_t *myblk_persistent_backing;  /* wishful thinking */
static size_t   myblk_persistent_size;
```

La intuición es que si asignamos `myblk_persistent_backing` en el primer attach y nos negamos a liberarlo en el detach, un `kldload` posterior encontrará el puntero aún establecido y reutilizará el buffer. El problema es que esta visión ignora cómo se carga y descarga realmente un KLD. Cuando `kldunload` elimina nuestro módulo, el kernel reclama los segmentos de texto, datos y `.bss` del módulo junto con el resto de su imagen. Nuestro puntero `static` no persiste en ningún lugar estable; desaparece junto con el módulo. Cuando `kldload` vuelve a cargar el módulo, el kernel asigna un `.bss` nuevo, lo pone a cero, y nuestro puntero comienza su vida como `NULL` de nuevo. El buffer asignado con `malloc` en el attach anterior sigue estando en algún lugar del heap del kernel, pero hemos perdido toda referencia a él. Lo hemos filtrado.

`SYSUNINIT` tampoco ayuda, porque en un contexto KLD se dispara en `kldunload`, no en algún evento posterior de "desmontaje definitivo". Registrar un `SYSUNINIT` para liberar el buffer lo liberaría en cada descarga, que es exactamente lo que no queríamos. No existe ningún hook a nivel de KLD que signifique "el archivo del módulo está siendo eliminado definitivamente de la memoria" distinto del simple `kldunload`.

Dos técnicas logran realmente la persistencia entre descargas, y ambas las utiliza `md(4)` en producción. La primera es un **backing store respaldado por archivo**. En lugar de asignar un buffer en el heap del kernel, el driver abre un archivo en un sistema de archivos existente usando la API de I/O de vnode (`VOP_READ`, `VOP_WRITE` y la referencia al vnode obtenida mediante `vn_open`) y atiende los BIOs leyendo y escribiendo en ese archivo. Al descargar, el driver cierra el archivo; en la siguiente carga, lo vuelve a abrir. La persistencia es real porque reside en un sistema de archivos cuyo estado es independiente de nuestro módulo. Esto es exactamente lo que hace `md -t vnode -f /path/to/image.img`, y puedes estudiarlo en `/usr/src/sys/dev/md/md.c`.

La segunda técnica es un **backing store respaldado por swap**. El driver asigna un objeto VM de tipo `OBJT_SWAP`, como mencionamos antes, y mapea páginas de él bajo demanda. El pager vive a un nivel más alto del kernel que nuestro módulo, por lo que el objeto puede sobrevivir a cualquier `kldunload` concreto mientras algo más mantenga una referencia a él. En la práctica, `md(4)` lo usa para discos de memoria respaldados por swap, y vincula el tiempo de vida del objeto a una lista global del kernel en lugar de a una instancia del módulo.

Para nuestro driver de enseñanza, no implementaremos ninguna de las dos técnicas. El propósito de mostrar esta discusión es asegurarse de que entiendas por qué el atajo aparente no funciona, para que no pases una tarde depurando un buffer que sigue desapareciendo después de `kldunload`. Si quieres experimentar con persistencia real entre descargas, lee `md.c` con atención, especialmente las ramas `MD_VNODE` y `MD_SWAP` en `mdstart_vnode` y `mdstart_swap`, y observa cómo los objetos de backing se asocian a la `struct md_s` por unidad en lugar de a variables globales del módulo. Esa elección estructural es lo que hace que esos backends funcionen a lo largo del ciclo de vida del módulo.

### Esquema de una función strategy respaldada por vnode

Para hacer concreta la discusión anterior, vamos a esbozar cómo es a nivel de código una función strategy respaldada por vnode. No vamos a incluir esto en nuestro driver de enseñanza. Lo mostramos para que puedas ver en qué consiste la solución real y reconocer la misma estructura en `md.c` cuando lo leas.

La idea es que el softc por unidad mantiene una referencia a un vnode, obtenida en el momento del attach a partir de una ruta proporcionada por el administrador. La función strategy traduce cada BIO en una llamada a `vn_rdwr` en el desplazamiento correcto y completa el BIO según el resultado.

El attach obtiene el vnode:

```c
static int
myblk_vnode_attach(struct myblk_softc *sc, const char *path)
{
    struct nameidata nd;
    int flags, error;

    flags = FREAD | FWRITE;
    NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, path);
    error = vn_open(&nd, &flags, 0, NULL);
    if (error != 0)
        return (error);
    NDFREE_PNBUF(&nd);
    VOP_UNLOCK(nd.ni_vp);
    sc->vp = nd.ni_vp;
    sc->vp_cred = curthread->td_ucred;
    crhold(sc->vp_cred);
    return (0);
}
```

`vn_open` busca la ruta y devuelve un vnode bloqueado y referenciado. A continuación liberamos el lock, porque queremos mantener una referencia sin bloquear otras operaciones, y colgamos el puntero al vnode en nuestro softc. También conservamos una referencia a las credenciales que usaremos para el I/O posterior.

La función strategy atiende los BIOs contra el vnode:

```c
static void
myblk_vnode_strategy(struct bio *bp)
{
    struct myblk_softc *sc = bp->bio_disk->d_drv1;
    int error;

    switch (bp->bio_cmd) {
    case BIO_READ:
        error = vn_rdwr(UIO_READ, sc->vp, bp->bio_data,
            bp->bio_length, bp->bio_offset, UIO_SYSSPACE,
            IO_DIRECT, sc->vp_cred, NOCRED, NULL, curthread);
        break;
    case BIO_WRITE:
        error = vn_rdwr(UIO_WRITE, sc->vp, bp->bio_data,
            bp->bio_length, bp->bio_offset, UIO_SYSSPACE,
            IO_DIRECT | IO_SYNC, sc->vp_cred, NOCRED, NULL,
            curthread);
        break;
    case BIO_FLUSH:
        error = VOP_FSYNC(sc->vp, MNT_WAIT, curthread);
        break;
    case BIO_DELETE:
        /* Vnode-backed devices usually do not support punching
         * holes through BIO_DELETE without additional plumbing.
         */
        error = EOPNOTSUPP;
        break;
    default:
        error = EOPNOTSUPP;
        break;
    }

    if (error != 0) {
        bp->bio_error = error;
        bp->bio_flags |= BIO_ERROR;
        bp->bio_resid = bp->bio_bcount;
    } else {
        bp->bio_resid = 0;
    }
    biodone(bp);
}
```

Observa cómo la estructura del switch es idéntica a nuestra función strategy respaldada por RAM. La única diferencia es lo que hacen las ramas del case: en lugar de un `memcpy` sobre un buffer, llamamos a `vn_rdwr` contra un vnode. El framework que hay sobre nosotros, GEOM y la caché de buffers, no sabe ni le importa qué backend hemos elegido.

El detach libera el vnode:

```c
static void
myblk_vnode_detach(struct myblk_softc *sc)
{

    if (sc->vp != NULL) {
        (void)vn_close(sc->vp, FREAD | FWRITE, sc->vp_cred,
            curthread);
        sc->vp = NULL;
    }
    if (sc->vp_cred != NULL) {
        crfree(sc->vp_cred);
        sc->vp_cred = NULL;
    }
}
```

`vn_close` libera la referencia al vnode y, si era la última referencia, permite que el vnode sea reciclado. Las credenciales se gestionan con conteo de referencias del mismo modo.

¿Por qué esto nos proporciona persistencia entre descargas? Porque el estado que nos importa, es decir, el contenido del backing store, vive en un archivo de un sistema de archivos real cuyo tiempo de vida es completamente independiente de nuestro módulo. Cuando llamamos a `kldunload`, la referencia al vnode se libera y el archivo se cierra; el sistema de archivos preserva su contenido en disco. Cuando volvemos a llamar a `kldload` y hacemos el attach, abrimos el archivo de nuevo y continuamos donde lo dejamos.

Las sutilezas restantes son considerables. Las rutas de error necesitan liberar el vnode si `vn_open` tuvo éxito pero los pasos de registro posteriores fallaron. Las llamadas a `vn_rdwr` pueden dormir, lo que significa que la función strategy no debe invocarse desde un contexto que no permita dormir; en la práctica, por eso `md(4)` utiliza un thread de trabajo dedicado para las unidades respaldadas por vnode. Leer un archivo puede entrar en condición de carrera con el administrador que lo modifica, por lo que los drivers de producción suelen adoptar medidas para detectar cambios externos concurrentes. `VOP_FSYNC` no es gratuito, por lo que es habitual tener un camino rápido que agrupe escrituras antes de vaciarlas. Y el tiempo de vida del propio vnode está limitado por el conteo de referencias del propio VFS, que interactúa con el desmontaje del sistema de archivos que lo contiene.

No añadiremos esto a nuestro driver de enseñanza, pero cuando leas `mdstart_vnode` en `/usr/src/sys/dev/md/md.c`, reconocerás cada uno de estos problemas tratados de forma cuidadosa y explícita.

### Cerrando la sección 7

La persistencia es un concepto por capas. La durabilidad, la consistencia y la seguridad ante fallos forman parte de lo que debe proporcionar un dispositivo de almacenamiento real, y diferentes backing stores ofrecen distintos subconjuntos de esas garantías. Para un driver de enseñanza, un buffer en memoria asignado con `malloc` es una elección razonable, y podemos añadir semántica de "sobrevive a la recarga del módulo" sin demasiado código desvinculando el buffer del softc de la instancia.

Para producción, las técnicas se vuelven más elaboradas: asignación página a página, objetos VM respaldados por swap, archivos respaldados por vnode, threads de trabajo dedicados, coordinación de `BIO_FLUSH` y manejo cuidadoso de cada ruta de error. `md(4)` es el ejemplo canónico en el árbol de FreeBSD, y se recomienda encarecidamente leerlo.

En la siguiente sección nos centraremos en detalle en la ruta de desmontaje. Veremos cómo GEOM coordina el desmontaje, el detach y la limpieza; cómo los contadores de acceso controlan la ruta de descarga del módulo; y cómo debe comportarse nuestro driver cuando algo falla a mitad del proceso. Los errores en el desmontaje de almacenamiento son algunos de los tipos más desagradables de error del kernel, y prestar atención cuidadosa aquí tiene recompensa para el resto de tu carrera escribiendo drivers.

## Sección 8: Desmontaje seguro y limpieza

Los drivers de almacenamiento gestionan el fin de su vida con más cuidado que los drivers de caracteres porque el riesgo es mayor. Cuando un driver de caracteres se descarga limpiamente, lo peor que puede ocurrir es que una sesión abierta se cierre abruptamente, posiblemente con algunos bytes en tránsito que se pierden. Cuando un driver de almacenamiento se descarga mientras hay un sistema de archivos montado sobre él, lo peor que puede ocurrir es que el kernel entre en pánico en el siguiente BIO, y el usuario se quede con una imagen del sistema de archivos que puede o no estar en estado consistente en el momento en que el driver desapareció.

La buena noticia es que las defensas del kernel hacen que el caso catastrófico sea casi imposible si usas `g_disk` correctamente. La negativa de `kldunload` a continuar cuando el contador de acceso de GEOM es distinto de cero, que vimos en la sección 6, es la principal red de seguridad. Pero no es la única preocupación. Esta sección recorre en detalle la ruta de desmontaje para que sepas qué esperar, qué implementar y qué probar.

### La secuencia de desmontaje esperada

La secuencia nominal de eventos cuando un usuario quiere eliminar un driver de almacenamiento es la siguiente.

1. El usuario desmonta todos los sistemas de archivos montados sobre el dispositivo.
2. El usuario cierra cualquier programa que tenga `/dev/myblk0` abierto para acceso directo.
3. El usuario llama a `kldunload`.
4. La función de descarga del módulo llama a `disk_destroy`.
5. `disk_destroy` encola el provider para su desmantelamiento, que se ejecuta en el thread de eventos de GEOM.
6. El proceso de desmantelamiento espera a que cualquier BIO en tránsito se complete.
7. El provider se elimina de la topología de GEOM y el nodo `/dev` se destruye.
8. `disk_destroy` devuelve el control a nuestra función de descarga.
9. Nuestra función de descarga libera el softc y el backing store.
10. El kernel descarga el módulo.

Cada paso tiene sus propios modos de fallo. Vamos a recorrerlos.

### Paso 1: Desmontaje

El usuario ejecuta `umount /mnt/myblk`. VFS le pide a UFS que vacíe el sistema de archivos, lo que hace que la caché de buffers emita cualquier escritura pendiente a GEOM, que las enruta a nuestro driver. Nuestra función strategy atiende las escrituras y llama a `biodone`. La caché de buffers informa de éxito; UFS descarta su estado en memoria; VFS libera el punto de montaje. El consumer que UFS había asociado a nuestro provider se desconecta. El contador de acceso disminuye.

Nuestro driver no hace nada especial durante esta fase. Seguimos atendiendo BIOs a medida que llegan hasta que UFS deja de emitirlos.

### Paso 2: Cierre del acceso directo

El usuario se asegura de que ningún programa tenga `/dev/myblk0` abierto. Si hay un `dd` en ejecución, mátalo. Si un shell tiene el dispositivo abierto a través de `exec`, ciérralo. Hasta que todos los descriptores abiertos se liberen, el contador de acceso seguirá siendo distinto de cero en al menos uno de los contadores `r`, `w` o `e`.

De nuevo, nuestro driver no hace nada especial. Las llamadas a `close(2)` sobre `/dev/myblk0` se propagan a través de `devfs`, a través de la integración de dispositivos de caracteres de GEOM, y liberan su acceso. No se emiten BIOs para el cierre.

### Paso 3: kldunload

El usuario ejecuta `kldunload myblk`. El subsistema de módulos del kernel llama a nuestra función de descarga con `MOD_UNLOAD`. Nuestra función de descarga llama a `myblk_detach_unit`, que llama a `disk_destroy`.

En este punto, nuestro driver está a punto de dejar de existir. No debemos mantener ningún lock que pueda bloquear, no debemos estar bloqueados en nuestros propios threads de trabajo (no tenemos ninguno en este diseño), y no debemos emitir nuevos BIOs. Nada de lo que hagamos ahora debería generar nuevo trabajo para el kernel.

### Paso 4: disk_destroy

`disk_destroy` es el punto de no retorno. Leer el código fuente en `/usr/src/sys/geom/geom_disk.c` revela que hace tres cosas:

1. Establece un indicador en el disco para señalar que la destrucción está en curso.
2. Encola un evento GEOM que desmantelará el provider.
3. Espera a que el evento se complete.

Mientras esperamos, el thread de eventos de GEOM recoge el evento y recorre nuestro geom. Si los contadores de acceso son cero, el evento continúa. Si no son cero, el evento entra en pánico con un mensaje sobre intentar destruir un disco que todavía tiene usuarios.

Aquí es donde se hace evidente la importancia del Paso 1 y el Paso 2. Si los saltas e intentas descargar mientras el sistema de archivos está montado, el pánico ocurre aquí. Afortunadamente, `g_disk` se niega a llegar al pánico porque el subsistema de módulos ya rechazó la descarga antes, pero si evitaras el subsistema de módulos y llamaras a `disk_destroy` directamente desde algún otro contexto, esta es la comprobación que protege al kernel.

### Pasos 5 a 7: Desmantelamiento

El proceso de marchitamiento de GEOM es el mecanismo por el que los providers se eliminan de la topología. Funciona marcando el provider como marchitado, cancelando los BIOs que estuvieran en cola pero que aún no hubieran sido entregados, esperando a que terminen los BIOs en tránsito, eliminando el provider de la lista de providers del geom y, a continuación, eliminando el geom de la clase. El nodo en `/dev` se elimina como parte de este proceso.

Durante el marchitamiento, la función strategy puede seguir siendo invocada para BIOs que estaban en tránsito antes de que el marchitamiento comenzara. Nuestra función strategy los gestionará con normalidad, porque nuestro driver no sabe ni le importa que el marchitamiento esté en curso. El framework se encarga de garantizar que no se emitan nuevos BIOs a partir del punto de no retorno.

Si nuestro driver tuviera worker threads, una cola u otro estado interno, necesitaríamos coordinar esa situación con el marchitamiento con cuidado. `md(4)` es un buen ejemplo de un driver que hace esto: su worker thread vigila la presencia de un indicador de cierre y vacía su cola antes de salir. Como nuestro driver es totalmente síncrono y de un solo thread, no tenemos esta complicación.

### Pasos 8 a 9: Liberar recursos

Una vez que `disk_destroy` retorna, el disco ha desaparecido, el proveedor ha desaparecido y no llegarán más BIOs. Es seguro liberar el almacén de respaldo y destruir el mutex.

```c
static void
myblk_detach_unit(struct myblk_softc *sc)
{

    if (sc->disk != NULL) {
        disk_destroy(sc->disk);
        sc->disk = NULL;
    }
    if (sc->backing != NULL) {
        free(sc->backing, M_MYBLK);
        sc->backing = NULL;
        sc->backing_size = 0;
    }
}
```

La función de descarga destruye entonces el mutex y libera el softc.

```c
case MOD_UNLOAD:
    sc = myblk_unit0;
    if (sc == NULL)
        return (0);
    myblk_detach_unit(sc);
    mtx_destroy(&sc->lock);
    free(sc, M_MYBLK);
    myblk_unit0 = NULL;
    printf("myblk: unloaded\n");
    return (0);
```

### Paso 10: Descarga del módulo

El subsistema de módulos descarga el archivo `.ko`. En este punto, el driver ha desaparecido. Cualquier intento de referenciar el módulo por nombre fallará hasta que el usuario lo cargue de nuevo.

### Qué puede salir mal

El camino feliz es sencillo. Vamos a enumerar los caminos problemáticos y cómo reconocerlos.

**`kldunload` devuelve `Device busy`**. El sistema de archivos sigue montado, o algún programa aún tiene el dispositivo raw abierto. Desmonta y cierra, luego vuelve a intentarlo. Este es el fallo más común y es benigno.

**`disk_destroy` no retorna nunca**. Algo retiene un BIO que nunca se completará, y el proceso de desmantelamiento está esperándolo. En la práctica, esto ocurre cuando tu función de estrategia no llama a `biodone` en algún camino. Consulta `procstat -kk` del thread `g_event`; si está bloqueado en `g_waitfor_event`, tienes un BIO perdido. La solución está en tu función de estrategia: asegúrate de que cada camino llame a `biodone` exactamente una vez.

**El kernel entra en pánico con "g_disk: destroy with open count"**. Tu driver llamó a `disk_destroy` mientras el proveedor aún tenía usuarios. Esto no debería ocurrir si solo llamas a `disk_destroy` desde el camino de descarga del módulo, porque el subsistema de módulos se niega a descargar módulos ocupados. Pero si llamas a `disk_destroy` en respuesta a algún otro evento, debes comprobar el contador de accesos tú mismo o asumir el pánico.

**El kernel entra en pánico con "Freeing free memory"**. Tu driver intentó liberar el softc o el almacén de respaldo dos veces. Comprueba si hay condiciones de carrera en tu camino de detach, o salidas tempranas que liberan y luego caen hasta liberar de nuevo.

**El kernel entra en pánico con "Page fault in kernel mode"**. Algo está desreferenciando un puntero liberado, la mayoría de las veces el almacén de respaldo después de haberse liberado mientras un BIO está aún en vuelo. La solución es asegurarse de que `disk_destroy` se completa antes de liberar cualquier cosa que toque la función de estrategia.

### El callback d_gone

Hay un elemento más de la historia del desmantelamiento que merece comentarse. El callback `d_gone` se invoca cuando algo ajeno a nuestro driver decide que el disco debe desaparecer. El ejemplo canónico es la extracción en caliente: el usuario desenchufa una unidad USB, la pila USB notifica al driver de almacenamiento que el dispositivo ha desaparecido, y el driver quiere indicar a GEOM que desmantele el disco de la forma más limpia posible aunque el I/O empiece a fallar.

Nuestro driver es un dispositivo pseudo; no tiene un evento de desaparición física. Pero registrar un callback `d_gone` no tiene coste y hace el driver algo más robusto frente a extensiones futuras.

```c
static void
myblk_disk_gone(struct disk *dp)
{

    printf("myblk: disk_gone(%s%u)\n", dp->d_name, dp->d_unit);
}
```

Regístralo con `sc->disk->d_gone = myblk_disk_gone;` antes de `disk_create`. La función la invoca `g_disk` cuando se llama a `disk_gone`. Puedes activarla manualmente durante el desarrollo llamando a `disk_gone(sc->disk)` desde un camino de prueba; en un pseudo driver normalmente no la llamarás tú mismo.

Observa la diferencia entre `disk_gone` y `disk_destroy`. `disk_gone` dice «este disco ha desaparecido físicamente; deja de aceptar I/O y marca el proveedor como retorno de error». `disk_destroy` dice «elimina este disco de la topología y libera sus recursos». En un camino de extracción en caliente, `disk_gone` se llama normalmente primero (por el driver de bus, cuando detecta que el dispositivo ha desaparecido) y `disk_destroy` se llama después (por la descarga del módulo, o por la función detach del driver de bus). Entre las dos llamadas, el disco sigue existiendo en la topología pero todo el I/O falla. Nuestro driver no implementa este desmantelamiento en dos fases; un driver de almacenamiento masivo USB, por ejemplo, sí debe hacerlo.

### Probar el desmantelamiento

Los errores de desmantelamiento se descubren con frecuencia no mediante pruebas cuidadosas, sino por accidente, meses después, cuando algún usuario encuentra una secuencia inusual que los desencadena. Es mucho más barato probar el desmantelamiento de forma deliberada.

Aquí están las pruebas que recomiendo ejecutar en cualquier driver de almacenamiento nuevo.

**Descarga básica**. Carga, formatea, monta, desmonta, descarga. Verifica que `dmesg` muestra nuestros mensajes de carga y descarga y nada más. Repite diez veces para detectar fugas lentas.

**Descarga sin desmontar**. Carga, formatea, monta. Intenta descargar. Verifica que la descarga es rechazada. Desmonta y luego descarga. Verifica que no queda ningún estado residual.

**Descarga bajo carga**. Carga, formatea, monta e inicia un `dd if=/dev/urandom of=/mnt/myblk/stress bs=1m count=64`. Mientras `dd` se ejecuta, intenta descargar. Verifica que la descarga es rechazada. Espera a que `dd` termine. Desmonta. Descarga. Verifica que queda limpio.

**Descarga con el dispositivo raw abierto**. Carga. En otro terminal, ejecuta `cat > /dev/myblk0` para mantener el dispositivo abierto. Intenta descargar. Verifica que la descarga es rechazada. Termina el proceso cat. Descarga. Verifica que queda limpio.

**Estrés de recarga**. Carga, descarga, carga, descarga en un bucle cerrado durante un minuto. Si `vmstat -m` o `zpool list` empiezan a mostrar fugas, investiga.

**Pánico por corrupción**. Esta es más difícil: corrompe deliberadamente el estado del módulo mediante un hook del depurador del kernel y verifica que el driver no devuelva datos incorrectos en silencio. En la práctica, pocos principiantes hacen esto, y no es necesario para un driver de enseñanza.

Si todas estas pruebas pasan, tienes un desmantelamiento razonablemente robusto. Continúa probando cada cambio que afecte al camino de descarga.

### El principio de idempotencia

Un buen camino de desmantelamiento es idempotente: llamarlo dos veces no es peor que llamarlo una sola vez. Esto importa porque los caminos de error durante el attach pueden invocar el desmantelamiento antes de que todo haya sido configurado.

Escribe tu desmantelamiento de forma que compruebe si cada recurso fue realmente asignado antes de intentar liberarlo.

```c
static void
myblk_detach_unit(struct myblk_softc *sc)
{

    if (sc == NULL)
        return;

    if (sc->disk != NULL) {
        disk_destroy(sc->disk);
        sc->disk = NULL;
    }
    if (sc->backing != NULL) {
        free(sc->backing, M_MYBLK);
        sc->backing = NULL;
        sc->backing_size = 0;
    }
}
```

Poner los punteros a `NULL` después de liberarlos es una pequeña disciplina que da sus frutos. Hace que los errores de doble liberación sean evidentes en tiempo de ejecución (se convierten en operaciones sin efecto en lugar de corrupciones) y hace que la función de desmantelamiento sea idempotente.

### Orden e inversión del orden

Una directriz general de desmantelamiento: libera los recursos en el orden inverso a su asignación. Si attach sigue el orden `A -> B -> C`, detach debe seguir `C -> B -> A`.

En nuestro driver, attach sigue `malloc backing -> disk_alloc -> disk_create`. Por lo tanto, detach sigue `disk_destroy -> free backing`. Nos saltamos la liberación del disco porque `disk_destroy` lo libera por nosotros.

Este patrón es universal. Toda función de desmantelamiento bien escrita invierte el orden de asignación. Cuando veas un detach que sigue el mismo orden que el attach, sospecha de un error.

### El evento MOD_QUIESCE

Hay un tercer evento de módulo que no hemos mencionado: `MOD_QUIESCE`. Se entrega antes de `MOD_UNLOAD` y da al módulo la oportunidad de rechazar la descarga si el driver está en un estado en el que descargar es inseguro.

Para la mayoría de los drivers, la comprobación del contador de accesos de GEOM es suficiente y no es necesario implementar `MOD_QUIESCE`. Pero si tu driver tiene un estado interno que hace insegura la descarga independientemente de GEOM (por ejemplo, un cache que debe vaciarse), `MOD_QUIESCE` es donde rechazas la descarga devolviendo un error.

Nuestro driver no implementa `MOD_QUIESCE`. El comportamiento predeterminado es aceptarlo en silencio, lo que es lo correcto para nosotros.

### Coordinación con threads de trabajo futuros

Si alguna vez añades un thread de trabajo al driver, el contrato de desmantelamiento cambia. Debes:

1. Señalizar al worker para que se detenga, normalmente estableciendo un indicador en el softc.
2. Despertar al worker si está dormido, normalmente con `wakeup` o `cv_signal`.
3. Esperar a que el worker termine, normalmente con un indicador de terminación visible para `kthread_exit`.
4. Solo entonces llamar a `disk_destroy`.
5. Liberar el softc y el almacén de respaldo.

Saltarse cualquiera de estos pasos es una receta para un pánico. El modo de fallo habitual es que el thread de trabajo duerme dentro de una función que accede al estado del softc después de que el softc ha sido liberado. `md(4)` maneja esto con cuidado, y vale la pena leer su código de cierre del worker si planeas añadir uno a tu propio driver.

### Limpieza ante errores

Una última preocupación: ¿qué ocurre si attach falla a mitad del camino? Supongamos que `disk_alloc` tiene éxito, pero `disk_create` falla. O supongamos que añadimos código que valida el tamaño del sector y rechaza configuraciones inválidas antes de llamar a `disk_create`.

El patrón para gestionar esto es el «camino único de limpieza». Escribe la función attach de modo que cualquier fallo salte a una etiqueta de limpieza que deshaga todo lo asignado hasta ese momento, en orden inverso.

```c
static int
myblk_attach_unit(struct myblk_softc *sc)
{
    int error;

    sc->backing_size = MYBLK_MEDIASIZE;
    sc->backing = malloc(sc->backing_size, M_MYBLK, M_WAITOK | M_ZERO);

    sc->disk = disk_alloc();
    sc->disk->d_name       = MYBLK_NAME;
    sc->disk->d_unit       = sc->unit;
    sc->disk->d_strategy   = myblk_strategy;
    sc->disk->d_ioctl      = myblk_ioctl;
    sc->disk->d_getattr    = myblk_getattr;
    sc->disk->d_gone       = myblk_disk_gone;
    sc->disk->d_sectorsize = MYBLK_SECTOR;
    sc->disk->d_mediasize  = MYBLK_MEDIASIZE;
    sc->disk->d_maxsize    = MAXPHYS;
    sc->disk->d_drv1       = sc;

    error = 0;  /* disk_create is void; no error path from it */
    disk_create(sc->disk, DISK_VERSION);
    return (error);

    /*
     * Future expansion: if we add a step that can fail between
     * disk_alloc and disk_create, use a cleanup label here.
     */
}
```

En nuestro driver, `disk_alloc` no falla en la práctica (usa `M_WAITOK`), y `disk_create` es una función `void` que encola el trabajo real de forma asíncrona. Por lo tanto, el camino de attach no puede realmente fallar. Pero el patrón de preparar una etiqueta única de limpieza vale la pena tenerlo en mente para drivers que se vuelvan más complejos.

### Cerrando la sección 8

El desmontaje y la limpieza seguros de un driver de almacenamiento se reducen a un pequeño conjunto de disciplinas: gestiona cada BIO hasta `biodone`, nunca mantengas locks durante los callbacks de finalización, llama a `disk_destroy` solo cuando el proveedor no tiene usuarios, libera los recursos en el orden inverso a su asignación, y prueba el desmantelamiento bajo carga. El framework `g_disk` gestiona la mayor parte de las partes difíciles; tu trabajo es evitar romper sus invariantes.

En la siguiente sección, nos alejaremos de los detalles del desmantelamiento y hablaremos de cómo dejar que un driver de almacenamiento crezca. Discutiremos la refactorización, el versionado, cómo admitir múltiples unidades de forma limpia y qué hacer cuando el driver se convierte en algo más que un único archivo fuente. Estos son los hábitos que convierten un driver de enseñanza en algo que puedes seguir evolucionando durante mucho tiempo.

## Sección 9: Refactorización y versionado

Nuestro driver cabe en un solo archivo y resuelve un único problema: expone un único disco pseudo de tamaño fijo, respaldado por RAM. Es un punto de partida pedagógico útil, pero no es donde viven la mayoría de los drivers reales. Un driver de almacenamiento real evoluciona. Incorpora soporte para ioctl. Incorpora soporte para múltiples unidades. Incorpora parámetros ajustables. Se divide en múltiples archivos fuente. Su representación en disco, si la tiene, pasa por cambios de formato. Acumula un historial de decisiones de compatibilidad.

Esta sección trata de los hábitos que permiten que un driver crezca con elegancia. No añadiremos grandes funcionalidades nuevas aquí; los laboratorios y desafíos de acompañamiento se encargarán de eso. Lo que haremos es repasar las preguntas de refactorización y versionado que surgen a medida que madura cualquier driver de almacenamiento, y señalaremos las respuestas idiomáticas de FreeBSD para cada una.

### Soporte para múltiples unidades

Por ahora, nuestro driver admite exactamente una instancia, codificada de forma fija como `myblk0`. Si quisieras dos o tres discos pseudo, el código actual necesitaría softcs duplicados y registros de disco duplicados. Los drivers reales resuelven esto con una estructura de datos capaz de albergar cualquier número de unidades.

El patrón idiomático de FreeBSD es una lista global protegida por un lock. El softc se asigna por unidad y se enlaza en la lista. Un parámetro ajustable en tiempo de carga o una llamada a través de ioctl decide cuándo crear una nueva unidad. El número de unidad se asigna desde un asignador `unrhdr` (rango de números únicos).

Un esbozo:

```c
static struct mtx          myblk_list_lock;
static LIST_HEAD(, myblk_softc) myblk_list =
    LIST_HEAD_INITIALIZER(myblk_list);
static struct unrhdr      *myblk_unit_pool;

static int
myblk_create_unit(size_t mediasize, struct myblk_softc **scp)
{
    struct myblk_softc *sc;
    int unit;

    unit = alloc_unr(myblk_unit_pool);
    if (unit < 0)
        return (ENOMEM);

    sc = malloc(sizeof(*sc), M_MYBLK, M_WAITOK | M_ZERO);
    mtx_init(&sc->lock, "myblk unit", NULL, MTX_DEF);
    sc->unit = unit;
    sc->backing_size = mediasize;
    sc->backing = malloc(mediasize, M_MYBLK, M_WAITOK | M_ZERO);

    sc->disk = disk_alloc();
    sc->disk->d_name       = MYBLK_NAME;
    sc->disk->d_unit       = sc->unit;
    sc->disk->d_strategy   = myblk_strategy;
    sc->disk->d_sectorsize = MYBLK_SECTOR;
    sc->disk->d_mediasize  = mediasize;
    sc->disk->d_maxsize    = MAXPHYS;
    sc->disk->d_drv1       = sc;
    disk_create(sc->disk, DISK_VERSION);

    mtx_lock(&myblk_list_lock);
    LIST_INSERT_HEAD(&myblk_list, sc, link);
    mtx_unlock(&myblk_list_lock);

    *scp = sc;
    return (0);
}

static void
myblk_destroy_unit(struct myblk_softc *sc)
{

    mtx_lock(&myblk_list_lock);
    LIST_REMOVE(sc, link);
    mtx_unlock(&myblk_list_lock);

    disk_destroy(sc->disk);
    free(sc->backing, M_MYBLK);
    mtx_destroy(&sc->lock);
    free_unr(myblk_unit_pool, sc->unit);
    free(sc, M_MYBLK);
}
```

El cargador inicializa el pool de unidades una sola vez, y luego las unidades individuales pueden crearse y destruirse de forma independiente. Esto es muy similar al patrón que usa `md(4)`.

No refactorizaremos todavía nuestro driver de capítulo a múltiples unidades, porque el código adicional distrae de los otros objetivos pedagógicos. Pero deberías saber que hacia aquí iría el driver. El soporte para múltiples unidades es una de las primeras extensiones que necesitan los drivers reales.

### Superficie de ioctl para configuración en tiempo de ejecución

Con múltiples unidades surge la necesidad de configurarlas en tiempo de ejecución. No querrás compilar un nuevo módulo cada vez que desees una segunda unidad o un tamaño diferente. La respuesta es un ioctl en un dispositivo de control.

`md(4)` sigue este patrón. Hay un único dispositivo `/dev/mdctl`, y `mdconfig(8)` se comunica con él mediante ioctls. `MDIOCATTACH` crea una nueva unidad con un tamaño y tipo de respaldo especificados. `MDIOCDETACH` destruye una unidad. `MDIOCQUERY` lee el estado de una unidad. `MDIOCRESIZE` cambia el tamaño.

Para un driver de cierta complejidad, aquí es donde vale la pena invertir esfuerzo. La configuración en tiempo de compilación mediante macros está bien para un ejemplo de juguete. La configuración en tiempo de ejecución mediante ioctls es lo que los administradores reales necesitan.

Si quisieras añadir esto a nuestro driver, tendrías que:

1. Crear un `cdev` para el dispositivo de control usando `make_dev`.
2. Implementar `d_ioctl` en el cdev, con un switch sobre un pequeño conjunto de números de ioctl que tú mismo defines.
3. Escribir una herramienta en espacio de usuario que emita los ioctls.

Se trata de una adición de cierta envergadura, razón por la que la mencionamos aquí sin implementarla. El capítulo 28 y los capítulos posteriores retomarán este patrón.

### División del archivo fuente

En algún momento, un driver supera el tamaño de un único archivo. La descomposición habitual para un driver de almacenamiento en FreeBSD es aproximadamente la siguiente:

- `driver_name.c`: el punto de entrada público del módulo, el despacho de ioctls y la conexión de attach/detach.
- `driver_name_bio.c`: la función strategy y la ruta de los BIOs.
- `driver_name_backing.c`: la implementación del backing store.
- `driver_name_util.c`: pequeñas funciones auxiliares, validación e impresión de información de depuración.
- `driver_name.h`: la cabecera compartida que declara el softc, los enums y los prototipos de funciones.

El Makefile se actualiza para listarlos todos en `SRCS`, y el sistema de build se encarga del resto. Esta es la estructura de `md(4)`, de `ata(4)` y de la mayoría de los drivers más elaborados del árbol.

Mantendremos nuestro driver en un único archivo para el capítulo. Pero cuando los desafíos o tus propias extensiones lo lleven más allá de, digamos, 500 líneas, una descomposición como la anterior es la elección correcta. Los lectores que quieran ver un ejemplo concreto deberían mirar `/usr/src/sys/dev/ata/`, que divide un driver complejo en varios archivos a lo largo de líneas bien definidas.

### Versionado

Un driver de almacenamiento tiene varios tipos de versionado de los que ocuparse.

**Versión del módulo**, declarada con `MODULE_VERSION(myblk, 1)`. Es un entero que solo aumenta y que otros módulos o herramientas de espacio de usuario pueden consultar. Auméntalo cada vez que cambies el comportamiento externo del módulo de una forma que no pueda detectarse desde el código.

**Versión del ABI de disco**, codificada en `DISK_VERSION`. Es la versión de la interfaz `g_disk` contra la que se compiló tu driver. Si el `g_disk` del kernel cambia de forma incompatible, incrementa la versión, y un driver compilado contra la versión anterior no podrá registrarse. No la estableces directamente; pasas la macro `DISK_VERSION` a través de `disk_create`, y esta recoge la versión que el compilador encontró en `geom_disk.h`. Debes recompilar los drivers contra el kernel al que apuntas.

**Versión del formato en disco**, para drivers que tengan metadatos en disco. Si tu driver graba un número mágico y una versión en un sector reservado, debes gestionar las actualizaciones. Nuestro driver no tiene formato en disco, así que esto no aplica por ahora, pero sería necesario si añadiéramos una cabecera apropiada para el backing store.

**Versión de los números de ioctl**. Una vez que defines ioctls, sus números forman parte del ABI de espacio de usuario. Cambiarlos rompe las herramientas de espacio de usuario más antiguas. Usa `_IO`, `_IOR`, `_IOW`, `_IOWR` con letras mágicas estables, y no reutilices los números.

Para nuestro driver del capítulo, el único versionado que nos importa ahora mismo es la versión del módulo. Pero tener en mente estos cuatro tipos de versionado evita problemas más adelante.

### Herramientas de depuración y observabilidad

A medida que el driver crece, querrás observar su estado de forma más rica que con `dmesg` solo. Merece la pena presentar tres herramientas ahora.

**Nodos `sysctl`**. El framework `sysctl(3)` de FreeBSD permite que un módulo publique variables de solo lectura o de lectura-escritura que las herramientas de espacio de usuario pueden consultar. Creas un árbol bajo un nombre elegido y le asocias valores. El patrón es estándar; con unas diez líneas de código puedes exponer el número de BIOs procesados, el número de bytes leídos y escritos, y el tamaño actual del medio.

```c
SYSCTL_NODE(_dev, OID_AUTO, myblk, CTLFLAG_RD, 0,
    "myblk driver parameters");
static u_long myblk_reads = 0;
SYSCTL_ULONG(_dev_myblk, OID_AUTO, reads, CTLFLAG_RD, &myblk_reads,
    0, "Number of BIO_READ requests serviced");
```

**Devstat**. Ya lo usamos a través de `g_disk`. Proporciona a `iostat` y a `gstat` sus datos. No es necesario hacer nada más.

**Sondas DTrace**. El framework `SDT` permite que un módulo defina sondas DTrace estáticas que no tienen ningún coste cuando no se están monitorizando. Son especialmente útiles en la ruta de los BIOs porque te permiten ver el flujo de solicitudes en tiempo real sin recompilar.

```c
#include <sys/sdt.h>
SDT_PROVIDER_DECLARE(myblk);
SDT_PROBE_DEFINE3(myblk, , strategy, request,
    "int" /* cmd */, "off_t" /* offset */, "size_t" /* length */);

/* inside myblk_strategy: */
SDT_PROBE3(myblk, , strategy, request,
    bp->bio_cmd, bp->bio_offset, bp->bio_bcount);
```

Puedes monitorizarlo después con `dtrace -n 'myblk::strategy:request {...}'`.

Para el driver del capítulo no añadiremos todo esto, pero estos son los patrones a los que deberías recurrir a medida que el driver crece.

### Estabilidad de nombres

Un hábito fácil de pasar por alto: no renombres las cosas a la ligera. El nombre `myblk` aparece en el nodo de dispositivo, en el registro de versión del módulo, en el nombre de devstat, posiblemente en nodos sysctl, en sondas DTrace y en la documentación. Renombrarlo implica cambios en cascada en todos ellos. Para un driver de proyecto, elige un nombre con el que puedas vivir para siempre. `md`, `ada`, `nvd`, `zvol` y otros drivers de almacenamiento han mantenido sus nombres durante años porque renombrar es un cambio que afecta al ABI de las herramientas de espacio de usuario.

### Mantener el driver de enseñanza simple

Todo en esta sección es una dirección hacia la que podría crecer tu driver. Nada de esto es necesario en la primera versión. Saber dónde se producirá el crecimiento te permite tomar decisiones tempranas que no tendrás que deshacer más adelante.

El archivo compañero `myfirst_blk.c` permanece en un único archivo al final de este capítulo. Su README documenta los puntos de extensión, y los ejercicios de desafío añaden algunos de ellos. Más allá de eso, eres libre de seguir extendiéndolo, y cada extensión que hagas utilizará estos patrones de alguna forma.

### Resumen de patrones de diseño

En este punto hemos acumulado suficientes patrones como para que listarlos sea útil. Cuando empieces tu próximo driver de almacenamiento, estos son los patrones a los que recurrir.

**El patrón softc.** Una estructura por instancia para almacenar todo lo que necesita el driver. Se apunta desde `d_drv1`. Se recupera dentro de los callbacks mediante `bp->bio_disk->d_drv1`.

**El par attach/detach.** La función attach asigna, inicializa y registra. La función detach invierte la secuencia. Ambas deben ser idempotentes.

**El patrón switch-and-biodone.** Toda función strategy hace un switch sobre `bio_cmd`, atiende cada comando, establece `bio_resid` y llama a `biodone` exactamente una vez.

**La comprobación defensiva de límites.** Valida el desplazamiento y la longitud frente al tamaño del medio, usando la resta para evitar desbordamientos.

**El patrón de lock simple.** Un único mutex alrededor del camino caliente suele ser suficiente para un driver de enseñanza. Divídelo solo cuando el rendimiento lo exija.

**El desmontaje en orden inverso.** Libera los recursos en el orden opuesto al de la asignación.

**El patrón null-after-free.** Después de liberar un puntero, ponlo a `NULL`. Esto detecta las dobles liberaciones.

**La etiqueta única de limpieza.** En las funciones attach que pueden fallar, todos los fallos saltan a una única etiqueta de limpieza que deshace el estado hasta ese punto.

**El ABI versionado.** Pasa `DISK_VERSION` a `disk_create`. Declara `MODULE_VERSION`. Usa `MODULE_DEPEND` en cada módulo del kernel del que dependas.

**El patrón de trabajo diferido.** El trabajo que debe bloquearse (como las operaciones de vnode I/O) pertenece a un thread trabajador, no a `d_strategy`.

**El hábito de observabilidad desde el principio.** Añade `printf`, sysctl o sondas DTrace a medida que construyes. Añadir la observabilidad a posteriori es más difícil que incorporarla desde el diseño.

Estos no son exhaustivos, pero son los patrones que usarás con más frecuencia. Cada uno de ellos aparece en algún lugar de nuestro driver, y cada uno de ellos aparece a lo largo del código de almacenamiento real de FreeBSD.

### Cierre de la sección 9

Un driver de almacenamiento que madura crece en direcciones predecibles: soporte multiunidad, configuración en tiempo de ejecución mediante ioctls, múltiples archivos fuente y versionado estable de cada interfaz que expone. Nada de esto tiene que aparecer en la primera versión. Saber dónde se producirá el crecimiento te permite tomar decisiones tempranas que no necesitarás deshacer más adelante.

Ya hemos cubierto todos los conceptos que el capítulo se propuso enseñar. Antes de los laboratorios prácticos, un tema más merece una sección dedicada, porque te reportará beneficios muchas veces como autor de drivers: observar un driver de almacenamiento en ejecución. En la siguiente sección, veremos las herramientas que FreeBSD te proporciona para monitorizar tu driver en tiempo real y para medir su comportamiento de forma disciplinada.

## Sección 10: Observabilidad y medición de tu driver

Escribir un driver de almacenamiento es principalmente una cuestión de acertar con la estructura. Una vez que la estructura es correcta, el driver simplemente funciona. Pero para que la estructura se mantenga correcta, debes poder observar lo que ocurre mientras el driver se ejecuta. Querrás saber cuántos BIOs por segundo llegan a la función strategy, cuánto tarda cada uno, cómo se distribuye la latencia, cuánta memoria consume el backing store, si algún BIO se está reintentando y si alguna ruta pierde la finalización de algún BIO.

FreeBSD te ofrece un conjunto notable de herramientas para esto, muchas de las cuales ya hemos usado de forma informal. En esta sección recorreremos las más importantes una por una, con el objetivo de que te sientas cómodo para recurrir a la herramienta adecuada cuando aparezca el próximo síntoma extraño.

### gstat

`gstat` es la primera herramienta a la que recurrir. Actualiza en tiempo real una vista por proveedor de la actividad de I/O y te muestra exactamente lo que ocurre en la capa GEOM.

```console
# gstat -I 1
dT: 1.002s  w: 1.000s
 L(q)  ops/s    r/s   kBps   ms/r    w/s   kBps   ms/w    %busy Name
    0    117      0      0    0.0    117    468    0.1    1.1| ada0
    0      0      0      0    0.0      0      0    0.0    0.0| myblk0
```

Las columnas, de izquierda a derecha, son:

- `L(q)`: longitud de la cola. El número de BIOs actualmente pendientes en este proveedor.
- `ops/s`: operaciones totales por segundo, independientemente de la dirección.
- `r/s`: lecturas por segundo.
- `kBps` (para lecturas): rendimiento de lectura en kilobytes por segundo.
- `ms/r`: latencia media de lectura, en milisegundos.
- `w/s`: escrituras por segundo.
- `kBps` (para escrituras): rendimiento de escritura en kilobytes por segundo.
- `ms/w`: latencia media de escritura, en milisegundos.
- `%busy`: el porcentaje de tiempo en que el proveedor no estaba inactivo.
- `Name`: el nombre del proveedor.

Para un driver que acabas de construir, `gstat` te dice de un vistazo si el kernel está enviando tráfico a tu dispositivo y cómo está rindiendo tu driver en comparación con discos reales. Si los números parecen muy diferentes de lo que esperas, tienes un punto de partida para la investigación.

`gstat -p` muestra solo proveedores (el modo predeterminado). `gstat -c` muestra solo consumidores, lo cual es menos útil para la depuración de drivers. `gstat -f <regex>` filtra por nombre. `gstat -b` muestra la salida en tandas de una pantalla a la vez en lugar de actualizarse en el lugar.

### iostat

`iostat` tiene un estilo más tradicional pero proporciona los mismos datos subyacentes. Es útil cuando quieres un registro de texto en lugar de una visualización interactiva.

```console
# iostat -x myblk0 1
                        extended device statistics
device     r/s     w/s    kr/s    kw/s  ms/r  ms/w  ms/o  ms/t qlen  %b
myblk0       0     128       0     512   0.0   0.1   0.0   0.1    0   2
myblk0       0     128       0     512   0.0   0.1   0.0   0.1    0   2
```

`iostat` puede monitorizar varios dispositivos a la vez y puede redirigirse a un archivo de registro para analizarlo más tarde. Para vistas en vivo rápidas, `gstat` suele ser mejor.

### diskinfo

`diskinfo` se ocupa menos del tráfico en vivo y más de las propiedades estáticas. Ya lo hemos usado para confirmar el tamaño de nuestro medio.

```console
# diskinfo -v /dev/myblk0
/dev/myblk0
        512             # sectorsize
        33554432        # mediasize in bytes (32M)
        65536           # mediasize in sectors
        0               # stripesize
        0               # stripeoffset
        myblk0          # Disk ident.
```

`diskinfo -c` ejecuta una prueba de temporización, leyendo unos cientos de megabytes e informando de la tasa sostenida. Es útil para una comparación de rendimiento de primer orden.

```console
# diskinfo -c /dev/myblk0
/dev/myblk0
        512             # sectorsize
        33554432        # mediasize in bytes (32M)
        65536           # mediasize in sectors
        0               # stripesize
        0               # stripeoffset
        myblk0          # Disk ident.

I/O command overhead:
        time to read 10MB block      0.000234 sec       =    0.000 msec/sector
        time to read 20480 sectors   0.001189 sec       =    0.000 msec/sector
        calculated command overhead                     =    0.000 msec/sector

Seek times:
        Full stroke:      250 iter in   0.000080 sec =    0.000 msec
        Half stroke:      250 iter in   0.000085 sec =    0.000 msec
        Quarter stroke:   500 iter in   0.000172 sec =    0.000 msec
        Short forward:    400 iter in   0.000136 sec =    0.000 msec
        Short backward:   400 iter in   0.000137 sec =    0.000 msec
        Seq outer:       2048 iter in   0.000706 sec =    0.000 msec
        Seq inner:       2048 iter in   0.000701 sec =    0.000 msec

Transfer rates:
        outside:       102400 kbytes in  0.017823 sec =  5746 MB/sec
        inside:        102400 kbytes in  0.017684 sec =  5791 MB/sec
```

Estos números son inusualmente rápidos porque el backing store es RAM. En un disco real serían muy diferentes, y comparar los números entre dispositivos suele ser el primer paso de diagnóstico para los problemas de rendimiento.

### sysctl

`sysctl` es la forma en que el kernel expone sus variables internas al espacio de usuario. Muchos subsistemas publican datos a través de `sysctl`. Puedes explorar los sysctls relacionados con el almacenamiento con:

```console
# sysctl -a | grep -i kern.geom
# sysctl -a | grep -i vfs
```

Añadir tu propio árbol sysctl a tu driver, como comentamos en la sección 9, te permite exponer las métricas que tu driver necesita rastrear, sin la ceremonia de definir una nueva herramienta.

### vmstat

`vmstat -m` muestra la asignación de memoria por etiqueta `MALLOC_DEFINE`. Nuestro driver usa `M_MYBLK`, así que podemos ver cuánta memoria ha asignado.

```console
# vmstat -m | grep myblk
       myblk     1  32768K         -       12  32K,32M
```

Las columnas son: tipo, número de asignaciones, tamaño actual, solicitudes de protección, solicitudes totales y tamaños posibles. Para un driver que mantiene un backing store de 32 MiB, el tamaño actual de 32 MiB es exactamente lo que esperamos. Si creciera con el tiempo sin una disminución equivalente al descargar el módulo, tendríamos una fuga.

`vmstat -z` muestra estadísticas del asignador de zonas. Mucho estado relacionado con el almacenamiento vive en zonas (proveedores GEOM, BIOs, estructuras de disco), y `vmstat -z` es donde mirar si sospechas fugas a nivel de GEOM.

### procstat

`procstat` muestra las pilas del kernel por thread. Es indispensable cuando algo se queda bloqueado.

```console
# procstat -kk -t $(pgrep -x g_event)
  PID    TID COMM                TDNAME              KSTACK                       
    4 100038 geom                -                   mi_switch sleepq_switch ...
```

Si el thread `g_event` está durmiendo, la capa GEOM está inactiva. Si está bloqueado en una función con el nombre de tu driver en su pila, tienes un BIO que no está completándose.

```console
# procstat -kk $(pgrep -x kldload)
```

Si `kldload` o `kldunload` se queda bloqueado, esto te muestra exactamente dónde. Lo más habitual es que el culpable sea un `disk_destroy` esperando a que se vacíen los BIOs pendientes.

### DTrace para la capa de bloques

Ya presentamos DTrace brevemente en la Sección 6 y en el Laboratorio 7. Vamos a profundizar un poco más aquí, porque DTrace es la herramienta más eficaz que existe para entender el comportamiento real del almacenamiento en tiempo de ejecución.

El proveedor Function Boundary Tracing (FBT) te permite colocar sondas en la entrada y el retorno de prácticamente cualquier función del kernel. Para la función de estrategia de nuestro driver, el nombre de la sonda es `fbt::myblk_strategy:entry` para la entrada y `fbt::myblk_strategy:return` para el retorno.

Un one-liner sencillo que cuenta BIOs por comando:

```console
# dtrace -n 'fbt::myblk_strategy:entry \
    { @c[args[0]->bio_cmd] = count(); }'
```

Cuando interrumpes el script (con `Ctrl-C`), imprime un recuento por valor de comando. `BIO_READ` es 1, `BIO_WRITE` es 2, `BIO_DELETE` es 3, `BIO_GETATTR` es 4 y `BIO_FLUSH` es 5. (Los números exactos están en `/usr/src/sys/sys/bio.h`.)

Un histograma de latencia:

```console
# dtrace -n '
fbt::myblk_strategy:entry { self->t = timestamp; }
fbt::myblk_strategy:return /self->t/ {
    @lat = quantize(timestamp - self->t);
    self->t = 0;
}'
```

Esto te da un histograma en escala logarítmica de cuánto tardó cada ejecución de la función de estrategia. Para nuestro driver en memoria, la mayoría de los cubos deberían estar en el rango de cientos de nanosegundos; cualquier valor en el rango de los milisegundos para un driver en memoria es sospechoso.

Un desglose del tamaño de I/O:

```console
# dtrace -n 'fbt::myblk_strategy:entry \
    { @sz = quantize(args[0]->bio_bcount); }'
```

Esto te muestra la distribución de los tamaños de BIO. En un sistema de archivos respaldado por UFS deberías ver picos en 4 KiB, 8 KiB, 16 KiB y 32 KiB. Con un `dd` directo y `bs=1m`, deberías ver un pico en 1 MiB (o el límite `MAXPHYS`, el que sea menor).

DTrace tiene una capacidad extraordinaria. Los one-liners anteriores apenas arañan la superficie. Si quieres profundizar, hay dos libros que merece la pena conseguir: la "DTrace Guide" original de Sun y el "DTrace Book" de Brendan Gregg. Ambos son anteriores a FreeBSD 14.3, pero los fundamentos siguen siendo válidos.

### kgdb y volcados de memoria

Cuando tu driver provoca un pánico, FreeBSD puede capturar un volcado de memoria (crash dump). Configura el dispositivo de volcado en `/etc/rc.conf` (normalmente `dumpdev="AUTO"`) y verifica con `dumpon`.

Tras un pánico, reinicia. `/var/crash/vmcore.last` (un enlace simbólico) apunta al volcado más reciente. `kgdb /boot/kernel/kernel /var/crash/vmcore.last` abre el volcado para su inspección. Algunos comandos útiles dentro de `kgdb`:

- `bt`: backtrace del thread que provocó el pánico.
- `info threads`: lista todos los threads del sistema caído.
- `thread N` y luego `bt`: backtrace del thread N.
- `print *var`: inspecciona una variable.
- `list function`: muestra el código fuente alrededor de una función.

Si has compilado tu módulo con símbolos de depuración (la configuración por defecto en la mayoría de los kernels), `kgdb` puede mostrarte variables a nivel de código fuente dentro de tu propio código. Es una capacidad que transforma tu manera de trabajar en cuanto te acostumbras a ella.

### ktrace

`ktrace` es una herramienta orientada al espacio de usuario, pero puede ser útil para depurar el almacenamiento cuando quieres ver exactamente qué llamadas al sistema está realizando un programa de usuario. Si `newfs_ufs` se comporta de forma extraña, puedes trazarlo:

```console
# ktrace -f /tmp/newfs.ktr newfs_ufs /dev/myblk0
# kdump /tmp/newfs.ktr | head -n 50
```

La traza resultante muestra la secuencia de llamadas al sistema, sus argumentos y sus resultados. Para las herramientas de almacenamiento, esto revela exactamente qué ioctls se están emitiendo y qué descriptores de archivo se están abriendo.

### dmesg y el log del kernel

El humilde `dmesg` es a menudo la forma más rápida de diagnosticar un problema. Nuestro driver escribe en él al cargar y al descargar. El kernel también escribe en él ante muchos otros eventos, como la creación de clases GEOM, violaciones de recuento de acceso y pánicos de los que el sistema se recupera.

Consejo: redirige `dmesg -a` a un archivo al inicio de cada sesión de laboratorio. Si algo va mal, tendrás un log completo.

```console
# dmesg -a > /tmp/session.log
# # ... work ...
# dmesg -a > /tmp/session-final.log
# diff /tmp/session.log /tmp/session-final.log
```

Esto te proporciona un registro preciso de lo que el kernel informó durante tu sesión.

### Una receta de medición sencilla

Aquí tienes una receta que puedes usar para obtener un perfil de rendimiento de una página sobre tu driver.

1. Carga el driver.
2. Ejecuta `diskinfo -c /dev/myblk0` y anota los tres valores de velocidad de transferencia.
3. Formatea el dispositivo y móntalo.
4. En un terminal, inicia `gstat -I 1 -f myblk0 -b` redirigido a un archivo.
5. En otro terminal, ejecuta `dd if=/dev/zero of=/mnt/myblk/stress bs=1m count=128`.
6. Detén `gstat` cuando termine `dd` y guarda el log.
7. Analiza el log con `awk` para extraer el pico de operaciones por segundo, el pico de rendimiento y la latencia media.
8. Desmonta y descarga el módulo.

Esta receta escala bien. Para un driver real lo automatizarías, lo ejecutarías con una matriz de tamaños de bloque y representarías los resultados gráficamente. Para un driver de enseñanza, ejecutarla una o dos veces te da una idea de los números y una referencia con la que comparar tras futuros cambios.

### Comparación con md(4)

Uno de los ejercicios más útiles es cargar `md(4)` con la misma configuración que tu driver y comparar.

```console
# mdconfig -a -t malloc -s 32m
md0
# diskinfo -c /dev/md0
```

Los números probablemente estarán dentro de un factor pequeño respecto a los de tu driver. Si difieren mucho, hay algo interesante ocurriendo. Las diferencias habituales son:

- `md(4)` usa un thread de trabajo que recibe BIOs de la función de estrategia y los procesa en un contexto separado. Esto añade una pequeña cantidad de latencia por BIO, pero permite mayor concurrencia.
- `md(4)` usa un respaldo página a página, que es ligeramente más lento por byte en I/O secuencial, pero escala a tamaños mucho mayores.
- `md(4)` admite más comandos y atributos BIO que nuestro driver.

Comparar con `md(4)` es una forma de depuración: si tu driver es mucho más lento o mucho más rápido que `md(4)` en la misma carga de trabajo, o bien has hecho algo inusual, o bien has descubierto una diferencia que merece entenderse.

### Cerrando la sección 10

La observabilidad no es un añadido posterior. Para un driver de almacenamiento, es la forma de mantener el rumbo. `gstat`, `iostat`, `diskinfo`, `sysctl`, `vmstat`, `procstat` y DTrace son las herramientas a las que recurrirás con más frecuencia. `kgdb` y los volcados de memoria son tu red de seguridad cuando las cosas van catastróficamente mal.

Aprende estas herramientas ahora, mientras el driver es sencillo, porque serán las mismas que uses cuando el driver sea complejo. Un desarrollador que puede observar un driver en ejecución es mucho más eficaz que uno que solo puede leer el código fuente.

Ya hemos cubierto todos los conceptos que el capítulo se propuso enseñar, además de la observabilidad y la medición. Antes de pasar a los laboratorios prácticos, dediquemos un tiempo a leer código fuente real de FreeBSD. Los casos de estudio que siguen anclan todo lo que hemos aprendido en código del árbol.

## Casos de estudio en código de almacenamiento real de FreeBSD

Leer el código fuente de drivers de producción es la forma más rápida de interiorizar patrones. En esta sección recorreremos fragmentos de tres drivers reales en `/usr/src/sys/`, con comentarios que señalan qué hace cada fragmento y por qué. Los fragmentos son cortos a propósito; no leeremos cada línea de cada driver. Seleccionaremos las líneas que importan.

Abre los archivos junto al texto y sigue la lectura. El objetivo es que veas cómo los mismos patrones de nuestro driver reaparecen en drivers reales, con nombres diferentes y con restricciones distintas.

### Caso de estudio 1: g_zero.c

`g_zero.c` es la clase GEOM más sencilla del árbol. Es un proveedor que siempre devuelve ceros en lectura y descarta las escrituras, sin almacenamiento real ni trabajo real que hacer. Su propósito es ofrecerte un "disco nulo" estándar contra el que puedas hacer pruebas. Es también una excelente referencia pedagógica porque ejercita la API `g_class` completa en menos de 150 líneas.

Vamos a ver su función de estrategia, llamada `g_zero_start`.

```c
static void
g_zero_start(struct bio *bp)
{
    switch (bp->bio_cmd) {
    case BIO_READ:
        bzero(bp->bio_data, bp->bio_length);
        g_io_deliver(bp, 0);
        break;
    case BIO_WRITE:
        g_io_deliver(bp, 0);
        break;
    case BIO_GETATTR:
    default:
        g_io_deliver(bp, EOPNOTSUPP);
        break;
    }
}
```

Tres comportamientos, con `BIO_GETATTR` incluido intencionalmente en el caso por defecto. Las lecturas se rellenan con ceros. Las escrituras se aceptan silenciosamente. Cualquier otra cosa, incluidas las consultas de atributos, recibe `EOPNOTSUPP`. El `/usr/src/sys/geom/zero/g_zero.c` real también gestiona `BIO_DELETE` en la ruta de escritura exitosa; nuestro fragmento simplificado omite ese caso para que puedas ver la estructura con claridad. Fíjate en la llamada a `g_io_deliver` en lugar de `biodone`. Esto se debe a que `g_zero` es un módulo GEOM a nivel de clase, no un módulo `g_disk`. `g_io_deliver` es la llamada de finalización a nivel de clase; `biodone` es el envoltorio de `g_disk`.

Si lees la función de estrategia de nuestro driver junto a esta, verás la misma estructura: un switch sobre `bio_cmd`, un caso para cada operación admitida y una ruta de error por defecto. Nuestro driver tiene más casos y tiene un almacenamiento real, pero la forma es idéntica.

La función `init` que `g_zero` registra con la clase también es pequeña:

```c
static void
g_zero_init(struct g_class *mp)
{
    struct g_geom *gp;

    gp = g_new_geomf(mp, "gzero");
    gp->start = g_zero_start;
    gp->access = g_std_access;
    g_new_providerf(gp, "%s", gp->name);
    g_error_provider(g_provider_by_name(gp->name), 0);
}
```

Cuando se carga el módulo `g_zero`, esta función se ejecuta. Crea un nuevo geom bajo la clase, apunta el método `start` a la función de estrategia, usa el manejador de acceso estándar y crea un proveedor. Eso es todo lo que hace falta para exponer `/dev/gzero`.

En nuestro driver, `g_disk` hace el equivalente de todo esto cuando se llama a `disk_create`. Puedes ver aquí, una vez más, lo que `g_disk` está abstrayendo. Para la mayoría de los drivers de disco es una buena elección; para `g_zero`, que no quiere las características específicas de disco de `g_disk`, usar directamente la API de clase es la opción más adecuada.

### Caso de estudio 2: md.c, la función de estrategia malloc

`md(4)` es un driver de disco en memoria con varios tipos de respaldo. El tipo de respaldo malloc es el más cercano a nuestro driver, y su función de estrategia merece leerse con detenimiento.

Aquí tienes una versión simplificada de lo que ocurre cuando el thread de trabajo de `md(4)` recoge un BIO para un disco de tipo `MD_MALLOC`. (En el `md(4)` real, esta es la función `mdstart_malloc`.)

```c
static int
mdstart_malloc(struct md_s *sc, struct bio *bp)
{
    u_char *dst, *src;
    off_t offset;
    size_t resid, len;
    int error;

    error = 0;
    resid = bp->bio_length;
    offset = bp->bio_offset;

    switch (bp->bio_cmd) {
    case BIO_READ:
        /* find the page that contains offset */
        /* copy len bytes out of it */
        /* advance, repeat until resid == 0 */
        break;
    case BIO_WRITE:
        /* find the page that contains offset */
        /* allocate it if not allocated yet */
        /* copy len bytes into it */
        /* advance, repeat until resid == 0 */
        break;
    case BIO_DELETE:
        /* free pages in the range */
        break;
    }

    bp->bio_resid = 0;
    return (error);
}
```

La diferencia clave respecto a nuestro driver es el respaldo página a página. `md(4)` no asigna un único buffer grande. Asigna páginas de 4 KiB bajo demanda y las indexa a través de una estructura de datos dentro del softc. La ventaja es que los discos en memoria pueden ser mucho más grandes de lo que permitiría un único `malloc` contiguo, y las regiones dispersas (nunca escritas) no consumen memoria.

El coste es que cada BIO puede abarcar múltiples páginas, por lo que la función de estrategia tiene que iterar en bucle. Cada iteración copia `len` bytes en la página actual, decrementa `resid`, avanza `offset` y o bien termina cuando `resid` llega a cero o bien pasa a la siguiente página.

Nuestro driver evita esta complejidad al precio de admitir solo respaldo contiguo, lo cual es válido para unas pocas decenas de megabytes, pero no más allá.

Si quisieras ampliar nuestro driver para igualar la escala de `md(4)`, el patrón página a página es la dirección a seguir. Es sencillo una vez que tienes `md(4)` delante como referencia.

### Caso de estudio 3: md.c, la ruta de carga del módulo

Otro fragmento de `md(4)` que merece estudiarse es cómo inicializa su clase y configura el dispositivo de control.

```c
static void
g_md_init(struct g_class *mp __unused)
{
    /*
     * Populate sc_list with pre-loaded memory disks
     * (preloaded kernel images, ramdisks from boot, etc.)
     */
    /* ... */

    /*
     * Create the control device /dev/mdctl.
     */
    status = make_dev_p(MAKEDEV_CHECKNAME | MAKEDEV_WAITOK,
        &status_dev, &mdctl_cdevsw, 0, UID_ROOT, GID_WHEEL,
        0600, MDCTL_NAME);
    /* ... */
}
```

La función `g_md_init` se ejecuta una vez por arranque del kernel, cuando la clase `md(4)` se instancia por primera vez. Gestiona los discos en memoria que el loader haya precargado en memoria (para que el kernel pueda arrancar desde una raíz en disco de memoria) y crea el dispositivo de control `/dev/mdctl` a través del cual `mdconfig` se comunicará con el driver más adelante.

Compáralo con nuestra función de carga, que es un simple `moduledata_t` que llama directamente a `disk_create`. `md(4)` no crea ningún disco en memoria por defecto. Solo los crea en respuesta a eventos de precarga o a ioctls `MDIOCATTACH` en el dispositivo de control.

El patrón aquí es generalizable. Si quieres un driver de almacenamiento que cree unidades bajo demanda en lugar de en el momento de la carga, debes:

1. Registrar la clase (o, para drivers basados en `g_disk`, configurar la infraestructura).
2. Crear un dispositivo de control con un cdevsw que admita ioctls.
3. Implementar ioctls de creación, destrucción y consulta.
4. Escribir una herramienta en espacio de usuario que se comunique con el dispositivo de control.

`md(4)` es el ejemplo canónico. Otros drivers, como `geli(4)` y `gmirror(4)`, usan un patrón ligeramente diferente porque son clases de transformación GEOM en lugar de drivers de disco, pero la forma general es similar.

### Caso de estudio 4: el lado newbus de un driver de almacenamiento real

A modo de contraste, veamos brevemente cómo se conecta un driver de almacenamiento real respaldado por hardware. El driver `ada(4)`, por ejemplo, es un driver ATA basado en CAM. Su ruta de attach no es directamente visible como una única función, porque CAM actúa de intermediario entre el driver y el hardware, pero el final de la cadena tiene este aspecto (abreviado de `/usr/src/sys/cam/ata/ata_da.c`):

```c
static void
adaregister(struct cam_periph *periph, void *arg)
{
    struct ada_softc *softc;
    /* ... */

    softc->disk = disk_alloc();
    softc->disk->d_open = adaopen;
    softc->disk->d_close = adaclose;
    softc->disk->d_strategy = adastrategy;
    softc->disk->d_getattr = adagetattr;
    softc->disk->d_dump = adadump;
    softc->disk->d_gone = adadiskgonecb;
    softc->disk->d_name = "ada";
    softc->disk->d_drv1 = periph;
    /* ... */
    softc->disk->d_unit = periph->unit_number;
    /* ... */
    softc->disk->d_sectorsize = softc->params.secsize;
    softc->disk->d_mediasize = ...;
    /* ... */

    disk_create(softc->disk, DISK_VERSION);
    /* ... */
}
```

La estructura es idéntica a la nuestra: rellenar una `struct disk` y llamar a `disk_create`. Las diferencias son:

- `d_strategy` es `adastrategy`, que traduce BIOs en comandos ATA y los envía al controlador a través de CAM.
- `d_dump` está implementado, porque `ada(4)` admite volcados de memoria del kernel. Nuestro driver no implementa esto.
- Los campos como `d_sectorsize` y `d_mediasize` provienen de la detección del hardware, no de macros.

Desde la perspectiva de `g_disk`, sin embargo, `ada0` y nuestro `myblk0` son el mismo tipo de objeto. Ambos son discos. Ambos reciben BIOs. Ambos se completan con `biodone`. La diferencia está en adónde van realmente los bytes.

Esta es la uniformidad que proporciona `g_disk`. Tu driver puede elegir cualquier tecnología subyacente y, siempre que rellene `struct disk` correctamente, se presentará como cualquier otro disco para el resto del kernel.

### Conclusiones de los casos prácticos

Tras leer estos fragmentos, tres patrones se hacen más evidentes.

El primero: la función strategy siempre es un switch sobre `bio_cmd`. Los casos varían, pero el switch siempre está presente. Memoriza este patrón: BIO entrante → switch → case por comando → finalización. Es el núcleo de todo driver de almacenamiento.

El segundo: los drivers `g_disk` son estructuralmente idénticos a nivel de registro. Tanto si el driver es un disco RAM como una unidad SATA real, el código de registro tiene el mismo aspecto. Las diferencias están en lo que ocurre cuando llega el BIO.

El tercero: los drivers más sofisticados encolan el trabajo en un thread dedicado. El nuestro no lo hace, porque puede realizar su trabajo de forma síncrona en cualquier thread. Los drivers que realizan trabajo lento o bloqueante deben encolar, porque las funciones strategy se ejecutan en el contexto del thread del llamador.

Con estos patrones en mente, ya puedes leer casi cualquier driver de almacenamiento del árbol de FreeBSD y seguir su estructura general, aunque los detalles específicos sobre hardware o sub-frameworks requieran un estudio más profundo.

Ya hemos cubierto todos los conceptos que el capítulo se proponía enseñar, además de la observabilidad, la medición y algunos casos prácticos reales. En la siguiente parte del capítulo, pondremos estos conocimientos en práctica a través de laboratorios prácticos. Los laboratorios se basan en el driver que has estado escribiendo y en las habilidades que has ido adquiriendo, y te llevan desde el driver mínimo funcional hasta escenarios de persistencia, montaje y limpieza. Comencemos.

## Laboratorios prácticos

Cada laboratorio es un punto de control autónomo. Están diseñados para realizarse en orden, pero puedes volver a cualquier laboratorio más adelante si quieres practicar una habilidad concreta. Cada laboratorio tiene una carpeta de acompañamiento en `examples/part-06/ch27-storage-vfs/`, que contiene la implementación de referencia y los artefactos que producirías si escribieras el código a mano.

Antes de empezar, asegúrate de que el driver del capítulo compila correctamente contra tu kernel local. Desde una copia limpia del árbol de ejemplos:

```console
# cd examples/part-06/ch27-storage-vfs
# make
# ls myblk.ko
myblk.ko
```

Si funciona, estás listo. Si no, revisa el Makefile y los consejos del Capítulo 26, sección "Tu entorno de build".

### Laboratorio 1: Explora GEOM en un sistema en ejecución

**Objetivo.** Familiarizarte con las herramientas de inspección de GEOM antes de tocar ningún código.

**Qué debes hacer.**

En tu sistema FreeBSD 14.3, ejecuta los siguientes comandos y toma notas en tu cuaderno de laboratorio.

```console
# geom disk list
# geom part show
# geom -t | head -n 40
# gstat -I 1
# diskinfo -v /dev/ada0   # or whatever your primary disk is called
```

**Qué debes buscar.**

Identifica cada geom de clase `DISK`. Para cada uno, anota su nombre de proveedor, su tamaño de medio, su tamaño de sector y su modo actual. Observa qué geoms tienen capas de particionado encima y cuáles no. Si tu sistema tiene `geli` o `zfs`, observa la cadena de clases.

**Pregunta adicional.** ¿Cuáles de tus geoms tienen recuentos de acceso distintos de cero ahora mismo? ¿Cuáles están libres? ¿Qué ocurriría si intentaras ejecutar `newfs_ufs` en cada uno?

**Implementación de referencia.** `examples/part-06/ch27-storage-vfs/lab01-explore-geom/README.md` contiene una guía sugerida y una transcripción de salida de ejemplo de un sistema típico.

### Laboratorio 2: Construye el driver esqueleto

**Objetivo.** Conseguir que el driver esqueleto de la Sección 3 compile y se cargue en tu sistema.

**Qué debes hacer.**

Copia `examples/part-06/ch27-storage-vfs/lab02-skeleton/myfirst_blk.c` y su `Makefile` en un directorio de trabajo. Compílalo.

```console
# cp -r examples/part-06/ch27-storage-vfs/lab02-skeleton /tmp/myblk
# cd /tmp/myblk
# make
```

Carga el módulo.

```console
# kldload ./myblk.ko
# dmesg | tail -n 2
# ls /dev/myblk0
# geom disk list myblk0
```

Descárgalo.

```console
# kldunload myblk
# ls /dev/myblk0
```

**Qué debes buscar.**

Confirma que el kernel imprimió tu mensaje `myblk: loaded`. Confirma que apareció `/dev/myblk0`. Confirma que `geom disk list` informó del tamaño de medio esperado. Confirma que el nodo desapareció tras la descarga.

**Pregunta adicional.** ¿Qué ocurre si intentas `newfs_ufs -N /dev/myblk0` con el driver esqueleto? ¿Puedes leer la salida? ¿Por qué tiene éxito la ejecución en seco aunque las escrituras reales fallaran?

### Laboratorio 3: Implementa el gestor de BIO

**Objetivo.** Añadir la función strategy funcional de la Sección 5 al driver esqueleto.

**Qué debes hacer.**

Partiendo del esqueleto, implementa `myblk_strategy` con soporte para `BIO_READ`, `BIO_WRITE`, `BIO_DELETE` y `BIO_FLUSH`. Asigna el buffer de respaldo en `myblk_attach_unit` y libéralo en `myblk_detach_unit`.

Compila, carga y prueba.

```console
# dd if=/dev/zero of=/dev/myblk0 bs=4096 count=16
# dd if=/dev/myblk0 of=/dev/null bs=4096 count=16
# dd if=/dev/random of=/dev/myblk0 bs=4096 count=16
# dd if=/dev/myblk0 of=/tmp/a bs=4096 count=16
# dd if=/dev/myblk0 of=/tmp/b bs=4096 count=16
# cmp /tmp/a /tmp/b
```

**Qué debes buscar.**

El último `cmp` debe tener éxito sin producir ninguna salida. Si imprime `differ: byte N`, tu función strategy tiene una condición de carrera o está devolviendo datos obsoletos.

**Pregunta adicional.** Añade un `printf` en la función strategy que informe de `bio_cmd`, `bio_offset` y `bio_bcount`. Ejecuta `dd if=/dev/myblk0 of=/dev/null bs=1m count=1` y observa `dmesg`. ¿Qué tamaño emitió realmente `dd`? ¿Ves fragmentación?

**Implementación de referencia.** `examples/part-06/ch27-storage-vfs/lab03-bio-handler/myfirst_blk.c`.

### Laboratorio 4: Aumenta el tamaño y monta UFS

**Objetivo.** Aumentar el almacenamiento de respaldo a 32 MiB y montar UFS en el dispositivo.

**Qué debes hacer.**

Cambia `MYBLK_MEDIASIZE` a `(32 * 1024 * 1024)` y vuelve a compilar. Carga el módulo. Formatea y monta.

```console
# newfs_ufs /dev/myblk0
# mkdir -p /mnt/myblk
# mount /dev/myblk0 /mnt/myblk
# echo "hello" > /mnt/myblk/greeting.txt
# cat /mnt/myblk/greeting.txt
# umount /mnt/myblk
# mount /dev/myblk0 /mnt/myblk
# cat /mnt/myblk/greeting.txt
# umount /mnt/myblk
# kldunload myblk
```

**Qué debes buscar.**

Verifica que el archivo sobrevive a un ciclo de desmontaje y remontaje. Verifica que los recuentos de acceso en `geom disk list` son cero tras el desmontaje. Verifica que `kldunload` tiene éxito sin errores.

**Pregunta adicional.** Observa `gstat -I 1` mientras ejecutas `dd if=/dev/zero of=/mnt/myblk/big bs=1m count=16`. ¿Puedes ver cómo llegan las escrituras en ráfagas? ¿Qué tamaño tienen los BIOs individuales? Pista: el tamaño de bloque por defecto de UFS es normalmente 32 KiB en un sistema de archivos tan pequeño.

**Implementación de referencia.** `examples/part-06/ch27-storage-vfs/lab04-mount-ufs/myfirst_blk.c`.

### Laboratorio 5: Observar la persistencia real entre recargas con md(4)

**Objetivo.** Confirmar experimentalmente que la persistencia entre recargas requiere un respaldo externo, tal como argumentó la Sección 7, usando el modo vnode de `md(4)` como control.

**Qué debes hacer.**

Primero, demuestra que nuestro `myblk` respaldado por RAM pierde su sistema de archivos al recargar. Carga, formatea, monta, escribe, desmonta, descarga, recarga, monta de nuevo y observa el sistema de archivos vacío.

```console
# kldload ./myblk.ko
# newfs_ufs /dev/myblk0
# mount /dev/myblk0 /mnt/myblk
# echo "not persistent" > /mnt/myblk/token.txt
# umount /mnt/myblk
# kldunload myblk
# kldload ./myblk.ko
# mount /dev/myblk0 /mnt/myblk
# ls /mnt/myblk
```

El `ls` debería mostrar un directorio UFS vacío o recién creado; el archivo `token.txt` ha desaparecido porque el kernel reclamó el buffer de respaldo cuando se descargó el módulo.

Ahora realiza la misma secuencia con el backend vnode de `md(4)`, que utiliza un archivo real en disco:

```console
# truncate -s 64m /var/tmp/mdimage.img
# mdconfig -a -t vnode -f /var/tmp/mdimage.img -u 9
# newfs_ufs /dev/md9
# mount /dev/md9 /mnt/md
# echo "persistent" > /mnt/md/token.txt
# umount /mnt/md
# mdconfig -d -u 9
# mdconfig -a -t vnode -f /var/tmp/mdimage.img -u 9
# mount /dev/md9 /mnt/md
# cat /mnt/md/token.txt
persistent
```

**Qué debes buscar.**

La primera secuencia pierde el archivo; la segunda lo conserva. La diferencia está en que `md9` está respaldado por un archivo real en disco, cuyo estado sobrevive independientemente de lo que ocurra dentro del kernel. Contrasta esto con `myblk0`, que está respaldado por el heap del kernel, el cual desaparece al ejecutar `kldunload`.

**Pregunta adicional.** Lee la rama `MD_VNODE` de `mdstart_vnode` en `/usr/src/sys/dev/md/md.c`. Identifica dónde se almacena la referencia al vnode (pista: vive en el `struct md_s` por unidad, no en un global de ámbito de módulo). Explica con tus propias palabras por qué ese diseño es lo que permite que el respaldo sobreviva a los ciclos de vida del módulo.

**Implementación de referencia.** `examples/part-06/ch27-storage-vfs/lab05-persistence/README.md` recorre ambas secuencias y su salida de diagnóstico.

### Laboratorio 6: Desmontaje seguro bajo carga

**Objetivo.** Verificar que la ruta de desmontaje gestiona correctamente un sistema de archivos activo.

**Qué debes hacer.**

Carga el módulo, fórmatealo y móntalo. En un terminal, inicia un bucle de carga.

```console
# while true; do dd if=/dev/urandom of=/mnt/myblk/stress bs=4k \
    count=512 2>/dev/null; sync; done
```

En otro terminal, intenta descargarlo.

```console
# kldunload myblk
kldunload: can't unload file: Device busy
```

Detén el bucle de carga. Desmonta. Descarga.

**Qué debes buscar.**

La descarga inicial debe fallar de forma controlada. Tras el desmontaje, la descarga final debe tener éxito. `dmesg` no debe mostrar ninguna advertencia del kernel.

**Pregunta adicional.** En lugar de matar el bucle de carga, prueba a ejecutar `umount /mnt/myblk` directamente. ¿Permite UFS desmontar mientras hay escrituras en curso? ¿Cuál es el error y qué significa?

**Implementación de referencia.** `examples/part-06/ch27-storage-vfs/lab06-safe-unmount/` incluye un script de prueba que ejecuta la secuencia anterior e informa de los fallos.

### Laboratorio 7: Observar el tráfico de BIO con DTrace

**Objetivo.** Usar DTrace para observar el camino del BIO en tiempo real.

**Qué debes hacer.**

Con el driver cargado y un sistema de archivos montado, ejecuta el siguiente comando de una línea de DTrace en un terminal:

```console
# dtrace -n 'fbt::myblk_strategy:entry { \
    printf("cmd=%d off=%lld len=%u", \
        args[0]->bio_cmd, args[0]->bio_offset, \
        args[0]->bio_bcount); \
    @count[args[0]->bio_cmd] = count(); \
}'
```

En otro terminal, crea y lee archivos en el sistema de archivos montado.

**Qué debes buscar.**

Anota qué comandos BIO observas y en qué cantidades. Anota los desplazamientos y longitudes típicos. Compara los patrones del tráfico de `dd` con los de `cp` y los de `tar`. Observa cómo `cp` o `mv` pueden producir patrones de BIO muy distintos dependiendo de lo que el buffer cache decida volcar.

**Pregunta adicional.** Ejecuta `sync` mientras DTrace está en marcha. ¿Qué comandos BIO provoca `sync`? ¿Y `newfs_ufs`?

**Implementación de referencia.** `examples/part-06/ch27-storage-vfs/lab07-dtrace/README.md` con salida de DTrace de ejemplo y notas.

### Laboratorio 8: Añadir un atributo getattr

**Objetivo.** Implementar un callback `d_getattr` que responda a `GEOM::ident`.

**Qué debes hacer.**

Añade la función `myblk_getattr` de la Sección 5 al driver y regístrala en el disco antes de `disk_create`. Vuelve a compilar, recarga y comprueba `diskinfo -v /dev/myblk0`.

**Qué debes buscar.**

El campo `ident` debería mostrar ahora `MYBLK0` en lugar de `(null)`.

**Pregunta adicional.** ¿Qué otros atributos podría consultar un sistema de archivos? Observa `/usr/src/sys/geom/geom.h` para ver atributos con nombre como `GEOM::rotation_rate`. Intenta implementar ese también.

**Implementación de referencia.** `examples/part-06/ch27-storage-vfs/lab08-getattr/myfirst_blk.c`.

### Laboratorio 9: Explorar md(4) para comparación

**Objetivo.** Leer un driver de almacenamiento real de FreeBSD e identificar los patrones que hemos utilizado.

**Qué debes hacer.**

Abre `/usr/src/sys/dev/md/md.c`. Es un archivo largo. No intentes leer cada línea. En cambio, busca y comprende estas cosas concretas:

1. La estructura `g_md_class` al principio del archivo.
2. El softc `struct md_s`.
3. La función `mdstart_malloc` que gestiona `BIO_READ` y `BIO_WRITE` para los discos de memoria `MD_MALLOC`.
4. El patrón de worker thread en `md_kthread` (o su equivalente en tu versión).
5. El gestor ioctl `MDIOCATTACH` que crea nuevas unidades bajo demanda.

Compara cada uno de estos elementos con el código correspondiente en nuestro driver.

**Qué debes buscar.**

Identifica las diferencias. ¿En qué partes tiene `md(4)` funcionalidades que nosotros no tenemos? ¿Dónde tiene nuestro driver el mismo mecanismo de forma más simple? ¿En qué puntos tendrías que extender nuestro driver para añadir una de las funcionalidades de `md(4)`?

**Notas de referencia.** `examples/part-06/ch27-storage-vfs/lab09-md-comparison/NOTES.md` contiene una guía mapeada de las secciones relevantes de `md.c` para FreeBSD 14.3.

### Laboratorio 10: Rómpelo a propósito

**Objetivo.** Provocar modos de fallo conocidos para poder reconocerlos rápidamente en trabajo real.

**Qué debes hacer.**

Toma una copia limpia del driver completado del Laboratorio 8. En copias separadas (no mezcles los fallos entre sí), introduce los siguientes errores de uno en uno, vuelve a compilar, carga y observa.

**Fallo 1: Olvidar biodone.** Comenta la llamada `biodone(bp)` en el caso `BIO_READ`. Carga, monta y ejecuta `cat` sobre un archivo. El `cat` se colgará indefinidamente. Intenta matarlo con `Ctrl-C`; puede que no responda. Usa `procstat -kk` sobre el PID bloqueado para ver dónde está esperando el proceso. Este es el síntoma clásico de un BIO filtrado.

**Fallo 2: Liberar el respaldo antes de disk_destroy.** En `myblk_detach_unit`, invierte el orden de modo que `free(sc->backing, ...)` aparezca antes que `disk_destroy(sc->disk)`. Carga, formatea, monta, desmonta e intenta descargarlo. Si no hay ningún BIO en vuelo durante la ventana de descarga, saldrás ileso. Si hay algún BIO en vuelo (usa un `dd` en ejecución para asegurarte), se producirá un panic con un fallo de página.

**Fallo 3: Omitir bio_resid.** Elimina la línea `bp->bio_resid = 0` del caso `BIO_READ`. Carga, formatea, monta y crea un archivo. Léelo de nuevo. Dependiendo de la basura que hubiera en `bio_resid` en el momento de la asignación, el sistema de archivos puede informar de tamaños de lectura incorrectos y registrar errores. A veces funciona; otras veces no. Este es el fallo intermitente característico de un `bio_resid` olvidado.

**Breakage 4: Off-by-one en los límites.** Cambia la comprobación de límites de `offset > sc->backing_size` a `offset >= sc->backing_size`. Esto rechaza lecturas válidas en el último offset. Carga, formatea, monta. Intenta escribir un archivo que llegue hasta el último bloque. Observa si UFS lo detecta, si `dd` lo detecta y qué error se notifica.

**Qué buscar.**

En cada caso, describe en tu cuaderno de laboratorio qué observaste, qué herramienta reveló el problema (dmesg, `procstat`, `gstat`, traza del pánico) y cuál sería la corrección. A continuación, aplica la corrección y confirma que la operación vuelve a ser normal.

**Pregunta de ampliación.** ¿Qué secuencia de comandos reproduce de forma fiable cada fallo? ¿Puedes escribir un script de shell que provoque de manera determinista el Breakage 1 o el Breakage 2?

**Notas de referencia.** `examples/part-06/ch27-storage-vfs/lab10-break-on-purpose/BREAKAGES.md` contiene descripciones breves y un script de prueba para cada modo de fallo.

### Laboratorio 11: Medir con diferentes tamaños de bloque

**Objetivo.** Comprender cómo afecta el tamaño del BIO al rendimiento.

**Qué debes hacer.**

Con el driver cargado y un sistema de archivos montado, ejecuta `dd` con tamaños de bloque progresivamente mayores y mide el tiempo de cada ejecución.

```console
# for bs in 512 4096 32768 131072 524288 1048576; do
    rm -f /mnt/myblk/bench
    time dd if=/dev/zero of=/mnt/myblk/bench bs=$bs count=$((16*1024*1024/bs))
done
```

Registra el rendimiento en cada caso.

**Qué observar.**

El rendimiento debería aumentar a medida que crece el tamaño de bloque, hasta estabilizarse en torno a `d_maxsize` (normalmente 128 KiB). Los tamaños de bloque muy pequeños estarán dominados por el coste por BIO.

**Pregunta adicional.** ¿A qué tamaño de bloque se estabiliza visiblemente la curva? ¿Por qué?

### Laboratorio 12: Prueba de condición de carrera con dos procesos

**Objetivo.** Observar cómo gestiona el driver el acceso simultáneo desde múltiples procesos.

**Qué debes hacer.**

Con el driver cargado y un sistema de archivos montado, ejecuta dos procesos `dd` en paralelo escribiendo en archivos distintos.

```console
# dd if=/dev/urandom of=/mnt/myblk/a bs=4k count=1024 &
# dd if=/dev/urandom of=/mnt/myblk/b bs=4k count=1024 &
# wait
```

Registra el rendimiento combinado.

**Qué observar.**

Ambas escrituras deben completarse sin corrupción. Verifica con `md5` o `sha256` cada archivo. El rendimiento combinado puede ser ligeramente inferior al doble del rendimiento con un solo proceso, debido a la contención del lock en nuestro mutex de grano grueso.

**Pregunta adicional.** ¿Afecta al rendimiento eliminar el mutex? ¿Provoca corrupción? ¿Por qué?

### Sobre la disciplina en el laboratorio

Cada laboratorio es pequeño y ninguno es un examen. Si te quedas atascado, la implementación de referencia está ahí para que la compares. No la copies y pegues como primer intento, sin embargo. Copiar no es la habilidad. La habilidad está en teclear, leer, diagnosticar y verificar.

Mantén el cuaderno de notas abierto. Registra lo que ejecutaste, lo que viste y lo que te sorprendió. Los bugs de almacenamiento suelen repetirse entre proyectos, y tu yo futuro agradecerá a tu yo actual esas anotaciones.

## Ejercicios de desafío

Los ejercicios de desafío llevan el driver un poco más lejos. Cada uno está acotado a algo que un principiante puede lograr con el material ya cubierto en el capítulo, combinado con una lectura atenta del código fuente de FreeBSD. No tienen límite de tiempo. Tómate tu tiempo. Abre el árbol de código fuente. Consulta las páginas del manual. Compara tu solución con `md(4)` cuando tengas dudas.

Cada desafío tiene una carpeta stub en `examples/part-06/ch27-storage-vfs/`, pero no se proporciona una solución de referencia. El objetivo es que los trabajes por ti mismo. Las soluciones se dejan como seguimiento que puedes comparar con otros o anotar en tus apuntes de estudio.

### Desafío 1: Implementar un modo de solo lectura

Añade un parámetro ajustable en la carga del módulo que permita al driver arrancar en modo de solo lectura. En ese modo, `BIO_WRITE` y `BIO_DELETE` deben fallar con `EROFS`. `newfs_ufs` debe rechazar el formateo del dispositivo, y `mount` sin `-r` debe rechazar el montaje.

Pista. El parámetro puede ser un `sysctl_int` vinculado a una variable static. `TUNABLE_INT` es otra forma, usada únicamente en el momento de carga. Tu función strategy puede comprobar la variable antes de despachar escrituras. Recuerda que cambiar el modo en tiempo de ejecución con un sistema de archivos montado es una receta para la corrupción: puedes prohibir el cambio o documentar que el parámetro solo tiene efecto al cargar el módulo.

### Desafío 2: Implementar una segunda unidad

Añade soporte para exactamente dos unidades: `myblk0` y `myblk1`. Cada una debe tener su propio almacenamiento de respaldo con su propio tamaño. No intentes implementar una asignación de unidades completamente dinámica; simplemente codifica dos softcs y dos llamadas attach en el cargador del módulo.

Pista. Mueve la asignación del almacenamiento de respaldo, la asignación del disco y la creación del disco a `myblk_attach_unit` parametrizado por número de unidad y tamaño, y llámalo dos veces desde el cargador. Asegúrate de que el camino de detach recorre ambas unidades.

### Desafío 3: Honrar BIO_DELETE con un contador sysctl

Extiende el manejo de `BIO_DELETE` para que también incremente un contador `sysctl` que informe del total de bytes eliminados. Verifica con `sysctl dev.myblk` mientras ejecutas `fstrim /mnt/myblk` o mientras `dd` escribe y sobreescribe archivos.

Pista. UFS no emite `BIO_DELETE` por defecto. Para ver el tráfico de eliminación, monta con `-o trim`. Puedes verificar el flujo trim con tu one-liner de DTrace del Laboratorio 7.

### Desafío 4: Responder a BIO_GETATTR para rotation_rate

Extiende `myblk_getattr` para responder a `GEOM::rotation_rate` con `DISK_RR_NON_ROTATING` (definido en `/usr/src/sys/geom/geom_disk.h`). Verifica con `gpart show` y `diskinfo -v` que el dispositivo se reporta como no rotacional.

Pista. El atributo se devuelve como un `u_int` simple. Observa cómo `md(4)` gestiona `BIO_GETATTR` para atributos comparables.

### Desafío 5: Cambiar el tamaño del dispositivo

Añade un ioctl que permita al espacio de usuario redimensionar el almacenamiento de respaldo mientras no haya nada montado. Si hay un sistema de archivos montado, el ioctl debe fallar con `EBUSY`. Si el redimensionado tiene éxito, actualiza `d_mediasize` y notifica a GEOM para que `diskinfo` informe del nuevo tamaño.

Pista. Observa el manejo de `MDIOCRESIZE` en `md(4)` para el patrón. Este es un desafío no trivial; tómate tu tiempo y prueba con sistemas de archivos desechables. No lo intentes con ningún almacenamiento que te importe perder.

### Desafío 6: Un contador de escrituras y visualización de tasa

Añade contadores de bytes escritos por segundo, expuestos mediante `sysctl`, y un pequeño script de shell en espacio de usuario que lea el sysctl cada segundo e imprima una tasa legible por humanos. Esto es útil para las pruebas y te da experiencia conectando métricas a través de la maquinaria de observabilidad del kernel.

Pista. Usa `atomic_add_long` en los contadores. El script de shell es un one-liner en bucles `while true`.

### Desafío 7: Un almacenamiento de respaldo con patrón fijo

Implementa un modo de almacenamiento de respaldo donde las lecturas siempre devuelvan un patrón de bytes fijo y las escrituras se descarten silenciosamente. Es similar a `g_zero` pero con un byte de patrón configurable. Es útil para las pruebas de estrés de las capas superiores cuando no te importa el contenido de los datos.

Pista. Ramifica según una variable de modo dentro de la función strategy. Mantén el almacenamiento en memoria para el modo normal y omite el `memcpy` en el modo de patrón.

### Desafío 8: Escribir una utilidad de control similar a mdconfig

Escribe un pequeño programa en espacio de usuario que se comunique con un dispositivo de control de tu driver (tendrás que añadirlo) y pueda crear, destruir y consultar unidades en tiempo de ejecución. El programa debe aceptar argumentos de línea de comandos similares a los de `mdconfig`.

Pista. Este es un desafío considerable. Empieza con un único ioctl que imprima "hola" y ve construyendo desde ahí. Usa `make_dev` en un cdev para tu dispositivo de control y luego implementa `d_ioctl` en ese cdev.

### Desafío 9: Sobrevivir a un fallo simulado

Añade un modo donde el driver descarte silenciosamente cada N-ésima escritura (fingiendo que la escritura tuvo éxito pero sin hacer nada). Úsalo para probar la resiliencia de UFS ante escrituras perdidas.

Pista. Este es un modo peligroso. Úsalo únicamente con sistemas de archivos desechables. Deberías poder reproducir escenarios interesantes de reparación con `fsck_ffs`. Prepárate para explicarte a ti mismo por qué este modo solo es seguro en pseudodispositivos que puedes regenerar desde cero.

### Desafío 10: Comprender md(4) lo suficiente como para enseñarlo

Escribe una explicación de una página sobre cómo `md(4)` crea una nueva unidad en respuesta a `MDIOCATTACH`. Cubre el camino del ioctl, la asignación del softc, la inicialización específica del tipo de almacenamiento de respaldo, el cableado de `g_disk` y la creación del thread de trabajo. Es un desafío de lectura más que de codificación, pero es uno de los ejercicios más útiles que puedes hacer para profundizar en tu comprensión de la pila de almacenamiento.

Pista. `/usr/src/sys/dev/md/md.c` y `/usr/src/sbin/mdconfig/mdconfig.c` son los dos archivos que debes leer. Presta atención a la estructura `struct md_ioctl` en `/usr/src/sys/sys/mdioctl.h`, porque es el ABI entre el espacio de usuario y el kernel.

### Cuándo intentar los desafíos

No necesitas hacerlos todos. Elige uno o dos que te llamen la atención por curiosidad o por algo que puedas imaginar usando más adelante. Un desafío bien hecho vale más que cinco a medias. La implementación de referencia de `md(4)` estará ahí siempre que quieras comparar tu enfoque con un driver de producción.

## Solución de problemas

Los drivers de almacenamiento tienen una familia particular de modos de fallo. Algunos son evidentes en el momento en que ocurren. Otros son silenciosos al principio y solo se hacen obvios tras un reinicio, a veces con corrupción de datos por medio. Esta sección enumera los síntomas que es más probable que encuentres mientras trabajas en el capítulo y los laboratorios, junto con las causas y soluciones habituales. Úsala como referencia cuando algo falle, y léela al menos una vez antes de empezar, porque es mucho más fácil reconocer un modo de fallo la segunda vez.

### `kldload` tiene éxito pero no aparece ningún nodo en /dev

**Síntoma.** `kldload` devuelve cero. `kldstat` muestra el módulo cargado. Pero `/dev/myblk0` no existe.

**Causas probables.**

- Olvidaste llamar a `disk_create`. El softc está asignado, el disco está asignado, pero el disco no está registrado en GEOM.
- Llamaste a `disk_create` con `d_name` apuntando a un puntero nulo o a una cadena vacía.
- Llamaste a `disk_create` con `d_mediasize` a cero. `g_disk` rechaza silenciosamente crear un proveedor de tamaño cero.
- Llamaste a `disk_create` antes de rellenar los campos. El framework captura los valores de los campos en el momento del registro y no los vuelve a leer después.

**Solución.** Comprueba el buffer de mensajes del kernel con `dmesg`. `g_disk` imprime un diagnóstico cuando rechaza un registro. Corrige el valor del campo y vuelve a compilar.

### `kldload` falla con "module version mismatch"

**Síntoma.** Al cargar el módulo se informa de `kldload: can't load ./myblk.ko: No such file or directory` o de un error más explícito sobre incompatibilidad de versiones.

**Causas probables.**

- Compilaste contra un kernel distinto al que está en ejecución.
- Modificaste `DISK_VERSION` por tu cuenta, lo cual nunca debes hacer.
- Olvidaste `MODULE_VERSION(myblk, 1)`.

**Solución.** Comprueba `uname -a` y la versión del kernel que eligió tu compilación. Recompila contra el kernel en ejecución.

### `diskinfo` muestra un tamaño incorrecto

**Síntoma.** `diskinfo -v /dev/myblk0` muestra un tamaño que no coincide con `MYBLK_MEDIASIZE`.

**Causas probables.**

- Estableciste `d_mediasize` con una expresión incorrecta. Un error de off-by-one habitual es asignarlo al número de sectores en lugar de al número de bytes.
- Tienes `MYBLK_MEDIASIZE` definido como algo distinto a `(size * 1024 * 1024)` y la macro se interpreta de forma diferente a la que pretendías. Usa paréntesis de forma generosa.

**Solución.** Imprime el tamaño en tu mensaje de carga y verifícalo con `diskinfo -v`.

### `newfs_ufs` falla con "Device not configured"

**Síntoma.** `newfs_ufs /dev/myblk0` muestra `newfs: /dev/myblk0: Device not configured`.

**Causas probables.**

- Tu función strategy sigue siendo el marcador de posición que devuelve `ENXIO` para todo. `ENXIO` se mapea al mensaje `Device not configured` mediante `errno`.

**Solución.** Implementa la función strategy de la Sección 5.

### `newfs_ufs` se queda colgado

**Síntoma.** `newfs_ufs /dev/myblk0` arranca pero nunca termina.

**Causas probables.**

- Tu función strategy no llama a `biodone` en algún camino de ejecución. `newfs_ufs` emite un BIO, espera su finalización, y esperará indefinidamente si la finalización nunca llega.
- Tu función strategy llama a `biodone` dos veces en algún camino. La primera llamada devuelve éxito; la segunda normalmente provoca un pánico, pero en algunos casos el estado del BIO queda lo suficientemente corrompido como para que el proceso se quede colgado.

**Solución.** Audita tu función strategy. Cada camino de flujo de control debe terminar con exactamente una llamada a `biodone(bp)`. Un patrón útil es usar un único punto de salida al final de la función.

### `mount` falla con "bad superblock"

**Síntoma.** `mount /dev/myblk0 /mnt/myblk` informa de `mount: /dev/myblk0: bad magic`.

**Causas probables.**

- Tu función strategy devuelve datos incorrectos para algunos desplazamientos. El superbloque está en el desplazamiento 65536, y UFS lo valida con cuidado.
- Tu comprobación de límites está rechazando una lectura legítima.
- Tu `memcpy` copia desde una dirección incorrecta (normalmente un error de off-by-one en la aritmética de desplazamientos).

**Solución.** Escribe un patrón conocido en el dispositivo con `dd`, luego léelo de vuelta con `dd` en varios desplazamientos y compara con `cmp`. Si el patrón va y vuelve correctamente, el I/O básico es correcto. Si no, encuentra el primer desplazamiento donde diverge e inspecciona el código en la comprobación de límites o en la aritmética de desplazamientos correspondiente.

### `kldunload` se bloquea

**Síntoma.** `kldunload myblk` no retorna.

**Causas probables.**

- Hay un BIO en vuelo y tu función strategy nunca llama a `biodone`. `disk_destroy` está esperando a que ese BIO finalice.
- Has añadido un thread de trabajo que duerme dentro de una función que nunca será despertada.

**Solución.** Ejecuta `procstat -kk` en otro terminal. Observa la pila del thread `g_event` y la de cualquier thread de tu driver. Si están bloqueados en un estado `sleep` o `waitfor`, tienes un BIO perdido o un worker defectuoso.

### `kldunload` devuelve "Device busy"

**Síntoma.** `kldunload myblk` informa de `Device busy` y termina.

**Causas probables.**

- Hay un sistema de archivos montado en `/dev/myblk0`.
- Algún programa mantiene `/dev/myblk0` abierto para acceso directo.
- Una sesión de terminal anterior tiene un proceso `dd` ejecutándose en segundo plano.

**Solución.** Ejecuta `mount | grep myblk` para comprobar si hay montajes activos. Ejecuta `fuser /dev/myblk0` para encontrar descriptores abiertos. Desmonta, cierra y luego descarga el módulo.

### Kernel panic con "freeing free memory"

**Síntoma.** El kernel entra en pánico con un mensaje sobre la liberación de memoria ya liberada, mostrando una traza de pila que pasa por tu driver.

**Causas probables.**

- La ruta de detach libera el softc o el almacén de respaldo dos veces.
- Un thread de trabajo sobrevivió a `disk_destroy` e intentó acceder a estado ya liberado.

**Solución.** Revisa el orden de detach. Destruye el disco primero (lo que espera a que terminen los BIOs en vuelo), después libera el respaldo, luego destruye el mutex y finalmente libera el softc. Si añadiste un thread de trabajo, asegúrate de que haya terminado antes de llamar a cualquier `free`.

### Kernel panic con "vm_fault: kernel mode"

**Síntoma.** El kernel entra en pánico con un fallo de página dentro de tu driver, normalmente en la función strategy o en la ruta de detach.

**Causas probables.**

- Desreferenciaste un puntero nulo o ya liberado. El caso más habitual es usar `sc->backing` después de haberlo liberado.
- Confundiste `bp->bio_data` con `bp->bio_disk` y leíste del puntero incorrecto.

**Solución.** Audita el ciclo de vida de los punteros. Si el almacén de respaldo se libera durante el detach, asegúrate de que ningún BIO pueda llegar a la función strategy después de ese momento. El orden correcto es `disk_destroy` -> `free(backing)`.

### `gstat` no muestra actividad

**Síntoma.** Estás ejecutando `dd` o `newfs_ufs` contra el dispositivo, pero `gstat -f myblk0` muestra cero operaciones por segundo.

**Causas probables.**

- Estás observando el dispositivo equivocado. `gstat -f myblk0` usa una expresión regular; asegúrate de que el nombre del dispositivo coincide.
- Tu driver usa un nombre de clase GEOM personalizado que `gstat` está filtrando.

**Solución.** Ejecuta `gstat` sin el filtro y busca tu dispositivo. Revisa el campo de nombre con atención.

### "Operation not supported" para DELETE

**Síntoma.** El montaje con trim falla o `fstrim` imprime "Operation not supported".

**Causas probables.**

- Tu función strategy no gestiona `BIO_DELETE` y devuelve `EOPNOTSUPP`.
- El sistema de archivos consultó el soporte de `BIO_DELETE` durante el montaje y almacenó en caché el resultado negativo.

**Solución.** Implementa `BIO_DELETE` en la función strategy, luego desmonta y vuelve a montar. La mayoría de los sistemas de archivos solo comprueban este soporte en el momento del montaje.

### /dev/myblk0 no aparece hasta varios segundos después de kldload

**Síntoma.** Inmediatamente después de `kldload`, `ls /dev/myblk0` falla. Unos instantes después, tiene éxito.

**Causas probables.**

- GEOM procesa los eventos de forma asíncrona. `disk_create` encola un evento, y el proveedor no se publica hasta que el thread de eventos lo procesa.
- En un sistema con carga, la cola de eventos puede ser lenta.

**Solución.** Este es el comportamiento normal. Si tus scripts dependen de que el nodo exista inmediatamente después de `kldload`, añade una pequeña pausa o un bucle de espera.

### Los datos escritos son legibles pero están corruptos

**Síntoma.** Una lectura posterior a una escritura devuelve el número correcto de bytes, pero con contenido diferente.

**Causas probables.**

- Un error de desplazamiento de uno en la aritmética de offsets del almacén de respaldo.
- Un BIO concurrente se solapa con el que esperas, y el lock no se mantiene el tiempo suficiente.
- La función strategy lee de `bp->bio_data` antes de que el kernel haya terminado de configurarlo (esto es muy poco probable con BIOs normales, pero puede ocurrir con errores en el análisis de atributos).

**Solución.** Añade un `printf` en la función strategy que registre los primeros bytes antes y después del `memcpy`. Repite la prueba con un patrón conocido y busca la discrepancia.

### El almacén de respaldo no se libera, la memoria crece con cada recarga

**Síntoma.** `vmstat -m | grep myblk` muestra que los bytes asignados crecen con cada ciclo de carga/descarga.

**Causas probables.**

- El manejador `MOD_UNLOAD` retornó sin llamar a `myblk_detach_unit`, por lo que el `free(sc->backing, M_MYBLK)` fue omitido.
- Un camino de error en `MOD_UNLOAD` retornó antes de llegar a la liberación. Cada camino de error debe liberar memoria o la asignación provocará una fuga.
- Un thread de trabajo mantiene una referencia al softc y el manejador se niega a liberar mientras esa referencia existe.

**Solución.** Audita la ruta `MOD_UNLOAD`. `vmstat -m` es una herramienta sencilla pero eficaz. Añade un `printf` en la ruta de liberación para confirmar que se está alcanzando.

### `gstat` muestra una longitud de cola muy elevada

**Síntoma.** `gstat -I 1` muestra que `L(q)` sube a decenas o cientos y nunca vuelve a cero.

**Causas probables.**

- Tu función strategy es lenta o está bloqueando, provocando que los BIOs se encolen más rápido de lo que se procesan.
- Añadiste un thread de trabajo, pero el planificador lo ejecuta con menos frecuencia de la necesaria.
- Un cuello de botella de sincronización (un mutex muy disputado) está serializando el trabajo.

**Solución.** Perfila con DTrace para averiguar qué hace la función strategy. Si la latencia por BIO ha aumentado, investiga el motivo. Para un driver en memoria, esto casi nunca debería ocurrir; si ocurre, es probable que hayas introducido una llamada a `vn_rdwr` u otra llamada bloqueante en la ruta crítica.

### La función strategy se llama con `bio_disk` nulo

**Síntoma.** El kernel entra en pánico en la función strategy al desreferenciar `bp->bio_disk->d_drv1`.

**Causas probables.**

- Un BIO fue construido incorrectamente por código ajeno a tu driver.
- Estás accediendo a `bp->bio_disk` desde un contexto incorrecto. En algunos caminos de GEOM, `bp->bio_disk` solo es válido dentro de la función strategy de un driver `g_disk`.

**Solución.** Si necesitas acceder al softc, hazlo al inicio de la función strategy. Guarda el puntero en una variable local. No accedas a `bp->bio_disk` desde un thread diferente ni desde un callback diferido.

### Errores de I/O misteriosos tras la recarga

**Síntoma.** Después de `kldunload` y `kldload`, las lecturas devuelven EIO en offsets que funcionaban antes de la descarga.

**Causas probables.**

- Estás trabajando con un experimento respaldado por archivo o vnode (del laboratorio 5 o de tus propias modificaciones) y el tamaño o el contenido del archivo han cambiado entre cargas.
- Una discrepancia de tipos entre los offsets guardados y los nuevos (por ejemplo, si has cambiado `d_sectorsize` entre cargas).
- `d_mediasize` ha cambiado, pero el archivo subyacente aún refleja la geometría antigua.

**Solución.** Asegúrate de que el archivo de respaldo y la geometría del driver coinciden tanto en tamaño como en disposición de sectores. Si cambias `d_mediasize` o `d_sectorsize`, regenera el archivo de respaldo para que coincida. Para una recarga sin cambios, el buffer de un driver respaldado por RAM siempre es nuevo, por lo que los EIOs misteriosos tras la recarga suelen indicar una discrepancia de geometría más que pérdida de datos.

### El contador de accesos se queda en un valor distinto de cero tras el desmontaje

**Síntoma.** Después de `umount`, `geom disk list` sigue mostrando contadores de acceso distintos de cero.

**Causas probables.**

- Algún programa aún tiene el dispositivo raw abierto. `fuser /dev/myblk0` lo revelará.
- El sistema de archivos no se desmontó correctamente. Comprueba con `mount | grep myblk` si sigue montado.
- Un cliente NFS residual o similar mantiene el sistema de archivos abierto. Es poco probable en un disco de memoria local, pero posible en sistemas compartidos.

**Solución.** Localiza y cierra el descriptor abierto. Si `umount` informa de éxito pero el contador de accesos persiste, reiniciar es la recuperación más segura.

### El driver está cargado pero no aparece en geom -t

**Síntoma.** `kldstat` muestra el módulo cargado, pero `geom -t` no muestra ninguna instancia GEOM con nuestro nombre.

**Causas probables.**

- El cargador se ejecutó, pero nunca llamó a `disk_create`.
- `disk_create` fue llamado, pero el thread de eventos aún no ha tenido oportunidad de ejecutarse.

**Solución.** Añade un `printf` para confirmar que `disk_create` se ejecutó. Espera uno o dos segundos después de `kldload` antes de comprobar, para dar oportunidad al thread de eventos.

### Panic en la segunda carga

**Síntoma.** Cargar el módulo una vez funciona. Descargarlo también. Cargarlo por segunda vez provoca un panic.

**Causas probables.**

- Un manejador `MOD_UNLOAD` no restableció todo el estado que `MOD_LOAD` asume que está limpio.
- Un puntero estático mantiene una referencia a una estructura ya liberada al cruzar el límite de descarga; la siguiente carga encuentra un puntero colgante.
- Una clase GEOM registrada en la primera carga no se desregistró.

**Solución.** Audita las rutas de carga y descarga como un par emparejado. Cada asignación en la carga necesita su correspondiente liberación en la descarga, y cada puntero escrito en la carga debe borrarse en la descarga. Para las clases GEOM, `DECLARE_GEOM_CLASS` gestiona el desregistro automáticamente; si lo omites, tendrás que hacerlo tú mismo.

### newfs_ufs aborta con "File system too small"

**Síntoma.** `newfs_ufs /dev/myblk0` aborta con `newfs: /dev/myblk0: partition smaller than minimum UFS size`.

**Causas probables.**

- `MYBLK_MEDIASIZE` es demasiado pequeño para el tamaño mínimo práctico de UFS.
- Olvidaste recompilar el módulo tras cambiar el tamaño.

**Solución.** Asegúrate de que el tamaño del medio sea de al menos unos pocos megabytes. El mínimo absoluto de UFS ronda 1 MiB, pero los mínimos prácticos son 4-8 MiB y un margen cómodo exige 32 MiB o más.

### mount -o trim no dispara BIO_DELETE

**Síntoma.** El montaje con `-o trim` tiene éxito, pero `gstat` no muestra operaciones de borrado ni siquiera durante eliminaciones intensas.

**Causas probables.**

- UFS emite `BIO_DELETE` solo en determinados patrones; no recorta incondicionalmente cada bloque liberado.
- Tu driver no anuncia el soporte de `BIO_DELETE` en sus `d_flags`.

**Solución.** Establece `sc->disk->d_flags |= DISKFLAG_CANDELETE;` antes de `disk_create`. Esto le indica a GEOM y a los sistemas de archivos que tu driver admite `BIO_DELETE` y está dispuesto a gestionarlos.

### UFS se queja de "Fragment out of bounds"

**Síntoma.** Tras el montaje, UFS registra un error sobre un fragmento fuera de rango y las operaciones de archivo comienzan a devolver EIO.

**Causas probables.**

- Tu driver devuelve datos incorrectos en algún offset y UFS ha leído un bloque de metadatos corrupto.
- El almacén de respaldo fue sobrescrito parcialmente durante alguna prueba anterior.
- La aritmética de comprobación de límites devuelve rangos incorrectos.

**Solución.** Desmonta, ejecuta `fsck_ffs -y /dev/myblk0` para reparar y vuelve a probar. Si el error reaparece con sistemas de archivos nuevos, busca errores en el cálculo de offsets en la función strategy.

### El kernel imprime mensajes de "interrupt storm"

**Síntoma.** `dmesg` muestra mensajes sobre tormentas de interrupciones y la capacidad de respuesta del sistema se degrada.

**Causas probables.**

- Un driver de hardware real (no el tuyo) está funcionando mal.
- Tu driver está bien; el problema pertenece a un subsistema diferente.

**Solución.** Verifica que la tormenta no está relacionada con tu módulo. Si lo está, el problema casi con certeza se encuentra en un manejador de interrupciones, que nuestro pseudo-driver no tiene.

### El reinicio se cuelga durante el desmontaje en el apagado

**Síntoma.** Durante el apagado, el sistema se queda bloqueado en el desmontaje, con un mensaje como "Syncing disks, vnodes remaining...".

**Causas probables.**

- Un sistema de archivos sigue montado en tu dispositivo y tu driver está reteniendo un BIO.
- Un thread de sincronización está atascado esperando la finalización.

**Solución.** Asegúrate de que tu driver se desmonta correctamente antes del apagado del sistema. Una forma robusta es añadir un manejador de eventos `shutdown_post_sync` que desmonte el sistema de archivos y descargue el módulo. Durante el desarrollo, desmonta y descarga manualmente antes de ejecutar `shutdown -r now`.

### Consejo general

Cuando algo falla, el primer paso es leer `dmesg` y buscar mensajes de tus propios printfs y de los subsistemas del kernel. El segundo paso es ejecutar `procstat -kk` y observar qué están haciendo los threads. El tercer paso es consultar `gstat`, `geom disk list` y `geom -t` para conocer la topología de almacenamiento. Estas tres herramientas te dirán la mayor parte de lo que necesitas en casi todos los casos.

Si se produce un panic, FreeBSD te lleva al depurador. Captura una traza de llamadas con `bt` y un volcado de registros con `show registers`, luego reinicia con `reboot`. Si se capturó un volcado de caída, `kgdb` sobre `/var/crash/vmcore.last` te permitirá inspeccionar el estado sin conexión. Conservar los volcados de caída, al menos en un entorno de desarrollo, compensa de inmediato cuando estás persiguiendo errores intermitentes.

Y sobre todo, cuando algo falle, intenta reproducirlo. Los bugs intermitentes en drivers de almacenamiento casi siempre están causados por diferencias de temporización en cuántas BIOs hay en vuelo, cuánto tardan y cuándo el planificador decide ejecutar tu thread. Si encuentras una reproducción fiable, ya tienes la mitad del camino hacia la solución.

## Cerrando

Este ha sido un capítulo largo. Detengámonos un momento para echar la vista atrás y repasar lo que hemos cubierto.

Empezamos situando los drivers de almacenamiento en la arquitectura en capas de FreeBSD. La capa del Virtual File System se sitúa entre las llamadas al sistema y los sistemas de archivos, dando a cada sistema de archivos una forma común. `devfs` es en sí mismo un sistema de archivos que proporciona el directorio `/dev` que las herramientas de espacio de usuario y los administradores usan para referirse a los dispositivos. Los drivers de almacenamiento no viven dentro de VFS. Viven en la parte inferior de la pila, por debajo de GEOM y del buffer cache, y se comunican con el resto del kernel a través de `struct bio`.

Construimos desde cero un driver de dispositivo de bloques pseudo funcional. En la Sección 3 escribimos el esqueleto que registraba un disco con `g_disk` y publicaba un nodo `/dev`. En la Sección 4 exploramos los conceptos de GEOM de clases, geoms, providers y consumers, y comprendimos cómo se compone la topología y cómo los contadores de acceso mantienen el sistema seguro durante el desmontaje. En la Sección 5 implementamos la función strategy que realmente atiende `BIO_READ`, `BIO_WRITE`, `BIO_DELETE` y `BIO_FLUSH` contra un almacén en memoria. En la Sección 6 formateamos el dispositivo con `newfs_ufs`, montamos un sistema de archivos real en él y vimos cómo los dos caminos de acceso (raw y sistema de archivos) convergían en nuestra función strategy. En la Sección 7 revisamos las opciones de persistencia y añadimos una técnica sencilla para sobrevivir a las recargas del módulo. En la Sección 8 recorrimos en detalle la ruta de desmontaje y aprendimos cómo probarla. En la Sección 9 exploramos las direcciones hacia las que tiende a crecer un driver: soporte multi-unidad, superficies ioctl, división en varios archivos fuente y versionado estable.

Pusimos a prueba el driver mediante laboratorios y lo ampliamos con ejercicios de desafío. Recogimos los modos de fallo más habituales en una sección de resolución de problemas. Y durante todo el camino, mantuvimos la vista puesta en el árbol de código fuente real de FreeBSD, porque el objetivo de este libro no es enseñar código de kernel de juguete, sino enseñar el código real.

Ahora deberías ser capaz de leer `md(4)` con comprensión real, no simplemente mirarlo. Deberías ser capaz de leer `g_zero.c` y reconocer cada función que llama. Deberías ser capaz de diagnosticar las clases habituales de errores en drivers de almacenamiento a partir de sus síntomas. Y deberías tener un pseudo dispositivo de bloques funcional, aunque sencillo, que tú mismo escribiste.

Es una cantidad considerable de terreno cubierto. Tómate un momento para notar hasta dónde has llegado. En el Capítulo 26 sabías cómo escribir un driver de caracteres. Ahora también puedes escribir un driver de bloques. Los dos capítulos juntos te dan la base para casi cualquier otro tipo de driver en FreeBSD, porque la mayoría de los drivers son de orientación carácter o de orientación bloque en el límite donde se encuentran con el resto del kernel.

### Resumen de los pasos clave

Para recordarlos rápidamente, aquí están los pasos que definen un driver de almacenamiento mínimo.

1. Incluye las cabeceras correctas: `sys/bio.h`, `geom/geom.h`, `geom/geom_disk.h`.
2. Asigna un `struct disk` con `disk_alloc`.
3. Rellena `d_name`, `d_unit`, `d_strategy`, `d_sectorsize`, `d_mediasize`, `d_maxsize` y `d_drv1`.
4. Llama a `disk_create(sc->disk, DISK_VERSION)`.
5. En `d_strategy`, haz un switch sobre `bio_cmd` y atiende la petición. Llama siempre a `biodone` exactamente una vez.
6. En la ruta de descarga, llama a `disk_destroy` antes de liberar cualquier cosa que toque la función strategy.
7. Declara `MODULE_DEPEND` sobre `g_disk`.
8. Usa `MAXPHYS` para `d_maxsize` a menos que tengas una razón concreta para usar un valor menor.
9. Prueba la ruta de descarga bajo carga. Pruébala con un sistema de archivos montado. Pruébala con un `cat` raw que mantenga el dispositivo abierto.
10. Lee `dmesg`, `gstat`, `geom disk list` y `procstat -kk` cuando algo vaya mal.

Estos diez pasos son el esqueleto de todo driver de almacenamiento de FreeBSD que escribirás alguna vez. Aparecen con distinta forma en `ada(4)`, en `nvme(4)`, en `mmcsd(4)`, en `zvol(4)` y en cualquier otro driver del árbol. Una vez que ves el patrón, la variedad entre los drivers reales resulta mucho menos misteriosa.

### Sobre el acceso raw

Aunque tengas un sistema de archivos montado, tu driver sigue siendo accesible como dispositivo de bloques raw. `/dev/myblk0` sigue siendo un identificador válido que herramientas como `dd`, `diskinfo`, `gstat` y `dtrace` pueden utilizar. Los dos caminos de acceso coexisten gracias a la disciplina de GEOM: ambos emiten BIOs, ambos respetan los contadores de acceso, y tu función strategy atiende a ambos sin distinguir entre ellos. Esa uniformidad es el gran regalo de GEOM a quienes escriben drivers de almacenamiento.

### Sobre la seguridad

Trabajar en un sistema compartido mientras desarrollas un driver de almacenamiento es una invitación al desastre. Usa una máquina virtual o, como mínimo, un sistema que puedas reinstalar. Ten a mano una imagen de rescate. Haz copias de seguridad de todo lo que no puedas permitirte perder, incluido el código que estás escribiendo. El driver de este capítulo se comporta correctamente y no debería dañar nada, pero los drivers que escribas en el futuro puede que no, y el coste de estar preparado es muy pequeño comparado con el coste de no estarlo.

### Qué explorar a continuación en el árbol de FreeBSD

Si quieres seguir explorando el almacenamiento antes del próximo capítulo, hay tres áreas del árbol que merecen una lectura cuidadosa.

- `/usr/src/sys/geom/` contiene el propio framework GEOM, incluyendo `g_class`, `g_disk` y muchas clases de transformación como `g_mirror`, `g_stripe` y `g_eli`.
- `/usr/src/sys/dev/md/md.c` es el driver de disco en memoria completo, ya mencionado muchas veces en este capítulo.
- `/usr/src/sys/ufs/` es el sistema de archivos UFS. No es una lectura obligatoria para el trabajo con drivers, pero ayuda ver la capa inmediatamente superior a la tuya.

Leerlos no es un requisito previo para el próximo capítulo. Es una recomendación para tu propio crecimiento.

## Puente hacia el próximo capítulo

En este capítulo construimos un driver de almacenamiento desde cero. Los datos que fluían por él eran internos al sistema: bytes escritos en un archivo, bytes leídos de un archivo, superblocks y grupos de cilindros e inodes moviéndose por el buffer cache. Ningún byte abandonó nunca la máquina. El mundo entero del driver era la propia memoria del kernel y los procesos que la consumen.

El Capítulo 28 nos lleva a un mundo diferente. Escribiremos un driver de interfaz de red. Los drivers de red son drivers de transporte como el driver serie USB del Capítulo 26 y el driver de almacenamiento de este capítulo, pero su interlocutor no es un proceso ni un sistema de archivos. Es una pila de red, y la unidad de trabajo no es un rango de bytes ni un bloque, sino un paquete. El paquete es un objeto estructurado con cabeceras y carga útil, y el driver participa en una pila que incluye IP, ARP, ICMP, TCP, UDP y muchos otros protocolos.

Los patrones que has interiorizado en este capítulo volverán a aparecer, con nombres diferentes. En lugar de `struct bio`, verás `struct mbuf`. En lugar de `g_disk`, verás la interfaz `ifnet`. En lugar de `disk_strategy`, verás los hooks `if_transmit` e `if_input`. En lugar de providers y consumers de GEOM, verás objetos de interfaz de red enlazados en la pila de red del kernel. El papel es el mismo: un driver de transporte toma peticiones de arriba, las entrega abajo, acepta respuestas de abajo y las entrega arriba.

Muchas de las preocupaciones también serán las mismas. Locking. Hot unplug. Limpieza de recursos en detach. Observabilidad a través de herramientas del kernel. Seguridad frente a errores. Los fundamentos se mantienen. Lo que cambia es el vocabulario, la estructura de la unidad de trabajo y algunas de las herramientas concretas.

Antes de continuar, tómate un breve descanso. Descarga tu driver de almacenamiento. Ejecuta `kldstat` y confirma que nada de este capítulo sigue cargado. Cierra tu cuaderno de laboratorio. Levántate. Rellena tu café. El próximo capítulo va a ser igual de sustancioso que este, y vas a querer la cabeza despejada.

Cuando vuelvas, el Capítulo 28 empezará de la misma manera que este: con una introducción suave y una imagen clara de hacia dónde vamos. Nos vemos allí.

## Referencia rápida

Las tablas siguientes están pensadas para una consulta rápida cuando estés escribiendo o depurando un driver de almacenamiento y necesites recordar un nombre, un comando o una ruta. No son un sustituto de las explicaciones completas que aparecen antes en el capítulo.

### Cabeceras principales

| Cabecera | Define |
|--------|---------|
| `sys/bio.h` | `struct bio`, `BIO_READ`, `BIO_WRITE`, `BIO_DELETE`, `BIO_FLUSH`, `BIO_GETATTR` |
| `geom/geom.h` | `struct g_class`, `struct g_geom`, `struct g_provider`, `struct g_consumer`, primitivas de topología |
| `geom/geom_disk.h` | `struct disk`, `DISK_VERSION`, `disk_alloc`, `disk_create`, `disk_destroy`, `disk_gone` |
| `sys/module.h` | `DECLARE_MODULE`, `MODULE_VERSION`, `MODULE_DEPEND` |
| `sys/malloc.h` | `MALLOC_DEFINE`, `malloc`, `free`, `M_WAITOK`, `M_NOWAIT`, `M_ZERO` |
| `sys/lock.h`, `sys/mutex.h` | `struct mtx`, `mtx_init`, `mtx_lock`, `mtx_unlock`, `mtx_destroy` |

### Estructuras principales

| Estructura | Función |
|-----------|------|
| `struct disk` | La representación `g_disk` de un disco. La rellena el driver; es propiedad del framework. |
| `struct bio` | Una petición de I/O que pasa entre las capas de GEOM y llega a la función strategy del driver. |
| `struct g_provider` | La interfaz orientada al productor de un geom. Los sistemas de archivos y otros geoms consumen de los providers. |
| `struct g_consumer` | La conexión desde un geom hacia el provider de otro geom. |
| `struct g_geom` | Una instancia de una `g_class`. |
| `struct g_class` | La plantilla a partir de la cual se crean los geoms. Define métodos como `init`, `fini`, `start`, `access`. |

### Comandos BIO más habituales

| Comando | Significado |
|---------|---------|
| `BIO_READ` | Leer bytes del dispositivo en un buffer. |
| `BIO_WRITE` | Escribir bytes desde un buffer en el dispositivo. |
| `BIO_DELETE` | Descartar un rango de bloques. Se usa para TRIM. |
| `BIO_FLUSH` | Confirmar las escrituras pendientes en el almacenamiento duradero. |
| `BIO_GETATTR` | Consultar un atributo con nombre del provider. |
| `BIO_ZONE` | Operaciones de dispositivo de bloques zonados. No se usa habitualmente. |

### Herramientas GEOM más habituales

| Herramienta | Propósito |
|------|---------|
| `geom disk list` | Listar los discos registrados y sus providers. |
| `geom -t` | Mostrar toda la topología de GEOM en forma de árbol. |
| `geom part show` | Mostrar los geoms de partición y sus providers. |
| `gstat` | Estadísticas de I/O en vivo por provider. |
| `diskinfo -v /dev/xxx` | Mostrar la geometría y los atributos del disco. |
| `iostat -x 1` | Rendimiento y latencia en vivo por dispositivo. |
| `dd if=... of=...` | I/O de bloques raw para pruebas. |
| `newfs_ufs /dev/xxx` | Crear un sistema de archivos UFS en un dispositivo. |
| `mount /dev/xxx /mnt` | Montar un sistema de archivos. |
| `umount /mnt` | Desmontar un sistema de archivos. |
| `mdconfig` | Crear o destruir discos en memoria. |
| `fuser` | Encontrar procesos que tienen un archivo abierto. |
| `procstat -kk` | Mostrar las trazas de pila del kernel para todos los threads. |

### Typedefs de callback principales

| Typedef | Propósito |
|---------|---------|
| `disk_strategy_t` | Gestiona los BIOs. La función de I/O principal. Obligatorio. |
| `disk_open_t` | Se llama cuando se concede un nuevo acceso. Opcional. |
| `disk_close_t` | Se llama cuando se libera un acceso. Opcional. |
| `disk_ioctl_t` | Gestiona los ioctls en el nodo `/dev`. Opcional. |
| `disk_getattr_t` | Responde a las consultas `BIO_GETATTR`. Opcional. |
| `disk_gone_t` | Notifica al driver cuando el disco está siendo retirado forzosamente. Opcional. |

### Referencia de archivos y rutas

| Ruta | Contenido |
|------|------------------|
| `/usr/src/sys/geom/geom_disk.c` | La implementación de `g_disk`. |
| `/usr/src/sys/geom/geom_disk.h` | La interfaz pública de `g_disk`. |
| `/usr/src/sys/geom/geom.h` | Estructuras y funciones principales de GEOM. |
| `/usr/src/sys/sys/bio.h` | La definición de `struct bio`. |
| `/usr/src/sys/dev/md/md.c` | El driver de referencia de disco en memoria. |
| `/usr/src/sys/geom/zero/g_zero.c` | Una clase GEOM mínima, útil como referencia de lectura. |
| `/usr/src/sys/ufs/ffs/ffs_vfsops.c` | La ruta de montaje de UFS. Léela si quieres ver qué hace mount en el lado del sistema de archivos. |
| `/usr/src/share/man/man9/disk.9` | La página de manual de `disk(9)`. |
| `/usr/src/share/man/man9/g_bio.9` | La página de manual de `g_bio(9)`. |

### Flags de disco más habituales

| Flag | Significado |
|------|---------|
| `DISKFLAG_CANDELETE` | El driver gestiona `BIO_DELETE`. |
| `DISKFLAG_CANFLUSHCACHE` | El driver gestiona `BIO_FLUSH`. |
| `DISKFLAG_UNMAPPED_BIO` | El driver acepta BIOs no mapeados (avanzado). |
| `DISKFLAG_WRITE_PROTECT` | El dispositivo es de solo lectura. |
| `DISKFLAG_DIRECT_COMPLETION` | La finalización es segura desde cualquier contexto (avanzado). |

Estos flags se establecen en `sc->disk->d_flags` antes de llamar a `disk_create`. Permiten al kernel tomar decisiones más inteligentes sobre cómo emitir BIOs a tu driver.

### Patrones de d_strategy

Estos son los tres patrones más comunes de una función de strategy.

**Patrón 1: Síncrono, en memoria.** Nuestro driver usa este patrón. La función sirve el BIO en línea y retorna tras llamar a `biodone`.

```c
void strategy(struct bio *bp) {
    /* validate */
    switch (bp->bio_cmd) {
    case BIO_READ:  memcpy(bp->bio_data, ...); break;
    case BIO_WRITE: memcpy(..., bp->bio_data); break;
    }
    bp->bio_resid = 0;
    biodone(bp);
}
```

**Patrón 2: Encolar en un thread de trabajo.** `md(4)` usa este patrón. La función añade el BIO a una cola y señala al worker.

```c
void strategy(struct bio *bp) {
    mtx_lock(&sc->lock);
    TAILQ_INSERT_TAIL(&sc->queue, bp, bio_queue);
    wakeup(&sc->queue);
    mtx_unlock(&sc->lock);
}
```

El worker extrae BIOs de la cola, los atiende de uno en uno (quizás llamando a `vn_rdwr` o enviando comandos al hardware) y completa cada uno con `biodone`.

**Patrón 3: DMA de hardware con finalización por interrupción.** Los drivers de hardware real usan este patrón. La función programa el hardware, configura el DMA y retorna. Un manejador de interrupción posterior completa el BIO.

```c
void strategy(struct bio *bp) {
    /* validate */
    program_hardware(bp);
    /* strategy returns, interrupt will call biodone eventually */
}
```

Cada patrón tiene sus compromisos. El patrón 1 es el más sencillo, pero no puede bloquearse. El patrón 2 gestiona trabajo bloqueante, pero añade latencia. El patrón 3 es necesario para hardware real, pero requiere gestión de interrupciones, lo que añade toda una capa adicional de complejidad.

El driver de este capítulo usa el patrón 1. `md(4)` usa el patrón 2. `ada(4)`, `nvme(4)` y similares usan el patrón 3.

### Secuencia mínima de registro

Para quien escribe un driver con prisa, la secuencia mínima para registrar un dispositivo de bloques es:

```c
sc->disk = disk_alloc();
sc->disk->d_name       = "myblk";
sc->disk->d_unit       = sc->unit;
sc->disk->d_strategy   = myblk_strategy;
sc->disk->d_sectorsize = 512;
sc->disk->d_mediasize  = size_in_bytes;
sc->disk->d_maxsize    = MAXPHYS;
sc->disk->d_drv1       = sc;
disk_create(sc->disk, DISK_VERSION);
```

Y la secuencia mínima de desmontaje es:

```c
disk_destroy(sc->disk);
free(sc->backing, M_MYBLK);
mtx_destroy(&sc->lock);
free(sc, M_MYBLK);
```

## Glosario

**Contador de acceso.** Una tupla de tres contadores en un provider de GEOM que registra cuántos lectores, escritores y titulares exclusivos tienen acceso en ese momento. Se muestra como `rNwNeN` en `geom disk list`.

**Attach.** En el sentido de Newbus, el paso en que un driver asume la responsabilidad de un dispositivo. En el sentido del almacenamiento, el paso en que el driver llama a `disk_create` para registrarse con `g_disk`. La palabra se usa con dos significados distintos; el contexto aclara cuál es.

**Backing store.** El lugar donde residen físicamente los bytes de un dispositivo de almacenamiento. En nuestro driver, el backing store es un buffer reservado con `malloc` en memoria del kernel. En discos reales, es el plato o la memoria flash. En `md(4)` en modo vnode, es un archivo en el sistema de archivos del host.

**BIO.** Una `struct bio`. La unidad de I/O que fluye por GEOM.

**BIO_DELETE.** Un comando BIO que pide al driver que descarte un rango de bloques. Se usa para TRIM en SSDs.

**BIO_FLUSH.** Un comando BIO que pide al driver que haga duraderas todas las escrituras anteriores antes de retornar.

**BIO_GETATTR.** Un comando BIO que pide al driver que devuelva el valor de un atributo con nombre.

**BIO_READ.** Un comando BIO que pide al driver que lea un rango de bytes.

**BIO_WRITE.** Un comando BIO que pide al driver que escriba un rango de bytes.

**Dispositivo de bloques.** Un dispositivo que se direcciona en bloques de tamaño fijo, con acceso aleatorio y posicionamiento. Históricamente distinto de los dispositivos de caracteres en BSD; en el FreeBSD moderno, el acceso de bloque y de carácter convergen a través de GEOM, aunque la distinción conceptual sigue siendo relevante.

**Buffer cache.** El subsistema del kernel que mantiene en RAM los bloques del sistema de archivos usados recientemente. Se sitúa entre los sistemas de archivos y GEOM. No debe confundirse con la caché de páginas; están relacionadas, pero son distintas en FreeBSD.

**Coherencia de caché.** La propiedad que garantiza que las lecturas y escrituras se ven entre sí en un orden consistente. La función de strategy no debe devolver datos obsoletos respecto a escrituras recientes en el mismo desplazamiento.

**Cdev.** Un nodo de dispositivo de caracteres representado por `struct cdev`. Los drivers de caracteres los crean con `make_dev`. Los drivers de bloques normalmente no lo hacen.

**Consumer.** El lado de entrada de un geom. Un consumer se conecta a un provider y emite BIOs hacia él.

**d_drv1.** Un puntero genérico en `struct disk` donde el driver almacena su contexto privado, normalmente el softc.

**d_mediasize.** El tamaño total del dispositivo en bytes.

**d_maxsize.** El BIO más grande que el driver puede aceptar de una sola vez. Normalmente `MAXPHYS` para pseudo dispositivos.

**d_sectorsize.** El tamaño de un sector en bytes. Normalmente 512 o 4096.

**d_strategy.** La función del driver que gestiona los BIOs.

**Devfs.** Un pseudosistema de archivos montado en `/dev` que sintetiza nodos de archivo para los dispositivos del kernel.

**Devstat.** El subsistema de estadísticas de dispositivos del kernel, usado por `iostat`, `gstat` y otras herramientas. Los drivers de almacenamiento que usan `g_disk` obtienen la integración con devstat de forma automática.

**Disk_alloc.** Asigna una `struct disk`. Nunca falla; usa `M_WAITOK` internamente.

**Disk_create.** Registra una `struct disk` ya rellenada en `g_disk`. El trabajo real se realiza de forma asíncrona.

**Disk_destroy.** Desregistra y destruye una `struct disk`. Espera a que los BIOs en vuelo terminen. Genera un panic si el provider todavía tiene usuarios.

**Disk_gone.** Notifica a `g_disk` que el medio subyacente ha desaparecido. Se usa en escenarios de desconexión en caliente. Es distinto de `disk_destroy`.

**DISK_VERSION.** La versión ABI de la interfaz `struct disk`. Se define en `geom_disk.h` y se pasa a `disk_create`.

**DTrace.** La herramienta de trazado dinámico de FreeBSD. Especialmente útil para observar el tráfico de BIOs.

**Thread de eventos.** El único thread del kernel que GEOM usa para procesar eventos de topología, como la creación y destrucción de geoms. Suele aparecer como `g_event` en la salida de `procstat`.

**Acceso exclusivo.** Un tipo de acceso sobre un provider que impide que otros escritores accedan a él. Los sistemas de archivos adquieren acceso exclusivo sobre los dispositivos que montan.

**Sistema de archivos.** Una implementación concreta de semántica de almacenamiento de archivos, como UFS, ZFS, tmpfs o NFS. Se integra en VFS.

**GEOM.** El framework de FreeBSD para transformaciones componibles en la capa de bloques. Sus objetos principales son las clases, los geoms, los providers y los consumers.

**g_disk.** El subsistema de GEOM que envuelve los drivers con forma de disco en una API más sencilla. Nuestro driver lo usa.

**g_event.** El thread de eventos de GEOM que procesa los cambios de topología.

**g_io_deliver.** La función usada a nivel de clase para completar un BIO. `g_disk` la llama por nosotros; nuestro driver llama a `biodone`.

**g_io_request.** La función usada a nivel de clase para emitir un BIO hacia abajo. Solo se usa en drivers que implementan su propia clase GEOM.

**Hotplug.** Un dispositivo que puede aparecer o desaparecer sin necesidad de reiniciar el sistema.

**Ioctl.** Una operación de control sobre un dispositivo, distinta de la lectura o escritura. En la ruta de almacenamiento, los ioctls sobre `/dev/diskN` pasan por GEOM y pueden ser gestionados por `g_disk` o por el `d_ioctl` del driver.

**md(4).** El driver de disco en memoria de FreeBSD. El pseudo dispositivo de bloques canónico del árbol de código fuente, y una referencia de lectura recomendada.

**Mount.** El acto de adjuntar un sistema de archivos a un punto del espacio de nombres. Llama a VFS, que a su vez llama a la rutina de montaje propia del sistema de archivos, la cual típicamente abre un provider de GEOM.

**Newbus.** El framework de bus de FreeBSD. Se usa para drivers de caracteres y de hardware. Nuestro driver de almacenamiento no usa Newbus directamente porque es un pseudo dispositivo; los drivers de almacenamiento de hardware real casi siempre lo hacen.

**Provider.** El lado de salida de un geom. Otros geoms o nodos de `/dev` consumen providers.

**Softc.** La estructura de estado por instancia de un driver.

**Función de strategy.** El gestor de BIOs del driver. Se llama `d_strategy` en la API de `struct disk`.

**Superbloque.** Una pequeña estructura en disco que describe la disposición de un sistema de archivos. El de UFS está en el desplazamiento 65536.

**Topología.** El árbol de clases, geoms, providers y consumers de GEOM. Protegido por el topology lock.

**Topology lock.** El lock global que protege la topología de GEOM frente a modificaciones concurrentes.

**UFS.** El Unix File System, el sistema de archivos predeterminado de FreeBSD. Se encuentra bajo `/usr/src/sys/ufs/`.

**Unidad.** Una instancia numerada de un driver. `myblk0` es la unidad 0 del driver `myblk`.

**VFS.** La capa de sistema de archivos virtual. Se sitúa entre las llamadas al sistema y los sistemas de archivos concretos.

**Vnode.** El manejador en tiempo de ejecución del kernel para un archivo o directorio abierto. Reside dentro de VFS.

**Withering.** El proceso de GEOM para eliminar un provider de la topología. Se encola en el thread de eventos, espera a que los BIOs en vuelo terminen y finalmente destruye el provider.

**Zona.** En el vocabulario del subsistema de memoria virtual, un pool de objetos de tamaño fijo asignados a través de UMA (Universal Memory Allocator). Muchas estructuras del kernel, incluidos los BIOs y los providers de GEOM, se asignan desde zonas.

**BIO_ORDERED.** Una flag de BIO que pide al driver que ejecute este BIO solo después de que todos los BIOs emitidos anteriormente hayan completado. Se usa para barreras de escritura.

**BIO_UNMAPPED.** Una flag de BIO que indica que `bio_data` no es una dirección virtual del kernel mapeada, sino una lista de páginas no mapeadas. Los drivers que pueden gestionar datos no mapeados deben establecer `DISKFLAG_UNMAPPED_BIO`.

**Completado directo.** Completar un BIO en el mismo thread que lo envió, sin pasar por un callback diferido. Normalmente más rápido, pero no siempre seguro.

**Drivers integrados en el árbol.** Drivers que residen dentro del árbol de código fuente de FreeBSD y se construyen como parte del build estándar del kernel. Se contraponen a los drivers fuera del árbol, que se mantienen de forma independiente.

**Driver fuera del árbol.** Un driver que no forma parte del árbol de código fuente de FreeBSD. Estos drivers deben compilarse contra un kernel coincidente y pueden necesitar actualizaciones cuando cambia el ABI del kernel.

**ABI.** Application Binary Interface. El conjunto de convenciones para las llamadas a funciones, la disposición de estructuras y el tamaño de los tipos que permiten que dos fragmentos de código compilado interoperen. `DISK_VERSION` es un marcador de ABI.

**API.** Application Programming Interface. El conjunto de firmas de funciones y tipos que el código usa a nivel de código fuente. Distinto del ABI: dos kernels con la misma API pueden tener ABIs distintos si se compilaron de forma diferente.

**KPI.** Kernel Programming Interface. El término preferido de FreeBSD para la API del kernel. Las garantías sobre la estabilidad del KPI son limitadas; recompila siempre contra el kernel que estás ejecutando.

**KLD.** Módulo del kernel cargable. El archivo `.ko` que producimos. "KLD" son las siglas de Kernel Loadable Driver, aunque los módulos no son necesariamente drivers.

**Módulo.** Véase KLD.

**Taste.** En el vocabulario de GEOM, el proceso de ofrecer un provider a todas las clases para que cada una decida si se conecta a él. Este proceso ocurre automáticamente cuando aparecen nuevos providers.

**Retaste.** Forzar a GEOM a volver a hacer el taste de un provider, normalmente después de que su contenido haya cambiado. `geom provider retaste` lo activa para un provider concreto; `geom retaste` lo activa globalmente.

**Orphan.** En el vocabulario de GEOM, un provider cuyo almacenamiento subyacente ha desaparecido. Los orphans son limpiados por el thread de eventos.

**Spoil.** Un concepto de GEOM relacionado con la invalidación de cachés. Si el contenido de un provider cambia de un modo que podría invalidar cachés, se dice que ha sido spoiled.

**Bufobj.** Un objeto del kernel que asocia un vnode (o un consumer de GEOM) con un buffer cache. Cada dispositivo de bloques y cada archivo tiene uno.

**bdev_strategy.** Un sinónimo heredado de `d_strategy`. El código moderno usa `d_strategy` directamente.

**Planificación.** El acto de colocar un BIO en una cola interna para su ejecución posterior. Distinto de "ejecutar".

**Plug/unplug.** En algunos kernels, el plug es un mecanismo de agrupación por lotes para la entrega de BIOs. FreeBSD no tiene plug/unplug; entrega los BIOs de forma inmediata.

**Elevator.** Un planificador de BIOs que los ordena por desplazamiento para reducir el tiempo de búsqueda en discos con cabezal mecánico. GEOM de FreeBSD no implementa un elevator en la capa GEOM; es responsabilidad del dispositivo de bloques, cuando resulta relevante.

**Superbloque.** El primer bloque de metadatos de un sistema de archivos. Describe la geometría. En UFS está en el desplazamiento 65536.

**Grupo de cilindros.** Un concepto de UFS. El sistema de archivos se divide en regiones, cada una con su propia tabla de inodos y mapa de asignación de bloques. Mantiene los datos relacionados físicamente cerca en un disco giratorio y limita el daño que una sola región defectuosa puede causar.

**Inode.** Una estructura de UFS (y POSIX) que describe un archivo: su modo, propietario, tamaño, marcas de tiempo y punteros a bloques de datos. Los nombres de archivo residen en las entradas de directorio, no en los inodos.

**Vop_vector.** La tabla de despacho que un sistema de archivos proporciona a VFS, con todas las operaciones que VFS sabe cómo solicitar (open, close, read, write, lookup, rename, etc.). VFS las invoca como punteros a función indirectos.

**Devstat.** Una estructura del kernel asociada a dispositivos que registra estadísticas de I/O agregadas. `iostat(8)` lee los datos de devstat; `g_disk` asigna y alimenta una estructura devstat para cada disco que crea.

**bp.** Abreviatura en el código fuente del kernel para un puntero BIO. Se utiliza de forma casi universal en las funciones strategy y en los callbacks de finalización. Cuando veas `struct bio *bp`, léelo como "la solicitud actual".

**Bread.** Función del buffer-cache que lee un bloque consultando primero la caché y emitiendo I/O únicamente cuando no hay coincidencia. La utilizan los sistemas de archivos, no los drivers.

**Bwrite.** Función del buffer-cache que escribe un bloque de forma síncrona. El sistema de archivos la utiliza; tu función strategy acaba viendo el BIO resultante.

**Getblk.** Función del buffer-cache que devuelve un buffer para un bloque dado, asignándolo si fuera necesario. Los sistemas de archivos la utilizan como punto de entrada tanto para la lectura como para la escritura.

**Bdwrite.** Escritura diferida en el buffer-cache. Marca el buffer como sucio pero no emite I/O de inmediato. La escritura se producirá más adelante, bien por obra del syncer o bien por la presión del buffer-cache.

**Bawrite.** Escritura asíncrona en el buffer-cache. Similar a `bwrite`, pero no espera a que la operación concluya.

**Syncer.** Un thread del kernel que vuelca periódicamente los buffers sucios en sus dispositivos subyacentes. Para cerrar un sistema de archivos de forma limpia es necesario que el syncer termine su trabajo.

**Taskqueue.** Una facilidad del kernel para ejecutar callbacks en un thread separado. Resulta útil cuando tu función strategy quiere diferir trabajo. Se trata con más detalle cuando hablamos de los manejadores de interrupciones en capítulos posteriores.

**Callout.** Una facilidad del kernel para programar un callback puntual o periódico en un momento determinado. No es habitual en drivers de almacenamiento sencillos, pero sí muy común en drivers de hardware que implementan tiempos de espera.

**Witness.** Un subsistema del kernel que detecta violaciones del orden de adquisición de locks e imprime advertencias. Siempre está habilitado en kernels de depuración; ahorra horas de trabajo de debug.

**INVARIANTS.** Una opción de compilación del kernel que añade aserciones en tiempo de ejecución. Siempre está habilitada en kernels de depuración; detecta muchos bugs de almacenamiento antes de que se conviertan en corrupción silenciosa.

**Debug kernel.** Un kernel compilado con `INVARIANTS`, `WITNESS` y opciones relacionadas. Más lento, pero mucho más seguro para el desarrollo de drivers. Úsalo durante el trabajo de laboratorio.

## Preguntas frecuentes

### ¿Necesito implementar soporte para BIO_ORDERED?

Para un pseudo dispositivo que atiende BIOs de forma síncrona, no. Cada BIO se completa antes de que se procese el siguiente, lo que preserva el orden de forma trivial. Para un driver asíncrono, debes respetar `BIO_ORDERED` retrasando los BIOs posteriores hasta que el ordenado se complete.

### ¿Cuál es la relación entre d_maxsize y MAXPHYS?

`d_maxsize` es el tamaño máximo de BIO que tu driver puede aceptar. `MAXPHYS` es el límite superior en tiempo de compilación para el tamaño de BIO, definido en `/usr/src/sys/sys/param.h`. En sistemas de 64 bits como amd64 y arm64, `MAXPHYS` vale 1 MiB; en sistemas de 32 bits vale 128 KiB. FreeBSD 14.3 también expone un parámetro ajustable en tiempo de ejecución, `maxphys`, que algunos subsistemas consultan a través de la macro `MAXPHYS` o la variable `maxphys`. Asignar `d_maxsize = MAXPHYS` acepta cualquier tamaño que el kernel esté dispuesto a emitir. Para la mayoría de los pseudo drivers, esto es suficiente.

### ¿Puede mi driver emitir BIOs a sí mismo?

Técnicamente sí, pero rara vez tiene sentido. Este patrón lo utilizan las clases de transformación de GEOM (reciben BIOs desde arriba y emiten nuevos BIOs hacia abajo). Un driver `g_disk` está en la parte inferior de la pila y no tiene capa inferior; si necesitas dividir el trabajo entre varias unidades de respaldo, probablemente necesites worker threads en lugar de BIOs anidados.

### ¿Por qué algunos campos de struct disk usan u_int y otros off_t?

`u_int` se usa para tamaños enteros sin signo que caben en 32 bits (tamaño de sector, número de cabezales, etc.). `off_t` es un tipo con signo de 64 bits que se usa para desplazamientos y tamaños en bytes que pueden superar los 32 bits (tamaño del medio, desplazamientos de solicitud). La distinción importa en discos grandes; un tamaño de medio de 10 TB requiere más de 32 bits.

### ¿Es seguro llamar a disk_alloc en cualquier momento?

`disk_alloc` usa `M_WAITOK` y puede dormir si la memoria escasea. No la llames mientras mantengas un spinlock o un mutex que no puedas liberar. Llámala en el momento del attach, fuera de cualquier lock.

### ¿Qué ocurre si llamo a disk_create dos veces con el mismo nombre?

`disk_create` creará múltiples discos con el mismo nombre sin problemas si los números de unidad difieren. Si coinciden tanto el nombre como el número de unidad, GEOM rechazará el segundo registro y el comportamiento resultante depende de la implementación. Evita este caso.

### ¿Puede dormir la función strategy?

Técnicamente sí, pero no debería. La función strategy se ejecuta en el contexto del thread del llamador, y dormir en ese punto bloquea al llamador. Para el trabajo que deba bloquearse, usa un worker thread.

### ¿Cómo sé cuándo han terminado todos los BIOs de un sistema de archivos determinado?

Normalmente no necesitas saberlo. `umount(2)` se encarga de ello: vacía los buffers sucios, drena los BIOs en vuelo y retorna solo cuando el sistema de archivos está completamente inactivo. Una vez que `umount` retorna, no llegará ningún BIO a ese punto de montaje a menos que algo más abra el dispositivo.

### ¿Puedo pasar punteros entre threads mediante bio_caller1 o campos similares?

Sí. `bio_caller1` y `bio_caller2` son campos opacos pensados para que el emisor del BIO guarde contexto que el manejador de finalización pueda usar. Mientras seas el propietario del BIO (que lo eres, ya que tú lo emitiste), esos campos son tuyos. Los drivers `g_disk` normalmente no los necesitan porque el BIO llega desde arriba y se completa llamando a `biodone`, siendo `g_disk` quien gestiona el enrutamiento del callback.

### Mi driver funciona en mi portátil pero no en el servidor. ¿Por qué?

Posibilidades: ABI del kernel diferente (recompila contra el kernel del servidor), `MAXPHYS` diferente (debería ser idéntico en sistemas 14.3, pero compruébalo), clases de GEOM distintas cargadas (poco probable pero posible), tamaño de memoria diferente (tu asignación podría fallar en un sistema con menos memoria), velocidad de reloj diferente (afecta a la temporización). Empieza comparando `uname -a` y `sysctl -a | grep kern.maxphys`.

### ¿De dónde viene realmente el nombre del nodo /dev?

De `d_name` y `d_unit` en el `struct disk` que pasas a `disk_create`. GEOM los concatena sin separador: `d_name = "myblk"`, `d_unit = 0` produce `/dev/myblk0`. Si quieres una convención diferente, establece `d_name` en consecuencia. No hay ningún carácter separador entre el nombre y el número de unidad.

### ¿Cuál es el número máximo de unidades que puedo crear?

Está limitado por `d_unit`, que es `u_int`, así que 2^32 - 1 en teoría. En la práctica, el consumo de memoria por unidad y los límites prácticos del espacio de nombres de `/dev` te detendrán mucho antes.

### ¿Puedo cambiar d_mediasize después de disk_create?

Sí, pero con cuidado. Los sistemas de archivos montados en el disco no adoptarán el cambio automáticamente; la mayoría requerirá desmontar y volver a montar. `md(4)` admite `MDIOCRESIZE` y existe infraestructura para notificar el cambio a GEOM, pero el patrón no es trivial.

### ¿Qué ocurre si olvido MODULE_DEPEND?

El kernel puede fallar al cargar tu módulo si `g_disk` no está ya cargado, o puede cargarlo correctamente si `g_disk` resulta estar compilado en el kernel. Declara siempre `MODULE_DEPEND` explícitamente para evitar sorpresas.

### ¿Debo usar biodone o g_io_deliver en mi driver?

Usa `biodone`. El wrapper `g_disk` proporciona una interfaz de estilo `d_strategy` donde la llamada de finalización correcta es `biodone`. Si escribes tu propia `g_class`, llamarás a `g_io_deliver` en su lugar, pero eso es un camino diferente que merece un capítulo entero de complejidad.

### ¿Cómo se relaciona BIO_DELETE con TRIM y UNMAP?

`BIO_DELETE` es la abstracción dentro del kernel. Para los SSD SATA se corresponde con el comando ATA TRIM, para SCSI/SAS con UNMAP, y para NVMe con Dataset Management con el bit de desasignación. El espacio de usuario lo activa a través de `fstrim(8)` o la opción de montaje `-o trim` en UFS. Nuestro driver puede tratarlo como una sugerencia u honrarlo poniendo a cero la memoria, ya que el respaldo está en RAM.

### ¿Por qué mi función strategy recibe a veces un BIO con bio_length igual a cero?

En condiciones normales nunca deberías ver esto. Si ocurre, trátalo como un caso defensivo: llama a `biodone(bp)` sin error y retorna. Un BIO de longitud cero no es ilegal, pero indica algo anómalo aguas arriba. Presentar un PR contra el código que lo emite es razonable.

### ¿Cuál es la diferencia entre d_flags y bio_flags?

`d_flags` es la configuración estática para el disco completo, establecida una sola vez en el registro y que describe lo que el driver puede hacer (gestiona DELETE, puede hacer FLUSH, acepta BIOs no mapeados, etc.). `bio_flags` es metadatos dinámicos en un BIO individual, que cambia por solicitud (ordenado, no mapeado, finalización directa). No los confundas.

### ¿Puede mi driver presentarse como medio extraíble?

Sí, establece `DISKFLAG_CANDELETE` y considera honrar `disk_gone` para simular la expulsión. Herramientas como `camcontrol` y los manejadores de sistemas de archivos generalmente tratan a cualquier proveedor de GEOM de forma uniforme, por lo que «extraíble» en el sentido visible para el usuario es menos diferenciado que en otros sistemas operativos.

### ¿Qué thread llama realmente a mi función strategy?

Depende. Para envíos síncronos desde el buffer cache, es el thread que llamó a `bwrite` o `bread`. Para las rutas de finalización asíncrona, suele ser un worker de GEOM o el thread de vaciado del buffer cache. Tu función strategy debe estar escrita para tolerar cualquier llamador. No asumas una identidad de thread específica ni una prioridad específica.

### ¿Cómo sé qué proceso causó un BIO determinado?

Normalmente no puedes saberlo, porque los BIOs pueden reordenarse, consolidarse, fusionarse y emitirse desde threads en segundo plano que no son el solicitante original. `dtrace` con la sonda `io:::start` más capturas de pila puede acercarte, pero es trabajo de investigación, no una responsabilidad rutinaria del driver.

### ¿Pueden montarse simultáneamente dos sistemas de archivos distintos en dos números de unidad de mi driver?

Sí, si implementaste soporte multi-unidad. Cada unidad presenta su propio proveedor de GEOM. Sus almacenes de respaldo son independientes. El único estado compartido son las variables globales de tu módulo y el propio kernel, de modo que los dos montajes no interactúan entre sí a menos que tú lo hagas.

### ¿Debe mi driver gestionar los eventos de gestión de energía?

Para un pseudo dispositivo, no. Para un driver de hardware real, sí: los eventos de suspend y resume fluyen a través de Newbus como llamadas a métodos, y el driver debe detener la I/O en suspend y revalidar el estado del dispositivo en resume. Los drivers de almacenamiento en portátiles son una fuente habitual de bugs relacionados con el suspend, por lo que los drivers reales lo toman muy en serio.

### ¿Cuál es el impacto práctico de elegir 512 frente a 4096 como d_sectorsize?

En los sistemas de archivos modernos, muy poco: UFS, ZFS y la mayoría de los sistemas de archivos de FreeBSD funcionan correctamente con cualquiera de los dos. Por el lado del driver, un tamaño de sector mayor reduce el número de BIOs para transferencias grandes. Por el lado de la carga de trabajo, las aplicaciones que usan O_DIRECT o realizan I/O alineada pueden verse afectadas. En caso de duda, elige 4096 para drivers nuevos; se corresponde con la flash moderna y evita penalizaciones de alineación.

### Si recargo mi driver muchas veces, ¿hay fugas de memoria?

Solo si tienes un bug. En nuestro diseño, `MOD_UNLOAD` llama a `myblk_detach_unit`, que libera el almacén de respaldo y el softc. La variante de persistencia retiene deliberadamente el almacén de respaldo entre recargas, pero usa un único puntero global, por lo que no hay fuga; se reutiliza la misma memoria. Si `vmstat -m | grep myblk` aumenta entre recargas, investígalo.

### ¿Por qué `mount` a veces funciona en mi dispositivo raw pero `newfs_ufs` falla?

`newfs_ufs` escribe metadatos estructurados (superbloque, grupos de cilindros, tablas de inodos) y luego lee parte de ellos para verificarlos. Si el dispositivo es demasiado pequeño, corrompe escrituras silenciosamente o devuelve errores solo bajo ciertas condiciones, `newfs_ufs` lo detecta primero. `mount` es mucho menos estricto en la ruta de escritura; puede leer un superbloque corrupto y producir errores extraños más tarde. Un `newfs` exitoso es una señal de corrección más fiable que un `mount` exitoso.

### ¿Cómo verifico que mi implementación de BIO_FLUSH realmente hace los datos duraderos?

Para nuestro driver en memoria, la durabilidad está limitada por la alimentación del host: vaciar no hace nada útil porque un corte de alimentación se lo lleva todo. Para un driver real respaldado por almacenamiento persistente, el contrato consiste en emitir un comando de vaciado al medio subyacente y confirmar su finalización antes de llamar a `biodone`. Las pruebas requieren un banco de pruebas con ciclos de alimentación o un simulador; no hay atajos.

### ¿Cuáles son las reglas de bloqueo correctas dentro de d_strategy?

Mantén el lock del driver el tiempo suficiente para proteger el almacén de respaldo contra el acceso concurrente, y libéralo antes de llamar a `biodone`. Nunca mantengas un lock durante una llamada a otro subsistema. Nunca llames a `malloc(M_WAITOK)` mientras mantengas un lock. Nunca duermas. Si necesitas dormir, programa el trabajo en un taskqueue y llama a `biodone` desde el worker.

### ¿Por qué BIO_FLUSH no es una barrera por porcentaje de capacidad como las barreras de escritura en Linux?

BIO_FLUSH en FreeBSD es una barrera puntual en el tiempo: cuando se completa, todas las escrituras emitidas anteriormente son duraderas. No está asociada a un rango o porcentaje específico del dispositivo. Los drivers pueden implementarla como una barrera estricta o como un vaciado oportunista, pero el contrato mínimo es la garantía puntual en el tiempo.

### ¿Hay herramientas que generen tráfico de BIO para ayudarme a probar?

Sí. `dd(1)` con varios valores de `bs=`, `fio(1)` desde ports, `ioping(8)` desde ports, más los de siempre: `newfs`, `tar`, `rsync`, `cp`. `diskinfo -t` ejecuta una batería de lecturas de benchmarking y es útil para obtener cifras de rendimiento aproximadas. Los arneses de prueba bajo `/usr/src/tools/regression/` también pueden adaptarse.

## Qué no cubre este capítulo

Este capítulo es extenso, pero hay varios temas relacionados que hemos dejado deliberadamente para más adelante. Nombrarlos aquí te ayuda a planificar el estudio futuro y evita la falsa impresión de que los drivers de almacenamiento terminan en el manejador de BIO.

**Los drivers de almacenamiento de hardware real**, como los de controladoras SATA, SAS y NVMe, viven bajo CAM y requieren maquinaria adicional considerable: asignación de bloques de comandos, tagged queueing, gestión de eventos hot-plug, carga de firmware, datos SMART y protocolos de recuperación de errores. Introdujimos el mundo de CAM brevemente a través del fragmento de `ada_da.c`, pero no lo exploramos en profundidad. Los capítulos 33 al 36 abordarán estas interfaces, y el driver `md(4)` que has leído en este capítulo es, en comparación, un escalón deliberadamente pequeño.

**La integración con ZFS** es un mundo aparte. ZFS consume proveedores GEOM a través de su capa vdev, pero añade semántica copy-on-write, sumas de verificación extremo a extremo, almacenamiento en pools y snapshots que ningún driver de bloques sencillo necesitaría conocer. Si tu driver funciona con UFS, casi con toda seguridad funcionará también con ZFS, pero la inversa no está garantizada: ZFS ejercita rutas BIO, especialmente el vaciado y el orden de escritura, que los sistemas de archivos menos exigentes omiten.

**La creación de clases GEOM** es un tema más amplio que el simple envoltorio de `g_disk`. Una clase completa implementa los métodos taste, start, access, attach, detach, dumpconf, destroy_geom y orphan. También puede crear y destruir consumidores, construir topologías multinivel y responder a la configuración mediante `gctl`. Las clases mirror, stripe y crypt son buenos puntos de partida cuando decidas profundizar.

**Las cuotas, las ACLs y los atributos extendidos** son características del sistema de archivos que viven completamente por encima de la capa GEOM. Importan para el espacio de usuario, pero no tocan el driver de almacenamiento. Se trata de una aclaración útil: el trabajo del driver termina en el límite BIO.

**El trazado y la depuración de fallos del kernel** merecen un capítulo propio. Los core dumps del kernel se almacenan en un dispositivo de volcado configurado mediante `dumpon(8)` y se analizan con `kgdb(1)` o `crashinfo(8)`. Si tu driver provoca un pánico en el sistema, saber cargar el archivo de volcado e inspeccionar los backtraces es una habilidad de nivel profesional que este capítulo apenas ha esbozado.

**Las rutas de almacenamiento de alto rendimiento** utilizan características como unmapped I/O, completado por despacho directo, CPU pinning, asignación consciente del NUMA y colas dedicadas. Estas optimizaciones importan en cargas de trabajo de gigabytes por segundo, pero son irrelevantes para un driver de enseñanza. Cuando empieces a perseguir microsegundos, vuelve a `/usr/src/sys/dev/nvme/` y estudia cómo lo hacen los verdaderos profesionales.

**El comportamiento específico de cada sistema de archivos** varía considerablemente. UFS solicita un conjunto de BIOs; ZFS solicita un conjunto diferente; msdosfs y ext2fs solicitan algo distinto todavía. Un buen driver de almacenamiento es independiente del sistema de archivos, pero observar distintos sistemas de archivos sobre tu driver es una forma fantástica de desarrollar la intuición. Prueba `msdosfs`, `ext2fs` y `tmpfs` como contraste una vez que te sientas cómodo con UFS.

**iSCSI y los dispositivos de bloques en red** también se presentan como proveedores GEOM, pero los crean demonios de control en userland que se comunican con la pila de red. El Capítulo 28 comienza con el trabajo de red que hace posibles esos proveedores.

Nuestro tratamiento de la ruta de almacenamiento fue deliberadamente enfocado. Escribimos un driver que los sistemas de archivos aceptan como real, entendimos por qué y cómo es percibido así, y trazamos el recorrido de los datos desde `write(2)` hasta la RAM. Esa base es suficiente para que los temas no explorados arriba resulten legibles en lugar de desconcertantes.

## Reflexión final

Los drivers de almacenamiento tienen fama de ser territorio inhóspito. Este capítulo debería haber sustituido parte de esa fama por familiaridad: el BIO no es más que una estructura, la función strategy no es más que un despachador, GEOM no es más que un grafo y `disk_create` no es más que una llamada de registro. Lo que eleva el trabajo con almacenamiento por encima de lo rutinario no son las APIs subyacentes, que son compactas, sino las exigencias operativas que se acumulan a su alrededor: rendimiento, durabilidad, recuperación de errores y corrección bajo contención.

Esas exigencias no desaparecen cuando dejas los pseudodispositivos para pasar al hardware real. Se multiplican. Pero ya tienes el vocabulario para entenderlas. Sabes qué es un BIO y de dónde viene. Sabes qué thread llama a tu código y qué espera de él. Sabes cómo registrarte en GEOM, cómo darte de baja de forma limpia y cómo reconocer una petición en vuelo por su sombra en `gstat`. Cuando te sientes frente a un driver real de controlador SATA y empieces a leer, reconocerás la forma del código aunque los detalles sean diferentes.

El arte de escribir drivers de almacenamiento es, en definitiva, paciente. Se aprende escribiendo drivers pequeños, leyendo el árbol de código fuente, reproduciendo experimentos sencillos y desarrollando instintos sobre cuándo algo que parece correcto es realmente correcto. El capítulo que acabas de terminar es un largo paso en ese camino. Los capítulos siguientes darán otro paso, cada vez en direcciones diferentes.

## Lecturas recomendadas

Si este capítulo ha abierto tu apetito por los entresijos del almacenamiento, aquí tienes algunos lugares donde continuar.

**Páginas de manual**. `disk(9)`, `g_bio(9)`, `geom(4)`, `devfs(5)`, `ufs(5)`, `newfs(8)`, `mdconfig(8)`, `gstat(8)`, `diskinfo(8)`, `mount(2)`, `mount(8)`. Léelas en ese orden.

**The FreeBSD Architecture Handbook**. El capítulo sobre almacenamiento es un buen complemento a este.

**Kirk McKusick et al., "The Design and Implementation of the FreeBSD Operating System"**. Los capítulos del libro sobre el sistema de archivos son especialmente relevantes.

**Libros sobre DTrace**. El *DTrace Book* de Brendan Gregg es una referencia práctica; el *Dynamic Tracing Guide* de Sun es el tutorial original.

**El árbol de código fuente de FreeBSD**. `/usr/src/sys/geom/`, `/usr/src/sys/dev/md/`, `/usr/src/sys/ufs/` y `/usr/src/sys/cam/ata/` (donde `ata_da.c` implementa el driver de disco `ada`). Todos los patrones tratados en este capítulo tienen su base en ese código.

**Los archivos de las listas de correo**. `freebsd-geom@` y `freebsd-fs@` son las dos listas más relevantes. Leer hilos históricos es una de las mejores formas de adquirir el conocimiento institucional que los libros no recogen.

**Historial de commits en los mirrors de GitHub**. El árbol de código fuente de FreeBSD tiene un largo historial de commits bien anotado. Para cualquier archivo que abras, ejecutar `git log --follow` contra su mirror revelará con frecuencia la justificación de las decisiones de diseño, los errores que dieron forma al código actual y las personas que lo mantienen. El contexto histórico facilita enormemente la lectura del código actual.

**The Transactions of the FreeBSD Developer Summit**. Varios encuentros han incluido sesiones centradas en el almacenamiento. Las grabaciones y las diapositivas, cuando están disponibles, son excelentes para conocer el estado del arte y los debates de diseño abiertos.

**Lectura de las pilas de almacenamiento de otros sistemas operativos**. Una vez que conoces la ruta de almacenamiento de FreeBSD, la capa de bloques de Linux, el framework SD de Illumos y las clases de almacenamiento IOKit de macOS se vuelven comprensibles de una manera que probablemente no lo eran antes. Las APIs específicas difieren, pero las formas fundamentales (BIOs o sus equivalentes, sistemas de archivos por encima, hardware por debajo) son universales.

**Marcos de pruebas para código del kernel**. El conjunto de pruebas `kyua(1)` ejecuta tests de regresión contra kernels reales. El árbol `/usr/tests/sys/geom/` contiene ejemplos de cómo son los tests bien escritos para código de almacenamiento; leerlos desarrolla tanto los instintos para las pruebas como la confianza en que tu código es correcto.

**Entradas del blog de la FreeBSD Foundation**. La Foundation financia varios proyectos relacionados con el almacenamiento y publica resúmenes accesibles que complementan el árbol de código fuente.

---

Fin del Capítulo 27. Cierra tu cuaderno de laboratorio, asegúrate de que tu driver está descargado y tus puntos de montaje liberados, y tómate un descanso antes del Capítulo 28.

Acabas de escribir un driver de almacenamiento, montar un sistema de archivos sobre él y seguir el recorrido de los datos a través del buffer cache, hasta GEOM, a través de tu función strategy y de vuelta. Eso es un verdadero logro. Tómate un momento para saborearlo antes de pasar la página.
