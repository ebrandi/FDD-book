---
title: "Referencia de la API del kernel de FreeBSD"
description: "Una referencia práctica orientada a la consulta de las APIs del kernel de FreeBSD, macros, estructuras de datos y familias de páginas de manual utilizadas a lo largo de los capítulos de desarrollo de drivers del libro."
appendix: "A"
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 45
language: "es-ES"
---
# Apéndice A: Referencia de la API del kernel de FreeBSD

## Cómo usar este apéndice

Este apéndice es la tabla de consulta rápida que acompaña todo lo que el libro te ha enseñado a usar dentro de un driver de FreeBSD. Los capítulos principales desarrollan cada API con cuidado, la muestran funcionando en un driver real y explican el modelo mental que hay detrás. Este apéndice es su contraparte breve y fácil de hojear, pensada para tenerla abierta mientras programas, depuras o lees el driver de otra persona.

Está redactado deliberadamente como referencia y no como tutorial. No pretende enseñar ningún subsistema desde cero. Cada entrada asume que ya has encontrado la API en algún punto del libro, o que estás dispuesto a leer la página del manual antes de usarla. Lo que la entrada te ofrece es el vocabulario para orientarte: para qué sirve la API, el pequeño conjunto de nombres que realmente importan, los errores que es probable que cometas, dónde encaja habitualmente en el ciclo de vida del driver y qué capítulo la enseña en profundidad. Si la entrada cumple su función, podrás responder cuatro preguntas en menos de un minuto:

1. ¿Qué familia de APIs necesito para el problema que tengo delante?
2. ¿Cuál es el nombre exacto de la función, macro o tipo que busco?
3. ¿Qué advertencia debo revisar antes de confiar en ella?
4. ¿Qué página del manual o capítulo debo abrir a continuación?

No se promete nada más. Todos los detalles que siguen se han verificado contra el árbol de código fuente de FreeBSD 14.3 y las páginas del manual correspondientes en `man 9`. Cuando una distinción es importante pero se pospone a otra parte del libro, la entrada apunta hacia adelante en lugar de intentar resolverla aquí.

### Cómo están organizadas las entradas

El apéndice agrupa las APIs por el problema que resuelven, no alfabéticamente. Un driver rara vez busca un nombre de forma aislada. Busca una familia completa: memoria con sus flags, un lock con sus reglas de suspensión, un callout con su historia de cancelación. Mantener esas familias juntas hace que el apéndice sea más útil para consultas reales.

Dentro de cada familia, cada entrada sigue el mismo patrón breve:

- **Propósito.** Para qué sirve la API, en una o dos frases.
- **Uso típico en drivers.** Cuándo un driver recurre a ella.
- **Nombres clave.** Las funciones, macros, flags o tipos que realmente llamas o declaras.
- **Cabecera(s).** Dónde viven las declaraciones.
- **Advertencias.** El pequeño conjunto de errores que provocan bugs reales.
- **Fase del ciclo de vida.** En qué momento del probe, attach, operación normal o detach aparece habitualmente la API.
- **Páginas del manual.** Las entradas de `man 9` que debes leer a continuación.
- **Dónde lo enseña el libro.** Referencias a capítulos para el contexto completo.

Ten presente este patrón cuando hagas una consulta rápida. Si solo necesitas el nombre de un flag, mira **Nombres clave**. Si solo necesitas la página del manual, mira **Páginas del manual**. Si has olvidado para qué existe la API, lee **Propósito** y detente ahí.

### Qué no es este apéndice

Este apéndice no es un sustituto de `man 9`, ni de los capítulos de enseñanza del libro, ni de la lectura de drivers reales en `/usr/src/sys/dev/`. Es breve a propósito. La referencia canónica sigue siendo la página del manual; el modelo mental canónico sigue siendo el capítulo que introdujo la API; la verdad canónica sigue siendo el árbol de código fuente. Este apéndice te ayuda a encontrar los tres con rapidez.

Tampoco cubre todas las interfaces del kernel. El kernel es grande, y una referencia completa repetiría material que pertenece al Apéndice E (Internos de FreeBSD y referencia del kernel) o a capítulos específicos como el Capítulo 16 (Acceso al hardware), el Capítulo 19 (Gestión de interrupciones) y el Capítulo 20 (Gestión avanzada de interrupciones). El objetivo aquí es cubrir las APIs que un autor de drivers usa realmente en su trabajo cotidiano, con el nivel de detalle que realmente necesita.

## Orientación para el lector

Puedes usar este apéndice de tres maneras distintas, y cada una requiere una estrategia de lectura diferente.

Si estás **escribiendo código nuevo**, trátalo como una lista de verificación. Elige la familia que corresponda a tu problema, hojea las entradas, anota los nombres clave y salta a la página del manual o al capítulo para los detalles. Tiempo de inversión: uno o dos minutos por consulta.

Si estás **depurando**, trátalo como un mapa de supuestos. Cuando un driver se comporta mal, el bug casi siempre está en una advertencia que el autor pasó por alto: un mutex retenido durante una copia que puede bloquearse, un callout detenido pero no vaciado, un recurso liberado antes de desmantelar la interrupción. La línea **Advertencias** de cada entrada es donde viven esos supuestos. Léelas en orden y pregúntate si tu driver respeta cada una.

Si estás **leyendo un driver desconocido**, trátalo como un traductor. Cuando veas una función o macro que no reconoces, busca su familia en este apéndice, lee el **Propósito** y continúa. La comprensión completa puede llegar después, a través del capítulo o la página del manual. El objetivo durante la exploración es seguir avanzando y formarse un modelo mental inicial de lo que hace el driver.

Algunas convenciones usadas en todo el apéndice:

- Todas las rutas de código fuente se muestran en formato orientado al lector, `/usr/src/sys/...`, conforme a la estructura de un sistema FreeBSD estándar.
- Las páginas del manual se citan en el estilo habitual de FreeBSD: `mtx(9)` significa la sección 9 del manual. Puedes leer cualquiera de ellas con, por ejemplo, `man 9 mtx`.
- Cuando una familia no tiene una página del manual dedicada, la entrada lo indica y apunta a la documentación disponible más cercana.
- Cuando el libro pospone un tema a un capítulo posterior o al Apéndice E, la entrada apunta hacia adelante en lugar de inventar detalles aquí.

Con esto en mente, podemos abrir el apéndice propiamente dicho. La primera familia es la memoria: de dónde obtienen los drivers los bytes que necesitan, cómo los devuelven y qué flags controlan el comportamiento en el proceso.

## Asignación de memoria

Todo driver asigna memoria, y cada asignación conlleva reglas sobre cuándo puedes bloquearte, dónde está ubicada físicamente la memoria y cómo se devuelve. El kernel proporciona tres asignadores principales: `malloc(9)` para asignación de propósito general, `uma(9)` para objetos de tamaño fijo y alta frecuencia, y `contigmalloc(9)` para rangos físicamente contiguos que el hardware puede direccionar. A continuación encontrarás cada uno de un vistazo, junto con el pequeño vocabulario de flags que comparten.

### `malloc(9)` / `free(9)` / `realloc(9)`

**Propósito.** Asignador de memoria del kernel de propósito general. Te proporciona un buffer de bytes de cualquier tamaño, etiquetado con un `malloc_type` para poder contabilizarlo después con `vmstat -m`.

**Uso típico en drivers.** Asignación de softc, buffers pequeños de tamaño variable, espacio temporal de trabajo, en cualquier caso donde una zona de tamaño fijo sería excesiva.

**Nombres clave.**

- `void *malloc(size_t size, struct malloc_type *type, int flags);`
- `void free(void *addr, struct malloc_type *type);`
- `void *realloc(void *addr, size_t size, struct malloc_type *type, int flags);`
- `MALLOC_DEFINE(M_FOO, "foo", "description for vmstat -m");`
- `MALLOC_DECLARE(M_FOO);` para usarlo en cabeceras.

**Cabecera.** `/usr/src/sys/sys/malloc.h`.

**Flags importantes.**

- `M_WAITOK`: el llamador puede bloquearse hasta que haya memoria disponible. La asignación tendrá éxito o el kernel provocará un pánico.
- `M_NOWAIT`: el llamador no debe bloquearse. La asignación puede devolver `NULL`. Comprueba siempre `NULL` cuando uses `M_NOWAIT`.
- `M_ZERO`: pone a cero la memoria devuelta antes de entregarla. Se combina con cualquiera de los dos flags de espera.
- `M_NODUMP`: excluye la asignación de los volcados de crash.

**Advertencias.**

- `M_WAITOK` no debe usarse mientras se retiene un spin mutex, en un filtro de interrupción o en cualquier contexto que no pueda bloquearse.
- Los llamadores que usen `M_NOWAIT` deben comprobar el valor de retorno. No gestionar `NULL` es uno de los crashes de driver más frecuentes en revisión de código.
- Nunca mezcles familias de asignadores. La memoria devuelta por `malloc(9)` debe liberarse con `free(9)`; `uma_zfree(9)` y `contigfree(9)` no son intercambiables.
- El puntero `struct malloc_type` debe coincidir entre `malloc` y el `free` correspondiente.

**Fase del ciclo de vida.** Principalmente en `attach` (softc, buffers) y `detach` (liberación). Las asignaciones más pequeñas pueden aparecer en las rutas de I/O normales, siempre que el contexto permita el flag elegido.

**Página del manual.** `malloc(9)`.

**Dónde lo enseña el libro.** Se introduce en el Capítulo 5 junto a los modismos de C específicos del kernel; se usa en el Capítulo 7 cuando tu primer driver asigna su softc; se retoma en el Capítulo 10 cuando los buffers de I/O se vuelven reales, y de nuevo en el Capítulo 11 cuando los flags de asignación deben respetar las reglas de locking.

### Zonas `uma(9)`

