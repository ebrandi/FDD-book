---
title: "Integración con el kernel"
description: "El capítulo 24 extiende el driver myfirst de la versión 1.6-debug a la 1.7-integration. Enseña qué significa que un driver deje de ser un módulo del kernel autocontenido y empiece a comportarse como un ciudadano del kernel de FreeBSD en sentido amplio. El capítulo explica por qué la integración es importante; cómo vive un driver dentro de devfs y cómo los nodos de /dev aparecen, obtienen permisos, se renombran y desaparecen; cómo implementar una interfaz ioctl() en la que el espacio de usuario pueda confiar, incluyendo la codificación _IO/_IOR/_IOW/_IOWR y la capa automática copyin/copyout del kernel; cómo exponer métricas, contadores y parámetros configurables del driver mediante árboles sysctl dinámicos con raíz en dev.myfirst.N.; cómo pensar en la integración de un driver con la pila de red a través de ifnet(9) a nivel introductorio usando if_tuntap.c como referencia; cómo pensar en la integración de un driver con el subsistema de almacenamiento CAM a nivel introductorio usando cam_sim_alloc y xpt_bus_register; cómo organizar el registro, el attach, el desmontaje y la limpieza para que los flujos de integración puedan cargarse y descargarse de forma limpia bajo presión; y cómo refactorizar el driver en un paquete mantenible y versionado que los capítulos siguientes puedan seguir extendiendo. El driver incorpora myfirst_ioctl.c, myfirst_ioctl.h, myfirst_sysctl.c y un pequeño programa de prueba complementario; añade un nodo /dev/myfirst0 con soporte de clonación y un subárbol sysctl por instancia; y al terminar el capítulo 24 se convierte en un driver con el que otro software puede comunicarse de la forma nativa en FreeBSD."
partNumber: 5
partName: "Debugging, Tools, and Real-World Practices"
chapter: 24
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 210
language: "es-ES"
---
# Integración con el kernel

## Guía del lector y objetivos

El capítulo 23 nos dejó un driver que por fin sabe explicarse a sí mismo. El driver `myfirst` en la versión `1.6-debug` sabe registrar mensajes estructurados a través de `device_printf`, controlar la verbosidad mediante una máscara sysctl en tiempo de ejecución, exponer puntos de sondeo estáticos a DTrace y dejar un registro de actividad que un operador puede consultar más adelante. Combinado con la disciplina de gestión de energía añadida en el capítulo 22, el pipeline DMA añadido en el capítulo 21 y el mecanismo de interrupciones añadido en los capítulos 19 y 20, el driver es ahora una unidad completa en sí misma: arranca, funciona, se comunica con un dispositivo PCI real, sobrevive a ciclos de suspend y resume, y le indica al desarrollador lo que está haciendo en cada momento.

Lo que el driver aún no hace es comportarse como un componente integrado en el kernel. Todavía hay muy poco de `myfirst` que un programa externo pueda ver, controlar o medir. El driver crea exactamente un nodo de dispositivo cuando se carga el módulo. No hay forma de que una herramienta en espacio de usuario le pida al driver que se reinicie, que cambie un parámetro de configuración en tiempo de ejecución o que devuelva el valor de un contador. No existen métricas en el árbol sysctl del sistema que un operador pueda alimentar a un sistema de monitorización. No hay forma de dar al driver varias instancias de manera limpia. No existe integración con la pila de red, con la pila de almacenamiento, ni con ninguno de los subsistemas del kernel a los que los usuarios acceden habitualmente desde sus propios programas. En todos los sentidos que importan, el driver sigue estando al margen del sistema. El capítulo 24 es el que lo integra en el conjunto.

El capítulo 24 enseña la integración con el kernel al nivel adecuado para esta etapa del libro. En este capítulo aprenderás qué significa la integración en la práctica, por qué importa más de lo que parece a primera vista y cómo se construye cada interfaz de integración. El capítulo comienza con la historia conceptual: la diferencia entre un driver que funciona y un driver integrado, y el coste de dejar la integración como algo secundario. Después dedica la mayor parte del tiempo a las cuatro interfaces con las que un driver FreeBSD típico siempre se integra: el sistema de archivos de dispositivos `devfs`, el canal controlado por el usuario a través de `ioctl(2)`, el árbol `sysctl(8)` del sistema y los hooks de ciclo de vida del kernel para un attach, detach y descarga del módulo limpios. Tras esos cuatro, se presentan dos temas opcionales en formato reducido: uno para drivers cuyo hardware es un dispositivo de red y otro para drivers cuyo hardware es un controlador de almacenamiento. Ambos se introducen a nivel conceptual para que los reconozcas cuando aparezcan más adelante en la parte 6 y la parte 7, y ninguno se enseña en su totalidad porque cada uno merecerá su propio capítulo en su momento. El capítulo da un paso atrás para hablar sobre la disciplina de registro y desmontaje en todas estas interfaces: el orden importa, los caminos de fallo importan, los casos límite bajo carga importan, y un driver que gestiona bien las interfaces de integración pero mal el ciclo de vida sigue siendo un driver frágil. Por último, el capítulo cierra con una refactorización que divide el nuevo código en sus propios archivos, sube el driver a la versión `1.7-integration`, actualiza el banner de versión y deja el árbol de código fuente organizado para todo lo que viene después.

El arco de la parte 5 continúa aquí. El capítulo 22 hizo que el driver sobreviviera a un cambio de estado de energía. El capítulo 23 hizo que el driver te cuente lo que está haciendo. El capítulo 24 hace que el driver encaje de forma natural en el resto del sistema, de modo que las herramientas y los hábitos que los usuarios de FreeBSD ya conocen se apliquen a tu driver sin sorpresas. El capítulo 25 continuará el arco enseñando la disciplina de mantenimiento que mantiene un driver legible, ajustable y extensible a medida que evoluciona, y la parte 6 comenzará entonces los capítulos específicos de transporte que se apoyan en todas las cualidades construidas en los capítulos de la parte 5.

### Por qué la integración con devfs, ioctl y sysctl merece un capítulo propio

Una preocupación que surge aquí es si conectar `devfs`, `ioctl` y `sysctl` merece realmente un capítulo completo. El driver ya tiene un único nodo `cdev` de un capítulo muy anterior. Añadir un ioctl parece algo pequeño. Añadir un sysctl parece aún más pequeño. ¿Por qué extender el trabajo a lo largo de un capítulo largo si cada interfaz parece consistir en unas pocas docenas de líneas de código?

La respuesta es que ver el trabajo como unas pocas docenas de líneas es la parte fácil. Cada interfaz tiene un conjunto de convenciones y trampas que no son evidentes al leer la API una sola vez, y el coste de equivocarse no lo paga el desarrollador, sino el operador que intenta monitorizar el driver, el usuario que intenta reiniciar el dispositivo, el empaquetador que intenta cargar y descargar el módulo bajo carga, y el siguiente desarrollador que intenta extender el driver seis meses después. El capítulo 24 dedica su tiempo a esas convenciones y trampas porque ahí está el valor real.

La primera razón por la que este capítulo se gana su lugar es que **las interfaces de integración son la vía por la que todo lo demás accede al driver**. El lector que ha seguido el libro hasta aquí ha construido un driver que hace trabajo interesante, pero en este momento solo el propio kernel sabe cómo pedirle al driver que haga ese trabajo. Una vez que el driver tiene una interfaz ioctl, un script de shell puede controlarlo. Una vez que el driver tiene un árbol sysctl, un sistema de monitorización puede observarlo. Una vez que el driver crea nodos en `/dev` que siguen las convenciones estándar, un empaquetador puede distribuir reglas estilo udev, el administrador del sistema puede escribir `/etc/devfs.rules` para él, y otro driver puede superponerse sobre él a través de `vop_open` o de `ifnet`. Nada de eso depende de para qué sirva el driver; todo depende de si la integración está bien hecha.

La segunda razón es que **las decisiones de integración se manifiestan en los modos de fallo en producción**. Un driver que llama a `make_dev` desde el contexto equivocado puede producir un deadlock al cargar el módulo. Un driver que omite la disciplina `_IO`, `_IOR`, `_IOW`, `_IOWR` obliga a cada invocador a inventar una convención privada sobre quién copia qué a través de la frontera usuario-kernel, y al menos uno de esos invocadores se equivocará. Un driver que olvida llamar a `sysctl_ctx_free` en el detach filtra OIDs, y la siguiente carga del módulo que usa el mismo nombre falla con un mensaje confuso. Un driver que destruye su `cdev` antes de vaciar sus descriptores de archivo abiertos produce pánicos de tipo use-after-free. El capítulo 24 dedica párrafos a cada uno de estos porque cada uno es un bug real que la comunidad FreeBSD ha tenido que rastrear a lo largo de los años, y el momento adecuado para aprender la disciplina es antes de escribir la primera línea de código de integración, no después.

La tercera razón es que **el código de integración es el primer lugar donde el diseño de un driver se vuelve visible para alguien que no sea su autor**. Hasta el capítulo 24, el driver era una caja negra con una tabla de métodos y un nodo de dispositivo. A partir del capítulo 24, el driver tiene una superficie pública. Los nombres de sus sysctls aparecen en gráficas de monitorización. Los números de sus ioctls aparecen en scripts de shell y en bibliotecas en espacio de usuario. La disposición de sus nodos en `/dev` aparece en la documentación de paquetes y en las guías de operaciones del administrador. Una vez que existe una superficie pública, cambiarla tiene un coste. El capítulo, por tanto, se toma el tiempo de enseñar las convenciones que mantienen la superficie estable a medida que el driver evoluciona. La subida de versión a `1.7-integration` es también la primera versión del driver que tiene una cara pública real; todo lo anterior era un hito interno.

El capítulo 24 se gana su lugar enseñando esas tres ideas juntas, de forma concreta, con el driver `myfirst` como ejemplo recurrente. El lector que termine el capítulo 24 podrá integrar cualquier driver FreeBSD en las interfaces estándar del sistema, conocerá las convenciones y las trampas de cada interfaz de integración, podrá leer el código de integración de otro driver e identificar qué es normal y qué es inusual, y tendrá un driver `myfirst` con el que otro software podrá comunicarse por fin.

### En qué estado dejó el capítulo 23 al driver

Un breve punto de control antes de empezar con el trabajo real. El capítulo 24 extiende el driver producido al final del capítulo 23, etiquetado como versión `1.6-debug`. Si alguno de los elementos siguientes no está claro, vuelve al capítulo 23 y corrígelo antes de comenzar este capítulo, porque los temas de integración asumen que la disciplina de depuración ya existe, y varias de las nuevas interfaces de integración la utilizarán.

- Tu driver compila sin errores y se identifica como `1.6-debug` en `kldstat -v`.
- El driver sigue haciendo todo lo que hacía en `1.5-power`: se engancha a un dispositivo PCI (o PCI simulado), asigna vectores MSI-X, ejecuta un pipeline DMA y sobrevive a `devctl suspend myfirst0` seguido de `devctl resume myfirst0`.
- El driver tiene un par `myfirst_debug.c` y `myfirst_debug.h` en disco. La cabecera define `MYF_DBG_INIT`, `MYF_DBG_OPEN`, `MYF_DBG_IO`, `MYF_DBG_IOCTL`, `MYF_DBG_INTR`, `MYF_DBG_DMA`, `MYF_DBG_PWR` y `MYF_DBG_MEM`. La macro `DPRINTF(sc, MASK, fmt, ...)` está disponible desde cualquier archivo fuente del driver.
- El driver tiene tres sondas SDT llamadas `myfirst:::open`, `myfirst:::close` y `myfirst:::io`. La sencilla instrucción de DTrace en una sola línea `dtrace -n 'myfirst::: { @[probename] = count(); }'` devuelve contadores cuando se ejercita el dispositivo.
- El softc lleva un campo `uint32_t sc_debug`, y `sysctl dev.myfirst.0.debug.mask` lo lee y escribe.
- El driver tiene un documento `DEBUG.md` junto al código fuente. `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`, `DMA.md` y `POWER.md` también están actualizados en tu árbol de trabajo desde capítulos anteriores.
- Tu kernel de pruebas aún tiene habilitados `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS` y `DDB_CTF`. Los laboratorios del capítulo 24 usan el mismo kernel.

Ese es el driver que el capítulo 24 extiende. Los añadidos son más grandes que en el capítulo 23 en líneas de código, pero más pequeños en superficie conceptual. Las nuevas piezas son: un nodo `/dev/myfirst0` más rico que puede clonar instancias bajo demanda, un pequeño conjunto de ioctls bien tipados con una cabecera pública que los programas en espacio de usuario pueden incluir, un subárbol sysctl por instancia bajo `dev.myfirst.N.` que expone un puñado de métricas y un parámetro modificable, una refactorización que divide el nuevo código en `myfirst_ioctl.c` y `myfirst_sysctl.c` con sus cabeceras correspondientes, un pequeño programa auxiliar en espacio de usuario llamado `myfirstctl` que ejercita las nuevas interfaces, un documento `INTEGRATION.md` junto al código fuente, una prueba de regresión actualizada y una subida de versión a `1.7-integration`.

### Qué aprenderás

Una vez que hayas terminado este capítulo, serás capaz de:

- Explicar qué significa la integración con el kernel en términos concretos de FreeBSD, distinguir un driver autocontenido de uno integrado, y enumerar los beneficios concretos visibles para el usuario que aporta cada superficie de integración.
- Describir qué es `devfs`, en qué se diferencia de los esquemas estáticos de `/dev` más antiguos, y cómo los nodos de dispositivo aparecen y desaparecen bajo él. Usar `make_dev`, `make_dev_s`, `make_dev_credf` y `destroy_dev` correctamente. Elegir el conjunto de flags, la propiedad y el modo adecuados para un nodo.
- Inicializar y rellenar una `struct cdevsw` con el campo moderno `D_VERSION`, el conjunto mínimo de callbacks y los callbacks opcionales (`d_kqfilter`, `d_mmap_single`, `d_purge`).
- Usar los campos `cdev->si_drv1` y `si_drv2` para asociar el estado del driver por nodo, y leer ese estado desde dentro de los callbacks de cdevsw.
- Crear más de un nodo de dispositivo a partir de una única instancia del driver y elegir entre nodos de nombre fijo, nodos indexados y nodos clonables mediante el manejador de eventos `dev_clone`.
- Establecer permisos y propietario por nodo en el momento de la creación, y ajustarlos después mediante `devfs.rules` para que los administradores puedan conceder acceso sin necesidad de recompilar el driver.
- Explicar qué es `ioctl(2)`, cómo el kernel codifica los comandos ioctl mediante `_IO`, `_IOR`, `_IOW` y `_IOWR`, qué indica cada macro sobre la dirección del flujo de datos, y por qué codificarlo correctamente importa para la portabilidad entre espacio de usuario de 32 y 64 bits.
- Definir una cabecera ioctl pública para un driver, elegir una letra mágica libre, y documentar cada comando de modo que los llamadores en espacio de usuario puedan confiar en la interfaz entre versiones.
- Implementar un manejador `d_ioctl` que despache según la palabra de comando, ejecute la lógica de cada comando de forma segura, y devuelva el errno correcto en cada ruta de fallo.
- Leer y comprender la capa automática de `copyin`/`copyout` del kernel para datos ioctl, y reconocer los casos en que el driver todavía tiene que copiar memoria por sí mismo: cargas útiles de longitud variable, punteros de usuario embebidos, y estructuras cuyo diseño requiere alineamiento explícito.
- Explicar `sysctl(9)`, distinguir OIDs estáticos de OIDs dinámicos, y recorrer el patrón de `device_get_sysctl_ctx` y `device_get_sysctl_tree` que proporciona a cada dispositivo su propio subárbol.
- Añadir contadores de solo lectura mediante `SYSCTL_ADD_UINT` y `SYSCTL_ADD_QUAD`, añadir parámetros ajustables en escritura con los flags de acceso adecuados, y añadir OIDs procedurales personalizados con `SYSCTL_ADD_PROC` para los casos en que el valor debe calcularse en el momento de la lectura.
- Gestionar tunables que el usuario puede establecer en `/boot/loader.conf` mediante `TUNABLE_INT_FETCH`, y combinar tunables y sysctls de modo que el mismo parámetro de configuración pueda fijarse en el boot o ajustarse en tiempo de ejecución.
- Reconocer la forma introductoria de la integración de red de FreeBSD: cómo se combinan `if_alloc`, `if_initname`, `if_attach`, `bpfattach`, `if_detach` e `if_free`; qué es un `if_t` a nivel conceptual; y qué papel desempeñan los drivers en la maquinaria ifnet más amplia. Comprender que el Capítulo 28 vuelve a este tema en profundidad.
- Reconocer la forma introductoria de la integración de almacenamiento de FreeBSD: qué es CAM, cómo se combinan `cam_sim_alloc`, `xpt_bus_register`, el callback `sim_action` y `xpt_done`; qué es un CCB a nivel conceptual; y por qué existe CAM. Comprender que el material sobre drivers de almacenamiento del Capítulo 27 y el material GEOM del Capítulo 27 vuelven a esto en profundidad.
- Aplicar una disciplina de registro y desmontaje que sea robusta ante repeticiones de `kldload`/`kldunload`, ante fallos de attach a mitad de la inicialización, ante fallos de detach cuando usuarios en curso todavía mantienen descriptores de archivo abiertos, y ante la extracción inesperada del dispositivo subyacente.
- Refactorizar un driver que ha acumulado varias superficies de integración en una estructura mantenible: un archivo separado por cada aspecto de integración, una cabecera pública para el espacio de usuario, una cabecera privada para uso interno del driver, y un sistema de build actualizado que compile todas las piezas en un único módulo del kernel.

La lista es larga porque la integración toca varios subsistemas. Cada punto es acotado y enseñable. El trabajo del capítulo consiste en unir todos esos elementos en un único driver coherente.

### Qué no cubre este capítulo

Varios temas adyacentes se posponen deliberadamente para que el capítulo 24 permanezca centrado en la disciplina de integración.

- **La implementación completa de un driver de red ifnet**, incluyendo colas de transmisión y recepción, coordinación de múltiples colas a través de `iflib(9)`, integración con BPF más allá de la llamada introductoria a `bpfattach`, eventos de estado del enlace y el ciclo de vida completo del driver Ethernet. El capítulo 28 es el capítulo dedicado al driver de red y asume que la disciplina de integración del capítulo 24 ya está asentada.
- **La implementación completa de un driver de almacenamiento CAM**, incluyendo el modo target, el conjunto completo de tipos CCB, notificaciones asíncronas a través de `xpt_setup_ccb` y `xpt_async`, y la presentación de geometría a través de `disk_create` o GEOM. El capítulo 27 cubre la pila de almacenamiento en profundidad.
- **La integración con GEOM**, incluyendo proveedores, consumidores, clases, `g_attach`, `g_detach` y la maquinaria de eventos de GEOM. GEOM es su propio subsistema con sus propias convenciones; el capítulo 27 lo cubre.
- **La concurrencia basada en `epoch(9)`**, que es el patrón de locking moderno para los hot paths de ifnet. El capítulo 24 lo menciona solo en contexto. El capítulo 28 (drivers de red) vuelve sobre él junto a `iflib(9)`, donde la concurrencia estilo epoch es necesaria en la práctica.
- **La integración con `mac(9)` (Mandatory Access Control)**, que añade ganchos de política en torno a las superficies de integración. El framework MAC es un tema especializado que todavía no aplica al sencillo driver `myfirst`.
- **La integración con `vfs(9)`**, que es lo que hacen los sistemas de archivos. Un driver de caracteres no interactúa con VFS en la capa de `vop_open` ni de `vop_read`; interactúa con `cdevsw` y devfs. El capítulo tiene cuidado de no confundir ambos.
- **Las interfaces entre drivers a través de `kobj(9)` y las interfaces personalizadas declaradas con el mecanismo de build `INTERFACE`**. Así es como las pilas de red y almacenamiento definen sus contratos internos. Se mencionan en contexto en la sección 7, pero el tratamiento en profundidad corresponde a un capítulo posterior y más avanzado.
- **La nueva interfaz `netlink(9)` que los kernels recientes de FreeBSD exponen para parte del tráfico de gestión de red**. Netlink lo utiliza actualmente el subsistema de enrutamiento en lugar de los drivers de dispositivo individuales, y el lugar adecuado para enseñarlo es junto al capítulo de red.
- **Los módulos de protocolo personalizados a través de `pr_protocol_init`**, que están pensados para nuevos protocolos de transporte, no para drivers de dispositivo.

Mantenerse dentro de esos límites hace que el capítulo 24 sea un capítulo sobre cómo un driver se integra en el kernel, no un capítulo sobre todos los subsistemas del kernel que un driver puede llegar a tocar.

### Inversión de tiempo estimada

- **Solo lectura**: cuatro o cinco horas. Las ideas del capítulo 24 son en su mayor parte extensiones conceptuales de cosas con las que el lector ya se ha encontrado. El nuevo vocabulario (devfs, cdev, ioctl, sysctl, ifnet, CAM) es mayoritariamente familiar por su nombre de capítulos anteriores; el trabajo del capítulo es dar a cada uno de ellos una forma concreta.
- **Lectura más escritura de los ejemplos resueltos**: de diez a doce horas repartidas en dos o tres sesiones. El driver evoluciona a través de tres superficies de integración de forma sucesiva (devfs, ioctl, sysctl), cada una con su propia etapa corta. Cada etapa es breve y autocontenida; las pruebas son lo que lleva tiempo, porque las superficies de integración se comprueban mejor escribiendo un pequeño programa en espacio de usuario que las ejercite.
- **Lectura más todos los laboratorios y desafíos**: de quince a dieciocho horas repartidas en tres o cuatro sesiones. Los laboratorios incluyen un experimento de devfs con soporte de clones, un roundtrip completo de ioctl con `myfirstctl`, un ejercicio de monitorización de contadores mediante sysctl, un laboratorio de disciplina de limpieza que rompe intencionadamente el teardown para exponer el patrón de fallo, y un pequeño desafío de stub de ifnet para lectores que quieran un adelanto del capítulo 28.

Las secciones 3 y 4 son las más densas en cuanto a vocabulario nuevo. Las macros de ioctl y las firmas de callback de sysctl son las únicas APIs verdaderamente nuevas del capítulo; el resto es composición. Si las macros resultan opacas en una primera lectura, es normal. Detente, ejecuta el ejercicio de correspondencia sobre el driver y vuelve cuando la forma haya quedado asentada.

### Requisitos previos

Antes de comenzar este capítulo, comprueba lo siguiente:

- El código fuente de tu driver coincide con el Stage 3 del capítulo 23 (`1.6-debug`). El punto de partida asume todos los primitivos del capítulo 23: la macro `DPRINTF`, las sondas SDT, el sysctl de máscara de depuración y el par de archivos `myfirst_debug.c`/`myfirst_debug.h`. El capítulo 24 construye nuevo código de integración que utiliza cada uno de ellos en los momentos adecuados.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con `/usr/src` en disco y coincidiendo con el kernel en ejecución.
- Un kernel de depuración con `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS` y `DDB_CTF` está compilado, instalado y arrancando de forma limpia.
- `bhyve(8)` o `qemu-system-x86_64` está disponible, y tienes una instantánea de VM utilizable en el estado `1.6-debug`. Los laboratorios del capítulo 24 incluyen escenarios de fallo intencionados para la sección de disciplina de limpieza, y una instantánea hace que la recuperación sea sencilla.
- Los siguientes comandos del espacio de usuario están en tu path: `dmesg`, `sysctl`, `kldstat`, `kldload`, `kldunload`, `devctl`, `cc`, `make`, `dtrace`, `dd`, `head`, `cat`, `chown`, `chmod` y `truss`. Los laboratorios del capítulo 24 hacen un uso ligero de `truss`, el equivalente en FreeBSD de `strace` en Linux, para verificar que los programas del espacio de usuario realmente alcanzan el driver a través de los nuevos ioctls.
- Te sientes cómodo escribiendo un programa corto en C usando las cabeceras de `libc` de FreeBSD. El capítulo introduce el lado del espacio de usuario de los nuevos ioctls a través de un pequeño programa llamado `myfirstctl`.
- Conocer `git` es útil pero no imprescindible. El capítulo recomienda que hagas un commit entre etapas para que cada versión del driver tenga un punto de recuperación.

Si alguno de los puntos anteriores no está claro, resuélvelo antes de continuar. El código de integración es, en términos generales, menos peligroso que el trabajo en modo kernel de los capítulos anteriores, porque la mayoría de los modos de fallo se detectan al cargar el módulo o al invocarlo desde el espacio de usuario, en lugar de producir kernel panics. Pero las lecciones se acumulan: un error en `make_dev` con soporte de clones en este capítulo producirá mensajes de diagnóstico problemáticos en el capítulo 28 cuando el driver de red también quiera sus propios nodos, y una fuga de OID de sysctl en este capítulo producirá fallos confusos al cargar el módulo en el capítulo 27, cuando el driver de almacenamiento intente registrar nombres que ya existen.

### Cómo sacar el máximo partido a este capítulo

Cinco hábitos dan más resultado en este capítulo que en cualquiera de los capítulos anteriores de la Parte 5.

Primero, ten marcados `/usr/src/sys/dev/null/null.c`, `/usr/src/sys/sys/conf.h`, `/usr/src/sys/sys/ioccom.h` y `/usr/src/sys/sys/sysctl.h`. El primero es el driver de caracteres no trivial más corto del árbol de código fuente de FreeBSD y es el ejemplo canónico del patrón `cdevsw`/`make_dev`/`destroy_dev`. El segundo declara la estructura `cdevsw`, la familia `make_dev` y los bits de flag `MAKEDEV_*` que el capítulo usa repetidamente. El tercero define las macros de codificación de ioctl (`_IO`, `_IOR`, `_IOW`, `_IOWR`) y contiene las constantes `IOC_VOID`, `IOC_IN`, `IOC_OUT` e `IOC_INOUT` que el kernel usa para decidir si copiar datos automáticamente. El cuarto define las macros OID de sysctl, la convención de llamada `SYSCTL_HANDLER_ARGS` y las interfaces OID estáticas y dinámicas. Ninguno de estos archivos es largo; el más extenso tiene unos pocos miles de líneas y la mayor parte son comentarios. Leer cada uno una vez al comienzo de la sección correspondiente es lo más eficaz que puedes hacer para ganar fluidez.

Segundo, ten a mano tres ejemplos reales de drivers: `/usr/src/sys/dev/null/null.c`, `/usr/src/sys/net/if_tuntap.c` y `/usr/src/sys/dev/virtio/block/virtio_blk.c`. El primero es el ejemplo mínimo de cdevsw. El segundo es el ejemplo canónico de `dev_clone` con soporte de clones que usa la sección 5 para presentar ifnet. El tercero ilustra un árbol sysctl dinámico completo basado en `device_get_sysctl_ctx` y un callback `SYSCTL_ADD_PROC` que activa y desactiva un parámetro en tiempo de ejecución. El capítulo 24 remite a cada uno en el momento oportuno. Leerlos una vez ahora, sin intentar memorizarlos, proporciona al resto del capítulo anclajes concretos sobre los que apoyar sus ideas.

> **Una nota sobre los números de línea.** Las referencias a `null.c`, `if_tuntap.c` y `virtio_blk.c` más adelante en el capítulo están ancladas a símbolos con nombre: una llamada específica a `make_dev`, un handler `SYSCTL_ADD_PROC`, un `cdevsw` concreto. Esos nombres se mantienen en futuras versiones de FreeBSD 14.x. El número de línea concreto en el que se encuentra cada nombre, no. Cuando el texto cite una ubicación, abre el archivo y busca el símbolo en lugar de desplazarte hasta el número.

Tercero, escribe a mano cada cambio de código en el driver `myfirst`. El código de integración es el tipo de código que es fácil de copiar y muy difícil de recordar después. Escribir a mano la tabla `cdevsw`, las definiciones de comandos ioctl, la construcción del árbol sysctl y el programa `myfirstctl` del espacio de usuario construye el tipo de familiaridad que el copiar y pegar no puede dar. El objetivo no es tener el código; el objetivo es ser la persona que podría volver a escribirlo desde cero en veinte minutos cuando un bug futuro lo exija.

Cuarto, compila el programa del espacio de usuario en cada etapa. Muchas de las lecciones del capítulo son visibles solo en el lado del usuario. Si el kernel ha copiado correctamente el payload de un ioctl, si un sysctl es legible pero no escribible, si un nodo `/dev` tiene los permisos que estableciste, si un clon produce un nodo de dispositivo utilizable: todas esas preguntas se responden con `cat`, `dd`, `chmod`, `sysctl`, `truss` y el pequeño programa auxiliar `myfirstctl`. Un driver probado solo por sus contadores en el lado del kernel solo está probado a medias.

Quinto, después de terminar la sección 4, vuelve a leer la disciplina de depuración del capítulo 23. Cada superficie de integración del capítulo 24 está envuelta en un `DPRINTF` del capítulo 23. Cada ruta ioctl dispara líneas de log `MYF_DBG_IOCTL`. Cada ruta sysctl puede observarse a través de la maquinaria SDT. Ver cómo las herramientas del capítulo 23 sirven a las interfaces del capítulo 24 refuerza ambos capítulos y prepara al lector para el capítulo 25, donde el mismo patrón continúa.

### Mapa de ruta del capítulo

Las secciones, en orden, son:

1. **Por qué importa la integración.** La historia conceptual. Del módulo independiente al componente del sistema; el coste de dejar la integración como una ocurrencia tardía; las cuatro interfaces visibles para el usuario que todo driver integrado acaba necesitando; los hooks de subsistema opcionales que el capítulo introduce pero no termina.
2. **Cómo trabajar con devfs y el árbol de dispositivos.** La visión del kernel sobre `/dev`. Qué es devfs y en qué se diferencia de las tablas de dispositivos estáticas de los sistemas más antiguos; el ciclo de vida de un `cdev`; `make_dev` y funciones relacionadas en detalle; la estructura `cdevsw` y sus callbacks; los campos `si_drv1`/`si_drv2`; permisos y propiedad; nodos clonables mediante el manejador de eventos `dev_clone`. La etapa 1 del driver del capítulo 24 sustituye la creación ad-hoc original del nodo por un patrón limpio con soporte de clonado.
3. **Implementación del soporte para `ioctl()`.** La interfaz de control dirigida por el usuario. Qué son los ioctls; la codificación `_IO`/`_IOR`/`_IOW`/`_IOWR`; la capa automática de copyin/copyout del kernel; cómo elegir una letra y un número mágicos; cómo estructurar una cabecera ioctl pública; cómo escribir un callback `d_ioctl` que distribuya según la palabra de comando; errores comunes (datos de longitud variable, punteros embebidos, evolución de versiones). La etapa 2 añade `myfirst_ioctl.c` y `myfirst_ioctl.h` y un pequeño programa de espacio de usuario `myfirstctl`.
4. **Exposición de métricas a través de `sysctl()`.** La interfaz de monitorización y ajuste. Qué son los sysctls; OIDs estáticos frente a dinámicos; el patrón `device_get_sysctl_ctx`/`device_get_sysctl_tree`; contadores, parámetros ajustables y callbacks procedurales; la familia `SYSCTL_ADD_*`; parámetros configurables en `/boot/loader.conf`; control de acceso y unidades. La etapa 3 añade `myfirst_sysctl.c` y un subárbol de métricas por instancia.
5. **Integración con el subsistema de red (opcional).** Una visión breve y conceptual de ifnet. Qué es `if_t`; el esquema `if_alloc`/`if_initname`/`if_attach`/`bpfattach`/`if_detach`/`if_free`; cómo se estructuran `tun(4)` y `tap(4)` en torno a él; qué papel desempeña el driver dentro del stack de red más amplio. La sección es breve a propósito; el capítulo 28 es el capítulo dedicado a los drivers de red.
6. **Integración con el subsistema de almacenamiento CAM (opcional).** Una visión breve y conceptual de CAM. Qué es CAM; qué son un SIM y un CCB; el esquema `cam_sim_alloc`/`xpt_bus_register`/`xpt_action`/`xpt_done`; cómo se podría exponer un pequeño disco de memoria de solo lectura a través de él. La sección es breve a propósito; el capítulo 27 es el capítulo dedicado a los drivers de almacenamiento.
7. **Registro, desmontaje y disciplina de limpieza.** El tema transversal. Manejadores de eventos de módulo (`MOD_LOAD`, `MOD_UNLOAD`, `MOD_SHUTDOWN`); fallo en attach con limpieza parcial; fallo en detach cuando los usuarios mantienen manejadores abiertos; el patrón de limpieza ante fallos; ordenación entre las superficies de integración; qué hacen por ti `bus_generic_attach`, `bus_generic_detach` y `device_delete_children`; SYSINIT y EVENTHANDLER para registros transversales.
8. **Refactorización y versionado de un driver integrado.** El orden final. La división final en `myfirst.c`, `myfirst_debug.c`/`.h`, `myfirst_ioctl.c`/`.h` y `myfirst_sysctl.c`/`.h`; la cabecera pública `myfirst.h` para los llamadores de espacio de usuario; el documento `INTEGRATION.md`; el incremento de versión a `1.7-integration`; las adiciones a las pruebas de regresión; el commit y el tag.

Tras las ocho secciones llegan una serie de laboratorios prácticos que ejercitan cada superficie de integración de extremo a extremo, una serie de ejercicios de desafío que amplían los conocimientos del lector sin introducir nuevas bases, una referencia de resolución de problemas para los síntomas que la mayoría de los lectores encontrarán, un cierre que concluye la historia del capítulo 24 y abre la del capítulo 25, un puente hacia el siguiente capítulo y la habitual tarjeta de referencia rápida con el glosario.

Si es tu primera lectura, sigue el texto de forma lineal y haz los laboratorios en orden. Si estás revisando el material, las secciones 2 y 3 son independientes y se pueden leer de una sola sentada. Las secciones 5 y 6 son breves y conceptuales: pueden saltarse en una primera lectura sin perder el hilo principal del capítulo, y retomarse antes de comenzar el capítulo 26 o el capítulo 27.

Una pequeña nota antes de que comience el trabajo técnico. El capítulo pide con frecuencia al lector que compile un pequeño programa de espacio de usuario, lo ejecute contra el driver, observe el resultado y vuelva después al lado del kernel para leer qué ha ocurrido. Ese ritmo es deliberado. La integración no es una propiedad exclusiva del driver; es una propiedad de la relación entre el driver y el resto del sistema. Los programas de espacio de usuario son breves, pero son la forma en que el capítulo comprueba si cada superficie de integración realmente funciona.

## Sección 1: Por qué importa la integración

Antes del código, el contexto. La sección 1 explica qué cambia cuando un driver se integra en el sistema. El lector que ha seguido la Parte 4 y los primeros capítulos de la Parte 5 ya tiene un driver funcional. ¿Qué significa decir que ese driver todavía no está *integrado* y qué cualidades específicas aporta la integración?

Esta sección responde a esa pregunta con detalle y detenimiento, porque el resto del capítulo es la implementación de esas cualidades. Un lector que entienda con claridad *por qué* existe cada superficie de integración encontrará el trabajo de implementación de las secciones 2 a 8 mucho más sencillo. Un lector que se salte el contexto pasará el capítulo preguntándose por qué el driver necesita un `ioctl` si un sysctl interno podría hacer lo mismo; esa pregunta tiene una respuesta real, y la sección 1 es donde vive esa respuesta.

### De módulo independiente a componente del sistema

Un driver independiente es un módulo del kernel que hace su trabajo correctamente cuando el kernel lo llama y no interfiere en nada más. El driver `myfirst` actual, en la versión `1.6-debug`, es exactamente ese tipo de módulo. Tiene un único nodo `cdev`, sin ioctls públicos, sin sysctls publicados más allá de unos pocos controles internos de depuración, sin relación con ningún subsistema del kernel fuera de su pequeño rincón, y sin ofrecer ninguna vía de control a los programas del espacio de usuario. Funciona, y funciona de forma aislada.

Un componente del sistema, en cambio, es un módulo del kernel cuyo valor depende de sus relaciones con el resto del sistema. El mismo driver `myfirst`, integrado, presenta un nodo `/dev/myfirst0` con los permisos adecuados al papel de su hardware, expone una pequeña interfaz ioctl que los programas de usuario pueden emplear para reiniciar el dispositivo o consultar su estado, publica un árbol sysctl por instancia que el software de monitorización puede consultar, se registra en el subsistema del kernel apropiado si el hardware es un dispositivo de red o de almacenamiento, y libera todos sus recursos de forma limpia al descargarse. Cada una de esas interfaces es pequeña. En conjunto son la diferencia entre un driver que funciona en la máquina de laboratorio de un desarrollador y un driver que se distribuye con FreeBSD.

El paso de independiente a integrado no es un cambio de una sola línea en el driver. Es una serie de decisiones conscientes, cada una de las cuales amplía la superficie pública del driver y cada una de las cuales conlleva un coste de mantenimiento. Decisiones que en el momento parecen pequeñas, como la elección de la letra mágica para un ioctl o el nombre de un OID de sysctl, se convierten en contratos de larga duración. Un driver que eligió la letra `M` para sus ioctls en 2010 todavía tiene esos números en su cabecera pública hoy, porque cambiarlos rompería cada programa del espacio de usuario que alguna vez los haya llamado.

Una forma útil de visualizar el cambio es imaginar dos lectores que se acercan al driver. El primer lector es el desarrollador que escribió el driver: lo conoce todo sobre él, puede cambiar cualquier cosa en él y puede reconstruirlo cuando quiera. El segundo lector es el administrador del sistema que instala el módulo desde un paquete y nunca lee su código fuente: solo ve el driver a través de sus nodos `/dev`, sus sysctls, sus ioctls y sus mensajes de registro. Un driver independiente es uno diseñado para el primer lector; un driver integrado es uno diseñado para ambos.

El capítulo 24 enseña al lector cómo diseñar pensando en el segundo lector. Ese es el trabajo conceptual que realiza el capítulo, y el trabajo de código en las secciones 2 a 8 es su realización práctica.

### Objetivos habituales de integración

Un driver de FreeBSD se integra habitualmente con cuatro superficies del lado del kernel y, según el hardware, con uno de dos subsistemas. Nombrar los objetivos con claridad desde el principio ayuda al lector a mantener la estructura del capítulo en mente.

El primer objetivo es **`devfs`**. Cada driver de caracteres crea uno o más nodos `/dev` mediante `make_dev` (o alguna de sus variantes) y los elimina mediante `destroy_dev`. La forma y el nombre de esos nodos es como el resto del sistema se dirige al driver. Un nodo llamado `/dev/myfirst0` permite al administrador abrirlo con `cat /dev/myfirst0`, permite a un script incluirlo en un `find /dev -name 'myfirst*'`, y permite al propio kernel despachar las llamadas de apertura, lectura, escritura e ioctl hacia el driver. La sección 2 está dedicada a devfs.

