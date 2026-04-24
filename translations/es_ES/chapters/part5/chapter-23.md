---
title: "Depuración y trazado"
description: "El capítulo 23 abre la Parte 5 enseñando cómo depurar y trazar drivers de dispositivo de FreeBSD de manera disciplinada y reproducible. Explica por qué la depuración del kernel difiere de la depuración en userland y qué implica esa diferencia para el lector; cómo usar printf() y device_printf() de forma eficaz y adónde va realmente la salida; cómo dmesg, el buffer de mensajes del kernel, /var/log/messages y syslog se combinan para formar el pipeline de logging en el que se apoya el driver; cómo construir y ejecutar un kernel de depuración con DDB, KDB, INVARIANTS, WITNESS y opciones relacionadas, y qué comprueba realmente cada opción; cómo DTrace expone una vista en tiempo real de la actividad del kernel mediante providers, probes y scripts breves; cómo ktrace y kdump revelan la frontera entre usuario y kernel desde el punto de vista del proceso de usuario; cómo diagnosticar los errores que los drivers producen con frecuencia, incluidos los memory leaks, las condiciones de carrera, el uso incorrecto de bus_space y bus_dma, y los accesos tras el detach; y cómo refactorizar el logging y el trazado en un subsistema limpio, activable y mantenible. El driver myfirst pasa de la versión 1.5-power a la 1.6-debug, incorpora myfirst_debug.c y myfirst_debug.h, añade probes SDT que el lector puede inspeccionar con dtrace, añade un documento DEBUG.md y llega al final del capítulo 23 con un driver que te cuenta lo que está haciendo cada vez que se lo preguntas."
partNumber: 5
partName: "Debugging, Tools, and Real-World Practices"
chapter: 23
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 210
language: "es-ES"
---
# Depuración y trazado

## Orientación al lector y resultados

El capítulo 22 cerró la Parte 4 con un driver que sobrevive a las transiciones de suspend, resume y shutdown. El driver `myfirst` en la versión `1.5-power` se conecta a un dispositivo PCI, asigna vectores MSI-X, ejecuta un pipeline DMA, gestiona el contrato de gestión de energía de kobj, expone contadores a través de sysctls y supera un script de regresión que ejercita attach, detach, suspend, resume y PM en tiempo de ejecución. Para un driver que arranca, funciona, se suspende, se reanuda y finalmente se descarga, esas mecánicas son completas. Lo que el driver todavía no tiene es la capacidad de decirle a su desarrollador qué está ocurriendo en su interior cuando algo sale mal.

El capítulo 23 añade esa capacidad. La Parte 5 se titula *Depuración, herramientas y prácticas del mundo real*, y el capítulo 23 la abre enseñando la disciplina de observabilidad y diagnóstico que convierte un driver que acabas de escribir en un driver que puedes mantener durante años. El lector pasará este capítulo aprendiendo cómo difiere la depuración en espacio del kernel de la depuración en userland, cómo usar `printf()` y `device_printf()` con intención en lugar de por hábito, cómo seguir los mensajes de log desde el momento en que se escriben hasta el momento en que un usuario los lee con `dmesg`, cómo construir un kernel de depuración que detecte errores que el kernel normal pasa por alto, cómo pedirle a DTrace que muestre el comportamiento en vivo del driver sin recompilar nada, cómo usar `ktrace` y `kdump` para seguir a un programa de usuario mientras cruza al kernel a través de la interfaz del driver, cómo leer los patrones característicos de los bugs más comunes en drivers la primera vez que aparece un síntoma, y cómo refactorizar el soporte de depuración del driver para que siga siendo útil sin convertir el código fuente en un montón de llamadas a `printf` dispersas.

El alcance del capítulo 23 es precisamente este. Enseña el conjunto de herramientas de depuración del kernel que un autor de drivers para FreeBSD usa en el trabajo de laboratorio, en las pruebas de regresión y al responder a informes de bugs. Enseña suficiente DTrace para instrumentar un driver y medir su comportamiento. Enseña el modelo mental que hay detrás de las opciones de depuración del kernel para que el lector pueda elegir el conjunto adecuado para el problema que tiene delante. Muestra dónde viven estas herramientas en el árbol de código fuente y cómo leer su documentación directamente. No enseña la mecánica más profunda del análisis post-mortem de volcados de kernel, porque ese material corresponde más adelante en este libro como un tema especializado. No enseña arquitectura de drivers ni integración con subsistemas específicos, porque el capítulo 24 trata de eso. No enseña ajuste de rendimiento avanzado ni perfilado a gran escala, porque la Parte 6 cubre en profundidad el ajuste orientado al hardware. La disciplina que introduce el capítulo 23 es la base que asumen los capítulos posteriores: no puedes ajustar lo que no puedes observar, y no puedes arreglar lo que no puedes reproducir.

La Parte 5 es donde el driver adquiere las cualidades que separan un prototipo funcional del software de producción. El capítulo 22 hizo que el driver sobreviviera a un cambio de estado de energía. El capítulo 23 hace que el driver te diga qué está haciendo. El capítulo 24 hará que el driver se integre limpiamente con los subsistemas del kernel a los que los usuarios acceden desde sus programas. El capítulo 25 hará que el driver sea resistente bajo estrés. Cada capítulo añade una cualidad más. El capítulo 23 añade observabilidad, que resulta ser la cualidad de la que dependen todos los temas posteriores.

### Por qué la depuración del kernel merece un capítulo propio

Antes de la primera línea de código, es útil entender por qué DTrace, `dmesg` y las opciones del kernel de depuración reciben un capítulo entero. Puede que ya te estés preguntando por qué esto es más difícil que depurar un programa de usuario: añades algunas sentencias `printf`, ejecutas bajo `gdb`, recorres el código paso a paso y listo. Ese modelo mental es razonable en espacio de usuario y catastrófico en espacio del kernel, por tres razones interrelacionadas.

La primera es que **un bug en un driver es un bug del kernel**. Cuando un programa en userland tiene un bug, el sistema operativo protege al resto de la máquina: el kernel mata el proceso, la shell imprime un mensaje, el usuario lo intenta de nuevo. Nada de esa protección se aplica dentro del kernel. Un único puntero incorrecto, un único lock no adquirido, una única asignación sin liberar, una única condición de carrera entre un gestor de interrupciones y una tarea, pueden provocar el pánico del sistema completo. Un driver que falla durante las pruebas tumba la VM en ejecución. Un driver que pierde memoria acaba por agotar la máquina. Un driver que corrompe la memoria destruye cualquier dato que tuviera la mala suerte de estar junto al bug. El capítulo 23 existe porque estos fallos no se parecen ni se comportan como los fallos en userland, y las herramientas para encontrarlos tampoco se parecen ni se comportan como las herramientas en userland.

La segunda es que **la visibilidad es limitada y costosa**. En un depurador de userland, el programa se detiene por ti. Puedes avanzar paso a paso, imprimir variables, establecer puntos de ruptura condicionales, retroceder el reloj con `rr` o `gdb --record`. En el kernel, nada de eso está disponible en tiempo de ejecución sin una preparación especial. No puedes detener un kernel en ejecución de la misma manera que detienes un proceso, porque el kernel es también lo que ejecuta todo lo demás, incluyendo el teclado, el terminal, la red y el disco. Cada pieza de visibilidad que añades tiene un coste: un `printf` es barato pero lento; una sonda SDT es rápida pero tiene que estar compilada y activada; un script de DTrace es flexible pero se ejecuta dentro del propio kernel y toca cada sonda que dispara. El capítulo 23 existe porque tienes que gastar el presupuesto de visibilidad deliberadamente, y saber lo que cuesta cada herramienta forma parte de saber cómo usarla.

La tercera es que **los ciclos de retroalimentación son más largos**. En userland, el ciclo de escribir-compilar-ejecutar dura segundos. En trabajo con el kernel, el ciclo es el mismo en las mejores condiciones, pero la consecuencia de un error es un reinicio o la vuelta atrás de la instantánea de la VM. Un bug que tarda un intento en reproducirse en userland puede necesitar diez suspensiones y reanudaciones en el kernel antes de que el contador interno del driver desborde y aparezca el síntoma. Un bug que se reproduce en cada ejecución bajo `gdb` puede reproducirse solo en un kernel sin carga, a una carga específica o en una CPU específica. El capítulo 23 existe porque la depuración del kernel solo es tolerable si el ciclo de retroalimentación es tan corto como puedas hacerlo, y los ciclos cortos requieren la combinación adecuada de herramientas para cada clase de bug.

El capítulo 23 se gana su lugar enseñando esas tres realidades juntas, de forma concreta, con el driver `myfirst` como ejemplo continuo. Un lector que termina el capítulo 23 puede añadir salida de depuración disciplinada y activable a cualquier driver de FreeBSD; leer e interpretar la salida de `dmesg`, `/var/log/messages` y DTrace sin ayuda; construir un kernel de depuración y saber qué aporta cada opción; encontrar y corregir las clases de bugs más comunes en drivers la primera vez que aparece un síntoma; y convertir un informe vago ("mi máquina a veces se cuelga cuando se carga el driver") en un caso de prueba reproducible, una hipótesis, una corrección y una prueba de regresión.

### Dónde dejó el driver el capítulo 22

Algunos requisitos previos para verificar antes de empezar. El capítulo 23 extiende el driver producido al final de la Etapa 4 del capítulo 22, etiquetado como versión `1.5-power`. Si alguno de los puntos siguientes es incierto, regresa al capítulo 22 y corrígelo antes de comenzar este, porque los temas de depuración asumen que el driver tiene una línea base estable sobre la que depurar.

- Tu driver compila sin errores y se identifica como `1.5-power` en `kldstat -v`.
- El driver asigna uno o tres vectores MSI-X, registra un pipeline de interrupciones de filtro más tarea, vincula cada vector a una CPU e imprime un banner de interrupción durante el attach.
- El driver asigna una etiqueta `bus_dma` y un buffer DMA de 4 KB, expone la dirección de bus a través de `dev.myfirst.N.dma_bus_addr` y limpia los recursos DMA en el detach.
- El driver implementa `DEVICE_SUSPEND`, `DEVICE_RESUME` y `DEVICE_SHUTDOWN`. Un `devctl suspend myfirst0` seguido de `devctl resume myfirst0` tiene éxito sin errores, y una transferencia DMA posterior funciona sin fallos.
- Los contadores de transiciones de suspend, resume, shutdown y PM en tiempo de ejecución aparecen como sysctls bajo `dev.myfirst.N.`.
- `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`, `DMA.md` y `POWER.md` están actualizados en tu árbol de trabajo.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` están habilitados en tu kernel de pruebas.

Ese driver es lo que el capítulo 23 extiende. Las adiciones son modestas en líneas de código fuente pero significativas en mantenibilidad: un nuevo archivo `myfirst_debug.c`, una cabecera `myfirst_debug.h` correspondiente, un banco de macros de logging con controles en tiempo de compilación y en tiempo de ejecución, un conjunto de puntos de sonda DTrace definidos estáticamente, un control sysctl de modo verbose, un conjunto de contadores de depuración que distinguen la operación normal de los eventos anómalos, una actualización de versión a `1.6-debug`, un documento `DEBUG.md` que explica el subsistema, y una pequeña colección de scripts auxiliares que convierten la salida cruda de logs y trazas en algo que un humano puede leer.

### Lo que aprenderás

Al final de este capítulo serás capaz de:

- Describir qué hace diferente la depuración del kernel de la depuración en userland, en términos concretos, y explicar cómo esa diferencia da forma al resto del conjunto de herramientas del capítulo.
- Usar `printf()`, `device_printf()`, `device_log()` y `log()` correctamente en código de driver, comprendiendo cuándo es apropiado cada uno y qué significan los niveles de prioridad de log en FreeBSD.
- Leer y filtrar `dmesg`, seguir el buffer de mensajes del kernel a lo largo de su ciclo de vida, correlacionar mensajes en tiempo de boot y en tiempo de ejecución, y saber cuándo los mensajes que buscas están en `dmesg`, cuándo están en `/var/log/messages` y cuándo están en ambos.
- Construir un kernel de depuración personalizado con `DDB`, `KDB`, `KDB_UNATTENDED`, `INVARIANTS`, `WITNESS` y opciones relacionadas, y saber qué hace realmente cada opción en tiempo de ejecución, cuánto cuesta y cuándo la necesitas.
- Entrar en `ddb` deliberadamente durante una prueba, ejecutar los comandos básicos de inspección (`show`, `ps`, `bt`, `trace`, `show alllocks`, `show pcpu`, `show mbufs`), salir de `ddb` limpiamente y comprender qué estás viendo en cada caso.
- Entender qué es un volcado de kernel, cómo `dumpdev` y `savecore(8)` producen uno, y cómo `kgdb` abre un volcado para análisis post-mortem a un nivel suficiente para abrirlo e inspeccionar algunos marcos.
- Explicar qué es DTrace, cargar el módulo `dtraceall`, listar los proveedores que expone el kernel, escribir un script corto que trace una función del kernel, y usar los proveedores `fbt`, `syscall`, `sched`, `io`, `vfs` y `sdt` para preguntas habituales sobre drivers.
- Añadir sondas SDT (trazado definido estáticamente) a un driver usando `SDT_PROVIDER_DEFINE`, `SDT_PROBE_DEFINE` y las macros `SDT_PROBE`, y usar `dtrace -n 'myfirst:::entry'` para observarlas.
- Ejecutar `ktrace` contra un programa de usuario que se comunica con el driver, leer la salida con `kdump`, y mapear syscalls como `open`, `ioctl`, `read` y `write` de vuelta a los puntos de entrada del driver.
- Reconocer los patrones característicos de los bugs en drivers: asignaciones sin liberar visibles en `vmstat -m`, violaciones de orden de lock reportadas por `WITNESS`, accesos `bus_space` más allá del final de un BAR, llamadas a `bus_dmamap_sync` omitidas antes o después de una transferencia, acceso a un dispositivo después de que se haya ejecutado su `DEVICE_DETACH`, y los síntomas que produce cada uno.
- Refactorizar el soporte de depuración del driver en un par dedicado `myfirst_debug.c` y `myfirst_debug.h` con salida verbose activable en tiempo de ejecución, macros ENTRY y EXIT para trazado a nivel de función, macros ERROR y WARN para la notificación estructurada de problemas, sondas SDT para trazado externo, y un subárbol sysctl para los controles de depuración.
- Leer código de depuración real de drivers en el árbol de código fuente de FreeBSD, como la familia `DPRINTF` en `/usr/src/sys/dev/ath/if_ath_debug.h`, el uso de `bootverbose` en `/usr/src/sys/dev/virtio/block/virtio_blk.c`, las sondas SDT en `/usr/src/sys/dev/virtio/virtqueue.c`, y los sysctls de depuración en drivers como `/usr/src/sys/dev/iwm/if_iwm.c`.

La lista es larga porque la depuración es en sí misma una familia de habilidades. Cada elemento es concreto y enseñable. El trabajo del capítulo es la composición.

### Lo que este capítulo no cubre

Varios temas adyacentes se difieren explícitamente para que el capítulo 23 se mantenga centrado en la disciplina de depuración cotidiana.

- **Scripts avanzados de `kgdb` y extensiones en Python**. El capítulo muestra cómo abrir un crash dump e inspeccionar algunos marcos de pila. La construcción de helpers en Python sobre `kgdb`, el recorrido de estructuras de datos del kernel con comandos definidos por el usuario y el análisis automatizado de volcados mediante scripts pertenecen a un capítulo avanzado posterior.
- **`pmcstat` y contadores de rendimiento de hardware**. Son herramientas de ajuste del rendimiento, no herramientas de depuración en sentido estricto. La Parte 6 vuelve a ellas.
- **Perfilado de locks más allá de `WITNESS`**. La vía de `options LOCK_PROFILING` y el proveedor DTrace `lockstat` se mencionan en contexto, pero el flujo de trabajo completo de ajuste para el análisis de contención pertenece a los capítulos de rendimiento de la Parte 6.
- **Trazado de eventos `KTR` en detalle**. El capítulo menciona `KTR` en contraste con `ktrace` para que el lector no los confunda, pero el pesado buffer de eventos en el kernel y su flujo de trabajo de registro circular son material avanzado que raramente requiere la atención de un driver.
- **Marcos de registro con limitación de tasa**. El Capítulo 25 cubre en profundidad la etiqueta de registro, los contadores estáticos y la limitación de tasa basada en tiempo. El Capítulo 23 introduce la necesidad de limitar la tasa de forma informal y lo deja ahí.
- **El backend `bhyve debug`, el modo servidor de GDB y los breakpoints en vivo a nivel del kernel**. Son posibles y en ocasiones útiles, pero constituyen un nicho avanzado que no encaja en el primer capítulo de depuración.
- **Marcos de inyección de errores y pruebas de fallos**. `fail(9)`, `fail_point(9)` y los mecanismos relacionados se introducen brevemente en contexto, pero se dejan para el Capítulo 25 y el material de pruebas que sigue.

Mantenerse dentro de esos límites hace que el Capítulo 23 sea un capítulo sobre cómo encontrar bugs, no un capítulo sobre todos los lugares en los que el kernel ha intentado alguna vez ayudarte a encontrarlos.

### Tiempo estimado de dedicación

- **Solo lectura**: cuatro o cinco horas. Las ideas del Capítulo 23 son conceptualmente más ligeras que las del Capítulo 21 o el Capítulo 22, pero se conectan con muchas herramientas, y una primera lectura del panorama de herramientas lleva tiempo.
- **Lectura más escritura de los ejemplos**: diez a doce horas repartidas en dos o tres sesiones. El driver evoluciona en tres etapas: primero el registro basado en macros y el modo detallado, luego las sondas SDT y, por último, la refactorización en su propio par de archivos. Cada etapa es breve, y cada una incluye un laboratorio corto que confirma su correcto funcionamiento.
- **Lectura más todos los laboratorios y desafíos**: quince a dieciocho horas repartidas en tres o cuatro sesiones. Los laboratorios incluyen una sesión deliberada de `ddb`, un ejercicio de DTrace que mide la latencia del driver, un recorrido por `ktrace`/`kdump`, una fuga de memoria intencionada que el lector debe localizar con `vmstat -m`, y una violación de orden de lock deliberada que el lector debe reproducir bajo `WITNESS`.

Las secciones 4 y 5 son las más densas en cuanto a vocabulario nuevo. Si los comandos de `ddb` o la sintaxis de DTrace resultan opacos en la primera lectura, es normal. Detente, ejecuta el ejercicio correspondiente sobre el driver simulado y vuelve cuando las ideas hayan asentado. La depuración es una de esas habilidades en las que la práctica concreta asienta las ideas más rápido que seguir leyendo.

### Requisitos previos

Antes de comenzar este capítulo, verifica:

- El código fuente de tu driver coincide con la Etapa 4 del Capítulo 22 (`1.5-power`). El punto de partida asume todos los elementos del Capítulo 22: los métodos `device_suspend`, `device_resume` y `device_shutdown`; el indicador `suspended` del softc y los contadores de energía; el documento POWER.md; y el script de pruebas de regresión. El Capítulo 23 se construye sobre todo eso.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con `/usr/src` en disco y coherente con el kernel en ejecución.
- Un kernel de depuración con `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS` y `DDB_CTF` está compilado, instalado y arrancando sin errores. Estas son exactamente las opciones que el Capítulo 22 ya solicitó; la Sección 4 de este capítulo explica cada una en detalle para que sepas qué hacen.
- `bhyve(8)` o `qemu-system-x86_64` está disponible. Los laboratorios del Capítulo 23 se ejecutan deliberadamente en una VM. Algunos pueden provocar un pánico del kernel (esa es la lección), y un snapshot de la VM es la herramienta adecuada para mantener el ciclo de retroalimentación corto.
- Los siguientes comandos del espacio de usuario están disponibles en tu PATH: `dmesg`, `sysctl`, `kldstat`, `kldload`, `kldunload`, `devctl`, `vmstat`, `ktrace`, `kdump`, `dtrace`, `procstat`, `savecore`, `kgdb`.
- Has creado un snapshot de tu VM en el estado `1.5-power` y le has asignado un nombre claro. Varios laboratorios de este capítulo provocan un pánico del kernel de forma intencionada; conviene poder revertir a un estado conocido y funcional en menos de un minuto.

Si alguno de los puntos anteriores no está resuelto, corrígelo ahora. La depuración, a diferencia de muchos otros temas de este libro, es una habilidad que recompensa la preparación y castiga los atajos. Una sesión de laboratorio que comienza sin snapshot es una sesión que termina con una hora de recuperación; un ejercicio de DTrace que comienza sin `KDTRACE_HOOKS` es un ejercicio que falla en silencio.

### Cómo sacar el máximo partido a este capítulo

Cinco hábitos dan mejores resultados en este capítulo que en cualquiera de los anteriores.

Primero, ten marcados como favoritos `/usr/src/sys/kern/subr_prf.c`, `/usr/src/sys/kern/kern_ktrace.c`, `/usr/src/sys/kern/subr_witness.c` y `/usr/src/sys/sys/sdt.h`. El primero define las implementaciones de `printf`, `log`, `vprintf` y `tprintf` del kernel, y es la respuesta canónica a cualquier pregunta sobre qué ocurre al invocarlos. El segundo contiene la implementación de `ktrace`. El tercero es `WITNESS`, el verificador de orden de locks en el que el Capítulo 23 se apoya intensamente. El cuarto contiene las macros de sondas SDT. Leer cada uno una vez al inicio de la sección correspondiente es lo más eficaz que puedes hacer para ganar fluidez. Ninguno de estos archivos es largo; el más extenso tiene unos pocos miles de líneas y la mayor parte son comentarios.

Segundo, ten a mano tres ejemplos de drivers reales: `/usr/src/sys/dev/ath/if_ath_debug.h`, `/usr/src/sys/dev/virtio/virtqueue.c` e `/usr/src/sys/dev/iwm/if_iwm.c`. El primero ilustra el patrón clásico de la macro `DPRINTF` del kernel con interruptores en tiempo de compilación y en tiempo de ejecución. El segundo contiene sondas SDT reales. El tercero muestra cómo un driver conecta un sysctl de depuración a través de `device_get_sysctl_tree()`. El Capítulo 23 hace referencia a cada uno en el momento oportuno. Leerlos una vez ahora, sin intentar memorizarlos, le da al resto del capítulo puntos de anclaje concretos en los que apoyar sus ideas.

> **Una nota sobre los números de línea.** Cuando se haga referencia a estos drivers reales más adelante, la referencia estará siempre anclada en un símbolo con nombre: una definición de `DPRINTF`, una macro de sonda SDT, un callback concreto de sysctl. Esos nombres se mantendrán en futuras versiones de punto de FreeBSD 14.x; las líneas donde se encuentran, no. Ve al símbolo en tu editor y confía en lo que te indique; la salida de ejemplo de `WITNESS` que aparece más adelante en el capítulo cita ubicaciones del código fuente por la misma razón, como punteros y no como direcciones fijas.

Tercero, escribe a mano cada cambio de código en el driver `myfirst`. El código de soporte a la depuración es el tipo de código que es fácil de pegar y difícil de recordar después. Escribir las macros, las definiciones SDT, el sysctl del modo detallado y la refactorización en `myfirst_debug.c` crea una familiaridad que una sesión de copiar y pegar no proporciona. El objetivo no es tener el código; el objetivo es ser la persona que podría escribirlo de nuevo desde cero en veinte minutos.

Cuarto, trabaja dentro de una VM, mantén un snapshot y no temas provocar un pánico del kernel de forma deliberada. Algunos de los laboratorios del capítulo te piden entrar en `ddb` en un sistema en ejecución. Otros te piden construir un driver que hace un uso incorrecto de un lock de forma intencionada y, a continuación, observar cómo `WITNESS` lo detecta. Un pánico real en una VM real es, para practicar la depuración, barato e inofensivo. Un pánico real en el portátil de desarrollo del lector es caro y molesto. El snapshot es la vía económica.

Quinto, tras terminar la Sección 4, vuelve a leer las notas de depuración del Capítulo 22 sobre suspend y resume. Las rutas de suspend y resume son objetivos clásicos del conjunto de herramientas del Capítulo 23: los problemas de lock aparecen en `WITNESS`, los errores de estado DMA se manifiestan como fallos de `KASSERT` bajo `INVARIANTS`, y las condiciones de carrera se presentan como fallos intermitentes de `devctl suspend`. Ver el material del Capítulo 22 a la luz de las herramientas del Capítulo 23 refuerza ambos capítulos.

### Hoja de ruta del capítulo

Las secciones, en orden, son:

1. **Por qué la depuración del kernel es un desafío.** El modelo mental: qué hace que los errores en espacio del kernel sean distintos; por qué las herramientas del userland no se trasladan directamente; por qué los cambios controlados e incrementales son la respuesta adecuada; por qué siempre se depura desde una posición de reproducibilidad.
2. **Registro con `printf()` y `device_printf()`.** La herramienta cotidiana. El printf del kernel; `device_printf` como la forma preferida para los drivers de dispositivo; la familia `log(9)` con niveles de prioridad; convenciones de formato; adónde va realmente la salida; qué registrar y qué omitir. La Etapa 1 del driver del Capítulo 23 extiende las líneas de registro del `myfirst` para usar `device_printf` de forma consistente y prepara el terreno para las macros de depuración de la Sección 8.
3. **Uso de `dmesg` y Syslog para el diagnóstico.** Seguir la salida. El buffer de mensajes del kernel; `dmesg` y sus filtros; `/var/log/messages` y `newsyslog`; la relación entre la salida de la consola, `dmesg` y syslog; qué significa que un mensaje se «pierda»; cómo correlacionar mensajes a lo largo de un ciclo de boot y de una recarga de módulo.
4. **Builds de depuración del kernel y opciones.** Los invariantes que tu kernel hace cumplir. `options KDB`, `DDB`, `KDB_UNATTENDED`, `KDB_TRACE`; `INVARIANTS` e `INVARIANT_SUPPORT`; `WITNESS` y sus variantes; `KDTRACE_HOOKS` y `DDB_CTF`; un recorrido por los comandos de DDB que más usa el autor de un driver; una breve introducción a `kgdb` sobre un volcado de memoria. Esta es la sección más larga del capítulo porque el kernel de depuración es el suelo sobre el que se asienta todo lo demás.
5. **Uso de DTrace para la inspección del kernel en vivo.** El bisturí. Qué es DTrace; proveedores y sondas; cómo listarlas con `dtrace -l`; scripts breves que usan `fbt`, `syscall`, `sched`, `io` y `sdt`; cómo añadir sondas SDT al driver `myfirst`. La Etapa 2 del driver del capítulo añade sondas SDT.
6. **Trazado de la actividad del kernel con `ktrace` y `kdump`.** La ventana del lado del usuario. Qué registra `ktrace`, qué no registra, cómo invocarlo, cómo interpretar la salida de `kdump`, cómo seguir un programa de usuario mientras cruza al driver a través de `open`, `ioctl`, `read` y `write`.
7. **Diagnóstico de errores comunes en drivers.** La guía de campo. Fugas de memoria y `vmstat -m`; violaciones del orden de lock y `WITNESS`; patrones de mal uso de `bus_space` y `bus_dma`; acceso tras el detach; errores en el manejador de interrupciones; errores de ciclo de vida; la lista de comprobación de depuración que ejecutar antes de reportar un error.
8. **Refactorización y versionado con puntos de traza.** La puesta en orden. La división final en `myfirst_debug.c` y `myfirst_debug.h`; el modo detallado activable en tiempo de ejecución mediante sysctl; las macros ENTRY/EXIT/ERROR; un pequeño banco de sondas SDT; el documento `DEBUG.md`; el salto de versión a `1.6-debug`.

Tras las ocho secciones llega un análisis detallado de patrones de depuración de drivers reales en `if_ath_debug.h`, `virtqueue.c` e `if_re.c`, un conjunto de laboratorios prácticos, un conjunto de ejercicios de desafío, una referencia de resolución de problemas, una sección de cierre que termina la historia del Capítulo 23 y abre la del Capítulo 24, un puente, una tarjeta de referencia rápida y el glosario habitual de términos nuevos.

Si es tu primera lectura, hazla de forma lineal y realiza los laboratorios en orden. Si estás revisando el material, las Secciones 4 y 7 son independientes y funcionan bien como lecturas de una sola sesión. La Sección 5 asume las Secciones 1 a 4; no leas DTrace de forma aislada en la primera lectura.



## Sección 1: Por qué la depuración del kernel es un desafío

Antes que las herramientas, la mentalidad. La Sección 1 explica qué hace que la depuración del kernel sea diferente de lo que el lector puede ya conocer. Un lector que haya escrito scripts de shell, pequeños programas en C, o incluso un servidor de aplicaciones con múltiples threads, probablemente haya usado `printf`, `gdb`, `strace`, `valgrind`, `lldb` o las herramientas de desarrollo del navegador. Esas herramientas son excelentes en espacio de usuario, y el modelo mental que fomentan es razonable en ese contexto. En espacio del kernel, el modelo mental necesita cambiar. La Sección 1 describe ese cambio, nombra las dificultades específicas con las que los autores de drivers se encuentran repetidamente, y prepara el escenario para el trabajo herramienta por herramienta que sigue.

### La naturaleza del código del kernel

Un driver es, en el enunciado más simple posible, código C que se ejecuta en el espacio de direcciones del kernel. Ese único hecho explica la mayor parte de las diferencias entre la depuración del kernel y la depuración en el userland.

El espacio de direcciones del kernel lo contiene todo: el código que el propio kernel ejecuta, las estructuras de datos que el kernel usa para gestionar procesos, archivos, memoria y dispositivos, las tablas de páginas de cada proceso del sistema, los buffers de cada paquete de red pendiente, las colas de cada solicitud de disco y la memoria que hay tras los registros mapeados de cada dispositivo. Cada puntero que el driver mantiene puede alcanzar potencialmente cualquier otra parte del kernel. No existe la separación de la que disfrutan los programas en espacio de usuario, donde un proceso solo puede acceder a su propio espacio de direcciones y el sistema operativo impone ese límite.

La consecuencia es contundente: un único puntero incorrecto en un driver puede corromper cualquier estructura de datos del kernel, no solo las propias del driver. Un driver que escribe un byte más allá del final de su softc puede acabar escribiendo en la tabla de procesos del kernel, en el mapa de bits de bloques libres del sistema de archivos, o en la tabla de vectores de interrupción de otro driver. La corrupción a menudo no produce un pánico inmediato; produce un pánico cinco minutos después, cuando otro código lee la estructura que el driver dañó en silencio. Ese retraso entre la causa y el síntoma es la razón más habitual por la que los errores del kernel parecen más difíciles que los del userland. No detectas el puntero incorrecto en el momento en que ocurre; detectas un código diferente tropezando con el desorden.

El código del kernel también se ejecuta sin las redes de seguridad habituales. En el espacio de usuario, desreferenciar un puntero nulo produce un fallo de segmentación, el kernel mata el proceso y el usuario ve un mensaje. En el espacio del kernel, desreferenciar un puntero nulo produce un fallo de página, el propio kernel falla y el manejador de fallos decide si el sistema puede continuar. Si el fallo de página ocurre en un lugar en el que dejaría el kernel en un estado inconsistente (dentro de un manejador de interrupciones, dentro de una sección crítica, dentro de `malloc`), el kernel entra en pánico para evitar daños mayores. En un kernel de desarrollo con `INVARIANTS` activado, el kernel entra en pánico de forma más agresiva que en un kernel de producción, porque la contrapartida es exactamente esa: detectar el error de forma ruidosa durante el desarrollo, en lugar de dejar que se propague silenciosamente.

Tampoco existe un flujo de salida estándar como el que tienen los programas de usuario. Un programa C en espacio de usuario llama a `printf` y la salida aparece en el terminal porque el C runtime enlaza la llamada a una cadena que acaba escribiendo en un descriptor de archivo que la shell conectó al terminal. En el kernel no hay shell, ni terminal en el sentido del usuario, ni C runtime. Cuando un driver llama a `printf`, llega a la propia implementación de `printf` del kernel, que escribe en el buffer de mensajes del kernel y, según la configuración, en la consola del sistema. La sección 2 cubre esto en detalle; lo importante por ahora es que el pipeline de registro del kernel no es el mismo que utilizan los programas de usuario, y asumir familiaridad con él lleva a confusión más adelante. Un driver que "simplemente llama a `printf`" está llamando a una función diferente de la que llama un programa de userland, con una semántica diferente y una ruta de salida distinta.

### Riesgos de la depuración en el espacio del kernel

Las consecuencias de un cambio de debug incorrecto en el espacio de usuario suelen ser pequeñas. Un `printf` que imprime la variable equivocada es una molestia. Un `printf` dentro de un bucle que se ejecuta mil millones de veces ralentiza el programa, pero no causa ningún daño. Un uso incorrecto de `malloc` en una instrucción de debug filtra unos pocos bytes, y el proceso eventualmente termina, y la fuga desaparece con él.

Las consecuencias de un cambio de debug incorrecto en el espacio del kernel pueden ser mucho mayores. Un `printf` en la ruta equivocada puede producir un deadlock en el kernel, si la ruta del printf necesita un lock que el contexto de llamada ya ha tomado. Un `printf` en un manejador de interrupciones que llama a código que puede dormir puede provocar un panic bajo `INVARIANTS`. Un mensaje de debug que llama a un asignador durmiente (`malloc(..., M_WAITOK)` en lugar de `M_NOWAIT`) mientras se mantiene un spinlock hará que `WITNESS` se queje mucho, y si `WITNESS_KDB` está habilitado, llevará la máquina al depurador del kernel. Un contador global usado con fines de debug que se incrementa sin una sincronización adecuada es en sí mismo una fuente de errores. Cada uno de estos es un error real que cometen los autores de drivers; cada uno es una lección que este capítulo enseñará.

El matiz es que el código de debug es, estrictamente, código, y tiene que ser tan correcto como cualquier otro código del driver. "Ya lo limpiaré después" no es seguro en el contexto del kernel. Un cambio de debug apresurado puede introducir un nuevo error que sea más difícil de encontrar que el error original. El capítulo 23 enseña hábitos que evitan esa trampa: usar macros que se eliminan al compilar cuando están deshabilitadas; usar el propio sistema de log del kernel en lugar de inventar uno; envolver bloques condicionales de modo que el código en modo verbose solo tenga coste cuando el modo verbose está activado; y probar el código de debug con el mismo nivel de cuidado que el código de producción.

Otro riesgo es de tipo observacional: añadir salida de debug puede cambiar el comportamiento que estás intentando observar. Una condición de carrera que se reproduce de forma fiable sin salida de debug puede desaparecer por completo cuando añades llamadas a `printf`, porque los printfs se serializan a través de un lock y resulta que ocultan la ventana de carrera. Un error de rendimiento puede verse diferente tras la instrumentación, porque la propia instrumentación añade coste. Los desarrolladores del kernel llaman a esto el problema de Heisenberg de la depuración del kernel. Las herramientas del capítulo se eligen en parte porque minimizan el problema: DTrace, las sondas SDT y KTR añaden muy poco overhead cuando están inactivos; `device_printf` es prolijo pero consistente. Esparcir `printf("HERE\n")` por el código es siempre una opción, pero como hábito cuesta más de lo que ayuda.

### Diferencias respecto a la depuración en userland

La diferencia entre la depuración en espacio del kernel y la depuración en espacio de usuario merece ser explicada herramienta por herramienta.

Un usuario puede ejecutar un programa bajo `gdb`, establecer un breakpoint y ver cómo el programa se detiene. No puedes hacer esto directamente con un kernel en ejecución, porque un kernel pausado significa un sistema pausado. El equivalente aproximado es `kgdb` sobre un volcado de memoria (crash dump), que te permite inspeccionar el estado del kernel en el momento del panic, pero que no puede avanzar paso a paso. El otro equivalente aproximado es `ddb`, el depurador integrado en el kernel, que te permite pausar un kernel en vivo pero se ejecuta como parte del propio kernel y tiene su propio conjunto de comandos limitado. Ambos son útiles; ninguno se comporta como `gdb` sobre un proceso en vivo.

Un usuario puede usar `strace` en Linux o `truss` en FreeBSD para observar las llamadas al sistema de un programa. El `ktrace` de FreeBSD es el equivalente más cercano; registra una traza de los syscalls, señales, eventos namei, I/O y cambios de contexto de un proceso en un archivo que `kdump` puede leer. Pero `ktrace` ve el lado del usuario de la interfaz del driver, no los internos del driver. Puede mostrarte que el proceso de usuario llamó a `ioctl(fd, CMD_FOO, ...)`, pero no lo que ocurrió dentro de la implementación del `ioctl` en el driver. La sección 6 dedica tiempo a `ktrace` precisamente porque es la herramienta que muestra ese límite con mayor claridad.

Un usuario puede usar `valgrind` para detectar errores de memoria y fugas en su programa. No existe un equivalente directo para el kernel. Los análogos más cercanos son `vmstat -m` para resumir las asignaciones del kernel por tipo, la opción `WITNESS` para los errores de orden de lock, `INVARIANTS` para las comprobaciones de consistencia que el desarrollador ha escrito en el código, y `MEMGUARD` para un asignador de memoria de debug más pesado. La sección 7 recorre los patrones prácticos.

Un usuario puede perfilar un programa con `perf` en Linux o `gprof` en FreeBSD. FreeBSD proporciona `pmcstat` y DTrace para el perfilado. DTrace es la herramienta más flexible para el tipo de preguntas que hace un desarrollador de drivers, y el capítulo 23 se centra en DTrace en lugar de `pmcstat`.

Un usuario puede a menudo simplemente volver a ejecutar un programa para ver si el error se reproduce. Los desarrolladores del kernel a menudo no pueden hacerlo; un kernel que ha entrado en panic tiene que reiniciarse, y los errores que solo aparecen bajo carga, o que dependen del tiempo, pueden tardar horas en reproducirse. La respuesta a esta dificultad es incorporar la reproducibilidad al proceso: hacer un snapshot de la VM, crear un script del caso de prueba y registrar todo. Cada herramienta de este capítulo existe para apoyar esa respuesta.

Una última diferencia es cultural. La comunidad de desarrollo del kernel ha construido un vocabulario específico en torno a la depuración que vale la pena aprender por sí solo. Términos como "lock order", "critical section", "preemption disabled", "epoch", "giant lock", "sleepable context", "non-sleepable context" e "interrupt context" tienen significados precisos en FreeBSD, y usarlos correctamente forma parte de ser fluido en la depuración del kernel. El capítulo 23 los usa a medida que avanza; el glosario al final recopila los que se repiten.

### La importancia de los cambios controlados e incrementales

En el espacio de usuario, cuando un programa se comporta mal, el primer instinto suele ser cambiar varias cosas a la vez y ver si el comportamiento cambia. En el espacio del kernel esta es una estrategia perdedora. Cada cambio es potencialmente un nuevo error. Cada reconstrucción cuesta un reinicio de la VM. Cada ciclo de prueba consume tiempo. Un desarrollador de drivers que cambia cinco cosas entre compilaciones pasa horas tratando de averiguar qué cambio causó el nuevo síntoma, porque ningún cambio está aislado.

El patrón correcto es el **cambio controlado e incremental**. Haz un cambio. Reconstruye. Prueba. Observa el efecto. Anota lo que ocurrió. Decide el siguiente cambio basándote en la evidencia. Continúa. Esto es más lento por cambio y más rápido por corrección funcional, porque nunca tienes que deshacer un cambio combinado cuando una de las cinco correcciones ha introducido realmente un nuevo error. La disciplina se siente artificial la primera vez. Después de una docena de sesiones, deja de sentirse artificial y empieza a sentirse como la única manera de avanzar.

Una disciplina relacionada es **empezar siempre desde un estado conocido como correcto**. No depures en un driver que ya se encuentra en un estado que no has verificado. No comiences una sesión aplicando seis cambios locales y luego ejecutando una prueba. Confirma tu línea base, verifica que funciona, y solo entonces comienza el nuevo trabajo. Si el nuevo trabajo sale mal, `git checkout .` te devuelve al estado conocido como correcto, y puedes empezar de nuevo. Un autor de drivers sin este hábito pasa la mitad de cada sesión de depuración preguntándose si la línea base o el nuevo cambio es el culpable.

Una tercera disciplina es **reproducir antes de corregir**. Un error que no se puede reproducir no puede demostrarse que está corregido. El primer trabajo de una sesión de depuración no es corregir el error; es producir una forma breve y fiable de desencadenarlo, idealmente en un script. Una vez que el desencadenante es fiable, la corrección es sencilla: aplica el candidato, vuelve a ejecutar el desencadenante, observa si el error ha desaparecido. Sin un desencadenante fiable, estás adivinando.

Una cuarta disciplina son los **casos de reproducción pequeños y con scripts**. La diferencia entre un error que lleva una hora corregir y uno que lleva una semana es a menudo el tamaño del script de reproducción. Un error que puedes reproducir con cinco líneas de shell es un error que puedes bisecar con `git bisect`. Un error que solo puedes reproducir después de tres horas de uso complejo es un error que te dará miedo incluso empezar a depurar. El tiempo invertido en reducir una reproducción es tiempo bien empleado.

Los laboratorios del capítulo 23 son deliberadamente pequeños. Los panics se desencadenan con tres líneas. Los scripts de DTrace son de cinco líneas. Las demostraciones de `ktrace` usan un programa C de una línea. Eso no es accidental: es el mismo patrón que usa una buena depuración de drivers a cualquier escala. Las reproducciones mínimas y los cambios mínimos escalan a errores reales.

### El cambio de mentalidad

Lo que cambia cuando un desarrollador pasa del espacio de usuario al espacio del kernel no son solo las herramientas; son las suposiciones. En el espacio de usuario, es razonable asumir que el kernel captará tus errores, que el runtime limpiará tras de ti, y que el peor caso suele ser un programa que se ha caído y que puedes reiniciar. En el espacio del kernel, esas suposiciones son incorrectas. El kernel no captura nada; el runtime es lo que estás escribiendo; el peor caso es un panic que lo derrumba todo.

Esto no es una razón para tener miedo. Es una razón para ser cuidadoso. Cuidadoso, aquí, tiene un significado específico: un autor de drivers escribe código que podría ser depurado por otra persona, en seis meses, a partir de los logs y el código fuente únicamente. El código cuidadoso usa nombres de función claros. El código cuidadoso registra lo que hace en los niveles de detalle que son útiles. El código cuidadoso comprueba sus propios invariantes. El código cuidadoso está estructurado de modo que cuando falla, el fallo es evidente. El código cuidadoso no intenta ser inteligente; el código inteligente es código que no puedes depurar bajo presión.

El capítulo 23 enseña las herramientas de la depuración del kernel, pero el hábito que realmente enseña es el cuidado. Cada herramienta del capítulo es más útil cuando la aplica un desarrollador cuidadoso sobre código cuidadoso. La misma herramienta aplicada a código descuidado produce información que no puedes interpretar. El objetivo no es memorizar la sintaxis de DTrace o los comandos de `ddb`; el objetivo es desarrollar el hábito de depurar a partir de evidencias, en pequeños pasos, contra una línea base conocida, con buenos logs, de una manera que cualquier colaborador pueda seguir.

### Cerrando la sección 1

La sección 1 ha establecido la forma del problema. El código del kernel se ejecuta en un único espacio de direcciones compartido, sin las redes de seguridad de las que depende el userland, con modos de fallo que dañan todo el sistema. Las herramientas de depuración del kernel son diferentes, más costosas y menos interactivas que sus equivalentes en userland. La disciplina del cambio controlado, incremental y reproducible no es opcional; es la única estrategia que funciona. El cambio de mentalidad requerido es real, pero es aprendible, y es la base sobre la que se asientan el resto de las herramientas del capítulo.

La sección 2 comienza el recorrido por las herramientas con la más sencilla y universal que todo driver acaba usando: la familia `printf` del kernel.



## Sección 2: Logging con `printf()` y `device_printf()`

La familia `printf` del kernel es la herramienta de depuración más humilde del kit de herramientas. También es la más usada, con diferencia. Casi todos los drivers de FreeBSD en `/usr/src/sys/dev` llaman a `device_printf` en algún lugar. Casi todos los mensajes de arranque que un usuario ve en su vida han sido escritos por un `printf` del kernel. Antes de DTrace, antes de `ddb`, antes de las sondas SDT, antes de los kernels de depuración, un autor de drivers recurre a `device_printf` porque siempre está disponible, siempre es seguro (con las salvedades que esta sección explicará), y lo entiende cualquier otro autor de drivers en el planeta.

Esta sección enseña a usar esa familia de funciones de forma deliberada. El objetivo no es simplemente mostrar cómo llamar a `device_printf`, porque esa parte es trivial. El objetivo es construir el hábito de usar el logging como una característica diseñada del driver, no como un conjunto disperso de notas de debug que se acumulan con el tiempo y nunca se limpian.

### Conceptos básicos de `printf()` en el contexto del kernel

El `printf` del kernel de FreeBSD reside en `/usr/src/sys/kern/subr_prf.c`. Su firma es familiar:

```c
int printf(const char *fmt, ...);
```

Recibe una cadena de formato y una lista de argumentos variables, y escribe el texto formateado en el buffer de mensajes del kernel. Ese buffer es una zona circular de memoria de aproximadamente 192 KB en los sistemas FreeBSD modernos, en la que se escriben todos los mensajes del kernel. El kernel no mantiene un registro permanente por sí solo; es el demonio `syslogd(8)` en espacio de usuario el que lee el buffer y escribe las líneas seleccionadas en `/var/log/messages`. La sección 3 cubre el pipeline completo.

La cadena de formato entiende la mayoría de las conversiones que admite el `printf` de userland: `%d`, `%u`, `%x`, `%s`, `%c`, `%p`, y otras similares. También admite algunos extras específicos del kernel, como `%D` para imprimir un volcado hexadecimal de un buffer de bytes (con un argumento separador). No entiende las conversiones de punto flotante como `%f` y `%g`, porque el kernel no usa aritmética de punto flotante por defecto y el código de conversión está deliberadamente ausente. Intentar imprimir un `double` o un `float` dentro del kernel es casi siempre un error, y el compilador generalmente advertirá de ello.

El `printf` del kernel tiene propiedades de seguridad distintas a las de su equivalente en userland. El `printf` de userland puede asignar memoria, puede tomar locks dentro del runtime de C, y puede bloquearse en operaciones de I/O. El `printf` del kernel no debe asignar memoria; no debe bloquearse; debe ser seguro de invocar desde casi cualquier contexto. La implementación en `subr_prf.c` es deliberadamente sencilla: formatea el texto en un pequeño buffer en la pila, copia el resultado en el buffer de mensajes, despierta a `syslogd` si corresponde, y retorna. Esa sencillez es parte de la razón por la que `printf` es la herramienta de respaldo universal.

Hay algunos contextos en los que incluso el `printf` del kernel no es seguro. Los manejadores de interrupciones rápidas, por ejemplo, se ejecutan antes de que el kernel haya estabilizado el estado de la consola, y un `printf` desde dentro de una interrupción rápida puede provocar un cuelgue o un deadlock en algunas plataformas. Las rutas de código que mantienen un lock que el propio camino de `printf` también intenta tomar pueden producir un deadlock igualmente. En la práctica, los autores de drivers evitan el problema usando `printf` únicamente desde los puntos de entrada ordinarios del driver (attach, detach, ioctl, read, write, suspend, resume) y desde las tareas de interrupción (la mitad diferida de un pipeline de interrupción con filtro y tarea, tal como introdujo el Capítulo 19), no desde el propio filtro. El driver `myfirst` ya sigue este patrón; el Capítulo 23 lo preserva.

### Uso preferido: `device_printf()` para registro estructurado y consciente del dispositivo

`printf` a secas es genérico. Un autor de drivers puede hacerlo mejor. El kernel proporciona `device_printf`, declarado en `/usr/src/sys/sys/bus.h`:

```c
int device_printf(device_t dev, const char *fmt, ...);
```

La función antepone a cada mensaje el nombre del dispositivo, que normalmente es el nombre del driver concatenado con el número de unidad: `myfirst0: `, `em1: `, `virtio_blk0: `. Ese prefijo no es meramente estético. Es la pieza de información más importante del registro del kernel en un sistema con varios dispositivos. Sin él, un mensaje como "interrupt received" carece de utilidad en una máquina con ocho tarjetas de red y tres controladoras de almacenamiento. Con él, "myfirst0: interrupt received" es inmediatamente localizable con cualquier herramienta que lea logs.

La regla es sencilla: **en código de drivers, prefiere `device_printf` sobre `printf`**. Allí donde el driver tenga un `device_t` en ámbito, usa `device_printf`. Allí donde el driver no tenga un `device_t` (una función de inicialización global, por ejemplo), usa `printf` con un prefijo explícito:

```c
printf("myfirst: module loaded\n");
```

Este es el patrón que hace legible el registro del kernel en todo un sistema. Es también el patrón que sigue casi todo driver de FreeBSD. Abre cualquier archivo bajo `/usr/src/sys/dev/` y el patrón aparece dentro de la primera función attach.

El driver `myfirst` ya utiliza `device_printf` en la mayoría de los lugares, porque el Capítulo 18 introdujo la convención cuando el driver se convirtió por primera vez en un dispositivo PCI. La Sección 8 de este capítulo envuelve `device_printf` en macros que añaden severidad, categoría y un interruptor de modo detallado, pero la función subyacente sigue siendo la misma. Nada en el Capítulo 23 reemplaza `device_printf`; el capítulo lo elabora.

### `device_log()` y los niveles de prioridad de registro

`device_printf` es directo: escribe el mensaje con la prioridad de registro predeterminada del kernel (`LOG_INFO`, en términos de syslog). A veces un driver quiere escribir con una prioridad mayor, para señalar un problema grave, o con una prioridad menor, para etiquetar un mensaje informativo rutinario que normalmente debería filtrarse.

FreeBSD 14 expone `device_log` para ese propósito, también en `/usr/src/sys/sys/bus.h`:

```c
int device_log(device_t dev, int pri, const char *fmt, ...);
```

El argumento `pri` es una de las constantes de prioridad de syslog definidas en `/usr/src/sys/sys/syslog.h`:

- `LOG_EMERG` (0): el sistema es inutilizable
- `LOG_ALERT` (1): se debe actuar de inmediato
- `LOG_CRIT` (2): condiciones críticas
- `LOG_ERR` (3): condiciones de error
- `LOG_WARNING` (4): condiciones de advertencia
- `LOG_NOTICE` (5): condición normal pero significativa
- `LOG_INFO` (6): informativo
- `LOG_DEBUG` (7): mensajes de nivel de depuración

Para el desarrollo de drivers, las cuatro prioridades que importan en la práctica son `LOG_ERR`, `LOG_WARNING`, `LOG_NOTICE` y `LOG_INFO`. Un driver rara vez necesita `LOG_EMERG`, `LOG_ALERT` o `LOG_CRIT`, porque la mayoría de los fallos de un driver son locales a ese driver, no a todo el sistema. `LOG_DEBUG` es útil durante la depuración activa; el Capítulo 23 lo usa para la salida en modo detallado.

La función subyacente `log(9)`, también en `subr_prf.c`, es más sencilla: `void log(int pri, const char *fmt, ...)`. `device_log` envuelve `log` y añade el prefijo con el nombre del dispositivo.

La diferencia práctica entre `printf` y `log` radica en el destino de la salida cuando el kernel está configurado de forma conservadora. Por defecto, FreeBSD envía todo lo que esté en `LOG_NOTICE` o por encima a la consola; `LOG_INFO` e inferiores van al buffer de mensajes del kernel pero no a la consola. Esto importa en dos situaciones. Primera, durante el boot: un mensaje de alta prioridad es visible en pantalla durante el arranque; un mensaje `LOG_INFO` no lo es. Segunda, durante la operación: un error grave en un sistema en producción debería aparecer en la consola (para que un administrador de sistemas que esté ejecutando `tail` sobre `messages` lo vea de inmediato), mientras que un mensaje rutinario de probe o attach no debería saturar la pantalla.

El driver `myfirst` del Capítulo 23 sigue esta convención:

- Éxito en probe y attach: `device_printf` (que equivale a `LOG_INFO`).
- Transiciones de suspend, resume y shutdown: `device_printf` (rutinario).
- Errores de hardware, contadores de interrupciones inesperados, fallos de DMA y estados de dispositivo incorrectos: `device_log(dev, LOG_ERR, ...)` o `device_log(dev, LOG_WARNING, ...)`.
- Salida en modo detallado (habilitada mediante un sysctl): `device_log(dev, LOG_DEBUG, ...)` dentro de las macros de depuración.

### Qué registrar y qué omitir

Saber qué no registrar importa tanto como saber qué registrar. Un driver con demasiada salida satura `dmesg`, desperdicia espacio en el buffer de mensajes y desplaza fuera del buffer circular los mensajes informativos más antiguos antes de que nadie los lea. Un driver con demasiado poco output es silencioso cuando algo va mal y no le da al lector nada con lo que trabajar.

La regla práctica para el driver `myfirst` del Capítulo 23 es:

**Registrar siempre, con `device_printf` (LOG_INFO):**

- Un mensaje de presentación de una sola línea en el attach, que indique el nombre del driver, la versión y los recursos clave asignados (tipo de interrupción, número de vectores MSI-X, tamaño del buffer de DMA). El lector debería poder ver de un vistazo qué hizo el driver.
- Una nota de una línea en el detach. El detach suele ser silencioso, pero registrar su finalización es útil cuando un driver se bloquea al descargarse y el lector necesita saber que el detach al menos se inició.
- Una nota de una línea en cada suspend, resume y shutdown. Las transiciones de energía importan, y a menudo el lector querrá correlacionar un fallo con una transición que lo precedió.

**Registrar siempre, con `device_log` usando `LOG_ERR` o `LOG_WARNING`:**

- Cualquier fallo al asignar un recurso (interrupción, etiqueta de DMA, buffer de DMA, mutex). La ruta de error siempre debe indicar qué falló.
- Cualquier error de hardware que el driver detecte: una respuesta que el dispositivo no esperaba, un timeout, un valor de registro que indica que el hardware ha fallado.
- Cualquier inconsistencia de estado que el driver detecte: una finalización de DMA para una transferencia que no se había iniciado, un suspend que encuentra el dispositivo ya suspendido, un detach que encuentra el dispositivo todavía con una referencia de cliente.

**No registrar nunca en la ruta normal de funcionamiento estable:**

- Cada interrupción. Aunque el dispositivo esté inactivo, un driver que registra cada interrupción satura el log en cuestión de segundos bajo carga real. Usa DTrace o contadores para observar interrupciones individualmente.
- Cada transferencia de DMA. Por la misma razón.
- Cada llamada a `read` o `write`. Por la misma razón.
- La entrada y salida de funciones internas en producción. Usa las macros de modo detallado (Sección 8) para que el desarrollador pueda activar o desactivar el rastreo de entrada y salida.

**Registrar en modo detallado (habilitado por las macros de la Sección 8 y el sysctl de depuración):**

- Entrada y salida de las funciones clave, con un breve resumen de los argumentos.
- Eventos de interrupción individuales con el valor del registro de estado de interrupción.
- Envíos y finalizaciones de transferencias de DMA.
- Operaciones de sysctl e ioctl y los valores que devolvieron.

La línea entre "siempre" y "modo detallado" es una decisión de criterio, y diferentes drivers la trazan de forma distinta. `/usr/src/sys/dev/re/if_re.c` registra muy poco en producción y usa un sysctl `re_debug` para habilitar una salida detallada. `/usr/src/sys/dev/virtio/block/virtio_blk.c` usa `bootverbose` para controlar el detalle adicional en el attach. `/usr/src/sys/dev/ath/if_ath_debug.h` define una sofisticada máscara por subsistema que permite habilitar selectivamente diferentes categorías de salida. El driver `myfirst` del Capítulo 23 adopta un patrón más sencillo, suficiente para los propósitos didácticos y extensible cuando el lector empiece a trabajar en un driver real.

### Rastrear el flujo de funciones sin saturar el log

Uno de los patrones de depuración que resulta tentador pero que produce más perjuicio que beneficio es escribir un `device_printf` al inicio y al final de cada función. La idea es razonable: si puedes ver la secuencia de llamadas, a menudo puedes deducir qué está ocurriendo. La práctica es incorrecta, porque la secuencia produce una cantidad abrumadora de salida, y esa salida se serializa a través de la ruta de printf, lo que altera el timing del código que se está probando.

El patrón correcto es usar un **modo detallado controlado por un sysctl**. Define un par de macros:

```c
#define MYF_LOG_ENTRY(sc)       \
    do {                        \
        if ((sc)->debug_verbose) \
            device_printf((sc)->dev, "ENTRY: %s\n", __func__); \
    } while (0)