**Propósito.** Cache de objetos de tamaño fijo, muy optimizada para asignaciones frecuentes, uniformes y sensibles al rendimiento. Reutiliza los objetos en lugar de acudir repetidamente al asignador general.

**Uso típico en drivers.** Estructuras similares a los mbuf de red, estado por paquete, descriptores por petición, en cualquier caso en el que asignes y liberes millones de objetos pequeños idénticos por segundo.

**Nombres clave.**

- `uma_zone_t uma_zcreate(const char *name, size_t size, uma_ctor, uma_dtor, uma_init, uma_fini, int align, uint32_t flags);`
- `void uma_zdestroy(uma_zone_t zone);`
- `void *uma_zalloc(uma_zone_t zone, int flags);`
- `void uma_zfree(uma_zone_t zone, void *item);`

**Cabecera.** `/usr/src/sys/vm/uma.h`.

**Flags.**

- `M_WAITOK`, `M_NOWAIT`, `M_ZERO` en la asignación, con idéntico significado que en `malloc(9)`.
- Los flags de creación como `UMA_ZONE_ZINIT`, `UMA_ZONE_NOFREE`, `UMA_ZONE_CONTIG` y las sugerencias de alineación (`UMA_ALIGN_CACHE`, `UMA_ALIGN_PTR`, etc.) ajustan el comportamiento para cargas de trabajo específicas.

**Advertencias.**

- Las zonas deben crearse antes de usarse y destruirse antes de que el módulo se descargue. Olvidar `uma_zdestroy` en `detach` provoca una fuga de toda la zona.
- Los constructores y destructores se ejecutan en la asignación y en la liberación, respectivamente, no en la creación y destrucción de la zona; usa los callbacks `init` y `fini` para el trabajo que se realiza una sola vez por slab.
- Crear una zona es costoso. Crea una por tipo de objeto y por módulo, no por instancia.
- No existe una página del manual dedicada a `uma(9)`. La referencia autorizada es la cabecera y los usuarios existentes bajo `/usr/src/sys/`.

**Fase del ciclo de vida.** `uma_zcreate` en la carga del módulo o al inicio del attach; `uma_zalloc` y `uma_zfree` en las rutas de I/O; `uma_zdestroy` en la descarga del módulo.

**Página del manual.** Ninguna dedicada. Lee `/usr/src/sys/vm/uma.h` y consulta `/usr/src/sys/kern/kern_mbuf.c` y `/usr/src/sys/net/netisr.c` para ver un uso realista.

**Dónde lo enseña el libro.** Se menciona brevemente en el Capítulo 7 como alternativa a `malloc(9)`; se retoma cuando los drivers de alta tasa lo necesitan en el Capítulo 28 (redes) y el Capítulo 33 (ajuste de rendimiento).

### `contigmalloc(9)` / `contigfree(9)`

**Propósito.** Asigna un rango de memoria físicamente contiguo dentro de una ventana de direcciones especificada. Es necesario cuando el hardware debe realizar DMA en memoria sin un IOMMU y, por tanto, requiere páginas físicas contiguas.

**Uso típico en drivers.** Buffers de DMA para dispositivos que no soportan scatter-gather, y solo después de confirmar que `bus_dma(9)` no es una opción más adecuada.

**Nombres clave.**

- `void *contigmalloc(unsigned long size, struct malloc_type *type, int flags, vm_paddr_t low, vm_paddr_t high, unsigned long alignment, vm_paddr_t boundary);`
- `void contigfree(void *addr, unsigned long size, struct malloc_type *type);`

**Cabecera.** `/usr/src/sys/sys/malloc.h`.

**Advertencias.**

- La fragmentación tras el boot hace que las asignaciones contiguas grandes fallen. No des el éxito por sentado.
- Para casi todo el hardware moderno, prefiere el framework `bus_dma(9)`. Gestiona tags, mapas, bouncing y alineación de forma portable.
- Las asignaciones de `contigmalloc` son un recurso escaso del sistema; libéralas tan pronto como sea posible.

**Fase del ciclo de vida.** Típicamente en `attach`; liberadas en `detach`.

**Página del manual.** `contigmalloc(9)`.

**Dónde lo enseña el libro.** Se menciona junto a `bus_dma(9)` en el Capítulo 21, cuando DMA se convierte por primera vez en una preocupación real.

### Tabla rápida de flags de asignación

| Flag         | Significado                                                        |
| :----------- | :----------------------------------------------------------------- |
| `M_WAITOK`   | El llamador puede bloquearse hasta que haya memoria disponible.   |
| `M_NOWAIT`   | El llamador no debe bloquearse; devuelve `NULL` en caso de error. |
| `M_ZERO`     | Pone a cero la asignación antes de devolverla.                    |
| `M_NODUMP`   | Excluye la asignación de los volcados de memoria.                 |

Usa `M_WAITOK` solo donde esté permitido que el hilo se bloquee. Ante la duda, la opción segura es `M_NOWAIT` junto con una comprobación de `NULL`.

## Primitivas de sincronización

Si la memoria es la materia prima de un driver, la sincronización es la disciplina que impide que dos contextos de ejecución la corrompan al mismo tiempo. FreeBSD te ofrece un conjunto de herramientas pequeño y bien diseñado. Los nombres que aparecen a continuación son los que encontrarás con más frecuencia. La enseñanza completa se encuentra en los Capítulos 11, 12 y 15, con los matices propios del contexto de interrupciones en los Capítulos 19 y 20; este apéndice reúne el vocabulario.

### `mtx(9)`: Mutexes

**Propósito.** La primitiva de exclusión mutua por defecto del kernel. Un thread adquiere el lock, entra en la sección crítica y lo libera.

**Uso típico en drivers.** Proteger los campos del softc, buffers circulares, contadores de referencias y cualquier estado compartido cuya sección crítica sea breve y no bloquee.

**Nombres clave.**

- `void mtx_init(struct mtx *m, const char *name, const char *type, int opts);`
- `void mtx_destroy(struct mtx *m);`
- `mtx_lock(m)`, `mtx_unlock(m)`, `mtx_trylock(m)`.
- `mtx_assert(m, MA_OWNED | MA_NOTOWNED | MA_RECURSED | MA_NOTRECURSED);` para invariantes.
- Helpers de espera sobre mutex: `msleep(9)` y `mtx_sleep(9)`.

**Cabecera.** `/usr/src/sys/sys/mutex.h`.

**Opciones.**

- `MTX_DEF`: el mutex por defecto, que permite dormir en caso de contención. Úsalo para casi todo.
- `MTX_SPIN`: spinlock puro. Obligatorio en el contexto de filtros de interrupción y en otros lugares donde bloquear es imposible. Las reglas son más estrictas.
- `MTX_RECURSE`: permite que el mismo thread adquiera el lock varias veces. Úsalo con precaución; a menudo oculta errores de diseño.
- `MTX_NEW`: obliga a `mtx_init` a tratar el lock como recién creado. Útil con `WITNESS`.

**Advertencias.**

- Nunca duermas mientras mantienes un mutex `MTX_DEF` o `MTX_SPIN`. `uiomove(9)`, `copyin(9)`, `copyout(9)`, `malloc(9, M_WAITOK)` y la mayoría de las primitivas de bus pueden dormir. Audita con cuidado.
- Empareja siempre `mtx_init` con `mtx_destroy`. Olvidar el destroy provoca fugas de estado interno y hace que `WITNESS` se queje.
- El orden de los locks importa. Una vez que el kernel ha visto que adquieres el lock A antes que el lock B, avisará si alguna vez inviertes ese par. Planifica tu jerarquía de locks con antelación.
- `MTX_SPIN` deshabilita la apropiación; mantenlo el menor tiempo posible.

**Fase del ciclo de vida.** `mtx_init` en attach; `mtx_destroy` en detach. Las operaciones de lock y unlock en todo lo que hay entre medias.

**Páginas de manual.** `mutex(9)`, `mtx_pool(9)`, `msleep(9)`.

**Dónde lo enseña el libro.** Primera explicación en el Capítulo 11, con mayor profundidad en el Capítulo 12 sobre orden de locks y disciplina con `WITNESS`, y revisado en el Capítulo 19 para las variantes seguras en contexto de interrupción (`MTX_SPIN`).

### `sx(9)`: Locks compartidos-exclusivos con soporte de bloqueo

**Propósito.** Un lock de lectores-escritores donde tanto el lector como el escritor pueden bloquearse. Úsalo cuando los múltiples lectores son habituales, los escritores son raros y la sección crítica puede dormir.

**Uso típico en drivers.** Estado de configuración leído por muchos caminos y modificado con poca frecuencia. No apto para la ruta de datos rápida.

**Nombres clave.**

- `void sx_init(struct sx *sx, const char *desc);`
- `void sx_destroy(struct sx *sx);`
- `sx_slock(sx)`, `sx_sunlock(sx)` para acceso compartido.
- `sx_xlock(sx)`, `sx_xunlock(sx)` para acceso exclusivo.
- `sx_try_slock`, `sx_try_xlock`, `sx_upgrade`, `sx_downgrade`.
- `sx_assert(sx, SA_SLOCKED | SA_XLOCKED | SA_LOCKED | SA_UNLOCKED);`

**Cabecera.** `/usr/src/sys/sys/sx.h`.

**Advertencias.**

- `sx` permite dormir dentro de la sección crítica, a diferencia de `mtx`. Esa flexibilidad es precisamente su razón de ser; asegúrate de que realmente la necesitas.
- Los locks `sx` son más costosos que los mutexes. No los uses como opción por defecto.
- Evita mezclar `sx` y `mtx` en el mismo orden de locks sin reflexionar detenidamente sobre las implicaciones.

**Fase del ciclo de vida.** `sx_init` en attach; `sx_destroy` en detach.

**Página de manual.** `sx(9)`.

