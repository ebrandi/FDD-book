---
title: "Conceptos de sistemas operativos"
description: "Complemento conceptual sobre la frontera kernel/usuario, los tipos de drivers, las estructuras de datos recurrentes del kernel y el proceso de arranque hasta init en el que deben encajar los drivers."
appendix: "D"
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 25
language: "es-ES"
---
# Apéndice D: Conceptos del sistema operativo

## Cómo usar este apéndice

Los capítulos principales enseñan al lector a escribir un driver de dispositivo para FreeBSD: cómo declarar un softc, cómo hacer probe y attach a un bus, cómo registrar un dispositivo de caracteres, cómo configurar una interrupción y cómo hacer DMA. Bajo todo eso existe la suposición de fondo de que el lector tiene un modelo mental funcional de lo que es un kernel de sistema operativo, de cómo se diferencia del userland, de lo que es un driver *dentro* de ese kernel y de cómo la máquina pasa del encendido a un kernel listo para cargar y ejecutar ese driver. El libro introduce cada pieza de este modelo en el capítulo donde primero cobra relevancia, pero las piezas nunca aparecen todas juntas en un mismo lugar.

Este apéndice es ese lugar. Es un compañero conceptual para las ideas del sistema operativo que el resto del libro utiliza continuamente: la frontera kernel/usuario, la definición de un driver, las pocas estructuras de datos que verás una y otra vez en el código fuente de FreeBSD y la secuencia de boot hasta init que prepara el escenario para todo lo que un driver puede hacer. Es deliberadamente breve. El objetivo es consolidar el modelo mental, no enseñar teoría de sistemas operativos ni repetir lo que los capítulos ya enseñan en profundidad.

### Qué encontrarás aquí

El apéndice está organizado por relevancia para los drivers, no según la taxonomía de los libros de texto. Cuatro secciones, cada una con un pequeño conjunto de entradas. Cada entrada sigue el mismo ritmo compacto:

- **Qué es.** Una o dos frases de definición sencilla.
- **Por qué importa a los desarrolladores de drivers.** El lugar concreto donde el concepto aparece en tu código.
- **Cómo aparece esto en FreeBSD.** La API, la cabecera, el subsistema o la convención que nombra la idea.
- **Trampa habitual.** El malentendido que realmente cuesta tiempo.
- **Dónde lo enseña el libro.** Una referencia al capítulo que lo utiliza en contexto.
- **Qué leer a continuación.** Una página de manual, una cabecera o un archivo fuente real que puedes abrir.

No todas las entradas utilizan todas las etiquetas; la estructura es una guía, no una plantilla.

### Lo que este apéndice no es

No es una introducción a la teoría de los sistemas operativos. Si nunca has encontrado antes la idea de un proceso o un espacio de direcciones, coge primero un libro de texto general sobre sistemas operativos y vuelve después. Tampoco es un tutorial de programación de sistemas; los primeros capítulos del libro ya se encargan de eso. Y no se solapa con los demás apéndices. El apéndice A es la referencia de API. El apéndice B es la guía de campo de patrones algorítmicos. El apéndice C es el compañero de conceptos de hardware. El apéndice E es la referencia de subsistemas del kernel. Si la pregunta que quieres responder es «qué hace esta llamada a una macro», «qué estructura de datos debería usar», «qué es un BAR» o «cómo funciona el planificador», lo que buscas está en otro apéndice. Este se centra en el modelo mental del sistema operativo que hace que el trabajo con drivers tenga sentido.

## Orientación para el lector

Hay tres formas de usar este apéndice, cada una con una estrategia de lectura diferente.

Si estás **aprendiendo los capítulos principales**, mantén este apéndice abierto como compañero. Cuando el capítulo 3 trate la división UNIX entre el kernel y el userland, echa un vistazo a la sección 1 de este apéndice para el resumen relevante para los drivers. Cuando el capítulo 6 introduzca Newbus y el ciclo de vida del driver, la sección 2 comprime la misma historia en un modelo mental de una página. Cuando el capítulo 11 te guíe por tu primer mutex, la sección 3 da nombre a la familia. Una primera lectura completa del apéndice debería llevar unos veinticinco minutos; el uso cotidiano se parece más a dos o tres minutos cada vez.

Si estás **leyendo código del kernel que no conoces**, trata el apéndice como un traductor. El código fuente del kernel da por hecho que el lector ya sabe por qué existe `copyin(9)`, qué es un softc, cuándo un `TAILQ` es mejor que un `LIST` y de dónde vienen las cadenas `SYSINIT`. Si alguna de esas palabras te resulta confusa cuando la encuentras en el código, la entrada correspondiente aquí nombra el concepto en una página y apunta al capítulo que lo enseña en profundidad.

Si **vuelves al libro después de un tiempo de ausencia**, lee el apéndice como una consolidación. Los conceptos de este apéndice son la columna vertebral recurrente del trabajo con drivers. Releerlos es una forma sencilla de recargar el modelo mental antes de abrir un driver desconocido.

A lo largo del apéndice se aplican algunas convenciones:

- Las rutas de código fuente se muestran en la forma orientada al libro, `/usr/src/sys/...`, correspondiente a un sistema FreeBSD estándar. Puedes abrir cualquiera de ellas en tu máquina de laboratorio.
- Las páginas de manual se citan en el estilo habitual de FreeBSD. Las páginas orientadas al kernel están en la sección 9 (`intr_event(9)`, `mtx(9)`, `kld(9)`). Las llamadas al sistema de userland están en la sección 2 (`open(2)`, `ioctl(2)`). Las descripciones de dispositivos están en la sección 4 (`devfs(4)`, `pci(4)`).
- Cuando una entrada señala código fuente real como lectura, el archivo es uno que un principiante puede recorrer en una sola sesión.

Con ese marco en su lugar, empezamos donde todo driver debe empezar: entendiendo en qué lado de la frontera kernel/usuario vive.

## Sección 1: ¿Qué es un kernel y qué hace?

Un kernel es el programa que posee la máquina. Es el único programa que se ejecuta en el modo más privilegiado de la CPU, el único que puede acceder directamente a la memoria física, el único que puede comunicarse con el hardware y el único que puede decidir qué otros programas se ejecutan y cuándo. Todo lo demás (shells, compiladores, navegadores web, demonios, tus propios programas) se ejecuta en un modo menos privilegiado y le pide al kernel las cosas que no puede hacer por sí mismo. Un driver es código del kernel. Ese único hecho cambia las reglas que el código debe seguir, y el resto de esta sección explica por qué.

### Responsabilidades del kernel en una página

**Qué es.** El kernel de FreeBSD es responsable de un conjunto pequeño y bien definido de tareas: gestionar la CPU (procesos, threads, planificación, interrupciones), gestionar la memoria (espacios de direcciones virtuales, tablas de páginas, asignación), gestionar la I/O (comunicarse con dispositivos a través de drivers), gestionar archivos y sistemas de archivos (a través de VFS y GEOM) y gestionar la pila de red. También media las fronteras de seguridad, gestiona las señales y es el único punto de entrada para toda llamada al sistema.

**Por qué importa a los desarrolladores de drivers.** Un driver no es un programa independiente. Es un participante en cada una de esas tareas. Un driver de red participa en el subsistema de red. Un driver de almacenamiento participa en los subsistemas de I/O y sistema de archivos. Un driver de sensor participa en el subsistema de I/O y a veces en el subsistema de gestión de eventos. Cuando escribes un driver, estás extendiendo el kernel, no situándote a su lado. Los invariantes del kernel se convierten en tus invariantes, y toda convención de la que el kernel depende (disciplina de locking, disciplina de memoria, convenciones de retorno de error, orden de limpieza) se convierte en una convención que debes respetar.

**Cómo aparece esto en FreeBSD.** El árbol de código fuente del kernel en `/usr/src/sys/` está organizado por responsabilidad: `/usr/src/sys/kern/` contiene el núcleo independiente de la arquitectura (gestión de procesos, planificación, locking, VFS), `/usr/src/sys/vm/` contiene el sistema de memoria virtual, `/usr/src/sys/dev/` contiene los drivers de dispositivos, `/usr/src/sys/net/` y `/usr/src/sys/netinet/` contienen la pila de red, y `/usr/src/sys/amd64/` (o `arm64/`, `i386/`, `riscv/`) contiene el código dependiente de la arquitectura. Cuando lees un driver, estás leyendo código que une piezas de varios de estos directorios.