El segundo objetivo es **`ioctl(2)`**. Las syscalls `read(2)` y `write(2)` mueven bytes; no controlan el driver. Cualquier operación de control que no encaje en el modelo de flujo de datos de lectura/escritura vive en `ioctl(2)`. Un programa del espacio de usuario llama a `ioctl(fd, MYF_RESET)` para pedir al driver que reinicie su hardware, o a `ioctl(fd, MYF_GET_STATS, &stats)` para leer una instantánea de contadores. Cada ioctl es un punto de entrada pequeño y bien tipado con un número, una dirección y un payload. La sección 3 está dedicada a los ioctls.

El tercer objetivo es **`sysctl(8)`**. Los contadores, estadísticas y parámetros ajustables viven en el árbol sysctl de todo el sistema, accesible desde el espacio de usuario con `sysctl(8)` y desde C con `sysctlbyname(3)`. Un driver coloca sus OIDs bajo `dev.<driver>.<unit>.<name>` de modo que `sysctl dev.myfirst.0` lista cada métrica y cada control que el dispositivo expone. La interfaz sysctl es el lugar adecuado para los contadores de solo lectura y para los controles que cambian poco; ioctl es el lugar adecuado para las acciones rápidas y para los datos tipados. La sección 4 está dedicada a los sysctls.

El cuarto objetivo son **los ganchos del ciclo de vida del kernel**. El manejador de eventos del módulo (`MOD_LOAD`, `MOD_UNLOAD`, `MOD_SHUTDOWN`), los métodos attach y detach del árbol de dispositivos, y los registros transversales mediante `EVENTHANDLER(9)` y `SYSINIT(9)` definen en conjunto qué ocurre cuando el driver entra y sale del kernel. Un driver que acierta en las interfaces de integración pero falla en el ciclo de vida provoca fugas de recursos, deadlocks al descargarse, o panics en el tercer ciclo de `kldload`/`kldunload`. La sección 7 está dedicada a la disciplina del ciclo de vida.

Además, los drivers cuyo hardware es un dispositivo de red se integran con el subsistema **`ifnet`**, y los drivers cuyo hardware es un dispositivo de almacenamiento se integran con el subsistema **`CAM`**. Ambos son conceptualmente lo suficientemente amplios como para que su tratamiento en profundidad requiera un capítulo propio (el capítulo 28 para ifnet, el capítulo 27 para CAM y GEOM). Las secciones 5 y 6 de este capítulo los presentan al nivel necesario para reconocer su forma y saber qué tipo de trabajo implican.

Cada objetivo merece ser nombrado por una razón concreta. `devfs` es el nombre que un usuario cualquiera escribe en una shell. `ioctl` es el punto de entrada para las acciones tipadas que un programa necesita emitir sobre un dispositivo. `sysctl` es el lugar donde una herramienta de monitorización busca números. Los ganchos del ciclo de vida son el punto al que llega un empaquetador y un administrador del sistema cuando ejecutan `kldload` y `kldunload`. Los cuatro en conjunto cubren las cuatro caras del driver que importan a cualquiera que no sea su autor.

### Ventajas de una integración correcta

La integración no es un fin en sí misma. Es un medio para un pequeño conjunto de resultados prácticos a los que el capítulo volverá repetidamente.

El primer resultado es la **monitorización**. Un driver cuyos contadores son visibles a través de `sysctl` puede ser consultado por `prometheus-fbsd-exporter`, por comprobaciones de Nagios, por un pequeño script de shell que se ejecuta cada minuto. Un driver cuyos contadores solo son visibles mediante `device_printf` en `dmesg` solo puede inspeccionarse leyendo el archivo de registro a mano. Las dos realidades operativas son muy distintas, y la diferencia la determina completamente la decisión de integración que tomó el desarrollador cuando escribió el driver.

El segundo resultado es la **gestión**. Un driver que expone un ioctl llamado `MYF_RESET` permite al administrador incluir un ciclo de reinicio en una ventana de mantenimiento mediante un script. Un driver sin esa interfaz obliga al administrador a ejecutar `kldunload` y `kldload` sobre el módulo, que es una operación mucho más pesada que cierra todos los descriptores de archivo abiertos y puede no ser aceptable mientras fluye tráfico de producción a través del dispositivo.

El tercer resultado es la **automatización**. Un driver que emite nodos `/dev` bien formados con nombres predecibles permite a `devd(8)` reaccionar ante eventos de attach y detach, ejecutar scripts en hotplug, e integrar el driver en los flujos de arranque, apagado y recuperación del sistema más amplio. Un driver que emite un único nodo opaco y nunca informa a nadie de su ciclo de vida no puede automatizarse sin recurrir a la lectura de registros de `dmesg`, lo cual es frágil.

El cuarto resultado es la **reutilización**. Un driver cuya interfaz ioctl está bien documentada puede ser la base de bibliotecas de mayor nivel. El demonio `bsnmp`, por ejemplo, utiliza interfaces del kernel bien definidas para exponer los contadores del driver a través de SNMP sin tocar el código fuente del driver. Un driver que diseñó sus interfaces correctamente desde el principio obtiene esas ventajas sin trabajo adicional.

Estos cuatro resultados (monitorización, gestión, automatización, reutilización) son la razón práctica de todo lo que sigue. Cada sección del capítulo aporta una pieza de uno de estos resultados, y la refactorización final de la sección 8 es lo que hace que el conjunto sea presentable para el resto del sistema.

### Un breve recorrido por las herramientas del sistema que dependen de la integración

Un ejercicio útil antes del trabajo técnico es observar las herramientas del espacio de usuario que existen precisamente porque los drivers se integran con los subsistemas del kernel mencionados. Ninguna de estas herramientas funciona con un driver independiente. Todas ellas funcionan, automáticamente, con uno integrado.

`devinfo(8)` recorre el árbol de dispositivos del kernel e imprime lo que encuentra. Funciona porque cada dispositivo del árbol fue registrado a través de la interfaz newbus y cada dispositivo tiene un nombre, un número de unidad y un padre. El administrador ejecuta `devinfo -v` y ve toda la jerarquía de dispositivos, incluyendo la instancia `myfirst0`.

`sysctl(8)` lee y escribe en el árbol sysctl del kernel. Funciona porque cada contador y cada control del kernel es accesible a través de la jerarquía de OIDs, incluidos los OIDs que el driver registró mediante `device_get_sysctl_tree`.

`devctl(8)` permite al administrador manipular dispositivos individuales: `devctl detach`, `devctl attach`, `devctl suspend`, `devctl resume`, `devctl rescan`. Funciona porque cada dispositivo implementa los métodos kobj que espera la maquinaria del árbol de dispositivos del kernel. El capítulo 22 ya utilizó `devctl suspend myfirst0` y `devctl resume myfirst0`.

`devd(8)` observa el canal de eventos de dispositivo del kernel y ejecuta scripts en respuesta a eventos de attach, detach, hotplug y similares. Funciona porque el kernel emite eventos estructurados para cada operación de newbus. Un driver que sigue el patrón estándar de newbus es automáticamente visible para `devd`.

`ifconfig(8)` configura interfaces de red. Funciona porque cada driver de red se registra en el subsistema ifnet y acepta un conjunto estándar de ioctls (la sección 5 lo presenta).

`camcontrol(8)` controla dispositivos SCSI y SATA a través de CAM. Funciona porque cada driver de almacenamiento registra un SIM y procesa CCBs (la sección 6 lo presenta).

`gstat(8)` muestra estadísticas de GEOM en tiempo real, `geom(8)` lista el árbol de GEOM, `top -H` muestra el uso de CPU por thread. Cada una de estas herramientas depende de una superficie de integración específica con la que los drivers se registran. El driver que ignora esas superficies no obtiene ninguno de los beneficios.

Una forma sencilla de confirmar este punto es ejecutar, en tu máquina de laboratorio, los siguientes ejercicios:

```sh
# Tour the device tree
devinfo -v | head -40

# Tour the sysctl tree, just the dev branch
sysctl dev | head -40

# See what the live network interfaces look like
ifconfig -a

# See what storage looks like through CAM
camcontrol devlist

# See what GEOM sees
geom -t

# Watch the device event channel for a few seconds
sudo devd -d -f /dev/null &
DEVDPID=$!
sleep 5
kill $DEVDPID
```

Cada comando existe porque los drivers se integran. Lee la salida y observa cuánto de lo que es visible proviene de drivers que realizaron el trabajo de integración. El driver `myfirst`, al inicio del capítulo 24, no aporta casi nada a esa salida. Al final del capítulo 24, aportará su subárbol `dev.myfirst.0` a `sysctl`, su dispositivo `myfirst0` a `devinfo -v`, y sus nodos `/dev/myfirst*` al sistema de archivos. Cada paso es pequeño. El conjunto es la diferencia entre un driver de laboratorio de un solo uso y una pieza real de FreeBSD.

### Qué significa «opcional» en este capítulo

Las secciones 5 (redes) y 6 (almacenamiento) están etiquetadas como opcionales. La etiqueta merece una definición cuidadosa.

Opcional no significa poco importante. Ambas secciones se convertirán en lectura esencial más adelante en el libro: el material sobre redes antes del Capítulo 27 y el material sobre almacenamiento antes del Capítulo 26 y el Capítulo 28. Opcional significa que para un lector que sigue el driver PCI `myfirst` como ejemplo recurrente, los hooks de red y de almacenamiento no se ejercitarán en este capítulo, ya que `myfirst` no es un dispositivo de red ni un dispositivo de almacenamiento. El capítulo introduce la forma conceptual de esos hooks para que el lector los reconozca cuando aparezcan, y para que las decisiones estructurales de la Sección 7 y la Sección 8 las tengan en cuenta.

Un lector que realice una primera lectura y tenga poco tiempo puede saltarse las Secciones 5 y 6. Las demás secciones no dependen de ellas. Un lector que planee seguir el Capítulo 26 o el Capítulo 27 debería leerlas, porque introducen el vocabulario que esos capítulos darán por sentado.

El capítulo es honesto sobre la profundidad de lo que enseña en esas secciones. El subsistema de red cuenta con varios miles de líneas de código fuente en `/usr/src/sys/net`. El subsistema CAM cuenta con varios miles de líneas de código fuente en `/usr/src/sys/cam`. Cada uno tardó años en diseñarse y sigue evolucionando. El Capítulo 24 los introduce al nivel de *esta es la forma*, *estas son las llamadas que un driver realiza habitualmente*, *este es un driver real que usa cada uno*. La mecánica completa corresponde a otros capítulos.

### Escollos en el camino hacia la integración

Tres escollos acechan a la mayoría de quienes integran un driver por primera vez.

El primero es **añadir superficies de integración de forma ad hoc**. Un driver que incorporó un ioctl un martes porque el desarrollador necesitaba una forma rápida de probar el dispositivo, luego un sysctl a la semana siguiente porque quería ver un contador, y otro ioctl al mes siguiente a raíz de un bug reportado, acaba con una superficie pública inconsistente en estilo, en nomenclatura, en el manejo de errores y en la documentación. El patrón correcto es diseñar la superficie pública de manera deliberada, utilizar una nomenclatura coherente y documentar cada punto de entrada en un archivo de cabecera antes de escribir la implementación. La sección 3 y la sección 8 retoman esta disciplina.

El segundo escollo es **mezclar responsabilidades dentro de los métodos del dispositivo**. Un driver cuyo `device_attach` realiza el trabajo de kobj, la asignación de recursos, la creación del nodo devfs, la construcción del árbol sysctl y la configuración orientada al espacio de usuario, todo en una única función larga, se vuelve ilegible rápidamente. El capítulo recomienda separar estas responsabilidades en funciones auxiliares en un primer momento, y en archivos fuente independientes en la sección 8. El par `myfirst_debug.c` y `myfirst_debug.h` del capítulo 23 fue el primer paso en esa dirección; los nuevos archivos `myfirst_ioctl.c` y `myfirst_sysctl.c` de este capítulo continúan el patrón.

El tercer escollo es **no probar la superficie pública desde el espacio de usuario**. Un driver que el desarrollador haya probado únicamente desde el lado del kernel superará todas las pruebas de ese lado y aún así fallará en el momento en que un programa real del espacio de usuario lo invoque, porque el desarrollador asumió algo sobre la convención de llamada que en la práctica no se cumple. Por eso el capítulo insiste en construir el pequeño programa auxiliar `myfirstctl` en cuanto el driver tenga algún ioctl, y en comprobar cada sysctl a través de `sysctl(8)` en lugar de leer el contador interno directamente. Las pruebas desde el espacio de usuario son las únicas que confirman que la integración funciona de verdad.

Estos escollos no son exclusivos de FreeBSD. Aparecen en cualquier sistema operativo que tenga una frontera entre el kernel y el usuario. Las herramientas de FreeBSD facilitan más que las de la mayoría de los sistemas hacer las cosas bien, porque las convenciones están bien documentadas en las páginas de manual (`devfs(5)`, `ioctl(2)`, `sysctl(9)`, `style(9)`) y porque el propio kernel incluye cientos de drivers integrados que el lector puede estudiar. El capítulo se apoya en esas convenciones y remite a los drivers reales correspondientes a lo largo del texto.

### Un modelo mental para el capítulo

Antes de pasar a la sección 2, conviene fijar una sola imagen en la mente. Al final del capítulo 24, el driver tendrá este aspecto visto desde fuera:

```text
Userland tools                          Kernel
+----------------------+                +----------------------+
| myfirstctl           |  ioctl(2)      | d_ioctl callback     |
| sysctl(8)            +--------------->| sysctl OID tree      |
| cat /dev/myfirst0    |  read/write    | d_read, d_write      |
| chmod, chown         |  fileops       | devfs node lifecycle |
| devinfo -v           |  newbus query  | device_t myfirst0    |
| dtrace -n 'myfirst:::'|  SDT probes   | sc_debug, DPRINTF    |
+----------------------+                +----------------------+
```

Cada entrada de la izquierda es una herramienta que cualquier usuario de FreeBSD ya conoce. Cada entrada de la derecha es un fragmento de integración que el capítulo te enseña a escribir. Las flechas del medio son lo que cada sección del capítulo implementa.

Mantén esa imagen presente mientras comienza el trabajo técnico. El objetivo de cada sección que sigue es añadir una de esas flechas al driver, con la disciplina que permite que la flecha siga siendo fiable a medida que el driver crece.

### Cerrando la sección 1

La integración es la disciplina de hacer que un driver sea visible, controlable y observable desde fuera de su propio código fuente. Las cuatro superficies de integración principales en FreeBSD son devfs, ioctl, sysctl y los hooks del ciclo de vida del kernel. Dos hooks opcionales de subsistema son ifnet para dispositivos de red y CAM para dispositivos de almacenamiento. En conjunto, así es como un driver deja de ser un proyecto de un solo desarrollador y se convierte en una pieza de FreeBSD. Las secciones restantes de este capítulo implementan cada superficie, con el driver `myfirst` como ejemplo conductor y el salto de versión de `1.6-debug` a `1.7-integration` como hito visible.

En la sección siguiente, nos dirigimos a la primera y más fundamental superficie de integración: `devfs` y el sistema de archivos de dispositivos que otorga a cada driver de caracteres su presencia en `/dev`.

## Sección 2: Trabajo con devfs y el árbol de dispositivos

La primera superficie de integración que cruza todo driver de caracteres es `devfs`, el sistema de archivos de dispositivos. El lector lleva creando `/dev/myfirst0` desde los primeros capítulos, pero la llamada a `make_dev` siempre se presentó como una única línea de código repetitivo sin mayor explicación. La sección 2 cubre ese hueco. Explica qué es realmente devfs, recorre el ciclo de vida de un `cdev`, repasa en detalle las variantes de `make_dev` y la tabla de callbacks de `cdevsw`, muestra cómo asociar estado por nodo a través de `si_drv1` y `si_drv2`, enseña el patrón moderno preparado para clones en drivers que desean un nodo por instancia bajo demanda, y termina mostrando cómo un administrador puede ajustar permisos y propietario sin recompilar el driver.

### Qué es devfs

`devfs` es un sistema de archivos virtual que expone el conjunto de dispositivos de caracteres registrados en el kernel como un árbol de archivos bajo `/dev`. Es virtual en el mismo sentido en que lo son `procfs` y `tmpfs`: no hay almacenamiento en disco que lo respalde. Cada archivo bajo `/dev` es la proyección que el kernel hace de una estructura `cdev` en el espacio de nombres del sistema de archivos. Cuando un programa del espacio de usuario llama a `open("/dev/null", O_RDWR)`, el kernel busca la ruta en devfs, encuentra el `cdev` correspondiente, sigue el puntero a la tabla `cdevsw` y despacha la apertura a través de ella.

Los sistemas UNIX más antiguos utilizaban un árbol de dispositivos estático. Un administrador ejecutaba un programa como `MAKEDEV` o editaba `/dev` directamente, y el sistema de archivos contenía nodos de dispositivo independientemente de si el hardware correspondiente estaba presente o no. El enfoque estático tenía dos problemas bien conocidos. En primer lugar, el administrador tenía que saber de antemano qué dispositivos eran posibles y crear los nodos correspondientes a mano, con los números mayor y menor correctos. En segundo lugar, el sistema de archivos contenía nodos huérfanos para hardware que el sistema no tenía realmente, lo cual resultaba confuso.

`devfs` de FreeBSD, introducido como opción predeterminada en las primeras versiones de la serie 5.x, reemplazó el esquema estático por uno dinámico. El propio kernel decide qué nodos de dispositivo existen, en función de qué drivers han llamado a `make_dev`. Cuando el driver llama a `make_dev`, el nodo aparece bajo `/dev`. Cuando llama a `destroy_dev`, el nodo desaparece. El administrador ya no tiene que mantener las entradas de dispositivos a mano, y no existen nodos huérfanos para hardware no presente.

La contrapartida que introduce devfs es que el ciclo de vida de un nodo `/dev` ahora lo controla el kernel en lugar del sistema de archivos. Un driver que no elimina su nodo al hacer el detach lo deja visible hasta que el propio kernel lo elimine (lo que ocurrirá eventualmente, pero no tan inmediatamente como si lo hiciera el driver). Un driver que crea accidentalmente el mismo nodo dos veces provoca un pánico por la comprobación de nombres duplicados. Un driver que crea un nodo desde el contexto equivocado puede producir un deadlock en el kernel. El capítulo enseña los patrones que evitan cada uno de estos problemas.

Un detalle útil para entender devfs es que el kernel mantiene un único espacio de nombres global de nodos de dispositivo, y `make_dev` registra en ese espacio de nombres. El administrador puede montar instancias adicionales de devfs dentro de jails o chroots; cada instancia proyecta una vista filtrada del espacio de nombres global, controlada a través de `devfs.rules(8)`. El propio driver no necesita conocer estas proyecciones. Simplemente registra su `cdev` una vez, y el kernel junto con el sistema de reglas deciden qué vistas pueden verlo.

### El ciclo de vida de un `cdev`

Todo `cdev` pasa por cinco fases. Conocer estas fases por su nombre facilita el seguimiento del resto de la sección.

La primera fase es el **registro**. El driver llama a `make_dev` (o a una de sus variantes) desde `device_attach` (o desde un manejador de eventos del módulo, dependiendo de si el dispositivo está conectado a un bus o es un pseudodispositivo). La llamada devuelve un `struct cdev *` que el driver almacena en su softc. A partir de este momento, el nodo es visible bajo `/dev`.

La segunda fase es el **uso**. Los programas del espacio de usuario pueden abrir el nodo y llamar a read, write, ioctl, mmap, poll o kqueue sobre él. Cada llamada pasa por el callback de cdevsw correspondiente que el driver instaló en el momento del registro. Un `cdev` puede tener muchos file handles abiertos en cualquier momento, y los callbacks del driver deben ser seguros para invocarse concurrentemente entre sí y con el trabajo interno del propio driver (interrupciones, callouts, taskqueues).

La tercera fase es la **solicitud de destrucción**. El driver llama a `destroy_dev` (o a `destroy_dev_sched` para la variante asíncrona). El nodo se desvincula de `/dev` inmediatamente, de modo que ninguna nueva apertura puede tener éxito. Las aperturas existentes no se cierran en este momento.

La cuarta fase es el **vaciado**. `destroy_dev` bloquea hasta que todos los file handles abiertos del cdev han pasado por `d_close` y todas las llamadas en curso al driver han retornado. Una vez que `destroy_dev` retorna, se garantiza que los callbacks del driver no serán invocados más.

La quinta fase es la **liberación**. Una vez que `destroy_dev` retorna, el driver puede liberar el softc, liberar los recursos que utilizaban los callbacks del cdev y descargar el módulo. El propio `struct cdev *` es propiedad del kernel y es liberado por el kernel cuando desaparece su última referencia; el driver no lo libera.

La fase de vaciado es la que más problemas da a los drivers por primera vez. Un driver ingenuo hace el equivalente de `destroy_dev; free(sc);` y luego un file handle abierto llama al cdevsw y desreferencia el softc ya liberado, lo que provoca un pánico. El capítulo enseña cómo manejar esto correctamente: coloca la llamada a destroy_dev antes de cualquier liberación de estado en la ruta de detach, y confía en que el kernel vaciará las llamadas en curso antes de que destroy retorne.

### La familia `make_dev`

FreeBSD ofrece varias variantes de `make_dev`, cada una con una combinación diferente de opciones. Se encuentran en `/usr/src/sys/sys/conf.h` y `/usr/src/sys/fs/devfs/devfs_devs.c`. El capítulo presenta las cuatro más útiles.

La forma más sencilla es **`make_dev`** en sí misma:

```c
struct cdev *
make_dev(struct cdevsw *devsw, int unit, uid_t uid, gid_t gid,
    int perms, const char *fmt, ...);
```

Esta llamada crea un nodo cuyo propietario es `uid:gid` con permisos `perms`, nombrado según el formato estilo `printf`. Usa `M_WAITOK` internamente y puede dormirse, por lo que debe llamarse desde un contexto que pueda dormir (típicamente `device_attach` o un manejador de carga de módulo, nunca un manejador de interrupciones). No puede fallar: si no puede asignar memoria, se duerme hasta que puede. El lector lleva usando esta forma desde los primeros capítulos.

La forma más completa es **`make_dev_credf`**:

```c
struct cdev *
make_dev_credf(int flags, struct cdevsw *devsw, int unit,
    struct ucred *cr, uid_t uid, gid_t gid, int mode,
    const char *fmt, ...);
```

Esta variante acepta un argumento `flags` explícito y una credencial explícita. El framework MAC utiliza la credencial para comprobar si el dispositivo puede crearse con el propietario indicado. Los flags seleccionan características como `MAKEDEV_ETERNAL` (el kernel nunca destruye este nodo automáticamente) y `MAKEDEV_ETERNAL_KLD` (lo mismo, pero permitido únicamente dentro de un módulo cargable). El driver `null(4)` usa esta forma, tal como el capítulo citó en la lista de referencia de la sección 1.

La forma recomendada para nuevos drivers es **`make_dev_s`**:

```c
int
make_dev_s(struct make_dev_args *args, struct cdev **cdev,
    const char *fmt, ...);
```

Esta variante recibe una estructura de argumentos en lugar de una larga lista de argumentos. La estructura se inicializa con `make_dev_args_init(&args)` antes de rellenar sus campos. La ventaja de `make_dev_s` es que puede fallar en lugar de dormirse, y el fallo se notifica mediante un valor de retorno en lugar de a través de un sleep. También tiene un parámetro de salida para el `cdev *`, lo que significa que el llamador no necesita recordar qué posición de retorno representa qué. El código nuevo debería preferir `make_dev_s` porque la ruta de error es más limpia.

La estructura de argumentos tiene esta forma:

```c
struct make_dev_args {
    size_t        mda_size;
    int           mda_flags;
    struct cdevsw *mda_devsw;
    struct ucred  *mda_cr;
    uid_t         mda_uid;
    gid_t         mda_gid;
    int           mda_mode;
    int           mda_unit;
    void          *mda_si_drv1;
    void          *mda_si_drv2;
};
```

El campo `mda_size` lo usa el kernel para detectar incompatibilidades de ABI; `make_dev_args_init` lo establece correctamente. Los campos `mda_si_drv1` y `mda_si_drv2` permiten al driver asociar dos punteros propios al cdev en el momento de su creación; el capítulo utiliza `mda_si_drv1` para asociar un puntero al softc.

Los bits de flag de `MAKEDEV_*` relevantes para la mayoría de los drivers son:

| Flag                    | Significado                                                                          |
|-------------------------|--------------------------------------------------------------------------------------|
| `MAKEDEV_REF`           | El `cdev` devuelto queda referenciado; equilibra la referencia con `dev_rel`.        |
| `MAKEDEV_NOWAIT`        | No duerme; devuelve error si la llamada tuviera que dormir.                          |
| `MAKEDEV_WAITOK`        | La llamada puede dormir (valor por defecto para `make_dev`).                         |
| `MAKEDEV_ETERNAL`       | El kernel no destruye este nodo automáticamente.                                     |
| `MAKEDEV_ETERNAL_KLD`   | Igual que ETERNAL pero permitido dentro de un módulo cargable.                       |
| `MAKEDEV_CHECKNAME`     | Valida el nombre contra el conjunto de caracteres de devfs.                          |

La variante `make_dev_p` es similar a `make_dev_s` pero recibe una lista de argumentos posicionales. Es más antigua, sigue siendo compatible y la utilizan algunos drivers del árbol; los drivers nuevos pueden ignorarla en favor de `make_dev_s`.

### La estructura `cdevsw`

La tabla `cdevsw` es la tabla de despacho para los callbacks de dispositivos de caracteres. Ya has instalado una en capítulos anteriores, pero la Sección 2 la examina campo por campo.

Un `cdevsw` moderno y mínimo tiene este aspecto:

```c
static struct cdevsw myfirst_cdevsw = {
    .d_version = D_VERSION,
    .d_flags   = D_TRACKCLOSE,
    .d_name    = "myfirst",
    .d_open    = myfirst_open,
    .d_close   = myfirst_close,
    .d_read    = myfirst_read,
    .d_write   = myfirst_write,
    .d_ioctl   = myfirst_ioctl,
};
```

`d_version` debe establecerse en `D_VERSION`. El kernel usa este campo para detectar drivers construidos contra un layout de `cdevsw` más antiguo. Un `d_version` ausente o incorrecto es una fuente habitual de fallos de carga de módulo difíciles de diagnosticar; establécelo siempre de forma explícita.

`d_flags` controla un pequeño conjunto de comportamientos opcionales. Los flags más comunes son:

| Flag             | Significado                                                                |
|------------------|------------------------------------------------------------------------|
| `D_TRACKCLOSE`   | Llama a `d_close` en el último cierre de cada fd, no en cada cierre.        |
| `D_NEEDGIANT`    | Adquiere el lock Giant global del kernel en torno al despacho (poco frecuente en código moderno).  |
| `D_NEEDMINOR`    | Asigna un número de minor (heredado; raramente necesario hoy en día).                  |
| `D_MMAP_ANON`    | El driver admite `mmap` anónimo a través de `dev_pager`.               |
| `D_DISK`         | El cdev es el punto de entrada para un dispositivo similar a un disco.                     |
| `D_TTY`          | El cdev es un dispositivo terminal; afecta al enrutamiento de la disciplina de línea.         |

Para el driver `myfirst`, `D_TRACKCLOSE` es el único flag que merece la pena establecer. Hace que el kernel llame a `d_close` exactamente una vez por descriptor de archivo, en el último cierre de ese descriptor, en lugar de en cada cierre. Sin `D_TRACKCLOSE`, un driver que quiera contar los file handles abiertos tiene que gestionar que `d_close` se llame múltiples veces, lo cual resulta incómodo.

`d_name` es el nombre que el kernel utiliza en algunos mensajes de diagnóstico. Por convención, coincide con el nombre del driver.

Los campos de callback son punteros a las funciones que implementan cada operación. Un driver solo necesita instalar los callbacks que realmente admite; los callbacks ausentes tienen como valor predeterminado stubs seguros que devuelven `ENODEV` o `EOPNOTSUPP`. La combinación más habitual para un dispositivo de caracteres es `d_open`, `d_close`, `d_read`, `d_write` y `d_ioctl`. Los drivers que admiten polling añaden `d_poll`. Los que admiten kqueue añaden `d_kqfilter`. Los que mapean memoria en espacio de usuario añaden `d_mmap` o `d_mmap_single`. Los que emulan un disco añaden `d_strategy`.

El callback `d_purge` es poco frecuente, pero conviene conocerlo. El kernel lo llama cuando el cdev está siendo destruido y el driver debe liberar cualquier I/O pendiente. La mayoría de los drivers no lo necesitan porque su `d_close` ya se encarga de la liberación.

La firma del callback `d_open` es:

```c
int myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td);
```

`dev` es el cdev que se está abriendo. `oflags` es la unión de los flags de `open(2)` (`O_RDWR`, `O_NONBLOCK`, etc.). `devtype` lleva el tipo de dispositivo y raramente resulta útil para un dispositivo de caracteres. `td` es el thread que realiza el open. El callback devuelve `0` en caso de éxito o un errno en caso de error. Un patrón habitual almacena el puntero al softc, obtenido a través de `dev->si_drv1`, en una estructura privada por apertura que las llamadas posteriores pueden recuperar.

La firma de `d_close` es paralela:

```c
int myfirst_close(struct cdev *dev, int fflags, int devtype, struct thread *td);
```

Las firmas de `d_read` y `d_write` usan la maquinaria `uio` que conociste en el Capítulo 8:

```c
int myfirst_read(struct cdev *dev, struct uio *uio, int ioflag);
int myfirst_write(struct cdev *dev, struct uio *uio, int ioflag);
```

La firma de `d_ioctl` es:

```c
int myfirst_ioctl(struct cdev *dev, u_long cmd, caddr_t data,
    int fflag, struct thread *td);
```

`cmd` es la palabra de comando del ioctl. `data` apunta al buffer de datos del ioctl (que el kernel ya ha copiado hacia adentro para los comandos `IOC_IN` y copiará hacia afuera para los comandos `IOC_OUT`, tal como analizará la Sección 3 en detalle). `fflag` son los flags de archivo de la llamada open. `td` es el thread que realiza la llamada.

### Estado por cdev a través de `si_drv1`

El cdev es el handle del kernel hacia el dispositivo, y el driver casi siempre necesita una forma de localizar su propio softc a partir de un puntero al cdev. El mecanismo estándar es `cdev->si_drv1`. El driver establece este campo cuando crea el cdev (ya sea en la llamada a `make_dev` o escribiendo en el campo posteriormente) y lo lee en cada callback de cdevsw.

El patrón tiene este aspecto en attach:

```c
sc->sc_cdev = make_dev(&myfirst_cdevsw, device_get_unit(dev),
    UID_ROOT, GID_WHEEL, 0660, "myfirst%d", device_get_unit(dev));
sc->sc_cdev->si_drv1 = sc;
```

Y este otro en cada callback:

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    struct myfirst_softc *sc = dev->si_drv1;

    DPRINTF(sc, MYF_DBG_OPEN, "open: pid=%d flags=%#x\n",
        td->td_proc->p_pid, oflags);
    /* ... rest of open ... */
    return (0);
}
```

`si_drv2` es un segundo puntero que el driver puede usar como prefiera. Algunos drivers lo usan para una cookie por instancia; otros no lo usan en absoluto y lo dejan en `NULL`. El driver `myfirst` usa únicamente `si_drv1`.

La variante `make_dev_s` es más limpia porque establece `si_drv1` en el momento de la creación:

```c
struct make_dev_args args;
make_dev_args_init(&args);
args.mda_devsw = &myfirst_cdevsw;
args.mda_uid = UID_ROOT;
args.mda_gid = GID_WHEEL;
args.mda_mode = 0660;
args.mda_si_drv1 = sc;
args.mda_unit = device_get_unit(dev);
error = make_dev_s(&args, &sc->sc_cdev, "myfirst%d", device_get_unit(dev));
if (error != 0) {
    device_printf(dev, "make_dev_s failed: %d\n", error);
    goto fail;
}
```

La forma `make_dev_s` tiene la ventaja adicional de que `si_drv1` se establece antes de que el cdev sea visible en `/dev`, lo que cierra una pequeña pero real ventana de condición de carrera en la que un programa que abre muy rápido podría invocar un cdevsw cuyo `si_drv1` fuera todavía `NULL`. Los drivers nuevos deberían preferir esta forma.

### La referencia `null(4)`

El ejemplo más limpio y reducido de cdevsw y `make_dev_credf` en el árbol de código fuente es `/usr/src/sys/dev/null/null.c`. Los fragmentos relevantes son suficientemente breves como para leerlos de una sola vez. Las declaraciones de cdevsw (una separada para `/dev/null` y `/dev/zero`):

```c
static struct cdevsw null_cdevsw = {
    .d_version = D_VERSION,
    .d_read    = (d_read_t *)nullop,
    .d_write   = null_write,
    .d_ioctl   = null_ioctl,
    .d_name    = "null",
};
```

El manejador de eventos de módulo que crea y destruye los nodos:

```c
static int
null_modevent(module_t mod, int type, void *data)
{
    switch (type) {
    case MOD_LOAD:
        full_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &full_cdevsw, 0,
            NULL, UID_ROOT, GID_WHEEL, 0666, "full");
        null_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &null_cdevsw, 0,
            NULL, UID_ROOT, GID_WHEEL, 0666, "null");
        zero_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &zero_cdevsw, 0,
            NULL, UID_ROOT, GID_WHEEL, 0666, "zero");
        break;
    case MOD_UNLOAD:
        destroy_dev(full_dev);
        destroy_dev(null_dev);
        destroy_dev(zero_dev);
        break;
    case MOD_SHUTDOWN:
        break;
    default:
        return (EOPNOTSUPP);
    }
    return (0);
}

DEV_MODULE(null, null_modevent, NULL);
MODULE_VERSION(null, 1);
```

Merece la pena detenerse en algunos detalles. El flag `MAKEDEV_ETERNAL_KLD` le indica al kernel que, incluso si el módulo se descarga en circunstancias inusuales, el cdev no debe ser invalidado silenciosamente. El modo `0666` significa que todos pueden leer y escribir; eso es correcto para `/dev/null`. El número de unidad es `0` porque solo existe uno de cada uno. La rama `MOD_LOAD` se ejecuta al cargar el módulo y crea los nodos; la rama `MOD_UNLOAD` se ejecuta al descargarlo y los destruye; `MOD_SHUTDOWN` se ejecuta durante la secuencia de apagado del sistema y aquí no hace nada, porque estos pseudo-dispositivos no requieren ningún trabajo de cierre.

Esta es la forma canónica de un pseudo-dispositivo que vive en el ámbito del módulo en lugar del árbol de dispositivos. El driver `myfirst`, en cambio, crea su cdev en `device_attach` porque la vida útil del cdev está ligada a la de un dispositivo PCI específico, no a la del módulo. Los dos patrones son diferentes, pero conviven sin problemas.

### Múltiples nodos y nodos clonables

Un solo driver puede crear más de un nodo. Hay tres patrones habituales.

El primero son los **nodos de nombre fijo**. El driver sabe de antemano cuántos nodos necesita y los crea con nombres fijos: `/dev/myfirst0`, `/dev/myfirst-status`, `/dev/myfirst-config`. Este es el patrón adecuado cuando cada nodo tiene un papel distinto y el número es conocido.

El segundo son los **nodos indexados**. El driver crea un nodo por unidad, con el nombre `/dev/myfirstN` donde `N` es el número de unidad. Este es el patrón adecuado cuando cada nodo representa una instancia independiente del mismo tipo de objeto, como una por tarjeta PCI conectada.

El tercero son los **nodos clonables**. El driver registra un manejador de clonación que crea un nuevo nodo bajo demanda cada vez que un usuario abre un nombre que coincide con un patrón. El usuario de `tun(4)` abre `/dev/tun` y obtiene `/dev/tun0`; volver a abrir `/dev/tun` produce `/dev/tun1`; el kernel asigna la siguiente unidad libre en cada apertura. Este es el patrón adecuado para pseudo-dispositivos que el usuario quiere «crear» simplemente abriéndolos.

El mecanismo de clonación es el manejador de eventos `dev_clone`. Se encuentra en `/usr/src/sys/sys/conf.h`:

```c
typedef void (*dev_clone_fn)(void *arg, struct ucred *cred, char *name,
    int namelen, struct cdev **result);

EVENTHANDLER_DECLARE(dev_clone, dev_clone_fn);
```

El driver registra un manejador con `EVENTHANDLER_REGISTER`, y el kernel llama al manejador cada vez que una ruta de apertura bajo `/dev` no coincide con un nodo existente. El manejador decide si el nombre pertenece a su driver, asigna una nueva unidad en caso afirmativo, llama a `make_dev` para crear el nodo con ese nombre y almacena el puntero al cdev resultante a través del argumento `result`. El kernel entonces reabre el nodo recién creado y continúa con la llamada open del usuario.

El driver `tun(4)` muestra este patrón. De `/usr/src/sys/net/if_tuntap.c`:

```c
static eventhandler_tag clone_tag;

static int
tuntapmodevent(module_t mod, int type, void *data)
{
    switch (type) {
    case MOD_LOAD:
        clone_tag = EVENTHANDLER_REGISTER(dev_clone, tunclone, 0, 1000);
        if (clone_tag == NULL)
            return (ENOMEM);
        ...
        break;
    case MOD_UNLOAD:
        EVENTHANDLER_DEREGISTER(dev_clone, clone_tag);
        ...
    }
}

static void
tunclone(void *arg, struct ucred *cred, char *name, int namelen,
    struct cdev **dev)
{
    /* If *dev != NULL, another handler already created the cdev. */
    if (*dev != NULL)
        return;

