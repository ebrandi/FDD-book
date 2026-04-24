---
title: "Arnés de benchmark y resultados"
description: "Un arnés de benchmark reproducible, con código fuente funcional y mediciones representativas, para las afirmaciones sobre el rendimiento realizadas en los capítulos 15, 28, 33 y 34."
appendix: "F"
lastUpdated: "2026-04-21"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 35
language: "es-ES"
---
# Apéndice F: Infraestructura de benchmarks y resultados

## Cómo usar este apéndice

Varios capítulos de este libro realizan afirmaciones sobre rendimiento. El capítulo 15 ofrece tiempos de orden de magnitud para `mtx_lock`, `sx_slock`, las variables de condición y los semáforos. El capítulo 28 señala que el código del driver suele reducirse entre un treinta y un cincuenta por ciento cuando un driver de red se convierte a `iflib(9)`. El capítulo 33 trata la jerarquía de costes de las fuentes de timecounter en FreeBSD, con TSC en un extremo, HPET en el otro y ACPI-fast en el centro. El capítulo 34 describe el coste en tiempo de ejecución de los kernels de depuración con `INVARIANTS` o `WITNESS` activados, y de los kernels con scripts DTrace activos. Todas esas afirmaciones están matizadas en el texto con expresiones como «en hardware FreeBSD 14.3-amd64 típico», «en nuestro entorno de laboratorio» u «orden de magnitud». Los matizadores existen porque los números absolutos dependen de la máquina concreta, la carga de trabajo concreta y el compilador concreto con el que se construyó el kernel.

Este apéndice existe para que esos matizadores se asienten sobre una base reproducible. Para cada clase de afirmación, el árbol complementario bajo `examples/appendices/appendix-f-benchmarks/` contiene un harness funcional que el lector puede construir, ejecutar y ampliar. Cuando un harness es portable y no requiere acceso al hardware, este apéndice también informa del resultado que produce en una máquina conocida, de modo que los lectores dispongan de un número concreto con el que comparar. Cuando un harness requiere una configuración del kernel específica que no se puede asumir en la máquina de cualquier lector, solo se proporciona el harness en sí, junto con instrucciones claras para reproducirlo.

El objetivo no es reemplazar las afirmaciones de los capítulos 15 o 34 con cifras definitivas. El objetivo es permitir que los lectores vean cómo se llegó a esas afirmaciones, que las verifiquen en su propio hardware y que los resultados sean honestos sobre qué varía con el entorno y qué no.

### Cómo se organiza este apéndice

El apéndice tiene cinco secciones de benchmark, cada una con la misma estructura interna.

- **Qué se mide.** Un párrafo que describe la afirmación del capítulo y la cantidad que mide el harness.
- **El harness.** La ubicación en el sistema de archivos de los archivos complementarios, el lenguaje de programación utilizado y una breve descripción del enfoque.
- **Cómo reproducirlo.** El comando exacto o la secuencia de comandos que ejecuta el lector.
- **Resultado representativo.** Un valor medido, o «solo harness, sin resultado capturado» cuando el harness no ha sido ejecutado por el autor.
- **Rango de hardware.** El rango de máquinas sobre el que se espera que el resultado sea generalizable, y el rango sobre el que se sabe que no lo es.

Las cinco secciones en orden son: coste de lectura del timecounter, latencia de primitivas de sincronización, reducción del tamaño de código del driver iflib y sobrecarga de DTrace con INVARIANTS y WITNESS. Una sección final sobre la latencia de despertar del planificador apunta al script del capítulo 33 ya existente en lugar de presentar uno nuevo, ya que ese script ya es el harness.

## Configuración de hardware y software

Antes de presentar los benchmarks, una nota sobre el rango de aplicabilidad. Los números de este apéndice y los resultados representativos que se citan en él proceden de dos tipos de medición.

El primero es la **medición portable**: cualquier cosa que cuente líneas de código fuente, lea la salida de una herramienta determinista o que dependa únicamente del árbol de código fuente de FreeBSD y de un compilador funcional. Esas mediciones producen el mismo resultado en cualquier máquina que tenga la misma copia del código fuente. La comparación del tamaño de código de iflib en la sección 4 es la única medición portable del apéndice, y su resultado se puede reproducir exactamente en cualquier máquina con `/usr/src` sincronizado con una etiqueta de código fuente de FreeBSD 14.3-RELEASE.

