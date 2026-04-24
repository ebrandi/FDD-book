---
title: "Drivers USB y serie"
description: "El capítulo 26 abre la Parte 6 enseñando el desarrollo de drivers específicos de transporte a través de dispositivos USB y serie. Explica qué hace distintos a los drivers USB y serie de los drivers de dispositivo de caracteres genéricos construidos en capítulos anteriores del libro; presenta el modelo mental USB (roles de host y dispositivo, clases de dispositivo, descriptores, interfaces, endpoints y los cuatro tipos de transferencia); presenta el modelo mental serie (hardware UART, entramado estilo RS-232, tasa de baudios, paridad, control de flujo y la disciplina tty de FreeBSD); recorre la organización del subsistema USB de FreeBSD y los patrones de registro que los drivers utilizan para conectarse a `uhub`; muestra cómo un driver USB configura transferencias bulk, de interrupción y de control a través de `usbd_transfer_setup` y las gestiona en callbacks que siguen la máquina de estados `USB_GET_STATE`; explica cómo un driver USB puede exponer una interfaz `/dev` visible para el usuario a través de `usb_fifo` o un `cdevsw` personalizado; contrasta los dos mundos serie de FreeBSD, el subsistema `uart(4)` para hardware UART real y el framework `ucom(4)` para adaptadores USB a serie; enseña cómo la tasa de baudios, la paridad, los bits de parada y el control de flujo RTS/CTS se transportan a través de `struct termios` y se programan en el hardware; y muestra cómo probar el comportamiento de los drivers USB y serie sin hardware físico ideal usando `nmdm(4)`, `cu(1)`, `usb_template(4)`, la redirección USB de QEMU y las propias facilidades de loopback del kernel. El driver `myfirst` gana un nuevo driver hermano específico de transporte, `myfirst_usb`, en la versión 1.9-usb, que identifica un par de identificadores fabricante/producto, se conecta al insertar el dispositivo, configura una transferencia bulk-in y una bulk-out, devuelve los bytes recibidos a través de un nodo /dev y se desconecta limpiamente al extraer el dispositivo en caliente. El capítulo prepara al lector para el capítulo 27 (almacenamiento y la capa VFS) estableciendo los dos modelos mentales que el lector reutilizará en toda la Parte 6: un transporte es un protocolo más un ciclo de vida, y un driver FreeBSD específico de transporte es un driver Newbus cuyos recursos son endpoints del bus en lugar de BARs de PCI."
partNumber: 6
partName: "Writing Transport-Specific Drivers"
chapter: 26
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 300
language: "es-ES"
---
# USB y drivers serie

## Introducción

El capítulo 25 cerró la Parte 5 con un driver con el que el resto del sistema podía comunicarse. El driver `myfirst` en la versión `1.8-maintenance` contaba con una macro de logging con limitación de tasa, un vocabulario de errno cuidadoso, tunables del loader y sysctls con permiso de escritura, una división en tres versiones, una cadena de limpieza con `goto` etiquetado en attach y detach, un diseño modular limpio del código fuente, metadatos `MODULE_DEPEND` y `MODULE_PNP_INFO`, un documento `MAINTENANCE.md`, un manejador de eventos `shutdown_pre_sync` y un script de regresión capaz de cargar y descargar el driver cien veces seguidas sin filtrar un solo recurso. Lo que ese driver no tenía era contacto alguno con hardware real. El dispositivo de caracteres respaldaba un buffer en memoria del kernel. Los contadores de sysctl rastreaban operaciones contra ese buffer. El ioctl `MYFIRSTIOC_GETCAPS` anunciaba capacidades implementadas íntegramente en software. Todo lo que hacía el driver, lo hacía sin leer jamás un byte de un cable.

El capítulo 26 comienza el paso hacia fuera. En lugar de servir un buffer en RAM, el driver se conectará a un bus real y atenderá a un dispositivo real. El bus será el Universal Serial Bus, porque USB es el transporte más accesible en FreeBSD: es ubicuo, su subsistema está muy bien organizado, la interfaz del kernel se articula alrededor de un puñado de estructuras y macros, y cualquier desarrollador de FreeBSD ya tiene una docena de dispositivos USB sobre su escritorio. Tras USB, el capítulo gira hacia el tema que históricamente precedió a USB y sigue conviviendo con él en todas partes, desde consolas de depuración hasta módulos GPS: el puerto serie, en su forma clásica como interfaz RS-232 gestionada por UART y en su forma moderna como puente USB a serie. Al final del capítulo, la familia de drivers `myfirst` habrá ganado un nuevo hermano específico de transporte, `myfirst_usb`, en la versión `1.9-usb`. Ese hermano sabe cómo conectarse a un dispositivo USB real, cómo configurar una transferencia bulk-in y otra bulk-out, cómo hacer eco de los bytes recibidos a través de un nodo en `/dev`, y cómo sobrevivir a que el dispositivo sea desenchufado mientras el driver está en uso.

El capítulo 26 es el capítulo inicial de la Parte 6. La Parte 6 se organiza en torno a la observación de que, hasta este punto, el libro ha enseñado las partes del desarrollo de drivers para FreeBSD que son *independientes del transporte*: el modelo Newbus, la interfaz de dispositivo de caracteres, la sincronización, las interrupciones, DMA, la gestión de energía, la depuración, la integración y el mantenimiento. Todas esas disciplinas se aplican a cualquier driver, independientemente de cómo esté conectado el dispositivo. La Parte 6 desplaza el enfoque. USB, almacenamiento y redes tienen cada uno su propio bus, su propio ciclo de vida, su propio patrón de flujo de datos y su propia forma idiomática de integrarse con el resto del kernel. Las disciplinas que has adquirido en las Partes 1 a 5 se mantienen sin cambios; lo que es nuevo es la forma de la interfaz entre tu driver y el subsistema específico al que se conecta. El capítulo 26 enseña esa forma para USB y para dispositivos serie. El capítulo 27 la enseña para dispositivos de almacenamiento y la capa VFS. El capítulo 28 la enseña para interfaces de red. Cada uno de los tres capítulos tiene una estructura paralela: se presenta el transporte, se mapea el subsistema, se construye un driver mínimo y se muestra al lector cómo probar sin el hardware específico que quizás no tenga a mano.

La combinación de USB y serie en este capítulo es deliberada. Los dos temas aparecen juntos porque ambos son ciudadanos de primera clase dentro del mismo modelo mental más amplio: un transporte es un *protocolo más un ciclo de vida*, y el driver es el fragmento de código que transporta datos a través del límite del protocolo y mantiene el ciclo de vida coherente con la visión del kernel sobre el dispositivo. USB es un protocolo con un vocabulario rico de cuatro tipos de transferencia y un ciclo de vida de hot-plug. Un UART es un protocolo con un vocabulario de enmarcado de bytes mucho más simple y un ciclo de vida de conexión estática. Estudiarlos juntos hace visible el patrón. Un estudiante que ha visto la máquina de estados de callbacks USB y el manejador de interrupciones de UART uno al lado del otro comprende que "el modelo de drivers de FreeBSD" no es una única forma, sino una familia de formas, cada una adaptada a las exigencias de su propio transporte.

La segunda razón para combinar USB y serie es histórica y práctica. Un número muy grande de lo que el sistema operativo llama "dispositivos USB" son, en realidad, puertos serie disfrazados. El chip FTDI FT232R, el Prolific PL2303, el Silicon Labs CP210x y el WCH CH340 exponen al espacio de usuario una API de puerto serie estándar, pero físicamente residen en el bus USB. FreeBSD gestiona esto con el framework `ucom(4)`: un driver USB registra callbacks con `ucom`, y `ucom` produce los nodos de dispositivo `/dev/ttyU0` y `/dev/cuaU0` visibles al usuario y se encarga de que la disciplina de línea compatible con termios funcione correctamente sobre un par bulk-in y bulk-out USB. El lector que está a punto de escribir un driver USB tiene muchas probabilidades de escribir, tarde o temprano, un driver USB a serie, y ese driver será una intersección de los dos mundos que presenta este capítulo. Reunir el material en un único capítulo hace visible esa intersección.

Una tercera razón es pedagógica. El driver `myfirst` hasta ahora ha sido un pseudodispositivo. La transición al hardware real es un paso conceptual, no solo un paso de codificación. Muchos lectores encontrarán inquietante su primer intento con un driver respaldado por hardware real: las interrupciones llegan sin avisar, las transferencias pueden bloquearse o agotar el tiempo de espera, el dispositivo puede desenchufarse en mitad de una operación y el kernel tiene opiniones sobre la rapidez con la que se puede responder. USB es la introducción más amable posible a ese mundo porque el subsistema USB realiza una cantidad inusualmente grande de trabajo en nombre del driver. Configurar una transferencia bulk en USB no es el mismo tipo de problema que configurar un anillo DMA en una NIC PCI Express. El núcleo USB gestiona los detalles de bajo nivel del DMA; tu driver trabaja al nivel de "avísame cuando esta transferencia se complete". Aprender el patrón USB primero hace que los capítulos de hardware posteriores (almacenamiento, redes, buses embebidos en la Parte 7) resulten menos intimidantes, porque para entonces la forma básica de un driver específico de transporte ya es familiar.

El recorrido del driver `myfirst` a lo largo de este capítulo es concreto. Parte de la versión `1.8-maintenance` al final del capítulo 25. Añade un nuevo archivo fuente, `myfirst_usb.c`, compilado en un nuevo módulo del kernel, `myfirst_usb.ko`. El nuevo módulo se declara dependiente de `usb`, lista un único identificador de fabricante y producto en su tabla de coincidencias, sondea y se conecta durante el hot-plug, asigna una transferencia bulk-in y otra bulk-out, expone un nodo `/dev/myfirst_usb0`, hace eco de los bytes entrantes al log del kernel y los copia de vuelta al leerlos, gestiona el detach limpiamente cuando se desconecta el cable y mantiene sin excepción todas las disciplinas del capítulo 25. Los laboratorios ejercitan cada pieza por separado. Al final del capítulo existe un segundo driver en la familia, un nuevo diseño de código fuente para acomodarlo y un ejemplo funcional de un driver USB para FreeBSD que el lector ha escrito él mismo.

Como este capítulo también trata de dispositivos serie, dedica tiempo a la parte serie de su alcance aunque `myfirst_usb` en sí no sea un driver serie. El material sobre serie enseña cómo está organizado `uart(4)`, cómo encaja `ucom(4)`, cómo termios lleva la velocidad en baudios, la paridad y el control de flujo desde el espacio de usuario hasta el hardware, y cómo probar interfaces serie sin hardware físico usando `nmdm(4)`. El material sobre serie no construye un nuevo driver de hardware UART desde cero. Escribir un driver de hardware UART es una tarea especializada que casi nunca es la elección correcta en un entorno moderno: el driver `ns8250` ya existente en el sistema base gestiona todos los puertos serie compatibles con PC, todas las tarjetas serie PCI comunes y el ARM PL011 que presentan la mayoría de las plataformas virtualizadas. El capítulo enseña el subsistema serie al nivel que el lector realmente necesita: cómo está organizado, cómo leer los drivers existentes, cómo termios alcanza el método `param` de un driver, cómo usar el subsistema desde el espacio de usuario y qué hacer cuando el objetivo es un driver USB a serie (el caso habitual) en lugar de un nuevo driver de hardware (el caso excepcional).

El ritmo del capítulo 26 es el ritmo del reconocimiento de patrones. El lector saldrá del capítulo sabiendo qué aspecto tiene un driver USB, qué aspecto tiene un driver serie, dónde se solapan ambos, en qué se diferencian de los pseudodrivers de las Partes 2 a 5, y cómo probar los dos sin un laboratorio lleno de adaptadores. Esos son los cimientos del desarrollo de drivers específicos de transporte. El capítulo 27 aplicará entonces la misma disciplina al almacenamiento, y el capítulo 28 la aplicará a las redes, adaptando cada vez el mismo patrón general a las reglas de un nuevo transporte.

### El driver al cierre del capítulo 25

Un breve punto de control antes de empezar el nuevo trabajo. El capítulo 26 extiende la familia de drivers producida al final del capítulo 25, etiquetada como versión `1.8-maintenance`. Si alguno de los puntos siguientes es incierto, vuelve al capítulo 25 y resuélvelo antes de comenzar este capítulo, porque el nuevo material asume que todas las primitivas del capítulo 25 funcionan y que todos los hábitos están asentados.

- Tu código fuente del driver coincide con la Etapa 4 del capítulo 25. `myfirst.ko` compila sin errores, se identifica como `1.8-maintenance` en `kldstat -v` y lleva la triple `MYFIRST_VERSION`, `MODULE_VERSION` y `MYFIRST_IOCTL_VERSION`.
- El diseño del código fuente está dividido: `myfirst_bus.c`, `myfirst_cdev.c`, `myfirst_ioctl.c`, `myfirst_sysctl.c`, `myfirst_debug.c`, `myfirst_log.c`, con `myfirst.h` como cabecera privada compartida.
- La macro de log con limitación de tasa `DLOG_RL` está en su lugar y vinculada a un `struct myfirst_ratelimit` dentro del softc.
- La cadena de limpieza `goto fail;` en `myfirst_attach` funciona y ha sido ejercitada en un laboratorio de fallo deliberado.
- El script de regresión supera cien ciclos consecutivos de `kldload`/`kldunload` sin OIDs residuales, sin nodos cdev huérfanos y sin memoria filtrada.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con `/usr/src` en disco, un kernel de depuración con `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS` y `DDB_CTF`, y un snapshot de VM en el estado `1.8-maintenance` al que puedes revertir.

Ese driver, esos archivos y esos hábitos son lo que el capítulo 26 extiende. Las incorporaciones introducidas en este capítulo viven casi en su totalidad en un nuevo archivo, `myfirst_usb.c`, que se convierte en un segundo módulo del kernel que comparte la misma familia conceptual que `myfirst.ko` pero construye un `myfirst_usb.ko` separado. Los laboratorios del capítulo ejercitan cada etapa del nuevo módulo: probe, attach, configuración de transferencias, gestión de callbacks, exposición en /dev y detach. El capítulo no modifica `myfirst.ko` en sí; el driver existente permanece como implementación de referencia de las Partes 2 a 5, y el nuevo driver es su hermano con transporte USB.

### Qué aprenderás

Al final de este capítulo serás capaz de:

- Explicar qué diferencia a un driver específico de transporte de los pseudo-drivers construidos en las Partes 2 a 5, y nombrar las tres grandes categorías de trabajo que un driver específico de transporte debe añadir a su base Newbus: reglas de coincidencia, mecánica de transferencia y gestión del ciclo de vida con hot-plug.
- Describir el modelo mental de USB al nivel necesario para escribir un driver: los roles de host frente a dispositivo, hubs y puertos, clases de dispositivo (CDC, HID, Mass Storage, Vendor), la jerarquía de descriptores (device, configuration, interface, endpoint), los cuatro tipos de transferencia (control, bulk, interrupt, isochronous) y el ciclo de vida del hot-plug.
- Leer la salida de `usbconfig` y `dmesg` para un dispositivo USB e identificar su identificador de fabricante, identificador de producto, clase de interfaz, direcciones de endpoint, tipos de endpoint y tamaños de paquete.
- Describir el modelo mental de comunicación serie al nivel necesario para escribir un driver: el UART como registro de desplazamiento con un generador de baudios, el entramado RS-232, los bits de inicio y parada, la paridad, el control de flujo por hardware mediante RTS y CTS, el control de flujo por software mediante XON y XOFF, y la relación entre `struct termios`, `tty(9)` y el callback `param` del driver.
- Explicar la diferencia entre el subsistema `uart(4)` de FreeBSD para hardware UART real y el framework `ucom(4)` para puentes USB-a-serie, y nombrar los dos mundos que el autor de un driver serie no debe confundir nunca.
- Escribir un driver de dispositivo USB que se enganche a `uhub`, declare una tabla de coincidencias `STRUCT_USB_HOST_ID`, implemente los métodos `probe`, `attach` y `detach`, utilice `usbd_transfer_setup` para configurar una transferencia bulk-in y una bulk-out, y realice una liberación limpia de recursos mediante una cadena de gotos etiquetados.
- Escribir un callback de transferencia USB que siga la máquina de estados `USB_GET_STATE`, gestione `USB_ST_SETUP` y `USB_ST_TRANSFERRED` correctamente, distinga `USB_ERR_CANCELLED` de otros errores y responda a un endpoint bloqueado con `usbd_xfer_set_stall`.
- Exponer un dispositivo USB al espacio de usuario mediante el framework `usb_fifo` o mediante un `cdevsw` registrado con `make_dev_s` personalizado, y saber cuándo es correcta cada opción.
- Leer un driver UART existente en `/usr/src/sys/dev/uart/` con un vocabulario de patrones que haga clara la intención del código en una primera lectura, incluyendo la separación `uart_class`/`uart_ops`, el despacho de métodos, el cálculo del divisor de baudios y la maquinaria de despertar en el lado tty.
- Traducir un `struct termios` a los cuatro argumentos del método `param` de un UART (baud, databits, stopbits, parity), y saber qué flags de termios pertenecen a la capa de hardware y cuáles a la disciplina de línea.
- Probar un driver USB frente a un dispositivo simulado mediante redirección USB de QEMU o `usb_template(4)`, y probar un driver serie frente a un par null-modem `nmdm(4)` sin ningún hardware real.
- Usar `cu(1)`, `tip(1)`, `stty(1)`, `comcontrol(8)` y `usbconfig(8)` para operar, configurar e inspeccionar dispositivos serie y USB desde el espacio de usuario.
- Gestionar la desconexión en caliente de forma limpia en el camino de detach de un driver USB: cancelar transferencias pendientes, vaciar callouts y taskqueues, liberar recursos del bus y destruir nodos cdev, sabiendo en todo momento que el dispositivo puede haber desaparecido antes de que se ejecute el método detach.

La lista es larga porque los drivers específicos de transporte tocan muchas superficies a la vez. Cada elemento es acotado y enseñable. El trabajo del capítulo consiste en convertir ese conjunto de elementos en una imagen mental coherente y reutilizable.

### Qué no cubre este capítulo

Varios temas relacionados se posponen explícitamente a capítulos posteriores para que el Capítulo 26 se mantenga centrado en los fundamentos del desarrollo de drivers USB y de puerto serie.

- **Las transferencias isócronas USB y la transmisión de vídeo/audio de alto ancho de banda** se mencionan a nivel conceptual en la Sección 1, pero no se desarrollan. Las transferencias isócronas son el tipo más complejo de los cuatro y casi siempre se utilizan a través de frameworks de nivel superior (audio, captura de vídeo) que merecen un tratamiento propio. El Capítulo 26 se centra en las transferencias de control, bulk e interrupción, que en conjunto cubren la gran mayoría del trabajo con drivers USB.
- **El modo dispositivo USB y la programación de gadgets** a través de `usb_template(4)` se introducen brevemente con fines de prueba, pero no se desarrollan. Escribir un gadget USB personalizado es un proyecto especializado que queda fuera del alcance de un primer capítulo centrado en transportes específicos.
- **Los internos de los drivers del controlador USB host** (`xhci`, `ehci`, `ohci`, `uhci`) quedan fuera del alcance. Estos drivers implementan la maquinaria de protocolo de bajo nivel que `usbd_transfer_setup` acaba invocando; un autor de drivers casi nunca necesita modificarlos. El capítulo los trata como una plataforma estable.
- **Escribir desde cero un nuevo driver de hardware UART** está fuera del alcance. El driver `ns8250` existente cubre todos los puertos serie comunes de PC, el driver `pl011` cubre la mayoría de las plataformas ARM y los drivers de SoC embebido cubren el resto. Escribir un nuevo driver UART es el trabajo especializado de portar FreeBSD a un nuevo system-on-chip, lo cual es un tema propio (que se trata en la Parte 7 junto con Device Tree y ACPI). El Capítulo 26 enseña al lector cómo *leer* y *comprender* un driver UART, no cómo escribir uno.
- **Los drivers de almacenamiento** (proveedores GEOM, dispositivos de bloques, integración con VFS) son el tema del Capítulo 27. El almacenamiento masivo USB se menciona únicamente como ejemplo de una clase de dispositivo USB, no como objetivo de desarrollo de un driver.
- **Los drivers de red** (`ifnet(9)`, mbufs, gestión de anillos RX/TX) son el tema del Capítulo 28. Los adaptadores de red USB se mencionan como ejemplo de CDC Ethernet, no como objetivo de desarrollo de un driver.
- **USB/IP para pruebas remotas de dispositivos USB a través de una red** se menciona como opción para los lectores que verdaderamente no pueden obtener ningún USB pass-through, pero no se desarrolla. La vía de pruebas estándar en este capítulo es una VM local con redirección de dispositivos.
- **Las peculiaridades y soluciones específicas de fabricante** a través de `usb_quirk(4)` se mencionan pero no se desarrollan. Un autor de drivers que necesite quirks ya está más allá del nivel que enseña este capítulo.
- **Bluetooth, Wi-Fi y otros transportes inalámbricos que utilizan USB** como bus físico están fuera del alcance. Esas pilas implican protocolos que van mucho más allá del propio USB y constituyen cuerpos de trabajo independientes.
- **La abstracción agnóstica de transporte para drivers multi-bus** (la misma lógica de driver conectándose a PCI, USB y puerto serie mediante una interfaz común) se pospone al capítulo de portabilidad de la Parte 7.

Mantenerse dentro de esos límites hace que el Capítulo 26 sea un capítulo sobre *los transportes USB y serie*, no un capítulo sobre todas las técnicas que un desarrollador de kernel senior especializado en transportes podría emplear en un problema avanzado de ese ámbito.

### Dónde estamos en la Parte 6

La Parte 6 tiene tres capítulos. El Capítulo 26 es el capítulo de apertura y enseña el desarrollo de drivers específicos de transporte a través de dispositivos USB y de puerto serie. El Capítulo 27 enseña el desarrollo de drivers específicos de transporte a través de dispositivos de almacenamiento y la capa VFS. El Capítulo 28 lo hace a través de interfaces de red. Los tres capítulos son estructuralmente paralelos en el sentido de que cada uno introduce un transporte, mapea el subsistema, construye un driver mínimo y enseña pruebas sin hardware.

El Capítulo 26 es el lugar adecuado para comenzar la Parte 6 por tres razones. La primera es que USB es la introducción más suave a los drivers respaldados por hardware: sus abstracciones fundamentales son más pequeñas que las del almacenamiento (sin grafo GEOM, sin VFS), más pequeñas que las de la red (sin cadenas de mbuf, sin ring buffers con punteros head/tail y mitigación de interrupciones), y el subsistema se encarga de una gran parte de las tareas difíciles en nombre del driver. La segunda es que USB está en todas partes. Incluso un lector que nunca escribirá un driver de almacenamiento o de red probablemente escribirá un driver USB en algún momento: un termómetro, un registrador de datos, un adaptador serie personalizado, un equipo de pruebas de fábrica. La tercera razón es pedagógica. El patrón que enseña USB (un subsistema con ciclos de vida probe-and-attach, configuración de transferencias a través de un array de configuración, finalización basada en callbacks y un detach limpio al desconectar) es el mismo patrón (con especificidades distintas) que enseñan el almacenamiento y la red. Verlo primero en USB hace que los dos capítulos siguientes resulten reconocibles.

El Capítulo 26 tiende un puente hacia el Capítulo 27 cerrando con una nota sobre el ciclo de vida: el camino de detach de USB es un ensayo general para el camino de extracción en caliente de un dispositivo de almacenamiento, y los patrones que el lector acaba de practicar volverán en cuanto se extraiga un disco USB externo en el Capítulo 27. También tiende un puente hacia atrás con el Capítulo 25, llevando hacia adelante toda la disciplina de ese capítulo: `MODULE_DEPEND`, `MODULE_PNP_INFO`, el patrón de goto etiquetado, el vocabulario de errno, el registro con limitación de tasa, la disciplina de versiones y un script de regresión que ejercita el nuevo módulo con el mismo rigor con que el Capítulo 25 ejercitaba el anterior.

### Una pequeña nota sobre la dificultad

Si la transición de los pseudo-drivers al hardware real parece intimidante en la primera lectura, esa sensación es completamente normal. Todo desarrollador de FreeBSD experimentado tuvo en su momento un primer driver USB que no hacía attach, una primera sesión serie en la que `cu` se negaba a hablar y una primera sesión de depuración en la que `dmesg` permanecía en silencio. El capítulo está estructurado para guiarte suavemente a través de cada uno de esos momentos con laboratorios, notas de resolución de problemas y puntos de salida. Si una sección empieza a resultar abrumadora, lo correcto no es seguir a la fuerza, sino detenerte, leer el driver real correspondiente en `/usr/src` y volver cuando el código real haga visible el concepto. Los drivers de FreeBSD existentes son el mejor recurso didáctico al que puede señalar este capítulo, y el capítulo señalará hacia ellos con frecuencia.

## Guía para el lector: cómo utilizar este capítulo

El Capítulo 26 tiene tres niveles de implicación, y puedes elegir el que se adapte a tu situación actual. Los niveles son lo suficientemente independientes como para que puedas leer para comprender ahora y volver a la práctica más adelante sin perder continuidad.

**Solo lectura.** De tres a cuatro horas. La lectura te proporciona los modelos mentales de USB y del puerto serie, la forma de los subsistemas de FreeBSD y la capacidad de reconocer patrones al leer drivers existentes. Si todavía no estás en posición de cargar módulos del kernel (porque tu VM de laboratorio no está disponible, lees en el transporte o tienes una reunión de planificación en treinta minutos), una pasada de solo lectura es una inversión que vale la pena. El capítulo está escrito de modo que la prosa lleva el peso de la enseñanza; los bloques de código están ahí para anclar la prosa, no para sustituirla.

**Lectura más los laboratorios prácticos.** De ocho a doce horas repartidas en dos o tres sesiones. Los laboratorios te guían a través de la construcción de `myfirst_usb.ko`, la exploración de dispositivos USB reales con `usbconfig`, la configuración de un enlace serie simulado con `nmdm(4)`, la comunicación con él mediante `cu` y la ejecución de una prueba de estrés de desconexión en caliente. Los laboratorios son el lugar donde el capítulo pasa de la explicación al reflejo. Si puedes dedicar de ocho a doce horas repartidas en dos o tres sesiones, haz los laboratorios. El coste de saltarlos es que los patrones permanecen abstractos en lugar de convertirse en hábito.

**Lectura más los laboratorios más los ejercicios de desafío.** De quince a veinte horas repartidas en tres o cuatro sesiones. Los ejercicios de desafío van más allá del ejemplo resuelto del capítulo y te adentran en el territorio donde debes adaptar el patrón a un nuevo requisito: añadir un ioctl de transferencia de control, portar el driver al framework `usb_fifo`, leer un driver USB desconocido de principio a fin, simular un cable defectuoso con inyección de fallos o extender el script de regresión para cubrir el nuevo módulo. El material de desafío no introduce nuevas bases; extiende las que el capítulo acaba de enseñar. Dedica tiempo a los desafíos en proporción a la autonomía que esperas tener en tu próximo proyecto de driver.

No te precipites. Este es el primer capítulo del libro cuyo material depende de hardware real o de una simulación convincente. Reserva un bloque de tiempo en el que puedas observar `dmesg` después de `kldload` y leerlo despacio. Un driver USB que hace attach sin errores suele ser correcto; un driver USB cuyos mensajes de attach no has leído realmente suele estar mal de una forma que te costará una hora de depuración dos días después. La pequeña disciplina de leer la salida del attach a medida que ocurre, en lugar de darla por supuesta, es un hábito que merece la pena adquirir en el Capítulo 26, porque todos los capítulos posteriores sobre transportes específicos dependen de él.

### Ritmo de estudio recomendado

Tres estructuras de sesión funcionan bien para este capítulo.

- **Dos sesiones largas de cuatro a seis horas cada una.** Primera sesión: Introducción, Guía para el lector, Cómo sacar el máximo partido a este capítulo, Sección 1, Sección 2 y Laboratorio 1. Segunda sesión: Sección 3, Sección 4, Sección 5, Laboratorios 2 a 5 y el Cierre. La ventaja de las sesiones largas es que permaneces en el modelo mental el tiempo suficiente para conectar el vocabulario de la Sección 1 con el código de callbacks de la Sección 3.

- **Cuatro sesiones medianas de dos a tres horas cada una.** Sesión 1: desde la Introducción hasta la Sección 1 y el Laboratorio 1. Sesión 2: Sección 2 y Laboratorio 2. Sesión 3: Sección 3 y Laboratorios 3 y 4. Sesión 4: Sección 4, Sección 5, Laboratorio 5 y Cierre. La ventaja es que cada sesión tiene un hito claro.

- **Una lectura lineal seguida de una pasada práctica.** Día uno: lee el capítulo completo de principio a fin sin ejecutar ningún código, para establecer el modelo mental completo. Día dos o día tres: vuelve al capítulo con un árbol de código fuente del kernel y una VM de laboratorio abierta, y trabaja los laboratorios en secuencia. La ventaja de este enfoque es que el modelo mental está completamente cargado antes de tocar código, lo que permite detectar errores de nivel conceptual con anticipación.

No intentes abarcar todo el capítulo en una única sesión maratónica. El material es denso, y la máquina de estados de callbacks USB en particular no da buenos resultados si se lee con cansancio.

### Cómo es una buena sesión de estudio

Una buena sesión de estudio para este capítulo tiene cinco elementos visibles a la vez. Coloca el capítulo del libro en un lado de tu pantalla. Coloca los archivos fuente relevantes de FreeBSD en un segundo panel: `/usr/src/sys/dev/usb/usbdi.h`, `/usr/src/sys/dev/usb/misc/uled.c` y `/usr/src/sys/dev/uart/uart_bus.h` son los tres más útiles para mantener abiertos. Coloca un terminal en tu VM de laboratorio en un tercer panel. Coloca `man 4 usb`, `man 4 uart` y `man 4 ucom` en un cuarto panel como referencia rápida. Por último, mantén abierto un pequeño archivo de notas para las preguntas que querrás responder más adelante. Si aparece un término que no puedes definir, escríbelo en el archivo de notas y sigue leyendo; si el mismo término aparece dos veces, búscalo antes de continuar. Esta es la postura de estudio que permite sacar el máximo partido de un capítulo técnico extenso.

### Si no tienes un dispositivo USB con el que probar

Muchos lectores no tendrán a mano un dispositivo USB de repuesto que coincida con los identificadores de fabricante y producto del ejemplo resuelto. No pasa nada. La Sección 5 enseña tres formas de proceder: la redirección de dispositivos USB de QEMU del host al invitado, `usb_template(4)` para FreeBSD como dispositivo USB, y el enfoque de dispositivo simulado que prueba la lógica del driver sin ningún bus real. El ejemplo resuelto del capítulo está escrito de modo que la tabla de coincidencias del driver puede sustituirse por otra que coincida con cualquier dispositivo USB que tengas en tu escritorio. Servirá una unidad flash USB. Servirá un ratón. Servirá un teclado. El capítulo explica cómo apuntar el driver al dispositivo que tengas, a costa de arrebatárselo temporalmente al driver integrado del kernel, lo cual también se cubre en el capítulo.

## Cómo sacar el máximo partido a este capítulo

Cinco hábitos rinden más en este capítulo que en cualquiera de los anteriores.

Primero, **mantén cuatro páginas del manual abiertas en una pestaña del navegador o en un panel de terminal**: `usb(4)`, `usb_quirk(4)`, `uart(4)` y `ucom(4)`. Estas cuatro páginas en conjunto ofrecen la visión de conjunto más concisa que el proyecto FreeBSD tiene de los subsistemas que este capítulo presenta. Ninguna de ellas es larga. `usb(4)` describe el subsistema desde la perspectiva del usuario y enumera las entradas de `/dev` que aparecen. `usb_quirk(4)` lista la tabla de quirks y explica qué es un quirk, lo que te ahorrará confusión más adelante cuando veas código de quirks en drivers reales. `uart(4)` describe el subsistema serie desde la perspectiva del usuario. `ucom(4)` describe el framework USB-to-serial. Ojeálas una vez al comienzo del capítulo. Cuando la prosa indique «consulta la página del manual para más detalles», vuelve a la página correspondiente. Las páginas del manual son la fuente autoritativa; este libro es comentario.

Segundo, **ten tres drivers reales a mano**. `/usr/src/sys/dev/usb/misc/uled.c` es un driver USB muy pequeño que controla un LED conectado por USB. Utiliza el framework `usb_fifo`, que es uno de los dos patrones visibles para el usuario que el capítulo enseña, y su función attach completa ocupa menos de una página. `/usr/src/sys/dev/usb/misc/ugold.c` es un driver USB algo mayor que lee datos de temperatura de un termómetro TEMPer mediante transferencias de interrupción. Muestra el otro tipo de transferencia habitual e ilustra cómo un driver emplea un callout para espaciar sus lecturas. `/usr/src/sys/dev/uart/uart_dev_ns8250.c` es el driver UART 16550 canónico; todos los puertos serie de PC del mundo lo utilizan. Lee cada uno de estos tres archivos una vez al comienzo del capítulo y otra vez al final. La primera lectura resultará en gran parte opaca; la segunda parecerá casi obvia. Ese cambio es la medida del progreso que este capítulo ofrece.

Tercero, **escribe a mano cada adición de código**. El archivo `myfirst_usb.c` crece a lo largo del capítulo en aproximadamente una docena de pequeños incrementos. Cada incremento corresponde a uno o dos párrafos de prosa. Escribir el código a mano es lo que convierte la prosa en memoria muscular. Pegarlo directamente te salta la lección. Si esto te parece pedante, ten en cuenta que todo autor de drivers USB funcionales ha escrito la función attach de un driver USB al menos una docena de veces; escribir esta es la primera de esa docena.

Cuarto, **lee `dmesg` después de cada `kldload`**. Un driver USB produce un patrón predecible de mensajes de attach: el dispositivo se detecta en un puerto, el driver realiza el probe, la coincidencia tiene éxito, el driver hace el attach y aparece el nodo `/dev`. Si alguno de esos pasos falta, algo va mal, y cuanto antes detectes el paso ausente, antes lo corregirás. La disciplina más pequeña que este capítulo puede darte es el hábito de ejecutar `dmesg | tail -30` inmediatamente después de `kldload` y leer cada línea. Si la salida resulta aburrida, el driver probablemente funciona. Si la salida te sorprende, investiga antes de continuar.

Quinto, **tras cada sección, pregúntate qué ocurriría si tiraras del cable**. La pregunta parece absurda; es fundamental. Un driver bien escrito para un transporte específico es siempre aquel que gestiona su retirada mientras está en uso. Un driver USB en particular opera en un entorno donde la desconexión en caliente es la condición normal de funcionamiento. Si te encuentras escribiendo una sección de código y no puedes responder «¿qué pasaría si se desconectara el cable justo aquí?», esa sección aún no está terminada. El capítulo 26 retoma esta pregunta con frecuencia, no como retórica sino como disciplina.

### Qué hacer cuando algo no funciona

No todo funcionará a la primera. Los drivers USB tienen algunos modos de fallo habituales, y el capítulo documenta cada uno de ellos en la sección de resolución de problemas al final. Un breve adelanto de los más comunes:

- El driver compila pero no hace attach cuando se enchufa el dispositivo. Normalmente la tabla de concordancia tiene el identificador de fabricante o producto incorrecto. La solución es verificar el identificador con `usbconfig dump_device_desc`.
- El driver hace attach pero el nodo `/dev` no aparece. Normalmente la llamada a `usb_fifo_attach` falló porque el nombre entra en conflicto con un dispositivo existente. La solución es cambiar el `basename` o desconectar antes el driver que causa el conflicto.
- El driver hace attach pero la primera transferencia nunca se completa. Normalmente `usbd_transfer_start` no fue llamado, o la transferencia se envió con un frame de longitud cero. La solución es recorrer `USB_ST_SETUP` y confirmar que `usbd_xfer_set_frame_len` fue llamado antes de `usbd_transfer_submit`.
- El driver hace attach pero el kernel entra en pánico al desenchufar el dispositivo. Normalmente la ruta de detach no incluye una llamada a `usbd_transfer_unsetup` o a `usb_fifo_detach`. La solución es ejecutar la secuencia de detach con INVARIANTS activado y seguir la salida de WITNESS hasta el primer paso de limpieza omitido.

La sección de resolución de problemas al final del capítulo desarrolla cada uno de estos casos en detalle, con comandos de diagnóstico y la salida esperada. El objetivo de este capítulo no es que todo funcione a la primera; el objetivo es desarrollar una postura sistemática de depuración que convierta cada fallo en un momento de aprendizaje.

### Mapa del capítulo

Las secciones, en orden, son:

1. **Fundamentos de USB y los dispositivos serie.** El modelo mental de USB al nivel necesario para escribir un driver: host y dispositivo, hubs y puertos, clases, descriptores, tipos de transferencia, ciclo de vida del hot-plug. El modelo mental de serie: hardware UART, tramas RS-232, tasa de baudios, paridad, control de flujo, la disciplina tty. La división específica de FreeBSD entre `uart(4)` y `ucom(4)`. Un primer ejercicio con `usbconfig` y `dmesg` que ancla el vocabulario en un dispositivo que puedes ver.

2. **Escribir un driver de dispositivo USB.** La distribución del subsistema USB de FreeBSD. La forma Newbus de un driver USB. `STRUCT_USB_HOST_ID` y la tabla de concordancia. `DRIVER_MODULE` con `uhub` como padre. `MODULE_DEPEND` en `usb`. `USB_PNP_HOST_INFO` para la carga automática. El método probe usando `usbd_lookup_id_by_uaa`. El método attach, la distribución del softc, la cadena de limpieza con goto etiquetado en attach y detach.

3. **Realizar transferencias de datos USB.** El array `struct usb_config`. `usbd_transfer_setup` y el ciclo de vida de un `struct usb_xfer`. Las formas de transferencia de control, bulk e interrupción. La máquina de estados `usb_callback_t` y `USB_GET_STATE`. Gestión de stalls con `usbd_xfer_set_stall`. Operaciones a nivel de frame (`usbd_xfer_set_frame_len`, `usbd_copy_in`, `usbd_copy_out`). Creación de una entrada `/dev` para el dispositivo USB mediante el framework `usb_fifo`. Un ejemplo desarrollado que envía bytes por un endpoint bulk-out y lee bytes de vuelta desde un endpoint bulk-in.

4. **Escribir un driver serie (UART).** El subsistema `uart(4)` al nivel necesario para leer un driver real. La división `uart_class`/`uart_ops`. La tabla de métodos despachada mediante kobj. La relación entre `uart_bus_attach` y `uart_tty_attach`. Tasa de baudios, bits de datos, bits de parada, paridad y el método `param`. Control de flujo hardware RTS/CTS. `struct termios` y cómo llega al driver. `/dev/ttyu*` frente a `/dev/cuau*` en FreeBSD. El framework `ucom(4)` para puentes USB a serie. Una lectura guiada del driver `ns8250` como ejemplo canónico.

5. **Probar drivers USB y serie sin hardware real.** Pares de null-modem virtuales `nmdm(4)` para pruebas de serie. `cu(1)` y `tip(1)` para acceso de terminal. `stty(1)` y `comcontrol(8)` para la configuración. Redirección de dispositivos USB de QEMU para pass-through de host a invitado. `usb_template(4)` para pruebas de FreeBSD como gadget. Patrones de loopback por software que ejercitan la lógica del driver sin ningún dispositivo físico. Un arnés de pruebas reproducible que ejecuta una regresión sin intervención humana.

Tras las cinco secciones vienen un conjunto de laboratorios prácticos, un conjunto de ejercicios de desafío, una referencia de resolución de problemas, un apartado de cierre que concluye la historia del Capítulo 26, un puente hacia el Capítulo 27 y un glosario. Lee de forma lineal en la primera lectura.

## Sección 1: Fundamentos de USB y los dispositivos serie

La primera sección enseña los modelos mentales en los que se apoya el resto del capítulo. Los dispositivos USB y serie comparten una cantidad sorprendente de maquinaria en la capa `tty`/`cdevsw`, y al mismo tiempo difieren de forma radical en la capa de transporte. Un lector que tenga claras tanto las similitudes como las diferencias encontrará las secciones 2 a 5 directas. Quien no las tenga encontrará el código posterior confusamente opaco. Esta sección es el mejor lugar donde invertir treinta minutos extra si quieres que el resto del capítulo te resulte más sencillo.

La sección está organizada en tres arcos. El primer arco establece qué es un *transporte* y por qué los drivers específicos de transporte tienen un aspecto diferente al de los pseudo-drivers de las partes 2 a 5. El segundo arco enseña el modelo USB: host y dispositivo, hubs y puertos, clases, descriptores, endpoints, tipos de transferencia y el ciclo de vida del hot-plug. El tercer arco enseña el modelo serie: el UART, las tramas RS-232, la tasa de baudios, la paridad, el control de flujo y la división específica de FreeBSD entre `uart(4)` y `ucom(4)`. Un primer ejercicio al final ancla el vocabulario en un dispositivo que puedes ver con `usbconfig`.

### Qué es un transporte y por qué importa aquí

Un *transporte* es el protocolo y el ciclo de vida mediante los cuales un dispositivo se conecta al resto del sistema. Hasta este punto del libro, el driver `myfirst` no tenía transporte. Su dispositivo existía íntegramente en el árbol Newbus, conectado al `nexus` a través del padre `pseudo`, y sus datos fluían hacia un buffer en memoria del kernel. Eso convierte a `myfirst` en un *pseudo-dispositivo*: un dispositivo cuya existencia es por completo una ficción de software. Los pseudo-dispositivos son herramientas de enseñanza esenciales. Permiten al lector aprender Newbus, la gestión del softc, las interfaces de dispositivo de caracteres, el manejo de ioctl, el uso de locks y todo lo demás, sin necesidad de aprender también los detalles de un bus. A estas alturas, esos temas ya están cubiertos.

Un driver específico de transporte, por el contrario, es aquel que hace attach a un *bus real*. El bus tiene sus propias reglas. Tiene su propia forma de decir "ha aparecido un nuevo dispositivo". Tiene su propia forma de entregar datos. Tiene su propia forma de decir "se ha retirado un dispositivo". Un driver específico de transporte sigue siendo un driver Newbus (eso nunca cambia en FreeBSD), pero su padre ya no es el bus abstracto `pseudo`. Su padre es `uhub` si es un driver USB, `pci` si es un driver PCI, `acpi` o `fdt` si está en una plataforma embebida, y así sucesivamente. El método attach del driver recibe argumentos específicos de ese bus. Sus responsabilidades de limpieza incluyen recursos específicos del bus además de los que ya tenía. Su ciclo de vida es el ciclo de vida del bus, no el del módulo.

Tres grandes categorías de trabajo distinguen a un driver específico de transporte de los pseudo-drivers de las partes 2 a 5. Merece la pena nombrarlas explícitamente porque reaparecen en cada capítulo de transporte de la Parte 6.

La primera es la *concordancia* (matching). Un pseudo-driver hace attach al cargar el módulo; no hay nada que concordar porque no existe ningún dispositivo real. Un driver específico de transporte tiene que declarar qué dispositivos gestiona. En USB, esto significa una tabla de concordancia de identificadores de fabricante y producto. En PCI, significa una tabla de concordancia de identificadores de fabricante y dispositivo. En ACPI o FDT, significa una tabla de concordancia de identificadores de cadena de texto. El código del bus del kernel enumera los dispositivos a medida que aparecen y ofrece cada uno a todos los drivers registrados por turno; el método probe del driver decide si reclama el dispositivo. Conseguir que la tabla de concordancia sea correcta es el primer obstáculo al que se enfrenta todo driver específico de transporte.

La segunda es la *mecánica de transferencia*. Los métodos `read` y `write` de un pseudo-driver acceden a un buffer en RAM. Los métodos `read` y `write` de un driver específico de transporte tienen que organizar el movimiento de datos a través del bus. En USB, esto significa configurar una o más transferencias con `usbd_transfer_setup`, enviarlas con `usbd_transfer_submit` y gestionar la finalización en un callback. En PCI, esto significa programar un motor DMA. En almacenamiento, esto significa traducir peticiones de bloque en transacciones del bus. El mecanismo de transferencia es específico del bus y es donde reside la mayor parte del código nuevo de un driver específico de transporte.

La tercera es el *ciclo de vida del hot-plug*. Un pseudo-driver se carga cuando se carga el módulo y se desconecta cuando se descarga el módulo. Ese es un ciclo de vida sencillo; `kldload` y `kldunload` son los únicos eventos a los que tiene que responder. Un driver específico de transporte tiene que tratar con el *hot-plug*: el dispositivo puede aparecer y desaparecer de forma independiente al ciclo de vida del módulo. Un dispositivo USB puede desenchufarse en mitad de una lectura. Un disco SATA puede extraerse mientras el sistema de archivos que contiene está montado. Un cable Ethernet puede desconectarse mientras hay una conexión TCP abierta. El método attach del driver se ejecuta cuando un dispositivo se inserta físicamente; su método detach se ejecuta cuando un dispositivo se retira físicamente. El detach puede ocurrir mientras el driver sigue en uso. Gestionar esto correctamente es el tercer gran obstáculo al que se enfrenta todo driver específico de transporte.

El resto de la Parte 6 trata esas tres categorías de trabajo en tres transportes diferentes. El Capítulo 26 enseña USB y serie. El Capítulo 27 enseña almacenamiento. El Capítulo 28 enseña redes. La concordancia, la mecánica de transferencia y el ciclo de vida del hot-plug tienen un aspecto diferente en cada transporte, pero la estructura de tres categorías se repite. Esa estructura es lo que hace posible aprender un transporte bien y luego aprender el siguiente con rapidez.

Un resumen útil: en las partes 2 a 5, aprendiste a *ser* un driver Newbus. En la Parte 6, aprendes a *conectarte* a un bus que tiene sus propias ideas sobre cuándo y cómo existes.

### El modelo mental de USB

USB, el Universal Serial Bus, es un bus serie en árbol, controlado por el host y con conexión en caliente. Cada uno de esos adjetivos importa, y comprender cada uno de ellos es la base para escribir un driver USB.

*En árbol* significa que los dispositivos USB no se encuentran en un cable compartido como los de un bus I2C o un antiguo bus ISA. Cada dispositivo USB tiene exactamente una conexión ascendente, hacia un hub padre. La raíz del árbol es el *hub raíz*, que expone el controlador host USB. Aguas abajo del hub raíz hay otros hubs y dispositivos. Un hub tiene un número fijo de puertos descendentes; cada puerto puede estar vacío o conectar a exactamente un dispositivo. El árbol se reconstruye en el boot y se actualiza cada vez que se conecta o desconecta un dispositivo. En FreeBSD, `usbconfig` muestra este árbol; en un arranque limpio de un escritorio típico verás algo como:

```text
ugen0.1: <Intel EHCI root HUB> at usbus0, cfg=0 md=HOST spd=HIGH
ugen1.1: <AMD OHCI root HUB> at usbus1, cfg=0 md=HOST spd=FULL
ugen0.2: <Some Vendor Hub> at usbus0, cfg=0 md=HOST spd=HIGH
ugen0.3: <Some Vendor Mouse> at usbus0, cfg=0 md=HOST spd=LOW
```

La estructura en árbol importa al autor de un driver por dos razones. La primera es que te indica que, cuando escribes un driver USB, el *padre* de tu driver en el árbol Newbus es `uhub`. Todo dispositivo USB se encuentra bajo un hub. Cuando escribes `DRIVER_MODULE(myfirst_usb, uhub, ...)`, le estás diciendo al kernel "mi driver hace attach a los hijos de `uhub`", que es la forma de FreeBSD de decir "mi driver hace attach a dispositivos USB". La segunda razón es que la estructura en árbol significa que la enumeración es dinámica. El kernel no sabe qué dispositivos hay en el árbol hasta que lo recorre; se le ofrece cada dispositivo a medida que aparece, y tiene que decidir si reclamarlo.

*Controlado por host* significa que uno de los lados del bus es el maestro, el *host*, y todos los demás son esclavos, los *dispositivos*. El host inicia cada transferencia; los dispositivos responden. Un teclado USB no envía las pulsaciones al host en el momento en que se presiona una tecla; el host hace polling al teclado en un *endpoint de interrupción* muchas veces por segundo, y el teclado responde con «ninguna tecla nueva» o «se ha pulsado la tecla 'A'» en respuesta a cada consulta. Este modelo de polling y respuesta tiene consecuencias importantes para un driver. Tu driver, que se ejecuta en el host, tiene que iniciar cada transferencia. Un dispositivo no puede enviar datos de forma espontánea; solo puede responder cuando el host pregunta. Lo que desde el espacio de usuario parece «el driver ha recibido datos» es siempre, en el fondo, «el driver tenía una transferencia de recepción pendiente y el controlador de host nos notificó que la transferencia se había completado».

Para la mayor parte de los objetivos de este capítulo, estás escribiendo drivers en *modo host*: drivers que se ejecutan en el lado del host. Un sistema FreeBSD también puede configurarse como *dispositivo* USB, a través del subsistema `usb_template(4)`, y presentarse como un teclado, un dispositivo de almacenamiento masivo o una interfaz Ethernet CDC ante otro host. Los drivers en modo dispositivo son un tema especializado que se trata solo brevemente en la Sección 5 con fines de prueba.

*Hot-pluggable* significa que los dispositivos pueden aparecer y desaparecer mientras el sistema está en funcionamiento, y el subsistema tiene que adaptarse a ello. El controlador de host USB detecta cuando se conecta un dispositivo (los registros de estado de los puertos de un hub se lo indican), enumera el nuevo dispositivo solicitándole sus descriptores, le asigna una dirección en el bus y, a continuación, lo ofrece a cualquier driver cuya tabla de coincidencias sea aplicable. Cuando se desconecta un dispositivo, el controlador de host detecta el cambio de estado del puerto e informa al subsistema, que a su vez llama al método detach del driver. El método detach puede ejecutarse en cualquier momento, incluso mientras el driver tiene una transferencia en curso que ya nunca se completará, mientras el espacio de usuario tiene abierto el nodo `/dev` del driver, o mientras el sistema está bajo carga. Escribir un método detach correcto es la parte más difícil del desarrollo de drivers USB. El capítulo vuelve a este tema repetidamente.

*Serie* significa que USB es un protocolo serie a nivel de cable: los bytes fluyen uno tras otro por un par diferencial. La velocidad del bus ha evolucionado con los años: Low-Speed (1.5 Mbps), Full-Speed (12 Mbps), High-Speed (480 Mbps), SuperSpeed (5 Gbps) y variantes aún más rápidas por encima de esa cifra. Desde la perspectiva del autor de un driver, la velocidad es en gran medida transparente: el controlador de host y el núcleo USB se encargan de la capa eléctrica y del encuadre de paquetes, y tu driver trabaja al nivel de «aquí tienes un buffer, por favor envíalo» o «aquí tienes un buffer, por favor rellénalo». La velocidad determina con qué rapidez pueden moverse los datos, pero el código del driver es el mismo.

Con esos cuatro adjetivos claros, el resto del modelo USB encaja por sí solo.

#### Clases de dispositivo y qué significan para un driver

Todo dispositivo USB pertenece a una o más *clases*, y la clase indica al host (y al driver) qué tipo de dispositivo es. Las clases son numéricas, están definidas por el USB Implementers Forum, y sus valores aparecen en los descriptores. Las que un autor de drivers FreeBSD encontrará con mayor frecuencia son:

- **HID (Human Interface Device)**, clase 0x03. Teclados, ratones, joysticks, mandos de juego y una larga cola de dispositivos programables que se hacen pasar por teclados o ratones. Los dispositivos HID presentan informes a través de endpoints de interrupción; el subsistema HID de FreeBSD los gestiona de forma mayoritariamente genérica, aunque un driver específico del fabricante puede anular este comportamiento.
- **Mass Storage**, clase 0x08. Memorias flash USB, discos externos y lectores de tarjetas. Estos se conectan a través de `umass(4)` al framework de almacenamiento CAM.
- **Communications (CDC)**, clase 0x02, con subclases para ACM (serie tipo módem), ECM (Ethernet), NCM (Ethernet con agregación de múltiples paquetes) y otras. Los dispositivos CDC ACM aparecen a través de `ucom(4)` como puertos serie. Los dispositivos CDC ECM y NCM aparecen a través de `cdce(4)` como interfaces de red.
- **Audio**, clase 0x01. Micrófonos, altavoces e interfaces de audio. La pila de audio de FreeBSD los gestiona a través de `uaudio(4)`.
- **Printer**, clase 0x07. Impresoras USB. Se gestionan a través de `ulpt(4)`.
- **Hub**, clase 0x09. Los propios hubs USB. Los gestiona el driver principal `uhub(4)`.
- **Vendor-specific**, clase 0xff. Cualquier dispositivo cuya funcionalidad no encaja en una clase estándar. Casi todos los dispositivos USB de uso aficionado interesantes (puentes USB a serie, termómetros, controladores de relés, programadores, registradores de datos) pertenecen a esta clase.

Cuando escribes un driver USB, habitualmente lo haces para un dispositivo específico del fabricante (clase 0xff) y la coincidencia se basa en los identificadores de fabricante y producto. En ocasiones escribirás para un dispositivo de clase estándar que FreeBSD todavía no gestiona, o para un dispositivo de clase estándar que presenta peculiaridades que requieren un driver dedicado. La clase no suele ser el criterio de coincidencia; lo es el par fabricante/producto. Sin embargo, la clase te indica con qué framework, si existe alguno, debes integrarte. Si la clase del dispositivo es CDC ACM, el framework adecuado es `ucom`. Si la clase es HID, el framework adecuado es `hidbus` (nuevo en FreeBSD 14). Si la clase es 0xff, no existe ningún framework; escribes un driver a medida.

#### Descriptores: la autodescripción del dispositivo

Cuando el host enumera un nuevo dispositivo USB, le pide que se describa a sí mismo. El dispositivo responde con una jerarquía de *descriptores*. Los descriptores son el concepto USB más importante que debes tener claro: son el equivalente USB del espacio de configuración PCI, pero más rico y con estructura anidada.

La jerarquía es la siguiente:

```text
Device descriptor
  Configuration descriptor [1..N]
    Interface descriptor [1..M] (optionally with alternate settings)
      Endpoint descriptor [0..E]
```

Un *descriptor de dispositivo* (`struct usb_device_descriptor` en `/usr/src/sys/dev/usb/usb.h`) describe el dispositivo en su conjunto: su identificador de fabricante (`idVendor`), su identificador de producto (`idProduct`), su clase de dispositivo, subclase y protocolo, su tamaño máximo de paquete en el endpoint cero, su número de versión y el número de configuraciones que admite. La mayoría de los dispositivos tienen una sola configuración, pero la especificación USB permite más (una cámara que puede funcionar en modos de alta o baja banda ancha, por ejemplo).

Un *descriptor de configuración* (`struct usb_config_descriptor`) describe un modo de funcionamiento: el número de interfaces que contiene, si el dispositivo se alimenta por sí mismo o desde el bus, y su consumo máximo de corriente. Cuando un driver selecciona una configuración (mediante una llamada a `usbd_req_set_config`, aunque en la práctica el núcleo USB lo hace por ti), los endpoints del dispositivo se activan.

Un *descriptor de interfaz* (`struct usb_interface_descriptor`) describe una función lógica del dispositivo. Un dispositivo compuesto, como una impresora USB con escáner integrado, tiene una interfaz por función. Cada interfaz tiene su propia clase, subclase y protocolo. Un driver puede hacer coincidir la clase de la interfaz en lugar de la clase del dispositivo; esto es habitual cuando la clase general del dispositivo es «Miscelánea» o «Compuesto», pero una de sus interfaces tiene una clase específica. Una interfaz puede tener varias *configuraciones alternativas*, que seleccionan diferentes disposiciones de endpoints; las interfaces de streaming de audio utilizan configuraciones alternativas para ofrecer diferentes anchos de banda.

Un *descriptor de endpoint* (`struct usb_endpoint_descriptor`) describe un canal de datos. Los endpoints tienen:

- Una *dirección*, que es el número de endpoint (del 0 al 15) combinado con un bit de dirección (IN, que significa del dispositivo al host, u OUT, que significa del host al dispositivo).
- Un *tipo*, que es uno de los siguientes: control, bulk, interrupción o isócrono.
- Un *tamaño máximo de paquete*, que es el paquete individual más grande que el endpoint puede gestionar.
- Un *intervalo*, que para los endpoints de interrupción e isócronos indica al host con qué frecuencia debe realizar el polling.

El endpoint cero es especial: todos los dispositivos lo tienen, es siempre un endpoint de control y es siempre bidireccional (una mitad IN y una mitad OUT). El núcleo USB utiliza el endpoint cero para la enumeración (solicitar los descriptores al dispositivo, establecer su dirección y seleccionar su configuración). Un driver también puede utilizar el endpoint cero para solicitudes de control específicas del fabricante, aunque los drivers suelen acceder a él a través de funciones auxiliares en lugar de configurar una transferencia directamente.

La jerarquía de descriptores es importante para un driver porque el método `probe` del driver tiene acceso a los descriptores a través del `struct usb_attach_arg` que recibe, y su lógica de coincidencia frecuentemente lee campos de ellos. El `struct usbd_lookup_info` dentro de `struct usb_attach_arg` contiene los identificadores del dispositivo, su clase, subclase y protocolo, la clase, subclase y protocolo de la interfaz actual, y algunos otros campos. La tabla de coincidencias filtra por algún subconjunto de esos campos; las macros auxiliares `USB_VP(v, p)`, `USB_VPI(v, p, info)`, `USB_IFACE_CLASS(c)` y otras similares construyen entradas que coinciden con diferentes combinaciones de campos.

#### Los cuatro tipos de transferencia

USB define cuatro tipos de transferencia, cada uno adecuado para un tipo diferente de movimiento de datos. Un driver elige uno o más tipos para sus endpoints, y esa elección afecta a todo el modo en que está estructurado el driver.

Las *transferencias de control* son para la configuración, la puesta en marcha y el intercambio de comandos. Todos los dispositivos las admiten en el endpoint cero. Tienen un formato pequeño y estructurado: un paquete de setup de ocho bytes (el `struct usb_device_request`) seguido de una etapa de datos opcional y una etapa de estado. El paquete de setup especifica qué hace la solicitud: su dirección (IN u OUT), su tipo (estándar, de clase o de fabricante), su destinatario (dispositivo, interfaz o endpoint) y cuatro campos (`bRequest`, `wValue`, `wIndex`, `wLength`) cuyo significado depende de la solicitud. Las solicitudes estándar incluyen `GET_DESCRIPTOR`, `SET_CONFIGURATION` y similares; las solicitudes de clase y de fabricante están definidas por la especificación de la clase o por el fabricante. Las transferencias de control son fiables: el protocolo del bus garantiza la entrega o devuelve un error específico. Son también relativamente lentas, porque el bus les asigna solo una pequeña fracción de su ancho de banda.

Las *transferencias bulk* son para datos grandes, fiables y no críticos en cuanto al tiempo. Una memoria flash USB utiliza transferencias bulk para los datos reales. Una impresora utiliza bulk OUT para el flujo de impresión. Un puente USB a serie utiliza bulk IN y bulk OUT para las dos direcciones del flujo serie. Las transferencias bulk son fiables (los errores los reintenta el hardware del bus), pero no tienen temporización garantizada: utilizan el ancho de banda que queda tras programar el tráfico de control, interrupción e isócrono. En la práctica, en un bus con poca carga, las transferencias bulk son muy rápidas. En un bus muy cargado, pueden detenerse durante milisegundos. Los endpoints bulk son el tipo de endpoint más habitual para la transmisión de datos de dispositivo a host o de host a dispositivo cuando la latencia no es crítica.

Las *transferencias de interrupción* son para datos pequeños y sensibles al tiempo. El nombre puede llevar a confusión: aquí no hay interrupciones de hardware. La «interrupción» hace referencia al hecho de que el dispositivo necesita llamar periódicamente la atención del host, y el host hace polling al endpoint a un intervalo configurable para comprobar si hay datos nuevos. Un teclado USB utiliza transferencias de interrupción para enviar las pulsaciones de teclas; un ratón USB las utiliza para los informes de movimiento; un termómetro las utiliza para enviar lecturas periódicas. Los endpoints de interrupción tienen un campo `interval` que indica al host con qué frecuencia realizar el polling (en milisegundos para dispositivos de baja velocidad y velocidad completa, en microframes para alta velocidad). Un driver que desea conocer la entrada en el momento en que se produce configura una transferencia interrupt-IN, la envía y el núcleo USB organiza el polling. Cuando llegan datos, se ejecuta el callback del driver.

*Las transferencias isócronas* son para datos en streaming con ancho de banda garantizado pero sin recuperación de errores. El audio USB y el vídeo USB utilizan endpoints isócronos. El bus reserva una parte fija de cada trama para el tráfico isócrono, de modo que el ancho de banda es predecible, pero las transferencias no se reintentan en caso de error; si un paquete resulta corrupto, se pierde. Esta concesión tiene sentido para el audio y el vídeo, donde perder una muestra es preferible a un bloqueo. Las transferencias isócronas son las más complejas de programar porque suelen operar sobre muchas tramas pequeñas por transferencia; la maquinaria de `struct usb_xfer` admite hasta miles de tramas por transferencia. El capítulo 26 introduce las transferencias isócronas a nivel conceptual y no las desarrolla más; los drivers isócronos reales (audio, vídeo) están fuera del alcance del capítulo.

Un dispositivo USB típico de fabricante específico para el que un aficionado o alguien que está aprendiendo a desarrollar drivers escribirá código tiene este aspecto: un identificador de fabricante/producto, una interfaz específica del fabricante, un endpoint bulk-IN, un endpoint bulk-OUT y, posiblemente, un endpoint interrupt-IN para eventos de estado. Esa es la forma del ejemplo desarrollado en las secciones 2 y 3.

#### El ciclo de vida hot-plug de USB

El ciclo de vida hot-plug es la secuencia de eventos que ocurre cuando se inserta un dispositivo USB, se utiliza y se extrae. Escribir un driver que gestione este ciclo de vida correctamente es la disciplina más importante en el desarrollo de drivers USB.

Cuando se inserta un dispositivo, el controlador de host detecta un cambio en el estado del puerto. Espera a que el dispositivo se estabilice, reinicia el puerto y asigna al dispositivo una dirección temporal de cero. Envía `GET_DESCRIPTOR` al endpoint cero en la dirección cero, recupera el descriptor del dispositivo y, a continuación, asigna al dispositivo una dirección única con `SET_ADDRESS`. Toda la comunicación posterior utiliza la nueva dirección. El host envía `GET_DESCRIPTOR` para el descriptor de configuración completo (incluidas interfaces y endpoints), elige una configuración y envía `SET_CONFIGURATION`. En ese momento, los endpoints del dispositivo están activos y el subsistema USB ofrece el dispositivo a cada driver registrado por turnos, llamando al método `probe` de cada uno. El primer driver que reclama el dispositivo devolviendo un código sin error desde `probe` gana; el subsistema llama entonces al método `attach` de ese driver.

Durante el funcionamiento normal, el driver envía transferencias a sus endpoints, el controlador de host las planifica en el bus y las callbacks se disparan al completarse. Este es el estado estable en el que operan los ejemplos de código del capítulo 26.

Cuando se extrae el dispositivo, el controlador de host detecta otro cambio en el estado del puerto. No espera; la señal eléctrica desaparece de inmediato. El subsistema cancela cualquier transferencia pendiente en los endpoints del dispositivo (se completan en la callback con `USB_ERR_CANCELLED`) y, a continuación, llama al método `detach` del driver. El método `detach` debe liberar todos los recursos que adquirió el método `attach`, incluidos los nodos de `/dev` que creó, los locks, los buffers y las transferencias. Debe hacer esto sabiendo que otros threads pueden estar en medio de llamadas al driver a través de esos recursos. Una lectura en curso debe ser despertada y retornar con un error. Un ioctl en curso debe poder terminar o ser interrumpido. Una callback que acaba de dispararse con `USB_ERR_CANCELLED` no debe intentar reenviar.

El ciclo de vida hot-plug es la razón por la que los drivers USB no pueden escribirse como se escriben los pseudodrivers. En un pseudodriver, el ciclo de vida del módulo (`kldload`/`kldunload`) es el único ciclo de vida; nada inesperado ocurre. En un driver USB, el ciclo de vida del dispositivo está separado del ciclo de vida del módulo y está impulsado por eventos físicos. Un usuario puede desconectar el dispositivo mientras un proceso en espacio de usuario está bloqueado en `read()` en el nodo `/dev` del driver. El driver debe despertar ese proceso y retornar un error. Un driver USB bien escrito trata esto como el caso normal, no como el caso excepcional.

La sección 2 recorrerá la estructura de un driver USB que gestiona esto correctamente. Por ahora, ten presente el ciclo de vida: probe, attach, transferencias en estado estable, detach. Todo driver USB tiene esa secuencia.

#### Velocidades USB y sus implicaciones

USB ha pasado por varias generaciones de velocidad, y cada una importa a quien escribe drivers de manera diferente. La baja velocidad (1,5 Mbps) fue la velocidad original de USB 1.0, utilizada principalmente por teclados y ratones. La velocidad completa (12 Mbps) fue USB 1.1, utilizada por impresoras, cámaras tempranas y dispositivos de almacenamiento masivo. La alta velocidad (480 Mbps) fue USB 2.0, que se convirtió en la velocidad dominante para la mayoría de los dispositivos en la década de 2000. SuperSpeed (5 Gbps) fue USB 3.0, que añadió una capa física separada para aplicaciones de alto rendimiento. SuperSpeed+ (10 Gbps y 20 Gbps) llegó con USB 3.1 y 3.2. USB 4.0 reutiliza la capa física de Thunderbolt y admite 40 Gbps.

Para la mayoría del trabajo de escritura de drivers, solo importan tres diferencias entre estas velocidades:

**Tamaño máximo de paquete.** Los endpoints de baja velocidad tienen un tamaño máximo de paquete de 8 bytes. La velocidad completa llega hasta 64 bytes. Los endpoints bulk de alta velocidad llegan hasta 512 bytes. Los endpoints bulk SuperSpeed llegan hasta 1024 bytes con soporte de ráfaga. Los tamaños de buffer en la configuración de transferencia deben coincidir con la velocidad del endpoint; usar un buffer de 512 bytes en un endpoint bulk de velocidad completa desperdicia memoria porque solo caben 64 bytes en cada paquete.

**Ancho de banda isócrono.** Las transferencias isócronas reservan ancho de banda a una velocidad específica. Un dispositivo que solicita 1 MB/s de ancho de banda isócrono solo puede ser compatible con un controlador de host que pueda proporcionarlo; en hosts más lentos, el dispositivo debe negociar una velocidad inferior o fallar. Por eso algunos dispositivos de audio USB funcionan en un puerto pero no en otro.

**Intervalo de sondeo del endpoint.** Los endpoints de interrupción se sondean a un intervalo específico codificado en el campo `bInterval` del descriptor. Las unidades son milisegundos a baja velocidad y velocidad completa, y «microtramas de 125 microsegundos» a alta velocidad y SuperSpeed. El framework se encarga de los cálculos; tu driver simplemente declara el intervalo de sondeo lógico a través del descriptor de endpoint y el framework hace lo correcto.

Para los drivers que escribimos en este capítulo (`myfirst_usb` y los puentes UART como FTDI), la velocidad no afecta a la estructura del código. La callback de un canal bulk-IN es la misma independientemente de si funciona a 12 Mbps o a 5 Gbps. Las diferencias están en los números, no en el flujo.

#### Endpoints, FIFOs y control de flujo

Un endpoint USB es lógicamente una cola de I/O en un extremo de una tubería (pipe). En el lado del dispositivo, un endpoint corresponde a un FIFO de hardware en el chip. En el lado del host, el endpoint es una abstracción del framework. Entre ellos, los paquetes USB fluyen bajo el control del propio protocolo USB, que gestiona la retransmisión, la secuenciación y la detección de errores.

Al host no se le puede indicar «el dispositivo está lleno» de la manera que cabría esperar en un enlace serie tradicional. En cambio, cuando un dispositivo no puede aceptar más datos (porque su FIFO está lleno), devuelve un handshake NAK. NAK significa «inténtalo de nuevo más tarde». El host seguirá reintentando, a nivel de protocolo, hasta que el dispositivo acepte los datos (devuelva ACK) o se dispare algún timeout de nivel superior. Esto se denomina limitación NAK o throttling del bus, y ocurre de forma invisible para el driver: el framework ve el ACK final y entrega una finalización satisfactoria.

Del mismo modo, cuando el dispositivo no tiene datos que enviar (para una transferencia bulk-IN o interrupt-IN), devuelve NAK al token IN y el host sondea de nuevo. Desde la perspectiva del driver, la transferencia simplemente está «pendiente» hasta que el dispositivo tiene algo que comunicar.

Este mecanismo NAK es la forma en que USB gestiona el control de flujo a nivel de protocolo. Tu driver no necesita implementar su propia lógica de throttling para los canales bulk e interrupt; el protocolo USB lo hace. Donde el control de flujo sí entra en juego es en los protocolos de nivel superior, donde el dispositivo puede querer señalar un fin de mensaje lógico o una indisponibilidad temporal. Esas señales son específicas del protocolo y no forman parte del propio USB.

#### Los descriptores en profundidad

Los descriptores USB son el mecanismo de autodescripción mediante el cual un dispositivo informa al host de qué es y cómo comunicarse con él. Los introdujimos brevemente antes; aquí tienes una visión más completa.

El descriptor de dispositivo es la raíz. Contiene el identificador de fabricante del dispositivo, el identificador de producto, la versión de la especificación USB, la clase/subclase/protocolo del dispositivo (para dispositivos que se declaran a sí mismos a nivel de dispositivo en lugar de a nivel de interfaz), el tamaño máximo de paquete para el endpoint cero y el número de configuraciones.

Los descriptores de configuración describen configuraciones completas. Una configuración es un conjunto de interfaces que trabajan juntas. La mayoría de los dispositivos tienen una sola configuración; algunos tienen varias para admitir diferentes modos de operación (por ejemplo, un dispositivo que puede ser tanto una impresora como un escáner, seleccionado por configuración).

Los descriptores de interfaz describen subconjuntos funcionales del dispositivo. Cada interfaz tiene una clase, una subclase y un protocolo que indican al host qué tipo de driver debe utilizar. Un dispositivo multifunción tiene varios descriptores de interfaz en la misma configuración. Además, una interfaz puede tener configuraciones alternativas: diferentes conjuntos de endpoints seleccionables dinámicamente para cosas como el «modo de bajo ancho de banda» frente al «modo de alto ancho de banda».

Los descriptores de endpoint describen los endpoints individuales dentro de una interfaz. Cada uno tiene una dirección (con bit de dirección), un tipo de transferencia, un tamaño máximo de paquete y un intervalo (para los endpoints de interrupción e isócronos).

Los descriptores de cadena contienen cadenas legibles por humanos: el nombre del fabricante, el nombre del producto, el número de serie. Son opcionales; su presencia se indica mediante índices de cadena distintos de cero en los demás descriptores.

Los descriptores específicos de clase amplían los descriptores estándar con metadatos específicos de clase. Los dispositivos HID tienen un descriptor de informe que describe el formato de los informes que envían. Los dispositivos de audio tienen descriptores para los controles de audio. Los dispositivos de almacenamiento masivo tienen descriptores para las subclases de interfaz.

El framework USB analiza todo esto en el momento de la enumeración y expone los datos analizados a los drivers a través de `struct usb_attach_arg`. Tu driver no tiene que leer los descriptores por sí mismo; consulta al framework la información que necesita. Cuando el capítulo dice «el `bInterfaceClass` de la interfaz», se refiere al «campo `bInterfaceClass` del descriptor de interfaz que el framework analizó y almacenó en caché por nosotros».

`usbconfig -d ugenN.M dump_all_config_desc` es la forma de ver los descriptores analizados desde el espacio de usuario. Ejecuta ese comando en algunos dispositivos que tengas y observa cómo son los descriptores. Verás que incluso los dispositivos sencillos como un ratón tienen un árbol de descriptores no trivial: normalmente un descriptor de dispositivo, un descriptor de configuración, un descriptor de interfaz (con class=HID) y uno o dos descriptores de endpoint (para la entrada del informe HID y tal vez una salida).

#### Solicitud-respuesta sobre USB

El tipo de transferencia de control USB admite un patrón de solicitud-respuesta entre el host y el dispositivo. Una transferencia de control consta de tres fases: una etapa de setup en la que el host envía un paquete de setup de 8 bytes que describe la solicitud, una etapa de datos opcional en la que el host envía datos o el dispositivo devuelve datos, y una etapa de estado en la que el receptor confirma la operación.

El paquete de setup tiene cinco campos:

- `bmRequestType`: describe la dirección (entrada o salida), el tipo de solicitud (estándar, de clase o de fabricante) y el destinatario (dispositivo, interfaz, endpoint u otro).
- `bRequest`: el número de solicitud. Las solicitudes estándar tienen números conocidos (GET_DESCRIPTOR = 6, SET_ADDRESS = 5, y así sucesivamente). Las solicitudes de clase y de fabricante tienen significados específicos de clase o de fabricante.
- `wValue`: un parámetro de 16 bits, utilizado habitualmente para especificar un índice de descriptor o un valor a establecer.
- `wIndex`: otro parámetro de 16 bits, utilizado habitualmente para especificar una interfaz o un endpoint.
- `wLength`: el número de bytes en la etapa de datos (cero si no hay etapa de datos).

Todo dispositivo USB debe admitir un pequeño conjunto de peticiones estándar: `GET_DESCRIPTOR`, `SET_ADDRESS`, `SET_CONFIGURATION` y algunas más. El framework gestiona todas ellas durante la enumeración. Tu driver también puede emitir peticiones específicas del fabricante para configurar el dispositivo de maneras que el estándar no define.

Por ejemplo, el driver FTDI emite peticiones específicas del fabricante como `FTDI_SIO_SET_BAUD_RATE`, `FTDI_SIO_SET_LINE_CTRL` y `FTDI_SIO_MODEM_CTRL` para programar el chip. Estas peticiones están documentadas en las notas de aplicación de FTDI; no forman parte de USB en sí, pero funcionan a través del mecanismo de transferencia de control de USB.

Cuando tu driver necesita emitir una petición de control específica del fabricante, el patrón es el que mostramos en la Sección 3: construir el setup packet, copiarlo en el frame cero de una transferencia de control, copiar los datos en el frame uno (para peticiones con fase de datos) y enviarlo. El framework gestiona las tres fases y llama a tu callback cuando la transferencia se completa.

### El modelo mental sobre la comunicación serie

El lado serie del Capítulo 26 trata sobre un protocolo mucho más antiguo y mucho más sencillo que USB. La comunicación serie mediante un UART es una de las formas más antiguas de comunicación entre dos ordenadores, y su sencillez es tanto su punto fuerte como su limitación. Un lector que llega al UART después de estudiar USB encontrará el protocolo casi ridículamente simple. Pero la integración con el resto del sistema operativo, la disciplina tty, la gestión del baud rate, la paridad, el control de flujo y la separación entre dos mundos representados por `uart(4)` y `ucom(4)` es donde reside la mayor parte del trabajo real.

#### El UART como elemento de hardware

Un UART es un *Universal Asynchronous Receiver/Transmitter*: un chip que convierte bytes en un flujo de bits en serie sobre un cable, y viceversa. El UART clásico tiene dos pines para datos (TX y RX), dos pines para control de flujo (RTS y CTS), cuatro pines para el estado del módem (DTR, DSR, DCD, RI), un pin de masa y, ocasionalmente, un pin para una segunda señal de llamada que la mayoría de los equipos modernos ignora. En un PC clásico, el puerto serie tiene un conector D-subminiatura de nueve o veinticinco pines y funciona con niveles de tensión RS-232 (típicamente ±12 V). Los UART embebidos modernos suelen funcionar con niveles lógicos de 3,3 V o 1,8 V; en caso de necesitar un puerto compatible, se interpone un chip conversor de niveles entre el UART y el conector RS-232.

En el interior del UART, el elemento central es un registro de desplazamiento. Cuando el driver escribe un byte en el registro de transmisión del UART, este añade un bit de inicio, desplaza el byte bit a bit a la velocidad de transmisión configurada, añade un bit de paridad opcional y, a continuación, añade uno o dos bits de parada. Cuando el UART receptor detecta un flanco descendente (el bit de inicio), muestrea la línea en el centro de cada período de bit, ensambla los bits en un byte, comprueba la paridad, verifica el bit de parada y, a continuación, almacena el byte en su registro de recepción. Si alguno de esos pasos falla (la paridad no coincide, el bit de parada es incorrecto o el entramado es erróneo), el UART registra un error de entramado, un error de paridad o una condición de break en su registro de estado.

En la mayoría de los UART modernos, los registros individuales de recepción y transmisión están respaldados por pequeños buffers de tipo primero en entrar, primero en salir (FIFO). El UART 16550A, que sigue siendo el estándar de facto, dispone de un FIFO de 16 bytes en cada sentido. Un driver que programa el FIFO con un «trigger level» adecuado puede dejar que el hardware almacene en buffer los bytes entrantes y genere una interrupción solo cuando el FIFO supera dicho nivel. Esta es la diferencia entre «una interrupción por byte» (lento) y «una interrupción por trigger level» (rápido). El FIFO del 16550A es gran parte de la razón por la que este chip se convirtió en el estándar universal para PC.

La velocidad del UART se controla mediante un *divisor de baud rate*: el UART tiene un reloj de entrada (a menudo 1,8432 MHz en hardware de PC clásico), y el baud rate es el reloj dividido entre 16 veces el divisor. Un divisor de 1 con un reloj de 1,8432 MHz da lugar a 115200 baud. Un divisor de 12 da lugar a 9600 baud. El driver `ns8250` de FreeBSD calcula el divisor a partir del baud rate solicitado y lo programa en los registros de pestillo divisor del UART. La sección 4 recorre este código en detalle.

El entramado RS-232 es el protocolo completo: bit de inicio (uno), bits de datos (cinco, seis, siete u ocho), bit de paridad opcional (ninguno, impar, par, mark o space), bit de parada (uno o dos). Una configuración moderna típica es «8N1»: ocho bits de datos, sin paridad, un bit de parada. Una configuración más antigua que se ve a veces en equipos industriales es «7E1»: siete bits de datos, paridad par, un bit de parada. El driver programa el registro de control de línea del UART para seleccionar el entramado; `struct termios` transporta la configuración desde el espacio de usuario.

#### Control de flujo

El UART puede transmitir más rápido de lo que el receptor puede leer si el código receptor es lento o está realizando otro trabajo. El *control de flujo* es el mecanismo por el que el receptor indica al transmisor que debe pausar. Existen dos mecanismos.

El *control de flujo por hardware* emplea dos cables adicionales: *RTS* (Request To Send), controlado por el receptor, y *CTS* (Clear To Send) desde la perspectiva del transmisor (es el cable que maneja el otro extremo). Cuando el buffer del receptor se está llenando, este desactiva RTS. El transmisor, al detectar CTS desactivado, deja de transmitir. Cuando el buffer se vacía, el receptor vuelve a activar RTS, CTS se activa en el otro extremo y la transmisión se reanuda. El control de flujo por hardware es fiable y no requiere ninguna sobrecarga de software en ninguno de los extremos; es la opción preferida cuando el hardware lo admite.

El *control de flujo por software*, también denominado XON/XOFF, utiliza dos bytes en banda: XOFF (tradicionalmente ASCII DC3, 0x13) para pausar la transmisión, y XON (ASCII DC1, 0x11) para reanudarla. El receptor envía XOFF cuando está casi lleno y XON cuando vuelve a tener espacio. Este mecanismo funciona con una conexión de tres hilos (TX, RX, masa) sin pines adicionales, a costa de reservar dos valores de byte para uso de control. Si estás enviando datos binarios que pueden contener 0x11 o 0x13, no puedes utilizar el control de flujo por software; el control de flujo por hardware es la única opción.

La disciplina tty de FreeBSD gestiona el control de flujo por software íntegramente en software, en la capa de disciplina de línea, sin ninguna intervención del driver UART. El control de flujo por hardware reside en parte en el driver (el driver programa la función automática RTS/CTS del UART si el chip lo admite) y en parte en la capa tty. El autor de un driver debe saber qué método de control de flujo ha seleccionado la capa tty; el indicador `CRTSCTS` en `struct termios` señala el control de flujo por hardware.

#### /dev/ttyuN y /dev/cuauN: una peculiaridad específica de FreeBSD

La capa tty de FreeBSD crea dos nodos de dispositivo por puerto serie. El nodo *callin* es `/dev/ttyuN` (donde N es el número de puerto, 0 para el primero). El nodo *callout* es `/dev/cuauN`. La distinción es histórica, de la época de los módems de marcación, y sigue siendo útil.

Un proceso que abre `/dev/ttyuN` está diciendo «quiero atender una llamada entrante»: la apertura se bloquea hasta que el módem activa DCD (Data Carrier Detect). Una vez que DCD está activo, la apertura concluye. Cuando DCD cae, el proceso que tiene el dispositivo abierto recibe SIGHUP. El nodo es para conexiones entrantes.

Un proceso que abre `/dev/cuauN` está diciendo «quiero realizar una llamada saliente»: la apertura tiene éxito de inmediato, sin bloquearse esperando DCD. El proceso puede entonces marcar hacia fuera o, en usos sin módem, simplemente comunicarse con el puerto serie. El nodo es para conexiones salientes y, en general, para cualquier uso que no requiera semántica de módem.

En el uso moderno, cuando un puerto serie está conectado a algo que no es un módem (un microcontrolador, una consola, un receptor GPS), el nodo correcto que se debe abrir es casi siempre `/dev/cuau0`. Abrir `/dev/ttyu0` en un puerto sin módem suele bloquear el proceso, porque DCD nunca se activa. Esta distinción es específica de FreeBSD; Linux no tiene nodos callout y utiliza `/dev/ttyS0` o `/dev/ttyUSB0` para todo.

Los laboratorios del capítulo utilizarán `/dev/cuau0` y el par simulado `/dev/nmdm0A`/`/dev/nmdm0B` para los ejercicios serie. Los nodos callin no se utilizan.

#### Dos mundos: `uart(4)` y `ucom(4)`

FreeBSD separa el hardware UART real de los puentes USB a serie en dos subsistemas distintos. La separación no es visible desde el espacio de usuario (tanto un adaptador USB serie como un puerto serie integrado aparecen como dispositivos tty), pero sí es muy visible desde el interior del kernel, y el autor de un driver no debe confundir ambos.

`uart(4)` es el subsistema para los UART reales. Su ámbito abarca el puerto serie integrado en la placa base de un PC, las tarjetas PCI serie, el `PL011` de PrimeCell presente en placas embebidas ARM, los UART de SoC integrados en plataformas i.MX, Marvell, Qualcomm, Broadcom y Allwinner, y similares. El subsistema `uart` se encuentra en `/usr/src/sys/dev/uart/`. Su código principal está en `uart_core.c` y `uart_tty.c`. Su driver de hardware canónico es `uart_dev_ns8250.c`. Un driver que se vincula a un UART real escribe un `uart_class` y un pequeño conjunto de `uart_ops`, y el subsistema se encarga del resto. Los nodos `/dev` que crea `uart(4)` se denominan `ttyu0`, `ttyu1`, etc. (callin) y `cuau0`, `cuau1`, etc. (callout).

`ucom(4)` es el framework para los puentes USB a serie: FTDI, Prolific, Silicon Labs, WCH y similares. Su ámbito *no* es en absoluto un UART; se trata de un dispositivo USB cuyos endpoints se comportan como un puerto serie. El framework `ucom` se encuentra en `/usr/src/sys/dev/usb/serial/`. Su cabecera es `usb_serial.h`. Su implementación principal es `usb_serial.c`. Un driver USB a serie implementa los métodos USB probe, attach y detach como cualquier otro driver USB, y a continuación registra un `struct ucom_callback` en el framework. La estructura de callback tiene entradas para «open», «close», «set line parameters», «start reading», «stop reading», «start writing», y otras. El framework crea el nodo `/dev` (denominado `ttyU0`, `ttyU1` para callin, `cuaU0`, `cuaU1` para callout, nótese la U mayúscula) y ejecuta la disciplina tty sobre las transferencias USB del driver.

Los dos mundos nunca se mezclan. `uart(4)` es para hardware que es físicamente un UART. `ucom(4)` es para dispositivos USB que se comportan como un UART. Un adaptador USB a serie es un driver `ucom`, no un driver `uart`. Una tarjeta PCI serie es un driver `uart` (concretamente, una capa de adaptación en `uart_bus_pci.c`), no un driver `ucom`. La interfaz de espacio de usuario es similar (ambos generan nodos de dispositivo `cu*`), pero el código del kernel es completamente independiente.

Una nota histórica que a veces confunde a los lectores: FreeBSD contó en su día con un driver `sio(4)` independiente para los UART de la familia 16550. `sio(4)` quedó retirado hace años y no está presente en FreeBSD 14.3. Si encuentras referencias a `sio` en documentación antigua, tradúcelas mentalmente a `uart(4)`. No intentes encontrar ni extender `sio`; ya no existe.

#### Qué transporta termios y adónde va

`struct termios` es la estructura del espacio de usuario que configura un tty. Tiene cinco campos: `c_iflag` (indicadores de entrada), `c_oflag` (indicadores de salida), `c_cflag` (indicadores de control), `c_lflag` (indicadores locales), `c_cc` (caracteres de control), y dos campos de velocidad: `c_ispeed` y `c_ospeed`. Los campos se manipulan con `tcgetattr(3)`, `tcsetattr(3)` y el comando de shell `stty(1)`.

Un driver UART se preocupa casi exclusivamente de `c_cflag` y de los campos de velocidad. `c_cflag` transporta:

- `CSIZE`: el tamaño del carácter (CS5, CS6, CS7, CS8).
- `CSTOPB`: si está activado, dos bits de parada; si está desactivado, uno.
- `PARENB`: si está activado, la paridad está habilitada; el tipo depende de `PARODD`.
- `PARODD`: si está activado junto con `PARENB`, paridad impar; si está desactivado junto con `PARENB`, paridad par.
- `CRTSCTS`: control de flujo por hardware.
- `CLOCAL`: ignorar las líneas de estado del módem; tratar el enlace como local.
- `CREAD`: habilitar el receptor.

Cuando el espacio de usuario llama a `tcsetattr`, la capa tty comprueba la solicitud, invoca el método `param` del driver (a través de la callback `tsw_param` en `ttydevsw`), y el driver traduce los campos de termios en configuraciones de registros de hardware. El código puente de `uart_tty.c` recorre este proceso por completo y es el mejor lugar para observar cómo se produce la traducción.

`c_iflag`, `c_oflag` y `c_lflag` los gestiona principalmente la disciplina de línea tty, no el driver. Controlan aspectos como si la disciplina de línea convierte CR a LF, si el eco está habilitado, si el modo canónico está activo, etc. Un driver UART no necesita saber nada de eso; la capa tty lo gestiona.

#### El control de flujo en las múltiples capas de un TTY

El control de flujo parece un concepto único, pero en la práctica hay varias capas independientes que pueden regular el flujo de datos por separado. Comprender estas capas ayuda a depurar situaciones en las que los datos misteriosamente no fluyen.

La capa más baja es eléctrica. En una línea RS-232 real, las señales de control de flujo (RTS, CTS, DTR, DSR) son pines físicos del conector. El transmisor del extremo remoto solo envía datos cuando su pin CTS está activado. El extremo local activa el pin RTS para indicar al remoto que está listo para recibir. Para que esto funcione, el cable debe conducir correctamente las señales RTS y CTS, y ambos extremos deben tener el control de flujo configurado de forma coherente.

La siguiente capa se encuentra en el propio chip UART. Algunos UART del modelo 16650 y posteriores disponen de control de flujo automático: si está configurado, el propio chip monitoriza CTS y pausa el transmisor sin intervención del driver. El indicador `CRTSCTS` en `c_cflag` habilita esta función.

La siguiente capa son los ring buffers del framework UART. Cuando el ring de RX se llena más allá de una marca de nivel alto, el framework desactiva RTS (si el control de flujo está habilitado) para indicar al lado remoto que pause. Cuando se vacía por debajo de una marca de nivel bajo, RTS vuelve a activarse.

La siguiente capa es la line discipline del tty, que tiene sus propias colas de entrada y salida. La line discipline también puede generar bytes XON/XOFF (0x11 y 0x13) si `IXON` e `IXOFF` están activos en `c_iflag`. Estas son señales de control de flujo por software.

La capa más alta es el bucle de lectura del programa en userland. Si el programa es lento al consumir datos, los bytes se acumulan en todas las capas inferiores.

Cuando depures problemas de control de flujo, comprueba cada capa. Usa `stty -a -f /dev/cuau0` para ver qué flags de `c_cflag` y `c_iflag` están activos. Usa `comcontrol /dev/cuau0` para ver las señales actuales del módem. Usa un multímetro u osciloscopio en las señales físicas si puedes. Trabaja capa por capa hacia abajo hasta encontrar la que está bloqueando realmente el flujo.

#### Por qué los errores de baud rate son insidiosos

Una clase habitual de bug serial es un desajuste de baud rate que casi funciona. Supón que un lado trabaja a 115200 y el otro a 114400 (que es lo que obtienes de un cristal ligeramente desviado). La mayoría de los bytes llegarán correctamente, pero algunos se corromperán. La tasa de error exacta depende del patrón de bits. Las largas rachas de una misma polaridad se desvían más que los patrones alternos.

Peor aún, la tasa de error depende del byte que se envía. Los caracteres imprimibles ASCII están en el rango de 0x20 a 0x7e, donde los bits están bien distribuidos. Los caracteres no imprimibles como 0xff o 0x00 son más propensos a sufrir errores de bit porque presentan largas rachas de una misma polaridad.

Si encuentras que tu driver serial "casi funciona" pero pierde o corrompe unos pocos bytes de cada mil, sospecha de un desajuste de baud rate antes de sospechar de un bug lógico en tu driver. Compara el divisor real que usa el chip con el divisor esperado. Si difieren, el baud rate no es el que solicitaste.

El 16550 usa una fuente de reloj (normalmente 1.8432 MHz) dividida por un divisor de 16 bits para producir 16 veces el baud rate. Para 115200, el divisor es `(1843200 / (115200 * 16)) = 1`. Para 9600, es 12. Para tasas arbitrarias, el divisor puede no ser un número entero, y el entero más cercano produce una tasa redondeada. Una tasa de 115200 solicitada a partir de un reloj de 24 MHz produciría un divisor de `(24000000 / (115200 * 16)) = 13.02`, que se redondea a 13, dando una tasa real de `(24000000 / (13 * 16)) = 115384`, lo que representa un error del 0.16%. La tolerancia estándar para la comunicación serial es del 2-3%, así que el 0.16% está bien.

Cuando configures un UART para un baud rate no estándar, comprueba si la tasa puede representarse exactamente. Si no es así, prueba con un intercambio real de datos, no solo con una comprobación de loopback.

#### Nota histórica sobre los números de minor

Las versiones antiguas de FreeBSD codificaban mucha información en los números de minor de los archivos de dispositivo serial. Distintos números de minor para el lado "callin" frente al lado "callout", para el control de flujo por hardware frente al de software, y para varios estados de bloqueo. Esta codificación ha desaparecido en gran medida en el FreeBSD moderno; las diferencias se gestionan ahora mediante nodos de dispositivo separados con distintos nombres (`ttyu` frente a `cuau`, con sufijos para los estados de lock e init). Si ves una manipulación extraña de números de minor en código antiguo, ten en cuenta que el código moderno no la necesita.

#### Cerrando la sección 1

La sección 1 ha establecido los dos modelos mentales de los que depende el capítulo 26. El modelo USB es un bus serial estructurado en árbol, controlado por el host, con soporte de conexión en caliente, cuatro tipos de transferencia, una jerarquía de descriptores rica y un ciclo de vida en el que los eventos físicos impulsan los eventos del kernel. El modelo serial es un protocolo hardware de registro de desplazamiento simple con baud rate, paridad, bits de parada y control de flujo opcional, integrado en FreeBSD a través de un subsistema dividido entre `uart(4)` para los UARTs reales y `ucom(4)` para los puentes USB a serial, y expuesto al espacio de usuario a través de la line discipline del tty y nodos de dispositivo como `/dev/cuau0`.

Antes de continuar, dedica unos minutos a explorar `usbconfig` en un sistema real. El vocabulario que acabas de aprender es más fácil de retener una vez que has visto los descriptores de un dispositivo USB real con tus propios ojos.

### Ejercicio: usa `usbconfig` y `dmesg` para explorar los dispositivos USB de tu sistema

Este ejercicio es un punto de verificación práctico que ancla el vocabulario de la sección 1 en un dispositivo que puedes ver. Realízalo en tu VM de laboratorio (o en cualquier sistema FreeBSD 14.3 con al menos un dispositivo USB conectado). Tarda aproximadamente quince minutos.

**Paso 1. Inventario.** Ejecuta `usbconfig` sin argumentos:

```console
$ usbconfig
ugen0.1: <Intel EHCI root HUB> at usbus0, cfg=0 md=HOST spd=HIGH (0mA)
ugen0.2: <Generic Storage> at usbus0, cfg=0 md=HOST spd=HIGH (500mA)
ugen0.3: <Logitech USB Mouse> at usbus0, cfg=0 md=HOST spd=LOW (98mA)
```

La primera línea es el hub raíz. El resto de líneas son dispositivos. Lee el formato: `ugenN.M`, donde N es el número de bus y M es el número de dispositivo; la descripción entre corchetes angulares es la cadena del dispositivo; `cfg` es la configuración activa; `md` es el modo (HOST o DEVICE); `spd` es la velocidad del bus (LOW, FULL, HIGH, SUPER); la corriente entre paréntesis es el consumo máximo de potencia que suministra el bus.

**Paso 2. Vuelca los descriptores de un dispositivo.** Elige uno de los dispositivos que no sean el hub raíz y vuelca su descriptor de dispositivo:

```console
$ usbconfig -d ugen0.2 dump_device_desc

ugen0.2: <Generic Storage> at usbus0, cfg=0 md=HOST spd=HIGH (500mA)

  bLength = 0x0012
  bDescriptorType = 0x0001
  bcdUSB = 0x0200
  bDeviceClass = 0x0000  <Probed by interface class>
  bDeviceSubClass = 0x0000
  bDeviceProtocol = 0x0000
  bMaxPacketSize0 = 0x0040
  idVendor = 0x13fe
  idProduct = 0x6300
  bcdDevice = 0x0112
  iManufacturer = 0x0001  <Generic>
  iProduct = 0x0002  <Storage>
  iSerialNumber = 0x0003  <0123456789ABCDE>
  bNumConfigurations = 0x0001
```

Lee cada campo. Observa que `bDeviceClass` es cero: esa es la convención USB que indica «la clase se define por interfaz, no a nivel de dispositivo». Para este dispositivo, la clase de interfaz será Mass Storage (0x08).

**Paso 3. Vuelca la configuración activa.** Ahora vuelca el descriptor de configuración, que incluye las interfaces y los endpoints:

```console
$ usbconfig -d ugen0.2 dump_curr_config_desc

ugen0.2: <Generic Storage> at usbus0, cfg=0 md=HOST spd=HIGH (500mA)

  Configuration index 0

    bLength = 0x0009
    bDescriptorType = 0x0002
    wTotalLength = 0x0020
    bNumInterface = 0x0001
    bConfigurationValue = 0x0001
    iConfiguration = 0x0000  <no string>
    bmAttributes = 0x0080
    bMaxPower = 0x00fa

    Interface 0
      bLength = 0x0009
      bDescriptorType = 0x0004
      bInterfaceNumber = 0x0000
      bAlternateSetting = 0x0000
      bNumEndpoints = 0x0002
      bInterfaceClass = 0x0008  <Mass storage>
      bInterfaceSubClass = 0x0006  <SCSI>
      bInterfaceProtocol = 0x0050  <Bulk only>
      iInterface = 0x0000  <no string>

     Endpoint 0
        bLength = 0x0007
        bDescriptorType = 0x0005
        bEndpointAddress = 0x0081  <IN>
        bmAttributes = 0x0002  <BULK>
        wMaxPacketSize = 0x0200
        bInterval = 0x0000

     Endpoint 1
        bLength = 0x0007
        bDescriptorType = 0x0005
        bEndpointAddress = 0x0002  <OUT>
        bmAttributes = 0x0002  <BULK>
        wMaxPacketSize = 0x0200
        bInterval = 0x0000
```

Cada campo del vocabulario de la sección 1 está ahí. La clase de interfaz es 0x08 (Mass Storage). La subclase es 0x06 (SCSI). El protocolo es 0x50 (Bulk-only Transport). Hay dos endpoints. El endpoint 0 tiene la dirección 0x81 (el bit más significativo indica dirección IN; los cinco bits menos significativos son el número de endpoint, 1). El endpoint 1 tiene la dirección 0x02 (el bit más significativo está a cero, lo que indica OUT; el número de endpoint es 2). Ambos endpoints son bulk. Ambos tienen un tamaño máximo de paquete de 0x0200 = 512 bytes. El intervalo es cero porque los endpoints bulk no lo utilizan.

**Paso 4. Compáralo con `dmesg`.** Ejecuta `dmesg | grep -A 3 ugen0.2` (o consulta la salida del último arranque para el dispositivo correspondiente). Deberías ver una línea similar a:

```text
ugen0.2: <Generic Storage> at usbus0
umass0 on uhub0
umass0: <Generic Storage, class 0/0, rev 2.00/1.12, addr 2> on usbus0
```

Es la misma información, formateada por el propio sistema de registro del kernel. El driver que se enganchó es `umass`, que es el driver de almacenamiento masivo USB de FreeBSD, y se enganchó a la clase de interfaz Mass Storage.

**Paso 5. Prueba `usbconfig -d ugen0.3 dump_all_config_desc` con otro dispositivo.** Un ratón, un teclado o una unidad flash funcionarán perfectamente. Compara los tipos de endpoint: un ratón tiene un endpoint interrupt-IN; una unidad flash tiene un bulk-IN y un bulk-OUT; un teclado tiene un interrupt-IN. El patrón se cumple.

Si quieres un ejercicio adicional breve, anota los identificadores de fabricante y producto de uno de tus dispositivos. En la sección 2 se te pedirá que introduzcas identificadores de fabricante y producto en una tabla de coincidencias; usar los que tienes a mano es lo más concreto.

### Cerrando la sección 1

La sección 1 ha hecho cuatro cosas. Ha establecido el modelo mental de un transporte: el protocolo más el ciclo de vida, más las tres categorías amplias de trabajo (coincidencia, mecánica de transferencia, ciclo de vida de conexión en caliente) que un driver específico de transporte debe añadir a su base Newbus. Ha construido el modelo USB: host y dispositivo, hubs y puertos, clases, descriptores con su estructura anidada, los cuatro tipos de transferencia y el ciclo de vida de conexión en caliente. Ha construido el modelo serie: el UART como registro de desplazamiento con un generador de baudios, el enmarcado RS-232, la velocidad en baudios, la paridad y los bits de parada, el control de flujo por hardware y por software, la distinción específica de FreeBSD entre nodos callin y callout, la división entre dos mundos representada por `uart(4)` y `ucom(4)`, y el papel de `struct termios`. Y ha anclado el vocabulario en un ejercicio concreto que lee descriptores de un dispositivo USB real con `usbconfig`.

A partir de aquí, el capítulo se adentra en el código. La sección 2 construye un esqueleto de driver USB: probe, attach, detach, tabla de coincidencias y macros de registro. La sección 3 hace que ese driver realice trabajo real añadiendo transferencias. La sección 4 se vuelve hacia el lado serie, recorre el subsistema `uart(4)` con un driver real como guía y explica dónde encaja `ucom(4)`. La sección 5 devuelve el material al laboratorio y enseña cómo probar drivers USB y serie sin hardware físico. Cada sección se apoya en los modelos mentales que acaban de establecerse. Si un párrafo posterior hace referencia a un descriptor o a un tipo de transferencia y el término no te resulta inmediato, vuelve a la sección 1 para repasarlo rápidamente antes de continuar.

## Sección 2: cómo escribir un driver de dispositivo USB

### De los conceptos al código

La sección 1 construyó una imagen mental de USB: un host que se comunica con dispositivos a través de un árbol de hubs, dispositivos que se describen a sí mismos con descriptores anidados, cuatro tipos de transferencia que cubren cualquier patrón de tráfico imaginable, y un ciclo de vida de conexión en caliente que los drivers deben respetar porque los dispositivos USB aparecen y desaparecen en cualquier momento. La sección 2 convierte esos conceptos en un esqueleto de driver real. Al final de esta sección, tendrás un driver USB que compila, se carga, se engancha a un dispositivo coincidente y se desengancha limpiamente cuando el dispositivo se desconecta. Aún no realizará transferencias de datos; esa es la tarea de la sección 3. Pero el andamiaje que construyes aquí es el mismo que usa todo driver USB de FreeBSD, desde el LED de notificación más pequeño hasta el controlador de almacenamiento masivo más complejo.

La disciplina que aprendiste en el capítulo 25 sigue siendo válida. Todo recurso debe tener un propietario. Toda asignación correcta en `attach` debe ir acompañada de una liberación explícita en `detach`. Cada camino de error debe dejar el sistema en un estado limpio. La cadena de limpieza con goto etiquetado, las funciones auxiliares que devuelven errno, el seguimiento de recursos basado en softc, el registro con tasa limitada: todo ello sigue siendo aplicable. Lo que cambia es el conjunto de recursos que gestionas. En lugar de recursos de bus asignados a través de Newbus y un dispositivo de caracteres creado mediante `make_dev`, gestionarás objetos de transferencia USB asignados a través de la pila USB y, opcionalmente, una entrada en `/dev` creada mediante el framework `usb_fifo`. La forma del código es la misma. Solo cambian las llamadas concretas.

Esta sección avanza de fuera hacia dentro. Comienza explicando dónde se sitúa un driver USB dentro del subsistema USB de FreeBSD, porque situar el driver en su entorno correcto es un requisito previo para comprender cada llamada que viene a continuación. Luego cubre la tabla de coincidencias, que es la forma en que un driver USB declara qué dispositivos quiere. Recorre probe y attach, las dos mitades del punto de entrada del driver al mundo. Cubre la estructura del softc, que es donde el driver mantiene su estado por dispositivo. Presenta la cadena de limpieza, que es la forma en que el driver deshace su propio trabajo cuando se llama a `detach`. Y termina con las macros de registro que vinculan el driver al sistema de módulos del kernel.

A lo largo del camino, el capítulo utiliza `uled.c` como referencia recurrente. Es un driver real de FreeBSD, de unas trescientas líneas, ubicado en `/usr/src/sys/dev/usb/misc/uled.c`. Es lo suficientemente corto para leerlo de principio a fin en una sola sesión y lo suficientemente rico para mostrar todos los mecanismos que necesita un driver USB. Si quieres fundamentar cada idea de esta sección en código real, abre ese archivo ahora en otra ventana y mantenla abierta. Cada vez que el capítulo haga referencia a un patrón, podrás ver ese patrón en un driver funcional.

### Dónde se sitúa un driver USB en el árbol de FreeBSD

El subsistema USB de FreeBSD reside en `/usr/src/sys/dev/usb/`. Ese directorio contiene de todo: desde los drivers de controlador de host en la parte inferior (`controller/ehci.c`, `controller/xhci.c`, etc.) hasta los drivers de clase en los niveles superiores (`net/if_cdce.c`, `wlan/if_rum.c`, `input/ukbd.c`), pasando por los drivers serie (`serial/uftdi.c`, `serial/uplcom.c`) y el código genérico del framework (`usb_device.c`, `usb_transfer.c`, `usb_request.c`). Cuando se añade un nuevo driver al árbol, va a uno de estos subdirectorios según su función. Un driver para un dispositivo de LED parpadeante pertenece a `misc/`. Un driver para un adaptador de red pertenece a `net/`. Un driver para un adaptador serie pertenece a `serial/`. Para tu propio trabajo, no añadirás archivos directamente a `/usr/src/sys/dev/usb/`; construirás módulos fuera del árbol en tu propio directorio de trabajo, del mismo modo que hizo el capítulo 25. La disposición del directorio importa para leer el código fuente, no para escribirlo.

Todo driver USB de FreeBSD ocupa un lugar en una pequeña pila vertical. En la parte inferior está el driver del controlador de host, que es el que realmente se comunica con el silicio. Por encima está el framework USB, que gestiona el análisis de descriptores, la enumeración de dispositivos, la planificación de transferencias, el enrutamiento de hubs y la maquinaria genérica que todo dispositivo necesita. Por encima del framework están los drivers de clase, que son los que escribirás. Un driver de clase se engancha a una interfaz USB, no directamente al bus. Este es el punto arquitectónico más importante del capítulo.

En el árbol Newbus, la relación de enganche tiene este aspecto:

```text
nexus0
  └─ pci0
       └─ ehci0   (or xhci0, depending on the host controller)
            └─ usbus0
                 └─ uhub0   (the root hub)
                      └─ uhub1 (a downstream hub, if present)
                           └─ [class driver]
```

El driver que escribirás se engancha a `uhub`, no a `usbus`, ni a `ehci`, ni a `pci`. El framework USB recorre los descriptores del dispositivo, crea un hijo por cada interfaz y ofrece esos hijos a los drivers de clase a través del mecanismo de probe de newbus. Cuando se llama a la rutina probe de tu driver, se le está preguntando: «aquí hay una interfaz; ¿es tuya?». La tabla de coincidencias de tu driver es la forma de responder a esa pregunta.

Hay un punto sutil que conviene asimilar. Un dispositivo USB puede exponer varias interfaces simultáneamente. Un periférico multifunción (por ejemplo, un dispositivo de audio USB con auriculares y micrófono en el mismo silicio) expone una interfaz para la reproducción y otra para la captura. FreeBSD asigna a cada interfaz su propio hijo de newbus, y cada hijo puede ser reclamado por un driver diferente. Por eso los drivers USB se enganchan a nivel de interfaz: permite que el framework enrute las interfaces de forma independiente. Tu driver no debe asumir que el dispositivo tiene una sola interfaz. Cuando escribes la tabla de coincidencias, la escribes para una interfaz concreta, identificada por su clase, subclase, protocolo, o bien por su par fabricante/producto más un número de interfaz opcional.

### La tabla de coincidencias: decirle al kernel qué dispositivos son tuyos

Un driver USB anuncia qué dispositivos aceptará mediante un array de entradas `STRUCT_USB_HOST_ID`. Esto es análogo a la tabla de coincidencias PCI del capítulo 23, pero con campos específicos de USB. La definición canónica se encuentra en `/usr/src/sys/dev/usb/usbdi.h`. Cada entrada especifica uno o varios de los siguientes campos: un ID de fabricante, un ID de producto, una tripleta clase/subclase/protocolo de dispositivo, una tripleta clase/subclase/protocolo de interfaz, o un rango bcdDevice definido por el fabricante. Puedes hacer coincidir de forma amplia (cualquier dispositivo que anuncie la clase de interfaz 0x03, que es HID) o de forma estrecha (el único dispositivo con fabricante 0x0403 y producto 0x6001, que es un FTDI FT232). La mayoría de los drivers hacen coincidir de forma estrecha, porque la mayoría de los dispositivos reales tienen peculiaridades específicas del driver que solo se aplican a determinadas revisiones de hardware.

El framework proporciona macros de conveniencia para construir entradas de coincidencia sin tener que inicializar cada campo a mano. Las más habituales son `USB_VPI(vendor, product, info)` para pares fabricante/producto con un campo de información opcional específico del driver, y la forma más extensa en la que se rellenan los indicadores `mfl_`, `pfl_`, `dcl_`, `dcsl_`, `dcpl_`, `icl_`, `icsl_`, `icpl_` para indicar qué campos son significativos. Por claridad y mantenibilidad, los drivers escritos hoy en día tienden a usar las macros compactas siempre que sean aplicables.

Así es como `uled.c` declara su tabla de coincidencias. El código fuente está en `/usr/src/sys/dev/usb/misc/uled.c`:

```c
static const STRUCT_USB_HOST_ID uled_devs[] = {
    {USB_VPI(USB_VENDOR_DREAMCHEEKY, USB_PRODUCT_DREAMCHEEKY_WEBMAIL_NOTIFIER, 0)},
    {USB_VPI(USB_VENDOR_RISO_KAGAKU, USB_PRODUCT_RISO_KAGAKU_WEBMAIL_NOTIFIER, 0)},
};
```

Dos entradas, cada una identificando un par proveedor/producto específico. El tercer argumento de `USB_VPI` es un entero sin signo que el driver puede usar para distinguir variantes en el momento del probe; `uled` lo establece a cero porque ambos dispositivos se comportan de la misma manera. Los nombres simbólicos de proveedor y producto se resuelven en identificadores numéricos definidos en `/usr/src/sys/dev/usb/usbdevs.h`, que es una tabla extensa generada a partir de `/usr/src/sys/dev/usb/usbdevs`. Añadir una nueva entrada de coincidencia para tu propio hardware de desarrollo suele implicar agregar una línea a `usbdevs` y regenerar la cabecera, o bien prescindir por completo de los nombres simbólicos y escribir los valores hexadecimales directamente en la tabla de coincidencia.

Para tu propio driver fuera del árbol de código fuente, no necesitas tocar `usbdevs` en absoluto. Puedes escribir:

```c
static const STRUCT_USB_HOST_ID myfirst_usb_devs[] = {
    {USB_VPI(0x16c0, 0x05dc, 0)},  /* VOTI / generic test VID/PID */
};
```

La forma numérica es perfectamente válida. Úsala cuando estés haciendo un prototipo con un dispositivo concreto y todavía no quieras proponer añadidos al archivo `usbdevs` del upstream.

Hay un detalle importante sobre las tablas de coincidencia: el tipo `STRUCT_USB_HOST_ID` incluye un byte de flags que registra qué campos son significativos. Cuando usas `USB_VPI`, la macro rellena esos flags por ti. Si construyes una entrada manualmente con llaves literales, debes rellenar los flags tú mismo, porque un byte de flags con valor cero significa «coincide con cualquier cosa», y raramente querrás eso. Prefiere las macros.

La tabla de coincidencia es datos simples. No asigna memoria, no accede al hardware y no depende de ningún estado por dispositivo. Se carga en el kernel junto con el módulo y el framework la utiliza cada vez que se enumera un nuevo dispositivo USB.

### El método `probe`

El framework USB invoca el método `probe` de un driver una vez por interfaz cuando se le presenta un candidato con posibilidad de coincidencia. El objetivo de `probe` es responder a una única pregunta: «¿Debe este driver hacer attach a esta interfaz?». El método no debe tocar el hardware. No debe asignar recursos. No debe bloquearse. Todo lo que hace es examinar el argumento de attach del USB, compararlo con la tabla de coincidencias y devolver un valor de bus-probe (que indica una coincidencia con su prioridad asociada) o `ENXIO` (que indica que este driver no quiere esa interfaz).

El argumento de attach reside en una estructura llamada `struct usb_attach_arg`, definida en `/usr/src/sys/dev/usb/usbdi.h`. Contiene el vendor ID, el product ID, el descriptor del dispositivo, el descriptor de interfaz y varios campos auxiliares. Newbus permite que un driver lo recupere mediante `device_get_ivars(dev)`. Para los drivers USB, el framework proporciona un wrapper llamado `usbd_lookup_id_by_uaa` que recibe una tabla de coincidencias y un argumento de attach, y devuelve cero si hay coincidencia o un errno distinto de cero si no la hay. Este wrapper encapsula todos los casos que el driver necesita manejar: coincidencia por vendor/product, por class/subclass/protocol, la lógica de los bytes de flags y el despacho a nivel de interfaz.

Un método probe completo para nuestro ejemplo de referencia tiene este aspecto:

```c
static int
myfirst_usb_probe(device_t dev)
{
    struct usb_attach_arg *uaa = device_get_ivars(dev);

    if (uaa->usb_mode != USB_MODE_HOST)
        return (ENXIO);

    if (uaa->info.bConfigIndex != 0)
        return (ENXIO);

    if (uaa->info.bIfaceIndex != 0)
        return (ENXIO);

    return (usbd_lookup_id_by_uaa(myfirst_usb_devs,
        sizeof(myfirst_usb_devs), uaa));
}
```

Las tres cláusulas de guardia al inicio de la función merecen una explicación detallada, porque reflejan una higiene estándar en los drivers USB.

La primera guardia rechaza el caso en que la pila USB actúa como dispositivo en lugar de como host. La pila USB de FreeBSD puede operar en modo USB-on-the-Go, en el que la propia máquina se presenta como periférico USB ante otro host. La mayoría de los drivers son drivers del lado host y no tienen ningún comportamiento significativo en modo dispositivo, por lo que lo rechazan de inmediato.

La segunda guardia rechaza configuraciones distintas del índice cero. Los dispositivos USB pueden exponer múltiples configuraciones, y un driver suele apuntar a una configuración específica. Restringir probe al índice de configuración cero mantiene la lógica simple para el caso habitual.

La tercera guardia rechaza interfaces distintas del índice cero. Si el dispositivo tiene múltiples interfaces y estás escribiendo un driver para la primera, esta cláusula es la que garantiza que el framework no te ofrezca por error las demás interfaces.

Tras las guardias, la llamada a `usbd_lookup_id_by_uaa` realiza el trabajo real de coincidencia. Si el vendor, product, class, subclass o protocol del dispositivo coincide con alguna entrada de la tabla, la función devuelve cero y el método probe devuelve cero, que el framework USB interpreta como «este driver quiere este dispositivo». Devolver `ENXIO` indica al framework que pruebe con otro driver candidato. Si ningún candidato quiere el dispositivo, acaba siendo adjuntado a `ugen`, el driver USB genérico, que expone los descriptores en bruto y las transferencias a través de nodos `/dev/ugenN.M` pero no proporciona ningún comportamiento específico del dispositivo.

Vale la pena señalar un punto sutil: `probe` devuelve cero en caso de coincidencia en lugar de un valor positivo de bus-probe. Otros frameworks de bus de FreeBSD usan valores positivos como `BUS_PROBE_DEFAULT` para indicar prioridad, pero para USB la convención es cero para coincidencia y un errno distinto de cero para no coincidencia. El framework gestiona la prioridad mediante el orden de despacho, no a través de los valores de retorno del probe.

### El método `attach`

Una vez que `probe` informa de una coincidencia, el framework llama a `attach`. Aquí es donde el driver hace el trabajo real: asignar su softc, registrar el puntero al dispositivo padre, bloquear la interfaz, configurar los canales de transferencia (tratados en la sección 3), crear una entrada en `/dev` si el driver está orientado al usuario, y registrar un breve mensaje informativo. Cada asignación y registro en `attach` debe tener su contrapartida simétrica en `detach`, y dado que cualquier paso puede fallar, la función debe tener una ruta de limpieza clara desde cada punto de fallo.

Un método attach mínimo tiene este aspecto:

```c
static int
myfirst_usb_attach(device_t dev)
{
    struct usb_attach_arg *uaa = device_get_ivars(dev);
    struct myfirst_usb_softc *sc = device_get_softc(dev);
    int error;

    device_set_usb_desc(dev);

    mtx_init(&sc->sc_mtx, "myfirst_usb", NULL, MTX_DEF);

    sc->sc_udev = uaa->device;
    sc->sc_iface_index = uaa->info.bIfaceIndex;

    error = usbd_transfer_setup(uaa->device, &sc->sc_iface_index,
        sc->sc_xfer, myfirst_usb_config, MYFIRST_USB_N_XFER,
        sc, &sc->sc_mtx);
    if (error != 0) {
        device_printf(dev, "usbd_transfer_setup failed: %d\n", error);
        goto fail_mtx;
    }

    sc->sc_dev = make_dev(&myfirst_usb_cdevsw, device_get_unit(dev),
        UID_ROOT, GID_WHEEL, 0644, "myfirst_usb%d", device_get_unit(dev));
    if (sc->sc_dev == NULL) {
        device_printf(dev, "make_dev failed\n");
        error = ENOMEM;
        goto fail_xfer;
    }
    sc->sc_dev->si_drv1 = sc;

    device_printf(dev, "attached\n");
    return (0);

fail_xfer:
    usbd_transfer_unsetup(sc->sc_xfer, MYFIRST_USB_N_XFER);
fail_mtx:
    mtx_destroy(&sc->sc_mtx);
    return (error);
}
```

Lee esta función de arriba a abajo. Cada bloque hace una sola cosa.

La llamada a `device_set_usb_desc` rellena la cadena de descripción del dispositivo Newbus a partir de los descriptores USB. Tras esta llamada, los mensajes de `device_printf` incluirán las cadenas del fabricante y del producto leídas del propio dispositivo, lo que hace que los logs sean mucho más informativos.

La llamada a `mtx_init` crea un mutex que protegerá el estado por dispositivo. Cada callback de transferencia USB se ejecuta bajo este mutex (el framework lo adquiere por ti antes de invocar el callback), por lo que todo lo que toca el callback debe estar serializado por él. El capítulo 25 presentó los mutex; el uso aquí es el mismo.

Las dos asignaciones `sc->sc_` almacenan en caché dos punteros que el resto del driver necesitará. `sc->sc_udev` es el `struct usb_device *` que el driver usa al emitir peticiones USB. `sc->sc_iface_index` identifica el índice de interfaz al que este driver se adjuntó, de modo que las llamadas posteriores de configuración de transferencias apunten a la interfaz correcta.

La llamada a `usbd_transfer_setup` es la operación individual más importante en `attach`. Asigna y configura todos los objetos de transferencia que el driver utilizará, basándose en un array de configuración (`myfirst_usb_config`) que la sección 3 examinará en detalle. Si esta llamada falla, el driver aún no ha asignado nada excepto el mutex, por lo que la ruta de limpieza va a `fail_mtx` y destruye el mutex.

La llamada a `make_dev` crea el nodo `/dev` visible para el usuario. El patrón del capítulo 25 se aplica aquí: establece `si_drv1` en el cdev para que los manejadores del cdevsw puedan recuperar el softc a través de `dev->si_drv1`. Si esta llamada falla, la ruta de limpieza va a `fail_xfer`, que también ejecuta el desmontaje de las transferencias antes de destruir el mutex.

El `return (0)` al final del camino feliz es el contrato con el framework: un retorno de cero significa que el dispositivo está adjuntado y el driver está listo.

Las dos etiquetas al final implementan la cadena de limpieza con goto etiquetado del capítulo 25. Cada etiqueta corresponde al estado que ha alcanzado el driver en el momento del fallo, y la caída a través de la limpieza ejecuta exactamente los pasos de desmontaje necesarios para deshacer el trabajo realizado hasta ese momento. Cuando leas un driver de FreeBSD y veas este patrón, estás ante la misma disciplina que practicaste en el capítulo 25, aplicada a un nuevo conjunto de recursos.

Un detalle importante sobre el framework USB que el capítulo 25 no necesitaba cubrir: si miras `uled.c` u otro driver USB real, verás a veces que `usbd_transfer_setup` acepta un puntero al índice de interfaz en lugar de un entero. El framework puede modificar ese puntero en el caso de interfaces virtuales o multiplexadas; pásalo por dirección, no por valor. El esqueleto anterior hace esto correctamente.

### El softc: estado por dispositivo

El softc de un driver USB es una estructura C simple almacenada como los datos del driver Newbus para cada dispositivo adjuntado. El framework la asigna automáticamente en función del tamaño declarado en el descriptor del driver, y es el lugar donde reside todo el estado mutable por dispositivo. Para nuestro ejemplo de referencia, el softc tiene este aspecto:

```c
struct myfirst_usb_softc {
    struct usb_device *sc_udev;
    struct mtx         sc_mtx;
    struct usb_xfer   *sc_xfer[MYFIRST_USB_N_XFER];
    struct cdev       *sc_dev;
    uint8_t            sc_iface_index;
    uint8_t            sc_flags;
#define MYFIRST_USB_FLAG_OPEN       0x01
#define MYFIRST_USB_FLAG_DETACHING  0x02
};
```

Recorramos cada miembro.

`sc_udev` es el puntero opaco que el framework USB usa para identificar el dispositivo. Toda llamada USB que actúe sobre el dispositivo recibe este puntero.

`sc_mtx` es el mutex por dispositivo que protege el propio softc y cualquier estado compartido que le importe al driver. El mutex debe adquirirse antes de tocar cualquier campo que un callback de transferencia también pueda tocar, y el callback de transferencia siempre se ejecuta con este mutex mantenido (el framework gestiona el bloqueo por ti cuando invoca el callback).

`sc_xfer[]` es un array de objetos de transferencia, uno por canal que utiliza el driver. Su tamaño es una constante en tiempo de compilación. La sección 3 explicará cómo cada entrada de este array se configura mediante el array de configuración pasado a `usbd_transfer_setup`.

`sc_dev` es la entrada del dispositivo de caracteres, si el driver expone un nodo visible al usuario. Para los drivers que no exponen un nodo `/dev` (algunos drivers solo exportan datos a través de `sysctl` o eventos `devctl`), este campo puede omitirse.

`sc_iface_index` registra a qué interfaz del dispositivo USB se adjuntó este driver. Lo utiliza la configuración de transferencias y, en drivers de múltiples interfaces, como discriminador en el registro de mensajes.

`sc_flags` es un vector de bits para el estado privado del driver. Aquí se declaran dos flags: `MYFIRST_USB_FLAG_OPEN` se activa mientras un proceso del userland mantiene el dispositivo abierto, y `MYFIRST_USB_FLAG_DETACHING` se activa al inicio de `detach` para que cualquier ruta de E/S concurrente pueda ver que debe abortar rápidamente. Esta es una aplicación de un patrón estándar: activar un flag bajo el mutex al inicio de detach, de modo que cualquier otro hilo que despierte lo vea y salga.

Los drivers reales suelen tener muchos más campos: buffers por transferencia, colas de peticiones, máquinas de estado entre callbacks, temporizadores, etc. Añades al softc a medida que el driver crece. El principio rector es que cualquier estado que persista entre llamadas a funciones y no sea global al módulo pertenece al softc.

### El método `detach`

Cuando se desconecta un dispositivo, cuando se descarga el módulo o cuando el userspace usa `devctl detach`, el framework llama al método `detach` del driver. La tarea del driver es liberar todos los recursos que asignó en `attach`, cancelar cualquier trabajo en curso, asegurarse de que no haya ningún callback en ejecución y devolver cero. Si `detach` devuelve un error, el framework trata el dispositivo como si siguiera adjuntado, lo que puede crear problemas si el hardware ya ha desaparecido físicamente. La mayoría de los drivers devuelven cero de forma incondicional, o solo devuelven un error en casos muy específicos de «dispositivo ocupado» en los que el driver implementa su propio recuento de referencias para los manejadores del userspace.

El método detach para nuestro ejemplo de referencia es la limpieza simétrica del método attach:

```c
static int
myfirst_usb_detach(device_t dev)
{
    struct myfirst_usb_softc *sc = device_get_softc(dev);

    mtx_lock(&sc->sc_mtx);
    sc->sc_flags |= MYFIRST_USB_FLAG_DETACHING;
    mtx_unlock(&sc->sc_mtx);

    if (sc->sc_dev != NULL) {
        destroy_dev(sc->sc_dev);
        sc->sc_dev = NULL;
    }

    usbd_transfer_unsetup(sc->sc_xfer, MYFIRST_USB_N_XFER);

    mtx_destroy(&sc->sc_mtx);

    return (0);
}
```

El primer bloque activa el flag de detaching bajo el mutex. Si otro thread está a punto de adquirir el mutex e iniciar una nueva transferencia, verá el flag y se negará. La llamada a `destroy_dev` elimina la entrada `/dev`; una vez que retorna, no pueden llegar nuevas llamadas de apertura. La llamada a `usbd_transfer_unsetup` cancela las transferencias en curso y espera a que sus callbacks terminen; una vez que retorna, ningún callback de transferencia puede seguir ejecutándose. Sin nuevas aperturas y sin callbacks en ejecución, es seguro destruir el mutex.

Hay una sutileza que los programadores noveles del kernel a veces no perciben: el orden importa. Destruir la entrada `/dev` antes de deshacer las transferencias garantiza que no pueda comenzar ninguna nueva operación del usuario, pero no detiene las transferencias que ya estaban en curso cuando se llamó a detach. Ese es el trabajo de `usbd_transfer_unsetup`. Ambos pasos son necesarios, y el orden (primero el cdev, luego las transferencias, luego el mutex) es el correcto porque cada paso posterior depende de que no lleguen nuevos trabajos durante él.

Un punto más sobre detach y la concurrencia. El framework garantiza que ningún probe, attach o detach se ejecute de forma concurrente con otro probe, attach o detach en el mismo dispositivo. Pero los callbacks de transferencia se ejecutan por su propio camino y pueden estar en curso en el momento exacto en que se llama a detach. La combinación del flag de detaching y `usbd_transfer_unsetup` es lo que hace esto seguro. Si añades nuevos recursos a tu driver, debes añadir una limpieza simétrica que tenga en cuenta esta concurrencia.

### Macros de registro

Todo driver de FreeBSD necesita registrarse con el kernel para que este sepa cuándo llamar a sus rutinas probe, attach y detach. Los drivers USB utilizan un pequeño conjunto de macros que unen todo en un módulo del kernel. Las macros van al final del archivo del driver y parecen intimidantes al principio, pero son completamente mecánicas una vez que sabes qué hace cada línea.

```c
static device_method_t myfirst_usb_methods[] = {
    DEVMETHOD(device_probe,  myfirst_usb_probe),
    DEVMETHOD(device_attach, myfirst_usb_attach),
    DEVMETHOD(device_detach, myfirst_usb_detach),
    DEVMETHOD_END
};

static driver_t myfirst_usb_driver = {
    .name    = "myfirst_usb",
    .methods = myfirst_usb_methods,
    .size    = sizeof(struct myfirst_usb_softc),
};

DRIVER_MODULE(myfirst_usb, uhub, myfirst_usb_driver, NULL, NULL);
MODULE_DEPEND(myfirst_usb, usb, 1, 1, 1);
MODULE_VERSION(myfirst_usb, 1);
USB_PNP_HOST_INFO(myfirst_usb_devs);
```

Leamos cada bloque.

El array `device_method_t` lista los métodos que proporciona el driver. Para un driver USB que no implementa hijos newbus adicionales, las tres entradas mostradas son suficientes: probe, attach, detach. Los drivers más complejos podrían añadir `device_suspend`, `device_resume` o `device_shutdown`, pero para la gran mayoría de los drivers USB las tres entradas básicas son todo lo que se necesita. `DEVMETHOD_END` termina el array; el framework lo requiere.

La estructura `driver_t` vincula el array de métodos a un nombre legible por humanos y declara el tamaño del softc. El nombre se utiliza en los registros del kernel y por `devctl`. El tamaño del softc indica a Newbus cuánta memoria debe asignar por dispositivo.

La macro `DRIVER_MODULE` registra el driver en el kernel. Los argumentos son, en orden: el nombre del módulo, el nombre del bus padre (siempre `uhub` para drivers de clase USB), la estructura del driver y dos hooks opcionales para eventos. Los hooks de eventos raramente son necesarios y suelen ser `NULL`.

La macro `MODULE_DEPEND` declara que este módulo necesita que `usb` esté cargado previamente. Los tres números son las versiones mínima, preferida y máxima compatibles del módulo `usb`. Para la mayoría de los drivers, `1, 1, 1` es lo correcto: el framework USB ha versionado su interfaz en 1 durante mucho tiempo, y sería inusual necesitar otra cosa.

La macro `MODULE_VERSION` declara el número de versión propio de este módulo. Otros módulos que quieran depender de `myfirst_usb` harían referencia al número que declares aquí.

La macro `USB_PNP_HOST_INFO` es la última pieza. Exporta la tabla de coincidencias a un formato que el daemon `devd(8)` puede leer, de modo que cuando se conecta un dispositivo USB compatible, el espacio de usuario puede cargar el módulo automáticamente. Esta macro es una incorporación relativamente reciente a FreeBSD; los drivers más antiguos puede que no la incluyan. Se recomienda encarecidamente incluirla en cualquier driver que quiera participar en el sistema plug-and-play USB de FreeBSD.

En conjunto, estas cinco declaraciones convierten tu archivo de driver en un módulo del kernel cargable. Una vez que el archivo se compila con un `Makefile` que usa `bsd.kmod.mk`, ejecutar `kldload myfirst_usb.ko` vinculará el driver al kernel, y cualquier dispositivo compatible que se conecte a continuación activará tus rutinas probe y attach.

### El ciclo de vida del hot-plug, revisitado en código

La sección 1 introdujo el ciclo de vida del hot-plug a nivel de modelo mental: un dispositivo aparece, el framework lo enumera, tu driver se conecta, el userland interactúa con él, el dispositivo desaparece, el framework invoca detach, y tu driver limpia. Con el código delante, esa narrativa tiene ahora una secuencia concreta:

1. El usuario conecta un dispositivo que coincide con la tabla.
2. El framework USB enumera el dispositivo, lee todos sus descriptores y decide qué interfaces ofrecer a qué drivers.
3. Para cada interfaz que coincide con la tabla de coincidencias del driver, el framework crea un hijo Newbus y llama al método `probe`.
4. Tu método `probe` devuelve cero.
5. El framework llama a tu método `attach`. Inicializas el softc, configuras las transferencias, creas el nodo `/dev` y devuelves cero.
6. El userland abre el nodo `/dev` y comienza a emitir operaciones de I/O. Los callbacks de transferencia de la sección 3 empiezan a ejecutarse.
7. El usuario desconecta el dispositivo.
8. El framework llama a tu método `detach`. Estableces el flag de desconexión, destruyes el nodo `/dev`, llamas a `usbd_transfer_unsetup` para cancelar todas las transferencias en vuelo y esperar a que los callbacks terminen, destruyes el mutex y devuelves cero.
9. El framework libera el softc y elimina el hijo Newbus.

En cada paso, el framework gestiona las partes que no tienes que escribir tú mismo. Tu responsabilidad es limitada: reaccionar correctamente a probe, attach y detach, y ejecutar callbacks de transferencia que respeten la máquina de estados. La maquinaria que te rodea se encarga de la enumeración, el arbitraje del bus, la planificación de transferencias, el enrutamiento por el hub y los decenas de casos extremos que impone la capa USB.

El ciclo de vida tiene un matiz sutil más que merece la pena mencionar. Entre el momento en que el usuario desconecta el dispositivo y el momento en que el framework invoca `detach`, hay una breve ventana en la que cualquier transferencia en vuelo recibe un error especial: `USB_ERR_CANCELLED`. El propio framework de transferencias genera este error cuando desmonta las transferencias como respuesta a la desconexión. La sección 3 explicará cómo gestionar este error en la máquina de estados del callback. Por ahora, basta con saber que existe y que es la señal normal que recibe el driver para indicar que el dispositivo está desapareciendo.

### Cerrando la sección 2

La sección 2 te ha proporcionado un esqueleto completo de driver USB. El esqueleto todavía no mueve datos; ese es el tema de la sección 3. Pero el resto del driver ya está en su lugar: la tabla de coincidencias, el método probe, el método attach, el softc, el método detach y los macros de registro. Has visto cómo el framework USB enruta un dispositivo recién enumerado a través de tu rutina probe, cómo tu rutina attach toma posesión y establece el estado, cómo el driver se integra con Newbus mediante `device_get_ivars` y `device_get_softc`, y cómo la rutina detach recorre los pasos de asignación en orden inverso para dejar el sistema limpio.

Dos temas del capítulo 25 se han extendido de forma natural al territorio USB. El primero es la cadena de limpieza con goto etiquetado. Cada recurso que adquieres tiene su propia etiqueta, y cada camino de error pasa por la secuencia exacta de llamadas de limpieza. Cuando compares `myfirst_usb_attach` con las funciones attach de `uled.c`, `ugold.c` o `uftdi.c`, verás el mismo patrón repetido. El segundo es la disciplina de mantener una única fuente de verdad para el estado en el softc. Cada campo tiene un propietario, un ciclo de vida y un lugar claro donde se inicializa y se destruye. Estos hábitos son los que hacen que un driver sea legible, portable y mantenible.

La sección 3 le dará voz a este esqueleto. Los canales de transferencia se declararán en un array de configuración. El framework USB asignará los buffers subyacentes y planificará las transacciones. Un callback se activará cada vez que una transferencia se complete o necesite más datos, y usará una máquina de estados de tres estados para decidir qué hacer. Se aplicará la misma disciplina que acabas de aprender, pero la nueva preocupación es el pipeline de datos en sí: cómo se mueven los bytes entre el driver y el dispositivo.

### Lectura de `uled.c` como ejemplo completo

Antes de adentrarnos en las transferencias, merece la pena detenerse a leer el ejemplo canónico de driver pequeño de principio a fin. El archivo `/usr/src/sys/dev/usb/misc/uled.c` tiene aproximadamente trescientas líneas de C e implementa un driver para los LEDs notificadores de correo web USB de Dream Cheeky y Riso Kagaku: pequeños gadgets USB con tres LEDs de colores que un programa del sistema anfitrión puede encender. El driver es lo suficientemente corto como para retenerlo en la memoria, es autocontenido y ejercita todos los patrones que hemos comentado.

Al abrir el archivo, el primer bloque que encuentras es el conjunto estándar de inclusiones de cabeceras. Un driver USB incorpora cabeceras de varias capas: `sys/param.h`, `sys/systm.h`, `sys/bus.h` para los fundamentos; `sys/module.h` para `MODULE_VERSION` y `MODULE_DEPEND`; las cabeceras USB bajo `dev/usb/` para el framework; y `usbdevs.h` para las constantes simbólicas de fabricante y producto. Ten en cuenta que `usbdevs.h` no es una cabecera mantenida manualmente: se genera durante el build a partir del archivo de texto `/usr/src/sys/dev/usb/usbdevs` cuando se compila el kernel o el módulo, de modo que las constantes que expone reflejan las entradas que contenga actualmente el archivo `usbdevs` del árbol de código fuente. `uled.c` también incorpora `sys/conf.h` y archivos relacionados porque crea un dispositivo de caracteres.

El segundo bloque es la declaración del softc. `uled` guarda su estado en una estructura que contiene el puntero al dispositivo, un mutex, un array de dos punteros de transferencia (uno para control, uno para datos), un puntero al dispositivo de caracteres, un puntero al estado del callback y un pequeño byte de "color" que registra el color actual del LED. El softc es directo: cada campo es privado, cada asignación tiene un lugar donde se realiza y un lugar donde se libera.

El tercer bloque es la tabla de coincidencias. `uled` admite dos fabricantes (Dream Cheeky y Riso Kagaku) con un ID de producto cada uno. El macro `USB_VPI` rellena el byte de flags para una coincidencia de fabricante más producto. La tabla tiene dos entradas, plana y simple.

El cuarto bloque es el array de configuración de transferencias. `uled` declara dos canales: un canal de control de salida que se usa para enviar peticiones SET_REPORT al dispositivo (que es como se programa realmente el color del LED) y un canal de interrupción de entrada que lee paquetes de estado del LED. El canal de control tiene `type = UE_CONTROL` y un tamaño de buffer suficiente para contener el paquete de configuración más el payload. El canal de interrupción tiene `type = UE_INTERRUPT`, `direction = UE_DIR_IN` y un tamaño de buffer que coincide con el tamaño del informe del LED.

El quinto bloque son las funciones callback. El callback de control sigue la máquina de tres estados que viste en la sección 3: en `USB_ST_SETUP`, construye un paquete de configuración y un payload de informe HID de ocho bytes, envía la transferencia y retorna. En `USB_ST_TRANSFERRED`, despierta a cualquier escritor del userland que estuviera esperando a que se completase el cambio de color. En el caso por defecto (errores), gestiona la cancelación de forma limpia y reintenta ante otros errores.

El callback de interrupción es similar pero sin la complicación del paquete de configuración. Lee un informe de estado de ocho bytes, comprueba si indica una pulsación de botón (los dispositivos Riso Kagaku tienen un botón opcional) y se rearma.

El sexto bloque son los métodos del dispositivo de caracteres. `uled` expone una entrada `/dev/uled0` que acepta llamadas `write(2)` con un payload de tres bytes (rojo, verde, azul). El manejador `d_write` copia los tres bytes en el softc, inicia la transferencia de control y retorna. Cuando la transferencia se completa, el color queda realmente programado. El manejador `d_read` no está implementado (los LEDs no tienen un estado significativo que leer), por lo que las lecturas devuelven cero.

El séptimo bloque son los métodos Newbus: probe, attach, detach. El método probe utiliza `usbd_lookup_id_by_uaa` exactamente como se mostró en la sección 2. El método attach llama a `device_set_usb_desc`, inicializa el mutex, llama a `usbd_transfer_setup` con el array de configuración y crea el dispositivo de caracteres. El método detach realiza estos pasos en orden inverso.

El octavo bloque son los macros de registro. `DRIVER_MODULE(uled, uhub, ...)`, `MODULE_DEPEND(uled, usb, 1, 1, 1)`, `MODULE_VERSION(uled, 1)` y `USB_PNP_HOST_INFO(uled_devs)`. Exactamente la secuencia que aprendiste.

Al leer `uled.c` con el vocabulario de la sección 2 en la mano, todo el archivo se mapea de forma legible sobre los patrones que ahora entiendes. Cada decisión estructural que toma el driver tiene un nombre. Cada línea de código es una instancia de un patrón general. Esta es la clase de claridad que hace que los drivers de FreeBSD sean legibles.

Antes de continuar con la sección 3, te recomendamos que abras `uled.c` ahora mismo y lo leas. Aunque algunas líneas te resulten todavía oscuras, la estructura general coincidirá con el modelo mental que has construido. Los detalles cobrarán más sentido a medida que avances por el resto del capítulo, y volver a este archivo después de terminarlo es una excelente forma de consolidar el material.

## Sección 3: Realizar transferencias de datos USB

### El array de configuración de transferencias

Un driver USB declara sus transferencias de antemano, en tiempo de compilación, mediante un pequeño array de entradas `struct usb_config`. Cada entrada describe un canal de transferencia: su tipo (control, bulk, interrupción o isócrono), su dirección (entrada o salida), qué endpoint tiene como objetivo, el tamaño de su buffer, los flags que se aplican y qué función callback se invoca cuando la transferencia se completa. El framework lee este array una sola vez, durante `attach`, cuando el driver llama a `usbd_transfer_setup`. A partir de ese momento, cada canal se comporta como una pequeña máquina de estados que el driver controla a través de su callback.

El array de configuración es declarativo. No estás programando la secuencia de operaciones de hardware; le estás indicando al framework qué canales usará tu driver, y el framework construye la infraestructura necesaria para darles soporte. Se trata de una abstracción eficaz, y es una de las razones por las que los drivers USB en FreeBSD suelen ser mucho más cortos que los drivers equivalentes para buses como PCI que exigen manipulación directa de registros.

Para nuestro ejemplo en curso, declararemos tres canales. Un canal bulk-IN para leer datos del dispositivo, un canal bulk-OUT para escribir datos en el dispositivo y un canal interrupt-IN para recibir eventos de estado asíncronos. Un driver real para un adaptador serie o un notificador LED puede usar uno o dos de estos; usamos tres para mostrar el patrón aplicado a distintos tipos de transferencia.

```c
enum {
    MYFIRST_USB_BULK_DT_RD,
    MYFIRST_USB_BULK_DT_WR,
    MYFIRST_USB_INTR_DT_RD,
    MYFIRST_USB_N_XFER,
};

static const struct usb_config myfirst_usb_config[MYFIRST_USB_N_XFER] = {
    [MYFIRST_USB_BULK_DT_RD] = {
        .type      = UE_BULK,
        .endpoint  = UE_ADDR_ANY,
        .direction = UE_DIR_IN,
        .bufsize   = 512,
        .flags     = { .pipe_bof = 1, .short_xfer_ok = 1 },
        .callback  = &myfirst_usb_bulk_read_callback,
    },
    [MYFIRST_USB_BULK_DT_WR] = {
        .type      = UE_BULK,
        .endpoint  = UE_ADDR_ANY,
        .direction = UE_DIR_OUT,
        .bufsize   = 512,
        .flags     = { .pipe_bof = 1, .force_short_xfer = 0 },
        .callback  = &myfirst_usb_bulk_write_callback,
        .timeout   = 5000,
    },
    [MYFIRST_USB_INTR_DT_RD] = {
        .type      = UE_INTERRUPT,
        .endpoint  = UE_ADDR_ANY,
        .direction = UE_DIR_IN,
        .bufsize   = 16,
        .flags     = { .pipe_bof = 1, .short_xfer_ok = 1 },
        .callback  = &myfirst_usb_intr_callback,
    },
};
```

La enumeración al principio le da un nombre a cada canal y define `MYFIRST_USB_N_XFER` como el recuento total. Este es un modismo habitual; mantiene los canales accesibles de forma simbólica y facilita añadir un nuevo canal más adelante. `MYFIRST_USB_N_XFER` es lo que pasas a `usbd_transfer_setup`, a `usbd_transfer_unsetup` y a la declaración del array `sc_xfer[]` del softc.

El propio array usa inicializadores designados, lo que mantiene explícita la asignación de cada canal a su índice de enumeración. Recorramos los campos.

`type` es uno de `UE_CONTROL`, `UE_BULK`, `UE_INTERRUPT` o `UE_ISOCHRONOUS`, definidos en `/usr/src/sys/dev/usb/usb.h`. Debe coincidir con el tipo del endpoint tal como se declara en los descriptores USB. Si especificas `UE_BULK` pero el dispositivo tiene un endpoint de interrupción, `usbd_transfer_setup` fallará.

`endpoint` identifica el número de endpoint, pero en la mayoría de los drivers se usa el valor especial `UE_ADDR_ANY`, que indica al framework que elija cualquier endpoint cuyo tipo y dirección coincidan. Esto funciona porque la mayoría de las interfaces USB tienen un único endpoint de cada par (tipo, dirección), de modo que "cualquiera" no resulta ambiguo. Un dispositivo con varios endpoints bulk-in requeriría direcciones de endpoint explícitas.

`direction` es `UE_DIR_IN` o `UE_DIR_OUT`. De nuevo, debe coincidir con los descriptores.

`bufsize` es el tamaño del buffer que el framework asigna para este canal. Para transferencias bulk, 512 bytes es una elección habitual porque ese es el tamaño máximo de paquete para endpoints bulk de alta velocidad, de modo que un único buffer de 512 bytes puede contener exactamente un paquete. Se admiten buffers más grandes, pero para la mayoría de los casos 512 o un pequeño múltiplo es suficiente. Para los endpoints de interrupción, el buffer puede ser más pequeño porque los paquetes de interrupción suelen tener ocho, dieciséis o sesenta y cuatro bytes.

`flags` es una estructura de bits (cada flag es un entero de un bit). Los flags determinan cómo gestiona el framework las transferencias cortas, los stalls, los timeouts y el comportamiento de los pipes.

- `pipe_bof` (pipe bloqueado ante fallo): si la transferencia falla, bloquea nuevas transferencias en el mismo pipe hasta que el driver lo reinicie explícitamente. Normalmente se activa tanto para los endpoints de lectura como de escritura.
- `short_xfer_ok`: para las transferencias entrantes, trata una transferencia que se completa con menos datos de los solicitados como éxito en lugar de error. Activar este flag es lo que permite a un canal bulk-IN leer respuestas de longitud variable desde el dispositivo.
- `force_short_xfer`: para las transferencias salientes, finaliza la transferencia con un paquete corto incluso cuando los datos están alineados con el límite de un paquete completo. Algunos protocolos utilizan esto para señalizar el final de un mensaje.
- Otros flags adicionales controlan comportamientos más avanzados; para la mayoría de los drivers, `pipe_bof` junto con `short_xfer_ok` (en lecturas) y posiblemente `force_short_xfer` (en escrituras, según el protocolo) es todo lo que se necesita.

`callback` es la función a la que llama el framework cada vez que este canal requiere atención. El callback es de tipo `usb_callback_t`, que recibe un puntero a la `struct usb_xfer` y devuelve void. Toda la lógica de la máquina de estados del canal reside dentro del callback.

`timeout` (en milisegundos) establece un límite superior sobre cuánto tiempo puede esperar una transferencia antes de completarse forzosamente con un error. Definir un timeout es útil en los canales de escritura, porque evita que un dispositivo bloqueado detenga indefinidamente el driver. En los canales de lectura, dejar el timeout a cero (es decir, "sin timeout") es lo habitual, ya que las lecturas suelen esperar bloqueadas a que el dispositivo tenga algo que comunicar.

Este array, combinado con `usbd_transfer_setup`, es todo lo que el driver necesita para declarar su pipeline de datos. El framework asigna los buffers DMA subyacentes, configura la planificación y vigila los pipes. El driver no necesita escribir nunca en un registro ni programar una transacción de forma manual. Solo escribe callbacks.

### Configuración y desmontaje de transferencias

En el método `attach` mostrado en la sección 2, la llamada a `usbd_transfer_setup` crea los canales a partir del array de configuración:

```c
error = usbd_transfer_setup(uaa->device, &sc->sc_iface_index,
    sc->sc_xfer, myfirst_usb_config, MYFIRST_USB_N_XFER,
    sc, &sc->sc_mtx);
```

Los argumentos son, en orden: el puntero al dispositivo USB, un puntero al índice de interfaz (el framework puede actualizarlo en determinados escenarios con múltiples interfaces), el array de destino para los objetos de transferencia creados, el array de configuración, el número de canales, el puntero al softc (que se pasa a los callbacks a través de `usbd_xfer_softc`) y el mutex que el framework mantendrá bloqueado durante cada callback.

Si esta llamada tiene éxito, `sc->sc_xfer[]` se rellena con punteros a objetos `struct usb_xfer`. Cada objeto encapsula el estado de un canal. A partir de aquí, el driver puede enviar una transferencia en un canal con `usbd_transfer_submit(sc->sc_xfer[i])`, y el framework invocará en su momento el callback correspondiente.

El desmontaje simétrico, mostrado en el método `detach`, es `usbd_transfer_unsetup`:

```c
usbd_transfer_unsetup(sc->sc_xfer, MYFIRST_USB_N_XFER);
```

Esta llamada hace tres cosas, en orden. Cancela cualquier transferencia en curso en cada canal. Espera a que el callback correspondiente se ejecute con `USB_ST_ERROR` o `USB_ST_CANCELLED`, de modo que el driver tenga la oportunidad de limpiar cualquier estado por transferencia. Libera el estado interno del framework para el canal. Tras el retorno de `usbd_transfer_unsetup`, las entradas de `sc_xfer[]` ya no son válidas y el callback asociado no volverá a ser invocado.

Este es el mecanismo que hace que detach sea seguro en presencia de I/O en curso. No necesitas implementar tu propia lógica de "espera de transferencias pendientes". El framework la proporciona, de forma atómica, mediante esta única llamada.

### La máquina de estados del callback

Todo callback de transferencia sigue la misma máquina de estados con tres fases. Cuando el framework invoca el callback, consultas `USB_GET_STATE(xfer)` para obtener el estado actual y a continuación lo gestionas. Los tres estados posibles están declarados en `/usr/src/sys/dev/usb/usbdi.h`:

- `USB_ST_SETUP`: el framework está listo para enviar una nueva transferencia en este canal. Debes preparar la transferencia (establecer su longitud, copiar datos en su buffer, etc.) y llamar a `usbd_transfer_submit`. Si en este momento no tienes trabajo para este canal, simplemente retorna; el framework dejará el canal inactivo hasta que algo más dispare un envío.
- `USB_ST_TRANSFERRED`: la transferencia más reciente se completó correctamente. Debes leer los resultados (copiar los datos recibidos, decidir qué hacer a continuación) y retornar (si el canal debe quedar inactivo) o continuar hacia `USB_ST_SETUP` para iniciar otra transferencia.
- `USB_ST_ERROR`: la transferencia más reciente falló. Debes inspeccionar `usbd_xfer_get_error(xfer)` para saber el motivo, gestionar el error (para la mayoría de errores, se continúa hacia `USB_ST_SETUP` para reintentar tras una breve espera; en caso de stall, se emite un clear-stall) y decidir si continuar.

La forma típica de un callback de lectura bulk es la siguiente:

```c
static void
myfirst_usb_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
    struct myfirst_usb_softc *sc = usbd_xfer_softc(xfer);
    struct usb_page_cache *pc;
    int actlen;

    usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

    switch (USB_GET_STATE(xfer)) {
    case USB_ST_TRANSFERRED:
        pc = usbd_xfer_get_frame(xfer, 0);
        /*
         * Copy actlen bytes from pc into the driver's receive buffer.
         * This is where you hand the data to userland, to a queue,
         * or to another callback.
         */
        myfirst_usb_deliver_received(sc, pc, actlen);
        /* FALLTHROUGH */
    case USB_ST_SETUP:
tr_setup:
        /*
         * Arm a read for 512 bytes.  The actual amount received may
         * be less, because we enabled short_xfer_ok in the config.
         */
        usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
        usbd_transfer_submit(xfer);
        break;

    default:  /* USB_ST_ERROR */
        if (error == USB_ERR_CANCELLED) {
            /* The device is going away.  Do nothing. */
            break;
        }
        if (error == USB_ERR_STALLED) {
            /* Arm a clear-stall on the control pipe; the framework
             * will call us back in USB_ST_SETUP after the clear
             * completes. */
            usbd_xfer_set_stall(xfer);
        }
        goto tr_setup;
    }
}
```

Analicemos cada parte.

La primera línea obtiene el puntero al softc desde el objeto de transferencia. Así es como el callback accede al estado por dispositivo. Funciona porque el softc se pasó a `usbd_transfer_setup`, que lo almacenó dentro del objeto de transferencia.

La llamada a `usbd_xfer_status` rellena `actlen`, el número de bytes transferidos realmente en el frame cero. En una lectura, esto indica cuántos datos llegaron. En una escritura, indica cuántos datos se enviaron. Los otros tres parámetros (que este ejemplo no utiliza) proporcionan la longitud total de la transferencia, el timeout y un puntero a los flags de estado; la mayoría de los callbacks solo necesitan `actlen`.

El switch sobre `USB_GET_STATE(xfer)` es la máquina de estados. En `USB_ST_TRANSFERRED`, el callback copia los datos recibidos desde el frame USB al buffer propio del driver. La función auxiliar `myfirst_usb_deliver_received` (que tú escribirías) podría insertar los datos en una cola, despertar una llamada `read()` dormida sobre el nodo `/dev`, o alimentar un analizador de protocolo de nivel superior.

El `FALLTHROUGH` tras procesar los datos transferidos lleva el callback a la rama `USB_ST_SETUP`. Este es el patrón idiomático para canales que funcionan de forma continua: cada vez que termina una lectura, se inicia inmediatamente otra. Si el driver quisiera detener la lectura tras una sola transferencia (por ejemplo, una petición de control puntual), haría `return;` al final de `USB_ST_TRANSFERRED` en lugar de continuar hacia la rama siguiente.

En `USB_ST_SETUP`, `usbd_xfer_set_frame_len` establece la longitud del frame cero al máximo que el canal puede manejar, y `usbd_transfer_submit` entrega la transferencia al framework. El framework iniciará la operación hardware real y, cuando termine, volverá a llamar al callback con `USB_ST_TRANSFERRED` o `USB_ST_ERROR`.

El caso `default` es donde se gestiona el error. Dos errores reciben tratamiento especial. `USB_ERR_CANCELLED` es la señal de que la transferencia está siendo cancelada, normalmente porque el dispositivo fue desconectado o se llamó a `usbd_transfer_unsetup`. En este caso, el callback no debe reenviar la transferencia; si lo hiciera, podría entrar en condición de carrera con el proceso de cierre y llegar a tocar memoria que está a punto de liberarse. Salir del switch sin llamar a `usbd_transfer_submit` es el comportamiento correcto.

`USB_ERR_STALLED` es la señal de que el endpoint devolvió un handshake STALL, lo que significa que el dispositivo se niega a aceptar más datos hasta que el host limpie el stall. La llamada a `usbd_xfer_set_stall` programa una operación clear-stall en el endpoint de control. Una vez completado el clear-stall, el framework volverá a llamar al callback con `USB_ST_SETUP`, momento en el que el driver puede reenviar la transferencia. Esta lógica está integrada en el framework para que todos los drivers obtengan el mismo comportamiento correcto con el mínimo código.

Para cualquier otro error, el callback pasa a `tr_setup` e intenta reenviar la transferencia. Esta es una política de reintento sencilla. Un driver más sofisticado podría contar errores consecutivos y abandonar tras un umbral, o escalar llamando a `usbd_transfer_unsetup` sobre sí mismo. Para muchos drivers, el bucle de reintento predeterminado es suficiente.

### El callback de escritura

El callback de escritura tiene la misma estructura, pero su rama `USB_ST_SETUP` es más interesante, porque tiene que decidir si hay datos que escribir:

```c
static void
myfirst_usb_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
    struct myfirst_usb_softc *sc = usbd_xfer_softc(xfer);
    struct usb_page_cache *pc;
    int actlen;
    unsigned int len;

    usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

    switch (USB_GET_STATE(xfer)) {
    case USB_ST_TRANSFERRED:
        /* A previous write finished.  Wake any blocked writer. */
        wakeup(&sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]);
        /* FALLTHROUGH */
    case USB_ST_SETUP:
tr_setup:
        len = myfirst_usb_dequeue_write(sc);
        if (len == 0) {
            /* Nothing to send right now.  Leave the channel idle. */
            break;
        }
        pc = usbd_xfer_get_frame(xfer, 0);
        myfirst_usb_copy_write_data(sc, pc, len);
        usbd_xfer_set_frame_len(xfer, 0, len);
        usbd_transfer_submit(xfer);
        break;

    default:  /* USB_ST_ERROR */
        if (error == USB_ERR_CANCELLED)
            break;
        if (error == USB_ERR_STALLED)
            usbd_xfer_set_stall(xfer);
        goto tr_setup;
    }
}
```

El cambio principal está en la lógica de `tr_setup`. Para una lectura, el driver siempre quiere armar otra lectura, por lo que el callback simplemente establece la longitud del frame y envía. Para una escritura, el driver solo envía si hay algo que mandar. La función auxiliar `myfirst_usb_dequeue_write` devuelve el número de bytes extraídos de una cola de transmisión interna; si es cero, el callback sale del switch sin enviar nada, dejando el canal inactivo. Cuando el espacio de usuario escribe más datos en el dispositivo posteriormente, el código del driver que gestiona la llamada al sistema `write()` encola los bytes y llama explícitamente a `usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR])`. Esa llamada dispara una invocación `USB_ST_SETUP` del callback, que ahora encuentra datos en la cola y los envía.

Esta interacción entre la ruta de I/O del espacio de usuario y la máquina de estados de transferencia es el núcleo de un driver USB interactivo. Las lecturas son autónomas: una vez armadas, se rearman en cada finalización. Las escrituras son orientadas a demanda: solo envían cuando hay datos disponibles y quedan inactivas en caso contrario. Ambos patrones se ejecutan dentro de la misma máquina de tres estados; la diferencia está únicamente en lo que ocurre en `USB_ST_SETUP`.

### Transferencias de control

Las transferencias de control no se ejecutan normalmente en canales armados continuamente; generalmente se emiten de forma puntual, bien de forma síncrona desde un manejador de llamada al sistema, bien como un callback puntual disparado por algún evento del driver. El `struct usb_config` para un canal de control tiene `type = UE_CONTROL` y por lo demás se asemeja a las configuraciones bulk e interrupt. El tamaño del buffer debe ser de al menos ocho bytes para contener el setup packet, y el callback gestiona dos frames: el frame cero es el setup packet y el frame uno es la fase de datos opcional.

El uso puntual más habitual consiste en emitir una petición específica del fabricante en el momento de cargar el driver. El driver serie FTDI, por ejemplo, utiliza transferencias de control para establecer la velocidad en baudios y los parámetros de línea cada vez que el usuario configura el puerto serie. Dado que el framework programa el callback de control igual que cualquier otro callback de transferencia, el patrón de código es idéntico. Lo que difiere es la construcción del setup packet en la rama `USB_ST_SETUP`.

Para una transferencia de control-lectura, el código tiene un aspecto similar a este:

```c
case USB_ST_SETUP: {
    struct usb_device_request req;
    req.bmRequestType = UT_READ_VENDOR_DEVICE;
    req.bRequest      = MY_VENDOR_GET_STATUS;
    USETW(req.wValue,  0);
    USETW(req.wIndex,  0);
    USETW(req.wLength, sizeof(sc->sc_status));

    pc = usbd_xfer_get_frame(xfer, 0);
    usbd_copy_in(pc, 0, &req, sizeof(req));

    usbd_xfer_set_frame_len(xfer, 0, sizeof(req));
    usbd_xfer_set_frame_len(xfer, 1, sizeof(sc->sc_status));
    usbd_xfer_set_frames(xfer, 2);
    usbd_transfer_submit(xfer);
    break;
}
```

La macro `USETW` almacena un valor de dieciséis bits en la estructura de petición en el orden de bytes little-endian que requiere USB. La función auxiliar `usbd_copy_in` copia desde un buffer del kernel a un frame USB. Las llamadas a `usbd_xfer_set_frame_len` y `usbd_xfer_set_frames` indican al framework cuántos frames abarca la transferencia y la longitud de cada uno. Para una control-lectura, el frame cero es el setup packet (ocho bytes) y el frame uno es la fase de datos; el framework gestiona de forma transparente la fase de estado al final.

En la rama `USB_ST_TRANSFERRED`, el driver lee la respuesta del frame uno:

```c
case USB_ST_TRANSFERRED:
    pc = usbd_xfer_get_frame(xfer, 1);
    usbd_copy_out(pc, 0, &sc->sc_status, sizeof(sc->sc_status));
    /* sc->sc_status now holds the device's response. */
    break;
```

Las transferencias de control son la herramienta adecuada para operaciones de configuración donde la latencia y el ancho de banda no importan, pero sí la corrección y la secuenciación. No son la herramienta adecuada para datos en streaming; para eso, utiliza transferencias bulk o interrupt.

### Transferencias de interrupción

Las transferencias de interrupción son conceptualmente las más sencillas de los cuatro tipos. Un canal interrupt-IN ejecuta una máquina de estados continua que sondea un único endpoint a intervalos regulares. Cada vez que llega un paquete desde el dispositivo, el callback se activa con `USB_ST_TRANSFERRED`. El driver lee el paquete, lo procesa (a menudo entregándolo al userland) y pasa al siguiente caso para rearmar el canal.

El callback de nuestro canal interrupt es casi idéntico al callback de lectura bulk:

```c
static void
myfirst_usb_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
    struct myfirst_usb_softc *sc = usbd_xfer_softc(xfer);
    struct usb_page_cache *pc;
    int actlen;

    usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

    switch (USB_GET_STATE(xfer)) {
    case USB_ST_TRANSFERRED:
        pc = usbd_xfer_get_frame(xfer, 0);
        myfirst_usb_handle_interrupt(sc, pc, actlen);
        /* FALLTHROUGH */
    case USB_ST_SETUP:
tr_setup:
        usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
        usbd_transfer_submit(xfer);
        break;

    default:
        if (error == USB_ERR_CANCELLED)
            break;
        if (error == USB_ERR_STALLED)
            usbd_xfer_set_stall(xfer);
        goto tr_setup;
    }
}
```

La única diferencia significativa respecto al callback de lectura bulk es que el buffer es más pequeño (los paquetes de endpoint interrupt son típicamente de ocho a sesenta y cuatro bytes) y la semántica de los datos suele ser "actualización de estado" en lugar de "carga de flujo". Un dispositivo USB HID, por ejemplo, envía un informe de sesenta y cuatro bytes cada pocos milisegundos describiendo pulsaciones de teclas y movimientos del ratón; un canal interrupt-IN sondeado continuamente con este patrón es como el kernel recibe esos informes.

Los canales interrupt-OUT funcionan del mismo modo pero al revés: el callback tiene que decidir si enviar algo en cada `USB_ST_SETUP`, de forma análoga al patrón de escritura bulk.

### Operaciones a nivel de frame: lo que ofrece el framework

Las transferencias USB están compuestas de frames. El hardware puede fragmentar una transferencia bulk con un buffer grande en múltiples paquetes; el framework oculta ese detalle y presenta la transferencia como una operación única. Una transferencia de control, en cambio, tiene una estructura de frames explícita (setup, data, status). Una transferencia isócrona tiene un frame por paquete programado. El framework expone esta estructura a través de un pequeño conjunto de funciones auxiliares:

- `usbd_xfer_max_len(xfer)` devuelve la longitud total máxima que el canal puede transferir en un único envío.
- `usbd_xfer_set_frame_len(xfer, frame, len)` establece la longitud de un frame concreto.
- `usbd_xfer_set_frames(xfer, n)` establece el número total de frames en la transferencia.
- `usbd_xfer_get_frame(xfer, frame)` devuelve un puntero de caché de página para un frame concreto, que es lo que se pasa a `usbd_copy_in` y `usbd_copy_out`.
- `usbd_xfer_frame_len(xfer, frame)` devuelve cuántos bytes se transfirieron realmente en un frame determinado (para las finalizaciones).
- `usbd_xfer_max_framelen(xfer)` devuelve la longitud máxima por frame para el canal.

En las transferencias bulk e interrupt, la gran mayoría de los drivers solo acceden al frame cero. En las transferencias de control, acceden a los frames cero y uno. En las transferencias isócronas (que no abordaremos en este capítulo), se itera sobre muchos frames. La clave está en que el framework te ofrece un control completo sobre la disposición de datos por frame, al mismo tiempo que oculta los detalles de hardware que, de otro modo, convertirían la planificación de transferencias en una pesadilla.

### Los helpers `usbd_copy_in` y `usbd_copy_out`

Los buffers USB no son buffers C ordinarios. El framework los asigna de una manera que el hardware del controlador anfitrión pueda direccionarlos directamente, lo que significa que suelen residir en páginas de memoria accesibles mediante DMA con requisitos de alineación específicos de cada plataforma. El framework envuelve estos buffers en un objeto opaco `struct usb_page_cache`, y el driver accede a ellos mediante dos helpers:

- `usbd_copy_in(pc, offset, src, len)` copia `len` bytes del buffer C ordinario `src` al buffer gestionado por el framework en `offset`.
- `usbd_copy_out(pc, offset, dst, len)` copia `len` bytes del buffer gestionado por el framework en `offset` al buffer C ordinario `dst`.

Nunca desreferenciarás un `struct usb_page_cache *` directamente. No asumas que apunta a una región de memoria contigua. Accede siempre a través de los helpers. Esto mantiene el driver portable entre plataformas con diferentes restricciones de DMA, y es la convención estándar en todo `/usr/src/sys/dev/usb/`.

Si tu driver necesita rellenar un buffer USB con datos procedentes de una cadena mbuf o de un puntero de userland, también existen helpers dedicados para eso: `usbd_copy_in_mbuf`, `usbd_copy_from_mbuf`, y la interacción con `uiomove` se gestiona a través de `usbd_m_copy_in` y rutinas relacionadas. Busca en el código fuente del framework USB el helper adecuado; casi con toda seguridad encontrarás uno que se ajuste a tu necesidad.

### Iniciar, detener y consultar transferencias

Además de los tres callbacks, el driver interactúa con los canales de transferencia a través de un pequeño conjunto de funciones de control. Las más importantes son:

- `usbd_transfer_start(xfer)`: solicita al framework que planifique una invocación del callback en el estado `USB_ST_SETUP`, incluso si el canal ha estado inactivo. Se usa cuando hay nuevos datos disponibles para un canal de escritura.
- `usbd_transfer_stop(xfer)`: detiene el canal. Cualquier transferencia en vuelo se cancela y el callback se invoca con `USB_ST_ERROR` (con `USB_ERR_CANCELLED`). No se producen nuevos callbacks hasta que el driver llame de nuevo a `usbd_transfer_start`.
- `usbd_transfer_pending(xfer)`: devuelve true si hay una transferencia actualmente pendiente. Útil para decidir si enviar una nueva o diferirla.
- `usbd_transfer_drain(xfer)`: bloquea hasta que cualquier transferencia pendiente se completa y el canal queda inactivo. Se usa en las rutas de desmontaje que necesitan esperar a que el I/O en vuelo termine antes de continuar.

Estas funciones son seguras de llamar mientras se mantiene el mutex del driver y, de hecho, la mayoría de ellas lo requieren. La documentación del framework y el código de los drivers existentes muestran los patrones de uso esperados; si tienes dudas, busca el nombre de la función en `/usr/src/sys/dev/usb/` con grep y lee cómo la utilizan los drivers existentes.

### Un ejemplo completo: echo-loop por USB

Para hacer concretos los mecanismos de transferencia, consideremos un escenario pequeño de extremo a extremo. El driver expone una entrada `/dev/myfirst_usb0` que acepta escrituras y devuelve lecturas. Un proceso de usuario escribe una cadena en el dispositivo; el driver envía esos bytes al dispositivo USB a través del canal bulk-OUT. El dispositivo devuelve los bytes a través de su endpoint bulk-IN; el driver los recibe y los entrega a cualquier proceso bloqueado en un `read()` sobre el nodo `/dev`. Se trata de un ejercicio útil porque ejercita ambas direcciones del pipeline bulk y porque tiene un criterio de éxito simple y observable: la cadena que entra es la cadena que sale.

El driver necesita una pequeña cola de transmisión y una pequeña cola de recepción, ambas protegidas por el mutex del softc. Cuando el espacio de usuario escribe, el manejador `d_write` adquiere el mutex, copia los bytes en la cola de transmisión y llama a `usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR])`. Cuando el espacio de usuario lee, el manejador `d_read` adquiere el mutex y comprueba la cola de recepción; si está vacía, duerme en un canal relacionado con la cola. El callback de escritura, que se ejecuta bajo el mutex, extrae bytes de la cola y envía la transferencia. El callback de lectura, también bajo el mutex, encola los bytes recibidos y despierta a cualquier lector bloqueado.

El flujo completo desde el `write("hi")` del espacio de usuario hasta que el `read()` del espacio de usuario recibe "hi" involucra tres threads de ejecución entrelazados a través de las máquinas de estados:

1. El thread de usuario ejecuta `write()`. El driver encola "hi" en la cola TX. El driver llama a `usbd_transfer_start`. El thread de usuario retorna.
2. El framework planifica el callback TX con `USB_ST_SETUP`. El callback extrae "hi" de la cola, lo copia en el frame cero, establece la longitud del frame en 2 y envía. El callback retorna.
3. El hardware realiza la transacción bulk-OUT. El dispositivo devuelve "hi" por bulk-IN.
4. El framework planifica el callback RX con `USB_ST_TRANSFERRED` (porque un `USB_ST_SETUP` anterior había armado una lectura). El callback lee "hi" del frame cero a la cola RX, despierta a cualquier lector bloqueado, continúa para rearmar la lectura y envía. El callback retorna.
5. El thread de usuario, si estaba bloqueado en `read()`, se despierta. El manejador `d_read` copia "hi" de la cola RX al espacio de usuario. El thread de usuario retorna.

En cada paso, el mutex se mantiene exactamente donde debe, la máquina de estados se mueve limpiamente entre `USB_ST_SETUP` y `USB_ST_TRANSFERRED`, y el driver no tiene que preocuparse por los límites de paquetes, los mapeos DMA ni la planificación del hardware. El framework se encarga de todo eso.

### Ensamblando el driver echo-loop completo

Para hacer concreta la descripción del echo-loop, recorramos el esqueleto completo de `myfirst_usb`. Lo que sigue no es una copia de los archivos reales del driver en `examples/`; es una presentación narrativa de cómo encajan las piezas. El código completo está en el directorio de ejemplos.

El driver tiene un archivo fuente C, `myfirst_usb.c`, y un pequeño header `myfirst_usb.h`. El header declara la estructura softc, las constantes para la enumeración de transferencias y los prototipos de las funciones auxiliares internas. El archivo fuente contiene la tabla de coincidencias, el array de configuración de transferencias, las funciones callback, los métodos del dispositivo de caracteres, el probe/attach/detach de Newbus y los macros de registro.

El softc es tal como describimos antes: un puntero al dispositivo USB, un mutex, el array de transferencias, un puntero al dispositivo de caracteres, un índice de interfaz, un byte de flags y dos ring buffers internos para los datos encolados de RX y TX. Cada ring buffer es un array de tamaño fijo (digamos, 4096 bytes) más índices de cabeza y cola, protegidos por el mutex.

La tabla de coincidencias contiene una entrada:

```c
static const STRUCT_USB_HOST_ID myfirst_usb_devs[] = {
    {USB_VPI(0x16c0, 0x05dc, 0)},
};
```

El par VID/PID 0x16c0/0x05dc es el VID/PID genérico de pruebas de Van Oosting Technologies Incorporated / OBDEV, libre para usar en prototipos.

El array de configuración de transferencias es el array de tres canales de la sección 3. Los callbacks son los patrones de lectura bulk, escritura bulk y lectura por interrupción que recorrimos.

La rama `USB_ST_TRANSFERRED` del callback de lectura bulk llama a un helper:

```c
static void
myfirst_usb_rx_enqueue(struct myfirst_usb_softc *sc,
    struct usb_page_cache *pc, int len)
{
    int space;
    unsigned int tail;

    space = MYFIRST_USB_RX_BUFSIZE - sc->sc_rx_count;
    if (space < len)
        len = space;  /* drop the excess; a real driver might flow-control */

    tail = (sc->sc_rx_head + sc->sc_rx_count) & (MYFIRST_USB_RX_BUFSIZE - 1);
    if (tail + len > MYFIRST_USB_RX_BUFSIZE) {
        /* wrap-around copy in two pieces */
        usbd_copy_out(pc, 0, &sc->sc_rx_buf[tail], MYFIRST_USB_RX_BUFSIZE - tail);
        usbd_copy_out(pc, MYFIRST_USB_RX_BUFSIZE - tail,
            &sc->sc_rx_buf[0], len - (MYFIRST_USB_RX_BUFSIZE - tail));
    } else {
        usbd_copy_out(pc, 0, &sc->sc_rx_buf[tail], len);
    }
    sc->sc_rx_count += len;

    /* Wake any sleeper. */
    wakeup(&sc->sc_rx_count);
}
```

Se trata de una operación de encolado en un ring buffer con manejo del desbordamiento circular. El helper `usbd_copy_out` se usa para mover bytes del frame USB al ring buffer. Si el ring buffer está lleno, los bytes se descartan. Un driver real probablemente aplicaría control de flujo a nivel USB (dejar de armar nuevas lecturas) o ampliaría el buffer; para el laboratorio, el descarte es aceptable.

El helper del callback de escritura bulk para extraer datos de la cola es la imagen especular:

```c
static unsigned int
myfirst_usb_tx_dequeue(struct myfirst_usb_softc *sc,
    struct usb_page_cache *pc, unsigned int max_len)
{
    unsigned int len, head;

    len = sc->sc_tx_count;
    if (len > max_len)
        len = max_len;
    if (len == 0)
        return (0);

    head = sc->sc_tx_head;
    if (head + len > MYFIRST_USB_TX_BUFSIZE) {
        usbd_copy_in(pc, 0, &sc->sc_tx_buf[head], MYFIRST_USB_TX_BUFSIZE - head);
        usbd_copy_in(pc, MYFIRST_USB_TX_BUFSIZE - head,
            &sc->sc_tx_buf[0], len - (MYFIRST_USB_TX_BUFSIZE - head));
    } else {
        usbd_copy_in(pc, 0, &sc->sc_tx_buf[head], len);
    }
    sc->sc_tx_head = (head + len) & (MYFIRST_USB_TX_BUFSIZE - 1);
    sc->sc_tx_count -= len;
    return (len);
}
```

Los métodos del dispositivo de caracteres son directos. Open comprueba que el dispositivo no esté ya abierto, establece el flag de apertura y arma el canal de lectura:

```c
static int
myfirst_usb_open(struct cdev *dev, int flags, int devtype, struct thread *td)
{
    struct myfirst_usb_softc *sc = dev->si_drv1;

    mtx_lock(&sc->sc_mtx);
    if (sc->sc_flags & MYFIRST_USB_FLAG_OPEN) {
        mtx_unlock(&sc->sc_mtx);
        return (EBUSY);
    }
    sc->sc_flags |= MYFIRST_USB_FLAG_OPEN;
    sc->sc_rx_head = sc->sc_rx_count = 0;
    sc->sc_tx_head = sc->sc_tx_count = 0;
    usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_RD]);
    usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_INTR_DT_RD]);
    mtx_unlock(&sc->sc_mtx);

    return (0);
}
```

Close limpia el flag de apertura y detiene el canal de lectura:

```c
static int
myfirst_usb_close(struct cdev *dev, int flags, int devtype, struct thread *td)
{
    struct myfirst_usb_softc *sc = dev->si_drv1;

    mtx_lock(&sc->sc_mtx);
    usbd_transfer_stop(sc->sc_xfer[MYFIRST_USB_BULK_DT_RD]);
    usbd_transfer_stop(sc->sc_xfer[MYFIRST_USB_INTR_DT_RD]);
    usbd_transfer_stop(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]);
    sc->sc_flags &= ~MYFIRST_USB_FLAG_OPEN;
    mtx_unlock(&sc->sc_mtx);

    return (0);
}
```

Read bloquea hasta que hay datos disponibles y luego copia bytes del ring buffer al espacio de usuario:

```c
static int
myfirst_usb_read(struct cdev *dev, struct uio *uio, int flags)
{
    struct myfirst_usb_softc *sc = dev->si_drv1;
    unsigned int len;
    char tmp[128];
    int error = 0;

    mtx_lock(&sc->sc_mtx);
    while (sc->sc_rx_count == 0) {
        if (sc->sc_flags & MYFIRST_USB_FLAG_DETACHING) {
            mtx_unlock(&sc->sc_mtx);
            return (ENXIO);
        }
        if (flags & O_NONBLOCK) {
            mtx_unlock(&sc->sc_mtx);
            return (EAGAIN);
        }
        error = msleep(&sc->sc_rx_count, &sc->sc_mtx,
            PCATCH | PZERO, "myfirstusb", 0);
        if (error != 0) {
            mtx_unlock(&sc->sc_mtx);
            return (error);
        }
    }

    while (uio->uio_resid > 0 && sc->sc_rx_count > 0) {
        len = min(uio->uio_resid, sc->sc_rx_count);
        len = min(len, sizeof(tmp));
        /* Copy out of ring buffer into tmp (handles wrap-around) */
        myfirst_usb_rx_read_into(sc, tmp, len);
        mtx_unlock(&sc->sc_mtx);
        error = uiomove(tmp, len, uio);
        mtx_lock(&sc->sc_mtx);
        if (error != 0)
            break;
    }
    mtx_unlock(&sc->sc_mtx);
    return (error);
}
```

Observa el patrón: el mutex se mantiene mientras se manipula el ring buffer, pero se libera alrededor de la llamada a `uiomove`, porque `uiomove` puede dormir (para provocar fallos de página en páginas de usuario) y dormir mientras se mantiene un mutex está prohibido. El mutex se readquiere tras el retorno de `uiomove`.

Write es la imagen especular: copia bytes del usuario al buffer TX y luego activa el canal de escritura:

```c
static int
myfirst_usb_write(struct cdev *dev, struct uio *uio, int flags)
{
    struct myfirst_usb_softc *sc = dev->si_drv1;
    unsigned int len, space, tail;
    char tmp[128];
    int error = 0;

    mtx_lock(&sc->sc_mtx);
    while (uio->uio_resid > 0) {
        if (sc->sc_flags & MYFIRST_USB_FLAG_DETACHING) {
            error = ENXIO;
            break;
        }
        space = MYFIRST_USB_TX_BUFSIZE - sc->sc_tx_count;
        if (space == 0) {
            /* buffer is full; wait for the write callback to drain it */
            error = msleep(&sc->sc_tx_count, &sc->sc_mtx,
                PCATCH | PZERO, "myfirstusbw", 0);
            if (error != 0)
                break;
            continue;
        }
        len = min(uio->uio_resid, space);
        len = min(len, sizeof(tmp));

        mtx_unlock(&sc->sc_mtx);
        error = uiomove(tmp, len, uio);
        mtx_lock(&sc->sc_mtx);
        if (error != 0)
            break;

        /* Copy tmp into TX ring buffer (handles wrap-around) */
        tail = (sc->sc_tx_head + sc->sc_tx_count) & (MYFIRST_USB_TX_BUFSIZE - 1);
        myfirst_usb_tx_buf_append(sc, tail, tmp, len);
        sc->sc_tx_count += len;
        usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]);
    }
    mtx_unlock(&sc->sc_mtx);
    return (error);
}
```

Hay dos cosas que merece la pena observar en write. En primer lugar, cuando el buffer TX está lleno, el manejador de escritura duerme sobre `sc_tx_count`; la rama `USB_ST_TRANSFERRED` del callback de escritura llama a `wakeup(&sc_tx_count)` después de drenar algunos bytes, lo que despierta al escritor dormido. En segundo lugar, el manejador de escritura llama a `usbd_transfer_start` en cada fragmento que encola. Esto es seguro (iniciar un canal que ya está en marcha es una operación nula) y garantiza que el callback de escritura reciba un impulso incluso si el canal había quedado inactivo.

Con estos cuatro métodos cdev y los tres callbacks de transferencia, tienes un driver USB de eco mínimamente viable completo. El código fuente completo tiene aproximadamente trescientas líneas: lo suficientemente corto para caber en una sola pantalla, lo suficientemente concreto para ejercitar la API real.

### Elegir entre `usb_fifo` y un `cdevsw` personalizado

Cuando un driver USB necesita exponer una entrada `/dev` al userland, FreeBSD ofrece dos enfoques. El primero es el framework `usb_fifo`, una abstracción genérica de flujo de bytes que proporciona nodos de estilo `/dev/ugenN.M.epM` con read, write, poll y una pequeña interfaz ioctl. Declaras una `struct usb_fifo_methods` con callbacks de open, close, start-read, start-write, stop-read y stop-write, y el framework se encarga del plumbing del cdev y del encolado. Este es el camino de menor resistencia; tanto `uhid(4)` como `ucom(4)` lo utilizan.

El segundo enfoque es un `cdevsw` personalizado, el mismo patrón que practicaste en el capítulo 24. Esto te da control total sobre la interfaz de usuario a costa de escribir más código. Es adecuado cuando el driver necesita una superficie ioctl muy específica, cuando la semántica de read/write no encaja en un flujo de bytes (por ejemplo, un protocolo orientado a mensajes) o cuando el driver ya encaja mal en el modelo `usb_fifo`.

Para el ejemplo que venimos desarrollando, un `cdevsw` personalizado es la elección correcta porque escribimos el método attach que llama a `make_dev` y el método detach que llama a `destroy_dev`, que es exactamente lo que un `cdevsw` personalizado requiere. Para un driver que expone un flujo de bytes (un adaptador serie, por ejemplo), `usb_fifo` es más sencillo. Cuando escribas tu próximo driver USB, examina ambas opciones y elige aquella cuya interfaz se ajuste mejor a tu problema.

### Gestión de errores y política de reintentos

El bucle de reintentos que utiliza nuestro callback de lectura bulk, «ante cualquier error, rearmar e intentarlo de nuevo», es un valor predeterminado razonable para drivers robustos. Pero no es la única política, y a veces es la equivocada.

En un dispositivo que podría desconectarse a mitad de una transferencia (un adaptador USB cuya conexión física se ha extraído antes de que el framework haya tenido ocasión de detectarlo), rearmar indefinidamente es un desperdicio; las transferencias seguirán fallando hasta que se llame a detach. Añadir un pequeño contador de reintentos y rendirse tras, digamos, cinco errores consecutivos evita que el registro se llene de ruido.

En un dispositivo que implementa un protocolo estricto de petición-respuesta, un error podría invalidar toda la sesión. En ese caso, el callback no debería rearmar; en su lugar, debería marcar el driver como «en error» y permitir al usuario cerrar y volver a abrir el dispositivo para restablecerlo.

En un dispositivo que admite stall-and-clear como mecanismo normal de control de flujo, la ruta `usbd_xfer_set_stall` está en el camino feliz, no en el camino de error. Algunos protocolos de clase utilizan stalls para señalar «no estoy listo en este momento»; la maquinaria automática de clear-stall del framework lo gestiona de forma transparente.

Tu elección de política de reintentos debe coincidir con el comportamiento real del dispositivo para el que estás escribiendo. Si tienes dudas, comienza con el valor predeterminado simple de «rearmar en caso de error», observa qué ocurre al conectar y desconectar el dispositivo repetidamente y perfecciona a partir de ahí.

### Tiempos de espera y sus consecuencias

Un tiempo de espera (timeout) en una transferencia USB no es solo una red de seguridad contra bloqueos del hardware; es una declaración explícita de cuánto tiempo está dispuesto a esperar el driver para que una operación se complete antes de tratarla como un fallo. Elegir un timeout es una decisión de diseño que interactúa con muchas otras partes del driver, y acertar con él requiere reflexionar sobre varios escenarios.

El campo de configuración `timeout` en `struct usb_config` se mide en milisegundos. Un valor de cero significa "sin timeout"; la transferencia esperará indefinidamente. Un valor positivo significa "si la transferencia no se ha completado tras este número de milisegundos, cancelarla y entregar un error de timeout al callback".

Para un canal de lectura en un endpoint bulk, la elección habitual es cero. Las lecturas en canales bulk esperan a que el dispositivo tenga algo que decir, y si el dispositivo permanece en silencio durante minutos, eso no es necesariamente un error. Un timeout forzaría al driver a rearmar la lectura cada pocos segundos, lo que desperdicia tiempo y genera ruido en el log.

Para un canal de escritura, la elección habitual es un valor positivo moderado, como 5000 (cinco segundos). Si el dispositivo no drena su FIFO en ese tiempo, algo ha ido mal; en lugar de bloquear una escritura de duración indefinida, el driver devuelve un error al userland, que puede reintentarlo si lo desea.

Para un canal interrupt-IN que sondea actualizaciones de estado, la elección habitual es cero (como en una lectura bulk) o un timeout que coincida con el intervalo de sondeo esperado, obtenido del campo `bInterval` del descriptor del endpoint. Hacer coincidir el valor con `bInterval` proporciona al driver una señal explícita de "a estas alturas ya debería haber recibido noticias del dispositivo".

Para una transferencia de control, los timeouts importan especialmente, porque las transferencias de control son el mecanismo mediante el cual el driver configura el dispositivo, y un dispositivo que no responde a la configuración está bloqueado. Un timeout de entre 500 y 2000 milisegundos es habitual. Si el dispositivo no responde a una solicitud de configuración en unos pocos segundos, el driver debe asumir que algo va mal.

¿Qué ocurre cuando se dispara un timeout? El framework llama al callback con `USB_ERR_TIMEOUT` como error. El callback normalmente trata esto como un fallo transitorio y rearma la transferencia (en canales repetitivos) o devuelve un error al llamante (en operaciones de un solo disparo). Un canal de lectura repetitivo que sigue agotando el timeout probablemente está comunicándose con un dispositivo que no responde; tras varios timeouts consecutivos, puede valer la pena escalar el problema llamando a `usbd_transfer_unsetup` o registrando una advertencia más visible.

Merece la pena mencionar una interacción sutil: si la transferencia tiene un timeout y el driver también activa `pipe_bof` (pipe bloqueado en caso de fallo), un timeout bloqueará el pipe hasta que el driver lo limpie explícitamente. Esto suele ser lo que se desea, porque el pipe puede estar en un estado inconsistente, y limpiar el bloqueo (enviando una nueva configuración o llamando a `usbd_transfer_start`) es un buen momento para registrar lo ocurrido y decidir qué hacer a continuación.

### Qué puede fallar durante la configuración de las transferencias

La llamada a `usbd_transfer_setup` puede fallar por varias razones. Comprender cada una resulta útil tanto para depurar tu propio driver como para leer el código fuente de FreeBSD cuando encuentres fallos.

**Endpoint incorrecto.** Si el array de configuración solicita un endpoint con un par tipo/dirección específico que no existe en la interfaz, la llamada falla con `USB_ERR_NO_PIPE`. Esto generalmente significa que la tabla de coincidencias encontró un dispositivo con un diseño de descriptor diferente al que el driver esperaba; es un bug en el driver.

**Tipo de transferencia no compatible.** Si la configuración especifica `UE_ISOCHRONOUS` en un controlador de host que no admite transferencias isócronas, o si no se puede satisfacer la reserva de ancho de banda, la llamada falla. Las transferencias isócronas son el tipo más complejo y el que con mayor probabilidad presenta limitaciones específicas de la plataforma.

**Sin memoria.** El framework asigna buffers con capacidad DMA para los canales. Si hay poca memoria, la asignación falla. Esto es poco habitual en sistemas modernos, pero puede ocurrir en plataformas embebidas con presupuestos de memoria ajustados.

**Atributos ausentes o no válidos.** Si la configuración tiene un tamaño de buffer de cero, un recuento de frames negativo o una combinación de flags no válida, la llamada falla. Comprueba la configuración con respecto a las declaraciones en `/usr/src/sys/dev/usb/usbdi.h`.

**Estados de gestión de energía.** Si el dispositivo ha sido suspendido o se encuentra en un estado de bajo consumo, algunas solicitudes de configuración de transferencia fallarán. Esto es relevante principalmente para los drivers que gestionan la suspensión selectiva USB.

Cuando `usbd_transfer_setup` falla, el código de error es un valor `usb_error_t`, no un errno estándar. Las definiciones se encuentran en `/usr/src/sys/dev/usb/usbdi.h`. La función `usbd_errstr` convierte un código de error en una cadena imprimible; úsala en tu `device_printf` para que los mensajes de diagnóstico sean informativos.

### Un detalle sobre `pipe_bof`

Ya mencionamos `pipe_bof` (pipe bloqueado ante un fallo) como un flag en la configuración de transferencia, pero la motivación detrás de él merece un análisis más detallado. Los endpoints USB son conceptualmente de un solo thread desde la perspectiva del dispositivo. Cuando el host envía un paquete bulk-OUT, el dispositivo debe procesar ese paquete antes de aceptar otro. Si el paquete falla, el dispositivo puede quedar en un estado indeterminado, y el siguiente paquete no debería enviarse hasta que el driver haya tenido la oportunidad de resincronizarse.

`pipe_bof` indica al framework que pause el pipe cuando una transferencia falla. La siguiente llamada a `usbd_transfer_submit` no iniciará en realidad una operación hardware; en cambio, el framework espera hasta que el driver llame explícitamente a `usbd_transfer_start` en el canal, lo que actúa como señal de reanudación. Esto permite al driver realizar un clear-stall o resincronizarse de otro modo antes de que comience la siguiente transferencia.

Sin `pipe_bof`, el framework enviaría inmediatamente la siguiente transferencia tras un fallo, lo que podría encontrar el mismo problema antes de que el driver haya tenido la oportunidad de reaccionar.

Establecer `pipe_bof = 1` es el valor por defecto seguro para la mayoría de los drivers. Desactivarlo es adecuado para drivers que quieren mantener el pipeline lleno incluso ante errores ocasionales (por ejemplo, drivers de audio donde un breve glitch es preferible a una resincronización síncrona).

### `short_xfer_ok` y semántica de longitud de datos

El flag `short_xfer_ok` es otra opción de configuración cuyo significado vale la pena clarificar. Las transferencias bulk USB no tienen un marcador de fin de mensaje inherente. Si el host tiene un buffer de 512 bytes y el dispositivo solo tiene 100 bytes que enviar, ¿qué debería ocurrir? Hay dos respuestas posibles.

Con `short_xfer_ok` desactivado (el valor por defecto), una transferencia que se completa con menos datos de los solicitados se trata como un error. El framework entrega `USB_ERR_SHORT_XFER` al callback, y el driver debe decidir si reintentar, ignorar o escalar el error.

Con `short_xfer_ok` activado, una transferencia corta se trata como éxito. El callback recibe `USB_ST_TRANSFERRED` con `actlen` establecido al número real de bytes recibidos. Esto es casi siempre lo que necesitas para bulk-IN en protocolos orientados a mensajes, donde el dispositivo decide cuántos datos enviar.

Existe un flag correspondiente para las transferencias salientes: `force_short_xfer`. Si está activado, una transferencia cuyos datos resulten ser un múltiplo exacto del tamaño máximo de paquete del endpoint se rellenará con un paquete de longitud cero al final para señalar el fin de mensaje. USB trata un paquete de longitud cero como una transacción válida, y muchos protocolos lo usan como marcador de límite explícito. El driver FTDI activa este flag en su canal de escritura, por ejemplo, porque el protocolo FTDI espera un paquete corto al final.

Saber qué flag es el adecuado requiere conocer el protocolo que implementa el dispositivo. Cuando escribas un driver para un dispositivo documentado con una especificación de protocolo pública, consulta la especificación para saber cómo gestiona los límites. Cuando escribas un driver para un dispositivo con documentación escasa, activa `short_xfer_ok` en las lecturas (siempre puedes contar los bytes) y prueba ambas configuraciones de `force_short_xfer` en las escrituras para ver cuál acepta el dispositivo.

### Reglas de locking en torno a las transferencias

El framework USB impone dos reglas de locking que es fundamental respetar.

En primer lugar, el mutex que pasas a `usbd_transfer_setup` está tomado por el framework en cada invocación del callback. No necesitas adquirirlo dentro del callback; ya está tomado. Tampoco debes liberarlo dentro del callback; hacerlo rompe la suposición del framework y puede causar fallos aleatorios.

En segundo lugar, toda llamada desde código del driver (no desde el callback) a cualquiera de `usbd_transfer_start`, `usbd_transfer_stop`, `usbd_transfer_submit`, `usbd_transfer_drain` y `usbd_transfer_pending` debe realizarse con el mutex tomado. Esto se debe a que estas funciones leen y escriben campos dentro del objeto de transferencia que el callback también modifica, y el mutex es lo que serializa el acceso.

En la práctica, esto significa que la mayor parte del código del driver que interactúa con las transferencias tiene este aspecto:

```c
mtx_lock(&sc->sc_mtx);
usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]);
mtx_unlock(&sc->sc_mtx);
```

o en secciones críticas más largas:

```c
mtx_lock(&sc->sc_mtx);
/* enqueue data */
enqueue(sc, data, len);
/* nudge the channel if it is idle */
if (!usbd_transfer_pending(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]))
    usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]);
mtx_unlock(&sc->sc_mtx);
```

Los drivers que violan estas reglas a veces parecen funcionar, pero fallan de forma intermitente bajo carga, con I/O intensa o durante el detach. Hacer bien el locking desde el principio ahorra muchas horas de depuración más adelante.

### Cerrando la sección 3

La sección 3 ha mostrado cómo fluyen los datos a través de un driver USB. Un array de configuración declara los canales, `usbd_transfer_setup` los asigna, los callbacks los conducen a través de la máquina de tres estados, y `usbd_transfer_unsetup` los libera. El framework abstrae los detalles hardware: buffers DMA, planificación de frames, arbitraje de endpoints, gestión de stalls. La tarea del driver es escribir callbacks que gestionen la compleción y organizar el flujo de datos a través de los callbacks.

Tres ideas merecen ser recordadas. Primera: la máquina de tres estados (`USB_ST_SETUP`, `USB_ST_TRANSFERRED`, `USB_ST_ERROR`) es la misma en cada canal, independientemente del tipo de transferencia. Aprender a leer un callback USB significa aprender a analizar esta máquina de estados; una vez que la conoces, todos los callbacks de todos los drivers USB del árbol son legibles. Segunda: la abstracción `struct usb_page_cache` es la única forma segura de mover datos dentro y fuera de los buffers USB. Nunca omitas `usbd_copy_in` y `usbd_copy_out`. Tercera: la disciplina de locking en torno a `usbd_transfer_start`, `_stop` y `_submit` no es opcional; toda llamada desde código del driver debe realizarse bajo el mutex.

Con las secciones 1 a 3 en mano, tienes un modelo mental completo de cómo escribir un driver USB: los conceptos, el esqueleto y el pipeline de datos. La sección 4 cambia ahora al lado serie de la parte 6. El subsistema UART es más antiguo, más sencillo en algunos aspectos, más limitado en otros, y sus convenciones son distintas a las de USB. Pero muchos de los mismos hábitos se trasladan: coincidir solo con lo que admites, hacer el attach en fases que puedan deshacerse limpiamente, conducir el hardware a través de una máquina de estados y respetar el locking.

> **Respira hondo.** Ya hemos recorrido la mitad USB del capítulo: los roles de host y dispositivo, el árbol de descriptores, los cuatro tipos de transferencia, el esqueleto probe/attach/detach y la máquina de callbacks de tres estados `USB_ST_SETUP`/`USB_ST_TRANSFERRED`/`USB_ST_ERROR` que ejecuta todo driver USB. El resto del capítulo se adentra en el lado serie: el framework `uart(4)` con su driver de referencia `ns8250`, la integración con la capa TTY y `termios`, el puente `ucom(4)` que utilizan los adaptadores USB a serie, y las herramientas y laboratorios que permiten probar ambos tipos de drivers sin hardware real. Si quieres cerrar el libro y retomarlo más tarde, este es un punto de pausa natural.

## Sección 4: Escribir un driver UART serie

### De USB a UART: un cambio de panorama

Las secciones 2 y 3 te proporcionaron un driver USB completo. El framework era moderno en todos los sentidos: hot-plug, compatible con DMA, orientado a mensajes, con una abstracción rica. La sección 4 se centra ahora en `uart(4)`, el framework de FreeBSD para controlar Universal Asynchronous Receiver/Transmitters. El panorama es distinto. Muchos chips UART son más antiguos que el propio USB, y el diseño del framework lo refleja. No hay hot-plug (un puerto serie suele estar soldado a la placa). No hay DMA para la mayoría de los chips (el chip tiene una pequeña FIFO que se sondea o una interrupción que se gestiona). No existe jerarquía de descriptores (el chip no anuncia sus capacidades; tú sabes con qué construiste). Y no existe la noción de canales de transferencia; solo existe el puerto, por el que entran y salen bytes.

Lo que el framework sí proporciona es una división disciplinada de responsabilidades entre tres capas. En la base está tu driver, que sabe cómo funcionan los registros del chip, cómo se generan sus interrupciones, cómo se comportan sus FIFOs y qué recursos específicos de la plataforma (línea IRQ, rango de puertos I/O, fuente de reloj) necesita. En el medio está el propio framework `uart(4)`, que gestiona el registro, los cálculos de configuración de la tasa de baudios, el buffering, la integración TTY y la planificación del trabajo de lectura y escritura. En la parte superior está la capa TTY, que presenta el puerto al espacio de usuario como `/dev/ttyuN` y `/dev/cuauN` y gestiona la semántica terminal: edición de línea, generación de señales, caracteres de control y el extenso vocabulario de opciones de `termios` que expone `stty(1)`.

Tú no escribes la capa TTY. Tú no escribes la mayor parte del framework `uart(4)`. Tu trabajo, cuando escribes un driver UART, es implementar un pequeño conjunto de métodos específicos del hardware que el framework invoca cuando necesita realizar alguna operación a nivel de registro. El framework conecta entonces esos métodos con el resto de la maquinaria serie del kernel sin coste adicional.

Esta sección recorre ese cableado. Cubre el diseño del framework `uart(4)`, las estructuras y métodos que debes rellenar, el driver canónico `ns8250` como referencia concreta y la integración con la capa TTY. Termina con el framework relacionado `ucom(4)`, que es la forma en que los puentes USB a serie se exponen al espacio de usuario usando la misma interfaz TTY que un UART real.

### Dónde vive el framework `uart(4)`

El propio framework vive en `/usr/src/sys/dev/uart/`. Si listas ese directorio, verás un puñado de archivos del framework y una familia de drivers específicos de hardware.

Los archivos del framework son:

- `/usr/src/sys/dev/uart/uart.h`: la cabecera de nivel superior que define la API pública del framework.
- `/usr/src/sys/dev/uart/uart_bus.h`: las estructuras para la integración con newbus y el softc por puerto.
- `/usr/src/sys/dev/uart/uart_core.c`: la lógica de attach, el despachador de interrupciones, el bucle de polling y el enlace entre `uart(4)` y `tty(4)`.
- `/usr/src/sys/dev/uart/uart_tty.c`: la implementación de `ttydevsw` que mapea las operaciones de `uart(4)` sobre las operaciones de `tty(4)`.
- `/usr/src/sys/dev/uart/uart_cpu.h`, `uart_dev_*.c`: pegamento de plataforma y registro de consola.

Los drivers específicos de hardware son archivos con la forma `uart_dev_NAME.c` y, en ocasiones, `uart_dev_NAME.h`. El más importante de ellos es `uart_dev_ns8250.c`, que implementa la familia ns8250 (incluyendo el 16450, 16550, 16550A, 16650, 16750 y muchos compatibles). Dado que el 16550A es prácticamente el UART estándar para los puertos serie de estilo PC, este único driver gestiona la mayor parte del hardware serie real en el mundo. Cuando quieras aprender cómo es un driver de UART de FreeBSD real, este es el archivo que debes abrir.

El resto de drivers del directorio gestionan chips que no son compatibles con el 16550: la variante Intel MID, el UART PL011 de ARM usado en Raspberry Pi y otras placas ARM, el UART NXP i.MX, el Z8530 de Sun Microsystems, y así sucesivamente. Cada uno sigue el mismo patrón: rellenar una `struct uart_class` y una `struct uart_ops`, registrarse en el framework e implementar los métodos de acceso al hardware.

### La estructura `uart_class`

Todo driver UART comienza declarando una `struct uart_class`, que es el descriptor de hardware que el framework utiliza para identificar la familia de chips. La definición se encuentra en `/usr/src/sys/dev/uart/uart_bus.h`. La estructura tiene esta forma (parafraseada; la declaración real incluye algunos campos más):

```c
struct uart_class {
    KOBJ_CLASS_FIELDS;
    struct uart_ops *uc_ops;
    u_int            uc_range;
    u_int            uc_rclk;
    u_int            uc_rshift;
};
```

La macro `KOBJ_CLASS_FIELDS` incorpora la maquinaria kobj que el Capítulo 23 presentó (en el contexto de Newbus). Una `uart_class` es, a nivel de objeto abstracto del kernel, una clase kobj cuyas instancias son `uart_softc`. Así es como el framework puede invocar métodos específicos del driver sin necesidad de una cadena de `if`: el despacho de métodos se realiza mediante búsqueda kobj.

`uc_ops` es un puntero a la estructura de operaciones (que veremos a continuación), la cual enumera los métodos específicos del chip.

`uc_range` indica cuántos bytes de espacio de direcciones de registros usa el chip. Para un UART compatible con ns16550, este valor es 8.

`uc_rclk` es la frecuencia del reloj de referencia en hercios. El framework la usa para calcular los divisores de velocidad de transmisión. Para un UART de estilo PC, el reloj de referencia suele ser 1,843,200 hercios (un múltiplo específico de las velocidades de transmisión estándar).

`uc_rshift` es el desplazamiento de dirección de registro. En algunos buses, los registros UART están espaciados en intervalos distintos a un byte (por ejemplo, cada cuatro bytes en algunos diseños con memoria mapeada). Un desplazamiento de cero indica un empaquetado ajustado; un desplazamiento de dos significa que cada registro lógico ocupa cuatro bytes de espacio de direcciones.

Para nuestro ejemplo en curso, la declaración de clase tiene este aspecto:

```c
static struct uart_class myfirst_uart_class = {
    "myfirst_uart class",
    myfirst_uart_methods,
    sizeof(struct myfirst_uart_softc),
    .uc_ops   = &myfirst_uart_ops,
    .uc_range = 8,
    .uc_rclk  = 1843200,
    .uc_rshift = 0,
};
```

Los tres primeros argumentos posicionales son las entradas de `KOBJ_CLASS_FIELDS`: un nombre, una tabla de métodos y un tamaño por instancia. Los campos nombrados son los específicos de UART. Para un driver dirigido a chips compatibles con 16550, estos valores son los predeterminados convencionales.

### La estructura `uart_ops`

La `struct uart_ops` es donde reside el código específico del hardware. Es una tabla de punteros a función que el framework invoca en momentos concretos. La definición se encuentra en `/usr/src/sys/dev/uart/uart_cpu.h`:

```c
struct uart_ops {
    int  (*probe)(struct uart_bas *);
    void (*init)(struct uart_bas *, int, int, int, int);
    void (*term)(struct uart_bas *);
    void (*putc)(struct uart_bas *, int);
    int  (*rxready)(struct uart_bas *);
    int  (*getc)(struct uart_bas *, struct mtx *);
};
```

Cada operación recibe un `struct uart_bas *` como primer argumento. "bas" son las siglas de "bus address space" (espacio de direcciones del bus); es la abstracción del framework para acceder a los registros del chip. Un driver no necesita saber ni preocuparse de si el chip está en espacio de I/O o en espacio con memoria mapeada; simplemente llama a `uart_getreg(bas, offset)` y a `uart_setreg(bas, offset, value)` (declaradas en `/usr/src/sys/dev/uart/uart.h`), y el framework enruta el acceso correctamente.

Veamos las seis operaciones una por una.

`probe` se invoca cuando el framework necesita saber si hay un chip de esta clase presente en una dirección determinada. El driver normalmente escribe en un registro, lo vuelve a leer y devuelve cero si la lectura coincide (lo que indica que el chip está realmente presente), o un errno distinto de cero en caso contrario. Para un ns16550, el probe escribe un patrón de prueba en el registro de prueba y lo vuelve a leer.

`init` se invoca para inicializar el chip a un estado conocido. Los argumentos tras el bas son `baudrate`, `databits`, `stopbits` y `parity`. El driver calcula el divisor, escribe el bit de acceso al divisor-latch, escribe el divisor, borra el divisor-latch, configura el registro de control de línea para la combinación de datos/stop/paridad solicitada, activa los FIFOs y habilita las interrupciones del chip. La secuencia exacta de registros para un 16550 ocupa varias decenas de líneas de código y está documentada en la hoja de datos del chip.

`term` se invoca para apagar el chip. Normalmente deshabilita las interrupciones, vacía los FIFOs y deja el chip en un estado seguro.

`putc` envía un único carácter. Se utiliza en la ruta de la consola de bajo nivel y para la salida de diagnóstico basada en polling. El driver espera activamente (busy-wait) hasta que se activa el flag de registro-de-retención-del-transmisor-vacío y, a continuación, escribe el byte en el registro de transmisión.

`rxready` devuelve un valor distinto de cero si hay al menos un byte disponible para leer. El driver lee el registro de estado de línea y comprueba el bit de dato-listo.

`getc` lee un único carácter. Se utiliza en la consola de bajo nivel para la entrada. El driver espera activamente hasta que se activa el flag de dato-listo (o el llamador garantiza que `rxready` acaba de devolver true), y luego lee el registro de recepción.

Estos seis métodos constituyen toda la superficie específica del hardware para un driver UART a bajo nivel. Todo lo demás (gestión de interrupciones, buffering, integración con TTY, conexión en caliente de UARTs PCIe, selección de consola) lo proporciona el framework. Un nuevo driver UART es, en la práctica, una implementación de seis funciones más unas pocas declaraciones.

### Una mirada más detallada a `ns8250`

El driver ns8250 en `/usr/src/sys/dev/uart/uart_dev_ns8250.c` es el mejor lugar para ver estos métodos de forma concreta. Es un driver maduro y listo para producción que gestiona todas las variantes de la familia 8250/16450/16550/16550A. Las definiciones de registros que utiliza (de `/usr/src/sys/dev/ic/ns16550.h`) son las mismas que usa cualquier cabecera relacionada con UART en el mundo de los PC. Cuando lees este driver, estás leyendo, en la práctica, la implementación de referencia de un driver 16550 para FreeBSD.

La implementación de put-character es instructiva por su sencillez:

```c
static void
ns8250_putc(struct uart_bas *bas, int c)
{
    int limit;

    limit = 250000;
    while ((uart_getreg(bas, REG_LSR) & LSR_THRE) == 0 && --limit)
        DELAY(4);
    uart_setreg(bas, REG_DATA, c);
    uart_barrier(bas);
    limit = 250000;
    while ((uart_getreg(bas, REG_LSR) & LSR_TEMT) == 0 && --limit)
        DELAY(4);
}
```

El bucle sondea el registro de estado de línea (LSR) para detectar el flag de registro-de-retención-del-transmisor-vacío (THRE). Cuando está activo, el registro de retención del transmisor está listo para aceptar un byte. El driver escribe el byte en el registro de datos (REG_DATA) y luego sondea de nuevo el flag de transmisor-vacío (TEMT) para garantizar que el byte se ha desplazado hacia fuera antes de retornar.

La llamada a `uart_barrier` es una barrera de memoria que garantiza que la escritura en el registro de datos sea visible para el hardware antes de las lecturas posteriores. En plataformas con ordenación de memoria débil, omitir esta barrera causaría pérdidas de datos intermitentes.

`DELAY(4)` cede cuatro microsegundos por iteración, y el contador `limit` es 250,000. Juntos, proporcionan un tiempo de espera de un segundo antes de que el bucle se rinda. Para un UART real, 250,000 iteraciones es un límite que nunca debería alcanzarse en un funcionamiento normal; es una red de seguridad para el caso patológico en que el chip se encuentre en un estado inesperado.

El probe es igualmente directo:

```c
static int
ns8250_probe(struct uart_bas *bas)
{
    u_char val;

    /* Check known 0 bits that don't depend on DLAB. */
    val = uart_getreg(bas, REG_IIR);
    if (val & 0x30)
        return (ENXIO);
    return (0);
}
```

Los bits 4 y 5 del Interrupt Identification Register (IIR) están definidos como siempre-cero en todas las variantes de la familia 16550. Si esos bits se leen como uno, no es un registro 16550 real, y el probe rechaza la dirección.

Podrías leer el driver completo en una tarde. Lo que obtendrías es un modelo mental claro: los métodos son reducidos, el framework es amplio, y la ingeniería real reside en gestionar las peculiaridades de revisiones concretas de chips (un bug en el FIFO del predecesor del 16550, una errata en algunos chipsets de PC, un problema de detección de señal en ciertos dispositivos Oxford). Un nuevo driver UART para un chip que se comporta correctamente es, realmente, un archivo pequeño.

### El `uart_softc` y cómo lo utiliza el framework

Cada instancia de un driver UART tiene una `struct uart_softc`, definida en `/usr/src/sys/dev/uart/uart_bus.h`. El framework asigna una por cada puerto conectado. Sus campos más importantes incluyen un puntero al `uart_bas` que describe la disposición de registros del puerto, los recursos de I/O (el IRQ, el rango de memoria o rango de puertos de I/O), el dispositivo TTY conectado a este puerto, los parámetros de línea actuales y dos buffers de bytes (RX y TX) que el framework utiliza internamente. El driver normalmente no asigna su propio softc; utiliza el `uart_softc` del framework, con las extensiones específicas del hardware añadidas mediante herencia de clases kobj.

Cuando el framework recibe una interrupción de un UART, invoca una función interna del framework que lee el registro de identificación de interrupción, decide qué tipo de trabajo ha solicitado el chip (transmisor-listo, dato-de-recepción-disponible, estado-de-línea, estado-de-módem) y despacha al manejador adecuado. Los manejadores extraen datos del FIFO de RX del chip hacia el buffer circular de RX del framework, o empujan datos desde el buffer circular de TX del framework hacia el FIFO de TX del chip, o actualizan variables de estado en respuesta a cambios en las señales del módem. El manejador de interrupción retorna, y la capa TTY consume los buffers circulares a su propio ritmo a través de las rutas put-character y get-character del framework.

Por eso la tabla `uart_ops` del driver es tan pequeña. El trabajo de alto volumen (mover bytes entre el chip y los buffers circulares) lo gestiona el código compartido del framework, que accede a los registros del chip a través de `uart_getreg` y `uart_setreg`. El driver solo necesita exponer las primitivas de bajo nivel; la composición se realiza por él.

### Integración con la capa TTY

Por encima del framework `uart(4)` se encuentra la capa TTY, definida en `/usr/src/sys/kern/tty.c` y archivos relacionados. Un puerto UART en FreeBSD aparece en el userland como dos nodos de `/dev`:

- `/dev/ttyuN`: el nodo de entrada (callin). Abrirlo bloquea la ejecución hasta que se activa una señal de detección de portadora, lo que modela una llamada entrante en un módem. Se utiliza para dispositivos que responden conexiones, no que las inician.
- `/dev/cuauN`: el nodo de salida (callout). Abrirlo no espera la detección de portadora. Se utiliza para dispositivos que inician conexiones, o para desarrolladores que quieren comunicarse con un puerto serie sin simular que es un módem.

La distinción es histórica, y se remonta a la era en que los puertos serie estaban realmente conectados a módems analógicos con semánticas separadas de "alguien está llamando" e "yo estoy iniciando una llamada". FreeBSD conserva esta distinción porque algunos flujos de trabajo embebidos e industriales todavía dependen de ella, y porque el coste de implementación es mínimo una vez que el patrón de "dos lados del mismo puerto" de la capa TTY está establecido.

La capa TTY invoca el framework `uart(4)` a través de una estructura `ttydevsw` cuyos métodos se corresponden con claridad con las operaciones UART. Las entradas más importantes incluyen:

- `tsw_open`: se invoca cuando el userland abre el puerto. El framework habilita las interrupciones, enciende el chip y aplica el `termios` por defecto.
- `tsw_close`: se invoca cuando se libera la última referencia del userland. El framework vacía el buffer TX, deshabilita las interrupciones (a menos que el puerto sea también una consola) y pone el chip en estado inactivo.
- `tsw_ioctl`: se invoca para los ioctls que la capa TTY no gestiona por sí misma. La mayoría de los ioctls específicos de UART los gestiona el framework.
- `tsw_param`: se invoca cuando cambia `termios`. El framework reprograma la velocidad de transmisión, los bits de datos, los bits de stop, la paridad y el control de flujo del chip.
- `tsw_outwakeup`: se invoca cuando hay nuevos datos que transmitir. El framework habilita la interrupción de transmisor-listo si estaba deshabilitada; en el siguiente IRQ, el framework empuja bytes desde el buffer circular hacia el chip.

Normalmente no tendrás que escribir ninguno de estos. El framework en `uart_tty.c` los implementa una sola vez para todos los drivers UART. La única contribución de tu driver son los seis métodos de `uart_ops`.

### La interfaz `termios` en la práctica

Cuando un usuario ejecuta `stty 115200` en un puerto serie, se produce la siguiente cadena de llamadas:

1. `stty(1)` abre el puerto y emite un ioctl `TIOCSETA` con la nueva `struct termios`.
2. La capa TTY del kernel recibe el ioctl y actualiza su copia interna del termios del puerto.
3. La capa TTY llama a `tsw_param` en el `ttydevsw` del puerto, pasando el nuevo termios.
4. La implementación `uart_param` del framework `uart(4)` examina los campos de termios (`c_ispeed`, `c_ospeed`, `c_cflag` con sus sub-bits `CSIZE`, `CSTOPB`, `PARENB`, `PARODD`, `CRTSCTS`) y llama al método `init` del driver con los valores brutos correspondientes.
5. El método `init` del driver calcula el divisor, escribe el registro de control de línea, reconfigura el FIFO y retorna.

Nada de esto requiere que el driver conozca termios. La traducción de los bits de termios a enteros brutos la realiza el framework. El driver ve únicamente los valores brutos: velocidad de transmisión en bits por segundo, bits de datos (habitualmente de 5 a 8), bits de stop (1 o 2) y un código de paridad.

Esta separación es lo que permite a FreeBSD ejecutar el mismo framework `uart(4)` sobre chips radicalmente diferentes. Un driver 16550 y un driver PL011 implementan los mismos seis métodos `uart_ops`. La traducción de termios a valores brutos ocurre una sola vez, en el código del framework, para cada familia de chips.

### Control de flujo a nivel de registro

El control de flujo por hardware se gestiona habitualmente mediante dos señales en el UART: CTS (clear to send) y RTS (request to send). Cuando el extremo remoto activa CTS, está indicando al transmisor local «estoy listo para recibir más datos». Cuando el extremo local activa RTS, le está diciendo lo mismo al transmisor remoto. Cuando cualquiera de las dos señales no está activa, el transmisor correspondiente se detiene.

En un 16550, RTS se controla mediante un bit del registro de control del módem (MCR), y CTS se lee desde un bit del registro de estado del módem (MSR). El framework expone el control de flujo a través de termios (flag `CRTSCTS`), a través de ioctls (`TIOCMGET`, `TIOCMSET`, `TIOCMBIS`, `TIOCMBIC`), y a través de respuestas automáticas al nivel de llenado del FIFO.

Cuando el FIFO de recepción supera un umbral de llenado, el driver desactiva RTS para pedir al extremo remoto que deje de transmitir. Cuando el FIFO se vacía por debajo de un umbral distinto, el driver vuelve a activar RTS. Cuando la interrupción de estado del módem se dispara porque CTS ha cambiado, el manejador de interrupciones habilita o deshabilita la ruta de transmisión en consecuencia. Toda esta lógica pertenece al framework; el driver solo expone las primitivas a nivel de registro.

El control de flujo por software (XON/XOFF) se gestiona íntegramente en la capa TTY, insertando e interpretando los bytes XON (0x11) y XOFF (0x13) en el flujo de datos. El driver del UART no tiene ningún papel en este proceso.

### El camino del manejador de interrupciones en detalle

Más allá de los seis métodos de `uart_ops`, un driver UART real suele implementar un manejador de interrupciones. El framework incluye uno genérico en `uart_core.c` que funciona para la gran mayoría de chips, pero el driver puede proporcionar el suyo propio para chips con comportamientos inusuales. Para entender qué hace el manejador genérico del framework, y cuándo podrías querer reemplazarlo, conviene seguir el camino del manejador.

Cuando se dispara la interrupción hardware, el ISR del framework lee el registro de identificación de interrupción (IIR) a través de `uart_getreg`. El IIR codifica cuál de cuatro condiciones desencadenó la interrupción: line-status (ocurrió un error de encuadre o un desbordamiento), received-data-available (hay al menos un byte en el FIFO de recepción), transmitter-holding-register-empty (el FIFO de TX quiere más datos) o modem-status (cambió el estado de una señal del módem).

Para las interrupciones de line-status, el framework registra una advertencia (o incrementa un contador) y continúa.

Para received-data-available, el framework lee bytes del FIFO RX del chip de uno en uno y los introduce en el ring buffer RX interno del driver. El bucle continúa hasta que el flag de datos disponibles se desactiva. Una vez que el ring buffer tiene bytes, el framework señaliza el camino de entrada de la capa TTY, que irá leyendo los bytes conforme el consumidor esté listo.

Para transmitter-holding-register-empty, el framework extrae bytes de su ring buffer TX interno y los introduce en el FIFO TX del chip de uno en uno. El bucle continúa hasta que el FIFO TX está lleno o el ring buffer está vacío. Una vez que el ring buffer está vacío, el framework deshabilita la interrupción de transmisión para que el chip no siga disparándose; la siguiente llamada a `tsw_outwakeup` (desde la capa TTY, cuando haya datos nuevos) la volverá a habilitar.

Para los cambios de modem-status, el framework actualiza su estado interno de señales del módem y señaliza la capa TTY si el cambio es significativo (por ejemplo, la desactivación de CTS cuando el control de flujo hardware está habilitado).

Todo esto se hace en contexto de interrupción con el mutex del driver tomado. El mutex es un spin mutex (`MTX_SPIN`) para los drivers UART, porque tomar un mutex que admita sleep en un manejador de interrupciones está prohibido. Los helpers del framework conocen esta restricción y utilizan las primitivas apropiadas.

¿Cuándo podría un driver querer reemplazar el manejador genérico? Vienen a la mente tres situaciones.

En primer lugar, si el chip tiene semánticas de FIFO inusuales. Algunos chips no borran sus registros de identificación de interrupción de la manera obvia; hay que vaciar completamente el FIFO, o hay que leer un registro específico para hacer el acknowledge. Si la hoja de datos de tu chip describe tal peculiaridad, deberás reemplazar el manejador con lógica específica del chip.

En segundo lugar, si el chip tiene soporte DMA que quieras aprovechar. El manejador genérico del framework es PIO (programmed I/O): un byte por acceso a registro. Un chip con motor DMA podría mover muchos bytes por interrupción, reduciendo significativamente la carga de la CPU a altas velocidades de baudios. Implementar DMA requiere código específico del chip.

En tercer lugar, si el chip tiene timestamping hardware u otras funcionalidades avanzadas. Algunos UARTs embebidos pueden registrar con precisión de microsegundos la marca de tiempo de cada byte recibido, algo muy valioso para protocolos industriales. El framework no conoce esta capacidad, por lo que el driver debe implementarla.

Para hardware típico, el manejador genérico es correcto y eficiente. No lo reemplaces sin una razón concreta.

### Los ring buffers de TX y RX

El framework `uart(4)` mantiene dos ring buffers dentro del softc de cada puerto. Estos son independientes de cualquier buffer que tenga el propio chip: aunque el chip tenga un FIFO de 64 bytes, el framework tiene sus propios ring buffers de cierto tamaño configurable (típicamente 4 KB para cada dirección) que se sitúan entre el chip y la capa TTY.

El propósito de estos ring buffers es absorber ráfagas. Supongamos que el consumidor de datos es lento (un proceso en userland ocupado) y el productor (el dispositivo serie remoto) está enviando datos a 115200 baudios. Sin un ring buffer, el FIFO de 64 bytes del chip se llenaría en unos 6 milisegundos y se perderían bytes. Con un ring buffer de 4 KB, el buffer puede absorber una ráfaga de 350 milisegundos a 115200 baudios, tiempo suficiente para que userland se ponga al día en casi cualquier escenario real.

Los tamaños de estos ring buffers no son configurables por driver en general; están integrados en el framework. La implementación del ring buffer reside en `uart_core.c` y utiliza el mismo tipo de aritmética de punteros head/tail que los ring buffers de nuestro driver USB de eco.

Cuando la capa TTY pide bytes (a través de `ttydisc_rint`), el framework mueve bytes del ring RX hacia la cola de entrada propia de la capa TTY, que tiene su propio buffer y procesamiento de disciplina de línea (modo canónico, eco, generación de señales, etc.). Cuando userland escribe bytes, estos llegan al camino `tsw_outwakeup` del framework y se transfieren al ring TX; el manejador de interrupción de transmisor vacío del framework los empuja desde el ring hacia el chip.

Este diseño tiene una propiedad interesante: el driver, el framework y la capa TTY están todos débilmente acoplados. El driver solo conoce el chip. El framework solo conoce registros y ring buffers. La capa TTY solo conoce el buffer y la disciplina de línea. Cada capa puede probarse y razonarse de forma independiente.

### Depuración de drivers serie

Cuando un driver serie no funciona, los síntomas pueden ser confusos. Los bytes entran, los bytes salen, pero los dos no coinciden. El reloj avanza, pero los caracteres parecen basura. El puerto se abre, pero las escrituras devuelven cero bytes. Esta sección recoge las técnicas de diagnóstico que resultan más útiles.

**Registra información detallada en attach.** Utiliza `device_printf(dev, "attached at %x, IRQ %d\n", ...)` para verificar la dirección y la IRQ que acabó asignando tu driver. Si la dirección es incorrecta, ninguna operación de I/O funcionará; si la IRQ es incorrecta, no se disparará ninguna interrupción. Los mensajes de attach son la primera línea de defensa.

**Usa `sysctl dev.uart.0.*` para inspeccionar el estado del puerto.** El framework `uart(4)` exporta muchos parámetros y estadísticas por puerto a través de sysctl. Su lectura muestra la velocidad de baudios actual, el número de bytes transmitidos, el número de desbordamientos, el estado de las señales del módem y más. Si `tx` está incrementándose pero `rx` no, el transmisor funciona pero el receptor no; si ambos son cero, no está ocurriendo nada en absoluto.

**Examina el hardware con `kgdb`.** Si dispones de un volcado de memoria del kernel o de la posibilidad de conectar un depurador de kernel, puedes inspeccionar el `uart_softc` directamente y leer sus valores de registro. Esto resulta muy valioso cuando el chip se encuentra en un estado confuso que la abstracción software oculta.

**Compara con un driver que funcione.** Si tu modificación rompió algo, busca la diferencia entre tu código y el `ns8250.c` original. La diferencia será pequeña y, una vez que la identifiques, la solución suele ser clara.

**Usa `dd` para pruebas pequeñas y reproducibles.** En lugar de `cu` para depuración, usa `dd if=/dev/zero of=/dev/cuau0 bs=1 count=100` para escribir exactamente 100 bytes. Luego `dd if=/dev/cuau0 of=output.bin bs=1 count=100` para leer exactamente 100 bytes (con un timeout adecuado o una segunda apertura). Esto aísla problemas de temporización y codificación de caracteres que el uso interactivo de `cu` podría enmascarar.

**Comprueba los pines de control de flujo hardware.** Muchos fallos de control de flujo son de hardware, no de software. Usa un break-out board, un multímetro o un osciloscopio para verificar que DTR, RTS, CTS y DSR están a los voltajes que esperas. Si alguno está flotando, el comportamiento del chip es indefinido.

**Compara el comportamiento con `nmdm(4)`.** Si tu herramienta en userland funciona con `nmdm(4)` pero no con tu driver, el fallo está en el driver. Si falla con ambos, el fallo está en la herramienta.

Estas técnicas son aplicables tanto a drivers `uart(4)` como a drivers `ucom(4)`. La diferencia es que los problemas de `uart(4)` a menudo se reducen a la manipulación de registros (¿calculaste el divisor correctamente?), mientras que los problemas de `ucom(4)` a menudo se reducen a las transferencias USB (¿se completó realmente la transferencia de control para establecer la velocidad de baudios?). Las herramientas de depuración son distintas (USB: `usbconfig` y estadísticas de transferencia; UART: `sysctl` y registros del chip), pero la mentalidad de investigación es la misma.

### Cómo escribir un driver UART por tu cuenta

Juntando todas las piezas, un driver UART mínimo para un chip imaginario compatible con los registros estaría organizado así:

1. Define los desplazamientos de registro y las posiciones de bit en un encabezado local.
2. Implementa los seis métodos de `uart_ops`: `probe`, `init`, `term`, `putc`, `rxready`, `getc`.
3. Declara una `struct uart_ops` inicializada con esos seis métodos.
4. Declara una `struct uart_class` inicializada con las ops y los parámetros hardware (rango, reloj de referencia, desplazamiento de registro).
5. Implementa el manejador de interrupciones si el chip necesita más que el despacho por defecto del framework.
6. Registra el driver en Newbus usando los macros estándar.

La mayoría de los nuevos drivers UART en el árbol son pequeños. Los UARTs Oxford PCIe de un solo puerto, por ejemplo, tienen unos pocos cientos de líneas porque son fundamentalmente compatibles con el 16550 y solo necesitan una fina capa de código de attach específico de PCI. Los más complejos, como el Z8530, son más grandes porque el chip tiene un modelo de programación más complicado; el tamaño del driver refleja la complejidad del chip, no la del framework.

### Un vistazo a `myfirst_uart.c` en forma de esqueleto

Para nuestro ejemplo en curso, el esqueleto de un driver UART mínimo tiene este aspecto:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/kernel.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_bus.h>

#include "uart_if.h"

static int   myfirst_uart_probe(struct uart_bas *);
static void  myfirst_uart_init(struct uart_bas *, int, int, int, int);
static void  myfirst_uart_term(struct uart_bas *);
static void  myfirst_uart_putc(struct uart_bas *, int);
static int   myfirst_uart_rxready(struct uart_bas *);
static int   myfirst_uart_getc(struct uart_bas *, struct mtx *);

static struct uart_ops myfirst_uart_ops = {
    .probe   = myfirst_uart_probe,
    .init    = myfirst_uart_init,
    .term    = myfirst_uart_term,
    .putc    = myfirst_uart_putc,
    .rxready = myfirst_uart_rxready,
    .getc    = myfirst_uart_getc,
};

struct myfirst_uart_softc {
    struct uart_softc base;
    /* any chip-specific state would go here */
};

static kobj_method_t myfirst_uart_methods[] = {
    /* Most methods inherit from the framework. */
    { 0, 0 }
};

struct uart_class myfirst_uart_class = {
    "myfirst_uart class",
    myfirst_uart_methods,
    sizeof(struct myfirst_uart_softc),
    .uc_ops    = &myfirst_uart_ops,
    .uc_range  = 8,
    .uc_rclk   = 1843200,
    .uc_rshift = 0,
};
```

La inclusión de `uart_if.h` es destacable: ese encabezado es generado en tiempo de compilación por la maquinaria kobj a partir de la definición de interfaz en `/usr/src/sys/dev/uart/uart_if.m`. Declara los prototipos de los métodos que el framework espera que implementen los drivers. Cuando escribes un nuevo driver, dependes de este encabezado.

Los seis métodos en sí son sencillos una vez que tienes abierto el manual de programación del chip. `init` calcula el divisor a partir de `uc_rclk` y la velocidad de baudios, escribe el registro de control de línea para la combinación de bits de datos, bits de parada y paridad, habilita los FIFOs y establece el registro de habilitación de interrupciones con la máscara deseada. `term` invierte lo que hace `init`. `putc`, `getc` y `rxready` realizan cada uno un único acceso a registro más un spin sobre el registro de estado.

Una implementación completa de los seis métodos para un chip compatible con el 16550 tiene unas trescientas líneas. Para un chip con peculiaridades, podría crecer hasta quinientas o más. El driver `ns8250` es más largo que la mayoría porque gestiona erratas y detección de variantes para docenas de chips reales, pero la lógica central de sus seis métodos sigue siendo el patrón estándar.

### El framework `ucom(4)`: puentes USB a serie

No todo puerto serie es un UART real en el bus del sistema. Muchos son adaptadores USB: un PL2303, un CP2102, un FTDI FT232, un CH340G. Estos chips exponen un puerto serie a través de USB, y el enfoque de FreeBSD para darles soporte es un pequeño framework llamado `ucom(4)`. Reside en `/usr/src/sys/dev/usb/serial/`, junto a los drivers para cada familia de chips.

`ucom(4)` es distinto de `uart(4)`. No usa `uart_ops`, no usa `uart_bas` y no usa los ring buffers de `uart_core.c`. Lo que hace es proporcionar una abstracción TTY sobre las transferencias USB. Un cliente de `ucom(4)` se declara a través de una `struct ucom_callback`:

```c
struct ucom_callback {
    void (*ucom_cfg_get_status)(struct ucom_softc *, uint8_t *, uint8_t *);
    void (*ucom_cfg_set_dtr)(struct ucom_softc *, uint8_t);
    void (*ucom_cfg_set_rts)(struct ucom_softc *, uint8_t);
    void (*ucom_cfg_set_break)(struct ucom_softc *, uint8_t);
    void (*ucom_cfg_set_ring)(struct ucom_softc *, uint8_t);
    void (*ucom_cfg_param)(struct ucom_softc *, struct termios *);
    void (*ucom_cfg_open)(struct ucom_softc *);
    void (*ucom_cfg_close)(struct ucom_softc *);
    int  (*ucom_pre_open)(struct ucom_softc *);
    int  (*ucom_pre_param)(struct ucom_softc *, struct termios *);
    int  (*ucom_ioctl)(struct ucom_softc *, uint32_t, caddr_t, int,
                      struct thread *);
    void (*ucom_start_read)(struct ucom_softc *);
    void (*ucom_stop_read)(struct ucom_softc *);
    void (*ucom_start_write)(struct ucom_softc *);
    void (*ucom_stop_write)(struct ucom_softc *);
    void (*ucom_tty_name)(struct ucom_softc *, char *pbuf, uint16_t buflen,
                         uint16_t unit, uint16_t subunit);
    void (*ucom_poll)(struct ucom_softc *);
    void (*ucom_free)(struct ucom_softc *);
};
```

Los métodos se dividen en tres grupos. Los métodos de configuración (cuyos nombres llevan el prefijo `ucom_cfg_`) se llaman para cambiar el estado del chip subyacente: activar DTR, activar RTS, cambiar la velocidad de baudios, etc. Estos métodos se ejecutan en el thread de configuración del framework, que está diseñado para realizar peticiones de control USB síncronas. Los métodos de flujo (`ucom_start_read`, `ucom_start_write`, `ucom_stop_read`, `ucom_stop_write`) se llaman para habilitar o deshabilitar el camino de datos en los canales USB subyacentes. Los pre-métodos (`ucom_pre_open`, `ucom_pre_param`) se ejecutan en el contexto del llamador antes de que el framework planifique una tarea de configuración; es aquí donde un driver valida los argumentos suministrados desde userland y devuelve un errno si son inaceptables. El método `ucom_ioctl` traduce los ioctls específicos del chip procedentes de userland que el framework no gestiona en peticiones USB.

El trabajo de un driver USB-a-serie consiste en implementar estos callbacks en términos de transferencias USB. Cuando se llama a `ucom_cfg_param` con una nueva velocidad de baudios, el driver emite una transferencia de control específica del fabricante que programa el registro de velocidad de baudios del chip. Cuando se llama a `ucom_start_read`, el driver inicia un canal bulk-IN que entrega los bytes entrantes. Cuando se llama a `ucom_start_write`, el driver inicia un canal bulk-OUT que envía los bytes salientes.

El driver FTDI en `/usr/src/sys/dev/usb/serial/uftdi.c` es la referencia concreta. Su implementación de `ucom_cfg_param` traduce los campos de termios al formato propietario de divisor de velocidad de baudios de FTDI (que resulta peculiar, ya que los chips FTDI utilizan un esquema de divisor sub-entero que se parece al 16550 estándar pero no es exactamente igual) y emite una transferencia de control a `bRequest = FTDI_SIO_SET_BAUD_RATE`. Su `ucom_start_read` inicia el canal bulk-IN que lee del RX FIFO del chip FTDI. Su `ucom_start_write` inicia el canal bulk-OUT que escribe en el TX FIFO del chip FTDI.

Desde la perspectiva del userland, un dispositivo `ucom(4)` es idéntico a un dispositivo `uart(4)`. Ambos aparecen como `/dev/ttyuN` y `/dev/cuauN`. Ambos responden a `stty`, `cu`, `tip`, `minicom` y cualquier otra herramienta de comunicación serie. Ambos admiten los mismos flags de termios. La distinción solo importa al desarrollador de drivers.

### Lectura de `uftdi.c` como ejemplo completo

Los chips FTDI (FT232R, FT232H, FT2232H y muchos otros) son los chips USB-a-serie más extendidos en el mundo del hardware embebido. Si alguna vez trabajas con microcontroladores, placas de evaluación, impresoras 3D o sensores industriales, te encontrarás con hardware FTDI. FreeBSD lleva soportando FTDI desde la versión 4.x, y el driver actual reside en `/usr/src/sys/dev/usb/serial/uftdi.c`. Con unas tres mil líneas aproximadamente, no es precisamente corto, pero la mayor parte de esa extensión está dedicada a la enorme tabla de coincidencias (los productos FTDI son numerosísimos) y a las particularidades de las variantes de chip (cada pocos años FTDI añade un nuevo tamaño de FIFO, un nuevo esquema de divisor de velocidad o un nuevo registro). El núcleo de interés pedagógico ocupa apenas unos cientos de líneas, y leerlo es la recompensa directa al trabajo conceptual de la sección 4.

Cuando abres el archivo, lo primero que llama la atención es la enorme tabla de coincidencias. FTDI asigna identificadores USB específicos para OEM a sus clientes, de modo que la tabla de coincidencias incluye no solo los pares VID/PID propios de FTDI, sino también cientos de VIDs y PIDs de empresas que integran chips FTDI en sus productos. Sparkfun, Pololu, Olimex, Adafruit, varios fabricantes industriales: todos tienen al menos una entrada en la tabla de coincidencias de uftdi. El array `STRUCT_USB_HOST_ID` tiene varios cientos de entradas, agrupadas con comentarios que indican a qué familia de producto pertenece cada bloque.

A continuación viene el softc. Un softc de FTDI incluye el puntero al dispositivo USB, un mutex, el array de transferencias para los canales bulk-IN y bulk-OUT (los dispositivos FTDI usan bulk para los datos, no interrupt), un `struct ucom_super_softc` para la capa `ucom(4)`, un `struct ucom_softc` para el estado por puerto, y campos específicos de FTDI: el divisor de velocidad en baudios actual, el contenido actual del registro de control de línea, el contenido actual del registro de control de módem, y algunos indicadores para la familia de variantes (FT232, FT2232, FT232H, etc.). Cada variante requiere un código ligeramente diferente en ciertas operaciones, por lo que el driver guarda un identificador de variante en el softc y lo utiliza para bifurcar en las operaciones que difieren.

El array de configuración de transferencias es donde se declara la interacción del driver FTDI con el framework USB. Declara dos canales: `UFTDI_BULK_DT_RD` para los datos entrantes y `UFTDI_BULK_DT_WR` para los salientes. Cada uno es una transferencia `UE_BULK` con un tamaño de buffer moderado (el valor predeterminado de FTDI es 64 bytes para baja velocidad y 512 bytes para velocidad completa, y el driver elige el tamaño adecuado durante el attach en función de la variante del chip). Los callbacks son `uftdi_read_callback` y `uftdi_write_callback`, y siguen exactamente el patrón de tres estados descrito en la sección 3.

La siguiente estructura importante es `ucom_callback`. Conecta el driver FTDI con el framework `ucom(4)`. Los métodos que proporciona incluyen `uftdi_cfg_param` (llamado cuando cambia la velocidad en baudios o el formato de byte), `uftdi_cfg_set_dtr` (llamado para activar o desactivar DTR), `uftdi_cfg_set_rts` (igual para RTS), `uftdi_cfg_open` y `uftdi_cfg_close` (llamados cuando un proceso del espacio de usuario abre o cierra el dispositivo), y `uftdi_start_read`, `uftdi_start_write`, `uftdi_stop_read`, `uftdi_stop_write` (llamados para habilitar o deshabilitar los canales de datos). Cada método de configuración traduce una operación de alto nivel en una transferencia de control USB hacia el chip FTDI.

La programación de la velocidad en baudios es una de las partes más instructivas del driver, porque los chips FTDI usan un esquema de divisor peculiar. En lugar de los divisores enteros limpios que usa un UART 16550, FTDI admite un divisor fraccional en el que el numerador es un entero y el denominador se calcula a partir de dos bits que seleccionan un octavo, un cuarto, tres octavos, la mitad o cinco octavos. La función `uftdi_encode_baudrate` recibe la velocidad en baudios solicitada y el reloj de referencia del chip, y calcula el divisor válido más próximo. Gestiona los casos extremos (velocidades muy bajas, velocidades muy altas en chips más nuevos, velocidades estándar como 115200 que se representan de forma exacta, velocidades no estándar como 31250 utilizadas por MIDI). El valor de dieciséis bits resultante se pasa a `uftdi_set_baudrate`, que emite una transferencia de control al registro de velocidad en baudios del FTDI.

El registro de control de línea (bits de datos, bits de parada, paridad) se programa mediante una secuencia similar: la estructura termios llega a `uftdi_cfg_param`, el driver extrae los bits relevantes, los codifica en el formato de control de línea del FTDI y emite una transferencia de control.

Las señales de control del módem (DTR, RTS) se programan mediante `uftdi_cfg_set_dtr` y `uftdi_cfg_set_rts`. Son las transferencias más sencillas: un control-out sin carga útil que el chip interpreta como "establecer DTR a X" o "establecer RTS a Y".

El camino de datos reside en los dos callbacks. `uftdi_read_callback` gestiona el canal bulk-IN. Cuando recibe `USB_ST_TRANSFERRED`, extrae los bytes recibidos del frame USB (ignorando los primeros dos bytes, que son bytes de estado del FTDI) y los entrega a la capa `ucom(4)` para su envío al espacio de usuario. Ante `USB_ST_SETUP`, rearma la lectura para otro buffer. `uftdi_write_callback` gestiona el canal bulk-OUT. Ante `USB_ST_SETUP`, solicita más datos a la capa `ucom(4)`, los copia en un frame USB y envía la transferencia. Ante `USB_ST_TRANSFERRED`, rearma para comprobar si hay más datos.

Al leer `uftdi.c` con el vocabulario de la sección 4 en mente, puedes ver cómo el patrón completo del framework `ucom(4)` se instancia para un chip concreto. La lógica específica del FTDI (codificación de velocidad en baudios, codificación de control de línea, configuración del control de módem) queda aislada en funciones auxiliares. La integración con el framework la gestiona la estructura `ucom_callback`. El flujo de datos lo gestionan las dos transferencias bulk. Si estuvieras escribiendo un driver para un chip USB-a-serie diferente, copiarías esta estructura y cambiarías las partes específicas del chip.

La existencia de este driver explica algo que, de otro modo, podría resultar desconcertante. ¿Por qué añadió FreeBSD `ucom(4)` como un framework independiente en lugar de integrarlo en `uart(4)`? Porque toda la maquinaria del camino de datos de un driver `uart(4)` (manejadores de interrupciones, buffers circulares, accesos a registros) no tiene equivalente en el mundo USB-a-serie. El "FIFO" del chip FTDI es un buffer integrado en el propio chip al que el driver no puede acceder directamente; solo puede enviar paquetes bulk al chip y recibirlos de vuelta. La maquinaria de `uart(4)` sería una sobrecarga innecesaria. Al disponer de `ucom(4)` como un framework independiente con sus propias abstracciones del camino de datos, FreeBSD consigue que un driver USB-a-serie como `uftdi` tenga apenas unos cientos de líneas de lógica central, en lugar de envolver una capa innecesaria de emulación 16550.

Cuando termines de leer `uftdi.c`, abre `uplcom.c` (el driver para el Prolific PL2303) y `uslcom.c` (el driver para el Silicon Labs CP210x) en ese orden. Ambos siguen la misma estructura con detalles específicos de cada chip. Tras leer los tres, tendrás una comprensión funcional de cómo se organiza un driver USB-a-serie en FreeBSD, y estarás preparado para escribir uno para cualquier chip que te encuentres.

### Cómo elegir entre `uart(4)` y `ucom(4)`

La elección es mecánica. Si el chip está en el bus del sistema (PCI, ISA, un puerto I/O de plataforma, un periférico SoC mapeado en memoria), escribes un driver `uart(4)`. Si el chip está en USB y expone una interfaz serie, escribes un driver `ucom(4)`.

Los dos frameworks no se mezclan. No puedes tomar un driver `uart(4)` y conectarlo a USB, ni puedes tomar un driver `ucom(4)` y adjuntarlo a PCIe. Son implementaciones independientes de la misma abstracción visible para el usuario (un puerto TTY), pero con mecanismos internos muy diferentes.

Los principiantes a veces se preguntan por qué existen los dos frameworks, en lugar de un único framework serie con una capa de transporte intercambiable. La respuesta es histórica: `uart(4)` se reescribió en su forma moderna a principios de los años 2000 para reemplazar al antiguo driver `sio(4)`, y en aquel momento el soporte para serie USB era un conjunto de drivers ad hoc. Cuando se unificó el soporte para serie USB, el enfoque natural fue añadir una delgada capa de integración TTY (`ucom(4)`) en lugar de adaptar `uart(4)`. Los dos son ahora independientes porque desacoplarlos ha resultado estable y útil. Un esfuerzo de unificación sería un proyecto considerable con un beneficio modesto.

Para ti, como escritor de drivers principiante, la regla es sencilla. Si estás escribiendo un driver para un chip que reside en los puertos serie de tu placa base o en una tarjeta PCIe, usa `uart(4)`. Si estás escribiendo un driver para un dongle USB que pretende ser un puerto serie, usa `ucom(4)`. Los drivers de referencia para cada caso (`ns8250` para `uart(4)`, `uftdi` para `ucom(4)`) son el lugar adecuado para aprender los detalles.

### Diferencias entre variantes de chip

Trabajar con hardware UART real te enseña rápidamente que "compatible con 16550" es un espectro, no una especificación fija. A continuación se presentan las variantes que es más probable que encuentres y las diferencias que importan.

**8250.** El original, de finales de los años 70. No tiene FIFO; el CPU debe recoger cada byte recibido antes de que llegue el siguiente. El software para 16550A funcionará normalmente, con un rendimiento reducido.

**16450.** Similar al 8250 pero con algunas mejoras en los registros y un comportamiento ligeramente más fiable. Tampoco tiene FIFO.

**16550.** Introdujo un FIFO de 16 bytes, pero el 16550 original tenía un comportamiento erróneo del FIFO. El software debe detectar esto y negarse a usar el FIFO en el caso defectuoso.

**16550A.** Corrigió los errores del FIFO. Este es el "16550" canónico al que apunta todo driver serie de PC. Fiable y ampliamente compatible.

**16550AF.** Revisiones adicionales para la temporización y los márgenes. A efectos de software, idéntico al 16550A.

**16650.** Amplió el FIFO a 32 bytes y añadió control de flujo por hardware automático. En su mayor parte compatible con el 16550A.

**16750.** Amplió el FIFO a 64 bytes. Algunos chips con esta etiqueta también tienen modos adicionales de autobaud y alta velocidad. El software debe decidir si habilitar el FIFO ampliado.

**16950 (Oxford Semiconductor).** Un FIFO de 128 bytes, características adicionales de control de flujo y soporte para velocidades de baudios inusuales mediante un esquema de divisor modificado. Se encuentra habitualmente en tarjetas serie PCIe de alto rendimiento.

**Controladores SoC compatibles con UART.** Muchos procesadores embebidos tienen UARTs integradas que son compatibles en registros con el 16550, pero con particularidades: algunos tienen diferentes frecuencias de reloj, otros tienen diferentes desplazamientos de registro, otros tienen soporte DMA, y otros tienen semánticas de interrupción distintas. El driver `ns8250` de FreeBSD sondea estas variantes durante el attach y ajusta su comportamiento en consecuencia.

La lógica de probe del driver `ns8250` lee varios registros para determinar qué variante está presente. Comprueba los bits del IIR que vimos antes, lee el registro de control del FIFO para ver qué tamaño de FIFO se informa, comprueba los marcadores de identificación del 16650/16750/16950 y registra el resultado en un campo de variante en el softc. El cuerpo del driver bifurca entonces sobre este campo en los pocos lugares donde las variantes difieren.

Cuando escribas un driver para un nuevo UART, decide desde el principio si quieres apuntar a una única variante o a una familia. Apuntar a una única variante es más sencillo, pero limita el hardware que puedes soportar. Apuntar a una familia requiere lógica de detección de variantes como la de `ns8250`.

### El camino de la consola

FreeBSD puede utilizar un puerto serie como consola. Esto es especialmente útil para sistemas embebidos sin pantalla, para servidores sin teclado ni monitor, y para la depuración del kernel (de modo que la salida de `printf` vaya a algún lugar visible incluso cuando el driver de pantalla esté roto).

El camino de la consola está estrechamente integrado con `uart(4)`. Un UART designado como consola se sondea al principio del boot, antes de que se haya inicializado la mayor parte del kernel. Los métodos putc y getc de la consola se utilizan para emitir mensajes de boot y para leer la entrada de teclado en tiempo de boot. Solo tras la puesta en marcha completa del kernel se adjunta el UART a la capa TTY de la forma habitual.

Dos mecanismos determinan qué puerto es la consola. El bootloader puede establecer una variable (típicamente `console=comconsole`) en el entorno, que el kernel lee al arrancar. De forma alternativa, el kernel puede configurarse en tiempo de compilación con un puerto específico como consola (mediante `options UART_EARLY_CONSOLE` en un archivo de configuración del kernel).

Cuando un puerto actúa como consola, permanece activo aunque se descargue el driver o se produzca el detach. No puedes descargar `uart` ni deshabilitar el puerto de consola sin perder la salida de consola. Esta restricción está impuesta en el framework de `uart(4)` y normalmente es invisible para quienes escriben drivers (no necesitas tratar el puerto de consola como un caso especial), pero conviene tenerla en cuenta por si aparecen comportamientos extraños relacionados con la consola durante las pruebas.

### Comparación de drivers UART en distintas arquitecturas

Una de las fortalezas de FreeBSD es que el mismo framework `uart(4)` funciona en múltiples arquitecturas. Un portátil `x86_64` con un 16550 en una tarjeta PCIe, una Raspberry Pi `aarch64` con un UART PL011 integrado en el chip, y una placa de desarrollo `riscv64` con un UART específico de SiFive exponen todos la misma interfaz TTY al espacio de usuario. Solo difiere el driver.

A continuación se presenta un repaso rápido de los drivers UART incluidos en FreeBSD 14.3:

- `uart_dev_ns8250.c`: la familia 16550 para x86 y muchas otras plataformas.
- `uart_dev_pl011.c`: el UART ARM PrimeCell PL011, utilizado en Raspberry Pi y muchos SoC ARM.
- `uart_dev_imx.c`: el UART NXP i.MX, utilizado en placas ARM basadas en i.MX.
- `uart_dev_z8530.c`: el Zilog Z8530, utilizado históricamente en estaciones de trabajo SPARC.
- `uart_dev_ti8250.c`: una variante TI del 16550 con características adicionales.
- `uart_dev_pl011.c` (variante sbsa): el UART ARM estandarizado por SBSA para hardware ARM de clase servidor.
- `uart_dev_snps.c`: el UART Synopsys DesignWare, utilizado en muchas placas RISC-V.

Abre dos cualesquiera de estos archivos y compara sus implementaciones de `uart_ops` una junto a la otra. La estructura es idéntica: seis métodos, cada uno apuntando a una función que lee o escribe registros específicos del chip. Los detalles específicos del chip difieren, pero la API del framework es la misma.

Este es el beneficio del diseño por capas. Un nuevo driver UART es un proyecto acotado: unas pocas centenas de líneas de código que reutilizan toda la gestión de buffers y la integración TTY del framework. Si FreeBSD tuviera que reimplementar la gestión de buffers para cada UART, el sistema sería mucho más grande y mucho más difícil de verificar.

### ¿Y el estándar USB CDC ACM?

USB dispone de una clase estándar para dispositivos serie, denominada CDC ACM (Communication Device Class, Abstract Control Model). Los chips que implementan CDC ACM se anuncian con una tripleta específica de clase/subclase/protocolo en el nivel de interfaz, y pueden ser controlados por un único driver genérico en lugar de uno específico del fabricante. El driver CDC ACM genérico de FreeBSD es `u3g.c`, ubicado en `/usr/src/sys/dev/usb/serial/`, y también está construido sobre `ucom(4)`.

Muchos chips USB serie modernos implementan CDC ACM, por lo que el driver genérico funciona directamente sin necesidad de un archivo específico del fabricante. Otros (como FTDI) utilizan protocolos propietarios que requieren un driver específico del fabricante. La tripleta clase/subclase/protocolo del descriptor de interfaz es la que te indica en qué caso te encuentras; `usbconfig -d ugenN.M dump_all_config_desc` la mostrará.

Cuando vayas a adquirir un adaptador serie USB para trabajo de desarrollo, prefiere chips que implementen CDC ACM. Son más baratos, más portables y no requieren drivers propietarios. Los chips FTDI han dominado históricamente el desarrollo embebido por su fiabilidad, y FreeBSD los soporta bien, pero un CP2102 o CH340G moderno funcionando en modo CDC ACM es igualmente utilizable.

### Cerrando la sección 4

La sección 4 te ha dado una imagen completa de cómo funcionan los drivers serie en FreeBSD. Has visto las capas: `uart(4)` en el nivel del framework, `ttydevsw` en el nivel de integración TTY, `uart_ops` en el nivel del hardware. Has visto la distinción entre `uart(4)` para los UART conectados al bus y `ucom(4)` para los puentes USB a serie, y la regla práctica para decidir cuál usar. Has visto, a alto nivel, los seis métodos de hardware que implementa un driver UART, los callbacks de configuración que implementa un driver USB a serie, y cómo la capa TTY se asienta sobre ambos con una interfaz uniforme hacia el espacio de usuario.

El nivel de profundidad de esta sección es necesariamente menor que el de las secciones dedicadas a USB, porque los drivers serie en FreeBSD son más especializados que los drivers USB y es más probable que vayas a leer uno existente (o a modificar uno) que a escribir uno completamente nuevo desde cero. Si te encuentras en la situación de escribir un nuevo driver UART para una placa personalizada, el camino está claro: abre `ns8250` en una ventana, el datasheet de tu chip en otra, y escribe los seis métodos uno a uno.

Dos conclusiones clave enmarcan la sección 5. En primer lugar, probar drivers serie no requiere hardware real. FreeBSD incluye un driver null-modem `nmdm(4)` que crea pares de TTY virtuales que puedes conectar entre sí, lo que te permite ejercitar cambios de termios, control de flujo y flujo de datos sin enchufar nada. En segundo lugar, probar drivers USB sin hardware es más difícil pero no imposible: puedes usar QEMU con redirección USB para probar contra dispositivos reales a través de una VM, o puedes usar el modo gadget USB de FreeBSD para que una máquina se presente a otra como un dispositivo USB. La sección 5 cubre ambas posibilidades. El objetivo es habilitar un ciclo de desarrollo que no dependa de la manipulación de cables ni de conectar y desconectar cosas.

## Sección 5: Pruebas de drivers USB y serie sin hardware real

### Por qué existe esta sección

Un escritor de drivers principiante a menudo se queda atascado en el mismo obstáculo. Escribe un driver, lo compila, quiere probarlo y descubre que no tiene el hardware, que el hardware se comporta mal, que el hardware está en la máquina equivocada, o que el ciclo de iteración de «cambia el código, enchúfalo, observa qué ocurre, desenchúfalo, vuelve a cambiar el código» es dolorosamente lento y poco fiable. La sección 5 aborda esto directamente. FreeBSD proporciona varios mecanismos que te permiten ejercitar los caminos de código de un driver sin hardware físico, y conocer estos mecanismos te ahorrará horas de frustración.

El objetivo no es fingir que el hardware está presente cuando no lo está. El objetivo es darte herramientas que cubran las partes del desarrollo de drivers en las que el hardware es accesorio, de modo que cuando conectes hardware real, ya sepas que la lógica de tus caminos de código es correcta y solo estés validando la interacción física. Depurar una peculiaridad a nivel de registro es más rápido cuando sabes que tu locking, tus máquinas de estado y tu interfaz de usuario ya son correctos.

Esta sección cubre cuatro de estos mecanismos: el driver null-modem `nmdm(4)` para pruebas de serie, herramientas básicas del espacio de usuario para ejercitar TTYs (`cu`, `tip`, `stty`, `comcontrol`), QEMU con redirección USB para pruebas de drivers USB, y el modo gadget USB de FreeBSD para presentar una máquina como dispositivo USB ante otra. Concluye con una breve discusión sobre técnicas que no requieren ninguna herramienta especial: tests unitarios en la capa funcional, disciplina de logging y desarrollo guiado por aserciones.

### El driver null-modem `nmdm(4)`

`nmdm(4)` es un módulo del kernel que crea pares de TTY virtuales enlazados. Cuando escribes en un lado, sale por el otro, exactamente como si hubieras conectado dos puertos serie reales con un cable null-modem. El driver se encuentra en `/usr/src/sys/dev/nmdm/nmdm.c`, y se carga con:

```console
# kldload nmdm
```

Una vez cargado, puedes instanciar pares bajo demanda simplemente abriéndolos. Ejecuta:

```console
# cu -l /dev/nmdm0A
```

Esto abre el lado `A` del par `0`. En otro terminal, ejecuta:

```console
# cu -l /dev/nmdm0B
```

Lo que escribas en una sesión de `cu` aparecerá en la otra. Acabas de crear un par de TTY virtuales sin hardware de por medio. Puedes cambiar las velocidades de transmisión con `stty` y el cambio se notará en ambos lados. Puedes activar DTR y CTS mediante ioctls y observar el efecto en el otro lado.

La utilidad de `nmdm(4)` para el desarrollo de drivers es doble. En primer lugar, si estás escribiendo un usuario de la capa TTY (por ejemplo, un driver que lanza un shell en un TTY virtual, o un programa en espacio de usuario que implementa un protocolo sobre un TTY), puedes probarlo de extremo a extremo contra `nmdm(4)` sin ningún hardware. En segundo lugar, si estás escribiendo un driver `ucom(4)` o `uart(4)`, puedes comparar su comportamiento con el de `nmdm(4)` ejecutando el mismo test del espacio de usuario contra ambos. Si tu driver se comporta mal donde `nmdm(4)` no lo hace, el error está en tu driver; si ambos se comportan mal, el error está probablemente en tu test del espacio de usuario.

Una pequeña advertencia: `nmdm(4)` no simula los retardos de la velocidad de transmisión. Lo que escribas sale por el otro lado a la velocidad de la memoria. Esto es generalmente lo que deseas (no quieres esperar a que una transmisión real a 9600 baudios complete una carga de prueba de cien kilobytes), pero implica que los protocolos sensibles al tiempo no pueden probarse únicamente con `nmdm(4)`.

### El conjunto de herramientas `cu(1)`, `tip(1)` y `stty(1)`

Tanto si usas `nmdm(4)`, un UART real o un dongle USB a serie, las herramientas del espacio de usuario que utilizas para interactuar con un TTY son las mismas. Las tres más importantes son `cu(1)`, `tip(1)` y `stty(1)`.

`cu` es el clásico programa «call up». Abre un TTY, pone el terminal en modo raw y te permite enviar bytes al puerto y ver los bytes que regresan. Para abrir un puerto a una velocidad de transmisión específica:

```console
# cu -l /dev/cuau0 -s 115200
```

El argumento `-l` especifica el dispositivo, y `-s` especifica la velocidad de transmisión. `cu` admite un conjunto de secuencias de escape (todas comenzando con `~`) para salir, enviar archivos y operaciones similares; `~.` es la secuencia de escape estándar para salir y `~?` lista las demás.

`tip` es una herramienta relacionada con semántica similar pero un mecanismo de configuración diferente. `tip` lee `/etc/remote` para las entradas de conexión con nombre y puede aceptar un nombre como argumento en lugar de una ruta de dispositivo. Para la mayoría de los propósitos, `cu` y `tip` son intercambiables; `cu` es más cómodo para usos puntuales.

`stty` imprime o cambia los parámetros termios de un TTY. Ejecuta `stty -a -f /dev/ttyu0` para ver todos los flags termios del puerto. Ejecuta `stty 115200 -f /dev/ttyu0` para establecer la velocidad de transmisión. Ejecuta `stty cs8 -parenb -cstopb -f /dev/ttyu0` para configurar ocho bits de datos, sin paridad, un bit de parada (la configuración más habitual en el trabajo embebido moderno). La página del manual es extensa, y los flags se mapean casi directamente sobre los bits de `c_cflag`, `c_iflag`, `c_lflag` y `c_oflag` en la estructura `termios`.

Usar estas tres herramientas conjuntamente te ofrece una forma flexible de examinar tu driver desde el espacio de usuario. Puedes cambiar la configuración con `stty`, abrir el puerto con `cu`, enviar y recibir bytes, cerrar el puerto, comprobar el estado con `stty` de nuevo, y repetir. Si la implementación de `tsw_param` de tu driver tiene un error, `stty` lo pondrá de manifiesto: la configuración que establezcas no se leerá correctamente, o el puerto se comportará de forma distinta a la solicitada.

### La utilidad `comcontrol(8)`

`comcontrol` es una utilidad especializada para puertos serie. Establece parámetros específicos del puerto que no están expuestos a través de termios. Los dos más importantes son `drainwait` y las opciones específicas de RS-485. Para las pruebas de drivers de principiantes, el uso más habitual es inspeccionar el estado del puerto: `comcontrol /dev/ttyu0` muestra las señales del módem actuales (DTR, RTS, CTS, DSR, CD, RI) y el `drainwait` actual. También puedes establecer las señales:

```console
# comcontrol /dev/ttyu0 dtr rts
```

establece DTR y RTS. Esto es útil para probar el manejo del control de flujo sin escribir un programa personalizado.

### La utilidad `usbconfig(8)`

En el lado USB, `usbconfig(8)` es la navaja suiza. La usaste al final de la sección 1 para inspeccionar los descriptores de un dispositivo. Varios otros subcomandos son útiles durante el desarrollo de drivers:

- `usbconfig list`: enumera todos los dispositivos USB conectados.
- `usbconfig -d ugenN.M dump_all_config_desc`: imprime todos los descriptores de un dispositivo.
- `usbconfig -d ugenN.M dump_device_quirks`: imprime los quirks aplicados por el framework USB.
- `usbconfig -d ugenN.M dump_stats`: imprime estadísticas por transferencia.
- `usbconfig -d ugenN.M suspend`: pone el dispositivo en estado de suspensión USB.
- `usbconfig -d ugenN.M resume`: lo despierta.
- `usbconfig -d ugenN.M reset`: reinicia físicamente el dispositivo.

El comando `reset` es especialmente útil durante el desarrollo. Un driver en prueba puede dejar fácilmente un dispositivo en un estado confuso; `usbconfig reset` devuelve el dispositivo a la condición de recién conectado sin necesidad de desconectarlo físicamente.

### Pruebas de drivers USB con QEMU

QEMU, el emulador genérico de CPU, tiene un sólido soporte USB. Puedes ejecutar un sistema invitado FreeBSD dentro de QEMU y redirigir dispositivos USB reales del host al invitado. Esta es la técnica más útil para el desarrollo de drivers USB, porque te permite probar contra hardware real manteniendo toda la velocidad de iteración de trabajar dentro de una VM.

En un host FreeBSD, instala QEMU desde ports:

```console
# pkg install qemu
```

Instala una imagen de invitado FreeBSD en un archivo de disco (el proceso se explica en el Capítulo 4 y el Apéndice A). Cuando arranques el invitado, añade las opciones de redirección USB:

```console
qemu-system-x86_64 \
  -drive file=freebsd.img,format=raw \
  -m 1024 \
  -device nec-usb-xhci,id=xhci \
  -device usb-host,bus=xhci.0,vendorid=0x0403,productid=0x6001
```

La línea `-device nec-usb-xhci` añade un controlador USB 3.0 al invitado. La línea `-device usb-host` conecta un dispositivo USB específico del host (identificado por fabricante y producto) a ese controlador. Cuando el invitado arranca, el dispositivo aparece en el bus USB del invitado y puede ser enumerado por el kernel del invitado.

Esta configuración te proporciona el ciclo de iteración completo dentro de la VM. Puedes cargar tu driver, descargarlo y volver a cargar una versión reconstruida, todo sin manipular físicamente ningún cable. Puedes usar la consola serie o la red para interactuar con la VM. Puedes tomar un snapshot del estado de la VM antes de una prueba arriesgada y revertirlo si la prueba provoca un pánico.

La principal limitación es el soporte de transferencias USB isócronas, que resulta menos estable en los emuladores. Para las transferencias bulk, interrupt y control (los tres tipos que usa la mayoría de los drivers), la redirección USB de QEMU es lo suficientemente fiable como para ser tu entorno de desarrollo principal.

### Modo gadget USB de FreeBSD

Si QEMU no está disponible y tienes dos máquinas FreeBSD, hay otra opción: `usb_template(4)` y el soporte de doble rol USB de cierto hardware te permiten hacer que una máquina se presente ante otra como un dispositivo USB. La máquina anfitriona ve un periférico USB normal; la máquina gadget ejecuta realmente el lado dispositivo del protocolo USB.

Este es un tema avanzado y el soporte de hardware es variable. En plataformas x86 con chipsets compatibles con USB On-The-Go, en algunas placas ARM y en configuraciones embebidas específicas, la configuración funciona. En la mayor parte del hardware de escritorio, no. Todos los detalles técnicos se encuentran en `/usr/src/sys/dev/usb/template/` y en la página de manual `usb_template(4)`.

Si tienes el hardware necesario para usar esta técnica, es lo más parecido a una prueba completa de extremo a extremo de un driver USB sin periféricos físicos. Si no lo tienes, no la explores para un proyecto de aprendizaje; usa QEMU en su lugar.

### Técnicas que no requieren herramientas especiales

Más allá de los marcos descritos anteriormente, existen varias técnicas que dependen únicamente de un buen diseño del driver.

En primer lugar, diseña tu driver de modo que las partes independientes del hardware puedan comprobarse mediante pruebas unitarias en userland. Si tu driver tiene un analizador de protocolo, una máquina de estados o un calculador de sumas de verificación, factorízalos en funciones que tomen buffers de C simples y devuelvan resultados de C simples. Luego puedes compilar esas funciones en un programa de prueba en userland y ejecutarlas con entradas conocidas. Esto detecta muchos bugs antes de que lleguen al kernel.

En segundo lugar, registra agresivamente durante el desarrollo y de forma discreta en producción. La macro `DLOG_RL` del Capítulo 25 es tu aliada: te permite emitir mensajes de diagnóstico frecuentes durante el desarrollo, con un sysctl para suprimirlos en producción. La limitación de velocidad previene las tormentas de mensajes de registro si algo va mal.

En tercer lugar, usa aserciones para los invariantes. `KASSERT(cond, ("message", args...))` provocará un panic del kernel si `cond` es falso, pero solo en kernels con `INVARIANTS`. Puedes ejecutar tu driver en un kernel con `INVARIANTS` durante el desarrollo y en un kernel de producción después, sin cambiar el código. La discusión de `INVARIANTS` en el Capítulo 20 es la referencia al respecto.

En cuarto lugar, sé riguroso con las pruebas de concurrencia. Usa `INVARIANTS` junto con `WITNESS` (que rastrea el orden de los locks) durante el desarrollo. Si tu driver tiene un bug de locking que casi siempre funciona pero que ocasionalmente provoca un deadlock, `WITNESS` lo detectará en la primera ocurrencia.

En quinto lugar, escribe un cliente de userland simple para tu driver y úsalo como parte de tu ciclo de desarrollo. Incluso un programa de diez líneas que abra el dispositivo, escriba una cadena conocida, lea una respuesta conocida y compruebe el resultado es enormemente útil. Puedes ejecutarlo en un bucle durante las pruebas de estrés, puedes ejecutarlo con `ktrace -f cmd` para obtener una traza de las llamadas al sistema, y puedes ejecutarlo bajo un depurador si algo te sorprende.

### Guía paso a paso de la redirección USB de QEMU

El soporte USB de QEMU es la herramienta más útil para el desarrollo de drivers USB, así que un recorrido más detallado está justificado. Supón que quieres desarrollar un driver para un adaptador FT232 concreto. Tu máquina anfitriona es una máquina FreeBSD 14.3 y quieres ejecutar tu driver en una VM FreeBSD 14.3 huésped dentro de QEMU.

Primero, instala QEMU y crea una imagen de disco para el huésped:

```console
# pkg install qemu
# truncate -s 16G guest.img
```

Instala FreeBSD en la imagen. El procedimiento exacto se describe en el Apéndice A, pero la versión resumida es: arranca un ISO de instalación de FreeBSD como CD-ROM, instala en la imagen de disco y reinicia.

Una vez instalado el huésped, localiza el dispositivo USB de la máquina anfitriona que quieres redirigir. Conecta el FT232 y anota los IDs de fabricante y producto con `usbconfig list`:

```text
ugen0.3: <FTDI FT232R USB UART> at usbus0
```

`usbconfig -d ugen0.3 dump_device_desc` mostrará `idVendor = 0x0403` e `idProduct = 0x6001`.

Ahora inicia QEMU con redirección USB:

```console
qemu-system-x86_64 \
  -enable-kvm \
  -cpu host \
  -m 2048 \
  -drive file=guest.img,format=raw \
  -device nec-usb-xhci,id=xhci \
  -device usb-host,bus=xhci.0,vendorid=0x0403,productid=0x6001 \
  -net user -net nic
```

La línea `-device nec-usb-xhci` añade un controlador USB 3.0 a la VM. La línea `-device usb-host` redirige hacia la VM el dispositivo de la máquina anfitriona que coincide. Cuando la VM arranque, el FT232 aparecerá como si estuviera conectado directamente al puerto USB de la VM.

Dentro de la VM, ejecuta `dmesg` y busca el attach USB:

```text
uhub0: 4 ports with 4 removable, self powered
uftdi0 on usbus0
uftdi0: <FTDI FT232R USB UART, class 255/0, rev 2.00/6.00, addr 2> on usbus0
```

Tu driver (ya sea `uftdi` o tu propio trabajo en curso) verá un FT232 real con descriptores reales, comportamiento de transferencia real y peculiaridades reales. Puedes descargar y recargar tu driver dentro de la VM sin desconectar nada; puedes ejecutar el kernel con `INVARIANTS` y `WITNESS` sin preocuparte por el impacto en el lado anfitrión; puedes hacer una instantánea de la VM y revertir si una prueba sale mal.

Algunos aspectos sutiles que debes tener en cuenta con la redirección USB:

- Solo un consumidor puede reclamar un dispositivo USB a la vez. Si rediriges un dispositivo a una VM, la máquina anfitriona pierde el acceso a él hasta que la VM lo libere. Esto importa si rediriges algo como un teclado o ratón USB; elige un dispositivo de repuesto para el desarrollo.

- Las transferencias USB isócronas tienen algunas peculiaridades en QEMU. Funcionan, pero la sincronización puede estar ligeramente desfasada. Para la mayor parte del desarrollo de drivers, trabajarás con transferencias bulk, de interrupción y de control, por lo que esto raramente supone un problema.

- Algunos controladores del host (en particular los xHCI) pueden reiniciarse bajo una carga pesada de I/O. Si tu driver se comporta de manera extraña durante las pruebas de estrés, prueba con un tipo `-device` diferente (uhci, ehci, xhci) para ver si el problema está en tu driver o en el controlador emulado.

- Las transferencias USB 3.0 SuperSpeed son más fiables con `-device nec-usb-xhci`. Los controladores basados en el indicador `-usb` más antiguos están limitados a USB 2.0.

Cuando la VM está en ejecución, el ciclo de iteración se convierte en: editar el código en el host, copiarlo a la VM (o montar un directorio compartido), hacer el build dentro de la VM, cargar, probar, recargar, repetir. Un Makefile con un objetivo `test:` que haga todo esto puede reducir el tiempo de iteración a decenas de segundos.

### Uso de `devd(8)` durante el desarrollo

`devd(8)` es el demonio de eventos de dispositivos de FreeBSD. Reacciona a las notificaciones del kernel sobre el attach y el detach de dispositivos, y puede ejecutar comandos configurados en respuesta. Durante el desarrollo de drivers, `devd` es útil de dos maneras.

En primer lugar, puede cargar automáticamente tu módulo cuando se conecta un dispositivo que coincida. Si tu módulo está en `/boot/modules/` y tu `USB_PNP_HOST_INFO` está configurado, `devd` ejecutará `kldload` automáticamente cuando vea un dispositivo que coincida.

En segundo lugar, puede ejecutar comandos de diagnóstico en el attach. Una entrada en `/etc/devd.conf` como esta:

```text
attach 100 {
    device-name "myfirst_usb[0-9]+";
    action "logger -t myfirst-usb 'device attached: $device-name'";
};
```

escribirá una línea de registro cada vez que un dispositivo `myfirst_usb` haga attach. Para diagnósticos más elaborados, puedes invocar tu propio script de shell que vuelque el estado, inicie consumidores en userland o envíe notificaciones.

Durante el desarrollo, un patrón útil es hacer que `devd` abra una sesión `cu` hacia un dispositivo `ucom` recién conectado, para poder ejercitar el driver en el momento del attach:

```text
attach 100 {
    device-name "cuaU[0-9]+";
    action "setsid screen -dmS usb-serial cu -l /dev/$device-name -s 9600";
};
```

Esto ejecuta la prueba en una sesión `screen` separada, a la que puedes conectarte más tarde con `screen -r usb-serial`.

### Cómo escribir un arnés de prueba sencillo en userland

La mayoría de los bugs del driver se exponen ejecutando realmente el driver contra el userland. Incluso un programa de prueba corto detecta más bugs que leer el código del driver con detenimiento. Para nuestro driver de eco, un programa de prueba mínimo tiene este aspecto:

```c
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
    int fd;
    const char *msg = "hello";
    char buf[64];
    int n;

    fd = open("/dev/myfirst_usb0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return (1);
    }

    if (write(fd, msg, strlen(msg)) != (ssize_t)strlen(msg)) {
        perror("write");
        close(fd);
        return (1);
    }

    n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        perror("read");
        close(fd);
        return (1);
    }
    buf[n] = '\0';
    printf("got %d bytes: %s\n", n, buf);

    close(fd);
    return (0);
}
```

Compila con `cc -o lab03-test lab03-test.c`. Ejecuta con `./lab03-test`. La salida esperada es "got 5 bytes: hello".

Extensiones de este arnés de prueba que detectan más bugs:

- Repite el ciclo open/write/read/close mil veces. Las fugas de memoria y de recursos aparecen después de unos cientos de iteraciones.
- Crea varios procesos con fork y haz que todos lean y escriban de forma concurrente. Las condiciones de carrera se manifiestan como corrupción aleatoria de datos o deadlocks.
- Mata intencionadamente el proceso de prueba en mitad de una transferencia. Las máquinas de estados del lado del driver a veces se confunden cuando un consumidor de userland desaparece de forma inesperada.
- Envía escrituras de longitud aleatoria (1 byte, 10 bytes, 100 bytes, 1 KB, 10 KB). Los casos límite en torno a las transferencias cortas y largas son donde viven muchos bugs sutiles.

Construye estas extensiones de forma incremental. Cada una probablemente revelará un bug que la versión anterior no revelaba; cada bug que corrijas hará que tu driver sea más robusto.

### Patrones de registro para el desarrollo

Durante el desarrollo, quieres un registro detallado. En producción, quieres silencio. El patrón del Capítulo 25 (`DLOG_RL` con un sysctl para controlar la verbosidad) se traslada sin cambios a los drivers USB y UART. Define una macro de registro con limitación de velocidad que se compile como una operación nula en los builds de producción, y distribúyela por cada rama que pueda resultar interesante durante la depuración:

```c
#ifdef MYFIRST_USB_DEBUG
#define DLOG(sc, fmt, ...) \
    do { \
        if (myfirst_usb_debug) \
            device_printf((sc)->sc_dev, fmt "\n", ##__VA_ARGS__); \
    } while (0)
#else
#define DLOG(sc, fmt, ...) ((void)0)
#endif
```

Luego en el callback:

```c
case USB_ST_TRANSFERRED:
    DLOG(sc, "bulk read completed, actlen=%d", actlen);
    ...
```

Controla `myfirst_usb_debug` mediante un sysctl:

```c
static int myfirst_usb_debug = 0;
SYSCTL_INT(_hw_myfirst_usb, OID_AUTO, debug, CTLFLAG_RWTUN,
    &myfirst_usb_debug, 0, "Enable debug logging");
```

Ahora puedes activar y desactivar el registro en tiempo de ejecución con `sysctl hw.myfirst_usb.debug=1`. Durante el desarrollo, actívalo. Durante las pruebas de estrés, desactívalo (la limitación de velocidad del registro ayuda, pero no registrar nada es todavía más barato). Durante el análisis post-mortem de un bug, actívalo y reprodúcelo.

### Un flujo de trabajo dirigido por pruebas para el Capítulo 26

Para los laboratorios prácticos que vienen en la siguiente sección, un buen flujo de trabajo tiene este aspecto:

1. Escribe el código del driver.
2. Compílalo. Corrige los errores de build.
3. Cárgalo en una VM de prueba. Observa `dmesg` para detectar fallos de attach.
4. Ejecuta un pequeño cliente en userland que ejercite las rutas de I/O del driver.
5. Descárgalo. Haz un cambio. Vuelve al paso 2.
6. Una vez que el driver se comporte bien en la VM, pruébalo en hardware real como comprobación de cordura.

La mayor parte del tiempo dedicado a este bucle se emplea en los pasos 1 a 4. Las pruebas con hardware real en el paso 6 son un paso de validación, no de iteración. Si intentas iterar en hardware real, perderás tiempo en ciclos de conexión y desconexión y en recuperarte de configuraciones accidentales incorrectas; la VM te ahorra todo esto.

Una instalación limpia de FreeBSD en una VM pequeña, configurada para arrancar rápidamente y tener el directorio de build de tu driver montado como un sistema de archivos compartido, es un entorno de desarrollo muy productivo. Dedicar medio día a configurarlo se recupera con creces en los días siguientes.

### Cerrando la sección 5

La sección 5 te ha proporcionado las herramientas para desarrollar drivers USB y de puerto serie sin estar ligado a hardware físico específico. `nmdm(4)` cubre el lado del puerto serie para cualquier prueba que no necesite un módem real. La redirección USB de QEMU cubre el lado USB para casi cualquier driver que puedas escribir. Las utilidades `cu`, `tip`, `stty`, `comcontrol` y `usbconfig` te proporcionan las herramientas de userland para ejercitar manualmente las rutas de código del driver. Y las técnicas generales, desde factorizar el código independiente del hardware en funciones comprobables en userland hasta usar `INVARIANTS` y `WITNESS` para la comprobación de corrección en tiempo de kernel, funcionan independientemente del transporte para el que escribas.

Habiendo llegado al final de la sección 5, tienes todo lo que necesitas para empezar a escribir drivers USB y de puerto serie reales para FreeBSD 14.3. Los modelos conceptuales, los esqueletos de código, la mecánica de transferencia, la integración con TTY y el entorno de pruebas están todos en su lugar. Lo que queda es práctica, que es el propósito de la siguiente sección.

## Patrones comunes en los drivers de transporte

Ahora que hemos recorrido USB y el puerto serie en detalle, vale la pena dar un paso atrás y observar los patrones que se repiten. Estos patrones aparecen en los drivers de red (Capítulo 28), los drivers de bloques (Capítulo 27) y en la mayoría de los demás drivers específicos de transporte en FreeBSD. Reconocerlos ahorra tiempo cuando lees un nuevo driver.

### Patrón 1: tabla de coincidencias, probe, attach, detach

Cada driver de transporte comienza con una tabla de coincidencias que describe los dispositivos que admite. Cada driver de transporte tiene un método probe que comprueba un candidato contra la tabla de coincidencias y devuelve cero o `ENXIO`. Cada driver de transporte tiene un método attach que toma posesión de un dispositivo que ha coincidido y asigna todo el estado por dispositivo. Cada driver de transporte tiene un método detach que libera todo lo que el método attach asignó, en orden inverso.

Los detalles varían. Las tablas de coincidencias USB usan `STRUCT_USB_HOST_ID`. Las tablas de coincidencias PCI usan entradas de `pcidev(9)`. Las tablas de coincidencias ISA usan descripciones de recursos. El contenido difiere, pero la estructura es idéntica.

Cuando lees un nuevo driver, lo primero que debes buscar es la tabla de coincidencias. Te indica qué hardware soporta el driver. Lo segundo que debes buscar es el método attach. Te indica qué recursos posee el driver. Lo tercero que debes buscar es el método detach. Te indica la forma de la jerarquía de recursos del driver.

### Patrón 2: El softc como única fuente de estado por dispositivo

Cada driver de transporte tiene un softc por dispositivo. Todo el estado mutable reside en el softc. No se utilizan variables globales para mantener el estado por dispositivo (la configuración global, como los flags de módulo, es aceptable). Este patrón mantiene la corrección de los drivers que gestionan múltiples dispositivos sin sorpresas inesperadas.

El tamaño del softc se declara en la estructura del driver. El framework asigna y libera el softc automáticamente. El driver accede a él mediante `device_get_softc(dev)` dentro de los métodos de Newbus, y mediante el helper del framework que corresponda (como `usbd_xfer_softc`) en los callbacks.

Añadir una nueva funcionalidad a un driver suele implicar agregar un nuevo campo al softc, un nuevo paso de inicialización en attach, un nuevo paso de limpieza en detach, y el código que usa ese campo en el camino entre ambos. Cuando estructuras los cambios de esta forma, raramente olvidas liberar los recursos, porque la forma del cambio hace que el paso de limpieza sea evidente.

### Patrón 3: La cadena de limpieza con goto etiquetado

Cuando un método attach tiene que asignar varios recursos, cada asignación cuenta con un camino de fallo que deshace todas las asignaciones anteriores. La cadena de goto con etiquetas del Capítulo 25 implementa esto de forma uniforme. Cada recurso tiene una etiqueta que corresponde al estado en el que ese recurso ha sido asignado correctamente. Un fallo en cualquier punto salta a la etiqueta del estado inmediatamente anterior, que realiza la limpieza en orden inverso.

Este patrón no resulta estéticamente atractivo para algunos programadores (el `goto` de C tiene mala reputación), pero es pragmáticamente la forma más limpia de gestionar un número arbitrario de pasos de limpieza en C. Las alternativas, como envolver cada recurso en una función independiente con su propia limpieza, suelen ser más verbosas. Otras alternativas, como establecer un flag por recurso y comprobarlo en una rutina de limpieza común, añaden una gestión de estado propensa a errores.

Independientemente de lo que pienses sobre `goto`, los drivers de FreeBSD usan el patrón de goto con etiquetas, y se espera que los nuevos drivers sigan esta convención.

### Patrón 4: Los frameworks ocultan los detalles del transporte

Cada transporte tiene un framework que oculta los detalles específicos del transporte detrás de una API uniforme. El framework USB oculta la gestión de buffers DMA detrás de `usb_page_cache` y `usbd_copy_in/out`. El framework UART oculta el despacho de interrupciones detrás de `uart_ops`. El framework de red (Capítulo 28) ocultará la gestión de buffers de paquetes detrás de mbufs e `ifnet(9)`.

El valor de estos frameworks está en que los drivers se vuelven más pequeños y más portables. Un driver UART de 200 líneas que soporte decenas de variantes de chip sería imposible sin el framework. Un driver USB de 500 líneas que soporte un protocolo complejo como el audio USB estaría igualmente fuera de alcance.

Cuando lees un driver nuevo, las partes que encuentras más densas suelen ser la lógica específica del chip. Las partes que parecen casi ausentes (la planificación de transferencias, la gestión de buffers, la integración con TTY) son donde el framework está haciendo su trabajo.

### Patrón 5: Callbacks con máquinas de estados

La máquina de tres estados del callback USB (`USB_ST_SETUP`, `USB_ST_TRANSFERRED`, `USB_ST_ERROR`) es el ejemplo canónico, pero patrones similares aparecen en otros drivers de transporte. El callback de finalización de transmisión de un driver de red tiene una estructura similar. El callback de finalización de solicitud de un driver de bloques es similar. El framework llama al driver en momentos bien definidos, y el driver usa una máquina de estados para decidir qué hacer.

Aprender a leer estas máquinas de estados es adquirir una habilidad universal para la lectura de drivers. Los estados concretos difieren de un framework a otro, pero el patrón es reconocible.

### Patrón 6: Mutexes y wakeups

Cada driver protege su softc con un mutex. El código orientado al userland (read, write, ioctl) toma el mutex al manipular los campos del softc. El código de callback se ejecuta con el mutex tomado (el framework lo adquiere antes de llamar). El código del userland libera el mutex antes de dormir y lo readquiere al despertar. Las llamadas de wakeup desde los callbacks liberan a cualquier hilo dormido que espere en el canal correspondiente.

Los detalles concretos varían según el transporte, pero el patrón es universal. Los drivers modernos de FreeBSD son uniformemente multithreaded y seguros para múltiples CPU, lo que requiere un locking disciplinado.

### Patrón 7: Helpers que devuelven errno

El Capítulo 25 introdujo el patrón de funciones helper que devuelven errno: cada función interna devuelve un errno entero (cero para éxito, distinto de cero para fallo). Los llamadores comprueban el valor de retorno y propagan el fallo hacia arriba por la pila. El método attach acumula los helpers exitosos en la cadena de goto con etiquetas; el fallo de cada helper desencadena la limpieza correspondiente a su posición.

Este patrón requiere disciplina. Cada helper debe ser consistente; ningún helper puede devolver un valor de éxito cuyo significado varíe, y ningún helper puede usar estado global para comunicar el fallo. Cuando se sigue rigurosamente, el patrón produce drivers en los que el flujo de control es legible y los caminos de error son fáciles de auditar.

### Patrón 8: Declaraciones de versión y dependencias de módulo

Cada módulo del driver declara su propia versión con `MODULE_VERSION`. Cada módulo del driver declara sus dependencias con `MODULE_DEPEND`. Las dependencias son rangos versionados (mínimo, preferido, máximo), lo que permite que el desarrollo paralelo del framework y el driver avance sin necesidad de lanzamientos sincronizados.

Cuando se publica una nueva versión mayor de un framework con cambios que rompen la API, el rango de versiones en `MODULE_DEPEND` es la forma en que un driver expresa "funciono con el framework v1 o v2, pero no con v3". El cargador de módulos del kernel se niega a cargar un driver cuyas dependencias no pueden satisfacerse, lo que previene muchas clases de fallos silenciosos.

### Patrón 9: Superposición de frameworks

Algunos drivers se asientan sobre múltiples frameworks. Un driver USB-a-Ethernet se apoya sobre `usbdi(9)` (para las transferencias USB) e `ifnet(9)` (para la semántica de la interfaz de red). Un driver USB-a-serie se apoya sobre `usbdi(9)` y `ucom(4)`. Un driver de almacenamiento masivo USB se apoya sobre `usbdi(9)` y CAM.

Cuando escribes un driver que combina varios frameworks, la estructura es la siguiente: escribes callbacks para cada framework y orquestas la interacción entre ellos en el código helper de tu driver. El framework sobre el que te apoyas en la capa superior define cómo ve el userland a tu driver. El framework de la capa inferior gestiona el transporte.

La lectura de `uftdi.c` te mostró este patrón: el driver es un driver USB (usa `usbdi(9)`) y un driver serie (usa `ucom(4)`), y la orquestación entre ambos es el núcleo del archivo.

### Patrón 10: Diferimiento de trabajo en attach

Algunos drivers no pueden terminar su trabajo de attach de forma síncrona. Por ejemplo, un driver podría necesitar leer una EEPROM de configuración que tarda varios cientos de milisegundos, o podría necesitar esperar a que un PHY autonegociase un enlace. Estos drivers usan el patrón de attach diferido: el método attach de Newbus encola una tarea de taskqueue que realiza el trabajo lento y retorna rápidamente.

Este patrón mantiene el arranque del sistema rápido (ningún driver retrasa el boot tardando demasiado en attach) y permite a los drivers realizar su trabajo de forma asíncrona. Quien invoca attach debe ser consciente de que el hecho de que attach "termine" no significa que el dispositivo esté completamente listo para usarse; un estado "listo" separado debe ser consultado periódicamente o señalizado.

Para los drivers USB y UART, attach suele ser lo suficientemente rápido como para que el diferimiento no sea necesario. Para drivers más complejos (las tarjetas de red en particular), el diferimiento es habitual. El Capítulo 28 mostrará un ejemplo.

### Patrón 11: Separación del camino de datos y el camino de control

En todo driver de transporte existen dos caminos conceptuales: el camino de control (configuración, cambios de estado, recuperación de errores) y el camino de datos (los bytes reales que circulan a través del dispositivo). La mayoría de los drivers estructuran estos caminos como rutas de código separadas, a veces con locking independiente.

El camino de control tiene poco ancho de banda y es poco frecuente. Puede permitirse un locking intenso y llamadas síncronas. El camino de datos tiene alto ancho de banda y es continuo. Debe estar optimizado para el rendimiento: locking mínimo, sin llamadas síncronas, gestión eficiente de buffers.

El framework USB los mantiene naturalmente separados: la configuración va a través de `usbd_transfer_setup` y transferencias de control; los datos van a través de transferencias bulk y de interrupción. El framework UART hace lo mismo: la configuración a través de `tsw_param`; los datos a través del manejador de interrupciones y los ring buffers. Los drivers de red tienen la separación más marcada: la configuración a través de ioctls; los datos a través de las colas TX y RX.

Al leer un driver nuevo, saber que esta separación existe te ayuda a comprender qué hace cada bloque de código. Una función con locking extenso y gestión de errores pertenece probablemente al camino de control. Una función con código corto y compacto y una gestión cuidadosa de buffers pertenece probablemente al camino de datos.

### Patrón 12: Drivers de referencia

Cada transporte en FreeBSD tiene uno o dos drivers de referencia "canónicos" que ilustran los patrones de forma correcta y exhaustiva. Para USB, `uled.c` y `uftdi.c` son las referencias. Para UART, `uart_dev_ns8250.c` es la referencia. Para redes, `em` (Intel Ethernet) y `rl` (Realtek) son las referencias. Para dispositivos de bloques, `da` (almacenamiento de acceso directo) es la referencia.

Cuando necesitas entender cómo escribir un nuevo driver en un transporte existente, el driver de referencia es el punto de partida adecuado. No intentes entender el framework solo a partir de su propio código; eso es demasiado abstracto. Empieza desde un driver en funcionamiento y deja que afiance tu comprensión.

## Laboratorios prácticos

Estos laboratorios te dan la oportunidad de convertir la lectura en memoria muscular. Cada laboratorio está diseñado para completarse en una sola sesión, idealmente en menos de una hora. Asumen un entorno de laboratorio con FreeBSD 14.3 (ya sea en hardware físico o dentro de una máquina virtual), acceso root y un entorno de compilación funcional tal como se describe en el Capítulo 3. Los archivos de acompañamiento de cada laboratorio de este capítulo están disponibles en `examples/part-06/ch26-usb-serial/` en el repositorio del libro.

Los laboratorios se complementan entre sí pero no dependen estrictamente unos de otros. Puedes saltarte un laboratorio y volver a él más adelante sin perder la continuidad. Los tres primeros laboratorios se centran en USB; los tres últimos, en serie. Cada laboratorio tiene la misma estructura: un breve resumen, los pasos, la salida esperada y una nota de "qué observar" que resalta el objetivo de aprendizaje.

### Laboratorio 1: Exploración de un dispositivo USB con `usbconfig`

Este laboratorio ejercita el vocabulario de descriptores de la Sección 1 inspeccionando dispositivos USB reales en tu máquina. No implica escribir ningún código.

**Objetivo.** Leer los descriptores de tres dispositivos USB diferentes e identificar su clase de interfaz, el número de endpoints y los tipos de endpoint.

**Requisitos.** Un sistema FreeBSD con al menos tres dispositivos USB conectados. Si solo dispones de una máquina con pocos puertos USB, un hub USB con algunos periféricos pequeños (ratón, teclado, unidad flash) es lo ideal.

**Pasos.**

1. Ejecuta `usbconfig list` como root. Anota los identificadores `ugenN.M` de tres dispositivos.

2. Para cada dispositivo, ejecuta:

   ```
   # usbconfig -d ugenN.M dump_all_config_desc
   ```

   Lee la salida. Identifica el `bInterfaceClass`, el `bInterfaceSubClass` y el `bInterfaceProtocol` de cada interfaz. Para cada endpoint de cada interfaz, anota el `bEndpointAddress` (incluido el bit de dirección), los `bmAttributes` (incluido el tipo de transferencia) y el `wMaxPacketSize`.

3. Construye una tabla pequeña. Para cada dispositivo, escribe: vendor ID, product ID, clase de interfaz (con el nombre de la lista de clases USB), número de endpoints y el tipo de transferencia de cada endpoint.

4. Compara tu tabla con la salida de `dmesg`. Confirma que el driver que tomó control de cada dispositivo tiene sentido dada la clase de interfaz que anotaste.

5. Opcional: repite el ejercicio con un dispositivo que no hayas visto antes (el teclado de otra persona, una interfaz de audio USB, un mando de juegos). Cuanta más variedad veas, más rápido te volverás en la lectura de descriptores.

**Salida esperada.** Una tabla rellenada con al menos tres filas. El ejercicio tiene éxito si puedes responder, para cualquier dispositivo de la tabla: "¿Qué clase de driver se encargaría de este dispositivo?"

**Qué debes observar.** Presta atención a los dispositivos que exponen múltiples interfaces. Una cámara web, por ejemplo, suele tener una interfaz de audio (para el micrófono) además de su interfaz de vídeo. Una impresora multifunción puede exponer una interfaz de impresora, una interfaz de escáner y una interfaz de almacenamiento masivo. Fijarte en estos detalles es lo que afina tu ojo para la lógica de múltiples interfaces del método `probe`.

### Laboratorio 2: Compilar y cargar el esqueleto del driver USB

Este laboratorio recorre la construcción del driver esqueleto de la Sección 2, su carga y la observación de su comportamiento cuando se conecta un dispositivo coincidente.

**Objetivo.** Compilar y cargar `myfirst_usb.ko`, y observar sus mensajes de attach y detach.

**Requisitos.** El entorno de compilación del Capítulo 3. Los archivos en `examples/part-06/ch26-usb-serial/lab02-usb-skeleton/`. Un dispositivo USB cuyo vendor/product puedas hacer coincidir. Para el desarrollo, puedes usar gratuitamente un VID/PID de prueba VOTI/OBDEV (0x16c0/0x05dc); en caso contrario, elige un dispositivo de prototipado económico (como una placa de desarrollo FT232) y ajusta la tabla de coincidencias para que corresponda con sus IDs.

**Pasos.**

1. Entra en el directorio del laboratorio:

   ```
   # cd examples/part-06/ch26-usb-serial/lab02-usb-skeleton
   ```

2. Lee `myfirst_usb.c` y `myfirst_usb.h`. Identifica la tabla de coincidencias, el método probe, el método attach, el softc y el método detach. Para cada uno, traza cómo se relaciona con el recorrido de la Sección 2.

3. Construye el módulo:

   ```
   # make
   ```

   Deberías ver que se crea `myfirst_usb.ko` en el directorio de compilación.

4. Carga el módulo:

   ```
   # kldload ./myfirst_usb.ko
   ```

   Ejecuta `kldstat | grep myfirst_usb` para confirmar que el módulo está cargado.

5. Conecta un dispositivo coincidente. Observa `dmesg`. Deberías ver una línea similar a:

   ```
   myfirst_usb0: <Vendor Product> on uhub0
   myfirst_usb0: attached
   ```

   Si el dispositivo no coincide, no ocurrirá nada. En ese caso, ejecuta `usbdevs` en la máquina de destino, busca el vendor/product de un dispositivo que sí tengas y edita la tabla de coincidencias en consecuencia. Vuelve a compilar, recarga y prueba de nuevo.

6. Desconecta el dispositivo. Observa `dmesg`. Deberías ver cómo el kernel elimina el dispositivo. Tu método `detach` no registra nada de forma explícita en este esqueleto mínimo, pero puedes añadir `device_printf(dev, "detached\n")` si deseas confirmación.

7. Descarga el módulo:

   ```
   # kldunload myfirst_usb
   ```

**Salida esperada.** Mensajes de attach en `dmesg` cuando se conecta el dispositivo. Descarga limpia sin panics cuando se retira el módulo.

**Qué vigilar.** Si `kldload` falla con un error sobre búsqueda de símbolos, probablemente olvidaste una línea `MODULE_DEPEND` o escribiste mal el nombre de un símbolo. Si nunca se llama a `attach` pero el dispositivo está sin duda presente, la tabla de coincidencias es incorrecta: comprueba los IDs de vendor y product en `usbconfig list` y verifica que coincidan con lo que escribiste en `myfirst_usb_devs`. Si se llama a `attach` pero falla, comprueba la salida de `device_printf` para averiguar el motivo del fallo.

### Laboratorio 3: Una prueba de loopback en bulk

Este laboratorio añade la mecánica de transferencia de la Sección 3 al esqueleto del Laboratorio 2 y envía algunos bytes a través de un dispositivo USB que implementa un protocolo de loopback. Es el primer laboratorio que realmente mueve datos.

**Objetivo.** Añadir un canal bulk-OUT y un canal bulk-IN al driver, escribir un pequeño cliente en espacio de usuario que envíe una cadena y la lea de vuelta, y observar el viaje de ida y vuelta.

**Requisitos.** Un dispositivo USB que implemente loopback en bulk. El dispositivo más sencillo para el desarrollo es un controlador USB gadget ejecutando un programa de loopback (posible en algunas placas ARM y en algunos kits de desarrollo). Si no dispones de uno, puedes sustituirlo por un ejercicio más sencillo: conecta el driver a una memoria USB, abre uno de sus endpoints `ugen` y simplemente arma, envía y completa una única transferencia de lectura. El bucle fallará (porque las memorias USB no hacen eco de los datos), pero la mecánica de configuración y envío funcionará correctamente.

**Pasos.**

1. Copia `lab02-usb-skeleton` en `lab03-bulk-loopback` como copia de trabajo.

2. Añade los canales bulk al driver. Pega el array de configuración de la Sección 3, las funciones de callback y la interacción con el espacio de usuario. Asegúrate de que la entrada `/dev` que crea tu driver admite `read(2)` y `write(2)`, que son las que utiliza el programa de prueba del laboratorio.

3. Vuelve a compilar y recarga el módulo.

4. Ejecuta el cliente en espacio de usuario:

   ```
   # ./lab03-test
   ```

   que encontrarás junto al driver en el directorio del laboratorio. El programa abre `/dev/myfirst_usb0`, escribe "hello", lee hasta 16 bytes y los imprime. Si el loopback funciona, la salida es "hello".

5. Observa `dmesg` en busca de advertencias de stall o mensajes de error.

**Salida esperada.** "hello" devuelto como eco. Si el dispositivo remoto no implementa loopback, la lectura retornará tras el tiempo de espera del canal sin datos, lo que también es un resultado de prueba válido a los efectos de ejercitar la máquina de estados.

**Qué vigilar.** El error más frecuente en este laboratorio es confundir las direcciones de los endpoints. Recuerda: `UE_DIR_IN` significa "el host lee desde el dispositivo" y `UE_DIR_OUT` significa "el host escribe hacia el dispositivo". Si los intercambias, las transferencias fallarán con stalls. Vigila también la falta de locking alrededor de los manejadores de lectura/escritura del espacio de usuario; si manipulas la cola de transmisión sin tener el mutex del softc, puedes entrar en una condición de carrera con el callback de escritura y ver cómo desaparecen bytes.

### Laboratorio 4: Un driver serie simulado con `nmdm(4)`

Este laboratorio no trata de escribir un driver, sino de aprender la mitad del espacio de usuario en las pruebas de comunicación serie. Los resultados te orientarán sobre cómo abordar el Laboratorio 5 y cómo depurar cualquier trabajo con la capa TTY en el futuro.

**Objetivo.** Crear un par de puertos virtuales `nmdm(4)`, observar cómo fluyen los datos y ejercitar `stty` y `comcontrol` para ver cómo funcionan termios y las señales de módem.

**Requisitos.** Un sistema FreeBSD. No se necesita hardware especial.

**Pasos.**

1. Carga el módulo `nmdm`:

   ```
   # kldload nmdm
   ```

2. En la terminal A, abre el lado `A`:

   ```
   # cu -l /dev/nmdm0A -s 9600
   ```

3. En la terminal B, abre el lado `B`:

   ```
   # cu -l /dev/nmdm0B -s 9600
   ```

4. Escribe en la terminal A. Observa que los caracteres aparecen en la terminal B. Escribe en la terminal B; aparecen en la terminal A.

5. Sal de `cu` en ambas terminales (escribe `~.`). En una tercera terminal, ejecuta:

   ```
   # stty -a -f /dev/nmdm0A
   ```

   Lee la salida. Fíjate en `9600` para la velocidad en baudios, `cs8 -parenb -cstopb` para el formato del byte y varias banderas para la disciplina de línea.

6. Cambia la velocidad en baudios en uno de los lados:

   ```
   # stty 115200 -f /dev/nmdm0A
   ```

   A continuación, vuelve a abrir los puertos con `cu -s 115200`. El cambio de velocidad en baudios es visible, aunque `nmdm(4)` no espera realmente a que los bits se serialicen.

7. Ejecuta:

   ```
   # comcontrol /dev/ttyu0A
   ```

   ...o más bien, el equivalente para los dispositivos de caracteres `nmdm`. Los pares `nmdm` no siempre tienen señales de módem visibles con `comcontrol`, según la versión de FreeBSD; si tu versión no las tiene, omite este paso.

**Salida esperada.** El texto aparece en el lado opuesto. `stty` muestra las banderas de termios. Ya tienes una forma reproducible de probar el comportamiento de la capa TTY en tu máquina.

**Qué vigilar.** Los identificadores de par (`0`, `1`, `2`...) son implícitos y se asignan en la primera apertura. Si no puedes abrir `/dev/nmdm5A` porque todavía no se ha abierto `/dev/nmdm4A`, es lo esperado: los pares se crean de forma diferida en orden creciente. Ten en cuenta también que `cu` usa un archivo de bloqueo en `/var/spool/lock/`; si matas `cu` de forma abrupta, el archivo de bloqueo puede persistir e impedir las reaperturas. Elimínalo manualmente si recibes un error de "puerto en uso".

### Laboratorio 5: Comunicarse con un adaptador USB-a-serie real

Este laboratorio incorpora hardware real al proceso. Utilizarás un adaptador USB-a-serie (un FT232, un CP2102, un CH340G o cualquier otro que FreeBSD admita) y un programa de terminal para ejercitar la ruta completa desde `ucom(4)` a través de la capa TTY hasta el espacio de usuario.

**Objetivo.** Conectar un adaptador USB-a-serie, verificar que el sistema lo reconoce y usar `cu` para enviarle datos (quizás haciendo un bucle de los pines TX y RX con un jumper).

**Requisitos.** Un adaptador USB-a-serie. Un cable jumper (si deseas hacer un loopback de hardware) o un segundo dispositivo serie con el que comunicarte (una placa de desarrollo, un ordenador embebido o un módem serie antiguo).

**Pasos.**

1. Conecta el adaptador. Ejecuta `dmesg | tail` y confirma que el sistema lo reconoce. Deberías ver líneas como:

   ```
   uftdi0 on uhub0
   uftdi0: <FT232R USB UART, class 0/0, ...> on usbus0
   ```

   y una línea `ucomN: <...>` justo después.

2. Ejecuta `ls -l /dev/cuaU*`. El puerto del adaptador suele ser `/dev/cuaU0` para el primer adaptador, `/dev/cuaU1` para el segundo, y así sucesivamente. (Fíjate en el sufijo U mayúscula, que distingue los puertos proporcionados por USB de los puertos UART reales en `/dev/cuau0`.)

3. Coloca un cable jumper entre los pines TX y RX del adaptador. Esto crea un loopback de hardware: todo lo que el adaptador transmite regresa por su propia línea RX.

4. En una terminal, establece la velocidad en baudios:

   ```
   # stty 9600 -f /dev/cuaU0
   ```

5. Abre el puerto con `cu`:

   ```
   # cu -l /dev/cuaU0 -s 9600
   ```

   Escribe caracteres. Cada carácter que escribas debería aparecer dos veces: una como eco local (si tu terminal hace eco) y otra como el carácter que regresa a través del loopback. Desactiva el eco local en `cu` si resulta confuso; `stty -echo` te ayudará.

6. Retira el jumper. Escribe caracteres. Ahora no volverán, porque no hay nada conectado a RX.

7. Sal de `cu` con `~.`. Desconecta el adaptador. Ejecuta `dmesg | tail` y verifica una desconexión limpia.

**Salida esperada.** Los caracteres se devuelven como eco cuando el jumper está colocado y se pierden cuando no lo está. El `dmesg` muestra un attach y un detach limpios.

**Qué vigilar.** Si el adaptador se inicializa pero no aparece ningún dispositivo `cuaU`, la instancia subyacente de `ucom(4)` puede haberse inicializado pero haber fallado al crear su TTY. Comprueba `dmesg` en busca de errores. Si los caracteres salen desordenados, probablemente la velocidad en baudios es incorrecta: asegúrate de que cada etapa de la ruta (tu terminal, `cu`, `stty`, el adaptador y el extremo remoto) esté configurada a la misma velocidad. En hardware antiguo, algunos adaptadores USB-a-serie no restablecen su configuración interna cuando los abres; es posible que necesites establecer la velocidad en baudios explícitamente con `stty` antes de que `cu` funcione correctamente.

### Laboratorio 6: Observar el ciclo de vida de hot-plug

Este laboratorio no requiere escribir ningún código de driver nuevo. Ejercita el ciclo de vida de hot-plug que describimos conceptualmente en la Sección 1 y en código en la Sección 2, utilizando el driver `uhid` o `ukbd` existente como sujeto de prueba.

**Objetivo.** Conectar y desconectar un dispositivo USB repetidamente mientras se monitorizan los registros del kernel, observando la secuencia completa de attach/detach.

**Requisitos.** Un dispositivo USB que puedas conectar y desconectar sin interrumpir tu sesión de trabajo. Una memoria USB o un ratón USB son opciones seguras; un teclado USB no lo es (porque desconectar un teclado en mitad de una sesión puede dejar tu shell inaccesible).

**Pasos.**

1. Abre una ventana de terminal y ejecuta:

   ```
   # tail -f /var/log/messages
   ```

   o, si tu sistema no registra los mensajes del kernel en ese archivo:

   ```
   # dmesg -w
   ```

   (El indicador `-w` es una incorporación de FreeBSD 14 que transmite en tiempo real los nuevos mensajes del kernel a medida que llegan.)

2. Conecta tu dispositivo USB. Observa los mensajes. Deberías ver:
   - Un mensaje del controlador USB sobre la aparición del nuevo dispositivo.
   - Un mensaje de `uhub` sobre la activación del puerto.
   - Un mensaje del driver de clase que coincidió con el dispositivo (p. ej., `ums0` para un ratón, `umass0` para una memoria USB).
   - Posiblemente un mensaje del subsistema de nivel superior (p. ej., `da0` para un dispositivo de almacenamiento masivo).

3. Desconecta el dispositivo. Observa los mensajes. Deberías ver:
   - Un mensaje de `uhub` sobre el apagado del puerto.
   - Un mensaje de detach del driver de clase.

4. Repite varias veces. Comprueba que cada attach tiene su correspondiente detach. Comprueba que no se pierde ningún mensaje. Observa el tiempo: la secuencia de attach puede llevar decenas o centenares de milisegundos porque la enumeración implica varias transferencias de control.

5. Escribe un pequeño bucle de shell que registre los tiempos de attach y detach:

   ```
   # dmesg -w | awk '/ums|umass/ { print systime(), $0 }'
   ```

   (Ajusta la expresión regular según el tipo de dispositivo que uses.) Esto te proporciona un registro legible por máquina con las marcas de tiempo de attach y detach.

**Salida esperada.** Un attach y un detach limpios en cada ocasión, sin estado residual.

**Qué vigilar.** En ocasiones verás que un dispositivo se inicializa y a continuación se desconecta en apenas unos cientos de milisegundos. Esto suele indicar que el dispositivo está fallando en la enumeración: ya sea por un cable defectuoso, alimentación insuficiente o firmware del dispositivo con errores. Si ocurre de forma sistemática con un dispositivo, prueba con un puerto USB diferente o un hub con alimentación propia. Vigila también los casos en que el kernel informa de un stall durante la enumeración; rara vez son dañinos, pero indican que la enumeración necesitó varios intentos.

### Laboratorio 7: Construir un esqueleto de ucom(4) desde cero

Este laboratorio es extenso y combina el material USB y serie del capítulo. Construirás un esqueleto mínimo de driver `ucom(4)` que se presenta como un puerto serie pero está respaldado por un dispositivo USB simple.

**Objetivo.** Construir un esqueleto de driver `ucom(4)` que se enganche a un dispositivo USB específico, se registre en el framework `ucom(4)` y proporcione implementaciones vacías de los callbacks principales. El driver no llegará a comunicarse con el hardware, pero ejercitará la ruta de registro completa de `ucom(4)`.

**Requisitos.** Los materiales del Laboratorio 2 (el esqueleto de driver USB). Un dispositivo USB con el que hacer la coincidencia (para las pruebas puedes usar el mismo VID/PID de VOTI/OBDEV que en el Laboratorio 2, o cualquier dispositivo USB de repuesto cuyos IDs puedas leer).

**Pasos.**

1. Parte del Laboratorio 2 como plantilla. Copia el directorio a `lab07-ucom-skeleton`.

2. Modifica el softc para incluir un `struct ucom_super_softc` y un `struct ucom_softc`:

   ```c
   struct lab07_softc {
       struct ucom_super_softc sc_super_ucom;
       struct ucom_softc        sc_ucom;
       struct usb_device       *sc_udev;
       struct mtx               sc_mtx;
       struct usb_xfer         *sc_xfer[LAB07_N_XFER];
       uint8_t                  sc_iface_index;
       uint8_t                  sc_flags;
   };
   ```

3. Añade un `struct ucom_callback` con implementaciones de relleno:

   ```c
   static void lab07_cfg_open(struct ucom_softc *);
   static void lab07_cfg_close(struct ucom_softc *);
   static int  lab07_pre_param(struct ucom_softc *, struct termios *);
   static void lab07_cfg_param(struct ucom_softc *, struct termios *);
   static void lab07_cfg_set_dtr(struct ucom_softc *, uint8_t);
   static void lab07_cfg_set_rts(struct ucom_softc *, uint8_t);
   static void lab07_cfg_set_break(struct ucom_softc *, uint8_t);
   static void lab07_start_read(struct ucom_softc *);
   static void lab07_stop_read(struct ucom_softc *);
   static void lab07_start_write(struct ucom_softc *);
   static void lab07_stop_write(struct ucom_softc *);
   static void lab07_free(struct ucom_softc *);

   static const struct ucom_callback lab07_callback = {
       .ucom_cfg_open       = &lab07_cfg_open,
       .ucom_cfg_close      = &lab07_cfg_close,
       .ucom_pre_param      = &lab07_pre_param,
       .ucom_cfg_param      = &lab07_cfg_param,
       .ucom_cfg_set_dtr    = &lab07_cfg_set_dtr,
       .ucom_cfg_set_rts    = &lab07_cfg_set_rts,
       .ucom_cfg_set_break  = &lab07_cfg_set_break,
       .ucom_start_read     = &lab07_start_read,
       .ucom_stop_read      = &lab07_stop_read,
       .ucom_start_write    = &lab07_start_write,
       .ucom_stop_write     = &lab07_stop_write,
       .ucom_free           = &lab07_free,
   };
   ```

   `ucom_pre_param` se ejecuta en el contexto del llamador antes de que se programe la tarea de configuración; úsalo para rechazar valores termios no soportados devolviendo un errno distinto de cero. `ucom_cfg_param` se ejecuta en el contexto de tarea del framework y es donde emitirías la transferencia de control USB real para reprogramar el chip.

4. Implementa cada callback como un no-op de momento. Añade `device_printf(sc->sc_super_ucom.sc_dev, "%s\n", __func__)` a cada uno para poder ver qué callbacks se están invocando.

5. En el método attach, después de `usbd_transfer_setup`, llama a:

   ```c
   error = ucom_attach(&sc->sc_super_ucom, &sc->sc_ucom, 1, sc,
       &lab07_callback, &sc->sc_mtx);
   if (error != 0) {
       goto fail_xfer;
   }
   ```

6. En el método detach, llama a `ucom_detach(&sc->sc_super_ucom, &sc->sc_ucom)` antes de `usbd_transfer_unsetup`.

7. Añade `MODULE_DEPEND(lab07, ucom, 1, 1, 1);` después del MODULE_DEPEND existente.

8. Compila, carga, conecta el dispositivo y observa. En `dmesg` deberías ver que el driver se engancha y que aparece un dispositivo `cuaU0` en `/dev/`.

9. Ejecuta `cu -l /dev/cuaU0 -s 9600`. El comando `cu` abrirá el dispositivo, lo que disparará varios de los callbacks de ucom. Observa `dmesg` para ver cuáles se activan. Cierra `cu` con `~.` y observa más callbacks.

10. Ejecuta `stty -a -f /dev/cuaU0`. Observa que el puerto tiene la configuración termios por defecto. Ejecuta `stty 115200 -f /dev/cuaU0` y comprueba que se llama a `lab07_cfg_param`.

11. Desconecta el dispositivo. Observa la secuencia de detach limpia.

**Resultado esperado.** El driver se engancha como dispositivo `ucom`, crea `/dev/cuaU0` y responde a los ioctls de configuración (aunque el dispositivo USB subyacente no haga nada en realidad). Cada invocación de callback es visible en `dmesg`.

**Qué vigilar.** Si el driver se engancha pero `/dev/cuaU0` no aparece, comprueba que `ucom_attach` haya tenido éxito. El valor de retorno es un errno; un valor distinto de cero indica fallo. Si falló con `ENOMEM`, la memoria disponible para la asignación del TTY se ha agotado. Si falló con `EINVAL`, probablemente uno de los campos de callback es nulo (consulta `/usr/src/sys/dev/usb/serial/usb_serial.c` para ver qué campos son estrictamente obligatorios).

Este laboratorio es un bloque de construcción. Un driver `ucom` real (como `uftdi`) rellenaría los callbacks con transferencias USB reales al chip. Partir de un esqueleto vacío y añadir un callback cada vez es una buena forma de construir un driver nuevo.

### Laboratorio 8: Diagnóstico de una sesión TTY bloqueada

Este laboratorio es un ejercicio de diagnóstico. Partiendo de una configuración serial defectuosa, usarás las herramientas de la sección 5 para encontrar el problema.

**Objetivo.** Descubre por qué una sesión `cu` no devuelve eco de los caracteres tras conectarse a un par `nmdm(4)` que tiene la tasa de baudios sin configurar en uno de los extremos.

**Pasos.**

1. Carga `nmdm`:

   ```
   # kldload nmdm
   ```

2. Establece tasas de baudios diferentes en los dos extremos. Es un escenario forzado, pero imita un error de configuración real:

   ```
   # stty 9600 -f /dev/nmdm0A
   # stty 115200 -f /dev/nmdm0B
   ```

3. Abre ambos extremos con `cu`, cada uno con la tasa incorrecta:

   ```
   (terminal 1) # cu -l /dev/nmdm0A -s 9600
   (terminal 2) # cu -l /dev/nmdm0B -s 115200
   ```

4. Escribe en el terminal 1. Es probable que veas los caracteres aparecer en el terminal 2, pero posiblemente distorsionados. O puede que no aparezcan en absoluto si el driver `nmdm(4)` aplica de forma estricta la coincidencia de tasas.

5. Sal de ambas sesiones `cu`.

6. Ejecuta `stty -a -f /dev/nmdm0A` y `stty -a -f /dev/nmdm0B`. Localiza la discrepancia.

7. Corrección: establece ambos extremos en la misma tasa. Vuelve a abrir `cu` y verifica que el problema esté resuelto.

**Qué observar.** Este laboratorio enseña el hábito diagnóstico de comprobar ambos extremos de un enlace. Una discrepancia en cualquiera de ellos genera problemas; encontrarla requiere examinarlos ambos. Las herramientas de diagnóstico (`stty`, `comcontrol`) funcionan desde la línea de comandos y producen salida legible. Usarlas es el primer paso sencillo antes de adentrarse en una depuración más profunda.

### Laboratorio 9: Supervisión de estadísticas de transferencia USB

Este laboratorio explora las estadísticas por canal que mantiene el framework USB, las cuales pueden ayudar a identificar problemas de rendimiento o errores ocultos.

**Objetivo.** Usa `usbconfig dump_stats` para observar los contadores de transferencia en un dispositivo USB activo e identificar si el dispositivo funciona según lo esperado.

**Pasos.**

1. Conecta un dispositivo USB con el que puedas trabajar de forma significativa. Una memoria USB es una buena elección porque puedes desencadenar transferencias bulk copiando archivos.

2. Identifica el dispositivo:

   ```
   # usbconfig list
   ```

   Anota el identificador `ugenN.M`.

3. Vuelca las estadísticas de referencia:

   ```
   # usbconfig -d ugenN.M dump_stats
   ```

   Registra la salida.

4. Realiza una cantidad significativa de I/O en el dispositivo. Para una memoria USB, copia un archivo grande:

   ```
   # cp /usr/src/sys/dev/usb/usb_transfer.c /mnt/usb_mount/
   ```

5. Vuelca las estadísticas de nuevo. Compáralas.

6. Anota qué contadores cambiaron. `xfer_completed` debería haber aumentado considerablemente. `xfer_err` debería seguir siendo pequeño.

7. Intenta provocar errores deliberadamente. Desconecta el dispositivo durante una transferencia. Vuelve a conectarlo. Vuelca las estadísticas sobre el nuevo `ugenN.M` (se asigna uno nuevo al volver a conectar).

**Qué observar.** Las estadísticas revelan comportamientos invisibles. Un dispositivo que en general funciona bien pero que de vez en cuando se queda parado mostrará `stall_count` distinto de cero. Un dispositivo que pierde transferencias mostrará `xfer_err` en aumento. En condiciones normales, un dispositivo sano muestra un crecimiento constante de `xfer_completed` y ningún error.

Si estás desarrollando un driver y las estadísticas muestran errores inesperados, eso es una señal de que algo va mal. Las estadísticas las mantiene el framework USB, no el driver, por lo que reflejan la realidad independientemente de si el driver lo advierte.

## Ejercicios de desafío

Los ejercicios de desafío amplían tu comprensión. No son estrictamente necesarios para avanzar al capítulo 27, pero cada uno de ellos profundizará tu dominio del trabajo con drivers USB y seriales. Tómate tu tiempo. Lee el código fuente de FreeBSD pertinente. Escribe programas pequeños. Espera que algunos desafíos lleven varias horas.

### Desafío 1: Añadir un tercer tipo de endpoint USB

El esqueleto de la sección 2 admite transferencias bulk. Extiéndelo para que también gestione un endpoint de interrupción. Añade un nuevo canal al array `struct usb_config` con `.type = UE_INTERRUPT`, `.direction = UE_DIR_IN`, y un buffer pequeño (por ejemplo, dieciséis bytes). Implementa el callback como un sondeo continuo que lea un pequeño paquete de estado del dispositivo en cada finalización de interrupt-IN.

Prueba el cambio comparando el comportamiento de los tres canales. Los canales bulk deberían estar inactivos la mayor parte del tiempo y solo enviar transferencias cuando el driver tenga trabajo que hacer. El canal de interrupción debería ejecutarse de forma continua, consumiendo silenciosamente los paquetes interrupt-IN cada vez que el dispositivo los envíe.

Un objetivo adicional: haz que el callback de interrupción entregue los bytes recibidos al mismo nodo `/dev` que el canal bulk. Cuando el espacio de usuario lea el nodo, obtendrá una vista combinada de los datos bulk-in e interrupt-in. Este es un patrón útil para dispositivos que tienen tanto datos en streaming como eventos de estado asíncronos.

### Desafío 2: Escribir un driver USB gadget mínimo

El ejemplo que hemos seguido es un driver del lado del host: la máquina FreeBSD actúa como host USB y el dispositivo es el periférico. Invierte el ejemplo escribiendo un driver USB gadget que haga que la máquina FreeBSD se presente como un dispositivo simple ante otro host.

Esto requiere hardware USB-on-the-Go, por lo que el desafío solo es viable en placas específicas (algunas placas de desarrollo ARM lo admiten). El código fuente relevante se encuentra en `/usr/src/sys/dev/usb/template/`. Parte de `usb_template_cdce.c`, que implementa la clase CDC Ethernet, y modifícalo para implementar una clase vendor-specific más sencilla con un único endpoint bulk-OUT que simplemente absorba todo lo que el host envíe.

Este desafío te enseña cómo se ve el framework USB desde el otro lado. Muchos de los conceptos son imagen especular: lo que era una transferencia desde la perspectiva del host es también una transferencia desde la perspectiva del dispositivo, pero la dirección de la flecha bulk está invertida.

### Desafío 3: Un manejador personalizado de flags de `termios`

La estructura `termios` tiene muchos flags, y el framework `uart(4)` gestiona la mayoría de ellos automáticamente. Escribe una pequeña modificación en un driver UART (o en una copia de `uart_dev_ns8250.c` en un build privado) que haga que el driver registre un mensaje con `device_printf` cada vez que un flag de termios específico cambie de valor.

Elige, por ejemplo, `CRTSCTS` (control de flujo por hardware) como el flag a seguir. Añade un mensaje de log en la ruta `param` del driver que imprima "CRTSCTS=on" o "CRTSCTS=off" siempre que el nuevo valor del flag difiera del anterior.

Prueba la modificación ejecutando:

```console
# stty crtscts -f /dev/cuau0
# stty -crtscts -f /dev/cuau0
```

Verifica que los mensajes de log aparezcan en `dmesg` y que correspondan correctamente a los cambios de `stty`.

Este desafío consiste en comprender exactamente en qué punto de la cadena de llamadas el cambio de termios llega al driver. La respuesta (en `param`) está documentada en el código fuente, pero verlo con tus propios ojos es diferente a leer sobre ello.

### Desafío 4: Analizar un protocolo USB sencillo

Elige un protocolo USB que te resulte interesante. HID es una buena opción porque está ampliamente documentado. CDC ACM es otra buena elección porque es sencillo. Elige uno, lee la especificación en usb.org (las partes públicas) y escribe un pequeño analizador de protocolo en C que tome un buffer de bytes e imprima qué significan.

Para HID, el analizador consumiría reports: input reports, output reports, feature reports. Consultaría el descriptor de report del dispositivo para conocer la estructura. Imprimiría, para cada report, el uso (movimiento de ratón, pulsación de botón, código de tecla) y el valor.

Para CDC ACM, el analizador consumiría el conjunto de comandos AT: un pequeño conjunto de comandos que los programas de terminal utilizan para configurar módems. Reconocería los comandos e informaría de cuáles gestionaría el driver y cuáles se pasarían directamente al dispositivo.

Este no es un desafío de escritura de drivers como tal; es un desafío de comprensión de protocolos. Los drivers de dispositivo implementan protocolos, y sentirse cómodo con las especificaciones de protocolo es una habilidad fundamental.

### Desafío 5: Robustez bajo carga

Toma el driver de eco en bucle del laboratorio 3 (o un driver similar que hayas escrito) y somételo a una prueba de carga. Escribe un programa en espacio de usuario que ejecute dos threads: uno que escriba constantemente bytes aleatorios en el dispositivo, y otro que lea y verifique constantemente.

Ejecuta el programa durante una hora. Luego, durante un día. Después, desconecta y vuelve a conectar el dispositivo mientras se ejecuta y comprueba si el programa se recupera correctamente.

Probablemente encontrarás errores. Los más comunes son: problemas de locking en el callback de escritura bajo acceso concurrente, condiciones de carrera entre close() y transferencias en vuelo, fugas de memoria de buffers que se asignan pero nunca se liberan en rutas de error específicas, y errores en la máquina de estados cuando aparece un stall en un momento inesperado.

Cada error que encuentres te enseñará algo sobre dónde el contrato del driver con sus llamantes es sutil. Corrige los errores. Anota lo que aprendes. Este es exactamente el tipo de trabajo que diferencia un buen driver de uno que simplemente funciona.

### Desafío 6: Implementar suspend/resume correctamente

La mayoría de los drivers USB no implementan manejadores de suspend y resume. El framework tiene valores predeterminados que funcionan para el caso común, pero un driver que mantiene estado a largo plazo (una cola de comandos pendientes, un contexto de streaming, una sesión negociada) puede necesitar guardar y restaurar ese estado en torno a los ciclos de suspend.

Extiende el driver de eco en bucle con los métodos `device_suspend` y `device_resume`. En suspend, vacía las transferencias pendientes y guarda una pequeña cantidad de estado. En resume, restaura el estado y reenvía cualquier trabajo pendiente.

Prueba ejecutando el sistema a través de un ciclo de suspend (en un portátil que lo admita) mientras el driver está en funcionamiento. Verifica que, tras el resume, el driver continúa funcionando correctamente y no se ha perdido ningún estado.

Este desafío enseña las sutilezas de suspend/resume, entre ellas que el hardware puede estar en un estado diferente tras el resume al que tenía antes del suspend, y que todo el estado en vuelo debe reconstruirse o descartarse.

### Desafío 7: Añadir soporte para `poll(2)`

La mayoría de los drivers mostrados en este capítulo admiten `read(2)` y `write(2)` pero no `poll(2)` ni `select(2)`. Estas llamadas al sistema permiten a los programas en espacio de usuario esperar la disponibilidad de I/O en múltiples descriptores a la vez, lo cual es esencial para servidores y programas interactivos.

Añade un método `d_poll` al `cdevsw` del driver de eco. El método debe devolver una máscara de bits que indique qué eventos de I/O son posibles en ese momento: POLLIN si hay datos para leer, POLLOUT si hay espacio para escribir.

La parte más difícil de añadir soporte para poll es la lógica de wakeup. Cuando un callback de transferencia añade datos a la cola RX, debe llamar a `selwakeup` sobre la estructura selinfo que utiliza el mecanismo de poll. Del mismo modo, cuando el callback de escritura consume bytes de la cola TX y libera espacio, debe llamar a `selwakeup` sobre el selinfo de escritura.

Este desafío requerirá leer `/usr/src/sys/kern/sys_generic.c` y `/usr/src/sys/sys/selinfo.h` para comprender el mecanismo selinfo.

### Desafío 8: Escribir un ioctl de contador de caracteres

Añade un ioctl al driver de eco que devuelva los contadores actuales de bytes TX y RX. La interfaz ioctl requiere que:

1. Definas un número mágico y una estructura para el ioctl en un archivo de cabecera:

   ```c
   struct myfirst_usb_stats {
       uint64_t tx_bytes;
       uint64_t rx_bytes;
   };
   #define MYFIRST_USB_GET_STATS _IOR('U', 1, struct myfirst_usb_stats)
   ```

2. Implementes un método `d_ioctl` que responda a `MYFIRST_USB_GET_STATS` copiando los contadores al espacio de usuario.

3. Mantengas los contadores en el softc, incrementándolos en los callbacks de transferencia.

4. Escribas un programa en espacio de usuario que emita el ioctl e imprima los resultados.

Este desafío enseña la interfaz ioctl, que es la forma estándar en que los drivers exponen operaciones no streaming al espacio de usuario. También te introduce a los macros `_IOR`, `_IOW` y `_IOWR` de `<sys/ioccom.h>`.

## Guía de resolución de problemas

A pesar de tu mejor esfuerzo, surgirán problemas. Esta sección documenta las clases de problemas más comunes que encontrarás al trabajar con drivers USB y seriales, con pasos concretos para diagnosticar cada uno.

### El módulo no carga

Síntoma: `kldload myfirst_usb.ko` devuelve un error, normalmente con un mensaje sobre símbolos no resueltos.

Causas y soluciones:
- Entrada `MODULE_DEPEND` ausente. Añade `MODULE_DEPEND(myfirst_usb, usb, 1, 1, 1);` al driver.
- Ausencia de `MODULE_DEPEND` sobre un segundo módulo, como `ucom`. Si tu driver usa `ucom_attach`, añade una dependencia sobre `ucom`.
- Compilado contra un kernel que no coincide con el kernel en ejecución. Reconstruye el módulo contra las fuentes del kernel que está ejecutándose actualmente.
- Tabla de símbolos del kernel desactualizada. Tras actualizar el kernel, ejecuta `kldxref /boot/kernel` para refrescarla.

Si el mensaje de error menciona un símbolo específico que tú no has escrito (como `ttycreate` o `cdevsw_open`), busca el símbolo que falta en el árbol de código fuente para averiguar en qué subsistema reside y añade un `MODULE_DEPEND` sobre ese módulo.

### El driver carga pero nunca hace attach

Síntoma: `kldstat` muestra que el driver ha cargado, pero `dmesg` no muestra ningún mensaje de attach al conectar el dispositivo.

Causas y soluciones:
- La tabla de coincidencias no coincide con el dispositivo. Compara los IDs de proveedor y producto que muestra `usbconfig list` con las entradas de tu `STRUCT_USB_HOST_ID`.
- Desajuste en el número de interfaz. Si el dispositivo tiene múltiples interfaces y tu probe descarta las que tienen `bIfaceIndex != 0`, prueba con una interfaz diferente.
- probe devuelve `ENXIO` por alguna otra razón. Añade `device_printf(dev, "probe with class=%x subclass=%x\n", uaa->info.bInterfaceClass, uaa->info.bInterfaceSubClass);` al principio de `probe` temporalmente para ver qué le ofrece el framework.
- Otro driver está reclamando el dispositivo primero. Comprueba `dmesg` para detectar attachments de otros drivers; puede que necesites descargar explícitamente el driver en competencia con `kldunload` antes de que el tuyo pueda vincularse. Alternativamente, asigna a tu driver una prioridad más alta mediante los valores de retorno del probe de bus (aplicable a buses similares a PCI, no a USB).

### El driver hace attach pero el nodo `/dev` no aparece

Síntoma: el mensaje de attach aparece en `dmesg`, pero `ls /dev/` no muestra ninguna entrada correspondiente.

Causas y soluciones:
- La llamada a `make_dev` falló. Comprueba el valor de retorno; si es null, gestiona el error y regístralo.
- cdevsw incorrecto. Asegúrate de que `myfirst_usb_cdevsw` está declarado correctamente con `d_version = D_VERSION` y con `d_name`, `d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl` válidos donde corresponda.
- `si_drv1` no establecido. Aunque no es estrictamente necesario para que aparezca el nodo, muchos errores se manifiestan como «el nodo aparece pero los ioctl ven un softc NULL» porque `si_drv1` no fue inicializado.
- Problema de permisos. Los permisos predeterminados 0644 pueden restringir el acceso; prueba con 0666 temporalmente durante el desarrollo.

### El driver entra en pánico durante el detach

Síntoma: desconectar el dispositivo (o descargar el módulo) provoca un kernel panic.

Causas y soluciones:
- El callback de transferencia se ejecuta durante el detach. Debes llamar a `usbd_transfer_unsetup` antes de destruir el mutex. La lógica de cancelación y espera del framework es lo que hace que el detach sea seguro.
- El nodo `/dev` está abierto cuando el driver se descarga. Si el espacio de usuario tiene el nodo abierto, el módulo no puede descargarse. Ejecuta `fstat | grep myfirst_usb` para ver qué proceso lo retiene, y termina el proceso o cierra el archivo.
- Memoria liberada antes de que todos los usos hayan terminado. Si usas trabajo diferido (taskqueue, callout), debes cancelarlo y esperar a que finalice antes de liberar el softc. Las funciones `taskqueue_drain` y `callout_drain` existen precisamente para esto.
- Softc use-after-free. Si tienes código fuera del driver que mantiene un puntero al softc, el softc puede liberarse mientras ese puntero sigue siendo un puntero colgante. Rediseña para evitar punteros externos al softc, o añade conteo de referencias.

### Las transferencias se bloquean

Síntoma: las transferencias bulk parecen tener éxito en la llamada de envío pero nunca se completan, o se completan con `USB_ERR_STALLED`.

Causas y soluciones:
- Dirección de endpoint incorrecta. Verifica la dirección en tu `struct usb_config` comparándola con el bit alto de `bEndpointAddress` del endpoint.
- Tipo de endpoint incorrecto. Verifica que el campo `type` coincide con los bits bajos de `bmAttributes` del endpoint.
- Transferencia demasiado grande. Si estableces una longitud de trama superior al `wMaxPacketSize` del endpoint, el framework normalmente la dividirá en paquetes, pero algunos dispositivos rechazan una transferencia que supera su buffer interno.
- Stall del firmware del dispositivo. El dispositivo remoto está señalando «no está listo». El mecanismo automático de clear-stall del framework debería recuperarse, pero un stall persistente suele indicar un error de protocolo (comando incorrecto, secuencia incorrecta, autenticación ausente).

### Caracteres serie corruptos

Síntoma: aparecen bytes en la línea pero son incorrectos o contienen caracteres extra.

Causas y soluciones:
- Desajuste de velocidad en baudios. Todas las etapas deben coincidir. Usa `stty` para comprobar todas las etapas.
- Desajuste de formato de byte. Ajusta los bits de datos, la paridad y los bits de parada para que coincidan. `stty cs8 -parenb -cstopb` es la configuración más habitual.
- Gestión incorrecta de los flags de `termios` en el driver. Si modificas `uart_dev_ns8250.c` y rompes `param`, el chip se programará incorrectamente. Compara con el archivo upstream.
- Desajuste de control de flujo. Si un lado tiene `CRTSCTS` activado y el otro no, se perderán bytes bajo carga. Configura ambos lados de forma consistente.
- Problema de cable. Un cable defectuoso o con un pinout inusual (algunos cables RJ45 a DB9 tienen pinouts no estándar) puede introducir errores de bit. Cambia los cables para descartarlo.

### Un proceso bloqueado en `read(2)` que no responde

Síntoma: un programa bloqueado en la ruta `read()` del driver no responde a Ctrl+C ni a `kill`.

Causas y soluciones:
- El `d_read` del driver duerme sin comprobar señales. Usa `msleep(..., PCATCH, ...)` (con el flag `PCATCH`) para que el sleep devuelva `EINTR` cuando llegue una señal, y propaga el errno de vuelta al espacio de usuario.
- El `d_read` del driver mantiene un lock no interrumpible. Verifica que el sleep se realiza sobre una variable de condición interrumpible y que el mutex se libera durante el sleep.
- El callback de transferencia nunca arma el canal. Si tu `d_read` espera a un flag que solo el callback de lectura establece, y ese callback nunca se dispara, la espera no terminará nunca. Asegúrate de que el canal se inicia en `d_open` o en el momento del attach.

### Uso elevado de CPU en reposo

Síntoma: el driver consume una cantidad significativa de CPU incluso cuando no fluyen datos.

Causas y soluciones:
- Implementación basada en polling. Si tu driver hace polling de un flag en un bucle activo, reescríbelo para que duerma sobre un evento.
- Callback disparándose en exceso. El framework no debería disparar un callback sin un cambio de estado, pero algunos canales mal configurados pueden entrar en un bucle de «reintentar en caso de error» que dispara el callback tan rápido como el hardware puede responder. Añade un contador de reintentos o un limitador de tasa.
- Callback de lectura sin trabajo pero que siempre se reactiva. Si el dispositivo envía transferencias de cero bytes para indicar «no tengo nada que decir», asegúrate de que tu callback las gestiona correctamente sin tratarlas como datos normales.

### `usbconfig` muestra el dispositivo pero `dmesg` no muestra nada

Síntoma: `usbconfig list` muestra el dispositivo, pero no aparece ningún mensaje de attach del driver.

Causas y soluciones:
- El dispositivo está vinculado a `ugen` (el driver genérico) porque ningún driver específico coincidió. Este es el comportamiento normal cuando no hay ningún driver que coincida. Comprueba las tablas de coincidencias de los drivers disponibles. `pciconf -lv` no sirve de ayuda aquí porque se trata de USB, no de PCI; el equivalente en USB es `usbconfig -d ugenN.M dump_device_desc`.
- `devd` está desactivado y la carga automática no se produce. Activa `devd` ejecutando `service devd onestart` y vuelve a conectar el dispositivo.
- El archivo del módulo no está en una ruta cargable. `kldload` puede aceptar una ruta completa (`kldload /path/to/module.ko`), pero para la carga automática por parte de `devd`, el módulo debe estar en un directorio que `devd` esté configurado para buscar. `/boot/modules/` es la ubicación convencional para módulos fuera del árbol de código fuente en un sistema de producción.

### Depurar un deadlock con `WITNESS`

Síntoma: el kernel se congela con la CPU bloqueada en una función específica y `WITNESS` está habilitado.

Causas y soluciones:
- Violación del orden de locks. `WITNESS` registrará la violación en la consola serie. Lee el registro: te indicará qué locks se adquirieron en qué orden y dónde se observó el orden inverso. Corrígelo estableciendo un orden de adquisición de locks consistente en todo tu driver.
- Lock mantenido durante un sleep. Si mantienes un mutex y luego llamas a una función que duerme, puedes entrar en deadlock con cualquier otro thread que quiera el mutex. Identifica la función que duerme (a menudo oculta en una asignación de memoria o en una espera de transferencia USB) y reestructura para liberar el mutex antes del sleep.
- Lock adquirido en contexto de interrupción habiendo sido adquirido por primera vez fuera de ese contexto sin `MTX_SPIN`. Los mutexes de FreeBSD tienen dos formas: el tipo predeterminado (`MTX_DEF`) puede dormir; el tipo spin (`MTX_SPIN`) no puede. Adquirir un sleep mutex desde un manejador de interrupción es un error.

Activar `WITNESS` durante el desarrollo (compilando el kernel con `options WITNESS` o usando la variante de `GENERIC-NODEBUG` con `INVARIANTS` habilitados) detecta muchos de estos problemas antes de que aparezcan en la máquina de un usuario.

### Un driver que aparece dos veces para el mismo dispositivo

Síntoma: `dmesg` muestra que tu driver hace attach dos veces para un único dispositivo, creando `myfirst_usb0` y `myfirst_usb1` con los mismos IDs USB.

Causas y soluciones:
- El dispositivo tiene dos interfaces y el driver coincide con ambas. Comprueba `bIfaceIndex` en el método probe y limita la coincidencia a la interfaz que realmente soportas.
- El dispositivo tiene múltiples configuraciones y todas están activas. Esto es poco frecuente; si es así, selecciona la configuración correcta explícitamente en el método attach.
- Otro driver está vinculado a una de las interfaces. Esto no es un error; simplemente significa que el dispositivo es multi-interfaz y distintos drivers reclaman interfaces distintas. Si ves `myfirst_usb0` y `ukbd0` para el mismo dispositivo, el dispositivo tiene tanto una interfaz específica del proveedor como una interfaz HID, y los dos drivers hacen attach de forma independiente.

### La velocidad en baudios USB serie no surte efecto

Síntoma: ejecutas `stty 115200 -f /dev/cuaU0`, pero el intercambio de datos ocurre a una velocidad diferente.

Causas y soluciones:
- La transferencia de control para programar la velocidad en baudios falló. Comprueba `dmesg` para detectar mensajes de error de `ucom_cfg_param`. Instrumenta el driver para registrar el resultado de la transferencia de control.
- La codificación del divisor del chip es incorrecta. Las diferentes variantes de FTDI usan fórmulas de divisor ligeramente distintas; comprueba la detección de variante en el driver.
- El extremo remoto funciona a una velocidad diferente. Como se indicó anteriormente en este capítulo, ambos extremos deben coincidir.
- El cable o adaptador está introduciendo su propia limitación de velocidad. Algunos adaptadores USB a serie renegocian silenciosamente; esto es poco frecuente pero puede ocurrir con cables de baja calidad.

### Un kernel panic con «Spin lock held too long»

Síntoma: el kernel entra en pánico con este mensaje, normalmente durante I/O intenso en el driver.

Causas y soluciones:
- Un método `uart_ops` del driver UART está durmiendo o bloqueándose. Los seis métodos de `uart_ops` se ejecutan con spinlocks adquiridos (en algunas rutas de ejecución) y no deben dormir, llamar a funciones que no sean seguras en contexto de spin, ni ejecutar bucles largos. Revisa el método problemático en busca de llamadas costosas.
- El manejador de interrupción no vacía la fuente de interrupción con suficiente rapidez. Si el manejador tarda más de lo que permite la tasa de interrupciones, estas se acumulan. Acelera el manejador.
- La contención de locks está causando inversión de prioridad. Reduce el ámbito de la sección crítica o divídela.

### Un dispositivo que nunca completa la enumeración

Síntoma: conectar un dispositivo produce una o dos líneas en `dmesg` sobre el inicio de la enumeración, pero nunca aparece un mensaje de finalización.

Causas y soluciones:
- El dispositivo está violando la especificación USB. Algunos dispositivos baratos o falsificados tienen firmware con errores. Si es posible, prueba con un dispositivo diferente.
- Alimentación insuficiente. Los dispositivos que demandan más energía de la que el puerto puede suministrar no lograrán enumerarse. Prueba con un hub con alimentación propia.
- Interferencia electromagnética. Un cable defectuoso o un puerto en mal estado pueden causar errores de bit durante la enumeración. Prueba con cables o puertos diferentes.
- El controlador host USB está en un estado confuso. Intenta descargar y recargar el driver del controlador host, o (como último recurso) reinicia el sistema.

### Lista de comprobación diagnóstica cuando estás bloqueado

Cuando un driver en desarrollo no se comporta correctamente y no sabes por qué, recorre esta lista de comprobación en orden. Cada paso elimina una gran clase de posibles problemas.

1. Compila sin errores con `-Wall -Werror`. Muchos bugs sutiles producen advertencias del compilador.
2. Carga el módulo en un kernel construido con `INVARIANTS` y `WITNESS`. Cualquier violación de lock o de invariante quedará atrapada de inmediato.
3. Activa el registro de depuración de tu driver. Ejecuta un escenario de reproducción mínimo y captura los logs.
4. Compara el comportamiento de tu driver con el de un driver que funcione correctamente para hardware similar. Contrastar comportamientos revela bugs que el simple repaso de tu propio código no descubre.
5. Simplifica el escenario. Escribe un programa de prueba mínimo en espacio de usuario. Usa un dispositivo USB mínimo (o un par `nmdm` para comunicación serie). Elimina todas las variables que puedas.
6. Usa `dtrace` en las funciones del framework USB. Las sondas `usbd_transfer_submit:entry` y `usbd_transfer_submit:return` te permiten trazar exactamente qué transferencias se enviaron y qué ocurrió con cada una de ellas.
7. Ejecuta el driver con `WITNESS_CHECKORDER` activado. Cada vez que se toma un mutex, el orden se verifica contra el historial acumulado.
8. Si el problema es intermitente, ejecuta el driver bajo un entorno de prueba de estrés que genere carga durante horas. Los bugs intermitentes acaban siendo reproducibles bajo carga sostenida.

Este checklist no es exhaustivo, pero cubre las técnicas que permiten detectar la gran mayoría de los bugs en drivers.

## Cómo leer el árbol de código fuente USB de FreeBSD: un recorrido guiado

El esqueleto `myfirst_usb` y el recorrido por FTDI te han dado la forma de un driver USB. Pero el aprendizaje real ocurre cuando lees los drivers existentes en el árbol. Cada uno es una pequeña lección sobre cómo aplicar el framework a una clase específica de dispositivo. Esta sección te ofrece un recorrido guiado por cinco drivers, ordenados de más sencillo a más representativo, y señala lo que cada uno enseña.

El patrón que recomendamos es el siguiente. Abre el archivo fuente de cada driver junto a esta sección. Lee primero el bloque de comentarios inicial y las definiciones de estructuras; esos te dicen para qué sirve el driver y qué estado mantiene. Luego traza el ciclo de vida: tabla de coincidencia, probe, attach, detach, registro. Solo cuando el ciclo de vida esté claro deberías pasar al camino de datos. Este orden refleja cómo el propio framework trata al driver: primero como candidato de coincidencia, después como driver conectado, y solo entonces como algo que mueve datos.

### Recorrido 1: uled.c, el driver USB más sencillo

Archivo: `/usr/src/sys/dev/usb/misc/uled.c`.

Empieza aquí. `uled.c` es el driver para el LED USB Dream Cheeky. Tiene menos de 400 líneas. Implementa una única salida (establecer el color del LED) a través de una única transferencia de control. No hay entrada, ni transferencia bulk, ni transferencia de interrupción, ni I/O concurrente. Todo en él es mínimo, y por eso todo en él es fácil de leer.

Aspectos clave que estudiar en `uled.c`:

La tabla de coincidencia tiene una única entrada: `{USB_VPI(USB_VENDOR_DREAM_CHEEKY, USB_PRODUCT_DREAM_CHEEKY_WEBMAIL_NOTIFIER_2, 0)}`. Este es el idioma mínimo de coincidencia por VID/PID. Sin filtrado por subclase ni protocolo; solo fabricante y producto.

El softc es diminuto. Contiene un mutex, el puntero `usb_device`, el array `usb_xfer` y el estado del LED. Este es el mínimo que necesita cualquier driver USB.

El método probe tiene dos líneas: comprueba que el dispositivo está en modo host y devuelve el resultado de `usbd_lookup_id_by_uaa` contra la tabla de coincidencia. Sin comprobación de índice de interfaz, sin coincidencia compleja. Para un dispositivo simple con una única función, esto es suficiente.

El método attach asigna el canal de transferencia, crea una entrada de archivo de dispositivo con `make_dev` y almacena los punteros. Sin negociación compleja; el dispositivo está listo cuando `attach` retorna.

El camino de I/O es una única transferencia de control con una configuración fija. El driver establece la longitud del frame, rellena los bytes de color con `usbd_copy_in` y llama a `usbd_transfer_submit`. Eso es todo.

Lee `uled.c` primero. Cuando lo hayas leído una vez, el resto del subsistema USB se abre ante ti. Cada driver más complejo es una variación sobre este patrón.

### Recorrido 2: ugold.c, incorporando transferencias de interrupción

Archivo: `/usr/src/sys/dev/usb/misc/ugold.c`.

`ugold.c` controla un termómetro USB. Sigue siendo muy corto, con menos de 500 líneas, pero introduce las transferencias de interrupción, que son el recurso habitual de los dispositivos de clase HID.

Aspectos clave que aprender de `ugold.c`:

El dispositivo publica lecturas de temperatura periódicamente a través de un endpoint de interrupción. El trabajo del driver es escuchar en ese endpoint y entregar las lecturas al espacio de usuario a través de `sysctl`.

El array `usb_config` tiene ahora una entrada para `UE_INTERRUPT`, con `UE_DIR_IN`. Esto le indica al framework que configure un canal que sondee el endpoint de interrupción.

El callback de interrupción muestra el patrón canónico: en `USB_ST_TRANSFERRED`, extrae los bytes recibidos con `usbd_copy_out`, analízalos y actualiza el softc. En `USB_ST_SETUP` (incluyendo el callback inicial tras `start`), establece la longitud del frame y envía. En `USB_ST_ERROR`, decide si recuperarte o abandonar.

El driver expone las lecturas a través de nodos `sysctl` creados en `attach` y destruidos en `detach`. Este es un patrón común para dispositivos que producen lecturas ocasionales: el callback de interrupción escribe en el estado del softc, y el espacio de usuario lee de `sysctl` cuando quiere un valor.

Compara `ugold.c` con `uled.c` después de leer ambos. El driver de solo transferencia de control y el driver de transferencia de interrupción representan los dos patrones de esqueleto más comunes. La mayoría de los demás drivers USB son variaciones de estos dos.

### Recorrido 3: udbp.c, transferencias bulk bidireccionales

Archivo: `/usr/src/sys/dev/usb/misc/udbp.c`.

`udbp.c` es el driver USB Double Bulk Pipe. Existe para probar el flujo de datos bulk bidireccional entre dos ordenadores conectados por un cable especial USB a USB. Tiene unas 700 líneas y te proporciona un ejemplo completo y funcional de lectura y escritura bulk.

Aspectos clave que aprender de `udbp.c`:

El `usb_config` tiene dos entradas: una para `UE_BULK` `UE_DIR_OUT` (del host al dispositivo) y otra para `UE_BULK` `UE_DIR_IN` (del dispositivo al host). Este es el patrón bulk dúplex estándar.

Cada callback realiza la misma danza de tres estados. En `USB_ST_SETUP`, establece la longitud del frame (o si es una lectura, simplemente envía). En `USB_ST_TRANSFERRED`, consume los datos completados y rearma. En `USB_ST_ERROR`, decide la política de recuperación.

El driver usa el framework netgraph para integrarse con las capas superiores. Esta es una elección específica del dispositivo Double Bulk Pipe. Para una aplicación simple, expondrías los canales bulk a través de un dispositivo de caracteres, como hace `myfirst_usb`.

Rastrea cómo el softc mantiene el estado de cada dirección de forma independiente. El callback de recepción rearma solo cuando hay un buffer disponible. El callback de transmisión rearma solo cuando hay algo que enviar. Los dos callbacks se coordinan únicamente a través de campos compartidos del softc (contador de operaciones pendientes, punteros de cola).

### Recorrido 4: uplcom.c, un puente USB a serie

Archivo: `/usr/src/sys/dev/usb/serial/uplcom.c`.

`uplcom.c` controla el Prolific PL2303, uno de los chips USB a serie más comunes. Con unas 1400 líneas, es más extenso que los tres anteriores, pero cada parte de él se corresponde directamente con el patrón de driver serie de la Sección 4 de este capítulo.

Aspectos clave que aprender de `uplcom.c`:

La estructura `ucom_callback` rellena cada método de configuración que esperarías que implementara un driver real: `ucom_cfg_open`, `ucom_cfg_param`, `ucom_cfg_set_dtr`, `ucom_cfg_set_rts`, `ucom_cfg_set_break`, `ucom_cfg_get_status`, `ucom_cfg_close`. Cada uno de ellos llama a las primitivas `ucom` proporcionadas por el framework después de emitir la transferencia de control USB específica del chip.

Observa `uplcom_cfg_param`. Toma una estructura `termios`, extrae la velocidad en baudios y el encuadre, y construye una transferencia de control específica del fabricante para programar el chip. Así es como una llamada de usuario `stty 9600` se propaga a través de las capas: `stty` actualiza `termios`, la capa TTY llama a `ucom_param`, el framework planifica la transferencia de control, y `uplcom_cfg_param` programa el chip.

Compara `uplcom_cfg_param` con la función correspondiente en `uftdi.c`. Ambos traducen un `termios` a una secuencia de control específica del fabricante, pero los protocolos de cada fabricante son completamente distintos. Esto ilustra por qué el framework insiste en drivers por fabricante: cada chip tiene su propio conjunto de comandos, y el trabajo del framework es únicamente dar a cada driver una forma uniforme de ser llamado.

Observa cómo el driver gestiona el reset, las señales del módem y la señal break. Cada operación de línea de módem es una transferencia de control USB independiente. El coste de cambiar, por ejemplo, DTR es un viaje de ida y vuelta al dispositivo, que en un bus de 12 Mbps tarda unos 1 ms. Esto te explica por qué las señales de línea cambian más lentamente a través de USB a serie que a través de un UART nativo, y por qué los protocolos que conmutan DTR con frecuencia pueden comportarse de forma diferente a través de un adaptador USB a serie.

### Recorrido 5: uhid.c, el driver de dispositivos de interfaz humana

Archivo: `/usr/src/sys/dev/usb/input/uhid.c`.

`uhid.c` es el driver HID genérico. HID son las siglas de Human Interface Device (dispositivo de interfaz humana); engloba teclados, ratones, mandos de juego, pantallas táctiles e innumerables dispositivos específicos de fabricante que cumplen con el estándar de clase HID. `uhid.c` tiene aproximadamente 1000 líneas.

Aspectos clave que aprender de `uhid.c`:

La tabla de coincidencia usa coincidencia basada en clase. En lugar de listar cada VID/PID, el driver coincide con cualquier dispositivo que anuncie la clase de interfaz HID. `UIFACE_CLASS(UICLASS_HID)` le indica al framework que coincida con cualquier interfaz HID, independientemente del fabricante del dispositivo.

El driver expone el dispositivo a través de un dispositivo de caracteres, no a través de `ucom` ni de un framework de red. El patrón de dispositivo de caracteres permite a los programas del espacio de usuario abrir `/dev/uhidN` y emitir llamadas `ioctl` para leer descriptores HID, leer informes y establecer informes de características.

El endpoint de interrupción entrega informes HID, y el driver los pasa al espacio de usuario a través de un buffer en anillo y `read`. Este es el equivalente USB de un bucle de lectura por interrupciones en un dispositivo de caracteres.

Estudia cómo `uhid.c` usa el descriptor de informe HID para entender qué es el dispositivo. El descriptor se analiza en el momento del attach, y el driver rellena sus tablas internas a partir del análisis. Todo dispositivo HID se describe así; el driver no codifica de forma rígida la semántica del dispositivo.

### Cómo estudiar un driver que nunca has visto

Más allá del recorrido, encontrarás drivers en el árbol que nunca has visto. Una estrategia de lectura de propósito general resulta de gran ayuda:

Abre el archivo fuente y desplázate hasta el final. Allí están las macros de registro. Te indican a qué se conecta el driver (`uhub`, `usb`) y su nombre (`udbp`, `uhid`). Ya sabes dónde encaja el driver en el árbol.

Desplázate hacia arriba hasta el array `usb_config` (o las declaraciones de transferencia para drivers no USB). Cada entrada es un canal. Cuéntalos; fíjate en sus tipos y direcciones. Ahora conoces la forma del camino de datos.

Observa el método probe. Si coincide por VID/PID, el dispositivo es específico de fabricante. Si coincide por clase, el driver admite una familia de dispositivos. Esto te dice el alcance del driver.

Observa el método attach. Sigue su cadena de gotos etiquetados. Las etiquetas te dan el orden de asignación de recursos: mutex, canales, dispositivo de caracteres, sysctls, etc.

Por último, observa los callbacks del camino de datos. Cada uno es una máquina de estados de tres estados. Lee `USB_ST_TRANSFERRED` primero; ahí es donde ocurre el trabajo real. Luego lee `USB_ST_SETUP`; ese es el punto de arranque. Luego lee `USB_ST_ERROR`; esa es la política de recuperación.

Con este orden de lectura, puedes entender cualquier driver USB del árbol en unos 15 minutos. Con la práctica, comenzarás a reconocer patrones en los drivers y sabrás cuáles son idiomáticos (los que conviene copiar) y cuáles son curiosidades históricas (los que conviene entender pero no copiar).

### Más allá del recorrido

El árbol `/usr/src/sys/dev/usb/` tiene cuatro subdirectorios que merece la pena explorar:

`/usr/src/sys/dev/usb/misc/` contiene drivers simples de un solo propósito: `uled`, `ugold`, `udbp`. Si estás escribiendo un nuevo driver específico de dispositivo que no encaja en una clase existente, lee los drivers de aquí para ver cómo se estructuran los drivers pequeños.

`/usr/src/sys/dev/usb/serial/` contiene los drivers puente USB a serie: `uftdi`, `uplcom`, `uslcom`, `u3g` (módems 3G, que se presentan como serie al espacio de usuario), `uark`, `uipaq`, `uchcom`. Si estás escribiendo un nuevo driver USB a serie, empieza aquí.

`/usr/src/sys/dev/usb/input/` contiene drivers de teclado, ratón y HID: `ukbd`, `ums`, `uhid`. Si estás escribiendo un nuevo driver de entrada, estos son los patrones que debes seguir.

`/usr/src/sys/dev/usb/net/` contiene drivers de red USB: `axge`, `axe`, `cdce`, `ure`, `smsc`. Estos son los drivers que conectan el Capítulo 26 con el Capítulo 27, porque combinan el framework USB de este capítulo con el framework `ifnet(9)` del siguiente. Leer uno de ellos al terminar el Capítulo 27 es un ejercicio muy provechoso.

El árbol `/usr/src/sys/dev/uart/` tiene menos archivos, pero cada uno merece la pena:

`/usr/src/sys/dev/uart/uart_core.c` es el núcleo del framework. Léelo para entender qué ocurre por encima de tu driver: cómo fluyen los bytes de entrada y salida, cómo se conecta la capa TTY y cómo se despachan las interrupciones.

`/usr/src/sys/dev/uart/uart_dev_ns8250.c` es el driver de referencia canónico. Léelo después del núcleo del framework para ver cómo se conecta un driver.

`/usr/src/sys/dev/uart/uart_bus_pci.c` muestra el código de pegamento para el attach al bus PCI en los UART. Si alguna vez necesitas escribir un driver UART que se conecte a PCI, este es tu punto de partida.

Cada uno de estos archivos es lo bastante pequeño como para leerlo de una sentada. Leer el código fuente no es una tarea obligatoria; es la forma en que aprendes un subsistema. El capítulo 26 te ha dado el vocabulario y el modelo mental; el código fuente es donde los aplicas.

## Consideraciones de rendimiento para drivers de transporte

La mayor parte del capítulo 26 se ha centrado en la corrección: conseguir que un driver se vincule, haga su trabajo y se desvincule de forma limpia. La corrección siempre va primero. Pero una vez que tu driver funciona, a menudo querrás saber lo rápido que es y si su rendimiento se ajusta a lo que el transporte puede sostener. Esta sección te ofrece un marco práctico para pensar en el rendimiento de USB y UART sin convertir el capítulo en un manual de benchmarking.

### El bus USB como recurso compartido

Todos los dispositivos de un bus USB comparten ese bus con el resto de dispositivos. El ancho de banda no se reparte de forma equitativa, sino que se asigna según las reglas de planificación del USB. Los endpoints de control e interrupción reciben servicio periódico garantizado. Los endpoints de tipo bulk obtienen lo que sobra, en un sentido de reparto equitativo. Los endpoints isócronos reservan ancho de banda de antemano; si no hay suficiente, la asignación falla.

Para un driver que realiza transferencias bulk, la conclusión práctica es la siguiente. Tu ancho de banda efectivo es la velocidad teórica del enlace (12, 480, 5000 Mbps) menos el overhead del tráfico periódico de otros dispositivos, menos el overhead del protocolo USB (aproximadamente un 10% en full-speed, menos en velocidades superiores), menos el overhead de las transferencias cortas.

El último factor es el que puedes controlar. Una transferencia de 16 KB no es 16 veces más costosa que una de 1 KB; el overhead de iniciar y completar una transferencia es fijo, y la parte correspondiente a la propia transferencia de datos crece de forma casi lineal con el tamaño. Para transferencias bulk de alto rendimiento, usa buffers grandes. El hardware está diseñado para ello; el framework está diseñado para ello; tu driver debería estarlo también.

Para un driver que trabaja con transferencias de interrupción, la restricción es diferente. El endpoint de interrupción realiza un sondeo a un intervalo fijo (configurado por el dispositivo). El framework invoca un callback cada vez que se completa la transferencia sondeada. La frecuencia máxima de notificación es la tasa de sondeo del endpoint. Si el dispositivo tiene un intervalo de 1 ms, obtendrás como máximo 1000 notificaciones por segundo. Planificar el rendimiento de un driver basado en interrupciones significa planificar en torno a la tasa de sondeo.

### Latencia: qué cuesta microsegundos, qué cuesta milisegundos

USB no es un bus de baja latencia. Una sola transferencia de control en USB full-speed tarda aproximadamente 1 ms en el viaje de ida y vuelta. Una sola transferencia bulk supone aproximadamente 1 ms de overhead de trama más el tiempo necesario para mover los datos. Las transferencias de interrupción se planifican según el intervalo de sondeo, por lo que la latencia mínima es el propio intervalo.

Compara esto con un UART nativo, donde la transmisión de un carácter tarda aproximadamente 1 ms a 9600 baudios, 100 us a 115200 baudios, y 10 us a 1 Mbps. Un driver de UART nativo puede enviar un byte en cientos de microsegundos si está bien diseñado; un puente USB-a-serie no puede igualar eso, porque cada byte debe atravesar primero el USB.

Para tu driver, esto significa: piensa en qué partes de tu caso de uso la latencia es relevante. Si estás construyendo un driver de monitorización que notifica una vez por segundo, USB es perfectamente válido. Si estás construyendo un controlador interactivo en el que el usuario puede notar el tiempo de ida y vuelta de cada carácter, el UART nativo es mucho mejor. Si estás construyendo un bucle de control en tiempo real en el que los caracteres deben recorrerse en decenas de microsegundos, ni USB ni el UART de propósito general son adecuados; necesitas un bus dedicado con tiempos conocidos.

### Cuándo reactivar la transferencia: el dilema clásico del USB

Una decisión clave en cualquier driver USB de streaming es en qué punto del callback reactivar la transferencia. Hay dos patrones viables:

**Reactivar después del trabajo.** En `USB_ST_TRANSFERRED`, realiza el trabajo (analiza los datos, pásalos hacia arriba, actualiza el estado) y luego reactiva la transferencia. Es sencillo de implementar. Tiene un coste en latencia: el tiempo entre la finalización anterior y la siguiente solicitud es el tiempo que tardó en realizarse el trabajo.

**Reactivar antes del trabajo, usando múltiples buffers.** En `USB_ST_TRANSFERRED`, reactiva inmediatamente con un buffer nuevo y luego realiza el trabajo sobre el buffer que acaba de completarse. Esto requiere múltiples `frames` en el `usb_config` (para que el framework rote entre un pool de buffers) o dos canales de transferencia paralelos. Tiene una latencia casi nula entre transferencias porque el hardware siempre dispone de un buffer preparado.

La mayoría de los drivers del árbol usan el primer patrón porque es más sencillo. El segundo patrón se usa en drivers de alto rendimiento donde importa ocultar la latencia del trabajo. `ugold.c` sigue el primer patrón; algunos de los drivers de USB Ethernet en `/usr/src/sys/dev/usb/net/` siguen el segundo.

### Dimensionamiento de buffers

Para las transferencias bulk, el tamaño del buffer es un parámetro ajustable. Los buffers más grandes amortizan el overhead por transferencia, pero también retrasan la entrega de datos parciales e incrementan el uso de memoria. Los valores típicos en el árbol oscilan entre 1 KB y 64 KB.

Para las transferencias de interrupción, el tamaño del buffer suele ser pequeño (de 8 a 64 bytes) porque el propio endpoint limita el tamaño de la notificación. No lo hagas mayor que el `wMaxPacketSize` del endpoint; el espacio de buffer adicional se desperdicia.

Para las transferencias de control, el tamaño del buffer viene determinado por el protocolo de la operación concreta. La cabecera `usb_device_request` tiene siempre 8 bytes; la parte de datos depende de la solicitud.

### Rendimiento del UART

Para un driver de UART, el rendimiento es habitualmente una cuestión de eficiencia en la gestión de interrupciones. Un 16550A con una profundidad de FIFO de 16 bytes a 115200 baudios necesita ser atendido aproximadamente cada 1.4 ms en el peor caso. Si tu manejador de interrupciones tarda más que eso, el FIFO desborda y se pierden datos. Los UART modernos (variantes 16750, 16950, ns16550 en SoCs embebidos) suelen tener FIFOs más profundas (64, 128 o 256 bytes) precisamente para relajar esta restricción.

El framework `uart(4)` gestiona por ti el manejo del FIFO a través de `uart_ops->rxready` y el buffer circular. Lo que controlas como autor del driver es: la rapidez de tu implementación de `getc`, la rapidez de `putc`, y si tu manejador de interrupciones está compartiendo la CPU con otro trabajo.

Para velocidades en baudios más altas (921600, 1.5M, 3M), un 16550A puro no es suficiente. Estas velocidades requieren bien un chip con una FIFO mayor, bien un driver que use DMA para mover caracteres directamente a memoria. El framework `uart(4)` admite drivers respaldados por DMA, pero la gran mayoría de los drivers (incluido `ns8250`) no lo usan. El soporte de DMA suele reservarse para plataformas embebidas que lo proporcionan de forma específica.

### Concurrencia y tiempos de retención de locks

Un callback USB se ejecuta con el mutex del driver adquirido. Si el callback tarda mucho tiempo (copiando un buffer grande, realizando un procesamiento complejo), ningún otro callback puede ejecutarse y ningún detach puede completarse. Mantén el trabajo del callback lo más breve posible.

El patrón idiomático para trabajo no trivial es: en el callback, copia los datos del buffer del framework a un buffer privado en el softc, luego marca los datos como listos y despierta a un consumidor. El consumidor (el espacio de usuario mediante `read`, o un taskqueue trabajador) realiza el procesamiento pesado sin el mutex del driver.

Para un driver de UART, se aplica el mismo principio. Los métodos `rxready` y `getc` deben ser rápidos porque se ejecutan en contexto de interrupción. El procesamiento pesado se realiza después, fuera de la interrupción, por la capa TTY y los procesos de usuario.

### Mide, no supongas

La mejor forma de responder a una pregunta de rendimiento es medir. Los hooks de `dtrace` sobre `usbd_transfer_submit` y funciones relacionadas te permiten medir el tiempo de las transferencias con precisión de microsegundos. `sysctl -a | grep usb` expone estadísticas por dispositivo. Para los UART, `sysctl -a dev.uart` y las estadísticas TTY de `vmstat` te dicen dónde se consume el tiempo.

No optimices un driver a ciegas. Ejecuta la carga de trabajo, mide, localiza el cuello de botella y corrige lo que realmente importa. En la mayoría de los drivers, el cuello de botella no es la transferencia en sí, sino algo que la rodea: la asignación de memoria, el locking, o un buffer mal dimensionado.

## Errores comunes al escribir tu primer driver de transporte

Los patrones de este capítulo son la forma correcta de escribir un driver de transporte. Pero los patrones son más fáciles de describir que de aplicar. La mayoría de los drivers escritos por primera vez siguen cada patrón correctamente en teoría, pero lo aplican mal en la práctica. Esta sección enumera los errores concretos que aparecen con más frecuencia cuando alguien se sienta a escribir un driver USB o UART por primera vez. Cada error va acompañado de su corrección y una breve explicación de por qué dicha corrección es necesaria.

Lee esta sección una vez antes de escribir tu primer driver, y de nuevo cuando estés depurando uno. Los errores son sorprendentemente universales; casi todos los autores de drivers de FreeBSD con experiencia han cometido varios de ellos en algún momento.

### Error 1: adquirir el mutex del framework explícitamente en un callback

El error tiene este aspecto:

```c
static void
my_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
    struct my_softc *sc = usbd_xfer_softc(xfer);

    mtx_lock(&sc->sc_mtx);   /* <-- wrong */
    /* ... do work ... */
    mtx_unlock(&sc->sc_mtx);
}
```

El framework ya ha adquirido el mutex antes de llamar al callback. Tomarlo una segunda vez provoca un auto-deadlock en la mayoría de las implementaciones de mutex y una adquisición extra no disputada en otras. En algunas configuraciones del kernel, entrará en pánico de inmediato con una aserción de «recursive lock» del subsistema WITNESS.

La corrección consiste simplemente en no adquirir el lock. El framework garantiza que los callbacks se invocan con el mutex del softc ya adquirido. Tu callback simplemente realiza su trabajo y retorna; el framework libera el mutex al retornar.

### Error 2: llamar a primitivas del framework sin el mutex adquirido

El error opuesto también es habitual:

```c
static int
my_userland_write(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct my_softc *sc = dev->si_drv1;

    /* no lock taken */
    usbd_transfer_start(sc->sc_xfer[MY_BULK_TX]);   /* <-- wrong */
    return (0);
}
```

La mayoría de las primitivas del framework (`usbd_transfer_start`, `usbd_transfer_stop`, `usbd_transfer_submit`) esperan que el llamador tenga el mutex asociado adquirido. Llamarlas sin el mutex es una condición de carrera: el propio estado del framework puede ser modificado por un callback concurrente mientras emites la primitiva.

La corrección consiste en adquirir el mutex alrededor de la llamada:

```c
mtx_lock(&sc->sc_mtx);
usbd_transfer_start(sc->sc_xfer[MY_BULK_TX]);
mtx_unlock(&sc->sc_mtx);
```

Este es el patrón idiomático. El framework proporciona el locking; el driver proporciona el mutex.

### Error 3: olvidar el manejo de `USB_ERR_CANCELLED`

El framework usa `USB_ERR_CANCELLED` para indicar a un callback que su transferencia está siendo desmontada (típicamente durante el detach). Si tu callback trata este error de la misma manera que otros errores (por ejemplo, reactivando la transferencia), el detach se quedará bloqueado indefinidamente porque la transferencia nunca se detiene realmente.

El patrón correcto es:

```c
case USB_ST_ERROR:
    if (error == USB_ERR_CANCELLED) {
        return;   /* do not rearm; the framework is tearing us down */
    }
    /* handle other errors, possibly rearm */
    break;
```

Omitir la comprobación de cancelación es una de las razones más comunes por las que un driver se desvincula limpiamente en desarrollo (porque el contador de referencias resulta ser cero) pero se bloquea en producción (porque había una lectura en curso cuando se ejecutó el detach).

### Error 4: enviar datos a un canal que no se ha iniciado

Un canal de transferencia está inactivo hasta que se ha llamado a `usbd_transfer_start` sobre él. Llamar a `usbd_transfer_submit` sobre un canal inactivo es una no-operación en algunas versiones del framework y provoca un pánico en otras.

El patrón correcto es llamar a `usbd_transfer_start` desde trabajo iniciado en el espacio de usuario (en respuesta a un open, por ejemplo) y dejar el canal activo hasta el detach. No llames a `usbd_transfer_submit` directamente; deja que `usbd_transfer_start` planifique el primer callback y reactiva desde `USB_ST_SETUP` o `USB_ST_TRANSFERRED`.

### Error 5: asumir que `USB_GET_STATE` devuelve el estado real del hardware

`USB_GET_STATE(xfer)` devuelve el estado que el framework quiere que el callback gestione en ese momento. No informa del estado subyacente del hardware. Los tres estados `USB_ST_SETUP`, `USB_ST_TRANSFERRED` y `USB_ST_ERROR` son conceptos del framework, no del hardware.

En particular, `USB_ST_TRANSFERRED` significa «el framework considera que esta transferencia se ha completado». Si el hardware se comporta de forma incorrecta (interrupciones de finalización de transferencia espurias, finalizaciones divididas), el callback puede invocarse con `USB_ST_TRANSFERRED` incluso cuando la transferencia real no se ha vaciado por completo. Esto es poco habitual, pero cuando estés depurando, no asumas que el estado del framework es la verdad absoluta sobre el hardware.

### Error 6: usar `M_WAITOK` en un callback

Un callback USB se ejecuta en un entorno donde no está permitido dormir. Las asignaciones de memoria en un callback deben usar `M_NOWAIT`. Usar `M_WAITOK` provocará una aserción o un pánico.

Una versión más sutil de este error consiste en llamar a una función auxiliar que internamente utiliza `M_WAITOK`. Por ejemplo, algunos helpers del framework pueden bloquearse; llamarlos desde un callback está prohibido. Si necesitas realizar trabajo que requeriría bloquearse (una consulta DNS, I/O de disco, transferencias de control USB desde un callback USB), ponlo en cola en un taskqueue y deja que el worker del taskqueue realice el trabajo fuera del callback.

### Error 7: Olvidarse de `MODULE_DEPEND` en `usb`

Un módulo de driver USB que no declara `MODULE_DEPEND(my, usb, 1, 1, 1)` fallará al cargarse con un error críptico de símbolo no resuelto:

```text
link_elf_obj: symbol usbd_transfer_setup undefined
```

El símbolo no está definido porque el módulo `usb` no se ha cargado, y el enlazador no puede resolver la dependencia del driver sobre él. Añadir la directiva `MODULE_DEPEND` correcta hace que el cargador de módulos del kernel cargue automáticamente `usb` antes que tu driver, lo que resuelve el símbolo y permite que tu driver haga el attach.

Todo driver USB debe tener `MODULE_DEPEND(drivername, usb, 1, 1, 1)`. Todo driver del framework UART debe tener `MODULE_DEPEND(drivername, uart, 1, 1, 1)`. Todo driver `ucom(4)` debe depender tanto de `usb` como de `ucom`.

### Error 8: Estado mutable en una ruta de solo lectura

Imagina un driver que expone un campo de estado a través de un `sysctl`. El manejador del sysctl lee el campo del softc sin tomar el mutex:

```c
static int
my_sysctl_status(SYSCTL_HANDLER_ARGS)
{
    struct my_softc *sc = arg1;
    int val = sc->sc_status;   /* <-- unlocked read */
    return (SYSCTL_OUT(req, &val, sizeof(val)));
}
```

Si el campo puede ser actualizado por un callback (que se ejecuta bajo el mutex) y leído por el manejador del sysctl (que no toma el mutex), tienes una condición de carrera de datos. En plataformas modernas, las lecturas de tamaño de palabra suelen ser atómicas, por lo que la condición de carrera a menudo es invisible. Pero en plataformas donde no lo son, o cuando el campo es más ancho que una palabra, puedes obtener lecturas fragmentadas.

La corrección es tomar el mutex para la lectura:

```c
mtx_lock(&sc->sc_mtx);
val = sc->sc_status;
mtx_unlock(&sc->sc_mtx);
```

Aunque la condición de carrera sea invisible en x86, tomar el lock documenta tu intención y protege frente a cambios futuros (como ampliar el campo a 64 bits).

### Error 9: Punteros obsoletos tras `usbd_transfer_unsetup`

`usbd_transfer_unsetup` libera los canales de transferencia. El puntero en `sc->sc_xfer[i]` ya no es válido una vez que la llamada retorna. Si cualquier otro código de tu driver usa ese puntero después del unsetup, el comportamiento es indefinido.

La corrección es poner a cero el array después del unsetup:

```c
usbd_transfer_unsetup(sc->sc_xfer, MY_N_TRANSFERS);
memset(sc->sc_xfer, 0, sizeof(sc->sc_xfer));   /* optional but defensive */
```

Más importante aún, estructura tu detach de forma que ningún código del driver pueda observar los punteros obsoletos. Normalmente esto implica establecer un indicador `detaching` en el softc antes de llamar a unsetup, y hacer que cualquier otra ruta de código compruebe el indicador antes de usar los punteros.

### Error 10: No poner a cero el indicador `detaching` del softc en el momento del attach

Si tu softc utiliza un indicador `detaching` para coordinar el detach, el indicador debe comenzar en cero cuando se llama a attach. Normalmente esto es automático (el framework rellena el softc con ceros), pero si tienes algún campo que necesita un valor inicial distinto de cero, ten cuidado de no inicializar `detaching` accidentalmente con un valor distinto de cero.

Un driver que comienza con `detaching = 1` parecerá "hacer el detach antes de haber hecho el attach", lo que se manifiesta como un driver que hace el attach con normalidad pero se niega a responder a cualquier I/O.

### Error 11: Olvidarse de destruir el nodo de dispositivo en el detach

Si tu driver crea un dispositivo de caracteres con `make_dev` en el attach, debes destruirlo con `destroy_dev` en el detach. Olvidarse de esto deja una entrada obsoleta en `/dev` que apunta a memoria liberada. Los programas de espacio de usuario que abran el nodo obsoleto provocarán un panic del kernel.

La corrección es llamar a `destroy_dev(sc->sc_cdev)` en el detach, y siempre antes de que se liberen los campos del softc a los que hace referencia.

Un patrón más robusto es situar la llamada a `destroy_dev` en primer lugar en el detach (antes de cualquier otra limpieza). Esto bloquea nuevas aperturas y espera a que se cierren las existentes, de manera que cuando el resto del detach se ejecuta, ningún código de espacio de usuario puede acceder al driver.

### Error 12: Condición de carrera en la apertura del dispositivo de caracteres

Incluso con `destroy_dev` en el lugar correcto, existe una ventana entre el momento en que el attach tiene éxito y el primer `open()` tiene éxito, durante la cual el estado del driver se está inicializando. Si tu manejador de apertura asume que ciertos campos del softc son válidos, y el attach no ha terminado de inicializarlos cuando llega la primera apertura, el open verá datos basura.

La corrección es llamar a `make_dev` al final del attach, solo después de que todo lo demás esté completamente inicializado. De esta forma, la entrada en `/dev` no aparece hasta que el driver está listo para atender aperturas. Del mismo modo, llama a `destroy_dev` al principio del detach, antes de desmantelar cualquier otra cosa.

### Error 13: Ignorar el propio sistema de locking de la capa TTY

Los drivers UART se integran con la capa TTY, que tiene sus propias reglas de locking. En particular, la capa TTY mantiene `tty_lock` cuando llama a los métodos `tsw_param`, `tsw_open` y `tsw_close` del driver. Si el driver toma otro lock dentro de estos métodos, el orden de lock es `tty_lock -> driver_mutex`. Si cualquier otra ruta de código toma el mutex del driver y luego el lock de TTY, tienes una inversión del orden de lock, y WITNESS lo detectará.

La corrección es respetar el orden de lock que establece el framework. Para los drivers UART, el orden está documentado en `/usr/src/sys/dev/uart/uart_core.c`. Ante cualquier duda, ejecuta bajo WITNESS con `WITNESS_CHECKORDER` activado; detectará cualquier violación de inmediato.

### Error 14: No gestionar datos de longitud cero en la lectura o escritura

Una llamada a `read` o `write` desde el espacio de usuario con un buffer de longitud cero es legal. Tu driver debe gestionarla, ya sea devolviendo cero inmediatamente o propagando la petición de longitud cero a través del framework. Olvidar este caso suele producir un driver que "funciona casi siempre" pero falla en escenarios de prueba extraños.

La corrección más sencilla es:

```c
if (uio->uio_resid == 0)
    return (0);
```

al principio de tus funciones de lectura y escritura.

### Error 15: Copiar datos antes de comprobar el estado de la transferencia

En una ruta de lectura, un error habitual es copiar datos del buffer USB de forma incondicional:

```c
case USB_ST_TRANSFERRED:
    usbd_copy_out(pc, 0, sc->sc_rx_buf, actlen);
    /* hand data up to userland */
    break;
```

Si la transferencia fue una lectura corta (`actlen < wMaxPacketSize`), la copia es correcta para exactamente `actlen` bytes, pero el código del driver puede asumir más. Si la transferencia estaba vacía (`actlen == 0`), la copia no hace nada y cualquier código posterior que opere sobre "datos recién recibidos" trabaja con datos obsoletos de la transferencia anterior.

La corrección es comprobar siempre `actlen` antes de actuar sobre los datos:

```c
case USB_ST_TRANSFERRED:
    if (actlen == 0)
        goto rearm;   /* nothing received */
    usbd_copy_out(pc, 0, sc->sc_rx_buf, actlen);
    /* work with exactly actlen bytes */
rearm:
    /* re-submit */
    break;
```

### Error 16: Asumir que los valores de `termios` están en unidades estándar

Los campos `c_ispeed` y `c_ospeed` de la estructura `termios` contienen valores de tasa de baudios, pero la codificación tiene particularidades históricas. En FreeBSD, las velocidades son valores enteros (9600, 38400, 115200). En algunos otros sistemas, son índices en una tabla. Portar código que asume velocidades basadas en índices a FreeBSD sin verificarlo es una fuente habitual del bug "el driver cree que la tasa de baudios es 13 en lugar de 115200".

La corrección es consultar la implementación real de FreeBSD: `/usr/src/sys/sys/termios.h` y `/usr/src/sys/kern/tty.c`. La tasa de baudios en el `termios` de FreeBSD es una tasa de bits entera. Cuando tu driver recibe un `termios` en `param`, lee `c_ispeed` y `c_ospeed` como enteros.

### Error 17: Omitir `device_set_desc` o `device_set_desc_copy`

La familia de llamadas `device_set_desc` establece la descripción legible por humanos que `dmesg` muestra cuando el dispositivo hace el attach. Sin ella, `dmesg` muestra una etiqueta genérica (como `my_drv0: <unknown>`), lo que resulta confuso para los usuarios y para tu propia depuración.

La corrección es llamar a `device_set_desc` en probe (no en attach), antes de devolver `BUS_PROBE_GENERIC` o similar:

```c
static int
my_probe(device_t dev)
{
    /* ... match check ... */
    device_set_desc(dev, "My Device");
    return (BUS_PROBE_DEFAULT);
}
```

Usa `device_set_desc_copy` cuando la cadena sea dinámica (construida a partir de datos del dispositivo); el framework liberará la copia cuando el dispositivo haga el detach.

### Error 18: `device_printf` en la ruta de datos sin limitación de tasa

La llamada a `device_printf` está bien para mensajes ocasionales. En un callback de la ruta de datos, no lo está, porque cada transferencia imprime una línea en `dmesg` y en la consola. Un flujo de caracteres a 1 Mbps se convierte en una avalancha de mensajes de registro.

La corrección es el patrón `DLOG_RL` del Capítulo 25: limita la tasa de los mensajes de registro de la ruta de datos a uno por segundo, o uno por cada mil eventos, según corresponda. Mantén el registro completo en las rutas de configuración y error; aplica la limitación de tasa en la ruta de datos.

### Error 19: No despertar a los lectores al desconectar el dispositivo

Si un programa de espacio de usuario está bloqueado en `read()` esperando datos y el dispositivo se desconecta, el driver debe despertar al lector y devolver un error (normalmente `ENXIO` o `ENODEV`). Olvidarse de hacer esto deja la lectura bloqueada indefinidamente, lo que supone una fuga de recursos y un cuelgue del proceso.

La corrección es despertar a todos los procesos en espera durante el detach, antes de retornar:

```c
mtx_lock(&sc->sc_mtx);
sc->sc_detaching = 1;
wakeup(&sc->sc_rx_queue);
wakeup(&sc->sc_tx_queue);
mtx_unlock(&sc->sc_mtx);
```

Y en la ruta de lectura, comprueba el indicador tras despertar:

```c
while (sc->sc_rx_head == sc->sc_rx_tail && !sc->sc_detaching) {
    error = msleep(&sc->sc_rx_queue, &sc->sc_mtx, PZERO | PCATCH, "myrd", 0);
    if (error != 0)
        break;
}
if (sc->sc_detaching)
    return (ENXIO);
```

Este es el patrón idiomático y evita el clásico bug del "proceso de espacio de usuario colgado tras desconectar el dispositivo".

### Error 20: Creer que "funciona en mi máquina" es suficiente

Los bugs de los drivers pueden depender del hardware. Un driver que funciona en una máquina puede fallar en otra por diferencias de temporización, diferencias en la entrega de interrupciones o peculiaridades de hardware del controlador USB. Un driver que funciona con un modelo de dispositivo puede fallar con otro modelo de la misma familia por diferencias de firmware.

La corrección es probar en varias máquinas, varios hosts USB (xHCI, EHCI, OHCI) y varios dispositivos si es posible. Cuando algo funciona en uno y falla en otro, la diferencia es información. Haz una traza de ambos, compáralos, y el bug suele quedar claro.

### Qué hacer después de cometer uno de estos errores

Cometerás varios de estos errores. Es normal. La forma de aprender es: depura el fallo, identifica cuál fue el error, comprende por qué causó ese síntoma concreto, y añade la corrección a tu repertorio mental. Lleva un registro de los errores que hayas cometido en la práctica. Cuando veas un nuevo fallo en un driver, consulta tu registro; la respuesta suele ser un error que ya has resuelto en alguna ocasión.

Los errores específicos descritos anteriormente son fruto de la experiencia personal del autor escribiendo y depurando drivers USB y UART en FreeBSD. No son exhaustivos, pero sí representativos de los tipos de problemas que surgen. Leer drivers del árbol de código fuente, participar en los foros de desarrolladores de FreeBSD y enviar tu trabajo a revisión de código son formas de acelerar este tipo de aprendizaje.

## En resumen

El Capítulo 26 te ha llevado por un recorrido extenso. Comenzó con la idea de que un driver específico de transporte es un driver Newbus más un conjunto de reglas sobre cómo funciona el transporte. A continuación, desarrolló las dos capas específicas de transporte en las que nos centramos en la Parte 6: USB y serial.

En el lado USB, aprendiste el modelo host-dispositivo, la jerarquía de descriptores, los cuatro tipos de transferencia y el ciclo de vida de la conexión en caliente. Recorriste un esqueleto completo de driver: la tabla de coincidencias, el método probe, el método attach, el softc, el método detach y las macros de registro. Viste cómo `struct usb_config` declara los canales de transferencia y cómo `usbd_transfer_setup` los da vida. Seguiste la máquina de estados de callback de tres estados a través de transferencias bulk, de interrupción y de control, y viste cómo `usbd_copy_in` y `usbd_copy_out` mueven datos entre el driver y los buffers del framework. Aprendiste las reglas de locking en torno a las operaciones de transferencia y las políticas de reintento que deben elegir los drivers. Al final de la Sección 3, tenías un modelo mental que te permitiría escribir un driver de loopback bulk desde cero.

En el lado serial, aprendiste que la capa TTY se asienta sobre dos frameworks distintos: `uart(4)` para UART conectadas al bus y `ucom(4)` para puentes USB a serial. Viste la estructura de seis métodos de un driver `uart(4)`, el papel de `uart_ops` y `uart_class`, y cómo el driver canónico `ns8250` implementa cada método. Aprendiste cómo la configuración de `termios` fluye desde `stty` a través de la capa TTY hasta la ruta `param` del driver, y cómo se implementa el control de flujo hardware a nivel de registro. Para los dispositivos USB a serial, viste la estructura específica `ucom_callback` y cómo los métodos de configuración traducen los cambios de termios en transferencias de control USB específicas del fabricante.

Para las pruebas, aprendiste a usar `nmdm(4)` para pruebas TTY puras, la redirección USB de QEMU para el desarrollo USB, y un puñado de herramientas de userland (`cu`, `tip`, `stty`, `comcontrol`, `usbconfig`) que hacen el desarrollo de drivers manejable incluso sin acceso constante al hardware. Viste que gran parte del trabajo en un driver no consiste en lidiar con registros a bajo nivel, sino en organizar cuidadosamente el flujo de datos a través de abstracciones bien definidas.

Los laboratorios prácticos y los ejercicios de desafío te proporcionaron problemas concretos en los que trabajar. Cada laboratorio es lo suficientemente breve como para completarlo en una sola sesión, y cada ejercicio de desafío extiende una de las ideas fundamentales del texto principal.

Tres hábitos de capítulos anteriores se extendieron de forma natural al Capítulo 26. La cadena de limpieza con goto etiquetado del Capítulo 25 es el mismo patrón que se utiliza en las rutinas attach de USB y UART. La disciplina de softc como única fuente de verdad del Capítulo 25 se aplica de forma idéntica al estado del driver USB y UART. El patrón de funciones auxiliares que devuelven errno permanece inalterado. Lo que añadió el Capítulo 26 fue vocabulario específico del transporte y abstracciones específicas del transporte construidas sobre esos hábitos.

Hay también un hábito que ha introducido el Capítulo 26 y que te acompañará a partir de ahora: la máquina de estados para los callbacks, con sus tres estados posibles (`USB_ST_SETUP`, `USB_ST_TRANSFERRED`, `USB_ST_ERROR`). Todos los drivers USB la utilizan. Aprender a leer esta máquina de estados es aprender a leer cualquier callback USB del árbol. Cuando abras `uftdi.c`, `ucycom.c`, `uchcom.c` o cualquier otro driver USB, verás el mismo patrón. Reconocerlo es reconocer la abstracción central del framework USB.

Los drivers específicos del transporte son el lugar donde los conceptos abstractos del framework del libro se vuelven concretos. A partir de aquí, cada capítulo de la Parte 6 profundizará tu destreza práctica con un transporte más o con un tipo adicional de servicio del kernel. Los cimientos de Newbus de la Parte 3, los conceptos básicos de dispositivo de caracteres de la Parte 4 y los temas de disciplina de la Parte 5 están todos en juego simultáneamente. Ya no estás aprendiendo conceptos de forma aislada; los estás usando juntos.

## Puente hacia el capítulo 27

El capítulo 27 se adentra en los drivers de red. Gran parte de la estructura te resultará familiar: hay una conexión Newbus, hay un estado por dispositivo (llamado `if_softc` en los drivers de red), hay una tabla de coincidencias, hay una secuencia probe-and-attach, hay consideraciones sobre hot-plug y hay una integración con un framework de nivel superior. Pero el framework de nivel superior aquí es `ifnet(9)`, la abstracción de framework de interfaces para dispositivos de red, y sus convenciones son distintas de las de USB y los dispositivos serie.

Un driver de red no expone un dispositivo de caracteres. Expone una interfaz, visible para el espacio de usuario a través de `ifconfig(8)`, de `netstat -i` y de la capa de sockets. En lugar de `read(2)` y `write(2)`, los drivers de red gestionan la entrada y la salida de paquetes a través del pipeline de la pila de red. En lugar de `termios` para la configuración, gestionan `SIOCSIFFLAGS`, `SIOCADDMULTI`, `SIOCSIFMEDIA` y toda una serie de ioctls específicos de red.

Muchas tarjetas de red también utilizan USB o PCIe como transporte subyacente. Un adaptador USB Ethernet, por ejemplo, se asienta sobre USB (a través de `if_cdce` o un driver específico del fabricante) y expone una interfaz `ifnet(9)`. Una tarjeta Ethernet PCIe se asienta sobre PCIe y también expone una interfaz `ifnet(9)`. El capítulo 27 mostrará cómo el mismo framework `ifnet(9)` se sitúa encima de estos transportes tan distintos y cómo esa separación te permite escribir un driver que se centre en el protocolo a nivel de paquete sin preocuparte por los detalles del transporte.

Algo concreto que encontrarás es el contraste entre la forma en que USB entrega paquetes (como transferencias completadas, un buffer a la vez, con control de flujo explícito en el nivel de transferencia) y la forma en que las tarjetas de red basadas en PCIe los entregan (como eventos DMA del hardware con anillos de descriptores). El pipeline de paquetes de la pila de red está diseñado para ocultar esta diferencia a las capas superiores, pero el autor de un driver debe comprender ambos modelos porque determinan la estructura interna del driver.

El capítulo 27 se ocupará también de los drivers de dispositivos de bloques (almacenamiento). Ese capítulo cubrirá el framework GEOM, que es la infraestructura de dispositivos de bloques en capas de FreeBSD. Los drivers de bloques tienen sus propios patrones: una forma diferente de hacer coincidir dispositivos, una forma diferente de exponer el estado (a través de proveedores y consumidores GEOM) y un modelo de flujo de datos fundamentalmente distinto (operaciones de lectura y escritura sobre sectores, con un sólido modelo de consistencia).

Las partes 7, 8 y 9 cubren a continuación los temas más especializados: servicios del kernel y patrones avanzados del kernel, depuración y pruebas en profundidad, y distribución y empaquetado. Al final del libro, habrás escrito y mantenido drivers a través de varias capas de transporte y varios subsistemas del kernel. La base que has construido en los capítulos 21 a 26 será el terreno común a lo largo de todo ese trabajo.

Por ahora, conserva tu driver `myfirst_usb`. No lo ampliarás en capítulos posteriores, pero los patrones que demuestra volverán a aparecer en contextos de red, almacenamiento y servicios del kernel. Tener tu propio ejemplo funcional a mano, algo que escribiste y entiendes por completo, es un recurso que se amortiza con creces a medida que avanza el libro.

## Referencia rápida

Esta referencia reúne en un solo lugar las APIs, constantes y ubicaciones de archivo más importantes del capítulo 26. Mantenla abierta mientras escribes o lees un driver; es más rápido que volver a buscar cada nombre en el árbol de código fuente.

### APIs del driver USB

| Función | Propósito |
|----------|---------|
| `usbd_lookup_id_by_uaa(table, size, uaa)` | Hace coincidir el argumento attach con la tabla de coincidencias |
| `usbd_transfer_setup(udev, &ifidx, xfer, config, n, priv, mtx)` | Asigna canales de transferencia |
| `usbd_transfer_unsetup(xfer, n)` | Libera canales de transferencia |
| `usbd_transfer_submit(xfer)` | Encola una transferencia para su ejecución |
| `usbd_transfer_start(xfer)` | Activa un canal |
| `usbd_transfer_stop(xfer)` | Desactiva un canal |
| `usbd_transfer_pending(xfer)` | Consulta si hay una transferencia pendiente |
| `usbd_transfer_drain(xfer)` | Espera a que se complete cualquier transferencia pendiente |
| `usbd_xfer_softc(xfer)` | Recupera el softc de una transferencia |
| `usbd_xfer_status(xfer, &actlen, &sumlen, &aframes, &nframes)` | Consulta los resultados de la transferencia |
| `usbd_xfer_get_frame(xfer, i)` | Obtiene el puntero de caché de página para el frame i |
| `usbd_xfer_set_frame_len(xfer, i, len)` | Establece la longitud del frame i |
| `usbd_xfer_set_frames(xfer, n)` | Establece el recuento total de frames |
| `usbd_xfer_max_len(xfer)` | Consulta la longitud máxima de transferencia |
| `usbd_xfer_set_stall(xfer)` | Programa clear-stall en este pipe |
| `usbd_copy_in(pc, offset, src, len)` | Copia en el buffer del framework |
| `usbd_copy_out(pc, offset, dst, len)` | Copia desde el buffer del framework |
| `usbd_errstr(err)` | Convierte un código de error en cadena de texto |
| `USB_GET_STATE(xfer)` | Estado actual del callback |
| `USB_VPI(vendor, product, info)` | Entrada compacta de tabla de coincidencias |

### Tipos de transferencia USB (`usb.h`)

- `UE_CONTROL`: transferencia de control (petición-respuesta)
- `UE_ISOCHRONOUS`: isócrono (periódico, sin reintento)
- `UE_BULK`: bulk (fiable, sin garantía de temporización)
- `UE_INTERRUPT`: interrupt (periódico, fiable)

### Dirección de transferencia USB

- `UE_DIR_IN`: dispositivo al host
- `UE_DIR_OUT`: host al dispositivo
- `UE_ADDR_ANY`: el framework elige cualquier endpoint coincidente

### Estados de callback USB (`usbdi.h`)

- `USB_ST_SETUP`: listo para enviar una nueva transferencia
- `USB_ST_TRANSFERRED`: la transferencia anterior se completó correctamente
- `USB_ST_ERROR`: la transferencia anterior falló

### Códigos de error USB (`usbdi.h`)

- `USB_ERR_NORMAL_COMPLETION`: éxito
- `USB_ERR_PENDING_REQUESTS`: trabajo pendiente
- `USB_ERR_NOT_STARTED`: transferencia no iniciada
- `USB_ERR_CANCELLED`: transferencia cancelada (p. ej., detach)
- `USB_ERR_STALLED`: endpoint bloqueado
- `USB_ERR_TIMEOUT`: timeout expirado
- `USB_ERR_SHORT_XFER`: datos recibidos inferiores a los solicitados
- `USB_ERR_NOMEM`: sin memoria
- `USB_ERR_NO_PIPE`: ningún endpoint coincidente

### Macros de registro

- `DRIVER_MODULE(name, parent, driver, evh, arg)`: registra el driver con el kernel
- `MODULE_DEPEND(name, dep, min, pref, max)`: declara la dependencia del módulo
- `MODULE_VERSION(name, version)`: declara la versión del módulo
- `USB_PNP_HOST_INFO(table)`: exporta la tabla de coincidencias a `devd`
- `DEVMETHOD(name, func)`: declara un método en la tabla de métodos
- `DEVMETHOD_END`: termina la tabla de métodos

### APIs del framework UART

| Función | Cabecera | Propósito |
|----------|--------|---------|
| `uart_getreg(bas, offset)` | `uart.h` | Lee un registro UART |
| `uart_setreg(bas, offset, value)` | `uart.h` | Escribe un registro UART |
| `uart_barrier(bas)` | `uart.h` | Barrera de memoria para acceso a registros |
| `uart_bus_probe(dev, regshft, regiowidth, rclk, rid, chan, quirks)` | `uart_bus.h` | Función auxiliar probe del framework |
| `uart_bus_attach(dev)` | `uart_bus.h` | Función auxiliar attach del framework |
| `uart_bus_detach(dev)` | `uart_bus.h` | Función auxiliar detach del framework |

### Métodos de `uart_ops`

- `probe(bas)`: ¿chip presente?
- `init(bas, baud, databits, stopbits, parity)`: inicializa el chip
- `term(bas)`: apaga el chip
- `putc(bas, c)`: envía un carácter (polling)
- `rxready(bas)`: ¿hay datos disponibles?
- `getc(bas, mtx)`: lee un carácter (polling)

### Métodos de `ucom_callback`

- `ucom_cfg_open`, `ucom_cfg_close`: hooks de apertura/cierre
- `ucom_cfg_param`: termios ha cambiado
- `ucom_cfg_set_dtr`, `ucom_cfg_set_rts`, `ucom_cfg_set_break`, `ucom_cfg_set_ring`: control de señales
- `ucom_cfg_get_status`: lee los bytes de estado de línea y módem
- `ucom_pre_open`, `ucom_pre_param`: hooks de validación (devuelven errno)
- `ucom_ioctl`: manejador ioctl específico del chip
- `ucom_start_read`, `ucom_stop_read`: activa/desactiva la lectura
- `ucom_start_write`, `ucom_stop_write`: activa/desactiva la escritura
- `ucom_tty_name`: personaliza el nombre del nodo de dispositivo TTY
- `ucom_poll`: sondea eventos
- `ucom_free`: limpieza final

### Archivos fuente clave

- `/usr/src/sys/dev/usb/usb.h`: definiciones del protocolo USB
- `/usr/src/sys/dev/usb/usbdi.h`: interfaz del driver USB, códigos `USB_ERR_*`
- `/usr/src/sys/dev/usb/usbdi_util.h`: funciones auxiliares de conveniencia
- `/usr/src/sys/dev/usb/usbdevs.h`: constantes de fabricante y producto (generadas durante la construcción por el sistema de build de FreeBSD a partir de `/usr/src/sys/dev/usb/usbdevs`; no presentes en un árbol de código fuente limpio hasta que se construye el kernel o el driver)
- `/usr/src/sys/dev/usb/controller/`: drivers del controlador host
- `/usr/src/sys/dev/usb/misc/uled.c`: driver de LED sencillo (referencia)
- `/usr/src/sys/dev/usb/serial/uftdi.c`: driver FTDI (referencia)
- `/usr/src/sys/dev/usb/serial/usb_serial.h`: definición de `ucom_callback`
- `/usr/src/sys/dev/usb/serial/usb_serial.c`: framework ucom
- `/usr/src/sys/dev/uart/uart.h`: `uart_getreg`, `uart_setreg`, `uart_barrier`
- `/usr/src/sys/dev/uart/uart_bus.h`: `uart_class`, `uart_softc`, funciones auxiliares de bus
- `/usr/src/sys/dev/uart/uart_cpu.h`: `uart_ops`, integración del lado de la CPU
- `/usr/src/sys/dev/uart/uart_core.c`: cuerpo del framework UART
- `/usr/src/sys/dev/uart/uart_tty.c`: integración UART-TTY
- `/usr/src/sys/dev/uart/uart_dev_ns8250.c`: driver de referencia ns8250
- `/usr/src/sys/dev/ic/ns16550.h`: definiciones de registros 16550
- `/usr/src/sys/dev/nmdm/nmdm.c`: driver null-modem

### Comandos de diagnóstico en espacio de usuario

| Comando | Propósito |
|---------|---------|
| `usbconfig list` | Lista los dispositivos USB |
| `usbconfig -d ugenN.M dump_all_config_desc` | Vuelca los descriptores |
| `usbconfig -d ugenN.M dump_stats` | Estadísticas de transferencia |
| `usbconfig -d ugenN.M reset` | Reinicia el dispositivo |
| `stty -a -f /dev/device` | Muestra los ajustes termios |
| `stty 115200 -f /dev/device` | Establece la velocidad de transmisión |
| `comcontrol /dev/device` | Muestra las señales del módem |
| `cu -l /dev/device -s speed` | Sesión interactiva |
| `tip name` | Conexión por nombre (a través de `/etc/remote`) |
| `kldload mod.ko` | Carga el módulo del kernel |
| `kldunload mod` | Descarga el módulo del kernel |
| `kldstat` | Lista los módulos cargados |
| `dmesg -w` | Muestra en tiempo real los mensajes del kernel |
| `sysctl hw.usb.*` | Consulta el framework USB |
| `sysctl dev.uart.*` | Consulta las instancias UART |

### Opciones de desarrollo estándar

Opciones del kernel en modo debug que deben activarse durante el desarrollo:
- `options INVARIANTS`: verificación de aserciones
- `options INVARIANT_SUPPORT`: necesaria junto con INVARIANTS
- `options WITNESS`: comprobación del orden de locks
- `options WITNESS_SKIPSPIN`: omite spinlocks en WITNESS (rendimiento)
- `options WITNESS_CHECKORDER`: verifica cada adquisición de lock
- `options DDB`: depurador del kernel
- `options KDB`: soporte del depurador del kernel
- `options USB_DEBUG`: registro extensivo de USB

Estas opciones deben activarse en máquinas de desarrollo, no en producción.

## Glosario

Los siguientes términos aparecieron en este capítulo. Algunos son nuevos; otros se introdujeron antes y se repiten aquí por conveniencia. Las definiciones son breves y pretenden ser un recordatorio rápido, no un sustituto de las explicaciones del texto principal.

**Dirección (USB).** Un número del 1 al 127 que el host asigna a un dispositivo durante la enumeración. Cada dispositivo físico en un bus tiene una dirección única.

**Attach.** El método invocado por el framework en el que un driver toma posesión de un dispositivo recién descubierto, asigna recursos, inicializa el estado y comienza a operar. Se empareja con `detach`.

**Transferencia bulk.** Un tipo de transferencia USB diseñado para datos fiables, de alto rendimiento y no críticos en cuanto a tiempo. Se utiliza para almacenamiento masivo, impresoras y adaptadores de red.

**Callout.** Un mecanismo de FreeBSD para programar la ejecución de una función tras un retardo específico. Los drivers lo emplean para timeouts y tareas periódicas.

**Nodo callin.** Un nodo de dispositivo TTY (generalmente `/dev/ttyuN`) cuya apertura bloquea hasta que se detecta portadora. Se usaba históricamente para atender llamadas entrantes de módem.

**Nodo callout.** Un nodo de dispositivo TTY (generalmente `/dev/cuauN`) cuya apertura no espera a detectar portadora. Se utiliza para iniciar conexiones o para dispositivos sin módem.

**CDC ACM.** Communication Device Class, Abstract Control Model. El estándar USB para puertos serie virtuales. En FreeBSD lo gestiona el driver `u3g`.

**Dispositivo de caracteres.** Una abstracción de dispositivo UNIX para dispositivos orientados a bytes. Se expone al espacio de usuario a través de las entradas de `/dev`. Se introdujo en el capítulo 24.

**Class driver.** Un driver USB que gestiona una clase entera de dispositivos (todos los dispositivos HID, todos los dispositivos de almacenamiento masivo) en lugar del producto de un fabricante concreto. Establece la coincidencia mediante la clase, subclase y protocolo de interfaz.

**Clear-stall.** Una operación USB que elimina una condición de stall en un endpoint. La gestiona el framework USB de FreeBSD cuando se llama a `usbd_xfer_set_stall`.

**Configuration (USB).** Un conjunto con nombre de interfaces y endpoints que un dispositivo USB puede exponer. Un dispositivo suele tener una sola configuración, aunque puede tener varias.

**Control transfer.** Un tipo de transferencia USB diseñado para intercambios petición-respuesta pequeños e infrecuentes. Se usa para configuración y estado.

**`cuau`.** Prefijo de nombre para el dispositivo TTY del lado callout de una UART conectada al bus. Ejemplo: `/dev/cuau0`.

**`cuaU`.** Prefijo de nombre para el dispositivo TTY del lado callout de un puerto serie proporcionado por USB. Ejemplo: `/dev/cuaU0`.

**Descriptor (USB).** Una pequeña estructura de datos que un dispositivo USB proporciona para describirse a sí mismo o a uno de sus componentes. Los tipos incluyen descriptores de dispositivo, configuración, interfaz, endpoint y cadena.

**Detach.** El método al que llama el framework cuando un driver libera todos los recursos y se prepara para que el dispositivo desaparezca. Se complementa con `attach`.

**`devd`.** El demonio de eventos de dispositivo de FreeBSD que reacciona a las notificaciones del kernel sobre la conexión y desconexión de dispositivos. Es responsable de cargar automáticamente módulos para los dispositivos recién descubiertos.

**Device (USB).** Un único periférico USB físico conectado a un puerto. Contiene una o más configuraciones.

**DMA.** Direct Memory Access. Un mecanismo por el cual el hardware puede leer o escribir en memoria sin intervención de la CPU. Lo utilizan los controladores de host USB de alto rendimiento y las tarjetas de red PCIe.

**Echo loopback.** Una configuración de prueba en la que un dispositivo repite todo lo que recibe, utilizada para validar el flujo de datos bidireccional.

**Endpoint.** Un canal de comunicación USB dentro de una interfaz. Cada endpoint tiene una dirección (IN u OUT) y un tipo de transferencia. Corresponde a un FIFO de hardware en el dispositivo.

**Enumeration.** El proceso USB por el cual un dispositivo recién conectado es descubierto, se le asigna una dirección y el host lee sus descriptores.

**FIFO (hardware).** Un pequeño buffer en un chip UART o USB que retiene bytes durante la transferencia. El FIFO típico del 16550 es de 16 bytes; muchas UART modernas tienen 64 o 128.

**FTDI.** Una empresa que fabrica chips adaptadores USB a serie muy populares. Los drivers para chips FTDI están en `/usr/src/sys/dev/usb/serial/uftdi.c`.

**`ifnet(9)`.** El framework de FreeBSD para drivers de dispositivos de red. Se trata en el Capítulo 27.

**Interface (USB).** Una agrupación lógica de endpoints dentro de un dispositivo USB. Un dispositivo multifunción puede exponer varias interfaces.

**Interrupt handler.** Una función que el kernel ejecuta en respuesta a una interrupción de hardware. En el contexto de UART, el framework proporciona un interrupt handler predeterminado.

**Interrupt transfer.** Un tipo de transferencia USB diseñado para datos periódicos de bajo ancho de banda y críticos en cuanto a latencia. Se usa para teclados, ratones y dispositivos HID.

**Isochronous transfer.** Un tipo de transferencia USB diseñado para flujos en tiempo real con ancho de banda garantizado pero sin garantía de entrega. Se usa para audio y vídeo.

**`kldload`, `kldunload`.** Comandos de FreeBSD para cargar y descargar módulos del kernel.

**`kobj`.** El framework del kernel orientado a objetos de FreeBSD. Se usa para el despacho de métodos en Newbus y otros subsistemas.

**Match table.** Un array de entradas `STRUCT_USB_HOST_ID` (para USB) o equivalentes que un driver usa para declarar qué dispositivos admite.

**Modem control register (MCR).** Un registro del 16550 que controla las señales de salida del módem (DTR, RTS).

**Modem status register (MSR).** Un registro del 16550 que informa de las señales de entrada del módem (CTS, DSR, CD, RI).

**`nmdm(4)`.** El driver de null-modem de FreeBSD. Crea pares de TTY virtuales enlazados para realizar pruebas. Se carga con `kldload nmdm`.

**ns8250.** Un driver UART compatible con 16550 de referencia para FreeBSD. Se encuentra en `/usr/src/sys/dev/uart/uart_dev_ns8250.c`.

**Pipe.** Un término para designar un canal de transferencia USB bidireccional desde la perspectiva del host. Un host tiene una pipe por endpoint.

**Port (USB).** Un punto de conexión descendente en un hub. Cada puerto puede tener un dispositivo (que a su vez puede ser un hub).

**Probe.** El método al que llama el framework en el que un driver examina un dispositivo candidato y decide si conectarse. Devuelve cero si hay coincidencia, y un errno distinto de cero si lo rechaza.

**Probe-and-attach.** El protocolo de dos fases por el cual Newbus vincula drivers a dispositivos. El probe comprueba la coincidencia; el attach realiza el trabajo.

**Retry policy.** La regla de un driver sobre qué hacer cuando falla una transferencia. Políticas comunes: volver a armar en cada error, volver a armar hasta N veces y luego abandonar, o volver a armar solo para errores concretos.

**Ring buffer.** Un buffer circular de tamaño fijo que usa el framework UART para almacenar datos entre el chip y la capa TTY.

**RTS/CTS.** Request To Send / Clear To Send. Señales de control de flujo por hardware en un puerto serie.

**Softc.** El estado por dispositivo que mantiene un driver. El nombre proviene de "software context", por analogía con el estado de los registros de hardware.

**Stall (USB).** Una señal de un endpoint USB que indica que no está listo para aceptar más datos hasta que el host lo limpie explícitamente.

**`stty(1)`.** Utilidad de espacio de usuario para inspeccionar y cambiar la configuración de TTY. Se corresponde directamente con los campos de `termios`.

**Taskqueue.** Un mecanismo de FreeBSD para diferir trabajo a un thread trabajador. Lo usan drivers que necesitan realizar operaciones que no pueden ejecutarse en un contexto de interrupción.

**`termios`.** Una estructura POSIX que describe la configuración de un TTY: velocidad en baudios, paridad, control de flujo, indicadores de disciplina de línea y muchos otros. Se establece y consulta mediante `tcsetattr(3)` y `tcgetattr(3)` desde el espacio de usuario, o mediante `stty(1)`.

**Transfer (USB).** Una única operación lógica en un canal USB. Puede consistir en un solo paquete o en varios.

**TTY.** Teletype. La abstracción UNIX para un dispositivo serie. I/O carácter a carácter, disciplina de línea, generación de señales y control de terminal.

**`ttydevsw`.** La estructura que un driver TTY usa para registrar sus operaciones en la capa TTY. Es análoga a `cdevsw` para dispositivos de caracteres.

**`ttyu`.** Prefijo de nombre para el dispositivo TTY del lado callin de una UART conectada al bus. Ejemplo: `/dev/ttyu0`.

**`uart(4)`.** El framework de FreeBSD para drivers UART. Gestiona el registro, el buffering y la integración con TTY. Los drivers implementan los métodos hardware de `uart_ops`.

**`uart_bas`.** "UART Bus Access Structure." La abstracción del framework para acceder a los registros de una UART, ocultando si los registros están en espacio de I/O o mapeados en memoria.

**`uart_class`.** El descriptor del framework que identifica una familia de chips UART. Se combina con `uart_ops` para proporcionar al framework todo lo que necesita.

**`uart_ops`.** La tabla de seis métodos específicos del hardware (`probe`, `init`, `term`, `putc`, `rxready`, `getc`) que implementa un driver UART.

**`ucom(4)`.** El framework de FreeBSD para drivers de dispositivos USB a serie. Se sitúa sobre las transferencias USB y proporciona integración con TTY.

**`ucom_callback`.** La estructura que un cliente de `ucom(4)` usa para registrar sus callbacks con el framework.

**`ugen(4)`.** El driver USB genérico de FreeBSD. Expone acceso USB sin procesar a través de `/dev/ugenN.M` para programas de espacio de usuario. Se utiliza cuando ningún driver específico coincide.

**`uhub`.** El driver de FreeBSD para hubs USB (incluido el hub raíz). Un class driver se conecta a `uhub`, no directamente al bus USB.

**`usbconfig(8)`.** Utilidad de espacio de usuario para inspeccionar y controlar dispositivos USB. Puede volcar descriptores, reiniciar dispositivos y enumerar el estado.

**`usb_config`.** Una estructura C que un driver USB usa para declarar cada uno de sus canales de transferencia: tipo, endpoint, dirección, tamaño de buffer, indicadores y callback.

**`usb_fifo`.** Una abstracción del framework USB para nodos `/dev` de flujo de bytes. Es una alternativa genérica a escribir un `cdevsw` personalizado.

**`usb_template(4)`.** El framework de FreeBSD para el lado dispositivo de USB (gadget). Se usa en hardware que puede actuar tanto como host USB como dispositivo USB.

**`usb_xfer`.** Una estructura opaca que representa un único canal de transferencia USB. Se asigna con `usbd_transfer_setup` y se libera con `usbd_transfer_unsetup`.

**`usbd_copy_in`, `usbd_copy_out`.** Funciones auxiliares para copiar datos entre buffers C ordinarios y buffers del framework USB. Deben usarse en lugar del acceso directo a punteros.

**`usbd_lookup_id_by_uaa`.** Función auxiliar del framework que compara un argumento de attach USB con una match table y devuelve cero si hay coincidencia.

**`usbd_transfer_setup`, `_unsetup`.** Las llamadas que asignan y liberan canales de transferencia. Se invocan desde `attach` y `detach` respectivamente.

**`usbd_transfer_submit`.** La llamada que entrega una transferencia al framework para su ejecución en el hardware.

**`usbd_transfer_start`, `_stop`.** Las llamadas que activan o desactivan un canal. La activación dispara un callback en `USB_ST_SETUP`; la desactivación cancela las transferencias en curso.

**`USB_ST_SETUP`, `_TRANSFERRED`, `_ERROR`.** Los tres estados de un callback de transferencia USB, tal como los devuelve `USB_GET_STATE(xfer)`.

**`USB_ERR_CANCELLED`.** El código de error que el framework pasa a un callback cuando una transferencia está siendo destruida (típicamente durante el detach).

**`USB_ERR_STALLED`.** El código de error cuando un endpoint USB devuelve un handshake STALL. Normalmente se gestiona llamando a `usbd_xfer_set_stall`.

**VID/PID.** Vendor ID / Product ID. Un par de números de 16 bits que identifica de forma única un modelo de dispositivo USB.

**`WITNESS`.** Una opción de depuración del kernel de FreeBSD que rastrea el orden de adquisición de locks y avisa sobre violaciones.

**Callin device.** Un dispositivo TTY (con nombre `/dev/ttyuN` o `/dev/ttyUN`) que bloquea al abrir hasta que se activa la señal de detección de portadora (CD) del módem. Lo usan programas que aceptan llamadas entrantes.

**Callout device.** Un dispositivo TTY (con nombre `/dev/cuauN` o `/dev/cuaUN`) que se abre inmediatamente sin esperar la detección de portadora. Lo usan programas que inician conexiones.

**`comcontrol(8)`.** Utilidad de espacio de usuario para controlar opciones de TTY (comportamiento de drenado, DTR, control de flujo) que no están expuestas a través de `stty`.

**Descriptor (USB).** Una estructura de datos que un dispositivo USB devuelve cuando el host solicita su identidad, configuración, interfaces o endpoints. Es jerárquica: el descriptor de dispositivo contiene descriptores de configuración; las configuraciones contienen interfaces; las interfaces contienen endpoints.

**Endpoint (USB).** Un canal de comunicación con nombre y tipo definido dentro de un dispositivo USB. Tiene un número de endpoint (del 1 al 15), una dirección (IN u OUT), un tipo (control, bulk, interrupt, isochronous) y un tamaño máximo de paquete.

**Line discipline.** La capa conectable de la capa TTY que se sitúa entre el driver y el espacio de usuario. Las disciplinas estándar incluyen `termios` (modo canónico y modo raw). Las line disciplines traducen entre bytes crudos y el comportamiento que espera un programa de usuario.

**`msleep(9)`.** La primitiva de suspensión del kernel que se usa para bloquear un thread en un canal con un mutex tomado. Combinada con `wakeup(9)`, implementa patrones productor-consumidor dentro de los drivers.

**`mtx_sleep`.** Un sinónimo de `msleep` que se usa en algunas partes del árbol de código fuente. Funcionalmente idéntico.

**Open/close pair.** Los métodos de dispositivo de caracteres `d_open` y `d_close`. Todo driver que expone un nodo `/dev` debe gestionarlos. Las aperturas son normalmente donde se inician los canales; los cierres son normalmente donde se detienen.

**Short transfer.** Una transferencia USB que se completa con menos bytes de los solicitados. Es normal para bulk IN (donde el dispositivo envía un paquete corto para señalar "fin de mensaje") y para interrupt IN (donde el dispositivo envía un paquete corto cuando tiene menos datos que el máximo). Comprueba siempre `actlen`.

**`USETW`.** Una macro de FreeBSD para establecer un campo de 16 bits en little-endian dentro de un buffer de descriptores USB. El formato de cable USB es siempre little-endian, por lo que `USETW` oculta el intercambio de bytes.

Este glosario no es exhaustivo; cubre los términos que este capítulo realmente utilizó. Para una referencia más amplia de USB en FreeBSD, la página de manual `usbdi(9)` es la fuente definitiva. Para el framework UART, la referencia es el código fuente en `/usr/src/sys/dev/uart/`. Cuando encuentres un término desconocido en cualquiera de esos lugares, consulta aquí primero; si no está definido, acude al código fuente.

### Una nota final sobre la precisión terminológica

Un último consejo sobre vocabulario. Las comunidades de USB, TTY y FreeBSD tienen sus propias distinciones cuidadosas entre términos que parecen sinónimos. Confundirlos en una conversación con desarrolladores más experimentados es la forma más rápida de parecer inseguro; usarlos con precisión es la forma más rápida de sentirte en tu elemento.

"Device" en el contexto USB significa el periférico USB completo (el teclado, el ratón, el adaptador serie). "Interface" significa un agrupamiento lógico de endpoints dentro del dispositivo. Una interfaz implementa una función; un dispositivo puede tener múltiples interfaces. Cuando dices «el dispositivo USB es un composite device», estás diciendo que tiene múltiples interfaces.

"Endpoint" y "pipe" están relacionados pero son distintos. Un endpoint reside en el dispositivo; un pipe es la vista del host sobre una conexión a ese endpoint. En el código de drivers de FreeBSD, el término "transfer channel" (canal de transferencia) se usa con frecuencia en lugar de "pipe", porque "pipe" sobrecarga un significado más común en UNIX.

"Transfer" y "transaction" también son distintos. Una transferencia es una operación lógica (una solicitud de lectura de N bytes); una transacción es el intercambio de paquetes a nivel USB que la lleva a cabo. Una transferencia bulk de 64 bytes a un endpoint con un tamaño máximo de paquete de 64 bytes es una transferencia y una transacción. Una transferencia bulk de 512 bytes al mismo endpoint es una transferencia y ocho transacciones.

"UART" y "serial port" (puerto serie) están estrechamente relacionados pero no son idénticos. Un UART es el chip (o el bloque lógico del chip); un serial port es el conector físico y su cableado. Un UART puede dar soporte a múltiples puertos serie en algunas configuraciones; un serial port siempre está respaldado por exactamente un UART.

"TTY" y "terminal" están relacionados. Un TTY es la abstracción del kernel para I/O carácter a carácter; un terminal es la vista del userland. Un TTY tiene una propiedad de terminal controlador; un terminal tiene un TTY que utiliza. En el código de drivers, TTY es casi siempre el término más preciso.

Usarlos correctamente en la escritura y en los comentarios del código señala que comprendes el diseño. Y cuando lees el código o la documentación de otra persona, fijarte en qué término ha elegido te indica en qué capa de abstracción está pensando.