**Trampa habitual.** Tratar el kernel como una biblioteca pasiva a la que los drivers «llaman». El kernel es el programa en ejecución; tu driver vive dentro de él. Un crash, una pérdida de memoria o un lock retenido en tu driver es un crash, una pérdida de memoria o un lock retenido para todo el sistema.

**Dónde lo enseña el libro.** El capítulo 3 introduce la visión de UNIX y FreeBSD. El capítulo 5 profundiza en la distinción al nivel del C en espacio del kernel. El capítulo 6 la fundamenta en la anatomía del driver.

**Qué leer a continuación.** `intro(9)` para una visión general del kernel, y el listado de directorios de `/usr/src/sys/`. Dedica cinco minutos a leer los nombres de las carpetas y ya tendrás un mapa aproximado.

### Espacio del kernel frente al espacio de usuario

**Qué es.** Dos entornos de ejecución distintos dentro de la misma máquina. El espacio del kernel se ejecuta con todos los privilegios de la CPU, comparte un único espacio de direcciones entre todos los threads del kernel y tiene acceso directo al hardware. El espacio de usuario se ejecuta con privilegios reducidos, vive en espacios de direcciones virtuales por proceso que el kernel establece y protege, y no tiene acceso directo al hardware. Una sola máquina siempre ejecuta código de ambos entornos; la CPU cambia entre ellos muchas veces por segundo.

**Por qué importa a los desarrolladores de drivers.** Casi todas las reglas que hacen que la programación del kernel se sienta diferente a la programación de usuario provienen de esta separación. Un puntero del kernel no es direccionable desde el espacio de usuario, y un puntero de usuario no es direccionable de forma segura desde el espacio del kernel. La memoria de usuario puede ser intercambiada a disco, paginada o desmapeada en cualquier momento; la memoria del kernel es estable dentro de su vida útil de asignación. El kernel nunca debe desreferenciar ciegamente un puntero proveniente del userland; siempre debe usar las primitivas de copia dedicadas. Y como un único espacio de direcciones del kernel es compartido por todos los threads del kernel, cualquier bug del kernel que machaque la memoria puede corromper todos los subsistemas a la vez.

**Cómo aparece esto en FreeBSD.** Hay tres lugares en el código del driver que son visibles cada vez que se cruza la frontera:

- `copyin(9)` y `copyout(9)` mueven datos de tamaño fijo entre un buffer de usuario y un buffer del kernel, devolviendo un error en caso de dirección de usuario inválida en lugar de hacer panic.
- `copyinstr(9)` hace lo mismo para cadenas terminadas en NUL con un límite de longitud proporcionado por el llamador.
- `uiomove(9)` recorre un `struct uio` (descriptor de I/O de usuario) en cualquier dirección de una llamada al sistema `read` o `write`, gestionando la lista de buffers y el indicador de dirección por ti.

Por debajo, cada una de estas rutinas utiliza helpers dependientes de la arquitectura que saben cómo capturar un fallo por una dirección de usuario inválida y convertirlo en un retorno `EFAULT` en lugar de un kernel panic.

**Trampa habitual.** Desreferenciar directamente un puntero de usuario dentro de un manejador de syscall o de una implementación de `ioctl`. El código puede parecer funcionar cuando el usuario pasa un puntero válido y la página resulta estar residente. Hará panic o se comportará mal de forma silenciosa en el momento en que el usuario pase algo incorrecto (ya sea por error o deliberadamente), y ese es uno de los bugs más fáciles de convertir en un problema de seguridad.

**Dónde lo enseña el libro.** El capítulo 3 establece la distinción. El capítulo 5 la aborda en el C del kernel. El capítulo 9 recorre la frontera en el código del driver de caracteres.

**Qué leer a continuación.** `copyin(9)`, `copyout(9)`, `uiomove(9)`, y la cabecera `/usr/src/sys/sys/uio.h` para `struct uio`.

### Las llamadas al sistema: la puerta entre los dos lados

**Qué es.** Una llamada al sistema es el mecanismo disciplinado que usa un programa del espacio de usuario para pedirle al kernel que haga algo en su nombre. El programa de usuario invoca una instrucción especial (`syscall` en amd64, `svc` en arm64, con trampas equivalentes en todas las demás arquitecturas que FreeBSD admite), lo que hace que la CPU entre en modo kernel a través de una trampa, cambie a la pila del kernel del thread actual y despache a la implementación que el kernel ha registrado para ese número de llamada al sistema.

**Por qué importa a los desarrolladores de drivers.** La mayor parte de la interacción de un driver con el userland ocurre porque un programa del userland ha emitido una llamada al sistema. Cuando el usuario ejecuta `cat /dev/mydevice`, `cat(1)` invoca `open(2)`, `read(2)` y `close(2)` sobre ese nodo de dispositivo. Cada una de esas llamadas al sistema llega finalmente a tu driver a través de la tabla de despacho `cdev` que registraste con `make_dev_s(9)`: `open(2)` se convierte en `d_open`, `read(2)` se convierte en `d_read`, `ioctl(2)` se convierte en `d_ioctl`. Los puntos de entrada de tu driver son, en el fondo, puntos de entrada de llamadas al sistema con otro nombre, que es exactamente por qué las primitivas de copia mencionadas antes importan tanto.

**Cómo se manifiesta esto en FreeBSD.** Dos piezas. Primero, la tabla de llamadas al sistema en sí y la ruta de gestión de traps, que residen en el núcleo independiente de la arquitectura y en los backends específicos de cada arquitectura. Segundo, las estructuras visibles para el driver que una llamada al sistema te entrega: un `struct thread *` para el thread que realiza la llamada, un `struct uio *` para la lista de buffers en `read`/`write`, un puntero de argumento para `ioctl`, y un conjunto de flags que describen el modo de la llamada. El driver no ve el trap; ve una llamada a función que llega ya en contexto del kernel.

**Error frecuente.** Asumir que el thread que realiza la llamada puede tratarse como un thread del kernel ordinario. Un thread que ha entrado al kernel a través de una llamada al sistema está ejecutando temporalmente código del kernel en nombre de un proceso de usuario. Puede ser eliminado si el proceso es eliminado, su prioridad puede diferir de la de un thread del kernel interno, y normalmente es el thread equivocado para encargarse de trabajo de larga duración. El trabajo de larga duración pertenece a un taskqueue o a un thread del kernel propiedad del driver.

**Dónde lo enseña el libro.** El capítulo 3 presenta el mecanismo. El capítulo 9 lo ejercita en una ruta de lectura/escritura funcional.

**Qué leer a continuación.** `syscall(2)` para la perspectiva desde el userland, `uiomove(9)` para la forma de entrada al kernel, y `/usr/src/sys/kern/syscalls.master` si tienes curiosidad sobre la tabla de numeración real de las llamadas al sistema.

### Por qué los errores del kernel tienen consecuencias en todo el sistema

**Qué es.** Una forma concisa de expresar lo que implican las entradas anteriores. Un thread del kernel puede leer y escribir cualquier memoria del kernel; no existe un espacio de direcciones por módulo dentro del kernel. Un error del kernel puede, por tanto, dañar cualquier estructura de datos, incluidas las estructuras de datos que pertenecen a otros subsistemas. Un crash del kernel derriba toda la máquina. Un deadlock del kernel puede bloquear threads en todos los drivers a la vez.

**Por qué esto importa a los desarrolladores de drivers.** El radio de acción de un error del kernel abarca todo el sistema. Por eso el libro repite las mismas disciplinas en cada capítulo: pares equilibrados de asignación/liberación, pares equilibrados de adquisición/liberación de lock, escaleras de limpieza coherentes ante errores, validación defensiva de cualquier puntero que provenga del userland, y ordenación cuidadosa en los límites (interrupciones, DMA, registros de dispositivo). Ninguna de estas disciplinas tiene que ver con heroísmos, sino con reconocer que el coste de un error del kernel lo pagan todos los procesos de la máquina, no solo el driver que lo cometió.

