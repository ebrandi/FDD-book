---
title: "Virtualización y contenedorización"
description: "Desarrollo de drivers para entornos virtualizados y contenedorizados"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 30
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 270
language: "es-ES"
---
# Virtualización y contenedorización

## Introducción

Llegas al Capítulo 30 con un nuevo hábito mental. El Capítulo 29 te enseñó a estructurar un driver para que absorba la variación de hardware, bus y arquitectura sin desmoronarse en un laberinto de condicionales. Has aprendido a aislar las partes que cambian en archivos pequeños específicos de cada backend y a mantener el núcleo limpio. Has conocido la idea de que un driver no se escribe para una sola máquina, sino para una familia de máquinas que comparten un modelo de programación. Esa lección tiene mucho peso en este capítulo, porque el entorno que rodea a tu driver puede cambiar ahora de una manera más radical que todo lo que el Capítulo 29 contempló. La propia máquina puede no ser real.

Este capítulo trata de lo que ocurre cuando el hardware sobre el que funciona tu driver no es una tarjeta física enchufada en una ranura física, sino una simulación de software presentada a un sistema operativo invitado por un hipervisor; o cuando se le pide al driver que se adjunte dentro de un jail que solo ve una parte del árbol de dispositivos; o cuando la pila de red con la que coopera es una de varias pilas que se ejecutan en paralelo dentro de un único kernel. Cada una de estas situaciones se aparta del modelo mental de «un kernel, una máquina, un árbol de dispositivos» que construyeron los capítulos anteriores. Cada una cambia lo que tu driver puede asumir, lo que puede hacer y lo que un usuario de tu driver puede esperar con seguridad de él.

La palabra «virtualización» puede significar cosas muy distintas según quién hable. Para un administrador de sistemas que gestiona una flota en la nube, significa máquinas virtuales con sus propios kernels completos. Para un usuario de FreeBSD, a menudo significa `jail(8)` y sus derivados, que aíslan procesos y sistemas de archivos sin proporcionarle un kernel propio a cada uno. Para un autor de drivers, significa ambas cosas y más. Significa dispositivos paravirtuales como VirtIO, diseñados explícitamente para que un sistema invitado pueda manejarlos con facilidad. Significa dispositivos emulados que un hipervisor presenta a un invitado como si fuesen hardware físico. Significa dispositivos en modo passthrough, donde un componente hardware real se entrega a un invitado de forma más o menos directa, apartándose el hipervisor todo lo que puede con seguridad. Significa jails cuya visión de `devfs` está recortada por un conjunto de reglas para que los procesos contenidos en su interior no puedan ver todos los dispositivos que el host puede ver. Significa jails VNET que poseen una pila de red completa propia, con un `ifnet` que el host les ha prestado.

Si esa lista ya te parece mucha cosa, respira. El capítulo presentará cada una de estas piezas de una en una, con suficiente base real de FreeBSD para que puedas cerrar el libro, abrir el árbol de código fuente y encontrar los archivos relevantes por tu propia cuenta. Nada en este capítulo es imposible de aprender; es un conjunto de ideas distintas pero relacionadas que comparten un hilo conductor. El hilo es este: un driver deja de ser la pieza de software más importante de su ruta de hardware particular. Por encima de él se sitúa un hipervisor, un framework de jails o un runtime de contenedores; a su lado se encuentran otros invitados o jails que comparten el host; por debajo de él hay un componente de silicio que el driver ya no posee en exclusiva. Escribir drivers para ese mundo no es más difícil que escribirlos para bare metal, pero es diferente de maneras que es fácil pasar por alto si no te las han mostrado.

El capítulo recorre dos direcciones bien diferenciadas. La primera dirección trata sobre **la escritura de drivers de invitado**: código que se ejecuta dentro de una máquina virtual y se comunica con los dispositivos que el hipervisor presenta. Pasarás la mayor parte del tiempo en esta dirección, porque es donde ocurre la mayor parte de la programación novedosa. VirtIO es el ejemplo canónico, y volveremos a él con frecuencia. La segunda dirección trata sobre **la cooperación con la infraestructura de virtualización propia de FreeBSD** desde el lado del host: entender cómo se adjunta tu driver dentro de un jail, cómo se comporta cuando el host mueve una interfaz a un jail VNET, cómo gestiona una llamada a `ioctl` por parte de un usuario dentro de un jail que solo el root del host debería poder realizar, y cómo probarlo todo sin estropear un host en funcionamiento. Esta dirección tiene menos que ver con APIs exóticas y más con la disciplina, los límites de privilegio y un modelo mental cuidadoso de qué es visible desde dónde.

El capítulo no pretende enseñarte cómo escribir un hipervisor, cómo implementar un nuevo transporte VirtIO ni cómo construir un runtime de contenedores desde cero. Son temas extensos con libros propios. Lo que enseña es cómo un autor de drivers debe pensar, prepararse y trabajar con entornos virtualizados y en contenedores, de modo que los drivers que escribas tengan sentido en cualquier lugar donde se carguen. Al final del capítulo, reconocerás un driver de invitado VirtIO cuando lo veas, sabrás cómo detectar si tu driver se está ejecutando en una máquina virtual, podrás explicar qué hace `devfs_ruleset(8)` y por qué importa para la exposición de dispositivos, podrás razonar sobre las diferencias entre los jails VNET y los jails ordinarios para un driver de red, y habrás escrito tu propio driver de invitado VirtIO de tamaño reducido contra un backend `bhyve(8)` de FreeBSD.

Antes de comenzar, unas palabras sobre el tono de lo que sigue. Algunos de los temas de este capítulo, en especial los aspectos internos del hipervisor y los límites de seguridad de los jails, se han ganado la reputación de ser exóticos. Un autor de drivers que nunca ha mirado bajo el capó puede sentirse intimidado por su jerga. No debería. El código es código de FreeBSD; las APIs son APIs de FreeBSD; la mentalidad es la misma que has ido construyendo desde el Capítulo 1. Avanzaremos despacio y volveremos continuamente a archivos reales que puedes abrir. Empecemos.

## Guía de lectura: cómo usar este capítulo

Este capítulo ocupa un lugar diferente en la progresión de aprendizaje respecto al Capítulo 29. El Capítulo 29 trataba sobre cómo está organizado tu driver en disco. El Capítulo 30 trata sobre cómo es el mundo que rodea a tu driver en tiempo de ejecución. Esa diferencia importa en cuanto a cómo debes leer y practicar. Los patrones del Capítulo 29 se pueden asimilar leyendo con atención y escribiendo al mismo tiempo. Los patrones de este capítulo se asientan con más firmeza si también arrancas una máquina virtual, creas un jail y observas cómo se comporta el driver dentro de ellos. Planifica en consecuencia.

Si eliges el **camino de solo lectura**, planifica unas dos o tres horas de concentración. Al final comprenderás el mapa conceptual: qué son los dispositivos paravirtuales, emulados y en modo passthrough; cómo utiliza VirtIO los anillos compartidos y la negociación de características; cómo los jails y VNET aíslan dispositivos y pilas de red; qué controla `rctl(8)` desde el punto de vista del driver. Todavía no tendrás los reflejos de un autor de drivers que ha depurado un fallo de concordancia en una virtqueue a las tres de la madrugada, y está bien. La lectura inicial es un primer encuentro legítimo, y el material tiene suficiente profundidad como para que una segunda pasada con laboratorios más adelante extraiga mucho más valor de él.

Si eliges el **camino de lectura más laboratorios**, planifica de seis a diez horas repartidas en dos o tres sesiones. Instalarás `bhyve(8)` en tu máquina de laboratorio, arrancarás un invitado FreeBSD 14.3 sobre él con dispositivos VirtIO, escribirás un pequeño driver de pseudodispositivo VirtIO llamado `vtedu`, y observarás cómo se adjunta, gestiona peticiones del dispositivo en el lado del host y se desconecta limpiamente. También crearás un jail simple, adjuntarás el driver a él bajo diferentes conjuntos de reglas de `devfs`, y observarás qué ocurre cuando el driver expone un dispositivo dentro del jail. Los laboratorios están estructurados de modo que cada uno es autosuficiente, te deja con un sistema funcional y refuerza un concepto específico del texto principal.

Si eliges el **camino de lectura más laboratorios más desafíos**, planifica un fin de semana largo o unas cuantas noches. Los desafíos llevan los laboratorios básicos a un territorio más realista: ampliar `vtedu` para aceptar múltiples virtqueues, escribir una pequeña herramienta que detecte el hipervisor del invitado a través de `vm.guest` y adapte su comportamiento, construir un jail VNET y mover una interfaz tap hacia él, y redactar un informe breve sobre cómo un driver que exporta una superficie `ioctl` debería decidir qué ioctls son accesibles desde dentro de un jail. Cada desafío es una invitación, no una obligación, y está dimensionado para poder completarse sin necesidad de un segundo fin de semana.

Una nota sobre el entorno de laboratorio. Seguirás utilizando la máquina FreeBSD 14.3 desechable que estableciste en capítulos anteriores. Esa máquina actuará como **host** en este capítulo. Sobre ella ejecutarás invitados `bhyve(8)`, que son instalaciones de FreeBSD más pequeñas gestionadas por tu host. También crearás algunos jails directamente en el host. Esta estructura anidada suena complicada la primera vez que se describe, pero en la práctica es genuinamente sencilla: tu host ejecuta FreeBSD, dentro del host arrancas una máquina virtual `bhyve` que también ejecuta FreeBSD, y dentro del host o del invitado creas jails. Cada capa es independiente, cada capa tiene su propio `dmesg`, y cada capa es barata de recrear desde cero si algo sale mal.

Haz una instantánea del host antes de empezar. El capítulo te pedirá que modifiques `/etc/devfs.rules`, que cargues y descargues módulos del kernel, y que crees pequeñas máquinas virtuales. Nada de esto es arriesgado si se maneja con cuidado, pero los accidentes ocurren, y la instantánea convierte cualquier error en una restauración de dos minutos. Si tu host es en sí mismo una máquina virtual en VirtualBox o VMware, la herramienta de instantáneas propia de la plataforma es la forma más rápida de hacerlo. Si tu host es bare metal, un entorno de boot de ZFS con `bectl(8)` es una buena alternativa.

Una nota más. Los laboratorios de este capítulo necesitan algunos paquetes que es posible que aún no tengas instalados. Necesitarás `bhyve-firmware` para el soporte de invitados UEFI, `vm-bhyve` como interfaz más cómoda para `bhyve` y sus accesorios, y `jq` para parsear la salida JSON durante algunas de las pruebas más elaboradas. Los comandos para instalarlos están en los laboratorios; no los instales ahora, pero ten en cuenta que llegarán.

### Requisitos previos

Deberías sentirte cómodo con todo lo visto en los capítulos anteriores. En particular, este capítulo asume que ya sabes cómo escribir un módulo del kernel cargable desde cero, cómo encajan `probe()` y `attach()` en el ciclo de vida del driver, cómo se asigna y utiliza softc, y cómo se relaciona `device_t` con `devclass`. Asume fluidez con los accesores `bus_read_*` y `bus_write_*` del Capítulo 15, familiaridad básica con los manejadores de interrupciones del Capítulo 18, y los hábitos de driver portable del Capítulo 29. Si algo de eso te resulta incierto, volver brevemente al material anterior te ahorrará tiempo aquí.

También deberías estar familiarizado con la administración básica de sistemas FreeBSD: leer `dmesg`, editar `/etc/rc.conf`, usar `sysctl(8)`, y crear y destruir jails con `jail(8)` o su envoltorio `service jail`. No necesitas experiencia previa con bhyve; los laboratorios te guiarán paso a paso. Tampoco necesitas experiencia previa con contenedores, ya que la historia de los contenedores en FreeBSD son, en última instancia, jails con una vestimenta operacional diferente.

### Lo que este capítulo no cubre

Un capítulo responsable te dice lo que omite. Este capítulo no enseña los aspectos internos del hipervisor `bhyve(8)`. No enseña `libvirt(3)`, `qemu(1)` ni el subsistema KVM de Linux. No te convierte en un experto en contenedores OCI. No cubre el código de paravirtualización de `xen(4)`, ya que Xen se ha convertido en un nicho más reducido en el ecosistema FreeBSD y `bhyve` es la historia nativa de FreeBSD que merece la pena aprender primero. Menciona `jail(8)` exactamente al nivel que necesita un autor de drivers, no con la profundidad que desearía un administrador de jails. Para los temas que faltan, el FreeBSD Handbook y las respectivas páginas del manual son tus amigos, y los señalaré cuando sean relevantes.

Varios temas que podrían aparecer razonablemente en un capítulo de virtualización tienen su lugar en otras partes de este libro. La programación segura frente a entradas hostiles es el tema del Capítulo 31, no de este; aquí encontrarás los límites de privilegio y la visibilidad de jails, pero la disciplina de seguridad más profunda (Capsicum, el framework MAC, el fuzzing dirigido por sanitizadores) espera en el capítulo siguiente. El DMA avanzado bajo virtualización (la configuración del IOMMU, el mapeo de páginas con `bus_dmamap`) se trata aquí de forma conceptual y se desarrolla con mayor profundidad en capítulos posteriores. El ajuste de rendimiento bajo virtualización (cómo medir el overhead paravirtual, cómo decidir entre emulado y passthrough para una carga de trabajo concreta) es un tema que reaparece en el Capítulo 35. Los mencionaremos donde conecten con el contenido, pero no profundizaremos en ellos.

### Estructura y ritmo

La sección 1 establece el modelo mental: qué significan la virtualización y la contenedorización para un autor de drivers, y en qué se diferencian de la idea de «hardware, pero más lento». La sección 2 explica los tres estilos de dispositivo invitado (emulado, paravirtualizado y passthrough) y qué implica cada uno para el driver. La sección 3 ofrece un recorrido cuidadoso y accesible para principiantes por VirtIO: el modelo de anillo compartido, la negociación de características y las APIs de `virtqueue(9)` que utilizarás. La sección 4 enseña cómo detecta un driver su entorno de ejecución mediante `vm_guest` y funciones relacionadas, y cuándo esa detección es una buena idea y cuándo no. La sección 5 se orienta hacia el lado del host y examina `bhyve(8)`, `vmm(4)` y el passthrough de PCI. La sección 6 cubre los jails, `devfs` y VNET: la historia de la contenedorización en FreeBSD desde la perspectiva del driver. La sección 7 aborda los límites de recursos y los límites de privilegios. La sección 8 trata las pruebas y la refactorización. La sección 9 retoma el tiempo, la memoria y el manejo de interrupciones desde la perspectiva de la virtualización, los temas menos llamativos donde los drivers de principiantes suelen fallar de formas sutiles. La sección 10 amplía el enfoque hasta FreeBSD en arm64 y riscv64, cuyas particularidades de virtualización tienen su propia forma. A continuación vienen los laboratorios y los ejercicios de desafío, junto con un apéndice de resolución de problemas y un puente final hacia el capítulo 31.

Lee las secciones en orden. Cada sección asume la anterior, y los laboratorios dependen de que hayas leído e interiorizado las secciones precedentes.

### Trabaja sección a sección

Un patrón recurrente en este libro es que cada sección hace una sola cosa. No intentes leer dos secciones a la vez, y no saltes a una sección «más interesante» si algo en la actual te resulta difícil. Las partes interesantes de este capítulo se apoyan en las fundamentales, y un lector que haya saltado los fundamentos empleará más tiempo desmontando los laboratorios que un lector cuidadoso en el capítulo entero.

### Mantén el driver de referencia a mano

Varios laboratorios de este capítulo parten de un pequeño driver pedagógico llamado `vtedu`. Lo encontrarás en `examples/part-07/ch30-virtualisation/`, organizado de la misma forma que los ejemplos de los capítulos anteriores. Cada directorio de laboratorio contiene el estado del driver en ese paso, junto con el Makefile, el README y los scripts de apoyo. Clona el directorio, escribe el código junto con el libro y carga el módulo después de cada cambio. Refactorizar un driver invitado de VirtIO en la cabeza es más difícil que hacerlo en disco; la retroalimentación del sistema de build y de `dmesg` es la mitad de la lección.

### Abre el árbol de código fuente de FreeBSD

Varias secciones señalarán archivos reales de FreeBSD. Los que merecen una lectura atenta en este capítulo son `/usr/src/sys/dev/virtio/random/virtio_random.c` (el driver de VirtIO más pequeño y completo del árbol), `/usr/src/sys/dev/virtio/virtio.h` y `/usr/src/sys/dev/virtio/virtqueue.h` (las superficies de API pública), `/usr/src/sys/dev/virtio/virtqueue.c` (la maquinaria del anillo), `/usr/src/sys/dev/virtio/pci/virtio_pci.c` y `/usr/src/sys/dev/virtio/mmio/virtio_mmio.c` (los dos backends de transporte), `/usr/src/sys/sys/systm.h` (para `vm_guest`), `/usr/src/sys/kern/subr_param.c` (para el sysctl `vm.guest`), `/usr/src/sys/net/vnet.h` (para los primitivos de VNET) y `/usr/src/sys/kern/kern_jail.c` (para las APIs de prison y jail). Ábrelos cuando el texto lo indique y lee alrededor del punto exacto al que señala. Los archivos no son decoración; son la fuente de verdad.

### Lleva un cuaderno de laboratorio

Continúa el cuaderno de laboratorio de los capítulos anteriores. Para este capítulo, anota una breve nota por cada laboratorio importante: qué comandos ejecutaste, qué módulos cargaste, qué dijo `dmesg`, qué te sorprendió. Los detalles del trabajo de virtualización son fáciles de olvidar después de unos días, y un registro escrito convierte cada sesión futura de depuración en una consulta de un minuto en lugar de media hora de reconstrucción.

### Ve a tu ritmo

Varias ideas de este capítulo te resultarán nuevas la primera vez que las encuentres: la memoria de anillo compartido, la negociación de características, el current vnet local al thread de VNET, el sistema de numeración de conjuntos de reglas de devfs. Esa novedad es normal. Si notas que tu comprensión se nubla durante una subsección concreta, para. Vuelve a leer el párrafo anterior, prueba un pequeño experimento en el shell y regresa con la mente despejada al cabo de una hora. Sesiones regulares de treinta minutos producen una comprensión mejor que un único esfuerzo agotador de todo el día.

## Cómo sacar el máximo partido de este capítulo

El capítulo 30 premia la curiosidad, la paciencia y la disposición a experimentar. Los patrones concretos que introduce, las estructuras de anillo, los bits de característica, los ámbitos de prison y vnet, no son abstractos. Cada uno corresponde a código que puedes leer, estado que puedes observar con `sysctl` o `vmstat` y comportamiento que puedes provocar con comandos cortos. El hábito más valioso que puedes adquirir mientras lees el capítulo es moverte con libertad entre el texto, el árbol de código fuente y el sistema en ejecución.

### Lee con el árbol de código fuente abierto

No leas la sección de VirtIO sin tener `virtio.h`, `virtqueue.h` y `virtio_random.c` abiertos en otra ventana. Cuando el texto diga que un driver llama a `virtqueue_enqueue` para entregar un buffer al host, desplázate hasta esa función y observa su contexto. Cuando el texto mencione la macro `VIRTIO_DRIVER_MODULE`, abre `virtio.h` y observa las dos líneas de `DRIVER_MODULE` en las que se expande. El kernel de FreeBSD es mucho más accesible de lo que su reputación sugiere, y la única forma de confirmarlo es seguir abriéndolo.

### Escribe el código de los laboratorios

Cada línea de código de los laboratorios está ahí para enseñar algo. Escribirla tú mismo te ralentiza lo suficiente para notar la estructura. Copiar y pegar el código a menudo parece productivo y generalmente no lo es; la memoria muscular de teclear código del kernel es parte del aprendizaje. Si tienes que copiar, copia una función cada vez y lee cada línea mientras la pegas.

### Ejecuta lo que lees

Cuando el texto presente un comando, ejecútalo. Cuando el texto presente un nombre de sysctl, consúltalo. Cuando el texto presente un módulo del kernel, cárgalo. El sistema en ejecución te sorprenderá a veces (el valor de `vm.guest` puede no ser el que esperas si tu propio laboratorio es una VM), y cada sorpresa es una oportunidad para aprender algo que el texto no necesitaba explicar explícitamente. Un sistema FreeBSD en ejecución es un tutor paciente.

### Trata `dmesg` como parte del manuscrito

Una fracción significativa de lo que enseña este capítulo solo es visible en la salida del log del kernel. El conjunto de características negociadas de un dispositivo VirtIO, la secuencia de attach de un driver invitado, la interacción entre un módulo y un jail: todo esto aflora en `dmesg`. Léelo con frecuencia. Sigue su salida en tiempo real durante los laboratorios. Copia las líneas relevantes en tu cuaderno cuando enseñen algo no evidente. No trates `dmesg` como ruido; trátalo como el testimonio fiel de lo que el kernel realmente hizo.

### Rompe las cosas deliberadamente

En tres puntos del capítulo te sugeriré romper algo deliberadamente para ver qué ocurre. Esos son los momentos más instructivos que puedes proporcionarte. Descarga un módulo necesario antes que un driver que depende de él y observa cómo se queja el grafo de dependencias. Elimina una regla de devfs y observa cómo el dispositivo desaparece desde dentro del jail. Arranca un sistema invitado con una CPU y luego con cuatro y compara la configuración de las virtqueues. Los fallos deliberados enseñan de una manera que el éxito no puede.

### Trabaja en parejas cuando puedas

Si tienes un compañero de estudio, este es un capítulo estupendo para trabajar en pareja. Uno puede ejecutar el host y observar `dmesg`, mientras el otro ejecuta el sistema invitado y maneja el driver. Las dos perspectivas enseñan cosas distintas, y cada uno notará lo que el otro pasó por alto. Si trabajas solo, usa dos pestañas de terminal y alterna la atención entre ellas.

### Confía en la iteración, no en la memorización

No recordarás cada flag, cada enum, cada macro de este capítulo en la primera lectura. Eso está bien. Lo que importa es que recuerdes dónde buscar, cuál es la forma general del tema y cómo reconocer cuándo tu driver está haciendo lo correcto. Los identificadores concretos se volverán algo natural después de haber escrito y depurado dos o tres drivers propios con conciencia de virtualización; no son un ejercicio de memorización.

### Tómate descansos

La depuración con conciencia de virtualización tiene un coste cognitivo particular. Estás siguiendo el estado en varios frentes a la vez: el host, el sistema invitado, el driver, el modelo de dispositivo, el jail. Tu mente se fatiga más rápido que cuando trabajas en un driver de bare metal monohilo. Dos horas de trabajo enfocado seguidas de un descanso real son casi siempre más productivas que cuatro horas de trabajo sin parar.

Con esos hábitos establecidos, comencemos.

## Sección 1: Qué significan la virtualización y la contenedorización para los autores de drivers

Antes de tocar ningún código, necesitamos ponernos de acuerdo sobre de qué estamos hablando. Las palabras «virtualización» y «contenedorización» han sido desgastadas por el lenguaje de marketing, y llevan significados distintos según el contexto en que las encuentras. El libro blanco de un proveedor usa «virtualización» como sinónimo de ejecutar múltiples sistemas operativos en el mismo hardware; la documentación de un proveedor cloud lo usa como abreviatura de cualquier carga de trabajo gestionada; un administrador de FreeBSD lo usa para cualquier cosa, desde `bhyve(8)` hasta `jail(8)`. Para un autor de drivers, esos significados no son intercambiables. La forma del problema que estás resolviendo cambia según el tipo de virtualización que encuentre tu driver. Esta sección ancla el vocabulario para el resto del capítulo.

### Dos familias, no una

La primera distinción que hay que trazar es entre **máquinas virtuales** y **contenedores**. Resuelven problemas distintos y generan restricciones diferentes para los autores de drivers. Confundirlas es el error más habitual en este territorio.

Una **máquina virtual** es un ordenador emulado por software. Un hipervisor, que a su vez se ejecuta en hardware real, crea un entorno de ejecución que parece una máquina completa para el software que hay dentro. La máquina tiene una BIOS o UEFI, memoria, CPUs, discos y tarjetas de red. El sistema operativo dentro de la VM arranca desde cero igual que lo haría en hardware real, carga un kernel y engancha los drivers. La observación que importa es que el **kernel invitado es un kernel completo**, independiente del kernel del host. Un sistema invitado FreeBSD que se ejecuta bajo un host FreeBSD no comparte código del kernel con el host; los dos kernels son pares, separados por el hipervisor y por la barrera entre la memoria del sistema invitado y la memoria del host.

Un **contenedor**, en el sentido que le da FreeBSD al término, es una cosa muy distinta. Un contenedor es una partición con espacio de nombres de un único kernel. El kernel del host es el único kernel en escena; los contenedores son grupos de procesos separados, vistas del sistema de archivos separadas y, a menudo, pilas de red separadas, pero todos se ejecutan en el mismo kernel con los mismos drivers. En FreeBSD, el contenedor clásico es un `jail(8)`. Los runtimes de contenedores modernos como `ocijail` y `pot` añaden scripts y orquestación alrededor de ese mismo primitivo de jail. La observación aquí es que **hay un único kernel**. Un driver en el host es visible para todos los jails, sujeto a las reglas que impone el host; un jail no puede cargar un driver propio y no tiene un kernel en el que cargarlo.

Estas dos familias se solapan solo de formas superficiales. Una VM y un jail parecen «otro sistema» desde el exterior. Tanto una VM como un jail tienen su propia `/`, su propia red y sus propios usuarios. Pero la frontera del kernel no está ni cerca de ser la misma, y eso importa enormemente para los drivers.

Una VM ejecuta un kernel invitado completo; el kernel ve hardware virtualizado (o en passthrough); el driver que escribes para ese hardware es un **driver invitado** que hace attach dentro del sistema invitado. Un jail comparte el kernel del host; no se escribe ningún driver invitado, porque no hay ningún kernel invitado en el que cargarlo; en su lugar, la pregunta es cómo un **driver del host** expone sus servicios a los procesos que se ejecutan dentro del jail. Las técnicas que usas en cada caso son distintas.

Para el resto de este capítulo, cuando el texto diga «virtualización», normalmente se refiere al caso de la VM, a menos que el contexto deje claro que se está hablando de jails. Cuando diga «contenerización», normalmente se refiere al caso de los jails. Lee esas palabras teniendo en mente estas dos familias y evitarás la mayor parte de la confusión que suelen tener los recién llegados.

### Las máquinas virtuales como entorno para drivers

Una VM presenta hardware al sistema invitado. El aspecto concreto de ese hardware depende de la configuración del hipervisor. Existen tres grandes categorías de dispositivos con los que puede encontrarse un invitado, y cada una de ellas transforma el problema del driver.

**Los dispositivos emulados** son el enfoque más antiguo. El hipervisor presenta al invitado un dispositivo que imita a uno real, normalmente un hardware físico muy conocido: una tarjeta Ethernet `ne2000`, por ejemplo, o un controlador IDE PIIX, o un puerto serie compatible con el venerable UART 16550. El modelo de dispositivo del hipervisor implementa la misma interfaz de registros que expondría el hardware real. El invitado carga el driver que habría cargado en hardware real, y ese driver se ejecuta sin modificaciones. El coste es el rendimiento: cada acceso a un registro en el invitado genera un trap hacia el hipervisor, que emula el comportamiento por software, lo cual es mucho más lento que el hardware real. Los dispositivos emulados son perfectos para la compatibilidad y para arrancar kernels de invitado sin modificar; resultan poco adecuados para cargas de trabajo exigentes.

**Los dispositivos paravirtuales** resuelven el problema de rendimiento sustituyendo la interfaz de compatibilidad por una diseñada para tener un coste bajo en ambos lados. En lugar de imitar hardware físico, el dispositivo define una nueva interfaz que resulta eficiente cuando se implementa en software. VirtIO es el ejemplo canónico y al que este capítulo dedicará la mayor parte de su tiempo. Un dispositivo VirtIO no se parece a ningún hardware real; se parece a un dispositivo VirtIO, que es una interfaz estandarizada que expone anillos de memoria compartida, unos pocos registros de control y poco más. El invitado debe disponer de un driver específico para VirtIO; ese driver es más pequeño y rápido que el driver equivalente para dispositivos emulados, porque la interfaz fue diseñada para software en ambos lados.

**Los dispositivos passthrough** van en la dirección opuesta. El hipervisor se aparta y entrega al invitado un dispositivo de hardware real: una NIC física, una GPU física, un SSD NVMe físico. El driver del invitado se comunica con ese hardware de forma más o menos directa. El hipervisor aún actúa de intermediario (a través de un IOMMU, por ejemplo, para limitar a qué memoria puede acceder el dispositivo) pero ya no emula ni paravirtualiza. El passthrough es rápido pero frágil: el dispositivo deja de compartirse con otros invitados, y migrar la VM a un host diferente deja de ser trivial.

Cada categoría de dispositivo impone un diseño distinto en el driver. Un driver para dispositivo emulado es habitualmente un driver antiguo y bien probado para el hardware emulado, que simplemente se ejecuta sobre una versión virtualizada del mismo hardware. Un driver paravirtual suele escribirse específicamente para la interfaz paravirtual, sin presuponer que el «hardware» exista en silicio real. Un driver passthrough es el mismo driver que habría funcionado en bare metal, con una sutileza: la memoria del invitado no es la misma memoria en la que el dispositivo hace DMA, y el mapeo del IOMMU debe ser correcto para que el dispositivo funcione.

El resto de este capítulo se centra con fuerza en la segunda categoría, porque la paravirtualización es donde se produce la mayor parte del trabajo de driver novedoso en entornos virtualizados, y porque ilustra un modelo de programación (anillos compartidos, bits de características, transacciones basadas en descriptores) que se generaliza a otras partes de FreeBSD.

### Los contenedores como entorno para drivers

Una jail es un tipo de entorno diferente, y plantea una pregunta distinta sobre un driver. El driver no se ejecuta dentro de la jail; se ejecuta en el único kernel compartido. Lo que hace la jail es cambiar lo que ven los procesos que hay en su interior.

Desde el kernel del host, nada cambia cuando se crea una jail. El kernel del host sigue teniendo todos los drivers que tenía antes; los drivers siguen asociados a los mismos dispositivos; las mismas entradas en `/dev/` siguen existiendo en el devfs montado en la raíz del host. Desde el interior de la jail, también se realiza un montaje de devfs, pero ese devfs se configura con un conjunto de reglas que oculta algunos dispositivos y expone otros. Un `/dev/mem` no suele ser visible dentro de una jail, porque un proceso dentro de una jail no debería poder inspeccionar la memoria del kernel; un `/dev/kvm` equivalente (si FreeBSD lo tuviera) quedaría igualmente oculto. Un `/dev/null` y un `/dev/zero` sí son visibles, porque no hay ninguna razón para ocultarlos.

¿Cuál es entonces el «problema del driver» en un contenedor? Básicamente, dos cosas. En primer lugar, cuando tu driver crea nodos de dispositivo, debes decidir si esos nodos deben ser visibles para los procesos dentro de una jail y, en caso afirmativo, en qué condiciones. Si tu driver expone un ioctl que permite a un proceso modificar la tabla de enrutamiento, ese ioctl no debería ser accesible para un proceso dentro de una jail que no tenga el privilegio para reconfigurar la red del host. En segundo lugar, cuando tu driver coopera con VNET (el framework de pila de red virtual de FreeBSD), debes tener cuidado con qué estado global es por VNET y cuál es compartido. Un `ifnet` movido a una jail VNET debe comportarse correctamente dentro de esa jail, lo que significa que tu driver debe haber declarado las variables con ámbito VNET correctas y no debe haber almacenado en caché ningún estado que cruce los límites de VNET.

Estas son preocupaciones diferentes a las de los drivers de VM, y merecen sus propias secciones. Las secciones 6 y 7 de este capítulo las trabajan en detalle.

### Por qué esto importa a los autores de drivers

Es legítimo preguntarse: ¿por qué debería importarle todo esto a un autor de drivers? Hasta hace poco, la mayoría de los drivers se escribían para hardware físico en bare metal, y la «historia de la virtualización» era algo que ocurría más arriba en la cadena. Hoy, y en el futuro previsible, la mayoría de las instalaciones de FreeBSD son máquinas virtuales. Un driver que solo funciona en bare metal es un driver que funciona en una minoría de los despliegues. Un driver que solo funciona en el host, y no dentro de una jail, es un driver que no puede usarse en ningún entorno moderno de FreeBSD con contenedores. El diseño de drivers consciente de la virtualización ya no es opcional; forma parte de escribir drivers sin más.

Hay tres razones concretas por las que el tema importa.

En primer lugar, **la mayoría de tus usuarios están dentro de un invitado**. Las VMs con FreeBSD que se ejecutan en nubes públicas, nubes privadas y laboratorios locales de `bhyve` superan en número a las instalaciones de FreeBSD en hardware real, quizás por un amplio margen. Un driver cuyo diseño falla en un entorno virtual fallará para la mayoría de sus usuarios.

En segundo lugar, **los drivers en el host exponen servicios a las jails**. Aunque tu driver sea para un dispositivo físico que reside en el host, en el momento en que una jail quiera usar sus servicios, el driver se enfrentará a la pregunta de la jail. Puede que necesite exponer un nodo de dispositivo a través del devfs de la jail, o decidir que ciertos ioctls quedan fuera del alcance de la jail.

En tercer lugar, **el rendimiento a escala importa más que nunca**. En un entorno virtualizado, la diferencia entre un driver emulado y uno paravirtual es a menudo un factor de diez o más en throughput. Saber cómo escribir y reconocer patrones paravirtuales vale el tiempo de ingeniería que requiere. Unos pocos párrafos más de comprensión sobre `virtqueue(9)` pueden ahorrarte un día entero de depuración más adelante.

### La virtualización no es simplemente hardware más lento

Un error habitual es pensar que la virtualización simplemente hace que el hardware sea más lento. Esa visión se pierde el mecanismo por completo. En un entorno virtualizado bien diseñado, el driver del invitado no ve «hardware más lento»; ve hardware diferente con patrones de acceso diferentes. Un driver VirtIO no realiza una lenta lectura de registro por descriptor para luego quejarse de la sobrecarga; agrupa una docena de descriptores en un anillo compartido, notifica al host una vez y espera una única confirmación. La diferencia entre un port ingenuo de un driver de hardware físico a VirtIO y un driver VirtIO idiomático no es una cuestión de ajuste fino; es una cuestión de intención arquitectónica.

De forma similar, la contenerización no es «procesos, pero confinados». Es una reorganización de la visibilidad, los privilegios y el estado global. Un driver que exporta un árbol de sysctl debe decidir si ese árbol debe ser visible dentro de una jail, y si las escrituras desde el interior de la jail deben afectar únicamente a la vista de la jail o a la del host. Estas no son solo preguntas de configuración; son preguntas de diseño que dan forma al código.

Si te llevas una sola idea de esta sección, que sea esta: **el entorno en el que se ejecuta tu driver no es un envoltorio transparente alrededor de una máquina**. Impone sus propias preocupaciones, ofrece sus propias APIs y premia o penaliza los diseños de driver que las respetan o las ignoran.

### Una taxonomía rápida

Para fijar el vocabulario en tu mente, aquí tienes una taxonomía compacta de los entornos que trataremos en este capítulo. Trátala como un mapa al que puedes volver cuando una sección posterior mencione un término.

| Entorno | Límite del kernel | Lo que ve el driver | Ejemplo típico en FreeBSD |
|---------|-------------------|---------------------|---------------------------|
| Bare metal | Completo | Hardware real | Cualquier driver |
| Dispositivo emulado en VM | Completo, en el kernel del invitado | Imitación de hardware real | `xn`, `em` emulados por QEMU |
| Dispositivo paravirtual en VM | Completo, en el kernel del invitado | Interfaz de anillo compartido | `virtio_blk`, `if_vtnet` |
| Dispositivo passthrough en VM | Completo, en el kernel del invitado | Hardware real con restricciones de IOMMU | `em` en una NIC passthrough |
| Jail (sin VNET) | Compartido con el host | Driver en el host; la jail ve el devfs del host filtrado por el conjunto de reglas | Visibilidad de `devfs` para `/dev/null` |
| Jail VNET | Compartido con el host | Driver en el host; la jail posee su propia pila de red; interfaces movidas con `if_vmove()` | `vnet.jail=1` en `jail.conf` |

Observa la columna del límite del kernel. El límite es la característica más importante de cada fila. Las VMs tienen un límite; las jails no. Todo lo demás se deriva de ahí.

### En resumen

Hemos trazado el primer mapa del capítulo. Las máquinas virtuales y los contenedores son dos familias distintas. Las VMs presentan hardware (emulado, paravirtual o passthrough) a un kernel de invitado completo. Los contenedores dividen un único kernel del host en entornos de proceso aislados, cambiando lo que ven y lo que pueden hacer, pero sin introducir un segundo kernel. Un autor de drivers trata cada familia de forma diferente: escribiendo drivers de invitado para VMs y adaptando drivers del host para jails. La siguiente sección toma el primero de esos dos mundos y examina los tres estilos de dispositivo con los que puede encontrarse un invitado, con suficiente detalle como para construir intuición antes de tocar VirtIO específicamente.

## Sección 2: Drivers de invitado, dispositivos emulados y dispositivos paravirtualizados

Un kernel de invitado ejecutándose dentro de una VM debe asociar drivers a los dispositivos que le ha proporcionado su hipervisor. Esos dispositivos pertenecen a los tres estilos presentados en la sección 1: emulados, paravirtuales y passthrough. Cada estilo produce un tipo diferente de driver, un tipo diferente de bug y un tipo diferente de oportunidad de optimización. Esta sección los examina en el orden en que es más probable que te los encuentres, y empieza a construir el vocabulario que necesitaremos en la sección 3 para VirtIO específicamente.

### Dispositivos emulados en detalle

Un dispositivo emulado es la historia más sencilla de contar. El hipervisor implementa, en software, una imitación fiel de un dispositivo físico conocido. Los ejemplos clásicos en FreeBSD incluyen la NIC Ethernet emulada `em(4)` (la familia Intel 8254x), el controlador SATA emulado `ahci(4)`, el puerto serie compatible con 16550 y el adaptador de vídeo VGA emulado.

Desde el punto de vista del invitado, no ocurre nada inusual. El enumerador de PCI encuentra una tarjeta Intel; el driver `em(4)` realiza probe y attach; el driver emite sus escrituras de registro habituales; los paquetes acaban apareciendo en la red. Por debajo, cada una de esas escrituras de registro es un trap hacia el hipervisor, que consulta su modelo del chip Intel, aplica el cambio de estado y posiblemente genera una interrupción virtual de vuelta en el invitado.

Este enfoque tiene una gran virtud y un notable inconveniente. La virtud es la compatibilidad: el kernel invitado no necesita ningún driver especial. Una imagen de instalación estándar de FreeBSD arranca sobre una tarjeta `em(4)` emulada con la misma facilidad que sobre una real. El inconveniente es el coste. Cada trap al hipervisor, cada emulación por software del comportamiento de un registro, cada interrupción sintetizada, consume ciclos de CPU. Para una carga de trabajo intensa con millones de accesos a registros por segundo, los dispositivos emulados son apreciablemente lentos.

### Lo que implican los dispositivos emulados para el código del driver

Desde la perspectiva del autor del driver, un dispositivo emulado tiene el mismo aspecto que el hardware real. Esto implica que normalmente no escribes un driver «especial» para hardware emulado; escribes un único driver que funciona con el silicio real y recurres a la emulación para el caso de compatibilidad. La mayoría de los drivers de dispositivos emulados que se encuentran en `/usr/src/sys/dev/` son, por tanto, los mismos archivos que controlan las tarjetas reales.

Hay dos pequeñas excepciones que nos interesan. En primer lugar, algunos drivers incluyen un probe breve al inicio del boot que distingue «esta es una tarjeta real» de «este es un hipervisor imitando una tarjeta». El driver `em(4)`, por ejemplo, registra un mensaje ligeramente diferente cuando detecta que el entorno está virtualizado, porque ciertos contadores de diagnóstico solo tienen sentido en el silicio real. En segundo lugar, algunos drivers omiten optimizaciones específicas del hardware en entornos virtualizados porque esas optimizaciones serían contraproducentes: hacer prefetch de bloques que la emulación del hipervisor ya tiene en RAM es un desperdicio, por ejemplo. Estos casos son poco frecuentes y generalmente se implementan como ajustes de rendimiento condicionales, no como cambios arquitectónicos.

En la práctica, la primera vez que te preocupa la diferencia entre emulado y real es cuando analizas un trace o un sysctl y te preguntas por qué un contador está a cero. Casi nunca es un problema de corrección.

### Por qué existe la emulación

Conviene detenerse a preguntar por qué existen los dispositivos emulados, dado que son más lentos que otras alternativas. La respuesta es triple.

En primer lugar, la emulación proporciona compatibilidad con los kernels de invitado existentes. Un hipervisor que solo ofreciera dispositivos paravirtuales necesitaría convencer a cada sistema operativo invitado de instalar drivers paravirtuales. Hoy es un problema resuelto, pero a principios de los 2000 (cuando se diseñaban los hipervisores modernos) era una barrera importante. Los dispositivos emulados permiten que los invitados funcionen sin modificaciones; el camino de alto rendimiento (paravirtual) llegó después.

En segundo lugar, la emulación suele ser suficientemente buena para cargas de trabajo de baja frecuencia. Un invitado que realiza una operación de E/S en disco por segundo no sufre de forma apreciable por la emulación. Uno que realiza cien mil operaciones de E/S en disco por segundo sí lo nota claramente. La mayoría de las cargas de trabajo se acercan más al primer caso que al segundo, y para ellas la emulación es una elección perfectamente válida.

En tercer lugar, la emulación es más fácil de implementar correctamente que la paravirtualización cuando el lado del invitado es un driver existente. Un autor de hipervisor que quiere dar soporte a imágenes de disco en formato VMware no tiene que escribir un driver de disco paravirtual para cada sistema operativo invitado; escribe un controlador de disco emulado compatible con VMware, y los drivers de invitado existentes de VMware funcionan sin cambios.

En `bhyve(8)` de FreeBSD, los dispositivos emulados incluyen el puente LPC (para puertos serie y el RTC), los controladores PCI-IDE y AHCI (para algunos casos de almacenamiento) y la NIC E1000 (a través del backend `e1000`). Se usan para instaladores y para la compatibilidad con invitados antiguos. Para trabajos orientados al rendimiento, los dispositivos VirtIO son la opción habitual.

### El passthrough desde la perspectiva del invitado

El passthrough merece un análisis más profundo desde la perspectiva del invitado, porque es el más parecido al hardware real de los tres estilos e introduce sutilezas que un autor novel de drivers podría no esperar.

Cuando se hace passthrough de un dispositivo PCI, el invitado ve una copia exacta de la configuración PCI del dispositivo: vendor ID, device ID, subsystem IDs, BARs, capabilities, tabla MSI-X. El enumerador PCI del invitado reclama el dispositivo con el mismo driver que habría utilizado en bare metal. El driver programa el dispositivo a través de accesos a registros; esos accesos son, en su mayor parte, directos (sin traps ni emulación), porque las extensiones de virtualización por hardware (Intel VT-x, AMD-V) permiten mapear la región MMIO de un dispositivo en el espacio de direcciones del invitado.

Donde entra la sutileza es en el DMA. El invitado programa direcciones físicas en los registros DMA del dispositivo, pero esas direcciones son físicas desde el punto de vista del invitado, no del host. Sin ayuda, el dispositivo haría DMA hacia direcciones físicas del host que no corresponden en absoluto a la memoria del invitado, lo que sería un desastre de seguridad. La ayuda viene de la IOMMU: Intel VT-d o AMD-Vi se sitúa entre el dispositivo y el bus de memoria del host, y reasigna las direcciones emitidas por el dispositivo a la memoria correcta del host.

Desde la perspectiva del driver del invitado, todo esto es invisible, siempre y cuando el driver use `bus_dma(9)` correctamente. El framework `bus_dma(9)` registra qué direcciones físicas son válidas para un handle de DMA determinado, y el kernel configura los mapeos de la IOMMU para que coincidan. Un driver que evita `bus_dma(9)` y programa direcciones físicas directamente (quizás llamando a `vtophys` sobre un puntero del kernel) es un driver que funcionará en bare metal pero fallará con passthrough.

Las interrupciones con passthrough se gestionan exclusivamente mediante MSI o MSI-X; las interrupciones legacy basadas en pin no pueden hacerse útilmente con passthrough. El driver del invitado configura MSI/MSI-X de la manera habitual, y el hipervisor configura el remapeo de interrupciones para que las interrupciones del dispositivo se entreguen al controlador de interrupciones virtual del invitado en lugar de al del host.

### Firmware y dependencias de plataforma con passthrough

Un área donde el passthrough puede sorprender a los autores de drivers es el firmware. Muchos dispositivos modernos esperan que su firmware sea cargado por el host, no por el propio dispositivo. Un dispositivo cuyo driver asume que el firmware ya ha sido cargado (quizás por una rutina BIOS en tiempo de boot del host) puede fallar al inicializarse dentro de un invitado, porque el BIOS del invitado no ha realizado el trabajo de carga del firmware.

La solución habitual es que el driver lleve consigo el firmware y lo cargue de forma explícita. El framework `firmware(9)` de FreeBSD lo facilita: el driver registra la imagen del firmware (típicamente un blob compilado en un módulo cargable), y en el momento del attach llama a `firmware_get` y sube el blob al dispositivo. Un driver que funciona tanto en bare metal como con passthrough es uno que realiza la carga del firmware él mismo, en lugar de depender del código de plataforma.

Del mismo modo, las tablas ACPI y otras tablas de plataforma pueden ser diferentes entre el host y un invitado. Un driver que lee una tabla ACPI para determinar el enrutamiento específico de la placa (por ejemplo, para identificar qué GPIO controla un raíl de alimentación) encontrará una tabla diferente en un invitado, porque el BIOS virtual del invitado genera la suya propia. El driver debe proporcionar valores predeterminados para los casos en que falte la tabla, o bien tratar la tabla ausente como una plataforma no soportada.

### Cuándo preferir cada estilo de dispositivo

Los tres estilos de dispositivo no son mutuamente excluyentes en una misma VM. Un invitado `bhyve(8)` puede tener un puente LPC emulado, un dispositivo de bloque VirtIO, una NIC con passthrough y una consola VirtIO, todo al mismo tiempo. El administrador elige qué estilo usar para cada rol en función de las ventajas e inconvenientes.

Para instalaciones e invitados legacy, los dispositivos emulados son la elección correcta. No requieren nada del invitado y funcionan de inmediato.

Para invitados FreeBSD típicos que realizan trabajo ordinario, los dispositivos VirtIO son la elección correcta. Son rápidos, están estandarizados y cuentan con buen soporte en el árbol de FreeBSD. La mayoría de los despliegues de `bhyve(8)` en producción usan VirtIO para disco, red y consola.

Para cargas de trabajo críticas en rendimiento o específicas del hardware, el passthrough es la elección correcta. Un invitado que ejecuta una carga de trabajo acelerada por GPU necesita passthrough de la GPU; uno que necesita las características reales de offload de la tarjeta de red necesita passthrough de la NIC.

Como autor de drivers, el estilo de dispositivo para el que es más probable que escribas es VirtIO, porque ahí es donde se concentra el nuevo trabajo de drivers. Los drivers emulados y de passthrough suelen ser drivers existentes; los drivers VirtIO se diseñan desde cero para la interfaz paravirtual.

### Los dispositivos paravirtuales en detalle

Los dispositivos paravirtuales representan un cambio más profundo. El hipervisor se niega a imitar ningún dispositivo físico y en su lugar define una interfaz optimizada para el software en ambos lados. El invitado debe tener un driver escrito para esa interfaz; un driver legacy estándar no funcionará. La ventaja es el rendimiento: una interfaz paravirtual puede agrupar, amortizar y simplificar la interacción host-invitado de maneras que un modelo de hardware realista no puede.

La familia paravirtual dominante en FreeBSD es VirtIO. VirtIO no es una invención de FreeBSD; es un estándar multiplataforma diseñado para su uso en Linux, FreeBSD y otros sistemas operativos invitados. La implementación de VirtIO en FreeBSD reside en `/usr/src/sys/dev/virtio/`, con drivers individuales para dispositivos de bloque (`virtio_blk`), dispositivos de red (`if_vtnet`), consolas serie (`virtio_console`), SCSI (`virtio_scsi`), entropía (`virtio_random`) y memory ballooning (`virtio_balloon`). Existen otros tipos de dispositivos VirtIO (sistemas de archivos 9p, dispositivos de entrada, sockets), y también cuentan con drivers en FreeBSD en distintos grados de madurez.

Desde el exterior, un dispositivo VirtIO parece un dispositivo PCI (cuando se conecta mediante PCI) o un dispositivo mapeado en memoria (cuando se conecta mediante MMIO, lo cual es habitual en sistemas embebidos y ARM). El kernel del invitado lo enumera de la misma manera que cualquier otro dispositivo: un escaneo del bus PCI encuentra un dispositivo con vendor ID 0x1af4 y un device ID que indica el tipo, o el device tree del invitado anuncia un transporte VirtIO MMIO con una cadena compatible conocida.

Una vez detectado y asociado, el driver se comunica con el dispositivo a través de un pequeño conjunto de primitivas: bits de características del dispositivo, estado del dispositivo, espacio de configuración del dispositivo y un conjunto de **virtqueues**. Las virtqueues son el corazón del modelo VirtIO. Cada una es un anillo de descriptores en memoria compartida, escribible por el invitado y legible por el host; el host tiene un anillo paralelo de descriptores usados que el invitado lee para descubrir las finalizaciones. Toda la E/S real ocurre a través de los anillos; los registros que el driver toca se usan casi exclusivamente para la configuración, la notificación y la negociación de características.

Este diseño es radicalmente diferente al de un driver de dispositivo emulado. Un driver VirtIO casi nunca emite una escritura en un registro durante la operación normal. Coloca buffers en los anillos, notifica al host de que hay nuevo trabajo disponible, y lee las finalizaciones del anillo cuando llegan. El overhead por petición es un puñado de accesos a memoria, no un trap de registro. Cuando el sistema está ocupado, el driver puede colocar muchas peticiones y notificar una sola vez; el host puede agrupar muchas finalizaciones y señalizar una sola vez. El resultado es una mejora de rendimiento de un orden de magnitud respecto a un driver emulado equivalente.

### Los dispositivos paravirtuales y el autor del driver

Para un autor de drivers, los dispositivos paravirtuales son donde se concentra gran parte del trabajo nuevo en el ecosistema de drivers de FreeBSD. VirtIO está bien definido y es estable, y escribir un nuevo tipo de dispositivo respaldado por VirtIO es un proyecto razonable para un fin de semana una vez que hayas leído los drivers fundamentales. Más importante aún, los patrones de VirtIO (anillos compartidos, negociación de características, transacciones basadas en descriptores) se repiten en otros subsistemas de FreeBSD (especialmente `if_netmap` y partes del stack de red), por lo que el tiempo invertido en comprenderlos resulta muy rentable.

Este capítulo dedicará toda la Sección 3 a los fundamentos de VirtIO. Por ahora, lo que importa es el marco conceptual: un driver paravirtual es un driver de invitado de primera clase que fue diseñado para vivir dentro de una VM, con las características de rendimiento que ese diseño proporciona.

### Los dispositivos passthrough en detalle

El passthrough es el tercer estilo y, en ciertos aspectos, el más interesante, porque colapsa la cuestión de la virtualización de vuelta a la cuestión bare metal, con un giro.

En el passthrough, el hipervisor se niega a abstraer el dispositivo en absoluto. Le entrega al invitado un dispositivo PCI o PCIe real: por ejemplo, una NIC física concreta en una de las ranuras del host, un SSD NVMe concreto, o una GPU concreta. El driver del invitado habla con ese hardware real como si fuera el host. El invitado incluso recibe las interrupciones del dispositivo, a través de un mecanismo que proporciona el hipervisor.

Hay tres razones por las que existe el passthrough. La primera es el rendimiento: una NIC con passthrough entrega tráfico a tasa de línea al invitado sin ninguna sobrecarga de software por paquete. La segunda es el acceso a características de hardware que el hipervisor no emula: aceleración GPU, colas específicas de NVMe y motores de criptografía por hardware. La tercera son las licencias o certificaciones: algunos drivers propietarios solo funcionan con hardware real y no pueden usarse a través de ninguna capa de emulación.

El coste del passthrough es el aislamiento. Una vez que un dispositivo se asigna mediante passthrough a un invitado, el host ya no lo posee en ningún sentido útil; el propio host no puede usar la NIC, y otros invitados no pueden compartirla. La migración en vivo se vuelve difícil o imposible, porque el host de destino puede no tener el mismo dispositivo físico en la misma ranura. El invitado posee el hardware hasta que deja de ejecutarse.

### Passthrough y el IOMMU

El passthrough parece sencillo en principio, pero requiere un componente de hardware fundamental: un IOMMU. Un IOMMU es a DMA lo que una MMU es al acceso de la CPU a la memoria: una tabla de traducción aplicada por el hardware entre la vista de la memoria que tiene un dispositivo (su «dirección DMA») y la memoria física real. Sin un IOMMU, un dispositivo en passthrough podría realizar DMA en cualquier región de la memoria física del host, incluidos los datos del kernel del host, con consecuencias obvias y aterradoras. Con un IOMMU, el hipervisor puede restringir el dispositivo para que solo realice DMA en las regiones de memoria asignadas al guest, preservando así el límite de seguridad.

En sistemas amd64, el IOMMU es Intel VT-d (en CPUs Intel) o AMD-Vi (en CPUs AMD). Normalmente se activa mediante una opción de configuración del kernel y algunos ajustes de la BIOS. El `bhyve(8)` de FreeBSD utiliza el IOMMU a través del mecanismo `pci_passthru(4)`, que veremos en la Sección 5.

Para el autor del driver, el IOMMU es invisible la mayor parte del tiempo. La API `bus_dma(9)` en el guest se comporta de la misma manera que lo hace en hardware real; las llamadas `bus_dmamem_alloc()` y `bus_dmamap_load()` del driver del guest producen direcciones DMA que, desde el punto de vista del guest, tienen el mismo aspecto que tendrían en hardware real. La traducción del IOMMU ocurre una capa por debajo, entre el dispositivo y la memoria real, y el driver del guest no participa en ella.

El único lugar donde el IOMMU importa para el autor del driver es cuando está mal configurado o ausente, y el DMA empieza a fallar de formas misteriosas. Si alguna vez ves mensajes «DMA timeout» procedentes de un driver en passthrough, el IOMMU suele ser el primer sospechoso. El Capítulo 32 profundizará en esto.

### El modelo mental del autor del driver

Si te alejas de estos tres estilos, emerge un patrón claro. El kernel del guest siempre carga un driver; el driver siempre ve un dispositivo; el comportamiento del dispositivo siempre está definido por alguna interfaz. Lo que cambia entre los estilos es qué interfaz y quién la implementa.

En los dispositivos emulados, la interfaz es el modelo de registros del hardware real, y el hipervisor lo implementa en software. El driver es el driver de hardware real.

En los dispositivos paravirtuales, la interfaz es un modelo específico optimizado por software, y el hipervisor lo implementa de forma nativa. El driver es específico para paravirtualización.

En los dispositivos en passthrough, la interfaz es el modelo de registros del hardware real, y el propio hardware lo implementa, con el IOMMU como guardián. El driver es el driver de hardware real, ejecutándose en el guest, con las direcciones DMA traducidas de forma transparente.

Por tanto, el autor del driver trabaja con las mismas habilidades en los tres casos. La forma del trabajo solo difiere en qué interfaz está en juego. Una vez que entiendes eso, el resto de este capítulo se convierte en un estudio de interfaces concretas, APIs concretas de FreeBSD y contextos de despliegue concretos.

### Una nota sobre los modelos híbridos

Los sistemas reales suelen combinar estos estilos. Un mismo guest puede tener un puerto serie emulado para el registro durante el arranque temprano, una NIC paravirtual para el tráfico de red principal, un dispositivo de bloques paravirtual para el sistema de archivos raíz y una GPU en passthrough para aceleración. Cada uno de esos dispositivos está gobernado por su propio driver, y el conjunto de herramientas del driver del guest necesita conocer los patrones de los tres estilos. Esto no es inusual; es el caso habitual en cualquier VM no trivial.

Para el autor del driver, la implicación es que generalmente no necesitas construir un driver universal para los tres estilos. Construyes el driver para el estilo que utiliza tu dispositivo, y coexistes con drivers para los otros estilos. Las decisiones por dispositivo sobre qué estilo usar las toma el administrador del hipervisor, no el driver.

### En resumen

Tres estilos de dispositivo de guest, tres tipos de trabajo de driver, un único modelo mental subyacente: un driver habla con una interfaz, y la interfaz la implementa la capa que el hipervisor elija. El emulado es fácil pero lento; el paravirtual es rápido y requiere drivers específicos; el passthrough es casi nativo y requiere un IOMMU. El resto de este capítulo se centra en el estilo paravirtual, con VirtIO como ejemplo, porque es ahí donde ocurre la mayor parte del trabajo nuevo e interesante en drivers de FreeBSD. En la Sección 3 abrimos el conjunto de herramientas de VirtIO y comenzamos a examinar sus partes.

## Sección 3: Fundamentos de VirtIO y `virtqueue(9)`

VirtIO es un estándar. Define una manera en que un guest puede comunicarse con un dispositivo que solo existe en software, sin referencia a ningún hardware físico. El estándar está mantenido por el OASIS VirtIO Technical Committee y lo implementan varios hipervisores, entre ellos `bhyve(8)`, QEMU y la familia Linux KVM. Dado que el estándar es compartido, un driver de guest VirtIO escrito para un guest FreeBSD podrá comunicarse con cualquier dispositivo VirtIO presentado por cualquier hipervisor que cumpla el estándar.

Esta sección presenta el modelo VirtIO con el nivel de detalle que necesita un autor de drivers. Es extensa, porque VirtIO tiene suficientes partes móviles que justifican el espacio, y breve comparada con la propia especificación de VirtIO, que abarca cientos de páginas. Nos centraremos en las partes que necesitas para escribir un driver de guest para FreeBSD.

### Los ingredientes de un dispositivo VirtIO

Un dispositivo VirtIO expone un pequeño número de elementos a su driver de guest. Comprender cada uno de ellos es la base de todo lo que viene a continuación.

El primer elemento es el **transporte**. Los dispositivos VirtIO pueden aparecer a través de varios transportes: VirtIO sobre PCI (el caso habitual en escritorios, servidores y cloud amd64), VirtIO sobre MMIO (el caso habitual en sistemas ARM e integrados) y VirtIO sobre channel I/O (utilizado en mainframes IBM). Desde el punto de vista del guest, el transporte dicta cómo se enumera el dispositivo y cómo se leen los registros, pero una vez que el driver tiene un `device_t`, el transporte pasa a un segundo plano. El framework VirtIO de FreeBSD proporciona una API independiente del transporte.

El segundo elemento es el **tipo de dispositivo**. Cada dispositivo VirtIO tiene un identificador de tipo de un byte (`VIRTIO_ID_NETWORK` es 1, `VIRTIO_ID_BLOCK` es 2, `VIRTIO_ID_CONSOLE` es 3, `VIRTIO_ID_ENTROPY` es 4, `VIRTIO_ID_BALLOON` es 5, `VIRTIO_ID_SCSI` es 8, y así sucesivamente, tal como se lista en `/usr/src/sys/dev/virtio/virtio_ids.h`). El tipo le indica al guest qué hace el dispositivo; el guest despacha al driver correcto en función del tipo.

El tercer elemento son los **bits de características del dispositivo**. Cada tipo de dispositivo define un conjunto de características opcionales, cada una representada por un bit en una máscara de 64 bits. Algunas características son universales (la capacidad de usar descriptores indirectos, por ejemplo) y otras son específicas del dispositivo (los dispositivos de bloques VirtIO pueden anunciar write-caching, soporte para discard, información de geometría y más). El driver del guest lee las características anunciadas por el dispositivo, selecciona cuáles sabe utilizar y escribe de vuelta una máscara de características negociada. El dispositivo acepta entonces el conjunto negociado y rechaza cualquier intento de utilizar una característica fuera de él. Esta negociación es el mecanismo mediante el cual los dispositivos y drivers VirtIO evolucionan sin romperse mutuamente.

El cuarto elemento es el **estado del dispositivo**. Un pequeño registro a nivel de byte informa en qué punto del ciclo de vida se encuentra el dispositivo: reconocido por el driver, driver encontrado para este tipo de dispositivo, características negociadas, dispositivo configurado y listo, y así sucesivamente. Escribir en el registro de estado lleva al dispositivo a través de su ciclo de vida; leerlo le indica al driver en qué punto se encuentra.

El quinto elemento es el **espacio de configuración del dispositivo**. Cada tipo de dispositivo tiene su propio pequeño esquema de bytes que contiene la configuración específica del dispositivo: la capacidad de un dispositivo de bloques, la dirección MAC de un dispositivo de red, el número de puertos de un dispositivo de consola. El guest lee el espacio de configuración para conocer los detalles específicos del dispositivo y ocasionalmente escribe en él para solicitar un cambio de configuración.

El sexto elemento, y el más importante para este capítulo, es el **conjunto de virtqueues**. Cada dispositivo VirtIO tiene una o más virtqueues, y las virtqueues son donde ocurre casi todo el trabajo real. Piensa en cada virtqueue como una cinta transportadora bidireccional de tamaño limitado entre el guest y el host. El guest coloca peticiones en la cinta; el host las consume, actúa sobre ellas y coloca las completaciones en una cinta de retorno paralela; el guest lee las completaciones para saber qué ocurrió. La cinta transportadora se implementa como un anillo de descriptores en memoria compartida.

### Las virtqueues y el modelo de anillo compartido

En el corazón de VirtIO se encuentra la virtqueue, la abstracción más importante de todo el protocolo. Una virtqueue es un anillo de descriptores, almacenado en memoria que tanto el guest como el host pueden leer y escribir. Su tamaño es una potencia de dos, elegida en la inicialización del dispositivo; los valores típicos son 128 o 256 entradas, aunque se permiten valores mayores.

Cada descriptor del anillo describe un único buffer scatter-gather en la memoria del guest: su dirección física del guest, su longitud y un par de flags (si este buffer es legible o escribible desde el punto de vista del dispositivo, si este descriptor encadena a un descriptor siguiente, si apunta a una tabla de descriptores indirectos). El driver rellena descriptores, los encadena según sea necesario para representar transacciones de múltiples buffers y escribe en un pequeño índice para anunciar que hay nuevos descriptores disponibles. La implementación del dispositivo en el host recorre los descriptores, realiza el trabajo que describen y escribe en su propio índice para anunciar que el trabajo está terminado.

Dentro de una virtqueue hay tres anillos, no uno. La **tabla de descriptores** contiene los descriptores reales. El **anillo de disponibles** (controlado por el guest) lista los índices de las cadenas de descriptores que el driver ha puesto a disposición. El **anillo de usados** (controlado por el host) lista los índices de las cadenas de descriptores que el dispositivo ha consumido y cuyos resultados están listos. La división es sutil pero importante: el driver escribe en el anillo de disponibles y lee del anillo de usados, mientras que el host hace lo contrario. Cada lado lee lo que el otro lado escribió, sin locks, mediante el uso cuidadoso de barreras de memoria y actualizaciones atómicas de índices.

Desde el punto de vista del autor del driver, casi nunca manipulas los anillos directamente. La API `virtqueue(9)` de FreeBSD los abstrae tras un puñado de funciones. Llamas a `virtqueue_enqueue()` para insertar una lista scatter-gather en el anillo de disponibles. Llamas a `virtqueue_notify()` para que el host sepa que hay nuevo trabajo disponible. Llamas a `virtqueue_dequeue()` para extraer la siguiente completación del anillo de usados. Si estás usando polling en lugar de interrupciones, llamas a `virtqueue_poll()` para esperar la siguiente completación. La API oculta la aritmética de índices, las barreras de memoria y la gestión de flags; tu código trabaja con listas scatter-gather y cookies.

El mecanismo de cookies merece una mención aparte. Cuando insertas una lista scatter-gather en la cola, proporcionas un puntero opaco, la cookie, que la API recuerda. Cuando extraes una completación de la cola, recibes esa cookie de vuelta. Esto permite al driver asociar cada completación con la petición que la generó, sin que el anillo tenga que llevar ningún contexto específico del driver. Un driver típicamente pasa el puntero a una estructura de petición a nivel de softc como cookie; el dequeue devuelve el mismo puntero y el driver reanuda el procesamiento.

### La negociación de características en la práctica

Antes de que el driver utilice cualquiera de estos mecanismos, debe negociar las características. La secuencia es sencilla y tiene el mismo aspecto en todos los drivers VirtIO.

En primer lugar, el driver lee los bits de características anunciados por el dispositivo con `virtio_negotiate_features()`. El argumento es una máscara de características que el driver está preparado para utilizar; el valor de retorno es la intersección de la máscara del driver y los bits anunciados por el dispositivo. El dispositivo se ha comprometido ahora a admitir solo ese subconjunto, y el driver sabe exactamente qué características están en juego.

En segundo lugar, el driver llama a `virtio_finalize_features()` para cerrar la negociación. Tras esta llamada, el dispositivo rechazará cualquier intento de habilitar características fuera del conjunto negociado. El código de configuración posterior del driver puede inspeccionar la máscara negociada a través de `virtio_with_feature()`, que devuelve true si un bit de característica determinado está presente.

En tercer lugar, el driver asigna virtqueues. El número exacto y los parámetros por cola dependen del tipo de dispositivo. Un dispositivo `virtio_random` asigna una virtqueue; un dispositivo `virtio_net` asigna al menos dos (una para recepción y otra para transmisión), y más si se negocian múltiples colas. La asignación se realiza mediante `virtio_alloc_virtqueues()`, pasando un array de `struct vq_alloc_info` donde cada entrada describe el nombre de una cola, su callback y el tamaño máximo de descriptor indirecto.

En cuarto lugar, el driver configura las interrupciones con `virtio_setup_intr()`. Los manejadores de interrupción son los callbacks del paso tres. Cuando el host ha publicado una finalización en una cola, el guest recibe una interrupción virtual, el manejador se ejecuta y procesa las finalizaciones llamando a `virtqueue_dequeue()` en un bucle.

Esta secuencia de cuatro pasos es la columna vertebral de todo driver VirtIO en `/usr/src/sys/dev/virtio/`. Lee `vtrnd_attach()` en `/usr/src/sys/dev/virtio/random/virtio_random.c` y lo verás con claridad: negociación de características, asignación de colas y configuración de interrupciones, exactamente en ese orden. Lee `vtblk_attach()` en `/usr/src/sys/dev/virtio/block/virtio_blk.c` y verás la misma secuencia, con más piezas en movimiento porque el dispositivo es más complejo, pero con el mismo esqueleto.

### Un recorrido por `virtio_random`

Dado que `virtio_random` es el driver VirtIO completo más pequeño del árbol de FreeBSD, es el mejor para leer en primer lugar. El archivo completo tiene menos de cuatrocientas líneas, y cada una está ahí por una razón. Vamos a recorrer las partes más importantes.

El softc es pequeño:

```c
struct vtrnd_softc {
    device_t          vtrnd_dev;
    uint64_t          vtrnd_features;
    struct virtqueue *vtrnd_vq;
    eventhandler_tag  eh;
    bool              inactive;
    struct sglist    *vtrnd_sg;
    uint32_t         *vtrnd_value;
};
```

Los campos son el handle del dispositivo, la máscara de características negociadas (el dispositivo no tiene bits de característica, así que siempre será cero), un puntero a la única virtqueue, una etiqueta de manejador de eventos para los hooks de apagado, un flag que se establece a true durante el desmontaje, una lista scatter-gather usada en cada encolar y un buffer en el que el dispositivo almacenará cada lote de entropía.

La tabla de métodos del dispositivo es el esqueleto estándar de newbus:

```c
static device_method_t vtrnd_methods[] = {
    DEVMETHOD(device_probe,    vtrnd_probe),
    DEVMETHOD(device_attach,   vtrnd_attach),
    DEVMETHOD(device_detach,   vtrnd_detach),
    DEVMETHOD(device_shutdown, vtrnd_shutdown),
    DEVMETHOD_END
};

static driver_t vtrnd_driver = {
    "vtrnd",
    vtrnd_methods,
    sizeof(struct vtrnd_softc)
};

VIRTIO_DRIVER_MODULE(virtio_random, vtrnd_driver, vtrnd_modevent, NULL);
MODULE_VERSION(virtio_random, 1);
MODULE_DEPEND(virtio_random, virtio, 1, 1, 1);
MODULE_DEPEND(virtio_random, random_device, 1, 1, 1);
```

`VIRTIO_DRIVER_MODULE` es una macro corta que se expande en dos declaraciones `DRIVER_MODULE`, una para `virtio_pci` y otra para `virtio_mmio`. Así es como el mismo driver se enlaza en ambos transportes sin necesidad de código específico de transporte propio; el framework lo enruta al transporte que encuentre un dispositivo coincidente.

La función `probe` es de una sola línea:

```c
static int
vtrnd_probe(device_t dev)
{
    return (VIRTIO_SIMPLE_PROBE(dev, virtio_random));
}
```

`VIRTIO_SIMPLE_PROBE` consulta la tabla de coincidencias PNP que se declaró anteriormente mediante `VIRTIO_SIMPLE_PNPINFO`. La tabla de coincidencias le indica al kernel que este driver quiere dispositivos cuyo tipo VirtIO sea `VIRTIO_ID_ENTROPY`.

La función `attach` es más extensa. Asigna el buffer de entropía y la lista scatter-gather, configura las características, asigna una única virtqueue, instala un manejador de eventos para el apagado, se registra en el framework `random(4)` de FreeBSD y publica su primer buffer en la virtqueue:

```c
sc = device_get_softc(dev);
sc->vtrnd_dev = dev;
virtio_set_feature_desc(dev, vtrnd_feature_desc);

len = sizeof(*sc->vtrnd_value) * HARVESTSIZE;
sc->vtrnd_value = malloc_aligned(len, len, M_DEVBUF, M_WAITOK);
sc->vtrnd_sg = sglist_build(sc->vtrnd_value, len, M_WAITOK);

error = vtrnd_setup_features(sc);      /* feature negotiation */
error = vtrnd_alloc_virtqueue(sc);     /* allocate queue */

/* [atomic global-instance check] */

sc->eh = EVENTHANDLER_REGISTER(shutdown_post_sync,
    vtrnd_shutdown, dev, SHUTDOWN_PRI_LAST + 1);

sc->inactive = false;
random_source_register(&random_vtrnd);

vtrnd_enqueue(sc);
```

La negociación de características es trivial porque el driver anuncia `VTRND_FEATURES = 0`:

```c
static int
vtrnd_negotiate_features(struct vtrnd_softc *sc)
{
    device_t dev = sc->vtrnd_dev;
    uint64_t features = VTRND_FEATURES;

    sc->vtrnd_features = virtio_negotiate_features(dev, features);
    return (virtio_finalize_features(dev));
}
```

La máscara de características del dispositivo es irrelevante; el driver no quiere ninguna característica y no obtiene ninguna, y la finalización tiene éxito de forma trivial. Un driver más complejo construiría una máscara distinta de cero y la comprobaría posteriormente.

La asignación de la virtqueue solicita una única cola:

```c
static int
vtrnd_alloc_virtqueue(struct vtrnd_softc *sc)
{
    device_t dev = sc->vtrnd_dev;
    struct vq_alloc_info vq_info;

    VQ_ALLOC_INFO_INIT(&vq_info, 0, NULL, sc, &sc->vtrnd_vq,
        "%s request", device_get_nameunit(dev));

    return (virtio_alloc_virtqueues(dev, 0, 1, &vq_info));
}
```

El primer argumento de `VQ_ALLOC_INFO_INIT` es el tamaño máximo de la tabla de descriptores indirectos, que es cero para este driver porque no utiliza descriptores indirectos. El segundo argumento es el callback del manejador de interrupciones, que es `NULL` porque este driver utiliza polling en lugar de finalización dirigida por interrupciones. El tercero es el argumento de ese manejador. El cuarto es el puntero de salida para el handle de la virtqueue. El quinto es una cadena de formato que genera el nombre de la cola.

La función de encolar es breve:

```c
static void
vtrnd_enqueue(struct vtrnd_softc *sc)
{
    struct virtqueue *vq = sc->vtrnd_vq;

    KASSERT(virtqueue_empty(vq), ("%s: non-empty queue", __func__));

    error = virtqueue_enqueue(vq, sc, sc->vtrnd_sg, 0, 1);
    KASSERT(error == 0, ("%s: virtqueue_enqueue returned error: %d",
        __func__, error));

    virtqueue_notify(vq);
}
```

Introduce en el anillo una lista scatter-gather que describe el buffer de entropía. El cookie es el propio `sc`. Los dos argumentos siguientes son «segmentos legibles» (ninguno, pues el dispositivo escribe el buffer) y «segmentos escribibles» (uno, el buffer de entropía). `virtqueue_notify()` notifica al host para que empiece a rellenar el buffer.

Como no hay interrupciones, el driver utiliza polling para recuperar las finalizaciones:

```c
static int
vtrnd_harvest(struct vtrnd_softc *sc, void *buf, size_t *sz)
{
    struct virtqueue *vq = sc->vtrnd_vq;
    void *cookie;
    uint32_t rdlen;

    if (sc->inactive)
        return (EDEADLK);

    cookie = virtqueue_dequeue(vq, &rdlen);
    if (cookie == NULL)
        return (EAGAIN);

    *sz = MIN(rdlen, *sz);
    memcpy(buf, sc->vtrnd_value, *sz);

    vtrnd_enqueue(sc);   /* re-post the buffer */
    return (0);
}
```

El ciclo de vida aquí es el ciclo VirtIO completo. La función de encolar publica un buffer; el host lo rellena con entropía y marca el descriptor como «used»; el driver desencola el descriptor usado, extrae el cookie y la longitud, copia el resultado al llamante y vuelve a publicar el buffer para la siguiente ronda. Si la cola está vacía (sin finalización todavía), `virtqueue_dequeue()` devuelve `NULL` y el driver devuelve `EAGAIN`.

Esta es la forma completa de un driver VirtIO. Todo lo demás, desde `virtio_blk` hasta `if_vtnet`, es una elaboración sobre esta base: más colas, más características, más trabajo por finalización, más gestión interna por petición. El esqueleto es el mismo.

### Lo que te ofrece la API `virtqueue(9)`

Da un paso atrás un momento y considera cuánta complejidad abstrae la API. La aritmética del anillo, con su envoltura circular y su cuidadoso manejo de los índices de disponibles frente a usados, está completamente oculta. Las barreras de memoria que mantienen sincronizados al guest y al host, lo bastante sutiles como para ser una fuente habitual de bugs en código de anillo escrito a mano, las coloca la propia API. Las características opcionales (descriptores indirectos, índices de eventos) se activan y desactivan según la negociación sin que el driver tenga que saber cómo funcionan.

El coste de esa abstracción es que un autor de drivers que solo use la API no comprenderá los anillos en profundidad. Normalmente eso no supone ningún problema, pero en la ocasión poco frecuente en que depures un desajuste en la capa del anillo (porque publicaste un descriptor con un recuento escribible incorrecto, por ejemplo, y el host lo está rechazando) querrás saber qué está ocurriendo. La especificación VirtIO es la referencia para el diseño subyacente del anillo, y `/usr/src/sys/dev/virtio/virtio_ring.h` es el lado FreeBSD de ese mismo diseño. Merece la pena leerlos al menos una vez.

### Descriptores indirectos

Una pequeña optimización que conviene conocer es la característica de descriptores indirectos (`VIRTIO_RING_F_INDIRECT_DESC`). Cuando se negocia, permite que un driver describa una transacción multibuffer en una tabla de descriptores indirectos en lugar de una cadena de descriptores en el anillo principal. La entrada del anillo se convierte en un único descriptor que apunta a un bloque de descriptores, de modo que una lista scatter-gather grande consume solo un slot en el anillo en lugar de muchos.

Los descriptores indirectos son importantes en cargas de trabajo con transacciones grandes e intensivas en scatter-gather, como los drivers de red que envían paquetes con muchos fragmentos. Para drivers pequeños, son un adorno opcional. La API `virtqueue(9)` gestiona el mecanismo de forma transparente: si pasas un valor distinto de cero en `vqai_maxindirsz` durante la asignación de la cola, el kernel puede usar indirectos; si pasas cero, no puede.

### Una mirada más profunda al diseño del anillo

Para el lector que quiera saber qué oculta `virtqueue(9)`, aquí tienes un breve recorrido por el diseño subyacente. Los detalles solo importan cuando depuras un problema en la capa del anillo; puedes saltarte esta subsección en la primera lectura.

Cada virtqueue tiene tres estructuras principales en memoria, asignadas de forma contigua (con requisitos de alineación que difieren ligeramente entre VirtIO legado y moderno).

La **tabla de descriptores** es un array de `struct vring_desc`, una entrada por slot de descriptor:

```c
struct vring_desc {
    uint64_t addr;     /* guest physical address of the buffer */
    uint32_t len;      /* length of the buffer */
    uint16_t flags;    /* VRING_DESC_F_NEXT, _WRITE, _INDIRECT */
    uint16_t next;     /* index of the next descriptor if chained */
};
```

La tabla de descriptores tiene tantas entradas como el tamaño de la cola (típicamente 128 o 256). Los descriptores no utilizados se enlazan en una lista libre mantenida por el driver; los descriptores usados forman cadenas que describen transacciones individuales.

El **anillo de disponibles** es lo que el driver escribe y el dispositivo lee:

```c
struct vring_avail {
    uint16_t flags;
    uint16_t idx;                    /* producer index, monotonic */
    uint16_t ring[QUEUE_SIZE];       /* head-of-chain indices */
    uint16_t used_event;             /* for VRING_F_EVENT_IDX */
};
```

Cuando el driver encola una nueva transacción, elige una cadena de descriptores de la lista libre, los rellena, coloca el índice del descriptor cabecera en `ring[idx % QUEUE_SIZE]` y luego incrementa `idx`. El incremento usa una barrera de memoria de tipo release para que el dispositivo vea la nueva entrada de `ring` antes de ver el nuevo `idx`.

El **anillo de usados** es lo que el dispositivo escribe y el driver lee:

```c
struct vring_used_elem {
    uint32_t id;        /* index of the head descriptor */
    uint32_t len;       /* number of bytes written */
};

struct vring_used {
    uint16_t flags;
    uint16_t idx;                                  /* producer index */
    struct vring_used_elem ring[QUEUE_SIZE];
    uint16_t avail_event;                          /* for VRING_F_EVENT_IDX */
};
```

Cuando el dispositivo finaliza una transacción, escribe una entrada en `ring[idx % QUEUE_SIZE]` con el índice cabecera y el recuento de bytes, y luego incrementa `idx`. El driver observa el incremento y desencola en consecuencia.

La sutileza del formato está en la sincronización. El guest y el host no se sincronizan mediante locks (no los comparten); se sincronizan mediante escrituras ordenadas y actualizaciones atómicas de índices. La especificación es cuidadosa al definir las barreras de memoria en cada lado, y la implementación de `virtqueue(9)` las coloca correctamente. Colocar las barreras incorrectamente es un bug habitual en implementaciones de anillos escritas a mano; esa es una razón para usar el framework.

### Índices de eventos: un detalle de rendimiento

La característica `VIRTIO_F_RING_EVENT_IDX` permite que ambos lados supriman notificaciones cuando no son necesarias. El mecanismo consiste en dos campos adicionales: `used_event` en el anillo de disponibles y `avail_event` en el anillo de usados. Cada lado escribe un valor que indica «interrúmpeme solo cuando el índice productor del otro lado supere este valor».

El efecto práctico es que un driver que produce peticiones a alta velocidad no provoca que el host sea interrumpido en cada encolar; en cambio, el host lee el `used_event` actual y solo entrega una notificación cuando el índice productor alcanza ese valor. Del mismo modo, un dispositivo que completa transacciones a alta velocidad no provoca que el guest sea interrumpido en cada desencolar; el guest establece `avail_event` para suprimir las notificaciones que no necesita.

Esta optimización reduce a la mitad la sobrecarga de notificaciones en las cargas de trabajo más exigentes. Es una característica que la mayoría de los drivers VirtIO modernos negocian, y la API `virtqueue(9)` la gestiona de forma transparente. Como autor de drivers, simplemente negocias la característica y no tienes que pensar en los detalles.

### VirtIO legado frente a moderno

VirtIO tiene dos versiones principales. El legado (a veces denominado «virtio 0.9») fue la especificación original; el moderno («virtio 1.0» y posteriores) llegó después con un diseño más limpio. FreeBSD admite ambos.

Las diferencias prácticas están en el diseño del espacio de configuración, el orden de bytes y la semántica de algunos bits de característica. VirtIO legado almacena la configuración en el orden de bytes nativo; VirtIO moderno es siempre little-endian. El legado exige algunos campos en una posición específica; el moderno usa estructuras de capacidad para describir el diseño de forma flexible. El legado define un conjunto específico de bits de característica por debajo del bit 32; el moderno los extiende por encima del bit 32.

Para los autores de drivers, el framework oculta la mayor parte de estas diferencias. Negociar `VIRTIO_F_VERSION_1` pone al driver en modo moderno; no negociarlo lo pone en modo legado. El helper `virtio_with_feature` comprueba el estado negociado. Siempre que tu driver siga la API `virtqueue(9)` y use `virtio_read_device_config` en lugar de acceder directamente al espacio de configuración, puedes ignorar la distinción legado/moderno casi por completo.

El único lugar donde se filtra esa distinción es en cómo se accede a los campos del espacio de configuración. VirtIO legado usa `virtio_read_device_config` con un argumento de tamaño y asume el orden de bytes nativo; VirtIO moderno asume little-endian y usa helpers que realizan el intercambio de bytes si el host es big-endian. El framework gestiona esto, pero un driver que acceda al espacio de configuración a mano (en lugar de hacerlo a través del framework) tendría que ser consciente de ello.

### Bits de característica que conviene conocer

Cada tipo de dispositivo VirtIO tiene su propio conjunto de bits de característica. Algunos merece la pena conocerlos de forma general, porque aparecen en muchos drivers.

- `VIRTIO_F_VERSION_1` indica que el dispositivo admite la versión 1 moderna de la especificación VirtIO. Casi todos los drivers modernos negocian este bit.
- `VIRTIO_F_RING_EVENT_IDX` habilita una forma más eficiente de supresión de interrupciones. Cuando ambos lados lo admiten, las notificaciones e interrupciones solo se envían cuando son realmente útiles, reduciendo la sobrecarga bajo carga.
- `VIRTIO_F_RING_INDIRECT_DESC`, ya mencionado, permite los descriptores indirectos.
- `VIRTIO_F_ANY_LAYOUT` relaja las reglas sobre cómo se ordenan los descriptores en una cadena.

Cada tipo de dispositivo tiene su propio conjunto además de estos. Para un dispositivo de bloques, `VIRTIO_BLK_F_WCE` indica que el dispositivo tiene una caché de escritura; `VIRTIO_BLK_F_FLUSH` proporciona un comando de vaciado. Para un dispositivo de red, `VIRTIO_NET_F_CSUM` anuncia el cálculo de checksum descargado en hardware. Para entropía, no hay bits de característica específicos del dispositivo.

La regla general es: lee los bits de característica que el dispositivo anuncia, decide cuáles puede usar el driver e ignora los demás. La negociación no consiste en «exigir» características; consiste en acordar una intersección.

### Un segundo ejemplo: estructura de `virtio_net`

Merece la pena echar un breve vistazo a un driver VirtIO más grande para ver cómo escala el esqueleto. El driver de red `if_vtnet` se encuentra en `/usr/src/sys/dev/virtio/network/if_vtnet.c`. Tiene aproximadamente diez veces la longitud de `virtio_random`, pero el tamaño adicional proviene de las características y la completitud, no de la complejidad en la interacción central con VirtIO. Saber cómo funciona ese escalado hace que la lectura futura sea más productiva.

`if_vtnet` comienza, como todo driver VirtIO, con el registro del módulo y una tabla `device_method_t`. La tabla de métodos es más extensa porque `if_vtnet` se integra en el framework `ifnet(9)`; encontrarás entradas `DEVMETHOD` para `device_probe`, `device_attach`, `device_detach`, `device_suspend`, `device_resume` y `device_shutdown`, cada una implementada por el driver. La macro `VIRTIO_DRIVER_MODULE` al final del archivo registra el driver para los transportes PCI y MMIO, exactamente igual que `virtio_random`.

El softc, `struct vtnet_softc`, es mucho más grande que `vtrnd_softc`, pero su función es la misma: almacenar el estado que el driver necesita para atender al dispositivo. Entre las incorporaciones más destacadas se encuentran un puntero a la estructura `ifnet` del kernel (`vtnet_ifp`), un array de colas de recepción (`vtnet_rxqs`), un array de colas de transmisión (`vtnet_txqs`), una máscara de características, una dirección MAC en caché y contadores de estadísticas. Cada estructura de cola contiene su propio puntero de virtqueue, una referencia inversa al softc, el contexto de taskqueue y varios campos administrativos. Un único dispositivo virtio-net de múltiples colas puede tener decenas de virtqueues; cada par de recepción/transmisión representa un par de colas que puede ejecutarse en paralelo con los demás.

La función probe sigue el mismo patrón `VIRTIO_SIMPLE_PROBE` que `virtio_random`, pero coincide con `VIRTIO_ID_NETWORK` (que vale 1). La función attach es extensa: lee los bits de características, los negocia, asigna las colas, lee la dirección MAC del dispositivo desde el espacio de configuración (`virtio_read_device_config`), inicializa el `ifnet`, se registra con la pila de red y configura los callouts para el sondeo del estado del enlace. Cada uno de estos pasos es una función pequeña por sí misma, y el flujo sigue el mismo ritmo de «negociar, asignar, registrar, arrancar» que ilustra `virtio_random`.

La ruta de recepción usa `virtqueue_dequeue` en un bucle para vaciar los paquetes completados. Por cada descriptor completado, el driver lee la cabecera del paquete (`virtio_net_hdr`) que el dispositivo escribió en los primeros bytes del buffer, extrae metadatos como los flags de estado del checksum y los tamaños de segmento GSO, y entrega el paquete a `if_input`. Si el paquete es el último de un lote, el driver repone nuevos buffers de recepción para mantener la cola abastecida de cara al siguiente ciclo.

La ruta de transmisión usa `virtqueue_enqueue` con una lista de scatter-gather que abarca tanto la cabecera del paquete como su cuerpo. La cookie es el puntero `mbuf(9)`, de modo que el callback de finalización de transmisión puede liberar el mbuf cuando el dispositivo ha terminado con él. Si la cola se llena, el driver deja de aceptar paquetes salientes hasta que se libere algo de espacio, lo que es la disciplina estándar de los drivers `ifnet`.

La negociación de características en `if_vtnet` resulta interesante por lo amplio que es el conjunto de características disponibles. Características como `VIRTIO_NET_F_CSUM` (descarga del checksum de transmisión), `VIRTIO_NET_F_GUEST_CSUM` (descarga del checksum de recepción), `VIRTIO_NET_F_GSO` (descarga de segmentación genérica) y `VIRTIO_NET_F_MRG_RXBUF` (buffers de recepción fusionados) modifican cada una la forma en que el driver programa las virtqueues y cómo interpreta los datos entrantes. La función de negociación de características del driver selecciona el conjunto que el driver implementa, lo ofrece para negociación y, a continuación, consulta la máscara negociada para decidir qué rutas de código activar.

La lección que nos deja `if_vtnet` es que un driver VirtIO escala mediante la diversidad de características y la multiplicidad de colas, no a través de una arquitectura fundamentalmente diferente. Si entiendes el ciclo de vida de `virtio_random`, entiendes el de `if_vtnet`; el código adicional reside en las sub-rutas específicas de cada característica y en los enganches propios de `ifnet`. Cuando llegue el momento de leer `if_vtnet.c`, concéntrate primero en `vtnet_attach`, después recorre la función de negociación de características y, a continuación, examina `vtnet_rxq_eof` (recepción) y `vtnet_txq_encap` (transmisión). Esas cuatro funciones explican el 80 % del driver.

### Un tercer ejemplo para la intuición: virtio_blk

`virtio_blk`, en `/usr/src/sys/dev/virtio/block/virtio_blk.c`, es más corto que `if_vtnet` pero más largo que `virtio_random`. Ilustra un tercer patrón habitual: un driver que expone un dispositivo orientado a bloques en lugar de uno orientado a flujos.

El softc de `vtblk` contiene una virtqueue, un pool de estructuras de solicitud, estadísticas e información de geometría leída desde el espacio de configuración del dispositivo. El probe y el attach siguen el patrón conocido. La parte interesante es cómo se estructuran las solicitudes.

Para cada operación de I/O de bloque, el driver construye una cadena de descriptores de tres segmentos: un descriptor de cabecera (que contiene el tipo de operación y el número de sector), cero o más descriptores de datos (la carga útil real, que es escribible desde la perspectiva del dispositivo en las lecturas y legible desde el dispositivo en las escrituras) y un descriptor de estado (donde el dispositivo escribe un código de estado de un solo byte). La cabecera y el estado son estructuras pequeñas; los segmentos de datos son los que trajo la solicitud `bio(9)`.

Este diseño de tres segmentos es un idioma habitual de VirtIO. Si ves un driver que construye una cadena que empieza con una cabecera y termina con un estado, estás ante un dispositivo de solicitud-respuesta. `virtio_scsi` emplea el mismo diseño, al igual que `virtio_console` (para mensajes de control). Reconocer el patrón acelera la lectura.

### Errores habituales al leer código VirtIO

Al leer cualquier driver VirtIO por primera vez, algunos patrones pueden confundir a un lector que no los espera.

El primero es la mezcla de patrones de probe. Algunos drivers usan `VIRTIO_SIMPLE_PROBE`; otros emplean sus propias funciones de probe que realizan comprobaciones de características más complejas. Ambas opciones son legítimas, y la primera es una forma abreviada para un caso habitual.

El segundo es el estilo de registro del módulo. `VIRTIO_DRIVER_MODULE` es una macro que se expande en dos llamadas a `DRIVER_MODULE`, y leer la definición de la macro (en `/usr/src/sys/dev/virtio/virtio.h`) lo aclara. Sin la macro, podrías preguntarte por qué el driver no llama a `DRIVER_MODULE` explícitamente; con ella, ves que lo hace, una vez por cada transporte.

El tercero es la distinción entre las funciones del núcleo de VirtIO (`virtio_*`) y las funciones de virtqueue (`virtqueue_*`). Las funciones del núcleo operan sobre el dispositivo completo; las de virtqueue operan sobre una única cola. Un driver normalmente usa ambas, y el prefijo del espacio de nombres es la pista.

El cuarto es la distinción entre polling e interrupción. Un driver que realiza polling (como `virtio_random`) pasa `NULL` como callback en `VQ_ALLOC_INFO_INIT` y llama a `virtqueue_poll` para esperar a que se completen las operaciones. Un driver que usa interrupciones (como `if_vtnet`) pasa un callback y deja que la infraestructura de interrupciones del kernel lo planifique. Ambas opciones son legítimas; la elección depende de la sensibilidad a la latencia de la carga de trabajo y de si el driver puede permitirse bloquear.

### Cerrando

Esta ha sido una sección larga, y deliberadamente así. VirtIO es denso, y su vocabulario es la base de la mayor parte del trabajo interesante que realiza un autor moderno de drivers de invitado. Ya sabes que un dispositivo VirtIO tiene un transporte, un tipo, una máscara de características, un estado, un espacio de configuración y un conjunto de virtqueues; que las virtqueues son anillos compartidos con componentes separados de descriptor, disponible y usado; que `virtqueue(9)` abstrae la mecánica del anillo detrás de una pequeña API de enqueue, dequeue, notify y poll; que la negociación de características es el mecanismo para la compatibilidad hacia adelante y hacia atrás; y que el driver VirtIO más pequeño de FreeBSD es `virtio_random`, con menos de cuatrocientas líneas de código que ilustran el esqueleto completo. También has visto cómo los drivers VirtIO más grandes, como `if_vtnet` y `virtio_blk`, están estructurados alrededor del mismo esqueleto, ampliado con código específico de cada característica.

La Sección 4 toma un rumbo diferente. Ahora que sabes qué es un driver de invitado, nos planteamos la siguiente pregunta: ¿cómo sabe el driver que se encuentra en un invitado en primer lugar?

## Sección 4: Detección del hipervisor y comportamiento del driver consciente del entorno

Hay razones legítimas para que un driver sepa si está ejecutándose dentro de una máquina virtual. Un driver que tiende a usar un contador de hardware costoso puede omitirlo en un hipervisor donde se sabe que el contador no es fiable. Un driver que realiza polling de un registro de hardware en un bucle ajustado puede afinar su intervalo de polling de forma diferente bajo virtualización, porque cada poll se convierte en una salida del hipervisor. Un driver que emite una advertencia sobre una tasa de interrupciones sospechosa puede suprimirla al ejecutarse en una nube donde el ruido de fondo es normal. Varios drivers de FreeBSD en el árbol de fuentes utilizan este tipo de adaptación.

También existen razones ilegítimas. Un driver que intenta comportarse de manera diferente para «ocultar» algo a un hipervisor está construyendo lógica anti-depuración, que tiene su lugar en el software anti-manipulación pero ninguno en un driver de propósito general. Un driver que bifurca en la marca exacta del hipervisor para ganar rendimiento está construyendo código frágil cuyo comportamiento derivará a medida que los hipervisores evolucionen. Nos centraremos en los usos legítimos.

### La variable global `vm_guest`

El kernel de FreeBSD expone una única variable global, `vm_guest`, que registra el hipervisor que detectó durante el boot. La variable se declara en `/usr/src/sys/sys/systm.h`:

```c
extern int vm_guest;

enum VM_GUEST {
    VM_GUEST_NO = 0,
    VM_GUEST_VM,
    VM_GUEST_XEN,
    VM_GUEST_HV,
    VM_GUEST_VMWARE,
    VM_GUEST_KVM,
    VM_GUEST_BHYVE,
    VM_GUEST_VBOX,
    VM_GUEST_PARALLELS,
    VM_GUEST_NVMM,
    VM_LAST
};
```

Los valores se explican por sí mismos. `VM_GUEST_NO` significa que el kernel se está ejecutando en bare metal; los demás valores identifican hipervisores específicos. `VM_GUEST_VM` es un valor de reserva para «una máquina virtual de tipo desconocido», para cuando el kernel podía detectar que estaba virtualizado pero no podía determinar qué hipervisor.

El sysctl correspondiente es `kern.vm_guest`, que devuelve la forma legible por humanos de la misma información. Los valores de cadena reflejan el enum: "none", "generic", "xen", "hv", "vmware", "kvm", "bhyve", "vbox", "parallels", "nvmm". Puedes consultarlo desde el shell:

```sh
sysctl kern.vm_guest
```

En una máquina FreeBSD en bare metal verás `none`. En un invitado de `bhyve(8)`, verás `bhyve`. En un invitado de VirtualBox, `vbox`. Y así sucesivamente.

### Cómo funciona la detección

La detección se produce al principio del boot, en las arquitecturas donde es detectable la presencia de un hipervisor. En amd64, el kernel examina la hoja CPUID en busca del bit de presencia de hipervisor y luego sondea las hojas específicas asociadas a cada marca de hipervisor. El código reside en `/usr/src/sys/x86/x86/identcpu.c`, en funciones llamadas `identify_hypervisor()` e `identify_hypervisor_cpuid_base()`. En arm64 existe un mecanismo similar a través de interfaces de firmware.

El autor del driver no necesita preocuparse por el código de detección. Lo que importa es que cuando se ejecuta el `attach()` de tu driver, `vm_guest` ya tiene su valor definitivo.

### Cuándo consultar `vm_guest`

Consultar `vm_guest` es adecuado en un driver cuando la corrección o el rendimiento del driver dependen de conocer el entorno de ejecución. Algunos ejemplos reales del árbol de fuentes ilustran el abanico de posibilidades:

- Algunos contadores de rendimiento en `/usr/src/sys/kern/kern_resource.c` ajustan su comportamiento cuando `vm_guest == VM_GUEST_NO`, porque el kernel asume que el modelo de costes en bare metal es preciso en ese contexto.
- Algunos fragmentos de código relacionados con el tiempo en el árbol de amd64 pueden no usar ciertas primitivas de temporización bajo hipervisores específicos donde esas primitivas se sabe que no son fiables.
- Algunos mensajes informativos en las rutas de probe del driver se suprimen bajo virtualización para no confundir a los usuarios que esperan que el driver se comporte «de forma diferente» en una VM.

Un driver que escribas debería usar `vm_guest` con moderación. Un anti-patrón habitual es comprobar `VM_GUEST_VM` como un interruptor genérico («si está virtualizado, haz X»), lo que normalmente es señal de que el driver tiene un error en hardware real que está encubriendo. Es preferible gestionar la causa subyacente directamente y usar `vm_guest` solo cuando la dependencia es genuinamente del entorno y no de una particularidad específica del hardware.

### Un ejemplo de uso

Supón que tu driver tiene un sysctl que controla la agresividad con la que realiza polling de un registro de hardware. El bucle ajustado es adecuado en bare metal, donde cada poll es económico, pero bajo virtualización cada poll tiene el coste de una salida del hipervisor, que es cara. Es posible que quieras que el intervalo de polling predeterminado sea mayor bajo virtualización:

```c
#include <sys/systm.h>

static int
my_poll_default(void)
{
    if (vm_guest != VM_GUEST_NO)
        return (100); /* milliseconds */
    else
        return (10);
}
```

Este es un uso legítimo de `vm_guest`: el driver está eligiendo un valor predeterminado razonable basado en una propiedad del entorno que afecta genuinamente a sus características de rendimiento. El valor sigue siendo modificable mediante un sysctl para los usuarios que saben más que el valor predeterminado.

Compara esto con un uso ilegítimo:

```c
/* DO NOT DO THIS */
if (vm_guest == VM_GUEST_VMWARE) {
    /* skip interrupt coalescing because VMware does it weirdly */
    sc->coalesce = false;
}
```

Aquí el driver bifurca en una marca específica de hipervisor para eludir un error percibido. Esto es frágil por tres razones. El comportamiento específico de VMware puede cambiar en una versión posterior; el mismo problema puede aplicarse a KVM o Parallels sin que el código lo detecte; y el driver tiene ahora una carga de mantenimiento vinculada a un producto de terceros. Un enfoque mejor es abordar la causa raíz (el código de coalescencia es frágil, así que arréglalo o expón un sysctl), no el síntoma (VMware lo desencadena casualmente).

### La vía del sysctl

Para las herramientas y los scripts en espacio de usuario, el sysctl `kern.vm_guest` es una interfaz mejor que hurgar en `/dev/mem` o similar. Un pequeño script de shell puede decidir si ejecutar determinadas pruebas en función del entorno:

```sh
if [ "$(sysctl -n kern.vm_guest)" = "none" ]; then
    echo "Running on bare metal, enabling hardware tests"
    run_hardware_tests
else
    echo "Running on a hypervisor, skipping hardware tests"
fi
```

Así es como la suite de pruebas de FreeBSD decide qué pruebas ejecutar en qué entorno. Reutilizar la misma variable desde dentro de un driver mantiene la coherencia de todo el sistema.

### Interacción con los subsistemas

Varios subsistemas de FreeBSD adaptan su comportamiento basándose en `vm_guest` de forma automática, y el autor de un driver debe conocer estas adaptaciones sin necesidad de tocarlas.

El código de selección del contador de tiempo en `/usr/src/sys/kern/kern_tc.c` tiene en cuenta `vm_guest` al elegir un contador de tiempo predeterminado, porque las implementaciones TSC de algunos hipervisores no son fiables en las migraciones de invitado. El driver no necesita preocuparse; el kernel simplemente elige un contador de tiempo más seguro.

Los drivers de transporte VirtIO (`virtio_pci.c` y `virtio_mmio.c`) no bifurcan en `vm_guest` directamente, porque ya son invitados por su propia ruta de probe. Confían en que si tienen un dispositivo, se están ejecutando en un entorno que emula VirtIO.

Algunos fragmentos de código del driver de red gestionan el caso en que se sabe que GRO (generic receive offload) interactúa mal con ciertas configuraciones de hipervisor. Estas adaptaciones no son decisiones driver a driver; se toman en la pila de red basándose en las características observadas.

Como autor de un único driver, tu regla general es: si sientes la necesidad de bifurcar en `vm_guest`, pregúntate si el problema corresponde al manejo del entorno del kernel en lugar de a tu driver. Normalmente es así.

### Detección dentro de un driver VirtIO

Un driver VirtIO, por definición, se está ejecutando dentro de algún tipo de entorno que habla VirtIO. El driver normalmente no necesita saber qué hipervisor específico respalda el dispositivo VirtIO. Esa es una de las virtudes de VirtIO: es agnóstico al entorno. El mismo driver `virtio_net` funciona bajo `bhyve`, bajo QEMU/KVM, bajo el backend de Google Cloud y bajo AWS Nitro, porque todos implementan el mismo estándar.

Hay dos excepciones. Una es cuando el driver se encuentra con una caída brusca de rendimiento específica del hipervisor, en cuyo caso `vm_guest` te dice qué solución alternativa aplicar. La otra es cuando el driver quiere imprimir un mensaje de diagnóstico que identifica el hipervisor por su nombre. Ambos casos son poco frecuentes.

### Detección desde el lado del host

Un driver del lado del host (uno que se ejecuta en el host, no en un invitado) normalmente no se preocupa en absoluto por `vm_guest`, porque el host no está virtualizado por definición. El sysctl devolverá `none` en bare metal, y ese es el caso esperado.

La única sutileza surge cuando el propio host es a su vez un guest, como ocurre en escenarios de virtualización anidada. Un host FreeBSD dentro de una VM de VMware ESXi, por ejemplo, tendrá `vm_guest = VM_GUEST_VMWARE`. Un `bhyve(8)` que se ejecute dentro de ese host presentará guests que también detectan virtualización, aunque verán la presentación de `bhyve` en lugar de la de VMware. La cadena de entornos puede alcanzar dos o tres niveles de profundidad en configuraciones de investigación y prueba. No asumas que un host es bare metal; si tu driver tiene una bifurcación según si actúa como host o como guest, utiliza el sysctl o la variable para distinguirlo.

### En resumen

`vm_guest` es una API pequeña y discreta que informa a tu driver, y a las herramientas en espacio de usuario, sobre el entorno en el que se ejecuta. Es fácil de usar y fácil de usar mal. Úsala para adaptar valores predeterminados sensatos, no para sortear fallos específicos del hipervisor. Úsala para orientar las decisiones en espacio de usuario a través del sysctl. No hagas que el comportamiento de tu driver dependa de la marca exacta del hipervisor; hacerlo acopla tu código al historial de versiones de un producto de terceros, y eso es un coste de mantenimiento que no querrás pagar.

La Sección 5 examina el otro lado de la virtualización para FreeBSD: el host que ejecuta los guests, y las herramientas e interfaces que el autor de un driver debe entender cuando trabaja con `bhyve(8)` o colabora con él.

## Sección 5: bhyve, PCI passthrough y consideraciones del lado del host

Hasta ahora nos hemos centrado en lo que ocurre dentro del guest. El guest es donde tiene lugar la mayor parte del aprendizaje, porque es donde vive la mayor parte del código del driver. Pero FreeBSD tiene otro papel en el relato de la virtualización: también es un hipervisor. `bhyve(8)` ejecuta máquinas virtuales, y entender cómo `bhyve(8)` presenta los dispositivos a sus guests resulta útil tanto si eres el autor del guest (para saber qué está haciendo el host) como si eres el autor del host (para saber cómo compartir un dispositivo real con un guest).

Esta sección da el paso al lado del host. No profundizaremos lo suficiente como para escribir código de hipervisor, porque ese es un tema en sí mismo. Sí llegaremos lo bastante lejos para que entiendas qué hace el host, cuáles son los controles disponibles en el lado del host y qué necesita saber el autor de un driver cuando coopera con `bhyve(8)`.

### bhyve desde la perspectiva del autor del driver

`bhyve(8)` es un hipervisor de tipo 2 que se ejecuta sobre un host FreeBSD y utiliza extensiones de virtualización por hardware (Intel VT-x o AMD-V) para ejecutar código del guest directamente en la CPU. Se implementa como un programa en espacio de usuario (`/usr/sbin/bhyve`) y un módulo del kernel (`vmm.ko`). El módulo del kernel gestiona las primitivas de virtualización de bajo nivel: entrada y salida de VM, gestión de tablas de páginas para la memoria del guest, emulación del APIC virtual y un conjunto de backends de dispositivo críticos para el rendimiento. El programa en espacio de usuario se encarga del resto: análisis de la línea de comandos, backends de dispositivos emulados que no necesitan vivir en el kernel, backends VirtIO y el bucle principal de VCPU.

Desde la perspectiva del autor de un driver, hay tres aspectos importantes de `bhyve(8)`.

En primer lugar, `bhyve(8)` es un programa de FreeBSD, de modo que el mismo kernel que ejecuta tu driver también podría estar ejecutando `bhyve(8)` en espacio de usuario. Esto significa que los recursos del lado del host (memoria, CPU, interfaces de red) pueden competir con `bhyve(8)` por la asignación. Si tu driver se ejecuta en un host que también es un hipervisor, quizás quieras pensar en la ubicación NUMA, la afinidad de IRQ y cuestiones similares.

En segundo lugar, `bhyve(8)` utiliza una interfaz del kernel de FreeBSD llamada `vmm(4)` para sus necesidades de bajo nivel. Esta interfaz es estable pero muy especializada; la mayoría de los autores de drivers nunca la tocan directamente. Si estás escribiendo un driver que necesita interactuar con máquinas virtuales (por ejemplo, un driver que proporciona un dispositivo paravirtual a los guests de `bhyve(8)` desde el lado del host), usarías `vmm(4)` o una de las bibliotecas de nivel superior que lo envuelven.

En tercer lugar, y lo más importante para este capítulo, `bhyve(8)` puede asignar un dispositivo PCI real directamente a un guest. Esto se denomina PCI passthrough y tiene implicaciones importantes para los autores de drivers a ambos lados de la valla.

### vmm(4): el lado del kernel de bhyve

`vmm(4)` es un módulo del kernel que expone una interfaz para crear y gestionar máquinas virtuales. Reside en `/usr/src/sys/amd64/vmm/` y directorios relacionados. El módulo se carga bajo demanda por `bhyvectl(8)` o `bhyve(8)`, y exporta una interfaz de dispositivo de caracteres a través de `/dev/vmm/NAME`, donde `NAME` es el nombre de la máquina virtual.

La interfaz de `vmm(4)` no es algo que un autor de drivers principiante necesite aprender en profundidad. Es compleja, especializada y de interés principalmente para quienes están ampliando o modificando el propio hipervisor. Para nuestros propósitos, basta con saber lo siguiente. `vmm(4)` gestiona el estado de la CPU virtual, incluidos los registros, las tablas de páginas y los controladores de interrupciones. Delega la emulación de los dispositivos no críticos para el rendimiento al espacio de usuario a través de una interfaz de buffer en anillo. Para los dispositivos críticos para el rendimiento, como el APIC virtual en el kernel o el IOAPIC virtual, gestiona la emulación en el propio kernel.

Un autor de drivers que se ejecute dentro de un guest de `bhyve(4)` nunca interactuará con `vmm(4)` directamente. El kernel del guest solo ve los dispositivos virtualizados; el mecanismo por el que se emulan es invisible. El único rastro observable está en `sysctl kern.vm_guest`, que informará `bhyve`.

Un autor de drivers que esté escribiendo código del lado del host para `bhyve(8)` verá `vmm(4)` únicamente a través de las bibliotecas en espacio de usuario. La mayoría de las tareas las gestiona `libvmmapi(3)`, que envuelve la interfaz ioctl en bruto. El trabajo directo con `vmm(4)` es poco frecuente fuera del propio desarrollo de `bhyve(8)`.

### PCI passthrough: dar a un guest un dispositivo real

La forma más directa de que un guest interactúe con un dispositivo físico es que el host le ceda acceso exclusivo a ese dispositivo. Esto se denomina PCI passthrough, y `bhyve(8)` lo admite a través del mecanismo `pci_passthru(4)`.

La idea es sencilla, pero la mecánica es sutil. Un dispositivo PCI real lo reclama normalmente un driver en el host. Ese driver programa el dispositivo, gestiona sus interrupciones y es propietario de sus registros mapeados en memoria. Cuando hacemos passthrough, queremos que el driver del guest se encargue de todo eso en su lugar. El host debe apartarse, y el hardware debe reconfigurarse para que las direcciones de memoria del guest se mapeen correctamente a la memoria del dispositivo y para que las operaciones DMA del dispositivo vayan a la memoria del guest y no a la del host.

El host se aparta desvinculando el driver que estaba vinculado al dispositivo y vinculando en su lugar un driver de sustitución (`ppt(4)`). `ppt` es un driver mínimo cuyo único propósito es reclamar el dispositivo para que nadie más lo haga. Su función probe coincide con cualquier dispositivo PCI cuya dirección encaje con un patrón especificado por el usuario en `/boot/loader.conf`, habitualmente mediante el parámetro ajustable `pptdevs`. Una vez que el driver de sustitución reclama el dispositivo, `bhyve(8)` puede solicitar un passthrough a través de la interfaz `vmm(4)`, y el dispositivo queda accesible dentro del guest.

La reconfiguración del hardware la gestiona la IOMMU. Esta es la parte que hace que el passthrough sea a la vez potente y peligroso. Sin una IOMMU, el DMA del dispositivo iría a direcciones físicas en el bus de memoria del host, y un guest malicioso podría programar el dispositivo para leer o escribir en cualquier lugar de la memoria del host. Eso es evidentemente inseguro. Una IOMMU (Intel VT-d o AMD-Vi en las plataformas que FreeBSD admite) se sitúa entre el dispositivo y el bus de memoria del host, remapeando las direcciones emitidas por el dispositivo para que no puedan escapar del guest. Desde la perspectiva del dispositivo, sigue realizando DMA a una dirección; desde la perspectiva del bus de memoria del host, esa dirección se traduce a algún lugar dentro de la memoria del guest y a ningún otro sitio.

Si tu host no tiene IOMMU, `bhyve(8)` rechazará configurar el passthrough. Esta es una comprobación de seguridad intencionada. Habilitar el passthrough sin la protección de la IOMMU sería como darle a un extraño un cheque en blanco firmado sobre el kernel del host: un fallo en el firmware del dispositivo o un guest malicioso, y el host queda comprometido.

### Cómo ve el passthrough el driver del guest

Desde la perspectiva del guest, un dispositivo con PCI passthrough parece exactamente el hardware real. La enumeración PCI del guest encuentra el dispositivo con sus identificadores de fabricante y de dispositivo reales, sus capacidades reales y sus BARs reales. El driver del guest se vincula exactamente igual que lo haría sobre bare metal. Las operaciones de lectura y escritura llegan al hardware real (con algo de traducción de direcciones de por medio). Las interrupciones se entregan al guest a través del controlador de interrupciones virtual del hipervisor. El DMA funciona, aunque las direcciones que el guest programa en el dispositivo son direcciones físicas del guest y no del host, siendo la IOMMU la encargada de la traducción.

Esto tiene tres consecuencias prácticas para el autor de un driver.

En primer lugar, tu driver no necesita saber que se está ejecutando bajo passthrough. El mismo binario del driver funciona tanto sobre bare metal como en un guest con passthrough. Este es un objetivo de diseño fundamental de todo el esquema.

En segundo lugar, si tu driver utiliza DMA, asegúrate de usar `bus_dma(9)` correctamente. El framework bus-DMA gestiona de forma transparente la traducción entre direcciones físicas del guest y del host si lo usas correctamente. Si estás haciendo cosas ingeniosas con direcciones físicas directamente (lo cual no deberías hacer), el passthrough romperá esas manipulaciones.

En tercer lugar, si tu driver depende de características específicas de la plataforma (por ejemplo, firmware especial en el propio dispositivo PCI o una tabla de BIOS concreta), esas características deben estar presentes también en el guest. El passthrough le da al guest el dispositivo, pero no le da el firmware ni las tablas de BIOS. Algunas configuraciones de passthrough fallan porque el driver asume la presencia de una tabla ACPI que existe en el host pero no dentro de la BIOS virtual del guest.

El último punto importa especialmente para dispositivos que esperan un orden estricto o acceso a memoria sin caché. Las interfaces bus-DMA y bus-space, usadas correctamente, gestionan estos casos. La manipulación directa de punteros a memoria mapeada generalmente no sobrevive al passthrough.

### Vinculación del driver en el host con bhyve

Cuando `bhyve(8)` se ejecuta en un host FreeBSD, existen dos categorías de dispositivos. Algunos los reclaman los drivers del host y se comparten con los guests mediante emulación o VirtIO. Otros los reclama `ppt(4)` y se pasan íntegramente al guest.

Si estás escribiendo un driver para un dispositivo que podría usarse bajo passthrough, hay algunas cosas que considerar.

La primera es si el dispositivo debería permitir siquiera el passthrough. Algunos dispositivos son fundamentales para el funcionamiento del host (por ejemplo, el controlador SATA desde el que arranca el host o la interfaz de red a través de la cual es accesible). Estos dispositivos no deberían marcarse como candidatos para el passthrough, porque quitárselos al host rompería el sistema. El mecanismo para marcar candidatos es administrativo, a través de `pptdevs` en `loader.conf`, y el administrador del host es el responsable. No hay un bloqueo por driver, pero el autor del driver puede documentar claramente si se recomienda el passthrough para el dispositivo.

La segunda es si el driver libera el dispositivo limpiamente en el momento del detach. El passthrough requiere que el driver original haga el detach y que luego `ppt(4)` haga el attach. Si el método `DEVICE_DETACH` de tu driver es descuidado, la configuración del passthrough será inestable. El código de detach debe detener el hardware limpiamente, liberar las IRQs, desasignar los recursos y liberar cualquier memoria que el hardware pudiera tocar. Cualquier cosa que persista después del detach es un riesgo.

La tercera es si el driver tolera ser reconectado después de un uso con passthrough. Cuando el guest se apaga y libera el dispositivo, el administrador del host podría querer volver a vincularlo al driver original para su uso en el host. El driver debería ser capaz de vincularse a un dispositivo que fue propiedad de `ppt(4)` anteriormente, aunque el dispositivo pueda haber sido reiniciado y reconfigurado por el guest. Esto significa que el método `DEVICE_ATTACH` del driver no debe asumir nada sobre el estado inicial del dispositivo; debe programarlo desde cero, igual que lo hace en el primer arranque.

Ninguno de estos es un requisito nuevo. Son todas cosas que un driver bien escrito hace de todos modos. El passthrough simplemente hace que los buenos hábitos sean más importantes, porque el coste de equivocarse se pone de manifiesto de inmediato.

### Grupos IOMMU y la realidad del aislamiento parcial

Unas palabras sobre los grupos IOMMU, que aparecen a veces en los debates sobre passthrough. Un IOMMU no siempre puede aislar un dispositivo de otro que comparta el mismo bus PCI. Cuando dos dispositivos comparten un bus o un bridge sin ACS (Access Control Services), el IOMMU los trata como un único grupo, porque no puede garantizar que uno no pueda acceder a las operaciones DMA del otro. Los drivers `dmar(4)` (Intel) y `amdvi(4)` (AMD) de FreeBSD gestionan este agrupamiento internamente, pero el administrador a veces tiene que hacer passthrough de un grupo completo en lugar de un único dispositivo.

Para el autor de un driver, la implicación práctica es que el passthrough a veces captura más dispositivos de los esperados. Si tu dispositivo está ubicado tras un bridge compartido con otro dispositivo, activar el passthrough para el tuyo puede arrastrar al otro también al sistema invitado. La solución habitual es colocar los dispositivos en bridges separados en la configuración del firmware, pero eso es una cuestión de administración del sistema, no de desarrollo de drivers. Conocer esta situación resulta útil al diagnosticar comportamientos inesperados.

### Consideraciones exclusivas del host: memoria, CPU y el hipervisor

Un host FreeBSD que ejecuta guests de `bhyve(8)` tiene algunas responsabilidades adicionales más allá de las habituales. La memoria para la RAM del guest se asigna desde la memoria física del host, por lo que un host que ejecuta muchos guests necesita proporcionalmente más memoria. Las VCPUs del guest se respaldan con threads del host, de modo que un host con muchos guests debe aprovisionar capacidad de CPU. Y el propio hipervisor utiliza una pequeña cantidad de memoria y CPU para su gestión interna.

El autor de un driver en el lado del host no necesita gestionar estos recursos directamente. Los gestiona `bhyve(8)` junto con el administrador del host. Pero hay dos formas en las que un driver del lado del host puede interactuar con ellos.

La primera se produce cuando un driver ofrece un backend de dispositivo que `bhyve(8)` consume. Un ejemplo sería un driver de almacenamiento que proporciona el almacén de respaldo para el disco virtual de un guest. Si el driver del lado del host es lento, el guest lo percibe como un disco lento. Si el driver del lado del host consume demasiada memoria, el host se queda sin memoria para los guests. Se trata de un problema clásico de recursos compartidos, y habitualmente se resuelve mediante el aprovisionamiento de recursos más que mediante código ingenioso.

La segunda se produce cuando un driver interactúa con el hipervisor a través de `vmm(4)` o similares. Los backends de dispositivos paravirtuales utilizan a veces hooks del lado del kernel para entregar notificaciones a los guests de forma más eficiente que pasando por el espacio de usuario. Son raros y avanzados, y quedan fuera del ámbito de este capítulo. Se mencionan aquí para que no te sorprendas si los ves referenciados más adelante.

### Notas multiplataforma: bhyve en arm64

`bhyve(8)` funciona en amd64 y, cada vez más, en arm64. El port a arm64 utiliza las extensiones de virtualización ARMv8 (EL2) en lugar de Intel VT-x o AMD-V. La SMMU (System Memory Management Unit) asume el papel del IOMMU. La interfaz en espacio de usuario es la misma, la experiencia VirtIO del guest es la misma y, desde la perspectiva del autor de un driver, las dos arquitecturas son intercambiables. La distinción solo importa para el código del hipervisor de bajo nivel dentro de `vmm(4)`.

Para un libro sobre desarrollo de drivers, la lección es la que ya aprendimos en el Capítulo 29: escribe código limpio, que use bus-dma y que sea correcto en cuanto a endianness, y no notarás sobre qué arquitectura estás ejecutando. La historia del hipervisor es otra buena razón para seguir esos hábitos.

### Inspección de bhyve desde el host

El autor de un driver puede inspeccionar el estado de `bhyve(8)` de varias formas útiles sin adentrarse en los internos de `vmm(4)`.

`bhyvectl(8)` es la herramienta de línea de comandos para consultar y controlar máquinas virtuales. `bhyvectl --vm=NAME --get-stats` muestra los contadores que mantiene `vmm(4)` para un guest en ejecución, incluyendo recuentos de VM exits, recuentos de emulación y diagnósticos similares. Esto es útil cuando sospechas que un driver del guest está generando VM exits innecesarios, un problema de rendimiento habitual.

`pciconf -lvBb` en el host muestra los dispositivos PCI y sus vinculaciones actuales con los drivers. Un dispositivo vinculado a `ppt(4)` es visible en modo passthrough; un dispositivo vinculado a su driver nativo está disponible para el host. Es una forma rápida de ver qué se está pasando en modo passthrough y qué no.

`vmstat -i` en el host muestra el recuento de interrupciones por dispositivo. Si un dispositivo se pasa en modo passthrough a un guest, sus interrupciones se entregan al controlador de interrupciones virtual del guest, no al host. En el lado del host, verás cómo aumentan en su lugar los contadores de posted-interrupt o de reasignación de interrupciones del hipervisor. Es un diagnóstico sutil pero útil.

Nada de esto es lectura obligatoria para el desarrollo de drivers. Se menciona aquí para que, cuando te encuentres con `bhyve(8)` en un host mientras depuras un driver, sepas dónde mirar primero.

### Una línea de comandos típica de bhyve

Para los lectores que quieran ver cómo es `bhyve(8)` cuando se invoca sin una herramienta envolvente, a continuación se muestra una línea de comandos representativa. Arranca un guest llamado `guest0` con dos VCPUs, dos gigabytes de memoria, un disco virtio-blk respaldado por un archivo y una interfaz de red virtio-net puenteada al host:

```sh
bhyve -c 2 -m 2G \
    -s 0,hostbridge \
    -s 2,virtio-blk,/vm/guest0/disk0.img \
    -s 3,virtio-net,tap0 \
    -s 31,lpc \
    -l com1,stdio \
    guest0
```

Cada opción `-s` define un slot PCI. El slot cero es el host bridge; el slot dos es un dispositivo virtio-blk respaldado por un archivo de imagen de disco; el slot tres es un dispositivo virtio-net respaldado por una interfaz `tap(4)` que el host ha configurado; el slot treinta y uno es el LPC bridge usado para dispositivos heredados como la consola serie. La opción `-l com1,stdio` redirige el primer puerto serie del guest a la E/S estándar del host, lo que resulta cómodo para el acceso a la consola.

Cuando se ejecuta este comando, el kernel del host crea una nueva VM a través de `vmm(4)`, asigna memoria para la RAM del guest y entrega la ejecución de las VCPUs al kernel del guest. El backend virtio-blk de `bhyve(8)` (código en espacio de usuario) atiende las solicitudes de bloques del guest leyendo y escribiendo en el archivo de imagen de disco. El backend virtio-net envía y recibe paquetes a través de la interfaz `tap0`, que la pila de red del host gestiona como una interfaz ordinaria.

Un driver que se ejecuta dentro de este guest ve un dispositivo PCI virtio-blk, un dispositivo PCI virtio-net y la habitual variedad de dispositivos LPC emulados. Desde la perspectiva del driver, no hay ninguna pista de que el «hardware» está implementado por un programa en espacio de usuario que se ejecuta unos pocos milisegundos más allá en el mismo host. La abstracción es, por diseño, completa.

### El interior del módulo vmm(4)

`vmm(4)` es la infraestructura del lado del kernel para `bhyve(8)`. En FreeBSD 14.3, sus archivos fuente principales se encuentran en `/usr/src/sys/amd64/vmm/`; existe un port a arm64 en desarrollo activo que se espera que se incluya en una versión posterior, por lo que si estás en FreeBSD 14.3 el árbol amd64 es el que debes leer. El módulo exporta una pequeña interfaz de control a través de `/dev/vmmctl` y una interfaz por VM a través de `/dev/vmm/NAME`.

La interfaz de control la utilizan `bhyvectl(8)` y `bhyve(8)` para crear, destruir y enumerar máquinas virtuales. La interfaz por VM se usa para leer y escribir el estado de las VCPUs, mapear la memoria del guest, inyectar interrupciones y recibir eventos de VM exit del guest.

Cuando una VCPU del guest ejecuta código que requiere emulación (leer de un puerto I/O, acceder a un registro de dispositivo mapeado en memoria, ejecutar una hypercall), el hardware intercepta la ejecución y el control vuelve a `vmm(4)`. El módulo puede gestionar el exit en el kernel (para un pequeño conjunto de casos críticos para el rendimiento, como leer el APIC local) o reenviarlo a `bhyve(8)` en el espacio de usuario (para la mayoría de los casos, incluida toda la emulación de dispositivos VirtIO).

La separación entre kernel y espacio de usuario es una decisión de diseño deliberada. Mantener el código en el espacio de usuario facilita su desarrollo, depuración y auditoría. Mantener los caminos críticos para el rendimiento en el kernel mantiene la sobrecarga baja. Para FreeBSD, la separación ha funcionado bien, y `bhyve(8)` ha crecido desde un pequeño prototipo académico hasta convertirse en un hipervisor de calidad de producción.

Para el autor de un driver que no esté extendiendo el propio `bhyve(8)`, nada de esto importa en detalle. Lo que importa es el comportamiento observable: un guest ejecuta código, algunas operaciones provocan un trap, el hipervisor las emula y el guest continúa. El driver del guest observa un comportamiento consistente independientemente de dónde ocurra la emulación.

### Virtualización anidada

Una breve mención a la virtualización anidada, ya que surge con frecuencia. El `bhyve(8)` de FreeBSD en amd64 no admite actualmente la ejecución de guests virtualizados por hardware dentro de guests virtualizados por hardware (todavía no hay soporte para las extensiones `VIRTUAL_VMX` ni `VIRTUAL_SVM`). Si intentas ejecutar `bhyve(8)` dentro de un guest de `bhyve(8)`, el hipervisor interior no conseguirá inicializarse. Tanto Intel como AMD admiten la virtualización anidada en hardware, pero la implementación de FreeBSD aún no la ha habilitado.

Esto importa para los laboratorios únicamente en el sentido de que debes ejecutar `bhyve(8)` en hardware físico (o en un host que proporcione virtualización anidada, algo que ofrecen algunas plataformas en la nube). Si tu máquina de laboratorio es a su vez una VM, puede que `bhyve(8)` no consiga cargar `vmm.ko`, o que lo cargue pero se niegue a arrancar guests.

En arm64, la virtualización anidada es otra historia; la arquitectura ARM tiene un soporte más limpio, y el port de FreeBSD a arm64 está avanzando hacia habilitarla. Para información actualizada, consulta las páginas de manual de `bhyve(8)` y `vmm(4)` en la versión de FreeBSD que estés ejecutando.

### Un ejemplo: depuración de un driver con PCI passthrough

Para hacer más concreta la discusión del lado del host, considera un escenario que combina varias de las ideas anteriores. Tienes una tarjeta de red PCI que el driver de tu host sabe cómo manejar. Quieres pasarla en modo passthrough a un guest de `bhyve(8)` y verificar que el mismo driver funciona sin cambios dentro del guest.

Paso 1: identifica el dispositivo. `pciconf -lvBb` muestra `em0@pci0:2:0:0` con el driver `em` vinculado. El dispositivo es un controlador Ethernet Gigabit de Intel.

Paso 2: márcalo para passthrough. Edita `/boot/loader.conf` y añade `pptdevs="2/0/0"`. Reinicia.

Paso 3: verifica. Tras el reinicio, `pciconf -l` muestra `ppt0@pci0:2:0:0`, con `ppt` (el driver de marcador de posición) vinculado en lugar de `em`. El dispositivo ya no está disponible para la pila de red del host.

Paso 4: configura el guest. En la configuración `bhyve` del guest, añade `-s 4,passthru,2/0/0`. Esto indica a `bhyve(8)` que pase el dispositivo en modo passthrough al guest en el slot PCI 4.

Paso 5: arranca el guest. Dentro del guest, ejecuta `pciconf -lvBb`. El dispositivo aparece, con sus IDs reales de fabricante y dispositivo de Intel, vinculado a `em`. Comprueba `dmesg`. El driver `em` del guest se ha vinculado al dispositivo en passthrough exactamente como lo haría en hardware físico.

Paso 6: ejercita el dispositivo. Configura la interfaz (`ifconfig em0 10.0.0.1/24 up`), envía tráfico y verifica que funciona.

Paso 7: apaga el guest. De vuelta en el host, decide si mantener el dispositivo en modo passthrough o devolvérselo al host. Si lo quieres de vuelta, edita `/boot/loader.conf` para eliminar `pptdevs`, reinicia y verifica que `em` vuelve a estar vinculado.

Cada paso de este flujo de trabajo es algo que hace el administrador; el driver en sí no se toca. Ese es el objetivo. Si el driver está escrito correctamente (con `bus_dma`, un detach limpio y sin suposiciones ocultas sobre la plataforma), funciona en ambos entornos sin cambios.

### Cerrando

El lado del host en la virtualización es donde FreeBSD desempeña un papel algo diferente. En lugar de escribir un driver que consume un dispositivo proporcionado por el hipervisor, puede que te encuentres escribiendo (o al menos interactuando con) la infraestructura que proporciona dispositivos a un hipervisor. `bhyve(8)`, `vmm(4)` y `pci_passthru(4)` son las principales interfaces que debes conocer. El PCI passthrough es el más relevante de todos para el autor de un driver, porque pone a prueba el ciclo de vida de detach y reattach que un driver bien escrito ya admite.

Con los guests y los hosts ya tratados, el siguiente entorno importante en la historia de la virtualización de FreeBSD es el que no usa ningún kernel separado: los jails. La Sección 6 se ocupa de ellos, de devfs y del framework VNET que extiende el modelo de jails a la pila de red.

## Sección 6: Jails, devfs, VNET y visibilidad de dispositivos

La virtualización y la containerización comparten un objetivo: ambas permiten que una máquina física aloje varias cargas de trabajo que, para sus usuarios, parecen ejecutarse en máquinas independientes. Los mecanismos que utilizan son radicalmente diferentes. Una máquina virtual ejecuta un kernel de guest completo sobre un hipervisor; tiene su propio mapa de memoria, su propio árbol de dispositivos, su propio todo. Un contenedor, en el sentido de FreeBSD, comparte completamente el kernel del host. Lo que tiene de propio es una vista del sistema de archivos, una tabla de procesos y (si el administrador lo configura así) una pila de red. La respuesta de FreeBSD a la pregunta de los contenedores es el jail, y los jails han existido de alguna forma desde FreeBSD 4.0. Para los autores de drivers, los jails importan porque cambian lo que un proceso puede ver y hacer con respecto a los dispositivos, sin modificar en absoluto el código del driver.

Esta sección explica cómo interactúan los jails con los dispositivos. Se centra en cuatro temas: el modelo de jails en sí, el sistema de conjuntos de reglas de devfs que controla la visibilidad de los dispositivos, el framework VNET que da a los jails sus propias pilas de red y la pregunta que todo sistema de contenedores tiene que responder en algún momento: qué procesos pueden acceder a qué drivers.

### Qué es un jail y qué no es

Un jail es una subdivisión de los recursos del kernel anfitrión. Un proceso que se ejecuta dentro de un jail ve un subconjunto del sistema de archivos (con raíz en el directorio raíz del jail), un subconjunto de la tabla de procesos (solo los procesos del mismo jail), un subconjunto de la red (dependiendo de si el jail tiene su propio VNET) y un subconjunto de los dispositivos (dependiendo del conjunto de reglas devfs del jail). El kernel anfitrión es un único kernel compartido; no hay un kernel invitado separado dentro del jail. Las llamadas al sistema realizadas dentro del jail son ejecutadas por el mismo kernel que ejecuta las llamadas desde fuera del jail, con comprobaciones específicas del jail insertadas en los lugares adecuados.

Dado que el kernel es compartido, los drivers también lo son. No hay una copia separada de un driver dentro de cada jail; solo existe el driver que el kernel cargó en el momento del arranque. El jail simplemente controla cuáles de los dispositivos del driver son visibles para los procesos del jail. Un jail que no haya sido configurado para ver `/dev/null` no verá ningún `/dev/null`; un jail que haya sido configurado para ver `/dev/null` verá el mismo `/dev/null` que ve el anfitrión. Ninguna nueva instancia del driver; ningún nuevo softc; solo una regla de visibilidad.

Esta sencillez es a la vez la gran fortaleza y la gran limitación de los jails. Significa que los jails son extremadamente baratos: arrancar un jail tiene aproximadamente el coste de ejecutar unas pocas llamadas al sistema, mientras que arrancar una VM tiene el coste de iniciar un kernel completo. También significa que los jails no pueden aislar fallos a nivel de kernel: un bug en un driver que provoque un pánico en el kernel anfitrión lo provocará también en todos los jails que se ejecuten sobre él. Un jail es un límite de políticas, no un límite de fallos. Para muchas cargas de trabajo, ese equilibrio es excelente; para otras, una VM es la respuesta correcta.

### La visión del kernel sobre las jails

Dentro del kernel, una jail se representa mediante una `struct prison`, definida en `/usr/src/sys/sys/jail.h`. Cada proceso tiene un puntero a la prison a la que pertenece, a través de `td->td_ucred->cr_prison`. El código que quiere comprobar si un proceso está dentro de una jail puede comparar este puntero con la variable global `prison0`, que es la jail raíz (el propio host). Si el puntero es `prison0`, el proceso está en el host; en caso contrario, está en alguna jail.

Existen varias funciones auxiliares para las comprobaciones habituales que podría querer hacer un driver. `jailed(cred)` devuelve true si la credencial pertenece a una jail distinta de `prison0`. `prison_check(cred1, cred2)` devuelve cero si dos credenciales están en la misma jail (o una es el padre de la otra); en caso contrario, devuelve un error. `prison_priv_check(cred, priv)` es la manera en que las comprobaciones de privilegios se extienden para las jails: un usuario root dentro de una jail no tiene todos los privilegios que root tiene en el host, y `prison_priv_check` implementa esa reducción.

Un autor de drivers normalmente no necesitará llamar a ninguna de estas funciones directamente. El framework las llama en tu nombre. Cuando un proceso abre un nodo de `devfs`, por ejemplo, la capa devfs consulta el ruleset de devfs de la jail antes de entregar el descriptor de archivo. Cuando un proceso intenta usar una función protegida por privilegios (como `bpf(4)` o `kldload(2)`), la comprobación de privilegios pasa por `prison_priv_check`. Un driver solo necesita ser consciente de que estas comprobaciones existen y de llamar correctamente a los helpers del framework cuando define sus propias reglas de acceso.

### devfs: El sistema de archivos a través del cual se exponen los dispositivos

En FreeBSD, los dispositivos se exponen a través de un sistema de archivos llamado `devfs(5)`. Cada entrada bajo `/dev` es un nodo de `devfs`. Un driver que llama a `make_dev(9)` o `make_dev_s(9)` crea un nodo de `devfs`; el nombre, los permisos y el uid/gid son atributos de ese nodo. El nodo es visible en todas las instancias de `devfs` que el kernel monta, y FreeBSD monta una instancia de `devfs` por cada vista del sistema de archivos: una para el `/dev` del host, una para el `/dev` de cada jail (si la jail tiene su propio `/dev`), y una por cada chroot que monte su propio `devfs`.

Esta es la primera parte de la historia de visibilidad. Cada jail (más concretamente, cada vista del sistema de archivos) tiene su propio mount de `devfs`, y el kernel puede aplicar reglas diferentes a distintos mounts. Las reglas se llaman rulesets de devfs, y son la principal herramienta para controlar qué dispositivos puede ver una jail.

### Rulesets de devfs: declarando qué puede ver una jail

Un ruleset de devfs es un conjunto numerado de reglas almacenadas en el kernel. Las reglas pueden ocultar nodos, revelar nodos, cambiar sus permisos o cambiar su propiedad. El ruleset se aplica a una instancia de `devfs` montada; cada vez que se produce una búsqueda en esa instancia, el kernel recorre el ruleset y aplica la regla coincidente a cada nodo.

En un sistema FreeBSD recién instalado, hay cuatro rulesets predefinidos en `/etc/defaults/devfs.rules` (que se procesa cuando el kernel arranca). El archivo usa la sintaxis de `devfs(8)`, un pequeño lenguaje declarativo para la construcción de rulesets. Veamos una muestra representativa.

```text
[devfsrules_hide_all=1]
add hide

[devfsrules_unhide_basic=2]
add path log unhide
add path null unhide
add path zero unhide
add path crypto unhide
add path random unhide
add path urandom unhide

[devfsrules_jail=4]
add include $devfsrules_hide_all
add include $devfsrules_unhide_basic
add include $devfsrules_unhide_login
add path zfs unhide

[devfsrules_jail_vnet=5]
add include $devfsrules_hide_all
add include $devfsrules_unhide_basic
add include $devfsrules_unhide_login
add path zfs unhide
add path 'bpf*' unhide
```

La regla 1, `devfsrules_hide_all`, oculta todo. Por sí sola es inútil, porque un mount de `devfs` sin nada visible no resulta de utilidad. Es el punto de partida para otros rulesets.

La regla 2, `devfsrules_unhide_basic`, desoculta un pequeño conjunto de dispositivos esenciales: `log`, `null`, `zero`, `crypto`, `random`, `urandom`. Estos son los dispositivos que prácticamente cualquier programa necesita; sin ellos, incluso las herramientas básicas fallan.

La regla 4, `devfsrules_jail`, es el ruleset pensado para jails sin VNET. Comienza incluyendo `devfsrules_hide_all` (de modo que todo queda oculto), luego aplica `devfsrules_unhide_basic` encima (para que los elementos esenciales sean visibles) y añade dispositivos ZFS. El resultado es una jail que ve un conjunto pequeño y seguro de dispositivos, y nada más.

La regla 5, `devfsrules_jail_vnet`, es el equivalente para jails VNET. Es igual que la regla 4 con la particularidad de que los dispositivos `bpf*` se desocultan, porque una jail VNET podría necesitar legítimamente `bpf(4)` (para herramientas como `tcpdump(8)` o `dhclient(8)`).

Al crear una jail, el administrador especifica qué ruleset aplicar al mount `/dev` de la jail, ya sea a través de `jail.conf(5)` (`devfs_ruleset = 4`) o en la línea de comandos (`jail -c ... devfs_ruleset=4`). El kernel aplica el ruleset al mount, y la jail solo ve lo que el ruleset permite.

### Creación de un ruleset de devfs personalizado

Para la mayoría de las jails, los rulesets predeterminados son suficientes. Cuando no lo son, un administrador puede definir nuevos. Un nuevo ruleset debe tener un número único (distinto de los valores predeterminados reservados) y puede construirse mediante inclusión, adición y sobreescritura.

Un ejemplo clásico es una jail que necesita un dispositivo concreto que las reglas predeterminadas ocultan. Supongamos que tenemos una jail que ejecuta un servicio que necesita `/dev/tun0` y queremos exponerlo sin abrir toda la familia `/dev/tun*`. Crearíamos un ruleset como el siguiente:

```text
[devfsrules_myjail=100]
add include $devfsrules_jail
add path 'tun0' unhide
```

Y lo aplicaríamos en `jail.conf(5)`:

```text
myjail {
    path = /jails/myjail;
    devfs_ruleset = 100;
    ...
}
```

La herramienta `devfs(8)` puede cargar este ruleset en el kernel en ejecución con `devfs rule -s 100 add ...`, o el administrador puede editar `/etc/devfs.rules` y reiniciar `devfs`. Para una configuración persistente, el archivo es el lugar adecuado.

### Lo que los rulesets de devfs no hacen

Vale la pena señalar lo que los rulesets de devfs no son. No son un sistema de capacidades. Ocultar un dispositivo a una jail significa que la jail no puede abrir esa ruta concreta, pero una jail que tenga el privilegio `allow.raw_sockets` puede seguir enviando paquetes raw arbitrarios, con ruleset o sin él. Ocultar `/dev/kmem` no impide que un atacante con los privilegios adecuados lea la memoria del kernel por otros medios; simplemente elimina una vía obvia.

Los rulesets son una política de visibilidad, por encima del modelo estándar de permisos UNIX. Los permisos UNIX siguen aplicándose: un archivo oculto por el ruleset no se puede abrir, pero un archivo visible para el ruleset sigue respetando sus propios permisos. El usuario `root` de una jail puede abrir `/dev/null` porque el ruleset lo permite y los permisos también lo permiten, no porque el ruleset por sí solo otorgue acceso.

Para un aislamiento robusto, combina los rulesets con restricciones de privilegios (parámetros `allow.*` en `jail.conf(5)`) y, si la carga de trabajo lo justifica, con una VM. Los rulesets solos son una capa de defensa, no la única.

### La perspectiva del autor de un driver sobre los rulesets de devfs

Para un autor de drivers, los rulesets de devfs importan por dos razones.

En primer lugar, cuando creas un nodo de `devfs` con `make_dev(9)`, eliges un propietario, un grupo y unos permisos predeterminados. Estos se aplican a todas las vistas de `devfs` en las que aparece el nodo. Si tu dispositivo es algo que las jails generalmente no deberían ver (por ejemplo, una interfaz de gestión de hardware de bajo nivel), considera si el nombre debería ser obvio (para que los administradores puedan escribir fácilmente una regla que lo oculte) o si debería estar bajo un subdirectorio (para que una sola regla pueda ocultar el subdirectorio completo).

En segundo lugar, si el dispositivo de tu driver es algo que las jails suelen necesitar, documenta ese hecho en la página de manual del driver. El administrador que escribe el ruleset de una jail normalmente no es quien escribió el driver, y necesita saber si debe desocultar tu nodo. Una línea como "Este dispositivo se usa habitualmente dentro de jails; desoculta con `add path mydev unhide` en el ruleset de la jail" resulta muy útil.

Ninguno de estos aspectos implica un cambio de código. Ambos son decisiones sobre nomenclatura y documentación. Escribir un driver no consiste solo en código; también consiste en hacer que ese código sea utilizable por los administradores que lo desplegarán.

### Cerrando la parte de las jails y devfs

Las jails son el mecanismo de contenerización ligero de FreeBSD. Comparten el kernel del host y, por tanto, también los drivers, pero controlan qué dispositivos son visibles a través del mecanismo de rulesets de devfs. Para un autor de drivers, las decisiones de diseño relevantes se encuentran en el nivel de nomenclatura y documentación: elige nombres que faciliten la escritura de reglas y documenta qué jails deberían ver el dispositivo.

El lado de red de las jails es otra historia, porque FreeBSD tiene dos modelos: jails de pila única que comparten la red del host, y jails VNET que tienen su propia pila de red. El modelo VNET es el más interesante para los autores de drivers, porque tiene consecuencias directas sobre cómo se asignan y trasladan los drivers de red. Pasamos a eso a continuación.

### El framework VNET: un kernel, muchas pilas de red

El modelo de jail predeterminado tiene una sola pila de red: la del host. Cada jail ve la misma tabla de enrutamiento, las mismas interfaces, los mismos sockets. Una jail puede restringirse a determinadas direcciones IPv4 o IPv6 (mediante `ip4.addr` e `ip6.addr` en `jail.conf(5)`), pero no puede tener una configuración de red genuinamente independiente. Eso suele ser suficiente para cargas de trabajo sencillas, pero no para ninguna jail que quiera ejecutar su propio firewall, usar su propio gateway predeterminado o ser alcanzable desde el exterior a través de un conjunto propio de direcciones en sus propias interfaces.

VNET (abreviatura de "virtual network stack", pila de red virtual) resuelve esto. Es una característica del kernel que replica las partes de la pila de red por jail, de modo que cada jail VNET ve su propia tabla de enrutamiento, su propia lista de interfaces, su propio estado del firewall y su propio espacio de nombres de sockets. El código de la pila sigue perteneciendo al mismo kernel, pero muchas de sus variables globales son ahora por VNET en lugar de verdaderamente globales. El estado por interfaz de un driver de red pertenece al VNET al que esté asignado en cada momento, y las interfaces pueden moverse de un VNET a otro.

Para los autores de drivers, VNET resulta interesante en tres aspectos. Cambia cómo se declara el estado global en los subsistemas de red. Añade un ciclo de vida a las interfaces: las interfaces pueden moverse, y los drivers deben soportar ese traslado correctamente. Y se integra con la creación y destrucción de jails a través de hooks específicos de VNET.

### Declaración del estado VNET: VNET_DEFINE y CURVNET_SET

El diseño de VNET recae sobre quien declara el estado global en un subsistema compatible con VNET. En lugar de un simple `static int mysubsys_count;`, una declaración compatible con VNET tiene este aspecto:

```c
VNET_DEFINE(int, mysubsys_count);
#define V_mysubsys_count VNET(mysubsys_count)
```

La macro `VNET_DEFINE` se expande a una declaración de almacenamiento que coloca la variable en una sección especial del kernel. En el momento de la creación del VNET, el kernel asigna una nueva región de memoria por VNET y la inicializa a partir de esa sección. La macro `VNET(...)`, usada a través de un alias corto como `V_mysubsys_count`, se resuelve en la copia correcta para el VNET actual.

"El VNET actual" es un contexto local al thread. Cuando un thread entra en código que opera sobre un VNET, llama a `CURVNET_SET(vnet)` para establecer el contexto, y a `CURVNET_RESTORE()` para restaurarlo. Dentro del contexto, `V_mysubsys_count` se resuelve en la instancia correcta. Fuera del contexto, acceder a `V_mysubsys_count` es un error; la macro depende del puntero al VNET actual, local al thread, y sin ese puntero establecido el resultado es indefinido.

La mayoría de los autores de drivers no necesitan escribir declaraciones `VNET_DEFINE` por su cuenta. La pila de red y el framework ifnet declaran su propio estado VNET. El estado a nivel de driver (softcs por interfaz, datos privados por hardware) normalmente no tiene alcance VNET, porque está ligado al hardware, no a la pila de red. El estado del driver vive donde el driver lo haya colocado, y el framework se encarga de mover los fragmentos adecuados entre VNETs cuando las interfaces se trasladan.

Lo que los autores de drivers sí necesitan hacer es envolver cualquier código que toque objetos de la pila de red en un par `CURVNET_SET` / `CURVNET_RESTORE` si ese código se invoca desde fuera de un punto de entrada de la pila de red. La mayor parte del código de un driver ya se llama desde la pila de red, por lo que el contexto VNET ya está establecido. La excepción son los callouts y las taskqueues: una callback disparada desde un callout no hereda un contexto VNET, y el driver debe establecer uno antes de tocar cualquier variable `V_`.

Un patrón típico dentro de un handler de callout:

```c
static void
mydev_callout(void *arg)
{
    struct mydev_softc *sc = arg;

    CURVNET_SET(sc->ifp->if_vnet);
    /* code that touches network-stack variables */
    CURVNET_RESTORE();
}
```

El driver almacenó una referencia al VNET de la interfaz en el momento del attach (`sc->ifp->if_vnet` lo rellena el framework cuando se crea el ifnet). En cada callout, establece el contexto, realiza su trabajo y lo restaura. Este es uno de los pocos lugares donde los autores de drivers se encuentran directamente con VNET.

### if_vmove: Mover una interfaz entre VNETs

Cuando arranca una jail con VNET, habitualmente recibe una o más interfaces de red para su uso. Existen dos mecanismos habituales. El primero consiste en que el administrador crea una interfaz virtual (un `epair(4)` o un `vlan(4)`) y mueve uno de sus extremos dentro de la jail. El segundo consiste en que el administrador mueve directamente una interfaz física dentro de la jail, de modo que esta tiene acceso exclusivo a ella mientras está en funcionamiento.

El traslado lo implementa la función del kernel `if_vmove()`. Recibe una interfaz y un VNET de destino, desconecta la interfaz de la pila de red del VNET de origen (sin destruirla) y la reconecta a la pila de red del VNET de destino. La interfaz conserva su driver, su softc, su estado de hardware y su dirección MAC configurada. Lo que cambia es a qué tabla de rutas, cortafuegos y espacio de nombres de sockets del VNET queda conectada.

Para el autor de un driver, el traslado impone un requisito de ciclo de vida. La interfaz debe ser capaz de sobrevivir a la desconexión de un VNET y a la reconexión en otro. La función `if_init` del driver puede volver a llamarse en el nuevo contexto. La función `if_transmit` del driver puede recibir paquetes procedentes de los sockets del nuevo VNET. Cualquier estado que el driver almacene en caché sobre la pila de red "actual" (por ejemplo, búsquedas en la tabla de rutas) debe invalidarse o restablecerse.

Para un driver de red escrito siguiendo la interfaz estándar `ifnet(9)`, el traslado funciona normalmente sin necesidad de tratamiento especial. El framework de ifnet hace el trabajo pesado y el driver, en su mayor parte, no es consciente del cambio. Lo que el driver debe evitar es mantener referencias a estado con ámbito en el VNET entre puntos de entrada. Código como "obtengo un puntero a la tabla de rutas actual en el attach y lo guardo en caché" no sobrevive a un traslado de interfaz, porque la interfaz puede pertenecer después a un VNET diferente con una tabla diferente.

Una primitiva relacionada, `if_vmove_loan()`, se usa para interfaces que deben volver al host cuando la jail se apaga. La jail obtiene la interfaz en régimen de préstamo, y al destruir la jail la interfaz vuelve a trasladarse. Esto es habitual en configuraciones con `epair(4)` donde la conexión física (si la hay) pertenece al host y solo la presencia lógica pertenece a la jail.

### Hooks del ciclo de vida de VNET

Cuando se crea o destruye un VNET, los subsistemas que mantienen estado por VNET necesitan inicializarlo o liberarlo. Los macros `VNET_SYSINIT` y `VNET_SYSUNINIT` registran funciones que se invocan en esos momentos. Un protocolo de red puede registrar una función de init que crea tablas hash por VNET, y una función de uninit que las destruye.

Los autores de drivers raramente necesitan estos hooks. Son relevantes para los protocolos y las funcionalidades de la pila, no para los drivers de dispositivo. Se mencionan aquí porque los encontrarás repartidos por todo el código de la pila de red, y saber que son hooks del ciclo de vida de VNET te ayuda a leer el código fuente.

### Un patrón VNET concreto

Para hacer concretas las abstracciones de VNET, considera un pseudo-driver simplificado que mantiene un contador de paquetes recibidos por VNET. El contador debe ser por VNET porque el mismo pseudo-driver puede clonarse en varios VNETs simultáneamente, y cada clon debe tener su propio contador.

La declaración tiene este aspecto:

```c
#include <net/vnet.h>

VNET_DEFINE_STATIC(uint64_t, pseudo_rx_count);
#define V_pseudo_rx_count VNET(pseudo_rx_count)

/* Optional init for the counter */
static void
pseudo_vnet_init(void *unused)
{
    V_pseudo_rx_count = 0;
}
VNET_SYSINIT(pseudo_vnet_init, SI_SUB_PSEUDO, SI_ORDER_ANY,
    pseudo_vnet_init, NULL);
```

`VNET_DEFINE_STATIC` sitúa el contador en una sección por VNET de la imagen del kernel. Cuando se crea un nuevo VNET, el kernel copia esa sección en memoria nueva, de modo que cada VNET comienza con su propia copia del contador inicializada a cero. El acceso abreviado `V_pseudo_rx_count` es un macro que se expande a `VNET(pseudo_rx_count)`, que a su vez desreferencia el almacenamiento del VNET actual.

Cuando llega un paquete, la ruta de recepción incrementa el contador:

```c
static void
pseudo_receive_one(struct mbuf *m)
{
    V_pseudo_rx_count++;
    /* deliver packet to the stack */
    netisr_dispatch(NETISR_IP, m);
}
```

Esto parece código ordinario, porque el macro oculta la indirección por VNET. La condición para que sea correcto es que el thread ya esté en el contexto VNET adecuado cuando se llama a `pseudo_receive_one`. En la ruta de recepción de un driver de red, esa condición se cumple de forma automática: la pila de red llama al punto de entrada del driver con el contexto ya establecido correctamente.

Cuando se accede al contador desde un contexto inusual, el contexto debe establecerse de forma explícita:

```c
static void
pseudo_print_counter(struct vnet *vnet)
{
    uint64_t count;

    CURVNET_SET(vnet);
    count = V_pseudo_rx_count;
    CURVNET_RESTORE();
    printf("pseudo: vnet %p has received %lu packets\n", vnet, count);
}
```

Aquí la función se llama desde alguna ruta administrativa que no conoce el VNET actual, así que establece el contexto manualmente, lee el contador, restaura el contexto e imprime el resultado. Este es el patrón que verás repetido en el código con soporte para VNET.

### Lectura de código VNET real

Si quieres ver el patrón en un driver real, `/usr/src/sys/net/if_tuntap.c` es un buen punto de partida. Los drivers de clonación `tun` y `tap` son conscientes de VNET: cada clon pertenece a un VNET, y la creación o destrucción de clones respeta los límites del VNET. El código está bien comentado y es lo suficientemente pequeño como para leerlo en un par de tardes.

Merece la pena fijarse en dos patrones de `if_tuntap.c`. El primero es el uso de `V_tun_cdevsw` y `V_tap_cdevsw`, estructuras cdevsw de dispositivos de caracteres por VNET. Cada VNET tiene su propia copia del switch, de modo que `/dev/tun0` en un VNET puede apuntar a un clon subyacente diferente que `/dev/tun0` en otro VNET. Este es el tipo de duplicación por VNET de grano fino que el framework hace posible.

El segundo es el uso de `if_clone(9)` con VNET. Las funciones `if_clone_attach` e `if_clone_detach` tienen en cuenta el VNET de forma automática, de modo que un clon creado en un VNET vive en ese VNET hasta que se mueve o destruye explícitamente. El clonador no necesita llevar estado VNET en su softc; el framework se encarga de ello.

Estudiar estos patrones hace que el texto de este capítulo sea concreto. Lee, toma notas y vuelve al texto si algo no queda claro.

### Jails jerárquicas

Vale la pena mencionar brevemente las jails jerárquicas, una funcionalidad con la que algunos lectores se encontrarán. FreeBSD admite el anidamiento de jails: una jail puede crear jails hijas, y las jails hijas están acotadas por las restricciones de la jail padre. Esto resulta útil para servicios que quieren subdividir aún más su entorno.

Desde la perspectiva del autor de un driver, las jails jerárquicas no introducen nuevas APIs. El helper `prison_priv_check` recorre la jerarquía de forma automática: un privilegio se concede únicamente si todos los niveles de la jerarquía lo permiten. Un driver que usa el framework correctamente funciona en jails jerárquicas sin código adicional.

La parte administrativa es más compleja (la jail padre debe permitir la creación de jails hijas, y las hijas heredan un conjunto restringido de privilegios), pero el lado del driver no necesita preocuparse. Saber que la funcionalidad existe ayuda cuando encuentras jails anidadas en un despliegue.

### Juntando las piezas

Un sistema FreeBSD con jails y VNET es un sistema donde un único kernel da servicio a muchos entornos aislados. Cada entorno ve su propia vista del sistema de archivos, su propia tabla de procesos, sus propios dispositivos (filtrados por el ruleset de devfs) y, posiblemente, su propia pila de red (bajo VNET). El driver que les da servicio a todos es un único binario compartido, pero respeta el aislamiento porque llama correctamente a las APIs del framework.

Las APIs del framework para este aislamiento, `priv_check`, `prison_priv_check`, `CURVNET_SET`, `VNET_DEFINE` y los helpers de clonación, son pequeñas y autocontenidas. Un autor de driver que las aprende una vez puede escribir drivers que funcionen correctamente en cualquier configuración de jail que el administrador pueda imaginar. No es necesario tratar casos especiales para configuraciones concretas de jails; el framework se encarga de ese trabajo.

### Jails de pila única y el término medio

No toda jail necesita VNET. Una jail que ejecuta un servidor web que se comunica a través de un proxy inverso en el host puede funcionar perfectamente con una jail de pila única vinculada a una dirección IPv4 específica. El principal coste de VNET es la complejidad en la pila (cada protocolo debe ser consciente de VNET) y una cierta sobrecarga de memoria (cada VNET tiene sus propias tablas hash, cachés y contadores). Para jails ligeras, el modelo de pila única suele ser la mejor opción.

La disyuntiva para los autores de drivers merece conocerse. Un driver de red que funciona correctamente en una jail con la pila del host puede necesitar atención adicional para jails VNET, ya que la interfaz puede trasladarse bajo VNET pero no bajo el modelo de pila única. Diseñar el driver con VNET en mente desde el principio es el enfoque correcto; la disciplina adicional es pequeña y prepara el driver para el futuro.

### Cerrando

Las jails comparten el kernel del host y, por tanto, los drivers del host. Lo que no comparten es la visibilidad: los rulesets de devfs controlan qué nodos de dispositivo puede abrir una jail, y VNET controla qué interfaces de red puede usar una jail. Los autores de drivers se benefician de entender ambos mecanismos, ya que las decisiones de diseño que toman (cómo nombrar los nodos de devfs, cómo gestionar el contexto VNET en los callouts, cómo dar soporte al traslado de interfaces) afectan al comportamiento de su driver en entornos con jails.

Con el panorama de las jails completo, podemos plantearnos la pregunta complementaria: una vez que una jail tiene acceso a un dispositivo, ¿qué privilegios tiene para usarlo? La sección 7 aborda los límites de recursos y los límites de seguridad, y examina el otro lado de la política de jails.

## Sección 7: Límites de recursos, límites de seguridad y acceso del host frente a la jail

Un driver no es un objeto aislado. Es un consumidor de recursos del kernel y un proveedor de servicios para los procesos, y ambas relaciones están mediadas por los frameworks de seguridad y contabilidad del kernel. Cuando un driver se ejecuta en un host FreeBSD que contiene jails, el límite de seguridad se desplaza: algunos privilegios que son incondicionales en el host están restringidos para los procesos de la jail, y algunos recursos que no tienen medición en un sistema tradicional están ahora sujetos a límites por jail. Un buen autor de driver sabe dónde se encuentran esos límites, porque el comportamiento de su driver en un host no es siempre su comportamiento dentro de una jail.

Esta sección abarca tres temas. Primero, el framework de privilegios y cómo `prison_priv_check` lo remodela para las jails. Segundo, `rctl(8)` y cómo se aplican los límites de recursos a los recursos del kernel que pueden importar a un driver. Tercero, la distinción práctica entre adjuntar un driver desde dentro de una jail (lo que normalmente es imposible) y hacer que los servicios de un driver estén disponibles para una jail (que es el caso habitual).

### El framework de privilegios y prison_priv_check

FreeBSD usa un sistema de privilegios para tomar decisiones detalladas sobre lo que un proceso puede y no puede hacer. El UNIX tradicional tiene un único bit de privilegio (root frente a no root), y ese bit lo determina todo. FreeBSD refina esto con el framework `priv(9)`, que define una larga lista de privilegios con nombre. Cada privilegio cubre un tipo específico de operación. Cargar un módulo del kernel es `PRIV_KLD_LOAD`. Establecer el directorio raíz de un proceso es `PRIV_VFS_CHROOT`. Abrir un socket raw es `PRIV_NETINET_RAW`. Configurar la dirección MAC de una interfaz es `PRIV_NET_SETLLADDR`. Usar un dispositivo BPF para la captura de paquetes es `PRIV_NET_BPF`.

Un proceso que es root (uid 0) tiene todos estos privilegios en el host. Un proceso que es root de una jail tiene algunos de ellos, pero no todos. La restricción la gestiona `prison_priv_check(cred, priv)`: recibe la credencial y el nombre del privilegio, y devuelve cero si el privilegio se concede y un error (habitualmente `EPERM`) si se deniega. La ruta de comprobación de privilegios del kernel está estructurada de modo que, para una credencial con jail, se llama primero a `prison_priv_check`; si deniega el privilegio, quien ha realizado la llamada devuelve `EPERM` sin más trámite.

Los privilegios que se permite ejercer a una jail se determinan por dos factores. El primero es una lista codificada en el interior de `prison_priv_check`: algunos privilegios simplemente nunca se conceden a las jails, independientemente de la configuración. Entre los ejemplos se encuentran `PRIV_KLD_LOAD` (cargar módulos del kernel) y `PRIV_IO` (acceso a puertos I/O). El segundo son los parámetros `allow.*` de `jail.conf(5)`, que activan o desactivan categorías específicas. `allow.raw_sockets` (desactivado por defecto) controla `PRIV_NETINET_RAW`. `allow.mount` (desactivado por defecto) controla los privilegios de montaje del sistema de archivos. `allow.vmm` (desactivado por defecto) controla el acceso a `vmm(4)` para ejecutar hipervisores anidados. Los valores por defecto se inclinan por la denegación: si no lo permites explícitamente, la jail no lo obtiene.

Para quien escribe un driver, el marco de privilegios cobra importancia cada vez que el driver realiza alguna operación que un proceso puede o no estar autorizado a efectuar. Un driver que implementa una interfaz hardware de bajo nivel podría requerir `PRIV_DRIVER` (el privilegio comodín para comprobaciones específicas del driver) o un privilegio más concreto. Un driver que expone un dispositivo de caracteres cuyos `ioctl`s pueden reconfigurar el hardware llamará a `priv_check(td, PRIV_DRIVER)` (o a un nombre más específico) para decidir si el llamante está autorizado a realizar esa reconfiguración.

El patrón habitual en el código de un driver tiene el siguiente aspecto:

```c
static int
mydev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
    int error;

    switch (cmd) {
    case MYDEV_CMD_RECONFIGURE:
        error = priv_check(td, PRIV_DRIVER);
        if (error != 0)
            return (error);
        /* do the reconfiguration */
        break;
    ...
    }
}
```

`priv_check(td, PRIV_DRIVER)` hace lo correcto tanto para los llamantes del sistema anfitrión como para los de una jail. En el sistema anfitrión, un proceso con privilegios de root supera la comprobación; sin ellos, el proceso es rechazado (salvo que el driver conceda permiso por otros medios). Dentro de una jail, se consulta `prison_priv_check` y, por defecto, `PRIV_DRIVER` se deniega en su interior. Si el administrador ha configurado la jail para permitir el acceso al driver (una configuración muy inusual), el privilegio se concede y la llamada prosigue.

El resultado es que un driver que usa `priv_check` correctamente obtiene compatibilidad con jails de manera automática. El driver no necesita saber si el llamante se encuentra dentro de una jail; simplemente pregunta si ese llamante posee el privilegio adecuado, y el framework se encarga del resto.

### Privilegios con nombre más relevantes para drivers

`PRIV_IO` es para el acceso directo a puertos de I/O en x86. Está definido en `/usr/src/sys/sys/priv.h` y se deniega a los jails de forma incondicional. Los drivers que ofrecen acceso directo a puertos de I/O al espacio de usuario son raros (por lo general, limitados a hardware legado como `/dev/io`), pero cuando existen, utilizan este privilegio.

`PRIV_DRIVER` es el comodín para privilegios específicos del driver. Si un driver necesita proteger un `ioctl` que solo debería invocar un administrador, `PRIV_DRIVER` es la opción predeterminada.

`PRIV_KMEM_WRITE` controla el acceso de escritura a `/dev/kmem`. Al igual que `PRIV_IO`, se deniega a los jails. Escribir en la memoria del kernel es la operación privilegiada por antonomasia; ninguna política de contenedor razonable lo permite.

`PRIV_NET_*` es una familia de privilegios relacionados con la red. `PRIV_NET_IFCREATE` sirve para crear interfaces de red; `PRIV_NET_SETLLADDR` para cambiar direcciones MAC; `PRIV_NET_BPF` para abrir un dispositivo BPF. Cada uno tiene su propia política de jail, y las combinaciones son las que permiten, por ejemplo, que un jail VNET ejecute `dhclient(8)` (que necesita `PRIV_NET_BPF`) sin poder reconfigurar interfaces de forma arbitraria.

`PRIV_VFS_MOUNT` controla el montaje de sistemas de archivos. Los jails tienen una versión muy restringida de este privilegio por defecto: pueden montar `nullfs` y `tmpfs` si `allow.mount` está activado, pero no sistemas de archivos arbitrarios.

La lista completa está en `/usr/src/sys/sys/priv.h`. En el desarrollo de drivers, rara vez necesitarás inventar nuevas categorías de privilegios; elegirás la que ya existe y que mejor se adapta.

### rctl(8): límites de recursos por jail y por proceso

Los jails (y, en realidad, también los procesos) pueden estar sujetos a límites de recursos más allá del modelo tradicional de UNIX `ulimit(1)`. El framework `rctl(8)` de FreeBSD (el marco de control de recursos en tiempo de ejecución) permite a un administrador establecer límites sobre una gran variedad de recursos y aplicar acciones específicas cuando se alcanzan esos límites.

Los límites abarcan cuestiones como el uso de memoria, el tiempo de CPU, el número de procesos, el número de archivos abiertos, el ancho de banda de I/O, y más. Pueden aplicarse por usuario, por proceso, por clase de login o por jail. El uso típico en una configuración de jails es limitar el total de memoria y CPU de un jail para que una aplicación que se comporte mal dentro del jail no afecte a los demás jails del mismo host.

Para el autor de un driver, `rctl(8)` importa por una razón sutil. Los drivers asignan recursos en nombre de los procesos. Cuando un driver llama a `malloc(9)` para asignar un buffer, la memoria va a algún sitio. Cuando un driver crea un descriptor de archivo al abrir un archivo internamente, el descriptor va a algún sitio. Cuando un driver lanza un thread del kernel, ese thread se ejecuta como parte de la contabilidad de alguien. Si ese «alguien» es un proceso dentro de un jail, la contabilidad podría alcanzar los límites `rctl` del jail.

Habitualmente, esto es exactamente lo que se desea. Si se supone que un jail está limitado a 100 MB de memoria, y una asignación en nombre de un proceso del jail debería contar contra ese límite, y `rctl` alcanza el límite, la asignación debería fallar con `ENOMEM`. Tu driver propaga entonces el fallo al espacio de usuario, y la aplicación bien escrita dentro del jail lo gestiona.

En ocasiones, la contabilidad es menos obvia. Un driver que mantiene un pool de buffers compartidos entre todos los llamadores cargará ese pool al kernel de forma predeterminada, en lugar de a un proceso concreto. Eso está bien, pero significa que el pool no está medido: un jail muy activo puede consumir una parte mayor del pool de la que «debería» según los límites de recursos. Para la mayoría de los drivers esto es aceptable, pero para drivers cuyos recursos son costosos (buffers DMA grandes, por ejemplo) puede valer la pena plantearse si el recurso debería rastrearse por proceso mediante `racct(9)`, la capa de contabilidad subyacente sobre la que se asienta `rctl`.

El framework `racct(9)` expone las funciones `racct_add(9)` y `racct_sub(9)` para los drivers que quieran participar en la contabilidad. La mayoría de los drivers nunca las llaman directamente. Añadir soporte `racct` es una decisión de diseño deliberada, que suele tomarse cuando el consumo de recursos de un driver es lo suficientemente grande como para importar en conjunto. Para drivers de dispositivos de caracteres o drivers de red habituales, la contabilidad predeterminada que realiza el kernel (memoria de buffer por socket, contadores de descriptores de archivo por proceso, etc.) es suficiente.

### Acciones de cumplimiento y lo que significan para los drivers

Cuando se alcanza un límite de recursos, `rctl(8)` puede hacer varias cosas: denegar la operación, enviar una señal al proceso infractor, registrar el evento o aplicar limitación de tasa (en el caso de recursos con tasa). El cumplimiento lo gestiona la capa de contabilidad del kernel, no los drivers. Lo que los drivers ven es el resultado: una asignación falla, llega una señal, una operación con limitación de tasa tarda más.

Para un driver, la implicación práctica es que toda asignación y toda adquisición de recursos debe estar escrita para gestionar fallos. Esto no es específico de los jails ni de `rctl`; es simplemente una buena programación defensiva. Un `malloc(9)` con `M_WAITOK` puede esperar memoria indefinidamente, pero en un jail con límite de memoria puede fallar de todos modos (si `M_WAITOK` no está establecido) o puede bloquearse durante mucho tiempo esperando memoria que nunca se liberará (porque ningún otro proceso del jail tiene memoria que liberar).

La regla general: si tu driver realiza asignaciones en nombre de un proceso de usuario, plantéate si `M_NOWAIT` es más apropiado que `M_WAITOK`, y si el llamador puede tolerar una asignación retrasada o fallida. Los jails (y los límites de recursos que los rodean) hacen que esta consideración sea más que teórica.

### Drivers del host frente a procesos del jail

Una pregunta recurrente para los autores de drivers es: ¿puede cargarse o vincularse mi driver desde dentro de un jail? La respuesta corta es casi siempre no. Cargar módulos del kernel (`kldload(2)`) requiere `PRIV_KLD_LOAD`, que nunca se concede a los jails. Un jail que necesite acceder a los servicios de un driver debe tener ese driver ya cargado y vinculado en el host, y entonces el jail puede usar el driver a través de las interfaces habituales del espacio de usuario.

Esto es consecuencia del modelo de kernel único. Un jail no tiene su propio kernel, por lo que no puede cargar sus propios drivers. Lo que tiene es acceso (sujeto al conjunto de reglas devfs) a los drivers que el host ha cargado. En la práctica, esto significa:

- La carga y descarga de drivers se producen en el host. El administrador del host es responsable de `kldload` y `kldunload`.
- La vinculación de dispositivos ocurre en el host. El método `DEVICE_ATTACH` del driver se ejecuta en contexto del host, no en contexto del jail.
- El acceso al dispositivo desde el espacio de usuario ocurre dentro del jail, a través de `/dev` (si el conjunto de reglas lo permite) y a través de los métodos `open`/`read`/`write`/`ioctl` del driver.

La separación suele ser limpia. El driver no necesita saber si su llamador está en un jail; el kernel gestiona el contexto. Donde a veces importa es en los manejadores de `ioctl` que quieren distinguir entre llamadores del host y del jail, o en drivers que asignan recursos por apertura cuya política de liberación difiere.

Una advertencia específica: cuando un driver crea estado al abrir, ese estado persiste hasta que se cierra. Si el jail que tenía el descriptor de archivo abierto desaparece antes de que se cierre el descriptor (porque el jail fue destruido mientras los procesos aún tenían el dispositivo abierto), el kernel cerrará el descriptor en nombre del jail. El método `close` del driver se ejecutará en un contexto seguro. Pero el driver no debe asumir que el jail sigue existiendo durante el cierre; puede que no, y si el driver intenta acceder al estado del jail, encontrará un `struct prison` ya liberado. La regla clara es que `close` solo debe tocar el estado del driver, no el estado del jail.

### Cómo cooperan los drivers con los jails en la práctica

Reuniendo todas las piezas, una configuración típica de FreeBSD con un driver y un jail tiene este aspecto.

1. El administrador carga el driver en el host, ya sea en el boot mediante `/boot/loader.conf` o en tiempo de ejecución mediante `kldload(8)`.
2. Las funciones probe y attach del driver se ejecutan en el host, crean los nodos de `devfs` apropiados y registran sus `cdev_methods`.
3. El administrador crea un jail, posiblemente con un conjunto de reglas devfs específico y posiblemente con VNET.
4. Los procesos del jail abren los dispositivos del driver (si son visibles a través del conjunto de reglas) y realizan llamadas `ioctl` o `read`/`write`.
5. Los métodos del driver se ejecutan en nombre del proceso del jail, con la credencial del jail asociada al thread, y las llamadas `priv_check` del driver devuelven correctamente `EPERM` para los privilegios que el jail no posee.
6. Cuando el jail se destruye, los descriptores de archivo abiertos se cierran, los métodos `close` del driver se ejecutan limpiamente y el estado del driver vuelve a su vista normal solo del host.

Nada de este flujo requiere que el driver conozca los jails de forma explícita. El driver es un participante pasivo que llama a las funciones correctas del framework, y el framework se encarga del resto. Este es el diseño más limpio posible, y es lo que deberías perseguir.

### Los frameworks de contenedores: ocijail y pot

La infraestructura de jail de FreeBSD es un mecanismo del kernel; las herramientas del espacio de usuario que la rodean tienen múltiples formas. El sistema base proporciona `jail(8)`, `jail.conf(5)`, `jls(8)` y herramientas relacionadas. Con estas es suficiente para gestionar jails a mano o con scripts de shell.

Por encima de ellas han surgido frameworks de contenedores de más alto nivel. `ocijail` pretende ofrecer un runtime OCI (Open Container Initiative) que usa jails como mecanismo de aislamiento, lo que permite que FreeBSD participe en ecosistemas de contenedores que utilizan imágenes compatibles con OCI. `pot` (disponible desde ports) es un gestor de contenedores más nativo de FreeBSD que agrupa un jail con una capa de sistema de archivos, una configuración de red y un ciclo de vida. Ambos son externos al sistema base y se instalan a través de la Ports Collection o paquetes.

Para un autor de drivers, estos frameworks no cambian los fundamentos. Siguen usando jails por debajo; siguen dependiendo de los conjuntos de reglas devfs y VNET para el aislamiento; siguen respetando el mismo framework de privilegios. Lo que cambian es la forma en que los administradores describen y despliegan los contenedores, no la manera en que los drivers interactúan con ellos. Un driver que funciona con un `jail.conf(5)` construido a mano funcionará también con `ocijail` y `pot`.

Lo único que suele necesitar conocer un autor de drivers es que estos frameworks existen y son cada vez más habituales. Si la documentación de tu driver menciona jails, señala que las recomendaciones se aplican igualmente a los frameworks de contenedores construidos sobre ellos. Esa única frase le ahorra a los administradores muchos quebraderos de cabeza.

### Cerrando

Los jails son un límite de política alrededor de un kernel compartido. La política se extiende en tres dimensiones: qué dispositivos son visibles (conjuntos de reglas devfs), qué privilegios se conceden (`prison_priv_check` y parámetros `allow.*`), y qué recursos se pueden consumir (`rctl(8)`). Los autores de drivers se encuentran con cada una de estas dimensiones de forma pequeña y local: `priv_check` para operaciones privilegiadas, nomenclatura sensata de los nodos `devfs`, gestión elegante de fallos de asignación. No hay una nueva API grande que aprender; hay pequeños hábitos nuevos que adquirir.

Con la imagen de seguridad y recursos cubierta, el último tema conceptual es cómo probar y desarrollar drivers en entornos virtualizados y contenerizados. La Sección 8 reúne las ideas del capítulo en un flujo de trabajo de desarrollo.

## Sección 8: Pruebas y refactorización de drivers para entornos virtualizados y contenerizados

Un driver que funciona en hardware real es un driver que funciona en una sola configuración. Un driver que funciona a través de entornos de virtualización y contenerización ha sido ejercitado bajo condiciones variadas: diferentes presentaciones de bus, diferentes mecanismos de entrega de interrupciones, diferentes comportamientos de mapeo de memoria, diferentes contextos de privilegios. El driver que supera todo eso sin cambios es el driver que también sobrevivirá al próximo entorno nuevo. Esta sección describe el flujo de trabajo de desarrollo y pruebas que te lleva hasta ahí.

El flujo de trabajo tiene tres capas. La capa de desarrollo utiliza una VM como host del kernel desechable, de modo que los panics y los bloqueos no te cuestan nada. La capa de integración utiliza dispositivos VirtIO, passthrough y jails para ejercitar el driver en entornos realistas. La capa de regresión utiliza automatización para ejecutar toda la suite repetidamente a medida que el driver evoluciona.

### Usar una VM como host de desarrollo

Cuando estás escribiendo un módulo del kernel, el coste de un pánico es tu sesión. En una máquina de desarrollo bare-metal, un pánico interrumpe tu trabajo, posiblemente obliga a comprobar el sistema de archivos y puede requerir un reinicio con pasos de recuperación manuales. En una VM, un pánico es un mero detalle: la VM se detiene, la reinicias y el host queda intacto.

Por este motivo, los autores de drivers con experiencia realizan casi todo el desarrollo de nuevos drivers dentro de una VM basada en `bhyve(8)` o QEMU, no en bare metal. El flujo de trabajo es el siguiente:

1. Se instala una VM con FreeBSD 14.3 en `bhyve(8)` con una imagen de disco estándar.
2. El árbol de código fuente está en el propio disco de la VM o montado vía NFS desde una máquina de compilación en el host.
3. El driver se compila dentro de la VM (`make clean && make`) o en el host y se copia.
4. `kldload(8)` carga el módulo. Si el módulo provoca un pánico en el kernel, la VM se bloquea y se reinicia.
5. Una vez que el módulo carga limpiamente, se ejercita contra el sistema de prueba que tengas: un dispositivo VirtIO, un modo loopback o un objetivo en passthrough.

El punto clave es que la iteración es rápida. Un módulo defectuoso que dejaría una máquina bare-metal sin posibilidad de arrancar es una pequeña molestia en una VM. Puedes probar cosas que nunca intentarías en una máquina de la que dependes.

En el desarrollo de drivers VirtIO en concreto, la VM no es solo conveniente; es la única plataforma sensata. Los dispositivos VirtIO solo existen dentro de VMs (o bajo la emulación de QEMU), por lo que la VM es donde están los dispositivos. Arrancar una VM con un dispositivo virtio-rnd, o virtio-net, o virtio-console, te proporciona un objetivo contra el que desarrollar. El hipervisor proporciona todo lo que un dispositivo real ofrecería, incluyendo interrupciones, DMA y acceso a registros, de modo que el driver que escribes dentro de la VM es el mismo que ejecutará en cualquier otro lugar.

### Usar VirtIO como sustrato de prueba

VirtIO tiene un segundo papel más allá de ser el objetivo para los drivers VirtIO: es un sustrato de prueba. Como los dispositivos VirtIO son fáciles de definir, fáciles de emular y están bien documentados, resultan útiles para construir escenarios de prueba controlados incluso para drivers que no son drivers VirtIO.

Por ejemplo, supón que estás escribiendo un driver para un dispositivo PCI físico y quieres probar cómo gestiona tu driver una condición de error específica. En el hardware real, reproducir el error puede requerir un fallo físico concreto, que es difícil de provocar. En un proxy basado en VirtIO, puedes implementar un dispositivo que siempre devuelva el error y probar la ruta de error de tu driver sin tocar el hardware físico. La salvedad es que tu driver debe estar débilmente acoplado al hardware específico (aquí importan las técnicas del capítulo 29); cuanto más esté dividido el driver según las líneas de accessor/backend descritas allí, más fácil será probar las capas superiores contra un backend sintético.

El mecanismo para sintetizar dispositivos VirtIO en espacio de usuario son los propios dispositivos emulados enchufables de `bhyve`. Escribir un nuevo emulador de dispositivo para `bhyve` va más allá del alcance de este capítulo, pero el código relevante se encuentra en `/usr/src/usr.sbin/bhyve/` y es asequible si tienes conocimientos básicos de C. Para casos más sencillos, usar un dispositivo virtio-blk, virtio-net o virtio-console preexistente configurado con parámetros específicos suele ser suficiente.

### Usar jails para pruebas de integración

Cuando tu driver funciona y quieres verificarlo bajo aislamiento de tipo contenedor, los jails son el siguiente paso evidente. La configuración es sencilla: crea un jail con un conjunto de reglas de devfs adecuado que exponga tu dispositivo y ejecuta tu arnés de pruebas en espacio de usuario dentro del jail.

Una estructura de prueba típica:

```text
myjail {
    path = /jails/myjail;
    host.hostname = myjail;
    devfs_ruleset = 100;   # custom ruleset that unhides /dev/mydev
    ip4 = inherit;
    allow.mount = 0;
    exec.start = "/bin/sh /etc/rc";
    exec.stop = "/bin/sh /etc/rc.shutdown";
    persist;
}
```

Dentro del jail, ejecutas tu arnés de pruebas. Abre `/dev/mydev`, ejercita los `ioctl`s o los métodos `read`/`write` y registra los resultados. En el host, ejecutas el mismo arnés y comparas. Si la prueba del lado del jail pasa y la prueba del lado del host pasa, tu driver tolera el entorno de jail.

Si una pasa y la otra no, tienes una oportunidad diagnóstica. Las posibles razones de una divergencia incluyen: una comprobación de privilegios que deniega la llamada del jail (busca `priv_check` en tu driver), un permiso de nodo de dispositivo que el conjunto de reglas no contempla, un límite de recursos que el host evita de casualidad o una ruta de código específica del jail en tu driver que existe por error. Cada uno de estos problemas es corregible una vez identificado.

### Usar jails VNET para pruebas de drivers de red

Para los drivers de red, la prueba es similar pero usa VNET. Crea un jail con `vnet = 1` en `jail.conf(5)`, mueve un extremo de un `epair(4)` dentro del jail y genera tráfico entre el jail y el host. Si tu driver es un driver de red físico, también puedes mover la interfaz física al jail para una prueba de aislamiento completo.

La prueba VNET ejercita el ciclo de vida de `if_vmove()`: la interfaz se desvincula del VNET del host, se vuelve a vincular al VNET del jail y finalmente se devuelve. Un driver que supera esto sin perder estado es un driver que tolera VNET. Un driver que entra en pánico, se bloquea o deja de entregar paquetes después de un move tiene trabajo pendiente.

Los modos de fallo más comunes en las pruebas VNET son:

- El driver mantiene un puntero al ifnet o VNET del host durante el move y lo usa desde un callout después de que el move ha ocurrido.
- El `if_init` del driver asume que se llama en el VNET original y falla cuando se llama en el nuevo.
- El driver limpia incorrectamente en `if_detach`, porque no distingue «detach para mover» de «detach para destruir».

Cada uno de estos problemas se puede diagnosticar con `dtrace(1)` o printfs del kernel en el lugar adecuado. La primera vez que veas un fallo relacionado con VNET, busca el punto del driver donde ocurre el move y trabaja hacia atrás desde ahí.

### Pruebas de passthrough

El passthrough PCI es el ejercicio que valida la ruta de detach de tu driver. Crea un huésped `bhyve(8)` con tu dispositivo en passthrough, instala FreeBSD dentro y carga tu driver allí. Si el driver se vincula limpiamente en el huésped, la configuración de tu dispositivo y el código DMA gestionan correctamente la reasignación IOMMU. Si el driver carga, la prueba es sencilla: ejecuta la carga de trabajo normal del driver dentro del huésped.

La prueba de detach es la más difícil. Apaga el huésped, vuelve a vincular el dispositivo a su driver nativo en el host (descargando `ppt(4)` de ese dispositivo si es necesario y dejando que el driver del host se vuelva a vincular) y ejercita el driver en el host. Si el driver se vincula limpiamente después de que el huésped haya usado el dispositivo y lo haya sometido a los cambios de estado que sean, el `DEVICE_ATTACH` del driver es correctamente defensivo. Si falla, busca suposiciones sobre el estado inicial del dispositivo que no deberían ser suposiciones.

El ciclo completo de ida y vuelta (host, huésped, host) es el estándar de oro para la compatibilidad con passthrough. Un driver que lo supera puede entregarse a cualquier administrador con confianza.

### Detección del hipervisor en las pruebas

Si tu driver usa `vm_guest` para ajustar los valores predeterminados, prueba el ajuste. Ejecuta el driver en bare metal (si está disponible), dentro de `bhyve(8)`, dentro de QEMU/KVM, y observa si los valores predeterminados que elige tienen sentido. El sysctl `kern.vm_guest` es tu comprobación rápida:

```sh
sysctl kern.vm_guest
```

Si tu driver registra su entorno en el momento de la vinculación («attaching on bhyve host, defaulting to X»), el registro hace visible la detección, lo que ayuda con la depuración. No registres en exceso: una vez en el attach suele ser suficiente.

### Automatizar la suite de pruebas

Una vez que se conocen las pruebas individuales, el siguiente paso es ejecutarlas repetidamente a medida que el driver evoluciona. El ejecutor de pruebas `kyua(1)` de FreeBSD, combinado con el framework de pruebas `atf(7)`, es el mecanismo estándar. Una suite de pruebas que incluya una «prueba de bare metal», una «prueba de huésped VirtIO», una «prueba de jail VNET» y una «prueba de passthrough» cubre la mayor parte de lo que quieres verificar.

Los detalles de cómo escribir pruebas en `atf(7)` están fuera de este capítulo; se tratan con mayor profundidad en el capítulo 32 (Depuración de drivers) y el capítulo 33 (Pruebas y validación). Lo importante por ahora es que la suite de pruebas debe ejercitar el driver en los entornos en los que se espera que ejecute. Una sola prueba en bare metal demuestra muy poco sobre la virtualización; una suite de pruebas en múltiples entornos demuestra algo sobre la portabilidad.

### Consejos de refactorización revisados

En el capítulo 29 introdujimos una disciplina para la portabilidad: capas de accessor, abstracciones de backend, ayudantes de endianness y ninguna suposición oculta de hardware. La virtualización y la contenerización ponen a prueba esa disciplina. Un driver escrito con abstracciones limpias sobrevivirá a la variedad de entornos descritos en este capítulo; un driver con suposiciones ocultas las encontrará en cuanto el entorno cambie.

Los consejos de refactorización más relevantes para este capítulo son:

- Pasa todo el acceso a registros a través de accessors, de modo que la ruta de acceso pueda simularse o redirigirse en las pruebas.
- Gestiona el ciclo de vida completo: attach, detach, suspend, resume. El passthrough ejercita attach y detach repetidamente; VNET ejercita detach de una manera que el driver puede que no vea en bare metal.
- Usa `bus_dma(9)` y `bus_space(9)` correctamente, nunca direcciones físicas directamente. La traducción de direcciones huésped frente a host bajo passthrough depende del uso correcto de estas APIs.
- Usa `priv_check` para el control de privilegios, no comprobaciones codificadas de uid 0. Las restricciones de jail funcionan correctamente solo si se llama al framework.
- Usa `CURVNET_SET` alrededor de cualquier código de callout o taskqueue que toque el estado de la pila de red. Esta es la única disciplina específica de VNET que sorprende a la mayoría de los autores de drivers.

Ninguno de estos son conceptos nuevos. Todos son práctica estándar de drivers en FreeBSD. Lo que este capítulo añade es el contexto en el que cada uno importa: qué entornos ejercitan qué disciplinas. Saberlo te permite priorizar a la hora de decidir qué refactorizar primero.

### Un orden de desarrollo que funciona

Uniendo las piezas, aquí tienes un orden de desarrollo que ha demostrado ser efectivo.

1. Empieza en una VM de `bhyve(8)`. Escribe el esqueleto básico del driver (ganchos de módulo, probe y attach, ruta de I/O sencilla). Ejercítalo con `kldload` y una prueba mínima.
2. Añade la capa de accessor y la abstracción de backend del capítulo 29. Comprueba que el backend de simulación funciona, aunque el hardware real no esté aún conectado.
3. Si VirtIO es el objetivo, desarrolla contra `virtio_pci.c` en la VM. Tienes un dispositivo real con el que comunicarte y puedes iterar rápidamente.
4. Si el hardware real es el objetivo, comienza las pruebas de passthrough PCI cuando el driver alcance un punto estable. El ciclo de ida y vuelta (host, huésped, host) pasa a formar parte del ciclo de pruebas habitual.
5. Añade pruebas basadas en jail cuando el driver exponga interfaces en espacio de usuario. Empieza con un jail de pila única; pasa a VNET si el driver es un driver de red.
6. Añade automatización con `kyua(1)` y `atf(7)` a medida que crece el número de pruebas.
7. Cuando se encuentre un bug, reprodúcelo en el entorno más pequeño que muestre el bug, corrígelo y añade una prueba de regresión a ese nivel.

Este orden mantiene la iteración rápida al principio (donde más importa) y añade complejidad ambiental solo a medida que el driver se estabiliza. Intentar probar todo a la vez es un error común de principiantes; así es como los proyectos se estancan. El camino incremental es más lento por paso pero mucho más rápido en conjunto.

### Un ejemplo de extremo a extremo: de bare metal a passthrough

Para ilustrar el flujo de trabajo, aquí tienes un recorrido de ejemplo completo para un driver hipotético llamado `mydev`. El driver es un dispositivo de caracteres basado en PCI; tiene una pequeña interfaz de registros, usa interrupciones MSI-X y realiza DMA. El orden de desarrollo que sigue está condensado en una sola narrativa para que puedas ver cómo se conectan los pasos.

Día 1: esqueleto en una VM. Instalas un huésped FreeBSD 14.3 en `bhyve(8)`, configuras NFS para que el árbol de código fuente en tu host sea visible en el huésped y escribes el esqueleto del módulo. Es un Makefile con `KMOD=mydev, SRCS=mydev.c` y un `mydev.c` con `DECLARE_MODULE`, un stub de probe, un stub de attach y un stub de detach. Carga y descarga limpiamente. `dmesg` muestra «mydev: hello» al cargar.

Día 2: capa de acceso. Añades el patrón de acceso del capítulo 29: todo el acceso a registros pasa por `mydev_reg_read32` y `mydev_reg_write32`, y el backend real llama a `bus_read_4` y `bus_write_4`. También añades un backend de simulación que almacena los valores de los registros en un pequeño array en memoria. El backend de simulación se selecciona mediante un parámetro del módulo. La capa de acceso hace posible que el mismo driver se ejecute contra un dispositivo real o contra el backend simulado sin ningún cambio en el código de la capa superior.

Día 3: código de la capa superior. Añades la lógica principal del driver: la inicialización, la interfaz de dispositivo de caracteres (`open`, `close`, `read`, `write`), la superficie `ioctl` y la configuración de DMA. El backend de simulación no modela DMA, pero la capa superior está estructurada para tratar el DMA a través de handles de `bus_dma(9)`, de modo que el código queda escrito correctamente desde el principio. Ejercitas el código a través del backend de simulación con un programa de prueba sencillo: abres el dispositivo, emites `ioctl`s y verificas las respuestas.

Día 4: hardware real, configuración de passthrough. Tienes el hardware objetivo en una estación de trabajo. Añades `pptdevs` a `/boot/loader.conf`, reinicias y confirmas que `ppt(4)` ha reclamado el dispositivo. Añades `passthru` a la configuración del invitado de `bhyve(8)` y arrancas el invitado. Dentro del invitado, cargas tu driver. El driver se enlaza al dispositivo pasado en modo passthrough. Has demostrado que la ruta de hardware real funciona.

Día 5: pruebas de interrupciones y DMA. El driver recibe interrupciones; el código de configuración de MSI-X funciona. Pruebas el DMA: una lectura DMA corta funciona, una lectura DMA larga funciona y una lectura y escritura simultáneas también funcionan. Encuentras un bug: el driver programa una dirección física calculada incorrectamente, pero solo para las regiones DMA que cruzan un límite de página. Lo corriges. Tiempo total dedicado a la depuración: dos horas, todo en una VM que habría requerido un reinicio forzado de la estación de trabajo sobre bare metal.

Día 6: prueba en jail. Sales del invitado, vuelves al host y configuras un jail que tiene acceso a `/dev/mydev0`. Tu programa de prueba se ejecuta dentro del jail y ejercita el driver exactamente igual que lo hacía en el host. Un `ioctl` falla con `EPERM`; revisas el driver y descubres que olvidaste añadir un `priv_check` para una operación que debería requerir privilegio. Añades la comprobación y el jail se comporta correctamente (el ioctl es denegado para los llamadores sin privilegio; el root del host puede seguir ejecutándolo).

Día 7: prueba VNET (si el driver tiene una interfaz de red). Creas un jail VNET y mueves una interfaz clonada a él. La interfaz funciona. Te das cuenta de que uno de tus callouts no establece `CURVNET_SET` antes de acceder a un contador por VNET; lo corriges. El callout funciona ahora tanto en el VNET del host como en el VNET del jail sin interferencias.

Día 8: ciclo completo de ida y vuelta. Destruyes el jail, apagas el invitado, descargas `ppt(4)` del dispositivo (o reinicias sin `pptdevs`) y esperas a que el driver del host vuelva a enlazarse. El attach se realiza limpiamente. Ejercitas el dispositivo en el host. Funciona. El ciclo de ida y vuelta host-invitado-host está completo.

Este ciclo de ocho días es una versión estilizada del desarrollo real; los resultados variarán según el caso. Lo importante es que el trabajo de cada día se apoya en el del anterior, y los entornos de prueba se vuelven más exigentes a medida que el driver se estabiliza. Para el octavo día, has ejercitado el driver en todos los entornos que verá en producción y has corregido los bugs que cada entorno expone. Lo que queda son las pruebas de soak y el pulido orientado al usuario, temas que se tratan en los capítulos 33 y 34.

### Medición de la sobrecarga de virtualización

Una nota rápida sobre rendimiento. Los drivers que funcionan bajo virtualización muestran en ocasiones una sobrecarga medible en comparación con el bare metal. Las fuentes de sobrecarga incluyen las salidas de VM en operaciones de I/O, la entrega de interrupciones a través del hipervisor y la traducción de direcciones DMA mediante el IOMMU.

Para la mayoría de los drivers en la mayoría de las situaciones, la sobrecarga no es significativa. El hardware moderno acelera casi todos los tramos de la ruta de virtualización (posted interrupts, tablas de páginas EPT, SR-IOV), y la fracción de tiempo de CPU que se invierte en código del hipervisor suele quedar en un solo dígito bajo. Para los drivers sensibles al rendimiento, sin embargo, medir y comprender la sobrecarga es esencial.

Las herramientas de medición son las mismas que se usan en cualquier otro trabajo de rendimiento. `pmcstat(8)` muestrea contadores de hardware, incluidos los contadores de salidas de VM y de fallos en la translation-lookaside-buffer que el IOMMU puede provocar. `dtrace(1)` puede rastrear rutas específicas del kernel y, con el proveedor `fbt`, puedes medir con qué frecuencia se entra en cada ruta y cuánto tiempo tarda. `vmstat -i` muestra las tasas de interrupciones.

Para los drivers VirtIO, la fuente de sobrecarga más común son las notificaciones excesivas. Cada `virtqueue_notify` provoca potencialmente una salida de VM, y un driver que notifica en cada paquete en lugar de agruparlos puede generar cientos de miles de salidas por segundo. La característica `VIRTIO_F_RING_EVENT_IDX`, si se negocia, permite que el invitado y el host cooperen para reducir la frecuencia de notificaciones. Asegúrate de que tu driver negocia esta característica si opera en una ruta de alta tasa de paquetes.

Para los drivers de passthrough, la fuente de sobrecarga más común son los fallos de traducción del IOMMU. Cada buffer DMA debe recorrerse a través de las tablas de páginas del IOMMU, y un driver que mapea y desmapea buffers muchas veces por segundo dedica una cantidad considerable de tiempo de CPU a ello. La solución habitual es mantener los mapeos DMA activos durante más tiempo (usando las funciones de retención de mapeos de `bus_dma(9)`) en lugar de mapear y desmapear en cada transacción.

El ajuste de rendimiento es un capítulo completo por sí mismo (el capítulo 34). Por ahora, la conclusión es que la virtualización tiene costes medibles, esos costes suelen ser pequeños y las herramientas estándar de FreeBSD se aplican sin modificación.

### En resumen

Un driver que funciona correctamente bajo virtualización y contenedorización no es el resultado de código especial de virtualización. Es el resultado de la disciplina estándar en el desarrollo de drivers para FreeBSD, ejercida en entornos que exponen suposiciones ocultas. El flujo de trabajo de prueba y desarrollo de esta sección es la vertiente práctica de esa disciplina: una VM para iteración rápida, VirtIO como sustrato de prueba controlado, jails para comprobaciones de privilegios y visibilidad, VNET para probar la pila de red, passthrough para rondas con el hardware y automatización para mantener todo bajo control.

Con el material conceptual y práctico en su lugar, el resto del capítulo se centra en laboratorios prácticos que te permiten probar estas técnicas por tu cuenta. Antes de llegar a ellos, dos secciones más completan el panorama. La sección 9 cubre los temas más discretos pero igualmente importantes del tiempo, la memoria y el manejo de interrupciones bajo virtualización, las áreas donde los drivers de principiantes suelen fallar de formas sutiles que solo se manifiestan en una VM. La sección 10 amplía la perspectiva a arquitecturas más allá de amd64, ya que FreeBSD en arm64 y riscv64 es cada vez más habitual y el panorama de la virtualización allí tiene su propia forma. Tras esas dos secciones, pasamos a los laboratorios.

## Sección 9: Gestión del tiempo, la memoria y las interrupciones bajo virtualización

Hasta ahora el capítulo se ha centrado en los dispositivos, los artefactos visibles a los que un driver se vincula y con los que se comunica. Pero un driver también depende de tres servicios de entorno que el kernel proporciona de forma transparente: el tiempo, la memoria y las interrupciones. Bajo virtualización, los tres cambian de formas sutiles que raramente rompen un driver de forma directa, pero que a menudo lo hacen comportarse de manera extraña. Un driver que ignore estas diferencias superará habitualmente las pruebas funcionales y luego fallará en producción cuando un usuario note que los tiempos de espera son incorrectos, el rendimiento es inferior al esperado o se están perdiendo interrupciones. Esta sección reúne lo que todo autor de drivers debería saber.

### Por qué el tiempo es distinto dentro de una VM

En bare metal, el kernel tiene acceso directo a varias fuentes de tiempo hardware. El TSC (time-stamp counter) lee un contador de ciclos por CPU; el HPET (high-precision event timer) proporciona un contador a nivel de sistema; el temporizador ACPI PM ofrece un respaldo de menor frecuencia. El kernel elige uno como fuente `timecounter(9)` actual, lo envuelve en una pequeña API y lo utiliza para derivar `getbintime`, `getnanotime` y funciones similares.

Dentro de una VM, esas mismas fuentes existen, pero están emuladas o son de passthrough, y cada una tiene sus propias particularidades. El TSC, que normalmente es la mejor fuente, puede volverse poco fiable cuando la VM migra entre hosts físicos, cuando los TSC del host no están sincronizados o cuando el hipervisor limita la tasa del invitado de formas que causan deriva del TSC. El HPET está emulado y cuesta una salida de VM en cada lectura, lo cual es barato para un uso ocasional pero costoso si un driver lo lee en un bucle cerrado. El temporizador ACPI PM es generalmente fiable pero lento.

Para solucionar esto, los principales hipervisores exponen interfaces de reloj *paravirtuales*. Linux popularizó el término `kvm-clock` para el reloj paravirtual de KVM; Xen tiene `xen-clock`; VMware tiene `vmware-clock`; el Hyper-V de Microsoft tiene `hyperv_tsc`. Cada uno de ellos es un pequeño protocolo mediante el cual el hipervisor publica información de tiempo en una página de memoria compartida que el invitado puede leer sin una salida de VM. FreeBSD admite varios de ellos. Puedes ver cuál ha elegido el kernel de la siguiente manera.

```sh
% sysctl kern.timecounter.choice
kern.timecounter.choice: ACPI-fast(900) i8254(0) TSC-low(1000) dummy(-1000000)

% sysctl kern.timecounter.hardware
kern.timecounter.hardware: TSC-low
```

En un invitado bajo bhyve, el kernel puede seleccionar `TSC-low` o una de las opciones paravirtuales dependiendo de los flags de CPU que anuncie el hipervisor. Lo importante es que la elección es automática y la API `timecounter(9)` es la misma independientemente de cuál sea.

### Qué significa esto para los drivers

Para un driver que solo utiliza las API de tiempo de alto nivel (`getbintime`, `ticks`, `callout`), no es necesario cambiar nada. La abstracción `timecounter(9)` te protege de los detalles subyacentes. El driver pregunta «qué hora es» o «cuántos ticks han transcurrido» y el kernel responde correctamente, ya sea en bare metal o en una VM.

Los problemas surgen cuando un driver elude la abstracción y lee las fuentes de tiempo directamente. Un driver que hace `rdtsc()` de forma inline y usa el resultado para temporización será incorrecto bajo virtualización cada vez que el TSC del host cambie (por ejemplo, durante una migración en vivo). Un driver que espera activamente en un registro de dispositivo con un tiempo de espera medido en ciclos de CPU consumirá CPU excesiva dentro de una VM donde un «ciclo de CPU» no es una unidad predecible.

La solución es sencilla: usa las primitivas de tiempo del kernel. `DELAY(9)` para esperas cortas y acotadas. `pause(9)` para cesiones que pueden dormir. `callout(9)` para trabajo diferido. `getbintime(9)` o `getsbinuptime(9)` para lecturas del reloj. Cada una de ellas es correcta bajo virtualización porque el kernel ya se ha adaptado al entorno.

Un patrón concreto que falla en las VM y es sorprendentemente común es la secuencia de «reiniciar y esperar».

```c
/* Broken pattern: busy-wait without yielding. */
bus_write_4(sc->res, RESET_REG, RESET_ASSERT);
for (i = 0; i < 1000000; i++) {
	if ((bus_read_4(sc->res, STATUS_REG) & RESET_DONE) != 0)
		break;
}
```

En bare metal, este bucle podría completarse en pocos microsegundos porque el dispositivo borra el bit de estado rápidamente. En una VM, la lectura y escritura del bus cuestan cada una una salida de VM, y el cuerpo del bucle se ejecuta mucho más lentamente porque cada iteración realiza un viaje de ida y vuelta a través del hipervisor. El límite de 1.000.000 de iteraciones, que es instantáneo en bare metal, puede convertirse en un bloqueo de varios segundos dentro de una VM. Peor aún, durante una pausa de la VM (una migración en vivo), el invitado no se ejecuta en absoluto y el tiempo de espera pierde su significado.

El patrón corregido usa `DELAY(9)` y una espera en tiempo real acotada.

```c
/* Correct pattern: bounded wait with DELAY and a wallclock timeout. */
bus_write_4(sc->res, RESET_REG, RESET_ASSERT);
for (i = 0; i < RESET_TIMEOUT_MS; i++) {
	if ((bus_read_4(sc->res, STATUS_REG) & RESET_DONE) != 0)
		break;
	DELAY(1000);	/* one millisecond */
}
if (i == RESET_TIMEOUT_MS)
	return (ETIMEDOUT);
```

`DELAY(9)` está calibrado con la fuente de tiempo del kernel, de modo que espera el número de microsegundos previsto independientemente de la velocidad a la que se esté ejecutando la CPU en ese momento. El límite del bucle está ahora expresado en milisegundos, lo que tiene significado tanto en entornos de bare metal como en entornos virtualizados.

### Callouts y temporizadores durante la migración

Una preocupación más sutil es qué ocurre con los callouts de un driver cuando la VM se pausa. Si un callout estaba programado para dispararse en 100 ms y la VM se pausa durante 5 segundos (durante una migración en vivo, por ejemplo), ¿se dispara el callout 5,1 segundos después de haberse programado o 100 ms después de que la VM se reanude?

La respuesta es que depende de la fuente de reloj que utilice el subsistema `callout(9)`. `callout` usa `sbt` (signed binary time), que en circunstancias normales se deriva del `timecounter` seleccionado. Para hipervisores que pausan el TSC virtual del invitado durante la migración, el callout se comporta como si no hubiera transcurrido ningún tiempo durante la pausa; la espera de 100 ms es de 100 ms de tiempo observado por el invitado, que puede equivaler a 5,1 segundos de tiempo de reloj real. Para hipervisores que no pausan el TSC virtual, el callout se dispara en el tiempo de reloj real programado, lo que puede ser «inmediatamente» después de que se reanude la VM.

Para la mayoría de los drivers, cualquiera de los dos comportamientos es aceptable. El callout acaba disparándose y el código que activa se ejecuta. Pero un driver que mide tiempo del mundo real (por ejemplo, un driver que se comunica con un dispositivo hardware cuyo estado depende del tiempo de reloj real) puede necesitar resincronizarse tras una reanudación. La infraestructura de suspensión y reanudación de FreeBSD proporciona `DEVMETHOD(device_resume, ...)`, y un driver puede detectar una reanudación y tomar medidas correctoras.

```c
static int
mydrv_resume(device_t dev)
{
	struct mydrv_softc *sc = device_get_softc(dev);

	/*
	 * After a resume (from ACPI suspend or VM pause), the device
	 * may have lost state and the driver's view of time may no
	 * longer align with the device's.  Reinitialise what needs
	 * reinitialising.
	 */
	mydrv_reset(sc);
	mydrv_reprogram_timers(sc);

	return (0);
}
```

`bhyve(8)` admite la suspensión del invitado a través de su propia interfaz de suspensión (en hipervisores que la implementan); el kernel invoca el método `device_resume` de forma normal al despertar. Un driver que implementa `device_resume` correctamente funciona en ambos casos.

### Presión de memoria, ballooning y buffers fijados

Los drivers que poseen buffers DMA o memoria fijada tienen una relación con el subsistema de memoria que cambia bajo virtualización. En bare metal, la memoria física que ve el kernel es la memoria realmente instalada en la máquina. Dentro de una VM, la memoria «física» del invitado es virtual desde el punto de vista del host: está respaldada por la RAM del host más posiblemente swap, y su extensión puede cambiar durante la vida del invitado.

El dispositivo `virtio-balloon` es el mecanismo que usan los hipervisores para recuperar memoria de un invitado que no la está utilizando. Cuando el host necesita memoria, pide al invitado que «infle» su globo, lo que asigna páginas del pool libre del kernel del invitado y las declara inutilizables. Esas páginas pueden entonces desmapearse del invitado y ser reutilizadas por el host. Por el contrario, cuando el host tiene memoria de sobra, puede «desinflar» el globo y devolver páginas al invitado.

FreeBSD tiene un driver `virtio_balloon` (en `/usr/src/sys/dev/virtio/balloon/virtio_balloon.c`) que participa en este protocolo. Para la mayoría de los drivers, esto es invisible: el globo toma del pool libre general, por lo que solo los drivers que fijan cantidades significativas de memoria pueden verse afectados. Si tu driver asigna un buffer DMA de 256 MB y lo fija (para un frame buffer, por ejemplo), el globo no puede recuperar esa memoria. Este es el comportamiento correcto para un buffer fijado, pero sí significa que una VM que ejecute tu driver no puede reducir su huella de memoria tanto como una VM con solo drivers que no fijan memoria.

Una directriz pragmática: evita asignar más memoria fijada de la que necesitas absolutamente. Para los buffers que pueden asignarse bajo demanda, asígnalos bajo demanda. Para los buffers que deben estar residentes en memoria, dimensiónalos a un límite razonable del conjunto de trabajo en lugar de a un máximo pesimista. El driver del globo devolverá el resto al host cuando lo necesite.

### Hotplug de memoria

Algunos hipervisores (incluido bhyve con el soporte adecuado) pueden añadir memoria en caliente a un invitado en ejecución. FreeBSD gestiona esto a través de eventos ACPI y la maquinaria genérica de hotplug. Los drivers que almacenan en caché información de memoria en el momento del attach deben estar preparados para que esa caché quede desactualizada cuando se produzca un hotplug; el patrón robusto es volver a leer la información cuando sea necesario en lugar de almacenarla en caché indefinidamente.

El hot-removal de memoria es más infrecuente y más delicado. Para los autores de drivers, suele ser suficiente con saber que si un driver posee memoria anclada (pinned), el hipervisor no puede eliminarla; si el driver posee memoria no anclada, la gestión de memoria del kernel se encarga de la reubicación. Los drivers que incumplan esto (al dar por sentado que las direcciones físicas que han obtenido del kernel seguirán siendo válidas indefinidamente) fallarán durante un hot-removal de memoria. La solución es utilizar `bus_dma(9)` para cada dirección física que se programe en el hardware, en lugar de almacenar en caché direcciones físicas fuera de un DMA map.

### DMA y la ruta de la IOMMU

Cubrimos la IOMMU en la Sección 5 desde la perspectiva del host. Aquí, desde la perspectiva del driver, hay dos consecuencias prácticas.

En primer lugar, toda dirección que se programe en el hardware debe provenir de una operación de carga de `bus_dma(9)`. En un sistema bare metal sin IOMMU, un driver que programa una dirección física obtenida mediante `vmem_alloc` o una interfaz similar suele funcionar, porque en ese entorno la dirección física coincide con la dirección de bus. En un entorno de passthrough con IOMMU, la dirección de bus que necesita el dispositivo no es la dirección física; es la dirección mapeada por la IOMMU, que `bus_dma_load` calcula. Un driver que programa direcciones físicas directamente transferirá datos hacia o desde la memoria incorrecta, a veces corrompiendo datos ajenos.

En segundo lugar, los mapeos de `bus_dma` tienen una vida útil. El patrón habitual consiste en asignar un tag DMA en el attach, asignar un mapa DMA por buffer, cargar el buffer, programar el dispositivo, esperar a que finalice y, a continuación, descargar el buffer. La carga y la descarga tienen un pequeño coste de CPU, y bajo la IOMMU también conllevan una invalidación de la IOMMU. En drivers que procesan muchos buffers pequeños por segundo, el coste de esa invalidación puede volverse significativo.

La solución, cuando aplica, es mantener los mapeos DMA activos durante más tiempo. `bus_dma` admite mapas preasignados que pueden reutilizarse; un driver que necesite hacer DMA sobre la misma región física de forma repetida puede cargarla una sola vez y reutilizar la dirección de bus hasta que la región deje de ser necesaria. Esta es una optimización estándar completamente independiente de la virtualización, pero importa más en passthrough porque el trabajo de la IOMMU es mayor que el de host a bus en un sistema bare metal.

### Entrega de interrupciones en entornos virtualizados

Las interrupciones siempre han sido el punto más delicado del diseño de drivers, y bajo virtualización se vuelven aún más exigentes. Los dos estilos de interrupción que puede encontrar un driver son *INTx* (basadas en pin) y *MSI/MSI-X* (señalizadas por mensaje).

INTx es el pin de interrupción tradicional. En una máquina real, el pin se conecta a través del bus PCI y un controlador de interrupciones (APIC, IOAPIC) hasta la CPU. En una VM, cada entrega de INTx requiere que el hipervisor intercepte la activación del pin del dispositivo, la mapee a una interrupción interna y la inyecte en el guest. Tanto la intercepción como la inyección tienen el coste de una VM exit. Para interrupciones de baja frecuencia (la clásica señal de "algo ha ocurrido") esto es aceptable. Para interrupciones de alta frecuencia puede convertirse en un cuello de botella.

MSI (Message-Signalled Interrupts) y su sucesor MSI-X prescinden del pin por completo. El dispositivo escribe un pequeño mensaje en una dirección mapeada en memoria bien conocida, y el controlador de interrupciones entrega el vector de interrupción correspondiente. Bajo virtualización, MSI-X funciona mucho mejor que INTx porque el hipervisor puede mapear la escritura del mensaje directamente a una interrupción del guest sin necesidad de interceptar cada transición de flanco en un pin virtual. El hardware moderno admite las llamadas *interrupciones posted*, que permiten al hipervisor entregar interrupciones MSI-X a una vCPU en ejecución sin ninguna VM exit.

La implicación para el driver es clara: usa MSI-X. La API `pci_alloc_msix` de FreeBSD permite a un driver solicitar interrupciones MSI-X. La mayoría de los drivers modernos ya la utilizan. Si estás escribiendo un nuevo driver o actualizando uno existente, usa MSI-X salvo que tengas una razón específica para no hacerlo.

```c
static int
mydrv_setup_msix(struct mydrv_softc *sc)
{
	int count = 1;
	int error;

	error = pci_alloc_msix(sc->dev, &count);
	if (error != 0)
		return (error);

	sc->irq_rid = 1;
	sc->irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &sc->irq_rid, RF_SHAREABLE | RF_ACTIVE);
	if (sc->irq_res == NULL) {
		pci_release_msi(sc->dev);
		return (ENXIO);
	}

	error = bus_setup_intr(sc->dev, sc->irq_res,
	    INTR_TYPE_NET | INTR_MPSAFE, NULL, mydrv_intr, sc,
	    &sc->irq_handle);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    sc->irq_rid, sc->irq_res);
		pci_release_msi(sc->dev);
		return (error);
	}

	return (0);
}
```

Esta es la secuencia estándar de configuración de MSI-X. La llamada a `pci_alloc_msix` negocia con la capa PCI del kernel para asignar un vector MSI-X. El recurso y el manejador de interrupciones se configuran de la manera habitual. La ruta de limpieza libera el vector MSI-X junto con el resto de recursos.

Para drivers con múltiples colas o múltiples fuentes de eventos, MSI-X admite hasta 2048 vectores por dispositivo, y un driver puede asignar uno por cola para evitar contención de locks. La API `pci_alloc_msix` permite solicitar varios vectores; `count` es un parámetro de entrada/salida. Bajo virtualización, cada vector se mapea a una interrupción separada del guest, y las interrupciones posted las entregan sin ninguna VM exit en hardware moderno.

### Coalescencia de interrupciones y supresión de notificaciones

Incluso con MSI-X, un modelo de una interrupción por transacción puede resultar demasiado costoso bajo virtualización. Un dispositivo de alta velocidad que dispara una interrupción por cada paquete recibido puede generar cientos de miles de interrupciones por segundo; aunque cada una sea barata individualmente, el coste agregado es perceptible.

La coalescencia de interrupciones en hardware aborda esto en dispositivos reales: el dispositivo puede configurarse para entregar una interrupción por lote de eventos en lugar de una por evento. En VirtIO, el mecanismo equivalente es la *supresión de notificaciones* (notification suppression), expuesto a través de la característica `VIRTIO_F_RING_EVENT_IDX`.

Con los índices de evento, el guest le indica al dispositivo "no me interrumpas hasta que hayas procesado el descriptor N", y el dispositivo respeta esto comprobando el campo `used_event` del guest antes de generar una interrupción. Un guest que ya está haciendo polling del anillo no necesita ninguna interrupción; un guest que desea recibir interrupciones solo después de un lote puede establecer el índice de evento al tamaño del lote.

El framework `virtqueue(9)` de FreeBSD admite índices de evento cuando se han negociado. Un driver que sabe que puede procesar múltiples buffers por interrupción puede activar los índices de evento para reducir la tasa de interrupciones. El patrón habitual es:

```c
static void
mydrv_intr(void *arg)
{
	struct mydrv_softc *sc = arg;
	void *cookie;
	uint32_t len;

	virtqueue_disable_intr(sc->vq);
	for (;;) {
		while ((cookie = virtqueue_dequeue(sc->vq, &len)) != NULL) {
			/* process the completed buffer */
		}
		if (virtqueue_enable_intr(sc->vq) == 0)
			break;
		/* a new buffer arrived between dequeue and enable; loop again */
	}
}
```

La llamada a `virtqueue_disable_intr` le indica al dispositivo que no genere más interrupciones hasta que se vuelvan a habilitar. El driver vacía el anillo a continuación. La llamada a `virtqueue_enable_intr` arma las interrupciones, pero solo si no ha llegado ningún buffer nuevo mientras tanto; si ha llegado alguno, devuelve un valor distinto de cero y el bucle continúa. Es un patrón estándar que minimiza la tasa de interrupciones sin perder nunca una finalización.

### Uniendo todas las piezas

El tiempo, la memoria y las interrupciones no son temas glamurosos, pero son el punto donde se pone a prueba en la práctica cualquier driver que pretenda ser correcto en entornos virtualizados. Las pautas se reducen a un pequeño conjunto de disciplinas:

- Usa las APIs de tiempo del kernel en lugar de implementar las tuyas propias.
- Trata el estado del dispositivo como algo que puede desaparecer en el resume; escribe `device_resume` correctamente.
- No fijes en memoria más de lo necesario y usa `bus_dma(9)` para toda dirección DMA.
- Prefiere MSI-X sobre INTx.
- Usa el mecanismo de índices de evento de la virtqueue cuando proceda para reducir la tasa de interrupciones.

Estas son las disciplinas de cualquier driver bien escrito; bajo virtualización no son opcionales. Un driver que las sigue funcionará en una VM. Un driver que las viola funcionará en bare metal y fallará en el campo en la VM de un cliente, a veces de formas difíciles de reproducir.

Con esa base establecida, podemos ampliar la perspectiva una vez más y ver cómo estas ideas se aplican a arquitecturas distintas de amd64.

## Sección 10: Virtualización en otras arquitecturas

FreeBSD funciona bien en amd64, arm64 y riscv64. La historia de la virtualización en amd64 es la que hemos venido contando: `bhyve(8)` usa Intel VT-x o AMD-V, los guests ven dispositivos VirtIO basados en PCI, la IOMMU es VT-d o AMD-Vi, y el flujo general resulta familiar. En las otras arquitecturas, las piezas son similares pero los detalles difieren. Para el autor de un driver, la mayor parte de esto es invisible, porque las APIs de virtualización de FreeBSD son independientes de la arquitectura. Pero algunos puntos prácticos merecen conocerse, y algunos drivers tienen comportamiento específico por arquitectura que solo aflora bajo virtualización.

### Virtualización en arm64

En arm64, el modo hipervisor se denomina *EL2* (Exception Level 2), y las extensiones de virtualización forman parte de la especificación estándar de la arquitectura (virtualización ARMv8-A). Un guest ejecuta en EL1 bajo el hipervisor en EL2. No existe el INTx de estilo amd64; el controlador de interrupciones es el *GICv3* (Generic Interrupt Controller versión 3), y la virtualización de interrupciones la proporciona la interfaz de CPU virtual del GIC.

FreeBSD tiene un port del host de `bhyve(8)` para arm64 en desarrollo orientado a una versión futura; en FreeBSD 14.3 la implementación vmm solo existe para amd64, por lo que los hosts arm64 aún no ejecutan guests de forma nativa. Sin embargo, el lado guest de FreeBSD ya usa los mismos transportes `virtio_mmio` y `virtio_pci` que amd64, de modo que un guest FreeBSD ejecutándose en un hipervisor arm64 (por ejemplo, KVM o un futuro `bhyve` arm64) no sabe ni le importa que el host sea arm64.

En el caso de VirtIO específicamente, los guests arm64 usan con más frecuencia el transporte MMIO. Esto es una consecuencia práctica de cómo suelen configurarse las plataformas virtuales: los hipervisores arm64 suelen exponer los dispositivos VirtIO como regiones MMIO en lugar de buses PCI emulados. FreeBSD proporciona el transporte a través de `virtio_mmio.c` (en `/usr/src/sys/dev/virtio/mmio/`). Un driver que usa `VIRTIO_DRIVER_MODULE` es automáticamente compatible con ambos transportes, porque la macro registra el driver tanto en `virtio_mmio` como en `virtio_pci`.

Esta es una de las ventajas discretas del diseño de `virtio_bus`. Un driver VirtIO escrito una sola vez se ejecuta en todos los transportes VirtIO en todas las arquitecturas que FreeBSD admite. Sin cláusulas `#ifdef __amd64__`, sin capas de traducción por arquitectura. `virtio_bus` abstrae el transporte; el driver habla con la abstracción.

### Virtualización en riscv64

En riscv64, la virtualización la proporciona la *extensión H* (extensión de hipervisor). FreeBSD también tiene un port de `bhyve` para riscv64, aunque menos maduro que los ports de amd64 y arm64 en FreeBSD 14.3. Los transportes VirtIO funcionan de la misma manera: los drivers usan `VIRTIO_DRIVER_MODULE` y el `virtio_bus` del kernel gestiona los detalles del transporte.

Para los autores de drivers que trabajan en riscv64, lo más importante es saber que todas las APIs independientes de arquitectura se aplican. `bus_dma(9)`, `callout(9)`, `mtx(9)`, `sx(9)` y el framework VirtIO funcionan en riscv64. El código que es correcto en amd64 normalmente se ejecuta sin cambios en riscv64. Las diferencias están en detalles de más bajo nivel (ordenación de memoria, volcado de caché, enrutamiento de interrupciones) que el kernel gestiona internamente.

### Consideraciones entre arquitecturas para autores de drivers

Si estás escribiendo un driver que debe funcionar en múltiples arquitecturas, las pautas del Capítulo 29 se aplican directamente. Usa las APIs independientes de arquitectura. Evita el ensamblador en línea salvo cuando sea estrictamente necesario (y en ese caso, aíslalo tras un envoltorio portable). No asumas un tamaño de línea de caché concreto ni un tamaño de página concreto. No asumas el orden de bytes de una arquitectura salvo que hayas utilizado explícitamente una macro de conversión.

En lo que respecta específicamente a la virtualización, la principal preocupación entre arquitecturas es *qué transportes VirtIO están presentes*. En amd64, VirtIO es casi siempre PCI. En arm64 con hipervisores QEMU o Ampere, VirtIO suele ser MMIO. En riscv64 con QEMU, VirtIO suele ser MMIO. Un driver que solo gestione VirtIO PCI no funcionará en entornos MMIO. La solución es usar `VIRTIO_DRIVER_MODULE`, que registra el driver en ambos transportes, y evitar asumir que el bus padre del dispositivo es PCI.

Una prueba concreta: en tu arquitectura de destino, ejecuta `pciconf -l` dentro de un guest y comprueba si aparecen dispositivos VirtIO. Si aparecen, el transporte es PCI. Si no aparecen (pero los dispositivos funcionan), el transporte es MMIO. En guests arm64, también puedes comprobar `sysctl dev.virtio_mmio` para ver los dispositivos VirtIO MMIO. Un driver que funcione con ambos transportes no producirá resultados diferentes en estas comprobaciones, porque la API `virtio_bus` es con la que interactúa.

### Cuándo importa realmente la arquitectura

La mayoría de los drivers son independientes de la arquitectura si se escriben siguiendo el idioma de FreeBSD. Las excepciones son:

- Drivers que tratan con características específicas del hardware: por ejemplo, la SMMU de ARM es arquitectónicamente diferente de Intel VT-d, y un driver que manipule la IOMMU directamente (algo poco habitual) debe gestionar ambas.
- Drivers que realizan conversión de orden de bytes: los drivers de red lo hacen de forma habitual, pero utilizan funciones auxiliares portables (`htonl`, `ntohs`, etc.) en lugar de código específico por arquitectura.
- Drivers que necesitan instrucciones de CPU concretas: por ejemplo, los drivers criptográficos que usan AES-NI en amd64 o las extensiones AES en arm64 necesitan rutas de código por arquitectura. Son poco frecuentes y ya están aisladas por el framework de criptografía del kernel.

Para la mayor parte del trabajo con drivers en la mayoría de las arquitecturas, el modelo mental correcto es «FreeBSD es FreeBSD». Las APIs son las mismas. Los modismos son los mismos. El framework de virtualización es el mismo. La arquitectura es un detalle que el kernel gestiona por ti.

Con las consideraciones de arquitectura ya tratadas, tenemos una visión completa de la virtualización y la contenerización en lo que respecta a un autor de drivers de FreeBSD. Las secciones restantes del capítulo se centran en la práctica.

## Laboratorios prácticos

Los laboratorios que siguen te guían a través de cuatro pequeños ejercicios que ponen en práctica las ideas del capítulo. Cada laboratorio tiene un objetivo claro, una serie de prerequisitos y una secuencia de pasos. Los archivos complementarios están disponibles en `examples/part-07/ch30-virtualisation/` para los que necesitan más que unas pocas líneas de código.

Trabájalos en orden. El primer laboratorio te familiariza con la inspección de un guest VirtIO desde dentro de una VM. El segundo te lleva a escribir un pequeño driver adyacente a VirtIO. El tercero y el cuarto pasan a trabajar con jails y VNET. Reserva un par de horas de tiempo práctico para todo el conjunto; más si la configuración de `bhyve(8)` o QEMU es nueva para ti.

### Laboratorio 1: Explorar un guest VirtIO

**Objetivo**: Confirmar que puedes arrancar un guest FreeBSD 14.3 bajo `bhyve(8)`, iniciar sesión y observar los dispositivos VirtIO que tiene conectados. Esto establece el entorno de desarrollo que utilizarás en los laboratorios posteriores.

**Prerequisitos**: Un host FreeBSD 14.3 (bare metal o anidado en otro hipervisor, siempre que el hipervisor externo admita virtualización anidada). Suficiente memoria para un guest pequeño (2 GB es más que suficiente). El módulo `vmm.ko` cargable en el host, lo cual es posible en cualquier kernel FreeBSD 14.3 estándar.

**Pasos**:

1. Descarga una imagen de VM de FreeBSD 14.3. El proyecto base proporciona imágenes de VM preconstruidas en `https://download.freebsd.org/`. Elige una imagen compatible con `bhyve` (normalmente las variantes "BASIC-CI" o "VM-IMAGE" del directorio `amd64`).
2. Instala el port o paquete `vm-bhyve`: `pkg install vm-bhyve`. Esto envuelve `bhyve(8)` en una interfaz de gestión más amigable.
3. Configura un directorio de VM: `zfs create -o mountpoint=/vm zroot/vm` (o `mkdir /vm` si no usas ZFS). En `/etc/rc.conf`, añade `vm_enable="YES"` y `vm_dir="zfs:zroot/vm"` (o `vm_dir="/vm"`).
4. Inicializa el directorio: `vm init`. Copia una plantilla por defecto: `cp /usr/local/share/examples/vm-bhyve/default.conf /vm/.templates/default.conf`.
5. Crea una VM: `vm create -t default -s 10G guest0`. Adjunta la imagen descargada: `vm install guest0 /path/to/FreeBSD-14.3-RELEASE-amd64.iso`.
6. Inicia sesión cuando el instalador termine y reinicie. El prompt de login aparece en `vm console guest0`.
7. Dentro del guest, ejecuta `pciconf -lvBb | head -40`. Verás dispositivos virtio-blk, virtio-net y posiblemente virtio-random, según la plantilla. Anota el driver al que está ligado cada dispositivo.
8. Ejecuta `sysctl kern.vm_guest`. La salida debería ser `bhyve`.
9. Ejecuta `dmesg | grep -i virtio`. Observa los mensajes de attach de cada dispositivo VirtIO.
10. Registra tus observaciones en un archivo de texto. Las necesitarás en los Laboratorios 2 y 4.

**Resultado esperado**: Una VM en funcionamiento, un listado claro de los dispositivos VirtIO conectados y una lectura confirmada de `kern.vm_guest = bhyve`.

**Errores frecuentes**: No habilitar VT-x o AMD-V en la BIOS del host. No tener suficiente memoria para el guest. Un bridge de red mal configurado que impida al guest acceder a la red (lo cual es inofensivo para este laboratorio, pero importará más adelante).

### Laboratorio 2: Usar la detección de hipervisor en un módulo del kernel

**Objetivo**: Escribir un pequeño módulo del kernel que lea `vm_guest` y registre el entorno en el momento de la carga. Este es el ejemplo más sencillo posible de comportamiento de un driver sensible al entorno.

**Prerequisitos**: Un guest FreeBSD 14.3 operativo del Laboratorio 1. Las herramientas de build del kernel instaladas (se incluyen con el sistema base en el kit de desarrollo). El sistema de build `bsd.kmod.mk`, accesible mediante un `Makefile` sencillo.

**Pasos**:

1. En `examples/part-07/ch30-virtualisation/lab02-detect/` del árbol de ejemplos complementario encontrarás un `detectmod.c` de inicio y un `Makefile`. Si no utilizas el árbol complementario, crea estos archivos a mano.
2. El archivo fuente debe definir un módulo del kernel que, al cargarse, imprima en qué entorno se encuentra basándose en `vm_guest`.
3. Construye el módulo: `make clean && make`.
4. Cárgalo: `sudo kldload ./detectmod.ko`.
5. Comprueba la salida de dmesg: `dmesg | tail`. Deberías ver una línea similar a `detectmod: running on bhyve`.
6. Descárgalo: `sudo kldunload detectmod`.
7. Si es posible, reinicia el guest en otro hipervisor (QEMU/KVM, por ejemplo) y vuelve a ejecutarlo. La salida debería reflejar el nuevo entorno.

**Resultado esperado**: Un módulo que identifica correctamente el hipervisor en el que se ejecuta, usando `vm_guest`.

**Errores frecuentes**: Olvidar incluir `<sys/systm.h>` para la declaración de `vm_guest`. Errores de enlace debidos a macros mal escritas. Olvidar establecer `KMOD=` y `SRCS=` en el Makefile.

### Laboratorio 3: Un driver mínimo de dispositivo de caracteres dentro de una jail

**Objetivo**: Escribir un pequeño driver de dispositivo de caracteres, exponerlo desde el host, crear una jail con un conjunto de reglas devfs personalizado que haga visible el dispositivo en su interior, y verificar que los procesos de la jail pueden usarlo mientras se siguen aplicando las comprobaciones de privilegios del host.

**Prerequisitos**: Un host FreeBSD 14.3. Un directorio de configuración de jail operativo (normalmente `/jails/`). Familiaridad básica con `make_dev(9)`, `d_read` y `d_write`.

**Pasos**:

1. En `examples/part-07/ch30-virtualisation/lab03-jaildev/` encontrarás `jaildev.c` y `Makefile`. El driver crea un dispositivo de caracteres `/dev/jaildev` cuya función `read` devuelve un saludo fijo.
2. Construye y carga el módulo en el host: `make && sudo kldload ./jaildev.ko`.
3. Verifica que `/dev/jaildev` existe y es legible: `cat /dev/jaildev`.
4. Crea un directorio raíz para la jail: `mkdir -p /jails/test && cp /bin/sh /jails/test/` (esta es una jail mínima; para una configuración real, usa un layout de sistema de archivos adecuado).
5. Añade un conjunto de reglas devfs a `/etc/devfs.rules`:
   ```text
   [devfsrules_jaildev=100]
   add include $devfsrules_hide_all
   add include $devfsrules_unhide_basic
   add path 'jaildev' unhide
   ```
6. Recarga las reglas: `sudo service devfs restart`.
7. Arranca la jail: `sudo jail -c path=/jails/test devfs_ruleset=100 persist command=/bin/sh`.
8. En otro terminal, entra en la jail: `sudo jexec test /bin/sh`.
9. Dentro de la jail, ejecuta `cat /dev/jaildev`. El saludo debería aparecer. Prueba `ls /dev/`: solo los dispositivos permitidos (incluido `jaildev`) son visibles.
10. Prueba el límite de privilegios: modifica el driver para requerir `PRIV_DRIVER` en un ioctl, reconstruye, recarga y verifica que el root de la jail no puede ejecutar el ioctl mientras que el root del host sí puede.

**Resultado esperado**: Un driver visible en la jail únicamente porque el conjunto de reglas lo permite, con comprobaciones de privilegios que se comportan de forma diferente para el root del host y el root de la jail.

**Errores frecuentes**: Olvidar reiniciar `devfs` tras editar las reglas. No establecer `persist` en la jail (sin esto, la jail muere en cuanto termina el proceso inicial). Leer mal la sintaxis del conjunto de reglas (los espacios en blanco son significativos).

### Laboratorio 4: Un driver de red dentro de una jail VNET

**Objetivo**: Crear una jail VNET, mover un extremo de un `epair(4)` a su interior y verificar que el tráfico de red fluye entre el host y la jail usando únicamente la VNET de la jail. Este laboratorio ejercita `if_vmove()` y el ciclo de vida de VNET.

**Prerequisitos**: Un host FreeBSD 14.3 con `if_epair.ko` cargable. Privilegios de root.

**Pasos**:

1. Carga el módulo `if_epair`: `sudo kldload if_epair`.
2. Crea un `epair`: `sudo ifconfig epair create`. Obtendrás un par de dispositivos `epair0a` y `epair0b`.
3. Asigna una IP a `epair0a` en el host: `sudo ifconfig epair0a 10.100.0.1/24 up`.
4. Crea un directorio raíz para la jail: `mkdir -p /jails/vnet-test`. Coloca dentro un shell mínimo y el binario `ifconfig` (o monta con bind `/bin`, `/sbin`, `/usr/bin` para las pruebas).
5. Crea la jail con VNET habilitado:
   ```sh
   sudo jail -c \
       name=vnet-test \
       path=/jails/vnet-test \
       host.hostname=vnet-test \
       vnet \
       vnet.interface=epair0b \
       persist \
       command=/bin/sh
   ```
   El parámetro `vnet.interface=epair0b` desencadena el `if_vmove()` que mueve la interfaz al interior de la jail.
6. Entra en la jail: `sudo jexec vnet-test /bin/sh`.
7. Dentro de la jail, configura la interfaz: `ifconfig epair0b 10.100.0.2/24 up`.
8. Aún dentro de la jail, haz ping al host: `ping -c 3 10.100.0.1`. Debería funcionar correctamente.
9. Desde el host, haz ping a la jail: `ping -c 3 10.100.0.2`. Debería funcionar correctamente.
10. Detén la jail: `sudo jail -r vnet-test`. La interfaz `epair0b` se mueve de vuelta al host (porque se movió con `vnet.interface`, que usa `if_vmove_loan()` internamente en las versiones recientes de FreeBSD).
11. Verifica que la interfaz ha vuelto al host: `ifconfig epair0b`. Debería seguir existiendo pero pertenecer de nuevo a la VNET del host.

**Resultado esperado**: Una jail con su propia pila de red, moviendo una interfaz de forma limpia hacia dentro y hacia fuera.

**Errores frecuentes**: Olvidar habilitar VNET en el kernel (`options VIMAGE` está habilitado en `GENERIC`, así que esto no debería ser un problema, pero los kernels personalizados podrían no tenerlo). Intentar usar una interfaz física en lugar de `epair(4)` en el primer intento (esto funciona, pero provoca que el host pierda esa interfaz mientras la jail la tiene). No proporcionar a la jail suficientes binarios para ejecutar un shell (la solución más sencilla es un mount `nullfs` del `/rescue` del host o una configuración mínima con bind-mount).

### Laboratorio 5: Simulación de PCI passthrough (opcional)

**Objetivo**: Observar cómo el PCI passthrough cambia la titularidad de un dispositivo, utilizando como objetivo un dispositivo no crítico. Este laboratorio está marcado como opcional porque requiere un dispositivo PCI de repuesto y un host con capacidad IOMMU. Si no están disponibles, lee los pasos de todas formas; ilustran el flujo de trabajo incluso sin ejecutarlos.

**Prerequisitos**: Un host FreeBSD 14.3 con VT-d o AMD-Vi habilitado en el firmware. Un dispositivo PCI de repuesto que sea seguro retirar del host (una NIC sin usar es una opción habitual). Una configuración de guest `bhyve(8)`.

**Pasos**:

1. Identifica el dispositivo objetivo: `pciconf -lvBb | grep -B 1 -A 10 'Ethernet'` (o el tipo que corresponda al dispositivo de repuesto). Anota su bus, slot y función (p. ej., `pci0:5:0:0`).
2. Edita `/boot/loader.conf` y añade el dispositivo a la lista de passthrough:
   ```text
   pptdevs="5/0/0"
   ```
3. Reinicia el host. El dispositivo debería estar ahora enlazado a `ppt(4)` en lugar de a su driver nativo. Confírmalo con `pciconf -l` (busca `ppt0`).
4. Configura un guest para hacer el passthrough del dispositivo:
   ```text
   passthru0="5/0/0"
   ```
   (Usando la configuración de `vm-bhyve`; el comando `bhyve` en bruto es más verboso.)
5. Arranca el guest. Dentro del guest, ejecuta `pciconf -lvBb`. El dispositivo ahora aparece en el guest con sus IDs de vendedor y dispositivo reales, enlazado a su driver nativo.
6. Ejercita el dispositivo dentro del guest: configura la NIC, envía tráfico y verifica que funciona.
7. Apaga el guest. Edita `/boot/loader.conf` para eliminar la línea `pptdevs`, reinicia y verifica que el dispositivo vuelve al host con su driver nativo conectado.

**Resultado esperado**: Un ciclo completo de un dispositivo PCI desde el host al guest y de vuelta, ejercitando las rutas de detach y reattach del driver nativo.

**Errores frecuentes**: El firmware del host no tiene VT-d o AMD-Vi habilitado (búscalo en la configuración BIOS/UEFI). El dispositivo elegido está en el mismo grupo IOMMU que un dispositivo que el host necesita, lo que obliga a hacer un passthrough de varios dispositivos. El dispositivo está enlazado a su driver nativo en el boot antes de que `ppt(4)` pueda reclamarlo (normalmente esto no es problema si `pptdevs` se establece con suficiente antelación).

### Laboratorio 6: Construir y cargar el driver vtedu

**Objetivo**: Construir el driver pedagógico `vtedu` del caso de estudio, cargarlo en un guest FreeBSD 14.3 y observar su comportamiento incluso sin un backend correspondiente. Este laboratorio ejercita el proceso de build de módulos del kernel en el contexto VirtIO y verifica la estructura del módulo.

**Prerequisitos**: Un guest FreeBSD 14.3 (el del Laboratorio 1 es válido). El árbol de código fuente de FreeBSD bajo `/usr/src` o los headers del kernel instalados (`pkg install kernel-14.3-RELEASE` funciona en sistemas estándar). Privilegios de root dentro del guest.

**Pasos**:

1. Copia los archivos complementarios a la máquina invitada. En el árbol de ejemplos del libro, `examples/part-07/ch30-virtualisation/vtedu/` contiene `vtedu.c`, `Makefile` y `README.md`. Transfiérelos a la máquina invitada (mediante `scp`, un recurso compartido `9p` o un volumen compartido).
2. Dentro de la máquina invitada, entra en el directorio `vtedu`: `cd /tmp/vtedu`.
3. Construye el módulo: `make clean && make`. Si la compilación falla porque `/usr/src` no está instalado, instálalo con `pkg install kernel-14.3-RELEASE` o apunta `SYSDIR` hacia un árbol de código fuente del kernel alternativo: `make SYSDIR=/path/to/sys`.
4. Una compilación correcta produce `virtio_edu.ko` en el directorio actual.
5. Carga el módulo: `sudo kldload ./virtio_edu.ko`. La carga tiene éxito con independencia de si hay un dispositivo compatible presente.
6. Comprueba el estado del módulo: `kldstat -v | grep -A 5 virtio_edu`. Verás que el módulo está cargado, pero que no hay ningún dispositivo vinculado. Es lo que cabe esperar cuando no hay backend.
7. Descarga el módulo: `sudo kldunload virtio_edu`.
8. Inspecciona la información PNP del módulo: `kldxref -d /boot/modules` lista los módulos y sus entradas PNP. Si copiaste el módulo en `/boot/modules`, verás la entrada PNP `VirtIO simple` que anuncia el ID de dispositivo 0xfff0.
9. Observa la salida de `dmesg` durante la carga y la descarga. No debería haber errores. La ausencia de un mensaje de attach confirma que no hay ningún dispositivo vinculado, que es el comportamiento esperado.

**Resultado esperado**: Un ciclo de compilación y carga exitoso que demuestra que el módulo está bien formado. Sin un backend, el módulo es inerte; esto es una confirmación útil de que la infraestructura de compilación y carga funciona con independencia de la parte del dispositivo.

**Problemas habituales**: Código fuente del kernel ausente (instala el paquete `src`). Dependencia `virtio.ko` ausente (carga `virtio` primero si por algún motivo no estuviera presente, aunque está compilado dentro de `GENERIC`). Confusión cuando no se vincula ningún dispositivo (relee la sección 1 del README de vtedu; es intencionado).

**Qué hacer a continuación**: El aprendizaje real llega cuando emparejas este módulo con un backend. El quinto ejercicio de desafío describe cómo escribir un backend compatible en `bhyve(8)`. Si lo completas, el driver se vinculará al dispositivo proporcionado por el backend, aparecerá un nodo `/dev/vtedu0` y podrás ejecutar `echo hello > /dev/vtedu0 && cat /dev/vtedu0` para ejercitar el ciclo completo de VirtIO que estudiaste en el caso práctico.

### Laboratorio 7: Medición del overhead de VirtIO

**Objetivo**: Cuantificar las características de rendimiento de VirtIO ejecutando una carga de trabajo sencilla en configuraciones de dispositivo emulado, paravirtualizado y passthrough. Este laboratorio trata de desarrollar intuición sobre cuánto cuesta la virtualización, no de optimizar un driver concreto.

**Requisitos previos**: Un host FreeBSD 14.3 con bhyve, al menos 8 GB de RAM y la herramienta `vm-bhyve` instalada. Un disco NVMe es ideal, aunque no imprescindible. La herramienta de benchmarking `fio(1)` (disponible en ports como `benchmarks/fio`). Opcional: una NIC de repuesto para comparar con passthrough.

**Pasos**:

1. Crea un guest de referencia con dispositivos VirtIO de bloque y de red (es la configuración por defecto en `vm-bhyve`).
2. Dentro del guest, instala `fio`: `pkg install fio`.
3. Ejecuta un benchmark de disco de referencia: `fio --name=baseline --rw=randread --bs=4k --size=1G --numjobs=4 --iodepth=32 --runtime=30s --group_reporting`. Anota los IOPS y la latencia.
4. En el host, mide la misma carga de trabajo directamente (sin estar dentro del guest), usando el almacenamiento de respaldo como destino. Compara los resultados.
5. Ejecuta un benchmark de red de referencia: `iperf3 -c 10.0.0.1 -t 30` (el servidor en el host, el cliente en el guest). Anota el throughput.
6. Si dispones de una NIC de repuesto, reconfigura el guest para usar passthrough en esa NIC (consulta el Laboratorio 5). Vuelve a ejecutar el benchmark de iperf3. Compara.
7. En el host, observa los contadores de interrupciones y salidas de VM mientras se ejecutan los benchmarks: `vmstat -i`, `pmcstat -S instructions -l 10`. Los contadores más interesantes son las tasas de salidas de VM, que correlacionan directamente con el overhead.
8. Para el benchmark de disco, prueba a variar las características de VirtIO. Modifica la configuración del guest para deshabilitar `VIRTIO_F_RING_EVENT_IDX` (si el backend lo permite) y observa el cambio en la tasa de interrupciones.

**Resultado esperado**: Un pequeño conjunto de números que cuantifican el coste de la virtualización para tu configuración concreta. En general, la I/O paravirtualizada con VirtIO se sitúa a entre un 10 y un 20 % de diferencia respecto al bare metal en cargas sostenidas, y a entre un 30 y un 40 % en cargas aleatorias sensibles a la latencia. El passthrough queda a apenas unos pocos puntos porcentuales del bare metal, aunque sacrifica flexibilidad. La emulación pura (por ejemplo, una E1000 emulada en lugar de virtio-net) es entre 5 y 10 veces más lenta que VirtIO y debe evitarse para cualquier carga de trabajo seria.

**Errores frecuentes**: Los benchmarks pueden ser ruidosos; ejecuta cada uno varias veces y usa la mediana en lugar de muestras únicas. El estado de la caché del host afecta a los benchmarks de disco; realiza una pasada de calentamiento antes de las ejecuciones medidas. El escalado de frecuencia de la CPU puede distorsionar los resultados; ancla el guest a núcleos concretos y desactiva el escalado en el host para obtener resultados reproducibles.

**Qué enseña este laboratorio**: Los números son lo de menos; lo principal es el método. Ser capaz de medir el overhead y atribuirlo a una capa concreta (emulación frente a paravirtualización frente a passthrough, tasa de interrupciones frente a throughput, CPU del guest frente a CPU del host) es la base del trabajo de rendimiento en entornos virtualizados. La misma técnica se aplica a cualquier driver que escribas: si tiene un requisito de rendimiento, necesitas medirlo, y las técnicas de medición son las que se usan en este laboratorio.

### Una nota sobre los laboratorios que no puedes completar hoy

No todos los lectores dispondrán del hardware necesario para cada laboratorio. Los laboratorios del 1 al 4 son realizables en prácticamente cualquier máquina FreeBSD 14.3 con memoria suficiente y el módulo `vmm`. El Laboratorio 5 requiere hardware específico que muchos lectores no tendrán. Si no puedes ejecutarlo, trátalo como una lectura acompañada; los conceptos se aplican a cualquier escenario de passthrough, y los comandos exactos están documentados en `pci_passthru(4)` y en la página de manual de `bhyve(8)`.

Si te quedas bloqueado en algún laboratorio, anota exactamente qué ha fallado y vuelve a él más tarde. La virtualización y la contenedorización son áreas en las que pequeños detalles del entorno pueden descarrilar toda una configuración, y la habilidad de diagnosticar y acotar el fallo es tan importante como completar el laboratorio.

## Ejercicios de desafío

Los laboratorios anteriores te guían por los caminos habituales. Los desafíos que siguen te piden que vayas más allá. Son más difíciles, menos prescriptivos y están pensados para recompensar la experimentación. Elige el que más te interese; cada uno ejercita distintas capacidades.

### Desafío 1: Amplía el módulo de detección

El módulo del Laboratorio 2 lee `vm_guest` e imprime una etiqueta de entorno. Amplíalo para que también lea la cadena de identificación del fabricante de la CPU (usando `cpu_vendor` y `cpu_vendor_id`), la memoria física total (`realmem`) y la firma del hipervisor cuando esté disponible. Produce una única línea de registro estructurada que un script de pruebas pueda analizar.

La cadena del fabricante de CPU es una parte bien conocida de la identificación del procesador; consulta `/usr/src/sys/x86/x86/identcpu.c` para ver cómo la lee el kernel. El total de memoria física se expone a través del global `realmem` y del sysctl `hw.physmem`. La firma del hipervisor reside en la hoja CPUID 0x40000000 en la mayoría de los hipervisores; leerla requiere un pequeño fragmento de ensamblador o un intrínseco.

La pregunta de diseño interesante es cómo exponer esta información al espacio de usuario. Podrías registrarla en el momento de cargar el módulo, añadir un sysctl o crear un dispositivo `/dev/envinfo`. Cada opción tiene sus ventajas e inconvenientes. Piensa cuál sería la más apropiada para un driver en producción real.

### Desafío 2: Un backend de simulación para un driver VirtIO real

Las técnicas del Capítulo 29 recomiendan dividir un driver en accesores, backends y una capa superior delgada. Aplica este enfoque a `virtio_random`. El driver real (en `/usr/src/sys/dev/virtio/random/virtio_random.c`) es pequeño y está bien escrito. ¿Puedes refactorizarlo para que las operaciones de virtqueue pasen por una capa de accesores, y que se pueda seleccionar un backend de simulación que no necesite un dispositivo VirtIO real para las pruebas?

La refactorización es sutil porque el framework VirtIO ya te proporciona la mayor parte de la capa de accesores: `virtqueue_enqueue`, `virtqueue_dequeue` y `virtio_notify` son ya abstracciones. El reto está en encontrar una capa por encima de ellas que pueda intercambiarse. Una posibilidad es trasladar el bucle de recolección a una función que acepte un callback para "obtener los datos de un buffer", e implementar ese callback bien como "llamar a la virtqueue" (real) o como "leer de `/dev/urandom` en el host" (simulación).

Este desafío es más un ejercicio de diseño que de codificación. El objetivo es entender hasta dónde puede llevarse el consejo del Capítulo 29 en un driver real que ya cuenta con buenas abstracciones.

### Desafío 3: Un esqueleto de driver con soporte VNET

Escribe un esqueleto de módulo del kernel que cree una interfaz de red pseudovirtual (usando el framework `if_clone(9)`), soporte ser movida entre VNETs e informe de su identidad VNET a través de un sysctl. Verifica con un jail VNET que la interfaz puede moverse al jail, usarse allí y volver.

La sutileza clave está en gestionar el contexto VNET correctamente. Lee el código de `/usr/src/sys/net/if_tuntap.c` como referencia. El ciclo de vida de `if_vmove` exige que el driver limpie el estado por VNET cuando la interfaz abandona un contexto y lo recree cuando llega a otro. Presta atención a `VNET_SYSINIT` y `VNET_SYSUNINIT` si tu driver necesita estado por VNET.

Este es un desafío profundo que te adentrará en los internos de VNET. No esperes terminarlo en una sola sesión. Trátalo como un proyecto de varios días.

### Desafío 4: Un dispositivo de estado visible desde jails

Escribe un driver que exponga un dispositivo de caracteres `/dev/status`. La operación `read` del dispositivo devuelve información diferente según si el llamante está en un jail o no. Si el llamante está en el host, devuelve el estado global del sistema. Si está en un jail, devuelve el estado específico de ese jail (número de procesos, uso de memoria actual, etc.).

La parte interesante es cómo distinguir el jail del llamante. El `struct thread` que se pasa al método `read` tiene `td->td_ucred->cr_prison`, y `prison0` es el host. Desde el puntero al prison puedes leer el nombre del jail, su número de procesos (`pr_nprocs`), etc. Ten cuidado con el locking: los campos del prison se leen en su mayoría bajo un mutex que el driver debe adquirir.

Este desafío es una buena manera de aprender sobre la API de jails sin escribir nada relacionado con el hipervisor. También te enseña sobre `struct thread` y `struct ucred`, que son fundamentales para el modelo de privilegios de FreeBSD.

### Desafío 5: Un dispositivo emulado de bhyve en espacio de usuario

Si estás preparado para un proyecto mayor, estudia cómo `bhyve(8)` emula un dispositivo VirtIO sencillo (por ejemplo, virtio-rnd) en espacio de usuario, y escribe tu propio dispositivo emulado. El objetivo más accesible es un dispositivo "hello" que devuelva una cadena fija a través de una interfaz VirtIO. El driver del lado del guest es el trabajo de tu Capítulo 29 o Capítulo 30; el emulador del lado del host vive en `/usr/src/usr.sbin/bhyve/`.

Este ejercicio une todo lo del capítulo: escribes un dispositivo VirtIO en espacio de usuario en el host, tu driver dentro del guest lo lee, el transporte virtqueue entre ellos es lo que has estado estudiando, y el conjunto pone a prueba el ciclo completo guest-host.

Es un proyecto considerable y requiere cierta familiaridad con la estructura del código de `bhyve(8)`. Considéralo una meta ambiciosa. Si lo terminas, habrás aprendido de verdad ambos lados de la historia de VirtIO.

### Desafío 6: Orquestación de contenedores para pruebas de drivers

Escribe un script de shell (o una herramienta más elaborada) que automatice los flujos de trabajo de los Laboratorios 3 y 4. Dado un driver y un arnés de pruebas, el script debe:

1. Construir el driver.
2. Cargarlo en el host.
3. Crear un jail (o un jail VNET) con un conjunto de reglas apropiado.
4. Ejecutar el arnés dentro del jail.
5. Recopilar los resultados.
6. Destruir el jail.
7. Descargar el driver.
8. Informar del resultado (éxito/fallo) junto con los diagnósticos.

El script debe ser idempotente (ejecutarlo dos veces no debe dejar residuos) y debe gestionar los fallos comunes de forma controlada. El resultado final es una herramienta que puedes ejecutar en CI para verificar que tu driver sigue funcionando correctamente bajo jails a medida que evoluciona.

No es un desafío técnicamente profundo, sino práctico. Construir este tipo de automatización representa una parte significativa del trabajo real con drivers, y hacerlo tú mismo una vez te enseña lo que implica.

### Cómo afrontar estos desafíos

Cada desafío podría ocupar un fin de semana. Elige uno que te parezca interesante y reserva tiempo para él. No intentes hacerlos todos a la vez; tu energía para trabajo desconocido es limitada, y aprendes más de un desafío terminado que de tres a medias.

Si te quedas atascado, haz dos cosas. Primero, vuelve a leer la sección correspondiente del capítulo; las pistas están ahí. Segundo, busca drivers reales de FreeBSD que hagan algo parecido. El árbol de código fuente es tu mejor maestro para aprender la manera idiomática de resolver problemas que ya han sido resueltos.

## Resolución de problemas y errores frecuentes

Esta sección recoge los problemas con los que los autores de drivers se encuentran más a menudo cuando trabajan en entornos virtualizados y en contenedores. Cada entrada describe el síntoma, la causa probable y la forma de verificarlo y solucionarlo. Guarda esta sección como referencia; la primera vez que veas un síntoma te resultará nuevo, pero la siguiente ya lo reconocerás.

### Dispositivo VirtIO no detectado en el guest

**Síntoma**: El guest arranca, pero `pciconf -l` no muestra el dispositivo VirtIO y `dmesg` no contiene mensajes de attach de `virtio`.

**Causa**: O bien el host no configuró el dispositivo (línea de comandos de `bhyve` incorrecta, plantilla de `vm-bhyve` errónea), o bien el kernel del guest no tiene el módulo VirtIO. En FreeBSD 14.3, los drivers VirtIO están incluidos en `GENERIC` y se cargan automáticamente; el lado del guest casi nunca es la causa.

**Solución**: En el host, verifica que la línea de comandos de `bhyve` incluye el dispositivo. Para `vm-bhyve`, consulta el archivo de configuración de la VM y busca líneas como `disk0_type="virtio-blk"` o `network0_type="virtio-net"`. Si el dispositivo aparece en la configuración pero sigue sin detectarse, comprueba que el hipervisor tiene permiso para acceder al recurso de respaldo (la imagen de disco, el dispositivo tap).

Dentro del guest, confirma que el kernel tiene VirtIO: `kldstat -m virtio` o `kldstat | grep -i virtio`. Si el módulo no está cargado, prueba con `kldload virtio`.

### `virtqueue_enqueue` falla con `ENOBUFS`

**Síntoma**: Un driver VirtIO intenta encolar un buffer y obtiene `ENOBUFS`.

**Causa**: La virtqueue está llena. O bien el driver no ha estado vaciando los buffers completados mediante `virtqueue_dequeue`, o el tamaño del anillo es menor del esperado y el driver está encolando más elementos de los que puede almacenar.

**Solución**: Llama a `virtqueue_dequeue` en el manejador de interrupciones para vaciar los buffers completados y liberar el estado asociado a cada uno. Si el anillo es genuinamente demasiado pequeño, negocia un tamaño mayor en el momento de la negociación de características, si el dispositivo admite `VIRTIO_F_RING_INDIRECT_DESC` u otras características similares.

Un error habitual entre los principiantes es olvidar que una operación de encolar produce un descriptor que debe ir acompañado de una operación de desencolar. Cada encolar exitoso debe producir en algún momento su correspondiente desencolar; de lo contrario, el anillo se llenará.

### El dispositivo en passthrough no aparece en el invitado

**Síntoma**: El host ha marcado un dispositivo como apto para passthrough, el invitado está configurado para utilizarlo, pero el invitado no lo detecta.

**Causa**: Varias posibles. El host no vinculó realmente el dispositivo a `ppt(4)` durante el boot (comprueba `pciconf -l` buscando `pptN`). El firmware del host no tiene el IOMMU habilitado (comprueba `dmesg | grep -i dmar` o `grep -i iommu`). El dispositivo pertenece a un grupo IOMMU que no se puede dividir (por lo que debes hacer passthrough del grupo completo).

**Solución**: Verifica cada uno de los puntos anteriores por orden. `dmesg | grep -i dmar` debería mostrar mensajes de inicialización de `dmar(4)` en hosts Intel o de `amdvi(4)` en hosts AMD. Si no aparecen, activa VT-d o AMD-Vi en el firmware. Si el dispositivo comparte grupo IOMMU, haz passthrough del grupo completo o mueve el dispositivo a un slot PCI diferente que esté mejor aislado (una tarea de administración que requiere acceso al chasis).

### Kernel panic al cargar un módulo dentro de un invitado

**Síntoma**: `kldload` de un nuevo driver dentro de un invitado provoca un kernel panic.

**Causa**: Generalmente un bug en el driver, aunque a veces es específico de VirtIO: el driver asume una característica del dispositivo que el backend no ofrece, o accede al espacio de configuración usando una disposición incorrecta (legacy frente a moderna).

**Solución**: Utiliza el invitado como plataforma de desarrollo precisamente para que los panics sean baratos. Captura la salida del panic (desde la consola serie o desde un log de `bhyve`), acota la función que falla con `ddb(4)` si lo tienes configurado, y repite el ciclo. La combinación de una VM (para que los panics sean baratos) y depuración con `printf` (para ver qué ocurre antes del panic) suele ser suficiente para corregir rápidamente un bug en un driver VirtIO.

Para bugs específicos de VirtIO, verifica con cuidado los bits de característica que negocia tu driver. Un driver que afirma soportar una característica pero luego no la implementa correctamente (por ejemplo, declara `VIRTIO_F_VERSION_1` pero usa el espacio de configuración legacy) fallará de formas inesperadas.

### El jail no puede ver un dispositivo aunque esté habilitado en las reglas de devfs

**Síntoma**: Un conjunto de reglas de devfs incluye el dispositivo y se aplica al jail, pero `ls /dev` dentro del jail no lo muestra.

**Causa**: El montaje de `devfs` dentro del jail no se volvió a montar ni se reaplicaron las reglas tras el cambio de conjunto de reglas. devfs almacena en caché la decisión de visibilidad en el momento del montaje, por lo que los cambios posteriores no se propagan hasta que se actualiza el montaje.

**Solución**: Ejecuta `devfs -m /jails/myjail/dev rule -s 100 applyset` (sustituye la ruta y el número de conjunto de reglas según corresponda) para forzar una reaplicación. Como alternativa, detén el jail, reinicia `devfs` y vuelve a iniciarlo. El comando `service devfs restart` aplica las reglas de `/etc/devfs.rules` al `/dev` del host; para los jails, generalmente es necesario reiniciar el jail.

### Privilegio denegado dentro de un jail en el que debería funcionar

**Síntoma**: Una operación del driver funciona en el host como root, pero falla con `EPERM` dentro del root de un jail.

**Causa**: La operación requiere un privilegio que `prison_priv_check` deniega por defecto. Este es el comportamiento esperado. La solución pasa por usar una operación que requiera menos privilegios, configurar el jail con `allow.*` para conceder el privilegio, o (si procede) modificar el driver para que use un privilegio diferente.

**Solución**: Examina la llamada a `priv_check` del driver e identifica qué privilegio se está comprobando. Consulta `/usr/src/sys/sys/priv.h` y `prison_priv_check` en `/usr/src/sys/kern/kern_jail.c` para comprobar si el privilegio está permitido dentro de los jails. Si no lo está, considera si la restricción es apropiada (habitualmente sí lo es) y ajusta la configuración del jail en consecuencia (`allow.raw_sockets`, `allow.mount`, etc.).

No cedas a la tentación de eliminar la llamada a `priv_check` solo para que el jail funcione. La comprobación existe por una razón; trabaja con el framework, no en su contra.

### El jail VNET no puede enviar ni recibir paquetes

**Síntoma**: Se crea un jail VNET, se mueve una interfaz a su interior, pero el tráfico de red no fluye.

**Causa**: Varias posibilidades. La interfaz no se configuró dentro del jail (cada VNET tiene su propio estado de ifconfig). No se estableció la ruta por defecto dentro del jail. El cortafuegos del host está bloqueando el tráfico entre la interfaz del jail y el resto de la red. La interfaz no está en estado `UP`.

**Solución**: Dentro del jail, ejecuta `ifconfig` y confirma que la interfaz tiene una dirección y está en estado `UP`. Ejecuta `netstat -rn` y verifica que la tabla de rutas contiene las entradas que esperas. Si utilizas `pf(4)` o `ipfw(8)` en el host, revisa las reglas: las reglas de filtrado se aplican al VNET del host, y los paquetes del jail pueden ser rechazados si el filtro del host los bloquea (según cómo esté configurada la topología).

Para configuraciones con `epair(4)` en particular, recuerda que ambos extremos necesitan configuración y que el extremo del host permanece en el host. El jail configura el extremo que se movió a su interior; el host configura el extremo que permaneció.

### El driver de passthrough PCI no logra hacer attach en el invitado

**Síntoma**: El invitado detecta el dispositivo en passthrough en `pciconf -l`, pero el driver no consigue hacer attach, o el attach se realiza correctamente pero `read`/`write` falla.

**Causa**: Generalmente una de dos cosas. El driver asume una característica específica de la plataforma que no está presente en el invitado (una tabla ACPI, una entrada en la BIOS), o el driver está programando direcciones físicas directamente en lugar de hacerlo a través de `bus_dma(9)`, y el IOMMU redirige el DMA de una manera que el driver no espera.

**Solución**: Para el primer caso, examina el código de attach del driver en busca de llamadas que lean tablas de plataforma (ACPI, FDT, etc.). Si el firmware del invitado no expone las tablas esperadas, el driver debe proporcionar valores por defecto o rechazar la inicialización de forma limpia.

Para el segundo caso, audita el código DMA del driver. Cada dirección que se programe en el dispositivo debe provenir de una operación de carga de `bus_dma(9)`, no de una llamada a `vtophys` o similar. Esto es una práctica estándar de todos modos, pero se vuelve obligatoria bajo passthrough.

### El host deja de responder cuando se inicia un invitado

**Síntoma**: Ejecutar `bhyve(8)` hace que el host se vuelva lento o deje de responder.

**Causa**: Contención de recursos. Al invitado se le ha asignado más memoria o más VCPUs de las que el host puede ceder. El host está realizando swap o girando en espera de un lock compartido.

**Solución**: Comprueba la asignación de recursos del invitado. Un invitado con toda la memoria del host dejará al host sin recursos; un invitado con más VCPUs que núcleos tenga el host causará thrashing. Una regla general habitual es dar a los invitados no más de la mitad de la memoria del host y no más VCPUs que (núcleos del host - 1), para dejar margen al propio host.

Si la lentitud persiste con una asignación razonable, comprueba en el host `top -H` para ver el proceso `bhyve(8)` y sus threads. Un uso elevado de CPU por parte de `bhyve(8)` indica que el invitado está realizando una tarea intensiva en CPU; un uso elevado de CPU por parte de los threads del kernel `vmm` sugiere demasiados VM exits, lo que puede indicar que un driver del invitado está haciendo polling de forma demasiado agresiva.

### `kldunload` se bloquea

**Síntoma**: Descargar un módulo del driver bloquea el proceso y no puede interrumpirse.

**Causa**: Algún recurso propiedad del driver sigue en uso. Un descriptor de archivo sigue abierto en el dispositivo del driver, hay un callout todavía programado, un taskqueue tiene tareas pendientes, o un thread del kernel creado por el driver no ha terminado.

**Solución**: Localiza y libera al titular del recurso. `fstat` o `lsof` lista los descriptores de archivo abiertos; `procstat -kk` muestra los threads del kernel. El manejador de descarga del módulo del driver debe vaciar todos los mecanismos asíncronos que pone en marcha: cancelar callouts, vaciar taskqueues, esperar a que los threads del kernel terminen y cerrar cualquier descriptor de archivo retenido. Si falta alguno de estos pasos, la descarga se bloquea.

Para los drivers con soporte VNET, la descarga debe limpiar correctamente el estado por VNET. Un error habitual es limpiar en `mod_event(MOD_UNLOAD)` pero olvidar que uno de los VNETs a los que está conectado el módulo no es el actual; acceder a su estado sin `CURVNET_SET` provoca bien un acceso en contexto incorrecto (rápido) o un bloqueo (lento). El patrón correcto es iterar sobre los VNETs y limpiar cada uno explícitamente.

### Bugs relacionados con la temporización que solo aparecen en VMs

**Síntoma**: El driver funciona en bare metal pero se bloquea o pierde interrupciones dentro de una VM.

**Causa**: Suposiciones de temporización que fallan bajo virtualización. La ejecución del invitado se pausa a veces durante milisegundos (durante los VM exits), y un driver que sondea un registro de estado en un bucle cerrado sin ceder el control puede no avanzar o consumir CPU en exceso.

**Solución**: Sustituye el polling cerrado por `DELAY(9)` para esperas en microsegundos, `pause(9)` para esperas cortas, o por una espera apropiada con `tsleep(9)` para esperas más largas. Utiliza diseños basados en interrupciones en lugar de polling siempre que sea posible. Comprueba con los contadores de rendimiento de virtio-blk si el driver está generando un número excesivo de VM exits.

Un driver que funciona correctamente en bare metal pero falla en una VM casi siempre está realizando alguna suposición de temporización. La solución es usar correctamente las primitivas de tiempo del kernel; funcionan en ambos entornos.

### La negociación VirtIO falla o devuelve características inesperadas

**Síntoma**: El driver registra que la negociación de características produjo un conjunto al que le faltan bits que esperabas, o la secuencia de probe y attach se completa con éxito pero el dispositivo se comporta de forma inesperada.

**Causa**: Dos categorías de problema. La primera es que el dispositivo (o su backend) anuncia un conjunto de características que no incluye el bit que solicitaste. Esto es normal cuando el backend del hipervisor es antiguo o intencionalmente mínimo. La segunda es que el código del lado del invitado solicita un bit de característica que el framework no conoce, en cuyo caso el framework puede eliminarlo silenciosamente.

**Solución**: Registra `sc->features` inmediatamente después de que `virtio_negotiate_features` retorne. Compáralo con el conjunto que solicitaste. Si al dispositivo le falta un bit que considerabas obligatorio, tu driver debe degradarse de forma controlada o rechazar el attach con un mensaje de error claro. Nunca asumas que un bit está presente sin comprobar el valor posterior a la negociación.

Para la investigación del lado del backend (si utilizas un hipervisor cuyo código fuente puedes leer, como `bhyve(8)` o QEMU), examina el anuncio de características del emulador del dispositivo. El backend es la fuente de verdad: el invitado solo ve lo que el backend anuncia. Una discrepancia entre lo que esperas y lo que ves casi siempre tiene su origen en el backend.

### `bus_alloc_resource` falla dentro de un invitado

**Síntoma**: La secuencia de attach llama a `bus_alloc_resource_any` o `bus_alloc_resource` y recibe `NULL`, lo que hace que el driver no complete el attach.

**Causa**: Bajo un hipervisor, los recursos del dispositivo (BARs, líneas IRQ, ventanas MMIO) pueden diferir de su disposición en bare metal. Un driver que tiene codificados IDs de recursos o que asume números de BAR concretos puede fallar si el hipervisor presenta una disposición diferente.

**Solución**: Usa siempre `pci_read_config(dev, PCIR_...)` para leer el contenido real de los BARs en lugar de asumirlo. Usa `bus_alloc_resource_any` con el rid obtenido de la lista de recursos, no un número codificado directamente. Si la asignación de recursos sigue fallando, compara la salida de `pciconf -lvBb` en bare metal y en el invitado para ver qué ha cambiado.

Un ejemplo concreto: un dispositivo que usa BAR 0 para MMIO y BAR 2 para I/O en bare metal puede ser configurado de forma diferente por el hipervisor. Lee siempre los BARs en tiempo de ejecución y asigna recursos basándote en lo que realmente está presente.

### `kldload` finaliza con éxito pero ningún dispositivo hace attach

**Síntoma**: `kldload` de un driver VirtIO retorna con éxito, pero `kldstat -v` no muestra ningún dispositivo vinculado al módulo y no se producen mensajes en dmesg.

**Causa**: La tabla PNP del driver no coincide con ningún dispositivo anunciado por el hipervisor. Esto es normal cuando un backend no proporciona el dispositivo esperado. Para `vtedu` en el caso práctico, este es el comportamiento esperado cuando no hay un backend de `bhyve(8)` correspondiente.

**Solución**: ejecuta `devctl list` (o `devinfo -v`) en el host o en el guest para ver qué dispositivos están presentes pero sin vincular. Si el dispositivo no aparece en absoluto, el backend no está en ejecución o está mal configurado. Si el dispositivo aparece en la lista pero sin vincular, comprueba sus identificadores PNP (`vendor`, `device` para PCI, o el ID de tipo VirtIO para VirtIO) y compáralos con la tabla PNP del driver. Una discrepancia es la causa más habitual.

Un error frecuente entre los principiantes es creer que si `kldload` tiene éxito, el driver está funcionando. Eso solo significa que el módulo se ha cargado. Usa `kldstat -v | grep yourdriver` para comprobar si algún dispositivo ha sido reclamado por el driver.

### El módulo está cargado pero el nodo `/dev` no aparece

**Síntoma**: El driver está cargado, `kldstat -v` lo muestra enlazado a un dispositivo, pero el nodo `/dev` esperado no aparece.

**Causa**: O bien el driver no llamó a `make_dev(9)`, o el montaje de `devfs` dentro del jail actual está filtrado por un conjunto de reglas que oculta el nodo, o la llamada a `make_dev` falló silenciosamente porque el número de unidad del dispositivo entró en conflicto con uno ya existente.

**Solución**: En el host, comprueba `ls /dev/yourdev*`. Si tampoco aparece allí, el driver no creó el nodo. Revisa la ruta de attach en busca de la llamada a `make_dev` y verifica su valor de retorno. Si el nodo está presente en el host pero no dentro de un jail, la causa es el conjunto de reglas de devfs. Ejecuta `devfs -m /path/to/jail/dev rule show` para ver el conjunto de reglas activo dentro del jail.

Para un driver que deba ser visible dentro de jails, la práctica correcta es documentar qué conjunto de reglas de devfs expone el nodo y proporcionar un ejemplo de conjunto de reglas en el README del driver. No des por supuesto que el administrador lo deducirá por su cuenta.

### Las interrupciones de virtqueue no se activan nunca

**Síntoma**: El manejador de interrupciones del driver nunca se invoca, aunque el driver haya enviado trabajo a la virtqueue.

**Causa**: Puede deberse a varias causas. El backend nunca procesa el trabajo (error en el backend). El driver no registró el manejador de interrupciones correctamente. El driver no llamó a `virtio_setup_intr`, por lo que no existe ningún cableado de interrupciones. El driver deshabilitó las interrupciones mediante `virtqueue_disable_intr` y nunca las volvió a habilitar.

**Solución**: Comprueba cada paso de forma sistemática. En la ruta de attach, verifica que `virtio_setup_intr` fue invocado y devolvió 0. En el manejador de interrupciones, verifica que estás rehabilitando las interrupciones cuando corresponde. Añade un `printf` al principio del manejador para confirmar que nunca se llama. Si el manejador realmente no se llama nunca, ejecuta `vmstat -i | grep yourdriver` para ver el recuento de interrupciones; un recuento de cero confirma que la interrupción no está llegando.

Si el recuento es distinto de cero pero el manejador no realiza ningún trabajo, el manejador se ejecuta pero no encuentra nada en la virtqueue. Esto sugiere que el backend está enviando acuses de recibo pero no produce completaciones reales; revisa el backend.

### Tormenta de interrupciones bajo virtualización

**Síntoma**: Dentro de una VM, `vmstat -i` muestra tasas de interrupciones de cientos de miles por segundo, y el uso de la CPU es elevado incluso sin trabajo real.

**Causa**: Una interrupción que no se está limpiando, o un driver que genera una interrupción por cada evento sin coalescencia. Con INTx en particular, una interrupción activada por nivel que permanece activa provoca que el manejador sea invocado en un bucle.

**Solución**: Para MSI-X, confirma que el manejador acusa recibo de las completaciones y que el anillo de la virtqueue se está vaciando. Para INTx, confirma que el manejador limpia el registro de estado de interrupción del dispositivo. Para VirtIO en particular, negocia `VIRTIO_F_RING_EVENT_IDX` si el backend lo soporta; esto permite al dispositivo suprimir interrupciones innecesarias.

Consulta el patrón de la sección 9 con `virtqueue_disable_intr` / `virtqueue_enable_intr`. Un driver correcto deshabilita las interrupciones al entrar, vacía el anillo y solo vuelve a habilitarlas cuando el anillo está vacío. No seguir esta estructura es una causa frecuente de tormentas de interrupciones.

### Fallos de DMA solo bajo passthrough

**Síntoma**: El driver funciona correctamente con un dispositivo emulado, pero cuando el mismo hardware se traspasa mediante `ppt(4)`, las transferencias DMA fallan silenciosamente o corrompen la memoria.

**Causa**: Lo más habitual es que el driver esté programando direcciones físicas directamente en lugar de pasar por `bus_dma(9)`. Bajo emulación, el hipervisor intercepta todo el I/O y traduce las direcciones al vuelo, ocultando el error. Bajo passthrough, el dispositivo realiza DMA directamente a través del IOMMU, y la dirección física que programó el driver no es la dirección de bus que el IOMMU espera.

**Solución**: Audita cada punto donde el driver calcula una dirección para programar en el dispositivo. Cada una debe proceder de `bus_dma_load`, `bus_dma_load_mbuf` o similar, no de `vtophys` ni de una dirección física directa. Esta es una disciplina obligatoria para passthrough y se recomienda encarecidamente para todos los drivers.

Un diagnóstico útil: habilita el registro detallado del IOMMU (`sysctl hw.dmar.debug=1` en Intel, o el equivalente en AMD) y observa el registro del kernel en busca de fallos de página del IOMMU mientras el driver se ejecuta. Un fallo de página revela exactamente qué dirección de bus intentó acceder el dispositivo; si no coincide con una región mapeada, el cálculo de direcciones del driver es incorrecto.

### Un dispositivo VirtIO aparece con el tipo incorrecto

**Síntoma**: El guest ve un dispositivo VirtIO en la dirección PCI esperada, pero `pciconf -lv` o `devinfo` informa de un tipo de dispositivo VirtIO diferente al esperado.

**Causa**: Confusión de device ID. El vendor ID de PCI para VirtIO es siempre 0x1af4, pero el device ID codifica el tipo VirtIO, y distintas versiones de VirtIO utilizan rangos de device ID diferentes. VirtIO heredado usa 0x1000 + VIRTIO_ID. VirtIO moderno usa 0x1040 + VIRTIO_ID. Un hipervisor que expone una mezcla de dispositivos modernos y heredados puede confundir a un driver que solo examina uno de los rangos.

**Solución**: El transporte `virtio_pci` de FreeBSD gestiona ambos rangos de forma transparente, por lo que la mayoría de los drivers no se ven afectados. Para los autores de drivers que inspeccionan `pciconf -lv` directamente, conviene saber que tanto el rango 0x1000-0x103f (heredado) como el 0x1040-0x107f (moderno) corresponden a VirtIO. La macro `VIRTIO_DRIVER_MODULE` registra el driver con ambos transportes y se comporta correctamente para ambos rangos de device ID.

### Un ioctl específico de jail falla con ENOTTY

**Síntoma**: Un ioctl que funciona en el host devuelve `ENOTTY` cuando se emite desde dentro de un jail, aunque el número de ioctl sea reconocido por el driver.

**Causa**: El manejador ioctl del driver comprueba la visibilidad del jail y devuelve `ENOTTY` para ocultar la existencia del ioctl a los llamadores enjaulados. Es un patrón de seguridad por oscuridad que utilizan algunos drivers que exponen operaciones administrativas exclusivas del host a través de dispositivos que, de otro modo, serían visibles dentro del jail.

**Solución**: Si el jail debería poder usar el ioctl, revisa la comprobación de visibilidad del driver. El enfoque idiomático es devolver `EPERM` (permiso denegado) en lugar de `ENOTTY` cuando una operación existe pero no está permitida; `ENOTTY` implica que el ioctl no existe, lo que puede confundir a los llamadores. Valora si el llamador enjaulado debería ver el ioctl; en caso afirmativo, elimina la lógica de ocultación y usa `priv_check` para el control de acceso.

### El movimiento de VNET filtra estado por VNET

**Síntoma**: Tras mover una interfaz hacia dentro y fuera de una VNET varias veces, el uso de memoria del kernel crece hasta desencadenar un evento de presión de memoria.

**Causa**: El driver asigna estado por VNET cuando una interfaz entra en una VNET, pero no lo libera cuando la interfaz sale. Cada movimiento de VNET filtra una cantidad fija de memoria.

**Solución**: Implementa correctamente el ciclo de vida del movimiento VNET. Cuando una interfaz entra en una VNET (`if_vmove` hacia la nueva VNET), asigna el estado por VNET. Cuando sale (`if_vmove` de salida), libéralo. El par `CURVNET_SET` y `CURVNET_RESTORE` delimita el contexto VNET; úsalos al asignar o liberar.

Consulta `/usr/src/sys/net/if_tuntap.c` para ver una implementación correcta del movimiento VNET. El ciclo de vida es sutil y fácil de implementar mal; una implementación de referencia es el mejor maestro.

### El kernel del guest entra en pánico con "Fatal Trap 12" en el primer I/O

**Síntoma**: El guest arranca, el driver hace el attach, y el primer I/O desde el espacio de usuario al dispositivo del driver provoca un pánico con el mensaje "Fatal Trap 12: page fault while in kernel mode".

**Causa**: Casi siempre se trata de una desreferencia de puntero NULL en la ruta `read`, `write` o `ioctl` del driver. Bajo virtualización, el fallo es inmediato en lugar de simplemente corromper y continuar, porque la protección de memoria del guest es precisa.

**Solución**: Usa el depurador del kernel (`ddb(4)` o `dtrace(1)`) para localizar la instrucción que falla. Una causa habitual es un `dev->si_drv1 = sc` que se olvidó en attach, de modo que cuando el espacio de usuario abre `/dev/yourdriver` y llama a `read`, `dev->si_drv1` es NULL. La solución es asignar siempre `si_drv1` en attach, justo después de crear el cdev.

Bajo VMs, estos pánicos son baratos: corrige el código, reconstruye y recarga. En hardware real, cada pánico cuesta un reinicio. Una razón más para desarrollar bajo virtualización.

### La migración en vivo falla o provoca un cuelgue del guest

**Síntoma**: Un guest que se ejecuta bajo un hipervisor compatible con migración en vivo (actualmente limitada en `bhyve(8)`, más común en Linux KVM y VMware) se migra a un host diferente, y el guest se cuelga o se corrompe tras la migración.

**Causa**: El driver del guest mantiene estado vinculado al host de origen (un TSC físico específico, un mapeo IOMMU específico, un dispositivo traspasado específico). La migración transfiere la memoria y el estado de la CPU del guest, pero no puede transferir el estado del hardware físico.

**Solución**: Para los autores de drivers, el consejo es sencillo: no almacenes en caché valores que estén vinculados al host físico. Vuelve a leer la frecuencia del TSC desde `timecounter(9)` en lugar de guardar una copia local. No traspases dispositivos PCI que necesites migrar. Para dispositivos VirtIO, la migración en vivo está contemplada en el estándar, y el driver del guest no necesita código especial.

Si estás escribiendo un driver para un entorno con soporte de migración en vivo, la regla de diseño principal es la siguiente: todo el estado debe estar en la memoria del guest; cualquier cosa del lado del host debe poder recrearse tras la migración. Los drivers VirtIO estándar cumplen este requisito porque el estado de la virtqueue está en la memoria del guest.

### Aparecen entradas de devfs inesperadas dentro de un jail

**Síntoma**: Un jail tiene un conjunto de reglas de devfs mínimo, pero aparecen entradas que el administrador no esperaba.

**Causa**: O bien el conjunto de reglas no se aplicó correctamente, o el jail heredó el conjunto de reglas por defecto antes de que se aplicara el personalizado, o apareció un nuevo dispositivo después de establecer el conjunto de reglas.

**Solución**: Ejecuta `devfs -m /jail/path/dev rule show` para ver qué reglas están activas. Compáralas con `/etc/devfs.rules`. Si una regla posterior añade visibilidad que una regla anterior denegó, el orden es incorrecto. Si el jail se inició antes de que el conjunto de reglas estuviera finalizado, reinicia el jail.

Una práctica robusta es iniciar siempre un jail con un conjunto de reglas conocido especificado en `/etc/jail.conf`, en lugar de depender del valor predeterminado de devfs. La directiva `devfs_ruleset = NNN` en la configuración del jail garantiza que el montaje devfs del jail use el conjunto de reglas esperado desde el momento en que el jail arranca.

### Dos drivers disputan el mismo dispositivo

**Síntoma**: Cargar el driver A funciona, pero cargar el driver B después del A (o viceversa) provoca que uno de ellos falle en el attach con un mensaje críptico sobre conflictos de recursos.

**Causa**: Dos drivers intentan reclamar el mismo dispositivo. Esto puede ocurrir si el dispositivo tiene varios drivers válidos (por ejemplo, un driver genérico y uno específico para una variante de chipset concreta) y el orden de carga determina cuál gana.

**Solución**: Newbus de FreeBSD arbitra la prioridad de los drivers mediante el orden de `DRIVER_MODULE`, pero la semántica exacta depende de qué driver hizo el attach primero. La regla general es que, una vez que un dispositivo está enlazado, otro driver no puede arrebatárselo. Si necesitas cambiar de driver, desenlaza el primero (`devctl detach yourdev0`) antes de cargar el segundo.

Bajo virtualización, esto puede aparecer cuando cargas un driver de prueba después de que el framework VirtIO ya haya enlazado un driver de producción al dispositivo. El driver de prueba debe usar una entrada PNP diferente o desenlazar explícitamente el existente.

### `vm_guest` muestra "no" dentro de una VM evidente

**Síntoma**: `sysctl kern.vm_guest` devuelve "no" dentro de un guest que claramente se está ejecutando bajo un hipervisor.

**Causa**: El hipervisor no está estableciendo el bit de presencia de hipervisor del CPUID, o está estableciendo una cadena de proveedor que el kernel de FreeBSD no reconoce. Esto puede ocurrir con hipervisores exóticos o personalizados.

**Corrección**: Esto es informativo, no una corrección como tal. Si tu driver necesita detectar la virtualización pero `vm_guest` no coopera, usa señales alternativas: la presencia de dispositivos VirtIO, la ausencia de hardware físico que estaría presente en bare metal, o entradas de CPUID específicas que exponen información propia del hipervisor. Ten en cuenta, sin embargo, que la detección precisa del hipervisor es difícil de garantizar; diseña tu driver para que funcione correctamente con independencia del entorno, y usa `vm_guest` solo para valores por defecto no críticos.

### Enfoque general de diagnóstico

Cuando algo falla en un entorno virtualizado o en contenedores, el patrón de diagnóstico es siempre el mismo. En primer lugar, reduce el entorno a la configuración más simple que reproduzca el problema: deja la VM con un único dispositivo VirtIO, elimina los jails adicionales, deshabilita VNET si no es necesario. En segundo lugar, prueba la misma operación en la capa superior: si el problema aparece en un jail, pruébalo en el host; si aparece en el host, pruébalo en un guest; si aparece en un guest, pruébalo sobre hardware real. La capa en la que el problema desaparece suele ser la capa en la que el problema reside.

En tercer lugar, una vez que hayas localizado la capa, añade registro. `printf` en el kernel sigue siendo una herramienta de depuración válida; combinado con `dmesg`, te proporciona una traza con marcas de tiempo de lo que hace el driver. `dtrace(1)` es más potente, pero tiene un coste de configuración mayor. Para un primer diagnóstico, `printf` suele ser suficiente.

En cuarto lugar, si el problema es una condición de carrera o un problema de temporización, simplifica antes de complicar. Una condición de carrera que ocurre una vez en diez mil iteraciones puede investigarse con un bucle de prueba que ejecute diez mil iteraciones. Un problema de temporización bajo ejecución en VM puede hacerse reproducible fijando la VM a CPUs concretas del host y deshabilitando el escalado de frecuencia.

Nada de esto es específico de la virtualización. Es una buena disciplina general de depuración. La virtualización resulta ser un entorno donde esa disciplina rinde frutos rápidamente, porque las capas están claramente separadas y pueden intercambiarse con facilidad.

## En resumen

La virtualización y la contenerización son dos nombres para lo que son en FreeBSD: dos respuestas distintas a la misma pregunta amplia. La virtualización multiplica kernels; la contenerización subdivide uno. Un driver vive dentro de un kernel, y la forma en que interactúa con su entorno depende de cuál de estas opciones esté en uso.

El lado de la virtualización es donde vive la historia de VirtIO. VirtIO es la interfaz paravirtual madura, estandarizada y de alto rendimiento entre un driver guest y un dispositivo proporcionado por el hipervisor. El framework VirtIO de FreeBSD (en `/usr/src/sys/dev/virtio/`) implementa el estándar de forma limpia, y el driver `virtio_random` que estudiaste en la Sección 3 es un ejemplo mínimo de cómo se estructura un driver VirtIO. Los conceptos clave, la negociación de características, la gestión de virtqueue y la notificación, son los mismos tanto si el dispositivo es un generador de números aleatorios como si es una tarjeta de red o un dispositivo de bloques. Apréndelos una vez y cada driver VirtIO se vuelve más accesible.

El mecanismo de detección del hipervisor, `vm_guest`, ofrece al driver una pequeña ventana al entorno. Es útil para ajustar valores por defecto, pero peligroso cuando se usa para sortear errores. La actitud correcta es «esto es informativo»; la incorrecta es «esto es un punto de bifurcación».

El lado del host, donde FreeBSD ejecuta `bhyve(8)` y proporciona dispositivos a los guests, es donde el desarrollo de drivers se encuentra con el desarrollo de hipervisores. La mayoría de los autores de drivers nunca tocan `vmm(4)` directamente, pero la funcionalidad de PCI passthrough vale la pena entenderla, porque ejercita las rutas de detach y reattach que un driver bien escrito ya soporta. Un driver que sobrevive a un ciclo completo a través de `ppt(4)` es un driver cuyo ciclo de vida es honesto.

El lado de la contenerización es donde los jails, los rulesets de devfs y VNET se unen. Los jails comparten el kernel, por lo que los drivers no se multiplican, sino que se filtran. El filtro opera en tres ejes: qué dispositivos puede ver el jail (rulesets de devfs), qué privilegios puede ejercer (`prison_priv_check` y `allow.*`), y cuánto puede consumir (`rctl(8)` y `racct(9)`). Para el autor de un driver, las decisiones de diseño son pequeñas y locales: elige nombres de `devfs` sensatos, llama a `priv_check` correctamente y gestiona los fallos de asignación con elegancia.

El framework VNET extiende el modelo de jails al stack de red. Es la parte de los jails que más se acerca a requerir cooperación explícita del driver. Los drivers que escriben en el estado por-VNET desde fuera de un punto de entrada del stack de red (por ejemplo, desde callouts) deben establecer el contexto VNET con `CURVNET_SET`. Los drivers que superan `if_vmove` limpiamente son drivers que funcionarán en jails VNET.

En conjunto, estos mecanismos configuran FreeBSD como una plataforma tanto para cargas de trabajo de hipervisor como para cargas de trabajo de contenedor. Un único kernel, un único conjunto de drivers, varias formas distintas de que ese driver se presente a sus usuarios. Entender los mecanismos es lo que te permite escribir drivers que funcionen correctamente en todos ellos, sin necesidad de tratar ninguno como un caso especial.

La disciplina que este capítulo ha defendido, a veces de forma explícita y a veces con el ejemplo, es una continuación del tema del Capítulo 29. Escribe abstracciones limpias. Usa las APIs del framework. No las sortees. No inventes tus propios mecanismos de DMA, privilegios o visibilidad; FreeBSD ya tiene los suyos, bien probados. Si haces todo esto, tu driver funcionará en entornos que no tenías en mente cuando lo escribiste, y esa es la mejor definición de portable que puede tener un driver.

Si has trabajado en los laboratorios, ahora tienes experiencia práctica arrancando un guest `bhyve`, escribiendo un módulo pequeño que usa `vm_guest`, poniendo un dispositivo de caracteres detrás de un ruleset de devfs en un jail y moviendo una interfaz de red a través de VNET. Esas cuatro habilidades son todo lo que necesitas para empezar a trabajar de verdad con código de driver que se ejecuta en entornos virtualizados y en contenedores. Todo lo demás en el capítulo es contexto de apoyo.

Si abordaste uno o más de los desafíos, has ido más lejos: hacia backends de simulación, hacia interfaces con soporte de VNET, hacia el propio emulador bhyve o hacia la automatización de pruebas. Cualquiera de estos es un trabajo genuino de desarrollo de drivers en FreeBSD, y el aprendizaje se acumula. Cada hora que dedicas a estos temas se recupera en cada driver que escribas después.

Una nota final sobre la actitud. La virtualización y la contenerización pueden resultar abrumadoras porque introducen muchas piezas nuevas a la vez: hipervisores, dispositivos paravirtuales, jails, VNET, rulesets, privilegios. Pero cada pieza tiene un propósito claro y una API pequeña y bien diseñada. La sensación de agobio desaparece en cuanto has visto cada una por separado, y el ritmo de este capítulo fue elegido para permitirte hacer precisamente eso. Si sigues sintiéndote abrumado, vuelve a la Sección 3 (VirtIO) o a la Sección 6 (jails y devfs) y relee con una pregunta pequeña y concreta en mente. Las respuestas están ahí.

## Mirando hacia adelante: Security and Privilege

El Capítulo 31, «Security and Privilege in Device Drivers», se asienta directamente sobre los cimientos establecidos en este capítulo. Los jails y las máquinas virtuales son un tipo de límite de seguridad; los drivers tienen muchos otros. Un driver que expone un `ioctl` es un driver que ha creado una nueva interfaz hacia el kernel, y esa interfaz debe comprobarse, validarse y restringirse.

El Capítulo 31 cubrirá en profundidad el framework de privilegios (aquí viste `priv_check`; el Capítulo 31 repasa la lista completa), la estructura `ucred(9)` y cómo fluyen las credenciales por el kernel, el framework de capacidades `capsicum(4)` para restricciones más finas y el framework MAC (Mandatory Access Control) para seguridad basada en políticas. También volverá a los jails desde una perspectiva centrada en la seguridad, complementando la perspectiva centrada en los contenedores de este capítulo.

El hilo que recorre los Capítulos 29, 30 y 31 es el entorno. El Capítulo 29 trataba sobre el entorno arquitectónico: ejecutar en el mismo kernel con diferentes buses o anchuras de bit. El Capítulo 30 trataba sobre el entorno operacional: ejecutar dentro de una VM, un contenedor o en un host. El Capítulo 31 trata sobre el entorno de políticas: ejecutar bajo las restricciones que un administrador consciente de la seguridad elige aplicar. Un driver que gestiona bien los tres tipos de entorno es un driver que puede desplegarse en cualquier lugar donde se ejecute FreeBSD.

Con el Capítulo 31, la Parte 7 se acerca a su punto medio. Los capítulos restantes de la parte se dedican a la depuración (Capítulo 32), las pruebas (Capítulo 33), el ajuste de rendimiento (Capítulo 34) y temas especializados de drivers en capítulos posteriores. Cada uno se asienta sobre lo que has aprendido hasta ahora. Tómate tiempo para dejar que este capítulo se asiente; los conceptos seguirán siendo útiles a medida que avances.

## Caso práctico: diseño de un driver VirtIO pedagógico

Este caso práctico final reúne los hilos del capítulo en un único recorrido de diseño. No presenta una implementación completa. En su lugar, recorre las decisiones que tomarías si te sentaras hoy a diseñar un driver VirtIO pedagógico llamado `vtedu`. El driver no hace nada útil en producción, pero ejercita suficiente superficie de VirtIO para ser una valiosa herramienta de enseñanza para futuros lectores.

### Para qué sirve vtedu

Imagina que `vtedu` está pensado para servir como driver de ejemplo en un futuro taller de FreeBSD sobre dispositivos paravirtualizados. Su función es exponer una única virtqueue, aceptar solicitudes de escritura desde el espacio de usuario, pasarlas a través de la virtqueue a un backend en `bhyve(8)` (que hace algo simple como devolver los bytes con eco) y entregar los bytes devueltos al espacio de usuario. Debe ser lo suficientemente pequeño para leerlo en una tarde y lo suficientemente completo para demostrar el ciclo de vida VirtIO completo.

Las decisiones de diseño que siguen explican cada paso. Un lector que termine esta sección debería ser capaz de razonar sobre cualquier driver VirtIO similar en los mismos términos.

### Elección del identificador de dispositivo

VirtIO define un conjunto de IDs de dispositivo bien conocidos en `/usr/src/sys/dev/virtio/virtio_ids.h`. Para un driver pedagógico, es apropiado un ID reservado o experimental. La especificación VirtIO reserva algunos rangos para dispositivos «específicos del fabricante», y un driver de taller elegiría uno de esos.

Para `vtedu`, elegimos un hipotético `VIRTIO_ID_EDU = 0xfff0` (elegido para no colisionar con IDs de dispositivo reales). El backend correspondiente en `bhyve(8)` registraría el mismo ID. Un proyecto real coordinaría con los mantenedores de `bhyve(8)` la asignación del ID.

### Definición de las características

Un driver de enseñanza debería negociar un bit de característica significativo para que el lector vea la negociación de características en acción. `vtedu` define una característica:

```c
#define VTEDU_F_UPPERCASE	(1ULL << 0)
```

Cuando se negocia, el backend devuelve los bytes de entrada en mayúsculas. Cuando no se negocia, los devuelve sin cambios. El driver anuncia la característica, el backend puede o no soportarla, y la negociación produce el resultado que ambas partes pueden soportar.

Este tipo de característica trivial es un recurso didáctico. En un driver real, las características corresponden a capacidades reales; en `vtedu`, la característica existe solo para mostrar cómo funciona la negociación.

### Estructura del softc

El softc es el estado por instancia:

```c
struct vtedu_softc {
	device_t		dev;
	struct virtqueue	*vq;
	uint64_t		features;
	struct mtx		lock;
	struct cdev		*cdev;
	struct sglist		*sg;
	char			buf[VTEDU_BUF_SIZE];
	size_t			buf_len;
};
```

El handle del dispositivo, el puntero a la virtqueue, la máscara de características negociadas, un mutex que protege el acceso serializado del driver, un `cdev` para la interfaz en espacio de usuario, una lista scatter-gather preasignada, un buffer para los datos y su longitud actual.

### Registro del transporte

El módulo usa `VIRTIO_DRIVER_MODULE` como siempre:

```c
static device_method_t vtedu_methods[] = {
	DEVMETHOD(device_probe,		vtedu_probe),
	DEVMETHOD(device_attach,	vtedu_attach),
	DEVMETHOD(device_detach,	vtedu_detach),
	DEVMETHOD_END
};

static driver_t vtedu_driver = {
	"vtedu",
	vtedu_methods,
	sizeof(struct vtedu_softc)
};

VIRTIO_DRIVER_MODULE(vtedu, vtedu_driver, vtedu_modevent, NULL);
MODULE_VERSION(vtedu, 1);
MODULE_DEPEND(vtedu, virtio, 1, 1, 1);
```

La información PNP de `vtedu` anuncia `VIRTIO_ID_EDU`, por lo que el framework vincula el driver a cualquier dispositivo VirtIO de ese tipo, ya sea con transporte PCI o MMIO.

### Probe y attach

El probe es una sola línea usando `VIRTIO_SIMPLE_PROBE`. El attach configura el dispositivo en el orden estándar:

```c
static int
vtedu_attach(device_t dev)
{
	struct vtedu_softc *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;
	mtx_init(&sc->lock, device_get_nameunit(dev), NULL, MTX_DEF);

	virtio_set_feature_desc(dev, vtedu_feature_descs);

	error = vtedu_negotiate_features(sc);
	if (error != 0)
		goto fail;

	error = vtedu_alloc_virtqueue(sc);
	if (error != 0)
		goto fail;

	error = virtio_setup_intr(dev, INTR_TYPE_MISC);
	if (error != 0)
		goto fail;

	sc->sg = sglist_alloc(2, M_WAITOK);
	sc->cdev = make_dev(&vtedu_cdevsw, device_get_unit(dev),
	    UID_ROOT, GID_WHEEL, 0600, "vtedu%d", device_get_unit(dev));
	if (sc->cdev == NULL) {
		error = ENXIO;
		goto fail;
	}
	sc->cdev->si_drv1 = sc;

	device_printf(dev, "attached (features=0x%lx)\n",
	    (unsigned long)sc->features);
	return (0);

fail:
	vtedu_detach(dev);
	return (error);
}
```

Esto sigue el ritmo estándar: negociar, asignar cola, configurar interrupciones, establecer la interfaz en espacio de usuario. Es estructuralmente idéntico al attach de `virtio_random`, con la creación de un `cdev` añadida porque `vtedu` expone un dispositivo de caracteres.

### Negociación de características

La negociación es sencilla:

```c
static int
vtedu_negotiate_features(struct vtedu_softc *sc)
{
	uint64_t features = VIRTIO_F_VERSION_1 | VTEDU_F_UPPERCASE;

	sc->features = virtio_negotiate_features(sc->dev, features);
	return (virtio_finalize_features(sc->dev));
}
```

El driver anuncia las dos características, recibe la intersección y finaliza. La intersección indica al driver si `VTEDU_F_UPPERCASE` está activo. El código posterior usa `virtio_with_feature(sc->dev, VTEDU_F_UPPERCASE)` para comprobarlo.

### Asignación de cola

Se asigna una única virtqueue con un callback de interrupción:

```c
static int
vtedu_alloc_virtqueue(struct vtedu_softc *sc)
{
	struct vq_alloc_info vq_info;

	VQ_ALLOC_INFO_INIT(&vq_info, 0, vtedu_vq_intr, sc, &sc->vq,
	    "%s request", device_get_nameunit(sc->dev));

	return (virtio_alloc_virtqueues(sc->dev, 0, 1, &vq_info));
}
```

El tamaño máximo del descriptor indirecto es cero (sin indirecciones), el callback de interrupción es `vtedu_vq_intr`, y el puntero a la virtqueue se almacena en `sc->vq`. Una sola cola es suficiente para un patrón de solicitud-respuesta en el que se utiliza la misma cola para ambas direcciones.

### La interfaz de dispositivo de caracteres

El espacio de usuario abre `/dev/vtedu0` y escribe bytes en él. El driver los acepta, emite una petición VirtIO, espera la respuesta y la expone de vuelta al espacio de usuario a través de read.

```c
static int
vtedu_write(struct cdev *dev, struct uio *uio, int flags __unused)
{
	struct vtedu_softc *sc = dev->si_drv1;
	size_t n;
	int error;

	n = uio->uio_resid;
	if (n == 0 || n > VTEDU_BUF_SIZE)
		return (EINVAL);

	mtx_lock(&sc->lock);
	error = uiomove(sc->buf, n, uio);
	if (error == 0) {
		sc->buf_len = n;
		error = vtedu_submit(sc);
	}
	mtx_unlock(&sc->lock);
	return (error);
}
```

La escritura copia bytes en el buffer del softc, establece `buf_len` y llama a `vtedu_submit`, que realiza el encolado VirtIO y la notificación.

```c
static int
vtedu_submit(struct vtedu_softc *sc)
{
	int error;

	sglist_reset(sc->sg);
	error = sglist_append(sc->sg, sc->buf, sc->buf_len);
	if (error != 0)
		return (error);

	error = virtqueue_enqueue(sc->vq, sc, sc->sg, 1, 1);
	if (error != 0)
		return (error);

	virtqueue_notify(sc->vq);
	return (0);
}
```

Un segmento legible (la escritura del driver en el buffer) y uno escribible (la escritura del resultado por parte del dispositivo). La cookie es el propio `sc`; el manejador de interrupciones lo recibirá de vuelta cuando llegue la confirmación de finalización.

### El manejador de interrupciones

Las finalizaciones se procesan de forma diferida mediante taskqueue o de forma inmediata. Por simplicidad pedagógica, `vtedu` las procesa de inmediato en el callback de interrupción:

```c
static void
vtedu_vq_intr(void *arg)
{
	struct vtedu_softc *sc = arg;
	void *cookie;
	uint32_t len;

	mtx_lock(&sc->lock);
	while ((cookie = virtqueue_dequeue(sc->vq, &len)) != NULL) {
		/* cookie == sc; len is the length the device wrote. */
		sc->buf_len = len;
		wakeup(sc);
	}
	mtx_unlock(&sc->lock);
}
```

El manejador drena todas las finalizaciones, actualiza el softc y despierta a cualquier proceso dormido esperando en el buffer. En un driver real esto sería más elaborado; para `vtedu`, es todo lo que necesitamos.

### El camino de lectura

El espacio de usuario lee entonces el resultado:

```c
static int
vtedu_read(struct cdev *dev, struct uio *uio, int flags __unused)
{
	struct vtedu_softc *sc = dev->si_drv1;
	int error;

	mtx_lock(&sc->lock);
	while (sc->buf_len == 0) {
		error = mtx_sleep(sc, &sc->lock, PCATCH, "vteduR", 0);
		if (error != 0) {
			mtx_unlock(&sc->lock);
			return (error);
		}
	}
	error = uiomove(sc->buf, sc->buf_len, uio);
	sc->buf_len = 0;
	mtx_unlock(&sc->lock);
	return (error);
}
```

Si no hay ningún resultado disponible, la lectura duerme sobre `sc`, al que el manejador de interrupciones despertará. Este es el patrón estándar de "bloquear hasta que esté listo" para dispositivos de caracteres con I/O lenta en el dispositivo subyacente.

### Detach

El detach invierte el attach de forma limpia:

```c
static int
vtedu_detach(device_t dev)
{
	struct vtedu_softc *sc = device_get_softc(dev);

	if (sc->cdev != NULL)
		destroy_dev(sc->cdev);
	if (sc->sg != NULL)
		sglist_free(sc->sg);
	virtio_stop(dev);
	if (mtx_initialized(&sc->lock))
		mtx_destroy(&sc->lock);
	return (0);
}
```

`virtio_stop` reinicia el estado del dispositivo para que deje de generar interrupciones. El `cdev` se destruye, la lista scatter-gather se libera y el mutex se destruye.

### El conjunto completo de vtedu

Este recorrido ha tocado todos los elementos principales de un driver VirtIO:

1. Selección de ID de dispositivo e información PNP.
2. Definición de características y negociación.
3. Disposición del softc y uso de locks.
4. Asignación de virtqueues con callback de interrupción.
5. Interfaz de espacio de usuario a través de `cdev`.
6. Envío de peticiones mediante encolado y notificación.
7. Gestión de finalizaciones a través del callback de interrupción.
8. Detach limpio.

El driver completo, con implementación íntegra, cabe en unas 300 líneas de C. Eso es menos que `virtio_random` una vez que añades el código de soporte del backend. Dado que la interfaz de espacio de usuario es más rica que la de `virtio_random` (que se oculta dentro del framework `random(4)`), el código es ligeramente mayor en total, pero la parte específica de VirtIO no es más grande.

### Uso de vtedu para la enseñanza

Un driver `vtedu` se usaría en un taller de la siguiente manera:

1. El instructor comienza demostrando la carga del driver, su attach y el procesamiento de un ciclo completo de escritura-lectura.
2. Los estudiantes siguen el proceso escribiendo las secciones clave a partir de una hoja de referencia.
3. El instructor introduce la negociación de características mostrando qué ocurre cuando se negocia `VTEDU_F_UPPERCASE` (las salidas llegan en mayúsculas) frente a cuando no se negocia.
4. Los estudiantes modifican el driver para añadir una segunda característica: "invertir los bytes". Aprenden cómo se combinan las características.
5. Por último, el instructor muestra cómo ejecutar el driver en una jail (funciona sin más, siempre que `vtedu0` esté en el ruleset de devfs), ilustrando el aspecto de contenedorización del capítulo.

Este es un diseño pedagógico, no uno de producción. Su propósito es mostrar cómo encajan las piezas. Un lector que haya seguido este caso práctico debería ser capaz de escribir un driver similar por su cuenta, partiendo de cero o tomando `virtio_random.c` como plantilla.

### Lo que vtedu no hace

Con total honestidad: `vtedu` tal como se describe aquí omite varias cosas que incluiría un driver de producción. No admite múltiples peticiones en vuelo simultáneamente (el lock serializa todo). No gestiona las situaciones de cola llena de forma elegante (asume una petición a la vez). No tiene limpieza a nivel de módulo para el evento de módulo (solo por dispositivo). No demuestra descriptores indirectos (porque la característica no es relevante para mensajes de 256 bytes).

Cada uno de estos puntos es un ejercicio que el lector puede abordar tras comprender el diseño base. Los ejercicios de desafío del capítulo apuntan a algunos de ellos; un taller dedicado los desarrollaría más.

### Un driver de enseñanza frente a un driver real

Antes de despedirnos de `vtedu`, conviene señalar la diferencia entre un driver de enseñanza y uno real. Un driver de enseñanza está diseñado para ser legible. Su código hace que cada concepto sea visible, a menudo a costa de optimizaciones inteligentes. Un driver real está diseñado para la fiabilidad y el rendimiento. Comprime los caminos habituales, añade gestión de errores para cada caso límite y optimiza para la carga de trabajo real.

La tentación al pasar del aprendizaje a la producción es partir del driver de enseñanza e ir añadiendo características. Eso suele producir peor código que empezar con un boceto arquitectónico e ir rellenándolo. Un driver de enseñanza es una referencia; un driver real es un sistema. Los dos no están en el mismo continuo.

Como autor de drivers, tu trabajo es entender el driver de enseñanza lo suficientemente bien como para que las decisiones de diseño del driver real se vuelvan claras. El tratamiento de VirtIO en este capítulo está pensado para llevarte a esa comprensión. El siguiente paso es tuyo.

## Apéndice: referencia rápida

Las tablas de referencia a continuación condensan los datos clave del capítulo en un formato al que puedes volver durante el trabajo con drivers. No sustituyen al texto explicativo; considéralas la hoja de referencia de una página para el día en que estés escribiendo código y necesites un detalle concreto.

### Funciones del API core de VirtIO

| Función | Propósito |
|----------|---------|
| `virtio_negotiate_features(dev, mask)` | Anunciar y negociar bits de características. |
| `virtio_finalize_features(dev)` | Cerrar la negociación de características. |
| `virtio_with_feature(dev, feature)` | Comprobar si una característica está negociada. |
| `virtio_alloc_virtqueues(dev, flags, nvqs, info)` | Asignar un conjunto de virtqueues. |
| `virtio_setup_intr(dev, type)` | Instalar los manejadores de interrupciones negociados. |
| `virtio_read_device_config(dev, offset, dst, size)` | Leer la configuración específica del dispositivo. |
| `virtio_write_device_config(dev, offset, src, size)` | Escribir la configuración específica del dispositivo. |

### Funciones de `virtqueue(9)`

| Función | Propósito |
|----------|---------|
| `virtqueue_enqueue(vq, cookie, sg, readable, writable)` | Colocar una cadena scatter-gather en el anillo disponible. |
| `virtqueue_dequeue(vq, &len)` | Extraer una cadena completada del anillo de uso. |
| `virtqueue_notify(vq)` | Notificar al host que hay trabajo nuevo disponible. |
| `virtqueue_poll(vq, &len)` | Esperar una finalización y devolverla. |
| `virtqueue_empty(vq)` | Comprobar si la cola tiene trabajo pendiente. |
| `virtqueue_full(vq)` | Comprobar si la cola tiene espacio para otro encolado. |

### Valores de vm_guest

| Constante | Cadena vía `kern.vm_guest` | Significado |
|----------|---------------------------|---------|
| `VM_GUEST_NO` | `none` | Hardware físico (bare metal). |
| `VM_GUEST_VM` | `generic` | Hipervisor desconocido. |
| `VM_GUEST_XEN` | `xen` | Xen. |
| `VM_GUEST_HV` | `hv` | Microsoft Hyper-V. |
| `VM_GUEST_VMWARE` | `vmware` | VMware ESXi / Workstation. |
| `VM_GUEST_KVM` | `kvm` | Linux KVM. |
| `VM_GUEST_BHYVE` | `bhyve` | FreeBSD bhyve. |
| `VM_GUEST_VBOX` | `vbox` | Oracle VirtualBox. |
| `VM_GUEST_PARALLELS` | `parallels` | Parallels. |
| `VM_GUEST_NVMM` | `nvmm` | NetBSD NVMM. |

### Rulesets predeterminados de devfs

| Número | Nombre | Propósito |
|--------|------|---------|
| 1 | `devfsrules_hide_all` | Empezar con todo oculto. |
| 2 | `devfsrules_unhide_basic` | Dispositivos esenciales (`null`, `zero`, `random`, etc.). |
| 3 | `devfsrules_unhide_login` | Dispositivos relacionados con el inicio de sesión (`pts`, `ttyv*`). |
| 4 | `devfsrules_jail` | Ruleset estándar para jails sin VNET. |
| 5 | `devfsrules_jail_vnet` | Ruleset estándar para jails con VNET. |

### Constantes de privilegios habituales

| Constante | Política habitual en jails |
|----------|--------------------|
| `PRIV_DRIVER` | Denegado (ioctls privados del driver). |
| `PRIV_IO` | Denegado (acceso directo a puertos de I/O). |
| `PRIV_KMEM_WRITE` | Denegado (escrituras en memoria del kernel). |
| `PRIV_KLD_LOAD` | Denegado (carga de módulos). |
| `PRIV_NET_SETLLADDR` | Denegado (cambios de dirección MAC). |
| `PRIV_NETINET_RAW` | Denegado salvo `allow.raw_sockets`. |
| `PRIV_NET_BPF` | Permitido vía `allow.raw_sockets` para herramientas que necesitan BPF. |

### Macros de VNET

| Macro | Propósito |
|-------|---------|
| `VNET_DEFINE(type, name)` | Declarar una variable por VNET. |
| `VNET(name)` | Acceder a la variable por VNET del VNET actual. |
| `V_name` | Abreviatura convencional para `VNET(name)`. |
| `CURVNET_SET(vnet)` | Establecer un contexto VNET en el thread actual. |
| `CURVNET_RESTORE()` | Desmantelar el contexto. |
| `VNET_SYSINIT(name, ...)` | Registrar una función de inicialización por VNET. |
| `VNET_SYSUNINIT(name, ...)` | Registrar una función de desinicialización por VNET. |

### Herramientas de bhyve y passthrough

| Herramienta | Propósito |
|------|---------|
| `bhyve(8)` | Ejecutar una máquina virtual. |
| `bhyvectl(8)` | Consultar y controlar VMs en ejecución. |
| `vm(8)` | Gestión de alto nivel (a través del port `vm-bhyve`). |
| `pciconf(8)` | Mostrar dispositivos PCI y sus bindings de driver. |
| `devctl(8)` | Control explícito del attach y detach de drivers. |
| `pptdevs` en `/boot/loader.conf` | Enlazar dispositivos al placeholder de passthrough. |

### Páginas de manual que merece la pena tener a mano

- `virtio(4)` - Visión general del framework VirtIO.
- `vtnet(4)`, `virtio_blk(4)` - Drivers VirtIO específicos.
- `bhyve(8)`, `bhyvectl(8)`, `vmm(4)` - Interfaces de usuario y de kernel del hipervisor.
- `pci_passthru(4)` - Mecanismo de passthrough PCI.
- `jail(8)`, `jail.conf(5)`, `jls(8)`, `jexec(8)` - Gestión de jails.
- `devfs(5)`, `devfs(8)`, `devfs.rules(5)` - devfs y rulesets.
- `if_epair(4)`, `vlan(4)`, `if_tap(4)` - Pseudointerfaces útiles para jails.
- `rctl(8)`, `racct(9)` - Control de recursos.
- `priv(9)` - Framework de privilegios.

### Las cinco cosas más importantes que debe hacer un autor de drivers

Si no recuerdas nada más de este capítulo, recuerda estos cinco hábitos:

1. Usa `bus_dma(9)` para cada buffer DMA. Nunca pases direcciones físicas directamente al hardware. Este es el hábito más importante para entornos con passthrough y protección mediante IOMMU.
2. Usa `priv_check(9)` para las operaciones privilegiadas. No pongas a mano comprobaciones de `cred->cr_uid == 0`. El framework extiende tu código a las jails sin coste adicional.
3. Mantén los nombres de los nodos de dispositivo predecibles. Los administradores que escriben rulesets de devfs necesitan saber qué deben desocultar. Documenta el nombre en la página de manual de tu driver.
4. Gestiona el detach de forma limpia. Libera cada recurso, cancela cada callout, drena cada taskqueue y nunca asumas que el softc se va a reutilizar. Tanto el passthrough como VNET ejercitan el detach de forma intensa.
5. Establece el contexto VNET alrededor del código de callout y taskqueue que acceda a estado por VNET. `CURVNET_SET` / `CURVNET_RESTORE` es el patrón habitual, y olvidarlo es el error más frecuente relacionado con VNET.

Estos cinco hábitos cubren en conjunto casi todo el trabajo que un autor de drivers necesita hacer para que su código "funcione bajo virtualización y contenedorización". Todo lo demás es refinamiento.

## Apéndice: patrones de código habituales

Un pequeño catálogo de patrones que aparecen repetidamente en drivers de FreeBSD que se ejecutan en entornos virtualizados o contenedorizados. Cada uno es un fragmento que puedes adaptar a tu propio código.

### Patrón: valor predeterminado adaptado al entorno

```c
static int
mydev_default_interrupt_moderation(void)
{

	switch (vm_guest) {
	case VM_GUEST_NO:
		return (1);	/* tight moderation on bare metal */
	default:
		return (4);	/* loose moderation under a hypervisor */
	}
}
```

Usa este patrón para inicializar un valor predeterminado que el usuario pueda sobreescribir mediante sysctl. No ramifiques según la marca concreta del hipervisor a menos que haya una razón real para ello.

### Patrón: ioctl con control de privilegios

```c
case MYDEV_IOC_DANGEROUS:
	error = priv_check(td, PRIV_DRIVER);
	if (error != 0)
		return (error);
	/* perform the dangerous operation */
	return (0);
```

La postura predeterminada para los privilegios específicos de un driver es exigir `PRIV_DRIVER`. Consulta `priv(9)` si encaja mejor un privilegio más específico.

### Patrón: callout con contexto VNET

```c
static void
mydev_callout(void *arg)
{
	struct mydev_softc *sc = arg;

	CURVNET_SET(sc->ifp->if_vnet);
	/* read or write V_ variables, or call network-stack functions */
	CURVNET_RESTORE();

	callout_reset(&sc->co, hz, mydev_callout, sc);
}
```

Cualquier callout, función de taskqueue o thread del kernel que pueda acceder a estado VNET debe establecer el contexto. Omitirlo es la fuente más habitual de errores relacionados con VNET.

### Patrón: esqueleto de attach VirtIO

```c
static int
mydev_attach(device_t dev)
{
	struct mydev_softc *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;
	virtio_set_feature_desc(dev, mydev_feature_descs);

	error = mydev_negotiate_features(sc);
	if (error != 0)
		goto fail;

	error = mydev_alloc_virtqueues(sc);
	if (error != 0)
		goto fail;

	error = virtio_setup_intr(dev, INTR_TYPE_MISC);
	if (error != 0)
		goto fail;

	/* post initial buffers, register with subsystem, etc. */
	return (0);

fail:
	mydev_detach(dev);
	return (error);
}
```

El ritmo de "negociar, asignar, configurar interrupciones, arrancar" es estándar para todo driver VirtIO. Cada attach VirtIO en `/usr/src/sys/dev/virtio/` es una variación de este esqueleto.

### Patrón: detach limpio

```c
static int
mydev_detach(device_t dev)
{
	struct mydev_softc *sc = device_get_softc(dev);

	/* Stop accepting new work. */
	sc->detaching = true;

	/* Drain async mechanisms. */
	if (sc->co_initialised)
		callout_drain(&sc->co);
	if (sc->tq != NULL)
		taskqueue_drain_all(sc->tq);

	/* Release hardware resources. */
	if (sc->irq_cookie != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_cookie);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);

	/* Destroy devfs nodes. */
	if (sc->cdev != NULL)
		destroy_dev(sc->cdev);

	return (0);
}
```

El detach debe ser simétrico con el attach: cada recurso que el attach asignó debe liberarse aquí, en orden inverso. Un detach limpio es lo que hace al driver seguro para el passthrough y para la descarga del módulo.

### Patrón: make_dev con valores predeterminados razonables

```c
sc->cdev = make_dev(&mydev_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, "mydev%d",
    device_get_unit(dev));
if (sc->cdev == NULL)
	return (ENXIO);
sc->cdev->si_drv1 = sc;
```

Usa el modo `0600` para nodos que solo deben ser accesibles por root, `0644` para lectura por cualquiera y escritura solo por root, y `0666` en los casos excepcionales en que cualquiera pueda leer y escribir. El campo `si_drv1` es el back-pointer convencional desde un `struct cdev` hasta el softc del driver.

## Apéndice: Glosario

Un breve glosario de los términos en los que se apoya este capítulo. Consúltalo cuando un término de una sección posterior haya quedado en el olvido.

- **Bare metal**: Un sistema que se ejecuta directamente sobre hardware físico sin ningún hipervisor de por medio.
- **bhyve**: El hipervisor nativo de tipo 2 de FreeBSD. Se ejecuta como un programa en espacio de usuario respaldado por el módulo `vmm(4)` del kernel.
- **Contenedor**: En FreeBSD, una jail o un framework en espacio de usuario construido sobre jails.
- **Credencial (`struct ucred`)**: El contexto de seguridad por proceso que lleva el uid, el gid, el puntero a la jail y el estado relacionado con privilegios.
- **devfs**: El sistema de archivos especial en `/dev` donde se exponen los dispositivos.
- **Conjunto de reglas devfs**: Un conjunto de reglas con nombre que controla qué nodos devfs son visibles en un montaje devfs determinado.
- **Dispositivo emulado**: Un dispositivo proporcionado por el hipervisor que imita la interfaz de hardware real.
- **Invitado**: El sistema operativo que se ejecuta dentro de una máquina virtual.
- **Anfitrión**: La máquina física (o el sistema que engloba los contenedores) que aloja invitados o jails.
- **Hipervisor**: Software que crea y gestiona máquinas virtuales.
- **IOMMU**: Una unidad situada entre un dispositivo y la memoria del anfitrión que reasigna direcciones DMA. Permite el passthrough seguro.
- **Jail**: El mecanismo de contenerización ligero de FreeBSD.
- **Dispositivo paravirtual**: Un dispositivo con una interfaz diseñada para ser fácil de emular por un hipervisor y de controlar por un invitado. VirtIO es el ejemplo canónico.
- **Passthrough**: Dar al invitado acceso directo a un dispositivo físico.
- **Prison**: El nombre interno del kernel para una jail. `struct prison` es la estructura de datos.
- **rctl / racct**: Los frameworks de control de recursos y contabilidad de recursos que imponen límites por jail o por proceso.
- **Conjunto de reglas**: Véase *Conjunto de reglas devfs*.
- **Transporte (VirtIO)**: El mecanismo a nivel de bus que transporta los mensajes de VirtIO. Ejemplos: PCI, MMIO.
- **Virtqueue**: La estructura de datos en anillo compartido en el corazón de VirtIO.
- **VirtIO**: El estándar de paravirtualización utilizado por la mayoría de los hipervisores modernos.
- **VM**: Máquina virtual.
- **VNET**: El framework de pila de red virtual de FreeBSD, que proporciona pilas independientes por jail.
- **vmm**: El módulo del kernel del hipervisor central de FreeBSD, utilizado por `bhyve(8)`.

## Apéndice: Observación de VirtIO con DTrace

DTrace es una de las herramientas de diagnóstico más potentes de FreeBSD, y resulta especialmente útil para comprender el comportamiento de un driver VirtIO en tiempo de ejecución. Este apéndice reúne varias recetas concretas de DTrace para la observabilidad de VirtIO. Ninguna de ellas requiere modificar el driver; funcionan contra el kernel sin modificar porque el proveedor `fbt` (function-boundary-tracing) instrumenta cada función del kernel.

### Una primera sonda: contar enqueues en la virtqueue

La sonda útil más sencilla cuenta la frecuencia con que se llama a `virtqueue_enqueue` por segundo.

```sh
sudo dtrace -n 'fbt::virtqueue_enqueue:entry /pid == 0/ { @[probefunc] = count(); } tick-1sec { printa(@); trunc(@); }'
```

Al ejecutar esto en una VM activa se obtienen números del estilo:

```text
virtqueue_enqueue 12340
virtqueue_enqueue 15220
virtqueue_enqueue 11890
```

Cada número representa los enqueues por segundo en todas las virtqueues. Un número en miles es normal para una VM activa; un número en millones sugiere un driver con un comportamiento patológico. El valor de referencia por VM depende de la carga de trabajo, pero familiarizarse con el baseline de tu entorno permite detectar anomalías más adelante.

### Separación de colas por virtqueue

Para ver qué virtqueue se está usando, amplía la sonda para que indexe por nombre de virtqueue.

```sh
sudo dtrace -n '
fbt::virtqueue_enqueue:entry
{
	this->vq = (struct virtqueue *)arg0;
	@[stringof(this->vq->vq_name)] = count();
}
tick-1sec
{
	printa(@);
	trunc(@);
}
'
```

La salida ahora tiene este aspecto:

```text
vtnet0-rx           482
vtnet0-tx           430
virtio_blk0         8220
```

Esto revela de inmediato qué dispositivo está haciendo trabajo. Una carga de trabajo intensiva en disco muestra `virtio_blk0` dominando; una carga intensiva en red muestra `vtnet0-tx` y `vtnet0-rx`. Este tipo de desglose de primer nivel suele ser suficiente para localizar un problema de rendimiento.

Ten en cuenta que el ejemplo desreferencia una estructura interna (`struct virtqueue`). La distribución de la estructura es un detalle de implementación que puede cambiar entre versiones de FreeBSD. Comprueba `/usr/src/sys/dev/virtio/virtqueue.c` si la distribución ha cambiado y la sonda falla.

### Medición del tiempo empleado en el enqueue

El tiempo en función es una receta estándar de DTrace.

```sh
sudo dtrace -n '
fbt::virtqueue_enqueue:entry
{
	self->ts = vtimestamp;
}
fbt::virtqueue_enqueue:return
/ self->ts /
{
	@["virtqueue_enqueue"] = quantize(vtimestamp - self->ts);
	self->ts = 0;
}
'
```

La salida es un histograma de cuánto tarda cada llamada a `virtqueue_enqueue`, en nanosegundos. En un VirtIO saludable, la mayoría de las llamadas se completan en el rango de pocos microsegundos. Una cola significativa sugiere contención de lock (el mutex de la función se mantiene demasiado tiempo), presión de memoria o un cálculo costoso de scatter-gather.

### Observación de las VM exits

Las VM exits son el coste fundamental de la virtualización. Las sondas `vmm` de DTrace (si están disponibles) permiten contarlas.

```sh
sudo dtrace -n 'fbt:vmm::*:entry { @[probefunc] = count(); } tick-1sec { printa(@); trunc(@); }'
```

En un anfitrión activo, esto muestra decenas de funciones `vmm`. Las que hay que vigilar son `vm_exit_*`, que gestionan distintos tipos de salida (I/O, interrupción, hypercall). Ver `vm_exit_inout` en los primeros resultados sugiere que el invitado está realizando mucho I/O a través de dispositivos emulados y se beneficiaría de VirtIO.

### Trazado de un driver específico

Para centrarse en las funciones de un driver concreto, limita la sonda al nombre de su módulo.

```sh
sudo dtrace -n 'fbt:virtio_blk::*:entry { @[probefunc] = count(); } tick-1sec { printa(@); trunc(@); }'
```

Esto cuenta todas las entradas a funciones de `virtio_blk`. En una VM inactiva, la salida está vacía; en una VM activa, se ven todas las funciones que llama el driver, con sus conteos. Esto es útil para hacerse una idea de la estructura interna de un driver.

### Trazado de un driver propio

Para el driver `vtedu` del caso de estudio, la misma técnica funciona siempre que el módulo esté cargado y se llame `virtio_edu`.

```sh
sudo dtrace -n 'fbt:virtio_edu::*:entry { @[probefunc] = count(); } tick-1sec { printa(@); trunc(@); }'
```

Si no hay ningún backend conectado, los conteos serán cero. Si hay un backend conectado y el driver está ejerciendo la virtqueue, verás `vtedu_write`, `vtedu_submit_locked`, `vtedu_vq_intr` y `vtedu_read` en los conteos, en proporción aproximada al uso.

### Observación de la frontera invitado-anfitrión

Un uso más ambicioso de DTrace consiste en correlacionar eventos del lado del invitado con eventos del lado del anfitrión. Esto es posible cuando ambos lados son FreeBSD y DTrace está disponible en ambos. Ejecuta DTrace en el anfitrión para contar las salidas de `vmm` y en el invitado para contar las llamadas al driver; los números deberían correlacionarse uno a uno en un sistema sin carga, divergiendo a medida que el anfitrión agrupa salidas o que entran en juego las interrupciones publicadas.

Esta es una técnica avanzada y resulta de interés principalmente para el análisis de rendimiento. Para la depuración cotidiana, DTrace en un solo lado suele ser suficiente.

### Guardar y reutilizar sondas

La forma de línea de comandos de `dtrace(1)` está bien para investigaciones rápidas. Para uso repetido, guarda las sondas en un archivo e invócalas con `dtrace -s`.

```sh
% cat > virtio_probes.d <<'EOF'
#pragma D option quiet

fbt::virtqueue_enqueue:entry
{
	this->vq = (struct virtqueue *)arg0;
	@[stringof(this->vq->vq_name)] = count();
}

tick-1sec
{
	printa(@);
	trunc(@);
}
EOF

% sudo dtrace -s virtio_probes.d
```

Un conjunto cuidado de sondas en archivos `.d` es una buena inversión para quien dedica tiempo significativo a depurar VirtIO.

### Cuándo DTrace no puede ayudar

DTrace puede observar el kernel y, con el proveedor `pid`, la mayoría de los programas en espacio de usuario. No puede observar directamente los kernels de los invitados desde el anfitrión, porque el invitado es un proceso cuya estructura interna `dtrace` no conoce. Puedes trazar el proceso `bhyve(8)` en sí (como programa en espacio de usuario) usando el proveedor `pid`, lo que muestra lo que hace `bhyve` pero no lo que hace su invitado.

Para el trazado en el lado del invitado, DTrace se ejecuta dentro del invitado. Si el invitado es FreeBSD 14.3, el conjunto completo de herramientas DTrace está disponible. Si el invitado es Linux, usa `bpftrace` o `perf` en su lugar; son herramientas distintas con capacidades similares.

### Una palabra final

DTrace es una de las ventajas competitivas de FreeBSD. Todo autor de drivers debería manejarlo con soltura; la inversión se amortiza repetidamente a lo largo de años de depuración. Este apéndice te ofrece un punto de partida; el capítulo sobre DTrace del FreeBSD Handbook y el libro original *DTrace: Dynamic Tracing in Oracle Solaris* (disponible gratuitamente en línea) son los siguientes pasos si quieres profundizar.

## Apéndice: Guía completa de configuración de bhyve

Los lectores que quieran realizar los laboratorios necesitan una configuración de `bhyve(8)` funcionando. Este apéndice recorre una configuración completa, desde un anfitrión FreeBSD 14.3 recién instalado hasta un invitado con dispositivos VirtIO, con el suficiente detalle como para que un principiante pueda reproducirla. Si ya has construido entornos `bhyve`, échale un vistazo por encima; el objetivo es proporcionar una referencia para quienes no lo hayan hecho.

### El lado del anfitrión

Empieza con un anfitrión FreeBSD 14.3. Confirma que las extensiones de virtualización están habilitadas.

```sh
% sysctl hw.vmm
hw.vmm.topology.cores_per_package: 0
hw.vmm.topology.threads_per_core: 0
hw.vmm.topology.sockets: 0
hw.vmm.topology.cpus: 0
...
```

Si `hw.vmm` está completamente ausente, el módulo `vmm(4)` no está cargado. Cárgalo con `kldload vmm`. Añade `vmm_load="YES"` a `/boot/loader.conf` para cargarlo en cada arranque.

Si `vmm` está cargado pero las características VT-x/AMD-V están ausentes, actívalas en el firmware del anfitrión. El ajuste normalmente se llama "VT-x", "VMX", "AMD-V" o "SVM" en el menú de la BIOS/UEFI. Tras habilitarlo, reinicia.

### Instalar vm-bhyve

`vm-bhyve` es un envoltorio que facilita el uso de `bhyve(8)`. Instálalo desde ports o paquetes.

```sh
% sudo pkg install vm-bhyve
```

Crea el directorio de VM. Usar ZFS es conveniente porque permite instantáneas.

```sh
% sudo zfs create -o mountpoint=/vm zroot/vm
```

O con UFS simple:

```sh
% sudo mkdir /vm
```

Habilita `vm-bhyve` y establece su directorio en `/etc/rc.conf`.

```text
vm_enable="YES"
vm_dir="zfs:zroot/vm"
```

Para UFS, usa `vm_dir="/vm"` en su lugar. Inicializa el directorio.

```sh
% sudo vm init
% sudo cp /usr/local/share/examples/vm-bhyve/config_samples/default.conf /vm/.templates/
```

Edita `/vm/.templates/default.conf` si quieres otros valores predeterminados (tamaño de memoria, número de CPUs).

### Crear e instalar un invitado

Descarga una imagen de instalación de FreeBSD 14.3. El archivo `FreeBSD-14.3-RELEASE-amd64-disc1.iso` es el instalador estándar; para una configuración más rápida, usa `FreeBSD-14.3-RELEASE-amd64.qcow2` si prefieres una imagen preinstalada.

```sh
% sudo vm iso https://download.freebsd.org/releases/amd64/amd64/ISO-IMAGES/14.3/FreeBSD-14.3-RELEASE-amd64-disc1.iso
```

Crea el invitado.

```sh
% sudo vm create -t default -s 20G guest0
```

Inicia el instalador.

```sh
% sudo vm install guest0 FreeBSD-14.3-RELEASE-amd64-disc1.iso
```

Conéctate a la consola.

```sh
% sudo vm console guest0
```

El instalador de FreeBSD se ejecuta; sígüelo hasta completarlo. Al final, reinicia el invitado.

### Configurar la red

`vm-bhyve` admite dos estilos de red: en puente y NAT. Por simplicidad, el modo en puente es suficiente.

Crea un puente.

```sh
% sudo vm switch create public
% sudo vm switch add public em0
```

Sustituye `em0` por la interfaz física de tu anfitrión. Asigna los invitados al switch en su configuración.

```sh
% sudo vm configure guest0
```

En el editor que se abre, asegúrate de que `network0_switch="public"` esté establecido.

### Iniciar el invitado

```sh
% sudo vm start guest0
% sudo vm console guest0
```

Inicia sesión, ejecuta `pciconf -lvBb` y confirma que los dispositivos VirtIO están presentes.

```sh
# pciconf -lvBb
hostb0@pci0:0:0:0:      class=0x060000 rev=0x00 ...
virtio_pci0@pci0:0:2:0: class=0x010000 rev=0x00 vendor=0x1af4 device=0x1001 ...
    vendor     = 'Red Hat, Inc.'
    device     = 'Virtio 1.0 block device'
virtio_pci1@pci0:0:3:0: class=0x020000 rev=0x00 vendor=0x1af4 device=0x1041 ...
    vendor     = 'Red Hat, Inc.'
    device     = 'Virtio 1.0 network device'
...
```

### Resolución de problemas en el lado del anfitrión

Si el invitado no arranca, ejecuta `vm start guest0` con el indicador verbose (`vm -f start guest0` lo mantiene en primer plano), observa el mensaje de error y consulta el manual de `bhyve(8)`. Los problemas más frecuentes son recursos ausentes (ruta a la imagen de disco incorrecta, switch no configurado) y permisos (usuario no perteneciente al grupo `vm`, o directorios sin permisos de lectura).

Si la red no funciona dentro del invitado, comprueba el puente en el anfitrión (`ifconfig bridge0`), comprueba el dispositivo tap (`ifconfig tapN`) y verifica que el ajuste `network0` de la VM coincida con el nombre del switch. `vm-bhyve` genera un dispositivo tap por VM y lo conecta al switch especificado.

### Uso de esta configuración para los laboratorios

Con un anfitrión e invitado funcionando, el Laboratorio 1 está esencialmente completado (tienes un invitado que usa VirtIO). Los Laboratorios 2 y 3 pueden realizarse dentro del invitado. El Laboratorio 4 necesita `if_epair` en el anfitrión; el Laboratorio 5 necesita un dispositivo PCI de repuesto y firmware con IOMMU habilitado. El Laboratorio 6 (construir vtedu) se realiza dentro del invitado. El Laboratorio 7 (medir la sobrecarga) usa el invitado como sujeto de prueba.

En resumen: si tu configuración de `vm-bhyve` es sólida, el resto del trabajo práctico del capítulo es accesible.

## Apéndice: Referencia de bits de características de VirtIO

Este apéndice reúne los bits de características de VirtIO que más probablemente interesan a un autor de drivers, con una breve descripción de cada uno. La fuente autorizada es la especificación de VirtIO; esto es un resumen para consulta rápida.

### Características independientes del dispositivo

Estas se aplican a todos los tipos de dispositivos VirtIO.

- `VIRTIO_F_NOTIFY_ON_EMPTY` (bit 24): El dispositivo debería notificar al driver cuando la virtqueue se vacíe, además de las notificaciones normales de finalización. Útil para drivers que quieren saber cuándo se han procesado todas las solicitudes pendientes.

- `VIRTIO_F_ANY_LAYOUT` (bit 27, en desuso en v1): Las cabeceras y los datos pueden estar en cualquier disposición scatter-gather. Se negocia siempre en drivers de v1; no es relevante para código moderno.

- `VIRTIO_F_RING_INDIRECT_DESC` (bit 28): Se admiten descriptores indirectos. Una entrada de la tabla de descriptores puede apuntar a otra tabla, lo que permite listas scatter-gather más largas sin ampliar el anillo principal. Se recomienda encarecidamente para drivers que manejan peticiones grandes.

- `VIRTIO_F_RING_EVENT_IDX` (bit 29): Supresión de interrupciones por índice de evento. Permite que el driver indique al dispositivo "no me interrumpas antes de que el descriptor N esté disponible", lo que reduce la tasa de interrupciones. Se recomienda encarecidamente para drivers de alta velocidad.

- `VIRTIO_F_VERSION_1` (bit 32): VirtIO moderno (versión 1.0 o posterior). Sin esto, el driver opera en modo legado, con una disposición del espacio de configuración y unas convenciones distintas. Los drivers nuevos deberían exigir esta característica.

- `VIRTIO_F_ACCESS_PLATFORM` (bit 33): El dispositivo utiliza una traducción de direcciones DMA específica de la plataforma (por ejemplo, una IOMMU). Obligatorio para despliegues con capacidad de passthrough.

- `VIRTIO_F_RING_PACKED` (bit 34): Disposición de virtqueue empaquetada. Una disposición más reciente y más eficiente con la caché que la disposición clásica dividida. No está admitida por todos los backends; negóciala, pero no la exijas.

- `VIRTIO_F_IN_ORDER` (bit 35): Los descriptores se usan en el mismo orden en que se pusieron a disposición. Permite optimizaciones en el driver (no es necesario rastrear los índices de los descriptores); no está admitida por todos los backends.

### Características del dispositivo de bloques (`virtio_blk`)

- `VIRTIO_BLK_F_SIZE_MAX` (bit 1): El dispositivo tiene un tamaño máximo por petición.
- `VIRTIO_BLK_F_SEG_MAX` (bit 2): El dispositivo tiene un número máximo de segmentos por petición.
- `VIRTIO_BLK_F_GEOMETRY` (bit 4): El dispositivo informa de su geometría de cilindros/cabezas/sectores. En gran medida es un legado histórico.
- `VIRTIO_BLK_F_RO` (bit 5): El dispositivo es de solo lectura.
- `VIRTIO_BLK_F_BLK_SIZE` (bit 6): El dispositivo informa de su tamaño de bloque.
- `VIRTIO_BLK_F_FLUSH` (bit 9): El dispositivo admite comandos de vaciado (fsync).
- `VIRTIO_BLK_F_TOPOLOGY` (bit 10): El dispositivo informa de datos de topología (alineación, etc.).
- `VIRTIO_BLK_F_CONFIG_WCE` (bit 11): El driver puede consultar y establecer el indicador de escritura en caché.
- `VIRTIO_BLK_F_DISCARD` (bit 13): El dispositivo admite comandos de descarte (trim).
- `VIRTIO_BLK_F_WRITE_ZEROES` (bit 14): El dispositivo admite comandos de escritura de ceros.

### Características del dispositivo de red (`virtio_net`)

- `VIRTIO_NET_F_CSUM` (bit 0): El dispositivo puede descargar el cálculo del checksum.
- `VIRTIO_NET_F_GUEST_CSUM` (bit 1): El driver puede descargar el checksum de los paquetes entrantes.
- `VIRTIO_NET_F_MAC` (bit 5): El dispositivo proporciona una dirección MAC.
- `VIRTIO_NET_F_GSO` (bit 6): GSO (generic segmentation offload) admitido.
- `VIRTIO_NET_F_GUEST_TSO4` (bit 7): TSO en recepción sobre IPv4.
- `VIRTIO_NET_F_GUEST_TSO6` (bit 8): TSO en recepción sobre IPv6.
- `VIRTIO_NET_F_GUEST_ECN` (bit 9): ECN (explicit congestion notification) admitido en recepción.
- `VIRTIO_NET_F_GUEST_UFO` (bit 10): UFO en recepción (UDP fragmentation offload).
- `VIRTIO_NET_F_HOST_TSO4` (bit 11): TSO en transmisión sobre IPv4.
- `VIRTIO_NET_F_HOST_TSO6` (bit 12): TSO en transmisión sobre IPv6.
- `VIRTIO_NET_F_HOST_ECN` (bit 13): ECN en transmisión.
- `VIRTIO_NET_F_HOST_UFO` (bit 14): UFO en transmisión.
- `VIRTIO_NET_F_MRG_RXBUF` (bit 15): Buffers de recepción fusionados.
- `VIRTIO_NET_F_STATUS` (bit 16): Se admite el estado de configuración.
- `VIRTIO_NET_F_CTRL_VQ` (bit 17): Virtqueue de control.
- `VIRTIO_NET_F_CTRL_RX` (bit 18): Canal de control para el filtrado del modo de recepción.
- `VIRTIO_NET_F_CTRL_VLAN` (bit 19): Canal de control para el filtrado de VLAN.
- `VIRTIO_NET_F_MQ` (bit 22): Soporte de múltiples colas.
- `VIRTIO_NET_F_CTRL_MAC_ADDR` (bit 23): Canal de control para establecer la dirección MAC.

### Cómo interpretar una palabra de características

Las características son una palabra de 64 bits, con los bits descritos anteriormente. Para comprobar si una característica ha sido negociada:

```c
if ((sc->features & VIRTIO_F_RING_EVENT_IDX) != 0) {
	/* event indexes are available */
}
```

Para anunciar una característica durante la negociación, combínala con `|` en la máscara de características antes de llamar a `virtio_negotiate_features`. El valor de `sc->features` tras la negociación es la intersección de lo que el driver solicitó y lo que el backend ofrece.

### Errores comunes

- Imponer características como requisitos fijos. Un driver que exige `VIRTIO_NET_F_MRG_RXBUF` y falla si no está presente no funcionará con backends más sencillos. Es mejor negociar de forma optimista y adaptarse a lo que se obtenga.
- Olvidar comprobar las características tras la negociación. Un driver que actúa como si una característica estuviera siempre presente, sin verificar `sc->features`, programará incorrectamente el dispositivo cuando esa característica esté ausente.
- Ignorar los requisitos de la versión 1. El código moderno debería exigir `VIRTIO_F_VERSION_1`; el modo legacy tiene demasiadas particularidades como para merecer soporte en drivers nuevos.

### Un hábito útil

Registra la palabra de características negociada en el momento del attach, con el nombre de cada bit relevante. El `device_printf` que se muestra a continuación lo hace de forma concisa.

```c
device_printf(dev, "features: ver1=%d evt_idx=%d indirect=%d mac=%d\n",
    (sc->features & VIRTIO_F_VERSION_1) != 0,
    (sc->features & VIRTIO_F_RING_EVENT_IDX) != 0,
    (sc->features & VIRTIO_F_RING_INDIRECT_DESC) != 0,
    (sc->features & VIRTIO_NET_F_MAC) != 0);
```

Esta sola línea en `dmesg` durante el attach te indica, para cualquier informe de error, con qué conjunto de características está operando el driver. Es el equivalente en VirtIO de registrar la revisión de hardware; imprescindible para el soporte.

## Apéndice: Lecturas recomendadas

Para los lectores que quieran profundizar, aquí tienes una lista breve y seleccionada.

### Árbol de código fuente de FreeBSD

- `/usr/src/sys/dev/virtio/random/virtio_random.c` — El driver VirtIO completo más pequeño. Léelo primero.
- `/usr/src/sys/dev/virtio/network/if_vtnet.c` — Un driver VirtIO más extenso.
- `/usr/src/sys/dev/virtio/block/virtio_blk.c` — Un driver VirtIO de petición-respuesta.
- `/usr/src/sys/dev/virtio/virtqueue.c` — La maquinaria del anillo.
- `/usr/src/sys/amd64/vmm/` — El módulo del kernel de bhyve.
- `/usr/src/usr.sbin/bhyve/` — El emulador en espacio de usuario de bhyve.
- `/usr/src/sys/kern/kern_jail.c` — Implementación de jail.
- `/usr/src/sys/net/vnet.h`, `/usr/src/sys/net/vnet.c` — El framework VNET.
- `/usr/src/sys/net/if_tuntap.c` — Un pseudo-driver de clonación compatible con VNET.

### Páginas de manual

- `virtio(4)`, `vtnet(4)`, `virtio_blk(4)`
- `bhyve(8)`, `bhyvectl(8)`, `vmm(4)`, `vmm_dev(4)`
- `pci_passthru(4)`
- `jail(8)`, `jail.conf(5)`, `jexec(8)`, `jls(8)`
- `devfs(5)`, `devfs.rules(5)`, `devfs(8)`
- `rctl(8)`, `racct(9)`
- `priv(9)`, `ucred(9)`
- `if_epair(4)`, `vlan(4)`, `tun(4)`, `tap(4)`

### Estándares externos

- La especificación VirtIO 1.2 (OASIS) es la referencia autoritativa del protocolo. Disponible en el sitio del Comité Técnico VirtIO de OASIS.
- Las especificaciones de PCI-SIG para PCI Express, MSI-X y ACS son relevantes para la asignación directa de dispositivos (passthrough).

### El FreeBSD Handbook

- El capítulo sobre jails, que complementa este capítulo con una perspectiva administrativa.
- El capítulo sobre virtualización, que cubre la gestión de `bhyve(8)` con mayor profundidad que este capítulo centrado en drivers.

Estos recursos forman en conjunto un programa de lecturas que llevará a un lector motivado desde la introducción de este capítulo hasta una fluidez práctica en virtualización y contenedores en FreeBSD. No es necesario leerlos todos; elige los que encajen con tu proyecto actual y ve construyendo a partir de ahí.

## Apéndice: Antipatrones en drivers virtualizados

Una buena manera de aprender un oficio es estudiar sus errores más frecuentes. Este apéndice reúne en un único lugar los antipatrones que hemos visto a lo largo del capítulo, junto con la solución para cada uno. Cuando revises un driver (el tuyo o el de otra persona), busca estos patrones; cada uno es una señal clara de que algo va mal.

### Espera activa sobre un registro de estado

```c
/* Anti-pattern */
while ((bus_read_4(sc->res, STATUS) & READY) == 0)
	;
```

Bajo virtualización, cada lectura del bus es una salida de VM (VM exit). Un bucle cerrado consume una cantidad enorme de CPU y puede no terminar nunca si el guest es desplanificado. Usa `DELAY(9)` dentro del bucle junto con un número máximo de iteraciones, o bien un diseño basado en interrupciones que no haga polling en absoluto.

### Cachear direcciones físicas

```c
/* Anti-pattern */
uint64_t phys_addr = vtophys(buffer);
bus_write_8(sc->res, DMA_ADDR, phys_addr);
/* ...later... */
bus_write_8(sc->res, DMA_ADDR, phys_addr);  /* still valid? */
```

Una dirección física es una vista temporal. Bajo compactación de memoria, migración en vivo o incorporación dinámica de memoria, la dirección puede dejar de referirse a la misma memoria física. Usa `bus_dma(9)` y conserva un mapa DMA; el mapa sigue correctamente la dirección del bus a través de las operaciones de memoria del kernel.

### Ignorar `si_drv1`

```c
/* Anti-pattern */
static int
mydrv_read(struct cdev *dev, struct uio *uio, int flags)
{
	struct mydrv_softc *sc = devclass_get_softc(mydrv_devclass, 0);
	/* what if there are multiple units? */
}
```

El campo `dev->si_drv1` existe para conectar el cdev con su softc. Establecerlo en attach y usarlo en read/write/ioctl es el patrón idiomático. Usar `devclass_get_softc` con un número de unidad fijo en el código es una fuente de errores en cuanto se adjunta más de una instancia.

### Asumir INTx

```c
/* Anti-pattern */
sc->irq_rid = 0;
sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid,
    RF_SHAREABLE | RF_ACTIVE);
```

INTx es más lento bajo virtualización y no escala bien con dispositivos de múltiples colas. Usa `pci_alloc_msix` primero, luego `pci_alloc_msi` como alternativa, y solo recurre a INTx para hardware que no admita interrupciones señalizadas por mensaje.

### Imponer bits de características de forma fija

```c
/* Anti-pattern */
if ((sc->features & VIRTIO_F_RING_INDIRECT_DESC) == 0)
	panic("device does not support indirect descriptors");
```

Un driver que produce un panic cuando falta una característica es imposible de usar con un backend que no la anuncia. Negocia de forma optimista, comprueba el resultado tras la negociación y degrada con elegancia a una ruta menos eficiente si la característica está ausente.

### Dormir con un spin-lock tomado

```c
/* Anti-pattern */
mtx_lock(&sc->lock);
tsleep(sc, PWAIT, "wait", hz);
mtx_unlock(&sc->lock);
```

Llamar a `tsleep` con un spin-lock de `mtx(9)` tomado provoca un panic en FreeBSD. Usa `mtx_sleep` (que libera el mutex durante el sleep) o usa un lock bloqueante (`sx(9)`) cuando necesites dormir.

### Devolver ENOTTY para operaciones no admitidas

```c
/* Anti-pattern */
case MYDRV_PRIV_IOCTL:
	if (cred->cr_prison != &prison0)
		return (ENOTTY);  /* hide from jails */
	...
```

`ENOTTY` significa "este ioctl no existe". Oculta la operación a los llamadores dentro de una jail, pero también rompe las herramientas que inspeccionan los ioctls disponibles. Prefiere `EPERM` para indicar "este ioctl existe pero no tienes permiso para usarlo", dejando la introspección en el estado correcto.

### Olvidar la ruta de detach

```c
/* Anti-pattern */
static int
mydrv_detach(device_t dev)
{
	struct mydrv_softc *sc = device_get_softc(dev);
	/* release a few things and call it done */
	bus_release_resource(dev, SYS_RES_MEMORY, sc->res_rid, sc->res);
	return (0);
}
```

Callouts sin drenar, taskqueues sin drenar, cdevs sin destruir, mutexes sin destruir, mapas DMA sin descargar, interrupciones sin desconectar. Cada uno se convierte en una fuga o un crash al ejecutar kldunload. La ruta de attach suele estar limpia; la de detach es donde se esconden los errores.

### Asumir que `device_printf` funciona sin un dispositivo

```c
/* Anti-pattern */
static int
mydrv_modevent(module_t mod, int event, void *arg)
{
	device_printf(NULL, "load event");  /* crashes */
}
```

Llamar a `device_printf` con un `device_t` NULL desreferencia un puntero nulo. En los manejadores de eventos de módulo, antes de que se haya adjuntado un dispositivo, usa `printf` (con un prefijo como "mydrv:" para identificar el origen). `device_printf` es para eventos por dispositivo, tras el attach.

### No limpiar el estado de VNET

```c
/* Anti-pattern */
static int
mydrv_mod_event(module_t mod, int event, void *arg)
{
	if (event == MOD_UNLOAD)
		free(mydrv_state, M_DEVBUF);  /* in which VNET? */
}
```

El estado por VNET se asigna en un contexto VNET específico. Liberarlo sin establecer ese contexto accede a los datos del VNET equivocado. Usa `VNET_FOREACH` y `CURVNET_SET` para recorrer cada VNET y limpiar el estado de cada uno.

### Usar `getnanouptime` para medir tiempos

```c
/* Anti-pattern */
struct timespec ts;
getnanouptime(&ts);
/* ...do work... */
struct timespec ts2;
getnanouptime(&ts2);
/* difference is arbitrary inside a VM */
```

`getnanouptime` devuelve una lectura de baja resolución que el kernel cachea. Para mediciones de tiempo de corta duración y alta precisión, usa `binuptime(9)` o `sbinuptime(9)`, que leen la fuente de tiempo de alta resolución. Bajo virtualización, las lecturas precisas son tan correctas como lo permita la fuente de tiempo; las lecturas cacheadas pueden estar desactualizadas hasta en un tick.

### Interactuar con el hardware en el método probe

```c
/* Anti-pattern */
static int
mydrv_probe(device_t dev)
{
	/* ...read status register... */
	return (BUS_PROBE_DEFAULT);
}
```

El método probe debe basarse exclusivamente en la identidad: inspecciona la información PNP, devuelve una prioridad y no hace nada que requiera que el hardware esté presente y funcional. La interacción con el hardware corresponde al attach. Bajo virtualización, un probe que accede al hardware antes de la negociación de características puede identificar mal el dispositivo.

### Almacenar punteros del kernel a través de `ioctl`

```c
/* Anti-pattern */
case MYDRV_GET_PTR:
	*(void **)data = sc->internal_state;
	return (0);
```

Pasar punteros del kernel al espacio de usuario es un error de seguridad. Bajo virtualización puede incluso filtrar información relevante para el hipervisor. Copia siempre los datos que solicita el llamador, nunca el puntero.

### Resumen

Estos antipatrones cubren la mayoría de las formas en que los drivers de FreeBSD fallan bajo virtualización. No son exclusivos de las máquinas virtuales, pero se *amplifican* bajo virtualización: el mismo error que corrompe memoria una vez al día en hardware real puede corromperla cientos de veces por segundo en una VM con una temporización diferente. Corregir estos patrones no es "arreglar el caso de la VM"; es arreglar el driver para que cumpla las expectativas base del kernel.

## Apéndice: Lista de comprobación para un driver listo para virtualización

Una lista de comprobación concreta que puedes aplicar a tu propio driver antes de declararlo listo para virtualización. Revisa cada elemento; cualquier respuesta negativa es una tarea pendiente.

### Vinculación y probe del dispositivo

- ¿Usa el driver `VIRTIO_SIMPLE_PNPINFO` o `VIRTIO_DRIVER_MODULE` en lugar de crear las entradas PNP a mano?
- ¿Evita el método probe el acceso al hardware y se limita a usar la identidad PNP?
- ¿Llama el método attach a `virtio_negotiate_features` y registra el resultado?
- ¿Falla el attach de forma limpia (liberando todos los recursos que haya asignado) si algún paso posterior a la asignación de virtqueues falla?

### Recursos

- ¿Usa el driver `bus_alloc_resource_any` con un RID dinámico en lugar de codificar IDs de recurso de forma fija?
- ¿Usa el driver `pci_alloc_msix` (o `pci_alloc_msi`) con preferencia sobre INTx?
- ¿Todas las llamadas a `bus_alloc_resource` tienen su correspondiente `bus_release_resource` en detach?

### DMA

- ¿Proviene toda dirección programada en el hardware de `bus_dma_load` (o similar) en lugar de `vtophys`?
- ¿Mantiene el driver etiquetas y mapas DMA con los tiempos de vida apropiados, sin recrearlos por operación cuando es evitable?
- ¿Gestiona el driver correctamente `bus_dma_load_mbuf_sg` para scatter-gather?
- ¿Se crea el `bus_dma_tag` del driver con las restricciones correctas de alineación, límite y tamaño máximo de segmento?

### Interrupciones

- ¿El manejador de interrupciones es MP-safe (declarado con `INTR_MPSAFE`)?
- ¿El manejador gestiona correctamente el caso de no haber trabajo pendiente (ante una activación espuria)?
- ¿El manejador deshabilita y vuelve a habilitar correctamente las interrupciones de virtqueue usando el patrón `virtqueue_disable_intr` / `virtqueue_enable_intr`?
- ¿La ruta de interrupción está libre de operaciones bloqueantes (sin `malloc(M_WAITOK)`, sin `mtx_sleep`)?

### Interfaz de dispositivo de caracteres

- ¿El método attach asigna `cdev->si_drv1 = sc`?
- ¿El método detach llama a `destroy_dev` antes de liberar el softc?
- ¿Las operaciones de lectura y escritura gestionan correctamente las operaciones de I/O cortas (inferiores al tamaño completo del buffer)?
- ¿El driver comprueba el resultado de `uiomove` en busca de errores?
- ¿El ioctl utiliza `priv_check` para las operaciones que requieren privilegios elevados?

### Locking

- ¿Cada acceso al softc está protegido por el mutex del softc?
- ¿La ruta de detach sigue el orden drain-then-free, y no free-then-drain?
- ¿Las esperas bloqueantes se realizan con `mtx_sleep` o `sx(9)` en lugar de `tsleep` con un mutex adquirido?
- ¿Existe un orden claro de adquisición de locks (para evitar deadlock)?

### Temporización

- ¿El driver utiliza `DELAY(9)`, `pause(9)` o `callout(9)` en lugar de bucles de espera activa?
- ¿El driver evita leer el TSC directamente?
- ¿Cada espera tiene un timeout acotado, con un error apropiado si se supera?

### Privilegios

- ¿El driver llama a `priv_check` para todas las operaciones que no deben estar disponibles para usuarios sin privilegios?
- ¿El driver utiliza el privilegio correcto (`PRIV_DRIVER`, `PRIV_IO`, no `PRIV_ROOT`)?
- ¿El driver tiene en cuenta a los llamadores dentro de jaulas, usando `priv_check`, que también llama a `prison_priv_check`?

### Detach y descarga

- ¿El detach drena todos los callouts (`callout_drain`)?
- ¿El detach drena todos los taskqueues (`taskqueue_drain_all`)?
- ¿El detach detiene los threads del kernel que haya creado el driver (mediante variables de condición o mecanismos similares)?
- ¿El detach libera todos los recursos que attach asignó?
- ¿Se puede descargar el módulo en cualquier momento (sin bloqueos en `kldunload`)?

### VNET (si aplica)

- ¿El driver registra sysinit/sysuninit de VNET para el estado por VNET?
- ¿El driver utiliza macros con el prefijo `V_` para las variables por VNET?
- ¿La ruta de traslado de VNET asigna estado en la entrada y lo libera en la salida?
- ¿El driver utiliza `CURVNET_SET` / `CURVNET_RESTORE` cuando accede a un VNET distinto del actual?

### Pruebas

- ¿El driver tiene un target `make test` (aunque solo compile y cargue el módulo)?
- ¿Se ha ejecutado el driver a través del ciclo attach-detach al menos 100 veces?
- ¿Se ha cargado el driver tanto bajo VirtIO como en modo passthrough (si aplica)?
- ¿Se ha cargado el driver dentro de al menos una jaula y una jaula VNET?
- ¿Se ha compilado el driver al menos en amd64; idealmente también en arm64?

### Documentación

- ¿Hay un README que explique qué hace el driver y cómo compilarlo?
- ¿Hay una página de manual que describa la interfaz del driver visible para el usuario?
- ¿La tabla PNP es suficientemente completa para que `kldxref` encuentre el driver para la carga automática?

Un driver que supera esta lista de comprobación está en buen camino para ser compatible con entornos de virtualización. Un driver que falla en varios puntos necesita atención antes de comportarse correctamente en los distintos entornos del despliegue moderno. Recorre la lista con cada driver que escribas; es más rápido que depurar cada problema individualmente cuando el driver llega a un cliente.

## Apéndice: Esbozo de un backend de bhyve para vtedu

El Desafío 5 pide al lector que escriba un backend de `bhyve(8)` para el driver `vtedu`. Este apéndice esboza la arquitectura de dicho backend con un nivel de detalle útil para la planificación. No se trata de una implementación completa; escribir esa implementación es el desafío. El objetivo aquí es desmitificar el lado del backend en la historia VirtIO para que el desafío resulte abordable.

### Dónde vive el código

El emulador en espacio de usuario `bhyve(8)` vive bajo `/usr/src/usr.sbin/bhyve/`. Sus archivos fuente son una mezcla de emulación de CPU y chipset, emuladores por dispositivo para distintos tipos VirtIO y código de pegamento que conecta `bhyve(8)` con el módulo del kernel `vmm(4)`. Los archivos por dispositivo relevantes para VirtIO son:

- `/usr/src/usr.sbin/bhyve/pci_virtio_rnd.c`: virtio-rnd (generador de números aleatorios). El backend VirtIO más sencillo. Léelo primero.
- `/usr/src/usr.sbin/bhyve/pci_virtio_block.c`: virtio-blk (dispositivo de bloques).
- `/usr/src/usr.sbin/bhyve/pci_virtio_net.c`: virtio-net (red).
- `/usr/src/usr.sbin/bhyve/pci_virtio_9p.c`: virtio-9p (recurso compartido de sistema de archivos).
- `/usr/src/usr.sbin/bhyve/pci_virtio_console.c`: virtio-console (serie).

Cada uno de estos archivos tiene entre unas pocas centenas y un par de miles de líneas de código. Todos comparten un framework de backend común en `/usr/src/usr.sbin/bhyve/virtio.h` y `/usr/src/usr.sbin/bhyve/virtio.c`. Los archivos `pci_virtio_*.c` anteriores son consumidores por dispositivo de ese framework; cada uno registra un `struct virtio_consts`, un conjunto de callbacks de virtqueue y el layout del espacio de configuración específico del dispositivo.

### El framework gestiona el protocolo

La buena noticia para quien escribe un backend es que `bhyve(8)` ya implementa el protocolo VirtIO. La negociación de características, la gestión del anillo de descriptores, la entrega de notificaciones y la inyección de interrupciones residen todas en el framework de `virtio.c`. Un nuevo backend implementa únicamente el comportamiento específico del dispositivo: qué ocurre cuando llega un buffer a la virtqueue, qué campos del espacio de configuración expone el dispositivo y cómo se generan los eventos a nivel de dispositivo.

La interfaz orientada al framework es un pequeño conjunto de callbacks, encapsulados en un `struct virtio_consts`.

```c
/* Sketch, not actual bhyve code. */
struct virtio_consts {
	const char *vc_name;
	int vc_nvq;
	size_t vc_cfgsize;
	void (*vc_reset)(void *);
	void (*vc_qnotify)(void *, struct vqueue_info *);
	int (*vc_cfgread)(void *, int, int, uint32_t *);
	int (*vc_cfgwrite)(void *, int, int, uint32_t);
	void (*vc_apply_features)(void *, uint64_t);
	uint64_t vc_hv_caps;
};
```

Un nuevo backend rellena esta estructura y la registra en el framework. El framework llama al backend cuando el driver del lado del huésped hace algo relevante (reinicia el dispositivo, notifica la virtqueue, lee o escribe el espacio de configuración).

### Esbozo del backend de vtedu

Para `vtedu`, el backend es sencillo. Tiene una sola virtqueue, ningún campo del espacio de configuración más allá de los genéricos y un único bit de característica (`VTEDU_F_UPPERCASE`). Su estado es:

```c
struct pci_vtedu_softc {
	struct virtio_softc vsc_vs;
	struct vqueue_info vsc_vq;  /* just one queue */
	pthread_mutex_t vsc_mtx;
	uint64_t vsc_features;
};
```

Los callbacks son pequeños.

```c
static void
pci_vtedu_reset(void *vsc)
{
	struct pci_vtedu_softc *sc = vsc;

	pthread_mutex_lock(&sc->vsc_mtx);
	vi_reset_dev(&sc->vsc_vs);
	sc->vsc_features = 0;
	pthread_mutex_unlock(&sc->vsc_mtx);
}

static void
pci_vtedu_apply_features(void *vsc, uint64_t features)
{
	struct pci_vtedu_softc *sc = vsc;

	pthread_mutex_lock(&sc->vsc_mtx);
	sc->vsc_features = features;
	pthread_mutex_unlock(&sc->vsc_mtx);
}
```

El callback interesante es `vc_qnotify`, que se llama cuando el huésped notifica la virtqueue.

```c
static void
pci_vtedu_qnotify(void *vsc, struct vqueue_info *vq)
{
	struct pci_vtedu_softc *sc = vsc;
	struct iovec iov[1];
	uint16_t idx;
	int n;

	while (vq_has_descs(vq)) {
		n = vq_getchain(vq, &idx, iov, 1, NULL);
		if (n < 1) {
			EPRINTLN("vtedu: empty chain");
			vq_relchain(vq, idx, 0);
			continue;
		}

		if ((sc->vsc_features & VTEDU_F_UPPERCASE) != 0) {
			for (int i = 0; i < iov[0].iov_len; i++) {
				uint8_t *b = iov[0].iov_base;
				if (b[i] >= 'a' && b[i] <= 'z')
					b[i] = b[i] - 'a' + 'A';
			}
		}

		vq_relchain(vq, idx, iov[0].iov_len);
	}

	vq_endchains(vq, 1);
}
```

Ese es el núcleo de todo. Las llamadas a `vq_has_descs`, `vq_getchain`, `vq_relchain` y `vq_endchains` son funciones auxiliares del framework que desempaquetan los descriptores de la virtqueue en estructuras `iovec` y reempaquetan los resultados.

### Conexión con la tabla de dispositivos

`bhyve(8)` mantiene una tabla de emuladores de dispositivos; cada backend se registra en tiempo de compilación. El registro utiliza una macro `PCI_EMUL_TYPE(...)` (o similar) que añade la vtable del backend a un linker-set. Una vez registrado, la línea de comandos de `bhyve(8)` puede referenciar el backend por su nombre:

```sh
bhyve ... -s 7,virtio-edu guest0
```

El argumento `-s 7,virtio-edu` añade el dispositivo `virtio-edu` en el slot PCI 7. Cuando arranca el huésped, el kernel de FreeBSD enumera el bus PCI, encuentra el dispositivo con el identificador de fabricante VirtIO y el ID de dispositivo correcto, y le adjunta `vtedu`.

### Lo que debe verificar el backend

Para que el backend sea correcto, el autor debe verificar:

- El ID de dispositivo VirtIO coincide con lo que espera el driver (`VIRTIO_ID_EDU = 0xfff0`).
- La respuesta de negociación de características coincide con lo que espera el driver (anunciar `VTEDU_F_UPPERCASE` y `VIRTIO_F_VERSION_1`).
- Los tamaños de la virtqueue son suficientemente grandes para la carga de trabajo del driver (256 es un valor razonable por defecto).
- El tamaño del espacio de configuración coincide con lo que lee el driver (cero para `vtedu`).

### Prueba del flujo completo extremo a extremo

Con ambas partes en su lugar, la prueba extremo a extremo tiene este aspecto.

En el host, tras compilar el backend e instalar el `bhyve(8)` modificado:

```sh
sudo bhyve ... -s 7,virtio-edu guest0
```

Dentro del huésped, tras copiar y compilar `vtedu.c`:

```sh
sudo kldload ./virtio_edu.ko
ls /dev/vtedu0
echo "hello world" > /dev/vtedu0
cat /dev/vtedu0
```

La salida esperada de `cat` es `HELLO WORLD` (convertida a mayúsculas por el backend).

Si la salida es `hello world` (sin mayúsculas), el backend no negoció `VTEDU_F_UPPERCASE`. Si el `echo` se bloquea, la notificación de la virtqueue no llega al backend. Si el `cat` se bloquea, la inyección de interrupciones del backend no llega al huésped. Cada uno de estos casos es un fallo específico que las técnicas de depuración del capítulo pueden localizar.

### Por qué vale la pena este ejercicio

Escribir un backend y un driver enseña ambos lados de la historia VirtIO. El lado del driver es lo que la mayoría de autores acaba escribiendo, pero el lado del backend te explica *por qué* el protocolo VirtIO tiene la forma que tiene. Los bits de característica cobran sentido cuando tienes que decidir cuáles anunciar. Los descriptores de virtqueue cobran sentido cuando tienes que desempaquetarlos. La entrega de interrupciones cobra sentido cuando tienes que inyectar una.

Completar el Desafío 5 te promueve de "usuario de VirtIO" a "autor de VirtIO", lo que supone un nivel de comprensión diferente. Si el desafío parece grande, abórdalo en etapas: primero consigue que el dispositivo aparezca en el huésped (verifícalo con `pciconf -lv`), luego haz que funcione la negociación de características, después gestiona un único mensaje de virtqueue y finalmente pule el pipeline completo. Cada etapa es un commit independiente y un hito satisfactorio por sí mismo.

Aquí termina el material técnico del capítulo. La prosa restante recoge lo que has aprendido y apunta hacia el Capítulo 31.

## Apéndice: Ejecución de las técnicas del capítulo en CI

La integración continua es hoy una parte estándar de la mayoría de los proyectos de drivers. Este breve apéndice describe cómo encajan las técnicas del capítulo en un pipeline de CI. El objetivo es mostrar que la virtualización y la contenedorización no son únicamente preocupaciones en tiempo de ejecución; también son herramientas prácticas para mantener un driver correcto a lo largo de los cambios.

### Por qué CI se beneficia de la virtualización

Un sistema de CI es, entre otras cosas, un lugar donde se necesitan entornos de prueba reproducibles. Ejecutar pruebas en hardware físico es posible, pero frágil: la máquina de pruebas acumula estado, distintas máquinas tienen hardware diferente y resulta difícil separar los fallos de las particularidades del hardware. Ejecutar las pruebas dentro de una VM elimina la mayor parte de estos problemas. La VM parte de una pizarra en blanco al inicio de cada ejecución, su "hardware" es uniforme entre ejecuciones y sus fallos son los fallos del driver, no los del host.

Para CI de drivers FreeBSD, el enfoque estándar es ejecutar un huésped FreeBSD bajo `bhyve(8)` (si el host de CI es FreeBSD) o bajo KVM/QEMU (si el host de CI es Linux). El huésped arranca una imagen FreeBSD, carga el driver, ejecuta el conjunto de pruebas y termina. El ciclo completo tarda menos de un minuto para un driver pequeño, lo que supone cientos de pruebas al día contra cada commit.

### Un flujo de CI mínimo para un driver VirtIO

Un flujo razonable para el CI de un driver VirtIO es:

1. Obtener el código fuente del driver.
2. Compilar el driver contra el kernel FreeBSD de destino (habitualmente con compilación cruzada en el host de CI).
3. Arrancar una VM FreeBSD con los dispositivos VirtIO apropiados.
4. Copiar el módulo compilado a la VM.
5. Conectarse por SSH a la VM y cargar el módulo.
6. Ejecutar el conjunto de pruebas.
7. Capturar la salida.
8. Apagar la VM.
9. Informar del resultado (éxito/fallo).

Los pasos 1 y 2 son idénticos a los de un flujo sin VM. Los pasos 3 a 9 son lo que añade la virtualización.

### Herramientas prácticas

Para el paso 3, `vm-bhyve` resulta cómodo en hosts FreeBSD. Para hosts de CI con Linux, `virt-install` de libvirt es una herramienta estándar. Ambas producen una VM en ejecución en pocos segundos a partir de una imagen precompilada.

Para el paso 4, lo habitual es un volumen compartido o una pequeña copia por SSH. `virtfs` (9P) o `virtiofs` hacen accesibles directorios del host dentro del huésped; `scp` a través de una interfaz tap también funciona.

Para el paso 5, las claves SSH preinstaladas y una dirección IP estática (o una reserva DHCP) hacen que la conexión sea sencilla.

Para los pasos 6 y 7, el conjunto de pruebas es lo que el autor del driver escriba: un script de shell, un programa en C, un conjunto de pruebas en Python. Sea lo que sea, se ejecuta dentro de la VM.

Para el paso 8, `vm stop guest0 --force` (o equivalente) apaga la VM rápidamente. La imagen se descarta; la siguiente ejecución comienza desde cero.

Para el paso 9, el código de salida del conjunto de pruebas determina el resultado. Los sistemas de CI esperan cero para el éxito y distinto de cero para el fallo; sé consistente.

### Un conjunto de pruebas mínimo

Un conjunto de pruebas sencillo de tipo éxito/fallo para un driver VirtIO podría tener este aspecto.

```sh
#!/bin/sh
set -e

# Inside the guest.  Expects the module at /tmp/mydriver.ko.

kldload /tmp/mydriver.ko

# Wait for the device to attach.
for i in 1 2 3 4 5; do
	if [ -c /dev/mydev0 ]; then
		break
	fi
	sleep 1
done

if [ ! -c /dev/mydev0 ]; then
	echo "FAIL: /dev/mydev0 did not appear"
	exit 1
fi

# Exercise the device.
echo "hello" > /dev/mydev0
output=$(cat /dev/mydev0)
if [ "$output" != "hello" ]; then
	echo "FAIL: expected 'hello', got '$output'"
	exit 1
fi

# Clean up.
kldunload mydriver

echo "PASS"
exit 0
```

Breve, legible y con un resultado claro. El CI escala con pruebas como esta: añade una por característica y ejecútalas todas en cada commit.

### Escalado a múltiples configuraciones

Un pipeline de CI puede ejecutar el mismo driver bajo múltiples configuraciones levantando varias VMs. Los ejes útiles incluyen:

- Versión del kernel (FreeBSD 14.3, 14.2, 13.5, -CURRENT).
- Arquitectura (amd64, arm64 con un guest arm64).
- Conjunto de características de VirtIO (deshabilitar forzosamente ciertas características en el backend para poner a prueba las rutas de fallback).
- Hipervisor (bhyve, QEMU/KVM, VMware), cuyo soporte varía.

Cada configuración es un job independiente que el sistema de CI paraleliza. El resultado acumulado de «el driver supera cada configuración» es una señal sólida de que el driver es robusto.

### Una nota sobre la CI con hardware en el bucle

Para los drivers que se comunican con hardware real, la CI necesita una configuración bare-metal o de passthrough. Esto es más caro y menos habitual, pero algunos proyectos mantienen un pequeño conjunto de máquinas de prueba para este fin. Las técnicas descritas en el capítulo son aplicables: un banco de pruebas de hardware usa el passthrough de `ppt(4)` para dar a un invitado acceso a un dispositivo concreto, y el sistema de CI controla ese invitado del mismo modo en que controlaría a un invitado VirtIO puro.

La CI con hardware es más lenta de configurar y más cara de mantener. Para la mayoría de los proyectos, la CI con VirtIO puro es suficiente para la mayor parte de las pruebas, con un pequeño conjunto de pruebas de hardware que se ejecutan con una cadencia menor.

### El beneficio

Una CI que ejercita un driver en condiciones realistas detecta regresiones rápidamente, cuando la corrección todavía está fresca en la mente del autor. Una regresión detectada en el momento del commit tarda minutos en corregirse; una detectada semanas después, durante una release candidate, puede costar horas. La virtualización hace que lo primero sea asequible, y ese es uno de los argumentos más sólidos para tomarse en serio las técnicas de este capítulo.

## Apéndice: hoja de referencia de comandos

Una lista compacta de los comandos que un autor de drivers usa con mayor frecuencia al trabajar con virtualización y contenerización en FreeBSD. Mantén esta página abierta mientras trabajas en los laboratorios.

### Virtualización en el host

```sh
# Check hypervisor extensions
sysctl hw.vmm

# Load/unload vmm(4)
kldload vmm
kldunload vmm

# vm-bhyve guest management
vm list
vm start guest0
vm stop guest0
vm console guest0
vm configure guest0
vm install guest0 /path/to/iso
vm create -t default -s 20G guest0

# Direct bhyve (verbose but informative)
bhyvectl --vm=guest0 --destroy
bhyvectl --vm=guest0 --suspend=normal
```

### Inspección en el invitado

```sh
# Is this a guest?
sysctl kern.vm_guest

# Which devices attached?
pciconf -lvBb
devinfo -v
kldstat -v

# VirtIO-specific
dmesg | grep -i virtio
sysctl dev.virtio_pci
sysctl dev.virtqueue
```

### PCI Passthrough

```sh
# Mark a device as passthrough-capable (in /boot/loader.conf)
pptdevs="5/0/0"

# Verify after reboot
pciconf -lvBb | grep ppt
dmesg | grep -i dmar      # Intel
dmesg | grep -i amdvi     # AMD
```

### Jails

```sh
# Create and manage jails
jail -c name=test path=/jails/test host.hostname=test ip4=inherit persist
jls
jexec test /bin/sh
jail -r test

# devfs rulesets
devfs rule -s 100 show
devfs -m /jails/test/dev rule -s 100 applyset
```

### VNET

```sh
# epair setup
kldload if_epair
ifconfig epair create
ifconfig epair0a 10.0.0.1/24 up

# Move one end into a VNET jail
jail -c name=vnet-test vnet vnet.interface=epair0b path=/jails/vnet-test persist

# Confirm
ifconfig -j vnet-test epair0b
```

### Límites de recursos

```sh
# rctl
rctl -a jail:test:memoryuse:deny=512M
rctl -a jail:test:pcpu:deny=50
rctl -h jail:test
rctl -l jail:test
```

### Observabilidad

```sh
# Interrupt rate
vmstat -i

# VM exit counts (needs PMC)
pmcstat -S VM_EXIT -l 10

# DTrace virtqueue activity
dtrace -n 'fbt::virtqueue_enqueue:entry { @[probefunc] = count(); }'

# Kernel trace
ktrace -i -p $(pgrep bhyve)
kdump -p $(pgrep bhyve)
```

### Ciclo de vida del módulo

```sh
# Build, load, test, unload
make clean && make
sudo kldload ./mydriver.ko
kldstat -v | grep mydriver
# ...exercise driver...
sudo kldunload mydriver
```

Estos son los comandos del día a día. Una tarjeta de referencia rápida como esta, clavada en la pared o abierta en una pestaña del terminal, ahorra horas a lo largo de un proyecto.

Con esto, el capítulo queda completo.