El segundo tipo es la **medición dependiente del hardware**: cualquier cosa que mida en tiempo un camino del kernel, una syscall o una lectura de un registro de hardware. Esas mediciones dependen del CPU, la jerarquía de memoria y la configuración del kernel. Para cada benchmark dependiente del hardware en este apéndice, se proporciona el harness, los pasos de reproducción son precisos, y solo se cita un resultado representativo cuando el autor ha ejecutado realmente el harness en una máquina conocida. Cuando no lo ha hecho, el apéndice lo indica explícitamente y deja la tabla de resultados en blanco para que el lector la complete.

La descripción más justa es que este apéndice es una **guía de campo ejecutable**. Los capítulos citan cifras matizadas; el apéndice te muestra cómo medir las mismas cantidades en el hardware que tienes delante, y qué esperar si tu hardware pertenece a la misma familia amplia que el hardware que los capítulos tenían en mente («amd64 moderno», «generaciones de Intel y AMD actualmente en uso»).

### Advertencias aplicables a todas las secciones

Algunas advertencias se aplican a lo largo de todo el apéndice, y es más sencillo nombrarlas una sola vez que repetirlas en cada sección.

Todos los harnesses dependientes del hardware miden medias sobre bucles grandes. Las medias ocultan el comportamiento de la cola. Una latencia P99 puede ser un orden de magnitud superior a la media en el mismo camino, especialmente para cualquier cosa que implique un despertar del planificador. Cualquier afirmación seria sobre rendimiento en producción necesitará una medición distribucional, no un único número; este apéndice trata sobre la media porque es a lo que se refieren las afirmaciones de los capítulos.

Los lectores que ejecuten estos harnesses bajo virtualización deben esperar resultados significativamente más ruidosos que en bare metal. Un TSC virtualizado, por ejemplo, puede ser sintetizado por el hipervisor de una forma que añade cientos de nanosegundos a cada lectura. La jerarquía de costes del capítulo 33 sigue siendo válida cualitativamente bajo virtualización, pero los números absolutos cambiarán.

Por último, ninguno de estos harnesses está pensado para su uso en kernels de producción. El kmod de primitivas de sincronización en particular genera threads del kernel que ejecutan bucles ajustados de no-op; es seguro cargarlo durante unos segundos en una máquina de desarrollo y descargarlo, pero no debe cargarse en un servidor en producción.

## Coste de lectura del timecounter

### Qué se mide

El capítulo 33 describe una jerarquía de costes de tres niveles para las fuentes de timecounter en FreeBSD: TSC es barato de leer, ACPI-fast tiene un coste moderado y HPET es caro. La afirmación está matizada con «en las generaciones de Intel y AMD actualmente en uso» y, por separado, con «en hardware FreeBSD 14.3-amd64 típico». El harness de esta sección mide el coste medio de una llamada a `clock_gettime(CLOCK_MONOTONIC)`, que el kernel resuelve a través de la fuente que `kern.timecounter.hardware` tenga seleccionada en ese momento, y por separado el coste de una instrucción `rdtsc` directa como valor mínimo de referencia.

La cantidad medida son nanosegundos por llamada, promediada sobre diez millones de iteraciones. El camino del kernel subyacente es `sbinuptime()` en `/usr/src/sys/kern/kern_tc.c`, que lee el método `tc_get_timecount` del timecounter actual, lo escala y devuelve el resultado como un `sbintime_t`.

### El harness

El harness se encuentra bajo `examples/appendices/appendix-f-benchmarks/timecounter/` y está compuesto por tres piezas.

`tc_bench.c` es un pequeño programa en espacio de usuario que llama a `clock_gettime(CLOCK_MONOTONIC)` en un bucle ajustado e informa de los nanosegundos medios por llamada. Lee `kern.timecounter.hardware` al arrancar e imprime el nombre de la fuente actual, de modo que cada ejecución sea autodocumentada.

`rdtsc_bench.c` es un programa en espacio de usuario complementario que lee la instrucción `rdtsc` directamente, usando el patrón de ensamblado en línea que el kernel utiliza en su propio envoltorio `rdtsc()` en `/usr/src/sys/amd64/include/cpufunc.h`. Su salida es el coste de la propia instrucción, sin ninguna sobrecarga del kernel.