#define MYF_LOG_EXIT(sc, err)   \
    do {                        \
        if ((sc)->debug_verbose) \
            device_printf((sc)->dev, "EXIT: %s err=%d\n", __func__, (err)); \
    } while (0)
```

Después, al inicio de cada función que el driver quiera rastrear, llama a `MYF_LOG_ENTRY(sc);`. En cada retorno, llama a `MYF_LOG_EXIT(sc, err);`. Cuando el indicador `debug_verbose` vale cero (el valor por defecto), las macros se reducen en compilación a una rama que nunca se toma y tienen un coste prácticamente nulo. Cuando vale uno (habilitado en tiempo de ejecución mediante `sysctl dev.myfirst.0.debug_verbose=1`), las macros producen un rastreo que cualquier desarrollador de drivers puede leer.

El indicador booleano mostrado arriba es deliberadamente simple. Introduce la idea de un control configurable en tiempo de ejecución sin exigir al lector que aprenda una nueva estructura al mismo tiempo. La Sección 8 de este capítulo desarrolla este patrón en un conjunto completo de macros, sustituye el booleano simple por una máscara de 32 bits (para que el operador pueda habilitar solo el rastreo de interrupciones, o solo el rastreo de I/O, en lugar de todo a la vez), mueve todo el mecanismo a `myfirst_debug.h` y añade sondas SDT a su lado. Por ahora, el patrón que debes interiorizar es que la salida de nivel de rastreo debe estar **desactivada por defecto** y **activable en tiempo de ejecución**. Los indicadores de depuración en tiempo de compilación (el viejo estilo `#ifdef DEBUG`) son una opción muy inferior: hay que recompilar el módulo para cambiarlos. Los controles en tiempo de ejecución permiten a un desarrollador habilitar el modo detallado sobre un sistema en funcionamiento en el momento en que lo necesita y deshabilitarlo en cuanto ha recogido la evidencia.

### Redirigir los logs a `dmesg` y a `/var/log/messages`

El `printf` del kernel escribe en el buffer de mensajes del kernel. Ese buffer es lo que `dmesg(8)` lee cuando el usuario lo ejecuta. La Sección 3 cubre `dmesg` en detalle; la versión breve es que el buffer es circular, de modo que los mensajes antiguos eventualmente son sobreescritos por los nuevos a medida que el buffer se llena.

El demonio `syslogd(8)` lee los mensajes nuevos del buffer y los escribe en el archivo de syslog correspondiente, normalmente `/var/log/messages`. La correspondencia entre prioridad y archivo se configura en `/etc/syslog.conf`. Por defecto, los mensajes en `LOG_INFO` o superiores van a `/var/log/messages`; los mensajes `LOG_DEBUG` se descartan a menos que estén habilitados explícitamente. `newsyslog(8)` rota `/var/log/messages` según las reglas de `/etc/newsyslog.conf`; esa rotación es la que produce los archivos `messages.0.gz`, `messages.1.gz`, y demás que el lector encontrará en el directorio de logs de un sistema que lleve tiempo en funcionamiento.

La consecuencia práctica es que la salida de registro de un driver vive en dos lugares a la vez. Un mensaje reciente está en el buffer del kernel (accesible mediante `dmesg`) y en `/var/log/messages` (accesible con cualquier herramienta de lectura de archivos). Un mensaje antiguo puede haber expirado del buffer pero seguir en `/var/log/messages` o en sus archivos históricos. Un mensaje muy antiguo puede haber salido de `/var/log/messages` hacia un archivo `messages.N.gz`, y puede leerse con `zcat`, `zgrep` o `zless`. La Sección 3 recorre estas herramientas en detalle.

Hay un detalle que merece mención aquí porque suele sorprender a los nuevos autores de drivers: **un mensaje `LOG_DEBUG` producido por el kernel no se escribe en `/var/log/messages` por defecto**. Si la salida de depuración del lector está en `LOG_DEBUG` y no puede encontrarla en el archivo de log, la causa suele estar en `/etc/syslog.conf`, no en el driver. Una prueba rápida: ejecuta el driver y luego ejecuta `dmesg | grep myfirst`. Si el mensaje aparece en `dmesg` pero no en `/var/log/messages`, el mensaje se produjo pero syslog lo filtró. Habilitar `LOG_DEBUG` en `/etc/syslog.conf` (si syslog está configurado para gestionar los mensajes de depuración del kernel) es una solución; usar `device_printf` o `device_log(dev, LOG_INFO, ...)` es una solución más habitual, porque esquiva el problema usando una prioridad más alta.

### Una recorrida concreta: el registro en el driver `myfirst`

El driver `myfirst` al inicio del Capítulo 23 ya tiene líneas de registro en los lugares obvios: attach imprime un mensaje de presentación, detach confirma la finalización, suspend y resume registran sus transiciones, y los errores de DMA registran una advertencia. La Etapa 1 del Capítulo 23 mejora esto de tres formas concretas.

En primer lugar, todo `printf` dentro del driver que tenga un `device_t` en su ámbito de visibilidad pasa a ser un `device_printf`. Un grep rápido localiza unas ocho líneas que todavía usan `printf` sin más en los manejadores `MOD_LOAD` y `MOD_UNLOAD` del módulo. Los que pertenecen al nivel del módulo se quedan como `printf` (no tienen `device_t`), pero utilizan el prefijo literal `"myfirst: "` para que el lector pueda seguir encontrándolos en el log. Un driver que llame a `printf` sin prefijo en cualquier punto es un driver que produce líneas de log que nadie podrá encontrar.

En segundo lugar, cada ruta de error se convierte en una llamada a `device_log` con una prioridad explícita:

```c
/* Before */
if (error) {
    device_printf(dev, "bus_dma_tag_create failed: %d\n", error);
    return (error);
}

/* After */
if (error) {
    device_log(dev, LOG_ERR, "bus_dma_tag_create failed: %d\n", error);
    return (error);
}
```

El cambio es pequeño y el comportamiento es el mismo en la mayoría de los kernels de prueba. El valor está en la prioridad. Un sistema en producción que vigile los mensajes `LOG_ERR` verá ahora los errores del driver; un entorno de desarrollo que filtre por `LOG_INFO` verá todo.

En tercer lugar, se introduce un nuevo patrón de logging para cambios de estado "poco frecuentes pero importantes". En concreto, el driver del Capítulo 23 registra con `LOG_NOTICE` tres eventos que el lector puede querer conocer pero sobre los que no desea recibir notificación en cada interrupción: la primera transferencia DMA satisfactoria después del attach, la transición a la suspensión en ejecución provocada por la detección de inactividad, y la primera reanudación satisfactoria tras una suspensión. No son errores, pero tampoco son eventos rutinarios; son los eventos que un desarrollador que revisa el log quiere ver destacados.

Los cambios de código exactos para la Etapa 1 se detallan en el ejercicio práctico de la Sección 2 al final de este apartado, y los archivos correspondientes se encuentran en `examples/part-05/ch23-debug/stage1-logging/`.

### Una nota sobre `uprintf` y `tprintf`

Dos funciones hermanas aparecen en `subr_prf.c` que los autores de drivers ven ocasionalmente pero raramente utilizan.

`uprintf(const char *fmt, ...)` escribe un mensaje en el terminal de control del proceso de usuario actual, si existe, *en lugar de* hacerlo en el buffer de mensajes del kernel. Es útil en los raros casos en que el manejador de `ioctl` de un driver quiere producir un mensaje de error que solo ve el usuario que ejecuta el comando, sin contaminar el registro del sistema. En la práctica, la mayoría de los drivers devuelven un código de error a través del mecanismo habitual de `errno` y no utilizan `uprintf`. El árbol `/usr/src/sys/kern/` tiene unos pocos lugares que la invocan, principalmente en sitios donde el kernel quiere advertir a un usuario sobre su propia acción (un evento relacionado con la seguridad, por ejemplo).

`tprintf(struct proc *p, int pri, const char *fmt, ...)` escribe en el terminal de un proceso específico. Es aún más raro. El capítulo 23 no utiliza ninguna de estas dos funciones directamente. El material de registro del capítulo 25 vuelve a ellas en el contexto de mensajes de diagnóstico dirigidos al usuario.

### Convenciones de formato

Unos pocos hábitos de formato hacen que la salida de registro sea mucho más fácil de leer:

1. **Termina cada línea con `\n`**. El `printf` del kernel no añade un salto de línea. Un mensaje sin salto de línea se unirá con el siguiente en `dmesg`, produciendo una salida como `myfirst0: interrupt receivedmyfirst0: interrupt received` que es prácticamente imposible de leer visualmente.

2. **Usa hexadecimal para los valores de registro y decimal para los contadores**. `status=0x80a1` es un registro de dispositivo; `count=27` es un contador. Mezclar ambas convenciones en un mismo driver produce una salida difícil de leer de un vistazo. Las conversiones `%x` y `%d` son la forma más sencilla de mantenerlos visualmente distintos.

3. **Pon los valores de las variables a la derecha, no a la izquierda**. `myfirst0: dma transfer failed (err=%d)` se escanea mejor que `myfirst0: %d was the err from dma transfer failed`. Los lectores humanos leen de izquierda a derecha, y colocar los datos variables al final de la línea se adapta a ese patrón de lectura.

4. **Usa etiquetas cortas para las categorías de mensajes repetidos**. `INFO:`, `WARN:`, `ERR:`, `DEBUG:` al inicio del contenido, después del prefijo del dispositivo, convierten un registro en algo que grep puede filtrar. La sección 8 usa esta convención en sus macros.

5. **No incluyas marcas de tiempo en el mensaje**. El kernel y syslog añaden sus propias marcas de tiempo; una marca de tiempo manual produce información duplicada y ruido visual.

6. **No incluyas el nombre de la función en los mensajes de producción**. Los nombres de función son útiles en la salida del modo verbose (donde las macros de entrada y salida los imprimen automáticamente), pero suponen un desperdicio en cada mensaje de producción. Un mensaje como `myfirst0: DMA completed` es más legible que `myfirst0: myfirst_dma_completion_handler: DMA completed`. Si quieres ver los nombres de función, activa el modo verbose.

7. **Sigue el estilo del driver**. Si el resto del driver usa "DMA" en mayúsculas, usa "DMA" en cada línea del registro. Si el resto usa "dma" en minúsculas, imita ese estilo. La coherencia es más fácil de grep que las mayúsculas y minúsculas mezcladas.

Son hábitos pequeños, pero dan sus frutos la primera vez que un registro se convierte en la única evidencia de un bug ocurrido en un sistema al que el lector no puede acceder de forma interactiva.

### Ejercicio: añade `device_printf()` en los puntos clave de tu driver

El complemento práctico de la sección 2 es un ejercicio breve. El lector debe:

1. Abrir `myfirst_pci.c`, `myfirst_sim.c`, `myfirst_intr.c`, `myfirst_dma.c` y `myfirst_power.c` uno por uno.

2. Por cada llamada a `printf` que tenga un `device_t` o un softc (que contiene un `device_t`) en su ámbito, reemplazarla por `device_printf(dev, ...)`.

3. Por cada mensaje en el camino de error, cambiar la llamada a `device_log(dev, LOG_ERR, ...)` si la condición es un error real, o a `device_log(dev, LOG_WARNING, ...)` si la condición es recuperable. Conservar `device_printf` para los mensajes informativos de rutina.

4. Para el banner de attach, añadir una única línea que indique la versión del driver: `device_printf(dev, "myfirst PCI driver %s attached\n", MYFIRST_VERSION);`. Definir `MYFIRST_VERSION` al inicio de `myfirst_pci.c` como un literal de cadena. El capítulo 23 lo subirá de `"1.5-power"` a `"1.6-debug"` en la refactorización final de la sección 8; por ahora, mantenlo en `"1.5-power-stage1"`.

5. Volver a construir el módulo, cargarlo y comprobar que `dmesg | grep myfirst0` muestra el banner de attach. Generar un fallo de DMA artificial (el sysctl `sim_force_dma_error` del simulador es una forma; el capítulo 17 introdujo el gancho) y comprobar que el mensaje de error aparece con la prioridad `LOG_ERR`.

6. Confirmar los cambios con un mensaje como "Chapter 23 Section 2: use device_printf consistently".

El lector puede comprobar su trabajo comparándolo con `examples/part-05/ch23-debug/stage1-logging/myfirst_pci.c` y los demás archivos de stage1, pero el aprendizaje ocurre al teclear, no al comprobar.

### Cerrando la sección 2

La sección 2 estableció las reglas fundamentales del registro. Usa `device_printf` cuando tengas un `device_t`. Usa `device_log` con una prioridad explícita cuando la prioridad importe. Registra lo suficiente para que el registro sea legible, y no tanto que se convierta en ruido. Envía la información de rutina a `LOG_INFO`, los errores reales a `LOG_ERR` y el rastreo verbose a `LOG_DEBUG` protegido por un sysctl.

La sección 3 sigue ahora la salida del registro desde el momento en que el driver la escribe hasta el momento en que un usuario la lee, a través de `dmesg`, el buffer de mensajes del kernel, `/var/log/messages` y la cadena de syslog. El trabajo del driver termina cuando llama a `device_printf`; las herramientas que el lector usa para ver lo que dijo el driver comienzan a partir de ahí.



## Sección 3: uso de `dmesg` y Syslog para diagnósticos

La sección 2 terminó con la llamada `device_printf` del driver. La sección 3 continúa desde el otro extremo: el momento en que un usuario ejecuta `dmesg`, abre `/var/log/messages` o busca en los archivos de syslog. Entre ambos extremos se encuentran el buffer de mensajes del kernel, el demonio `syslogd`, el rotador `newsyslog` y un pequeño conjunto de archivos de configuración que determinan silenciosamente qué mensajes aparecen y dónde. Entender esta cadena es lo que permite a un autor de drivers dejar de preocuparse por "¿por qué no aparecen mis mensajes?" y empezar a depurar el driver real.

### El buffer de mensajes del kernel

Cada mensaje del kernel que produce el driver va primero al **buffer de mensajes del kernel**. Es una única región circular de memoria que el kernel asigna durante el boot, dimensionada por defecto a unos 192 KB en FreeBSD 14 para amd64. El tamaño lo establece el parámetro ajustable `msgbufsize`; el tamaño actual es visible como:

```sh
sysctl kern.msgbufsize
```

El buffer es "circular" en el sentido de que cuando se llena, los mensajes nuevos sobrescriben los más antiguos. Esto importa cuando se busca un bug antiguo: si reiniciaste hace cinco horas, produjiste bastante salida de registro del driver y solo ahora vas a buscarla, puede que esa salida ya haya quedado fuera del buffer. La suposición segura es que el buffer almacena varios días de mensajes de rutina en un sistema tranquilo y unas pocas horas de mensajes en uno ocupado.

Dado que el buffer es un único bloque de memoria, leerlo es una operación barata. El comando `dmesg(8)` funciona llamando a `sysctl(8)` para leer `kern.msgbuf` e imprimir el contenido. El binario `dmesg` es pequeño; su código fuente está en `/usr/src/sbin/dmesg/dmesg.c` y vale la pena echarle un vistazo rápido si el lector tiene curiosidad.

La parte del buffer correspondiente al boot se preserva con más cuidado que la parte de ejecución en tiempo real. Durante el boot, el kernel escribe cada mensaje de autoconf, el banner de attach de cada driver y cada nota de detección de hardware en el buffer. El archivo `/var/run/dmesg.boot`, producido por el script de arranque `/etc/rc.d/dmesg`, captura el contenido del buffer en el momento en que la secuencia de boot finaliza y antes de que la salida en tiempo real empiece a sobreescribirlo. Leer `dmesg.boot` es la forma de ver los mensajes de boot cuando el sistema en ejecución ya los ha expulsado del buffer activo desde hace tiempo:

```sh
cat /var/run/dmesg.boot | grep myfirst
```

Para un autor de drivers, `dmesg.boot` es la respuesta a "¿se enganchó mi driver durante el boot?" cuando la respuesta se necesita horas después. El `dmesg` activo es la respuesta a "¿qué ha dicho mi driver desde entonces?".

### `dmesg` en la práctica

Las invocaciones más habituales merece la pena aprenderlas de memoria.

`dmesg` sin argumentos imprime el contenido actual del buffer del kernel en orden de más antiguo a más reciente. En un sistema que lleva funcionando un tiempo, esto produce mucha salida:

```sh
dmesg
```

Pásalo por `less` para desplazarte, o por `grep` para filtrar:

```sh
dmesg | less
dmesg | grep myfirst
dmesg | grep -i error
dmesg | tail -50
```

`dmesg -a` imprime todos los mensajes, incluidos aquellos con prioridades que el kernel está configurado para suprimir de la salida normal. En algunas configuraciones esto saca a la luz detalles adicionales de baja prioridad. En la mayoría de las configuraciones la salida es la misma que la de un `dmesg` simple.

`dmesg -M /var/crash/vmcore.0 -N /var/crash/kernel.0` lee el buffer de mensajes de un volcado de memoria guardado, en lugar de hacerlo del kernel activo. Así es como se ven los últimos mensajes que produjo un kernel antes de que entrara en pánico, cuando el propio pánico impidió que el camino de registro normal funcionara. El material sobre `kgdb` de la sección 4 vuelve a esto.

