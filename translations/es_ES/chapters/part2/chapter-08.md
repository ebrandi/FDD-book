---
title: "Trabajando con archivos de dispositivo"
description: "Cómo devfs, los cdevs y los nodos de dispositivo dan a tu driver una superficie de usuario segura y bien estructurada."
partNumber: 2
partName: "Building Your First Driver"
chapter: 8
lastUpdated: "2026-04-17"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 210
language: "es-ES"
---
# Trabajando con archivos de dispositivo

## Orientación al lector y resultados de aprendizaje

En el Capítulo 7 construiste `myfirst`, un driver real de FreeBSD que se engancha limpiamente, crea `/dev/myfirst0`, abre y cierra ese nodo y se descarga sin fugas. Fue la primera victoria, y fue real. Ahora tienes en disco un esqueleto de driver funcional, un archivo `.ko` que el kernel aceptará y liberará a tu orden, y una entrada en `/dev` a la que los programas de usuario pueden llegar.

Este capítulo se centra en la pieza de ese trabajo que más fácilmente se da por sentada: **el archivo de dispositivo en sí**. La línea de código que creó `/dev/myfirst0` en el Capítulo 7 era compacta, pero se asienta sobre un subsistema llamado **devfs**, y ese subsistema es el puente entre todo lo que tu driver hace dentro del kernel y cualquier herramienta o programa al que un usuario apunte. Entender bien ese puente ahora hará que los Capítulos 9 y 10, donde el flujo de datos reales comienza, resulten mucho menos misteriosos.

### Por qué este capítulo tiene lugar propio

El Capítulo 6 introdujo el modelo de archivos de dispositivo a nivel de imágenes mentales, y el Capítulo 7 usó suficiente de él para poner en marcha un driver. Ninguno de los dos se detuvo a examinar la superficie en sí. Eso no es un descuido. En un libro que enseña a escribir drivers desde los primeros principios, el archivo de dispositivo merece un capítulo dedicado porque los errores que se cometen en esa superficie son también los más difíciles de deshacer después.

Piensa en lo que esa superficie debe soportar. Soporta identidad (una ruta que un programa de usuario puede predecir). Soporta política de acceso (quién tiene permiso para abrir, leer o escribir). Soporta multiplexación (un driver, muchas instancias, muchas aperturas simultáneas). Soporta ciclo de vida (cuándo aparece el nodo, cuándo desaparece, y qué ocurre cuando un programa de usuario está en medio de una llamada mientras sucede). Soporta compatibilidad (nombres heredados junto con nombres modernos). Soporta observabilidad (qué pueden ver y cambiar los operadores desde el userland). Un driver que tenga los internos correctos pero la superficie incorrecta será un driver que los operadores rechacen desplegar, un driver que los revisores de seguridad marquen, un driver que falle de forma sutil dentro de jails, un driver que sufra deadlock al descargarse bajo carga realista.

El Capítulo 7 te dio justo la superficie suficiente para demostrar que el camino funcionaba. Este capítulo te da la suficiente para diseñarla a propósito.

### Dónde dejó el driver el Capítulo 7

Vale la pena hacer una pequeña revisión del estado de `myfirst` antes de extenderlo. El driver del Capítulo 7 termina con todo esto en su lugar:

- Una ruta `device_identify`, `device_probe` y `device_attach` que crea exactamente un hijo de Newbus de `nexus0` llamado `myfirst0`.
- Un softc asignado por Newbus, accesible a través de `device_get_softc(dev)`.
- Un mutex, un árbol sysctl bajo `dev.myfirst.0.stats` y tres contadores de solo lectura.
- Una `struct cdevsw` poblada con `d_open`, `d_close` y stubs para `d_read` y `d_write`.
- Un nodo `/dev/myfirst0` creado con `make_dev_s(9)` en `attach` y eliminado con `destroy_dev(9)` en `detach`.
- Un desenrollado de errores de etiqueta única que deja el kernel en un estado coherente si algún paso de attach falla.
- Una política de apertura exclusiva que rechaza un segundo `open(2)` con `EBUSY`.

El Capítulo 8 trata ese driver como punto de partida y lo hace crecer en tres ejes: **forma** (cómo se llama el nodo y cómo se agrupa), **política** (quién puede usarlo y cómo esa política se mantiene entre reinicios) y **estado por descriptor** (cómo dos aperturas simultáneas pueden tener su propio registro independiente).

### Qué aprenderás

Al final de este capítulo serás capaz de:

- Explicar qué es un archivo de dispositivo en FreeBSD y por qué `/dev` no es un directorio ordinario.
- Describir cómo `struct cdev`, el vnode de devfs y un descriptor de archivo de usuario se relacionan entre sí.
- Elegir valores sensatos de propietario, grupo y permisos para un nuevo nodo de dispositivo.
- Dar a un nodo de dispositivo un nombre estructurado (incluyendo un subdirectorio bajo `/dev`).
- Crear un alias para que un único cdev sea accesible bajo más de una ruta.
- Adjuntar estado por apertura con `devfs_set_cdevpriv()` y limpiarlo de forma segura cuando el descriptor de archivo se cierra.
- Ajustar los permisos del nodo de dispositivo de forma persistente desde el userland con `devfs.conf` y `devfs.rules`.
- Ejercitar tu driver desde un pequeño programa C en el userland, no solo con `cat` y `echo`.

### Qué construirás

Extenderás el driver `myfirst` del Capítulo 7 en tres pequeños pasos:

1. **Etapa 0: permisos más ordenados y un nombre estructurado.** El nodo pasa de `/dev/myfirst0` a `/dev/myfirst/0` con una variante accesible para el grupo, para uso en el laboratorio.
2. **Etapa 1: un alias visible para el usuario.** Añades `/dev/myfirst` como alias de `/dev/myfirst/0` para que las rutas heredadas sigan funcionando.
3. **Etapa 2: estado por apertura.** Cada `open(2)` obtiene su propio pequeño contador con `devfs_set_cdevpriv()`, y verificas desde el userland que dos aperturas simultáneas ven valores independientes.

También escribirás un pequeño programa en el userland, `probe_myfirst.c`, que abre el dispositivo, lee un poco, informa de lo que vio y cierra limpiamente. Este programa volverá en el Capítulo 9 cuando se implementen las rutas reales de `read(2)` y `write(2)`.

### Qué no cubre este capítulo

Varios temas rozan `/dev` pero se posponen deliberadamente:

- **Semántica completa de `read` y `write`.** El Capítulo 7 los convirtió en stubs; el Capítulo 9 los implementa correctamente con `uiomove(9)`. Aquí solo preparamos el terreno.
- **Clonación de dispositivos** (`clone_create`, el manejador de eventos `dev_clone`). Estos merecen un examen cuidadoso más adelante, una vez que el modelo básico esté asentado.
- **Diseño de `ioctl(2)`.** Inspeccionar y cambiar el estado del dispositivo mediante `ioctl` es un tema en sí mismo y pertenece a una parte posterior del libro.
- **GEOM y dispositivos de almacenamiento.** GEOM se construye sobre cdevs pero añade toda una pila propia. Eso corresponde a la Parte 6.
- **Nodos de interfaz de red e `ifnet`.** Los drivers de red no viven bajo `/dev`. Aparecen a través de una superficie diferente, que conoceremos en la Parte 6.

Mantener el alcance acotado aquí es la cuestión. La superficie de un dispositivo es pequeña; la disciplina en torno a ella debería ser lo primero que domines.

### Tiempo estimado de dedicación

- **Solo lectura:** unos 30 minutos.
- **Lectura más los cambios de código en `myfirst`:** alrededor de 90 minutos.
- **Lectura más los cuatro laboratorios:** de dos a tres horas, incluyendo ciclos de recompilación y pruebas en el userland.

Una sesión continuada con pausas funciona mejor. El capítulo es más corto que el Capítulo 7, pero las ideas aquí aparecen en casi todos los drivers que leerás alguna vez.

### Requisitos previos

- Un driver `myfirst` del Capítulo 7 que se carga, engancha y descarga limpiamente.
- FreeBSD 14.3 en tu laboratorio con `/usr/src` correspondiente.
- Comodidad básica leyendo rutas de `/usr/src` como `/usr/src/sys/dev/null/null.c`.

### Cómo sacar el máximo provecho de este capítulo

Abre tu código fuente del Capítulo 7 junto a este capítulo y edita el mismo archivo. No estás empezando un proyecto nuevo; estás haciendo crecer el que ya tienes. Cuando el capítulo te pida inspeccionar un archivo de FreeBSD, ábrelo de verdad con `less` y desplázate por él. El modelo de archivos de dispositivo encaja mucho más rápido cuando has visto a un par de drivers reales dar forma a sus nodos.

Un hábito práctico que da frutos inmediatamente: a medida que lees, mantén un segundo terminal abierto contra un sistema de laboratorio recién arrancado y confirma cada afirmación sobre los nodos existentes con `ls -l` o `stat(1)`. Escribir `ls -l /dev/null` y ver que la salida coincide con la prosa es algo pequeño, pero ancla la abstracción en algo que puedes ver. Para cuando el capítulo llegue a los laboratorios, habrás adquirido el reflejo tranquilo de verificar cada afirmación contra el kernel en ejecución, en lugar de confiar solo en el texto.

Un segundo hábito: cuando el capítulo mencione un archivo fuente bajo `/usr/src`, ábrelo junto a la sección. Los drivers reales de FreeBSD son el libro de texto; este libro es solo la guía de lectura. El material dentro de `/usr/src/sys/dev/null/null.c` y `/usr/src/sys/dev/led/led.c` es lo suficientemente corto para repasarlo en pocos minutos cada uno, y cada uno de ellos está moldeado por las mismas decisiones que este capítulo está a punto de explicar. Un breve recorrido por allí vale más que cualquier cantidad de prosa aquí.

### Hoja de ruta por el capítulo

Si quieres una imagen del capítulo como un hilo continuo, aquí la tienes. Las secciones en orden son:

1. Qué es realmente un archivo de dispositivo, en teoría y en la práctica de `ls -l`.
2. Cómo llegó a ser devfs, el sistema de archivos detrás de `/dev`, y qué hace por ti.
3. Los tres objetos del kernel que se alinean detrás de un archivo de dispositivo.
4. Cómo la propiedad, el grupo y el modo conforman lo que muestra `ls -l` y quién puede abrir el nodo.
5. Cómo se eligen los nombres, incluyendo números de unidad y subdirectorios.
6. Cómo un cdev puede responder a varios nombres mediante aliases.
7. Cómo se registra, recupera y limpia el estado por apertura.
8. Cómo funciona realmente el destructor una vez que se llama a `destroy_dev(9)`.
9. Cómo `devfs.conf` y `devfs.rules` dan forma a la política desde el userland.
10. Cómo manejar el dispositivo desde pequeños programas en el userland que puedes escribir tú mismo.
11. Cómo los drivers reales de FreeBSD resuelven estos mismos problemas.
12. Qué valores errno debe devolver tu `d_open`, y cuándo.
13. A qué herramientas recurrir cuando algo en esta superficie parece incorrecto.
14. De cuatro a ocho laboratorios que te guían por cada patrón de forma práctica.
15. Desafíos que extienden los patrones hacia escenarios realistas.
16. Una guía de campo sobre los escollos que hacen perder tiempo si los encuentras sin previo aviso.

Sigue el capítulo de principio a fin si es la primera vez. Si estás revisando, puedes abordar cada sección por separado; la estructura está diseñada para leerse como un recorrido completo, no solo como un tutorial lineal.



## Qué es realmente un archivo de dispositivo

Mucho antes de que FreeBSD existiera, UNIX se comprometió con una idea famosa: **tratar los dispositivos como archivos**. Una línea serie, un disco, un terminal, un flujo de bytes aleatorios, cada uno de estos podía abrirse, leerse, escribirse y cerrarse con el mismo puñado de llamadas al sistema. Los programas de usuario no necesitaban saber si los bytes que consumían provenían de un disco giratorio, un buffer de memoria o una fuente imaginaria como `/dev/null`.

Esa idea no era marketing. Era una decisión de diseño que Ken Thompson y Dennis Ritchie tomaron en las primeras versiones de UNIX a principios de los años setenta, y moldeó todo el sistema operativo que siguió. Al presentar cada dispositivo a través de la misma interfaz de llamadas al sistema que cualquier archivo ordinario, convirtieron cada herramienta de línea de comandos que manejaba archivos en una herramienta que también podía manejar dispositivos. `cat` podía copiar bytes de un puerto serie. `dd` podía leer de una unidad de cinta. `cp` podía transmitir un flujo. Esa alineación sigue siendo la propiedad más útil de un sistema tipo UNIX, y FreeBSD la hereda completamente.

FreeBSD mantiene ese espíritu. Desde el espacio de usuario, un archivo de dispositivo parece como cualquier otra entrada en el sistema de archivos. Tiene una ruta, un propietario, un grupo, un modo y un tipo visible para `ls -l`. Puedes usar `open(2)` con él, pasar el descriptor de archivo devuelto a `read(2)` y `write(2)`, consultarlo con `stat(2)` y cerrarlo cuando hayas terminado.

### Una breve historia, en fragmentos que vale la pena conocer

El modelo de archivos de dispositivo en el que vas a escribir ha pasado por varias revisiones importantes desde 1971, y cada revisión fue impulsada por una limitación real de la anterior. Conocer los aspectos generales evita confusiones más adelante cuando leas libros o páginas de manual antiguos.

En **V7 UNIX** y en los BSDs que le siguieron durante dos décadas, las entradas bajo `/dev` eran entradas reales en un sistema de archivos real en disco. Un administrador usaba `mknod(8)` para crearlas, pasando un tipo de dispositivo (carácter o bloque) y un par de enteros pequeños llamados *número mayor* y *número menor*. El kernel usaba el número mayor para seleccionar una fila en una tabla de drivers (el array `cdevsw` o `bdevsw`, según el tipo), y el número menor para elegir a qué instancia de ese driver iba la llamada. El par se escribía en `/dev` una vez, a mano o mediante un script de shell llamado `MAKEDEV`, y luego vivía en el disco para siempre.

Ese modelo funcionó durante mucho tiempo. Dejó de funcionar cuando ocurrieron dos cosas a la vez: el hardware empezó a ser hot-plug y el espacio de números mayor se saturó hasta el punto de que cualquier cambio en el kernel requería coordinación en todo el árbol de código fuente. Un disco conectado en tiempo de ejecución necesitaba un nodo que no existía antes del arranque. Un driver que se movía de un lugar a otro en el árbol necesitaba que sus números fueran renegociados. Las entradas estáticas de `/dev` no representaban fielmente ninguna de las dos situaciones.

En **FreeBSD 5**, publicado en 2003, la respuesta fue **devfs**, un sistema de archivos virtual gestionado por el kernel que reemplaza por completo el `/dev` almacenado en disco. Cuando un driver crea un nodo de dispositivo mediante `make_dev(9)`, devfs añade una entrada a su árbol activo y los programas de usuario pueden verla de inmediato. Cuando el driver llama a `destroy_dev(9)`, devfs elimina la entrada. Los números mayor y menor siguen existiendo dentro del kernel como detalle de implementación, pero ya no forman parte del contrato. Las rutas y los punteros cdev sí lo son. Ese es el modelo con el que trabajas hoy.

Un tercer cambio que merece mencionarse: **los dispositivos de bloques han dejado de ser visibles en userland**. Las variantes más antiguas de UNIX exponían ciertos dispositivos de almacenamiento como archivos especiales de bloque cuya letra de tipo en `ls -l` era `b`. FreeBSD no ha incluido nodos de dispositivo de bloque especial en userland desde hace muchos años. Los drivers de almacenamiento siguen existiendo en el kernel; simplemente se publican a través de GEOM y aparecen en `/dev` como dispositivos de caracteres. La única vez que verás una `b` en FreeBSD es en discos montados desde otros tipos de sistema de archivos. Tus drivers expondrán dispositivos de caracteres, punto.

### Por qué la abstracción de archivo demostró su valor

La ventaja de la idea de «todo es un archivo» es que cada herramienta del sistema base ya es, sin necesidad de conocimiento especial, una herramienta para comunicarse con tu dispositivo. Vale la pena detenerse un momento a reflexionar sobre esto, porque marca el tono de cómo deberías diseñar tu driver.

`cat` lee archivos. También leerá de `/dev/myfirst/0` en cuanto tu driver implemente `d_read`, sin necesidad de compilación especial y sin saber que está hablando con un driver. `dd` lee y escribe archivos en bloques de tamaño arbitrario; transferirá datos hacia o desde un dispositivo de caracteres sin problema, y ofrece opciones (`bs=`, `count=`, `iflag=nonblock`) que permiten a los operadores ejercer el comportamiento del driver sin necesidad de escribir nuevos programas. `tee`, `head`, `tail` en modo de seguimiento, `od`, `hexdump`, `strings`, todas ellas funcionan ya con tu dispositivo el día en que lo publicas. La redirección de shell funciona. Las tuberías funcionan. La maquinaria de descriptores de archivo del kernel no distingue qué lado de una tubería es un dispositivo y cuál es un archivo normal.

La guía de diseño que se desprende de esto es simple y estricta: **tu driver debe comportarse como un archivo siempre que pueda**. Eso significa devolver longitudes en `read(2)` que coincidan con la realidad, devolver fin de archivo como cero bytes leídos, devolver resultados de `write(2)` que cuenten los bytes realmente consumidos, respetar las convenciones de `errno` cuando algo va mal, y no inventar nuevos significados para las llamadas al sistema existentes a menos que el significado sea inevitable. Cuanto más se parezca tu dispositivo a un archivo ordinario para cada herramienta de `/bin` y `/usr/bin`, menos tendrán que aprender tus usuarios, y menos frágil se volverá tu interfaz cuando aparezcan nuevas herramientas años después.

### Lo que un archivo de dispositivo no es

La abstracción es generosa, pero no ilimitada. Vale la pena enumerar algunas cosas que un archivo de dispositivo explícitamente no es, para que no diseñes basándote en un modelo mental equivocado.

Un archivo de dispositivo **no es un canal IPC** en general. Puede comportarse como tal, del mismo modo que una tubería con nombre puede comportarse como tal, pero las herramientas clásicas para la comunicación entre procesos son `pipe(2)`, `socketpair(2)`, los sockets de dominio UNIX y `kqueue(9)`. Si dos programas de usuario quieren intercambiar mensajes entre sí, deberían usar esas herramientas en lugar de abrir un nodo de dispositivo como canal secundario. Un driver que se deja utilizar como bus IPC improvisado verá cómo su semántica se complica cada vez más a medida que los usuarios inventen nuevos usos para él.

Un archivo de dispositivo **no es un registro de configuraciones** que tu driver conserve entre reinicios. devfs no persiste nada. Todo lo que escribas en `/dev/yournode` es procesado por tu driver en el momento en que se escribe y desaparece a menos que tu driver haya decidido recordarlo. Si necesitas configuración persistente, las herramientas adecuadas son los ajustables de `sysctl(8)`, las variables de entorno del cargador establecidas mediante `loader.conf(5)`, y los archivos de configuración que la parte en espacio de usuario de tu sistema lee desde `/etc`.

Un archivo de dispositivo **no es un medio de difusión** por defecto. Si un driver quiere entregar el mismo flujo de bytes a cada descriptor de archivo abierto, debe implementarlo explícitamente; el kernel no distribuye escrituras entre lectores automáticamente, ni duplica lecturas en múltiples archivos. El capítulo 10 aborda `poll(2)` y `kqueue(9)` de forma tangencial a esto, y varios drivers del árbol (por ejemplo `/usr/src/sys/net/bpf.c`) lo resuelven de forma deliberada. No viene de forma gratuita.

Un archivo de dispositivo **no es un sustituto de una llamada al sistema**. Si tu driver necesita una interfaz de control estructurada, tipada y versionada que los programas de usuario invoquen con comandos con nombre, para eso está `ioctl(2)`. En este capítulo no diseñamos `ioctl`, pero la distinción es importante ahora: no introduzcas comandos de control de contrabando mediante cadenas de `write(2)` cuando `ioctl(2)` los expresaría con más precisión. El capítulo 25 retoma el diseño de `ioctl` como parte de la práctica avanzada de drivers.

Tener presentes esos límites ahora mantendrá tu diseño honesto más adelante. La superficie de archivo de dispositivo es un conjunto reducido de primitivas bien definidas. El arte está en elegir cuáles de ellas expone tu driver y en conectar cada una de ellas con cuidado.

### La variedad que encontrarás bajo /dev

Un breve recorrido por un sistema FreeBSD 14.3 recién arrancado es una buena forma de calibrar lo que este capítulo está perfilando. Abre un terminal y ejecuta `ls /dev` en tu máquina de laboratorio. Verás una muestra representativa de los patrones de nombres que el libro te enseñará a reconocer:

- **Singletons**: `null`, `zero`, `random`, `urandom`, `full`, `klog`, `mem`, `kmem`.
- **Instancias numeradas**: `bpf0`, `bpf1`, `md0`, `ttyu0`, `ttyv0`, `ttyv1`, `cuaU0`.
- **Subdirectorios por driver**: `led/`, `pts/`, `fd/`, `input/`.
- **Nombres estándar con significado especial**: `stdin`, `stdout`, `stderr`, `console`, `tty`.
- **Alias para rutas convencionales**: `log`, `sndstat`, `midistat`.

Cada entrada de ese recorrido es un cdev que algún driver o subsistema pidió a devfs que presentara. Algunas fueron creadas por drivers que cargaron en el arranque, otras por drivers compilados en el propio kernel, y otras por la propia configuración del montaje de devfs. Cada una de ellas fue moldeada por las mismas decisiones que estás a punto de tomar para `myfirst`.

Echa un vistazo rápido a los nodos ya presentes en tu sistema de laboratorio:

```sh
% ls -l /dev/null /dev/zero /dev/random
crw-rw-rw-  1 root  wheel     0x17 Apr 17 09:14 /dev/null
crw-r--r--  1 root  wheel     0x14 Apr 17 09:14 /dev/random
crw-rw-rw-  1 root  wheel     0x18 Apr 17 09:14 /dev/zero
```

La `c` al inicio de cada campo de modo te indica que son **dispositivos de caracteres**. En FreeBSD no hay nodos de dispositivos de bloque de los que preocuparse en este contexto; el almacenamiento se gestiona a través de dispositivos de caracteres cubiertos por GEOM, y la antigua distinción de `block special` ya no se expone al espacio de usuario como lo hacía en los UNIX más antiguos. Para el trabajo que estás haciendo ahora, y para la mayoría de los drivers que escribirás, **dispositivo de caracteres** es la forma que tomará tu nodo.

La parte interesante de `ls -l` es lo que *no* está ahí. No hay ningún archivo de respaldo en disco que contenga los bytes de `/dev/null`. No hay ningún archivo normal escondido detrás de `/dev/random` en algún lugar de `/var`. Estos nodos son presentados por el kernel bajo demanda, y los permisos y la propiedad que ves son los que el kernel ha elegido anunciar. Ese es el cambio mental clave para este capítulo: en FreeBSD, las entradas bajo `/dev` no son archivos ordinarios que tu driver manipula. Son una **vista** de objetos del lado del kernel llamados `struct cdev`, servidos por un sistema de archivos dedicado.

### Los dispositivos de caracteres son el caso más habitual

Un dispositivo de caracteres entrega un flujo de bytes a través de `read(2)` y acepta un flujo de bytes a través de `write(2)`. Puede o no admitir desplazamiento (seeking). Puede admitir `ioctl(2)`, `mmap(2)`, `poll(2)` y `kqueue(9)`, y cada uno de ellos es opcional en el lado del driver. El driver declara qué operaciones admite rellenando los campos relevantes de una `struct cdevsw`, exactamente como viste en el capítulo 7.

`myfirst` es un dispositivo de caracteres. También lo son `/dev/null`, `/dev/zero`, los nodos de terminal bajo `/dev/ttyu*`, las interfaces de paquetes BPF bajo `/dev/bpf*`, y muchos otros. Cuando eres nuevo en el desarrollo de drivers, «dispositivo de caracteres» es casi siempre la respuesta correcta.

Si vienes de un entorno donde los nodos de bloque especiales eran habituales, el ajuste mental es pequeño. En FreeBSD nunca escribes un driver que exponga un nodo de bloque especial directamente; los drivers de almacenamiento producen dispositivos de caracteres que GEOM consume, y GEOM a su vez republica sus propios dispositivos de caracteres hacia arriba. Para este capítulo y los siguientes, la expresión «archivo de dispositivo» siempre significa un dispositivo de caracteres.

### Cómo leer `ls -l` en un nodo de dispositivo

Vale la pena dedicar un momento a la forma del resultado, porque cada línea que inspeccionarás con `ls -l` sigue la misma plantilla.

```sh
% ls -l /dev/null
crw-rw-rw-  1 root  wheel     0x17 Apr 17 09:14 /dev/null
```

La `c` en la posición inicial indica que es un dispositivo de caracteres. Un archivo normal mostraría `-`, un directorio mostraría `d`, un enlace simbólico mostraría `l`. Los nueve caracteres de permisos que le siguen se leen exactamente igual que para un archivo normal: tres tríos de permisos del propietario, del grupo y de otros, en el orden lectura, escritura, ejecución. Los dispositivos ignoran el bit de ejecución, por lo que casi nunca lo verás activado.

El propietario es `root` y el grupo es `wheel`. Se imprimen a partir de los valores que el kernel anuncia, que son una combinación de lo que el driver solicitó y lo que `devfs.conf` o `devfs.rules` aplicaron encima. Cambia cualquiera de ellos y esta columna cambia de inmediato; no hay ningún archivo en disco que reescribir.

El campo que parece extraño es `0x17`. En un archivo normal, esta columna contiene el tamaño en bytes. En un archivo de dispositivo, devfs informa en su lugar de un pequeño identificador hexadecimal. No es un número mayor o menor en el sentido antiguo de System V, y normalmente no necesitarás interpretarlo. Si quieres confirmar que dos nombres apuntan al mismo cdev subyacente (por ejemplo un nodo primario y un alias), `stat -f '%d %i' path` es una forma más fiable de comparar. Volveremos a ello en la sección de userland.

Por último, `ls -l` sobre un directorio bajo `/dev` funciona exactamente como cabría esperar, porque devfs es realmente un sistema de archivos. `ls -l /dev/myfirst` listará los archivos dentro del subdirectorio `myfirst/` si el driver colocó uno allí.



## devfs: de dónde viene /dev

En un sistema UNIX clásico, `/dev` era un directorio real en un disco real, y los nodos de dispositivo se creaban mediante un comando llamado `mknod(8)`. Si necesitabas un nuevo nodo, ejecutabas `mknod` con un tipo, un número mayor y un número menor, y aparecía una entrada en el disco. Era estático. No le importaba si el hardware estaba presente. No hacía limpieza tras de sí.

FreeBSD se alejó de ese modelo. En un sistema FreeBSD moderno, `/dev` es un montaje de **devfs**, un sistema de archivos virtual mantenido íntegramente por el kernel. Puedes verlo en el resultado de `mount(8)`:

```sh
% mount | grep devfs
devfs on /dev (devfs)
```

Dentro de una jaula (jail), generalmente verás un segundo devfs montado en el `/dev` propio de la jaula. El mismo tipo de sistema de archivos, el mismo mecanismo, solo una vista diferente filtrada por `devfs.rules`.

Las reglas de devfs son simples y vale la pena interiorizarlas:

1. No creas archivos bajo `/dev` con `touch` ni con `mknod`. Los creas desde el interior del kernel, llamando a `make_dev_s(9)` o a una de sus variantes.
2. Cuando el cdev de tu driver desaparece, la entrada correspondiente desaparece de `/dev` de forma automática.
3. La propiedad, el grupo y el modo de un nodo empiezan con lo que tu driver haya solicitado, y pueden ajustarse desde el espacio de usuario mediante `devfs.conf` y `devfs.rules`.
4. Nada de un nodo devfs persiste entre reinicios. Siempre es un reflejo en vivo del estado actual del kernel.

Ese último punto es el que sorprende a la gente la primera vez. No puedes ejecutar `chmod /dev/myfirst0` y esperar que el cambio sobreviva al siguiente reinicio. Si necesitas que un permiso persista, lo codificas en el driver o en uno de los archivos de configuración de devfs. Haremos ambas cosas en este capítulo.

Si quieres examinar devfs directamente, su implementación vive en `/usr/src/sys/fs/devfs/`. No necesitas leerlo de cabo a rabo, pero saber dónde está te evitará confusión más adelante cuando alguien pregunte por qué `/dev/foo` tiene el aspecto que tiene.

### Por qué este modelo reemplazó a `mknod`

El alejamiento de los nodos de dispositivo estáticos en el `/dev` del disco no fue una elección estilística. Fue impulsado por tres problemas reales:

1. **Nodos para hardware que no estaba presente.** Antes de devfs, `/dev` contenía entradas para cada dispositivo que el sistema *podría* tener, independientemente de si el kernel tenía en ese momento un driver para él o no. Los usuarios se quedaban adivinando qué rutas estaban activas.
2. **Nodos obsoletos para hardware que había sido retirado.** El hardware de conexión en caliente (USB, FireWire, CardBus en su época) hacía que los árboles `/dev` estáticos fueran activamente engañosos.
3. **Agotamiento de números mayor y menor.** El par `(major, minor)` era un recurso finito y una fuente de disputas de asignación en todo el árbol del kernel. devfs elude ese problema por completo.

Hoy `/dev` es un espejo vivo de lo que el kernel soporta realmente y de qué dispositivos están presentes en ese momento. Un driver que carga crea un nodo; un driver que se descarga lo retira. Un disco que se extrae desaparece. Eso es por diseño, y por eso la palabra «nodo» es un modelo mental mejor que «archivo» para las cosas que ves bajo `/dev`.

### Lo que devfs es y lo que no es

devfs **es** un sistema de archivos. Puedes ejecutar `ls` dentro de él, moverte a subdirectorios, redirigir hacia nodos y demás. Lo que devfs **no es** es un lugar genérico para guardar datos de usuario. No intentes redirigir con `echo` un archivo de log hacia `/dev`. No esperes que `touch` funcione dentro de él. devfs acepta un conjunto pequeño y bien definido de operaciones, y cualquier cosa fuera de ese conjunto devuelve un error.

Esta superficie tan acotada es una ventaja. Significa que devfs nunca te sorprende guardando estado, nunca compite con un sistema de archivos convencional por espacio, y nunca confunde un archivo ordinario con un dispositivo. Tu driver crea los nodos; devfs los presenta; todo lo demás pasa a través de tus manejadores de `cdevsw`.

### devfs en la familia de sistemas de archivos sintéticos

FreeBSD cuenta con una pequeña familia de sistemas de archivos que no almacenan archivos en ningún disco. devfs es uno de ellos. Otros con los que quizá ya te hayas cruzado son `tmpfs(5)`, que sirve archivos desde RAM, `nullfs(5)`, que republica otro directorio bajo un nombre distinto, y `fdescfs(5)`, que presenta los descriptores de archivo de cada proceso como archivos bajo `/dev/fd`. Todos ellos son sistemas de archivos reales a ojos de `mount(8)` y de la capa VFS, pero cada uno sintetiza su contenido a partir de algo distinto a un dispositivo de bloques.

Conocer la familia ayuda por dos razones. La primera es que devfs comparte algunas ideas con sus parientes. Todos los sistemas de archivos sintéticos construyen su árbol bajo demanda, gestionan su almacenamiento fuera de cualquier contenedor en disco y tienen criterios muy definidos sobre qué operaciones tienen sentido dentro de ellos y cuáles no. La segunda razón es que los verás combinados en la práctica. Un jail normalmente monta su propio devfs bajo su propio `/dev`, y puede montar también una vista `nullfs` de `/usr/ports` o un `tmpfs` para `/var/run`. Leer la salida de `mount(8)` dentro de un host FreeBSD o un jail en ejecución es la forma más rápida de hacerse una idea de cómo cooperan estos sistemas de archivos.

Una ojeada rápida en un host de laboratorio típico podría mostrar:

```sh
% mount
/dev/ada0p2 on / (ufs, local, journaled soft-updates)
devfs on /dev (devfs)
fdescfs on /dev/fd (fdescfs)
tmpfs on /tmp (tmpfs, local)
```

Cada uno de ellos es un tipo de sistema de archivos con su propia semántica. devfs es el que nos interesa en este capítulo, y su descripción de trabajo es única: presentar la colección activa de objetos `struct cdev` del kernel como un árbol de nodos con apariencia de archivo.

### Opciones de montaje que conviene conocer

devfs acepta un puñado de opciones de montaje, definidas en el código de devfs y descritas en la página de manual de `mount_devfs(8)`. Rara vez tendrás que cambiarlas desde sus valores predeterminados, pero conviene reconocerlas cuando aparecen en `/etc/fstab`, en configuraciones de jail o en la salida de `mount -v`.

- **`ruleset=N`**: aplica el ruleset de devfs `N` al montaje. Un ruleset es una lista con nombre de patrones de ruta y acciones definida en `/etc/devfs.rules`. Esta opción es el mecanismo por el que los jails limitan el aspecto de su `/dev`.
- **`noauto`**: aparece en `fstab` para marcar el sistema de archivos como no montado automáticamente en el arranque.
- **`late`**: monta tarde en la secuencia de arranque, después de los sistemas de archivos locales y de la red. Relevante cuando se combina con `ruleset=`.

La configuración típica de un host no necesita ninguna de ellas; el montaje predeterminado de devfs en `/dev` es sencillo. Donde más importan es en la configuración de jails, razón por la que la sección 10 de este capítulo recorre un ejemplo completo de jail.

### devfs dentro de los jails

Un jail de FreeBSD es un entorno de ejecución restringido con su propia vista del sistema de archivos, su propio conjunto de procesos y, habitualmente, su propio `/dev`. Cuando `jail(8)` arranca un jail con `mount.devfs=1` en su configuración, monta un devfs separado bajo el `/dev` del jail. Ese montaje es una instancia del mismo tipo de sistema de archivos, con una diferencia decisiva: aplica un ruleset que filtra lo que aparece dentro del jail.

El ruleset predeterminado para un jail es `devfsrules_jail`, numerado `4` en `/etc/defaults/devfs.rules`. Esa es la ruta que los lectores editarán o consultarán en un sistema en ejecución; la fuente de la que se instala es `/usr/src/sbin/devfs/devfs.rules`, para quienes quieran ver las reglas canónicas incluidas en la distribución. Parte de `devfsrules_hide_all` (que oculta todo) y luego desoculta selectivamente exactamente el puñado de nodos que un jail típico necesita: `null`, `zero`, `random`, `urandom`, `crypto`, `ptmx`, `pts`, `fd` y los nodos PTY. Todo lo demás que existe en el `/dev` del host sencillamente no existe dentro del jail. El jail no puede abrirlo, no puede listarlo, no puede hacer stat sobre él.

Este es el mecanismo que mantiene a los jails dentro de sus límites. Es también el mecanismo con el que interactuarás si los nodos de tu driver deben, o no deben, aparecer dentro de un jail. Si un laboratorio necesita que `/dev/myfirst/0` sea accesible desde un jail, escribe un ruleset que lo desoculte. Si un despliegue debe mantener el nodo fuera de los jails por defecto, no haces nada: devfs lo ocultará por ti. El capítulo 8 retoma esto en detalle cuando lleguemos a la sección 10.

### chroot y devfs

Hay un contexto relacionado que merece una nota breve porque a veces confunde a quienes leen documentación más antigua. Un entorno `chroot(8)` normal **no** obtiene automáticamente su propio devfs. Si un script de shell hace chroot en `/compat/linux` e intenta abrir `/dev/null`, la apertura tiene éxito únicamente porque `/dev/null` existe en el host y es visible bajo la ruta del chroot a través de un bind mount, o porque se montó explícitamente un devfs allí. Si ninguna de las dos condiciones se cumple, la apertura falla con `ENOENT`.

Los jails son distintos porque construyen explícitamente una vista del sistema de archivos con devfs montado dentro. Un chroot es de más bajo nivel y deja la organización del sistema de archivos enteramente en manos de quien lo llama. La consecuencia práctica, para quienes escriben drivers, es que una prueba de regresión que se ejecute bajo `chroot` puede o no puede ver tu dispositivo, dependiendo de cómo esté configurado el chroot. En caso de duda, prueba dentro de un jail.

### /dev/fd y los enlaces simbólicos estándar

Algunas entradas bajo `/dev` no son cdevs en absoluto; son enlaces simbólicos mantenidos como parte del montaje de devfs. `/dev/stdin`, `/dev/stdout` y `/dev/stderr` se resuelven cada uno a través de `/dev/fd/0`, `/dev/fd/1` y `/dev/fd/2`, y estos a su vez son servidos por `fdescfs(5)` cuando está montado en `/dev/fd`. La combinación proporciona a los programas de usuario una forma fiable basada en rutas para referenciar el descriptor de archivo actual, lo que resulta útil ocasionalmente en scripts de shell y programas awk que quieren leer o escribir "sea lo que sea que sea el stdin del programa actual".

Estas entradas merecen una mención por dos razones. La primera es que son ejemplos de enlaces simbólicos dentro de devfs: `ls -l /dev/stdin` muestra `lrwxr-xr-x` y una flecha, no `crw-...`. La segunda es que sirven de recordatorio de que las entradas bajo `/dev` no son todas cdevs. La mayoría sí lo son; unas pocas no. Cuando el capítulo más adelante contraste `make_dev_alias(9)` con la directiva `link` en `devfs.conf`, esta es la distinción que subyace.

### Por qué la propiedad de "espejo activo" importa a los drivers

El hecho de que devfs sea un espejo activo del estado actual del kernel tiene varias implicaciones sobre cómo diseñas y depuras drivers. Cada uno de estos puntos volverá a aparecer en el capítulo; conviene enunciarlos con claridad ahora.

- **Un nodo que falta en `/dev` es un nodo que tu driver no creó.** Si esperabas que `/dev/myfirst/0` existiera y no existe, lo primero que debes comprobar es si `attach` se ejecutó y si `make_dev_s` devolvió cero. `dmesg` normalmente te lo dice.
- **Un nodo que permanece después de descargar el módulo es un nodo que tu driver no destruyó.** Eso no puede ocurrir si usaste `destroy_dev(9)` correctamente, pero es un marco útil: si la ruta sobrevive al `kldunload`, te faltó una llamada.
- **Un cambio de permisos que hayas hecho de forma interactiva no sobrevivirá a un reinicio.** devfs no tiene ningún registro en disco de lo que hiciste. Las herramientas persistentes para expresar política son `devfs.conf` y `devfs.rules`, y la sección 10 de este capítulo las cubre en profundidad.
- **Un jail ve un subconjunto filtrado.** Cuando razonas sobre seguridad o exposición de funcionalidades, asume que alguien acabará ejecutando tu driver con un jail activo en el mismo host. Si tu nodo tiene permisos demasiado abiertos y un ruleset permite que los jails lo vean, esa apertura tiene un radio de impacto mayor.