`run_tc_bench.sh` es un script de shell solo para root que lee `kern.timecounter.choice` (la lista de fuentes disponibles para el kernel actual), itera sobre cada entrada, establece `kern.timecounter.hardware` a esa fuente, ejecuta `tc_bench` y restaura la configuración original al salir. El resultado es una tabla con una fila por fuente de timecounter, lista para comparar.

### Cómo reproducirlo

Construye los dos programas en espacio de usuario:

```console
$ cd examples/appendices/appendix-f-benchmarks/timecounter
$ make
```

Ejecuta la rotación (requiere root para cambiar el sysctl):

```console
# sh run_tc_bench.sh
```

O simplemente el valor mínimo TSC directo:

```console
$ ./rdtsc_bench
```

### Resultado representativo

Solo harness, sin resultado capturado. El harness ha sido compilado y su lógica revisada, pero el autor no lo ha ejecutado en la máquina de referencia mientras escribía este apéndice. Un lector que ejecute el harness en hardware FreeBSD 14.3-amd64 típico debería esperar que la columna TSC informe de valores en las decenas bajas de nanosegundos, que la columna ACPI-fast informe de valores varias veces superiores y que la columna HPET (si está disponible y no está desactivada en el firmware) informe de valores aún un orden de magnitud superiores. Los números absolutos variarán según la generación del CPU, el estado de energía y si `clock_gettime` es atendido por el camino fast-gettime o cae hasta la syscall completa.

### Rango de hardware

El orden de las tres fuentes de timecounter por coste ha sido estable en las generaciones amd64 desde que el TSC invariante se convirtió en estándar a mediados de los años 2000. Los lectores con distintos fabricantes de CPU o diferentes generaciones microarquitectónicas verán números absolutos distintos pero el mismo orden. En ARM64, no existe HPET ni ACPI-fast en el sentido habitual, y la comparación relevante es entre el registro de contador del Generic Timer y los caminos de software que lo envuelven; el harness seguirá ejecutándose, pero solo aparecerá una entrada en la tabla. Si `kern.timecounter.choice` en la máquina del lector muestra una única fuente, eso en sí mismo es un dato útil: el firmware del sistema ha restringido la elección y no es posible ninguna rotación.

Consulta también el capítulo 33 para el contexto circundante, en particular la sección sobre `sbinuptime()` y la discusión sobre por qué el código del driver debería evitar leer `rdtsc()` directamente.

## Latencias de primitivas de sincronización

### Qué se mide

El capítulo 15 presenta una tabla de costes aproximados por operación para las primitivas de sincronización de FreeBSD: las operaciones atómicas en uno o dos nanosegundos, `mtx_lock` sin contención en decenas de nanosegundos, `sx_slock` y `sx_xlock` sin contención algo superiores, y `cv_wait`/`sema_wait` en microsegundos porque siempre implican un despertar completo del planificador. Los números de la tabla se describen como «estimaciones de orden de magnitud en hardware FreeBSD 14.3 amd64 típico», con la advertencia de que «pueden variar por un factor de dos o más entre generaciones de CPU». El harness de esta sección mide cada fila de esa tabla directamente.

Las cantidades medidas son:

- Nanosegundos por par `mtx_lock` / `mtx_unlock` en un mutex sin contención.
- Nanosegundos por par `sx_slock` / `sx_sunlock` en un sx sin contención.
- Nanosegundos por par `sx_xlock` / `sx_xunlock` en un sx sin contención.
- Nanosegundos por ida y vuelta individual de `cv_signal` / `cv_wait` entre dos threads del kernel.
- Nanosegundos por ida y vuelta individual de `sema_post` / `sema_wait` entre dos threads del kernel.

### El harness

El harness se encuentra bajo `examples/appendices/appendix-f-benchmarks/sync/` y es un único módulo del kernel cargable, `sync_bench.ko`. El módulo expone cinco sysctls de solo escritura bajo `debug.sync_bench.` (uno por benchmark) y cinco sysctls de solo lectura que informan del resultado más reciente de cada benchmark.