**Dónde lo enseña el libro.** Capítulo 12.

### `rmlock(9)`: Locks de lectura mayoritaria

**Propósito.** Ruta de lectura extremadamente rápida, ruta de escritura más lenta. Los lectores no compiten entre sí. Diseñado para datos que se leen en cada operación pero se escriben muy raramente.

**Uso típico en drivers.** Tablas similares a las de enrutamiento, estado de configuración utilizado en rutas rápidas, estructuras donde el coste del escritor es aceptable porque las escrituras son poco frecuentes.

**Nombres clave.**

- `void rm_init(struct rmlock *rm, const char *name);`
- `void rm_destroy(struct rmlock *rm);`
- `rm_rlock(rm, tracker)`, `rm_runlock(rm, tracker)`.
- `rm_wlock(rm)`, `rm_wunlock(rm)`.

**Cabecera.** `/usr/src/sys/sys/rmlock.h`.

**Advertencias.**

- Cada lector necesita su propio `struct rm_priotracker`, normalmente en la pila. No lo compartas.
- Los lectores no pueden dormir a menos que el lock se haya inicializado con `RM_SLEEPABLE`.
- La ruta del escritor es costosa; si las escrituras son frecuentes, `sx` o `mtx` es una mejor opción.

**Fase del ciclo de vida.** `rm_init` en attach; `rm_destroy` en detach.

**Página de manual.** `rmlock(9)`.

**Dónde lo enseña el libro.** Se presenta brevemente en el Capítulo 12 y se utiliza en capítulos posteriores donde aparecen patrones de lectura mayoritaria.

### `cv(9)` / `condvar(9)`: Variables de condición

**Propósito.** Un canal de espera con nombre. Uno o más threads duermen hasta que otro thread señala que la condición que estaban esperando se ha cumplido.

**Uso típico en drivers.** Esperar a que un buffer se vacíe, a que el hardware termine un comando o a que se produzca una transición de estado concreta. Úsalas en lugar de canales `wakeup(9)` directos cuando quieras que el motivo de espera sea explícito.

**Nombres clave.**

- `void cv_init(struct cv *cv, const char *desc);`
- `void cv_destroy(struct cv *cv);`
- `cv_wait(cv, mtx)`, `cv_wait_sig(cv, mtx)`, `cv_wait_unlock(cv, mtx)`.
- `cv_timedwait(cv, mtx, timo)`, `cv_timedwait_sig(cv, mtx, timo)`.
- `cv_signal(cv)`, `cv_broadcast(cv)`, `cv_broadcastpri(cv, pri)`.

**Cabecera.** `/usr/src/sys/sys/condvar.h`.

**Advertencias.**

- El mutex que se pasa a `cv_wait` debe estar en posesión del llamador; `cv_wait` lo libera mientras duerme y lo vuelve a adquirir al retornar.
- Comprueba siempre el predicado de nuevo después de que `cv_wait` retorne. Los wakeups espurios y las señales son posibles.
- `cv_signal` despierta a un único esperador; `cv_broadcast` despierta a todos. Elige en función del diseño, no por instinto.

**Fase del ciclo de vida.** `cv_init` en attach; `cv_destroy` en detach.

**Página de manual.** `condvar(9)`.

**Dónde lo enseña el libro.** Capítulo 12, con las esperas interrumpibles y con tiempo de espera máximo revisadas en el Capítulo 15.

### `sema(9)`: Semáforos de conteo

**Propósito.** Un semáforo de conteo con operaciones `wait` y `post`. Menos común que los mutexes o las variables de condición.

**Uso típico en drivers.** Patrones productor-consumidor donde es necesario rastrear un recurso contado, como un conjunto fijo de ranuras de comandos.

**Nombres clave.**

- `void sema_init(struct sema *sema, int value, const char *desc);`
- `void sema_destroy(struct sema *sema);`
- `sema_wait(sema)`, `sema_trywait(sema)`, `sema_timedwait(sema, timo)`.
- `sema_post(sema)`.

**Cabecera.** `/usr/src/sys/sys/sema.h`.

**Advertencias.**

- Los semáforos son apropiados para el conteo. Para patrones de un único thread en sección crítica, usa `mtx`.
- `sema_wait` puede retornar anticipadamente ante una señal; comprueba los valores de retorno.

**Página de manual.** `sema(9)`.

**Dónde lo enseña el libro.** Capítulo 15, como parte del conjunto de herramientas avanzadas de sincronización.

### `atomic(9)`: Operaciones atómicas

**Propósito.** Operaciones de lectura-modificación-escritura sobre una sola palabra, sin posibilidad de interrupción. Más rápidas que cualquier lock, y estrictamente limitadas en expresividad.

**Uso típico en drivers.** Contadores, flags y patrones de comparación e intercambio donde la sección crítica cabe en un único entero.

**Nombres clave.**

- `atomic_add_int`, `atomic_subtract_int`, `atomic_set_int`, `atomic_clear_int`.
- `atomic_load_int`, `atomic_store_int`, con variantes de adquisición y liberación.
- `atomic_cmpset_int`, `atomic_fcmpset_int` para comparación e intercambio.
- Variantes de anchura: `_8`, `_16`, `_32`, `_64` y `_ptr` para tamaño de puntero.
- Helpers de barrera: `atomic_thread_fence_acq()`, `atomic_thread_fence_rel()`, `atomic_thread_fence_acq_rel()`.

**Cabecera.** `/usr/src/sys/sys/atomic_common.h` más `machine/atomic.h` para las piezas específicas de arquitectura.

**Advertencias.**

- Las operaciones atómicas te dan exclusión mutua sobre una sola palabra. Cualquier invariante que abarque dos campos sigue necesitando un lock.
- El orden de memoria importa. Las operaciones simples son relajadas; usa las variantes `_acq`, `_rel` y `_acq_rel` cuando un acceso deba hacerse visible antes o después que otro.
- Para contadores por CPU que se leen raramente, `counter(9)` escala mejor.

**Fase del ciclo de vida.** Cualquiera. Suficientemente baratas para usarlas en filtros de interrupción.

**Página de manual.** `atomic(9)`.

**Dónde lo enseña el libro.** Capítulo 11, con `counter(9)` presentado junto a ellas para patrones por CPU.

### `epoch(9)`: Secciones de lectura mayoritaria sin locks

**Propósito.** Protección ligera de lectores para estructuras de datos donde los lectores superan ampliamente a los escritores y la latencia debe ser mínima. Los escritores esperan a que todos los lectores actuales abandonen antes de liberar memoria.

**Uso típico en drivers.** Rutas rápidas de la pila de red, tablas de búsqueda de lectura mayoritaria en drivers de alto rendimiento. No es una primitiva de uso general.

**Nombres clave.**

- `epoch_t epoch_alloc(const char *name, int flags);`
- `void epoch_free(epoch_t epoch);`
- `epoch_enter(epoch)`, `epoch_exit(epoch)`.
- `epoch_wait(epoch)` para que los escritores se bloqueen hasta que los lectores terminen.
- Envoltorios `NET_EPOCH_ENTER(et)` y `NET_EPOCH_EXIT(et)` para la pila de red.

**Cabecera.** `/usr/src/sys/sys/epoch.h`.

**Advertencias.**

- Los lectores no deben bloquearse, dormir ni llamar a ninguna función que lo haga mientras están dentro de una sección epoch.
- La liberación de memoria protegida debe diferirse hasta que `epoch_wait` retorne.
- Las secciones epoch son un recurso de último recurso, no una primitiva por defecto. Elige locks primero.

**Página de manual.** `epoch(9)`.

**Dónde lo enseña el libro.** Se presenta brevemente en el Capítulo 12; se usa en profundidad solo cuando los drivers reales en capítulos posteriores lo requieren.

### Guía rápida para elegir el lock

| Lo que quieres hacer...                                               | Recurre a                  |
| :-------------------------------------------------------------------- | :------------------------- |
| Proteger una sección crítica breve que no duerme                      | `mtx(9)` con `MTX_DEF`     |
| Proteger estado en un filtro de interrupción                          | `mtx(9)` con `MTX_SPIN`    |
| Permitir muchos lectores, escritores raros, puede dormir              | `sx(9)`                    |
| Permitir muchos lectores, escritores raros, sin dormir en lectores    | `rmlock(9)`                |
| Dormir hasta que una condición con nombre se cumpla                   | `cv(9)` con un mutex       |
| Incrementar o comparar e intercambiar una sola palabra                | `atomic(9)`                |
| Ruta de lectores sin locks para datos de lectura mayoritaria          | `epoch(9)`                 |

Cuando ninguna fila encaje claramente con el problema, la discusión completa en los Capítulos 11, 12 y 15 es el lugar donde resolverlo.

## Ejecución diferida y temporizadores

Los drivers a menudo necesitan ejecutar trabajo más tarde, de forma periódica o desde un contexto que pueda dormir. El kernel te ofrece tres herramientas para ello: `callout(9)` para temporizadores de un único disparo y periódicos, `taskqueue(9)` para trabajo diferido que puede dormir, y `kthread(9)` o `kproc(9)` para threads de fondo de larga duración. Se solapan en algunas situaciones; la regla general es que los callouts se ejecutan desde el contexto de la interrupción del temporizador (rápidos, sin posibilidad de dormir), las taskqueues se ejecutan en un thread trabajador (pueden dormir, pueden adquirir locks que bloquean) y los kthreads son threads completos de los que eres responsable.

### `callout(9)`: Temporizadores del kernel

**Propósito.** Planificar la ejecución de una función tras un retardo de tiempo. El callback se ejecuta por defecto en contexto de soft-interrupt y no debe dormir.

**Uso típico en drivers.** Temporizadores de watchdog, intervalos de sondeo, retardos en reintentos, tiempos de espera por inactividad.

**Nombres clave.**

