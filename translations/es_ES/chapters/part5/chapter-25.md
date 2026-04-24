---
title: "Temas avanzados y consejos prácticos"
description: "El capítulo 25 cierra la Parte 5 enseñando los hábitos de ingeniería que convierten un driver de FreeBSD funcional e integrado en una pieza de software del kernel robusta y mantenible. Abarca el registro del kernel con limitación de tasa y las buenas prácticas de logging; un vocabulario disciplinado de valores errno y convenciones de retorno para los callbacks de read, write, ioctl, sysctl y ciclo de vida; la configuración del driver mediante parámetros ajustables en /boot/loader.conf y sysctls de escritura; estrategias de versionado y compatibilidad para ioctls, sysctls y el comportamiento visible desde el espacio de usuario; la gestión de recursos en los caminos de error mediante el patrón de goto etiquetado para limpieza; la modularización del driver en archivos de código fuente separados lógicamente; la disciplina de preparar un driver para uso en producción con MODULE_DEPEND, MODULE_PNP_INFO y un empaquetado sensato; y los mecanismos SYSINIT / SYSUNINIT / EVENTHANDLER que extienden el ciclo de vida de un driver más allá del simple MOD_LOAD y MOD_UNLOAD. El driver myfirst evoluciona de la versión 1.7-integration a 1.8-maintenance: incorpora myfirst_log.c y myfirst_log.h con una macro DLOG_RL respaldada por ppsratecheck, una separación entre myfirst_cdev.c y myfirst_bus.c de modo que los callbacks del cdev viven separados de la maquinaria de attach de Newbus, un documento MAINTENANCE.md, un manejador de eventos shutdown_pre_sync, un ioctl MYFIRSTIOC_GETCAPS que permite al espacio de usuario negociar los bits de características, y un script de regresión con la versión actualizada. El capítulo deja la Parte 5 completa: el driver sigue siendo comprensible, puede seguir ajustándose sin necesidad de recompilarlo, y ahora puede absorber el mantenimiento del próximo año sin volverse ilegible."
partNumber: 5
partName: "Debugging, Tools, and Real-World Practices"
chapter: 25
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 225
language: "es-ES"
---
# Temas avanzados y consejos prácticos

## Orientación para el lector y objetivos

El capítulo 24 cerró con un driver con el que el resto del sistema puede comunicarse. El driver `myfirst` en la versión `1.7-integration` tiene un nodo `/dev/myfirst0` limpio creado mediante `make_dev_s`, una cabecera ioctl pública compartida entre el kernel y el espacio de usuario, un subárbol sysctl por instancia bajo `dev.myfirst.0`, un tunable de arranque para la máscara de depuración, y un dispatcher de ioctls que respeta la regla de fallback `ENOIOCTL` para que los helpers del kernel como `FIONBIO` lleguen correctamente a la capa cdev. El driver compila, se carga, se ejecuta bajo estrés y sobrevive ciclos repetidos de `kldload` y `kldunload` sin filtrar OIDs ni nodos cdev. En todos los sentidos observables, el driver funciona.

El capítulo 25 trata sobre la diferencia entre un driver que *funciona* y un driver que es *mantenible*. Esas dos cualidades no son lo mismo, y la diferencia aparece poco a poco. Un driver que funciona supera su primera ronda de pruebas, se conecta limpiamente a su hardware y entra en uso. Un driver mantenible también hace eso, y además absorbe el siguiente año de correcciones de errores, adiciones de funcionalidades, cambios de portabilidad, nuevas revisiones de hardware y cambios en la API del kernel sin derrumbarse lentamente bajo su propio peso. El primero le da al desarrollador un buen día. El segundo le da al driver una buena década.

El capítulo 25 es el capítulo de cierre de la Parte 5. Donde el capítulo 23 enseñó observabilidad y el capítulo 24 enseñó integración, el capítulo 25 enseña los hábitos de ingeniería que preservan ambas cualidades a lo largo del tiempo. La Parte 6 comienza inmediatamente después con drivers específicos de transporte (USB en el capítulo 26, almacenamiento en el capítulo 27, redes en el capítulo 28 y siguientes), y cada uno de esos capítulos asumirá la disciplina introducida aquí. Sin registro de mensajes con tasa limitada, una tormenta de hotplug USB llena el buffer de mensajes. Sin una convención de errores coherente, un driver de almacenamiento y su periférico en CAM discrepan sobre lo que significa `EBUSY`. Sin tunables del cargador, un driver de red con una profundidad de cola predeterminada subóptima no puede ajustarse en una máquina de producción sin recompilar. Sin disciplina de versiones, una herramienta en espacio de usuario escrita para esta versión del driver interpreta silenciosamente de forma incorrecta un nuevo campo añadido dos meses después. Cada hábito es pequeño. Juntos, son lo que convierte a un driver en una pieza de FreeBSD de larga vida, en lugar de un experimento de laboratorio de corta duración.

El ejemplo continuo del capítulo sigue siendo el driver `myfirst`. Al comienzo del capítulo está en la versión `1.7-integration`. Al final del capítulo está en la versión `1.8-maintenance`, dividido en más archivos que antes, registrando mensajes sin inundar el buffer, devolviendo errores de un vocabulario coherente, configurable desde `/boot/loader.conf`, con un documento `MAINTENANCE.md` que explica el contrato de mantenimiento continuo, anunciando sus eventos a través del canal `devctl`, y enganchado a los eventos de apagado y de poca memoria del kernel mediante `EVENTHANDLER(9)`. Ninguna de esas adiciones requiere nuevos conocimientos de hardware. Todas ellas requieren una disciplina más afinada.

La Parte 5 cierra aquí con los hábitos que mantienen al driver coherente a medida que crece. El capítulo 22 hizo que el driver sobreviviera a un cambio de estado de energía. El capítulo 23 hizo que el driver te dijera lo que está haciendo. El capítulo 24 hizo que el driver encajara en el resto del sistema. El capítulo 25 hace que el driver conserve todas esas cualidades a medida que evoluciona. El capítulo 26 abrirá entonces la Parte 6 poniendo estas cualidades a trabajar frente a un transporte real, el Universal Serial Bus, donde cada atajo en el registro de mensajes o en el manejo de rutas de fallo queda expuesto por la velocidad y variedad del tráfico USB.

### Por qué la disciplina de mantenimiento merece un capítulo propio

Antes de continuar, vale la pena detenerse a reflexionar sobre si el registro con tasa limitada, el vocabulario de errno y los tunables del cargador realmente merecen un capítulo completo. Los capítulos anteriores ya han enseñado tanto. Añadir una macro de registro respaldada por `ppsratecheck(9)` parece pequeño. Estandarizar los códigos de error parece aún más pequeño. ¿Por qué extender el trabajo a lo largo de un capítulo largo cuando cada hábito parece un puñado de líneas?

La respuesta es que cada hábito es pequeño, y la ausencia de cada hábito es grande. Un driver que registra mensajes sin limitar la tasa está bien en el laboratorio y resulta catastrófico en producción la primera vez que un cable defectuoso desencadena diez mil reenumeraciones por segundo. Un driver que devuelve `EINVAL` cuando debería devolver `ENXIO`, y `ENXIO` cuando debería devolver `ENOIOCTL`, está bien cuando el autor es el único que lo llama, y es un informe de error esperando ocurrir cuando un segundo desarrollador escribe el primer helper en espacio de usuario. Un driver que deja que cada configuración predeterminada sea una constante en tiempo de compilación está bien para una sola persona y es inmanejable para un equipo que mantiene el mismo módulo en varias máquinas de producción con distintas cargas de trabajo. El capítulo 25 dedica tiempo a cada uno de estos hábitos porque el valor no se mide en el laboratorio, sino en el coste de mantenimiento a dos años que cada hábito reduce.

La primera razón por la que el capítulo se gana su lugar es que **estos hábitos determinan el aspecto del código base del driver a medida que crece**. Un lector que ha seguido los capítulos 23 y 24 ya ha visto el driver dividido en múltiples archivos: `myfirst.c`, `myfirst_debug.c`, `myfirst_ioctl.c`, `myfirst_sysctl.c`. Eso fue modularización hecha en pequeño, una superficie a la vez. El capítulo 25 retoma la modularización con la pregunta que la mayoría de los lectores aún no se han hecho: *¿qué aspecto tiene una disposición de código fuente mantenible una vez que el driver tiene una docena de archivos y tres desarrolladores?* El capítulo responde a esa pregunta con una división consciente entre la capa de attach de Newbus, la capa cdev, la capa ioctl, la capa sysctl y la capa de registro, y luego usa esa división para dar soporte a todos los demás hábitos que el capítulo enseña.

La segunda razón es que **estos hábitos determinan si el driver puede depurarse en producción**. Un driver que registra mensajes con criterio y devuelve errores informativos le da al operador suficiente información para redactar un informe de error útil. Un driver que registra demasiado o demasiado poco, o que inventa sus propias convenciones de errno, obliga al operador a razonar solo a partir de síntomas, y el desarrollador acaba persiguiendo problemas intermitentes a ciegas. El kit de herramientas de depuración del capítulo 23 es eficaz, pero depende de la cooperación del driver. Esa cooperación se construye aquí.

La tercera razón es que **estos hábitos hacen que el driver sea extensible sin romper a sus consumidores**. La cabecera `myfirst_ioctl.h` del capítulo 24 es ya un contrato entre el driver y el espacio de usuario. El capítulo 25 enseña al lector cómo evolucionar ese contrato, añadir un nuevo ioctl que los programas en espacio de usuario más antiguos puedan ignorar de forma segura, retirar un sysctl obsoleto sin romper los scripts de los administradores, e incrementar la versión del driver de un modo que los consumidores externos puedan comprobar en tiempo de ejecución. Sin esos hábitos, la primera v2 del driver obliga a reescribir todos los consumidores. Con ellos, el driver puede añadir funcionalidades durante una década y seguir ejecutando los helpers en espacio de usuario que se compilaron la primera semana que el driver se distribuyó.

El capítulo 25 se gana su lugar enseñando esas tres ideas juntas, de forma concreta, con el driver `myfirst` como ejemplo continuo. Un lector que termine el capítulo 25 será capaz de preparar cualquier driver de FreeBSD para el mantenimiento a largo plazo, de leer los patrones de endurecimiento para producción de otro driver y reconocer cuáles responden a principios y cuáles son ad hoc, de negociar la compatibilidad con herramientas existentes en espacio de usuario, y tendrá un driver `myfirst` en la versión `1.8-maintenance` que está visiblemente listo para comenzar la Parte 6.

### Dónde dejó el capítulo 24 al driver

Un breve resumen de en qué punto deberías encontrarte. El capítulo 25 extiende el driver producido al final de la Etapa 3 del capítulo 24, etiquetado como versión `1.7-integration`. Si alguno de los puntos siguientes es incierto, vuelve al capítulo 24 y corrígelo antes de comenzar este capítulo, porque el nuevo material asume que todos los primitivos del capítulo 24 están funcionando.

- Tu driver compila limpiamente y se identifica como `1.7-integration` en la salida de `kldstat -v`.
- Existe un nodo `/dev/myfirst0` tras `kldload`, con propiedad `root:wheel` y modo `0660`, y desaparece limpiamente al ejecutar `kldunload`.
- El módulo exporta cuatro ioctls: `MYFIRSTIOC_GETVER`, `MYFIRSTIOC_GETMSG`, `MYFIRSTIOC_SETMSG` y `MYFIRSTIOC_RESET`. El pequeño programa `myfirstctl` en espacio de usuario del capítulo 24 ejercita cada uno de ellos y devuelve éxito en los cuatro.
- El subárbol sysctl `dev.myfirst.0` lista al menos `version`, `open_count`, `total_reads`, `total_writes`, `message`, `message_len`, `debug.mask` y `debug.classes`.
- `sysctl dev.myfirst.0.debug.mask=0xff` habilita todas las clases de depuración, y la salida de registro posterior del driver muestra las etiquetas esperadas.
- El tunable de arranque `hw.myfirst.debug_mask_default`, colocado en `/boot/loader.conf`, se aplica antes del attach y establece el valor inicial del sysctl.
- Los ciclos repetidos de `kldload` y `kldunload` en bucle durante un minuto no dejan OIDs residuales, ni cdev huérfanos, ni memoria filtrada, según lo informado por `vmstat -m | grep myfirst`.
- Tu árbol de trabajo contiene `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`, `DMA.md`, `POWER.md`, `DEBUG.md` e `INTEGRATION.md` de los capítulos anteriores.
- Tu kernel de pruebas tiene habilitados `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS` y `DDB_CTF`. Los laboratorios del capítulo 25 dependen de `WITNESS` y de `INVARIANTS` con la misma firmeza que los del capítulo 24.

Ese driver es lo que el capítulo 25 extiende. Las adiciones son menores en líneas de código que en cualquier capítulo anterior de la Parte 5, pero mayores en superficie conceptual. Las nuevas piezas son: un par `myfirst_log.c` y `myfirst_log.h` construido alrededor de `ppsratecheck(9)`, una cadena de limpieza con goto etiquetado en `myfirst_attach`, un vocabulario de errores refinado en todo el dispatcher, un par de hooks `SYSINIT`/`SYSUNINIT` para la inicialización a nivel de driver, un manejador de eventos `shutdown_pre_sync`, un nuevo ioctl `MYFIRSTIOC_GETCAPS` que permite al espacio de usuario consultar bits de funcionalidad, una refactorización moderada que separa el attach de Newbus de `myfirst.c` en `myfirst_bus.c` y los callbacks cdev en `myfirst_cdev.c`, un documento `MAINTENANCE.md` que explica la política de incremento de versiones, un script de regresión actualizado, y un incremento de versión a `1.8-maintenance`.

### Qué aprenderás

Al final de este capítulo serás capaz de:

- Explicar por qué el logging sin restricciones en el kernel es un riesgo en producción, describir cómo `ppsratecheck(9)` limita los eventos por segundo, escribir una macro de logging con limitación de tasa que coopere con la máscara de debug del Capítulo 23, y reconocer las tres clases de mensajes de log que merecen estrategias de throttling distintas.
- Auditar las rutas `read`, `write`, `ioctl`, `open`, `close`, `attach`, `detach` y del manejador sysctl de un driver para verificar el uso correcto de errno. Distinguir `EINVAL` de `ENXIO`, `ENOIOCTL` de `ENOTTY`, `EBUSY` de `EAGAIN`, `EPERM` de `EACCES`, y `EIO` de `EFAULT`, y saber cuándo cada uno es el retorno adecuado.
- Añadir tunables del loader mediante `TUNABLE_INT_FETCH`, `TUNABLE_LONG_FETCH`, `TUNABLE_BOOL_FETCH` y `TUNABLE_STR_FETCH`, y combinarlos con sysctls de escritura para que un mismo parámetro pueda establecerse en el boot o ajustarse en tiempo de ejecución. Comprender cómo `CTLFLAG_TUN` coopera con los fetchers de tunables.
- Exponer la configuración como una superficie pequeña y bien documentada, en lugar de como un conjunto de variables de entorno ad hoc. Elegir entre tunables por driver y por instancia con criterio. Documentar la unidad, el rango y el valor por defecto de cada tunable.
- Versionar la interfaz visible por el usuario de un driver con una división estable en tres niveles: la cadena de versión legible por humanos en `dev.myfirst.0.version`, el entero `MODULE_VERSION` utilizado por el mecanismo de dependencias de módulos del kernel, y el entero de formato de protocolo `MYFIRST_IOCTL_VERSION` integrado en la cabecera pública.
- Añadir un nuevo ioctl a una cabecera pública existente sin romper las llamadas antiguas, retirar un ioctl obsoleto con el período de deprecación correcto, y proporcionar una máscara de bits de capacidades mediante `MYFIRSTIOC_GETCAPS` para que los programas en espacio de usuario puedan detectar la disponibilidad de características sin prueba y error.
- Estructurar las rutas attach y detach de un driver con el patrón `goto fail;` para que cada asignación tenga exactamente un único punto de limpieza, cada limpieza se ejecute en orden inverso a la asignación, y un attach parcial nunca deje atrás un recurso que la ruta detach no liberará.
- Dividir un driver en archivos fuente lógicos según la responsabilidad en lugar del tamaño del archivo. Elegir entre un único archivo grande, una pequeña colección de archivos centrados en temas concretos, y un árbol de subsistema completo, y saber cuándo corresponde cada opción.
- Preparar un driver para uso en producción con `MODULE_DEPEND`, `MODULE_PNP_INFO`, un manejador `modevent` con buen comportamiento que acepte `MOD_QUIESCE` cuando el driver pueda pausarse limpiamente, un sistema de build pequeño que distribuya tanto el módulo como su documentación, y un patrón preparado para `devd(8)` que anuncie eventos del driver mediante `devctl_notify`.
- Usar `SYSINIT(9)` y `SYSUNINIT(9)` para enganchar la inicialización y el desmontaje globales del driver en etapas específicas del subsistema del kernel, y comprender la diferencia entre los manejadores de eventos de módulo y los hooks de inicialización a nivel de subsistema.
- Registrar y desregistrar callbacks en eventos conocidos del kernel mediante `EVENTHANDLER(9)`: `shutdown_pre_sync`, `shutdown_post_sync`, `shutdown_final`, `vm_lowmem`, `power_suspend_early` y `power_resume`. Saber cómo elegir una prioridad y cómo garantizar la desregistración en el detach.

La lista es larga porque la disciplina de mantenimiento toca muchas superficies pequeñas a la vez. Cada punto es concreto y enseñable. El trabajo del capítulo es convertirlos en hábito.

### Qué no cubre este capítulo

Varios temas adyacentes se aplazan explícitamente para que el Capítulo 25 se mantenga centrado en la disciplina de mantenimiento al nivel adecuado para un lector que termina la Parte 5.

- **Los patrones de producción específicos de cada transporte**, como las tormentas de hotplug de USB, los eventos de estado de enlace SATA y el manejo de cambios de medio en Ethernet, pertenecen a la Parte 6, donde se enseña cada transporte en detalle. El Capítulo 25 enseña los hábitos *generales*; el Capítulo 26 y los posteriores los aplican específicamente a USB.
- **El diseño completo de un marco de pruebas**, incluyendo los arneses de regresión que se ejecutan en varias configuraciones del kernel y escenarios de inyección de fallos, pertenece a las secciones de pruebas sin hardware de los Capítulos 26, 27 y 28. El Capítulo 25 añade una línea más al script de regresión existente; no introduce un arnés completo.
- **`fail(9)` y `fail_point(9)`**, las utilidades de inyección de errores del kernel, se aplazan hasta el Capítulo 28, junto al trabajo con el driver de almacenamiento donde se usan con más frecuencia.
- **La integración continua, la firma de paquetes y la distribución** son preocupaciones operativas del proyecto que distribuye el driver, no del código fuente del driver en sí. El capítulo dice lo justo sobre el empaquetado para que el driver sea reproducible.
- **Los hooks de `MAC(9)` (Mandatory Access Control)** son un marco especializado que se presenta mejor en un capítulo posterior centrado en seguridad.
- **La estabilidad de `kbi(9)` y el congelamiento del ABI** son decisiones de ingeniería de versiones tomadas por el proyecto FreeBSD, no por el autor del driver. El capítulo señala las implicaciones del ABI en las funciones exportadas por el kernel, pero no cubre la ingeniería de versiones en profundidad.
- **La integración del modo de capacidades de `capsicum(4)`** para los programas auxiliares en espacio de usuario es un tema de seguridad en espacio de usuario, no del driver en sí. El `myfirstctl` del capítulo sigue siendo una herramienta UNIX tradicional.
- **Los patrones avanzados de concurrencia** como `epoch(9)`, los locks de lectura mayoritaria y las colas sin locks. Solo se mencionan de pasada; el único mutex del softc del driver sigue siendo suficiente en esta etapa.

Mantenerse dentro de esos límites hace que el Capítulo 25 sea un capítulo sobre *disciplina de mantenimiento*, no un capítulo sobre todas las técnicas que un desarrollador de kernel experimentado podría usar en un problema complejo del kernel.

### Inversión de tiempo estimada

- **Solo lectura**: tres o cuatro horas. Las ideas del Capítulo 25 son conceptualmente más ligeras que las del Capítulo 24, y gran parte del vocabulario ya resulta familiar. El trabajo del capítulo es convertir primitivas conocidas en disciplina.
- **Lectura más escritura de los ejemplos trabajados**: ocho o diez horas repartidas en dos o tres sesiones. El driver evoluciona a través de cuatro etapas cortas (registro con límite de frecuencia, auditoría de errores, disciplina de tunables y versiones, SYSINIT y EVENTHANDLER), cada una más pequeña que una sola etapa del Capítulo 24. La refactorización de la Sección 6 toca varios archivos pero cambia poco código; la mayor parte del trabajo consiste en mover el código existente a su nuevo lugar.
- **Lectura más todos los laboratorios y desafíos**: doce o quince horas repartidas en tres o cuatro sesiones. Los laboratorios incluyen una reproducción y reparación de una inundación de registros, una auditoría de errno con `truss`, un laboratorio de tunables que arranca una VM dos veces con diferentes valores en `/boot/loader.conf`, un laboratorio de fallo deliberado en el attach que ejercita cada etiqueta de la cadena `goto fail;`, un laboratorio de `shutdown_pre_sync` que confirma que el callback se ejecuta en el momento correcto, y un recorrido por el script de regresión que une todo.

La Sección 5 (gestión de rutas de fallo) es la más densa en nueva disciplina, no en nuevo vocabulario. El patrón `goto fail;` en sí es mecánico; el truco está en leer una función attach real de FreeBSD y ver cada asignación como candidata para una nueva etiqueta. Si el patrón parece mecánico en la primera lectura, esa es la señal de que se ha convertido en un hábito.

### Requisitos previos

Antes de empezar este capítulo, confirma:

- El código fuente de tu driver coincide con la Etapa 3 del Capítulo 24 (`1.7-integration`). Se dan por sentadas todas las primitivas del Capítulo 24: la creación del cdev basada en `make_dev_s`, el despachador `myfirst_ioctl.c`, la construcción del árbol `myfirst_sysctl.c`, la triple `MYFIRST_VERSION`, `MODULE_VERSION` y `MYFIRST_IOCTL_VERSION`, y el patrón de `sysctl_ctx_free` por dispositivo.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con `/usr/src` en disco y coincidiendo con el kernel en ejecución.
- Un kernel de depuración con `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS` y `DDB_CTF` está construido, instalado y arrancando limpiamente.
- Tienes guardado un snapshot del estado `1.7-integration` en tu VM. Los laboratorios del Capítulo 25 incluyen escenarios de fallo deliberado en el attach, y un snapshot hace que la recuperación sea sencilla.
- Los siguientes comandos de espacio de usuario están en tu PATH: `dmesg`, `sysctl`, `kldstat`, `kldload`, `kldunload`, `devctl`, `devd`, `cc`, `make`, `dtrace`, `truss`, `ktrace`, `kdump` y `procstat`.
- Te sientes cómodo editando `/boot/loader.conf` y reiniciando una VM para aplicar nuevos tunables.
- Tienes el programa complementario `myfirstctl` del Capítulo 24 compilado y funcionando.

Si alguno de los puntos anteriores está sin resolver, corrígelo ahora. La disciplina de mantenimiento es más fácil de aprender en un driver que ya cumple las reglas de los capítulos anteriores que en uno que todavía tiene problemas pendientes de etapas anteriores.

### Cómo sacar el máximo partido de este capítulo

Cinco hábitos resultan especialmente útiles en este capítulo, más que en cualquiera de los capítulos anteriores de la Parte 5.

Primero, mantén abiertas cuatro páginas de manual breves en una pestaña del navegador o en un panel del terminal: `ppsratecheck(9)`, `style(9)`, `sysctl(9)` y `module(9)`. La primera es la documentación canónica de la API de comprobación de frecuencia. La segunda es el estilo de codificación de FreeBSD. La tercera explica el marco sysctl. La cuarta es el contrato del manejador de eventos del módulo. Ninguna de ellas es larga; vale la pena hojear cada una una vez al inicio del capítulo y volver a consultarlas cuando el texto diga «consulta la página de manual para más detalles».

Segundo, ten a mano tres drivers reales. `/usr/src/sys/dev/mmc/mmcsd.c` muestra `ppsratecheck` utilizado para limitar un `device_printf` en producción. `/usr/src/sys/dev/virtio/block/virtio_blk.c` muestra una cadena `goto fail;` limpia en su ruta de attach y un conjunto de tunables de calidad para producción. `/usr/src/sys/dev/e1000/em_txrx.c` muestra cómo un driver complejo distribuye el registro, los tunables y el despacho entre varios archivos. El Capítulo 25 hace referencia a cada uno en el momento oportuno; leerlos una vez ahora le da al resto del capítulo anclajes concretos.

> **Una nota sobre los números de línea.** Cuando el capítulo señala un lugar concreto en `mmcsd.c`, `virtio_blk.c` o `em_txrx.c`, el indicador es un símbolo con nombre, no una línea numérica. `ppsratecheck`, las etiquetas `goto fail;` en `virtio_blk_attach` y las llamadas `TUNABLE_*_FETCH` permanecen localizables por sus nombres en futuras revisiones del árbol, aunque las líneas que los rodean se muevan. Los ejemplos de auditoría que verás más adelante en el capítulo utilizan la notación `file:line` puramente como transcripción de muestra de una herramienta, y llevan la misma advertencia.

Tercero, escribe cada cambio en el driver `myfirst` a mano. Las adiciones del Capítulo 25 son el tipo de cambios que un desarrollador hace por reflejo tras un año de trabajo de mantenimiento. Escribirlos ahora construye ese reflejo; pegarlos se salta la lección.

Cuarto, después del material sobre tunables de la Sección 3, reinicia tu VM al menos una vez con un nuevo ajuste en `/boot/loader.conf` y observa cómo el driver lo recoge durante el attach. Los tunables son una de esas características que se sienten abstractas hasta que ves un valor real fluir desde el bootloader a través del kernel hasta tu softc. Dos reinicios y un comando `sysctl` es todo lo que hace falta.

Quinto, cuando la sección sobre `goto fail;` te pida que introduzcas un fallo deliberado en `myfirst_attach`, hazlo de verdad. Inyectar un solo `return (ENOMEM);` en el medio del attach y ver cómo la cadena de limpieza se desenrolla correctamente es la mejor manera de interiorizar el patrón. El capítulo sugiere un lugar concreto para inyectar el fallo, y el script de regresión confirma que la limpieza realmente se ejecutó.

### Hoja de ruta del capítulo

Las secciones, en orden, son:

1. **Limitación de frecuencia y buenas prácticas de registro.** Por qué el registro del kernel sin control es un riesgo en producción; las tres clases de mensajes de registro de un driver (ciclo de vida, error, depuración); `ppsratecheck(9)` y `ratecheck(9)` como la respuesta de FreeBSD a las inundaciones de registros; una macro `DLOG_RL` con límite de frecuencia que coopera con la máscara de depuración del Capítulo 23; los niveles de prioridad de `log(9)` y su relación con `device_printf` y `printf`; lo que realmente cuesta el buffer de mensajes del kernel y cómo no gastarlo de forma descuidada.
2. **Informe de errores y convenciones de retorno.** Por qué la disciplina de errno es un contrato con cada llamador; el pequeño vocabulario de errno del kernel que un driver usa habitualmente; cuándo es apropiado cada uno; `ENOIOCTL` frente a `ENOTTY` y por qué el driver nunca debe devolver `EINVAL` desde el ioctl por defecto; los códigos de retorno del manejador de sysctl; los códigos de retorno del manejador de eventos del módulo; una lista de comprobación que el lector puede aplicar a todos los drivers que escriba a partir de ahora.
3. **Configuración del driver mediante tunables del cargador y sysctl.** La diferencia entre los tunables de `/boot/loader.conf` y los sysctls en tiempo de ejecución; la familia `TUNABLE_*_FETCH` y el flag `CTLFLAG_TUN`; tunables por driver frente a tunables por instancia; cómo documentar un tunable para que los operadores puedan fiarse de él; un laboratorio guiado que arranca una VM con un tunable en tres posiciones diferentes y observa el efecto en el árbol sysctl del driver.
4. **Estrategias de versionado y compatibilidad.** La triple división de versiones (`MODULE_VERSION` entero, `MYFIRST_VERSION` cadena legible, `MYFIRST_IOCTL_VERSION` entero en formato de cable); cómo se usa cada una; cómo añadir un nuevo ioctl sin romper a los llamadores antiguos; cómo retirar un ioctl obsoleto; `MYFIRSTIOC_GETCAPS` y la idea del bitmask de capacidades; cómo un driver puede deprecar un OID de sysctl de forma elegante en ausencia de un flag del kernel dedicado; cómo `MODULE_DEPEND` impone una versión mínima de un módulo dependencia.
5. **Gestión de recursos en rutas de fallo.** El problema de la limpieza ante fallos en `myfirst_attach`; el patrón `goto fail;` y por qué el desenrollado lineal supera a las cadenas de `if` anidados; las convenciones de nomenclatura de etiquetas (`fail_mtx`, `fail_cdev`, `fail_sysctl`); los errores comunes (caer en el bloque de limpieza tras el éxito, omitir una etiqueta, añadir un recurso sin añadir su limpieza); una pequeña disciplina de funciones auxiliares que reduce la duplicación; un laboratorio de fallo deliberado que prueba toda la cadena.
6. **Modularización y separación de responsabilidades.** Dividir un driver en archivos según los ejes de responsabilidad; la división canónica para un driver de caracteres (`myfirst.c`, `myfirst_bus.c`, `myfirst_cdev.c`, `myfirst_ioctl.c`, `myfirst_sysctl.c`, `myfirst_debug.c`, `myfirst_log.c`); cabeceras públicas frente a privadas; cómo organizar el `Makefile` para que todos estos archivos se construyan en un único `.ko`; cuándo ayuda la modularización y cuándo estorba; cómo un equipo de desarrolladores usa la división para reducir los conflictos de fusión.
7. **Preparación para el uso en producción.** `MODULE_DEPEND` y la imposición de dependencias; `MODULE_PNP_INFO` para la carga automática; `MOD_QUIESCE` y el contrato de pausa antes de la descarga; un patrón del sistema de construcción que instala el módulo y su documentación; una regla de `devd(8)` que reacciona a los eventos del driver; un pequeño documento `MAINTENANCE.md` que establece por escrito el contrato de mantenimiento del driver.
8. **SYSINIT, SYSUNINIT y EVENTHANDLER.** La maquinaria más amplia del ciclo de vida del kernel más allá de `MOD_LOAD` y `MOD_UNLOAD`; `SYSINIT(9)` y `SYSUNINIT(9)` con identificadores de subsistema y constantes de orden; ejemplos reales de FreeBSD de cada uno; `EVENTHANDLER(9)` para notificaciones transversales (`shutdown_pre_sync`, `vm_lowmem`, `power_resume`); cómo registrarse y darse de baja limpiamente; cómo un driver usa los tres mecanismos sin caer en la sobreingeniería.

Tras las ocho secciones vienen un conjunto de laboratorios prácticos que ejercitan cada disciplina, un conjunto de ejercicios de desafío que ponen a prueba al lector sin introducir nuevas bases, una referencia de resolución de problemas para los síntomas que la mayoría de los lectores encontrará, un apartado de cierre que concluye la historia del Capítulo 25 y abre la del Capítulo 26, un puente al siguiente capítulo, una tarjeta de referencia rápida y un glosario.

Si es tu primera vez aquí, lee en orden y realiza los laboratorios en secuencia. Si estás revisando el material, las secciones 1 y 5 son autónomas y se prestan bien a una lectura en una sola sesión. La sección 8 es un breve remate conceptual al final del capítulo; depende ligeramente del material de uso en producción de la sección 7 y es fácil de reservar para una segunda sesión.

Una pequeña nota antes de que empiece el trabajo técnico. El capítulo 25 es el último capítulo de la Parte 5. Sus adiciones son más pequeñas que las del capítulo 24, pero tocan casi todos los archivos del driver. Espera dedicar más tiempo a releer tu propio código anterior que a escribir código nuevo. Eso también es disciplina de mantenimiento. Un driver que relees con paciencia es un driver que puedes modificar con confianza; un driver que modificas con confianza es un driver que puedes mantener vivo.

## Sección 1: Limitación de tasa y etiqueta de registro

La primera disciplina que enseña este capítulo es la disciplina de no hablar demasiado. El driver `myfirst` del final del Capítulo 24 registra eventos al hacer attach, al hacer detach, cuando un cliente abre o cierra el dispositivo, cuando una lectura o escritura cruza el límite, cuando se despacha un ioctl y cuando se ajusta la máscara de depuración. Cada una de esas líneas de registro se introdujo por una razón válida, y cada una de ellas es útil cuando un evento ocurre una sola vez. Lo que ninguna de las líneas de registro del Capítulo 24 contempla es qué ocurre cuando el mismo evento se dispara cien mil veces por segundo.

Esta sección explica por qué esa pregunta importa más de lo que parece, presenta las tres categorías de mensaje de registro de driver que se comportan de manera diferente bajo presión, enseña las primitivas de comprobación de tasa de FreeBSD (`ratecheck(9)` y `ppsratecheck(9)`), y muestra cómo construir un pequeño macro disciplinado sobre ellas que coopere con el mecanismo de máscara de depuración del Capítulo 23. Al final de la sección, el driver `myfirst` dispondrá de un par `myfirst_log.c` y `myfirst_log.h`, y su buffer de mensajes dejará de convertirse en ruido bajo presión.

### El problema del registro sin restricciones

Un mensaje del kernel es barato de escribir y caro de transportar. `device_printf(dev, "something happened\n")` es una única llamada a función, decenas de nanosegundos en una CPU moderna, y retorna casi de inmediato. El coste no está en la llamada; está en todo lo que le ocurre a los bytes después. La cadena formateada se copia en el buffer de mensajes del kernel, una zona circular de memoria del kernel cuyo tamaño se fija en el arranque. Se entrega al dispositivo de consola si la consola está conectada (con frecuencia un puerto serie en una VM, con una tasa de bits finita). Se envía al demonio syslog que se ejecuta en espacio de usuario a través del camino `log(9)` si el driver usa esa ruta, y después a través de `newsyslog(8)` hacia `/var/log/messages` en disco. Cada uno de esos pasos tiene un coste, y todos ellos son síncronos en el momento en que el driver escribe la línea.

Cuando el driver escribe una línea, nada de esto importa. Cuando el driver escribe un millón de líneas por segundo, todo ello importa. El buffer de mensajes del kernel se llena, y los mensajes más antiguos se sobreescriben antes de que nadie los lea. La consola, que suele funcionar a 115200 baudios, se queda atrás y no puede ponerse al día, lo que a su vez ejerce contrapresión sobre el camino del kernel que escribió la línea, es decir, el camino rápido de tu driver. El demonio syslog se despierta, trabaja y vuelve a dormirse muchas veces por segundo, robando ciclos a otros procesos. El disco donde reside `/var/log/messages` se llena a una tasa predecible, y un driver que registra diez mil líneas por segundo puede llenar una partición de tamaño razonable en una tarde.

Ninguno de estos síntomas está causado por un error en la lógica del driver. Están causados por el *volumen de registro* del driver, que a su vez lo provoca el hecho de que el driver emita una línea de registro razonable en cada evento. Las líneas de registro razonables están bien mientras los eventos sean raros. Se convierten en un riesgo cuando los eventos son frecuentes. Toda la habilidad de la etiqueta de registro consiste en saber, en el momento en que escribes una línea de registro, si el evento que la origina es raro o frecuente, y escribir el código de forma que la repetición incontrolada no pueda convertir un registro de evento raro en un registro de evento frecuente.

Un ejemplo concreto de un driver real ilustra la idea. Imagina un controlador SSD PCIe que notifica a su driver una condición de cola llena recuperable. En un sistema sano, esa condición es suficientemente rara como para que registrar cada ocurrencia sea útil. En un sistema defectuoso puede producirse cientos de veces por segundo hasta que alguien reemplace el hardware. Si el driver escribe una línea cada vez, el buffer de mensajes se llena de líneas casi idénticas, todos los mensajes anteriores de ese arranque se sobreescriben y se pierden, y el operador que intenta diagnosticar el problema leyendo `dmesg` solo ve la última página de la avalancha. El comportamiento real del hardware queda oscurecido por la reacción del driver ante él. Una línea de registro con limitación de tasa habría mostrado las primeras ocurrencias, la tasa y luego un recordatorio periódico; el contexto anterior en `dmesg` habría sobrevivido; el operador habría tenido algo con lo que trabajar.

La lección se generaliza. La disciplina correcta de registro no es "registra menos" ni "registra más", sino "registra a una tasa que siga siendo útil independientemente de con qué frecuencia se dispare el evento subyacente". El resto de esta sección enseña esa disciplina de manera concreta.

### Tres categorías de mensaje de registro de driver

Antes de elegir la política de limitación correcta, conviene nombrar las tres categorías de mensaje de registro que emite típicamente un driver. Cada categoría tiene una historia de limitación diferente.

La primera categoría son los **eventos de ciclo de vida**. Son los mensajes que marcan attach, detach, suspend, resume, la carga del módulo y la descarga del módulo. Ocurren una vez por transición de ciclo de vida, normalmente pocas veces a lo largo de la vida de un módulo. No se necesita limitación; el volumen es naturalmente bajo. Aplicar limitación de tasa a los mensajes de ciclo de vida sería un error, ya que ocultaría transiciones de estado importantes.

La segunda categoría son los **mensajes de error y advertencia**. Son mensajes que notifican algo que el driver considera incorrecto. Por construcción, cada uno de ellos debería ser infrecuente; si una advertencia se dispara cien veces por segundo, la advertencia te está diciendo algo sobre la tasa de los eventos subyacentes, y esa información merece conservarse aunque el evento se repita. Los mensajes de error y advertencia se benefician enormemente de la limitación de tasa, pero el límite de tasa debería preservar al menos un mensaje por ráfaga y hacer visible la propia *tasa*.

La tercera categoría son los **mensajes de depuración y trazado**. Son los mensajes bajo los macros `DPRINTF` del Capítulo 23. Son intencionalmente detallados cuando la máscara de depuración está activa y silenciosos cuando la máscara está desactivada. Limitarlos en el punto de emisión añade ruido a lo que ya es un camino de baja señal; la disciplina correcta es evitar emitirlos cuando la máscara está desactivada, que es exactamente lo que ya hace el `DPRINTF` existente. Los mensajes de depuración y trazado no necesitan limitación de tasa adicional, pero sí necesitan que el usuario pueda desactivarlos por completo con un único comando `sysctl`. El mecanismo del Capítulo 23 ya proporciona eso.

Con las tres categorías nombradas, el resto de la sección se centra en la segunda. Los mensajes de ciclo de vida están bien tal como están. Los mensajes de depuración los gestiona la máscara existente. Los mensajes de error y advertencia son donde reside la verdadera disciplina.

### Introducción a `ratecheck` y `ppsratecheck`

El kernel de FreeBSD proporciona dos primitivas estrechamente relacionadas para la salida con limitación de tasa. Ambas residen en `/usr/src/sys/kern/kern_time.c` y se declaran en `/usr/src/sys/sys/time.h`.

`ratecheck(struct timeval *lasttime, const struct timeval *mininterval)` es la más sencilla de las dos. El llamador mantiene un `struct timeval` que recuerda cuándo se disparó el evento por última vez, junto con un intervalo mínimo entre impresiones permitidas. En cada llamada, `ratecheck` compara el tiempo actual con `*lasttime`, y si ha transcurrido `mininterval`, actualiza `*lasttime` y devuelve 1. En caso contrario devuelve 0. El código llamador imprime solo cuando el retorno es 1. El resultado es un límite simple en la tasa de impresiones: como máximo una impresión por cada `mininterval`.

`ppsratecheck(struct timeval *lasttime, int *curpps, int maxpps)` es la forma más utilizada habitualmente en los drivers. Su nombre es un legado del caso de uso de telemetría de pulsos por segundo para el que se escribió originalmente. El código fuente del kernel lo expone mediante un `#define` en `/usr/src/sys/sys/time.h`:

```c
int    eventratecheck(struct timeval *, int *, int);
#define ppsratecheck(t, c, m) eventratecheck(t, c, m)
```

La llamada acepta un puntero a una marca de tiempo, un puntero a un contador de eventos en la ventana actual de un segundo y el número máximo de eventos por segundo permitidos. En cada llamada, si el segundo no ha vuelto a empezar, el contador se incrementa. Si el contador supera `maxpps`, la función devuelve 0 y el llamador suprime su salida. Cuando comienza un nuevo segundo, el contador se reinicia a 1 y la función devuelve 1, permitiendo una impresión para el nuevo segundo. Un valor especial de `maxpps == -1` desactiva la limitación de tasa por completo (útil para los caminos de depuración).

Ambas primitivas son baratas: una comparación y una actualización aritmética, sin locks. Ambas son seguras de llamar desde cualquier contexto en que el driver actualmente llama a `device_printf`, incluidos los manejadores de interrupción, siempre que el almacenamiento al que acceden sea estable en ese contexto. En la práctica, los drivers guardan el `struct timeval` y el contador dentro de su softc, protegidos por el mismo lock que protege el punto de registro, o utilizan estado por CPU donde resulte conveniente.

Un ejemplo breve del árbol de FreeBSD muestra el patrón en uso real. El driver de tarjeta MMC SD, `/usr/src/sys/dev/mmc/mmcsd.c`, aplica limitación de tasa a un aviso sobre errores de escritura para que una tarjeta defectuosa no inunde el registro:

```c
if (ppsratecheck(&sc->log_time, &sc->log_count, LOG_PPS))
        device_printf(dev, "Error indicated: %d %s\n",
            err, mmcsd_errmsg(err));
```

El driver almacena `log_time` y `log_count` en su softc, elige un `LOG_PPS` razonable (normalmente entre 5 y 10) y envuelve la llamada a `device_printf` en la comprobación de tasa. Los primeros errores de cada segundo producen líneas de registro; los siguientes centenares en ese mismo segundo no producen nada.

Esa es toda la idea. Todo lo que sigue en esta sección trata de hacer lo mismo con más estructura, más disciplina y menos repetición.

### Un macro de registro con limitación de tasa sencillo

El objetivo es un macro que el driver pueda usar en lugar del `device_printf` desnudo en cualquier camino de error o advertencia donde el evento pueda repetirse. El macro debe:

1. Descartar silenciosamente la salida cuando se supere el límite de tasa.
2. Permitir una tasa diferente por punto de llamada, o al menos por categoría.
3. Cooperar con el mecanismo de máscara de depuración del Capítulo 23, de modo que la salida de depuración siga estando controlada por la máscara y no por el limitador de tasa.
4. Compilarse fuera de las builds sin depuración si el driver así lo decide, sin coste en tiempo de ejecución.

Una implementación mínima tiene este aspecto. En un nuevo `myfirst_log.h`:

```c
#ifndef _MYFIRST_LOG_H_
#define _MYFIRST_LOG_H_

#include <sys/time.h>

struct myfirst_ratelimit {
        struct timeval rl_lasttime;
        int            rl_curpps;
};

/*
 * Default rate for warning messages: at most 10 per second per call
 * site.  Chosen to keep the log readable under a burst while still
 * showing the rate itself.
 */
#define MYF_RL_DEFAULT_PPS  10

/*
 * DLOG_RL - rate-limited device_printf.
 *
 * rlp must point at a per-call-site struct myfirst_ratelimit stored in
 * the driver (typically in the softc).  pps is the maximum allowed
 * prints per second.  The remaining arguments match device_printf.
 */
#define DLOG_RL(sc, rlp, pps, fmt, ...) do {                            \
        if (ppsratecheck(&(rlp)->rl_lasttime, &(rlp)->rl_curpps, pps))  \
                device_printf((sc)->sc_dev, fmt, ##__VA_ARGS__);        \
} while (0)

#endif /* _MYFIRST_LOG_H_ */
```

En el softc, reserva una o más estructuras de límite de tasa:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct myfirst_ratelimit sc_rl_ioerr;
        struct myfirst_ratelimit sc_rl_short;
};
```

En cada punto de error, reemplaza el `device_printf` desnudo por `DLOG_RL`:

```c
/* Old:
 * device_printf(sc->sc_dev, "I/O error on read, ENXIO\n");
 */
DLOG_RL(sc, &sc->sc_rl_ioerr, MYF_RL_DEFAULT_PPS,
    "I/O error on read, ENXIO\n");
```

El macro usa el operador coma dentro de un bloque `do { ... } while (0)` para que encaje en cualquier lugar donde encaje una sentencia, incluidos los cuerpos de `if` y `else` sin llaves. La llamada a `ppsratecheck` es poco costosa; cuando se supera el límite de tasa, simplemente no se llama a `device_printf`. Cuando no se supera el límite de tasa, el comportamiento es idéntico al de un `device_printf` directo.

Un detalle pequeño pero importante: cada punto de llamada debe tener su propia `struct myfirst_ratelimit`. Compartir una estructura entre varios puntos de llamada no relacionados significa que el primer camino que se dispare en cada segundo suprimirá todos los demás durante el resto de ese segundo. En un driver con un puñado de errores infrecuentes pero posibles, reserva una estructura de límite de tasa por categoría, nómbrala según la categoría y úsala de forma coherente.

### Cooperación con la máscara de depuración del Capítulo 23

El macro con limitación de tasa resuelve el caso de errores y advertencias. El caso de depuración ya tiene su propio mecanismo del Capítulo 23:

```c
DPRINTF(sc, MYF_DBG_IO, "read: %zu bytes requested\n", uio->uio_resid);
```

El macro `DPRINTF` se expande a nada cuando el bit correspondiente en `sc_debug` está a cero, por lo que la salida de depuración con la máscara silenciosa (`mask = 0`) no tiene coste en tiempo de ejecución. No hay necesidad de aplicar limitación de tasa a la salida de depuración: el operador la activa cuando quiere verla y la desactiva cuando no. Si el operador activa `MYF_DBG_IO` en un dispositivo ocupado y ve una avalancha de salida, ese es el comportamiento previsto; buscaba esa avalancha. El macro de limitación de tasa y el macro de depuración tienen propósitos diferentes y no deben combinarse.

Donde sí se encuentran es en la línea de registro ocasional que conceptualmente es una advertencia pero que el desarrollador quiere poder silenciar por completo. Para esos casos, el patrón correcto es condicionar la llamada a `DLOG_RL` a un bit de depuración:

```c
if ((sc->sc_debug & MYF_DBG_IO) != 0)
        DLOG_RL(sc, &sc->sc_rl_short, MYF_RL_DEFAULT_PPS,
            "short read: %d bytes\n", n);
```

La limitación de tasa actúa bajo la máscara de depuración, y la salida es tanto opcional como acotada. Este es un patrón minoritario; la mayoría de las advertencias deben dispararse incondicionalmente con un límite de tasa, y la mayoría de las impresiones de depuración deben estar controladas por máscara sin límite de tasa.

### Niveles de prioridad de `log(9)`

Vale la pena mencionar aquí un tercer primitivo de registro: `log(9)`. A diferencia de `device_printf`, que siempre envía los mensajes a través del buffer de mensajes del kernel, `log` los dirige por la ruta de syslog asignándoles una prioridad syslog. La función reside en `/usr/src/sys/kern/subr_prf.c` y acepta una prioridad definida en `/usr/src/sys/sys/syslog.h`:

```c
void log(int level, const char *fmt, ...);
```

Las prioridades más habituales son: `LOG_EMERG` (0) para condiciones en las que el sistema no puede funcionar, `LOG_ALERT` (1) para situaciones que requieren acción inmediata, `LOG_CRIT` (2) para condiciones críticas, `LOG_ERR` (3) para condiciones de error, `LOG_WARNING` (4) para advertencias, `LOG_NOTICE` (5) para condiciones notables pero normales, `LOG_INFO` (6) para mensajes informativos y `LOG_DEBUG` (7) para mensajes de depuración. Un driver que utiliza `log(LOG_WARNING, ...)` para su ruta de advertencias en lugar de `device_printf` gana la posibilidad de que `syslog.conf(5)` lo filtre hacia un archivo de log independiente, sin que el autor del driver tenga que hacer nada más.

El inconveniente es que `log(9)` no antepone el nombre del dispositivo. Un driver que usa `log` tiene que incluir el nombre del dispositivo en el mensaje de forma manual, lo que resulta verboso. Por eso, la mayoría de los drivers de FreeBSD prefieren `device_printf` para los mensajes específicos del driver y reservan `log` para las notificaciones transversales. El driver `myfirst` sigue la misma convención: `device_printf` para todo lo que el operador debe leer con `dmesg`, y `log` para nada en esta etapa.

Una pauta pragmática: usa `device_printf` cuando el mensaje trate *sobre este dispositivo*. Usa `log(9)` cuando el mensaje trate *sobre una condición transversal* cuyo lugar natural de observación sea la infraestructura syslog, como un evento de autenticación o una infracción de política. El código del driver rara vez necesita el segundo tipo.

### El buffer de mensajes del kernel y su coste

Un detalle técnico más antes de cerrar la sección. El buffer de mensajes del kernel (`msgbuf`) es un buffer circular de tamaño fijo dentro del kernel, asignado durante el boot. Su tamaño lo controla el parámetro ajustable `kern.msgbufsize`, que por defecto es 96 KiB en amd64 y puede aumentarse en `/boot/loader.conf`. Todas las llamadas a `printf`, `device_printf` y `log` pasan por este buffer. Cuando el buffer se llena, los mensajes más antiguos se sobrescriben. El contenido del buffer es lo que imprime `dmesg`.

Se derivan dos consecuencias prácticas. La primera: una avalancha de mensajes cortos puede desplazar los mensajes anteriores que el operador necesita. Una línea que diga "hello" ocupa unas pocas docenas de bytes; un buffer de 96 KiB alberga tal vez tres mil líneas de ese tipo; un bucle que imprime a diez mil líneas por segundo desaloja el registro de boot completo en menos de medio segundo. La segunda: producir un mensaje formateado no es gratuito. El formateo al estilo `printf` tiene un coste de CPU, y dentro de un manejador de interrupciones o de una ruta crítica ese coste se refleja directamente en las cifras de latencia. La macro con limitación de tasa ayuda con la primera consecuencia. La segunda es la razón por la que los mensajes de depuración se controlan con máscara: un `DPRINTF` con máscara cero se compila como una instrucción vacía en tiempo de ejecución, omitiendo tanto el formateo como el almacenamiento.

Aumentar `kern.msgbufsize` es una respuesta razonable ante una máquina que pierde repetidamente los registros de boot, pero no es un sustituto de la limitación de tasa. Un buffer mayor simplemente compra más espacio antes de que la avalancha desplace los mensajes antiguos; la limitación de tasa reduce la avalancha en sí. Ambas medidas merecen la pena. `kern.msgbufsize=262144` en `/boot/loader.conf` es una elección habitual en máquinas de producción. No es una acción del Capítulo 25, porque el driver no puede cambiar el tamaño del buffer en tiempo de ejecución.

### Un ejemplo práctico: la ruta de lectura de `myfirst`

Reuniendo las piezas, considera el callback `myfirst_read` existente. Una versión simplificada del Capítulo 24 tenía este aspecto:

```c
static int
myfirst_read(struct cdev *cdev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = cdev->si_drv1;
        int error = 0;

        mtx_lock(&sc->sc_mtx);
        if (uio->uio_resid == 0) {
                device_printf(sc->sc_dev, "read: empty request\n");
                goto out;
        }
        /* copy bytes into user space, update counters ... */
out:
        mtx_unlock(&sc->sc_mtx);
        return (error);
}
```

Ese código tiene un problema latente de avalancha de mensajes. Bajo estrés, un programa en espacio de usuario defectuoso o malicioso puede llamar a `read(fd, buf, 0)` en un bucle cerrado y llenar el buffer de mensajes con líneas "empty request". El evento no es un error del driver; es un patrón de syscall extraño pero legítimo. Registrarlo en absoluto es discutible, pero si el driver lo registra, la línea de registro debe estar limitada en tasa.

Tras la refactorización, la misma ruta tiene este aspecto:

```c
static int
myfirst_read(struct cdev *cdev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = cdev->si_drv1;
        int error = 0;

        mtx_lock(&sc->sc_mtx);
        if (uio->uio_resid == 0) {
                DLOG_RL(sc, &sc->sc_rl_short, MYF_RL_DEFAULT_PPS,
                    "read: empty request\n");
                goto out;
        }
        /* copy bytes into user space, update counters ... */
out:
        mtx_unlock(&sc->sc_mtx);
        return (error);
}
```

El cambio son tres líneas. El efecto es que el registro ya no puede inundarse, y la primera ocurrencia en cada segundo sigue produciendo una línea para que el operador la vea. El softc gana un campo `struct myfirst_ratelimit sc_rl_short`; ningún otro código cambia.

Aplica la misma transformación a cada `device_printf` en cada ruta de error o advertencia, reserva una `struct myfirst_ratelimit` por categoría, y el driver estará limitado en tasa. El diff es mecánico; la disciplina es lo que hace posible el diff.

### Errores comunes y cómo evitarlos

Tres errores son habituales al aplicar la limitación de tasa por primera vez. Cada uno es fácil de detectar cuando sabes qué buscar.

El primer error es **compartir una única estructura de limitación de tasa entre puntos de llamada no relacionados**. Si el punto A y el punto B utilizan ambos `sc->sc_rl_generic`, una ráfaga en el punto A silencia al punto B durante el resto del segundo, y el operador solo ve una categoría. La disciplina correcta es una estructura de limitación de tasa por categoría lógica. Dos o tres categorías por driver es lo habitual; diez es señal de que el driver está registrando demasiados tipos de eventos.

El segundo error es **aplicar limitación de tasa a los mensajes de ciclo de vida**. El driver se carga e imprime un banner. Ese banner se dispara una sola vez. Envolverlo en `ppsratecheck` añade ruido sin ningún beneficio, y en una segunda carga durante un límite de segundo desafortunado puede omitir el banner por completo. Reserva la limitación de tasa para los mensajes que realmente pueden repetirse.

El tercer error es **olvidar que el contador de limitación de tasa vive en el softc**. Un punto de llamada que se dispara antes de que attach complete, o después de que detach comience, puede acceder a un softc cuya estructura de limitación de tasa todavía no está inicializada (o ha sido puesta a cero por `bzero(sc, sizeof(*sc))`). Tanto `struct timeval` como `int` son tipos de valor; una estructura inicializada a cero es válida para la primera llamada, porque `ppsratecheck` gestiona correctamente el caso `lasttime == 0`. Sin embargo, una asignación en el heap sin inicializar que posteriormente contenga basura no es válida, porque el campo `lasttime` puede contener un valor grande que haga pensar al código que el último evento ocurrió en el futuro lejano, y todas las llamadas posteriores devuelven 0 hasta que el reloj del kernel supere ese instante futuro, lo que puede no ocurrir nunca. La solución es garantizar que el softc esté inicializado a cero, lo que en `myfirst` ya es así (newbus asigna el softc con `MALLOC(... M_ZERO)`). Un driver que asigne su propio estado con `M_NOWAIT` sin `M_ZERO` debe llamar a `bzero` de forma explícita.

### Cuándo no aplicar limitación de tasa

La limitación de tasa es una disciplina para rutas que pueden dispararse con frecuencia. Algunas rutas no pueden hacerlo. Un fallo de `KASSERT` provoca el pánico del kernel, por lo que aplicar limitación de tasa al mensaje previo al pánico es esfuerzo malgastado. Un error que aborta la carga de un módulo termina la carga, así que solo puede aparecer una copia del mensaje. Un `device_printf` en el momento del attach se dispara como mucho una vez por instancia. Para todos estos casos, el `device_printf` directo es correcto y el envoltorio extra es ruido innecesario.

Una regla práctica útil: si el punto de llamada se ejecuta después de que `attach` haya completado y antes de que `detach` se haya ejecutado, y si el evento puede ser provocado por algo externo al driver (un programa en espacio de usuario que se comporta mal, un dispositivo inestable, un kernel bajo presión), aplica limitación de tasa. En caso contrario, no lo hagas.

### Qué contiene ahora el driver `myfirst`

Tras esta sección, el árbol de trabajo del driver `myfirst` cuenta con dos nuevos archivos:

```text
myfirst_log.h   - the DLOG_RL macro and struct myfirst_ratelimit definition
myfirst_log.c   - any non-trivial rate-check helpers (empty for now)
```

La cabecera `myfirst.h` sigue conteniendo el softc. El softc gana dos o tres campos `struct myfirst_ratelimit`, nombrados según las categorías de puntos de llamada que los usan. Las rutas `read`, `write` e `ioctl` reemplazan sus llamadas directas a `device_printf` en los puntos de error por `DLOG_RL`. Las rutas attach, detach, open y close conservan sus llamadas directas a `device_printf`, porque esos son mensajes de ciclo de vida y no se repiten.

El `Makefile` gana una línea:

```makefile
SRCS= myfirst.c myfirst_debug.c myfirst_ioctl.c myfirst_sysctl.c \
      myfirst_log.c
```

El módulo compila, se carga y se comporta exactamente igual que antes en el caso habitual. Bajo estrés, el driver ya no inunda el buffer de mensajes. Esa es la contribución completa de la Sección 1.

### Cerrando la sección 1

Un driver que registra mensajes sin disciplina es un driver que falla con elegancia en el laboratorio y falla ruidosamente en producción. Las primitivas de comprobación de tasa de FreeBSD, `ratecheck(9)` y `ppsratecheck(9)`, son lo bastante pequeñas como para entenderlas en una hora y lo bastante eficaces como para amortizar su coste durante toda la vida del driver. Combinadas con la maquinaria de máscara de depuración del Capítulo 23, ofrecen al driver `myfirst` una historia de registro limpia y tripartita: los mensajes de ciclo de vida pasan por `device_printf` directamente, los mensajes de error y advertencia pasan por `DLOG_RL`, y los mensajes de depuración pasan por `DPRINTF` bajo la máscara.

En la siguiente sección, pasamos de lo que el driver dice a lo que devuelve. Una línea de registro es para el operador; un errno es para el llamador. Un driver que dice lo correcto al operador pero lo incorrecto al llamador sigue siendo un driver roto.

## Sección 2: Notificación de errores y convenciones de retorno

La segunda disciplina que enseña este capítulo es la disciplina de devolver el errno correcto. Un errno es un número pequeño. El conjunto de posibles errnos está definido en `/usr/src/sys/sys/errno.h`, y en el momento de escribir estas líneas FreeBSD define menos de un centenar de ellos. Un driver descuidado con los errnos que devuelve parece correcto en el momento, porque el llamador generalmente solo comprueba si el valor de retorno es distinto de cero, y cualquier valor distinto de cero supera esa comprobación. Parece mucho menos correcto unos meses después, cuando el primer programa auxiliar en espacio de usuario intenta distinguir *por qué* falló una llamada, y las elecciones de errno del driver resultan ser inconsistentes. Esta sección enseña el vocabulario reducido de errnos que un driver utiliza habitualmente, muestra cómo elegir entre ellos y recorre una auditoría de las rutas existentes del driver `myfirst`.

### Por qué importa la disciplina con errno

El errno que devuelve un driver es un contrato con cada llamador. Los programas en espacio de usuario utilizan los errnos a través de `strerror(3)` y mediante comparación directa (`if (errno == EBUSY)`). El código del kernel que invoca los callbacks del driver utiliza el valor de retorno para decidir qué hacer a continuación: un `d_open` que devuelve `EBUSY` provoca que el kernel falle la syscall `open(2)` con `EBUSY`; un `d_ioctl` que devuelve `ENOIOCTL` provoca que el kernel pase a la capa ioctl genérica; un `device_attach` que devuelve un valor distinto de cero provoca que Newbus deshaga el attach y abandone el dispositivo. Cada uno de esos consumidores espera que un valor específico signifique algo específico. Un driver que devuelve `EINVAL` donde se esperaba `ENXIO` no falla necesariamente; a menudo simplemente lleva a error, y ese errno engañoso aparece como un diagnóstico desconcertante en algún lugar que el autor del driver nunca verá.

La disciplina es barata. El coste de ignorarla se acumula con el tiempo. Un driver que eligió bien sus errnos desde el principio es un driver que produce páginas de manual precisas, mensajes de error precisos en los programas auxiliares de espacio de usuario e informes de errores precisos. Un driver descuidado con los errnos empieza a producir salidas de `strerror` ligeramente incorrectas en muchos lugares, y el lado de espacio de usuario del ecosistema hereda ese descuido.

### El vocabulario básico

La lista completa de errnos es larga. El subconjunto que utiliza un driver de caracteres típico es corto. La siguiente tabla recoge el vocabulario que se necesita con más frecuencia en un driver de FreeBSD, agrupado según cuándo es apropiado cada uno.

| Errno | Valor numérico | Cuándo devolverlo |
|-------|---------------|-------------------|
| `0` | 0 | Éxito. El único retorno sin error. |
| `EPERM` | 1 | El llamador carece de privilegio para la operación solicitada, aunque la llamada en sí es correcta. Ejemplo: un usuario sin privilegios de root solicita un ioctl con privilegios. |
| `ENOENT` | 2 | El objeto solicitado no existe. Ejemplo: una búsqueda por nombre o ID que no encuentra nada. |
| `EIO` | 5 | Error de I/O genérico del hardware. Úsalo cuando el hardware ha devuelto un fallo y no hay un errno más específico. |
| `ENXIO` | 6 | El dispositivo ha desaparecido, se ha desconectado o no es alcanzable de otro modo. Ejemplo: un ioctl sobre un descriptor de archivo cuyo dispositivo subyacente fue eliminado. Diferente de `ENOENT`: el objeto existía y ya no existe. |
| `EBADF` | 9 | El descriptor de archivo no está abierto correctamente para la operación. Ejemplo: una llamada `MYFIRSTIOC_SETMSG` realizada sobre un descriptor de archivo abierto solo para lectura. |
| `ENOMEM` | 12 | La asignación de memoria ha fallado. Úsalo para fallos de `malloc(M_NOWAIT)` y situaciones similares. |
| `EACCES` | 13 | El llamador carece de permiso a nivel del sistema de archivos. Diferente de `EPERM`: `EACCES` trata sobre permisos de archivo, `EPERM` trata sobre privilegio. |
| `EFAULT` | 14 | Un puntero de usuario no es válido. Lo devuelven `copyin` o `copyout` cuando fallan. Los drivers deben propagar los fallos de `copyin`/`copyout` sin modificarlos. |
| `EBUSY` | 16 | El recurso está en uso. Úsalo en un `detach` que no puede continuar porque un cliente aún mantiene el dispositivo abierto, o en intentos de adquisición tipo mutex que no pueden esperar. |
| `EINVAL` | 22 | Los argumentos se reconocen pero son inválidos. Úsalo cuando el driver entendió la solicitud pero las entradas están mal formadas. |
| `EAGAIN` | 35 | Inténtalo de nuevo más tarde. Se devuelve desde I/O no bloqueante cuando la operación bloquearía, o desde fallos de asignación que pueden tener éxito en un reintento. |
| `EOPNOTSUPP` | 45 | La operación no está soportada por este driver. Úsalo cuando la llamada es correcta pero el driver no tiene código para manejarla. |
| `ETIMEDOUT` | 60 | Una espera ha excedido el tiempo límite. Úsalo para comandos de hardware que no completaron dentro del presupuesto de tiempo del driver. |
| `ENOIOCTL` | -3 | El comando ioctl es desconocido para este driver. **Usa esto en el caso por defecto de `d_ioctl`; el kernel lo traduce a `ENOTTY` para el espacio de usuario.** |
| `ENOSPC` | 28 | No queda espacio, ya sea en el dispositivo, en un buffer o en una tabla interna. |

Tres pares de esta tabla son notoriamente fáciles de confundir: `EPERM` frente a `EACCES`, `ENOENT` frente a `ENXIO`, e `EINVAL` frente a `EOPNOTSUPP`. Vale la pena examinarlos uno por uno.

`EPERM` frente a `EACCES`. `EPERM` tiene que ver con el privilegio: el proceso que llama no tiene suficientes privilegios para realizar la operación. `EACCES` tiene que ver con el permiso: la ACL del sistema de archivos o los bits de modo prohíben el acceso. Un usuario sin privilegios de root que intenta escribir en `/dev/myfirst0` cuando el modo del nodo es `0600 root:wheel` recibe `EACCES` del kernel antes de que el driver sea consultado. Un usuario root que intenta invocar un ioctl privilegiado que el driver rechaza porque el proceso que llama no está en una jail determinada recibe `EPERM` del driver. La distinción importa porque el remedio del administrador es diferente en cada caso: `EACCES` le indica que ajuste los permisos del dispositivo, mientras que `EPERM` le indica que ajuste los privilegios del proceso que llama.

`ENOENT` frente a `ENXIO`. `ENOENT` significa *ese objeto no existe*. `ENXIO` significa *el objeto ha desaparecido, o el dispositivo no es alcanzable*. Al realizar una búsqueda en la tabla interna de un driver, `ENOENT` es la respuesta correcta cuando la clave solicitada no está presente. En una operación sobre un dispositivo que ha sido desconectado o que ha señalado una condición de extracción inesperada, `ENXIO` es la respuesta correcta. La distinción importa porque las herramientas de operación las tratan de forma diferente: `ENOENT` sugiere que el proceso que llama proporcionó la clave incorrecta; `ENXIO` sugiere que el dispositivo necesita ser reconectado.

`EINVAL` frente a `EOPNOTSUPP`. `EINVAL` significa *entendí lo que pediste, pero los argumentos son incorrectos*. `EOPNOTSUPP` significa *no soporto lo que pediste*. Una llamada a `MYFIRSTIOC_SETMSG` con un buffer demasiado largo produce `EINVAL`. Una llamada a `MYFIRSTIOC_SETMODE` para un modo que el driver nunca implementa produce `EOPNOTSUPP`. La distinción importa porque `EOPNOTSUPP` indica al proceso que llama que utilice un enfoque diferente, mientras que `EINVAL` le indica que corrija los argumentos e intente de nuevo.

Una cuarta confusión merece un párrafo propio: `ENOIOCTL` frente a `ENOTTY`. `ENOIOCTL` es un valor negativo (`-3`) definido para el camino de código ioctl dentro del kernel. El caso por defecto del `d_ioctl` de un driver devuelve `ENOIOCTL` para indicarle al kernel: "No reconozco este comando; continúa hacia la capa genérica". La capa genérica gestiona `FIONBIO`, `FIOASYNC`, `FIOGETOWN`, `FIOSETOWN` y otros ioctls comunes a varios dispositivos. Si la capa genérica tampoco reconoce el comando, traduce `ENOIOCTL` a `ENOTTY` (valor positivo 25) para entregarlo al espacio de usuario. El error habitual es devolver `EINVAL` desde el caso por defecto de un switch `d_ioctl`, lo que suprime por completo el mecanismo de respaldo genérico. El driver del Capítulo 24 ya devuelve `ENOIOCTL` correctamente; la auditoría del Capítulo 25 lo confirma y comprueba todos los demás errno del driver en busca de problemas similares.

### La auditoría del dispatcher de ioctl

El primer pase de auditoría se centra en `myfirst_ioctl.c`. Cada caso de la sentencia switch produce como máximo un retorno distinto de cero. La auditoría examina cada uno y pregunta si el errno devuelto es correcto.

Caso `MYFIRSTIOC_GETVER`: devuelve 0 en caso de éxito, nunca falla. No hay nada que auditar.

Caso `MYFIRSTIOC_GETMSG`: devuelve 0 en caso de éxito. El código actual no rechaza en base a `fflag` porque el mensaje es público. Eso es una decisión de diseño, no un error. Si el driver quisiera restringir `GETMSG` a los lectores (es decir, exigir `FREAD`), devolvería `EBADF` en la comprobación de fflag, de forma coherente con los casos `SETMSG` y `RESET`.

Caso `MYFIRSTIOC_SETMSG`: devuelve `EBADF` cuando el descriptor de archivo carece de `FWRITE`, lo cual es correcto. La segunda pregunta de auditoría es qué ocurre cuando la entrada no está terminada en NUL: `strlcpy` en el kernel lo tolera (copia hasta `MYFIRST_MSG_MAX - 1` caracteres y termina la cadena), de modo que el driver no necesita comprobarlo. La tercera pregunta es si la longitud debería validarse antes de copiar. El `copyin` automático del kernel ya impuso la longitud fija codificada en la definición del ioctl, así que no hay ningún buffer de espacio de usuario que validar; el valor está en `data` y ya ha sido copiado.

Caso `MYFIRSTIOC_RESET`: devuelve `EBADF` cuando el descriptor de archivo carece de `FWRITE`. La auditoría del capítulo 25 plantea una segunda pregunta: ¿debería el reset requerir privilegios? Un driver que permite a cualquier escritor llamar a `RESET` y poner las estadísticas a cero expone una superficie menor de denegación de servicio. La solución sencilla es comprobar `priv_check(td, PRIV_DRIVER)` antes de ejecutar el reset:

```c
case MYFIRSTIOC_RESET:
        if ((fflag & FWRITE) == 0) {
                error = EBADF;
                break;
        }
        error = priv_check(td, PRIV_DRIVER);
        if (error != 0)
                break;
        /* ... existing reset body ... */
        break;
```

Si `priv_check` falla, el errno es `EPERM` (el kernel devuelve `EPERM` en lugar de `EACCES` porque la comprobación es sobre privilegios, no sobre permisos del sistema de archivos). El programa `myfirstctl` ejecutado como root obtiene 0; un programa sin privilegios ejecutado como el usuario `_myfirst` obtiene `EPERM`.

Caso por defecto: devuelve `ENOIOCTL`, lo cual es correcto. Déjalo tal como está.

### La auditoría del camino de lectura y escritura

El segundo pase de auditoría se centra en los callbacks de lectura y escritura.

Para `myfirst_read`, el código actual devuelve 0 en caso de éxito, `EFAULT` cuando `uiomove` falla, y 0 cuando `uio_resid == 0`. El retorno de 0 ante una petición vacía es el comportamiento estándar de UNIX (un `read` de cero bytes está permitido y devuelve 0 bytes) y es correcto. No es necesario cambiar ningún errno.

Para `myfirst_write`, de forma análoga: 0 en caso de éxito, `EFAULT` si falla `uiomove`, 0 en una escritura de cero bytes. Correcto.

Ninguno de los dos callbacks necesita `EIO`: en este punto el driver no realiza I/O de hardware, por lo que no hay ningún fallo de hardware que propagar. Una versión futura del driver que controle hardware real devolvería `EIO` desde el callback de lectura o escritura cuando el hardware indicara un fallo a nivel de transporte. Añadir ese retorno ahora sería prematuro; es el tipo de cosa que el trabajo con almacenamiento del capítulo 28 tratará de forma concreta.

### La auditoría del camino de apertura y cierre

El callback de apertura devuelve 0 de forma incondicional. La pregunta de auditoría es si alguna vez debería fallar. Convencionalmente son posibles tres modos de fallo: el dispositivo tiene apertura exclusiva y ya tiene un usuario (`EBUSY`), el dispositivo está apagado y no puede aceptarse la apertura en este momento (`ENXIO`), o el driver está siendo desconectado justo ahora (`ENXIO`). El driver `myfirst` sencillo no impone apertura exclusiva y siempre acepta aperturas excepto durante el detach. Durante el detach, el kernel destruye el cdev antes de que el detach retorne, de modo que cualquier apertura que llegue después de que comience `destroy_dev` es rechazada por el propio kernel antes de que se llame al `d_open` del driver. Por tanto, el driver `myfirst` no necesita lógica explícita de `ENXIO`. Dejar el callback de apertura devolviendo 0 es correcto.

El callback de cierre devuelve 0 de forma incondicional. Eso es correcto. La única razón concebible por la que `d_close` podría devolver un valor distinto de cero es una operación de hardware durante el cierre que hubiera fallado; como el driver `myfirst` no realiza ninguna operación de ese tipo, 0 es el retorno correcto.

### La auditoría de los caminos de attach y detach

Attach y detach son los callbacks que invoca Newbus. Sus valores de retorno indican a Newbus si debe deshacer la operación o continuar.

Un retorno distinto de cero de `myfirst_attach` significa "el attach ha fallado; por favor, deshaz los cambios." Cada camino de error en attach debe devolver un errno positivo. El código actual devuelve el valor de `error` procedente de `make_dev_s`, que es positivo en caso de fallo; eso es correcto. Las incorporaciones de la sección 5 de este capítulo introducirán más caminos de error con gotos etiquetados; cada uno de ellos usará el errno correcto para el paso que falló (`ENOMEM` para un fallo de asignación de memoria, `ENXIO` para un fallo de asignación de recursos, etc.).

Un retorno distinto de cero de `myfirst_detach` significa "no es posible desconectar ahora mismo; por favor, deja el dispositivo conectado." El código actual devuelve `EBUSY` cuando `sc_open_count > 0`, lo cual es correcto. Newbus traduce `EBUSY` procedente del detach en un fallo de `devctl detach` con el mismo errno, que es el comportamiento correcto para el usuario.

El handler de eventos del módulo (`myfirst_modevent`) devuelve un valor distinto de cero para rechazar el evento. Un `MOD_UNLOAD` que no puede proceder porque alguna instancia del dispositivo sigue en uso devuelve `EBUSY`. Un `MOD_LOAD` que no puede proceder por un fallo en una comprobación de cordura devuelve el errno apropiado (`ENOMEM`, `EINVAL`, etc.). El código actual es correcto.

### La auditoría del handler de sysctl

Los handlers de sysctl tienen sus propias convenciones de errno. El driver del capítulo 24 tiene un handler personalizado, `myfirst_sysctl_message_len`. Su cuerpo es:

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

El handler lee su entrada con `sysctl_handle_int`, que devuelve 0 en caso de éxito y un errno positivo en caso de fallo. El handler reenvía ese errno sin modificarlo, lo cual es correcto. No es necesario ningún cambio de auditoría.

Un handler de sysctl que escribe (en lugar de solo leer) debería comprobar `req->newptr` para distinguir una lectura de una escritura, y debería devolver `EPERM` si se intenta escribir en un OID de solo lectura. El OID `debug.mask` existente está declarado con `CTLFLAG_RW`, por lo que el kernel permite las escrituras automáticamente; el handler no necesita una comprobación de privilegios porque el OID ya está restringido a root por los permisos del MIB de sysctl. El driver del capítulo 25 no añade más handlers de sysctl personalizados en esta etapa.

### Mensajes en el camino de error

Devolver el errno correcto es la mitad del contrato. Emitir el mensaje de log correcto es la otra mitad. Esta disciplina combina el logging con límite de tasa de la sección 1 con el vocabulario de errnos de la sección 2. Un camino de advertencia tiene este aspecto:

```c
if (input_too_large) {
        DLOG_RL(sc, &sc->sc_rl_inval, MYF_RL_DEFAULT_PPS,
            "ioctl: SETMSG buffer too large (%zu > %d)\n",
            length, MYFIRST_MSG_MAX);
        error = EINVAL;
        break;
}
```

Tres propiedades hacen de este un buen camino de error. En primer lugar, el mensaje de log nombra la llamada ("ioctl: SETMSG"), la razón ("buffer too large") y los valores numéricos implicados. En segundo lugar, el errno devuelto es `EINVAL`, que es el valor correcto para "te he entendido, pero el argumento es incorrecto." En tercer lugar, todo el camino tiene límite de tasa, de modo que un programa de espacio de usuario con errores que llame al ioctl en un bucle cerrado no puede inundar el buffer de mensajes.

Un camino de error incorrecto tiene este aspecto:

```c
if (input_too_large) {
        device_printf(sc->sc_dev, "ioctl failed\n");
        return (-1);
}
```

Tres propiedades hacen de este un mal camino de error. El mensaje de log no aporta información: "ioctl failed" no dice nada que el llamador no supiera ya. El valor de retorno es `-1`, que no es un errno de kernel válido. Y la línea de log no tiene límite de tasa, de modo que un llamador con mal comportamiento puede llenar el buffer de mensajes de ruido.

El camino correcto ocupa nueve líneas y el incorrecto ocupa tres, lo cual es un buen intercambio. Una línea de log de error solo se imprime cuando algo va mal; dedicar unos segundos extra a hacerla informativa cuando se imprime merece la pena.

### Convenciones del handler de eventos de módulo

El handler de eventos del módulo tiene su propia convención de errno. La firma del handler es:

```c
static int
myfirst_modevent(module_t mod, int what, void *arg)
{
        switch (what) {
        case MOD_LOAD:
                /* Driver-wide init. */
                return (0);
        case MOD_UNLOAD:
                /* Driver-wide teardown. */
                return (0);
        case MOD_QUIESCE:
                /* Pause and prepare for unload. */
                return (0);
        case MOD_SHUTDOWN:
                /* System shutting down. */
                return (0);
        default:
                return (EOPNOTSUPP);
        }
}
```

Cada caso devuelve 0 en caso de éxito, o un errno positivo para rechazar el evento. Los errnos específicos por caso:

- `MOD_LOAD` devuelve `ENOMEM` si una asignación global ha fallado, `ENXIO` si el driver no es compatible con el kernel actual, o `EINVAL` si el valor de un tunable está fuera de rango.
- `MOD_UNLOAD` devuelve `EBUSY` si el driver no puede descargarse ahora porque alguna instancia sigue en uso. El kernel respeta esto y deja el módulo cargado.
- `MOD_QUIESCE` devuelve `EBUSY` si el driver no puede pausarse. Un driver que no admite quiescence simplemente devuelve 0 en este caso, porque quiescence es una característica opcional y devolver éxito significa "estoy en pausa" en el sentido trivial de no tener trabajo en vuelo.
- `MOD_SHUTDOWN` raramente falla; devuelve 0 a menos que el driver tenga una razón específica para oponerse al apagado. Un driver que quiera vaciar estado persistente usa un `EVENTHANDLER` en `shutdown_pre_sync` en lugar de rechazar `MOD_SHUTDOWN`.
- El caso por defecto devuelve `EOPNOTSUPP` para indicar que el driver no reconoce el tipo de evento. Esto no es un error; es la forma estándar de decir "no implemento este evento."

### Una lista de verificación de errnos

Para cerrar la sección, una lista de verificación que puedes aplicar a cualquier driver que escribas. Cada elemento es una pregunta cuya respuesta debería ser sí.

1. Todos los retornos distintos de cero en un callback son un errno positivo de `errno.h`, excepto en `d_ioctl`, que puede devolver `ENOIOCTL` (un valor negativo).
2. Los fallos de `copyin` y `copyout` propagan su errno sin modificarlo (típicamente `EFAULT`).
3. El caso por defecto de `d_ioctl` devuelve `ENOIOCTL`, no `EINVAL`.
4. `d_detach` devuelve `EBUSY` si el dispositivo sigue en uso, no `ENXIO` ni ningún otro valor.
5. `d_open` devuelve `ENXIO` si el hardware subyacente ha desaparecido o si el driver está siendo desconectado, no `EIO`.
6. `d_write` devuelve `EBADF` si el descriptor de archivo carece de `FWRITE`, no `EPERM`.
7. Todo camino de error registra un mensaje que nombra la llamada, la razón y los valores relevantes, usando la macro con límite de tasa.
8. Ningún camino de error registra un mensaje *y* devuelve un errno genérico. Si el driver tiene suficiente contexto para registrar la razón específica, también tiene suficiente contexto para devolver un errno específico.
9. El driver distingue de forma coherente entre `EINVAL` (argumentos incorrectos) y `EOPNOTSUPP` (característica ausente).
10. El driver distingue de forma coherente entre `ENOENT` (clave inexistente) y `ENXIO` (dispositivo inalcanzable).

Un driver que supera esta lista de verificación tiene una superficie de errno coherente, y la superficie es lo suficientemente pequeña como para que la página de manual pueda enumerar todos los errnos que el driver devuelve y decir exactamente cuándo ocurre cada uno.

### Cerrando la sección 2

Los errnos son un vocabulario reducido y un contrato. La falta de rigor en cualquiera de los dos se manifiesta como comportamiento inexplicable en el espacio de usuario; la disciplina en ambos se traduce en diagnósticos precisos e informes de errores más breves. Combinado con el logging con límite de tasa de la sección 1, el driver `myfirst` se comunica ahora con cuidado tanto con el operador (a través de las líneas de log) como con el llamador (a través de los errnos).

En la siguiente sección veremos al tercer destinatario al que el driver debe un contrato: el administrador que configura el driver a través de `/boot/loader.conf` y `sysctl`. La configuración es un tercer tipo de contrato, y la disciplina en su manejo es lo que permite que el driver siga siendo útil en distintas cargas de trabajo sin necesidad de recompilarlo.

## Sección 3: Configuración del driver mediante tunables del loader y sysctl

La tercera disciplina es la disciplina de externalizar las decisiones. Todo driver tiene valores que alguien podría querer cambiar de forma razonable sin recompilar el módulo: un tiempo de espera, un contador de reintentos, el tamaño de un buffer interno, un nivel de verbosidad, un interruptor de característica. Un driver que incrusta esos valores en el código fuente obliga a que cada cambio pase por un ciclo completo de compilación, instalación y reinicio del sistema. Un driver que los expone como tunables del loader y sysctls permite a un operador ajustar el comportamiento en el arranque o en tiempo de ejecución con una sola edición o un solo comando. El coste de ofrecer esos controles es pequeño; el coste de no ofrecerlos lo paga el operador.

Esta sección enseña los dos mecanismos de FreeBSD para externalizar la configuración: los loader tunables (leídos desde `/boot/loader.conf` y aplicados antes de que el kernel llegue a `attach`) y los sysctls (leídos y escritos en tiempo de ejecución mediante `sysctl(8)`). Se explica cómo cooperan a través del flag `CTLFLAG_TUN`, se muestra cómo elegir entre tunables por driver y por instancia, se recorre la familia `TUNABLE_*_FETCH` y se cierra con un laboratorio breve en el que el driver `myfirst` gana tres nuevos tunables y el lector arranca la VM con cada uno de ellos.

### La diferencia entre un tunable y un sysctl

Un tunable y un sysctl se parecen desde la perspectiva de un operador. Ambos son cadenas en un espacio de nombres como `hw.myfirst.debug_mask_default` o `dev.myfirst.0.debug.mask`. Ambos aceptan valores que el operador establece. Ambos acaban en memoria del kernel. La diferencia está en cuándo y cómo actúan.

Un **tunable** es una variable que se establece en el entorno del bootloader. El bootloader (`loader(8)`) lee `/boot/loader.conf`, recoge sus pares `clave=valor` en un entorno y entrega ese entorno al kernel cuando este arranca. El kernel expone ese entorno a través de la familia `getenv(9)` y de los macros `TUNABLE_*_FETCH`. Los tunables se leen durante el boot, normalmente antes de que el driver correspondiente se enlace. No se pueden cambiar en tiempo de ejecución (modificar `/boot/loader.conf` requiere un reinicio para que el cambio surta efecto). Son adecuados para valores que deben conocerse antes de que se ejecute `attach`: el tamaño de una tabla asignada estáticamente, un indicador de característica que controla qué rutas de código se compilan en la ruta de attach, o el valor inicial de una máscara de depuración.

Un **sysctl** es una variable en el árbol de configuración jerárquico del kernel, accesible en tiempo de ejecución a través de la syscall `sysctl(2)` y de la herramienta `sysctl(8)`. Los sysctls pueden ser de solo lectura (`CTLFLAG_RD`), de lectura y escritura (`CTLFLAG_RW`), o de solo lectura pero escribibles por root (con diversas combinaciones de flags). Son adecuados para valores que tiene sentido modificar después de que el driver se haya enlazado: un nivel de verbosidad, una tasa de limitación, un comando de reinicio de contador o un indicador de estado modificable.

La característica útil es que ambos mecanismos pueden compartir una misma variable. Un sysctl declarado con `CTLFLAG_TUN` indica al kernel que lea un tunable del mismo nombre durante el boot y use su valor como valor inicial del sysctl. El operador puede luego ajustar el sysctl en tiempo de ejecución, y el tunable persiste entre reinicios como valor por defecto. El driver `myfirst` ya usa este patrón para su máscara de depuración: `debug.mask` es un sysctl `CTLFLAG_RW | CTLFLAG_TUN`, y `hw.myfirst.debug_mask_default` es el tunable correspondiente en `/boot/loader.conf`. La sección 3 generaliza ese patrón a todos los ajustes de configuración que el driver quiera exponer.

### La familia `TUNABLE_*_FETCH`

FreeBSD proporciona una familia de macros para leer tunables del entorno del bootloader. Cada macro lee el tunable indicado, lo analiza y lo convierte al tipo C adecuado, y almacena el resultado. Si el tunable no está definido, la variable conserva su valor actual; por tanto, el código que llama al macro debe inicializar la variable con el valor por defecto correcto antes de invocar el macro de lectura.

Los macros, declarados en `/usr/src/sys/sys/kernel.h`:

```c
TUNABLE_INT_FETCH(path, pval)        /* int */
TUNABLE_LONG_FETCH(path, pval)       /* long */
TUNABLE_ULONG_FETCH(path, pval)      /* unsigned long */
TUNABLE_INT64_FETCH(path, pval)      /* int64_t */
TUNABLE_UINT64_FETCH(path, pval)     /* uint64_t */
TUNABLE_BOOL_FETCH(path, pval)       /* bool */
TUNABLE_STR_FETCH(path, pval, size)  /* char buffer of given size */
```

Cada uno se expande en la llamada `getenv_*` correspondiente. Para `TUNABLE_INT_FETCH`, por ejemplo, la expansión es `getenv_int(path, pval)`, que lee el entorno del bootloader y analiza el valor como un entero.

La ruta es una cadena, con la convención `hw.<driver>.<knob>` para tunables de driver y `hw.<driver>.<unit>.<knob>` para tunables de instancia. El prefijo `hw.` es una convención para tunables relacionados con hardware; existen otros prefijos (`kern.`, `net.`) para diferentes subsistemas, pero son menos frecuentes en el código de drivers.

Un ejemplo práctico del driver `myfirst` muestra el patrón:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        int error;

        /* Initialise defaults. */
        sc->sc_debug = 0;
        sc->sc_timeout_sec = 30;
        sc->sc_max_retries = 3;

        /* Read tunables.  The variables keep their default values if
         * the tunables are not set. */
        TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default", &sc->sc_debug);
        TUNABLE_INT_FETCH("hw.myfirst.timeout_sec", &sc->sc_timeout_sec);
        TUNABLE_INT_FETCH("hw.myfirst.max_retries", &sc->sc_max_retries);

        /* ... rest of attach ... */
}
```

El operador establece los tunables en `/boot/loader.conf`:

```ini
hw.myfirst.debug_mask_default="0xff"
hw.myfirst.timeout_sec="15"
hw.myfirst.max_retries="5"
```

Tras el reinicio, cada instancia de `myfirst` se enlaza con `sc_debug=0xff`, `sc_timeout_sec=15` y `sc_max_retries=5`. No fue necesario recompilar; los valores viven fuera del código fuente del driver.

### Tunables por driver frente a tunables por instancia

Un driver que puede enlazarse más de una vez necesita tomar una decisión: ¿deben sus tunables aplicarse a todas las instancias, o debe cada instancia tener los suyos propios?

La forma por driver usa una ruta del tipo `hw.myfirst.debug_mask_default`. Cada instancia de `myfirst` lee esta única variable durante el attach, de modo que todas las instancias arrancan con el mismo valor por defecto. Es la forma más sencilla y es correcta cuando el tunable tiene el mismo significado en todas las instancias.

La forma por instancia usa una ruta del tipo `hw.myfirst.0.debug_mask_default`, donde `0` es el número de unidad. Cada instancia lee su propia variable, por lo que la instancia 0 y la instancia 1 pueden tener valores por defecto distintos. Es la forma adecuada cuando el hardware detrás de cada instancia puede necesitar razonablemente una configuración diferente, por ejemplo dos adaptadores PCI en el mismo sistema con cargas de trabajo distintas.

La decisión es una elección de diseño, no una cuestión de corrección. La mayoría de los drivers usan la forma por driver para la mayor parte de los tunables, reservando las formas por instancia para los casos en que la configuración individual realmente importa. Para `myfirst`, un pseudodispositivo ficticio, la forma por driver es la opción correcta para todos los tunables. El driver del capítulo 25 añade por tanto tres tunables por driver (`timeout_sec`, `max_retries`, `log_ratelimit_pps`) y mantiene el `debug_mask_default` por driver ya existente.

Un patrón que combina ambas formas, si el driver lo necesita, consiste en leer primero el tunable por driver como base y luego el tunable por instancia como reemplazo:

```c
int defval = 30;

TUNABLE_INT_FETCH("hw.myfirst.timeout_sec", &defval);
sc->sc_timeout_sec = defval;
TUNABLE_INT_FETCH_UNIT("hw.myfirst", unit, "timeout_sec",
    &sc->sc_timeout_sec);
```

FreeBSD no incluye un macro `TUNABLE_INT_FETCH_UNIT` de serie; un driver que lo necesite tiene que componer la ruta con `snprintf` y luego llamar directamente a `getenv_int`. El esfuerzo es pequeño, pero la necesidad es infrecuente, por lo que `myfirst` no llega a ese punto.

### El flag `CTLFLAG_TUN`

La segunda parte de la historia de externalización es que un tunable por sí solo solo se lee durante el boot. Para que ese mismo valor sea ajustable en tiempo de ejecución, el driver declara el sysctl correspondiente con `CTLFLAG_TUN`:

```c
SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "debug.mask",
    CTLFLAG_RW | CTLFLAG_TUN,
    &sc->sc_debug, 0,
    "Bitmask of enabled debug classes");
```

`CTLFLAG_TUN` indica al kernel que el valor inicial de este sysctl debe tomarse de la variable del entorno del bootloader con el mismo nombre, usando el nombre del OID como clave. La correspondencia es textual y automática; el driver no necesita llamar a `TUNABLE_INT_FETCH` por separado.

Hay una regla sutil sobre cuándo se respeta `CTLFLAG_TUN`. El flag se aplica al valor *inicial* del OID, que se lee del entorno cuando el sysctl se crea. Si el driver llama a `TUNABLE_INT_FETCH` explícitamente antes de crear el sysctl, esa lectura explícita tiene prioridad y `CTLFLAG_TUN` resulta efectivamente redundante. Si el driver no llama a `TUNABLE_INT_FETCH` y se apoya únicamente en `CTLFLAG_TUN`, el valor inicial del sysctl proviene del entorno de forma automática.

En la práctica, el driver `myfirst` usa ambos mecanismos a la vez por claridad. El `TUNABLE_INT_FETCH` explícito en attach hace visible la intención del driver en el código fuente; el `CTLFLAG_TUN` en el sysctl ofrece al operador una pista clara en la documentación del OID de que este respeta un tunable del bootloader. Cualquiera de los dos mecanismos por sí solo funcionaría; usar ambos es una pequeña duplicación que compensa con creces en legibilidad.

### Declarar un tunable como sysctl estático

Para los sysctls de alcance global en el driver que no pertenecen a una instancia concreta, FreeBSD ofrece macros en tiempo de compilación que vinculan un sysctl a una variable estática y leen su valor por defecto del entorno en una sola declaración. La forma canónica:

```c
SYSCTL_NODE(_hw, OID_AUTO, myfirst, CTLFLAG_RW, NULL,
    "myfirst pseudo-driver");

static int myfirst_verbose = 0;
SYSCTL_INT(_hw_myfirst, OID_AUTO, verbose,
    CTLFLAG_RWTUN, &myfirst_verbose, 0,
    "Enable verbose driver logging");
```

`SYSCTL_NODE` declara un nuevo nodo padre `hw.myfirst`. `SYSCTL_INT` declara un OID entero `hw.myfirst.verbose` con `CTLFLAG_RWTUN` (que combina `CTLFLAG_RW` y `CTLFLAG_TUN`). La variable `myfirst_verbose` es el nivel de verbosidad global del driver. El operador establece `hw.myfirst.verbose=1` en `/boot/loader.conf` para activar la salida detallada durante el boot, o ejecuta `sysctl hw.myfirst.verbose=1` para cambiarlo en tiempo de ejecución.

La declaración estática es adecuada para el estado global del driver. El estado por instancia (`sc_debug`, contadores) sigue viviendo bajo `dev.myfirst.<unit>.*` y se declara dinámicamente a través de `device_get_sysctl_ctx`.

### Una nota sobre `SYSCTL_INT` frente a `SYSCTL_ADD_INT`

La forma estática `SYSCTL_INT(parent, OID_AUTO, ...)` es una declaración en tiempo de compilación. La forma dinámica `SYSCTL_ADD_INT(ctx, list, OID_AUTO, ...)` es una llamada en tiempo de ejecución. Ambas producen un OID de sysctl. La forma estática es adecuada para sysctls de alcance global cuya existencia no depende de enlazarse con hardware. La forma dinámica es adecuada para sysctls por instancia, creados en attach y destruidos en detach.

Un error frecuente en quienes se inician es usar la forma dinámica para sysctls de alcance global, lo que funciona pero requiere un `sysctl_ctx_list` global que debe inicializarse en `MOD_LOAD` y liberarse en `MOD_UNLOAD`. La forma estática evita todo eso: el sysctl existe desde que el módulo se carga hasta que se descarga, y el kernel gestiona el registro y la cancelación del registro de forma automática.

### Documentar un tunable

Un tunable que el operador desconoce es un tunable que nunca se usa. La disciplina consiste en documentar cada tunable que expone el driver, en tres lugares.

En primer lugar, la declaración del tunable en el código fuente debe incluir una cadena de descripción de una sola línea. Para `SYSCTL_ADD_UINT` y similares, el último argumento es la descripción:

```c
SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "timeout_sec",
    CTLFLAG_RW | CTLFLAG_TUN,
    &sc->sc_timeout_sec, 0,
    "Timeout in seconds for hardware commands (default 30, min 1, max 3600)");
```

La cadena de descripción es lo que `sysctl -d` muestra cuando un operador solicita documentación. Una buena descripción indica la unidad, el valor por defecto y el rango aceptable.

En segundo lugar, el `MAINTENANCE.md` del driver (introducido en la sección 7) debe listar cada tunable con un párrafo por cada uno. El párrafo explica qué hace el tunable, cuándo modificarlo, cuál es su valor por defecto y qué efectos secundarios puede tener al cambiarlo.

En tercer lugar, la página de manual del driver (normalmente `myfirst(4)`) debe listar cada tunable bajo una sección `LOADER TUNABLES` y cada sysctl bajo una sección `SYSCTL VARIABLES`. El driver `myfirst` todavía no tiene página de manual; el capítulo trata esa página como algo que se abordará más adelante. El documento `MAINTENANCE.md` es el que carga con la documentación completa mientras tanto.

### Un ejemplo completo: `hw.myfirst.timeout_sec`

El driver `myfirst` no tiene hardware real en esta etapa, pero el capítulo introduce un ajuste ficticio `timeout_sec` que los capítulos futuros usarán. El flujo de trabajo completo es:

1. En `myfirst.h`, añadir el campo al softc:
   ```c
   struct myfirst_softc {
           /* ... existing fields ... */
           int   sc_timeout_sec;
   };
   ```

2. En `myfirst_bus.c` (el nuevo archivo introducido en la sección 6, que contiene attach y detach), inicializar el valor por defecto y leer el tunable:
   ```c
   sc->sc_timeout_sec = 30;
   TUNABLE_INT_FETCH("hw.myfirst.timeout_sec", &sc->sc_timeout_sec);
   ```

3. En `myfirst_sysctl.c`, exponer el ajuste como sysctl en tiempo de ejecución:
   ```c
   SYSCTL_ADD_INT(ctx, child, OID_AUTO, "timeout_sec",
       CTLFLAG_RW | CTLFLAG_TUN,
       &sc->sc_timeout_sec, 0,
       "Timeout in seconds for hardware commands");
   ```

4. En `MAINTENANCE.md`, documentar el tunable:
   ```
   hw.myfirst.timeout_sec
       Timeout in seconds for hardware commands.  Default 30.
       Acceptable range 1 through 3600.  Values below 1 are
       clamped to 1; values above 3600 are clamped to 3600.
       Adjustable at run time via sysctl dev.myfirst.<unit>.
       timeout_sec.
   ```

5. En el script de regresión, añadir una línea que verifique que el tunable recoge su valor por defecto:
   ```
   [ "$(sysctl -n dev.myfirst.0.timeout_sec)" = "30" ] || fail
   ```

El driver tiene ahora un ajuste de timeout que el operador puede establecer durante el boot a través de `/boot/loader.conf`, puede modificar en tiempo de ejecución con `sysctl`, y puede encontrar documentado en `MAINTENANCE.md`. Cada capítulo futuro que introduzca un nuevo valor configurable seguirá el mismo flujo de trabajo de cinco pasos.

### Comprobación de rangos y validación

Un tunable que el operador puede fijar en cualquier valor es un tunable que puede establecerse fuera de rango, ya sea por accidente (una errata en `/boot/loader.conf`) o por un intento de ajuste equivocado. El driver debe validar el valor que lee y recortarlo o rechazarlo.

Para los tunables leídos durante el boot con `TUNABLE_INT_FETCH`, la validación se realiza en línea:

```c
sc->sc_timeout_sec = 30;
TUNABLE_INT_FETCH("hw.myfirst.timeout_sec", &sc->sc_timeout_sec);
if (sc->sc_timeout_sec < 1 || sc->sc_timeout_sec > 3600) {
        device_printf(dev,
            "tunable hw.myfirst.timeout_sec out of range (%d), "
            "clamping to default 30\n",
            sc->sc_timeout_sec);
        sc->sc_timeout_sec = 30;
}
```

Para los sysctls con soporte de escritura en tiempo de ejecución, la validación se realiza en el handler. Un sysctl `CTLFLAG_RW` simple sobre una variable int acepta cualquier int; para rechazar escrituras fuera de rango, el driver declara un handler personalizado:

```c
static int
myfirst_sysctl_timeout(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int v;
        int error;

        v = sc->sc_timeout_sec;
        error = sysctl_handle_int(oidp, &v, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);
        if (v < 1 || v > 3600)
                return (EINVAL);
        sc->sc_timeout_sec = v;
        return (0);
}
```

El handler lee el valor actual, llama a `sysctl_handle_int` para realizar la E/S real, y aplica el nuevo valor únicamente si está dentro del rango. Una escritura de 0 o de 7200 devuelve `EINVAL` al operador sin modificar el valor del sysctl. Este es el comportamiento correcto: el operador recibe información clara de que la escritura fue rechazada.

El driver `myfirst` en esta etapa no valida sus sysctls enteros porque ninguno de ellos puede salir de rango de forma significativa (la máscara de depuración es una máscara de bits, y cualquier valor de 32 bits es una máscara válida). Los futuros drivers que introduzcan timeouts, contadores de reintentos y tamaños de buffer usarán el patrón de handler personalizado de forma sistemática.

### Cuándo exponer un tunable y cuándo dejarlo interno

Exponer un tunable es un compromiso. Una vez que el operador establece `hw.myfirst.timeout_sec=15` en `/boot/loader.conf`, el driver ha hecho una promesa: el significado de ese parámetro no cambiará en versiones posteriores. Eliminar el tunable rompe los despliegues en producción. Cambiar su interpretación de forma silenciosa es incluso más perjudicial.

La disciplina adecuada consiste en exponer un valor como tunable solo cuando se cumplen las tres condiciones siguientes:

1. El valor tiene un caso de uso operacional. Alguien podría tener motivos razonables para cambiarlo en un despliegue real.
2. El rango de valores razonables es conocido. El driver puede documentarlo en `MAINTENANCE.md`.
3. El coste de mantener ese parámetro durante toda la vida del driver merece la pena por el valor operacional que aporta.

Un driver que se hace esas tres preguntas y responde sí a todas ellas expone un conjunto pequeño y deliberado de tunables. Un driver que expone cada constante interna como un tunable porque "los operadores podrían querer ajustarlo" acaba con una superficie de configuración desmesurada que nadie puede documentar y nadie puede probar en todo su rango.

Para `myfirst`, el conjunto inicial de tunables es deliberadamente pequeño: `debug_mask_default`, `timeout_sec`, `max_retries`, `log_ratelimit_pps`. Cada uno tiene un caso operacional claro, un valor por defecto claro y un rango claro. El driver no pretende exponer cada campo int del softc como un tunable; pretende exponer únicamente los que un operador podría querer modificar en la práctica.

### Una nota de advertencia sobre `CTLFLAG_RWTUN` para cadenas

El macro `TUNABLE_STR_FETCH` lee una cadena del entorno del loader en un buffer de tamaño fijo. El flag de sysctl correspondiente, `CTLFLAG_RWTUN` sobre un `SYSCTL_STRING`, funciona, pero tiene una trampa: el almacenamiento de la cadena debe ser un buffer estático, no un campo `char[]` por instancia en el softc. Un sysctl de cadena que escribe en un campo del softc puede sobrevivir al softc si el framework de sysctl no cancela el registro del OID antes de que el softc sea liberado, lo que conduce a bugs de use-after-free.

El patrón más seguro es exponer las cadenas como de solo lectura y gestionar las escrituras mediante un manejador personalizado que copia el nuevo valor en el softc bajo un lock. El driver `myfirst` sigue este patrón: `dev.myfirst.0.message` se expone con `CTLFLAG_RD` únicamente, y las escrituras pasan por el ioctl `MYFIRSTIOC_SETMSG`. La ruta del ioctl toma el mutex del softc, copia el nuevo valor y libera el lock; no existe ningún problema de vida útil con los OID de sysctl.

Los tunables y sysctls de cadena son lo suficientemente útiles para algunos drivers como para merecer ese cuidado, pero el driver del Capítulo 25 no los necesita. Merece la pena señalar el principio porque la trampa aparece más adelante en drivers reales.

### Tunables frente a módulos del kernel: dónde viven

Hay dos detalles pequeños pero importantes sobre el entorno del loader que vale la pena mencionar.

En primer lugar, un tunable en `/boot/loader.conf` se aplica desde el momento en que arranca el kernel. Está disponible para cualquier módulo que llame a `TUNABLE_*_FETCH` o que tenga un sysctl con `CTLFLAG_TUN`, aunque el módulo no se cargue en el boot. Un módulo cargado posteriormente con `kldload` sigue viendo el valor del tunable. Esto es conveniente: el operador establece el tunable una vez y lo olvida hasta que el módulo se carga.

En segundo lugar, un tunable se lee del entorno pero no se puede escribir de vuelta. Cambiar `hw.myfirst.timeout_sec` en tiempo de ejecución (con `kenv`) no afecta a ningún driver que ya lo haya leído; lo que importa es la variable en el softc, no el entorno. Para cambiar un valor en tiempo de ejecución, el operador usa el sysctl correspondiente.

Estos dos detalles juntos explican por qué `CTLFLAG_TUN` es la forma adecuada para la mayoría de los parámetros de configuración: el tunable establece el valor predeterminado en el boot, el sysctl gestiona el ajuste en tiempo de ejecución, y el conjunto de herramientas del operador (`/boot/loader.conf` más `sysctl(8)`) funciona como se espera.

### Cerrando la sección 3

La configuración es una conversación con el operador. Un driver que externaliza los valores adecuados mediante tunables y sysctls puede ajustarse sin recompilar; un driver que oculta cada valor dentro del código fuente obliga a recompilar ante cualquier cambio. La familia `TUNABLE_*_FETCH` y el flag `CTLFLAG_TUN` juntos cubren el ajuste en tiempo de boot y en tiempo de ejecución, y la elección entre configuración a nivel de driver y a nivel de instancia adapta el driver a su realidad operacional. El driver `myfirst` tiene ahora tres tunables nuevos además del `debug_mask_default` existente, cada uno con un rango documentado y con un sysctl correspondiente.

En la siguiente sección, pasamos de lo que el driver expone a cómo el driver evoluciona. Un parámetro de configuración que funciona hoy debe seguir funcionando mañana cuando el driver cambie. La disciplina de versionado es lo que mantiene esa promesa.

## Sección 4: Estrategias de versionado y compatibilidad

La cuarta disciplina es la disciplina de evolucionar sin romper la compatibilidad. Toda superficie pública que ofrece el driver `myfirst` (el nodo `/dev/myfirst0`, la interfaz ioctl, el árbol de sysctl, el conjunto de tunables) es un contrato con alguien. Un cambio que altera silenciosamente el significado de cualquiera de ellos es un cambio que rompe la compatibilidad, y los cambios que rompen la compatibilidad y se escurren sin que el desarrollador los advierta son el origen de una cantidad desproporcionada de bugs reales en drivers. Esta sección enseña cómo versionar deliberadamente la superficie pública de un driver, de modo que los cambios sean visibles para los llamadores y que los llamadores más antiguos sigan funcionando cuando el driver añade nuevas funcionalidades.

El capítulo usa tres números de versión distintos para el driver `myfirst`. Cada uno tiene un propósito específico. Confundirlos es una fuente de confusión que conviene evitar antes de que eche raíces.

### Los tres números de versión

El driver `myfirst` tiene tres identificadores de versión, introducidos a lo largo de los Capítulos 23, 24 y 25. Cada uno vive en un lugar diferente y cambia por una razón diferente.

El primero es la **cadena de versión legible por humanos**. Para `myfirst`, esta es `MYFIRST_VERSION`, definida en `myfirst_sysctl.c` y expuesta a través del sysctl `dev.myfirst.0.version`. Su valor actual es `"1.8-maintenance"`. La cadena de versión es para personas: un operador que ejecuta `sysctl dev.myfirst.0.version` ve una etiqueta corta que identifica este punto concreto de la historia del driver. La cadena de versión no la analizan programas; la leen personas. Cambia cada vez que el driver alcanza un nuevo hito que el autor quiere marcar, lo cual en este libro ocurre al final de cada capítulo.

El segundo es el **entero de versión del módulo del kernel**. Este es `MODULE_VERSION(myfirst, N)`, donde `N` es un entero utilizado por la maquinaria de dependencias del kernel. Otro módulo que declare `MODULE_DEPEND(other, myfirst, 1, 18, 18)` requiere que `myfirst` esté presente en la versión 18 o superior (y menor o igual a 18, lo que en esta declaración significa exactamente 18). El entero de versión del módulo cambia únicamente cuando un llamador interno del kernel necesitaría recompilar, por ejemplo cuando cambia la firma de un símbolo compartido. Para un driver que no exporta símbolos públicos del kernel (como `myfirst`), el número de versión del módulo es principalmente simbólico; el capítulo lo incrementa en cada hito para mantener el modelo mental del lector coherente entre los tres identificadores de versión.

El tercero es el **entero de versión de la interfaz ioctl**. Para `myfirst`, este es `MYFIRST_IOCTL_VERSION` en `myfirst_ioctl.h`. Su valor actual es 1. La versión de la interfaz ioctl cambia cuando la cabecera ioctl cambia de forma que un programa de espacio de usuario antiguo compilado contra la versión anterior lo interpretaría de manera errónea. Un comando ioctl renumerado, un diseño de payload modificado, una semántica cambiada en un ioctl existente: cada uno de estos es un cambio que rompe la compatibilidad de la interfaz ioctl y debe incrementar la versión. Añadir un nuevo comando ioctl, extender un payload con un campo al final sin reinterpretar los campos existentes, añadir una funcionalidad que no afecta a los comandos anteriores: estos son cambios compatibles y no requieren incremento.

Una regla sencilla mantiene los tres bien diferenciados. La cadena de versión es lo que lee el operador. El entero de versión del módulo es lo que comprueban otros módulos. El entero de versión de la interfaz ioctl es lo que comprueban los programas de espacio de usuario. Cada uno avanza según su propio calendario.

### Por qué los usuarios necesitan consultar la versión

Un programa de espacio de usuario que se comunica con el driver a través de ioctls tiene un problema. La cabecera `myfirst_ioctl.h` define un conjunto de comandos, diseños y semánticas de la versión 1. Una nueva versión del driver puede añadir comandos, cambiar diseños o cambiar semánticas. Cuando el programa de espacio de usuario se ejecuta en un sistema con una versión del driver más nueva o más antigua que aquella contra la que fue compilado, no tiene forma de conocer la versión real del driver a menos que la consulte.

La solución es un ioctl cuyo único propósito es devolver la versión ioctl del driver. El driver `myfirst` ya tiene uno: `MYFIRSTIOC_GETVER`, definido como `_IOR('M', 1, uint32_t)`. Un programa de espacio de usuario llama a este ioctl inmediatamente después de abrir el dispositivo, compara la versión devuelta con la versión contra la que fue compilado y decide si puede continuar de forma segura.

El patrón en espacio de usuario:

```c
#include "myfirst_ioctl.h"

int fd = open("/dev/myfirst0", O_RDWR);
uint32_t ver;
if (ioctl(fd, MYFIRSTIOC_GETVER, &ver) < 0)
        err(1, "getver");
if (ver != MYFIRST_IOCTL_VERSION)
        errx(1, "driver version %u, tool expects %u",
            ver, MYFIRST_IOCTL_VERSION);
```

La herramienta se niega a ejecutarse si las versiones no coinciden. Esa es una política posible. Una política más permisiva permitiría que la herramienta se ejecute contra un driver más nuevo si los nuevos ioctls del driver son superconjuntos de los anteriores, y permitiría que la herramienta se ejecute contra un driver más antiguo si la herramienta puede recurrir al conjunto de comandos anterior. Una política más rígida exigiría coincidencia exacta. El autor de la herramienta elige entre estas opciones según el esfuerzo que valga la pena invertir en compatibilidad hacia atrás.

### Añadir un nuevo ioctl sin romper los llamadores anteriores

El caso habitual es añadir una nueva funcionalidad al driver, lo que generalmente implica añadir un nuevo ioctl. La disciplina es sencilla siempre que se sigan dos reglas.

En primer lugar, **no reutilices un número de ioctl existente**. Cada comando ioctl tiene un par único `(magic, number)` codificado por `_IO`, `_IOR`, `_IOW` o `_IOWR`. Las asignaciones actuales en `myfirst_ioctl.h`:

```c
#define MYFIRSTIOC_GETVER   _IOR('M', 1, uint32_t)
#define MYFIRSTIOC_GETMSG   _IOR('M', 2, char[MYFIRST_MSG_MAX])
#define MYFIRSTIOC_SETMSG   _IOW('M', 3, char[MYFIRST_MSG_MAX])
#define MYFIRSTIOC_RESET    _IO('M', 4)
```

Un nuevo ioctl toma el siguiente número disponible bajo la misma letra mágica: `MYFIRSTIOC_GETCAPS = _IOR('M', 5, uint32_t)`. El número 5 no se ha utilizado antes y no puede entrar en conflicto con el binario compilado de un programa más antiguo. Un programa más antiguo compilado contra una versión sin `GETCAPS` simplemente nunca envía ese ioctl, por lo que no se ve afectado por la adición.

En segundo lugar, **no incrementes `MYFIRST_IOCTL_VERSION` por una adición pura**. Un nuevo ioctl que no cambia el significado de los anteriores es un cambio compatible. Un programa de espacio de usuario más antiguo que nunca oyó hablar del nuevo ioctl sigue hablando el mismo idioma; el entero de versión debe permanecer igual. Incrementar la versión ante cada adición obligaría a reconstruir todos los llamadores cada vez que el driver gana un nuevo comando, lo que anula el propósito del versionado.

Un nuevo ioctl que sustituye a uno existente con semánticas diferentes sí requiere un incremento. Si el driver añade `MYFIRSTIOC_SETMSG_V2` con un nuevo diseño y retira `MYFIRSTIOC_SETMSG`, los programas más antiguos que llamen al comando retirado verán un comportamiento diferente (el driver puede devolver `ENOIOCTL` o comportarse de otra manera). Eso es un cambio que rompe la compatibilidad, y el incremento lo señala.

### Retirar un ioctl en desuso

La retirada es la forma cortés y ordenada de eliminar un comando. Cuando un comando va a eliminarse, el driver anuncia la intención, mantiene el comando en funcionamiento durante un periodo de transición y lo elimina en una versión posterior. Una secuencia típica de deprecación:

- Versión N: se anuncia la deprecación en `MAINTENANCE.md`. El comando sigue funcionando.
- Versión N+1: el comando funciona, pero registra una advertencia de tasa limitada cada vez que se usa. Los usuarios ven la advertencia y saben que deben migrar.
- Versión N+2: el comando devuelve `EOPNOTSUPP` y registra un error de tasa limitada. La mayoría de los usuarios ya habrán migrado; los pocos que no lo hayan hecho se ven obligados a ello.
- Versión N+3: el comando se elimina de la cabecera. Los programas que aún hagan referencia a él dejan de compilar.

El periodo de transición debe medirse en versiones (típicamente una o dos versiones principales) y no en tiempo de calendario. Un driver que mantiene su contrato de deprecación de forma predecible ofrece a sus consumidores un objetivo estable al que apuntar.

Para `myfirst` en este capítulo, todavía no hay ningún comando en desuso. El capítulo introduce el patrón para el futuro. La misma disciplina se aplica al árbol de sysctl: una advertencia de tasa limitada en el manejador del OID indica a los operadores que el nombre está en camino de desaparecer, y una nota en `MAINTENANCE.md` registra la fecha de eliminación prevista.

### El patrón de máscara de bits de capacidades

Para los drivers que evolucionan a lo largo de varias versiones, un único entero de versión indica a los llamadores con qué versión están hablando, pero no qué funcionalidades específicas admite esa versión. Un driver rico en funcionalidades se beneficia de un mecanismo más granular: una máscara de bits de capacidades.

La idea es sencilla. El driver define un conjunto de bits de capacidades en `myfirst_ioctl.h`:

```c
#define MYF_CAP_RESET       (1U << 0)
#define MYF_CAP_GETMSG      (1U << 1)
#define MYF_CAP_SETMSG      (1U << 2)
#define MYF_CAP_TIMEOUT     (1U << 3)
#define MYF_CAP_MAXRETRIES  (1U << 4)
```

Un nuevo ioctl, `MYFIRSTIOC_GETCAPS`, devuelve un `uint32_t` con los bits establecidos para las funcionalidades que este driver admite realmente:

```c
#define MYFIRSTIOC_GETCAPS  _IOR('M', 5, uint32_t)
```

En el kernel:

```c
case MYFIRSTIOC_GETCAPS:
        *(uint32_t *)data = MYF_CAP_RESET | MYF_CAP_GETMSG |
            MYF_CAP_SETMSG;
        break;
```

En espacio de usuario:

```c
uint32_t caps;
ioctl(fd, MYFIRSTIOC_GETCAPS, &caps);
if (caps & MYF_CAP_TIMEOUT)
        set_timeout(fd, 60);
else
        warnx("driver does not support timeout configuration");
```

La máscara de bits de capacidades permite que un programa de espacio de usuario descubra funcionalidades sin necesidad de ensayo y error. Si el llamador quiere saber si una funcionalidad existe, comprueba el bit; si el bit está establecido, el llamador sabe que el driver admite la funcionalidad y los ioctls relevantes. Un driver más antiguo que no define el bit no pretende admitir una funcionalidad de la que nunca oyó hablar.

El patrón escala bien a medida que el driver crece. Cada versión añade nuevos bits para nuevas funcionalidades. Las funcionalidades retiradas conservan su bit reservado como no utilizado; reutilizar un bit para un nuevo significado sería un cambio incompatible. La máscara de bits en sí es un `uint32_t`, lo que le da al driver 32 funcionalidades antes de necesitar añadir una segunda palabra. Si el driver llega a 32 funcionalidades, añadir una segunda palabra es un cambio compatible (los nuevos bits están en un nuevo campo, de modo que los programas más antiguos que leen solo la primera palabra ven los mismos bits).

El capítulo 25 añade `MYFIRSTIOC_GETCAPS` al driver `myfirst` con tres bits activados: `MYF_CAP_RESET`, `MYF_CAP_GETMSG` y `MYF_CAP_SETMSG`. El programa en espacio de usuario `myfirstctl` se amplía para consultar las capacidades al inicio y para negarse a invocar una funcionalidad no admitida.

### Deprecación de sysctl

FreeBSD no ofrece un flag `CTLFLAG_DEPRECATED` dedicado en el árbol sysctl. El flag relacionado `CTLFLAG_SKIP`, definido en `/usr/src/sys/sys/sysctl.h`, oculta un OID de los listados predeterminados (sigue siendo legible si se nombra explícitamente), pero se usa principalmente para fines distintos de anunciar una retirada. La forma correcta de retirar un OID sysctl es, por tanto, reemplazar su manejador con uno que realice el trabajo previsto *y* registre una advertencia con límite de frecuencia las primeras veces que se accede al OID.

```c
static int
myfirst_sysctl_old_counter(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;

        DLOG_RL(sc, &sc->sc_rl_deprecated, MYF_RL_DEFAULT_PPS,
            "sysctl dev.myfirst.%d.old_counter is deprecated; "
            "use new_counter instead\n",
            device_get_unit(sc->sc_dev));
        return (sysctl_handle_int(oidp, &sc->sc_old_counter, 0, req));
}
```

El operador ve la advertencia en `dmesg` las primeras veces que se lee el OID, lo que es una señal clara para migrar. El sysctl sigue funcionando, de modo que los scripts que lo referencian explícitamente no se rompen durante la transición. Tras una o dos versiones, el OID en sí se elimina. Una nota en `MAINTENANCE.md` registra la intención y la versión objetivo.

Para `myfirst`, ningún sysctl está aún en desuso. El driver del capítulo 25 introduce el patrón en la documentación y lo deja listo para uso futuro.

### Cambios de comportamiento visibles para el usuario

No todo cambio que rompe la compatibilidad es un cambio de nombre o de numeración. A veces el driver mantiene el mismo ioctl, el mismo sysctl, el mismo tunable y cambia silenciosamente lo que hace la operación. Un `MYFIRSTIOC_RESET` que antes ponía a cero los contadores pero que ahora también borra el mensaje es un cambio de comportamiento. Un sysctl que antes informaba del total de bytes escritos pero ahora informa de kilobytes es un cambio de comportamiento. Un tunable que antes era un valor absoluto y ahora es un multiplicador es un cambio de comportamiento.

Los cambios de comportamiento son los más difíciles de detectar porque no aparecen en los diffs de los archivos de cabecera ni en los listados de sysctl. La disciplina consiste en documentar cada cambio de comportamiento en `MAINTENANCE.md` bajo una sección "Change Log", en incrementar el entero de versión de la interfaz ioctl cuando cambia la semántica de un ioctl, y en anunciar los cambios semánticos de sysctl en la propia cadena de descripción.

Un buen patrón para los cambios de comportamiento es introducir un nuevo comando con nombre o un nuevo sysctl en lugar de redefinir uno existente. `MYFIRSTIOC_RESET` mantiene la semántica antigua. `MYFIRSTIOC_RESET_ALL` es un nuevo comando con la nueva semántica. El comando antiguo acaba por quedar en desuso. El coste es una superficie pública ligeramente mayor durante el período de transición; el beneficio es que ningún código que lo invoca se rompe por un cambio de comportamiento silencioso.

### `MODULE_DEPEND` y compatibilidad entre módulos

El macro `MODULE_DEPEND` declara que un módulo depende de otro y requiere un rango de versiones específico:

```c
MODULE_DEPEND(myfirst, dependency, 1, 2, 3);
```

Los tres enteros son la versión mínima, la preferida y la máxima de `dependency` con las que `myfirst` es compatible. El kernel rechaza cargar `myfirst` si `dependency` no está presente o está fuera del rango.

Para los drivers que no publican símbolos en el kernel, `MODULE_DEPEND` se usa con más frecuencia para declarar dependencia de un módulo de subsistema estándar:

```c
MODULE_DEPEND(myfirst_usb, usb, 1, 1, 1);
```

Esto declara que la versión USB de `myfirst` necesita la pila USB exactamente en la versión 1. Los números de versión de los módulos de subsistema los gestionan los autores del subsistema; un autor de driver encuentra los valores actuales en la cabecera del subsistema (para USB, `/usr/src/sys/dev/usb/usbdi.h`) o en otro driver que ya depende de él.

Para `myfirst` al final del capítulo 25, no se necesita ningún `MODULE_DEPEND` porque el pseudo-driver no requiere ningún subsistema. El capítulo 26, dedicado a USB, añadirá el primer `MODULE_DEPEND` real cuando el driver se convierta en una versión con conexión USB.

### Un ejemplo práctico: la transición de 1.7 a 1.8

El driver del capítulo 25 incrementa tres identificadores de versión al final del capítulo:

- `MYFIRST_VERSION`: de `"1.7-integration"` a `"1.8-maintenance"`.
- `MODULE_VERSION(myfirst, N)`: de 17 a 18.
- `MYFIRST_IOCTL_VERSION`: se mantiene en 1, porque las incorporaciones de ioctl de este capítulo son adiciones puras (nuevos comandos, sin eliminaciones, sin cambios semánticos).

El ioctl `GETCAPS` se añade con el número de comando 5, que antes no estaba en uso. Los binarios más antiguos de `myfirstctl`, compilados contra la versión del capítulo 24 de la cabecera, no conocen `GETCAPS` y no lo envían; siguen funcionando sin cambios. Los nuevos binarios de `myfirstctl`, compilados contra la cabecera del capítulo 25, consultan `GETCAPS` al arrancar y se comportan en consecuencia.

El documento `MAINTENANCE.md` gana una entrada en el Change Log para la versión 1.8:

```text
## 1.8-maintenance

- Added MYFIRSTIOC_GETCAPS (command 5) returning a capability
  bitmask.  Compatible with all earlier user-space programs.
- Added tunables hw.myfirst.timeout_sec, hw.myfirst.max_retries,
  hw.myfirst.log_ratelimit_pps.  Each has a matching writable
  sysctl under dev.myfirst.<unit>.
- Added rate-limited logging through ppsratecheck(9).
- No breaking changes from 1.7.
```

Un usuario del driver que lea `MAINTENANCE.md` verá de un vistazo qué ha cambiado y podrá evaluar si necesita actualizar sus herramientas. Un usuario que no lea `MAINTENANCE.md` puede igualmente consultar las capacidades en tiempo de ejecución y descubrir las nuevas funciones de forma programática.

### Errores comunes en el versionado

Hay tres errores frecuentes al aplicar por primera vez la disciplina de versiones. Merece la pena nombrar cada uno de ellos.

El primer error es **reutilizar un número de ioctl**. Un número que fue asignado una vez y que luego quedó retirado permanece retirado. Un nuevo comando recibe el siguiente número disponible, no el número de un comando retirado. Reutilizar un número rompe silenciosamente a los invocadores más antiguos que tenían compilado el significado anterior; el compilador no tiene forma de detectar el conflicto porque la cabecera del comando retirado fue eliminada.

El segundo error es **incrementar el entero de versión con cada cambio**. Si cada parche incrementa `MYFIRST_IOCTL_VERSION`, las herramientas del espacio de usuario tienen que reconstruirse constantemente o la comprobación de versión falla. El entero debe cambiar únicamente por cambios genuinamente incompatibles. Las adiciones puras no lo modifican.

El tercer error es **tratar la cadena de versión como una versión semántica**. La cadena de versión es para personas; puede ser cualquier cosa. El entero de versión del módulo y el entero de versión del ioctl son analizados por programas y deben seguir una disciplina (monotónicamente creciente, incrementado solo por razones específicas). Confundir ambos lleva a números de versión confusos.

### Cerrando la sección 4

El versionado es la disciplina de evolucionar sin romper. Un driver que mantiene distintos sus tres identificadores de versión, sus adiciones de ioctl compatibles, sus deprecaciones anunciadas y sus bits de capacidad precisos ofrece a quienes lo invocan un objetivo estable durante la larga vida del driver. El driver `myfirst` tiene ahora un ioctl `GETCAPS` funcional, una política de deprecación documentada en `MAINTENANCE.md` y tres identificadores de versión que cambian cada uno por su propio motivo. Todo lo que un futuro desarrollador necesita para añadir una funcionalidad o retirar un comando ya está en su lugar.

En la siguiente sección, pasamos de la superficie pública del driver a la disciplina de recursos privados. Un driver que se bloquea al fallar el attach es un driver que no puede recuperarse de ningún error. El patrón de goto etiquetado es la forma en que los drivers de FreeBSD hacen que cada asignación sea reversible.

## Sección 5: Gestión de recursos en las rutas de fallo

Toda rutina attach es una secuencia ordenada de adquisiciones. Asigna un lock, crea un cdev, cuelga un árbol sysctl del dispositivo, quizás registra un manejador de eventos o un temporizador, y en drivers más complejos asigna recursos del bus, mapea ventanas de I/O, conecta una interrupción y configura DMA. Cada adquisición puede fallar. Y cada adquisición que tuvo éxito antes del fallo debe liberarse en orden inverso, o el kernel pierde memoria, pierde un lock, pierde un cdev y, en el peor caso, mantiene un nodo de dispositivo activo con un puntero obsoleto en su interior.

El driver `myfirst` ha ido creciendo su ruta de attach sección a sección desde el capítulo 17. El attach empezó siendo pequeño: un lock y un cdev. El capítulo 24 añadió el árbol sysctl. El capítulo 25 está a punto de añadir el estado de limitación de frecuencia, los valores predeterminados obtenidos mediante tunables y uno o dos contadores. El orden en que se adquieren esos recursos importa ahora a la ruta de limpieza. Cada nueva adquisición tiene que saber en qué lugar del orden de limpieza le corresponde estar, y la propia limpieza debe estar estructurada de forma que añadir un nuevo recurso la semana que viene no obligue a reescribir la función attach.

El capítulo 20 introdujo el patrón de forma informal; esta sección le da nombre, vocabulario y una disciplina lo bastante sólida como para sobrevivir a la forma completa de `myfirst_attach` en el capítulo 25.

### El problema: las rutas anidadas con `if` no escalan

La forma ingenua de una rutina attach es una escalera de sentencias `if` anidadas. Cada condición de éxito contiene el siguiente paso. Cada fallo retorna. El problema es que cada fallo tiene que deshacer lo que los pasos anteriores ya hicieron, y el código de limpieza se duplica en cada nivel de la escalera:

```c
/*
 * Naive attach.  DO NOT WRITE DRIVERS THIS WAY.  This example shows
 * how the nested-if pattern forces duplicated cleanup at every level
 * and why it becomes unmaintainable as soon as a fourth resource is
 * added to the chain.
 */
static int
myfirst_attach_bad(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	struct make_dev_args args;
	int error;

	sc->sc_dev = dev;
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

	make_dev_args_init(&args);
	args.mda_devsw   = &myfirst_cdevsw;
	args.mda_uid     = UID_ROOT;
	args.mda_gid     = GID_WHEEL;
	args.mda_mode    = 0660;
	args.mda_si_drv1 = sc;
	args.mda_unit    = device_get_unit(dev);

	error = make_dev_s(&args, &sc->sc_cdev, "myfirst%d",
	    device_get_unit(dev));
	if (error == 0) {
		myfirst_sysctl_attach(sc);
		if (myfirst_log_attach(sc) == 0) {
			/* all resources held; we succeeded */
			return (0);
		} else {
			/* log allocation failed: undo sysctl and cdev */
			/* but wait, sysctl is owned by Newbus, so skip it */
			destroy_dev(sc->sc_cdev);
			mtx_destroy(&sc->sc_mtx);
			return (ENOMEM);
		}
	} else {
		mtx_destroy(&sc->sc_mtx);
		return (error);
	}
}
```

Incluso en este pequeño ejemplo la lógica de limpieza aparece en dos lugares distintos, el lector tiene que leer una rama para saber qué recursos se han adquirido en cada punto, y añadir un cuarto recurso obliga a añadir otro nivel de anidamiento y otro bloque de limpieza duplicado. Los drivers reales tienen siete u ocho recursos. Un driver como `if_em` en `/usr/src/sys/dev/e1000/if_em.c` tiene más de una docena. El `if` anidado no es una opción allí.

Los modos de fallo del patrón anidado no son teóricos. Un patrón de bug frecuente en drivers antiguos de FreeBSD era un `mtx_destroy` omitido o un `bus_release_resource` omitido en alguna de las ramas de limpieza: una rama destruía el lock, otra lo olvidaba. Cada rama era una oportunidad para cometer un error, y el bug solo aparecía cuando ese fallo específico se producía, lo que significaba que a menudo no salía a la luz hasta que un cliente reportaba un pánico en un dispositivo que fallaba al hacer el attach.

### El patrón `goto fail;`

La respuesta de FreeBSD al problema de la limpieza anidada es el patrón de goto etiquetado. La función attach se escribe como una secuencia lineal de adquisiciones. Cada adquisición que puede fallar va seguida de una comprobación que sigue adelante en caso de éxito o salta a una etiqueta de limpieza en caso de fallo. Las etiquetas de limpieza están ordenadas de la más adquirida a la menos adquirida. Cada etiqueta libera los recursos que se mantenían en ese punto y cae al nivel de la siguiente etiqueta. La función termina con un único `return (0)` en caso de éxito y un único `return (error)` al final de la cadena de limpieza:

```c
static int
myfirst_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	struct make_dev_args args;
	int error;

	/* Resource 1: softc basics.  Cannot fail. */
	sc->sc_dev = dev;

	/* Resource 2: the lock.  Cannot fail on DEF mutex. */
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

	/* Resource 3: the cdev.  Can fail. */
	make_dev_args_init(&args);
	args.mda_devsw   = &myfirst_cdevsw;
	args.mda_uid     = UID_ROOT;
	args.mda_gid     = GID_WHEEL;
	args.mda_mode    = 0660;
	args.mda_si_drv1 = sc;
	args.mda_unit    = device_get_unit(dev);

	error = make_dev_s(&args, &sc->sc_cdev, "myfirst%d",
	    device_get_unit(dev));
	if (error != 0)
		goto fail_mtx;

	/* Resource 4: the sysctl tree.  Cannot fail (Newbus owns it). */
	myfirst_sysctl_attach(sc);

	/* Resource 5: the log state.  Can fail. */
	error = myfirst_log_attach(sc);
	if (error != 0)
		goto fail_cdev;

	/* All resources held.  Announce and return. */
	DPRINTF(sc, MYF_DBG_INIT,
	    "attach: version 1.8-maintenance ready\n");
	return (0);

fail_cdev:
	destroy_dev(sc->sc_cdev);
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
}
```

Lee la función de arriba abajo. Cada paso es una adquisición de recursos. Cada comprobación de fallo es un bloque de dos líneas: si la adquisición falló, salta a la etiqueta que lleva el nombre del recurso adquirido anteriormente. Las etiquetas del final liberan los recursos en orden inverso y caen al nivel de la siguiente etiqueta. El `return (error)` final devuelve el errno de la adquisición que falló.

Esta forma escala. Añadir un sexto recurso implica añadir un bloque de adquisición en la parte superior, un objetivo `goto` en la parte inferior y una línea de código de limpieza. Sin anidamiento, sin duplicación, sin árbol de ramas. La misma regla que gobierna la ruta de attach gobierna cada adición futura a la ruta de attach: adquirir, comprobar, ir a la etiqueta anterior, liberar en orden inverso.

### Por qué la limpieza lineal es la forma correcta

El valor del patrón de goto etiquetado no es puramente estilístico. Se corresponde directamente con la propiedad estructural de que la secuencia de attach es una pila de recursos, y la limpieza es la operación de extracción de esa pila.

Una pila tiene tres propiedades que son fáciles de enunciar y fáciles de violar. Primera, los recursos se liberan en el orden inverso al de su adquisición. Segunda, una adquisición fallida no añade un recurso a la pila, por lo que la limpieza comienza desde el recurso adquirido anteriormente, no desde el que acaba de fallar. Tercera, cada recurso de la pila se libera exactamente una vez: ni cero veces ni dos.

Cada una de estas propiedades tiene un correlato visible en el patrón `goto fail;`. Las etiquetas de limpieza aparecen en el archivo en el orden inverso al de las adquisiciones: la etiqueta de limpieza de la última adquisición está en la parte superior de la cadena de limpieza. Una adquisición fallida salta a la etiqueta que lleva el nombre de la adquisición anterior, no de sí misma; el nombre de la etiqueta es literalmente el nombre del recurso que ahora hay que deshacer. Y como cada etiqueta cae al nivel de la siguiente, y cada recurso aparece en exactamente una etiqueta, cada recurso se libera exactamente una vez en cada ruta de fallo.

La disciplina de pila es lo que hace robusto al patrón. Si un lector quiere auditar la ruta de limpieza en busca de corrección, no tiene que leer ramas. Solo tiene que contar las etiquetas, contar las adquisiciones y comparar.

### Convenciones de nomenclatura de etiquetas

Las etiquetas en los drivers de FreeBSD comienzan tradicionalmente con `fail_` seguido del nombre del recurso que está a punto de deshacerse. El nombre del recurso coincide con el nombre del campo en el softc o con el nombre de la función que se llamó para adquirirlo. Patrones habituales que se encuentran en todo el árbol:

- `fail_mtx` deshace `mtx_init`
- `fail_sx` deshace `sx_init`
- `fail_cdev` deshace `make_dev_s`
- `fail_ires` deshace `bus_alloc_resource` para una IRQ
- `fail_mres` deshace `bus_alloc_resource` para una ventana de memoria
- `fail_intr` deshace `bus_setup_intr`
- `fail_dma_tag` deshace `bus_dma_tag_create`
- `fail_log` deshace una asignación privada del driver (el bloque de limitación de tasa en `myfirst`)

Algunos drivers más antiguos usan etiquetas numeradas (`fail1`, `fail2`, `fail3`). Las etiquetas numeradas son válidas pero inferiores: añadir un recurso en medio de la secuencia obliga a renumerar todas las etiquetas posteriores al punto de inserción, y los números de etiqueta no indican al lector qué recurso se está liberando. Las etiquetas con nombre sobreviven a las inserciones sin problemas y se documentan por sí solas.

Sea cual sea la convención que elija un driver, debe ser coherente en todos sus archivos. `myfirst` usa la convención `fail_<resource>` para cada función attach a partir de este capítulo.

### La regla del encadenamiento por caída

La única regla que toda cadena de limpieza debe obedecer es que cada etiqueta de limpieza caiga hacia la siguiente. Un `return` suelto en medio de la cadena, o una etiqueta que falta, omite la limpieza de los recursos que deberían haberse liberado. El compilador no advierte sobre ninguno de los dos errores.

Considera qué ocurre si un desarrollador edita la cadena de limpieza y escribe accidentalmente esto:

```c
fail_cdev:
	destroy_dev(sc->sc_cdev);
	return (error);          /* BUG: skips mtx_destroy. */
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
```

El primer `return` impide que `mtx_destroy` se ejecute en la ruta `fail_cdev`. El lock queda con una fuga. El código witness del kernel no se quejará, porque el lock perdido nunca vuelve a adquirirse. La fuga persiste hasta que la máquina reinicia. Es invisible en operación normal y solo se manifiesta como un lento incremento de memoria en un sistema donde el driver se adjunta y falla repetidamente (un dispositivo de conexión en caliente, por ejemplo).

La manera de prevenir este tipo de bug es escribir la cadena de limpieza con un único `return` al final y sin retornos intermedios. Las etiquetas intermedias contienen únicamente la llamada de limpieza para su recurso. El encadenamiento por caída es el comportamiento predeterminado e intencionado:

```c
fail_cdev:
	destroy_dev(sc->sc_cdev);
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
```

Un lector que audite la cadena la lee como una lista simple: destruir el cdev, destruir el lock, retornar. No hay ramificaciones que seguir, y añadir una etiqueta implica añadir una sola línea de código de limpieza y, opcionalmente, un nuevo destino.

### Cómo es la ruta de éxito

La función attach tiene éxito con un único `return (0)`, situado inmediatamente antes de la primera etiqueta de limpieza. Este es el punto en el que todas las adquisiciones han tenido éxito y no se necesita ninguna limpieza. El `return (0)` separa visualmente la cadena de adquisición de la cadena de limpieza: todo lo que está por encima es adquisición, todo lo que está por debajo es limpieza.

Algunos drivers olvidan esta separación y caen desde la última adquisición hasta la primera etiqueta de limpieza, liberando recursos que acaban de adquirir. Un `return (0)` que falta por descuido es la manera más sencilla de provocar este bug:

```c
	/* Resource N: the final acquisition. */
	...

	/* Forgot to put a return here. */

fail_cdev:
	destroy_dev(sc->sc_cdev);
```

Sin el `return (0)`, el control cae hacia `fail_cdev` tras cada attach exitoso, destruyendo el cdev en la ruta de éxito. El driver informa entonces del attach como fallido porque `error` es cero y el kernel ve el retorno exitoso, pero el cdev que acaba de crear ha desaparecido. El resultado es un nodo de dispositivo que desaparece segundos después de aparecer. Depurar esto requiere percatarse de que el mensaje de attach se imprime pero el dispositivo no responde: no es un bug fácil de encontrar en un log con mucha actividad.

La defensa es la disciplina. Toda función attach termina su cadena de adquisición con `return (0);` en su propia línea, seguida de una línea en blanco y de las etiquetas de limpieza. Sin excepciones. Un linter como `igor` o el ojo de un revisor detecta las violaciones rápidamente cuando la forma es siempre la misma.

### Cuando una adquisición no puede fallar

Algunas adquisiciones no pueden fallar. `mtx_init` para un mutex de estilo predeterminado no puede devolver un error. `sx_init` tampoco. `callout_init_mtx` tampoco. Las llamadas `SYSCTL_ADD_*` no pueden devolver un error que se espere que compruebe un driver (un fallo allí es un problema interno del kernel, no un problema del driver).

Para las adquisiciones que no pueden fallar, no hay goto. La adquisición va seguida del siguiente paso sin una comprobación. La etiqueta de limpieza para la adquisición sigue siendo necesaria, porque la cadena de limpieza tiene que liberar el recurso si una adquisición posterior falla:

```c
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

	error = make_dev_s(&args, &sc->sc_cdev, ...);
	if (error != 0)
		goto fail_mtx;       /* undoes the lock. */
```

`fail_mtx` existe aunque `mtx_init` en sí no tenga una ruta de fallo, porque el lock sigue necesitando destruirse si algo posterior a él falla.

El patrón se mantiene: todo recurso adquirido tiene una etiqueta, independientemente de si su adquisición puede fallar o no.

### Funciones auxiliares para reducir la duplicación

Cuando varias adquisiciones comparten la misma forma (asignar, comprobar, goto en caso de error), es tentador ocultarlas detrás de una función auxiliar. El trabajo de la función auxiliar es consolidar la adquisición y la comprobación; el llamador solo ve una única línea `if (error != 0) goto fail_X;`. Esto está bien siempre que la función auxiliar siga la misma disciplina: en caso de fallo, no libera nada de lo que adquirió parcialmente, y devuelve un errno con significado para que el destino del goto del llamador pueda confiar en él.

En `myfirst`, los ejemplos de la Sección 5 presentan una función auxiliar llamada `myfirst_log_attach` que asigna el estado de limitación de tasa, inicializa sus campos y devuelve 0 en caso de éxito o un errno distinto de cero en caso de fallo. La función attach la invoca con una sola línea:

```c
	error = myfirst_log_attach(sc);
	if (error != 0)
		goto fail_cdev;
```

La propia función auxiliar sigue el mismo patrón internamente. Si asigna dos recursos y el segundo falla, la función auxiliar deshace el primero antes de retornar. El llamador ve la función auxiliar como una única adquisición atómica: o ha tenido éxito completo o ha fallado por completo, y el llamador nunca tiene que preocuparse por el estado intermedio de la función auxiliar.

Sin embargo, las funciones auxiliares que simplifican en exceso rompen el patrón. Una función auxiliar que asigna un recurso y lo almacena en el softc está bien. Una función auxiliar que asigna un recurso, lo almacena en el softc y también lo libera en caso de error no está bien: la etiqueta de limpieza del llamador también intentará liberarlo, lo que provocará un double-free. La regla es que las funciones auxiliares de adquisición o bien tienen éxito y dejan el recurso en el softc, o bien fallan y dejan el softc sin cambios. No tienen éxito a medias.

### El detach como espejo del attach

La rutina detach es la cadena de limpieza de un attach exitoso. Tiene que liberar exactamente los recursos que attach adquirió, en orden inverso. La forma de la función detach es la forma de la cadena de limpieza sin las etiquetas y sin las adquisiciones:

```c
static int
myfirst_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	/* Check for busy first. */
	mtx_lock(&sc->sc_mtx);
	if (sc->sc_open_count > 0) {
		mtx_unlock(&sc->sc_mtx);
		return (EBUSY);
	}
	mtx_unlock(&sc->sc_mtx);

	/* Release resources in the reverse order of attach. */
	myfirst_log_detach(sc);
	destroy_dev(sc->sc_cdev);
	/* Sysctl is cleaned up by Newbus after detach returns. */
	mtx_destroy(&sc->sc_mtx);

	return (0);
}
```

Leída junto a la función attach, la correspondencia es exacta. Cada recurso nombrado en attach tiene su liberación en detach. Cada nueva adquisición añadida a attach tiene su correspondiente añadido en detach. Un revisor que audite un parche que añade un nuevo recurso al driver debería poder encontrar ambas adiciones en el diff, una en la cadena del attach y otra en la del detach; un diff que añade solo al attach está incompleto.

Una disciplina útil al tocar la cadena del attach es abrir la función detach en un buffer del editor adyacente y añadir la liberación inmediatamente después de añadir la adquisición. Esta es la manera más sencilla de asegurarse de que las dos funciones permanecen sincronizadas: se editan juntas como una única operación.

### Inyección deliberada de fallos para pruebas

Una cadena de limpieza es correcta solo si toda etiqueta es alcanzable. La única manera de estar seguros es disparar cada ruta de fallo adrede y observar que el driver se descarga limpiamente después. Esperar a fallos reales de hardware para ejercitar las rutas no es una estrategia: la mayoría de las rutas nunca se ejercitan en la práctica.

La herramienta para este tipo de pruebas es la inyección deliberada de fallos. El desarrollador añade un `goto` temporal o un retorno anticipado temporal en medio de la cadena del attach y confirma que todos los recursos del driver se liberan cuando se activa el fallo inyectado.

Un patrón mínimo para `myfirst`:

```c
#ifdef MYFIRST_DEBUG_INJECT_FAIL_CDEV
	error = ENOMEM;
	goto fail_cdev;
#endif
```

Compila el driver con `-DMYFIRST_DEBUG_INJECT_FAIL_CDEV` y cárgalo. Attach devuelve `ENOMEM`. `kldstat` no muestra ningún residuo. `dmesg` muestra el fallo del attach y ninguna queja del kernel sobre locks o recursos con fugas. Descarga el módulo, elimina el define, recompila, y el driver vuelve a la normalidad.

Hazlo una vez por cada etiqueta, en este orden:

1. Inyecta un fallo justo después de que el lock se inicialice. Confirma que solo se libera el lock.
2. Inyecta un fallo justo después de que el cdev se cree. Confirma que el cdev y el lock se liberan.
3. Inyecta un fallo justo después de que el árbol sysctl se construya. Confirma que el cdev y el lock se liberan, y que los OIDs de sysctl desaparecen.
4. Inyecta un fallo justo después de que el estado del log se inicialice. Confirma que todos los recursos adquiridos hasta ese punto se liberan.

Si alguna inyección deja un residuo, la cadena de limpieza tiene un bug. Corrige el bug, repite la inyección y continúa.

Este trabajo resulta incómodo la primera vez, y reconfortante después. Un driver cuyas rutas de fallo se han ejercitado una vez es un driver cuyas rutas de fallo seguirán funcionando a medida que el código evolucione. Un driver cuyas rutas de fallo nunca se han ejercitado es un driver con bugs latentes que aparecerán en el peor momento posible.

El ejemplo complementario `ex05-failure-injection/` en `examples/part-05/ch25-advanced/` contiene una versión de `myfirst_attach` con cada punto de inyección de fallos marcado por un `#define` comentado. El laboratorio al final del capítulo recorre cada inyección por orden.

### Un `myfirst_attach` completo para el Capítulo 25

Reuniendo todo el contenido de la Sección 5 junto con los añadidos del Capítulo 25 (estado del log, lecturas de tunables, máscara de capacidades), la función attach final tiene este aspecto:

```c
static int
myfirst_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	struct make_dev_args args;
	int error;

	/*
	 * Stage 1: softc basics.  Cannot fail.  Recorded for consistency;
	 * no cleanup label is needed because no resource is held yet.
	 */
	sc->sc_dev = dev;

	/*
	 * Stage 2: lock.  Cannot fail on MTX_DEF, but needs a label
	 * because anything below this line can fail and must release it.
	 */
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

	/*
	 * Stage 3: pre-populate the softc with defaults, then allow
	 * boot-time tunables to override.  No allocations here, so no
	 * cleanup is needed.  Defaults come from the Section 3 tunable
	 * set.
	 */
	strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
	sc->sc_msglen = strlen(sc->sc_msg);
	sc->sc_open_count = 0;
	sc->sc_total_reads = 0;
	sc->sc_total_writes = 0;
	sc->sc_debug = 0;
	sc->sc_timeout_sec = 5;
	sc->sc_max_retries = 3;
	sc->sc_log_pps = MYF_RL_DEFAULT_PPS;

	TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default",
	    &sc->sc_debug);
	TUNABLE_INT_FETCH("hw.myfirst.timeout_sec",
	    &sc->sc_timeout_sec);
	TUNABLE_INT_FETCH("hw.myfirst.max_retries",
	    &sc->sc_max_retries);
	TUNABLE_INT_FETCH("hw.myfirst.log_ratelimit_pps",
	    &sc->sc_log_pps);

	/*
	 * Stage 4: cdev.  Can fail.  On failure, release the lock and
	 * return the error from make_dev_s.
	 */
	make_dev_args_init(&args);
	args.mda_devsw   = &myfirst_cdevsw;
	args.mda_uid     = UID_ROOT;
	args.mda_gid     = GID_WHEEL;
	args.mda_mode    = 0660;
	args.mda_si_drv1 = sc;
	args.mda_unit    = device_get_unit(dev);

	error = make_dev_s(&args, &sc->sc_cdev, "myfirst%d",
	    device_get_unit(dev));
	if (error != 0)
		goto fail_mtx;

	/*
	 * Stage 5: sysctl tree.  Cannot fail.  The framework owns the
	 * context, so no cleanup label is required specifically for it.
	 */
	myfirst_sysctl_attach(sc);

	/*
	 * Stage 6: rate-limit and counter state.  Can fail if memory
	 * allocation fails.  On failure, release the cdev and the lock.
	 */
	error = myfirst_log_attach(sc);
	if (error != 0)
		goto fail_cdev;

	DPRINTF(sc, MYF_DBG_INIT,
	    "attach: version 1.8-maintenance complete\n");
	return (0);

fail_cdev:
	destroy_dev(sc->sc_cdev);
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
}
```

Cada recurso está contabilizado. Cada ruta de fallo es lineal. La función tiene un único retorno de éxito en la transición de adquisiciones a limpieza, y un único retorno de fallo al final de la cadena de limpieza. Añadir un séptimo recurso en el próximo capítulo es una operación de tres líneas: un nuevo bloque de adquisición, una nueva etiqueta y una nueva línea de limpieza.

### Errores habituales en las rutas de fallo

Merece la pena nombrar algunos errores en las rutas de fallo al menos una vez, para poder reconocerlos cuando aparezcan en el código de otra persona o en una revisión.

El primer error es **una etiqueta que falta**. Un desarrollador añade una nueva adquisición de recurso pero olvida añadir su etiqueta de limpieza. El compilador no advierte; la cadena parece correcta desde fuera; pero en caso de fallo después de la nueva adquisición, se omite la limpieza de todo lo que viene después. La regla es que cada adquisición tiene una etiqueta. Aunque la adquisición no pueda fallar, sigue necesitando una etiqueta para que las adquisiciones posteriores puedan alcanzarla.

El segundo error es **liberar un recurso dos veces**. Un desarrollador añade una limpieza local dentro de una función auxiliar y olvida que la etiqueta de limpieza del llamador también libera el recurso. La función auxiliar libera una vez, el llamador libera de nuevo, y o bien el kernel entra en pánico (en el caso de memoria) o el código witness se queja (en el caso de locks). La regla es que solo una parte es responsable de la limpieza de cada recurso. Si la función auxiliar adquiere el recurso y lo almacena en el softc, la función auxiliar no lo limpia en nombre del llamador; o tiene éxito o deja el softc sin cambios.

El tercer error es **depender de comprobaciones de `NULL`**. Un desarrollador escribe una cadena de limpieza como esta:

```c
fail_cdev:
	if (sc->sc_cdev != NULL)
		destroy_dev(sc->sc_cdev);
fail_mtx:
	if (sc->sc_mtx_initialised)
		mtx_destroy(&sc->sc_mtx);
```

La lógica es la siguiente: saltarse la limpieza si el recurso no se adquirió realmente. La intención es defensiva; el efecto es ocultar bugs. Si la comprobación de `NULL` está ahí porque la limpieza puede alcanzarse en un estado en el que el recurso no se ha adquirido, la cadena es incorrecta: el destino del `goto` debería ser una etiqueta diferente. El comportamiento correcto es que la etiqueta de limpieza sea inalcanzable a menos que el recurso haya sido adquirido realmente. Las etiquetas que pueden alcanzarse en cualquiera de los dos estados son síntoma de un orden de adquisición confuso, y la comprobación de `NULL` solo enmascara el problema.

El cuarto error es **usar `goto` en flujos que no son de error**. El `goto` dentro de la función attach es exclusivamente para rutas de fallo. Un `goto` que salta una sección de la cadena de adquisición bajo alguna condición que no es de error viola el invariante de limpieza lineal: la cadena de limpieza asume que cada etiqueta corresponde a un recurso que ha sido adquirido, y un `goto` que evita una adquisición rompe esa suposición. Si se necesita una adquisición condicional, utiliza un `if` alrededor de la propia adquisición, no un `goto` a su alrededor.

### Cerrando la sección 5

Attach y detach son las costuras que mantienen unido el driver al kernel. Un attach correcto es una pila lineal de adquisiciones; un detach correcto es esa pila vaciada en orden inverso. El patrón de goto etiquetado es la forma en que los drivers de FreeBSD codifican esa pila en C sin adoptar los mecanismos de otros lenguajes (destructores de C++, defer de Go, Drop de Rust). No resulta llamativo, pero escala bien: un driver con una docena de recursos se lee exactamente igual que uno con dos, y las reglas para añadir un nuevo recurso son siempre las mismas.

La función attach de `myfirst` tiene ahora cuatro etiquetas de fallo y una separación clara entre la adquisición, el retorno exitoso y la limpieza. Cada nuevo recurso que el Capítulo 26 añada encajará en esta misma forma.

En la siguiente sección, nos alejamos de cualquier función concreta y observamos cómo un driver en crecimiento se distribuye entre varios archivos. Un único `myfirst.c` con todas las funciones nos ha bastado durante ocho capítulos; ha llegado el momento de dividirlo en unidades específicas para que la estructura del driver sea visible a nivel de archivo.

## Sección 6: Modularización y separación de responsabilidades

Al llegar al final del Capítulo 24, el driver `myfirst` ha crecido más allá de lo que un solo archivo fuente puede albergar cómodamente. La estructura de archivos era `myfirst.c` junto con `myfirst_debug.c`, `myfirst_ioctl.c` y `myfirst_sysctl.c`; `myfirst.c` seguía conteniendo el cdevsw, los callbacks de lectura/escritura, los callbacks de apertura/cierre, las rutinas attach y detach, y el código de pegamento del módulo. Eso estaba bien para enseñar, porque cada adición caía en un archivo lo suficientemente pequeño como para que un lector lo retuviese en la cabeza. Ya no resulta adecuado para un driver que tiene una superficie ioctl, un árbol sysctl, un framework de depuración, un helper de logging con limitación de tasa, una máscara de bits de capacidades, una disciplina de versionado y una rutina attach con limpieza etiquetada. Un archivo con tanto contenido se vuelve difícil de leer, difícil de revisar y difícil de entregar a un colaborador nuevo.

La Sección 6 va en la dirección contraria. No introduce ningún comportamiento nuevo; todas las funciones que existen al final de la Sección 5 siguen presentes al final de la Sección 6. Lo que cambia es la disposición de archivos y los límites entre las piezas. El objetivo es un driver cuya estructura puedas entender con un simple `ls`, y cuyos archivos individuales respondan cada uno a una sola pregunta.

### Por qué dividir los archivos

La tentación con un driver autocontenido es mantener todo en un único archivo. Un único `myfirst.c` es fácil de localizar, fácil de buscar con grep y fácil de copiar en un tarball. Dividirlo parece burocracia. El argumento para hacerlo aparece cuando el driver supera uno de tres umbrales.

El primer umbral es la **comprensión**. Un lector que abra `myfirst.c` debería ser capaz de encontrar lo que busca en pocos segundos. Un archivo de 1200 líneas con ocho responsabilidades no relacionadas es difícil de navegar; el lector tiene que desplazarse más allá del cdevsw para encontrar el sysctl, más allá del sysctl para encontrar el ioctl, más allá del ioctl para encontrar la rutina attach. Cada vez que cambia de tema, tiene que recargar su contexto mental. Con archivos separados, el tema es el nombre del archivo: `myfirst_ioctl.c` trata de los ioctls, `myfirst_sysctl.c` trata de los sysctls, `myfirst.c` trata del ciclo de vida.

El segundo umbral es la **independencia**. Dos cambios no relacionados no deberían modificar el mismo archivo. Cuando un desarrollador añade un sysctl y otro añade un ioctl, sus parches no deberían competir por las mismas líneas de `myfirst.c`. Los archivos pequeños y específicos permiten que dos cambios lleguen en paralelo sin conflictos de fusión y sin riesgo de que un error en uno afecte accidentalmente al otro.

El tercer umbral es la **testabilidad y la reutilización**. La infraestructura de logging de un driver, su despacho ioctl y su árbol sysctl suelen ser útiles para más de un driver dentro del mismo proyecto. Mantenerlos en archivos separados con interfaces limpias los convierte en candidatos para su reutilización posterior. Un driver que vive en un único archivo no puede compartir nada fácilmente; extraer código implica copiar y renombrar manualmente, lo que es una operación propensa a errores.

`myfirst` al final del Capítulo 25 ha superado los tres umbrales. Dividir el archivo es el acto de mantenimiento que mantiene el driver saludable para los próximos diez capítulos.

### Una disposición de archivos para `myfirst`

La disposición propuesta es la que utiliza el Makefile en el directorio de ejemplos del Capítulo 25 final:

```text
myfirst.h          - public types and constants (softc, SRB, status bits).
myfirst.c          - module glue, cdevsw, devclass, module events.
myfirst_bus.c      - Newbus methods and device_identify.
myfirst_cdev.c     - open/close/read/write callbacks; no ioctl.
myfirst_ioctl.h    - ioctl command numbers and payload structures.
myfirst_ioctl.c    - myfirst_ioctl switch and helpers.
myfirst_sysctl.c   - myfirst_sysctl_attach and handlers.
myfirst_debug.h    - DPRINTF/DLOG/DLOG_RL macros and class bits.
myfirst_debug.c    - debug-class enumeration (if any out-of-line).
myfirst_log.h      - rate-limit state structure.
myfirst_log.c      - myfirst_log_attach/detach and helpers.
```

Siete archivos `.c` y cuatro archivos `.h`. Cada archivo `.c` tiene un tema denominado por su nombre de archivo. Los archivos de cabecera declaran las interfaces que cruzan los límites entre archivos. Ningún archivo importa los internos de otro; toda referencia entre archivos pasa por un archivo de cabecera.

A primera vista parece más archivos de los que el driver necesita. No lo es. Cada archivo tiene una responsabilidad específica, y el archivo de cabecera que lo acompaña tiene entre una y tres docenas de líneas de declaraciones. El tamaño acumulado es el mismo que el de la versión de archivo único; la estructura es considerablemente más clara.

### La regla de responsabilidad única

La regla que gobierna la división es la regla de responsabilidad única: cada archivo responde a una sola pregunta sobre el driver.

- `myfirst.c` responde: ¿cómo se adjunta este módulo al kernel y conecta sus piezas?
- `myfirst_bus.c` responde: ¿cómo descubre e instancia Newbus mi driver?
- `myfirst_cdev.c` responde: ¿cómo sirve el driver las operaciones open/close/read/write?
- `myfirst_ioctl.c` responde: ¿cómo gestiona el driver los comandos que declara su cabecera?
- `myfirst_sysctl.c` responde: ¿cómo expone el driver su estado a `sysctl(8)`?
- `myfirst_debug.c` responde: ¿cómo se clasifican y limitan en tasa los mensajes de depuración?
- `myfirst_log.c` responde: ¿cómo se inicializa y libera el estado de limitación de tasa?

La prueba que determina si un cambio pertenece a un archivo dado es la prueba de la respuesta. Si el cambio no responde a la pregunta del archivo, pertenece a otro lugar. Un nuevo sysctl no pertenece a `myfirst_ioctl.c`; un nuevo ioctl no pertenece a `myfirst_sysctl.c`; una nueva variante de callback de lectura no pertenece a `myfirst.c`. La regla es explícita, y un revisor que la aplique rechazará los parches que coloquen las cosas en el archivo incorrecto.

Aplicar la regla a la estructura del Capítulo 24 da como resultado la estructura del Capítulo 25.

### Cabeceras públicas frente a cabeceras privadas

Los archivos de cabecera transportan la interfaz entre archivos. Un driver que divide sus archivos `.c` tiene que decidir, para cada declaración, si pertenece a una cabecera pública o a una privada.

Las **cabeceras públicas** contienen tipos y constantes que son visibles para más de un archivo `.c`. `myfirst.h` es la cabecera pública principal del driver. Declara:

- La definición de `struct myfirst_softc` (todo archivo `.c` la necesita).
- Constantes que aparecen en más de un archivo (bits de clase de depuración, tamaños de campos del softc).
- Prototipos de funciones que se invocan a través de los límites de los archivos (`myfirst_sysctl_attach`, `myfirst_log_attach`, `myfirst_log_ratelimited_printf`, `myfirst_ioctl`).

Las **cabeceras privadas** contienen declaraciones que solo necesita un único archivo `.c`. `myfirst_ioctl.h` es el ejemplo canónico. Declara los números de comando y las estructuras de carga útil; las necesitan `myfirst_ioctl.c` y los llamantes en espacio de usuario, pero ningún otro archivo del kernel las necesita. Incluirlas en `myfirst.h` filtraría el formato de transmisión a cada unidad de traducción.

La distinción importa porque toda declaración pública es un contrato que el driver debe respetar. Un tipo en `myfirst.h` que cambie de tamaño rompe todos los archivos que incluyen `myfirst.h`. Un tipo en `myfirst_ioctl.h` que cambie de tamaño solo rompe `myfirst_ioctl.c` y las herramientas en espacio de usuario que compilaron contra él.

Para `myfirst` al final del Capítulo 25, la cabecera pública `myfirst.h` tiene el siguiente aspecto (recortada a las declaraciones relevantes para esta sección):

```c
/*
 * myfirst.h - public types and constants for the myfirst driver.
 *
 * Types and prototypes declared here are visible to every .c file in
 * the driver.  Keep this header small.  Wire-format declarations live
 * in myfirst_ioctl.h.  Debug macros live in myfirst_debug.h.  Rate-
 * limit state lives in myfirst_log.h.
 */

#ifndef _MYFIRST_H_
#define _MYFIRST_H_

#include <sys/types.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/conf.h>

#include "myfirst_log.h"

struct myfirst_softc {
	device_t       sc_dev;
	struct mtx     sc_mtx;
	struct cdev   *sc_cdev;

	char           sc_msg[MYFIRST_MSG_MAX];
	size_t         sc_msglen;

	u_int          sc_open_count;
	u_int          sc_total_reads;
	u_int          sc_total_writes;

	u_int          sc_debug;
	u_int          sc_timeout_sec;
	u_int          sc_max_retries;
	u_int          sc_log_pps;

	struct myfirst_ratelimit sc_rl_generic;
	struct myfirst_ratelimit sc_rl_io;
	struct myfirst_ratelimit sc_rl_intr;
};

#define MYFIRST_MSG_MAX  256

/* Sysctl tree. */
void myfirst_sysctl_attach(struct myfirst_softc *);

/* Rate-limit state. */
int  myfirst_log_attach(struct myfirst_softc *);
void myfirst_log_detach(struct myfirst_softc *);

/* Ioctl dispatch. */
struct thread;
int  myfirst_ioctl(struct cdev *, u_long, caddr_t, int, struct thread *);

#endif /* _MYFIRST_H_ */
```

Nada en `myfirst.h` hace referencia a ninguna constante de formato de transmisión, ningún bit de clase de depuración ni a los internos de ninguna estructura de limitación de tasa. El softc incluye tres campos de limitación de tasa por valor, por lo que `myfirst.h` tiene que incluir `myfirst_log.h`, pero los internos de `struct myfirst_ratelimit` viven en `myfirst_log.h` y no se exponen aquí.

### La anatomía de `myfirst.c` tras la división

`myfirst.c` tras la división es el archivo `.c` más corto del driver. Contiene la tabla cdevsw, el manejador de eventos del módulo, la declaración de clase del dispositivo y las rutinas attach/detach. Todas las demás responsabilidades se han trasladado a otro lugar:

```c
/*
 * myfirst.c - module glue and cdev wiring for the myfirst driver.
 *
 * This file owns the cdevsw table, the devclass, the attach and
 * detach routines, and the MODULE_VERSION declaration.  The cdev
 * callbacks themselves live in myfirst_cdev.c.  The ioctl dispatch
 * lives in myfirst_ioctl.c.  The sysctl tree lives in
 * myfirst_sysctl.c.  The rate-limit infrastructure lives in
 * myfirst_log.c.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>

#include "myfirst.h"
#include "myfirst_debug.h"
#include "myfirst_ioctl.h"

MODULE_VERSION(myfirst, 18);

extern d_open_t    myfirst_open;
extern d_close_t   myfirst_close;
extern d_read_t    myfirst_read;
extern d_write_t   myfirst_write;

struct cdevsw myfirst_cdevsw = {
	.d_version = D_VERSION,
	.d_name    = "myfirst",
	.d_open    = myfirst_open,
	.d_close   = myfirst_close,
	.d_read    = myfirst_read,
	.d_write   = myfirst_write,
	.d_ioctl   = myfirst_ioctl,
};

static int
myfirst_attach(device_t dev)
{
	/* Section 5's labelled-cleanup attach goes here. */
	...
}

static int
myfirst_detach(device_t dev)
{
	/* Section 5's mirror-of-attach detach goes here. */
	...
}

static device_method_t myfirst_methods[] = {
	DEVMETHOD(device_probe,   myfirst_probe),
	DEVMETHOD(device_attach,  myfirst_attach),
	DEVMETHOD(device_detach,  myfirst_detach),
	DEVMETHOD_END
};

static driver_t myfirst_driver = {
	"myfirst",
	myfirst_methods,
	sizeof(struct myfirst_softc),
};

DRIVER_MODULE(myfirst, nexus, myfirst_driver, 0, 0);
```

El archivo tiene un único trabajo: conectar las piezas del driver a nivel del kernel. Tiene unos pocos cientos de líneas; todos los demás archivos del driver son más pequeños.

### `myfirst_cdev.c`: los callbacks del dispositivo de caracteres

Los callbacks de open, close, read y write fueron el primer código que escribimos en el Capítulo 18. Han crecido desde entonces. Extraerlos a `myfirst_cdev.c` los mantiene juntos y fuera de `myfirst.c`:

```c
/*
 * myfirst_cdev.c - character-device callbacks for the myfirst driver.
 *
 * The open/close/read/write callbacks all operate on the softc that
 * make_dev_s installed as si_drv1.  The ioctl dispatch is in
 * myfirst_ioctl.c; this file intentionally does not handle ioctls.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include "myfirst.h"
#include "myfirst_debug.h"

int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct myfirst_softc *sc = dev->si_drv1;

	mtx_lock(&sc->sc_mtx);
	sc->sc_open_count++;
	mtx_unlock(&sc->sc_mtx);

	DPRINTF(sc, MYF_DBG_OPEN, "open: count %u\n", sc->sc_open_count);
	return (0);
}

/* close, read, write follow the same pattern. */
```

Cada callback comienza con `sc = dev->si_drv1` (el puntero por cdev que estableció `make_dev_args`) y opera sobre el softc. No hay acoplamiento entre archivos más allá de la cabecera pública.

### `myfirst_ioctl.c`: el switch de comandos

`myfirst_ioctl.c` ha estado en su propio archivo desde el Capítulo 22. La adición del Capítulo 25 es el manejador de `MYFIRSTIOC_GETCAPS`:

```c
int
myfirst_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag,
    struct thread *td)
{
	struct myfirst_softc *sc = dev->si_drv1;
	int error = 0;

	switch (cmd) {
	case MYFIRSTIOC_GETVER:
		*(int *)data = MYFIRST_IOCTL_VERSION;
		break;
	case MYFIRSTIOC_RESET:
		mtx_lock(&sc->sc_mtx);
		sc->sc_total_reads  = 0;
		sc->sc_total_writes = 0;
		mtx_unlock(&sc->sc_mtx);
		break;
	case MYFIRSTIOC_GETMSG:
		mtx_lock(&sc->sc_mtx);
		strlcpy((char *)data, sc->sc_msg, MYFIRST_MSG_MAX);
		mtx_unlock(&sc->sc_mtx);
		break;
	case MYFIRSTIOC_SETMSG:
		mtx_lock(&sc->sc_mtx);
		strlcpy(sc->sc_msg, (const char *)data, MYFIRST_MSG_MAX);
		sc->sc_msglen = strlen(sc->sc_msg);
		mtx_unlock(&sc->sc_mtx);
		break;
	case MYFIRSTIOC_GETCAPS:
		*(uint32_t *)data = MYF_CAP_RESET | MYF_CAP_GETMSG |
		                    MYF_CAP_SETMSG;
		break;
	default:
		error = ENOIOCTL;
		break;
	}
	return (error);
}
```

El switch es toda la superficie ioctl pública del driver. Añadir un comando significa añadir un case; retirar uno significa eliminar un case y deprecar la constante en `myfirst_ioctl.h`.

### `myfirst_log.h` y `myfirst_log.c`: logging con limitación de tasa

La Sección 1 introdujo la macro de logging con limitación de tasa `DLOG_RL` y el estado `struct myfirst_ratelimit` que rastrea. El estado de limitación de tasa quedó embebido en el softc en la Sección 1 porque la abstracción aún no se había separado. La Sección 6 es el momento adecuado para separarla: el código de limitación de tasa es lo suficientemente pequeño como para merecer estar recogido en un único lugar, y lo suficientemente general como para que otros drivers puedan quererlo.

`myfirst_log.h` contiene la definición del estado:

```c
#ifndef _MYFIRST_LOG_H_
#define _MYFIRST_LOG_H_

#include <sys/time.h>

struct myfirst_ratelimit {
	struct timeval rl_lasttime;
	int            rl_curpps;
};

#define MYF_RL_DEFAULT_PPS  10

#endif /* _MYFIRST_LOG_H_ */
```

`myfirst_log.c` contiene los helpers de attach y detach:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include "myfirst.h"
#include "myfirst_debug.h"

int
myfirst_log_attach(struct myfirst_softc *sc)
{
	/*
	 * The rate-limit state is embedded by value in the softc, so
	 * there is no allocation to do.  This function exists so that
	 * the attach chain has a named label for logging in case a
	 * future version needs per-class allocations.
	 */
	bzero(&sc->sc_rl_generic, sizeof(sc->sc_rl_generic));
	bzero(&sc->sc_rl_io,      sizeof(sc->sc_rl_io));
	bzero(&sc->sc_rl_intr,    sizeof(sc->sc_rl_intr));

	return (0);
}

void
myfirst_log_detach(struct myfirst_softc *sc)
{
	/* Nothing to release; the state is embedded in the softc. */
	(void)sc;
}
```

Hoy `myfirst_log_attach` no realiza ninguna asignación; pone a cero los campos de limitación de tasa y retorna. Mañana, si el driver necesita un array dinámico de contadores por clase, la asignación encaja aquí y la cadena attach no tiene que cambiar. Este es el valor de extraer el helper antes de que sea estrictamente necesario: la estructura está lista para crecer.

El tamaño del archivo de cabecera importa aquí. `myfirst_log.h` tiene menos de 20 líneas. Una cabecera de 20 líneas es barata de incluir en cualquier lugar, barata de leer y barata de mantener sincronizada. Si `myfirst_log.h` creciese hasta las 200 líneas, el coste de incluirla en cada archivo `.c` empezaría a notarse en los tiempos de compilación y en la fricción durante la revisión; en ese punto, el siguiente paso es dividirla de nuevo.

### El Makefile actualizado

El Makefile del driver dividido lista todos los archivos `.c`:

```makefile
# Makefile for the myfirst driver - Chapter 25 (1.8-maintenance).
#
# Chapter 25 splits the driver into subject-matter files.  Each file
# answers a single question; the Makefile lists them in alphabetical
# order after myfirst.c (which carries the module glue) so the
# reader sees the main file first.

KMOD=	myfirst
SRCS=	myfirst.c myfirst_cdev.c myfirst_debug.c myfirst_ioctl.c \
	myfirst_log.c myfirst_sysctl.c

CFLAGS+=	-I${.CURDIR}

SYSDIR?=	/usr/src/sys

.include <bsd.kmod.mk>
```

`SRCS` lista seis archivos `.c`, uno por tema. Añadir un séptimo es un cambio de una sola línea. El sistema de construcción del kernel recoge automáticamente todos los archivos de `SRCS`; no hay ningún paso de enlace manual ni ningún árbol de dependencias del Makefile que mantener.

### Dónde trazar el límite de cada archivo

La parte más difícil de dividir un driver no es decidir dividirlo; es decidir dónde van los límites. La mayoría de las divisiones pasan por tres fases, y estas fases se aplican a cualquier driver, no solo a `myfirst`.

La **fase uno** es el archivo plano. Todo está en `driver.c`. Esta es la forma correcta para las primeras 300 líneas de un driver. Dividir antes crea más fricción de la que ahorra.

La **fase dos** es la división por tema. El despacho ioctl va a `driver_ioctl.c`, el árbol sysctl va a `driver_sysctl.c`, la infraestructura de depuración va a `driver_debug.c`. Cada archivo recibe el nombre del tema que gestiona. Aquí es donde ha estado `myfirst` desde el Capítulo 24.

La **fase tres** es la división por subsistema. A medida que el driver crece, un tema se hace más grande que un solo archivo. El archivo ioctl se divide en `driver_ioctl.c` (el despacho) y `driver_ioctl_rw.c` (los helpers de carga útil de lectura/escritura). El archivo sysctl se divide de forma similar. Aquí es donde acaba un driver completo, a menudo en su tercera o cuarta versión mayor.

`myfirst` al final del Capítulo 25 se encuentra sólidamente en la fase dos. La fase tres no está justificada todavía, y el Capítulo 26 reiniciará el reloj cuando divida el pseudo-driver en una variante conectada por USB y deje `myfirst_core.c` como el núcleo agnóstico al dispositivo. Anticipar la división a la fase tres hoy no aporta ningún valor.

La regla general para decidir cuándo pasar de la fase dos a la fase tres es la siguiente: cuando un único archivo de sujeto supera las 1.000 líneas, o cuando dos cambios no relacionados en el mismo archivo de sujeto provocan un conflicto de merge, el sujeto está listo para dividirse.

### El grafo de inclusión y el orden de construcción

Una vez que un driver se divide en varios archivos, el grafo de inclusión cobra importancia. Una inclusión circular no es un error grave en C, pero es señal de una estructura de dependencias confusa que desorientará a quien lea el código. La forma correcta es un grafo acíclico dirigido de cabeceras, con raíz en `myfirst.h` y hojas como `myfirst_ioctl.h` y `myfirst_log.h`.

`myfirst.h` es la cabecera más amplia. Declara el softc y los prototipos que usan todos los demás archivos. Incluye `myfirst_log.h` porque el softc tiene campos de limitación de tasa por valor.

`myfirst_debug.h` es una hoja. Declara la familia de macros `DPRINTF` y los bits de clase. Es incluida por todos los archivos `.c`, directa o indirectamente. No es incluida por `myfirst.h`, porque `myfirst.h` no debe imponer las macros de depuración a ningún consumidor que no las necesite.

`myfirst_ioctl.h` es una hoja. Declara los números de comando, las estructuras de payload y el entero de versión del formato de datos. Es incluida por `myfirst_ioctl.c` (y su contraparte en espacio de usuario `myfirstctl.c`).

Ninguna cabecera incluye otro archivo que no sean las cabeceras públicas del kernel y las propias del driver. Ningún archivo `.c` incluye otro archivo `.c`. El grafo de inclusión es superficial y fácil de representar.

### El coste de la división

Dividir archivos tiene un coste real. Cada división añade una cabecera, y cada cabecera hay que mantenerla. Una función cuya firma cambia hay que actualizarla tanto en el `.c` como en el `.h`, y el cambio tiene que propagarse a todos los demás archivos `.c` que incluyan la cabecera. Un driver con doce archivos se compila algo más lentamente que uno con un único archivo, porque cada `.c` debe incluir varias cabeceras que el preprocesador ha de analizar.

Estos costes son reales, pero pequeños. Son mucho menores que el coste de un archivo monolítico que nadie quiere tocar. La regla es dividir cuando el coste de no hacerlo supera el coste de mantener la frontera. Para `myfirst` al final del capítulo 25, ese umbral ya se ha superado.

### Un procedimiento práctico para dividir un driver real

Dividir un archivo es una refactorización de rutina, pero incluso una refactorización de rutina puede introducir errores si se hace con descuido. Un procedimiento práctico para dividir un driver del árbol de código fuente es el siguiente:

1. **Identifica los temas.** Lee el archivo monolítico de arriba abajo y agrupa sus funciones por tema (cdev, ioctl, sysctl, depuración, ciclo de vida). Escribe la agrupación en un papel o en un bloque de comentarios.

2. **Crea los archivos vacíos.** Añade los nuevos archivos `.c` y sus cabeceras al árbol de código fuente. Compila una vez para asegurarte de que el sistema de construcción los detecta.

3. **Mueve un tema cada vez.** Mueve las funciones ioctl a `driver_ioctl.c`. Mueve sus declaraciones a `driver_ioctl.h`. Actualiza `driver.c` para que incluya `#include "driver_ioctl.h"`. Compila. Ejecuta el driver en su matriz de pruebas.

4. **Haz un commit por cada tema.** Cada movimiento de tema es un único commit. El mensaje del commit dice: "myfirst: split ioctl dispatch into driver_ioctl.c". Un revisor puede ver el movimiento con claridad; un `git blame` muestra la misma línea en el nuevo archivo en el mismo commit.

5. **Verifica el grafo de inclusión.** Cuando todos los temas estén movidos, compila con `-Wunused-variable` y `-Wmissing-prototypes` para detectar funciones que deberían tener prototipos pero no los tienen. Usa `nm` sobre el módulo compilado para confirmar que ningún símbolo que deba ser `static` se está exportando.

6. **Vuelve a probar.** Ejecuta la matriz de pruebas completa del driver. Dividir un archivo no debería cambiar el comportamiento; si una prueba empieza a fallar, la división ha introducido un error.

### Errores comunes al dividir

Algunos errores son habituales la primera vez que se divide un driver. Conocerlos de antemano acorta la curva de aprendizaje.

El primer error es **poner declaraciones en la cabecera equivocada**. Si `myfirst.h` declara una función que solo se llama desde `myfirst_ioctl.c`, cada unidad de traducción paga el coste de analizar una declaración que no necesita. Si `myfirst_ioctl.h` declara una función que se llama tanto desde `myfirst_ioctl.c` como desde `myfirst_cdev.c`, los dos consumidores quedan acoplados a través de la cabecera ioctl, y cualquier cambio en esa cabecera recompila ambos archivos. La solución es colocar las declaraciones transversales en `myfirst.h` y las específicas de cada tema en las cabeceras de ese tema.

El segundo error es **olvidar `static` en funciones que deberían tener ámbito de archivo**. Una función que solo se usa dentro de `myfirst_sysctl.c` debería declararse `static`. Sin `static`, la función se exporta desde el archivo objeto, lo que significa que otro archivo podría llamarla accidentalmente, y cualquier renombramiento posterior en el archivo original se convierte en un cambio de ABI. Una disciplina de `static` evita toda esta clase de problemas.

El tercer error son las **inclusiones circulares**. Si `myfirst_ioctl.h` incluye `myfirst.h`, y `myfirst.h` incluye `myfirst_ioctl.h`, el driver compila (gracias a los guardas de inclusión) pero el grafo de dependencias es incorrecto. Cualquier edición en cualquiera de los dos archivos dispara una recompilación de todo lo que incluya a cualquiera de ellos. La solución es decidir qué cabecera ocupa un nivel superior en el grafo y eliminar la referencia inversa.

El cuarto error es **reintroducir un tema en el archivo equivocado**. Seis meses después de la división, alguien añade un nuevo ioctl editando `myfirst.c` porque ahí es donde vivían los ioctls. La regla de responsabilidad única tiene que ser impuesta por los revisores. Un parche que ponga un nuevo ioctl en `myfirst.c` es rechazado con un comentario que señala a `myfirst_ioctl.c`.

### Cerrando la sección 6

Un driver cuyos archivos responden cada uno a una sola pregunta es un driver que puedes entregarle a un nuevo colaborador el primer día. Lee los nombres de archivo, elige un tema y empieza a editar exactamente un archivo. El driver `myfirst` ha cruzado ese umbral. Seis archivos `.c` más sus cabeceras contienen todas las funciones que el driver ha acumulado desde el capítulo 17, con cada archivo nombrado por lo que hace.

En la próxima sección, pasamos de la organización interna a la preparación externa. Un driver listo para producción tiene una lista corta de propiedades que debe cumplir antes de poder instalarse en una máquina que el desarrollador no controla. La lista de verificación de preparación para producción del capítulo 25 nombra esas propiedades y lleva a `myfirst` a través de cada una.

## Sección 7: Preparación para el uso en producción

Un driver que funciona en tu estación de trabajo no es un driver listo para producción. La producción es el conjunto de condiciones a las que se enfrenta tu código cuando se instala en hardware que no posees, arrancado por operadores con los que nunca te encontrarás, y del que se espera un comportamiento predecible durante meses o años entre reinicios. La distancia entre «funciona en mi máquina» y «está listo para distribuirse» se mide en hábitos, no en funcionalidades. Esta sección nombra esos hábitos.

`myfirst` en su forma del capítulo 25 está tan completo en funcionalidades como va a llegar a estarlo el pseudodriver. El trabajo restante no es añadir funcionalidad, sino endurecer los extremos para que el driver sobreviva en los entornos que no puede controlar.

### La mentalidad de preparación para producción

El cambio de mentalidad es este: cada decisión que el driver toma implícitamente durante el desarrollo tiene que tomarse explícitamente en producción. Cuando un parámetro ajustable tiene un valor por defecto, ese valor tiene que ser el correcto. Cuando un sysctl es escribible, las consecuencias de que un operador asustado lo modifique a las 3 de la mañana tienen que ser seguras. Cuando un mensaje de registro puede dispararse, ese mensaje tiene que ser útil sin ayuda del desarrollador. Cuando un módulo depende de otro, la dependencia tiene que declararse para que el cargador no los cargue en el orden equivocado.

La preparación para producción no es una acción puntual; es una actitud que impregna cada decisión. Un driver casi listo para producción suele tener uno o dos huecos concretos: un parámetro ajustable sin documentación, un mensaje de registro que se dispara cada microsegundo, una ruta de detach que asume que nadie está usando el dispositivo. La disciplina de la preparación para producción consiste en encontrar esos huecos concretos y cerrarlos, uno a uno, hasta que el comportamiento del driver sea predecible en una máquina frente a la que el desarrollador no está.

### Declaración de dependencias de módulo

El primer hábito de producción es ser explícito sobre lo que el módulo necesita. Si `myfirst` llama a una función que vive en otro módulo del kernel, el cargador de módulos del kernel necesita conocer la dependencia antes de la llamada, o bien el kernel carga `myfirst` y entra en pánico la primera vez que se usa la dependencia.

El mecanismo es `MODULE_DEPEND`. La sección 4 lo presentó como una herramienta de compatibilidad; en producción, también es una herramienta de corrección. Un driver sin `MODULE_DEPEND` en sus dependencias reales funciona por casualidad en la mayoría de los órdenes de arranque y falla misteriosamente en otros. Un driver con `MODULE_DEPEND` en cada dependencia real o bien se carga correctamente o bien se niega a cargarse con un mensaje de error claro.

Para el pseudodriver `myfirst`, todavía no hay dependencias reales; el driver solo usa símbolos del núcleo del kernel, que siempre está presente. La variante USB del capítulo 26 añadirá el primer `MODULE_DEPEND` real:

```c
MODULE_DEPEND(myfirst_usb, usb, 1, 1, 1);
```

Los tres números de versión son el mínimo, el preferido y el máximo de la versión del stack USB con la que `myfirst_usb` es compatible. En el momento de la carga, el kernel compara la versión del stack USB instalado con este rango y se niega a cargar `myfirst_usb` si el stack USB falta o está fuera del rango.

El hábito de producción es: antes de distribuir, busca con grep en el driver todos los símbolos que llama y confirma que cada símbolo vive en el núcleo del kernel o en un módulo cuya dependencia declara el driver. Un `MODULE_DEPEND` que falta funciona hasta que el orden de arranque cambia, y entonces el driver entra en pánico en el hardware de producción.

### Publicación de información PNP

Para los drivers de hardware, el cargador de módulos del kernel consulta los metadatos PNP de cada módulo para decidir qué driver gestiona qué dispositivo. Un driver USB que no publica información PNP funciona cuando se carga manualmente y falla cuando el boot loader intenta cargarlo automáticamente para un dispositivo recién conectado. La solución es `MODULE_PNP_INFO`, que el driver usa para declarar los identificadores de fabricante y producto que gestiona:

```c
MODULE_PNP_INFO("U16:vendor;U16:product", uhub, myfirst_usb,
    myfirst_pnp_table, nitems(myfirst_pnp_table));
```

La primera cadena describe el formato de las entradas de la tabla PNP. `uhub` es el nombre del bus; `myfirst_usb` es el nombre del driver; `myfirst_pnp_table` es un array static de estructuras, una por cada dispositivo que gestiona el driver.

`myfirst` en el capítulo 25 sigue siendo un pseudodriver y no tiene hardware que identificar. `MODULE_PNP_INFO` entra en juego en el capítulo 26 con el primer attach de hardware real. En el capítulo 25, el hábito de producción es simplemente saber que la macro existe y planificar su uso cuando llegue el hardware.

### El evento `MOD_QUIESCE`

El manejador de eventos del módulo del kernel se invoca con uno de cuatro eventos: `MOD_LOAD`, `MOD_UNLOAD`, `MOD_SHUTDOWN`, `MOD_QUIESCE`. La mayoría de los drivers gestionan `MOD_LOAD` y `MOD_UNLOAD` explícitamente, y el kernel sintetiza los otros dos. Para los drivers de producción, `MOD_QUIESCE` merece atención.

`MOD_QUIESCE` es la pregunta del kernel: «¿puedes descargarte ahora mismo?». Se dispara antes de `MOD_UNLOAD` y le da al driver la oportunidad de negarse limpiamente. Un driver que está en mitad de una operación (una transferencia DMA en curso, un descriptor de archivo abierto, un temporizador pendiente) puede devolver un errno distinto de cero desde `MOD_QUIESCE` para rechazar la descarga; el kernel entonces no procede a `MOD_UNLOAD`.

Para `myfirst`, la comprobación de quiesce ya está incorporada en `myfirst_detach`: si `sc_open_count > 0`, detach devuelve `EBUSY`. El cargador de módulos del kernel propaga ese `EBUSY` de vuelta a `kldunload(8)`, y el operador ve «module myfirst is busy». La comprobación está en el lugar correcto, pero vale la pena nombrar la disciplina de pensar en `MOD_QUIESCE` por separado de `MOD_UNLOAD`: `MOD_QUIESCE` es la pregunta «¿es seguro descargarte?», y `MOD_UNLOAD` es la orden «adelante, descárgate». Algunos drivers tienen estado que es seguro comprobar en `MOD_QUIESCE` pero no es seguro adquirir en `MOD_UNLOAD`; separarlos permite al driver responder la pregunta sin efectos secundarios.

### Emisión de eventos `devctl_notify`

Los sistemas de producción de larga duración están monitorizados por daemons como `devd(8)`, que vigilan la llegada, la salida y los cambios de estado de los dispositivos. El mecanismo que utiliza el kernel para notificar a `devd` es `devctl_notify(9)`: el driver emite un evento estructurado, `devd` lo lee y toma la acción configurada (ejecutar un script, registrar un mensaje, notificar a un operador).

El prototipo es:

```c
void devctl_notify(const char *system, const char *subsystem,
    const char *type, const char *data);
```

- `system` es una categoría de nivel superior como `"DEVFS"`, `"ACPI"`, o una etiqueta específica del driver.
- `subsystem` es el nombre del driver o subsistema.
- `type` es un nombre de evento breve.
- `data` son datos estructurados opcionales (pares clave=valor) para que el daemon los analice.

Para `myfirst`, un evento útil en producción es "el mensaje interno del driver ha sido reescrito":

```c
devctl_notify("myfirst", device_get_nameunit(sc->sc_dev),
    "MSG_CHANGED", NULL);
```

Después de que el operador escriba un nuevo mensaje mediante `ioctl(fd, MYFIRSTIOC_SETMSG, buf)`, el driver emite un evento `MSG_CHANGED`. Una regla de `devd` puede detectar el evento y, por ejemplo, enviar una entrada a syslog o notificar a un daemon de monitorización:

```text
notify 0 {
    match "system"    "myfirst";
    match "type"      "MSG_CHANGED";
    action "logger -t myfirst 'message changed on $subsystem'";
};
```

El hábito de producción aquí consiste en preguntarse, para cada evento relevante del driver, si un operador podría querer reaccionar ante él. Si la respuesta es afirmativa, emite un `devctl_notify` con un nombre bien elegido. Las herramientas externas podrán entonces apoyarse en ese evento sin que el driver tenga que saber cuáles son.

### Cómo escribir un `MAINTENANCE.md`

Todo driver de producción debería tener un archivo de mantenimiento que describa, en lenguaje llano, qué hace el driver, qué tunables acepta, qué sysctls expone, qué ioctls gestiona, qué eventos emite y cuál es su historial de versiones. El archivo vive junto al código fuente en el repositorio; lo leen los operadores, los nuevos desarrolladores, los revisores de seguridad y el propio autor seis meses después.

Un esqueleto concreto para `MAINTENANCE.md`:

```text
# myfirst

A demonstration character driver that carries the book's running
example.  This file is the operator-facing reference.

## Overview

myfirst registers a pseudo-device at /dev/myfirst0 and serves a
read-write message buffer, a set of ioctls, a sysctl tree, and a
configurable debug-class logger.

## Tunables

- hw.myfirst.debug_mask_default (int, default 0)
    Initial value of dev.myfirst.<unit>.debug.mask.
- hw.myfirst.timeout_sec (int, default 5)
    Initial value of dev.myfirst.<unit>.timeout_sec.
- hw.myfirst.max_retries (int, default 3)
    Initial value of dev.myfirst.<unit>.max_retries.
- hw.myfirst.log_ratelimit_pps (int, default 10)
    Initial rate-limit ceiling (prints per second per class).

## Sysctls

All sysctls live under dev.myfirst.<unit>.

Read-only: version, open_count, total_reads, total_writes,
message, message_len.

Read-write: debug.mask (mirror of debug_mask_default), timeout_sec,
max_retries, log_ratelimit_pps.

## Ioctls

Defined in myfirst_ioctl.h.  Command magic 'M'.

- MYFIRSTIOC_GETVER (0): returns MYFIRST_IOCTL_VERSION.
- MYFIRSTIOC_RESET  (1): zeros read/write counters.
- MYFIRSTIOC_GETMSG (2): reads the in-driver message.
- MYFIRSTIOC_SETMSG (3): writes the in-driver message.
- MYFIRSTIOC_GETCAPS (5): returns MYF_CAP_* bitmask.

Command 4 was reserved during Chapter 23 draft work and retired
before release.  Do not reuse the number.

## Events

Emitted through devctl_notify(9).

- system=myfirst subsystem=<unit> type=MSG_CHANGED
    The operator-visible message was rewritten.

## Version History

See Change Log below.

## Change Log

### 1.8-maintenance
- Added MYFIRSTIOC_GETCAPS (command 5).
- Added tunables for timeout_sec, max_retries, log_ratelimit_pps.
- Added rate-limited logging via ppsratecheck(9).
- Added devctl_notify for MSG_CHANGED.
- No breaking changes from 1.7.

### 1.7-integration
- First end-to-end integration of ioctl, sysctl, debug.
- Introduced MYFIRSTIOC_{GETVER,RESET,GETMSG,SETMSG}.

### 1.6-debug
- Added DPRINTF framework and SDT probes.
```

El archivo no tiene nada de glamuroso. Es una referencia que se mantiene actualizada con cada cambio de versión y que sirve como fuente única de verdad para los operadores.

El hábito de producción es: todo cambio en la superficie visible del driver (un nuevo tunable, un nuevo sysctl, un nuevo ioctl, un nuevo evento, un cambio de comportamiento) tiene su entrada correspondiente en `MAINTENANCE.md`. El archivo nunca queda por detrás del código. Un driver cuyo `MAINTENANCE.md` está desactualizado es un driver cuyos usuarios están adivinando; un driver cuyo `MAINTENANCE.md` está al día es un driver cuyos usuarios pueden consultar por sí mismos.

### Un conjunto de reglas para `devd`

Las reglas de `devd(8)` indican al demonio cómo reaccionar ante los eventos del kernel. Para un despliegue en producción de `myfirst`, un conjunto mínimo de reglas garantizaría que los eventos importantes lleguen al operador:

```console
# /etc/devd/myfirst.conf
#
# devd rules for the myfirst driver.  Drop this file into
# /etc/devd/ and restart devd(8) for the rules to take effect.

notify 0 {
    match "system"    "myfirst";
    match "type"      "MSG_CHANGED";
    action "logger -t myfirst 'message changed on $subsystem'";
};

# Future: match attach/detach events once Chapter 26's USB variant
# starts emitting them.
```

El archivo es breve. Declara una regla, coincide con un evento específico y realiza una acción concreta. En producción, estos archivos crecen para coincidir con más eventos, disparar más acciones y, en algunos despliegues, notificar a un sistema de monitorización que vigila las anomalías del driver.

Incluir un borrador de `devd.conf` en el repositorio del driver facilita su adopción por parte del operador. Copia el archivo, ajusta las acciones y los eventos del driver quedan integrados en la monitorización del entorno desde el primer día.

### Los logs: aliado del técnico de soporte

Los mensajes de log de un driver de producción los leen técnicos de soporte que no tienen acceso al código fuente y no pueden reproducir el problema bajo demanda. Las reglas que hacen útil un mensaje de log para un técnico de soporte son distintas de las que lo hacen útil para un desarrollador.

Un desarrollador que lee su propio mensaje de log puede apoyarse en un contexto que el técnico de soporte no tiene. El técnico no puede preguntar «¿qué attach?» o «¿qué dispositivo?» o «¿cuál era `error` cuando se disparó esto?». La respuesta tiene que estar ya en el propio mensaje.

El hábito de producción consiste en revisar cada mensaje de log del driver y hacerse tres preguntas:

1. **¿Nombra el mensaje a su dispositivo?** `device_printf(dev, ...)` antepone al mensaje el nameunit del dispositivo; un `printf` simple no lo hace. Todo mensaje que no provenga de `MOD_LOAD` (donde todavía no hay dispositivo) debería usar `device_printf`.

2. **¿Incluye el mensaje el contexto numérico relevante?** «Failed to allocate» no resulta útil. «Failed to allocate: error 12 (ENOMEM)» sí lo es. «Failed to allocate a timer: error 12» es aún mejor.

3. **¿Aparece el mensaje a la frecuencia adecuada?** La sección 1 trató la limitación de velocidad. El paso final consiste en comprobar que todo mensaje que pueda dispararse en un bucle sea o bien rate-limited o bien manifiestamente de única emisión.

Un mensaje de log que satisface estas tres preguntas llega al técnico de soporte con información suficiente para abrir un informe de error útil. Un mensaje de log que falla en alguna de ellas hace perder el tiempo al operador y al desarrollador.

### Cómo gestionar attach y detach del bus con elegancia

Los drivers de producción, especialmente los que admiten conexión en caliente, tienen que gestionar ciclos repetidos de attach y detach sin fugas. La disciplina del patrón de limpieza con etiquetas de la sección 5 forma parte de la respuesta; la otra parte consiste en confirmar que los ciclos repetidos de attach/detach funcionan correctamente. El laboratorio al final de este capítulo recorre un script de regresión que carga, descarga y recarga el driver 100 veces seguidas y verifica que el consumo de memoria del módulo no crece.

Un driver que supera el test de 100 ciclos es un driver que sobrevivirá un mes de eventos de conexión en caliente en hardware de producción. Un driver que falla el test de 100 ciclos tiene una fuga que se manifestará, con el tiempo, como un lento crecimiento de la memoria o como el agotamiento de algún recurso acotado del kernel (OIDs de sysctl, números menores de cdev, entradas de devclass).

El test es sencillo de ejecutar y tiene un valor desproporcionado. Inclúyelo en la lista de comprobación previa a la publicación del driver.

### Cómo gestionar acciones inesperadas del operador

Los operadores cometen errores. Ejecutan `kldunload myfirst` mientras un programa de prueba está leyendo de `/dev/myfirst0`. Establecen `dev.myfirst.0.debug.mask` a un valor que habilita todas las clases a la vez. Copian `MAINTENANCE.md` y se saltan la sección sobre tunables. El driver de producción tiene que tolerar estas acciones sin bloquearse, sin corromper el estado ni dejar el sistema en una configuración rota.

Para cada interfaz expuesta, el hábito de producción consiste en preguntarse: ¿cuál es la peor secuencia de acciones del operador que puedo imaginar y el driver la sobrevive?

- `kldunload` con un descriptor de archivo abierto: `myfirst_detach` devuelve `EBUSY`. El operador ve «module busy». El driver no cambia.
- Un sysctl con escritura habilitada al que se le asigna un valor fuera de rango: el manejador del sysctl ajusta el valor o devuelve `EINVAL`. El estado interno del driver no cambia.
- Un `MYFIRSTIOC_SETMSG` con un mensaje más largo que el buffer: `strlcpy` lo trunca. La copia es correcta; el truncado es visible en `message_len`.
- Un par de llamadas `MYFIRSTIOC_SETMSG` concurrentes: el mutex de softc las serializa. Gana la que ejecuta en segundo lugar; ambas tienen éxito.

Si alguna de estas acciones produce un bloqueo, una corrupción o un estado inconsistente, el driver no está listo para producción. La corrección es siempre la misma: añadir la guarda que falta, reiniciar el test y añadir un comentario que documente el invariante.

### Una lista de comprobación de preparación para producción

Los hábitos de esta sección caben en una lista breve que un desarrollador puede recorrer antes de publicar:

```text
myfirst production readiness
----------------------------

[  ] MODULE_DEPEND declared for every real dependency.
[  ] MODULE_PNP_INFO declared if the driver binds to hardware.
[  ] MOD_QUIESCE answers "can you unload?" without side effects.
[  ] devctl_notify emitted for operator-relevant events.
[  ] MAINTENANCE.md current: tunables, sysctls, ioctls, events.
[  ] devd.conf snippet included with the driver.
[  ] Every log message is device_printf, includes errno,
     and is rate-limited if it can fire in a loop.
[  ] attach/detach survives 100 load/unload cycles.
[  ] sysctls reject out-of-range values.
[  ] ioctl payload is bounds-checked.
[  ] Failure paths exercised via deliberate injection.
[  ] Versioning discipline: MYFIRST_VERSION, MODULE_VERSION,
     MYFIRST_IOCTL_VERSION each bumped for their own reason.
```

La lista es breve a propósito. Doce elementos, la mayoría ya cubiertos por los hábitos introducidos en secciones anteriores. Un driver que marca todas las casillas está listo para ser instalado por alguien que nunca te conocerá.

### Qué cubre el driver `myfirst`

Pasar `myfirst` por la lista de comprobación al final del capítulo 25 arroja el siguiente estado.

`MODULE_DEPEND` no es necesario porque el driver no tiene dependencias de subsistema; esto se indica explícitamente en `MAINTENANCE.md`.

`MODULE_PNP_INFO` no es necesario porque el driver no se vincula a hardware; esto también se indica en `MAINTENANCE.md`.

`MOD_QUIESCE` queda cubierto por la comprobación de `sc_open_count` en `myfirst_detach`; no se añade un manejador dedicado a `MOD_QUIESCE` en esta versión porque la semántica es idéntica.

`devctl_notify` se emite en `MYFIRSTIOC_SETMSG` con el tipo de evento `MSG_CHANGED`.

`MAINTENANCE.md` se distribuye en el directorio de ejemplos y contiene tunables, sysctls, ioctls, eventos y una entrada de Change Log para 1.8-maintenance.

El fragmento de `devd.conf` se distribuye junto a `MAINTENANCE.md` y demuestra la única regla de `MSG_CHANGED`.

Todos los mensajes de log se emiten a través de `device_printf` (o `DPRINTF`, que envuelve a `device_printf`); todo mensaje que se dispara en una ruta caliente está envuelto en `DLOG_RL`.

El script de regresión de attach/detach (véase el apartado de laboratorios) ejecuta 100 ciclos sin aumentar el consumo de memoria del kernel.

Los sysctls para `timeout_sec`, `max_retries` y `log_ratelimit_pps` rechazan cada uno los valores fuera de rango en sus manejadores.

Las cargas útiles de ioctl se comprueban en cuanto a límites a nivel de estructura por el framework de ioctl del kernel (`_IOR`, `_IOW`, `_IOWR` declaran tamaños exactos) y dentro del driver allí donde importa la longitud de la cadena.

Los puntos de inyección de fallos están marcados con `#ifdef` condicional en los ejemplos; cada etiqueta ha sido alcanzada al menos una vez durante el desarrollo.

Los identificadores de versión tienen cada uno su propia regla: la cadena se incrementa, el entero del módulo se incrementa, el entero de ioctl no cambia porque las adiciones son compatibles hacia atrás.

Doce comprobaciones, doce resultados. El driver está listo para el siguiente capítulo.

### Cerrando la sección 7

La producción es el estándar silencioso que separa el código interesante del código publicable. Las disciplinas descritas aquí no son glamurosas; son las cosas concretas que mantienen un driver funcionando cuando se despliega lejos del desarrollador que lo escribió. `myfirst` ha crecido a lo largo de cinco capítulos de contenido didáctico y ahora lleva el arnés que le permite sobrevivir fuera del libro.

En la siguiente sección, nos volcamos en las dos infraestructuras del kernel que permiten a un driver ejecutar código en puntos específicos del ciclo de vida sin necesidad de conexión manual: `SYSINIT(9)` para la inicialización en el boot y `EVENTHANDLER(9)` para las notificaciones en tiempo de ejecución. Estas son las dos últimas piezas del kit de herramientas de FreeBSD que el libro presentará antes de que el capítulo 26 aplique todo a un bus real.

## Sección 8: SYSINIT, SYSUNINIT y EVENTHANDLER

Las rutinas attach y detach de un driver gestionan todo lo que ocurre entre la instanciación y el desmontaje, pero hay cosas que un driver puede necesitar hacer que quedan fuera de esa ventana. Parte del código tiene que ejecutarse antes de que se instancie cualquier dispositivo: cargar tunables del boot, inicializar un lock de subsistema, configurar un pool que el primer `attach` consumirá. Otro código tiene que ejecutarse en respuesta a eventos del sistema que no son específicos de un dispositivo: una suspensión global, una condición de poca memoria, un apagado que está a punto de sincronizar los sistemas de archivos y cortar la alimentación.

El kernel de FreeBSD proporciona dos mecanismos para estos casos. `SYSINIT(9)` registra una función para que se ejecute en una etapa específica del boot, y su compañero `SYSUNINIT(9)` registra una función de limpieza para que se ejecute al descargar el módulo. `EVENTHANDLER(9)` registra un callback para que se ejecute cada vez que el kernel lanza un evento con nombre.

Ambos mecanismos están disponibles desde las primeras versiones de FreeBSD. Son infraestructura aburrida; ese es su valor. Un driver que los usa correctamente puede reaccionar al ciclo de vida completo del kernel sin escribir una sola línea de código de registro manual. Un driver que los ignora o bien pierde su momento o bien reinventa una versión peor de la misma funcionalidad.

### Por qué el kernel necesita un orden de arranque

El kernel de FreeBSD arranca en un orden preciso. La gestión de memoria se activa antes de que cualquier asignador sea utilizable. Los tunables se analizan antes de que los drivers puedan leerlos. Los locks se inicializan antes de que nada pueda adquirirlos. Los sistemas de archivos se montan solo después de que se hayan sondeado los dispositivos en los que residen. Cada una de estas dependencias debe respetarse, o el kernel entra en pánico antes de que `init(8)` arranque.

El mecanismo que impone el orden es `SYSINIT(9)`. Una macro `SYSINIT` declara que una función determinada debe ejecutarse en un ID de subsistema dado con una constante de orden dada. La secuencia de boot del kernel reúne cada `SYSINIT` de la configuración en ejecución, los ordena por (subsistema, orden) y los llama en esa secuencia. Los módulos cargados después de que el kernel haya arrancado siguen respetando sus declaraciones `SYSINIT`: el cargador los llama en el momento del attach del módulo, en el mismo orden.

Desde el punto de vista del driver, un `SYSINIT` es una forma de decir «haz esta cosa en ese punto de la secuencia de boot, y no me importa qué otro código también esté registrándose en ese punto». El kernel se encarga del orden; el driver escribe el callback.

### El espacio de IDs de subsistema

Los IDs de subsistema se definen en `/usr/src/sys/sys/kernel.h`. Las constantes tienen nombres descriptivos y valores numéricos que reflejan su orden. Un driver elige el subsistema que corresponde al propósito de su callback:

- `SI_SUB_TUNABLES` (0x0700000): evalúa los tunables del boot. Aquí es donde se ejecutan `TUNABLE_INT_FETCH` y sus variantes. El código que consume tunables debe ejecutarse después de este punto.
- `SI_SUB_KLD` (0x2000000): configuración de módulos del kernel cargables. La infraestructura temprana de módulos se ejecuta aquí.
- `SI_SUB_SMP` (0x2900000): activa los procesadores de aplicación.
- `SI_SUB_DRIVERS` (0x3100000): permite que los drivers se inicialicen. Es el subsistema con el que se registra la mayoría de los drivers de dispositivo cuando necesitan código temprano que se ejecute antes de que cualquier dispositivo haga attach.
- `SI_SUB_CONFIGURE` (0x3800000): configura los dispositivos. Al final de este subsistema, todo driver compilado en el kernel ha tenido la oportunidad de hacer attach.

Hay más de un centenar de identificadores de subsistema en `kernel.h`. Los mencionados anteriormente son los que un driver de dispositivo de caracteres utiliza con mayor frecuencia. Los valores numéricos están ordenados de forma que "número menor" significa "antes en el arranque".

Dentro de un subsistema, la constante de orden proporciona un control fino sobre la secuencia de ejecución:

- `SI_ORDER_FIRST` (0x0): se ejecuta antes que la mayor parte del código del mismo subsistema.
- `SI_ORDER_SECOND`, `SI_ORDER_THIRD`: ordenación explícita paso a paso.
- `SI_ORDER_MIDDLE` (0x1000000): se ejecuta en medio. La mayoría de los `SYSINIT` a nivel de driver utilizan este valor o el que se describe a continuación.
- `SI_ORDER_ANY` (0xfffffff): se ejecuta en último lugar. El kernel no garantiza ningún orden específico entre las entradas `SI_ORDER_ANY`.

El autor del driver elige el valor de orden más bajo que haga que el callback se ejecute después de sus prerrequisitos y antes de sus dependientes. En la mayoría de los casos, `SI_ORDER_MIDDLE` es la elección correcta.

### Cuándo necesita un driver `SYSINIT`

La mayoría de los drivers de dispositivos de caracteres no necesitan `SYSINIT` en absoluto. `DRIVER_MODULE` ya registra el driver con Newbus; el método `device_attach` del driver se ejecuta cuando aparece un dispositivo coincidente. Eso es suficiente para cualquier trabajo que sea por instancia.

`SYSINIT` es para trabajos que no son por instancia. Estas son algunas razones por las que un driver podría registrar un `SYSINIT`:

- **Inicializar un pool global** del que pueda disponer cada instancia del driver. El pool existe una sola vez; no pertenece a ningún softc en particular.
- **Registrarse en un subsistema del kernel** que espera que los llamantes se registren antes de usarlo. Por ejemplo, un driver que quiere recibir eventos `vm_lowmem` se registra pronto para que el primer evento de poca memoria no lo pase por alto.
- **Procesar un tunable complejo** que requiere más trabajo que un simple `TUNABLE_INT_FETCH`. El código de análisis del tunable se ejecuta durante `SI_SUB_TUNABLES` y rellena una estructura global que el código por instancia consulta más adelante.
- **Autocomprobar** una primitiva criptográfica o un inicializador de subsistema antes de que el primer llamante pueda usarlo.

En el caso de `myfirst`, nada de esto aplica por ahora. El driver es por instancia, sus tunables son simples y no utiliza ningún subsistema que requiera pre-registro. El Capítulo 25 presenta `SYSINIT` no porque `myfirst` lo necesite, sino para que el lector se familiarice con la macro y entienda cuándo un cambio futuro lo justificaría.

### La forma de una declaración `SYSINIT`

La firma de la macro es:

```c
SYSINIT(uniquifier, subsystem, order, func, ident);
```

- `uniquifier` es un identificador de C que vincula el símbolo `SYSINIT` a esta declaración. No aparece en ningún otro lugar. La convención es usar un nombre corto que coincida con el subsistema o la función.
- `subsystem` es la constante `SI_SUB_*`.
- `order` es la constante `SI_ORDER_*`.
- `func` es un puntero a función con la firma `void (*)(void *)`.
- `ident` es un único argumento que se pasa a `func`. En la mayoría de los casos, es `NULL`.

La macro de limpieza correspondiente es:

```c
SYSUNINIT(uniquifier, subsystem, order, func, ident);
```

`SYSUNINIT` registra una función de limpieza. Se ejecuta al descargar el módulo, en el orden inverso al de las declaraciones `SYSINIT`. Para el código compilado directamente en el kernel (no como módulo), `SYSUNINIT` nunca se dispara porque el kernel nunca se descarga; pero la declaración sigue siendo útil, ya que compilar el driver como módulo ejercita la ruta de limpieza.

### Un ejemplo resuelto de `SYSINIT` para `myfirst`

Considera una mejora hipotética de `myfirst`: un pool global, compartido por toda la vida del driver, de buffers de log preasignados del que cada instancia puede disponer. El pool se inicializa una sola vez al cargar el módulo y se destruye una sola vez al descargarlo. El attach y detach por instancia no tocan el pool directamente; solo toman y devuelven buffers de él.

La declaración `SYSINIT` tiene este aspecto:

```c
#include <sys/kernel.h>

static struct myfirst_log_pool {
	struct mtx       lp_mtx;
	/* ... per-pool state ... */
} myfirst_log_pool;

static void
myfirst_log_pool_init(void *unused __unused)
{
	mtx_init(&myfirst_log_pool.lp_mtx, "myfirst log pool",
	    NULL, MTX_DEF);
	/* Allocate pool entries. */
}

static void
myfirst_log_pool_fini(void *unused __unused)
{
	/* Release pool entries. */
	mtx_destroy(&myfirst_log_pool.lp_mtx);
}

SYSINIT(myfirst_log_pool,  SI_SUB_DRIVERS, SI_ORDER_MIDDLE,
    myfirst_log_pool_init, NULL);
SYSUNINIT(myfirst_log_pool, SI_SUB_DRIVERS, SI_ORDER_MIDDLE,
    myfirst_log_pool_fini, NULL);
```

Cuando se carga `myfirst`, el kernel ordena las entradas `SYSINIT` y llama a `myfirst_log_pool_init` durante la fase `SI_SUB_DRIVERS`. El primer `myfirst_attach` que se ejecuta a continuación encuentra el pool listo. Cuando se descarga el módulo, `myfirst_log_pool_fini` se ejecuta después de que todas las instancias hayan sido desconectadas, dando al pool la oportunidad de liberar sus recursos.

Esto es un esquema con fines didácticos; `myfirst` no utiliza realmente un pool global en el código del Capítulo 25 incluido en el libro. El lector que en algún momento escriba un driver que sí lo necesite encontrará el patrón aquí.

### El orden entre `SYSINIT` y `DRIVER_MODULE`

`DRIVER_MODULE` en sí mismo está implementado como un `SYSINIT` por debajo. Registra el driver con Newbus durante una fase específica del subsistema, y los propios `SYSINIT`s de Newbus se encargan de explorar y conectar dispositivos después. Un `SYSINIT` personalizado de un driver puede ordenarse en relación con `DRIVER_MODULE` eligiendo el subsistema y el orden correctos.

Una regla general:

- Un `SYSINIT` en `SI_SUB_DRIVERS` con `SI_ORDER_FIRST` se ejecuta antes del registro de `DRIVER_MODULE`.
- Un `SYSINIT` en `SI_SUB_CONFIGURE` con `SI_ORDER_MIDDLE` se ejecuta después de la mayoría de los attaches de dispositivos pero antes del paso final de configuración.

Para un pool global del que depende attach, `SI_SUB_DRIVERS` con `SI_ORDER_MIDDLE` suele ser lo correcto: el pool se inicializa antes de que los dispositivos de `DRIVER_MODULE` empiecen a conectarse (porque `SI_SUB_DRIVERS` es anterior a `SI_SUB_CONFIGURE`), y la constante de orden lo mantiene alejado de los hooks más tempranos.

### `EVENTHANDLER`: reaccionar a eventos en tiempo de ejecución

Un `SYSINIT` se dispara una sola vez, en una fase de arranque conocida. Un `EVENTHANDLER` se dispara cero o más veces, cada vez que ocurre un evento concreto del sistema. Los mecanismos son parientes; se complementan.

El kernel define una serie de eventos con nombre. Cada evento tiene una firma de callback fija y un conjunto fijo de circunstancias en las que se dispara. Un driver que se interesa por un evento registra un callback; el kernel invoca ese callback cada vez que el evento se dispara; el driver cancela el registro del callback en detach.

Algunos eventos de uso habitual:

- `shutdown_pre_sync`: el sistema está a punto de sincronizar los sistemas de archivos. Los drivers con cachés en memoria las vacían aquí.
- `shutdown_post_sync`: el sistema ha terminado de sincronizar los sistemas de archivos. Los drivers que necesitan saber que "el sistema de archivos está en silencio" se enganchan aquí.
- `shutdown_final`: el sistema está a punto de detenerse o reiniciarse. Los drivers con estado de hardware que debe guardarse lo hacen aquí.
- `vm_lowmem`: el subsistema de memoria virtual está bajo presión. Los drivers con sus propias cachés deben liberar algo de memoria.
- `power_suspend_early`, `power_suspend`, `power_resume`: ciclo de vida de suspensión y reanudación.
- `dev_clone`: un evento de clonación de dispositivo, utilizado por pseudodispositivos que aparecen bajo demanda.

La lista no es fija; se añaden nuevos eventos a medida que el kernel crece. Los anteriores son los que un driver general considera con más frecuencia.

### La forma de un registro `EVENTHANDLER`

El patrón tiene tres partes: declarar una función manejadora con la firma correcta, registrarla en attach y cancelar el registro en detach. El registro devuelve una etiqueta opaca; la cancelación del registro necesita esa etiqueta.

Para `shutdown_pre_sync`, la firma del manejador es:

```c
void (*handler)(void *arg, int howto);
```

`arg` es el puntero que el driver pasó al registro; normalmente es el softc. `howto` son los flags de apagado (`RB_HALT`, `RB_REBOOT`, etc.).

Un manejador de apagado mínimo para `myfirst`:

```c
#include <sys/eventhandler.h>

static eventhandler_tag myfirst_shutdown_tag;

static void
myfirst_shutdown(void *arg, int howto)
{
	struct myfirst_softc *sc = arg;

	mtx_lock(&sc->sc_mtx);
	DPRINTF(sc, MYF_DBG_INIT, "shutdown: howto=0x%x\n", howto);
	/* Flush any pending state here. */
	mtx_unlock(&sc->sc_mtx);
}
```

El registro ocurre dentro de `myfirst_attach` (o en un helper llamado desde él):

```c
myfirst_shutdown_tag = EVENTHANDLER_REGISTER(shutdown_pre_sync,
    myfirst_shutdown, sc, SHUTDOWN_PRI_DEFAULT);
```

La cancelación del registro ocurre dentro de `myfirst_detach`:

```c
EVENTHANDLER_DEREGISTER(shutdown_pre_sync, myfirst_shutdown_tag);
```

La cancelación del registro es obligatoria. Un driver que se desconecta sin cancelarla deja un puntero de callback colgante en la lista de eventos del kernel. Cuando el kernel dispara el evento a continuación, llama a una región de memoria que ya no está mapeada y el sistema entra en pánico.

La etiqueta almacenada en `myfirst_shutdown_tag` es lo que vincula el registro con la cancelación. Para un driver con una sola instancia, una variable estática como la anterior funciona bien. Para un driver con múltiples instancias, la etiqueta debería residir en el softc para que la cancelación de cada instancia haga referencia a su propia etiqueta.

### `EVENTHANDLER` en la cadena de attach

Dado que el registro y la cancelación son simétricos, encajan limpiamente en el patrón de limpieza con etiquetas de la Sección 5. El registro se convierte en una adquisición; su modo de fallo es "¿devolvió el registro un error?" (puede fallar en condiciones de poca memoria); su limpieza es `EVENTHANDLER_DEREGISTER`.

El fragmento de attach actualizado para un `myfirst` con soporte de `EVENTHANDLER`:

```c
	/* Stage 7: shutdown handler. */
	sc->sc_shutdown_tag = EVENTHANDLER_REGISTER(shutdown_pre_sync,
	    myfirst_shutdown, sc, SHUTDOWN_PRI_DEFAULT);
	if (sc->sc_shutdown_tag == NULL) {
		error = ENOMEM;
		goto fail_log;
	}

	return (0);

fail_log:
	myfirst_log_detach(sc);
fail_cdev:
	destroy_dev(sc->sc_cdev);
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
```

Y el detach correspondiente, con la cancelación del registro en primer lugar (orden inverso de adquisición):

```c
	EVENTHANDLER_DEREGISTER(shutdown_pre_sync, sc->sc_shutdown_tag);
	myfirst_log_detach(sc);
	destroy_dev(sc->sc_cdev);
	mtx_destroy(&sc->sc_mtx);
```

`sc->sc_shutdown_tag` reside en el softc. Almacenarlo allí es importante: la cancelación del registro necesita saber qué registro concreto eliminar, y el almacenamiento por softc mantiene las dos instancias del driver independientes.

### Prioridad: `SHUTDOWN_PRI_*`

Dentro de un mismo evento, los callbacks se invocan en orden de prioridad. La prioridad es el cuarto argumento de `EVENTHANDLER_REGISTER`. Para los eventos de apagado, las constantes habituales son:

- `SHUTDOWN_PRI_FIRST`: ejecutar antes que la mayoría de los demás manejadores.
- `SHUTDOWN_PRI_DEFAULT`: ejecutar en el orden predeterminado.
- `SHUTDOWN_PRI_LAST`: ejecutar después que los demás manejadores.

Un driver con hardware que debe silenciarse antes de que los sistemas de archivos se vacíen podría registrarse con `SHUTDOWN_PRI_FIRST`. Un driver cuyo estado depende de que los sistemas de archivos ya estén vaciados (improbable en la práctica) podría registrarse con `SHUTDOWN_PRI_LAST`. La mayoría de los drivers usa `SHUTDOWN_PRI_DEFAULT` y no se preocupa por la prioridad.

Existen constantes de prioridad similares para otros eventos (`EVENTHANDLER_PRI_FIRST`, `EVENTHANDLER_PRI_ANY`, `EVENTHANDLER_PRI_LAST`).

### Cuándo usar `vm_lowmem`

`vm_lowmem` es el evento que el subsistema de VM dispara cuando la memoria libre cae por debajo de un umbral. Un driver que mantiene su propia caché (un pool de bloques preasignados, por ejemplo) puede liberar algunos de ellos al kernel como respuesta.

El manejador se llama con un único argumento (el ID del subsistema que desencadenó el evento). Un manejador mínimo para un driver con caché:

```c
static void
myfirst_lowmem(void *arg, int unused __unused)
{
	struct myfirst_softc *sc = arg;

	mtx_lock(&sc->sc_mtx);
	/* Release some entries from the cache. */
	mtx_unlock(&sc->sc_mtx);
}
```

El registro tiene el mismo aspecto que el de apagado, pero con el nombre del evento:

```c
sc->sc_lowmem_tag = EVENTHANDLER_REGISTER(vm_lowmem,
    myfirst_lowmem, sc, EVENTHANDLER_PRI_ANY);
```

Un driver que no mantiene una caché no debería registrarse para `vm_lowmem`. El coste de hacerlo no es cero: el kernel llama a todos los manejadores registrados en cada evento de poca memoria, y un manejador que no hace nada añade latencia a esa cadena de llamadas.

Para `myfirst` no hay caché, por lo que `vm_lowmem` no se usa. El patrón se presenta para el lector que está a punto de escribir un driver que sí lo necesita.

### `power_suspend_early` y `power_resume`

La suspensión y reanudación es un ciclo de vida delicado. Entre `power_suspend_early` y `power_resume`, se espera que los dispositivos del driver estén quiescentes: sin I/O, sin interrupciones, sin transiciones de estado. Un driver con estado de hardware que debe guardarse antes de la suspensión y restaurarse después de la reanudación registra manejadores para ambos eventos.

Para los drivers de dispositivos de caracteres que no gestionan hardware, estos eventos normalmente no son aplicables. Para los drivers conectados a un bus (PCI, USB, SPI), la capa del bus gestiona la mayor parte del trabajo de suspensión y reanudación, y el driver solo tiene que proporcionar los métodos `device_suspend` y `device_resume` en su tabla `device_method_t`. El enfoque con `EVENTHANDLER` es para drivers que quieren reaccionar a una suspensión del sistema sin estar conectados a un bus.

El Capítulo 26 revisará la suspensión y reanudación cuando `myfirst` se convierta en un driver USB; en ese punto, el mecanismo de la capa del bus es el preferido.

### El manejador de eventos del módulo

Relacionado con `SYSINIT` y `EVENTHANDLER` está el manejador de eventos del módulo: el callback que el kernel invoca para `MOD_LOAD`, `MOD_UNLOAD`, `MOD_QUIESCE` y `MOD_SHUTDOWN`. La mayoría de los drivers no lo sobreescribe; `DRIVER_MODULE` proporciona una implementación predeterminada que llama a `device_probe` y `device_attach` de forma apropiada.

Un driver que necesite un comportamiento personalizado en la carga del módulo (más allá de lo que `SYSINIT` puede hacer) puede proporcionar su propio manejador:

```c
static int
myfirst_modevent(module_t mod, int what, void *arg)
{
	switch (what) {
	case MOD_LOAD:
		/* Custom load behaviour. */
		return (0);
	case MOD_UNLOAD:
		/* Custom unload behaviour. */
		return (0);
	case MOD_QUIESCE:
		/* Can we be unloaded?  Return errno if not. */
		return (0);
	case MOD_SHUTDOWN:
		/* Shutdown notification; usually no-op. */
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}
```

El manejador se conecta a través de una estructura `moduledata_t` en lugar de a través de `DRIVER_MODULE`. Los dos enfoques son mutuamente excluyentes para un nombre de módulo dado; el driver elige uno u otro.

Para la mayoría de los drivers, el valor predeterminado de `DRIVER_MODULE` es correcto y el manejador de eventos del módulo no se personaliza. `myfirst` usa `DRIVER_MODULE` en todo momento.

### Disciplina de cancelación de registro

La regla más importante al usar `EVENTHANDLER` es: registrar una vez, cancelar el registro una vez, en attach y detach respectivamente. Dos modos de fallo aparecen cuando se rompe esta regla.

El primer modo de fallo es la **cancelación de registro omitida**. Detach se ejecuta, la etiqueta no se cancela, la lista de eventos del kernel sigue apuntando al manejador del softc, y el siguiente evento se dispara hacia memoria ya liberada. El pánico ocurre lejos de la causa, porque el siguiente evento puede dispararse minutos u horas después de detach.

La solución es mecánica: cada `EVENTHANDLER_REGISTER` en attach tiene un `EVENTHANDLER_DEREGISTER` correspondiente en detach. El patrón de limpieza con etiquetas de la Sección 5 lo facilita: el registro es una adquisición con una etiqueta, y la cadena de limpieza cancela el registro en orden inverso.

El segundo modo de fallo es el **doble registro**. Un driver que registra el mismo handler dos veces tiene dos entradas en la lista de eventos del kernel; al hacer el detach una sola vez, se elimina únicamente una de ellas. El kernel queda entonces con una entrada obsoleta que apunta al softc que acaba de desaparecer.

La corrección es igualmente mecánica: registra exactamente una vez por cada attach. No registres en una función auxiliar que se llame desde varios lugares; no registres de forma diferida en respuesta al primer evento.

### Un ejemplo completo del ciclo de vida

Juntando `SYSINIT`, `EVENTHANDLER` y el attach con limpieza etiquetada, el ciclo de vida completo de un driver `myfirst` con un pool global y un manejador de apagado funciona de la siguiente manera:

Al arrancar el kernel o cargar el módulo:
- Se dispara `SI_SUB_TUNABLES`. Las llamadas a `TUNABLE_*_FETCH` en attach verán sus valores.
- Se dispara `SI_SUB_DRIVERS`. Se ejecuta `myfirst_log_pool_init` (a través de `SYSINIT`). El pool global está listo.
- Se dispara `SI_SUB_CONFIGURE`. `DRIVER_MODULE` registra el driver. Newbus sondea; `myfirst_probe` y `myfirst_attach` se ejecutan para cada instancia.
- Dentro de `myfirst_attach`: se inicializan el lock, el cdev, el sysctl, el estado de log y se registra el manejador de apagado.

En tiempo de ejecución:
- `ioctl(fd, MYFIRSTIOC_SETMSG, buf)` actualiza el mensaje.
- `devctl_notify` emite `MSG_CHANGED`; `devd` lo registra en el log.

Durante el apagado:
- El kernel dispara `shutdown_pre_sync`. `myfirst_shutdown` se ejecuta para cada manejador registrado.
- Los sistemas de archivos sincronizan.
- Se dispara `shutdown_final`. La máquina se detiene.

Al descargar el módulo (antes del apagado):
- Se dispara `MOD_QUIESCE`. `myfirst_detach` devuelve `EBUSY` si algún dispositivo está en uso.
- Se dispara `MOD_UNLOAD`. `myfirst_detach` se ejecuta para cada instancia: se cancela el registro del manejador, se libera el estado de log, se destruye el cdev y se destruye el lock.
- Se dispara `SYSUNINIT`. Se ejecuta `myfirst_log_pool_fini`. El pool global se libera.
- El módulo se desmapea.

Cada paso ocupa un lugar bien definido. Cada adquisición tiene su correspondiente liberación. Un driver que sigue este patrón de cerca es un driver que el kernel de FreeBSD puede cargar, ejecutar y descargar cualquier número de veces sin acumular estado.

### Decidir para qué eventos registrarse

Un autor de drivers que decide si registrarse para un evento debería hacerse tres preguntas.

La primera es: **¿el evento realmente importa a este driver?** `vm_lowmem` importa a un driver con caché; es ruido para uno sin ella. `shutdown_pre_sync` importa a un driver cuyo hardware necesita ser detenido ordenadamente; es ruido para un pseudo-driver. Un manejador que no hace nada útil sigue siendo llamado en cada evento, ralentizando ligeramente el sistema en cada disparo.

La segunda es: **¿es el evento correcto?** FreeBSD tiene varios eventos de apagado. `shutdown_pre_sync` se dispara antes de que los sistemas de archivos sincronicen; `shutdown_post_sync` se dispara después; `shutdown_final` se dispara justo antes del halt. Un driver que se registra en la fase equivocada podría vaciar su caché demasiado pronto (antes de datos que deberían vaciarse) o demasiado tarde (cuando los sistemas de archivos ya están finalizando).

La tercera es: **¿ha sido el evento estable a lo largo de las versiones del kernel?** `shutdown_pre_sync` lleva tiempo siendo estable y es seguro de usar. Los eventos más nuevos o más especializados pueden cambiar de firma entre versiones. Un driver que apunta a una versión específica de FreeBSD (este libro está alineado con la 14.3) puede confiar en los eventos de esa versión; un driver que apunta a un rango de versiones debe ser más cuidadoso.

En el caso de `myfirst`, el Capítulo 25 final registra `shutdown_pre_sync` como demostración. El manejador es un no-op: simplemente registra en el log que el apagado ha comenzado. El registro, la cancelación del registro y la forma de limpieza etiquetada son el punto central del ejemplo, no el cuerpo del manejador.

### Errores frecuentes con `SYSINIT` y `EVENTHANDLER`

Hay un puñado de errores que se repiten cuando estos mecanismos se usan por primera vez.

El primero es **ejecutar código pesado en un `SYSINIT`**. El código que se ejecuta durante el boot trabaja en un contexto donde muchos subsistemas del kernel aún se están inicializando. Un `SYSINIT` que llama a un subsistema complejo puede entrar en condición de carrera con la propia inicialización de ese subsistema. La regla es: el código en `SYSINIT` debe ser mínimo y autocontenido. La inicialización compleja pertenece a la rutina attach del driver, que se ejecuta después de que todos los subsistemas hayan arrancado.

El segundo error es **usar `SYSINIT` en lugar de `device_attach`**. Un `SYSINIT` se ejecuta una vez por carga del módulo, pero `device_attach` se ejecuta una vez por dispositivo. Un driver que inicializa estado por dispositivo en un `SYSINIT` está cometiendo un error categórico; el estado por dispositivo no existe todavía en el momento del `SYSINIT`.

El tercer error es **olvidar el argumento de prioridad en `EVENTHANDLER_REGISTER`**. La función toma cuatro argumentos: nombre del evento, callback, argumento y prioridad. Algunos drivers olvidan la prioridad y pasan un número incorrecto de argumentos; el compilador detecta esto con un error, pero un driver que por casualidad pasa `0` se registra con la prioridad más baja posible, lo cual puede ser incorrecto.

El cuarto error es **no poner a cero el campo del tag**. Si `sc->sc_shutdown_tag` no está inicializado cuando se llama a `EVENTHANDLER_DEREGISTER` en una ruta de fallo, la cancelación del registro intenta eliminar un tag que nunca se registró. El kernel detecta esto (el tag no existe en su lista de eventos) y la cancelación es un no-op, pero el patrón es frágil. La disciplina más limpia es poner a cero el softc en el momento de la asignación (Newbus hace esto automáticamente a través de `device_get_softc`, pero los drivers que asignan su propio softc deben hacerlo manualmente) y no llegar nunca a una cancelación de registro para un tag que no fue registrado.

### Cerrando la sección 8

`SYSINIT` y `EVENTHANDLER` son la forma que tiene el kernel de permitir que un driver participe en ciclos de vida más allá de su propia ventana de attach/detach. `SYSINIT` ejecuta código en una fase de boot específica; `EVENTHANDLER` ejecuta código en respuesta a un evento del kernel con nombre. Juntos cubren los casos en los que el código por dispositivo no es suficiente y el driver necesita interactuar con el sistema como un todo.

`myfirst` al final del Capítulo 25 usa `EVENTHANDLER_REGISTER` para un manejador de `shutdown_pre_sync` de demostración; el registro, la cancelación del registro y la forma de limpieza etiquetada están todos en su lugar. `SYSINIT` se presenta pero no se usa, porque `myfirst` no tiene hoy un pool global. Los patrones están sembrados; cuando el driver de un capítulo futuro sí los necesite, el lector los reconocerá de inmediato.

Con la sección 8 completa, todos los mecanismos que el capítulo se propuso enseñar están en el driver. El material restante del capítulo aplica estos mecanismos a través de laboratorios prácticos, ejercicios de desafío y una referencia de resolución de problemas para cuando las cosas salgan mal.

## Laboratorios prácticos

Los laboratorios de esta sección ejercitan los mecanismos del capítulo en un sistema real con FreeBSD 14.3. Cada laboratorio tiene un resultado concreto y medible; tras ejecutarlo, deberías ser capaz de describir qué observaste y qué significa. Los laboratorios asumen que tienes a mano el directorio de acompañamiento `examples/part-05/ch25-advanced/`.

Antes de empezar, construye el driver tal como viene en la parte superior de `ch25-advanced/`:

```console
# cd examples/part-05/ch25-advanced
# make clean
# make
# kldload ./myfirst.ko
# ls /dev/myfirst*
/dev/myfirst0
```

Si alguno de estos pasos falla, soluciona el problema con el toolchain o el código fuente antes de continuar. Los laboratorios asumen una base de trabajo funcional.

### Laboratorio 1: reproducir una inundación de log

Propósito: ver la diferencia entre un `device_printf` sin limitación de tasa y el `DLOG_RL` con límite de tasa cuando se dispara en un bucle intensivo.

Fuente: `examples/part-05/ch25-advanced/lab01-log-flood/` contiene un pequeño programa en espacio de usuario que llama a `read()` sobre `/dev/myfirst0` 10.000 veces tan rápido como el kernel lo permita.

Paso 1. Ajusta temporalmente la máscara de depuración para activar la clase de I/O y el printf en la ruta de lectura:

```console
# sysctl dev.myfirst.0.debug.mask=0x4
```

El bit de máscara `0x4` activa `MYF_DBG_IO`, que el callback de lectura utiliza.

Paso 2. Ejecuta la inundación primero con la versión ingenua del driver que usa `DPRINTF` sin límite. Compila y carga `myfirst-flood-unlimited.ko` desde `lab01-log-flood/unlimited/`:

```console
# make -C lab01-log-flood/unlimited
# kldunload myfirst
# kldload lab01-log-flood/unlimited/myfirst.ko
# dmesg -c > /dev/null
# ./lab01-log-flood/flood 10000
# dmesg | wc -l
```

Resultado esperado: aproximadamente 10.000 líneas en `dmesg`. La consola también puede llenarse; el buffer de log del sistema rota y se pierden los mensajes anteriores.

Paso 3. Descarga y recarga la versión con límite de tasa desde `lab01-log-flood/limited/`, que usa `DLOG_RL` con un límite de 10 mensajes por segundo:

```console
# kldunload myfirst
# kldload lab01-log-flood/limited/myfirst.ko
# dmesg -c > /dev/null
# ./lab01-log-flood/flood 10000
# sleep 5
# dmesg | wc -l
```

Resultado esperado: aproximadamente 50 líneas en `dmesg`. La inundación ahora emite como máximo 10 mensajes por segundo; la ventana de prueba de 10 segundos produce alrededor de 50 mensajes (el token de ráfaga del primer segundo más las asignaciones de los segundos siguientes, por lo que el recuento exacto puede variar pero debería estar dentro de diez).

Paso 4. Compara las dos salidas lado a lado. La versión con límite de tasa es legible; la versión sin límite no lo es. Ambos drivers tenían un comportamiento de lectura idéntico; solo difiere la disciplina de logging.

Anota: el tiempo de reloj que tardó la inundación en completarse en ambos casos. La versión sin límite es notablemente más lenta porque la propia salida por consola es un cuello de botella. El límite de tasa tiene un beneficio de rendimiento visible además del beneficio en claridad.

### Laboratorio 2: auditoría de errno con `truss`

Propósito: ver qué reporta `truss(1)` cuando el driver devuelve diferentes valores de errno, y calibrar la intuición sobre qué errno devolver desde cada ruta de código.

Fuente: `examples/part-05/ch25-advanced/lab02-errno-audit/` contiene un programa de usuario que realiza una serie de llamadas deliberadamente inválidas y un script que lo ejecuta bajo `truss`.

Paso 1. Carga el `myfirst.ko` de base si no está ya cargado:

```console
# kldload ./myfirst.ko
```

Paso 2. Ejecuta el programa de auditoría bajo `truss`:

```console
# truss -f -o /tmp/audit.truss ./lab02-errno-audit/audit
# less /tmp/audit.truss
```

El programa realiza estas operaciones en secuencia:
1. Abre `/dev/myfirst0`.
2. Emite un comando ioctl desconocido (número de comando 99).
3. Emite `MYFIRSTIOC_SETMSG` con un argumento NULL.
4. Escribe un buffer de longitud cero.
5. Escribe un buffer más grande de lo que el driver acepta.
6. Establece `dev.myfirst.0.timeout_sec` a un valor mayor del permitido.
7. Cierra.

Paso 3. En la salida de `truss`, localiza cada operación y anota su errno. Los resultados esperados son:

1. `open`: devuelve un descriptor de archivo. Sin errno.
2. `ioctl(_IOC=0x99)`: devuelve `ENOTTY` (la traducción del kernel del `ENOIOCTL` del driver).
3. `ioctl(MYFIRSTIOC_SETMSG, NULL)`: devuelve `EFAULT` (el kernel detecta el NULL antes de que se ejecute el manejador).
4. `write(0 bytes)`: devuelve `0` (sin error, simplemente sin bytes escritos).
5. `write(sobredimensionado)`: devuelve `EINVAL` (el driver rechaza longitudes por encima del tamaño de su buffer).
6. `sysctl write fuera de rango`: devuelve `EINVAL` (el manejador sysctl rechaza el valor).
7. `close`: devuelve 0. Sin errno.

Paso 4. Para cada errno observado, localiza en el código del driver la línea que lo devuelve. Recorre la cadena de llamadas desde `truss` hasta el código fuente del kernel y confirma que el errno que ves en `truss` es el que devolvió el driver. Este ejercicio calibra tu mapa mental entre "lo que ve el usuario" y "lo que dice el driver".

### Laboratorio 3: comportamiento del tunable tras un reinicio

Propósito: verificar que un tunable del cargador realmente cambia el estado inicial del driver cuando el módulo se carga por primera vez.

Fuente: `examples/part-05/ch25-advanced/lab03-tunable-reboot/` contiene un script auxiliar `apply_tunable.sh`.

Paso 1. Con el módulo de base cargado y sin ningún tunable configurado, confirma el valor inicial del timeout:

```console
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.timeout_sec
dev.myfirst.0.timeout_sec: 5
```

El valor por defecto es 5, establecido en la rutina attach.

Paso 2. Descarga el módulo, configura el tunable del cargador, recárgalo y confirma el nuevo valor inicial:

```console
# kldunload myfirst
# kenv hw.myfirst.timeout_sec=12
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.timeout_sec
dev.myfirst.0.timeout_sec: 12
```

El tunable establecido mediante `kenv(1)` surtió efecto porque `TUNABLE_INT_FETCH` en attach lo leyó antes de que se publicara el sysctl.

Paso 3. Cambia el sysctl en tiempo de ejecución y confirma que el cambio se acepta pero no se propaga de vuelta al tunable:

```console
# sysctl dev.myfirst.0.timeout_sec=25
dev.myfirst.0.timeout_sec: 12 -> 25
# kenv hw.myfirst.timeout_sec
hw.myfirst.timeout_sec="12"
```

El tunable sigue valiendo 12; el sysctl vale 25. El tunable es el valor inicial; el sysctl es el valor en tiempo de ejecución. Divergen en el momento en que se escribe el sysctl.

Paso 4. Descarga y recarga. El valor del tunable sigue siendo 12 (porque está en el entorno del kernel), por lo que el nuevo sysctl arranca en 12, no en 25. Este es el ciclo de vida: el tunable establece el valor inicial, el sysctl establece el valor en tiempo de ejecución, la descarga pierde el valor en tiempo de ejecución, el tunable sobrevive.

Paso 5. Borra el tunable y recarga:

```console
# kldunload myfirst
# kenv -u hw.myfirst.timeout_sec
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.timeout_sec
dev.myfirst.0.timeout_sec: 5
```

De vuelta al valor por defecto del momento del attach. El ciclo de vida es coherente de principio a fin.

### Laboratorio 4: inyección deliberada de fallos en attach

Propósito: verificar que cada etiqueta de la cadena de limpieza se alcanza y que no hay fugas de recursos cuando se inyecta un fallo en mitad del attach.

Fuente: `examples/part-05/ch25-advanced/lab04-failure-injection/` contiene cuatro variantes de compilación del módulo, cada una con un punto de inyección de fallo diferente compilado en su interior:

- `inject-mtx/`: falla justo después de que se inicializa el lock.
- `inject-cdev/`: falla justo después de que se crea el cdev.
- `inject-sysctl/`: falla justo después de que se construye el árbol sysctl.
- `inject-log/`: falla justo después de que se inicializa el estado de log.

Cada variante define exactamente uno de los macros `MYFIRST_DEBUG_INJECT_FAIL_*` de la Sección 5.

Paso 1. Construye y carga la primera variante. La carga debería fallar:

```console
# make -C lab04-failure-injection/inject-mtx
# kldload lab04-failure-injection/inject-mtx/myfirst.ko
kldload: an error occurred while loading module myfirst. Please check dmesg(8) for more details.
# dmesg | tail -3
myfirst0: attach: stage 1 complete
myfirst0: attach: injected failure after mtx_init
device_attach: myfirst0 attach returned 12
```

La función attach devolvió `ENOMEM` (errno 12) en el punto de fallo inyectado. El módulo no está cargado:

```console
# kldstat -n myfirst
kldstat: can't find file: myfirst
```

Paso 2. Repite el proceso con las otras tres variantes. Cada una debería fallar en la etapa concreta que su nombre sugiere, y cada una debería dejar el kernel en un estado limpio. Para confirmar ese estado limpio, comprueba si han quedado OIDs de sysctl, cdevs o locks huérfanos:

```console
# sysctl dev.myfirst 2>&1 | head
sysctl: unknown oid 'dev.myfirst'
# ls /dev/myfirst* 2>&1
ls: No match.
# dmesg | grep -i "witness\|leak"
```

Resultado esperado: sin coincidencias. Ningún sysctl, ningún cdev, ninguna queja de witness. La cadena de limpieza funciona correctamente.

Paso 3. Ejecuta el script de regresión combinado, que construye cada variante y comprueba los resultados de forma automática:

```console
# ./lab04-failure-injection/run.sh
```

El script construye cada variante, la carga, confirma que la carga falla, confirma que el estado es limpio y muestra un resumen de una línea por variante. Que las cuatro variantes pasen significa que cada etiqueta de la cadena de limpieza ha sido ejercitada en un kernel real y que ha liberado todos los recursos que mantenía en ese punto.

### Lab 5: handler `shutdown_pre_sync`

Objetivo: confirmar que el handler de apagado registrado se activa realmente durante un apagado real, y observar su orden de ejecución relativo a la sincronización del sistema de archivos.

Fuente: `examples/part-05/ch25-advanced/lab05-shutdown-handler/` contiene una versión de `myfirst.ko` cuyo handler `shutdown_pre_sync` imprime un mensaje distintivo en la consola.

Paso 1. Carga el módulo y verifica que el handler está registrado leyendo el log en el momento del attach:

```console
# kldload lab05-shutdown-handler/myfirst.ko
# dmesg | tail -1
myfirst0: attach: shutdown_pre_sync handler registered
```

Paso 2. Emite un reinicio. En una máquina de pruebas (no en una máquina de producción), la forma más sencilla es:

```console
# shutdown -r +1 "testing myfirst shutdown handler"
```

Observa la consola mientras la máquina se apaga. La secuencia esperada es:

```text
myfirst0: shutdown: howto=0x4
Syncing disks, buffers remaining... 0 0 0
Uptime: ...
```

La línea `myfirst0: shutdown: howto=0x4` aparece **antes** de "Syncing disks", porque `shutdown_pre_sync` se activa antes de que los sistemas de archivos se sincronicen. Si el mensaje del handler aparece después del mensaje de sincronización, el registro se realizó en el evento equivocado (`shutdown_post_sync` o `shutdown_final`). Si el mensaje no aparece nunca, el handler nunca se registró o nunca se desregistró (un double-free provocaría un panic, pero su ausencia silenciosa indicaría un error de registro).

Paso 3. Después de que la máquina reinicie, confirma que descargar el módulo antes del apagado elimina el handler correctamente:

```console
# kldload lab05-shutdown-handler/myfirst.ko
# kldunload myfirst
# dmesg | tail -2
myfirst0: detach: shutdown handler deregistered
myfirst0: detach: complete
```

El mensaje de desregistro confirma que la ruta de limpieza en detach se ejecutó. El par attach/detach es simétrico; no se filtran entradas de la lista de eventos.

### Lab 6: El script de regresión de 100 ciclos

Objetivo: ejecutar un ciclo sostenido de carga/descarga para detectar fugas que solo aparecen con attach/detach repetidos. Esta es la prueba de la lista de comprobación de producción de la Sección 7.

Fuente: `examples/part-05/ch25-advanced/lab06-100-cycles/` contiene `run.sh`, que realiza 100 ciclos de kldload / sleep / kldunload y registra la huella de memoria del kernel antes y después.

Paso 1. Registra la huella de memoria inicial del kernel:

```console
# vmstat -m | awk '$1=="Solaris" || $1=="kernel"' > /tmp/before.txt
# cat /tmp/before.txt
```

Paso 2. Ejecuta el script de ciclos:

```console
# ./lab06-100-cycles/run.sh
cycle 1/100: ok
cycle 2/100: ok
...
cycle 100/100: ok
done: 100 cycles, 0 failures, 0 leaks detected.
```

Paso 3. Registra la huella de memoria final:

```console
# vmstat -m | awk '$1=="Solaris" || $1=="kernel"' > /tmp/after.txt
# diff /tmp/before.txt /tmp/after.txt
```

Resultado esperado: sin diferencias significativas. Si hay una diferencia de más de unos pocos kilobytes (la contabilidad interna del kernel fluctúa), el driver tiene una fuga.

Paso 4. Si el script notifica algún fallo, examina `/tmp/myfirst-cycles.log` (que `run.sh` rellena) para encontrar el primer ciclo fallido. El fallo suele estar en el paso de desregistro: un `EVENTHANDLER_DEREGISTER` ausente o un `mtx_destroy` ausente.

Una ejecución limpia de 100 ciclos es una de las formas más sencillas de adquirir confianza en la disciplina de ciclo de vida de un driver. Repítela tras cada cambio sustancial en la cadena de attach o detach.

### Lab 7: Descubrimiento de capacidades en el espacio de usuario

Objetivo: confirmar que un programa en espacio de usuario puede descubrir las capacidades del driver en tiempo de ejecución y actuar en consecuencia, tal como se diseñó en la Sección 4.

Fuente: `examples/part-05/ch25-advanced/lab07-getcaps/` contiene `mfctl25.c`, una versión actualizada de `myfirstctl` que emite `MYFIRSTIOC_GETCAPS` antes de cada operación y omite las no admitidas.

Paso 1. Compila `mfctl25`:

```console
# make -C lab07-getcaps
```

Paso 2. Ejecútalo contra el driver estándar del Capítulo 25 y observa el informe de capacidades:

```console
# ./lab07-getcaps/mfctl25 caps
Driver reports capabilities:
  MYF_CAP_RESET
  MYF_CAP_GETMSG
  MYF_CAP_SETMSG
```

El driver informa de tres capacidades. El bit `MYF_CAP_TIMEOUT` está definido pero no establecido porque el comportamiento de timeout es un sysctl, no un ioctl.

Paso 3. Ejecuta cada operación y confirma que el programa solo intenta las que están soportadas:

```console
# ./lab07-getcaps/mfctl25 reset
# ./lab07-getcaps/mfctl25 getmsg
Current message: Hello from myfirst
# ./lab07-getcaps/mfctl25 setmsg "new message"
# ./lab07-getcaps/mfctl25 timeout
Timeout ioctl not supported; use sysctl dev.myfirst.0.timeout_sec instead.
```

La última línea es la comprobación de capacidad en acción: el programa solicitó `MYF_CAP_TIMEOUT`, el driver no lo anunció, y el programa imprimió un mensaje informativo en lugar de emitir un ioctl que habría devuelto `ENOTTY`.

Paso 4. Carga un build anterior (el `myfirst.ko` del Capítulo 24 en `lab07-getcaps/ch24/`) y vuelve a ejecutar:

```console
# kldunload myfirst
# kldload lab07-getcaps/ch24/myfirst.ko
# ./lab07-getcaps/mfctl25 caps
GETCAPS ioctl not supported.  Falling back to default feature set:
  MYF_CAP_RESET
  MYF_CAP_GETMSG
  MYF_CAP_SETMSG
```

Cuando `GETCAPS` en sí devuelve `ENOTTY`, el programa recurre a un conjunto de valores predeterminados seguros que coincide con el comportamiento conocido del Capítulo 24. Este es el patrón de compatibilidad hacia adelante en acción.

Paso 5. Recarga el driver del Capítulo 25 para restaurar el estado de la prueba:

```console
# kldunload myfirst
# kldload ./myfirst.ko
```

El ejercicio demuestra que el descubrimiento de capacidades permite que un único programa en espacio de usuario funcione correctamente con dos versiones del driver, que es precisamente el objetivo del patrón.

### Lab 8: Validación de rangos de sysctl

Objetivo: confirmar que cada sysctl escribible que expone el driver rechaza los valores fuera de rango y deja el estado interno intacto al rechazarlos.

Fuente: `examples/part-05/ch25-advanced/lab08-sysctl-validation/` contiene el driver compilado con comprobaciones de rango y un script de prueba `run.sh` que lleva cada sysctl a sus límites.

Paso 1. Carga el driver y lista sus sysctls escribibles:

```console
# kldload ./myfirst.ko
# sysctl -W dev.myfirst.0 | grep -v "^dev.myfirst.0.debug.classes"
dev.myfirst.0.timeout_sec: 5
dev.myfirst.0.max_retries: 3
dev.myfirst.0.log_ratelimit_pps: 10
dev.myfirst.0.debug.mask: 0
```

Cuatro sysctls escribibles. Cada uno tiene un rango válido específico.

Paso 2. Intenta establecer cada sysctl en cero, en su máximo permitido y en uno por encima de su máximo:

```console
# sysctl dev.myfirst.0.timeout_sec=0
sysctl: dev.myfirst.0.timeout_sec: Invalid argument
# sysctl dev.myfirst.0.timeout_sec=60
dev.myfirst.0.timeout_sec: 5 -> 60
# sysctl dev.myfirst.0.timeout_sec=61
sysctl: dev.myfirst.0.timeout_sec: Invalid argument
# sysctl dev.myfirst.0.timeout_sec
dev.myfirst.0.timeout_sec: 60
```

Los intentos fuera de rango son rechazados con `EINVAL`; el valor interno no se modifica. La asignación válida de 60 tiene éxito.

Paso 3. Repite el proceso para los demás sysctls:

- `max_retries`: rango válido 1-100. Prueba con 0, 100 y 101.
- `log_ratelimit_pps`: rango válido 1-10000. Prueba con 0, 10000 y 10001.
- `debug.mask`: rango válido 0-0xff (los bits definidos). Prueba con 0, 0xff y 0x100.

Para cada uno, el script informa de si pasa o falla. Un driver que supera todos los casos tiene una validación correcta a nivel de handler.

Paso 4. Examina los handlers de sysctl en `examples/part-05/ch25-advanced/myfirst_sysctl.c` y observa el patrón:

```c
static int
myfirst_sysctl_timeout_sec(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	u_int new_val;
	int error;

	mtx_lock(&sc->sc_mtx);
	new_val = sc->sc_timeout_sec;
	mtx_unlock(&sc->sc_mtx);

	error = sysctl_handle_int(oidp, &new_val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (new_val < 1 || new_val > 60)
		return (EINVAL);

	mtx_lock(&sc->sc_mtx);
	sc->sc_timeout_sec = new_val;
	mtx_unlock(&sc->sc_mtx);
	return (0);
}
```

Observa el orden de las operaciones: copia el valor actual para el lado de lectura, llama a `sysctl_handle_int` para gestionar la copia, valida en la escritura y confirma bajo el lock solo después de que la validación tenga éxito. Un handler que confirma antes de validar expone un estado inconsistente a los lectores concurrentes.

Paso 5. Confirma que la descripción del sysctl es útil (`sysctl -d`):

```console
# sysctl -d dev.myfirst.0.timeout_sec
dev.myfirst.0.timeout_sec: Operation timeout in seconds (range 1-60)
```

La descripción indica la unidad y el rango. Un usuario que lea el sysctl sin consultar ninguna documentación puede configurarlo correctamente de todos modos.

### Lab 9: Auditoría de los mensajes de log en todo el driver

Objetivo: hacer un inventario de todos los mensajes de log del driver y confirmar que cada uno sigue las disciplinas de la Sección 1 y la Sección 2 (`device_printf`, incluye errno cuando procede, con limitación de tasa cuando está en una ruta caliente).

Fuente: `examples/part-05/ch25-advanced/lab09-log-audit/` contiene un script de auditoría `audit.sh` y un comprobador basado en grep.

Paso 1. Ejecuta el script de auditoría contra el código fuente del driver:

```console
# cd examples/part-05/ch25-advanced
# ./lab09-log-audit/audit.sh
```

El script busca con grep cada llamada a `printf`, `device_printf`, `log`, `DPRINTF` y `DLOG_RL` en el árbol de código fuente y las clasifica en:

- PASS: usa `device_printf` o `DPRINTF`, el nombre del dispositivo es implícito.
- PASS: usa `DLOG_RL` en una ruta caliente.
- WARN: usa `printf` sin contexto de dispositivo (puede ser legítimo en `MOD_LOAD`).
- FAIL: usa `device_printf` en una ruta caliente sin limitación de tasa.

Salida esperada (para el driver estándar del Capítulo 25):

```text
myfirst.c:    15 log messages - 15 PASS
myfirst_cdev.c:  6 log messages - 6 PASS
myfirst_ioctl.c: 4 log messages - 4 PASS
myfirst_sysctl.c: 0 log messages
myfirst_log.c:   2 log messages - 2 PASS
Total: 27 log messages - 0 WARN, 0 FAIL
```

Paso 2. Rompe intencionalmente un mensaje (por ejemplo, cambia un `DPRINTF(sc, MYF_DBG_IO, ...)` en el callback de lectura por un `device_printf(sc->sc_dev, ...)` a secas) y vuelve a ejecutar:

```text
myfirst_cdev.c: 6 log messages - 5 PASS, 1 FAIL
  myfirst_cdev.c:83: device_printf on hot path not rate-limited
Total: 27 log messages - 0 WARN, 1 FAIL
```

La auditoría detectó la regresión. Revierte el cambio y vuelve a ejecutar para confirmar que el recuento vuelve a cero fallos.

Paso 3. Añade un nuevo mensaje de log a una ruta no caliente (por ejemplo, un mensaje de inicialización que se emite una sola vez en el attach). Confirma que la auditoría lo acepta como PASS:

```c
device_printf(dev, "initialised with timeout %u\n",
    sc->sc_timeout_sec);
```

Los mensajes que se emiten una única vez en el attach no necesitan limitación de tasa porque se activan exactamente una vez por instancia y por carga.

Paso 4. Para cada mensaje que la auditoría clasifique como PASS, confirma que el mensaje incluye contexto significativo. Un mensaje como "error" es un PASS para la herramienta de auditoría, pero un FAIL para el lector humano. Se requiere una segunda revisión manual de la salida de grep para confirmar que los mensajes son realmente útiles.

El laboratorio demuestra dos puntos. Primero, una auditoría mecánica detecta las reglas categóricas (limitación de tasa en rutas calientes, `device_printf` en lugar de `printf` a secas) pero no puede juzgar la calidad del mensaje. Segundo, la revisión humana es la que confirma que los mensajes contienen suficiente contexto para ser diagnosticados. Ambas revisiones juntas proporcionan al driver una superficie de log que realmente ayudará a un ingeniero de soporte en el futuro.

### Lab 10: Matriz de compatibilidad multiversión

Objetivo: confirmar que el patrón de descubrimiento de capacidades introducido en la Sección 4 permite realmente que un único programa en espacio de usuario funcione con tres versiones distintas del driver.

Fuente: `examples/part-05/ch25-advanced/lab10-compat-matrix/` contiene tres archivos `.ko` precompilados correspondientes a las versiones del driver 1.6-debug, 1.7-integration y 1.8-maintenance, junto con un único programa en espacio de usuario `mfctl-universal` que utiliza `MYFIRSTIOC_GETCAPS` (o un fallback) para decidir qué operaciones intentar.

Paso 1. Carga cada versión del driver por turno y ejecuta `mfctl-universal --caps` contra ella:

```console
# kldload lab10-compat-matrix/v1.6/myfirst.ko
# ./lab10-compat-matrix/mfctl-universal --caps
Driver: version 1.6-debug
GETCAPS ioctl: not supported
Using fallback capability set:
  MYF_CAP_GETMSG
  MYF_CAP_SETMSG

# kldunload myfirst
# kldload lab10-compat-matrix/v1.7/myfirst.ko
# ./lab10-compat-matrix/mfctl-universal --caps
Driver: version 1.7-integration
GETCAPS ioctl: not supported
Using fallback capability set:
  MYF_CAP_RESET
  MYF_CAP_GETMSG
  MYF_CAP_SETMSG

# kldunload myfirst
# kldload lab10-compat-matrix/v1.8/myfirst.ko
# ./lab10-compat-matrix/mfctl-universal --caps
Driver: version 1.8-maintenance
GETCAPS ioctl: supported
Driver reports capabilities:
  MYF_CAP_RESET
  MYF_CAP_GETMSG
  MYF_CAP_SETMSG
```

Tres versiones del driver, un único programa en espacio de usuario, tres decisiones de capacidad distintas. El programa funciona con cada versión.

Paso 2. Ejercita cada capacidad por turno y confirma que el programa omite las no soportadas:

```console
# kldunload myfirst
# kldload lab10-compat-matrix/v1.6/myfirst.ko
# ./lab10-compat-matrix/mfctl-universal reset
reset: not supported on this driver version (1.6-debug)
# ./lab10-compat-matrix/mfctl-universal getmsg
Current message: Hello from myfirst
```

La operación de reset, añadida en la versión 1.7, se omite limpiamente en la 1.6. El programa imprime un mensaje informativo en lugar de emitir un ioctl que devolvería `ENOTTY`.

Paso 3. Lee el código fuente de `mfctl-universal` y observa el fallback de tres niveles:

```c
uint32_t
driver_caps(int fd, const char *version)
{
	uint32_t caps;

	if (ioctl(fd, MYFIRSTIOC_GETCAPS, &caps) == 0)
		return (caps);
	if (errno != ENOTTY)
		err(1, "GETCAPS ioctl");

	/* Fallback by version string. */
	if (strstr(version, "1.8-") != NULL)
		return (MYF_CAP_RESET | MYF_CAP_GETMSG |
		    MYF_CAP_SETMSG);
	if (strstr(version, "1.7-") != NULL)
		return (MYF_CAP_RESET | MYF_CAP_GETMSG |
		    MYF_CAP_SETMSG);
	if (strstr(version, "1.6-") != NULL)
		return (MYF_CAP_GETMSG | MYF_CAP_SETMSG);

	/* Unknown version: use the minimal safe set. */
	return (MYF_CAP_GETMSG);
}
```

El primer nivel pregunta al driver directamente. El segundo nivel compara cadenas de versión conocidas. El tercer nivel recurre a un conjunto mínimo que todas las versiones del driver han soportado siempre.

Paso 4. Reflexiona sobre qué ocurre cuando la versión 1.9 sale con un nuevo bit de capacidad. El programa no necesita actualizarse: `MYFIRSTIOC_GETCAPS` en la versión 1.9 informará del nuevo bit, el programa lo verá, y si conoce la operación correspondiente la utilizará. Si el programa no conoce la operación, el bit se ignora. De cualquier manera, el programa sigue funcionando.

El laboratorio demuestra que el descubrimiento de capacidades no es un patrón abstracto; es el mecanismo concreto que permite que un único programa en espacio de usuario abarque tres versiones del driver sin modificación.

## Ejercicios de desafío

Los desafíos de esta sección van más allá de los laboratorios. Cada uno te pide que extiendas el driver en una dirección que el capítulo apuntó pero no completó. Trabájalos cuando estés listo; ninguno requiere conocimientos del kernel que vayan más allá de lo cubierto en el capítulo, pero cada uno exige una lectura cuidadosa del código existente.

### Desafío 1: Límites de tasa por clase

La Sección 1 esbozó tres ranuras de limitación de tasa en el softc (`sc_rl_generic`, `sc_rl_io`, `sc_rl_intr`), pero el macro `DLOG_RL` usa un único valor pps (`sc_log_pps`). Extiende el driver para que cada clase tenga su propio límite pps configurable mediante sysctl:

- Añade los campos `sc_log_pps_io` y `sc_log_pps_intr` al softc junto a `sc_log_pps` (que se mantiene como límite genérico).
- Añade los sysctls correspondientes bajo `dev.myfirst.<unit>.log.pps_*` y los tunables correspondientes bajo `hw.myfirst.log_pps_*`.
- Actualiza los helpers `DLOG_RL_IO` y `DLOG_RL_INTR` (o un helper genérico que tome tanto la clase como el valor pps) para que respeten el límite por clase.

Escribe un programa de prueba corto que lance una ráfaga de mensajes en cada clase y confirma en `dmesg` que cada clase tiene limitación de tasa de forma independiente. El bucket genérico no debe privar de recursos al bucket de I/O, ni al revés.

Pista: la forma más reutilizable es una función auxiliar `myfirst_log_ratelimited(sc, class, fmt, ...)` que busca el estado de limitación de tasa correcto y el límite pps correcto dado el bit de clase. Los macros `DLOG_RL_*` se convierten en envoltorios ligeros sobre la función auxiliar.

### Desafío 2: Un sysctl de cadena escribible

La Sección 3 advirtió sobre la complicación de los sysctls de cadena escribibles. Impleméntalo correctamente. El sysctl debe ser `dev.myfirst.<unit>.message`, con `CTLFLAG_RW`, y debe permitir que un operador reescriba el mensaje almacenado en el driver con una sola llamada a `sysctl(8)`.

Requisitos:

1. El handler debe tomar el mutex de softc alrededor de la actualización.
2. El handler debe validar la longitud contra `sizeof(sc->sc_msg)` y rechazar cadenas demasiado largas con `EINVAL`.
3. El handler debe usar `sysctl_handle_string` para la copia; no reimplementes el acceso al espacio de usuario.
4. Tras una actualización correcta, el handler debe emitir un `devctl_notify` para `MSG_CHANGED`, igual que hace el ioctl.

Pruébalo con:

```console
# sysctl dev.myfirst.0.message="hello from sysctl"
# sysctl dev.myfirst.0.message
dev.myfirst.0.message: hello from sysctl
# sysctl dev.myfirst.0.message="$(printf 'A%.0s' {1..1000})"
sysctl: dev.myfirst.0.message: Invalid argument
```

El segundo `sysctl` debería fallar (por exceder el tamaño), y el mensaje del driver debería quedar sin cambios.

Considera: ¿deberían el ioctl y el sysctl emitir el mismo evento `MSG_CHANGED`, o eventos distintos? Ambos lados están actualizando el mismo estado subyacente; un único tipo de evento es probablemente lo correcto. Documenta tu decisión en `MAINTENANCE.md`.

### Desafío 3: Un manejador `MOD_QUIESCE` independiente del detach

La sección 7 señalaba que `MOD_QUIESCE` y `MOD_UNLOAD` son conceptualmente distintos, pero que `myfirst` gestiona ambos a través de `myfirst_detach`. Sepáralos de modo que la pregunta de quiesce pueda responderse sin efectos secundarios.

Requisitos:

1. Añade una comprobación explícita de `MOD_QUIESCE` en un manejador de eventos del módulo. El manejador devuelve `EBUSY` si algún dispositivo está abierto, `0` en caso contrario.
2. El manejador no llama a `destroy_dev`, no destruye locks, no altera el estado. Solo lee `sc_open_count`.
3. Para cada instancia adjunta, itera mediante `devclass` y comprueba cada softc. Usa el símbolo `myfirst_devclass` que exporta `DRIVER_MODULE`.

Pista: consulta `/usr/src/sys/kern/subr_bus.c` para `devclass_get_softc` y funciones auxiliares relacionadas. Son la forma de enumerar softcs desde una función a nivel de módulo que no dispone de un `device_t`.

Prueba: abre `/dev/myfirst0`, intenta `kldunload myfirst`, confirma que informa "module busy" y que el driver no cambia. Cierra el fd, vuelve a intentar la descarga y confirma que tiene éxito.

### Desafío 4: Un detach basado en vaciado en lugar de `EBUSY`

El patrón de detach del capítulo rechaza la descarga si el driver está en uso. Un patrón más elaborado vacía las referencias en vuelo en lugar de rechazar. Impleméntalo.

Requisitos:

1. Añade un booleano `is_dying` al softc, protegido por `sc_mtx`.
2. En `myfirst_open`, comprueba `is_dying` bajo el lock y devuelve `ENXIO` si es true.
3. En `myfirst_detach`, establece `is_dying` bajo el lock. Espera a que `sc_open_count` llegue a cero, usando `mtx_sleep` con una variable de condición o un bucle de sondeo simple con un tiempo de espera.
4. Una vez que `sc_open_count` llega a cero, procede con `destroy_dev` y el resto de la cadena de detach.

Añade un tiempo de espera: si `sc_open_count` no llega a cero en (digamos) 30 segundos, devuelve `EBUSY` desde el detach. El operador recibe una señal clara de que el driver no está vaciando; puede matar el proceso responsable y volver a intentarlo.

Prueba: abre `/dev/myfirst0` en un bucle desde un shell, llama a `kldunload myfirst` desde otro shell y observa el comportamiento de vaciado.

### Desafío 5: Una comprobación de versión gestionada mediante sysctl

Escribe un pequeño programa en espacio de usuario que lea `dev.myfirst.<unit>.version`, analice la cadena de versión y la compare con la versión mínima que requiere el programa. El programa debe imprimir "ok" si el driver es suficientemente reciente y "driver too old, please update" si no lo es.

Requisitos:

1. Analiza la cadena `X.Y-tag` como enteros. Rechaza cadenas malformadas con un error claro.
2. Compara con un mínimo de `"1.8"`. Un driver que informe `"1.7-integration"` debe fallar la comprobación; uno que informe `"1.8-maintenance"` debe pasar; uno que informe `"2.0-something"` debe pasar.
3. Sal con estado `0` en caso de éxito y distinto de cero en caso de fallo, de modo que la comprobación pueda usarse en scripts de shell.

Reflexiona: ¿podría un programa bien diseñado confiar en la cadena de versión para la comprobación de compatibilidad, o el bitmask de capacidades de la sección 4 es la señal más adecuada? No hay una única respuesta correcta; el ejercicio consiste en reflexionar sobre las ventajas e inconvenientes de cada enfoque.

### Desafío 6: Añadir un sysctl que enumere los descriptores de archivo abiertos

Añade un nuevo sysctl `dev.myfirst.<unit>.open_fds` que devuelva, como cadena, los PIDs de los procesos que actualmente tienen el dispositivo abierto. Esto es más difícil de lo que parece: el driver normalmente no registra qué proceso abrió cada fd.

Pista: en `myfirst_open`, almacena el PID del thread que realiza la llamada en una lista enlazada bajo el softc. En `myfirst_close`, elimina la entrada correspondiente. En el manejador del sysctl, recorre la lista bajo el mutex del softc y construye una cadena de PIDs separados por comas.

Casos límite:

1. ¿Un proceso que tiene el dispositivo abierto varias veces (múltiples fds, hijos creados con fork) debe aparecer una o varias veces? Decide y documéntalo.
2. La lista debe tener una longitud máxima; un atacante podría abrir el dispositivo millones de veces.
3. El valor del sysctl es de solo lectura; el manejador no debe modificar la lista.

Reflexiona: ¿es esta información realmente útil, o `fstat(1)` es una herramienta mejor para el mismo fin? La respuesta depende de si el driver puede proporcionar información que las herramientas de espacio de usuario no pueden obtener por sí solas.

### Desafío 7: Un segundo `EVENTHANDLER` para `vm_lowmem`

`myfirst` no tiene ninguna caché hoy en día, pero imagina que sí: un pool de buffers de 4 KB preasignados utilizados para operaciones de lectura y escritura. Bajo presión de memoria baja, el driver debería liberar algunos de esos buffers.

Implementa una caché sintética: asigna un array de 64 punteros a `malloc(M_TEMP, 4096)` en el attach. Registra un manejador de `vm_lowmem` que, cuando se dispara, libere la mitad de los buffers almacenados. El reattach los reasigna.

Requisitos:

1. Las asignaciones de la caché se realizan bajo el mutex del softc.
2. El manejador de `vm_lowmem` toma el mutex, recorre el array y llama a `free()` sobre los primeros 32 buffers.
3. Un sysctl `dev.myfirst.<unit>.cache_free` informa del número actual de slots libres (NULL); un operador puede confirmar así que el manejador se ha disparado.

Prueba: usa un bucle `stress -m 10 --vm-bytes 512M` para poner el sistema bajo presión de memoria baja y observa el sysctl `cache_free`. Con el tiempo, su valor debería crecer a medida que `vm_lowmem` se dispara repetidamente.

Reflexiona: ¿es esto para lo que está pensado el evento? Muchos drivers que se registran para `vm_lowmem` tienen cachés mucho más grandes que 64 buffers; la relación coste/beneficio es diferente. Esto es un ejercicio de aprendizaje; un driver real pensaría más detenidamente si su caché merece la complejidad añadida.

### Desafío 8: Un ioctl `MYFIRSTIOC_GETSTATS` que devuelve una carga útil estructurada

Hasta ahora, cada ioctl que gestiona el driver devuelve un escalar: un entero, un uint32 o una cadena de tamaño fijo. Añade un ioctl `MYFIRSTIOC_GETSTATS` que devuelva una carga útil estructurada con todos los contadores que el driver mantiene.

Requisitos:

1. Define `struct myfirst_stats` en `myfirst_ioctl.h` con campos para `open_count`, `total_reads`, `total_writes`, `log_drops` (un nuevo contador que añades) y `last_error_errno` (otro nuevo contador).
2. Añade `MYFIRSTIOC_GETSTATS` con el número de comando 6, declarado como `_IOR('M', 6, struct myfirst_stats)`.
3. El manejador copia los contadores del softc en la carga útil bajo `sc_mtx` y retorna.
4. Anuncia un nuevo bit de capacidad `MYF_CAP_STATS` en la respuesta de `GETCAPS`.
5. Actualiza `MAINTENANCE.md` para documentar el nuevo ioctl y la nueva capacidad.

Casos límite:

1. ¿Qué ocurre si el tamaño de la estructura cambia más adelante? La macro `_IOR` incluye el tamaño en el número de comando. Añadir un campo cambia el número de comando, lo que rompe los llamadores anteriores. La solución es incluir un campo `version` y un espacio `reserved` en la estructura desde el primer día; cualquier adición futura reutiliza el espacio reservado.

2. ¿Es seguro devolver todos los contadores de forma atómica, o necesitan locks separados? Mantener `sc_mtx` durante toda la copia es la disciplina más sencilla.

Reflexiona: aquí es donde el diseño de ioctl empieza a volverse complejo. Para una instantánea simple de contadores, un sysctl con salida en formato de cadena podría ser más sencillo que un ioctl con una estructura con versión. ¿Cuál elegirías y por qué?

### Desafío 9: Monitorización en tiempo real basada en devctl

Añade un segundo evento `devctl_notify` que se dispare cada vez que el cubo de limitación de tasa descarte un mensaje. El evento debe incluir el nombre de la clase y el estado actual del cubo como datos en formato clave=valor.

Requisitos:

1. Cuando `ppsratecheck` devuelve cero (mensaje descartado), incrementa un contador de descartes por clase y emite un `devctl_notify` con `system="myfirst"`, `type="LOG_DROPPED"` y datos `"class=io drops=42"`.
2. El propio evento devctl debe estar limitado en tasa; de lo contrario, el acto de informar sobre los descartes se convierte en otra avalancha. Usa un segundo `ppsratecheck` con un límite lento (por ejemplo, 1 pps) para las emisiones devctl.
3. Escribe una regla devd que coincida con el evento y registre un resumen cada vez que se dispare.

Prueba: ejecuta el programa de avalancha del Laboratorio 1 y confirma que `devctl` emite el informe de descarte sin desbordarse a sí mismo.

## Guía de resolución de problemas

Cuando los mecanismos de este capítulo no se comportan correctamente, los síntomas suelen ser indirectos: una línea de registro que desaparece silenciosamente, un driver que se niega a cargar por la razón equivocada, un sysctl que no aparece, un reinicio que no llama a tu manejador. Esta referencia relaciona los síntomas más comunes con el mecanismo más probable responsable, e indica los primeros lugares donde buscar en el código fuente del driver.

### Síntoma: `kldload` devuelve "Exec format error"

El módulo fue construido contra un ABI del kernel que no coincide con el kernel en ejecución. La causa habitual es una discrepancia entre la versión del kernel en ejecución y el `SYSDIR` utilizado durante la compilación.

Comprueba: `uname -r` y el valor de `SYSDIR` en el Makefile. Si el kernel es 14.3-RELEASE pero el build tomó cabeceras de un árbol 15.0-CURRENT más reciente, el ABI es diferente.

Solución: apunta `SYSDIR` al árbol de código fuente que coincide con el kernel en ejecución. El Makefile del Capítulo 25 usa `/usr/src/sys` por defecto; en un sistema 14.3 con `/usr/src` coincidente, esto es correcto.

### Síntoma: `kldload` devuelve "No such file or directory" para un archivo aparentemente obvio

El archivo está presente pero el cargador de módulos del kernel no puede analizarlo. Las causas más comunes son que el archivo es un artefacto de construcción obsoleto de otra máquina, o que el archivo está corrupto.

Comprueba: `file myfirst.ko` debería informar de que es un objeto compartido ELF de 64 bits LSB. Si informa de cualquier otra cosa, vuelve a construirlo desde el código fuente.

### Síntoma: `kldload` tiene éxito pero `kldstat` no muestra el módulo

El cargador decidió descargar automáticamente el módulo. Esto ocurre cuando `MOD_LOAD` devolvió cero pero el `device_identify` de `DRIVER_MODULE` no encontró ningún dispositivo. Para `myfirst`, que usa `nexus` como padre, esto no debería ocurrir; el pseudo-driver siempre encuentra `nexus`.

Comprueba: `dmesg | tail -20` en busca de una línea similar a `module "myfirst" failed to register`. El mensaje indica qué salió mal.

### Síntoma: `kldload` informa "module busy"

Una instancia anterior del driver sigue cargada y tiene un descriptor de archivo abierto en algún lugar. La ruta `MOD_QUIESCE` de la instancia anterior devolvió `EBUSY`.

Comprueba: `fstat | grep myfirst` debería mostrar el proceso que mantiene el fd. Mata el proceso o cierra el fd y luego vuelve a intentar `kldunload`.

### Síntoma: `sysctl dev.myfirst.0.debug.mask=0x4` devuelve "Operation not permitted"

El llamador no es root. Los sysctls con `CTLFLAG_RW` generalmente requieren privilegios de root a menos que estén marcados explícitamente de otro modo.

Comprueba: ¿estás ejecutando como root? Usa `sudo sysctl ...` o `su -` primero.

### Síntoma: Un nuevo sysctl no aparece en el árbol

`SYSCTL_ADD_*` no fue llamado, o fue llamado con el puntero de contexto o de árbol incorrecto. El error más común es usar `SYSCTL_STATIC_CHILDREN` para un OID por dispositivo en lugar de `device_get_sysctl_tree`.

Comprueba: dentro de `myfirst_sysctl_attach`, confirma que se usan `ctx = device_get_sysctl_ctx(dev)` y `tree = device_get_sysctl_tree(dev)`, y que cada llamada a `SYSCTL_ADD_*` pasa `ctx` como primer argumento.

### Síntoma: Un tunable parece ser ignorado

`TUNABLE_*_FETCH` se ejecuta en el momento del attach, pero solo si el tunable está en el entorno del kernel en ese momento. Los errores más comunes son: (a) establecer el tunable después de cargar el módulo, (b) escribir mal el nombre, (c) olvidar que `kenv` no es persistente.

Comprueba:
- `kenv hw.myfirst.timeout_sec` antes de recargar el módulo. El valor debería ser el esperado.
- La cadena pasada a `TUNABLE_INT_FETCH` debe coincidir exactamente con `kenv`. Un error tipográfico en uno de los dos lados es silencioso.
- Establecer el valor mediante `/boot/loader.conf` requiere un reinicio (o un `kldunload` seguido de `kldload`, que relee `loader.conf` para los tunables específicos del módulo).

### Síntoma: Un mensaje de registro debería dispararse pero no aparece en `dmesg`

Tres causas comunes:

1. El bit de la clase de depuración no está activado. Comprueba `sysctl dev.myfirst.0.debug.mask`; el bit de la clase debe estar habilitado.
2. El cubo de limitación de tasa está vacío. Si el mensaje se emite a través de `DLOG_RL`, los primeros se disparan y el resto se suprime silenciosamente. Establece un límite de pps más alto mediante el sysctl o espera un segundo a que el cubo se rellene.
3. El mensaje se emite pero es filtrado por la configuración de `sysctl kern.msgbuf_show_timestamp` del sistema o por el tamaño del buffer de `dmesg` (`sysctl kern.msgbuf_size`).

Comprueba: `dmesg -c > /dev/null` para vaciar el buffer, reproduce la acción y vuelve a leer el buffer. El buffer vaciado debería contener únicamente la salida del driver.

### Síntoma: Un mensaje de log aparece una vez y después queda en silencio para siempre

El bucket de control de tasa deniega el mensaje de forma permanente. Esto ocurre si `rl_curpps` se hace muy grande y el límite de pps es muy bajo. Comprueba que `ppsratecheck` se esté llamando con un `struct timeval` estable y un `int *` estable (ambos miembros del softc); una variable de pila por llamada se reiniciaría a cero en cada invocación y el algoritmo dispararía en todo momento.

Comprobación: el estado de control de tasa debe estar en el softc u otra ubicación persistente, no en variables locales.

### Síntoma: Attach falla y la cadena de limpieza no libera un recurso

Al goto etiquetado le falta un paso o tiene un `return` desviado que cortocircuita la cadena. Añade un `device_printf(dev, "reached label fail_X\n")` al inicio de cada etiqueta y vuelve a ejecutar el laboratorio de inyección de fallos. La etiqueta que no imprime es la que se saltó.

Causa común: un `return (error)` intermedio insertado para depuración y nunca eliminado. El compilador no avisa porque la cadena sigue siendo sintácticamente válida; el comportamiento es incorrecto.

### Síntoma: Detach provoca un panic con un aviso de "witness"

Se destruyó un lock mientras seguía retenido, o se adquirió un lock después de que su propietario fue destruido. El subsistema witness detecta ambos casos. El backtrace apunta al nombre del lock, que se corresponde con el campo del softc.

Comprobación: la cadena de detach debe ser el inverso exacto de attach. Un error común es llamar a `mtx_destroy(&sc->sc_mtx)` antes de `destroy_dev(sc->sc_cdev)`: los callbacks del cdev pueden seguir en ejecución, intentan tomar el lock y el lock ya no existe. Solución: destruye primero el cdev y después el lock.

### Síntoma: El driver provoca un panic al descargar el módulo con un puntero colgante

No se llamó a `EVENTHANDLER_DEREGISTER`, el kernel disparó el evento y el puntero de callback apuntaba a memoria ya liberada.

Comprobación: por cada `EVENTHANDLER_REGISTER` en attach, busca `EVENTHANDLER_DEREGISTER` en detach. El número debe coincidir. Si coincide pero el panic sigue ocurriendo, el tag almacenado en el softc fue corrompido; revisa el flujo de código entre el registro y la cancelación del registro en busca de corrupción de memoria.

### Síntoma: `MYFIRSTIOC_GETVER` devuelve un valor inesperado

El entero de versión del ioctl en `myfirst_ioctl.h` no coincide con lo que `myfirst_ioctl.c` escribe en el buffer. Esto ocurre si se actualiza el header pero el handler sigue devolviendo una constante fija.

Comprobación: el handler debe escribir `MYFIRST_IOCTL_VERSION` (la constante del header), no un entero literal.

### Síntoma: Los eventos de `devctl_notify` nunca aparecen en `devd.log`

`devd(8)` no está en ejecución, o su configuración no coincide con el evento.

Comprobación:
- `service devd status` confirma que el daemon está en ejecución.
- `grep myfirst /etc/devd/*.conf` debería encontrar la regla.
- `devd -Df` en primer plano imprime cada evento a medida que llega; reproduce la acción y observa la salida.

### Síntoma: El script de regresión de 100 ciclos aumenta el consumo de memoria del kernel

Hay un recurso que se filtra en cada ciclo de carga/descarga. Los culpables habituales son: un `malloc` sin su correspondiente `free`, un `EVENTHANDLER_REGISTER` sin su cancelación del registro, o un `sysctl_ctx_init` que el driver llama manualmente sin llamar a `sysctl_ctx_free` en detach (el Capítulo 25 usa `device_get_sysctl_ctx`, que Newbus gestiona; un driver que asigna su propio contexto debe liberarlo).

Comprobación: ejecuta `vmstat -m | grep myfirst` antes y después para ver el consumo de memoria propio del driver, y `vmstat -m | grep solaris` para detectar estructuras a nivel del kernel que el driver pueda estar asignando de forma indirecta.

### Síntoma: Dos llamadas concurrentes a `MYFIRSTIOC_SETMSG` intercalan sus escrituras

El mutex del softc no se retiene durante la actualización. Los dos threads están escribiendo en `sc->sc_msg` simultáneamente, lo que produce un resultado corrupto.

Comprobación: cada acceso a `sc->sc_msg` y `sc->sc_msglen` en el handler del ioctl debe estar dentro de `mtx_lock(&sc->sc_mtx) ... mtx_unlock(&sc->sc_mtx)`.

### Síntoma: Un valor de sysctl se reinicia en cada carga del módulo

Este es el comportamiento esperado, no un error. El valor predeterminado en el momento del attach es el que evalúe el tunable, ya sea el valor predeterminado de `TUNABLE_INT_FETCH` o el valor establecido mediante `kenv`. La escritura en sysctl en tiempo de ejecución se pierde al descargar el módulo. Si quieres que el valor persista, establécelo mediante `kenv` o `/boot/loader.conf`.

### Síntoma: `MYFIRSTIOC_GETCAPS` devuelve un valor sin los bits que acabas de añadir

El archivo `myfirst_ioctl.c` fue actualizado pero no recompilado, o se cargó el build incorrecto. Comprueba también que el handler en el switch use el operador `|=` o una única asignación que incluya todos los bits.

Comprobación: ejecuta `make clean && make` desde el directorio del ejemplo. Usa `kldstat -v | grep myfirst` para confirmar que la ruta del módulo cargado coincide con lo que construiste.

### Síntoma: Un SYSINIT se dispara antes de que el asignador de memoria del kernel esté listo

El SYSINIT está registrado con un ID de subsistema demasiado temprano. Muchos subsistemas (tunables, locks, initcalls tempranas) no tienen permitido llamar a `malloc` con `M_NOWAIT`, y mucho menos con `M_WAITOK`. Si tu callback llama a `malloc` y el kernel provoca un panic en el boot, revisa el ID de subsistema.

Comprobación: el ID de subsistema en `SYSINIT(...)`. Para un callback que asigna memoria, usa `SI_SUB_DRIVERS` o posterior; no uses `SI_SUB_TUNABLES` ni ninguno anterior.

### Síntoma: Un handler registrado con `EVENTHANDLER_PRI_FIRST` sigue ejecutándose tarde

`EVENTHANDLER_PRI_FIRST` no es una garantía absoluta; es una prioridad en una cola ordenada. Si otro handler también está registrado con `EVENTHANDLER_PRI_FIRST`, el orden entre ellos es indefinido. Las prioridades documentadas son aproximadas; no se admite un ordenamiento de grano fino.

Comprobación: acepta que la prioridad es una indicación, no un contrato. Si el driver necesita ejecutarse estrictamente antes o después de otro handler concreto, el diseño es incorrecto; reestructura el driver para que el orden no importe.

### Síntoma: `dmesg` no muestra ninguna salida del driver

El driver está usando `printf` (el de tipo libc) en lugar de `device_printf` o `DPRINTF`. El `printf` del kernel sigue funcionando, pero no incluye el nombre del dispositivo, lo que dificulta el filtrado de mensajes.

Comprobación: todos los mensajes del driver deben pasar por `device_printf(dev, ...)` o `DPRINTF(sc, class, fmt, ...)`. El uso de `printf` a secas es habitualmente un error.

## Referencia rápida

La Referencia rápida es un resumen en una página de las macros, flags y funciones que introdujo el Capítulo 25. Úsala como consulta rápida en el teclado una vez que el material te resulte familiar.

### Logging con control de tasa

```c
struct myfirst_ratelimit {
	struct timeval rl_lasttime;
	int            rl_curpps;
};

#define DLOG_RL(sc, rlp, pps, fmt, ...) do {                            \
	if (ppsratecheck(&(rlp)->rl_lasttime, &(rlp)->rl_curpps, pps)) \
		device_printf((sc)->sc_dev, fmt, ##__VA_ARGS__);        \
} while (0)
```

Usa `DLOG_RL` para cualquier mensaje que pueda dispararse en un bucle. Coloca la `struct myfirst_ratelimit` en el softc (no en el stack).

### Vocabulario de errno

| Errno | Valor | Uso |
|-------|-------|-----|
| `0` | 0 | éxito |
| `EPERM` | 1 | operación no permitida (solo root) |
| `ENOENT` | 2 | no existe el archivo |
| `EBADF` | 9 | descriptor de archivo incorrecto |
| `ENOMEM` | 12 | no se puede asignar memoria |
| `EACCES` | 13 | permiso denegado |
| `EFAULT` | 14 | dirección incorrecta (puntero de usuario) |
| `EBUSY` | 16 | recurso ocupado |
| `ENODEV` | 19 | no existe el dispositivo |
| `EINVAL` | 22 | argumento inválido |
| `ENOTTY` | 25 | ioctl inapropiado para el dispositivo |
| `ENOTSUP` / `EOPNOTSUPP` | 45 | operación no admitida |
| `ENOIOCTL` | -3 | ioctl no gestionado por este driver (interno; el kernel lo mapea a `ENOTTY`) |

### Familias de tunables

```c
TUNABLE_INT_FETCH("hw.myfirst.name",    &sc->sc_int_var);
TUNABLE_LONG_FETCH("hw.myfirst.name",   &sc->sc_long_var);
TUNABLE_BOOL_FETCH("hw.myfirst.name",   &sc->sc_bool_var);
TUNABLE_STR_FETCH("hw.myfirst.name",     sc->sc_str_var,
                                          sizeof(sc->sc_str_var));
```

Llama a cada fetch una vez en attach después de que el valor predeterminado haya sido establecido. El fetch actualiza la variable solo si el tunable está presente.

### Resumen de flags de sysctl

| Flag | Significado |
|------|-------------|
| `CTLFLAG_RD` | solo lectura |
| `CTLFLAG_RW` | lectura y escritura |
| `CTLFLAG_TUN` | coopera con un tunable del loader en el momento del attach |
| `CTLFLAG_RDTUN` | abreviatura de solo lectura + tunable |
| `CTLFLAG_RWTUN` | abreviatura de lectura/escritura + tunable |
| `CTLFLAG_MPSAFE` | el handler es MPSAFE |
| `CTLFLAG_SKIP` | oculta el OID de los listados de `sysctl(8)` predeterminados |

### Identificadores de versión

- `MYFIRST_VERSION`: cadena de versión legible, p. ej. `"1.8-maintenance"`.
- `MODULE_VERSION(myfirst, N)`: entero utilizado por `MODULE_DEPEND`.
- `MYFIRST_IOCTL_VERSION`: entero devuelto por `MYFIRSTIOC_GETVER`; se incrementa solo cuando cambia el formato de transferencia.

### Bits de capacidad

```c
#define MYF_CAP_RESET    (1U << 0)
#define MYF_CAP_GETMSG   (1U << 1)
#define MYF_CAP_SETMSG   (1U << 2)
#define MYF_CAP_TIMEOUT  (1U << 3)

#define MYFIRSTIOC_GETCAPS  _IOR('M', 5, uint32_t)
```

### Esqueleto de limpieza con etiquetas

```c
static int
myfirst_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error;

	/* acquire resources in order */
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

	error = make_dev_s(...);
	if (error != 0)
		goto fail_mtx;

	myfirst_sysctl_attach(sc);

	error = myfirst_log_attach(sc);
	if (error != 0)
		goto fail_cdev;

	sc->sc_shutdown_tag = EVENTHANDLER_REGISTER(shutdown_pre_sync,
	    myfirst_shutdown, sc, SHUTDOWN_PRI_DEFAULT);
	if (sc->sc_shutdown_tag == NULL) {
		error = ENOMEM;
		goto fail_log;
	}

	return (0);

fail_log:
	myfirst_log_detach(sc);
fail_cdev:
	destroy_dev(sc->sc_cdev);
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
}
```

### Organización de archivos para un driver modular

```text
driver.h           public types
driver.c           module glue, cdevsw, attach/detach
driver_cdev.c      open/close/read/write
driver_ioctl.h     ioctl command numbers
driver_ioctl.c     ioctl dispatch
driver_sysctl.c    sysctl tree
driver_debug.h     DPRINTF macros
driver_log.h       rate-limit structures
driver_log.c       rate-limit helpers
```

### Lista de verificación para producción

```text
[  ] MODULE_DEPEND declared for every real dependency.
[  ] MODULE_PNP_INFO declared if the driver binds to hardware.
[  ] MOD_QUIESCE answers "can you unload?" without side effects.
[  ] devctl_notify emitted for operator-relevant events.
[  ] MAINTENANCE.md current.
[  ] devd.conf snippet included.
[  ] Every log message is device_printf, includes errno,
     and is rate-limited if it can fire in a loop.
[  ] attach/detach survives 100 load/unload cycles.
[  ] sysctls reject out-of-range values.
[  ] ioctl payload is bounds-checked.
[  ] Failure paths exercised via deliberate injection.
[  ] Versioning discipline: three independent version
     identifiers, each bumped for its own reason.
```

### IDs de subsistema de SYSINIT

| Constante | Valor | Uso |
|-----------|-------|-----|
| `SI_SUB_TUNABLES` | 0x0700000 | establece los valores de los tunables |
| `SI_SUB_KLD` | 0x2000000 | configuración de KLD y módulos |
| `SI_SUB_SMP` | 0x2900000 | arranca los APs |
| `SI_SUB_DRIVERS` | 0x3100000 | permite que los drivers se inicialicen |
| `SI_SUB_CONFIGURE` | 0x3800000 | configura los dispositivos |

Dentro de un subsistema:
- `SI_ORDER_FIRST` = 0x0
- `SI_ORDER_SECOND` = 0x1
- `SI_ORDER_MIDDLE` = 0x1000000
- `SI_ORDER_ANY` = 0xfffffff

### Prioridades de eventos de apagado

- `SHUTDOWN_PRI_FIRST`: se ejecuta al principio.
- `SHUTDOWN_PRI_DEFAULT`: valor predeterminado.
- `SHUTDOWN_PRI_LAST`: se ejecuta al final.

### Esqueleto de EVENTHANDLER

```c
sc->sc_tag = EVENTHANDLER_REGISTER(shutdown_pre_sync,
    my_handler, sc, SHUTDOWN_PRI_DEFAULT);
/* ... in detach ... */
EVENTHANDLER_DEREGISTER(shutdown_pre_sync, sc->sc_tag);
```

### Jerarquía de nombres de tunables

Los tunables y sysctls siguen una convención de nombres jerárquica. La tabla a continuación lista los nodos que introduce este capítulo:

| Nombre | Tipo | Propósito |
|--------|------|-----------|
| `hw.myfirst.debug_mask_default` | tunable | máscara de depuración inicial para cada instancia |
| `hw.myfirst.timeout_sec` | tunable | tiempo de espera de operación inicial en segundos |
| `hw.myfirst.max_retries` | tunable | número de reintentos inicial |
| `hw.myfirst.log_ratelimit_pps` | tunable | límite de mensajes por segundo inicial |
| `dev.myfirst.<unit>.version` | sysctl (RD) | cadena de versión |
| `dev.myfirst.<unit>.open_count` | sysctl (RD) | recuento de descriptores de archivo activos |
| `dev.myfirst.<unit>.total_reads` | sysctl (RD) | llamadas de lectura totales |
| `dev.myfirst.<unit>.total_writes` | sysctl (RD) | llamadas de escritura totales |
| `dev.myfirst.<unit>.message` | sysctl (RD) | contenido actual del buffer |
| `dev.myfirst.<unit>.message_len` | sysctl (RD) | longitud actual del buffer |
| `dev.myfirst.<unit>.timeout_sec` | sysctl (RWTUN) | tiempo de espera en tiempo de ejecución |
| `dev.myfirst.<unit>.max_retries` | sysctl (RWTUN) | número de reintentos en tiempo de ejecución |
| `dev.myfirst.<unit>.log_ratelimit_pps` | sysctl (RWTUN) | límite de pps en tiempo de ejecución |
| `dev.myfirst.<unit>.debug.mask` | sysctl (RWTUN) | máscara de depuración en tiempo de ejecución |
| `dev.myfirst.<unit>.debug.classes` | sysctl (RD) | nombres de clase y valores de bits |

Lee la tabla como el contrato de interfaz. La familia `hw.myfirst.*` se establece en el boot; la familia `dev.myfirst.*` se ajusta en tiempo de ejecución. Cada entrada modificable tiene su correspondiente entrada de solo lectura que el operador puede usar para confirmar el valor actual.

### Jerarquía de comandos ioctl

El header ioctl del Capítulo 25 define estos comandos bajo la clave mágica `'M'`:

| Comando | Número | Dirección | Propósito |
|---------|--------|-----------|-----------|
| `MYFIRSTIOC_GETVER` | 0 | lectura | devuelve `MYFIRST_IOCTL_VERSION` |
| `MYFIRSTIOC_RESET` | 1 | sin datos | pone a cero los contadores de lectura/escritura |
| `MYFIRSTIOC_GETMSG` | 2 | lectura | copia el mensaje actual hacia el usuario |
| `MYFIRSTIOC_SETMSG` | 3 | escritura | copia el nuevo mensaje desde el usuario |
| (retired) | 4 | n/a | reservado; no reutilizar |
| `MYFIRSTIOC_GETCAPS` | 5 | lectura | máscara de bits de capacidad |

Añadir un nuevo comando implica elegir el siguiente número no utilizado. Retirar un comando significa dejar su número en la jerarquía con `(retired)` junto a él, sin reutilizar el número.

## Análisis de drivers reales

El capítulo, hasta este punto, ha desarrollado su disciplina en torno al pseudo-driver `myfirst`. Esta sección cambia de perspectiva y examina cómo las mismas disciplinas aparecen en drivers que se distribuyen como parte de FreeBSD 14.3. Cada análisis parte de un archivo de código fuente real en `/usr/src`, identifica el patrón del Capítulo 25 que está en juego y señala las líneas donde ese patrón es visible. El objetivo no es documentar los drivers concretos (su propia documentación ya se encarga de eso), sino demostrar que los hábitos del Capítulo 25 no son inventados: son los hábitos que los drivers reales del árbol ya practican.

Leer drivers reales con un vocabulario de patrones es la manera más rápida de desarrollar tu propio criterio. Una vez que reconoces `ppsratecheck` de un vistazo, cada driver que lo utiliza se lee con mayor rapidez.

### `mmcsd(4)`: Registro de errores con limitación de tasa en una ruta caliente

El driver `mmcsd` en `/usr/src/sys/dev/mmc/mmcsd.c` gestiona el almacenamiento en tarjetas MMC y SD. El sistema de archivos situado por encima genera un flujo continuo de I/O de bloque, y cada bloque que falla en la petición MMC subyacente puede producir una línea en el registro de errores. Sin limitación de tasa, una tarjeta lenta o inestable inundaría `dmesg` en cuestión de segundos.

El driver declara su estado de limitación de tasa por softc, tal como recomienda el capítulo:

```c
struct mmcsd_softc {
	...
	struct timeval log_time;
	int            log_count;
	...
};
```

`log_time` y `log_count` son el estado de `ppsratecheck`. Cada ruta caliente que emite un mensaje de registro envuelve el `device_printf` de la misma forma:

```c
#define LOG_PPS  5 /* Log no more than 5 errors per second. */

...

if (req.cmd->error != MMC_ERR_NONE) {
	if (ppsratecheck(&sc->log_time, &sc->log_count, LOG_PPS))
		device_printf(dev, "Error indicated: %d %s\n",
		    req.cmd->error,
		    mmcsd_errmsg(req.cmd->error));
	...
}
```

El patrón es exactamente la forma `DLOG_RL` que introdujo el capítulo, con la macro expandida en el propio lugar. `LOG_PPS` se fija en 5 mensajes por segundo, y el estado reside en el softc para que las llamadas repetidas en la ruta caliente compartan el mismo bucket.

Hay tres observaciones que conviene retener. Primera: este patrón no es teórico; un driver de FreeBSD en producción lo usa en una ruta caliente que puede dispararse miles de veces por segundo. Segunda: la elección entre macro e inline es una cuestión de gusto; `mmcsd.c` escribe la llamada directamente, y el patrón resulta igual de legible. Tercera: la constante `LOG_PPS` es conservadora (5 por segundo); el autor prefirió menos mensajes a más. El autor de un driver puede ajustar el límite de pps para adaptarlo a la tasa de error esperada y a la tolerancia del operador.

### `uftdi(4)`: Dependencias de módulo y metadatos PNP

El driver `uftdi` en `/usr/src/sys/dev/usb/serial/uftdi.c` se enlaza a adaptadores USB-serie basados en chips FTDI. Es un ejemplo de manual de un driver que depende de otro módulo del kernel: no puede funcionar sin la pila USB.

Cerca del final del archivo:

```c
MODULE_DEPEND(uftdi, ucom, 1, 1, 1);
MODULE_DEPEND(uftdi, usb, 1, 1, 1);
MODULE_VERSION(uftdi, 1);
```

Se declaran dos dependencias. La primera es hacia `ucom`, el marco genérico USB-serie sobre el que se construye `uftdi`. La segunda es hacia `usb`, el núcleo USB. Ambas están limitadas exactamente a la versión 1. Cargar `uftdi.ko` en un kernel sin `usb` ni `ucom` falla con un error claro; cargarlo en un kernel donde la versión del subsistema ha superado la 1 también falla hasta que se actualice la propia declaración de `uftdi`.

Los metadatos PNP se publican mediante una macro que se expande a `MODULE_PNP_INFO`:

```c
USB_PNP_HOST_INFO(uftdi_devs);
```

`USB_PNP_HOST_INFO` es el helper específico de USB definido en `/usr/src/sys/dev/usb/usbdi.h`. Se expande a `MODULE_PNP_INFO` con la cadena de formato correcta para las tuplas de vendor/producto USB. `uftdi_devs` es un array estático de entradas `struct usb_device_id`, una por cada triple (vendor, producto, interfaz) que gestiona el driver.

Este es el patrón de preparación para producción del Capítulo 25, Sección 7, aplicado a un driver de hardware real: dependencias declaradas, metadatos publicados, entero de versión presente. Cuando aparece un nuevo adaptador USB serie en el sistema, `devd(8)` consulta los metadatos PNP, identifica `uftdi` como el driver y lo carga si no está ya cargado. El mecanismo es completamente automático una vez que los metadatos son correctos.

La versión de `myfirst` del Capítulo 26 usará el mismo patrón y el mismo helper.

### `iscsi(4)`: Manejador de apagado registrado en `attach`

El iniciador iSCSI en `/usr/src/sys/dev/iscsi/iscsi.c` mantiene conexiones abiertas con destinos de almacenamiento remoto. Cuando el sistema se apaga, el iniciador debe cerrar esas conexiones de forma ordenada antes de que se desmonte la capa de red; de lo contrario, el extremo remoto queda con sesiones obsoletas.

El manejador de apagado se registra en attach:

```c
sc->sc_shutdown_pre_eh = EVENTHANDLER_REGISTER(shutdown_pre_sync,
    iscsi_shutdown_pre, sc, SHUTDOWN_PRI_FIRST);
```

Dos detalles son importantes. Primero, la etiqueta del registro se almacena en el softc (`sc->sc_shutdown_pre_eh`), de modo que la posterior cancelación del registro pueda referenciarlo. Segundo, la prioridad es `SHUTDOWN_PRI_FIRST`, no `SHUTDOWN_PRI_DEFAULT`: el driver iSCSI quiere cerrar las conexiones antes de que nadie más comience el trabajo de apagado, porque las conexiones de almacenamiento necesitan tiempo para cerrarse de forma limpia.

La cancelación del registro ocurre en la ruta de detach:

```c
EVENTHANDLER_DEREGISTER(shutdown_pre_sync, sc->sc_shutdown_pre_eh);
```

Un registro, una cancelación. La etiqueta en el softc los mantiene vinculados.

En el caso de `myfirst`, la demostración del capítulo usó `SHUTDOWN_PRI_DEFAULT` porque no hay ningún motivo sólido para que el pseudo-driver se ejecute antes que los demás. Los drivers reales eligen una prioridad según quién depende de quién: los drivers que deben silenciarse antes que otros eligen `SHUTDOWN_PRI_FIRST`; los que dependen de que los sistemas de archivos estén intactos eligen `SHUTDOWN_PRI_LAST`. La prioridad es una decisión de diseño, e `iscsi` muestra una manera de tomarla.

### `ufs_dirhash`: Desalojo de caché con `vm_lowmem`

La caché de hash de directorios UFS en `/usr/src/sys/ufs/ufs/ufs_dirhash.c` es un acelerador en memoria por sistema de archivos para búsquedas de directorios. En condiciones normales la caché es beneficiosa; bajo presión de memoria se convierte en un lastre, por lo que el subsistema registra un manejador `vm_lowmem` que descarta entradas de caché:

```c
EVENTHANDLER_REGISTER(vm_lowmem, ufsdirhash_lowmem, NULL,
    EVENTHANDLER_PRI_FIRST);
```

El cuarto argumento es `EVENTHANDLER_PRI_FIRST`, que solicita ejecutarse antes en la lista de manejadores `vm_lowmem` registrados. El autor de dirhash eligió ejecución temprana porque la caché es memoria recuperable pura: liberarla con rapidez da a otros manejadores (que pueden tener un estado más sucio o menos fácil de liberar) una mejor oportunidad de completarse.

El callback en sí realiza el trabajo real: recorre las tablas de hash, libera las entradas que se pueden liberar y contabiliza la memoria liberada. El punto de diseño fundamental es que el callback no entra en pánico si no hay nada que liberar; simplemente retorna sin haber hecho nada.

Este es el patrón `vm_lowmem` del Capítulo 25 en un subsistema que no es un driver de dispositivo pero que comparte la misma disciplina. Las lecciones son transferibles: si `myfirst` algún día tiene una caché, la forma ya está aquí.

### `tcp_subr`: `vm_lowmem` sin softc

El subsistema TCP en `/usr/src/sys/netinet/tcp_subr.c` también se registra para `vm_lowmem`, pero su registro es diferente de una forma instructiva:

```c
EVENTHANDLER_REGISTER(vm_lowmem, tcp_drain, NULL, LOWMEM_PRI_DEFAULT);
```

El tercer argumento (los datos del callback) es `NULL`, no un puntero a softc. El subsistema TCP no tiene un softc único; su estado está distribuido entre muchas estructuras. El callback debe encontrar su estado por otros medios (variables globales, variables por CPU, búsquedas en tablas de hash).

Esto plantea una pregunta que el capítulo insinuó: ¿cuándo es aceptable pasar `NULL` como argumento del callback? La respuesta es: cuando el callback tiene otra forma de encontrar su estado. Para un driver con un softc por dispositivo, pasar `sc` es casi siempre la elección correcta. Para un subsistema con estado global, pasar `NULL` y dejar que el callback use sus variables globales conocidas está bien.

`myfirst` siempre pasará `sc` porque `myfirst` es un driver por dispositivo. Un lector que se encuentre escribiendo un callback a nivel de subsistema debe reconocer que el patrón cambia de formas sutiles cuando el sujeto es global.

### `if_vtnet`: Registro con limitación de tasa para un fallo específico

El driver de red VirtIO en `/usr/src/sys/dev/virtio/network/if_vtnet.c` ofrece un contrapunto más estrecho pero instructivo respecto a `mmcsd`. Mientras que `mmcsd` envuelve cada error de ruta caliente en registro con limitación de tasa, `if_vtnet` recurre a `ppsratecheck` solo en torno a un comportamiento anómalo específico: un paquete TSO con los bits ECN activos que el host VirtIO no ha negociado. El punto de llamada es pequeño y autocontenido:

```c
static struct timeval lastecn;
static int curecn;
...
if (ppsratecheck(&lastecn, &curecn, 1))
        if_printf(sc->vtnet_ifp,
            "TSO with ECN not negotiated with host\n");
```

Dos detalles merecen ser destacados. Primero, el estado de limitación de tasa se declara `static` en el ámbito del archivo, no por softc. El autor decidió que "el host VirtIO no está de acuerdo con el invitado sobre ECN" es una configuración incorrecta a nivel de sistema más que un fallo por interfaz, por lo que un bucket compartido es suficiente; el estado por softc permitiría que la avalancha de una sola interfaz virtual privara a otra de sus mensajes. Segundo, el límite es de 1 pps, lo que es deliberadamente restrictivo: el aviso es informativo y no debería aparecer más de una vez por segundo en todo el sistema. Un diseñador de drivers que esperara que el aviso se disparara con frecuencia podría aumentar el límite.

FreeBSD también proporciona `ratecheck(9)`, el hermano de conteo de eventos de `ppsratecheck`. `ratecheck` se activa cuando el tiempo transcurrido desde el último evento permitido supera un umbral; `ppsratecheck` se activa cuando la tasa de ráfaga reciente está por debajo de un límite. Son complementarios: `ratecheck` es mejor cuando se quiere un intervalo mínimo entre mensajes, mientras que `ppsratecheck` es mejor cuando se quiere una tasa de ráfaga máxima.

La conclusión de `if_vtnet` es que el estado de limitación de tasa puede ser global o por instancia, y la elección sigue la forma esperada del fallo. El Capítulo 25 puso el estado en el softc porque los errores de `myfirst` son por instancia; un driver diferente podría razonablemente tomar una decisión distinta.

### `vt_core`: Múltiples registros de `EVENTHANDLER` en secuencia

El subsistema de terminal virtual en `/usr/src/sys/dev/vt/vt_core.c` registra múltiples manejadores de eventos en distintos momentos de su ciclo de vida. El manejador de apagado para la ventana de consola es uno de ellos:

```c
EVENTHANDLER_REGISTER(shutdown_pre_sync, vt_window_switch,
    vw, SHUTDOWN_PRI_DEFAULT);
```

`SHUTDOWN_PRI_DEFAULT` es la prioridad neutral: el cambio VT se ejecuta después de cualquier cosa que haya pedido `SHUTDOWN_PRI_FIRST` y antes de cualquier cosa que haya pedido `SHUTDOWN_PRI_LAST`. La elección es deliberada: el cambio de terminal no tiene ningún requisito de ordenación respecto a otros subsistemas, por lo que el autor eligió el valor predeterminado en lugar de reclamar una prioridad que el driver no necesitaba.

Lo relevante para nosotros es que `vt_core` registra este manejador una sola vez, en el momento del boot, y nunca lo cancela. Un driver cuya vida útil es la del kernel no necesita cancelar el registro; el kernel nunca llama al manejador después de que haya desaparecido. Un driver como `myfirst` que puede cargarse y descargarse como módulo sí necesita cancelar el registro, porque descargar el módulo destruye el código del manejador. La regla es: cancela el registro si tu código puede desaparecer mientras el kernel sigue ejecutándose.

Esta distinción es importante para los autores de módulos. Las rutas de código integradas a menudo parecen que les falta la cancelación del registro, pero no es así; simplemente no la necesitan. Un módulo que siga el patrón integrado al pie de la letra entrará en pánico al descargarse.

### FFS (`ffs_alloc.c`): Buckets de limitación de tasa por condición

El asignador FFS en `/usr/src/sys/ufs/ffs/ffs_alloc.c` es un sistema de archivos y no un driver de dispositivo, pero se enfrenta exactamente al problema de inundación de registros que aborda el Capítulo 25. Un disco que se queda repetidamente sin bloques o inodos puede emitir un error por cada llamada `write(2)` fallida, lo que en la práctica no tiene límite. El asignador recurre a `ppsratecheck` en cuatro lugares distintos, y la forma en que delimita el estado de limitación de tasa es una buena lección en el diseño de buckets.

Cada sistema de archivos montado lleva el estado de limitación de tasa en su estructura de montaje. Dos buckets separados gestionan dos tipos distintos de error:

```c
/* "Filesystem full" reports: blocks or inodes exhausted. */
um->um_last_fullmsg
um->um_secs_fullmsg

/* Cylinder-group integrity reports. */
um->um_last_integritymsg
um->um_secs_integritymsg
```

El bucket de "sistema de archivos lleno" es compartido entre dos rutas de código (`ffs_alloc` y `ffs_realloccg`, ambas escribiendo un mensaje del tipo "write failed, filesystem is full") más el agotamiento de inodos. El bucket de integridad es compartido entre dos fallos de integridad distintos (discrepancia en el checkhash de cilindro y discrepancia en el número mágico). En cada punto de llamada la forma es idéntica:

```c
if (ppsratecheck(&ump->um_last_fullmsg,
    &ump->um_secs_fullmsg, 1)) {
        UFS_UNLOCK(ump);
        ffs_fserr(fs, ip->i_number, "filesystem full");
        uprintf("\n%s: write failed, filesystem is full\n",
            fs->fs_fsmnt);
        ...
}
```

Son visibles tres decisiones de diseño. Primera, el estado de limitación de tasa es por punto de montaje, no global. Un sistema de archivos con mucha escritura no debería suprimir mensajes de otro sistema de archivos que también se está llenando. Segunda, el límite es de 1 pps: como máximo un mensaje por segundo por punto de montaje y por bucket. Tercera, los mensajes relacionados comparten un bucket (todos los mensajes de "plenitud"; todos los de "integridad"), mientras que los no relacionados tienen el suyo propio. El autor de `ffs_alloc.c` decidió que un operador que recibe una avalancha de mensajes "filesystem full" no debe recibir también una avalancha de "out of inodes" para el mismo punto de montaje; los dos son síntomas de la misma condición y un mensaje por segundo es suficiente.

Un pseudo-driver como `myfirst` puede tomar prestado el patrón directamente. Si `myfirst` algún día desarrolla una clase de errores relacionados con la capacidad (por ejemplo, "buffer full" para la ruta de escritura y "no free slots" para la ruta de ioctl), esos pertenecen al mismo bucket. Un fallo completamente diferente (por ejemplo, "command version mismatch") merece el suyo propio. La disciplina que `ffs_alloc.c` aplica a un sistema de archivos se traslada sin cambios a un driver de dispositivo.

### Lectura de más drivers

Cada driver de FreeBSD es un estudio de caso de cómo un equipo concreto resolvió los problemas que el Capítulo 25 nombra. Hay algunas zonas de `/usr/src/sys` especialmente ricas para buscar patrones:

- `/usr/src/sys/dev/usb/` para drivers USB: `MODULE_DEPEND` y `MODULE_PNP_INFO` por todas partes.
- `/usr/src/sys/dev/pci/` para drivers PCI: rutinas attach con limpieza por etiquetas a escala industrial.
- `/usr/src/sys/dev/cxgbe/` como ejemplo de un driver moderno y complejo: logging con limitación de tasa, árboles sysctl con cientos de OIDs, y versionado mediante ABI de módulo.
- `/usr/src/sys/netinet/` para uso de `EVENTHANDLER` a nivel de subsistema.
- `/usr/src/sys/kern/subr_*.c` como ejemplos de `SYSINIT` con muchos identificadores de subsistema distintos.

Cuando leas un driver nuevo, empieza por encontrar su función attach. Cuenta las adquisiciones y las etiquetas; deberían coincidir. Encuentra la función detach. Comprueba que libera los recursos en orden inverso. Encuentra los caminos de error y observa qué errno devuelve cada uno. Busca `ppsratecheck` o `ratecheck` cerca de cualquier mensaje de log que se dispare en un hot path. Busca declaraciones `MODULE_DEPEND`. Busca `EVENTHANDLER_REGISTER` y confirma que existe una baja correspondiente.

Cada una de estas inspecciones lleva apenas unos segundos cuando los patrones ya son familiares. Cada una refuerza tu propio instinto para reconocer cuándo un patrón se aplica correctamente y cuándo no.

### Lo que no verás

Algunos patrones que el capítulo 25 recomienda no aparecen en todos los drivers, y su ausencia no siempre indica un error. Saber qué patrones son opcionales evita que los esperes en todas partes.

`MAINTENANCE.md` es un hábito recomendado por este libro, no un requisito de FreeBSD. La mayoría de los drivers incluidos en el árbol no incluyen un archivo de mantenimiento por driver; en su lugar, la página del manual es la referencia para el operador, y las notas de versión recogen el registro de cambios. Ambas soluciones funcionan; la elección entre ellas es una convención del proyecto.

`devctl_notify` es opcional. Muchos drivers no emiten ningún evento, y no hay ninguna regla que los obligue a hacerlo. El patrón resulta valioso cuando existen eventos ante los que los operadores querrían reaccionar; para drivers con un comportamiento silencioso y sin cambios de estado visibles para el operador, emitir eventos es innecesario.

`SYSINIT` fuera de `DRIVER_MODULE` es poco frecuente en los drivers modernos. La mayor parte del trabajo a nivel de driver ocurre en `device_attach`, que se ejecuta por instancia. Los registros explícitos de `SYSINIT` son más habituales en subsistemas y en el código central del kernel; los drivers individuales raramente necesitan uno. El capítulo 25 presentó `SYSINIT` porque el lector acabará encontrándolo, no porque la mayoría de los drivers lo utilicen.

El manejador de eventos de módulo explícito (una función `MOD_LOAD`/`MOD_UNLOAD` suministrada por el driver en lugar de `DRIVER_MODULE`) también es poco frecuente. Se utiliza cuando un driver necesita un comportamiento personalizado en el momento del boot que no encaja en el modelo Newbus, pero la mayoría de los drivers utilizan sin problema el comportamiento por defecto.

Cuando leas un driver que omite uno de estos patrones, la ausencia suele reflejar una decisión de diseño específica de ese driver, no una falta de disciplina. Los patrones son herramientas; no todas las herramientas se necesitan en cada trabajo.

### Cerrando los recorridos

Cada patrón que presentó el capítulo es visible en algún lugar del árbol de código fuente de FreeBSD. El driver `mmcsd` limita la tasa de sus registros en el camino crítico. El driver `uftdi` declara sus dependencias de módulo y sus metadatos PNP. El driver `iscsi` registra un manejador `shutdown_pre_sync` con prioridad. La caché dirhash de UFS libera memoria ante `vm_lowmem`. Cada uno de estos es una aplicación real, en producción y probada de una disciplina del capítulo 25.

Leer estos drivers con un vocabulario de patrones acelera tu propia intuición más rápido de lo que podría hacerlo cualquier libro de un solo autor. Los patrones se repiten; los drivers difieren. Una vez que puedes nombrar el patrón, cada nuevo driver que lees lo refuerza.

El driver `myfirst` al final del capítulo 25 exhibe todas las disciplinas. Acabas de ver las mismas disciplinas aplicadas en otros ocho drivers. El siguiente paso es aplicarlas tú mismo, en un driver que no está en este libro. Ese es el trabajo de una carrera, y los cimientos están ahora en tus manos.

## Cerrando

El capítulo 25 es el más largo de la parte 5, y su extensión es deliberada. Cada sección presentó una disciplina que transforma un driver funcional en uno mantenible. Ninguna de las disciplinas es glamurosa; cada una es el hábito concreto que mantiene un driver en funcionamiento cuando lo utilizan personas distintas a su autor.

El capítulo comenzó con el registro con limitación de tasa. Un driver que no puede saturar la consola es un driver cuyos mensajes de registro merece la pena leer. `ppsratecheck(9)` y la macro `DLOG_RL` hacen que la disciplina sea mecánica: coloca el estado de limitación de tasa en el softc, envuelve los mensajes del camino crítico con la macro y deja que el kernel lleve la contabilidad por segundo. Todo lo que viene después del registro se beneficia, porque un registro que puedes leer es un registro desde el que puedes depurar.

La segunda sección nombró los valores errno y los distinguió. `ENOTTY` no es `EINVAL`; `EPERM` no es `EACCES`; `ENOIOCTL` es la señal especial del kernel que indica que un driver no reconoció un comando ioctl y quiere que el kernel intente otro manejador antes de rendirse. Conocer el vocabulario transforma los informes de errores vagos en precisos, y los informes precisos llegan a las causas raíz más rápido.

La sección 3 trató la configuración como una preocupación de primer orden. Un tunable es un valor inicial en el momento del boot; un sysctl es un punto de control en tiempo de ejecución. Ambos cooperan a través de `TUNABLE_*_FETCH` y `CTLFLAG_TUN`. Un driver que expone exactamente los tunables y sysctls correctos da a los operadores suficiente control para resolver sus propios problemas sin tener que modificar el código fuente. El par `hw.myfirst.timeout_sec` y `dev.myfirst.0.timeout_sec` es ahora la plantilla que seguirá cada tunable futuro.

La sección 4 nombró los tres identificadores de versión que necesita un driver (cadena de versión de lanzamiento, entero de módulo, entero wire de ioctl) y señaló que cambian por razones distintas. El ioctl `GETCAPS` proporciona al espacio de usuario un mecanismo de descubrimiento en tiempo de ejecución que sobrevive a la adición y eliminación de capacidades a lo largo del tiempo, y la macro `MODULE_DEPEND` hace explícita la compatibilidad entre módulos. El propio salto del driver `myfirst` de la versión 1.7 a la 1.8 es un caso de estudio pequeño pero completo.

La sección 5 nombró el patrón del goto etiquetado y lo convirtió en la disciplina incondicional para attach y detach. Cada recurso recibe una etiqueta; cada etiqueta cae al siguiente; el único `return (0)` separa la cadena de adquisición de la cadena de limpieza. El patrón escala desde dos recursos hasta una docena sin cambiar de forma. La inyección deliberada de fallos confirma que cada etiqueta es alcanzable.

La sección 6 dividió `myfirst` en archivos por ámbito. Un archivo por responsabilidad, una regla de responsabilidad única para decidir dónde colocar el código nuevo, y un pequeño encabezado público que contiene solo las declaraciones que necesita cada archivo. El driver se encuentra ahora en la fase dos de la división típica en múltiples archivos, y las reglas para alcanzar la fase tres se nombran explícitamente.

La sección 7 dirigió la atención hacia el exterior. La lista de verificación de preparación para producción cubrió las dependencias de módulo, los metadatos PNP, `MOD_QUIESCE`, `devctl_notify`, `MAINTENANCE.md`, las reglas de `devd`, la calidad de los mensajes de registro, la prueba de regresión de 100 ciclos, la validación de entradas, el ejercicio del camino de fallos y la disciplina de versiones. Cada elemento de la lista es un hábito concreto que detecta un modo de fallo concreto. El driver `myfirst` al final del capítulo 25 supera todos los elementos.

La sección 8 cerró el círculo con `SYSINIT(9)` y `EVENTHANDLER(9)`. Un driver que necesita participar en el ciclo de vida del kernel más allá de su propia ventana de attach/detach dispone de un mecanismo limpio para hacerlo. `myfirst` se registra en `shutdown_pre_sync` como demostración; otros drivers se registran en `vm_lowmem`, suspend/resume, o eventos personalizados.

El driver ha evolucionado desde un único archivo con un buffer de mensajes hasta convertirse en un pseudodispositivo modular, observable, con disciplina de versiones, con limitación de tasa y listo para producción. La mecánica de la autoría de drivers de caracteres en FreeBSD está ahora en manos del lector.

Lo que aún no se ha cubierto es el mundo del hardware. Todos los capítulos hasta ahora han utilizado un pseudodriver; el dispositivo de caracteres respalda un buffer de software y un conjunto de contadores. Un driver que realmente habla con hardware tiene que asignar recursos del bus, mapear ventanas de I/O, conectar manejadores de interrupciones, programar motores DMA y sobrevivir a los modos de fallo específicos de los dispositivos reales. El capítulo 26 comienza ese trabajo.

## Punto de control de la parte 5

La parte 5 cubrió la depuración, el rastreo y las prácticas de ingeniería que separan un driver que simplemente compila de uno que un equipo puede mantener. Antes de que la parte 6 cambie el terreno al conectar `myfirst` a transportes reales, confirma que los hábitos de la parte 5 están en tus dedos y no solo en tus notas.

Al final de la parte 5 deberías sentirte cómodo haciendo cada uno de los siguientes:

- Investigar el comportamiento del driver con la herramienta adecuada para cada pregunta: `ktrace` y `kdump` para las llamadas al sistema por proceso, `ddb` para la inspección del kernel en vivo o post-mortem, `kgdb` con un volcado de memoria para kernels que han fallado, `dtrace` y sondas SDT para el rastreo dentro del kernel sin cambios en el código fuente, y `procstat` junto con `lockstat` para vistas de estado y contención.
- Leer un mensaje de pánico y extraer las pistas correctas, convirtiendo un stack trace en un caso de prueba reproducible en lugar de un juego de adivinanzas.
- Construir y ejecutar la pila de integración `myfirst` del capítulo 24: un driver que expone sus superficies a través de ioctl, sysctl, sondas DTrace y `devctl_notify` mientras ejercita un camino completo de inyección del ciclo de vida.
- Aplicar la lista de verificación de preparación para producción del capítulo 25 elemento a elemento: `MODULE_DEPEND`, metadatos PNP, `MOD_QUIESCE` como manejador separado, `devctl_notify`, `MAINTENANCE.md`, reglas de `devd`, calidad de los mensajes de registro, la prueba de regresión de 100 ciclos, validación de entradas, ejercicio deliberado del camino de fallos y disciplina de versiones.
- Utilizar `SYSINIT(9)` y `EVENTHANDLER(9)` cuando un driver debe participar en el ciclo de vida del kernel más allá de su propia ventana de attach y detach, incluyendo el registro y la cancelación del registro de forma ordenada.

Si alguno de estos todavía te parece poco sólido, los laboratorios que debes revisar son:

- Herramientas de depuración en práctica: Lab 23.1 (Una primera sesión con DDB), Lab 23.2 (Midiendo el driver con DTrace), Lab 23.4 (Encontrando una fuga de memoria con vmstat -m) y Lab 23.5 (Instalando la refactorización 1.6-debug).
- Integración y observabilidad: Lab 1 (Construir y cargar el driver de la etapa 1), Lab 4 (Recorrer el ciclo de vida inyectando un fallo), Lab 5 (Rastrear las superficies de integración con DTrace) y Lab 6 (Prueba de humo de integración) en el capítulo 24.
- Disciplina de producción: Lab 3 (Comportamiento configurable en el reinicio), Lab 4 (Inyección deliberada de fallos en attach), Lab 6 (El script de regresión de 100 ciclos) y Lab 10 (Matriz de compatibilidad multiversión) en el capítulo 25.

La parte 6 espera lo siguiente como base:

- Confianza para leer un pánico y seguirlo hasta su causa raíz, ya que tres nuevos transportes introducirán tres nuevas formas en que las cosas pueden salir mal.
- Un `myfirst` que supere la lista de verificación de producción del capítulo 25, de modo que el capítulo 26 pueda añadir USB sin que la disciplina anterior tambalee bajo la nueva carga.
- Conocimiento de que la parte 6 cambia el patrón del ejemplo continuo: el hilo de `myfirst` se extenderá a lo largo del capítulo 26 como `myfirst_usb`, luego se pausará mientras los capítulos 27 y 28 utilizan demostraciones nuevas orientadas a cada transporte. La disciplina continúa; solo cambia el artefacto de código. El capítulo 29 vuelve a la forma acumulativa.

Si eso se cumple, estás listo para la parte 6. Si alguno todavía parece inestable, la solución es un único laboratorio bien elegido, no avanzar precipitadamente.

## Puente al capítulo 26

El capítulo 26 dirige el driver `myfirst` hacia el exterior. En lugar de servir un buffer en RAM, el driver se conectará a un bus real y atenderá a un dispositivo real. El primer objetivo hardware del libro es el subsistema USB: USB es ubicuo, está bien documentado y tiene una interfaz del kernel limpia que resulta más fácil como punto de partida que PCI o ISA.

Los hábitos que has adquirido en el capítulo 25 se trasladan sin cambios. El patrón del goto etiquetado en attach escala a las asignaciones de recursos del bus: `bus_alloc_resource`, `bus_setup_intr`, `bus_dma_tag_create`, cada uno se convierte en otra adquisición en la cadena, cada uno con su propia etiqueta. La estructura modular de archivos se extiende de forma natural: `myfirst_usb.c` se une a `myfirst.c`, `myfirst_cdev.c` y el resto. La lista de verificación de producción añade dos elementos: `MODULE_DEPEND(myfirst_usb, usb, ...)` y `MODULE_PNP_INFO` para los identificadores de fabricante y producto que gestiona el driver. La disciplina de versiones continúa; el driver del capítulo 26 será la versión 1.9-usb.

Lo que es verdaderamente nuevo en el capítulo 26 no es la estructura del driver; son las interfaces entre el driver y el subsistema USB. Conocerás `usb_request_methods`, los callbacks de configuración de transferencia, la separación entre transferencias de control, bulk, interrupción e isócrona, y el ciclo de vida específico de USB (attach es por dispositivo, igual que en Newbus; detach es por dispositivo, igual que en Newbus; el hot-plug es la condición de operación normal, a diferencia de la mayoría de los otros buses).

Antes de comenzar el capítulo 26, haz una pausa y confirma que el material del capítulo 25 está bien asentado. Los laboratorios están pensados para ejecutarse; ejecutarlos es la mejor manera de encontrar las partes del capítulo que no han quedado claras. Si has ejecutado los laboratorios del 1 al 7 y los ejercicios de desafío están claros, estás listo para el hardware.

El capítulo 26 comienza con un dispositivo USB sencillo (un adaptador serie FTDI, lo más probable) y recorre paso a paso las rutas de probe, attach, configuración de transferencias bulk, despacho de lectura/escritura y detach con desconexión en caliente. Al final del capítulo 26, `myfirst_usb.ko` servirá bytes reales desde un cable real, y la disciplina del capítulo 25 mantendrá el código en buen estado.

## Glosario

Los términos de este glosario aparecen todos en el Capítulo 25 y merece la pena conocerlos por su nombre. Las definiciones son breves a propósito; el cuerpo del capítulo es donde se desarrolla cada concepto en profundidad.

**Cadena de attach.** La secuencia ordenada de adquisiciones de recursos en el método `device_attach` de un driver. Cada adquisición que puede fallar va seguida de un goto a la etiqueta que deshace los recursos adquiridos previamente.

**Bitmask de capacidades.** Un entero de 32 bits (o 64 bits) devuelto por `MYFIRSTIOC_GETCAPS`, con un bit por funcionalidad opcional. Permite que el espacio de usuario consulte al driver en tiempo de ejecución qué funcionalidades admite.

**Cadena de limpieza.** La secuencia ordenada de etiquetas al final del método `device_attach` de un driver. Cada etiqueta libera un recurso y deja que la ejecución continúe hasta la siguiente. Es el inverso del orden de adquisición.

**`CTLFLAG_SKIP`.** Un flag de sysctl que oculta un OID de los listados por defecto de `sysctl(8)`. El OID sigue siendo legible si se proporciona su nombre completo de forma explícita. Definido en `/usr/src/sys/sys/sysctl.h`.

**`CTLFLAG_RDTUN` / `CTLFLAG_RWTUN`.** Abreviaturas de `CTLFLAG_RD | CTLFLAG_TUN` y `CTLFLAG_RW | CTLFLAG_TUN` respectivamente. Declaran un sysctl que coopera con un tunable del cargador.

**`devctl_notify`.** Función del kernel que emite un evento estructurado que puede leer `devd(8)`. Permite que un driver notifique a los daemons del espacio de usuario sobre cambios de estado relevantes.

**`DLOG_RL`.** Macro que envuelve `ppsratecheck` y `device_printf` para limitar un mensaje de log a una tasa determinada de mensajes por segundo.

**Errno.** Un entero positivo pequeño que representa un modo de fallo específico. La tabla de errno de FreeBSD se encuentra en `/usr/src/sys/sys/errno.h`.

**Event handler.** Un callback registrado con `EVENTHANDLER_REGISTER` que se ejecuta cada vez que el kernel dispara un evento con nombre. Se da de baja con `EVENTHANDLER_DEREGISTER`.

**`EVENTHANDLER(9)`.** El framework genérico de notificación de eventos de FreeBSD. Define eventos, permite a los subsistemas publicarlos y permite a los drivers suscribirse a ellos.

**Inyección de fallos.** Técnica de prueba deliberada que provoca el fallo de un camino de código para ejercitar su limpieza. Normalmente se implementa como un `return` condicional protegido por un `#ifdef`.

**Patrón goto etiquetado.** Véase "cadena de attach" y "cadena de limpieza". La forma idiomática de FreeBSD para attach y detach que usa `goto label;` para el desenrollado lineal en lugar de `if` anidados.

**`MOD_QUIESCE`.** El evento de módulo que pregunta "¿puedes ser descargado ahora?". Un driver devuelve `EBUSY` para negarse; `0` para aceptar.

**`MODULE_DEPEND`.** Macro que declara una dependencia de otro módulo del kernel. El kernel aplica el orden de carga y la compatibilidad de versiones.

**`MODULE_PNP_INFO`.** Macro que publica los identificadores de fabricante y producto que gestiona un driver. El kernel usa esos metadatos para cargar drivers automáticamente cuando aparece hardware compatible.

**`MODULE_VERSION`.** Macro que declara el entero de versión de un módulo. Lo utiliza `MODULE_DEPEND` para las comprobaciones de compatibilidad.

**`myfirst_ratelimit`.** La struct que contiene el estado de limitación de tasa por clase (`lasttime` y `curpps`). Debe vivir en el softc, no en la pila.

**`MYFIRST_VERSION`.** La cadena de versión legible por humanos del driver, p. ej. `"1.8-maintenance"`. Se expone a través de `dev.myfirst.<unit>.version`.

**`MYFIRST_IOCTL_VERSION`.** El entero de versión del formato de intercambio ioctl del driver. Lo devuelve `MYFIRSTIOC_GETVER`. Solo se incrementa ante cambios incompatibles.

**Pps.** Eventos por segundo. Un límite de tasa expresado en pps (p. ej., 10 pps = 10 mensajes por segundo).

**`ppsratecheck(9)`.** El primitivo de limitación de tasa de FreeBSD. Recibe un `struct timeval`, un `int *` y un límite en pps; devuelve un valor distinto de cero para permitir el evento.

**Listo para producción.** Un driver que satisface la lista de comprobación de la Sección 7: dependencias declaradas, superficie documentada, logging con límite de tasa, caminos de fallo ejercitados y test de 100 ciclos superado.

**`SHUTDOWN_PRI_*`.** Constantes de prioridad pasadas a `EVENTHANDLER_REGISTER` para eventos de apagado. `FIRST` se ejecuta al principio; `LAST` se ejecuta al final; `DEFAULT` se ejecuta en medio.

**`SI_SUB_*`.** Identificadores de subsistema para `SYSINIT`. Los valores numéricos se ordenan según la secuencia de arranque. Constantes habituales: `SI_SUB_TUNABLES`, `SI_SUB_DRIVERS`, `SI_SUB_CONFIGURE`.

**`SI_ORDER_*`.** Constantes de orden para `SYSINIT` dentro de un subsistema. `FIRST` se ejecuta primero; `MIDDLE` se ejecuta en el medio; `ANY` se ejecuta al final (sin orden garantizado entre entradas `ANY`).

**Regla de responsabilidad única.** Cada archivo fuente responde a una única pregunta sobre el driver. Las violaciones consisten en que nuevos ioctls se cuelen en `myfirst_sysctl.c` o nuevos sysctls en `myfirst_ioctl.c`.

**Softc.** La estructura de estado por dispositivo. Newbus la asigna, la pone a cero y la entrega a `device_attach` mediante `device_get_softc`.

**`sysctl(8)`.** El comando del espacio de usuario y la interfaz del kernel para parámetros en tiempo de ejecución. Los nombres de nodo viven bajo una jerarquía fija (`kern.*`, `hw.*`, `net.*`, `dev.*`, `vm.*`, `debug.*`, etc.).

**`SYSINIT(9)`.** La macro de inicialización en tiempo de arranque de FreeBSD. Registra una función para que se ejecute en un subsistema y orden determinados durante el arranque del kernel o la carga del módulo.

**`SYSUNINIT(9)`.** Complemento de `SYSINIT`. Registra una función de limpieza que se ejecuta en el momento de la descarga del módulo, en el orden inverso al de `SYSINIT`.

**Tag (event handler).** El valor opaco devuelto por `EVENTHANDLER_REGISTER` y consumido por `EVENTHANDLER_DEREGISTER`. Debe almacenarse (normalmente en el softc) para que la baja de registro pueda localizar el registro.

**Tunable.** Un valor extraído del entorno del kernel durante el arranque o la carga del módulo y consumido mediante `TUNABLE_*_FETCH`. Establece valores iniciales; vive en el nivel `hw.`, `kern.` o `debug.`.

**`TUNABLE_INT_FETCH`, `_LONG_FETCH`, `_BOOL_FETCH`, `_STR_FETCH`.** La familia de macros que leen un tunable del entorno del kernel y rellenan una variable. No producen ningún efecto si el tunable está ausente.

**División de versiones.** La práctica de usar tres identificadores de versión independientes (cadena de versión, entero de módulo, entero de ioctl) que cambian por razones distintas.

**Evento `vm_lowmem`.** Un evento de `EVENTHANDLER` que se dispara cuando el subsistema de memoria virtual está bajo presión. Los drivers con cachés pueden devolver parte de esa memoria.

**Formato de intercambio.** La disposición de los datos que cruzan la frontera entre el espacio de usuario y el kernel. El formato de intercambio de ioctl lo determinan las declaraciones `_IOR`, `_IOW`, `_IOWR` y la estructura de carga útil. Un cambio en el formato de intercambio es un cambio incompatible y requiere incrementar `MYFIRST_IOCTL_VERSION`.