Cada benchmark mide en tiempo un número fijo de iteraciones usando `sbinuptime()` en `/usr/src/sys/kern/kern_tc.c` para las marcas de tiempo. Los benchmarks de mutex, sx_slock y sx_xlock se ejecutan completamente en el thread que los llama y ejercitan únicamente el camino rápido sin contención. Los benchmarks de cv y sema generan un kproc trabajador que ejecuta un protocolo ping/pong con el thread principal; cada iteración incluye por tanto un despertar y un cambio de contexto en cada dirección, que es precisamente lo que mide la columna «latencia de despertar» del capítulo 15.

El módulo sigue el patrón de kmod utilizado en otros capítulos de este libro, declarado con `DECLARE_MODULE` e inicializado mediante `SI_SUB_KLD / SI_ORDER_ANY`. Las fuentes son `/usr/src/sys/kern/kern_mutex.c` para `mtx_lock`, `/usr/src/sys/kern/kern_sx.c` para `sx_slock` / `sx_xlock`, `/usr/src/sys/kern/kern_condvar.c` para las variables de condición, y `/usr/src/sys/kern/kern_sema.c` para los semáforos de conteo.

### Cómo reproducir

Construye el módulo:

```console
$ cd examples/appendices/appendix-f-benchmarks/sync
$ make
```

Cárgalo y ejecútalo:

```console
# kldload ./sync_bench.ko
# sh run_sync_bench.sh
# kldunload sync_bench
```

`run_sync_bench.sh` ejecuta cada benchmark en secuencia e imprime una pequeña tabla; también es posible activar benchmarks individuales directamente escribiendo un `1` en el sysctl correspondiente `debug.sync_bench.run_*` y leyendo luego `debug.sync_bench.last_ns_*`.

### Resultado representativo

Solo el harness, sin resultado capturado. El kmod ha sido escrito contra las cabeceras de FreeBSD 14.3 y su lógica revisada en relación con la tabla del Capítulo 15, pero el autor no lo ha cargado ni ejecutado en la máquina de referencia mientras escribía este apéndice. Un lector que ejecute el harness en hardware típico de FreeBSD 14.3-amd64 puede esperar que los valores de mutex y sx sin contención se sitúen en el orden de pocas decenas de nanosegundos, y que los valores de vuelta completa de cv y sema se sitúen en los pocos microsegundos, ya que esos caminos cruzan el planificador dos veces. Cualquier lector cuyos números sean más del doble de esos valores debería revisar la afinidad del planificador, el escalado de frecuencia de la CPU y si el host está bajo carga adicional.

### Entorno de hardware

El calificativo «orden de magnitud» de la tabla del Capítulo 15 es deliberado. Los costes de lock sin contención siguen el coste de una o dos operaciones atómicas de compare-and-swap sobre la línea de caché actual, que varía según la generación de CPU y la topología de caché. Los costes de vuelta completa de activación siguen la latencia del planificador, que varía más: un servidor con CPU dedicada y `kern.sched.preempt_thresh` ajustado para baja latencia puede mostrar tiempos de vuelta por debajo del microsegundo, mientras que una máquina multi-inquilino con carga puede llegar a decenas de microsegundos. El harness informa de una única media por benchmark; los lectores que necesiten una distribución deberían ampliar el módulo para capturar cuantiles, o usar en su lugar las sondas DTrace `lockstat` sobre el kernel en ejecución.

Véase también el Capítulo 15 para el marco conceptual y la orientación sobre cuándo es apropiado cada primitivo.

## Reducción del tamaño de código del driver iflib

### Qué se mide

El Capítulo 28 afirma que, en los drivers que se han convertido a `iflib(9)` hasta la fecha, el código del driver se reduce típicamente entre un treinta y un cincuenta por ciento en comparación con la implementación equivalente con plain-ifnet. A diferencia de los otros benchmarks de este apéndice, este no es una medición de hardware. Es una medición del código fuente: cuántas líneas de fuente C necesita un driver de NIC moderno bajo `iflib(9)` frente a cuántas necesitaba sin él.

La cantidad medida es el recuento de líneas del archivo fuente principal de un driver, dividido en tres cifras: el total bruto de `wc -l`, el número de líneas no vacías y el número de líneas no vacías ni comentadas (una aproximación de «líneas de código» lo suficientemente cercana a la realidad para una comparación de orden de magnitud).

### El harness