- `void callout_init(struct callout *c, int mpsafe);` más `callout_init_mtx` y `callout_init_rm`.
- `int callout_reset(struct callout *c, int ticks, void (*func)(void *), void *arg);`
- `int callout_stop(struct callout *c);`
- `int callout_drain(struct callout *c);`
- `int callout_pending(struct callout *c);`, `callout_active(struct callout *c);`

**Cabecera.** `/usr/src/sys/sys/callout.h`.

**Advertencias.**

- `callout_stop` no espera a que finalice un callback en ejecución. Usa `callout_drain` antes de liberar el softc en `detach`.
- Un callout puede dispararse incluso después de creer que lo has cancelado, si el temporizador ya había sido enviado. Protege el callback con un flag o usa las variantes `_mtx` y `_rm` para integrar la cancelación con tu lock.
- Ejecutar kernels tickless significa que los ticks son abstractos. Convierte el tiempo real con `hz` o usa `callout_reset_sbt` para precisión subsegundo.

**Fase del ciclo de vida.** `callout_init` en `attach`; `callout_drain` en `detach`; `callout_reset` cada vez que sea necesario establecer el próximo momento de disparo.

**Página de manual.** `callout(9)`.

**Dónde enseña esto el libro.** Capítulo 13.

### `taskqueue(9)`: Trabajo diferido en un thread trabajador

**Propósito.** Delegar trabajo desde un contexto que no puede dormir, o que no debería mantener un lock durante mucho tiempo, a un thread trabajador. Las tareas encoladas en el mismo taskqueue se ejecutan en orden.

**Uso típico en drivers.** Posprocesamiento de interrupciones, manejadores de finalización de comandos de hardware, rutas de reinicio y recuperación que pueden necesitar asignar memoria o adquirir locks que permiten dormir.

**Nombres clave.**

- `struct taskqueue *taskqueue_create(const char *name, int mflags, taskqueue_enqueue_fn, void *context);`
- `void taskqueue_free(struct taskqueue *queue);`
- `TASK_INIT(struct task *t, int priority, task_fn_t *func, void *context);`
- `int taskqueue_enqueue(struct taskqueue *queue, struct task *task);`
- `void taskqueue_drain(struct taskqueue *queue, struct task *task);`
- `void taskqueue_drain_all(struct taskqueue *queue);`
- Colas globales como `taskqueue_thread`, `taskqueue_swi`, `taskqueue_fast`.

**Cabecera.** `/usr/src/sys/sys/taskqueue.h`.

**Advertencias.**

- Encolar la misma tarea dos veces antes de que se ejecute es un no-op por diseño. Si necesitas una nueva solicitud cada vez, está bien; si esperas dos ejecuciones, usa tareas distintas.
- `taskqueue_drain` espera a que la tarea finalice; llámalo antes de liberar cualquier recurso que utilice la tarea.
- Los taskqueues privados son baratos pero no gratuitos. Reutiliza las colas globales (`taskqueue_thread`, `taskqueue_fast`) a menos que tengas una razón para tener una propia.

**Fase del ciclo de vida.** `taskqueue_create` (si es privado) y `TASK_INIT` en `attach`; `taskqueue_drain` y `taskqueue_free` en `detach`.

**Página de manual.** `taskqueue(9)`.

**Dónde lo explica el libro.** Capítulo 14.

### `kthread(9)` y `kproc(9)`: Threads y procesos del kernel

**Propósito.** Crear un thread o proceso del kernel dedicado que ejecute tu función. Resulta útil cuando la carga de trabajo es de larga duración, necesita su propia política de planificación o debe ser explícitamente direccionable.

**Uso típico en drivers.** Poco frecuente. La mayor parte del trabajo en drivers se gestiona mejor con un taskqueue o un callout. Los threads del kernel aparecen en subsistemas con bucles verdaderamente prolongados, como daemons de mantenimiento.

**Nombres clave.**

- `int kthread_add(void (*func)(void *), void *arg, struct proc *p, struct thread **td, int flags, int pages, const char *fmt, ...);`
- `int kproc_create(void (*func)(void *), void *arg, struct proc **procp, int flags, int pages, const char *fmt, ...);`
- `void kthread_exit(void);`
- `kproc_exit`, `kproc_suspend_check`.

**Cabecera.** `/usr/src/sys/sys/kthread.h`.

**Advertencias.**

- Crear un thread es más costoso que encolar una tarea. Prefiere `taskqueue(9)` a menos que la carga de trabajo sea genuinamente prolongada.
- Detener limpiamente un kthread requiere cooperación: establece un indicador de parada, despierta el thread y espera a que finalice. Olvidar cualquier paso provoca una fuga del thread al descargar el módulo.
- Un kthread debe salir llamando a `kthread_exit`, no retornando.

**Páginas de manual.** `kthread(9)`, `kproc(9)`.

**Dónde lo explica el libro.** Se menciona en el Capítulo 14 como la alternativa más pesada a los taskqueues.

### Tabla de referencia rápida para trabajo diferido

| Necesitas...                                                       | Utiliza              |
| :----------------------------------------------------------------- | :------------------- |
| Ejecutar una función tras un retardo, brevemente, sin dormir       | `callout(9)`         |
| Diferir trabajo que puede dormir o adquirir locks con sleep        | `taskqueue(9)`       |
| Ejecutar un bucle de fondo persistente                             | `kthread(9)`         |
| Convertir sondeos periódicos cortos en interrupciones reales       | Véase el Capítulo 19 |

## Gestión del bus y de recursos

La capa del bus es donde un driver se encuentra con el hardware. Newbus presenta el driver al kernel; `rman(9)` distribuye los recursos que representan regiones MMIO, puertos de I/O e interrupciones; `bus_space(9)` accede a ellos de forma portable; `bus_dma(9)` permite que los dispositivos realicen DMA de manera segura.

### Newbus: `DRIVER_MODULE`, `DEVMETHOD` y macros relacionadas

**Propósito.** Registrar un driver en el kernel, vincularlo a una clase de dispositivo, declarar los puntos de entrada que el kernel debe llamar y publicar información de versión y dependencias.

**Uso típico en drivers.** Cualquier módulo del kernel que gestione un dispositivo. Este es el andamiaje que convierte un conjunto de código C en algo que `kldload` puede vincular al hardware.

**Nombres clave.**

- `DRIVER_MODULE(name, bus, driver, devclass, evh, evharg);`
- `MODULE_VERSION(name, version);`
- `MODULE_DEPEND(name, busname, vmin, vpref, vmax);`
- `DEVMETHOD(method, function)` y `DEVMETHOD_END` para la tabla de métodos.
- Entradas de `device_method_t` como `device_probe`, `device_attach`, `device_detach`, `device_shutdown`, `device_suspend`, `device_resume`.
- Tipos: `device_t`, `devclass_t`, `driver_t`.

**Cabeceras.** `/usr/src/sys/sys/module.h` y `/usr/src/sys/sys/bus.h`.

**Advertencias.**

- `DRIVER_MODULE` se expande en un manejador de eventos de módulo; no declares tu propia tabla `module_event_t` a mano a menos que sepas exactamente por qué.
- `MODULE_DEPEND` es la forma de indicar al cargador que traiga tus prerrequisitos. Olvidarlo produce desagradables fallos de resolución de símbolos en el momento de carga.
- `DEVMETHOD_END` termina la tabla de métodos. Sin él, el kernel sobrepasará el final de la tabla.
- `device_t` es opaco; usa los accesores como `device_get_softc`, `device_get_parent`, `device_get_name` y `device_printf`.

**Fase del ciclo de vida.** Solo declaración. Las macros se expanden en el código de inicialización y finalización del módulo que se ejecuta al hacer `kldload` y `kldunload`.

**Páginas de manual.** `DRIVER_MODULE(9)`, `MODULE_VERSION(9)`, `MODULE_DEPEND(9)`, `module(9)`, `DEVICE_PROBE(9)`, `DEVICE_ATTACH(9)`, `DEVICE_DETACH(9)`.

**Dónde lo explica el libro.** Tratamiento completo en el Capítulo 7, con la anatomía esbozada por primera vez en el Capítulo 6.

### `devclass(9)` y accesores de dispositivo

**Propósito.** Un `devclass_t` agrupa instancias del mismo driver para que el kernel pueda encontrarlas, numerarlas e iterar sobre ellas. En los drivers, se utilizan principalmente los accesores, no el devclass directamente.

**Nombres clave.**

- `device_t device_get_parent(device_t dev);`
- `void *device_get_softc(device_t dev);`
- `int device_get_unit(device_t dev);`
- `const char *device_get_nameunit(device_t dev);`
- `int device_printf(device_t dev, const char *fmt, ...);`
- `devclass_find`, `devclass_get_device`, `devclass_get_devices`, `devclass_get_count` cuando realmente necesitas recorrer una clase.

**Cabecera.** `/usr/src/sys/sys/bus.h`.

**Advertencias.**

- `device_get_softc` asume que el softc fue registrado a través de la estructura del driver. Crear tu propio mapeo de `device_t` a estado casi siempre es un error.
- La manipulación directa del devclass es poco frecuente en los drivers. Si te encuentras recurriendo a ella, comprueba si la cuestión pertenece a una interfaz de nivel de bus.

**Páginas de manual.** `devclass(9)`, `device(9)`, `device_get_softc(9)`, `device_printf(9)`.

**Dónde lo explica el libro.** Capítulo 6 y Capítulo 7.

### `rman(9)`: El gestor de recursos

**Propósito.** Una vista uniforme sobre regiones MMIO, puertos de I/O, números de interrupción y canales DMA. Tu driver solicita recursos por tipo y RID y recibe un `struct resource *` con accesores útiles.

**Nombres clave.**