**Cómo se manifiesta esto en FreeBSD.** Directamente, en las herramientas que el kernel te ofrece para detectar errores antes de que se propaguen. `witness(9)` verifica el orden de los lock. `KASSERT(9)` te permite codificar invariantes que desaparecen al compilar en producción pero que se activan con claridad en un build de desarrollo. `INVARIANTS` e `INVARIANT_SUPPORT` en la configuración del kernel activan comprobaciones adicionales. DTrace te permite observar el comportamiento del driver bajo carga real. Tu entorno de laboratorio de desarrollo siempre debería ejecutar un kernel con estos diagnósticos activados; el destino de producción no.

**Error habitual.** Probar únicamente sobre un kernel de build de lanzamiento y descubrir los errores en producción. El kernel de desarrollo es el que detecta el problema a bajo coste. Úsalo.

**Dónde lo enseña el libro.** El capítulo 2 te guía a través de la construcción de un kernel de laboratorio con diagnósticos activados. El capítulo 5 concreta la disciplina a nivel de C. Todos los capítulos a partir del capítulo 11 dependen de estos hábitos.

**Qué leer a continuación.** `witness(9)`, `kassert(9)`, `ddb(4)`, y las opciones de depuración del kernel en `/usr/src/sys/conf/NOTES`.

### Cómo participan los drivers en las responsabilidades del kernel

Las entradas anteriores describen el kernel como una entidad abstracta. El papel que desempeña un driver dentro de ese kernel es concreto y limitado. Un driver es el componente que hace utilizable un dispositivo de hardware concreto, o un pseudo-dispositivo concreto, a través de los subsistemas existentes del kernel. Un driver de almacenamiento expone un disco a través de GEOM y el buffer cache para que los sistemas de archivos puedan utilizarlo. Un driver de red expone una NIC a través de `ifnet` para que la pila de red pueda utilizarla. Un driver de caracteres expone un nodo de dispositivo en `/dev` para que los programas del userland puedan usarlo mediante llamadas al sistema de archivos ordinarias. Los drivers no inventan interfaces; implementan las que el kernel ya publica.

Vale la pena tener presente ese marco mientras lees un driver nuevo. Identificar los dos extremos, el extremo de hardware (registros, interrupciones, DMA) y el extremo del subsistema (`ifnet`, `cdev`, el proveedor GEOM), es la mejor forma de orientarse en un driver desconocido. La sección 2 perfila con más detalle el extremo del subsistema.

## Sección 2: ¿Qué es un driver de dispositivo?

Un driver de dispositivo es la pieza de código del kernel que hace que un dispositivo concreto, real o virtual, sea utilizable a través de las interfaces habituales del sistema operativo. Vive en dos frentes al mismo tiempo. Por un lado habla el lenguaje del hardware: accesos a registros, interrupciones, descriptores DMA, secuencias de protocolo. Por el otro habla una de las interfaces de subsistema estándar de FreeBSD: `cdevsw` para dispositivos de caracteres, `ifnet` para interfaces de red, GEOM para almacenamiento. La tarea del driver es traducir entre esos dos lados, de forma correcta y segura, durante toda la vida útil del dispositivo.

Esta sección nombra los tipos de driver que trata el libro, el papel que desempeña el driver en el camino del hardware al userland, y los mecanismos por los cuales FreeBSD carga un driver y lo vincula a un dispositivo.

### Tipos de driver: de caracteres, de bloques, de red y pseudo

**Qué es.** Una clasificación según la interfaz de subsistema que publica el driver. El vocabulario es anterior a FreeBSD y los nombres históricos de las categorías siguen apareciendo, pero en el FreeBSD moderno los límites exactos difieren de lo que describe un libro de texto clásico.

- Un **driver de dispositivo de caracteres** expone el hardware como un flujo a través de un nodo `cdev` en `/dev`. Registra un `struct cdevsw` con puntos de entrada como `d_open`, `d_close`, `d_read`, `d_write` y `d_ioctl`, y el kernel enruta las llamadas al sistema correspondientes hacia esos puntos de entrada. Los pseudo-dispositivos (drivers sin hardware real detrás, como `/dev/null` o `/dev/random`) también son dispositivos de caracteres.
- Un **driver de dispositivo de bloques**, en el sentido histórico de UNIX, exponía un disco de tamaño fijo a través de un caché orientado a bloques. En el FreeBSD moderno, los dispositivos similares a discos se presentan a través de GEOM, no a través de un `cdevsw` de bloques separado; las entradas en `/dev` para un disco (`/dev/ada0`, `/dev/da0`) siguen siendo nodos `cdev`, pero su sustancia es un proveedor GEOM, y los sistemas de archivos se comunican con ellos a través de la capa BIO en lugar de mediante read/write directas.
- Un **driver de red** expone una interfaz de red a través de `ifnet`. No utiliza `cdevsw` en absoluto; se registra en la pila de red, recibe paquetes como cadenas de `struct mbuf`, envía paquetes a través de las colas de transmisión de la pila y participa en los eventos de estado de enlace.
- Un **driver de pseudo-dispositivo** es cualquier driver sin hardware real detrás. Puede ser un dispositivo de caracteres (`/dev/null`, `/dev/random`), un dispositivo de red (`tun`, `tap`, `lo`) o un proveedor de almacenamiento (`md`, `gmirror`). La palabra "pseudo" se refiere únicamente a la ausencia de hardware; la integración en el subsistema es la misma que para un dispositivo real.

**Por qué esto importa a los desarrolladores de drivers.** El tipo de driver que estás escribiendo determina la interfaz que debes implementar. Un driver de dispositivo de caracteres invierte su tiempo en `d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl`. Un driver de red invierte su tiempo en `if_transmit`, `if_input` y las rutas de recepción dirigidas por interrupciones. Un driver de almacenamiento invierte su tiempo en las rutinas de inicio de GEOM y en la finalización de BIO. Elegir el tipo correcto desde el principio evita una reescritura; elegir el incorrecto es un error costoso.

**Cómo se manifiesta esto en FreeBSD.** Tres conjuntos separados de cabeceras y partes del árbol de código fuente gestionan los tres tipos principales:

- Los drivers de caracteres se registran a través de `/usr/src/sys/sys/conf.h` (`struct cdevsw`, `make_dev_s`) y están implementados en todo `/usr/src/sys/dev/`.
- Los drivers de red se registran a través de `/usr/src/sys/net/if.h` (`struct ifnet`) y tienen su integración en la pila en `/usr/src/sys/net/` y `/usr/src/sys/netinet/`.
- Los drivers de almacenamiento se registran a través del framework GEOM en `/usr/src/sys/geom/` y utilizan `struct bio` para las peticiones de I/O.

**Error habitual.** Recurrir a `cdevsw` para exponer un disco. La intuición de "todo es un archivo" está a medias en lo correcto, pero un disco en el FreeBSD moderno es un proveedor GEOM; una interfaz `cdevsw` directa omite el caché, la capa de particionado y las transformaciones que espera el resto del sistema.

**Dónde lo enseña el libro.** El capítulo 6 nombra las categorías y las sitúa dentro de Newbus. El capítulo 7 escribe el primer driver de caracteres desde cero. El capítulo 27 cubre el almacenamiento y GEOM. El capítulo 28 cubre los drivers de red.

**Qué leer a continuación.** `cdevsw(9)`, `ifnet(9)`, y un pseudo-dispositivo compacto como `/usr/src/sys/dev/null/null.c`. Lee el driver `null` de una sola vez; es el ejemplo más limpio de un driver de caracteres completo en el árbol.

### El papel del driver en la comunicación con el hardware

**Qué es.** El driver es el único código del kernel que se comunica con su hardware. Nada más en el kernel accede a los registros del dispositivo, arma sus interrupciones o encola sus transferencias DMA. Todos los demás subsistemas acceden al dispositivo a través del driver.

**Por qué esto importa a los desarrolladores de drivers.** Esta exclusividad es la razón por la que el driver debe ser fiable. La pila de red no puede verificar si un paquete fue realmente transmitido; confía en el driver de red. El sistema de archivos no puede verificar si un bloque llegó realmente al disco; confía en el driver de almacenamiento. Cuando el driver miente (completando una transferencia antes de que el hardware haya terminado, por ejemplo), el resto del sistema cree la mentira y continúa. La mayoría de los fallos catastróficos en sistemas reales provienen de esta clase de deshonestidad sutil del driver, no de crashes directos.