El harness reside en `examples/appendices/appendix-f-benchmarks/iflib/` y es un conjunto de scripts de shell portables.

`count_driver_lines.sh` toma un archivo fuente de driver e informa de las tres cifras. El filtrado de comentarios es un paso sencillo con `awk` que entiende las formas `/* ... */` (incluyendo múltiples líneas) y `// ... EOL`; no es un parser C completo, pero es suficientemente preciso para ser útil.

`compare_iflib_corpus.sh` es el script principal. Recorre dos corpus curados y produce una tabla comparativa:

- Corpus iflib: `/usr/src/sys/dev/e1000/if_em.c`, `/usr/src/sys/dev/ixgbe/if_ix.c`, `/usr/src/sys/dev/igc/if_igc.c`, `/usr/src/sys/dev/vmware/vmxnet3/if_vmx.c`.
- Corpus plain-ifnet: `/usr/src/sys/dev/re/if_re.c`, `/usr/src/sys/dev/bge/if_bge.c`, `/usr/src/sys/dev/fxp/if_fxp.c`.

Los drivers de iflib se seleccionaron haciendo grep de las callbacks de método `IFDI_`, que son los puntos de interfaz característicos de iflib; los drivers plain-ifnet se seleccionaron para abarcar un rango comparable de clases de hardware. Ambas listas son variables al inicio del script que el lector puede editar.

`git_conversion_delta.sh` es un tercer script para lectores que dispongan de un clon Git completo de FreeBSD con historial. Localiza el commit que convirtió un driver concreto a iflib (buscando commits que toquen el archivo y mencionen «iflib» en el log) e informa del delta de recuento de líneas en ese commit. La diferencia antes/después de la conversión es la única forma de medir directamente la afirmación del Capítulo 28; la comparación entre drivers es un proxy que depende de que la complejidad de los drivers sea aproximadamente comparable, lo cual es una suposición fuerte.

### Cómo reproducir

En cualquier checkout del código fuente de FreeBSD 14.3:

```console
$ cd examples/appendices/appendix-f-benchmarks/iflib
$ sh compare_iflib_corpus.sh /usr/src
```

Para la medición antes/después, en un clon Git completo de FreeBSD:

```console
$ sh git_conversion_delta.sh /path/to/freebsd-src.git if_em.c
```

### Resultado representativo

Capturado contra el árbol de código fuente de FreeBSD 14.3-RELEASE, `compare_iflib_corpus.sh` produce el siguiente resumen:

```text
=== iflib ===
  if_em.c  raw=5694  nonblank=5044  code=4232
  if_ix.c  raw=5168  nonblank=4519  code=3573
  if_igc.c raw=3305  nonblank=2835  code=2305
  if_vmx.c raw=2544  nonblank=2145  code=1832
  corpus=iflib drivers=4 avg_code=2985

=== plain-ifnet ===
  if_re.c  raw=4151  nonblank=3693  code=3037
  if_bge.c raw=6839  nonblank=6055  code=4990
  if_fxp.c raw=3245  nonblank=2943  code=2228
  corpus=plain-ifnet drivers=3 avg_code=3418

=== summary ===
  iflib avg code lines:       2985
  plain-ifnet avg code lines: 3418
  delta:                      433
  reduction:                  12%
```

Una reducción entre corpus de aproximadamente un doce por ciento es menor que la afirmación del treinta a cincuenta por ciento del Capítulo 28, lo cual es exactamente lo que advierte la nota al final del script. La afirmación del capítulo es una cifra por driver antes y después: el mismo hardware, convertido a iflib, pasa de N líneas a (0.5-0.7) veces N líneas. Una comparación entre drivers distintos es algo diferente: compara hardware diferente con conjuntos de características distintos y distinto número de peculiaridades. La reducción entre drivers establece un límite inferior (hay *alguna* reducción en el tamaño medio del corpus), y la reducción por driver citada en el Capítulo 28 establece el límite superior (los commits de conversión individuales muestran el número mayor). Los lectores que dispongan de un clon Git pueden usar `git_conversion_delta.sh` para verificar directamente el número por driver.

### Medición anclada por driver