La forma `dmesg -c` (que en algunos sistemas vacía el buffer después de leerlo) no está disponible en la forma en que el kernel de FreeBSD expone el buffer, porque el buffer no se vacía al leerlo; la lectura produce una copia y el buffer activo sigue recibiendo mensajes nuevos. Los usuarios que vienen de Linux a veces esperan `dmesg -c` y se sorprenden.

Un hábito útil durante el desarrollo de drivers es ejecutar `dmesg | grep myfirst | tail` después de cada carga del módulo, cada prueba y cada evento interesante. La repetición construye el reflejo de "¿qué acaba de decir el driver?" que da sus frutos la primera vez que ocurre algo inesperado.

### Registros permanentes en `/var/log/messages`

El buffer de mensajes del kernel es volátil; un reinicio lo vacía. Para cualquier cosa más a largo plazo, FreeBSD usa `syslogd(8)`, el demonio syslog estándar de UNIX, que lee los mensajes nuevos a medida que el kernel los produce y escribe los seleccionados en archivos del disco.

El archivo canónico, en casi todos los sistemas FreeBSD, es `/var/log/messages`. Este archivo recibe la mayoría de los mensajes del kernel con prioridad `LOG_INFO` o superior, y también la mayoría de los mensajes de los demonios del userland. La correspondencia entre prioridad y archivo de destino está en `/etc/syslog.conf`. Un fragmento relevante tiene este aspecto:

```text
*.notice;kern.debug;lpr.info;mail.crit;news.err		/var/log/messages
```

La sintaxis son pares "facility.priority". `kern.debug` significa "todos los mensajes del kernel con prioridad `LOG_DEBUG` o superior". `*.notice` significa "todas las facilities con prioridad `LOG_NOTICE` o superior". El lector debe saber que esta sintaxis existe; entenderla es útil cuando un tipo específico de mensaje debe ir a un archivo específico.

Dado que `/var/log/messages` es un archivo de texto ordinario, todas las herramientas disponibles funcionan con él:

```sh
tail -f /var/log/messages
tail -50 /var/log/messages
grep myfirst /var/log/messages
less /var/log/messages
wc -l /var/log/messages
```

Especialmente útil durante el desarrollo de drivers:

```sh
tail -f /var/log/messages | grep myfirst
```

Esto proporciona un flujo en tiempo real únicamente de la salida del driver, que es lo que la mayoría de las sesiones de depuración realmente necesitan.

Cuando los mensajes envejecen, se mueven a `/var/log/messages.0`, luego a `.1`, luego a `.N.gz` a medida que ocurre la rotación. La rotación la realiza `newsyslog(8)`, que es invocado por un trabajo periódico de `cron` y lee sus reglas desde `/etc/newsyslog.conf`. Una regla típica para `/var/log/messages` rota el archivo cuando alcanza un umbral de tamaño, conserva un número determinado de archivos comprimidos y reinicia `syslogd` para que abra el nuevo archivo.

Para buscar en los archivos comprimidos, la herramienta es `zgrep`:

```sh
zgrep myfirst /var/log/messages.*.gz
```

O, para todo el historial incluido el archivo actual:

```sh
grep myfirst /var/log/messages && zgrep myfirst /var/log/messages.*.gz
```

Los lectores que buscan un bug ocurrido hace días recurren a estos comandos habitualmente.

### Salida por consola frente a salida en buffer

Un matiz confunde a muchos autores de drivers la primera vez que lo encuentran. El kernel tiene dos destinatarios para sus mensajes: el buffer en memoria y la consola del sistema. La consola es el terminal físico de un portátil (o la consola VGA en una VM, o la consola serie en un equipo sin pantalla). Un mensaje puede ir a uno, al otro o a ambos, dependiendo de su prioridad y del nivel de registro de consola vigente.

El nivel de log de la consola está controlado por `kern.consmsgbuf_size` (la cantidad de memoria que utiliza la consola) y, más importante aún, por `kern.msgbuflock` y el filtro de prioridad de la consola. El ajuste más sencillo es:

```sh
sysctl -d kern.log_console_level
```

En FreeBSD, los mensajes de alta prioridad (LOG_WARNING y superiores) se muestran siempre en la consola. Los mensajes de menor prioridad se almacenan en el buffer pero no se muestran. Por eso un mensaje de pánico aparece en la consola independientemente del nivel de ejecución: tiene prioridad `LOG_CRIT` o superior, y la ruta de salida por consola del kernel evita el almacenamiento en buffer habitual.

Para quien desarrolla drivers, la consecuencia práctica es que `device_log(dev, LOG_ERR, ...)` tiene más posibilidades de ser visto por una persona que `device_printf(dev, ...)`, porque el primero llega a la consola y el segundo va únicamente al buffer. La Sección 2 del Capítulo 23 ya lo señalaba; la Sección 3 lo refuerza explicando los mecanismos internos.

Un driver que genera un torrente de mensajes LOG_ERR satura la consola y, en casos extremos, ralentiza el sistema, porque la ruta de salida por consola tiene un coste no despreciable. El rate limiting es la solución; el Capítulo 25 lo explica en detalle. Por ahora, la lección es que la prioridad importa.

### Revisión de logs en la práctica

El patrón habitual para leer los logs del driver durante el desarrollo es el siguiente:

1. Cargar el módulo.
2. Ejecutar la prueba.
3. Descargar el módulo.
4. Copiar la salida de `dmesg` a un archivo para su análisis.

La secuencia en forma de shell:

```sh
# Clean slate
sudo kldunload myfirst 2>/dev/null
sudo kldload ./myfirst.ko

# Test
dev_major=$(stat -f '%Hr' /dev/myfirst0)
echo "Running driver test..."
some_test_program

# Capture
dmesg | tail -200 > /tmp/myfirst-test-$(date +%Y%m%d-%H%M%S).log

# Clean slate again
sudo kldunload myfirst

# Now inspect the log at leisure
less /tmp/myfirst-test-*.log
```

Vale la pena enunciar explícitamente varios hábitos que este patrón fomenta:

**Empieza desde cero, siempre.** Descarga cualquier instancia existente del módulo antes de cargar el que deseas probar. De lo contrario, podrías estar ejecutando accidentalmente una compilación anterior y preguntándote por qué tu nuevo código no tiene el efecto esperado. Una descarga también deja el buffer de `dmesg` más legible, porque tu nueva sesión parte de un estado conocido.

**Guarda cada sesión interesante.** Un log guardado en un archivo es un log que puedes comparar con diff, filtrar con grep, compartir y retomar la semana que viene. Un log que dejaste en el buffer activo es un log que puede haberse perdido cuando vuelvas a él.

**Usa marcas de tiempo en los nombres de archivo.** La convención `date +%Y%m%d-%H%M%S` del fragmento produce nombres de archivo como `myfirst-test-20260419-143027.log`. Cuando acumulas una docena de archivos de log de un día de pruebas, esta es la única manera de encontrar el que buscas.

**Ajusta `tail -N` a una ventana razonable.** El valor predeterminado es 10 líneas, que casi nunca es suficiente para una prueba de driver. De 200 a 500 es normalmente el tamaño adecuado: suficiente para capturar un ciclo de prueba completo, y lo bastante corto para leerlo de una vez. Si tu prueba produce más salida que eso, o bien la prueba es demasiado grande o el logging es demasiado verboso.

### Filtrado de la salida

La cadena de herramientas de `grep` es tu aliada. Algunos patrones se repiten con suficiente frecuencia como para que valga la pena interiorizarlos.

**Mostrar solo la salida del driver:**

```sh
dmesg | grep '^myfirst'
```

El ancla `^myfirst` solo coincide con las líneas que comienzan con el nombre del dispositivo. Sin el ancla, también coincidirías con líneas que por casualidad contienen "myfirst" en su texto (por ejemplo, mensajes del kernel sobre el descriptor de archivo del módulo).

**Mostrar solo los errores:**

```sh
dmesg | grep -iE 'error|fail|warn|fault'
```

Sin distinción entre mayúsculas y minúsculas, con una expresión regular extendida que coincide con varias palabras de error habituales.

**Mostrar un intervalo concreto:**

```sh
awk '/START OF TEST/,/END OF TEST/' /var/log/messages
```

Esto depende de que el arnés de prueba imprima las marcas "START OF TEST" y "END OF TEST" en los límites. Un script que hace esto es un script cuyos logs son fáciles de segmentar.

**Extraer solo las marcas de tiempo y la primera palabra:**

```sh
awk '{print $1, $2, $3, $5}' /var/log/messages | grep myfirst
```

Para un escaneo de alto nivel de un log con mucha actividad.

**Contar cuántos mensajes hay de cada tipo:**

```sh
dmesg | grep myfirst | awk '{print $2}' | sort | uniq -c | sort -rn
```

Esto cuenta los mensajes agrupados por su segunda palabra, que (siguiendo la convención de la sección 2) suele ser la categoría, como INFO, WARN, ERR o DEBUG. Útil para detectar situaciones como «¿por qué mi driver produce 30.000 mensajes DEBUG y solo 2 INFO?».

### Configuración permanente: `/etc/syslog.conf` y `/etc/newsyslog.conf`

Para la mayor parte del trabajo con drivers, la configuración predeterminada de syslog es suficiente. El capítulo 23 no pide que la modifiques. Pero vale la pena saber que existen esos dos archivos y, a grandes rasgos, qué hacen.

`/etc/syslog.conf` es leído por `syslogd` al arranque y al recibir `SIGHUP`. Contiene reglas que asocian «facilidad y prioridad» con un «destino». La facilidad es el tipo de productor (`kern`, `auth`, `mail`, `daemon`, `local0` hasta `local7`), y la prioridad es el nivel de prioridad de syslog. Los mensajes de un driver siempre tienen la facilidad `kern` (porque provienen del kernel) y la prioridad que el driver especificó.

Un experimento útil, una vez que hayas leído el capítulo 23, es añadir una línea como la siguiente:

```text
kern.debug					/var/log/myfirst-debug.log
```

a `/etc/syslog.conf`, ejecutar `touch` sobre el archivo y recargar syslogd con `service syslogd reload`. A partir de ese momento, todos los mensajes del kernel de nivel debug se dirigen a un archivo separado. Si a continuación habilitas el modo verbose en el driver `myfirst` (el sysctl de la sección 8), el archivo separado captura únicamente la salida verbosa y el archivo `messages` principal permanece limpio. Esta técnica merece conocerse: permite habilitar un logging verboso en profundidad sin inundar `/var/log/messages` con todo.

`/etc/newsyslog.conf` es leído por `newsyslog` cuando rota los logs. Una regla típica tiene este aspecto:

```text
/var/log/messages    644  5    100    *     JC
```

Los campos, de izquierda a derecha: ruta del archivo, modo, número de rotaciones a conservar, tamaño máximo en KB, hora de rotación, indicadores. Para el archivo `myfirst-debug.log` que acabas de añadir, una regla adecuada de newsyslog evita que los logs crezcan indefinidamente:

```text
/var/log/myfirst-debug.log  644  3  500  *  JC
```

De nuevo, esto no es un requisito del capítulo; es un patrón que vale la pena conocer.

### Correlación de mensajes a lo largo de un ciclo de arranque

Una de las tareas habituales de depuración es «el driver funcionaba tras el último arranque, pero ahora no». Quieres comparar lo que ocurrió en el último arranque con lo que está ocurriendo ahora. Hay dos caminos:

**Primera opción:** leer `/var/run/dmesg.boot`, que capturó los mensajes del momento del arranque, y compararlo con el `dmesg` actual:

```sh
diff <(grep myfirst /var/run/dmesg.boot) <(dmesg | grep myfirst)
```

Esto muestra exactamente qué mensajes son nuevos desde el arranque. Útil para detectar sorpresas en tiempo de ejecución.

**Segunda opción:** buscar con grep en los archivos históricos de `/var/log/messages`:

```sh
zgrep myfirst /var/log/messages.*.gz | head -100
```

Esto muestra el historial del driver durante los últimos días, a través de varios reinicios. Útil para detectar patrones como «empezó a fallar el martes».

Un driver que registra su versión en el attach facilita enormemente esta tarea. Si `dmesg.boot` muestra `myfirst0: myfirst PCI driver 1.5-power attached` y el arranque actual muestra `myfirst0: myfirst PCI driver 1.6-debug attached`, sabes de inmediato que son versiones distintas y que se produjo un cambio en el código entre ellas. El ejercicio de la fase 1 del capítulo 23 añade exactamente esta línea de versión en el attach precisamente por esa razón.

### Ejercicio: generar un mensaje conocido del driver y confirmar que aparece

Un pequeño ejercicio práctico complementario de la sección 3. Sigue estos pasos:

1. Reconstruye y carga el driver `1.5-power-stage1` del ejercicio de la sección 2. Confirma que el banner de attach aparece en `dmesg`.

2. Activa el sysctl `sim_force_dma_error=1` del simulador para hacer que la siguiente transferencia DMA falle. Ejecuta una transferencia DMA. Confirma que el mensaje de error aparece tanto en `dmesg` como en `/var/log/messages`.

3. Produce una línea informativa verbosa: establece el sysctl debug_verbose (que aún no existe) en 1. Como el sysctl todavía no existe, este paso fallará, y debes anotar de qué forma falló. Esto es intencionado; la sección 8 añade el sysctl.

4. De las líneas que sí aparecieron, usa `awk` o `grep` para extraer solo las que tienen el prefijo `myfirst0:`, y cuéntalas.

5. Crea el archivo `/var/log/myfirst-debug.log` añadiendo una línea `kern.debug` a `/etc/syslog.conf` como se describió antes. Recarga `syslogd`. Confirma que el archivo se ha creado. Confirma que aún no aparecen mensajes en él (porque el driver `myfirst` todavía no produce salida `LOG_DEBUG`). Toma nota de volver a esto en la sección 8.

El ejercicio es breve. Su valor radica en que ya has utilizado de verdad `dmesg`, `/var/log/messages`, `grep` y `syslog.conf` con salida real de un driver. Los capítulos futuros darán por sentado que estas herramientas te resultan familiares.

### Cierre de la sección 3

La sección 3 ha seguido el log desde el momento en que el driver llama a `device_printf` o a `device_log` hasta el momento en que el usuario ve la salida. Los mensajes van primero al buffer circular de mensajes del kernel, accesible a través de `dmesg`. Una instantánea del momento del arranque se conserva en `/var/run/dmesg.boot`. El demonio `syslogd` lee los mensajes nuevos y los escribe en `/var/log/messages` de acuerdo con las reglas de `/etc/syslog.conf`. El demonio `newsyslog` rota los archivos según `/etc/newsyslog.conf`. La salida por consola es automática para los mensajes de alta prioridad y está limitada para los de baja prioridad.

Con la cadena de logging bien entendida, la sección 4 pasa a la otra pieza esencial de la base de depuración: el kernel de debug. Un driver cuyos bugs siempre provocan un panic silencioso y no producen nada en el log es un driver que no puede depurarse. Un kernel de debug hace que esos bugs silenciosos sean ruidosos.



## Sección 4: Builds de kernel de debug y opciones

Un kernel de debug no es un kernel diferente; es el mismo kernel con comprobaciones adicionales, ayudas de depuración adicionales y un pequeño conjunto de características compiladas que un kernel de producción no tiene. El código fuente del driver no cambia. Las herramientas de espacio de usuario no cambian. Lo que cambia es que el kernel en ejecución ahora detecta más de sus propios bugs, conserva más contexto cuando ocurre un panic y ofrece más formas de inspeccionarse a sí mismo.

Esta sección es la más extensa del capítulo porque el kernel de debug es la base sobre la que se apoya el resto. Cada herramienta posterior depende de tener configuradas las opciones correctas. DTrace depende de `KDTRACE_HOOKS`. `ddb` depende de `DDB` y `KDB`. `INVARIANTS` detecta bugs que de otro modo producirían corrupción de memoria silenciosa. `WITNESS` detecta violaciones del orden de los locks que de otro modo producirían panics intermitentes semanas después. Cada opción tiene un coste, un beneficio y un contexto en el que es apropiada; la sección 4 los recorre uno a uno.

### Las opciones de debug principales

El kernel de FreeBSD se configura mediante un archivo de configuración del kernel, como `/usr/src/sys/amd64/conf/GENERIC`. El archivo de configuración es una lista de líneas `options` que habilitan o deshabilitan características en tiempo de compilación. Un kernel de debug se produce partiendo de `GENERIC` (u otra configuración base), añadiendo opciones de debug, reconstruyendo, instalando y arrancando el resultado.

Las opciones que importan para la depuración de drivers se agrupan en cuatro categorías: el depurador del kernel, las comprobaciones de consistencia, la infraestructura de debug de locks y la infraestructura de trazado.

**Grupo del depurador del kernel.** Estas opciones habilitan el depurador `ddb` integrado en el kernel y la infraestructura que lo sustenta.

- `options KDB`: habilita el framework del depurador del kernel. Es la opción paraguas que permite que cualquier backend (`DDB`, `GDB` por puerto serie, etc.) se conecte.
- `options DDB`: habilita el depurador DDB integrado en el kernel. DDB es el backend que usa la mayoría de los autores de drivers. Se ejecuta dentro del kernel y entiende las estructuras de datos del kernel.
- `options DDB_CTF`: habilita la carga de datos CTF (Compact C Type Format) en el kernel, lo que permite que `ddb` y DTrace impriman información con conocimiento de tipos sobre las estructuras del kernel. Requiere `makeoptions WITH_CTF=1` en la configuración.
- `options DDB_NUMSYM`: habilita las búsquedas de símbolos por número dentro de DDB. Útil para examinar direcciones específicas.
- `options KDB_UNATTENDED`: ante un panic, no detiene la ejecución para entrar en `ddb`. En su lugar, el kernel vuelca el núcleo (si `dumpdev` está configurado), reinicia y continúa. Es la configuración adecuada para las máquinas virtuales de laboratorio, donde no quieres perder el kernel en una sesión del depurador en cada panic.
- `options KDB_TRACE`: ante cualquier entrada al depurador (por panic o por otro motivo), imprime automáticamente una traza de pila. Ahorra un paso en casi todas las sesiones de diagnóstico.
- `options BREAK_TO_DEBUGGER`: permite que una interrupción en la consola serie o USB abra `ddb`. Útil en sistemas de prueba sin pantalla.
- `options ALT_BREAK_TO_DEBUGGER`: combinación de teclas alternativa para el mismo efecto.

Una configuración típica de kernel de debug incluye todas estas opciones. El kernel `GENERIC` en amd64 ya habilita `KDB`, `KDB_TRACE` y `DDB`; las opciones adicionales que debes habilitar son `KDB_UNATTENDED`, `INVARIANTS`, `INVARIANT_SUPPORT`, `WITNESS` y otras similares.

**Grupo de comprobaciones de consistencia.** Estas opciones habilitan aserciones adicionales en tiempo de ejecución en el código del kernel.

- `options INVARIANTS`: activa las aserciones en el kernel. Casi todos los archivos de `/usr/src/sys/*` contienen llamadas a `KASSERT`, y esas llamadas solo hacen algo cuando `INVARIANTS` está habilitado. Sin `INVARIANTS`, una expresión como `KASSERT(ptr != NULL, ("oops"))` se compila como nada. Con `INVARIANTS`, provoca un panic del kernel en caso de fallo. El coste es medible (quizás un 10% del tiempo de CPU del kernel en un sistema con carga elevada), pero habitualmente es insignificante durante el desarrollo de drivers, porque el kernel dedica menos tiempo por operación que la aplicación que lo utiliza. La ventaja es que los bugs que de otro modo corromperían datos silenciosamente quedan atrapados en el momento exacto en que ocurren.
- `options INVARIANT_SUPPORT`: la maquinaria que `INVARIANTS` utiliza internamente. Es obligatorio si `INVARIANTS` está habilitado; también es necesario por separado si algún módulo cargable se compiló con `INVARIANTS`. Olvidarlo produce errores de "símbolo no resuelto" al cargar el módulo.
- `options DIAGNOSTIC`: comprobaciones adicionales ligeras, menos agresivas que `INVARIANTS`. Algunos subsistemas lo usan para verificaciones lo suficientemente baratas como para ejecutarse en producción, pero demasiado lentas para bucles muy ajustados.

**Grupo de depuración de locks.** Estas opciones habilitan la infraestructura que verifica la disciplina de sincronización del driver.

- `options WITNESS`: habilita el verificador de orden de locks. WITNESS rastrea cada lock que toma el kernel, registra el orden en que se adquieren y se queja de forma clara cuando un thread adquiere locks en un orden que podría provocar un deadlock si otro thread los tuviera en sentido contrario. Un driver con un bug de orden de locks generará un informe de WITNESS la primera vez que se dé la secuencia incorrecta, incluso si no se produce ningún deadlock real. Esto supone una ventaja enorme para la depuración, porque detecta el bug antes de que el deadlock llegue a manifestarse.
- `options WITNESS_SKIPSPIN`: omite la comprobación de orden de locks para los spinlocks. Los spinlocks tienen sus propias restricciones que el verificador general no está diseñado para evaluar, y el verificador puede producir falsos positivos con ellos. Habilitar `WITNESS_SKIPSPIN` mantiene el verificador útil para el caso habitual del mutex.
- `options WITNESS_KDB`: ante una violación de orden de locks, entra directamente en `ddb` en lugar de limitarse a registrar el evento. Es agresivo; resulta adecuado para una VM donde iniciar una sesión manual de `ddb` es sencillo.
- `options DEBUG_LOCKS`: depuración adicional de la API genérica de locks (independiente de WITNESS). Detecta el uso de locks no inicializados, locks tomados en un contexto incorrecto y problemas relacionados.
- `options LOCK_PROFILING`: instrumentación que permite a `lockstat` medir la contención de locks. No es estrictamente una opción de depuración (es una opción de profiling) y tiene un coste elevado; úsala solo cuando estés buscando contención, no por defecto.
- `options DEBUG_VFS_LOCKS`: depuración de locks específica del VFS. Útil solo para el desarrollo de drivers de sistema de archivos.

**Grupo de infraestructura de trazado.** Estas opciones habilitan los frameworks DTrace y KTR.

- `options KDTRACE_HOOKS`: los hooks de DTrace a nivel de todo el kernel. Sin esto, DTrace no tiene nada a lo que engancharse. Son ligeros incluso cuando no hay ningún script de DTrace en ejecución, porque los hooks son principalmente ranuras de punteros a función que no hacen nada.
- `options KDTRACE_FRAME`: información de desenrollado de pila que DTrace utiliza para las acciones `stack()`. Necesario para obtener trazas de pila con sentido dentro de los scripts de DTrace.
- `makeoptions WITH_CTF=1`: una línea de `makeoptions` (no de `options`) que habilita la generación de CTF para el kernel. CTF es la información de tipos que DTrace y DDB usan para entender la disposición de las estructuras del kernel.
- `options KTR`: el trazador de eventos con buffer en anillo dentro del kernel. Es distinto de `ktrace`; KTR es un buffer de alto rendimiento para eventos del kernel que resulta útil principalmente para programadores de las capas más bajas del kernel. La mayoría de los autores de drivers no necesitan `KTR` y pueden dejarlo desactivado; el Capítulo 23 lo menciona solo para nombrarlo correctamente.
- `options KTR_ENTRIES`, `KTR_COMPILE`, `KTR_MASK`: los parámetros de configuración de KTR. Consulta `/usr/src/sys/conf/NOTES` para ver la lista completa.

Para el Capítulo 23, el lector necesita `KDB`, `DDB`, `DDB_CTF`, `KDB_UNATTENDED`, `KDB_TRACE`, `INVARIANTS`, `INVARIANT_SUPPORT`, `WITNESS`, `WITNESS_SKIPSPIN`, `KDTRACE_HOOKS` y `makeoptions DEBUG=-g` para los símbolos. Si el lector siguió los prerrequisitos del Capítulo 22, estas opciones ya estarán habilitadas.

### Construcción de un kernel de depuración

Construir un kernel personalizado es una operación estándar en FreeBSD. El proceso es el siguiente:

1. Copia la configuración `GENERIC` con un nombre nuevo:

   ```sh
   cd /usr/src/sys/amd64/conf
   sudo cp GENERIC MYDEBUG
   ```

2. Edita `MYDEBUG` para añadir las opciones de depuración. Una adición mínima:

   ```text
   ident MYDEBUG

   options INVARIANTS
   options INVARIANT_SUPPORT
   options WITNESS
   options WITNESS_SKIPSPIN
   options KDB_UNATTENDED

   makeoptions DEBUG=-g
   makeoptions WITH_CTF=1
   ```

   `DDB`, `KDB`, `KDTRACE_HOOKS` y `DDB_CTF` ya están presentes en `GENERIC` en amd64 a partir de FreeBSD 14, por lo que no es necesario volver a añadirlos. Si utilizas una arquitectura que no sea x86, comprueba el archivo `conf/GENERIC` correspondiente para ver qué opciones ya están incluidas.

   Un punto de confusión frecuente, aunque menor: `/usr/src/sys/amd64/conf/` contiene los *archivos fuente de configuración* del kernel. El directorio `/usr/src/sys/amd64/compile/` que algunos tutoriales mencionan es un directorio de salida del build que `config(8)` poblaba históricamente; en un sistema FreeBSD moderno, los productos del build residen bajo `/usr/obj/usr/src/amd64.amd64/sys/<KERNCONF>/`, y la ruta `compile/` ya no es el lugar donde se edita ni donde se busca el código fuente.

3. Construye el kernel:

   ```sh
   cd /usr/src
   sudo make -j4 buildkernel KERNCONF=MYDEBUG
   ```

   En una VM razonable, esto lleva entre diez y veinte minutos. En una VM más lenta, media hora. El build es el paso individual más largo de todo el proceso de depuración, pero solo hay que ejecutarlo una vez por cada cambio de configuración, no una vez por cada prueba.

4. Instala el nuevo kernel:

   ```sh
   sudo make installkernel KERNCONF=MYDEBUG
   ```

   Esto copia los archivos del kernel en `/boot/kernel` y hace una copia de seguridad del kernel anterior en `/boot/kernel.old`. Si el nuevo kernel no arranca, puedes seleccionar `kernel.old` desde el menú del cargador de arranque y continuar.

5. Reinicia.

6. Comprueba:

   ```sh
   uname -a
   ```

   La salida debería mencionar `MYDEBUG` en la cadena de identificación del kernel.

7. Confirma que las opciones de depuración están activas:

   ```sh
   sysctl debug.witness.watch
   sysctl kern.witness
   ```

   En un kernel con `WITNESS` activado, estos producen salida. En un kernel sin `WITNESS`, los sysctls no existen.

El tiempo total desde "querer un kernel de depuración" hasta "tener un kernel de depuración en marcha" suele ser inferior a una hora. El tiempo invertido se recupera en la primera sesión de depuración, porque el kernel de depuración es el que detecta los errores.

### Cuándo usar `INVARIANTS`

`INVARIANTS` es la opción de depuración individual más importante para un desarrollador de drivers. También es la más incomprendida.

Cuando un programador del kernel escribe `KASSERT(condition, ("message %d", val))`, la intención es: esta condición debe ser verdadera en este punto; si alguna vez es falsa, algo ha ido mal de un modo del que el código no puede recuperarse. Sin `INVARIANTS`, `KASSERT` se compila como nada; la condición no se verifica. Con `INVARIANTS`, `KASSERT` evalúa la condición y llama a `panic` si falla, imprimiendo el mensaje.

Considera un ejemplo concreto del driver `myfirst`. La ruta de envío de DMA (Capítulo 21) mantiene un lock de buffer, establece una marca en vuelo y llama a `bus_dmamap_sync`. Un error que invoque la ruta de envío dos veces sin esperar a que se complete la primera es un error de doble envío, y puede corromper el DMA. Un `KASSERT` al comienzo de la ruta de envío lo detecta:

```c
static int
myfirst_dma_submit(struct myfirst_softc *sc, ...)
{
    KASSERT(!sc->dma_in_flight,
        ("myfirst_dma_submit: previous transfer still in flight"));
    ...
}
```

Sin `INVARIANTS`, esta línea no hace nada. Con `INVARIANTS`, la primera vez que un error envía una segunda transferencia antes de que se complete la primera, el kernel entra en pánico con el mensaje. Examinas el backtrace, ves el doble envío y corriges el error.

Esta es la forma en que `INVARIANTS` aporta valor: transforma la corrupción silenciosa en pánicos ruidosos. La contrapartida es que un autor de drivers que no escribe ningún `KASSERT` no obtiene ningún beneficio de `INVARIANTS`; la opción solo es útil si el código contra el que se ejecuta tiene aserciones. El driver `myfirst` en la Etapa 1 tiene unas pocas; la Sección 7 añade más; la Sección 8 las mueve al header de depuración.

Junto a `KASSERT` existen otros helpers de aserción:

- `MPASS(expr)`: una forma compacta de `KASSERT(expr, (...))`. Se expande en la misma comprobación con un mensaje proporcionado por el compilador. Es útil cuando la condición se explica por sí sola.
- `VNASSERT(expr, vp, msg)`: aserción especializada para vnodes. Los autores de drivers raramente la usan directamente.
- `atomic_testandset_int`, `atomic_cmpset_int`: no son helpers de aserción, sino operaciones comunes en la sincronización de drivers que tienen sus propias variantes con conocimiento de depuración.

Lee `KASSERT` en su contexto: es tanto una ayuda para la documentación como una ayuda para la depuración. Si examinas el código fuente de una función desconocida, encontrarás los `KASSERT`s al principio y conocerás de inmediato los invariantes que el código espera. Un driver que acumula `KASSERT`s con el tiempo se vuelve autodocumentado de un modo que la prosa simple no puede igualar.

### Cuándo usar `WITNESS` y `DEBUG_LOCKS`

`WITNESS` es la otra mitad del par de depuración de drivers. Donde `INVARIANTS` detecta errores de lógica, `WITNESS` detecta errores de sincronización.

La idea básica: cada lock tiene un identificador, el kernel registra el conjunto de locks que mantiene cada thread y, cuando un thread intenta adquirir un lock, `WITNESS` comprueba si el orden de adquisición es coherente con las adquisiciones anteriores en el historial del kernel. Si el thread A toma el lock X y luego el lock Y, y más tarde el thread B toma el lock Y y luego el lock X, los dos threads forman un posible deadlock: si A mantiene X esperando Y mientras B mantiene Y esperando X, ninguno puede avanzar. `WITNESS` detecta la inconsistencia la segunda vez que un thread intenta adquirir los locks en el orden incorrecto, aunque el deadlock real no se produzca durante la prueba.

Una queja de `WITNESS` tiene un aspecto similar a este:

```text
lock order reversal:
 1st 0xfffff80001a23200 myfirst_sc (myfirst_sc) @ /usr/src/sys/dev/myfirst/myfirst_pci.c:523
 2nd 0xfffff80001a23240 some_other_lock (some_other_lock) @ /some/other/file.c:789
witness_order_list_add: lock order reversal
```

El mensaje identifica ambos locks, sus direcciones, los archivos y números de línea donde se adquirieron y la inversión del orden. Un autor de drivers lee esto y sabe de inmediato dónde buscar.

`WITNESS` interactúa con las demás opciones de depuración. `WITNESS_KDB` entra en `ddb` al detectar una violación en lugar de simplemente registrarla. `WITNESS_SKIPSPIN` omite los spinlocks, que tienen reglas distintas (no pueden dormir, tienen gestión de prioridad y la disciplina de orden es ligeramente diferente). `DEBUG_LOCKS` añade comprobaciones adicionales sobre `WITNESS`: detección de doble desbloqueo, detección de desbloqueo por el thread incorrecto y detección del uso de mutexes no inicializados. Para el desarrollo de drivers, la recomendación por defecto es activar los tres (`WITNESS`, `WITNESS_SKIPSPIN`, `DEBUG_LOCKS`).

El coste de `WITNESS` es medible. Cada adquisición de lock añade unas pocas docenas de instrucciones y un acceso a una línea de cache de memoria. En una carga de trabajo intensiva en drivers, esto puede suponer entre un 20 y un 30 por ciento más de lentitud respecto a un kernel sin depuración. Para el desarrollo y las pruebas, esto es aceptable. Para un kernel de producción, `WITNESS` suele estar desactivado.

El recorrido por errores comunes de la Sección 7 incluye un laboratorio con `WITNESS`: introduces deliberadamente una violación del orden de locks en el driver `myfirst`, vuelves a compilar, recargas, activas la ruta y observas cómo `WITNESS` la detecta. Ese laboratorio es la forma más rápida de interiorizar cómo funciona la herramienta.

### Cuándo usar `DEBUG_MEMGUARD` y `MEMGUARD`

`MEMGUARD` es un asignador de depuración de memoria más pesado. Asigna memoria con páginas de guarda a ambos lados, detecta escrituras más allá del límite de la asignación y detecta el uso tras la liberación (use-after-free). Se activa con `options DEBUG_MEMGUARD` y se configura en tiempo de ejecución mediante `sysctl vm.memguard.desc`.

MEMGUARD es costoso: utiliza mucha más memoria que un asignador normal a causa de las páginas de guarda, y su ruta de asignación es más lenta. El uso correcto es dirigido: se activa para un tipo de malloc específico que el driver usa de forma intensiva, no para todo el kernel. Si el driver `myfirst` tiene un tipo de malloc `M_MYFIRST`, puedes activar MEMGUARD solo para ese tipo:

```sh
sysctl vm.memguard.desc="M_MYFIRST"
```

Tras un reinicio (MEMGUARD necesita inicializar su arena en el arranque), las asignaciones de memoria `M_MYFIRST` pasarán por MEMGUARD. Una escritura más allá del final de una asignación provoca un pánico de inmediato. Un uso tras la liberación también provoca un pánico de inmediato. Puedes ejecutar pruebas y hacer que el kernel detecte errores de heap en la memoria del driver sin ralentizar el resto del sistema.

El Capítulo 23 no hace un uso intensivo de MEMGUARD, porque los patrones de memoria del driver `myfirst` son sencillos. La Sección 7 lo menciona como una herramienta más en el arsenal para drivers con asignaciones más complejas. Si en algún momento escribes un driver con colas dinámicas, pools de buffers o asignaciones de larga duración, vale la pena volver a MEMGUARD.

### Compromisos: rendimiento frente a visibilidad de depuración

La tentación, cuando se aprenden por primera vez estas opciones, es activarlo todo. Esta es la elección correcta durante el desarrollo. Es la elección equivocada para producción.

Un kernel con `INVARIANTS`, `WITNESS`, `DEBUG_LOCKS`, `KDTRACE_HOOKS`, `DDB` y `MEMGUARD` activados es notablemente más lento que `GENERIC`: quizás un 20 % más lento en general, un 50 % más lento en cargas de trabajo con muchos locks, según lo que esté haciendo la máquina. En una estación de trabajo de desarrollo, un 20 % más de lentitud es aceptable. En un servidor de producción, un 20 % más de lentitud implica un 20 % más de hardware para realizar el mismo trabajo.

La distinción entre kernels de depuración y kernels de producción es nítida en la práctica. Los desarrolladores ejecutan kernels de depuración porque lo que les importa es encontrar errores. Los usuarios ejecutan kernels de versión porque lo que les importa es el rendimiento. Un driver que es correcto bajo `WITNESS` e `INVARIANTS` es correcto, sin más; esas opciones no cambian el comportamiento del driver, solo lo verifican. Un driver que supera un laboratorio de `WITNESS` ejecutado bajo un kernel de depuración también lo superará bajo un kernel de versión. Este es el invariante que hace que el patrón funcione: se desarrolla contra un kernel estricto y se despliega contra uno permisivo, y las comprobaciones adicionales del kernel estricto son las que mantienen correcto al permisivo.

Para el lector, el consejo práctico es el siguiente: ejecuta siempre un kernel de depuración en la VM que uses para el trabajo con drivers. No cambies a un kernel de versión para el desarrollo diario. La retroalimentación sobre los errores del driver vale la pena cien veces más que el coste en rendimiento. Cuando estés listo para probar el driver en un kernel de estilo producción (para mediciones de tiempo o pruebas de integración), arranca un kernel de versión para esa prueba específica, confirma el comportamiento correcto y vuelve al kernel de depuración.

### Un recorrido por los comandos de `ddb`

DDB es el depurador integrado en el kernel. Cuando el kernel entra en pánico en un sistema donde `DDB` está compilado, en lugar de reiniciar de inmediato, el sistema cae en un prompt de depurador en la consola:

```text
KDB: enter: panic
[ thread pid 42 tid 100049 ]
Stopped at:  kdb_enter+0x3b:  movq    $0,kdb_why
db>
```

En ese prompt, puedes inspeccionar el estado del kernel. Los comandos son breves y directos. Un recorrido completo le corresponde a una referencia separada; lo que sigue es la lista que un autor de drivers usa con más frecuencia.

**Comandos de inspección:**