- `struct resource *bus_alloc_resource(device_t dev, int type, int *rid, rman_res_t start, rman_res_t end, rman_res_t count, u_int flags);`
- `struct resource *bus_alloc_resource_any(device_t dev, int type, int *rid, u_int flags);`
- `int bus_release_resource(device_t dev, int type, int rid, struct resource *r);`
- `int bus_activate_resource(device_t dev, int type, int rid, struct resource *r);`
- `int bus_deactivate_resource(device_t dev, int type, int rid, struct resource *r);`
- `rman_res_t rman_get_start(struct resource *r);`, `rman_get_end`, `rman_get_size`.
- `bus_space_tag_t rman_get_bustag(struct resource *r);`, `rman_get_bushandle`.
- Tipos de recurso: `SYS_RES_MEMORY`, `SYS_RES_IOPORT`, `SYS_RES_IRQ`, `SYS_RES_DRQ`.
- Flags: `RF_ACTIVE`, `RF_SHAREABLE`.

**Cabecera.** `/usr/src/sys/sys/rman.h`.

**Advertencias.**

- El parámetro `rid` es un puntero y puede ser reescrito por el asignador. Pasa la dirección de una variable real.
- Libera cada recurso asignado en `detach` en orden inverso a la asignación. Una fuga de recursos casi siempre corrompe el siguiente attach.
- `RF_ACTIVE` es el caso habitual. No lo olvides, o obtendrás un handle que no se puede usar con `bus_space(9)`.
- Comprueba siempre el valor de retorno. Una asignación fallida es frecuente en hardware con peculiaridades.

**Fase del ciclo de vida.** Asignación en `attach`; liberación en `detach`. Si el driver tiene necesidades especiales, `bus_activate_resource` y `bus_deactivate_resource` pueden gestionar la activación por separado.

**Páginas de manual.** `rman(9)`, `bus_alloc_resource(9)`, `bus_release_resource(9)`, `bus_activate_resource(9)`.

**Dónde lo explica el libro.** Capítulo 16.

### `bus_space(9)`: Acceso portable a registros

**Propósito.** Leer y escribir registros de dispositivo a través de una tripleta `(tag, handle, offset)` que oculta si el acceso subyacente es mapeado en memoria, basado en puertos, big-endian, little-endian o indexado.

**Uso típico en drivers.** Cualquier acceso MMIO o de puerto de I/O. No desreferencies `rman_get_virtual` directamente; usa `bus_space`.

**Nombres clave.**

- Tipos: `bus_space_tag_t`, `bus_space_handle_t`.
- Lecturas: `bus_space_read_1(tag, handle, offset)`, `_2`, `_4`, `_8`.
- Escrituras: `bus_space_write_1(tag, handle, offset, value)`, `_2`, `_4`, `_8`.
- Ayudantes multiregistro: `bus_space_read_multi_N`, `bus_space_write_multi_N`, `bus_space_read_region_N`, `bus_space_write_region_N`.
- Barreras: `bus_space_barrier(tag, handle, offset, length, flags)` con `BUS_SPACE_BARRIER_READ` y `BUS_SPACE_BARRIER_WRITE`.

**Cabecera.** `/usr/src/sys/sys/bus.h`, con detalles específicos de la arquitectura en `machine/bus.h`.

**Advertencias.**

- Nunca accedas a los registros del dispositivo mediante un puntero directo. Tanto la portabilidad como la depuración dependen de `bus_space`.
- Las barreras no son automáticas. Cuando dos escrituras deben producirse en orden, inserta `bus_space_barrier` entre ellas.
- El ancho utilizado en `bus_space_read_N` o `bus_space_write_N` debe coincidir con el tamaño natural del registro. Las discrepancias provocan corrupción silenciosa en algunas arquitecturas.

**Fase del ciclo de vida.** En cualquier momento en que el driver se comunique con el dispositivo.

**Página de manual.** `bus_space(9)`.

**Dónde lo explica el libro.** Capítulo 16.

### `bus_dma(9)`: DMA portable

**Propósito.** Describir las restricciones de DMA con un tag, cargar un buffer a través de un mapa y dejar que el framework gestione la alineación, el rebotado y la coherencia. Imprescindible para cualquier dispositivo serio que transfiera datos.

**Nombres clave.**

- `int bus_dma_tag_create(bus_dma_tag_t parent, bus_size_t alignment, bus_addr_t boundary, bus_addr_t lowaddr, bus_addr_t highaddr, bus_dma_filter_t *filtfunc, void *filtfuncarg, bus_size_t maxsize, int nsegments, bus_size_t maxsegsz, int flags, bus_dma_lock_t *lockfunc, void *lockfuncarg, bus_dma_tag_t *dmat);`
- `int bus_dma_tag_destroy(bus_dma_tag_t dmat);`
- `int bus_dmamap_create(bus_dma_tag_t dmat, int flags, bus_dmamap_t *mapp);`
- `int bus_dmamap_destroy(bus_dma_tag_t dmat, bus_dmamap_t map);`
- `int bus_dmamap_load(bus_dma_tag_t dmat, bus_dmamap_t map, void *buf, bus_size_t buflen, bus_dmamap_callback_t *callback, void *arg, int flags);`
- `void bus_dmamap_unload(bus_dma_tag_t dmat, bus_dmamap_t map);`
- `void bus_dmamap_sync(bus_dma_tag_t dmat, bus_dmamap_t map, bus_dmasync_op_t op);`
- `int bus_dmamem_alloc(bus_dma_tag_t dmat, void **vaddr, int flags, bus_dmamap_t *mapp);`
- `void bus_dmamem_free(bus_dma_tag_t dmat, void *vaddr, bus_dmamap_t map);`
- Flags: `BUS_DMA_WAITOK`, `BUS_DMA_NOWAIT`, `BUS_DMA_ALLOCNOW`, `BUS_DMA_COHERENT`, `BUS_DMA_ZERO`.
- Operaciones de sincronización: `BUS_DMASYNC_PREREAD`, `BUS_DMASYNC_POSTREAD`, `BUS_DMASYNC_PREWRITE`, `BUS_DMASYNC_POSTWRITE`.

**Cabecera.** `/usr/src/sys/sys/bus_dma.h`.

**Advertencias.**

- Las tags forman un árbol. Las tags hijas heredan las restricciones de las tags padre; créalas en el orden correcto.
- `bus_dmamap_load` puede completarse de forma asíncrona. Usa siempre el callback, incluso para buffers síncronos.
- `bus_dmamap_sync` no es decoración. Sin la dirección de sincronización correcta, las cachés y la memoria del dispositivo discrepan.
- En plataformas con IOMMUs, el framework hace lo correcto. No lo omitas solo porque tu hardware de desarrollo sea coherente.

**Fase del ciclo de vida.** Creación de tags y configuración del mapping en `attach`; carga y sincronización en las rutas de I/O; descarga y destrucción en `detach`.

**Página de manual.** `bus_dma(9)`.

**Dónde lo explica el libro.** Capítulo 21.

### Configuración de interrupciones

**Propósito.** Asociar un filtro o manejador a un recurso IRQ para que el kernel pueda entregar interrupciones al driver.

**Nombres clave.**

- `int bus_setup_intr(device_t dev, struct resource *r, int flags, driver_filter_t *filter, driver_intr_t *handler, void *arg, void **cookiep);`
- `int bus_teardown_intr(device_t dev, struct resource *r, void *cookie);`
- Flags: `INTR_TYPE_NET`, `INTR_TYPE_BIO`, `INTR_TYPE_TTY`, `INTR_TYPE_MISC`, `INTR_MPSAFE`, `INTR_EXCL`.

**Advertencias.**

- Proporciona un filtro cuando la decisión de la ruta rápida tiene un coste bajo y el driver puede respetar las restricciones del contexto del filtro (sin dormir, sin locks durmientes). Proporciona un manejador cuando el trabajo requiere un thread.
- `INTR_MPSAFE` es obligatorio para los drivers nuevos. Sin él, el kernel serializa el manejador en el Giant lock, lo que casi siempre es incorrecto.
- Desmonta siempre antes de liberar el recurso. El orden es: `bus_teardown_intr`, seguido de `bus_release_resource`.

**Fase del ciclo de vida.** `bus_setup_intr` al final de `attach`, una vez que el resto del softc está listo; `bus_teardown_intr` al inicio de `detach`, antes de que se libere cualquier recurso.

**Página del manual.** `BUS_SETUP_INTR(9)`, `bus_alloc_resource(9)` para la parte del recurso.

**Dónde lo explica el libro.** El capítulo 19, con patrones avanzados en el capítulo 20.

## Nodos de dispositivo e I/O de dispositivos de caracteres

Una vez que el hardware está vinculado, los drivers se exponen habitualmente al espacio de usuario a través de un nodo de dispositivo en `/dev/`. Las API que se presentan a continuación crean y destruyen esos nodos, y la tabla de despacho asociada declara cómo debe el kernel distribuir las llamadas `read`, `write`, `ioctl` y `poll`.

### `make_dev_s(9)` y `destroy_dev(9)`

**Propósito.** Crear un nuevo nodo de dispositivo bajo `/dev/`, conectado a un `cdevsw` que contiene los punteros a función que invocará el kernel.

**Nombres clave.**

- `int make_dev_s(struct make_dev_args *args, struct cdev **cdev, const char *fmt, ...);`
- `void destroy_dev(struct cdev *cdev);`
- Campos de `struct make_dev_args`: `mda_si_drv1`, `mda_devsw`, `mda_uid`, `mda_gid`, `mda_mode`, `mda_flags`, `mda_unit`.
- La función heredada `make_dev(struct cdevsw *, int unit, uid_t, gid_t, int mode, const char *fmt, ...)` todavía existe, pero `make_dev_s` es preferible en código nuevo.

**Encabezado.** `/usr/src/sys/sys/conf.h`.

**Advertencias.**