Para anclar el rango del Capítulo 28 a una cifra concreta, ejecutamos la comparación por commit el 2026-04-21 contra el commit de conversión de ixgbe `4fd3548cada3` («ixgbe(4): Convert driver to use iflib», de fecha 2017-12-20, por erj@FreeBSD.org). Ese commit es la conversión por driver más limpia del árbol: no consolidó drivers ni cambió características en la misma revisión. Los cuatro archivos específicos del driver que existían en ambos lados del commit (`if_ix.c`, `if_ixv.c`, `ix_txrx.c` e `ixgbe.h`) pasaron de 10.606 líneas brutas (7.093 no vacías ni comentadas) a 7.600 líneas brutas (5.074 no vacías ni comentadas) en el propio commit de conversión, una reducción del veintiocho por ciento en líneas de código. Restringiendo la comparación a los archivos PF principales (`if_ix.c` e `ix_txrx.c`) se ajusta el resultado al treinta y dos por ciento, dentro del rango de treinta a cincuenta por ciento del Capítulo 28. La cifra del veintiocho por ciento es la principal, y la cifra más estrecha del treinta y dos por ciento muestra cuánto del residuo corresponde a código de cabecera compartida y código hermano de VF, en lugar de lógica del driver ahorrada por el framework.

Un dato mayor pero menos representativo es el commit de conversión anterior de em/e1000 `efab05d61248` («Migrate e1000 to the IFLIB framework», 2017-01-10), que logra una reducción de aproximadamente el setenta por ciento en el código del driver e1000: las fuentes combinadas del driver caen de 13.188 a 3.920 líneas no vacías ni comentadas. Ese commit fusionó `if_em.c`, `if_igb.c` e `if_lem.c` en un único `if_em.c` basado en iflib más un nuevo `em_txrx.c`, por lo que la reducción medida mezcla el ahorro del framework con la consolidación de tres drivers relacionados en uno, y no debe interpretarse como una cifra típica por driver. Tomados conjuntamente, los datos de ixgbe y e1000 enmarcan el rango de treinta a cincuenta por ciento del Capítulo 28 por debajo y por arriba: una conversión limpia de un único driver se sitúa en el límite inferior o ligeramente por debajo, y una conversión que consolida drivers supera el límite superior.

### Entorno de hardware

Este benchmark no depende del hardware. El resultado es una función del árbol de código fuente de FreeBSD en una revisión concreta, y debería ser idéntico en cualquier máquina con el mismo checkout. Los lectores que usen una rama diferente de FreeBSD (15-CURRENT, una versión anterior) verán números absolutos distintos porque el árbol evoluciona.

Véase también el Capítulo 28 para la discusión circundante sobre `ifnet(9)` y cómo `iflib(9)` se sitúa dentro de él.

## Sobrecarga de DTrace, INVARIANTS y WITNESS

### Qué se mide

El Capítulo 34 hace dos afirmaciones de rendimiento sobre los kernels de depuración:

- Un kernel `INVARIANTS` bajo carga funciona aproximadamente entre un cinco y un veinte por ciento más lento que un kernel de release, a veces más en cargas de trabajo con muchas asignaciones, como cifra aproximada de orden de magnitud en hardware típico de FreeBSD 14.3-amd64.
- `WITNESS` añade contabilidad a cada adquisición y liberación de lock; en nuestro entorno de laboratorio, en un kernel bajo carga que ejecuta cargas de trabajo con muchos locks, la sobrecarga puede acercarse al veinte por ciento.

También menciona, en el contexto de DTrace, que los scripts de DTrace activos añaden sobrecarga proporcional a cuántas sondas activan y cuánto trabajo realiza cada sonda.

La cantidad medida por el harness en esta sección es el tiempo de reloj real para completar una carga de trabajo fija. El harness no intenta calcular porcentajes directamente; proporciona dos cargas de trabajo bien definidas y un formato de salida coherente, y espera que el lector ejecute el conjunto una vez por condición del kernel y calcule las razones entre ellos.

### El harness

El harness reside en `examples/appendices/appendix-f-benchmarks/dtrace/` y tiene cuatro partes.

`workload_syscalls.c` es un programa de userland que realiza un millón de iteraciones de un bucle compacto que contiene cuatro syscalls baratas (`getpid`, `getuid`, `gettimeofday`, `clock_gettime`). Esta carga de trabajo exagera el coste de los caminos de entrada y salida de syscall, donde las aserciones de `INVARIANTS` y el seguimiento de locks de `WITNESS` se activan con más frecuencia.