- `bt` o `backtrace`: muestra el backtrace del thread actual. Este es el primer comando que debes ejecutar en cada pánico.
- `ps`: muestra la tabla de procesos. Útil para ver qué estaba ejecutándose.
- `show thread <tid>`: muestra los detalles de un thread específico.
- `show proc <pid>`: muestra los detalles de un proceso específico.
- `show pcpu`: muestra el estado por CPU, incluido el thread actual en cada CPU.
- `show allpcpu`: muestra todo el estado por CPU de una vez.
- `show lockchain <addr>`: muestra la cadena de threads bloqueados en un lock específico.
- `show alllocks`: muestra todos los locks que se mantienen en ese momento, en todos los threads. Útil para diagnosticar deadlocks aparentes.
- `show mbufs`: muestra estadísticas de mbuf, si el stack de red está implicado.
- `show malloc`: muestra las estadísticas de malloc del kernel, agrupadas por tipo. Útil para encontrar un driver que haya perdido memoria antes del pánico.
- `show registers`: muestra los registros de la CPU actual.

**Comandos de navegación:**

- `x/i <addr>`: desensambla en una dirección. Útil para examinar la instrucción donde se produjo un fallo de página.
- `x/xu <addr>`: vuelca la memoria como bytes hexadecimales.
- `x/sz <addr>`: vuelca la memoria como una cadena terminada en nulo.

**Comandos de control:**

- `continue`: reanuda la ejecución. Si el kernel estaba lo suficientemente estable como para entrar en `ddb`, a veces esto permite que la máquina continúe con normalidad. Con frecuencia la opción más segura es `panic`.
- `reset`: reinicia la máquina de inmediato.
- `panic`: fuerza un pánico, lo que activa un volcado de memoria si `dumpdev` está configurado.
- `call <func>`: llama a una función del kernel por nombre. Avanzado; raramente útil en la depuración de drivers.

**Comandos de exploración de threads:**

- `show all procs`: lista todos los procesos.
- `show sleepq`: muestra las colas de espera de procesos dormidos.
- `show turnstile`: muestra el estado del turnstile (usado para locks de bloqueo).
- `show sema`: muestra los semáforos.

Un autor de drivers no necesita memorizar esta lista. El patrón es el siguiente: cuando entras en `ddb`, ejecuta primero `bt`, luego `ps`, después `show alllocks`, después `show mbufs` y finalmente `show malloc`. Esos cinco comandos capturan el 80% de lo que la mayoría de los panics de driver necesitan.

Un hábito útil es imprimir los comandos y tenerlos junto al teclado durante las primeras sesiones con `ddb`. La primera sesión resulta incómoda porque todavía no existe la memoria muscular necesaria. En la tercera sesión, los comandos ya salen solos.

### Una breve introducción a `kgdb`

`ddb` es el depurador en vivo. `kgdb` es el depurador post-mortem. Cuando el kernel entra en pánico con `dumpdev` configurado, la rutina de pánico escribe la imagen completa de la memoria del kernel en el dispositivo de volcado. En el siguiente boot, `savecore(8)` copia el volcado desde el dispositivo de volcado a `/var/crash`. El lector puede entonces abrirlo con `kgdb`:

```sh
sudo kgdb /boot/kernel/kernel.debug /var/crash/vmcore.0
```

(Nota: `kernel.debug` es la versión del kernel con símbolos de depuración, generada cuando el kernel se compiló con `makeoptions DEBUG=-g`. Sin `DEBUG=-g`, `kgdb` puede abrir el volcado igualmente, pero produce una salida menos útil.)

Dentro de `kgdb`, el lector dispone de algo muy parecido a la interfaz habitual de `gdb`. Los comandos son los mismos: `bt`, `frame N`, `info locals`, `print variable`, `list`, y así sucesivamente. La diferencia es que la máquina no está en ejecución; el kernel está detenido para siempre en el momento del pánico, y toda inspección se realiza contra ese estado congelado. Puedes recorrer la pila hacia arriba, examinar estructuras de datos, seguir punteros y razonar sobre lo que llevó al pánico. No puedes avanzar.

Una sesión típica de `kgdb` sobre un pánico tiene este aspecto:

```text
(kgdb) bt
#0  doadump (...)
#1  kern_reboot (...)
#2  vpanic (...)
#3  panic (...)
#4  witness_checkorder (...)
...

(kgdb) frame 10
#10 myfirst_pci_detach (dev=0xfffff80001a23000) at myfirst_pci.c:789
789            mtx_destroy(&sc->lock);

(kgdb) list
784         mtx_lock(&sc->lock);
785         sc->detaching = true;
786         /* wait for all users */
787         while (sc->refs > 0)
788             cv_wait(&sc->cv, &sc->lock);
789            mtx_destroy(&sc->lock);
790         return (0);
791     }

(kgdb) print sc->refs
$1 = 0

(kgdb) print sc->lock
$2 = {
   ...
}
```

El lector recorre la pila para encontrar el frame del driver, examina lo que el driver estaba haciendo e identifica el fallo. En este ejemplo, el driver estaba destruyendo un mutex que seguía bloqueado, porque la línea 789 llama a `mtx_destroy` sin haberlo desbloqueado antes. El fallo es inmediatamente visible.

El capítulo 23 no hace un uso intensivo de `kgdb`, porque los laboratorios están diseñados para ser diagnosticables solo con `ddb` y `dmesg`. La sección 7 menciona `kgdb` en el recorrido de fallos comunes para una clase de errores en los que la inspección post-mortem es la herramienta adecuada. El lector que quiera profundizar debe leer la página de manual `kgdb(1)` y experimentar con un pánico conocido.

### Ejercicio: construir un kernel de depuración y confirmar que los símbolos están presentes con `kgdb`

Un ejercicio breve que asienta los conceptos de la sección.

1. Si aún no lo has hecho, construye e instala un kernel `MYDEBUG` tal como se describe más arriba, con `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN` y `makeoptions DEBUG=-g`. Reinicia.

2. Confirma que el kernel en ejecución es el build de depuración:

   ```sh
   uname -v | grep MYDEBUG
   sysctl debug.witness.watch
   ```

3. Configura un dispositivo de volcado. Normalmente es una partición de swap:

   ```sh
   sudo dumpon /dev/adaNpY
   ```

   donde `adaNpY` es tu partición de swap. En una VM típica, esta es `ada0p3`.

4. Provoca un pánico deliberado para generar un volcado de memoria. La forma más segura es ejecutar `sysctl debug.kdb.panic=1` como root:

   ```sh
   sudo sysctl debug.kdb.panic=1
   ```

   El sistema entra en `ddb` (porque `KDB_UNATTENDED` no estaba configurado para este ejercicio; si lo estuviera, el sistema escribiría el volcado y reiniciaría). Ejecuta `bt`, `ps` y `show alllocks`, y luego escribe `panic` para que el volcado continúe. El sistema escribe en el dispositivo de volcado y reinicia.

5. Tras el reinicio, verifica que `savecore(8)` ha copiado el volcado:

   ```sh
   ls /var/crash
   ```

   Deberías ver `vmcore.0`, `info.0` y `kernel.0`.

6. Abre el volcado con `kgdb`:

   ```sh
   sudo kgdb /var/crash/kernel.0 /var/crash/vmcore.0
   ```

7. Dentro de `kgdb`, ejecuta `bt`. Deberías ver la pila del pánico. Sube unos cuantos frames con `frame N`. Confirma que las líneas de código fuente y los argumentos de las funciones son visibles; eso es lo que aportan los símbolos de depuración.

8. Sal de `kgdb` con `quit`.

El ejercicio no consiste en corregir un fallo, sino en demostrar que la infraestructura de depuración funciona. A partir de este punto, cualquier pánico real que provoque el lector es totalmente depurable: el volcado queda preservado, los símbolos están presentes, el depurador abre el dump. Esa es la base. Sin ella, la depuración es en gran medida una cuestión de conjeturas.

### Cerrando la sección 4

La sección 4 convirtió el kernel del lector en un kernel de depuración. Las opciones activan las aserciones, la comprobación del orden de los locks, los hooks de DTrace, los símbolos de depuración y el depurador integrado en el kernel. El coste es un sistema más lento; el beneficio es un sistema que detecta sus propios fallos de forma ruidosa. `ddb` es la herramienta de inspección en vivo; `kgdb` es la herramienta de inspección post-mortem. Ambas funcionan contra el mismo kernel de depuración que el lector tiene en ejecución ahora mismo.

Con el pipeline de registro (sección 3) y el kernel de depuración (sección 4) ya en su lugar, el lector dispone de la infraestructura que todas las herramientas posteriores dan por sentada. La sección 5 presenta DTrace, la herramienta que convierte el kernel de depuración en una plataforma de trazado y medición en vivo.



## Sección 5: uso de DTrace para la inspección del kernel en vivo

DTrace es el bisturí del kit de herramientas de depuración de FreeBSD. Si `printf` es un instrumento contundente y `ddb` es un martillo, DTrace es un bisturí: permite formular preguntas específicas y precisas sobre el comportamiento en vivo del kernel y obtener respuestas sin modificar el código fuente, reconstruir el kernel ni detener el sistema. El autor de un driver que aprende DTrace alcanza un nuevo nivel de productividad: la capacidad de preguntarse «¿con qué frecuencia se dispara la interrupción del driver?» y obtener una respuesta en diez segundos sin cambiar una sola línea de código.

Esta sección presenta DTrace con el nivel de detalle que necesita un autor de drivers. No pretende ser una referencia completa de DTrace; la excelente página de manual `dtrace(1)`, la DTrace Guide disponible en línea y el libro *Illumos DTrace Toolkit* son las referencias más profundas. Lo que la sección 5 ofrece al lector es suficiente DTrace para instrumentar el driver `myfirst`, medir su comportamiento y entender qué puede y qué no puede hacer DTrace.

### ¿Qué es DTrace?

DTrace es un **framework de trazado dinámico**. La palabra «dinámico» es importante: no requiere que el kernel haya sido construido con conocimiento de cada punto de trazado; no requiere que el código en ejecución sea parcheado; no requiere que el lector reconstruya el kernel ni reinicie. Cargas el módulo de DTrace, solicitas una probe, y el framework adjunta la instrumentación al código del kernel en ejecución. Cuando el código instrumentado se ejecuta, la instrumentación se activa y DTrace registra el evento. Cuando terminas, se desconecta y la instrumentación se elimina.

El framework nació en Solaris y fue portado a FreeBSD. Reside en `/usr/src/cddl/` (la parte del árbol de código fuente bajo licencia CDDL), ya que hereda la licencia de Solaris. La herramienta en espacio de usuario es `dtrace(1)`; la infraestructura del lado del kernel es un conjunto de módulos que pueden cargarse y descargarse bajo demanda.

DTrace tiene tres conceptos organizativos: **providers**, **probes** y **actions**.

Un **provider** es una fuente de puntos de trazado. Algunos de los providers que FreeBSD incluye son: `fbt` (trazado de límites de función, una probe por cada entrada y salida de cada función del kernel), `syscall` (una probe por cada entrada y retorno de llamada al sistema), `sched` (eventos del planificador), `io` (eventos de I/O de bloques), `vfs` (eventos del sistema de archivos), `proc` (eventos de procesos), `vm` (eventos de memoria virtual), `sdt` (trazado estático, para probes compiladas directamente en el kernel) y `lockstat` (operaciones de lock). Algunos providers (`fbt`, `syscall`) están siempre disponibles una vez cargado el módulo; otros (`sdt`) solo existen si el código del kernel define las probes de forma explícita.

Una **probe** es un punto específico que el lector puede trazar. Las probes tienen nombres de cuatro partes con la forma `provider:module:function:name`. Por ejemplo, `fbt:kernel:myfirst_pci_attach:entry` es la probe de entrada del provider `fbt` sobre la función `myfirst_pci_attach` en el módulo del kernel `kernel` (el binario principal del kernel, no un módulo cargable). El comodín `*` coincide con cualquier segmento: `fbt::myfirst_*:entry` coincide con la entrada de cualquier función cuyo nombre comienza por `myfirst_`, en cualquier módulo.

Una **action** es lo que DTrace hace cuando se activa una probe. La acción más sencilla es `{ trace(...) }`, que registra el argumento. Hay acciones más interesantes que agregan datos: `@counts[probefunc] = count()` cuenta cuántas veces se ha activado la probe de cada función. El lenguaje D (el lenguaje de scripting de DTrace) es parecido al C, pero más seguro: no tiene bucles, ni llamadas a funciones, ni asignación de memoria, y se ejecuta en el kernel bajo estrictas restricciones de seguridad.

La potencia de DTrace proviene de la combinación: habilitas una probe concreta, adjuntas una action concreta, observas solo los eventos que te interesan, y lo haces sin tocar el código fuente.

### Activar el soporte de DTrace

Las opciones del kernel necesarias para DTrace se trataron en la sección 4: `options KDTRACE_HOOKS`, `options KDTRACE_FRAME`, `options DDB_CTF` y `makeoptions WITH_CTF=1`. Si el lector construyó el kernel `MYDEBUG` con esas opciones, DTrace está listo para usarse.

En el lado del espacio de usuario hay que cargar el módulo de DTrace. El módulo paraguas es `dtraceall`, que incluye todos los providers:

```sh
sudo kldload dtraceall
```

También es posible cargar providers individuales:

```sh
sudo kldload dtrace          # core framework
sudo kldload dtraceall       # all providers (recommended for development)
sudo kldload fbt             # just function boundary tracing
sudo kldload systrace        # just syscall tracing
```

Para el desarrollo, `dtraceall` es la opción más sencilla: todos los providers que el lector pudiera necesitar están disponibles.

Para confirmar que DTrace funciona:

```sh
sudo dtrace -l | head -20
```

Esto lista las primeras veinte probes que DTrace conoce. En un kernel FreeBSD 14 estándar con `dtraceall` cargado, la salida incluye cientos de miles de probes, la mayoría del provider `fbt` (cada función del kernel tiene una probe de entrada y otra de retorno). Un conteo rápido:

```sh
sudo dtrace -l | wc -l
```

Un número típico es de entre 40.000 y 80.000 probes, dependiendo de los módulos cargados.

Para listar solo las probes de las funciones del driver `myfirst`:

```sh
sudo dtrace -l -n 'fbt::myfirst_*:'
```

Si el driver está cargado, esto produce una lista de las probes de entrada y retorno de cada función `myfirst_*`. Si el driver no está cargado, la lista estará vacía: las probes de `fbt` solo existen para el código que el kernel tiene cargado.

### Escribir scripts sencillos de DTrace

La invocación más sencilla de DTrace traza una única probe e imprime cuando se activa:

```sh
sudo dtrace -n 'fbt::myfirst_pci_attach:entry'
```

La sintaxis es `provider:module:function:name`. El doble dos puntos en `fbt::myfirst_pci_attach:entry` utiliza el módulo vacío por defecto, que coincide con cualquier módulo. DTrace imprime una línea por cada activación de la probe:

```text
CPU     ID                    FUNCTION:NAME
  2  28791         myfirst_pci_attach:entry
```

Esto confirma que la función fue llamada, en la CPU 2, con el identificador interno de probe 28791. Para una pregunta puntual de «¿se ejecutó esta función?», ya es suficiente.

Un script algo más útil traza la entrada y el retorno:

```sh
sudo dtrace -n 'fbt::myfirst_pci_attach:entry,fbt::myfirst_pci_attach:return'
```

Cada línea muestra cuándo se activaron la entrada y el retorno. Las dos juntas indican al lector que la función se ejecutó y retornó sin producir un pánico.

Para trazar muchas funciones a la vez, usa un comodín:

```sh
sudo dtrace -n 'fbt::myfirst_*:entry'
```

La entrada de cada función `myfirst_*` produce una línea. Para el lector que está depurando un camino de attach, es una forma económica de ver la secuencia de llamadas en tiempo real.

Para capturar argumentos, añade una action en lenguaje D:

```sh
sudo dtrace -n 'fbt::myfirst_pci_suspend:entry { printf("%d: suspending %s", timestamp, stringof(args[0]->name)); }'
```

`args[0]` hace referencia al primer argumento de la función sondada. Para las probes FBT, `args[0]` es el primer argumento de la función. Para `myfirst_pci_suspend`, el primer argumento es un `device_t`, y `args[0]->name` (si la disposición de `device_t` en FreeBSD tiene un campo `name` accesible para DTrace) muestra el nombre del dispositivo.

Nota: `device_t` es en realidad un puntero a una estructura opaca, y DTrace solo puede seguir cadenas de punteros con conciencia de tipos si los datos CTF están cargados. Por eso importan `options DDB_CTF` y `makeoptions WITH_CTF=1`: sin ellos, DTrace ve `args[0]` como un entero y no puede desreferenciarlo de forma significativa. Con ellos, DTrace sabe que `args[0]` es un `device_t` (que es un `struct _device *`) y puede recorrer la estructura.

Un script más interesante cuenta cuántas veces se activa cada función `myfirst_*`:

```sh
sudo dtrace -n 'fbt::myfirst_*:entry { @counts[probefunc] = count(); }'
```

Deja que se ejecute durante un minuto mientras el driver está en uso. Pulsa Ctrl-C. DTrace imprime un histograma:

```text
  myfirst_pci_attach                                                1
  myfirst_pci_detach                                                1
  myfirst_pci_suspend                                              42
  myfirst_pci_resume                                               42
  myfirst_intr_filter                                           10342
  myfirst_rx_task                                                5120
  myfirst_dma_submit                                             1024
```

El lector ve de un vistazo la distribución de llamadas del driver: dos attach/detach, cuarenta y dos ciclos de suspend/resume, diez mil filtros de interrupción, cinco mil tareas de recepción, mil envíos DMA. Cualquier número inesperado es una señal para investigar. Si el lector esperaba una relación uno a uno entre interrupciones y tareas, y observa dos a uno, hay un fallo.

### Medir la latencia de las funciones

Uno de los patrones más útiles de DTrace es medir cuánto tarda una función. La idea consiste en registrar una marca de tiempo en la entrada, otra en el retorno, y restar:

```sh
sudo dtrace -n '
fbt::myfirst_pci_suspend:entry { self->start = timestamp; }
fbt::myfirst_pci_suspend:return /self->start/ {
    @times = quantize(timestamp - self->start);
    self->start = 0;
}'
```

La primera cláusula almacena la marca de tiempo de entrada en la variable thread-local `self->start`. La segunda cláusula, que se activa cuando `self->start` es distinto de cero, calcula la diferencia, la añade a una agregación `quantize` (un histograma logarítmico) y borra la variable. Deja que se ejecute mientras el lector provoca ciclos de suspend, pulsa Ctrl-C, y DTrace imprime el histograma:

```text
           value  ------------- Distribution ------------- count
            1024 |                                         0
            2048 |@@@@                                     4
            4096 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@             28
            8192 |@@@@@@@@                                 8
           16384 |@@                                       2
           32768 |                                         0
```

Los valores están en nanosegundos. Esto indica que la mayoría de las llamadas a suspend tardaron entre 4.096 y 8.192 nanosegundos (de 4 a 8 microsegundos), con una cola que llega hasta 16.384 nanosegundos (16 microsegundos). Para una llamada a suspend que debería ser rápida, este tipo de medición le dice al lector si la disciplina del driver está funcionando correctamente.

Para obtener un tipo diferente de información, utiliza las agregaciones `avg()` o `max()`:

```sh
sudo dtrace -n '
fbt::myfirst_pci_suspend:entry { self->start = timestamp; }
fbt::myfirst_pci_suspend:return /self->start/ {
    @avg = avg(timestamp - self->start);
    @max = max(timestamp - self->start);
    self->start = 0;
}'
```

Esto imprime el tiempo medio y el tiempo máximo de suspend al finalizar. Muy útil para informes de resumen.

### Uso del proveedor `syscall`

El proveedor `syscall` muestra cada llamada al sistema. Es especialmente útil para el trabajo con drivers, porque las llamadas del espacio de usuario al driver (`open`, `read`, `write`, `ioctl`) atraviesan primero la capa de syscalls. Un script que observa las llamadas al sistema sobre un descriptor de archivo concreto es una forma ligera de ver cómo los programas de usuario utilizan el driver:

```sh
sudo dtrace -n 'syscall::ioctl:entry /execname == "myftest"/ { printf("%s ioctl on fd %d", execname, arg0); }'
```

Esto imprime cada llamada al sistema `ioctl` realizada por un proceso llamado `myftest`. La variable `execname` es una variable interna de DTrace que contiene el nombre del ejecutable del proceso actual. Ejecutar el programa de prueba `myftest` (un pequeño programa de usuario que ejercita el driver) mientras este script está activo ofrece una vista en tiempo real de cada ioctl que el programa emite.

### Uso del proveedor `sched`

El proveedor `sched` registra eventos del planificador: cambios de thread, despertares y encolados. Es útil para comprender la concurrencia en el kernel, incluido el manejo de interrupciones en los drivers:

```sh
sudo dtrace -n 'sched:::on-cpu /execname == "myftest"/ { @[cpu] = count(); }'
```

Esto cuenta, por CPU, cuántas veces el programa `myftest` fue planificado en cada CPU durante la traza. Es útil para entender si el planificador mantiene el programa en un solo núcleo o lo rebota entre varios, lo cual afecta al comportamiento de caché del driver.

### Uso del proveedor `io`

El proveedor `io` muestra eventos de I/O de bloque. No es directamente relevante para `myfirst` (un driver PCI genérico), pero conviene conocerlo. Un script para mostrar cada operación de I/O a disco:

```sh
sudo dtrace -n 'io:::start { printf("%s %d", execname, args[0]->bio_bcount); }'
```

Para los drivers de almacenamiento, `io` es esencial. Para `myfirst` es periférico. El capítulo 23 no lo explora en profundidad.

### Tracing definido estáticamente (SDT)

El proveedor `fbt` instrumenta la entrada y el retorno de cada función. Eso es flexible pero algo tosco: obtienes sondas de entrada y retorno para cada función, y a menudo lo que se quiere son sondas en puntos concretos e interesantes dentro de una función.

SDT (Statically Defined Tracing) resuelve esto. SDT permite al autor del driver añadir sondas con nombre en lugares específicos del código fuente, y DTrace las hace visibles. Cuando DTrace no está observando, las sondas son esencialmente gratuitas: se compilan como una única instrucción no-op. Cuando DTrace se conecta, el no-op es reemplazado por una trampa que dispara la sonda.

La maquinaria de SDT vive en `/usr/src/sys/sys/sdt.h`. Las macros básicas son:

- `SDT_PROVIDER_DEFINE(name)`: declara un proveedor. Normalmente se hace una sola vez por driver, en el archivo fuente principal.
- `SDT_PROBE_DEFINEN(provider, module, function, name, "arg1-type", "arg2-type", ...)`: declara una sonda con N argumentos tipados.
- `SDT_PROBEN(provider, module, function, name, arg1, arg2, ...)`: dispara la sonda con los argumentos indicados.

Un ejemplo mínimo para el driver `myfirst`. En `myfirst_pci.c`, cerca del inicio:

```c
#include <sys/sdt.h>

SDT_PROVIDER_DEFINE(myfirst);
SDT_PROBE_DEFINE2(myfirst, , attach, entry,
    "struct myfirst_softc *", "device_t");
SDT_PROBE_DEFINE2(myfirst, , attach, return,
    "struct myfirst_softc *", "int");
SDT_PROBE_DEFINE3(myfirst, , dma, submit,
    "struct myfirst_softc *", "bus_addr_t", "size_t");
SDT_PROBE_DEFINE2(myfirst, , dma, complete,
    "struct myfirst_softc *", "int");
```

Luego en el código del driver:

```c
int
myfirst_pci_attach(device_t dev)
{
    struct myfirst_softc *sc = device_get_softc(dev);
    int error;

    SDT_PROBE2(myfirst, , attach, entry, sc, dev);
    ...
    error = ...;  /* real attach code */
    ...
    SDT_PROBE2(myfirst, , attach, return, sc, error);
    return (error);
}
```

Desde el espacio de usuario, el lector puede observar ahora las sondas de `myfirst` a través de DTrace:

```sh
sudo dtrace -n 'myfirst:::' | head
```

Esto muestra cada sonda de myfirst. Para observar solo los eventos de DMA:

```sh
sudo dtrace -n 'myfirst:::dma-submit'
sudo dtrace -n 'myfirst:::dma-complete'
```

Para contar entregas por tamaño de DMA:

```sh
sudo dtrace -n 'myfirst:::dma-submit { @[args[2]] = count(); }'
```

Esto imprime un histograma de los tamaños de transferencia DMA al finalizar la traza. En una sesión de depuración de un driver, ese es el tipo de dato que convierte la especulación en evidencia.

La Fase 2 del capítulo 23 añade exactamente estas sondas al driver `myfirst`. El ejercicio al final de la Sección 5 explica paso a paso cómo hacerlo. Los archivos correspondientes se encuentran en `examples/part-05/ch23-debug/stage2-sdt/`.

Los drivers reales que usan SDT merecen estudio. `/usr/src/sys/dev/virtio/virtqueue.c` define:

```c
SDT_PROVIDER_DEFINE(virtqueue);
SDT_PROBE_DEFINE6(virtqueue, , enqueue_segments, entry, "struct virtqueue *",
    ...);
SDT_PROBE_DEFINE1(virtqueue, , enqueue_segments, return, "uint16_t");
```

y las dispara en `virtqueue_enqueue`:

```c
SDT_PROBE6(virtqueue, , enqueue_segments, entry, vq, desc, head_idx, ...);
...
SDT_PROBE1(virtqueue, , enqueue_segments, return, idx);
```

El lector puede ejecutar `sudo dtrace -l -n 'virtqueue:::' ` en un sistema con dispositivos respaldados por virtio (algo habitual en máquinas virtuales) y ver estas sondas de inmediato. Trazarlas ofrece una vista en tiempo real de la actividad de la virtqueue que sería imposible reproducir con `printf`.

### Proveedores útiles para el trabajo con drivers

Un resumen de los proveedores que un autor de drivers usa con más frecuencia:

- **`fbt`**: tracing en los límites de función. Cada función del kernel dispone de sondas de entrada y retorno. Ideal para "¿se ejecutó esta función?" y "¿qué argumentos recibió?".
- **`sdt`**: tracing definido estáticamente. Sondas que el driver añade explícitamente. Ideal para observar eventos específicos del driver en puntos concretos.
- **`syscall`**: tracing de llamadas al sistema. Ideal para ver qué pide el programa de usuario al kernel.
- **`sched`**: eventos del planificador. Ideal para comprender la concurrencia y el uso de CPU.
- **`io`**: eventos de I/O de bloque. Ideal para trabajo con almacenamiento.
- **`vfs`**: eventos del sistema de archivos. Ideal para trabajo con drivers de sistemas de archivos.
- **`proc`**: eventos del ciclo de vida de procesos. Ideal para creación, salida y entrega de señales.
- **`lockstat`**: operaciones de lock. Ideal para analizar la contención de locks; costoso cuando está activo.
- **`priv`**: eventos relacionados con privilegios. Útil para auditoría de seguridad.

La mayor parte del trabajo con drivers utiliza `fbt`, `sdt` y `syscall` de forma intensiva, y toca los demás de manera ocasional.

### Errores comunes en DTrace

Algunos patrones que confunden a los nuevos usuarios de DTrace.

**Buffer demasiado pequeño.** DTrace dispone de buffers por CPU; si un script dispara sondas más rápido de lo que DTrace puede vaciarlos, los eventos se descartan. El lector verá un mensaje como `dtrace: X drops on CPU N`. La solución habitual es afinar el predicado (disparar con menos frecuencia) o aumentar el tamaño del buffer con `-b 16m`.

**Olvidar limpiar las variables locales al thread.** En el patrón de medición de latencia, el script asigna `self->start` en la entrada y comprueba `self->start != 0` en el retorno. Si el lector olvida limpiarlo (`self->start = 0;` tras usarlo), la variable acumula estado y las trazas posteriores se confunden. Siempre hay que emparejar la asignación con la limpieza.

**Usar `trace()` cuando `printf()` sería más claro.** `trace(x)` imprime el valor en un formato escueto difícil de leer. `printf("x=%d", x)` suele ser mejor opción.

**Ejecutar como usuario sin privilegios.** DTrace requiere root. Ejecutar `dtrace` como usuario normal falla con un error de permisos. Usa siempre `sudo dtrace`.

**Sondas que no existen.** Si el nombre de una sonda está mal escrito o el módulo no está cargado, `dtrace` imprime un error. La solución es confirmar que la sonda existe con `dtrace -l` antes de intentar usarla.

**La trampa de `stringof()`.** Al imprimir un puntero a cadena del kernel, el lector suele necesitar `stringof(ptr)` y no `ptr` directamente, porque DTrace no sigue automáticamente los punteros del kernel. Olvidar esto produce una salida ilegible.

### Ejercicio: usar DTrace para medir cuánto tarda una operación de lectura del driver

Un ejercicio práctico complementario para la Sección 5.

1. Asegúrate de que el kernel `MYDEBUG` está en ejecución y de que `dtraceall` está cargado.

2. Escribe un programa de usuario que lea de `/dev/myfirst0` cien veces. El programa puede ser tan sencillo como:

   ```c
   #include <fcntl.h>
   #include <unistd.h>
   int main(void) {
       int fd = open("/dev/myfirst0", O_RDWR);
       char buf[4096];
       for (int i = 0; i < 100; i++)
           read(fd, buf, sizeof(buf));
       close(fd);
       return 0;
   }
   ```

   Compílalo con `cc -o myftest myftest.c`.

3. En una terminal, ejecuta el script de latencia de DTrace sobre la ruta de `read` del driver. Suponiendo que la función de lectura del driver es `myfirst_read`:

   ```sh
   sudo dtrace -n '
   fbt::myfirst_read:entry { self->start = timestamp; }
   fbt::myfirst_read:return /self->start/ {
       @times = quantize(timestamp - self->start);
       self->start = 0;
   }'
   ```

4. En otra terminal, ejecuta el programa de usuario:

   ```sh
   ./myftest
   ```

5. Pulsa Ctrl-C en el script de DTrace. Lee el histograma. La mayoría de las lecturas deberían estar en los microsegundos bajos (unos pocos miles de nanosegundos).

6. Haz una captura de pantalla o guarda la salida. Compárala con tu expectativa. Si una lectura tarda más de lo esperado, ese es el punto de partida de una sesión de depuración.

Opcional: ejecuta el mismo script contra `fbt::myfirst_intr_filter:entry` y observa la distribución de latencia de las interrupciones.

### Cerrando la Sección 5

La Sección 5 presentó DTrace como el bisturí del conjunto de herramientas de depuración. DTrace es dinámico: las sondas se conectan al código en ejecución sin necesidad de recompilar. Es seguro: el lenguaje D no tiene bucles ni asignación de memoria. Es amplio: los proveedores exponen decenas de miles de puntos de observación a lo largo del kernel. Para el trabajo con drivers, la combinación de `fbt` (sondas automáticas en cada función) y `sdt` (sondas personalizadas en puntos específicos) da al lector la capacidad de responder a casi cualquier pregunta sobre "¿qué está haciendo el driver?" sin modificar el código fuente.

El coste de DTrace cuando está inactivo es prácticamente nulo: las sondas existen en el kernel como instrucciones no-op o ranuras de punteros a función. El coste cuando está activo depende de lo que haga el script: un contador simple es barato, un histograma complejo es más costoso, y un script que imprime en cada sonda puede desbordar el buffer. Comprender esta compensación es parte de escribir scripts de DTrace eficaces.

Con el registro de eventos (Sección 2), las tuberías de log (Sección 3), los kernels de depuración (Sección 4) y DTrace (Sección 5) en su lugar, el lector dispone de cuatro vistas complementarias del comportamiento del kernel. La Sección 6 añade la última vista esencial para el trabajo con drivers: `ktrace`, la herramienta que muestra lo que un programa de usuario está haciendo mientras cruza hacia el kernel a través de la interfaz del driver.



## Sección 6: trazando la actividad del kernel con `ktrace` y `kdump`

DTrace traza el kernel desde el lado del kernel: qué funciones se ejecutaron, cuánto tardaron, qué estado tocaron. `ktrace` traza la misma actividad desde el lado del usuario: qué llamadas al sistema realizó un proceso, qué argumentos pasó, qué valores de retorno obtuvo, qué señales recibió. Juntas, las dos herramientas ofrecen una vista completa. DTrace muestra lo que hizo el kernel; `ktrace` muestra lo que pidió el programa.

Para el trabajo con drivers, `ktrace` es la herramienta de elección cuando la pregunta es "¿qué está haciendo el programa de usuario?". Un driver que recibe argumentos extraños puede rastrearse: quizás el programa está llamando a `ioctl` con el número de comando incorrecto, o a `write` con un buffer de longitud cero, o a `open` con los flags incorrectos. `ktrace` responde a esas preguntas registrando cada syscall que emite el programa.

### ¿Qué es `ktrace`?

`ktrace(1)` es una herramienta del espacio de usuario que indica al kernel que registre una traza de las actividades de uno o más procesos en un archivo. Los eventos registrados incluyen:

- Cada entrada y retorno de llamada al sistema, con argumentos y valor de retorno.
- Cada señal entregada al proceso.
- Cada evento `namei` (resolución de nombre de ruta).
- Cada buffer de I/O pasado a read o write.
- Cambios de contexto, a un nivel general.

La traza se escribe en un archivo binario (por defecto `ktrace.out`). Para leerlo, el lector ejecuta `kdump(1)`, que traduce la traza binaria a texto legible por humanos.

`ktrace` es una herramienta sencilla, mucho más antigua que DTrace, y eso se nota en algunas interfaces. Pero para la pregunta central de "¿qué le pidió este proceso al kernel?", es ideal.

La implementación se encuentra en `/usr/src/sys/kern/kern_ktrace.c` y la cabecera orientada al usuario en `/usr/src/sys/sys/ktrace.h`. Las herramientas del espacio de usuario están en `/usr/src/usr.bin/ktrace/` y `/usr/src/usr.bin/kdump/`.

El kernel necesita `options KTRACE` para soportar `ktrace`. Esta opción está habilitada en `GENERIC` por defecto en todas las arquitecturas; el lector no necesita recompilar el kernel para usar `ktrace`.

### Iniciar una traza

La invocación más sencilla se conecta a un proceso en ejecución por PID:

```sh
sudo ktrace -p 12345
```

Esto indica al kernel que comience a registrar el proceso con PID 12345. El kernel escribe los eventos en `ktrace.out` en el directorio actual. Cuando el lector haya terminado, detiene la traza:

```sh
sudo ktrace -C
```

O, de forma más precisa, detiene la traza para un PID específico:

```sh
sudo ktrace -c -p 12345
```

Para ejecutar un comando desde cero con la traza habilitada:

```sh
sudo ktrace /path/to/program arg1 arg2
```

Esto arranca el programa y lo traza desde la primera syscall. Cuando el programa termina, el archivo de traza permanece para que el lector lo examine.

Opciones útiles:

- `-d`: traza también a los procesos descendientes. Si el programa hace fork, los hijos también son trazados.
- `-i`: hereda la traza a través de `exec`. Si el programa ejecuta otro programa con `exec`, la traza continúa. Sin `-i`, la traza se detiene en el exec.
- `-t <mask>`: selecciona qué tipos de eventos registrar. La máscara es un conjunto de códigos de letras, como `cnios` (calls, namei, I/O, signals). Por defecto se incluyen la mayoría de tipos.
- `-f <file>`: escribe en un archivo específico en lugar de `ktrace.out`.
- `-C`: elimina toda traza activa (detiene todas las trazas).

Una combinación habitual:

```sh
sudo ktrace -di -f mytrace.out -p 12345
```

Traza el proceso, todos sus descendientes y a través de exec, en `mytrace.out`.

### Leer la salida con `kdump`

El archivo de traza binaria no es útil por sí solo. `kdump` lo traduce:

```sh
kdump -f mytrace.out
```

La salida tiene este aspecto:

```text
 12345 myftest  CALL  open(0x8004120a0,0x0)
 12345 myftest  NAMI  "/dev/myfirst0"
 12345 myftest  RET   open 3
 12345 myftest  CALL  ioctl(0x3,0x20006601,0x7fffffffe8c0)
 12345 myftest  RET   ioctl 0
 12345 myftest  CALL  read(0x3,0x7fffffffe900,0x1000)
 12345 myftest  GIO   fd 3 read 4096 bytes
                 "... data bytes ..."
 12345 myftest  RET   read 4096/0x1000
```

Cada línea contiene un PID, el nombre del proceso, un tipo de evento y datos específicos del evento:

- Las líneas `CALL` muestran la entrada a una syscall, con los argumentos en hexadecimal.
- Las líneas `NAMI` muestran una resolución de nombre de ruta, con la cadena resuelta.
- Las líneas `RET` muestran el retorno de una syscall, con el valor devuelto.
- Las líneas `GIO` muestran buffers de I/O genéricos, seguidos de un volcado del contenido del buffer.
- Las líneas `PSIG` muestran la entrega de una señal.

La salida es densa, pero se vuelve legible con la práctica. Un patrón habitual en el trabajo con drivers:

1. Ejecuta el programa de usuario bajo `ktrace`.
2. Busca la apertura de `/dev/myfirst0`. Anota el descriptor de archivo que devolvió el kernel.
3. Filtra las líneas siguientes para quedarte con las operaciones sobre ese fd: `ioctl`, `read`, `write`, `close`.
4. Observa los argumentos exactos, especialmente en `ioctl`, donde el número de comando codifica la operación que se le pidió realizar al driver.

Por ejemplo, la línea `ioctl(0x3,0x20006601,0x7fffffffe8c0)` muestra que el programa emite un ioctl sobre el fd 3 con el comando `0x20006601`. El comando es la codificación de `_IO`/`_IOR`/`_IOW`/`_IOWR`: los 16 bits bajos corresponden al número de comando y los bits altos indican la dirección y el tamaño. Un autor de drivers que depura un bug de ioctl puede examinar ese comando, compararlo con las definiciones del encabezado del driver y confirmar que el programa está enviando exactamente el comando que el driver espera.

### Filtros útiles de `kdump`

`kdump` acepta varios indicadores que filtran la salida:

- `-t <mask>`: filtra por tipo, con la misma máscara que `ktrace -t`.
- `-E`: muestra el tiempo transcurrido desde el evento anterior.
- `-R`: muestra marcas de tiempo relativas en lugar de absolutas.
- `-l`: muestra el PID y el nombre del proceso en cada línea.
- `-d`: vuelca en hexadecimal los buffers de I/O (activo por defecto para las líneas `GIO`).
- `-H`: volcado hexadecimal más legible.

Un refinamiento habitual:

```sh
kdump -f mytrace.out -E -t cnri
```

Tiempo transcurrido, entrada/retorno de syscall más namei más I/O.

Pasar la salida por `grep` también es útil:

```sh
kdump -f mytrace.out | grep -E 'myfirst|ioctl|open|read|write|close' | less
```

Esto muestra solo las líneas relevantes para el driver.

### Encontrar las syscalls relacionadas con el driver

Un programa de usuario se comunica con un driver a través de cuatro syscalls principales: `open`, `ioctl`, `read`, `write` y `close`. Un autor de drivers que traza un programa de usuario debe observar las siguientes:

- **`open("/dev/myfirst0", ...)`**: el programa abre el dispositivo. El valor de retorno es un descriptor de archivo que las syscalls posteriores utilizarán.
- **`ioctl(fd, cmd, arg)`**: el programa emite un comando específico del dispositivo. El argumento `cmd` codifica la operación; `arg` es normalmente un puntero a una estructura.
- **`read(fd, buf, size)`**: el programa lee `size` bytes en `buf`.
- **`write(fd, buf, size)`**: el programa escribe `size` bytes desde `buf`.
- **`close(fd)`**: el programa cierra el descriptor.

Una traza que muestre estas llamadas en orden es la vista del lector del lado de usuario de la interfaz del driver. Un fallo en el que el programa pasa un tamaño incorrecto a `read` aparece inmediatamente como `read(fd, buf, 0)` (tamaño cero) en la traza. Un fallo en el que el programa olvida llamar a `close` aparece como la ausencia de una llamada a `close` antes de salir. Un fallo en el que el programa utiliza un comando `ioctl` indefinido aparece como un valor hexadecimal inesperado en el argumento `cmd`.

### Comparación entre `ktrace` y DTrace

Ambas herramientas se solapan en algunos aspectos y se complementan en otros. Una guía aproximada:

Usa `ktrace` cuando:

- Quieras ver qué está haciendo un programa de usuario, desde la perspectiva del propio programa.
- Quieras un registro que puedas guardar y revisar más adelante.
- Quieras una configuración mínima (sin reconstruir el kernel, sin escribir scripts).
- Quieras capturar los argumentos de las syscalls, incluidas las cadenas apuntadas por punteros y los buffers de I/O.

Usa DTrace cuando:

- Quieras ver qué está ocurriendo dentro del kernel.
- Quieras medir latencia, distribución o contención.
- Quieras agregar datos de múltiples eventos.
- Necesites trazar sin modificar el programa ni ejecutarlo bajo un envoltorio.

Para la mayor parte del trabajo con drivers, lo habitual es usar ambas. `ktrace` responde a "¿qué está pidiendo el programa?". DTrace responde a "¿qué hace el driver al respecto?". La combinación ofrece el panorama completo.

### Una nota sobre `KTR` frente a `ktrace`

Los nombres confunden a los nuevos autores de drivers. `ktrace` (en minúsculas) es la herramienta de trazado en espacio de usuario descrita anteriormente. `KTR` (en mayúsculas) es el framework de trazado de eventos dentro del kernel, habilitado con `options KTR`. Son cosas distintas.

`KTR` es un buffer circular de alto rendimiento para eventos del kernel, pensado para los desarrolladores del kernel que depuran el propio kernel. Los autores de drivers pueden añadir macros `CTR` (category-0, category-1, etc.) a su código y observarlas mediante el comando `show ktr` de `ddb`. En la práctica, `KTR` lo utilizan unos pocos subsistemas del núcleo del kernel (el planificador, el sistema de locking, algunos subsistemas de dispositivos) y raramente los drivers individuales. El capítulo 23 no profundiza más en `KTR`; quien sienta curiosidad puede leer `/usr/src/sys/sys/ktr.h` para ver las definiciones de las macros y `/usr/src/sys/conf/NOTES` para las opciones de configuración.

### Ejercicio: trazar un programa de usuario sencillo que accede a tu driver

Un ejercicio práctico complementario de la sección 6.

1. Escribe un pequeño programa de usuario `myftest.c` que abra `/dev/myfirst0`, emita un ioctl, lea 4 KB, escriba 4 KB y cierre. La versión más sencilla:

   ```c
   #include <sys/ioctl.h>
   #include <fcntl.h>
   #include <unistd.h>
   #include <stdio.h>

   int
   main(void)
   {
       int fd, error;
       char buf[4096];

       fd = open("/dev/myfirst0", O_RDWR);
       if (fd < 0) { perror("open"); return 1; }

       error = ioctl(fd, 0x20006601 /* placeholder */, NULL);
       printf("ioctl: %d\n", error);

       error = read(fd, buf, sizeof(buf));
       printf("read: %d\n", error);

       error = write(fd, buf, sizeof(buf));
       printf("write: %d\n", error);

       close(fd);
       return 0;
   }
   ```

   Compila con: `cc -o myftest myftest.c`.

2. Traza el programa:

   ```sh
   sudo ktrace -di -f mytrace.out ./myftest
   ```

3. Lee la traza:

   ```sh
   kdump -f mytrace.out | less
   ```

4. Encuentra las líneas relacionadas con `/dev/myfirst0`:

   ```sh
   kdump -f mytrace.out | grep -E 'myfirst0|CALL|RET|NAMI' | less
   ```

5. Confirma que cada syscall está presente con los argumentos esperados. Para el ioctl, decodifica el número de comando a mano: `0x20006601` indica dirección IOC_OUT (byte alto 0x20), tamaño 0 (los siguientes 14 bits), tipo 'f' (0x66), comando 01. Un driver que haya definido `MYFIRST_IOC_RESET = _IO('f', 1)` vería exactamente este comando.

6. (Opcional) Ejecuta el programa bajo DTrace en un segundo terminal, contando las funciones del driver:

   ```sh
   sudo dtrace -n 'fbt::myfirst_*:entry { @[probefunc] = count(); }' &
   ./myftest
   ```

   Compara el recuento de DTrace con la secuencia de ktrace. Deberías ver que cada syscall de usuario activó funciones específicas del driver: `open` → `myfirst_open`, `ioctl` → `myfirst_ioctl`, `read` → `myfirst_read`, y así sucesivamente.

El ejercicio ancla las herramientas en una traza específica y concreta. Todo autor de drivers lleva este patrón en su kit para el resto de su carrera: traza del lado de usuario con `ktrace`, traza del lado del kernel con DTrace, correlacionadas por tiempo y PID.

### Cerrando la sección 6

La sección 6 presentó `ktrace` y `kdump`, el par de trazado del lado de usuario que complementa a DTrace. `ktrace` registra las syscalls que realiza un proceso; `kdump` traduce el registro a texto; la combinación muestra la interfaz del driver desde la perspectiva del programa de usuario. Para un autor de drivers, esta vista es esencial para depurar fallos que se originan en el lado de usuario del límite del driver.

Con el logging, el análisis de logs, los kernels de depuración, DTrace y `ktrace` en mano, el lector dispone de las cinco herramientas que resuelven la mayoría de las cuestiones de depuración de drivers. La sección 7 se centra en las clases de fallos que estas herramientas detectan con más frecuencia, con el síntoma característico de cada uno y la herramienta que mejor lo diagnostica.

## Sección 7: Diagnóstico de fallos habituales en drivers

Las seis secciones anteriores construyeron una caja de herramientas: logging, inspección de logs, kernels de depuración, DTrace y `ktrace`. La sección 7 aplica esa caja de herramientas a los fallos que los lectores encontrarán en la práctica. Todo autor experimentado de drivers en FreeBSD se ha topado con cada uno de ellos al menos una vez. El propósito de esta sección es describir los síntomas, explicar la causa subyacente y asociar cada clase de fallo con la herramienta que lo diagnostica de forma más eficiente.

Ninguno de los fallos que se describen a continuación es exótico. Surgen de errores de programación ordinarios, de una comprensión incompleta del entorno del kernel, o de suposiciones que son válidas en una plataforma pero no en otra. El lector que aprenda a reconocer los síntomas se ahorrará horas de frustración, porque el primer paso hacia una solución siempre es una clasificación correcta.

### 7.1 Fugas de memoria: el crecimiento silencioso

Una fuga de memoria en un módulo del kernel es más difícil de detectar que una en un programa de usuario. No existe un equivalente de `valgrind` que funcione de forma transparente, y el kernel no termina cuando el módulo se descarga, así que una fuga continúa consumiendo memoria incluso después de que el driver haya desaparecido. En sistemas de larga ejecución, una fuga pequeña es fatal: el pool del que se asigna crece indefinidamente, otros subsistemas empiezan a fallar y, en último término, la máquina se queda sin memoria del kernel y entra en pánico.

El síntoma característico es sencillo: un pool de `malloc` que crece pero nunca mengua. La herramienta que pone de manifiesto el síntoma es `vmstat -m`:

```sh
vmstat -m | head -1
vmstat -m | grep -E 'Type|myfirst'
```

La salida tiene este aspecto:

```text
         Type InUse MemUse Requests  Size(s)
      myfirst    12     3K       48  256
```

Las tres columnas que importan son `InUse`, `MemUse` y `Requests`. `InUse` es el número de asignaciones de este pool que están pendientes en este momento. `MemUse` es la memoria total que ocupan esas asignaciones, en kilobytes. `Requests` es el número total de asignaciones realizadas desde que se creó el pool, incluidas las que se liberaron posteriormente.

Un pool sano tiene un valor de `InUse` que permanece aproximadamente constante bajo una carga de trabajo estable. Un pool con fuga tiene un valor de `InUse` que crece sin límite.

Para confirmar una fuga, ejecuta la carga de trabajo que ejerce el driver y luego toma dos instantáneas de `vmstat -m` con varios minutos de diferencia:

```sh
vmstat -m | grep myfirst
# run the workload for 5 minutes
vmstat -m | grep myfirst
```

Si la primera muestra `InUse=12` y la segunda `InUse=4800`, y la carga de trabajo no debería haber añadido cuatro mil asignaciones, el driver tiene una fuga.

El segundo paso es identificar la ruta del código que provoca el problema. Cada llamada a `malloc(9)` tiene como contraparte una llamada esperada a `free(9)`. La fuga está en la ruta que asigna sin liberar. Busca en el driver las llamadas a `malloc(..., M_MYFIRST, ...)` y a `free(..., M_MYFIRST)`, y verifica que cada asignación tiene su correspondiente liberación en todas las rutas de salida.

Los patrones de fuga más habituales en el código de los drivers son:

1. **Fugas en rutas de error.** Una asignación tiene éxito, un paso posterior falla, y el retorno por error omite la llamada a `free`. La solución es una única ruta de limpieza con etiquetas `goto fail;`, del tipo explorado en el capítulo 18.

2. **Fugas condicionales.** La memoria se libera solo cuando un indicador está activado, y a veces ese indicador no lo está. La solución es liberar incondicionalmente o llevar un seguimiento más cuidadoso de la propiedad.

3. **Destructor de contexto olvidado.** Se asigna un objeto por descriptor de archivo abierto y se almacena en `si_drv1` o similar, pero el manejador `d_close` no lo libera. La solución es tratar `d_close` (o el callback de destrucción del `cdev`) como la contraparte simétrica de `d_open`.

4. **Fuga de temporizador o cola de tareas.** Se programa una tarea, su rutina de limpieza asigna memoria para el procesamiento diferido, pero la tarea se cancela antes de ejecutarse y el buffer asignado nunca se libera.

Una vez identificada la ruta, añade la llamada a `free` que falta, recarga el módulo, vuelve a ejecutar la carga de trabajo y confirma con `vmstat -m` que `InUse` se estabiliza. Añadir un poco de logging resulta útil con frecuencia: un `device_printf` en la ruta de asignación y otro en la ruta de liberación exponen rápidamente la proporción de asignaciones respecto a liberaciones en `dmesg`. Para un driver que use una macro `DPRINTF`, una clase dedicada `MYF_DBG_MEM` hace que el trazado de memoria sea opcional y configurable en tiempo de ejecución, que es exactamente lo que la sección 8 implementará para el driver `myfirst`.

DTrace también puede ayudar aquí, especialmente en kernels con `KDTRACE_HOOKS`. Una sencilla línea de comandos cuenta las llamadas a `malloc` y `free` del driver y muestra cualquier desequilibrio:

```sh
dtrace -n 'fbt::malloc:entry /execname == "kernel"/ { @["malloc"] = count(); }
           fbt::free:entry   /execname == "kernel"/ { @["free"]   = count(); }'
```

Un enfoque mucho más específico del driver consiste en añadir probes de DTrace al propio driver, uno en cada asignación y otro en cada liberación, y dejar que DTrace cuente la proporción. Esta es la técnica que la sección 8 demuestra para el driver `myfirst`.

### 7.2 Condiciones de carrera: el fallo poco frecuente pero destructivo

Las condiciones de carrera son los fallos de driver más difíciles de reproducir y los más fáciles de pasar por alto. Una condición de carrera ocurre cuando dos threads acceden al mismo estado sin una sincronización correcta, y el comportamiento resultante depende del orden relativo en el tiempo de ambos threads. Con carga ligera, la condición de carrera puede no manifestarse nunca; con carga pesada, puede producirse cientos de veces por segundo.

Los síntomas de las condiciones de carrera son muy variados:
- pánicos esporádicos en lugares imprevisibles
- corrupción de datos (valores que nunca deberían existir)
- fallos de aserción relacionados con locks ocasionales, como "mutex myfirst_lock not owned"
- pilas de llamadas que parecen imposibles, en las que dos threads parecen estar dentro de la misma región protegida por un mutex

Ningún síntoma por sí solo prueba una condición de carrera, pero cualquier patrón de fallos que aparezca solo bajo carga elevada debe hacer sospechar.

La herramienta más eficaz de FreeBSD para encontrar condiciones de carrera es `WITNESS`, presentada en la sección 4. Un kernel construido con `options WITNESS` examina cada operación de lock y entra en pánico ante cualquier violación del orden de locks declarado, ante cualquier intento de adquirir un spinlock mientras se mantiene un sleep lock, ante cualquier llamada a una función que puede dormir mientras se mantiene un spinlock, y ante muchos otros errores relacionados.

Cuando `WITNESS` entra en pánico o imprime una violación, produce una traza de pila que muestra:
- qué lock se estaba adquiriendo
- qué locks ya se mantenían
- el orden declarado que la adquisición violaría
- ambas pilas (el lock actual y el que entra en conflicto)

La solución es habitualmente una de las siguientes:

1. **Reordena las adquisiciones.** Si el código toma el lock A y luego el lock B mientras otro camino toma B y luego A, uno de ellos debe cambiar.
2. **Añade el lock que falta.** Si se accede al estado sin ningún mutex, añade el mutex `MTX_DEF` apropiado alrededor del acceso.
3. **Elimina el lock redundante.** A veces dos locks protegen estado superpuesto y uno puede eliminarse.
4. **Cambia el tipo de lock.** Los spin locks y los sleep locks tienen reglas distintas. Una región que necesite dormir debe usar un sleep lock. Una región llamada desde contexto de interrupción debe usar a menudo un spin lock.

Un driver que no registra ninguna violación de `WITNESS` no está libre de condiciones de carrera, porque `WITNESS` solo detecta problemas de ordenación de locks. Las condiciones de carrera sobre estado al que se accede sin ningún lock requieren un enfoque diferente: lectura cuidadosa del código, sondas de DTrace en los puntos sospechosos para confirmar el timing, y ocasionalmente aserciones `mtx_assert(&sc->sc_mtx, MA_OWNED)` dispersas por las secciones críticas. `INVARIANTS` habilita estas aserciones, lo que es una razón más para ejecutar un kernel de depuración durante el desarrollo.

Entre `WITNESS`, `INVARIANTS`, `mtx_assert` y los contadores de DTrace en los caminos donde se sospecha contención, la mayoría de las condiciones de carrera pueden acotarse en una o dos horas. Las que sobreviven a este arsenal son raras y casi siempre implican estructuras de datos lock-free, operaciones atómicas o suposiciones de ordenación de memoria que requieren una revisión cuidadosa con un ingeniero sénior.

### 7.3 Uso incorrecto de bus_space y bus_dma

Un autor de drivers nuevo suele encontrarse con bugs de acceso al bus que comparten una familia característica de síntomas:

- las lecturas devuelven 0xFF, 0xFFFFFFFF u otras constantes sospechosas
- las escrituras parecen tener éxito pero el dispositivo no reacciona
- la máquina funciona durante segundos o minutos y luego se cuelga o entra en pánico
- el comportamiento es correcto en una arquitectura (amd64) y erróneo en otra (arm64, riscv64)

Cada uno de ellos apunta a un uso incorrecto de `bus_space` o `bus_dma`.

El primer error consiste en omitir el handle. `bus_space_read_4()` recibe un `bus_space_tag_t` y un `bus_space_handle_t`, obtenidos mediante `rman_get_bustag()` y `rman_get_bushandle()` sobre el recurso asignado durante el attach. Usar la dirección física bruta del registro, o un puntero obtenido mediante un cast directo del recurso, se salta la abstracción de bus de la plataforma. En amd64 el programa puede parecer que funciona (el kernel tiene la región MMIO mapeada de una forma que tolera esto), pero en arm64 el acceso al registro falla.

La corrección es mecánica: usa siempre `bus_space_read_N()` / `bus_space_write_N()` o los helpers más modernos basados en recursos `bus_read_N()` / `bus_write_N()`; nunca desreferencies punteros brutos para acceder a la memoria del dispositivo.

El segundo error es un tamaño incorrecto. `bus_space_read_4` lee un valor de 32 bits. Si el registro es de 16 bits y el código lee 4 bytes, también leerá en el registro adyacente, y el valor de ese registro adyacente aparecerá en los bits 16 a 31 del valor devuelto. Aún peor, algunos dispositivos no toleran un tamaño incorrecto y responden con un error. La corrección consiste en usar la variante correcta, `read_1`, `read_2`, `read_4` o `read_8`, para la anchura de cada registro, tal como se documenta en el datasheet del dispositivo.

El tercer error es un offset incorrecto. El handle de `bus_space` apunta a la base de la región mapeada; el offset se suma para calcular la dirección del registro. Un error tipográfico en el offset hace que se lea un registro diferente. Por ejemplo, leer el offset `0x18` en lugar de `0x10` produce un valor inesperado, y la lógica posterior del driver se basa en una lectura falsa. La corrección consiste en definir cada offset como una constante con nombre en un archivo de cabecera, y referirse a la constante en lugar del número: `#define MYFIRST_REG_STATUS 0x10`, `#define MYFIRST_REG_CONFIG 0x14`, y así sucesivamente.

En el caso de DMA, el uso incorrecto más habitual consiste en liberar o reutilizar un buffer mientras el dispositivo todavía está leyendo de él o escribiendo en él. El síntoma característico es la corrupción intermitente de datos, a veces solo bajo alta carga. La causa es la ausencia de llamadas a `bus_dmamap_sync`, un buffer `bus_dmamem` liberado mientras el descriptor sigue en cola, o una dirección incorrecta en `bus_dmamap_sync`.

El enfoque de diagnóstico consiste en registrar cada operación de mapeado, sincronización y desmapeado de DMA, y después cruzar ese registro con el estado del anillo de descriptores. Un one-liner de DTrace sobre los caminos DMA del driver suele ser suficiente para localizar el error:

```sh
dtrace -n 'fbt::myfirst_dma_sync:entry { printf("%s dir=%d", probefunc, arg1); }'
```

Para un tratamiento completo de las reglas y trampas del DMA, el Capítulo 21 es la referencia. El papel de este capítulo de depuración es ayudarte a reconocer los síntomas y acotar la causa.

### 7.4 Use-After-Detach

Una categoría de bug más sutil aparece cuando la ruta de detach del driver retorna y el softc del driver se libera, pero alguna otra parte del kernel todavía mantiene una referencia a esa memoria liberada. Algunos ejemplos:

- una interrupción que llega después de `bus_release_resource` pero antes de que el handler se haya desmontado
- un callout que se dispara tras el detach y desreferencia un softc liberado
- una sonda de DTrace en el driver que se dispara desde una tarea que sigue ejecutándose en el momento de la descarga
- un nodo de dispositivo de caracteres que un proceso de usuario mantiene abierto y recibe I/O mientras el driver se descarga

Los síntomas son casi siempre fatales: page faults en código del driver alcanzado tras el detach, kernel panics con stacks corruptos, o datos espurios en el buffer de mensajes del kernel justo antes del pánico.

La corrección tiene varios componentes, cada uno de los cuales la ruta de detach debe implementar en un orden concreto:

1. Primero, impide nuevas entradas: establece un flag en el softc (`sc->sc_detaching = 1`), o adquiere un write lock que todos los puntos de entrada comprueben, de modo que las nuevas llamadas detecten que el driver está siendo retirado.

2. Espera a que los llamadores en curso terminen. `bus_teardown_intr` drena el handler de interrupciones. `callout_drain` espera a que un callout pendiente se complete. `taskqueue_drain` drena las tareas diferidas. `destroy_dev_sched_cb` espera a que los descriptores de archivo abiertos se cierren.

3. Solo después de que todos los llamadores externos se hayan drenado, libera los recursos que el driver asignó en el attach: libera memoria, libera IRQs, libera recursos de memoria, destruye mutexes.

El principio es sencillo: el detach es la imagen especular del attach, y todo recurso asignado en el attach debe liberarse en el detach en orden inverso. Las violaciones de este principio producen bugs de use-after-detach.

La herramienta de depuración de elección es el propio kernel de depuración. Un kernel con `DEBUG_MEMGUARD` habilitado puede configurarse para envenenar la memoria liberada, de modo que un acceso a un softc liberado produce un page fault inmediato con un stack limpio, en lugar de una corrupción sutil que puede tardar horas en manifestarse.

### 7.5 Errores en el handler de interrupciones

Los autores de drivers que son nuevos en el kernel a veces tratan los handlers de interrupciones como funciones ordinarias y cometen errores habituales pero perjudiciales:

- llamar a una función que duerme (`malloc(..., M_WAITOK, ...)`, `tsleep`, `mtx_lock` de un sleep lock, `copyin`, `uiomove`) desde un handler de interrupciones de nivel filtro
- intentar mantener un sleep lock durante un período prolongado
- leer o escribir un bloque grande de datos dentro del handler de interrupciones en lugar de usar un `taskqueue`
- no hacer ack de la interrupción, de modo que el handler se dispara indefinidamente en un bucle

Cada uno de ellos tiene un síntoma distinto. Dormir en contexto de interrupción produce una violación de `WITNESS` o, en kernels de producción, una corrupción silenciosa del estado del planificador. Mantener un sleep lock demasiado tiempo provoca que otros threads giren o duerman, y la latencia se dispara. Hacer trabajo pesado en el handler bloquea las interrupciones posteriores y degrada la capacidad de respuesta de todo el sistema. No hacer ack de la interrupción mantiene una CPU al 100% gestionando interrupciones indefinidamente.

La solución es siempre la misma: hacer el handler de interrupciones corto y atómico. Debe:

1. Leer el registro de estado para determinar la causa.
2. Reconocer la interrupción escribiendo en el registro de estado.
3. Delegar cualquier trabajo sustancial a un `taskqueue` o `ithread`, como se explora en el Capítulo 19.
4. Retornar.

El procesamiento complejo, la asignación de memoria y las operaciones de larga duración pertenecen a la ruta diferida, no al propio handler.

DTrace es especialmente útil para diagnosticar el rendimiento del handler de interrupciones. El proveedor `intr`, o una sonda `fbt` en la entrada del handler, puede medir cuánto tarda cada invocación:

```sh
dtrace -n 'fbt::myfirst_intr:entry { self->t = timestamp; }
           fbt::myfirst_intr:return /self->t/ {
               @ = quantize(timestamp - self->t); self->t = 0; }'
```

Un handler sano retorna en unos pocos microsegundos. Si la cuantización muestra invocaciones de cientos de microsegundos o milisegundos, el handler está haciendo demasiado trabajo y debe refactorizarse.

### 7.6 Errores de secuenciación en el ciclo de vida

El ciclo de vida del driver atraviesa las fases probe, attach, open, close, detach y unload. Cada método tiene reglas sobre lo que puede hacer y lo que debe haber ocurrido antes. Incumplir esas reglas produce bugs característicos:

- llamar a `bus_alloc_resource_any` durante el probe (debe ocurrir en el attach) produce asignaciones parciales y confusión en la lógica de ordenación del probe
- hacer trabajo sustancial en el probe ralentiza el boot de forma significativa, porque el mismo probe se ejecuta para cada dispositivo candidato
- crear el `cdev` antes de que el hardware esté inicializado permite a los procesos de usuario abrir el dispositivo y recibir I/O sobre un estado sin inicializar
- destruir el `cdev` en el detach mientras otros threads todavía lo tienen abierto corrompe el estado de devfs
- hacer trabajo significativo en el unload del módulo en lugar de en el detach deja sin liberar los recursos asignados por cada attach

La solución es mantener cada método del ciclo de vida centrado en su tarea:

- **probe** solo identifica el dispositivo y devuelve `BUS_PROBE_DEFAULT` o un error; sin asignaciones, sin registro
- **attach** asigna recursos, inicializa el softc, configura las interrupciones, crea el `cdev` y registra el dispositivo
- **detach** invierte exactamente el attach, en orden inverso
- **open / close** gestionan el estado por archivo sin tocar los recursos de todo el dispositivo
- **unload** realiza solo la limpieza a nivel de módulo; la limpieza por dispositivo pertenece al detach

Cuando un driver sigue esta estructura, los bugs del ciclo de vida son raros. Cuando se desvía de ella, son frecuentes y dolorosos.

### 7.7 Errores al copiar datos hacia o desde el espacio de usuario

Las funciones `copyin(9)`, `copyout(9)` y las relacionadas `fueword` / `suword` transfieren datos a través del límite usuario/kernel. Están protegidas por el sistema de memoria virtual: si la dirección en espacio de usuario no es válida, la copia devuelve `EFAULT` en lugar de provocar un pánico. Sin embargo, esta protección solo se aplica si la copia se realiza a través de estas funciones. Un driver que desreferencia directamente un puntero de espacio de usuario entrará en pánico en el momento en que el puntero de usuario sea inválido, lo que en despliegues reales ocurre la mayor parte del tiempo.

El síntoma es un pánico con una dirección de espacio de usuario en la instrucción que falla, con el stack apuntando a la ruta read/write/ioctl del driver.

La corrección es obligatoria: cada vez que datos de usuario cruzan el límite, usa `copyin`/`copyout` o un `uiomove` a través de un `struct uio`. Nunca hagas un cast de un puntero de usuario a un puntero de kernel y lo desreferencies. Esta regla es absoluta.

En el caso concreto del ioctl, el handler `d_ioctl` recibe un puntero de kernel, porque el kernel ya ha copiado el argumento de tamaño fijo desde el espacio de usuario. Pero si el argumento del ioctl es una estructura que contiene un puntero a un buffer de usuario de mayor tamaño, ese puntero embebido sigue siendo de espacio de usuario, y es necesario usar `copyin` para acceder de forma segura al buffer al que apunta.

### 7.8 Bugs a nivel de módulo

Algunos bugs afectan al módulo completo en lugar de a un único dispositivo:

- el módulo no carga: el loader imprime un error en `dmesg`; léelo y corrige el síntoma (dependencia ausente, fallo de resolución de símbolo, incompatibilidad de versión)
- el módulo no descarga: normalmente significa que un dispositivo sigue conectado. Desconecta todos los dispositivos (`devctl detach myfirst0`) antes de `kldunload`
- el módulo descarga pero deja basura: la ruta de unload (el handler de eventos de `module_t`, o el unload de `DRIVER_MODULE_ORDERED`) no revirtió la carga del módulo. Corrígelo leyendo la ruta de unload y haciéndola simétrica a la de carga.

Las herramientas de depuración son sencillas: `dmesg | tail` muestra los mensajes de carga/descarga, `kldstat -v` muestra el estado del módulo, `vmstat -m` muestra si el pool de malloc del módulo se vació al descargarse.

### 7.9 Una lista de comprobación para depuración

A continuación se presenta una lista de comprobación compacta que relaciona herramientas con síntomas. Puedes tenerla a mano durante el desarrollo de drivers.

| Síntoma                                              | Primera herramienta              | Segunda herramienta             |
|------------------------------------------------------|----------------------------------|---------------------------------|
| Línea de `dmesg` incorrecta o ausente                | `dmesg`, `/var/log/messages`     | añadir `device_printf`          |
| `InUse` crece en `vmstat -m`                         | `vmstat -m`                      | recuento malloc con DTrace      |
| Pánico por orden de lock                             | traza de `WITNESS`               | `ddb> show locks`               |
| Corrupción esporádica bajo carga                     | `WITNESS`, `INVARIANTS`          | temporización con DTrace        |
| Las lecturas de registro devuelven 0xFFFFFFFF        | revisar el handle de `bus_space` | datasheet, offset               |
| La máquina se cuelga al usar el dispositivo          | DTrace `fbt::myfirst_*`          | `ddb> bt`                       |
| Pánico tras el detach                                | `DEBUG_MEMGUARD`                 | auditar orden del detach        |
| Sistema lento bajo carga del driver                  | proveedor `intr` de DTrace       | acortar el handler              |
| ioctl con comando incorrecto                         | `ktrace` / `kdump`               | decodificar el comando          |
| Pánico con dirección de fallo en espacio de usuario  | revisar `copyin`/`copyout`       | auditar el puntero del ioctl    |
| El módulo no se descarga                             | `kldstat -v`                     | `devctl detach`                 |

La tabla no es exhaustiva, pero cubre la gran mayoría de los bugs reales de drivers. Con ella y las herramientas de las secciones 1 a 6, dispones de un enfoque estructurado para la mayoría de los diagnósticos en campo.

### Cerrando la sección 7

La sección 7 dirigió el conjunto de herramientas formado por el registro de eventos, el análisis de logs, los kernels de depuración, DTrace y `ktrace` hacia los errores que el lector tiene más probabilidades de encontrar: fugas de memoria, condiciones de carrera, errores de acceso al bus, uso tras detach, errores en interrupciones, errores de ciclo de vida, fallos en la frontera usuario/kernel y problemas a nivel de módulo. Para cada categoría, la sección describió el síntoma, la causa subyacente y la herramienta que lo diagnostica con mayor eficacia. La lista de verificación al final hace explícita esa correspondencia, y las próximas sesiones frente al teclado permitirán al lector elegir la herramienta correcta en el primer intento, no en el tercero.

La brecha que queda por cubrir es el driver en sí. Hasta ahora en este capítulo, `myfirst` se ha utilizado principalmente como objetivo de herramientas externas. La sección 8 cierra esa brecha refactorizando el driver para exponer puntos de traza y un control de nivel de verbosidad de depuración propios, de modo que la instrumentación se convierta en una parte orgánica del código y no en algo añadido a posteriori. La versión sube a `1.6-debug` y el driver incorpora `myfirst_debug.h` junto con la estructura que lo acompañará durante el resto del libro.

## Sección 8: Refactorización y versionado con puntos de traza

Las siete secciones anteriores de este capítulo trataban de aprender a usar herramientas que ya existen en FreeBSD: `printf`, `dmesg`, kernels de depuración, DTrace, `ktrace`, y la disciplina de leer su salida. La sección 8 mira hacia adentro. El driver `myfirst` que llegó del Capítulo 22 como versión `1.5-power` no tiene ninguna infraestructura de depuración propia real. Cada instrucción de registro es incondicional. No hay ningún control que el operador pueda ajustar, ningún sysctl con el que solicitar una salida más detallada cuando surge un problema, y ningún punto de traza estático al que engancharse con DTrace. La sección 8 corrige esa carencia.

El trabajo en esta sección es pequeño en líneas de código, pero de gran impacto. Al final, el driver tendrá:

1. Una cabecera `myfirst_debug.h` que define una macro `DPRINTF`, bits de verbosidad y puntos de traza SDT.
2. Un árbol sysctl (`dev.myfirst.0.debug`) que establece la verbosidad en tiempo de ejecución.
3. Un patrón de traza de entrada-salida-error aplicado de forma coherente en todo el driver.
4. Tres sondas de proveedor SDT que exponen los eventos de apertura, cierre y I/O.
5. Un documento `DEBUG.md` en el árbol de ejemplos que describe cómo configurar y leer la nueva salida.
6. Un salto de versión a `1.6-debug` registrado en `myfirst_version` y `MODULE_VERSION`.

El driver resultante será la plataforma para los capítulos restantes del libro. Cada nuevo subsistema añadido en las partes 5, 6 y 7 se enganchará a este marco, de modo que la infraestructura de traza crezca con el driver en lugar de añadirse al final.

### 8.1 Por qué usar una cabecera de depuración

Los drivers de FreeBSD más grandes tienen un archivo de cabecera dedicado para la infraestructura de depuración. El patrón es visible en `/usr/src/sys/dev/ath/if_ath_debug.h`, `/usr/src/sys/dev/bwn/if_bwn_debug.h`, `/usr/src/sys/dev/iwn/if_iwn_debug.h`, y muchos otros. Cada una de estas cabeceras:

1. define una macro `DPRINTF` que comprueba una máscara de bits de verbosidad en el softc
2. declara un conjunto de clases de verbosidad como `#define MYF_DBG_INIT 0x0001`, `MYF_DBG_OPEN 0x0002`, y así sucesivamente
3. opcionalmente declara sondas SDT que se corresponden con los límites funcionales del driver

Poner todo esto en una cabecera tiene dos ventajas. En primer lugar, la infraestructura de depuración es una responsabilidad independiente, separada limpiamente del código funcional. En segundo lugar, cuando un lector quiere entender la estrategia de trazado del driver, la cabecera es el único archivo que hay que leer. El patrón está consolidado, se usa ampliamente, y es lo que `myfirst` adoptará en esta sección.

La cabecera se guardará en `examples/part-05/ch23-debug/stage3-refactor/myfirst_debug.h`, y el código fuente del driver la incluirá al principio:

```c
#include "myfirst_debug.h"
```

Los cambios asociados en el código fuente del driver son aditivos: las llamadas existentes a `device_printf` pueden permanecer, y las nuevas llamadas se añaden a través de la macro `DPRINTF`.

### 8.2 Declaración de clases de depuración

El primer paso es definir las clases de verbosidad. Una clase es un único bit en una máscara de 32 bits. El driver reserva un bit por área funcional. Para `myfirst`, ocho clases son más que suficientes en esta etapa:

```c
/* myfirst_debug.h */
#ifndef _MYFIRST_DEBUG_H_
#define _MYFIRST_DEBUG_H_

#include <sys/sdt.h>

#define MYF_DBG_INIT    0x00000001  /* probe/attach/detach */
#define MYF_DBG_OPEN    0x00000002  /* open/close lifecycle */
#define MYF_DBG_IO      0x00000004  /* read/write paths */
#define MYF_DBG_IOCTL   0x00000008  /* ioctl handling */
#define MYF_DBG_INTR    0x00000010  /* interrupt handler */
#define MYF_DBG_DMA     0x00000020  /* DMA mapping/sync */
#define MYF_DBG_PWR     0x00000040  /* power-management events */
#define MYF_DBG_MEM     0x00000080  /* alloc/free trace */

#define MYF_DBG_ANY     0xFFFFFFFF
#define MYF_DBG_NONE    0x00000000

#endif /* _MYFIRST_DEBUG_H_ */
```