---

## cdev, vnode y descriptor de archivo

Abre un archivo de dispositivo y tres objetos del lado del kernel se colocan silenciosamente detrás de tu descriptor de archivo. Comprender este trío es la diferencia entre escribir un driver que funciona por casualidad y un driver cuyo ciclo de vida controlas de verdad.

El primer objeto es `struct cdev`, la **identidad del dispositivo en el lado del kernel**. Existe un `struct cdev` por cada nodo de dispositivo, independientemente de cuántos programas lo tengan abierto. Tu driver lo crea con `make_dev_s(9)` y lo destruye con `destroy_dev(9)`. El cdev lleva la información identificativa sobre el nodo: su nombre, su propietario, su modo, el `struct cdevsw` que despacha las llamadas al sistema, y dos ranuras controladas por el driver llamadas `si_drv1` y `si_drv2`. El capítulo 7 ya usó `si_drv1` para guardar el puntero al softc, y ese es con diferencia su uso más habitual.

El segundo objeto es un **vnode de devfs**. Los vnodes son los objetos VFS genéricos de FreeBSD que representan inodes abiertos del sistema de archivos. Cada nodo de dispositivo tiene un vnode debajo, igual que un archivo normal, y la capa VFS usa el vnode para enrutar las operaciones hacia el sistema de archivos correcto. Para un nodo de dispositivo, ese sistema de archivos es devfs, y devfs reenvía la operación al cdev.

El tercer objeto es el **descriptor de archivo** en sí, representado dentro del kernel por un `struct file`. A diferencia del cdev, existe un `struct file` por cada apertura, no uno por dispositivo. Aquí es donde vive el estado por apertura. Dos procesos que abren `/dev/myfirst0` comparten el mismo cdev pero obtienen estructuras de archivo separadas, y devfs sabe cómo mantener esas estructuras claramente diferenciadas.

Junta los tres y la ruta de una única llamada `read(2)` tiene el siguiente aspecto:

```text
user process
   read(fd, buf, n)
         |
         v
 file descriptor (struct file)  per-open state
         |
         v
 devfs vnode                     VFS routing
         |
         v
 struct cdev                     device identity
         |
         v
 cdevsw->d_read                  your driver's handler
```

Cada bloque del diagrama existe de forma independiente, y cada uno tiene una vida útil distinta. El cdev vive mientras tu driver lo mantenga con vida. El vnode vive mientras alguien tenga el nodo resuelto en la capa VFS. El `struct file` vive mientras el proceso de usuario mantenga su descriptor abierto. Cuando escribes un driver, solo estás rellenando la última fila de ese diagrama, pero conocer las filas superiores ayuda enormemente.

### Seguimiento de una única llamada read(2) a través de la pila

Recorre la historia una vez en prosa, con una llamada `read(2)` concreta como ancla. Un programa de usuario tiene esta línea:

```c
ssize_t n = read(fd, buf, 64);
```

Esto es lo que ocurre. El kernel recibe el syscall `read(2)` y busca `fd` en la tabla de descriptores de archivo del proceso que lo llama. Eso produce un `struct file`. El kernel detecta que el tipo del archivo es un archivo respaldado por vnode cuyo vnode reside en devfs, por lo que despacha a través del vector genérico de operaciones de archivo hacia el manejador de lectura de devfs.

devfs toma una referencia sobre el `struct cdev` subyacente, recupera el puntero a `struct cdevsw` de él, y llama a `cdevsw->d_read`. Esa es **tu** función. Dentro de ella inspeccionas el `struct uio` que el kernel preparó, examinas el dispositivo a través del argumento `struct cdev *dev`, y opcionalmente recuperas la estructura por apertura con `devfs_get_cdevpriv`. Cuando retornas, devfs libera su referencia sobre el cdev y la llamada de lectura se deshace de vuelta hasta el programa de usuario.

Del seguimiento se extraen algunos invariantes que merece la pena recordar:

- **Tu manejador nunca se ejecuta si el cdev ha desaparecido.** Entre que `destroy_dev(9)` retira el nodo y el momento en que el último llamador suelta su referencia, devfs simplemente rechaza nuevas operaciones.
- **Dos llamadas de dos procesos distintos pueden llegar a `d_read` simultáneamente.** Ni devfs ni la capa VFS serializan a los llamadores en tu nombre. El control de concurrencia es responsabilidad tuya, y la parte 3 de este libro está dedicada a ello.
- **El `struct file` al que estás sirviendo implícitamente está oculto para tu manejador.** No necesitas saber qué descriptor disparó la llamada; solo necesitas el cdev, el uio y, opcionalmente, el puntero cdevpriv.

Ese último punto es el que más valor aporta en la práctica. Al ocultar el descriptor del manejador, FreeBSD te ofrece una API limpia: toda la contabilidad por descriptor pasa por `devfs_set_cdevpriv` y `devfs_get_cdevpriv`, y el código de tu manejador se mantiene pequeño.

### Por qué esto importa para los principiantes

De este modelo se extraen dos consecuencias prácticas, y ambas volverán a aparecer en el próximo capítulo.

En primer lugar, **los punteros almacenados en el cdev se comparten entre todas las aperturas**. Si guardas un contador en `si_drv1`, todos los procesos que abran el nodo ven el mismo contador. Eso es perfecto para el estado global del driver, como el softc, y desastroso para el estado por sesión, como una posición de lectura.

En segundo lugar, **al kernel no le importa cuántas veces se abre tu dispositivo**. A menos que le indiques lo contrario, cualquier `open(2)` pasa directamente. Si necesitas acceso exclusivo, como hace el código del capítulo 7 mediante su flag `is_open`, tienes que imponerlo tú mismo. Si necesitas llevar un registro por apertura, ese registro se asocia al descriptor de archivo, no al cdev. Haremos ambas cosas antes de que termine el capítulo.

### Un vistazo más detallado a struct cdev

Has estado usando `struct cdev` a través de un puntero a lo largo de todo el capítulo 7. Ha llegado el momento de mirar su interior. La definición completa se encuentra en `/usr/src/sys/sys/conf.h`, y los campos importantes son estos:

```c
struct cdev {
        void            *si_spare0;
        u_int            si_flags;
        struct timespec  si_atime, si_ctime, si_mtime;
        uid_t            si_uid;
        gid_t            si_gid;
        mode_t           si_mode;
        struct ucred    *si_cred;
        int              si_drv0;
        int              si_refcount;
        LIST_ENTRY(cdev) si_list;
        LIST_ENTRY(cdev) si_clone;
        LIST_HEAD(, cdev) si_children;
        LIST_ENTRY(cdev) si_siblings;
        struct cdev     *si_parent;
        struct mount    *si_mountpt;
        void            *si_drv1, *si_drv2;
        struct cdevsw   *si_devsw;
        int              si_iosize_max;
        u_long           si_usecount;
        u_long           si_threadcount;
        union { ... }    __si_u;
        char             si_name[SPECNAMELEN + 1];
};
```

No todos los campos son relevantes para un driver de nivel principiante. Algunos sí, y saber qué representan ahorra horas la primera vez que te enfrentas a código desconocido.

**`si_name`** es el nombre con terminador nulo del nodo tal como lo ve devfs. Cuando pasas `"myfirst/%d"` y la unidad `0` a `make_dev_s`, este es el campo que termina conteniendo la cadena `myfirst/0`. La función auxiliar `devtoname(struct cdev *dev)` devuelve un puntero a este campo y es la herramienta adecuada para registros o salidas de depuración.

**`si_flags`** es un campo de bits que almacena indicadores de estado sobre el cdev. Los indicadores que tu driver tocará con más frecuencia son `SI_NAMED` (establecido cuando `make_dev*` ha colocado el nodo en devfs) y `SI_ALIAS` (establecido en los alias creados por `make_dev_alias`). El kernel los gestiona; tu código raramente, o nunca, escribe directamente en este campo. Un hábito de lectura útil: si ves un indicador `SI_*` desconocido en el driver de otra persona, búscalo en `/usr/src/sys/sys/conf.h` y lee el comentario de una sola línea.

**`si_drv1`** y **`si_drv2`** son los dos slots genéricos controlados por el driver. El capítulo 7 usó `si_drv1` para guardar el puntero al softc, y ese es el patrón más habitual. `si_drv2` está disponible para un segundo puntero cuando lo necesitas. Estos campos son tuyos; el kernel nunca los toca.

**`si_devsw`** es el puntero a la `struct cdevsw` que despacha las operaciones sobre este cdev. Es el enlace entre el nodo y tus manejadores.

**`si_uid`**, **`si_gid`**, **`si_mode`** almacenan el propietario y el modo anunciados. Se establecen a partir de los argumentos `mda_uid`, `mda_gid`, `mda_mode` que pasas a `make_dev_args_init`. Son mutables en principio, pero la forma correcta de cambiarlos es a través de `devfs.conf` o `devfs.rules`, no asignando directamente en la estructura.

**`si_refcount`**, **`si_usecount`**, **`si_threadcount`** son los tres contadores que devfs usa para mantener el cdev vivo mientras alguien pueda seguir tocándolo. `si_refcount` cuenta las referencias de larga duración (el cdev está listado en devfs, otros cdevs pueden tener alias de él). `si_usecount` cuenta los descriptores de archivo abiertos activos. `si_threadcount` cuenta los threads del kernel que en este momento están ejecutando dentro de un manejador de `cdevsw` para este cdev. Tu driver casi nunca los lee directamente; las rutinas `dev_ref`, `dev_rel`, `dev_refthread` y `dev_relthread` los gestionan en tu nombre. Lo que importa conceptualmente es que `destroy_dev(9)` se negará a terminar de destruir un cdev mientras `si_threadcount` sea distinto de cero; espera, durmiendo brevemente, hasta que cada manejador en vuelo haya retornado.

**`si_parent`** y **`si_children`** conectan un cdev en una relación padre-hijo. Así es como `make_dev_alias(9)` conecta un cdev alias con su primario y cómo ciertos mecanismos de clonación conectan nodos por apertura con su plantilla. La mayor parte del tiempo no interactuarás con estos campos; basta con saber que existen y que son una de las razones por las que devfs puede deshacer un alias de forma limpia cuando se destruye el primario.

**`si_flags & SI_ETERNAL`** merece una nota breve. Algunos nodos, en particular los que `null` crea para `/dev/null`, `/dev/zero` y `/dev/full`, están marcados como eternos con `MAKEDEV_ETERNAL_KLD`. El kernel se niega a destruirlos durante el funcionamiento normal. Cuando empieces a escribir módulos que exponen dispositivos en el momento de la carga del KLD y quieras que los nodos permanezcan vivos tras los intentos de descarga, este es el parámetro que debes usar. Para un driver en desarrollo activo, déjalo en paz.

### struct cdevsw: la tabla de despacho

Tu `cdevsw` del capítulo 7 rellenó unos pocos campos. La estructura real es más larga, y los campos restantes merecen al menos un repaso de reconocimiento, porque los encontrarás en drivers reales y tarde o temprano querrás usar algunos de ellos.

La estructura está definida en `/usr/src/sys/sys/conf.h` como:

```c
struct cdevsw {
        int              d_version;
        u_int            d_flags;
        const char      *d_name;
        d_open_t        *d_open;
        d_fdopen_t      *d_fdopen;
        d_close_t       *d_close;
        d_read_t        *d_read;
        d_write_t       *d_write;
        d_ioctl_t       *d_ioctl;
        d_poll_t        *d_poll;
        d_mmap_t        *d_mmap;
        d_strategy_t    *d_strategy;
        void            *d_spare0;
        d_kqfilter_t    *d_kqfilter;
        d_purge_t       *d_purge;
        d_mmap_single_t *d_mmap_single;
        /* fields managed by the kernel, not touched by drivers */
};
```

Veamos los campos uno a uno.

**`d_version`** es un sello ABI. Debe establecerse a `D_VERSION`, un valor definido unas líneas más arriba de la estructura. El kernel comprueba este campo al registrar el cdevsw y se negará a continuar si el sello no coincide. Olvidarlo es un error clásico de principiante: el driver compila, se carga y luego produce errores extraños en el primer `open` o directamente provoca un crash del sistema. Establece siempre `d_version = D_VERSION` como primer campo en cada `cdevsw` que escribas.

**`d_flags`** lleva un conjunto de indicadores válidos para todo el cdevsw. Los nombres de los indicadores están definidos junto al resto de la estructura. Los que vale la pena reconocer ahora son:

- `D_TAPE`, `D_DISK`, `D_TTY`, `D_MEM`: dan una pista al kernel sobre la naturaleza del dispositivo. Para la mayoría de los drivers, déjalo en cero.
- `D_TRACKCLOSE`: si está establecido, devfs llama a tu `d_close` en cada `close(2)` sobre un descriptor, no solo en el último cierre. Útil cuando quieres ejecutar de forma fiable la limpieza por descriptor incluso ante un `dup(2)`.
- `D_MMAP_ANON`: manejo especial para mapeos de memoria anónima. `/dev/zero` lo establece, que es como `mmap(..., /dev/zero, ...)` produce páginas rellenas de ceros.
- `D_NEEDGIANT`: fuerza el despacho de los manejadores de este cdevsw bajo el lock Giant. Los drivers modernos no deberían necesitar esto; si lo ves en código ajeno, trátalo como una marca histórica y no como un modelo a seguir.
- `D_NEEDMINOR`: indica que el driver usa `clone_create` para asignar números de menor a los cdevs clonados. No lo necesitarás en el capítulo 8.

**`d_name`** es la cadena de nombre base que el kernel usa cuando registra eventos relacionados con este cdevsw. También forma parte del patrón que el mecanismo `clone_create(9)` usa cuando sintetiza dispositivos clonados. Establécelo con una cadena corta y legible, como `"myfirst"`.

**`d_open`**, **`d_close`**: los límites de sesión. Se llaman cuando un programa de usuario invoca `open(2)` sobre el nodo o libera su último descriptor con `close(2)`. El capítulo 7 los presentó a ambos, y este capítulo perfecciona cómo usarlos.

**`d_fdopen`**: una alternativa a `d_open` para drivers que quieren recibir directamente la `struct file *`. Poco frecuente en drivers de nivel principiante. Ignóralo a menos que un capítulo futuro lo introduzca.

**`d_read`**, **`d_write`**: operaciones de flujo de bytes. El capítulo 7 las dejó como stubs. El capítulo 9 las implementa con `uiomove(9)`.

**`d_ioctl`**: operaciones de la ruta de control. El capítulo 25 tratará el diseño de `ioctl` en profundidad. Por ahora, reconoce el campo y sabe que ahí llegan los comandos estructurados procedentes de `ioctl(2)`.

**`d_poll`**: llamado por `poll(2)` para preguntar si el dispositivo está actualmente disponible para lectura o escritura. El capítulo 10 trata esto como parte de la historia de eficiencia de I/O.

**`d_kqfilter`**: llamado por la maquinaria de `kqueue(9)`. El mismo capítulo.

**`d_mmap`**, **`d_mmap_single`**: permite mapear el dispositivo en el espacio de direcciones de un proceso de usuario. Poco frecuente en drivers principiantes; se trata más adelante cuando resulta relevante.

**`d_strategy`**: llamado por algunas capas del kernel (en especial la antigua ruta de `physio(9)`) para entregar al driver un bloque de I/O como `struct bio`. No es relevante para los pseudodispositivos que escribirás en la Parte 2.

**`d_purge`**: llamado por devfs durante la destrucción si el cdev todavía tiene threads ejecutando dentro de sus manejadores. Un `d_purge` bien escrito despierta esos threads y los convence de retornar rápidamente para que la destrucción pueda continuar. La mayoría de los drivers simples no necesitan uno; el capítulo 10 vuelve sobre esto en el contexto del I/O bloqueante.

Cuando diseñes tu propio cdevsw, rellenas únicamente los campos que corresponden a las operaciones que tu dispositivo realmente admite. Cada campo con `NULL` es una denegación educada: el kernel lo interpreta como "esta operación no está soportada en este dispositivo" o como "usa el comportamiento por defecto", dependiendo de qué operación sea. No toques los campos sobrantes.

### El sello D_VERSION y por qué existe

Un breve aparte sobre `d_version` resulta útil, porque te ahorrará tiempo la primera vez que tu driver falle misteriosamente al registrarse.

La interfaz del kernel para las estructuras cdevsw ha evolucionado a lo largo de la vida de FreeBSD. Los campos se han añadido, eliminado o cambiado de tipo a través de las versiones principales. El sello `d_version` es el mecanismo del kernel para confirmar que tu módulo fue compilado contra una definición compatible de la estructura. La forma canónica de establecerlo es:

```c
static struct cdevsw myfirst_cdevsw = {
        .d_version = D_VERSION,
        /* ...remaining fields... */
};
```

La macro `D_VERSION` está definida en `/usr/src/sys/sys/conf.h` y será actualizada por el equipo del kernel siempre que la estructura cambie de manera que rompa el ABI. Los módulos construidos contra la nueva cabecera reciben el nuevo sello. Los módulos construidos contra una cabecera antigua reciben un sello antiguo y el kernel los rechaza.

Es un detalle pequeño que evita grandes dolores de cabeza. Establécelo siempre. Si alguna vez ves que el kernel imprime un error de discrepancia en la versión del cdevsw al cargar, tu entorno de compilación y tu kernel en ejecución se han desincronizado; recompila el módulo contra las cabeceras del kernel que pretendes ejecutar.

### El conteo de referencias al nivel de cdev

Los contadores que viste en `struct cdev` son el motor que mantiene la destrucción de dispositivos segura. Una forma sencilla de visualizarlos:

- `si_refcount` es el contador de "cuántas cosas en el kernel todavía retienen este cdev". Los alias, los clones y ciertas rutas de contabilidad lo incrementan. El cdev no puede liberarse realmente mientras este valor sea distinto de cero.
- `si_usecount` es el contador de "cuántos descriptores de archivo en espacio de usuario tienen este cdev abierto". devfs lo incrementa en un `open(2)` exitoso y lo decrementa en `close(2)`. Tu driver nunca lo toca directamente.
- `si_threadcount` es el contador de "cuántos threads del kernel están ahora mismo ejecutando dentro de uno de mis manejadores de `cdevsw`". `dev_refthread(9)` lo incrementa cuando devfs entra en un manejador en tu nombre y `dev_relthread(9)` lo decrementa cuando el manejador retorna. Tu driver nunca lo toca directamente.

La regla que hace todo esto utilizable es: `destroy_dev(9)` bloqueará hasta que `si_threadcount` caiga a cero y no retornará hasta que ningún manejador más pueda ser invocado para este cdev. Así es como `destroy_dev` puede garantizar que, después de retornar, tus manejadores no volverán a ser llamados. La sección más adelante en este capítulo titulada "Destrucción segura de cdevs" vuelve sobre esta garantía y los casos en los que necesitas su variante más robusta, `destroy_dev_drain(9)`.

### Una vuelta más sobre los tiempos de vida

Con todo eso en mente, el diagrama de la subsección anterior tiene un poco más de significado que la primera vez que lo viste. El cdev es un objeto del kernel de larga duración cuyo tiempo de vida está bajo el control de tu driver. El vnode es un objeto de la capa VFS que vive solo mientras la capa del sistema de archivos lo necesite. La `struct file` es un objeto de corta duración por apertura que vive solo mientras el proceso mantiene el descriptor. Y por debajo de los tres, los contadores descritos más arriba los mantienen honestos.

No necesitas memorizar nada de eso. Necesitas reconocer la forma. Cuando más adelante leas un driver y veas `dev_refthread` o `si_refcount`, recordarás para qué sirven. Cuando veas `destroy_dev` dormir en un depurador, reconocerás que está esperando a que `si_threadcount` baje a cero. Ese reconocimiento es lo que convierte el código del kernel de un puzzle en algo sobre lo que puedes razonar.



## Permisos, propietario y modo

Cuando tu driver llama a `make_dev_s(9)`, tres campos de la `struct make_dev_args` deciden lo que mostrará `ls -l /dev/yournode`:

```c
args.mda_uid  = UID_ROOT;
args.mda_gid  = GID_WHEEL;
args.mda_mode = 0600;
```

`UID_ROOT`, `UID_BIN`, `UID_UUCP`, `UID_NOBODY`, `GID_WHEEL`, `GID_KMEM`, `GID_TTY`, `GID_OPERATOR`, `GID_DIALER` y otros nombres relacionados están definidos en `/usr/src/sys/sys/conf.h`. Usa esas constantes en lugar de números directos cuando exista una identidad bien conocida. Hace que tu driver sea más fácil de leer y te protege de una derivación silenciosa si el valor numérico cambia alguna vez.

El modo es la triple de permisos UNIX clásica. El significado de cada bit es el mismo que para un archivo normal, con la salvedad de que los dispositivos no tienen en cuenta el bit de ejecución. Algunas combinaciones aparecen con frecuencia:

- `0600`: lectura y escritura para el propietario. El valor predeterminado más seguro para un driver que todavía está en desarrollo.
- `0660`: lectura y escritura para el propietario y el grupo. Apropiado cuando tienes un grupo privilegiado bien definido, como `operator` o `dialer`.
- `0644`: lectura y escritura para el propietario, todo el mundo puede leer. Poco habitual en dispositivos de control; a veces apropiado para nodos de solo lectura o de tipo generador de bytes aleatorios.
- `0666`: todos pueden leer y escribir. Se usa únicamente para fuentes intencionalmente inocuas como `/dev/null` y `/dev/zero`. No recurras a este modo a menos que tengas una razón real para ello.

La regla general es sencilla: pregúntate «¿quién necesita realmente acceder a este nodo?» y codifica esa respuesta, sin añadir nada más. Ampliar los permisos más adelante es fácil. Restringirlos después de que los usuarios hayan llegado a depender del modo más abierto no lo es.

### De dónde viene el modo

Vale la pena ser explícito sobre quién decide el modo final del nodo. Tres actores tienen voz en el asunto:

1. **Tu driver**, a través de los campos `mda_uid`, `mda_gid` y `mda_mode` en el momento de llamar a `make_dev_s()`. Esta es la línea base.
2. **`/etc/devfs.conf`**, que puede aplicar un ajuste estático puntual cuando aparece un nodo. Es la forma habitual en que un operador restringe o amplía los permisos en una ruta concreta.
3. **`/etc/devfs.rules`**, que puede aplicar ajustes basados en reglas, normalmente para filtrar lo que ve una jail.

Si el driver establece `0600` y no se configura nada más, verás `0600`. Si el driver establece `0600` y `devfs.conf` indica `perm myfirst/0 0660`, verás `0660` en ese nodo. El kernel es el mecanismo; la configuración del operador es la política.

### Grupos conocidos que encontrarás

FreeBSD incluye un pequeño conjunto de grupos bien conocidos que aparecen repetidamente en la propiedad de los dispositivos. Cada uno tiene una constante asociada en `/usr/src/sys/sys/conf.h`. Este breve repaso te ayudará a elegir el adecuado rápidamente:

- **`GID_WHEEL`** (`wheel`). Administradores de confianza. La opción predeterminada más segura cuando no tienes claro quién debería tener acceso más allá de root.
- **`GID_OPERATOR`** (`operator`). Usuarios que ejecutan herramientas de operación pero no son administradores completos. Se usa habitualmente para dispositivos que requieren supervisión humana pero sin necesidad de ejecutar `sudo` cada vez.
- **`GID_DIALER`** (`dialer`). Históricamente para acceso dial-out por puerto serie. Sigue usándose en nodos TTY que necesitan programas de marcado en espacio de usuario.
- **`GID_KMEM`** (`kmem`). Acceso de lectura a la memoria del kernel a través de nodos del estilo `/dev/kmem`. Muy sensible; rara vez es la respuesta correcta para un driver nuevo.
- **`GID_TTY`** (`tty`). Propietario para dispositivos de terminal.

Cuando exista un grupo con nombre adecuado, úsalo. Si ninguno encaja, deja el grupo como `wheel` y añade entradas en `devfs.conf` para los sitios que necesiten su propia agrupación. Inventar un grupo completamente nuevo dentro del driver casi nunca merece la pena.

### Un ejemplo práctico

Supongamos que la línea base del driver es `UID_ROOT`, `GID_WHEEL`, `0600`, y quieres permitir que un usuario de laboratorio concreto lea y escriba a través de un grupo controlado. La secuencia sería la siguiente.

Con el driver cargado y sin entradas en `devfs.conf`:

```sh
% ls -l /dev/myfirst/0
crw-------  1 root  wheel     0x5a Apr 17 10:02 /dev/myfirst/0
```

Añade una sección a `/etc/devfs.conf`:

```text
own     myfirst/0       root:operator
perm    myfirst/0       0660
```

Aplícala e inspecciona de nuevo:

```sh
% sudo service devfs restart
% ls -l /dev/myfirst/0
crw-rw----  1 root  operator  0x5a Apr 17 10:02 /dev/myfirst/0
```

El driver no se recargó. El cdev en el kernel es el mismo objeto. Solo cambiaron la propiedad y el modo que se muestran, y cambiaron porque un archivo de política le indicó a devfs que los cambiara. Este es el modelo de capas que quieres: el driver incluye una línea base defendible, y el operador ajusta la vista.

### Casos de estudio del árbol

Conviene dedicar un momento a los permisos que anuncian los dispositivos reales de FreeBSD, porque las decisiones de esos drivers no son accidentales. Cada una es una pequeña decisión de diseño, y cada una es coherente con el modelo de amenaza para ese tipo de nodo.

`/dev/null` y `/dev/zero` se publican con modo `0666`, `root:wheel`. Cualquier usuario del sistema, privilegiado o no, puede abrirlos y leer o escribir a través de ellos. Es la elección correcta porque los datos que gestionan son trivialmente inagotables (cero bytes de salida, bytes descartados de entrada, sin estado de hardware, sin secretos). Restringirlos más rompería una larga lista de scripts, herramientas e idiomas de programación que dependen de su disponibilidad universal. El código que los crea está en `/usr/src/sys/dev/null/null.c`, y merece la pena echar un vistazo a los argumentos de `make_dev_credf(9)` que allí aparecen.

`/dev/random` suele tener el modo `0644`, legible por cualquiera, escribible solo por root. El acceso de lectura es deliberadamente amplio porque muchos programas en espacio de usuario necesitan entropía. El acceso de escritura es restringido porque alimentar el pool de entropía es una operación privilegiada.

`/dev/mem` y `/dev/kmem` tienen históricamente el modo `0640`, propietario `root`, grupo `kmem`. El grupo existe precisamente para que las herramientas de monitorización privilegiadas puedan vincularse a ellos sin ejecutarse como root. El modo es restrictivo porque los nodos exponen memoria directa; un `/dev/mem` legible sin restricciones sería un desastre. Si alguna vez ves un driver con un modo tan permisivo por defecto para un nodo que contiene estado de hardware o memoria del kernel, trátalo como un defecto.

`/dev/pf`, el nodo de control del filtro de paquetes, tiene el modo `0600`, propietario `root`, grupo `wheel`. Un usuario que puede escribir en `/dev/pf` puede cambiar las reglas del cortafuegos. No existe un modo más amplio aceptable; toda la finalidad de la interfaz es centralizar la configuración de red privilegiada, y cualquier cosa más permisiva convertiría el cortafuegos en un caos.

`/dev/bpf*`, los nodos de Berkeley Packet Filter, tienen el modo `0600`, propietario `root`, grupo `wheel`. Un lector de `/dev/bpf*` ve todos los paquetes en una interfaz conectada. Eso es un privilegio inequívoco, y el permiso lo refleja.

Los nodos TTY bajo `/dev/ttyu*` y superficies de hardware serie similares suelen tener el modo `0660`, propietario `uucp`, grupo `dialer`. El grupo `dialer` existe para permitir que un conjunto de usuarios de confianza ejecuten programas de marcado sin `sudo`. El conjunto de permisos es el más restrictivo que aún permite que funcione el flujo de trabajo previsto.

El patrón es fácil de enunciar: **el sistema base de FreeBSD nunca elige permisos amplios para un dispositivo a menos que los datos del otro lado sean inofensivos**. Cuando diseñes un nodo propio, usa ese patrón como verificación mental. Si tu nodo contiene datos que podrían perjudicar a alguien, restringe el modo. Si contiene datos trivialmente regenerables y descartables, ampliarlo es defendible; hazlo igualmente solo cuando haya una razón para ello.

### El principio de mínimo privilegio aplicado a los archivos de dispositivo

"Mínimo privilegio" es una expresión sobreutilizada, pero es exactamente apropiada cuando se aplica a archivos de dispositivo. Tú, como autor del driver, decides quién puede hablar con tu código desde el espacio de usuario, y tú estableces el límite inferior. Cada elección más amplia de lo necesario es una elección que invita a errores en el futuro.

Una lista de verificación práctica para cada nuevo nodo que diseñes:

1. **Identifica al consumidor principal en una frase.** "El demonio de monitorización lee el estado cada segundo." "La herramienta de control invoca ioctl para enviar configuración." "Los usuarios del grupo operator pueden leer contadores de paquetes en crudo." Si no puedes nombrar al consumidor, no puedes establecer los permisos; estarás adivinando.
2. **Deriva el modo a partir de esa frase.** Un demonio de monitorización que se ejecuta como `root:wheel` y lee una vez por segundo necesita `0600`. Una herramienta de control que ejecuta un subconjunto de administradores privilegiados necesita `0660` con un grupo dedicado. Un nodo de estado de solo lectura consumido por paneles de control sin privilegios necesita `0644`.
3. **Documenta el razonamiento en un comentario junto a la línea `mda_mode`.** Los futuros mantenedores te lo agradecerán. Los futuros auditores aún más.
4. **Usa `UID_ROOT` por defecto.** Casi nunca hay razón para que el propietario de un nodo creado por un driver sea cualquier otra cosa, salvo que el driver modele explícitamente una identidad de demonio no root.

El hábito contrario, que el libro quiere prevenir, es el impulso de "abrirlo todo y restringir después". Los permisos de un driver publicado son muy difíciles de restringir, porque cuando alguien lo advierte, el flujo de trabajo de algún usuario ya depende del modo permisivo y la restricción le arruina el día. Empieza restringido. Amplía cuando hayas revisado una solicitud real.

### Transición de un modo permisivo a uno restrictivo

En ocasiones heredarás un driver que era demasiado abierto y necesita restringirse. El enfoque correcto tiene tres etapas:

**Etapa 1: Anúncialo.** Incluye el cambio previsto en las notas de la versión, en el log del kernel del driver al primer attach, y en el canal orientado al operador que use tu proyecto. Invita a recibir comentarios durante al menos un ciclo de publicación.

**Etapa 2: Ofrece un camino de transición.** Ya sea una entrada en `devfs.conf` que restaure el modo anterior para quienes lo necesiten, o un sysctl que el driver lea al hacer el attach para elegir su modo predeterminado. La propiedad importante es que un sitio con una necesidad legítima de conservar el modo antiguo pueda hacerlo sin bifurcar el driver.

**Etapa 3: Cambia el valor predeterminado.** En la siguiente versión, una vez terminada la ventana de transición, cambia el propio `mda_mode` del driver al valor más restrictivo. La vía de escape a través de `devfs.conf` permanece para los sitios que la necesiten; todos los demás obtienen el valor predeterminado más restrictivo.

Nada de eso es específico de FreeBSD; así es como cualquier proyecto bien gestionado maneja los cambios de interfaz incompatibles con versiones anteriores. Vale la pena mencionarlo aquí porque los permisos de archivos de dispositivo tienen exactamente esta propiedad: son parte de la interfaz pública de tu driver.

### Qué son realmente las constantes uid y gid

Las constantes `UID_*` y `GID_*` definidas en `/usr/src/sys/sys/conf.h` **no** están garantizadas para coincidir con la base de datos de usuarios y grupos en todos los sistemas. Los nombres elegidos en el encabezado corresponden a identidades que el sistema base de FreeBSD reserva en `/etc/passwd` y `/etc/group`, pero un sistema modificado localmente podría en teoría renumerarlos, o un producto construido sobre FreeBSD podría añadir los suyos propios. En la práctica, en todos los sistemas FreeBSD que vayas a manejar, las constantes coinciden.

La disciplina a mantener es sencilla: usa el nombre simbólico cuando exista uno, y consúltalo en el encabezado antes de inventar una nueva identidad. El encabezado define actualmente al menos estos:

- IDs de usuario: `UID_ROOT` (0), `UID_BIN` (3), `UID_UUCP` (66), `UID_NOBODY` (65534).
- IDs de grupo: `GID_WHEEL` (0), `GID_KMEM` (2), `GID_TTY` (4), `GID_OPERATOR` (5), `GID_BIN` (7), `GID_GAMES` (13), `GID_VIDEO` (44), `GID_RT_PRIO` (47), `GID_ID_PRIO` (48), `GID_DIALER` (68), `GID_NOGROUP` (65533), `GID_NOBODY` (65534).

Si necesitas una identidad que no está en la lista, probablemente el sistema base no tenga ninguna reservada para ese caso. En ese caso, deja la propiedad como `UID_ROOT`/`GID_WHEEL` y permite que los operadores mapeen tu nodo a su propio grupo local mediante `devfs.conf`. Inventar un nuevo grupo dentro del driver casi siempre es la decisión equivocada.

### Política de tres capas: driver, devfs.conf, devfs.rules

Cuando combinas la línea base del driver con `devfs.conf` y `devfs.rules`, obtienes un modelo de política por capas que merece verse de principio a fin. Considera un dispositivo que el driver crea con `root:wheel 0600`. Tres capas actúan sobre él:

- **Capa 1, el propio driver**: establece la línea base. Todo `/dev/myfirst/0` en cualquier montaje de devfs comienza con `root:wheel 0600`.
- **Capa 2, `/etc/devfs.conf`**: se aplica una vez por montaje devfs del host, normalmente al arrancar. Puede cambiar la propiedad, el modo o añadir un enlace simbólico. En el host en ejecución, tras `service devfs restart`, el nodo podría aparecer como `root:operator 0660`.
- **Capa 3, `/etc/devfs.rules`**: se aplica en el momento del montaje en función del conjunto de reglas asociado al montaje. Una jail cuyo montaje devfs usa el conjunto de reglas `10` ve el subconjunto filtrado y posiblemente modificado. El mismo nodo podría estar oculto dentro de la jail, o podría estar visible con ajustes adicionales de modo y grupo.

La consecuencia práctica de esta estratificación es que **el mismo cdev puede verse diferente en distintos lugares al mismo tiempo**. En el host podría ser `0660` propiedad de `operator`. En una jail podría ser `0640` propiedad de una identidad de usuario enjaulada. En otra jail podría no existir en absoluto.

Eso es una característica, no un error. Permite publicar un driver con una línea base estricta y que los operadores la amplíen por entorno sin editar tu código. La sección 10 del capítulo 8 recorre las tres capas con un ejemplo completo.



## Nomenclatura, números de unidad y subdirectorios

El argumento con formato printf de `make_dev_s(9)` determina dónde aparece el nodo en `/dev`. En el capítulo 7 utilizaste:

```c
error = make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
```

Eso produjo `/dev/myfirst0`. En eso se ocultan dos detalles.

El primero es `sc->unit`. Es el número de unidad de Newbus que FreeBSD asigna a tu instancia de dispositivo. Con una instancia conectada, obtienes `0`. Si el driver admitiera varias instancias, podrías ver `myfirst0`, `myfirst1`, y así sucesivamente.

El segundo detalle es el propio formato de cadena. Los nombres de dispositivo son rutas relativas a `/dev`, y pueden contener barras. Un nombre como `"myfirst/%d"` no produce un nombre de archivo extraño con una barra dentro; devfs interpreta la barra como lo haría un sistema de archivos, crea el directorio intermedio si es necesario y coloca el nodo dentro. De modo que:

- `"myfirst%d"` con unidad `0` da `/dev/myfirst0`.
- `"myfirst/%d"` con unidad `0` da `/dev/myfirst/0`.
- `"myfirst/control"` da `/dev/myfirst/control`, sin ningún número de unidad.

Agrupar nodos relacionados en un subdirectorio es un patrón habitual en drivers que exponen más de una superficie. Piensa en `/dev/led/*` de `/usr/src/sys/dev/led/led.c`, o en `/dev/pf`, `/dev/pflog*` y sus compañeros del subsistema de filtrado de paquetes. El subdirectorio hace evidente la relación de un vistazo, mantiene ordenado el nivel superior de `/dev` y permite que un operador conceda o deniegue acceso a todo el conjunto con una sola línea en `devfs.conf`.

En este capítulo adoptarás este patrón para `myfirst`. La ruta principal de datos pasa de `/dev/myfirst0` a `/dev/myfirst/0`. Después añadirás un alias para que la ruta anterior siga funcionando en los scripts de laboratorio que recuerden el diseño anterior.

### Nombres en el árbol real de FreeBSD

Explorar el `/dev` de un sistema FreeBSD en funcionamiento es instructivo por sí solo, porque las convenciones de nombres que verás allí fueron moldeadas por las mismas presiones a las que se enfrentará tu driver. Un breve recorrido, agrupado por tema:

- **Nombres de dispositivo directos.** `/dev/null`, `/dev/zero`, `/dev/random`, `/dev/urandom`. Un cdev por nodo, en el nivel superior, con nombres cortos y estables. Adecuados para singletons sin jerarquía.
- **Nombres con número de unidad.** `/dev/bpf0`, `/dev/bpf1`, `/dev/ttyu0`, `/dev/md0`. Un cdev por instancia, numerados desde cero. La cadena de formato tiene el aspecto de `"bpf%d"` y el driver gestiona los números de unidad.
- **Subdirectorios por driver.** `/dev/led/*`, `/dev/pts/*`, `/dev/ipmi*` en algunas configuraciones. Se utilizan cuando un único driver expone muchos nodos relacionados. Simplifican la política del operador: una sola entrada en `devfs.conf` o en `devfs.rules` puede cubrir todo el conjunto.
- **Nodos de datos y de control separados.** `/dev/bpf` (el punto de entrada de clonado) más clones por apertura, `/dev/fido/*` para dispositivos FIDO, y así sucesivamente. Se usan cuando un driver necesita semánticas distintas para el descubrimiento frente al acceso a datos.
- **Nombres con alias para mayor comodidad.** `/dev/stdin`, `/dev/stdout`, `/dev/stderr` son enlaces simbólicos que devfs proporciona para los descriptores de archivo del proceso actual. `/dev/random` y `/dev/urandom` estuvieron aliasados en su momento; en FreeBSD moderno son nodos separados servidos por el mismo driver de aleatoriedad, aunque la historia sigue siendo visible.