**Cómo se manifiesta esto en FreeBSD.** En la forma de las funciones públicas del driver. El `d_read` de un driver de caracteres retorna solo después de que los datos estén realmente en el buffer del usuario o haya ocurrido un error. El `if_transmit` de un driver de red indica a la pila si el paquete pertenece ahora al driver o todavía a la pila. El `g_start` de un driver de almacenamiento asume la responsabilidad de completar el `bio` a través de `biodone(9)` en algún momento. Cada uno de esos contratos es una promesa que el driver hace en nombre del hardware.

**Error habitual.** Retornar antes de tiempo de una ruta de finalización porque "el hardware generalmente funciona". Un buen día, nada falla. Un mal día, la pila cree que se envió un paquete que no se envió, un sistema de archivos cree que se escribió un bloque que no se escribió, y la corrupción resultante es imposible de rastrear hasta su origen.

**Dónde lo enseña el libro.** El capítulo 6 establece el contrato. El capítulo 19 lo hace riguroso para la finalización dirigida por interrupciones. El capítulo 21 hace lo mismo para DMA.

**Qué leer a continuación.** `biodone(9)`, y la ruta de transmisión/recepción de un driver NIC pequeño.

### Cómo se cargan los drivers y se vinculan a los dispositivos

**Qué es.** Los drivers de FreeBSD llegan a un kernel en ejecución de una de dos maneras. O bien están compilados en el binario del kernel (drivers integrados, activados durante el boot), o bien se construyen como módulos cargables del kernel (archivos `.ko`) que el administrador carga en tiempo de ejecución con `kldload(8)`. Una vez que un driver está presente en el kernel, Newbus le ofrece los dispositivos que reclama.

La danza del enlace es el ciclo *probe/attach*. Cada bus enumera sus hijos en el boot o al conectar en caliente y pregunta a cada driver registrado si puede gestionar cada hijo. La función `probe` del driver examina el hijo (ID de fabricante/dispositivo para PCI, VID/PID para USB, cadena de compatibilidad FDT para sistemas embebidos, un registro de clase para buses genéricos) y devuelve un valor de prioridad que indica su nivel de confianza. El bus elige la coincidencia de mayor prioridad y llama a la función `attach` de ese driver. A partir de ese momento, el dispositivo pertenece al driver, y el softc del driver mantiene el estado del dispositivo.

**Por qué esto importa a los desarrolladores de drivers.** Todos los drivers de FreeBSD terminan con un bloque corto de código que los registra para esta danza. Ese bloque es el contrato entre el driver y el resto del kernel. Si lo haces mal, el driver nunca llega a hacer el attach; si lo haces bien, el kernel realizará el attach, el detach, el suspend, el resume y la descarga de tu driver en los momentos que elija.

**Cómo se manifiesta esto en FreeBSD.** Cinco piezas, todas compactas:

- Un array `device_method_t` que enumera las implementaciones del driver de los métodos de dispositivo estándar, terminando con `DEVMETHOD_END`.
- Una estructura `driver_t` que vincula el nombre del driver, la tabla de métodos y el tamaño del softc.
- Una invocación `DRIVER_MODULE(name, busname, driver, evh, arg)` al final del archivo, que registra el driver con el bus indicado. Los campos `evh` y `arg` pasan un manejador de eventos opcional; la mayoría de los drivers los dejan como `0, 0`.
- Una invocación `MODULE_VERSION(name, version)` para anunciar la versión ABI del módulo.
- Líneas opcionales `MODULE_DEPEND(name, other, min, pref, max)` si el driver depende de otro módulo.

Estas residen en `/usr/src/sys/sys/module.h` y `/usr/src/sys/sys/bus.h`. La macro `DEVMETHOD` engancha las funciones `probe`, `attach`, `detach`, y las opcionales `suspend`/`resume`/`shutdown` del driver en el despachador de métodos kobj que Newbus utiliza internamente.

**Trampa habitual.** Olvidar `MODULE_VERSION`. El módulo puede cargar y funcionar en tu máquina y luego fallar al cargarse en la de otra persona porque el cargador del kernel no tiene forma de verificar la compatibilidad ABI. La declaración de versión no es ceremonial; es la manera en que el cargador de módulos mantiene la compatibilidad entre componentes.

**Dónde lo enseña el libro.** El Capítulo 6 introduce el árbol Newbus. El Capítulo 7 escribe el boilerplate completo de principio a fin y utiliza `kldload(8)` para ponerlo a prueba.

**Qué leer a continuación.** `DRIVER_MODULE(9)`, `MODULE_VERSION(9)`, `kld(9)`, `kldload(8)` y el compacto `/usr/src/sys/dev/null/null.c` como ejemplo completo.

### Conectar hardware, subsistemas del kernel y userland

**Qué es.** Una forma concisa de describir qué hace un driver visto desde arriba. El hardware está a un lado. El userland está al otro. En medio se encuentran los subsistemas del kernel (VFS, GEOM, `ifnet`, `cdev`). El driver es el componente que permite que los bytes fluyan a través de ese sandwich.

Una imagen aproximada, en un sentido:

```text
userland                kernel subsystems        driver             hardware
--------                ------------------       ------             --------
cat(1)                  -> open(2) / read(2)
                        -> cdev switch                              ADC, NIC,
                           (d_read)            -> driver entry      disk, etc.
                                                   point
                                                -> register read,
                                                   MMIO, DMA
                                                                    -> bytes

                                                <- sync, interrupt,
                                                   completion
                        <- mbuf chain / bio /
                           uiomove back
                        <- read(2) returns
```

La imagen no es código literal. Es la forma de la responsabilidad. Todo driver encaja en alguna variante de ella.

**Por qué esto importa a quien escribe drivers.** Cuando no tengas claro a qué capa pertenece un fragmento de código, pregúntate qué capa del sandwich es responsable de esa tarea en concreto. La validación de un puntero de usuario pertenece al límite del subsistema. Un acceso a registro pertenece al driver. Una actualización de estado por dispositivo pertenece al softc. Una vez que la capa está clara, los nombres y las primitivas se deducen solos.

**Dónde lo enseña el libro.** El capítulo 6 dibuja el esquema para los drivers de caracteres. El capítulo 27 lo redibuja para GEOM. El capítulo 28 lo redibuja para `ifnet`. La forma siempre resulta reconocible.

**Qué leer a continuación.** La cabecera de `/usr/src/sys/dev/null/null.c` para la variante de caracteres mínima, y la cabecera de `/usr/src/sys/net/if_tuntap.c` para la variante de red mínima.

## Sección 3: Estructuras de datos del kernel que verás con frecuencia

Ciertas formas aparecen en casi todos los drivers de FreeBSD. Una lista enlazada de peticiones pendientes. Un buffer que atraviesa el límite kernel/usuario. Un lock alrededor del softc. Una variable de condición esperada desde un thread y señalizada desde otro. Ninguna de ellas es complicada por sí sola, pero encontrarlas por primera vez dentro de código real es donde el modelo mental suele romperse. Esta sección nombra las familias, explica qué papel desempeñan y te remite al capítulo que las enseña en profundidad. El apéndice A contiene las APIs detalladas; el apéndice B, los patrones algorítmicos. Esta sección es la orientación de una sola página.

### Listas y colas de `<sys/queue.h>`

**Qué es.** Una familia de macros de listas intrusivas que vive únicamente en una cabecera. Incrusta un `TAILQ_ENTRY`, `LIST_ENTRY` o `STAILQ_ENTRY` dentro de tu estructura de elemento, define una cabeza, y las macros te proporcionan inserción, eliminación y recorrido sin ninguna asignación en el heap para los nodos de la lista. Existen cuatro variantes: `SLIST` (enlace simple), `LIST` (enlace doble, inserción por la cabeza), `STAILQ` (enlace simple con inserción rápida por la cola) y `TAILQ` (enlace doble con inserción por la cola y eliminación arbitraria en O(1)).

**Por qué esto importa a quien escribe drivers.** Casi todas las colecciones por driver en el árbol de código fuente son una de estas. Comandos pendientes, identificadores de archivo abiertos, callbacks registrados, peticiones `bio` en cola. Elegir la variante adecuada para el patrón de acceso produce código corto, predecible y protegible con un único mutex. Elegir la equivocada genera una contabilidad de punteros innecesaria o una eliminación O(n) inesperada.