El valor `MYF_DBG_NONE` es el predeterminado: ninguna salida de depuración en absoluto. `MYF_DBG_ANY` habilita todas las clases, lo que resulta útil durante el desarrollo. Una configuración típica de operador podría habilitar únicamente `MYF_DBG_INIT | MYF_DBG_OPEN` para obtener eventos del ciclo de vida sin el ruido de cada operación de I/O.

Cada bit se declara como un solo dígito hexadecimal o un par, de modo que el operador puede establecer la máscara con un valor simple: `sysctl dev.myfirst.0.debug=0x3` habilita el trazado de inicialización y apertura. Los nombres comentados dejan claro el propósito de cada bit.

### 8.3 La macro DPRINTF

A continuación, la macro que condiciona el registro a la máscara:

```c
#ifdef _KERNEL
#define DPRINTF(sc, m, ...) do {                                        \
        if ((sc)->sc_debug & (m))                                        \
                device_printf((sc)->sc_dev, __VA_ARGS__);                \
} while (0)
#endif
```

La macro toma tres argumentos: el puntero al softc, la máscara de bits y la cadena de formato más los argumentos variables. Se expande a una comprobación de `sc->sc_debug & m`, seguida de una llamada a `device_printf` si la comprobación tiene éxito. El patrón `do { ... } while (0)` es el modismo estándar para macros de múltiples instrucciones que deben comportarse como una única instrucción en contextos `if`/`else`.

El coste de la llamada a `DPRINTF` cuando el bit está a cero es una única carga desde `sc->sc_debug`, una AND a nivel de bits y una bifurcación. La bifurcación casi siempre se predice como «no tomada», por lo que el coste en la práctica es insignificante. El driver puede distribuir llamadas a `DPRINTF` libremente por todo el código, y el usuario no incurre en ningún coste cuando la depuración está desactivada.

Cuando el bit está activo, la llamada se convierte en un `device_printf` normal, que aparece en `dmesg` como cualquier otro registro del kernel. Nada en el comportamiento difiere del camino de registro habitual, salvo que el registro ahora es condicional.

### 8.4 Añadir `sc_debug` al softc

El softc gana un nuevo campo, un `uint32_t sc_debug`. Su valor se manipula mediante un sysctl, que la función attach registra. El fragmento relevante de `myfirst_debug.c`:

```c
struct myfirst_softc {
        device_t        sc_dev;
        struct mtx      sc_mtx;
        struct cdev    *sc_cdev;
        uint32_t        sc_debug;       /* debug verbosity mask */
        /* other fields as in 1.5-power */
};
```

La función attach inicializa el campo a cero y registra el sysctl:

```c
sc->sc_debug = 0;
sysctl_ctx_init(&sc->sc_sysctl_ctx);
sc->sc_sysctl_tree = SYSCTL_ADD_NODE(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->sc_dev)),
    OID_AUTO, "debug",
    CTLFLAG_RW, 0, "debug verbosity tree");

SYSCTL_ADD_U32(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(sc->sc_sysctl_tree),
    OID_AUTO, "mask",
    CTLFLAG_RW, &sc->sc_debug, 0, "debug class bitmask");
```

Después del attach, el sysctl aparece como:

```sh
sysctl dev.myfirst.0.debug.mask
```

y el operador puede cambiarlo a voluntad:

```sh
sysctl dev.myfirst.0.debug.mask=0x3     # enable INIT + OPEN
sysctl dev.myfirst.0.debug.mask=0xFFFFFFFF   # enable all classes
sysctl dev.myfirst.0.debug.mask=0        # disable
```

La rutina detach destruye el contexto sysctl de la misma manera que lo hace actualmente:

```c
sysctl_ctx_free(&sc->sc_sysctl_ctx);
```

Un detalle importa: colocar el campo sysctl cerca del inicio del softc lo sitúa en la primera línea de caché, donde su coste de acceso durante DPRINTF es mínimo. Se trata de un punto de rendimiento menor, pero el énfasis del libro en la programación con principios lo hace digno de mención.

### 8.5 El patrón de entrada / salida / error

Con `DPRINTF` instalado, las funciones del driver pueden usar un patrón coherente para el trazado. Cada función sustancial registra la entrada, la salida (o el error) y cualquier estado intermedio de interés. Para una función pequeña como `myfirst_open`, esto tiene el siguiente aspecto:

```c
static int
myfirst_open(struct cdev *dev, int flags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error = 0;

        DPRINTF(sc, MYF_DBG_OPEN, "open: pid=%d uid=%d flags=0x%x\n",
            td->td_proc->p_pid, td->td_ucred->cr_uid, flags);

        mtx_lock(&sc->sc_mtx);
        if (sc->sc_open_count >= MYFIRST_MAX_OPENS) {
                error = EBUSY;
                goto out;
        }
        sc->sc_open_count++;
out:
        mtx_unlock(&sc->sc_mtx);

        if (error != 0)
                DPRINTF(sc, MYF_DBG_OPEN, "open failed: error=%d\n", error);
        else
                DPRINTF(sc, MYF_DBG_OPEN, "open ok: count=%d\n",
                    sc->sc_open_count);

        return (error);
}
```

Tres principios guían el patrón:

1. El registro de entrada muestra quién llamó y con qué argumentos. Para open, se trata de `pid`, `uid` y los flags.
2. El registro de salida muestra el resultado: «failed: error=N» o «ok: ...» con cualquier estado relevante.
3. El estado intermedio se registra cuando es relevante. Para open, el nuevo recuento de aperturas es útil.

El patrón es el mismo para close, read, write e ioctl. Cada función tiene dos o tres llamadas a `DPRINTF` que enmarcan el trabajo.

Esta disciplina da sus frutos la primera vez que aparece un bug. Sin el patrón, el desarrollador debe añadir registros de forma retroactiva, intentando adivinar dónde está el problema. Con el patrón, activar un único bit (`sysctl dev.myfirst.0.debug.mask=0x2` para `MYF_DBG_OPEN`) produce una traza completa de cada apertura y cierre, con argumentos y resultados. El tiempo de diagnóstico se reduce de horas a minutos.

### 8.6 Adición de sondas SDT

La macro estática `DPRINTF` genera mensajes legibles por humanos en `dmesg`. Las sondas SDT producen eventos procesables por máquinas que DTrace puede agregar, filtrar y temporizar. Ambas tienen su lugar. El driver declara sondas SDT para los tres eventos más interesantes, y un usuario cuidadoso puede adjuntar scripts personalizados a voluntad.

En `myfirst_debug.h`, las declaraciones de sondas tienen este aspecto:

```c
#include <sys/sdt.h>

SDT_PROVIDER_DECLARE(myfirst);
SDT_PROBE_DECLARE(myfirst, , , open);
SDT_PROBE_DECLARE(myfirst, , , close);
SDT_PROBE_DECLARE(myfirst, , , io);
```

Las definiciones correspondientes residen en el código fuente del driver (`myfirst_debug.c`):

```c
#include <sys/sdt.h>

SDT_PROVIDER_DEFINE(myfirst);
SDT_PROBE_DEFINE2(myfirst, , , open,
    "struct myfirst_softc *", "int");
SDT_PROBE_DEFINE2(myfirst, , , close,
    "struct myfirst_softc *", "int");
SDT_PROBE_DEFINE4(myfirst, , , io,
    "struct myfirst_softc *", "int", "size_t", "off_t");
```

Las macros `DEFINE` registran las sondas con la infraestructura SDT del kernel. `DEFINEn` toma `n` argumentos, cada uno con una cadena de tipo C que describe lo que el script DTrace recibirá como `arg0`, `arg1`, etc.

El driver entonces dispara las sondas en los lugares apropiados:

```c
/* in myfirst_open */
SDT_PROBE2(myfirst, , , open, sc, flags);

/* in myfirst_close */
SDT_PROBE2(myfirst, , , close, sc, flags);

/* in myfirst_read or myfirst_write */
SDT_PROBE4(myfirst, , , io, sc, is_write, (size_t)uio->uio_resid, uio->uio_offset);
```

El coste de una sonda a la que ningún script se ha conectado es una única bifurcación en torno a un no-op, el mismo coste insignificante que `DPRINTF` con un bit a cero. Cuando un script DTrace sí se conecta, se toma la bifurcación, los argumentos se registran y el script los ve como `args[0]`, `args[1]`, y así sucesivamente.

Con estas sondas, un lector puede ahora ejecutar scripts como:

```sh
dtrace -n 'myfirst::: { @[probename] = count(); }'
```

Esto cuenta cada evento de sonda de `myfirst`. Para ver los bytes transferidos por segundo:

```sh
dtrace -n 'myfirst:::io { @["bytes"] = sum(arg2); }'
```

O para trazar cada apertura con PID y flags:

```sh
dtrace -n 'myfirst:::open { printf("open pid=%d flags=0x%x", pid, arg1); }'
```

El driver expone ahora su comportamiento de dos formas: legible por humanos a través de `DPRINTF`, y procesable por máquinas a través de SDT. Un operador puede elegir la forma adecuada para cada tarea.

### 8.7 Redacción del archivo DEBUG.md

El árbol de ejemplos incorpora un documento breve (`examples/part-05/ch23-debug/stage3-refactor/DEBUG.md`) que explica la infraestructura de depuración y trazado a un lector que descargue los archivos. El documento es breve, pero cubre:

1. Qué es `DPRINTF` y cómo habilitarlo.
2. La tabla de bits de clase.
3. Comandos `sysctl` de ejemplo para habilitar cada clase.
4. La lista de sondas SDT y el orden de los argumentos.
5. Tres comandos DTrace de ejemplo en una sola línea.
6. Cómo combinar `DPRINTF` y SDT para la depuración de extremo a extremo.

El documento no es largo: unas treinta líneas. Su propósito es hacer que las herramientas se documenten a sí mismas, de modo que un lector que retome el ejemplo dentro de un año sepa todavía cómo usarlo.

### 8.8 Incremento de la versión

Cada capítulo del libro que modifica el comportamiento del driver incrementa su versión. La regla es: la cadena de versión en el driver coincide con la versión mostrada en el README del árbol de ejemplos y en el texto del capítulo.

En el Capítulo 22, el driver alcanzó la versión `1.5-power`. La sección 8 del Capítulo 23 lo lleva a `1.6-debug`. Los cambios en `myfirst_debug.c` son:

```c
static const char myfirst_version[] = "myfirst 1.6-debug";

MODULE_VERSION(myfirst, 16);
```

El texto sobre la constante de versión indica explícitamente qué ha cambiado:

```c
/*
 * myfirst driver - version 1.6-debug
 *
 * Added in this revision:
 *   - DPRINTF macro with 8 verbosity classes
 *   - sysctl dev.myfirst.0.debug.mask for runtime control
 *   - SDT probes for open, close, io
 *   - Entry/exit/error pattern across all methods
 */
```

La función `attach` registra la versión en el nivel `MYF_DBG_INIT`, de modo que el operador puede confirmar el driver cargado:

```c
DPRINTF(sc, MYF_DBG_INIT, "attach: %s loaded\n", myfirst_version);
```

Con la verbosidad habilitada, un lector que cargue el driver verá:

```text
myfirst0: attach: myfirst 1.6-debug loaded
```

lo que confirma tanto la identidad del módulo como la infraestructura de depuración.

### 8.9 Cerrando la sección 8

La sección 8 ha refactorizado el driver `myfirst` para que lleve su propia infraestructura de depuración. El driver ahora tiene una máscara de verbosidad, un sysctl para establecerla en tiempo de ejecución, un patrón coherente de entrada-salida-error en cada función, y tres sondas SDT en los límites funcionales. La cabecera de depuración puede incluirse en capítulos futuros, de modo que cada subsistema añadido más adelante en el libro pueda engancharse al mismo marco.

La versión del driver avanza de `1.5-power` a `1.6-debug`. El árbol de ejemplos obtiene un directorio `stage3-refactor` con el código fuente final, un documento `DEBUG.md` y un `README.md` que guía al lector a través de la construcción, carga y prueba de la nueva infraestructura.

Con la sección 8 completa, el capítulo ha cubierto el arco completo: entender por qué la depuración del kernel es difícil, usar todas las herramientas que FreeBSD proporciona, reconocer los bugs más comunes, y finalmente construir el driver para que admita su propia depuración. El material restante del capítulo es la secuencia de laboratorios, los ejercicios de desafío y el material de cierre que conecta este capítulo con el Capítulo 24.

## Laboratorios prácticos

Los laboratorios de este capítulo forman una progresión. Cada uno refuerza una herramienta específica de las secciones 1 a 6, y el laboratorio final aplica la refactorización de la sección 8 para que el lector se marche con un driver instrumentado y listo para el resto del libro.

Ninguno de estos laboratorios es largo. Los cinco se pueden completar en una sola noche, aunque distribuirlos en dos o tres sesiones da al lector tiempo para asimilar cada herramienta.

### Laboratorio 23.1: Una primera sesión de DDB

**Objetivo:** Entrar en el depurador del kernel de forma deliberada, inspeccionar el estado del driver y salir con seguridad.

**Requisitos previos:** Un kernel de depuración construido como se describe en la sección 4, con `options KDB` y `options DDB`. El driver `myfirst` del Capítulo 22 (versión `1.5-power`) cargado.

**Pasos:**

1. Confirma que el kernel de depuración está en ejecución:

   ```sh
   sysctl kern.version
   sysctl debug.kdb
   ```

   El segundo comando debería mostrar DDB entre los backends disponibles.

2. Carga el driver y confirma el nodo de dispositivo:

   ```sh
   sudo kldload myfirst
   ls /dev/myfirst0
   ```

3. Entra en DDB con una interrupción de teclado. En la consola del sistema, pulsa `Ctrl-Alt-Esc` en una consola VGA, o envía el carácter BREAK en una consola serie. Aparece el prompt:

   ```
   KDB: enter: manual entry
   [thread pid 42 tid 100024 ]
   Stopped at      kdb_enter+0x37: movq    $0,0x158e4fa(%rip)
   db>
   ```

4. Muestra el árbol de dispositivos:

   ```
   db> show devmap
   ```

   Localiza `myfirst0` en la salida.

5. Imprime el backtrace del thread actual:

   ```
   db> bt
   ```

6. Imprime todos los procesos:

   ```
   db> ps
   ```

   Confirma que tu propio shell es visible en la lista.

7. Reanuda la ejecución del kernel:

   ```
   db> continue
   ```

   El sistema vuelve a la operación normal.

**Qué observar:**

- El kernel se detuvo limpiamente al producirse el break.
- Todos los dispositivos, procesos y threads del kernel eran inspeccionables.
- El comando `continue` devolvió el sistema a la normalidad sin ningún efecto secundario.

Anota el tiempo empleado y cualquier observación en el cuaderno de laboratorio bajo "Chapter 23, Lab 1". El lector debe llevarse la conclusión de que DDB es una herramienta segura cuando se utiliza de forma deliberada.

### Laboratorio 23.2: Medición del driver con DTrace

**Objetivo:** Usar DTrace para medir las tasas de apertura, cierre y E/S del driver `myfirst` bajo una carga de trabajo sencilla.

**Requisitos previos:** Un kernel con `options KDTRACE_HOOKS` y `makeoptions WITH_CTF=1` cargado. El driver `myfirst` cargado en la versión `1.5-power` (las sondas SDT todavía no están presentes en esta fase; el laboratorio usa únicamente `fbt`).

**Pasos:**

1. Confirma que DTrace funciona:

   ```sh
   sudo dtrace -l | head -5
   ```

2. Inicia un script de DTrace que cuente las entradas a cada función de `myfirst`:

   ```sh
   sudo dtrace -n 'fbt::myfirst_*:entry { @[probefunc] = count(); }'
   ```

3. En un segundo terminal, ejercita el driver:

   ```sh
   for i in $(seq 1 100); do cat /dev/myfirst0 > /dev/null; done
   ```

4. Detén el script de DTrace (Ctrl-C en el primer terminal) y lee el recuento:

   ```
     myfirst_open                                             100
     myfirst_close                                            100
     myfirst_read                                             100
   ```

5. Ahora mide el tiempo empleado en `myfirst_read`:

   ```sh
   sudo dtrace -n 'fbt::myfirst_read:entry { self->t = timestamp; }
                   fbt::myfirst_read:return /self->t/ {
                       @ = quantize(timestamp - self->t); self->t = 0; }'
   ```

6. Ejercita el driver con 1000 lecturas, detén DTrace y lee la cuantización.

**Qué observar:**

- Cada `cat` produjo una apertura, una lectura y un cierre, lo que coincide con el recuento.
- El tiempo por lectura es pequeño (microsegundos) y se concentra en un único cubo de la cuantización.
- No se realizó ninguna modificación al driver; la medición es no invasiva.

### Laboratorio 23.3: Rastreo en el lado del usuario con ktrace

**Objetivo:** Rastrear un programa de usuario que ejercita el driver `myfirst` y verificar la secuencia de syscalls.

**Requisitos previos:** El driver cargado. El programa de prueba `myftest.c` de la Sección 6.4 compilado.

**Pasos:**

1. Compila el programa de prueba (de la Sección 6.4):

   ```sh
   cc -o myftest myftest.c
   ```

2. Ejecútalo bajo `ktrace`:

   ```sh
   sudo ktrace -di -f mytrace.out ./myftest
   ```

3. Vuelca el rastreo:

   ```sh
   kdump -f mytrace.out | less
   ```

4. Localiza los syscalls relevantes para el driver:

   ```sh
   kdump -f mytrace.out | grep -E 'myfirst0|CALL|RET'
   ```

5. Correlaciona el rastreo con los recuentos de DTrace del driver del Laboratorio 23.2. Cada syscall del lado del usuario debe coincidir con una o más entradas de función en el kernel.

**Qué observar:**

- La vista del programa de usuario sobre el driver queda completa en el rastreo.
- Los argumentos, los valores de retorno y los códigos de error son todos visibles.
- El rastreo del lado del usuario complementa la vista del kernel que ofrece DTrace.

### Laboratorio 23.4: Detección de una fuga de memoria con vmstat -m

**Objetivo:** Introducir una fuga deliberada, detectarla con `vmstat -m` y corregirla.

**Requisitos previos:** El código fuente del driver bajo el control del lector. Un módulo compilado y cargable.

**Pasos:**

1. Modifica `myfirst_open` para asignar un pequeño buffer y almacenarlo en `si_drv2`, sin liberarlo en `myfirst_close`:

   ```c
   /* in myfirst_open */
   dev->si_drv2 = malloc(128, M_MYFIRST, M_WAITOK | M_ZERO);
   ```

2. Construye y carga el driver.

3. Ejecuta una carga de trabajo que abra y cierre el dispositivo muchas veces:

   ```sh
   for i in $(seq 1 1000); do cat /dev/myfirst0 > /dev/null; done
   ```

4. Comprueba el pool de memoria:

   ```sh
   vmstat -m | grep myfirst
   ```

   La columna `InUse` debería mostrar 1000 (o más), y `MemUse` debería mostrar aproximadamente 128 KB.

5. Corrige la fuga añadiendo `free(dev->si_drv2, M_MYFIRST);` a `myfirst_close`.

6. Construye y recarga el driver. Vuelve a ejecutar la carga de trabajo.

7. Comprueba el pool:

   ```sh
   vmstat -m | grep myfirst
   ```

   `InUse` debería estabilizarse cerca de cero tras la carga de trabajo.

**Qué observar:**

- La salida de `vmstat -m` expuso la fuga de inmediato.
- Sin una herramienta especializada, la fuga habría pasado desapercibida hasta que el kernel se quedara sin memoria.
- La corrección es mecánica y sencilla una vez identificado el síntoma.

No dejes el código que provoca la fuga en el driver tras el laboratorio. Revierte a la versión limpia antes de continuar.

### Laboratorio 23.5: Instalación del refactor 1.6-debug

**Objetivo:** Aplicar el refactor de la Sección 8 al driver `myfirst` y confirmar que la nueva infraestructura funciona.

**Requisitos previos:** El código fuente del driver del Capítulo 22 bajo el control del lector. Un entorno de desarrollo capaz de construir módulos del kernel.

**Pasos:**

1. Crea `myfirst_debug.h` exactamente como se muestra en la Sección 8.2, y las declaraciones SDT de la Sección 8.6.

2. Actualiza el softc para añadir `uint32_t sc_debug;`.

3. Registra el sysctl en `myfirst_attach` como se muestra en la Sección 8.4.

4. Sustituye las llamadas incondicionales a `device_printf` por llamadas a `DPRINTF(sc, MYF_DBG_<class>, ...)` según corresponda.

5. Define los proveedores y sondas SDT en `myfirst_debug.c` (o `myfirst.c`), y dispáralas en open, close y read/write.

6. Incrementa `myfirst_version` a `1.6-debug` y `MODULE_VERSION` a 16.

7. Construye:

   ```sh
   cd /path/to/myfirst-source
   make clean && make
   ```

8. Recarga:

   ```sh
   sudo kldunload myfirst
   sudo kldload ./myfirst.ko
   ```

9. Confirma que el módulo se cargó en la nueva versión:

   ```sh
   kldstat -v | grep myfirst
   ```

10. Confirma que el sysctl está presente:

    ```sh
    sysctl dev.myfirst.0.debug.mask
    ```

11. Activa la verbosidad máxima y abre el dispositivo:

    ```sh
    sudo sysctl dev.myfirst.0.debug.mask=0xFFFFFFFF
    cat /dev/myfirst0 > /dev/null
    dmesg | tail
    ```

    La salida esperada incluye líneas como:

    ```
    myfirst0: open: pid=1234 uid=1001 flags=0x0
    myfirst0: open ok: count=1
    myfirst0: read: size=4096 off=0
    ```

12. Desactiva la verbosidad y confirma que los mensajes se detienen:

    ```sh
    sudo sysctl dev.myfirst.0.debug.mask=0
    cat /dev/myfirst0 > /dev/null
    dmesg | tail
    ```

13. Conecta DTrace a las sondas SDT:

    ```sh
    sudo dtrace -n 'myfirst::: { @[probename] = count(); }'
    ```

    En un segundo terminal, ejercita el driver:

    ```sh
    for i in $(seq 1 100); do cat /dev/myfirst0 > /dev/null; done
    ```

    Detén DTrace y confirma que las sondas se dispararon.

**Qué observar:**

- El control en tiempo de ejecución de la verbosidad es inmediato y responsivo.
- La ruta DPRINTF produce mensajes legibles por personas; las sondas SDT producen eventos analizables por máquina.
- Ambas formas son complementarias e independientes.
- Ambas están desactivadas por defecto, por lo que los usuarios en producción no pagan ningún coste en tiempo de ejecución.

El driver está ahora en la versión `1.6-debug`, con una infraestructura que dará soporte a todos los capítulos posteriores. Registra la actualización en el libro de registro.

## Ejercicios de desafío

Los ejercicios que figuran a continuación se basan en el material del capítulo. Son lo suficientemente abiertos como para que cada lector llegue a respuestas ligeramente distintas. Tómate tu tiempo; trabaja de forma incremental; usa las herramientas del capítulo en cada paso.

### Desafío 23.1: Una sonda SDT en un driver real

Escoge un driver de `/usr/src/sys/dev/` que ya contenga sondas SDT. Buenos candidatos son `virtqueue.c`, algunos archivos de `ath` o `if_re.c`.

Para el driver elegido:

1. Lista sus sondas con `dtrace -l -P <provider>`.
2. Escribe un one-liner que cuente eventos agrupados por nombre de sonda.
3. Escribe un segundo one-liner que agregue por uno de los argumentos de la sonda (por ejemplo, una longitud de paquete o un número de comando).
4. Explica en tres frases qué ganó el autor del driver al añadir las sondas.

### Desafío 23.2: Un script de DTrace personalizado

Escribe un script de DTrace que:

1. Se conecte a las sondas SDT de `myfirst` (añadidas en el Laboratorio 23.5).
2. Lleve el seguimiento del ciclo de vida de cada descriptor de archivo abierto por pid.
3. Imprima un resumen cuando se cierre un fd, mostrando: pid, número de lecturas, número de escrituras, total de bytes y el tiempo transcurrido entre la apertura y el cierre.

El script debe tener como máximo 50 líneas de código D. Pruébalo ejecutando un pequeño programa de usuario que abra, lea, escriba y cierre `/dev/myfirst0` varias veces.

### Desafío 23.3: Un experimento con WITNESS

Modifica el driver para introducir un error deliberado de ordenación de locks. Por ejemplo, añade dos mutexes `sc_mtx_a` y `sc_mtx_b`, y organiza una ruta de código para que tome A y luego B mientras otra ruta toma B y luego A.

Construye con `options WITNESS`, carga y provoca el error. Captura el pánico del kernel o la salida de WITNESS resultante. Describe en tres frases qué mostró WITNESS, por qué detectó el problema y cuál sería la corrección.

Asegúrate de revertir el error tras el ejercicio. No dejes un driver defectuoso cargado.

### Desafío 23.4: Categorización extendida de errores

Escoge cuatro rutas de error en el driver `myfirst` (por ejemplo, la ruta `EBUSY` en `myfirst_open`, una ruta `EINVAL` en `myfirst_ioctl`, etc.). Para cada una:

1. Identifica la causa subyacente que produciría este error en el mundo real.
2. Añade un `DPRINTF(sc, MYF_DBG_<class>, ...)` que capture la causa con claridad.
3. Escribe una breve nota en `DEBUG.md` explicando qué buscar en `dmesg` cuando se produzca el error.

El objetivo es que cada error del driver se explique por sí mismo sin que el lector necesite consultar el código fuente.

### Desafío 23.5: Debug en tiempo de arranque

Modifica el driver para registrar la versión en el momento del attach y los detalles del hardware únicamente cuando `bootverbose` esté activo. El efecto debe ser: un arranque normal muestra una línea, un arranque detallado (solicitado con `boot -v`) muestra la configuración completa.

Lee `/usr/src/sys/sys/systm.h` para ver la declaración de `bootverbose`, y `/usr/src/sys/kern/subr_boot.c` para ver ejemplos. Describe en dos frases para qué usa FreeBSD `bootverbose`, y por qué es mejor que un flag de verbosidad específico del driver para el caso particular del arranque temprano.

## Resolución de problemas y errores comunes

Cada lector se encontrará con al menos alguno de los problemas que se describen a continuación. Cada uno se documenta con el síntoma, la causa probable y el camino hacia la solución.

**"Mi kldload falla con `link_elf: symbol X undefined`."**

- Causa: el módulo depende de un símbolo del kernel que no está en el kernel en ejecución. Normalmente significa que el kernel es más antiguo que el código fuente del módulo, o que el módulo se compiló contra un build de kernel diferente.
- Solución: reconstruye el kernel y el módulo desde el mismo árbol de código fuente, usando los mismos flags de compilación. Confirma que `uname -a` y el directorio de build del módulo coinciden.

**"DTrace dice `invalid probe specifier`."**

- Causa: el nombre de la sonda está mal escrito, o el campo provider/module/function no existe en tiempo de ejecución.
- Solución: ejecuta `dtrace -l | grep <provider>` para listar las sondas disponibles y escoge un nombre de la lista. Recuerda que los comodines deben coincidir con un nombre existente.

**"WITNESS provoca un pánico en el arranque con `WITNESS_CHECKORDER`."**

- Causa: un driver temprano (habitualmente de terceros) viola el orden de locks declarado. Con un kernel estándar la violación era silenciosa; con WITNESS provoca un pánico inmediato.
- Solución: deshabilita temporalmente el driver problemático, o establece `debug.witness.watch=0` en el arranque para desactivar la comprobación, o reconstruye el driver problemático con un orden de locks corregido.

**"Los mensajes de DPRINTF no aparecen en `dmesg`."**

- Causa: el bit correspondiente en `sc_debug` no está activado.
- Solución: confirma el sysctl con `sysctl dev.myfirst.0.debug.mask`. Activa el bit: `sysctl dev.myfirst.0.debug.mask=0xFF`. Vuelve a intentarlo.

**"DTrace dice `probe description myfirst::: matched 0 probes`."**

- Causa: el driver no está cargado, o CTF no se generó (las sondas SDT están declaradas pero no son visibles para DTrace).
- Solución: ejecuta `kldstat | grep myfirst` y confirma que el módulo está cargado. Si lo está, reconstruye el kernel con `makeoptions WITH_CTF=1` y reinicia.

**"La fuga de memoria persiste tras mi corrección."**

- Causa: hay más de una fuga. La primera corrección abordó una ruta, pero otra ruta sigue teniendo la fuga.
- Solución: vuelve a examinar los números de `vmstat -m`. Si siguen creciendo, rastrea el asignador: añade `device_printf` en cada llamada a `malloc` y `free`, recarga, ejercita el driver y cuenta las apariciones.

**"El driver se descarga, pero `vmstat -m` sigue mostrando `InUse` distinto de cero."**

- Causa: la ruta de descarga no liberó toda la memoria. Esto ocurre con frecuencia porque algo se asignó en open pero no se liberó en close, y la ruta de close no se invocó nunca antes del detach.
- Solución: verifica que `destroy_dev_sched_cb` se utiliza para esperar a que finalicen las aperturas pendientes antes de que proceda el `detach`. Todo buffer `si_drv1` o `si_drv2` asignado por open debe liberarse en close o en el callback ordenado por detach.

**"Mi sonda `fbt` tiene un nombre engañoso."**

- Causa: algunos drivers usan funciones inline o helpers estáticos que el compilador puede haber integrado directamente. El binario compilado muestra únicamente la función contenedora.
- Solución: compila el módulo con `-O0` o añade `__noinline` al helper. Esto es útil únicamente para depuración; las builds de producción deben usar el nivel de optimización normal.

**"Los registros de ktrace son demasiado largos para leerlos."**

- Causa: por defecto, ktrace captura muchas clases de eventos a la vez.
- Solución: limita a clases específicas con el flag `-t`: `ktrace -t c ./myftest` captura únicamente syscalls, no E/S ni NAMI.

**"El kernel arranca pero no imprime ningún mensaje del driver en absoluto."**

- Causa: el driver no está compilado en el kernel, o el probe del driver devolvió un error distinto de cero.
- Solución: `kldstat -v | grep myfirst` confirma que el módulo está cargado. `dmesg | grep -i 'probe'` muestra los mensajes del probe. Si el módulo está cargado pero el driver no ha realizado el attach, comprueba `device_set_desc` y el valor de retorno de la función probe.

## Apéndice: Patrones de depuración reales en el árbol de FreeBSD

Los patrones de las secciones 7 y 8 se basan en convenciones que ya utilizan los drivers en producción. Un lector que inspeccione el árbol encontrará variaciones de estas convenciones por todas partes. Este apéndice ofrece un breve recorrido.

**`if_ath_debug.h`** (`/usr/src/sys/dev/ath/if_ath_debug.h`) define aproximadamente 40 clases de verbosidad que cubren reset, interrupciones, RX, TX, beacon, control de tasa, selección de canal y muchas otras, cada una expresada como un bit en una máscara de 64 bits. Los nombres de clase siguen el patrón `ATH_DEBUG_<subsystem>` (por ejemplo, `ATH_DEBUG_RECV`, `ATH_DEBUG_XMIT`, `ATH_DEBUG_RESET`, `ATH_DEBUG_BEACON`), y la macro `DPRINTF` sigue el patrón estándar. El driver utiliza `ATH_DEBUG_<class>` en cientos de lugares a lo largo del código fuente, lo que otorga a los operadores un control muy preciso sobre la salida de trazas.

**`virtqueue.c`** (`/usr/src/sys/dev/virtio/virtqueue.c`) hace un uso intensivo de sondas SDT. Un único `SDT_PROVIDER_DEFINE(virtqueue)` al principio del archivo declara el proveedor, seguido de una docena de llamadas a `SDT_PROBE_DEFINEn` para cada evento significativo: enqueue, dequeue, notify y demás. El driver dispara las sondas dentro de la ruta de ejecución crítica, y las sondas son no-ops cuando no hay ningún script asociado. Scripts de DTrace como `dtrace -n 'virtqueue::: { @[probename] = count(); }'` proporcionan visibilidad inmediata sobre la actividad del dispositivo virtio.

**`if_re.c`** (`/usr/src/sys/dev/re/if_re.c`) combina `device_printf`, `printf` e `if_printf`. Las llamadas a `device_printf` aparecen en attach y detach, donde la identidad del dispositivo es relevante. `if_printf` aparece en las rutas de paquetes, donde importa la identidad de la interfaz. `printf` aparece en los helpers compartidos, donde ninguna de las dos identidades está disponible. La división de responsabilidades es pragmática, no ideológica.

**`uart_core.c`** (`/usr/src/sys/dev/uart/uart_core.c`) define `UART_DBG_LEVEL` y una familia de macros `UART_DBG` que comprueban el nivel y registran mensajes mediante `printf`. El nivel se establece en tiempo de compilación a través de `options UART_POLL_FREQ` y `options UART_DEV_TOLERANCE_PCT`, entre otras opciones. El diseño es estático: las decisiones de depuración se fijan en tiempo de construcción, no en tiempo de ejecución. Es la elección correcta para un driver que debe ejecutarse muy pronto durante el boot, cuando sysctl todavía no está disponible.

**`random_harvestq.c`** (`/usr/src/sys/dev/random/random_harvestq.c`) utiliza WITNESS de forma exhaustiva. Cada lock del archivo tiene una comprobación `mtx_assert(&lock, MA_OWNED)` al comienzo de cada función que depende de ese lock. Cuando WITNESS está habilitado en la build, estas aserciones detectan cualquier llamante que haya olvidado adquirir el lock. En builds de producción, la aserción se elimina durante la compilación y no tiene ningún coste.

En todo el árbol, el patrón es consistente: los drivers exponen sus interioridades mediante una combinación de registro de depuración estático, sondas SDT y macros de aserciones. Cada uno de estos mecanismos no tiene ningún coste en tiempo de ejecución cuando está deshabilitado, y ofrece una visibilidad muy rica cuando está habilitado. El lector que adopte este patrón en su propio driver escribe código que se adapta bien tanto a la depuración del primer día como a la monitorización en producción.

## Apéndice: Casos prácticos de depuración de drivers

El material de este capítulo resulta más útil cuando se practica con bugs concretos. Este apéndice recorre tres casos prácticos realistas. Cada uno comienza con un síntoma que podrías encontrar, sigue los pasos de diagnóstico que permiten las herramientas de este capítulo, llega a la causa raíz y registra la solución. Los tres casos juntos cubren las tres grandes categorías de bugs en drivers: un bug de corrección en la ruta de registro, un bug de orden de locks y un bug de rendimiento en la ruta de interrupción.

Los casos están escritos como narrativas en lugar de como una secuencia árida de comandos, porque la depuración es un proceso narrativo. El desarrollador con experiencia no sigue un único algoritmo; lee una traza, formula una hipótesis, la comprueba, la refina e itera. Estos recorridos intentan preservar esa textura mientras mantienen cada paso reproducible.

### Caso práctico 1: El mensaje que desaparece

**Síntoma.** Un lector ha añadido la línea `DPRINTF(sc, MYF_DBG_OPEN, "open ok: count=%d\n", sc->sc_open_count)` a `myfirst_open`. Establece `sysctl dev.myfirst.0.debug.mask=0x2` para habilitar `MYF_DBG_OPEN`, abre el dispositivo y comprueba `dmesg`. No aparece nada.

**Primera hipótesis: el bit es incorrecto.** El lector vuelve a verificar la máscara. `MYF_DBG_OPEN` es `0x02`, de modo que establecer `mask=0x2` debería habilitarlo. La hipótesis es incorrecta.

**Segunda hipótesis: el dispositivo no se está abriendo.** El lector ejecuta `cat /dev/myfirst0 > /dev/null` y comprueba `dmesg`. Sigue sin aparecer nada.

**Tercera hipótesis: el sysctl no está actualizando realmente el campo.** El lector lee el sysctl de vuelta:

```sh
sysctl dev.myfirst.0.debug.mask
```

La salida muestra `0x0`. La escritura no surtió efecto. Este es el problema real.

**Acotando el problema.** El lector inspecciona el código de attach y encuentra:

```c
SYSCTL_ADD_U32(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(sc->sc_sysctl_tree),
    OID_AUTO, "mask",
    CTLFLAG_RD, &sc->sc_debug, 0, "debug class bitmask");
```

Nota: `CTLFLAG_RD` (solo lectura), no `CTLFLAG_RW`. El sysctl fue declarado como solo lectura, por lo que el comando `sysctl` parecía tener éxito pero en realidad rechazó la escritura en silencio.

**Solución.** Cambiar el flag a `CTLFLAG_RW`:

```c
SYSCTL_ADD_U32(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(sc->sc_sysctl_tree),
    OID_AUTO, "mask",
    CTLFLAG_RW, &sc->sc_debug, 0, "debug class bitmask");
```

Vuelve a compilar, recarga, establece la máscara, abre el dispositivo y el mensaje aparece.