    /* Examine name; if it matches our pattern, allocate a unit */
    /* and call make_dev to populate *dev. */
    ...
}
```

Algunas reglas se aplican a los manejadores de clonación. El manejador no debe asumir que es el único registrado; múltiples subsistemas pueden registrarse contra `dev_clone`, y cada manejador debe comprobar si `*dev` ya es distinto de NULL antes de hacer cualquier trabajo. El manejador se ejecuta en un contexto que puede dormir, por lo que puede llamar a `make_dev` directamente. El manejador debe validar el nombre con cuidado porque lo proporciona el espacio de usuario.

El driver `myfirst` usa en principio un patrón de nodos indexados (un nodo por dispositivo PCI conectado, `/dev/myfirst0`, `/dev/myfirst1`, etc.), y el laboratorio de esta sección explica paso a paso cómo añadir un manejador de clonación para que el usuario pueda abrir `/dev/myfirst-clone` y obtener una nueva unidad bajo demanda. El patrón de clonación es más útil para pseudo-dispositivos sin hardware subyacente; para drivers respaldados por hardware raramente es necesario.

### Permisos, propietario y `devfs.rules`

`make_dev` recibe el UID del propietario inicial, el GID del grupo y los permisos del nodo. Estos valores se fijan en el momento de la creación. Son visibles desde el espacio de usuario mediante `ls -l /dev/myfirst0` y determinan qué programas en espacio de usuario pueden abrir el nodo.

Para un driver respaldado por hardware, los valores predeterminados adecuados dependen del papel del dispositivo. Un dispositivo al que solo debe acceder root usa `UID_ROOT, GID_WHEEL, 0600`. Un dispositivo al que debe poder acceder cualquier usuario administrativo usa `UID_ROOT, GID_OPERATOR, 0660`. Un dispositivo que cualquier usuario puede leer pero solo root puede escribir usa `UID_ROOT, GID_WHEEL, 0644`. El driver `myfirst` usa `UID_ROOT, GID_WHEEL, 0660` de forma predeterminada; se espera que el usuario sea root o que se le conceda acceso a través de `devfs.rules` si lo necesita.

El administrador puede anular estos valores predeterminados en tiempo de ejecución mediante `devfs.rules(8)`. Un archivo de reglas típico tiene este aspecto:

```text
[localrules=10]
add path 'myfirst*' mode 0660
add path 'myfirst*' group operator
```

El administrador activa las reglas añadiéndolas a `/etc/rc.conf`:

```text
devfs_system_ruleset="localrules"
```

Tras `service devfs restart` (o un reinicio), las reglas se aplican a los nuevos nodos de dispositivo que aparecen. Este mecanismo permite al administrador conceder acceso al driver sin recompilar el módulo, lo cual es la división correcta de responsabilidades: el desarrollador elige valores predeterminados seguros y el administrador los amplía cuando es necesario.

Un error habitual es hacer que un dispositivo tenga permisos de escritura para todos de forma predeterminada con el argumento de que «facilita las pruebas». Un driver que se distribuye con permisos `0666` para un dispositivo que controla hardware es un problema de seguridad. La recomendación del capítulo es usar `0660` con `GID_WHEEL` como valor predeterminado e instruir a los lectores en `INTEGRATION.md` sobre cómo usar `devfs.rules` para cambiarlo si lo necesitan.

### Integrando todo: el driver en la Etapa 1

El driver de la Etapa 1 del Capítulo 24 reemplaza la creación de nodos ad hoc original por el patrón moderno `make_dev_s`, establece `si_drv1` en el momento de la creación, usa el flag `D_TRACKCLOSE` y prepara el camino para los callbacks de ioctl que se añadirán en la Sección 3. A continuación se muestra el fragmento relevante de la nueva función attach. Escríbelo a mano; el propósito central del capítulo es el cambio cuidadoso de la forma ad hoc antigua a la nueva forma disciplinada.

```c
static int
myfirst_attach(device_t dev)
{
    struct myfirst_softc *sc;
    struct make_dev_args args;
    int error;

    sc = device_get_softc(dev);
    sc->sc_dev = dev;

    /* ... earlier attach work: PCI resources, MSI-X, DMA, sysctl tree
     * stub, debug subtree.  See Chapters 18-23 for these.  ... */

    /* Build the cdev for /dev/myfirstN. */
    make_dev_args_init(&args);
    args.mda_devsw = &myfirst_cdevsw;
    args.mda_uid = UID_ROOT;
    args.mda_gid = GID_WHEEL;
    args.mda_mode = 0660;
    args.mda_si_drv1 = sc;
    args.mda_unit = device_get_unit(dev);
    error = make_dev_s(&args, &sc->sc_cdev, "myfirst%d",
        device_get_unit(dev));
    if (error != 0) {
        device_printf(dev, "make_dev_s failed: %d\n", error);
        DPRINTF(sc, MYF_DBG_INIT, "cdev creation failed (%d)\n", error);
        goto fail;
    }
    DPRINTF(sc, MYF_DBG_INIT, "cdev created at /dev/myfirst%d\n",
        device_get_unit(dev));

    /* ... rest of attach: register callouts, finalise sysctl OIDs, etc. */

    return (0);

fail:
    /* Unwind earlier resources in reverse order. */
    /* See Section 7 for the discipline that goes here. */
    return (error);
}
```

El cdevsw correspondiente con `D_TRACKCLOSE`:

```c
static d_open_t myfirst_open;
static d_close_t myfirst_close;
static d_read_t myfirst_read;
static d_write_t myfirst_write;
static d_ioctl_t myfirst_ioctl;

static struct cdevsw myfirst_cdevsw = {
    .d_version = D_VERSION,
    .d_flags   = D_TRACKCLOSE,
    .d_name    = "myfirst",
    .d_open    = myfirst_open,
    .d_close   = myfirst_close,
    .d_read    = myfirst_read,
    .d_write   = myfirst_write,
    .d_ioctl   = myfirst_ioctl,
};
```

El open y el close correspondientes, con búsquedas de `si_drv1` y actualizaciones del contador por apertura:

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    struct myfirst_softc *sc = dev->si_drv1;

    SDT_PROBE2(myfirst, , , open, sc, oflags);

    mtx_lock(&sc->sc_mtx);
    sc->sc_open_count++;
    mtx_unlock(&sc->sc_mtx);

    DPRINTF(sc, MYF_DBG_OPEN,
        "open: pid=%d flags=%#x open_count=%u\n",
        td->td_proc->p_pid, oflags, sc->sc_open_count);
    return (0);
}

static int
myfirst_close(struct cdev *dev, int fflags, int devtype, struct thread *td)
{
    struct myfirst_softc *sc = dev->si_drv1;

    SDT_PROBE2(myfirst, , , close, sc, fflags);

    mtx_lock(&sc->sc_mtx);
    KASSERT(sc->sc_open_count > 0,
        ("myfirst_close: open_count underflow"));
    sc->sc_open_count--;
    mtx_unlock(&sc->sc_mtx);

    DPRINTF(sc, MYF_DBG_OPEN,
        "close: pid=%d flags=%#x open_count=%u\n",
        td->td_proc->p_pid, fflags, sc->sc_open_count);
    return (0);
}
```

Y la actualización correspondiente de detach, con `destroy_dev` antes de liberar cualquier estado del softc:

```c
static int
myfirst_detach(device_t dev)
{
    struct myfirst_softc *sc = device_get_softc(dev);

    /* Refuse detach while users still hold the device open. */
    mtx_lock(&sc->sc_mtx);
    if (sc->sc_open_count > 0) {
        mtx_unlock(&sc->sc_mtx);
        device_printf(dev, "detach refused: %u open(s) outstanding\n",
            sc->sc_open_count);
        return (EBUSY);
    }
    mtx_unlock(&sc->sc_mtx);

    /* Destroy the cdev first.  The kernel drains any in-flight
     * callbacks before destroy_dev returns. */
    if (sc->sc_cdev != NULL) {
        destroy_dev(sc->sc_cdev);
        sc->sc_cdev = NULL;
        DPRINTF(sc, MYF_DBG_INIT, "cdev destroyed\n");
    }

    /* ... rest of detach: tear down DMA, MSI-X, callouts, sysctl ctx,
     * etc., in reverse order of attach.  See Section 7. */

    return (0);
}
```

Este es el hito de la Etapa 1. El driver tiene ahora una entrada de devfs limpia, moderna y fácil de entender para quienes se inician. Las etapas siguientes añadirán ioctls y sysctls sobre esta base.

### Un recorrido concreto: cargar e inspeccionar el driver de la Etapa 1

Compila, carga e inspecciona el driver de la Etapa 1:

```sh
cd ~/myfirst-1.7-integration/stage1-devfs
make
sudo kldload ./myfirst.ko

# Confirm the device exists and has the expected attributes.
ls -l /dev/myfirst0

# Read its sysctl debug subtree (still from Chapter 23).
sysctl dev.myfirst.0

# Open it with cat to confirm the cdevsw read and close paths fire.
sudo cat /dev/myfirst0 > /dev/null
dmesg | tail -20
```

La salida esperada para `ls -l /dev/myfirst0`:

```text
crw-rw----  1 root  wheel  0x71 Apr 19 16:30 /dev/myfirst0
```

`crw-rw----` indica dispositivo de caracteres, lectura+escritura para el propietario, lectura+escritura para el grupo, sin permisos para otros. El propietario es root. El grupo es wheel. El número de minor es `0x71` (la asignación del kernel; el valor variará entre sistemas). El tamaño es el número de unidad con el que se llamó a `make_dev_s`.

La salida esperada del fragmento de `dmesg`:

```text
myfirst0: cdev created at /dev/myfirst0
myfirst0: open: pid=4321 flags=0x1 open_count=1
myfirst0: close: pid=4321 flags=0x1 open_count=0
```

(Suponiendo que la máscara de depuración tiene `MYF_DBG_INIT` y `MYF_DBG_OPEN` activados. Si la máscara es cero, las líneas permanecen en silencio; actívalas con `sysctl dev.myfirst.0.debug.mask=0xFFFFFFFF` según la disciplina de depuración del capítulo 23.)

Si las líneas de apertura y cierre no aparecen, comprueba la máscara de depuración. Si la línea de apertura aparece pero no la de cierre, has olvidado `D_TRACKCLOSE` y el kernel está llamando a close en cada cierre de fd en lugar de solo en el último; activa `D_TRACKCLOSE` o espera múltiples líneas de cierre por cada apertura. Si el contador de aperturas se vuelve negativo, tienes un bug real: se ha llamado a close sin una apertura correspondiente.

### Errores frecuentes con devfs

Cinco errores explican la mayoría de los problemas con devfs que encuentran quienes integran drivers por primera vez. Conocerlos de antemano puede ahorrarte una sesión de depuración más adelante.

El primer error es **llamar a `make_dev` desde un contexto en el que no se puede dormir**. `make_dev` puede dormir. Si se llama desde un manejador de interrupciones, desde un callout, desde dentro de un spinlock o desde cualquier contexto en el que el bloqueo esté prohibido, el kernel entrará en pánico con `WITNESS` o con `INVARIANTS` quejándose de una función que puede dormirse en un contexto que no lo permite. La solución es llamar a `make_dev` desde `device_attach` o desde un manejador de eventos de módulo (ambos contextos seguros), o bien usar `make_dev_s` con `MAKEDEV_NOWAIT` (que en ese caso puede fallar, y quien llama debe gestionar el fallo).

El segundo error es **olvidar asignar `si_drv1`**. Los callbacks de cdevsw desreferenciarán entonces un puntero NULL y provocarán un pánico en el kernel. La solución es asignar `si_drv1` inmediatamente después de `make_dev` (o, mejor aún, usar `make_dev_s` y establecer `mda_si_drv1` en los argumentos, lo que elimina la ventana de condición de carrera entre la creación y la asignación).

El tercer error es **llamar a `destroy_dev` después de liberar el softc**. Los callbacks de cdevsw pueden estar aún en ejecución cuando se llama a `destroy_dev`; el kernel los drena antes de que `destroy_dev` retorne. Si el softc ya ha sido liberado, los callbacks desreferenciarán memoria basura. La solución es llamar primero a `destroy_dev` y luego liberar el softc, en ese orden estricto.

El cuarto error es **crear dos cdevs con el mismo nombre**. El kernel comprueba si hay nombres duplicados y la segunda llamada a `make_dev` produce un pánico o devuelve un error según la variante. La solución es componer el nombre a partir del número de unidad, o usar un manejador de clones.

El quinto error es **no gestionar el caso en el que el dispositivo está abierto en el momento del detach**. Un driver ingenuo simplemente llama a `destroy_dev` y libera el softc, lo que solo funciona si ningún usuario tiene el dispositivo abierto. Un usuario que tenga `/dev/myfirst0` abierto con `cat` rompe este supuesto. La solución es rechazar el detach cuando `open_count > 0`, o usar la maquinaria `dev_ref`/`dev_rel` del kernel para coordinar. El patrón de este capítulo es el rechazo simple (`return (EBUSY)`), porque proporciona el error más claro desde el punto de vista del usuario.

### Problemas específicos de los drivers con múltiples instancias

Los drivers que crean más de un cdev por dispositivo conectado, o un cdev por canal dentro de un dispositivo multicanal, se topan con algunos problemas adicionales.

El primero es **dejar nodos sin liberar ante un fallo parcial**. Si el driver crea tres cdevs y la tercera llamada falla, debe destruir los dos primeros antes de retornar. El patrón de limpieza en caso de fallo de la sección 7 es la solución canónica. El laboratorio de este capítulo recorre el patrón con una inyección de fallo deliberada.

El segundo es **olvidar que cada cdev necesita su propio `si_drv1`**. Un driver que crea nodos por canal normalmente querrá que `si_drv1` apunte al canal en lugar de al softc. Los callbacks de cdevsw recorren entonces el camino de vuelta desde el canal hasta el softc según sea necesario. Mezclar ambos provoca que los canales corrompan el estado de los demás.

El tercero es **la visibilidad del nodo sin condiciones de carrera**. Entre el retorno de `make_dev` (o `make_dev_s`) y el momento en que el driver termina el resto del attach, un usuario suficientemente rápido puede ya abrir el nodo. El driver debe estar preparado para gestionar aperturas que lleguen antes de que el attach haya terminado por completo. El patrón más sencillo es diferir `make_dev` al final del attach, de modo que el nodo solo sea visible cuando todo el estado restante esté listo. El driver `myfirst` sigue este patrón.

Estos problemas no surgen con frecuencia en drivers con un único cdev como `myfirst`, pero reconocerlos ahora significa que no te sorprenderán cuando aparezcan en el capítulo 27 (los drivers de almacenamiento pueden tener muchos cdevs, uno por LUN) o en el capítulo 28 (los drivers de red pueden tener muchos cdevs, uno por canal de comandos)

### Cerrando la sección 2

La sección 2 convirtió la presencia del driver en `/dev` en algo de primera clase. El cdev se crea ahora con el patrón moderno de `make_dev_s`, el cdevsw está totalmente poblado con `D_TRACKCLOSE` y un conjunto de callbacks amigable para la depuración, el estado por cdev se enlaza a través de `si_drv1`, y la ruta de detach drena y destruye el cdev de forma limpia. El driver sigue realizando el mismo trabajo que `1.6-debug`, pero ahora expone ese trabajo a través de un nodo `/dev` correctamente construido que un administrador puede manejar con `chmod`, `chown`, monitorizar con `ls` y razonar desde fuera del kernel.

En la siguiente sección, pasamos a la segunda superficie de integración: la interfaz de control orientada al usuario que permite a un programa indicar al driver qué hacer. El vocabulario es `ioctl(2)`, `_IO`, `_IOR`, `_IOW`, `_IOWR`, y una pequeña cabecera pública que incluyen los programas en espacio de usuario.

## Sección 3: Implementación del soporte de `ioctl()`

### Qué es `ioctl(2)` y por qué lo necesitan los drivers

Las llamadas al sistema `read(2)` y `write(2)` son excelentes para mover flujos de bytes entre un programa de usuario y un driver. Sin embargo, no son adecuadas para el control. Un `read` no puede preguntarle al driver "¿cuál es tu estado actual?" sin sobrecargar el significado de los bytes devueltos. Un `write` no puede pedirle al driver "por favor, reinicia tus estadísticas" sin inventar un vocabulario de comandos privado dentro del flujo de bytes. La llamada al sistema que cubre esta brecha es `ioctl(2)`, la llamada de control de entrada/salida.

`ioctl` es un canal lateral para comandos. La firma en espacio de usuario es sencilla: `int ioctl(int fd, unsigned long request, ...);`. El primer argumento es un descriptor de archivo (en nuestro caso, un `/dev/myfirst0` abierto). El segundo es un código de solicitud numérico que le indica al driver qué hacer. El tercero es un puntero opcional a una estructura que transporta los parámetros de la solicitud, ya sea de entrada, de salida o de ambas direcciones. El kernel enruta la llamada hacia el cdevsw del cdev que respalda ese descriptor de archivo, hacia la función a la que apunta `d_ioctl`. El driver examina el código de solicitud, realiza la acción correspondiente y devuelve 0 en caso de éxito o un `errno` positivo en caso de fallo.

Casi todos los drivers que exponen una interfaz de control al espacio de usuario utilizan `ioctl` para ello. Los drivers de disco utilizan `ioctl` para informar sobre el tamaño del sector, las tablas de particiones y los destinos de volcado. Los drivers de cinta utilizan `ioctl` para comandos de rebobinado, expulsión y retensado. Los drivers de red utilizan `ioctl` para cambios de medio (`SIOCSIFFLAGS`), actualizaciones de dirección MAC y comandos de sondeo del bus. Los drivers de sonido utilizan `ioctl` para la negociación de la frecuencia de muestreo, el número de canales y el tamaño del buffer. El vocabulario es tan universal que aprenderlo una vez abre el acceso a cada categoría de driver del árbol.

Para el driver `myfirst`, `ioctl` nos permite añadir comandos que no tienen una expresión limpia como bytes. Podemos dejar que el operador consulte la longitud del mensaje en memoria sin tener que leerlo. Podemos dejar que el operador reinicie el mensaje y los contadores de apertura sin tener que escribir un centinela especial. Podemos exponer el número de versión del driver para que las herramientas en espacio de usuario puedan detectar la API con la que se están comunicando. Cada uno de estos casos es un cambio de una línea para el operador y de media página para el driver, y todos encajan perfectamente en el modelo de `ioctl`.

Esta sección recorre todo el pipeline de ioctl: la codificación de los códigos de solicitud, el copyin y copyout automáticos del kernel, el diseño de una cabecera pública, la implementación del despachador, la construcción de un pequeño programa de acompañamiento en espacio de usuario, y los errores más frecuentes. Al final de la sección, el driver estará en la versión `1.7-integration-stage-2` y admitirá cuatro comandos ioctl: `MYFIRSTIOC_GETVER`, `MYFIRSTIOC_GETMSG`, `MYFIRSTIOC_SETMSG` y `MYFIRSTIOC_RESET`.

### Cómo se codifican los números de `ioctl`

Un código de solicitud ioctl no es un entero arbitrario. Es un valor de 32 bits compacto que codifica cuatro piezas de información en campos de bits fijos, definidos en `/usr/src/sys/sys/ioccom.h`. La cabecera comienza con un comentario que muestra la disposición, y vale la pena leerlo antes de continuar.

```c
/*
 * Ioctl's have the command encoded in the lower word, and the size of
 * any in or out parameters in the upper word.  The high 3 bits of the
 * upper word are used to encode the in/out status of the parameter.
 *
 *       31 29 28                     16 15            8 7             0
 *      +---------------------------------------------------------------+
 *      | I/O | Parameter Length        | Command Group | Command       |
 *      +---------------------------------------------------------------+
 */
```

Los cuatro campos son:

Los **bits de dirección** (bits 29 a 31) indican al kernel si el tercer argumento de `ioctl` es puramente de salida (`IOC_OUT`, el kernel copiará el resultado de vuelta al espacio de usuario), puramente de entrada (`IOC_IN`, el kernel copiará los datos del usuario al kernel antes de ejecutar el despachador), bidireccional (`IOC_INOUT`, ambas direcciones) o ausente (`IOC_VOID`, la solicitud no tiene argumento de datos). El kernel usa estos bits para decidir qué `copyin` y `copyout` realizar de forma automática. El driver en sí nunca tiene que llamar a `copyin` o `copyout` para un ioctl correctamente codificado.

La **longitud del parámetro** (bits 16 a 28) codifica el tamaño en bytes de la estructura pasada como tercer argumento, con un máximo de `IOCPARM_MAX = 8192`. El kernel usa este tamaño para asignar un buffer temporal en el kernel, realizar el `copyin` o `copyout` apropiado, y presentar el buffer al despachador como argumento `caddr_t data`. Un driver que necesite pasar más de 8192 bytes a través de un único ioctl debe incrustar un puntero en una estructura más pequeña (asumiendo el coste de realizar su propio `copyin`), o usar un mecanismo diferente como `mmap` o `read`.

El **grupo de comandos** (bits 8 a 15) es un único carácter que nombra la familia de ioctls relacionados. Convencionalmente es una de las letras ASCII imprimibles e identifica el subsistema. `'d'` lo usan los ioctls de disco GEOM (`DIOCGMEDIASIZE`, `DIOCGSECTORSIZE`). `'i'` lo usa `if_ioctl` (`SIOCSIFFLAGS`). `'t'` lo usan los ioctls de terminal (`TIOCGPTN`). Debes elegir una letra que no esté ya ocupada por algo con lo que el driver pueda coexistir. Para el driver `myfirst` usaremos `'M'`.

El **número de comando** (bits 0 a 7) es un entero pequeño que identifica el ioctl específico dentro del grupo. La numeración suele empezar en 1 y aumenta de forma monótona a medida que se añaden comandos. Reutilizar un número es un riesgo para la compatibilidad hacia atrás, por lo que un driver que retire un comando debe dejar el número reservado en lugar de reciclarlo.

Los macros de `ioccom.h` construyen estas codificaciones por ti. Son la única forma correcta de construir números de ioctl:

```c
#define _IO(g,n)        _IOC(IOC_VOID, (g), (n), 0)
#define _IOR(g,n,t)     _IOC(IOC_OUT,  (g), (n), sizeof(t))
#define _IOW(g,n,t)     _IOC(IOC_IN,   (g), (n), sizeof(t))
#define _IOWR(g,n,t)    _IOC(IOC_INOUT,(g), (n), sizeof(t))
```

`_IO` declara un comando que no recibe ningún argumento. `_IOR` declara un comando que devuelve un resultado del tamaño de `t` al espacio de usuario. `_IOW` declara un comando que acepta un argumento del tamaño de `t` desde el espacio de usuario. `_IOWR` declara un comando que acepta un argumento del tamaño de `t` y escribe un resultado del tamaño de `t` de vuelta a través del mismo buffer. La `t` es un tipo, no un puntero; los macros usan `sizeof(t)` para calcular el campo de longitud.

Algunos ejemplos del mundo real hacen el patrón más concreto. De `/usr/src/sys/sys/disk.h`:

```c
#define DIOCGSECTORSIZE _IOR('d', 128, u_int)
#define DIOCGMEDIASIZE  _IOR('d', 129, off_t)
```

Estos comandos son solicitudes de solo lectura para el tamaño del sector (devuelto como `u_int`) y el tamaño del medio (devuelto como `off_t`). La letra de grupo `'d'` y los números 128 y 129 están reservados para el subsistema de disco.

El driver en sí nunca tiene que decodificar la disposición de bits. El código de comando es opaco para el despachador, que lo compara con constantes con nombre:

```c
switch (cmd) {
case MYFIRSTIOC_GETVER:
        ...
        break;
case MYFIRSTIOC_RESET:
        ...
        break;
default:
        return (ENOIOCTL);
}
```

El kernel usa la disposición de bits cuando configura la llamada (para asignar el buffer y realizar copyin/copyout), y el espacio de usuario la usa implícitamente a través de los macros. Entre esos dos puntos, el código de solicitud es simplemente una etiqueta.

### Elegir una letra de grupo

La elección de una letra de grupo es importante porque los conflictos son silenciosos. Dos drivers que elijan la misma letra de grupo y el mismo número de comando verán solicitudes ioctl destinadas al otro driver si el operador los confunde. El kernel no impone la unicidad entre drivers, en parte porque ninguna autoridad central asigna letras y en parte porque la mayoría de las letras están reservadas de facto por tradición y no por registro.

Un enfoque defensivo es seguir estas convenciones:

Usa **letras minúsculas** (`'d'`, `'t'`, `'i'`) solo cuando estés extendiendo un subsistema conocido cuya letra ya conoces. Las letras minúsculas están muy utilizadas por los drivers base y es fácil colisionar con ellas.

Usa **letras mayúsculas** (`'M'`, `'X'`, `'Q'`) para drivers nuevos que necesiten su propio espacio de nombres de ioctl. Hay 26 letras mayúsculas y muchas menos colisiones en el árbol.

Evita los **dígitos** por completo. Están reservados por convención histórica para los primeros subsistemas, y un driver nuevo que utilice uno resultará fuera de lugar a los ojos de cualquier revisor.

Para el driver `myfirst` usamos `'M'`. Es la primera letra del nombre del driver, está en mayúscula (para no colisionar con ningún subsistema base) y hace que los códigos de petición se documenten solos en los stack traces y en la salida de `ktrace`: un volcado hexadecimal de un número ioctl con `0x4d` (el valor ASCII de `'M'`) en el campo de grupo identifica sin ninguna ambigüedad un comando de `myfirst`.

### La firma del callback `d_ioctl`

La función dispatcher a la que apunta `cdevsw->d_ioctl` tiene el tipo `d_ioctl_t`, definido en `/usr/src/sys/sys/conf.h`. La firma es:

```c
typedef int d_ioctl_t(struct cdev *dev, u_long cmd, caddr_t data,
                      int fflag, struct thread *td);
```

Los cinco argumentos merecen una lectura pausada.

`dev` es el cdev que respalda el descriptor de archivo sobre el que el usuario llamó a `ioctl`. El driver usa `dev->si_drv1` para recuperar su softc. Este es el mismo patrón que emplea cada callback de cdevsw que ya hemos visto.

`cmd` es el código de petición, el valor que el usuario pasó como segundo argumento a `ioctl`. El driver lo compara con las constantes con nombre de su cabecera pública.

`data` es la copia local del kernel del tercer argumento. Como el kernel realizó el copyin y realizará el copyout (para las peticiones `_IOR`, `_IOW` y `_IOWR`), `data` es siempre un puntero del kernel. El driver lo desreferencia directamente sin llamar a `copyin`. Para las peticiones `_IO`, `data` no está definido y no debe desreferenciarse.

`fflag` son las banderas de archivo de la llamada open: `FREAD`, `FWRITE` o ambas. El driver puede usar `fflag` para imponer acceso de solo lectura o solo escritura para comandos específicos. Un comando que restablece el estado, por ejemplo, podría requerir `FWRITE` y devolver `EBADF` en caso contrario.

`td` es el thread llamante. El driver puede usar `td` para extraer las credenciales del llamante (`td->td_ucred`), para realizar comprobaciones de privilegios (`priv_check_cred(td->td_ucred, PRIV_DRIVER, 0)`), o simplemente para registrar el pid del llamante. Para la mayoría de los comandos, `td` no se usa.

El valor de retorno es 0 en caso de éxito, o un valor `errno` positivo en caso de fallo. El valor especial `ENOIOCTL` (definido en `/usr/src/sys/sys/errno.h`) indica al kernel que el driver no reconoce el comando, y el kernel enrutará entonces el comando a través del manejador ioctl genérico de la capa del sistema de archivos. Devolver `EINVAL` en lugar de `ENOIOCTL` para un comando desconocido es un bug sutil: le dice al kernel «reconocí el comando pero los argumentos son incorrectos», lo que suprime el fallback genérico. Usa siempre `ENOIOCTL` para el caso por defecto.

### Cómo realiza el kernel el copyin y el copyout

Antes de que se ejecute el dispatcher, el kernel inspecciona los bits de dirección y la longitud de los parámetros codificada en `cmd`. Si se activa `IOC_IN`, el kernel lee la longitud del parámetro, asigna un buffer temporal del kernel de ese tamaño, copia en él el argumento del espacio de usuario (`copyin`) y pasa el buffer del kernel como `data`. Si se activa `IOC_OUT`, el kernel asigna un buffer, llama al dispatcher con el buffer (no inicializado) como `data` y, si el retorno es 0, copia el buffer de vuelta al espacio de usuario (`copyout`). Si ambos bits están activos (`IOC_INOUT`), el kernel realiza tanto el copyin como el copyout alrededor de la llamada al dispatcher. Si ninguno está activo (`IOC_VOID`), no se asigna ningún buffer y `data` no está definido.

Esta automatización tiene dos consecuencias que vale la pena recordar.

En primer lugar, el dispatcher escribe en `data` y lee de él usando la desreferenciación normal de C. El driver nunca llama a `copyin` ni a `copyout` para un ioctl correctamente codificado. Esta es una de las razones por las que una interfaz ioctl bien diseñada es mucho más sencilla de implementar que, por ejemplo, un protocolo de escritura y lectura que simule un canal de control.

En segundo lugar, el tipo de parámetro codificado en `_IOW` o `_IOR` debe coincidir con lo que el dispatcher realmente lee o escribe. Si la cabecera del espacio de usuario declara `_IOR('M', 1, uint32_t)` pero el dispatcher escribe un `uint64_t` en `*(uint64_t *)data`, el dispatcher desbordará el buffer de 4 bytes del kernel y corromperá la memoria de la pila adyacente, provocando un panic del kernel en una build con `WITNESS` habilitado y corrompiendo el estado de forma silenciosa en una build de producción. La cabecera es el contrato; el dispatcher debe respetarlo byte a byte.

Para ioctls con punteros embebidos (una struct que contiene un `char *buf` apuntando a un buffer separado), el kernel no puede realizar el copyin ni el copyout del buffer porque no forma parte de la estructura. El driver debe hacer su propio `copyin` y `copyout` para el contenido del buffer, mientras el kernel gestiona la struct envolvente. Este patrón es necesario para datos de longitud variable y se trata en la subsección de problemas comunes más adelante.

### Diseño de una cabecera pública

Un driver que expone ioctls debe publicar una cabecera que declare los códigos de petición y las estructuras de datos. Los programas del espacio de usuario incluyen esta cabecera para construir llamadas correctas. La cabecera vive fuera del módulo del kernel: es parte del contrato del driver con el espacio de usuario y debe poder instalarse en el sistema (por ejemplo, en `/usr/local/include/myfirst/myfirst_ioctl.h`).

La convención es colocar la cabecera en el directorio de código fuente del driver con un sufijo `.h` que coincida con el nombre público. Para el driver `myfirst`, la cabecera es `myfirst_ioctl.h`. Sus responsabilidades son limitadas: declarar los números de ioctl, declarar las estructuras utilizadas como argumentos de ioctl, declarar cualquier constante relacionada (como la longitud máxima del campo de mensaje) y nada más. No debe incluir cabeceras exclusivas del kernel, no debe declarar tipos exclusivos del kernel y debe compilar limpiamente cuando la incluya un programa del espacio de usuario.

Esta es la cabecera completa para el driver de la etapa 2 del capítulo:

```c
/*
 * myfirst_ioctl.h - public ioctl interface for the myfirst driver.
 *
 * This header is included by both the kernel module and any user-space
 * program that talks to the driver. Keep it self-contained: no kernel
 * headers, no kernel types, no inline functions that pull kernel state.
 */

#ifndef _MYFIRST_IOCTL_H_
#define _MYFIRST_IOCTL_H_

#include <sys/ioccom.h>
#include <sys/types.h>

/*
 * Maximum length of the in-driver message, including the trailing NUL.
 * The driver enforces this on SETMSG; user-space programs that build
 * larger buffers will see EINVAL.
 */
#define MYFIRST_MSG_MAX 256

/*
 * The interface version. Bumped when this header changes in a way that
 * is not backward-compatible. User-space programs should call
 * MYFIRSTIOC_GETVER first and refuse to operate on an unexpected
 * version.
 */
#define MYFIRST_IOCTL_VERSION 1

/*
 * MYFIRSTIOC_GETVER - return the driver's interface version.
 *
 *   ioctl(fd, MYFIRSTIOC_GETVER, &ver);   // ver = 1, 2, ...
 *
 * No FREAD or FWRITE flag is required.
 */
#define MYFIRSTIOC_GETVER  _IOR('M', 1, uint32_t)

/*
 * MYFIRSTIOC_GETMSG - copy the current in-driver message into the
 * caller's buffer. The buffer must be MYFIRST_MSG_MAX bytes; the
 * message is NUL-terminated.
 */
#define MYFIRSTIOC_GETMSG  _IOR('M', 2, char[MYFIRST_MSG_MAX])

/*
 * MYFIRSTIOC_SETMSG - replace the in-driver message. The buffer must
 * be MYFIRST_MSG_MAX bytes; the kernel takes the prefix up to the
 * first NUL or to MYFIRST_MSG_MAX - 1 bytes.
 *
 * Requires FWRITE on the file descriptor.
 */
#define MYFIRSTIOC_SETMSG  _IOW('M', 3, char[MYFIRST_MSG_MAX])

/*
 * MYFIRSTIOC_RESET - reset all per-instance counters and clear the
 * message. Returns 0 on success.
 *
 * Requires FWRITE on the file descriptor.
 */
#define MYFIRSTIOC_RESET   _IO('M', 4)

#endif /* _MYFIRST_IOCTL_H_ */
```

Merece la pena detenerse en algunos detalles de esta cabecera.

El uso de `uint32_t` y `sys/types.h` (en lugar de `u_int32_t` y `sys/cdefs.h`) mantiene la cabecera portable tanto en la base de FreeBSD como en cualquier programa que siga POSIX. El kernel y el espacio de usuario coinciden en el tamaño de `uint32_t`, por lo que la longitud codificada en el código de petición coincide con la visión que tiene el dispatcher de los datos.

La longitud máxima del mensaje, `MYFIRST_MSG_MAX = 256`, está muy por debajo de `IOCPARM_MAX = 8192`, por lo que el kernel realizará el copyin y el copyout del mensaje sin problemas. Un driver que necesitara transferir mensajes más grandes tendría que aumentar el límite (hasta 8192) o cambiar al patrón de puntero embebido.

La constante `MYFIRST_IOCTL_VERSION` da al espacio de usuario una forma de detectar cambios en la API. El primer ioctl que cualquier programa debería emitir es `MYFIRSTIOC_GETVER`; si la versión devuelta no coincide con la que usó el programa al compilarse, este debería negarse a emitir más ioctls y mostrar un error claro. Esta es la práctica habitual para drivers que esperan evolucionar.

El tipo de argumento `char[MYFIRST_MSG_MAX]` es inusual pero legal en `_IOR` e `_IOW`. La macro toma `sizeof(t)`, y `sizeof(char[256]) == 256`, por lo que la longitud codificada es exactamente el tamaño del array. Esta es la forma más limpia de expresar un buffer de tamaño fijo en una cabecera ioctl pública.

### Implementación del dispatcher

Con la cabecera disponible, el dispatcher es una sentencia switch que lee el código de comando, realiza la acción y devuelve 0 (éxito) o un errno positivo (fallo). El dispatcher vive en `myfirst_ioctl.c`, un nuevo archivo de código fuente añadido al driver en la etapa 2.

El dispatcher completo:

```c
/*
 * myfirst_ioctl.c - ioctl dispatcher for the myfirst driver.
 *
 * The d_ioctl callback in myfirst_cdevsw points at myfirst_ioctl.
 * Per-command argument layout is documented in myfirst_ioctl.h, which
 * is shared with user space.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/proc.h>

#include "myfirst.h"
#include "myfirst_debug.h"
#include "myfirst_ioctl.h"

SDT_PROBE_DEFINE3(myfirst, , , ioctl,
    "struct myfirst_softc *", "u_long", "int");

int
myfirst_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error = 0;

        SDT_PROBE3(myfirst, , , ioctl, sc, cmd, fflag);
        DPRINTF(sc, MYF_DBG_IOCTL, "ioctl: cmd=0x%08lx fflag=0x%x\n",
            cmd, fflag);

        mtx_lock(&sc->sc_mtx);

        switch (cmd) {
        case MYFIRSTIOC_GETVER:
                *(uint32_t *)data = MYFIRST_IOCTL_VERSION;
                break;

        case MYFIRSTIOC_GETMSG:
                /*
                 * Copy the current message into the caller's buffer.
                 * The buffer is MYFIRST_MSG_MAX bytes; we always emit
                 * a NUL-terminated string.
                 */
                strlcpy((char *)data, sc->sc_msg, MYFIRST_MSG_MAX);
                break;

        case MYFIRSTIOC_SETMSG:
                if ((fflag & FWRITE) == 0) {
                        error = EBADF;
                        break;
                }
                /*
                 * The kernel has copied MYFIRST_MSG_MAX bytes into
                 * data. Take the prefix up to the first NUL.
                 */
                strlcpy(sc->sc_msg, (const char *)data, MYFIRST_MSG_MAX);
                sc->sc_msglen = strlen(sc->sc_msg);
                DPRINTF(sc, MYF_DBG_IOCTL,
                    "SETMSG: new message is %zu bytes\n", sc->sc_msglen);
                break;

        case MYFIRSTIOC_RESET:
                if ((fflag & FWRITE) == 0) {
                        error = EBADF;
                        break;
                }
                sc->sc_open_count = 0;
                sc->sc_total_reads = 0;
                sc->sc_total_writes = 0;
                bzero(sc->sc_msg, sizeof(sc->sc_msg));
                sc->sc_msglen = 0;
                DPRINTF(sc, MYF_DBG_IOCTL,
                    "RESET: counters and message cleared\n");
                break;

        default:
                error = ENOIOCTL;
                break;
        }

        mtx_unlock(&sc->sc_mtx);
        return (error);
}
```

Hay varias decisiones disciplinadas integradas en este dispatcher que merece la pena señalar antes de que el lector escriba el suyo.

El dispatcher toma el mutex del softc una sola vez al principio y lo libera una sola vez al final. Cada comando se ejecuta bajo el mutex. Esto impide que un `read` compita con un `SETMSG` (de lo contrario, el read vería un buffer de mensaje a medio reemplazar) y evita que dos llamadas `RESET` simultáneas corrompan los contadores. El mutex es el mismo `sc->sc_mtx` introducido anteriormente en la Parte IV; simplemente estamos extendiendo su ámbito para cubrir la serialización de los ioctl.

La primera acción del dispatcher tras tomar el mutex es una sola sonda SDT y un único `DPRINTF`. Ambos reportan el comando y las banderas de archivo. La sonda SDT permite a un script DTrace rastrear cada ioctl en tiempo real; el `DPRINTF` permite a un operador activar `MYF_DBG_IOCTL` y observar el mismo flujo a través de `dmesg`. Ambos utilizan la infraestructura de depuración introducida en el Capítulo 23 sin necesidad de ningún mecanismo nuevo.

Las ramas `MYFIRSTIOC_SETMSG` y `MYFIRSTIOC_RESET` comprueban `fflag & FWRITE` antes de mutar el estado. Sin esta comprobación, un programa que hubiera abierto el dispositivo en modo solo lectura podría cambiar el estado del driver, lo que es un patrón de escalada de privilegios en algunos drivers. La comprobación devuelve `EBADF` (descriptor de archivo incorrecto para la operación) en lugar de `EPERM` (sin permiso), porque el fallo tiene que ver con las banderas de apertura del archivo y no con la identidad del usuario.

La rama por defecto devuelve `ENOIOCTL`, nunca `EINVAL`. Esta es la regla de la subsección anterior, repetida aquí porque es el bug más frecuente en los dispatchers escritos a mano.