`workload_locks.c` es un programa de userland que lanza cuatro threads y realiza diez millones de pares `pthread_mutex_lock` / `pthread_mutex_unlock` por thread, sobre un pequeño conjunto rotativo de mutexes. Los mutexes de userland caen hacia el camino `umtx(2)` del kernel cuando hay contención, por lo que esta carga de trabajo ejercita el camino de contención con muchos locks que `WITNESS` instrumenta.

`dtrace_overhead.d` es un script DTrace mínimo que se activa en cada entrada y retorno de syscall sin imprimir nada por sonda. Adjuntarlo durante una ejecución de `workload_syscalls` mide el coste de tener el framework de sondas DTrace instrumentando activamente el camino de syscall.

`run_overhead_suite.sh` ejecuta ambas cargas de trabajo una vez, captura `uname` y `sysctl kern.conftxt`, y escribe un informe etiquetado. Se espera que el lector ejecute el conjunto cuatro veces: en un kernel `GENERIC` base, en un kernel `INVARIANTS`, en un kernel `WITNESS` y en cualquiera de ellos con `dtrace_overhead.d` adjunto en otro terminal. Comparar los cuatro informes proporciona los porcentajes que cita el Capítulo 34.

Las fuentes del kernel relacionadas son `/usr/src/sys/kern/subr_witness.c` para WITNESS, `/usr/src/sys/sys/proc.h` y sus archivos hermanos para las macros de aserción habilitadas por `INVARIANTS`, y las fuentes del proveedor DTrace bajo `/usr/src/sys/cddl/dev/` para el framework de sondas.

### Cómo reproducir

En cada condición del kernel:

1. Arranca en el kernel bajo prueba.
2. Construye las cargas de trabajo:

   ```console
   $ cd examples/appendices/appendix-f-benchmarks/dtrace
   $ make
   ```

3. Ejecuta el conjunto:

   ```console
   # sh run_overhead_suite.sh > result-<label>.txt
   ```

4. Para la condición de DTrace, lanza el script en otro terminal antes de ejecutar el conjunto:

   ```console
   # dtrace -q -s dtrace_overhead.d
   ```

Tras las cuatro ejecuciones, compara los archivos `result-*.txt` uno junto al otro. Las razones `INVARIANTS_ns / base_ns`, `WITNESS_ns / base_ns` y `dtrace_ns / base_ns` son las cifras de sobrecarga a las que se refiere el Capítulo 34.

### Resultado representativo

Solo el arnés, sin resultados capturados. Las cargas de trabajo se han compilado en el sistema de referencia y su lógica se ha revisado en relación con los postulados del Capítulo 34, pero el autor no ha construido los tres kernels de comparación para capturar cifras de extremo a extremo mientras redactaba este apéndice. Los lectores que ejecuten la comparación de cuatro kernels en hardware típico FreeBSD 14.3-amd64 pueden esperar que `INVARIANTS` se sitúe en el rango del cinco al veinte por ciento en `workload_syscalls`, algo más alto en `workload_locks` porque las rutas de lock son el código más caliente bajo `INVARIANTS`. `WITNESS` debería quedar por debajo de `INVARIANTS` en cargas de trabajo de syscall puras (donde se visitan pocas órdenes de lock nuevas) y por encima en `workload_locks`, aproximándose a la cifra del veinte por ciento que menciona el capítulo. La columna de DTrace debería ser pequeña en `workload_locks` (ninguna probe relevante se activa en la ruta caliente) y no trivial en `workload_syscalls` (cada iteración dispara dos probes).

### La envolvente de hardware

Los ratios son más estables entre distintos equipos que los números absolutos. Los lectores con CPUs diferentes verán tiempos de reloj distintos para la carga de trabajo de referencia, pero el porcentaje de sobrecarga por kernel debería mantenerse dentro de una banda estrecha, porque lo determina cuánto trabajo adicional realiza el kernel de depuración por operación, no la velocidad de cada operación. Las fuentes de sorpresa más habituales son la virtualización (donde la ruta del syscall tiene una sobrecarga adicional del hipervisor por llamada que diluye el porcentaje), la gestión agresiva de energía (donde la frecuencia del CPU varía entre ejecuciones) y el SMT (donde los números por núcleo físico difieren de los números por CPU lógica). El harness no intenta controlar ninguno de estos factores; si quieres obtener números de calidad para producción, necesitarás fijar la prueba a un único CPU, deshabilitar el escalado de frecuencia y ejecutar la suite varias veces.