- Utiliza siempre `make_dev_s`. La función más antigua `make_dev` descarta los errores y no permite establecer todos los argumentos.
- Establece `mda_si_drv1` apuntando al softc, de modo que el cdev almacene un puntero de vuelta al estado del driver sin necesidad de una búsqueda adicional.
- `destroy_dev` espera a que todos los threads activos abandonen el cdev antes de retornar, por lo que es seguro liberar el softc a continuación.

**Fase del ciclo de vida.** `make_dev_s` al final de `attach`; `destroy_dev` al inicio de `detach`, antes de desmontar cualquier estado subyacente.

**Página del manual.** `make_dev(9)`.

**Dónde lo explica el libro.** El capítulo 8.

### `cdevsw`: tabla de despacho de dispositivos de caracteres

**Propósito.** Declarar los puntos de entrada que el kernel debe invocar cuando un proceso abre, lee, escribe o interactúa de cualquier otro modo con el nodo de dispositivo.

**Nombres clave.**

- Campos de `struct cdevsw`: `d_version`, `d_flags`, `d_name`, `d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl`, `d_poll`, `d_kqfilter`, `d_mmap`, `d_mmap_single`.
- `d_version` debe ser `D_VERSION`.
- Flags habituales: `D_NEEDGIANT` (heredado), `D_TRACKCLOSE`, `D_MEM`.

**Encabezado.** `/usr/src/sys/sys/conf.h`.

**Advertencias.**

- Establece siempre `d_version = D_VERSION`. El kernel rechaza asociar una tabla de despacho con una versión ausente o desactualizada.
- El valor predeterminado de cero para `d_flags` es adecuado en drivers modernos con soporte MPSAFE. No añadas `D_NEEDGIANT` a menos que lo necesites de verdad.
- Las entradas no utilizadas pueden dejarse como `NULL`; el kernel sustituye los valores predeterminados. No las apuntes a stubs que no hacen nada.

**Fase del ciclo de vida.** Se declara estáticamente en el ámbito del módulo. `struct make_dev_args` hace referencia a ella.

**Página del manual.** `make_dev(9)` describe la estructura en el contexto de `make_dev_s`.

**Dónde lo explica el libro.** El capítulo 8.

### `ioctl(9)`: despacho

**Propósito.** Proporcionar comandos fuera de banda a un dispositivo, identificados por un código de comando numérico y un buffer de argumento.

**Nombres clave.**

- Punto de entrada: `d_ioctl_t` con la firma `int (*)(struct cdev *, u_long cmd, caddr_t data, int fflag, struct thread *td);`
- Macros de codificación de comandos: `_IO`, `_IOR`, `_IOW`, `_IOWR`.
- Funciones de copia: `copyin(9)` y `copyout(9)` para ioctls que transportan punteros.

**Advertencias.**

- Utiliza `_IOR`, `_IOW` o `_IOWR` para declarar los comandos. Estos macros codifican el tamaño y la dirección, lo que es importante para la compatibilidad entre arquitecturas.
- Valida los argumentos del comando antes de actuar sobre ellos. Un ioctl es un límite de confianza.
- No desreferencies nunca directamente un puntero de usuario. Usa `copyin(9)` y `copyout(9)`.

**Fase del ciclo de vida.** Operación normal.

**Página del manual.** `ioctl(9)` (conceptual); el punto de entrada está documentado junto a `cdevsw` en `make_dev(9)`.

**Dónde lo explica el libro.** El capítulo 24 (sección 3), con más patrones en el capítulo 25.

### `devfs_set_cdevpriv(9)`: estado por apertura

**Propósito.** Asociar un puntero privado del driver a un descriptor de archivo abierto. El puntero se libera mediante un callback cuando se produce el último cierre.

**Nombres clave.**

- `int devfs_set_cdevpriv(void *priv, d_priv_dtor_t *dtor);`
- `int devfs_get_cdevpriv(void **datap);`
- `void devfs_clear_cdevpriv(void);`

**Encabezado.** `/usr/src/sys/sys/conf.h`.

**Advertencias.**

- El estado por apertura es la herramienta adecuada para ajustes por descriptor, cursores o transacciones pendientes. No almacenes el estado por apertura en el softc.
- El destructor se ejecuta en el contexto del último cierre. Mantenlo breve y no bloqueante.

**Página del manual.** `devfs_set_cdevpriv(9)`.

**Dónde lo explica el libro.** El capítulo 8.

## Interacción con los procesos y el espacio de usuario

No se puede confiar en el espacio de usuario con las direcciones del kernel, y el kernel no puede seguir punteros del espacio de usuario sin la debida precaución. Las API que se presentan a continuación cruzan ese límite de confianza de forma segura.

### `copyin(9)`, `copyout(9)`, `copyinstr(9)`

**Propósito.** Mover bytes entre el espacio de direcciones del kernel y el del usuario con validación de direcciones. Estas son las únicas formas seguras de acceder a un puntero de usuario desde código del kernel.

**Nombres clave.**

- `int copyin(const void *uaddr, void *kaddr, size_t len);`
- `int copyout(const void *kaddr, void *uaddr, size_t len);`
- `int copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done);`
- Relacionadas: `fueword`, `fuword`, `subyte`, `suword`, documentadas en `fetch(9)` y `store(9)`.

**Encabezado.** `/usr/src/sys/sys/systm.h`.

**Advertencias.**

- Las tres pueden dormir. No las llames mientras mantienes un mutex no durmiente.
- Devuelven `EFAULT` ante una dirección incorrecta, no cero. Comprueba siempre el valor de retorno.
- `copyinstr` distingue la truncación del éxito mediante su argumento `done`; no lo ignores.

**Fase del ciclo de vida.** `d_ioctl`, `d_read`, `d_write`, y cualquier otro lugar donde el espacio de usuario sea el origen o el destino.

**Páginas del manual.** `copy(9)`, `fetch(9)`, `store(9)`.

**Dónde lo explica el libro.** El capítulo 9.

### `uio(9)`: descriptor de I/O para lectura y escritura

**Propósito.** La descripción interna del kernel de una solicitud de I/O. Oculta la diferencia entre buffers de usuario y del kernel, entre transferencias scatter-gather y contiguas, y entre las direcciones de lectura y escritura.

**Nombres clave.**

- `int uiomove(void *cp, int n, struct uio *uio);`
- `int uiomove_nofault(void *cp, int n, struct uio *uio);`
- Campos de `struct uio`: `uio_iov`, `uio_iovcnt`, `uio_offset`, `uio_resid`, `uio_segflg`, `uio_rw`, `uio_td`.
- Indicadores de segmento: `UIO_USERSPACE`, `UIO_SYSSPACE`, `UIO_NOCOPY`.
- Dirección: `UIO_READ`, `UIO_WRITE`.

**Encabezado.** `/usr/src/sys/sys/uio.h`.

**Advertencias.**

- Utiliza `uiomove` en los puntos de entrada `d_read` y `d_write`. Es la herramienta adecuada incluso cuando el buffer del espacio de usuario es una región contigua simple.
- `uiomove` puede dormir. Suelta los mutexes no durmientes antes de llamarlo.
- Tras el retorno de `uiomove`, `uio_resid` ha sido actualizado. No mantengas tu propio contador de bytes en paralelo; léelo desde `uio_resid`.

**Fase del ciclo de vida.** I/O normal.

**Página del manual.** `uio(9)`.

**Dónde lo explica el libro.** El capítulo 9.

### `proc(9)` y contexto de thread para drivers

**Propósito.** Acceso al thread que realiza la llamada y a su proceso, principalmente para comprobaciones de credenciales, estado de señales e impresión de diagnóstico.

**Nombres clave.**

- `curthread`, `curproc`, `curthread->td_proc`.
- `struct ucred *cred = curthread->td_ucred;`
- `int priv_check(struct thread *td, int priv);`
- `pid_t pid = curproc->p_pid;`

**Encabezado.** `/usr/src/sys/sys/proc.h`.

**Advertencias.**

- El uso directo de los elementos internos del proceso es poco habitual. Cuando se necesita, suele ser para una comprobación de credenciales, que debería hacerse a través de `priv_check(9)`.
- No almacenes `curthread` para usarlo después de una suspensión. El thread que vuelva a entrar en el driver puede ser uno diferente.

**Página del manual.** No existe una página única; consulta `priv(9)` y `proc(9)`.

**Dónde lo explica el libro.** Se menciona en los capítulos 9 y 24, cuando los manejadores de ioctl necesitan credenciales.

## Observabilidad y notificaciones

Un driver que no puede observarse es un driver en el que no se puede confiar. El kernel ofrece varias formas de que el espacio de usuario inspeccione el estado del driver, se suscriba a eventos y espere a que el driver esté listo. Las API que se presentan a continuación son las más habituales.

### `sysctl(9)`: nodos de configuración de lectura y escritura

**Propósito.** Publicar el estado del driver y los parámetros configurables bajo un nombre jerárquico, de modo que herramientas como `sysctl(8)` y scripts de monitorización puedan leerlos o modificarlos.

**Nombres clave.**

- Declaraciones estáticas: `SYSCTL_NODE`, `SYSCTL_INT`, `SYSCTL_LONG`, `SYSCTL_STRING`, `SYSCTL_PROC`, `SYSCTL_OPAQUE`.
- API de contexto dinámico: `sysctl_ctx_init`, `sysctl_ctx_free`, `SYSCTL_ADD_NODE`, `SYSCTL_ADD_INT`, `SYSCTL_ADD_PROC`.
- Funciones auxiliares de manejador: `sysctl_handle_int`, `sysctl_handle_long`, `sysctl_handle_string`.
- Indicadores de acceso: `CTLFLAG_RD`, `CTLFLAG_RW`, `CTLFLAG_TUN`, `CTLFLAG_STATS`, `CTLTYPE_INT`, `CTLTYPE_STRING`.