**Cómo aparece en FreeBSD.** Las macros viven en `/usr/src/sys/sys/queue.h`. Un softc suele tener sus cabezas de cola como campos ordinarios. Cada recorrido está protegido por el lock del driver. Las variantes seguras frente a eliminaciones (`TAILQ_FOREACH_SAFE`, `LIST_FOREACH_SAFE`) son las que debes usar siempre que el cuerpo del bucle pueda desconectar el elemento actual.

**Trampa habitual.** Usar un `LIST_FOREACH` simple mientras se liberan elementos dentro del cuerpo del bucle. El iterador desreferencia el puntero de enlace del elemento después de que ya lo hayas liberado. Cambia a la variante `_SAFE` o reorganiza el bucle.

**Dónde lo enseña el libro.** El capítulo 5 presenta las macros en el contexto de C para el kernel. El capítulo 11 las usa junto con locking. El apéndice B ofrece el tratamiento completo orientado a patrones.

**Qué leer a continuación.** `queue(3)`, `/usr/src/sys/sys/queue.h`, y cualquier softc de driver que tenga una lista de peticiones en vuelo.

### Buffers del kernel: `mbuf`, `buf` y `bio`

**Qué es.** Tres representaciones de buffer que aparecen en subsistemas concretos. `struct mbuf` es el tipo de representación de paquetes de red: una lista enlazada de unidades pequeñas de tamaño fijo que juntas contienen un paquete y sus metadatos. `struct buf` es la unidad de caché de buffer de almacenamiento, utilizada por el VFS y la capa de bloques clásica. `struct bio` es la estructura de petición de I/O de GEOM, la unidad moderna de I/O de almacenamiento que un driver de almacenamiento completa con `biodone(9)`.

**Por qué esto importa a quien escribe drivers.** No vas a encontrar los tres a la vez. Un driver de red piensa en cadenas de `mbuf`. Un driver de almacenamiento piensa en peticiones `bio` y, en la capa VFS, en entradas `buf`. Un driver de caracteres raramente ve ninguno de ellos; usa `struct uio` en su lugar. Saber qué tipo de buffer usa tu subsistema te indica qué convención de finalización aplica y qué cabeceras incluir. Mezclarlos es una señal de que el tipo de driver es incorrecto o de que se está violando un límite de capa.

**Cómo aparece en FreeBSD.** `struct mbuf` está definido en `/usr/src/sys/sys/mbuf.h`. `struct buf` está definido en `/usr/src/sys/sys/buf.h`. `struct bio` está definido en `/usr/src/sys/sys/bio.h`. Cada uno tiene sus propios rituales de asignación, conteo de referencias y finalización; el driver nunca inventa un sustituto.

**Trampa habitual.** Asignar un buffer de bytes plano para la transmisión de red porque parece más sencillo. La pila de red te entrega un `mbuf`; la pila de red espera recibir un `mbuf` de vuelta. Convertir entre el tipo correcto y cualquier otro es casi siempre un error, y rompe las rutas rápidas de copia cero en las que la pila se basa.

**Dónde lo enseña el libro.** El capítulo 27 presenta `bio` en el contexto de almacenamiento. El capítulo 28 presenta `mbuf` en el contexto de red. El apéndice E tendrá entradas más detalladas para cada uno.

**Qué leer a continuación.** `mbuf(9)`, `bio(9)`, `buf(9)`.

### Locking: mutex, spin mutex, sleep lock y variable de condición

**Qué es.** Una familia de primitivas del kernel para ordenar el acceso a datos compartidos. Cuatro tipos son relevantes:

- Un **mutex por defecto** (`mtx(9)` con `MTX_DEF`) es el mutex cotidiano con capacidad de sleep. Un thread que no puede adquirirlo inmediatamente se duerme hasta que el propietario lo libera. Es la elección por defecto correcta para casi todos los campos del softc.
- Un **spin mutex** (`mtx(9)` con `MTX_SPIN`) es un lock de espera activa seguro frente a interrupciones. Un thread que no puede adquirirlo inmediatamente hace spin. Los spin mutexes se usan en el pequeño conjunto de contextos donde dormir está prohibido (filtros de interrupción y algunas rutas del planificador). No son una optimización de rendimiento; son una herramienta de corrección para un caso muy concreto.
- Un **lock compartido/exclusivo con sleep** (`sx(9)`) es la elección correcta para secciones críticas largas de lectura predominante que pueden dormir mientras mantienen el lock. Lo utiliza el código que realiza operaciones en las que el kernel podría bloquearse (por ejemplo, asignar memoria con `M_WAITOK` mientras se mantiene el lock).
- Una **variable de condición** (`cv(9)`) es una primitiva de sincronización para esperar a que se cumpla un predicado. Un thread mantiene un mutex, comprueba una condición y, si la condición es falsa, llama a `cv_wait`, que de forma atómica libera el mutex, se duerme y vuelve a adquirir el mutex al despertar. Otro thread cambia el predicado bajo el mismo mutex y llama a `cv_signal` o `cv_broadcast`. Una variable de condición nunca existe sin un mutex asociado.

**Por qué esto importa a quien escribe drivers.** Todo driver no trivial mantiene estado compartido: el propio softc, una lista de peticiones en vuelo, un contador de I/O pendiente, un recuento de referencias. Proteger ese estado correctamente es la diferencia entre un driver que funciona bajo carga y uno que falla de forma intermitente de maneras que nadie puede reproducir. Las primitivas de locking de FreeBSD son las herramientas a las que recurres cada vez.

**Cómo aparece en FreeBSD.** Las cabeceras son `/usr/src/sys/sys/mutex.h` para los mutexes normales y spin, `/usr/src/sys/sys/sx.h` para el lock compartido/exclusivo y `/usr/src/sys/sys/condvar.h` para las variables de condición. La convención es almacenar el lock dentro del softc y nombrarlo según el campo del softc: un driver que protege su cola de trabajo suele tener algo como `mtx_init(&sc->sc_lock, device_get_nameunit(dev), NULL, MTX_DEF)` en `attach` y el correspondiente `mtx_destroy` en `detach`.

**Trampa habitual.** Hay dos, ambas frecuentes. La primera es adquirir un sleep mutex desde un filtro de interrupción. Un filtro se ejecuta en un contexto en el que no se puede dormir; un sleep mutex puede bloquearse; el kernel detecta esto con `witness(9)` en una compilación de desarrollo. La segunda es leer el estado que guarda la variable de condición *fuera* del mutex. El patrón mutex-predicado-espera se basa en que el predicado y la espera sean atómicos con respecto al señalizador; romper esa atomicidad reintroduce la condición de carrera que la variable de condición debía eliminar.

**Dónde lo enseña el libro.** El capítulo 11 presenta los mutexes y los spin mutexes. El capítulo 12 añade las variables de condición y los locks compartidos/exclusivos. El apéndice B recoge los patrones de razonamiento.

**Qué leer a continuación.** `mtx(9)`, `sx(9)`, `cv(9)`, `witness(9)`, y la parte superior de la estructura softc de un driver real para ver el lock y sus usuarios en un mismo lugar.

### `softc`: la estructura del driver por dispositivo

**Qué es.** La estructura de estado por dispositivo que el driver define y que el kernel asigna en su nombre. «Softc» es la abreviatura de «software context» (contexto de software). Contiene todo lo que el driver necesita saber sobre una instancia concreta del dispositivo que gestiona: el puntero de retorno `device_t`, el lock, la etiqueta y el identificador de `bus_space` para MMIO, los recursos asignados, cualquier estado en vuelo, contadores y nodos sysctl. Cuando FreeBSD asocia un driver a un dispositivo, asigna un softc del tamaño que el driver declaró en su `driver_t` y entrega al driver un puntero a él a través de `device_get_softc(9)`.

**Por qué esto importa a quien escribe drivers.** Todas las funciones del driver acceden al softc de la misma manera: `struct mydev_softc *sc = device_get_softc(dev)`. Todo lo específico de ese dispositivo concreto vive detrás de ese puntero. Un driver que se asocia a tres dispositivos obtiene tres softcs independientes, cada uno con su propio lock, su propio estado y su propia vista del hardware. Mantener el softc como el único propietario del estado por dispositivo es lo que hace que el código sea reentrante entre múltiples dispositivos.