No necesitas memorizar estos patrones. Sí necesitas reconocerlos, porque cuando leas drivers existentes todo cobrará más sentido una vez que las convenciones de nombres tengan nombre.

### Múltiples nodos por dispositivo

Algunos drivers exponen un único nodo y eso es suficiente. Otros drivers exponen varios, cada uno con semánticas distintas. Una división habitual es:

- Un **nodo de datos** que transporta la carga principal (lecturas, escrituras, mmaps) y está pensado para un uso de alto rendimiento.
- Un **nodo de control** que transporta el tráfico de gestión (configuración, estado, reinicio) y que normalmente tiene permisos de lectura para el grupo, de modo que las herramientas de monitorización puedan acceder a él.

Cuando un driver hace esto, llama a `make_dev_s(9)` dos veces en `attach()` y conserva ambos punteros cdev en el softc. En el Capítulo 8 te quedarás con un nodo de datos más un alias, pero merece la pena conocer el patrón ahora para reconocerlo cuando lo veas.

El Laboratorio 8.5 construye una variante mínima de dos nodos de `myfirst` con un nodo de datos en `/dev/myfirst/0` y un nodo de control en `/dev/myfirst/0.ctl`. Cada nodo tiene su propio `cdevsw` y su propio modo de permisos. El laboratorio está pensado para mostrar qué aspecto tiene el patrón en el código; la mayoría de tus drivers en capítulos posteriores lo usarán.

### La familia make_dev en profundidad

Has utilizado `make_dev_s(9)` para cada nodo que has creado hasta ahora. FreeBSD proporciona en realidad una pequeña familia de funciones `make_dev*`, cada una con una ergonomía ligeramente distinta. La lectura de drivers existentes te expondrá a todas ellas, y saber cuándo usar cuál ahorra problemas más adelante.

Las declaraciones completas se encuentran en `/usr/src/sys/sys/conf.h`. En orden creciente de modernidad:

```c
struct cdev *make_dev(struct cdevsw *_devsw, int _unit, uid_t _uid, gid_t _gid,
                      int _perms, const char *_fmt, ...);

struct cdev *make_dev_cred(struct cdevsw *_devsw, int _unit,
                           struct ucred *_cr, uid_t _uid, gid_t _gid, int _perms,
                           const char *_fmt, ...);

struct cdev *make_dev_credf(int _flags, struct cdevsw *_devsw, int _unit,
                            struct ucred *_cr, uid_t _uid, gid_t _gid, int _mode,
                            const char *_fmt, ...);

int make_dev_p(int _flags, struct cdev **_cdev, struct cdevsw *_devsw,
               struct ucred *_cr, uid_t _uid, gid_t _gid, int _mode,
               const char *_fmt, ...);

int make_dev_s(struct make_dev_args *_args, struct cdev **_cdev,
               const char *_fmt, ...);
```

Veámoslas una por una.

**`make_dev`** es la forma original con argumentos posicionales. Devuelve el nuevo puntero cdev directamente, o provoca un pánico ante cualquier error. El hecho de provocar un pánico ante un error es una fuerte indicación de que está pensada para rutas de código que no pueden recuperarse, como la inicialización muy temprana de dispositivos verdaderamente eternos. Evítala en drivers nuevos. Sigue en el árbol únicamente porque drivers más antiguos la usan y porque algunos de esos drivers son lugares donde un pánico temprano es genuinamente aceptable.

**`make_dev_cred`** añade un argumento de credencial (`struct ucred *`). La credencial la utiliza devfs al aplicar reglas; le indica al sistema «este cdev fue creado con esta credencial» a efectos de coincidencia de reglas. La mayoría de los drivers pasan `NULL` como credencial y obtienen el comportamiento predeterminado. Verás esta forma en drivers que clonan dispositivos bajo demanda en respuesta a peticiones del usuario; en otros contextos no es habitual.

**`make_dev_credf`** extiende `make_dev_cred` con una palabra de flags. Es el primer miembro de la familia que te permite indicar «no entres en pánico si esto falla; devuelve `NULL` para que pueda gestionarlo».

**`make_dev_p`** es un equivalente funcional de `make_dev_credf` con una convención de valor de retorno más limpia: devuelve un valor `errno` (cero en caso de éxito) y escribe el nuevo puntero cdev a través de un parámetro de salida. Es la forma más ampliamente utilizada en bases de código modernas escritas antes de que existiera `make_dev_s`.

**`make_dev_s`** es la forma moderna recomendada. Acepta una `struct make_dev_args` previamente rellenada (inicializada con `make_dev_args_init_impl` y descrita más adelante) y escribe el puntero cdev a través de un parámetro de salida. Devuelve un valor `errno`, cero en caso de éxito. La razón por la que el libro la utiliza es sencilla: es la forma más fácil de leer, la más fácil de extender (añadir un nuevo campo a la estructura de argumentos es compatible con el ABI) y la más fácil de verificar errores.

La estructura de argumentos, también de `/usr/src/sys/sys/conf.h`:

```c
struct make_dev_args {
        size_t         mda_size;
        int            mda_flags;
        struct cdevsw *mda_devsw;
        struct ucred  *mda_cr;
        uid_t          mda_uid;
        gid_t          mda_gid;
        int            mda_mode;
        int            mda_unit;
        void          *mda_si_drv1;
        void          *mda_si_drv2;
};
```

`mda_size` se establece automáticamente mediante `make_dev_args_init(a)`; nunca debes tocarlo. `mda_flags` contiene los flags `MAKEDEV_*` descritos más adelante. `mda_devsw`, `mda_cr`, `mda_uid`, `mda_gid`, `mda_mode` y `mda_unit` se corresponden con los argumentos posicionales de las formas más antiguas. `mda_si_drv1` y `mda_si_drv2` permiten pre-rellenar los slots de puntero del driver en el cdev resultante, que es la forma de evitar la ventana en la que `si_drv1` podría estar brevemente a `NULL` tras el retorno de `make_dev_s` pero antes de que se le asigne un valor. Rellena siempre `mda_si_drv1` antes de la llamada.

### ¿Qué forma deberías usar?

Para drivers nuevos, **usa `make_dev_s`**. Todos los ejemplos de este libro la usan, y todo driver que escribas para ti mismo debería hacer lo mismo a menos que una razón muy específica obligue a lo contrario.

Para leer código existente, reconócelas todas. Si encuentras un driver que llama a `make_dev(...)` e ignora su valor de retorno, estás ante un driver que es anterior a las API modernas o ante un driver cuyos autores decidieron que un pánico en caso de fallo es aceptable. Ambas situaciones son defendibles en su contexto; ninguna es el valor predeterminado correcto para código nuevo.

### Los flags MAKEDEV_*

Los flags que se pueden combinar con OR en `mda_flags` (o pasarse como primer argumento a `make_dev_p` y a `make_dev_credf`) están definidos en `/usr/src/sys/sys/conf.h`. Cada uno tiene un significado específico:

- **`MAKEDEV_REF`**: incrementa el recuento de referencias del cdev resultante en uno, además de la referencia habitual. Se usa cuando quien llama planea conservar el puntero cdev a largo plazo a través de eventos que normalmente reducirían la referencia. Poco frecuente en drivers de nivel principiante.
- **`MAKEDEV_NOWAIT`**: indica al asignador que no espere si la memoria es escasa. Ante una condición de falta de memoria, la función devuelve `ENOMEM` (para `make_dev_s`) o `NULL` (para las formas más antiguas) en lugar de bloquearse. Úsalo solo si quien llama no puede permitirse dormir.
- **`MAKEDEV_WAITOK`**: el inverso. Le indica al asignador que es seguro dormir esperando memoria. Este es el comportamiento predeterminado para `make_dev` y `make_dev_s`, por lo que rara vez es necesario especificarlo.
- **`MAKEDEV_ETERNAL`**: marca el cdev como nunca destruible. devfs se negará a atender `destroy_dev(9)` sobre él durante la operación normal. Lo usan los dispositivos eternos del kernel como `null`, `zero` y `full`. No lo establezcas en un driver que planees descargar.
- **`MAKEDEV_CHECKNAME`**: pide a la función que valide el nombre del nodo según las reglas de devfs antes de crearlo. En caso de fallo devuelve un error en lugar de crear un cdev con un nombre incorrecto. Útil en rutas de código que sintetizan nombres a partir de la entrada del usuario.
- **`MAKEDEV_WHTOUT`**: crea una entrada de tipo «whiteout», utilizada junto con sistemas de archivos apilados para enmascarar una entrada subyacente. No es algo que vayas a encontrar en el trabajo con drivers.
- **`MAKEDEV_ETERNAL_KLD`**: una macro que se expande a `MAKEDEV_ETERNAL` cuando el código se compila fuera de un módulo cargable y a cero cuando se compila como KLD. Esto permite que el código fuente compartido de un dispositivo (como `null`) establezca el flag al compilarse estáticamente y lo elimine al cargarse como módulo, de modo que el módulo siga siendo descargable.

Para un driver típico de nivel principiante, el campo de flags es cero, que es lo que usan los ejemplos de `myfirst` en el árbol de acompañamiento. `MAKEDEV_CHECKNAME` merece la pena usarlo cuando el nombre del nodo se construye a partir de la entrada del usuario o de una cadena cuya procedencia no controlas completamente; para un driver que pasa una cadena de formato constante como `"myfirst/%d"`, el flag no aporta nada útil.

### Los d_flags de cdevsw

Independientemente de los flags `MAKEDEV_*`, el propio `cdevsw` lleva un campo `d_flags` que determina cómo devfs y el resto de la maquinaria del kernel tratan el cdev. Estos flags se enumeraron en el recorrido por cdevsw unas secciones atrás; esta sección es el lugar para entender cuándo establecerlos.

**`D_TRACKCLOSE`** es el flag que más probablemente necesitarás en el Capítulo 8. De forma predeterminada, devfs llama a tu `d_close` únicamente cuando se libera el último descriptor de archivo que hace referencia al cdev. Si un proceso ha llamado a `dup(2)` o a `fork(2)` y dos descriptores comparten la apertura, `d_close` se dispara una sola vez, al final del todo. Eso es a menudo lo que quieres. No es lo que quieres si necesitas un hook de cierre fiable por descriptor. Establecer `D_TRACKCLOSE` hace que devfs llame a `d_close` en cada `close(2)` sobre cada descriptor. Para un driver que usa `devfs_set_cdevpriv(9)` para el estado por apertura, el destructor suele ser el hook más adecuado; `D_TRACKCLOSE` sigue siendo útil cuando la semántica de tu dispositivo requiere genuinamente que cada cierre sea observable.

**`D_MEM`** etiqueta el cdev como un dispositivo de tipo memoria; el propio `/dev/mem` lo establece. Cambia cómo ciertas rutas del kernel tratan la I/O hacia el nodo.

**`D_DISK`**, **`D_TAPE`**, **`D_TTY`** son indicadores de la categoría del dispositivo. Los drivers modernos generalmente no los establecen, porque GEOM es el propietario de los discos, el subsistema TTY es el propietario de los TTYs y los dispositivos de cinta se enrutan a través de su propia capa. Los verás en drivers heredados.

**`D_MMAP_ANON`** altera la forma en que el mapeo del dispositivo produce páginas. El dispositivo `zero` lo establece; mapear `/dev/zero` produce páginas anónimas rellenas de ceros. Merece la pena reconocerlo; no necesitarás establecerlo hasta que escribas un driver que desee la misma semántica.

**`D_NEEDGIANT`** solicita que todos los manejadores de `cdevsw` para este cdev se despachen bajo el lock Giant. Existe como red de seguridad para drivers que no han sido auditados para SMP. Un driver nuevo no debería establecer este flag. Si lo ves en código escrito después de 2010 aproximadamente, trátalo con desconfianza.

**`D_NEEDMINOR`** le indica a devfs que el driver usa `clone_create(9)` para asignar números de minor bajo demanda. No lo encontrarás hasta que escribas un driver de clonado, que está fuera del ámbito de este capítulo.

Los flags que establecerás en `myfirst` son, en la mayoría de las versiones, ninguno. Una vez que el Capítulo 8 añada estado por apertura, el driver seguirá sin necesitar `D_TRACKCLOSE` porque el destructor de cdevpriv cubre la necesidad de limpieza por descriptor.

### Longitud y caracteres del nombre

`make_dev_s` acepta un formato estilo printf y produce un nombre que devfs almacena en el campo `si_name` del cdev. El tamaño de ese campo es `SPECNAMELEN + 1`, y `SPECNAMELEN` es actualmente 255. Un nombre más largo que eso es un error.

Además de la longitud, un nombre debe ser aceptable como ruta del sistema de archivos bajo devfs. Eso significa que no debe contener bytes nulos, no debe usar `.` o `..` como componentes, y no debe usar caracteres que las shells o los scripts interpreten de forma especial. El conjunto más seguro son las letras ASCII en minúsculas, los dígitos y los tres separadores `/`, `-` y `.`. Otros caracteres a veces funcionarán y a veces no; si alguna vez sientes la tentación de usar espacios, dos puntos o caracteres no ASCII en un nombre de dispositivo, detente y elige un nombre más sencillo.

### Números de unidad: de dónde vienen

Los números de unidad son enteros pequeños que distinguen instancias del mismo driver. Aparecen en el nombre del dispositivo (`myfirst0`, `myfirst1`), en las ramas de `sysctl` (`dev.myfirst.0`, `dev.myfirst.1`) y en el campo `si_drv0` del cdev.

Hay dos formas habituales de asignarlos:

**Asignación por Newbus.** Cuando tu driver se adjunta a un bus y Newbus instancia un dispositivo, el bus asigna un número de unidad. Lo obtienes con `device_get_unit(9)` y lo usas como `sc->unit`, exactamente como hace el Capítulo 7. Newbus garantiza que el número es único dentro del espacio de nombres del driver.

**Asignación explícita con `unrhdr`.** Para los drivers que crean nodos fuera del flujo de Newbus, el asignador `unrhdr(9)` asigna números de unidad de un pool. `/usr/src/sys/dev/led/led.c` usa este mecanismo: `sc->unit = alloc_unr(led_unit);`. El framework LED no se adjunta a través de Newbus para cada LED, así que no puede pedirle a Newbus un número de unidad; en su lugar, mantiene su propio pool de unidades.

Para un driver básico construido sobre Newbus, el primer enfoque es el que debes utilizar. El segundo cobra relevancia cuando escribas un pseudodispositivo que pueda instanciarse muchas veces bajo demanda, lo cual es un tema de capítulos posteriores.

### Convenciones de nomenclatura en el árbol

Como es probable que leas drivers reales de FreeBSD durante el aprendizaje, te ayuda reconocer las formas que toman sus nombres. Un breve recorrido:

- **`bpf%d`**: un nodo por instancia BPF. Aparece en `/usr/src/sys/net/bpf.c`.
- **`md%d`**: discos de memoria. `/usr/src/sys/dev/md/md.c`.
- **`led/%s`**: un subdirectorio por driver, un nodo por LED. `/usr/src/sys/dev/led/led.c` usa el argumento de nombre como una cadena libre, elegida por quien llama, p. ej. `led/ehci0`.
- **`ttyu%d`**, **`cuaU%d`**: puertos serie hardware, nodos "in" y "out" pareados.
- **`ptyp%d`**, **`ttyp%d`**: pares de pseudoterminales.
- **`pts/%d`**: asignación moderna de PTY en un subdirectorio.
- **`fuse`**: punto de entrada singleton para el subsistema FUSE.
- **`mem`**, **`kmem`**: singletons para la inspección de memoria.
- **`pci`, `pciconf`**: interfaces de inspección del bus PCI.
- **`io`**: acceso a puertos I/O, singleton.
- **`audit`**: dispositivo de control del subsistema de auditoría.

Observa que en la mayoría de estos casos el nombre codifica la identidad del driver. Es algo deliberado. Cuando un operador necesita escribir una regla en `devfs.conf`, una regla de firewall o un script de copias de seguridad, trabaja con rutas, y las rutas predecibles le facilitan mucho la tarea.

### Gestión de múltiples unidades

El driver del Capítulo 7 registró exactamente un hijo de Newbus en su callback `device_identify`, por lo que solo existe una instancia y el único número de unidad es `0`. Algunos drivers necesitan más de una instancia, ya sea en el arranque o bajo demanda.

Para un driver instanciado en el arranque con una cantidad fija, el patrón consiste en añadir más hijos en `device_identify`:

```c
static void
myfirst_identify(driver_t *driver, device_t parent)
{
        int i;

        for (i = 0; i < MYFIRST_INSTANCES; i++) {
                if (device_find_child(parent, driver->name, i) != NULL)
                        continue;
                if (BUS_ADD_CHILD(parent, 0, driver->name, i) == NULL)
                        device_printf(parent,
                            "myfirst%d: BUS_ADD_CHILD failed\n", i);
        }
}
```

Newbus llama a `attach` por cada hijo, y cada llamada obtiene su propio softc y su propio número de unidad. La cadena de formato `"myfirst/%d"` de `make_dev_s` con `sc->unit` produce entonces `/dev/myfirst/0`, `/dev/myfirst/1`, y así sucesivamente.

Para un driver instanciado bajo demanda, la arquitectura es muy diferente. Normalmente se expone un único cdev de "control", y cuando un usuario realiza una operación sobre él el driver asigna una nueva instancia y un nuevo cdev. El driver de discos de memoria en `/usr/src/sys/dev/md/md.c` es un ejemplo claro: `/dev/mdctl` acepta un ioctl `MDIOCATTACH`, y cada attach exitoso produce un nuevo cdev `/dev/mdN` a través de la capa GEOM. El subsistema de pseudoterminales adopta un enfoque similar: un usuario que abre `/dev/ptmx` recibe al otro lado un `/dev/pts/N` recién asignado. El Capítulo 8 no te guía a través de esas maquinarias; basta con saber que cuando ves un driver crear cdevs desde el interior de un manejador de eventos en lugar de desde `attach`, estás ante el patrón de instanciación dinámica.

### Un pequeño desvío: devtoname y sus compañeros

Tres pequeñas funciones auxiliares aparecen con frecuencia en el código de drivers y en el libro a partir de aquí. Merece la pena conocerlas:

- **`devtoname(cdev)`**: devuelve un puntero al nombre del nodo. Solo lectura. Se usa para mensajes de log: `device_printf(dev, "created /dev/%s\n", devtoname(sc->cdev))`.
- **`dev2unit(cdev)`**: devuelve el campo `si_drv0`, que convencionalmente es el número de unidad. Se define como una macro en `conf.h`.
- **`device_get_nameunit(dev)`**: se usa sobre un `device_t` y devuelve el nombre en el ámbito de Newbus, como `"myfirst0"`. Útil para nombres de mutex.

Las tres son seguras de usar en contextos donde se sabe que el cdev o el dispositivo está activo, lo que en un manejador de driver es siempre el caso.



## Alias: un cdev, más de un nombre

A veces un dispositivo necesita ser accesible bajo más de un nombre. Quizás has renombrado un nodo y quieres que el nombre antiguo siga funcionando durante un periodo de deprecación. Quizás quieres un nombre corto y estable que siempre apunte a la unidad `0` sin que el usuario tenga que saber cuál es la unidad actual. Quizás el resto del sistema ya tiene una convención consolidada y quieres respetarla.

FreeBSD ofrece `make_dev_alias(9)` para esto. Un alias es en sí mismo un `struct cdev`, pero con el flag `SI_ALIAS` activo y compartiendo la misma maquinaria de despacho subyacente que el nodo primario. Un programa de usuario que abre el alias llega a los mismos manejadores de `cdevsw` que un programa que abre el nombre primario.

Firmas, de `/usr/src/sys/sys/conf.h`:

```c
struct cdev *make_dev_alias(struct cdev *_pdev, const char *_fmt, ...);
int          make_dev_alias_p(int _flags, struct cdev **_cdev,
                              struct cdev *_pdev, const char *_fmt, ...);
```

Le pasas el cdev primario, una cadena de formato y argumentos opcionales. Recibes de vuelta un nuevo cdev que representa el alias. Cuando hayas terminado, destruyes el alias con `destroy_dev(9)`, de la misma forma que destruyes cualquier otro cdev.

Esta es la forma del código que añadirás a `myfirst_attach()`:

```c
sc->cdev_alias = make_dev_alias(sc->cdev, "myfirst");
if (sc->cdev_alias == NULL) {
        device_printf(dev, "failed to create /dev/myfirst alias\n");
        /* fall through; the primary node is still usable */
}
```

Dos observaciones sobre ese fragmento. La primera: no crear el alias no es fatal. La ruta primaria sigue funcionando, así que registramos el error y continuamos. La segunda: solo necesitas guardar un puntero al cdev del alias si piensas destruirlo en detach. En la mayoría de los drivers lo harás, así que guárdalo en el softc justo al lado de `cdev`.

### Alias frente a `link` en devfs.conf

Los lectores familiarizados con los enlaces simbólicos de UNIX a veces preguntan por qué FreeBSD ofrece dos formas distintas de dar a un dispositivo un segundo nombre. La distinción es real y merece explicarse con claridad.

Un alias de `make_dev_alias(9)` es un **segundo cdev que comparte su maquinaria de despacho con el primario**. Cuando el usuario lo abre, devfs navega directamente hasta tus manejadores de `cdevsw`. No hay ningún enlace simbólico en el sistema de archivos. Ejecutar `ls -l` sobre el alias muestra otro nodo de caracteres especial con su propio modo y propietario. El kernel sabe que el alias está vinculado al cdev primario (el flag `SI_ALIAS` y el puntero `si_parent` registran esa relación) y lo limpia automáticamente si el primario desaparece, siempre que tu driver recuerde llamar a `destroy_dev(9)` sobre él.

Una directiva `link` en `/etc/devfs.conf` crea un **enlace simbólico dentro de devfs**. `ls -l` muestra una `l` en el campo de tipo y una flecha apuntando al destino. Al abrirlo, el kernel primero resuelve el enlace simbólico y luego abre el destino. El destino y el enlace tienen permisos y propietario independientes; el propio enlace simbólico no lleva ninguna política de acceso más allá de su existencia.

¿Cuál elegir?

- Usa `make_dev_alias` cuando el propio driver tenga razón para exponer el nombre adicional, por ejemplo una forma corta y conocida o una ruta heredada que debe parecer idéntica a la nueva en cuanto a permisos.
- Usa `link` en `devfs.conf` cuando el operador quiera un atajo de conveniencia y el driver no tenga opinión al respecto. Nada de ese tipo de enlace pertenece al código del kernel.

Ambos enfoques funcionan. La elección equivocada no es peligrosa; normalmente solo resulta poco elegante. Mantén el código del driver limpio y deja que la política del operador resida donde corresponde.

### Tabla comparativa: tres formas de dar a un nodo dos nombres

Una breve comparación reúne las diferencias en un solo lugar:

| Propiedad                                            | `make_dev_alias` | `devfs.conf link`                    | Enlace simbólico con `ln -s`    |
|------------------------------------------------------|:----------------:|:------------------------------------:|:-------------------------------:|
| Vive en el código del kernel                         | sí               | no                                   | no                              |
| Vive en devfs                                        | sí               | sí                                   | no (vive en el FS subyacente)   |
| `ls -l` lo muestra como `c`                          | sí               | no (aparece como `l`)                | no (aparece como `l`)           |
| Tiene su propio modo y propietario                   | sí               | hereda del destino                   | hereda del destino              |
| Se limpia automáticamente al descargar el driver     | sí               | sí (en el siguiente `service devfs restart`) | no                    |
| Sobrevive a un reinicio                              | solo mientras el driver esté cargado | sí, si está en `devfs.conf` | sí, si está bajo `/etc` o similar |
| Apropiado para nombres gestionados por el driver     | sí               | no                                   | no                              |
| Apropiado para atajos del operador                   | no               | sí                                   | a veces                         |

El patrón es el siguiente: los drivers son dueños de sus nombres primarios y de cualquier alias que lleve política; los operadores son dueños de los enlaces de conveniencia que no llevan política. Cruzar esa línea es la fuente de los problemas de mantenimiento futuros.

### La variante `make_dev_alias_p`

`make_dev_alias` tiene un hermano que acepta una palabra de flags y devuelve un `errno`, por las mismas razones que la familia principal `make_dev`. Su declaración en `/usr/src/sys/sys/conf.h`:

```c
int make_dev_alias_p(int _flags, struct cdev **_cdev, struct cdev *_pdev,
                     const char *_fmt, ...);
```

Los flags válidos son `MAKEDEV_WAITOK`, `MAKEDEV_NOWAIT` y `MAKEDEV_CHECKNAME`. El comportamiento es análogo al de `make_dev_p`: cero en caso de éxito, el nuevo cdev escrito a través del puntero de salida, y un valor `errno` distinto de cero en caso de fallo.

Si la creación del alias está en una ruta que no puede bloquearse, usa `make_dev_alias_p(MAKEDEV_NOWAIT, ...)` y prepárate para recibir `ENOMEM`. En el caso habitual, donde el alias se crea durante `attach` en condiciones normales, `make_dev_alias(9)` es suficiente; usa `MAKEDEV_WAITOK` internamente.

### La variante `make_dev_physpath_alias`

Existe una tercera función de alias, `make_dev_physpath_alias`, utilizada por drivers que quieren publicar alias de ruta física además de sus nombres lógicos. Existe para soportar las rutas de topología hardware bajo `/dev/something/by-path/...` que ciertos drivers de almacenamiento exponen. La mayoría de los drivers para principiantes nunca la necesitan.

### Usos de `make_dev_alias` en el árbol de código fuente

Un ejercicio útil: busca `make_dev_alias` con `grep` por todo `/usr/src/sys` y observa los contextos en que se usa. Lo encontrarás en drivers de almacenamiento que quieren publicar un nombre estable junto a uno numerado dinámicamente, en ciertos pseudodispositivos que necesitan un nombre de compatibilidad heredado, y en un pequeño número de drivers especializados que modelan una topología hardware.

La mayoría de los drivers no lo usan, y eso está perfectamente bien. Cuando un driver lo hace, la razón es casi siempre una de estas tres:

1. **Compatibilidad con rutas heredadas.** Un driver que fue renombrado pero debe mantener el nombre antiguo en funcionamiento.
2. **Un atajo conocido.** Un nombre corto que siempre resuelve a la instancia cero o al predeterminado actual, para que los scripts de shell puedan escribir una ruta en lugar de negociar el número de unidad.
3. **Exposición de topología.** Un nombre que refleja dónde se encuentra el hardware, además de qué es el hardware.

Tu driver `myfirst` usa el caso 1: `/dev/myfirst` como atajo de `/dev/myfirst/0` para que los ejemplos del Capítulo 7 sigan siendo válidos. Esa es la forma típica de uso para principiantes.

### Ciclo de vida de los alias y orden de destrucción

Un cdev registrado como alias tiene el flag `SI_ALIAS` activo y está enlazado en la lista `si_children` del cdev primario mediante el puntero de retorno `si_parent`. Esto significa que el kernel conoce la relación y actuará correctamente incluso si desmontas el cdev en un orden ligeramente incorrecto. No significa que puedas ignorar el orden; significa que la destrucción es más tolerante que el desmontaje de objetos del kernel en general.

En la práctica, la regla que debes seguir en tu ruta de `detach` es: **destruye el alias primero, luego el primario**. Los drivers de ejemplo del árbol de acompañamiento siguen esta convención, y la razón es la legibilidad. Cualquier otro orden hace que tu código sea más difícil de razonar, y los revisores lo señalarán.

Si un driver omite por completo la llamada a `destroy_dev` sobre el alias, la destrucción del primario eliminará el alias automáticamente cuando el primario desaparezca; eso es lo que hace `destroy_devl` al recorrer `si_children`. Pero dejar ese trabajo al destructor es un desperdicio porque el primario mantiene una referencia que lo mantiene vivo más tiempo del necesario, y porque el operador ve que el alias desaparece "más tarde" en lugar de limpiamente en el momento de la descarga. Simplemente destruye ambos.

### Cuándo los alias empiezan a oler mal

Algunos patrones con alias son code smells leves que merece la pena nombrar:

- **Cadenas de alias.** Los alias de alias son legales pero casi siempre indican que el driver intenta tapar una decisión de nomenclatura que debería haberse revisado. Si te encuentras queriendo crear un alias de un alias, para y renombra el primario.
- **Demasiados alias.** Uno o dos son habituales. Cinco o más sugieren que el driver no sabe muy bien cómo quiere llamarse. Revisa la nomenclatura.
- **Alias con modos muy diferentes.** Dos cdevs que apuntan al mismo conjunto de manejadores pero exponen modos de permiso radicalmente distintos son indistinguibles de una trampa. Haz los permisos consistentes, o usa dos primarios separados con dos valores `cdevsw` separados que implementen diferentes políticas en el código.

Ninguno de estos es un error. Son señales de que el diseño se está desviando. Detéctalos a tiempo y el driver seguirá siendo legible; ignóralos y el driver se convertirá en algo que los revisores temen.

## Estado por apertura con devfs_set_cdevpriv

Llegamos ahora a la parte del capítulo que prepara el terreno para el Capítulo 9. El driver del Capítulo 7 imponía **apertura exclusiva** estableciendo un indicador en el softc. Funciona, pero es la política más tosca posible. Muchos dispositivos reales permiten varios abrientes y necesitan mantener un pequeño registro **por descriptor de archivo**, no por dispositivo. Piensa en un flujo de registros, un canal de estado, o cualquier nodo donde distintos consumidores quieran sus propias posiciones de lectura.

FreeBSD ofrece tres funciones relacionadas para esto, declaradas en `/usr/src/sys/sys/conf.h` e implementadas en `/usr/src/sys/fs/devfs/devfs_vnops.c`:

```c
int  devfs_set_cdevpriv(void *priv, d_priv_dtor_t *dtr);
int  devfs_get_cdevpriv(void **datap);
void devfs_clear_cdevpriv(void);
```

El modelo es sencillo y agradable de usar:

1. Dentro de tu manejador `d_open`, asigna una pequeña estructura por apertura y llama a `devfs_set_cdevpriv(priv, dtor)`. El kernel asocia `priv` al descriptor de archivo actual y recuerda `dtor` como la función que debe invocar cuando ese descriptor se cierre.
2. En `d_read`, `d_write`, o cualquier otro manejador, llama a `devfs_get_cdevpriv(&priv)` para recuperar el puntero.
3. Cuando el proceso llama a `close(2)`, termina, o de algún otro modo suelta su última referencia al descriptor, devfs invoca tu destructor con `priv`. Tú liberas lo que hayas asignado.

No tienes que preocuparte por el orden de limpieza en relación con tu propio manejador `d_close`. Devfs lo gestiona. El invariante importante es que tu destructor se invocará exactamente una vez por cada llamada exitosa a `devfs_set_cdevpriv`.

Un ejemplo real de `/usr/src/sys/net/bpf.c` tiene este aspecto:

```c
d = malloc(sizeof(*d), M_BPF, M_WAITOK | M_ZERO);
error = devfs_set_cdevpriv(d, bpf_dtor);
if (error != 0) {
        free(d, M_BPF);
        return (error);
}
```

Eso es, en esencia, el patrón completo. BPF asigna un descriptor por apertura, lo registra, y si el registro falla, libera la asignación y devuelve el error. El destructor `bpf_dtor` realiza la limpieza cuando el descriptor muere. Harás lo mismo para `myfirst`, con una estructura por apertura mucho más pequeña.

### Un contador mínimo por apertura para myfirst

Añadirás una pequeña estructura y un destructor. Nada más en el driver cambia de forma.

```c
struct myfirst_fh {
        struct myfirst_softc *sc;    /* back-pointer to the owning softc */
        uint64_t              reads; /* bytes this descriptor has read */
        uint64_t              writes;/* bytes this descriptor has written */
};

static void
myfirst_fh_dtor(void *data)
{
        struct myfirst_fh *fh = data;
        struct myfirst_softc *sc = fh->sc;

        mtx_lock(&sc->mtx);
        sc->active_fhs--;
        mtx_unlock(&sc->mtx);

        device_printf(sc->dev, "per-open dtor fh=%p reads=%lu writes=%lu\n",
            fh, (unsigned long)fh->reads, (unsigned long)fh->writes);

        free(fh, M_DEVBUF);
}
```

El destructor hace tres cosas que merece la pena destacar. Decrementa `active_fhs` bajo el mismo mutex que protege los demás contadores del softc, de modo que el recuento permanece coherente con lo que `d_open` vio al abrir el descriptor. Registra una línea que concuerda con la forma del mensaje `open via ...`, de modo que cada apertura en `dmesg` tiene un destructor visiblemente emparejado. Y libera la asignación al final, una vez que todo lo que pudiera necesitar leer de `fh` ya ha terminado.

En tu `d_open`, asigna una de estas estructuras y regístrala:

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc;
        struct myfirst_fh *fh;
        int error;

        sc = dev->si_drv1;
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        fh = malloc(sizeof(*fh), M_DEVBUF, M_WAITOK | M_ZERO);
        fh->sc = sc;

        error = devfs_set_cdevpriv(fh, myfirst_fh_dtor);
        if (error != 0) {
                free(fh, M_DEVBUF);
                return (error);
        }

        mtx_lock(&sc->mtx);
        sc->open_count++;
        sc->active_fhs++;
        mtx_unlock(&sc->mtx);

        device_printf(sc->dev, "open via %s fh=%p (active=%d)\n",
            devtoname(dev), fh, sc->active_fhs);
        return (0);
}
```

Observa dos cosas. Primero, la comprobación de apertura exclusiva del Capítulo 7 ha desaparecido. Con el estado por apertura en su lugar, no hay razón para rechazar un segundo abriente. Si en algún momento deseas exclusividad, puedes volver a añadirla; es una decisión independiente. Segundo, el destructor se encargará de la liberación. Tu `d_close` no necesita tocar `fh` en absoluto.

En un manejador que se ejecuta más tarde, como `d_read`, recuperas la estructura por apertura:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_fh *fh;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);

        /* Real read logic arrives in Chapter 9. For now, report EOF
         * and leave the counter untouched so userland tests can observe
         * that the descriptor owns its own state.
         */
        (void)fh;
        return (0);
}
```

El `(void)fh` silencia la advertencia de «variable no utilizada» hasta que el Capítulo 9 le asigne trabajo que hacer. No hay problema. Lo que importa por ahora es que tu driver tiene una estructura por archivo limpia, funcional y con una destrucción también limpia. Desde userland puedes confirmar el cableado abriendo el dispositivo desde dos procesos y observando cómo los mensajes de device-printf llegan con dos punteros `fh=` distintos.

### Lo que garantiza el destructor

Como el destructor realiza la mayor parte del trabajo, merece la pena ser precisos sobre cuándo se ejecuta y en qué estado se encuentra el sistema en ese momento. Leer `devfs_destroy_cdevpriv` en `/usr/src/sys/fs/devfs/devfs_vnops.c` confirma los detalles.

- El destructor se ejecuta **exactamente una vez por cada llamada exitosa a `devfs_set_cdevpriv`**. Si la función devolvió `EBUSY` porque el descriptor ya tenía datos privados, el destructor de *tus* datos nunca se invoca; debes liberar la asignación tú mismo, tal como hace el código de ejemplo.
- El destructor se ejecuta **cuando se libera el descriptor de archivo**, no cuando se llama a tu `d_close`. Para un `close(2)` ordinario, los dos momentos están próximos. Para un proceso que termina mientras mantiene el descriptor, este se libera como parte del desmontaje de salida; el destructor se ejecuta igualmente. Para un descriptor compartido mediante `fork(2)` o pasado a través de un socket de dominio UNIX, el destructor se ejecuta solo cuando cae la última referencia.
- El destructor se ejecuta sin ningún kernel lock retenido en tu nombre. Si tu destructor accede al estado del softc, toma el lock que usa el softc, tal como hace el ejemplo de la etapa 2 al decrementar `active_fhs`.
- El destructor no debe bloquearse durante mucho tiempo. No es un contexto de espera indefinida, pero tampoco es un manejador de interrupciones. Trátalo como una función del kernel ordinaria y mantenlo breve.

### Cuando `devfs_set_cdevpriv` devuelve EBUSY

`devfs_set_cdevpriv` puede fallar de exactamente una manera interesante: el descriptor ya tiene datos privados asociados. Eso ocurre cuando algo, normalmente tu propio código en una llamada anterior, ya ha establecido un cdevpriv y estás intentando establecer otro. La solución limpia es hacer el establecimiento una sola vez, al principio, y luego recuperarlo con `devfs_get_cdevpriv` donde lo necesites.

De esto se derivan dos precauciones. La primera: no llames a `devfs_set_cdevpriv` dos veces desde la misma apertura. La segunda: cuando la llamada falle, libera lo que hayas asignado antes de intentar establecerlo. El ejemplo `myfirst_open` de este capítulo sigue ambas reglas. Cuando adaptes el patrón a tu propio driver, tenlas presentes.

### Cuándo no usar devfs_set_cdevpriv

El estado por apertura no es el lugar adecuado para todo. Mantén el estado global del dispositivo en el softc, accesible mediante `si_drv1`. Mantén el estado por apertura en la estructura cdevpriv, accesible mediante `devfs_get_cdevpriv`. Mezclar ambos es la manera más rápida de escribir un driver que funciona en pruebas de un solo abriente y se rompe cuando aparecen dos procesos a la vez.

`devfs_clear_cdevpriv(9)` existe, y puedes verlo en código de terceros, pero para la mayoría de los drivers la limpieza automática a través del destructor es suficiente. Recurre a `devfs_clear_cdevpriv` solo cuando tengas una razón concreta, por ejemplo un driver que puede desconectar limpiamente el estado por apertura de forma anticipada en respuesta a un `ioctl(2)`. Si no estás seguro de necesitarlo, no lo necesitas.

### El interior de devfs_set_cdevpriv: cómo funciona el mecanismo

Las dos funciones que llamas parecen casi triviales desde fuera. El mecanismo que impulsan merece la pena examinar una vez, porque conocer su forma facilita el razonamiento sobre cualquier caso extremo.