Consulta también el Capítulo 34 para el tratamiento más amplio de los kernels de depuración y para la discusión sobre cuándo merece la pena habilitar cada uno de `INVARIANTS`, `WITNESS` y DTrace.

## Latencia de wakeup del planificador

El Capítulo 33 contiene un fragmento de DTrace que mide la latencia de wakeup del planificador usando `sched:::wakeup` y `sched:::on-cpu`. Ese fragmento ya es el harness; esta sección solo lo duplicaría. Si te interesan la cifra de submicrosegundo para el sistema en reposo y la cifra de decenas de microsegundos bajas para el sistema bajo contención que menciona el Capítulo 33, usa el script exactamente tal como aparece impreso allí, en tu propio hardware. Las fuentes del proveedor de DTrace están en `/usr/src/sys/cddl/dev/dtrace/`.

Si quieres comparar la latencia de wakeup con y sin `WITNESS`, el mismo script es válido; simplemente arranca los dos kernels y ejecútalo en cada uno.

## Cerrando: cómo usar el harness sin dejarse engañar

El harness de este apéndice existe porque cada número de los Capítulos 15, 28, 33 y 34 es una afirmación de orden de magnitud, y las afirmaciones de orden de magnitud merecen ser reproducibles. Unos pocos hábitos hacen que el harness sea realmente útil en lugar de una fuente de falsa confianza.

En primer lugar, ejecuta cada benchmark más de una vez. Los resultados de una sola ejecución están dominados por el ruido de arranque, los efectos de calentamiento y la interferencia de lo que esté ejecutándose en la máquina en ese momento. Los scripts del harness informan de un único número; ejecútalos de tres a cinco veces y toma la mediana, o amplíalos para que agreguen las ejecuciones automáticamente antes de fiarte del resultado.

En segundo lugar, mantén las condiciones honestas. Si quieres comparar `INVARIANTS` con un kernel base, compila ambos kernels con el mismo compilador, los mismos `CFLAGS` y la misma base `GENERIC` o `GENERIC-NODEBUG`. Si quieres comparar dos fuentes de timecounter, realiza ambas comparaciones en el mismo arranque del mismo kernel; reiniciar entre ejecuciones cambia demasiadas variables.

En tercer lugar, resiste la tentación de tratar los números como universales. Una medición realizada en un portátil de 4 núcleos en una oficina tranquila no es una medición en un servidor de producción de 64 núcleos en un rack ruidoso. El harness mide la máquina que tienes delante; las frases matizadas de los Capítulos 15 a 34 ("en hardware amd64 14.3 típico", "en nuestro entorno de laboratorio") son igual de honestas sobre esa misma limitación. El harness hace que la frase matizada sea verificable sin pretender que se vuelve universal.

En cuarto lugar, da preferencia a los ratios sobre los números absolutos. Una afirmación de que `WITNESS` cuesta un veinte por ciento es mucho más estable que una afirmación de que `WITNESS` cuesta cuatrocientos nanosegundos por lock. La primera sobrevive a cambios de CPU, cambios de compilador y cambios de versión del kernel que harían inútil a la segunda. Cuando ejecutes el harness en tu hardware, el porcentaje de sobrecarga es lo que debes conservar; el número absoluto es solo la aritmética que lo produjo.

Y por último, amplía el harness en lugar de fiarte ciegamente de él. Cada script aquí es lo suficientemente pequeño como para leerlo de principio a fin; cada kmod tiene menos de trescientas líneas. Si una afirmación de un capítulo te importa, abre el harness, verifica que mide lo que esperas que mida, modifícalo si tu carga de trabajo necesita algo diferente y ejecuta la versión modificada. El harness es un punto de partida, no una línea de llegada.

Las frases matizadas de los Capítulos 15, 28, 33 y 34 y el harness ejecutable de este apéndice son dos mitades de la misma disciplina. Los capítulos son honestos sobre lo que es variable; el apéndice te muestra cómo medir la variación tú mismo.