Las llamadas a `strlcpy` en `GETMSG` y `SETMSG` son la primitiva de copia de cadenas segura del kernel de FreeBSD. Garantizan la terminación NUL y nunca desbordan el destino. Las mismas llamadas serían `strncpy` en código más antiguo; `strlcpy` es la forma moderna preferida y la que recomienda `style(9)`.

### Las adiciones al softc

La etapa 2 extiende el softc con dos campos y confirma que los campos existentes siguen en uso:

```c
struct myfirst_softc {
        device_t        sc_dev;
        struct cdev    *sc_cdev;
        struct mtx      sc_mtx;

        /* From earlier chapters. */
        uint32_t        sc_debug;
        u_int           sc_open_count;
        u_int           sc_total_reads;
        u_int           sc_total_writes;

        /* New for stage 2. */
        char            sc_msg[MYFIRST_MSG_MAX];
        size_t          sc_msglen;
};
```

El buffer de mensaje es un array de tamaño fijo dimensionado para coincidir con la cabecera pública. Almacenarlo en línea (en lugar de como puntero a un buffer asignado por separado) simplifica la gestión del tiempo de vida: el buffer vive exactamente lo mismo que el softc. No hay ningún `malloc` que rastrear ni ningún `free` que olvidar.

La inicialización en `myfirst_attach` queda así:

```c
strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
sc->sc_msglen = strlen(sc->sc_msg);
```

El driver tiene ahora un saludo por defecto que persiste hasta que el operador lo cambia mediante `SETMSG`, y sobrevive a ciclos de `unload`/`load` únicamente porque el nuevo valor se reinicia en cada carga. (Este es el mismo tiempo de vida que cualquier otro campo del softc; la persistencia entre reinicios requeriría un ajustable sysctl, que es el tema de la Sección 4.)

### Conectar el dispatcher a `cdevsw`

El cdevsw declarado en la etapa 1 ya tenía un campo `.d_ioctl` esperando ser rellenado. La etapa 2 lo rellena:

```c
static struct cdevsw myfirst_cdevsw = {
        .d_version = D_VERSION,
        .d_flags   = D_TRACKCLOSE,
        .d_name    = "myfirst",
        .d_open    = myfirst_open,
        .d_close   = myfirst_close,
        .d_read    = myfirst_read,
        .d_write   = myfirst_write,
        .d_ioctl   = myfirst_ioctl,    /* new */
};
```

El kernel lee esta tabla una sola vez, cuando se carga el módulo. No hay ningún paso de registro en tiempo de ejecución; el cdevsw es parte del estado estático del driver.

### Compilar el driver de la etapa 2

El `Makefile` de la etapa 2 debe incluir el nuevo archivo de código fuente:

```make
KMOD=   myfirst
SRCS=   myfirst.c myfirst_debug.c myfirst_ioctl.c

CFLAGS+= -I${.CURDIR}

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
```

Los comandos de compilación no cambian respecto a la etapa 1:

```console
$ make
$ sudo kldload ./myfirst.ko
$ ls -l /dev/myfirst0
crw-rw---- 1 root wheel 0x... <date> /dev/myfirst0
```

Si la compilación falla porque no se encuentra `myfirst_ioctl.h`, comprueba que la línea `CFLAGS` incluye `-I${.CURDIR}`. Si la carga falla por un símbolo no resuelto como `myfirst_ioctl`, comprueba que `myfirst_ioctl.c` esté listado en `SRCS` y que el nombre de la función coincida con la entrada en cdevsw.

### El programa auxiliar `myfirstctl` en espacio de usuario

Un driver con una interfaz ioctl necesita un pequeño programa auxiliar que la ejercite. Sin él, el operador no tiene forma de llamar a los ioctls salvo mediante una prueba escrita a mano o a través del paso de ioctl de `devctl(8)`, lo que resulta incómodo para el uso habitual.

El programa auxiliar es `myfirstctl`, un programa C de un único archivo que recibe un subcomando en la línea de comandos y llama al ioctl correspondiente. Es deliberadamente pequeño (menos de 200 líneas) y depende únicamente de la cabecera pública.

```c
/*
 * myfirstctl.c - command-line front end to the myfirst driver's ioctls.
 *
 * Build:  cc -o myfirstctl myfirstctl.c
 * Usage:  myfirstctl get-version
 *         myfirstctl get-message
 *         myfirstctl set-message "<text>"
 *         myfirstctl reset
 */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "myfirst_ioctl.h"

#define DEVPATH "/dev/myfirst0"

static void
usage(void)
{
        fprintf(stderr,
            "usage: myfirstctl get-version\n"
            "       myfirstctl get-message\n"
            "       myfirstctl set-message <text>\n"
            "       myfirstctl reset\n");
        exit(EX_USAGE);
}

int
main(int argc, char **argv)
{
        int fd, flags;
        const char *cmd;

        if (argc < 2)
                usage();
        cmd = argv[1];

        /*
         * SETMSG and RESET need write access; the others only need
         * read. Open the device with the right flags so the dispatcher
         * does not return EBADF.
         */
        if (strcmp(cmd, "set-message") == 0 ||
            strcmp(cmd, "reset") == 0)
                flags = O_RDWR;
        else
                flags = O_RDONLY;

        fd = open(DEVPATH, flags);
        if (fd < 0)
                err(EX_OSERR, "open %s", DEVPATH);

        if (strcmp(cmd, "get-version") == 0) {
                uint32_t ver;
                if (ioctl(fd, MYFIRSTIOC_GETVER, &ver) < 0)
                        err(EX_OSERR, "MYFIRSTIOC_GETVER");
                printf("driver ioctl version: %u\n", ver);
        } else if (strcmp(cmd, "get-message") == 0) {
                char buf[MYFIRST_MSG_MAX];
                if (ioctl(fd, MYFIRSTIOC_GETMSG, buf) < 0)
                        err(EX_OSERR, "MYFIRSTIOC_GETMSG");
                printf("%s\n", buf);
        } else if (strcmp(cmd, "set-message") == 0) {
                char buf[MYFIRST_MSG_MAX];
                if (argc < 3)
                        usage();
                strlcpy(buf, argv[2], sizeof(buf));
                if (ioctl(fd, MYFIRSTIOC_SETMSG, buf) < 0)
                        err(EX_OSERR, "MYFIRSTIOC_SETMSG");
        } else if (strcmp(cmd, "reset") == 0) {
                if (ioctl(fd, MYFIRSTIOC_RESET) < 0)
                        err(EX_OSERR, "MYFIRSTIOC_RESET");
        } else {
                usage();
        }

        close(fd);
        return (0);
}
```

Merece la pena señalar dos detalles.

El programa abre el dispositivo con las banderas mínimas necesarias para la operación solicitada. `MYFIRSTIOC_GETVER` y `MYFIRSTIOC_GETMSG` funcionan correctamente con `O_RDONLY`, pero `MYFIRSTIOC_SETMSG` y `MYFIRSTIOC_RESET` requieren `O_RDWR` porque el dispatcher comprueba `fflag & FWRITE`. Un usuario que ejecute `myfirstctl set-message foo` sin la pertenencia al grupo adecuado verá `Permission denied` de `open`; uno que tenga la pertenencia pero al que el dispatcher siga rechazando verá `Bad file descriptor` de `ioctl`. Ambos errores son comprensibles.

La llamada a `MYFIRSTIOC_RESET` no pasa ningún tercer argumento porque la macro `_IO` (sin `R` ni `W`) declara un ioctl de datos void. La función `ioctl(2)` de la biblioteca es variádica, por lo que llamarla con dos argumentos es legal, aunque hay que tener cuidado porque un argumento adicional se pasaría pero sería ignorado. La convención en este libro es llamar a los ioctls `_IO` con exactamente dos argumentos para dejar claro en el código fuente su naturaleza de datos void.

Una sesión típica tiene este aspecto:

```console
$ myfirstctl get-version
driver ioctl version: 1
$ myfirstctl get-message
Hello from myfirst
$ myfirstctl set-message "drivers are fun"
$ myfirstctl get-message
drivers are fun
$ myfirstctl reset
$ myfirstctl get-message

$
```

Tras el `reset`, el buffer de mensaje está vacío y `myfirstctl get-message` imprime una línea en blanco. Los contadores también se reinician, algo que la interfaz sysctl de la siguiente sección nos permitirá verificar directamente.

### Errores frecuentes con ioctl

El primer error es el **desajuste de tamaño de tipo entre la cabecera y el dispatcher**. Si la cabecera declara `_IOR('M', 1, uint32_t)` pero el dispatcher escribe un `uint64_t` en `*(uint64_t *)data`, el kernel ha asignado un buffer de 4 bytes y el dispatcher escribe 8 bytes en él. Los 4 bytes extra corrompen lo que hubiera al lado del buffer (normalmente otros argumentos de ioctl o el stack frame local del dispatcher). Con `WITNESS` e `INVARIANTS` activos, el kernel puede detectar el desbordamiento y provocar un panic; en una build de producción, el resultado es corrupción silenciosa. La solución es mantener la cabecera y el dispatcher sincronizados, idealmente incluyendo la misma cabecera en ambos sitios (que es lo que hace el patrón de este capítulo).

El segundo error son los **punteros embebidos**. Una estructura que contiene `char *buf; size_t len;` no puede transferirse de forma segura a través de un único `_IOW`. El kernel hará copyin de la estructura (el puntero y la longitud), pero el buffer al que apunta ese puntero está en espacio de usuario y el dispatcher no puede desreferenciarlo directamente. El dispatcher debe llamar a `copyin(uap->buf, kbuf, uap->len)` para transferir el contenido del buffer por su cuenta. Olvidar este paso hace que el dispatcher lea memoria de espacio de usuario a través de un puntero del kernel, lo que la protección de espacio de direcciones del kernel detectará como un fallo. La solución es bien incrustar el buffer directamente en la estructura (el patrón que este capítulo usa para el campo de mensaje), bien añadir llamadas explícitas a `copyin`/`copyout` dentro del dispatcher.

El tercer error es **no gestionar `ENOIOCTL` correctamente**. Un driver que devuelve `EINVAL` para un comando desconocido suprime el fallback genérico de ioctl del kernel. El usuario podría ver `Invalid argument` en un comando que debería haberse pasado silenciosamente a la capa del sistema de archivos (como `FIONBIO` para la indicación de I/O no bloqueante). La solución es usar `ENOIOCTL` como valor de retorno por defecto.

El cuarto error es **cambiar el formato de cable de un ioctl existente**. Una vez que un programa se compila contra `MYFIRSTIOC_SETMSG` declarado con un buffer de 256 bytes, recompilar el driver con un buffer de 512 bytes rompe el programa: la longitud codificada en el código de petición cambia, el kernel detecta el desajuste (porque el usuario ha pasado un buffer de 256 bytes con el nuevo comando de 512 bytes) y la llamada a `ioctl` devuelve `ENOTTY` ("Inappropriate ioctl for device"). La solución es dejar los ioctls existentes sin tocar y definir nuevos comandos con nuevos números cuando haya que cambiar el formato. La constante `MYFIRST_IOCTL_VERSION` permite a los programas de espacio de usuario detectar esa evolución antes de emitir las llamadas afectadas.

El quinto error es **realizar trabajo lento en el dispatcher mientras se mantiene un mutex**. El dispatcher de esta sección mantiene `sc->sc_mtx` durante todo el bloque switch, lo que es correcto porque todos los comandos son rápidos (un memcpy, un reinicio de contador, un strlcpy). Un driver real que necesite realizar una operación de hardware que pueda tardar milisegundos debe soltar primero el mutex y readquirirlo después, o usar un lock que admita sleep. Mantener un mutex que no admite sleep a través de un `tsleep` o `msleep` provocaría un panic en el kernel.

### Cerrando la sección 3

La sección 3 ha completado la segunda superficie de integración: una interfaz ioctl correctamente diseñada. El driver ahora expone cuatro comandos a través de `MYFIRSTIOC_GETVER`, `MYFIRSTIOC_GETMSG`, `MYFIRSTIOC_SETMSG` y `MYFIRSTIOC_RESET`. La interfaz se autodescribe (cualquier programa de espacio de usuario puede llamar a `MYFIRSTIOC_GETVER` para detectar la versión de la API), la codificación es explícita (las macros `_IOR`/`_IOW`/`_IO` de `/usr/src/sys/sys/ioccom.h`) y el kernel gestiona el copyin y copyout automáticamente en función de la disposición de bits. El programa complementario `myfirstctl` demuestra cómo una herramienta de espacio de usuario ejercita la interfaz sin tocar nunca directamente los bytes del código de petición.

El hito del driver para la etapa 2 es la adición de `myfirst_ioctl.c` y `myfirst_ioctl.h`, ambos integrados sin fricciones con la infraestructura de depuración del Capítulo 23 (el bit de máscara `MYF_DBG_IOCTL` y la sonda SDT `myfirst:::ioctl`). El `Makefile` ha crecido en una entrada de `SRCS` y el cdevsw tiene ya un callback más relleno. Todo lo demás del driver permanece sin cambios.

En la sección 4 pasamos a la tercera superficie de integración: controles de solo lectura y lectura-escritura que un administrador puede consultar y ajustar desde la shell usando `sysctl(8)`. Donde ioctl es el canal adecuado para un programa que ya tiene el dispositivo abierto, sysctl es el canal adecuado para un script o un operador que quiere inspeccionar o ajustar el estado del driver sin abrir nada. Ambas interfaces se complementan; la mayoría de los drivers de producción ofrecen las dos.

## Sección 4: Exponer métricas a través de `sysctl()`

### Qué es `sysctl` y por qué lo usan los drivers

`sysctl(8)` es el servicio de nombres jerárquico del kernel de FreeBSD. Cada nombre del árbol apunta a un fragmento del estado del kernel: una constante, un contador, una variable ajustable o un puntero a función que produce un valor bajo demanda. El árbol tiene como raíz `kern.`, `vm.`, `hw.`, `net.`, `dev.` y unos pocos prefijos de nivel superior más. Cualquier programa con los privilegios apropiados puede leer y (para los nodos con escritura habilitada) modificar estos valores a través de la biblioteca `sysctl(3)`, la herramienta de línea de comandos `sysctl(8)` o la interfaz de conveniencia `sysctlbyname(3)`.

Para los drivers, el subárbol relevante es `dev.<driver_name>.<unit>.*`. El subsistema Newbus crea este prefijo automáticamente para cada dispositivo conectado. Un driver llamado `myfirst` con unidad 0 conectada obtiene el prefijo `dev.myfirst.0` de forma gratuita, sin que sea necesario ningún código adicional en el driver. La única tarea del driver es poblar el prefijo con OIDs (identificadores de objeto) nombrados para los valores que quiere exponer.

¿Por qué exponer estado a través de sysctl en lugar de ioctl? Los dos mecanismos responden a preguntas diferentes. Ioctl es el canal adecuado para un programa que ya ha abierto el dispositivo y quiere emitir un comando. Sysctl es el canal adecuado para un operador en una shell que quiere inspeccionar o ajustar el estado sin abrir nada. La mayoría de los drivers de producción ofrecen ambas cosas: la interfaz ioctl para los programas y la interfaz sysctl para personas, scripts y herramientas de monitorización.

El patrón habitual es que sysctl exponga:

* **Contadores** que resumen la actividad del driver desde el attach
* **Estado de solo lectura** como números de versión, identificadores de hardware y estado del enlace
* **Variables ajustables de lectura-escritura** como máscaras de depuración, profundidades de cola y valores de timeout
* **Variables ajustables en el momento del boot** que se leen desde `/boot/loader.conf` antes de que el driver se conecte

Al final de esta sección, el driver `myfirst` expondrá las cuatro categorías bajo `dev.myfirst.0` y leerá su máscara de depuración inicial desde `/boot/loader.conf`. El hito del driver para la etapa 3 es la adición de `myfirst_sysctl.c` y un pequeño árbol de OIDs.

### El espacio de nombres de sysctl

Un nombre completo de sysctl tiene el aspecto de una ruta con puntos. El prefijo por defecto de Newbus para nuestro driver es:

```text
dev.myfirst.0
```

Bajo este prefijo el driver puede añadir lo que desee. El driver `myfirst` en la etapa 3 añadirá:

```text
dev.myfirst.0.%desc            "myfirst pseudo-device, integration version 1.7"
dev.myfirst.0.%driver          "myfirst"
dev.myfirst.0.%location        ""
dev.myfirst.0.%pnpinfo         ""
dev.myfirst.0.%parent          "nexus0"
dev.myfirst.0.version          "1.7-integration"
dev.myfirst.0.open_count       0
dev.myfirst.0.total_reads      0
dev.myfirst.0.total_writes     0
dev.myfirst.0.message          "Hello from myfirst"
dev.myfirst.0.debug.mask       0
dev.myfirst.0.debug.classes    "INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) ..."
```

Los primeros cinco nombres (los que empiezan por `%`) los añade Newbus automáticamente y describen la relación en el árbol de dispositivos. Los nombres restantes son la contribución del driver. De estos, `version`, `open_count`, `total_reads`, `total_writes` y `debug.classes` son de solo lectura; `message` y `debug.mask` son de lectura-escritura. El subárbol `debug` es en sí mismo un nodo, lo que significa que puede contener más OIDs a medida que el driver crezca.

Ya puedes ver el resultado en un sistema con `myfirst` cargado:

```console
$ sysctl dev.myfirst.0
dev.myfirst.0.debug.classes: INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) INTR(0x10) DMA(0x20) PWR(0x40) MEM(0x80)
dev.myfirst.0.debug.mask: 0
dev.myfirst.0.message: Hello from myfirst
dev.myfirst.0.total_writes: 0
dev.myfirst.0.total_reads: 0
dev.myfirst.0.open_count: 0
dev.myfirst.0.version: 1.7-integration
dev.myfirst.0.%parent: nexus0
dev.myfirst.0.%pnpinfo:
dev.myfirst.0.%location:
dev.myfirst.0.%driver: myfirst
dev.myfirst.0.%desc: myfirst pseudo-device, integration version 1.7
```

El orden de las líneas es el orden de creación de los OIDs, invertido. (Newbus añade los nombres con prefijo `%` al final, por lo que aparecen primero cuando sysctl recorre la lista en sentido inverso. Esto es cosmético y no tiene ningún significado semántico.)

### OIDs estáticos frente a OIDs dinámicos

Los OIDs de sysctl se presentan en dos variantes.

Un **OID estático** se declara en tiempo de compilación con una de las macros `SYSCTL_*` (`SYSCTL_INT`, `SYSCTL_STRING`, `SYSCTL_ULONG`, etc.). La macro genera una estructura de datos constante que el enlazador pega en una sección especial, y el kernel ensambla esa sección en el árbol global durante el boot. Los OIDs estáticos son adecuados para valores de todo el sistema que existen durante la vida del kernel: tics del temporizador, estadísticas del planificador y similares.

Un **OID dinámico** se crea en tiempo de ejecución con una de las funciones `SYSCTL_ADD_*` (`SYSCTL_ADD_INT`, `SYSCTL_ADD_STRING`, `SYSCTL_ADD_PROC`, etc.). La función recibe un contexto, un nodo padre, un nombre y un puntero a los datos subyacentes, e inserta un nuevo nodo en el árbol. Los OIDs dinámicos son adecuados para valores por instancia que aparecen y desaparecen con un dispositivo: un driver los crea en `attach` y los elimina en `detach`.

El código de los drivers usa OIDs dinámicos casi de forma exclusiva. Un driver no existe en el momento de compilar el kernel; aparece cuando se carga el módulo, y cualquier subárbol de sysctl que le pertenezca debe construirse en el momento del attach y liberarse en el detach. El framework Newbus proporciona a cada driver un contexto de sysctl por dispositivo y un OID padre específicamente para este propósito:

```c
struct sysctl_ctx_list *ctx;
struct sysctl_oid *tree;
struct sysctl_oid_list *child;

ctx = device_get_sysctl_ctx(dev);
tree = device_get_sysctl_tree(dev);
child = SYSCTL_CHILDREN(tree);
```

`device_get_sysctl_ctx` devuelve el contexto por dispositivo. El contexto registra cada OID que crea el driver para que el framework pueda liberarlos todos en una sola llamada cuando el driver se desconecta. El driver no tiene que llevar ese registro por su cuenta.

`device_get_sysctl_tree` devuelve el nodo de árbol por dispositivo, que es el OID correspondiente a `dev.<driver>.<unit>`. Newbus creó ese árbol cuando se añadió el dispositivo.

`SYSCTL_CHILDREN(tree)` extrae la lista de hijos del nodo de árbol. Esto es lo que el driver pasa como argumento padre en las llamadas posteriores a `SYSCTL_ADD_*`.

Con estas tres referencias a mano, el driver puede añadir cualquier número de OIDs a su subárbol:

```c
SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "open_count",
    CTLFLAG_RD, &sc->sc_open_count, 0,
    "Number of times the device has been opened");
```

La llamada a `SYSCTL_ADD_UINT` añade un OID de entero sin signo bajo el padre con el nombre `open_count`, con `CTLFLAG_RD` (solo lectura), respaldado por `&sc->sc_open_count`, sin valor inicial especial y con una descripción. La descripción es lo que `sysctl -d dev.myfirst.0.open_count` imprimirá. Escribe siempre una descripción útil; una vacía es un agujero de documentación.

La llamada equivalente para un entero de lectura-escritura es idéntica salvo por el flag:

```c
SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "debug_mask_simple",
    CTLFLAG_RW, &sc->sc_debug, 0,
    "Simple writable debug mask");
```

El flag `CTLFLAG_RW` le indica al kernel que permita escrituras de usuarios privilegiados (root o procesos con `PRIV_DRIVER`).

Para una cadena, la macro es `SYSCTL_ADD_STRING`:

```c
SYSCTL_ADD_STRING(ctx, child, OID_AUTO, "version",
    CTLFLAG_RD, sc->sc_version, 0,
    "Driver version string");
```

El antepenúltimo argumento es un puntero al buffer que contiene la cadena y el penúltimo es el tamaño del buffer (cero significa ilimitado para cadenas de solo lectura).

### OIDs respaldados por un manejador

Algunos OIDs necesitan más lógica que un simple acceso a memoria. Leer el OID puede requerir calcular el valor a partir de varios campos del softc; escribir en él puede requerir validar el nuevo valor y actualizar el estado relacionado. Estos OIDs usan una función manejadora y la macro `SYSCTL_ADD_PROC`.

Un manejador tiene la siguiente firma:

```c
static int handler(SYSCTL_HANDLER_ARGS);
```

`SYSCTL_HANDLER_ARGS` es una macro que se expande a:

```c
struct sysctl_oid *oidp, void *arg1, intptr_t arg2,
struct sysctl_req *req
```

`oidp` identifica el OID al que se está accediendo. `arg1` y `arg2` son los argumentos suministrados por el usuario cuando se registró el OID (normalmente `arg1` apunta al softc y `arg2` no se usa o contiene una pequeña constante). `req` lleva el contexto de lectura/escritura: `req->newptr` no es NULL para una escritura (y apunta al nuevo valor que está proporcionando el usuario), y el manejador debe llamar a `SYSCTL_OUT(req, value, sizeof(value))` para devolver un valor en una lectura.

Un manejador típico que expone un valor calculado:

```c
static int
myfirst_sysctl_message_len(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        u_int len;

        mtx_lock(&sc->sc_mtx);
        len = (u_int)sc->sc_msglen;
        mtx_unlock(&sc->sc_mtx);

        return (sysctl_handle_int(oidp, &len, 0, req));
}
```

El manejador calcula el valor (aquí, copiando la longitud del mensaje bajo el mutex) y después delega en `sysctl_handle_int`, que gestiona el protocolo de lectura o escritura y (en el caso de una escritura) vuelve a llamar al manejador con el nuevo valor ya en `*ptr`. El patrón de manejador sobre manejador es idiomático; usarlo correctamente evita tener que reimplementar copyin y copyout para cada manejador tipado.

El manejador se registra con `SYSCTL_ADD_PROC`:

```c
SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "message_len",
    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
    sc, 0, myfirst_sysctl_message_len, "IU",
    "Current length of the in-driver message");
```

Tres argumentos merecen atención especial. `CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE` es la palabra de tipo y flags. `CTLTYPE_UINT` declara el tipo externo del OID (unsigned int); `CTLFLAG_RD` lo declara de solo lectura; `CTLFLAG_MPSAFE` declara que el handler es seguro para ser invocado sin el giant lock. El flag `CTLFLAG_MPSAFE` es obligatorio en el código nuevo; sin él, el kernel sigue funcionando pero adquiere el giant lock en cada lectura, lo que serializa todo el sistema en cualquier acceso a sysctl.

El séptimo argumento es la cadena de formato. `"IU"` declara un unsigned int (`I` para integer, `U` para unsigned). El conjunto completo está documentado en `/usr/src/sys/sys/sysctl.h`: `"I"` para int, `"IU"` para uint, `"L"` para long, `"LU"` para ulong, `"Q"` para int64, `"QU"` para uint64, `"A"` para string y `"S,structname"` para una struct opaca. El comando `sysctl(8)` utiliza la cadena de formato para decidir cómo imprimir el valor cuando se invoca sin `-x` (el flag de hex en bruto).

### El árbol sysctl de `myfirst`

El árbol sysctl completo de la etapa 3 se construye en una sola función, `myfirst_sysctl_attach`, llamada desde `myfirst_attach` tras crear el cdev. La función es suficientemente corta como para leerla de principio a fin:

```c
/*
 * myfirst_sysctl.c - sysctl tree for the myfirst driver.
 *
 * Builds dev.myfirst.<unit>.* with version, counters, message, and a
 * debug subtree (debug.mask, debug.classes).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>

#include "myfirst.h"
#include "myfirst_debug.h"

#define MYFIRST_VERSION "1.7-integration"

static int
myfirst_sysctl_message_len(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        u_int len;

        mtx_lock(&sc->sc_mtx);
        len = (u_int)sc->sc_msglen;
        mtx_unlock(&sc->sc_mtx);

        return (sysctl_handle_int(oidp, &len, 0, req));
}

void
myfirst_sysctl_attach(struct myfirst_softc *sc)
{
        device_t dev = sc->sc_dev;
        struct sysctl_ctx_list *ctx;
        struct sysctl_oid *tree;
        struct sysctl_oid_list *child;
        struct sysctl_oid *debug_node;
        struct sysctl_oid_list *debug_child;

        ctx = device_get_sysctl_ctx(dev);
        tree = device_get_sysctl_tree(dev);
        child = SYSCTL_CHILDREN(tree);

        /* Read-only: driver version. */
        SYSCTL_ADD_STRING(ctx, child, OID_AUTO, "version",
            CTLFLAG_RD, MYFIRST_VERSION, 0,
            "Driver version string");

        /* Read-only: counters. */
        SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "open_count",
            CTLFLAG_RD, &sc->sc_open_count, 0,
            "Number of currently open file descriptors");

        SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "total_reads",
            CTLFLAG_RD, &sc->sc_total_reads, 0,
            "Total read() calls since attach");

        SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "total_writes",
            CTLFLAG_RD, &sc->sc_total_writes, 0,
            "Total write() calls since attach");

        /* Read-only: message buffer (no copy through user) */
        SYSCTL_ADD_STRING(ctx, child, OID_AUTO, "message",
            CTLFLAG_RD, sc->sc_msg, sizeof(sc->sc_msg),
            "Current in-driver message");

        /* Read-only handler: message length, computed. */
        SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "message_len",
            CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
            sc, 0, myfirst_sysctl_message_len, "IU",
            "Current length of the in-driver message in bytes");

        /* Subtree: debug.* */
        debug_node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "debug",
            CTLFLAG_RD | CTLFLAG_MPSAFE, NULL,
            "Debug controls and class enumeration");
        debug_child = SYSCTL_CHILDREN(debug_node);

        SYSCTL_ADD_UINT(ctx, debug_child, OID_AUTO, "mask",
            CTLFLAG_RW | CTLFLAG_TUN, &sc->sc_debug, 0,
            "Bitmask of enabled debug classes");

        SYSCTL_ADD_STRING(ctx, debug_child, OID_AUTO, "classes",
            CTLFLAG_RD,
            "INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) "
            "INTR(0x10) DMA(0x20) PWR(0x40) MEM(0x80)",
            0, "Names and bit values of debug classes");
}
```

Tres detalles merecen una atención más detallada.

El OID `version` está respaldado por una constante de cadena (`MYFIRST_VERSION`), no por un campo del softc. Un OID de cadena de solo lectura puede apuntar a cualquier buffer estable; el kernel nunca escribe a través del puntero. Esto es más seguro y sencillo que mantener una copia por softc de la versión, y permite que la versión sea visible a través de `sysctl` incluso si el driver falla durante el attach a mitad del proceso.

El OID `message` apunta directamente al campo `sc_msg` del softc con `CTLFLAG_RD`. Un lector que llame a `sysctl dev.myfirst.0.message` obtendrá el valor actual. Como el OID es de solo lectura, sysctl no escribirá en el buffer, de modo que no necesitamos un handler de escritura. (Una versión de lectura-escritura de este OID necesitaría un handler para validar la entrada; la ruta de lectura-escritura pasa por la interfaz ioctl de la etapa 2.)

El OID `debug.mask` tiene `CTLFLAG_RW | CTLFLAG_TUN`. El flag `RW` permite escrituras por parte de un usuario privilegiado. El flag `TUN` indica al kernel que busque un tunable coincidente en `/boot/loader.conf` y lo aplique antes de que el OID sea accesible. (Configuraremos el hook de loader.conf en la siguiente subsección.)

### Integración del sysctl en attach y detach

La ruta de attach ahora llama al constructor del sysctl tras crear el cdev:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        struct make_dev_args args;
        int error;

        sc->sc_dev = dev;
        mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);
        strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
        sc->sc_msglen = strlen(sc->sc_msg);

        make_dev_args_init(&args);
        args.mda_devsw = &myfirst_cdevsw;
        args.mda_uid = UID_ROOT;
        args.mda_gid = GID_WHEEL;
        args.mda_mode = 0660;
        args.mda_si_drv1 = sc;
        args.mda_unit = device_get_unit(dev);

        error = make_dev_s(&args, &sc->sc_cdev,
            "myfirst%d", device_get_unit(dev));
        if (error != 0) {
                mtx_destroy(&sc->sc_mtx);
                return (error);
        }

        myfirst_sysctl_attach(sc);

        DPRINTF(sc, MYF_DBG_INIT, "attach: cdev created and sysctl tree built\n");
        return (0);
}
```

La ruta de detach no cambia: no necesita llamar a `myfirst_sysctl_detach`. El framework Newbus es propietario del contexto sysctl por dispositivo y lo destruye automáticamente cuando el dispositivo hace detach. El driver solo necesita limpiar los recursos que asignó fuera del framework (el cdev y el mutex). Esta es una de las razones pequeñas pero reales para preferir el contexto por dispositivo sobre un contexto privado.

### Tunables en tiempo de boot mediante `/boot/loader.conf`

Un driver puede permitir que el operador configure su comportamiento inicial en tiempo de boot leyendo valores del entorno del loader. El loader (`/boot/loader.efi` o `/boot/loader`) analiza `/boot/loader.conf` antes de que el kernel arranque y exporta las variables a un pequeño entorno que el kernel puede consultar.

La forma más sencilla de leer una variable del loader es `TUNABLE_INT_FETCH`:

```c
TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default", &sc->sc_debug);
```

El primer argumento es el nombre de la variable del loader. El segundo es un puntero al destino, que actúa también como valor por defecto si la variable está ausente. La llamada no hace nada si la variable está ausente y escribe el valor analizado en caso contrario.

La llamada se coloca en `myfirst_attach` antes de `myfirst_sysctl_attach`. En el momento en que se construye el árbol sysctl, `sc->sc_debug` ya tiene el valor proporcionado por el loader (o el valor por defecto en tiempo de compilación), y el OID `dev.myfirst.0.debug.mask` lo refleja.

Una entrada representativa de `/boot/loader.conf` para el driver tiene este aspecto:

```ini
myfirst_load="YES"
hw.myfirst.debug_mask_default="0x06"
```

La primera línea indica al loader que cargue `myfirst.ko` automáticamente. La segunda establece la máscara de depuración por defecto en `MYF_DBG_OPEN | MYF_DBG_IO`. Tras el boot, `sysctl dev.myfirst.0.debug.mask` informa el valor `6` y el operador puede modificarlo en tiempo de ejecución sin necesidad de reiniciar.

La convención de nombres es flexible, pero sigue algunas prácticas recomendadas. Mantén las variables del loader bajo `hw.<driver>.<knob>` porque el espacio de nombres `hw.` es convencionalmente de solo lectura en tiempo de ejecución y no está sujeto a cambios de nombre inesperados. Usa `default` en el nombre de la variable cuando el valor sea el valor inicial de un OID modificable en tiempo de ejecución, para dejar clara la relación. Documenta cada variable del loader en la página de manual del driver o en la tarjeta de referencia del capítulo (el capítulo tiene una al final).

### Combinación del sysctl con la máscara de depuración

El lector recordará del Capítulo 23 que el driver ya tiene un campo `sc->sc_debug` y una macro `DPRINTF` que lo consulta. Con la etapa 3 en su lugar, el operador puede ahora manipular la máscara desde el shell:

```console
$ sysctl dev.myfirst.0.debug.mask
dev.myfirst.0.debug.mask: 0
$ sysctl dev.myfirst.0.debug.classes
dev.myfirst.0.debug.classes: INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) ...
$ sudo sysctl dev.myfirst.0.debug.mask=0xff
dev.myfirst.0.debug.mask: 0 -> 255
$ # now every DPRINTF call inside the driver will print
```

El OID `classes` existe precisamente para ahorrar al operador la necesidad de memorizar los valores de bits. `sysctl` imprime ambos nombres juntos y el operador puede copiar un valor hexadecimal de la pantalla y pegarlo en el siguiente comando.

El mismo mecanismo se extiende a cualquier otro parámetro que el driver quiera exponer. Un driver que tenga un timeout ajustable añadiría:

```c
SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "timeout_ms",
    CTLFLAG_RW | CTLFLAG_TUN, &sc->sc_timeout_ms, 0,
    "Operation timeout in milliseconds");
```

Un driver que quiera habilitar o deshabilitar una característica por instancia añadiría un `bool` (declarado con `SYSCTL_ADD_BOOL`, que es el tipo moderno preferido para flags booleanos) o un int con dos valores válidos (0 y 1).

### Compilación del driver de la etapa 3

El `Makefile` para la etapa 3 lista el nuevo archivo fuente:

```make
KMOD=   myfirst
SRCS=   myfirst.c myfirst_debug.c myfirst_ioctl.c myfirst_sysctl.c

CFLAGS+= -I${.CURDIR}

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
```

Tras ejecutar `make` y `kldload`, el operador puede recorrer inmediatamente el árbol:

```console
$ sudo kldload ./myfirst.ko
$ sysctl -a dev.myfirst.0
dev.myfirst.0.debug.classes: INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) ...
dev.myfirst.0.debug.mask: 0
dev.myfirst.0.message_len: 18
dev.myfirst.0.message: Hello from myfirst
dev.myfirst.0.total_writes: 0
dev.myfirst.0.total_reads: 0
dev.myfirst.0.open_count: 0
dev.myfirst.0.version: 1.7-integration
dev.myfirst.0.%parent: nexus0
dev.myfirst.0.%pnpinfo:
dev.myfirst.0.%location:
dev.myfirst.0.%driver: myfirst
dev.myfirst.0.%desc: myfirst pseudo-device, integration version 1.7
```

Abrir y leer el dispositivo debería incrementar los contadores de inmediato:

```console
$ cat /dev/myfirst0
Hello from myfirst
$ sysctl dev.myfirst.0.total_reads
dev.myfirst.0.total_reads: 1
$ sysctl dev.myfirst.0.open_count
dev.myfirst.0.open_count: 0
```

El contador `open_count` muestra cero porque `cat` abre el dispositivo, lee y cierra inmediatamente; en el momento en que `sysctl` se ejecuta, el contador ha vuelto a cero. Para ver un valor distinto de cero, mantén el dispositivo abierto en otro terminal:

```console
# terminal 1
$ exec 3< /dev/myfirst0

# terminal 2
$ sysctl dev.myfirst.0.open_count
dev.myfirst.0.open_count: 1

# terminal 1
$ exec 3<&-