**Encabezado.** `/usr/src/sys/sys/sysctl.h`.

**Advertencias.**

- Para todo lo que esté vinculado a una instancia de dispositivo concreta, utiliza la API dinámica. `device_get_sysctl_ctx` y `device_get_sysctl_tree` te proporcionan el contexto adecuado.
- Los manejadores se ejecutan en contexto de usuario. Pueden dormir y pueden fallar.
- Publica parámetros configurables con moderación. Cada parámetro es un contrato con los usuarios futuros.

**Fase del ciclo de vida.** Las declaraciones estáticas tienen alcance de módulo. Las dinámicas se crean en `attach` y se destruyen automáticamente en `detach` a través del contexto.

**Páginas del manual.** `sysctl(9)`, `sysctl_add_oid(9)`, `sysctl_ctx_init(9)`.

**Dónde lo explica el libro.** Se introduce en el capítulo 7, con un tratamiento más profundo en el capítulo 24 (sección 4), cuando el driver empieza a exponer métricas al espacio de usuario.

### `eventhandler(9)`: publicación-suscripción dentro del kernel

**Propósito.** Registrarse para eventos de todo el kernel, como el montaje, el desmontaje, la escasez de memoria o el apagado del sistema. El kernel invoca los callbacks registrados en respuesta a dichos eventos.

**Nombres clave.**

- `EVENTHANDLER_DECLARE(name, type_t);`
- `eventhandler_tag EVENTHANDLER_REGISTER(name, func, arg, priority);`
- `void EVENTHANDLER_DEREGISTER(name, tag);`
- `void EVENTHANDLER_INVOKE(name, ...);`
- Constantes de prioridad: `EVENTHANDLER_PRI_FIRST`, `EVENTHANDLER_PRI_ANY`, `EVENTHANDLER_PRI_LAST`.

**Encabezado.** `/usr/src/sys/sys/eventhandler.h`.

**Advertencias.**

- Los manejadores se ejecutan de forma síncrona. Mantenlos breves.
- Cancela siempre el registro antes de descargar el módulo. Un manejador colgante provocará un panic cuando se dispare el evento.

**Página del manual.** `EVENTHANDLER(9)`.

**Dónde lo explica el libro.** Se menciona en el capítulo 24, cuando los drivers se integran con notificaciones de todo el kernel, como el apagado del sistema y los eventos de escasez de memoria.

### `poll(2)` y `kqueue(2)`: notificación de disponibilidad

**Propósito.** Permitir que el espacio de usuario espere a que se produzcan eventos de disponibilidad gestionados por el driver. `poll(2)` es la interfaz más antigua; `kqueue(2)` es la moderna, con filtros más completos.

**Nombres clave.**

- Punto de entrada de `poll`: `int (*d_poll)(struct cdev *, int events, struct thread *);`
- Punto de entrada de `kqueue`: `int (*d_kqfilter)(struct cdev *, struct knote *);`
- Gestión de la lista de espera: `struct selinfo`, `selrecord(struct thread *td, struct selinfo *sip)`, `selwakeup(struct selinfo *sip)`.
- Soporte para kqueue: `struct knote`, `knote_enqueue`, `knlist_init_mtx`, `knlist_add`, `knlist_remove`.
- Bits de evento: `POLLIN`, `POLLOUT`, `POLLERR`, `POLLHUP` para `poll`; `EVFILT_READ`, `EVFILT_WRITE` para `kqueue`.

**Cabeceras.** `/usr/src/sys/sys/selinfo.h`, `/usr/src/sys/sys/event.h`, `/usr/src/sys/sys/poll.h`.

**Advertencias.**

- `d_poll` debe llamar a `selrecord` cuando no hay eventos listos, e informar de la disponibilidad actual cuando los haya.
- `selwakeup` debe invocarse sin ningún mutex tomado que pueda invertir el orden de bloqueo con el planificador. Este es un error de orden de locks muy habitual.
- El soporte para kqueue es más rico, pero también implica más código. Cuando el driver ya cuenta con un camino `poll` limpio, extenderlo a kqueue suele ser el paso siguiente más adecuado en lugar de una reescritura completa.

**Fase del ciclo de vida.** Configuración en `attach`; desmontaje en `detach`; despacho real en `d_poll` o `d_kqfilter`.

**Páginas de manual.** `selrecord(9)`, `kqueue(9)`, y las páginas de espacio de usuario `poll(2)` y `kqueue(2)`.

**Dónde lo enseña el libro.** El capítulo 10 presenta la integración con `poll(2)` de forma completa; `kqueue(2)` se menciona allí y se explora en profundidad en el capítulo 35.

## Diagnósticos, logging y tracing

La corrección de un driver no reside solo en el código. Reside en la capacidad de observar, verificar y rastrear. Las APIs que se describen a continuación son la forma en que logras que un driver diga la verdad sobre sí mismo.

### `log(9)` y `printf(9)`

**Propósito.** Emitir mensajes al log del kernel para que aparezcan en `dmesg` y en `/var/log/messages`.

**Nombres clave.**

- `void log(int level, const char *fmt, ...);`
- Familia `printf` estándar del kernel: `printf`, `vprintf`, `uprintf`, `tprintf`.
- Función auxiliar por dispositivo: `device_printf(device_t dev, const char *fmt, ...);`
- Constantes de prioridad de `syslog.h`: `LOG_EMERG`, `LOG_ALERT`, `LOG_CRIT`, `LOG_ERR`, `LOG_WARNING`, `LOG_NOTICE`, `LOG_INFO`, `LOG_DEBUG`.

**Cabeceras.** `/usr/src/sys/sys/systm.h`, `/usr/src/sys/sys/syslog.h`.

**Advertencias.**

- No registres mensajes con nivel `LOG_INFO` en la ruta rápida de I/O. Inunda la consola y oculta los problemas reales.
- `device_printf` antepone automáticamente el nombre del dispositivo, lo que facilita el filtrado de los logs. Prefiere esta función al `printf` directo.
- Emite un mensaje una sola vez por cada clase de evento distinta, no una vez por paquete.

**Fase del ciclo de vida.** Cualquiera.

**Páginas de manual.** `printf(9)`.

**Dónde lo enseña el libro.** Capítulo 23.

### `KASSERT(9)`: aserciones del kernel

**Propósito.** Declarar invariantes que deben cumplirse. Cuando el kernel se construye con `INVARIANTS`, una aserción violada genera un panic con un mensaje descriptivo. Sin `INVARIANTS`, la aserción se elimina en la compilación.

**Nombres clave.**

- `KASSERT(expression, (format, args...));`
- `MPASS(expression);` para aserciones más simples sin mensaje.
- `CTASSERT(expression);` para aserciones en tiempo de compilación sobre constantes.

**Cabecera.** `/usr/src/sys/sys/kassert.h`, incluida transitivamente por `/usr/src/sys/sys/systm.h`.

**Advertencias.**

- La expresión debe ser barata y libre de efectos secundarios. El compilador no la optimiza en tu lugar; tú escribes el invariante.
- El mensaje es una lista de argumentos de `printf` entre paréntesis. Incluye suficiente contexto para diagnosticar un fallo a partir del panic.
- Usa `KASSERT` para condiciones que indican un error del programador, no para condiciones normales de ejecución.

**Fase del ciclo de vida.** En cualquier punto donde sea necesario documentar y hacer cumplir un invariante.

**Página de manual.** `KASSERT(9)`.

**Dónde lo enseña el libro.** El capítulo 23 introduce `INVARIANTS` y el uso de aserciones; la sección 2 del capítulo 34 trata `KASSERT` y las macros de diagnóstico en profundidad.

### `WITNESS`: verificador de orden de locks

**Propósito.** Una opción del kernel que registra el orden en que cada thread adquiere los locks y advierte cuando un thread posterior invierte un orden observado anteriormente.

**Nombres clave.**

- Integrado en `mtx(9)`, `sx(9)`, `rm(9)` y las macros de locking. No se necesita ninguna llamada a API adicional.
- Opciones del kernel: `WITNESS`, `WITNESS_SKIPSPIN`, `WITNESS_COUNT`.
- Aserciones que cooperan con `WITNESS`: `mtx_assert`, `sx_assert`, `rm_assert`.

**Advertencias.**

- `WITNESS` es una opción de depuración. Construye un kernel de depuración para activarla; es demasiado costosa para producción.
- Las advertencias no son ruido. Si `WITNESS` se queja, hay un bug.
- Las advertencias de orden de locks hacen referencia a los nombres de lock pasados a `mtx_init`, `sx_init` y similares. Dale a cada lock un nombre significativo.

**Página de manual.** No existe una página única. Consulta `lock(9)` y `locking(9)`.

**Dónde lo enseña el libro.** Capítulo 12 (sección 6), con refuerzo en el capítulo 23.

### `ktr(9)`: mecanismo de tracing del kernel

**Propósito.** Un buffer circular de baja sobrecarga para el tracing de eventos dentro del kernel. Los registros de `ktr` se emiten mediante macros y se pueden volcar con `ktrdump(8)`.

**Nombres clave.**

- `CTR0(class, fmt)`, `CTR1(class, fmt, a1)`, hasta `CTR6` con un número creciente de argumentos.
- Clases de tracing: `KTR_GEN`, `KTR_NET`, `KTR_DEV` y muchas otras en `sys/ktr_class.h`.
- Opción del kernel: `KTR` con máscaras por clase.

**Cabecera.** `/usr/src/sys/sys/ktr.h`.

**Advertencias.**

- `ktr` debe estar habilitado en el momento de construir el kernel; comprueba si `KTR` aparece en la configuración.
- Cada registro es pequeño. No intentes registrar estructuras enteras.
- Para diagnósticos orientados al usuario, `dtrace(1)` suele ser una mejor opción.