**Lecciones aprendidas.** Tres herramientas combinadas encontraron este bug en menos de un minuto: `dmesg` mostró el síntoma (mensaje ausente), `sysctl` reveló el estado real (la escritura no surtía efecto) y el código fuente del driver hizo visible la causa (`CTLFLAG_RD`). Sin cualquiera de ellas, el lector podría haber perdido diez minutos siguiendo una hipótesis incorrecta. Las herramientas no reemplazan al pensamiento; proporcionan la evidencia que el pensamiento necesita.

La lectura de vuelta del sysctl es una disciplina útil en general. Siempre que se supone que un valor del driver es escribible, comprueba la escritura leyéndolo de nuevo. Este único hábito atrapará una clase de bugs que ha afectado a todos los desarrolladores de FreeBSD al menos una vez.

### Caso práctico 2: El pánico intermitente

**Síntoma.** Con carga ligera el driver funciona perfectamente. Con una carga de estrés (por ejemplo, cien lectores simultáneos), el kernel entra en pánico con:

```text
panic: mutex myfirst_lock recursed on non-recursive mutex myfirst
```

El pánico es intermitente: a veces la carga de trabajo se ejecuta durante cinco minutos antes de entrar en pánico, a veces falla de inmediato. El lector confirma que se está usando un kernel de depuración con `options WITNESS` y `options INVARIANTS`.

**Primer paso: leer el stack.** El mensaje de pánico incluye una traza de pila. Las líneas relevantes señalan a `myfirst_read` como el punto de recursión, con `myfirst_read` ya presente más arriba en el stack. De algún modo, `myfirst_read` está siendo llamada mientras una llamada previa a `myfirst_read` aún se está ejecutando, y ambas llamadas se encuentran en el mismo thread.

**Hipótesis.** Algo dentro de `myfirst_read` está desencadenando una segunda llamada a `myfirst_read`. La candidata más probable es `uiomove`, que llama a la ruta de fallo de página del programa de usuario, la cual en circunstancias muy inusuales puede volver a entrar en el I/O del dispositivo.

**Verificación.** El lector añade dos llamadas a `DPRINTF`, una en la entrada y otra justo antes de `uiomove`, capturando el thread actual en cada una:

```c
DPRINTF(sc, MYF_DBG_IO, "read entry: tid=%d\n", curthread->td_tid);
DPRINTF(sc, MYF_DBG_IO, "read about to uiomove: tid=%d\n", curthread->td_tid);
```

Vuelve a compilar, recarga y ejecuta la carga de trabajo. El registro del kernel muestra dos mensajes "read entry" para el mismo tid antes de cualquier "about to uiomove", lo que confirma la hipótesis.

**Diagnóstico.** La llamada `mtx_lock(&sc->sc_mtx)` en `myfirst_read` se mantiene adquirida durante `uiomove`. El destino de `uiomove` resulta estar en una región de `/dev/myfirst0` mapeada en memoria con `mmap`, porque la carga de trabajo de prueba usaba `mmap` para la salida. El fallo de página que desencadena `uiomove` vuelve a entrar en `myfirst_read` para atender el fallo, lo que intenta adquirir `sc->sc_mtx` por segunda vez.

**Solución.** El lock debe liberarse antes de `uiomove`. En general, un sleep lock nunca debe mantenerse adquirido a través de una función que pueda provocar un fallo de página.

```c
/* buggy version */
mtx_lock(&sc->sc_mtx);
error = uiomove(sc->sc_buffer, sc->sc_bufsize, uio);
mtx_unlock(&sc->sc_mtx);

/* fixed version */
mtx_lock(&sc->sc_mtx);
/* snapshot the buffer so we can release the lock */
tmp_buffer = sc->sc_buffer;
tmp_bufsize = sc->sc_bufsize;
mtx_unlock(&sc->sc_mtx);
error = uiomove(tmp_buffer, tmp_bufsize, uio);
```

Vuelve a compilar, recarga y ejecuta la carga de estrés. El pánico desaparece.

**Lecciones aprendidas.** `WITNESS` no detectó esto directamente porque se trata de una recursión, no de una violación de orden de locks. Sin embargo, `INVARIANTS` convirtió la recursión en un pánico limpio en lugar de corrupción silenciosa, y la traza de `DPRINTF` hizo visible la cronología. La regla "no mantengas un sleep lock adquirido a través de `uiomove` ni de ninguna función que pueda dormir" está documentada en `locking(9)`, pero es fácil de olvidar en la práctica. Cada adquisición de sleep lock en el código de un driver merece una breve comprobación mental: ¿podría la llamada envuelta dormir o provocar un fallo de página?

### Caso práctico 3: La interrupción lenta

**Síntoma.** Un driver funciona correctamente, pero el sistema responde con lentitud cuando el dispositivo está activo. La respuesta del shell se degrada, las aplicaciones interactivas se bloquean momentáneamente y `top` muestra un uso elevado de CPU del sistema.

**Primer paso: confirmar que el driver es la causa.** El lector descarga el driver y compara:

```sh
# before unload
top -aC1 -bn1 | head -20
sudo kldunload myfirst
# after unload
top -aC1 -bn1 | head -20
```

Si la lentitud desaparece al descargar el driver, el driver es el origen del problema.

**Segundo paso: identificar la ruta de código costosa.** El proveedor `profile` de DTrace muestrea el kernel a una frecuencia fija y muestra dónde se invierte el tiempo:

```sh
sudo dtrace -n 'profile-1001hz /arg0/ { @[stack(10)] = count(); } tick-10s { exit(0); }'
```

Tras diez segundos de la carga de trabajo, la salida muestra los stacks más activos. Si `myfirst_intr` domina la muestra, el manejador de interrupción es demasiado pesado.

**Tercer paso: medir cuánto tarda cada interrupción.**

```sh
sudo dtrace -n 'fbt::myfirst_intr:entry { self->t = timestamp; }
                fbt::myfirst_intr:return /self->t/ {
                    @ = quantize(timestamp - self->t); self->t = 0; }'
```

Un manejador en buen estado completa su ejecución en menos de 10 microsegundos (10 000 nanosegundos). Si la cuantización muestra la mayoría de las invocaciones en el rango de 100 000 a 1 000 000 de nanosegundos (de 100 microsegundos a 1 milisegundo), el manejador está haciendo demasiado trabajo.

**Cuarto paso: leer el código del manejador.** El lector inspecciona `myfirst_intr` y encuentra:

```c
static void
myfirst_intr(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct mbuf *m;

        mtx_lock(&sc->sc_mtx);
        m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
        if (m != NULL) {
                myfirst_process_data(sc, m);
                m_freem(m);
        }
        mtx_unlock(&sc->sc_mtx);
}
```

El manejador realiza trabajo real: asigna un mbuf, procesa datos y libera el mbuf. `myfirst_process_data` incluye varias unidades de trabajo, cada una con su propia asignación y procesamiento. Bajo carga, cada interrupción puede mantener el CPU ocupado durante cientos de microsegundos.

**Solución.** El trabajo pesado se traslada a un taskqueue. El manejador en sí hace el mínimo indispensable: reconoce la interrupción y señala el taskqueue.

```c
static void
myfirst_intr(void *arg)
{
        struct myfirst_softc *sc = arg;

        /* Acknowledge the interrupt */
        bus_write_4(sc->sc_res, MYFIRST_REG_INTR_STATUS, 0);
        /* Schedule deferred processing */
        taskqueue_enqueue(sc->sc_tq, &sc->sc_process_task);
}

static void
myfirst_process_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;
        struct mbuf *m;

        mtx_lock(&sc->sc_mtx);
        m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
        if (m != NULL) {
                myfirst_process_data(sc, m);
                m_freem(m);
        }
        mtx_unlock(&sc->sc_mtx);
}
```

El taskqueue se configura en `attach` tal como se explora en el capítulo 19.

Vuelve a compilar, recarga y ejecuta la carga de trabajo. La respuesta del shell se recupera, `top` muestra que el driver contribuye con una cantidad normal de CPU del sistema, y la cuantización de latencia de `myfirst_intr` está ahora concentrada en el rango de 1 a 10 microsegundos.

**Lecciones aprendidas.** Tres mediciones con DTrace guiaron la solución: `profile-1001hz` localizó el código caliente, `fbt::myfirst_intr:entry/return` midió la duración del manejador y la medición final tras la corrección confirmó la mejora. No se modificó ningún código fuente durante el diagnóstico; las modificaciones se realizaron solo después de haber comprendido el bug.

La regla de que los manejadores de interrupción deben ser cortos es conocida por los autores de drivers, pero su cumplimiento no es automático. Las builds de depuración no entran en pánico por un manejador lento. La única forma de detectar esta categoría de bug es medir, y DTrace es la herramienta adecuada para ello. Un equipo de desarrollo maduro ejecuta el perfilado `fbt::<driver>_intr:entry/return` como parte de cada ciclo de versión; sus manejadores se mantienen ligeros porque las mediciones hacen visible cualquier desviación.

### Caso práctico 4: El dispositivo que desaparece

**Síntoma.** Tras `kldunload myfirst`, un segundo `kldload myfirst` a veces entra en pánico con un fallo de página en `devfs_ioctl`, con el stack apuntando al antiguo `myfirst_ioctl`. El pánico no ocurre siempre, pero bajo carga (por ejemplo, si un daemon mantiene `/dev/myfirst0` abierto durante la descarga) es consistentemente fatal.

**Primer paso: leer el pánico.** El stack muestra `devfs_ioctl -> myfirst_ioctl -> (address not found)`. La instrucción que falla es la correspondiente a "address not found". Se trata de un use-after-free del código de una función: el kernel está intentando llamar a una función cuya memoria ya ha sido desmapeada.

**Hipótesis.** La ruta de descarga del driver no esperó a que terminase el proceso que tenía el dispositivo abierto. El proceso emitió un ioctl entre la descarga y la recarga; el ioctl se despachó al `myfirst_ioctl` ya desmapeado.

**Verificación.** El lector inspecciona `myfirst_detach` y encuentra:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        destroy_dev(sc->sc_cdev);
        bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem);
        mtx_destroy(&sc->sc_mtx);
        return (0);
}
```

La llamada a `destroy_dev` es el problema: destruye el nodo de dispositivo de inmediato, pero puede haber ioctls en vuelo sobre ese nodo que aún estén ejecutándose. Cuando `destroy_dev` retorna, el driver cree que es seguro liberar los recursos. Pero el ioctl sigue despachándose justo cuando esos recursos desaparecen.

**Solución.** Usa `destroy_dev_sched_cb` (o de forma equivalente, `destroy_dev_sched` y después esperar a que finalice) para diferir la destrucción real hasta que ningún thread esté dentro de los métodos del dispositivo.

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* Tell devfs to destroy the node after all callers finish */
        destroy_dev_sched_cb(sc->sc_cdev, myfirst_detach_cb, sc);
        /* Return success; cleanup happens in the callback */
        return (0);
}

static void
myfirst_detach_cb(void *arg)
{
        struct myfirst_softc *sc = arg;

        bus_release_resource(sc->sc_dev, SYS_RES_MEMORY, 0, sc->sc_mem);
        mtx_destroy(&sc->sc_mtx);
}
```

El detach retorna de inmediato; la liberación real de recursos ocurre en el callback, que está programado para ejecutarse solo cuando todos los llamadores dentro de los métodos del `cdev` hayan salido.

Vuelve a compilar y recarga repetidamente bajo la carga de trabajo problemática. El panic ha desaparecido.

**Lecciones aprendidas.** Este es el clásico bug de use-after-detach descrito en la Sección 7.4. La opción `DEBUG_MEMGUARD` del kernel de depuración lo habría detectado antes, envenenando la memoria liberada y convirtiendo la corrupción latente en un fallo de página inmediato en la siguiente llamada. La corrección utiliza una primitiva de FreeBSD (`destroy_dev_sched_cb`) que existe precisamente para este caso. Leer la documentación de gestión de `cdev` en `cdev(9)` es el ejercicio que previene este bug en el código nuevo.

### Caso de estudio 5: La salida corrompida

**Síntoma.** Un programa en el espacio de usuario lee de `/dev/myfirst0` esperando un patrón concreto (por ejemplo, la secuencia `0x01, 0x02, 0x03, ...`). La mayor parte del tiempo el patrón es correcto. Ocasionalmente, en un sistema con mucha carga, el patrón falla: uno o dos bytes son erróneos o la secuencia aparece desplazada. No hay ningún panic, ningún código de error, solo datos sutilmente incorrectos.

**Primer paso: reproducir de forma fiable.** El lector escribe un pequeño programa de prueba que lee repetidamente y compara el resultado con el patrón esperado. Bajo carga, los fallos ocurren aproximadamente una vez de cada diez mil lecturas. El lector confirma que descargar el driver y usar una prueba simple con `/dev/zero` no produce ningún fallo, lo que descarta el programa de usuario como origen del error.

**Hipótesis 1: una condición de carrera en el buffer.** Si dos threads leen de forma concurrente y el buffer se comparte sin ningún lock, pueden corromperse mutuamente. El lector inspecciona el driver y encuentra:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;

        /* Generate the pattern into sc->sc_buffer */
        for (int i = 0; i < sc->sc_bufsize; i++)
                sc->sc_buffer[i] = (uint8_t)(i + 1);

        return (uiomove(sc->sc_buffer, sc->sc_bufsize, uio));
}
```

El buffer pertenece al softc y se comparte entre todas las aperturas. Dos threads que llamen a `read` simultáneamente escriben en el mismo buffer; el segundo thread sobrescribe el contenido del primero antes de que este haya terminado de copiarlo. El `uiomove` del primer thread lee entonces parte del patrón del primer thread y parte del segundo. El resultado es una secuencia ininteligible.

**Verificación.** El lector añade un `DPRINTF` al inicio y al final de la lectura, capturando el tid del thread:

```c
DPRINTF(sc, MYF_DBG_IO, "read start: tid=%d\n", curthread->td_tid);
/* ... generate and uiomove ... */
DPRINTF(sc, MYF_DBG_IO, "read end: tid=%d\n", curthread->td_tid);
```

Cuando se examina el log durante un evento de corrupción, aparecen dos mensajes `start` entre un único `start` y su `end` correspondiente. La condición de carrera queda confirmada.

**Solución.** El buffer debe ser por llamada (asignado en cada lectura) o el acceso debe serializarse con un mutex:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        uint8_t *buf;
        int error;

        buf = malloc(sc->sc_bufsize, M_MYFIRST, M_WAITOK);
        for (int i = 0; i < sc->sc_bufsize; i++)
                buf[i] = (uint8_t)(i + 1);

        error = uiomove(buf, sc->sc_bufsize, uio);
        free(buf, M_MYFIRST);
        return (error);
}
```

Una asignación por llamada elimina completamente el estado compartido y el error desaparece. Una solución basada en un lock es más rápida para buffers pequeños, pero solo permite un lector a la vez; la decisión depende del caso de uso del driver.

**Lecciones aprendidas.** La corrupción es sutil precisamente porque no produce un panic ni un error. `WITNESS` no puede ayudar aquí porque no hay ningún lock que ordenar. `INVARIANTS` no puede ayudar porque el estado compartido no dispara ninguna aserción. La única forma de encontrarlo es: comprobar la salida, observar la corrupción, formular una hipótesis sobre condiciones de carrera, añadir instrumentación para confirmar la hipótesis y aplicar la corrección. El patrón DPRINTF es la instrumentación; un banco de pruebas disciplinado es la reproducción. Juntos resuelven el problema.

Este tipo de error es el argumento más sólido para que los autores de drivers escriban bancos de pruebas desde el principio. Un pequeño programa que lea del dispositivo y compare contra una salida esperada detecta en minutos muchos errores que de otro modo sobrevivirían durante meses en producción.

### Resumen de los cinco casos

Los cinco casos de estudio cubrieron un conjunto deliberadamente diverso de errores. El caso 1 fue un error de corrección en el registro (logging), encontrado en segundos en cuanto se leyó el sysctl. El caso 2 fue una recursión de lock disparada por una ruta de paginación, encontrada combinando una traza de panic y DPRINTF. El caso 3 fue un error de rendimiento en el manejador de interrupciones, diagnosticado con el profiling de DTrace. El caso 4 fue un uso tras detach (use-after-detach) en la ruta de `cdev`, diagnosticado con la pila del panic y corregido con la primitiva correcta de FreeBSD. El caso 5 fue una condición de carrera sobre estado compartido en la ruta de lectura, encontrada con un banco de pruebas y DPRINTF.

Tres patrones se repiten a lo largo de los casos:

1. **Las herramientas encuentran síntomas; leer el código encuentra las causas.** Ninguna herramienta señaló directamente la línea con el error. Cada herramienta redujo el ámbito de la búsqueda: `vmstat -m`, WITNESS, DTrace, DDB. Luego el lector abrió el código fuente y encontró el fallo.

2. **La reproducción es más importante que las herramientas.** Un error que se reproduce de forma fiable puede diagnosticarse con casi cualquier herramienta. Un error que no se reproduce debe reproducirse primero, normalmente creando un script de carga que ejercite la ruta sospechosa.

3. **La infraestructura de debug amortiza su coste con creces.** El macro `DPRINTF`, las sondas SDT, el sysctl `sc_debug`: cada uno de ellos costó cinco minutos añadirlo y cada uno ahorró horas de depuración más adelante. El lector que escribe código instrumentado desde el principio es el lector que evita las largas y dolorosas sesiones de debug.

Con estos patrones en mente, el lector está preparado para llevar los métodos del capítulo hacia adelante. Cada nueva parte del driver `myfirst` añadida en capítulos posteriores se desarrollará con llamadas a `DPRINTF`, sondas SDT y bancos de pruebas; la reducción en el esfuerzo de depuración será perceptible a partir del Capítulo 24.

## Apéndice: Técnicas avanzadas de DTrace para el desarrollo de drivers

Este apéndice amplía la Sección 5 con un recorrido centrado en las técnicas de DTrace que aparecen con frecuencia en el desarrollo de drivers. La Sección 5 introdujo los conceptos básicos; este apéndice muestra cómo combinarlos de forma productiva. Los lectores que encuentren DTrace útil pueden conservar este apéndice como referencia rápida.

### Predicados y rastreo selectivo

Un predicado restringe una sonda a los eventos que cumplen una condición. Aparece entre `/ ... /`, entre la descripción de la sonda y la acción:

```sh
dtrace -n 'fbt::myfirst_read:entry /execname == "myftest"/ {
               @ = count(); }'
```

Solo se cuentan las lecturas de procesos llamados `myftest`. Los predicados pueden referenciar `pid`, `tid`, `execname`, `zonename`, `uid` y los argumentos de la sonda `arg0`, `arg1`, etc.

Un patrón habitual es filtrar por puntero softc para centrarse en una única instancia de dispositivo:

```sh
dtrace -n 'fbt::myfirst_read:entry /args[0] == 0xfffff8000a000000/ {
               @ = count(); }'
```

La dirección literal del softc se obtiene de `devctl` o inspeccionando el sysctl del driver. Cuando hay varios dispositivos `myfirst` presentes, esto aísla uno a la vez.

### Agregaciones que cuentan una historia

Las agregaciones son los caballos de batalla estadísticos de DTrace. La Sección 5 introdujo `count()` y `quantize()`; otras funciones de agregación son igualmente útiles:

- `sum(x)`: total acumulado de `x`
- `avg(x)`: media acumulada de `x`
- `min(x)`: mínimo de `x` observado
- `max(x)`: máximo de `x` observado
- `lquantize(x, low, high, step)`: cuantización lineal con límites
- `llquantize(x, factor, low, high, steps)`: cuantización logarítmica con límites

Un informe típico de latencia por función usa `quantize` porque revela la distribución:

```sh
dtrace -n 'fbt::myfirst_read:entry { self->t = timestamp; }
           fbt::myfirst_read:return /self->t/ {
               @ = quantize((timestamp - self->t) / 1000);
               self->t = 0; }'
```

Se divide entre 1000 para convertir nanosegundos en microsegundos, lo que da sentido a los rangos. Se añade una agregación indexada `@buf` para ver la latencia por tamaño de buffer:

```sh
dtrace -n '
fbt::myfirst_read:entry {
    self->t = timestamp;
    self->len = args[1]->uio_resid;
}
fbt::myfirst_read:return /self->t/ {
    @["read", self->len] = quantize((timestamp - self->t) / 1000);
    self->t = 0;
}'
```

Esto agrega por separado para cada valor distinto de `uio_resid`, revelando si las lecturas grandes son desproporcionadamente lentas.

### Variables locales al thread

El prefijo `self->` introduce una variable local al thread: cada thread ve su propia copia. Así funciona la medición de latencia de la Sección 5: la sonda de entrada almacena la marca de tiempo en `self->t` y la sonda de retorno la lee. Como ambas sondas se activan en el mismo thread, la variable es inequívoca.

Usos habituales:

```sh
self->start    /* time of entry for a specific function */
self->args     /* saved arguments for use in the return probe */
self->error    /* error code captured from return */
```

Las variables locales al thread se inicializan a cero por defecto. Úsalas libremente; su coste es una palabra de memoria por thread activo.

### Arrays y acceso asociativo

Los arrays de DTrace son arrays asociativos dispersos, indexados por cualquier expresión. Las agregaciones los usan implícitamente (la sintaxis `@[key]`), pero las variables ordinarias también pueden usarlos:

```sh
dtrace -n 'fbt::myfirst_read:entry {
               counts[args[0], execname]++; }'
```

Aunque en la práctica las agregaciones son preferibles porque acumulan correctamente en todos los CPUs.

### Rastreo especulativo

Para un evento de alta frecuencia en el que la mayoría de los casos no son interesantes, el rastreo especulativo de DTrace almacena la salida en un buffer hasta que se toma una decisión:

```sh
dtrace -n '
fbt::myfirst_read:entry {
    self->spec = speculation();
    speculate(self->spec);
    printf("read entry: pid=%d", pid);
}
fbt::myfirst_read:return /self->spec && args[1] == 0/ {
    speculate(self->spec);
    printf("read return: bytes=%d", arg1);
    commit(self->spec);
    self->spec = 0;
}
fbt::myfirst_read:return /self->spec && args[1] != 0/ {
    discard(self->spec);
    self->spec = 0;
}'
```

Solo las lecturas que retornan con éxito (`args[1] == 0`) tienen su salida confirmada; las lecturas fallidas se descartan. Esto produce un log silencioso y centrado bajo carga elevada.

El rastreo especulativo es una de las características más efectivas de DTrace y raramente se necesita para drivers pequeños. Pero en un driver con miles de eventos por segundo, marca la diferencia entre una salida útil y un aluvión ilegible.

### Cadenas de predicados para un rastreo preciso

Una necesidad habitual es rastrear el árbol de llamadas de una función desde un punto de entrada concreto. Los predicados pueden encadenarse usando flags locales al thread:

```sh
dtrace -n '
syscall::ioctl:entry /pid == $target/ { self->trace = 1; }
fbt::myfirst_*: /self->trace/ { printf("%s: %s", probefunc, probename); }
syscall::ioctl:return /self->trace/ { self->trace = 0; }'
```

Ejecútalo con `-p <pid>` para apuntar a un proceso concreto. El flag de rastreo se activa en la entrada a la syscall y se borra en el retorno, de modo que solo se registran las funciones del driver llamadas durante el ioctl. Este patrón es fundamental para aislar una acción de usuario concreta.

### Perfilado del kernel

El proveedor `profile` muestrea el kernel a una frecuencia regular. Las sondas más útiles son:

- `profile-97`: muestrea cada 1/97 de segundo en todos los CPUs
- `profile-1001hz`: muestrea a 1001 Hz, ligeramente desfasado respecto a cualquier carga que opere a intervalos de un segundo exacto
- `profile-5ms`: muestrea cada 5 ms

Cada muestra registra la pila actual. Agregar por pila revela dónde se consume el tiempo:

```sh
dtrace -n 'profile-1001hz /arg0/ { @[stack(8)] = count(); }
           tick-10s { exit(0); }'
```

El predicado `arg0` filtra los CPUs en reposo. El `stack(8)` registra los 8 frames superiores. El `tick-10s` finaliza la ejecución tras 10 segundos.

La salida tiene este aspecto:

```text
  kernel`myfirst_process_data+0x120
  kernel`myfirst_intr+0x48
  kernel`ithread_loop+0x180
  kernel`fork_exit+0x7f
  kernel`fork_trampoline+0xe
         15823
```

15 823 muestras en esa pila, de un total de aproximadamente 100 000 (10 segundos × 1001 Hz × unos pocos CPUs). La ruta de interrupción del driver domina. Combinado con la medición de latencia anterior, esto es suficiente para justificar una refactorización.

### Visualización legible de estructuras

Un kernel con CTF habilitado proporciona a DTrace los tipos de cada estructura del kernel. Esto permite que `print()` muestre los campos por nombre en lugar de memoria bruta:

```sh
dtrace -n 'fbt::myfirst_read:entry { print(*args[0]); }'
```

La salida muestra cada campo de la `struct myfirst_softc` sobre la que opera la lectura, por nombre de campo, con valores legibles.

Esta característica depende de que CTF haya sido generado (`makeoptions WITH_CTF=1`). Sin CTF, DTrace recurre a la impresión basada en direcciones, que es mucho menos legible.

### Salida con buffer circular

Para ejecuciones de DTrace muy largas, la salida por defecto llena la memoria. El flag `-b` establece un buffer circular que descarta los registros más antiguos cuando se llena:

```sh
dtrace -b 16m -n 'myfirst::: { trace(timestamp); }'
```

Un buffer circular de 16 MB contiene aproximadamente los últimos 16 MB de salida de rastreo. Los datos más antiguos se sobreescriben a medida que llegan datos nuevos, lo que generalmente no es un problema para el diagnóstico: la ventana de interés son los últimos segundos antes del problema, no la hora anterior.

### Salida limpia

DTrace acumula las agregaciones en memoria hasta que el script termina. Para una ejecución larga, el lector debe salir de forma limpia para ver las agregaciones:

```sh
dtrace -n 'fbt::myfirst_*:entry { @[probefunc] = count(); }
           tick-60s { printf("%Y\n", walltimestamp); printa(@);
                      clear(@); }'
```

Cada 60 segundos, la agregación se imprime y se borra. La salida es un informe continuo en lugar de un único informe al salir. Este patrón es ideal para ejecuciones nocturnas: la salida contiene instantáneas por minuto y el lector puede ver cómo evoluciona el comportamiento del driver.

### Resumen

Estas técnicas cubren en conjunto aproximadamente el 80% de las habilidades de DTrace que necesita un autor de drivers. El 20% restante proviene de la Guía de DTrace y de leer los scripts existentes en `/usr/share/dtrace/toolkit`.

Dos reglas generales guían el uso avanzado:

1. **Empieza con el script más sencillo que pueda responder la pregunta.** Añade predicados solo cuando el script produzca demasiada salida. Añade agregaciones solo cuando los recuentos y los tiempos importen.

2. **Valida la salida de DTrace con una carga de trabajo conocida.** Ejecuta primero un caso de prueba sencillo (por ejemplo, una apertura, una lectura y un cierre), confirma que la salida de DTrace coincide y solo entonces confía en la herramienta con una carga de trabajo compleja.

Con estas reglas, DTrace es la herramienta de depuración más potente del arsenal de FreeBSD, y la que recompensa cada hora de estudio con meses de tiempo de depuración ahorrado.

## Apéndice: Interpretación de las trazas de WITNESS

WITNESS suele ser la primera vez que un lector se enfrenta a la interpretación de una traza diagnóstica del kernel. Este apéndice recorre el formato de un mensaje de violación de WITNESS y explica qué significa cada campo.

### El formato general

Una violación de WITNESS tiene este aspecto:

```text
lock order reversal: (non-sleepable after sleepable)
 1st 0xfffff8000a000000 myfirst_lock (myfirst, sleep mutex) @ myfirst.c:123
 2nd 0xfffff8000a001000 myfirst_intr_lock (myfirst, spin mutex) @ myfirst.c:234
lock order myfirst_intr_lock -> myfirst_lock established at:
#0 0xffffffff80abcdef at kdb_backtrace+0x1f
#1 0xffffffff80abcdee at _witness_debugger+0x4f
#2 0xffffffff80abcdaf at witness_checkorder+0x21f
#3 0xffffffff80c00000 at _mtx_lock_flags+0x8f
#4 0xffffffff80ffff00 at myfirst_intr+0x30
#5 ...
panic: witness
```

El mensaje tiene tres secciones: la línea de cabecera, la sección de descripción del lock y el backtrace de dónde se estableció por primera vez el orden invertido.

### La cabecera

```text
lock order reversal: (non-sleepable after sleepable)
```

Esto indica que se adquirió un mutex de tipo spin (no durmiente) mientras ya se mantenía un mutex de tipo sleep (durmiente). El orden inverso (primero spin, luego sleep) está permitido; este orden (primero sleep, luego spin) no lo está.

Variantes habituales de la cabecera:

- `lock order reversal: (sleepable after non-sleepable)`: significa que el orden declarado fue violado posteriormente mediante una adquisición que va en sentido contrario
- `spin lock recursion`: se intentó adquirir un spinlock que el thread actual ya sostiene, lo que no está permitido salvo en el caso de spinlocks recursivos
- `spin lock held too long`: un spinlock estuvo retenido durante más tiempo del umbral establecido (habitualmente varios segundos)
- `blocking on condition variable with spin lock`: se intentó dormir en una variable de condición mientras se sostenía un spinlock

```text
 1st 0xfffff8000a000000 myfirst_lock (myfirst, sleep mutex) @ myfirst.c:123
 2nd 0xfffff8000a001000 myfirst_intr_lock (myfirst, spin mutex) @ myfirst.c:234
```

- **0xfffff8000a000000**: la dirección del mutex en memoria
- **myfirst_lock**: el nombre legible pasado a `mtx_init`
- **(myfirst, sleep mutex)**: la clase (normalmente el nombre del driver) y el tipo
- **@ myfirst.c:123**: la ubicación en el código fuente de la llamada a `mtx_init` (cuando WITNESS está configurado con DEBUG_LOCKS)

Ambos locks aparecen: primero el que ya estaba retenido y después el que se intenta adquirir.

### El backtrace

El backtrace muestra dónde se registró por primera vez el orden invertido. WITNESS recuerda la primera instancia de cada par (A, B) que observa; las violaciones posteriores hacen referencia a esa primera aparición. Si la primera aparición era válida en ese momento (por ejemplo, ambos locks se adquirieron correctamente) pero un camino posterior los invierte, el backtrace apunta a esa adquisición anterior y correcta. Es posible que tengas que buscar en el código fuente el camino concreto de inversión, que suele ser la pila de llamadas actual (no mostrada en este fragmento).

Dos marcos a identificar:

1. La función que actualmente retiene el primer lock e intenta adquirir el segundo (o viceversa).
2. La función que adquirió los dos locks por primera vez en el orden original.

Comparar ambos revela el conflicto.

### Lectura de un ejemplo real

Un ejemplo habitual en drivers nuevos:

```text
lock order reversal: (non-sleepable after sleepable)
 1st 0xfff0 myfirst_lock (sleep mutex) @ myfirst.c:100
 2nd 0xfff1 ithread_lock (spin mutex) @ kern_intr.c:234
```

El código del nuevo driver en `myfirst.c:100` adquiere `myfirst_lock` (un sleep mutex). En algún punto más profundo del camino de código, una función de planificación de interrupciones intenta adquirir `ithread_lock` (un spin mutex usado por el subsistema de interrupciones). Esto es la violación `sleep-then-spin`, que siempre es un error: nunca se debe adquirir un spinlock mientras se retiene un sleep lock.

La solución consiste en reescribir el código para que el spinlock (o el subsistema que lo usa) no sea llamado mientras se retiene `myfirst_lock`. Los enfoques más habituales son liberar `myfirst_lock` antes de la llamada y readquirirlo después si es necesario.

### Aserción de propiedad del lock

Independientemente de WITNESS, colocar `mtx_assert(&lock, MA_OWNED)` al inicio de cada función que presupone que un lock está retenido es un sólido patrón de programación defensiva. Cuando `INVARIANTS` está activado, la aserción se dispara ante cualquier violación. Combinados con WITNESS, estos dos mecanismos detectan la gran mayoría de los errores relacionados con locks en su primera aparición, mucho antes de que corrompan datos en producción.

### Cuando WITNESS genera demasiado ruido

En ocasiones, WITNESS genera violaciones en código que en realidad es correcto, normalmente porque la adquisición del lock está protegida por una comprobación en tiempo de ejecución que WITNESS no puede ver. Para esos casos, `mtx_lock_flags(&lock, MTX_DUPOK)` o `MTX_NEW` indican a WITNESS que permita la adquisición. Usa estos flags con moderación; la mayoría de las violaciones de WITNESS son reales.

## Apéndice: construcción de un toolkit de depuración específico del driver

Las macros de depuración y las sondas SDT añadidas a `myfirst` en la sección 8 son un punto de partida. A lo largo de la vida de un driver, el toolkit crece: más clases, más sondas, sysctls personalizadas y, quizás, un ioctl exclusivo de depuración que vuelca el estado interno. Este apéndice describe los patrones para extender el toolkit.

### Sysctls exclusivas de depuración

Un sysctl bajo `dev.myfirst.0.debug.*` es el lugar natural para los controles de depuración en tiempo de ejecución. Añade nodos sin restricciones:

- `dev.myfirst.0.debug.mask`: la máscara de bits de clase
- `dev.myfirst.0.debug.loglevel`: la prioridad de syslog para la salida de DPRINTF
- `dev.myfirst.0.debug.trace_pid`: un PID sobre el que centrar el trazado
- `dev.myfirst.0.debug.count_io`: un contador de eventos I/O procesados
- `dev.myfirst.0.debug.dump_state`: solo escritura; dispara un volcado de estado puntual

Cada uno de estos nodos tiene un coste mínimo en tamaño y resulta enormemente útil durante la depuración en campo. La regla es la siguiente: si un dato del estado sería útil de inspeccionar durante la búsqueda de un error, exponlo mediante sysctl.

### Un ioctl de depuración

Para un estado demasiado complejo o grande para exponerse como sysctl, un ioctl exclusivo de depuración es una buena opción. Define:

```c
#define MYFIRST_IOC_DUMP_STATE  _IOR('f', 100, struct myfirst_debug_state)

struct myfirst_debug_state {
        uint64_t        open_count;
        uint64_t        read_count;
        uint64_t        write_count;
        uint64_t        error_count;
        uint32_t        current_mask;
        /* ... */
};
```

El manejador del ioctl copia el estado actual en el buffer de usuario. Un pequeño programa de usuario lo lee e imprime:

```c
struct myfirst_debug_state s;
int fd = open("/dev/myfirst0", O_RDONLY);
ioctl(fd, MYFIRST_IOC_DUMP_STATE, &s);
printf("opens=%llu, reads=%llu, writes=%llu, errors=%llu\n",
    s.open_count, s.read_count, s.write_count, s.error_count);
```

Un ioctl de depuración resulta especialmente útil cuando el estado de interés no es un número simple sino una estructura, y cuando las restricciones de tipo de sysctl resultan incómodas.

### Contadores para eventos clave

El softc del driver incorpora un conjunto de contadores, uno por cada evento significativo. Los contadores son campos `uint64_t` simples que se incrementan con `atomic_add_64` o bajo el lock del softc. Se exponen mediante sysctl o el ioctl de depuración, y se reinician a petición del lector:

```c
sc->sc_stats.opens++;       /* in open */
sc->sc_stats.reads++;       /* in read */
sc->sc_stats.errors++;      /* on any error path */
```

Con diez contadores por driver, el coste de memoria es inferior a 100 bytes, y la visibilidad del comportamiento del driver es enorme. Los sistemas de monitorización en producción pueden consultar los contadores periódicamente y generar alertas ante anomalías.

### Un ioctl de autocomprobación

Para drivers complejos, un ioctl de autocomprobación puede resultar indispensable. Ejecuta una secuencia de pruebas internas (cada una es una función del driver) e informa de cuáles han pasado y cuáles han fallado. Los resultados se devuelven como una pequeña estructura. Un operador que depura un problema en campo puede ejecutar la autocomprobación y saber de inmediato si alguno de los subsistemas del driver está fallando.

El ioctl de autocomprobación no es un sustituto de las pruebas unitarias ni de las pruebas de integración. Es un atajo diagnóstico para el campo.

### Integración con scripts rc

Un script rc con soporte de depuración (en `/usr/local/etc/rc.d/myfirst_debug`) puede establecer la máscara de depuración durante el boot:

```sh
#!/bin/sh
# PROVIDE: myfirst_debug
# REQUIRE: myfirst
# KEYWORD: shutdown

. /etc/rc.subr

name="myfirst_debug"
rcvar="myfirst_debug_enable"
start_cmd="myfirst_debug_start"
stop_cmd="myfirst_debug_stop"

myfirst_debug_start()
{
        echo "Enabling myfirst debug mask"
        sysctl dev.myfirst.0.debug.mask=${myfirst_debug_mask:-0x0}
}

