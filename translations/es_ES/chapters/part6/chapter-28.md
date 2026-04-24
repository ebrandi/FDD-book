---
title: "Escritura de un driver de red"
description: "Desarrollo de drivers de interfaz de red para FreeBSD"
partNumber: 6
partName: "Writing Transport-Specific Drivers"
chapter: 28
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 240
language: "es-ES"
---
# Cómo escribir un driver de red

## Introducción

En el capítulo anterior construiste un driver de almacenamiento. Un sistema de archivos se apoyaba sobre él, un buffer cache le enviaba peticiones BIO, y tu código transportaba bloques de datos hacia una región de RAM y de vuelta. Eso ya suponía alejarse del mundo de los dispositivos de caracteres de los capítulos anteriores, porque un driver de almacenamiento no recibe consultas de un único proceso que posea un descriptor de archivo. Lo dirigen muchas capas superiores, todas cooperando para convertir llamadas `write(2)` en bloques duraderos, y tu driver tenía que situarse en silencio al final de esa cadena y atender cada petición en su turno.

Un driver de red es una tercera especie diferente. No es un flujo de bytes para un único proceso, como un dispositivo de caracteres. No es una superficie de bloques direccionables donde pueda montarse un sistema de archivos, como un dispositivo de almacenamiento. Es una **interfaz**. Se sitúa entre la pila de red de la máquina por un lado y un medio, real o simulado, por el otro. Los paquetes llegan por ese medio y el driver los convierte en mbufs y los entrega hacia arriba a la pila. Los paquetes salen de la pila en forma de mbufs y el driver los convierte en bits en el cable, o en cualquier sustituto del cable que hayas elegido usar. El estado del enlace cambia, y el driver lo notifica. La velocidad del medio cambia, y el driver también lo notifica. El usuario escribe `ifconfig mynet0 up`, y el kernel enruta esa petición a través de `if_ioctl` hasta tu código. El kernel espera una forma concreta de cooperación, no una secuencia particular de lecturas y escrituras.

Este capítulo te enseña esa forma. Aprenderás lo que FreeBSD espera que sea un driver de red. Aprenderás el objeto central que representa una interfaz en el kernel, la estructura llamada `ifnet`, junto con el handle opaco moderno `if_t` que la envuelve. Aprenderás cómo asignar un `ifnet`, cómo registrarlo en la pila, cómo exponerlo como una interfaz con nombre que `ifconfig` pueda ver. Aprenderás cómo los paquetes entran en tu driver a través del callback de transmisión y cómo empujas paquetes en la dirección contraria hacia la pila a través de `if_input`. Aprenderás cómo los mbufs transportan esos paquetes, cómo se notifican el estado del enlace y el estado del medio, cómo se usan los flags como `IFF_UP` e `IFF_DRV_RUNNING`, y cómo un driver se desconecta limpiamente cuando se descarga. Terminarás el capítulo con un driver pseudo-Ethernet funcional llamado `mynet` que podrás cargar, configurar, ejercitar con `ping`, `tcpdump` y `netstat`, y luego descargar sin dejar nada atrás.

El driver que construirás es pequeño a propósito. Los drivers Ethernet reales en el FreeBSD moderno se escriben habitualmente sobre `iflib(9)`, el framework compartido que se encarga de los ring buffers, la moderación de interrupciones y el encaminamiento de paquetes para la mayoría de las NIC de producción. Esa maquinaria es magnífica cuando estás entregando un driver para una tarjeta de 100 gigabits, y volveremos a ella en capítulos posteriores. Pero es demasiado andamiaje que oculta las ideas fundamentales. Para enseñarte lo que es realmente un driver de red, escribiremos la forma clásica, anterior a iflib: un driver `ifnet` simple con su propia función de transmisión y su propia ruta de recepción. Una vez que entiendas eso con claridad, iflib se sentirá como una capa de comodidad sobre algo que ya conoces.

Al igual que el Capítulo 27, este capítulo es largo porque el tema tiene múltiples capas. A diferencia de los drivers `/dev`, los drivers de red vienen envueltos en un vocabulario propio: tramas Ethernet, clonadores de interfaces, estado del enlace, descriptores de medio, `if_transmit`, `if_input`, `bpfattach`, `ether_ifattach`. Presentaremos ese vocabulario con cuidado, un concepto a la vez, y cada concepto lo anclaremos en código del árbol real de FreeBSD. Verás cómo `epair(4)`, `disc(4)` y la pila UFS prestan patrones que podemos adaptar para nuestro propio driver. Al final reconocerás la forma de un driver de red en cualquier archivo fuente de FreeBSD que abras.

El objetivo no es un driver NIC de producción. El objetivo es darte una comprensión completa, honesta y correcta de la capa que existe entre un trozo de hardware y la pila de red de FreeBSD, construida a través de prosa, código y práctica. Una vez que ese modelo mental sea sólido, leer `if_em.c`, `if_bge.c` o `if_ixl.c` se convierte en una cuestión de reconocer patrones y buscar las partes desconocidas. Sin ese modelo mental, parecen una tormenta de macros y operaciones de bits. Con él, parecen un driver más que hace las mismas cosas que tu driver `mynet`, solo que con hardware debajo.

Tómate tu tiempo. Abre una shell de FreeBSD mientras lees. Lleva un cuaderno de laboratorio. Piensa en la pila de red no como una caja negra sobre tu código, sino como un igual que espera un handshake claro y contractual con el driver. Tu trabajo es cumplir ese contrato de forma limpia.

## Guía para el lector: cómo usar este capítulo

Este capítulo continúa el patrón establecido en el Capítulo 27: largo, acumulativo y deliberadamente pausado. El tema es nuevo y el vocabulario es nuevo, así que avanzaremos con un poco más de cuidado de lo habitual en las secciones iniciales antes de dejarte escribir código.

Si eliges el **camino solo de lectura**, planifica unas dos o tres horas de concentración. Saldrás con un modelo mental claro de lo que es un driver de red, cómo encaja en la pila de red de FreeBSD y qué está haciendo el código en los drivers reales. Esta es una forma legítima de usar el capítulo en una primera lectura, y a menudo es la elección correcta en un día en que no tienes tiempo para recompilar un módulo del kernel.

Si eliges el **camino de lectura más laboratorios**, planifica unas cinco a ocho horas distribuidas a lo largo de una o dos tardes. Escribirás, compilarás y cargarás un driver pseudo-Ethernet funcional, lo levantarás con `ifconfig`, verás cómo se mueven sus contadores, le enviarás paquetes con `ping`, los observarás con `tcpdump`, y luego apagarás todo y descargarás el módulo limpiamente. Los laboratorios están diseñados para ser seguros en cualquier sistema FreeBSD 14.3 reciente, incluida una máquina virtual.

Si eliges el **camino de lectura más laboratorios más desafíos**, planifica un fin de semana o un puñado de tardes. Los desafíos extienden el driver en direcciones que importan en la práctica: añadir un compañero de enlace simulado real con una cola compartida entre dos interfaces, soportar diferentes estados de enlace, exponer un sysctl para inyectar errores y medir el comportamiento con `iperf3`. Cada desafío es autocontenido y usa únicamente lo que el capítulo ya ha cubierto.

Independientemente del camino que elijas, no te saltes la sección de resolución de problemas al final. Los drivers de red fallan de unas pocas formas características, y aprender a reconocer esos patrones es más valioso a largo plazo que memorizar los nombres de cada función de `ifnet`. El material de resolución de problemas se coloca al final por legibilidad, pero es posible que vuelvas a él mientras ejecutas los laboratorios.

Una palabra sobre los requisitos previos. Deberías sentirte cómodo con todo lo del Capítulo 26 y el Capítulo 27: escribir un módulo del kernel, asignar y liberar un softc, razonar sobre la ruta de carga y descarga, y probar tu trabajo con `kldload` y `kldunload`. También deberías sentirte suficientemente cómodo con el userland de FreeBSD para ejecutar `ifconfig`, `netstat -in`, `tcpdump` y `ping` sin detenerte a consultar los flags. Si algo de eso te parece inseguro, un repaso rápido de los capítulos anteriores correspondientes te ahorrará tiempo más adelante.

Deberías trabajar en una máquina FreeBSD 14.3 desechable. Una máquina virtual dedicada es la mejor opción, porque los drivers de red, por su naturaleza, pueden interactuar con las tablas de enrutamiento y la lista de interfaces del sistema anfitrión. Una pequeña VM de laboratorio te permite experimentar sin preocuparte de confundir tu sistema principal. Hacer una instantánea antes de empezar es una precaución que sale barata.

### Trabaja sección por sección

El capítulo está organizado como una progresión. La Sección 1 explica qué hace un driver de red y en qué se diferencia de los drivers de caracteres y almacenamiento que ya has escrito. La Sección 2 presenta el objeto `ifnet`, la estructura de datos central de todo el subsistema de red. La Sección 3 recorre la asignación, nomenclatura y registro de una interfaz, incluidos los clonadores de interfaces. La Sección 4 trata la ruta de transmisión, desde `if_transmit` hasta el procesamiento de mbufs. La Sección 5 trata la ruta de recepción, incluidos `if_input` y la generación simulada de paquetes. La Sección 6 cubre los descriptores de medio, los flags de interfaz y las notificaciones de estado del enlace. La Sección 7 te muestra cómo probar el driver con las herramientas de red estándar de FreeBSD. La Sección 8 cierra con la desconexión limpia, la descarga del módulo y consejos de refactorización.

Debes leer estas secciones en orden. Cada una supone que las anteriores están frescas en tu mente, y los laboratorios se apoyan unos en otros. Si saltas al medio, algunas piezas parecerán extrañas.

### Escribe el código

Escribir sigue siendo la forma más efectiva de interiorizar los modismos del kernel. Los archivos complementarios en `examples/part-06/ch28-network-driver/` existen para que puedas verificar tu trabajo, no para que te saltes la escritura. Leer código no es lo mismo que escribirlo, y leer un driver de red es particularmente fácil hacerlo de forma pasiva porque el código a menudo parece una larga sentencia switch. Escribirlo te obliga a pensar en cada rama.

### Abre el árbol de código fuente de FreeBSD

Se te pedirá varias veces que abras archivos fuente reales de FreeBSD, no solo los ejemplos complementarios. Los archivos de interés para este capítulo incluyen `/usr/src/sys/net/if.h`, `/usr/src/sys/net/if_var.h`, `/usr/src/sys/net/if_disc.c`, `/usr/src/sys/net/if_epair.c`, `/usr/src/sys/net/if_ethersubr.c`, `/usr/src/sys/net/if_clone.c`, `/usr/src/sys/net/if_media.h` y `/usr/src/sys/sys/mbuf.h`. Cada uno de ellos es una referencia primaria, y la prosa de este capítulo vuelve a referirse a ellos repetidamente. Si todavía no has clonado ni instalado el árbol de código fuente de la versión 14.3, este es un buen momento para hacerlo.

### Usa tu cuaderno de laboratorio

Mantén abierto el cuaderno de laboratorio que empezaste en el Capítulo 26 mientras trabajas. Querrás registrar la salida de `ifconfig` antes y después de cargar el módulo, los comandos exactos que usas para enviar tráfico, los contadores informados por `netstat -in`, la salida de `tcpdump -i mynet0`, y cualquier advertencia o panic. El trabajo de red es especialmente amigable con los cuadernos porque el mismo comando, `ifconfig mynet0`, produce una salida diferente en distintos puntos del ciclo cargar-configurar-usar-descargar, y ver esas diferencias en tus propios apuntes hace que los conceptos queden grabados.

### Ve a tu ritmo

Si tu comprensión se difumina durante una sección concreta, detente. Vuelve a leer la subsección anterior. Prueba un pequeño experimento, por ejemplo `ifconfig lo0` o `netstat -in` para ver una interfaz real, y piensa en cómo corresponde a lo que el capítulo está enseñando. La programación de red en el kernel premia la exposición lenta y deliberada. Hojear el capítulo en busca de términos que reconocer más tarde es mucho menos útil que leer bien una sección, hacer un laboratorio y continuar.

## Cómo sacar el máximo partido de este capítulo

El capítulo está estructurado de modo que cada sección añade exactamente un concepto nuevo sobre lo que vino antes. Para aprovechar al máximo esa estructura, trata el capítulo como un taller, no como una referencia. No estás aquí para encontrar una respuesta rápida. Estás aquí para construir un modelo mental correcto de lo que es una interfaz, cómo habla un driver con el kernel y cómo le responde la pila de red.

### Trabaja por secciones

No leas el capítulo entero de principio a fin sin parar. Lee una sección, luego detente. Prueba el experimento o laboratorio que va con ella. Mira el código fuente de FreeBSD relacionado. Escribe unas pocas líneas en tu cuaderno. Solo entonces continúa. La programación de red en el kernel es fuertemente acumulativa, y saltarte partes normalmente significa que estarás confundido sobre el siguiente concepto por una razón que se explicó dos secciones atrás.

### Mantén el driver en funcionamiento

Una vez que hayas cargado el driver en la sección 3, mantenlo cargado todo el tiempo que puedas mientras lees. Modifícalo, recárgalo, pruébalo con `ifconfig`, envíale paquetes con `ping`, obsérvalo con `tcpdump`. Tener un ejemplo vivo y observable vale mucho más que cualquier cantidad de lectura, especialmente con código de red, porque el ciclo de retroalimentación es rápido: el kernel acepta tu configuración o la rechaza, y los contadores avanzan o no.

### Consulta las páginas del manual

Las páginas del manual de FreeBSD forman parte del material de enseñanza, no son un trámite aparte. La sección 9 del manual es donde viven las interfaces del kernel. A lo largo de este capítulo haremos referencia a páginas como `ifnet(9)`, `mbuf(9)`, `ifmedia(9)`, `ether(9)` y `ng_ether(4)`, así como a páginas del espacio de usuario como `ifconfig(8)`, `netstat(1)`, `tcpdump(1)`, `ping(8)` y `ngctl(8)`. Léelas junto a este capítulo. Son más cortas de lo que parecen, y las escribió la misma comunidad que desarrolló el kernel que estás aprendiendo.

### Escribe el código y luego modifícalo

Cuando construyas el driver a partir de los ejemplos del libro, escríbelo primero. Una vez que funcione, empieza a cambiarlo. Renombra un método y observa cómo falla la compilación. Elimina una rama `if` en la función de transmisión y mira qué ocurre con `ping`. Pon en código fijo un MTU más pequeño y observa cómo reacciona `ifconfig`. El código del kernel se comprende mucho mejor mediante la mutación deliberada que mediante la lectura pura, y el código de red es especialmente adecuado para este enfoque porque cada cambio produce un efecto inmediatamente visible en `ifconfig` o en `netstat`.

### Confía en las herramientas

FreeBSD te ofrece una gran variedad de herramientas para inspeccionar la pila de red: `ifconfig`, `netstat`, `tcpdump`, `ngctl`, `sysctl net.`, `arp`, `ndp`. Úsalas. Cuando algo va mal, el primer paso casi nunca es leer más código fuente. Es preguntarle al sistema en qué estado se encuentra. Un minuto con `ifconfig mynet0` y `netstat -in` suele ser más revelador que cinco minutos de `grep`.

### Haz pausas

El código de red está lleno de pasos pequeños y precisos. Una flag olvidada o un callback sin definir producirán un comportamiento que parecerá misterioso hasta que te detengas, respires y traces el flujo de datos de nuevo. Dos o tres horas de concentración suelen ser más productivas que una sesión de siete horas. Si te sorprendes cometiendo el mismo error tipográfico tres veces, o copiando y pegando sin leer, esa es tu señal de que necesitas levantarte diez minutos.

Con esos hábitos en mente, empecemos.

## Sección 1: Qué hace un driver de red

Un driver de red tiene una tarea que suena sencilla pero resulta estar llena de capas: mueve paquetes entre un transporte y la pila de red de FreeBSD. Todo lo demás se desprende de ahí. Para entender lo que esa frase significa de verdad, necesitamos ir despacio y examinar cada una de sus partes. ¿Qué es un paquete? ¿Qué es un transporte? ¿Qué es exactamente "la pila"? ¿Y cómo se sitúa un driver entre ambos sin convertirse en un cuello de botella ni en una fuente de errores sutiles?

### Un paquete en el kernel

En el espacio de usuario rara vez trabajas con paquetes en bruto. Abres un socket, llamas a `send` o `recv`, y el kernel se encarga de encapsular tu payload en TCP, envolver eso en IP, añadir una cabecera Ethernet y, finalmente, entregar todo el conjunto a un driver. En el kernel, el mismo paquete está representado por una lista enlazada de estructuras llamadas **mbufs**. Un mbuf es una pequeña celda de memoria, normalmente de 256 bytes, que almacena datos del paquete y una pequeña cabecera. Si el paquete es más grande de lo que puede contener un solo mbuf, el kernel encadena varios mbufs a través de un puntero `m_next`, y la longitud total del payload queda registrada en `m->m_pkthdr.len`. Si el paquete no cabe en un solo cluster de mbuf, el kernel utiliza buffers externos referenciados por el mbuf, a través de un mecanismo al que volveremos en capítulos posteriores.

Desde la perspectiva del driver, un paquete casi siempre se presenta como una cadena de mbufs, y el primer mbuf lleva la cabecera del paquete. Ese primer mbuf tiene `M_PKTHDR` activado en sus flags, lo que indica que `m->m_pkthdr` contiene campos válidos como la longitud total del paquete, la etiqueta VLAN, los flags de checksum y la interfaz receptora. Cualquier driver que gestione paquetes transmitidos empieza inspeccionando el mbuf que se le ha entregado, y cualquier driver que entregue paquetes recibidos empieza construyendo un mbuf con la forma correcta.

Cubriremos la construcción y liberación de mbufs con más detalle en las secciones 4 y 5. Por ahora, lo que importa es el vocabulario. Un mbuf es un paquete. Una cadena de mbufs es un paquete cuyo payload abarca varios mbufs. El primer mbuf de una cadena lleva la cabecera del paquete. El resto de la cadena continúa el payload, y cada mbuf apunta al siguiente a través de `m_next`.

### Un transporte

El transporte es aquello con lo que el driver se comunica del lado del hardware. En una NIC Ethernet física es el cable real, al que se accede mediante una combinación de buffers DMA, anillos de hardware e interrupciones del chip. En un adaptador USB Ethernet es el pipeline de endpoint USB que presentamos en el Capítulo 26. En una tarjeta inalámbrica es la radio. En un pseudodispositivo, que es lo que construiremos en este capítulo, el transporte es simulado: fingiremos que un paquete que transmitimos aparece en algún otro cable virtual, y fingiremos que los paquetes entrantes llegan desde él a intervalos regulares controlados por un temporizador.

La elegancia de la abstracción `ifnet` es que a la pila de red no le importa cuál de estos transportes uses. La pila ve una interfaz. Le entrega mbufs a la interfaz para transmitir. Espera que la interfaz le entregue mbufs que han sido recibidos. Ya sea que los paquetes viajen realmente por un cable de categoría 6, ondas de radio, un bus USB o una región de memoria que controlamos, la superficie es la misma. Esa uniformidad es la que permite a FreeBSD admitir decenas de dispositivos de red sin reescribir su código de red para cada uno.

### La pila de red

"La pila" es una forma abreviada de referirse al conjunto de código que se sitúa por encima del driver e implementa los protocolos. Capa a capa, de la más baja a la más alta: el encuadre Ethernet, ARP y el descubrimiento de vecinos, IPv4 e IPv6, TCP y UDP, los buffers de socket y la capa de llamadas al sistema que traduce `send` y `recv` en operaciones de la pila. En FreeBSD, el código vive en `/usr/src/sys/net/`, `/usr/src/sys/netinet/`, `/usr/src/sys/netinet6/` y directorios relacionados, y se comunica con los drivers a través de un conjunto pequeño y bien definido de punteros a funciones almacenados en cada `ifnet`.

Para este capítulo no necesitas conocer el interior de la pila. Necesitas conocer su interfaz exterior tal como la ve un driver. Esa interfaz es:

* La pila llama a tu función de transmisión, `if_transmit`, y te entrega un mbuf. Tu trabajo es convertir ese mbuf en algo que el transporte acepte.
* La pila llama a tu manejador de ioctl, `if_ioctl`, en respuesta a comandos del espacio de usuario como `ifconfig mynet0 up` o `ifconfig mynet0 mtu 1400`. Tu trabajo es atender la petición o devolver un error razonable.
* La pila llama a tu función de inicialización, `if_init`, cuando la interfaz pasa al estado activo. Tu trabajo es preparar el transporte para su uso.
* Tú llamas a `ifp->if_input(ifp, m)` o, en el estilo moderno, `if_input(ifp, m)`, para entregar un paquete recibido a la pila. Tu trabajo es asegurarte de que el mbuf está bien formado y el paquete está completo.

Ese es el contrato. El resto son detalles.

### En qué se diferencia un driver de red de un driver de caracteres

Ya construiste drivers de caracteres en los Capítulos 14 y 18. Un driver de caracteres vive dentro de `/dev/`, el espacio de usuario lo abre mediante `open(2)`, e intercambia bytes con uno o más procesos a través de `read(2)` y `write(2)`. Tiene una tabla `cdevsw`. Es sondeado y empujado por quien lo abre.

Un driver de red no es nada de eso. No vive en `/dev/`. Ningún proceso lo abre con `open(2)`. No hay `cdevsw`. Lo más parecido a un manejador de archivo visible para el usuario en una interfaz de red es el socket vinculado a ella, y aun así está mediado por la pila, no por el driver.

En lugar de un `cdevsw`, un driver de red tiene un `struct ifnet`. En lugar de `d_read`, tiene `if_input`, pero en el sentido contrario: el driver lo llama, en lugar de que lo llame el espacio de usuario. En lugar de `d_write`, tiene `if_transmit`, llamado por la pila. En lugar de `d_ioctl`, tiene `if_ioctl`, llamado por la pila en respuesta a `ifconfig` y herramientas relacionadas. La estructura de alto nivel parece similar, pero las relaciones entre los actores son diferentes. En un driver de caracteres esperas las lecturas y escrituras del espacio de usuario. En un driver de red estás integrado en un pipeline donde la pila es tu principal colaborador, y el espacio de usuario es un espectador en lugar de un par directo.

Vale la pena interiorizar este cambio de perspectiva antes de escribir ningún código. Cuando algo va mal en un driver de caracteres, la pregunta suele ser "¿qué hizo el espacio de usuario?". Cuando algo va mal en un driver de red, la pregunta suele ser "¿qué esperaba la pila que hiciera mi driver, y en qué fallé?".

### En qué se diferencia un driver de red de un driver de almacenamiento

Un driver de almacenamiento, como viste en el Capítulo 27, tampoco es un endpoint de `/dev/` en el sentido habitual. Sí expone un nodo de dispositivo de bloques, pero el acceso a él está casi siempre mediado por un sistema de archivos que se sitúa encima. Las peticiones llegan en forma de BIOs, el driver las gestiona y la finalización se señaliza mediante `biodone(bp)`.

Un driver de red comparte con el driver de almacenamiento la forma de "estoy por debajo de un subsistema, no al lado del espacio de usuario", pero el subsistema que tiene encima es muy diferente. El subsistema de almacenamiento es profundamente síncrono a nivel de BIO, en el sentido de que cada petición tiene un evento de finalización bien definido. El tráfico de red no es así. Un driver transmite un paquete, pero no hay ningún callback de finalización por paquete que suba desde el driver hacia ningún solicitante concreto. La pila confía en que el driver tenga éxito o falle de forma limpia, incrementa los contadores y sigue adelante. Del mismo modo, los paquetes recibidos no son respuestas a transmisiones anteriores específicas: simplemente llegan, y el driver debe canalizarlos hacia `if_input` en cuanto aparecen.

Otra diferencia es la concurrencia. Un driver de almacenamiento suele tener un único camino de BIO y gestiona cada BIO de forma secuencial. Un driver de red es llamado con frecuencia desde múltiples contextos de CPU a la vez, porque la pila sirve a muchos sockets en paralelo, y el hardware moderno entrega eventos de recepción en múltiples colas. No cubriremos esa complejidad en este capítulo, pero conviene que ya seas consciente de que las convenciones de locking para drivers de red son estrictas. El driver `mynet` que construiremos es lo suficientemente pequeño como para que un único mutex sea suficiente, pero incluso así la disciplina sobre cuándo tomarlo y cuándo soltarlo antes de llamar hacia arriba importa.

### El papel de `ifconfig`, `netstat` y `tcpdump`

Todo usuario de FreeBSD conoce `ifconfig`. Desde la perspectiva del autor de un driver de red, `ifconfig` es la principal vía por la que el kernel espera que los comandos de usuario lleguen a tu driver. Cuando el usuario ejecuta `ifconfig mynet0 up`, el kernel traduce eso en un ioctl `SIOCSIFFLAGS` sobre la interfaz cuyo nombre es `mynet0`. La llamada llega a tu callback `if_ioctl`, y tú decides qué hacer con ella. La simetría entre el comando del espacio de usuario y el callback del lado del kernel es casi uno a uno.

`netstat -in` solicita al kernel las estadísticas de la interfaz almacenadas en cada `ifnet`. Tu driver actualiza esos contadores llamando a `if_inc_counter(ifp, IFCOUNTER_*, n)` en los momentos oportunos del camino de transmisión y recepción. El conjunto de contadores está definido en `/usr/src/sys/net/if.h` e incluye `IFCOUNTER_IPACKETS`, `IFCOUNTER_OPACKETS`, `IFCOUNTER_IBYTES`, `IFCOUNTER_OBYTES`, `IFCOUNTER_IERRORS`, `IFCOUNTER_OERRORS`, `IFCOUNTER_IMCASTS`, `IFCOUNTER_OMCASTS` e `IFCOUNTER_OQDROPS`, entre otros. Estos contadores son los que los usuarios ven en `netstat` y `systat`.

`tcpdump` se apoya en un subsistema separado llamado el Berkeley Packet Filter, o BPF. Cualquier interfaz que quiera ser visible para `tcpdump` debe registrarse con BPF a través de `bpfattach()`, y cada paquete que el driver transmita o reciba debe presentarse a BPF mediante `BPF_MTAP()` o `bpf_mtap2()` antes de ser enviado o entregado hacia arriba. Haremos esto en nuestro driver. Es una de las pequeñas cortesías que le debes al resto del sistema para que las herramientas funcionen.

### Una imagen útil

Vale la pena cerrar la sección con un diagrama. La imagen que sigue muestra cómo encajan las piezas que hemos descrito. No la memorices todavía. Acostúmbrate simplemente a la forma. Volveremos a cada cuadro en secciones posteriores.

```text
          +-------------------+
          |     userland      |
          |   ifconfig(8),    |
          |   tcpdump(1),     |
          |   ping(8), ...    |
          +---------+---------+
                    |
     socket calls,  |  ifconfig ioctls
     tcpdump via bpf|
                    v
          +---------+---------+
          |     network       |
          |      stack        |
          |  TCP/UDP, IP,     |
          |  Ethernet, ARP,   |
          |  routing, BPF     |
          +---------+---------+
                    |
        if_transmit |    if_input
                    v
          +---------+---------+
          |    network        |
          |     driver        |    <-- that is where we live
          |   (ifnet, softc)  |
          +---------+---------+
                    |
                    v
          +---------+---------+
          |    transport      |
          |   real NIC, USB,  |
          |   radio, loopback,|
          |   or simulation   |
          +-------------------+
```

Los recuadros situados encima del driver son el stack y el userland. El recuadro de abajo es el transporte. Tu driver, en esa línea intermedia, es el único lugar del sistema donde un `struct ifnet` se encuentra con un `struct mbuf` y con un cable. Ese es tu territorio.

### Trazando un paquete a través de la pila

Es útil seguir un paquete concreto de principio a fin, porque eso fija las relaciones del diagrama anterior en código real. Vamos a trazar una solicitud de eco ICMP saliente generada por `ping 192.0.2.99` en una interfaz llamada `mynet0` a la que se ha asignado la dirección `192.0.2.1/24`.

El programa `ping(8)` abre un socket ICMP raw y escribe un payload de solicitud de eco mediante `sendto(2)`. Dentro del kernel, la capa de socket en `/usr/src/sys/kern/uipc_socket.c` copia el payload en una cadena mbuf nueva. El socket no está conectado, por lo que cada escritura lleva una dirección de destino que la capa de socket reenvía a la capa de protocolo. La capa de protocolo, en `/usr/src/sys/netinet/raw_ip.c`, adjunta una cabecera IP y llama a `ip_output` en `/usr/src/sys/netinet/ip_output.c`. `ip_output` realiza la búsqueda de ruta y encuentra una entrada de enrutamiento que apunta a `mynet0`. También detecta que el destino no es la dirección de broadcast ni un vecino en el mismo enlace cuya MAC ya conoce, por lo que debe iniciar ARP.

En este punto la capa IP llama a `ether_output`, definida en `/usr/src/sys/net/if_ethersubr.c`. `ether_output` detecta que la dirección del siguiente salto no está resuelta y emite primero una solicitud ARP. El mecanismo ARP, en `/usr/src/sys/netinet/if_ether.c`, construye una trama ARP de broadcast, la envuelve en un nuevo mbuf y llama a `ether_output_frame`, que a su vez llama a `ifp->if_transmit`. Esa es nuestra función `mynet_transmit`. El mbuf que recibimos en el callback de transmisión ya contiene una trama Ethernet completa: MAC de destino `ff:ff:ff:ff:ff:ff`, MAC de origen nuestra dirección fabricada, EtherType `0x0806` (ARP) y el payload ARP.

Hacemos lo que hace todo driver en ese punto: validar, contar, pasar por BPF y liberar. Como somos un pseudo-driver, liberamos la trama en lugar de entregarla al hardware. En un driver real de NIC entregaríamos el mbuf a DMA y lo liberaríamos más tarde cuando se dispare la interrupción de finalización. De cualquier modo, el mbuf ha llegado al final de su vida desde la perspectiva del driver.

Mientras la solicitud ARP queda sin respuesta, la pila encola el payload ICMP original en la cola de pendientes de ARP. Cuando la respuesta ARP no llega dentro de un tiempo de espera configurable, la pila abandona ese paquete e incrementa `IFCOUNTER_OQDROPS`. En nuestro pseudo-driver, por supuesto, no llegará ninguna respuesta porque no hay nada en el otro extremo del cable simulado. Por eso `ping` finalmente imprime "100.0% packet loss" y sale sin éxito. La ausencia de respuesta no es un error en nuestro driver; es una propiedad del transporte que hemos elegido simular.

Ahora traza el camino inverso. La solicitud ARP sintética que generamos cada segundo en `mynet_rx_timer` nace como memoria que asignamos con `MGETHDR` dentro de nuestro driver. Rellenamos la cabecera Ethernet, la cabecera ARP y el payload ARP. Pasamos por BPF. Llamamos a `if_input`, que desreferencia `ifp->if_input` y aterriza en `ether_input`. `ether_input` examina el EtherType y envía el payload a `arpintr` (o su equivalente moderno, una llamada directa desde `ether_demux`). El código ARP inspecciona las IP de origen y destino, detecta que el destino no somos nosotros y descarta silenciosamente la trama. Listo.

En ambas direcciones el driver es un paso breve: llega un mbuf, parte un mbuf, los contadores se actualizan y BPF lo ve todo entre medias. Esa simplicidad es engañosa, porque cada paso tiene un contrato que no debe romperse, pero el patrón es genuinamente así de corto.

### Las disciplinas de cola por encima de ti

No las ves desde el driver, pero la pila tiene disciplinas de cola que gobiernan cómo se entregan los paquetes a `if_transmit`. Históricamente, los drivers tenían un callback `if_start` y la pila colocaba los paquetes en una cola interna (`if_snd`) para su despacho posterior. Los drivers modernos utilizan `if_transmit` y reciben el mbuf directamente, dejando que el driver o la biblioteca auxiliar `drbr(9)` gestionen internamente las colas por CPU.

En la práctica, casi todos los drivers modernos utilizan `if_transmit` y dejan que la pila les entregue los paquetes de uno en uno. Como `if_transmit` se llama en el thread que produjo el paquete (normalmente un temporizador de retransmisión TCP o el thread que escribió en el socket), la ruta de transmisión suele estar en un thread del kernel normal con la apropiación habilitada. Esto importa porque significa que por lo general no puedes asumir que la transmisión se ejecute con prioridad elevada, y no debes mantener un mutex durante una operación larga.

Un pequeño número de drivers sigue utilizando el modelo clásico `if_start`, donde la pila llena una cola y llama a `if_start` para vaciarla. Ese modelo es más sencillo para drivers con colas de hardware simples, pero menos flexible bajo carga. `epair(4)` utiliza `if_transmit` directamente. `disc(4)` implementa su propio `discoutput` mínimo que se llama desde la ruta pre-transmisión de `ether_output`. La mayoría de los drivers reales de NIC usan `if_transmit` con colas internas por CPU impulsadas por `drbr`.

Para `mynet`, usamos `if_transmit` sin cola interna. Este es el diseño más sencillo posible y se corresponde con lo que haría un driver real mínimo para enlaces de baja velocidad.

### Una nota sobre la visibilidad de las capturas de paquetes

Las capturas de paquetes (packet taps), que se tratan en las próximas secciones, son una de las razones principales por las que un driver de red se siente diferente a un driver de caracteres. El tráfico de un driver de caracteres es invisible para los observadores externos, porque no existe un análogo de `tcpdump` para el tráfico arbitrario de `/dev/`. El tráfico de un driver de red, en cambio, es observable en múltiples niveles simultáneamente: BPF captura a nivel del driver, pflog a nivel del filtro de paquetes, contadores de interfaz a nivel del kernel y buffers de socket a nivel del userland. Toda esa observabilidad es gratuita para el autor del driver, siempre que el driver pase por BPF y actualice los contadores en los puntos correctos.

Este nivel de visibilidad externa tan poco habitual es una bendición para la depuración. Cuando no puedes saber por qué un paquete fluyó o no fluyó, casi siempre puedes responder a la pregunta con una combinación de `tcpdump`, `netstat`, `arp` y `route monitor`. Es un conjunto de herramientas muy capaz, y lo utilizaremos a lo largo de los laboratorios.

### Cerrando la sección 1

Hemos establecido el escenario. Un driver de red mueve mbufs entre la pila y un transporte. Presenta una interfaz estandarizada llamada `ifnet`. Lo dirigen llamadas de la pila a callbacks fijos. Empuja el tráfico recibido hacia arriba a través de `if_input`. Es visible para `ifconfig`, para `netstat` y para `tcpdump` gracias a un puñado de convenciones del kernel.

Con esa forma aproximada en mente, podemos examinar el objeto `ifnet` en sí. Ese es el tema de la sección 2.

## Sección 2: Presentando `ifnet`

Cada interfaz de red en un sistema FreeBSD en ejecución está representada en el kernel por un `struct ifnet`. Esa estructura es el objeto central del subsistema de red. Cuando `ifconfig` lista las interfaces, básicamente está iterando sobre una lista de objetos `ifnet`. Cuando la pila elige una ruta, acaba aterrizando en un `ifnet` y llama a su función de transmisión. Cuando un driver informa del estado del enlace, actualiza campos dentro de un `ifnet`. Aprender `ifnet` no es opcional. Todo lo demás en este capítulo está construido sobre él.

### Dónde vive `ifnet`

La declaración de `struct ifnet` está en `/usr/src/sys/net/if_var.h`. A lo largo de los años FreeBSD ha evolucionado hacia tratarlo como opaco, y la forma recomendada de referirse a él en el código nuevo de drivers es mediante el typedef `if_t`, que es un puntero a la estructura subyacente:

```c
typedef struct ifnet *if_t;
```

El código antiguo de drivers accede directamente a `ifp->if_softc`, `ifp->if_flags`, `ifp->if_mtu` y campos similares. El código nuevo de drivers prefiere funciones de acceso como `if_setsoftc(ifp, sc)`, `if_getflags(ifp)`, `if_setflags(ifp, flags)` e `if_setmtu(ifp, mtu)`. Ambos estilos siguen existiendo en el árbol, y drivers existentes como `/usr/src/sys/net/if_disc.c` siguen usando acceso directo a los campos. El estilo opaco es la dirección hacia la que se mueve el kernel, pero verás ambos durante años.

A lo largo de este capítulo usaremos lo que resulte más claro en cada contexto. Cuando el estilo de acceso directo hace el código más pequeño y fácil de leer, lo usaremos. Cuando un acceso hace que la intención sea más clara, usaremos ese. Deberías poder leer cualquiera de las dos formas.

### Los campos mínimos que te importan

Un `struct ifnet` tiene docenas de campos. La buena noticia es que un driver solo toca directamente un puñado de ellos. Los campos que establecerás o inspeccionarás en el driver que construimos son, a grandes rasgos:

* **Identidad.** `if_softc` apunta de vuelta a la estructura privada de tu driver, `if_xname` es el nombre de la interfaz (por ejemplo `mynet0`), `if_dname` es el nombre de familia (`"mynet"`) e `if_dunit` es el número de unidad.
* **Capacidades y contadores.** `if_mtu` es la unidad máxima de transmisión, `if_baudrate` es la velocidad de línea informada en bits por segundo, `if_capabilities` e `if_capenable` describen capacidades de offload como el etiquetado VLAN y el offload de suma de verificación.
* **Flags.** `if_flags` contiene los flags a nivel de interfaz establecidos por el userland: `IFF_UP`, `IFF_BROADCAST`, `IFF_SIMPLEX`, `IFF_MULTICAST`, `IFF_POINTOPOINT`, `IFF_LOOPBACK`. `if_drv_flags` contiene flags privados del driver; el más importante es `IFF_DRV_RUNNING`, que significa que el driver ha asignado sus recursos por interfaz y está listo para mover tráfico.
* **Callbacks.** `if_init`, `if_ioctl`, `if_transmit`, `if_qflush` e `if_input` son los punteros de función que invoca la pila. Algunos de estos tienen campos directos de larga tradición; los equivalentes de acceso son `if_setinitfn`, `if_setioctlfn`, `if_settransmitfn`, `if_setqflushfn` e `if_setinputfn`.
* **Estadísticas.** Los accesos por contador `if_inc_counter(ifp, IFCOUNTER_*, n)` incrementan los contadores que muestra `netstat -in`.
* **Hook de BPF.** `if_bpf` es un puntero opaco usado por BPF. Tu driver normalmente no lo lee directamente, pero cuando llamas a `bpfattach(ifp, ...)` y `BPF_MTAP(ifp, m)`, el sistema lo gestionará.
* **Medios y estado del enlace.** `ifmedia` vive en tu softc, no en el `ifnet`, pero la interfaz informa del estado del enlace llamando a `if_link_state_change(ifp, LINK_STATE_*)`.

Si la lista parece larga, recuerda que la mayoría de los drivers establece cada campo una vez y luego lo deja en paz. El trabajo de un driver está en los callbacks, no en los campos del ifnet en sí.

### El ciclo de vida de `ifnet`

Un `struct ifnet` pasa por las mismas etapas de alto nivel que un `device_t` o un softc: asignación, configuración, registro, vida activa y desmontaje. El grafo de llamadas es:

```text
  if_alloc(type)         -> returns a fresh ifnet, not yet attached
     |
     | configure fields
     |  if_initname()       set the name
     |  if_setsoftc()       point at your softc
     |  if_setinitfn()      set if_init callback
     |  if_setioctlfn()     set if_ioctl
     |  if_settransmitfn()  set if_transmit
     |  if_setqflushfn()    set if_qflush
     |  if_setflagbits()    set IFF_BROADCAST, etc.
     |  if_setmtu()         set MTU
     v
  if_attach(ifp)         OR ether_ifattach(ifp, mac)
     |
     | live interface
     |  if_transmit called by stack
     |  if_ioctl called by stack
     |  driver calls if_input to deliver received packets
     |  driver calls if_link_state_change on link events
     v
  ether_ifdetach(ifp)    OR if_detach(ifp)
     |
     | finish teardown
     v
  if_free(ifp)
```

Existen dos variantes comunes de las llamadas de attach y detach. Una pseudo-interfaz sencilla que no necesita conexión Ethernet usa `if_attach` e `if_detach`. Una pseudo-interfaz o interfaz Ethernet real usa `ether_ifattach` y `ether_ifdetach` en su lugar. Las variantes Ethernet envuelven las básicas y añaden la configuración adicional necesaria para una interfaz Ethernet de capa 2, incluyendo `bpfattach`, el registro de direcciones y la conexión de `ifp->if_input` e `ifp->if_output` a `ether_input` y `ether_output`. Usaremos la variante Ethernet en nuestro driver porque nos proporciona una interfaz familiar con dirección MAC que `ifconfig`, `ping` y `tcpdump` comprenden sin tratamiento especial.

Si abres `/usr/src/sys/net/if_ethersubr.c` y miras `ether_ifattach`, verás exactamente esta lógica: establece `if_addrlen` a `ETHER_ADDR_LEN`, establece `if_hdrlen` a `ETHER_HDR_LEN`, establece `if_mtu` a `ETHERMTU`, llama a `if_attach`, luego instala las rutinas comunes de entrada y salida Ethernet y finalmente llama a `bpfattach`. Vale la pena leer esa función completa. Es corta y muestra exactamente lo que obtiene un driver de forma gratuita al usar `ether_ifattach` en lugar del `if_attach` básico.

### Por qué `ifnet` no es un `cdevsw`

Es tentador ver `ifnet` como simplemente "un `cdevsw` para redes". No lo es. Un `cdevsw` es una tabla de entradas que `devfs` utiliza para despachar `read`, `write`, `ioctl`, `open` y `close` desde el userland hasta el driver. Un `ifnet` es el objeto de primera clase que la propia pila de red mantiene para cada interfaz. Aunque ningún proceso en el userland haya interactuado jamás con la interfaz, la pila sigue necesitando su `ifnet`, porque las tablas de enrutamiento, el ARP y el reenvío de paquetes dependen de él.

Puedes verlo si piensas en cómo `ifconfig` se comunica con el kernel. No abre `/dev/mynet0`. Abre un socket y emite ioctls sobre ese socket, pasando el nombre de la interfaz como argumento. El kernel entonces busca el `ifnet` por nombre e invoca `if_ioctl` sobre él. No hay ningún descriptor de archivo que apunte a tu interfaz en el lado del userland. La interfaz es una entidad a nivel de pila, no una entidad de `/dev/`.

Por eso necesitamos un objeto completamente nuevo: porque las redes requieren un handle persistente, interno al kernel, que exista independientemente de qué proceso esté haciendo qué. `ifnet` es ese handle.

### Pseudo-interfaces frente a interfaces NIC reales

Toda interfaz del kernel, pseudo o real, tiene un `ifnet`. La interfaz de loopback `lo0` tiene uno. La interfaz `disc` que estudiaremos tiene uno. Cada adaptador Ethernet `emX` tiene uno. Cada interfaz inalámbrica `wlanX` tiene uno. El `ifnet` es la moneda universal.

Las pseudo-interfaces se diferencian de las NIC reales en cómo se instancian. Una interfaz NIC real se crea mediante el método `attach` del driver durante el sondeo del bus, del mismo modo en que los drivers USB y PCI del Capítulo 26 conectan sus dispositivos. Una pseudo-interface se crea en el momento de carga del módulo, o bajo demanda mediante `ifconfig mynet0 create`, a través de un mecanismo denominado **interface cloner**. Usaremos un interface cloner para `mynet`, lo que significa que los usuarios podrán crear interfaces de forma dinámica, igual que pueden crear interfaces epair hoy en día:

```console
# ifconfig mynet create
mynet0
# ifconfig mynet0 up
# ifconfig mynet0
mynet0: flags=8843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST> metric 0 mtu 1500
```

Describiremos los cloners en la Sección 3. Por ahora, basta con saber que el cloning es el mecanismo por el que un módulo añade uno o varios objetos `ifnet` al sistema en ejecución a petición del usuario.

### Un vistazo más detallado a los campos clave de `ifnet`

Como el `ifnet` es la estructura que tu driver modifica con más frecuencia, conviene examinar algunos de sus campos con un poco más de profundidad antes de abrir el código. No es necesario que memorices la declaración completa. Lo que necesitas es la suficiente familiaridad con la distribución para leer código de drivers sin tener que volver constantemente a `if_var.h`.

`if_xname` es un array de caracteres que contiene el nombre visible para el usuario de la interfaz, como `mynet0`. Lo establece `if_initname` y, a partir de ese momento, la pila de red lo trata como de solo lectura. Cuando lees la salida de `ifconfig -a`, cada línea que comienza con un nombre de interfaz imprime una copia de `if_xname`.

`if_dname` e `if_dunit` almacenan por separado el nombre de familia del driver y el número de unidad. `if_dname` es `"mynet"` para cada instancia de nuestro driver, e `if_dunit` es `0` para `mynet0`, `1` para `mynet1`, y así sucesivamente. La pila de red usa estos campos para indexar la interfaz en diversas tablas hash, e `ifconfig` los usa para asociar un nombre de interfaz con una familia de drivers.

`if_softc` es el puntero de retorno a la estructura privada por interfaz de tu driver. Cada callback que invoca la pila recibe un argumento `ifp`, y lo primero que hace la mayoría de los callbacks es extraer el softc de `ifp->if_softc` (o `if_getsoftc(ifp)`). Si olvidas establecer `if_softc` durante la creación, tus callbacks desreferenciarán un puntero NULL y el kernel hará panic.

`if_type` es la constante de tipo definida en `/usr/src/sys/net/if_types.h`. `IFT_ETHER` para una interfaz de tipo Ethernet, `IFT_LOOP` para loopback, `IFT_IEEE80211` para inalámbrica, `IFT_TUNNEL` para un túnel genérico, y docenas más. La pila especializa ocasionalmente su comportamiento en función de `if_type`, por ejemplo para decidir cómo formatear una dirección de capa de enlace para su presentación.

`if_addrlen` e `if_hdrlen` describen la longitud de la dirección de capa de enlace (seis bytes para Ethernet, ocho para InfiniBand, cero para un túnel L3 puro) y la longitud de la cabecera de capa de enlace (14 bytes para Ethernet sin etiquetas, 22 para Ethernet etiquetado). `ether_ifattach` establece ambos con los valores predeterminados de Ethernet. Otros helpers de capa de enlace los establecen con sus propios valores.

`if_flags` es una máscara de bits de flags visibles para el usuario, como `IFF_UP` e `IFF_BROADCAST`. `if_drv_flags` es una máscara de bits de flags privados del driver, como `IFF_DRV_RUNNING`. Están separados porque tienen reglas de acceso diferentes. El usuario puede escribir en `if_flags`; solo el driver escribe en `if_drv_flags`. Mezclarlos es un error clásico.

`if_capabilities` e `if_capenable` describen las funcionalidades de offload. `if_capabilities` indica lo que el hardware dice ser capaz de hacer. `if_capenable` indica lo que está actualmente activado. Esta separación permite al userland activar o desactivar offloads en tiempo de ejecución mediante `ifconfig mynet0 -rxcsum` o `ifconfig mynet0 +tso`, y al driver respetar dicha elección. Veremos cómo interactúa esto con `SIOCSIFCAP` en la Sección 6.

`if_mtu` es la unidad máxima de transmisión en bytes. Es la carga útil L3 más grande que la interfaz puede transportar, sin contar la cabecera de capa de enlace. El valor predeterminado en Ethernet es 1500. El Ethernet con tramas jumbo suele admitir 9000 o 9216. `if_baudrate` es un campo informativo de velocidad de línea en bits por segundo; es meramente orientativo.

`if_init` es un puntero a función que se invoca cuando la interfaz pasa al estado up. Su firma es `void (*)(void *softc)`. `if_ioctl` se invoca para los ioctls de socket destinados a esta interfaz; su firma es `int (*)(struct ifnet *, u_long, caddr_t)`. `if_transmit` se invoca para enviar un paquete; su firma es `int (*)(struct ifnet *, struct mbuf *)`. `if_qflush` se invoca para vaciar las colas privadas del driver; su firma es `void (*)(struct ifnet *)`. `if_input` es un puntero a función en la dirección opuesta: el driver lo llama (normalmente a través del helper `if_input(ifp, m)`) para entregar un mbuf recibido a la pila de red.

`if_snd` es la cola de envío heredada, utilizada por los drivers que aún tienen un callback `if_start` en lugar de `if_transmit`. En los drivers modernos con `if_transmit`, `if_snd` no se utiliza. La mayoría de los ejemplos didácticos que encontrarás en el árbol (incluida nuestra referencia `if_disc.c`) ya no tocan `if_snd`.

`if_bpf` es el puntero de conexión a BPF. BPF gestiona el valor internamente; los drivers lo tratan como opaco. `BPF_MTAP` y las macros relacionadas lo usan internamente.

`if_data` es una estructura de gran tamaño que contiene estadísticas por interfaz, descriptores de medios y campos varios. Los drivers modernos evitan acceder a `if_data` directamente y en su lugar utilizan `if_inc_counter` y funciones similares. La estructura `if_data` sigue presente por compatibilidad con versiones anteriores y para las estadísticas visibles desde el userland.

Esta lista está lejos de ser exhaustiva; `struct ifnet` tiene más de cincuenta campos en total. Pero los mencionados son los que tu driver tendrá más probabilidades de usar, y familiarizarte con ellos hará que cada listado de código posterior sea más fácil de leer.

### La API de acceso con más detalle

El manejador opaco `if_t` lleva acumulando una familia de funciones de acceso desde FreeBSD 12. El patrón es consistente: donde antes escribías `ifp->if_flags |= IFF_UP`, ahora escribes `if_setflagbits(ifp, IFF_UP, 0)`. Donde antes escribías `ifp->if_softc = sc`, ahora escribes `if_setsoftc(ifp, sc)`. La motivación es permitir que el kernel evolucione la distribución interna de `struct ifnet` sin romper los drivers.

Las funciones de acceso incluyen:

* `if_setsoftc(ifp, sc)` e `if_getsoftc(ifp)` para el puntero al softc.
* `if_setflagbits(ifp, set, clear)` e `if_getflags(ifp)` para `if_flags`.
* `if_setdrvflagbits(ifp, set, clear)` e `if_getdrvflags(ifp)` para `if_drv_flags`.
* `if_setmtu(ifp, mtu)` e `if_getmtu(ifp)` para el MTU.
* `if_setbaudrate(ifp, rate)` e `if_getbaudrate(ifp)` para la velocidad de línea anunciada.
* `if_sethwassist(ifp, assist)` e `if_gethwassist(ifp)` para las indicaciones de checksum offload.
* `if_settransmitfn(ifp, fn)` para `if_transmit`.
* `if_setioctlfn(ifp, fn)` para `if_ioctl`.
* `if_setinitfn(ifp, fn)` para `if_init`.
* `if_setqflushfn(ifp, fn)` para `if_qflush`.
* `if_setinputfn(ifp, fn)` para `if_input`.
* `if_inc_counter(ifp, ctr, n)` para los contadores de estadísticas.

Algunas de estas funciones son inlines que en realidad acceden directamente al campo internamente; otras son envoltorios que en el futuro podrían referirse a una distribución de campos ligeramente diferente. Usar los accesores ahora no tiene ningún coste y protege tu driver frente a cambios futuros.

Para `mynet` usamos principalmente el estilo de acceso directo a campos, porque es lo que los drivers de referencia existentes como `if_disc.c` e `if_epair.c` siguen usando, y la coherencia con el resto del árbol es valiosa para los lectores. Cuando llegues a escribir tu propio driver nuevo, no dudes en preferir los accesores. Ambos estilos son correctos.

### Un primer vistazo al código

Antes de continuar, veamos un pequeño fragmento de código que resume la forma en que un driver se relaciona con `ifnet`. Este es el patrón que escribirás de forma más completa en la Sección 3, pero ya resulta útil ver el esqueleto:

```c
struct mynet_softc {
    struct ifnet    *ifp;
    struct mtx       mtx;
    uint8_t          hwaddr[ETHER_ADDR_LEN];
    /* ... fields for simulation state ... */
};

static int
mynet_transmit(struct ifnet *ifp, struct mbuf *m)
{
    /* pass packet to the transport, or drop it */
}

static int
mynet_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
    /* handle SIOCSIFFLAGS, SIOCSIFMTU, ... */
}

static void
mynet_init(void *arg)
{
    /* make the interface ready to move traffic */
}

static void
mynet_create(void)
{
    struct mynet_softc *sc = malloc(sizeof(*sc), M_MYNET, M_WAITOK | M_ZERO);
    struct ifnet *ifp = if_alloc(IFT_ETHER);

    sc->ifp = ifp;
    mtx_init(&sc->mtx, "mynet", NULL, MTX_DEF);
    ifp->if_softc = sc;
    if_initname(ifp, "mynet", 0);
    ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
    ifp->if_init = mynet_init;
    ifp->if_ioctl = mynet_ioctl;
    ifp->if_transmit = mynet_transmit;
    ifp->if_qflush = mynet_qflush;

    /* fabricate a MAC address ... */
    ether_ifattach(ifp, sc->hwaddr);
}
```

No lo escribas todavía. Es solo un esbozo y faltan varias piezas. Las iremos completando en la Sección 3. Lo que importa ahora es la forma: asignar, configurar, conectar. Todos los drivers del árbol hacen esto, con variaciones según el bus en el que residen y el transporte con el que trabajan.

### Cerrando la Sección 2

El objeto `ifnet` es la representación del kernel de una interfaz de red. Tiene campos de identidad, campos de capacidades, flags, callbacks, contadores y estado del medio. Se crea con `if_alloc`, el driver lo configura y se instala en el sistema con `if_attach` o `ether_ifattach`. Un driver de pseudo-interface crea objetos `ifnet` bajo demanda a través de un interface cloner. Un driver de NIC real crea su `ifnet` durante el probe y el attach.

Ahora tienes el vocabulario necesario. En la Sección 3 lo pondremos en práctica creando y registrando una interfaz de red real y funcional. Antes, sin embargo, merece la pena dedicar un momento a leer un driver real que usa los mismos patrones que estamos a punto de escribir. La siguiente subsección te guía a través de `if_disc.c`, el driver canónico de "pseudo-Ethernet más sencillo" del árbol de código fuente de FreeBSD.

### Un recorrido guiado por `if_disc.c`

Abre `/usr/src/sys/net/if_disc.c` en tu editor. Tiene unas doscientas líneas de código, y cada una resulta instructiva. El driver `disc(4)` crea una interfaz cuya única función es descartar silenciosamente cada paquete que recibe para su transmisión. Es el equivalente moral de `/dev/null` para paquetes. Por ser tan pequeño, muestra la estructura de un pseudo-driver sin ninguna distracción.

El archivo comienza con la cabecera de licencia estándar, seguida de un conjunto de directivas `#include` que ya deberían resultar familiares. `net/if.h` y `net/if_var.h` para la estructura de interfaz, `net/ethernet.h` para los helpers específicos de Ethernet, `net/if_clone.h` para la API del cloner, `net/bpf.h` para las capturas de paquetes y `net/vnet.h` para la integración con VNET. Es casi exactamente el conjunto de includes que usaremos en `mynet.c`.

A continuación aparecen unas pocas declaraciones a nivel de módulo. La cadena `discname = "disc"` es el nombre de familia que expondrá el cloner. `M_DISC` es la etiqueta de tipo de memoria para la contabilidad de `vmstat -m`. `VNET_DEFINE_STATIC(struct if_clone *, disc_cloner)` declara una variable de cloner por VNET, y la macro `V_disc_cloner` proporciona el shim de acceso. Todas estas piezas las reconocerás cuando escribamos las mismas tres líneas en nuestro propio driver unas páginas más adelante.

La declaración del softc es especialmente breve. `struct disc_softc` contiene únicamente un puntero `ifnet`. Es todo el estado que necesita un driver de descarte: una interfaz por softc, sin contadores, sin colas, sin temporizadores. Nuestro softc de `mynet` será más extenso porque tenemos un camino de recepción simulado, un descriptor de medios y un mutex, pero el patrón de "un softc por interfaz" es el mismo.

Desplázate por el archivo hasta `disc_clone_create`. Comienza asignando el softc con `M_WAITOK | M_ZERO`, porque el cloner se llama desde el contexto de usuario y puede permitirse dormir. A continuación asigna el `ifnet` con `if_alloc(IFT_LOOP)`. Observa que `disc` usa `IFT_LOOP` en lugar de `IFT_ETHER`, porque su semántica de capa de enlace se parece más a loopback que a Ethernet. La elección de la constante `IFT_*` importa porque la pila consulta `if_type` para decidir qué helper de capa de enlace invocar. Nuestro driver usará `IFT_ETHER` porque queremos usar `ether_ifattach`.

Después, `disc_clone_create` llama a `if_initname(ifp, discname, unit)`, establece el puntero al softc, establece el `if_mtu` en `DSMTU` (un valor definido localmente) y establece `if_flags` en `IFF_LOOPBACK | IFF_MULTICAST`. Se establecen los callbacks `if_ioctl`, `if_output` e `if_init`. Observa que `disc` establece `if_output` en lugar de `if_transmit`, porque los drivers de estilo loopback siguen conectados al camino de salida clásico. Nuestro driver Ethernet usará `if_transmit` a través de `ether_ifattach`.

A continuación viene `if_attach(ifp)`, que registra la interfaz en la pila sin ninguna configuración específica de Ethernet. Después llega `bpfattach(ifp, DLT_NULL, sizeof(u_int32_t))`, que registra la interfaz en BPF usando el tipo de enlace nulo (lo que indica a `tcpdump` que espere una cabecera de cuatro bytes con la familia de direcciones del payload). Nuestro driver usará `DLT_EN10MB`, de forma automática, a través de `ether_ifattach`.

La ruta de destrucción, `disc_clone_destroy`, es simétrica: llama a `bpfdetach`, `if_detach`, `if_free` y, finalmente, a `free(sc, M_DISC)`. Nuestro driver será algo más elaborado porque tenemos callouts y un descriptor de medios que desmontar, pero el esqueleto es idéntico.

La ruta de transmisión, `discoutput`, son tres líneas de código. Inspecciona la familia del paquete, rellena la cabecera BPF de cuatro bytes, captura en BPF, actualiza los contadores y libera el mbuf. Eso es todo lo que necesita hacer un driver de tipo «descartar todo». Nuestra función `mynet_transmit` será más larga, pero estructuralmente hace exactamente lo mismo con un poco más de rigor: validar, capturar, contar, liberar.

El manejador de ioctl, `discioctl`, gestiona `SIOCSIFADDR`, `SIOCSIFFLAGS` y `SIOCSIFMTU`, y devuelve `EINVAL` para todo lo demás. Para un pseudodriver mínimo es más que suficiente. Nuestro driver será más elaborado porque añadimos descriptores de medios y delegamos los ioctls desconocidos en `ether_ioctl`, pero la forma de la sentencia switch es la misma.

Por último, el registro del clonador se realiza en `vnet_disc_init` mediante `if_clone_simple(discname, disc_clone_create, disc_clone_destroy, 0)`, envuelto en `VNET_SYSINIT` y complementado por un `VNET_SYSUNINIT` que llama a `if_clone_detach`. De nuevo, este es exactamente el patrón que usaremos.

La conclusión de leer `disc` es que un pseudodriver funcional en el árbol de FreeBSD tiene unas doscientas líneas de código. La mayor parte de esas líneas son código repetitivo que se escribe una vez y se olvida. Las partes interesantes son el softc, el clonador y el puñado de callbacks. Todo lo demás es ritmo.

No te sientas obligado a memorizar `disc`. Léelo una vez, despacio, ahora. Cuando empecemos a escribir `mynet`, vuelve a esta sección y verás que la mayor parte de lo que tecleamos es el mismo patrón con algunas adiciones para el comportamiento similar a Ethernet, la recepción de paquetes y los descriptores de medios. Merece la pena ver el patrón una vez en su forma más pura antes de elaborarlo.

## Sección 3: Creación y registro de una interfaz de red

Ha llegado el momento de escribir código. En esta sección construiremos el esqueleto de `mynet`, un driver pseudo-Ethernet. Aparecerá como una interfaz Ethernet normal ante el resto del sistema. Desde el espacio de usuario, será posible crear una instancia con `ifconfig mynet create`, asignarle una dirección IPv4, activarla, desactivarla y destruirla, igual que ocurre con `epair` y `disc`. Todavía no gestionaremos el movimiento real de paquetes. Las secciones 4 y 5 se ocuparán de los caminos de transmisión y recepción. Aquí nos centramos en la creación, el registro y los metadatos básicos.

### Organización del proyecto

Todos los archivos de acompañamiento de este capítulo se encuentran en `examples/part-06/ch28-network-driver/`. El esqueleto de esta sección está en `examples/part-06/ch28-network-driver/lab01-skeleton/`. Crea ese directorio si vas siguiendo los pasos manualmente, o examina los archivos si prefieres leer primero y experimentar después. La estructura de directorios que utilizaremos a lo largo del capítulo es la siguiente:

```text
examples/part-06/ch28-network-driver/
  Makefile
  mynet.c
  README.md
  shared/
  lab01-skeleton/
  lab02-transmit/
  lab03-receive/
  lab04-media/
  lab05-bpf/
  lab06-detach/
  lab07-reading-tree/
  challenge01-shared-queue/
  challenge02-link-flap/
  challenge03-error-injection/
  challenge04-iperf3/
  challenge05-sysctl/
  challenge06-netgraph/
```

El archivo `mynet.c` de nivel superior es el driver de referencia para todo el capítulo y evoluciona desde el esqueleto de la sección 3 hasta el código de limpieza final de la sección 8. Los directorios `lab0x` contienen archivos README que te guían paso a paso por el laboratorio correspondiente. Los ejercicios de desafío añaden cada uno una pequeña funcionalidad sobre el driver terminado, y `shared/` contiene scripts auxiliares y notas a los que hacen referencia varios laboratorios.

### El Makefile

Empecemos por el archivo de build. Un módulo del kernel para un driver pseudo-Ethernet es uno de los Makefiles más sencillos de todo el árbol. El nuestro tendrá este aspecto:

```console
# Makefile for mynet - Chapter 28 (Writing a Network Driver).
#
# Builds the chapter's reference pseudo-Ethernet driver,
# mynet.ko, which demonstrates ifnet registration through an
# interface cloner, minimal transmit and receive paths, and
# safe load and unload lifecycle.

KMOD=   mynet
SRCS=   mynet.c opt_inet.h opt_inet6.h

SYSDIR?=    /usr/src/sys

.include <bsd.kmod.mk>
```

Este archivo es muy similar al Makefile que utiliza `/usr/src/sys/modules/if_disc/Makefile`, que es exactamente lo que se quiere para un driver de pseudo-interfaz basado en clonación. Hay dos pequeñas diferencias: no establecemos `.PATH`, porque nuestro archivo fuente está en el directorio actual en lugar de en `/usr/src/sys/net/`, y establecemos `SYSDIR` explícitamente para que el build funcione en máquinas que quizás no incluyan una configuración del sistema para ello. Por lo demás, es el patrón estándar de `bsd.kmod.mk` que has visto desde el Capítulo 10.

### Includes preliminares y pegamento del módulo

Abre tu editor e inicia `mynet.c` con el siguiente preámbulo. Cada include tiene un papel específico, así que los iremos anotando a medida que avancemos:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/callout.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_types.h>
#include <net/if_clone.h>
#include <net/if_media.h>
#include <net/bpf.h>
#include <net/vnet.h>
```

El primer bloque incorpora las cabeceras del kernel que ya conoces de capítulos anteriores: parámetros, llamadas al sistema, maquinaria de módulos, el asignador de memoria, el sistema de locking, mbufs, constantes de IO de sockets y el subsistema callout. El segundo bloque incorpora las cabeceras específicas de red: `if.h` para la estructura `ifnet` y sus flags, `if_var.h` para los helpers en línea, `if_arp.h` para las constantes de resolución de direcciones, `ethernet.h` para el encuadrado Ethernet, `if_types.h` para las constantes de tipo de interfaz como `IFT_ETHER`, `if_clone.h` para la API de clonación, `if_media.h` para los descriptores de medios, `bpf.h` para el soporte de `tcpdump` y `vnet.h` para la compatibilidad con VNET, que usamos de la misma forma que lo hace `/usr/src/sys/net/if_disc.c`.

A continuación, un tipo de memoria global del módulo y el nombre de la familia de interfaz:

```c
static const char mynet_name[] = "mynet";
static MALLOC_DEFINE(M_MYNET, "mynet", "mynet pseudo Ethernet driver");

VNET_DEFINE_STATIC(struct if_clone *, mynet_cloner);
#define V_mynet_cloner  VNET(mynet_cloner)
```

`mynet_name` es la cadena que pasaremos a `if_initname` para que las interfaces se llamen `mynet0`, `mynet1`, etc. `M_MYNET` es la etiqueta de tipo de memoria para que `vmstat -m` te muestre cuánta memoria está usando el driver. `VNET_DEFINE_STATIC` es compatible con VNET: le da a cada pila de red virtual su propia variable de clonación. Esto refleja la declaración `VNET_DEFINE_STATIC(disc_cloner)` que aparece en `/usr/src/sys/net/if_disc.c`. Volveremos brevemente a VNET en la sección 8.

Los nombres de funciones, macros y estructuras son la referencia duradera al árbol de FreeBSD. Los números de línea varían de una versión a otra. Solo como orientación para FreeBSD 14.3: en `/usr/src/sys/net/if_disc.c`, la declaración `VNET_DEFINE_STATIC(disc_cloner)` se encuentra hacia la línea 79 y la llamada a `if_clone_simple` dentro de `vnet_disc_init` hacia la línea 134; en `/usr/src/sys/net/if_epair.c`, `epair_transmit` comienza hacia la línea 324 y `epair_ioctl` hacia la línea 429; en `/usr/src/sys/sys/mbuf.h`, la macro de compatibilidad `MGETHDR` se encuentra hacia la línea 1125. Abre el archivo y salta directamente al símbolo.

### El softc

El softc, como ya sabes de capítulos anteriores, es la estructura privada por instancia que tu driver reserva para rastrear el estado de un dispositivo. En un driver de red, el softc es por interfaz. Así es como queda el nuestro en esta etapa:

```c
struct mynet_softc {
    struct ifnet    *ifp;
    struct mtx       mtx;
    uint8_t          hwaddr[ETHER_ADDR_LEN];
    struct ifmedia   media;
    struct callout   rx_callout;
    int              rx_interval_hz;
    bool             running;
};

#define MYNET_LOCK(sc)      mtx_lock(&(sc)->mtx)
#define MYNET_UNLOCK(sc)    mtx_unlock(&(sc)->mtx)
#define MYNET_ASSERT(sc)    mtx_assert(&(sc)->mtx, MA_OWNED)
```

Los campos son sencillos. `ifp` es el objeto de interfaz que creamos. `mtx` es un mutex para proteger el softc durante la transmisión concurrente, los ioctls y el proceso de destrucción. `hwaddr` es la dirección Ethernet de seis bytes que fabricamos. `media` es el descriptor de medios que exponemos a través de `SIOCGIFMEDIA`. `rx_callout` y `rx_interval_hz` los utiliza el camino de recepción simulado que construimos en la sección 5. `running` refleja la percepción del driver sobre si la interfaz está activa en ese momento.

Las macros del final nos proporcionan primitivas de locking cortas y legibles. Es una convención de estilo utilizada en muchos drivers de FreeBSD, incluyendo `/usr/src/sys/dev/e1000/if_em.c` y `/usr/src/sys/net/if_epair.c`.

### El esqueleto de `mynet_create`

Ahora llega la acción principal de esta sección. Escribiremos una función que el clonador llama para crear y registrar una nueva interfaz. Esta función es el núcleo del código de inicialización. La construiremos paso a paso para luego ensamblar las piezas.

Primero, reservamos memoria para el softc y el `ifnet`:

```c
struct mynet_softc *sc;
struct ifnet *ifp;

sc = malloc(sizeof(*sc), M_MYNET, M_WAITOK | M_ZERO);
ifp = if_alloc(IFT_ETHER);
if (ifp == NULL) {
    free(sc, M_MYNET);
    return (ENOSPC);
}
sc->ifp = ifp;
mtx_init(&sc->mtx, "mynet", NULL, MTX_DEF);
```

Usamos `M_WAITOK | M_ZERO` porque esta función se invoca desde un camino en contexto de usuario (el clonador) y queremos memoria inicializada a cero. `IFT_ETHER` proviene de `/usr/src/sys/net/if_types.h`: declara nuestra interfaz como una interfaz Ethernet para el registro interno del kernel, lo cual es importante porque la pila usa `if_type` para decidir qué semántica de capa de enlace aplicar.

A continuación, fabricamos una dirección MAC. En los drivers de NIC reales, el hardware dispone de una EEPROM con una MAC única asignada en fábrica. Nosotros no tenemos ese lujo, así que la inventamos. Una dirección unicast administrada localmente empieza con un byte cuyo segundo bit menos significativo está activado y cuyo bit menos significativo está a cero. La forma clásica es `02:xx:xx:xx:xx:xx`. Haremos algo similar a lo que hace `epair(4)` en su función `epair_generate_mac`:

```c
arc4rand(sc->hwaddr, ETHER_ADDR_LEN, 0);
sc->hwaddr[0] = 0x02;  /* locally administered, unicast */
```

`arc4rand` es una función aleatoria interna del kernel respaldada por entropía, definida en `/usr/src/sys/libkern/arc4random.c`. Es perfectamente válida para fabricar direcciones MAC. Luego forzamos el primer byte a `0x02` para que la dirección sea tanto administrada localmente como unicast, que es lo que IEEE reserva para las direcciones que no provienen de fábrica.

A continuación, configuramos los campos de la interfaz:

```c
if_initname(ifp, mynet_name, unit);
ifp->if_softc = sc;
ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
ifp->if_capabilities = IFCAP_VLAN_MTU;
ifp->if_capenable = IFCAP_VLAN_MTU;
ifp->if_transmit = mynet_transmit;
ifp->if_qflush = mynet_qflush;
ifp->if_ioctl = mynet_ioctl;
ifp->if_init = mynet_init;
ifp->if_baudrate = IF_Gbps(1);
```

`if_initname` establece tanto `if_xname`, el nombre único de la interfaz, como el nombre de la familia del driver y el número de unidad. `if_softc` vincula la interfaz a nuestra estructura privada para que los callbacks puedan encontrarla. Los flags marcan la interfaz como capaz de difusión (broadcast), simplex (lo que significa que no puede escuchar sus propias transmisiones, como ocurre con una NIC Ethernet real) y capaz de multicast. `IFCAP_VLAN_MTU` indica que podemos reenviar tramas etiquetadas con VLAN cuyo payload total supera en cuatro bytes el MTU base de Ethernet. Los callbacks son las funciones que implementaremos en breve. `if_baudrate` es informativo; `IF_Gbps(1)` declara un gigabit por segundo, aproximando lo que podría reclamar un enlace simulado promedio.

Después, configuramos el descriptor de medios. Esto es lo que devolverá `SIOCGIFMEDIA` y lo que usará `ifconfig mynet0` para imprimir la línea de medios:

```c
ifmedia_init(&sc->media, 0, mynet_media_change, mynet_media_status);
ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_T | IFM_FDX, 0, NULL);
ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);
```

`ifmedia_init` registra dos callbacks: uno que la pila llama cuando el usuario cambia el medio, y otro que llama para conocer el estado actual del medio. `ifmedia_add` declara un tipo de medio concreto que la interfaz admite. `IFM_ETHER | IFM_1000_T | IFM_FDX` significa "Ethernet, 1000BaseT, dúplex completo"; `IFM_ETHER | IFM_AUTO` significa "Ethernet, negociación automática". `ifmedia_set` elige el valor por defecto. `ifconfig mynet0` reflejará esta elección.

A continuación, inicializamos el callout de recepción simulado. Lo implementaremos en la sección 5, pero preparamos el campo ahora para que `mynet_create` deje el softc completamente utilizable:

```c
callout_init_mtx(&sc->rx_callout, &sc->mtx, 0);
sc->rx_interval_hz = hz;  /* one simulated packet per second */
```

`callout_init_mtx` registra nuestro callout con el mutex del softc para que el sistema de callouts adquiera y libere el lock en nuestro nombre cuando invoque el manejador. Este es un patrón ampliamente utilizado en el kernel y evita toda una clase de bugs de ordenación de locks.

Finalmente, conectamos la interfaz a la capa Ethernet:

```c
ether_ifattach(ifp, sc->hwaddr);
```

Esta única llamada hace mucho trabajo. Establece `if_addrlen`, `if_hdrlen` e `if_mtu` con los valores por defecto de Ethernet, llama a `if_attach` para registrar la interfaz, instala `ether_input` y `ether_output` como manejadores de entrada y salida de la capa de enlace, y llama a `bpfattach(ifp, DLT_EN10MB, ETHER_HDR_LEN)` para que `tcpdump -i mynet0` funcione de inmediato. Después de esta llamada, la interfaz está activa: el espacio de usuario puede verla, asignarle direcciones y empezar a emitir ioctls sobre ella.

### El esqueleto de `mynet_destroy`

La destrucción es el espejo de la creación, pero en orden inverso. Aquí está el esqueleto:

```c
static void
mynet_destroy(struct mynet_softc *sc)
{
    struct ifnet *ifp = sc->ifp;

    MYNET_LOCK(sc);
    sc->running = false;
    MYNET_UNLOCK(sc);

    callout_drain(&sc->rx_callout);

    ether_ifdetach(ifp);
    if_free(ifp);

    ifmedia_removeall(&sc->media);
    mtx_destroy(&sc->mtx);
    free(sc, M_MYNET);
}
```

Marcamos el softc como ya no activo, drenamos el callout para que ningún evento de recepción programado pueda dispararse, llamamos a `ether_ifdetach` para desvincular la interfaz, liberamos el ifnet, eliminamos las entradas de medios asignadas, destruimos el mutex y liberamos el softc. El orden importa: no debes liberar el `ifnet` mientras el callout podría estar ejecutándose contra él, y no debes destruir el mutex mientras el callout podría estar intentando adquirirlo. `callout_drain` es lo que nos da la garantía síncrona de que no se dispararán más callbacks después de que retorne.

### Registro del clonador

Dos piezas conectan `mynet_create` y `mynet_destroy` con el kernel: el registro del clonador y el manejador del módulo. Aquí está el código del clonador:

```c
static int
mynet_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
    return (mynet_create_unit(unit));
}

static void
mynet_clone_destroy(struct ifnet *ifp)
{
    mynet_destroy((struct mynet_softc *)ifp->if_softc);
}

static void
vnet_mynet_init(const void *unused __unused)
{
    V_mynet_cloner = if_clone_simple(mynet_name, mynet_clone_create,
        mynet_clone_destroy, 0);
}
VNET_SYSINIT(vnet_mynet_init, SI_SUB_PSEUDO, SI_ORDER_ANY,
    vnet_mynet_init, NULL);

static void
vnet_mynet_uninit(const void *unused __unused)
{
    if_clone_detach(V_mynet_cloner);
}
VNET_SYSUNINIT(vnet_mynet_uninit, SI_SUB_INIT_IF, SI_ORDER_ANY,
    vnet_mynet_uninit, NULL);
```

`if_clone_simple` registra un clonador simple, es decir, un clonador cuya coincidencia de nombres se basa en un prefijo exacto (`mynet` seguido de un número de unidad opcional). `/usr/src/sys/net/if_disc.c` usa esta misma llamada dentro de `vnet_disc_init`, la rutina de inicialización VNET del driver `disc`. La función de creación recibe un número de unidad y es responsable de producir una nueva interfaz. La función de destrucción recibe un `ifnet` y es responsable de eliminarlo. Las macros `SYSINIT` y `SYSUNINIT` garantizan que el clonador se registre cuando se cargue el módulo y se desregistre cuando se descargue.

El helper `mynet_create_unit` une las dos mitades. Recibe un número de unidad, realiza la reserva de memoria que describimos anteriormente, llama a `ether_ifattach` y devuelve cero si tiene éxito o un error en caso contrario. El listado completo está en el archivo de acompañamiento bajo `lab01-skeleton/`.

### El manejador del módulo

Por último, el código repetitivo estándar del módulo:

```c
static int
mynet_modevent(module_t mod, int type, void *data __unused)
{
    switch (type) {
    case MOD_LOAD:
    case MOD_UNLOAD:
        return (0);
    default:
        return (EOPNOTSUPP);
    }
}

static moduledata_t mynet_mod = {
    "mynet",
    mynet_modevent,
    NULL
};

DECLARE_MODULE(mynet, mynet_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_DEPEND(mynet, ether, 1, 1, 1);
MODULE_VERSION(mynet, 1);
```

El manejador del módulo en sí no hace nada interesante. La inicialización real ocurre en `vnet_mynet_init`, que `VNET_SYSINIT` se encarga de invocar en `SI_SUB_PSEUDO`. Esta separación no es estrictamente necesaria para un driver sin VNET, pero seguir el patrón de `disc(4)` y `epair(4)` deja nuestro driver preparado para su uso con VNET y se ajusta a la convención empleada por el resto del árbol.

`MODULE_DEPEND(mynet, ether, 1, 1, 1)` declara una dependencia del módulo `ether` para que el soporte Ethernet esté cargado antes de que intentemos usar `ether_ifattach`. `MODULE_VERSION(mynet, 1)` declara nuestro propio número de versión para que otros módulos puedan depender de nosotros si alguna vez lo necesitan.

### Un análisis más detallado de los clonadores de interfaz

Los clonadores de interfaz merecen una pequeña pausa, porque dirigen gran parte del ciclo de vida de un pseudo-driver y porque la API es algo más rica que la llamada a `if_clone_simple` que hemos usado hasta ahora.

Un clonador es una fábrica con nombre registrada en la pila de red. Lleva un prefijo de nombre, un callback de creación, un callback de destrucción y, opcionalmente, un callback de coincidencia. Cuando el espacio de usuario ejecuta `ifconfig mynet create`, la pila recorre su lista de clonadores buscando uno cuyo prefijo coincida con la cadena `mynet`. Si lo encuentra, elige un número de unidad, llama al callback de creación y devuelve el nombre de la interfaz resultante.

La API tiene dos variantes. `if_clone_simple` registra un cloner con la regla de coincidencia predeterminada: el nombre debe comenzar con el prefijo del cloner y puede ir seguido de un número de unidad. `if_clone_advanced` registra un cloner con una función de coincidencia proporcionada por el llamador, lo que permite una nomenclatura más flexible. `epair(4)` usa `if_clone_advanced` porque sus interfaces vienen en pares con los nombres `epairXa` y `epairXb`. Usamos `if_clone_simple` porque `mynet0`, `mynet1` y similares son suficientes.

Dentro del callback de creación dispones de dos datos: el propio cloner (a través del cual puedes buscar interfaces hermanas) y el número de unidad solicitado (que puede ser `IF_MAXUNIT` si el usuario no especificó ninguno, en cuyo caso debes elegir una unidad libre). En nuestro driver aceptamos la unidad que nos indique el cloner y la pasamos directamente a `if_initname`.

El callback de destrucción es más sencillo: recibe el puntero `ifnet` de la interfaz que se va a destruir y debe desmontar todo. El framework del cloner gestiona la lista de interfaces por nosotros; no necesitamos mantenerla nosotros mismos.

Cuando el módulo se descarga, `if_clone_detach` recorre la lista de interfaces que creó el cloner y llama al callback de destrucción para cada una. Después de eso, el propio cloner se da de baja. Este desmontaje en dos pasos es lo que hace que `kldunload` sea limpio: incluso si el usuario olvidó ejecutar `ifconfig mynet0 destroy` antes de descargar el módulo, el cloner se encarga de ello.

Si tu driver necesita en algún momento exponer argumentos adicionales en la ruta de creación (por ejemplo, el nombre de la interfaz asociada en un driver de estilo `epair`), el framework del cloner admite un argumento `caddr_t params` en el callback de creación, que transporta los bytes que el usuario proporcionó a través de `ifconfig mynet create foo bar`. No usamos ese mecanismo aquí, pero existe y merece la pena conocerlo.

### Qué ocurre dentro de `ether_ifattach`

Llamamos a `ether_ifattach(ifp, sc->hwaddr)` al final de `mynet_create_unit` y dijimos simplemente que «hace mucho trabajo». Abramos `/usr/src/sys/net/if_ethersubr.c` y veamos en qué consiste realmente ese trabajo, porque entenderlo hace que el resto del comportamiento de nuestro driver sea predecible en lugar de misterioso.

`ether_ifattach` comienza estableciendo `ifp->if_addrlen = ETHER_ADDR_LEN` e `ifp->if_hdrlen = ETHER_HDR_LEN`. Estos campos indican a la pila cuántos bytes de direccionamiento de capa de enlace y de cabecera antepone una trama. Para Ethernet, ambos valores son constantes: seis bytes de MAC y catorce bytes de cabecera.

A continuación establece `ifp->if_mtu = ETHERMTU` (1500 bytes, el valor predeterminado de Ethernet según IEEE) si el driver no ha fijado ya un valor mayor. Nuestro driver dejó `if_mtu` a cero tras `if_alloc`, por lo que `ether_ifattach` nos asigna el valor predeterminado. Podríamos sobreescribirlo después; un driver con soporte de tramas jumbo podría establecer `if_mtu` a 9000 antes de llamar a `ether_ifattach`.

Después establece la función de salida de capa de enlace, `if_output`, apuntando a `ether_output`. `ether_output` es el manejador genérico de L3 a L2: recibe un paquete con una cabecera IP y una dirección de destino, resuelve ARP o el descubrimiento de vecinos si es necesario, construye la cabecera Ethernet y llama a `if_transmit`. Esta cadena de indirección es la que permite que un paquete IP procedente de un socket viaje de forma transparente por la pila hasta llegar a nuestro driver.

Establece `if_input` apuntando a `ether_input`. `ether_input` es la función inversa: recibe una trama Ethernet completa, elimina la cabecera Ethernet, despacha según el EtherType y entrega el payload al protocolo correspondiente (IPv4, IPv6, ARP, LLC, etcétera). Cuando nuestro driver llama a `if_input(ifp, m)`, en la práctica está llamando a `ether_input(ifp, m)`.

Luego almacena la dirección MAC en la lista de direcciones de la interfaz, haciéndola visible al espacio de usuario a través de `getifaddrs(3)` y de `ifconfig`. Así es como `ifconfig mynet0` muestra una línea `ether`.

Después llama a `if_attach(ifp)`, que registra la interfaz en la lista global, asigna cualquier estado necesario en el lado de la pila y hace que la interfaz sea visible al espacio de usuario.

Por último llama a `bpfattach(ifp, DLT_EN10MB, ETHER_HDR_LEN)`, que registra la interfaz con BPF usando el tipo de enlace Ethernet. A partir de ese momento, `tcpdump -i mynet0` encontrará la interfaz y esperará tramas con cabeceras Ethernet de 14 bytes.

Es mucho trabajo para una sola llamada a función. Hacerlo todo a mano es válido (y muchos drivers antiguos lo hacen así), pero es propenso a errores. `ether_ifattach` es uno de esos helpers cuya existencia hace que escribir un driver sea genuinamente más fácil, y leer su cuerpo tiene su recompensa, porque desmitifica lo que ocurre entre «he asignado un ifnet» y «la pila conoce completamente mi interfaz».

La función complementaria `ether_ifdetach` realiza las operaciones inversas en el orden inverso correcto. Es la función adecuada para llamar durante el desmontaje, y es la que llamamos en `mynet_destroy`.

### Compilar, cargar y verificar

En este punto, incluso sin lógica de transmisión y recepción, el esqueleto debería compilar y cargarse. Este es el aspecto del flujo de verificación:

```console
# cd examples/part-06/ch28-network-driver
# make
# kldload ./mynet.ko

# ifconfig mynet create
mynet0
# ifconfig mynet0
mynet0: flags=8802<BROADCAST,SIMPLEX,MULTICAST> metric 0 mtu 1500
        ether 02:a3:f1:22:bc:0d
        media: Ethernet autoselect
        status: no carrier
        groups: mynet
```

La dirección MAC exacta será diferente porque `arc4rand` te proporciona una dirección aleatoria distinta cada vez. El resto de la salida debería coincidir aproximadamente. Si es así, lo has logrado: tienes una interfaz de red activa, registrada, con nombre y dirección MAC, visible para todas las herramientas estándar, sin haber procesado todavía ningún paquete real. Eso ya es un logro significativo.

Destruye la interfaz y descarga el módulo para cerrar el ciclo de vida:

```console
# ifconfig mynet0 destroy
# kldunload mynet
```

`kldstat` debería mostrar que el módulo ha desaparecido. `ifconfig -a` ya no debería listar `mynet0`. Si queda algo pendiente, veremos cómo diagnosticarlo en la sección 8.

### Lo que la pila sabe ahora sobre nosotros

Después de que `ether_ifattach` retorne, la pila conoce varios datos importantes sobre nuestra interfaz:

* Es de tipo `IFT_ETHER`.
* Admite broadcast, simplex y multicast.
* Tiene una dirección MAC específica.
* Tiene un MTU predeterminado de 1500 bytes.
* Tiene un callback de transmisión, un callback de ioctl, un callback de init y un manejador de medios.
* Está asociada a BPF con encapsulación `DLT_EN10MB`.
* Su estado del enlace es actualmente indefinido (aún no hemos llamado a `if_link_state_change`).

Todo lo demás, el movimiento de paquetes, la actualización de contadores y el estado del enlace, cobrará vida en las secciones siguientes. El esqueleto es intencionadamente pequeño. Es la primera vez que puedes señalar algo en tu sistema y decir, con toda honestidad, «esa es mi interfaz de red». Detente un momento en esa frase. Marca un hito real en el libro.

### Errores comunes

Hay dos errores fáciles de cometer en esta sección, y ambos producen síntomas confusos.

El primero es olvidarse de llamar a `ether_ifattach` y llamar directamente a `if_attach`. Eso es perfectamente válido y da lugar a una pseudo-interfaz no Ethernet, pero entonces tu driver tiene que instalar sus propios manejadores `if_input` e `if_output`, y `tcpdump` no funciona hasta que llames a `bpfattach` por tu cuenta. Si ves una interfaz que parece que debería funcionar pero `tcpdump -i mynet0` se queja del tipo de enlace, comprueba si usaste `ether_ifattach`.

El segundo error es asignar el softc con `M_NOWAIT` en lugar de `M_WAITOK`. `M_NOWAIT` es correcto en contexto de interrupción, pero `mynet_clone_create` se ejecuta en un contexto de usuario normal a través de la ruta de `ifconfig create`, y `M_WAITOK` es la elección correcta. Usar `M_NOWAIT` aquí introduce una rara ruta de fallo de asignación sin ningún beneficio.

### Cerrando la sección 3

Ya tienes un esqueleto funcional. La interfaz existe, está registrada, tiene una dirección Ethernet y puede crearse y destruirse a demanda. La pila está preparada para llamar a nuestro driver a través de `if_transmit`, `if_ioctl` e `if_init`, pero todavía no hemos implementado el cuerpo de esos callbacks. La sección 4 aborda la ruta de transmisión. Es la que sentirás de forma más visceral, porque una vez que funcione, `ping` empezará a empujar bytes reales a través de tu código.

## Sección 4: Gestión de la transmisión de paquetes

La transmisión es la mitad de salida del flujo de paquetes. Cuando la pila de red del kernel decide que un paquete debe salir por `mynet0`, lo empaqueta en una cadena de mbufs e invoca nuestro callback `if_transmit`. Nuestro trabajo consiste en aceptar el mbuf, hacer lo que corresponda con él y liberarlo. En esta sección construiremos una ruta de transmisión completa que valide el mbuf, actualice contadores, haga un tap en BPF para que `tcpdump` vea el paquete y descarte la trama. Como `mynet` es un pseudo-dispositivo sin un cable real, inicialmente descartaremos el paquete después de contabilizarlo. Eso es similar a lo que hace `disc(4)` en `/usr/src/sys/net/if_disc.c`, y es suficiente para demostrar el flujo de transmisión completo de extremo a extremo.

### Cómo llega la pila hasta nosotros

Antes de abrir el editor, rastreemos cómo llega un paquete desde un proceso hasta nuestro driver. Cuando un proceso llama a `send(2)` sobre un socket TCP vinculado a una dirección IP asignada a `mynet0`, ocurre la siguiente secuencia, a grandes rasgos. No te preocupes por memorizar cada paso; la idea es ver dónde se sitúa nuestro código en el panorama general.

1. La capa de socket copia el payload del usuario en mbufs y lo pasa a TCP.
2. TCP segmenta el payload, añade cabeceras TCP y pasa los segmentos a IP.
3. IP añade cabeceras IP, consulta la tabla de rutas y pasa el resultado a la capa Ethernet a través de `ether_output`.
4. `ether_output` resuelve la dirección MAC del siguiente salto (mediante ARP si es necesario), antepone una cabecera Ethernet y llama a `if_transmit` sobre la interfaz de salida.
5. Nuestra función `if_transmit` es invocada con `ifp` apuntando a `mynet0` y el mbuf apuntando a la trama Ethernet completa, lista para transmitirse.

A partir de ese momento, la trama es nuestra. Debemos enviarla, descartarla de forma limpia o ponerla en cola para su entrega posterior. Sea cual sea nuestra elección, debemos liberar el mbuf exactamente una vez. Un doble free provoca corrupción del kernel, un use-after-free conduce a panics misteriosos, y olvidar liberar produce una fuga de mbufs hasta que la máquina se queda sin ellos.

### La firma del callback de transmisión

El prototipo de un callback `if_transmit` es:

```c
int mynet_transmit(struct ifnet *ifp, struct mbuf *m);
```

Está declarado en `/usr/src/sys/net/if_var.h` como el typedef `if_transmit_fn_t`. El valor de retorno es un errno: cero en caso de éxito, o un error como `ENOBUFS` si el paquete no pudo encolarse. Los drivers de NIC reales raramente devuelven un valor distinto de cero, porque prefieren descartar silenciosamente e incrementar `IFCOUNTER_OERRORS`. Los pseudo-drivers que imitan el comportamiento real suelen hacer lo mismo.

Este es el callback completo que implementaremos:

```c
static int
mynet_transmit(struct ifnet *ifp, struct mbuf *m)
{
    struct mynet_softc *sc = ifp->if_softc;
    int len;

    if (m == NULL)
        return (0);
    M_ASSERTPKTHDR(m);

    /* Reject oversize frames. Leave a little slack for VLAN. */
    if (m->m_pkthdr.len > (ifp->if_mtu + sizeof(struct ether_vlan_header))) {
        m_freem(m);
        if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
        return (E2BIG);
    }

    /* If the interface is administratively down, drop. */
    if ((ifp->if_flags & IFF_UP) == 0 ||
        (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
        m_freem(m);
        if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
        return (ENETDOWN);
    }

    /* Let tcpdump see the outgoing packet. */
    BPF_MTAP(ifp, m);

    /* Count it. */
    len = m->m_pkthdr.len;
    if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
    if_inc_counter(ifp, IFCOUNTER_OBYTES, len);
    if (m->m_flags & (M_BCAST | M_MCAST))
        if_inc_counter(ifp, IFCOUNTER_OMCASTS, 1);

    /* In a real NIC we would DMA this to hardware. Here we just drop. */
    m_freem(m);
    return (0);
}
```

Vamos a repasarlo paso a paso. Aquí es donde la forma de una rutina de transmisión queda clara, así que vale la pena leer el código despacio.

### La comprobación de NULL

Las dos primeras líneas gestionan el caso defensivo en el que la pila nos llama con un puntero NULL. En principio esto no debería ocurrir en una operación normal, pero el kernel es un entorno donde la programación defensiva justifica su coste. Devolver `0` ante una entrada NULL es el idioma estándar; `if_epair.c` hace lo mismo al comienzo de `epair_transmit`.

### `M_ASSERTPKTHDR`

La siguiente línea es una macro de `/usr/src/sys/sys/mbuf.h` que verifica que el mbuf tiene `M_PKTHDR` establecido. Todo mbuf que llegue al callback de transmisión de un driver debe ser la cabeza de un paquete y, por tanto, debe llevar una cabecera de paquete válida. Esta aserción detecta errores causados por manipulaciones de mbufs en otras partes del sistema. En los kernels de producción la aserción se elimina durante la compilación, pero tenerla en el árbol de código fuente documenta el contrato, y en los kernels con `INVARIANTS` detecta usos incorrectos durante el desarrollo.

### Validación del MTU

El bloque bajo el comentario `/* Reject oversize frames. */` rechaza los paquetes cuyo tamaño supera el MTU de la interfaz más un margen pequeño para una cabecera VLAN. `epair_transmit` en `/usr/src/sys/net/if_epair.c` realiza exactamente la misma comprobación; busca el guardia `if (m->m_pkthdr.len > (ifp->if_mtu + sizeof(struct ether_vlan_header)))` que llama a `m_freem` sobre la trama e incrementa `IFCOUNTER_OERRORS`. Dejamos margen para `ether_vlan_header` porque las tramas con etiqueta VLAN llevan cuatro bytes adicionales más allá de la cabecera Ethernet base, y anunciamos `IFCAP_VLAN_MTU` en la sección 3, por lo que debemos respetar esa capacidad.

Al rechazar la trama, liberamos el mbuf con `m_freem(m)` e incrementamos `IFCOUNTER_OERRORS`. También devolvemos `E2BIG` como indicación al llamador, aunque en la práctica la pila raramente examina el valor de retorno más allá de decidir si descarta localmente.

### Validación del estado

El bloque `if` bajo el comentario `/* If the interface is administratively down, drop. */` comprueba dos condiciones. `IFF_UP` se establece con `ifconfig mynet0 up` y se borra con `ifconfig mynet0 down`, y es la forma que tiene el espacio de usuario de indicar si la interfaz debe o no cursar tráfico. `IFF_DRV_RUNNING` es el indicador interno del driver de «he asignado mis recursos y estoy listo para mover tráfico». Si alguno de los dos está a cero, no debemos enviar el paquete, así que lo descartamos e incrementamos el contador de errores.

Esta comprobación no es estrictamente necesaria para la corrección en todos los casos, porque la pila normalmente evita enrutar tráfico a través de una interfaz desactivada. Pero los drivers defensivos comprueban de todas formas, porque las condiciones de carrera entre la visión de estado de la pila y la del driver sí ocurren, especialmente durante el desmontaje de la interfaz.

### El tap de BPF

`BPF_MTAP(ifp, m)` es una macro que llama condicionalmente a BPF si hay alguna sesión de captura de paquetes activa en la interfaz. Se expande a `bpf_mtap_if((_ifp), (_m))` en el árbol actual. La macro está definida en `/usr/src/sys/net/bpf.h`. Cuando `tcpdump -i mynet0` está en ejecución, BPF se ha conectado al puntero `if_bpf` de la interfaz, y la macro le entrega una copia del paquete saliente. Cuando nadie está escuchando, la macro retorna rápidamente y tiene un coste despreciable.

La posición importa. Hacemos el tap antes de descartar, porque queremos que `tcpdump` vea el paquete incluso si estamos simulando una interfaz desactivada. Los drivers de NIC reales hacen el tap ligeramente antes, justo antes de entregar la trama al DMA del hardware, pero la idea es la misma.

### Actualización de contadores

Cuatro contadores son relevantes en cada transmisión:

* `IFCOUNTER_OPACKETS`: el número de paquetes transmitidos.
* `IFCOUNTER_OBYTES`: el total de bytes transmitidos.
* `IFCOUNTER_OMCASTS`: el número de tramas multicast o broadcast transmitidas.
* `IFCOUNTER_OERRORS`: el número de errores observados durante la transmisión.

`if_inc_counter(ifp, IFCOUNTER_*, n)` es la forma correcta de actualizar estos contadores. Está definida en `/usr/src/sys/net/if.c` y usa contadores por CPU internamente para que las llamadas concurrentes desde múltiples CPUs no compitan entre sí. No accedas directamente a los campos de `if_data`: los detalles internos han cambiado a lo largo de los años, y el accessor es la interfaz estable.

Dado que la pila ya ha calculado la longitud del paquete y ha rellenado `m->m_pkthdr.len`, guardamos ese valor en una variable local `len` antes de liberar el mbuf. Leer `m->m_pkthdr.len` después de `m_freem(m)` sería un use-after-free, así que la variable local no es una elección estilística. Es una elección de corrección.

### La liberación final

`m_freem(m)` libera una cadena de mbuf completa. Recorre la cadena a través de los punteros `m_next` y libera cada mbuf que la compone. No necesitas liberar cada uno a mano. Si solo usaras `m_free(m)`, liberarías el primer mbuf y perderías el resto. Confundir `m_freem` con `m_free` es uno de los errores más habituales entre quienes empiezan. Los nombres convencionales son:

* `m_free(m)`: libera un único mbuf. Raramente se llama en drivers.
* `m_freem(m)`: libera una cadena completa. Es lo que casi siempre querrás usar.

En un driver de NIC real, en lugar de `m_freem(m)`, pasaríamos el frame al DMA del hardware y liberaríamos el mbuf más tarde, en la interrupción de finalización de transmisión. En nuestro pseudo-driver, simplemente lo descartamos. Este es el comportamiento de `if_disc.c` en el árbol: simular la transmisión, liberar el mbuf y retornar.

### La callback de vaciado de cola

Junto a `if_transmit`, la pila espera una callback trivial llamada `if_qflush`. Se invoca cuando la pila desea vaciar los paquetes que el driver tiene en cola internamente. Como nuestro driver no mantiene ninguna cola, la callback no tiene nada que hacer:

```c
static void
mynet_qflush(struct ifnet *ifp __unused)
{
}
```

Es idéntica a `epair_qflush` en `/usr/src/sys/net/if_epair.c`. Los drivers que mantienen sus propias colas de paquetes, lo cual es menos habitual hoy de lo que era antes, tienen más trabajo que hacer aquí. Nosotros, no.

### La callback `mynet_init`

La tercera callback asignada en la sección 3 fue `mynet_init`, la función que la pila invoca cuando la interfaz pasa al estado activo. Para nosotros, es sencilla:

```c
static void
mynet_init(void *arg)
{
    struct mynet_softc *sc = arg;

    MYNET_LOCK(sc);
    sc->running = true;
    sc->ifp->if_drv_flags |= IFF_DRV_RUNNING;
    sc->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
    callout_reset(&sc->rx_callout, sc->rx_interval_hz,
        mynet_rx_timer, sc);
    MYNET_UNLOCK(sc);

    if_link_state_change(sc->ifp, LINK_STATE_UP);
}
```

Al inicializarse, nos marcamos como en ejecución, limpiamos `IFF_DRV_OACTIVE` (un flag que significa "la cola de transmisión está llena, no me llames de nuevo hasta que lo limpie"), arrancamos el callout de simulación de recepción que describiremos en la sección 5 y anunciamos que el enlace está activo. La llamada a `if_link_state_change` al final hace que `ifconfig` informe de `status: active` en esta interfaz. Ten en cuenta el orden: primero establecemos `IFF_DRV_RUNNING` y luego anunciamos el enlace, en ese orden. Invertir el orden le indicaría a la pila que el enlace está activo en una interfaz cuyo driver todavía está inicializándose, y la pila podría comenzar a enviarnos tráfico antes de que estuviéramos listos.

### Un vistazo al orden y al locking

El código anterior es suficientemente simple como para que el locking parezca excesivo. ¿Por qué necesitamos un mutex? Hay dos razones.

La primera es que `if_transmit` y `if_ioctl` se ejecutan de forma concurrente. La pila puede llamar a `if_transmit` en un CPU mientras el espacio de usuario ejecuta `ifconfig mynet0 down` en otro, lo que se traduce en que `if_ioctl(SIOCSIFFLAGS)` se ejecuta en ese otro CPU. Sin un mutex, ambas callbacks pueden leer y escribir el estado del softc simultáneamente. El mutex es lo que nos permite razonar sobre las transiciones de estado.

La segunda razón es que la simulación de recepción basada en callout de la sección 5 accede al softc cuando se dispara. Sin un mutex, el callout y `if_ioctl` pueden colisionar, y aparece el clásico error de tipo "la lista que estaba recorriendo acaba de cambiar bajo mis pies". De nuevo, un único mutex por softc es suficiente para que estas interacciones sean seguras.

Hemos elegido una regla de locking sencilla: el mutex del softc es el lock global. Todo acceso al softc fuera del camino rápido de transmisión lo adquiere. El camino rápido de transmisión en `mynet_transmit` no adquiere el mutex, porque `if_transmit` está diseñado para llamantes concurrentes y solo tocamos contadores de ifnet y BPF, ambos seguros para threads por sí mismos. Si añadiéramos estado compartido específico del driver que la transmisión actualice, añadiríamos un lock más granular para ese estado.

Esto es una simplificación. Los drivers de NIC de alto rendimiento reales utilizan un locking mucho más complejo, a menudo con locks por cola, estado por CPU y comprobaciones de integridad por paquete. El diseño de mutex único es perfectamente válido para un pseudo-driver y para cualquier interfaz de baja velocidad; en un driver de 100 gigabits en producción se convertiría en un cuello de botella, que es una de las razones por las que existe el framework moderno iflib. Hablaremos de iflib en capítulos posteriores.

### Cirugía de paquetes con `m_pullup`

Los drivers de red reales necesitan con frecuencia leer campos del interior de un paquete antes de decidir qué hacer con él. Un driver VLAN necesita leer la etiqueta 802.1Q. Un driver de bridging necesita leer la MAC de origen para actualizar la tabla de reenvío. Un driver con offload de hardware necesita leer las cabeceras IP y TCP para decidir si un checksum puede calcularse en hardware.

El problema es que una cadena de mbuf recibida no garantiza que un byte concreto resida en un mbuf concreto. El primer mbuf podría contener solo los primeros catorce bytes (la cabecera Ethernet) mientras el siguiente mbuf contiene el resto. Un driver que hace un cast con `mtod(m, struct ip *)` y accede más allá de la cabecera Ethernet leerá basura a menos que primero garantice que los bytes que necesita son contiguos.

El kernel proporciona `m_pullup(m, len)` exactamente para este propósito. `m_pullup` garantiza que los primeros `len` bytes de la cadena de mbuf residen en el primer mbuf. Si ya es así, no hace nada. Si no, remodela la cadena moviendo bytes al primer mbuf, posiblemente asignando un nuevo mbuf si el primero es demasiado pequeño. Devuelve un puntero mbuf (posiblemente diferente), o NULL en caso de fallo de asignación, en cuyo caso la cadena de mbuf ya ha sido liberada automáticamente.

El patrón habitual para un driver que necesita inspeccionar cabeceras es:

```c
m = m_pullup(m, sizeof(struct ether_header) + sizeof(struct ip));
if (m == NULL) {
    if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
    return;
}
eh = mtod(m, struct ether_header *);
ip = (struct ip *)(eh + 1);
```

`mynet` no necesita hacer esto, porque no inspeccionamos el contenido de los paquetes en el camino de transmisión. Pero verás `m_pullup` repartido por todos los drivers reales, especialmente en el lado de recepción y en los helpers de L2.

Una función relacionada, `m_copydata(m, offset, len, buf)`, copia bytes de una cadena de mbuf a un buffer proporcionado por el llamante. Es la herramienta adecuada cuando quieres leer algunos bytes sin modificar la cadena. `m_copyback` hace lo contrario: escribe bytes en una cadena a un desplazamiento dado, extendiendo la cadena si es necesario.

Otro helper de uso frecuente es `m_defrag(m, how)`, que aplana una cadena en un único mbuf de mayor tamaño. Lo usan los drivers cuyo hardware tiene un límite máximo de entradas scatter-gather. Si un frame de transmisión abarca más mbufs de los que el hardware puede gestionar, el driver recurre a `m_defrag`, que copia el payload en un único cluster contiguo.

Te encontrarás con todas estas funciones al leer drivers reales. Por ahora, es suficiente saber que existen y que la disposición del mbuf es algo que un driver real debe tomar en serio.

### Una mirada más profunda a la estructura del mbuf

Dado que los mbufs son la moneda corriente de la pila de red, dedicar unas páginas más a su estructura es tiempo bien invertido. Las decisiones que toma un driver sobre los mbufs son las que determinan si el driver es rápido, correcto y mantenible.

La estructura mbuf en sí reside en `/usr/src/sys/sys/mbuf.h`. La estructura tal como aparece en el código fuente de FreeBSD 14.3 es algo parecido a esto (simplificada para fines didácticos):

```c
struct mbuf {
    struct m_hdr    m_hdr;      /* fields common to every mbuf */
    union {
        struct {
            struct pkthdr m_pkthdr;  /* packet header, if M_PKTHDR */
            union {
                struct m_ext m_ext;  /* external storage, if M_EXT */
                char         m_pktdat[MLEN - sizeof(struct pkthdr)];
            } MH_dat;
        } MH;
        char    M_databuf[MLEN]; /* when no packet header */
    } M_dat;
};
```

Dos variantes union dentro de dos unions. La disposición refleja el hecho de que un mbuf puede estar en uno de varios modos:

* Un mbuf simple con sus datos almacenados en línea (unos 200 bytes disponibles).
* Un mbuf con cabecera de paquete y datos almacenados en línea (algo menos disponible a causa de la cabecera).
* Un mbuf con cabecera de paquete y datos almacenados en un cluster externo (`m_ext`).
* Un mbuf sin cabecera con sus datos almacenados en un cluster externo.

El campo `m_flags` indica qué variante está activa mediante los bits `M_PKTHDR` y `M_EXT`.

Un cluster es un buffer preasignado de mayor tamaño, típicamente 2048 bytes en FreeBSD moderno. El mbuf contiene un puntero al cluster en `m_ext.ext_buf`, y el cluster se gestiona mediante conteo de referencias a través de `m_ext.ext_count`. Los clusters existen porque muchos paquetes son más grandes de lo que puede contener un mbuf simple, y asignar un nuevo buffer para cada paquete grande resultaría costoso.

Cuando llamas a `MGETHDR(m, M_NOWAIT, MT_DATA)`, obtienes un mbuf con cabecera de paquete y datos en línea. Cuando llamas a `m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR)`, obtienes un mbuf con cabecera de paquete y un cluster externo adjunto. La segunda forma puede contener unos 2000 bytes sin encadenamiento, lo cual resulta conveniente para paquetes de tamaño Ethernet.

### Cadenas de mbuf y scatter-gather

Dado que un único mbuf solo puede contener un número limitado de bytes, muchos paquetes abarcan varios mbufs encadenados mediante `m_next`. El campo `m_pkthdr.len` del mbuf inicial contiene la longitud total del paquete; el `m_len` de cada mbuf de la cadena contiene la contribución de ese mbuf. Su relación es `m_pkthdr.len == sum(m_len across chain)`, y cualquier discrepancia es un error.

Este encadenamiento tiene varias ventajas. Permite a la pila anteponer cabeceras de forma económica: para añadir una cabecera Ethernet, la pila puede asignar un nuevo mbuf, rellenar la cabecera y enlazarlo como nuevo inicio. Permite a la pila dividir paquetes de forma económica: TCP puede segmentar un payload grande recorriendo una cadena en lugar de copiar datos. Permite al hardware usar DMA de tipo scatter-gather: una NIC puede transmitir una cadena emitiendo múltiples descriptores DMA, uno por mbuf.

El inconveniente es que los drivers deben recorrer las cadenas con cuidado. Si haces un cast con `mtod(m, struct ip *)` y la cabecera IP está dividida entre el primero y el segundo mbuf, leerás basura. `m_pullup` es la defensa contra ese error, y todo driver serio lo utiliza cuando necesita inspeccionar cabeceras.

### Tipos de mbuf y su significado

El campo `m_type` de cada mbuf clasifica su propósito:

* `MT_DATA`: datos de paquete ordinarios. Es lo que se usa para paquetes de red.
* `MT_HEADER`: un mbuf dedicado a almacenar cabeceras de protocolo.
* `MT_SONAME`: una estructura de dirección de socket. La usa el código de la capa de sockets.
* `MT_CONTROL`: datos de control auxiliares de socket.
* `MT_NOINIT`: un mbuf sin inicializar. Los drivers nunca lo ven.

Para el código de driver, `MT_DATA` es casi siempre la opción correcta. La pila gestiona los demás internamente.

### Campos de la cabecera de paquete

La estructura `m_pkthdr` en un mbuf de cabecera contiene campos que viajan con el paquete a través de la pila. Algunos de los más relevantes para quienes desarrollan drivers:

* `len`: longitud total de la cadena de mbuf.
* `rcvif`: la interfaz en la que se recibió el paquete. Los drivers lo establecen al construir un mbuf de recepción.
* `flowid` y `rsstype`: hash del flujo del paquete, utilizado para el despacho a múltiples colas.
* `csum_flags` y `csum_data`: estado del checksum por hardware. Los drivers con offload de checksum en TX los leen; los drivers con offload de checksum en RX los escriben.
* `ether_vtag` y el flag `M_VLANTAG` en `m_flags`: etiqueta VLAN extraída por hardware, si se utiliza el etiquetado VLAN por hardware.
* `vt_nrecs` y otros campos VLAN: para configuraciones VLAN más elaboradas.
* `tso_segsz`: tamaño de segmento para frames TSO.

La mayoría de estos campos son establecidos por capas superiores antes de que el paquete llegue al driver. Para nuestros propósitos, establecer `rcvif` durante la recepción y leer `len` durante la transmisión es suficiente. Los demás campos son hooks que iflib y sus predecesores usan para la coordinación del offload; un pseudo-driver puede ignorarlos sin problema.

### Buffers externos con conteo de referencias

Cuando un cluster está adjunto a un mbuf, el cluster se gestiona mediante conteo de referencias. Esto permite la duplicación de paquetes (mediante `m_copypacket`) sin copiar el payload: dos mbufs pueden compartir el mismo cluster, y el cluster solo se libera cuando ambos mbufs liberan su referencia. BPF utiliza este mecanismo para interceptar un paquete sin forzar una copia.

Para el código de driver, esto es en gran medida transparente. Llamas a `m_freem` sobre tu mbuf, y si el mbuf tiene un cluster externo, el conteo de referencias del cluster se decrementa; si llega a cero, el cluster se libera. No tienes que pensar en los conteos de referencias de forma explícita. Pero debes saber que existen, porque explican por qué `BPF_MTAP` puede ser económico: no copia el paquete, simplemente adquiere una referencia adicional.

### El patrón de asignación para recepción

Un driver de NIC real normalmente asigna mbufs y les adjunta clusters en tiempo de inicialización, rellena el anillo de recepción con esos mbufs y deja que el hardware haga DMA en ellos. El patrón es:

```c
for (i = 0; i < RX_RING_SIZE; i++) {
    struct mbuf *m = m_getcl(M_WAITOK, MT_DATA, M_PKTHDR);
    rx_ring[i].mbuf = m;
    rx_ring[i].dma_addr = pmap_kextract((vm_offset_t)mtod(m, char *));
    rx_ring[i].desc->addr = rx_ring[i].dma_addr;
    rx_ring[i].desc->status = 0;
}
```

Cuando el hardware recibe un paquete, escribe los datos del paquete en el cluster apuntado por uno de los descriptores, establece el estado para indicar que la operación ha finalizado y lanza una interrupción. La rutina de recepción del driver examina el estado, toma el mbuf, establece `m->m_pkthdr.len` y `m->m_len` a partir del campo de longitud del descriptor, hace el tap de BPF, llama a `if_input` y, a continuación, asigna un mbuf de sustitución para el descriptor.

Nuestro pseudo-driver usa un patrón mucho más sencillo: asignar un mbuf nuevo cada vez que el temporizador de recepción se dispara. Esto es perfectamente válido para un driver de enseñanza, porque la tasa de asignación es baja. Con tasas más altas querrías el patrón de preasignación, ya que asignar mbufs en bloque durante la inicialización y reciclarlos resulta mucho más barato que asignar uno por paquete.

### Errores comunes relacionados con mbuf

Incluso conociendo lo anterior, algunos errores aparecen con frecuencia en el código de los drivers:

* Usar `m_free` en lugar de `m_freem` sobre la cabeza de una cadena. Se libera el primer mbuf y se pierde el resto.
* Olvidar establecer `m_pkthdr.len` correctamente al construir un paquete. La pila lee `m_pkthdr.len` en lugar de recorrer la cadena, por lo que si los dos valores no coinciden, la decodificación falla en silencio.
* Leer `m_pkthdr.len` después de llamar a `m_freem`. Guarda siempre la longitud en una variable local antes de liberar.
* Confundir `m->m_len` (longitud de este mbuf) con `m->m_pkthdr.len` (longitud total de la cadena). Para un paquete de un único mbuf son iguales; para cadenas difieren.
* Leer más allá de `m_len` sin recorrer la cadena. Si necesitas bytes que están más allá del primer mbuf, usa `m_pullup` o `m_copydata`.
* Modificar un mbuf que no te pertenece. Una vez que le has entregado un mbuf a `if_input`, ya no es tuyo.
* Asignar memoria sin comprobar si el resultado es NULL. `m_gethdr(M_NOWAIT, ...)` puede devolver NULL bajo presión de memoria, y el driver debe gestionar esa situación con elegancia.

Estos errores son fáciles de evitar si conoces las reglas, y leer otros drivers es la mejor forma de interiorizarlas.

### Transmisión multi-cola en drivers reales

Las NIC de hardware modernas pueden transmitir en muchas colas en paralelo. Una NIC de 10 gigabits suele tener ocho o dieciséis colas de transmisión, cada una con su propio ring buffer de hardware, sus propios descriptores DMA y su propia interrupción de finalización. El driver distribuye los paquetes salientes entre estas colas en función de un hash de las direcciones de origen y destino del paquete, de modo que el tráfico de distintos flujos va a colas diferentes y puede procesarse de forma concurrente en diferentes núcleos de CPU.

Esto está muy por encima de lo que necesita nuestro pseudo-driver. Pero el patrón merece conocerse, porque aparece con mucha presencia en los drivers de producción. Las piezas clave son:

* Una función de selección de cola que recibe un mbuf y devuelve un índice en el array de colas del driver. `mynet` tiene una sola cola (o ninguna, según cómo se cuente), así que este paso es trivial. Los drivers reales suelen usar `m->m_pkthdr.flowid` como hash precalculado.
* Un lock por cola y una cola software por cola (gestionada habitualmente con `drbr(9)`) que permite que varios productores concurrentes encolen paquetes sin contención.
* Un kick de transmisión que vacía la cola software hacia el hardware cuando un productor ha encolado algo y el hardware está inactivo.
* Un callback de finalización, normalmente procedente de una interrupción de hardware, que libera los mbufs cuya transmisión ha completado.

El prototipo de `if_transmit` está diseñado para encajar con este patrón de forma natural. El llamante produce un mbuf y se lo entrega a `if_transmit`. El driver lo encola de inmediato (en un caso sencillo como el nuestro) o lo despacha a la cola de hardware apropiada (en un caso multi-cola). En cualquier caso, el llamante ve una sola llamada a función y no necesita saber cuántas colas hay por debajo.

Volveremos al diseño multi-cola cuando hablemos de iflib en un capítulo posterior. Por ahora, basta con saber que el modelo de cola única que estamos construyendo aquí es una simplificación que los drivers reales elaboran.

### Una digresión sobre el helper `drbr(9)`

`drbr` significa "driver ring buffer" y es una librería auxiliar para drivers que quieren mantener su propia cola software por cola. La API está definida e implementada como funciones `static __inline` en `/usr/src/sys/net/ifq.h`; no hay ningún archivo `drbr.c` ni `drbr.h` separado. Los helpers envuelven los ring buffers subyacentes de `buf_ring(9)` con operaciones explícitas de encolado y desencolado, además de helpers para conectar BPF, contar paquetes y sincronizar con el thread de transmisión. La forma para la que está construido `drbr` es múltiples productores y un único consumidor, que es la forma típica de una cola de transmisión donde muchos threads encolan pero un único thread de desencolado vacía el anillo hacia el hardware.

Un driver que usa `drbr` tiene habitualmente una función de transmisión con este aspecto:

```c
int
my_transmit(struct ifnet *ifp, struct mbuf *m)
{
    struct mydrv_softc *sc = ifp->if_softc;
    struct mydrv_txqueue *txq = select_queue(sc, m);
    int error;

    error = drbr_enqueue(ifp, txq->br, m);
    if (error)
        return (error);
    taskqueue_enqueue(txq->tq, &txq->tx_task);
    return (0);
}
```

El productor encola en un ring buffer y activa un taskqueue. El consumidor del taskqueue desencola entonces del ring buffer y entrega los frames al hardware. Esto desacopla al productor (que puede ser cualquier CPU) del consumidor (que se ejecuta en un thread worker dedicado por cola), que es exactamente la estructura que funciona bien en sistemas multinúcleo.

`mynet` no usa `drbr`, porque no tenemos ni múltiples colas ni hardware al que activar. Pero el patrón merece verse una vez, porque aparece en todos los drivers del árbol que se preocupan por el rendimiento.

### Prueba del camino de transmisión

Compila, carga y crea la interfaz como en la Sección 3, y envíale tráfico:

```console
# kldload ./mynet.ko
# ifconfig mynet create
mynet0
# ifconfig mynet0 inet 192.0.2.1/24 up
# ping -c 1 192.0.2.99
PING 192.0.2.99 (192.0.2.99): 56 data bytes
--- 192.0.2.99 ping statistics ---
1 packets transmitted, 0 packets received, 100.0% packet loss
# netstat -in -I mynet0
Name    Mtu Network     Address              Ipkts Ierrs ...  Opkts Oerrs
mynet0 1500 <Link#12>   02:a3:f1:22:bc:0d        0     0        1     0
mynet0    - 192.0.2.0/24 192.0.2.1                0     -        0     -
```

La línea clave es `Opkts 1`. Aunque el ping no recibió respuesta, podemos ver que un paquete se transmitió a través de nuestro driver. El motivo de que no haya respuesta es que `mynet0` es una pseudo-interfaz sin nada al otro lado. Le daremos un camino de llegada simulado en la Sección 5.

Deja `tcpdump -i mynet0 -n` ejecutándose en otra terminal, repite el `ping` y verás la petición ARP saliente y el paquete IPv4 siendo capturados. Eso confirma que `BPF_MTAP` está conectado correctamente.

### Errores frecuentes

Un puñado de errores aparecen repetidamente en el código de estudiantes e incluso en drivers con experiencia. Veámoslos para que aprendas a reconocerlos.

**Liberar el mbuf dos veces.** Si tu función de transmisión tiene múltiples caminos de salida y alguno de ellos olvida saltarse `m_freem`, el mismo mbuf acaba liberándose dos veces. El kernel suele entrar en pánico con un mensaje sobre una lista de libres corrupta. La solución es estructurar la función con una única salida que sea la propietaria de la liberación, o anular `m` después de liberarlo y comprobarlo antes de volver a liberar.

**No liberar el mbuf en absoluto.** El otro lado del mismo error. Si vuelves de `if_transmit` sin liberar ni encolar el mbuf, lo pierdes. En un driver de baja tasa esto puede tardar horas en notarse; en un driver de alta tasa la máquina se queda sin memoria de mbufs rápidamente. `vmstat -z | grep mbuf` es tu mejor aliado para detectar esto.

**Asumir que el mbuf cabe en un único bloque de memoria.** Incluso una trama Ethernet sencilla puede estar repartida entre varios mbufs en cadena, especialmente tras la fragmentación IP o la segmentación TCP. Si necesitas examinar las cabeceras, usa `m_pullup` para traerlas al primer mbuf, o recorre la cadena con cuidado.

**Olvidar conectar BPF.** `tcpdump -i mynet0` seguirá funcionando para los paquetes recibidos pero se perderá los transmitidos, y tu depuración será más difícil porque las dos mitades de la conversación parecerán asimétricas.

**Actualizar contadores después de `m_freem`.** Ya lo mencionamos antes. Lee siempre `m->m_pkthdr.len` en una variable local antes de liberar, o realiza todas tus actualizaciones de contadores antes de liberar.

**Llamar a `if_link_state_change` con el argumento incorrecto.** `LINK_STATE_UP`, `LINK_STATE_DOWN` y `LINK_STATE_UNKNOWN` son los tres valores definidos en `/usr/src/sys/net/if.h`. Pasar un entero aleatorio como `1` puede coincidir por casualidad con `LINK_STATE_DOWN`, pero hace el código ilegible y frágil.

### Cerrando la Sección 4

El camino de transmisión es la demostración más clara de cómo cooperan la pila de red y el driver. Aceptamos un mbuf, lo validamos, lo contamos, dejamos que BPF lo vea y lo liberamos. Los drivers de hardware real añaden DMA y rings de hardware en la parte inferior; el esqueleto permanece igual.

Nos falta una pieza importante: el camino de recepción. Sin él, nuestra interfaz habla pero nunca escucha. La Sección 5 construye esa mitad.

## Sección 5: Gestión de la recepción de paquetes

La recepción es la mitad entrante del flujo de paquetes. Los paquetes llegan desde el transporte, y el driver es responsable de convertirlos en mbufs, mostrárselos a BPF y entregarlos a la pila a través de `if_input`. En un driver de NIC real, la llegada es una interrupción o la finalización de un descriptor de anillo. En nuestro pseudo-driver, simularemos la llegada con un callout que se dispara cada segundo y construye un paquete sintético. El mecanismo es artificial, pero el camino de código es idéntico al que siguen los drivers reales después del desencolado inicial del descriptor de anillo.

### La dirección del callback

La transmisión fluye hacia abajo: la pila llama al driver. La recepción fluye hacia arriba: el driver llama a la pila. No registras un callback de recepción para que la pila lo invoque. En cambio, cuando llega un paquete, llamas a `if_input(ifp, m)` (o de forma equivalente `(*ifp->if_input)(ifp, m)`) y la pila toma el control. `ether_ifattach` se encargó de que `ifp->if_input` apuntara a `ether_input`, así que cuando llamamos a `if_input` la capa Ethernet recibe la trama, elimina la cabecera Ethernet, despacha según el EtherType y entrega el payload a IPv4, IPv6, ARP o donde corresponda.

Este es un cambio mental importante respecto a `if_transmit`. La pila no hace polling de tu driver. Espera a que la llamen. Tu driver es la parte activa en la recepción. Cuando tienes una trama lista, haces la llamada. La pila hace el resto.

### La llegada simulada

Vamos a construir un camino de llegada simulado. La idea: una vez por segundo, despertarse, construir un mbuf pequeño que contenga una trama Ethernet válida y entregársela a la pila. La trama será una petición ARP de broadcast dirigida a una dirección IP inexistente. Es fácil de construir, útil para las pruebas porque `tcpdump` la mostrará claramente, e inofensiva para el resto del sistema.

Primero, el manejador del callout:

```c
static void
mynet_rx_timer(void *arg)
{
    struct mynet_softc *sc = arg;
    struct ifnet *ifp = sc->ifp;

    MYNET_ASSERT(sc);
    if (!sc->running) {
        return;
    }
    callout_reset(&sc->rx_callout, sc->rx_interval_hz,
        mynet_rx_timer, sc);
    MYNET_UNLOCK(sc);

    mynet_rx_fake_arp(sc);

    MYNET_LOCK(sc);
}
```

El callout se inicializa con `callout_init_mtx` y el mutex del softc, de modo que el sistema adquiere nuestro mutex antes de llamarnos. Eso nos da `MYNET_ASSERT` de forma gratuita: el lock ya está tomado. Comprobamos si seguimos en ejecución, reprogramamos el temporizador para el siguiente tick, soltamos el lock, realizamos el trabajo real y volvemos a adquirir el lock al terminar. Soltar el lock es importante, porque `if_input` puede tomarse su tiempo y puede adquirir otros locks. Llamar hacia arriba a la pila mientras se sostiene un mutex del driver es una receta para inversiones de orden de lock.

A continuación, la construcción del paquete en sí:

```c
static void
mynet_rx_fake_arp(struct mynet_softc *sc)
{
    struct ifnet *ifp = sc->ifp;
    struct mbuf *m;
    struct ether_header *eh;
    struct arphdr *ah;
    uint8_t *payload;
    size_t frame_len;

    frame_len = sizeof(*eh) + sizeof(*ah) + 2 * (ETHER_ADDR_LEN + 4);
    MGETHDR(m, M_NOWAIT, MT_DATA);
    if (m == NULL) {
        if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
        return;
    }

    m->m_pkthdr.len = m->m_len = frame_len;
    m->m_pkthdr.rcvif = ifp;

    eh = mtod(m, struct ether_header *);
    memset(eh->ether_dhost, 0xff, ETHER_ADDR_LEN);   /* broadcast */
    memcpy(eh->ether_shost, sc->hwaddr, ETHER_ADDR_LEN);
    eh->ether_type = htons(ETHERTYPE_ARP);

    ah = (struct arphdr *)(eh + 1);
    ah->ar_hrd = htons(ARPHRD_ETHER);
    ah->ar_pro = htons(ETHERTYPE_IP);
    ah->ar_hln = ETHER_ADDR_LEN;
    ah->ar_pln = 4;
    ah->ar_op  = htons(ARPOP_REQUEST);

    payload = (uint8_t *)(ah + 1);
    memcpy(payload, sc->hwaddr, ETHER_ADDR_LEN);     /* sender MAC */
    payload += ETHER_ADDR_LEN;
    memset(payload, 0, 4);                            /* sender IP 0.0.0.0 */
    payload += 4;
    memset(payload, 0, ETHER_ADDR_LEN);               /* target MAC */
    payload += ETHER_ADDR_LEN;
    memcpy(payload, "\xc0\x00\x02\x63", 4);          /* target IP 192.0.2.99 */

    BPF_MTAP(ifp, m);

    if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
    if_inc_counter(ifp, IFCOUNTER_IBYTES, frame_len);

    if_input(ifp, m);
}
```

Hay mucho que desgranar, pero la mayor parte es sencilla. Veámoslo paso a paso.

### `MGETHDR`: asignación de la cabeza de la cadena

`MGETHDR(m, M_NOWAIT, MT_DATA)` asigna un nuevo mbuf y lo prepara como cabeza de una cadena de paquetes. Se expande a `m_gethdr(M_NOWAIT, MT_DATA)` a través del bloque de macros de compatibilidad en `/usr/src/sys/sys/mbuf.h` (la entrada `#define MGETHDR(m, how, type) ((m) = m_gethdr((how), (type)))`, justo al lado de `MGET` y `MCLGET`). `M_NOWAIT` le dice al asignador que falle en lugar de dormir, lo que es apropiado porque podemos ejecutarnos en contextos donde dormir está prohibido (este callback en concreto es un callout, que no puede dormir). `MT_DATA` es el tipo de mbuf para datos genéricos.

En caso de fallo en la asignación, incrementamos `IFCOUNTER_IQDROPS` (descartes de cola de entrada) y retornamos. Los descartes causados por escasez de mbufs se cuentan así en la mayoría de los drivers.

### Establecimiento de los campos de la cabecera del paquete

Una vez que tenemos el mbuf, establecemos tres campos en la cabecera del paquete:

* `m->m_pkthdr.len`: la longitud total del paquete. Es la suma de `m_len` a lo largo de la cadena. Para un paquete de un único mbuf como el nuestro, `m_pkthdr.len` es igual a `m_len`.
* `m->m_len`: la longitud de los datos en este mbuf. Almacenamos toda la trama en el primero (y único) mbuf.
* `m->m_pkthdr.rcvif`: la interfaz por la que llegó el paquete. La pila lo usa para las decisiones de enrutamiento y para los informes.

Un mbuf pequeño (de unos 256 bytes) contiene cómodamente nuestra trama Ethernet ARP de 42 bytes. Si estuviéramos construyendo una trama más grande, usaríamos `MGET` y buffers externos, o `m_getcl` para un mbuf respaldado por un cluster, o encadenaríamos varios mbufs. Revisitaremos esos patrones en capítulos posteriores.

### Escritura de la cabecera Ethernet

`mtod(m, struct ether_header *)` es una macro de `/usr/src/sys/sys/mbuf.h` que convierte `m_data` en un puntero al tipo solicitado. Significa "mbuf to data". La usamos para obtener un puntero `struct ether_header` escribible al inicio del paquete y rellenamos la MAC de destino (broadcast `ff:ff:ff:ff:ff:ff`), la MAC de origen (la MAC de nuestra interfaz) y el EtherType (`ETHERTYPE_ARP`, en orden de bytes de red).

La cabecera Ethernet es la encapsulación mínima de capa 2 que la pila espera en nuestra interfaz, porque la registramos con `ether_ifattach`. `ether_input` eliminará esta cabecera y despachará según el EtherType.

### Construcción del cuerpo ARP

Tras la cabecera Ethernet viene la cabecera ARP propiamente dicha, seguida del payload ARP (MAC del emisor, IP del emisor, MAC del destinatario, IP del destinatario). Los nombres de campo y las constantes proceden de `/usr/src/sys/net/if_arp.h`. Asignamos una MAC de emisor real (la nuestra), una IP de emisor de `0.0.0.0`, una MAC de destinatario a cero y una IP de destinatario de `192.0.2.99`. Esa última dirección pertenece al rango TEST-NET-1 reservado por RFC 5737 para documentación y ejemplos, lo que la convierte en una elección responsable para un paquete sintético que nunca abandonará nuestro sistema.

Nada de esto es código ARP de producción. No intentamos resolver nada. Estamos generando una trama bien formada que la capa de entrada Ethernet reconocerá, analizará, registrará en contadores y descartará (porque la IP de destino no nos pertenece). Es exactamente el nivel de realismo adecuado para un driver didáctico.

### Entrega a BPF

`BPF_MTAP(ifp, m)` da a `tcpdump` la oportunidad de ver la trama entrante. Hacemos el tap antes de llamar a `if_input`, porque `if_input` puede modificar el mbuf de formas que harían que el tap mostrase datos confusos. Los drivers reales siempre hacen el tap antes de consumir.

### Incremento de contadores de entrada

`IFCOUNTER_IPACKETS` e `IFCOUNTER_IBYTES` cuentan los paquetes y bytes recibidos, respectivamente. Si la trama es broadcast o multicast, también incrementaríamos `IFCOUNTER_IMCASTS`. Lo omitimos aquí por brevedad, pero el archivo complementario completo lo incluye.

### Llamada a `if_input`

`if_input(ifp, m)` es el paso final. Es una función auxiliar inline en `/usr/src/sys/net/if_var.h` que desreferencia `ifp->if_input` (que `ether_ifattach` estableció apuntando a `ether_input`) e invoca a esa función. A partir de ese momento, el mbuf es responsabilidad de la pila. Si la pila acepta el paquete, lo usa y finalmente lo libera. Si la pila rechaza el paquete, lo libera e incrementa `IFCOUNTER_IERRORS`. En cualquier caso, no debemos volver a tocar `m`.

Esta es la regla complementaria a la transmisión: en transmisión, el driver es dueño del mbuf hasta que se libera o se entrega al hardware; en recepción, la pila toma posesión en el momento en que llamas a `if_input`. Respetar estas reglas de propiedad es la disciplina más importante al escribir drivers de red.

### Verificación del camino de recepción

Compila y carga el driver actualizado, levanta la interfaz y observa `tcpdump`:

```console
# kldload ./mynet.ko
# ifconfig mynet create
mynet0
# ifconfig mynet0 inet 192.0.2.1/24 up
# tcpdump -i mynet0 -n
tcpdump: verbose output suppressed, use -v or -vv for full protocol decode
listening on mynet0, link-type EN10MB (Ethernet), capture size 262144 bytes
14:22:01.000 02:a3:f1:22:bc:0d > ff:ff:ff:ff:ff:ff, ethertype ARP, Request who-has 192.0.2.99 tell 0.0.0.0, length 28
14:22:02.000 02:a3:f1:22:bc:0d > ff:ff:ff:ff:ff:ff, ethertype ARP, Request who-has 192.0.2.99 tell 0.0.0.0, length 28
...
```

Cada segundo deberías ver pasar una petición ARP sintetizada. Si a continuación compruebas `netstat -in -I mynet0`, el contador `Ipkts` debería estar aumentando. La pila acepta el paquete, inspecciona el ARP, decide que no es una pregunta dirigida a ella (porque `192.0.2.99` no está asignada a la interfaz) y lo descarta silenciosamente. Es exactamente lo que queremos, y demuestra que el camino de recepción completo funciona.

### Propiedad: un diagrama

Dado que las reglas de propiedad son tan importantes, resulta útil visualizarlas. El siguiente diagrama resume quién es el propietario del mbuf en cada etapa:

```text
Transmit:
  stack allocates mbuf
  stack calls if_transmit(ifp, m)    <-- ownership handed to driver
  driver inspects, counts, taps, drops or sends
  driver must m_freem(m) exactly once
  return 0 to stack

Receive:
  driver allocates mbuf (MGETHDR/MGET)
  driver fills in data
  driver taps BPF
  driver calls if_input(ifp, m)      <-- ownership handed to stack
  driver MUST NOT touch m again
```

Si mantienes estos dos diagramas en mente, no cometerás errores de propiedad de mbufs en tus propios drivers.

### Seguridad del camino de recepción bajo contención

El camino de recepción de un driver de producción se invoca habitualmente desde un manejador de interrupciones o desde una rutina de finalización de cola de hardware, ejecutándose en una CPU mientras otra puede estar transmitiendo o gestionando ioctls. El patrón que hemos mostrado aquí es seguro porque:

* Mantenemos nuestro mutex alrededor de la comprobación de si estamos en ejecución.
* Liberamos el mutex antes del trabajo costoso de asignación de mbufs y construcción del paquete.
* Liberamos el mutex antes de llamar a `if_input`, que puede a su vez llamar a la pila y adquirir otros locks.
* Reacquirimos nuestro mutex tras retornar `if_input`, para que el framework de callout vea un estado consistente.

Los drivers reales suelen añadir colas de recepción por CPU, procesamiento diferido mediante taskqueues y contadores sin lock. Todo ello es un refinamiento del mismo patrón. Las invariantes fundamentales siguen siendo las mismas: no llamar hacia arriba con el lock del driver adquirido, y no tocar un mbuf después de haberlo entregado a la pila.

### Alternativa: uso de `if_epoch`

FreeBSD 12 introdujo un mecanismo de época de red, `net_epoch`, para acceder a determinadas estructuras de datos sin locks de larga duración. Los drivers modernos suelen entrar en el net_epoch alrededor del código de recepción para que su acceso a la tabla de rutas, las tablas ARP y algunas partes de la lista `ifnet` sea seguro y rápido. Verás `NET_EPOCH_ENTER(et)` y `NET_EPOCH_EXIT(et)` en muchos drivers. Para nuestro sencillo pseudo-driver, entrar en net_epoch añadiría complejidad que no necesitamos. Lo mencionamos aquí para que lo reconozcas cuando leas `if_em.c` o `if_bge.c`, y volveremos a él en capítulos posteriores.

### Caminos de recepción en drivers de NIC reales

Nuestro camino de recepción simulado es artificial, pero la estructura que lo rodea es exactamente la que usan los drivers reales. Las diferencias están en de dónde proviene el mbuf y en quién llama a la rutina de recepción, no en lo que esa rutina hace a continuación. Esta subsección recorre el camino de recepción típico de un driver real para que lo reconozcas la próxima vez que abras un driver Ethernet del árbol.

En una NIC real, los paquetes llegan como escrituras DMA en descriptores de recepción de buffers en anillo. El hardware rellena cada descriptor con un puntero a un mbuf preasignado (proporcionado por el driver durante la inicialización), una longitud y un campo de estado que indica si el descriptor está listo para que el driver lo procese. Cuando un descriptor está listo, el hardware genera una interrupción, establece un bit que el driver detectará mediante polling, o ambas cosas.

La rutina de recepción del driver recorre el anillo comenzando por el último índice procesado. Para cada descriptor listo, lee la longitud y el estado, ajusta el mbuf correspondiente para que tenga los valores correctos de `m_len` y `m_pkthdr.len`, establece `m->m_pkthdr.rcvif = ifp`, hace el tap a BPF, actualiza los contadores y llama a `if_input`. A continuación asigna un mbuf de reemplazo para colocarlo de vuelta en el descriptor, de modo que los paquetes futuros tengan donde aterrizar, y avanza el puntero de cabeza.

Este bucle continúa hasta que el anillo está vacío o el driver ha procesado su cuota de paquetes por invocación. Procesar demasiados paquetes en una sola interrupción priva a otras interrupciones de tiempo de CPU y perjudica la latencia de otros dispositivos; procesar muy pocos desperdicia cambios de contexto. Una cuota de 32 o 64 paquetes es habitual.

Tras el bucle de recepción, el driver actualiza el puntero de cola del hardware para reflejar los descriptores recién repuestos. Si quedan descriptores listos, el driver rearma la interrupción o se programa para ejecutarse de nuevo mediante un taskqueue.

La rutina de finalización de transmisión es la imagen especular: recorre el anillo de transmisión buscando descriptores cuyo estado indica que el hardware ha terminado con ellos, libera los mbufs correspondientes y actualiza el recuento de ranuras de transmisión disponibles que maneja el driver.

Encontrarás todo esto en `/usr/src/sys/dev/e1000/em_txrx.c` y sus equivalentes para otro hardware Ethernet. La maquinaria del buffer en anillo parece intimidante al principio, pero su propósito es siempre el mismo: producir mbufs a partir de DMA del hardware y entregarlos a través de `if_input`. Nuestro pseudo-driver produce mbufs a través de `malloc` y los entrega a través de `if_input`. La entrega es idéntica; solo difiere el origen de los mbufs.

### Procesamiento diferido de recepción con taskqueues

Un refinamiento habitual en drivers de alta tasa es diferir el procesamiento de recepción real fuera del contexto de interrupción y hacia un taskqueue. El manejador de interrupciones hace el mínimo trabajo posible (típicamente reconocer la interrupción al hardware y programar la tarea), y el thread worker del taskqueue realiza el recorrido del anillo y las llamadas a `if_input`.

¿Por qué diferir? Porque `if_input` puede realizar un trabajo considerable dentro de la pila, incluido el procesamiento TCP, el depósito en buffers de socket y operaciones de sleep. Mantener una CPU en un manejador de interrupciones durante tanto tiempo es perjudicial para la latencia de interrupciones de otros dispositivos. Mover el procesamiento de recepción a un taskqueue permite al planificador intercalarlo con otro trabajo.

El subsistema taskqueue de FreeBSD, `/usr/src/sys/kern/subr_taskqueue.c`, proporciona threads worker por CPU que los drivers pueden usar como destino. Un manejador de interrupciones de recepción tiene este aspecto:

```c
static void
my_rx_intr(void *arg)
{
    struct mydrv_softc *sc = arg;

    /* Acknowledge the interrupt. */
    write_register(sc, RX_INT_STATUS, RX_READY);

    /* Defer the actual work. */
    taskqueue_enqueue(sc->rx_tq, &sc->rx_task);
}

static void
my_rx_task(void *arg, int pending __unused)
{
    struct mydrv_softc *sc = arg;

    mydrv_rx_drain(sc);       /* walk the ring and if_input each packet */
}
```

De nuevo, `mynet` es un pseudo-driver y no necesita esta complejidad. Pero conocer el patrón significa que cuando leas `if_em.c` o `if_ixl.c` y veas `taskqueue_enqueue`, sabrás qué se está difiriendo y por qué.

### Comprensión de `net_epoch`

El framework `net_epoch` de FreeBSD es una implementación de reclamación basada en épocas adaptada al subsistema de red. Su propósito es permitir que los lectores de estructuras de datos de red (tablas de rutas, tablas ARP, listas de interfaces, etc.) lean esas estructuras sin adquirir locks, garantizando al mismo tiempo que los escritores no liberen una estructura mientras un lector podría estar accediendo a ella.

La API es sencilla. Un lector entra en la época con `NET_EPOCH_ENTER(et)` y sale con `NET_EPOCH_EXIT(et)`, donde `et` es una variable de seguimiento por llamada. Entre la entrada y la salida, el lector puede desreferenciar de forma segura punteros a las estructuras de datos protegidas. Los escritores que deseen liberar un objeto protegido llaman a `epoch_call` para diferir la liberación hasta que todos los lectores actuales hayan salido.

Para el código de driver, la relevancia es la siguiente: las rutinas de la pila que llamas desde tu camino de recepción, incluidas `ether_input` y sus llamadas posteriores, esperan ser invocadas mientras el llamante está dentro del net_epoch. Algunos drivers por tanto envuelven sus llamadas a `if_input` en `NET_EPOCH_ENTER`/`NET_EPOCH_EXIT`. Otros (y esto incluye a la mayoría de los pseudo-drivers basados en callout) confían en que `if_input` entra en la época al inicio si no está ya dentro de ella.

Para `mynet`, no entramos en la época de forma explícita. `if_input` lo gestiona por nosotros. Si quieres extremar la precaución o estás operando en un contexto donde se sabe que no se ha entrado en la época, puedes envolver tu llamada así:

```c
struct epoch_tracker et;

NET_EPOCH_ENTER(et);
if_input(ifp, m);
NET_EPOCH_EXIT(et);
```

Este es el modismo que verás en los drivers más recientes. Lo hemos omitido en el texto principal del capítulo porque añade ruido sin cambiar el comportamiento de nuestro pseudo-driver. En un driver que pueda llamar a `if_input` desde contextos inusuales (por ejemplo, una workqueue o un tick de temporizador programado en una CPU no de red), querrías envolver de forma explícita.

### Contrapresión en la recepción

Un driver que recibe paquetes más rápido de lo que la pila puede procesarlos acabará desbordando su buffer en anillo. Los drivers reales gestionan esto de una de dos maneras: descartan los paquetes pendientes más antiguos y actualizan `IFCOUNTER_IQDROPS`, o dejan de tomar nuevos descriptores y dejan que el propio hardware descarte.

En los pseudo-drivers software no hay hardware que se quede sin descriptores, pero deberías pensar igualmente en la contrapresión. Si tu camino de recepción simulado genera paquetes más rápido de lo que la pila puede consumirlos, acabarás viendo fallos de asignación de mbufs, o el sistema empezará a encolar paquetes en buffers de socket sin llegar a vaciarlos nunca. La defensa práctica es autolimitarte mediante el intervalo de callout y observar `vmstat -z | grep mbuf` durante las pruebas de larga duración.

Para `mynet`, generamos un ARP sintético por segundo. Eso está varios órdenes de magnitud por debajo de cualquier umbral de contrapresión razonable. Pero si aumentas `sc->rx_interval_hz` a algo agresivo como `hz / 1000` (un paquete por milisegundo), estás pidiendo al kernel que absorba mil ARPs por segundo de un único driver, y verás los costes.

### Errores comunes

Los errores más habituales en el camino de recepción son los siguientes.

**Olvidar la disciplina de `M_PKTHDR`.** Si construyes el mbuf sin `MGETHDR`, no obtienes una cabecera de paquete y la pila generará un assert o se comportará de forma incorrecta. Usa siempre `MGETHDR` (o `m_gethdr`) para el mbuf de cabeza, y `MGET` (o `m_get`) para los siguientes.

**Olvidar establecer `m_len` y `m_pkthdr.len`.** La pila usa `m_pkthdr.len` para decidir el tamaño del paquete, y usa `m_len` para recorrer la cadena. Si estos valores son incorrectos, la decodificación falla silenciosamente.

**Mantener el mutex del driver al llamar a `if_input`.** La pila puede tardar mucho tiempo dentro de `if_input`, y puede intentar adquirir otros locks. Liberar el lock del driver antes de llamar hacia arriba es una disciplina que evita deadlocks.

**Tocar `m` después de `if_input`.** La pila puede haber liberado ya el mbuf o haberlo vuelto a encolar. Trata `if_input` como una puerta de un solo sentido.

**Pasar datos en bruto sin cabecera de capa de enlace.** Como hemos utilizado `ether_ifattach`, `ether_input` espera una trama Ethernet completa. Si le pasas un paquete IPv4 sin cabecera de enlace, rechazará la trama e incrementará `IFCOUNTER_IERRORS`.

### Cerrando la sección 5

Ya tenemos tráfico bidireccional a través de nuestro driver. La transmisión consume mbufs de la pila; la recepción produce mbufs para la pila. Entre medias disponemos de hooks de BPF, actualizaciones de contadores y disciplina de mutex. Lo que aún nos falta es un relato cuidadoso sobre el estado del enlace, los descriptores de medios y los flags de interfaz. Eso es lo que aborda la sección 6.

## Sección 6: Estado de medios, flags y eventos de enlace

Hasta ahora nos hemos centrado en los paquetes. Pero una interfaz de red es algo más que un transmisor de paquetes. Es un participante con estado dentro de la pila de red. Se activa y se desactiva. Tiene un tipo de medio, y ese medio puede cambiar. Su enlace puede aparecer y desaparecer. La pila tiene en cuenta todas esas transiciones, y las herramientas del userland se las presentan al administrador. En esta sección añadimos la capa de gestión de estado a `mynet`.

### Flags de interfaz: `IFF_` e `IFF_DRV_`

Ya conoces `IFF_UP` e `IFF_DRV_RUNNING`. Hay muchos más, y se dividen en dos familias que funcionan de formas distintas.

Los flags `IFF_`, definidos en `/usr/src/sys/net/if.h`, son los flags visibles para el usuario. Son los que `ifconfig` lee y escribe. Entre los más comunes se encuentran:

* `IFF_UP` (`0x1`): la interfaz está activa a nivel administrativo.
* `IFF_BROADCAST` (`0x2`): la interfaz admite broadcast.
* `IFF_POINTOPOINT` (`0x10`): la interfaz es punto a punto.
* `IFF_LOOPBACK` (`0x8`): la interfaz es un loopback.
* `IFF_SIMPLEX` (`0x800`): la interfaz no puede recibir sus propias transmisiones.
* `IFF_MULTICAST` (`0x8000`): la interfaz admite multicast.
* `IFF_PROMISC` (`0x100`): la interfaz está en modo promiscuo.
* `IFF_ALLMULTI` (`0x200`): la interfaz recibe todo el tráfico multicast.
* `IFF_DEBUG` (`0x4`): el usuario ha solicitado trazado de debug.

Estos flags los activa y desactiva principalmente el userland a través de `SIOCSIFFLAGS`. Tu driver debe reaccionar a sus cambios: cuando `IFF_UP` pasa de desactivado a activado, inicializa; cuando pasa de activado a desactivado, detén el tráfico.

Los flags `IFF_DRV_`, también en `if.h`, son privados del driver. Residen en `ifp->if_drv_flags` (no en `if_flags`). El userland no puede verlos ni modificarlos. Los dos más importantes son:

* `IFF_DRV_RUNNING` (`0x40`): el driver ha asignado sus recursos por interfaz y puede mover tráfico. Equivalente al antiguo alias `IFF_RUNNING`.
* `IFF_DRV_OACTIVE` (`0x400`): la cola de salida del driver está llena. La pila no debe volver a llamar a `if_start` ni a `if_transmit` hasta que este flag se desactive.

Piensa en `IFF_UP` como la intención del usuario y en `IFF_DRV_RUNNING` como la disponibilidad del driver. Ambos deben estar activos para que fluya el tráfico.

### El ioctl `SIOCSIFFLAGS`

Cuando el userland ejecuta `ifconfig mynet0 up`, activa `IFF_UP` en el campo de flags de la interfaz y emite `SIOCSIFFLAGS`. La pila despacha este ioctl a través de nuestro callback `if_ioctl`. Nuestra tarea consiste en detectar el cambio de flag y reaccionar.

Este es el patrón canónico para gestionar `SIOCSIFFLAGS` en un driver de red:

```c
case SIOCSIFFLAGS:
    MYNET_LOCK(sc);
    if (ifp->if_flags & IFF_UP) {
        if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
            MYNET_UNLOCK(sc);
            mynet_init(sc);
            MYNET_LOCK(sc);
        }
    } else {
        if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
            MYNET_UNLOCK(sc);
            mynet_stop(sc);
            MYNET_LOCK(sc);
        }
    }
    MYNET_UNLOCK(sc);
    break;
```

Analicemos esto.

Si `IFF_UP` está activado, comprobamos si el driver ya está en marcha. Si no lo está, invocamos `mynet_init` para inicializarlo. Si el driver ya está en marcha, no hacemos nada: que el usuario vuelva a activar el flag no tiene efecto.

Si `IFF_UP` no está activado, comprobamos si estábamos en marcha. Si es así, llamamos a `mynet_stop` para detenerlo. Si no, tampoco hay nada que hacer.

Liberamos el lock antes de llamar a `mynet_init` o `mynet_stop`, porque esas funciones pueden tardar y pueden readquirir el lock internamente. El patrón «liberar, llamar, readquirir» es un idioma estándar en los manejadores de ioctl.

### Implementación de `mynet_stop`

`mynet_init` la escribimos en la sección 4. Su contraparte, `mynet_stop`, es similar pero a la inversa:

```c
static void
mynet_stop(struct mynet_softc *sc)
{
    struct ifnet *ifp = sc->ifp;

    MYNET_LOCK(sc);
    sc->running = false;
    ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
    callout_stop(&sc->rx_callout);
    MYNET_UNLOCK(sc);

    if_link_state_change(ifp, LINK_STATE_DOWN);
}
```

Desactivamos nuestro flag de ejecución, borramos el bit `IFF_DRV_RUNNING` para que la pila sepa que no estamos transportando tráfico, detenemos el callout de recepción y anunciamos a la pila que el enlace ha caído. Esta función es la contraparte simétrica de la de inicialización.

### Estado del enlace: `if_link_state_change`

`if_link_state_change(ifp, state)` es la forma canónica que tiene un driver de notificar las transiciones de enlace. Los valores provienen de `/usr/src/sys/net/if.h`:

* `LINK_STATE_UNKNOWN` (0): el driver desconoce el estado del enlace. Es el valor inicial.
* `LINK_STATE_DOWN` (1): sin portadora, sin ningún extremo del enlace accesible.
* `LINK_STATE_UP` (2): el enlace está activo, el extremo es accesible y hay portadora.

La pila registra el nuevo estado, envía una notificación al socket de enrutamiento, despierta a los procesos que esperaban en el estado de la interfaz y lo comunica al userland a través de la línea `status:` de `ifconfig`. Los drivers reales de NIC llaman a `if_link_state_change` desde el manejador de interrupciones de cambio de estado del enlace, normalmente en respuesta a la finalización o pérdida de la autonegociación del PHY. En los pseudo-drivers, elegimos cuándo llamarla según la lógica propia del driver.

Vale la pena ser deliberado en cuanto a cuándo llamar a esta función. En `mynet_init` la llamamos con `LINK_STATE_UP` después de haber activado `IFF_DRV_RUNNING`. En `mynet_stop` la llamamos con `LINK_STATE_DOWN` después de haber desactivado `IFF_DRV_RUNNING`. Si inviertes el orden, durante un breve instante estarás notificando un enlace activo en una interfaz que no está en marcha, o un enlace caído en una interfaz que todavía dice estar activa. La pila puede manejarlo, pero los síntomas de ese desfase resultan confusos.

### Descriptores de medios

Por encima del estado del enlace está el medio. El medio es la descripción del tipo de conexión en uso: 10BaseT, 100BaseT, 1000BaseT, 10GBaseSR, etcétera. No es lo mismo que el estado del enlace: una conexión puede tener un tipo de medio conocido aunque el enlace esté caído.

El subsistema de medios de FreeBSD reside en `/usr/src/sys/net/if_media.c` y su cabecera `/usr/src/sys/net/if_media.h`. Los drivers lo utilizan a través de una pequeña API:

* `ifmedia_init(ifm, dontcare_mask, change_fn, status_fn)`: inicializa el descriptor.
* `ifmedia_add(ifm, word, data, aux)`: añade una entrada de medio.
* `ifmedia_set(ifm, word)`: selecciona la entrada predeterminada.
* `ifmedia_ioctl(ifp, ifr, ifm, cmd)`: gestiona `SIOCGIFMEDIA` y `SIOCSIFMEDIA`.

El «word» es un campo de bits que combina el subtipo de medio con los flags. Para drivers Ethernet se combina `IFM_ETHER` con un subtipo como `IFM_1000_T` (1000BaseT), `IFM_10G_T` (10GBaseT) o `IFM_AUTO` (autonegociación). El conjunto completo de subtipos está enumerado en `if_media.h`.

Configuramos el descriptor en la sección 3:

```c
ifmedia_init(&sc->media, 0, mynet_media_change, mynet_media_status);
ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_T | IFM_FDX, 0, NULL);
ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);
```

Los callbacks son los que la pila invoca cuando el userland consulta o configura el medio:

```c
static int
mynet_media_change(struct ifnet *ifp __unused)
{
    /* In a real driver, program the PHY here. */
    return (0);
}

static void
mynet_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
    struct mynet_softc *sc = ifp->if_softc;

    imr->ifm_status = IFM_AVALID;
    if (sc->running)
        imr->ifm_status |= IFM_ACTIVE;
    imr->ifm_active = IFM_ETHER | IFM_1000_T | IFM_FDX;
}
```

`mynet_media_change` es el stub: en un pseudo-driver no hay PHY que reprogramar. `mynet_media_status` es lo que `ifconfig` informa a través de `SIOCGIFMEDIA`: `ifm_status` recibe `IFM_AVALID` (los campos de estado son válidos) e `IFM_ACTIVE` (el enlace está activo en este momento) cuando estamos en marcha, e `ifm_active` indica al llamador qué medio estamos usando realmente.

El manejador de ioctl enruta las solicitudes de medio hacia `ifmedia_ioctl`:

```c
case SIOCGIFMEDIA:
case SIOCSIFMEDIA:
    error = ifmedia_ioctl(ifp, ifr, &sc->media, cmd);
    break;
```

Este es exactamente el patrón que emplea el caso `SIOCSIFMEDIA` / `SIOCGIFMEDIA` dentro de `epair_ioctl` en `/usr/src/sys/net/if_epair.c`.

Con esto en su lugar, `ifconfig mynet0` mostrará algo como:

```text
mynet0: flags=8843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST> metric 0 mtu 1500
        ether 02:a3:f1:22:bc:0d
        inet 192.0.2.1 netmask 0xffffff00 broadcast 192.0.2.255
        media: Ethernet autoselect (1000baseT <full-duplex>)
        status: active
```

### Gestión de cambios de MTU

`SIOCSIFMTU` es el ioctl que el usuario emite al ejecutar `ifconfig mynet0 mtu 1400`. Un driver bien construido comprueba que el valor solicitado está dentro de su rango soportado y luego actualiza `if_mtu`. Nuestro código:

```c
case SIOCSIFMTU:
    if (ifr->ifr_mtu < 68 || ifr->ifr_mtu > 9216) {
        error = EINVAL;
        break;
    }
    ifp->if_mtu = ifr->ifr_mtu;
    break;
```

El límite inferior de 68 bytes se corresponde con el payload IPv4 mínimo más las cabeceras. El límite superior de 9216 es un margen generoso para tramas jumbo. Los drivers reales tienen rangos más estrechos que se ajustan a lo que su hardware puede fragmentar. Mantenemos el rango permisivo porque se trata de un pseudo-driver.

### Gestión de cambios en grupos multicast

`SIOCADDMULTI` y `SIOCDELMULTI` indican que el usuario ha añadido o eliminado un grupo multicast en la interfaz. Para una NIC real que implementa filtrado multicast por hardware, el driver reprogramaría el filtro en cada ocasión. Nuestro pseudo-driver no tiene filtro, así que simplemente reconocemos la solicitud:

```c
case SIOCADDMULTI:
case SIOCDELMULTI:
    /* Nothing to program. */
    break;
```

Esto es suficiente para un funcionamiento correcto. La pila entregará el tráfico multicast a la interfaz según su lista de grupos interna, y no necesitamos hacer nada especial.

### Ensamblando el manejador de ioctl

Con todo lo anterior, el `mynet_ioctl` completo tiene este aspecto:

```c
static int
mynet_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
    struct mynet_softc *sc = ifp->if_softc;
    struct ifreq *ifr = (struct ifreq *)data;
    int error = 0;

    switch (cmd) {
    case SIOCSIFFLAGS:
        MYNET_LOCK(sc);
        if (ifp->if_flags & IFF_UP) {
            if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
                MYNET_UNLOCK(sc);
                mynet_init(sc);
                MYNET_LOCK(sc);
            }
        } else {
            if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
                MYNET_UNLOCK(sc);
                mynet_stop(sc);
                MYNET_LOCK(sc);
            }
        }
        MYNET_UNLOCK(sc);
        break;

    case SIOCSIFMTU:
        if (ifr->ifr_mtu < 68 || ifr->ifr_mtu > 9216) {
            error = EINVAL;
            break;
        }
        ifp->if_mtu = ifr->ifr_mtu;
        break;

    case SIOCADDMULTI:
    case SIOCDELMULTI:
        break;

    case SIOCGIFMEDIA:
    case SIOCSIFMEDIA:
        error = ifmedia_ioctl(ifp, ifr, &sc->media, cmd);
        break;

    default:
        /* Let the common ethernet handler process this. */
        error = ether_ioctl(ifp, cmd, data);
        break;
    }

    return (error);
}
```

El caso `default` delega en `ether_ioctl`, que gestiona los ioctls que todo driver Ethernet maneja de la misma forma (por ejemplo, `SIOCSIFADDR` y `SIOCSIFCAP` en los casos comunes). Eso nos ahorra escribir quince líneas de código repetitivo. `/usr/src/sys/net/if_epair.c` hace lo mismo en el brazo `default` del switch de `epair_ioctl`.

### Reglas de coherencia de flags

Hay algunas reglas de coherencia que debes tener presentes al escribir las transiciones de estado del driver:

1. `IFF_DRV_RUNNING` sigue a `IFF_UP`, no al revés. El usuario activa `IFF_UP`, y el driver activa o desactiva `IFF_DRV_RUNNING` en respuesta.
2. Los cambios de estado del enlace deben producirse después de las transiciones de `IFF_DRV_RUNNING`, no antes.
3. Los callouts y taskqueues que se iniciaron al activar `IFF_DRV_RUNNING` deben detenerse o vaciarse cuando lo desactives.
4. Las llamadas a `if_input` solo deben producirse cuando `IFF_DRV_RUNNING` esté activado. De lo contrario, estarías entregando paquetes en una interfaz que la pila aún no ha terminado de iniciar.
5. `if_transmit` puede ser llamada incluso cuando `IFF_UP` está desactivado, debido a una condición de carrera entre el userland y la pila. Tu ruta de transmisión debe comprobar los flags y descartar el paquete de forma controlada si cualquiera de ellos está desactivado.

Estas reglas son implícitas en el código de todo driver bien escrito. Hacerlas explícitas resulta útil cuando se está aprendiendo.

### Capacidades de interfaz en profundidad

Mencionamos las capacidades en la sección 3 cuando establecimos `IFCAP_VLAN_MTU`. Merecen un tratamiento más completo aquí, porque son el mecanismo que usa un driver para indicarle a la pila qué offloads puede realizar, y son cada vez más centrales para que los drivers rápidos sigan siendo rápidos.

El campo `if_capabilities`, definido en `/usr/src/sys/net/if.h`, es una máscara de bits con las capacidades que el hardware puede realizar. El campo `if_capenable` es una máscara de bits con las capacidades actualmente habilitadas. Están separados porque el userland puede activar o desactivar offloads individuales en tiempo de ejecución mediante `ifconfig mynet0 -rxcsum` o `ifconfig mynet0 +tso`, y el driver debe respetar esa decisión.

Las capacidades más comunes son:

* `IFCAP_RXCSUM` e `IFCAP_RXCSUM_IPV6`: el driver verificará los checksums de IPv4 e IPv6 por hardware y marcará los paquetes con checksum correcto con `CSUM_DATA_VALID` en el campo `m_pkthdr.csum_flags` del mbuf.
* `IFCAP_TXCSUM` e `IFCAP_TXCSUM_IPV6`: el driver calculará los checksums de TCP, UDP e IP por hardware para los paquetes salientes cuyo `m_pkthdr.csum_flags` así lo solicite.
* `IFCAP_TSO4` e `IFCAP_TSO6`: el driver acepta segmentos TCP grandes y el hardware los divide en tramas del tamaño de la MTU en el cable. Esto reduce drásticamente la carga de CPU en cargas de trabajo intensivas en TCP.
* `IFCAP_LRO`: el driver agrega varios segmentos TCP recibidos en un único mbuf de gran tamaño antes de entregarlos a la pila. Es la contraparte simétrica de TSO en el lado de recepción.
* `IFCAP_VLAN_HWTAGGING`: el driver añadirá y eliminará las etiquetas VLAN 802.1Q por hardware en lugar de por software. Esto ahorra una copia de mbuf por trama VLAN.
* `IFCAP_VLAN_MTU`: el driver puede transportar tramas con etiqueta VLAN cuya longitud total supera ligeramente la MTU Ethernet estándar debido a los 4 bytes adicionales de la etiqueta.
* `IFCAP_JUMBO_MTU`: el driver admite tramas con un payload superior a 1500 bytes.
* `IFCAP_WOL_MAGIC`: wake-on-LAN mediante el paquete mágico.
* `IFCAP_POLLING`: polling clásico de dispositivo, actualmente en desuso.
* `IFCAP_NETMAP`: el driver admite I/O de paquetes con bypass del kernel mediante `netmap(4)`.
* `IFCAP_TOE`: motor de offload TCP. Poco frecuente, pero existe en algunas NIC de gama alta.

Anunciar una capacidad es hacer una promesa a la pila que debes cumplir. Si declaras `IFCAP_TXCSUM` pero en realidad no calculas el checksum TCP para las tramas salientes, el kernel te entregará encantado paquetes con el checksum sin calcular y esperará que tú termines el trabajo. El receptor recibirá tramas corruptas y las descartará. El síntoma es pérdida silenciosa de datos, que resulta muy dolorosa de depurar.

Para `mynet`, solo anunciamos lo que realmente podemos ofrecer. `IFCAP_VLAN_MTU` es la única capacidad que declaramos, y la respetamos aceptando tramas de hasta `ifp->if_mtu + sizeof(struct ether_vlan_header)` en la ruta de transmisión.

Un driver bien comportado también gestiona `SIOCSIFCAP` en su manejador de ioctl para que el usuario pueda activar o desactivar offloads concretos:

```c
case SIOCSIFCAP:
    mask = ifr->ifr_reqcap ^ ifp->if_capenable;
    if (mask & IFCAP_VLAN_MTU)
        ifp->if_capenable ^= IFCAP_VLAN_MTU;
    /* Reprogram hardware if needed. */
    break;
```

Para un pseudo-driver no hay hardware que reprogramar, pero el interruptor visible para el usuario sigue funcionando porque el ioctl actualiza `if_capenable` y cualquier decisión de transmisión posterior lee ese campo.

### El manejador común `ether_ioctl`

Vimos antes que `mynet_ioctl` delega los ioctls desconocidos a `ether_ioctl`. Merece la pena echar un vistazo a lo que hace esa función, porque explica por qué la mayoría de los drivers pueden arreglárselas manejando solo un puñado de ioctls de forma explícita.

`ether_ioctl`, definida en `/usr/src/sys/net/if_ethersubr.c`, es un manejador genérico para los ioctls que toda interfaz Ethernet trata del mismo modo. Sus responsabilidades incluyen:

* `SIOCSIFADDR`: el usuario está asignando una dirección IP a la interfaz. `ether_ioctl` gestiona la sonda ARP y el registro de la dirección. Invoca el callback `if_init` del driver si la interfaz está caída y debe levantarse.
* `SIOCGIFADDR`: devuelve la dirección de la capa de enlace de la interfaz.
* `SIOCSIFMTU`: si el driver no proporciona su propio manejador, `ether_ioctl` realiza el cambio genérico de MTU actualizando `if_mtu`.
* `SIOCADDMULTI` y `SIOCDELMULTI`: actualizan el filtro multicast del driver, si existe alguno.
* Varios ioctls relacionados con capacidades.

Dado que el manejador por defecto se encarga de tanto, los drivers normalmente solo necesitan gestionar los ioctls que requieren lógica específica del driver: `SIOCSIFFLAGS` para la transición up/down, `SIOCSIFMEDIA` para reprogramar el medio y `SIOCSIFCAP` para activar o desactivar capacidades. Todo lo demás cae hasta `ether_ioctl`.

Este modelo de delegación es una de las cosas que hace agradable escribir un driver Ethernet sencillo: tú escribes el código específico de tu driver y el código común se encarga del resto.

### Filtrado multicast por hardware

En una NIC real, el filtrado multicast se realiza a menudo en hardware. El driver programa un conjunto de direcciones MAC en una tabla de filtros hardware y la NIC solo entrega las tramas cuyo destino coincide con una dirección de la tabla. Cuando el usuario ejecuta `ifconfig mynet0 addm 01:00:5e:00:00:01` para unirse a un grupo multicast, la pila emite `SIOCADDMULTI` y el driver debe actualizar la tabla de filtros.

El patrón típico en un driver real es:

```c
case SIOCADDMULTI:
case SIOCDELMULTI:
    if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
        MYDRV_LOCK(sc);
        mydrv_setup_multicast(sc);
        MYDRV_UNLOCK(sc);
    }
    break;
```

`mydrv_setup_multicast` recorre la lista multicast de la interfaz (accesible a través de `if_maddr_rlock` y funciones relacionadas) y programa cada dirección en el filtro hardware. El código es tedioso pero importante; si se hace mal, las aplicaciones multicast como mDNS (Bonjour, Avahi), el enrutamiento basado en IGMP y el descubrimiento de vecinos de IPv6 dejan de funcionar de forma silenciosa.

En `mynet` no tenemos filtro hardware, así que simplemente aceptamos `SIOCADDMULTI` y `SIOCDELMULTI` sin hacer nada. La pila sigue gestionando por nosotros la lista de grupos multicast y nuestro camino de recepción no filtra nada, por lo que todo funciona.

Si alguna vez escribes un driver con filtrado multicast por hardware, lee la función `em_multi_set` de `/usr/src/sys/dev/e1000/if_em.c` para ver un ejemplo claro del patrón.

### Cerrando la sección 6

Hemos cubierto la mitad de estado de un driver de red. Flags, estado del enlace, descriptores de medio y los ioctls que los unen a todos. Combinado con los caminos de transmisión y recepción de las secciones 4 y 5, tenemos ahora un driver indistinguible de un driver Ethernet real sencillo en el límite de `ifnet`.

Antes de poder dar el driver por terminado, necesitamos asegurarnos de que podemos probarlo a fondo con las herramientas que ofrece el ecosistema de FreeBSD. Eso es la sección 7.

## Sección 7: Probar el driver con las herramientas de red estándar

Un driver solo vale lo que la confianza que tienes en que funciona. La confianza no surge de mirar el código. Surge de ejecutar el driver, interactuar con él desde el exterior y observar los resultados. Esta sección recorre las herramientas de red estándar de FreeBSD y muestra cómo usar cada una de ellas para ejercitar un aspecto concreto de `mynet`.

### Cargar, crear, configurar

Empieza desde cero. Si el módulo está cargado, descárgalo, luego carga la compilación reciente y crea la primera interfaz:

```console
# kldstat | grep mynet
# kldload ./mynet.ko
# ifconfig mynet create
mynet0
```

`ifconfig mynet0` debería mostrar la interfaz con una dirección MAC, sin IP, sin flags más allá del conjunto por defecto y un descriptor de medio que dice "autoselect". Asigna una dirección y levanta la interfaz:

```console
# ifconfig mynet0 inet 192.0.2.1/24 up
# ifconfig mynet0
mynet0: flags=8843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST> metric 0 mtu 1500
        ether 02:a3:f1:22:bc:0d
        inet 192.0.2.1 netmask 0xffffff00 broadcast 192.0.2.255
        media: Ethernet autoselect (1000baseT <full-duplex>)
        status: active
        groups: mynet
```

Los flags `UP` y `RUNNING` confirman que la intención del usuario y la disponibilidad del driver están presentes. La línea `status: active` proviene de nuestro callback de medio. La descripción del medio incluye `1000baseT` porque eso es lo que devolvió `mynet_media_status`.

### Inspección con `netstat`

`netstat -in -I mynet0` muestra los contadores por interfaz. Al principio todo es cero; espera unos segundos a que arranque la simulación de recepción y el contador debería ir subiendo:

```console
# netstat -in -I mynet0
Name    Mtu Network      Address                  Ipkts Ierrs ...  Opkts Oerrs
mynet0 1500 <Link#12>   02:a3:f1:22:bc:0d           3     0        0     0
mynet0    - 192.0.2.0/24 192.0.2.1                   0     -        0     -
```

El campo `Ipkts` de la primera línea cuenta las peticiones ARP sintéticas que genera nuestro temporizador de recepción. Debería aumentar aproximadamente una vez por segundo. Si no lo hace, la configuración de `rx_interval_hz` es incorrecta, o el callout no se está iniciando en `mynet_init`, o `running` es falso.

### Captura con `tcpdump`

`tcpdump -i mynet0 -n` captura todo el tráfico en nuestra interfaz. Deberías ver las peticiones ARP sintéticas generadas cada segundo, junto con cualquier tráfico causado por tus propios intentos de `ping`:

```console
# tcpdump -i mynet0 -n
tcpdump: verbose output suppressed, use -v or -vv for full protocol decode
listening on mynet0, link-type EN10MB (Ethernet), capture size 262144 bytes
14:30:12.000 02:a3:f1:22:bc:0d > ff:ff:ff:ff:ff:ff, ethertype ARP, Request who-has 192.0.2.99 tell 0.0.0.0, length 28
14:30:13.000 02:a3:f1:22:bc:0d > ff:ff:ff:ff:ff:ff, ethertype ARP, Request who-has 192.0.2.99 tell 0.0.0.0, length 28
...
```

El mensaje "link-type EN10MB (Ethernet)" confirma que BPF nos vio como una interfaz Ethernet, consecuencia de que `ether_ifattach` llamó a `bpfattach(ifp, DLT_EN10MB, ETHER_HDR_LEN)` por nosotros. Cambia a `-v` o `-vv` para ver una decodificación de protocolo más completa.

### Generación de tráfico con `ping`

Provoca tráfico saliente haciendo ping a una IP en la subred que asignamos:

```console
# ping -c 3 192.0.2.99
PING 192.0.2.99 (192.0.2.99): 56 data bytes
--- 192.0.2.99 ping statistics ---
3 packets transmitted, 0 packets received, 100.0% packet loss
```

Los tres pings se pierden, porque nuestro pseudo-driver simula un cable sin nada en el otro extremo. Pero el contador de transmisión se mueve:

```console
# netstat -in -I mynet0
Name    Mtu Network     Address                Ipkts Ierrs ... Opkts Oerrs
mynet0 1500 <Link#12>   02:a3:f1:22:bc:0d         30     0       6     0
```

Los 6 paquetes transmitidos son tres pings más tres peticiones ARP broadcast que la pila emitió intentando resolver `192.0.2.99`. Puedes verificarlo con `tcpdump`.

### `arp -an`

`arp -an` muestra la caché ARP del sistema. Las entradas para `192.0.2.99` aparecerán como incompletas mientras la pila espera una respuesta ARP que nunca llegará. Después de un minuto aproximadamente expiran.

### `sysctl net.link` y `sysctl net.inet`

Los subsistemas de red exponen una gran cantidad de sysctls por interfaz y por protocolo. `sysctl net.link.ether` controla el comportamiento de la capa Ethernet. `sysctl net.inet.ip` controla el comportamiento de la capa IP. Aunque ninguno de estos es específico de `mynet`, conviene conocerlos. Uno habitual cuando se diagnostica el comportamiento de pseudo-drivers es `sysctl net.link.ether.inet.log_arp_wrong_iface=0`, que silencia los mensajes de log sobre tráfico ARP que aparece en interfaces inesperadas.

### Monitorización de eventos de enlace con `ifstated` o `devd`

FreeBSD propaga los cambios de estado del enlace a través del socket de enrutamiento. Puedes observarlo en tiempo real con `route monitor`:

```console
# route monitor
```

Cuando ejecutas `ifconfig mynet0 down` seguido de `ifconfig mynet0 up`, `route monitor` imprime mensajes `RTM_IFINFO` correspondientes a los cambios de estado del enlace que anunciamos a través de `if_link_state_change`. Ese es el mismo mecanismo que usa `devd` para sus eventos `notify`, y es como los scripts pueden reaccionar a los cambios de enlace.

### Prueba de cambios de MTU

```console
# ifconfig mynet0 mtu 9000
# ifconfig mynet0
mynet0: ... mtu 9000
```

Cambia el MTU a un valor razonable y observa cómo `ifconfig` refleja el cambio. Prueba un valor fuera de rango y verifica que el kernel lo rechaza:

```console
# ifconfig mynet0 mtu 10
ifconfig: ioctl SIOCSIFMTU (set mtu): Invalid argument
```

Ese error proviene de nuestro manejador de `SIOCSIFMTU` que devuelve `EINVAL`.

### Prueba de comandos de medio

```console
# ifconfig mynet0 media 10baseT/UTP
ifconfig: requested media type not found
```

Esto falla porque no registramos `IFM_ETHER | IFM_10_T` como tipo de medio aceptable. Regístralo en `mynet_create_unit` y vuelve a compilar para ver que el comando tiene éxito.

```console
# ifconfig mynet0 media 1000baseT
# ifconfig mynet0 | grep media
        media: Ethernet 1000baseT <full-duplex>
```

### Comparación con `if_disc`

Carga `if_disc` junto al nuestro y compara:

```console
# kldload if_disc
# ifconfig disc create
disc0
# ifconfig disc0 inet 192.0.2.50/24 up
```

`disc0` es un pseudo-driver más sencillo. Ignora todos los paquetes salientes descartándolos en su función `discoutput` (no la que nosotros escribimos, sino la que está en `if_disc.c`). No tiene camino de recepción. Ejecutar `tcpdump -i disc0` mientras se hace ping a `192.0.2.50` muestra tramas ICMP salientes pero ninguna actividad ARP entrante. Compáralo con nuestro `mynet0`, que sigue mostrando sus tramas ARP sintéticas llegando una vez por segundo.

El contraste es útil porque muestra lo pequeño que es el paso de "descarta todo" a "simula una interfaz Ethernet completa". Añadimos una dirección MAC, un descriptor de medio, un callout y un constructor de paquetes. Todo lo demás, incluyendo el registro de la interfaz, el hook de BPF y los flags, ya estaba en el patrón.

### Prueba de estrés con `iperf3`

`iperf3` puede saturar un enlace Ethernet real. En nuestro pseudo-driver no producirá cifras de throughput significativas (los paquetes no van a ningún lado), pero ejercita `if_transmit` con mucha intensidad:

```console
# iperf3 -c 192.0.2.99 -t 10
Connecting to host 192.0.2.99, port 5201
iperf3: error - unable to connect to server: Connection refused
```

La conexión falla porque no hay servidor, pero `netstat -in -I mynet0` mostrará `Opkts` subiendo rápidamente con las retransmisiones TCP y las peticiones ARP que causó `iperf3`. Observa `vmstat 1` en otro terminal y asegúrate de que la carga del sistema se mantiene razonable. Si ves mucho tiempo invertido en el driver, es posible que haya un punto caliente de locking que vale la pena investigar.

### Ejecuciones de prueba con scripts

Puedes envolver los comandos anteriores en un pequeño script de shell que ejercite el driver en una secuencia conocida. He aquí un ejemplo mínimo:

```sh
#!/bin/sh

set -e

echo "== load =="
kldload ./mynet.ko

echo "== create =="
ifconfig mynet create

echo "== configure =="
ifconfig mynet0 inet 192.0.2.1/24 up

echo "== traffic =="
(tcpdump -i mynet0 -nn -c 5 > /tmp/mynet-tcpdump.txt 2>&1) &
sleep 3
ping -c 2 192.0.2.99 || true
wait
cat /tmp/mynet-tcpdump.txt

echo "== counters =="
netstat -in -I mynet0

echo "== teardown =="
ifconfig mynet0 destroy
kldunload mynet
```

Guárdalo en `examples/part-06/ch28-network-driver/lab05-bpf/run.sh`, márcalo como ejecutable y ejecútalo como root. Lleva al driver a través de todo su ciclo de vida en menos de diez segundos. Cuando algo falle más adelante, una línea base como esta en forma de script es inestimable para detectar regresiones.

### Qué vigilar

Durante las pruebas, presta atención a:

* La salida de `dmesg` durante la carga y la descarga, por si hay advertencias inesperadas.
* `netstat -in -I mynet0` antes y después de las operaciones, para confirmar que los contadores se mueven en la dirección esperada.
* `kldstat` tras la descarga, para confirmar que el módulo ha desaparecido.
* `ifconfig -a` tras `destroy`, para confirmar que no queda ninguna interfaz huérfana.
* `vmstat -m | grep mynet` para confirmar que la memoria se devuelve al descargar.
* `vmstat -z | grep mbuf` a lo largo de las ejecuciones de prueba de carga, para confirmar que los contadores de mbuf se estabilizan.

Un driver correcto en una carga en frío puede seguir teniendo fugas al descargar, o bajo carga, o puede provocar un pánico del kernel en una condición de carrera poco frecuente. Las herramientas listadas anteriormente son la primera línea de defensa contra todas esas categorías de bugs.

### Mayor observabilidad con DTrace

La implementación de DTrace de FreeBSD es una herramienta formidable para la observabilidad de drivers y, una vez que conoces unos pocos patrones, la usarás con frecuencia. La idea básica es que cada entrada y salida de función en el kernel es un punto de sonda, y cada punto de sonda puede instrumentarse desde el espacio de usuario sin modificar el código.

Para contar cuántas veces se llama a nuestra función de transmisión:

```console
# dtrace -n 'fbt::mynet_transmit:entry { @c = count(); }'
```

Ejecuta eso en un terminal, genera tráfico en otro y verás cómo sube la cuenta. Para observar cada llamada con la longitud del paquete:

```console
# dtrace -n 'fbt::mynet_transmit:entry { printf("len=%d", args[1]->m_pkthdr.len); }'
```

Los scripts de DTrace pueden ser mucho más elaborados. He aquí uno que cuenta los paquetes transmitidos agrupados por IP de origen, si la interfaz lleva tráfico IPv4:

```console
# dtrace -n 'fbt::mynet_transmit:entry /args[1]->m_pkthdr.len > 34/ {
    this->ip = (struct ip *)(mtod(args[1], struct ether_header *) + 1);
    @src[this->ip->ip_src.s_addr] = count();
}'
```

Este tipo de observabilidad es difícil de añadir a un driver a mano, pero DTrace te la proporciona gratis. Úsalo. Cuando no puedas averiguar por qué un paquete fluyó o no fluyó, las sondas de DTrace sobre tus propias funciones revelarán casi siempre la respuesta.

Algunos one-liners adicionales útiles para el trabajo con drivers de red:

```console
# dtrace -n 'fbt::if_input:entry { @ifs[stringof(args[0]->if_xname)] = count(); }'
```

Este cuenta las llamadas a `if_input` en todo el sistema, agrupadas por nombre de interfaz. Es una forma rápida de verificar que tu camino de recepción está llegando a la pila.

```console
# dtrace -n 'fbt::if_inc_counter:entry /args[1] == 1/ {
    @[stringof(args[0]->if_xname)] = count();
}'
```

Este cuenta las llamadas a `if_inc_counter` para `IFCOUNTER_IPACKETS` (que es el valor 1 en el enum) agrupadas por nombre de interfaz. Comparado con `netstat -in`, te permite ver los incrementos en tiempo real.

No le tengas miedo a DTrace. Al principio parece intimidante por su sintaxis similar a la de un script, pero una sesión de depuración de un driver con DTrace suele llevarse en minutos donde la depuración equivalente con printf lleva horas. Cada minuto que inviertes en aprender los modismos de DTrace se amortiza muchas veces.

### Consejos del depurador del kernel para drivers

Cuando un driver de red provoca un pánico o se bloquea, el depurador del kernel (`ddb` o `kgdb`) es el último recurso. Algunos consejos específicos para el trabajo con drivers:

* Tras un panic, `show mbuf` (o `show pcpu`, `show alltrace`, `show lockchain`, según lo que estés investigando) recorre las asignaciones de mbuf, los datos por CPU o las cadenas de threads bloqueados. Saber cuál invocar en cada momento es cuestión de práctica.
* `show ifnet <pointer>` imprime el contenido de una estructura `ifnet` dada su dirección. Resulta útil cuando un mensaje de panic dice «ifp = 0xffff...». El equivalente para un softc depende del driver.
* `bt` imprime un stack trace. En la mayoría de los casos querrás usar `bt <tid>`, donde `<tid>` es el identificador del thread que te interesa.
* `continue` reanuda la ejecución, pero tras un panic real por lo general no es seguro hacerlo. Recoge la información necesaria y luego ejecuta `reboot`.

Para la depuración fuera de un panic, `kgdb /boot/kernel/kernel /var/crash/vmcore.0` te permite analizar un crash dump a posteriori. Desarrollar drivers en una VM de laboratorio con una partición de crash dump es un flujo de trabajo cómodo: ocurre el panic, reinicias y examinas el dump con calma.

### `systat -if` para visualizar contadores en tiempo real

`systat -if 1` abre una vista ncurses que se actualiza cada segundo y muestra las tasas de contadores por interfaz. Es un complemento útil de `netstat -in` porque puedes observar cómo sube y baja el tráfico en tiempo real sin necesidad de leer el registro de la terminal.

```text
                    /0   /1   /2   /3   /4   /5   /6   /7   /8   /9   /10
     Load Average   ||
          Interface          Traffic               Peak                Total
             mynet0     in      0.000 KB/s      0.041 KB/s         0.123 KB
                       out      0.000 KB/s      0.047 KB/s         0.167 KB
```

Las tasas que aparecen en esta vista las calcula `systat` a partir de los contadores que incrementamos en `if_transmit` y en nuestra ruta de recepción. Si las tasas no coinciden con lo esperado, la primera sospecha debería ser que un contador se está actualizando dos veces, o que se actualiza después de `m_freem`, o que usa `IFCOUNTER_OPACKETS` donde debería usar `IFCOUNTER_IPACKETS`. `systat -if` hace que esos errores sean muy evidentes.

### Cerrando la sección 7

Ahora tienes un driver probado. Se carga, se configura, transporta tráfico en ambas direcciones, informa de su estado al espacio de usuario, coopera con BPF y reacciona a eventos de enlace. Lo que queda es la fase final del ciclo de vida: la desconexión limpia, la descarga del módulo y algunos consejos de refactorización. Eso es la sección 8.

## Sección 8: limpieza, desconexión y refactorización del driver de red

Todo driver tiene un principio y un fin. El principio es el patrón que hemos ido construyendo a lo largo del capítulo: asignar, configurar, registrar, ejecutar. El fin es el desmontaje simétrico: silenciar, desregistrar, liberar. Un driver que pierde un solo byte al descargarse no es un driver correcto, por más bueno que sea durante su vida activa. En esta sección finalizamos la ruta de limpieza, repasamos la disciplina de descarga y ofrecemos consejos de refactorización para que el código se mantenga manejable a medida que crece.

### La secuencia de desmontaje completa

Reuniendo todo lo que hemos explicado, el desmontaje completo de una interfaz `mynet` tiene este aspecto:

```c
static void
mynet_destroy(struct mynet_softc *sc)
{
    struct ifnet *ifp = sc->ifp;

    MYNET_LOCK(sc);
    sc->running = false;
    ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
    MYNET_UNLOCK(sc);

    callout_drain(&sc->rx_callout);

    ether_ifdetach(ifp);
    if_free(ifp);

    ifmedia_removeall(&sc->media);
    mtx_destroy(&sc->mtx);
    free(sc, M_MYNET);
}
```

El orden importa. Veámoslo paso a paso.

**Paso 1: marcar como no activo.** Establecer `sc->running = false` y limpiar `IFF_DRV_RUNNING` bajo el mutex hace que cualquier invocación concurrente del callout vea la actualización y termine limpiamente. Esto por sí solo no basta para detener los callouts en ejecución, pero sí impide que se programe nuevo trabajo.

**Paso 2: drenar el callout.** `callout_drain(&sc->rx_callout)` bloquea el thread que realiza la llamada hasta que cualquier invocación del callout en curso haya terminado y no se vaya a producir ninguna más. Una vez que `callout_drain` retorna, es seguro acceder al softc sin preocuparse de que el callout vuelva a dispararse. Esta es la forma más limpia de sincronizarse con un callout y es el patrón que recomendamos en todo driver que los utilice.

**Paso 3: desconectar la interfaz.** `ether_ifdetach(ifp)` deshace lo que hizo `ether_ifattach`. Llama a `if_detach`, que elimina la interfaz de la lista global, revoca sus direcciones e invalida cualquier puntero en caché. También llama a `bpfdetach` para que BPF libere su handle. Tras esta llamada, la interfaz deja de ser visible para el espacio de usuario o para la pila.

**Paso 4: liberar el ifnet.** `if_free(ifp)` libera la memoria. Tras esta llamada, el puntero `ifp` es inválido y no debe usarse.

**Paso 5: limpiar el estado privado del driver.** `ifmedia_removeall` libera las entradas de medio que añadimos. `mtx_destroy` destruye el mutex. `free` libera el softc.

Ejecutar esta secuencia de forma incorrecta en cualquier punto genera bugs sutiles. Liberar el softc antes de drenar el callout provoca un use-after-free cuando el callout se dispara. Liberar el ifnet antes de desconectarlo provoca fallos en cascada por toda la pila. Destruir el mutex antes de drenar el callout (que vuelve a adquirir el mutex al entrar) provoca el clásico panic de "destroying locked mutex". La disciplina de "silenciar, desconectar, liberar" es lo que mantiene el desmontaje limpio.

### La ruta de destrucción del cloner

Recuerda que registramos nuestro cloner con `if_clone_simple`, pasando `mynet_clone_create` y `mynet_clone_destroy`. La función de destrucción la llama el framework del cloner cuando el espacio de usuario ejecuta `ifconfig mynet0 destroy` o cuando el módulo se descarga y el cloner se desconecta. Nuestra implementación es un simple wrapper:

```c
static void
mynet_clone_destroy(struct ifnet *ifp)
{
    mynet_destroy((struct mynet_softc *)ifp->if_softc);
}
```

El framework del cloner recorre la lista de interfaces que ha creado y llama a la función de destrucción para cada una. No realiza el drenado ni el desbloqueo por sí mismo. Esa es la responsabilidad del driver, y `mynet_destroy` lo hace correctamente.

### Descarga del módulo

Cuando se invoca `kldunload mynet`, el kernel llama al manejador de eventos del módulo con `MOD_UNLOAD`. Nuestro manejador de módulo no hace nada interesante; el trabajo pesado lo realiza el VNET sysuninit que registramos:

```c
static void
vnet_mynet_uninit(const void *unused __unused)
{
    if_clone_detach(V_mynet_cloner);
}
```

`if_clone_detach` hace dos cosas. Primero, destruye todas las interfaces creadas a través del cloner llamando a nuestro `mynet_clone_destroy` para cada una. Segundo, desregistra el propio cloner para que no se puedan crear nuevas interfaces. Tras esta llamada, no queda ningún rastro de nuestro driver en el estado del kernel.

Pruébalo:

```console
# ifconfig mynet create
mynet0
# ifconfig mynet create
mynet1
# kldunload mynet
# ifconfig -a
```

`mynet0` y `mynet1` deberían haber desaparecido. Sin mensajes en la consola, sin contadores perdidos, sin cloners sobrantes. Eso es una descarga exitosa.

### Contabilidad de memoria

`vmstat -m | grep mynet` muestra la asignación actual de nuestra etiqueta `M_MYNET`:

```console
# vmstat -m | grep mynet
         Type InUse MemUse Requests  Size(s)
        mynet     0     0K        7  2048
```

`InUse 0` y `MemUse 0K` tras la descarga confirman que no hay fugas de memoria. `Requests` cuenta las asignaciones a lo largo de toda la vida útil del módulo. Si descargas y recargas varias veces, `Requests` aumenta pero `InUse` vuelve a cero cada vez. Si `InUse` alguna vez se mantiene por encima de cero tras la descarga, tienes una fuga.

### Cómo tratar los callouts bloqueados

En ocasiones, durante el desarrollo, modificarás el driver y terminarás con un callout que no se drena limpiamente. El síntoma es que `kldunload` se queda colgado, o el sistema entra en pánico con un mensaje sobre un mutex bloqueado. La causa raíz es casi siempre una de las siguientes:

* El manejador del callout vuelve a adquirir el mutex pero no se reprograma a sí mismo, y `callout_drain` se llama antes de que termine el último disparo programado.
* El manejador del callout está bloqueado esperando un lock que otro thread mantiene.
* El propio callout no se detuvo correctamente antes del drenado.

La primera línea de defensa es `callout_init_mtx` con el mutex del softc: esto establece un patrón de adquisición automática que hace que el drenado sea correcto por construcción. La segunda es usar `callout_stop` o `callout_drain` de forma consistente y evitar mezclar ambos en el mismo callout.

Si la descarga se queda colgada, usa `ps -auxw` para encontrar el thread problemático, y `kgdb` sobre un kernel en ejecución (a través de `/dev/mem` y `bin/kgdb /boot/kernel/kernel`) para ver en qué está atascado. El frame bloqueado casi siempre está en el código del callout, y la solución casi siempre es drenar antes de destruir el mutex.

### Consideraciones sobre VNET

La pila de red de FreeBSD admite VNETs, pilas de red virtuales asociadas a una jail o a una instancia VNET. Un driver puede ser compatible con VNET si quiere permitir la creación de interfaces por VNET, o puede no serlo si con un conjunto de interfaces por sistema es suficiente.

Utilizamos `VNET_DEFINE_STATIC` y `VNET_SYSINIT`/`VNET_SYSUNINIT` en el registro de nuestro cloner. Esa elección hace que nuestro driver sea implícitamente compatible con VNET: cada VNET obtiene su propio cloner, y las interfaces `mynet` pueden crearse en cualquier VNET. Para un pseudo-driver pequeño, esto no nos cuesta nada y nos aporta flexibilidad.

Los aspectos más profundos de VNET, como mover una interfaz entre VNETs con `if_vmove` o gestionar el desmontaje de VNET, están fuera del alcance de este capítulo y se tratarán más adelante en el libro, en el capítulo 30. Por ahora es suficiente saber que nuestro driver sigue las convenciones que lo hacen compatible con VNET.

### Consejos de refactorización

El driver que hemos construido es un único archivo C con unas 500 líneas de código. Eso es cómodo para un ejemplo didáctico. En un driver de producción con más funcionalidades, el archivo crecería y querrías dividirlo. Estas son las divisiones que casi todo driver termina haciendo.

**Separa el código de enlace del ifnet de la ruta de datos.** El registro del ifnet, la lógica del cloner y el manejo de ioctl tienden a ser estables a lo largo del tiempo. La ruta de datos, transmisión y recepción, evoluciona a medida que cambian las características del hardware. Dividirlos en `mynet_if.c` y `mynet_data.c` mantiene la mayoría de los archivos pequeños y centrados en una sola responsabilidad.

**Aísla el backend.** En un driver de NIC real, el backend es código específico del hardware: acceso a registros, DMA, MSI-X, ring buffers. En un pseudo-driver, el backend es la simulación. En cualquier caso, poner el backend en `mynet_backend.c` con una interfaz limpia hace posible reemplazarlo sin tocar el código del ifnet.

**Separa sysctl y la depuración.** A medida que tu driver crezca, añadirás sysctls para controles de diagnóstico, contadores para depuración y quizás sondas SDT de DTrace. Estos tienden a acumularse de forma desordenada. Mantenerlos en `mynet_sysctl.c` hace que los archivos principales sigan siendo legibles.

**Mantén el header público.** Un header `mynet_var.h` o `mynet.h` que declare el softc y los prototipos entre archivos es el pegamento que mantiene compilando la división. Trata ese header como una pequeña API pública.

**Versiona el driver.** `MODULE_VERSION(mynet, 1)` es el mínimo imprescindible. Cuando añadas una funcionalidad significativa, incrementa la versión. Los consumidores aguas abajo que dependen de tu módulo pueden entonces requerir una versión mínima, y los usuarios del kernel pueden saber qué versión del driver tienen cargada mediante `kldstat -v`.

### Flags de características y capacidades

Los drivers de Ethernet anuncian sus capacidades a través de `if_capabilities` e `if_capenable`. Nosotros establecemos `IFCAP_VLAN_MTU`. Otras capacidades que un driver real podría anunciar incluyen:

* `IFCAP_HWCSUM`: descarga de checksum por hardware.
* `IFCAP_TSO4`, `IFCAP_TSO6`: descarga de segmentación TCP para IPv4 e IPv6.
* `IFCAP_LRO`: descarga de recepción de gran tamaño.
* `IFCAP_VLAN_HWTAGGING`: etiquetado VLAN por hardware.
* `IFCAP_RXCSUM`, `IFCAP_TXCSUM`: descarga de checksum de recepción y transmisión.
* `IFCAP_JUMBO_MTU`: soporte de tramas jumbo.
* `IFCAP_LINKSTATE`: eventos de estado de enlace por hardware.
* `IFCAP_NETMAP`: soporte de netmap(4) para I/O de paquetes de alta velocidad.

Para un pseudo-driver, la mayoría de estas no son relevantes. Anunciarlas falsamente causa problemas porque la pila intentará usarlas y esperará que funcionen. Mantén el conjunto de capacidades honesto: anuncia solo lo que tu driver realmente admite.

### Cómo escribir un script de ejecución

Uno de los artefactos más útiles que se pueden producir junto a un driver es un pequeño script de shell que ejercite todo su ciclo de vida. El esqueleto que mostramos en la sección 7 ya es el 80% de ese script. Amplíalo con:

* Comprobaciones de consistencia tras cada operación (`ifconfig -a | grep mynet0` o `netstat -in -I mynet0 | ...`).
* Registro opcional de cada paso en un archivo para inspección posterior.
* Un bloque de limpieza al final que garantice que el sistema quede en un estado conocido aunque un paso anterior falle.

Un buen script de ejecución es la herramienta más valiosa de todas para un desarrollo sin regresiones. Te animamos a que mantengas uno a medida que extiendas el driver en los ejercicios.

### Cómo mantener el archivo en orden

Por último, unas palabras sobre el estilo del código. Los drivers reales de FreeBSD siguen KNF (Kernel Normal Form), el estilo de código documentado en `style(9)`. En resumen: tabuladores para la sangría, llaves en la misma línea que las definiciones de función pero en la línea siguiente para estructuras y enumeraciones, líneas de 80 columnas siempre que sea posible, sin espacios antes del paréntesis de apertura de una llamada a función, y así sucesivamente. Tu driver será más fácil de integrar aguas arriba (y más fácil de leer dentro de un año) si sigues KNF de forma consistente.

### Cómo gestionar fallos de inicialización parciales

Nos hemos centrado en el camino ideal. ¿Qué ocurre si `mynet_create_unit` falla a mitad del proceso? Imagina que `if_alloc` tiene éxito, `mtx_init` se ejecuta, `ifmedia_init` configura el medio y luego el `malloc` de algún buffer auxiliar devuelve NULL. Necesitamos revertir limpiamente, porque el usuario acaba de ver que `ifconfig mynet create` ha fallado y no debemos dejar ningún rastro.

El patrón habitual para la reversión es un bloque de etiquetas al final de la función, cada una deshaciendo un paso de la inicialización:

```c
static int
mynet_create_unit(int unit)
{
    struct mynet_softc *sc;
    struct ifnet *ifp;
    int error = 0;

    sc = malloc(sizeof(*sc), M_MYNET, M_WAITOK | M_ZERO);
    ifp = if_alloc(IFT_ETHER);
    if (ifp == NULL) {
        error = ENOSPC;
        goto fail_alloc;
    }

    sc->ifp = ifp;
    mtx_init(&sc->mtx, "mynet", NULL, MTX_DEF);
    /* ... other setup ... */

    ether_ifattach(ifp, sc->hwaddr);
    return (0);

fail_alloc:
    free(sc, M_MYNET);
    return (error);
}
```

Este patrón, común en el código del kernel, convierte la reversión en algo rutinario. Cada etiqueta se responsabiliza del paso inmediatamente anterior. La forma general es: si algo en el paso N falla, salta a la etiqueta N-1 y deshaz desde allí.

En nuestro driver, el único punto de fallo realista al comienzo de create es `if_alloc`. Si esa llamada tiene éxito, el resto de la inicialización (mutex init, media init, ether_ifattach) es infalible o suficientemente idempotente como para que no sea necesario ningún rollback. Pero la forma del rollback importa, porque un driver más complejo tendrá más puntos de fallo, y el mismo patrón escala de manera limpia.

### Sincronización con callbacks en vuelo

Además de los callouts, puede haber otro código asíncrono en vuelo cuando desmontamos una interfaz. Las tareas del taskqueue, los manejadores de interrupción y las funciones de rearmado basadas en temporizador deben detenerse antes de liberar la memoria.

El kernel proporciona `taskqueue_drain(tq, task)` para las tareas del taskqueue, de forma análoga a `callout_drain` para los callouts. Para las interrupciones, `bus_teardown_intr` y `bus_release_resource` garantizan que el manejador de interrupción no vuelva a ser invocado. Para los callouts rearmables en los que el manejador se reprograma a sí mismo, `callout_drain` sigue haciendo lo correcto: espera a que termine la invocación actual e impide nuevos rearmados.

Una regla general para la secuencia de desmontaje:

1. Borra los flags de "en ejecución" o "armado" que comprueba el código asíncrono.
2. Drena cada fuente asíncrona en orden (taskqueue, callout, interrupción).
3. Desconéctate de las capas superiores (`ether_ifdetach`).
4. Libera la memoria.

Saltarse el paso 1 suele ser la causa de los pánicos "destroying locked mutex", porque el código asíncrono sigue ejecutándose cuando se destruye el mutex. Saltarse el paso 2 es la causa de los errores use-after-free. Los pasos 3 y 4 deben ocurrir en ese orden, o el stack puede intentar llamar a nuestros callbacks después de que hayan sido liberados.

### Un escenario de error ilustrado

Para hacer lo anterior más concreto, imagina un bug sutil. Supón que durante el desarrollo llamamos a `mtx_destroy` antes que a `callout_drain`. El callout está programado, el usuario ejecuta `ifconfig mynet0 destroy`, nuestra función destroy destruye el mutex, y entonces el callout programado se dispara. El callout intenta adquirir el mutex (porque lo registramos con `callout_init_mtx`), encuentra un mutex destruido y lanza una aserción: "acquiring a destroyed mutex". El sistema entra en pánico con un stack trace que apunta al código del callout.

La solución es invertir el orden: primero `callout_drain`, y `mtx_destroy` después. El principio general es que las primitivas de sincronización se destruyen al final, una vez que se sabe con certeza que todos sus consumidores han parado.

Este tipo de bug es fácil de introducir y difícil de diagnosticar si no lo has visto antes. Tener un modelo mental explícito de "silenciar, desconectar, liberar" lo previene.

### Cerrando la sección 8

El ciclo de vida completo está ahora en tus manos. Carga, registro del cloner, creación por interfaz, vida activa con transmisión, recepción, ioctl y eventos de enlace, destrucción por interfaz, desconexión del cloner y descarga del módulo. Puedes construir, probar, desmontar y reconstruir con la seguridad de que el kernel vuelve a un estado limpio.

Las secciones que vienen a continuación son la parte práctica del capítulo: laboratorios que te guían por los hitos que hemos descrito, desafíos que amplían el driver, indicaciones para la resolución de problemas y un resumen final.

## Laboratorios prácticos

Los laboratorios que siguen están ordenados para reflejar el flujo del capítulo. Cada uno se apoya en el anterior, así que realízalos en orden. Los archivos complementarios se encuentran en `examples/part-06/ch28-network-driver/`, y cada laboratorio tiene su propio README con los comandos específicos.

Antes de empezar, asegúrate de estar en una VM de laboratorio con FreeBSD 14.3 con acceso de root, un directorio de trabajo limpio donde puedas construir un módulo del kernel, y un estado de snapshot reciente al que puedas volver si algo sale mal. Hacer un snapshot antes de comenzar los laboratorios es una pequeña inversión que se amortiza la primera vez que la necesitas.

Cada laboratorio termina con un breve bloque de "punto de control" que enumera las observaciones concretas que debes registrar en tu cuaderno de notas. Si tu cuaderno ya contiene esas observaciones, puedes continuar. Si no es así, vuelve al paso anterior y repítelo. La estructura acumulativa de los laboratorios implica que una observación omitida en el Laboratorio 2 hará que el Laboratorio 4 resulte confuso.

### Laboratorio 1: compilar y cargar el esqueleto

**Objetivo.** Construir el driver esqueleto de la sección 3, cargarlo, crear una instancia y observar el estado por defecto.

**Pasos.**

1. `cd examples/part-06/ch28-network-driver/`
2. `make` y observa si hay advertencias. El build debe producir `mynet.ko` sin advertencias.
3. `kldload ./mynet.ko`. No deberían aparecer mensajes en la consola; `kldstat` debería listar `mynet` como presente.
4. `ifconfig mynet create` debería imprimir `mynet0`.
5. `ifconfig mynet0` y registra la salida en tu cuaderno de notas. Fíjate especialmente en los flags, la dirección MAC, la línea de media y el estado.
6. `kldstat -v | grep mynet` y verifica que el módulo está presente y cargado en la dirección esperada.
7. `sysctl net.generic.ifclone` y confirma que `mynet` aparece en la lista de cloners.
8. `ifconfig mynet0 destroy`. La interfaz debería desaparecer.
9. `kldunload mynet`. El módulo debería descargarse limpiamente.
10. `kldstat` e `ifconfig -a` para confirmar que no queda nada.

**Qué observar.** La salida de `ifconfig mynet0` debe mostrar los flags `BROADCAST,SIMPLEX,MULTICAST`, una dirección MAC, una línea de media "Ethernet autoselect" y un estado "no carrier". Si falta alguno de estos elementos, revisa la función `mynet_create_unit` y la llamada a `ifmedia_init`.

**Punto de control del cuaderno.**

* Registra la dirección MAC exacta asignada a `mynet0`.
* Registra el valor inicial de `if_mtu`.
* Anota los flags reportados antes y después de `ifconfig mynet0 up`.
* Observa si `status:` cambia entre "no carrier" y "active".

**Si algo sale mal.** El fallo más frecuente en el Laboratorio 1 es un error de compilación causado por una cabecera que falta. Asegúrate de que el árbol de código fuente del kernel en `/usr/src/sys/` coincide con la versión del kernel en ejecución. Si `kldload` falla con "module already present", descarga cualquier instancia anterior con `kldunload mynet` e inténtalo de nuevo. Si `ifconfig mynet create` devuelve "Operation not supported", el cloner no se registró y debes revisar la llamada a `VNET_SYSINIT`.

### Laboratorio 2: ejercitar la ruta de transmisión

**Objetivo.** Verificar que `if_transmit` se llama cuando el tráfico sale por la interfaz.

**Pasos.**

1. Crea la interfaz y ponla en marcha como en el Laboratorio 1.
2. `ifconfig mynet0 inet 192.0.2.1/24 up`. Ahora deberían aparecer los flags `UP` y `RUNNING`.
3. En una terminal, ejecuta `tcpdump -i mynet0 -nn`.
4. En otra, ejecuta `ping -c 3 192.0.2.99`.
5. Observa el tráfico ARP e ICMP impreso por `tcpdump`.
6. `netstat -in -I mynet0` y registra los contadores. La columna `Opkts` debería mostrar al menos cuatro (tres peticiones ICMP más los intentos de broadcast ARP).
7. Modifica la función de transmisión para que devuelva `ENOBUFS` en cada llamada y reconstruye.
8. Descarga y recarga, repite el `ping`, observa que `Opkts` deja de crecer y que `Oerrors` aumenta en su lugar.
9. Revierte la modificación y reconstruye.
10. Opcional: ejecuta el one-liner de DTrace `dtrace -n 'fbt::mynet_transmit:entry { @c = count(); }'` mientras generas tráfico para confirmar que cada llamada llega a tu función de transmisión.

**Qué observar.** En el paso 5, cada `ping` produce un broadcast ARP (porque el stack no conoce la MAC de `192.0.2.99`) y una petición de echo ICMP por cada intento de ping, pero la respuesta ARP nunca llega, por lo que los pings posteriores solo añaden peticiones ICMP. Comprender el motivo, y cómo se muestra en `tcpdump`, es una parte importante de este laboratorio.

**Punto de control del cuaderno.**

* Registra el número exacto de `Opkts` tras tres pings.
* Registra el recuento de `Obytes` y verifica que coincide con la suma esperada de la trama ARP (42 bytes) más tres tramas ICMP.
* Anota qué cambia en `Oerrors` cuando devuelves `ENOBUFS` deliberadamente.

**Si algo sale mal.** Si `Opkts` es cero tras los pings, tu callback `if_transmit` no está siendo llamado. Comprueba que `ifp->if_transmit = mynet_transmit` se establece durante la creación. Si `Obytes` crece pero `Opkts` no, una de las llamadas al contador falta o apunta al contador equivocado. Si `tcpdump` no muestra ningún tráfico saliente, falta el tap BPF en la transmisión; añade `BPF_MTAP(ifp, m)` antes de la liberación.

### Laboratorio 3: ejercitar la ruta de recepción

**Objetivo.** Verificar que `if_input` entrega paquetes al stack.

**Pasos.**

1. Crea la interfaz y ponla en marcha.
2. `tcpdump -i mynet0 -nn`.
3. Espera cinco segundos y confirma que aparece una petición ARP sintetizada por segundo.
4. `netstat -in -I mynet0` y confirma que `Ipkts` coincide con el recuento de paquetes.
5. Cambia `sc->rx_interval_hz = hz / 10;` y reconstruye.
6. Descarga, recarga y vuelve a crear. Observa que la tasa pasa a ser diez paquetes por segundo.
7. Vuelve a un paquete por segundo.
8. Opcional: comenta la llamada a `BPF_MTAP` en la ruta de recepción, reconstruye y observa que `tcpdump` ya no muestra el ARP sintetizado pero `Ipkts` sigue incrementándose. Esto confirma que la visibilidad del BPF y las actualizaciones del contador son independientes.
9. Opcional: comenta la llamada a `if_input` (deja `BPF_MTAP` en su sitio), reconstruye y observa el comportamiento contrario: `tcpdump` ve la trama, pero `Ipkts` no se mueve porque el stack nunca recibió realmente la trama.

**Qué observar.** El contador `Ipkts` debe incrementarse exactamente una vez por cada trama sintetizada. Si no lo hace, puede que el tap BPF esté viendo la trama pero `if_input` no esté siendo llamado, o que las llamadas estén compitiendo con el desmontaje.

**Punto de control del cuaderno.**

* Registra el intervalo entre ARPs sintetizados consecutivos, tal como muestran las marcas de tiempo de `tcpdump`.
* Registra las direcciones MAC en la trama ARP y confirma que la MAC de origen coincide con la dirección de la interfaz.
* Observa lo que muestra `arp -an` antes y después; las entradas de `192.0.2.99` deben permanecer incompletas.

**Si algo sale mal.** Si no aparece ningún ARP sintetizado en `tcpdump`, el callout no se está disparando. Comprueba que `callout_reset` se llama en `mynet_init` y que `sc->running` es true en ese momento. Si `tcpdump` muestra el ARP pero `Ipkts` es cero, el contador no se está actualizando, o bien se actualiza después de `if_input`, que ya ha liberado el mbuf.

### Laboratorio 4: media y estado del enlace

**Objetivo.** Observar la diferencia entre el estado del enlace, el media y los flags de la interfaz.

**Pasos.**

1. Crea y configura la interfaz.
2. `ifconfig mynet0` y anota las líneas `status` y `media`.
3. `ifconfig mynet0 down`.
4. `ifconfig mynet0` y observa que `status` cambia.
5. `ifconfig mynet0 up`.
6. En otra terminal, ejecuta `route monitor` y repite los pasos 3 y 5 mientras observas la salida.
7. `ifconfig mynet0 media 1000baseT mediaopt full-duplex` y confirma que `ifconfig mynet0` refleja el cambio.
8. Añade una tercera entrada de media `IFM_ETHER | IFM_100_TX | IFM_FDX` a `mynet_create_unit`, reconstruye y verifica que `ifconfig mynet0 media 100baseTX mediaopt full-duplex` funciona ahora con éxito.
9. Elimina la entrada y reconstruye. Verifica que el mismo comando falla ahora con "requested media type not found".

**Qué observar.** `route monitor` imprime mensajes `RTM_IFINFO` en cada transición de estado del enlace. La línea `status:` de `ifconfig mynet0` muestra `active` cuando el driver está en ejecución y el enlace está activo, y `no carrier` cuando el driver ha llamado a `LINK_STATE_DOWN`.

**Punto de control del cuaderno.**

* Registra el texto exacto del mensaje `RTM_IFINFO` de `route monitor`.
* Anota la diferencia entre `IFF_UP` y `LINK_STATE_UP` capturando la salida de `ifconfig mynet0` en cada una de las cuatro combinaciones posibles (activo o inactivo combinado con enlace activo o inactivo).
* Observa si `status:` y los flags de la interfaz se mantienen consistentes en los cuatro estados.

**Si algo sale mal.** Si `status:` permanece en "no carrier" incluso después de que la interfaz esté activa, no estás llamando a `if_link_state_change(ifp, LINK_STATE_UP)` desde `mynet_init`. Si `ifconfig mynet0 media 1000baseT` falla con "requested media type not found", no registraste `IFM_ETHER | IFM_1000_T` mediante `ifmedia_add`, o lo registraste con los flags incorrectos.

### Laboratorio 5: `tcpdump` y BPF

**Objetivo.** Confirmar que BPF ve tanto los paquetes salientes como los entrantes.

**Pasos.**

1. Crea y configura la interfaz con la IP `192.0.2.1/24`.
2. `tcpdump -i mynet0 -nn > /tmp/dump.txt &`
3. Espera diez segundos.
4. `ping -c 3 192.0.2.99`.
5. Espera otros diez segundos.
6. `kill %1`.
7. `cat /tmp/dump.txt` e identifica las solicitudes ARP sintetizadas, los broadcasts ARP generados por tu `ping` y las solicitudes de eco ICMP.
8. Elimina la llamada a `BPF_MTAP` de `mynet_transmit` y vuelve a compilar. Repite el proceso. Observa que el tráfico ICMP saliente ya no aparece en la salida de `tcpdump`.
9. Restaura la llamada a `BPF_MTAP`.
10. Experimenta con filtros: `tcpdump -i mynet0 -nn 'arp'` debería mostrar solo los ARP sintetizados y los ARP de tus pings, mientras que `tcpdump -i mynet0 -nn 'icmp'` debería mostrar únicamente las solicitudes de eco ICMP.
11. Observa la línea de tipo de enlace en la salida inicial de `tcpdump`. Debería indicar `EN10MB (Ethernet)`, ya que `ether_ifattach` lo configuró así. Si indica `NULL`, la interfaz se conectó sin semántica Ethernet.

**Qué observar.** El ejercicio demuestra que la visibilidad de BPF no es automática para cada paquete. Es responsabilidad del driver hacer el tap tanto en la ruta de transmisión como en la de recepción.

**Punto de control del cuaderno de bitácora.**

* Registra una línea completa de la salida de `tcpdump` para cada tipo de trama que hayas observado: ARP sintetizado, ARP saliente y solicitud de eco ICMP saliente.
* Registra la línea de tipo de enlace tal como la imprime `tcpdump`.
* Anota qué ocurre con la salida cuando eliminas `BPF_MTAP` de la ruta de transmisión.

**Si algo sale mal.** Si `tcpdump` no muestra ningún paquete, `bpfattach` no fue invocado (normalmente porque olvidaste llamar a `ether_ifattach`). Si muestra los paquetes recibidos pero no los transmitidos, falta el tap de transmisión. Si muestra los paquetes transmitidos pero no los recibidos, falta el tap de recepción. Si el tipo de enlace es incorrecto, el tipo de interfaz o la llamada a `bpfattach` están mal configurados.

### Laboratorio 6: Desmontaje limpio

**Objetivo.** Comprobar que la descarga devuelve el sistema a un estado limpio.

**Pasos.**

1. Crea tres interfaces: ejecuta `mynet create` tres veces.
2. Configura cada una con una IP diferente en `192.0.2.0/24` (por ejemplo, `192.0.2.1/24`, `192.0.2.2/24`, `192.0.2.3/24`).
3. Ejecuta `vmstat -m | grep mynet` y anota el recuento de asignaciones.
4. Ejecuta `kldunload mynet` (sin destruirlas antes).
5. Ejecuta `ifconfig -a` y confirma que ninguna de `mynet0`, `mynet1`, `mynet2` sigue presente.
6. Ejecuta `vmstat -m | grep mynet` y confirma que `InUse` vuelve a cero.
7. Repite los pasos del 1 al 6 cinco veces seguidas. Cada ronda debe dejar `InUse` en cero y no debe dejar ningún estado huérfano.
8. Opcional: introduce un bug artificial eliminando la llamada a `callout_drain` de `mynet_destroy`. Recompila, carga, crea una interfaz y descarga. Observa qué ocurre (normalmente es un pánico, y es una forma dramática de entender por qué existe `callout_drain`).
9. Restaura la llamada a `callout_drain`.

**Qué observar.** La ruta de desmontaje del cloner debe iterar sobre las tres interfaces, llamar a `mynet_clone_destroy` en cada una y liberar toda la memoria. Si alguna interfaz permanece, o si `InUse` es distinto de cero, algo en el proceso de desmontaje está roto.

**Punto de registro en el cuaderno de bitácora.**

* Anota los valores de `InUse` antes y después de cada ronda de carga-creación-descarga.
* Observa la columna `Requests` en `vmstat -m | grep mynet`; debe crecer de forma monotónica porque registra las asignaciones acumuladas durante toda la vida del módulo.
* Anota cualquier mensaje inesperado en `dmesg`.

**Si algo va mal.** Si `kldunload` se queda colgado, hay un callout o una tarea de taskqueue que todavía está en ejecución. Usa `ps -auxw` para encontrar el thread del kernel y `procstat -k <pid>` para ver su backtrace. Si `InUse` se mantiene por encima de cero tras la descarga, hay una fuga de memoria; el sospechoso habitual es que `mynet_destroy` no se esté llamando en alguna de las interfaces, lo que significa que `if_clone_detach` no la encontró.

### Laboratorio 7: Leyendo el árbol real

**Objetivo.** Conectar lo que has construido con lo que existe en `/usr/src/sys/net/`.

**Pasos.**

1. Abre `/usr/src/sys/net/if_disc.c` en paralelo con tu `mynet.c`. Para cada uno de los puntos siguientes, localiza el código correspondiente en ambos archivos:
   * Registro del cloner.
   * Asignación del softc.
   * Tipo de interfaz (`IFT_LOOP` frente a `IFT_ETHER`).
   * Attach de BPF.
   * Ruta de transmisión.
   * Gestión de ioctl.
   * Destrucción del cloner.
2. Abre `/usr/src/sys/net/if_epair.c` y realiza el mismo ejercicio. Observa el uso de `if_clone_advanced`, la lógica de emparejamiento y el uso de `ifmedia_init`.
3. Abre `/usr/src/sys/net/if_ethersubr.c` y localiza `ether_ifattach`. Recórrela línea por línea y compara cada línea con lo que dijimos que hace en la Sección 3.
4. Abre `/usr/src/sys/net/bpf.c` y localiza `bpf_mtap_if`, que es la función a la que expande `BPF_MTAP`. Observa la comprobación de ruta rápida para peers activos.

**Qué observar.** El objetivo de este laboratorio es el reconocimiento, no la comprensión exhaustiva. No es necesario entender cada línea de `epair(4)` o de `ether_ifattach`. Basta con ver que los mismos patrones que usamos en nuestro driver aparecen también en el árbol real, y que el nuevo código que puedas encontrar en otro lugar es una variación sobre los temas que ya conoces.

**Punto de registro en el cuaderno de bitácora.**

* Anota un nombre de función de cada uno de `if_disc.c`, `if_epair.c` e `if_ethersubr.c` que ahora entiendas lo suficientemente bien como para explicarla en voz alta.
* Señala cualquier patrón en estos archivos que te haya sorprendido o que contradiga alguna suposición que habías construido a partir del capítulo.

## Ejercicios de desafío

Los desafíos que se presentan a continuación amplían el driver en direcciones pequeñas e independientes entre sí. Cada uno está pensado para completarse en una o dos sesiones concentradas y se apoya únicamente en lo que el capítulo ha enseñado.

### Desafío 1: Cola compartida entre interfaces emparejadas

**Descripción.** Modifica `mynet` para que al crear dos interfaces emparejadas (`mynet0a` y `mynet0b`) el comportamiento sea similar al de `epair(4)`: transmitir en una interfaz hace que la trama aparezca en la otra.

**Pistas.** Usa `if_clone_advanced` con una función de coincidencia, tal como hace `epair.c`. Comparte una cola entre las dos estructuras softc. Usa un callout o un taskqueue para desencolar en el otro lado y llamar a `if_input`.

**Resultado esperado.** Cuando hagas `ping` a una IP asignada a `mynet0a` desde una IP asignada a `mynet0b`, las respuestas deben volver realmente. Has construido una simulación por software de dos cables conectados entre sí.

**Preguntas clave de diseño.** ¿Dónde almacenas la cola compartida? ¿Cómo garantizas que un paquete enviado por un lado no pueda ser visto por el propio emisor (el contrato `IFF_SIMPLEX`)? ¿Cómo gestionas el caso en que solo uno de los lados del par está activo?

**Estructura sugerida.** Añade una `struct mynet_pair` que posea dos softcs, y haz que cada softc lleve un puntero al par. La función de transmisión del lado A encola el mbuf en la cola de entrada del lado B y programa el taskqueue. El taskqueue desencola y llama a `if_input` en el lado B. Usa un mutex en la estructura del par para proteger la cola.

### Desafío 2: Simulación de fluctuación del enlace

**Descripción.** Añade un sysctl `net.mynet.flap_interval` que, cuando es distinto de cero, hace que el driver haga fluctuar el enlace entre activo e inactivo cada `flap_interval` segundos.

**Pistas.** Usa un callout que llame a `if_link_state_change` alternativamente con `LINK_STATE_UP` y `LINK_STATE_DOWN`. Observa el efecto en `route monitor`.

**Resultado esperado.** Mientras la fluctuación esté activa, `ifconfig mynet0` debe alternar entre `status: active` y `status: no carrier` al intervalo elegido. `route monitor` debe imprimir mensajes `RTM_IFINFO` en cada transición.

**Extensión.** Haz que el intervalo de fluctuación sea por interfaz en lugar de global. Puedes hacerlo creando un nodo sysctl por instancia bajo `net.mynet.<ifname>`, lo que requiere usar `sysctl_add_oid` y APIs similares de sysctl dinámico.

### Desafío 3: Inyección de errores

**Descripción.** Añade un sysctl `net.mynet.drop_rate` que establezca el porcentaje de tramas salientes que deben descartarse con un error.

**Pistas.** En `mynet_transmit`, genera un número aleatorio con `arc4random`. Si cae por debajo del porcentaje configurado, incrementa `IFCOUNTER_OERRORS`, libera el mbuf y retorna. En caso contrario, continúa como antes.

**Resultado esperado.** Con `drop_rate` a 50, `ping` debe mostrar aproximadamente un 50% de pérdida de paquetes en lugar del 100%. (Recuerda que la "pérdida del 100%" sin drop_rate se debía a que nunca volvía ninguna respuesta, no a un descarte en la transmisión. Por tanto, con drop_rate=50 el ping seguirá mostrando un 100% de pérdida; pero si combinas este desafío con la pareja del Desafío 1, el comportamiento combinado debería ser del 50% de pérdida en el ping.)

**Extensión.** Añade un `rx_drop_rate` separado que descarte las tramas de recepción sintetizadas. Observa en qué se diferencia la salida de los contadores de recepción respecto a los de transmisión cuando se producen los descartes.

### Desafío 4: Estrés con iperf3

**Descripción.** Usa `iperf3` para estresar la ruta de transmisión y medir con qué rapidez puede procesar tramas el driver.

**Pistas.** Ejecuta `iperf3 -c 192.0.2.99 -t 10 -u -b 1G` para generar una ráfaga UDP. Observa `netstat -in -I mynet0` antes y después. Observa `vmstat 1` para ver la carga del sistema. Reflexiona sobre qué habría que cambiar en el driver para soportar tasas más altas: contadores por CPU, rutas de transmisión sin lock, procesamiento diferido basado en taskqueue.

**Resultado esperado.** La ejecución de iperf3 no producirá cifras de ancho de banda significativas (porque no hay ningún servidor que acuse recibo), pero hará subir `Opkts` rápidamente. Vigila si aparece algún punto caliente de CPU en la ruta de transmisión. Si has combinado esto con el Desafío 1, la configuración con interfaces emparejadas debería mostrar paquetes cruzando el enlace simulado.

**Consejos de medición.** Usa `pmcstat` o `dtrace` para perfilar dónde se invierte el tiempo. La ruta de transmisión es un lugar razonable donde buscar contención de locks. Si observas una tasa elevada de `mtx_lock` en el mutex del softc dentro de `mynet_transmit`, es señal de que estás contendiendo en un lock que los drivers reales dividirían por cola.

### Desafío 5: Árbol sysctl por interfaz

**Descripción.** Expón controles en tiempo de ejecución y estadísticas por interfaz bajo `net.mynet.mynet0.*`.

**Pistas.** Usa `sysctl_add_oid` para añadir dinámicamente sysctls por interfaz cuando se crea la interfaz, y elimínalos cuando se destruye. Un patrón habitual es crear un contexto por instancia bajo un nodo raíz estático, y añadir hojas hijo para los controles y estadísticas específicos.

**Resultado esperado.** `sysctl net.mynet.mynet0.rx_interval_hz` debe poder leerse y escribirse, sobrescribiendo el valor predeterminado en tiempo de compilación. `sysctl net.mynet.mynet0.rx_packets_generated` debe leer un contador que se incrementa cada vez que se dispara el temporizador de recepción sintética.

**Extensión.** Añade un sysctl `rx_enabled` que pause y reanude el temporizador de recepción sintética. Verifica el comportamiento observando `tcpdump` mientras activas y desactivas el sysctl.

### Desafío 6: Nodo de netgraph

**Descripción.** Expón `mynet` como un nodo de netgraph para que pueda conectarse al framework netgraph.

**Pistas.** Este es un desafío más extenso porque requiere familiarizarse con `netgraph(4)`. Lee `/usr/src/sys/netgraph/ng_ether.c` como ejemplo de referencia de una interfaz expuesta como nodo de netgraph. Añade un único hook que proporcione intercepción de paquetes antes o después de nuestro `if_transmit` y `if_input`.

**Resultado esperado.** Una vez presente el nodo de netgraph, deberías poder usar `ngctl` para adjuntar un nodo de filtrado o redirección y observar cómo los paquetes fluyen a través de la cadena de netgraph.

Este desafío es el más abierto del conjunto. Si consigues un esqueleto funcional, habrás completado esencialmente el camino desde un driver "hola mundo" hasta un driver que participa plenamente en la infraestructura de red avanzada de FreeBSD.

## Solución de problemas y errores frecuentes

Los drivers de red fallan de unas pocas maneras características. Aprender a reconocerlas te ahorrará horas de depuración.

### Síntoma: `ifconfig mynet create` devuelve "Operation not supported"

**Causa probable.** El cloner no está registrado, o el nombre del cloner no coincide. Comprueba que `V_mynet_cloner` se inicializa en `vnet_mynet_init`, y que la cadena `mynet_name` es la que el usuario está escribiendo.

**Diagnóstico.** `sysctl net.generic.ifclone` lista todos los cloners registrados. Si `mynet` no aparece, el registro no se ha producido.

### Síntoma: `ifconfig mynet0 up` se queda colgado o entra en pánico

**Causa probable.** La función `mynet_init` está haciendo algo que duerme mientras mantiene el mutex del softc, o está llamando hacia arriba en la pila con el mutex retenido.

**Diagnóstico.** Si el sistema se queda colgado, entra en el depurador (`Ctrl-Alt-Esc` en una consola) y escribe `ps` para ver qué thread está bloqueado, luego `trace TID` para obtener un backtrace. Busca la adquisición de lock problemática.

### Síntoma: `tcpdump -i mynet0` no captura ningún paquete

**Causa probable.** `BPF_MTAP` no se está llamando, o `bpfattach` no se llamó durante la configuración de la interfaz.

**Diagnóstico.** `bpf_peers_present(ifp->if_bpf)` debe devolver true cuando `tcpdump` está en ejecución. Si no es así, comprueba que se llamó a `ether_ifattach`. Si `ether_ifattach` se llamó pero `BPF_MTAP` no está en la ruta de datos, añade la llamada tanto en la transmisión como en la recepción.

### Síntoma: `ping` muestra 100% de pérdida (esperado) pero `Opkts` se queda en cero

**Causa probable.** `if_transmit` no se está llamando, o está retornando antes de tiempo sin incrementar los contadores.

**Diagnóstico.** `dtrace -n 'fbt::mynet_transmit:entry { @[probefunc] = count(); }'` cuenta cuántas veces se llama a la función. Si es cero, la pila no nos está despachando, y la asignación a `ifp->if_transmit` (o, si usaste el helper, la llamada a `if_settransmitfn`) durante la configuración es sospechosa.

### Síntoma: `kldunload` entra en pánico con "destroying locked mutex"

**Causa probable.** El mutex se está destruyendo mientras otro thread (típicamente un callout) todavía lo mantiene retenido.

**Diagnóstico.** Revisa el orden de desmontaje. `callout_drain` debe llamarse antes que `mtx_destroy`. `ether_ifdetach` debe llamarse antes que `if_free`. Si el callout adquiere el mutex del softc, `callout_drain` debe ejecutarse antes de que ese mutex desaparezca.

### Síntoma: `netstat -in -I mynet0` muestra `Opkts` más alto que `Opkts` en `systat -if`

**Causa probable.** Uno de los contadores se está incrementando dos veces en la ruta de transmisión.

**Diagnóstico.** Inspecciona las rutas del código. Un error frecuente es incrementar `IFCOUNTER_OPACKETS` tanto en el driver como en una función auxiliar.

### Síntoma: el módulo carga pero `ifconfig mynet create` produce un aviso del kernel

**Causa probable.** Un campo de `ifnet` no está correctamente inicializado, o se llama a `ether_ifattach` sin una dirección MAC válida.

**Diagnóstico.** Ejecuta `dmesg` tras el aviso. El kernel suele imprimir contexto suficiente para identificar el campo problemático.

### Síntoma: `kldunload` regresa pero `ifconfig -a` sigue mostrando `mynet0`

**Causa probable.** El detach del cloner no iteró todas las interfaces. Esto suele ser señal de que la interfaz se creó fuera del camino del cloner, o de que las estructuras de datos de `if_clone` están desincronizadas.

**Diagnóstico.** `sysctl net.generic.ifclone` tras la descarga no debería listar `mynet`. Si lo hace, `if_clone_detach` no completó la operación.

### Síntoma: pánicos intermitentes bajo carga de `iperf3`

**Causa probable.** Una condición de carrera entre la ruta de transmisión y la ruta de ioctl, normalmente porque una de ellas no adquiere el lock cuando la otra sí lo hace.

**Diagnóstico.** Ejecuta el kernel con `INVARIANTS` y `WITNESS` activados. Estas opciones añaden comprobaciones de orden de lock y aserciones que detectan la mayoría de condiciones de carrera de forma inmediata. Son la herramienta de desarrollo más valiosa para los drivers de red.

### Síntoma: `ifconfig mynet0 mtu 9000` tiene éxito pero los jumbo frames fallan

**Causa probable.** El driver anuncia un rango de MTU que no puede transportar en realidad. Nuestro driver de referencia usa un rango amplio por simplicidad, pero un driver real tiene un límite superior estricto impuesto por el hardware.

**Diagnóstico.** Envía un frame mayor que el MTU configurado y observa que `IFCOUNTER_OERRORS` se incrementa. Ajusta el límite superior anunciado a la capacidad real.

### Síntoma: `dmesg` muestra "acquiring a destroyed mutex"

**Causa probable.** Un callout, una tarea del taskqueue o un manejador de interrupción está adquiriendo un mutex después de que se ha llamado a `mtx_destroy`. Casi siempre se debe a un orden de desmontaje incorrecto.

**Diagnóstico.** Recorre tu `mynet_destroy` paso a paso. Las operaciones `callout_drain` y equivalentes deben producirse antes que `mtx_destroy`. El orden correcto es "quiesce, detach, destroy", no "destroy, quiesce".

### Síntoma: `WITNESS` reporta una inversión del orden de lock

**Causa probable.** Dos threads adquieren el mismo par de locks en órdenes opuestos. En un driver de red, esto ocurre con mayor frecuencia entre el mutex del softc y un lock interno de la pila como el lock de la tabla ARP o el lock de la tabla de rutas.

**Diagnóstico.** Lee el resultado de `WITNESS` con atención; muestra ambos backtraces. La solución habitual es liberar el mutex del driver antes de llamar a la pila (por ejemplo, antes de `if_input` o `if_link_state_change`), tal y como recomendamos a lo largo de este capítulo.

### Síntoma: pérdida de paquetes bajo carga moderada

**Causa probable.** Agotamiento de mbufs (comprueba `vmstat -z | grep mbuf`) o una cola de transmisión sin backpressure que descarta paquetes en silencio.

**Diagnóstico.** Ejecuta `vmstat -z | grep mbuf` antes y después de la carga. Si las asignaciones de `mbuf` o `mbuf_cluster` suben pero no se liberan, tienes una fuga de mbufs. Si se liberan pero la cola interna de tu driver descarta paquetes, necesitas ampliar la cola o implementar backpressure.

### Síntoma: `ifconfig mynet0 inet6 2001:db8::1/64` no tiene efecto

**Causa probable.** IPv6 no está compilado en tu kernel, o la interfaz no anuncia `IFF_MULTICAST` (que IPv6 requiere).

**Diagnóstico.** `sysctl net.inet6.ip6.v6only` y sysctls similares te indican si IPv6 está disponible. `ifconfig mynet0` muestra los flags; asegúrate de que `MULTICAST` es uno de ellos.

### Síntoma: el módulo carga pero `ifconfig mynet create` no crea ninguna interfaz ni produce ningún error

**Causa probable.** La función create del cloner devuelve éxito pero nunca asigna realmente una interfaz. Es fácil provocarlo devolviendo 0 antes de llamar a `if_alloc`.

**Diagnóstico.** Añade un `printf("mynet_clone_create called\n")` al inicio de tu callback de creación. Si aparece el mensaje pero no se crea ninguna interfaz, el error está entre el printf y la llamada a `if_attach`.

### Síntoma: `sysctl net.link.generic` devuelve resultados inesperados

**Causa probable.** El driver ha corrompido un campo de `ifnet` que lee el manejador genérico de sysctl. Es poco frecuente, pero indica problemas más profundos.

**Diagnóstico.** Ejecuta el kernel con `INVARIANTS` y busca fallos de aserción. La escritura problemática suele encontrarse cerca de donde se inicializan los campos de `ifnet`.

## Tablas de referencia rápida

Las tablas siguientes resumen las APIs y constantes de uso más frecuente introducidas en este capítulo. Mantenlas a la vista mientras realizas los laboratorios.

### Funciones de ciclo de vida

| Función | Propósito |
| --- | --- |
| `if_alloc(type)` | Asigna un nuevo `ifnet` del tipo IFT_ indicado. |
| `if_free(ifp)` | Libera un `ifnet` tras el detach. |
| `if_attach(ifp)` | Registra la interfaz en la pila. |
| `if_detach(ifp)` | Desregistra la interfaz. |
| `ether_ifattach(ifp, mac)` | Registra una interfaz de tipo Ethernet. Envuelve `if_attach` junto con `bpfattach` y establece los valores predeterminados de Ethernet. |
| `ether_ifdetach(ifp)` | Deshace `ether_ifattach`. |
| `if_initname(ifp, family, unit)` | Establece el nombre de la interfaz. |
| `bpfattach(ifp, dlt, hdrlen)` | Registra con BPF manualmente. Lo hace automáticamente `ether_ifattach`. |
| `bpfdetach(ifp)` | Desregistra de BPF. Lo hace automáticamente `ether_ifdetach`. |
| `if_clone_simple(name, create, destroy, minifs)` | Registra un cloner simple. |
| `if_clone_advanced(name, minifs, match, create, destroy)` | Registra un cloner con una función match personalizada. |
| `if_clone_detach(cloner)` | Desmonta un cloner y todas sus interfaces. |
| `callout_init_mtx(co, mtx, flags)` | Inicializa un callout asociado a un mutex. |
| `callout_reset(co, ticks, fn, arg)` | Programa o reprograma un callout. |
| `callout_stop(co)` | Cancela un callout. |
| `callout_drain(co)` | Espera de forma síncrona a que un callout finalice. |
| `ifmedia_init(ifm, mask, change, status)` | Inicializa un descriptor de medios. |
| `ifmedia_add(ifm, word, data, aux)` | Añade una entrada de medios soportada. |
| `ifmedia_set(ifm, word)` | Elige el medio predeterminado. |
| `ifmedia_ioctl(ifp, ifr, ifm, cmd)` | Gestiona `SIOCGIFMEDIA` y `SIOCSIFMEDIA`. |
| `ifmedia_removeall(ifm)` | Libera todas las entradas de medios durante el desmontaje. |

### Funciones de la ruta de datos

| Función | Propósito |
| --- | --- |
| `if_transmit(ifp, m)` | El callback de salida del driver. |
| `if_input(ifp, m)` | Entrega un mbuf a la pila. |
| `if_qflush(ifp)` | Vacía las colas internas del driver. |
| `BPF_MTAP(ifp, m)` | Captura un frame en BPF si hay observadores. |
| `bpf_mtap2(bpf, data, dlen, m)` | Captura con una cabecera antepuesta. |
| `m_freem(m)` | Libera una cadena de mbufs completa. |
| `m_free(m)` | Libera un único mbuf. |
| `MGETHDR(m, how, type)` | Asigna un mbuf como cabeza de un paquete. |
| `MGET(m, how, type)` | Asigna un mbuf como continuación de una cadena. |
| `m_gethdr(how, type)` | Forma alternativa de MGETHDR. |
| `m_pullup(m, len)` | Garantiza que los primeros len bytes son contiguos. |
| `m_copydata(m, off, len, buf)` | Lee bytes de una cadena sin consumirla. |
| `m_defrag(m, how)` | Aplana una cadena en un único mbuf. |
| `mtod(m, type)` | Convierte `m_data` al tipo solicitado. |
| `if_inc_counter(ifp, ctr, n)` | Incrementa un contador por interfaz. |
| `if_link_state_change(ifp, state)` | Notifica una transición de enlace. |

### Flags `IFF_` habituales

| Flag | Significado |
| --- | --- |
| `IFF_UP` | Activa administrativamente. Controlada por el usuario. |
| `IFF_BROADCAST` | Admite broadcast. |
| `IFF_DEBUG` | Rastreo de depuración solicitado. |
| `IFF_LOOPBACK` | Interfaz de loopback. |
| `IFF_POINTOPOINT` | Enlace punto a punto. |
| `IFF_RUNNING` | Alias de `IFF_DRV_RUNNING`. |
| `IFF_NOARP` | ARP deshabilitado. |
| `IFF_PROMISC` | Modo promiscuo. |
| `IFF_ALLMULTI` | Recibe todo el multicast. |
| `IFF_SIMPLEX` | No puede recibir sus propias transmisiones. |
| `IFF_MULTICAST` | Admite multicast. |
| `IFF_DRV_RUNNING` | Privado del driver: recursos asignados. |
| `IFF_DRV_OACTIVE` | Privado del driver: cola de transmisión llena. |

### Capacidades `IFCAP_` habituales

| Capacidad | Significado |
| --- | --- |
| `IFCAP_RXCSUM` | Offload de checksum de recepción IPv4. |
| `IFCAP_TXCSUM` | Offload de checksum de transmisión IPv4. |
| `IFCAP_RXCSUM_IPV6` | Offload de checksum de recepción IPv6. |
| `IFCAP_TXCSUM_IPV6` | Offload de checksum de transmisión IPv6. |
| `IFCAP_TSO4` | Offload de segmentación TCP IPv4. |
| `IFCAP_TSO6` | Offload de segmentación TCP IPv6. |
| `IFCAP_LRO` | Offload de recepción de paquetes grandes. |
| `IFCAP_VLAN_HWTAGGING` | Etiquetado VLAN por hardware. |
| `IFCAP_VLAN_MTU` | VLAN sobre MTU estándar. |
| `IFCAP_JUMBO_MTU` | Jumbo frames admitidos. |
| `IFCAP_POLLING` | Modo polling en lugar de interrupciones. |
| `IFCAP_WOL_MAGIC` | Paquete mágico de Wake-on-LAN. |
| `IFCAP_NETMAP` | Soporte para `netmap(4)`. |
| `IFCAP_TOE` | Motor de offload TCP. |
| `IFCAP_LINKSTATE` | Eventos de estado de enlace por hardware. |

### Contadores `IFCOUNTER_` habituales

| Contador | Significado |
| --- | --- |
| `IFCOUNTER_IPACKETS` | Paquetes recibidos. |
| `IFCOUNTER_IERRORS` | Errores de recepción. |
| `IFCOUNTER_OPACKETS` | Paquetes transmitidos. |
| `IFCOUNTER_OERRORS` | Errores de transmisión. |
| `IFCOUNTER_COLLISIONS` | Colisiones (Ethernet). |
| `IFCOUNTER_IBYTES` | Bytes recibidos. |
| `IFCOUNTER_OBYTES` | Bytes transmitidos. |
| `IFCOUNTER_IMCASTS` | Paquetes multicast recibidos. |
| `IFCOUNTER_OMCASTS` | Paquetes multicast transmitidos. |
| `IFCOUNTER_IQDROPS` | Descartes en la cola de recepción. |
| `IFCOUNTER_OQDROPS` | Descartes en la cola de transmisión. |
| `IFCOUNTER_NOPROTO` | Paquetes para protocolo desconocido. |

### Ioctls de interfaz habituales

| Ioctl | Cuándo se emite | Responsabilidad del driver |
| --- | --- | --- |
| `SIOCSIFFLAGS` | `ifconfig up` / `down` | Activa o desactiva el driver. |
| `SIOCSIFADDR` | `ifconfig inet 1.2.3.4` | Asignación de dirección. Normalmente gestionada por `ether_ioctl`. |
| `SIOCSIFMTU` | `ifconfig mtu N` | Valida y actualiza `if_mtu`. |
| `SIOCADDMULTI` | Grupo multicast unido | Reprograma el filtro de hardware. |
| `SIOCDELMULTI` | Grupo multicast abandonado | Reprograma el filtro de hardware. |
| `SIOCGIFMEDIA` | Visualización de `ifconfig` | Devuelve el medio actual. |
| `SIOCSIFMEDIA` | `ifconfig media X` | Reprograma el PHY o equivalente. |
| `SIOCSIFCAP` | `ifconfig ±offloads` | Activa o desactiva los offloads. |
| `SIOCSIFNAME` | `ifconfig name X` | Renombra la interfaz. |

## Lectura de drivers de red reales

Una de las mejores formas de consolidar tu comprensión es leer drivers reales del árbol de FreeBSD. Esta sección te guía a través de un puñado de drivers que ilustran patrones importantes y sugiere un orden de lectura que se apoya en lo que has aprendido en este capítulo. No necesitas entender cada línea de estos archivos. El objetivo es el reconocimiento: ver los elementos familiares de `ether_ifattach`, `if_transmit`, `if_input`, `ifmedia_init` y demás dentro de drivers de tamaños y propósitos muy distintos.

### Lectura de `/usr/src/sys/net/if_tuntap.c`

Los drivers `tun(4)` y `tap(4)` están implementados juntos en este archivo. Proporcionan al espacio de usuario un descriptor de archivo a través del cual los paquetes pueden fluir hacia dentro y hacia fuera del kernel. La lectura de `if_tuntap.c` te muestra cómo un driver puede conectar el mundo de los dispositivos de caracteres del espacio de usuario del Capítulo 14 con el mundo de la pila de red de este capítulo.

Abre el archivo y busca los siguientes puntos de referencia:

* La declaración `cdevsw` al inicio del archivo, que es como el espacio de usuario abre `/dev/tun0` o `/dev/tap0`.
* La función `tunstart`, que mueve paquetes desde la cola de la interfaz del kernel hacia las lecturas del espacio de usuario.
* La función `tunwrite`, que mueve paquetes desde las escrituras del espacio de usuario hacia el kernel a través de `if_input`.
* La función `tuncreate`, que asigna un ifnet y lo registra.

Verás `ether_ifattach` para `tap` e `if_attach` simple para `tun`, porque los dos tipos difieren en la semántica de la capa de enlace: `tap` es un túnel de aspecto Ethernet, mientras que `tun` es un túnel IP puro sin capa de enlace. Este archivo es un excelente caso de estudio sobre cómo la elección entre `ether_ifattach` y `if_attach` se propaga por el resto del driver.

Observa que `tuntap` no usa un cloner de interfaz del mismo modo que `disc`. Crea interfaces bajo demanda cuando el espacio de usuario abre `/dev/tapN`, lo que muestra otra forma en que las interfaces pueden crearse. Se trata de una variante del patrón de cloner, no de una ruptura con él.

### Lectura de `/usr/src/sys/net/if_bridge.c`

El driver de bridge implementa el puente Ethernet por software entre múltiples interfaces. Es un archivo más extenso (de más de tres mil líneas), pero su núcleo es el mismo: crea un ifnet por cada bridge, recibe tramas de las interfaces miembro a través de hooks `if_input`, busca los destinos en una tabla de direcciones MAC a puerto y reenvía las tramas mediante `if_transmit` por el puerto de salida.

Lo que hace que `if_bridge.c` sea especialmente instructivo es que es a la vez cliente y proveedor de la interfaz ifnet. Es cliente porque transmite tramas a las interfaces miembro. Es proveedor porque expone una interfaz bridge que otro código puede utilizar. Leerlo te muestra cómo escribir un driver que se superpone de forma transparente sobre otros drivers.

### Lectura de `/usr/src/sys/dev/e1000/if_em.c`

El driver `em(4)` es el ejemplo canónico de un driver PCI Ethernet para hardware Intel de la clase e1000. Es considerablemente más grande que nuestro pseudo-driver porque hace todo lo que el hardware real exige: attach PCI, programación de registros, lectura de EEPROM, asignación de MSI-X, gestión de ring buffers, DMA, gestión de interrupciones, y más.

Sin embargo, si miras más allá de las partes específicas del hardware, verás nuestros patrones familiares por todas partes:

* `em_if_attach_pre` reserva un softc.
* `em_if_attach_post` rellena el ifnet.
* `em_if_init` es el callback `if_init`.
* `em_if_ioctl` es el callback `if_ioctl`.
* `em_if_tx` es el callback de transmisión (envuelto a través de iflib).
* `em_if_rx` es el callback de recepción (envuelto a través de iflib).
* `em_if_detach` es la función detach.

El driver usa `iflib(9)` en lugar de llamadas directas a `ifnet`, pero iflib es en sí mismo una capa delgada sobre las mismas APIs que hemos estado usando. Leer `em` es una buena manera de ver cómo un driver real crece a partir de nuestro pequeño ejemplo didáctico.

Céntrate primero en la función de transmisión. Verás gestión de anillos de descriptores, mapeo DMA, manejo de TSO y decisiones de offload de checksum. La cantidad de estado es mayor, pero cada decisión tiene un propósito claro que se corresponde con uno de los conceptos que hemos tratado.

### Lectura de `/usr/src/sys/dev/virtio/network/if_vtnet.c`

El driver `vtnet(4)` es para adaptadores de red VirtIO usados por máquinas virtuales. Es más pequeño que `em`, aunque sigue siendo más grande que nuestro pseudo-driver. Usa `virtio(9)` como transporte en lugar de `bus_space(9)` más ring buffers de DMA, lo que hace que el código sea más fácil de seguir si no estás muy familiarizado con el hardware PCI.

`vtnet` es un segundo driver real especialmente bueno para leer después de `mynet` porque:

* Se usa en prácticamente todas las VMs de FreeBSD.
* Su código fuente es limpio y está bien comentado.
* Demuestra transmisión y recepción multi-cola.
* Muestra cómo los offloads interactúan con el camino de transmisión.

Dedica una tarde a leer el camino de transmisión y el camino de recepción. Probablemente reconocerás de inmediato el 70 u 80 por ciento de los patrones, y el 20 por ciento que no te resulte familiar serán cosas como la gestión de colas VirtIO, que pertenecen al transporte y no al contrato del driver de red.

### Lectura de `/usr/src/sys/net/if_lagg.c`

El driver de agregación de enlaces implementa LACP 802.3ad, round-robin, failover y otros protocolos de bonding. Es en sí mismo un ifnet que agrega sobre ifnets miembro. Leerlo es un ejercicio para ver cómo los drivers de agregación pueden superponerse sobre drivers hoja, y te muestra toda la potencia de la abstracción `ifnet`: una interfaz de bond tiene el mismo aspecto para la pila que una NIC individual.

### Orden de lectura sugerido

Si tienes tiempo para un estudio más profundo, lee en este orden:

1. `if_disc.c`: el pseudo-driver más pequeño. Reconocerás todo.
2. `if_tuntap.c`: pseudo-driver más interfaz de dispositivo de caracteres en userland.
3. `if_epair.c`: pseudo-drivers emparejados con cable simulado.
4. `if_bridge.c`: driver en capa ifnet.
5. `if_vtnet.c`: driver real pequeño para VirtIO.
6. `if_em.c`: driver real completo que usa iflib.
7. `if_lagg.c`: driver de agregación.
8. `if_wg.c`: driver de túnel WireGuard. Moderno, criptográfico, interesante.

Tras esta secuencia habrás visto suficientes drivers como para que casi cualquier driver del árbol resulte legible. Las partes que no te resulten familiares encajarán en "esto es específico del hardware" o "esto es un subsistema que aún no he estudiado", y ambas categorías son finitas y abordables.

### La lectura como hábito

Cultiva el hábito de leer un driver al mes. Elige uno al azar, lee la función attach y da un repaso rápido al camino de transmisión y al de recepción. Te sorprenderá la rapidez con la que crecen tu vocabulario y tu velocidad de lectura. Al cabo de un año reconocerás patrones en drivers que nunca has visto, y el instinto de "dónde debo buscar esta característica" se vuelve más afinado.

La lectura es también la mejor preparación para la escritura. En el momento en que necesites añadir una nueva funcionalidad a un driver que nunca has tocado, la experiencia de haber leído treinta drivers significa que sabes aproximadamente dónde buscar y qué imitar.

## Consideraciones para producción

La mayor parte de este capítulo ha tratado sobre comprensión. Antes de cerrar, un breve apartado sobre qué cambia cuando pasas de un driver didáctico a un driver que vivirá en un entorno de producción.

### Rendimiento

Un driver de producción se mide normalmente en paquetes por segundo, bytes por segundo o latencia en microsegundos. El pseudo-driver que hemos construido en este capítulo no está sometido a presión en ninguna de esas dimensiones. Si intentas llevar `mynet` a una carga de trabajo real, pronto toparás con los límites de un diseño de mutex único, la llamada síncrona a `m_freem` y el despacho de cola única.

Las mejoras habituales incluyen:

* Locks por cola en lugar de un lock a nivel de softc.
* `drbr(9)` para anillos de transmisión por CPU.
* Procesamiento de recepción diferido basado en taskqueue.
* Pools de mbufs preasignados con `m_getcl`.
* Bypass de `if_input` mediante helpers de despacho directo en algunos caminos.
* Hashing de flujo para anclar sockets a CPUs específicas.
* Soporte de netmap para cargas de trabajo de kernel bypass.

Cada una de estas optimizaciones añade código. Un driver de calidad de producción para una NIC de 10 Gbps puede tener entre 3000 y 10000 líneas de C, frente a las 500 líneas de nuestro driver didáctico.

### Fiabilidad

Se espera que un driver de producción aguante meses de operación continua sin fugas de memoria, sin hacer caer el kernel y sin que sus contadores deriven. Las prácticas que hacen esto posible incluyen:

* Ejecutar el kernel con `INVARIANTS` y `WITNESS` en QA, de modo que las aserciones detecten errores a tiempo.
* Escribir pruebas de regresión que ejerciten cada camino del ciclo de vida.
* Someter el driver a pruebas de estrés (como `iperf3`, pktgen o netmap pkt-gen) durante períodos prolongados.
* Instrumentar el driver con contadores para cada camino de error, de modo que los operadores puedan diagnosticar problemas en producción.
* Proporcionar diagnósticos claros a través de `dmesg`, sysctl y sondas SDT.

Estas prácticas no son opcionales para un driver que se desplegará a escala. Son el precio de entrada.

### Observabilidad

Un driver de producción bien escrito expone suficiente estado a través de sysctl, contadores y sondas DTrace como para que un operador pueda diagnosticar la mayoría de los problemas sin añadir printfs ni reconstruir el kernel. La regla general es que cada camino de código significativo debería tener un contador o un punto de sonda, y cada decisión que dependa del estado en tiempo de ejecución debería poder consultarse sin reconstruir el kernel.

En `mynet` solo tenemos los contadores ifnet incorporados. Una versión de producción añadiría contadores por driver para cosas como entradas al camino de transmisión, descartes en el camino de recepción e invocaciones del manejador de interrupciones. Estos contadores son baratos de incrementar y no tienen precio cuando aparece un problema.

### Compatibilidad hacia atrás

Un driver que se incluye en una versión debe funcionar también en versiones futuras, idealmente sin modificaciones. El kernel de FreeBSD evoluciona sus APIs internas con el tiempo, y los drivers que acceden demasiado profundamente a las estructuras pueden romperse cuando esas estructuras cambian.

La API de accesores que introdujimos en la sección 2 es una de las defensas. Usar `if_setflagbits` en lugar de `ifp->if_flags |= flag` te aísla de los cambios en la disposición de las estructuras. Del mismo modo, `if_inc_counter` en lugar de actualizaciones directas de contadores te aísla de cambios en la representación de los contadores.

Para drivers de producción, prefiere el estilo de accesores siempre que esté disponible.

### Licencias e integración en el árbol principal

Un driver que pretendas integrar en el árbol principal debe estar licenciado de forma compatible con el árbol de FreeBSD, que habitualmente usa una licencia BSD de dos cláusulas. También debería seguir KNF (`style(9)`), incluir páginas de manual en `share/man/man4`, incluir un Makefile del módulo en `sys/modules` y enviarse a través del proceso de contribución de FreeBSD (revisiones en Phabricator en el momento de escribir estas líneas).

Los drivers didácticos como `mynet` no necesitan preocuparse por la integración en el árbol principal, pero si escribes un driver con la intención de distribuirlo a otros, estas son las consideraciones adicionales que convierten tu código C en un artefacto comunitario.

## Cerrando

Tómate un momento para apreciar lo que acabas de hacer. Has:

* Construido tu primer driver de red, desde cero.
* Registrado el driver con la pila de red a través de `ifnet` y `ether_ifattach`.
* Implementado un camino de transmisión que acepta mbufs, engancha BPF, actualiza contadores y libera recursos.
* Implementado un camino de recepción que construye mbufs, los entrega a BPF y los lleva a la pila.
* Gestionado flags de interfaz, transiciones de estado de enlace y descriptores de medios.
* Probado el driver con `ifconfig`, `netstat`, `tcpdump`, `ping` y `route monitor`.
* Limpiado todo al destruir la interfaz y al descargar el módulo, sin fugas.

Más importante que cualquiera de estos logros individuales, has interiorizado un modelo mental. Un driver de red es un participante en un contrato con la pila de red del kernel. El contrato tiene una forma fija: unos pocos callbacks hacia abajo, una llamada hacia arriba, un puñado de flags, algunos contadores, un descriptor de medios y un estado de enlace. Una vez que puedes ver esa forma con claridad, cualquier driver de red del árbol de FreeBSD se vuelve comprensible. Los drivers de producción son más grandes, pero no son fundamentalmente diferentes.

### Lo que este capítulo no ha cubierto

Algunos temas están al alcance, pero se han aplazado deliberadamente para mantener este capítulo manejable.

**iflib(9).** El framework moderno de driver de NIC que usan la mayoría de los drivers de producción en FreeBSD 14. iflib comparte ring buffers de transmisión y recepción entre muchos drivers y proporciona un modelo orientado a callbacks más sencillo para NICs de hardware. Los patrones que hemos escrito a mano en este capítulo son exactamente lo que iflib automatiza, de modo que todo lo que has aprendido aquí sigue siendo válido. Veremos iflib en capítulos posteriores cuando estudiemos drivers de hardware específicos.

**DMA para recepción y transmisión.** Las NICs reales mueven datos de paquetes a través de ring buffers mapeados por DMA. La API `bus_dma(9)` que introdujimos en capítulos anteriores es cómo se hace eso. Añadir DMA a un driver transforma la historia de construcción de mbufs en "mapear el mbuf, pasar la dirección mapeada al hardware, esperar la interrupción de finalización, desmapear". Es una cantidad considerable de código adicional, y merece su propio tratamiento en un capítulo posterior.

**MSI-X y moderación de interrupciones.** Las NICs modernas tienen múltiples vectores de interrupción y admiten la coalescencia de interrupciones. Usamos un callout porque somos un pseudo-driver. Los drivers reales usan manejadores de interrupciones. La moderación de interrupciones (dejar que el hardware agregue varios eventos de finalización en una sola interrupción) es crítica para el rendimiento.

**netmap(4).** El camino rápido de kernel bypass usado por algunas aplicaciones de alto rendimiento. Los drivers se adhieren llamando a `netmap_attach()` y exponiendo ring buffers por cola. Es una especialización para casos de uso sensibles al rendimiento.

**polling(4).** Una técnica más antigua en la que el driver es sondeado para obtener paquetes por un thread del kernel en lugar de ser manejado por interrupciones. Sigue disponible, pero se usa con menos frecuencia que antes.

**VNET en detalle.** Configuramos el driver para que sea compatible con VNET, pero no exploramos qué significa mover interfaces entre VNETs con `if_vmove`, ni cómo se ve un teardown de VNET desde la perspectiva del driver. El capítulo 30 abordará ese territorio.

**Offloads de hardware.** Offload de checksum, TSO, LRO, etiquetado VLAN, offload de cifrado. Todas estas son capacidades que una NIC real podría exponer. Un driver que las anuncia tiene que cumplirlas, y eso lleva a un rico espacio de diseño que no hemos abordado.

**Wireless.** Los drivers `wlan(4)` son radicalmente diferentes de los drivers Ethernet porque tratan con formatos de trama 802.11, escaneo, autenticación y tramas de gestión. El `ifnet` sigue presente, pero se asienta sobre una capa de enlace muy diferente. Visitaremos los drivers wireless en un capítulo posterior.

**Grafo de red (`netgraph(4)`).** El framework de filtrado y clasificación de paquetes de FreeBSD. Es en gran medida ortogonal a la escritura de drivers, pero vale la pena conocerlo para arquitecturas de red avanzadas.

**Interfaces de bridging y VLAN.** Interfaces virtuales que agregan o modifican el tráfico. Se construyen sobre `ifnet`, exactamente igual que nuestro driver, aunque su papel es bastante diferente.

Cada uno de estos temas merece su propio capítulo. Lo que has construido aquí es el campamento base estable desde el que parten todas esas expediciones.

### Reflexión final

Los drivers de red tienen fama de ser una de las áreas más exigentes de la ingeniería del kernel. Con razón: las restricciones son estrictas, las interacciones con la pila son numerosas, las expectativas de rendimiento son altas y los comandos visibles para el usuario son muchos. Pero la estructura de un driver de red es clara una vez que eres capaz de verla. Eso es lo que este capítulo te ha dado: la capacidad de ver la estructura.

Lee `if_em.c`, `if_bge.c` o `if_tuntap.c` ahora. Reconocerás el esqueleto. El softc. La llamada a `ether_ifattach`. El `if_transmit`. El switch de `if_ioctl`. El `if_input` en el manejador de recepción. El `bpfattach` y el `BPF_MTAP`. Allí donde el código añade complejidad, lo está haciendo sobre un esqueleto que ya has construido en miniatura.

Al igual que el capítulo 27, este capítulo es extenso porque el tema está compuesto de capas. Hemos intentado que cada capa se asiente con suavidad antes de que llegue la siguiente. Si alguna sección no ha quedado clara, vuelve atrás y repite el laboratorio correspondiente. El aprendizaje del kernel es fuertemente acumulativo. Una segunda lectura de una sección a menudo aporta más que una primera lectura de la siguiente.

### Lecturas adicionales

**Páginas de manual.** `ifnet(9)`, `ifmedia(9)`, `mbuf(9)`, `ether(9)`, `bpf(9)`, `polling(9)`, `ifconfig(8)`, `netstat(1)`, `tcpdump(1)`, `route(8)`, `ngctl(8)`. Léelas en ese orden.

**The FreeBSD Architecture Handbook.** Los capítulos sobre redes son un buen complemento.

**Kirk McKusick et al., "The Design and Implementation of the FreeBSD Operating System".** Los capítulos sobre la pila de red son especialmente relevantes.

**"TCP/IP Illustrated, Volume 2", de Wright y Stevens.** Un recorrido clásico por una pila de red derivada de BSD. Algo anticuado, pero único en su profundidad.

**El árbol de código fuente de FreeBSD.** `/usr/src/sys/net/`, `/usr/src/sys/netinet/`, `/usr/src/sys/dev/e1000/`, `/usr/src/sys/dev/bge/`, `/usr/src/sys/dev/mlx5/`. Todos los patrones tratados en este capítulo tienen su base en ese código.

**Los archivos de las listas de correo.** `freebsd-net@` es la lista más relevante. Leer hilos históricos es una excelente forma de asimilar modismos que nunca llegaron a la documentación oficial.

**El historial de commits en los espejos de GitHub.** El repositorio de FreeBSD cuenta con un historial excelente. `git log --follow sys/net/if_var.h` es un buen punto de partida para ver cómo evolucionó la abstracción ifnet.

**Las diapositivas del FreeBSD Developer Summit.** Cuando están disponibles, suelen incluir sesiones centradas en redes.

**Otros BSDs.** NetBSD y OpenBSD tienen marcos de driver de red ligeramente diferentes, pero las ideas centrales son idénticas. Leer un driver en otro BSD después de leer su equivalente en FreeBSD es una buena forma de entender qué es universal y qué es específico de FreeBSD.

## Guía de referencia de los subsistemas ifnet relacionados

Has construido un driver. Has leído un puñado de drivers reales. Antes de cerrar el capítulo, hagamos un repaso de los subsistemas circundantes para que sepas dónde buscar cuando los necesites.

### `arp(4)` y el descubrimiento de vecinos

ARP para IPv4 reside en `/usr/src/sys/netinet/if_ether.c`. Es el subsistema que traduce direcciones IP a direcciones MAC. Los drivers normalmente no interactúan con ARP directamente; transportan paquetes (incluidas las peticiones y respuestas ARP) a través de sus rutas de transmisión y recepción, y el código ARP dentro de `ether_input` y `arpresolve` hace el resto.

El equivalente para IPv6 es el descubrimiento de vecinos, en `/usr/src/sys/netinet6/nd6.c`. Utiliza ICMPv6 en lugar de un protocolo separado, pero el papel es el mismo: traducir direcciones IPv6 a direcciones MAC para la entrega en el enlace local.

### `bpf(4)`

El subsistema Berkeley Packet Filter reside en `/usr/src/sys/net/bpf.c`. BPF es el mecanismo visible desde el espacio de usuario para la captura de paquetes. `tcpdump(1)`, `libpcap(3)` y muchas otras herramientas utilizan BPF. Los drivers se registran en BPF a través de `bpfattach` (lo hace automáticamente `ether_ifattach`) y desvían paquetes a BPF mediante `BPF_MTAP` (que tú haces manualmente).

Los filtros BPF son programas escritos en el pseudolenguaje de máquina BPF, compilados a bytecode en el espacio de usuario y ejecutados en el kernel. Son lo que permite que `tcpdump 'port 80'` funcione de manera eficiente: el filtro se ejecuta antes de que el paquete se copie al espacio de usuario, de modo que solo se transfieren los paquetes que coinciden.

### `route(4)`

El subsistema de enrutamiento reside en `/usr/src/sys/net/route.c` y ha crecido con el tiempo (la reciente abstracción de siguiente salto `nhop(9)` es un cambio notable). Los drivers interactúan con el enrutamiento de forma indirecta: cuando notifican cambios de estado del enlace, el subsistema de enrutamiento actualiza las métricas; cuando transmiten, la pila ya ha realizado la búsqueda de ruta. `route monitor`, que utilizamos en un laboratorio, se suscribe a los eventos de enrutamiento y los muestra.

### `if_clone(4)`

El subsistema de clonado en `/usr/src/sys/net/if_clone.c` es el que hemos estado usando a lo largo de este capítulo. Gestiona la lista de clonadores por driver y despacha las peticiones `ifconfig create` e `ifconfig destroy` al driver correcto.

### `pf(4)`

El filtro de paquetes reside en `/usr/src/sys/netpfil/pf/`. Es independiente de cualquier driver específico y funciona como un gancho en los caminos de paquetes a través de `pfil(9)`. Los drivers normalmente no interactúan con `pf` directamente; el tráfico que pasa por la pila se filtra de forma transparente.

### `netmap(4)`

`netmap(4)` es un framework de I/O de paquetes que evita el kernel, ubicado en `/usr/src/sys/dev/netmap/`. Los drivers que soportan netmap exponen sus buffers de anillo directamente al espacio de usuario, sin pasar por las rutas normales de `if_input` e `if_transmit`. Esto permite a las aplicaciones recibir y transmitir paquetes a la velocidad de línea sin intervención del kernel. Solo un puñado de drivers admiten netmap de forma nativa; el resto utiliza una capa de emulación que imita la semántica de netmap a costa de algo de rendimiento.

### `netgraph(4)`

`netgraph(4)` es el framework modular de procesamiento de paquetes de FreeBSD, ubicado en `/usr/src/sys/netgraph/`. Te permite construir grafos arbitrarios de nodos de procesamiento de paquetes en el kernel, configurados desde el espacio de usuario mediante `ngctl`. Los drivers pueden exponerse como nodos de netgraph (véase `ng_ether.c`), y netgraph puede utilizarse para implementar túneles, PPP sobre Ethernet, enlaces cifrados y muchas otras funcionalidades sin modificar la propia pila.

### `iflib(9)`

`iflib(9)` es el framework moderno para drivers Ethernet de alto rendimiento, ubicado en `/usr/src/sys/net/iflib.c`. Se encarga de las partes más mecánicas de un driver de NIC (gestión de buffers de anillo, manejo de interrupciones, fragmentación TSO, agregación LRO) y deja al autor del driver la tarea de proporcionar las callbacks específicas del hardware. En los drivers que se han convertido a iflib hasta la fecha, el código del driver suele reducirse entre un 30 y un 50 por ciento en comparación con la implementación equivalente en ifnet puro. Consulta el Apéndice F para ver una comparación reproducible del número de líneas entre los corpus de drivers iflib y no iflib. La sección iflib del Apéndice F también incluye una medición concreta por driver sobre el commit de conversión de ixgbe, situada aproximadamente en el límite inferior de ese rango.

Por ahora, `iflib` queda fuera del alcance de este capítulo. La sección iflib del Apéndice F ofrece una comparación reproducible del número de líneas que muestra cuánto código ahorra el framework en los drivers que se han convertido a él.

### Resumen del panorama

Un driver de red vive en un entorno rico. Por encima de él están ARP, IP, TCP, UDP y la capa de sockets. A su alrededor están BPF, `pf`, netmap y netgraph. Por debajo hay hardware, o una simulación de transporte, o un canal hacia el espacio de usuario. Cada uno de estos componentes tiene sus propias convenciones, y aprender cualquiera de ellos en profundidad es una inversión valiosa. Lo que este capítulo te ha dado es suficiente familiaridad con el objeto central, el `ifnet`, como para acercarte a cualquiera de estos subsistemas sin sentirte intimidado.

## Escenarios de depuración: un ejemplo práctico

Una de las mejores formas de cerrar un capítulo sobre escritura de drivers es recorrer una sesión de depuración concreta. El escenario que se presenta a continuación es compuesto: combina síntomas y correcciones de varios bugs de driver diferentes en una sola narrativa, para que el arco completo de «algo va mal, vamos a encontrarlo» sea visible.

### El problema

Cargas `mynet`, creas una interfaz, asignas una IP y ejecutas `ping`. El ping informa de un 100% de pérdida, como era de esperar (nuestro pseudodriver no tiene nada al otro extremo). Pero `netstat -in -I mynet0` muestra `Opkts 0` incluso después de varios pings. Algo en la ruta de transmisión está roto.

### Primera hipótesis: la función de transmisión no está siendo invocada

Ejecutas `dtrace -n 'fbt::mynet_transmit:entry { printf("called"); }'`. Sin salida, ni siquiera durante el ping. Eso confirma que `if_transmit` no está siendo invocado.

### Investigando la causa

Abres el código fuente y compruebas que `ifp->if_transmit = mynet_transmit;` está presente. Verificas `ifp->if_transmit` en tiempo de ejecución obteniendo el puntero ifnet a través de los informes de `ifconfig` (no hay una forma directa de leer punteros a funciones desde el espacio de usuario, pero una sonda DTrace sí puede hacerlo):

```console
# dtrace -n 'fbt::ether_output_frame:entry {
    printf("if_transmit = %p", args[0]->if_transmit);
}'
```

La salida muestra una dirección diferente a la esperada. Una inspección más detallada revela que `ether_ifattach` sobreescribió `if_transmit` con su propio wrapper. Buscas `if_transmit` en `if_ethersubr.c` con grep y confirmas que `ether_ifattach` establece `ifp->if_output = ether_output` pero no toca `if_transmit`. Por lo tanto, `if_transmit` debería seguir siendo tu función.

Vuelves a tu código fuente y te das cuenta de que estableciste `ifp->if_transmit = mynet_transmit;` antes de `ether_ifattach`, pero sin querer lo estableciste también a través del campo heredado `if_start` en una segunda asignación que olvidaste eliminar. El mecanismo heredado `if_start` tiene precedencia bajo ciertas condiciones, y el kernel acaba llamando a `if_start` en lugar de a `if_transmit`.

Eliminas la asignación residual de `if_start` y recompilas. La función de transmisión ahora es invocada.

### Segundo problema: discrepancia en los contadores

La transmisión ya es invocada y `Opkts` sube. Pero `Obytes` es sospechosamente bajo: se incrementa en uno por cada ping, no en la longitud en bytes del ping. Vuelves a inspeccionar el código de actualización de contadores:

```c
if_inc_counter(ifp, IFCOUNTER_OBYTES, 1);
```

La constante `1` debería ser `len`. Escribiste el argumento incorrecto. Lo cambias a `if_inc_counter(ifp, IFCOUNTER_OBYTES, len)` y recompilas. `Obytes` ahora sube en la cantidad esperada.

### Tercer problema: la ruta de recepción parece intermitente

Los ARPs sintetizados aparecen la mayor parte del tiempo, pero ocasionalmente se detienen durante varios segundos. Añades una sonda DTrace a `mynet_rx_timer` y ves que la función es llamada a intervalos regulares, pero que algunas llamadas retornan anticipadamente sin generar una trama.

Inspeccionas `mynet_rx_fake_arp` y descubres que usa `M_NOWAIT` para la asignación de mbuf. Bajo presión de memoria, `M_NOWAIT` devuelve NULL, y la ruta de recepción descarta el paquete en silencio. Instrumentas el camino de fallo de la asignación:

```c
if (m == NULL) {
    if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
    return;
}
```

Y compruebas el contador: coincide con las tramas que faltan. Has encontrado la causa: presión transitoria de mbuf en tu VM de prueba. La corrección consiste en aceptar las pérdidas ocasionales (son legítimas y están correctamente contabilizadas) o cambiar a `M_WAITOK` si el callout puede tolerar el bloqueo (no puede, porque los callouts se ejecutan en un contexto que no admite suspensión).

En este caso, aceptar las pérdidas es lo correcto. La corrección consiste entonces en hacer el comportamiento visible en un panel de monitorización: añades un sysctl que expone `IFCOUNTER_IQDROPS` en esta interfaz específica y lo documentas en la documentación del driver.

### Qué nos enseña este escenario

Tres bugs separados. Ninguno fue catastrófico. Cada uno requirió una combinación diferente de herramientas para diagnosticarlo: DTrace para el rastreo de funciones, lectura de código para entender la API, y contadores para observar el efecto en tiempo de ejecución.

La lección es que los bugs de los drivers tienden a esconderse a plena vista. La primera regla de la depuración de drivers es «no confíes, verifica». La segunda regla es «los contadores y las herramientas te lo dirán». La tercera regla es «si los contadores no te dicen lo que necesitas, añade más contadores o más sondas».

Con la práctica, las sesiones de depuración como esta se vuelven más rápidas. Desarrollas un instinto para saber qué herramienta elegir primero, y la diferencia entre un driver que funciona a la primera carga y uno que requirió seis iteraciones se convierte en un ciclo de depuración más corto.

## Una nota sobre la disciplina de pruebas

Antes de cerrar definitivamente el capítulo, dedicaremos unos párrafos a la disciplina de pruebas. Un driver didáctico puede probarse de forma informal. Un driver que pretendes mantener a lo largo del tiempo merece un enfoque más riguroso.

### Pensamiento a nivel de unidad

Cada callback de tu driver tiene un contrato pequeño y bien definido. `mynet_transmit` recibe un ifnet y un mbuf, valida, cuenta, captura con BPF y libera. `mynet_ioctl` recibe un ifnet y un código de ioctl, despacha y devuelve un errno. Cada uno de ellos puede ejercitarse de forma independiente.

En la práctica, las pruebas unitarias del código del kernel son difíciles porque el kernel no es fácil de incrustar en un arnés de pruebas en espacio de usuario. Pero puedes aproximarte a esa disciplina diseñando el código de modo que la mayor parte de cada callback sea pura: dados unos inputs, producir outputs sin tocar el estado global. El bloque de validación de `mynet_transmit` es un buen ejemplo: no toca nada salvo `ifp->if_mtu` y variables locales.

Un modelo mental del tipo "este callback tiene un contrato; estos son los casos que ejercitan el contrato; estos son los comportamientos esperados para cada caso" es la base de unas buenas pruebas.

### Pruebas de ciclo de vida

Todo driver debería probarse a lo largo de su ciclo de vida completo: carga, creación, configuración, tráfico, parada, destrucción y descarga. El script del Laboratorio 6 es una versión mínima de ese tipo de prueba. Una versión más rigurosa incluiría:

* Múltiples interfaces creadas de forma concurrente.
* Descarga mientras fluye tráfico (a baja velocidad, por seguridad).
* Ciclos repetidos de carga/descarga para detectar fugas de memoria.
* Pruebas con INVARIANTS y WITNESS activados.

### Pruebas en los caminos de error

Cada camino de error del driver debe poder ejercitarse. Si `if_alloc` falla, ¿la función de creación deshace los cambios correctamente? Si un ioctl devuelve un error, ¿lo gestiona bien el llamador? Si el callout no puede asignar un mbuf, ¿el driver mantiene la coherencia?

Una técnica útil es la inyección de fallos: añadir un sysctl que falle de forma probabilística operaciones concretas (`if_alloc`, `m_gethdr`, etc.) y ejecutar las pruebas de ciclo de vida con la inyección de fallos activada. Esto expone caminos de error que casi nunca se disparan en producción, pero que aun así pueden ocurrir bajo carga.

### Pruebas de regresión

Cada vez que corrijas un bug, añade una prueba que lo hubiera detectado. Incluso un script de shell sencillo que cargue el driver, ejercite una función concreta y compruebe un contador es una prueba de regresión.

Con el tiempo, una suite de pruebas de regresión se convierte en un muro de contención contra la reintroducción de bugs. Además, documenta el comportamiento que garantizas. Un nuevo colaborador que lea la suite de pruebas obtendrá una imagen más clara de lo que el driver promete que la que podría obtener leyendo únicamente el código.

### Vigilando los problemas latentes

Algunos problemas solo se manifiestan tras horas o días de operación: fugas de memoria lentas, desviación de contadores, condiciones de carrera poco frecuentes. Las pruebas de larga duración son la única forma de encontrarlos. Un driver desplegado en producción sin al menos 24 horas de funcionamiento continuo bajo una carga representativa no está listo.

Para `mynet`, la prueba podría ser tan sencilla como "dejar el driver cargado un día entero y comprobar `vmstat -m` y `vmstat -z` al final". Para un driver real, podría implicar terabytes-hora de tráfico bajo una carga de trabajo real. La escala difiere; el principio es el mismo.

## Un recorrido completo por `mynet.c`

Antes de cerrar el capítulo, merece la pena mostrar un recorrido conciso de extremo a extremo del driver de referencia. El objetivo es ver el driver completo de una vez, con anotaciones breves en cada paso, para poder visualizar la forma completa sin tener que saltar entre las Secciones 3 y 6.

### Preámbulo a nivel de archivo

El driver comienza con una cabecera de licencia, el aviso de copyright y el bloque de includes que describimos en la Sección 3. Tras los includes, el archivo declara el tipo de memoria, la variable del cloner y la estructura softc:

```c
static const char mynet_name[] = "mynet";
static MALLOC_DEFINE(M_MYNET, "mynet", "mynet pseudo Ethernet driver");

VNET_DEFINE_STATIC(struct if_clone *, mynet_cloner);
#define V_mynet_cloner  VNET(mynet_cloner)

struct mynet_softc {
    struct ifnet    *ifp;
    struct mtx       mtx;
    uint8_t          hwaddr[ETHER_ADDR_LEN];
    struct ifmedia   media;
    struct callout   rx_callout;
    int              rx_interval_hz;
    bool             running;
};

#define MYNET_LOCK(sc)      mtx_lock(&(sc)->mtx)
#define MYNET_UNLOCK(sc)    mtx_unlock(&(sc)->mtx)
#define MYNET_ASSERT(sc)    mtx_assert(&(sc)->mtx, MA_OWNED)
```

Cada campo y macro aquí tiene un propósito que tratamos anteriormente. El softc almacena el estado por instancia; las macros de locking documentan cuándo debe estar retenido el mutex; el cloner consciente de VNET es el mecanismo por el que `ifconfig mynet create` produce nuevas interfaces.

### Declaraciones previas

Un pequeño bloque de declaraciones previas para las funciones estáticas que el driver expone como callbacks:

```c
static int      mynet_clone_create(struct if_clone *, int, caddr_t);
static void     mynet_clone_destroy(struct ifnet *);
static int      mynet_create_unit(int unit);
static void     mynet_destroy(struct mynet_softc *);
static void     mynet_init(void *);
static void     mynet_stop(struct mynet_softc *);
static int      mynet_transmit(struct ifnet *, struct mbuf *);
static void     mynet_qflush(struct ifnet *);
static int      mynet_ioctl(struct ifnet *, u_long, caddr_t);
static int      mynet_media_change(struct ifnet *);
static void     mynet_media_status(struct ifnet *, struct ifmediareq *);
static void     mynet_rx_timer(void *);
static void     mynet_rx_fake_arp(struct mynet_softc *);
static int      mynet_modevent(module_t, int, void *);
static void     vnet_mynet_init(const void *);
static void     vnet_mynet_uninit(const void *);
```

Las declaraciones previas son una cortesía para el lector. Permiten escanear la parte superior del archivo y ver cada función con nombre que el driver exporta, sin necesidad de buscar las definiciones.

### Despacho del cloner

Las funciones de creación y destrucción del cloner son envoltorios ligeros que delegan el trabajo real en los helpers por unidad:

```c
static int
mynet_clone_create(struct if_clone *ifc __unused, int unit, caddr_t params __unused)
{
    return (mynet_create_unit(unit));
}

static void
mynet_clone_destroy(struct ifnet *ifp)
{
    mynet_destroy((struct mynet_softc *)ifp->if_softc);
}
```

Mantener pequeños los callbacks del cloner es una convención que vale la pena seguir. Facilita probar las funciones de trabajo real (`mynet_create_unit`, `mynet_destroy`) de forma aislada y convierte el código de pegamento del cloner en algo aburrido.

### Creación por unidad

La función de creación por unidad es donde ocurre la configuración real:

```c
static int
mynet_create_unit(int unit)
{
    struct mynet_softc *sc;
    struct ifnet *ifp;

    sc = malloc(sizeof(*sc), M_MYNET, M_WAITOK | M_ZERO);
    ifp = if_alloc(IFT_ETHER);
    if (ifp == NULL) {
        free(sc, M_MYNET);
        return (ENOSPC);
    }
    sc->ifp = ifp;
    mtx_init(&sc->mtx, "mynet", NULL, MTX_DEF);

    arc4rand(sc->hwaddr, ETHER_ADDR_LEN, 0);
    sc->hwaddr[0] = 0x02;  /* locally administered, unicast */

    if_initname(ifp, mynet_name, unit);
    ifp->if_softc = sc;
    ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
    ifp->if_capabilities = IFCAP_VLAN_MTU;
    ifp->if_capenable = IFCAP_VLAN_MTU;
    ifp->if_transmit = mynet_transmit;
    ifp->if_qflush = mynet_qflush;
    ifp->if_ioctl = mynet_ioctl;
    ifp->if_init = mynet_init;
    ifp->if_baudrate = IF_Gbps(1);

    ifmedia_init(&sc->media, 0, mynet_media_change, mynet_media_status);
    ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_T | IFM_FDX, 0, NULL);
    ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
    ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);

    callout_init_mtx(&sc->rx_callout, &sc->mtx, 0);
    sc->rx_interval_hz = hz;

    ether_ifattach(ifp, sc->hwaddr);
    return (0);
}
```

Puedes ver todos los conceptos de la Sección 3 reunidos en un solo lugar: asignación del softc y del ifnet, fabricación de la dirección MAC, configuración de campos, configuración de medios, inicialización del callout y el `ether_ifattach` final que registra la interfaz en el stack de red.

### Destrucción

La destrucción es el espejo de la creación, en orden inverso:

```c
static void
mynet_destroy(struct mynet_softc *sc)
{
    struct ifnet *ifp = sc->ifp;

    MYNET_LOCK(sc);
    sc->running = false;
    ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
    MYNET_UNLOCK(sc);

    callout_drain(&sc->rx_callout);

    ether_ifdetach(ifp);
    if_free(ifp);

    ifmedia_removeall(&sc->media);
    mtx_destroy(&sc->mtx);
    free(sc, M_MYNET);
}
```

De nuevo, cada paso es uno que tratamos. El orden es: detener, desconectar, liberar.

### Init y Stop

Las transiciones entre "no en ejecución" y "en ejecución" las gestionan dos funciones pequeñas:

```c
static void
mynet_init(void *arg)
{
    struct mynet_softc *sc = arg;

    MYNET_LOCK(sc);
    sc->running = true;
    sc->ifp->if_drv_flags |= IFF_DRV_RUNNING;
    sc->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
    callout_reset(&sc->rx_callout, sc->rx_interval_hz,
        mynet_rx_timer, sc);
    MYNET_UNLOCK(sc);

    if_link_state_change(sc->ifp, LINK_STATE_UP);
}

static void
mynet_stop(struct mynet_softc *sc)
{
    MYNET_LOCK(sc);
    sc->running = false;
    sc->ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
    callout_stop(&sc->rx_callout);
    MYNET_UNLOCK(sc);

    if_link_state_change(sc->ifp, LINK_STATE_DOWN);
}
```

Ambas son simétricas, ambas respetan la regla de soltar el lock antes de llamar a `if_link_state_change`, y ambas mantienen las reglas de coherencia que describimos en la Sección 6.

### El camino de datos

La transmisión y la recepción simulada son el corazón del driver:

```c
static int
mynet_transmit(struct ifnet *ifp, struct mbuf *m)
{
    struct mynet_softc *sc = ifp->if_softc;
    int len;

    if (m == NULL)
        return (0);
    M_ASSERTPKTHDR(m);

    if (m->m_pkthdr.len > (ifp->if_mtu + sizeof(struct ether_vlan_header))) {
        m_freem(m);
        if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
        return (E2BIG);
    }

    if ((ifp->if_flags & IFF_UP) == 0 ||
        (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
        m_freem(m);
        if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
        return (ENETDOWN);
    }

    BPF_MTAP(ifp, m);

    len = m->m_pkthdr.len;
    if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
    if_inc_counter(ifp, IFCOUNTER_OBYTES, len);
    if (m->m_flags & (M_BCAST | M_MCAST))
        if_inc_counter(ifp, IFCOUNTER_OMCASTS, 1);

    m_freem(m);
    return (0);
}

static void
mynet_qflush(struct ifnet *ifp __unused)
{
}

static void
mynet_rx_timer(void *arg)
{
    struct mynet_softc *sc = arg;

    MYNET_ASSERT(sc);
    if (!sc->running)
        return;
    callout_reset(&sc->rx_callout, sc->rx_interval_hz,
        mynet_rx_timer, sc);
    MYNET_UNLOCK(sc);

    mynet_rx_fake_arp(sc);

    MYNET_LOCK(sc);
}
```

El helper de ARP falso construye la trama sintética y la entrega al stack:

```c
static void
mynet_rx_fake_arp(struct mynet_softc *sc)
{
    struct ifnet *ifp = sc->ifp;
    struct mbuf *m;
    struct ether_header *eh;
    struct arphdr *ah;
    uint8_t *payload;
    size_t frame_len;

    frame_len = sizeof(*eh) + sizeof(*ah) + 2 * (ETHER_ADDR_LEN + 4);
    MGETHDR(m, M_NOWAIT, MT_DATA);
    if (m == NULL) {
        if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
        return;
    }
    m->m_pkthdr.len = m->m_len = frame_len;
    m->m_pkthdr.rcvif = ifp;

    eh = mtod(m, struct ether_header *);
    memset(eh->ether_dhost, 0xff, ETHER_ADDR_LEN);
    memcpy(eh->ether_shost, sc->hwaddr, ETHER_ADDR_LEN);
    eh->ether_type = htons(ETHERTYPE_ARP);

    ah = (struct arphdr *)(eh + 1);
    ah->ar_hrd = htons(ARPHRD_ETHER);
    ah->ar_pro = htons(ETHERTYPE_IP);
    ah->ar_hln = ETHER_ADDR_LEN;
    ah->ar_pln = 4;
    ah->ar_op  = htons(ARPOP_REQUEST);

    payload = (uint8_t *)(ah + 1);
    memcpy(payload, sc->hwaddr, ETHER_ADDR_LEN);
    payload += ETHER_ADDR_LEN;
    memset(payload, 0, 4);
    payload += 4;
    memset(payload, 0, ETHER_ADDR_LEN);
    payload += ETHER_ADDR_LEN;
    memcpy(payload, "\xc0\x00\x02\x63", 4);

    BPF_MTAP(ifp, m);
    if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
    if_inc_counter(ifp, IFCOUNTER_IBYTES, frame_len);
    if_inc_counter(ifp, IFCOUNTER_IMCASTS, 1);  /* broadcast counts as multicast */

    if_input(ifp, m);
}
```

### Callbacks de ioctl y de medios

El manejador de ioctl y los dos callbacks de medios:

```c
static int
mynet_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
    struct mynet_softc *sc = ifp->if_softc;
    struct ifreq *ifr = (struct ifreq *)data;
    int error = 0;

    switch (cmd) {
    case SIOCSIFFLAGS:
        MYNET_LOCK(sc);
        if (ifp->if_flags & IFF_UP) {
            if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
                MYNET_UNLOCK(sc);
                mynet_init(sc);
                MYNET_LOCK(sc);
            }
        } else {
            if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
                MYNET_UNLOCK(sc);
                mynet_stop(sc);
                MYNET_LOCK(sc);
            }
        }
        MYNET_UNLOCK(sc);
        break;

    case SIOCSIFMTU:
        if (ifr->ifr_mtu < 68 || ifr->ifr_mtu > 9216) {
            error = EINVAL;
            break;
        }
        ifp->if_mtu = ifr->ifr_mtu;
        break;

    case SIOCADDMULTI:
    case SIOCDELMULTI:
        break;

    case SIOCGIFMEDIA:
    case SIOCSIFMEDIA:
        error = ifmedia_ioctl(ifp, ifr, &sc->media, cmd);
        break;

    default:
        error = ether_ioctl(ifp, cmd, data);
        break;
    }

    return (error);
}

static int
mynet_media_change(struct ifnet *ifp __unused)
{
    return (0);
}

static void
mynet_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
    struct mynet_softc *sc = ifp->if_softc;

    imr->ifm_status = IFM_AVALID;
    if (sc->running)
        imr->ifm_status |= IFM_ACTIVE;
    imr->ifm_active = IFM_ETHER | IFM_1000_T | IFM_FDX;
}
```

### Pegamento del módulo y registro del cloner

La parte inferior del archivo contiene el manejador del módulo, las funciones VNET sysinit/sysuninit y las declaraciones del módulo:

```c
static void
vnet_mynet_init(const void *unused __unused)
{
    V_mynet_cloner = if_clone_simple(mynet_name, mynet_clone_create,
        mynet_clone_destroy, 0);
}
VNET_SYSINIT(vnet_mynet_init, SI_SUB_PSEUDO, SI_ORDER_ANY,
    vnet_mynet_init, NULL);

static void
vnet_mynet_uninit(const void *unused __unused)
{
    if_clone_detach(V_mynet_cloner);
}
VNET_SYSUNINIT(vnet_mynet_uninit, SI_SUB_INIT_IF, SI_ORDER_ANY,
    vnet_mynet_uninit, NULL);

static int
mynet_modevent(module_t mod __unused, int type, void *data __unused)
{
    switch (type) {
    case MOD_LOAD:
    case MOD_UNLOAD:
        return (0);
    default:
        return (EOPNOTSUPP);
    }
}

static moduledata_t mynet_mod = {
    "mynet",
    mynet_modevent,
    NULL
};

DECLARE_MODULE(mynet, mynet_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_DEPEND(mynet, ether, 1, 1, 1);
MODULE_VERSION(mynet, 1);
```

### Recuento de líneas y densidad

El `mynet.c` completo tiene unas 500 líneas de código C. Todo el driver de enseñanza, desde la cabecera de licencia al principio hasta el `MODULE_VERSION` al final, es más corto que muchas funciones individuales en drivers de producción. Esa compacidad no es casualidad: los pseudo-drivers no tienen hardware con el que comunicarse, por lo que pueden centrarse en el contrato de ifnet y nada más.

Lee el archivo completo en los materiales complementarios. Escribe tu propia copia si aún no lo has hecho. Compílalo. Cárgalo. Modifícalo. Hasta que no hayas interiorizado su forma, no pases al siguiente capítulo.

## Una traza completa del ciclo de vida

Ayuda ver, de extremo a extremo, la secuencia de eventos que ocurre cuando ejecutas los comandos habituales desde una shell. La traza que aparece a continuación sigue el modelo mental que hemos construido, pero lo une en una historia continua. Léela como una secuencia animada cuadro a cuadro, no como una tabla de referencia.

### Traza 1: de kldload a ifconfig up

Imagina que estás sentado frente al teclado con una máquina FreeBSD 14.3 recién instalada. Nunca has cargado `mynet`. Escribes el primer comando:

```console
# kldload ./mynet.ko
```

¿Qué ocurre a continuación? El cargador lee la cabecera ELF de `mynet.ko`, reubica el módulo en la memoria del kernel y recorre el linker set `modmetadata_set` del módulo. Encuentra el registro `DECLARE_MODULE` de `mynet` y llama a `mynet_modevent(mod, MOD_LOAD, data)`. Nuestro manejador devuelve cero sin hacer ningún trabajo. El cargador también procesa los registros `MODULE_DEPEND` y, como `ether` ya forma parte del kernel base, la dependencia se satisface de inmediato.

A continuación se recorre el linker set de `VNET_SYSINIT`. Se ejecuta `vnet_mynet_init()`. Llama a `if_clone_simple()` con el nombre `mynet` y los dos callbacks `mynet_clone_create` y `mynet_clone_destroy`. El kernel registra un nuevo cloner en la lista de cloners de VNET. En este punto no existe aún ninguna interfaz: el cloner es solo una fábrica.

El prompt de la shell vuelve. Escribes:

```console
# ifconfig mynet create
```

`ifconfig(8)` abre un socket de datagramas y emite el ioctl `SIOCIFCREATE2` sobre él, pasando el nombre `mynet`. El despachador de cloners del kernel encuentra el cloner `mynet` y llama a `mynet_clone_create(cloner, unit, params, params_len)` con el primer número de unidad disponible, que es cero. Nuestro callback asigna un `mynet_softc`, bloquea su mutex, llama a `if_alloc(IFT_ETHER)`, rellena los callbacks, inicializa la tabla de medios, genera una dirección MAC, llama a `ether_ifattach()` y devuelve cero. Dentro de `ether_ifattach()`, el kernel llama a `if_attach()`, que enlaza la interfaz en la lista global de interfaces, llama a `bpfattach()` para que `tcpdump(8)` pueda observarla, publica el dispositivo en el espacio de usuario a través de `devd(8)` y ejecuta los manejadores de `ifnet_arrival_event` registrados.

El prompt de la shell vuelve de nuevo. Escribes:

```console
# ifconfig mynet0 up
```

El mismo socket, el mismo tipo de ioctl, un comando diferente: `SIOCSIFFLAGS`. El kernel busca la interfaz por nombre, encuentra `mynet0` y llama a `mynet_ioctl(ifp, SIOCSIFFLAGS, data)`. Nuestro manejador observa que `IFF_UP` está activo pero `IFF_DRV_RUNNING` no, así que llama a `mynet_init()`. Esa función establece `running` a verdadero, activa `IFF_DRV_RUNNING` en la interfaz, programa el primer tick del callout y devuelve. El ioctl devuelve cero. El prompt de la shell vuelve.

Escribes:

```console
# ping -c 1 -t 1 192.0.2.99
```

En este punto, el stack de red intenta la resolución ARP. Construye un paquete de petición ARP, formateado como Ethernet + ARP, y llama a `ether_output()` para la interfaz. `ether_output()` antepone la cabecera Ethernet, llama a `if_transmit()`, que es una macro que llama a nuestra función `mynet_transmit()`. Nuestra función de transmisión incrementa los contadores, captura el paquete con BPF, libera el mbuf y devuelve cero. `tcpdump -i mynet0` habría visto la petición ARP en tránsito.

Mientras tanto, como nuestro driver también genera respuestas ARP entrantes falsas en su temporizador callout, el siguiente tick del callout sintetiza una respuesta ARP, llama a `if_input()`, y el stack cree haber recibido respuesta de `192.0.2.99`. `ping` envía la petición ICMP echo request, nuestro driver la captura, libera el mbuf y registra el éxito. `ping` nunca recibe respuesta, porque nuestro driver solo falsifica ARP; pero el ciclo de vida funcionó exactamente como se esperaba, y nada se cayó.

Esta secuencia, por trivial que parezca, ejercita casi todos los caminos de código de tu driver. Interiorízala.

### Traza 2: de ifconfig down a kldunload

Ahora estás recogiendo. Escribes:

```console
# ifconfig mynet0 down
```

De nuevo `SIOCSIFFLAGS`, esta vez con `IFF_UP` desactivado. Nuestro manejador de ioctl observa que `IFF_DRV_RUNNING` está activo pero `IFF_UP` no, así que llama a `mynet_stop()`. Esa función establece `running` a falso, desactiva `IFF_DRV_RUNNING`, drena el callout y devuelve. Los intentos de transmisión posteriores serán rechazados por `mynet_transmit()` debido a la comprobación de `running`.

```console
# ifconfig mynet0 destroy
```

El ioctl `SIOCIFDESTROY`. El kernel encuentra el cloner propietario de esta interfaz y llama a `mynet_clone_destroy(cloner, ifp)`. Nuestro callback llama a `mynet_stop()` (por si acaso: la interfaz ya estaba abajo), luego a `ether_ifdetach()`, que internamente llama a `if_detach()`. `if_detach()` desvincula la interfaz de la lista global, drena las referencias, llama a `bpfdetach()`, notifica a `devd(8)` y ejecuta los manejadores de `ifnet_departure_event`. Nuestro callback llama entonces a `ifmedia_removeall()` para liberar la lista de medios, destruye el mutex, libera el ifnet con `if_free()` y libera el softc con `free()`.

```console
# kldunload mynet
```

El cargador recorre `VNET_SYSUNINIT` y llama a `vnet_mynet_uninit()`, que desconecta el cloner con `if_clone_detach()`. A continuación se ejecuta `mynet_modevent(mod, MOD_UNLOAD, data)` y devuelve cero. El cargador desmapea el módulo de la memoria del kernel. El sistema queda limpio.

Cada comando de la secuencia corresponde a un callback específico de tu driver. Si un comando se congela, el callback roto suele ser evidente. Si provoca una caída del sistema, la traza de pila apunta directamente a él. Practica esta traza hasta que te resulte mecánica; pasarás el resto de tu carrera como autor de drivers recorriendo variantes de ella.

## Conceptos erróneos frecuentes sobre los drivers de red

Los principiantes llegan a este capítulo con un puñado de conceptos erróneos recurrentes. Nombrarlos explícitamente te ayuda a evitar bugs sutiles más adelante.

**"El driver analiza las cabeceras Ethernet."** No exactamente. Para la recepción, el driver no analiza la cabecera Ethernet en absoluto: entrega el frame en bruto a `ether_input()` (llamada desde `if_input()` dentro del framework Ethernet), y es `ether_input()` quien realiza el análisis. Para la transmisión, `ether_output()` en la capa genérica añade la cabecera Ethernet como prefijo; tu callback de transmisión normalmente recibe el frame completo y simplemente mueve sus bytes hacia el cable. El trabajo del driver es mover frames, no entender protocolos.

**"El driver debe conocer las direcciones IP."** No. Un driver Ethernet opera por debajo de IP en su totalidad. Gestiona direcciones MAC, tamaños de frame, filtros multicast y el estado del enlace, pero nunca examina las cabeceras IP. Cuando adjuntas un driver de red a una dirección mediante `ifconfig mynet0 192.0.2.1/24`, la asignación se almacena en una estructura específica de la familia de protocolos (un `struct in_ifaddr`) que el driver nunca toca. El driver solo ve los frames salientes y solo produce los entrantes: que esos frames transporten IPv4, IPv6, ARP o algo más exótico está por encima de sus competencias.

**"`IFF_UP` significa que la interfaz puede enviar paquetes."** Parcialmente cierto. `IFF_UP` significa que el administrador ha dicho: «Quiero que esta interfaz esté activa». El driver responde inicializando el hardware (o, en nuestro caso, poniendo `running` a true) y estableciendo `IFF_DRV_RUNNING`. La distinción es importante. `IFF_UP` es la intención del usuario; `IFF_DRV_RUNNING` es el estado del driver. Solo el segundo indica de forma fiable que el driver está preparado para enviar frames. Si solo compruebas `IFF_UP` antes de transmitir, en ocasiones enviarás frames a un estado de hardware medio inicializado y verás cómo la máquina entra en pánico.

**"BPF es algo que se activa para depuración."** BPF está siempre activo en todo driver de red. La llamada `bpfattach()` dentro de `ether_ifattach()` registra la interfaz con el framework Berkeley Packet Filter de forma incondicional. Cuando no existen listeners BPF, `BPF_MTAP()` es barato; comprueba un contador atómico y retorna. Cuando existen listeners, el mbuf se clona y se entrega a cada uno. No necesitas hacer nada especial para que `tcpdump` funcione con tu driver; solo tienes que llamar a `BPF_MTAP()` en ambos caminos. Olvidar esa única llamada es la razón más frecuente por la que un nuevo driver muestra paquetes en los contadores pero nada en `tcpdump`.

**"El kernel limpiará si mi driver falla."** Falso. Un crash dentro de un driver es un crash dentro del kernel. No existe ningún límite de proceso que contenga el daño. Si tu función de transmisión desreferencia un puntero nulo, la máquina entra en pánico. Si tu callback pierde un mutex, cada llamada posterior que toque la interfaz se quedará bloqueada. Programa de forma defensiva. Prueba bajo carga. Usa builds con INVARIANTS y WITNESS.

**"Los drivers de red son más lentos que los drivers de almacenamiento."** No inherentemente. Los NICs modernos procesan decenas de millones de paquetes por segundo, y un driver bien escrito usando `iflib(9)` puede mantener ese ritmo. La confusión viene del hecho de que cada paquete individual es diminuto comparado con una solicitud de almacenamiento, por lo que la sobrecarga por paquete de los diseños descuidados se hace visible de inmediato. Un driver de almacenamiento descuidado puede seguir alcanzando el 80 % de la velocidad de línea porque una sola operación de I/O mueve 64 KiB; un driver de red descuidado se desmoronará al 10 % de la velocidad de línea porque cada frame tiene 1,5 KiB y la sobrecarga por frame domina.

**"Una vez que mi driver supera `ifconfig`, ya he terminado."** Ni de lejos. Un driver que supera `ifconfig up` pero falla bajo `jail`, `vnet` o al descargar el módulo puede romper sistemas en producción. La matriz de pruebas rigurosa que construiste en la sección de testing es el verdadero baremo. Muchos bugs en producción solo se descubren en la intersección de funcionalidades: VLAN más TSO, frames jumbo más offload de checksum, modo promiscuo más filtrado multicast, ciclos rápidos de subida y bajada más listeners BPF.

Cada malentendido puede rastrearse hasta algún fragmento de lectura anterior que era técnicamente preciso pero incompleto. Ahora que has escrito un driver, estos bordes adquieren nitidez.

## Cómo llegan y salen los paquetes de tu driver

Vale la pena detenerse y trazar el camino exacto que recorre un paquete. La geografía de la pila de red de FreeBSD es más antigua de lo que podrías pensar, y gran parte de ella es invisible desde la perspectiva del driver. Conocer esa geografía facilita el diagnóstico de los errores que encuentres.

### El camino de salida

Cuando un proceso en espacio de usuario llama a `send()` sobre un socket UDP, el camino tiene este aspecto:

1. La llamada al sistema entra en el kernel y llega a la capa de sockets. Los datos se copian desde el espacio de usuario a los mbufs del kernel mediante `sosend()`.
2. La capa de sockets entrega los mbufs a la capa de protocolo, que en este caso es UDP. UDP añade una cabecera UDP al inicio y entrega el paquete a IP.
3. IP añade la cabecera IP al inicio, selecciona una ruta de salida consultando la tabla de rutas y entrega el paquete a la función de salida específica de la interfaz a través del puntero `rt_ifp` de la ruta. Para una interfaz Ethernet, esa función es `ether_output()`.
4. `ether_output()` llama a `arpresolve()` para encontrar la dirección MAC de destino. Si la caché ARP tiene una entrada, la ejecución continúa. En caso contrario, el paquete se encola dentro de ARP y se transmite una solicitud ARP; el paquete en cola se liberará más adelante cuando llegue la respuesta.
5. `ether_output()` añade la cabecera Ethernet al inicio y llama a `if_transmit(ifp, m)`, que es un macro ligero sobre el callback `if_transmit` del driver.
6. Se ejecuta tu función `mynet_transmit()`. Esta puede encolar el mbuf en el hardware, llamar a BPF, actualizar contadores y liberar o retener el mbuf según si es su propietario.

Seis capas, y solo una de ellas es tu driver. El resto es escenario que nunca tendrás que tocar. Pero cuando aparece un error, entender qué capa podría ser la responsable marca la diferencia entre una corrección de dos horas y una de dos días.

### El camino de entrada

En el lado de recepción, el camino recorre la dirección opuesta:

1. Un frame llega por el cable (o, en nuestro caso, es generado por el driver).
2. El driver construye un mbuf con `m_gethdr()`, rellena `m_pkthdr.rcvif`, llama a BPF con `BPF_MTAP()` y llama a `if_input(ifp, m)`.
3. `if_input()` es un wrapper ligero que llama al callback `if_input` de la interfaz. Para interfaces Ethernet, `ether_ifattach()` establece ese callback en `ether_input()`.
4. `ether_input()` examina la cabecera Ethernet, busca el tipo Ethernet (IPv4, IPv6, ARP, etc.) y llama a la rutina de desmultiplexación adecuada: `netisr_dispatch(NETISR_IP, m)` para IPv4, `netisr_dispatch(NETISR_ARP, m)` para ARP, y así sucesivamente.
5. El framework netisr puede diferir opcionalmente el paquete a un thread de trabajo y luego lo entrega a la rutina de entrada específica del protocolo. Para IPv4, esa rutina es `ip_input()`.
6. IP analiza la cabecera, realiza comprobaciones de origen y destino, consulta la tabla de rutas para decidir si el paquete es local o de tránsito, y lo entrega hacia arriba a la capa de transporte o lo reenvía hacia abajo para su encaminamiento.
7. Si el paquete es para el host local y el protocolo es UDP, `udp_input()` valida el checksum UDP y entrega el payload al buffer de recepción del socket correspondiente.
8. El proceso en espacio de usuario que llamó a `recv()` se despierta y lee los datos.

Ocho capas en la recepción, y de nuevo, solo una de ellas es tu driver. Pero fíjate en cuántos lugares puede llamarse a `m_pullup()` para hacer contigua una cabecera en memoria, en cuántos lugares puede liberarse el mbuf, en cuántos puede incrementarse un contador. Si ves que `ifconfig mynet0` informa de paquetes recibidos pero `tcpdump -i mynet0` no muestra nada, el problema se encuentra muy probablemente entre el paso 2 y el paso 3 (tu llamada a `BPF_MTAP()` falta o es incorrecta). Si `tcpdump` muestra los paquetes pero `netstat -s` indica que se están descartando, el problema se encuentra muy probablemente entre el paso 6 y el paso 7 (la tabla de rutas no considera que la interfaz sea propietaria de la dirección de destino).

### Por qué esta geografía importa a los autores de drivers

Comprender la geografía te otorga capacidad de diagnóstico. Cuando algo falla, puedes formular preguntas precisas. ¿Está incrementando el contador? El paso 6 del camino de salida se ejecutó. ¿Está viendo el paquete BPF? Tu llamada a `BPF_MTAP()` está presente y la interfaz está marcada como activa. ¿Llega el paquete al destino? El hardware lo transmitió realmente. Cada pregunta corresponde a un punto de control específico en la geografía, y cada punto de control reduce el rango de posibles errores.

Los drivers de producción amplían esta geografía con anillos de transmisión y recepción, procesamiento por lotes, offloads de hardware y moderación de interrupciones. Cada optimización cambia el camino; ninguna de ellas cambia la forma general. Vale la pena memorizar esa forma ahora, antes de que las optimizaciones la compliquen.

## Lo que este capítulo no ha cubierto

Una lista honesta de omisiones te ayuda a saber qué aprender a continuación, y prepara el Capítulo 29 con más precisión de lo que lo haría un resumen artificial.

**Inicialización de hardware real.** No hemos tratado la enumeración PCI, la asignación de recursos del bus, la configuración de interrupciones ni la construcción de anillos DMA. Para ello, lee detenidamente drivers como `/usr/src/sys/dev/e1000/if_em.c`, en especial `em_attach()` y `em_allocate_transmit_structures()`. Verás `bus_alloc_resource_any()`, `bus_setup_intr()`, `bus_dma_tag_create()` y `bus_dmamap_create()` en acción. Esas son las funciones que hacen que una NIC física mueva bits de verdad.

**iflib.** El framework `iflib(9)` abstrae la mayoría de las partes más delicadas de un driver Ethernet moderno. A modo de cifra aproximada, un nuevo driver de NIC en FreeBSD 14.3 suele constar de unas 1.500 líneas de código específico de hardware más llamadas a `iflib`, en lugar de las aproximadamente 10.000 líneas de gestión de anillos escrita a mano que requeriría un driver completamente artesanal. Hemos mencionado `iflib` sin enseñarlo, porque el driver didáctico es más sencillo sin él. Un driver real en producción en 2026 probablemente usa `iflib`.

**Offload de checksum.** Las NIC modernas calculan los checksums de TCP, UDP e IP en hardware. Configurar `IFCAP_RXCSUM`, `IFCAP_TXCSUM` y sus equivalentes IPv6 requiere tanto soporte en el driver como manipulación de los flags del mbuf (`CSUM_DATA_VALID`, `CSUM_PSEUDO_HDR`, etc.). Si lo haces mal, corromperás silenciosamente el tráfico solo para algunos usuarios. La mejor introducción es la función `em_transmit_checksum_setup()` de `if_em.c`, combinada con `ether_input()` para ver cómo se propagan los flags hacia arriba.

**Offload de segmentación.** TSO (transmisión), LRO (recepción) y GSO (offload de segmentación genérico) permiten que el host entregue a la NIC frames de múltiples segmentos que el hardware (o un componente auxiliar del driver) divide en fragmentos del tamaño de la MTU. Como punto de partida, lee `tcp_output()` y sigue la traza de cómo coopera con `if_hwtsomax` e `IFCAP_TSO4`.

**Filtrado multicast.** Los drivers reales programan tablas hash de multidifusión en el hardware a partir de las membresías anunciadas mediante `SIOCADDMULTI`. Hemos dejado los ioctls como stubs; una implementación real recorre `ifp->if_multiaddrs` y escribe en un registro hash de la NIC.

**Procesamiento de VLAN.** Los drivers reales establecen `IFCAP_VLAN_HWTAGGING` y permiten que `vlan(4)` delegue el etiquetado y desetiquetado al hardware. Sin ello, cada frame etiquetado con VLAN pasa por las funciones software `vlan_input()` y `vlan_output()`, más lentas pero más sencillas. Nuestro driver es transparente a VLAN: transporta los frames etiquetados tal como están.

**Negociación de offload mediante SIOCSIFCAP.** `ifconfig mynet0 -rxcsum` activa y desactiva capacidades en tiempo de ejecución. Los drivers reales deben gestionar el cambio de capacidades en pleno funcionamiento: vaciar los anillos, reconfigurar el hardware y volver a aceptar tráfico.

**SR-IOV.** La virtualización de I/O de raíz única permite que una NIC física presente múltiples funciones virtuales a un hipervisor. El soporte de FreeBSD (`iov(9)`) no es trivial. No nos hemos acercado a ello.

**Wireless.** Los drivers inalámbricos usan `net80211(4)`, un framework independiente que se superpone a `ifnet`. Tienen una máquina de estados compleja, control de velocidad sofisticado, offload de cifrado y una historia de cumplimiento normativo completamente diferente. Leer `/usr/src/sys/dev/ath/if_ath.c` es una tarde bien empleada, pero la mayor parte de lo que enseña es ortogonal a lo que hemos construido aquí.

**InfiniBand y RDMA.** Completamente fuera del alcance de este libro. Usan `/usr/src/sys/ofed/` y un framework de verbos independiente del sistema operativo.

**Aceleración específica para virtualización.** `netmap(4)`, `vhost(4)` y los fastpaths en espacio de usuario al estilo DPDK existen y son relevantes en entornos de producción en 2026. Son temas para etapas más avanzadas de tu carrera.

No cubrimos ninguno de estos temas en profundidad. Te hemos proporcionado referencias a cada uno para que, cuando tu trabajo requiera alguno, sepas por dónde empezar a leer.

## Contexto histórico: por qué ifnet tiene el aspecto que tiene

Una última parada antes del puente: una breve lección de historia. Entender de dónde viene `ifnet` hace que algunos de sus bordes más ásperos resulten menos sorprendentes.

Las primeras pilas de red de UNIX, a finales de los años 70, no tenían ninguna estructura `ifnet`. Cada driver proporcionaba un conjunto ad hoc de callbacks registrados mediante convenciones inconsistentes. Cuando 4.2BSD introdujo la API de sockets y la moderna pila TCP/IP en 1983, el equipo BSD también introdujo `struct ifnet` como interfaz uniforme entre el código de protocolo y el código del driver. La versión inicial tenía alrededor de una docena de campos: un nombre, un número de unidad, un conjunto de flags, un callback de salida y unos pocos contadores. Comparada con la `struct ifnet` actual, parece casi vacía.

A lo largo de las siguientes cuatro décadas, `struct ifnet` fue creciendo. BPF se añadió a finales de los años 80. El soporte de multidifusión llegó a principios de los años 90. El soporte de IPv6 se incorporó a finales de los años 90. La clonación de interfaces, la capa de medios y los eventos de estado de enlace llegaron a lo largo de los años 2000. Las capacidades de offload, VNETs, flags de offload de checksum, TSO, LRO y offload de VLAN aparecieron a lo largo de los años 2010. Cuando llegó FreeBSD 11 en 2016, la estructura había crecido tanto que el proyecto introdujo el tipo opaco `if_t` y las funciones de acceso `if_get*`/`if_set*`, de modo que la disposición de la estructura pudiera cambiar sin romper la compatibilidad binaria de los módulos.

Esa historia explica varias cosas. Explica por qué `ifnet` tiene tanto `if_ioctl` como `if_ioctl2`; por qué se accede a algunos campos mediante macros y a otros directamente; por qué `IFF_*` e `IFCAP_*` existen como espacios de flags paralelos; por qué la API de clonación tiene tanto `if_clone_simple()` como `if_clone_advanced()`; por qué `ether_ifattach()` existe como un wrapper sobre `if_attach()`. Cada adición resolvió un problema real. El peso acumulado es el coste de vivir dentro de un sistema en funcionamiento que nunca tuvo el lujo de empezar desde cero.

Para ti, la conclusión práctica es que la superficie de ifnet es grande y algo inconsistente. Léela como geología, no como arquitectura. Los estratos registran eventos reales en la historia de la red en UNIX. Una vez que sabes que son estratos, las inconsistencias se vuelven navegables.

## Autoevaluación: ¿conoces realmente este material?

Antes de continuar, mide tu comprensión con una rúbrica concreta. Un autor de drivers de red debería poder responder a todas las preguntas siguientes sin consultar el capítulo. Respóndelas con honestidad. Si no puedes responder alguna, vuelve a leer la sección correspondiente; no te limites a hojear hasta que la respuesta te parezca familiar.

**Preguntas conceptuales.**

1. ¿Cuál es la diferencia entre `IFF_UP` e `IFF_DRV_RUNNING`, y cuál de los dos decide si un frame se envía realmente?
2. Nombra tres callbacks que tu driver debe proporcionar y, para cada uno, describe qué ocurriría si no lo implementaras correctamente.
3. ¿Por qué el kernel genera una dirección MAC administrada localmente de forma aleatoria para las pseudo-interfaces, y qué bit debe estar activo para marcar una dirección como administrada localmente?
4. Cuando `ether_input()` recibe un frame Ethernet, ¿qué campo de `m_pkthdr` indica a la pila de qué interfaz proviene el frame, y por qué es necesario que esté correctamente establecido en todo mbuf entrante?
5. ¿Qué protege `net_epoch`, y por qué se considera más ligero que un lock de lectura tradicional?

**Preguntas mecánicas.**

6. Escribe, de memoria, la secuencia de llamadas a función desde `if_alloc()` hasta `ether_ifattach()` que crea una interfaz mínimamente funcional. No necesitas recordar las listas de argumentos; solo los nombres y el orden.
7. Escribe la llamada de macro exacta que entrega un mbuf saliente a BPF. Escribe la llamada de macro exacta para el caso entrante.
8. Dada una cadena de mbufs que podría estar fragmentada, ¿qué función auxiliar te proporciona un único buffer plano adecuado para DMA? ¿Qué función auxiliar garantiza que al menos los primeros `n` bytes sean contiguos?
9. ¿Qué ioctl produce `ifconfig mynet0 192.0.2.1/24`? ¿Qué capa del kernel lo procesa realmente: el despachador genérico `ifioctl()`, `ether_ioctl()`, o el callback `if_ioctl` de tu driver? ¿Por qué?
10. Tu driver usa `callout_init_mtx(&sc->tick, &sc->mtx, 0)`. ¿Cuál es el propósito del argumento mutex, y qué error aparecería si pasaras `NULL`?

**Preguntas de depuración.**

11. `ifconfig mynet0 up` regresa al instante, pero `netstat -in` muestra la interfaz con cero paquetes tras diez minutos de `ping`. Describe las tres causas más probables y los comandos que ejecutarías para distinguir entre ellas.
12. El módulo se carga sin problemas. `ifconfig mynet create` tiene éxito. `ifconfig mynet0 destroy` provoca un pánico con el mensaje "locking assertion". ¿Cuál es el error más probable? ¿Cómo lo corregirías?
13. `tcpdump -i mynet0` muestra paquetes salientes pero nunca entrantes, aunque `netstat -in` muestra que los contadores RX aumentan. ¿Qué llamada a función falta casi con toda seguridad, y en qué ruta de código?
14. Ejecutas `kldunload mynet` mientras una interfaz sigue existiendo. ¿Qué ocurre? ¿Qué secuencia segura debería haber seguido el usuario? ¿Cómo podría un driver de producción negarse a descargarse en estas condiciones?
15. Ejecutar `ifconfig mynet0 up` seguido inmediatamente de `ifconfig mynet0 down` cien veces en un bucle provoca que la máquina entre en pánico en la quincuagésima iteración con una cola de mbufs corrupta. Analiza la clase de error probable y cómo abordarías su corrección.

**Preguntas avanzadas.**

16. Explica con tus propias palabras qué aporta `net_epoch` que un mutex no proporciona, y cuándo usarías uno frente al otro dentro de un driver de red.
17. Si tu driver anuncia `IFCAP_VLAN_HWTAGGING`, ¿cómo cambia eso los mbufs que ve tu callback de transmisión en comparación con el comportamiento predeterminado?
18. El kernel tiene dos rutas de entrega distintas para tramas entrantes: una a través de `netisr_dispatch()` y otra mediante despacho directo. ¿Cuáles son, y cuándo le importaría a un driver cuál de las dos se usa?
19. ¿Cuál es la diferencia entre `if_transmit` y el par más antiguo `if_start`/`if_output`, y cuál debería usar un driver nuevo?
20. Describe el ciclo de vida de un VNET en un sistema FreeBSD 14.3, y explica por qué un cloner registrado con `VNET_SYSINIT` produce un cloner en cada VNET en lugar de un único cloner global.

Si respondiste todas las preguntas sin dudar, estás preparado para el Capítulo 29. Si cinco o más preguntas te dieron problemas, dedica otra sesión a este capítulo antes de avanzar. El siguiente capítulo parte de la base de que dominas este material a la perfección.

## Lecturas adicionales y estudio del código fuente

La bibliografía que sigue es breve, enfocada y ordenada por utilidad para un autor de drivers en tu etapa actual. Trátala como una lista de lecturas para las semanas posteriores a que termines el capítulo 28, no como una biblioteca abrumadora.

**Lectura obligatoria, en orden.**

- `/usr/src/sys/net/if.c`: la maquinaria genérica de interfaces. Empieza por `if_alloc()`, `if_attach()`, `if_detach()` y el despachador de ioctl `ifioctl()`. Este es el archivo que ejecuta realmente las funciones del ciclo de vida que invocas en tu driver.
- `/usr/src/sys/net/if_ethersubr.c`: la trama Ethernet. Lee `ether_ifattach()`, `ether_ifdetach()`, `ether_output()`, `ether_input()` y `ether_ioctl()`. Estas cuatro funciones forman el contrato entre tu driver y la capa Ethernet.
- `/usr/src/sys/net/if_disc.c`: el pseudo-driver mínimo. Menos de 200 líneas. Una referencia para la `ifnet` mínima indispensable.
- `/usr/src/sys/net/if_epair.c`: el pseudo-driver en par. La referencia más limpia para escribir un clonador con una estructura compartida entre dos instancias.
- `/usr/src/sys/dev/virtio/network/if_vtnet.c`: un driver paravirtual moderno. Suficientemente pequeño para leerlo íntegramente, suficientemente realista para enseñarte sobre anillos, offload de suma de verificación, multicola y gestión de recursos similar a la del hardware.

**Lee después, cuando llegue el momento.**

- `/usr/src/sys/dev/e1000/if_em.c` junto con `em_txrx.c` e `if_em.h`: un driver de NIC Intel en producción. Mayor y más elaborado, pero representativo de la complejidad real de los drivers.
- `/usr/src/sys/net/iflib.c` y `/usr/src/sys/net/iflib.h`: el framework iflib. Léelos después de estudiar `if_em.c` para que puedas reconocer las estructuras que iflib gestiona.
- `/usr/src/sys/net/if_lagg.c`: el driver de agregación de enlaces. Un estudio detallado de la orquestación de múltiples interfaces, el failover y la selección de modo.
- `/usr/src/sys/net/if_bridge.c`: bridging por software. Excelente para aprender sobre reenvío multicast, puentes de aprendizaje y la máquina de estados STP.

**Páginas de manual recomendadas.**

- `ifnet(9)`: el framework de interfaces.
- `mbuf(9)`: el sistema de buffers de paquetes.
- `bpf(9)` y `bpf(4)`: el Berkeley Packet Filter.
- `ifmedia(9)`: el framework de medios.
- `ether(9)`: funciones auxiliares para Ethernet.
- `vnet(9)`: pilas de red virtualizadas.
- `net_epoch(9)`: la primitiva de sincronización por épocas de la capa de red.
- `iflib(9)`: el framework iflib.
- `netmap(4)`: I/O de paquetes de alta velocidad en espacio de usuario.
- `netgraph(4)` y `netgraph(3)`: el framework netgraph.
- `if_clone(9)`: clonación de interfaces.

**Libros y artículos.**

Los libros de diseño de 4.4BSD (en particular *The Design and Implementation of the 4.4BSD Operating System* de McKusick, Bostic, Karels y Quarterman) siguen siendo la mejor explicación extensa de cómo surgieron las capas de sockets e interfaces. Las secciones del FreeBSD Developer's Handbook sobre programación del kernel y módulos cargables son el siguiente paso para obtener contexto general. Para el procesamiento de paquetes a alta velocidad, los artículos sobre `netmap` de Luigi Rizzo son fundamentales; explican tanto las técnicas como el razonamiento detrás de las canalizaciones modernas de paquetes de alto rendimiento.

Lleva un diario de lectura. Cuando termines un archivo, escribe un párrafo resumiendo qué te sorprendió, qué quieres revisar y qué crees que podrías tomar prestado para tus propios drivers. Con seis meses de esta práctica, tu intuición sobre cómo se estructuran los drivers en producción crecerá más rápido de lo que esperas.

## Preguntas frecuentes

Los autores de drivers nuevos tienden a hacer las mismas preguntas mientras trabajan en su primer driver `ifnet`. A continuación encontrarás las más habituales, con respuestas breves y directas. Cada respuesta es una señal de orientación, no un tratamiento exhaustivo; sigue las pistas de vuelta a la sección correspondiente del capítulo si quieres más detalle.

**P: ¿Puedo escribir un driver Ethernet sin usar `ether_ifattach`?**

Técnicamente sí; en la práctica no. `ether_ifattach()` asigna `if_input` a `ether_input()`, conecta BPF mediante `bpfattach()` y configura una docena de comportamientos predeterminados menores. Prescindir de ella significa reimplementar a mano cada uno de esos comportamientos. El único motivo para omitir `ether_ifattach()` es que tu driver no sea realmente Ethernet; en ese caso, usarás `if_attach()` directamente y proporcionarás tus propias callbacks de trama.

**P: ¿Cuál es la diferencia entre `if_transmit` e `if_output`?**

`if_output` es la callback de salida más antigua, independiente del protocolo. Para los drivers Ethernet, `ether_ifattach()` la asigna a `ether_output()`, que gestiona la resolución ARP y la trama Ethernet antes de llamar a `if_transmit`. `if_transmit` es la callback específica del driver que tú escribes. En resumen: `if_output` es lo que llama la pila; `if_transmit` es lo que llama `if_output`; tu driver proporciona esta última.

**P: ¿Necesito gestionar `SIOCSIFADDR` en mi callback de ioctl?**

No directamente. `ether_ioctl()` gestiona la configuración de direcciones para las interfaces Ethernet. Tu callback debería delegar los ioctl no reconocidos a `ether_ioctl()` a través del bloque `default:` de su sentencia switch, y los ioctl relacionados con direcciones fluirán correctamente por esa ruta.

**P: ¿Cómo sé cuándo el hardware ha transmitido realmente una trama?**

En nuestro pseudo-driver, «transmitir» es síncrono: `mynet_transmit()` libera el mbuf de inmediato. En un driver de NIC real, el hardware señaliza la finalización mediante una interrupción o un flag de descriptor de anillo; el manejador de finalización de transmisión del driver (a veces denominado «tx reaper») recorre el anillo, libera los mbufs y actualiza los contadores. Consulta `em_txeof()` en `if_em.c` para ver un ejemplo concreto.

**P: ¿Por qué `ifconfig mynet0 delete` no llama a mi driver?**

Porque la configuración de direcciones vive en la capa de protocolo, no en la capa de interfaz. La eliminación de una dirección de una interfaz Ethernet la gestiona `in_control()` (para IPv4) o `in6_control()` (para IPv6). Tu driver no tiene conocimiento de estas operaciones; solo las percibe indirectamente a través de los cambios de ruta y las actualizaciones de la tabla ARP.

**P: ¿Por qué mi driver entra en pánico cuando llamo a `if_inc_counter()` desde un callout?**

Con casi toda seguridad es porque mantienes un mutex no recursivo que fue adquirido en otro lugar. `if_inc_counter()` es seguro desde cualquier contexto en FreeBSD moderno, pero si tu callout adquiere un lock que la infraestructura del callout ya mantiene, entrarás en deadlock. El patrón más seguro es llamar a `if_inc_counter()` sin mantener ningún lock específico del driver, y actualizar tus propios contadores por separado dentro del lock.

**P: ¿Cómo consigo que mi driver aparezca en `sysctl net.link.generic.ifdata.mynet0.link`?**

No tienes que hacer nada. Ese árbol de sysctl lo rellena automáticamente la capa genérica `ifnet`. Cada interfaz registrada mediante `if_attach()` (directamente o a través de `ether_ifattach()`) recibe un nodo de sysctl. Si el tuyo falta, es que tu interfaz no se registró correctamente.

**P: Mi driver funciona en FreeBSD 14.3 pero no compila en FreeBSD 13.x. ¿Por qué?**

El tipo opaco `if_t` y las funciones de acceso asociadas se estabilizaron entre FreeBSD 13 y 14, pero varias APIs auxiliares solo llegaron en la versión 14. Por ejemplo, `if_clone_simple()` existe desde hace años, pero algunos de los auxiliares de acceso a contadores son nuevos. Puedes usar guardas `__FreeBSD_version` para compilar limpiamente en ambas versiones, o indicar claramente en tu driver que se requiere FreeBSD 14.0 o posterior.

**P: Quiero escribir un driver que acepte paquetes en una interfaz y los retransmita por otra. ¿Es eso un driver de red?**

No exactamente. Eso es un puente o un reenviador. El kernel de FreeBSD dispone de `if_bridge(4)` para bridging, `netgraph(4)` para canalizaciones de paquetes arbitrarias y `pf(4)` para filtrado y políticas. Escribir tu propio código de reenvío desde cero casi nunca es la respuesta correcta en 2026; los frameworks existentes están mejor mantenidos, son más rápidos y más flexibles. Estúdialos y configúralos antes de escribir un nuevo driver.

**P: ¿Debo preocuparme por el orden de bytes dentro de mi driver de red?**

Solo en límites concretos. Las tramas Ethernet usan orden de bytes de red (big-endian) por convención; si analizas una cabecera Ethernet tú mismo, el campo `ether_type` necesita `ntohs()`. Dentro del mbuf, los datos se almacenan en orden de bytes de red, no en el orden nativo del host. Las funciones `ether_input()` y `ether_output()` gestionan las conversiones por ti, así que la mayor parte del código de driver no toca directamente el orden de bytes.

**P: ¿Cuándo uso `m_pullup()` frente a `m_copydata()`?**

`m_pullup(m, n)` muta la cadena de mbufs de modo que los primeros `n` bytes queden almacenados de forma contigua en memoria, lo que permite acceder a ellos de forma segura mediante una conversión de puntero. `m_copydata(m, off, len, buf)` copia bytes de la cadena de mbufs a un buffer separado que tú proporcionas. Usa `m_pullup()` cuando quieras leer y posiblemente modificar campos de cabecera en su lugar. Usa `m_copydata()` cuando quieras una instantánea para inspección sin alterar el mbuf.

**P: ¿Por qué `netstat -I mynet0 1` muestra a veces cero bytes aunque se estén intercambiando paquetes?**

Es posible que estés incrementando `IFCOUNTER_IPACKETS` o `IFCOUNTER_OPACKETS` sin incrementar también `IFCOUNTER_IBYTES` o `IFCOUNTER_OBYTES`. La visualización por segundos muestra los bytes por separado; si los contadores de bytes nunca cambian, `netstat -I` informa un rendimiento de cero. Actualiza siempre el recuento de paquetes y el de bytes juntos.

**P: ¿Cómo destruyo todas las interfaces clonadas al descargar el módulo?**

El enfoque más sencillo es dejar que `if_clone_detach()` lo haga por ti; el auxiliar de desconexión del clonador recorre la lista de interfaces del clonador y destruye cada una. Si quieres protegerte contra fugas, también puedes enumerar las interfaces pertenecientes al clonador y destruirlas explícitamente antes de llamar a `if_clone_detach()`. El camino más corto suele ser el mejor, porque el auxiliar está probado y el tuyo probablemente no.

**P: Mi driver funciona bien con `ping` pero se cuelga durante una ejecución larga de `iperf3`. ¿Qué suele ocurrir?**

A tasas de paquetes elevadas, todos los sutiles errores de concurrencia de un driver quedan expuestos. Las causas habituales incluyen: un contador actualizado fuera de un lock que se ejecuta en múltiples CPUs, una cola de mbufs que no se vacía correctamente antes de liberarla, un callout que se dispara durante el apagado, una llamada a `BPF_MTAP()` después de que la interfaz haya sido desconectada. Ejecuta con WITNESS e INVARIANTS activados; las aserciones de locking casi siempre lo detectan.

## Una breve nota final sobre el oficio

Hemos dedicado muchas páginas a la mecánica: callbacks, locks, mbufs, ioctls, contadores. La mecánica es necesaria, pero no suficiente. Un buen driver de red es el producto de un autor disciplinado, no simplemente un conjunto correcto de callbacks.

Esa disciplina se manifiesta en pequeños detalles. Se manifiesta en la decisión de vaciar un callout al desconectar, aunque el conjunto de pruebas nunca detecte una fuga. Se manifiesta en la decisión de actualizar un contador en el orden correcto para que `netstat -s` cuadre en una ejecución larga. Se manifiesta en la decisión de registrar una sola vez, con claridad, cuando no se puede asignar un recurso, en lugar de permanecer en silencio o inundar el registro. Se manifiesta en la decisión de usar `M_ZERO` al asignar un softc, de modo que cualquier campo que se añada en el futuro a la estructura comience en un cero conocido aunque se olvide la inicialización explícita.

Cada decisión es pequeña. El efecto acumulado es la diferencia entre un driver que funciona el primer día y uno que funciona el día mil. Estás entrenando un hábito, no memorizando una sintaxis. Ten paciencia contigo mismo mientras el hábito se forma; lleva años.

Los grandes autores de drivers de FreeBSD, aquellos cuyos nombres ves en las etiquetas `$FreeBSD$` y en los registros de commits, no se hicieron grandes por conocer la API mejor que tú. Se hicieron grandes revisando su propio trabajo como si lo hubiera escrito otra persona, y corrigiendo cada pequeño defecto que encontraban. Esa práctica escala. Adóptala pronto.

## Miniglosario de términos de drivers de red

A continuación encontrarás un breve glosario orientado al lector que quiere repasar el vocabulario fundamental del capítulo en un solo lugar. Úsalo como recordatorio, no como sustituto de las explicaciones del texto principal.

- **ifnet.** La estructura de datos del kernel que representa una interfaz de red. Cada interfaz conectada tiene exactamente un `ifnet`. El handle opaco `if_t` es el que utiliza la mayor parte del código moderno.
- **ether_ifattach.** El wrapper sobre `if_attach()` que configura los valores predeterminados específicos de Ethernet, incluidos los hooks de BPF y la función estándar `if_input`.
- **cloner.** Una fábrica de pseudo-interfaces. Se registra con `if_clone_simple()` o `if_clone_advanced()`. Se encarga de crear y destruir interfaces en respuesta a `ifconfig name create` e `ifconfig name0 destroy`.
- **mbuf.** El buffer de paquetes del kernel. Una pequeña estructura con metadatos, un payload embebido opcional y punteros a buffers adicionales para datos encadenados. Se asigna con `m_gethdr()` y se libera con `m_freem()`.
- **softc.** Estado del driver por instancia. Se asigna con `malloc(M_ZERO)` en el callback de creación del cloner y se libera en el callback de destrucción. Habitualmente apunta al mutex, al descriptor de media, al callout y a la interfaz.
- **BPF.** El Berkeley Packet Filter, un framework que permite a las herramientas en espacio de usuario, como `tcpdump`, observar el tráfico en una interfaz. Los drivers se conectan a él mediante `BPF_MTAP()` tanto en la ruta de transmisión como en la de recepción.
- **IFF_UP.** El flag administrativo establecido por `ifconfig name0 up`. Indica la intención del usuario de activar la interfaz.
- **IFF_DRV_RUNNING.** El flag controlado por el driver que indica que este está preparado para enviar y recibir paquetes. Se establece dentro del driver una vez que finaliza la inicialización del hardware (o del pseudo-hardware).
- **Media.** La abstracción para la velocidad de enlace, el modo dúplex, la autonegociación y otras propiedades de la capa física. Se gestiona a través del framework `ifmedia(9)`.
- **Estado del enlace.** Un indicador con tres valores posibles (`LINK_STATE_UP`, `LINK_STATE_DOWN`, `LINK_STATE_UNKNOWN`) notificado mediante `if_link_state_change()`. Lo utilizan los demonios de enrutamiento y las herramientas en espacio de usuario.
- **VNET.** La pila de red virtualizada de FreeBSD. Cada VNET tiene su propia lista de interfaces, tabla de enrutamiento y sockets. Los pseudo-drivers suelen usar `VNET_SYSINIT` para registrar cloners en cada VNET.
- **net_epoch.** Un primitivo de sincronización ligero que delimita secciones críticas de lectura en la pila de red. Es más rápido que un read lock tradicional.
- **IFCAP.** Un campo de bits de capacidades (`IFCAP_RXCSUM`, `IFCAP_TSO4`, etc.) negociadas entre el driver y la pila. Controla qué offloads están activos en una interfaz concreta.
- **IFCOUNTER.** Un contador con nombre (`IFCOUNTER_IPACKETS`, `IFCOUNTER_OBYTES`, etc.) mostrado por `netstat`. Los drivers lo actualizan mediante `if_inc_counter()`.
- **Tipo Ethernet.** El campo de 16 bits en la cabecera de una trama Ethernet que identifica el protocolo encapsulado. Los valores se definen en `net/ethernet.h`, siendo `ETHERTYPE_IP` y `ETHERTYPE_ARP` los más habituales.
- **Trama jumbo.** Una trama Ethernet mayor que la MTU estándar de 1500 bytes, habitualmente de 9000 bytes. Los drivers anuncian su compatibilidad a través de `ifp->if_capabilities |= IFCAP_JUMBO_MTU`.
- **Modo promiscuo.** Un modo en el que la interfaz entrega a la pila todas las tramas observadas, no solo las dirigidas a su propia dirección MAC. Se controla mediante `IFF_PROMISC`. Lo utilizan las herramientas de análisis de red.
- **Multicast.** Tramas dirigidas a un grupo de receptores en lugar de a un único destino. Los drivers realizan el seguimiento de las suscripciones a grupos mediante `SIOCADDMULTI` y `SIOCDELMULTI`, programando habitualmente un filtro hash en el hardware.
- **Checksum offload.** Una capacidad mediante la cual la NIC calcula en hardware las sumas de comprobación de las cabeceras TCP, UDP e IP. Se negocia a través de `IFCAP_RXCSUM` e `IFCAP_TXCSUM`; se indica por mbuf mediante `m_pkthdr.csum_flags`.
- **TSO (TCP Segmentation Offload).** Una capacidad mediante la cual el host entrega a la NIC un segmento TCP de gran tamaño y la NIC lo divide en fragmentos del tamaño de la MTU. Se negocia a través de `IFCAP_TSO4` e `IFCAP_TSO6`.
- **LRO (Large Receive Offload).** El equivalente en recepción del TSO. La NIC o la capa software agrega segmentos de entrada secuenciales en una única cadena de mbufs de gran tamaño antes de entregarla a la pila.
- **VLAN tagging.** Un shim de cuatro bytes en la trama Ethernet que identifica la pertenencia a una VLAN. Los drivers pueden anunciar `IFCAP_VLAN_HWTAGGING` para delegar al hardware la inserción y eliminación del shim.
- **MSI-X.** Las interrupciones de señalización por mensaje, el reemplazo moderno de las IRQ cableadas. Permite a la NIC generar interrupciones separadas por cola.
- **Moderación de interrupciones.** Una técnica mediante la cual la NIC agrupa múltiples eventos de finalización en un menor número de interrupciones, reduciendo la sobrecarga a altas tasas de paquetes.
- **Ring buffer.** Una cola circular de descriptores compartida entre el driver y la NIC. Los rings de transmisión alimentan al hardware con paquetes; los rings de recepción entregan los paquetes del hardware al driver.
- **iflib.** El framework moderno de FreeBSD para drivers de NIC. Abstrae la gestión de rings, el manejo de interrupciones y el flujo de mbufs, de modo que el autor del driver pueda centrarse en el código específico del hardware.
- **netmap.** Un framework de E/S de paquetes de alto rendimiento que proporciona al espacio de usuario acceso directo a los rings del driver, obviando la mayor parte de la pila de red.
- **netgraph.** Un framework flexible para componer pipelines de procesamiento de paquetes a partir de nodos reutilizables. En gran medida ortogonal a la escritura de drivers, aunque suele ser relevante para la arquitectura de red.
- **pf.** El filtro de paquetes de FreeBSD. Un motor de firewall y NAT que se sitúa en línea con `ether_input()` y `ether_output()` a través de hooks de `pfil(9)`. Los drivers no interactúan con él directamente; los hooks los inserta la capa genérica.
- **pfil.** La interfaz del filtro de paquetes a través de la cual los firewalls se conectan a la ruta de reenvío. Proporciona a frameworks como `pf` e `ipfw` un punto estable desde el que observar y modificar paquetes.
- **if_transmit.** El callback de salida por driver, establecido durante la asignación de la interfaz. Recibe una cadena de mbufs y es responsable de encolarla para el hardware o descartarla.
- **if_input.** El callback de entrada por interfaz. En los drivers Ethernet, `ether_ifattach()` lo establece a `ether_input()`. El driver lo invoca mediante el helper `if_input(ifp, m)` para entregar las tramas recibidas a la pila.
- **if_ioctl.** El callback ioctl por driver. Gestiona los ioctls a nivel de interfaz como `SIOCSIFFLAGS`, `SIOCSIFMTU` y `SIOCSIFMEDIA`. Delega los ioctls desconocidos a `ether_ioctl()` en el caso de los drivers Ethernet.

Ten este glosario a mano mientras lees el Capítulo 29 y los capítulos que siguen. Cada término reaparece con la frecuencia suficiente como para que una referencia rápida resulte de gran utilidad.

## Punto de control de la Parte 6

La Parte 6 ha puesto a prueba la disciplina de las Partes 1 a 5 bajo tres transportes muy diferentes: USB, almacenamiento respaldado por GEOM e interfaces de red basadas en `ifnet`. Antes de que la Parte 7 retome el arco de `myfirst` y empiece a trabajar en portabilidad, seguridad, rendimiento y calidad de código, conviene confirmar que los tres vocabularios de transporte han convergido en el mismo modelo subyacente.

Al final de la Parte 6 deberías ser capaz de hacer cada una de las siguientes cosas:

- Conectarte a un dispositivo USB a través del framework `usb_request_methods`: configurar transferencias para endpoints de control, bulk, interrupción e isocrónicos; despachar lectura y escritura a través de los callbacks de transferencia; y sobrevivir al hot-plug y hot-unplug como condiciones normales de operación.
- Escribir un driver de almacenamiento que se conecte a GEOM: provisionar un proveedor mediante `g_new_providerf`, atender solicitudes BIO en la rutina `start` de la clase, visualizar mentalmente los threads `g_down`/`g_up`, y desmontar limpiamente con el sistema de archivos montado.
- Escribir un driver de red que presente un `ifnet` a través de `ether_ifattach`: implementar `if_transmit` para el camino de salida, llamar a `if_input` para el camino de entrada, integrarse con `bpf` y el estado del medio, y limpiar mediante `ether_ifdetach`.
- Explicar por qué los tres transportes parecen tan diferentes en la superficie pero comparten la misma disciplina subyacente de las Partes 1 a 5: attach con Newbus, gestión del softc, asignación de recursos, locking, orden de detach, observabilidad y disciplina de producción.

Si alguno de estos puntos todavía se siente inestable, los laboratorios que conviene revisar son:

- Ruta USB: Lab 2 (Construcción y carga del esqueleto del driver USB), Lab 3 (Una prueba de loopback bulk), Lab 6 (Observar el ciclo de vida del hot-plug) y Lab 7 (Construir un esqueleto de `ucom(4)` desde cero) en el Capítulo 26.
- Ruta de almacenamiento GEOM: Lab 2 (Construir el driver esqueleto), Lab 3 (Implementar el manejador de BIO), Lab 4 (Aumentar el tamaño y montar UFS) y Lab 10 (Romperlo a propósito) en el Capítulo 27.
- Ruta de red: Lab 1 (Construir y cargar el esqueleto), Lab 2 (Ejercitar el camino de transmisión), Lab 3 (Ejercitar el camino de recepción), Lab 5 (`tcpdump` y BPF) y Lab 6 (Detach limpio) en el Capítulo 28.

La Parte 7 espera lo siguiente como punto de partida:

- Soltura para pasar de `cdevsw`, GEOM e `ifnet` como tres idiomas sobre el mismo núcleo de Newbus y softc, en lugar de verlos como tres temas desconectados.
- Comprensión de que la Parte 7 retoma el arco de `myfirst` para el pulido final en portabilidad, seguridad, rendimiento, trazado, trabajo con el depurador del kernel y el oficio de participar en la comunidad. Las demostraciones específicas de cada transporte de la Parte 6 no continúan; sus lecciones sí lo hacen.
- Una biblioteca mental de tres transportes reales que has tocado con tus propias manos, de modo que cuando el Capítulo 29 hable de abstracción entre backends, estarás apoyándote en la experiencia y no en ejemplos que solo has leído.

Si todo eso está sólido, la Parte 7 está lista para ti. Los nueve capítulos finales son la parte del libro que convierte a un autor competente de drivers en un artesano del oficio; los cimientos que las Partes 1 a 6 han sentado son lo que hace posible esa transición.

## Mirando hacia delante: puente al Capítulo 29

Acabas de escribir un driver de red. El siguiente capítulo, **Portabilidad y abstracción de drivers**, amplía el enfoque desde los detalles concretos que ya dominas y plantea la pregunta: ¿cómo escribimos drivers que funcionen bien en las múltiples arquitecturas que soporta FreeBSD, y cómo estructuramos el código de un driver de modo que partes de él puedan reutilizarse en distintos backends de hardware?

Esa pregunta es más nítida después del Capítulo 28 que antes. Has escrito drivers para tres subsistemas muy diferentes: dispositivos de caracteres sobre `cdevsw`, dispositivos de almacenamiento sobre GEOM e interfaces de red sobre `ifnet`. Los tres se ven distintos en la superficie, pero comparten una cantidad sorprendente de fontanería interna: probe y attach, asignación del softc, gestión de recursos, control del ciclo de vida, limpieza al descargar. El Capítulo 29 convertirá esa observación en una refactorización práctica: aislar el código dependiente del hardware, separar los backends detrás de una API común y preparar el driver para compilar en x86, ARM y RISC-V por igual.

En el Capítulo 29 no estarás escribiendo un nuevo tipo de driver. Estarás aprendiendo a hacer que los drivers que ya has escrito sean más robustos, más portables y más mantenibles. Ese es un tipo de progreso diferente, uno que importa en el momento en que empiezas a trabajar en un driver que vivirá durante años.

Antes de continuar, descarga todos los módulos que creaste en este capítulo, destruye todas las interfaces y asegúrate de que `netstat -in` vuelva a mostrar una línea de base tranquila. Cierra el diario de laboratorio con una nota breve sobre lo que ha funcionado y lo que te ha dejado perplejo. Descansa los ojos un momento. Luego, cuando estés listo, pasa la página.

Te has ganado el paso.