De `/usr/src/sys/fs/devfs/devfs_vnops.c`:

```c
int
devfs_set_cdevpriv(void *priv, d_priv_dtor_t *priv_dtr)
{
        struct file *fp;
        struct cdev_priv *cdp;
        struct cdev_privdata *p;
        int error;

        fp = curthread->td_fpop;
        if (fp == NULL)
                return (ENOENT);
        cdp = cdev2priv((struct cdev *)fp->f_data);
        p = malloc(sizeof(struct cdev_privdata), M_CDEVPDATA, M_WAITOK);
        p->cdpd_data = priv;
        p->cdpd_dtr = priv_dtr;
        p->cdpd_fp = fp;
        mtx_lock(&cdevpriv_mtx);
        if (fp->f_cdevpriv == NULL) {
                LIST_INSERT_HEAD(&cdp->cdp_fdpriv, p, cdpd_list);
                fp->f_cdevpriv = p;
                mtx_unlock(&cdevpriv_mtx);
                error = 0;
        } else {
                mtx_unlock(&cdevpriv_mtx);
                free(p, M_CDEVPDATA);
                error = EBUSY;
        }
        return (error);
}
```

Un recorrido breve por los puntos importantes:

- `curthread->td_fpop` es el puntero de archivo para el despacho actual. devfs lo establece antes de llamar a tu `d_open` y lo deshace después. Si llamaras a `devfs_set_cdevpriv` desde un contexto en el que no hay ningún despacho activo, `fp` sería `NULL` y la función devolvería `ENOENT`. En la práctica, esto solo ocurre si intentas llamarlo desde el contexto equivocado, por ejemplo desde un callback de temporizador que no está vinculado a un archivo.
- Se asigna un pequeño registro, `struct cdev_privdata`, desde un bucket de malloc dedicado `M_CDEVPDATA`. Contiene tres campos: tu puntero, tu destructor, y un puntero de retorno a la `struct file`.
- Que dos threads entrasen en esta función al mismo tiempo para el mismo descriptor sería un desastre, por lo que un único mutex `cdevpriv_mtx` protege la sección crítica. La comprobación de `fp->f_cdevpriv == NULL` es lo que impide el doble registro: si ya hay un registro asociado, el nuevo se libera y se devuelve `EBUSY`.
- Si tiene éxito, el registro se inserta en dos listas: el puntero propio del descriptor `fp->f_cdevpriv`, y la lista del cdev con todos sus registros privados de descriptor `cdp->cdp_fdpriv`. La primera convierte `devfs_get_cdevpriv` en una búsqueda de un solo puntero. La segunda permite a devfs iterar sobre todos los registros activos cuando el cdev se destruye.

La ruta del destructor es igual de pequeña:

```c
void
devfs_destroy_cdevpriv(struct cdev_privdata *p)
{

        mtx_assert(&cdevpriv_mtx, MA_OWNED);
        KASSERT(p->cdpd_fp->f_cdevpriv == p,
            ("devfs_destoy_cdevpriv %p != %p",
             p->cdpd_fp->f_cdevpriv, p));
        p->cdpd_fp->f_cdevpriv = NULL;
        LIST_REMOVE(p, cdpd_list);
        mtx_unlock(&cdevpriv_mtx);
        (p->cdpd_dtr)(p->cdpd_data);
        free(p, M_CDEVPDATA);
}
```

Dos cosas a tener en cuenta. Primero, el destructor se invoca **con el mutex liberado**, de modo que tu destructor puede tomar sus propios locks sin riesgo de deadlock contra `cdevpriv_mtx`. Segundo, el propio registro se libera inmediatamente después de que tu destructor retorna, por lo que un puntero obsoleto hacia él sería un use-after-free. Si tu destructor guarda el puntero en otro lugar, guarda una copia de los datos, no el registro.

### Interacción con fork, dup y SCM_RIGHTS

Los descriptores de archivo en UNIX tienen tres formas habituales de multiplicarse: `dup(2)`, `fork(2)`, y pasarlos a través de un socket de dominio UNIX con `SCM_RIGHTS`. Cada una produce referencias adicionales a la misma `struct file`. El mecanismo cdevpriv de devfs se comporta de forma coherente en los tres casos.

Tras `dup(2)` o `fork(2)`, el nuevo descriptor de archivo apunta a la **misma** `struct file` que el original. El registro cdevpriv se indexa sobre la `struct file`, no sobre el número de descriptor, por lo que ambos descriptores comparten el registro. Tu destructor se activa exactamente una vez, cuando se libera el último descriptor que apunta a ese archivo. Esa última liberación puede ser un `close(2)` explícito, un `exit(3)` implícito que cierra todo, o incluso un fallo que termine el proceso.

Pasar el descriptor a través de `SCM_RIGHTS` es la misma historia desde el punto de vista del cdevpriv. El proceso receptor obtiene un nuevo descriptor que apunta a la misma `struct file`. El registro permanece asociado; el destructor se activa solo cuando cae la última referencia, que puede estar ahora en el proceso en el otro extremo del socket.

Esto es normalmente exactamente lo que quieres, porque coincide con el modelo mental del usuario. Un estado por apertura por apertura conceptual. Si alguna vez necesitas un modelo diferente, por ejemplo un modelo en el que cada descriptor creado con `dup(2)` tenga su propio estado, la solución es establecer `D_TRACKCLOSE` en tu `cdevsw` y asignar estado por descriptor dentro del propio `d_open` sin usar `devfs_set_cdevpriv`. Eso es poco habitual; los drivers ordinarios no lo necesitan.

### Un recorrido por usos reales en el árbol

Para afianzar el patrón, un breve recorrido por tres drivers que usan cdevpriv de formas reconocibles. No necesitas entender lo que hace cada driver en su conjunto; céntrate solo en la forma del archivo de dispositivo.

**`/usr/src/sys/net/bpf.c`** es el ejemplo canónico. Su `bpfopen` asigna un descriptor por apertura, llama a `devfs_set_cdevpriv(d, bpf_dtor)`, y configura un pequeño conjunto de contadores y estado. El destructor `bpf_dtor` desmonta todo: desconecta el descriptor de su interfaz BPF, libera contadores, vacía una lista de select y suelta una referencia. El patrón es exactamente el que este capítulo ha descrito, más un buen conjunto de maquinaria específica de BPF que la Parte 6 revisitará.

**`/usr/src/sys/fs/fuse/fuse_device.c`** toma el mismo patrón y añade encima un estado específico de FUSE. La apertura asigna una `struct fuse_data`, la registra con `devfs_set_cdevpriv`, y todos los manejadores posteriores la recuperan con `devfs_get_cdevpriv`. El destructor desmonta la sesión FUSE.

**`/usr/src/sys/opencrypto/cryptodev.c`** usa cdevpriv para el estado de sesión de cifrado por apertura. Cada apertura obtiene su propio registro, y el destructor lo limpia.

Estos tres drivers no tienen casi nada en común a nivel de subsistema: uno trata sobre la captura de paquetes, otro sobre sistemas de archivos en userland, otro sobre la descarga de cifrado por hardware. Lo que comparten es la forma del archivo de dispositivo. Los mismos tres pasos, en el mismo orden, por las mismas razones.

### Patrones para lo que incluir en la estructura por apertura

Ahora que conoces los mecanismos, la pregunta de diseño es qué campos debe contener tu estructura por apertura. Algunos patrones se repiten en drivers reales.

**Contadores.** Bytes leídos, bytes escritos, llamadas realizadas, errores registrados. Cada descriptor tiene sus propios contadores. `myfirst` en la etapa 2 ya hace esto con `reads` y `writes`.

**Posiciones de lectura.** Si tu driver expone un flujo de bytes con posicionamiento, el desplazamiento actual pertenece a la estructura por apertura, no al softc. La razón son dos lectores en distintos desplazamientos.

**Handles de suscripción.** Si el descriptor está leyendo eventos y un `poll` o `kqueue` necesita saber si hay más eventos pendientes para este descriptor concreto, el registro de suscripción pertenece aquí. El capítulo 10 utiliza este patrón.

**Estado del filtro.** Drivers como BPF permiten que cada descriptor instale un programa de filtro. La forma compilada de ese programa es por descriptor. También pertenece aquí, en la estructura por apertura.

**Reservas o tickets.** Si el driver entrega recursos escasos (un slot de hardware, un canal DMA, un rango de buffer compartido) y los vincula a una apertura, el registro va en el estado por apertura. Cuando el descriptor se cierra, el destructor libera la reserva automáticamente.

**Instantáneas de credenciales.** Algunos drivers quieren recordar quién abrió el descriptor en el momento de la apertura, con independencia de quién esté realizando actualmente una lectura o escritura sobre él. Capturar una instantánea de `td->td_ucred` en el momento de la apertura es un patrón habitual. Las credenciales están contadas por referencia (`crhold`/`crfree`), y el destructor es el lugar adecuado para soltar esa referencia.

No todos los drivers necesitan todos estos elementos. La lista es un menú, no una lista de comprobación. Cuando diseñes un driver, recórrela y pregúntate: "¿qué información pertenece a esta apertura específica del nodo?". Las respuestas van en la estructura por apertura.

### Una advertencia sobre las referencias cruzadas del softc a los registros por apertura

Una tentación que surge con el estado por apertura es que el softc lleve punteros de vuelta a los registros por apertura, de modo que difundir un evento a cada descriptor sea un simple recorrido de lista. La tentación es comprensible; la implementación está llena de casos límite. Dos threads que compiten para cerrar el último descriptor mientras un tercero intenta difundir el evento es el escenario que rompe el código directo, y solucionarlo tiende a requerir más locks de los que conviene añadir.

La respuesta de FreeBSD es `devfs_foreach_cdevpriv(9)`, un iterador basado en callbacks que recorre los registros por apertura asociados a un cdev determinado bajo el lock correcto. Si alguna vez necesitas este patrón, usa esa función y pásale un callback. No mantengas tus propias listas.

No usaremos `devfs_foreach_cdevpriv` en el Capítulo 8. Se menciona aquí porque si buscas `cdevpriv` en el árbol de FreeBSD lo encontrarás, y deberías reconocerlo como la alternativa segura a reinventar la iteración por tu cuenta.



## Destrucción segura de cdevs

El acto de registrar un cdev en devfs es rutinario. Eliminarlo es la parte que requiere reflexión. El Capítulo 7 te enseñó `destroy_dev(9)`, y para la ruta sencilla de un driver bien comportado es todo lo que necesitas. Los drivers reales a veces necesitan más. Esta sección recorre la familia de funciones auxiliares de destrucción, explica qué garantizan y muestra cuál es la herramienta adecuada en cada situación.

### El modelo de drenaje

Empecemos por la pregunta que debe responder la destrucción: «¿cuándo es seguro liberar el softc y descargar el módulo?». La respuesta ingenua, «después de que retorne `destroy_dev`», está casi bien. La respuesta cuidadosa es: «después de que retorne `destroy_dev` **y** ningún thread del kernel pueda estar en ninguno de mis handlers para este cdev».

Los contadores de `struct cdev` que conociste antes son el mecanismo que usa el kernel para rastrear esto. `si_threadcount` se incrementa cada vez que devfs entra en uno de tus handlers en nombre de un syscall de usuario, y se decrementa cada vez que el handler retorna. `destroy_devl`, que es la función interna a la que llama `destroy_dev`, vigila ese contador. A continuación se muestra el fragmento relevante de `/usr/src/sys/kern/kern_conf.c`:

```c
while (csw != NULL && csw->d_purge != NULL && dev->si_threadcount) {
        csw->d_purge(dev);
        mtx_unlock(&cdp->cdp_threadlock);
        msleep(csw, &devmtx, PRIBIO, "devprg", hz/10);
        mtx_lock(&cdp->cdp_threadlock);
        if (dev->si_threadcount)
                printf("Still %lu threads in %s\n",
                    dev->si_threadcount, devtoname(dev));
}
while (dev->si_threadcount != 0) {
        /* Use unique dummy wait ident */
        mtx_unlock(&cdp->cdp_threadlock);
        msleep(&csw, &devmtx, PRIBIO, "devdrn", hz / 10);
        mtx_lock(&cdp->cdp_threadlock);
}
```

Dos bucles. El primer bucle llama a `d_purge` si el driver lo proporciona; el segundo simplemente espera. En ambos casos el resultado es el mismo: `destroy_dev` no retorna hasta que `si_threadcount` es cero. Este es el comportamiento de **drenaje** que hace segura la destrucción. Cuando la llamada retorna, ningún thread está dentro de ningún handler, y ningún thread nuevo puede entrar en uno, porque `si_devsw` ha sido vaciado.

Lo que esto significa para tu código: **después de que retorne `destroy_dev(sc->cdev)`, nada en el espacio de usuario puede desencadenar una llamada a tus handlers con este cdev**. Eres libre de destruir los miembros del softc de los que dependen esos handlers.

### Las cuatro funciones de destrucción

FreeBSD expone cuatro funciones relacionadas para la destrucción de cdevs. Cada una gestiona un caso ligeramente distinto.

**`destroy_dev(struct cdev *dev)`**

El caso ordinario. Síncrona: espera a que terminen los handlers en vuelo, luego desvincula el cdev de devfs y libera la referencia primaria del kernel. Se usa en el Capítulo 7 y en todas las rutas de destrucción de un solo hilo de este libro. Requiere que el llamador pueda dormir y que no tenga ningún lock que los handlers en vuelo puedan necesitar.

**`destroy_dev_sched(struct cdev *dev)`**

Una forma diferida. Programa la destrucción en un taskqueue y retorna inmediatamente. Es útil cuando el contexto de llamada no puede dormir, por ejemplo desde dentro de un callback que se ejecuta bajo un lock. La destrucción real ocurre de forma asíncrona, y el llamador no debe asumir que se ha completado cuando la función retorna.

**`destroy_dev_sched_cb(struct cdev *dev, void (*cb)(void *), void *arg)`**

La misma forma diferida, pero con un callback que se ejecuta después de que se complete la destrucción. Úsala cuando necesites hacer trabajo de seguimiento (liberar el softc, por ejemplo) una vez que sepas que el cdev ha desaparecido definitivamente.

**`destroy_dev_drain(struct cdevsw *csw)`**

El barrido completo. Espera a que **todos** los cdevs registrados en el `cdevsw` dado sean completamente destruidos, incluidos los programados mediante las formas diferidas. Se usa cuando vas a desregistrar o liberar el propio `cdevsw`, por ejemplo dentro del handler `MOD_UNLOAD` de un módulo que incluye múltiples drivers.

### La condición de carrera que justifica destroy_dev_drain

El drenaje es un punto sutil, y la mejor forma de explicarlo es con el escenario que resuelve.

Supón que tu módulo exporta un `cdevsw`. En `MOD_UNLOAD`, tu código llama a `destroy_dev(sc->cdev)` y luego retorna con éxito. El kernel procede a desmontar el módulo. Todo parece estar bien, hasta que un momento después se ejecuta finalmente una tarea diferida programada anteriormente mediante `destroy_dev_sched`. Esa tarea desreferencia el `struct cdevsw` como parte de su limpieza. El `cdevsw` ha sido desmapeado junto con el módulo. El kernel entra en pánico.

La condición de carrera es estrecha pero real. `destroy_dev_drain` es la solución: llámala sobre tu `cdevsw` después de tener la certeza de que no se crearán más cdevs, y no retornará hasta que cada cdev registrado en ese `cdevsw` haya completado su destrucción. Solo entonces es seguro dejar que el módulo se descargue.

Si tu driver crea un cdev desde `attach`, lo destruye desde `detach`, y nunca usa las formas diferidas, no necesitas `destroy_dev_drain`. `myfirst` no lo necesita. Los drivers reales que gestionan cdevs de clonación o que destruyen cdevs desde handlers de eventos normalmente sí lo necesitan.

### El orden de operaciones en detach

Dado todo lo anterior, el orden correcto de operaciones en un handler `detach` para un driver con un cdev primario, un alias y estado por apertura es:

1. Rechaza el detach si hay algún descriptor todavía abierto. Retorna `EBUSY`. Tu contador `active_fhs` es lo que debes comprobar.
2. Destruye el cdev alias con `destroy_dev(sc->cdev_alias)`. Esto desvincula el alias de devfs y drena las llamadas en vuelo contra él.
3. Destruye el cdev primario con `destroy_dev(sc->cdev)`. Lo mismo que en el punto anterior para el primario.
4. Desmonta el árbol de sysctl con `sysctl_ctx_free(9)`.
5. Destruye el mutex con `mtx_destroy(9)`.
6. Borra el indicador `is_attached` por si algo todavía lo lee.
7. Retorna cero.

Observa que los pasos 2 y 3 cumplen dos propósitos cada uno. Eliminan los nodos de devfs para que no puedan llegar nuevas aperturas, y drenan las llamadas en vuelo para que ningún handler siga ejecutándose cuando el paso 4 intente liberar el estado que un handler leería.

El patrón es sencillo. La única forma de equivocarse es liberar algo antes de que el `destroy_dev` de drenaje haya terminado con ello. Mantén este orden y estarás a salvo.

### Descarga bajo carga

Un buen ejercicio para desarrollar la intuición es razonar sobre qué sucede cuando llega `kldunload` mientras un programa de userland está dentro de un `read(2)` sobre tu dispositivo.

Sigue la secuencia paso a paso:

- El kernel comienza a descargar el módulo. Llama a tu handler `MOD_UNLOAD`, que en última instancia llama a `device_delete_child` sobre tu dispositivo Newbus, lo que invoca tu `detach`.
- Tu `detach` llega a `destroy_dev(sc->cdev)`. Esta llamada es síncrona y esperará a que terminen los handlers en vuelo.
- El `read(2)` de userland está ejecutando actualmente tu `d_read`. `si_threadcount` vale 1.
- `destroy_dev` duerme, vigilando `si_threadcount`.
- Tu `d_read` retorna. `si_threadcount` baja a 0.
- `destroy_dev` retorna. Tu `detach` continúa con el desmontaje de sysctl y del mutex.
- El `read(2)` de userland ya ha devuelto sus bytes al espacio de usuario. El descriptor sigue abierto.
- Un `read(2)` posterior sobre el mismo descriptor desde el mismo proceso falla de forma limpia, porque el cdev ha desaparecido.

Esto es lo que te aporta «destruir primero el nodo, y luego desmontar sus dependencias». La ventana en la que userland podría observar un estado inconsistente se reduce hasta casi desaparecer gracias al comportamiento de drenaje del kernel.

### Cuándo destroy_dev tarda en avanzar

A veces `destroy_dev` permanecerá en su bucle de drenaje más tiempo del esperado. Hay algunas razones habituales.

- Un handler está bloqueado en un sleep que ningún wakeup va a liberar. Por ejemplo, un `d_read` que llama a `msleep(9)` sobre una variable de condición que nadie señaliza nunca. En ese caso, tu driver tiene un error de lógica. La destrucción está haciendo exactamente lo correcto al negarse a avanzar; tu trabajo es despertar el thread bloqueado, ya sea escribiendo un handler `d_purge` que lo mueva o asegurándote de que el flujo de control que finalmente lo despierta siga siendo funcional durante la descarga.
- Un programa de userland está atascado en un `ioctl` que espera al hardware. La solución suele ser la misma: un handler `d_purge` que le dice al thread bloqueado que abandone la espera.
- Dos destrucciones compitiendo, cada una drenando a la otra. Ese es el caso para el que existe `destroy_dev_drain`.

Si observas `dmesg` durante una descarga bloqueada, verás el mensaje `"Still N threads in foo"` impreso por el bucle anterior. Esa es tu señal: averigua qué están haciendo esos threads y convéncelos de que retornen.

### Un ejemplo mínimo de d_purge

Para completar el panorama, así es como se ve un `d_purge` sencillo. No todos los drivers lo necesitan; vale la pena mostrarlo para que reconozcas la estructura cuando leas código real:

```c
static void
myfirst_purge(struct cdev *dev)
{
        struct myfirst_softc *sc = dev->si_drv1;

        mtx_lock(&sc->mtx);
        sc->shutting_down = 1;
        wakeup(&sc->rx_queue);   /* nudge any thread waiting on us */
        mtx_unlock(&sc->mtx);
}

static struct cdevsw myfirst_cdevsw = {
        .d_version = D_VERSION,
        .d_purge   = myfirst_purge,
        /* ... */
};
```

La función es llamada desde dentro de `destroy_devl` mientras `si_threadcount` sigue siendo distinto de cero. Su cometido es avisar a cualquier thread del kernel bloqueado dentro de tus handlers para que el thread observe el apagado y retorne. Para los drivers que solo realizan lecturas bloqueantes, establecer un indicador de cierre y emitir un `wakeup(9)` suele ser todo lo que se necesita. El Capítulo 10 retoma esto cuando la I/O bloqueante se convierte en un tema de primer orden.

### Resumen

La destrucción de un cdev es una coreografía ensayada entre tres participantes: tu driver, devfs, y los threads de userland que puedan estar en medio de una llamada. Las garantías son sólidas si usas las herramientas correctas:

- Usa `destroy_dev` en las rutas ordinarias. Deja que drene.
- Usa las formas diferidas cuando no puedas dormir o cuando necesites un callback después de la destrucción.
- Usa `destroy_dev_drain` cuando vayas a liberar o desregistrar un `cdevsw`.
- Destruye el nodo antes que cualquier cosa de la que dependan los handlers del nodo.
- Proporciona un `d_purge` si tus handlers pueden bloquearse indefinidamente.

Más allá de eso, el detalle es algo que consultas cuando lo necesitas. La estructura es lo que importa, y la estructura es sencilla.



## Política persistente: devfs.conf y devfs.rules

Tu driver fija el modo **base**, el propietario y el grupo de cada nodo. Los ajustes persistentes por parte del operador pertenecen a `/etc/devfs.conf` y `/etc/devfs.rules`. Ambos archivos son partes estándar del sistema base de FreeBSD, y ambos se aplican a todos los montajes de devfs del host.

### devfs.conf: ajustes únicos por ruta

`devfs.conf` es la herramienta más sencilla. Cada línea aplica un ajuste único cuando aparece un nodo de dispositivo coincidente. El formato está documentado en `devfs.conf(5)`. Las directivas más comunes son `own`, `perm` y `link`:

```console
# /etc/devfs.conf
#
# Adjustments applied once when each node appears.

own     myfirst/0       root:operator
perm    myfirst/0       0660
link    myfirst/0       myfirst-primary
```

Esas tres líneas dicen: cada vez que aparezca `/dev/myfirst/0`, cámbiale el propietario a `root:operator`, establece su modo en `0660`, y crea un enlace simbólico llamado `/dev/myfirst-primary` que apunte a él. Reinicia el servicio devfs para aplicar los cambios en un sistema en ejecución:

```sh
% sudo service devfs restart
```

`devfs.conf` es adecuado para entornos de laboratorio pequeños y estables. No es un motor de políticas. Si necesitas reglas condicionales o filtrado específico por jail, usa `devfs.rules`.

### devfs.rules: basado en reglas, usado por jails

`devfs.rules` describe conjuntos de reglas con nombre; cada conjunto de reglas es una lista de patrones y acciones. Un jail hace referencia a un conjunto de reglas por nombre en su `jail.conf(5)`, y cuando el montaje de devfs propio del jail arranca, el kernel recorre el conjunto de reglas coincidente y filtra el conjunto de nodos en consecuencia. El formato está documentado en `devfs(8)` y `devfs.rules(5)`.

Un ejemplo breve:

```text
# /etc/devfs.rules

[myfirst_lab=10]
add path 'myfirst/*' unhide
add path 'myfirst/*' mode 0660 group operator
```

Esto define un conjunto de reglas numerado `10`, llamado `myfirst_lab`. Muestra cualquier nodo bajo `myfirst/` (los jails ocultan los nodos por defecto), y luego los configura como legibles y escribibles para el grupo `operator`. Para usar el conjunto de reglas, nómbralo en `jail.conf`:

```ini
devfs_ruleset = 10;
```

En este capítulo no vamos a configurar una jail. Lo importante aquí es el reconocimiento: cuando veas `devfs_ruleset` en una configuración de jail o `service devfs restart` en la documentación de un operador, estás ante una política que se sitúa por encima de lo que tu driver expone, no dentro de él. Mantén tu driver honesto en el nivel base y deja que estos archivos determinen lo que el operador permite.

### La sintaxis completa de devfs.conf

`devfs.conf` tiene una gramática pequeña y estable. Cada línea es una directiva. Las líneas en blanco y las que comienzan con `#` se ignoran. Un `#` en cualquier punto de una línea inicia un comentario hasta el final de esa línea. Solo existen tres palabras clave de directiva:

- **`own   path   user[:group]`**: cambia la propiedad de `path` al usuario indicado en `user` y, si se especifica `:group`, también al grupo correspondiente. El usuario y el grupo pueden ser nombres que existan en la base de datos de contraseñas o identificadores numéricos.
- **`perm  path   mode`**: cambia el modo de `path` al modo octal indicado. El cero inicial es opcional pero convencional.
- **`link  path   linkname`**: crea un enlace simbólico en `/dev/linkname` que apunta a `/dev/path`.

Cada directiva actúa sobre el nodo cuya ruta se indica relativa a `/dev`. La ruta puede nombrar un dispositivo directamente o puede ser un glob que coincida con una familia de dispositivos. Los caracteres glob son `*`, `?` y clases de caracteres entre corchetes.

La acción se aplica cuando el nodo aparece por primera vez en `/dev`. Para los nodos que existen en el arranque, esto ocurre durante la fase temprana de `service devfs start`. Para los nodos que aparecen más tarde, por ejemplo cuando se carga un módulo de driver, la acción se aplica cuando el cdev correspondiente se añade a devfs.

El efecto de `service devfs restart` en un sistema en ejecución es volver a ejecutar todas las directivas de `/etc/devfs.conf` sobre todo lo que existe actualmente en `/dev`. Así es como se aplica una directiva recién añadida a dispositivos que ya existen.

### Lectura del ejemplo de devfs.conf

El sistema base de FreeBSD incluye un ejemplo comentado en `/etc/defaults/devfs.conf` (o una ruta equivalente; el archivo se instala junto con el sistema base). El código fuente en `/usr/src/sbin/devfs/devfs.conf` es instructivo:

```console
# Commonly used by many ports
#link   cd0     cdrom
#link   cd0     dvd

# Allow a user in the wheel group to query the smb0 device
#perm   smb0    0660

# Allow members of group operator to cat things to the speaker
#own    speaker root:operator
#perm   speaker 0660
```

El archivo es casi todo comentarios, que es el estado esperado: FreeBSD se instala sin ninguna directiva activa en `devfs.conf`. Los operadores que necesitan cambios específicos al host los añaden en `/etc/devfs.conf`. El Laboratorio 8.4 de este capítulo añade entradas para `myfirst/0`.

### Ejemplos prácticos que se repiten habitualmente

Hay tres patrones de `devfs.conf` que aparecen con frecuencia. Merece la pena mostrar cada uno al menos una vez.

**Conceder acceso a un nodo de estado a una herramienta de monitorización.** Supón que un driver expone `/dev/something/status` como `root:wheel 0600` y que quieres que una herramienta de monitorización local que se ejecuta como usuario sin privilegios pueda leerlo. La solución más sencilla es un grupo dedicado:

```text
# /etc/devfs.conf
own     something/status        root:monitor
perm    something/status        0640
```

Tras ejecutar `service devfs restart`, el nodo es legible por los miembros del grupo `monitor`. Crea el grupo con `pw groupadd monitor` y añade los usuarios correspondientes.

**Proporcionar un nombre estable y cómodo para un dispositivo renumerado.** Supón que el driver creaba antes `/dev/old_name` y ahora crea `/dev/new_name/0`, y que tienes scripts que aún hacen referencia a la ruta antigua. Una directiva `link` preserva la compatibilidad:

```text
link    new_name/0      old_name
```

`/dev/old_name` se convierte en un enlace simbólico que apunta a `/dev/new_name/0`. El enlace no tiene política propia; la propiedad y el modo provienen del destino.

**Ampliar un permiso restrictivo por defecto en un sistema de laboratorio.** Supón que un driver tiene por defecto `root:wheel 0600` y que en una máquina de laboratorio quieres que el administrador local pueda interactuar con el nodo sin `sudo`. En lugar de modificar el driver, añade al administrador local el grupo operator y amplía el modo en `devfs.conf`:

```text
own     myfirst/0       root:operator
perm    myfirst/0       0660
```

Esto es exactamente la configuración del Laboratorio 8.4. Se circunscribe a la máquina de laboratorio y no altera el valor por defecto de entrega del driver.

### devfs.rules en profundidad

`devfs.rules` es una bestia diferente. En lugar de aplicar directivas puntuales a una ruta, define **rulesets con nombre** que un montaje de devfs puede referenciar. Cada ruleset es una lista de reglas; cada regla coincide con rutas por patrón y aplica acciones.

El archivo reside en `/etc/devfs.rules` y el sistema base incluye valores por defecto en `/etc/defaults/devfs.rules`. El formato está documentado en `devfs.rules(5)` y `devfs(8)`.

Un ruleset se introduce mediante una cabecera entre corchetes:

```text
[rulesetname=number]
```

`number` es un entero pequeño, la forma en que devfs identifica el ruleset internamente. `rulesetname` es una etiqueta legible por humanos que se usa en la configuración de jaulas. Las reglas que siguen a una cabecera pertenecen a ese ruleset hasta la siguiente cabecera.

Una regla comienza con la palabra clave `add` e indica un patrón de ruta y una acción. Las acciones más comunes son:

- **`unhide`**: hace visibles los nodos que coinciden. Los rulesets que derivan de `devfsrules_hide_all` usan esta acción para crear una lista blanca de nodos específicos.
- **`hide`**: hace invisibles los nodos que coinciden. Se usa para eliminar algo del conjunto por defecto.
- **`group name`**: cambia el grupo de los nodos que coinciden.
- **`user name`**: cambia el propietario.
- **`mode N`**: cambia el modo al valor octal `N`.
- **`include $name`**: incluye las reglas de otro ruleset llamado `$name`.

Las directivas de inclusión son el mecanismo mediante el que los rulesets incluidos en FreeBSD se componen. El ruleset `devfsrules_jail` comienza con `add include $devfsrules_hide_all` para establecer una base limpia, luego incluye `devfsrules_unhide_basic` para el pequeño conjunto de nodos que cualquier programa razonable espera encontrar, después `devfsrules_unhide_login` para los PTY y los descriptores estándar, y a continuación añade algunas rutas específicas de la jaula.

### Lectura de los rulesets por defecto

El código fuente de FreeBSD incluye `/etc/defaults/devfs.rules` (instalado desde `/usr/src/sbin/devfs/devfs.rules`). Leerlo una vez te da un modelo de cómo se encapan las reglas.

```text
[devfsrules_hide_all=1]
add hide
```

El ruleset 1 oculta todos los nodos de devfs. Es el punto de partida para los rulesets de jaula que necesitan crear listas blancas de nodos.

```text
[devfsrules_unhide_basic=2]
add path null unhide
add path zero unhide
add path crypto unhide
add path random unhide
add path urandom unhide
```

El ruleset 2 desoculta los pseudodispositivos básicos que cualquier proceso razonable espera encontrar.

```text
[devfsrules_unhide_login=3]
add path 'ptyp*' unhide
add path 'ptyq*' unhide
/* ... many more PTY paths ... */
add path ptmx unhide
add path pts unhide
add path 'pts/*' unhide
add path fd unhide
add path 'fd/*' unhide
add path stdin unhide
add path stdout unhide
add path stderr unhide
```

El ruleset 3 desoculta la infraestructura de TTY y descriptores de archivo de la que dependen los usuarios con sesión iniciada.

```text
[devfsrules_jail=4]
add include $devfsrules_hide_all
add include $devfsrules_unhide_basic
add include $devfsrules_unhide_login
add path fuse unhide
add path zfs unhide
```

El ruleset 4 combina los tres rulesets anteriores y añade `fuse` y `zfs`. Es el ruleset por defecto que usa la mayoría de las jaulas.

```text
[devfsrules_jail_vnet=5]
add include $devfsrules_hide_all
add include $devfsrules_unhide_basic
add include $devfsrules_unhide_login
add include $devfsrules_jail
add path pf unhide
```

El ruleset 5 es el ruleset 4 más el nodo de control del filtro de paquetes. Se usa en jaulas que necesitan acceso a `pf`.

Leerlos con detenimiento es una inversión que vale la pena. Todos los patrones que vayas a necesitar para tus propios rulesets están en alguno de ellos.

### Un ejemplo completo de jaula de principio a fin

Para aterrizar la teoría, aquí tienes un ejemplo completo que puedes aplicar en un sistema de laboratorio. Se asume que has compilado y cargado el driver de la etapa 2 del Capítulo 8 y que `/dev/myfirst/0` existe en el host.

**Paso 1: define un ruleset en `/etc/devfs.rules`.** Añade al final del archivo:

```text
[myfirst_jail=100]
add include $devfsrules_jail
add path 'myfirst'   unhide
add path 'myfirst/*' unhide
add path 'myfirst/*' mode 0660 group operator
```

El ruleset tiene el número `100` (cualquier entero pequeño no utilizado sirve; `100` está con seguridad por encima de los números incluidos en el sistema). Incluye el ruleset de jaula por defecto para que la jaula siga teniendo `/dev/null`, `/dev/zero`, los PTY y todo lo demás que una jaula normal necesita. Luego desoculta el directorio `myfirst/` y los nodos que contiene, y establece su modo y grupo.

**Paso 2: crea una jaula.** Una entrada mínima en `/etc/jail.conf`:

```text
myfirstjail {
        path = "/jails/myfirstjail";
        host.hostname = "myfirstjail.example.com";
        mount.devfs;
        devfs_ruleset = 100;
        exec.start = "/bin/sh";
        exec.stop  = "/bin/sh -c 'exit'";
        persist;
}
```

Crea el directorio raíz de la jaula:

```sh
% sudo mkdir -p /jails/myfirstjail
% sudo bsdinstall jail /jails/myfirstjail
```

Si ya tienes otro método de creación de jaulas en tu laboratorio, sustitúyelo por `bsdinstall`.

**Paso 3: arranca la jaula e inspecciona.**

```sh
% sudo service devfs restart
% sudo service jail start myfirstjail
% sudo jexec myfirstjail ls -l /dev/myfirst
total 0
crw-rw----  1 root  operator  0x5a Apr 17 10:00 0
```

El nodo aparece dentro de la jaula con la propiedad y el modo especificados en el ruleset. Si el ruleset no lo hubiera desoculto, la jaula no vería ningún directorio `myfirst` en absoluto.

**Paso 4: compruébalo.** Comenta la línea `add path 'myfirst/*' unhide` en `/etc/devfs.rules`, ejecuta `sudo service devfs restart` y vuelve a entrar en la jaula:

```sh
% sudo jexec myfirstjail ls -l /dev/myfirst
ls: /dev/myfirst: No such file or directory
```

El nodo es invisible para la jaula. El host aún lo ve. El driver no se ha recargado. La política en el archivo determina completamente lo que ve la jaula.

Este ejercicio de principio a fin es lo que el Laboratorio 8.7 recorre paso a paso. El objetivo de mostrarlo en prosa es fijar el patrón: **los rulesets controlan lo que ve la jaula, y el driver no hace nada diferente independientemente de eso**. El trabajo de tu driver es exponer una base sólida; el trabajo de los rulesets es filtrar y ajustar según el entorno.

### Depuración de discrepancias en las reglas

Cuando devfs no presenta un nodo de la manera esperada, hay algunas herramientas para diagnosticar el motivo.

- **`devfs rule show`** muestra los rulesets cargados actualmente en el kernel. Puedes compararlos con el archivo.
- **`devfs rule showsets`** lista los números de ruleset activos en ese momento.
- **`devfs rule ruleset NUM`** seguido de **`devfs rule show`** muestra las reglas de un ruleset específico.
- **`service devfs restart`** vuelve a aplicar `/etc/devfs.conf` y recarga todos los rulesets desde `/etc/devfs.rules`. Úsalo cada vez que cambies cualquiera de los dos archivos.

Errores habituales:

- Una regla usa un patrón de ruta que no coincide. Pon tus patrones glob entre comillas simples y recuerda que `myfirst/*` es diferente de `myfirst` (el directorio en sí no queda cubierto por el patrón `/*`; necesitas ambas reglas).
- Un ruleset es referenciado por una jaula pero no está presente en `/etc/devfs.rules`. El `/dev` de la jaula acaba sin que se le aplique ninguna regla coincidente, lo que habitualmente resulta en un sistema de archivos oculto por defecto.
- Un ruleset está presente pero no se ha reiniciado el servicio. Después de añadir una regla, ejecuta `service devfs restart` para propagarla realmente al kernel.

### Manipulación en tiempo de ejecución con devfs(8)

`devfs(8)` es la herramienta de administración de bajo nivel que `service devfs restart` usa internamente. Puedes invocarla directamente para aplicar cambios sin reiniciar ni el sistema ni todo el subsistema devfs.

```sh
% sudo devfs rule -s 100 add path 'myfirst/*' unhide
```

Esto añade una regla al ruleset 100 en el kernel en ejecución, sin tocar el archivo. Es útil para experimentar. Las reglas añadidas de esta manera no persisten entre reinicios.

```sh
% sudo devfs rule showsets
0 1 2 3 4 5 100
```

Muestra qué números de ruleset están cargados en ese momento.

```sh
% sudo devfs rule -s 100 del 1
```

Elimina la regla número 1 del ruleset 100.

En producción raramente necesitarás tocar `devfs(8)` directamente; el flujo de trabajo basado en archivos y `service devfs restart` son suficientes para la mayoría de los casos. Durante la depuración de un ruleset problemático, el comando interactivo resulta invaluable.

### Una advertencia sobre el momento de aplicación de devfs.conf

Hay un patrón que pilla desprevenidos a los nuevos: añades una línea en `devfs.conf`, reinicias, y descubres que la línea no ha surtido efecto. La causa habitual es el orden de arranque. `service devfs start` se ejecuta en una fase temprana de la secuencia de boot, antes de que se carguen algunos módulos. Los nodos creados más tarde por módulos que se cargan después no serán procesados por las directivas que ya se ejecutaron, a menos que reinicies el servicio devfs tras cargar el módulo.

En la práctica, esto significa:

1. Si tu driver está compilado en el kernel, sus nodos existen en el momento en que se aplica `devfs.conf`. Las directivas surten efecto en el primer arranque.
2. Si tu driver se carga desde `/boot/loader.conf`, sus nodos existen antes de que arranque el espacio de usuario, por lo que las directivas se aplican con normalidad.
3. Si tu driver se carga más tarde (desde `rc.d` o manualmente), debes ejecutar `service devfs restart` después de la carga para que las directivas se apliquen a los nodos recién aparecidos.

En los laboratorios, el último caso es el más habitual. Carga el driver, ejecuta `service devfs restart` y después verifica.



## Ejercicio del dispositivo desde el espacio de usuario

Las herramientas de shell te llevarán sorprendentemente lejos. Ya las conoces del Capítulo 7:

```sh
% ls -l /dev/myfirst/0
% sudo cat </dev/myfirst/0
% echo "hello" | sudo tee /dev/myfirst/0 >/dev/null
```

Siguen siendo útiles, especialmente `ls -l` para confirmar que un cambio de permisos ha surtido efecto. Pero en algún momento querrás abrir el dispositivo desde un programa que hayas escrito tú mismo, para poder controlar los tiempos, medir el comportamiento y simular código de usuario realista. Los archivos complementarios en `examples/part-02/ch08-working-with-device-files/userland/` contienen un pequeño programa de sondeo que hace exactamente eso. Las partes relevantes tienen este aspecto:

```c
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
        const char *path = (argc > 1) ? argv[1] : "/dev/myfirst/0";
        char buf[64];
        ssize_t n;
        int fd;

        fd = open(path, O_RDWR);
        if (fd < 0)
                err(1, "open %s", path);

        n = read(fd, buf, sizeof(buf));
        if (n < 0)
                err(1, "read %s", path);

        printf("read %zd bytes from %s\n", n, path);

        if (close(fd) != 0)
                err(1, "close %s", path);

        return (0);
}
```

Hay dos cosas que conviene notar. En primer lugar, no hay nada específico del dispositivo en el código. Es el mismo `open`, `read`, `close` que escribirías para un archivo normal. Ahí se aprecia el valor de la tradición UNIX. En segundo lugar, compilar y ejecutar este programa te da una manera repetible de ejercitar tu driver sin tener que preocuparte por las comillas del shell. En el Capítulo 9 lo extenderás para escribir datos, medir conteos de bytes y comparar el estado por descriptor entre descriptores distintos.

Ejecutarlo una vez contra tu driver de la Etapa 2 debería producir algo parecido a esto:

```sh
% cc -Wall -Werror -o probe_myfirst probe_myfirst.c
% sudo ./probe_myfirst
read 0 bytes from /dev/myfirst/0
```

Cero bytes, porque `d_read` sigue devolviendo EOF. El número es aburrido; el hecho de que todo el camino haya funcionado, no.

### Un segundo sondeo: inspección con stat(2)

Leer los metadatos de un nodo de dispositivo es tan instructivo como abrirlo. Tanto el comando `stat(1)` de FreeBSD como la llamada al sistema `stat(2)` informan de lo que devfs anuncia. Un pequeño programa construido en torno a `stat(2)` permite comparar con facilidad un nodo primario y un alias, y confirmar que ambos resuelven al mismo cdev.

El archivo fuente de ejemplo `examples/part-02/ch08-working-with-device-files/userland/stat_myfirst.c` tiene este aspecto:

```c
#include <err.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

int
main(int argc, char **argv)
{
        struct stat sb;
        int i;

        if (argc < 2) {
                fprintf(stderr, "usage: %s path [path ...]\n", argv[0]);
                return (1);
        }

        for (i = 1; i < argc; i++) {
                if (stat(argv[i], &sb) != 0)
                        err(1, "stat %s", argv[i]);
                printf("%s: mode=%06o uid=%u gid=%u rdev=%#jx\n",
                    argv[i],
                    (unsigned)(sb.st_mode & 07777),
                    (unsigned)sb.st_uid,
                    (unsigned)sb.st_gid,
                    (uintmax_t)sb.st_rdev);
        }
        return (0);
}
```

Ejecutarlo sobre el nodo primario y el alias debería mostrar el mismo `rdev` en ambas rutas:

```sh
% sudo ./stat_myfirst /dev/myfirst/0 /dev/myfirst
/dev/myfirst/0: mode=020660 uid=0 gid=5 rdev=0x5a
/dev/myfirst:   mode=020660 uid=0 gid=5 rdev=0x5a
```

`rdev` es el identificador que devfs emplea para etiquetar el nodo, y constituye la prueba más sencilla de que dos nombres hacen referencia al mismo cdev subyacente. Los bits altos `020000` del modo indican «archivo especial de caracteres»; los bits bajos son el conocido `0660`.

### Una tercera prueba: aperturas en paralelo

El driver de la etapa 2 permite que varios procesos mantengan el dispositivo abierto al mismo tiempo, y cada uno obtiene su propia estructura por apertura. Una buena forma de confirmar el cableado es ejecutar un programa que abra el nodo varias veces desde el mismo proceso, mantenga todos los descriptores un momento y notifique lo que ha ocurrido.

El fuente de acompañamiento `examples/part-02/ch08-working-with-device-files/userland/parallel_probe.c` hace exactamente eso:

```c
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_FDS 8

int
main(int argc, char **argv)
{
        const char *path = (argc > 1) ? argv[1] : "/dev/myfirst/0";
        int fds[MAX_FDS];
        int i, n;

        n = (argc > 2) ? atoi(argv[2]) : 4;
        if (n < 1 || n > MAX_FDS)
                errx(1, "count must be 1..%d", MAX_FDS);

        for (i = 0; i < n; i++) {
                fds[i] = open(path, O_RDWR);
                if (fds[i] < 0)
                        err(1, "open %s (fd %d of %d)", path, i + 1, n);
                printf("opened %s as fd %d\n", path, fds[i]);
        }

        printf("holding %d descriptors; press enter to close\n", n);
        (void)getchar();

        for (i = 0; i < n; i++) {
                if (close(fds[i]) != 0)
                        warn("close fd %d", fds[i]);
        }
        return (0);
}
```

Ejecútalo y observa `dmesg` al mismo tiempo:

```sh
% sudo ./parallel_probe /dev/myfirst/0 4
opened /dev/myfirst/0 as fd 3
opened /dev/myfirst/0 as fd 4
opened /dev/myfirst/0 as fd 5
opened /dev/myfirst/0 as fd 6
holding 4 descriptors; press enter to close
```

Deberías ver cuatro líneas `open via myfirst/0 fh=<ptr> (active=N)` en `dmesg`, cada una con un puntero distinto. Cuando pulses Enter, aparecerán cuatro líneas `per-open dtor fh=<ptr>` a medida que se cierra cada descriptor. Esta es la evidencia más sólida de que el estado por apertura es realmente por descriptor.

### Una cuarta prueba: prueba de estrés

Una prueba de estrés corta ejercita repetidamente el camino del destructor y detecta fugas que una prueba de apertura única pasaría por alto. `examples/part-02/ch08-working-with-device-files/userland/stress_probe.c` itera abriendo y cerrando en bucle:

```c
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
        const char *path = (argc > 1) ? argv[1] : "/dev/myfirst/0";
        int iters = (argc > 2) ? atoi(argv[2]) : 1000;
        int i, fd;

        for (i = 0; i < iters; i++) {
                fd = open(path, O_RDWR);
                if (fd < 0)
                        err(1, "open (iter %d)", i);
                if (close(fd) != 0)
                        err(1, "close (iter %d)", i);
        }
        printf("%d iterations completed\n", iters);
        return (0);
}
```

Ejecútala contra el driver cargado y verifica que el contador de aperturas activas vuelve a cero:

```sh
% sudo ./stress_probe /dev/myfirst/0 10000
10000 iterations completed
% sysctl dev.myfirst.0.stats.active_fhs
dev.myfirst.0.stats.active_fhs: 0
% sysctl dev.myfirst.0.stats.open_count
dev.myfirst.0.stats.open_count: 10000
```

Si `active_fhs` se estabiliza por encima de cero después de que el programa termine, el destructor no se está ejecutando en algún camino y tienes una fuga real que investigar. Si `open_count` coincide con el número de iteraciones, cada apertura fue registrada. La prueba de estrés es un instrumento tosco, pero es rápida y detecta los errores más comunes.

### Observar eventos de devd

`devd(8)` es el demonio del espacio de usuario que reacciona a los eventos de dispositivos. Cada vez que un cdev aparece o desaparece, devd recibe una notificación y puede ejecutar una acción configurada. No es necesario configurar devd para ver sus eventos; puedes suscribirte al flujo de eventos directamente a través del socket `/var/run/devd.pipe`.

Un pequeño script de apoyo, `examples/part-02/ch08-working-with-device-files/userland/devd_watch.sh`, lo conecta todo:

```sh
#!/bin/sh
# Print devd events related to the myfirst driver.
nc -U /var/run/devd.seqpacket.pipe | grep -i 'myfirst'
```

Ejecútalo en un terminal y, en otro, carga y descarga el driver:

```sh
% sudo sh ./devd_watch.sh &
% sudo kldload ./myfirst.ko
!system=DEVFS subsystem=CDEV type=CREATE cdev=myfirst/0
% sudo kldunload myfirst
!system=DEVFS subsystem=CDEV type=DESTROY cdev=myfirst/0
```

Deberías ver notificaciones `CREATE` y `DESTROY` con el nombre del cdev. Así es como los operadores construyen reacciones automáticas: una regla de `devd` en `/etc/devd.conf` puede coincidir con estos eventos y ejecutar un script en respuesta. El desafío 5 al final de este capítulo te pide que escribas una regla mínima de `devd.conf` que registre los eventos de `myfirst`.

### Gestionar errno en operaciones sobre dispositivos

Una buena prueba en el espacio de usuario hace algo más que llamar a `open` y salir. Distingue entre los distintos valores de errno que devuelve el kernel y toma medidas sensatas en cada caso. Las pruebas de este capítulo usan todas `err(3)` para imprimir un mensaje legible y salir, lo cual es correcto para una herramienta de experimentación pequeña. El código en espacio de usuario de producción que trabaja sobre nodos de dispositivo suele tener un aspecto más parecido a este:

```c
fd = open(path, O_RDWR);
if (fd < 0) {
        switch (errno) {
        case ENXIO:
                /* Driver not ready yet. Retry or report. */
                warnx("%s: driver not ready", path);
                break;
        case EBUSY:
                /* Node is exclusive and already open. */
                warnx("%s: in use by another process", path);
                break;
        case EACCES:
                /* Permission denied. */
                warnx("%s: permission denied", path);
                break;
        case ENOENT:
                /* Node does not exist. Is the driver loaded? */
                warnx("%s: not present", path);
                break;
        default:
                warn("%s", path);
                break;
        }
        return (1);
}
```

Merece la pena interiorizar esa tabla de valores errno. La sección 13 del capítulo 8 la trata como un tema de primera importancia, porque los mismos valores aparecen en el lado del driver y decidir cuál devolver es una decisión de diseño.

### Controlar dispositivos desde scripts de shell

Antes de recurrir a un programa C en el espacio de usuario, recuerda que las herramientas de shell cubren muchos casos de manera adecuada. Para `myfirst`, un puñado de comandos simples resultan útiles:

```sh
# Verify the node exists and report its ownership and mode.
ls -l /dev/myfirst/0

# Open and immediately close the device, for probe purposes.
sudo sh -c ': </dev/myfirst/0'

# Read once from the device and pipe the result into hexdump.
sudo cat /dev/myfirst/0 | hexdump -C | head

# Hold the device open for a few seconds with a background shell.
sudo sh -c 'exec 3</dev/myfirst/0; sleep 5; exec 3<&-'

# Show what processes currently have the device open.
sudo fstat /dev/myfirst/0
```

Cada uno de estos es un movimiento de depuración independiente y, juntos, constituyen un kit de herramientas de shell útil. Cuando puedas expresar tu prueba en shell, el shell suele ser el camino más rápido.



## Leer drivers reales de FreeBSD a través de la lente del archivo de dispositivo

Nada afianza el modelo de archivos de dispositivo como leer drivers que han tenido que resolver los mismos problemas que ahora estás resolviendo tú. Esta sección es un recorrido guiado por tres drivers de `/usr/src/sys`. El objetivo no es entender cada driver de principio a fin. El objetivo es ver cómo cada uno de ellos da forma a su archivo de dispositivo, para que vayas construyendo una biblioteca de patrones en tu cabeza.

Cada recorrido sigue la misma estructura: abre el archivo, encuentra el `cdevsw`, encuentra la llamada a `make_dev`, encuentra la llamada a `destroy_dev`, observa lo que es idiomático y lo que resulta inusual.

### Recorrido 1: /usr/src/sys/dev/null/null.c

El módulo `null` es el ejemplo bueno más pequeño del árbol. Ábrelo en un editor. Es lo bastante corto como para leerlo de una sola vez.

Lo primero que hay que observar: hay **tres** estructuras `cdevsw` en un mismo archivo.

```c
static struct cdevsw full_cdevsw = {
        .d_version =    D_VERSION,
        .d_read =       zero_read,
        .d_write =      full_write,
        .d_ioctl =      zero_ioctl,
        .d_name =       "full",
};

static struct cdevsw null_cdevsw = {
        .d_version =    D_VERSION,
        .d_read =       (d_read_t *)nullop,
        .d_write =      null_write,
        .d_ioctl =      null_ioctl,
        .d_name =       "null",
};

static struct cdevsw zero_cdevsw = {
        .d_version =    D_VERSION,
        .d_read =       zero_read,
        .d_write =      null_write,
        .d_ioctl =      zero_ioctl,
        .d_name =       "zero",
        .d_flags =      D_MMAP_ANON,
};
```

Tres nodos distintos, tres valores `cdevsw` distintos, ningún softc. El módulo registra tres cdevs en su manejador `MOD_LOAD`:

```c
full_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &full_cdevsw, 0,
    NULL, UID_ROOT, GID_WHEEL, 0666, "full");
null_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &null_cdevsw, 0,
    NULL, UID_ROOT, GID_WHEEL, 0666, "null");
zero_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &zero_cdevsw, 0,
    NULL, UID_ROOT, GID_WHEEL, 0666, "zero");
```

Fíjate en `MAKEDEV_ETERNAL_KLD`. Cuando este código se compila de forma estática dentro del kernel, la macro se expande a `MAKEDEV_ETERNAL` y marca los cdevs como que nunca deben destruirse. Cuando el mismo código se construye como módulo cargable, la macro se expande a cero y los cdevs pueden destruirse durante la descarga.

Observa también el modo `0666` y `root:wheel`. Todo lo que sirve el módulo null es intencionadamente accesible para todos.

La descarga es tan simple como la carga:

```c
destroy_dev(full_dev);
destroy_dev(null_dev);
destroy_dev(zero_dev);
```

Un `destroy_dev` por cdev. Ningún softc que desmontar. Ningún estado por apertura. Ningún locking más allá del que proporciona el kernel. Esto es lo que tiene pinta de mínimo.

**Qué copiar de null:** el hábito de establecer `d_version`, el hábito de dar a cada `cdevsw` su propio `d_name`, la simetría entre carga y descarga, la disposición a usar manejadores con nombres simples en lugar de inventar abstracciones.

**Qué dejar:** `MAKEDEV_ETERNAL_KLD`. Tu driver debe ser descargable, así que no quieres el flag eternal. El módulo `null` es especial porque los nodos que crea son anteriores a casi cualquier otro subsistema del kernel y se espera que permanezcan activos durante toda la vida del kernel.

### Recorrido 2: /usr/src/sys/dev/led/led.c

El framework de LEDs es un paso más en complejidad estructural. Sigue siendo lo bastante pequeño como para leerlo en una sentada. Donde `null` no tiene softc, `led` tiene un softc completo por LED. Donde `null` crea tres singletons, `led` crea un cdev por LED bajo demanda.

Fíjate primero en el único `cdevsw`:

```c
static struct cdevsw led_cdevsw = {
        .d_version =    D_VERSION,
        .d_write =      led_write,
        .d_name =       "LED",
};
```

Un solo `cdevsw` para todos los LEDs. El framework lo utiliza para cada cdev que crea, y se apoya en `si_drv1` para distinguir entre ellos. El minimalismo de esta definición es en sí mismo una lección: `led` no implementa `d_open`, `d_close` ni `d_read`, porque toda interacción que un operador tiene con un LED es una cadena de patrón escrita con `echo`. Leer del nodo no tiene sentido y no hay estado de sesión que rastrear en la apertura, así que el driver simplemente deja esos campos sin establecer. devfs interpreta cada ranura a `NULL` como "usa el comportamiento predeterminado", que para `d_read` es devolver cero bytes y para `d_open` y `d_close` es no hacer nada. Ten esto presente cuando diseñes tus propios valores de `cdevsw`: rellena lo que tu dispositivo realmente necesita y deja el resto en paz.

El softc por LED vive en una `struct ledsc` definida cerca del principio del archivo:

```c
struct ledsc {
        LIST_ENTRY(ledsc)       list;
        char                    *name;
        void                    *private;
        int                     unit;
        led_t                   *func;
        struct cdev             *dev;
        /* ... more state ... */
};
```

Lleva un puntero de vuelta a su cdev en el campo `dev`, y un número de unidad asignado desde un pool `unrhdr(9)` en lugar de desde Newbus:

```c
sc->unit = alloc_unr(led_unit);
```

La llamada real a `make_dev` está justo debajo:

```c
sc->dev = make_dev(&led_cdevsw, sc->unit,
    UID_ROOT, GID_WHEEL, 0600, "led/%s", name);
```

Fíjate en la ruta: `"led/%s"`. Cada LED creado por el framework cae en el subdirectorio `/dev/led/` con un nombre libre elegido por el driver que hace la llamada (por ejemplo, `led/ehci0`). Así es como el framework mantiene sus nodos agrupados.

Inmediatamente después del `make_dev`, el framework guarda el puntero al softc:

```c
sc->dev->si_drv1 = sc;
```

Esta es la forma anterior a `mda_si_drv1` de hacerlo, y es anterior a `make_dev_s`. Los drivers más modernos deberían pasar `mda_si_drv1` a través de la estructura args, de modo que el puntero quede establecido antes de que el cdev sea accesible.

La destrucción es una única llamada:

```c
destroy_dev(dev);
```

Simple. Síncrona. Sin destrucción diferida, sin bucle de drenado en el nivel del llamante. El framework confía en el comportamiento de drenado del kernel en `destroy_dev` para terminar cualquier manejador en vuelo.

**Qué copiar de led:** la convención de nombres (subdirectorio por framework), la disposición del softc (puntero de vuelta más campos de identidad más puntero a callback), el patrón limpio de `alloc_unr` / `free_unr` para números de unidad que no provienen de Newbus.

**Qué dejar:** la asignación `sc->dev->si_drv1 = sc` después de `make_dev`. Usa `mda_si_drv1` en `make_dev_s` en su lugar.

### Recorrido 3: /usr/src/sys/dev/md/md.c

El driver de disco en memoria es mayor que los dos anteriores, y la mayor parte de su volumen no tiene que ver con los archivos de dispositivo en absoluto. Tiene que ver con GEOM, con el almacenamiento de respaldo, con instancias respaldadas por swap y por vnode. Para nuestros propósitos, nos fijamos en una cosa concreta: el nodo de control `/dev/mdctl`.

Busca la declaración del `cdevsw` cerca del principio de `md.c`:

```c
static struct cdevsw mdctl_cdevsw = {
        .d_version =    D_VERSION,
        .d_ioctl =      mdctlioctl,
        .d_name =       MD_NAME,
};
```

Solo dos campos establecidos. `d_version`, `d_ioctl` y un nombre. Sin `d_open`, `d_close`, `d_read` ni `d_write`. El nodo de control se usa exclusivamente a través de `ioctl(2)`: crear un md, asociar un almacenamiento de respaldo, destruir un md. Esa es la forma que tienen muchas interfaces de control en el árbol.

El cdev se crea cerca del final del archivo:

```c
status_dev = make_dev(&mdctl_cdevsw, INT_MAX, UID_ROOT, GID_WHEEL,
    0600, MDCTL_NAME);
```

`INT_MAX` es un patrón habitual para los singletons cuando el número de unidad no importa: sitúa el cdev fuera de cualquier rango plausible de números de unidad de instancias de driver. `0600` y `root:wheel` son la base estrecha que esperarías para un nodo de control con privilegios.

La destrucción ocurre en el camino de descarga del módulo:

```c
destroy_dev(status_dev);
```

De nuevo, una única llamada.

**Qué copiar de md:** el patrón de exponer un único cdev de control para un subsistema cuyo camino de datos vive en otro lugar (en el caso de md, en GEOM), y los permisos muy restrictivos para un nodo que tiene privilegios reales.

**Qué dejar:** `md` es un subsistema grande; no intentes copiar su estructura como plantilla. Copia la idea del nodo de control; deja la capa GEOM para el capítulo 27.

### Una breve nota sobre el cloning

El framework de LEDs crea un cdev por LED en respuesta a una llamada de API procedente de otros drivers. El módulo `md` crea un cdev por disco en memoria en respuesta a un `ioctl` sobre su nodo de control. Ambos son ejemplos de **drivers que crean cdevs de forma dinámica en respuesta a eventos**.

FreeBSD también dispone de un mecanismo de cloning dedicado: `clone_create(9)` y el manejador de eventos `dev_clone`. Cuando un usuario abre un nombre que coincide con un patrón registrado por un driver de cloning, el kernel sintetiza un nuevo cdev, lo abre y devuelve el descriptor. Históricamente fue un patrón habitual en los subsistemas que querían un nuevo cdev por sesión en cada apertura. El FreeBSD moderno se apoya en `devfs_set_cdevpriv(9)` siempre que la única razón para clonar un cdev era dar a cada descriptor estado independiente, porque el estado por apertura es más simple, más ligero y no necesita un pool de números de minor. El cloning verdadero sigue existiendo en el árbol (el subsistema PTY bajo `/dev/pts/*` es el ejemplo vivo más claro), y la superficie de API que lo soporta merece ser reconocida.

El cloning es flexible y no lo cubriremos en el capítulo 8. Varios de los elementos de API que ya hemos visto (el flag `D_NEEDMINOR`, `dev_stdclone`, las constantes `CLONE_*`) existen para soportarlo. Mencionarlos aquí te da vocabulario para el día en que te encuentres un driver que los utilice. Por ahora, la conclusión es que FreeBSD tiene un espectro de mecanismos para crear cdevs, y `make_dev_s` desde `attach` es el extremo más sencillo de ese espectro.

### Recorrido 4: /usr/src/sys/net/bpf.c (solo la superficie del archivo de dispositivo)

BPF, el Berkeley Packet Filter, es un subsistema grande. No intentaremos entender qué hace a nivel de red; la parte 6 de este libro tiene un capítulo dedicado a los drivers de red. Aquí nos fijamos en una cosa concreta: cómo BPF da forma a su superficie de archivo de dispositivo.

Las declaraciones relevantes están cerca del principio de `/usr/src/sys/net/bpf.c`. El `cdevsw` está poblado con el conjunto completo de operaciones del camino de datos más poll y kqueue:

```c
static struct cdevsw bpf_cdevsw = {
        .d_version =    D_VERSION,
        .d_open =       bpfopen,
        .d_read =       bpfread,
        .d_write =      bpfwrite,
        .d_ioctl =      bpfioctl,
        .d_poll =       bpfpoll,
        .d_name =       "bpf",
        .d_kqfilter =   bpfkqfilter,
};
```

Sin `d_close`. BPF hace todo en el cierre a través del destructor cdevpriv, que es el patrón que este capítulo recomienda. La tabla de despacho dice "para estas operaciones, llama a estos manejadores, y no hagas nada especial en el cierre porque el destructor se encarga de ello".

El handler de open, ya citado en la sección sobre el estado por apertura, encaja exactamente con el patrón:

```c
d = malloc(sizeof(*d), M_BPF, M_WAITOK | M_ZERO);
error = devfs_set_cdevpriv(d, bpf_dtor);
if (error != 0) {
        free(d, M_BPF);
        return (error);
}
```

Reservar memoria, registrar con devfs, liberar en caso de fallo en el registro. Nada más en el camino del open importa para nuestros propósitos aquí.

El destructor es la parte interesante. El `bpf_dtor` de BPF tiene que deshacer una cantidad considerable de estado: detiene un callout, se desconecta de su interfaz BPF, vacía un conjunto de selección y libera referencias. Todo eso lo hace **sin llamar nunca a `d_close`** desde el cdevsw. La limpieza basada en el destructor es más limpia que la basada en close para un driver que admite múltiples aperturas, porque el destructor se dispara exactamente una vez por apertura, mientras que `d_close` sin `D_TRACKCLOSE` solo se dispara en el último cierre del archivo compartido final.

La disposición del cdev en el lado de BPF es más sencilla de lo que sugiere una primera lectura del subsistema. BPF crea exactamente un nodo primario y un alias:

```c
dev = make_dev(&bpf_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, "bpf");
make_dev_alias(dev, "bpf0");
```

Esa es la superficie completa de registro de dispositivo. No existe un cdev por instancia `/dev/bpfN`; los nombres que puedes ver en un sistema en ejecución, como `/dev/bpf0` y `/dev/bpf`, resuelven ambos al mismo cdev, y cada usuario distinto de BPF se distingue únicamente por su estructura por apertura, vinculada a través de `devfs_set_cdevpriv` en el momento de la apertura. Esa es exactamente la forma que adoptarás para `myfirst` al final de este capítulo: un cdev, un alias, estado por descriptor y un destructor que gestiona el desmontaje. BPF es un subsistema grande, pero su superficie de archivo de dispositivo es algo que ahora sabes cómo escribir.

**Qué copiar de BPF:** la práctica de centralizar la limpieza por apertura en el destructor en lugar de en `d_close`. La práctica de asignar el estado por apertura de inmediato y registrarlo antes de cualquier otro trabajo. La disciplina de liberar la asignación en caso de fallo en el registro. La disposición a mantener el número de cdev pequeño y dejar que el descriptor lleve la sesión.

**Qué dejar de lado:** la maquinaria específica de BPF que rodea los caminos de open y close, incluida la lógica de attach de interfaz, la contabilidad del conjunto de selección y las estadísticas basadas en contadores. Esos pertenecen al lado de red de BPF, y la Parte 6 los revisará cuando el libro introduzca los drivers de red.

### Una síntesis de los cuatro drivers

Con cuatro recorridos en tu haber, resulta útil alinear las similitudes y las diferencias en una sola tabla. Cada fila es una propiedad del driver; cada columna es un driver.

| Propiedad                         | `null`       | `led`          | `md`                  | `bpf`                     |
|-----------------------------------|--------------|----------------|-----------------------|---------------------------|
| Cuántos valores `cdevsw`          | 3            | 1              | 1 (más GEOM)          | 1                         |
| cdevs por attach                  | 3 en total   | 1 por LED      | 1 control + varios    | 1 más 1 alias             |
| ¿Softc?                           | no           | sí             | sí                    | sí (por apertura)         |
| ¿Subdirectorio en /dev?           | no           | sí (`led/*`)   | no                    | no                        |
| Modo de permisos                  | `0666`       | `0600`         | `0600`                | `0600`                    |
| ¿Usa `devfs_set_cdevpriv`?        | no           | no             | no                    | sí                        |
| ¿Usa cloning?                     | no           | no             | no                    | no                        |
| ¿Usa `make_dev_alias`?            | no           | no             | no                    | sí                        |
| ¿`d_close` definido?              | no           | no             | no                    | no                        |
| ¿Usa `destroy_dev_drain`?         | no           | no             | no                    | no                        |
| Caso de uso principal             | pseudodatos  | control de hardware | control de subsistema | captura de paquetes   |

Cada columna es defendible. Cada driver ha elegido el conjunto más simple de funcionalidades que se ajusta a su tarea. Tu driver `myfirst` al final del Capítulo 8 tiene un perfil más cercano a `led` que al resto: un `cdevsw`, softc por instancia, nombres con subdirectorio, permisos restringidos, más `devfs_set_cdevpriv` para el estado por apertura (que `led` no necesita) y un alias (que `led` no utiliza).

Ese perfil es un buen punto de partida. Es lo suficientemente amplio para demostrar que has trabajado con los mecanismos reales, y lo suficientemente compacto para que cada línea del driver esté ahí por una razón.

### Lo que cuatro drivers nos enseñaron

Cuatro drivers, cuatro formas distintas, todos ellos buenos ejemplos:

- `null` es minimalismo: tres valores `cdevsw`, tres singletons, sin softc, sin estado por apertura, modo `0666` porque los datos son inofensivos.
- `led` es un framework: un `cdevsw`, muchos cdevs, cada uno con su propio softc y número de unidad, nombres con subdirectorio, permisos restringidos, solo `d_write` definido porque el dispositivo se escribe en lugar de leerse.
- `md` es una interfaz de control: un `cdevsw` con solo `d_ioctl`, un cdev singleton, `INT_MAX` como número de unidad, permisos restringidos para operaciones privilegiadas.
- `bpf` es un driver por sesión: un `cdevsw` con el conjunto completo de rutas de datos, un único cdev principal más un alias, y todo el estado por descriptor transportado a través de `devfs_set_cdevpriv(9)`.

Tu `myfirst` se parece más a `bpf` en estructura al final de este capítulo: un `cdevsw`, un cdev principal con un alias, un softc para el estado del dispositivo en su conjunto, y estado por apertura para el seguimiento por descriptor. Donde difiere es en el alcance. `bpf` implementa la ruta de datos completa y lo hace para gestionar paquetes reales. `myfirst` se detiene en la superficie. Eso está bien por ahora. Estás en buena compañía, y las habitaciones que hay detrás de la puerta se abren en el Capítulo 9.

Leer otros drivers es una habilidad que se desarrolla con el tiempo. La perspectiva del archivo de dispositivo es solo una de las varias perspectivas que aplicarás. Ten en mente los cuatro recorridos anteriores como una biblioteca de partida.



## Escenarios habituales y recetas

Esta sección es un recetario. Cada entrada describe una situación que encontrarás tarde o temprano, una receta breve para gestionarla y un indicador de qué parte del capítulo explica la maquinaria subyacente. Léelas ahora o guárdalas como referencia para cuando surjan.

### Receta 1: Un driver que no debe abrirse dos veces

**Situación.** Tu hardware tiene un estado que solo es coherente con un único usuario concurrente. Un segundo `open(2)` corrompería el estado.

**Receta.**

1. En el softc, mantén un indicador `int is_open` y un mutex.
2. En `d_open`, toma el mutex, comprueba el indicador, devuelve `EBUSY` si está activo, actívalo y suelta el mutex.
3. En `d_close`, toma el mutex, borra el indicador y suelta el mutex.
4. En `detach`, comprueba el indicador bajo el mutex y devuelve `EBUSY` si está activo.
5. **No** uses también `devfs_set_cdevpriv` para el estado por apertura; no existe el concepto de «por apertura» porque solo hay una apertura activa a la vez.

Este es el patrón del Capítulo 7. `myfirst` lo utilizó para centrar la atención en el ciclo de vida; la etapa 2 del Capítulo 8 lo abandona porque la mayoría de los drivers admiten múltiples aperturas. Usa el patrón exclusivo solo cuando las restricciones del hardware lo impongan.

### Receta 2: Un driver con desplazamientos de lectura por usuario

**Situación.** Tu dispositivo expone un flujo con capacidad de posicionamiento. Dos procesos de usuario distintos deben tener cada uno su propio desplazamiento en el flujo; ninguno debe ver que la posición del otro modifica su propia vista.

**Receta.**

1. Define `struct myfirst_fh` con un campo `off_t read_offset`.
2. En `d_open`, asigna la estructura y llama a `devfs_set_cdevpriv` con un destructor que la libere.
3. En `d_read`, llama a `devfs_get_cdevpriv` para recuperar la estructura. Usa `fh->read_offset` como punto de partida. Avánzalo según el número de bytes transferidos realmente.
4. En `d_close`, no hagas nada; el destructor se ejecuta automáticamente cuando el descriptor se libera.

El Capítulo 9 completará el cuerpo de `d_read`. El esqueleto ya está en la etapa 2.

### Receta 3: Un dispositivo con nodo de control privilegiado

**Situación.** Tu driver tiene una superficie de datos que cualquiera debería poder leer, y una superficie de control que solo los usuarios privilegiados deberían usar.

**Receta.**

1. Define dos estructuras `cdevsw`, `something_cdevsw` y `something_ctl_cdevsw`.
2. Crea dos cdevs en `attach`: uno para el nodo de datos (`0644 root:wheel` o similar) y otro para el nodo de control (`0600 root:wheel`).
3. Guarda ambos punteros en el softc. Destruye el cdev de control antes que el cdev de datos en `detach`.
4. Usa `priv_check(9)` dentro del manejador `d_ioctl` del nodo de control si deseas una verificación que vaya más allá de los permisos del archivo.

El Laboratorio 8.5 recorre este escenario. El recorrido de `md` en la sección de lectura de drivers reales es una variante del mundo real.

### Receta 4: Un nodo que aparece solo cuando se cumple una condición

**Situación.** Quieres que `/dev/myfirst/status` aparezca solo cuando el hardware del driver esté en un estado concreto, y que desaparezca en caso contrario.

**Receta.**

1. Mantén `sc->cdev_status` como un `struct cdev *` en el softc, inicializado a `NULL`.
2. En el manejador donde la condición se vuelve verdadera, llama a `make_dev_s` y guarda el puntero.
3. En el manejador donde la condición se vuelve falsa, llama a `destroy_dev` sobre el puntero y ponlo a `NULL`.
4. Protege ambas operaciones con el mutex del softc para que las transiciones concurrentes no generen condiciones de carrera.
5. En `detach`, si el puntero no es `NULL`, destrúyelo antes de desmontar los demás cdevs.

Este es el mismo patrón que el nodo de datos principal, solo que disparado por un evento diferente. Presta atención al caso en que la condición cambie repetidamente en rápida sucesión: presionarás el asignador de devfs más de lo esperado y es posible que quieras aplicar un debounce.

### Receta 5: Un nodo cuya propiedad debe cambiar según un parámetro en tiempo de ejecución

**Situación.** Un sistema de laboratorio a veces quiere que el nodo sea propiedad de `operator`, otras de `wheel`. No quieres recargar el driver para cambiar.

**Receta.**

1. Deja los valores `mda_uid` y `mda_gid` del driver en una línea base restringida (`UID_ROOT`, `GID_WHEEL`).
2. Usa `devfs.conf` para ampliarlos cuando sea necesario:

   ```
   own     myfirst/0       root:operator
   perm    myfirst/0       0660
   ```

3. Aplica los cambios con `service devfs restart`.
4. Para volver a la configuración anterior, comenta las líneas y vuelve a ejecutar `service devfs restart`.

La política vive en el espacio de usuario; el driver permanece intacto. El Laboratorio 8.4 practica esto.

### Receta 6: Un nodo que debe ser visible solo dentro de una jail

**Situación.** Un nodo debe aparecer dentro de una jail específica pero no en el host.

**Receta.**

1. En el driver, crea el nodo de forma predeterminada como siempre en el host.
2. En `/etc/devfs.rules`, crea un conjunto de reglas para la jail que oculte explícitamente el nodo:

   ```
   [special_jail=120]
   add include $devfsrules_hide_all
   add include $devfsrules_unhide_basic
   add path myfirst hide
   ```

3. Aplica el número de conjunto de reglas en el `jail.conf` de la jail.

Esta es una variante más directa del Laboratorio 8.7. Para un nodo que debe ser visible en la jail pero no en el host, la lógica se invierte: crea el nodo en el host (donde siempre se crea) y usa una regla `devfs.conf` del lado del host para restringir los permisos de modo que nada en el host lo toque.

### Receta 7: Un nodo que puede sobrevivir a terminaciones inesperadas de procesos

**Situación.** Tu driver mantiene recursos por apertura. Si un proceso cae, no debes filtrar el recurso.

**Receta.**

1. Asigna el recurso en `d_open`.
2. Registra un destructor con `devfs_set_cdevpriv`. El destructor libera el recurso.
3. Confía en que el kernel ejecutará el destructor cuando la última referencia a la `struct file` caiga, independientemente del motivo. `close(2)`, `exit(3)` o SIGKILL llegan todos al mismo camino de limpieza.

La garantía del destructor es la razón principal por la que existe `devfs_set_cdevpriv`. Por muy mal que se comporte el espacio de usuario, tu recurso se libera.

### Receta 8: Un dispositivo que debe admitir polling

**Situación.** Tu driver produce eventos, y los programas de usuario quieren usar `select(2)` o `poll(2)` sobre el dispositivo para saber cuándo hay un evento pendiente.

**Receta.** Fuera del alcance del Capítulo 8; esto es territorio del Capítulo 10. El resumen para reconocerlo: establece `.d_poll = myfirst_poll` en tu `cdevsw`, implementa el manejador para devolver la máscara apropiada de bits `POLLIN`, `POLLOUT`, `POLLERR`, y usa `selrecord(9)` para registrar el interés en una activación diferida. El Capítulo 10 recorre cada uno de estos aspectos en detalle.

### Receta 9: Un dispositivo que necesita mapearse en la memoria de usuario

**Situación.** Tu driver tiene una región de memoria compartida (buffer DMA, ventana de registros de hardware) a la que un proceso de usuario debería acceder directamente mediante `mmap(2)`.

**Receta.** Fuera del alcance del Capítulo 8; se trata en la Parte 4 cuando se introduce el acceso al hardware. Solo para reconocerlo: establece `.d_mmap = myfirst_mmap` en tu `cdevsw`, implementa el manejador para devolver la dirección de página física para cada desplazamiento y máscara de protección, y reflexiona sobre lo que ocurre cuando la memoria mapeada por el usuario está respaldada por un hardware que puede desaparecer. Este es uno de los aspectos más sutiles del trabajo con drivers.

### Receta 10: Un dispositivo que expone un registro

**Situación.** Tu driver produce mensajes de registro más voluminosos de lo que `device_printf` debería gestionar, y los programas de usuario deberían poder leerlos en orden con `read(2)`.

**Receta.**

1. Asigna un ring buffer en el softc.
2. En las rutas de código que producen eventos de registro, fórmalos en el ring buffer bajo un lock.
3. En `d_read`, copia bytes desde el ring buffer al espacio de usuario mediante `uiomove(9)` (Capítulo 9).
4. Usa el estado por apertura (un `read_offset` en el `fh`) para que cada lector drene el buffer a su propio ritmo.
5. Considera establecer `D_TRACKCLOSE` si se debe permitir que un único lector vacíe el buffer al cerrar.