myfirst_debug_stop()
{
        echo "Disabling myfirst debug"
        sysctl dev.myfirst.0.debug.mask=0
}

load_rc_config $name
run_rc_command "$1"
```

Un operador establece `myfirst_debug_enable=YES` y `myfirst_debug_mask=0x3` en `/etc/rc.conf`, reinicia, y la infraestructura de depuración se activa automáticamente. Este patrón es el que usan los sistemas en producción para gestionar la verbosidad de depuración en múltiples dispositivos.

### Resumen

La infraestructura de la sección 8 es suficiente para el ejemplo recurrente del libro. Los drivers reales crecen más allá de eso y adquieren una docena o más de funcionalidades orientadas a la depuración con el tiempo. El patrón es siempre el mismo: hacer que la instrumentación sea barata cuando está desactivada, rica cuando está activada, y universalmente controlable por el operador. Con esa disciplina, un driver permanece diagnosticable a lo largo de toda su vida.

## Apéndice: referencia de laboratorio del capítulo 23

Este último apéndice reúne los laboratorios en una tabla con estimaciones de tiempo y dependencias para ayudar al lector a planificar su sesión:

| Lab. | Tema                                                   | T. est. | Dependencias                              |
|------|--------------------------------------------------------|---------|-------------------------------------------|
| 23.1 | Primera sesión con DDB                                 | 15 min  | Kernel de depuración con KDB/DDB          |
| 23.2 | Medición del driver con DTrace                         | 20 min  | KDTRACE_HOOKS, CTF, driver cargado        |
| 23.3 | ktrace del acceso al driver desde el usuario           | 15 min  | binario ktrace, driver cargado            |
| 23.4 | Introducción y corrección de una fuga de memoria       | 30 min  | Capacidad de recompilar el módulo         |
| 23.5 | Instalación del refactor 1.6-debug                     | 60 min  | Material de la sección 8, configuración de build |

Tiempo total de trabajo concentrado: aproximadamente dos horas y veinte minutos, más el tiempo necesario para asimilar el material. La intención del libro es que realices cada laboratorio a un ritmo que permita una comprensión real, sin precipitarse.

Seguimientos opcionales:

- **23.6 (opcional).** Combina `ktrace` y DTrace sobre el mismo programa de prueba y correlaciona el trazado del lado del usuario con los eventos del lado del driver.
- **23.7 (opcional).** Añade un ioctl de depuración (tal como se describe en el apéndice del toolkit de depuración) y escribe un pequeño programa de usuario que lo active.

Estas dos extensiones consolidan los temas del capítulo y desarrollan habilidades prácticas que todos los capítulos posteriores pondrán en juego.

## Apéndice: listado anotado completo de los cambios en myfirst_debug.h y myfirst_debug.c

Para consolidar la sección 8 en una única referencia, este apéndice reúne el texto completo de los archivos tal como quedan tras el refactor 1.6-debug, con comentarios en línea que explican cada bloque. El lector que prefiera ver el código terminado en un único lugar puede usar esta sección; el árbol de ejemplos en `examples/part-05/ch23-debug/stage3-refactor/` contiene el mismo texto como archivos descargables.

### myfirst_debug.h

```c
/*
 * myfirst_debug.h - debug and tracing infrastructure for the myfirst driver
 *
 * This header is included from the driver's source files. It provides:
 *   - a bitmask of debug verbosity classes
 *   - the DPRINTF macro for conditional device_printf
 *   - declarations for SDT probes that the driver fires at key points
 *
 * The matching SDT_PROVIDER_DEFINE and SDT_PROBE_DEFINE calls live in the
 * driver source, which owns the storage for the probe entries.
 */

#ifndef _MYFIRST_DEBUG_H_
#define _MYFIRST_DEBUG_H_

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sdt.h>

/*
 * Debug verbosity classes.  Each class is a single bit in sc->sc_debug.
 * The operator sets sysctl dev.myfirst.0.debug.mask to a combination of
 * these bits to enable the corresponding categories of output.
 *
 * Add new classes here when the driver grows new subsystems.  Use the
 * next unused bit and update DEBUG.md accordingly.
 */
#define MYF_DBG_INIT    0x00000001  /* probe/attach/detach */
#define MYF_DBG_OPEN    0x00000002  /* open/close lifecycle */
#define MYF_DBG_IO      0x00000004  /* read/write paths */
#define MYF_DBG_IOCTL   0x00000008  /* ioctl handling */
#define MYF_DBG_INTR    0x00000010  /* interrupt handler */
#define MYF_DBG_DMA     0x00000020  /* DMA mapping/sync */
#define MYF_DBG_PWR     0x00000040  /* power-management events */
#define MYF_DBG_MEM     0x00000080  /* malloc/free trace */
/* Bits 0x0100..0x8000 reserved for future driver subsystems */

#define MYF_DBG_ANY     0xFFFFFFFF
#define MYF_DBG_NONE    0x00000000

/*
 * DPRINTF - conditionally log a message via device_printf when the
 * given class bit is set in the softc's debug mask.
 *
 * Usage: DPRINTF(sc, MYF_DBG_OPEN, "open: pid=%d\n", pid);
 *
 * When the bit is clear, the cost is one load and one branch, which
 * is negligible in practice.  When the bit is set, the cost equals
 * a normal device_printf call.
 */
#ifdef _KERNEL
#define DPRINTF(sc, m, ...) do {                                        \
        if ((sc)->sc_debug & (m))                                        \
                device_printf((sc)->sc_dev, __VA_ARGS__);                \
} while (0)
#endif

/*
 * SDT probe declarations.  The matching SDT_PROBE_DEFINE calls are in
 * myfirst_debug.c (or in the main driver source if preferred).
 *
 * Probe argument conventions:
 *   open  (softc *, flags)            -- entry, before access check
 *   close (softc *, flags)            -- entry, before state update
 *   io    (softc *, is_write, resid, off) -- entry, into read or write
 */
SDT_PROVIDER_DECLARE(myfirst);
SDT_PROBE_DECLARE(myfirst, , , open);
SDT_PROBE_DECLARE(myfirst, , , close);
SDT_PROBE_DECLARE(myfirst, , , io);

#endif /* _MYFIRST_DEBUG_H_ */
```

El archivo de cabecera tiene tres secciones: la máscara de bits de clase, la macro DPRINTF y las declaraciones de las sondas SDT. Cada una es pequeña, autocontenida y está diseñada para crecer a medida que el driver crece.

### Las definiciones SDT en myfirst_debug.c

```c
/*
 * myfirst_debug.c - storage for the SDT probe entries.
 *
 * This file exists to hold the SDT_PROVIDER_DEFINE and SDT_PROBE_DEFINE
 * declarations.  By convention in the myfirst driver, these live in a
 * dedicated source file to keep the main driver uncluttered.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sdt.h>
#include "myfirst_debug.h"

/*
 * The provider "myfirst" exposes all of our static probes to DTrace.
 * Scripts select probes with "myfirst:::<name>".
 */
SDT_PROVIDER_DEFINE(myfirst);

/*
 * open: fired on every successful or attempted device open.
 *   arg0 = struct myfirst_softc *
 *   arg1 = int flags from the open call
 */
SDT_PROBE_DEFINE2(myfirst, , , open,
    "struct myfirst_softc *", "int");

/*
 * close: fired on every device close.
 *   arg0 = struct myfirst_softc *
 *   arg1 = int flags
 */
SDT_PROBE_DEFINE2(myfirst, , , close,
    "struct myfirst_softc *", "int");

/*
 * io: fired on every read or write call, at function entry.
 *   arg0 = struct myfirst_softc *
 *   arg1 = int is_write (0 for read, 1 for write)
 *   arg2 = size_t resid (bytes requested)
 *   arg3 = off_t offset
 */
SDT_PROBE_DEFINE4(myfirst, , , io,
    "struct myfirst_softc *", "int", "size_t", "off_t");
```

El archivo es breve de forma intencionada. Su único propósito es contener el almacenamiento de las entradas de sondas. Si el driver incorpora más sondas, las añades aquí.

### Disparo de las sondas en el driver

Dentro de `myfirst.c` (o del archivo que implemente cada método), las sondas se disparan en los puntos de llamada apropiados:

```c
/*
 * myfirst_open: device open method.
 * Called when a user process opens /dev/myfirst0.
 */
static int
myfirst_open(struct cdev *dev, int flags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error = 0;

        DPRINTF(sc, MYF_DBG_OPEN, "open: pid=%d uid=%d flags=0x%x\n",
            td->td_proc->p_pid, td->td_ucred->cr_uid, flags);

        /* Fire the SDT probe at entry, before any state change. */
        SDT_PROBE2(myfirst, , , open, sc, flags);

        mtx_lock(&sc->sc_mtx);
        if (sc->sc_open_count >= MYFIRST_MAX_OPENS) {
                error = EBUSY;
                goto out;
        }
        sc->sc_open_count++;
out:
        mtx_unlock(&sc->sc_mtx);

        if (error != 0)
                DPRINTF(sc, MYF_DBG_OPEN, "open failed: error=%d\n", error);
        else
                DPRINTF(sc, MYF_DBG_OPEN, "open ok: count=%d\n",
                    sc->sc_open_count);

        return (error);
}

/*
 * myfirst_close: device close method.
 * Called when the last reference to the device is released.
 */
static int
myfirst_close(struct cdev *dev, int flags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;

        DPRINTF(sc, MYF_DBG_OPEN, "close: pid=%d flags=0x%x\n",
            td->td_proc->p_pid, flags);

        SDT_PROBE2(myfirst, , , close, sc, flags);

        mtx_lock(&sc->sc_mtx);
        if (sc->sc_open_count > 0)
                sc->sc_open_count--;
        DPRINTF(sc, MYF_DBG_OPEN, "close ok: count=%d\n", sc->sc_open_count);
        mtx_unlock(&sc->sc_mtx);

        return (0);
}

/*
 * myfirst_read: device read method.
 */
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error;

        DPRINTF(sc, MYF_DBG_IO, "read: pid=%d resid=%zu off=%jd\n",
            curthread->td_proc->p_pid,
            (size_t)uio->uio_resid, (intmax_t)uio->uio_offset);

        SDT_PROBE4(myfirst, , , io, sc, 0,
            (size_t)uio->uio_resid, uio->uio_offset);

        error = myfirst_read_impl(sc, uio);

        if (error != 0)
                DPRINTF(sc, MYF_DBG_IO, "read failed: error=%d\n", error);
        return (error);
}

/*
 * myfirst_write: device write method.
 */
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error;

        DPRINTF(sc, MYF_DBG_IO, "write: pid=%d resid=%zu off=%jd\n",
            curthread->td_proc->p_pid,
            (size_t)uio->uio_resid, (intmax_t)uio->uio_offset);

        SDT_PROBE4(myfirst, , , io, sc, 1,
            (size_t)uio->uio_resid, uio->uio_offset);

        error = myfirst_write_impl(sc, uio);

        if (error != 0)
                DPRINTF(sc, MYF_DBG_IO, "write failed: error=%d\n", error);
        return (error);
}
```

Cada método sigue el patrón entrada-salida-error:

1. Un `DPRINTF` en la entrada, que muestra quién llamó y con qué argumentos.
2. Un `SDT_PROBE` tras el registro de entrada, antes de que comience el trabajo real.
3. Un `DPRINTF` en el camino de error ante cualquier retorno distinto de cero.
4. Un `DPRINTF` en el camino de éxito que muestra el resultado cuando resulta útil.

### Registro del sysctl en myfirst_attach

El sysctl que controla `sc_debug` se registra durante el attach del dispositivo:

```c
/*
 * Build the debug sysctl tree:  dev.myfirst.N.debug.*
 */
sysctl_ctx_init(&sc->sc_sysctl_ctx);

sc->sc_sysctl_tree = SYSCTL_ADD_NODE(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->sc_dev)),
    OID_AUTO, "debug",
    CTLFLAG_RW, 0, "debug and tracing controls");

SYSCTL_ADD_U32(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(sc->sc_sysctl_tree),
    OID_AUTO, "mask",
    CTLFLAG_RW, &sc->sc_debug, 0,
    "debug class bitmask (see myfirst_debug.h for class definitions)");
```

El camino de detach desmonta el árbol de sysctl:

```c
sysctl_ctx_free(&sc->sc_sysctl_ctx);
```

### La actualización de versión

Al inicio del código fuente del driver, se actualizan la cadena de versión y MODULE_VERSION:

```c
static const char myfirst_version[] = "myfirst 1.6-debug";

/* ...driver methods, attach, detach, etc... */

MODULE_VERSION(myfirst, 16);
```

Y cerca del inicio de `myfirst_attach`, se registra la versión:

```c
DPRINTF(sc, MYF_DBG_INIT, "attach: %s loaded\n", myfirst_version);
```

Con la máscara configurada para incluir `MYF_DBG_INIT`, el operador ve una línea clara de informe de versión en cada attach.

### Un ejemplo de dmesg con todas las clases activadas

Cuando todas las clases están activadas y el dispositivo se ejercita, `dmesg` muestra un registro completo:

```text
myfirst0: attach: myfirst 1.6-debug loaded
myfirst0: open: pid=1234 uid=1001 flags=0x0
myfirst0: open ok: count=1
myfirst0: read: pid=1234 resid=4096 off=0
myfirst0: close: pid=1234 flags=0x0
myfirst0: close ok: count=0
```

Cada evento que importa al driver es visible, etiquetado con el nombre del dispositivo, el PID y los argumentos relevantes. Un ingeniero de campo que reciba este registro puede reconstruir la secuencia de eventos sin necesitar acceso al código fuente.

Con todas las clases desactivadas, `dmesg` muestra únicamente la línea de attach (si `MYF_DBG_INIT` está activado) o nada (si la máscara es 0). El driver funciona a plena velocidad sin ningún coste de observabilidad.

### Una sesión de ejemplo de DTrace con las sondas

Las sondas SDT permiten un análisis legible por máquina:

```sh
$ sudo dtrace -n 'myfirst::: { @[probename] = count(); } tick-10s { exit(0); }'
dtrace: description 'myfirst::: ' matched 3 probes
CPU     ID                    FUNCTION:NAME
  0  49012                        :tick-10s

  close                                                     43
  open                                                      43
  io                                                       120
```

En los 10 segundos de muestreo, el driver procesó 43 ciclos de apertura y cierre y 120 eventos I/O. El agregado se genera sin ninguna modificación al driver y con un coste en tiempo de ejecución prácticamente nulo, porque la ausencia de un script adjunto hace que las sondas sean inertes.

### Resumen del refactor

El refactor 1.6-debug añade aproximadamente 100 líneas de código distribuidas de la siguiente manera:

- `myfirst_debug.h`: 50 líneas (clases, DPRINTF, declaraciones SDT)
- `myfirst_debug.c`: 30 líneas (proveedor SDT y definiciones de sondas)
- Cambios en el código fuente del driver: 20 líneas (registro de sysctl, llamadas a DPRINTF, disparos de sondas)

La inversión es pequeña; el retorno es enorme. Todos los capítulos posteriores del libro heredan esta infraestructura sin coste adicional, y cada problema de campo futuro del driver dispone de un camino diagnóstico claro.

## Apéndice: qué hace el kernel cuando un driver genera un pánico

El lector que ha seguido el capítulo hasta aquí comprende bien las herramientas de depuración. Este último apéndice trata lo que ocurre cuando algo falla de forma catastrófica: un pánico. El comportamiento del kernel durante un pánico no es algo optativo para el autor de un driver, porque cualquier error lo suficientemente grave como para provocar un pánico produce una secuencia específica de eventos ante la que el driver debe comportarse correctamente.

### La secuencia de pánico

Cuando cualquier código del kernel llama a `panic()`, ocurre lo siguiente, en este orden:

1. El mensaje de pánico se imprime en la consola, normalmente mediante `printf`.
2. La CPU eleva su prioridad al máximo, deteniendo la mayor parte de la actividad ordinaria del kernel.
3. Si el kernel fue compilado con `options KDB` y `kdb_unattended` no está activado, el control pasa al depurador del kernel. El operador ve el prompt `db>`.
4. Si el operador (o `kdb_unattended=1`) continúa el pánico, el kernel inicia un volcado de fallo. El volcado escribe la imagen actual de la memoria en el dispositivo de volcado de fallos (normalmente el swap).
5. El kernel se reinicia (a menos que `panic_reboot_wait_time` esté configurado).

En el siguiente arranque, `savecore(8)` extrae el volcado del dispositivo de swap, lo escribe en `/var/crash/vmcore.N` y renumera el contador. A continuación, puedes ejecutar `kgdb` contra el volcado para inspeccionar el estado post mortem.

### Responsabilidades del driver

La ruta de pánico impone requisitos específicos a los drivers:

1. **No provocar un pánico durante detach.** Un driver que provoca un pánico durante `detach` impide el cierre limpio del resto del kernel. Toda ruta de detach debe estar libre de pánicos.

2. **No asignar memoria en el manejador de pánico.** Si el driver registra un shutdown hook que se ejecuta durante el pánico, ese hook no debe llamar a `malloc` ni a ninguna función que pueda dormir.

3. **No depender del espacio de usuario durante el pánico.** Los programas de usuario quedan congelados durante el pánico; el driver no puede enviarles mensajes.

4. **No esperar interrupciones durante el pánico.** Las interrupciones pueden estar desactivadas durante la ruta de pánico; un driver que espere una se colgará.

Estas reglas se aplican a los shutdown hooks y a las escasas rutas que el kernel recorre durante el pánico. En condiciones normales de operación, los drivers no se ejecutan en la ruta de pánico, pero ciertos callbacks sí lo hacen.

### Lectura de un volcado de memoria

El crash dump es una imagen completa de la memoria del kernel en el momento del pánico. `kgdb` lo abre con información simbólica completa si el kernel tiene información de depuración:

```sh
sudo kgdb /boot/kernel/kernel /var/crash/vmcore.0
```

Dentro de `kgdb`, los comandos más útiles son:

```console
(kgdb) bt            # backtrace of the thread that panicked
(kgdb) info threads  # list all threads
(kgdb) thread N      # switch to thread N
(kgdb) frame N       # show frame N (in the current backtrace)
(kgdb) list          # show source at the current frame
(kgdb) info locals   # show local variables
(kgdb) print var     # show a variable
(kgdb) print *sc     # show the softc pointer contents
(kgdb) ptype struct myfirst_softc  # show the structure type
```

Pasos habituales de investigación:

1. `bt` para ver qué provocó el pánico.
2. `list` para ver la línea de código fuente.
3. `info locals` para ver el estado local.
4. `print` para los campos de interés.
5. `thread apply all bt` para ver todos los threads, por si el pánico lo desencadenó la interacción de un thread con otro.

### Combinación de símbolos del kernel y del driver

Si el driver se compila como módulo, `kgdb` necesita cargar también sus símbolos:

```console
(kgdb) add-symbol-file /boot/kernel/myfirst.ko <address>
```

`<address>` es la dirección de carga del módulo, que `kldstat -v` muestra en el sistema en ejecución, o que aparece en la salida del pánico. Una vez cargados, `kgdb` puede decodificar los frames dentro del módulo con visibilidad completa del código fuente.

### Tamaño del crash dump

Un crash dump completo tiene el mismo tamaño que el uso de memoria del kernel en el momento del pánico, normalmente varios gigabytes. El dispositivo de volcado (habitualmente swap) debe tener espacio suficiente. Los sistemas optimizados para una recuperación rápida tras el pánico suelen usar un dispositivo de volcado dedicado, no el swap, para evitar las limitaciones del espacio de swap.

Para depurar únicamente drivers, un mini dump captura solo la memoria propia del kernel, no la memoria de usuario. Establece `dumpdev=none` o `dumpdev=mini` en `loader.conf` para ajustar este comportamiento. Los mini dumps son pequeños (decenas de megabytes) y se cargan rápidamente en `kgdb`.

### Resumen

La ruta de pánico es poco frecuente en producción, pero es esencial comprenderla. Un driver que gestiona sus errores correctamente rara vez provoca un pánico; un driver que sí lo provoca genera un crash dump que su autor puede analizar con `kgdb`. La combinación de `INVARIANTS`, `WITNESS`, `DEBUG_MEMGUARD` y un pipeline de crash dump funcional convierte los pánicos en eventos de diagnóstico valiosos en lugar de catástrofes.

## Apéndice: el Capítulo 23 de un vistazo

Este resumen de una sola página sirve como repaso que puedes tener a mano mientras trabajas. Cada entrada nombra un concepto, indica dónde se introdujo y proporciona el comando o código mínimo para ponerlo en práctica.

### Herramientas en orden de primera aparición

1. **`printf` / `device_printf`** (Sección 2): Registro básico del kernel. Usa `device_printf(dev, "msg\n")` dentro de los métodos del driver.
2. **`log(LOG_PRIORITY, ...)`** (Sección 2): Registro enrutado a syslog, con un nivel de prioridad. Úsalo para mensajes que correspondan a `/var/log/messages`.
3. **`dmesg`** (Sección 3): Lee el buffer de mensajes del kernel. Combínalo con `grep` y `tail` para obtener salida filtrada.
4. **`/var/log/messages`** (Sección 3): Historial de registros persistente. `syslogd(8)` escribe en él; `newsyslog(8)` lo rota.
5. **Kernel de depuración** (Sección 4): Reconstruye con `KDB`, `DDB`, `INVARIANTS`, `WITNESS`, `KDTRACE_HOOKS` y `DEBUG=-g`. Más lento, pero con capacidad de diagnóstico.
6. **`ddb`** (Sección 4): Depurador interactivo del kernel. Se accede mediante `sysctl debug.kdb.enter=1` o una interrupción en la consola. Usa `bt`, `show locks`, `ps`, `continue`.
7. **`kgdb`** (Sección 4): Depurador post-mortem. Abre `/boot/kernel/kernel` y `/var/crash/vmcore.N` juntos.
8. **DTrace** (Sección 5): Trazado y medición en vivo. `dtrace -n 'fbt::myfirst_*:entry { @[probefunc] = count(); }'`.
9. **Sondas SDT** (Sección 5, Sección 8): Puntos de trazado estáticos en el código fuente del driver. `dtrace -n 'myfirst::: { @[probename] = count(); }'`.
10. **`ktrace`/`kdump`** (Sección 6): Trazado de syscalls en el espacio de usuario. `sudo ktrace -di -f trace.out ./program; kdump -f trace.out`.
11. **`vmstat -m`** (Sección 7.1): Vista de la memoria del kernel por pool. Se usa para detectar fugas de memoria.

### Reflejos que conviene desarrollar

- Añade un `device_printf` antes de que sospeches de un bug, no después.
- Consulta `dmesg` antes de hacer suposiciones sobre el comportamiento del driver.
- Reconstruye con `INVARIANTS` y `WITNESS` durante el desarrollo.
- Escribe one-liners de DTrace en lugar de ciclos de compilación y recarga cuando la pregunta es «¿se ejecuta esto?».
- Mantén la estabilidad de `vmstat -m` como invariante continuo, no como comprobación a posteriori.
- Nunca mantengas un sleep lock activo durante `uiomove` ni durante ninguna llamada que pueda dormir.
- Mantén los manejadores de interrupciones cortos. Delega el trabajo a `taskqueue`.
- Empareja cada `malloc` con un `free`. Comprueba el emparejamiento con las herramientas.

### La refactorización 1.6-debug

- Añade `myfirst_debug.h` con 8 clases de verbosidad y la macro DPRINTF.
- Añade un `sysctl dev.myfirst.0.debug.mask` para control en tiempo de ejecución.
- Añade tres sondas SDT: `myfirst:::open`, `myfirst:::close`, `myfirst:::io`.
- Sigue el patrón entrada/salida/error en cada método.
- Incrementa `myfirst_version` a `1.6-debug` y `MODULE_VERSION` a 16.

### Próximos pasos recomendados

- Completa los cinco laboratorios si el tiempo lo permite.
- Intenta el Desafío 23.2, el script personalizado de DTrace.
- Escribe una entrada en tu cuaderno de laboratorio personal para este capítulo.
- Lee `/usr/src/sys/dev/ath/if_ath_debug.h` como referencia del mundo real.
- Antes de comenzar el Capítulo 24, confirma que el driver está en la versión `1.6-debug` y que la máscara de depuración responde correctamente.

Este es el final del material de referencia del capítulo. Las secciones «Cerrando el capítulo» y de puente que siguen preparan al lector para el Capítulo 24.

## Cerrando el capítulo

El Capítulo 23 ha presentado los métodos de trabajo y las herramientas de depuración del kernel de FreeBSD. La estructura del capítulo ha seguido un arco deliberado: explicar por qué la depuración del kernel es diferente, enseñar las herramientas básicas de registro e inspección, construir un kernel de depuración, dominar DTrace y ktrace, reconocer las clases de bugs más comunes y, por último, refactorizar el driver `myfirst` para que admita su propia observabilidad.

A lo largo del camino, el capítulo ha ganado su lugar en el libro. Se sitúa entre el trabajo arquitectónico de la Parte 4 y el trabajo de integración de la Parte 5 porque los capítulos restantes añadirán código sustancialmente nuevo a `myfirst` (hooks de devfs, ioctl, sysctl, interfaces de red, interfaces GEOM, soporte USB, soporte virtio) y cada uno de esos capítulos depende de la capacidad de ver lo que hace el driver y de corregir los problemas a medida que surgen. Las herramientas y la instrumentación no son un añadido posterior; son lo que hace que el resto del libro sea manejable.

El driver en sí está ahora en la versión `1.6-debug`. Su softc tiene una máscara `sc_debug`; el árbol dispone de un subárbol `sysctl dev.myfirst.0.debug`; la macro DPRINTF condiciona el registro por clase; y tres sondas SDT exponen los eventos de apertura, cierre y E/S. Cada capítulo posterior de las Partes 5, 6 y 7 ampliará el mismo marco: aparecerán nuevas clases en `myfirst_debug.h` y se declararán nuevas sondas a medida que el driver adquiera nuevos límites funcionales.

### Lo que el lector ya es capaz de hacer

- Usar `device_printf`, `log` y el patrón de macro `DPRINTF` para registrar eventos del kernel de forma estructurada.
- Leer `dmesg` y `/var/log/messages` para localizar los mensajes del driver y correlacionarlos con los eventos.
- Construir un kernel de depuración con `KDB`, `DDB`, `INVARIANTS`, `WITNESS` y `KDTRACE_HOOKS`.
- Escribir one-liners de DTrace que cuenten, agreguen y midan el tiempo de los eventos del kernel.
- Usar `ktrace` y `kdump` para observar las interacciones del espacio de usuario con el driver.
- Reconocer los bugs de driver más comunes y asociarlos con la herramienta de depuración adecuada.
- Declarar sondas SDT y exponerlas a los scripts de DTrace.
- Incrementar la versión del driver y mantener la infraestructura de depuración.

### Lo que viene a continuación

Con la infraestructura de depuración en su lugar, el Capítulo 24 (Integración con el kernel: devfs, ioctl y sysctl) ampliará la interfaz del driver con el espacio de usuario. El lector creará entradas `cdev` más ricas, definirá ioctls personalizados para configurar el dispositivo y registrará nodos sysctl para los parámetros en tiempo de ejecución del driver. Todos esos añadidos pueden ahora instrumentarse y trazarse con el marco de este capítulo, de modo que el trabajo estará sustentado en un comportamiento visible y depurable desde la primera línea.

El arsenal de depuración construido en el Capítulo 23 tendrá un uso intenso a partir de aquí. En el Capítulo 24 el lector añadirá su primer ioctl real, y la primera vez que llegue un argumento con el valor equivocado activará `MYF_DBG_IOCTL`, leerá el registro y encontrará el error en segundos en lugar de horas. En el Capítulo 25 (Escritura de drivers de caracteres en profundidad), el lector usará DTrace para confirmar que la ruta select/poll del driver se activa correctamente. En el Capítulo 26 y en los siguientes, cada nuevo hook de subsistema quedará registrado a través de DPRINTF y sondado con SDT. El retorno de la inversión del capítulo de depuración es largo y constante.

Una reflexión final: las herramientas de este capítulo no son glamurosas. No generan drivers funcionales por sí solas. Pero son lo que convierte un bug frustrante de seis horas en un diagnóstico preciso de veinte minutos. Todo desarrollador experimentado de FreeBSD ha trazado problemas, al menos en una docena de ocasiones, a través de `vmstat -m`, luego DTrace, luego el código fuente, y ha salido con una corrección precisa. El lector que domina el conjunto de herramientas de este capítulo se incorpora a ese grupo y ya está preparado para el trabajo de integración más rico que viene a continuación.

## Puente hacia el Capítulo 24

El Capítulo 24 (Integración con el kernel: devfs, ioctl y sysctl) abre el siguiente arco del libro. Hasta el Capítulo 23 el driver ha sido un único nodo `cdev` con comportamiento fijo. El Capítulo 24 hace que la interfaz sea expresiva: el lector ampliará el `cdev` en entradas de devfs más ricas, definirá ioctls personalizados para configurar el dispositivo y registrará nodos sysctl para los parámetros en tiempo de ejecución.

El puente es directo. La máscara de depuración introducida en la Sección 8 es un sysctl. Los ioctls del Capítulo 24 se trazarán a través de `MYF_DBG_IOCTL`. Los nodos de devfs personalizados registrarán su creación y destrucción a través de `MYF_DBG_INIT`. Cuando el lector construya nuevas funcionalidades en el Capítulo 24, el marco de depuración estará listo para observarlas.

Nos vemos en el Capítulo 24.

## Referencia: tarjeta de consulta rápida del Capítulo 23

La tabla siguiente resume las herramientas, comandos y patrones introducidos en el Capítulo 23 para consulta rápida.

### Registro

| Llamada                              | Uso                                                                        |
|--------------------------------------|----------------------------------------------------------------------------|
| `printf("...")`                      | Registro muy básico; sin identidad de dispositivo.                         |
| `device_printf(dev, "...")`          | Registro estándar del driver; añade el nombre del dispositivo como prefijo.|
| `log(LOG_PRI, "...")`                | Syslog con prioridad; enviado a `/var/log/messages`.                       |
| `DPRINTF(sc, CLASS, "...")`          | Condicional según `sc->sc_debug & CLASS`; macro estándar del driver.       |

### Niveles de prioridad de syslog

| Prioridad     | Significado                                                      |
|---------------|------------------------------------------------------------------|
| `LOG_EMERG`   | El sistema no puede funcionar.                                   |
| `LOG_ALERT`   | Se debe actuar de inmediato.                                     |
| `LOG_CRIT`    | Condiciones críticas.                                            |
| `LOG_ERR`     | Condiciones de error.                                            |
| `LOG_WARNING` | Condiciones de aviso.                                            |
| `LOG_NOTICE`  | Condición normal pero significativa.                             |
| `LOG_INFO`    | Informativo.                                                     |
| `LOG_DEBUG`   | Mensajes de nivel de depuración.                                 |

### Opciones del kernel para depuración

| Opción                       | Efecto                                                                          |
|------------------------------|---------------------------------------------------------------------------------|
| `options KDB`                | Incluye el marco del depurador del kernel.                                      |
| `options DDB`                | UI interactiva del depurador en la consola.                                     |
| `options DDB_CTF`            | Soporte de CTF en DDB para impresión con conocimiento de tipos.                 |
| `options KDB_TRACE`          | Backtrace automático al entrar en KDB.                                          |
| `options KDB_UNATTENDED`     | Los panics provocan un reinicio en lugar de entrar en DDB.                      |
| `options INVARIANTS`         | Habilita `KASSERT`, `MPASS` y otras aserciones del kernel.                      |
| `options WITNESS`            | Rastrea el orden de los locks y genera un panic ante violaciones.               |
| `options WITNESS_KDB`        | Entra en KDB ante una violación detectada por WITNESS.                          |
| `options DEBUG_MEMGUARD`     | Envenena la memoria liberada; detecta accesos use-after-free.                   |
| `options KDTRACE_HOOKS`      | Habilita las sondas de DTrace.                                                  |
| `options KDTRACE_FRAME`      | Genera punteros de frame para los recorridos de pila de DTrace.                 |
| `makeoptions WITH_CTF=1`     | Genera CTF en el kernel y en los módulos.                                       |
| `makeoptions DEBUG=-g`       | Incluye información DWARF completa en el kernel y en los módulos.               |

### DTrace one-liners habituales

```sh
# Count every function entry in the driver
dtrace -n 'fbt::myfirst_*:entry { @[probefunc] = count(); }'

# Measure the time spent in a specific function
dtrace -n 'fbt::myfirst_read:entry { self->t = timestamp; }
           fbt::myfirst_read:return /self->t/ {
               @ = quantize(timestamp - self->t); self->t = 0; }'

# Count SDT probes fired by the driver
dtrace -n 'myfirst::: { @[probename] = count(); }'

# Show syscall frequency for a named process
dtrace -n 'syscall:::entry /execname == "myftest"/ { @[probefunc] = count(); }'

# Aggregate I/O sizes through the driver
dtrace -n 'myfirst:::io { @ = quantize(arg2); }'
```

### Flujo de trabajo con ktrace / kdump

```sh
# Record a process under ktrace
sudo ktrace -di -f trace.out ./myprogram

# Dump in human-readable form
kdump -f trace.out

# Filter for syscalls only
kdump -f trace.out | grep -E 'CALL|RET|NAMI'
```

### Lista de verificación de depuración (abreviada)

1. ¿Está `dmesg` limpio? Si no es así, lee el último mensaje anterior al problema.
2. ¿Es estable `vmstat -m` para el pool del driver? Si no, busca una fuga de memoria.
3. ¿Genera `WITNESS` un pánico con la carga de trabajo? Si es así, el orden de los locks es incorrecto.
4. ¿Coinciden los conteos de `fbt::myfirst_*:entry` con la actividad esperada? Si no, revisa las rutas de ejecución.
5. ¿Muestra el `ktrace` en el lado del usuario la secuencia de syscalls esperada? Si no, comprueba la configuración del ioctl o del comando.
6. ¿Está el driver en la versión esperada? Ejecuta `kldstat -v | grep myfirst` y confirma `MODULE_VERSION`.

## Referencia: Glosario de términos del capítulo 23

**CTF**: Compact C Type Format. Formato de información de tipos del kernel que DDB y DTrace utilizan para mostrar estructuras tipadas. Requiere `makeoptions WITH_CTF=1`.

**DDB**: El depurador de kernel integrado en FreeBSD. Interactivo, basado en consola. Se activa con `options DDB`.

**DDB_CTF**: Una opción de DDB que permite al depurador mostrar valores tipados usando información de tipos CTF.

**device_printf**: La función estándar de FreeBSD para registrar mensajes de driver. Añade como prefijo el nombre del dispositivo a la salida.

**DPRINTF**: El macro convencional de driver para registro condicional, controlado por una máscara de verbosidad.

**DTrace**: Un framework de trazado dinámico. Utiliza los proveedores `fbt`, `syscall`, `sdt`, `io`, `sched`, `lockstat` y otros.

**fbt**: Function Boundary Tracing. Un proveedor de DTrace que registra la entrada y el retorno de cada función del kernel.

**INVARIANTS**: Una opción de compilación del kernel que activa `KASSERT`, `MPASS` y otras aserciones relacionadas. Estándar en builds de depuración.

**KASSERT**: Un macro de aserción que se evalúa únicamente cuando `INVARIANTS` está activo. Genera un pánico si la condición es falsa.

**KDB**: El framework del depurador del kernel. `DDB` es uno de sus backends.

**KDTRACE_HOOKS**: La opción de compilación del kernel que habilita las sondas de DTrace.

**kdump**: La herramienta en espacio de usuario que lee los archivos de salida de `ktrace` y los muestra en formato legible.

**ktrace**: La herramienta en espacio de usuario que registra syscalls y otros eventos de un proceso determinado.

**log**: La función del kernel para enviar mensajes a syslog con un nivel de prioridad.

**MPASS**: Similar a `KASSERT`, pero con una comprobación solo en tiempo de compilación. Sin coste alguno en builds de producción.

**myfirst_debug.h**: El archivo de cabecera de depuración introducido en la sección 8, que declara las clases de verbosidad y las estructuras de sondas SDT.

**sc_debug**: El campo `uint32_t` en el softc del driver que controla qué categorías de `DPRINTF` están activas.

**SDT**: Statically Defined Tracing. Sondas definidas en tiempo de compilación a las que DTrace puede conectarse.

**SDT_PROBE_DEFINE**: Familia de macros que registra una nueva sonda SDT en el kernel o en el driver.

**syslog**: El subsistema de registro de BSD; las prioridades de los mensajes se asignan a destinos según `/etc/syslog.conf`.

**vmstat -m**: Una herramienta en espacio de usuario que muestra estadísticas de memoria del kernel por pool.

**WITNESS**: Un sistema de verificación del orden de locks del kernel. Genera un pánico ante violaciones del orden de locks cuando `options WITNESS` está activo.

**1.6-debug**: La versión del driver tras la sección 8. Incorpora la máscara de verbosidad, el sysctl, el patrón DPRINTF y las sondas SDT.