# terminal 2
$ sysctl dev.myfirst.0.open_count
dev.myfirst.0.open_count: 0
```

El comando `exec 3< /dev/myfirst0` del shell abre el dispositivo en el descriptor de archivo 3 y lo mantiene abierto hasta que `exec 3<&-` lo cierra. Es una técnica útil para inspeccionar la métrica de recuento de aperturas de cualquier driver sin necesidad de escribir un programa.

### Escollos habituales con sysctl

El primer escollo es **olvidar `CTLFLAG_MPSAFE`**. Sin el flag, el kernel toma el giant lock alrededor del handler del OID. Para un entero de solo lectura esto es inofensivo; para un OID con acceso frecuente serializa el kernel entero y supone un desastre de latencia. El código moderno del kernel usa `CTLFLAG_MPSAFE` en todas partes; la ausencia del flag es una señal de que el código es anterior al paso al locking de grano fino y de que debería revisarse también por corrección.

El segundo escollo es **usar un OID estático en el código del driver**. Las macros `SYSCTL_INT` y `SYSCTL_STRING` (sin el prefijo `_ADD_`) declaran OIDs estáticos y los colocan en una sección especial del enlazador que se procesa durante el boot del kernel. Un módulo cargable que use estas macros instalará los OIDs cuando el módulo cargue, pero los OIDs referenciarán campos por instancia que no existían en tiempo de compilación, provocando panics en el momento en que el operador los lea. La solución es usar la familia `SYSCTL_ADD_*` para todos los OIDs del driver.

El tercer escollo es **dejar escapar el contexto por driver**. Un driver que use su propio `sysctl_ctx_init` y `sysctl_ctx_free` (en lugar del contexto por dispositivo devuelto por `device_get_sysctl_ctx`) debe recordar llamar a `sysctl_ctx_free` en detach. Olvidarlo provoca una fuga de todos los OIDs que el driver creó y un panic del kernel la próxima vez que el operador lea alguno. La solución es usar el contexto por dispositivo (que el framework limpia automáticamente) siempre que sea posible.

El cuarto escollo es **colocar estado por instancia en un OID compartido**. Un driver que quiera un tunable compartido entre todas sus instancias podría tener la tentación de colocarlo bajo `kern.myfirst.foo` o bajo `dev.myfirst.foo`. Lo segundo parece inocente pero falla: cuando la segunda instancia hace attach, Newbus intenta crear `dev.myfirst.0.foo` y `dev.myfirst.1.foo`, y el `dev.myfirst.foo` existente (sin el número de unidad) ya no está en ámbito. La solución es usar `hw.myfirst.<knob>` para tunables compartidos o OIDs por instancia para estado por instancia, pero no ambos con el mismo nombre.

El quinto escollo es **cambiar el tipo de un OID**. Un OID declarado como `CTLTYPE_UINT` no puede cambiar de tipo sin invalidar cualquier programa en espacio de usuario que haya llamado a `sysctlbyname` con él. El kernel devuelve `EINVAL` si el usuario pasa un buffer de tamaño incorrecto. La solución es mantener el tipo estable entre versiones; si se necesita un tipo diferente, define un nuevo nombre de OID y depreca el antiguo.

### Cerrando la sección 4

La sección 4 añadió la tercera superficie de integración: el árbol sysctl bajo `dev.myfirst.0`. El driver ahora expone su versión, contadores, mensaje actual, máscara de depuración y enumeración de clases, todo con texto de ayuda descriptivo y todo construido con el contexto sysctl por dispositivo proporcionado por Newbus. La máscara de depuración puede establecerse en tiempo de boot mediante `/boot/loader.conf` y ajustarse en tiempo de ejecución mediante `sysctl(8)`. Un breve bloque en attach construye el árbol completo; detach no hace nada porque el framework limpia automáticamente.

El hito del driver en la etapa 3 es la incorporación de `myfirst_sysctl.c` y una pequeña extensión de `myfirst_attach`. El cdevsw, el dispatcher de ioctl, la infraestructura de depuración y el resto del driver no cambian. El árbol sysctl es puramente aditivo.

En la sección 5 analizamos un objetivo de integración opcional pero ilustrativo: la pila de red. La mayoría de los drivers nunca se convertirán en drivers de red, pero entender cómo un driver registra un `ifnet` y participa en la API `if_*` ofrece al lector un ejemplo del patrón que el kernel usa para cada «subsistema con una interfaz de registro». Si el driver no es un driver de red, el lector puede leer la sección 5 por contexto y saltar directamente a la sección 7.

## Sección 5: Integración con la red (opcional)

### Por qué esta sección es opcional

El driver `myfirst` no es un driver de red y no se convertirá en uno en este capítulo. Las interfaces cdevsw, ioctl y sysctl que hemos construido son suficientes para él. El lector que esté siguiendo el capítulo para integrar un driver que no es de red puede saltar sin riesgo a la sección 7 sin perder nada esencial.

Sin embargo, la integración con la red es una ilustración perfecta de un principio más general: muchos subsistemas de FreeBSD ofrecen una interfaz de registro que convierte un driver en participante de un framework más amplio. El patrón es el mismo tanto si el framework es de red, almacenamiento, USB o sonido: el driver asigna un objeto definido por el framework, rellena los callbacks, llama a una función de registro y, a partir de ese momento, recibe callbacks del framework. Leer esta sección, aunque no vayas a escribir un driver de red, desarrolla la intuición para todas las demás integraciones con frameworks del libro.

El capítulo usa la pila de red como ejemplo por dos razones. En primer lugar, es el framework más ampliamente conocido, por lo que el vocabulario (`ifnet`, `if_attach`, `bpf`) conecta con comandos visibles para el usuario como `ifconfig(8)` y `tcpdump(8)`. En segundo lugar, la interfaz de registro de red es lo suficientemente pequeña como para recorrerla de principio a fin sin perder al lector. La sección 6 muestra luego el mismo patrón aplicado a la pila de almacenamiento CAM.

### Qué es un `ifnet`

`ifnet` es el objeto por interfaz de la pila de red. Es el equivalente en red al `cdev` con el que trabajamos en la sección 2. Así como un `cdev` representa un nodo de dispositivo bajo `/dev`, un `ifnet` representa una interfaz de red en `ifconfig`. Cada línea de `ifconfig -a` corresponde a un `ifnet`.

El `ifnet` es opaco desde fuera de la pila de red. Los drivers lo ven a través del typedef `if_t` y lo manipulan mediante funciones de acceso (`if_setflags`, `if_getmtu`, `if_settransmitfn`). La opacidad es deliberada: permite que la pila de red evolucione los internos de `ifnet` sin romper todos los drivers en cada versión. Los drivers nuevos deben usar exclusivamente la API `if_t`.

El ciclo de vida de un `ifnet` en un driver es el siguiente:

1. **asignar** con `if_alloc(IFT_<type>)`
2. **nombrar** con `if_initname(ifp, "myif", unit)`
3. **rellenar los callbacks** de ioctl, transmit, init y otros similares
4. **hacer attach** con `if_attach(ifp)`, que hace visible la interfaz
5. **conectar a BPF** con `bpfattach(ifp, ...)` para que `tcpdump` pueda ver el tráfico
6. ... la interfaz vive, recibe tráfico y ejecuta ioctls ...
7. **desconectar de BPF** con `bpfdetach(ifp)`
8. **hacer detach** con `if_detach(ifp)`, que la elimina de la lista visible
9. **liberar** con `if_free(ifp)`

El ciclo de vida refleja casi exactamente el ciclo de vida del cdev (asignar, nombrar, attach, detach, liberar), lo cual no es una coincidencia; tanto la pila de red como devfs evolucionaron a partir del mismo patrón de interfaz de registro.

### Un recorrido usando `disc(4)`

El ejemplo más sencillo de un driver de `ifnet` incluido en el árbol de código fuente es `disc(4)`, la interfaz de descarte. `disc(4)` acepta paquetes y los descarta silenciosamente; su código es, por tanto, casi todo andamiaje de integración, sin lógica de protocolo que distraiga al lector. El driver completo se encuentra en `/usr/src/sys/net/if_disc.c`.

La función relevante es `disc_clone_create`, que se invoca cada vez que el operador ejecuta `ifconfig disc create`:

```c
static int
disc_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
        struct ifnet     *ifp;
        struct disc_softc *sc;

        sc = malloc(sizeof(struct disc_softc), M_DISC, M_WAITOK | M_ZERO);
        ifp = sc->sc_ifp = if_alloc(IFT_LOOP);
        ifp->if_softc = sc;
        if_initname(ifp, discname, unit);
        ifp->if_mtu = DSMTU;
        ifp->if_flags = IFF_LOOPBACK | IFF_MULTICAST;
        ifp->if_drv_flags = IFF_DRV_RUNNING;
        ifp->if_ioctl = discioctl;
        ifp->if_output = discoutput;
        ifp->if_hdrlen = 0;
        ifp->if_addrlen = 0;
        ifp->if_snd.ifq_maxlen = 20;
        if_attach(ifp);
        bpfattach(ifp, DLT_NULL, sizeof(u_int32_t));

        return (0);
}
```

Paso a paso:

`malloc` asigna el softc del driver con `M_WAITOK | M_ZERO`. El flag waitok está permitido porque clone-create se ejecuta en un contexto suspendible. El flag zero inicializa la estructura a cero, lo que permite al driver asumir que cualquier campo que no establezca explícitamente es cero o NULL.

`if_alloc(IFT_LOOP)` asigna el `ifnet` desde el pool de la pila de red. El argumento `IFT_LOOP` identifica el tipo de interfaz, que la pila utiliza para los informes de estilo SNMP y para algunos comportamientos predeterminados. Otros tipos habituales son `IFT_ETHER` (para drivers Ethernet) y `IFT_TUNNEL` (para pseudodispositivos de túnel).

`if_initname` establece el nombre visible para el usuario. `discname` es la cadena `"disc"`, y `unit` es el número de unidad que pasa el mecanismo de clonación. Juntos forman `disc0`, `disc1`, etcétera.

Las siguientes líneas rellenan los callbacks y los datos por interfaz: el MTU, los flags, el manejador ioctl (`discioctl`), la función de salida (`discoutput`), la longitud máxima de la cola de envío, etcétera. Es el equivalente en red de la tabla `cdevsw` de la Sección 2; la diferencia es que se rellena en un objeto por interfaz en lugar de en una tabla estática.

`if_attach(ifp)` hace la interfaz visible en el espacio de usuario. Una vez que esta llamada retorna, `ifconfig disc0` funciona, la interfaz aparece en `netstat -i` y los protocolos pueden enlazarse a ella.

`bpfattach(ifp, DLT_NULL, ...)` conecta la interfaz al mecanismo BPF (Berkeley Packet Filter), que es lo que lee `tcpdump`. `DLT_NULL` declara el tipo de capa de enlace como "sin capa de enlace", lo apropiado para un loopback. Un driver Ethernet llamaría a `bpfattach(ifp, DLT_EN10MB, ETHER_HDR_LEN)`. Sin `bpfattach`, `tcpdump` no puede ver el tráfico de la interfaz, aunque la interfaz en sí funcione.

La ruta de destrucción es el reflejo de la ruta de creación, en orden inverso:

```c
static void
disc_clone_destroy(struct ifnet *ifp)
{
        struct disc_softc *sc;

        sc = ifp->if_softc;

        bpfdetach(ifp);
        if_detach(ifp);
        if_free(ifp);

        free(sc, M_DISC);
}
```

`bpfdetach` primero, porque `tcpdump` puede tener una referencia. `if_detach` a continuación, porque la pila de red puede seguir encolando tráfico hacia la interfaz; `if_detach` vacía la cola y elimina la interfaz de la lista visible. `if_free` por último, porque el `ifnet` puede seguir siendo referenciado por sockets que las capas superiores aún no han terminado de limpiar; `if_free` aplaza la liberación real hasta que desaparezca la última referencia.

El driver `disc(4)` tiene unas 200 líneas. Un driver Ethernet real se acerca a las 5000, pero el código de integración estándar (allocate, initname, attach, bpfattach, detach, bpfdetach, free) es idéntico. Las 4800 líneas adicionales son detalles específicos del protocolo: anillos de descriptores, manejadores de interrupciones, gestión de direcciones MAC, filtros multicast, estadísticas, sondeo del estado del enlace, etcétera. Cada uno tiene su propio patrón, y el Capítulo 28 los cubre en detalle. El marco de integración que se ha visto aquí es la base de todos ellos.

### Cómo ve el resultado un operador

Una vez que `disc_clone_create` devuelve con éxito, el operador puede manipular la interfaz desde la shell:

```console
$ sudo ifconfig disc create
$ ifconfig disc0
disc0: flags=8049<UP,LOOPBACK,RUNNING,MULTICAST> metric 0 mtu 1500
$ sudo ifconfig disc0 inet 169.254.99.99/32
$ sudo tcpdump -i disc0 &
$ ping -c1 169.254.99.99
... ping output ...
$ sudo ifconfig disc destroy
```

Cada uno de estos comandos afecta a una parte diferente de la integración:

* `ifconfig disc create` llama a `disc_clone_create`, que construye el `ifnet` y lo conecta.
* `ifconfig disc0` lee los flags y el MTU del `ifnet` a través de los accesores `if_t`.
* `ifconfig disc0 inet 169.254.99.99/32` llama a `discioctl` con `SIOCAIFADDR`, el ioctl que añade una dirección.
* `tcpdump -i disc0` abre el tap de BPF que creó `bpfattach`.
* `ping -c1` envía un paquete que se enruta a través de `discoutput`, se descarta y nunca regresa.
* `ifconfig disc destroy` llama a `disc_clone_destroy`, que desconecta y libera.

Toda la integración es visible en el nivel del espacio de usuario. No fue necesario modificar ninguna parte de la maquinaria de protocolo subyacente para acomodar el nuevo driver; el framework de la pila de red ya tenía un hueco para él.

### A qué otros casos se generaliza este patrón

El mismo patrón de registro se aplica a muchos otros subsistemas:

* La **pila de sonido** (`sys/dev/sound`) usa `pcm_register` y `pcm_unregister` para hacer visible un dispositivo de sonido. El driver rellena callbacks para la reproducción de buffers, el acceso al mezclador y la configuración de canales.
* La **pila USB** (`sys/dev/usb`) usa `usb_attach` y `usb_detach` para registrar drivers de dispositivos USB. El driver rellena callbacks para la configuración de transferencias, las peticiones de control y la desconexión.
* El **framework de I/O GEOM** (`sys/geom`) usa `g_attach` y `g_detach` para registrar proveedores y consumidores de almacenamiento. El driver rellena callbacks para el inicio de I/O, la finalización y el orphaning.
* El **framework CAM SIM** (`sys/cam`) usa `cam_sim_alloc` y `xpt_bus_register` para registrar adaptadores de almacenamiento. La sección 6 lo recorre con más detalle.
* El **sistema de despacho de métodos kobj** (que ya hemos visto detrás de `device_method_t`) es en sí mismo un framework de registro: el driver declara una tabla de métodos y el subsistema kobj despacha las llamadas a través de ella.

En todos los casos los pasos son los mismos: asignar el objeto del framework, rellenar los callbacks, llamar a la función de registro, recibir tráfico y desregistrarse limpiamente. El vocabulario cambia, pero el ritmo no.

### Cerrando la sección 5

La sección 5 utilizó la pila de red para ilustrar una integración de estilo registro. El driver asigna un `ifnet`, le pone nombre, rellena los callbacks, lo conecta a la pila, lo conecta a BPF, recibe tráfico y desmonta en orden inverso al destruirse. El patrón es pequeño y bien delimitado; la maquinaria de protocolo se encuentra detrás y es el tema del capítulo 27.

El lector que no esté escribiendo un driver de red obtiene un beneficio útil de esta sección incluso sin aplicarla a `myfirst`: todas las demás integraciones de estilo registro en el kernel de FreeBSD siguen la misma forma. Una vez interiorizado el ritmo de asignar-nombrar-rellenar-conectar-recibir-desconectar-liberar, la pila de almacenamiento de la sección 6 resultará familiar a primera vista.

En la sección 6 aplicamos la misma perspectiva a la pila de almacenamiento CAM. El vocabulario cambia (`cam_sim`, `xpt_bus_register`, `xpt_action`, CCBs), pero la forma de registro es la misma.

## Sección 6: Integración con el almacenamiento CAM (opcional)

### Por qué esta sección es opcional

`myfirst` no es un adaptador de almacenamiento y no llegará a serlo. El lector que esté integrando un driver que no sea de almacenamiento debería ojear esta sección en busca de vocabulario, tomar nota de la forma de registro que refleja la sección 5, y continuar con la sección 7.

El lector que esté integrando un adaptador de almacenamiento (un adaptador de bus host SCSI, un controlador NVMe, un controlador de almacenamiento virtual emulado) encontrará aquí los fundamentos básicos de cómo CAM espera que un driver se comunique con él. La superficie de protocolo completa es lo suficientemente amplia como para llenar un capítulo entero y es el tema del capítulo 27; lo que cubrimos aquí es solo el encuadre de la integración, idéntico en espíritu al encuadre de `if_alloc` / `if_attach` que usamos para las redes.

### Qué es CAM

CAM (Common Access Method) es el subsistema de almacenamiento de FreeBSD situado por encima de la capa del driver de dispositivo. Es el responsable de gestionar la cola de peticiones de I/O pendientes, la noción abstracta de un target y un número de unidad lógica (LUN), la lógica de enrutamiento de paths que envía una petición al adaptador correcto, y el conjunto de drivers periféricos genéricos (`da(4)` para discos, `cd(4)` para ópticos, `sa(4)` para cintas) que convierten el I/O de bloques en comandos específicos del protocolo. El driver se sitúa por debajo de CAM y es responsable únicamente del trabajo específico del adaptador: enviar comandos al hardware y notificar las finalizaciones.

El vocabulario que usa CAM es pequeño pero específico:

* Un **SIM** (SCSI Interface Module) es la visión que tiene el framework de un adaptador de almacenamiento. El driver asigna uno con `cam_sim_alloc`, rellena un callback (la función de acción) y lo registra con `xpt_bus_register`. El SIM es el análogo del `ifnet` para la pila de almacenamiento.
* Un **CCB** (CAM Control Block) es una única petición de I/O. CAM entrega un CCB al driver a través del callback de acción; el driver inspecciona el `func_code` del CCB, realiza la acción solicitada, rellena el resultado y devuelve el CCB a CAM con `xpt_done`. Los CCBs son el análogo de los `mbuf`s para la pila de almacenamiento, con la diferencia de que un CCB lleva tanto la petición como la respuesta.
* Un **path** identifica un destino como una tripleta `(bus, target, LUN)`. El driver llama a `xpt_create_path` para construir un path que pueda usar para eventos asíncronos.
* El **XPT** (Transport Layer) es el mecanismo central de despacho de CAM. El driver llama a `xpt_action` para enviar un CCB a CAM (o a sí mismo, en el caso de acciones dirigidas al propio driver); CAM eventualmente llama de vuelta a la función de acción del driver para los CCBs de I/O dirigidos al bus del driver.

### El ciclo de vida del registro

Para un adaptador de un solo canal, los pasos de registro son:

1. Asignar una cola de dispositivos CAM con `cam_simq_alloc(maxq)`.
2. Asignar un SIM con `cam_sim_alloc(action, poll, "name", softc, unit, mtx, max_tagged, max_dev_transactions, devq)`.
3. Bloquear el mutex del driver.
4. Registrar el SIM con `xpt_bus_register(sim, dev, 0)`.
5. Crear un path que el driver pueda usar para eventos: `xpt_create_path(&path, NULL, cam_sim_path(sim), CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD)`.
6. Desbloquear el mutex.

La limpieza se ejecuta en orden inverso:

1. Bloquear el mutex del driver.
2. Liberar el path con `xpt_free_path(path)`.
3. Desregistrar el SIM con `xpt_bus_deregister(cam_sim_path(sim))`.
4. Liberar el SIM con `cam_sim_free(sim, TRUE)`. El argumento `TRUE` le indica a CAM que también libere la devq subyacente; pasa `FALSE` si el driver quiere conservar la devq para reutilizarla.
5. Desbloquear el mutex.

El driver `ahci(4)` en `/usr/src/sys/dev/ahci/ahci.c` es un buen ejemplo del mundo real. Su camino de conexión de canal incluye la secuencia canónica:

```c
ch->sim = cam_sim_alloc(ahciaction, ahcipoll, "ahcich", ch,
    device_get_unit(dev), (struct mtx *)&ch->mtx,
    (ch->quirks & AHCI_Q_NOCCS) ? 1 : min(2, ch->numslots),
    (ch->caps & AHCI_CAP_SNCQ) ? ch->numslots : 0,
    devq);
if (ch->sim == NULL) {
        cam_simq_free(devq);
        device_printf(dev, "unable to allocate sim\n");
        error = ENOMEM;
        goto err1;
}
if (xpt_bus_register(ch->sim, dev, 0) != CAM_SUCCESS) {
        device_printf(dev, "unable to register xpt bus\n");
        error = ENXIO;
        goto err2;
}
if (xpt_create_path(&ch->path, NULL, cam_sim_path(ch->sim),
    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
        device_printf(dev, "unable to create path\n");
        error = ENXIO;
        goto err3;
}
```

Las etiquetas `goto` (`err1`, `err2`, `err3`) confluyen en una sección de limpieza única que deshace todo lo que se ha asignado hasta ese momento. Este es el patrón estándar del driver FreeBSD para el manejo de fallos, y es exactamente la disciplina que la sección 7 codificará.

### El callback de acción

El callback de acción es el núcleo de un driver CAM. Su signatura es `void action(struct cam_sim *sim, union ccb *ccb)`. El driver inspecciona `ccb->ccb_h.func_code` y despacha:

```c
static void
mydriver_action(struct cam_sim *sim, union ccb *ccb)
{
        struct mydriver_softc *sc;

        sc = cam_sim_softc(sim);

        switch (ccb->ccb_h.func_code) {
        case XPT_SCSI_IO:
                mydriver_start_io(sc, ccb);
                /* completion is asynchronous; xpt_done called later */
                return;

        case XPT_RESET_BUS:
                mydriver_reset_bus(sc);
                ccb->ccb_h.status = CAM_REQ_CMP;
                break;

        case XPT_PATH_INQ: {
                struct ccb_pathinq *cpi = &ccb->cpi;

                cpi->version_num = 1;
                cpi->hba_inquiry = PI_SDTR_ABLE | PI_TAG_ABLE;
                cpi->target_sprt = 0;
                cpi->hba_misc = PIM_NOBUSRESET | PIM_SEQSCAN;
                cpi->hba_eng_cnt = 0;
                cpi->max_target = 0;
                cpi->max_lun = 7;
                cpi->initiator_id = 7;
                strncpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
                strncpy(cpi->hba_vid, "MyDriver", HBA_IDLEN);
                strncpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
                cpi->unit_number = cam_sim_unit(sim);
                cpi->bus_id = cam_sim_bus(sim);
                cpi->ccb_h.status = CAM_REQ_CMP;
                break;
        }

        default:
                ccb->ccb_h.status = CAM_REQ_INVALID;
                break;
        }

        xpt_done(ccb);
}
```

Tres ramas ilustran los patrones:

`XPT_SCSI_IO` es la ruta de datos. El driver inicia un I/O asíncrono (escribiendo descriptores en el hardware, programando DMA y demás) y regresa inmediatamente sin llamar a `xpt_done`. El hardware completa el I/O unos milisegundos después, genera una interrupción, el manejador de interrupciones calcula el resultado, rellena el estado del CCB, y solo entonces llama a `xpt_done`. CAM no exige finalización síncrona; el driver puede tardar tanto como tarde el hardware.

`XPT_RESET_BUS` es un control síncrono. El driver realiza el reset, establece `CAM_REQ_CMP` y cae al `xpt_done`. No hay ningún componente asíncrono.

`XPT_PATH_INQ` es la autodescripción del SIM. La primera vez que CAM sondea el SIM emite `XPT_PATH_INQ` y lee las características del bus: máximo LUN, flags admitidos, identificadores del fabricante, etc. El driver rellena la estructura y devuelve. Sin una respuesta correcta a `XPT_PATH_INQ`, CAM no puede sondear los targets detrás del SIM y el driver aparece como registrado pero inerte.

La rama `default` devuelve `CAM_REQ_INVALID` para cualquier código de función que el driver no implemente. CAM es tolerante en este aspecto; simplemente trata la petición como no admitida y o bien recurre a una implementación genérica o bien expone el error al driver periférico.

### Cómo ve el resultado un operador

Una vez que un driver con CAM ha llamado a `xpt_bus_register`, CAM sondea el bus y el resultado visible para el usuario es una o más entradas en `camcontrol devlist`:

```console
$ camcontrol devlist
<MyDriver Volume 1.0>             at scbus0 target 0 lun 0 (pass0,da0)
$ ls /dev/da0
/dev/da0
$ diskinfo /dev/da0
/dev/da0   512 ... ...
```

El dispositivo `da0` bajo `/dev` es un driver periférico de CAM (`da(4)`) que envuelve el LUN que CAM descubrió detrás del SIM. El operador nunca trata con el SIM directamente; solo ve la interfaz estándar `/dev/daN` que usa cualquier dispositivo de bloques. Esto es lo que hace de CAM un objetivo de integración tan productivo: escribe un SIM y obtienes un I/O de tipo disco completo gratis.

### Reconocimiento de patrones

En este punto el lector debería ver la misma forma que vimos en la sección 5:

| Paso                  | Redes                            | CAM                                             |
|-----------------------|----------------------------------|-------------------------------------------------|
| Asignar objeto        | `if_alloc`                       | `cam_sim_alloc`                                 |
| Nombrar y configurar  | `if_initname`, establecer callbacks | implícito en los argumentos de `cam_sim_alloc` |
| Conectar al framework | `if_attach`                      | `xpt_bus_register`                              |
| Hacer descubrible     | `bpfattach`                      | `xpt_create_path`                               |
| Recibir tráfico       | callback `if_output`             | callback de acción                              |
| Completar una op      | (síncrono)                       | `xpt_done(ccb)`                                 |
| Desconectar           | `bpfdetach`, `if_detach`         | `xpt_free_path`, `xpt_bus_deregister`           |
| Liberar               | `if_free`                        | `cam_sim_free`                                  |

Otras interfaces de registro (`pcm_register` para sonido, `usb_attach` para USB, `g_attach` para GEOM) siguen la misma estructura de columnas con su propio vocabulario. Una vez que el lector ve esta tabla, cada integración posterior es cuestión de buscar los nombres.

### Cerrando la sección 6

La sección 6 esbozó la interfaz de registro para un SIM de CAM. El driver asigna un SIM con `cam_sim_alloc`, lo registra con `xpt_bus_register`, crea un path para eventos, recibe I/O a través del callback de acción, completa el I/O con `xpt_done` y desregistra en orden inverso al desconectarse. El mismo patrón de integración de estilo registro que vimos con `ifnet` se aplica aquí, con el obvio cambio de vocabulario.

El lector ya ha visto tres superficies de integración (devfs, ioctl, sysctl) que casi todo driver necesita y dos superficies de estilo registro (redes, CAM) que algunos drivers necesitan. En la sección 7 damos un paso atrás y codificamos la disciplina del ciclo de vida que lo mantiene todo unido: el orden de registro en attach, el orden de desmontaje en detach, y el pequeño conjunto de patrones que distingue a un driver que carga, funciona y se descarga limpiamente de uno que pierde recursos o entra en pánico al desconectarse.

## Sección 7: Disciplina de registro, desmontaje y limpieza

### La regla cardinal

Un driver que se integra con el kernel a través de varios frameworks (devfs para `/dev`, sysctl para parámetros ajustables, `ifnet` para redes, CAM para almacenamiento, callouts para temporizadores, taskqueues para trabajo diferido, etc.) acumula un pequeño zoo de objetos asignados y callbacks registrados. Todos ellos tienen la misma propiedad: deben liberarse en el orden inverso al que fueron creados. Olvidar esto convierte una desconexión limpia en un pánico del kernel, pierde recursos al descargar el módulo y dispersa punteros colgantes por subsistemas que el driver ya no posee.

La regla cardinal de la integración es, por tanto, muy sencilla de enunciar, aunque aplicarla con limpieza requiere cuidado:

> **Todo registro exitoso debe ir acompañado de su correspondiente desregistro. El orden de desregistro es el inverso al orden de registro. Un registro fallido debe provocar el desregistro de todos los registros previos exitosos antes de que la función devuelva el error.**

Esa única frase describe toda la disciplina del ciclo de vida. El resto de esta sección es un recorrido guiado sobre cómo aplicarla.

### Por qué el orden inverso

La regla del orden inverso puede parecer arbitraria; no lo es. Cada registro es una promesa al framework de que «desde ahora hasta que llame a deregister, puedes hacer callbacks hacia mí, depender de mi estado o asignarme trabajo». Un framework que tiene un callback hacia el driver y que conserva trabajo pendiente para él no puede desmontarse de forma segura mientras otro framework siga teniendo acceso al mismo estado.

Por ejemplo, supongamos que el driver registra un callout, luego un cdev y después un sysctl OID. El callback `read` del cdev puede consultar un valor que el callout actualiza; el callout, a su vez, puede leer el estado que expone el sysctl OID. Si detach desmonta el callout primero, entonces mientras el cdev se está desmontando, un `read` desde el espacio de usuario podría intentar consultar un valor que el callout debería haber mantenido actualizado; ese valor es ahora obsoleto y la lectura devuelve un resultado sin sentido. Si detach desmonta primero el cdev, ya no hay forma de que llegue ningún `read`, y el callout puede cancelarse de forma segura. El orden importa.

La regla general es: desmonta primero lo que puede hacer callbacks hacia ti, antes de desmontar aquello de lo que depende.

Para la mayoría de drivers, la cadena de dependencias coincide con el orden de creación:

* El cdev depende del softc (los callbacks del cdev desreferencian `si_drv1`).
* Los sysctl OIDs dependen del softc (apuntan a campos del softc).
* Los callouts y taskqueues dependen del softc (reciben un puntero al softc como argumento).
* Los manejadores de interrupción dependen del softc, los locks y los DMA tags.
* Los DMA tags y los recursos del bus dependen del dispositivo.

Si el driver los crea en este orden, debe destruirlos en el orden exactamente inverso: primero las interrupciones (pueden dispararse en cualquier momento), luego los callouts y taskqueues (se ejecutan en cualquier momento), después los cdevs (reciben llamadas desde el espacio de usuario), luego los sysctl OIDs (el framework los limpia automáticamente), después el DMA, los recursos del bus y, por último, los locks. El propio softc es lo último que se libera.

### El patrón `goto err1` en attach

El lugar más difícil en el que aplicar esta regla es en attach, cuando un fallo parcial puede dejar al driver a mitad de inicialización. El patrón canónico en FreeBSD es la cadena de etiquetas `goto`, cada una de las cuales representa la limpieza necesaria hasta ese punto:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        struct make_dev_args args;
        int error;

        sc->sc_dev = dev;
        mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);
        strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
        sc->sc_msglen = strlen(sc->sc_msg);

        TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default", &sc->sc_debug);

        make_dev_args_init(&args);
        args.mda_devsw = &myfirst_cdevsw;
        args.mda_uid = UID_ROOT;
        args.mda_gid = GID_WHEEL;
        args.mda_mode = 0660;
        args.mda_si_drv1 = sc;
        args.mda_unit = device_get_unit(dev);

        error = make_dev_s(&args, &sc->sc_cdev,
            "myfirst%d", device_get_unit(dev));
        if (error != 0)
                goto fail_mtx;

        myfirst_sysctl_attach(sc);

        DPRINTF(sc, MYF_DBG_INIT, "attach: stage 3 complete\n");
        return (0);

fail_mtx:
        mtx_destroy(&sc->sc_mtx);
        return (error);
}
```

La etiqueta de error única existe aquí porque solo hay un punto en el que puede producirse un fallo real (la llamada a `make_dev_s`). Un driver más elaborado tendría una etiqueta por cada paso de registro. Por convención, cada etiqueta recibe el nombre del paso que ha fallado (`fail_mtx`, `fail_cdev`, `fail_sysctl`), y cada etiqueta ejecuta la limpieza de todos los pasos **anteriores** en la función. La etiqueta que gestiona el último fallo posible tiene la limpieza más larga; la que gestiona el primer fallo tiene la más corta.

Un attach de cuatro etapas para un driver de hardware hipotético tendría este aspecto:

```c
static int
mydriver_attach(device_t dev)
{
        struct mydriver_softc *sc = device_get_softc(dev);
        int error;

        mtx_init(&sc->sc_mtx, "mydriver", NULL, MTX_DEF);

        error = bus_alloc_resource_any(...);
        if (error != 0)
                goto fail_mtx;

        error = bus_setup_intr(...);
        if (error != 0)
                goto fail_resource;

        error = make_dev_s(...);
        if (error != 0)
                goto fail_intr;

        return (0);

fail_intr:
        bus_teardown_intr(...);
fail_resource:
        bus_release_resource(...);
fail_mtx:
        mtx_destroy(&sc->sc_mtx);
        return (error);
}
```

Las etiquetas se leen de arriba abajo en el mismo orden en que se ejecutan las acciones de limpieza. Un fallo en cualquier paso salta a la etiqueta correspondiente y ejecuta en secuencia las etiquetas de limpieza de todos los pasos anteriores que tuvieron éxito. El patrón es tan habitual que leer código de un driver sin él resulta chocante; los revisores esperan encontrarlo.

### El espejo de detach

Detach debe ser el reflejo exacto de un attach exitoso. Cada registro realizado en attach debe tener su correspondiente baja en detach, en orden inverso:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        mtx_lock(&sc->sc_mtx);
        if (sc->sc_open_count > 0) {
                mtx_unlock(&sc->sc_mtx);
                return (EBUSY);
        }
        mtx_unlock(&sc->sc_mtx);

        DPRINTF(sc, MYF_DBG_INIT, "detach: tearing down\n");

        /*
         * destroy_dev drains any in-flight cdevsw callbacks. After
         * this call returns, no new open/close/read/write/ioctl can
         * arrive, and no in-flight callback is still running.
         */
        destroy_dev(sc->sc_cdev);

        /*
         * The per-device sysctl context is torn down automatically by
         * the framework after detach returns successfully. Nothing to
         * do here.
         */

        mtx_destroy(&sc->sc_mtx);
        return (0);
}
```

Detach comienza comprobando `open_count` bajo el mutex; si alguien tiene el dispositivo abierto, detach rechaza la operación (devolviendo `EBUSY`) para que el operador reciba un error claro en lugar de un pánico del kernel. Tras la comprobación, la función desmonta todo lo que attach asignó, en orden inverso: primero el cdev, luego el sysctl (automático) y después el mutex.

El retorno temprano con `EBUSY` es el patrón de detach «suave». Pone la responsabilidad de cerrar el dispositivo en el operador: `kldunload myfirst` fallará hasta que el operador ejecute `pkill cat` (o lo que sea que esté manteniendo el dispositivo abierto). La alternativa es el patrón «duro», que rechaza detach solo si un recurso crítico está en uso y acepta que el kernel es responsable de drenar los descriptores de archivo ordinarios. El patrón duro es más complejo (normalmente requiere `dev_ref` y `dev_rel`) y se deja como tema para la sección de drivers CAM del Capítulo 27.

### Manejadores de eventos de módulo

Hasta ahora hemos tratado `attach` y `detach`, los hooks del ciclo de vida por dispositivo que Newbus invoca cuando se añade o elimina una instancia del driver. Existe también un ciclo de vida por módulo, controlado por la función registrada a través de `DRIVER_MODULE` (o `MODULE_VERSION` junto con un `DECLARE_MODULE`). El kernel invoca esta función en los eventos `MOD_LOAD`, `MOD_UNLOAD` y `MOD_SHUTDOWN`.

Para la mayoría de drivers, el hook por módulo no se utiliza. `DRIVER_MODULE` acepta NULL como manejador de eventos por defecto, y el kernel hace lo correcto: en `MOD_LOAD` añade el driver a la lista de drivers del bus y, en `MOD_UNLOAD`, recorre el bus y desconecta cada instancia. El autor del driver solo escribe `attach` y `detach`.

Sin embargo, algunos drivers sí necesitan un hook a nivel de módulo. El caso clásico es un driver que necesita configurar un recurso global (una tabla hash global, un mutex global, un manejador de eventos global) compartido entre todas las instancias. El hook para ello es:

```c
static int
myfirst_modevent(module_t mod, int what, void *arg)
{
        switch (what) {
        case MOD_LOAD:
                /* allocate global state */
                return (0);
        case MOD_UNLOAD:
                /* free global state */
                return (0);
        case MOD_SHUTDOWN:
                /* about to power off; flush anything important */
                return (0);
        default:
                return (EOPNOTSUPP);
        }
}