**Página de manual.** `ktr(9)`.

**Dónde lo enseña el libro.** Capítulo 23.

### Sondas estáticas de DTrace y proveedores principales

**Propósito.** Infraestructura de tracing estático y dinámico que permite al espacio de usuario conectarse a puntos de sonda en el kernel en ejecución sin necesidad de recompilar.

**Nombres clave.**

- Tracing definido estáticamente: `SDT_PROVIDER_DECLARE`, `SDT_PROVIDER_DEFINE`, `SDT_PROBE_DECLARE`, `SDT_PROBE_DEFINE`, `SDT_PROBE`.
- Proveedores habituales en FreeBSD: `sched`, `proc`, `io`, `vfs`, `fbt` (function boundary tracing), `sdt`.
- Cabeceras: `/usr/src/sys/sys/sdt.h`, `/usr/src/sys/cddl/dev/dtrace/...`.

**Advertencias.**

- `fbt` no requiere ningún cambio en el driver, pero las sondas `sdt` ofrecen puntos con nombre estables que sobreviven a futuras refactorizaciones.
- Una sonda desactivada tiene un coste negligible. No te preocupes por añadir varias.
- Los scripts de DTrace son código del espacio de usuario; el driver solo define los puntos de sonda a los que los scripts pueden conectarse.

**Páginas de manual.** `SDT(9)`, `dtrace(1)`, `dtrace(8)`.

**Dónde lo enseña el libro.** Capítulo 23.

## Referencia cruzada por fase del ciclo de vida del driver

Las mismas APIs aparecen en distintas fases de la vida de un driver. La tabla siguiente es un índice inverso rápido: cuando estés escribiendo una fase concreta, aquí están las familias que suelen corresponderle.

### Carga del módulo

- `MODULE_VERSION`, `MODULE_DEPEND`, `DEV_MODULE` (si el módulo es un cdev puro).
- Declaraciones estáticas de `MALLOC_DEFINE`, `SYSCTL_NODE`, `SDT_PROVIDER_DEFINE`.
- Registro de manejadores de eventos que deben estar activos antes de que se conecte cualquier dispositivo.

### Probe

- `device_get_parent`, `device_get_nameunit`, `device_printf`.
- Valores de retorno: `BUS_PROBE_DEFAULT`, `BUS_PROBE_GENERIC`, `BUS_PROBE_SPECIFIC`, `BUS_PROBE_LOW_PRIORITY`, `ENXIO` si no hay coincidencia.

### Attach

- `device_get_softc`, `malloc(9)` para los campos del softc, etiquetas `MALLOC_DEFINE`.
- Inicialización de locks: `mtx_init`, `sx_init`, `rm_init`, `cv_init`, `sema_init`.
- Asignación de recursos: `bus_alloc_resource` o `bus_alloc_resource_any`.
- Configuración de `bus_space` mediante `rman_get_bustag` y `rman_get_bushandle`.
- Andamiaje DMA: `bus_dma_tag_create`, `bus_dmamap_create`.
- `callout_init`, `TASK_INIT`, creación de taskqueue cuando sea necesario.
- Configuración de interrupciones: `bus_setup_intr`.
- Creación del nodo de dispositivo: `make_dev_s`.
- Árbol sysctl: `device_get_sysctl_ctx`, `SYSCTL_ADD_*`.
- `uma_zcreate` para objetos de alta frecuencia.
- Registro de manejadores de eventos vinculados a este driver.

### Operación normal

- `d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl`, `d_poll`, `d_kqfilter`.
- `uiomove`, `copyin`, `copyout`, `copyinstr`.
- `bus_space_read_*`, `bus_space_write_*`, `bus_space_barrier`.
- `bus_dmamap_load`, `bus_dmamap_sync`, `bus_dmamap_unload`.
- Locking: `mtx_lock`, `mtx_unlock`, `sx_slock`, `sx_xlock`, `cv_wait`, `cv_signal`, `atomic_*`.
- Trabajo diferido: `callout_reset`, `taskqueue_enqueue`.
- Diagnósticos: `device_printf`, `log`, `KASSERT`, `SDT_PROBE`.

### Detach

- Desmontaje en orden inverso al de attach.
- `bus_teardown_intr` antes de liberar cualquier recurso.
- `destroy_dev` antes de desmontar los campos del softc a los que hace referencia.
- `callout_drain` antes de liberar la estructura callout.
- `taskqueue_drain_all` y `taskqueue_free` para taskqueues privados.
- `bus_dmamap_unload`, `bus_dmamap_destroy`, `bus_dma_tag_destroy`.
- `bus_release_resource` por cada recurso asignado en attach.
- `cv_destroy`, `sx_destroy`, `mtx_destroy`, `rm_destroy`, `sema_destroy`.
- `uma_zdestroy` por cada zona que posea el driver.
- Cancelación del registro de manejadores de eventos.
- `free` o `contigfree` final de todo lo que se haya asignado.

### Descarga del módulo

- Verificar que ninguna instancia de dispositivo siga conectada. Newbus suele encargarse de esto, pero los manejadores de eventos defensivos de `DRIVER_MODULE` deben rechazar la descarga si persiste algún estado.

## Listas de comprobación rápida

Estas listas están pensadas para leerse en cinco minutos o menos. No reemplazan la enseñanza de los capítulos; son un recordatorio de las cosas que los autores de drivers con experiencia ya no olvidan.

### Lista de comprobación de disciplina de locking

- Cada campo compartido en el softc tiene exactamente un lock que lo protege, documentado en un comentario junto al campo.
- No se mantiene ningún mutex durante `uiomove`, `copyin`, `copyout`, `malloc(9, M_WAITOK)` ni `bus_alloc_resource`.
- El orden de los locks está declarado en un comentario al principio del archivo y se respeta en todos los puntos.
- `mtx_assert` o `sx_assert` aparece en las funciones que requieren que un lock concreto esté adquirido en la entrada.
- `WITNESS` está habilitado en el kernel de desarrollo y sus advertencias se tratan como bugs.
- Cada `mtx_init` tiene un `mtx_destroy` correspondiente, y lo mismo para cada tipo de lock.

### Lista de comprobación de tiempo de vida de recursos

- `bus_setup_intr` es lo último en `attach`; `bus_teardown_intr` es lo primero en `detach`.
- Cada recurso asignado tiene una liberación correspondiente, en orden inverso, en `detach`.
- `callout_drain` se llama antes de liberar la estructura a la que apunta.
- `taskqueue_drain_all` o `taskqueue_drain` se llama antes de liberar las estructuras de tarea o sus argumentos.
- `destroy_dev` se llama antes de desmontar los campos del softc referenciados por `mda_si_drv1`.

### Lista de comprobación de seguridad en el espacio de usuario

- Ningún puntero de usuario se desreferencia directamente. Todo acceso entre contextos se realiza mediante `copyin`, `copyout`, `copyinstr` o `uiomove`.
- Todos los valores de retorno de las funciones auxiliares de copia se comprueban. `EFAULT` se propaga en lugar de ignorarse.
- `_IOR`, `_IOW` e `_IOWR` se usan para los números de comandos ioctl.
- Los manejadores de ioctl validan los argumentos antes de actuar sobre ellos.
- Las credenciales se comprueban con `priv_check(9)` cuando la operación requiere privilegios.

### Lista de comprobación de cobertura de diagnósticos

- Cada rama importante que nunca debería tomarse lleva un `KASSERT`.
- Los logs usan `device_printf` para el contexto de la instancia.
- Al menos una sonda DTrace SDT marca la entrada a las rutas principales de I/O.
- `sysctl` expone los contadores del driver en un árbol estable y documentado.
- El driver se ha construido y probado bajo `INVARIANTS` y `WITNESS` antes de considerarse terminado.

## Cerrando

Este apéndice es una referencia, no un capítulo. Se vuelve más útil cuanto más lo usas. Tenlo a mano mientras escribes, depuras o lees código de drivers, y acude a él siempre que necesites un recordatorio rápido sobre un flag, una página de manual o una advertencia que casi recuerdas.

Tres sugerencias para sacarle el máximo partido con el tiempo.

La primera: trata la línea de **Páginas de manual** como el siguiente paso canónico para cualquier API que recuerdes a medias. Las páginas de manual de la sección 9 se mantienen junto con el árbol de código fuente; envejecen bien. Abrir una de ellas no cuesta nada y siempre merece la pena.

La segunda: trata la línea de **Advertencias** como un compañero de depuración. La mayoría de los bugs en drivers no son incógnitas desconocidas. Son advertencias documentadas que el autor se saltó bajo presión de tiempo. Cuando estés atascado, lee las advertencias de cada API que toca el área problemática. No es glamuroso, pero funciona.

La tercera: cuando encuentres una entrada que falta o una corrección que hacer, anótala. Este apéndice mejora a medida que mejoran los drivers. El kernel de FreeBSD está vivo, y la referencia también. Si aparece un nuevo primitivo o se retira uno antiguo, el apéndice que refleja la realidad es aquel en el que confiarás de verdad.

Desde aquí puedes saltar en varias direcciones. El apéndice E cubre los aspectos internos de FreeBSD y el comportamiento de los subsistemas con una profundidad que esta referencia evita deliberadamente. El apéndice B recoge los algoritmos y patrones de programación de sistemas que reaparecen a lo largo del kernel. El apéndice C fundamenta los conceptos de hardware de los que dependen las familias de bus y DMA. Y cada capítulo del libro principal tiene código fuente que puedes leer, laboratorios que puedes ejecutar y preguntas que puedes responder abriendo `/usr/src/` y mirando la realidad.

El buen material de referencia es silencioso. Se mantiene al margen mientras trabajas y está ahí cuando lo necesitas. Ese es el papel que este apéndice está destinado a desempeñar durante el resto de tu vida escribiendo drivers con FreeBSD.