**Cómo aparece en FreeBSD.** El patrón es uniforme en todo el árbol de código fuente. En el driver:

- Una definición de `struct mydev_softc` al principio del archivo.
- `.size = sizeof(struct mydev_softc)` en el inicializador de `driver_t`.
- `struct mydev_softc *sc = device_get_softc(dev)` al inicio de cada función que recibe un `device_t`.
- Un campo `sc->sc_lock` inicializado en `attach` y destruido en `detach`.

El kernel pone el softc a cero antes de entregarlo al driver, por lo que una nueva asociación siempre empieza en un estado conocido.

**Trampa habitual.** Guardar estado en variables globales de ámbito de archivo porque «solo hay un dispositivo». Entonces alguien asocia dos, o construye una VM con dos, y el segundo dispositivo corrompe silenciosamente al primero. Todo driver, por sencillo que parezca, mantiene el estado en el softc.

**Dónde lo enseña el libro.** El capítulo 6 presenta el concepto. El capítulo 7 escribe un softc completo y lo usa de principio a fin. Todos los capítulos a partir del capítulo 8 dan el patrón por conocido.

**Qué leer a continuación.** `device_get_softc(9)`, el bloque `softc` al principio de `/usr/src/sys/dev/null/null.c`, y el softc de cualquier driver PCI pequeño.

### Por qué estas estructuras reaparecen continuamente

Las familias anteriores (colas, buffers, locks y el softc) reaparecen porque corresponden a las cuatro preguntas que todo driver acaba teniendo que responder. ¿Qué trabajo pendiente tengo? ¿Qué bytes estoy moviendo? ¿Cómo serializo el acceso a mi propio estado? ¿Dónde guardo el estado de una instancia de mi dispositivo? Una vez que reconoces ese marco, el código de un driver empieza a leerse de un modo muy distinto. Las partes desconocidas son los detalles de un dispositivo o bus concreto; las partes conocidas son las cuatro respuestas, en cuatro formas estándar, usadas por todos los drivers del árbol.

## Sección 4: Compilación del kernel y arranque

Un driver se ejecuta dentro de un kernel. Ese kernel no apareció de la nada: algo tuvo que compilarlo, algo tuvo que cargarlo en memoria, algo tuvo que invocar su punto de entrada y algo tuvo que preparar el entorno en el que la función `attach` de tu driver acaba ejecutándose. Esta sección recorre esa cadena a nivel conceptual, para que cuando un error aparezca en las primeras fases del boot, tengas una imagen mental de lo que el sistema intentaba hacer.

### Descripción general del proceso de arranque

**Qué es.** La secuencia de eventos desde el encendido hasta un kernel de FreeBSD en ejecución con procesos en espacio de usuario. En una máquina x86 típica, la secuencia es aproximadamente la siguiente: firmware (BIOS o UEFI), boot de primera etapa (`boot0` en BIOS, el gestor de arranque UEFI en UEFI), el loader de FreeBSD (`loader(8)`), el punto de entrada al kernel específico de arquitectura (`hammer_time` en amd64, invocado desde ensamblador), el inicio independiente de arquitectura (`mi_startup`), la cadena `SYSINIT` y, finalmente, el primer proceso en espacio de usuario (`/sbin/init`). En ARM64 y otras arquitecturas, el firmware y las partes dependientes de la arquitectura difieren; el inicio del kernel independiente de arquitectura es el mismo.

**Por qué importa esto a los escritores de drivers.** Por dos razones. La primera es que muchos fallos de drivers ocurren durante el procesamiento de `SYSINIT`, antes de que haya una consola en la que imprimir, y necesitas saber en qué punto te encuentras cuando eso ocurre. La segunda es que el orden en que se inicializan los subsistemas determina cuándo el bus de tu driver tiene enumeradores, cuándo están disponibles los asignadores de memoria, cuándo se monta el sistema de archivos raíz y cuándo es seguro comunicarse con el espacio de usuario. Un driver que intenta asignar memoria demasiado pronto fallará; un driver que intenta abrir un archivo antes de que se monte el sistema de archivos raíz también fallará. El mecanismo `SYSINIT` nombra las etapas de forma explícita para que el orden sea visible en el código.

**Cómo se manifiesta esto en FreeBSD.** Cada etapa tiene un archivo que puedes abrir. El boot loader reside bajo `/usr/src/stand/`; el punto de entrada temprano en C para amd64 es `hammer_time` en `/usr/src/sys/amd64/amd64/machdep.c` (invocado desde `/usr/src/sys/amd64/amd64/locore.S`); el inicio independiente de arquitectura es `mi_startup` en `/usr/src/sys/kern/init_main.c`; y `start_init`, en el mismo archivo, es lo que ejecuta el proceso init cuando finalmente es planificado, haciendo exec de `/sbin/init` como PID 1.

**Error frecuente.** Tratar el boot como algo opaco. El mecanismo `SYSINIT` hace que las etapas sean explícitas e inspeccionables, y un driver que entiende su lugar en ellas resulta mucho más fácil de depurar.

**Dónde lo enseña el libro.** El capítulo 2 recorre el boot en un contexto de laboratorio. El capítulo 6 conecta la secuencia de carga de drivers con el panorama de arranque del kernel.

**Qué leer a continuación.** `boot(8)`, `loader(8)`, `/usr/src/sys/kern/init_main.c` y la sección `SYSINIT` de `/usr/src/sys/sys/kernel.h`.

### De encendido a `init`: una cronología compacta

Una visión general de toda la secuencia. Los nombres de la derecha son las funciones o páginas de manual que abrirías para profundizar.

```text
+-----------------------------------------------+-----------------------------+
| Stage                                         | Where to look               |
+-----------------------------------------------+-----------------------------+
| Firmware: BIOS / UEFI starts executing        | (hardware / firmware)       |
+-----------------------------------------------+-----------------------------+
| First-stage boot: boot0 / UEFI boot manager   | /usr/src/stand/              |
+-----------------------------------------------+-----------------------------+
| FreeBSD loader: selects kernel and modules    | loader(8), loader.conf(5)   |
+-----------------------------------------------+-----------------------------+
| Kernel entry (amd64): hammer_time() + locore  | sys/amd64/amd64/machdep.c,  |
|                                               | sys/amd64/amd64/locore.S    |
+-----------------------------------------------+-----------------------------+
| MI startup: mi_startup() drives SYSINITs      | sys/kern/init_main.c        |
+-----------------------------------------------+-----------------------------+
| SYSINIT chain: subsystems init in order       | sys/sys/kernel.h            |
|   SI_SUB_VM, SI_SUB_LOCK, SI_SUB_KLD, ...     |                             |
|   SI_SUB_DRIVERS, SI_SUB_CONFIGURE, ...       |                             |
+-----------------------------------------------+-----------------------------+
| Bus probe and attach: drivers bind to devices | sys/kern/subr_bus.c         |
+-----------------------------------------------+-----------------------------+
| SI_SUB_CREATE_INIT / SI_SUB_KTHREAD_INIT:     | sys/kern/init_main.c        |
|   init proc forked, then made runnable        |                             |
+-----------------------------------------------+-----------------------------+
| start_init() mounts root, execs /sbin/init    | sys/kern/init_main.c,       |
|   (PID 1)                                     | sys/kern/vfs_mountroot.c    |
+-----------------------------------------------+-----------------------------+
| Userland startup: rc(8), daemons, logins      | rc(8), init(8)              |
+-----------------------------------------------+-----------------------------+
```

La tabla es un mapa, no un contrato. Muchos detalles varían según la arquitectura, y los límites entre etapas cambian a medida que el kernel evoluciona. Lo que no cambia es la dirección: entorno pequeño y simple al principio, entorno cada vez más funcional hacia el final, espacio de usuario al término.

### Los boot loaders y los módulos del kernel en el momento del arranque

**Qué es.** El loader de FreeBSD (`/boot/loader`) es un pequeño programa al que el código de boot de primera etapa cede el control. Su tarea consiste en leer `/boot/loader.conf`, cargar el kernel (`/boot/kernel/kernel`), cargar los módulos que solicite la configuración, pasar tunables y hints al kernel y transferir el control al punto de entrada del kernel. También proporciona el menú de boot, la posibilidad de arrancar un kernel diferente para recuperación y la capacidad de cargar un módulo antes de que el kernel arranque.