static moduledata_t myfirst_mod = {
        "myfirst", myfirst_modevent, NULL
};
DECLARE_MODULE(myfirst, myfirst_mod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(myfirst, 1);
```

El driver `myfirst` de este capítulo no tiene estado global y, por tanto, no necesita un `modevent`. El mecanismo predeterminado de `DRIVER_MODULE` es suficiente. Mencionamos el hook aquí para que el lector pueda reconocerlo en drivers más complejos.

### `EVENTHANDLER` para eventos del sistema

Algunos drivers necesitan estar al tanto de eventos que ocurren en otras partes del kernel: un proceso está bifurcándose, el sistema se está apagando, la red está cambiando de estado, etcétera. El mecanismo `EVENTHANDLER` permite a un driver registrar un callback para un evento con nombre:

```c
static eventhandler_tag myfirst_eh_tag;

static void
myfirst_shutdown_handler(void *arg, int howto)
{
        /* called when the system is shutting down */
}

/* In attach: */
myfirst_eh_tag = EVENTHANDLER_REGISTER(shutdown_pre_sync,
    myfirst_shutdown_handler, sc, EVENTHANDLER_PRI_ANY);

/* In detach: */
EVENTHANDLER_DEREGISTER(shutdown_pre_sync, myfirst_eh_tag);
```

Los nombres de evento `shutdown_pre_sync`, `shutdown_post_sync`, `shutdown_final` y `vm_lowmem` son los más utilizados en drivers. Cada uno es un punto de enganche documentado con su propia semántica sobre lo que el driver puede hacer dentro del callback (dormir, asignar memoria, tomar locks, comunicarse con el hardware).

La regla cardinal se aplica a los manejadores de eventos exactamente igual que a todo lo demás: cada `EVENTHANDLER_REGISTER` exitoso debe estar emparejado con un `EVENTHANDLER_DEREGISTER` en orden inverso. Olvidar el deregister deja un puntero a función colgante en la tabla de manejadores de eventos; la próxima vez que el evento se dispare tras descargar el módulo, el kernel saltará a memoria ya liberada y provocará un pánico.

### `SYSINIT` para la inicialización única del kernel

Otro mecanismo que merece conocerse es `SYSINIT(9)`, el mecanismo de inicialización única del kernel registrado en tiempo de compilación. Una declaración `SYSINIT` en el código de un driver:

```c
static void
myfirst_sysinit(void *arg __unused)
{
        /* runs once, very early at kernel boot */
}
SYSINIT(myfirst_init, SI_SUB_DRIVERS, SI_ORDER_FIRST,
    myfirst_sysinit, NULL);
```

declara una función que se ejecuta en un punto específico durante la inicialización del kernel, antes de que exista ningún proceso en espacio de usuario. `SYSINIT` raramente se necesita en el código de un driver; la función no se vuelve a ejecutar cuando el módulo se recarga, por lo que no ofrece al driver la oportunidad de configurar el estado por carga. La mayoría de los drivers que creen necesitar `SYSINIT` en realidad necesitan un manejador de eventos `MOD_LOAD`.

La declaración `SYSUNINIT(9)` correspondiente:

```c
SYSUNINIT(myfirst_uninit, SI_SUB_DRIVERS, SI_ORDER_FIRST,
    myfirst_sysuninit, NULL);
```

declara una función que se ejecuta en el punto de desmontaje correspondiente. El orden de declaración importa: `SI_SUB_DRIVERS` se ejecuta después de `SI_SUB_VFS` pero antes de `SI_SUB_KICK_SCHEDULER`, por lo que un `SYSINIT` en este nivel ya puede usar el sistema de archivos, pero aún no puede planificar procesos.

### `bus_generic_detach` and `device_delete_children`

Los drivers que son ellos mismos buses (un driver de puente PCI a PCI, un driver de hub USB, un driver virtual tipo bus) tienen dispositivos hijo conectados a ellos. Desconectar el padre requiere desconectar primero todos los hijos, en el orden correcto. El framework proporciona dos funciones auxiliares:

`bus_generic_detach(dev)` recorre los hijos del dispositivo y llama a `device_detach` en cada uno. Devuelve 0 si todos los hijos se desconectaron correctamente, o el primer código de retorno distinto de cero si alguno lo rechazó.

`device_delete_children(dev)` llama a `bus_generic_detach` y después a `device_delete_child` para cada hijo, liberando las estructuras del dispositivo hijo.

El detach de un driver tipo bus siempre debe comenzar con una de estas dos llamadas:

```c
static int
mybus_detach(device_t dev)
{
        int error;

        error = bus_generic_detach(dev);
        if (error != 0)
                return (error);

        /* now safe to tear down per-bus state */
        ...
        return (0);
}
```

Si el driver desmonta su estado de bus antes de desconectar los hijos, estos encontrarán que los recursos de su padre ya han sido liberados y se producirá un fallo. El orden es, por tanto: desconectar primero los hijos (bus_generic_detach) y luego desmontar el estado por bus.

### Reuniendo todo

La disciplina del ciclo de vida puede resumirse en una breve lista de comprobación que todo driver debería cumplir:

1. **Cada asignación tiene su correspondiente liberación.** Haz un seguimiento con una cadena `goto err` en attach y un orden de espejo en detach.
2. **Cada registro tiene su correspondiente baja.** Esto se aplica por igual a los cdevs, sysctls, callouts, taskqueues, manejadores de eventos, manejadores de interrupción, DMA tags y recursos del bus.
3. **El orden de desmontaje es el inverso al orden de configuración.** Un driver que viole esto provocará fugas, un pánico del kernel, o ambas cosas.
4. **La función detach rechaza la operación si algún recurso visible externamente sigue en uso.** `EBUSY` es el código de retorno correcto.
5. **La función detach nunca libera el softc; el framework lo hace automáticamente después de que detach retorne con éxito.**
6. **El cdev se destruye (no se libera) con `destroy_dev`, y `destroy_dev` bloquea hasta que retornen los callbacks en vuelo.**
7. **El contexto sysctl por dispositivo se desmonta automáticamente; el driver no llama a `sysctl_ctx_free` para ello.**
8. **Los drivers tipo bus desconectan primero sus hijos con `bus_generic_detach` o `device_delete_children`, y luego desmontan el estado por bus.**
9. **Un attach fallido revierte todos los pasos anteriores antes de devolver el código de error.**
10. **El kernel nunca ve un driver a mitad de conexión: attach tiene éxito de forma completa o falla de forma completa.**

El driver `myfirst` de la etapa 3 supera todos los puntos de esta lista; el laboratorio de la Sección 9 pide al lector que inyecte un fallo deliberado para ver el proceso de reversión en acción.

### Cerrando la Sección 7

La Sección 7 ha codificado la disciplina del ciclo de vida que mantiene unidas todas las secciones anteriores. La cadena `goto err` en attach y el desmontaje en orden inverso en detach son los dos patrones que el lector utilizará en cada driver que escriba a partir de ahora. Los hooks a nivel de módulo (`MOD_LOAD`, `MOD_UNLOAD`), el registro de manejadores de eventos (`EVENTHANDLER_REGISTER`) y el detach tipo bus (`bus_generic_detach`) son las variaciones que algunos drivers necesitan; para un pseudo-driver de instancia única como `myfirst`, el par básico attach/detach más la cadena `goto err` es suficiente.

En la Sección 8 retomamos el otro meta-tema del capítulo: cómo evolucionó el driver desde la versión `1.0` en la Parte II, pasando por `1.5-channels` en la Parte III y `1.6-debug` en el Capítulo 23, hasta `1.7-integration` aquí, y cómo esa evolución debería ser visible en los comentarios del código fuente, en la declaración `MODULE_VERSION` y en lugares visibles para el usuario como el sysctl OID `version`. El lector abandonará el Capítulo 24 no solo con un driver completamente integrado, sino también con una disciplina sobre cómo el número de versión de un driver le indica al lector qué esperar.

## Sección 8: Refactorización y versionado

### Un driver tiene una historia

El driver `myfirst` no apareció completamente formado. Comenzó en la Parte II como una demostración de un solo archivo de cómo funciona `DRIVER_MODULE`, creció en la Parte III para dar soporte a múltiples instancias y estado por canal, adquirió infraestructura de depuración y trazado en el Capítulo 23, y obtiene su superficie de integración completa en este capítulo. Cada paso dejó el código fuente más grande y más capaz.

Los drivers del árbol de FreeBSD tienen historias igualmente largas. `null(4)` se remonta a 1982; su `cdevsw` ha sido refactorizado al menos tres veces para adaptarse a la evolución del kernel, pero su comportamiento visible para el usuario no ha cambiado. `if_ethersubr.c` es anterior a IPv6 y su API ha ido sumando nuevas funciones en cada versión mientras las antiguas permanecían en su lugar. El arte del mantenimiento de drivers consiste en parte en saber cómo extender un driver sin romper lo que existía antes.

Esta sección es una breve pausa para hablar de tres disciplinas estrechamente relacionadas: cómo refactorizar un driver a medida que crece, cómo expresar la versión en la que se encuentra y cómo decidir qué cuenta como un cambio incompatible. El ejemplo de trabajo del capítulo es la transición de `1.6-debug` (el final del Capítulo 23) a `1.7-integration` (el final de este capítulo), pero los patrones se generalizan a cualquier proyecto de driver.

### De un solo archivo a varios

El driver `myfirst` del Capítulo 23 era un árbol de código fuente pequeño pero real:

```text
myfirst.c          /* probe, attach, detach, cdevsw, read, write */
myfirst.h          /* softc, function declarations */
myfirst_debug.c    /* SDT provider definition */
myfirst_debug.h    /* DPRINTF, debug class bits */
Makefile
```

La etapa 3 de este capítulo añade dos nuevos archivos fuente:

```text
myfirst_ioctl.c    /* ioctl dispatcher */
myfirst_ioctl.h    /* PUBLIC ioctl interface for user space */
myfirst_sysctl.c   /* sysctl OID construction */
```

La decisión de separar cada nueva responsabilidad en su propio par de archivos es deliberada. Un único `myfirst.c` de 2000 líneas compilaría, cargaría y funcionaría, pero también sería más difícil de leer, de probar y de navegar para un co-mantenedor. Dividir por responsabilidades (open/close frente a ioctl frente a sysctl frente a debug) permite que cada archivo quepa en pantalla y que el lector comprenda una responsabilidad cada vez.

El patrón es, a grandes rasgos:

* `<driver>.c` contiene probe, attach, detach, la estructura cdevsw y el pequeño conjunto de callbacks de cdevsw (open, close, read, write).
* `<driver>.h` contiene el softc, las declaraciones de función compartidas entre archivos y cualquier constante privada. **No** lo incluye el espacio de usuario.
* `<driver>_debug.c` y `<driver>_debug.h` contienen el proveedor SDT, la macro DPRINTF y la enumeración de clases de depuración. **No** los incluye el espacio de usuario.
* `<driver>_ioctl.c` contiene el despachador de ioctl. `<driver>_ioctl.h` es la cabecera **pública**, incluye únicamente `sys/types.h` y `sys/ioccom.h`, y puede incluirse de forma segura desde código de espacio de usuario.
* `<driver>_sysctl.c` contiene la construcción del OID de sysctl. **No** lo incluye el espacio de usuario.

La distinción entre cabeceras públicas y privadas importa por dos razones. En primer lugar, las cabeceras públicas deben compilar sin errores fuera del contexto del kernel (`_KERNEL` no está definido cuando el espacio de usuario las incluye); una cabecera que incorpore `sys/lock.h` y `sys/mutex.h` fallará al compilar desde un build de espacio de usuario. En segundo lugar, las cabeceras públicas forman parte del contrato del driver con el espacio de usuario y deben poder instalarse en una ubicación del sistema como `/usr/local/include/myfirst/myfirst_ioctl.h`. Una cabecera privada que se convierte accidentalmente en pública es una trampa de mantenimiento: todo programa de espacio de usuario que la incluya fija la disposición interna del driver y cualquier refactorización futura los romperá.

La cabecera `myfirst_ioctl.h` de este capítulo es la única cabecera pública del driver. Es pequeña, autocontenida y utiliza únicamente tipos estables.

### Cadenas de versión, números de versión y la versión de API

Un driver lleva tres versiones distintas, cada una con un significado diferente.

La **versión de release** es la cadena legible por humanos que se imprime en `dmesg`, se expone a través de `dev.<driver>.0.version` y se usa en conversaciones y documentación. El driver `myfirst` usa cadenas con puntos como `1.6-debug` y `1.7-integration`. El formato es una convención; lo que importa es que la cadena sea corta, descriptiva y única por release.

La **versión de módulo** es un entero declarado con `MODULE_VERSION(<name>, <integer>)`. El kernel la utiliza para imponer dependencias entre módulos. Un módulo que depende de `myfirst` declara `MODULE_DEPEND(other, myfirst, 1, 1, 1)`, donde los tres enteros son las versiones mínima, preferida y máxima aceptables. Incrementar la versión de módulo indica "he roto la compatibilidad con versiones anteriores; los módulos que dependen de mí deben recompilarse".

La **versión de API** es el entero expuesto a través de `MYFIRSTIOC_GETVER` y almacenado en la constante `MYFIRST_IOCTL_VERSION`. Los programas de espacio de usuario la utilizan para detectar cambios en la API antes de emitir ioctls que podrían fallar. Incrementar la versión de API indica "la interfaz visible desde el espacio de usuario ha cambiado de una forma que los programas más antiguos no sabrán gestionar".

Las tres versiones son independientes. Un mismo release puede incrementar únicamente la versión de API (porque se añadió un nuevo ioctl) sin incrementar la versión de módulo (porque los dependientes en el kernel no se ven afectados). A la inversa, una refactorización que cambia la disposición de una estructura de datos exportada al kernel puede incrementar la versión de módulo sin incrementar la versión de API, porque el espacio de usuario no aprecia ningún cambio.

Para `myfirst`, el capítulo utiliza estos valores:

```c
/* myfirst_sysctl.c */
#define MYFIRST_VERSION "1.7-integration"

/* myfirst.c */
MODULE_VERSION(myfirst, 1);

/* myfirst_ioctl.h */
#define MYFIRST_IOCTL_VERSION 1
```

El release es `1.7-integration` porque acabamos de incorporar el trabajo de integración. La versión de módulo sigue siendo `1` porque no existen dependientes en el kernel. La versión de API es `1` porque este es el primer capítulo que expone ioctls; la etapa 2 del capítulo introdujo la interfaz y cualquier cambio futuro en la disposición del ioctl tendría que incrementarla.

### Cuándo incrementar cada una

La regla para incrementar la **versión de release** es "cada vez que el driver cambia de una forma que pueda importarle al operador". Añadir una funcionalidad, cambiar el comportamiento por defecto o corregir un error notable son razones válidas. La versión de release es para las personas; debería cambiar con la frecuencia suficiente para que el campo sea informativo.

La regla para incrementar la **versión de módulo** es "cuando los usuarios del driver en el kernel necesitarían recompilar para seguir funcionando". Añadir una nueva función en el kernel no supone un incremento (los dependientes anteriores siguen funcionando). Eliminar una función o cambiar su firma sí lo supone. Renombrar un campo de struct que otros módulos leen también lo supone. Un driver que no exporta nada al kernel puede mantener su versión de módulo en 1 indefinidamente.

La regla para incrementar la **versión de API** es "cuando un programa de espacio de usuario existente interpretaría erróneamente las respuestas del driver o fallaría de forma no evidente". Añadir un nuevo ioctl no supone un incremento (los programas anteriores no lo utilizan). Cambiar la disposición de la estructura de argumentos de un ioctl existente sí lo supone. Renumerar un ioctl existente también lo supone. Un driver que todavía no se ha publicado para los usuarios puede cambiar libremente la versión de API mientras la interfaz aún está en diseño; una vez que el primer usuario haya distribuido software contra ella, cualquier cambio es un evento público.

### Shims de compatibilidad

Un driver ampliamente distribuido acumula shims de compatibilidad. La forma clásica es un ioctl de "versión 1" que el driver soporta indefinidamente junto a un ioctl de "versión 2" que lo reemplaza. Los programas de espacio de usuario que utilizan la interfaz v1 siguen funcionando, los que utilizan v2 obtienen el nuevo comportamiento y el driver mantiene ambas rutas de código.

El coste de los shims es real. Cada shim es código que hay que probar, documentar y mantener. Cada shim es también una API fijada que limita las refactorizaciones futuras. Un driver con cinco shims es más difícil de evolucionar que uno con un solo shim.

La disciplina consiste, por tanto, en diseñar con cuidado desde el principio para que los shims sean poco frecuentes. Tres hábitos ayudan a conseguirlo:

* **Usa constantes con nombre, no números literales.** Un programa que utiliza `MYFIRSTIOC_SETMSG` en lugar de `0x802004d3` seguirá funcionando cuando el driver renumere el ioctl, porque tanto la cabecera como el programa se recompilan contra la nueva cabecera.
* **Prefiere los cambios aditivos a los cambios mutativos.** Cuando el driver necesite exponer un nuevo campo, añade un nuevo ioctl en lugar de extender una estructura existente. El ioctl antiguo conserva su disposición; el nuevo lleva la información adicional.
* **Versiona cada estructura pública.** Un `struct myfirst_v1_args` emparejado con `MYFIRSTIOC_SETMSG_V1` es una pequeña anotación ahora y una gran ventaja de compatibilidad más adelante.

`myfirst` en este capítulo es tan pequeño que todavía no tiene ningún shim. La única concesión del capítulo al versionado es el ioctl `MYFIRSTIOC_GETVER`, que ofrece a un futuro mantenedor un lugar limpio donde añadir lógica de shim cuando llegue el momento.

### Una refactorización comentada: dividir `myfirst.c`

La transición desde la etapa 3 del Capítulo 23 (debug) hasta la etapa 3 de este capítulo (sysctl) es en sí misma una pequeña refactorización. El código fuente de partida tenía un único `myfirst.c` de 1000 líneas y un pequeño `myfirst_debug.c`. El código fuente resultante tiene el mismo `myfirst.c` reducido en unas 100 líneas, más dos nuevos archivos (`myfirst_ioctl.c` y `myfirst_sysctl.c`) que absorben la nueva lógica.

Los pasos de la refactorización fueron:

1. Añadir los dos nuevos archivos con la nueva lógica.
2. Añadir las nuevas declaraciones de función a `myfirst.h` para que el cdevsw pueda referenciar `myfirst_ioctl`.
3. Actualizar `myfirst.c` para llamar a `myfirst_sysctl_attach(sc)` desde `attach`.
4. Actualizar el `Makefile` para incluir los nuevos archivos en `SRCS`.
5. Construir, cargar, ejercitar y verificar que el driver sigue superando todos los laboratorios del Capítulo 23.
6. Incrementar la versión de release a `1.7-integration`.
7. Añadir la prueba de `MYFIRSTIOC_GETVER` a los scripts de verificación del capítulo.

Cada paso es suficientemente pequeño para revisarlo por separado. Ninguno toca la lógica existente, lo que significa que la refactorización tiene pocas probabilidades de introducir regresiones en código que funcionaba antes. Esta es la disciplina de la refactorización aditiva: hacer crecer el driver hacia fuera añadiendo nuevos archivos y nuevas declaraciones, dejar el código existente en su sitio e incrementar la versión cuando el polvo se asiente.

Una refactorización más agresiva (renombrar una función, reorganizar una estructura, cambiar el conjunto de flags de cdevsw) requeriría una disciplina diferente: un único commit por cambio, pruebas de regresión ejecutadas tras cada uno y una nota clara en el incremento de versión sobre qué se reorganizó. Los drivers de amplia distribución aplican esta disciplina en cada release; el driver `if_em` del árbol de código fuente, por ejemplo, tiene una refactorización de varios commits en casi cada release menor de FreeBSD, con cada commit desplegado de forma independiente y probado por separado.

### Tres drivers del árbol comparados

Tres drivers del árbol de código fuente de FreeBSD ilustran la disciplina de organización del código fuente en tres puntos del espectro de complejidad. Leerlos como un tríptico hace visibles los patrones.

`/usr/src/sys/dev/null/null.c` es el más pequeño. Es un único archivo fuente de 200 líneas con una tabla `cdevsw`, un conjunto de callbacks, sin cabecera separada y sin maquinaria de debug ni de sysctl. El driver completo cabe en tres páginas impresas. Este es el esquema para un driver cuyo único trabajo es estar presente y absorber (o generar) bytes; la integración se produce únicamente en la capa cdev.

`/usr/src/sys/net/if_disc.c` es un driver de red de dos archivos: `if_disc.c` para el código del driver y el implícito `if.h` para el framework. El driver se registra en la pila de red pero no tiene árbol sysctl, ni subárbol de depuración, ni cabecera ioctl pública (utiliza el conjunto estándar `if_ioctl` definido por el framework). Este es el esquema para un driver que es una instancia de un framework en lugar de algo propio; el framework define la superficie y el driver rellena los huecos.

`/usr/src/sys/dev/ahci/ahci.c` es un driver con múltiples archivos: archivos separados para el núcleo AHCI, el código de attach PCI, el código de attach FDT del árbol de dispositivos, el código de gestión del enclosure y la lógica específica del bus. Cada archivo se dedica a una responsabilidad; el archivo central supera las 5000 líneas, pero el tamaño por archivo es manejable. Este es el esquema que escala a un driver real de producción: dividir por responsabilidades, enlazar mediante cabeceras y usar el límite de archivo como unidad de refactorización.

El driver `myfirst` de este capítulo se encuentra en el punto medio. La etapa 3 tiene cinco archivos fuente: `myfirst.c` (open/close/read/write y el cdevsw), `myfirst.h` (softc, declaraciones), `myfirst_debug.c` y `myfirst_debug.h` (debug y SDT), `myfirst_ioctl.c` y `myfirst_ioctl.h` (ioctl, siendo este último público) y `myfirst_sysctl.c` (árbol sysctl). Esto es suficiente para demostrar el patrón de división por responsabilidades sin la carga cognitiva de un driver de cincuenta archivos. Un lector que necesite hacer crecer `myfirst` más allá dispone de una plantilla clara: añadir un nuevo par de archivos para la nueva responsabilidad, añadir el archivo fuente a `SRCS`, añadir la cabecera pública al conjunto de instalación si el espacio de usuario la necesita e incrementar `MYFIRST_VERSION`.

### Cerrando la sección 8

La sección 8 cerró el círculo sobre el otro tema del capítulo: cómo la organización del código fuente, los números de versión y la disciplina de refactorización de un driver reflejan su evolución. El hito del driver para este capítulo es `1.7-integration`, expresado simultáneamente como la cadena de release en `MYFIRST_VERSION`, la versión de módulo `1` (sin cambios porque no existen dependientes en el kernel) y la versión de API `1` (establecida por primera vez porque este es el primer capítulo que expone una interfaz ioctl estable). La refactorización se mantuvo aditiva, por lo que no fue necesario ningún shim.

El lector ya ha visto la superficie de integración completa: las secciones 2 a 4 cubrieron los tres universales (devfs, ioctl, sysctl), las secciones 5 y 6 esbozaron las integraciones basadas en registro (red, CAM), la sección 7 codificó la disciplina del ciclo de vida y la sección 8 enmarcó el conjunto como una evolución que los números de versión del driver deben reflejar. Las secciones restantes del capítulo ofrecen al lector práctica directa con el mismo material.

### Reuniéndolo todo: el attach y el detach finales

Antes de pasar a los laboratorios, merece la pena ver las funciones attach y detach completas del capítulo reunidas en un mismo lugar. Conectan todas las secciones anteriores: la construcción del cdev de la Sección 2, el cableado del ioctl de la Sección 3, el árbol sysctl de la Sección 4, la disciplina del ciclo de vida de la Sección 7 y el manejo de versiones de la Sección 8.

El attach completo para la etapa 3:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        struct make_dev_args args;
        int error;

        /* 1. Stash the device pointer and initialise the lock. */
        sc->sc_dev = dev;
        mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

        /* 2. Initialise the in-driver state to its defaults. */
        strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
        sc->sc_msglen = strlen(sc->sc_msg);
        sc->sc_open_count = 0;
        sc->sc_total_reads = 0;
        sc->sc_total_writes = 0;
        sc->sc_debug = 0;

        /* 3. Read the boot-time tunable for the debug mask. If the
         *    operator set hw.myfirst.debug_mask_default in
         *    /boot/loader.conf, sc_debug now holds that value;
         *    otherwise sc_debug remains zero.
         */
        TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default", &sc->sc_debug);

        /* 4. Construct the cdev. The args struct gives us a typed,
         *    versionable interface; mda_si_drv1 wires the per-cdev
         *    pointer to the softc atomically, closing the race window
         *    between creation and assignment.
         */
        make_dev_args_init(&args);
        args.mda_devsw = &myfirst_cdevsw;
        args.mda_uid = UID_ROOT;
        args.mda_gid = GID_WHEEL;
        args.mda_mode = 0660;
        args.mda_si_drv1 = sc;
        args.mda_unit = device_get_unit(dev);

        error = make_dev_s(&args, &sc->sc_cdev,
            "myfirst%d", device_get_unit(dev));
        if (error != 0)
                goto fail_mtx;

        /* 5. Build the sysctl tree. The framework owns the per-device
         *    context, so we do not need to track or destroy it
         *    ourselves; detach below does not call sysctl_ctx_free.
         */
        myfirst_sysctl_attach(sc);

        DPRINTF(sc, MYF_DBG_INIT,
            "attach: stage 3 complete, version " MYFIRST_VERSION "\n");
        return (0);

fail_mtx:
        mtx_destroy(&sc->sc_mtx);
        return (error);
}
```

El detach completo para la etapa 3:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* 1. Refuse detach if anyone holds the device open. The
         *    chapter's pattern is the simple soft refusal; Challenge 3
         *    walks through the more elaborate dev_ref/dev_rel pattern
         *    that drains in-flight references rather than refusing.
         */
        mtx_lock(&sc->sc_mtx);
        if (sc->sc_open_count > 0) {
                mtx_unlock(&sc->sc_mtx);
                return (EBUSY);
        }
        mtx_unlock(&sc->sc_mtx);

        DPRINTF(sc, MYF_DBG_INIT, "detach: tearing down\n");

        /* 2. Destroy the cdev. destroy_dev blocks until every
         *    in-flight cdevsw callback returns; after this call,
         *    no new open/close/read/write/ioctl can arrive.
         */
        destroy_dev(sc->sc_cdev);

        /* 3. The per-device sysctl context is torn down automatically
         *    by the framework after detach returns successfully.
         *    Nothing to do here.
         */

        /* 4. Destroy the lock. Safe now because the cdev is gone and
         *    no other code path can take it.
         */
        mtx_destroy(&sc->sc_mtx);

        return (0);
}
```

Dos aspectos merecen una nota final.

El orden de las operaciones es exactamente el inverso del attach: el lock se adquiere primero en attach y se destruye al final en detach; el cdev se crea hacia el final de attach y se destruye al principio de detach; el árbol sysctl se crea el último en attach y el framework lo desmonta el primero, de forma automática, en detach. Esta es la regla fundamental de la Sección 7 llevada a la práctica.

El patrón de rechazo en detach (la comprobación `if (open_count > 0)`) es la opción elegida en este capítulo por su simplicidad. Un driver real puede necesitar la maquinaria más elaborada de `dev_ref`/`dev_rel` para implementar un detach con drenado; el Ejercicio de desafío 3 recorre esa variante. Para `myfirst`, el rechazo simple proporciona al operador un error claro y es suficiente.

En la Sección 9 pasamos de la explicación a la práctica. Los laboratorios guían al lector en la construcción de la etapa 1, la etapa 2 y la etapa 3 de la integración, con los comandos de verificación y la salida esperada para cada una. Tras los laboratorios llegan los ejercicios de desafío (Sección 10), el catálogo de resolución de problemas (Sección 11) y el cierre y el puente que dan fin al capítulo.

## Laboratorios prácticos

Los laboratorios de esta sección llevan al lector desde un árbol de trabajo recién clonado hasta cada superficie de integración añadida en el capítulo. Cada laboratorio es lo suficientemente pequeño para completarlo en una sola sesión y va acompañado de un comando de verificación que confirma el cambio. Ejecuta los laboratorios en orden; los posteriores se apoyan en los anteriores.

Los archivos complementarios en `examples/part-05/ch24-integration/` contienen tres drivers de referencia por etapas (`stage1-devfs/`, `stage2-ioctl/`, `stage3-sysctl/`) que se corresponden con los hitos de este capítulo. Los laboratorios parten del supuesto de que el lector comienza desde su propio driver al final del Capítulo 23 (versión `1.6-debug`) o copia el directorio de etapa adecuado en una ubicación de trabajo, realiza los cambios allí y consulta el directorio de etapa correspondiente si se queda atascado.

Cada laboratorio utiliza un sistema FreeBSD 14.3 real. Una máquina virtual es perfectamente válida; no ejecutes estos laboratorios en un servidor en producción, porque la carga y descarga de módulos puede bloquear el sistema o provocar un panic si el driver tiene errores.

### Laboratorio 1: compilar y cargar el driver de etapa 1

**Objetivo**: llevar el driver desde la base del Capítulo 23 (`1.6-debug`) hasta el hito de etapa 1 de este capítulo (un cdev correctamente construido en `/dev/myfirst0`).

**Preparación**:

Parte de tu propio árbol de trabajo al final del Capítulo 23 (el driver en la versión `1.6-debug`) o copia el árbol de referencia de la última etapa del Capítulo 23 en un directorio de laboratorio:

```console
$ cp -r ~/myfirst-1.6-debug ~/myfirst-lab1
$ cd ~/myfirst-lab1
$ ls
Makefile  myfirst.c  myfirst.h  myfirst_debug.c  myfirst_debug.h
```

Si quieres comparar con el punto de partida ya migrado para la etapa 1 del capítulo (con `make_dev_s` ya aplicado), consulta `examples/part-05/ch24-integration/stage1-devfs/` como solución de referencia, no como directorio de inicio.

**Paso 1**: Abre `myfirst.c` y localiza la llamada existente a `make_dev`. El código del Capítulo 23 utiliza la forma antigua de llamada única. Sustitúyela por la forma `make_dev_args` de la Sección 2:

```c
struct make_dev_args args;
int error;

make_dev_args_init(&args);
args.mda_devsw = &myfirst_cdevsw;
args.mda_uid = UID_ROOT;
args.mda_gid = GID_WHEEL;
args.mda_mode = 0660;
args.mda_si_drv1 = sc;
args.mda_unit = device_get_unit(dev);

error = make_dev_s(&args, &sc->sc_cdev,
    "myfirst%d", device_get_unit(dev));
if (error != 0) {
        mtx_destroy(&sc->sc_mtx);
        return (error);
}
```

**Paso 2**: Añade `D_TRACKCLOSE` a los flags del cdevsw (ya debería tener `D_VERSION`):

```c
static struct cdevsw myfirst_cdevsw = {
        .d_version = D_VERSION,
        .d_flags   = D_TRACKCLOSE,
        .d_name    = "myfirst",
        .d_open    = myfirst_open,
        .d_close   = myfirst_close,
        .d_read    = myfirst_read,
        .d_write   = myfirst_write,
};
```

**Paso 3**: Confirma que `myfirst_open` y `myfirst_close` usan `dev->si_drv1` para recuperar el softc:

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        ...
}
```

**Paso 4**: Compila y carga:

```console
$ make
$ sudo kldload ./myfirst.ko
```

**Verificación**:

```console
$ ls -l /dev/myfirst0
crw-rw---- 1 root wheel 0x... <date> /dev/myfirst0
$ sudo cat /dev/myfirst0
Hello from myfirst
$ sudo kldstat | grep myfirst
N    1 0xffff... 1...    myfirst.ko
$ sudo dmesg | tail
... (debug messages from MYF_DBG_INIT)
```

Si `ls` muestra el propietario, el grupo o el modo incorrectos, revisa los valores `mda_uid`, `mda_gid` y `mda_mode`. Si `cat` devuelve una cadena vacía, comprueba que `myfirst_read` está llenando el buffer de usuario desde `sc->sc_msg`. Si la carga se realiza correctamente pero el dispositivo no aparece, comprueba que el cdevsw está referenciado desde `make_dev_args`.

**Limpieza**:

```console
$ sudo kldunload myfirst
$ ls -l /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
```

Una descarga satisfactoria elimina el nodo de dispositivo. Si la descarga falla con `Device busy`, comprueba que ningún shell o programa tenga el dispositivo abierto (`fstat | grep myfirst0`).

### Laboratorio 2: añadir la interfaz ioctl

**Objetivo**: ampliar el driver hasta la etapa 2 añadiendo los cuatro comandos ioctl de la Sección 3.

**Preparación**:

```console
$ cp -r examples/part-05/ch24-integration/stage1-devfs ~/myfirst-lab2
$ cd ~/myfirst-lab2
```

**Paso 1**: Crea `myfirst_ioctl.h` a partir de la plantilla de la Sección 3. Colócalo en el mismo directorio que los demás archivos fuente. Incluye `sys/ioccom.h` y `sys/types.h`. Define `MYFIRST_MSG_MAX = 256` y los cuatro números ioctl. No incluyas ningún header exclusivo del kernel.

**Paso 2**: Crea `myfirst_ioctl.c` a partir de la plantilla de la Sección 3. El dispatcher es una función única `myfirst_ioctl` con la firma estándar `d_ioctl_t`.

**Paso 3**: Añade `myfirst_ioctl.c` a `SRCS` en el `Makefile`:

```make
SRCS=   myfirst.c myfirst_debug.c myfirst_ioctl.c
```

**Paso 4**: Actualiza el cdevsw para que `.d_ioctl` apunte al nuevo dispatcher:

```c
.d_ioctl = myfirst_ioctl,
```

**Paso 5**: Añade `sc_msg` y `sc_msglen` al softc e inicialízalos en attach:

```c
strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
sc->sc_msglen = strlen(sc->sc_msg);
```

**Paso 6**: Compila el programa auxiliar en espacio de usuario. Coloca `myfirstctl.c` en el mismo directorio y crea un pequeño `Makefile.user`:

```make
CC?= cc
CFLAGS+= -Wall -Werror -I.

myfirstctl: myfirstctl.c myfirst_ioctl.h
        ${CC} ${CFLAGS} -o myfirstctl myfirstctl.c
```

(Ten en cuenta que la sangría debe ser un tabulador, no espacios, para que `make` pueda interpretar la regla.)

Compila el módulo del kernel y el programa auxiliar:

```console
$ make
$ make -f Makefile.user
$ sudo kldload ./myfirst.ko
```

**Verificación**:

```console
$ ./myfirstctl get-version
driver ioctl version: 1
$ ./myfirstctl get-message
Hello from myfirst
$ sudo ./myfirstctl set-message "drivers are fun"
$ ./myfirstctl get-message
drivers are fun
$ sudo ./myfirstctl reset
$ ./myfirstctl get-message

$
```

Si `set-message` devuelve `Permission denied`, el problema es que el dispositivo tiene el modo `0660` y el usuario no pertenece al grupo `wheel`. Ejecuta el comando con `sudo` (como hacen los comandos de verificación anteriores) o cambia el grupo del dispositivo a uno al que pertenezca el usuario mediante `mda_gid` y recarga el módulo.

Si `set-message` devuelve `Bad file descriptor`, el problema es que `myfirstctl` abrió el dispositivo en modo de solo lectura. Comprueba que el programa selecciona `O_RDWR` para `set-message` y `reset`.

Si algún ioctl devuelve `Inappropriate ioctl for device`, el problema es un desajuste entre la longitud codificada en `myfirst_ioctl.h` y la vista que tiene el dispatcher de los datos. Revisa los macros `_IOR`/`_IOW` y el tamaño de las estructuras que declaran.

**Limpieza**:

```console
$ sudo kldunload myfirst
```

### Laboratorio 3: añadir el árbol sysctl

**Objetivo**: ampliar el driver hasta la etapa 3 añadiendo los OID de sysctl de la Sección 4 y leyendo un tunable desde `/boot/loader.conf`.

**Preparación**:

```console
$ cp -r examples/part-05/ch24-integration/stage2-ioctl ~/myfirst-lab3
$ cd ~/myfirst-lab3
```

**Paso 1**: Crea `myfirst_sysctl.c` a partir de la plantilla de la Sección 4. La función `myfirst_sysctl_attach(sc)` construye el árbol completo.

**Paso 2**: Añade `myfirst_sysctl.c` a `SRCS` en el `Makefile`.

**Paso 3**: Actualiza `myfirst_attach` para que llame a `TUNABLE_INT_FETCH` y a `myfirst_sysctl_attach`:

```c
TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default", &sc->sc_debug);

/* ... after make_dev_s succeeds: */
myfirst_sysctl_attach(sc);
```

**Paso 4**: Compila y carga:

```console
$ make
$ sudo kldload ./myfirst.ko
```

**Verificación**:

```console
$ sysctl -a dev.myfirst.0
dev.myfirst.0.debug.classes: INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) ...
dev.myfirst.0.debug.mask: 0
dev.myfirst.0.message_len: 18
dev.myfirst.0.message: Hello from myfirst
dev.myfirst.0.total_writes: 0
dev.myfirst.0.total_reads: 0
dev.myfirst.0.open_count: 0
dev.myfirst.0.version: 1.7-integration
```

Abre el dispositivo una vez y vuelve a comprobar los contadores:

```console
$ cat /dev/myfirst0
Hello from myfirst
$ sysctl dev.myfirst.0.total_reads
dev.myfirst.0.total_reads: 1
```

Prueba el tunable en tiempo de carga. Edita `/boot/loader.conf` (haz primero una copia de seguridad):

```console
$ sudo cp /boot/loader.conf /boot/loader.conf.backup
$ sudo sh -c 'echo hw.myfirst.debug_mask_default=\"0x06\" >> /boot/loader.conf'
```

Ten en cuenta que esto solo surte efecto en el siguiente arranque y únicamente si el módulo es cargado por el loader (no mediante `kldload` tras el boot). Para una prueba interactiva sin reiniciar, establece el valor antes de cargar:

```console
$ sudo kenv hw.myfirst.debug_mask_default=0x06
$ sudo kldload ./myfirst.ko
$ sysctl dev.myfirst.0.debug.mask
dev.myfirst.0.debug.mask: 6
```

Si el valor es 0 en lugar de 6, comprueba que la llamada a `TUNABLE_INT_FETCH` usa la misma cadena que el comando `kenv`. La llamada debe ejecutarse antes que `myfirst_sysctl_attach` para que el valor esté disponible cuando se cree el OID.

**Limpieza**:

```console
$ sudo kldunload myfirst
$ sudo cp /boot/loader.conf.backup /boot/loader.conf
```

### Laboratorio 4: recorrer el ciclo de vida inyectando un fallo

**Objetivo**: observar cómo la cadena `goto err` en attach realmente se deshace al provocar deliberadamente un fallo en uno de los pasos.

**Preparación**:

```console
$ cp -r examples/part-05/ch24-integration/stage3-sysctl ~/myfirst-lab4
$ cd ~/myfirst-lab4
```

**Paso 1**: Abre `myfirst.c` y localiza `myfirst_attach`. Inserta un fallo deliberado justo después de que `make_dev_s` tenga éxito:

```c
error = make_dev_s(&args, &sc->sc_cdev,
    "myfirst%d", device_get_unit(dev));
if (error != 0)
        goto fail_mtx;

/* DELIBERATE FAILURE for Lab 4 */
device_printf(dev, "Lab 4: injected failure after make_dev_s\n");
error = ENXIO;
goto fail_cdev;

myfirst_sysctl_attach(sc);
return (0);

fail_cdev:
        destroy_dev(sc->sc_cdev);
fail_mtx:
        mtx_destroy(&sc->sc_mtx);
        return (error);
```

**Paso 2**: Compila e intenta cargar:

```console
$ make
$ sudo kldload ./myfirst.ko
kldload: an error occurred while loading module myfirst. Please check dmesg(8) for more details.
$ sudo dmesg | tail
myfirst0: Lab 4: injected failure after make_dev_s
```

**Verificación**:

```console
$ ls /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
$ kldstat | grep myfirst
$
```

El cdev ha desaparecido (la limpieza de `goto fail_cdev` lo ha destruido), el módulo no está cargado y no ha habido fuga de recursos. Si el cdev permanece tras el fallo, la limpieza no tiene la llamada a `destroy_dev`. Si el kernel entra en panic en el siguiente intento de cargar el módulo, la limpieza está liberando o destruyendo algo dos veces.

**Bonus**: cambia la inyección del fallo para que ocurra **antes** de `make_dev_s`. La cadena de limpieza debería ahora omitir la etiqueta `fail_cdev` y ejecutar únicamente `fail_mtx`. Verifica que el cdev nunca fue creado y que el mutex se destruye:

```console
$ sudo kldload ./myfirst.ko
$ sudo dmesg | tail
... no Lab 4 message because it now runs before make_dev_s ...
```

**Limpieza**:

Elimina el bloque de fallo deliberado antes de continuar.

### Laboratorio 5: trazar las superficies de integración con DTrace

**Objetivo**: utilizar los probes SDT del Capítulo 23 para trazar el tráfico de ioctl, open, close y read en tiempo real.

**Preparación**: driver de etapa 3 cargado como en el Laboratorio 3.

**Paso 1**: Verifica que los probes son visibles para DTrace:

```console
$ sudo dtrace -l -P myfirst
   ID   PROVIDER      MODULE    FUNCTION   NAME
... id  myfirst       kernel    -          open
... id  myfirst       kernel    -          close
... id  myfirst       kernel    -          io
... id  myfirst       kernel    -          ioctl
```

Si la lista está vacía, los probes SDT no están registrados. Comprueba que `myfirst_debug.c` está en `SRCS` y que `SDT_PROBE_DEFINE*` se llama desde allí.

**Paso 2**: Abre una traza de larga duración en un terminal:

```console
$ sudo dtrace -n 'myfirst:::ioctl { printf("ioctl cmd=0x%x flags=0x%x", arg1, arg2); }'
dtrace: description 'myfirst:::ioctl' matched 1 probe
```

**Paso 3**: Ejercita el driver en otro terminal:

```console
$ ./myfirstctl get-version
$ ./myfirstctl get-message
$ sudo ./myfirstctl set-message "Lab 5"
$ sudo ./myfirstctl reset
```

El terminal de DTrace debería mostrar una línea por ioctl con el código de comando y los flags del archivo.

**Paso 4**: Combina múltiples probes en un solo script:

```console
$ sudo dtrace -n '
    myfirst:::open  { printf("open  pid=%d", pid); }
    myfirst:::close { printf("close pid=%d", pid); }
    myfirst:::io    { printf("io    pid=%d write=%d resid=%d", pid, arg1, arg2); }
    myfirst:::ioctl { printf("ioctl pid=%d cmd=0x%x", pid, arg1); }
'
```

En otro terminal:

```console
$ cat /dev/myfirst0
$ ./myfirstctl get-version
$ echo "hello" | sudo tee /dev/myfirst0
```

La salida de DTrace muestra ahora el patrón completo de tráfico, con el open y el close alrededor de cada operación, el read o write en su interior y cualquier ioctl. Este es el valor de tener probes SDT integrados con los callbacks del cdevsw: cada superficie de integración que expone el driver es también una superficie de probe para DTrace.

**Limpieza**:

```console
^C
$ sudo kldunload myfirst
```

### Laboratorio 6: prueba de humo de integración