Esta es la forma de varios dispositivos de registro del kernel. Vale la pena conocer el patrón; la implementación completa es un ejercicio de capítulos posteriores.

### Cuando una receta no encaja

El recetario no es exhaustivo. Cuando te enfrentes a una situación que no encaja con ninguna de las recetas anteriores, un hábito útil es hacerse tres preguntas en orden:

- **¿Se trata de identidad?** Entonces es una pregunta sobre `cdev`: nombres, subdirectorios, alias, creación, destrucción.
- **¿Se trata de política?** Entonces es una pregunta sobre permisos y política: propiedad, modo, `devfs.conf`, `devfs.rules`.
- **¿Se trata de estado?** Entonces es una pregunta de estado por apertura frente a estado por dispositivo: softc o `devfs_set_cdevpriv`.

La mayoría de las preguntas de diseño de drivers del mundo real se resuelven en una de estas tres categorías. Una vez que hayas clasificado la pregunta, el resto del capítulo te indica qué herramienta utilizar.



## Flujos de trabajo prácticos para la superficie del archivo de dispositivo

Conocer las APIs es la mitad del trabajo. Saber cuándo recurrir a cada una y cómo detectar los problemas rápidamente es la otra mitad. Esta sección recoge los flujos de trabajo que harán que los próximos capítulos transcurran sin tropiezos: el ciclo de edición de un driver, los hábitos que permiten atrapar errores a tiempo y las listas de verificación que merece la pena repasar antes de acometer un cambio importante.

### El bucle interno

El "bucle interno" es el ciclo de editar, compilar, cargar, probar, descargar y volver a editar. Los scripts del Capítulo 7 ya tienen una versión de esto. En el Capítulo 8, el bucle interno se enriquece un poco porque hay más superficies visibles al usuario que verificar.

Una secuencia útil cuando trabajas en una fase de `myfirst`:

```sh
% cd ~/drivers/myfirst
% sudo kldunload myfirst 2>/dev/null || true
% make clean && make
% sudo kldload ./myfirst.ko
% dmesg | tail -5
% ls -l /dev/myfirst /dev/myfirst/0 2>/dev/null
% sysctl dev.myfirst.0.stats
% sudo ./probe_myfirst /dev/myfirst/0
% sudo kldunload myfirst
% dmesg | tail -3
```

Cada línea tiene un propósito. La primera descarga es preventiva: la prueba anterior dejó el módulo cargado, y esto limpia el estado. El `make clean && make` reconstruye desde cero para evitar un archivo objeto desactualizado. El primer `dmesg | tail -5` muestra los mensajes de attach. El `ls -l` y el `sysctl` confirman que la superficie visible al usuario está presente y que los contadores internos están inicializados. El probe ejercita la ruta de datos. La descarga final y `dmesg` confirman los mensajes de detach.

Si algún paso produce un resultado inesperado, sabrás cuál es. Ese es el valor de automatizar el bucle en un script: no para ahorrar escritura, sino para que la señal de fallo sea inequívoca.

El helper `rebuild.sh` incluido con los ejemplos del Capítulo 7 encapsula la mayor parte de esto. El Laboratorio 8 lo reutiliza sin modificaciones.

### Leer dmesg correctamente

`dmesg` es la narrativa de lo que hizo tu driver. Aprender a leerlo bien es un hábito que merece construirse cuanto antes.

El buffer circular por defecto del kernel puede mostrar decenas de miles de líneas de actividad anterior de arranque y de ejecución. Cuando desarrollas un driver concreto, tres técnicas hacen visible el fragmento relevante:

**Limpiar antes de la prueba.** `sudo dmesg -c > /dev/null` limpia el buffer. El siguiente ciclo de carga y descarga produce entonces un log pequeño y enfocado. Úsalo entre experimentos.

**Filtrar por etiqueta.** `dmesg | grep myfirst` acota la vista a las líneas que produjo tu driver, suponiendo que tus llamadas a `device_printf` emiten el nombre del driver. Lo hacen, porque `device_printf(9)` antepone a cada línea el nombre Newbus del dispositivo.

**Observar en tiempo real.** Ejecuta `tail -f /var/log/messages` en un segundo terminal. Cada mensaje del driver que llegue a `dmesg` aparece también ahí, con marcas de tiempo. Esto es especialmente útil durante pruebas de larga duración, como el ejercicio `parallel_probe` del Laboratorio 8.6.

### Vigilar con fstat

Para los problemas de detach, `fstat(1)` es tu aliado. Aparecen dos usos frecuentes:

```sh
% fstat /dev/myfirst/0
```

Búsqueda simple; muestra todos los procesos que tienen el nodo abierto. Las columnas de salida son usuario, comando, pid, fd, mount, inum, modo, rdev, r/w y nombre.

```sh
% fstat -p $$ | grep myfirst
```

Restringe la búsqueda al shell actual. Útil cuando no estás seguro de si tu shell actual tiene un descriptor residual abierto de una prueba anterior.

```sh
% fstat -u $USER | grep myfirst
```

Restringe a los procesos del propio usuario. Caso de uso similar, ámbito más amplio.

### El sysctl como aliado de cada driver

Desde el Capítulo 7, tu driver ya expone un árbol sysctl bajo `dev.myfirst.0.stats`. La fase 2 del Capítulo 8 añade `active_fhs` a ese árbol. Cuando realizas experimentos, los sysctls son la herramienta de observación más barata posible:

```sh
% sysctl dev.myfirst.0.stats
dev.myfirst.0.stats.attach_ticks: 123456
dev.myfirst.0.stats.open_count: 42
dev.myfirst.0.stats.active_fhs: 0
dev.myfirst.0.stats.bytes_read: 0
```

Cada contador es una comprobación de lo que el driver cree que es verdad. Las discrepancias entre lo que esperabas y lo que muestra sysctl son siempre una señal. Si `active_fhs` es distinto de cero cuando no debería haber ningún descriptor abierto, hay una fuga. Si `open_count` es menor que el número de veces que abriste el dispositivo, tu ruta de attach se está ejecutando dos veces o tu contador tiene una condición de carrera.

Los sysctls son más baratos que cualquier otro mecanismo de observación. Dales preferencia sobre leer el propio dispositivo siempre que un valor numérico o una cadena corta sea suficiente.

### Una lista de comprobación rápida para cada cambio de código

Antes de confirmar un cambio en el driver, repasa lo siguiente. Diez minutos aquí te ahorran horas de depuración después.

1. ¿Sigue compilando el driver desde un árbol limpio?
2. ¿Sigue cargándose y descargándose limpiamente en un sistema sin descriptores abiertos?
3. ¿La superficie visible al usuario (`ls -l /dev/myfirst/...`) coincide con lo que tu código pretende?
4. ¿Los permisos siguen siendo los valores predeterminados restrictivos que deben ser?
5. Si cambiaste `attach`, ¿sigue deshaciendo completamente cada ruta de error?
6. Si cambiaste `detach`, ¿sigue descargándose el driver limpiamente cuando hay descriptores abiertos (ya sea con un retorno limpio de `EBUSY` o, si elegiste una política diferente, sin provocar fugas)?
7. Si cambiaste el estado por apertura, ¿siguen comportándose como se espera `stress_probe`, `parallel_probe` y `hold_myfirst`?
8. ¿Introdujiste llamadas a `device_printf` que deberían estar protegidas con `if (bootverbose)` para no inundar el log?
9. ¿Dejaste algún bloque `#if 0` o prints de debug activos? Quítalos ahora.
10. Si cambiaste el propietario o el modo, ¿sigue `devfs.conf` produciendo la sobreescritura esperada?

Esta lista es deliberadamente aburrida. Ese es el propósito. Un proceso fiable y predecible supera siempre a una sesión de depuración heroica.

### Una lista de comprobación rápida antes de una entrega

Cuando preparas una fase de tu driver como "terminada", la lista se alarga un poco. Todo lo anterior, más:

1. `make clean && make` desde un árbol verdaderamente limpio compila sin advertencias.
2. `kldload ./myfirst.ko; sleep 0.1; kldunload myfirst` se completa diez veces sin problemas.
3. `stress_probe 10000` se completa sin problemas y `active_fhs` vuelve a cero.
4. `parallel_probe 8` abre ocho descriptores, los mantiene abiertos y los cierra limpiamente. El log del kernel muestra ocho punteros `fh=` distintos y ocho destructores.
5. `kldunload` con un descriptor abierto devuelve `EBUSY` limpiamente, sin provocar un pánico.
6. Una entrada en `devfs.conf` que amplía los permisos se aplica tras `service devfs restart`.
7. Una revisión con `ls -l` de `/dev/myfirst*` no muestra modos ni propietarios inesperados.
8. `dmesg` contiene exactamente los mensajes de attach y detach que esperas, sin advertencias ni errores.
9. El código fuente está libre de experimentos comentados, líneas TODO y helpers de debug.
10. Los sysctls que expone el driver son descriptivos y están documentados en comentarios del código.

Ningún commit debería saltarse los puntos 1 a 3. Son el seguro de alta rentabilidad más económico que puedes adquirir.

### Un flujo de trabajo para añadir un nuevo nodo

Recorrer el flujo de trabajo de extremo a extremo una vez consolidará las secciones anteriores. Supón que decides que `myfirst` debe exponer un nodo de estado adicional de solo lectura en `/dev/myfirst/status`, distinto de los nodos de datos numerados. Así es como lo harías.

**Paso 1: diseño.** Decide la forma del nodo. ¿Pertenece al mismo `cdevsw` que el nodo de datos, o a uno diferente? Un nodo de solo estado que responde a `read(2)` con un breve resumen de texto normalmente querrá su propio `cdevsw` con solo `d_read` configurado, porque la política es diferente a la del nodo de datos. Decide el modo de permisos. Solo lectura para cualquiera sugiere `0444`; solo lectura para operadores sugiere `0440` con el grupo apropiado.

**Paso 2: declarar.** Añade el nuevo `cdevsw`, su manejador `d_read` y un campo `struct cdev *cdev_status` al softc.

**Paso 3: implementar.** Escribe el manejador `d_read`. Formatea una cadena corta basada en el estado del softc y la devuelve a través de `uiomove(9)`. Para el Capítulo 8, puedes dejarlo con una implementación mínima y completarlo tras el Capítulo 9.

**Paso 4: conectar.** En `attach`, añade la llamada a `make_dev_s` para el nodo de estado. En `detach`, añade la llamada a `destroy_dev`, antes de la del nodo de datos.

**Paso 5: probar.** Reconstruye, recarga, inspecciona, ejercita, descarga. Comprueba que `ls -l` muestra el nodo de estado con el modo esperado. Comprueba que `cat /dev/myfirst/status` funciona y produce una salida coherente. Comprueba que el driver completo sigue descargándose limpiamente.

**Paso 6: documentar.** Añade un comentario en el código fuente del driver que describa el nodo. Añade una entrada en el sysctl `dev.myfirst.0.stats` si el estado es numérico y encajaría ahí también. Anota el cambio en el registro de cambios que mantengas.

Seis pasos, cada uno pequeño, cada uno específico. Ese es el nivel de granularidad al que los errores permanecen visibles.

### Un flujo de trabajo para diagnosticar un nodo ausente

El recorrido de resolución de problemas del Capítulo 8 en la sección de herramientas te proporcionó una lista corta. Aquí tienes un flujo de trabajo más completo que cabe en una ficha de notas.

**Fase 1: ¿es el módulo?**

- `kldstat | grep myfirst` muestra el módulo.
- `dmesg | grep myfirst` muestra los mensajes de attach.

Si el módulo no está cargado o el attach no se ejecutó, corrígelo primero.

**Fase 2: ¿es Newbus?**

- `devinfo -v | grep myfirst` muestra el dispositivo Newbus.

Si Newbus no muestra nada, tu `device_identify` o `device_probe` no está creando el hijo. Busca ahí.

**Fase 3: ¿es devfs?**

- `ls -l /dev/myfirst` lista el directorio (o informa de que falta).
- `dmesg | grep 'make_dev'` muestra cualquier fallo de `make_dev_s`.

Si Newbus está bien pero devfs no muestra nada, `make_dev_s` devolvió un error. Comprueba el formato de tu cadena de ruta, tu `mda_devsw` y la estructura de tus argumentos.

**Fase 4: ¿es la política?**

- `devfs rule showsets` lista los conjuntos de reglas activos.
- `devfs rule -s N show` lista las reglas del conjunto N.

Si devfs tiene el cdev pero tu jaula o tu sesión local no lo ve, el conjunto de reglas lo está ocultando.

Cada fallo se corresponde con una de estas cuatro fases. Recórrelas en orden y casi siempre identificarás la causa en menos de un minuto.

### Un flujo de trabajo para revisar el driver de otra persona

Cuando revisas un pull request que modifica la superficie de archivos de dispositivo de un driver, las preguntas útiles son:

- ¿Tiene cada `make_dev_s` un `destroy_dev` correspondiente?
- ¿Llama cada ruta de error tras `make_dev_s` a `destroy_dev` antes de retornar?
- ¿Destruye `detach` cada cdev que creó?
- ¿Se rellena `si_drv1` a través de `mda_si_drv1` en lugar de una asignación posterior?
- ¿Es el modo de permisos justificable para el propósito del nodo?
- ¿Está `d_version` en el cdevsw establecido a `D_VERSION`?
- ¿Están presentes todos los manejadores `d_*` para las operaciones que el nodo debe soportar, y es cada uno de ellos coherente en sus retornos de errno?
- Si el driver usa `devfs_set_cdevpriv`, ¿hay exactamente una asignación exitosa por apertura y exactamente un destructor?
- Si el driver usa alias, ¿se destruyen en `detach` antes que el primario?
- Si el driver tiene más de un cdev, ¿llama a `destroy_dev_drain` en su ruta de descarga?

Esta es una lista de revisión, no un tutorial. La revisión es más rápida porque cada pregunta tiene una respuesta de sí o no, y cada sí puede comprobarse de forma mecánica.

### Llevar un cuaderno de laboratorio

Un cuaderno de laboratorio es una libreta pequeña o un archivo de texto donde anotas lo que hiciste, lo que observaste y lo que aprendiste. El libro ha recomendado esto desde el Capítulo 2. En el Capítulo 8 da sus frutos de una manera concreta: realizarás los mismos tipos de experimentos muchas veces, y una nota breve te permite evitar repetir el mismo error dos veces.

Una plantilla útil para una entrada del cuaderno:

```text
Date: 2026-04-17
Driver: myfirst stage 2
Goal: verify per-open state is isolated across two processes
Steps:
 - loaded stage 2 kmod
 - ran parallel_probe with count=4
 - observed 4 distinct fh= pointers in dmesg
 - observed active_fhs=4 in sysctl
 - closed, observed 4 destructor lines, active_fhs=0
Result: as expected
Notes: first run missed destructor lines because dmesg ring buffer
       was full; dmesg -c before the test solved it
```

Dos minutos por experimento, no más. El valor aparece meses después, cuando estás rastreando un nuevo problema y una búsqueda en el cuaderno revela que el mismo síntoma apareció una vez antes bajo circunstancias diferentes.

### Preguntas de diseño habituales y cómo abordarlas

Algunas preguntas se repiten cuando los autores de drivers alcanzan la fase de archivo de dispositivo. Cada una de ellas ha surgido más de una vez en discusiones de revisión reales. Las respuestas son breves; el razonamiento merece interiorizarse.

**P: ¿Debo crear el cdev en `device_identify` o en `device_attach`?**

En `device_attach`. El callback identify se ejecuta muy pronto, antes de que la instancia del driver tenga un softc. El cdev necesita referenciar el softc a través de `mda_si_drv1`, lo que significa que el softc debe existir ya. El Capítulo 7 estableció este patrón; mantenlo.

**P: ¿Debo crear cdevs adicionales fuera de `attach` y `detach`?**

Si son genuinamente por instancia del driver, ponlos en `attach` y destrúyelos en `detach`. Si son dinámicos, creados en respuesta a una acción del usuario, créalos en el manejador que reciba la solicitud del usuario y destrúyelos bien cuando un manejador posterior deshaga la solicitud, bien cuando el driver se desconecte. Llévalos con cuidado; los cdevs perdidos son una fuente común de fugas.

**P: ¿Debo activar `D_TRACKCLOSE`?**

Por lo general, no. El mecanismo de estado por apertura mediante `devfs_set_cdevpriv` cubre casi todos los casos en los que `D_TRACKCLOSE` podría resultar tentador, y se limpia solo de forma automática. Establece `D_TRACKCLOSE` únicamente cuando necesites que tu `d_close` se ejecute en cada cierre de descriptor, no solo en el último. Los casos de uso reales son escasos; los drivers de TTY y unos pocos más encajan en ese perfil.

**P: ¿Debería permitir múltiples aperturas?**

Por defecto, sí, mediante estado por apertura. El acceso exclusivo es a veces necesario para hardware que solo puede soportar una sesión a la vez, pero es una decisión, no un valor predeterminado. El Capítulo 7 impuso la exclusividad como recurso didáctico; la etapa 2 del Capítulo 8 elimina esa restricción precisamente porque no es el caso habitual.

**P: ¿Debería devolver `ENXIO` o `EBUSY` cuando falla una apertura?**

`ENXIO` cuando el driver no está listo. `EBUSY` cuando el dispositivo puede abrirse en principio, pero no en ese momento. Los mensajes que ve el usuario son distintos, y un operador que lea el registro del kernel te lo agradecerá por haber elegido el correcto.

**P: ¿Debería usar `strdup` con las cadenas que recibo de userland?**

No durante la apertura. Si un handler tiene un motivo legítimo para conservar una cadena proporcionada por el usuario más allá de la llamada, usa `malloc(9)` con un tamaño explícito y copia la cadena en ese espacio. Nunca confíes en un puntero a memoria de userland después de que el handler retorne; puede que ya no sea válido, y aunque lo fuera, el kernel nunca debería depender de memoria propiedad de userland durante mucho tiempo.

**P: ¿Debería el softc recordar qué descriptores lo tienen abierto?**

Por lo general, no. El estado por apertura mediante `devfs_set_cdevpriv` es la respuesta. Si necesitas un mecanismo de iteración, `devfs_foreach_cdevpriv` existe y es la opción correcta. No mantengas tu propia lista de punteros a descriptores en el softc; el locking no es trivial y el kernel ya proporciona la respuesta adecuada.

**P: ¿Cuándo debería mi detach rechazar con `EBUSY`?**

Cuando el driver no puede desmontarse de forma segura con el estado actual. Los descriptores abiertos son el motivo más habitual. Algunos drivers también rechazan si el hardware está transfiriendo datos activamente o si hay una operación de control en curso. Falla pronto y con claridad; no intentes forzar un estado limpio desde dentro de `detach`.

**P: ¿Puedo descargar el driver mientras hay descriptores abiertos?**

No si `detach` lo rechaza. Si tu `detach` acepta la situación, el kernel drenará igualmente los handlers en curso, pero los descriptores abiertos permanecen en las tablas de archivos existentes hasta que los procesos los cierren, y esos descriptores devolverán `ENXIO` (o similar) en las operaciones posteriores. Para un driver de aprendizaje, rechazar con `EBUSY` es la opción más limpia.

Estas son las preguntas que surgirán en tu primera revisión real de un driver. Haberlas visto aquí una vez significa que no las estás viendo por primera vez cuando el revisor te las plantee.

### Un árbol de decisión para las elecciones de diseño más habituales

Cuando te sientas a diseñar un nuevo nodo o a modificar uno existente, las preguntas suelen caer en un conjunto reducido de ramas. El árbol que se muestra a continuación es una guía de campo, no un algoritmo; el diseño real siempre implica criterio, pero conocer la forma del árbol ayuda.

**Punto de partida: quiero exponer algo a través de `/dev`.**

**Rama 1: ¿Qué tipo de estado lleva el nodo?**

- **Fuente o sumidero de datos trivial sin sesión** (como `/dev/null`, `/dev/zero`): sin softc, sin estado por apertura, un `cdevsw` por comportamiento. Usa `make_dev` o `make_dev_s` en un manejador `MOD_LOAD`. Modo típicamente `0666`.
- **Hardware por dispositivo** (como un puerto serie, un sensor, un LED): un softc por instancia, un cdev por instancia. Usa el patrón `attach`/`detach`. Modo típicamente `0600` o `0660`.
- **Control de subsistema** (como `/dev/pf` o `/dev/mdctl`): un cdev que expone únicamente operaciones `d_ioctl`. Modo `0600`.
- **Estado por sesión** (como BPF, como FUSE): un cdev por sesión o un punto de entrada con clonación. Estado por apertura mediante `devfs_set_cdevpriv`. Modo `0600`.

**Rama 2: ¿Cómo descubren los usuarios el nodo?**

- **Nombre fijo estable** (como `/dev/null`): pon el nombre en la cadena de formato de `make_dev` y déjalo ahí.
- **Nombre numerado por instancia** (como `/dev/myfirst0`): usa `%d` en la cadena de formato y `device_get_unit(9)` para el número.
- **Agrupación en subdirectorio** (como `/dev/led/foo`): usa `/` dentro de la cadena de formato; devfs crea el directorio bajo demanda.
- **Instancia por apertura bajo demanda**: usa clonación. Se cubre más adelante.

**Rama 3: ¿Quién puede acceder a él?**

- **Cualquiera**: `UID_ROOT`, `GID_WHEEL`, modo `0666`. Poco habitual; úsalo solo para nodos inofensivos.
- **Solo root**: `UID_ROOT`, `GID_WHEEL`, modo `0600`. El valor predeterminado para cualquier cosa que requiera privilegios.
- **Root más un grupo de operadores**: `UID_ROOT`, `GID_OPERATOR`, modo `0660`. Habitual en herramientas privilegiadas de operación directa.
- **Root puede escribir, cualquiera puede leer**: `UID_ROOT`, `GID_WHEEL`, modo `0644`. Para nodos de estado.
- **Grupo con nombre personalizado**: define el grupo en `/etc/group`, usa `devfs.conf` para ajustar la propiedad en el momento de creación del nodo. No inventes un grupo dentro de tu driver.

**Rama 4: ¿Cuántos usuarios pueden abrir el nodo simultáneamente?**

- **Exactamente uno a la vez**: patrón de apertura exclusiva, un indicador en softc, comprobación bajo mutex en `d_open`, devolución de `EBUSY` en caso de conflicto. Sin `devfs_set_cdevpriv`.
- **Múltiples, cada uno con estado independiente**: elimina la comprobación exclusiva, asigna una estructura por apertura en `d_open`, llama a `devfs_set_cdevpriv` y recupérala con `devfs_get_cdevpriv`.
- **Múltiples, todos compartiendo el estado global del driver**: no asignes nada por apertura; simplemente lee y escribe el softc bajo su mutex.

**Rama 5: ¿Qué ocurre cuando se descarga el driver con usuarios activos?**

- **Rechazar la descarga** con `EBUSY` desde `detach` mientras haya algún descriptor abierto. Este es el comportamiento limpio por defecto.
- **Aceptar la descarga** pero invalidar los descriptores abiertos. En este caso necesitas un manejador `d_purge` que despierte los threads bloqueados y los convenza de retornar con rapidez. Es más complejo; hazlo solo cuando el rechazo dejaría el sistema en un estado peor.

**Rama 6: ¿Qué ajustes de nombre necesitan usuarios y operadores?**

- **Un segundo nombre mantenido por el propio driver** (ruta heredada, acceso directo bien conocido): `make_dev_alias(9)` en `attach`, `destroy_dev(9)` sobre él en `detach`.
- **Un segundo nombre mantenido por el operador**: `link` en `/etc/devfs.conf`. El driver no hace nada.
- **Ampliación o restricción de permisos por host**: `own` y `perm` en `/etc/devfs.conf`. El driver mantiene su línea de base.
- **Una vista filtrada por jail**: un conjunto de reglas en `/etc/devfs.rules`, referenciado en `jail.conf`. El driver no tiene nada que decir al respecto.

**Rama 7: ¿Cómo reciben los programas en espacio de usuario los eventos del driver?**

- **Polling mediante lecturas**: drivers que solo necesitan entregar bytes. `d_read` y `d_write`.
- **Lecturas bloqueantes con señales**: drivers que deben desbloquearse con SIGINT. Se cubre en el Capítulo 10.
- **Poll/select**: `d_poll`. Se cubre en el Capítulo 10.
- **Kqueue**: `d_kqfilter`. Se cubre en el Capítulo 10.
- **Notificaciones de devd**: `devctl_notify(9)` desde el driver; reglas del lado del operador en `/etc/devd.conf`.
- **Lecturas mediante sysctl**: para observabilidad sin coste de descriptor de archivo. Siempre complementario a la superficie `/dev`.

Este árbol no cubre todos los casos. Cubre los suficientes para que un autor de drivers pueda navegar las primeras decisiones de diseño sin entrar en pánico. Cuando surja una pregunta nueva que no figure en el árbol, anota la pregunta y la respuesta a la que llegaste; así es como el árbol crece para ti de forma personal.

### Una advertencia sobre el sobrediseño

Conviene señalar específicamente algunas tentaciones de diseño, porque tienden a convertir drivers sencillos en drivers complicados sin ningún beneficio.

- **Inventar tu propio protocolo IPC sobre `read`/`write`**. Si los mensajes tienen estructura, usa `ioctl(2)` (Capítulo 25).
- **Incrustar un pequeño lenguaje en los comandos `ioctl`** para que los usuarios puedan "escribir scripts" para el driver. Esto es casi siempre una señal de que la funcionalidad pertenece al espacio de usuario.
- **Multiplexar varios subsistemas no relacionados a través de un único `cdevsw`**. Si dos superficies tienen semánticas distintas, dales dos valores `cdevsw` separados; no cuesta nada y resulta más claro.
- **Añadir `D_NEEDGIANT` para silenciar una advertencia de SMP**. La advertencia es correcta; arregla el locking.
- **Gestionar todos los valores posibles de `errno` de todos los programas posibles en espacio de usuario**. Elige el correcto para tu situación y mantente en él. La familia estándar `err(3)` hace el resto.

La disciplina de "tan simple como sea posible, pero no más" es especialmente importante a este nivel. Cada línea de código del driver es una línea que podría tener un bug bajo carga. Un driver compacto es más fácil de revisar, más fácil de depurar, más fácil de portar y más fácil de entregar al siguiente mantenedor.



## Laboratorios prácticos

Estos laboratorios amplían el driver del Capítulo 7 en su lugar. No deberías necesitar reescribir nada desde cero. El directorio de acompañamiento refleja las etapas.

### Laboratorio 8.1: Nombre estructurado y permisos más restrictivos

**Objetivo.** Mover el dispositivo de `/dev/myfirst0` a `/dev/myfirst/0`, y cambiar el grupo a `operator` con modo `0660`.

**Pasos.**

1. En `myfirst_attach()`, cambia la cadena de formato de `make_dev_s()` a `"myfirst/%d"`.
2. Cambia `args.mda_gid` de `GID_WHEEL` a `GID_OPERATOR`, y `args.mda_mode` de `0600` a `0660`.
3. Vuelve a compilar y recarga:

   ```sh
   % make clean && make
   % sudo kldload ./myfirst.ko
   % ls -l /dev/myfirst
   total 0
   crw-rw----  1 root  operator  0x5a Apr 17 09:41 0
   ```

4. Confirma que un usuario normal del grupo `operator` puede leer desde el nodo sin `sudo`. En FreeBSD, puedes añadir un usuario a ese grupo con `pw groupmod operator -m yourname`, y después abrir un shell nuevo.
5. Descarga el driver y confirma que el directorio `/dev/myfirst/` desaparece junto con el nodo.

**Criterios de éxito.**

- `/dev/myfirst/0` aparece al cargar y desaparece al descargar.
- `ls -l /dev/myfirst/0` muestra `crw-rw----  root  operator`.
- Un miembro del grupo `operator` puede ejecutar `cat </dev/myfirst/0` sin errores.

### Laboratorio 8.2: Añadir un alias

**Objetivo.** Exponer `/dev/myfirst` como alias de `/dev/myfirst/0`.

**Pasos.**

1. Añade un campo `struct cdev *cdev_alias` al softc.
2. Tras la llamada exitosa a `make_dev_s()` en `myfirst_attach()`, llama a:

   ```c
   sc->cdev_alias = make_dev_alias(sc->cdev, "myfirst");
   if (sc->cdev_alias == NULL)
           device_printf(dev, "failed to create alias\n");
   ```

3. En `myfirst_detach()`, destruye el alias antes de destruir el cdev principal:

   ```c
   if (sc->cdev_alias != NULL) {
           destroy_dev(sc->cdev_alias);
           sc->cdev_alias = NULL;
   }
   if (sc->cdev != NULL) {
           destroy_dev(sc->cdev);
           sc->cdev = NULL;
   }
   ```

4. Vuelve a compilar, recarga y verifica:

   ```sh
   % ls -l /dev/myfirst /dev/myfirst/0
   ```

   Ambas rutas deberían responder. `sudo cat </dev/myfirst` y `sudo cat </dev/myfirst/0` deberían comportarse de forma idéntica.

**Criterios de éxito.**

- Ambas rutas existen mientras el driver está cargado.
- Ambas rutas desaparecen al descargar.
- El driver no entra en pánico ni pierde memoria si falla la creación del alias; comenta temporalmente la línea `make_dev_alias` para confirmarlo.

### Laboratorio 8.3: Estado por apertura

**Objetivo.** Dar a cada `open(2)` su propia pequeña estructura, y verificar desde el espacio de usuario que dos descriptores ven datos independientes.

**Pasos.**

1. Añade el tipo `struct myfirst_fh` y el destructor `myfirst_fh_dtor()` tal como se muestran anteriormente en este capítulo.
2. Reescribe `myfirst_open()` para que asigne un `myfirst_fh`, llame a `devfs_set_cdevpriv()` y libere la memoria en caso de fallo en el registro. Elimina la comprobación de apertura exclusiva.
3. Reescribe `myfirst_read()` y `myfirst_write()` para que cada una comience con una llamada a `devfs_get_cdevpriv(&fh)`. Deja el cuerpo sin cambios de momento; el Capítulo 9 lo completará.
4. Vuelve a compilar, recarga y ejecuta dos procesos `probe_myfirst` en paralelo:

   ```sh
   % (sudo ./probe_myfirst &) ; sudo ./probe_myfirst
   ```

5. En `dmesg`, confirma que los dos mensajes `open (per-open fh=...)` muestran punteros distintos.

**Criterios de éxito.**

- Dos aperturas simultáneas tienen éxito. Sin `EBUSY`.
- Dos punteros `fh=` distintos aparecen en el log del kernel.
- `kldunload myfirst` solo es posible una vez que ambas sondas han terminado.

### Laboratorio 8.4: Persistencia con devfs.conf

**Objetivo.** Hacer que el cambio de propiedad del Laboratorio 8.1 sobreviva a los reinicios, sin modificar el driver de nuevo.

**Pasos.**

1. En el Laboratorio 8.1, revierte `args.mda_gid` y `args.mda_mode` a los valores predeterminados del Capítulo 7 (`GID_WHEEL`, `0600`).
2. Crea o edita `/etc/devfs.conf` y añade:

   ```
   own     myfirst/0       root:operator
   perm    myfirst/0       0660
   ```

3. Aplica el cambio sin reiniciar:

   ```sh
   % sudo service devfs restart
   ```

4. Recarga el driver y confirma que `ls -l /dev/myfirst/0` vuelve a mostrar `root  operator  0660`, aunque el driver en sí haya solicitado `root  wheel  0600`.

**Criterios de éxito.**

- Con el driver cargado y `devfs.conf` en su lugar, el nodo muestra los valores de `devfs.conf`.
- Con el driver cargado y las líneas de `devfs.conf` comentadas y devfs reiniciado, el nodo vuelve a la línea de base del driver.

**Notas.** El Laboratorio 8.4 es un laboratorio del lado del operador. El driver no cambia entre pasos. El objetivo es ver el modelo de política de dos capas en acción: el driver establece la línea de base, y `devfs.conf` da forma a la vista.

### Laboratorio 8.5: Driver con dos nodos (datos y control)

**Objetivo.** Ampliar `myfirst` para exponer dos nodos distintos: un nodo de datos en `/dev/myfirst/0` y un nodo de control en `/dev/myfirst/0.ctl`, cada uno con su propio `cdevsw` y su propio modo de permisos.

**Requisitos previos.** Haber completado el Laboratorio 8.3 (etapa 2 con estado por apertura).

**Pasos.**

1. Define un segundo `struct cdevsw` en el driver, llamado `myfirst_ctl_cdevsw`, con `d_name = "myfirst_ctl"` y solo `d_ioctl` como esbozo (no implementarás comandos ioctl; basta con que la función exista y devuelva `ENOTTY`).
2. Añade un campo `struct cdev *cdev_ctl` al softc.
3. En `myfirst_attach`, tras crear el nodo de datos, crea el nodo de control con una segunda llamada a `make_dev_s`. Usa `"myfirst/%d.ctl"` como formato. Establece el modo a `0640` y el grupo a `GID_WHEEL` para que el nodo de control sea más restrictivo que el de datos.
4. Pasa `sc` a través de `mda_si_drv1` también para el cdev de control, de modo que `d_ioctl` pueda encontrarlo.
5. En `myfirst_detach`, destruye el cdev de control **antes** que el cdev de datos. Registra cada destrucción.
6. Vuelve a compilar, recarga y verifica:

   ```sh
   % ls -l /dev/myfirst
   total 0
   crw-rw----  1 root  operator  0x5a Apr 17 10:02 0
   crw-r-----  1 root  wheel     0x5b Apr 17 10:02 0.ctl
   ```

**Criterios de éxito.**

- Ambos nodos aparecen al cargar.
- Ambos nodos desaparecen al descargar.
- El nodo de datos puede ser leído por el grupo `operator`; el nodo de control no.
- Intentar `cat </dev/myfirst/0.ctl` desde un usuario que no sea root ni wheel falla con `Permission denied`.

**Notas.** En los drivers reales, el nodo de control es donde viven los comandos `ioctl` de configuración. Este capítulo no implementa ningún comando `ioctl`; ese trabajo corresponde al Capítulo 25. El objetivo del Laboratorio 8.5 es mostrar que puedes tener dos nodos con políticas distintas conectados a un mismo driver.

### Laboratorio 8.6: Verificación con sonda en paralelo

**Objetivo.** Usar la herramienta `parallel_probe` del árbol de acompañamiento para demostrar que el estado por apertura es realmente independiente por descriptor.

**Requisitos previos.** Haber completado el Laboratorio 8.3. El driver de la etapa 2 está cargado.

**Pasos.**

1. Compila las herramientas de espacio de usuario:

   ```sh
   % cd examples/part-02/ch08-working-with-device-files/userland
   % make
   ```

2. Ejecuta `parallel_probe` con cuatro descriptores:

   ```sh
   % sudo ./parallel_probe /dev/myfirst/0 4
   opened /dev/myfirst/0 as fd 3
   opened /dev/myfirst/0 as fd 4
   opened /dev/myfirst/0 as fd 5
   opened /dev/myfirst/0 as fd 6
   holding 4 descriptors; press enter to close
   ```

3. Abre un segundo terminal e inspecciona `dmesg`:

   ```sh
   % dmesg | tail -20
   ```

   Deberías ver cuatro líneas `open via myfirst/0 fh=<pointer> (active=N)`, cada una con un valor de puntero distinto.

4. En el segundo terminal, comprueba el sysctl de aperturas activas:

   ```sh
   % sysctl dev.myfirst.0.stats.active_fhs
   dev.myfirst.0.stats.active_fhs: 4
   ```

5. Vuelve al primer terminal y pulsa Enter. La sonda cierra los cuatro descriptores. Comprueba `dmesg` de nuevo:

   ```sh
   % dmesg | tail -10
   ```

   Deberías ver cuatro líneas `per-open dtor fh=<pointer>`, una por descriptor, con los mismos valores de puntero que aparecieron en el log de apertura.

6. Verifica que `active_fhs` ha vuelto a cero:

   ```sh
   % sysctl dev.myfirst.0.stats.active_fhs
   dev.myfirst.0.stats.active_fhs: 0
   ```

**Criterios de éxito.**

- Cuatro punteros `fh=` distintos en el log de apertura.
- Cuatro punteros coincidentes en el log del destructor.
- `active_fhs` incrementa hasta cuatro y decrementa de vuelta a cero.
- Ningún mensaje del log del kernel sobre fuga de memoria o estado inesperado.

**Notas.** El laboratorio 8.6 es la evidencia más sólida que puedes obtener fácilmente de que el estado por apertura está aislado. Si algún paso falla, el culpable más habitual es una llamada omitida a `devfs_set_cdevpriv` o un destructor que no decrementa `active_fhs`.

### Laboratorio 8.7: devfs.rules para una jail

**Objetivo.** Hacer que `/dev/myfirst/0` sea visible dentro de una jail mediante un ruleset de devfs.

**Requisitos previos.** Una jail de FreeBSD funcional en tu sistema de laboratorio. Si aún no tienes una, omite este laboratorio y vuelve después de la Parte 7.

**Pasos.**

1. Añade un ruleset en `/etc/devfs.rules`:

   ```
   [myfirst_jail=100]
   add include $devfsrules_jail
   add path 'myfirst'   unhide
   add path 'myfirst/*' unhide
   add path 'myfirst/*' mode 0660 group operator
   ```

2. Añade una entrada devfs en el archivo `jail.conf` de la jail:

   ```
   myfirstjail {
           path = "/jails/myfirstjail";
           host.hostname = "myfirstjail.example.com";
           mount.devfs;
           devfs_ruleset = 100;
           exec.start = "/bin/sh";
           persist;
   }
   ```

3. Recarga devfs e inicia la jail:

   ```sh
   % sudo service devfs restart
   % sudo service jail start myfirstjail
   ```

4. Dentro de la jail, confirma el nodo:

   ```sh
   % sudo jexec myfirstjail ls -l /dev/myfirst
   ```

5. Verifica que el ruleset funciona comentando la línea `add path 'myfirst/*' unhide`, reiniciando devfs y la jail, y observando cómo desaparece el nodo.

**Criterios de éxito.**

- `/dev/myfirst/0` aparece dentro de la jail con modo `0660` y grupo `operator`.
- Eliminar la regla unhide elimina el nodo del interior de la jail.
- El host sigue viendo el nodo independientemente del ruleset de la jail.