**Por qué importa esto a los escritores de drivers.** Algunos drivers deben estar presentes antes de que se monte el sistema de archivos raíz (los controladores de almacenamiento, por ejemplo). Esos drivers se compilan dentro del kernel o los carga el loader desde `/boot/loader.conf`. Un driver que se carga después de montar el sistema de archivos raíz (mediante `kldload(8)`) no puede manejar el disco que contiene `/`. Conocer esta diferencia forma parte de planificar cuándo debe hacer attach un driver.

**Cómo se manifiesta esto en FreeBSD.** `/boot/loader.conf` es el archivo de texto que enumera los módulos que se cargarán en el arranque. `loader(8)` y `loader.conf(5)` describen el mecanismo. El loader también proporciona mecanismos como los tunables de `kenv(2)` que el kernel puede leer en las primeras etapas de `SYSINIT`.

**Error frecuente.** Depender de variables del loader en tiempo de arranque desde código del kernel que se ejecuta después de montar el sistema de archivos raíz. Esas variables están disponibles; simplemente no son el lugar adecuado para la configuración en tiempo de ejecución. Utiliza tunables de `sysctl(9)` para el tiempo de ejecución y `kenv` únicamente para decisiones de inicialización temprana.

**Dónde lo enseña el libro.** El capítulo 2 utiliza el loader durante la configuración del laboratorio. El capítulo 6 presenta `kldload(8)` para la carga de drivers en tiempo de ejecución.

**Qué leer a continuación.** `loader(8)`, `loader.conf(5)`, `kenv(2)`.

### Los módulos del kernel y el attach de drivers

**Qué es.** Un módulo del kernel es un archivo `.ko` compilado que el kernel puede cargar en tiempo de ejecución (o que el loader puede cargar antes de la entrada al kernel). Los módulos son la forma en que la mayoría de los drivers llegan al kernel durante la operación normal. El framework de módulos gestiona la resolución de símbolos, el versionado (`MODULE_VERSION`), las dependencias (`MODULE_DEPEND`) y el registro de las declaraciones `DRIVER_MODULE` que contenga el archivo.

**Por qué importa esto a los escritores de drivers.** La mayor parte de tu ciclo de desarrollo tendrá este aspecto: editar el código fuente, ejecutar `make`, `kldload ./mydriver.ko`, probar, `kldunload mydriver`, repetir. El framework de módulos hace que ese ciclo sea rápido y seguro, y también simplifica el despliegue en producción (el mismo `.ko` que pruebas es el que se distribuye). El loader y `kldload(8)` en tiempo de ejecución utilizan el mismo formato de módulo.

**Cómo se manifiesta esto en FreeBSD.** Todo archivo de driver termina con `DRIVER_MODULE`, `MODULE_VERSION` y, opcionalmente, `MODULE_DEPEND`. Estas declaraciones registran en el kernel los metadatos de attach al bus del driver y los metadatos a nivel de módulo. Cuando se carga el módulo, el kernel ejecuta el manejador de eventos del módulo para `MOD_LOAD`, que (entre otras cosas) registra el driver en Newbus. Newbus ofrece entonces el driver a cada dispositivo del bus correspondiente, invocando `probe` en cada uno.

**Error frecuente.** Olvidar llamar a `bus_generic_detach`, `bus_release_resource` o `mtx_destroy` desde `detach`. La descarga del módulo tiene éxito, el driver desaparece, pero los recursos quedan sin liberar. Las recargas sucesivas acumulan fugas hasta que algo falla de manera evidente.

**Dónde lo enseña el libro.** El capítulo 6 presenta la relación módulo/driver. El capítulo 7 ejercita el ciclo completo de edición, build, carga, prueba y descarga.

**Qué leer a continuación.** `kld(9)`, `kldload(8)`, `kldstat(8)`, `DRIVER_MODULE(9)`, `MODULE_VERSION(9)`.

### `init_main` y la cadena `SYSINIT`

**Qué es.** `mi_startup` es el punto de entrada independiente de arquitectura al que llama el código de boot específico de cada arquitectura tras el ensamblador inicial (en amd64, `hammer_time` regresa a `locore.S`, que invoca `mi_startup`). Su cuerpo consiste casi en su totalidad en un bucle que recorre una lista ordenada de registros `SYSINIT` y llama a cada uno. Cada registro se inscribe en tiempo de compilación mediante una macro `SYSINIT(...)` y lleva un identificador de subsistema (`SI_SUB_*`) y un orden dentro del subsistema (`SI_ORDER_*`). El enlazador los reúne todos en una única sección; `mi_startup` los ordena y los ejecuta. El proceso init se crea en estado detenido en `SI_SUB_CREATE_INIT`, se hace ejecutable en `SI_SUB_KTHREAD_INIT`, y una vez planificado ejecuta `start_init`, que monta el sistema de archivos raíz y hace exec de `/sbin/init` como PID 1.

**Por qué importa esto a los escritores de drivers.** La mayoría de los drivers nunca toca `SYSINIT` directamente; `DRIVER_MODULE` se encarga de registrarlos correctamente. Sin embargo, algunas partes de la infraestructura (threads del kernel que deben existir antes de que los drivers hagan attach, inicializadores de subsistemas completos, manejadores de eventos tempranos) usan `SYSINIT` directamente, y cuando lees ese código ayuda saber qué está registrando la declaración. Los nombres `SI_SUB_*` hacen que el orden sea auditable, y el espaciado numérico entre ellos deja margen para etapas futuras.

**Cómo se manifiesta esto en FreeBSD.** La macro `SYSINIT` y las constantes `SI_SUB_*` están en `/usr/src/sys/sys/kernel.h`. Las etapas relevantes para los drivers incluyen `SI_SUB_VM` (memoria virtual activa), `SI_SUB_LOCK` (inicialización de locks), `SI_SUB_KLD` (sistema de módulos), `SI_SUB_DRIVERS` (inicialización temprana del subsistema de drivers), `SI_SUB_CONFIGURE` (probe y attach de Newbus), `SI_SUB_ROOT_CONF` (dispositivos raíz candidatos identificados), `SI_SUB_CREATE_INIT` (proceso init bifurcado) y `SI_SUB_KTHREAD_INIT` (init marcado como ejecutable). `mi_startup` se encuentra en `/usr/src/sys/kern/init_main.c` y `start_init` está en el mismo archivo.

**Error frecuente.** Suponer que el `attach` de tu driver se ejecuta muy pronto. Se ejecuta en `SI_SUB_CONFIGURE`, que es después de que los locks, la memoria y la mayoría de los subsistemas ya están activos. Si necesitas coordinarte con algo anterior, utiliza el sistema de manejadores de eventos (`eventhandler(9)`) para engancharte a un evento bien definido, o sitúa el trabajo en la etapa `SYSINIT` correcta.

**Dónde lo enseña el libro.** El capítulo 2 presenta el panorama en tiempo de arranque. El capítulo 6 conecta conceptualmente `DRIVER_MODULE` con la cadena `SYSINIT`. El apéndice E profundiza en el detalle de las etapas de subsistema para los lectores que lo necesiten.

**Qué leer a continuación.** Las definiciones de `SYSINIT` en `/usr/src/sys/sys/kernel.h`, el bucle en `/usr/src/sys/kern/init_main.c` y `/usr/src/sys/kern/subr_bus.c` para la parte de Newbus.

### La compilación del kernel en un párrafo

El kernel de FreeBSD se construye desde el código fuente mediante el sistema `make` estándar, invocado desde `/usr/src`. El build toma un archivo de configuración del kernel (normalmente bajo `/usr/src/sys/amd64/conf/GENERIC` o un archivo personalizado que mantienes junto a él), compila las opciones y entradas de dispositivo seleccionadas y enlaza el resultado en `kernel` más un conjunto de archivos `.ko` para los dispositivos no incluidos estáticamente. Los comandos estándar son `make buildkernel KERNCONF=MYCONF` y `make installkernel KERNCONF=MYCONF`, ejecutados desde `/usr/src`. Para los módulos fuera del árbol de código fuente, el build utiliza `bsd.kmod.mk`, un fragmento de Makefile breve que sabe cómo compilar uno o más archivos `.c` en un archivo `.ko` con los flags correctos del kernel. El capítulo 2 recorre ambos caminos en un contexto de laboratorio; los detalles no se repiten aquí.