**Objetivo**: construir un script de shell que ejercite cada superficie de integración en una sola ejecución y genere un resumen en verde/rojo que el lector pueda incluir en un informe de errores o en una lista de comprobación de preparación para un lanzamiento.

Una prueba de humo es una comprobación pequeña, rápida y de extremo a extremo que verifica que el driver está activo y que cada superficie responde. No reemplaza las pruebas unitarias rigurosas; le ofrece al lector una confirmación de cinco segundos de que nada está visiblemente roto antes de invertir más tiempo. Los drivers reales tienen pruebas de humo; el capítulo recomienda añadir una a cada nuevo driver desde el primer día.

**Preparación**: driver de etapa 3 cargado.

**Paso 1**: Crea `smoke.sh` en el directorio de trabajo:

```sh
#!/bin/sh
# smoke.sh - end-to-end smoke test for the myfirst driver.

set -u
fail=0

check() {
        if eval "$1"; then
                printf "  PASS  %s\n" "$2"
        else
                printf "  FAIL  %s\n" "$2"
                fail=$((fail + 1))
        fi
}

echo "=== myfirst integration smoke test ==="

# 1. Module is loaded.
check "kldstat | grep -q myfirst" "module is loaded"

# 2. /dev node exists with the right mode.
check "test -c /dev/myfirst0" "/dev/myfirst0 exists as a character device"
check "test \"\$(stat -f %Lp /dev/myfirst0)\" = \"660\"" "/dev/myfirst0 is mode 0660"

# 3. Sysctl tree is present.
check "sysctl -N dev.myfirst.0.version >/dev/null 2>&1" "version OID is present"
check "sysctl -N dev.myfirst.0.debug.mask >/dev/null 2>&1" "debug.mask OID is present"
check "sysctl -N dev.myfirst.0.open_count >/dev/null 2>&1" "open_count OID is present"

# 4. Ioctls work (requires myfirstctl built).
check "./myfirstctl get-version >/dev/null" "MYFIRSTIOC_GETVER returns success"
check "./myfirstctl get-message >/dev/null" "MYFIRSTIOC_GETMSG returns success"
check "sudo ./myfirstctl set-message smoke && [ \"\$(./myfirstctl get-message)\" = smoke ]" "MYFIRSTIOC_SETMSG round-trip works"
check "sudo ./myfirstctl reset && [ -z \"\$(./myfirstctl get-message)\" ]" "MYFIRSTIOC_RESET clears state"

# 5. Read/write basic path.
check "echo hello | sudo tee /dev/myfirst0 >/dev/null" "write to /dev/myfirst0 succeeds"
check "[ \"\$(cat /dev/myfirst0)\" = hello ]" "read returns the previously written message"

# 6. Counters update.
sudo ./myfirstctl reset >/dev/null
cat /dev/myfirst0 >/dev/null
check "[ \"\$(sysctl -n dev.myfirst.0.total_reads)\" = 1 ]" "total_reads incremented after one read"

# 7. SDT probes are registered.
check "sudo dtrace -l -P myfirst | grep -q open" "myfirst:::open SDT probe is visible"

echo "=== summary ==="
if [ $fail -eq 0 ]; then
        echo "ALL PASS"
        exit 0
else
        printf "%d FAIL\n" "$fail"
        exit 1
fi
```

**Paso 2**: Hazlo ejecutable y ejecútalo:

```console
$ chmod +x smoke.sh
$ ./smoke.sh
=== myfirst integration smoke test ===
  PASS  module is loaded
  PASS  /dev/myfirst0 exists as a character device
  PASS  /dev/myfirst0 is mode 0660
  PASS  version OID is present
  PASS  debug.mask OID is present
  PASS  open_count OID is present
  PASS  MYFIRSTIOC_GETVER returns success
  PASS  MYFIRSTIOC_GETMSG returns success
  PASS  MYFIRSTIOC_SETMSG round-trip works
  PASS  MYFIRSTIOC_RESET clears state
  PASS  write to /dev/myfirst0 succeeds
  PASS  read returns the previously written message
  PASS  total_reads incremented after one read
  PASS  myfirst:::open SDT probe is visible
=== summary ===
ALL PASS
```

Si alguna comprobación falla, la salida del script apunta directamente a la superficie de integración rota. Un fallo en `version OID is present` indica que la construcción del sysctl no se ejecutó; un fallo en `MYFIRSTIOC_GETVER` indica que el dispatcher del ioctl no está cableado correctamente; un fallo en `total_reads incremented` indica que el callback de lectura no está incrementando el contador bajo el mutex.

**Verificación**: vuelve a ejecutarlo tras cada cambio en el driver. Una prueba de humo que pase antes de un commit es el seguro más barato posible contra una regresión que rompa el flujo básico.

### Laboratorio 7: recargar sin reiniciar programas en espacio de usuario

**Objetivo**: confirmar que el driver puede descargarse y recargarse mientras un programa en espacio de usuario mantiene un descriptor de archivo abierto en otro terminal.

Esta prueba revela errores en el ciclo de vida que el patrón de «detach suave» del capítulo está diseñado para prevenir. Un driver que devuelve `EBUSY` desde detach cuando un usuario mantiene el dispositivo abierto se está defendiendo correctamente; un driver que permite que detach tenga éxito y luego entra en panic cuando el usuario emite un ioctl está roto.

**Preparación**: driver de etapa 3 cargado.

**Paso 1** (terminal 1): mantén el dispositivo abierto con un comando de larga duración:

```console
$ sleep 3600 < /dev/myfirst0 &
$ jobs
[1]+ Running                 sleep 3600 < /dev/myfirst0 &
```

**Paso 2** (terminal 2): intenta descargarlo:

```console
$ sudo kldunload myfirst
kldunload: can't unload file: Device busy
```

Este es el comportamiento esperado. El `myfirst_detach` del capítulo comprueba `open_count > 0` y devuelve `EBUSY` en lugar de desmontar el cdev bajo un descriptor de archivo abierto.

**Paso 3** (terminal 2): verifica que el dispositivo sigue funcionando desde otro shell:

```console
$ ./myfirstctl get-version
driver ioctl version: 1
$ sysctl dev.myfirst.0.open_count
dev.myfirst.0.open_count: 1
```

El contador de aperturas refleja el descriptor de archivo retenido.

**Paso 4** (terminal 1): libera el descriptor de archivo:

```console
$ kill %1
$ wait
```

**Paso 5** (terminal 2): ahora la descarga tiene éxito:

```console
$ sudo kldunload myfirst
$ sysctl dev.myfirst.0
sysctl: unknown oid 'dev.myfirst.0'
```

El OID ha desaparecido porque Newbus destruyó el contexto sysctl por dispositivo una vez que detach retornó correctamente.

**Verificación**: la descarga debe tener éxito en todo momento sin provocar un pánico del kernel. Si el kernel entra en pánico durante el paso 5, la causa casi siempre es que los callbacks del cdev siguen en ejecución cuando `destroy_dev` retorna; comprueba que el `d_close` del cdevsw libera correctamente todo lo que adquirió en `d_open`, y comprueba que no queda ningún callout ni taskqueue pendiente.

Como extensión adicional, escribe un pequeño programa que abra el dispositivo, llame inmediatamente a `MYFIRSTIOC_RESET` y luego itera llamando a `MYFIRSTIOC_GETVER` durante varios segundos. Mientras el bucle está en ejecución, intenta descargar el módulo desde otro terminal. La descarga debe seguir fallando con `EBUSY`; los ioctls en vuelo no deberían corromper nada.

### Cerrando los laboratorios

Los siete laboratorios guiaron al lector a través de toda la superficie de integración, la disciplina del ciclo de vida, la prueba de humo y el contrato de soft-detach. El paso 1 añadió el cdev; el paso 2 añadió la interfaz ioctl; el paso 3 añadió el árbol sysctl; el laboratorio sobre el ciclo de vida (Lab 4) confirmó el desenrollado; el laboratorio de DTrace (Lab 5) confirmó la integración con la infraestructura de depuración del Capítulo 23; la prueba de humo (Lab 6) proporcionó al lector un script de verificación reutilizable; y el laboratorio de recarga (Lab 7) confirmó el contrato de soft-detach.

Un driver que supera los siete laboratorios se encuentra en la versión de hito del capítulo, `1.7-integration`, y está listo para el tema del siguiente capítulo. Los ejercicios de desafío de la Sección 10 ofrecen trabajo adicional opcional que amplía el driver más allá de lo que cubre el capítulo.

## Ejercicios de desafío

Los desafíos que se presentan a continuación son opcionales y están pensados para el lector que quiera llevar el driver más allá del hito del capítulo. Cada desafío tiene un objetivo declarado, algunas pistas sobre el enfoque y una nota sobre qué secciones del capítulo contienen el material relevante. Ninguno de los desafíos tiene una única respuesta correcta; se anima a los lectores a comparar su solución con un revisor o con los drivers del árbol de código fuente citados como referencias.

### Desafío 1: añadir un ioctl de longitud variable

**Objetivo**: ampliar la interfaz ioctl para que un programa en espacio de usuario pueda transferir un buffer de mayor tamaño que los 256 bytes fijos que usa `MYFIRSTIOC_SETMSG`.

El patrón del capítulo es de tamaño fijo: `MYFIRSTIOC_SETMSG` declara `_IOW('M', 3, char[256])` y el kernel gestiona todo el copyin automáticamente. Para buffers más grandes (por ejemplo, hasta 1 MB), se necesita el patrón de puntero embebido:

```c
struct myfirst_blob {
        size_t  len;
        char   *buf;    /* user-space pointer */
};
#define MYFIRSTIOC_SETBLOB _IOW('M', 5, struct myfirst_blob)
```

El dispatcher debe llamar a `copyin` para transferir los bytes a los que apunta el puntero; la propia estructura llega mediante el copyin automático, como antes. Pistas: impón una longitud máxima (1 MB es razonable). Asigna un buffer temporal en el kernel con `malloc(M_TEMP, len, M_WAITOK)`; no lo asignes dentro del mutex de softc. Libéralo antes de retornar. Referencia: Sección 3, "Errores comunes con ioctl", segundo problema habitual.

Una ampliación adicional consiste en añadir `MYFIRSTIOC_GETBLOB` que copie el mensaje actual con el mismo formato de longitud variable; presta atención al caso en que el buffer proporcionado por el usuario es más corto que el mensaje y decide si truncar, devolver `ENOMEM` o escribir de vuelta la longitud requerida. Los drivers reales (`SIOCGIFCAP`, `KIOCGRPC`) usan este último patrón.

### Desafío 2: añadir un contador por apertura

**Objetivo**: mantener un contador por descriptor de archivo (uno por cada apertura de `/dev/myfirst0`) en lugar del contador por instancia que tenemos ahora.

El `sc_open_count` del capítulo agrega las aperturas de todos los descriptores. Un contador por apertura permitiría a un programa saber cuánto ha leído desde su propio descriptor. Pistas: usa `cdevsw->d_priv` para adjuntar una estructura por fd (una `struct myfirst_fdpriv` que contenga un contador). Asigna la estructura en `myfirst_open` y libérala en `myfirst_close`. El framework asigna a cada `cdev_priv` un puntero único en el campo `f_data` del archivo; las retrollamadas de lectura y escritura pueden entonces buscar la estructura por fd mediante `devfs_get_cdevpriv()`.

Referencia: `/usr/src/sys/kern/kern_conf.c` para `devfs_set_cdevpriv` y `devfs_get_cdevpriv`. El patrón también lo usa `/usr/src/sys/dev/random/random_harvestq.c`.

Una ampliación adicional consiste en añadir un OID sysctl que informe de la suma de los contadores por fd y verificar que coincide en todo momento con el contador agregado existente. Las discrepancias indican que falta un incremento en algún lugar.

### Desafío 3: implementar soft detach con `dev_ref`

**Objetivo**: reemplazar el patrón del capítulo "rechazar el detach si hay una apertura" por el patrón más limpio "esperar al último cierre y luego hacer el detach".

El detach del capítulo devuelve `EBUSY` si algún usuario tiene el dispositivo abierto. Un patrón más elegante usa `dev_ref`/`dev_rel` para contar las referencias pendientes y espera a que el contador llegue a cero antes de completar el detach. Pistas: toma un `dev_ref` en `myfirst_open` y suéltalo en `myfirst_close`. En detach, activa un flag que indique que el dispositivo está en proceso de cierre, y llama a `destroy_dev_drain` (o escribe un pequeño bucle que llame a `tsleep` mientras `dev_refs > 0`) antes de llamar a `destroy_dev`. Una vez que el contador llega a cero y el cdev se destruye, completa el detach normalmente.

Referencia: `/usr/src/sys/kern/kern_conf.c` para el mecanismo `dev_ref`; `/usr/src/sys/fs/cuse` es un driver real que usa el patrón de drenaje para el detach bloqueante.

La ampliación adicional consiste en añadir un OID sysctl que informe del recuento de referencias actual y verificar que coincide con el recuento de aperturas.

### Desafío 4: reemplazar la letra mágica estática

**Objetivo**: reemplazar la letra mágica `'M'` codificada de forma fija en `myfirst_ioctl.h` por un nombre que no entre en conflicto con nada más del árbol de código fuente.

El capítulo eligió `'M'` de forma arbitraria y advirtió sobre el riesgo de colisiones. Un driver más defensivo usa un identificador mágico más largo y construye los números ioctl a partir de él. Pistas: define `MYFIRST_IOC_GROUP = 0x83` (o cualquier byte que no use otro driver). El macro `_IOC` toma esa constante en lugar de un literal de carácter. Documenta la elección con un comentario en el encabezado que explique cómo se eligió.

Una ampliación adicional consiste en hacer un grep en `/usr/src/sys` buscando `_IO[RW]?\\(.\\?'M'` y producir una lista de todos los usos existentes de `'M'`. (Hay varios, entre ellos los ioctls de `MIDI` y otros; el propio análisis resulta muy instructivo.)

### Desafío 5: añadir un `EVENTHANDLER` para el apagado

**Objetivo**: hacer que el driver se comporte de forma correcta cuando el sistema se apague.

El driver del capítulo no tiene ningún manejador de apagado; si el sistema se apaga con `myfirst` cargado, el framework acaba llamando a detach. Un driver más pulido registra un `EVENTHANDLER` para `shutdown_pre_sync` de manera que pueda vaciar cualquier estado en curso antes de que el sistema de archivos pase a modo de solo lectura.

Pistas: registra el manejador en attach con `EVENTHANDLER_REGISTER(shutdown_pre_sync, ...)`. El manejador se invoca en la etapa de apagado correspondiente. Cancela el registro en detach con `EVENTHANDLER_DEREGISTER`. Dentro del manejador, coloca el driver en un estado quiescente (borra el mensaje, pone a cero los contadores); en ese momento el sistema de archivos sigue siendo escribible, por lo que cualquier mensaje de usuario enviado mediante `printf` quedará registrado en `/var/log/messages` tras el siguiente arranque.

Referencia: Sección 7, "EVENTHANDLER para eventos del sistema" y `/usr/src/sys/sys/eventhandler.h` para la lista completa de eventos con nombre.

### Desafío 6: un segundo subárbol sysctl por driver

**Objetivo**: añadir un segundo subárbol bajo `dev.myfirst.0` que exponga estadísticas por thread.

El árbol del capítulo tiene un subárbol `debug.`. Un driver completo podría tener también un subárbol `stats.` (para estadísticas de lectura y escritura desglosadas por descriptor de archivo) o un subárbol `errors.` (para contadores de errores). Pistas: usa `SYSCTL_ADD_NODE` para crear un nuevo nodo y luego `SYSCTL_ADD_*` para poblarlo bajo el `SYSCTL_CHILDREN` del nuevo nodo. El patrón es idéntico al del subárbol `debug.` pero con raíz en un nombre diferente.

Referencia: Sección 4, "El árbol sysctl de `myfirst`" como modelo del subárbol `debug.` existente; `/usr/src/sys/dev/iicbus` para varios drivers que usan diseños sysctl con múltiples subárboles.

### Desafío 7: dependencia entre módulos

**Objetivo**: construir un segundo módulo pequeño (`myfirst_logger`) que dependa de `myfirst` y use su API en el kernel.

El driver `myfirst` del capítulo no exporta ningún símbolo para usuarios en el kernel. Añadir un segundo módulo que llame a `myfirst` ejercita el mecanismo `MODULE_DEPEND`. Pistas: declara una función que exponga un símbolo en `myfirst.h` (por ejemplo, `int myfirst_get_message(int unit, char *buf, size_t len)`) e impleméntala en `myfirst.c`. Construye el segundo módulo con `MODULE_DEPEND(myfirst_logger, myfirst, 1, 1, 1)` para que el kernel cargue `myfirst` automáticamente cuando se cargue `myfirst_logger`.

Una ampliación adicional consiste en incrementar la versión del módulo de `myfirst` a 2, cambiar la API en el kernel de forma no retrocompatible y observar que el segundo módulo no consigue cargarse hasta que se recompile contra la nueva versión. Referencia: Sección 8, "Cadenas de versión, números de versión y la versión de la API".

### Cerrando los desafíos

Los siete desafíos van desde los cortos (el Desafío 4 es principalmente un renombrado y un comentario) hasta los sustanciales (el Desafío 3 requiere leer y comprender `dev_ref`). El lector que complete los siete tendrá experiencia práctica con cada uno de los aspectos de integración que el capítulo solo esboza. El lector que complete cualquiera de ellos tendrá una comprensión más profunda de la disciplina de integración de la que el capítulo por sí solo puede ofrecer.

## Resolución de problemas

Las superficies de integración de este capítulo se encuentran en la frontera entre el kernel y el resto del sistema. Los problemas en esa frontera a menudo parecen errores del driver pero en realidad son síntomas de un flag que falta, una errata en un encabezado o un malentendido sobre quién es propietario de qué. El catálogo que se muestra a continuación recoge los síntomas más comunes, sus causas probables y la solución para cada uno.

### `/dev/myfirst0` no aparece tras `kldload`

Lo primero que hay que comprobar es si el módulo se cargó correctamente:

```console
$ kldstat | grep myfirst
```

Si el módulo no aparece en la lista, la carga falló; consulta `dmesg` para obtener un mensaje más específico. La razón más habitual es un símbolo sin resolver (normalmente porque el nuevo archivo fuente no está en `SRCS`).

Si el módulo aparece en la lista pero falta el nodo de dispositivo, es probable que la llamada a `make_dev_s` dentro de `myfirst_attach` haya fallado. Añade un `device_printf(dev, "make_dev_s returned %d\n", error)` junto a la llamada y vuelve a intentarlo. La razón más habitual para que se devuelva un valor distinto de cero es que otro driver ya haya creado `/dev/myfirst0` (el kernel no sobreescribirá silenciosamente un nodo existente) o que `make_dev_s` se haya llamado desde un contexto no bloqueante con `MAKEDEV_NOWAIT`.

Una razón más sutil es que `cdevsw->d_version` no sea igual a `D_VERSION`. El kernel lo comprueba y se niega a registrar un cdevsw con un desajuste de versión. La solución es `static struct cdevsw myfirst_cdevsw = { .d_version = D_VERSION, ... };` exactamente así.

### `cat /dev/myfirst0` devuelve "Permission denied"

El dispositivo existe pero el usuario no puede abrirlo. El modo predeterminado en este capítulo es `0660` y el grupo predeterminado es `wheel`. Ejecuta el comando con `sudo`, cambia `mda_gid` al grupo del usuario o cambia `mda_mode` a `0666` (esto último es aceptable para un módulo didáctico, pero es una mala elección para un driver de producción porque cualquier usuario local podría abrir el dispositivo).

### `ioctl` devuelve "Inappropriate ioctl for device"

El kernel devolvió `ENOTTY`, lo que significa que no pudo hacer coincidir el código de solicitud con ningún cdevsw. Las dos causas más comunes son:

* El dispatcher del driver devolvió `ENOIOCTL` para el comando. El kernel traduce `ENOIOCTL` a `ENOTTY` para el espacio de usuario. La solución es añadir un caso para el comando en la sentencia switch del dispatcher.

* La longitud codificada en el código de solicitud no coincide con el tamaño real del buffer que usa el programa. Esto ocurre después de refactorizar un encabezado en el que se editó la línea `_IOR` pero el programa en espacio de usuario no se recompiló con el nuevo encabezado. La solución es recompilar el programa con el encabezado actual y reconstruir el módulo con el mismo código fuente.

### `ioctl` devuelve "Bad file descriptor"

El dispatcher devolvió `EBADF`, que es el patrón del capítulo para indicar que el archivo no se abrió con los flags correctos para ese comando. La solución es abrir el dispositivo con `O_RDWR` en lugar de `O_RDONLY` para cualquier comando que modifique estado. El programa `myfirstctl` ya hace esto; un programa personalizado puede no hacerlo.

### `sysctl dev.myfirst.0` muestra el árbol pero las lecturas devuelven "operation not supported"

Esto normalmente significa que el OID sysctl se añadió con un puntero de manejador obsoleto o inválido. Si la lectura retorna inmediatamente con `EOPNOTSUPP` (95), la causa es casi siempre que el OID se registró con `CTLTYPE_OPAQUE` y un manejador que no llama a `SYSCTL_OUT`. La solución es usar uno de los helpers tipados `SYSCTL_ADD_*` (`SYSCTL_ADD_UINT`, `SYSCTL_ADD_STRING`, `SYSCTL_ADD_PROC` con la cadena de formato correcta) para que el framework sepa qué hacer en una lectura.

### `sysctl -w dev.myfirst.0.foo=value` falla con "permission denied"

El OID probablemente fue creado con `CTLFLAG_RD` (solo lectura) cuando se pretendía usar la variante escribible `CTLFLAG_RW`. Comprueba de nuevo el campo de flags en la llamada a `SYSCTL_ADD_*` y vuelve a compilar.

Si el flag es correcto y el fallo persiste, puede que no estés ejecutando como root. Las escrituras por sysctl requieren el privilegio `PRIV_SYSCTL` de forma predeterminada; utiliza `sudo` para la escritura.

### `sysctl` se cuelga o provoca un deadlock

El manejador del OID está tomando el giant lock (porque falta `CTLFLAG_MPSAFE`) al mismo tiempo que otro thread mantiene el giant lock y llama al driver. La solución es añadir `CTLFLAG_MPSAFE` a la palabra de flags de cada OID. Los kernels modernos asumen MPSAFE en todos los sitios; la ausencia del flag es un problema de revisión de código.

Una causa más sutil es un manejador que toma el mutex del softc mientras otro thread mantiene ese mismo mutex y está leyendo de sysctl. Audita el manejador: debe calcular el valor bajo el mutex, pero llamar a `sysctl_handle_*` fuera del mutex. La función `myfirst_sysctl_message_len` del capítulo sigue este patrón.

### `kldunload myfirst` falla con "Device busy"

El detach se negó porque algún usuario mantiene el dispositivo abierto. Encuéntраlos con `fstat | grep myfirst0` y pídeles que lo cierren, o termina el proceso. Una vez que liberen el dispositivo, la descarga tendrá éxito.

Si `fstat` no muestra nada y la descarga sigue fallando, la causa más probable es un `dev_ref` que no se ha liberado. Comprueba que cada ruta de código del driver que toma un `dev_ref` también llama a `dev_rel`; en particular, cualquier ruta de error dentro de `myfirst_open` debe liberar cualquier referencia tomada antes del fallo.

### `kldunload myfirst` provoca un kernel panic

El detach del driver está destruyendo o liberando algo que el kernel todavía está usando. Las dos causas más habituales son:

* El detach liberó el softc antes de destruir el cdev. Los callbacks del cdev pueden estar todavía en vuelo; desreferencian `si_drv1`, obtienen basura y provocan un panic. La solución es respetar el orden estricto: `destroy_dev` (que drena los callbacks en vuelo) primero, luego mutex_destroy, luego return; el framework libera el softc.

* El detach olvidó dar de baja un manejador de eventos. El siguiente evento se dispara tras la descarga y salta a memoria liberada. La solución es llamar a `EVENTHANDLER_DEREGISTER` por cada `EVENTHANDLER_REGISTER` realizado en attach.

Los mensajes `Lock order reversal` y `WITNESS` en `dmesg` son diagnósticos útiles para ambos casos. Un panic con `page fault while in kernel mode` y un valor `%rip` corrupto corresponde al segundo patrón; un panic con `lock order reversal` y una traza de pila a través de ambos subsistemas corresponde al primero.

### Las sondas de DTrace no son visibles

`dtrace -l -P myfirst` no devuelve nada aunque el módulo esté cargado. La causa es casi siempre que las sondas SDT se declaran en un encabezado pero no se definen en ningún sitio. Las sondas necesitan tanto `SDT_PROBE_DECLARE` (en el encabezado, donde los consumidores las ven) como `SDT_PROBE_DEFINE*` (en exactamente un archivo fuente, que posee el almacenamiento de las sondas). El patrón del capítulo sitúa las definiciones en `myfirst_debug.c`. Si ese archivo no está en `SRCS`, las sondas no se definirán y DTrace no verá nada.

Una causa más sutil es que la sonda SDT fue renombrada en el encabezado pero el `SDT_PROBE_DEFINE*` correspondiente no se actualizó. El build sigue teniendo éxito porque las dos declaraciones hacen referencia a símbolos diferentes, pero DTrace solo ve el nombre definido. Audita el encabezado y el fuente para verificar que el nombre de la sonda coincide en ambos sitios.

### El árbol sysctl sobrevive a la descarga pero bloquea el siguiente sysctl

Esto ocurre cuando el driver usa su propio contexto sysctl (en lugar del de por dispositivo) y olvida llamar a `sysctl_ctx_free` en detach. Los OIDs hacen referencia a campos del softc ahora liberado; el siguiente recorrido de `sysctl` desreferencia la memoria liberada y el kernel entra en panic o devuelve basura. La solución es cambiar a `device_get_sysctl_ctx`, que el framework limpia automáticamente.

### Lista de comprobación diagnóstica general

Cuando algo falla y la causa no es obvia, repasa esta lista antes de recurrir a `kgdb`:

1. `kldstat | grep <driver>`: ¿está el módulo realmente cargado?
2. `dmesg | tail`: ¿hay mensajes del kernel que mencionen el driver?
3. `ls -l /dev/<driver>0`: ¿existe el nodo de dispositivo con el modo esperado?
4. `sysctl dev.<driver>.0.%driver`: ¿es Newbus consciente del dispositivo?
5. `fstat | grep <driver>0`: ¿alguien mantiene el dispositivo abierto?
6. `dtrace -l -P <driver>`: ¿están registradas las sondas SDT?
7. Vuelve a leer la función attach y comprueba que cada paso tiene su correspondiente limpieza en detach.

Los primeros seis comandos llevan diez segundos y descartan la gran mayoría de los problemas habituales. El séptimo es el lento, pero casi siempre es la respuesta definitiva para cualquier bug que los seis primeros no hayan revelado.

### Preguntas frecuentes

Las preguntas siguientes surgen con suficiente frecuencia durante el trabajo de integración como para que el capítulo acabe con un breve FAQ. Cada respuesta es intencionadamente concisa; la sección correspondiente del capítulo contiene la discusión completa.

**P1. ¿Por qué usar tanto ioctl como sysctl cuando parecen solaparse?**

Responden a preguntas distintas. Ioctl es para un programa que ya ha abierto el dispositivo y quiere emitir un comando (solicitar un estado, enviar un nuevo estado, desencadenar una acción). Sysctl es para un operador en el prompt del shell o un script que quiere inspeccionar o ajustar el estado sin abrir nada. El mismo valor puede exponerse a través de ambas interfaces, y muchos drivers de producción hacen exactamente eso: un `MYFIRSTIOC_GETMSG` para programas y un `dev.myfirst.0.message` para personas. Cada usuario elige el canal que se ajusta a su contexto.

**P2. ¿Cuándo debería usar mmap en lugar de read/write/ioctl?**

Usa `mmap` cuando los datos son grandes, se accede a ellos de forma aleatoria y residen de forma natural en una dirección de memoria (un frame buffer, un anillo de descriptores DMA, un espacio de registros mapeado en memoria). Usa `read`/`write` cuando los datos son secuenciales, orientados a bytes y pequeños por llamada. Usa `ioctl` para comandos de control. Los tres no son excluyentes; muchos drivers exponen los tres (como hace `vt(4)` para la consola).

**P3. ¿Por qué el capítulo usa `make_dev_s` en lugar de `make_dev`?**

`make_dev_s` es la forma moderna preferida. Devuelve un error explícito en lugar de entrar en panic ante un nombre duplicado; acepta una estructura de argumentos para que puedan añadirse nuevas opciones sin cambios traumáticos; y es lo que usa la mayoría de los drivers actuales. El antiguo `make_dev` sigue funcionando, pero se desaconseja para código nuevo.

**P4. ¿Necesito declarar `D_TRACKCLOSE`?**

Lo necesitas si el `d_close` de tu driver debe llamarse solo en el último cierre de un descriptor de archivo (el significado natural de «close»). Sin él, el kernel llama a `d_close` en cada cierre de cada descriptor duplicado, lo que sorprende a la mayoría de los drivers. Establécelo en cualquier cdevsw nuevo, salvo que tengas una razón concreta para no hacerlo.

**P5. ¿Cuándo debería incrementar `MODULE_VERSION`?**

Cuando algo en la API interna del driver cambia de forma incompatible. Añadir nuevos símbolos exportados está bien; renombrarlos o eliminarlos exige incrementarlo. Cambiar el diseño de una estructura visible públicamente exige incrementarlo. Incrementar la versión del módulo obliga a reconstruir los dependientes (consumidores de `MODULE_DEPEND`).

**P6. ¿Cuándo debería incrementar la constante de versión de la API en mi encabezado público?**

Cuando algo en la interfaz visible para el usuario cambia de forma incompatible. Añadir un nuevo ioctl está bien; cambiar el diseño de la estructura de argumentos de un ioctl existente exige incrementarlo. Renumerar un ioctl existente exige incrementarlo. Incrementar la versión de la API permite a los programas del espacio de usuario detectar la incompatibilidad antes de realizar llamadas.

**P7. ¿Debo dar de baja mis OIDs en `myfirst_detach`?**

No, si usaste `device_get_sysctl_ctx` (el contexto por dispositivo). El framework limpia el contexto por dispositivo automáticamente tras un detach exitoso. Solo necesitas limpieza explícita si usaste `sysctl_ctx_init` para crear tu propio contexto.

**P8. ¿Por qué mi detach entra en panic con «invalid memory access»?**

Casi siempre porque los callbacks del cdev todavía están en vuelo cuando el driver liberó algo que ellos referencian. La solución es llamar primero a `destroy_dev(sc->sc_cdev)`; `destroy_dev` bloquea hasta que todos los callbacks en vuelo retornan. Tras su retorno, el cdev ha desaparecido y no pueden llegar nuevos callbacks. Solo entonces es seguro liberar el softc, liberar los locks, etc. El orden estricto es innegociable.

**P9. ¿Cuál es la diferencia entre `dev_ref` / `dev_rel` y `D_TRACKCLOSE`?**

`D_TRACKCLOSE` es un flag de cdevsw que controla cuándo llama el kernel a `d_close`: con él, solo en el último cierre; sin él, en cada cierre. `dev_ref`/`dev_rel` es un mecanismo de conteo de referencias que permite al driver retrasar el detach hasta que se liberen las referencias pendientes. Son independientes y complementarios. El capítulo usa `D_TRACKCLOSE` en la etapa 1; el ejercicio de desafío 3 demuestra `dev_ref`/`dev_rel`.

**P10. ¿Por qué mi escritura en sysctl devuelve EPERM aunque sea root?**

Hay tres causas posibles. (a) El OID se creó solo con `CTLFLAG_RD`; añade `CTLFLAG_RW`. (b) El OID tiene `CTLFLAG_SECURE` y el sistema está con `securelevel > 0`; baja el securelevel o elimina el flag. (c) El usuario no es realmente root, sino que está en una jaula sin `allow.sysvipc` u opción similar; root dentro de una jaula no tiene `PRIV_SYSCTL` para OIDs arbitrarios.

**P11. Mi manejador de sysctl toma el giant lock cuando no debería. ¿Qué olvidé?**

`CTLFLAG_MPSAFE` en la palabra de flags. Sin él, el kernel toma el giant lock alrededor de cada llamada al manejador. Añádelo en todos los sitios; los kernels modernos asumen MPSAFE en todos los sitios.

**P12. ¿Debo nombrar la letra de grupo de mi ioctl en mayúsculas o minúsculas?**

En mayúsculas para los drivers nuevos. Las letras minúsculas están muy usadas por los subsistemas base (`'d'` para disco, `'i'` para `if_ioctl`, `'t'` para terminal) y la posibilidad de colisión es real. Las mayúsculas están mayoritariamente libres, y un driver nuevo debería elegir una de ellas.

**P13. Mi ioctl devuelve `Inappropriate ioctl for device` y no entiendo por qué.**

El kernel devolvió `ENOTTY` porque (a) el dispatcher devolvió `ENOIOCTL` para el comando (añade un case para él), o (b) la longitud codificada en el código de solicitud no coincide con el buffer que pasó el usuario (recompila ambos lados contra el mismo encabezado).

**P14. ¿Debo usar `strncpy` o `strlcpy` en el kernel?**

`strlcpy`. Garantiza la terminación NUL y nunca desborda el destino. `strncpy` no garantiza ninguna de las dos cosas y es una fuente frecuente de bugs sutiles. La página de manual `style(9)` de FreeBSD recomienda `strlcpy` para todo el código nuevo.

**P15. Mi módulo se carga pero `dmesg` no muestra mensajes de mi driver. ¿Qué falla?**

La máscara de depuración del driver es cero. La macro `DPRINTF` del capítulo imprime solo cuando el bit de la máscara está activado. O bien establece la máscara antes de cargar (`kenv hw.myfirst.debug_mask_default=0xff`), o bien después de cargar (`sysctl dev.myfirst.0.debug.mask=0xff`).

**P16. ¿Por qué el capítulo menciona DTrace tan a menudo?**

Porque es la herramienta de depuración más productiva del kernel de FreeBSD y porque la infraestructura de depuración del capítulo 23 está diseñada para integrarse con ella. Las sondas SDT dan al operador acceso en tiempo de ejecución a cada superficie de integración sin tener que reconstruir el driver. Un driver que expone sondas SDT con nombres claros es mucho más fácil de depurar que uno que no lo hace.

**P17. ¿Puedo usar este driver como plantilla para un driver de hardware real?**

La superficie de integración (cdev, ioctl, sysctl) se traslada directamente. Las partes específicas del hardware (asignación de recursos, gestión de interrupciones, configuración de DMA) se tratan en los capítulos 18 al 22 de la Parte IV. Un driver PCI real combina típicamente los patrones estructurales de la Parte IV con los patrones de integración de este capítulo para llegar a un driver listo para producción.

**P18. ¿Cómo concedo acceso a `/dev/myfirst0` a un usuario sin privilegios de root sin reconstruir el driver?**

Usa `devfs.rules(5)`. Añade un archivo de reglas en `/etc/devfs.rules` que coincida con el nombre del dispositivo y establezca el propietario, el grupo o el modo en tiempo de ejecución. Por ejemplo, para permitir al grupo `operator` leer y escribir en `/dev/myfirst*`:

```text
[myfirst_rules=10]
add path 'myfirst*' mode 0660 group operator
```

Activa el ruleset con `devfs_system_ruleset="myfirst_rules"` en `/etc/rc.conf` y `service devfs restart`. Los campos `mda_uid`, `mda_gid` y `mda_mode` del driver siguen estableciendo los valores predeterminados en el momento de creación; `devfs.rules` permite al administrador anularlos sin tocar el código fuente.

**P19. Mi lista `SRCS` no para de crecer. ¿Es eso un problema?**

No por sí sola. La línea `SRCS` del `Makefile` del módulo del kernel enumera todos los archivos fuente que se compilan en el módulo; que la lista crezca a medida que cada nueva responsabilidad tiene su propio archivo es algo normal y esperado. El driver de la Etapa 3 del capítulo ya cuenta con cuatro archivos fuente (`myfirst.c`, `myfirst_debug.c`, `myfirst_ioctl.c`, `myfirst_sysctl.c`), y el Capítulo 25 añadirá más. La señal de alarma no es el número de entradas, sino la falta de estructura: si `SRCS` contiene archivos no relacionados que se agruparon sin ningún esquema de nomenclatura, el driver ha superado su organización actual y merece una pequeña refactorización. El Capítulo 25 trata esa refactorización como un hábito de primera importancia.

**P20. ¿Qué debo hacer a continuación?**

Lee el Capítulo 25 (temas avanzados y consejos prácticos) para convertir este driver integrado en uno *mantenible*, trabaja los ejercicios de desafío del capítulo si quieres práctica adicional, y consulta uno de los drivers incluidos en el árbol de código fuente que se citan en la tarjeta de referencia para ver un ejemplo completo. El driver `null(4)` es el punto de entrada más suave; el driver Ethernet `if_em` es el más completo; el driver de almacenamiento `ahci(4)` muestra los patrones de CAM. Elige el que más se acerque a lo que quieres construir y léelo de principio a fin.

## En resumen

Este capítulo llevó a `myfirst` desde un módulo funcional pero aislado hasta un driver de FreeBSD completamente integrado. El recorrido fue deliberado: cada sección añadió una superficie de integración concreta y concluyó con el driver más útil y más descubrible que al principio. El lector que completó los laboratorios de la sección 9 tiene ahora en disco un driver que expone un cdev correctamente construido bajo `/dev`, cuatro ioctls bien diseñados bajo una cabecera pública, un árbol sysctl autodescriptivo bajo `dev.myfirst.0`, un tunable de tiempo de arranque a través de `/boot/loader.conf`, y un ciclo de vida limpio que sobrevive ciclos de carga/descarga sin fugas de recursos.

Los hitos técnicos del camino fueron:

* La etapa 1 (sección 2) sustituyó la llamada antigua a `make_dev` por la forma moderna `make_dev_args`, estableció `D_TRACKCLOSE`, conectó `si_drv1` para el estado por cdev, y recorrió el ciclo de vida del cdev desde la creación, pasando por el drenaje, hasta la destrucción. La presencia del driver en `/dev` pasó a ser de primera clase.

* La etapa 2 (sección 3) añadió los ioctls `MYFIRSTIOC_GETVER`, `MYFIRSTIOC_GETMSG`, `MYFIRSTIOC_SETMSG` y `MYFIRSTIOC_RESET`, junto con la cabecera pública `myfirst_ioctl.h` correspondiente. El dispatcher reutiliza la infraestructura de depuración del capítulo 23 (`MYF_DBG_IOCTL` y la sonda SDT `myfirst:::ioctl`). El programa de espacio de usuario `myfirstctl` que acompaña al capítulo demostró cómo una pequeña herramienta de línea de comandos ejercita cada ioctl sin necesidad de decodificar un código de solicitud a mano.