**Notas.** La configuración de jails se trata habitualmente en capítulos posteriores; este laboratorio es una vista previa para demostrar el resultado desde el lado del driver. Si el laboratorio resulta complicado en tu sistema, vuelve a él después de haber configurado jails para otros propósitos.

### Laboratorio 8.8: Destroy-Dev Drain

**Objetivo.** Demostrar la diferencia entre `destroy_dev` y `destroy_dev_drain` cuando se libera un `cdevsw` junto con varios cdevs.

**Requisitos previos.** Haber completado el Laboratorio 8.3. El driver está cargado y sin actividad.

**Pasos.**

1. Revisa el código de detach de la Etapa 2. El driver con un único cdev no necesita `destroy_dev_drain`. El laboratorio modela qué sale mal en un driver con varios cdevs que sí lo requiere.
2. Compila la variante `stage4-destroy-drain` del driver (disponible en el árbol de ejemplos). Esta variante crea cinco cdevs en attach y usa `destroy_dev_sched` para programar su destrucción en detach, sin drenar.
3. Carga la variante y, a continuación, descárgala inmediatamente mientras un proceso en espacio de usuario mantiene uno de los cdevs abierto:

   ```sh
   % sudo kldload ./stage4.ko
   % sudo ./hold_myfirst 60 /dev/myfirstN/3 &
   % sudo kldunload stage4
   ```

4. Observa el log del kernel. Deberías ver mensajes de error o, dependiendo del momento, un panic. La variante es deliberadamente insegura.
5. Pasa a la versión «corregida» del código fuente de la etapa 4, que llama a `destroy_dev_drain(&mycdevsw)` después de las llamadas de destroy-sched por cdev. Repite la secuencia cargar/mantener/descargar.
6. Confirma que la versión corregida se descarga limpiamente, esperando a que el descriptor retenido se cierre antes de que el módulo desaparezca.

**Criterios de éxito.**

- La variante defectuosa produce un problema observable (mensaje, cuelgue o panic) cuando se descarga con un descriptor retenido.
- La variante corregida completa la descarga limpiamente.
- Leer el código fuente deja claro qué llamada marcó la diferencia.

**Notas.** Este laboratorio provoca deliberadamente un estado incorrecto. Ejecútalo en una VM desechable, no en un sistema que te importe. El objetivo es desarrollar la intuición sobre por qué existe `destroy_dev_drain`; una vez que hayas visto cómo falla la ruta defectuosa, recordarás llamarlo en drivers con varios cdevs.



## Ejercicios de desafío

Estos ejercicios se basan en los laboratorios. Tómate tu tiempo; ninguno introduce mecánicas nuevas, solo amplían las que acabas de practicar.

### Desafío 1: Usa el alias

Modifica `probe_myfirst.c` para que abra `/dev/myfirst` en lugar de `/dev/myfirst/0` por defecto. Confirma en el log del kernel que tu `d_open` se ejecuta y que `devfs_set_cdevpriv` tiene éxito exactamente una vez por `open(2)`. Después, vuelve a cambiar la ruta. No deberías tener que editar el driver.

### Desafío 2: Observa la limpieza por apertura

Añade un `device_printf` dentro de `myfirst_fh_dtor()` que registre el puntero `fh` que se está liberando. Ejecuta `probe_myfirst` una vez y confirma que aparece exactamente una línea del destructor en `dmesg` por ejecución. A continuación, escribe un pequeño programa que abra el dispositivo, duerma durante 30 segundos y salga sin llamar a `close(2)`. Confirma que el destructor sigue disparándose cuando el proceso termina. La limpieza no es una cortesía; está garantizada.

### Desafío 3: Experimenta con devfs.rules

Si tienes una jail de FreeBSD configurada, añade un ruleset `myfirst_lab` en `/etc/devfs.rules` que haga visible `/dev/myfirst/*` dentro de la jail. Inicia la jail, abre el dispositivo desde dentro de ella y confirma que el driver detecta una nueva apertura. Si aún no tienes una jail, omite este desafío por ahora y vuelve a él después de la Parte 7.

### Desafío 4: Lee dos drivers más

Elige dos drivers de `/usr/src/sys/dev/` que aún no hayas leído. Buenos candidatos son `/usr/src/sys/dev/random/randomdev.c`, `/usr/src/sys/dev/hwpmc/hwpmc_mod.c`, `/usr/src/sys/dev/kbd/kbd.c`, o cualquier otro lo bastante corto para hojear. Para cada driver, busca:

- La definición de `cdevsw` y su `d_name`.
- La llamada `make_dev*` y el modo de permisos que establece.
- Las llamadas a `destroy_dev`, o su ausencia.
- Si el driver utiliza `devfs_set_cdevpriv`.
- Si el driver crea un subdirectorio dentro de `/dev`.

Escribe un párrafo breve para cada driver clasificando su superficie de archivo de dispositivo. El objetivo es afinar la vista; no existe una única taxonomía correcta.

### Desafío 5: Configuración de devd

Escribe una regla mínima en `/etc/devd.conf` que registre un mensaje cada vez que `/dev/myfirst/0` aparezca o desaparezca. El formato de configuración de devd está documentado en `devd.conf(5)`. Una plantilla de partida:

```text
notify 100 {
        match "system"      "DEVFS";
        match "subsystem"   "CDEV";
        match "cdev"        "myfirst/0";
        action              "/usr/bin/logger -t myfirst event=$type";
};
```

Instala la regla, reinicia devd (`service devd restart`), carga y descarga el driver, y verifica que `grep myfirst /var/log/messages` muestra ambos eventos.

### Desafío 6: Añade un nodo de estado

Modifica `myfirst` para exponer un nodo de estado de solo lectura junto al nodo de datos. El nodo de estado reside en `/dev/myfirst/0.status`, con modo `0444` y propietario `root:wheel`. Su `d_read` devuelve una cadena de texto breve que resume el estado actual del driver:

```ini
attached_at=12345
active_fhs=2
open_count=17
```

Pista: asigna un pequeño buffer de tamaño fijo en el softc, formatea la cadena bajo el mutex y devuélvela al usuario con `uiomove(9)` si ya has leído el Capítulo 9, o bien con una implementación manual por ahora.

Si aún no te sientes cómodo con `uiomove`, pospón este desafío hasta después del Capítulo 9. Es un primer uso natural de lo que enseña ese capítulo.



## Códigos de error en operaciones con archivos de dispositivo

Cada `d_open` y `d_close` que devuelve un valor distinto de cero le dice algo concreto a devfs. Los valores errno que elijas son el contrato entre tu driver y cualquier programa de usuario que alguna vez acceda a tu nodo. Acertarlos no cuesta nada; equivocarse genera informes de bugs que no entenderás a primera vista.

Esta sección repasa los valores errno que aparecen en la práctica en la superficie de archivos de dispositivo. El Capítulo 9 tratará por separado las elecciones de errno para `d_read` y `d_write`, porque las decisiones en la ruta de datos tienen un carácter diferente. Aquí nos centramos en los retornos de open, close y los relacionados con ioctl.

### La lista breve

En orden aproximado de frecuencia de uso:

- **`ENXIO` (No such device or address)**: "El dispositivo no está en un estado en el que pueda abrirse." Úsalo cuando el driver está adjunto pero no está listo, cuando se sabe que el hardware no está presente, o cuando el softc se encuentra en un estado transitorio. El usuario ve `Device not configured`.
- **`EBUSY` (Device busy)**: "El dispositivo ya está abierto y este driver no permite el acceso concurrente." Úsalo para políticas de apertura exclusiva. El usuario ve `Device busy`.
- **`EACCES` (Permission denied)**: "La credencial que presenta esta apertura no está autorizada." El kernel normalmente detecta los fallos de permisos antes de que se ejecute tu manejador, pero un driver puede comprobar una política secundaria (por ejemplo, un nodo solo accesible mediante ioctl que rechaza aperturas de lectura) y devolver `EACCES` por su cuenta.
- **`EPERM` (Operation not permitted)**: "La operación requiere privilegios que el emisor de la llamada no posee." Similar a `EACCES` en espíritu, pero orientado a distinciones de privilegio (fallos de `priv_check(9)`) más que a permisos UNIX del sistema de archivos.
- **`EINVAL` (Invalid argument)**: "La llamada era estructuralmente válida, pero el driver no acepta estos argumentos." Úsalo cuando `oflags` especifica una combinación que el driver rechaza.
- **`EAGAIN` (Resource temporarily unavailable)**: "El dispositivo podría abrirse en principio, pero no en este momento." Úsalo cuando hay una escasez temporal (una ranura está llena, un recurso se está reconfigurando) y el usuario debería reintentar más tarde. El usuario ve `Resource temporarily unavailable`.
- **`EINTR` (Interrupted system call)**: Se devuelve cuando un sleep dentro del manejador es interrumpido por una señal. Normalmente no lo devolverás desde `d_open` porque las aperturas no suelen dormir de forma interrumpible. Aparece más en los manejadores de la ruta de datos.
- **`ENOENT` (No such file or directory)**: Casi siempre lo sintetiza el propio devfs cuando la ruta no se resuelve. Un driver raramente lo devuelve desde sus propios manejadores.
- **`ENODEV` (Operation not supported by device)**: "La operación en sí es válida, pero este dispositivo no la soporta." Úsalo cuando una interfaz secundaria del driver rechaza una operación que la otra interfaz sí soporta.
- **`EOPNOTSUPP` (Operation not supported)**: Un primo de `ENODEV`. Se usa en algunos subsistemas para situaciones similares.

### ¿Qué valor para qué situación?

Los drivers reales siguen patrones. Estos son los que escribirás con más frecuencia.

**Patrón A: Driver adjunto pero softc aún no listo.** Puedes encontrarte con este caso durante un attach en dos etapas en el que el cdev se crea antes de que finalice alguna inicialización, o durante el detach mientras el cdev todavía existe.

```c
if (sc == NULL || !sc->is_attached)
        return (ENXIO);
```

**Patrón B: Política de apertura exclusiva.**

```c
mtx_lock(&sc->mtx);
if (sc->is_open) {
        mtx_unlock(&sc->mtx);
        return (EBUSY);
}
sc->is_open = 1;
mtx_unlock(&sc->mtx);
```

Esto es lo que hacía el Capítulo 7. La etapa 2 del Capítulo 8 elimina la comprobación exclusiva porque hay estado por apertura disponible; el `EBUSY` simplemente deja de ser necesario.

**Patrón C: Nodo de solo lectura que rechaza escrituras.**

```c
if ((oflags & FWRITE) != 0)
        return (EACCES);
```

Úsalo cuando el nodo es conceptualmente de solo lectura y abrirlo para escritura es un error del emisor de la llamada.

**Patrón D: Interfaz solo para usuarios con privilegios.**

```c
if (priv_check(td, PRIV_DRIVER) != 0)
        return (EPERM);
```

Devuelve `EPERM` cuando un emisor sin privilegios intenta abrir un nodo que aplica comprobaciones de privilegio adicionales más allá del modo del sistema de archivos.

**Patrón E: Temporalmente no disponible.**

```c
if (sc->resource_in_flight) {
        return (EAGAIN);
}
```

Úsalo cuando el driver puede aceptar la apertura más tarde pero no ahora, y el usuario debería reintentar.

**Patrón F: Combinación no válida específica del driver.**

```c
if ((oflags & O_NONBLOCK) != 0 && !sc->supports_nonblock) {
        return (EINVAL);
}
```

Úsalo cuando los `oflags` del emisor especifican un modo que tu driver no implementa.

### Devolver errores desde d_close

`d_close` tiene sus propias consideraciones. El kernel normalmente no presta atención a los errores de close, porque para cuando `close(2)` vuelve al userland el descriptor ya ha desaparecido. Pero close sigue siendo tu última oportunidad para detectar un fallo y registrarlo, y algunos emisores pueden comprobarlo. El patrón más seguro es:

- Devuelve cero en las rutas de close ordinarias.
- Devuelve un errno distinto de cero solo cuando ocurra algo genuinamente inusual y el userland deba saberlo.
- En caso de duda, registra con `device_printf(9)` y devuelve cero.

Un driver que devuelve errores aleatorios desde `d_close` es un driver cuyos tests fallarán de forma misteriosa, porque la mayor parte del código en userland ignora los errores de close. Reserva errno para open y para ioctl, donde sí importa.

### Cómo mapear tu errno a mensajes de usuario

Los valores definidos en `/usr/include/errno.h` tienen representaciones textuales estables a través de `strerror(3)` y `perror(3)`. Cada mensaje de `err(3)` y `warn(3)` en un programa de userland utilizará estas representaciones. Una tabla breve de las correspondencias:

| errno             | Texto de `strerror`               | Comportamiento típico del programa de usuario |
|-------------------|-----------------------------------|-----------------------------------------------|
| `ENXIO`           | Device not configured             | Esperar o abandonar; informar claramente       |
| `EBUSY`           | Device busy                       | Reintentar más tarde o abortar                 |
| `EACCES`          | Permission denied                 | Pedir `sudo` o salir                           |
| `EPERM`           | Operation not permitted           | Similar a `EACCES`                             |
| `EINVAL`          | Invalid argument                  | Informar de un error en el código que llama    |
| `EAGAIN`          | Resource temporarily unavailable  | Reintentar tras una breve pausa                |
| `EINTR`           | Interrupted system call           | Reintentar, normalmente en un bucle            |
| `ENOENT`          | No such file or directory         | Verificar que el driver está cargado           |
| `ENODEV`          | Operation not supported by device | Informar de una incompatibilidad de diseño     |
| `EOPNOTSUPP`      | Operation not supported           | Informar de una incompatibilidad de diseño     |

El Apéndice E de este libro recoge la lista completa de valores errno del kernel y sus significados. Para el Capítulo 8, la lista anterior cubre todo lo que necesitarás en la superficie del archivo de dispositivo.

### Lista de comprobación antes de elegir un errno

Cuando no estés seguro de qué errno encaja, hazte tres preguntas:

1. **¿El problema es de identidad?** "Este dispositivo no puede abrirse ahora" es `ENXIO`. "Este dispositivo no existe" es `ENOENT`. Rara vez es decisión del driver; devfs suele encargarse de ello.
2. **¿El problema es de permisos?** "No tienes permiso" es `EACCES`. "Te falta un privilegio específico" es `EPERM`.
3. **¿El problema es de argumentos?** "La llamada era estructuralmente correcta, pero el driver no aceptará estos argumentos" es `EINVAL`.

Cuando dos valores de errno puedan encajar razonablemente, elige aquel cuya representación textual coincida con lo que querrías que leyera un usuario frustrado. Recuerda que los valores de errno se convierten en mensajes de error en herramientas sobre las que no tienes control, y cuanto más claro sea el mapeo entre la intención del kernel y el texto que ve el usuario, mejor acogerán tu driver en las revisiones de código.

### Un relato breve: errno elegido tres veces

Para hacer lo abstracto concreto, aquí tienes tres pequeñas escenas extraídas de conversaciones reales sobre revisión de drivers. Cada una gira en torno a la elección de un único valor de errno.

**Escena 1. El open demasiado temprano.**

Un driver conecta un sensor integrado en la placa. El sensor tarda cien milisegundos después del encendido en producir datos válidos. Durante esos cien milisegundos, los programas de usuario que intenten leer recibirán datos incorrectos.

El primer borrador del driver devuelve `EAGAIN` desde `d_open` durante la ventana de calentamiento. El revisor lo señala. `EAGAIN` significa "reintenta más tarde", lo cual es válido, pero el texto que ve el usuario es "Resource temporarily unavailable", y eso no refleja lo que el usuario está viendo: el dispositivo existe y en principio puede abrirse, pero todavía no produce datos.

El borrador revisado devuelve `ENXIO` durante el calentamiento. El usuario ve "Device not configured", que se acerca más a la realidad. Un programa de userland bien escrito puede tratar ese errno como caso especial si desea esperar al dispositivo. Una herramienta habitual mostrará un mensaje claro y terminará.

Lección: piensa en lo que ve el usuario, no solo en lo que quieres expresar internamente.

**Escena 2. El error de permisos incorrecto.**

Un driver tiene un modo configurable: un sysctl puede ponerlo en "solo lectura". Cuando el sysctl está activado, `d_write` devuelve un error. El primer borrador devuelve `EPERM`. El revisor lo señala. `EPERM` tiene que ver con privilegios; el kernel lo utiliza cuando falla una llamada concreta a `priv_check(9)`. Pero en este driver no se está realizando ninguna comprobación de privilegios; el dispositivo simplemente está en un estado de solo lectura.

El borrador revisado devuelve `EROFS`, "Read-only file system". El mapeo textual es casi perfecto para este escenario.

Lección: el valor de errno más próximo al significado real suele ser el mejor. No uses `EPERM` por defecto ante cualquier rechazo.

**Escena 3. El archivo ocupado.**

Un driver que impone acceso exclusivo devuelve `EBUSY` desde `d_open` cuando llega un segundo proceso que intenta abrirlo. Eso es correcto. En la revisión de código, un revisor señala que el driver también devuelve `EBUSY` desde un ioctl en el nodo de control cuando lo rechaza durante una reconfiguración en curso. El argumento de la revisión es que se trata de situaciones distintas y el doble uso de `EBUSY` confundirá a los operadores que lean los logs.

La discusión llega a un compromiso: `EBUSY` para la comprobación de exclusividad en el camino de apertura, `EAGAIN` para el caso de reconfiguración en progreso. La distinción es que el rechazo en el camino de apertura significa "seguirá ocupado hasta que el otro usuario cierre", mientras que el rechazo por reconfiguración significa "reintenta en un momento, se resolverá solo".

Lección: dos situaciones que parecen similares pueden mapearse a distintos valores de errno si el razonamiento sobre la siguiente acción del usuario difiere.

Estas escenas son pequeñas, pero el principio que encierran no lo es. Cada valor de errno es una pista para el usuario sobre qué hacer a continuación. Elígelo con la perspectiva del usuario en mente, no solo con la tuya.

### Uso de `err(3)` y `warn(3)` para probar los valores de errno

La familia `err(3)` de la libc de FreeBSD imprime un limpio "programa: mensaje: cadena-errno" cuando falla una operación. Tus sondas en espacio de usuario utilizan `err(3)` porque es la forma más directa de obtener un error legible. Puedes verificar las elecciones de errno de tu driver ejecutando una sonda que desencadene deliberadamente cada una:

```c
fd = open("/dev/myfirst/0", O_RDWR);
if (fd < 0)
        err(1, "open /dev/myfirst/0");
```

Cuando el driver devuelve `EBUSY`, el programa imprime:

```text
probe_myfirst: open /dev/myfirst/0: Device busy
```

Cuando el driver devuelve `ENXIO`, el programa imprime:

```text
probe_myfirst: open /dev/myfirst/0: Device not configured
```

Ejecuta la sonda contra cada uno de los casos de error que puedas construir. Lee los mensajes en voz alta. Si alguno de ellos confundiría a un usuario que no hubiera leído el código fuente de tu driver, reconsidera el errno.

### Valores de errno que tu driver casi nunca debería devolver

Para equilibrar, una lista de valores que rara vez encajan en el open o el close de un archivo de dispositivo:

- **`ENOMEM`**: deja que la llamada a `malloc` lo notifique devolviéndolo a través de tu función, pero no lo inventes.
- **`EIO`**: reservado para errores de I/O de hardware. Si tu dispositivo no tiene hardware, este valor está fuera de lugar.
- **`EFAULT`**: se usa cuando userland pasa al kernel un puntero inválido. En el camino de apertura rara vez se tocan punteros de usuario, por lo que `EFAULT` no encaja.
- **`ESRCH`**: "No such process". Es poco probable que sea correcto para una operación sobre un archivo de dispositivo.
- **`ECHILD`**: errno de relaciones entre procesos. No aplica.
- **`EDOM`** y **`ERANGE`**: errores matemáticos. No aplican.

Ante la duda, si el valor no aparece en la "Lista corta" del Capítulo 8 presentada anteriormente en esta sección, casi con total seguridad es incorrecto para un open o un close. Reserva los valores inusuales para las operaciones inusuales que genuinamente los producen.



## Herramientas para inspeccionar /dev

Vale la pena conocer algunas utilidades pequeñas, porque en cuanto llegues al Capítulo 9 te apoyarás en ellas para confirmar el comportamiento rápidamente. Esta sección presenta cada una con la profundidad suficiente para usarla, y termina con dos breves guías de resolución de problemas.

### ls -l para permisos y existencia

El primer paso. `ls -l /dev/yourpath` confirma la existencia, el tipo, la propiedad y el modo. Si el nodo falta después de una carga, tu `make_dev_s` probablemente falló; comprueba `dmesg` para ver el código de error.

`ls -l` en un directorio de devfs funciona como cabría esperar: `ls -l /dev/myfirst` lista las entradas del subdirectorio. Combinado con `-d`, informa sobre el directorio en sí:

```sh
% ls -ld /dev/myfirst
dr-xr-xr-x  2 root  wheel  512 Apr 17 10:02 /dev/myfirst
```

El modo de un subdirectorio de devfs es `0555` por defecto, y no es directamente configurable mediante `devfs.conf`. El subdirectorio existe únicamente porque al menos un nodo está en su interior; cuando desaparece el último nodo, el directorio también desaparece.

### stat y stat(1)

`stat(1)` imprime una vista estructurada de cualquier nodo. La salida por defecto es detallada e incluye marcas de tiempo. Una forma más útil es un formato personalizado:

```sh
% stat -f '%Sp %Su %Sg %T %N' /dev/myfirst/0
crw-rw---- root operator Character Device /dev/myfirst/0
```

Los marcadores de posición están documentados en `stat(1)`. Los cinco anteriores son: permisos, nombre de usuario, nombre de grupo, descripción del tipo de archivo y ruta. Esta forma es útil dentro de scripts que necesitan una representación textual estable.

Para comparar dos rutas y verificar que apuntan al mismo cdev, `stat -f '%d %i %Hr,%Lr'` imprime el dispositivo del sistema de archivos, el inodo y los componentes mayor y menor de `rdev`. En dos nodos de devfs que se refieren al mismo cdev, el componente `rdev` coincidirá.

### fstat(1): ¿quién lo tiene abierto?

`fstat(1)` lista todos los archivos abiertos en el sistema. Filtrado a una ruta de dispositivo, te indica qué procesos tienen el nodo abierto:

```sh
% fstat /dev/myfirst/0
USER     CMD          PID   FD MOUNT      INUM MODE         SZ|DV R/W NAME
root     probe_myfir  1234    3 /dev          4 crw-rw----   0,90 rw  /dev/myfirst/0
```

Esta es la herramienta que resuelve el enigma de "`kldunload` devuelve `EBUSY` y no sé por qué". Ejecútala contra tu nodo, identifica el proceso infractor y espera a que termine o termínalo.

`fstat -u username` filtra por usuario, útil cuando sospechas que los daemons de un usuario en particular mantienen el nodo abierto. `fstat -p pid` inspecciona un proceso concreto.

### procstat -f: una visión centrada en el proceso

`fstat(1)` lista archivos y te dice quién los tiene. `procstat -f pid` hace lo contrario: lista los archivos que tiene abiertos un proceso concreto. Cuando tienes el PID de un programa en ejecución y quieres confirmar qué nodos de dispositivo tiene abiertos en ese momento, esta es la herramienta:

```sh
% procstat -f 1234
  PID COMM                FD T V FLAGS    REF  OFFSET PRO NAME
 1234 probe_myfirst        3 v c rw------   1       0     /dev/myfirst/0
```

La columna `T` muestra el tipo de archivo (`v` para vnode, que incluye los archivos de dispositivo), y la columna `V` muestra el tipo de vnode (`c` para vnode de dispositivo de caracteres). Esta es la forma más rápida de confirmar lo que te muestra un depurador.

### devinfo(8): el lado de Newbus

`devinfo(8)` no mira devfs en absoluto. Recorre el árbol de dispositivos de Newbus e imprime la jerarquía de dispositivos. Tu hijo `myfirst0` de `nexus0` aparece allí independientemente de que exista un cdev:

```sh
% devinfo -v
nexus0
  myfirst0
  pcib0
    pci0
      <...lots of PCI children...>
```

Esta es la herramienta a la que acudes cuando falta algo en `/dev` y necesitas comprobar si el propio dispositivo se ha conectado (attached). Si `devinfo` muestra `myfirst0` pero `ls /dev` no muestra el nodo, tu `make_dev_s` falló. Si ninguno de los dos muestra el dispositivo, tu `device_identify` o `device_probe` no crearon el hijo. Dos bugs distintos, dos soluciones distintas.

El flag `-r` filtra a la jerarquía de Newbus con raíz en un dispositivo específico, lo cual resulta útil en sistemas complejos con muchos dispositivos PCI.

### devfs(8): rulesets y reglas

`devfs(8)` es la interfaz administrativa de bajo nivel para los rulesets de devfs. Ya lo viste en la Sección 10. Tres subcomandos aparecen con frecuencia:

- `devfs rule showsets` lista los números de ruleset cargados en ese momento.
- `devfs rule -s N show` imprime las reglas dentro del ruleset `N`.
- `devfs rule -s N add path 'pattern' action args` añade una regla en tiempo de ejecución.

Las reglas añadidas en tiempo de ejecución no persisten; para hacerlas permanentes, añádelas a `/etc/devfs.rules` y ejecuta `service devfs restart`.

### sysctl dev.* y otras jerarquías

`sysctl dev.myfirst` imprime cada variable sysctl bajo el espacio de nombres de tu driver. Desde el Capítulo 7 ya tienes un árbol `dev.myfirst.0.stats`. Leerlo confirma que el softc está presente, que el attach se ejecutó y que los contadores avanzan.

Los sysctls son una superficie complementaria a `/dev`. Son principalmente para observabilidad; son más baratos de leer que abrir un dispositivo; no tienen coste de descriptor de archivo. Cuando una información es lo suficientemente simple como para ser un número o una cadena corta, considera exponerla como sysctl en lugar de como una lectura sobre el nodo de dispositivo.

### kldstat: ¿está cargado el módulo?

Cuando falta un nodo, vale la pena preguntarse primero: "¿está siquiera cargado mi driver?".

```sh
% kldstat | grep myfirst
 8    1 0xffffffff82a00000     3a50 myfirst.ko
```

Si ves el módulo en `kldstat`, el módulo está en el kernel. Si `devinfo` muestra el dispositivo pero `ls /dev` no muestra el nodo, el problema está dentro de tu driver. Si `kldstat` no muestra el módulo, el problema es externo: olvidaste hacer `kldload`, o la carga falló. Comprueba `dmesg`.

### dmesg: el registro de lo que ocurrió

Cada llamada a `device_printf` y `printf` desde un driver acaba en el buffer de mensajes del kernel, y `dmesg` (o `dmesg -a`) lo imprime. Cuando algo sale mal en esta superficie, `dmesg` es el primer lugar donde mirar:

```sh
% dmesg | tail -20
```

Los mensajes de attach y detach, cualquier fallo de `make_dev_s`, y cualquier mensaje de pánico procedente de los caminos de destrucción acaban aquí. Adquiere el hábito de observar `dmesg` con un segundo terminal abierto a `tail -f /var/log/messages` durante el desarrollo.

### Guía de resolución de problemas 1: falta el nodo

Una lista de comprobación para el caso "esperaba que `/dev/myfirst/0` existiera y no existe".

1. ¿Está cargado el módulo? `kldstat | grep myfirst`.
2. ¿Se ejecutó el attach? `devinfo -v | grep myfirst`.
3. ¿Tuvo éxito `make_dev_s`? `dmesg | tail` debería mostrar tu mensaje de attach satisfactorio.
4. ¿Está devfs montado en `/dev`? `mount | grep devfs`.
5. ¿Estás mirando la ruta correcta? Si tu cadena de formato era `"myfirst%d"`, el nodo es `/dev/myfirst0`, no `/dev/myfirst/0`. Los errores tipográficos ocurren.
6. ¿Oculta el nodo alguna entrada de `devfs.rules`? Ejecuta `devfs rule showsets` e inspecciona.

Nueve de cada diez veces, una de las tres primeras preguntas da la respuesta.

### Guía de resolución de problemas 2: kldunload devuelve EBUSY

Una lista de comprobación para el caso "puedo cargar mi módulo pero no puedo descargarlo".

1. ¿El nodo sigue abierto? `fstat /dev/myfirst/0` muestra quién lo tiene abierto.
2. ¿Tu detach devuelve `EBUSY` por sí mismo? Comprueba `dmesg` para ver si hay algún mensaje de tu driver. El detach de la etapa 2 devuelve `EBUSY` cuando `active_fhs > 0`.
3. ¿Hay un enlace `link` en `devfs.conf` apuntando a tu nodo? El enlace puede mantener una referencia si el destino está abierto.
4. ¿Hay algún thread del kernel bloqueado dentro de uno de tus manejadores? Busca el mensaje `Still N threads in foo` en `dmesg`. Si aparece, necesitas un `d_purge`.

La mayoría de los `EBUSY` se deben a descriptores abiertos. Los demás casos son poco frecuentes.

### Una nota sobre los hábitos

Ninguna de estas herramientas es inusual. Son los instrumentos cotidianos de la administración de FreeBSD. Lo que importa es el hábito de recurrir a ellas en un orden conocido cuando algo parece incorrecto. Las tres primeras veces que depures un nodo que falta, buscarás la herramienta correcta a tientas; la cuarta, el orden te parecerá automático. Construye ese reflejo ahora, mientras los problemas son pequeños.



## Errores frecuentes y aspectos que merece la pena vigilar

Una guía práctica de los errores que más a menudo sorprenden a los principiantes. Cada uno nombra el síntoma, la causa y el remedio.

- **Crear el nodo de dispositivo antes de que el softc esté listo.** *Síntoma:* open provoca una desreferencia de NULL en cuanto se carga el driver. *Causa:* `si_drv1` sigue sin asignar, o un campo del softc que consulta `open()` no ha sido inicializado. *Remedio:* establece `mda_si_drv1` en `make_dev_args` y finaliza los campos del softc antes de la llamada a `make_dev_s`. Piensa en `make_dev_s` como publicar, no preparar.
- **Destruir el softc antes que el nodo de dispositivo.** *Síntoma:* panics ocasionales durante `kldunload` o poco después. *Causa:* inversión del orden de desmontaje en `detach()`. *Remedio:* destruye siempre el cdev primero, luego el alias, luego el lock, luego el softc. El cdev es la puerta; ciérrala antes de desmantelar las habitaciones que hay detrás.
- **Almacenar estado por apertura en el cdev.** *Síntoma:* funciona bien con un solo usuario, estado corrupto con dos. *Causa:* posiciones de lectura u otros datos por descriptor almacenados en `si_drv1` o en el softc. *Remedio:* muévelos a una `struct myfirst_fh` y regístralos con `devfs_set_cdevpriv`.
- **Olvidar que los cambios en `/dev` no son persistentes.** *Síntoma:* un `chmod` que ejecutaste a mano desaparece tras un reinicio o una recarga del módulo. *Causa:* devfs es dinámico, no vive en disco. *Remedio:* escribe el cambio en `/etc/devfs.conf` y ejecuta `service devfs restart`.
- **No liberar el alias al hacer detach.** *Síntoma:* `kldunload` devuelve `EBUSY` y el driver queda bloqueado. *Causa:* el cdev alias sigue activo. *Remedio:* llama a `destroy_dev(9)` sobre el alias antes que sobre el principal en `detach()`.
- **Llamar a `devfs_set_cdevpriv` dos veces.** *Síntoma:* la segunda llamada devuelve `EBUSY` y el manejador retorna el error al usuario. *Causa:* dos rutas independientes en `open` intentan registrar datos privados, o el manejador se ejecuta dos veces para el mismo open. *Remedio:* audita la ruta de código para que exactamente un `devfs_set_cdevpriv` exitoso ocurra por invocación de `d_open`.
- **Reservar `fh` sin liberarlo en la ruta de error.** *Síntoma:* fuga de memoria sostenida correlacionada con opens fallidos. *Causa:* `devfs_set_cdevpriv` devolvió un error y la asignación quedó abandonada. *Remedio:* en cualquier error tras `malloc` y antes de un `devfs_set_cdevpriv` exitoso, libera la asignación explícitamente con `free`.
- **Confundir los alias con los enlaces simbólicos.** *Síntoma:* los permisos establecidos mediante `devfs.conf` en un `link` no coinciden con lo que el driver anuncia en el principal. *Causa:* mezcla de ambos mecanismos sobre el mismo nombre. *Remedio:* elige una sola herramienta por nombre; usa alias cuando el driver es propietario del nombre, y enlaces simbólicos cuando la comodidad del operador es el objetivo.
- **Usar modos muy permisivos «solo para probar».** *Síntoma:* un driver que se envió a staging con `0666` necesita que ese valor se restrinja sin romper a los consumidores. *Causa:* un modo temporal de laboratorio se convirtió en el valor por defecto. *Remedio:* usa `0600` por defecto, amplia solo cuando un consumidor concreto lo pida, y anota el motivo en un comentario junto a la línea de `mda_mode`.
- **Usar `make_dev` en código nuevo.** *Síntoma:* el driver compila y funciona, pero un revisor señala la llamada. *Causa:* `make_dev` es la forma más antigua de la familia y genera un panic en caso de fallo. *Remedio:* usa `make_dev_s` con una `struct make_dev_args` rellena. La forma más reciente es más fácil de leer, más fácil de comprobar errores y más amigable para futuras adiciones a la API. *Cómo detectarlo antes:* ejecuta `mandoc -Tlint` sobre tu driver y lee el apartado `SEE ALSO` de `make_dev(9)`.
- **Olvidar `D_VERSION`.** *Síntoma:* el driver carga pero el primer `open` devuelve un fallo críptico, o el kernel imprime un error de versión de cdevsw. *Causa:* el campo `d_version` de `cdevsw` se dejó a cero. *Remedio:* establece `.d_version = D_VERSION` como primer campo en todos los literales de `cdevsw`. *Cómo detectarlo antes:* una plantilla de código que incluya el campo evita que jamás escribas un `cdevsw` sin él.
- **Publicar código con `D_NEEDGIANT` «porque compiló».** *Síntoma:* el driver funciona, pero cada operación se serializa detrás del lock Giant, lo que ralentiza las cargas de trabajo intensas en SMP. *Causa:* el flag se copió de un driver más antiguo, o se añadió para silenciar una advertencia, y nunca se eliminó. *Remedio:* elimina el flag. Si tu driver realmente necesita Giant para funcionar, tiene un error de sincronización real que necesita una corrección real, no un flag.
- **Codificar a mano el identificador hexadecimal en los scripts de prueba.** *Síntoma:* una prueba falla en una máquina ligeramente diferente porque el `0x5a` en la salida de `ls -l` es distinto allí. *Causa:* el identificador `rdev` de devfs no es estable entre reinicios, kernels ni sistemas. *Remedio:* compara `stat -f '%d %i'` entre dos rutas para verificar la equivalencia del alias, en lugar de extraer el identificador hexadecimal de `ls -l`.
- **Asumir que `devfs.conf` se ejecuta antes de que cargue el driver.** *Síntoma:* una línea de `devfs.conf` para el nodo del driver no surte efecto tras `kldload`. *Causa:* `service devfs start` se ejecuta al principio del arranque, antes de los módulos cargados en tiempo de ejecución. *Remedio:* ejecuta `service devfs restart` después de cargar el driver, o compila el driver de forma estática para que sus nodos existan antes de que devfs arranque.
- **Depender de nombres de nodo con caracteres no POSIX.** *Síntoma:* los scripts de shell fallan con errores de entrecomillado; los patrones de `devfs.rules` no coinciden. *Causa:* el nombre del nodo usa espacios, dos puntos o caracteres no ASCII. *Remedio:* limítate a letras ASCII en minúsculas, dígitos y los tres separadores `/`, `-`, `.`. Otros caracteres a veces funcionarán y a veces no, y el «a veces no» siempre surge en el peor momento.
- **Perder estado por apertura en la ruta de error de `d_open`.** *Síntoma:* fuga de memoria sutil, detectada mucho después al ejecutar una prueba de estrés durante horas. *Causa:* `malloc` tuvo éxito, `devfs_set_cdevpriv` falló, y la asignación quedó abandonada sin liberarse. *Remedio:* toda ruta de error en `d_open` entre `malloc` y el `devfs_set_cdevpriv` exitoso debe llamar a `free` sobre la asignación. Escribir la ruta de error primero, antes que la ruta de éxito, es un hábito útil.
- **Registrar `devfs_set_cdevpriv` dos veces en el mismo open.** *Síntoma:* la segunda llamada devuelve `EBUSY` y el usuario ve `Device busy` al abrir, sin razón aparente. *Causa:* dos rutas de código independientes en `d_open` intentan adjuntar datos privados, o el manejador de apertura se ejecuta dos veces para el mismo archivo. *Remedio:* audita la ruta de código para que exactamente un `devfs_set_cdevpriv` exitoso ocurra por invocación de `d_open`. Si el driver genuinamente quiere reemplazar los datos, usa `devfs_clear_cdevpriv(9)` primero, aunque esto es casi siempre una señal de que el diseño necesita revisión.

### Errores que son en realidad sobre el ciclo de vida

Un grupo separado de errores proviene de la confusión sobre el ciclo de vida. Vale la pena señalarlos explícitamente.

- **Liberar el softc antes de destruir el cdev.** *Síntoma:* un panic poco después de `kldunload`, normalmente una desreferencia de NULL o un use-after-free en un manejador. *Causa:* el driver desmontó el estado del softc en `detach` antes de que `destroy_dev` terminara de vaciar el cdev, y un manejador en vuelo desreferenció el estado ya liberado. *Remedio:* destruye el cdev primero y confía en su comportamiento de vaciado; desmonta el softc solo después. *Cómo detectarlo antes:* ejecuta cualquiera de las pruebas de estrés mientras vigilas `dmesg` en busca de panics del kernel; la condición de carrera es fácil de provocar en un sistema SMP con carga moderada.
- **Asumir que `destroy_dev` retorna inmediatamente.** *Síntoma:* un deadlock, normalmente en un manejador que mantiene un lock y luego llama a una función que necesita ese mismo lock. *Causa:* `destroy_dev` bloquea hasta que los manejadores en vuelo retornan; si quien llama mantiene un lock que alguno de esos manejadores necesita, el sistema se bloquea. *Remedio:* nunca llames a `destroy_dev` mientras mantienes un lock que un manejador en vuelo pueda necesitar. En el caso habitual en `detach`, no mantengas ninguno.
- **Olvidar poner `is_attached = 0` al deshacer errores.** *Síntoma:* comportamiento sutilmente erróneo tras un ciclo de carga-descarga-recarga fallido; los manejadores creen que el dispositivo sigue conectado e intentan usar estado ya liberado. *Causa:* una ruta `goto fail_*` que no borró el flag. *Remedio:* el patrón de deshacimiento con etiqueta única del Capítulo 7; la última etiqueta de fallo siempre borra `is_attached` antes de retornar.