## Tablas de referencia rápida

Las tablas compactas que aparecen a continuación están pensadas para una consulta rápida. No sustituyen a las secciones anteriores; simplemente te ayudan a localizar la sección correcta con rapidez.

### Espacio del kernel frente a espacio de usuario de un vistazo

| Propiedad                         | Espacio del kernel                              | Espacio de usuario                              |
| :-------------------------------- | :---------------------------------------------- | :---------------------------------------------- |
| Privilegio                        | Privilegio completo de CPU                      | Reducido, impuesto por la CPU                   |
| Espacio de direcciones            | Uno, compartido por todos los kernel threads    | Uno por proceso, aislado                        |
| Estabilidad de la memoria         | Estable mientras está asignada                  | Puede paginarse, desmapearse o moverse          |
| Acceso directo al hardware        | Sí                                              | No                                              |
| Radio de impacto ante un fallo    | Toda la máquina                                 | Un solo proceso                                 |
| Cómo cruzar la frontera           | Trampa de llamada al sistema                    | Instrucción `syscall` (según la arquitectura)   |
| Primitiva de copia (hacia dentro) | `copyin(9)`, `uiomove(9)`                       | (sin equivalente; el kernel realiza la copia)   |
| Primitiva de copia (hacia fuera)  | `copyout(9)`, `uiomove(9)`                      | (sin equivalente; el kernel realiza la copia)   |

### Comparación de tipos de driver

| Tipo de driver        | Se registra con      | Puntos de entrada principales    | Tipo de buffer principal | Dispositivos típicos       |
| :-------------------- | :------------------- | :------------------------------- | :----------------------- | :------------------------- |
| Caracteres            | `cdevsw` en devfs    | `d_open`, `d_read`, `d_ioctl`    | `struct uio`             | Serie, sensores, `/dev/*`  |
| Red                   | `ifnet`              | `if_transmit`, ruta de recepción | `struct mbuf`            | NICs, `tun`, `tap`         |
| Almacenamiento (GEOM) | Clase/proveedor GEOM | `g_start`, `biodone(9)`          | `struct bio`             | Discos, `md`, `geli`       |
| Pseudo (caracteres)   | `cdevsw` en devfs    | Igual que caracteres             | `struct uio`             | `/dev/null`, `/dev/random` |
| Pseudo (red)          | `ifnet`              | Igual que red                    | `struct mbuf`            | `tun`, `tap`, `lo`         |

### Selección de primitivas de sincronización

| Necesitas...                                                  | Usa                                    |
| :------------------------------------------------------------ | :------------------------------------- |
| Un lock predeterminado para los campos de softc               | `mtx(9)` con `MTX_DEF`                 |
| Un lock seguro para mantener en un filtro de interrupción     | `mtx(9)` con `MTX_SPIN`                |
| Muchos lectores, escritor ocasional, puede bloquearse         | `sx(9)`                                |
| Esperar un predicado bajo un mutex                            | `cv(9)` junto con el mutex             |
| Lectura-modificación-escritura atómica de una sola palabra    | `atomic(9)`                            |

### Punto de control del ciclo de vida de softc

| Fase      | Acciones típicas de softc                                                                              |
| :-------- | :----------------------------------------------------------------------------------------------------- |
| `probe`   | Lee los IDs y devuelve `BUS_PROBE_*`. Aún no hay asignación de softc.                                  |
| `attach`  | `device_get_softc`, inicializa el lock, asigna recursos, configura el estado.                          |
| Runtime   | Todas las operaciones desreferencian `sc`; todas las actualizaciones de estado se realizan bajo lock.  |
| `suspend` | Guarda el estado volátil del hardware, detiene el trabajo pendiente.                                   |
| `resume`  | Restaura el estado, reinicia el trabajo.                                                               |
| `detach`  | Detiene el trabajo, libera recursos en orden inverso, destruye el lock.                                |

### Mapa rápido de boot a init

| Fase                                         | Dato relevante para el driver                                                              |
| :------------------------------------------- | :----------------------------------------------------------------------------------------- |
| Firmware + loader                            | El kernel y los módulos precargados llegan a memoria.                                      |
| Entrada al kernel (`hammer_time` en amd64)   | Configuración inicial dependiente de la máquina; aún no hay drivers.                      |
| Cadena `mi_startup` / `SYSINIT`              | Los subsistemas se inicializan en orden `SI_SUB_*`.                                        |
| `SI_SUB_DRIVERS` / `SI_SUB_CONFIGURE`        | Los buses enumeran; los drivers compilados y precargados ejecutan `probe` / `attach`.      |
| `SI_SUB_ROOT_CONF`                           | Se identifican los dispositivos raíz candidatos.                                           |
| `SI_SUB_CREATE_INIT` / `SI_SUB_KTHREAD_INIT` | El proceso init se bifurca y luego pasa a ser ejecutable.                                  |
| `start_init` se ejecuta                      | Se monta el sistema de archivos raíz; `/sbin/init` se ejecuta como PID 1.                  |
| `kldload(8)` en tiempo de ejecución          | Los drivers adicionales se conectan (attach) a partir de este punto.                       |

## Cerrando: cómo estos conceptos del sistema operativo apoyan el desarrollo de drivers

Cada uno de los conceptos de este apéndice ya se utiliza en algún lugar del libro. El apéndice solo ensambla las piezas para que el lector pueda echar un vistazo al cuadro completo cuando alguna parte resulte confusa.

La frontera kernel/usuario es la razón por la que el código de un driver tiene un aspecto diferente al del C ordinario. Cada regla inusual, desde `copyin(9)` hasta el escueto filtro de interrupción seguro con spinlock, existe porque el kernel opera en un mundo privilegiado que no puede permitirse confiar en el menos privilegiado. Mantén ese marco conceptual y el resto se deduce solo.

Los tipos de driver nombran los contratos con el resto del kernel. Los dispositivos de caracteres, la red y GEOM no son adornos; son las interfaces estándar que permiten que userland y otros subsistemas alcancen tu dispositivo sin necesidad de saber nada de su hardware. Cuando comienzas un nuevo driver, elegir el tipo es la primera decisión arquitectónica que tomas.

Las estructuras de datos recurrentes nombran las formas dentro del driver. Una cola para el trabajo pendiente. Un buffer para los bytes que fluyen por el subsistema. Un mutex alrededor del estado compartido. Un softc que posee todo lo relativo a un dispositivo. Las formas son pequeñas; la disciplina que codifican es lo que mantiene la corrección del driver.

La secuencia de boot nombra el escenario en el que tu driver actúa. El kernel no surgió de la nada; fue ensamblado mediante una cadena de pasos bien definidos, y tu driver se incorpora durante un paso específico en `SI_SUB_CONFIGURE`. Entender el orden de los pasos convierte los errores del boot temprano en simples diagnósticos del kernel, en lugar de dejarlos como misterios sin resolver.

Tres hábitos mantienen estos conceptos activos en lugar de quedarse sin uso.

El primero es preguntarse en qué lado de la frontera te encuentras. Antes de tocar un puntero, pregúntate si es un puntero de usuario, un puntero del kernel o una dirección de bus; la primitiva correcta viene a continuación de forma inmediata.

El segundo es preguntarse con qué subsistema estás hablando. Antes de recurrir a un tipo de buffer, pregúntate si el código está en la ruta de caracteres, de red o de GEOM; el buffer adecuado viene a continuación de forma inmediata.

El tercero es mantener una hoja de referencia rápida para los conceptos que más utilizas. Los archivos bajo `examples/appendices/appendix-d-operating-system-concepts/` están pensados exactamente para eso. Una cheatsheet de kernel frente a espacio de usuario, una comparación de tipos de driver, una lista de verificación del ciclo de vida de softc, una línea de tiempo de boot a init y un diagrama de «dónde encajan los drivers en el OS». La enseñanza está en este apéndice; la aplicación está en esas hojas.

Con eso, el lado del sistema operativo del libro tiene un hogar consolidado. Los capítulos siguen enseñando; este apéndice sigue nombrando; los ejemplos siguen recordando. Cuando un lector cierra este apéndice y abre un driver desconocido, el modelo mental está listo, y cada línea de código del kernel tiene un lugar al que anclarse.