* La etapa 3 (sección 4) añadió el árbol sysctl `dev.myfirst.0.*`, incluyendo un subárbol `debug.` que permite al operador inspeccionar y modificar la máscara de depuración en tiempo de ejecución, un OID `version` que informa de la versión de integración, contadores de actividad de lectura y escritura, y un OID de cadena para el mensaje actual. El tunable de tiempo de arranque `hw.myfirst.debug_mask_default` permite al operador precargar la máscara de depuración antes del attach.

* Las secciones 5 y 6 esbozaron la misma integración basada en registro aplicada a la pila de red (`if_alloc`, `if_attach`, `bpfattach`) y a la pila de almacenamiento CAM (`cam_sim_alloc`, `xpt_bus_register`, `xpt_action`). El lector que no esté construyendo un driver de red o de almacenamiento igualmente obtuvo un patrón útil: todo registro de framework en FreeBSD sigue la misma forma de asignar-nombrar-rellenar-conectar-tráfico-desconectar-liberar.

* La sección 7 codificó la disciplina de ciclo de vida que lo une todo: cada registro exitoso debe emparejarse con un desregistro en orden inverso, y un attach fallido debe deshacer cada paso previo antes de retornar. La cadena `goto err` es la codificación canónica de esta regla.

* La sección 8 encuadró el capítulo como un paso en un arco más largo: `myfirst` comenzó como una demostración de un solo archivo, creció hasta convertirse en un driver multifichero a lo largo de las partes II a IV, adquirió depuración y trazado en el capítulo 23, y adquirió la superficie de integración aquí. La versión de lanzamiento, la versión del módulo y la versión de la API siguen aspectos diferentes de esa evolución; incrementar cada una en el momento adecuado es la disciplina de versiones de un driver de larga vida.

Los laboratorios del capítulo (sección 9) guiaron al lector por cada hito, los desafíos (sección 10) ofrecieron trabajo adicional al lector motivado, y el catálogo de resolución de problemas (sección 11) reunió los síntomas y soluciones más comunes para consulta rápida.

El resultado es un hito del driver (`1.7-integration`) que el lector puede llevar al siguiente capítulo sin que quede ningún trabajo de integración pendiente. Los patrones de este capítulo (construcción de cdev, diseño de ioctls, árboles sysctl, disciplina del ciclo de vida) son también los patrones que el resto de la parte V y la mayor parte de las partes VI y VII darán por sabidos.

## Puente hacia el capítulo 25

El capítulo 25 (Temas avanzados y consejos prácticos) cierra la parte 5 convirtiendo el driver integrado de este capítulo en uno *mantenible*. Donde el capítulo 24 añadió las interfaces que permiten al driver comunicarse con el resto del sistema, el capítulo 25 enseña los hábitos de ingeniería que mantienen esas interfaces estables y legibles a medida que el driver absorbe el siguiente año de correcciones de errores, cambios de portabilidad y solicitudes de nuevas funcionalidades. El driver crece de `1.7-integration` a `1.8-maintenance`; las adiciones visibles son modestas, pero la disciplina que las sustenta es lo que separa a un driver que sobrevive un ciclo de desarrollo de uno que sobrevive una década.

El puente del capítulo 24 al capítulo 25 tiene cuatro partes concretas.

En primer lugar, el registro con límite de tasa que introduce el capítulo 25 se apoya directamente sobre la macro `DPRINTF` del capítulo 23 y las superficies de integración que este capítulo añadió. Una nueva macro `DLOG_RL` construida alrededor de `ppsratecheck(9)` permite al driver mantener las mismas clases de depuración que ya usa, pero sin saturar `dmesg` durante una tormenta de eventos. La disciplina es sencilla: elegir un límite por segundo, incorporarlo en los puntos de llamada de depuración existentes, y auditar los pocos lugares donde un `device_printf` sin restricciones podría ejecutarse en un bucle.

En segundo lugar, las rutas de ioctl y sysctl que este capítulo construyó serán auditadas en el capítulo 25 para establecer un vocabulario de errno coherente. El capítulo distingue `EINVAL` de `ENXIO`, `ENOIOCTL` de `ENOTTY`, `EBUSY` de `EAGAIN`, y `EPERM` de `EACCES`, de modo que cada superficie de integración devuelva el código correcto en cada fallo. El lector recorre el dispatcher escrito en la sección 3 y los manejadores sysctl escritos en la sección 4, y los ajusta donde se devolvía el error incorrecto.

En tercer lugar, el tunable de tiempo de arranque `hw.myfirst.debug_mask_default` introducido en la sección 4 se generalizará en el capítulo 25 en un vocabulario de tunables pequeño pero disciplinado mediante `TUNABLE_INT_FETCH`, `TUNABLE_LONG_FETCH`, `TUNABLE_BOOL_FETCH` y `TUNABLE_STR_FETCH`, en cooperación con sysctls de escritura bajo `CTLFLAG_TUN`. La misma triple combinación de `MYFIRST_VERSION`, `MODULE_VERSION` y `MYFIRST_IOCTL_VERSION` que este capítulo estableció se extenderá con un ioctl `MYFIRSTIOC_GETCAPS` para que las herramientas de espacio de usuario puedan detectar funcionalidades en tiempo de ejecución sin ensayo y error.

En cuarto lugar, la cadena `goto err` introducida en la sección 7 pasará de ser un ejercicio de laboratorio al patrón de limpieza en producción del driver, y la refactorización del capítulo moverá la lógica de attach de Newbus y las callbacks de cdev a archivos separados (`myfirst_bus.c` y `myfirst_cdev.c`), junto con un `myfirst_log.c` para las nuevas macros de registro. El capítulo 25 también introduce `SYSINIT(9)` y `SYSUNINIT(9)` para la inicialización a nivel de driver y un manejador de eventos `shutdown_pre_sync` a través de `EVENTHANDLER(9)`, añadiendo dos superficies de registro más a las que este capítulo ya enseñó.

Continúa con la confianza de que el vocabulario de integración ya está asentado. El capítulo 25 toma este driver y lo prepara para el largo plazo; la parte 6 da comienzo entonces a los capítulos específicos de transporte, que se apoyan en todos los hábitos que la parte 5 ha construido.

## Tarjeta de referencia y glosario

Las páginas restantes del capítulo constituyen una referencia compacta. Están diseñadas para leerse de corrido la primera vez y consultarse después cuando el lector necesite buscar algo. El orden es: una tarjeta de referencia de las macros, estructuras y flags importantes; un glosario del vocabulario de integración; y un breve directorio de los archivos que acompañan al capítulo.

### Referencia rápida: construcción de cdev

| Función | Cuándo usarla |
|----------|-------------|
| `make_dev_args_init(args)` | Siempre antes de `make_dev_s`; pone a cero la estructura de argumentos de forma segura. |
| `make_dev_s(args, &cdev, fmt, ...)` | La forma moderna y preferida. Devuelve 0 o errno. |
| `make_dev(devsw, unit, uid, gid, mode, fmt, ...)` | Forma antigua de llamada única. Desaconsejada para código nuevo. |
| `make_dev_credf(flags, ...)` | Cuando se necesitan los bits de flag `MAKEDEV_*`. |
| `destroy_dev(cdev)` | Siempre en detach; drena las callbacks en vuelo. |
| `destroy_dev_drain(cdev)` | Cuando detach debe esperar a que se liberen las referencias pendientes. |

### Referencia rápida: flags de cdevsw

| Flag | Significado |
|------|---------|
| `D_VERSION` | Obligatorio; identifica la versión de la disposición de cdevsw. |
| `D_TRACKCLOSE` | Llama a `d_close` solo en el último cierre. Recomendado. |
| `D_NEEDGIANT` | Toma el giant lock alrededor de cada callback. Desaconsejado. |
| `D_DISK` | El cdev representa un disco; usa bio en lugar de uio para I/O. |
| `D_TTY` | El cdev es un terminal; afecta al enrutamiento de la disciplina de línea. |
| `D_MMAP_ANON` | El cdev admite mmap anónimo. |
| `D_MEM` | El cdev es similar a `/dev/mem`; acceso directo a memoria. |

### Referencia rápida: flags de `make_dev` (`MAKEDEV_*`)

| Flag | Significado |
|------|---------|
| `MAKEDEV_REF` | Toma una referencia adicional; el llamador debe ejecutar `dev_rel` después. |
| `MAKEDEV_NOWAIT` | No duerme; devuelve `ENOMEM` si no hay memoria. |
| `MAKEDEV_WAITOK` | Se permite dormir (valor por defecto para la mayoría de los llamadores). |
| `MAKEDEV_ETERNAL` | El cdev nunca desaparece; se aplican ciertas optimizaciones. |
| `MAKEDEV_ETERNAL_KLD` | Como ETERNAL, pero solo durante la vida útil del kld. |
| `MAKEDEV_CHECKNAME` | Valida el nombre; `ENAMETOOLONG` si es demasiado largo. |

### Referencia rápida: macros de codificación de ioctl

Todas en `/usr/src/sys/sys/ioccom.h`.

| Macro | Dirección | Argumento |
|-------|-----------|----------|
| `_IO(g, n)` | ninguna | ninguno |
| `_IOR(g, n, t)` | salida | tipo `t`, tamaño `sizeof(t)` |
| `_IOW(g, n, t)` | entrada | tipo `t`, tamaño `sizeof(t)` |
| `_IOWR(g, n, t)` | entrada y salida | tipo `t`, tamaño `sizeof(t)` |
| `_IOWINT(g, n)` | ninguna, pero se pasa el valor de int | int |

Los argumentos significan:

* `g`: letra de grupo, convencionalmente `'M'` para `myfirst` y similares.
* `n`: número de comando, monotónicamente creciente dentro del grupo.
* `t`: tipo del argumento, usado únicamente por su `sizeof`.

El tamaño máximo es `IOCPARM_MAX = 8192` bytes. Para transferencias mayores, usa el patrón de puntero embebido (desafío 1) o un mecanismo diferente como `mmap` o `read`/`write`.

### Referencia rápida: firma de `d_ioctl_t`

```c
int d_ioctl(struct cdev *dev, u_long cmd, caddr_t data,
            int fflag, struct thread *td);
```

| Argumento | Significado |
|----------|---------|
| `dev` | El cdev que respalda el descriptor de archivo. Usa `dev->si_drv1` para obtener el softc. |
| `cmd` | El código de solicitud procedente del espacio de usuario. Se compara con constantes con nombre. |
| `data` | Buffer en el lado del kernel con los datos del usuario. Desreferenciación directa; no se necesita `copyin`. |
| `fflag` | Flags de archivo de la llamada open (`FREAD`, `FWRITE`). Compruébalos antes de modificar. |
| `td` | Thread que realiza la llamada. Usa `td->td_ucred` para las credenciales. |

Devuelve 0 en caso de éxito, un errno positivo en caso de fallo, o `ENOIOCTL` para comandos desconocidos.

### Referencia rápida: macros de OID de sysctl

Todas en `/usr/src/sys/sys/sysctl.h`.

| Macro | Añade |
|-------|------|
| `SYSCTL_ADD_INT(ctx, parent, nbr, name, flags, ptr, val, descr)` | int con signo, respaldado por `*ptr`. |
| `SYSCTL_ADD_UINT` | int sin signo. |
| `SYSCTL_ADD_LONG` / `SYSCTL_ADD_ULONG` | long / long sin signo. |
| `SYSCTL_ADD_S64` / `SYSCTL_ADD_U64` | 64 bits con signo / sin signo. |
| `SYSCTL_ADD_BOOL` | Booleano (preferido frente a int 0/1). |
| `SYSCTL_ADD_STRING(ctx, parent, nbr, name, flags, ptr, len, descr)` | Cadena terminada en NUL. |
| `SYSCTL_ADD_NODE(ctx, parent, nbr, name, flags, handler, descr)` | Nodo de subárbol. |
| `SYSCTL_ADD_PROC(ctx, parent, nbr, name, flags, arg1, arg2, handler, fmt, descr)` | OID respaldado por un manejador. |

### Referencia rápida: bits de flag de sysctl

| Flag | Significado |
|------|---------|
| `CTLFLAG_RD` | Solo lectura. |
| `CTLFLAG_WR` | Solo escritura (poco frecuente). |
| `CTLFLAG_RW` | Lectura-escritura. |
| `CTLFLAG_TUN` | Tunable del cargador; se lee en el arranque desde `/boot/loader.conf`. |
| `CTLFLAG_MPSAFE` | El manejador es seguro sin el giant lock. **Siempre establecido en código nuevo.** |
| `CTLFLAG_PRISON` | Visible dentro de los jails. |
| `CTLFLAG_VNET` | Por VNET (pila de red virtualizada). |
| `CTLFLAG_DYN` | OID dinámico; establecido automáticamente por `SYSCTL_ADD_*`. |
| `CTLFLAG_SECURE` | Solo lectura cuando `securelevel > 0`. |

### Referencia rápida: bits de tipo de sysctl

Se combinan con OR en la palabra de flags para `SYSCTL_ADD_PROC` y similares.

| Flag | Significado |
|------|---------|
| `CTLTYPE_INT` / `CTLTYPE_UINT` | int con signo / sin signo. |
| `CTLTYPE_LONG` / `CTLTYPE_ULONG` | long / long sin signo. |
| `CTLTYPE_S64` / `CTLTYPE_U64` | 64 bits con signo / sin signo. |
| `CTLTYPE_STRING` | Cadena terminada en NUL. |
| `CTLTYPE_OPAQUE` | Blob opaco; raramente usado en código nuevo. |
| `CTLTYPE_NODE` | Nodo de subárbol. |

### Referencia rápida: cadenas de formato del manejador sysctl

Utilizado por `SYSCTL_ADD_PROC` para indicar a `sysctl(8)` cómo mostrar el valor.

| Formato | Tipo |
|--------|------|
| `"I"` | int |
| `"IU"` | unsigned int |
| `"L"` | long |
| `"LU"` | unsigned long |
| `"Q"` | int64 |
| `"QU"` | uint64 |
| `"A"` | cadena terminada en NUL |
| `"S,structname"` | struct opaca (poco frecuente) |

### Referencia rápida: plantilla del handler de sysctl

```c
static int
my_handler(SYSCTL_HANDLER_ARGS)
{
        struct my_softc *sc = arg1;
        u_int val;

        /* Read the current value into val under the mutex. */
        mtx_lock(&sc->sc_mtx);
        val = sc->sc_field;
        mtx_unlock(&sc->sc_mtx);

        /* Let the framework do the read or write. */
        return (sysctl_handle_int(oidp, &val, 0, req));
}
```

### Referencia rápida: contexto sysctl por dispositivo

```c
struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(dev);
struct sysctl_oid      *tree = device_get_sysctl_tree(dev);
struct sysctl_oid_list *child = SYSCTL_CHILDREN(tree);
```

El framework es propietario del ctx; el driver no debe llamar a `sysctl_ctx_free` sobre él. El framework limpia automáticamente tras un detach satisfactorio.

### Referencia rápida: tunables del loader

```c
TUNABLE_INT_FETCH("hw.driver.knob", &sc->sc_knob);
TUNABLE_LONG_FETCH("hw.driver.knob", &sc->sc_knob);
TUNABLE_STR_FETCH("hw.driver.knob", buf, sizeof(buf));
```

El primer argumento es el nombre de la variable del loader. El segundo es un puntero al destino, que también actúa como valor predeterminado si la variable no está definida.

### Referencia rápida: ciclo de vida de ifnet

| Función | Cuándo |
|----------|------|
| `if_alloc(IFT_<type>)` | Asigna el ifnet. |
| `if_initname(ifp, name, unit)` | Establece el nombre visible para el usuario (lo muestra `ifconfig`). |
| `if_setflags(ifp, flags)` | Establece los flags `IFF_*`. |
| `if_setsoftc(ifp, sc)` | Asocia el softc del driver. |
| `if_setioctlfn(ifp, fn)` | Establece el handler de ioctl. |
| `if_settransmitfn(ifp, fn)` | Establece la función de transmisión. |
| `if_attach(ifp)` | Hace visible la interfaz. |
| `bpfattach(ifp, dlt, hdrlen)` | Hace visible el tráfico para BPF. |
| `bpfdetach(ifp)` | Invierte `bpfattach`. |
| `if_detach(ifp)` | Invierte `if_attach`. |
| `if_free(ifp)` | Libera el ifnet. |

### Referencia rápida: ciclo de vida del SIM de CAM

| Función | Cuándo |
|----------|------|
| `cam_simq_alloc(maxq)` | Asigna la cola del dispositivo. |
| `cam_sim_alloc(action, poll, name, sc, unit, mtx, max_tagged, max_dev_tx, devq)` | Asigna el SIM. |
| `xpt_bus_register(sim, dev, 0)` | Registra el bus en CAM. |
| `xpt_create_path(&path, NULL, cam_sim_path(sim), targ, lun)` | Crea un path para eventos. |
| `xpt_action(ccb)` | Envía un CCB a CAM. |
| `xpt_done(ccb)` | Informa a CAM de que el driver ha completado un CCB. |
| `xpt_free_path(path)` | Invierte `xpt_create_path`. |
| `xpt_bus_deregister(cam_sim_path(sim))` | Invierte `xpt_bus_register`. |
| `cam_sim_free(sim, free_devq)` | Invierte `cam_sim_alloc`. Pasa `TRUE` para liberar también el devq. |

### Referencia rápida: ciclo de vida del módulo

```c
static moduledata_t mymod = {
        "myfirst",        /* name */
        myfirst_modevent, /* event handler, can be NULL */
        NULL              /* extra data, rarely used */
};
DECLARE_MODULE(myfirst, mymod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(myfirst, 1);
MODULE_DEPEND(myfirst, otherdriver, 1, 1, 1);
```

La firma del event handler es `int (*)(module_t mod, int what, void *arg)`. El argumento `what` es uno de `MOD_LOAD`, `MOD_UNLOAD`, `MOD_QUIESCE` o `MOD_SHUTDOWN`. Devuelve 0 en caso de éxito o un errno positivo en caso de error.

### Referencia rápida: event handler

```c
eventhandler_tag tag;

tag = EVENTHANDLER_REGISTER(event_name, callback,
    arg, EVENTHANDLER_PRI_ANY);

EVENTHANDLER_DEREGISTER(event_name, tag);
```

Nombres de evento habituales: `shutdown_pre_sync`, `shutdown_post_sync`, `shutdown_final`, `vm_lowmem`, `power_suspend_early`, `power_resume`.

### Referencia rápida: convenciones de errno

| Errno | Cuándo devolverlo |
|-------|----------------|
| `0` | Éxito. |
| `EINVAL` | Los argumentos son reconocidos pero no son válidos. |
| `EBADF` | El descriptor de archivo no está abierto correctamente para la operación. |
| `EBUSY` | El recurso está en uso (se devuelve habitualmente desde detach). |
| `ENOIOCTL` | El comando ioctl es desconocido. **Úsalo para el caso por defecto en `d_ioctl`.** |
| `ENOTTY` | La traducción del kernel de `ENOIOCTL` para el espacio de usuario. |
| `ENOMEM` | La asignación ha fallado. |
| `EAGAIN` | Inténtalo de nuevo más tarde (se devuelve habitualmente en I/O no bloqueante). |
| `EPERM` | El llamador no tiene el privilegio necesario. |
| `EOPNOTSUPP` | La operación no está soportada por este driver. |
| `EFAULT` | Un puntero de usuario no es válido (devuelto por `copyin`/`copyout` fallido). |
| `ETIMEDOUT` | Una espera ha expirado. |
| `EIO` | Error de I/O genérico del hardware. |

### Referencia rápida: bits de clase de depuración (del Capítulo 23)

| Bit | Nombre | Usado para |
|-----|------|----------|
| `0x01` | `MYF_DBG_INIT` | probe / attach / detach |
| `0x02` | `MYF_DBG_OPEN` | ciclo de vida de open / close |
| `0x04` | `MYF_DBG_IO` | rutas de lectura/escritura |
| `0x08` | `MYF_DBG_IOCTL` | gestión de ioctl |
| `0x10` | `MYF_DBG_INTR` | handler de interrupción |
| `0x20` | `MYF_DBG_DMA` | mapeo/sincronización de DMA |
| `0x40` | `MYF_DBG_PWR` | eventos de gestión de energía |
| `0x80` | `MYF_DBG_MEM` | traza de malloc/free |
| `0xFFFFFFFF` | `MYF_DBG_ANY` | todas las clases |
| `0` | `MYF_DBG_NONE` | sin registro |

La máscara se establece por instancia mediante `dev.<driver>.<unit>.debug.mask`, o de forma global en el boot mediante `hw.<driver>.debug_mask_default` en `/boot/loader.conf`.

### Glosario del vocabulario de integración

**API version**: Un entero expuesto a través de la interfaz ioctl de un driver (generalmente a través de un ioctl `GETVER`) que los programas del espacio de usuario pueden consultar para detectar cambios en la interfaz pública del driver. Se incrementa únicamente cuando la interfaz visible para el usuario cambia de forma incompatible.

**bpfattach**: La función que engancha un `ifnet` en el mecanismo BPF (Berkeley Packet Filter) para que `tcpdump` y herramientas similares puedan observar su tráfico. Debe ir emparejada con `bpfdetach`.

**bus_generic_detach**: Una función auxiliar que hace el detach de todos los hijos de un driver de tipo bus. Se usa como primer paso en el detach de un driver de bus para liberar los dispositivos hijo antes de que el padre desmonte su propio estado.

**CAM**: El Common Access Method, el subsistema de almacenamiento de FreeBSD situado por encima de los drivers de dispositivo. Gestiona la cola de I/O, la abstracción de target/LUN y los drivers de periférico (`da`, `cd`, `sa`).

**CCB**: CAM Control Block. Una única solicitud de I/O estructurada como una unión etiquetada; el driver inspecciona `ccb->ccb_h.func_code` y despacha en consecuencia. Se completa mediante `xpt_done`.

**cdev**: Dispositivo de caracteres. El objeto del kernel por nodo de dispositivo que respalda una entrada bajo `/dev`. Se crea con `make_dev_s` y se destruye con `destroy_dev`.

**cdevsw**: Character device switch. La tabla estática de callbacks (`d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl`, ...) que el kernel invoca en las operaciones sobre un cdev.

**copyin / copyout**: Funciones que transfieren bytes entre direcciones del espacio de usuario y del espacio del kernel. El kernel las ejecuta automáticamente para los ioctls correctamente codificados; el driver las llama explícitamente solo para el patrón de punteros embebidos.

**CTLFLAG_MPSAFE**: Un flag de sysctl que declara que el handler del OID es seguro para llamar sin el giant lock. Obligatorio en código nuevo; sin él, el kernel toma el giant lock en cada acceso.

**d_ioctl_t**: El tipo de puntero a función para el callback ioctl del cdevsw. Firma: `int (*)(struct cdev *, u_long, caddr_t, int, struct thread *)`.

**d_priv**: Un puntero privado por descriptor de archivo asociado mediante `devfs_set_cdevpriv`. Se utiliza para el estado que debe estar vinculado a una apertura concreta y no a la instancia del driver en su conjunto.

**dev_ref / dev_rel**: Un par de funciones que incrementan y decrementan el contador de referencias de un cdev. Se usan para coordinar el detach con los callbacks en vuelo; véase el Ejercicio de desafío 3.

**devfs**: El sistema de archivos gestionado por el kernel que respalda `/dev`. El driver crea los cdevs y devfs los hace visibles.

**devfs.rules(8)**: Un mecanismo de configuración para los permisos de devfs en tiempo de ejecución. Se aplica con `service devfs restart` tras editar `/etc/devfs.rules`.

**DTrace**: El framework de trazado dinámico. Los drivers exponen puntos de sonda a través de macros SDT; los scripts de DTrace se enganchan a ellos en tiempo de ejecución.

**EVENTHANDLER_REGISTER**: El mecanismo para registrar un callback para un evento con nombre a nivel de sistema (`shutdown_pre_sync`, `vm_lowmem`, etc.). Debe ir emparejado con `EVENTHANDLER_DEREGISTER`.

**ifnet**: El objeto del stack de red por interfaz. El equivalente de red del cdev.

**if_t**: El typedef opaco que usa el stack de red para `ifnet`. Los drivers manipulan la interfaz a través de funciones de acceso en lugar de acceder directamente a los campos.

**IOC_VOID / IOC_IN / IOC_OUT / IOC_INOUT**: Los cuatro bits de dirección codificados en un código de solicitud ioctl. El kernel los usa para decidir qué operación `copyin`/`copyout` realizar.

**IOCPARM_MAX**: El tamaño máximo (8192 bytes) de la estructura de argumento de un ioctl tal como está codificado en el código de solicitud. Las transferencias de mayor tamaño requieren el patrón de punteros embebidos.

**kldload / kldunload**: Las herramientas del espacio de usuario que cargan y descargan módulos del kernel. Ambas invocan el event handler del módulo correspondiente (`MOD_LOAD` y `MOD_UNLOAD`).

**make_dev_args**: La estructura que se pasa a `make_dev_s` para describir un nuevo cdev. Se inicializa con `make_dev_args_init`.

**make_dev_s**: La función moderna preferida para crear un cdev. Devuelve 0 o un errno positivo; establece `*cdev` en caso de éxito.

**MAKEDEV_***: Bits de flag que se pasan a `make_dev_credf` y funciones similares. Bits habituales: `MAKEDEV_REF`, `MAKEDEV_NOWAIT`, `MAKEDEV_ETERNAL_KLD`.

**MOD_LOAD / MOD_UNLOAD / MOD_SHUTDOWN**: Los eventos que se entregan al event handler de un módulo. Devuelve 0 para confirmar o distinto de cero para rechazar.

**MODULE_DEPEND**: La macro que declara la dependencia de un módulo respecto a otro. El kernel usa los argumentos de versión (`min`, `pref`, `max`) para garantizar la compatibilidad.

**MODULE_VERSION**: La macro que declara el número de versión de un módulo. Se incrementa cuando los usuarios internos del kernel necesitarían recompilar.

**Newbus**: El framework de árbol de dispositivos de FreeBSD. Gestiona el `device_t`, el softc por dispositivo, el contexto sysctl por dispositivo y el ciclo de vida probe/attach/detach.

**OID**: Identificador de objeto. Un nodo en el árbol de sysctl. Los OIDs estáticos se declaran en tiempo de compilación; los OIDs dinámicos se añaden en tiempo de ejecución con `SYSCTL_ADD_*`.

**Path (CAM)**: Una terna `(bus, target, LUN)` que identifica un destino CAM. Se crea con `xpt_create_path`.

**Public header**: Un header que los programas del espacio de usuario incluyen para comunicarse con el driver. Debe compilar limpiamente sin `_KERNEL` definido y usar únicamente tipos estables.

**Registration framework**: Un subsistema de FreeBSD que expone una interfaz de tipo "asignar-nombrar-rellenar-asociar-tráfico-desasociar-liberar" para los drivers. Ejemplos: networking (`ifnet`), almacenamiento (CAM), sonido, USB, GEOM.

**Release version**: La cadena legible por humanos que identifica la versión de un driver. Se expone a través de sysctl como `dev.<driver>.<unit>.version`.

**SDT**: Trazado definido estáticamente. El mecanismo del kernel para puntos de sonda en tiempo de compilación consumibles por DTrace.

**si_drv1 / si_drv2**: Dos campos de puntero privado en `struct cdev` disponibles para uso del driver. Por convención, `si_drv1` apunta al softc.

**SIM**: SCSI Interface Module. La vista que CAM tiene de un adaptador de almacenamiento. Se asigna con `cam_sim_alloc` y se registra con `xpt_bus_register`.

**Soft detach**: Un patrón de detach en el que el driver espera a que las referencias pendientes lleguen a cero en lugar de rechazar el detach de inmediato. Véase el Ejercicio de desafío 3.

**Softc**: Contexto software. El estado por instancia del driver. Lo asigna Newbus y se accede a él mediante `device_get_softc(dev)`.

**SYSINIT**: Una función de inicialización del kernel de un solo disparo, registrada en tiempo de compilación. Se ejecuta en una etapa específica durante el boot. Raramente se necesita en código de drivers.

**SYSCTL_HANDLER_ARGS**: Una macro que se expande a la lista de argumentos estándar para un handler de sysctl: `oidp, arg1, arg2, req`.

**TUNABLE_INT_FETCH**: Una función que lee un valor del entorno del loader y lo escribe en una variable del kernel. La variable conserva su valor anterior si la variable del loader está ausente.

**XPT**: CAM Transport Layer. El mecanismo central de despacho de CAM. El driver llama a `xpt_action` para enviar un CCB; CAM responde a través de la función de acción del SIM para los CCBs de I/O.

### Inventario de archivos complementarios

Los archivos complementarios de este capítulo se encuentran en `examples/part-05/ch24-integration/` en el repositorio del libro. La estructura de directorios es la siguiente:

```text
examples/part-05/ch24-integration/
├── README.md
├── INTEGRATION.md
├── stage1-devfs/
│   ├── Makefile
│   ├── myfirst.c             (with make_dev_args)
│   └── README.md
├── stage2-ioctl/
│   ├── Makefile
│   ├── Makefile.user         (for myfirstctl)
│   ├── myfirst_ioctl.c
│   ├── myfirst_ioctl.h       (PUBLIC)
│   ├── myfirstctl.c
│   └── README.md
├── stage3-sysctl/
│   ├── Makefile
│   ├── myfirst.c
│   ├── myfirst_sysctl.c
│   └── README.md
└── labs/
    ├── lab24_1_stage1.sh     (verification commands for Lab 1)
    ├── lab24_2_stage2.sh
    ├── lab24_3_stage3.sh
    ├── lab24_4_failure.sh
    ├── lab24_5_dtrace.sh
    ├── lab24_6_smoke.sh
    ├── lab24_7_reload.sh
    └── loader.conf.example
```

El punto de partida del Laboratorio 1 es el propio driver del lector al final del Capítulo 23 (`1.6-debug`); `stage1-devfs/`, `stage2-ioctl/` y `stage3-sysctl/` son soluciones de referencia que el lector puede consultar tras completar cada laboratorio. El directorio de laboratorios contiene pequeños scripts de shell que ejecutan los comandos de verificación y que el lector puede adaptar para sus propias pruebas.

El archivo `README.md` en la raíz del capítulo describe cómo usar el directorio, el orden de las etapas y la relación entre los árboles de etapas. El archivo `INTEGRATION.md` es un documento más extenso que relaciona cada concepto del capítulo con el archivo en el que aparece.

### Dónde se encuentra el árbol de código fuente del capítulo en FreeBSD real

Para el lector que desee consultar las implementaciones en el árbol de código fuente referenciadas a lo largo del capítulo, aquí tiene un índice breve de los archivos más importantes:

| Concepto | Archivo en el árbol |
|---------|--------------|
| codificación de ioctl | `/usr/src/sys/sys/ioccom.h` |
| definición de cdevsw | `/usr/src/sys/sys/conf.h` |
| familia make_dev | `/usr/src/sys/kern/kern_conf.c` |
| framework sysctl | `/usr/src/sys/sys/sysctl.h`, `/usr/src/sys/kern/kern_sysctl.c` |
| API de ifnet | `/usr/src/sys/net/if.h`, `/usr/src/sys/net/if.c` |
| ejemplo de ifnet | `/usr/src/sys/net/if_disc.c` |
| API CAM SIM | `/usr/src/sys/cam/cam_xpt.h`, `/usr/src/sys/cam/cam_sim.h` |
| ejemplo de CAM | `/usr/src/sys/dev/ahci/ahci.c` |
| EVENTHANDLER | `/usr/src/sys/sys/eventhandler.h` |
| mecanismo MODULE | `/usr/src/sys/sys/module.h`, `/usr/src/sys/kern/kern_module.c` |
| TUNABLE | `/usr/src/sys/sys/sysctl.h` (buscar `TUNABLE_INT_FETCH`) |
| sondas SDT | `/usr/src/sys/sys/sdt.h`, `/usr/src/sys/cddl/dev/sdt/sdt.c` |
| La referencia `null(4)` | `/usr/src/sys/dev/null/null.c` |

Leer estos archivos junto con el capítulo es el siguiente paso para cualquier lector que quiera profundizar en sus conocimientos de integración. El driver `null(4)` merece especialmente leerse completo; es lo suficientemente pequeño como para absorberlo en una sola sesión y demuestra casi todos los patrones que se trataron en este capítulo.

### Lo que no entró en el capítulo

Algunos temas de integración pertenecen al conjunto de herramientas más amplio de FreeBSD, pero no han merecido una sección propia aquí, bien porque son específicos de un subsistema de una forma que distraería a un lector principiante, bien porque se tratan con mayor profundidad en un capítulo posterior. Mencionarlos aquí mantiene al capítulo honesto sobre su alcance y ofrece al lector un mapa orientado hacia adelante.

La primera omisión es `geom(4)`. Un driver que expone un dispositivo de bloques se engancha a GEOM en lugar de a CAM. El patrón de registro es similar al patrón cdev (asignar un `g_geom`, rellenar los callbacks de `g_class`, llamar a `g_attach`), pero el vocabulario es suficientemente distinto como para que mezclarlo en el capítulo habría desdibujado la distinción entre almacenamiento y dispositivos de caracteres. Los drivers para discos físicos y destinos de pseudodisco viven en este vecindario; la referencia canónica es `/usr/src/sys/geom/geom_disk.c`.

La segunda omisión es `usb(4)`. Un driver USB se registra en la pila USB a través de `usb_attach` y una tabla de métodos específica de USB, en lugar de hacerlo directamente a través de Newbus. Las superficies de integración (devfs, sysctl) son las mismas una vez que el dispositivo está conectado, pero el borde superior pertenece a la pila USB. Las referencias canónicas se encuentran en `/usr/src/sys/dev/usb/`.

La tercera omisión es `iicbus(4)` y `spibus(4)`. Los drivers que se comunican con periféricos I2C o SPI se conectan como hijos de un driver de bus y utilizan rutinas de transferencia específicas del bus. Las superficies de integración son las mismas, pero la integración con el árbol de dispositivos y FDT que impulsa los SoC Arm modernos añade un vocabulario que merece su propio capítulo. La Parte VI cubre estas superficies en su contexto apropiado.

La cuarta omisión es la integración con `kqueue(2)` y `poll(2)`. Un driver de caracteres que quiera despertar a programas en espacio de usuario bloqueados en `select`, `poll` o `kqueue` debe implementar `d_kqfilter` (y opcionalmente `d_poll`), conectar `selwakeup` y `KNOTE` en la ruta de datos, y suministrar un pequeño conjunto de operaciones de filtro. El mecanismo no es difícil, pero conceptualmente es una capa por encima del contrato cdev básico; volveremos a él en el Capítulo 26.

Un lector que necesite cualquiera de estas superficies hoy debe tratar el patrón del capítulo como la base y recurrir a las referencias del árbol de código fuente mencionadas anteriormente. La disciplina (registrar en attach, drenar en detach, mantener un único mutex a través de los callbacks que modifican el estado, versionar la superficie pública) es la misma.

### Autoevaluación del lector

Antes de pasar la página, el lector que haya trabajado el capítulo debería ser capaz de responder las siguientes preguntas sin consultar el texto. Cada pregunta se corresponde con una sección que introdujo el material subyacente. Si alguna pregunta resulta desconocida, la sección del capítulo indicada entre paréntesis es el lugar adecuado para repasar antes de continuar.

1. ¿Qué cambia `D_TRACKCLOSE` en la forma en que se invoca `d_close`? (Sección 2)
2. ¿Por qué es preferible `mda_si_drv1` frente a asignar `si_drv1` después de que `make_dev` retorne? (Sección 2)
3. ¿Qué codifica la macro `_IOR('M', 1, uint32_t)` en el código de solicitud resultante? (Sección 3)
4. ¿Por qué debe la rama por defecto del despachador devolver `ENOIOCTL` en lugar de `EINVAL`? (Sección 3)
5. ¿Qué función del kernel desmonta el contexto sysctl por dispositivo, y cuándo se ejecuta? (Sección 4)
6. ¿Cómo cooperan `CTLFLAG_TUN` y `TUNABLE_INT_FETCH` para aplicar un valor en tiempo de boot? (Sección 4)
7. ¿Cuál es la diferencia entre la cadena `MYFIRST_VERSION`, el entero `MODULE_VERSION` y el entero `MYFIRST_IOCTL_VERSION`? (Sección 8)
8. ¿Por qué la cadena de limpieza en attach usa `goto`s etiquetados en orden inverso en lugar de sentencias `if` anidadas? (Sección 7)
9. ¿En qué se diferencia el patrón soft-detach del capítulo del patrón `dev_ref`/`dev_rel` que esboza el Ejercicio de Desafío 3? (Secciones 7 y 10)
10. ¿Cuáles son las dos superficies de integración necesarias para casi cualquier driver, y cuáles son las dos que solo se requieren en drivers que se unen a un subsistema específico? (Secciones 1, 2, 3, 4, 5, 6)

Un lector que responda la mayoría de estas preguntas sin dudar habrá interiorizado el capítulo y estará preparado para lo que viene a continuación. Un lector que dude en más de dos debería repasar las secciones correspondientes antes de abordar la disciplina de mantenimiento del Capítulo 25.

### Palabra final

La integración es lo que convierte un módulo funcional en un driver utilizable. Los patrones de este capítulo no son un acabado opcional; son la diferencia entre un driver que un operador puede adoptar y uno con el que tiene que luchar. Domínalos una vez, y cada driver posterior resultará más fácil de construir, más fácil de mantener y más fácil de distribuir.

El siguiente capítulo toma la disciplina introducida aquí y la generaliza en un conjunto de hábitos de mantenimiento: registro con tasa limitada, vocabulario de errno coherente, tunables y versionado, limpieza de nivel de producción, y los mecanismos `SYSINIT`/`SYSUNINIT`/`EVENTHANDLER` que extienden el ciclo de vida de un driver más allá de una simple carga y descarga. El vocabulario cambia, pero el ritmo es el mismo: registrar, recibir tráfico, desregistrar limpiamente. Con la base del Capítulo 24 establecida, el Capítulo 25 se sentirá como una extensión natural en lugar de un mundo nuevo.

Un último pensamiento antes de pasar la página. Las superficies de integración de este capítulo son deliberadamente pocas. Están devfs, ioctl y sysctl. Están los registros opcionales en subsistemas como ifnet y CAM. Está la disciplina del ciclo de vida que los une. Cinco conceptos en total.

Una vez que estos resulten familiares, el resto del libro es la aplicación de los mismos patrones a hardware cada vez más interesante. Un lector que haya terminado este capítulo ha terminado la mitad del libro dedicada a la API del kernel; lo que queda son los sistemas y la disciplina para manejarlos correctamente.