### Errores en permisos y políticas

Dos categorías de errores relacionados con los permisos suelen aparecer mucho después de que el driver se haya publicado.

- **Asumir que un nodo es «visible solo para root» porque se creó con `0600`.** *Síntoma:* una auditoría de seguridad señala que el nodo es accesible desde una jail que no debería verlo. *Causa:* el modo por sí solo no filtra la visibilidad en jails; `devfs.rules` es el filtro, y el valor por defecto puede ser lo suficientemente inclusivo como para pasar el nodo a la jail. *Remedio:* si el nodo no debe ser visible dentro de las jails, asegúrate de que el conjunto de reglas de jail por defecto lo oculte. `devfs_rules_hide_all` es el punto de partida conservador.
- **Confiar en `devfs.conf` para mantener un nodo en secreto en una máquina de laboratorio compartida.** *Síntoma:* un colaborador cambia `devfs.conf` y el nodo se vuelve legible para todo el mundo. *Causa:* `devfs.conf` es política del operador; cualquier operador con acceso de escritura a `/etc` puede cambiarlo. *Remedio:* la línea base propia del driver debe ser segura en ausencia de cualquier entrada en `devfs.conf`. Trata `devfs.conf` como un ampliador de permisos, nunca como un reductor de permisos respecto a una línea base fundamentalmente segura.

### Errores en la observabilidad

Unos pocos errores no tienen nada que ver con el código, pero sí mucho con lo fácil que es depurar tu driver.

- **Registrar cada apertura y cierre a volumen máximo.** *Síntoma:* el buffer de mensajes del kernel se llena de ruido rutinario del driver; los errores reales son más difíciles de encontrar. *Causa:* el driver usa `device_printf` para cada `d_open` y `d_close`. *Remedio:* condiciona los mensajes rutinarios con `if (bootverbose)` o elimínalos por completo una vez que el driver esté estable. Reserva `device_printf` para eventos del ciclo de vida y para errores genuinos.
- **No exponer suficientes sysctls para diagnosticar estados inusuales.** *Síntoma:* un usuario reporta un bug, no puedes saber qué cree el driver que está pasando, y añadir diagnósticos requiere recompilar y recargar. *Causa:* el árbol de sysctl es escaso. *Remedio:* expón contadores con generosidad. `active_fhs`, `open_count`, `read_count`, `write_count`, `error_count` son baratos. Añade un `attach_ticks` y un `last_event_ticks` para que los operadores sepan cuánto tiempo lleva activo el driver y cuándo fue la última vez que tuvo actividad.



## Un plan de estudio final

Si quieres profundizar en el material más allá de los laboratorios y los desafíos, aquí tienes un plan sugerido para la semana después de terminar el capítulo.

**Día 1: Relee una sección.** Elige cualquier sección que te pareciera más difícil en la primera lectura y léela de nuevo con el árbol de ejemplos abierto junto al texto. Solo lee. Todavía no intentes programar.

**Día 2: Reconstruye la etapa 2 desde cero.** Partiendo del código fuente de la etapa 2 del Capítulo 7, realiza cada uno de los cambios que describen las etapas del Capítulo 8, un commit a la vez. Compara tu trabajo con el árbol de ejemplos en cada etapa.

**Día 3: Rompe el driver a propósito.** Introduce tres bugs diferentes, uno a la vez: omite el destructor, olvida destruir el alias, devuelve el errno incorrecto. Predice qué hace cada bug. Ejecuta las comprobaciones. Comprueba si el fallo coincide con tu predicción.

**Día 4: Lee `null.c` y `led.c` de principio a fin.** Dos drivers pequeños, centrados en la superficie del archivo de dispositivo. Escribe un párrafo sobre cada uno resumiendo lo que hayas observado.

**Día 5: Añade el nodo de estado del Desafío 6.** Implementa el nodo de estado de solo lectura con un equivalente artesanal de `uiomove` por ahora; el Capítulo 9 mostrará el idioma real.

**Día 6: Prueba el laboratorio de jails.** Si todavía no has hecho el Laboratorio 8.7, hazlo ahora. Las jails merecen el esfuerzo de configurarlas porque los capítulos posteriores asumirán familiaridad con ellas.

**Día 7: Avanza.** No esperes a sentir que has «dominado» el capítulo 8. Volverás a sus contenidos de manera natural a medida que los capítulos posteriores vayan construyendo sobre él. El camino para ganar fluidez es seguir construyendo; el camino para quedarse atascado es esperar la perfección.

## Referencia rápida del árbol de archivos de acompañamiento

Como el árbol de código fuente de acompañamiento forma parte de cómo enseña este capítulo, un índice rápido de lo que hay en cada lugar puede ayudarte a encontrar las cosas durante los laboratorios y ejercicios de desafío.

### Etapas del driver

- `examples/part-02/ch08-working-with-device-files/stage0-structured-name/` es el resultado del Laboratorio 8.1: el driver de la etapa 2 del Capítulo 7 con el nodo movido a `/dev/myfirst/0` y los permisos ajustados a `root:operator 0660`.
- `examples/part-02/ch08-working-with-device-files/stage1-alias/` es el resultado del Laboratorio 8.2: la etapa 0 más `make_dev_alias("myfirst")`.
- `examples/part-02/ch08-working-with-device-files/stage2-perhandle/` es el resultado del Laboratorio 8.3: la etapa 1 más estado por apertura con `devfs_set_cdevpriv` y la eliminación de la comprobación de apertura exclusiva. Este es el driver que utilizan la mayoría de los demás ejercicios del capítulo.
- `examples/part-02/ch08-working-with-device-files/stage3-two-nodes/` es el resultado del Laboratorio 8.5: añade un nodo de control en `/dev/myfirst/%d.ctl` con su propio `cdevsw` y un modo de permisos más restrictivo.
- `examples/part-02/ch08-working-with-device-files/stage4-destroy-drain/` es el ejercicio del Laboratorio 8.8: un driver multi-cdev que muestra la diferencia entre usar `destroy_dev` solo y `destroy_dev_drain`. Compílalo con `make CFLAGS+=-DUSE_DRAIN=1` para obtener la variante correcta.

### Sondas en userland

- `userland/probe_myfirst.c`: apertura, lectura y cierre en una sola operación.
- `userland/hold_myfirst.c`: abre y se queda dormido sin cerrar, para ejercitar el destructor de cdevpriv al terminar el proceso.
- `userland/stat_myfirst.c`: muestra los metadatos de `stat(2)` para una o varias rutas; útil para comparar el alias y el nodo principal.
- `userland/parallel_probe.c`: abre N descriptores desde un mismo proceso, los mantiene abiertos y los cierra todos.
- `userland/stress_probe.c`: bucle de apertura y cierre para detectar fugas de recursos.
- `userland/devd_watch.sh`: se suscribe a los eventos de `devd(8)` y los filtra por `myfirst`.

### Ejemplos de configuración

- `devfs/devfs.conf.example`: entradas de persistencia del Laboratorio 8.4.
- `devfs/devfs.rules.example`: conjunto de reglas para jail del Laboratorio 8.7.
- `devfs/devd.conf.example`: regla devd del Desafío 5.
- `jail/jail.conf.example`: definición del jail del Laboratorio 8.7 que hace referencia al conjunto de reglas 100.

### Diferencias entre etapas

Cada etapa es un diff respecto a la etapa 2 del Capítulo 7. Un primer ejercicio útil después de leer el capítulo es ejecutar `diff` entre cada par de etapas y leer el resultado. Los cambios son suficientemente pequeños para entenderlos línea a línea, y el diff cuenta la historia progresiva de los cambios del código del capítulo de forma más compacta que releer cada archivo fuente.

```sh
% diff -u examples/part-02/ch07-writing-your-first-driver/stage2-final/myfirst.c \
         examples/part-02/ch08-working-with-device-files/stage0-structured-name/myfirst.c

% diff -u examples/part-02/ch08-working-with-device-files/stage0-structured-name/myfirst.c \
         examples/part-02/ch08-working-with-device-files/stage1-alias/myfirst.c

% diff -u examples/part-02/ch08-working-with-device-files/stage1-alias/myfirst.c \
         examples/part-02/ch08-working-with-device-files/stage2-perhandle/myfirst.c
```

Cada diff debería mostrar un puñado de adiciones y ninguna sustracción inesperada. Si ves cambios sorprendentes, el texto del capítulo es donde encontrarás el razonamiento.

### Sobre reutilizar este árbol más adelante

Las etapas aquí no pretenden ser el driver «definitivo». Son instantáneas que corresponden a puntos de control del capítulo. Cuando continúes con el Capítulo 9, editarás la etapa 2 directamente, y seguirá creciendo. Cuando llegues al final de la Parte 2, el driver habrá evolucionado hasta convertirse en algo mucho más rico de lo que captura cualquier etapa individual. Ese es el objetivo: cada capítulo añade una capa, y el árbol de acompañamiento sirve para mostrar cada capa por separado de forma que puedas ver la progresión.



## Una reflexión final sobre las interfaces

Cada capítulo de este libro enseña algo diferente, pero hay algunos que enseñan algo que recorre toda la práctica del desarrollo de drivers. El Capítulo 8 es uno de ellos. El tema concreto son los archivos de dispositivo, pero el tema más amplio es el **diseño de interfaces**: ¿cómo das forma al límite entre un fragmento de código que controlas y un mundo que no controlas?

La filosofía UNIX tiene una respuesta que ha sobrevivido medio siglo. Haz que el límite se parezca lo más posible a un archivo ordinario. Deja que el vocabulario ya existente de `open`, `read`, `write` y `close` haga el trabajo pesado. Elige nombres y permisos de modo que los operadores puedan razonar sobre ellos sin leer tu código fuente. Expón solo lo que el usuario necesita, y nada más. Limpia después de ti con tanta agresividad que el kernel pueda detectar cuándo has perdido la pista de algo. Documenta cada decisión que tomes con un comentario, un `device_printf` o un sysctl.

Ninguno de estos principios es exclusivo de los archivos de dispositivo. Vuelven a aparecer en el diseño de interfaces de red, en la estratificación del almacenamiento, en las APIs internas del kernel, en las herramientas de userland que hablan con el kernel. La razón por la que dedicamos un capítulo entero a la pequeña superficie bajo `/dev` es que los mismos hábitos, practicados aquí sobre algo tangible y acotado, te servirán en cada capa del kernel que llegues a tocar.

Cuando lees un driver en `/usr/src/sys` que te parece elegante, una de las razones es casi siempre que la superficie de sus archivos de dispositivo es estrecha y honesta. Cuando lees un driver que parece enredado, una de las razones es casi siempre que su superficie de archivos de dispositivo se diseñó con prisas, o se amplió en respuesta a una presión a corto plazo, y nunca se volvió a reducir. El objetivo de este capítulo ha sido ayudarte a notar esa diferencia y darte el vocabulario y la disciplina para escribir el primer tipo de driver en lugar del segundo.



## Cerrando

Ahora entiendes la capa entre tu driver y el espacio de usuario lo suficientemente bien como para darle forma de manera deliberada. En concreto:

- `/dev` no es un directorio en disco. Es una vista devfs de objetos vivos del kernel.
- Un `struct cdev` es la identidad del nodo en el lado del kernel. Un vnode es la forma en que el VFS lo alcanza. Un `struct file` es la forma en que una llamada `open(2)` individual se asienta en el kernel.
- `mda_uid`, `mda_gid` y `mda_mode` establecen la línea base de lo que muestra `ls -l`. `devfs.conf` y `devfs.rules` añaden la política del operador encima.
- La ruta del nodo es lo que indique tu cadena de formato, barras incluidas. Los subdirectorios bajo `/dev` son una forma normal y bienvenida de agrupar nodos relacionados.
- `make_dev_alias(9)` permite que un cdev responda a más de un nombre. Recuerda eliminar el alias cuando destruyas el nodo principal.
- `devfs_set_cdevpriv(9)` da a cada `open(2)` su propio estado, con limpieza automática. Esta es la herramienta en la que más te apoyarás en el próximo capítulo.

El driver con el que entras al Capítulo 9 es el mismo `myfirst` con el que empezaste, pero con un nombre más limpio, un conjunto de permisos más sensato y estado por apertura listo para albergar las posiciones de lectura, los contadores de bytes y la pequeña contabilidad que necesitará la E/S real. Mantén el archivo abierto. Pronto volverás a editarlo.

### Una breve autoevaluación

Antes de continuar, asegúrate de que puedes responder a cada una de las preguntas siguientes sin volver al capítulo. Si alguna respuesta te resulta difusa, repasa la sección correspondiente antes de comenzar el Capítulo 9.

1. ¿Cuál es la diferencia entre un `struct cdev`, un vnode de devfs y un `struct file`?
2. ¿De dónde obtiene `make_dev_s(9)` la propiedad y el modo para el nodo que crea?
3. ¿Por qué un `chmod` sobre `/dev/yournode` no sobrevive a un reinicio?
4. ¿Qué hace `make_dev_alias(9)` y en qué se diferencia de `link` en `devfs.conf`?
5. ¿Cuándo se ejecuta el destructor registrado con `devfs_set_cdevpriv(9)` y cuándo *no* se ejecuta?
6. ¿Cómo confirmarías desde userland que dos rutas resuelven al mismo cdev?
7. ¿Por qué se requiere `D_VERSION` en cada `cdevsw` y qué ocurre cuando falta?
8. ¿Cuándo elegirías `make_dev_s` en lugar de `make_dev_p` y por qué?
9. ¿Qué garantías te ofrece `destroy_dev(9)` respecto a los threads que en ese momento están dentro de tus manejadores?
10. Si un jail no ve `/dev/myfirst/0` pero el host sí, ¿dónde está la política que lo ocultó y cómo la inspeccionarías?

Si puedes responder las diez con tus propias palabras, el próximo capítulo se sentirá como una continuación natural y no como un salto.

### Resumen por temas

El capítulo ha cubierto mucho terreno. Aquí tienes una breve reorganización del material por temas en lugar de por secciones, para que puedas asentar lo que has aprendido.

**Sobre la relación entre el kernel y el sistema de archivos:**

- devfs es un sistema de archivos virtual que presenta la colección de objetos `struct cdev` activos del kernel como nodos similares a archivos bajo `/dev`.
- No tiene almacenamiento en disco. Cada nodo refleja algo que el kernel mantiene en ese momento.
- Solo admite un conjunto pequeño y bien definido de operaciones sobre sus nodos.
- Los cambios realizados de forma interactiva (con `chmod`, por ejemplo) no persisten. La política persistente reside en `/etc/devfs.conf` y `/etc/devfs.rules`.

**Sobre los objetos con los que interactúa tu driver:**

- Un `struct cdev` es la identidad del nodo de dispositivo en el lado del kernel. Uno por nodo, independientemente de cuántos descriptores de archivo apunten a él.
- El `struct cdevsw` es la tabla de despacho que proporciona tu driver. Asocia cada tipo de operación con un manejador en tu código.
- El `struct file` y el vnode de devfs se sitúan entre el descriptor de archivo del usuario y tu cdev. Transportan el estado por apertura y enrutan las operaciones.

**Sobre la creación y destrucción de nodos:**

- `make_dev_s(9)` es la forma moderna y recomendada de crear un cdev. Rellena un `struct make_dev_args`, pásalo y obtendrás un cdev a cambio.
- `make_dev_alias(9)` crea un segundo nombre para un cdev existente. Los alias son cdevs de primera clase; el kernel los mantiene sincronizados con el nodo principal.
- `destroy_dev(9)` destruye un cdev de forma síncrona, vaciando los manejadores en vuelo. Sus variantes `destroy_dev_sched` y `destroy_dev_drain` cubren los casos diferidos y de barrido respectivamente.

**Sobre el estado por apertura:**

- `devfs_set_cdevpriv(9)` asocia un puntero proporcionado por el driver al descriptor de archivo actual, junto con un destructor.
- `devfs_get_cdevpriv(9)` recupera ese puntero dentro de los manejadores posteriores.
- El destructor se dispara exactamente una vez por cada llamada `set` exitosa, cuando cae la última referencia al descriptor de archivo.
- Este es el mecanismo principal para la contabilidad por apertura en los drivers modernos de FreeBSD.

**Sobre la política de acceso:**

- El driver establece un modo base, uid y gid en la llamada a `make_dev_s`.
- `/etc/devfs.conf` puede ajustarlos por nodo en los montajes devfs del host.
- `/etc/devfs.rules` puede definir conjuntos de reglas con nombre que filtran y ajustan por montaje, normalmente para jails.
- Tres capas pueden actuar sobre el mismo cdev, y el orden importa.

**Sobre userland:**

- `ls -l`, `stat(1)`, `fstat(1)`, `procstat(1)`, `devinfo(8)`, `devfs(8)`, `sysctl(8)` y `kldstat(8)` son las herramientas cotidianas para inspeccionar y manipular la superficie que expone tu driver.
- Merece la pena escribir pequeños programas C en userland que abran, lean, cierren y hagan `stat` del dispositivo. Te dan control sobre la temporización y te permiten probar casos extremos de forma limpia.

**Sobre la disciplina:**

- Usa permisos restrictivos por defecto y amplíalos solo cuando un consumidor concreto lo pida.
- Usa constantes con nombre (`UID_ROOT`, `GID_WHEEL`) en lugar de números en bruto.
- Destruye en el orden inverso al de creación.
- Libera las asignaciones de memoria en todos los caminos de error antes de retornar.
- Registra los eventos del ciclo de vida con `device_printf(9)` para que `dmesg` cuente la historia de lo que hace tu driver.

Es mucho. No necesitas retenerlo todo a la vez. Los laboratorios y los desafíos son donde el material se convierte en memoria muscular; el texto es solo la guía de lectura.

### Una mirada al Capítulo 9

En el Capítulo 9 rellenaremos `d_read` y `d_write` correctamente. Aprenderás cómo el kernel mueve bytes entre la memoria del usuario y la memoria del kernel con `uiomove(9)`, por qué `struct uio` tiene el aspecto que tiene, y cómo diseñar un driver que sea seguro frente a lecturas cortas, escrituras cortas, buffers desalineados y programas de usuario que se comportan mal. El estado por apertura que acabas de conectar almacenará los desplazamientos de lectura y el estado de escritura. El alias mantendrá funcionando las interfaces de usuario antiguas mientras el driver crece. Y el modelo de permisos que has configurado aquí mantendrá honestos tus scripts de laboratorio cuando empieces a enviar datos reales.

En concreto, el Capítulo 9 necesitará los campos que añadiste a `struct myfirst_fh` para dos cosas. El contador `reads` ganará un campo `read_offset` correspondiente para que cada descriptor recuerde en qué punto estaba dentro de un flujo de datos sintetizado. Al contador `writes` se le unirá un pequeño buffer circular al que `d_write` añade datos y del que `d_read` extrae. El puntero `fh` que recuperas con `devfs_get_cdevpriv` en cada manejador será el punto de entrada para todo ese estado.

El alias que creaste en el Laboratorio 8.2 seguirá funcionando sin ningún cambio: tanto `/dev/myfirst` como `/dev/myfirst/0` producirán datos, y el estado por descriptor será independiente entre ambos.

Los permisos que estableciste en el Laboratorio 8.1 y el Laboratorio 8.4 seguirán siendo los valores predeterminados adecuados para el desarrollo: lo suficientemente restrictivos como para obligar a usar `sudo` de forma consciente cuando un usuario sin privilegios acceda al dispositivo, y lo suficientemente abiertos como para que un arnés de pruebas del grupo `operator` pueda ejecutar las pruebas de la ruta de datos sin necesidad de escalar privilegios.

Has construido una puerta bien formada. En el próximo capítulo, las habitaciones que hay detrás de ella cobrarán vida.

## Referencia: make_dev_s y cdevsw de un vistazo

Esta referencia reúne las declaraciones y los valores de flag más útiles en un solo lugar, con referencias cruzadas a las secciones del capítulo que explicaron cada uno. Mantenla abierta mientras escribes tus propios drivers; la mayoría de los errores que cuestan un día entero son errores relativos a uno de estos valores.

### Esqueleto canónico de make_dev_s

Una plantilla disciplinada para un driver de un único nodo:

```c
struct make_dev_args args;
int error;

make_dev_args_init(&args);
args.mda_devsw   = &myfirst_cdevsw;
args.mda_uid     = UID_ROOT;
args.mda_gid     = GID_OPERATOR;
args.mda_mode    = 0660;
args.mda_si_drv1 = sc;

error = make_dev_s(&args, &sc->cdev, "myfirst/%d", sc->unit);
if (error != 0) {
        device_printf(dev, "make_dev_s: %d\n", error);
        /* unwind and return */
        goto fail;
}
```

### Esqueleto canónico de cdevsw

```c
static struct cdevsw myfirst_cdevsw = {
        .d_version = D_VERSION,
        .d_name    = "myfirst",
        .d_open    = myfirst_open,
        .d_close   = myfirst_close,
        .d_read    = myfirst_read,
        .d_write   = myfirst_write,
        .d_ioctl   = myfirst_ioctl,     /* add in Chapter 25 */
        .d_poll    = myfirst_poll,      /* add in Chapter 10 */
        .d_kqfilter = myfirst_kqfilter, /* add in Chapter 10 */
};
```

Los campos omitidos son equivalentes a `NULL`, que el kernel interpreta como «no admitido» o «usar el comportamiento por defecto», según el campo.

### La estructura make_dev_args

De `/usr/src/sys/sys/conf.h`:

```c
struct make_dev_args {
        size_t         mda_size;         /* set by make_dev_args_init */
        int            mda_flags;        /* MAKEDEV_* flags */
        struct cdevsw *mda_devsw;        /* required */
        struct ucred  *mda_cr;           /* usually NULL */
        uid_t          mda_uid;          /* see UID_* in conf.h */
        gid_t          mda_gid;          /* see GID_* in conf.h */
        int            mda_mode;         /* octal mode */
        int            mda_unit;         /* unit number (0..INT_MAX) */
        void          *mda_si_drv1;      /* usually the softc */
        void          *mda_si_drv2;      /* second driver pointer */
};
```

### La palabra de flags MAKEDEV

| Flag                   | Significado                                                     |
|------------------------|-----------------------------------------------------------------|
| `MAKEDEV_REF`          | Añade una referencia extra en la creación.                      |
| `MAKEDEV_NOWAIT`       | No duerme esperando memoria; devuelve `ENOMEM` si no hay disponible. |
| `MAKEDEV_WAITOK`       | Duerme esperando memoria (opción por defecto para `make_dev_s`). |
| `MAKEDEV_ETERNAL`      | Marca el cdev como indestructible.                              |
| `MAKEDEV_CHECKNAME`    | Valida el nombre; devuelve error si el nombre no es válido.     |
| `MAKEDEV_WHTOUT`       | Crea una entrada de tipo whiteout (sistemas de archivos apilados). |
| `MAKEDEV_ETERNAL_KLD`  | `MAKEDEV_ETERNAL` cuando es estático, cero si se construye como KLD. |

### El campo d_flags de cdevsw

| Flag             | Significado                                                              |
|------------------|--------------------------------------------------------------------------|
| `D_TAPE`         | Indicación de categoría: dispositivo de cinta.                           |
| `D_DISK`         | Indicación de categoría: dispositivo de disco (obsoleto; los discos modernos usan GEOM). |
| `D_TTY`          | Indicación de categoría: dispositivo TTY.                                |
| `D_MEM`          | Indicación de categoría: dispositivo de memoria, como `/dev/mem`.        |
| `D_TRACKCLOSE`   | Llama a `d_close` en cada `close(2)` sobre cada descriptor.              |
| `D_MMAP_ANON`    | Semántica de mmap anónimo para este cdev.                                |
| `D_NEEDGIANT`    | Fuerza el despacho con el Giant lock. Evítalo en código nuevo.           |
| `D_NEEDMINOR`    | El driver usa `clone_create(9)` para la asignación de números menores.   |

### Constantes comunes de UID y GID

| Constante      | Valor numérico | Propósito                                       |
|----------------|----------------|-------------------------------------------------|
| `UID_ROOT`     | 0              | Superusuario. Propietario por defecto de la mayoría de los nodos. |
| `UID_BIN`      | 3              | Ejecutables de daemon.                          |
| `UID_UUCP`     | 66             | Subsistema UUCP.                                |
| `UID_NOBODY`   | 65534          | Marcador sin privilegios.                       |
| `GID_WHEEL`    | 0              | Administradores de confianza.                   |
| `GID_KMEM`     | 2              | Acceso de lectura a la memoria del kernel.      |
| `GID_TTY`      | 4              | Dispositivos terminales.                        |
| `GID_OPERATOR` | 5              | Herramientas operativas.                        |
| `GID_BIN`      | 7              | Archivos propiedad del daemon.                  |
| `GID_VIDEO`    | 44             | Acceso al framebuffer de vídeo.                 |
| `GID_DIALER`   | 68             | Programas de marcación saliente por puerto serie. |
| `GID_NOGROUP`  | 65533          | Sin grupo.                                      |
| `GID_NOBODY`   | 65534          | Marcador sin privilegios.                       |

### Funciones de destrucción

| Función                                    | Cuándo usarla                                                       |
|--------------------------------------------|---------------------------------------------------------------------|
| `destroy_dev(cdev)`                        | Destrucción ordinaria y síncrona con drenado.                       |
| `destroy_dev_sched(cdev)`                  | Destrucción diferida cuando no se puede dormir.                     |
| `destroy_dev_sched_cb(cdev,cb,arg)`        | Destrucción diferida con una llamada de retorno posterior.          |
| `destroy_dev_drain(cdevsw)`                | Espera a que todos los cdevs de un `cdevsw` terminen, antes de liberarlo. |
| `delist_dev(cdev)`                         | Elimina un cdev de devfs sin destruirlo completamente todavía.      |

### Funciones de estado por apertura

| Función                                    | Propósito                                                           |
|--------------------------------------------|---------------------------------------------------------------------|
| `devfs_set_cdevpriv(priv, dtor)`           | Asocia datos privados al descriptor actual.                         |
| `devfs_get_cdevpriv(&priv)`                | Recupera los datos privados del descriptor actual.                  |
| `devfs_clear_cdevpriv()`                   | Desvincula y ejecuta el destructor anticipadamente.                 |
| `devfs_foreach_cdevpriv(dev, cb, arg)`     | Itera todos los registros por apertura de un cdev.                  |

### Funciones de alias

| Función                                              | Propósito                                          |
|------------------------------------------------------|----------------------------------------------------|
| `make_dev_alias(pdev, fmt, ...)`                     | Crea un alias para un cdev principal.              |
| `make_dev_alias_p(flags, &cdev, pdev, fmt, ...)`     | Crea un alias con flags y devolución de error.     |
| `make_dev_physpath_alias(...)`                       | Crea un alias de ruta topológica.                  |

### Helpers de conteo de referencias

Normalmente los drivers no los llaman directamente. Se incluyen aquí para su reconocimiento.

| Función                          | Propósito                                                    |
|----------------------------------|--------------------------------------------------------------|
| `dev_ref(cdev)`                  | Adquiere una referencia de larga duración.                   |
| `dev_rel(cdev)`                  | Libera una referencia de larga duración.                     |
| `dev_refthread(cdev, &ref)`      | Adquiere una referencia para una llamada de manejador.       |
| `dev_relthread(cdev, ref)`       | Libera la referencia de la llamada de manejador.             |

### Dónde leer más

- Páginas de manual `make_dev(9)`, `destroy_dev(9)`, `cdev(9)` para la superficie de la API.
- `devfs(5)`, `devfs.conf(5)`, `devfs.rules(5)`, `devfs(8)` para la documentación de la capa del sistema de archivos.
- `/usr/src/sys/sys/conf.h` para las definiciones canónicas de la estructura y los flags.
- `/usr/src/sys/kern/kern_conf.c` para la implementación de la familia `make_dev*`.
- `/usr/src/sys/fs/devfs/devfs_vnops.c` para la implementación de `devfs_set_cdevpriv` y funciones relacionadas.
- `/usr/src/sys/fs/devfs/devfs_rule.c` para el subsistema de reglas.

Esta referencia es breve a propósito. El capítulo es donde vive el razonamiento; esta sección es simplemente la tabla de consulta rápida.

### Catálogo de patrones condensado

La tabla siguiente resume los principales patrones que ha mostrado el capítulo, cada uno emparejado con la sección que lo explica en detalle. Cuando estés a medias construyendo un driver y necesites orientarte rápidamente, consulta primero esta lista.

| Patrón                                                            | Sección del capítulo                                              |
|-------------------------------------------------------------------|-------------------------------------------------------------------|
| Crear un nodo de datos en `attach`, destruir en `detach`          | Capítulo 7, referenciado en el Laboratorio 8.1 del Capítulo 8    |
| Mover el nodo a un subdirectorio bajo `/dev`                      | Nomenclatura, números de unidad y subdirectorios                 |
| Exponer tanto un nodo de datos como un nodo de control            | Múltiples nodos por dispositivo; Laboratorio 8.5                 |
| Añadir un alias para que el driver responda en dos rutas          | Alias: un cdev, más de un nombre; Laboratorio 8.2                |
| Ampliar o restringir los permisos a nivel de operador             | Política persistente; Laboratorio 8.4                            |
| Ocultar o exponer un nodo dentro de un jail                       | Política persistente; Laboratorio 8.7                            |
| Dar a cada apertura su propio estado y contadores                 | Estado por apertura con `devfs_set_cdevpriv`; Laboratorio 8.3   |
| Ejecutar asignación pre-apertura segura frente a fallos           | Estado por apertura; Desafío 2                                    |
| Imponer apertura exclusiva con `EBUSY`                            | Códigos de error; Receta 1                                        |
| Destruir múltiples cdevs en un solo detach                        | Destrucción segura de cdevs; Laboratorio 8.8                     |
| Reaccionar a la creación de nodos en userland mediante devd       | Probar tu dispositivo desde userland; Desafío 5                  |
| Comparar dos rutas para verificar que comparten un cdev           | Probar tu dispositivo desde userland                             |
| Exponer el estado del driver a través de sysctl                   | Flujos de trabajo prácticos; referencia al Capítulo 7            |

Cada fila nombra un patrón. Cada patrón tiene una receta breve en algún punto del capítulo. Cuando te enfrentes a un problema de diseño, encuentra la fila que encaja y sigue el enlace de vuelta.

### Valores de errno comunes por operación

Una referencia cruzada compacta de qué valores de errno son convencionales para cada operación. Úsala junto con la Sección 13.

| Operación                | Valores de errno devueltos más habituales                                      |
|--------------------------|--------------------------------------------------------------------------------|
| `d_open`                 | `0`, `ENXIO`, `EBUSY`, `EACCES`, `EPERM`, `EINVAL`, `EAGAIN`                  |
| `d_close`                | `0` casi siempre; registra las condiciones inusuales, pero no las devuelvas    |
| `d_read`                 | `0` en caso de éxito, `ENXIO` si el dispositivo ha desaparecido, `EFAULT` para buffers inválidos, `EINTR` ante una señal, `EAGAIN` para reintento sin bloqueo |
| `d_write`                | La misma familia que `d_read`, más `ENOSPC` para falta de espacio              |
| `d_ioctl` (Capítulo 25)  | `0` en caso de éxito, `ENOTTY` para comandos desconocidos, `EINVAL` para argumentos incorrectos |
| `d_poll` (Capítulo 10)   | Devuelve una máscara de revents, no un errno                                   |

El driver del Capítulo 8 concierne principalmente a las dos primeras filas. El Capítulo 9 extenderá el alcance hasta la tercera y la cuarta.

### Breve glosario de términos usados en el capítulo

Para lectores que no hayan visto todos los términos antes, o que quieran un recordatorio rápido.

- **cdev**: la identidad del archivo de dispositivo en el lado del kernel, una por nodo.
- **cdevsw**: la tabla de despacho que asocia operaciones a los manejadores del driver.
- **cdevpriv**: estado por apertura asociado a un descriptor de archivo mediante `devfs_set_cdevpriv(9)`.
- **devfs**: el sistema de archivos virtual que presenta los cdevs como nodos bajo `/dev`.
- **mda_***: miembros de la estructura `make_dev_args` pasada a `make_dev_s(9)`.
- **softc**: datos privados por dispositivo asignados por Newbus y accesibles mediante `device_get_softc(9)`.
- **SI_***: flags almacenados en un `struct cdev` en su campo `si_flags`.
- **D_***: flags almacenados en un `struct cdevsw` en su campo `d_flags`.
- **MAKEDEV_***: flags pasados a `make_dev_s(9)` y sus funciones relacionadas a través de `mda_flags`.
- **UID_*** y **GID_***: constantes simbólicas para identidades estándar de usuario y grupo.
- **destroy_dev_drain**: la función de drenado a nivel de `cdevsw` que se usa al descargar un módulo que ha creado muchos cdevs.
- **devfs.conf**: el archivo de política a nivel de host para la propiedad y el modo persistentes de los nodos.
- **devfs.rules**: el archivo de conjunto de reglas que modela las vistas por montaje de devfs, principalmente para jails.

El glosario irá creciendo a medida que avance el libro. El Capítulo 8 ha introducido la mayoría de los términos que necesitará; los capítulos posteriores añadirán los suyos y harán referencia a esta lista.

## Consolidación y repaso

Antes de dar el capítulo por terminado, merece la pena hacer un último recorrido por el material. Esta sección une las piezas de una manera que la estructura sección a sección no podía lograr del todo.

### Las tres ideas más importantes

Si solo pudieras recordar tres cosas del Capítulo 8, que sean estas:

**Primero, `/dev` es un sistema de archivos activo mantenido por el kernel.** Cada nodo está respaldado por un `struct cdev` que tu driver posee. Nada de lo que ves en `/dev` es persistente; es una ventana al estado actual del kernel. Cuando escribes un driver, estás añadiendo a esa ventana y eliminando de ella, y el kernel refleja tus cambios con honestidad.

**Segundo, la interfaz de archivos de dispositivo forma parte de la interfaz pública de tu driver.** El nombre, los permisos, la propiedad, la existencia de alias, el conjunto de operaciones que implementas, los valores de errno que devuelves, el orden de destrucción: todas estas son decisiones de las que un usuario depende. Trátalas como contractuales desde el primer día. Ampliarlas o restringirlas con posterioridad siempre resulta más perturbador que elegir una base correcta desde el principio.

**Tercero, el estado por apertura es el lugar adecuado para la información por descriptor.** `devfs_set_cdevpriv(9)` existe porque el modelo de descriptores de UNIX es más expresivo de lo que un único softc puede representar. Cuando dos procesos abren el mismo nodo, cada uno merece su propia vista de él. Proporcionar estado por apertura cuesta una pequeña asignación y un destructor; la alternativa es un laberinto de condiciones de carrera sobre estado compartido que no querrás depurar.

Todo lo demás en el Capítulo 8 desarrolla una de estas tres ideas.

### La forma del driver con el que acabas el capítulo

Al final del Laboratorio 8.8, tu driver `myfirst` ha crecido hasta convertirse en algo que se parece mucho más a un driver real de FreeBSD que al final del Capítulo 7. En concreto:

- Tiene un softc, un mutex y un árbol de sysctl.
- Crea su nodo en un subdirectorio dentro de `/dev`, con propietario y modo elegidos intencionadamente.
- Ofrece un alias para el nombre heredado, de modo que los usuarios existentes siguen funcionando.
- Asigna estado por apertura en cada `open(2)` y lo limpia mediante un destructor que se ejecuta de forma fiable en todos los casos.
- Cuenta las aperturas activas y se niega a hacer detach mientras haya alguna viva.
- Destruye sus cdevs en un orden razonable durante el detach.

Esa es la forma de casi todos los drivers pequeños que encontrarás en `/usr/src/sys/dev`. No necesitas construir desde cero cada driver que escribas; la mayor parte del tiempo, partirás de una plantilla que tiene exactamente este aspecto y añadirás encima la lógica específica del subsistema.

### Qué practicar antes de empezar el Capítulo 9

Una lista breve de ejercicios para consolidar el material del capítulo, en orden creciente aproximado de dificultad:

1. **Reconstruye `myfirst` etapa por etapa sin mirar el árbol de ejemplos complementarios.** Abre el código fuente de la etapa 2 del Capítulo 7. Realiza los cambios del Laboratorio 8.1 desde cero. Luego los del Laboratorio 8.2. Después los del Laboratorio 8.3. Compara tu resultado con el código fuente de la etapa 2 del árbol de ejemplos complementarios. Las diferencias son cosas que merece la pena entender.
2. **Rompe una etapa a propósito.** Introduce un error deliberado en el Laboratorio 8.3 (por ejemplo, omite la llamada a `devfs_set_cdevpriv`). Predice qué ocurrirá cuando cargues el módulo y ejecutes la prueba paralela. Ejecútala. Comprueba si el fallo coincide con tu predicción.
3. **Añade un tercer cdev.** Extiende el driver de la etapa 3 del Laboratorio 8.5 con un segundo nodo de control que sirva a un namespace diferente. Observa cómo el nodo aparece y desaparece al mismo ritmo que el driver.
4. **Escribe un servicio en userland.** Escribe un pequeño daemon que abra `/dev/myfirst/0` al arrancar, mantenga el descriptor y responda a `SIGUSR1` leyendo y registrando en el log. Instálalo. Pruébalo con cargas y descargas del driver. Observa qué ocurre cuando el driver se descarga mientras el daemon aún tiene su descriptor abierto.
5. **Lee un driver nuevo.** Elige un driver de `/usr/src/sys/dev` que no hayas tocado todavía, léelo desde la perspectiva de los archivos de dispositivo y clasifícalo usando el árbol de decisión de la Sección 15. Escribe un párrafo describiendo lo que has encontrado.

Cada ejercicio requiere entre treinta minutos y una hora. Hacer dos o tres es suficiente para pasar el material del capítulo de "lo leí una vez" a "me siento cómodo con esto". Hacerlos todos te dará una intuición que te servirá durante el resto del libro.

El Capítulo 9 es el siguiente. Las habitaciones detrás de la puerta cobran vida.
