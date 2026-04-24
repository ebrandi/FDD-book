---
title: "Ajuste del rendimiento y profiling"
description: "Optimización del rendimiento de drivers mediante profiling y ajuste"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 33
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 225
language: "es-ES"
---
# Ajuste de rendimiento y profiling

## Introducción

El capítulo 32 terminó con un driver que se enlaza a una placa embebida, lee las asignaciones de pines desde un Device Tree, activa y desactiva un LED, y se desconecta limpiamente. Ese capítulo respondía a una pregunta: *¿funciona el driver?* El presente capítulo responde a una distinta: *¿funciona bien?* Las dos preguntas suenan parecidas y son profundamente distintas.

Un driver que funciona es aquel cuyo camino de código produce el resultado esperado para cada entrada válida. Un driver que funciona bien es aquel que produce ese resultado con el throughput adecuado, la latencia adecuada, el coste de CPU adecuado y la huella de memoria adecuada para la máquina en la que se ejecuta. Puedes escribir un driver que supere todas las pruebas funcionales y aun así deje el sistema lento, incumpla plazos, descarte paquetes o sobrecaliente una placa pequeña. En el desarrollo del kernel, la corrección es el suelo. El rendimiento es lo que está por encima.

En este capítulo vamos a construir los hábitos, las herramientas y el modelo mental que convierten una observación como *el sistema se siente lento* en un diagnóstico como *el camino de escritura del driver adquiere un mutex en disputa dos veces por byte, lo que en esta carga de trabajo añade cuarenta microsegundos por llamada*. El diagnóstico, no la solución, es la parte difícil. La solución suele ser pequeña. Lo que requiere experiencia es la disciplina para resistir la tentación de adivinar y la habilidad para medir la cosa correcta.

Hay un chiste entre los ingenieros de rendimiento que dice que toda optimización de principiante comienza con las palabras *Pensé que sería más rápido si...*. Esa frase es la fuente más fiable de regresiones en el código del kernel. Una distribución favorable para la caché en abstracto puede convertirse en una distribución hostil para la caché en un procesador concreto. Una cola sin lock puede ser más lenta que una protegida por mutex con poca contención. Una ventana de coalescencia de interrupciones cuidadosamente ajustada puede arruinar la latencia para una pequeña clase de cargas de trabajo que no tenías en mente. El único antídoto a *Pensé que sería más rápido* es *Medí que es más rápido*, y el objetivo de este capítulo es darte un conjunto de herramientas con el que *lo medí* se convierta en un hábito.

FreeBSD te ofrece un conjunto de herramientas de medición inusualmente rico. DTrace, introducido originalmente en el árbol desde Solaris, te permite sondear casi cualquier función del kernel sin recompilar nada. El subsistema `hwpmc(4)` y la herramienta de userland `pmcstat(8)` exponen los contadores de rendimiento hardware del CPU: ciclos, instrucciones, fallos de caché, predicciones erróneas de salto y paradas. La interfaz `sysctl(9)` y el framework `counter(9)` te proporcionan contadores baratos, seguros y siempre activos que puedes dejar en producción. La herramienta de registro del kernel `log(9)` te ofrece un canal con límite de tasa para mensajes informativos. El subsistema `ktrace(1)` rastrea los límites de las llamadas al sistema. Las herramientas `top(1)` y `systat(1)` te permiten ver en conjunto dónde va el tiempo de CPU. Cada una de estas herramientas tiene un punto fuerte particular, y el capítulo dedicará tiempo a mostrarte qué herramienta usar en cada situación.

También vamos a dedicar tiempo a los *hábitos* de medición, no solo a las herramientas. Es fácil hacer mal la medición. Un driver muy instrumentado puede no comportarse como la versión sin instrumentar; las sondas modifican la temporización. Un contador que se escribe desde todos los CPUs en una única línea de caché puede convertirse en el cuello de botella que el driver intenta medir. Un script de DTrace que imprime en cada sonda se convierte en un ataque de denegación de servicio contra `dmesg`. Un sysctl que lee memoria no alineada puede parecer rápido porque el compilador lo inserta en línea de forma silenciosa en x86-64 y lento en arm64. Las herramientas que usas para medir *afectan* a lo que mides, y necesitas el hábito de recordarlo mientras trabajas.

El capítulo tiene un arco claro. Comenzamos en la Sección 1 con qué es el rendimiento, por qué importa, cuándo vale la pena perseguirlo y cómo establecer objetivos medibles. La Sección 2 introduce las primitivas de temporización y medición del kernel: las diferentes llamadas `nano*` y `bintime`, el compromiso entre precisión y coste entre ellas, y los hábitos para contar operaciones y medirlas sin alterar el camino de código que intentas observar. La Sección 3 es el capítulo dentro del capítulo sobre DTrace: analizamos los providers más útiles para los autores de drivers, la forma de un one-liner útil, la estructura de un script más largo y el provider `lockstat`, que se especializa en la contención de locks. La Sección 4 introduce los PMC de hardware, explica qué miden realmente los ciclos, los fallos de caché y las ramas, y recorre una sesión de `pmcstat(8)` desde la recopilación de muestras hasta una vista estilo flame graph de las funciones más activas de un driver. La Sección 5 se vuelve hacia el interior, hacia el driver: alineación de líneas de caché, preasignación de buffers, zonas UMA, contadores por CPU mediante `DPCPU_DEFINE(9)` y `counter(9)`. La Sección 6 analiza el manejo de interrupciones y el uso de taskqueue, porque las interrupciones son a menudo donde se encuentra el primer precipicio de rendimiento. La Sección 7 cubre las métricas de tiempo de ejecución de calidad de producción que dejas en el driver una vez terminado el ajuste: árboles sysctl, registro con límite de tasa y los tipos de modos de depuración que pertenecen a un driver publicado. La Sección 8 cierra el ciclo. Te enseña a eliminar la instrumentación temporal que ayudó durante el ajuste, documentar lo que mediste y publicar el driver optimizado como una versión en la que el resto del sistema pueda confiar.

Tras la Sección 8 encontrarás laboratorios prácticos, ejercicios de desafío, una referencia de resolución de problemas, una sección de cierre y un puente hacia el Capítulo 34.

Una nota práctica antes de comenzar. El trabajo de rendimiento en un kernel es más fácil de lo que era en los años noventa y más difícil de lo que parece hoy. El problema de los años noventa era que las herramientas eran pocas y la documentación escasa. El problema moderno es el contrario: las herramientas son muchas, cada una es capaz, y es fácil usar la equivocada o perseguir una métrica que no se corresponde con la carga de trabajo. Elige la herramienta más sencilla que responda a tu pregunta. Un one-liner de DTrace que te muestra que una función se ejecuta el doble de veces de lo esperado vale más que una sesión de seis horas de `pmcstat` que produce un flame graph de ruido. La habilidad más valiosa es el criterio para elegir la pregunta, no la virtuosidad con ninguna herramienta en particular.

Comencemos.

## Orientación para el lector: cómo usar este capítulo

El capítulo 33 ocupa un lugar particular en el arco del libro. Los capítulos anteriores te enseñaron a construir drivers que hacen su trabajo, sobreviven a entradas incorrectas, se integran con los frameworks del kernel y se ejecutan en plataformas diversas. Este capítulo te enseña a mirar esos drivers con instrumentos de medición y decidir si están haciendo su trabajo *bien*. La orientación es hacia dentro, hacia el código que ya has escrito, y hacia fuera, hacia las herramientas que FreeBSD te proporciona para observar ese código mientras se ejecuta.

Hay dos caminos de lectura. El camino de solo lectura lleva unas tres o cuatro horas concentradas. Terminas con un modelo mental claro de qué hacen las herramientas de rendimiento de FreeBSD, cuándo usar cada una y cómo luce un ciclo de ajuste disciplinado desde el objetivo hasta la solución. No habrás producido un driver ajustado, pero podrás leer una salida de `pmcstat`, una agregación de DTrace, un resumen de lockstat o una métrica sysctl de un colega y entender lo que dice.

El camino de lectura con laboratorios lleva de siete a diez horas repartidas en dos o tres sesiones. Los laboratorios se construyen alrededor de un pequeño driver pedagógico llamado `perfdemo`, un dispositivo de caracteres que sintetiza lecturas desde un generador en el kernel a una tasa configurable. A lo largo del capítulo instrumentarás `perfdemo` con contadores y nodos sysctl, lo trazarás con DTrace, lo muestrearás con `pmcstat`, estrecharás sus caminos de memoria e interrupción, expondrás métricas de tiempo de ejecución y, finalmente, retirarás el andamiaje y publicarás una versión v2.3 optimizada. Cada laboratorio termina en algo que puedes observar en un sistema FreeBSD en ejecución. No necesitas hardware embebido; una máquina FreeBSD ordinaria amd64 o arm64, física o virtual, es suficiente.

Si tienes una máquina con PMU de hardware y privilegios para cargar el módulo del kernel `hwpmc(4)`, sacarás el mayor partido de la Sección 4. La mayoría de los sistemas x86 y arm64 de consumo tienen contadores utilizables. Las máquinas virtuales exponen un subconjunto; algunos entornos cloud no exponen ninguno. Cuando un laboratorio requiera soporte PMC, lo señalaremos y ofreceremos un camino alternativo usando el muestreo DTrace `profile` que funciona en todas partes.

### Requisitos previos

Deberías sentirte cómodo con el material sobre drivers de las Partes 1 a 6 y el Capítulo 32, centrado en sistemas embebidos. En particular, deberías saber:

- Qué hacen `probe()`, `attach()` y `detach()` y cuándo se ejecuta cada uno.
- Cómo declarar un softc, cómo encajan `device_t` y la tabla de métodos kobj, y cómo `DRIVER_MODULE(9)` registra un driver con newbus.
- Cómo asignar y liberar un mutex mediante `mtx_init(9)`, `mtx_lock(9)`, `mtx_unlock(9)` y `mtx_destroy(9)`.
- Cómo `bus_setup_intr(9)` registra un filtro o manejador de interrupciones, y la diferencia entre los manejadores filter e ithread.
- Cómo asignar memoria con `malloc(9)`, liberarla con `free(9)` y cuándo es apropiado `M_WAITOK` frente a `M_NOWAIT`.
- Cómo declarar un nodo `sysctl` con las macros estáticas (`SYSCTL_DECL`, `SYSCTL_NODE`, `SYSCTL_INT` y sus análogas) y el contexto dinámico (`sysctl_ctx_init(9)`).
- Cómo ejecutar `kldload(8)`, `kldunload(8)` y `sysctl(8)` en un sistema FreeBSD.

Si alguno de estos conceptos te resulta poco sólido, el capítulo anterior correspondiente es una breve revisita. El Capítulo 7 cubre los conceptos básicos de módulos y ciclo de vida, los Capítulos 11 al 13 cubren el locking y el manejo de interrupciones, el Capítulo 15 cubre sysctl, el Capítulo 17 cubre los taskqueues, el Capítulo 21 cubre DMA y buffering, y el Capítulo 32 los patrones de adquisición de recursos de newbus sobre los que construirás en la Sección 6.

No necesitas experiencia previa con DTrace, `pmcstat` ni flame graphs. El capítulo introduce cada uno desde cero al nivel que necesita un autor de drivers, y te señala las páginas del manual cuando quieres profundizar.

### Estructura y ritmo

Las Secciones 1 y 2 son fundamentales. Introducen el vocabulario del rendimiento y las primitivas de medición del kernel. Son breves para los estándares de este capítulo, porque el material anterior del libro ya asume que el lector sabe leer un `sysctl` y razonar sobre mutexes.

Las Secciones 3 y 4 son las secciones con mayor carga de herramientas. DTrace y `pmcstat` merecen un tratamiento exhaustivo porque son las dos herramientas a las que recurrirás con más frecuencia. Espera dedicar una hora a cada una si lees con atención.

Las Secciones 5 y 6 son las secciones de ajuste. Son las que te dicen qué *cambiar* una vez que has medido. También son las secciones con más probabilidad de tentar al lector hacia la sobreingeniería; léelas con la mentalidad de que aplicarás sus técnicas de forma selectiva, solo cuando la evidencia lo exija.

La Sección 7 trata sobre las métricas que permanecen en el driver después del ajuste. Esta sección es la que convierte a un velocista en corredor de maratón; los drivers que envejecen bien son los que exponen los números correctos, no los que fueron más rápidos el día del benchmark.

La Sección 8 cierra el ciclo. Te enseña a eliminar la instrumentación temporal que ayudó durante el ajuste, documentar los benchmarks que ejecutaste, actualizar la página del manual con los parámetros de ajuste que el driver ahora expone y publicar el driver en su nueva versión.

Lee las secciones en orden en una primera lectura. Cada una está escrita para ser suficientemente autónoma como referencia posterior, pero el modelo de enseñanza progresivo del libro depende del orden.

### Trabaja sección por sección

Cada sección cubre una parte clara del tema. Lee una, déjala reposar un momento y luego continúa. Si el final de una sección se siente confuso, haz una pausa y vuelve a leer los últimos párrafos; están diseñados para consolidar el material antes de que la siguiente sección lo tome como base.

### Mantén el driver del laboratorio cerca

El driver del laboratorio se encuentra en `examples/part-07/ch33-performance/` dentro del repositorio del libro. Cada directorio de laboratorio contiene una etapa autocontenida del driver con su `Makefile`, su `README.md` y cualquier script de DTrace o script de shell auxiliar que se incluya. Clona el directorio, trabaja en él directamente, constrúyelo con `make`, cárgalo con `kldload` y ejecuta las mediciones pertinentes. El bucle de retroalimentación entre un módulo del kernel y una lectura mediante sysctl o DTrace es la característica más didáctica de este capítulo; aprovéchalo.

### Abre el árbol de código fuente de FreeBSD

Varias secciones hacen referencia a archivos reales de FreeBSD. Los más útiles para tener abiertos durante este capítulo son `/usr/src/sys/sys/counter.h` y `/usr/src/sys/sys/pcpu.h`, que definen las primitivas counter y por CPU; `/usr/src/sys/sys/time.h`, que declara las funciones de tiempo del kernel; `/usr/src/sys/sys/lockstat.h`, que define las sondas DTrace de lockstat; `/usr/src/sys/vm/uma.h`, que declara el asignador de zonas UMA; `/usr/src/sys/dev/hwpmc/`, el árbol del driver PMC; y `/usr/src/sys/kern/subr_taskqueue.c`, donde reside `taskqueue_start_threads_cpuset()`. Ábrelos cuando el capítulo te indique que lo hagas. Las páginas de manual (`counter(9)`, `sysctl(9)`, `mutex(9)`, `dtrace(1)`, `pmc(3)`, `pmcstat(8)`, `hwpmc(4)`, `uma(9)`, `taskqueue(9)`) son la segunda mejor referencia después del código fuente.

> **Una nota sobre los números de línea.** Cada uno de esos archivos seguirá definiendo los símbolos a los que hace referencia el capítulo: `sbinuptime` en `time.h`, las macros de sonda `lockstat` en `lockstat.h` y `taskqueue_start_threads_cpuset` en `subr_taskqueue.c`. Esos nombres se mantienen en todas las versiones puntuales de FreeBSD 14.x. Sin embargo, la línea concreta en la que se encuentra cada uno en tu árbol puede haber cambiado desde que se escribió este capítulo, así que busca el símbolo en lugar de desplazarte hasta un número específico.

### Lleva un cuaderno de laboratorio

Continúa el cuaderno de laboratorio de los capítulos anteriores. Para este capítulo, anota los *números* en particular. Una entrada como *perfdemo v1.0, read() a 1000 Hz, mediana 14.2 us, P99 85 us* es el tipo de evidencia que te permite comparar una versión posterior de forma honesta. Sin el cuaderno, dentro de una semana estarás intentando recordar si un cambio mejoró las cosas o no; con él, puedes comprobarlo.

### Márcate un ritmo

El trabajo de rendimiento es cognitivamente exigente. Un lector que dedique dos horas de trabajo concentrado, tome un descanso apropiado y luego dedique dos horas más llegará casi siempre más lejos que otro que se esfuerce durante cinco horas seguidas. Las herramientas y los datos se benefician de una mente descansada.

## Cómo sacar el máximo partido a este capítulo

Algunos hábitos se van afianzando a lo largo del capítulo. Son los mismos hábitos que utilizan los ingenieros de rendimiento con experiencia; el único truco es adoptarlos desde el principio.

### Mide antes de cambiar

Esta es la regla. Toda optimización debe comenzar con una medición que muestre el comportamiento actual, terminar con una medición que muestre el nuevo comportamiento y comparar ambas con un objetivo definido de antemano. Una optimización sin un número de antes y después es una suposición que por casualidad pasó la revisión.

Si no recuerdas nada más de este capítulo, recuerda esto.

### Define el objetivo en números

Un objetivo como *quiero que la ruta de lectura sea más rápida* no es un objetivo. Un objetivo como *quiero que la latencia mediana de `read()` sea inferior a 20 microsegundos y el P99 inferior a 80 microsegundos, en esta carga de trabajo* sí lo es. Puedes medirlo, puedes saber cuándo lo has alcanzado y puedes dejar de optimizar en cuanto lo consigas. La mayoría del trabajo de rendimiento se prolonga demasiado porque el objetivo era vago.

### Prefiere la herramienta más sencilla que responda a la pregunta

La tentación de recurrir a `pmcstat` con un callgraph y un flame graph es grande. Normalmente, un one-liner de DTrace es suficiente. La herramienta más sencilla que te diga qué función domina, qué contador no para de crecer o qué lock está bajo contención es la herramienta adecuada. Escala solo cuando la herramienta más sencilla se quede sin respuestas.

### Nunca instrumentes el hot path al coste de producción

El acto de medir añade latencia. Un incremento de contador cuesta unos pocos nanosegundos en hardware moderno. Una sonda DTrace, cuando está habilitada, cuesta entre decenas y cientos de nanosegundos según la sonda. Un `printf` en el hot path cuesta decenas de microsegundos. Conoce estas cifras aproximadas antes de repartir código de medición por todas partes. Si el presupuesto del driver es de cincuenta microsegundos, un solo `printf` agotará ese presupuesto.

### Lee las páginas de manual que uses

`dtrace(1)`, `pmcstat(8)`, `hwpmc(4)`, `sysctl(8)`, `sysctl(9)`, `counter(9)`, `uma(9)`, `taskqueue(9)`, `mutex(9)`, `timer(9)` y `logger(1)` son las páginas de manual que importan en este capítulo. Cada una es breve, precisa y está escrita por personas que conocen la herramienta en profundidad. El libro no puede reemplazarlas, solo orientarte.

### Escribe el código del laboratorio

El driver `perfdemo` en los laboratorios es pequeño por la misma razón que `edled` era pequeño en el Capítulo 32: para que puedas escribirlo. Escríbelo. La memoria muscular que se adquiere al escribir un árbol sysctl, una definición de sonda DTrace, un incremento de contador y una ruta de adquisición de lock vale más que leer el mismo código diez veces.

### Etiqueta cada medición con su contexto

Un número sin contexto no sirve de nada. Cada medición debe registrarse con la carga de trabajo que la produjo, la máquina en la que se ejecutó, la versión del kernel, la versión del driver y lo que el sistema estaba haciendo en ese momento. Un driver que realiza 1,2 millones de operaciones por segundo en un servidor sin carga puede hacer 400.000 en el mismo servidor mientras se ejecuta una copia de seguridad. Si no registras el contexto, te desconcertará que el número cambie y acabarás culpando al driver.

### No ajustes lo que no es lento

Cada sección de este capítulo describe una técnica que podrías aplicar. Aplícalas solo cuando la medición lo exija. La alineación de línea de caché importa cuando el false sharing es un coste real; en un driver de baja concurrencia es ruido. Los contadores por CPU importan cuando la contención en los contadores es real; en un driver de bajo throughput son una optimización prematura. Resiste la tentación de aplicar todas las técnicas por el mero hecho de acabar de aprenderlas; el libro te ha enseñado las opciones, y las mediciones te dicen cuáles elegir.

### Sigue los números entre secciones

A medida que avances por el capítulo, mantén los números del driver `perfdemo` en tu cuaderno. Cada sección debería producir una pequeña mejora que mueva los números en la dirección correcta. Si la técnica de una sección no mueve tus números, anótalo también; es igualmente valioso saber qué *no* ayudó.

Con esos hábitos en mente, examinemos la razón por la que todo esto importa en primer lugar: el comportamiento de rendimiento del propio driver.

## Sección 1: por qué el rendimiento importa en los drivers de dispositivo

Todo driver es un contrato de rendimiento, lo escriba su autor explícitamente o no. El contrato dice: *cuando me pidas hacer X, haré X en esta cantidad de tiempo, usando esta cantidad de CPU, con este pico de memoria y con esta variabilidad.* Un driver que cumple su contrato es una pieza bien comportada del sistema. Un driver que lo incumple produce los síntomas que todos en el sistema perciben: saltos de audio, tearing de vídeo, paquetes descartados, colas de almacenamiento que se acumulan, shells bloqueadas en una escritura, sensores controlados por interrupciones que pierden eventos, una interfaz de usuario que tartamudea aunque la máquina tenga CPU de sobra. La mayoría de los problemas de rendimiento visibles para el usuario en cualquier sistema no trivial tienen un driver en algún punto de su call path.

El contrato tiene cuatro ejes. Aprender a verlos como aspectos separados es el primer paso real de este capítulo.

### Los cuatro ejes del rendimiento de un driver

**Throughput** es la cantidad de trabajo que el driver puede realizar por unidad de tiempo. En un driver de red es paquetes por segundo o bytes por segundo. En un driver de almacenamiento es operaciones de I/O por segundo (IOPS) o megabytes por segundo. En un dispositivo de caracteres es lecturas o escrituras por segundo. En un driver GPIO es cambios de estado de pin por segundo. El throughput responde a la pregunta *¿cuánto?*

**Latencia** es el tiempo que transcurre entre una solicitud y su finalización. En un driver de red son los microsegundos entre la llegada de un paquete y que la pila lo entrega a la capa de protocolo. En un driver de almacenamiento son los milisegundos (o ahora microsegundos) entre una llamada a `read()` y la disponibilidad del dato. En un driver de entrada es el tiempo entre la pulsación de una tecla y el momento en que el proceso en espacio de usuario ve el evento. La latencia responde a la pregunta *¿cuán rápido?*, y a diferencia del throughput se suele medir como una distribución. Una mediana (P50) de 10 microsegundos con un P99 de 500 microsegundos no es el mismo driver que una mediana de 50 microsegundos con un P99 de 60 microsegundos.

**Capacidad de respuesta** es un eje relacionado pero distinto: la rapidez con la que el driver se activa y realiza su trabajo tras un evento, desde la perspectiva de otros threads. Un driver puede tener una latencia excelente en su hot path y aun así ser poco reactivo si mantiene un lock durante diez milisegundos, si hace busy-wait en un sondeo de registro o si encola trabajo en un taskqueue que comparte threads con algún otro subsistema lento. La capacidad de respuesta es lo que perciben los usuarios y el planificador; la latencia es lo que el propio driver reportaría.

**Coste de CPU** es cuánto tiempo de CPU consume cada unidad de trabajo. Un driver que realiza 100.000 operaciones por segundo al 1% de CPU es más económico que uno que hace el mismo trabajo al 10%. En una placa embebida pequeña, este es el eje que determina si un bucle de sensor es viable en absoluto; en un servidor más grande, es el eje que determina cuánto más puede compartir la máquina.

Estos cuatro ejes no son independientes. Un driver puede intercambiar throughput por latencia: agrupa más trabajo por interrupción y aumentas el throughput, pero también aumentas la latencia que ve cada paquete. Puede intercambiar latencia por CPU: sondea con más frecuencia y reduces la latencia, pero aumentas el coste de CPU. Puede intercambiar capacidad de respuesta por throughput: mantén el lock más tiempo por llamada y cada llamada es más barata, pero la contención aumenta. No puedes minimizar los cuatro a la vez; el trabajo de rendimiento consiste en elegir el equilibrio que se adapte a la carga de trabajo.

Eso, en una frase, es por qué la medición importa. Sin números, no puedes saber cuál es el eje que realmente tiene el problema, ni si tu cambio mejoró el correcto.

### Un ejemplo práctico: ¿dónde aparecen estos ejes?

Imagina un driver de red que gestiona simultáneamente un flujo pequeño de tráfico de control a 100 paquetes por segundo y un flujo grande de transferencia masiva a 1 gigabit por segundo. Cada flujo necesita un perfil de rendimiento diferente.

El flujo pequeño quiere baja latencia. Un pico de 1 ms en un paquete de control puede hacer que se pierda un plazo que le importa a la capa superior. Para este flujo, es preferible gestionar cada paquete de forma inmediata en lugar de agruparlos. La ruta RX del driver debería activar la capa de protocolo en cada paquete, la interrupción debería atenderse con un retardo mínimo y cualquier lock por el que pase el paquete debería mantenerse brevemente.

El flujo grande quiere alto throughput y es relativamente insensible a la latencia. Cada paquete de la transferencia masiva puede permanecer en una cola de recepción durante decenas de microsegundos sin que nadie lo note, y el sistema se beneficia si el driver agrupa el procesamiento de interrupciones para reducir la sobrecarga por paquete. Para este flujo, la coalescencia de interrupciones es tu aliada y tiene sentido usar un thread dedicado de taskqueue.

Un driver que atiende solo uno de estos flujos puede ajustarse para él. Un driver que atiende ambos tiene que elegir un compromiso, mantener múltiples colas con políticas distintas o proporcionar un parámetro de ajuste que permita al operador elegir. En los tres casos, el autor del driver necesita saber qué eje importa para la carga de trabajo que el driver va a enfrentar en la práctica.

Los drivers reales de FreeBSD sí hacen este tipo de distinción. El framework `iflib(9)` que utilizan muchos drivers de red modernos proporciona tanto una recepción por fast path para entrega de baja latencia como un bucle de procesamiento por lotes rxeof para el throughput. El subsistema de almacenamiento `cam(4)` distingue entre I/O síncrono, que bloquea un thread, e I/O asíncrono, que no. El módulo `hwpmc(4)` distingue entre el modo de conteo (baja sobrecarga, siempre activo) y el modo de muestreo (mayor sobrecarga, diagnóstico). Todo árbol de drivers maduro en FreeBSD tiene estas distinciones incorporadas en algún lugar; reconocerlas es parte de aprender a leer código del kernel como un ingeniero de rendimiento.

### Cuándo vale la pena optimizar

No todo driver necesita ajustes. Un driver GPIO que cambia el estado de un pin unas pocas veces por segundo funciona perfectamente aunque cada operación tarde un milisegundo. Un pseudo-dispositivo de depuración que escribe una línea por hora es tan rápido como necesita ser. Un driver de notificación de hotplug de ACPI que se activa una vez por semana pasa casi todo el tiempo sin hacer nada. Para drivers como estos, dedicar un día a hacer profiling sería un día desperdiciado.

Los drivers que suelen merecer ajustes de rendimiento comparten algunas características comunes. Se encuentran en un *hot path*, una secuencia de llamadas por la que una carga de trabajo de alta tasa o sensible a la latencia pasa muchas veces por segundo. Están en la cadena que va desde la syscall de userland hasta el hardware, con suficientes capas por encima y por debajo como para que el driver represente una fracción medible del coste total. Operan sobre dispositivos cuyo hardware es lo bastante rápido como para que el driver, y no el dispositivo, se convierta en el cuello de botella. Y son visibles para los usuarios o para los tests, de modo que una regresión se notará y una mejora de rendimiento tendrá valor.

Los ejemplos clásicos son los drivers de red de alta velocidad (10G, 25G, 40G, 100G), los drivers de almacenamiento NVMe, los drivers de I/O para virtualización (virtio-net, virtio-blk), los drivers de audio con presupuestos de latencia ajustados y, en sistemas embebidos, cualquier driver que forme parte del bucle de control principal del producto. Si un driver pertenece a esta lista, medirlo y ajustarlo compensa con creces. Si no es así, solo merece la pena optimizarlo cuando una medición (o una queja de un usuario) señale al driver como el problema.

### Cuándo la optimización es prematura

La observación de Knuth de que *la optimización prematura es la raíz de todos los males* se refería a la capa intermedia de un programa, no a sus decisiones de diseño más externas. El dicho se aplica a los drivers de una manera particular: casi siempre es un error invertir esfuerzo en optimizar código antes de que funcione correctamente, antes de medirlo o antes de haber establecido un objetivo. La razón no es que la optimización nunca esté justificada; es que la optimización temprana tiende a hacer el código más difícil de razonar, más difícil de depurar y más difícil de modificar cuando resulta que el cuello de botella real está en otro lugar.

En concreto, las siguientes son señales de alerta de optimización prematura en un driver:

- Código SIMD escrito a mano en un driver cuyo rendimiento se mide en miles, no en miles de millones, de operaciones por segundo.
- Un softc cuidadosamente alineado a línea de caché en un driver que tiene como máximo una operación pendiente por CPU en un momento dado.
- Un ring buffer sin lock en un driver cuyo cuello de botella real es una llamada a `malloc(9)` por operación.
- Un conjunto de contadores por CPU en un driver que ejecuta una operación por segundo.
- Un esquema elaborado de taskqueue en un driver cuyo manejador de interrupciones se ejecuta en 2 microsegundos.

En cada caso, la técnica es real y legítima, pero se ha elegido sin evidencia. El esfuerzo tiene un coste (complejidad, revisabilidad, riesgo de errores) y solo produce un beneficio cuando la evidencia demuestra que es necesaria. La disciplina no es *nunca optimices*; es *mide y luego decide*.

### Cómo establecer objetivos medibles

Un objetivo de rendimiento tiene cuatro partes: la **métrica**, la **meta**, la **carga de trabajo** y el **entorno**.

La métrica indica qué estás midiendo. *Latencia mediana de `read()`*, *paquetes por segundo reenviados*, *interrupciones por segundo atendidas*, *porcentaje de CPU a plena carga*, *memoria del kernel usada en el pico* o *latencia en el peor caso P99 del camino de probe a attach* son todas métricas válidas. Cada una es un único número producido por un procedimiento concreto.

La meta indica el valor que quieres que tenga la métrica. *Por debajo de 20 microsegundos*, *por encima de 1 millón de paquetes por segundo*, *menos del 5% de CPU*, *menos de 4 MB de memoria del kernel en el pico*. La meta debe ser concreta y medible.

La carga de trabajo indica las condiciones en las que se produce la métrica. *Llamadas a `read()` emitidas a 10,000 Hz con asignación `M_WAITOK`*, *un flujo TCP a la velocidad de línea en una NIC de 10 Gbps*, *una prueba de lectura aleatoria de 4K contra un archivo de 16 GB*. La carga de trabajo debe ser reproducible; si nadie puede volver a ejecutarla, la medición no tiene utilidad.

El entorno indica la máquina, el kernel y los procesos circundantes. *amd64, Xeon de 8 núcleos a 3.0 GHz, kernel FreeBSD 14.3 GENERIC, por lo demás inactivo*. Dos máquinas distintas darán números diferentes para el mismo driver, y una medición válida en un entorno puede no serlo en otro.

Un objetivo de la forma *la latencia mediana de `read()` del driver `perfdemo`, medida en 100,000 llamadas sobre un Xeon E5-2680 amd64 inactivo a 3.0 GHz ejecutando FreeBSD 14.3 GENERIC, con un thread lector en el espacio de usuario anclado a la CPU 1, debe ser inferior a 20 microsegundos* es un objetivo de rendimiento completo. Tiene una métrica, una meta, una carga de trabajo y un entorno. Puedes ejecutarlo, registrar el resultado, comparar un cambio de ajuste y decidir si has terminado.

Un objetivo de la forma *haz el driver más rápido* no sirve de nada. No tiene métrica, ni meta, ni carga de trabajo, ni entorno. Puedes trabajar en ello durante un año y seguir sin saber cuándo parar.

La disciplina de escribir el objetivo antes de empezar es de donde proviene la mayor parte de la calidad en un proyecto de rendimiento. Si no puedes escribir el objetivo, no estás listo para optimizar. El resto del capítulo asumirá que tienes uno y te enseñará las herramientas que te permiten verificarlo.

### Una nota sobre «rápido» y «correcto»

Dos fallos del trabajo de rendimiento merecen mencionarse desde el principio.

El primero es que un driver que es rápido en una dimensión puede ser malo en general. Un driver que logra un rendimiento vertiginoso descartando paquetes ocasionales en silencio no es más rápido; está roto. Un driver que reduce su latencia a la mitad saltándose una comprobación de seguridad en el camino rápido no es más rápido; es arriesgado. Un driver que reduce la carga de la CPU difiriendo la liberación de memoria hasta el descargue no es más rápido; tiene una fuga. Toda optimización debe preservar la corrección, incluida la corrección de los caminos de error y el comportamiento ante entradas patológicas. Una optimización que cambia corrección por velocidad es una regresión, y punto.

El segundo fallo es que un driver que hoy es rápido puede convertirse en una pesadilla de depuración mañana. El trabajo de rendimiento introduce a menudo complejidad: pools de preasignación, múltiples locks, operaciones en lotes, ensamblador inline para atómicas, diseños de memoria cuidadosos. Cada uno de estos tiene un coste futuro en depuración. Antes de comprometerte con una optimización compleja, pregúntate si una más sencilla es suficiente. El driver que vive en el árbol durante años no suele ser el que fue más rápido en el benchmark; es el que un mantenedor todavía puede leer y razonar después de que el autor original se haya marchado.

Volveremos a ambos puntos en la Sección 8 cuando hablemos de publicar un driver ajustado.

### Ejercicio 33.1: Define objetivos de rendimiento para un driver que ya tienes

Elige un driver que hayas escrito en capítulos anteriores. `perfdemo` aparecerá en la Sección 2, así que por ahora elige uno anterior: `nullchar` del Capítulo 7, el driver de caracteres con ring buffer del Capítulo 12 o el `edled` del Capítulo 32. Para ese driver, escribe objetivos de rendimiento para al menos dos de los cuatro ejes (rendimiento, latencia, capacidad de respuesta, coste de CPU). Para cada objetivo, incluye una métrica, una meta, una carga de trabajo y un entorno. Guarda los objetivos en tu cuaderno de notas.

No tienes que haber medido el driver todavía. El ejercicio es la escritura del objetivo. Si no puedes decidir una meta, escribe *desconocida, por medir en la Sección 2* y vuelve más tarde.

El acto de escribir un objetivo concreto agudiza lo que el resto del capítulo intenta enseñar. Los lectores que se saltan este ejercicio suelen encontrar que las Secciones 2 a 7 parecen abstractas; los que lo hacen descubren que cada nueva herramienta tiene un uso obvio.

### Cerrando la Sección 1

Abrimos con la afirmación de que todo driver es un contrato de rendimiento, tanto si el autor lo escribe como si no. Los cuatro ejes de rendimiento, latencia, capacidad de respuesta y coste de CPU te dan un vocabulario para plasmar ese contrato por escrito. La disciplina de los objetivos medibles, fijados a una métrica, una meta, una carga de trabajo y un entorno, te da una forma de saber si el contrato se está cumpliendo. Los recordatorios sobre la optimización prematura y sobre la relación entre velocidad y corrección mantienen el trabajo honesto.

El resto del capítulo es el conjunto de herramientas. En la siguiente sección examinamos las primitivas de medición del kernel: funciones de temporización, contadores, nodos sysctl y las herramientas de FreeBSD (`sysctl`, `dtrace`, `pmcstat`, `ktrace`, `top`, `systat`) que exponen las métricas de las que depende tu objetivo.

## Sección 2: Cómo medir el rendimiento en el kernel

La medición en el espacio de usuario es el mundo en el que la mayoría de los programadores crecen. Envuelves una llamada a función con un temporizador, la ejecutas un millón de veces e imprimes la media. La medición en el kernel no es tan diferente en su forma, pero es más delicada en la práctica. El código del kernel se ejecuta en el límite del sistema; una medición descuidada puede ralentizar lo mismo que intenta medir o, lo que es peor, cambiar el comportamiento del sistema de formas que hagan la medición carente de sentido. El objetivo de esta sección es enseñarte a medir dentro del kernel sin contaminar la medición.

Recorreremos cuatro aspectos: las funciones de tiempo del kernel, las primitivas de contador que te permiten acumular eventos de forma económica, las herramientas que leen esos contadores desde el espacio de usuario y los hábitos de instrumentación que mantienen la medición honesta.

### Funciones de tiempo del kernel

El kernel expone varias funciones que devuelven el tiempo actual. Difieren en precisión, coste y en si avanzan de forma monótona. Las declaraciones se encuentran en `/usr/src/sys/sys/time.h`.

Al elegir una de estas funciones tienes dos elecciones ortogonales. La primera es el **formato**:

- Las variantes `*time` devuelven un `struct timespec` (segundos y nanosegundos) o un `struct timeval` (segundos y microsegundos).
- Las variantes `*uptime` devuelven el mismo formato, pero medido desde el arranque del sistema en lugar del reloj de pared. El uptime no salta cuando el administrador ajusta el reloj de pared; el tiempo de pared sí.
- Las variantes `*bintime` devuelven un `struct bintime` (una fracción binaria de punto fijo de un segundo) y `sbinuptime()` devuelve un `sbintime_t`, un valor de punto fijo con signo de 64 bits. Estos son los formatos internos del kernel con mayor resolución.

La segunda elección es la **precisión frente al coste**. FreeBSD distingue entre un camino *rápido pero impreciso* y otro *preciso pero más costoso*:

- Las funciones con el prefijo **`get`** (`getnanotime()`, `getnanouptime()`, `getbinuptime()`, `getmicrotime()`, `getmicrouptime()`, `getsbinuptime()`) devuelven un valor almacenado en caché en el último tick del temporizador. Son muy rápidas, normalmente solo unas pocas cargas desde una variable global residente en caché. Su precisión es del orden de `1/hz`, que en un sistema FreeBSD con la configuración predeterminada es de aproximadamente 1 milisegundo.
- Las funciones sin el prefijo `get` (`nanotime()`, `nanouptime()`, `binuptime()`, `microtime()`, `microuptime()`, `sbinuptime()`) realizan una llamada al hardware del timecounter seleccionado y devuelven un valor preciso según la resolución del timecounter, a menudo decenas de nanosegundos. Tienen mayor coste, normalmente decenas a cientos de nanosegundos, y en algunos hardware pueden serializar el pipeline de la CPU.

La regla de oro: usa las variantes `get*` siempre que una precisión de 1 milisegundo sea suficiente, y las variantes sin `get` cuando necesites precisión real. Un driver que mide su propia latencia en el camino caliente generalmente necesita las variantes sin `get`. Un driver que pone marcas de tiempo a un evento raro, o registra el momento de un cambio de estado, casi siempre puede usar las variantes `get*`.

Esta es la versión resumida del árbol de decisión:

- Si necesitas tiempo de reloj de pared, precisión de milisegundo: `getnanotime()`.
- Si necesitas uptime (monótono), precisión de milisegundo: `getnanouptime()`.
- Si necesitas uptime, precisión de nanosegundo: `nanouptime()`.
- Si necesitas uptime, mayor resolución, coste mínimo por llamada: `sbinuptime()` (o `getsbinuptime()` cuando la precisión de milisegundo es suficiente).
- Si necesitas calcular una duración: resta dos valores de `sbinuptime()` y convierte a microsegundos o nanosegundos al final.

Una temporización representativa en un camino de lectura de un driver podría tener este aspecto:

```c
#include <sys/time.h>
#include <sys/sysctl.h>

static uint64_t perfdemo_read_ns_total;
static uint64_t perfdemo_read_count;

static int
perfdemo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    sbintime_t t0, t1;
    int error;

    t0 = sbinuptime();

    /* ... the real work of reading ... */
    error = do_the_read(uio);

    t1 = sbinuptime();

    /* Convert sbintime_t difference to nanoseconds.
     * sbt2ns() is defined in /usr/src/sys/sys/time.h. */
    atomic_add_64(&perfdemo_read_ns_total, sbttons(t1 - t0));
    atomic_add_64(&perfdemo_read_count, 1);

    return (error);
}
```

Conviene observar varias cosas. Las marcas de tiempo se capturan con `sbinuptime()`, no con `getsbinuptime()`, porque estamos midiendo duraciones en la escala de microsegundos. El acumulador y el contador son de 64 bits y se actualizan con `atomic_add_64()`, de modo que los lectores concurrentes no pierden actualizaciones. La conversión de `sbintime_t` a nanosegundos usa `sbttons()`, una macro declarada en `/usr/src/sys/sys/time.h`; no realizamos la división en el camino caliente. Ni las marcas de tiempo ni la acumulación imprimen nada; los datos van a contadores que un sysctl lee bajo demanda.

El patrón se generaliza. Pon marcas de tiempo en los límites de la operación que te interesa, acumula la diferencia, expón el acumulador a través de sysctl y calcula las métricas derivadas (media, rendimiento) en el espacio de usuario.

### Contadores de marca de tiempo y otras fuentes de alta precisión

Por debajo de la familia `nanotime` y `sbinuptime` se encuentra el hardware que realmente las alimenta: el Time Stamp Counter (TSC) del procesador en x86, el Generic Timer en arm64 y sus equivalentes en otras arquitecturas. FreeBSD abstrae estas fuentes detrás de una interfaz *timecounter*; el kernel selecciona una en el boot y el camino `sbinuptime` la lee. Puedes ver qué timecounter está usando tu sistema con `sysctl kern.timecounter`:

```console
# sysctl kern.timecounter
kern.timecounter.tick: 1
kern.timecounter.choice: ACPI-fast(900) HPET(950) i8254(0) TSC-low(1000) dummy(-1000000)
kern.timecounter.hardware: TSC-low
```

En una máquina amd64 moderna, el TSC es casi siempre la fuente elegida. Avanza a una tasa constante independientemente de la frecuencia del CPU (en procesadores que soportan TSC invariante, que son prácticamente todos los fabricados desde mediados de la década de 2000), su lectura es muy económica (una sola instrucción) y su resolución es del orden del período de reloj del CPU, aproximadamente 0,3 nanosegundos a 3 GHz.

Un driver rara vez necesita leer el TSC directamente. `sbinuptime()` ya lo abstrae. Pero cuando estás depurando el propio código de temporización del kernel, o cuando necesitas una marca de tiempo que no sea más que una carga directa desde un registro sin aritmética adicional, el kernel proporciona `rdtsc()` como una función `static __inline` en `/usr/src/sys/amd64/include/cpufunc.h`. Su uso casi siempre es un error en el código del driver: pierdes las conversiones de unidades del kernel, pierdes la portabilidad entre arquitecturas y ganas unos pocos nanosegundos. Recurre a `sbinuptime()` en su lugar; la portabilidad y la abstracción merecen la pena.

En arm64, el equivalente del TSC es el registro contador del Generic Timer, que se lee a través de los macros `READ_SPECIALREG` específicos de ARM en `/usr/src/sys/arm64/include/cpu.h`. El kernel lo expone mediante la misma abstracción de `sbinuptime()`, de modo que un driver escrito sobre `sbinuptime()` es portable entre las dos arquitecturas sin ningún cambio. Esta es una de las ventajas pequeñas pero significativas de mantenerse dentro de las abstracciones que ofrece FreeBSD.

Un aspecto sutil: las diferentes opciones de timecounter tienen costes y precisiones distintas. En las generaciones de Intel y AMD actualmente en uso, HPET es lento (del orden de cientos de nanosegundos por lectura) pero de alta precisión; ACPI-fast es rápido pero de menor precisión; y el TSC es rápido y preciso, razón por la que el kernel lo prefiere cuando está disponible. Si `kern.timecounter.hardware` muestra algo distinto de TSC en una máquina amd64, algún elemento del sistema lo ha desactivado y las llamadas a `sbinuptime()` serán más costosas de lo esperado. Ejecuta `dmesg | grep timecounter` al comienzo de cualquier investigación de rendimiento. Consulta el Apéndice F para ver un benchmark reproducible que recorre cada fuente de timecounter.

### Composición: temporización, conteo y agregación

Los ejemplos anteriores mostraron la temporización (un par de llamadas a `sbinuptime()`) y el conteo (un incremento de `counter_u64_add()`) por separado. La medición real en un driver casi siempre combina ambos: quieres saber *cuántas* operaciones de cada tipo se produjeron *y* cuál fue la *distribución de latencia* de cada tipo. La composición es sencilla una vez que se dominan los primitivos.

Un patrón que aparece en muchos drivers de FreeBSD es el par contador-histograma. Para cada operación, se incrementa un contador y se añade su latencia al cubo correspondiente del histograma. El histograma se representa como un array de valores `counter_u64_t`, uno por cubo, con los límites de cada cubo elegidos según el rango esperado. En el hot path, se realiza un incremento de contador para el total y otro para el cubo en el que cae la latencia:

```c
#define PD_HIST_BUCKETS 8

static const uint64_t perfdemo_hist_bounds_ns[PD_HIST_BUCKETS] = {
    1000,       /* <1us    */
    10000,      /* <10us   */
    100000,     /* <100us  */
    1000000,    /* <1ms    */
    10000000,   /* <10ms   */
    100000000,  /* <100ms  */
    1000000000, /* <1s     */
    UINT64_MAX, /* >=1s    */
};

static counter_u64_t perfdemo_read_hist[PD_HIST_BUCKETS];
static counter_u64_t perfdemo_read_count;

static int
perfdemo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    sbintime_t t0, t1;
    uint64_t ns;
    int error, i;

    t0 = sbinuptime();
    error = do_the_read(uio);
    t1 = sbinuptime();

    ns = sbttons(t1 - t0);
    counter_u64_add(perfdemo_read_count, 1);
    for (i = 0; i < PD_HIST_BUCKETS; i++) {
        if (ns < perfdemo_hist_bounds_ns[i]) {
            counter_u64_add(perfdemo_read_hist[i], 1);
            break;
        }
    }

    return (error);
}
```

El hot path realiza un par de llamadas a `sbinuptime` (del orden de pocas decenas de nanosegundos cada una en hardware típico FreeBSD 14.3-amd64), una multiplicación para convertir a nanosegundos, un recorrido lineal por los ocho límites de cubo (bien predicho por el predictor de ramas tras el calentamiento) y dos incrementos de contadores. Como cifra aproximada de orden de magnitud en ese mismo tipo de máquina, la sobrecarga total se mantiene cómodamente por debajo de un microsegundo, lo que es aceptable para la mayoría de los caminos de lectura y suficientemente reducido como para dejarlo activo en producción. Consulta el Apéndice F para encontrar un benchmark reproducible del coste subyacente de `sbinuptime()` en tu propio hardware.

Exponer el histograma mediante sysctl es una pequeña extensión del patrón sysctl procedimental de la Sección 7. Se obtiene el contador de cada cubo, se copian en un array local y se entrega dicho array a `SYSCTL_OUT()`. El espacio de usuario lee el array y lo representa gráficamente.

Si se conoce el rango de latencia esperado, la búsqueda lineal de cubo puede sustituirse por un mapeo de tiempo constante. Para cubos de potencias de diez, el `log10` de la latencia te lleva al cubo correcto; para cubos de potencias de dos, basta con una única instrucción `fls`. Las variantes de tiempo constante solo merecen la complejidad adicional cuando la búsqueda lineal aparece como un coste real en un perfil, lo que en la práctica solo ocurre en hot paths muy exigentes con muchos cubos.

### Una ruta de lectura completamente instrumentada

Reuniendo todo lo anterior, una función `perfdemo_read()` completamente instrumentada que captura el número de llamadas, el número de errores, el total de bytes, la latencia total y un histograma de latencia tiene el siguiente aspecto:

```c
#include <sys/counter.h>
#include <sys/time.h>

struct perfdemo_stats {
    counter_u64_t reads;
    counter_u64_t errors;
    counter_u64_t bytes;
    counter_u64_t lat_ns_total;
    counter_u64_t hist[PD_HIST_BUCKETS];
};

static struct perfdemo_stats perfdemo_stats;

static int
perfdemo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    sbintime_t t0, t1;
    uint64_t ns, bytes_read;
    int error, i;

    t0 = sbinuptime();
    bytes_read = uio->uio_resid;
    error = do_the_read(uio);
    bytes_read -= uio->uio_resid;
    t1 = sbinuptime();

    ns = sbttons(t1 - t0);
    counter_u64_add(perfdemo_stats.reads, 1);
    counter_u64_add(perfdemo_stats.lat_ns_total, ns);
    counter_u64_add(perfdemo_stats.bytes, bytes_read);
    if (error)
        counter_u64_add(perfdemo_stats.errors, 1);
    for (i = 0; i < PD_HIST_BUCKETS; i++) {
        if (ns < perfdemo_hist_bounds_ns[i]) {
            counter_u64_add(perfdemo_stats.hist[i], 1);
            break;
        }
    }

    return (error);
}
```

Esta es la forma de una medición de calidad para producción. Cuenta, mide tiempos, registra distribuciones y cuesta un puñado de nanosegundos por llamada en el hot path. Las métricas derivadas (latencia media, P50, P95, P99 estimadas a partir del histograma) se calculan en el espacio de usuario bajo demanda. Los valores de `counter(9)` escalan hasta decenas de núcleos sin contención; la búsqueda lineal de cubo es amigable con la caché; el contador condicional de `errors` no añade coste en el camino de éxito.

En las secciones siguientes ajustaremos otras partes del driver, pero el andamiaje de medición anterior es la línea de base *contra la que medimos*. Cada cambio que hagamos se juzgará frente a los números que produce dicho andamiaje.

### Contadores: simples, atómicos y por CPU

El código del kernel a menudo necesita contar cosas: operaciones completadas, errores, reintentos, tormentas de interrupciones. FreeBSD ofrece tres niveles de primitivo de contador, en orden creciente de complejidad y escalabilidad.

**Un `uint64_t` simple actualizado con `atomic_add_64()`**. Es el contador más sencillo. Es correcto en entornos concurrentes, barato en hardware moderno y funciona en todas partes. Con poca concurrencia (decenas de miles de actualizaciones por segundo desde unos pocos CPUs), es una opción perfectamente válida por defecto. Con alta concurrencia (cientos de miles de actualizaciones por segundo desde muchos CPUs), la línea de caché que contiene el contador se convierte en un punto de contención y la operación atómica empieza a aparecer en los perfiles. Para la mayoría de los contadores en un driver, el atómico simple es la elección correcta.

**Un valor `counter(9)` (`counter_u64_t`)**. Es un contador por CPU con una interfaz de lectura sencilla. Se asigna con `counter_u64_alloc()`, se actualiza con `counter_u64_add()` y se lee con `counter_u64_fetch()` (que suma los valores de todos los CPUs). Como cada CPU actualiza su propia memoria, no hay contención en el camino de actualización. La contrapartida es que la lectura itera sobre todos los CPUs, por lo que leer el contador es ligeramente más costoso. Para drivers cuyos contadores se actualizan en fast paths desde muchos CPUs, `counter(9)` encaja mejor que un atómico simple. Las declaraciones se encuentran en `/usr/src/sys/sys/counter.h`.

**Una variable `DPCPU_DEFINE(9)`**. DPCPU son las siglas de *dynamic per-CPU*. Permite definir cualquier tipo de variable, no solo un contador, con almacenamiento por CPU. La variable se declara con `DPCPU_DEFINE(type, name)`, se accede a la copia del CPU actual con `DPCPU_GET(name)` y `DPCPU_SET(name, v)`, y se suma el valor de todos los CPUs con `DPCPU_SUM(name)`. Es el más flexible de los tres primitivos, y es sobre él que se implementa `counter(9)`. Úsalo cuando necesites estado por CPU que no sea simplemente un contador, o cuando necesites un contador y tengas una razón concreta para evitar la abstracción de `counter(9)`. Las declaraciones se encuentran en `/usr/src/sys/sys/pcpu.h`.

A continuación se muestra un ejemplo práctico. Supón que `perfdemo` tiene tres contadores: lecturas totales, errores totales y bytes totales entregados. La forma con `counter(9)` es:

```c
#include <sys/counter.h>

static counter_u64_t perfdemo_reads;
static counter_u64_t perfdemo_errors;
static counter_u64_t perfdemo_bytes;

/* In module init: */
perfdemo_reads  = counter_u64_alloc(M_WAITOK);
perfdemo_errors = counter_u64_alloc(M_WAITOK);
perfdemo_bytes  = counter_u64_alloc(M_WAITOK);

/* On the fast path: */
counter_u64_add(perfdemo_reads, 1);
counter_u64_add(perfdemo_bytes, bytes_delivered);
if (error)
    counter_u64_add(perfdemo_errors, 1);

/* In module fini: */
counter_u64_free(perfdemo_reads);
counter_u64_free(perfdemo_errors);
counter_u64_free(perfdemo_bytes);

/* In a sysctl handler that reports the values: */
uint64_t v = counter_u64_fetch(perfdemo_reads);
```

La API de `counter(9)` mantiene el coste en el hot path pequeño y constante independientemente del número de CPUs que atienda tu driver. En un servidor de 64 núcleos supone una mejora radical frente a un atómico individual. En un portátil de 4 núcleos es una mejora pequeña pero real. En una placa embebida con un solo núcleo no aporta ventaja sobre un atómico. Úsalo cuando haya contención, o cuando quieras estar preparado para el día en que el driver pase de una máquina de pruebas con 2 núcleos a una de producción con 64.

### Exposición de métricas mediante sysctl

Un contador que nadie puede leer es un contador que no existe. El subsistema `sysctl(9)` de FreeBSD es la forma estándar de exponer las métricas del driver al espacio de usuario, y el capítulo cubre los patrones de producción en detalle en la Sección 7. Por ahora, el patrón más sencillo es:

```c
SYSCTL_NODE(_hw, OID_AUTO, perfdemo, CTLFLAG_RD, 0,
    "perfdemo driver");

SYSCTL_U64(_hw_perfdemo, OID_AUTO, reads, CTLFLAG_RD,
    &perfdemo_reads_value, 0,
    "Total read() calls");
```

Para una variable `counter(9)`, la forma idiomática es utilizar un sysctl procedimental que llame a `counter_u64_fetch()` en cada lectura. Desarrollaremos ese patrón en la Sección 7. Por ahora, la macro `SYSCTL_U64` anterior es suficiente para un `uint64_t` simple o para el resultado de `counter_u64_fetch()` guardado en una variable ordinaria durante un callout periódico.

Desde el espacio de usuario, `sysctl hw.perfdemo.reads` devuelve el valor, y los scripts pueden sondear a intervalos, calcular tasas y generar gráficas.

### Las herramientas de medición que ofrece FreeBSD

Además de los primitivos anteriores, FreeBSD incluye varias herramientas en el espacio de usuario que leen las métricas expuestas por el kernel y las presentan de forma útil. A continuación se ofrece un recorrido breve; volveremos a las más importantes en secciones posteriores.

**`sysctl(8)`**. Lee cualquier nodo sysctl. Es la interfaz de medición más básica. Es la herramienta para sondear un contador cada segundo y calcular una tasa, comprobar una métrica puntual o volcar un subárbol para compararlo después. Un script como `while sleep 1; do sysctl hw.perfdemo; done | awk ...` es un primer paso sorprendentemente habitual.

**`top(1)`**. Muestra procesos, threads, uso de CPU y uso de memoria. Con el flag `-H` muestra los threads del kernel, incluidos los threads de interrupción y taskqueue. Resulta útil cuando un driver tiene su propio thread cuyo uso de CPU quieres monitorizar, o cuando quieres ver un ithread consumiendo CPU al gestionar interrupciones.

**`systat(1)`**. Una familia de vistas de monitorización antigua pero aún útil. `systat -vmstat` muestra tasas de CPU, disco, memoria e interrupciones. `systat -iostat` se centra en el almacenamiento. `systat -netstat` se centra en la red. Cuando necesitas un monitor en tiempo real sin escribir un script, `systat` suele ser más rápido de usar que cualquier alternativa más elaborada.

**`vmstat(8)`**. Una vista por segundo de las estadísticas globales del sistema. `vmstat -i` lista las tasas de interrupción por vector. `vmstat -z` lista la actividad de las zonas UMA, lo que resulta inestimable para rastrear la presión de memoria en el kernel.

**`ktrace(1)`**. Traza las entradas y salidas de syscalls para procesos concretos. Es de más bajo nivel que DTrace y lo precede, pero sigue siendo útil cuando la interacción que se desea observar cruza la frontera entre el espacio de usuario y el kernel y no requiere sondas personalizadas en el lado del kernel.

**`dtrace(1)`**. La navaja suiza de la observación del kernel de FreeBSD. Se trata en profundidad en la Sección 3.

**`pmcstat(8)`**. La herramienta de contadores de rendimiento de hardware. Se trata en profundidad en la Sección 4.

La conclusión importante es que estas herramientas no son competidoras entre sí. Cada una es la mejor opción para un tipo concreto de medición. Se aprende el trabajo de rendimiento identificando qué herramienta responde a qué pregunta y recurriendo a ella en primer lugar.

### El efecto Heisenberg en la práctica

La referencia jocosa de la introducción del capítulo apuntaba a un problema real: el acto de medir cambia lo que se mide. En el código del kernel el problema tiene fuerza, y conviene conocer su forma.

La primera forma del problema es la **sobrecarga directa**. Toda medición tiene un coste. Si añades un `printf` dentro de un camino de lectura que funciona a 100.000 lecturas por segundo, habrás añadido decenas de microsegundos por lectura, y la tasa efectiva del driver cae a una fracción de lo que sería sin esa llamada. Si mides cada operación con `sbinuptime()` e incrementas un contador atómico, habrás añadido quizá 50 nanosegundos por llamada; para la mayoría de los drivers esto es aceptable, pero en un hot path con presupuesto de nanosegundos incluso esto puede ser demasiado. La regla general es: estima el coste de cada sonda de medición y decide si el driver puede permitírselo en el camino que estás sondando.

La segunda forma es la **contención de caché y lock**. Un único contador atómico actualizado por todos los CPUs es una línea de caché que rebota entre ellos. En caminos con poca contención esto cuesta unos pocos nanosegundos. En caminos con alta contención puede costar cientos de nanosegundos por actualización y dominar la operación. Un contador que debería ser barato puede convertirse en el cuello de botella que él mismo intenta medir. La solución, como vimos antes, es `counter(9)` o una variable DPCPU; ambas actualizan memoria por CPU.

La tercera forma es el **cambio de comportamiento inducido por el observador**. Es la más insidiosa. Añadir instrumentación puede cambiar el comportamiento del sistema más allá del coste directo. Un `printf` puede hacer que un temporizador que estaba a punto de dispararse incumpla su plazo. Una sonda DTrace que acaba usando un camino de código más lento puede deshabilitar una optimización del fast path. Un breakpoint establecido en un depurador puede ocultar una condición de carrera que el código sin sonda sí exhibe. Un buen diseño de medición trata de minimizar los efectos del observador, pero la conciencia de que existen es lo que separa una práctica de medición disciplinada de una supersticiosa.

La consecuencia práctica es que siempre debes *desactivar* el código de medición cuando midas la línea de base, a menos que la medición sea precisamente lo que estás evaluando. Si quieres saber qué velocidad puede alcanzar un driver, ejecútalo con la instrumentación mínima necesaria para informar del resultado. Si esa instrumentación mínima es costosa, redúcela. Por eso también se prefiere `counter(9)` sobre los atómicos para el conteo en hot paths: no solo porque es más rápido, sino porque perturba menos el sistema.

### Instrumentar sin contaminar

El consejo práctico sobre cómo añadir código de medición se resume en una pequeña lista de comprobación. Antes de añadir una sonda, pregúntate:

1. **¿Qué métrica produce esta sonda?** Toda sonda debe corresponder a una línea concreta del documento de objetivos. Si no puedes nombrarla, no la añadas.
2. **¿En qué ruta vive la sonda?** Una sonda en la ruta de carga del módulo es gratuita. Una sonda en una ruta que se ejecuta una vez por operación en un driver de alto rendimiento debe ser lo bastante barata para encajar en el presupuesto.
3. **¿Cuánto cuesta la sonda?** Un contador atómico cuesta unos pocos nanosegundos. Un `counter(9)` es similar, pero escala mejor. Una marca de tiempo con `sbinuptime()` cuesta decenas de nanosegundos. Un `printf` cuesta decenas de microsegundos. Una sonda DTrace, cuando está habilitada, cuesta cientos de nanosegundos.
4. **¿Está la sonda siempre activa, o solo durante un modo de instrumentación?** Un contador que permanece en producción es una funcionalidad. Una traza detallada es una herramienta para una sesión de ajuste.
5. **¿Provoca la sonda contención?** Escribir en una única línea de caché desde múltiples CPUs es un problema de contención; leerla no lo es.
6. **¿Altera la sonda el orden de ejecución?** Una barrera de memoria añadida puede ocultar una condición de carrera; un sleep añadido puede ocultar un problema de contención.

La mayor parte de esto es sentido común una vez que has visto fallar una o dos mediciones. Escribirlo como una lista de comprobación desde el principio ahorra el dolor de tener que redescubrirlo.

### Ejercicio 33.2: Cronometra el camino de lectura

Toma un driver sencillo que hayas escrito anteriormente, o usa el andamiaje `perfdemo` que presentaremos en la sección de laboratorio. Añade dos variables `counter_u64_t`: `reads` y `read_ns_total`. En el manejador de lectura, toma una marca de tiempo con `sbinuptime()` al entrar, realiza el trabajo, toma otra marca de tiempo, añade la diferencia en nanosegundos a `read_ns_total` e incrementa `reads` en 1. Expón ambas vía sysctl.

Compila el driver. Cárgalo. Ejecuta un bucle en espacio de usuario que lea del dispositivo 100.000 veces. Luego calcula la latencia media como `read_ns_total / reads` y anota el número en tu cuaderno de registro. A continuación, elimina las actualizaciones de los contadores, vuelve a compilar y repite la prueba; si las herramientas de medición lo permiten, compara el tiempo de reloj de pared del bucle de 100.000 lecturas con y sin los contadores. La diferencia es el overhead de la sonda.

Comprobarás que las actualizaciones de `counter(9)` son lo suficientemente baratas como para dejarlas en producción. Este hecho es uno de los aprendizajes prácticos más útiles del capítulo.

### Cerrando la sección 2

La medición en el kernel usa los mismos primitivos que en espacio de usuario, pero con restricciones más estrictas. Las funciones `get*` de tiempo son para marcas de tiempo baratas y de baja precisión; las funciones sin `get*` son para mediciones precisas y más costosas. Los primitivos de contador existen en tres categorías: atomics simples, `counter(9)` y variables DPCPU, con escalabilidad y flexibilidad crecientes. La exposición al espacio de usuario se realiza a través de `sysctl(9)`. Las herramientas que leen el estado expuesto (`sysctl(8)`, `top`, `systat`, `vmstat`, `ktrace`, `dtrace`, `pmcstat`) se especializan cada una en algo distinto. Y la disciplina de medir sin contaminar la medición es tan importante como los propios primitivos.

La siguiente sección presenta DTrace, la más general y capaz de las herramientas de medición del kernel que FreeBSD te ofrece. DTrace merece una sección propia porque responde mejor que ninguna otra cosa a la pregunta de *qué está ocurriendo ahora mismo*.

## Sección 3: Uso de DTrace para analizar el comportamiento del driver

DTrace es la herramienta más útil en el arsenal de un ingeniero de rendimiento del kernel. Su combinación de expresividad, bajo overhead cuando está deshabilitado y capacidad para observar casi cualquier función del kernel sin recompilar no tiene parangón. La sección 3 es la más extensa del capítulo por esta razón; el tiempo que dediques a aprender DTrace se amortiza en cada driver que midas.

La sección no presupone experiencia previa con DTrace. Los lectores que hayan usado DTrace en Solaris o macOS encontrarán la implementación de FreeBSD familiar en esencia, con algunas diferencias en la disponibilidad de proveedores y en cómo habilitarlos.

### Qué es DTrace

DTrace es una facilidad del kernel que te permite adjuntar pequeños scripts a sondas (probes). Una sonda es un punto con nombre en el kernel en el que puedes observar lo que está ocurriendo: la entrada a una función, el retorno de una función, un cambio de contexto del planificador, la finalización de una operación de I/O, la llegada de un paquete, un evento definido por el usuario en un driver. Un script indica *cuando esta sonda se dispare, haz X*. El script se compila en un bytecode seguro que se ejecuta en el contexto del kernel, se agrega en memoria y se lee de vuelta en el proceso `dtrace` del espacio de usuario.

El diseño tiene cuatro características que importan para el trabajo con drivers.

Primera, las sondas son **omnipresentes**. Cada función no estática del kernel es una sonda. También lo son los eventos del planificador, cada frontera de llamada al sistema, cada operación de lock, cada despacho de I/O y miles de otros puntos. Rara vez necesitarás añadir sondas para encontrar la que necesitas.

Segunda, las sondas tienen un **costo casi nulo cuando están deshabilitadas**. Una sonda sin usar es un NOP en el flujo de instrucciones; no ralentiza el kernel. El overhead aparece solo cuando habilitas la sonda y, aun así, el overhead en estado habilitado es típicamente de unos pocos cientos de nanosegundos por disparo. Puedes dejar tu driver listo para producción con una docena de sondas SDT y, cuando DTrace no está en ejecución, el driver es exactamente tan rápido como si las sondas no existieran.

Tercera, DTrace es **seguro**. Los scripts se ejecutan en un lenguaje restringido que prohíbe los bucles, el acceso arbitrario a memoria y cualquier operación que pudiera provocar un pánico en el sistema. Un script con errores devuelve un error; no produce un pánico.

Cuarta, DTrace **agrega en el kernel**. Puedes mantener contadores por thread, cuantiles, valores medios, histogramas y resúmenes similares en el kernel y extraer el resumen periódicamente. No necesitas emitir cada evento al espacio de usuario; la mayoría de los scripts DTrace emiten resúmenes agregados, razón por la cual escalan a millones de eventos por segundo.

### Habilitar DTrace en FreeBSD

DTrace forma parte del sistema base y funciona de fábrica en la mayoría de las instalaciones de FreeBSD, pero requiere que el kernel tenga el soporte de DTrace compilado y que los módulos correspondientes estén cargados. En un kernel GENERIC, el soporte está presente. Para usarlo, carga los módulos del proveedor:

```console
# kldload dtraceall
```

El módulo `dtraceall` es una forma cómoda de cargar todos los proveedores de DTrace. Si solo necesitas un proveedor específico (por ejemplo, `fbt` para el rastreo en los límites de función o `lockstat` para la contención de locks), puedes cargar únicamente ese:

```console
# kldload fbt
# kldload lockstat
```

Tras la carga, `dtrace -l` lista las sondas disponibles. Espera que la lista sea larga; un kernel GENERIC típico tiene decenas de miles de sondas disponibles.

### El formato del nombre de sonda

Cada sonda DTrace tiene un nombre de cuatro elementos con la forma:

```text
provider:module:function:name
```

El **proveedor** es el origen de la sonda: `fbt` para el rastreo en los límites de función, `sdt` para los puntos de traza definidos estáticamente, `profile` para el muestreo temporizado, `sched` para los eventos del planificador, `io` para la finalización de I/O, `lockstat` para los eventos de lock, y varios más.

El **módulo** es el módulo del kernel donde reside la sonda: `kernel` para el código del kernel principal, `your_driver_name` para tu propio módulo, `geom_base` para GEOM, y así sucesivamente.

La **función** es la función en la que se encuentra la sonda.

El **nombre** es el nombre de la sonda dentro de la función: para `fbt`, siempre es `entry` o `return`.

Así, una sonda como `fbt:kernel:malloc:entry` significa *el punto de entrada de la función `malloc`, en el módulo del kernel, a través del proveedor `fbt`*. Una sonda como `fbt:perfdemo:perfdemo_read:return` significa *el punto de retorno de `perfdemo_read`, en el módulo `perfdemo`*.

Se admiten comodines. `fbt:perfdemo::entry` significa *todos los puntos de entrada en el módulo `perfdemo`*. `fbt:::entry` significa *todas las entradas de función en el kernel*. Ten cuidado con este último; son muchas sondas.

### Tu primer one-liner de DTrace

La introducción canónica a DTrace consiste en contar con qué frecuencia se llama a una función. Para `perfdemo`, una vez cargado el driver:

```console
# dtrace -n 'fbt:perfdemo:perfdemo_read:entry { @[probefunc] = count(); }'
```

Esto indica: en cada entrada a cualquier función de `perfdemo` con nombre `perfdemo_read`, incrementa una agregación indexada por nombre de función. Cuando pulses Ctrl-C, DTrace imprime la agregación:

```text
  perfdemo_read                                                  10000
```

Se realizaron diez mil llamadas a `perfdemo_read` durante la ventana de rastreo. Ahora conoces la tasa de llamadas; divídela por los segundos de reloj de pared durante los que ejecutaste el rastreo.

Un one-liner algo más rico que mide el tiempo empleado en la función:

```console
# dtrace -n 'fbt:perfdemo:perfdemo_read:entry { self->t = timestamp; }
              fbt:perfdemo:perfdemo_read:return /self->t/ {
                  @t = quantize(timestamp - self->t); self->t = 0; }'
```

Esto indica: al entrar, guarda la marca de tiempo en una variable local al thread `self->t`. Al retornar, si tenemos una marca de tiempo guardada (el predicado `/self->t/`), calcula la diferencia y añádela a un histograma cuantizado. Cuando detengas el rastreo, DTrace imprime el histograma:

```text
           value  ------------- Distribution ------------- count
             512 |                                         0
            1024 |@@@@                                     1234
            2048 |@@@@@@@@@@@@@@@@@@                       6012
            4096 |@@@@@@@@@@@@@@                           4581
            8192 |@@@@                                     1198
           16384 |@                                        145
           32768 |                                         29
           65536 |                                         8
          131072 |                                         2
          262144 |                                         0
```

El histograma muestra que la función se ejecuta principalmente en el rango de 1 a 8 microsegundos, con una cola larga que llega hasta un cuarto de milisegundo. Ahora tienes la distribución, no solo la media, de la latencia de la función. Esto es ya mejor información que la que publica la mayoría de los benchmarks publicados.

### Agregaciones: el superpoder de DTrace

El agregador `quantize` es uno de los varios que proporciona DTrace. Los que usarás con más frecuencia son:

- `count()`: cuántas veces se disparó el evento.
- `sum(value)`: total de `value` a lo largo de todos los disparos.
- `avg(value)`: media de `value`.
- `min(value)`, `max(value)`: valores extremos.
- `quantize(value)`: histograma de potencias de dos.
- `lquantize(value, lower, upper, step)`: histograma lineal con límites personalizados.
- `stddev(value)`: desviación estándar.

Las agregaciones pueden indexarse. `@[probefunc] = count();` indexa por nombre de función. `@[pid] = count();` indexa por identificador de proceso. `@[execname] = sum(arg0);` suma un valor por nombre de ejecutable. Las agregaciones indexadas son el mecanismo con el que DTrace produce resúmenes estilo *top-N* sin enviar nunca datos por evento al espacio de usuario.

### Patrones útiles de DTrace para el trabajo con drivers

A continuación se presentan algunos patrones que aparecen una y otra vez en el trabajo de rendimiento de drivers. Cada uno es una plantilla lista para rellenar.

**Contar llamadas a una función por llamador:**

```console
# dtrace -n 'fbt:perfdemo:perfdemo_read:entry {
    @[stack(5)] = count();
}'
```

El argumento `stack(5)` captura la pila de cinco marcos en el momento de la sonda. La agregación te indica quién es el llamador habitual.

**Medir el tiempo que una función pasa en sí misma frente al que pasa en sus llamadas:**

```console
# dtrace -n '
fbt:perfdemo:perfdemo_read:entry { self->s = timestamp; }
fbt:perfdemo:perfdemo_read:return / self->s / {
    @total = quantize(timestamp - self->s);
    self->s = 0;
}
fbt:perfdemo:perfdemo_do_work:entry { self->w = timestamp; }
fbt:perfdemo:perfdemo_do_work:return / self->w / {
    @worktime = quantize(timestamp - self->w);
    self->w = 0;
}'
```

Ejecutar esto con un bucle de lectura concurrente te indica qué parte del tiempo de lectura se pasa dentro de `perfdemo_do_work` frente al resto de `perfdemo_read`.

**Contar errores por ubicación:**

```console
# dtrace -n 'fbt:perfdemo:perfdemo_read:return / arg1 != 0 / {
    @errors[probefunc, arg1] = count();
}'
```

El `arg1` en una sonda de retorno es el valor de retorno de la función. Si la función devuelve un errno en caso de fallo, esta agregación muestra qué errores ocurren y con qué frecuencia.

**Observar la asignación de memoria:**

```console
# dtrace -n 'fbt:kernel:malloc:entry / execname == "perfdemo" / {
    @sizes = quantize(arg0);
}'
```

Esto agrega el argumento de tamaño (`arg0`) pasado a `malloc(9)` según los contextos que se ejecutan en el módulo `perfdemo`. Responde preguntas como *¿qué tamaño tienen las asignaciones que hace mi driver?*

Estos patrones son pequeñas variaciones sobre un mismo tema. Apréndelos escribiéndolos, no leyéndolos. El laboratorio del capítulo en la Sección 3 te dará uno concreto para probar.

### Muestreo de perfiles con el proveedor `profile`

El proveedor `fbt` se dispara en cada entrada o retorno de función, lo que es exhaustivo pero ruidoso. Para preguntas del tipo *¿dónde se está yendo el tiempo de CPU?*, el proveedor `profile` suele ser mejor. Se dispara a una tasa regular (por ejemplo, 997 Hz, un número primo para no sincronizarse con la interrupción del temporizador) en cada CPU, independientemente de lo que esté ejecutándose. Un script indexado por pila del kernel ofrece un perfil estadístico de dónde pasa tiempo el kernel.

```console
# dtrace -n 'profile-997 / arg0 != 0 / {
    @[stack()] = count();
}'
```

El predicado `/arg0 != 0/` filtra las CPU inactivas (donde `arg0` es el PC del espacio de usuario, que es cero cuando hay threads del kernel en reposo). Pulsa Ctrl-C pasado un minuto y obtendrás una lista de pilas del kernel con conteos. La pila superior es donde el kernel pasó más tiempo. Envía la salida a un renderizador de flame graph (las herramientas FlameGraph se pueden instalar como port) y tendrás una vista visual y jerárquica del uso de CPU del kernel.

### Sondas estáticas con SDT

`fbt` es amplio pero no siempre la respuesta correcta. El proveedor `fbt` tiene una sonda para cada función no estática del kernel, lo que significa que puedes observar cualquier función que desees, pero también que los nombres de las sondas los genera el compilador y pueden cambiar a medida que el código evoluciona. Para puntos de observación estables y con nombre, FreeBSD ofrece el proveedor **SDT** (Statically Defined Tracepoint). Una sonda SDT se declara en el código fuente del driver, tiene un nombre estable y puede ser inspeccionada por los scripts DTrace igual que cualquier otra sonda. Se dispara solo cuando está habilitada y, cuando está deshabilitada, es un NOP.

Un driver añade sondas SDT así:

```c
#include <sys/sdt.h>

SDT_PROVIDER_DEFINE(perfdemo);
SDT_PROBE_DEFINE2(perfdemo, , , read_start,
    "struct perfdemo_softc *", "size_t");
SDT_PROBE_DEFINE3(perfdemo, , , read_done,
    "struct perfdemo_softc *", "size_t", "int");
```

El primer argumento es el nombre del proveedor (`perfdemo`). Los dos siguientes son la agrupación por módulo y función; los dejamos vacíos para obtener `perfdemo:::read_start` como nombre de sonda. Luego viene el nombre de la sonda en sí. Después, los tipos de los argumentos, uno por argumento disparado.

En el código del driver, disparas la sonda donde corresponde:

```c
static int
perfdemo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct perfdemo_softc *sc = dev->si_drv1;
    size_t want = uio->uio_resid;
    int error;

    SDT_PROBE2(perfdemo, , , read_start, sc, want);

    error = do_the_read(sc, uio);

    SDT_PROBE3(perfdemo, , , read_done, sc, uio->uio_resid, error);

    return (error);
}
```

Desde un script DTrace, las sondas tienen ahora nombres legibles:

```console
# dtrace -n 'perfdemo:::read_start { @starts = count(); }
              perfdemo:::read_done  { @done = count(); }'
```

Los nombres estables son la clave. Un cambio en la estructura interna de `perfdemo_read` no rompe el script; un usuario de DTrace puede escribir `perfdemo:::read_done` y saber exactamente qué significa, algo que no podría decir de `fbt:perfdemo:perfdemo_read:return`.

Para los drivers que se van a observar con frecuencia, un conjunto modesto de sondas SDT en los límites operacionales (lectura, escritura, ioctl, interrupción, error, desbordamiento, desbordamiento inferior) supone un costo de código muy pequeño y una gran ganancia en observabilidad.

### El proveedor `lockstat`

Uno de los providers más especializados de DTrace, `lockstat`, merece una introducción aparte porque la contención de locks es uno de los problemas de rendimiento más habituales en los drivers concurrentes. Sus probes están declaradas en `/usr/src/sys/sys/lockstat.h`. Se disparan en cada adquisición, liberación, bloqueo y spin de cada mutex del kernel, rwlock, sxlock y lock de lockmgr.

Las dos probes que utilizarás con más frecuencia son:

- `lockstat:::adaptive-acquire` y `lockstat:::adaptive-release` para operaciones de mutex simple (`mtx`).
- `lockstat:::adaptive-block` cuando un thread tuvo que dormir esperando un mutex en contención.
- `lockstat:::spin-acquire`, `lockstat:::spin-release`, `lockstat:::spin-spin` para spin mutexes.

El argumento `arg0` de una probe de lockstat es un puntero `struct lock_object *`. Para obtener el nombre del lock, usa `((struct lock_object *) arg0)->lo_name`. Un comando de una sola línea que muestra los nombres de los locks con más adquisiciones durante una ventana de medición:

```console
# dtrace -n 'lockstat:::adaptive-acquire {
    @[((struct lock_object *)arg0)->lo_name] = count();
}'
```

Uno más útil que mide cuánto tiempo estuvo *retenido* el lock:

```console
# dtrace -n '
lockstat:::adaptive-acquire {
    self->s[arg0] = timestamp;
}
lockstat:::adaptive-release / self->s[arg0] / {
    @[((struct lock_object *)arg0)->lo_name] = sum(timestamp - self->s[arg0]);
    self->s[arg0] = 0;
}'
```

Es posible que el almacenamiento local de thread crezca si se adquieren muchos locks distintos; el kernel lo limita y DTrace detendrá el script si se desborda. En la mayoría de los drivers, este script funciona con comodidad durante varios minutos antes de agotar el espacio disponible.

Una tercera variante útil rastrea la **contención**, no solo la adquisición. Un mutex adaptativo que se bloquea porque otro thread lo tiene retenido dispara `lockstat:::adaptive-block`. Contarlos por nombre de lock te muestra cuáles están realmente en contención:

```console
# dtrace -n 'lockstat:::adaptive-block {
    @[((struct lock_object *)arg0)->lo_name] = count();
}'
```

Si el mutex de tu driver aparece al principio de esta lista, has encontrado un problema de contención real. Si no aparece, los locks no son tu problema y puedes centrar la atención en otra parte. Los datos valen más que casi cualquier consejo que puedas darte a ti mismo sobre el diseño de locks en abstracto.

### Un script de DTrace más largo

Los scripts de DTrace pueden residir en archivos (`.d`) e invocarse con `dtrace -s script.d`. A continuación se muestra un script más extenso para medir el comportamiento de lectura de `perfdemo`, con agregación por proceso y un predicado para ignorar un proceso concreto que también está en ejecución en el sistema:

```c
/*
 * perfdemo-reads.d
 *
 * Aggregate perfdemo_read() timings per userland process.
 * Requires the perfdemo module to be loaded.
 */

#pragma D option quiet

fbt:perfdemo:perfdemo_read:entry
{
    self->start = timestamp;
    self->size  = args[1]->uio_resid;
}

fbt:perfdemo:perfdemo_read:return
/ self->start && execname != "dtrace" /
{
    this->dur = timestamp - self->start;
    @durations[execname] = quantize(this->dur);
    @sizes[execname]     = quantize(self->size);
    @count[execname]     = count();
    self->start = 0;
    self->size  = 0;
}

END
{
    printa("\nRead counts per process:\n%-20s %@u\n", @count);
    printa("\nRead durations (ns) per process:\n%s\n%@d\n", @durations);
    printa("\nRead request sizes per process:\n%s\n%@d\n", @sizes);
}
```

El script utiliza tres patrones de DTrace que merece la pena destacar. Primero, `args[1]->uio_resid` accede al segundo argumento de `perfdemo_read` (el `struct uio *`) y lee su campo `uio_resid`; DTrace entiende las estructuras del kernel con sus nombres de campo. Segundo, `self->...` es almacenamiento local al thread que transporta datos entre una sonda de entrada y la sonda de retorno correspondiente. Tercero, el predicado `/ self->start && execname != "dtrace" /` filtra las sondas en las que no se registró la entrada (por ejemplo, porque el script arrancó a mitad de una llamada) y excluye las lecturas que el propio DTrace hace del driver, que de otro modo distorsionarían los resultados.

Invoca el script mientras una carga de trabajo está en ejecución:

```console
# dtrace -s perfdemo-reads.d
```

Déjalo correr durante un minuto y luego pulsa Ctrl-C. Los tres listados de agregaciones al final muestran recuentos, histogramas de duración e histogramas de tamaño por proceso, todo ello desde un único script y con un efecto prácticamente nulo sobre el rendimiento de la carga de trabajo.

Con scripts como este es donde DTrace realmente brilla. Responden a preguntas que un desarrollador no habría podido responder sin recompilar el kernel hace veinte años.

### El proveedor `sched` para eventos del planificador

Uno de los proveedores menos publicitados de DTrace es `sched`, que se activa en eventos del planificador: un thread añadido a la cola de ejecución, un thread retirado de la cola de ejecución, un cambio de contexto, un thread despertado, y eventos relacionados. Para el trabajo de rendimiento en drivers, `sched` responde preguntas sobre la *latencia en el planificador*, que es la capa del sistema interpuesta entre la llamada de despertar de tu driver y el momento en que el thread de userland empieza realmente a ejecutarse.

Las sondas a las que recurrirás con más frecuencia son:

- `sched:::enqueue`: un thread se añade a la cola de ejecución.
- `sched:::dequeue`: un thread se retira de la cola de ejecución.
- `sched:::wakeup`: un thread es despertado por otro thread (a menudo desde una interrupción o un driver).
- `sched:::on-cpu`: un thread comienza a ejecutarse en una CPU.
- `sched:::off-cpu`: un thread deja de ejecutarse en una CPU.

Un uso habitual en el trabajo con drivers es medir la *latencia de despertar a ejecución*: cuánto tiempo transcurre desde que el driver llama a `wakeup(9)` en un canal de espera hasta que el thread de userland empieza a ejecutarse. Una sencilla instrucción de una línea:

```console
# dtrace -n '
sched:::wakeup / curthread->td_proc->p_comm == "mydriver_reader" / {
    self->w = timestamp;
}
sched:::on-cpu / self->w / {
    @runlat = quantize(timestamp - self->w);
    self->w = 0;
}'
```

Este script registra la marca de tiempo en el momento en que se despierta el proceso lector y de nuevo cuando comienza a ejecutarse en una CPU. La agregación muestra la distribución del retraso entre ambos momentos. En hardware típico de FreeBSD 14.3-amd64 en nuestro entorno de laboratorio, un sistema inactivo suele mostrar esta latencia por debajo del microsegundo, mientras que uno con contención la empuja hasta las pocas decenas de microsegundos. Si la latencia mediana de lectura de tu driver es inferior a 10 microsegundos pero su P99 se sitúa en los cientos, el planificador suele ser el origen de esa cola, y `sched:::wakeup` y `sched:::on-cpu` son las sondas que lo demuestran. Consulta el Apéndice F para ver notas sobre cómo reproducir esta medición con distintas configuraciones del kernel.

Otro patrón útil de `sched` consiste en contar con qué frecuencia un thread es expulsado de una CPU, clasificado por el nombre del thread:

```console
# dtrace -n '
sched:::off-cpu / curthread->td_flags & TDF_NEEDRESCHED / {
    @preempt[execname] = count();
}'
```

Esto indica qué threads están siendo expulsados por un thread de mayor prioridad, lo que en un sistema con muchas interrupciones señala a los drivers cuyos manejadores están forzando cambios de contexto. En un sistema bien comportado este recuento es bajo.

### El proveedor `io` para drivers de almacenamiento

Para los drivers en la pila de almacenamiento, el proveedor `io` resulta indispensable. Se activa en eventos de buffer-cache y bio: `io:::start` cuando se despacha un bio, `io:::done` cuando se completa, `io:::wait-start` cuando un thread empieza a esperar, `io:::wait-done` cuando la espera termina. La combinación te proporciona la latencia de extremo a extremo de cada operación de almacenamiento.

Una instrucción clásica de una línea para medir la latencia de almacenamiento:

```console
# dtrace -n 'io:::start { self->s = timestamp; }
             io:::done / self->s / {
                 @lat = quantize(timestamp - self->s);
                 self->s = 0; }'
```

El histograma que produce muestra la distribución de latencia de cada bio completado por el sistema durante la ventana de medición, en nanosegundos. En un sistema de archivos respaldado por NVMe la mediana es de decenas de microsegundos; en un SSD SATA, de cientos; en un disco de rotación, de milisegundos. Si un driver que estás investigando muestra una latencia mucho más alta que su equivalente de referencia, has acotado el problema.

Una versión más completa agrega por dispositivo y operación:

```console
# dtrace -n '
io:::start { self->s = timestamp; }
io:::done / self->s / {
    @lat[args[0]->bio_dev, args[0]->bio_cmd == BIO_READ ? "READ" : "WRITE"]
        = quantize(timestamp - self->s);
    self->s = 0;
}'
```

El script accede a los campos de la estructura bio a través de `args[0]`, algo que DTrace comprende porque los argumentos de las sondas están tipados en el kernel. Divide la distribución de latencia por dispositivo y dirección de la operación, de modo que puedes ver si las lecturas y las escrituras tienen distribuciones distintas, o si un dispositivo está arrastrando la media hacia abajo.

La verdadera fortaleza del proveedor `io` es que responde a la pregunta de la latencia *total*: el tiempo que vio la aplicación, no solo el tiempo que aportó el driver. Si tu driver es rápido pero el sistema es lento, `io` ayuda a localizar el problema.

### Combinar múltiples proveedores en un solo script

Un script no está limitado a un único proveedor. El potencial de DTrace se hace más evidente cuando combinas proveedores para responder a una pregunta que ninguno podría responder por sí solo.

Considera esta pregunta: *en un sistema donde el driver perfdemo es utilizado por dos procesos, ¿cuáles de sus lecturas provocan que se pase más tiempo en el kernel?*

Un script:

```c
/*
 * perfdemo-by-process.d
 *
 * Measures cumulative kernel time spent handling perfdemo_read()
 * per userland process, using sched + fbt providers.
 */

#pragma D option quiet

fbt:perfdemo:perfdemo_read:entry
{
    self->start = timestamp;
    self->pid   = pid;
    self->exec  = execname;
}

fbt:perfdemo:perfdemo_read:return / self->start /
{
    @total_time[self->exec] = sum(timestamp - self->start);
    @count[self->exec]      = count();
    self->start = 0;
}

END
{
    printf("\nPer-process perfdemo_read() summary:\n");
    printf("%-20s %10s %15s\n", "PROCESS", "CALLS", "TOTAL_NS");
    printa("%-20s %@10d %@15d\n", @count, @total_time);
}
```

Ejecuta este script mientras corre la carga de trabajo, pulsa Ctrl-C y verás un resumen de dos columnas que muestra qué proceso pasó más tiempo total en la ruta de lectura de tu driver. Esos son datos que un simple `top(1)` no puede ofrecerte; atribuye el tiempo en el kernel *por proceso*, no *por thread*, y únicamente para tu función concreta.

### Integrando todo: sesión de trazado de driver

Una sesión típica de DTrace para un driver que estás ajustando sigue una secuencia predecible de scripts, cada uno construido sobre el anterior. Un flujo de trabajo concreto:

1. **¿Quién llama al driver?** Ejecuta `dtrace -n 'fbt:perfdemo:perfdemo_read:entry { @[execname, pid] = count(); }'` durante treinta segundos y pulsa Ctrl-C. Esto indica qué proceso de userland y qué PID está ejercitando el driver, lo cual es contexto útil para el resto de la sesión.

2. **¿Cuánto tardan las llamadas?** Ejecuta el script quantize del capítulo. La distribución indica el rango esperado y si existe una cola.

3. **Si hay una cola, ¿qué la provoca?** Ejecuta un script que agrupe el histograma por traza de pila. Distintas trazas de pila toman caminos diferentes; verlas separadas muestra cuál es el camino lento.

4. **¿Qué lock, si alguno, está bajo contención?** Ejecuta el script `lockstat::adaptive-block`. Si tu mutex aparece entre las primeras entradas, la contención de lock es real.

5. **¿A dónde va el tiempo de CPU?** Ejecuta el script `profile-997` durante un minuto. Las trazas de pila más frecuentes indican qué funciones dominan.

6. **¿Qué hace el planificador?** Si la latencia de despertar importa, ejecuta la instrucción de una línea con `sched:::wakeup` / `sched:::on-cpu`. Si la distribución tiene una cola larga, el planificador es el origen de los retrasos visibles para el usuario.

Al final de esta secuencia tienes una imagen coherente: quién llama, con qué rapidez, adónde va el tiempo, qué bloquea y cómo gestiona el planificador los resultados. Cada script son unas pocas líneas. La secuencia es el flujo de trabajo completo de DTrace para drivers.

### Una nota sobre las limitaciones de DTrace

DTrace es potente, pero no ilimitado. Algunas limitaciones a tener en cuenta:

- El lenguaje DTrace no tiene bucles. Esto es intencionado; es lo que garantiza que las sondas terminen. Si necesitas algo que se parezca a un bucle, utiliza agregaciones en su lugar.
- No puedes modificar el estado del kernel desde un script de DTrace. Es un observador, no un depurador. Para intervenir, necesitas herramientas diferentes.
- Las sondas se activan en un contexto seguro pero restringido. No puedes, por ejemplo, llamar a funciones arbitrarias del kernel desde dentro de una sonda.
- Grandes cantidades de almacenamiento local al thread pueden desbordarse. El kernel recupera el TLS obsoleto de forma conservadora; un script que almacena datos en `self->foo` sin limpiarlos acabará agotándolo.
- Las agregaciones se mantienen en buffers por CPU y se fusionan de forma diferida. Si imprimes una agregación y sales inmediatamente, es posible que los últimos eventos no aparezcan.
- Algunos proveedores no están disponibles en VMs, en jails, o cuando las políticas MAC restringen el trazado.

El último punto es especialmente relevante en la práctica: si tu driver está siendo probado en un huésped bhyve o en una VM en la nube, es posible que algunos proveedores sencillamente no funcionen. Los proveedores `fbt` y `sdt` suelen funcionar; el proveedor `profile` depende del soporte del kernel; el proveedor `lockstat` depende de que el kernel se haya compilado con las sondas activas.

### Ejercicio 33.3: trazado DTrace de tu driver

Toma el driver `perfdemo` y escribe un script de DTrace (o una instrucción de una línea) que responda a cada una de estas preguntas:

1. ¿Cuántas lecturas por segundo realiza el driver durante una ventana de un minuto?
2. ¿Cuáles son las latencias de lectura P50, P95 y P99, en nanosegundos?
3. ¿Qué proceso de userland está realizando las lecturas?
4. ¿Duerme alguna vez el driver sobre su mutex y, si es así, durante cuánto tiempo?
5. Del tiempo de CPU en el kernel durante la medición, ¿cuánto se gasta dentro de `perfdemo` y cuánto en otros lugares?

Guarda tus scripts en el directorio del laboratorio. El acto de escribirlos es donde DTrace se convierte en algo natural.

### Cerrando la sección 3

DTrace es la herramienta predeterminada del ingeniero de rendimiento del kernel. Sus sondas están en todas partes, su coste al activarse es mínimo, sus agregaciones escalan bien y sus scripts son expresivos sin ser peligrosos. Hemos repasado el formato de nombre de las sondas, las instrucciones de una línea y las agregaciones, las sondas SDT para puntos de observación estables, el proveedor `lockstat` para la contención de locks, el muestreo con profile para la perfilación de CPU, y la estructura de un script `.d` más extenso.

La siguiente sección pasa de la observación a nivel de función a la observación a nivel de CPU. `pmcstat` y `hwpmc(4)` te proporcionan datos de contadores de hardware que DTrace no puede ofrecer: ciclos, fallos de caché, predicciones de salto erróneas. Son la herramienta adecuada cuando ya sabes qué función es costosa y necesitas entender *por qué* el hardware tarda tanto en ejecutarla.

## Sección 4: uso de pmcstat y contadores de CPU

La sección 3 te proporcionó una forma de medir el tiempo y los recuentos de eventos a nivel de función. La sección 4 trata de medir lo que el hardware de la CPU hace mientras ejecuta esas funciones: instrucciones retiradas, ciclos consumidos, fallos de caché, predicciones de salto erróneas y paradas por memoria. Estos eventos de hardware son la capa que está por debajo de la temporización a nivel de función, y explican el *por qué* detrás de un resultado sorprendente de `pmcstat`. Una función puede ser lenta porque ejecuta demasiadas instrucciones, porque espera a la memoria, porque predice mal los saltos o porque la CPU está parada esperando una dependencia. `pmcstat` te indica cuál.

La sección presenta los contadores de hardware, explica cómo los usa `pmcstat(8)`, recorre una sesión de muestreo desde la configuración hasta la interpretación, y cierra con las limitaciones de la herramienta.

### Contadores de rendimiento de hardware en una página

Las CPUs modernas incluyen un pequeño conjunto de contadores de hardware que el procesador incrementa ante eventos específicos. Los contadores son programables: eliges qué evento contar, arrancas el contador y lees el valor. Intel denomina al subsistema *Performance Monitoring Counters* (PMCs). AMD lo llama de la misma forma. ARM lo llama *Performance Monitor Unit* (PMU). Difieren en los detalles pero comparten un modelo.

En una CPU Intel o AMD x86-64 dispones normalmente de:

- Un pequeño número de contadores de función fija, cada uno asociado a un evento concreto (por ejemplo, *instructions retired* o *unhalted core cycles*).
- Un número mayor de contadores programables, que pueden configurarse para contar cualquiera de los cientos de eventos disponibles.

Cada contador es un registro de 48 o 64 bits que se incrementa en cada evento. Los eventos son específicos de cada fabricante y varían según la generación del procesador. Los eventos más habituales incluyen:

- **Ciclos**: cuántos ciclos de CPU han transcurrido. `cpu_clk_unhalted.thread` en Intel.
- **Instrucciones retiradas**: cuántas instrucciones han completado. `inst_retired.any` en Intel.
- **Referencias de caché** y **fallos de caché**: en diversos niveles de la jerarquía de memoria. `llc-misses` es una abreviatura habitualmente útil.
- **Saltos** y **predicciones erróneas de salto**.
- **Detenciones de memoria**: ciclos en los que el pipeline estaba esperando a la memoria.
- **Fallos de TLB**: ciclos en los que la traducción de virtual a físico no encontró entrada en el TLB.

Los contadores pueden usarse en dos modos. El **modo de conteo** simplemente lee el contador al final de una carga de trabajo y te da un total. El **modo de muestreo** configura el contador para que genere una interrupción cada N eventos; el manejador de interrupciones captura el PC actual y la stack, y al terminar la ejecución tienes una muestra estadística de dónde en el código han ocurrido los eventos. El modo de conteo te dice el total; el modo de muestreo te dice la distribución. Ambos son útiles.

### `hwpmc(4)` y `pmcstat(8)`

FreeBSD expone los contadores de CPU a través del módulo del kernel `hwpmc(4)` y la herramienta de userland `pmcstat(8)`. El módulo maneja el hardware; la herramienta recoge y presenta los datos. Para usarlos:

```console
# kldload hwpmc
# pmcstat -L
```

El primer comando carga el módulo. El segundo le pide a la herramienta que liste los nombres de eventos disponibles en esta máquina. En un portátil Intel Core i7, la lista tiene cientos de entradas; en una placa arm64 es más corta, pero sigue siendo sustancial.

Los nombres de eventos son la principal complicación. Intel tiene su propia nomenclatura, AMD tiene otra distinta y ARM tiene una tercera. El comando `pmcstat -L` lista los nombres para tu CPU. FreeBSD también proporciona un conjunto de eventos mnemónicos portátiles que funcionan en cualquier procesador compatible: `CPU_CLK_UNHALTED`, `INSTRUCTIONS_RETIRED`, `LLC_MISSES`, y unos pocos más. Prefiere los mnemónicos portátiles cuando tu medición no depende de un evento específico del fabricante.

### Una primera sesión de muestreo

La invocación más simple de `pmcstat` ejecuta un comando bajo un contador de muestreo:

```console
# pmcstat -S instructions -O /tmp/perfdemo.pmc sleep 30 &
# dd if=/dev/perfdemo of=/dev/null bs=4096 count=1000000 &
```

El flag `-S instructions` configura el contador para muestrear sobre `instructions` (un mnemónico portátil para instrucciones retiradas). `-O /tmp/perfdemo.pmc` le indica que escriba las muestras brutas en un archivo. `sleep 30` es la carga de trabajo; durante treinta segundos el muestreador se ejecuta. El `dd` corre en paralelo y genera carga sobre el dispositivo `perfdemo`.

Cuando `sleep 30` termina, `pmcstat` se detiene y el archivo `/tmp/perfdemo.pmc` contiene las muestras brutas. Las conviertes en un resumen con:

```console
# pmcstat -R /tmp/perfdemo.pmc -G /tmp/perfdemo.graph
```

El flag `-R` lee el archivo bruto; `-G` escribe un resumen de callgraph. El callgraph es un archivo de texto en un formato que un renderizador de flame graphs o un pipeline sencillo de `sort | uniq -c` puede consumir.

También puedes pedir una vista estilo top de las funciones más calientes:

```console
# pmcstat -R /tmp/perfdemo.pmc -T
```

que imprime una lista ordenada por recuento de muestras:

```text
 %SAMP CUM IMAGE            FUNCTION
  12.5 12.5 kernel          perfdemo_read
   8.3 20.8 kernel          uiomove
   6.9 27.7 kernel          copyout
   5.1 32.8 kernel          _mtx_lock_sleep
   ...
```

La columna `%SAMP` es la fracción de muestras que recibió la función. `perfdemo_read` dominó con un 12,5%. Le siguieron `uiomove`, `copyout` y `_mtx_lock_sleep`. Ahora ya sabes dónde centrarte. Si `perfdemo_read` hace la mayor parte del trabajo y `_mtx_lock_sleep` está entre las cinco primeras, tu driver probablemente está compitiendo por su mutex.

### Elección del evento de muestreo

`instructions` es el valor por defecto porque es portátil y generalmente útil, pero otros eventos cambian la pregunta que estás formulando. Algunas variantes útiles:

- **`-S cycles`**: muestrea sobre ciclos no detenidos. Indica dónde pasa la CPU el tiempo de reloj. Suele ser el mejor evento de partida.
- **`-S LLC-misses`** (Intel): muestrea sobre fallos de caché de último nivel. Indica qué funciones están sufriendo accesos a memoria principal.
- **`-S branches`** o **`-S branch-misses`**: indica qué funciones son calientes en el predictor de ramas.
- **`-S mem-any-ops`** (Intel): tasa de operaciones de memoria.

Un flujo de trabajo habitual es ejecutar primero una sesión con `-S cycles`, identificar una función sospechosa y luego repetir con `-S LLC-misses` para ver si esa función es caliente debido a instrucciones o a memoria.

### Callgraphs y flame graphs

Una lista de las N funciones más calientes te dice dónde está la función caliente; un callgraph te dice *cómo llegaste hasta allí*. `pmcstat -G` escribe un archivo de callgraph, y el formato convencional puede alimentar los scripts FlameGraph de Brendan Gregg para producir un flame graph en SVG. Un flame graph muestra las trazas de pila que condujeron a cada función muestreada, con un tamaño proporcional al recuento de muestras. Es la visualización de un perfil de CPU más útil que conozco, y aprender a leerlo merece un par de horas.

Una invocación práctica en FreeBSD, suponiendo que hayas instalado las herramientas FlameGraph desde el port `sysutils/flamegraph`:

```console
# pmcstat -R /tmp/perfdemo.pmc -g -k /boot/kernel > /tmp/perfdemo.stacks
# stackcollapse-pmc.pl /tmp/perfdemo.stacks > /tmp/perfdemo.folded
# flamegraph.pl /tmp/perfdemo.folded > /tmp/perfdemo.svg
```

El flag `-g` le indica a `pmcstat` que incluya los stacks del kernel; `-k /boot/kernel` le dice que resuelva los símbolos del kernel contra el kernel en ejecución. Los scripts `stackcollapse-pmc.pl` y `flamegraph.pl` provienen de las herramientas FlameGraph. Abre el SVG resultante en un navegador; cada caja es una función, su anchura es la fracción de tiempo que se pasa en ella, y la pila de cajas por debajo muestra cómo llegó la ejecución hasta allí.

### Interpretación de un resultado de PMC

Un resultado de `pmcstat` son datos brutos, no una conclusión. Tienes que razonar sobre lo que significa en el contexto de la carga de trabajo y el objetivo. Algunos patrones son lo suficientemente comunes como para reconocerlos.

**Una función en lo alto de `%SAMP` pero con un IPC bajo (instrucciones por ciclo)** probablemente está limitada por la memoria. Compara el recuento de ciclos con el de instrucciones; si los ciclos son mucho mayores que las instrucciones, la CPU está en espera. Confirma con los fallos de caché y los fallos de TLB.

**Una función en lo alto con IPC alto** está haciendo mucho trabajo en un bucle compacto. Eso es o bien un bucle caliente legítimo que debes dejar en paz, o una oportunidad de mejora algorítmica. Ejecuta los contadores `LLC-misses` y `branches` para ver si el hardware está contento.

**`_mtx_lock_sleep` o `turnstile_wait` entre las cinco primeras** es una señal de que un mutex está siendo disputado. Ejecuta `lockstat` (sección 3) para saber cuál es.

**Muchas funciones por debajo del umbral del 1% y ninguna función claramente caliente** suele significar que la sobrecarga está distribuida por toda la ruta del driver. Mira el coste total de CPU de la operación y decide si el driver está haciendo demasiado por llamada, en lugar de hacer una cosa lentamente.

**Una función que no aparece en absoluto pero que esperabas ver** puede haber sido compilada de forma inline, puede haber sido absorbida por una macro, o simplemente puede no estar en la ruta caliente. Comprueba si el compilador la ha optimizado; un rápido `objdump -d` sobre el módulo puede confirmarlo.

Estos patrones se hacen más fáciles con la práctica. Las primeras sesiones de `pmcstat` dejan a la mayoría de los lectores confundidos. A la décima, los patrones ya son familiares.

### Lectura detallada de un callgraph

Cuando ejecutas `pmcstat -R output.pmc -G callgraph.txt`, el archivo resultante es un callgraph en formato texto: la traza de pila completa de cada muestra, una por línea, en orden inverso (el frame más interno al final). Un pequeño fragmento:

```text
Callgraph for event instructions
@ 100.0% 12345 total samples
perfdemo_read    <- devfs_read_f <- dofileread <- sys_read <- amd64_syscall <- fast_syscall_common
    at 45.2% 5581 samples
perfdemo_do_work <- perfdemo_read <- devfs_read_f <- dofileread <- sys_read <- amd64_syscall <- fast_syscall_common
    at 20.1% 2481 samples
_mtx_lock_sleep  <- perfdemo_read <- devfs_read_f <- dofileread <- sys_read <- amd64_syscall <- fast_syscall_common
    at 12.8% 1580 samples
```

Cada entrada muestra la función *hoja* a la izquierda, seguida de la cadena de llamantes. El porcentaje es la fracción del total de muestras en la que se vio esa cadena de llamadas exacta. La primera entrada muestra dónde acabó la mayor parte del tiempo de CPU; la cadena de llamantes muestra *cómo* llegó el perfilador hasta allí.

Tres hábitos hacen que la lectura de callgraphs sea productiva.

Primero, **no te fíes de los porcentajes al pie de la letra en sesiones cortas**. Un recuento de muestras inferior a unos pocos cientos es ruidoso; los porcentajes basados en esos recuentos pueden variar diez puntos o más entre ejecuciones. Recoge muestras durante el tiempo suficiente para llegar a los miles antes de sacar conclusiones.

Segundo, **sigue la cadena, no solo la hoja**. Una función que aparece en lo alto puede ser una subrutina común llamada desde muchos lugares. La pregunta interesante es a menudo *¿qué llamante pasó más tiempo llamando a esta subrutina?* El callgraph responde eso directamente; la lista de funciones no.

Tercero, **trata las muestras de la raíz con precaución**. La parte superior del stack suele ser una entrada de syscall o un envoltorio de ithread. Es el llamante de interés el que te dice qué código está caliente, no el texto repetitivo común en la parte inferior del stack.

### Correlación entre pmcstat y DTrace

`pmcstat` te dice dónde pasa el tiempo la CPU. DTrace te dice qué están haciendo las funciones. Una investigación rigurosa usa ambos. Un flujo de trabajo típico:

1. Ejecuta `pmcstat -S cycles` durante un minuto mientras corre la carga de trabajo. Identifica las tres funciones más calientes.
2. Para cada función caliente, ejecuta un script de DTrace sobre sus sondas `fbt:::entry` y `fbt:::return` para obtener una tasa de llamadas y un histograma de latencia.
3. Multiplica la tasa de llamadas por la latencia media para estimar el tiempo total de CPU que recibe la función. Este número debería coincidir aproximadamente con la fracción de `pmcstat`; si no lo hace, una de las dos herramientas está dando resultados incorrectos (habitualmente por culpa de tu predicado).
4. Elige la función cuyo impacto es mayor e instrumenta su cuerpo con sondas de DTrace más granulares.

Las dos herramientas se complementan. `pmcstat` tiene la granularidad de los contadores de hardware; DTrace tiene la granularidad del cuerpo de la función y la capacidad de agregar por contexto (proceso, thread, syscall, lock). Usadas por separado, cada una cuenta la mitad de la historia. Usadas juntas, triangulan el panorama de rendimiento.

### Un método de análisis de rendimiento de arriba abajo

Para CPUs amd64, Intel publica un método de *análisis de microarquitectura de arriba abajo* (TMA) que organiza el rendimiento de la CPU en un árbol de categorías: limitado por el front-end, limitado por el back-end, mala especulación y retirada. Cada categoría tiene subcategorías que acotan el diagnóstico: limitado por memoria frente a limitado por núcleo, ancho de banda frente a latencia, predicciones erróneas de rama frente a borrados de máquina. El método es útil porque convierte una lista de cientos de eventos PMC en una pequeña jerarquía que apunta al cuello de botella.

El `pmcstat` de FreeBSD no produce un informe de arriba abajo directamente, pero puedes calcular las ratios relevantes recogiendo los eventos correctos de forma conjunta:

- **Tasa de retirada**: `UOPS_RETIRED.RETIRE_SLOTS` dividido entre el total de slots de emisión.
- **Limitado por el front-end**: ciclos de parada en el front-end divididos entre el total de ciclos.
- **Limitado por el back-end**: ciclos de parada en el back-end divididos entre el total de ciclos.
- **Mala especulación**: coste de las predicciones erróneas de rama dividido entre el total de ciclos.

Una invocación de `pmcstat` que cuenta estos eventos de forma conjunta:

```console
# pmcstat -s cpu_clk_unhalted.thread -s uops_retired.retire_slots \
          -s idq_uops_not_delivered.core -s int_misc.recovery_cycles \
          ./run-workload.sh 100000
```

Los nombres exactos de los eventos varían entre generaciones de CPU; `pmcstat -L` lista los que admite tu CPU. Calcula las ratios manualmente a partir de la salida de `pmcstat`. Si la retirada es inferior al 30% de los slots, tu código está en espera; las demás categorías acotan la causa.

Para la mayor parte del trabajo con drivers, este nivel de detalle es excesivo. La sesión de muestreo más simple con `-S cycles` identifica la función caliente, y un vistazo al código fuente de la función te dice si está dominada por accesos a memoria, aritmética, ramas o locks. Pero cuando el análisis más simple se agota (ves una función caliente y no puedes saber *por qué* lo está), el método de arriba abajo es el siguiente paso sistemático.

### Modo de conteo

`pmcstat -P` (conteo por proceso) y `pmcstat -s` (conteo en todo el sistema) son los equivalentes de conteo de `-S`. El modo de conteo es ideal cuando tienes un benchmark corto y quieres un único número. Una invocación típica:

```console
# pmcstat -s instructions -s cycles dd if=/dev/perfdemo of=/dev/null bs=4096 count=10000
```

Cuando `dd` termina, `pmcstat` imprime:

```text
p/instructions p/cycles
  2.5e9          9.8e9
```

Esto te dice que `dd` ejecutó 2500 millones de instrucciones en 9800 millones de ciclos, un IPC de aproximadamente 0,25. Eso es un IPC bajo; una CPU moderna puede sostener 3 o 4 instrucciones por ciclo con código favorable. Un IPC de 0,25 sugiere que el código está en espera con frecuencia, habitualmente por la memoria. Una segunda ejecución con `-s LLC-misses` lo confirma:

```text
p/LLC-misses p/cycles
  1.2e7        9.8e9
```

12 millones de fallos de caché sobre 9800 millones de ciclos. Divide 9800 millones entre 1,2 millones: unos 8000 ciclos por fallo de media, que es aproximadamente el coste de una lectura desde DRAM. Eso es coherente con un comportamiento limitado por la memoria.

El flujo de trabajo es: ejecuta una sesión de conteo para obtener los números globales del sistema, ejecuta una sesión de muestreo para encontrar qué función es la responsable y luego razona sobre el porqué. Cada sesión responde a una pregunta diferente.

### Por proceso frente a todo el sistema

`pmcstat -P` se adjunta a un proceso e informa exclusivamente de sus contadores. `-s` cuenta en todo el sistema. La distinción importa cuando quieres excluir otras cargas de trabajo. Una medición por proceso de `dd` te da solo los ciclos e instrucciones de `dd`, no los de todo el sistema. Una medición en todo el sistema incluye todo.

Para el trabajo con drivers, el muestreo en todo el sistema es habitualmente lo que quieres, porque el tiempo de CPU del driver se cuenta en threads del kernel que no son el proceso del benchmark. Usa `pmcstat -s` con la recogida de stacks del kernel cuando quieras ver funciones del kernel, y `pmcstat -P` cuando quieras centrarte en un único proceso de userland.

### `pmcstat` y la virtualización

Los PMC de hardware en máquinas virtuales son un asunto complicado. Algunos hipervisores exponen los PMC del host a los guests (KVM con soporte `vPMC`, Xen con PMC passthrough); otros exponen un subconjunto filtrado; otros no exponen nada en absoluto. El `hwpmc(4)` de FreeBSD informará de errores en los eventos que el hardware subyacente no expone. Si `pmcstat -L` te devuelve una lista corta en tu VM, o si `pmcstat -S cycles` falla con un error, probablemente estés en un entorno con PMC restringidos.

La alternativa es el proveedor `profile` de DTrace. Toma muestras a una frecuencia fija en lugar de basarse en eventos de hardware, por lo que funciona en cualquier entorno en que lo haga el kernel, incluidos los entornos fuertemente virtualizados. Sus resultados son menos granulares (no puedes tomar muestras sobre fallos de caché, por ejemplo), pero te indica en qué se invierte el tiempo de CPU, que es, al fin y al cabo, la pregunta más habitual.

### Ejercicio 33.4: pmcstat `perfdemo`

En una máquina FreeBSD con el driver `perfdemo` cargado y el módulo `hwpmc` disponible, ejecuta estas tres sesiones y registra los resultados en tu cuaderno de notas:

1. Modo de conteo: `pmcstat -s instructions -s cycles <tu invocación de dd>`. Anota el IPC.
2. Modo de muestreo: `pmcstat -S cycles -O /tmp/perfdemo.pmc <tu carga de trabajo>`, después `pmcstat -R /tmp/perfdemo.pmc -T`. Anota las cinco funciones más frecuentes.
3. Si tu sistema lo permite, `pmcstat -S LLC-misses -O /tmp/perfdemo-miss.pmc <tu carga de trabajo>`, después `pmcstat -R /tmp/perfdemo-miss.pmc -T`. Compara con las muestras de ciclos.

Si `hwpmc` no está disponible, sustituye las sesiones de muestreo por el proveedor `profile-997` de DTrace con `@[stack()] = count();`. Los resultados serán menos precisos, pero igualmente instructivos.

### Cerrando la sección 4

Los contadores de rendimiento hardware son la capa que hay por debajo de los tiempos a nivel de función. `hwpmc(4)` los expone, `pmcstat(8)` los gestiona, y el proveedor `profile` de DTrace es la alternativa portable. El modo de conteo ofrece totales, el modo de muestreo ofrece distribuciones, y un flame graph convierte las muestras en una forma que el ojo humano puede leer. Los datos son crudos; la interpretación requiere práctica. Pero una vez que los patrones resultan familiares, una sesión con `pmcstat` responde preguntas que ninguna otra herramienta puede.

Con esto concluimos la primera mitad del capítulo, dedicada a la medición. Las secciones 1 a 4 te enseñaron cómo saber qué está haciendo tu driver. Las secciones 5 a 8 se centran en qué hacer con ese conocimiento: cómo aplicar buffering, alineamiento y asignación para maximizar el rendimiento; cómo mantener los manejadores de interrupción rápidos; cómo exponer métricas en tiempo de ejecución; y cómo entregar el resultado optimizado.

## Sección 5: Buffering y optimización de memoria

La memoria es donde se esconde un número sorprendente de problemas de rendimiento en los drivers. Una función que parece correcta sobre el papel puede pasar la mayor parte de sus ciclos esperando a la memoria, agotando la caché, peleando con otra CPU por una línea compartida, o consumiendo el asignador de memoria porque libera y reasigna en cada llamada. Las técnicas que resuelven estos problemas son bien conocidas y están bien respaldadas en FreeBSD; lo que requiere experiencia es reconocer cuándo aplicar cada una.

Esta sección cubre cinco temas sobre memoria, en orden de especificidad creciente: líneas de caché y false sharing, alineamiento para DMA y eficiencia de caché, buffers preasignados, zonas UMA y contadores por CPU.

### Líneas de caché y false sharing

Una CPU moderna no lee la memoria byte a byte. Lee en unidades llamadas **línea de caché**, típicamente 64 bytes en x86-64 y en arm64 (algunas implementaciones arm64 utilizan líneas de 128 bytes; consulta `CACHE_LINE_SIZE` en `/usr/src/sys/arm64/include/param.h` para tu arquitectura objetivo). Cada vez que la CPU lee un byte, lee la línea de caché entera en su caché de datos L1. Cada vez que escribe un byte, modifica la línea de caché entera.

Esto casi siempre es una ventaja: la localidad espacial implica que los próximos bytes que leas suelen estar en la misma línea. Pero también crea un problema sutil cuando dos campos de una misma línea de caché son escritos por distintas CPUs. Las cachés de las CPUs deben coordinarse; cada vez que una CPU escribe la línea, la copia de la otra CPU queda invalidada, y la línea rebota entre ellas. El coste es medible y, con alta concurrencia, puede ser dominante. Este fenómeno se denomina **false sharing**, porque las dos CPUs no comparten datos realmente (escriben campos distintos), pero el protocolo de coherencia de caché las trata como si lo hicieran.

La estructura softc de un driver es un lugar habitual donde aparece el false sharing. Si un softc tiene dos contadores, uno actualizado por el camino de lectura y otro por el de escritura, y ambos residen en la misma línea de caché, cada lectura y cada escritura provocan un rebote en la caché. Con poca concurrencia esto es invisible. Con alta concurrencia puede reducir el rendimiento a la mitad.

La solución es el alineamiento explícito a línea de caché. El macro `CACHE_LINE_SIZE` de FreeBSD indica el tamaño de la línea de caché para la arquitectura actual, y el atributo de compilador `__aligned` sitúa una variable en el alineamiento correcto. Para aislar un campo en su propia línea de caché:

```c
struct perfdemo_softc {
    struct mtx          mtx;
    /* ... other fields ... */

    uint64_t            read_ops __aligned(CACHE_LINE_SIZE);
    char                pad1[CACHE_LINE_SIZE - sizeof(uint64_t)];

    uint64_t            write_ops __aligned(CACHE_LINE_SIZE);
    char                pad2[CACHE_LINE_SIZE - sizeof(uint64_t)];
};
```

Esta es una forma de hacerlo, y deja la separación explícita para el lector. Una forma más limpia, cuando la estructura va a asignarse de forma individual en lugar de como array, es colocar cada contador caliente en su propia subestructura alineada, o usar `counter(9)`, que gestiona internamente el emplazamiento por CPU y evita el problema.

No es necesario rellenar cada campo de cada estructura. En la mayoría de los casos, el softc se asigna una sola vez por dispositivo y lo accede una única CPU a la vez, y el alineamiento no importa. Recurre al alineamiento a línea de caché cuando:

- Un perfil muestre un comportamiento similar al false sharing (muchos `LLC-misses` en una función que no parece intensiva en memoria, o IPC bajo en una función que debería estar limitada por la CPU).
- Se sabe que varias CPUs escriben distintos campos de la misma estructura de forma concurrente.
- Hayas medido una diferencia. Como siempre.

### Alineamiento para DMA

Los motores DMA suelen requerir buffers alineados a una frontera mayor que una línea de caché: 512 bytes para DMA de disco, 4 KB para cierto hardware de red, a veces más. Si asignas un buffer con `malloc(9)` y lo pasas a DMA sin comprobar el alineamiento, estás confiando en el alineamiento por defecto del asignador, que normalmente es suficiente en amd64 pero no está garantizado en todas las arquitecturas.

Para DMA, la API `bus_dma(9)` de FreeBSD es la interfaz correcta. Gestiona el alineamiento, los bounce buffers y la dispersión-concentración (scatter-gather) por ti. Lo vimos en el capítulo 21. La nota relevante aquí es simplemente que bus_dma gestiona el alineamiento de forma explícita, y un driver no debería asignar memoria para DMA con `malloc(9)` a secas y esperar que funcione.

Una preocupación relacionada es el **alineamiento para SIMD o código vectorizado**, donde el compilador o la ISA pueden requerir alineamiento de 16 o 32 bytes en ciertos argumentos. De nuevo, `__aligned` es la herramienta adecuada. Para buffers que asignes tú mismo, `contigmalloc(9)` o el asignador de zonas UMA pueden producir memoria alineada.

### Buffers preasignados

Todo driver tiene un hot path (camino caliente). La regla cardinal para un hot path es que no debe asignar memoria. La asignación de memoria es cara en cualquier asignador del kernel: `malloc(9)` toma un lock y recorre listas; `uma(9)` almacena elementos en caché por CPU pero tiene un camino de respaldo al asignador de slabs; `contigmalloc(9)` puede bloquearse en operaciones a nivel de página. Ninguno de ellos pertenece a una función que se ejecuta miles de veces por segundo.

La solución es la **preasignación**. Asigna la memoria que necesita tu hot path en `attach()`, no en el propio hot path. Si necesitas un número fijo de buffers para un ring, asígnalos una sola vez. Si necesitas un pool de buffers que se reutilicen, asigna el pool en attach y mantén una lista libre. Si necesitas un buffer más grande de forma ocasional, asígnalo en un camino frío.

Para `perfdemo`, la preasignación podría tener este aspecto:

```c
#define PERFDEMO_RING_SIZE      64
#define PERFDEMO_BUFFER_SIZE    4096

struct perfdemo_softc {
    struct mtx      mtx;
    char           *ring[PERFDEMO_RING_SIZE];
    int             ring_head;
    int             ring_tail;
    /* ... */
};

static int
perfdemo_attach(device_t dev)
{
    struct perfdemo_softc *sc = device_get_softc(dev);
    int i;

    for (i = 0; i < PERFDEMO_RING_SIZE; i++) {
        sc->ring[i] = malloc(PERFDEMO_BUFFER_SIZE, M_PERFDEMO, M_WAITOK);
    }
    /* ... */
    return (0);
}

static int
perfdemo_detach(device_t dev)
{
    struct perfdemo_softc *sc = device_get_softc(dev);
    int i;

    for (i = 0; i < PERFDEMO_RING_SIZE; i++) {
        if (sc->ring[i] != NULL) {
            free(sc->ring[i], M_PERFDEMO);
            sc->ring[i] = NULL;
        }
    }
    return (0);
}
```

En el hot path, el driver toma un buffer del ring, lo usa y lo devuelve. Sin asignador, sin lock más allá del propio ring, y sin riesgo de fallo de asignación bajo carga.

Este es el patrón que distingue a los drivers que rinden bien bajo presión de los que se desmoronan cuando aumenta la presión de memoria. Un driver que asigna en el hot path acabará fallando en la asignación en el momento de mayor estrés, y su camino de error se ejercitará precisamente entonces. Un driver que preasigna falla en el momento del attach, donde el error puede notificarse de forma limpia.

### Zonas UMA

`malloc(9)` es un asignador de propósito general muy válido, pero para objetos de tamaño fijo que se asignan y liberan con frecuencia no es óptimo. FreeBSD ofrece el framework de zonas UMA (Universal Memory Allocator) para este caso. UMA proporciona cachés por CPU, de modo que un par `uma_zalloc()` / `uma_zfree()` dentro del thread de una misma CPU es normalmente un intercambio de unos pocos punteros sin locks. Las declaraciones se encuentran en `/usr/src/sys/vm/uma.h`.

Una zona se crea una vez por driver al cargar el módulo y se destruye al descargarlo:

```c
#include <vm/uma.h>

static uma_zone_t perfdemo_buffer_zone;

static int
perfdemo_modevent(module_t mod, int what, void *arg)
{
    switch (what) {
    case MOD_LOAD:
        perfdemo_buffer_zone = uma_zcreate("perfdemo_buffer",
            PERFDEMO_BUFFER_SIZE, NULL, NULL, NULL, NULL,
            UMA_ALIGN_CACHE, 0);
        if (perfdemo_buffer_zone == NULL)
            return (ENOMEM);
        break;
    case MOD_UNLOAD:
        uma_zdestroy(perfdemo_buffer_zone);
        break;
    }
    return (0);
}
```

Observa el flag `UMA_ALIGN_CACHE`. Solicita a UMA que alinee cada elemento de la zona a una frontera de línea de caché, lo que importa cuando los elementos se usan desde varias CPUs. El macro está definido en `/usr/src/sys/vm/uma.h`.

En el hot path, la asignación y liberación tienen este aspecto:

```c
void *buf;

buf = uma_zalloc(perfdemo_buffer_zone, M_WAITOK);
/* use buf */
uma_zfree(perfdemo_buffer_zone, buf);
```

El flag `M_WAITOK` indica que "esta llamada puede dormirse esperando memoria". En un camino que no puede dormirse, usa `M_NOWAIT` y gestiona un retorno NULL. Para un hot path que no puede bloquearse en absoluto, también puedes usar `M_NOWAIT | M_ZERO` y mantener un pool de respaldo.

UMA es la herramienta correcta cuando el driver asigna y libera objetos del mismo tamaño repetidamente. Para asignaciones puntuales de tamaño variable, `malloc(9)` sigue siendo perfectamente válido. Para rings de buffers preasignados, UMA es excesivo; un array simple es más claro.

La regla general: si estás asignando lo mismo cientos de veces por segundo, merece una zona UMA.

### Un recorrido por las interioridades de UMA

El comportamiento interno de UMA vale la pena entenderlo porque explica tanto sus ventajas de rendimiento como los casos en los que *no* ayuda. El asignador de zonas tiene tres capas: cachés por CPU, listas globales por zona, y el asignador de slabs por debajo de todo.

La caché por CPU es un pequeño array de elementos libres, uno por CPU. Cuando una CPU llama a `uma_zalloc`, UMA consulta primero su propia caché; si hay un elemento disponible, la llamada es un simple intercambio de puntero sin locks. El tamaño por defecto de la caché lo ajusta UMA en función del tamaño del elemento y del número de CPUs de la máquina, y puede modificarse con `uma_zone_set_maxcache()` cuando una zona concreta necesita un límite diferente.

Cuando la caché por CPU está vacía, UMA la rellena desde la lista libre global de la zona. Este camino toma un lock, extrae varios elementos de una vez y retorna. Es más costoso que el camino de caché, pero sigue siendo mucho más barato que una llamada completa al asignador.

Cuando la lista libre global de la zona también está vacía, UMA llama al asignador de slabs para extraer más elementos de una página nueva. Este es el camino más costoso e implica asignar una página física, particionarla en elementos del tamaño del slab y poblar la lista libre de la zona. La mayoría de las llamadas en el hot path de una zona nunca llegan a esta capa; la caché por CPU y la lista global de la zona se alcanzan antes.

De aquí se derivan tres consecuencias.

Primera, **UMA es más rápido cuando las asignaciones y las liberaciones ocurren en la misma CPU**. Un par `uma_zalloc` / `uma_zfree` en una misma CPU es un intercambio de unos pocos punteros. Un `uma_zalloc` en la CPU 0 seguido de un `uma_zfree` en la CPU 1 devuelve el elemento a la caché de la CPU 1, lo que puede beneficiar o no a las asignaciones futuras de la CPU 0. Si tu driver asigna en una CPU y libera en otra de forma habitual, la ventaja por CPU se diluye.

Segunda, **las zonas se pueden ajustar con `uma_zone_set_max()` y `uma_prealloc()`**. La preasignación reserva un número fijo de elementos al crear la zona, de modo que las primeras cientos de asignaciones no lleguen nunca al camino de slabs. El límite máximo acota la zona y hace que las asignaciones posteriores fallen con `M_NOWAIT` en lugar de permitir un crecimiento ilimitado. Ambas opciones son útiles para drivers que necesitan un comportamiento de memoria predecible.

Tercera, **`vmstat -z` es tu ventana a UMA**. Lista cada zona, el tamaño por elemento, el recuento actual, el recuento acumulado y el recuento de fallos. Una zona cuyo recuento actual crece sin un camino de liberación correspondiente es una fuga. Una zona con un recuento de fallos no nulo está bajo presión de memoria. Aprende a leer `vmstat -z` con fluidez; es la mejor herramienta individual para diagnosticar problemas con UMA.

Las interioridades se encuentran en `/usr/src/sys/vm/uma_core.c` para el asignador principal y en `/usr/src/sys/vm/uma.h` para la interfaz pública. Leer el archivo de interfaz es suficiente para la mayoría del trabajo con drivers; el archivo del núcleo es donde mirar cuando un modo de fallo no se corresponde con lo esperado.

### Bounce buffers y mapeo de memoria DMA

Los dispositivos con capacidad DMA necesitan memoria físicamente contigua y apta para DMA. En amd64 toda la memoria tiene capacidad DMA por defecto (suponiendo que el dispositivo no esté restringido por IOMMU), pero en otras arquitecturas y en sistemas protegidos por IOMMU la situación es más compleja. La API `bus_dma(9)` de FreeBSD oculta esa complejidad, pero el autor de un driver necesita saber cuándo entran en juego los bounce buffers.

Un **bounce buffer** es un buffer intermedio que el kernel utiliza cuando un buffer proporcionado por el usuario no es apto para DMA. Si el motor DMA del dispositivo no puede acceder a una dirección física determinada (por ejemplo, el dispositivo es de 32 bits y el buffer se encuentra por encima de 4 GB), el kernel asigna un bounce buffer en memoria accesible, copia los datos hacia o desde él, y apunta el motor DMA a ese bounce buffer. Esto es transparente para el driver, pero no es gratuito: cada operación con bounce incluye una copia en memoria.

El impacto en el rendimiento se manifiesta en dos lugares. En primer lugar, la copia duplica el tráfico de memoria de la operación, lo cual importa cuando la operación es sensible al ancho de banda. En segundo lugar, el pool de bounce buffers tiene un tamaño acotado; bajo presión, las asignaciones fallan y el driver debe esperar. Un driver que usa `bus_dma(9)` en un sistema de 64 bits con memoria abundante rara vez encuentra bounce buffers; un driver de 32 bits en un sistema con memoria por encima de 4 GB los encuentra constantemente.

Las opciones de ajuste disponibles cuando los bounce buffers se convierten en un coste medible:

- Comprueba si el dispositivo admite DMA de 64 bits. Muchos dispositivos de la era de 32 bits implementan en realidad direccionamiento de 64 bits, y el driver puede habilitarlo con los flags adecuados de `bus_dma_tag_create()`.
- Usa `BUS_SPACE_UNRESTRICTED` en el campo `lowaddr` del tag si el dispositivo realmente no tiene restricciones, para indicarle a `bus_dma` que no haga bounce.
- En el caso de dispositivos genuinamente de 32 bits que deben funcionar en un sistema de 64 bits, considera preasignar buffers en memoria accesible desde el driver y copiar allí en la ruta de despacho, lo que al menos sitúa el coste del bounce en un lugar conocido.

Este tema merece un capítulo propio; el Capítulo 21 cubre `bus_dma(9)` en profundidad. El punto aquí es que los bounce buffers son una característica de rendimiento que quizás no hayas elegido conscientemente, y cuando aparecen en un perfil, ahora ya sabes lo que significan.

### Contadores por CPU revisitados

La sección 2 introdujo `counter(9)` y `DPCPU_DEFINE(9)`. Esta sección es donde esas primitivas importan más. En un driver con muchos CPUs escribiendo todos al mismo contador, la línea de caché del contador se convierte en un punto de contención. `counter(9)` lo evita manteniendo una copia por CPU y sumando al leer.

Un ejemplo antes y después hace el punto más concreto. Supongamos que `perfdemo` tiene un único contador `atomic_add_64()` para el total de lecturas, actualizado en cada lectura. En un sistema de 8 núcleos ejecutando un benchmark de lectores en paralelo, la línea de caché del contador rebota entre ocho cachés L1. Un perfil de `pmcstat` muestra `atomic_add_64` sorprendentemente alto entre las funciones muestreadas. Cambiar a `counter_u64_add()`:

- Cada CPU actualiza su propia copia. Sin contención.
- La línea de caché que cada CPU escribe está en su propia L1 de forma exclusiva.
- La ruta de lectura suma las copias de todos los CPUs, lo que solo ocurre cuando alguien lee el sysctl.

El resultado es que el coste del contador en el hot path baja de cientos de nanosegundos por actualización (el cache miss más la operación atómica) a unos pocos nanosegundos (una escritura directa en una línea de caché local a la CPU). En un sistema de 8 núcleos, esto puede suponer una reducción del coste del contador de 10x.

Una plantilla práctica para un driver con varios contadores por CPU:

```c
#include <sys/counter.h>

struct perfdemo_stats {
    counter_u64_t reads;
    counter_u64_t writes;
    counter_u64_t bytes;
    counter_u64_t errors;
};

static struct perfdemo_stats perfdemo_stats;

static void
perfdemo_stats_init(void)
{
    perfdemo_stats.reads  = counter_u64_alloc(M_WAITOK);
    perfdemo_stats.writes = counter_u64_alloc(M_WAITOK);
    perfdemo_stats.bytes  = counter_u64_alloc(M_WAITOK);
    perfdemo_stats.errors = counter_u64_alloc(M_WAITOK);
}

static void
perfdemo_stats_free(void)
{
    counter_u64_free(perfdemo_stats.reads);
    counter_u64_free(perfdemo_stats.writes);
    counter_u64_free(perfdemo_stats.bytes);
    counter_u64_free(perfdemo_stats.errors);
}
```

Cada campo es un `counter_u64_t`, que internamente es un puntero a almacenamiento por CPU. Cada incremento es una suma por CPU. Cada lectura es una suma de todos los CPUs.

Para estado que no es un contador, DPCPU es la herramienta:

```c
#include <sys/pcpu.h>

DPCPU_DEFINE_STATIC(struct perfdemo_cpu_state, perfdemo_cpu);

/* In a fast path, on the current CPU: */
struct perfdemo_cpu_state *s = DPCPU_PTR(perfdemo_cpu);
s->last_read_ns = now;
s->read_count++;
```

DPCPU es algo menos cómodo que `counter(9)` porque tienes que definir e inicializar tú mismo la estructura por CPU, pero te ofrece la flexibilidad de un estado por CPU arbitrario en lugar de solo un contador.

### Patrones de datos por CPU más allá de los contadores

`counter(9)` gestiona el caso común de acumulación de enteros por CPU. DPCPU gestiona el caso general de cualquier cosa por CPU. En la práctica, hay cuatro patrones que aparecen de forma recurrente.

**Patrón 1: Caché por CPU de un buffer temporal**. Cuando un driver necesita un pequeño buffer temporal en un hot path, y el buffer siempre se usa en una CPU a la vez, un puntero DPCPU a un buffer preasignado por CPU evita cualquier asignación:

```c
DPCPU_DEFINE_STATIC(char *, perfdemo_scratch);

static int
perfdemo_init_scratch(void)
{
    int cpu;
    CPU_FOREACH(cpu) {
        char *p = malloc(PERFDEMO_SCRATCH_SIZE, M_PERFDEMO, M_WAITOK);
        if (p == NULL)
            return (ENOMEM);
        DPCPU_ID_SET(cpu, perfdemo_scratch, p);
    }
    return (0);
}

/* On the hot path: */
critical_enter();
char *s = DPCPU_GET(perfdemo_scratch);
/* ... use s ... */
critical_exit();
```

El par `critical_enter()` / `critical_exit()` es necesario porque el planificador podría expulsar al thread entre el `DPCPU_GET` y el uso, y el thread podría reanudarse en una CPU diferente. Permanecer en una sección crítica evita la migración. El coste es pequeño (unos pocos nanosegundos) pero no nulo; úsalo siempre que accedas a datos DPCPU que deban ser estables durante una operación.

**Patrón 2: Estado por CPU para estadísticas sin locks**. Algunos drivers mantienen ventanas deslizantes, valores del último timestamp u otro estado por CPU. DPCPU te proporciona el almacenamiento por CPU sin ninguna sincronización: cada CPU lee y escribe su propia copia, y un resumen global se calcula bajo demanda.

```c
struct perfdemo_cpu_stats {
    uint64_t last_read_ns;
    uint64_t cpu_time_ns;
    uint32_t current_queue_depth;
};

DPCPU_DEFINE_STATIC(struct perfdemo_cpu_stats, perfdemo_cpu_stats);

/* Fast path: */
critical_enter();
struct perfdemo_cpu_stats *s = DPCPU_PTR(perfdemo_cpu_stats);
s->last_read_ns = sbttons(sbinuptime());
s->cpu_time_ns += elapsed;
critical_exit();
```

El resumen para reportar:

```c
static uint64_t
perfdemo_total_cpu_time(void)
{
    uint64_t total = 0;
    int cpu;
    CPU_FOREACH(cpu) {
        struct perfdemo_cpu_stats *s =
            DPCPU_ID_PTR(cpu, perfdemo_cpu_stats);
        total += s->cpu_time_ns;
    }
    return (total);
}
```

**Patrón 3: Colas por CPU, unidireccionales**. Un driver que difiere trabajo a un taskqueue puede mantener una lista pendiente por CPU a la que el CPU productor añade elementos y que el consumidor vacía periódicamente. Como cada productor es el único escritor de su lista por CPU, no se necesitan locks en el lado del productor. El consumidor todavía necesita locks o intercambios atómicos para vaciar de forma segura, pero el hot path no necesita locks.

**Patrón 4: Configuración por CPU**. Algunos drivers parametrizan su comportamiento por CPU por razones de escalabilidad: cada CPU tiene su propio tamaño de buffer, su propio número de reintentos, sus propios límites blandos. DPCPU hace que esto sea natural. La contrapartida es que los valores de configuración están dispersos entre CPUs; reportarlos requiere un bucle que visite cada CPU.

Estos patrones comparten un tema común: el estado por CPU elimina la sincronización entre CPUs en el hot path, a cambio de un pequeño coste en la ruta de agregación. Cuando el hot path es rápido y la agregación es poco frecuente, la contrapartida es claramente favorable.

### Cuándo no alinear, preasignar ni cachear

Cada técnica descrita tiene un coste. La alineación rellena las estructuras y desperdicia memoria. La preasignación inmoviliza buffers que pueden quedar sin usar. Las zonas UMA consumen memoria del kernel. Los contadores por CPU usan `N * CPU_COUNT` bytes de memoria donde el driver podría haberse librado con `N * 1`. Ninguno de estos costes es fatal, pero en conjunto se acumulan, especialmente en sistemas pequeños.

La regla general es la que escucharás a lo largo del capítulo: no apliques estas técnicas hasta que la medición lo exija. Un driver que funciona a 1000 operaciones por segundo en un escritorio inactivo no se beneficia de la alineación de caché, las zonas UMA ni los contadores por CPU. Un driver en un servidor de almacenamiento de 64 núcleos que procesa 10 millones de operaciones por segundo puede necesitar las tres.

Una autocomprobación útil antes de añadir cualquiera de estas técnicas: *¿puedo señalar un resultado concreto de `pmcstat`, una agregación de DTrace o un número de benchmark que muestre que el código actual es lento de la manera que la técnica corregiría?* Si la respuesta es sí, aplícala. Si no, no lo hagas.

### Ejercicio 33.5: Ajuste de línea de caché y por CPU

Usando el driver `perfdemo`, haz lo siguiente:

1. Línea base: mide el rendimiento de `perfdemo_read()` con la implementación actual de contadores atómicos, usando un lector multithreaded que ejecute un thread por CPU. Registra el rendimiento en ops/seg.
2. Añade `__aligned(CACHE_LINE_SIZE)` a los contadores atómicos en el softc y añade relleno para aislarlos. Reconstruye, recarga, vuelve a ejecutar. Registra el nuevo rendimiento.
3. Sustituye los contadores atómicos por contadores `counter(9)`. Reconstruye, recarga, vuelve a ejecutar. Registra el nuevo rendimiento.
4. Compara los tres números.

El objetivo no es que un enfoque sea siempre el mejor. El objetivo es que los tres enfoques producen números diferentes en hardware diferente, y la evidencia te dice cuál es el correcto para tu objetivo.

### Cerrando la sección 5

El ajuste de memoria consiste en comprender la jerarquía de memoria contra la que trabaja la CPU. La alineación de línea de caché evita el false sharing. La alineación DMA funciona a través de `bus_dma(9)`. La preasignación mantiene el hot path fuera del asignador. Las zonas UMA especializan las asignaciones frecuentes de tamaño fijo. Los contadores por CPU y el estado por CPU eliminan la contención de línea de caché en las métricas compartidas. Cada técnica es una herramienta en el conjunto de herramientas, y cada una merece evidencia antes de aplicarse.

La siguiente sección examina la otra gran fuente de cuellos de botella en el rendimiento de los drivers: el manejo de interrupciones y el trabajo diferido a través de taskqueues.

## Sección 6: Optimización de interrupciones y taskqueues

Las interrupciones son donde el driver se encuentra con el hardware, y para muchos drivers es ahí donde aparece el primer gran obstáculo de rendimiento. Un manejador de interrupciones se ejecuta en un contexto restringido. No puede dormir. No puede adquirir la mayoría de los locks que permiten dormir. Compite con otros threads por tiempo de CPU, pero también los expulsa. Un manejador que tarda demasiado paraliza el sistema completo. Un manejador que se dispara con demasiada frecuencia consume CPU que el resto del sistema necesita. Las técnicas de esta sección tratan de mantener el manejador de interrupciones rápido y mover el resto del trabajo a un lugar donde pueda realizarse con tranquilidad.

La sección cubre cuatro temas: medir el comportamiento de las interrupciones, los filter handlers frente a los ithread handlers (un breve resumen), mover trabajo a taskqueues y la coalescencia de interrupciones.

### Medir el comportamiento de las interrupciones

Antes de cambiar nada, necesitas saber con qué frecuencia se disparan las interrupciones y cuánto tiempo tarda el manejador. FreeBSD te proporciona ambas métricas a través de herramientas estándar.

**La tasa de interrupciones** se muestra con `vmstat -i`:

```console
# vmstat -i
interrupt                          total       rate
irq1: atkbd0                          10          0
irq9: acpi0                      1000000        500
irq11: em0 ehci0+                  44000         22
irq16: ohci0 uhci1+                 1000          0
cpu0:timer                       2000000       1000
cpu1:timer                       2000000       1000
cpu2:timer                       2000000       1000
cpu3:timer                       2000000       1000
Total                            8148010       4547
```

La columna `total` es acumulativa desde el boot; la columna `rate` es por segundo durante la ventana de muestreo. Si la interrupción de tu driver se dispara a miles de hercios y no lo esperabas, algo va mal. Si se dispara a un solo dígito por segundo y esperabas miles, algo más va mal.

**La latencia del manejador de interrupciones** se muestra con DTrace. El proveedor `fbt` tiene sondas en cada función no estática, incluido el manejador de interrupciones del driver. Un one-liner que mide cuánto tarda el manejador:

```console
# dtrace -n 'fbt:perfdemo:perfdemo_intr:entry { self->t = timestamp; }
              fbt:perfdemo:perfdemo_intr:return /self->t/ {
                  @ = quantize(timestamp - self->t);
                  self->t = 0; }'
```

El histograma resultante te indica la distribución de latencia de tu manejador de interrupciones. Un manejador con una mediana de uno o dos microsegundos y un P99 inferior a diez tiene un comportamiento correcto. Un manejador con una mediana de 100 microsegundos y un P99 en milisegundos está haciendo demasiado en el contexto equivocado.

**La detección de tormentas de interrupciones** está integrada en FreeBSD. Cuando una interrupción se dispara con demasiada frecuencia (el umbral predeterminado es 100.000 por segundo), el kernel desactiva temporalmente la fuente de interrupción y registra una advertencia:

```text
interrupt storm detected on "irq18:"; throttling interrupt source
```

Si ves este mensaje en `dmesg`, tu driver o bien está procesando la interrupción incorrectamente (sin reconocerla, dejándola dispararse de nuevo de inmediato) o el hardware está generando a una tasa que el driver no puede seguir. Cualquiera de los dos casos es un error; no es un estado normal.

### Filter handlers frente a ithread handlers

El capítulo 13 cubrió las dos formas del manejador de interrupciones en detalle. Esta sección se apoya en ellos; aquí tienes un breve resumen para contextualizar.

Un **filter handler** se ejecuta en el contexto de interrupción del propio hardware. Tiene restricciones muy estrictas: no puede dormir, no puede adquirir la mayoría de los locks, y debe hacer el trabajo mínimo necesario para callar el hardware e indicar al kernel qué hacer a continuación. Devuelve uno de `FILTER_HANDLED`, `FILTER_STRAY` o `FILTER_SCHEDULE_THREAD`. El último indica al kernel que ejecute el ithread handler asociado.

Un **ithread handler** se ejecuta en un thread del kernel dedicado, planificado poco después de que el filter devuelva `FILTER_SCHEDULE_THREAD`. Puede adquirir locks que permiten dormir, realizar trabajo más complejo, y tardar tanto como sea razonable sin paralizar el sistema. Sigue siendo expulsable, pero no es el contexto de interrupción.

El diseño de dos niveles te permite dividir el manejo de interrupciones de forma limpia. El filter hace lo mínimo: leer el registro de estado, determinar si la interrupción es nuestra, reconocerla para que el hardware deje de asertarla, y planificar el ithread si queda trabajo. El ithread hace el resto: procesar los datos, despertar al espacio de usuario, liberar buffers, actualizar contadores.

Para el rendimiento, la pregunta es: ¿necesitas ambos niveles? La respuesta es casi siempre sí para drivers que manejan hardware real, y con frecuencia no para dispositivos simples. Un diseño solo con filter es rápido: el manejador se ejecuta una vez por interrupción, hace su trabajo y devuelve. Un diseño de filter más ithread añade el coste de planificación del thread (unos pocos microsegundos) pero permite que el trabajo principal se ejecute con menos restricciones de contexto. Si tu filter handler es rápido y completo, mantén el diseño solo con filter. Si es grande y necesitas locks que permiten dormir, divide en filter e ithread. Si el ithread en sí mismo es rápido y la división solo añade sobrecarga, consolida. Las mediciones te dirán en qué caso te encuentras.

### Mover trabajo a taskqueues

Un patrón común para el trabajo de larga duración del driver es sacarlo completamente del contexto de interrupción y llevarlo a un taskqueue. Un taskqueue es una cola simple con nombre de funciones a ejecutar, planificada por un thread del kernel dedicado. Encolas una tarea desde el manejador de interrupciones; el thread la desencola y la ejecuta; la interrupción retorna rápidamente.

El patrón básico:

```c
#include <sys/taskqueue.h>

struct perfdemo_softc {
    struct task perfdemo_task;
    /* ... */
};

static void
perfdemo_task_handler(void *arg, int pending)
{
    struct perfdemo_softc *sc = arg;
    /* long-running work here; can acquire sleepable locks */
}

static int
perfdemo_attach(device_t dev)
{
    struct perfdemo_softc *sc = device_get_softc(dev);

    TASK_INIT(&sc->perfdemo_task, 0, perfdemo_task_handler, sc);
    /* ... */
    return (0);
}

static void
perfdemo_intr(void *arg)
{
    struct perfdemo_softc *sc = arg;

    /* Quick filter work */

    taskqueue_enqueue(taskqueue_fast, &sc->perfdemo_task);
}
```

El filter handler hace su trabajo mínimo y luego planifica una tarea en `taskqueue_fast`, el fast-taskqueue predeterminado del kernel. La tarea se ejecuta en el thread del taskqueue, que es expulsable y puede dormir. El manejador de interrupciones retorna tan rápido como puede.

La cola `taskqueue_fast` es compartida por muchos subsistemas. En un sistema con carga elevada, esto puede significar que tu tarea espera detrás de otras. Si necesitas un taskqueue propio, crea uno:

```c
struct taskqueue *my_tq;

/* In module init: */
my_tq = taskqueue_create("perfdemo_tq", M_WAITOK,
    taskqueue_thread_enqueue, &my_tq);
taskqueue_start_threads(&my_tq, 1, PI_NET, "perfdemo tq thread");
```

`taskqueue_start_threads` crea un thread vinculado a la cola. Para un driver con trabajo intensivo en CPU en un sistema multinúcleo, puedes usar `taskqueue_start_threads_cpuset()` para fijar threads a CPUs específicas, lo que mejora la localidad de caché:

```c
#include <sys/cpuset.h>

cpuset_t cpus;

CPU_ZERO(&cpus);
CPU_SET(1, &cpus);   /* bind to CPU 1 */
taskqueue_start_threads_cpuset(&my_tq, 1, PI_NET, &cpus,
    "perfdemo tq thread");
```

La ventaja de un taskqueue dedicado es la planificación predecible. Tu tarea no compite con todo el sistema por tiempo de thread en `taskqueue_fast`. La desventaja es un mayor número de threads y más uso de memoria, así que recurre a esto solo cuando el taskqueue compartido esté causando un problema real.

### Coalescencia de interrupciones

Las interrupciones de hardware tienen un coste fijo: guardar el estado del CPU, hacer la transición al contexto del kernel, despachar el manejador y retornar. En el hardware moderno esto está por debajo de un microsegundo, pero no es cero. Cuando un dispositivo genera un número muy elevado de interrupciones por segundo, el coste acumulado se vuelve apreciable. Las tarjetas de red en un enlace de 10 Gbps pueden generar cientos de miles de interrupciones por segundo en momentos de carga máxima; los controladores de almacenamiento NVMe pueden generar aún más.

La **coalescencia de interrupciones** es la técnica de pedir al hardware que agrupe múltiples eventos en una sola interrupción. En lugar de interrumpir una vez por paquete, la tarjeta de red interrumpe una vez por milisegundo, y el driver gestiona todos los paquetes recibidos en ese milisegundo. El rendimiento aumenta porque el coste por evento disminuye; la latencia también aumenta, porque los eventos esperan más tiempo en el lote.

No todo driver admite coalescencia, y es una característica del hardware. Cuando el hardware la ofrece (la mayoría de las tarjetas de red modernas y los controladores NVMe sí lo hacen), el driver expone controles a través de sysctl o ioctl. El equilibrio debe ajustarse a la carga de trabajo: una carga sensible a la latencia necesita ventanas de coalescencia cortas; una carga orientada al rendimiento necesita ventanas largas. A veces un driver expone controles separados para las dos direcciones (RX y TX), lo que permite un ajuste más granular.

Como autor de un driver, tienes tres opciones en relación con la coalescencia:

1. **Usar la coalescencia por hardware.** Si el hardware la tiene, expón un control sysctl. El operador ajusta el driver para su carga de trabajo.
2. **Hacer agrupación por software.** Si el hardware no la tiene y el driver puede procesar eventos en grupos de forma útil, el driver puede mantener el trabajo pendiente hasta alcanzar un umbral y luego despachar el lote completo. `iflib(9)` hace esto para muchos drivers de red.
3. **No hacer nada.** Si la tasa de eventos es lo suficientemente baja como para que la coalescencia no aporte nada, o el presupuesto de latencia es tan ajustado que perjudicaría, déjalo como está.

La elección correcta es, de nuevo, la que indiquen las mediciones. Si `vmstat -i` muestra que tu driver genera interrupciones a cientos de miles de hercios y el tiempo de CPU en la ruta de interrupción representa una fracción apreciable del total de CPU, vale la pena probar la coalescencia. Si la interrupción se produce a una cifra de un solo dígito por segundo, la coalescencia es irrelevante.

### Antirrebote

Un concepto relacionado es el **antirrebote** (debouncing): filtrar las interrupciones redundantes o demasiado próximas entre sí. Los botones de entrada GPIO son el ejemplo clásico; un interruptor mecánico produce muchas interrupciones rápidas al ser pulsado o liberado, y el driver debe filtrar las que no son cambios de estado reales. La técnica consiste en:

1. Al recibir una interrupción, leer la entrada y registrar la marca de tiempo.
2. Si la entrada coincide con el último estado notificado y la marca de tiempo está dentro de la ventana de debouncing, ignorarla.
3. En caso contrario, notificar el cambio y actualizar el último estado notificado.

Una marca de tiempo de `sbinuptime()` y una ventana de debouncing de 20 milisegundos son suficientes para la mayoría de los interruptores mecánicos. Para otras fuentes de eventos, el umbral es específico del dominio.

El debouncing no suele denominarse ajuste de rendimiento, pero tiene el mismo efecto: menos interrupciones gestionadas, menos CPU consumida, menos eventos espurios notificados. Es una técnica pequeña pero efectiva.

### Threads dedicados para trabajo prolongado

El kernel permite que un driver tenga su propio thread de larga duración, distinto tanto de los threads de taskqueue como de los threads de interrupción. Usa `kproc_create(9)` o `kthread_add(9)` para crearlo. Un thread dedicado tiene sentido cuando:

- El driver tiene un bucle de larga duración (por ejemplo, un bucle de polling que reemplaza las interrupciones para obtener una temporización determinista).
- El trabajo no debe competir con otros subsistemas en un taskqueue compartido.
- El trabajo tiene su propio ritmo que no coincide con el de las interrupciones (por ejemplo, un vaciado periódico, un watchdog o un driver de máquina de estados).

Para la mayoría de los drivers, un taskqueue es más sencillo y suficiente. Un thread dedicado supone un incremento de complejidad y debe justificarse por la naturaleza del trabajo.

### MSI y MSI-X: interrupciones por vector

Un dispositivo clásico utiliza una única línea de interrupción: cada evento dispara la misma interrupción, el manejador debe distinguirlos leyendo un registro de estado, y el manejador se ejecuta en la CPU que elija el balanceador de interrupciones del kernel. Un dispositivo moderno admite interrupciones señalizadas por mensaje (MSI) o la variante extendida MSI-X, que permite al dispositivo enviar muchas interrupciones distintas, cada una con su propio vector y cada una configurable para una CPU específica.

Para un autor de drivers, MSI-X ofrece tres ventajas de rendimiento respecto a las interrupciones de línea clásicas.

En primer lugar, **las interrupciones por vector eliminan los manejadores compartidos**. Con las interrupciones clásicas, el manejador de filtro de un driver puede dispararse por eventos de otros drivers y debe devolver `FILTER_STRAY` cuando el evento no es propio. Con MSI-X, cada vector pertenece a un único driver; el manejador de filtro solo se ejecuta cuando su dispositivo tiene trabajo pendiente.

En segundo lugar, **la asignación de CPU por vector reduce el rebote de líneas de caché**. Un driver de red con múltiples colas puede dedicar un vector RX a la CPU 0, otro a la CPU 1, y así sucesivamente. Cada CPU procesa su propia cola, accediendo a sus propias estructuras de datos, sin compartir líneas de caché con las demás CPU. En un driver de alto rendimiento, esto escala linealmente con el número de núcleos.

En tercer lugar, **el enrutamiento de interrupciones reduce la latencia**. Si un driver sabe qué CPU consumirá los datos de un paquete, puede solicitar una interrupción directamente en esa CPU, de modo que los datos llegan con la caché ya caliente.

En FreeBSD, MSI-X se solicita mediante `pci_alloc_msix(9)` durante `attach()`. El driver solicita N vectores; el subsistema PCI asigna hasta N (o menos si el hardware admite menos). Cada vector es un IRQ estándar de `bus_alloc_resource()` que se configura con `bus_setup_intr(9)` como de costumbre. El framework `iflib(9)` encapsula la maquinaria para los drivers de red; para drivers personalizados, la API directa solo requiere unas pocas líneas más.

A continuación se muestra un pequeño fragmento de un driver que asigna cuatro vectores MSI-X:

```c
static int
mydrv_attach(device_t dev)
{
    struct mydrv_softc *sc = device_get_softc(dev);
    int n = 4;

    if (pci_alloc_msix(dev, &n) != 0 || n < 4) {
        device_printf(dev, "cannot allocate 4 MSI-X vectors\n");
        return (ENXIO);
    }

    for (int i = 0; i < 4; i++) {
        sc->irq[i] = bus_alloc_resource_any(dev, SYS_RES_IRQ,
            &(int){i + 1}, RF_ACTIVE);
        if (sc->irq[i] == NULL)
            return (ENXIO);
        if (bus_setup_intr(dev, sc->irq[i], INTR_TYPE_NET | INTR_MPSAFE,
                NULL, mydrv_intr, &sc->queue[i], &sc->ih[i]) != 0)
            return (ENXIO);
    }

    /* Optionally, pin each IRQ to a CPU: */
    for (int i = 0; i < 4; i++) {
        int cpu = i;
        bus_bind_intr(dev, sc->irq[i], cpu);
    }

    return (0);
}
```

El manejador por vector `mydrv_intr` solo se ejecuta cuando *su* cola tiene trabajo pendiente; recibe el puntero softc de su cola como argumento. La llamada a `bus_bind_intr()` fija cada vector a una CPU específica; en un sistema con más CPU que vectores, se puede asignar cada vector a una CPU diferente.

MSI-X no siempre es una ventaja. En un dispositivo de una sola cola no hay ningún beneficio, solo configuración adicional. En un dispositivo con una tasa de interrupciones baja, la diferencia es insignificante. Pero en cualquier dispositivo moderno de alto rendimiento (tarjeta de red de 10 Gbps, unidad NVMe), MSI-X es el modelo estándar de interrupciones, y usar cualquier otra alternativa supone dejar rendimiento sobre la mesa.

### El polling como alternativa a las interrupciones

En el extremo superior del rendimiento, algunos drivers abandonan las interrupciones por completo en favor del *polling*. Un driver de polling ejecuta un bucle compacto que comprueba si el hardware tiene trabajo pendiente, lo procesa de inmediato y vuelve a empezar. El coste es en CPU: un thread de polling consume una CPU continuamente, incluso cuando está inactivo. El beneficio es la eliminación del coste de las interrupciones: a tasas de eventos elevadas, el coste de la interrupción y el cambio de contexto por evento desaparece.

La pila de red de FreeBSD admite polling mediante `ifconfig polling`. Los drivers de almacenamiento no suelen usar polling. Para drivers personalizados, el polling solo merece consideración cuando:

- La tasa de eventos es extremadamente alta (millones por segundo).
- La latencia es tan crítica que la latencia de interrupción (inferior a un microsegundo) resulta excesiva.
- Los recursos de CPU son abundantes (dedicar un núcleo al bucle de polling es aceptable).

La mayoría de los drivers no cumplen ninguno de estos criterios, y el polling es la elección incorrecta. Pero conocer su existencia te permite reconocer cuándo un perfil muestra que el coste de las interrupciones es dominante, y te proporciona una alternativa de último recurso.

Un punto intermedio es el *polling adaptativo*: alternar entre el modo guiado por interrupciones y el modo de polling en función de la carga. El agrupamiento estilo NAPI (denominado así por el subsistema de Linux que lo popularizó, pero ampliamente utilizado en `iflib` de FreeBSD) toma la primera interrupción, deshabilita las interrupciones posteriores, hace polling hasta vaciar la cola y luego las vuelve a habilitar. Esto captura la mayor parte de la eficiencia del polling a tasas altas, manteniendo bajo el coste en tiempo de inactividad.

### El agrupamiento estilo NAPI en la práctica

El framework `iflib(9)` implementa el agrupamiento estilo NAPI de forma automática. Un driver que usa `iflib` recibe paquetes a través de un callback que hace polling en la cola de hardware hasta vaciarla. Para los drivers que no usan `iflib`, el patrón es sencillo de implementar manualmente:

```c
static int
mydrv_filter(void *arg)
{
    struct mydrv_softc *sc = arg;

    /* Disable hardware interrupts for this queue. */
    mydrv_hw_disable_intr(sc);

    /* Schedule the ithread or taskqueue to drain. */
    return (FILTER_SCHEDULE_THREAD);
}

static void
mydrv_ithread(void *arg)
{
    struct mydrv_softc *sc = arg;
    int drained = 0;

    while (mydrv_hw_has_work(sc) && drained < MYDRV_POLL_BUDGET) {
        mydrv_process_one(sc);
        drained++;
    }

    if (!mydrv_hw_has_work(sc)) {
        /* Queue is empty; re-enable interrupts. */
        mydrv_hw_enable_intr(sc);
    } else {
        /* Budget hit with work remaining; reschedule. */
        taskqueue_enqueue(sc->tq, &sc->task);
    }
}
```

El filtro deshabilita la interrupción y programa el ithread. El ithread hace polling de hasta `POLL_BUDGET` eventos, luego comprueba si la cola está vacía. Si lo está, vuelve a habilitar la interrupción. Si no, se reprograma a sí mismo para continuar vaciando la cola en el siguiente ciclo. El presupuesto evita que una única ráfaga monopolice la CPU; la comprobación de cola vacía evita el polling perpetuo cuando el tráfico se detiene.

El agrupamiento estilo NAPI es una buena opción para drivers de tasa media-alta donde ni las interrupciones puras ni el polling puro son ideales. El presupuesto y la lógica de reactivación son los dos puntos donde se producen errores; el presupuesto debe ser lo bastante grande para amortizar el coste de reactivar la interrupción, pero lo bastante pequeño como para no bloquear a otros drivers, y la reactivación debe producirse antes de que el hardware pueda perder el rastro del trabajo pendiente.

### Distribución del presupuesto entre etapas

Un ejercicio útil para cualquier driver crítico en cuanto a rendimiento es escribir un *presupuesto de latencia* a lo largo de las etapas del driver. El presupuesto total es el plazo de la operación (por ejemplo, 100 microsegundos para un paquete de red de baja latencia). Se resta el coste esperado de cada etapa, y lo que queda es el margen disponible.

Para un driver de red que recibe un paquete:

- Despacho de interrupción: 1 µs.
- Manejador de filtro: 2 µs.
- Programación e inicio del ithread: 3 µs.
- Procesamiento del paquete (capa de protocolo): 20 µs.
- Despertar al lector en espacio de usuario: 5 µs.
- Despacho del planificador: 5 µs.
- Copia en espacio de usuario y acuse de recibo: 10 µs.

Total: 46 µs. Con un presupuesto de 100 µs, quedan 54 µs de margen. Si el driver empieza a incumplir su plazo, el presupuesto te indica qué etapa es la más probable que haya superado su coste, y dónde medir primero.

Los números anteriores son ilustrativos; los valores reales dependen del hardware y de la carga de trabajo. El hábito de escribir el presupuesto antes de medir es lo que hace eficiente el proceso de medición. Empiezas con una hipótesis sobre dónde va el tiempo, la confirmas o refutas con las herramientas de las secciones 2 a 4, y refinas el presupuesto a medida que el driver evoluciona.

### Ejercicio 33.6: latencia de interrupción frente a taskqueue

Con el driver `perfdemo`, añade una segunda variante que simule el procesamiento similar a una interrupción usando un callout de alta resolución. Compara dos configuraciones:

1. Todo el procesamiento realizado directamente en el manejador del callout (similar a una interrupción, en un contexto privilegiado).
2. El callout encola una tarea en un taskqueue; el thread del taskqueue realiza el procesamiento.

Mide la latencia de extremo a extremo (el tiempo desde la interrupción simulada hasta que los datos procesados son visibles en el espacio de usuario) en ambos casos. Un script de DTrace puede registrar las marcas de tiempo; un lector en espacio de usuario con `nanosleep()` sobre un `select()` puede observar la entrega.

La configuración 1 tendrá normalmente menor latencia y mayor concentración de CPU en el contexto del callout. La configuración 2 tendrá una latencia ligeramente mayor, pero un uso de CPU más distribuido entre threads. Cuál es mejor depende de la carga de trabajo; el ejercicio consiste en ver el equilibrio con números reales, no en declarar uno como universalmente correcto.

### Cerrando la sección 6

Las interrupciones son el punto donde el driver se encuentra con el hardware, y son donde aparecen primero los precipicios de rendimiento. Medir la tasa de interrupciones y la latencia del manejador es sencillo con `vmstat -i` y DTrace. La división filtro más ithread mantiene pequeño el contexto de interrupción; los taskqueues mueven el trabajo a un entorno más tranquilo; los threads dedicados ofrecen aislamiento para casos especiales; la coalescencia y el debouncing controlan la propia tasa de interrupciones. Cada técnica tiene un coste, y cada una merece evidencia antes de aplicarse.

La siguiente sección aborda las métricas y el registro que dejas en el driver una vez concluido el ajuste: el árbol sysctl que expone el estado en tiempo de ejecución, las llamadas a `log(9)` que notifican las condiciones que merecen ser registradas, y los patrones que distinguen un driver listo para producción de uno pensado solo para benchmarks.

## Sección 7: Uso de sysctl y logging para métricas en tiempo de ejecución

Las técnicas de medición de las secciones 2 a 6 son instrumentos a los que acudes cuando surge una pregunta concreta. Las métricas de la sección 7 son las que permanecen en el driver para siempre. Son como las luces del cuadro de mandos de un coche: siempre visibles, siempre actualizadas, siempre disponibles para decirle a un operador futuro qué está haciendo el driver. Un driver sin un buen árbol sysctl es un driver que un operador no puede diagnosticar después de que el autor original haya dejado el proyecto. Un driver con uno bueno es un driver que comunica su estado siempre que alguien lo solicita.

Esta sección aborda el diseño del árbol sysctl, las formas habituales de métricas, el logging con limitación de tasa mediante `log(9)`, y el arte de incluir la cantidad adecuada de observabilidad en un driver que se distribuye.

### Diseño de un árbol sysctl

El árbol sysctl es jerárquico. Cada nodo tiene un nombre, un tipo, un conjunto de indicadores y, habitualmente, un valor. Los nodos de nivel superior (`kern`, `net`, `vm`, `hw`, `dev` y algunos otros) los fija el kernel; un driver crea un subárbol bajo uno de ellos. Para un driver de hardware, la ubicación convencional es `dev.<driver_name>.<unit>.<metric>`. Para un pseudodispositivo o un driver que no es de hardware, lo habitual es `hw.<driver_name>.<metric>`.

Las declaraciones utilizan las macros de `/usr/src/sys/sys/sysctl.h`. Un subárbol mínimo de driver:

```c
SYSCTL_NODE(_dev, OID_AUTO, perfdemo, CTLFLAG_RD, 0,
    "perfdemo driver");

SYSCTL_NODE(_dev_perfdemo, OID_AUTO, stats, CTLFLAG_RD, 0,
    "Statistics");

SYSCTL_U64(_dev_perfdemo_stats, OID_AUTO, reads, CTLFLAG_RD,
    &perfdemo_reads_cached, 0, "Total read() calls");
```

El primer `SYSCTL_NODE` crea `dev.perfdemo`. El segundo crea `dev.perfdemo.stats`. El `SYSCTL_U64` crea `dev.perfdemo.stats.reads` y lo apunta a una variable `uint64_t`. Desde el espacio de usuario:

```console
# sysctl dev.perfdemo.stats.reads
dev.perfdemo.stats.reads: 12345
```

Para un dispositivo con múltiples instancias, la forma más idiomática es usar `device_get_sysctl_ctx(9)` y `device_get_sysctl_tree(9)` dentro del método `attach()`. FreeBSD ya habrá creado `dev.perfdemo.0` y `dev.perfdemo.1` para dos instancias, y estos helpers te dan acceso para añadir nodos hijos bajo cada uno:

```c
static int
perfdemo_attach(device_t dev)
{
    struct perfdemo_softc *sc = device_get_softc(dev);
    struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(dev);
    struct sysctl_oid *tree = device_get_sysctl_tree(dev);

    SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "reads",
        CTLFLAG_RD, &sc->stats.reads_cached, 0,
        "Total read() calls");
    SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "errors",
        CTLFLAG_RD, &sc->stats.errors_cached, 0,
        "Total errors");
    /* ... */
    return (0);
}
```

El ctx y el tree se liberan automáticamente cuando el dispositivo se desconecta, por lo que no tienes que limpiar nada manualmente. Este es el patrón estándar para las métricas por dispositivo.

### Exposición de valores de `counter(9)` mediante sysctl

Un `counter_u64_t` es un contador por CPU, y exponerlo a través de sysctl requiere un poco más de trabajo que un `uint64_t` simple. El patrón es un *sysctl procedimental*: un sysctl que ejecuta una función al ser leído. La función obtiene la suma del contador y la escribe en el buffer del sysctl.

```c
static int
perfdemo_sysctl_counter(SYSCTL_HANDLER_ARGS)
{
    counter_u64_t *cntp = arg1;
    uint64_t val = counter_u64_fetch(*cntp);

    return (sysctl_handle_64(oidp, &val, 0, req));
}

SYSCTL_PROC(_dev_perfdemo_stats, OID_AUTO, reads,
    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    &perfdemo_stats.reads, 0, perfdemo_sysctl_counter, "QU",
    "Total read() calls");
```

El indicador `CTLFLAG_MPSAFE` indica que el manejador no necesita ser serializado mediante el lock del subsistema sysctl. `"QU"` es la cadena de tipo: `Q` para quad (64 bits) y `U` para sin signo. El manejador se llama en cada lectura, de modo que la suma siempre está actualizada.

Varios drivers modernos encapsulan esto en una macro auxiliar; consulta `/usr/src/sys/net/if.c` para ver un ejemplo de `SYSCTL_ADD_COUNTER_U64`. Si añades muchos sysctls de contador, merece la pena definir un helper similar para tu driver.

### Qué exponer

El árbol sysctl de un driver es una interfaz. Como cualquier interfaz, debe ser cuidadosa: expón suficiente para ser útil, pero no tanto que el árbol se vuelva desordenado y sus valores pierdan significado. Un buen conjunto inicial para un driver de plano de datos:

- **Operaciones totales**: lecturas, escrituras, ioctls, interrupciones. Contadores acumulativos.
- **Errores totales**: errores transitorios (reintentos), errores permanentes (fallos), errores de transferencia parcial.
- **Bytes transferidos**: total de bytes transferidos en cada dirección.
- **Estado actual**: profundidad de cola, indicador de ocupado, modo.
- **Configuración**: umbral de agrupación de interrupciones, tamaño del buffer, nivel de depuración.

Para cada uno, piensa si un operador querría ese valor para diagnosticar un problema. Si es así, exponlo. Si no, mantenlo interno.

Evita exponer detalles de implementación que el driver pueda cambiar. El árbol sysctl es una API; cambiar el nombre de un nodo rompe scripts y paneles de monitorización. Elige los nombres con cuidado y mantenlos estables.

### Solo lectura, lectura-escritura y tunable

El indicador `CTLFLAG_RD` hace que un sysctl sea de solo lectura desde el espacio de usuario. El indicador `CTLFLAG_RW` lo hace escribible, de modo que un operador puede cambiar el comportamiento del driver en tiempo de ejecución. `CTLFLAG_TUN` marca un sysctl como **tunable**, lo que significa que su valor inicial se puede establecer en `/boot/loader.conf` antes de que el módulo se cargue.

Los sysctls escribibles son flexibles y peligrosos. Un operador puede establecer un nivel de depuración, cambiar el tamaño de un buffer o activar y desactivar un indicador de función. El driver debe validar cuidadosamente el valor escrito; un tamaño de buffer fuera de rango puede corromper el estado. Para la mayoría de las métricas, solo lectura es la elección correcta. Para la configuración, el patrón es lectura-escritura con validación en un manejador procedimental.

A continuación se muestra un tunable validado que acepta valores entre 16 y 4096:

```c
static int perfdemo_buffer_size = 1024;

static int
perfdemo_sysctl_bufsize(SYSCTL_HANDLER_ARGS)
{
    int new_val = perfdemo_buffer_size;
    int error;

    error = sysctl_handle_int(oidp, &new_val, 0, req);
    if (error != 0 || req->newptr == NULL)
        return (error);
    if (new_val < 16 || new_val > 4096)
        return (EINVAL);
    perfdemo_buffer_size = new_val;
    return (0);
}

SYSCTL_PROC(_dev_perfdemo, OID_AUTO, buffer_size,
    CTLTYPE_INT | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_sysctl_bufsize, "I",
    "Buffer size in bytes (16..4096)");
```

El manejador comprueba `req->newptr != NULL` para distinguir una escritura de una lectura (una lectura tiene `newptr` distinto de null solo si se proporcionó un valor). Valida el rango y actualiza la variable. El indicador `CTLFLAG_RWTUN` combina `CTLFLAG_RW` y `CTLFLAG_TUN`: escribible en tiempo de ejecución y ajustable desde el loader.

Una regla que vale la pena interiorizar: *cualquier sysctl que modifique el estado del driver debe validar su entrada*. La alternativa es un fallo configurable por el usuario.

### Logging con limitación de tasa mediante `log(9)`

El `printf(9)` del kernel es rápido pero indisciplinado: cada llamada produce una línea en `dmesg` independientemente de la tasa. Para mensajes informativos que pueden dispararse con frecuencia, el kernel proporciona `log(9)`, que etiqueta el mensaje con un nivel de prioridad (`LOG_DEBUG`, `LOG_INFO`, `LOG_NOTICE`, `LOG_WARNING`, `LOG_ERR`, `LOG_CRIT`, `LOG_ALERT`, `LOG_EMERG`) y permite que `syslogd(8)` del espacio de usuario lo filtre. Un mensaje con `LOG_DEBUG` solo se registra si syslogd está configurado para aceptar mensajes de depuración; la configuración predeterminada los descarta.

```c
#include <sys/syslog.h>

log(LOG_DEBUG, "perfdemo: read of %zu bytes returning %d\n",
    size, error);
```

La línea sigue pasando por el buffer de log del kernel y tiene el mismo coste de producción que un `printf(9)`, pero no aparece en `dmesg` a menos que se indique a syslogd que incluya mensajes de depuración. Es la herramienta adecuada para mensajes de diagnóstico que un operador puede querer en ocasiones pero que no deben contaminar el log predeterminado.

Para mensajes que realmente deben tener limitación de tasa, FreeBSD dispone de `ppsratecheck(9)` y `ratecheck(9)`. Devuelven un valor distinto de cero como máximo N veces por segundo; úsalos para controlar una impresión:

```c
static struct timeval perfdemo_last_err;

if (error != 0 && ratecheck(&perfdemo_last_err,
    &(struct timeval){1, 0})) {
    device_printf(dev, "transient error %d\n", error);
}
```

Esto limita la impresión a una vez por segundo. El `struct timeval` en el segundo argumento es el intervalo; `{1, 0}` significa un segundo. Si la tasa de errores es de mil por segundo, obtienes una línea de log por segundo en lugar de mil.

El logging con limitación de tasa es el patrón adecuado para cualquier mensaje que pueda dispararse de forma plausible en una ruta caliente. Un driver que registra cada error transitorio a plena velocidad puede sufrir un DoS a través del propio `dmesg`.

### `device_printf(9)` y sus variantes

Para mensajes que identifican el dispositivo concreto, usa `device_printf(9)`:

```c
device_printf(sc->dev, "attached at %p\n", sc);
```

Antepone el nombre del dispositivo y el número de unidad automáticamente: `perfdemo0: attached at 0xfffffe00c...`. Esta es la convención para cualquier mensaje que de otro modo necesitaría incluir `sc->dev` en su cadena de formato. Todos los drivers de FreeBSD usan `device_printf(9)` para sus mensajes de attach y detach; sigue ese patrón.

Para mensajes que no pertenecen a un dispositivo concreto, el `printf(9)` simple es correcto, aunque debe etiquetarse con el nombre del módulo: `printf("perfdemo: %s: ...\n", __func__, ...)`. El identificador `__func__` es un elemento integrado de C99 que se expande al nombre de la función actual; hace que los logs sean mucho más fáciles de rastrear hasta su origen.

### Modos de depuración

Un patrón habitual en drivers maduros es un **modo de depuración**: un sysctl escribible que controla la verbosidad del logging. En el nivel de depuración 0, el driver registra solo attach, detach y errores reales. En el nivel 1 registra errores transitorios. En el nivel 2 registra cada operación. El patrón es económico (una comparación con cero en la ruta caliente) y ofrece a los operadores una forma segura de activar el logging detallado al diagnosticar un problema.

```c
static int perfdemo_debug = 0;
SYSCTL_INT(_dev_perfdemo, OID_AUTO, debug, CTLFLAG_RWTUN,
    &perfdemo_debug, 0,
    "Debug level (0=errors, 1=transient, 2=verbose)");

#define PD_DEBUG(level, sc, ...) do {                    \
    if (perfdemo_debug >= (level))                       \
        device_printf((sc)->dev, __VA_ARGS__);           \
} while (0)
```

Se usa de la siguiente manera:

```c
PD_DEBUG(2, sc, "read %zu bytes\n", bytes);
PD_DEBUG(1, sc, "transient error %d\n", error);
```

Con el valor predeterminado `perfdemo_debug = 0`, ambas líneas son una simple comparación con cero que el predictor de ramas gestiona bien. No se produce ningún mensaje. Con `perfdemo_debug = 1`, solo se produce la línea de error transitorio. Con `perfdemo_debug = 2`, se producen ambas.

El operador lo activa con:

```console
# sysctl dev.perfdemo.debug=2
```

y lo desactiva cuando termina. Esta es una convención presente en muchos drivers de FreeBSD; adóptala.

### Seguimiento del comportamiento a lo largo del tiempo

Un contador que se consulta cada segundo te da una tasa. Un sysctl que devuelve un histograma de latencias recientes te da una distribución. Para el seguimiento a más largo plazo, puedes mantener una ventana deslizante de valores dentro del driver, expuesta a través de un sysctl que devuelve el array:

```c
#define PD_LAT_SAMPLES 60

static uint64_t perfdemo_recent_lat[PD_LAT_SAMPLES];
static int perfdemo_lat_idx;

static void
perfdemo_lat_record(uint64_t ns)
{
    perfdemo_recent_lat[perfdemo_lat_idx] = ns;
    perfdemo_lat_idx = (perfdemo_lat_idx + 1) % PD_LAT_SAMPLES;
}

static int
perfdemo_sysctl_recent_lat(SYSCTL_HANDLER_ARGS)
{
    return (SYSCTL_OUT(req, perfdemo_recent_lat,
        sizeof(perfdemo_recent_lat)));
}

SYSCTL_PROC(_dev_perfdemo, OID_AUTO, recent_lat,
    CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_sysctl_recent_lat, "S",
    "Last 60 samples of read latency (ns)");
```

El patrón funciona para cualquier ventana deslizante: latencias, profundidades de cola, tasas de interrupción, lo que el operador quiera ver en orden temporal.

Las formas más sofisticadas de la misma idea almacenan estimaciones de percentiles (P50, P95, P99) mediante algoritmos de flujo como las medias móviles ponderadas exponencialmente o el t-digest. Para la mayoría de los drivers esto es excesivo; basta con una ventana deslizante simple, o incluso un histograma acumulativo almacenado como un array de cubos.

### Exposición de histogramas mediante sysctl

Un histograma acumulativo es una de las métricas de larga duración más útiles que un driver puede exponer. El espacio de usuario puede consultarlo, restar la instantánea anterior a la actual y calcular la tasa por cubo. Representar el resultado a lo largo del tiempo ofrece una vista inmediata de la distribución de latencia del driver.

El patrón: declara un array de `counter_u64_t`, uno por cubo. Actualiza el cubo correcto en la ruta caliente (búsqueda lineal o categorización en tiempo constante). Expón el array mediante un único sysctl procedimental.

```c
static int
perfdemo_sysctl_hist(SYSCTL_HANDLER_ARGS)
{
    uint64_t values[PD_HIST_BUCKETS];
    int i, error;

    for (i = 0; i < PD_HIST_BUCKETS; i++)
        values[i] = counter_u64_fetch(perfdemo_stats.hist[i]);

    error = SYSCTL_OUT(req, values, sizeof(values));
    return (error);
}

SYSCTL_PROC(_dev_perfdemo_stats, OID_AUTO, lat_hist,
    CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_sysctl_hist, "S",
    "Read latency histogram (buckets: <1us, <10us, <100us, "
    "<1ms, <10ms, <100ms, <1s, >=1s)");
```

Desde el espacio de usuario, un pequeño script lee el array y lo imprime:

```sh
#!/bin/sh
sysctl -x dev.perfdemo.stats.lat_hist | awk -F= '{
    split($2, bytes, " ");
    for (i = 1; i <= length(bytes); i += 8) {
        val = 0;
        for (j = 7; j >= 0; j--) {
            val = val * 256 + strtonum("0x" bytes[i + j]);
        }
        printf "Bucket %d: %d\n", (i - 1) / 8, val;
    }
}'
```

El script tiene unas pocas líneas y te proporciona un panel de monitorización inmediatamente útil. Con el tiempo, las extensiones se vuelven evidentes: compara dos instantáneas para obtener tasas, representa los deltas a lo largo del tiempo, alerta cuando un cubo de alta latencia supera un umbral.

### Presentación de sysctl por CPU

Para un driver con estado DPCPU, existe la posibilidad de elegir entre agregar los valores a través de todas las CPUs (el enfoque estándar: calcular un único número a partir de `DPCPU_SUM` o un bucle manual) y presentar los valores por CPU por separado. La presentación por CPU es útil cuando el operador necesita diagnosticar un desequilibrio entre CPUs: una CPU gestionando la mayor parte del trabajo, otra inactiva, o varianza en la profundidad de cola entre CPUs.

Un sysctl procedimental que devuelve un array por CPU:

```c
static int
perfdemo_sysctl_percpu(SYSCTL_HANDLER_ARGS)
{
    uint64_t values[MAXCPU];
    int cpu;

    for (cpu = 0; cpu < mp_ncpus; cpu++) {
        struct perfdemo_cpu_stats *s =
            DPCPU_ID_PTR(cpu, perfdemo_cpu_stats);
        values[cpu] = s->cpu_time_ns;
    }
    return (SYSCTL_OUT(req, values, mp_ncpus * sizeof(uint64_t)));
}
```

El manejador recorre cada CPU activa, lee su estado DPCPU y copia los valores en un buffer contiguo. El lector del espacio de usuario ve un array de `uint64_t` de tamaño `mp_ncpus`. Para exportar a una herramienta de monitorización, la presentación por CPU ofrece una imagen detallada que la agregación oculta.

### Interfaces estables para los operadores

El árbol sysctl de un driver es una interfaz; una vez que los operadores escriben scripts o paneles de control basándose en él, cambiar los nombres lo rompe todo. Unas pocas reglas ayudan a mantener el árbol estable durante años.

Primero, **nombra las cosas por lo que son, no por cómo están implementadas**. Un sysctl llamado `reads` describe un concepto observable por el usuario; uno llamado `atomic_counter_0` describe un detalle de implementación. El primero es estable a través de las refactorizaciones; el segundo obliga a cambiar el nombre cada vez que cambia la implementación.

Segundo, **versiona la interfaz si tiene que cambiar**. Si añades un nuevo campo a un sysctl con valor de estructura existente, los scripts más antiguos seguirán leyendo el tamaño anterior correctamente. Si cambias el nombre de un nodo, añade primero el nuevo nombre como alias y luego depreca el antiguo durante al menos un ciclo de lanzamiento.

Tercero, **documenta cada nodo en la cadena `descr`**. El último argumento de las macros `SYSCTL_*` es una descripción breve que aparece en `sysctl -d`. Mantenla precisa y útil; es la única documentación en línea que tiene un operador cuando diagnostica a las tres de la mañana.

Cuarto, **evita los conceptos internos del driver en los nombres**. Un contador llamado `uma_zone_free_count` requiere que el operador sepa qué es UMA; uno llamado `free_buffers` describe lo que cuenta en términos que cualquier operador puede entender.

Un driver que sigue estas reglas produce un árbol sysctl que se mantiene bien con el tiempo. El ejercicio de la sección 7 es una oportunidad para practicar.

### Gestión segura de escrituras en sysctl

Un sysctl con permiso de escritura es una API pública para modificar el estado del driver en tiempo de ejecución. Toda ruta que acepte entrada del usuario debe validar, sincronizar e informar de los errores de forma clara. El patrón en drivers de producción es el siguiente:

```c
static int
perfdemo_sysctl_mode(SYSCTL_HANDLER_ARGS)
{
    struct perfdemo_softc *sc = arg1;
    int new_val, error;

    new_val = sc->mode;
    error = sysctl_handle_int(oidp, &new_val, 0, req);
    if (error != 0 || req->newptr == NULL)
        return (error);

    if (new_val < 0 || new_val > 2)
        return (EINVAL);

    PD_LOCK(sc);
    if (sc->state != PD_STATE_READY) {
        PD_UNLOCK(sc);
        return (EBUSY);
    }
    sc->mode = new_val;
    perfdemo_apply_mode(sc);
    PD_UNLOCK(sc);

    return (0);
}
```

Aquí suceden tres cosas. En primer lugar, el manejador valida el valor; los valores fuera de rango devuelven `EINVAL` en lugar de corromper el driver. En segundo lugar, el manejador adquiere el lock del driver antes de modificar el estado; así, otro CPU que lea o escriba el modo de forma concurrente no verá actualizaciones a medias. En tercer lugar, el manejador comprueba el estado del ciclo de vida del driver; los cambios se rechazan si el driver está en proceso de detach o en estado de error. Cada uno de estos elementos es una pequeña adición al manejador más simple posible, y cada uno evita una clase real de errores.

Una consideración relacionada es si el cambio de sysctl es idempotente. Si el nuevo valor es igual al actual, el manejador no debe hacer nada (o, como mucho, confirmar el estado actual). Si el nuevo valor difiere, el manejador debe cambiar el estado de forma atómica para que nadie vea una actualización parcial. El patrón de adquirir el lock, validar y aplicar descrito más arriba satisface ambas restricciones.

### Ejercicio 33.7: Un árbol sysctl de informes

Amplía el árbol sysctl del driver `perfdemo` con al menos los siguientes nodos:

- `dev.perfdemo.<unit>.stats.reads`: lecturas totales.
- `dev.perfdemo.<unit>.stats.errors`: errores totales.
- `dev.perfdemo.<unit>.stats.bytes`: bytes totales.
- `dev.perfdemo.<unit>.stats.latency_avg_ns`: latencia media en nanosegundos.
- `dev.perfdemo.<unit>.config.buffer_size`: tamaño del buffer (ajustable, 16..4096).
- `dev.perfdemo.<unit>.config.debug`: nivel de depuración (0..2).

Usa `counter(9)` para los contadores; usa un sysctl procedimental para la media de latencia derivada; usa `CTLFLAG_RWTUN` para los valores ajustables. Compila y carga el driver; verifica que cada sysctl devuelve un valor coherente; cambia un valor ajustable en tiempo de ejecución y confirma que el driver lo respeta.

Este ejercicio produce una interfaz observable en la que el resto del capítulo puede apoyarse. También es un trabajo autocontenido e interesante que merece la pena añadir a un portfolio.

### Cerrando la sección 7

El árbol sysctl es la interfaz de observabilidad siempre activa del driver. Un árbol bien diseñado expone totales, tasas, estados y configuración. Los valores de `counter(9)` requieren sysctls procedimentales para obtener sus totales acumulados. El registro con tasa limitada mediante `log(9)` y `ratecheck(9)` evita que los mensajes informativos inunden el log. Los modos de depuración ofrecen a los operadores una forma segura de activar el trazado detallado al diagnosticar problemas. Cada uno de ellos supone una pequeña inversión; el efecto acumulado es un driver que se puede auditar desde la línea de comandos, diagnosticar sin reiniciar y mantener con confianza incluso cuando el autor ya no esté.

La última sección instructiva, la sección 8, es la sección de disciplina. Enseña cómo limpiar el código tras un proyecto de ajuste de rendimiento, documentar los benchmarks que produjeron el resultado, actualizar la página de manual con los parámetros que el driver ahora expone y entregar el driver optimizado como una versión en la que el sistema puede confiar.

## Sección 8: Refactorización y versionado tras el ajuste de rendimiento

El ajuste de rendimiento añade andamiaje. Contadores que medían rutas de código críticas específicas, sondas de DTrace que respondieron una pregunta en su momento, instrucciones de impresión de una noche de trabajo intenso, variantes comentadas que querías tener a mano: todo esto se acumula durante un proyecto de rendimiento. La misma disciplina que produjo las mediciones debe producir la limpieza final. El driver que sobrevive en el árbol de código tras el ajuste es aquel cuyo autor supo cuándo parar y cómo dejar el código en un estado que un futuro mantenedor pueda leer.

Esta sección trata precisamente de esa limpieza. Cubre qué eliminar, qué conservar, cómo documentar el trabajo y cómo versionar el resultado.

### Eliminación del código de medición temporal

La limpieza más importante es la eliminación del código de medición temporal. Durante el ajuste puede que hayas añadido:

- Llamadas a `printf()` que rastrean operaciones específicas.
- Contadores que te ayudaron a encontrar un cuello de botella pero que no corresponden al código de producción.
- Mediciones de tiempo que se acumulaban en arrays globales.
- Sondas al estilo DTrace en medio de rutas críticas que no están formalmente declaradas como SDT.
- Versiones comentadas de optimizaciones que probaste y descartaste.

Cada uno de ellos es un coste de mantenimiento futuro. La regla es sencilla: si un fragmento de código solo fue útil durante la sesión de ajuste, elimínalo. El control de versiones conserva el historial; el mensaje de commit explica lo que probaste. El código del árbol de trabajo debería leerse como si las mediciones nunca hubieran existido.

Una disciplina útil es **marcar el código temporal con un comentario al añadirlo**, para saber qué eliminar después:

```c
/* PERF: added for v2.3 tuning; remove before ship. */
printf("perfdemo: read entered at %ju\n", (uintmax_t)sbinuptime());
```

Cuando el proyecto de ajuste haya terminado, busca `PERF:` con grep y elimina cada línea. Un driver sin marcadores `PERF:` es uno que ha sido limpiado; un driver con una docena de ellos es aquel donde el autor se olvidó.

### Qué conservar

No todo el código de medición debe eliminarse. Parte de él tiene valor duradero y pertenece al driver que se entrega. Los criterios son:

- **Conserva los contadores que informan a los operadores de lo que hace el driver.** Las lecturas totales, escrituras, errores y bytes pertenecen al árbol sysctl de forma permanente.
- **Conserva las sondas SDT en los límites operacionales.** No tienen ningún coste cuando están desactivadas y ofrecen a cualquier futuro ingeniero una forma inmediata de medir el driver.
- **Conserva los parámetros de configuración que exponen compromisos significativos.** El tamaño del buffer, el umbral de coalescencia y el nivel de depuración son interfaces para el operador.
- **Elimina las impresiones puntuales y las mediciones de tiempo escritas a mano.** Eran para una sesión de ajuste y no tienen cabida en el código entregado.
- **Elimina los contadores demasiado específicos para la pregunta que estabas respondiendo.** Un contador llamado `reads_between_cache_miss_A_and_cache_miss_B` probablemente no tiene valor duradero; uno llamado `reads` sí.

La prueba es: *¿se beneficiaría un futuro ingeniero de esta información dentro de seis meses?* Si la respuesta es sí, consérvala. Si no, elimínala.

### Benchmark del driver final

Una vez eliminado el andamiaje, realiza un benchmark del driver final una vez más. Este benchmark es el que se convierte en el rendimiento *publicado* del driver; regístralo en tu cuaderno de notas y en un documento de texto plano en el árbol de código fuente del driver. Un informe de benchmark debe incluir:

- La versión del driver (por ejemplo, `perfdemo 2.3`).
- La máquina: modelo de CPU, número de núcleos, tamaño de RAM y versión de FreeBSD.
- La carga de trabajo: qué comando `dd`, qué tamaños de archivo, cuántos threads en userland y qué ventana de coalescencia.
- El resultado: throughput, latencia (P50, P95, P99) y uso de CPU.
- El contexto: qué más estaba en ejecución y los ajustes de sysctl relevantes.

Un lector que retome el driver meses después debería poder reproducir el benchmark sin necesidad de preguntarte. Esa reproducibilidad es el propósito del informe.

### Documentación de los parámetros de ajuste

Si el driver ahora expone sysctls ajustables que afectan al rendimiento, documéntalos. El lugar adecuado es la página de manual del driver (una página `.4` en `/usr/share/man/man4/`) o una sección al comienzo del archivo de código fuente del driver. Una sección de página de manual tiene este aspecto:

```text
.Sh TUNABLES
The following tunables can be set at the
.Xr loader.conf 5
prompt or at runtime via
.Xr sysctl 8 :
.Bl -tag -width "dev.perfdemo.buffer_size"
.It Va dev.perfdemo.buffer_size
Size of the internal read buffer, in bytes.
Default 1024; valid range 16 to 4096.
Larger values increase throughput for bulk reads
but raise per-call latency.
.It Va dev.perfdemo.debug
Verbosity of debug logging.
0 logs errors only.
1 adds transient-error notes.
2 logs every operation.
Default 0; higher values should only be used
during diagnostic sessions.
.El
```

El formato resulta oscuro a primera vista, pero todas las páginas de manual de FreeBSD usan el mismo patrón. Las páginas `.4` existentes en `/usr/src/share/man/man4/` son excelentes plantillas; copia una cuyo estilo se adapte a tu driver y adáptala.

Para los drivers que todavía no incluyen una página de manual, este es el momento de escribirla. Un driver sin página de manual está escasamente documentado; uno con página de manual alcanza el umbral de credibilidad que se espera del código del sistema base.

### Versionado

Un driver ajustado es una nueva versión del driver. Márcalo con `MODULE_VERSION()` en el código fuente del driver:

```c
MODULE_VERSION(perfdemo, 3);   /* was 2 before this tuning pass */
```

El número de versión es utilizado por `kldstat -v` y por otros módulos que declaran `MODULE_DEPEND()` frente al tuyo. Incrementarlo indica que el comportamiento del driver ha cambiado lo suficiente como para que los consumidores deban prestar atención.

Para un driver entregado, la convención es una versión mayor para cambios incompatibles, una versión menor (o sufijo de versión) para adiciones compatibles y una versión de parche para corrección de errores. Una pasada de ajuste de rendimiento puro que no añade nueva funcionalidad es un parche; una pasada que añade nuevos sysctls es una versión menor; una pasada que cambia la semántica de las interfaces existentes es una versión mayor. Los ejercicios del libro denominan al driver ajustado `v2.3-optimized`; en la práctica, el esquema de versiones es el que utilice tu proyecto.

Actualizar el changelog es parte del trabajo de versionado. Un `CHANGELOG.md` o un comentario en el código fuente del driver es el lugar adecuado. Cada entrada debe incluir:

- La versión y la fecha.
- Qué cambió a alto nivel (ajuste de rendimiento, corrección de errores, nueva funcionalidad).
- Las cifras del benchmark si cambiaron.
- Cualquier cambio incompatible con versiones anteriores que los operadores deban conocer.

El hábito de mantener el changelog actualizado es el hábito que hace que el código de larga duración sea mantenible. Los drivers sin changelogs acumulan tradición oral; los que los tienen acumulan historia.

### Revisión del diff

Antes de entregar, lee el diff completo del trabajo de ajuste como si fueras un nuevo mantenedor revisando el cambio. Busca:

- Rutas de código que se han vuelto más difíciles de razonar.
- Comentarios que ya no coinciden con el código.
- Código temporal que sobrevivió a la limpieza.
- Rutas de error que fueron modificadas pero no volvieron a probarse.
- Locking que fue añadido o eliminado.

Esta revisión detecta los problemas que tus propias mediciones no captaron. Un fragmento de código correcto pero ilegible es un problema que tarde o temprano ocurrirá.

### El informe de rendimiento

El entregable final de un proyecto de ajuste es un breve informe escrito. No una entrada de blog, no una presentación, sino un documento sencillo que acompaña al driver. Una plantilla útil:

```text
perfdemo v2.3-optimized - Performance Report
==============================================

Summary: v2.3 is a pure-tuning release that reduces median read
latency from 40 us to 12 us and triples throughput on a 4-core
amd64 test machine, without changing the driver's interface.

Goals (set before tuning):
  - Median read() latency under 20 us.
  - P99 read() latency under 100 us.
  - Single-thread throughput above 500K ops/sec.
  - Multi-thread throughput scaling linearly up to 4 CPUs.

Before (v2.2):
  - Median read: 40 us.
  - P99 read: 180 us.
  - Single-thread: 180K ops/sec.
  - 4-thread: 480K ops/sec (2.7x; sublinear).

After (v2.3):
  - Median read: 12 us.
  - P99 read: 65 us.
  - Single-thread: 520K ops/sec.
  - 4-thread: 1.95M ops/sec (3.75x; near-linear).

Changes applied:
  1. Replaced single atomic counter with counter(9).
  2. Cache-line aligned hot softc fields.
  3. Pre-allocated read buffers in attach().
  4. Switched to UMA zone for per-request scratch buffers.
  5. Added sysctl nodes for stats and config (non-breaking).

Measurements:
  All numbers from DTrace aggregations over 100,000-sample
  windows, 4-core amd64, 3.0 GHz Xeon, FreeBSD 14.3 GENERIC,
  otherwise idle. See benchmark/v2.3/ for raw data and scripts.

Tunables introduced:
  - dev.perfdemo.buffer_size: runtime buffer size (default 1024).
  - dev.perfdemo.debug: runtime debug verbosity (default 0).

Risks:
  Cache-line alignment increases softc memory by approximately
  200 bytes per instance. Unlikely to matter on modern systems
  but worth noting for memory-constrained embedded targets.

Remaining work:
  None for v2.3. Future tuning may investigate reducing P99
  latency further if workload analysis shows a specific cause.
```

Un informe así se convierte en conocimiento institucional. Un mantenedor que lea este documento dos años después tendrá todo lo necesario para entender qué se hizo, por qué y cómo reproducirlo.

### Pruebas A/B de los cambios de ajuste

Antes de incorporar un cambio de ajuste al árbol, lo responsable es compararlo con la versión anterior bajo la misma carga de trabajo. Una prueba A/B compara las dos versiones frente a un mismo benchmark, varias veces, bajo el mismo estado del sistema. Si la nueva versión es significativamente mejor *y* la diferencia se mantiene por encima del ruido en varias ejecuciones, el cambio merece conservarse.

Un arnés A/B sencillo:

```sh
#!/bin/sh
# ab-test.sh <module-a-name> <module-b-name> <runs>

MODULE_A=$1
MODULE_B=$2
RUNS=$3

echo "Module A: $MODULE_A"
echo "Module B: $MODULE_B"
echo "Runs per module: $RUNS"
echo

for i in $(seq 1 $RUNS); do
    sudo kldload ./"$MODULE_A".ko
    time ./run-workload.sh 100000
    sudo kldunload "$MODULE_A"
    echo "A$i done"

    sudo kldload ./"$MODULE_B".ko
    time ./run-workload.sh 100000
    sudo kldunload "$MODULE_B"
    echo "B$i done"
done
```

Ejecútalo con `./ab-test.sh perfdemo-v22 perfdemo-v23 10`. El script alterna los módulos para evitar que los efectos de calentamiento favorezcan a uno frente al otro. Diez ejecuciones de cada uno suelen ser suficientes para distinguir una diferencia del 5 % del ruido.

Las pruebas A/B importan por dos razones. En primer lugar, te obligan a formular la comparación como *versión A frente a versión B bajo la carga de trabajo X*, que es la forma que debe tener una afirmación de rendimiento. En segundo lugar, detectan regresiones: si v2.3 es más lento que v2.2 en algún eje que no mediste, la ejecución A/B lo pone de manifiesto. Un cambio de ajuste que mejora una métrica mientras perjudica otra es sorprendentemente común; solo una comparación rigurosa los saca a la luz.

### El arnés de benchmark

Un proyecto sofisticado acaba acumulando un arnés de benchmark apropiado: un script reutilizable que ejecuta una carga de trabajo conocida, recopila un conjunto conocido de métricas y escribe los resultados en un formato conocido. Vale la pena construir un arnés cuando:

- Ejecutas el mismo benchmark más de tres o cuatro veces.
- Varias personas necesitan reproducir el benchmark.
- Los resultados se usan en informes de rendimiento y deben ser comparables entre ejecuciones.

El arnés suele incluir:

- Un script de configuración que carga el driver, ajusta sus sysctls y verifica que el sistema está inactivo.
- Un script de ejecución que lanza la carga de trabajo durante un tiempo fijo o un número fijo de iteraciones.
- Un recopilador que captura métricas antes y después, calcula las diferencias y escribe un informe estructurado.
- Un script de limpieza que descarga el driver y restaura cualquier estado del sistema.
- Un archivo de resultados que conserva las salidas brutas y procesadas con marcas de tiempo e identificadores de ejecución.

Para `perfdemo`, el arnés reside en el directorio del laboratorio como `final-benchmark.sh`. Para un driver de producción, reside en el árbol de código junto al driver, de modo que cualquiera pueda reproducir los resultados.

Los detalles del harness dependen del driver. Lo que importa es que exista *algún* harness; los benchmarks ad-hoc que alguien tiene que recordar cómo ejecutar son evidencia ad-hoc que alguien tiene que recordar cómo confiar.

### Compartir las cifras de rendimiento con el proyecto

Para los drivers que van al sistema base de FreeBSD o a un port significativo, las cifras de rendimiento no son un entregable privado. Pasan a formar parte del registro del proyecto: mensajes de commit, hilos en listas de correo, notas de versión. Vale la pena conocer las convenciones para compartirlas.

**Los mensajes de commit** deben resumir el cambio, el benchmark y el resultado en el cuerpo del mensaje. Un buen mensaje de commit relacionado con el rendimiento tiene este aspecto:

```text
perfdemo: switch counters to counter(9) for better scaling

Single counters in the softc were contended on multi-CPU systems.
Switching to counter(9) reduces per-call overhead and allows the
driver to scale to higher throughput under parallel load.

Measured on an 8-core amd64 Xeon at 3.0 GHz, FreeBSD 14.3-STABLE:
  Before: 480K ops/sec at 4-thread peak.
  After:  1.95M ops/sec at 4-thread peak (4x).

Benchmark data and scripts are in tools/perfdemo-bench/.
```

La primera línea es un resumen breve. El cuerpo amplía qué cambió, por qué, y qué mostró la medición. Un lector que recorra el historial de commits puede entender la importancia del cambio en 60 segundos.

**Los hilos en listas de correo** que anuncian el cambio siguen la misma estructura, pero pueden incluir más contexto: el objetivo del ajuste, las alternativas consideradas y cualquier advertencia. Incluye enlaces a los scripts de benchmark y a los datos brutos si la escala del cambio merece discusión.

**Las notas de versión** son más escuetas. Una o dos líneas: *El driver `perfdemo(4)` usa ahora `counter(9)` para sus estadísticas internas. Esto reduce la sobrecarga en sistemas con múltiples CPU y permite mayor rendimiento bajo carga paralela.*

Cada audiencia recibe lo que necesita. El patrón es que toda afirmación sobre rendimiento, a cualquier nivel de detalle, apunta a una medición reproducible.

### Ejercicio 33.8: Publicar perfdemo v2.3

Usando el estado de `perfdemo` que construiste a lo largo de las Secciones 2 a 7, produce una versión v2.3 optimizada:

1. Elimina todos los marcadores `PERF:` y el andamiaje que designaban.
2. Decide qué permanece en el árbol de sysctl y qué solo fue útil durante el ajuste.
3. Actualiza `MODULE_VERSION()` a 3 (o al número nuevo que hayas elegido).
4. Actualiza la página de manual del driver (o escribe una si no existe) con la sección TUNABLES.
5. Ejecuta el benchmark final y registra los resultados.
6. Escribe el informe de rendimiento.

Este ejercicio es el resultado concreto de todo el capítulo. Cuando lo termines, tendrás un driver que pasaría la revisión en cualquier proyecto serio de FreeBSD, con las mediciones de rendimiento que justifican su estado.

### Cerrando la sección 8

El ajuste es solo la mitad de un proyecto. La otra mitad es la disciplina de limpiar, documentar y publicar. Un driver con primitivas de medición limpias, un árbol de sysctl bien pensado, documentación honesta en la página de manual, un número de versión actualizado y un informe de rendimiento escrito es el driver que envejece bien. La tentación de saltarse cualquiera de estos pasos es grande, pero el coste lo paga el siguiente mantenedor, que podría ser tu yo futuro.

La parte instructiva del capítulo está completa. Las secciones siguientes son los laboratorios prácticos, los ejercicios de desafío, la referencia de resolución de problemas, el cierre y el puente hacia el Capítulo 34.

## Poniéndolo todo junto: una sesión de ajuste completa

Antes de los laboratorios, un recorrido práctico que une las ocho secciones en una sola historia. Esta es la forma que toma una sesión de ajuste real, comprimida en unas pocas páginas. La sesión no es ficticia; es el mismo patrón que produce la mayor parte de las mejoras de rendimiento en el árbol de FreeBSD.

**El driver.** `perfdemo` v2.0 es un dispositivo de caracteres funcional que produce datos sintéticos en `read()`. Su ruta caliente toma un mutex, asigna un buffer temporal mediante `malloc(9)`, lo rellena con datos pseudoaleatorios, lo copia al espacio de usuario mediante `uiomove(9)`, libera el buffer, suelta el mutex y retorna. Tiene un único contador atómico para el total de lecturas y ningún árbol de sysctl más allá de lo que FreeBSD le proporciona gratuitamente a través de `device_t`.

**El objetivo.** Un usuario informa de que `perfdemo` es lento en su carga de trabajo en producción: ejecuta cuarenta threads lectores concurrentes y el rendimiento se satura en torno a 600.000 ops/seg en todos los threads en su servidor amd64 de 16 núcleos. Quiere 2 millones o más. El objetivo declarado es *latencia mediana de `read()` por debajo de 20 us y rendimiento agregado superior a 2 millones de ops/seg en un Xeon amd64 de 16 núcleos ejecutando FreeBSD 14.3 GENERIC, con cuarenta threads lectores distribuidos en las CPU 0-15*.

**Medición, ronda 1.** Antes de cambiar nada, necesitamos cifras. Cargamos el driver, ejecutamos la carga de trabajo del usuario y recopilamos datos de referencia.

`vmstat -i` no muestra tasas de interrupción inusuales, por lo que descartamos problemas de interrupciones desde el principio. `top -H` muestra que la CPU del sistema está al 65%; el driver claramente no es el único consumidor, pero sí uno importante. Instrumentamos el driver de forma mínima: un contador `counter(9)` para el total de lecturas, uno para la latencia total en nanosegundos, ambos expuestos mediante un sysctl procedimental. Ejecutamos la carga de trabajo durante 60 segundos y registramos la latencia media: 52 microsegundos. El objetivo es estar por debajo de 20. Nos quedan 32 microsegundos de margen.

**DTrace, ronda 1.** Con los datos de referencia en mano, recurrimos a DTrace. Un one-liner para ver la distribución de latencia a nivel de función:

```console
# dtrace -n '
fbt:perfdemo:perfdemo_read:entry { self->s = timestamp; }
fbt:perfdemo:perfdemo_read:return /self->s/ {
    @lat = quantize(timestamp - self->s);
    self->s = 0;
}'
```

Tras un minuto, el histograma muestra una forma clara. P50 es de unos 30 us; P95 es de 150 us; P99 es un sorprendente 1,5 ms. La cola larga es la fuente del malestar del usuario.

Ampliamos la investigación. Otro one-liner:

```console
# dtrace -n 'lockstat:::adaptive-block {
    @[((struct lock_object *)arg0)->lo_name] = count();
}'
```

La primera entrada: *perfdemo_softc_mtx* con unos 30.000 bloqueos durante la ventana de un minuto. El mutex de nuestro driver presenta contención; cuarenta threads lectores se están serializando en él.

**La primera corrección.** El mutex protege un contador y un puntero al pool; en principio, el contador podría ser por CPU y el puntero al pool podría ser sin lock. Reemplazamos el contador atómico con `counter_u64_add()`, que no requiere el lock del driver, y reorganizamos el pool en un array de buffers por CPU (uno por CPU). La ruta caliente ya no necesita tomar el mutex en el caso común; el mutex solo protege las rutas administrativas del pool (inicialización, finalización, redimensionamiento).

Reconstruimos, recargamos, volvemos a ejecutar. La latencia media cae de 52 us a 14 us. El rendimiento sube de 600.000 a 1,8 millones de ops/seg. Estamos cerca del objetivo, pero sin alcanzarlo aún.

**DTrace, ronda 2.** Con el mutex fuera del camino, volvemos a perfilar:

```console
# dtrace -n 'profile-997 /arg0 != 0/ { @[stack()] = count(); }'
```

La pila superior muestra ahora que la mayor parte del tiempo está en `uiomove`, `malloc` y `free`. El asignador de memoria es el siguiente cuello de botella; cada lectura asigna y libera un buffer temporal. El coste a nivel de contador es insignificante.

**La segunda corrección.** Sustituimos `malloc(9)` en la ruta caliente por una zona UMA creada en attach, con `UMA_ALIGN_CACHE` para elementos alineados por CPU. El tamaño de la zona se ajusta a la carga de trabajo esperada; `vmstat -z` confirma que la población de la zona se estabiliza unos segundos después de que arranque la carga de trabajo.

Reconstruimos, recargamos, volvemos a ejecutar. La latencia media cae de 14 us a 9 us. El rendimiento sube de 1,8 millones a 2,3 millones de ops/seg. Hemos alcanzado el objetivo: mediana por debajo de 20 us y rendimiento superior a 2 millones.

**pmcstat, ronda 1.** Estamos dentro del objetivo, pero antes de declarar la victoria queremos saber si el driver está bien equilibrado entre las CPU. Ejecutamos una sesión de muestreo:

```console
# pmcstat -S cycles -O /tmp/perfdemo.pmc ./run-workload.sh
# pmcstat -R /tmp/perfdemo.pmc -T
```

Las funciones más destacadas son `uiomove`, `perfdemo_read` y un par de primitivas de coherencia de caché del kernel. No hay un único cuello de botella; el driver dedica ahora su tiempo al trabajo que debería hacer. Bien.

Una sesión de conteo:

```console
# pmcstat -s cycles -s instructions ./run-workload.sh
```

Imprime un IPC de aproximadamente 1,8. Es un valor saludable para código que principalmente mueve memoria; no estamos desaprovechando el rendimiento del hardware.

**Limpieza.** Leemos el diff. Quedan cuatro marcadores `PERF:` de la investigación; los eliminamos. Existen dos variantes comentadas de la segunda corrección; las eliminamos. Un sysctl de nivel de depuración que añadimos en algún momento pero nunca usamos aparece en el código; lo eliminamos también.

**Árbol de sysctl.** El árbol tiene ahora `dev.perfdemo.0.stats.reads`, `dev.perfdemo.0.stats.errors`, `dev.perfdemo.0.stats.bytes`, `dev.perfdemo.0.stats.lat_hist` (el histograma) y `dev.perfdemo.0.config.debug` (un nivel de verbosidad de depuración del 0 al 2). Cada uno está documentado en su cadena `descr`.

**Página de manual.** Un párrafo en la página `.4` existente describe los nuevos ajustes. El documento del informe de benchmark describe el proceso de ajuste y los resultados.

**Cifras finales.** En el Xeon de 16 núcleos bajo la carga de trabajo del usuario:

- Latencia mediana de `read()`: 9 us.
- Latencia P95 de `read()`: 35 us.
- Latencia P99 de `read()`: 95 us.
- Rendimiento: 2,3 millones de ops/seg.

**Tiempo invertido.** Aproximadamente una jornada de ingeniero desde la primera medición hasta el informe final. Las dos correcciones son pequeñas; encontrarlas llevó más tiempo que escribirlas. Esa proporción es habitual.

**Lecciones.** Destacan tres puntos.

Primero, la investigación no comenzó con cambios en el código. Empezó con medición, pasó por DTrace y solo entonces cambió el código. Cada cambio estuvo motivado por una observación concreta.

Segundo, dos correcciones fueron suficientes. El mutex y el asignador de memoria eran los dos cuellos de botella; las optimizaciones de tercer orden (alineación de caché en campos pequeños, procesamiento por lotes al estilo NAPI, MSI-X) no fueron necesarias. El capítulo las describe todas porque hay que saber que existen; la sesión demostró que se aplican de forma selectiva.

Tercero, el andamiaje fue temporal. Marcadores `PERF:`, variantes comentadas, un nivel de depuración sin usar: todo eliminado antes de publicar. El driver limpio parece sencillo porque el ajuste dejó poca huella. Esa sencillez es el objetivo.

Los laboratorios siguientes recorren la misma forma de sesión, con código y comandos concretos, en cada etapa.

## Laboratorios prácticos

Los laboratorios de este capítulo se construyen alrededor de un pequeño driver pedagógico llamado `perfdemo`. El driver es un dispositivo de caracteres que sintetiza datos en `read()` a una velocidad controlada y expone un árbol de sysctl. Es deliberadamente sencillo; lo interesante es lo que los laboratorios *hacen* con él. Cada laboratorio lleva el driver por una etapa del flujo de trabajo de rendimiento: medición de referencia, trazado con DTrace, muestreo con PMC, ajuste de memoria, ajuste de interrupciones y limpieza final.

Todos los archivos de los laboratorios se encuentran en `examples/part-07/ch33-performance/`. Cada directorio de laboratorio tiene su propio `README.md` con instrucciones de compilación y ejecución. Clona el directorio, sigue el README de cada laboratorio y trabájalos en orden.

### Laboratorio 1: `perfdemo` de referencia, contadores y sysctl

**Objetivo:** Construir el driver `perfdemo` de referencia, cargarlo y ejercitar sus contadores basados en sysctl. Este laboratorio es la base de todos los demás laboratorios del capítulo; tómate el tiempo necesario para hacerlo bien.

**Directorio:** `examples/part-07/ch33-performance/lab01-perfcounters/`.

**Prerrequisitos:** Un sistema FreeBSD 14.3 (físico, virtual o jail con herramientas de build del kernel), el árbol de código fuente de FreeBSD en `/usr/src` y acceso root para cargar módulos del kernel.

**Pasos:**

1. Entra en el directorio del laboratorio: `cd examples/part-07/ch33-performance/lab01-perfcounters/`.
2. Lee `perfdemo.c` para familiarizarte con el driver de referencia. Fíjate en su estructura: un `module_event_handler`, un conjunto `probe`/`attach`/`detach`, un `cdevsw` con un método de lectura, un softc y el árbol de sysctl bajo `hw.perfdemo`.
3. Compila el módulo con `make`. Si la compilación falla, comprueba que `/usr/src` está poblado y que puedes compilar otros módulos del kernel (la prueba más sencilla: `cd /usr/src/sys/modules/nullfs && make`).
4. Carga el módulo: `sudo kldload ./perfdemo.ko`. Confirma que está cargado: `kldstat | grep perfdemo`.
5. Consulta el árbol de sysctl: `sysctl hw.perfdemo`. Deberías ver:

    ```
    hw.perfdemo.reads: 0
    hw.perfdemo.writes: 0
    hw.perfdemo.errors: 0
    hw.perfdemo.bytes: 0
    ```

6. Verifica que el nodo de dispositivo existe: `ls -l /dev/perfdemo`.
7. En una terminal, inicia una supervisión: `watch -n 1 'sysctl hw.perfdemo'`.
8. En otra terminal, ejecuta la carga de trabajo: `./run-workload.sh 100000`. El script lanza 100.000 lecturas contra `/dev/perfdemo`. Al terminar, imprime su tiempo de reloj de pared.
9. Verifica que los contadores se han movido como se esperaba. Tras la carga de trabajo, `hw.perfdemo.reads` debería estar alrededor de 100.000 (unos pocos extras por lecturas ad hoc son normales).
10. Ejecuta la carga de trabajo con un volumen mayor: `./run-workload.sh 1000000`. Registra el tiempo de reloj de pared de nuevo. Divide `1000000 / segundos_de_reloj` para obtener ops/seg.
11. Descarga el driver: `sudo kldunload perfdemo`.

**Lo que deberías ver:**

- Los contadores sysctl aumentan mientras se ejecuta la carga de trabajo.
- Los valores finales deberían coincidir con el número de peticiones de la carga de trabajo (con una variación de una o dos operaciones).
- El tiempo de reloj de la ejecución de 1M lecturas debería ser de unos pocos segundos en una máquina moderna.
- Sin advertencias del kernel en `dmesg`.

**Lo que debes registrar en tu cuaderno de notas:**

- El tiempo de reloj y las ops/sec de ambas ejecuciones de carga de trabajo.
- La máquina: modelo de CPU, número de núcleos, RAM, versión de FreeBSD.
- Cualquier salida de `dmesg` de los eventos de carga y descarga del módulo.
- Los valores finales de sysctl.

**Números de referencia esperados (para comparación):**

En un Xeon amd64 de 8 núcleos a 3,0 GHz ejecutando FreeBSD 14.3-RELEASE con el driver base, una carga de trabajo single-threaded de 1M lecturas se completa en aproximadamente 3 segundos, dando unas 330.000 ops/sec. Tus números variarán según el hardware, pero el orden de magnitud debería ser similar.

**Errores comunes:**

- Olvidar cargar el driver antes de ejecutar la carga de trabajo. El script de carga fallará con `ENODEV` si `/dev/perfdemo` no está disponible.
- Ejecutar el script de carga con un número muy pequeño de iteraciones (por ejemplo, 100) y esperar números estables. El ruido de medición empieza a dominar por debajo de unos pocos miles de iteraciones. Usa 100.000 o más para las ejecuciones de referencia.
- Confundir `hw.perfdemo.reads` con `hw.perfdemo.bytes`. El contador de lecturas totales es distinto del contador de bytes totales. Una lectura de 100 bytes incrementa `reads` en uno y `bytes` en 100.
- Olvidar descargar el driver antes de recompilar. Un archivo `.ko` obsoleto en el kernel puede causar síntomas confusos en la siguiente carga.

**Resolución de problemas:** Si el módulo falla al cargarse con *module version mismatch*, la versión de tu kernel no coincide con la que se usó para compilar `perfdemo`. Recompila después de asegurarte de que `/usr/obj` y `/usr/src` coinciden con tu kernel en ejecución.

### Lab 2: DTrace `perfdemo`

**Objetivo:** Usa DTrace para entender qué hace el camino de lectura de `perfdemo`, medido desde fuera sin modificar el driver.

**Directorio:** `examples/part-07/ch33-performance/lab02-dtrace-scripts/`.

**Prerrequisitos:** Laboratorio 1 completado; el driver `perfdemo` cargado.

**Pasos:**

1. Asegúrate de que el driver `perfdemo` esté cargado (del Laboratorio 1). Si no lo está, haz `cd` a `lab01-perfcounters/` y ejecuta `sudo kldload ./perfdemo.ko`.
2. Carga los proveedores de DTrace: `sudo kldload dtraceall`. Puedes verificarlo con `sudo dtrace -l | head` (se listarán algunas sondas).
3. En un terminal, ejecuta la carga de trabajo de forma continua: `while :; do ./run-workload.sh 10000; done`. Deja que siga ejecutándose.
4. En otro terminal, ejecuta el one-liner simple de conteo:

    ```
    # sudo dtrace -n 'fbt:perfdemo:perfdemo_read:entry { @ = count(); }'
    ```

    Déjalo correr 30 segundos y luego pulsa Ctrl-C. Deberías ver un número del orden de decenas de miles. Divídelo entre 30 para obtener lecturas por segundo, lo que debería coincidir con el delta que muestra `hw.perfdemo.reads`.

5. Ejecuta el script de histograma de latencia `read-latency.d` que se encuentra en el directorio de este laboratorio:

    ```
    # sudo dtrace -s read-latency.d
    ```

    Déjalo correr 30 segundos. El histograma tendrá un aspecto similar a este:

    ```
               value  ------------- Distribution ------------- count
                 512 |                                         0
                1024 |@                                        125
                2048 |@@@@@                                    520
                4096 |@@@@@@@@@@@@@@@@@@                       1850
                8192 |@@@@@@@@@@@@@                            1320
               16384 |@@@                                      290
               32768 |                                         35
               65536 |                                         8
              131072 |                                         2
              262144 |                                         0
    ```

    Anota el P50 (el bucket que contiene la mediana) y el P99 (el bucket que cruza el 99% acumulado).

6. Ejecuta el script de contención de locks `lockstat-simple.d`:

    ```
    # sudo dtrace -s lockstat-simple.d
    ```

    Déjalo correr 30 segundos. Busca `perfdemo` o `perfdemo_softc_mtx` en la salida. El driver de referencia *sí* mostrará contención en su mutex si ejecutas la carga de trabajo con concurrencia.

7. Ejecuta el script de muestreo de perfil `profile-sample.d`:

    ```
    # sudo dtrace -s profile-sample.d
    ```

    Déjalo correr 60 segundos. La salida lista los stacks del kernel más frecuentes por número de muestras. `perfdemo_read` debería aparecer entre los primeros.

8. Detén la carga de trabajo (Ctrl-C en su terminal).

**Qué deberías ver:**

- `read-latency.d` produce un histograma con un pico similar a una campana alrededor de la mediana y una cola hacia la derecha.
- `lockstat-simple.d` produce una lista de locks utilizados durante la medición; si la carga de trabajo es concurrente, `perfdemo_softc_mtx` aparece en ella.
- `profile-sample.d` produce una lista de stacks de funciones; `perfdemo_read`, `uiomove`, `copyout` y `_mtx_lock_sleep` son entradas habituales.

**Qué debes registrar en tu cuaderno de laboratorio:**

- P50 y P99 de latencia.
- Las tres funciones principales de `profile-sample.d`.
- Los nombres de los locks con contención de `lockstat-simple.d`.

**Resultados esperados (para comparación):**

En el mismo Xeon de 8 núcleos, el driver de referencia bajo carga de lectura concurrente de 4 threads muestra una mediana de unos 8 µs, un P99 de unos 60 µs, contención intensa en `perfdemo_softc_mtx`, y los principales perfiles dominados por `perfdemo_read`, `uiomove` y `_mtx_lock_sleep`.

**Errores frecuentes:**

- Ejecutar DTrace antes de que el driver esté cargado. Las sondas `fbt:perfdemo:` no existen hasta que `perfdemo.ko` está en el kernel.
- Escribir scripts que imprimen por evento. El buffer de DTrace puede descartar eventos; agrega siempre y muestra al final.
- Olvidar hacer Ctrl-C en la sesión de DTrace en el momento adecuado. Algunas agregaciones solo se imprimen al detener explícitamente la ejecución.
- Ejecutar la carga de trabajo a una tasa demasiado baja para producir datos interesantes. Los histogramas de latencia de DTrace necesitan al menos decenas de miles de muestras para obtener estimaciones de percentiles limpias.
- Olvidar cargar `dtraceall`. Una instalación básica de DTrace sin proveedores se quejará de proveedores desconocidos.

**Solución de problemas:** Si `dtrace -l | grep perfdemo` está vacío después de cargar el driver, comprueba que `dtraceall` esté cargado (`kldstat | grep dtrace`). Si las sondas siguen sin aparecer, el driver puede haberse compilado sin símbolos de depuración; comprueba que `make` haya usado los flags predeterminados y no haya eliminado `-g`.

### Lab 3: pmcstat `perfdemo` (opcional, requiere hwpmc)

**Objetivo:** Muestrea `perfdemo` con contadores de rendimiento de hardware e interpreta la salida. Este laboratorio es más completo que los anteriores porque `pmcstat` tiene más opciones que DTrace; planifica tiempo adicional.

**Directorio:** `examples/part-07/ch33-performance/lab03-pmcstat/`.

**Prerrequisitos:** Una máquina física o totalmente paravirtualizada en la que los PMC estén disponibles. En la mayoría de máquinas virtuales en la nube y en hipervisores compartidos, los PMC están restringidos; si `pmcstat -L` lista solo un puñado de eventos o la herramienta se niega a arrancar, sigue el camino alternativo con el proveedor `profile` de DTrace.

**Pasos:**

1. Asegúrate de que `hwpmc` esté cargado: `sudo kldload hwpmc` (o confirma que ya está cargado con `kldstat | grep hwpmc`). Comprueba `dmesg | tail` tras la carga; deberías ver una línea parecida a `hwpmc: SOFT/16/64/0x67<REA,WRI,INV,LOG,EV1,EV2,CAS>`.

2. Verifica que la herramienta detecta eventos: `pmcstat -L | head -30`. Deberías ver una combinación de nombres de eventos portables (`cycles`, `instructions`, `cache-references`, `cache-misses`) y nombres específicos del fabricante (en Intel, entradas que empiezan por `cpu_clk_`, `inst_retired`, `uops_issued`, etc.).

3. Ejecuta una sesión de conteo durante una carga de trabajo. El flag `-s` (minúscula) es para conteo en todo el sistema; úsalo para ver la contribución del driver junto con todo lo demás que hace el kernel:

    ```
    # sudo pmcstat -s instructions -s cycles -O /tmp/pd.pmc \
        ./run-workload.sh 100000
    ```

    Cuando la carga de trabajo termine, `pmcstat` imprime los totales:

    ```
    p/instructions p/cycles
        3.2e9          9.5e9
    ```

    Divide instrucciones entre ciclos para calcular el IPC: 3200 millones / 9500 millones = 0,34. Un IPC inferior a 1 en una CPU moderna suele indicar paradas; probablemente por accesos a memoria o predicciones de salto incorrectas.

4. Ejecuta una sesión de muestreo. El flag `-S` (mayúscula) configura el muestreo:

    ```
    # sudo pmcstat -S cycles -O /tmp/pd-cycles.pmc \
        ./run-workload.sh 100000
    # pmcstat -R /tmp/pd-cycles.pmc -T
    ```

    La salida de `-T` es la lista de las N funciones más frecuentes. Deberías ver algo como:

    ```
     %SAMP CUM IMAGE            FUNCTION
      13.2 13.2 kernel          perfdemo_read
       9.1 22.3 kernel          uiomove_faultflag
       8.5 30.8 kernel          copyout
       6.2 37.0 kernel          _mtx_lock_sleep
       4.9 41.9 kernel          _mtx_unlock_sleep
       ...
    ```

    Anota las cinco funciones principales y sus porcentajes.

5. Ejecuta una segunda sesión de muestreo, esta vez con `LLC-misses` para ver dónde se producen los fallos de caché (requiere CPU Intel):

    ```
    # sudo pmcstat -S LLC-misses -O /tmp/pd-llc.pmc \
        ./run-workload.sh 100000
    # pmcstat -R /tmp/pd-llc.pmc -T
    ```

    Compara las entradas principales con las muestras de ciclos. Si las mismas funciones aparecen en ambas, el tiempo está limitado por memoria; si aparecen funciones distintas, las funciones más costosas en ciclos están limitadas por la CPU (aritmética, saltos).

6. Si tienes las herramientas de FlameGraph instaladas (instálalas desde el port `sysutils/flamegraph`: `sudo pkg install flamegraph`), genera un SVG:

    ```
    # sudo pmcstat -R /tmp/pd-cycles.pmc -g -k /boot/kernel > pd.stacks
    # stackcollapse-pmc.pl pd.stacks > pd.folded
    # flamegraph.pl pd.folded > pd.svg
    ```

    Abre `pd.svg` en un navegador. El SVG es interactivo; haz clic en cualquier caja para hacer zoom en ese stack de llamadas.

7. Inspecciona el flame graph. Las cajas inferiores (entrada de la llamada al sistema) deben ser anchas y poco informativas. Por encima de ellas, las funciones de tu driver aparecen como stacks más estrechos. El ancho de cada caja representa su fracción del tiempo de CPU; la altura muestra la profundidad de la llamada.

**Alternativa si hwpmc no está disponible:** Usa el proveedor `profile` de DTrace:

```console
# sudo dtrace -n 'profile-997 /arg0 != 0/ { @[stack()] = count(); }' \
    -o /tmp/pd-prof.txt
```

Déjalo correr un minuto mientras se ejecuta la carga de trabajo, pulsa Ctrl-C y examina `/tmp/pd-prof.txt`. La salida es similar al callgraph de pmcstat, aunque más gruesa porque la tasa de muestreo del perfil es fija.

**Qué debes registrar en tu cuaderno de laboratorio:**

- El IPC de la sesión de conteo.
- Las cinco funciones principales de la sesión de muestreo, con sus porcentajes.
- Las tres funciones principales en la sesión de `LLC-misses` (si está disponible) y si coinciden con la lista de muestras de ciclos.
- Una captura de pantalla o descripción de los stacks dominantes del flame graph.

**Números de referencia esperados (para comparación):**

En el driver de referencia sobre un Xeon de 8 núcleos, el IPC suele ser de 0,3 a 0,5 (limitado por memoria). La función principal suele ser `perfdemo_read` con un 10-15% de los ciclos, seguida de `uiomove`, `copyout` y `_mtx_lock_sleep`. Tras el ajuste (Laboratorios 4 y 6), el IPC debería subir por encima de 1,0 y `_mtx_lock_sleep` debería desaparecer de los primeros puestos.

**Errores frecuentes:**

- Olvidar cargar `hwpmc` antes de ejecutar `pmcstat`. La herramienta informará de *no device* o *no PMCs available*.
- Ejecutar la sesión de muestreo durante un intervalo demasiado corto. Unos pocos segundos de muestras generan datos ruidosos; un minuto suele ser suficiente.
- Malinterpretar la salida de `pmcstat -T`. La columna `%SAMP` es la fracción de muestras que recibió una función en todas las CPU; un valor del 10% significa el 10% del tiempo, no el 10% de las instrucciones.
- Usar el flag incorrecto: `-s` es conteo, `-S` es muestreo. Producen salidas distintas y la diferencia es fácil de pasar por alto al leer los scripts de otra persona.
- Intentar muestrear una función que fue inlineada. `inst_retired.any` cuenta las instrucciones retiradas; las instrucciones de una función inlineada se contabilizan contra el llamador, no contra la función inlineada. Si esperas ver la función X en las muestras y no aparece, comprueba con `objdump -d perfdemo.ko | grep X` si X fue compilada como una función real.

**Solución de problemas:** Si `pmcstat` se cuelga o imprime *ENOENT*, el nombre del evento es incorrecto. `pmcstat -L` lista todos los nombres de eventos que el kernel conoce en tu CPU; elige uno de esa lista. Si la herramienta falla con una señal, puede que `hwpmc` no esté completamente inicializado; descarga y vuelve a cargar el módulo.

### Lab 4: Alineación de caché y contadores por CPU

**Objetivo:** Medir el impacto en el rendimiento de la alineación de línea de caché y la API `counter(9)` en comparación con un atomic simple, bajo carga concurrente de múltiples CPU.

**Directorio:** `examples/part-07/ch33-performance/lab04-cache-aligned/`.

**Prerrequisitos:** Laboratorios 1 y 2 completados, para que conozcas el comportamiento del driver de referencia y sepas usar DTrace.

**Pasos:**

1. Construye tres variantes del driver, cada una en su propio subdirectorio:
   - `v1-atomic`: contador atomic simple, diseño predeterminado.
   - `v2-aligned`: contador atomic simple con `__aligned(CACHE_LINE_SIZE)` y relleno a su alrededor para aislar la línea de caché.
   - `v3-counter9`: contador `counter(9)` en lugar de un atomic.

   Cada subdirectorio tiene su propio `Makefile`. Ejecuta `make` en cada uno. Las únicas diferencias entre los tres archivos fuente están en el diseño del softc y en las líneas de incremento del contador.

2. Identifica el número de CPU de tu sistema: `NCPU=$(sysctl -n hw.ncpu)`.

3. Para cada variante, ejecuta el script de lectura multi-thread `./run-parallel.sh <N>` con `N` igual al número de CPU:

    ```
    # cd v1-atomic && sudo kldload ./perfdemo.ko && ./run-parallel.sh $NCPU
    # sudo kldunload perfdemo

    # cd ../v2-aligned && sudo kldload ./perfdemo.ko && ./run-parallel.sh $NCPU
    # sudo kldunload perfdemo

    # cd ../v3-counter9 && sudo kldload ./perfdemo.ko && ./run-parallel.sh $NCPU
    # sudo kldunload perfdemo
    ```

    El script lanza `N` threads lectores, cada uno fijado a una CPU diferente, cada uno emitiendo lecturas tan rápido como puede durante un tiempo fijo. Al final imprime el rendimiento agregado.

4. Registra el rendimiento (ops/segundo) de cada variante.

5. Ahora repite la prueba con la mitad y el doble del número de threads:

    ```
    # ./run-parallel.sh $((NCPU / 2))
    # ./run-parallel.sh $((NCPU * 2))
    ```

    Compara los tres valores de throughput para cada recuento de threads. Las ejecuciones con la mitad de las CPU suelen mostrar menos contención; las ejecuciones con el doble muestran contención elevada y efectos del planificador.

6. Para la demostración más clara, ejecuta un script de DTrace sobre cada variante mientras la carga de trabajo está en ejecución:

    ```
    # sudo dtrace -n 'lockstat:::adaptive-block /
        ((struct lock_object *)arg0)->lo_name == "perfdemo_mtx" / {
            @ = count();
        }'
    ```

    `v1-atomic` debería mostrar un bloqueo significativo; `v3-counter9` casi ninguno.

**Lo que deberías observar:**

- `v1-atomic` es la línea base. Su throughput no escala bien más allá de aproximadamente la mitad de las CPU; el rebote de líneas de caché del contador atómico se convierte en un cuello de botella.
- `v2-aligned` mejora si los contadores estaban previamente comprimidos junto con otros campos calientes en la misma línea de caché. Si el softc original ya tenía el contador atómico en su propia línea, la alineación no produce ningún efecto.
- `v3-counter9` escala de forma casi lineal con el número de CPU. Cada CPU actualiza su propia copia por CPU; no hay rebote de líneas de caché en el contador.

**Resultados esperados (para comparación):**

En un Xeon de 8 núcleos con 8 threads ejecutando esta carga de trabajo:

- `v1-atomic`: alrededor de 600K ops/sec en total.
- `v2-aligned`: alrededor de 680K ops/sec en total.
- `v3-counter9`: alrededor de 2,0M ops/sec en total.

Con 16 threads (sobreasignados):

- `v1-atomic`: alrededor de 550K (empieza a caer).
- `v2-aligned`: alrededor de 650K (también cae).
- `v3-counter9`: alrededor de 1,8M (limitado por la sobrecarga del planificador, no por la contención).

El contador por CPU escala; los atómicos no.

**Lo que deberías anotar en tu cuaderno de trabajo:**

- Los valores de throughput de cada variante y cada recuento de threads.
- El recuento de bloqueos de lock de cada variante según el script de DTrace.
- Una breve nota sobre lo que los números te dicen de tu hardware (cuánta contención presenta la interconexión entre CPU y si el hyper-threading ayuda o perjudica).

**Errores habituales:**

- Ejecutar el script paralelo con un solo thread. El objetivo del experimento es la contención; una ejecución con un solo thread la elimina y oculta el efecto.
- Sacar conclusiones de una diferencia pequeña. Si los tres valores difieren en menos de un 10%, el ruido de medición podría explicar la diferencia; repite la prueba al menos tres veces y comprueba la varianza.
- Olvidar que `UMA_ALIGN_CACHE` en las zonas UMA realiza un trabajo similar por ti. El primitivo `counter(9)` es una solución limpia; rara vez necesitarás alinear contadores manualmente.
- Esperar un escalado lineal de `v1-atomic`. Los contadores atómicos *no pueden* escalar más allá del ancho de banda de coherencia del sistema de memoria; una vez que ese ancho de banda se satura, añadir más CPU empeora el throughput en lugar de mejorarlo.
- Olvidar descargar el módulo entre ejecuciones. Cargar una variante nueva sobre una que ya está en ejecución no la reemplaza; usa `kldunload perfdemo` y luego `kldload ./perfdemo.ko`.

**Resolución de problemas:** Si los valores de throughput son inconsistentes entre ejecuciones, el escalado de frecuencia de la CPU es probablemente la causa. Fija la frecuencia con `sysctl dev.cpufreq.0.freq=$(sysctl -n dev.cpufreq.0.freq_levels | awk '{print $1}' | cut -d/ -f1)` (ajústalo para tu CPU). En sistemas donde esto no esté disponible, desactiva el escalado dinámico de frecuencia en la BIOS/firmware.

### Lab 5: División entre interrupción y taskqueue

**Objetivo:** Comparar el trabajo realizado en el contexto de interrupción con el trabajo diferido mediante taskqueue, incluyendo el comportamiento bajo carga.

**Directorio:** `examples/part-07/ch33-performance/lab05-interrupt-tq/`.

**Requisitos previos:** Lab 1 completado.

**Pasos:**

1. Inspecciona las dos variantes del directorio del laboratorio:
   - `v1-in-callout`: un callout se dispara cada milisegundo y realiza su procesamiento de forma inline. El "manejador de interrupción" es la función del callout; todo el trabajo ocurre en ese contexto.
   - `v2-taskqueue`: el callout encola una tarea; un thread de taskqueue la desencola y la procesa. El callout en sí no hace nada más.

2. Compila cada una: `make` en cada subdirectorio.

3. Carga la primera variante: `cd v1-in-callout && sudo kldload ./perfdemo.ko`.

4. Ejecuta el script de medición: `./measure-latency.sh`. Lanza un lector en espacio de usuario que espera los datos procesados por el callout y registra el tiempo de reloj de pared entre el disparo del callout y el momento en que el lector recibe el resultado. El script imprime la latencia mediana y P99 sobre 10.000 iteraciones.

5. Descarga y carga la segunda variante: `sudo kldunload perfdemo && cd ../v2-taskqueue && sudo kldload ./perfdemo.ko`.

6. Ejecuta el mismo script de medición.

7. Registra la latencia mediana y P99 de cada variante en reposo.

8. Repite ahora con carga artificial en el sistema. En un segundo shell, inicia una carga intensiva de CPU: `make -j$(sysctl -n hw.ncpu) buildworld` (si tienes el código fuente del sistema descargado) o un generador de carga más sencillo: `for i in $(seq 1 $(sysctl -n hw.ncpu)); do yes > /dev/null & done`.

9. Repite la medición para cada variante. El planificador ahora está ocupado; las dos variantes deberían diferir de forma más marcada.

10. Detén la carga artificial (`killall yes` u equivalente). Comprueba que el uso de CPU vuelve al estado de reposo antes de dar la prueba por finalizada.

**Qué deberías observar:**

- `v1-in-callout` en reposo presenta una latencia mediana menor, pero experimenta picos cuando el contexto del callout es expulsado por el planificador (lo que ocurre en el límite del taskqueue).
- `v2-taskqueue` en reposo tiene unos pocos microsegundos de latencia adicional (el despacho del callout al taskqueue), pero un comportamiento más estable.
- Bajo carga, la latencia de `v1-in-callout` se vuelve muy variable; el contexto del callout compite con el generador de carga por tiempo de CPU.
- Bajo carga, la latencia de `v2-taskqueue` es mayor pero más estable; la clase de planificación del thread de taskqueue es más predecible.

**Resultados esperados (para comparación):**

En un Xeon de 8 núcleos:

- `v1-in-callout` en reposo: mediana 8 us, P99 30 us.
- `v2-taskqueue` en reposo: mediana 12 us, P99 40 us.
- `v1-in-callout` bajo carga: mediana 15 us, P99 2000 us (picos por expulsión del planificador).
- `v2-taskqueue` bajo carga: mediana 20 us, P99 250 us (más estable).

La mejora del P99 bajo carga es la razón principal por la que los drivers de producción prefieren los taskqueues para cualquier trabajo de cierta envergadura.

**Qué debes registrar:**

- Latencia mediana y P99 en reposo para cada variante.
- Latencia mediana y P99 bajo carga para cada variante.
- Una breve nota sobre cuándo utilizarías cada una en la práctica.

**Errores frecuentes:**

- Medir el rendimiento solo en reposo. Las dos variantes muestran sus mayores diferencias bajo carga del sistema; prueba ambos estados.
- Confundir los callouts con interrupciones reales. Un callout es un temporizador por software; el laboratorio lo utiliza como sustituto de una interrupción porque el comportamiento temporal de las interrupciones reales depende del hardware. Las conclusiones son transferibles, pero los números absolutos dependen de tu planificador.
- Olvidar detener el generador de carga antes de dar la prueba por finalizada. Dejar `yes` en ejecución perjudica a la siguiente prueba.
- Sacar conclusiones de una única medición. La latencia es una distribución; una sola ejecución es un punto, no una distribución. Recoge al menos 10.000 mediciones por configuración.

### Lab 6: Publicar la v2.3 optimizada

**Objetivo:** Aplicar todo el trabajo de ajuste de rendimiento para producir un `perfdemo` v2.3 optimizado y terminado, con árbol de sysctl, página de manual e informe de rendimiento.

**Directorio:** `examples/part-07/ch33-performance/lab06-v23-final/`.

**Requisitos previos:** Labs 1 a 5 completados. Deberías tener los números de referencia del Lab 1 y estar familiarizado con el trabajo de contadores por CPU del Lab 4.

**Pasos:**

1. Inspecciona el estado inicial del driver en el directorio del laboratorio. Es el `perfdemo` de referencia sin ningún ajuste aplicado; los comentarios a lo largo del código fuente indican dónde irá cada pasada de ajuste.

2. Aplica los tres cambios que trabajamos a lo largo del capítulo:

   **Cambio 1: `counter(9)` para todas las estadísticas.** Sustituye los contadores atómicos por `counter_u64_t`. Actualiza los manejadores de sysctl para utilizar sysctls procedurales que llamen a `counter_u64_fetch`. Marca cada cambio con un breve comentario que describa lo que se modificó.

   **Cambio 2: Alineación a línea de caché en los campos calientes del softc.** Identifica los campos del softc que se escriben frecuentemente desde múltiples CPUs. Para cada uno, añade `__aligned(CACHE_LINE_SIZE)` y el relleno adecuado. Nota: si utilizas `counter(9)` para todos los contadores, la mayor parte del trabajo de alineación de caché ya está hecha; solo los campos calientes que no son contadores (como punteros a pool o indicadores de estado) necesitan alineación manual.

   **Cambio 3: Buffers preasignados.** Crea una zona UMA en el momento de `MOD_LOAD` con `UMA_ALIGN_CACHE`. Utiliza `uma_zalloc`/`uma_zfree` en el camino caliente en lugar de `malloc`/`free`. Destruye la zona en `MOD_UNLOAD`.

3. Compila y carga: `make && sudo kldload ./perfdemo.ko`. Ejecuta una prueba rápida de humo con `./run-workload.sh 10000` para confirmar que el driver funciona.

4. Elimina del código fuente cualquier marcador `PERF:`. Búscalos con: `grep -n 'PERF:' perfdemo.c`. Cada línea debe eliminarse o, si la medición se va a conservar, el marcador debe sustituirse por un comentario permanente.

5. Actualiza el macro `MODULE_VERSION()` a `3`. Localiza la línea y cámbiala:

    ```c
    MODULE_VERSION(perfdemo, 2);   /* before */
    MODULE_VERSION(perfdemo, 3);   /* after */
    ```

6. Actualiza la página de manual del driver. Si no existe ninguna, copia `/usr/src/share/man/man4/null.4` como plantilla y adáptala. La página debe contener:

    - `.Nm perfdemo`
    - Una descripción de una línea en `.Nd`
    - Una sección `.Sh DESCRIPTION` que explique qué hace el driver.
    - Una sección `.Sh TUNABLES` que documente los parámetros de sysctl.
    - Una sección `.Sh SEE ALSO` con referencias a páginas relacionadas.

    Verifica la sintaxis de la página: `mandoc -Tlint perfdemo.4`. Renderízala: `mandoc -Tascii perfdemo.4 | less`.

7. Ejecuta la prueba de rendimiento final: `./final-benchmark.sh`. El script somete al driver a varias cargas de trabajo (secuencial de un solo thread, aleatoria de un solo thread, paralela multithreaded) y registra la latencia mediana y P99 más el rendimiento para cada una. Copia la salida en tu cuaderno de registro.

8. Redacta el informe de rendimiento. Usa la plantilla de la sección 8 como punto de partida. Completa:

    - Los números "Antes" del Lab 1.
    - Los números "Después" del paso 7.
    - Los tres cambios aplicados.
    - Los detalles de la máquina.
    - Los parámetros configurables introducidos (si los hay).

    Guarda el informe como `PERFORMANCE.md` en el directorio del laboratorio.

9. Descarga el driver: `sudo kldunload perfdemo`. Confirma que se descargó limpiamente, sin threads bloqueados ni memoria perdida (`vmstat -z | grep perfdemo` debe mostrar cero asignados, o la zona no debe aparecer si fue destruida).

**Qué debes producir:**

- `perfdemo.c` con los tres cambios de ajuste aplicados y sin marcadores `PERF:`.
- `perfdemo.4` (página de manual) con una sección TUNABLES.
- `PERFORMANCE.md` (informe de rendimiento).
- Build limpio, carga limpia, descarga limpia.

**Resultados esperados (para comparación):**

El driver v2.3 en un Xeon de 8 núcleos:

- Lecturas secuenciales de un solo thread: mediana 9 us, P99 60 us, rendimiento 400K ops/s.
- Lecturas paralelas multithreaded (8 threads): mediana 11 us, P99 85 us, rendimiento 2,8M ops/s.

Compara con la línea de base (aproximadamente 30 us de mediana, 330K ops/s, 600K con 8 threads). La v2.3 triplica el rendimiento en un solo thread y lo multiplica por casi 5 en paralelo.

**Errores frecuentes:**

- Saltarse la limpieza. Un driver con marcadores `PERF:` no está listo para publicar.
- Publicar números de rendimiento sin contexto. Todo número de benchmark necesita el entorno en que fue obtenido.
- Actualizar el número de versión sin la entrada correspondiente en el registro de cambios. El cambio de versión indica un cambio de comportamiento; el registro de cambios documenta qué cambió.
- Redactar la página de manual con prosa vaga. La sección `TUNABLES` en particular debe especificar rangos de valores exactos, valores por defecto y compromisos de diseño.
- No ejecutar `mandoc -Tlint`. Una página de manual con errores es tan mala como no tener ninguna.

### Cerrando los laboratorios

Los laboratorios recorren un ciclo de ajuste completo: línea de base, DTrace, PMC, memoria, interrupciones, publicación. Cuando termines el Lab 6, habrás utilizado todas las herramientas del capítulo al menos una vez y tendrás un artefacto `perfdemo` concreto que podrás llevar adelante. Si tienes tiempo, los ejercicios de desafío que siguen extienden el driver en direcciones demasiado específicas para el texto principal, pero enseñan los hábitos a los que apuntaba el capítulo.

### Tabla resumen de los laboratorios

Para referencia rápida, un vistazo a los laboratorios:

| Lab | Qué enseña | Herramienta principal | Resultado |
|-----|------------|----------------------|-----------|
| 1   | Medición de línea de base | `sysctl(8)` | Número de ops/s |
| 2   | Perfilado a nivel de función | `dtrace(1)` | Histograma de latencia, funciones calientes |
| 3   | Perfilado a nivel de hardware | `pmcstat(8)` | IPC, funciones calientes por PMC |
| 4   | Ajuste de contención en contadores | `counter(9)` | Comparación de escalado |
| 5   | Compromisos del contexto de interrupción | taskqueue | Latencia bajo carga |
| 6   | Disciplina de publicación | `MODULE_VERSION`, página de manual | Un driver v2.3 limpio |

Cada laboratorio está pensado para una hora si ya dominas las herramientas, o dos horas si las estás aprendiendo por primera vez. El conjunto completo representa una tarde de trabajo sólida; los ejercicios de desafío lo amplían a un fin de semana para los lectores que quieran una práctica más profunda.

### Dependencias entre laboratorios

Los laboratorios son secuenciales. El Lab 1 establece los números de referencia. El Lab 2 utiliza esos números como comparación antes/después. El Lab 3 es opcional (hwpmc puede no estar disponible), pero sus conceptos alimentan el Lab 4. El Lab 4 es donde se desarrolla la lección principal de ajuste de memoria. El Lab 5 es independiente de los demás, pero usa el mismo driver `perfdemo`. El Lab 6 asume que has asimilado los Labs 1-5 y estás listo para aplicar la secuencia completa.

Si el tiempo es escaso, haz los Labs 1, 2, 4 y 6 en orden. Esos cuatro son el núcleo del capítulo.

## Ejercicios de desafío

Estos ejercicios van más allá de los laboratorios principales. Cada uno es opcional y puede realizarse en cualquier orden. Están diseñados para extender las ideas del capítulo de formas que se generalizan al trabajo real con drivers.

### Desafío 1: Presupuesto de latencia para un driver real

Elige un driver real de FreeBSD que te interese. Buenos candidatos son `/usr/src/sys/dev/e1000/` para la familia de drivers Ethernet Intel e1000 (incluyendo `em` e `igb`), `/usr/src/sys/dev/nvme/` para el driver de almacenamiento NVMe, `/usr/src/sys/dev/sound/pcm/` para el núcleo de audio, o `/usr/src/sys/dev/virtio/block/` para el dispositivo de bloques virtio.

Lee el código fuente del driver y redacta un presupuesto de latencia. Para cada fase de su camino caliente (filtro de interrupción, ithread, manejador de taskqueue, callout de softclock, activación en espacio de usuario), estima la latencia máxima que el driver puede permitirse y aun así satisfacer las necesidades de su carga de trabajo. Una entrada de presupuesto razonable tiene este aspecto:

```text
phase                  target    justification
---------------------  --------  --------------
PCIe read of status    < 500ns   register read on local bus
interrupt filter       < 2us     must not steal ithread time
ithread hand-off       < 10us    softclock expects prompt wake
taskqueue deferred     < 100us   reasonable for RX processing
wakeup to userland     < 50us    scheduler-dependent
```

A continuación, audita el código real: ¿justifica el código de cada fase tu estimación? Si el manejador del ithread toma un mutex de espera `MTX_DEF` que podría bloquearse, ¿es tu presupuesto honesto?

El ejercicio no trata de precisión; los números anteriores son ilustrativos. Trata de mirar un driver real con ojos de rendimiento y razonar sobre cómo su diseño encaja en una carga de trabajo. Registra tu presupuesto, tu auditoría y cualquier discrepancia en tu cuaderno de registro. Este hábito, aplicado a muchos drivers, construye intuición más rápido que cualquier otra práctica.

**Pista:** Busca comentarios explícitos sobre temporización en el código fuente. Los drivers reales tienen ocasionalmente comentarios de la forma `/* this must complete within N us because ... */`. Esos comentarios son un regalo. Léelos.

### Desafío 2: Comparar tres asignadores

Para un camino caliente de driver que asigna buffers de tamaño fijo (digamos, buffers de 64 bytes), mide el rendimiento de tres implementaciones:

1. `malloc(9)` con una llamada por operación a `malloc(M_TEMP, M_WAITOK)` seguida de `free(M_TEMP)`.
2. Una zona UMA creada con `uma_zcreate("myzone", 64, ..., UMA_ALIGN_CACHE, 0)`, con `uma_zalloc()`/`uma_zfree()` en la ruta caliente.
3. Un array de 1024 buffers preasignado junto con una freelist, asignado una única vez en el attach y reutilizado en la ruta caliente mediante un pop/push atómico simple.

Conecta cada variante a un clon de `perfdemo` y ejecuta la misma carga de trabajo de lectura en los tres. Mide:

- Rendimiento (lecturas por segundo).
- Latencia mediana y P99 (a partir de un histograma respaldado por contadores).
- Tasa de fallos de caché (usando `pmcstat -S LLC_MISSES` o el alias `LLC_MISSES` del proveedor `profile` de DTrace, si está disponible).

En una máquina amd64 moderna, deberías observar un salto significativo entre las opciones 1 y 2 (aproximadamente 1,5x a 3x de rendimiento, según la contención), y un salto más pequeño pero real entre 2 y 3 (quizás entre un 10% y un 30%). La forma del histograma suele ser más interesante que la media: `malloc(9)` tiene una cola larga impulsada por la presión de VM, UMA es más ajustada y el ring preasignado tiene la cola más estrecha.

Escribe una nota breve que explique los compromisos. El ring preasignado es el más rápido pero también el más rígido (el número de buffers queda fijado en el attach). UMA es la opción predeterminada correcta para cargas de trabajo variables. `malloc(9)` es perfectamente adecuado para asignaciones poco frecuentes, de gran tamaño o que no se encuentren en la ruta caliente. No concluyas de este ejercicio que `malloc(9)` es malo; con frecuencia es la elección correcta para las rutas de control.

### Desafío 3: Escribe un script de DTrace útil

Escribe un script de DTrace que responda a una pregunta que realmente tengas sobre un driver. Algunos ejemplos:

- *¿Cuánto tiempo pasa mi driver esperando en su propio mutex?* Usa `lockstat:::adaptive-spin` y `lockstat:::adaptive-block` filtrados por la dirección del lock.
- *¿Cuál es la distribución de los tamaños de solicitud que llegan a mi driver?* Captura `args[0]->uio_resid` desde `fbt:myfs:myfs_read:entry` en una agregación `quantize()`.
- *¿Qué proceso de userland consume más mi driver?* Agrega `@[execname] = count()` en el punto de entrada del driver.
- *¿Cuál es la latencia en el percentil 99 del camino de escritura, desglosada por tamaño de solicitud?* Usa una agregación bidimensional `@[bucket(size)] = quantize(latency)` donde `bucket()` agrupa los tamaños en un puñado de rangos.
- *¿Se llama alguna vez a mi driver en contexto de interrupción?* Usa `self->in_intr = curthread->td_intr_nesting_level` y agrega por ese indicador.

Guarda el script como archivo `.d` en tu directorio de laboratorio. Anótalo con un comentario de cabecera que indique:

- El propósito del script (una frase).
- Las sondas que utiliza.
- El comando de invocación, incluidos los argumentos.
- Una muestra de la salida y cómo interpretarla.

Un script de DTrace que tiene la respuesta a una pregunta que te hace un compañero vale más que casi cualquier otra cosa en el cuaderno de un ingeniero. Mantén una carpeta creciente con estos scripts a lo largo de los años; se acumulan y multiplican su valor.

**Pista:** Antes de comprometerte con una agregación compleja, ejecuta la sonda con un `printf()` sobre un puñado de eventos y confirma que las variables que quieres capturar están realmente disponibles y contienen los valores que esperas. Añade la agregación solo una vez que sepas que la sonda básica se dispara correctamente.

### Desafío 4: Instrumenta un camino de lectura sin contaminarlo

Parte de un driver al estilo `perfdemo` donde el camino de lectura se ejecuta en un bucle intensivo con alto rendimiento. Añade suficiente instrumentación para responder dos preguntas:

1. ¿Cuál es la latencia en P50 y P99 de este camino de lectura?
2. ¿Cuál es la distribución de los tamaños de solicitud?

Mide el rendimiento antes y después de la instrumentación. Aspira a menos de un 5 % de pérdida de rendimiento debida a la instrumentación. El ejercicio recompensa el pensar en el coste de cada sonda. Jerarquía de costes aproximada en amd64 moderno, de más barato a más caro:

- Sonda SDT estática en tiempo de compilación con DTrace desactivado: coste prácticamente nulo.
- Un incremento de `counter(9)` en una ranura por CPU: unos pocos ciclos.
- Un `atomic_add_int()` sobre un contador compartido: decenas o centenares de ciclos bajo contención.
- Una llamada a `sbinuptime()`: decenas de ciclos.
- Una llamada a `log(9)`: cientos o miles de ciclos más posible contención de lock.
- Una llamada a `printf()`: miles de ciclos más una posible suspensión.

Diseña la instrumentación en consecuencia. Una forma razonable: `counter(9)` por CPU para los recuentos, cubos de histograma con `DPCPU_DEFINE` para la distribución de latencias, ningún `printf()` por llamada, sin atomics compartidos. Mide el rendimiento antes y después con cuidado y escribe lo que encontraste.

**Pista:** Compila con `-O2` e inspecciona el ensamblado generado para la función crítica con `objdump -d perfdemo.ko | sed -n '/<perfdemo_read>/,/^$/p'` para confirmar que la instrumentación no anuló las optimizaciones del compilador, como empujar a la pila variables que la versión sin instrumentación mantenía en registros.

### Desafío 5: Expón un histograma en tiempo real

Extiende el árbol de sysctl de `perfdemo` con un histograma de latencias. Define un conjunto de cubos de latencia (por ejemplo, de 0 a 1 µs, de 1 a 10 µs, de 10 a 100 µs, de 100 µs a 1 ms, de 1 ms a 10 ms, y 10 ms o más). Para cada lectura, mide la latencia, encuentra su cubo e incrementa un contador por CPU para ese cubo. Expón el array completo de cubos como un único sysctl que devuelva una estructura opaca.

Un manejador razonable:

```c
static int
perfdemo_sysctl_hist(SYSCTL_HANDLER_ARGS)
{
    uint64_t snapshot[PD_HIST_BUCKETS];
    for (int i = 0; i < PD_HIST_BUCKETS; i++)
        snapshot[i] = counter_u64_fetch(perfdemo_read_hist[i]);
    return (SYSCTL_OUT(req, snapshot, sizeof(snapshot)));
}
```

Desde userland, escribe un script en Python o en shell que consulte el sysctl cada segundo con `sysctl -b` e imprima un gráfico de barras textual en tiempo real de la distribución. Compara la distribución en vivo durante una carga de trabajo estable, durante una carga intermitente y durante un periodo de inactividad.

El ejercicio demuestra el ciclo completo: contadores por CPU en el kernel, agregación en el momento de la lectura, un sysctl binario, visualización en userland y un flujo de observación continuo. Este es el patrón que usan los drivers reales para enviar telemetría de latencias.

**Pista:** `sysctl -b` lee los bytes en bruto; tu código de userland debe desempaquetarlos con `struct.unpack` en Python o con un pequeño programa en C. No expongas los cubos como una cadena formateada desde el kernel; deja que los números en bruto fluyan hacia fuera y deja que userland se encargue de la presentación.

### Desafío 6: Ajusta el rendimiento para dos cargas de trabajo

Elige dos cargas de trabajo realistas para el driver. Por ejemplo, *lecturas pequeñas y sensibles a la latencia a 100 por segundo* (limitadas por latencia) y *lecturas grandes y orientadas al rendimiento a 1000 por segundo* (limitadas por throughput). Identifica los parámetros del driver que optimizan cada carga de trabajo. Los candidatos incluyen:

- Tamaño de buffer (pequeño para latencia, grande para rendimiento).
- Umbral de fusión, si lo hay (ninguno para latencia, sí para rendimiento).
- Tamaño de lote para operaciones en lote (pequeño para latencia, grande para rendimiento).
- Polling frente a interrupciones (interrupciones para latencia, polling para rendimiento).
- Nivel de registro de depuración (desactivado en ambos casos; actívalo solo para resolución de problemas).

Escribe una tabla al estilo `loader.conf` que muestre la configuración óptima para cada carga de trabajo. Explica los compromisos: por qué la configuración de latencia perjudicaría el rendimiento, por qué la configuración de rendimiento perjudicaría la latencia, y cuál sería el coste de elegir una configuración intermedia.

Este desafío hace concreto el punto de la Sección 1: un mismo driver puede ser rápido de distintas maneras según la carga de trabajo. El ajuste de drivers reales a menudo implica exponer estos controles con valores predeterminados razonables y documentar la carga de trabajo a la que corresponde cada uno.

**Pista:** No adivines la mejor configuración; mídela. Ejecuta cada carga de trabajo con cada configuración y rellena una tabla. La configuración ganadora a menudo no es la que habrías supuesto, sobre todo en los compromisos entre rendimiento y latencia, donde las interacciones de caché y planificador sorprenden a ingenieros experimentados.

### Desafío 7: Escribe la página de manual

Si `perfdemo` no tiene página de manual, escríbela. Sigue la convención de `/usr/src/share/man/man4/`: copia una página simple existente como `null(4)` o `mem(4)`, cambia el nombre, documenta el dispositivo, los controles sysctl, los parámetros configurables y los errores relevantes.

Un esqueleto razonable:

```text
.Dd March 1, 2026
.Dt PERFDEMO 4
.Os
.Sh NAME
.Nm perfdemo
.Nd performance demonstration pseudo-device
.Sh SYNOPSIS
To load the driver as a module at boot time, place the following line in
.Xr loader.conf 5 :
.Pp
.Dl perfdemo_load="YES"
.Sh DESCRIPTION
The
.Nm
driver is a pseudo-device used to illustrate performance measurement and
tuning techniques in FreeBSD device drivers.
...
.Sh SYSCTL VARIABLES
.Bl -tag -width "hw.perfdemo.bytes"
.It Va hw.perfdemo.reads
Total number of reads served since load.
...
.El
.Sh SEE ALSO
.Xr sysctl 8 ,
.Xr dtrace 1
.Sh HISTORY
The
.Nm
driver first appeared as a companion to
.Em FreeBSD Device Drivers: From First Steps to Kernel Mastery .
```

Verifica el formato con `mandoc -Tlint perfdemo.4`. Renderízala con `mandoc -Tascii perfdemo.4 | less`.

Una página de manual es un compromiso pequeño pero real con el resto del sistema. Convierte una herramienta privada en una compartida. También te obliga a nombrar y describir cada control, lo que en ocasiones revela controles que no necesitan existir.

**Pista:** El lenguaje mdoc es conciso pero estricto. Lee `mdoc(7)` una vez y usa una página de manual simple reciente como plantilla. No inventes macros de mdoc; usa solo las documentadas.

### Desafío 8: Perfila un driver desconocido

Elige un driver de FreeBSD con el que no hayas trabajado. En un sistema de prueba donde el hardware esté presente (o en una VM con un dispositivo emulado compatible), ejecuta `pmcstat` o un perfil de DTrace durante una carga de trabajo realista. Objetivos adecuados para VM:

- `virtio_blk` bajo una carga generada por `dd if=/dev/vtbd0 of=/dev/null bs=1M count=1024`.
- `virtio_net` bajo una carga generada por `iperf3`.
- `xhci(4)` si tienes dispositivos USB conectados.
- `urtw(4)` o drivers inalámbricos similares si tienes el hardware.

Identifica las tres funciones principales por tiempo de CPU. Para cada una, lee el código fuente y formula una hipótesis sobre qué hace esa función que consume tanto tiempo. Registra la hipótesis en tu cuaderno de laboratorio. Por ejemplo: *`virtio_blk_enqueue` pasa la mayor parte de su tiempo en `bus_dmamap_load_ccb` porque cada solicitud copia en un bounce buffer.*

No tienes que verificar la hipótesis. El ejercicio trata de adquirir el hábito de observar un driver desconocido con herramientas de medición y razonar sobre los datos que producen. La hipótesis, sea correcta o no, afila la siguiente lectura del código fuente.

**Pista:** Si la función principal aparece en código común del kernel (`copyin`, `bcopy`, `memset`, spinlocks), eso sigue siendo información útil: te indica dónde pasa el tiempo el driver, aunque la causa raíz esté fuera del driver en sí. Sigue a los llamadores hasta la función del driver y formula la hipótesis allí.

### Cerrando los desafíos

Los desafíos amplían las ideas del capítulo en direcciones que el texto principal no podía cubrir. Si tienes tiempo para uno o dos, refuerzan todo lo que el capítulo enseñó. Si no tienes tiempo para ninguno, está bien: los laboratorios principales te dan la experiencia esencial.

Una estrategia de selección útil para tiempo limitado: elige un desafío de la familia de *lectura* (Desafío 1 u 8), uno de la familia de *instrumentación* (Desafío 3, 4 o 5) y uno de la familia de *publicación* (Desafío 6 o 7). Esa combinación ejercita las tres habilidades que el capítulo desarrolla: leer drivers existentes con ojos de rendimiento, instrumentar drivers nuevos de forma limpia y dejar drivers terminados listos para otros.

## Referencia de patrones de rendimiento

El capítulo cubrió una gran superficie. Esta sección consolida los patrones prácticos en una referencia compacta que puedes consultar cuando trabajes en un driver real. Cada patrón nombra la situación a la que se aplica, la técnica y la primitiva de FreeBSD a la que acudir.

### Patrón: necesitas un contador en el camino crítico

**Situación:** Quieres contar un evento que ocurre miles o millones de veces por segundo, y tu medición no debe dominar el coste del evento.

**Técnica:** Usa `counter(9)`. Declara un `counter_u64_t`, asígnalo en attach con `counter_u64_alloc(M_WAITOK)`, increméntalo en el camino crítico con `counter_u64_add(c, 1)`, súmalo al leer con `counter_u64_fetch(c)` y libéralo en detach con `counter_u64_free(c)`.

**Ruta de la primitiva:** `/usr/src/sys/sys/counter.h`, `/usr/src/sys/kern/subr_counter.c`.

**Por qué este patrón:** `counter(9)` usa ranuras por CPU actualizadas sin atomics. La lectura las combina. El incremento es un puñado de ciclos sin contención de línea de caché.

**Antipatrón:** Un `atomic_add_64()` sobre un `uint64_t` compartido. Cada incremento hace un viaje de ida y vuelta de la línea de caché; a alta frecuencia, el contador se convierte en el cuello de botella.

### Patrón: necesitas una asignación en el camino crítico

**Situación:** Quieres asignar un buffer de tamaño fijo muchas veces por segundo y `malloc(9)` aparece en los perfiles.

**Técnica:** Crea una zona UMA en attach con `uma_zcreate("mydrv_buf", sizeof(struct my_buf), NULL, NULL, NULL, NULL, UMA_ALIGN_CACHE, 0)`. Asigna en el camino crítico con `uma_zalloc(zone, M_NOWAIT)`. Libera con `uma_zfree(zone, buf)`. Destruye en detach con `uma_zdestroy(zone)`.

**Ruta de la primitiva:** `/usr/src/sys/vm/uma.h`, `/usr/src/sys/vm/uma_core.c`.

**Por qué este patrón:** UMA le da a cada CPU una caché de buffers libres. Asignar y liberar en la misma CPU es típicamente un pop y un push desde una pila por CPU, sin coordinación entre CPUs. `UMA_ALIGN_CACHE` evita el false sharing entre buffers adyacentes.

**Antipatrón:** Un `malloc(M_DEVBUF, M_WAITOK)` por solicitud en el camino crítico. El asignador de propósito general tiene más sobrecarga y menos localidad que una zona dedicada.

### Patrón: necesitas un pool preasignado

**Situación:** Tienes un número fijo y conocido de buffers de trabajo (por ejemplo, un recuento de descriptores de anillo ligado al hardware) y quieres coste cero del asignador en el camino crítico.

**Técnica:** En attach, asigna el array una sola vez con `malloc()`. Mantén la lista de libres como un SLIST simple o una pila. Extrae e inserta desde la lista de libres bajo un único lock (o un puntero atómico si se justifica un diseño sin lock). Libera el array completo en detach.

**Por qué este patrón:** Un pool preasignado tiene el menor coste posible por solicitud y la latencia más predecible. La rigidez (recuento fijo) es el precio a pagar.

**Antipatrón:** Usar `malloc()` o UMA cuando el recuento es conocido y fijo. La indirección adicional cuesta ciclos sin ningún beneficio.

### Patrón: necesitas evitar el false sharing

**Situación:** Dos variables que se actualizan con frecuencia residen en la misma línea de caché y son escritas por distintas CPUs. Cada escritura invalida la copia de la otra CPU, provocando un ping-pong.

**Técnica:** Coloca cada variable activa en su propia línea de caché con `__aligned(CACHE_LINE_SIZE)`. Para datos por CPU, utiliza `DPCPU_DEFINE`, que añade el relleno automáticamente.

**Ruta del primitivo:** `/usr/src/sys/amd64/include/param.h` y sus equivalentes en `/usr/src/sys/<arch>/include/param.h` para `CACHE_LINE_SIZE` (incluido a través de `<sys/param.h>`), y `/usr/src/sys/sys/pcpu.h` para las macros DPCPU.

**Por qué este patrón:** Los protocolos de coherencia de caché trasladan líneas de caché completas; si dos escritores comparten una línea, cada escritura de uno invalida la caché del otro. Separar las variables tiene un coste mínimo en padding y elimina el ping-pong.

**Antipatrón:** Añadir alineación sin medir antes. Muchas variables no necesitan alineación; añadirla a ciegas desperdicia memoria sin mejorar el rendimiento.

### Patrón: necesitas medir la latencia sin contaminarla

**Situación:** Quieres medir la latencia de una ruta caliente sin que la medición domine el coste.

**Técnica:** Para una medición muestreada, utiliza el provider `profile` de DTrace a 997 Hz. Para una medición en cada llamada, captura `sbinuptime()` a la entrada y a la salida, réstalos, y clasifícalos en un histograma `counter(9)` por CPU. Lee el histograma a través de sysctl.

**Por qué este patrón:** DTrace `profile` prácticamente no tiene coste cuando está desactivado. `sbinuptime()` es rápido (decenas de ciclos). Un cubo de histograma por CPU es un incremento de `counter(9)`. El agregado se lee desde el espacio de usuario sin tocar la ruta caliente.

**Antipatrón:** `nanotime()` por cada llamada (es mucho más lento que `sbinuptime()`), o un `printf()` por llamada (ruinoso), o un atómico compartido por llamada (cuello de botella en la línea de caché).

### Patrón: necesitas exponer el estado del driver

**Situación:** Quieres que los operadores puedan consultar y configurar el driver en tiempo de ejecución.

**Técnica:** Construye un árbol `sysctl(9)` bajo `hw.<drivername>`. Utiliza `SYSCTL_U64` para contadores simples, `SYSCTL_INT` para enteros simples, y sysctls procedurales (`SYSCTL_PROC`) para valores calculados o datos en bloque. Marca los parámetros ajustables con `CTLFLAG_TUN` para que `loader.conf` pueda establecerlos.

**Ruta del primitivo:** `/usr/src/sys/sys/sysctl.h`, `/usr/src/sys/kern/kern_sysctl.c`.

**Por qué este patrón:** sysctl es la interfaz canónica de FreeBSD para la observabilidad y la configuración de drivers. Los operadores saben cómo usar `sysctl(8)`; los agregadores de logs pueden recorrer los árboles sysctl; los scripts de monitorización pueden hacer polling.

**Antipatrón:** ioctls personalizados para cada parámetro, o lectura de variables de entorno en tiempo de ejecución, o un `printf()` que espera que el usuario esté mirando el log.

### Patrón: necesitas dividir un manejador de interrupciones

**Situación:** El manejador de interrupciones hace más trabajo del que es seguro en contexto de interrupción; a veces bloquea o genera contención.

**Técnica:** Registra un manejador de filtro (que devuelve `FILTER_SCHEDULE_THREAD`) que haga el mínimo indispensable (reconocer el hardware, leer el estado). Registra un manejador ithread que se ejecute en contexto de thread y haga trabajo moderado. Para trabajo que puede esperar o que lleva tiempo real, planifícalo en un taskqueue.

**Ruta del primitivo:** `/usr/src/sys/sys/bus.h`, `/usr/src/sys/kern/kern_intr.c`, `/usr/src/sys/kern/subr_taskqueue.c`.

**Por qué este patrón:** El filtro se ejecuta en contexto de interrupción dura con las restricciones más estrictas; el ithread se ejecuta con una prioridad específica; el taskqueue se ejecuta con una prioridad configurable en un thread trabajador. Cada capa está adaptada a su trabajo.

**Antipatrón:** Hacer todo el trabajo en el filtro, o bloquear en locks dentro del ithread, o ejecutar trabajo pesado con prioridad de interrupción, lo que retrasa a otros drivers.

### Patrón: necesitas limitar la tasa de los mensajes de log

**Situación:** Una condición de error puede producir muchas entradas de log por segundo; quieres una por N segundos o por M eventos para evitar la inundación.

**Técnica:** Usa `ratecheck(9)`. Mantén un `struct timeval` y un intervalo; llama a `ratecheck(&last, &interval)`. La llamada devuelve 1 cuando ha pasado suficiente tiempo y actualiza el estado; un retorno de 0 significa *omitir este mensaje*.

**Ruta del primitivo:** `/usr/src/sys/sys/time.h` (declaración) y `/usr/src/sys/kern/kern_time.c` (implementación).

**Por qué este patrón:** `log(9)` es barato por llamada, pero no gratuito a miles por segundo. Un log sin límites llena el disco de logs y puede generar contención en el dispositivo de log. Un limitador de tasa da visibilidad sin inundación.

**Antipatrón:** Un `printf()` incondicional en los caminos de error, o un contador manual que se desvía y filtra.

### Patrón: necesitas una interfaz de operador estable

**Situación:** Quieres que los operadores puedan confiar en nombres sysctl o estructuras de contadores específicos que no cambien entre versiones.

**Técnica:** Documenta la interfaz estable en la página del manual. Versiona el driver con `MODULE_VERSION()` para que los operadores puedan detectar qué interfaz tienen. Cuando tengas que cambiar una interfaz, conserva la antigua (o un shim mínimo) durante al menos una versión y anuncia la deprecación en las notas de la versión.

**Ruta del primitivo:** Convenciones de página de manual en `/usr/src/share/man/man4/`.

**Por qué este patrón:** Los operadores escriben scripts contra nombres sysctl. Cambiar esos nombres rompe sus scripts. Una interfaz publicada y estable es un compromiso que puedes mantener.

**Antipatrón:** Renombrar un sysctl sin cuidado, o eliminar un contador del que dependen scripts, o remodelar la salida de un sysctl procedural sin versionado.

### Patrón: necesitas entregar el código tras el ajuste

**Situación:** Has terminado una ronda de ajuste y quieres dejar el driver en un estado limpio y listo para su distribución.

**Técnica:** Elimina el código de medición temporal que no estaba destinado a quedarse. Conserva los contadores de calidad de producción y las sondas SDT. Actualiza `MODULE_VERSION()`. Ejecuta un benchmark completo y registra los resultados. Actualiza la página del manual. Escribe un breve informe de rendimiento que explique qué se ajustó y por qué. Haz un commit con un mensaje que haga referencia al informe.

**Por qué este patrón:** El cambio de código es solo una parte del trabajo. El informe, los números y la documentación son lo que hace que el cambio sea duradero. Sin ellos, el próximo mantenedor no tiene forma de saber qué se hizo ni por qué.

**Antipatrón:** Hacer commit de cambios de ajuste sin benchmarks ni documentación. El próximo mantenedor no puede saber qué cambió, no puede reproducir la medición y puede deshacer el trabajo por accidente.

### Una tabla de referencia rápida de herramientas

A continuación se presenta un resumen en una página con las herramientas introducidas en este capítulo, su uso típico y su ruta de fuente o documentación.

| Herramienta | Uso | Dónde aprender más |
|------|-----|---------------------|
| `sysctl(8)` | Leer y escribir variables de estado del kernel | `sysctl(8)`, `/usr/src/sbin/sysctl/` |
| `top(1)` | Vista general de procesos y recursos del sistema | `top(1)` |
| `systat(1)` | Visualización interactiva de estadísticas del sistema | `systat(1)` |
| `vmstat(8)` | Estadísticas de memoria virtual, interrupciones y zonas | `vmstat(8)` |
| `ktrace(1)` | Trazar llamadas al sistema de un proceso | `ktrace(1)`, `kdump(1)` |
| `dtrace(1)` | Trazado dinámico del espacio de usuario y el kernel | `dtrace(1)`, `dtrace(7)`, `fbt(7)`, `sdt(7)` |
| `pmcstat(8)` | Lector de contadores de rendimiento del hardware | `pmcstat(8)`, `hwpmc(4)` |
| `flamegraph.pl` | Visualización de muestras de pila | externo, de Brendan Gregg |
| `gprof(1)` | Perfilar programas de usuario | `gprof(1)`; no se usa para el kernel |
| `netstat(1)` | Estadísticas de red | `netstat(1)` |

Un mantenedor de driver no necesita todas estas herramientas todos los días. Pero saber cuál responde a qué pregunta ahorra horas de investigación a ciegas.

### Una tabla de referencia rápida de primitivos

Los primitivos en el kernel utilizados en este capítulo, reunidos en un solo lugar.

| Primitivo | Propósito | Cabecera |
|-----------|---------|--------|
| `counter(9)` | Contadores calientes por CPU | `sys/counter.h` |
| `DPCPU_DEFINE_STATIC` | Definir variable por CPU | `sys/pcpu.h` |
| `DPCPU_GET` / `DPCPU_PTR` | Acceder a la ranura de la CPU actual | `sys/pcpu.h` |
| `DPCPU_SUM` | Sumar a través de todas las CPUs | `sys/pcpu.h` |
| `uma_zcreate` | Crear zona UMA | `vm/uma.h` |
| `uma_zalloc` / `uma_zfree` | Asignar desde / liberar hacia la zona | `vm/uma.h` |
| `UMA_ALIGN_CACHE` | Alinear elementos de zona a la línea de caché | `vm/uma.h` |
| `bus_alloc_resource_any` | Asignar recurso de bus (IRQ, memoria) | `sys/bus.h` |
| `bus_setup_intr` | Registrar manejador de interrupciones | `sys/bus.h` |
| `bus_dma_tag_create` | Crear etiqueta DMA | `sys/bus_dma.h` |
| `taskqueue_enqueue` | Encolar trabajo diferido | `sys/taskqueue.h` |
| `callout(9)` | Temporizador de un solo disparo o periódico | `sys/callout.h` |
| `SYSCTL_U64` / `SYSCTL_INT` | Declarar sysctl | `sys/sysctl.h` |
| `SYSCTL_PROC` | Declarar sysctl procedural | `sys/sysctl.h` |
| `log(9)` | Registrar un mensaje | `sys/syslog.h` |
| `ratecheck(9)` | Limitar la tasa de un mensaje | `sys/time.h` |
| `SDT_PROBE_DEFINE` | Declarar sonda DTrace estática | `sys/sdt.h` |
| `CACHE_LINE_SIZE` | Tamaño de línea de caché por arquitectura | `sys/param.h` (vía MD `<arch>/include/param.h`) |
| `sbinuptime()` | Tiempo monótono rápido | `sys/time.h` |
| `getnanotime()` | Tiempo de pared aproximado rápido | `sys/time.h` |
| `nanotime()` | Tiempo de pared preciso | `sys/time.h` |

El hábito de saber dónde encontrar estos primitivos en un árbol de código fuente recién instalado vale tanto como saber cuándo usarlos. Abre la cabecera, lee las definiciones de macros y consulta los puntos de uso con `grep -r SYSCTL_PROC /usr/src/sys/dev/ | head`.

### Cerrando la referencia de patrones

Los patrones anteriores no son mandamientos; son valores predeterminados bien respaldados. Un driver específico puede tener razones para desviarse de cualquiera de ellos. Lo que importa es que la desviación sea consciente, medida y documentada. *No usamos UMA aquí porque...* es correcto; *no usamos UMA aquí, supongo* no lo es.

Cuando trabajas en un driver desconocido, puedes usar esta tabla como lista de verificación: ¿el driver cuenta sus eventos calientes de forma barata, asigna desde una zona adecuada, evita el false sharing en variables compartidas, divide su trabajo de interrupción de forma apropiada, expone su estado a través de sysctl, limita la tasa de sus logs y se distribuye con una interfaz estable versionada? Cada respuesta negativa es un candidato de mejora si el rendimiento está bajo investigación.

## Solución de problemas y errores comunes

El trabajo de rendimiento tiene su propia clase de problemas, distintos de los bugs funcionales. Un driver que mide mal está tan roto como uno que falla, pero los síntomas son más sutiles. Esta sección cataloga los fallos más comunes, sus síntomas y sus soluciones.

### DTrace informa de cero muestras

**Síntoma:** Un script DTrace compila, se ejecuta y al pulsar Ctrl-C informa de una agregación vacía o un recuento de cero.

**Causas más frecuentes:**

- El módulo del driver no está cargado. Las sondas `fbt:mymodule:` no existen hasta que el módulo está en el kernel.
- El nombre de la función es incorrecto. Compruébalo con `dtrace -l | grep myfunction`; DTrace pone en minúsculas los nombres de las sondas en el listado, pero el nombre de la función dentro del kernel es el identificador C.
- La función fue inlineada por el compilador. Una sonda `fbt` solo existe para una función que el compilador emite como símbolo invocable. Las funciones static pueden ser inlineadas y desaparecer; compruébalo con `objdump -t module.ko | grep myfunction`.
- La sonda está en un camino que no fue ejercitado. Ejecuta la carga de trabajo que debería activar ese camino, no cualquier carga de trabajo.
- El predicado filtra todo. Elimina el predicado y confirma que la sonda se activa; luego ajusta el predicado.

**Secuencia de depuración:** Comprueba `kldstat` (¿está cargado el módulo?), `dtrace -l -n probe_name` (¿existe la sonda?), `dtrace -n 'probe_name { printf("fired"); }'` (¿se activa siquiera?), y luego añade tu lógica real.

### DTrace descarta eventos

**Síntoma:** DTrace imprime *dropped N events* o los recuentos de la agregación son sospechosamente bajos para un evento de tasa conocidamente alta.

**Causas más frecuentes:**

- El buffer es demasiado pequeño. Auméntalo: `dtrace -b 64m ...` para un buffer de 64 MB.
- La agregación genera demasiada actividad. Cada `printf()` dentro de una sonda es una posible pérdida; utiliza agregaciones para eventos de alta tasa, no impresiones por evento.
- La frecuencia de intercambio es demasiado alta. DTrace intercambia buffers entre el kernel y el espacio de usuario; a tasas muy altas esto puede causar pérdidas. Ajústalo con `dtrace -x bufpolicy=fill` para reducir las pérdidas a costa de perder el final del registro.

### pmcstat informa de *No Device* o *No PMCs*

**Síntoma:** `pmcstat` imprime errores como `pmc_init: no device`, `ENXIO` o similares.

**Causas más frecuentes:**

- `hwpmc` no está cargado. Ejecuta `kldload hwpmc`.
- La CPU subyacente no expone PMCs. Muchas VMs y algunas instancias en la nube tienen los PMCs desactivados. Recurre al provider `profile` de DTrace.
- El nombre del evento no es compatible con esta CPU. Ejecuta `pmcstat -L` para ver los eventos disponibles.

### El muestreo de pmcstat no detecta funciones calientes

**Síntoma:** Una función que *sabes* que está caliente no aparece en la salida de `pmcstat -T`.

**Causas más frecuentes:**

- La función es inline y no tiene símbolo invocable. El muestreador atribuye las muestras a la función externa.
- La tasa de muestreo es demasiado baja. Prueba con `-S cycles -e 100000` para obtener una muestra por cada 100k ciclos.
- La función es demasiado rápida para que una sola muestra la capture de forma fiable. Acumula muestras a lo largo de una carga de trabajo prolongada.
- La función está en un módulo cuyos símbolos han sido eliminados. Reconstrúyelo con símbolos de depuración.

### Los valores del contador parecen imposibles

**Síntoma:** Un contador muestra un valor negativo, se desborda de forma inesperada o informa de cifras incompatibles con la carga de trabajo.

**Causas más frecuentes:**

- Un contador fue actualizado desde múltiples CPUs sin operaciones atómicas. Utiliza `atomic_add_64()` o `counter(9)`.
- Se leyó un contador mientras se estaba actualizando. Con `counter(9)` esto no supone un problema porque la lectura suma los valores por CPU; con un atómico simple, una sola lectura siempre es consistente, pero una combinación aritmética de dos lecturas puede no serlo. Captura los contadores en un orden coherente.
- El contador se desbordó. Un contador de 32 bits a un millón de eventos por segundo se desborda en aproximadamente una hora. Usa `uint64_t`.
- El contador no fue inicializado. `counter_u64_alloc()` devuelve un contador inicializado a cero, pero una variable simple solo se pone a cero al cargar el módulo. Una variable global estática está bien; una local no.

### El handler de sysctl falla con pánico

**Síntoma:** Leer o escribir un sysctl provoca un pánico del kernel.

**Causas más frecuentes:**

- El sysctl fue registrado apuntando a memoria que ya se liberó (por ejemplo, un campo de softc tras el detach).
- El handler desreferencia un puntero NULL. Comprueba `req->newptr` antes de asumir que hay un dato a escribir.
- El handler se ejecuta sin los locks apropiados. Usa `CTLFLAG_MPSAFE` y gestiona los locks dentro del handler, o bien omite `CTLFLAG_MPSAFE` y acepta el giant lock.
- El handler llama a una función que no puede invocarse desde el contexto de un sysctl (por ejemplo, una que bloquea el thread mientras mantiene un spinlock).

### El driver se ralentiza bajo medición

**Síntoma:** El throughput del driver cae cuando DTrace o `pmcstat` están en ejecución.

**Causas más frecuentes:**

- Las sondas están en una ruta de acceso muy frecuente (hot path). Muévelas a rutas menos activas o reduce el número de sondas.
- El script de DTrace genera demasiada salida. Usa agregaciones en lugar de impresiones por evento.
- La tasa de muestreo del PMC es demasiado alta. Redúcela.
- El predicado de la sonda es costoso. Los predicados simples (`arg0 == 0`) son baratos; los complejos con comparaciones de cadenas no lo son.

### Inanición del thread del taskqueue

**Síntoma:** El trabajo pendiente en el taskqueue se acumula sin procesarse y la respuesta visible para el usuario empeora.

**Causas más frecuentes:**

- El thread del taskqueue estaba asignado a una CPU que ahora está ocupada con otra tarea. Usa `taskqueue_start_threads_cpuset()` para una ubicación predecible o deja el thread sin anclar.
- El taskqueue es compartido (`taskqueue_fast`) y otros subsistemas lo mantienen ocupado. Usa un taskqueue dedicado.
- Una tarea anterior está bloqueada esperando algo que nunca termina. Cancela la tarea y añade un tiempo de espera máximo.
- El thread fue destruido por un MOD_UNLOAD pero las tareas no fueron vaciadas. Usa `taskqueue_drain(9)` antes de liberar recursos.

### Tormenta de interrupciones detectada

**Síntoma:** `dmesg` muestra *interrupt storm detected* y la interrupción queda limitada.

**Causas más frecuentes:**

- El driver no reconoce la interrupción en el handler, por lo que el hardware la sigue activando.
- El driver reconoce el registro equivocado.
- El hardware genera interrupciones a una tasa que el driver no puede atender. Activa la coalescencia si está disponible.
- Un driver que comparte la interrupción se comporta de forma incorrecta, y su filter handler reclama la interrupción falsamente devolviendo `FILTER_HANDLED` cuando debería devolver `FILTER_STRAY`.

### La alineación de línea de caché no tiene efecto

**Síntoma:** Añadiste `__aligned(CACHE_LINE_SIZE)` a un contador y no observaste cambio alguno en el throughput.

**Causas más frecuentes:**

- El contador no tiene contención real. Las cargas de trabajo de un solo thread o de baja concurrencia no se benefician de la alineación.
- El compilador ya alineó el contador con su disposición predeterminada. Confírmalo con `offsetof()`.
- El campo vecino ya no es de acceso frecuente. El campo caliente cambió, la alineación antigua sigue ahí y no existe contención.
- La CPU no sufre suficiente rebote de líneas de caché como para que la diferencia sea medible. Algunas arquitecturas tienen un protocolo de coherencia de caché más barato que otras.

La alineación no es gratuita (añade relleno a la estructura y desperdicia caché); si no tiene efecto, elimínala. La medición es quien decide.

### Fugas de zonas UMA

**Síntoma:** `vmstat -z` muestra una zona UMA que crece sin límite y el sistema acaba agotando la memoria del kernel.

**Causas más frecuentes:**

- El driver asigna memoria con `uma_zalloc()` pero no la libera con `uma_zfree()` en la ruta correspondiente. Asegúrate de que cada asignación tenga su liberación.
- Se sobrescribió un puntero antes de liberarlo. Inspecciónalo con KASAN o MEMGUARD (tema del Capítulo 34).
- La ruta de limpieza de una operación fallida es incorrecta. Cada retorno anticipado de una función que asigna memoria debe liberar lo que se asignó.

### El benchmark produce resultados ruidosos

**Síntoma:** Ejecuciones repetidas del mismo benchmark producen valores significativamente distintos.

**Causas más frecuentes:**

- La actividad del sistema interfiere. Ejecuta los benchmarks en una máquina sin otra carga.
- El escalado de frecuencia de la CPU cambia la velocidad del reloj entre ejecuciones. Fija la frecuencia con `sysctl dev.cpufreq.0.freq_levels` (comprueba las opciones disponibles) o activa el modo de alto rendimiento.
- El calentamiento de la caché varía entre ejecuciones. Descarta los resultados de la primera ejecución o añade una pasada de calentamiento.
- Las estructuras de datos diferidas del kernel (cachés UMA, cachés de buffer) difieren entre ejecuciones. Vacía las cachés entre ejecuciones o acepta la varianza.
- Los pares de Hyper-Threading compiten entre sí. Desactiva SMT para obtener resultados limpios.

Un benchmark con alta varianza no es necesariamente incorrecto, pero sus conclusiones deben mantenerse a pesar de ella. Si el ajuste mejora la mediana un 5 % pero la varianza entre ejecuciones es del 20 %, no puedes concluir nada a partir de una sola ejecución; ejecuta muchas y analiza la distribución.

### Mediste lo incorrecto

**Síntoma:** Ajustaste el driver para cumplir un objetivo, pero el rendimiento visible para el usuario no cambió.

**Causas más frecuentes:**

- La métrica que optimizaste no es la que importa. Ajustar la latencia mediana cuando el problema visible para el usuario es la P99 es un error clásico.
- La carga de trabajo que usaste para el benchmark no es la que ejecuta el usuario. Optimizar para lecturas secuenciales cuando el usuario hace lecturas aleatorias es esfuerzo desperdiciado.
- El driver no era el cuello de botella. A veces el cuello de botella está en el userland, en el planificador o en otro subsistema. `pmcstat -s cycles` sobre todo el sistema te indica dónde va realmente el tiempo.

Si el ajuste no ayudó, no es un fracaso; es información. La métrica o la carga de trabajo eran incorrectas, y ahora lo sabes. Reformula el objetivo y vuelve a medir.

### Los números empeoran tras un cambio de ajuste

**Síntoma:** Aplicaste un cambio que parecía beneficioso (añadiste una caché por CPU, acortaste una sección crítica, moviste trabajo a un taskqueue) y los números medidos empeoran.

**Causas más frecuentes:**

- El cambio introdujo una regresión que no es evidente en el código. Por ejemplo, mover trabajo a un taskqueue añade un cambio de contexto en cada petición, y a tu tasa de operaciones ese coste domina.
- El cambio desplazó el cuello de botella. Eliminaste la contención del lock y ahora el asignador es el nuevo cuello de botella.
- El cambio rompió una propiedad de corrección que antes era invisible. Una condición de carrera más leve genera ahora trabajo adicional (reintentos, rutas de error) y el driver parece más lento porque hace más trabajo.
- La medición tiene ruido y la ejecución que parecía peor está dentro de la banda de varianza. Ejecútalo diez veces y compruébalo.

**Secuencia de depuración:** Revierte el cambio y confirma que la línea base se reproduce. Aplica el cambio y vuelve a medir. Si ambos se reproducen, el cambio es la causa; si la línea base varía, la medición es inestable. Revisa el diff en busca de cambios no relacionados que se hayan colado. Perfila las dos variantes y compara: el perfil te mostrará adónde va realmente el tiempo.

### El throughput sube pero la latencia P99 empeora

**Síntoma:** Un cambio mejoró el throughput medio pero empeoró la latencia de cola. Los usuarios se quejan aunque la métrica principal de tu benchmark mejorara.

**Causas más frecuentes:**

- El cambio intercambió latencia de cola por throughput. Por ejemplo, un agrupamiento mayor mejora el throughput pero aumenta el tiempo que una petición con mala suerte espera a que su lote se complete.
- La profundidad de la cola creció. Las colas más largas dan más oportunidades de acumulación, lo que aumenta la P99 bajo carga.
- Una ruta más lenta se recorrió con mayor frecuencia. Por ejemplo, la ruta de acceso frecuente se volvió más rápida, pero la ruta de error sigue pagando el coste antiguo y el benchmark ahora la ejerce más.

**Secuencia de depuración:** Representa el throughput frente a la carga ofrecida; representa la latencia P99 frente a la carga ofrecida. El punto de inflexión de la curva de latencia es donde el driver entra en sobrecarga. Si ese punto se desplazó hacia la izquierda (una carga menor produce alta latencia), el cambio empeoró la latencia de cola aunque el throughput máximo mejorara. Ajusta de nuevo hacia el objetivo de latencia.

### Los resultados difieren entre configuraciones de hardware

**Síntoma:** Un cambio que mejora el rendimiento en una máquina no tiene efecto o empeora en otra.

**Causas más frecuentes:**

- La microarquitectura de la CPU difiere. Los protocolos de coherencia de caché, los predictores de ramas y los subsistemas de memoria varían entre generaciones; las optimizaciones ajustadas para una generación pueden ser irrelevantes o perjudiciales para otra.
- El número de núcleos difiere. Las cachés por CPU escalan con el número de CPUs; en un sistema con pocas CPUs, la sobrecarga puede superar el beneficio.
- La topología NUMA difiere. Una optimización que ignora NUMA puede funcionar bien en un sistema de un solo zócalo y ser perjudicial en uno de múltiples zócalos.
- El subsistema de memoria difiere. La velocidad de la DRAM, los canales de memoria y los tamaños de caché varían ampliamente.

**Secuencia de depuración:** Registra la configuración completa del hardware con cada medición (modelo de CPU, número de núcleos, configuración de memoria). Mide en al menos dos máquinas distintas. Acepta que algunas optimizaciones son específicas de cada máquina; documéntalas si es así.

### KASSERT solo se activa bajo carga

**Síntoma:** Un `KASSERT()` que ha permanecido silencioso durante las pruebas funcionales se activa cuando el driver está bajo carga intensa.

**Causas más frecuentes:**

- Una condición de carrera que es rara con baja concurrencia se vuelve frecuente con alta concurrencia. La aserción detecta un estado que solo ocurre cuando dos CPUs alcanzan la sección crítica simultáneamente.
- Una violación de orden que depende del momento exacto de ejecución. Las rutas más lentas la ocultaban; las más rápidas la exponen.
- Un agotamiento de recursos que solo aparece a alta tasa. Por ejemplo, `M_NOWAIT` devuelve NULL porque el asignador se queda sin memoria momentáneamente.
- Un efecto visible para el driver de la evaluación diferida del kernel (reequilibrio de cachés UMA, procesamiento del backlog de callouts).

**Secuencia de depuración:** Captura el stack trace del pánico y los valores de las variables locales. Añade `mtx_assert(&sc->mtx, MA_OWNED)` en puntos clave para verificar la propiedad del lock. Añade sondas SDT temporales alrededor de la región sospechosa y traza el orden de los eventos con DTrace. A veces la solución es un lock más estricto; a veces es una barrera de memoria; a veces es un rediseño.

### La medición no concuerda con la percepción del usuario

**Síntoma:** Tu benchmark dice que el driver es rápido; los usuarios dicen que el driver se siente lento.

**Causas más frecuentes:**

- El benchmark mide un agregado; los usuarios perciben eventos individuales en el peor caso. Un driver con 10 µs de latencia mediana y 1 s de P99.99 se siente terrible aunque la puntuación del benchmark sea excelente.
- El benchmark usa carga sintética; la carga de trabajo real tiene dependencias (patrones de acceso a archivos, cadenas de syscalls) que el benchmark no reproduce.
- Los usuarios perciben la latencia de extremo a extremo, no la latencia del driver. Si el driver representa el 5 % del tiempo total, incluso una mejora significativa no produce cambio visible para el usuario.
- La queja del usuario es correcta y el benchmark está midiendo lo incorrecto.

**Secuencia de depuración:** Pregunta a los usuarios exactamente qué hacen. Reprodúcelo. Mide la experiencia real con un cronómetro o un perfilador en espacio de usuario si es necesario. Añade histogramas por operación y examina los percentiles de cola. Una P99.99 no es un artefacto estadístico; es el mal día de alguien.

### El script de DTrace se cuelga o tarda mucho en finalizar

**Síntoma:** Tras pulsar Ctrl-C, DTrace tarda segundos o minutos en terminar de imprimir las agregaciones; a veces parece haberse colgado.

**Causas más frecuentes:**

- La agregación es muy grande (millones de claves). La impresión lleva un tiempo proporcional al tamaño de la agregación.
- La agregación contiene cadenas de texto largas. La asignación de cadenas en DTrace no es gratuita.
- El buffer del kernel está lleno de eventos en cola esperando ser entregados al espacio de usuario. DTrace vacía el buffer antes de salir.
- Una sonda sigue disparándose rápidamente y DTrace intenta mantenerse al día.

**Secuencia de depuración:** Limita la agregación con `trunc(@, N)` o `printa()` con una lista acotada. Usa `dtrace -x aggsize=M` para acotar el tamaño de la agregación. Usa `dtrace -x switchrate=1hz` para reducir la sobrecarga de conmutación durante la ejecución. Si necesitas detener un DTrace fuera de control, `kill -9` es seguro; DTrace libera las sondas sin dejar rastros.

### Sysctl informa valores distintos en lecturas sucesivas

**Síntoma:** Leer el mismo sysctl dos veces en rápida sucesión devuelve valores distintos, incluso cuando no hay ninguna carga de trabajo en ejecución.

**Causas más habituales:**

- El contador sigue actualizándose por operaciones en curso. Esto es lo esperado y normalmente no es un problema.
- Un contador por CPU se está capturando de forma no atómica; distintas CPUs se leen en instantes diferentes.
- El handler usa una estructura de datos no estable (una lista enlazada en proceso de recorrido) sin mantener el lock correspondiente.
- Un bug en el handler lee más allá del buffer asignado y devuelve datos obsoletos de la pila.

**Secuencia de depuración:** Detén la carga de trabajo por completo. Lee el sysctl diez veces. Si los valores siguen variando, el handler tiene un bug (lo más probable es que falte un lock o una barrera). Si los valores son estables en reposo, la variación procede de la actividad en segundo plano; documenta el contador como *aproximado* si procede.

### Un driver que va rápido solo, va lento en producción

**Síntoma:** Los benchmarks en aislamiento muestran que el driver cumple sus objetivos; el rendimiento en producción es mucho peor.

**Causas más habituales:**

- Los recursos compartidos en producción están en contención. El buffer cache del sistema de archivos, el planificador, la pila de red u otros drivers compiten por las mismas CPUs y por la misma memoria.
- El perfil de carga de trabajo en producción difiere del benchmark. El benchmark usa un patrón uniforme; la producción usa una mezcla que ejercita rutas de código que el benchmark nunca toca.
- La presión de memoria en producción invalida los caches. En los benchmarks, tu zona UMA se mantiene caliente; en producción, la presión de memoria la vacía.
- Las características de seguridad (IBRS, retpolines, aislamiento de tabla de páginas del kernel) tienen costes distintos en producción que en el entorno del benchmark.

**Secuencia de depuración:** Instrumenta el driver para que sea observable en producción. Lanza DTrace o un profiler ligero en producción durante un momento y compara las distribuciones con las del benchmark. Si la diferencia es reproducible, determina qué subsistema difiere; la respuesta suele ser la presión de memoria o las diferencias en el planificador, no el driver en sí.

### Cerrando la resolución de problemas

Cada elemento de esta sección es un modo de fallo real que ingenieros de rendimiento experimentados han encontrado. El valor está en leer la lista una vez, reconocer los patrones cuando aparecen y dedicar el tiempo de medición a diagnosticar el fallo concreto en lugar de adivinar. *Por qué va lento mi driver* tiene miles de respuestas posibles; los patrones de resolución de problemas reducen el espacio de búsqueda en unos pocos pasos concretos.

Un hábito que merece la pena cultivar: cuando encuentres un modo de fallo nuevo que no esté en esta lista, anótalo. Registra el síntoma, la causa y la secuencia de depuración. Con el paso de los años, tu propia lista se vuelve más útil que la de cualquier libro, porque refleja los drivers y el hardware con los que trabajas realmente.

## En resumen

El capítulo 33 nos ha llevado de *¿funciona el driver?* a *¿funciona bien?*. Las dos preguntas suenan parecidas y difieren en la forma de la respuesta. La primera es un sí o no de corrección; la segunda es una distribución numérica de throughput, latencia, capacidad de respuesta y coste de CPU, medida bajo una carga de trabajo concreta en un entorno concreto.

Comenzamos en la sección 1 con los cuatro ejes del rendimiento de un driver y la disciplina de enunciar objetivos medibles. Un objetivo con una métrica, un umbral, una carga de trabajo y un entorno es lo que separa un proyecto de rendimiento de una mera esperanza de rendimiento. El recordatorio de que la optimización debe preservar la corrección, la depurabilidad y la mantenibilidad es la restricción que mantiene honesto el trabajo de rendimiento.

La sección 2 introdujo las primitivas de medición del kernel: las funciones de tiempo `get*` y las no `get`, los tres niveles de contador (atómico simple, `counter(9)`, DPCPU), la maquinaria de sysctl para exponer el estado y las herramientas de FreeBSD (`sysctl`, `top`, `systat`, `vmstat`, `ktrace`, `dtrace`, `pmcstat`) que lo leen. La idea central: la medición tiene un coste, y ese coste no debe contaminar la propia medición.

La sección 3 introdujo DTrace. Sus sondas omnipresentes, su coste casi nulo cuando están desactivadas, su agregación en el lado del kernel y su expresivo lenguaje de scripting lo convierten en la herramienta preferida para la mayor parte de la observación de drivers. El proveedor `fbt` cubre cada función; el proveedor `sdt` te da sondas estables con nombre en tu propio driver; el proveedor `profile` muestrea a frecuencia fija; el proveedor `lockstat` se especializa en la contención de locks.

La sección 4 introdujo los contadores de rendimiento de hardware. `hwpmc(4)` expone los contadores integrados de la CPU; `pmcstat(8)` los controla desde el espacio de usuario. El modo de conteo da totales; el modo de muestreo da distribuciones. El recolector de callgraph más el renderizador de flame graph convierte las muestras en una visualización que hace obvios los hotspots. Los datos son en bruto y la interpretación requiere práctica, pero la combinación responde preguntas que ninguna otra herramienta puede responder.

La sección 5 se centró en la memoria. La alineación de línea de caché evita el false sharing; `bus_dma(9)` gestiona la alineación DMA; la preasignación mantiene el hot path fuera del asignador; las zonas UMA con `UMA_ALIGN_CACHE` se especializan para asignaciones frecuentes de tamaño fijo; los contadores por CPU con `counter(9)` y DPCPU eliminan la contención de línea de caché en las métricas compartidas. Cada técnica tiene un coste, y cada una merece evidencia antes de aplicarla.

La sección 6 se centró en las interrupciones y las taskqueues. Medir la tasa de interrupciones y la latencia del handler es fácil con `vmstat -i` y DTrace. La división filter más ithread mantiene pequeño el contexto de interrupción; las taskqueues mueven el trabajo a un entorno más tranquilo; los threads dedicados ofrecen aislamiento para casos especiales; la coalescencia y el debouncing controlan la propia tasa de interrupciones. El criterio para saber cuándo aplicar cada técnica procede de la medición, no de la lectura.

La sección 7 miró hacia adentro, hacia las métricas en tiempo de ejecución que pertenecen al driver entregado. Un árbol de sysctl bien pensado expone totales, tasas, estados y configuración. Los sysctls procedimentales obtienen sumas de `counter(9)`. El registro con limitación de tasa mediante `log(9)` y `ratecheck(9)` evita que los mensajes informativos inunden el log. Los modos de depuración dan a los operadores una forma segura de aumentar la verbosidad sin recompilar. Cada elemento es un pequeño compromiso; el resultado acumulado es un driver auditable, diagnosticable y mantenible.

La sección 8 cerró el ciclo. El ajuste produce andamiaje; la entrega requiere limpieza. Elimina el código de medición temporal. Conserva los contadores, las sondas SDT y los controles de configuración que tienen valor duradero. Haz un benchmark del driver final, documenta los controles de ajuste en la página del manual, actualiza `MODULE_VERSION()` y escribe un informe de rendimiento en texto plano. Estos pasos son la diferencia entre una mejora puntual y una duradera.

Los laboratorios recorrieron seis etapas concretas: contadores de referencia, scripts de DTrace, muestreo con pmcstat, ajuste con alineación de caché y por CPU, la división interrupción contra taskqueue y la finalización optimizada en v2.3. Cada laboratorio produjo una salida medible; los números son en definitiva de lo que trata el capítulo.

Los ejercicios de desafío extendieron el trabajo en direcciones demasiado específicas para el texto principal: presupuestos de latencia para drivers reales, comparaciones de asignadores, scripts de DTrace propios y útiles, un estudio de presupuesto de observabilidad, un histograma en vivo, ajuste específico para la carga de trabajo, escritura de páginas del manual y perfilado de un driver desconocido.

La sección de resolución de problemas catalogó los modos de fallo habituales y sus diagnósticos. Cada uno es un patrón reconocible; dedicar el tiempo de medición a identificar en qué patrón te encuentras es la mitad del trabajo que la mayoría de los principiantes omite.

### Lo que deberías ser capaz de hacer ahora

Si has trabajado los laboratorios y leído las explicaciones con atención, ahora puedes:

- Establecer objetivos de rendimiento medibles para un driver antes de comenzar a ajustar.
- Elegir la función de tiempo del kernel adecuada para una medición en función de la precisión que necesitas.
- Contar eventos de forma económica en el hot path con `counter(9)` o `atomic_add_64()`.
- Exponer métricas del driver al espacio de usuario mediante un árbol de sysctl.
- Escribir one-liners y scripts de DTrace que respondan preguntas concretas sobre un driver.
- Medir la contención de locks con el proveedor `lockstat`.
- Muestrear el tiempo de CPU del kernel con `pmcstat` o con el proveedor `profile` de DTrace.
- Leer un flame graph.
- Aplicar alineación de línea de caché, preasignación, zonas UMA y contadores por CPU de forma selectiva, con evidencia.
- Dividir el trabajo de interrupción de un driver entre filter, ithread y taskqueue en función de la latencia medida.
- Entregar un driver ajustado: eliminar el andamiaje, documentar los parámetros ajustables, actualizar la versión y escribir un informe.

### Lo que este capítulo deja implícito

Hay dos temas que el capítulo ha rozado pero no ha profundizado. Cada uno tiene su propio capítulo o su propia bibliografía, y tratar de cubrirlos completamente aquí habría entorpecido el hilo principal.

En primer lugar, **los internos profundos de `hwpmc(4)`**. El capítulo te mostró cómo usarlo; el árbol de código fuente bajo `/usr/src/sys/dev/hwpmc/` te muestra cómo funciona, en los backends de Intel, AMD, ARM, RISC-V y PowerPC. Si quieres entender cómo el kernel muestrea el registro PC en una interrupción de desbordamiento, o cómo el `pmcstat` del espacio de usuario habla con el kernel, leer ese árbol merece la pena. La mayoría de los lectores nunca lo necesitan.

En segundo lugar, **DTrace avanzado**. Los scripts que escribimos son sencillos. Las especulaciones de DTrace, sus interfaces USDT, sus agregaciones complejas y sus predicados avanzados merecen un libro propio. *DTrace: Dynamic Tracing in Oracle Solaris, Mac OS X, and FreeBSD*, de Brendan Gregg, es la referencia canónica. Para el trabajo con drivers, los patrones que hemos cubierto te llevan muy lejos.

### Conclusiones clave

Destiladas en una página, las lecciones fundamentales del capítulo son:

1. **Mide antes de cambiar.** Una optimización sin un número de antes y después es una suposición.
2. **Enuncia el objetivo en números.** Métrica, umbral, carga de trabajo, entorno. Sin estos elementos, no puedes saber cuándo has terminado.
3. **Prefiere la herramienta más sencilla que responda la pregunta.** Un one-liner de DTrace suele superar a una sesión completa de `pmcstat`.
4. **Las herramientas que miden también perturban.** Conoce el coste de cada sonda antes de usarla en un hot path.
5. **Cuatro ejes, no uno.** Throughput, latencia, capacidad de respuesta, coste de CPU. Optimizar uno puede perjudicar a otro.
6. **`counter(9)` para contadores en hot paths.** Actualización por CPU, suma en la lectura. Escala de una CPU a muchas.
7. **`UMA_ALIGN_CACHE` para asignaciones frecuentes.** Caches por CPU alineadas con la caché, asignación y liberación baratas.
8. **Preasigna, no asignes en el hot path.** El coste en el momento del attach es aceptable; el coste por llamada, no.
9. **Mantén el handler de interrupción pequeño.** Filter más ithread más taskqueue es la división estándar; úsala.
10. **Entrega con un árbol de sysctl.** Un driver sin observabilidad es un driver que nadie puede diagnosticar.
11. **Limita la tasa de tus logs.** Un driver que registra cada error puede hacerse un DoS a sí mismo.
12. **Escribe el informe.** El trabajo de ajuste no está terminado hasta que los números están escritos donde el siguiente mantenedor pueda encontrarlos.

### Antes de continuar

Antes de dar este capítulo por terminado y pasar al capítulo 34, tómate un momento para verificar lo siguiente. Estas son las comprobaciones que separan a quien ha visto el material de quien lo ha asimilado de verdad.

- Puedes escribir, de memoria, el esqueleto de un sysctl procedimental que devuelva una suma de `counter(9)`.
- Puedes escribir un one-liner de DTrace que represente en histograma la latencia de una función.
- Puedes explicar, en un párrafo, la diferencia entre `getnanotime()` y `nanotime()`.
- Puedes nombrar tres mediciones que capturarías antes de cambiar el hot path de un driver, y al menos una herramienta para cada una.
- Dado un driver que parece estar limitado por la CPU, tienes un plan: `vmstat -i` para comprobar si hay tormentas de interrupciones, `dtrace -n 'profile-997 { @[stack()] = count(); }'` para encontrar la función caliente, `pmcstat -S cycles -T` para confirmarlo y `lockstat` si el perfil apunta a locks.
- Dado un driver cuyo throughput no escala con el número de CPUs, tienes un plan: comprobar si hay un único contador atómico en contención, comprobar si hay una taskqueue compartida, comprobar si hay un lock global.
- Sabes en qué secciones de tu página del manual se nombrarían los parámetros ajustables y tienes una idea aproximada de cómo escribirlos.

Si alguno de estos conceptos todavía te resulta inseguro, vuelve a la sección correspondiente y vuélvela a leer. El material de este capítulo es acumulativo; el Capítulo 34 dará por sentado que has asimilado la mayor parte de lo que se ha visto aquí.

### Una nota sobre la práctica sostenida

El trabajo de rendimiento, más que casi cualquier otra área de la ingeniería del kernel, recompensa la práctica constante. Las primeras sesiones con `pmcstat` resultan confusas; la décima es natural. El primer script de DTrace que escribes es laborioso; el centésimo es un comando de una sola línea que tecleas sin pensar. El primer informe de rendimiento parece un misterio; el décimo parece un documento de decisión.

Si tienes trabajo con drivers en tu empleo diario o en un proyecto personal, dedica diez minutos de tu semana a una medición de rendimiento. No un ajuste, solo una medición. Anota los números. Compáralos con los de la semana anterior. A lo largo de un año esto se acumula y da lugar a los hábitos que este capítulo trata de enseñar. Las herramientas están todas ahí; el hábito es lo que lleva tiempo.

### Conexión con el resto del libro

El capítulo 33 se apoya en material de partes anteriores. Un breve repaso de lo que ahora ves con una nueva perspectiva:

De la Parte 1 y la Parte 2, el ciclo de vida del módulo y el registro con `DRIVER_MODULE()`. El driver que ajustaste en este capítulo tiene la misma forma que el driver que construiste en esas partes; ajustar el rendimiento no cambia el esqueleto.

De la Parte 3, el framework Newbus. `simplebus`, las APIs de recursos del bus, `bus_alloc_resource_any()`: estos son los bloques de construcción que tu driver usa para encontrar su hardware. Conocerlos te permite razonar sobre dónde va el tiempo durante attach y detach.

De la Parte 4, las interfaces del driver hacia el espacio de usuario. Los caminos de `read()` y `write()` son los que ajustaste en este capítulo. El árbol sysctl que construiste en la Sección 7 es una extensión natural del material sobre ioctl y sysctl del capítulo 15.

De la Parte 5 y la Parte 6, los capítulos de pruebas y depuración. Esas herramientas se aplican a los drivers ajustados en su totalidad. Un driver que funciona bien en rendimiento pero falla bajo carga no es un driver ajustado; es uno defectuoso.

De la Parte 7, el capítulo 32 sobre Device Tree y desarrollo embebido es la mitad de la plataforma. El rendimiento en una placa embebida tiene la misma forma que el rendimiento en un servidor, pero las restricciones son más estrictas: menos RAM, menos CPU, menos energía. Las técnicas escalan hacia abajo en prioridad (la alineación de líneas de caché importa menos cuando tienes un solo núcleo), pero la disciplina de medición importa tanto o más.

La imagen tras el capítulo 33 es que tienes el kit completo de medición y ajuste para drivers de FreeBSD. Puedes construir drivers. Puedes probarlos. Puedes portarlos. Puedes medirlos. Puedes ajustarlos. Puedes publicarlos. Los capítulos restantes de la Parte 7 refinan y consolidan esas habilidades, y el siguiente trata sobre las técnicas de depuración que el trabajo de rendimiento a veces revela que aún son necesarias.

### Lo que viene a continuación

El capítulo 34 pasa del rendimiento a la **depuración avanzada**. Cuando el ajuste revela un error más profundo, cuando un driver entra en pánico bajo una carga específica, cuando la corrupción de memoria aparece en un volcado de memoria, las herramientas del capítulo 34 son las que usas. `KASSERT(9)` para aserciones en el código, `panic(9)` para estados irrecuperables, `ddb(4)` para depuración interactiva del kernel en vivo, `kgdb` para depuración remota y post-mortem, `ktr(9)` para rastreo ligero con buffer en anillo, y `memguard(9)` para la detección de corrupción de memoria. La frontera entre el trabajo de rendimiento y la depuración es a menudo difusa: un problema de rendimiento resulta ser un error de corrección disfrazado, o un error de corrección se esconde tras un síntoma de rendimiento. El capítulo 34 te proporciona las herramientas para la otra mitad del diagnóstico.

Más allá del capítulo 34, los capítulos finales de la Parte 7 completan el arco hacia la maestría: E/S asíncrona y gestión de eventos, estilo de código y legibilidad, contribución a FreeBSD, y un proyecto final que reúne varios hilos de capítulos anteriores. Cada uno se apoya en lo que has hecho aquí.

El trabajo de medición ha concluido. El siguiente capítulo trata sobre qué hacer cuando los números señalan algo que las herramientas de ajuste por sí solas no pueden alcanzar.

### Glosario de términos introducidos en este capítulo

Una referencia rápida para el vocabulario que el capítulo introdujo o usó con frecuencia. Cuando un término tiene una página de manual dedicada o una primitiva específica de FreeBSD, se indica la referencia.

**Aggregation (DTrace):** Un resumen acumulativo en el lado del servidor (count, sum, min, max, average, quantize) calculado en espacio del kernel e impreso al final de la sesión. Usa agregaciones, no impresiones por evento, para cualquier probe que dispare más de unas pocas veces por segundo.

**Benchmark harness:** Un script repetible que ejecuta una carga de trabajo contra el driver con parámetros controlados, recoge mediciones e informa de los resultados en un formato consistente. El harness reduce la varianza entre mediciones al eliminar la variación humana.

**Bounce buffer:** Un buffer del kernel que se sitúa entre un dispositivo y la memoria de usuario cuando no es posible el DMA directo por restricciones de alineación o de rango de direcciones. Gestionado por `bus_dma(9)`. Los bounce buffers cuestan una copia extra; eliminarlos es un objetivo de ajuste habitual.

**Cache coherence:** El mecanismo hardware que mantiene consistentes las cachés en múltiples CPUs. Cuando una CPU escribe una línea, las demás CPUs que tienen esa línea deben invalidar o actualizar sus copias. El coste del tráfico de coherencia es la razón por la que el false sharing es un problema.

**Cache-line alignment:** Colocar una variable en un límite que coincide con el tamaño de la línea de caché del hardware (típicamente 64 bytes en amd64). Se usa para evitar el false sharing entre una variable caliente y sus vecinas. `__aligned(CACHE_LINE_SIZE)`.

**Callgraph (profiling):** Un árbol de relaciones llamador-llamado ponderado por el recuento de muestras. Leer un callgraph te indica qué caminos de llamada consumen más tiempo, no solo qué funciones hoja.

**Coalescing:** Combinar muchos eventos pequeños en unos pocos más grandes. El coalescing de interrupciones hardware reduce la tasa de interrupciones a costa de la latencia por evento.

**Counter(9):** Una primitiva de FreeBSD para contadores sumados por CPU. Barata al actualizar, barata al leer el agregado, escala con el número de CPUs.

**DPCPU:** Almacenamiento de variables por CPU en FreeBSD con macros de alineación y acceso. `DPCPU_DEFINE_STATIC`, `DPCPU_GET`, `DPCPU_PTR`, `DPCPU_SUM`.

**DTrace:** El framework de rastreo dinámico de FreeBSD. Seguro, omnipresente, con coste prácticamente nulo cuando está desactivado. Los scripts componen probes, predicados y acciones.

**Event mode (PMC):** Modo de contador de rendimiento hardware en el que el contador cuenta un evento específico (ciclos, instrucciones, fallos de caché). Compárese con el modo de muestreo.

**False sharing:** Ping-pong de coherencia de caché entre CPUs causado por variables no relacionadas que residen en la misma línea de caché. El efecto es invisible en el código fuente y evidente en los resultados del perfil.

**`fbt` provider:** El proveedor de DTrace para *function boundary tracing* (rastreo en los límites de función). Proporciona probes en la entrada y el retorno de cada función del kernel que no está inlinada.

**Filter handler:** Un manejador de interrupciones que se ejecuta en el contexto de interrupción de bajo nivel. Hace el mínimo indispensable y o bien reclama la interrupción y señala al ithread (`FILTER_SCHEDULE_THREAD`), o la rechaza (`FILTER_STRAY`).

**Flame graph:** Una visualización de las muestras del perfil como rectángulos anidados. El ancho representa el recuento de muestras; la altura representa la profundidad de la pila. Hace visual la identificación de los puntos calientes.

**hwpmc(4):** El módulo del kernel de FreeBSD que expone los contadores de rendimiento hardware. Se carga con `kldload hwpmc`.

**Ithread:** Thread de interrupción. Un thread del kernel que ejecuta un manejador de interrupciones en contexto de thread, no en contexto de interrupción. Se planifica con una prioridad especificada por el driver.

**Latency:** El tiempo desde la llegada de una solicitud hasta su finalización. Normalmente se informa como percentiles (P50, P95, P99, P99.9) en lugar de una media.

**Lock contention:** El estado en que múltiples threads compiten por el mismo lock, haciendo que algunos esperen. Detectable con el proveedor `lockstat` de DTrace.

**MSI / MSI-X:** Message Signalled Interrupt (PCIe). Sustituye las interrupciones INTx heredadas por mensajes dentro de la banda. MSI-X permite muchos vectores independientes por dispositivo, lo que habilita el manejo de interrupciones por cola o por CPU.

**NAPI-style batching:** Una técnica tomada de Linux pero aplicable en FreeBSD en la que una interrupción desactiva las interrupciones adicionales del dispositivo y un bucle de sondeo vacía la cola hasta que está vacía, y luego reactiva las interrupciones. Amortiza el coste de las interrupciones bajo carga.

**Observability:** La propiedad de un sistema que hace que su estado interno sea visible para las herramientas externas. En FreeBSD, los árboles sysctl, los contadores, los probes SDT y los mensajes de registro contribuyen a la observabilidad.

**P99 / P99.9:** Los percentiles 99 y 99,9 de una distribución. El valor por debajo del cual cae el 99 % (o el 99,9 %) de las muestras. Los percentiles de cola a menudo revelan una peor experiencia de usuario que las medias.

**Per-CPU data:** Estructuras de datos que tienen una copia distinta en cada CPU, eliminando la contención por línea de caché. Las macros `DPCPU` de FreeBSD gestionan la disposición y el acceso.

**pmcstat(8):** La herramienta de espacio de usuario que maneja `hwpmc(4)`. Admite el modo de eventos y el modo de muestreo, tanto a nivel de sistema como por proceso.

**`profile` provider (DTrace):** Un proveedor de DTrace que dispara a una frecuencia fija (por ejemplo, 997 Hz) en todas las CPUs. Se usa para el perfilado estadístico portátil.

**Responsiveness:** El tiempo desde un estímulo externo (interrupción, ioctl) hasta la primera acción del driver. Distinto de la latencia (de la solicitud a la finalización) y del throughput.

**Sampling mode (PMC):** Modo de contador de rendimiento hardware en el que el contador dispara una interrupción cada N eventos y el kernel registra el contador de programa. Se usa para el perfilado estadístico.

**`sched` provider (DTrace):** Un proveedor de DTrace para eventos del planificador: `on-cpu`, `off-cpu`, `enqueue`, `dequeue`. Se usa para diagnosticar la latencia relacionada con la planificación.

**SDT probe:** Probe de rastreo definida estáticamente. Compilada en el driver con los macros `SDT_PROBE_DEFINE` y `SDT_PROBE*()`. Siempre presente, siempre estable, con coste prácticamente nulo cuando está desactivada.

**softc:** La estructura de *contexto software* de un driver. Contiene el estado por dispositivo. Se accede con `device_get_softc(dev)`.

**sysctl tree:** Una jerarquía de variables del kernel expuesta al espacio de usuario a través de `sysctl(8)`. Por convención, los drivers registran nodos bajo `hw.<drivername>`.

**Tail latency:** Los percentiles altos de una distribución de latencia (P99, P99.9, P99.99). A menudo es lo que los usuarios perciben como *lento*, incluso cuando las medias son buenas.

**Taskqueue:** Un pool de threads del kernel de FreeBSD para trabajo diferido. Los drivers encolan tareas con `taskqueue_enqueue(9)`; el pool de threads las desencola y ejecuta.

**TMA (Top-Down Microarchitecture Analysis):** Un método para clasificar los problemas de rendimiento limitados por la CPU. Se ramifica en *frontend-bound*, *bad-speculation*, *backend-bound* y *retiring*.

**Throughput:** La tasa a la que el driver completa trabajo, medida en operaciones por segundo o bytes por segundo.

**UMA zone:** Un asignador especializado para objetos de tamaño fijo. Se crea con `uma_zcreate()`. Asignación barata con caché por CPU; adecuada para caminos críticos con buffers de tamaño fijo.

**workload:** El patrón específico de solicitudes utilizado durante la medición. El rendimiento de un driver depende del workload; una medición sin un workload nombrado está incompleta.

### Una última reflexión

La ingeniería de rendimiento tiene fama de ser arcana. La fama está a medias ganada y a medias inmerecida. Está ganada porque las herramientas son muchas, la salida es numérica y la interpretación requiere experiencia. Está inmerecida porque la disciplina es pequeña y aprendible: mide antes de cambiar, expresa el objetivo en números, prefiere la herramienta más sencilla, conoce el coste de tus probes, preserva la corrección. Esa es la disciplina entera en una frase.

La lección más difícil del capítulo es la de la humildad. El instinto de un principiante es mirar una línea de código y pensar *puedo hacer esto más rápido*. El instinto de un ingeniero con experiencia es mirar una medición y pensar *esta es la función caliente; ahora entendamos por qué*. La brecha entre las dos mentalidades es la brecha entre años de práctica. Cada hora pasada con DTrace, `pmcstat` y un cuaderno lleno de mediciones reales la reduce.

Ahora ya conoces la mitad de medición del kit de rendimiento de FreeBSD. Las herramientas están en el árbol de código fuente; las páginas de manual te esperan; el hábito de usarlas es la última pieza. Conviértelo en parte de cada driver serio que escribas, y el resto viene solo.

Un último aliento de ánimo antes de continuar. Si la profundidad del capítulo te ha resultado intimidante, vuelve a él sección por sección. El trabajo de rendimiento no es una habilidad única que se domine en una sola sesión; es un conjunto de habilidades relacionadas, cada una pequeña por sí sola. Ejecuta un one-liner de DTrace. Expón un contador de sysctl. Lanza una sesión de `pmcstat`. Cada pequeña práctica genera una pequeña confianza, y esas confianzas se van acumulando. Al cabo de unos pocos drivers, el conjunto de herramientas resulta rutinario en lugar de exótico, y la mentalidad de *mide, luego decide* se convierte en algo natural en lugar de una lista de comprobación.

Al Capítulo 34 y a las técnicas de depuración que complementan todo lo que acabamos de ver.

Respira hondo; te has ganado el siguiente capítulo.
