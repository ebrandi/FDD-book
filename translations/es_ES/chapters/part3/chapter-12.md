---
title: "Mecanismos de sincronización"
description: "Nombrar los canales en los que esperas, tomar el lock una sola vez y leer desde múltiples threads, acotar los bloqueos indefinidos, y convertir el mutex del Capítulo 11 en un diseño de sincronización que puedas defender."
partNumber: 3
partName: "Concurrency and Synchronization"
chapter: 12
lastUpdated: "2026-04-18"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 195
language: "es-ES"
---
# Mecanismos de sincronización

## Orientación para el lector y resultados

El capítulo 11 concluyó con un driver que era, por primera vez en este libro, *verificablemente* concurrente. Tenías un único mutex protegiendo el buffer circular, contadores atómicos que escalaban a muchos núcleos, `WITNESS` e `INVARIANTS` vigilando cada adquisición de lock, un kit de pruebas de estrés que ejecutabas sobre un kernel de depuración y un documento `LOCKING.md` que cualquier mantenedor futuro (incluido tú mismo en el futuro) podría leer para entender la historia de concurrencia. Eso fue un progreso real. Sin embargo, no era el final de la historia.

El conjunto de herramientas de sincronización que FreeBSD pone a tu disposición es mucho más amplio que la única primitiva que introdujo el capítulo 11. El mutex que utilizaste es la respuesta correcta para muchas situaciones. También es la respuesta incorrecta para varias otras. Una tabla de configuración que veinte threads consultan diez mil veces por segundo necesita algo distinto a un mutex serializando las lecturas. Una lectura bloqueante que debe responder a Ctrl-C en milisegundos necesita algo distinto a `mtx_sleep(9)` sin tiempo límite. Un despertar coordinado entre una docena de threads en espera, uno por condición, necesita algo más expresivo que un puntero de canal anónimo como `&sc->cb`. El kernel tiene primitivas para cada uno de estos casos, y el capítulo 12 es donde las conoceremos.

Este capítulo hace por el resto del conjunto de herramientas de sincronización lo que el capítulo 11 hizo por el mutex. Presentamos cada primitiva empezando por la motivación, construimos el modelo mental, la anclamos en el código fuente real de FreeBSD, la aplicamos al driver `myfirst` en curso y verificamos el resultado contra un kernel con `WITNESS`. Al terminar, podrás leer un driver real y reconocer, solo por la elección de primitiva, qué intentaba expresar su autor. También podrás tomar esas decisiones tú mismo, en tu propio driver, sin recurrir a la herramienta equivocada por inercia.

### Por qué este capítulo merece su propio espacio

Sería posible, en principio, detenerse en el capítulo 11. Un mutex, un contador atómico y `mtx_sleep` cubren los casos simples. Muchos drivers pequeños en el árbol de FreeBSD no utilizan nada más.

El problema es que los drivers pequeños no son donde vive la mayoría de los errores. Los drivers que la gente mantiene durante más tiempo son los que fueron creciendo. Un driver de dispositivo USB empieza siendo pequeño, luego adquiere un canal de control, después una tabla de configuración que el espacio de usuario puede modificar en tiempo de ejecución y, finalmente, una cola de eventos independiente con sus propios threads en espera. Cada una de esas incorporaciones pone de manifiesto los límites del esquema «un mutex lo protege todo». Un desarrollador de drivers que solo conoce el mutex acaba bien usándolo mal (un mutex retenido demasiado tiempo bloquea el subsistema entero) o esquivándolo (una maraña de bucles de espera activa, reintentos con condiciones de carrera y flags globales que «nunca deberían ocurrir, pero a veces ocurren»). Las primitivas de sincronización que enseña este capítulo existen precisamente para evitar que esos parches entren en el código.

Cada primitiva es una forma distinta de acuerdo entre threads. Un mutex dice *solo uno de nosotros a la vez*. Una variable de condición dice *esperaré un cambio concreto del que me informarás*. Un lock compartido/exclusivo dice *muchos de nosotros podemos leer; solo uno puede escribir*. Un sleep con tiempo límite dice *y por favor abandona si tarda demasiado*. Un driver que usa cada herramienta para lo que está pensada se lee con claridad, se comporta de forma predecible y sigue siendo comprensible mucho después de que su autor original haya dejado de mirarlo. Un driver que usa una sola herramienta para todo o bien sufre en rendimiento o bien esconde errores en lugares donde nadie mira.

Este capítulo es, por tanto, un capítulo de vocabulario tanto como de mecánica. Sí presentamos las APIs y sí recorremos el código. El objetivo más profundo es darte las palabras para lo que intentas expresar.

### En qué estado dejó el driver el capítulo 11

Un punto de control rápido, porque el capítulo 12 se construye directamente sobre los resultados del capítulo 11. Si alguno de los siguientes puntos falta o te genera dudas, vuelve al capítulo 11 antes de empezar este.

- Tu driver `myfirst` compila sin errores ni advertencias con `WARNS=6`.
- Utiliza las macros `MYFIRST_LOCK(sc)`, `MYFIRST_UNLOCK(sc)` y `MYFIRST_ASSERT(sc)`, que se expanden a `mtx_lock`, `mtx_unlock` y `mtx_assert(MA_OWNED)` sobre el `sc->mtx` del dispositivo (un sleep mutex de tipo `MTX_DEF`).
- El cbuf, los contadores por descriptor, el recuento de aperturas y el recuento de descriptores activos están todos protegidos por `sc->mtx`.
- Los contadores de bytes `sc->bytes_read` y `sc->bytes_written` son contadores `counter_u64_t` por CPU; no necesitan el mutex.
- Los caminos de lectura y escritura bloqueantes utilizan `mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd"|"myfwr", 0)` como primitiva de espera y `wakeup(&sc->cb)` como la señal de despertar correspondiente.
- `INVARIANTS` y `WITNESS` están habilitados en tu kernel de pruebas; lo has compilado y arrancado.
- Un documento `LOCKING.md` acompaña al driver y enumera cada campo compartido, cada lock, cada canal de espera y cada decisión de «no proteger intencionadamente» con su justificación.
- El kit de estrés del capítulo 11 (`producer_consumer`, `mp_stress`, `mt_reader`, `lat_tester`) se compila y ejecuta sin problemas.

Ese driver es el sustrato del que parte este capítulo. No lo vamos a descartar. Reemplazaremos algunas primitivas por otras más adecuadas, añadiremos un pequeño subsistema nuevo que dé a las nuevas primitivas algo que proteger y terminaremos con un driver cuyo diseño de sincronización sea a la vez más capaz y más fácil de leer.

### Qué aprenderás

Al cerrar este capítulo deberías ser capaz de:

- Explicar qué significa *sincronización* en el sentido del kernel y distinguirla del concepto más estrecho de *exclusión mutua*.
- Mapear el conjunto de herramientas de sincronización de FreeBSD en un pequeño árbol de decisión: mutex, variable de condición, lock compartido/exclusivo, lock de lectores/escritores, atómico, sleep con tiempo límite.
- Reemplazar canales de espera anónimos por variables de condición con nombre (`cv(9)`), y explicar por qué ese cambio mejora tanto la corrección como la legibilidad.
- Usar `cv_wait`, `cv_wait_sig`, `cv_wait_unlock`, `cv_signal`, `cv_broadcast` y `cv_broadcastpri` correctamente con respecto a su mutex de interlock.
- Acotar una operación bloqueante mediante un tiempo límite usando `cv_timedwait_sig` (o `mtx_sleep` con un argumento `timo` distinto de cero), y diseñar la respuesta del llamante cuando ese tiempo se agota.
- Distinguir `EINTR`, `ERESTART` y `EWOULDBLOCK`, y decidir cuál debe devolver el driver cuando una espera falla.
- Elegir entre `sx(9)` (con capacidad de dormir) y `rw(9)` (basado en giro), entender las reglas que cada uno impone al contexto llamante, y aplicar `sx_init`, `sx_xlock`, `sx_slock`, `sx_xunlock`, `sx_sunlock`, `sx_try_upgrade` y `sx_downgrade`.
- Diseñar un esquema de múltiples lectores y un único escritor que use un lock para el camino de datos y otro para el camino de configuración, con un orden de locks documentado y una ausencia de inversiones verificada por `WITNESS`.
- Leer los avisos de `WITNESS` con la precisión suficiente para identificar el par de locks exacto y la línea de código fuente de la violación.
- Usar los comandos del depurador integrado en el kernel `show locks`, `show all locks`, `show witness` y `show lockchain` para inspeccionar un sistema bloqueado por un error de sincronización.
- Construir una carga de estrés que ejercite el driver a través de descriptores, sysctls y esperas con tiempo límite simultáneamente, y leer la salida de `lockstat(1)` para encontrar las primitivas con contención.
- Refactorizar el driver hasta dejarlo en un estado en que la historia de sincronización esté documentada, el orden de locks sea explícito y la cadena de versión refleje la nueva arquitectura.

Es una lista considerable. Ninguno de esos puntos es opcional para un driver que aspire a sobrevivir a su autor. Todo ello se construye sobre lo que el capítulo 11 dejó en tus manos.

### Qué no cubre este capítulo

Varios temas adyacentes se posponen deliberadamente:

- **Callouts (`callout(9)`).** El capítulo 13 introduce el trabajo temporizado que se dispara desde la infraestructura de reloj del kernel. Aquí lo tocamos únicamente como una primitiva de sleep con tiempo límite vista desde la llamada bloqueante del driver; la API completa de callout y sus reglas pertenecen al capítulo 13.
- **Taskqueues (`taskqueue(9)`).** El capítulo 16 introduce el marco de trabajo diferido del kernel. Varios drivers usan un taskqueue para desacoplar el thread bloqueante de la señal de despertar, pero hacerlo bien requiere su propio capítulo.
- **`epoch(9)` y patrones lock-free de lectura predominante.** Los drivers de red en particular usan `epoch(9)` para que los lectores avancen sin adquirir ningún lock. El mecanismo es sutil y se enseña mejor junto al subsistema de drivers de red en la Parte 6.
- **Sincronización en contexto de interrupción.** Los manejadores de interrupción de hardware real añaden otra capa de restricciones sobre qué locks puedes mantener y qué primitivas de sleep son legales. El capítulo 14 introduce los manejadores de interrupción y revisa las reglas de sincronización desde ese contexto. En el capítulo 12 nos quedamos completamente en contexto de proceso y de kernel thread.
- **Estructuras de datos lockless.** `buf_ring(9)` y similares son herramientas eficaces para los caminos calientes, pero exigen un estudio cuidadoso y requieren una carga de trabajo específica para justificar su complejidad. La Parte 6 (capítulo 28) los introduce cuando un driver del libro realmente los necesita.
- **Sincronización distribuida y entre máquinas.** Fuera del alcance. En este libro trabajamos con un sistema operativo en una sola máquina.

Mantenerse dentro de esos límites mantiene el capítulo centrado en lo que puede enseñar bien. El lector del capítulo 12 debería terminar con un control seguro de `cv(9)`, `sx(9)` y las esperas con tiempo límite, y con una comprensión operativa de dónde encajan `rw(9)` y `epoch(9)`; esa seguridad es lo que hace legibles los capítulos posteriores cuando aparecen.

### Inversión de tiempo estimada

- **Solo lectura**: aproximadamente tres horas. El nuevo vocabulario (variables de condición, locks compartidos/exclusivos, reglas de sleepability) tarda en absorberse aunque la superficie de la API sea pequeña.
- **Lectura más escritura de los ejemplos guiados**: de seis a ocho horas repartidas en dos sesiones. El driver evoluciona en cuatro etapas pequeñas; cada etapa añade una primitiva.
- **Lectura más todos los laboratorios y desafíos**: de diez a catorce horas repartidas en tres o cuatro sesiones, incluido el tiempo para las ejecuciones de estrés y el análisis con `lockstat(1)`.

Si te encuentras confundido a mitad de la Sección 4, es normal. La distinción compartido/exclusivo es genuinamente nueva incluso para los lectores familiarizados con los mutexes, y la tentación de usar `sx` para el camino de datos es exactamente la tentación que la Sección 5 existe para resolver. Para, vuelve a leer el ejemplo de la Sección 4 y continúa cuando el modelo haya encajado.

### Requisitos previos

Antes de empezar este capítulo, confirma lo siguiente:

- Tu código fuente del driver corresponde al árbol del capítulo 11, Etapa 3 (counter9) o Etapa 5 (KASSERTs). Se prefiere la Etapa 5 porque las aserciones detectan los nuevos errores más rápido.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con `/usr/src` en disco y correspondiente al kernel en ejecución.
- Un kernel de depuración con `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` está compilado, instalado y arrancando correctamente. La sección de referencia del capítulo 11 «Compilar y arrancar un kernel de depuración» contiene los pasos necesarios.
- Has leído el capítulo 11 con atención. Las reglas del mutex, la regla del sleep con mutex, la disciplina de orden de locks y el flujo de trabajo con `WITNESS` son conocimiento asumido aquí.
- Has ejecutado el kit de estrés del capítulo 11 al menos una vez y has visto que pasa.

Si alguno de los puntos anteriores está flojo, solucionarlo ahora es una inversión mucho mejor que avanzar por el capítulo 12 e intentar depurar desde una base inestable.

### Cómo sacar el máximo provecho de este capítulo

Tres hábitos darán sus frutos rápidamente.

Primero, ten marcados `/usr/src/sys/kern/kern_condvar.c`, `/usr/src/sys/kern/kern_sx.c` y `/usr/src/sys/kern/kern_rwlock.c`. Cada uno es breve, está bien comentado y es la fuente de autoridad sobre lo que la primitiva hace realmente. Varias veces en el capítulo te pediremos que eches un vistazo a una función concreta. Un minuto invertido allí hará que los párrafos circundantes sean más fáciles de absorber.

> **Una nota sobre los números de línea.** Cuando más adelante en el capítulo te indiquemos una función concreta, abre el archivo y busca el símbolo en lugar de saltar a un número de línea. `_cv_wait_sig` se encuentra en `/usr/src/sys/kern/kern_condvar.c` y `sleepq_signal` en `/usr/src/sys/kern/subr_sleepqueue.c` en el árbol de la versión 14.3 en el momento de escribir estas líneas; los nombres se mantendrán en futuras versiones de punto, pero el número de línea en que se sitúa cada uno, no. La referencia duradera es siempre el símbolo.

En segundo lugar, ejecuta cada cambio de código que hagas bajo `WITNESS`. Las primitivas de sincronización que presentamos en este capítulo tienen reglas más estrictas que las del mutex. `WITNESS` es la manera más económica de descubrir que has roto alguna de ellas. La sección de referencia del Capítulo 11 "Construir e iniciar un kernel de depuración" recorre el proceso de build del kernel si la necesitas; no la omitas ahora.

En tercer lugar, escribe los cambios del driver a mano siempre que puedas. El código fuente de acompañamiento en `examples/part-03/ch12-synchronization-mechanisms/` es la versión canónica, pero la memoria muscular de escribir `cv_wait_sig(&sc->data_cv, &sc->mtx)` una sola vez vale más que leerlo diez veces. El capítulo muestra los cambios de forma incremental; sigue ese mismo ritmo incremental en tu propia copia del driver.

### Mapa de ruta del capítulo

Las secciones, en orden, son las siguientes:

1. Qué es la sincronización en el kernel y dónde se sitúa cada primitiva en un pequeño árbol de decisión.
2. Las variables de condición, la alternativa más limpia a los canales de wakeup anónimos, y la primera refactorización de los caminos de bloqueo de `myfirst`.
3. Timeouts y sleep interrumpible, incluyendo la gestión de señales y la elección entre `EINTR`, `ERESTART` y `EWOULDBLOCK`.
4. El lock `sx(9)`, que al fin nos permite expresar "muchos lectores, escritor ocasional" sin serializar a todos los lectores.
5. Un escenario con múltiples lectores y un único escritor en el driver: un pequeño subsistema de configuración, el orden de adquisición de locks entre el camino de datos y el camino de configuración, y la disciplina de `WITNESS` que lo mantiene todo correcto.
6. Depuración de problemas de sincronización, con un recorrido cuidadoso por `WITNESS`, los comandos del depurador interno del kernel para inspeccionar locks, y los patrones de deadlock más habituales.
7. Pruebas de estrés bajo patrones de I/O realistas, con `lockstat(1)`, `dtrace(1)` y los testers del Capítulo 11 ampliados para las nuevas primitivas.
8. Refactorización y versionado del driver: un `LOCKING.md` limpio, una cadena de versión actualizada, un changelog revisado y una pasada de regresión que valida el conjunto.

A continuación vienen los laboratorios prácticos y los ejercicios de desafío, seguidos de una referencia para la resolución de problemas, una sección de cierre y un puente hacia el Capítulo 13.

Si es tu primera lectura, avanza de forma lineal y realiza los laboratorios en orden. Si estás revisitando el capítulo, la sección de depuración y la de refactorización son autónomas y pueden leerse en una sola sesión.



## Sección 1: ¿Qué es la sincronización en el kernel?

El Capítulo 11 utilizó las palabras *sincronización*, *locking*, *exclusión mutua* y *coordinación* de forma más o menos intercambiable. Era aceptable cuando la única primitiva disponible era el mutex, porque el mutex colapsa todas esas ideas en un único mecanismo. Con el conjunto de herramientas más amplio que introduce el Capítulo 12, las palabras empiezan a significar cosas distintas, y aclararlas desde el principio evita mucha confusión posterior.

Esta sección establece el vocabulario. También traza el pequeño árbol de decisión al que recurriremos a lo largo de todo el capítulo cuando nos preguntemos "¿qué primitiva debo utilizar aquí?".

### Qué significa sincronización

La **sincronización** es la idea más amplia: cualquier mecanismo por el que dos o más threads de ejecución concurrentes coordinan su acceso al estado compartido, su avance a través de un procedimiento compartido o su temporización relativa entre sí.

Tres formas de coordinación cubren casi todo lo que un driver puede necesitar:

**Exclusión mutua**: como máximo un thread a la vez dentro de una región crítica. Los mutexes y los locks exclusivos ofrecen esta garantía. La garantía es estructural: mientras estás dentro, nadie más puede estarlo.

**Acceso compartido con escrituras restringidas**: muchos threads pueden inspeccionar un valor al mismo tiempo, pero un thread que quiere modificarlo debe esperar hasta que todos los demás hayan salido y ninguno más esté dentro. Los locks compartidos/exclusivos ofrecen esto. La garantía es asimétrica: los lectores se toleran mutuamente; un escritor no tolera a nadie.

**Espera coordinada**: un thread se suspende hasta que alguna condición se hace verdadera, y otro thread que sabe que la condición se ha cumplido despierta al que espera. Las variables de condición y el mecanismo más antiguo de canal `mtx_sleep` / `wakeup` ofrecen esto. La garantía es temporal: el que espera no consume CPU mientras está suspendido; el que despierta no necesita saber quién está esperando; el kernel gestiona el encuentro.

Un driver suele utilizar las tres. El cbuf de `myfirst` ya emplea dos: exclusión mutua para proteger el estado del cbuf, y espera coordinada para suspender a un lector cuando el buffer está vacío. El Capítulo 12 añade la tercera (acceso compartido) y refina la segunda (variables de condición con nombre en lugar de canales anónimos).

### Sincronización frente a locking

Es tentador pensar que *sincronización* y *locking* son la misma cosa. No lo son.

El **locking** es una técnica de sincronización. Es la familia de mecanismos que operan sobre un objeto compartido y otorgan o deniegan el acceso a él. Los mutexes, los sx locks, los rw locks y los lockmgr locks son todos locks.

La **sincronización** incluye el locking, pero también incluye la espera coordinada (variables de condición, canales de sleep), la señalización de eventos (semáforos) y la coordinación temporizada (callouts, sleeps temporizados). Un thread que espera puede no estar reteniendo ningún lock en el momento en que se suspende (de hecho, con `mtx_sleep` y `cv_wait`, el lock se libera durante la espera) y, aun así, está participando en la sincronización con los threads que eventualmente lo despertarán.

El modelo mental que se desprende de esta distinción resulta útil: el locking trata del *acceso*; la espera coordinada trata del *progreso*. La mayor parte del código de driver no trivial mezcla ambos. El mutex alrededor del cbuf es locking. El sleep sobre `&sc->cb` mientras el buffer está vacío es espera coordinada. Ambos son sincronización. Ninguno de los dos por sí solo sería suficiente.

### Bloqueo frente a spinning

Dos formas básicas se repiten en todas las primitivas de FreeBSD. Saber qué forma utiliza una primitiva es la mitad del trabajo a la hora de elegir entre ellas.

Las **primitivas de bloqueo** ponen a dormir a un thread contendiente en la sleep queue del kernel. El thread dormido no consume CPU; el kernel lo pondrá de nuevo en estado ejecutable cuando el thread que retiene el lock lo libere o cuando se señalice la condición esperada. Las primitivas de bloqueo son adecuadas cuando la espera puede ser larga, cuando el thread se encuentra en un contexto donde está permitido dormir y cuando mantener la CPU ocupada en un bucle de reintento perjudicaría el rendimiento global. Los mutexes `MTX_DEF`, `cv_wait`, `sx_xlock`, `sx_slock` y `mtx_sleep` son todos de bloqueo.

Las **primitivas de spinning** mantienen el thread en la CPU y realizan una espera activa sobre el estado del lock, reintentando de forma atómica hasta que el poseedor lo libere. Son adecuadas únicamente cuando la sección crítica es muy corta, cuando el thread no puede dormir legalmente (por ejemplo, dentro de un filtro de interrupción de hardware) o cuando el coste de un cambio de contexto superaría con creces el tiempo de espera. Los mutexes `MTX_SPIN` y los locks `rw(9)` son de spinning. El propio kernel utiliza spin locks para las capas más bajas del planificador y la maquinaria de interrupciones.

El Capítulo 12 se mueve mayoritariamente en el mundo del bloqueo. Nuestro driver se ejecuta en contexto de proceso; tiene permiso para dormir; la ventaja del spinning sería marginal. La única excepción es cuando mencionamos `rw(9)` como hermano de `sx(9)` a modo de referencia; el tratamiento más profundo de `rw(9)` corresponde a capítulos donde un driver lo utiliza por una razón real.

### Un pequeño mapa de las primitivas de FreeBSD

El conjunto de herramientas de sincronización de FreeBSD es mayor de lo que la gente espera. Para el desarrollo de drivers, ocho primitivas soportan prácticamente toda la carga:

| Primitiva | Cabecera | Comportamiento | Ideal para |
|---|---|---|---|
| `mtx(9)` (`MTX_DEF`) | `sys/mutex.h` | Sleep mutex; un propietario a la vez | Lock por defecto para la mayor parte del estado del softc |
| `mtx(9)` (`MTX_SPIN`) | `sys/mutex.h` | Spin mutex; deshabilita interrupciones | Secciones críticas cortas en contexto de interrupción |
| `cv(9)` | `sys/condvar.h` | Canal de espera con nombre; se usa junto con un mutex | Esperas coordinadas con múltiples condiciones distintas |
| `sx(9)` | `sys/sx.h` | Lock compartido/exclusivo en modo sleep | Estado de solo lectura frecuente en contexto de proceso |
| `rw(9)` | `sys/rwlock.h` | Lock lector/escritor en modo spin | Estado de solo lectura frecuente en contexto de interrupción o secciones críticas cortas |
| `rmlock(9)` | `sys/rmlock.h` | Lock de solo lectura frecuente; lecturas baratas, escrituras costosas | Caminos de lectura con cambios de configuración poco frecuentes |
| `sema(9)` | `sys/sema.h` | Semáforo con contador | Contabilidad de recursos; rara vez necesario en drivers |
| `epoch(9)` | `sys/epoch.h` | Sincronización de solo lectura frecuente con reclamación diferida | Caminos de lectura muy activos en drivers de red y almacenamiento |

Las que utilizamos en este capítulo, además del mutex introducido en el Capítulo 11, son `cv(9)` y `sx(9)`. `rw(9)` se menciona por contexto. `rmlock(9)`, `sema(9)` y `epoch(9)` se posponen para capítulos posteriores donde el driver en cuestión las justifique realmente.

### Los atómicos en el mismo mapa

En sentido estricto, las primitivas `atomic(9)` del Capítulo 11 no forman parte del conjunto de herramientas de sincronización en absoluto. Son *operaciones concurrentes*: accesos a memoria indivisibles que se combinan con los locks pero que por sí mismos no proporcionan bloqueo, espera ni señalización. Se sitúan junto a los locks de la misma forma en que una herramienta eléctrica se sitúa junto a una herramienta de mano: útil para un trabajo específico, no un sustituto del resto del conjunto.

Recurriremos a los atómicos en este capítulo solo cuando una operación de lectura-modificación-escritura de una sola palabra sea la forma adecuada de expresar lo que queremos. Para todo lo demás, los locks y las variables de condición demuestran su valor.

### Un primer árbol de decisión

Cuando te encuentras ante un fragmento de estado compartido y debes decidir cómo protegerlo, trabaja las preguntas en este orden. La primera pregunta que dé una respuesta definitiva termina la búsqueda.

1. **¿Es el estado una única palabra que necesita una única operación de lectura-modificación-escritura?** Usa un atómico. (Ejemplos: un contador de generación, una palabra de flags.)
2. **¿Tiene el estado un invariante compuesto que abarca más de un campo, y se produce el acceso en contexto de proceso?** Usa un mutex `MTX_DEF`. (Ejemplos: el par head/used del cbuf, un par head/tail de cola.)
3. **¿Se produce el acceso en contexto de interrupción, o la sección crítica debe deshabilitar la apropiación?** Usa un mutex `MTX_SPIN`.
4. **¿Se lee el estado con frecuencia desde muchos threads pero se escribe raramente?** Usa `sx(9)` para llamadores que pueden dormir (la mayor parte del código de driver) o `rw(9)` para secciones críticas cortas que pueden ejecutarse en contexto de interrupción.
5. **¿Necesitas esperar hasta que una condición específica se haga verdadera (no solo adquirir un lock)?** Usa una variable de condición (`cv(9)`) emparejada con el mutex que protege la condición. El mecanismo antiguo de canal `mtx_sleep`/`wakeup` es la alternativa heredada; el código nuevo debería preferir `cv(9)`.
6. **¿Necesitas acotar una espera por tiempo de reloj?** Usa la variante temporizada (`cv_timedwait_sig`, `mtx_sleep` con un argumento `timo` distinto de cero, `msleep_sbt(9)`) y diseña el llamador para que gestione `EWOULDBLOCK`.

El árbol se resume en un breve lema: *atómico para una palabra, mutex para una estructura, sx para lectura frecuente, cv para esperar, temporizado para espera acotada, spin solo cuando sea imprescindible*.

### Una decisión práctica: dónde se sitúa cada estado de `myfirst`

Recorre el árbol aplicándolo a cada fragmento de estado de tu driver actual. El ejercicio es breve y útil.

- Los índices del cbuf y la memoria de respaldo: invariante compuesto, contexto de proceso. Usa un mutex `MTX_DEF`. (Esto es lo que eligió el Capítulo 11.)
- `sc->bytes_read`, `sc->bytes_written`: contadores de alta frecuencia, leídos raramente. Usa contadores por CPU de `counter(9)`. (Esto es lo que migró el Capítulo 11.)
- `sc->open_count`, `sc->active_fhs`: enteros de baja frecuencia, perfectamente manejables bajo el mismo mutex que el cbuf. No hay razón para separarlos.
- `sc->is_attached`: un flag, leído con frecuencia al entrar en los handlers, escrito una vez por cada attach/detach. El diseño del Capítulo 11 lo lee sin el mutex como optimización, lo comprueba de nuevo después de cada sleep y lo escribe bajo el mutex.
- La condición "¿está vacío el buffer?" sobre la que bloquean los lectores en espera: una espera coordinada. Actualmente usa `mtx_sleep(&sc->cb, ...)`. La Sección 2 la sustituirá por una variable de condición con nombre.
- La condición "¿hay espacio en el buffer?" sobre la que bloquean los escritores en espera: otra espera coordinada, que actualmente comparte el mismo canal. La Sección 2 le dará su propia variable de condición.
- Un futuro subsistema de configuración (que se añade en la Sección 5): leído con frecuencia por cada llamada de I/O, escrito ocasionalmente por un handler de sysctl. Usa `sx(9)`.

Observa cómo el árbol hizo el trabajo. No tuvimos que inventar un diseño personalizado para ninguno de estos casos; planteamos las preguntas y la primitiva adecuada surgió de forma natural.

### Analogía del mundo real: puertas, pasillos y pizarras

Una pequeña analogía para quienes la encuentren útil. Imagina un laboratorio de investigación.

El cbuf es un instrumento de precisión que solo una persona a la vez puede operar. El laboratorio instala una puerta con una sola llave. Quien quiera usar el instrumento debe coger la llave. Mientras la tiene, nadie más puede entrar. Eso es un mutex.

El laboratorio tiene una pizarra de estado que muestra la calibración actual del instrumento. Cualquiera puede consultarla en cualquier momento sin interferir con los demás. Solo el encargado del laboratorio la actualiza, y lo hace únicamente después de esperar a que todos los demás se hayan apartado. Eso es un lock compartido/exclusivo.

El laboratorio tiene una cafetera. Quienes quieren café y encuentran la cafetera vacía dejan una nota en el tablón de anuncios: «Estoy en el salón; avisadme cuando haya café». Cuando alguien prepara una cafetera nueva, revisa el tablón y da un toque en el hombro a todos aquellos cuya nota pide «café», sin importar cuánto tiempo hace que la escribieron. Eso es una variable de condición.

La misma persona que dejó la nota del café puede dejar también una segunda nota: «pero esperad solo quince minutos; si para entonces no hay café, me voy a la cafetería». Eso es una espera con tiempo límite.

Cada mecanismo del laboratorio corresponde a un problema de coordinación real. Ninguno es un sustituto de los demás. Lo mismo ocurre en el kernel.

### Comparación de primitivas en paralelo

A veces, ver las primitivas una junto a la otra en una sola tabla hace que la elección sea inmediata. Las propiedades que difieren entre ellas son: si bloquean o hacen spin, si admiten acceso compartido (multi-lector), si el poseedor puede hacer sleep, si admiten propagación de prioridad, si son interrumpibles por señales, y si el contexto de llamada puede incluir operaciones bloqueantes.

| Propiedad | `mtx(9) MTX_DEF` | `mtx(9) MTX_SPIN` | `sx(9)` | `rw(9)` | `cv(9)` |
|---|---|---|---|---|---|
| Comportamiento bajo contención | Sleep | Spin | Sleep | Spin | Sleep |
| Múltiples titulares | No | No | Sí (compartido) | Sí (lectura) | n/a (waiters) |
| El llamador puede hacer sleep mientras lo mantiene | Sí | No | Sí | No | n/a |
| Propagación de prioridad | Sí | No (interrupciones desactivadas) | No | Sí | n/a |
| Variante interrumpible por señal | n/a | n/a | `_sig` | No | `_sig` |
| Tiene variante con tiempo de espera acotado | `mtx_sleep` con timo | n/a | n/a | n/a | `cv_timedwait` |
| Apto en contexto de interrupción | No | Sí | No | Sí (con cuidado) | No |

Dos cosas destacan. En primer lugar, la columna de `cv(9)` no encaja realmente en las mismas preguntas, porque cv no es un lock; es una primitiva de espera. La incluimos en la comparación porque la pregunta «¿debo esperar o hacer spin?» es esencialmente la misma que «¿debo bloquearme en un cv o hacer spin en un `MTX_SPIN`?». En segundo lugar, la columna de propagación de prioridad distingue `mtx(9)` y `rw(9)` de `sx(9)`. `sx(9)` no propaga la prioridad porque sus colas de espera no lo admiten. En la práctica, esto solo importa para cargas de trabajo en tiempo real; los drivers ordinarios no lo notan.

Usa la tabla como referencia rápida cuando te enfrentes a un nuevo fragmento de estado. El árbol de decisión anterior te indica el *orden* en el que formular las preguntas; la tabla te da la *respuesta* una vez que las has planteado.

### Una nota sobre los semáforos

FreeBSD también dispone de una primitiva de semáforo contador (`sema(9)`) que resulta útil en ocasiones. Un semáforo es un contador; los threads lo decrementan (mediante `sema_wait` o `sema_trywait`) y se bloquean si el contador es cero; los threads lo incrementan (mediante `sema_post`) y pueden despertar a un waiter. El uso clásico es el control de recursos limitados: una cola con longitud máxima donde los productores se bloquean cuando la cola está llena y los consumidores se bloquean cuando está vacía.

La mayoría de los problemas de driver que tienen forma de semáforo pueden resolverse igualmente bien con un mutex más una variable de condición. La solución con cv tiene la ventaja de que puedes asociar un nombre a cada condición; el semáforo es anónimo. El semáforo tiene la ventaja de que la espera y la señalización forman parte de la propia primitiva, sin necesidad de un interlock separado.

Este capítulo no usa `sema(9)`. Lo mencionamos por completitud; si lo encuentras en código fuente real de un driver, ahora ya sabes qué forma tiene.

### Cerrando la sección 1

La sincronización es más amplia que el locking, el locking es más amplio que la exclusión mutua, y el conjunto de herramientas de FreeBSD te ofrece una primitiva distinta para cada tipo de problema de coordinación que puedas encontrar. Atómicas para actualizaciones de una sola palabra, mutexes para invariantes compuestos, sx locks para estado de lectura mayoritaria, variables de condición para espera coordinada, sleeps con tiempo límite para espera acotada, y variantes de spin solo cuando el contexto de llamada las exige.

Ese árbol de decisión guiará cada elección que hagamos durante el resto del capítulo. La sección 2 comienza con el primer refactor: convertir el canal de wakeup anónimo `&sc->cb` del capítulo 11 en un par de variables de condición con nombre.



## Sección 2: Variables de condición y sleep/wakeup

El driver `myfirst` tal como lo dejó el capítulo 11 tiene dos condiciones distintas que bloquean las rutas de I/O. Un lector hace sleep cuando `cbuf_used(&sc->cb) == 0` y espera a que hayan llegado datos. Un escritor hace sleep cuando `cbuf_free(&sc->cb) == 0` y espera a que haya aparecido espacio. Ambos duermen actualmente en el mismo canal anónimo, `&sc->cb`. Ambos wakeups llaman a `wakeup(&sc->cb)` tras cada cambio de estado, lo que despierta a todos los threads dormidos independientemente de qué condición provocó el cambio.

Ese esquema funciona. Pero también es ineficiente, opaco y más difícil de razonar de lo necesario. Esta sección introduce las variables de condición (`cv(9)`), la primitiva más limpia de FreeBSD para el mismo patrón de espera coordinada, y recorre el refactor que le asigna a cada condición su propia variable.

### Por qué mutex más wakeup no es suficiente

El capítulo 11 usó `mtx_sleep(chan, mtx, pri, wmesg, timo)` y `wakeup(chan)` para coordinar las rutas de lectura y escritura. El par tiene la gran virtud de la simplicidad: cualquier puntero puede ser un canal, el kernel mantiene una tabla hash de waiters por canal, y un `wakeup` en el canal correcto los encuentra a todos.

Los defectos aparecen a medida que el driver crece.

**El canal es anónimo.** Un lector del código fuente ve `mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", 0)` y debe inferir, a partir del contexto y la cadena wmesg, qué condición está esperando el thread. En `&sc->cb` no hay nada que indique «datos disponibles» en lugar de «espacio disponible» o «dispositivo desconectado». El canal es solo un puntero que el kernel usa como clave de hash; el significado reside en la convención.

**Múltiples condiciones comparten un canal.** Cuando `myfirst_write` finaliza una escritura, llama a `wakeup(&sc->cb)`. Eso despierta a todos los waiters en `&sc->cb`, incluidos los lectores que esperan datos (correcto) y los escritores que esperan espacio (incorrecto; una escritura no libera espacio, lo consume). Cada waiter indeseado vuelve a dormir tras volver a comprobar su condición. Este es el problema del *thundering herd* en miniatura: el wakeup es correcto pero costoso.

**Los wakeups perdidos siguen siendo posibles si no se tiene cuidado.** Si alguna vez sueltas el mutex entre la comprobación de la condición y la entrada en `mtx_sleep`, un wakeup puede dispararse en esa ventana y perderse. El capítulo 11 explicó que `mtx_sleep` en sí mismo es atómico con la liberación del lock, lo que cierra esa ventana; pero la regla es implícita en la API y fácil de violar al refactorizar.

**El argumento wmesg es la única etiqueta.** Un ingeniero que depura un proceso bloqueado ejecutando `procstat -kk` ve `myfrd` y tiene que recordar qué significa eso. La cadena tiene siete caracteres como máximo; es una pista, no una descripción estructurada.

Las variables de condición resuelven los cuatro problemas. Cada cv tiene un nombre (su cadena de descripción es verificable desde `dtrace` y `procstat`). Cada cv representa exactamente una condición lógica; no hay duda sobre qué waiters afectará un `cv_signal`. La primitiva `cv_wait` garantiza la atomicidad de la liberación del lock al tomar el mutex como argumento, lo que hace mucho más difícil el uso incorrecto. Y la relación entre el waiter y el waker se expresa en el propio tipo: ambos lados referencian el mismo `struct cv`.

### Qué es una variable de condición

Una **variable de condición** es un objeto del kernel que representa una condición lógica que algunos threads están esperando y que otros señalizarán en algún momento. La variable de condición no almacena la condición; la condición vive en el estado de tu driver, protegida por tu mutex. La variable de condición es el punto de encuentro: el lugar donde los waiters hacen cola y donde los wakers los encuentran.

La estructura de datos es pequeña y reside en `/usr/src/sys/sys/condvar.h`:

```c
struct cv {
        const char      *cv_description;
        int              cv_waiters;
};
```

Dos campos: una cadena de descripción usada para la depuración, y un contador de threads en espera en ese momento (una optimización para omitir la maquinaria de wakeup cuando nadie está esperando).

La API también es pequeña:

```c
void  cv_init(struct cv *cvp, const char *desc);
void  cv_destroy(struct cv *cvp);

void  cv_wait(struct cv *cvp, struct mtx *mtx);
int   cv_wait_sig(struct cv *cvp, struct mtx *mtx);
void  cv_wait_unlock(struct cv *cvp, struct mtx *mtx);
int   cv_timedwait(struct cv *cvp, struct mtx *mtx, int timo);
int   cv_timedwait_sig(struct cv *cvp, struct mtx *mtx, int timo);

void  cv_signal(struct cv *cvp);
void  cv_broadcast(struct cv *cvp);
void  cv_broadcastpri(struct cv *cvp, int pri);

const char *cv_wmesg(struct cv *cvp);
```

(Los prototipos reales usan `struct lock_object` en lugar de `struct mtx`; las macros en `condvar.h` sustituyen por ti el puntero `&mtx->lock_object` correcto. La forma anterior es la que escribirás en el código del driver.)

Desde el principio son importantes algunas reglas y convenciones.

`cv_init` se llama una vez, después de que la estructura cv existe en memoria y antes de que cualquier waiter o waker pueda acceder a ella. El correspondiente `cv_destroy` se llama una vez, después de que cada waiter se haya despertado o haya sido expulsado de la cola, y antes de que se libere la estructura cv. Los errores de ciclo de vida aquí provocan el mismo tipo de fallos catastróficos que los errores de ciclo de vida de un mutex.

`cv_wait` y sus variantes deben llamarse con el mutex de interlock *adquirido*. Dentro de `cv_wait`, el kernel libera atómicamente el mutex y coloca el thread que lo llama en la cola de espera del cv. Cuando el thread se despierta, el mutex se readquiere antes de que `cv_wait` retorne. Desde la perspectiva de tu código, el mutex está adquirido tanto antes como después de la llamada; otro thread no puede haber observado el intervalo, aunque realmente existió. Este es exactamente el mismo contrato de liberación atómica y sleep que proporciona `mtx_sleep`.

`cv_signal` despierta a un waiter, y `cv_broadcast` despierta a todos los waiters. Vale la pena explicar con precisión qué waiter elige `cv_signal`. La página de manual de `condvar(9)` solo garantiza que desbloquea «un waiter»; *no* promete un orden FIFO estricto, y tu código no debe depender de un orden específico. Lo que hace realmente la implementación actual de FreeBSD 14.3, dentro de `sleepq_signal(9)` en `/usr/src/sys/kern/subr_sleepqueue.c`, es recorrer la cola de espera del cv y elegir el thread con la prioridad más alta, resolviendo empates a favor del thread que lleva más tiempo durmiendo. Es un modelo mental útil, pero trátalo como un detalle de implementación, no como una garantía de la API. Si la corrección depende de qué thread se despierta a continuación, tu diseño probablemente sea incorrecto y debería usar una primitiva diferente o una cola explícita. Tanto `cv_signal` como `cv_broadcast` se llaman típicamente con el mutex de interlock adquirido, aunque la regla tiene más que ver con la corrección de la lógica circundante que con la primitiva en sí: si llamas a `cv_signal` sin el interlock, es posible que un nuevo waiter llegue y pierda la señal. La disciplina estándar es, por tanto, «adquiere el mutex, cambia el estado, señaliza, suelta el mutex».

`cv_wait_sig` devuelve un valor distinto de cero si el thread fue despertado por una señal (normalmente `EINTR` o `ERESTART`); cero si fue despertado por `cv_signal` o `cv_broadcast`. Los drivers que quieren que sus rutas de I/O bloqueantes respondan a Ctrl-C usan `cv_wait_sig`, no `cv_wait`. La sección 3 explora las reglas de gestión de señales en profundidad.

`cv_wait_unlock` es la variante poco habitual para cuando el llamador quiere que el interlock se libere en el lado de la *espera* y no se readquiera al retornar. Resulta útil en secuencias de desmontaje donde el llamador no tiene más trabajo con el interlock una vez que la espera concluye. Los drivers raramente la necesitan; la mencionamos porque la verás en algunos lugares del árbol de FreeBSD, y el capítulo no la utiliza más.

`cv_timedwait` y `cv_timedwait_sig` añaden un timeout en ticks. Devuelven `EWOULDBLOCK` si el timeout expira antes de que llegue algún wakeup. La sección 3 explica cómo acotar una operación bloqueante con estas variantes.

### Refactor práctico: añadir dos variables de condición a myfirst

El driver del capítulo 11 tenía un canal anónimo para ambas condiciones. La etapa 1 de este capítulo lo divide en dos variables de condición con nombre: `data_cv` («hay datos disponibles para leer») y `room_cv` («hay espacio disponible para escribir»).

Añade dos campos al softc:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct cv               data_cv;
        struct cv               room_cv;
        /* ... existing fields ... */
};
```

Inicialízalas y destrúyelas en attach y detach:

```c
static int
myfirst_attach(device_t dev)
{
        /* ... existing setup ... */
        cv_init(&sc->data_cv, "myfirst data");
        cv_init(&sc->room_cv, "myfirst room");
        /* ... rest of attach ... */
}

static int
myfirst_detach(device_t dev)
{
        /* ... existing teardown that cleared is_attached and woke sleepers ... */
        cv_destroy(&sc->data_cv);
        cv_destroy(&sc->room_cv);
        /* ... rest of detach ... */
}
```

Una sutileza pequeña pero importante: detach no debe destruir un cv que aún tiene waiters. La ruta de detach del capítulo 11 ya despierta a los threads dormidos y se niega a continuar mientras `active_fhs > 0`, lo que significa que cuando llegamos a `cv_destroy`, ningún descriptor está abierto y ningún thread puede seguir dentro de `cv_wait`. Añadimos un `cv_broadcast(&sc->data_cv)` y un `cv_broadcast(&sc->room_cv)` inmediatamente antes de destruirlos, como precaución adicional, por si alguna ruta en segundo plano logra colarse.

Actualiza los helpers de espera para usar las nuevas variables:

```c
static int
myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error;

        MYFIRST_ASSERT(sc);
        while (cbuf_used(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore)
                        return (-1);
                if (ioflag & IO_NDELAY)
                        return (EAGAIN);
                error = cv_wait_sig(&sc->data_cv, &sc->mtx);
                if (error != 0)
                        return (error);
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}

static int
myfirst_wait_room(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error;

        MYFIRST_ASSERT(sc);
        while (cbuf_free(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore)
                        return (-1);
                if (ioflag & IO_NDELAY)
                        return (EAGAIN);
                error = cv_wait_sig(&sc->room_cv, &sc->mtx);
                if (error != 0)
                        return (error);
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}
```

Tres cosas cambiaron y nada más. `mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", 0)` se convirtió en `cv_wait_sig(&sc->data_cv, &sc->mtx)`. La línea correspondiente en la ruta de escritura se convirtió en `cv_wait_sig(&sc->room_cv, &sc->mtx)`. La cadena wmesg ha desaparecido (la cadena de descripción del cv ocupa su lugar), el canal es ahora un objeto real con nombre, y el flag `PCATCH` es implícito en el sufijo `_sig`.

Actualiza los despertadores. Tras una lectura exitosa, en lugar de despertar a todos los que esperan en `&sc->cb`, despierta únicamente a los escritores que aguardan espacio libre:

```c
got = myfirst_buf_read(sc, bounce, take);
fh->reads += got;
MYFIRST_UNLOCK(sc);

if (got > 0) {
        cv_signal(&sc->room_cv);
        selwakeup(&sc->wsel);
}
```

Tras una escritura exitosa, despierta únicamente a los lectores que aguardan datos:

```c
put = myfirst_buf_write(sc, bounce, want);
fh->writes += put;
MYFIRST_UNLOCK(sc);

if (put > 0) {
        cv_signal(&sc->data_cv);
        selwakeup(&sc->rsel);
}
```

Dos mejoras confluyen a la vez. Primero, una lectura exitosa ya no despierta a otros lectores (un despertar inútil que volvería a dormirse de inmediato); solo se despierta a los escritores, que son los únicos que pueden aprovechar el espacio liberado. De forma simétrica ocurre en el lado de la escritura. Segundo, el código ahora es autoexplicativo: `cv_signal(&sc->room_cv)` se lee como «hay espacio ahora»; el lector no tiene que recordar qué significa `&sc->cb`.

Fíjate en que hemos añadido una guarda `if (got > 0)` y `if (put > 0)` antes de la señal. No tiene sentido despertar a un proceso dormido si nada ha cambiado; la señal vacía es inocua, pero suprimirla es barato. Se trata de una pequeña optimización y a la vez una aclaración: la señal está anunciando un cambio de estado, y la guarda lo expresa con claridad.

`cv_signal` en lugar de `cv_broadcast`: despertamos a un único proceso en espera por cada cambio de estado, no a todos. El cambio de estado (un byte liberado por una lectura, un byte añadido por una escritura) basta para que un proceso en espera avance. Si hay varios procesos bloqueados, la siguiente señal despertará al siguiente. Esta es la correspondencia uno a uno por evento que fomenta la API de cv.

### Cuándo usar cv_signal frente a cv_broadcast

`cv_signal` despierta a un único waiter. `cv_broadcast` los despierta a todos. La elección importa más de lo que se suele pensar.

Usa `cv_signal` cuando:

- El cambio de estado es una actualización por evento (llegó un byte; se liberó un descriptor; se encoló un paquete). Con que un waiter avance es suficiente; el siguiente evento despertará al siguiente waiter.
- Todos los waiters son equivalentes y cualquiera de ellos puede consumir el cambio.
- El coste de despertar a un waiter que no tiene nada que hacer no es despreciable (porque el waiter vuelve a comprobar la condición inmediatamente y se duerme de nuevo).

Usa `cv_broadcast` cuando:

- El cambio de estado es global y todos los waiters necesitan saberlo (el dispositivo está siendo desenganchado; la configuración cambió; el buffer se reinició).
- Los waiters no son equivalentes; cada uno puede estar esperando en una subcondición ligeramente diferente que el broadcast resuelve.
- Quieres evitar el trabajo de determinar qué subconjunto de waiters puede continuar, a costa de despertar a algunos que volverán a dormirse.

Para las condiciones de datos y espacio de `myfirst`, `cv_signal` es la opción correcta. Para la ruta de detach, `cv_broadcast` es la opción correcta: el detach debe despertar a todos los threads bloqueados para que puedan devolver `ENXIO` y terminar limpiamente.

Añade los broadcasts al detach:

```c
MYFIRST_LOCK(sc);
sc->is_attached = 0;
cv_broadcast(&sc->data_cv);
cv_broadcast(&sc->room_cv);
MYFIRST_UNLOCK(sc);
```

Eso reemplaza el `wakeup(&sc->cb)` del Capítulo 11. Los dos broadcasts despiertan a todos los lectores y escritores que puedan estar dormidos; cada uno vuelve a comprobar `is_attached`, ve que ahora es cero y devuelve `ENXIO`.

### Una trampa sutil: cv_signal sin el mutex

La disciplina estándar dice "mantén el mutex, cambia el estado, haz la señal, suelta el mutex". Puede que hayas notado que nuestro refactor hace la señal *después* de soltar el mutex (el `MYFIRST_UNLOCK(sc)` precede al `cv_signal`). ¿Es eso un error?

No es un error, y la razón merece entenderse.

La condición de carrera que la disciplina pretende evitar es esta: un waiter comprueba la condición (falsa), está a punto de llamar a `cv_wait`, pero ha soltado el mutex. El emisor de la señal cambia entonces el estado, no ve a nadie en la cola de espera del cv (porque el waiter todavía no se ha encolado) y omite la señal. El waiter se encola después y se duerme para siempre.

El propio `cv_wait` evita esa condición de carrera encolando al waiter en el cv *antes* de soltar el mutex. El lock interno de la cola del cv se adquiere mientras el mutex del llamador sigue tomado, el thread se añade a la cola de espera, y solo entonces se suelta el mutex del llamador y se desplanifica el thread. Cualquier `cv_signal` posterior sobre ese cv, con o sin el mutex del llamador tomado, encontrará al waiter y lo despertará.

La disciplina de hacer la señal con el mutex tomado es, por tanto, una convención defensiva más que un requisito estricto. La seguimos en los casos simples (porque es más difícil cometer errores) y la relajamos cuando hacer la señal fuera del mutex supone una mejora medible (permite al thread despertado adquirir el mutex sin contender con el emisor de la señal). Para `myfirst`, hacer la señal después de `MYFIRST_UNLOCK(sc)` ahorra unos pocos ciclos en la ruta de activación; por seguridad seguimos cuidando de no dejar una ventana entre el cambio de estado y la señal en la que el estado pudiera revertirse. En nuestro refactor, el único thread que puede revertir el estado también opera bajo el mutex, por lo que la ventana está cerrada.

Si no tienes claro qué hacer, haz la señal con el mutex tomado. Es la opción predeterminada más segura y el coste es despreciable.

### Verificación del refactor

Compila el nuevo driver y cárgalo en un kernel `WITNESS`. Ejecuta el kit de pruebas de estrés del Capítulo 11. Tres cosas deberían ocurrir:

- Todas las pruebas pasan con la misma semántica de recuento de bytes que antes.
- `dmesg` está en silencio. Sin nuevas advertencias.
- `procstat -kk` sobre un lector dormido muestra ahora la descripción del cv en la columna del canal de espera. Las herramientas que informan de `wmesg` truncan a `WMESGLEN` (ocho caracteres, definido en `/usr/src/sys/sys/user.h`); una descripción de `"myfirst data"` aparece por tanto como `"myfirst "` en `procstat` y `ps`. La cadena de descripción completa sigue siendo visible para `dtrace` (que lee `cv_description` directamente) y en el código fuente. Si quieres que la forma truncada sea más informativa, elige descripciones más cortas como `"mfdata"` y `"mfroom"`; el capítulo conserva los nombres más largos y legibles porque dtrace y el código fuente usan la cadena completa, y es ahí donde pasas la mayor parte del tiempo depurando.

`lockstat(1)` mostrará menos eventos de cv de los que el antiguo mecanismo `wakeup` producía, porque la señalización por condición no despierta threads que no tienen nada que hacer. Esta es la mejora de rendimiento que esperábamos.

### Un modelo mental: cómo se desarrolla un cv_wait

Para quienes aprenden mejor con una imagen paso a paso, aquí está la secuencia de eventos cuando un thread llama a `cv_wait_sig` y es señalizado posteriormente.

Tiempo t=0: el thread A está en `myfirst_read`. El cbuf está vacío.

Tiempo t=1: el thread A llama a `MYFIRST_LOCK(sc)`. Se adquiere el mutex. El thread A es ahora el único thread en cualquier sección crítica protegida por `sc->mtx`.

Tiempo t=2: el thread A entra en el helper de espera. La comprobación `cbuf_used(&sc->cb) == 0` es verdadera. El thread A llama a `cv_wait_sig(&sc->data_cv, &sc->mtx)`.

Tiempo t=3: dentro de `cv_wait_sig`, el kernel toma el spinlock de la cola del cv para `data_cv`, incrementa `data_cv.cv_waiters`, y atómicamente hace dos cosas: suelta `sc->mtx` y añade el thread A a la cola de espera del cv. El estado del thread A cambia a "dormido en data_cv".

Tiempo t=4: el thread A es desplanificado. La CPU ejecuta otros threads.

Tiempo t=5: el thread B entra en `myfirst_write` desde otro proceso. El thread B llama a `MYFIRST_LOCK(sc)`. El mutex está libre en ese momento; el thread B lo adquiere.

Tiempo t=6: el thread B lee desde el espacio de usuario (`uiomove`), confirma bytes en el cbuf y actualiza contadores. El thread B llama a `MYFIRST_UNLOCK(sc)`.

Tiempo t=7: el thread B llama a `cv_signal(&sc->data_cv)`. El kernel toma el spinlock de la cola del cv, encuentra al thread A en la cola de espera, decrementa `cv_waiters`, elimina al thread A de la cola y lo marca como ejecutable.

Tiempo t=8: el planificador decide que el thread A es el thread ejecutable de mayor prioridad (o uno de varios; FIFO entre iguales). El thread A se planifica en una CPU.

Tiempo t=9: el thread A se reanuda dentro de `cv_wait_sig`. La función vuelve a adquirir `sc->mtx` (esto puede bloquearse a su vez si otro thread tiene el mutex en ese momento; en ese caso, el thread A se añade a la lista de espera del mutex). El thread A retorna de `cv_wait_sig` con valor de retorno 0 (activación normal).

Tiempo t=10: el thread A continúa en el helper de espera. La comprobación `while (cbuf_used(&sc->cb) == 0)` es ahora falsa (el thread B añadió bytes). El bucle termina.

Tiempo t=11: el thread A lee del cbuf y continúa.

Tres lecciones que llevarse de esta imagen. Primera: el estado del lock es consistente en cada paso. El mutex está tomado por exactamente un thread o por ninguno; la visión del mundo que tiene el thread A es la misma antes de la espera que después. Segunda: la activación está desacoplada de la planificación real; el thread B no entregó la CPU directamente al thread A. Tercera: hay una ventana entre t=9 y t=10 en la que el thread A tiene el mutex y otro escritor podría (si hubiera estado esperando) rellenar el buffer más. Eso está bien; la comprobación del thread A es sobre el estado del cbuf en t=10, no en t=7.

Esta secuencia es el patrón canónico de "esperar, señalar, despertar, volver a comprobar, continuar". Cada uso del cv en el capítulo es una instancia de él.

### Un vistazo a kern_condvar.c

Si tienes diez minutos, abre `/usr/src/sys/kern/kern_condvar.c` y échale un vistazo. Tres funciones merecen especial atención:

`cv_init` (al inicio del archivo): muy corta. Solo inicializa la descripción y pone a cero el contador de waiters.

`_cv_wait` (en medio del archivo): el primitivo de bloqueo central. Toma el spinlock de la cola del cv, incrementa `cv_waiters`, suelta el interlock del llamador, invoca la maquinaria de la cola de sueño para encolar el thread y ceder la CPU, y al retornar decrementa `cv_waiters` y vuelve a adquirir el interlock. El soltado y el sueño atómicos los realiza la capa de la cola de sueño, exactamente la misma maquinaria que respalda a `mtx_sleep`. No hay nada mágico en el cv encima de las colas de sueño; es una interfaz fina y con nombre.

`cv_signal` y `cv_broadcastpri`: cada uno toma el spinlock de la cola del cv, encuentra a uno (o a todos) los waiters y usa `sleepq_signal` o `sleepq_broadcast` para despertarlos.

La conclusión: las variables de condición son una capa fina y estructurada sobre los mismos primitivos de cola de sueño que usa `mtx_sleep`. No son más lentas; no son más rápidas; son más claras.

### Cerrando la sección 2

El refactor de esta sección otorga a cada condición de espera su propio objeto, su propio nombre, su propia cola de waiters y su propia señal. El driver se comporta igual de cara al espacio de usuario, pero ahora se lee con más honestidad: `cv_signal(&sc->room_cv)` dice "hay espacio", que es lo que queremos decir. La disciplina `WITNESS` se preserva; las llamadas a `mtx_assert` en los helpers siguen cumpliendo; el kit de pruebas sigue pasando. Hemos subido un nivel en el vocabulario de sincronización sin perder ninguna de las garantías de seguridad que construyó el Capítulo 11.

La sección 3 aborda la pregunta ortogonal de *¿cuánto tiempo debemos esperar?*. El bloqueo indefinido es cómodo para la implementación, pero cruel con el usuario. Las esperas con tiempo límite y las esperas sensibles a señales son la manera en que un driver bien comportado responde al mundo.



## Sección 3: gestión de timeouts y sueño interrumpible

Un primitivo de bloqueo es, por defecto, indefinido. Un lector que llama a `cv_wait_sig` cuando el buffer está vacío dormirá hasta que alguien llame a `cv_signal` (o `cv_broadcast`) sobre el mismo cv, o hasta que se entregue una señal al proceso del lector. Desde el punto de vista del kernel, "indefinido" es una respuesta perfectamente respetable. Desde el punto de vista del usuario, "indefinido" es un cuelgue.

Esta sección trata de las dos maneras en que los primitivos de sincronización de FreeBSD permiten acotar una espera: mediante un timeout de reloj de pared y mediante una interrupción provocada por una señal. Ambas son sencillas de usar y ambas tienen reglas sorprendentemente sutiles en torno a los valores de retorno. Empezamos por la más fácil y avanzamos desde ahí.

### Qué sale mal con los sueños indefinidos

Tres problemas reales nos empujan a usar esperas con tiempo límite e interrumpibles en un driver.

**Programas colgados.** Un usuario ejecuta `cat /dev/myfirst` en un terminal. No hay productor. El `cat` se bloquea en `read(2)`, que se bloquea en `myfirst_read`, que se bloquea en `cv_wait_sig`. El usuario pulsa Ctrl-C. Si la espera es interrumpible (la variante `_sig`), el kernel entrega `EINTR` y el usuario recupera su shell. Si no lo es (`cv_wait` sin `_sig`), el kernel ignora la señal y el usuario tiene que usar Ctrl-Z y `kill %1` desde otro terminal. La mayoría de los usuarios no sabe cómo hacer eso. Y terminan buscando el botón de reinicio.

**Progreso bloqueado.** Un driver de dispositivo espera una interrupción que nunca llega porque el hardware está atascado. El thread de I/O del driver duerme indefinidamente. El sistema entero se llena poco a poco de procesos bloqueados en este driver. Con el tiempo un administrador lo nota, pero para entonces no queda más remedio que reiniciar. Una espera acotada lo habría detectado mucho antes.

**Mala experiencia de usuario.** Un protocolo de red espera una respuesta en un tiempo determinado. Una operación de almacenamiento espera una finalización dentro de un acuerdo de nivel de servicio. Ninguna de las dos se sirve bien con un primitivo que puede esperar eternamente. El driver debe ser capaz de imponer un plazo y devolver un error limpio cuando se supere.

Los primitivos de FreeBSD que resuelven estos problemas son `cv_wait_sig` y `cv_timedwait_sig`, mientras que la familia más antigua de `mtx_sleep` y `tsleep` ofrece las mismas capacidades con una forma diferente. Ya conocemos `cv_wait_sig` de la sección 2. Aquí examinamos con más cuidado lo que nos dice su valor de retorno y cómo añadir un timeout explícito.

### Los tres resultados posibles de una espera

Cualquier primitivo de sueño bloqueante puede retornar por una de estas tres razones:

1. **Un despertar normal.** Otro thread llamó a `cv_signal`, `cv_broadcast` o (en la API heredada) `wakeup`. La condición que este thread estaba esperando ha cambiado, y el thread debería volver a comprobarla.
2. **Se entregó una señal al proceso.** Se le pide al thread que abandone la espera para que pueda ejecutarse el manejador de señal. El driver normalmente devuelve `EINTR` al espacio de usuario, que es también el valor de retorno de la primitiva de sleep.
3. **Se disparó un timeout.** El thread esperaba con un plazo máximo y éste expiró antes de que llegara ningún despertar. La primitiva de sleep devuelve `EWOULDBLOCK`.

La tarea del driver es determinar cuál de los tres ha ocurrido y responder en consecuencia.

El primer caso es el más sencillo. El thread vuelve a comprobar su condición (el bucle `while` alrededor de `cv_wait_sig` es lo que hace esto); si la condición es ahora verdadera, el bucle termina y la I/O continúa; si no, el thread vuelve a dormir.

El segundo caso es más interesante. El kernel entrega una señal no a un *thread* sino a un *proceso*. Una señal puede ser una condición grave (`SIGTERM`, `SIGKILL`) o rutinaria (`SIGINT` por Ctrl-C, `SIGALRM` de un temporizador). El thread que estaba durmiendo necesita volver al espacio de usuario con prontitud para que el manejador de señal pueda ejecutarse. La convención es que la primitiva de sleep devuelve `EINTR` (llamada al sistema interrumpida), el driver devuelve `EINTR` desde su manejador, y el kernel o bien reinicia la llamada al sistema (si el manejador devolvió con `SA_RESTART`) o devuelve `EINTR` al espacio de usuario (en caso contrario).

El tercer caso es el de espera acotada. El driver suele traducir `EWOULDBLOCK` en `EAGAIN` (vuelve a intentarlo más tarde) o en un error más específico (`ETIMEDOUT`, cuando corresponda).

### EINTR, ERESTART y la pregunta sobre el reinicio

Hay una sutileza en el caso 2 que vale la pena entender antes de que el capítulo avance más.

Cuando `cv_wait_sig` es interrumpido por una señal, el valor de retorno real es una de estas dos cosas:

- `EINTR` si la disposición de la señal es "no reiniciar las llamadas al sistema". El kernel devuelve `EINTR` al espacio de usuario, y `read(2)` reporta `-1` con `errno == EINTR`. El programa de usuario es responsable de reintentar si así lo desea.
- `ERESTART` si la disposición de la señal es "reiniciar las llamadas al sistema" (el flag `SA_RESTART`). El kernel vuelve a entrar en la syscall de forma transparente y la espera ocurre de nuevo. El programa de usuario no percibe la interrupción.

Un driver no debe devolver `ERESTART` directamente al espacio de usuario; es un marcador interno de la capa de syscalls. Si el driver devuelve `ERESTART` desde su manejador, la capa de syscalls sabe que debe reiniciar. Si el driver devuelve `EINTR`, la capa de syscalls devuelve `EINTR` al espacio de usuario.

La convención que sigue la mayoría de los drivers: propagar el valor de retorno de `cv_wait_sig` sin modificarlo. Si se obtuvo `EINTR`, el driver devuelve `EINTR`. Si se obtuvo `ERESTART`, el driver devuelve `ERESTART`. El kernel se encarga del resto. El driver del Capítulo 11 hacía esto de forma implícita; la refactorización del Capítulo 12 en la Sección 2 continúa haciéndolo:

```c
error = cv_wait_sig(&sc->data_cv, &sc->mtx);
if (error != 0)
        return (error);
```

Devolver `error` directamente es la decisión correcta. El Capítulo 12 no cambia nada respecto a esta regla; simplemente la hace visible en las nuevas APIs.

### Añadir un timeout al camino de lectura

Ahora el caso de espera acotada. Supongamos que queremos que `myfirst_read` espere opcionalmente como máximo durante una duración configurable antes de devolver `EAGAIN` si no llegan datos. (Usamos `EAGAIN` en lugar de `ETIMEDOUT` porque `EAGAIN` es la respuesta UNIX convencional a "la operación bloquearía; inténtalo de nuevo más tarde".)

El driver necesita tres cosas:

1. Un valor de configuración para el timeout (en milisegundos, por ejemplo). Cero significa "bloquear indefinidamente como antes".
2. Una forma de convertir el timeout a ticks, ya que `cv_timedwait_sig` recibe su argumento `timo` en ticks.
3. Un bucle que gestione correctamente los tres resultados posibles: despertar normal, interrupción por señal y timeout.

Añade el campo de configuración al softc:

```c
int     read_timeout_ms;  /* 0 = no timeout */
```

Inicialízalo en attach:

```c
sc->read_timeout_ms = 0;
```

Exponlo como un sysctl:

```c
SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "read_timeout_ms", CTLFLAG_RW,
    &sc->read_timeout_ms, 0,
    "Read timeout in milliseconds (0 = block indefinitely)");
```

Usamos un `SYSCTL_ADD_INT` simple por ahora; el valor es un entero, la lectura es atómica a nivel de palabra en amd64, y un valor ligeramente desactualizado es aceptable. (La Sección 5 nos dará una forma más disciplinada de gestionar los cambios de configuración.)

Actualiza el helper de espera:

```c
static int
myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error, timo;

        MYFIRST_ASSERT(sc);
        while (cbuf_used(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore)
                        return (-1);
                if (ioflag & IO_NDELAY)
                        return (EAGAIN);

                timo = sc->read_timeout_ms;
                if (timo > 0) {
                        int ticks_total = (timo * hz + 999) / 1000;
                        error = cv_timedwait_sig(&sc->data_cv, &sc->mtx,
                            ticks_total);
                } else {
                        error = cv_wait_sig(&sc->data_cv, &sc->mtx);
                }
                if (error == EWOULDBLOCK)
                        return (EAGAIN);
                if (error != 0)
                        return (error);
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}
```

Algunos detalles merecen comentario.

La aritmética `(timo * hz + 999) / 1000` convierte milisegundos a ticks redondeando hacia arriba. Queremos al menos la espera solicitada, nunca menos. Un timeout de 1 ms en un kernel a 1000 Hz se convierte en 1 tick. Un timeout de 1 ms en un kernel a 100 Hz se convierte en 1 tick (redondeado hacia arriba desde 0,1). Un timeout de 5500 ms se convierte en 5500 ticks a 1000 Hz, o 550 a 100 Hz.

La rama sobre `timo > 0` elige `cv_timedwait_sig` cuando se solicita un timeout positivo, y `cv_wait_sig` (sin timeout) cuando no. Podríamos llamar siempre a `cv_timedwait_sig` con `timo = 0`, pero la API de cv trata `timo = 0` como "esperar indefinidamente", y el comportamiento es idéntico al de `cv_wait_sig`. La rama explícita hace la intención más clara para quien lea el código.

La traducción `EWOULDBLOCK -> EAGAIN` proporciona al espacio de usuario la indicación convencional de "inténtalo de nuevo". Un programa de usuario que recibe `EAGAIN` sabe qué hacer; un programa que recibe `ETIMEDOUT` tendría que aprender un nuevo código de error.

La re-comprobación de `is_attached` tras cada sleep permanece. Incluso con una espera acotada, el dispositivo podría haberse desconectado durante el sleep; el cv broadcast en detach (añadido en la Sección 2) nos despierta; el propio timeout no omite las comprobaciones posteriores al sleep.

Aplica el cambio simétrico a `myfirst_wait_room` si quieres escrituras acotadas, con un sysctl `write_timeout_ms` separado. El código fuente de acompañamiento hace ambas cosas.

### Verificar el timeout

Un pequeño programa de prueba en espacio de usuario confirma el nuevo comportamiento. Ajusta el timeout a 100 ms, abre el dispositivo sin ningún productor y realiza una lectura. Deberías ver que `read(2)` devuelve `-1` con `errno == EAGAIN` tras aproximadamente 100 ms, sin bloquearse indefinidamente.

```c
/* timeout_tester.c: confirm bounded reads. */
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>

#define DEVPATH "/dev/myfirst"

int
main(void)
{
        int timeout_ms = 100;
        size_t sz = sizeof(timeout_ms);

        if (sysctlbyname("dev.myfirst.0.read_timeout_ms",
            NULL, NULL, &timeout_ms, sz) != 0)
                err(1, "sysctlbyname set");

        int fd = open(DEVPATH, O_RDONLY);
        if (fd < 0)
                err(1, "open");

        char buf[1024];
        struct timeval t0, t1;
        gettimeofday(&t0, NULL);
        ssize_t n = read(fd, buf, sizeof(buf));
        gettimeofday(&t1, NULL);
        int saved = errno;

        long elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1000 +
            (t1.tv_usec - t0.tv_usec) / 1000;
        printf("read returned %zd, errno=%d (%s) after %ld ms\n",
            n, saved, strerror(saved), elapsed_ms);

        close(fd);
        return (0);
}
```

Ejecuta sin ningún productor conectado. El resultado esperado es similar a:

```text
read returned -1, errno=35 (Resource temporarily unavailable) after 102 ms
```

El errno 35 en FreeBSD es `EAGAIN`. Los 102 ms son los 100 ms de timeout más un par de milisegundos de jitter de planificación.

Restablece el sysctl a cero (`sysctl -w dev.myfirst.0.read_timeout_ms=0`) y vuelve a ejecutar. Ahora `read(2)` se bloquea hasta que pulses Ctrl-C, momento en el que devuelve `-1` con `errno == EINTR`. La capacidad de interrupción (el sufijo `_sig`) y el timeout (la variante `_timedwait`) son capacidades independientes. Podemos tener ninguna, cualquiera de ellas o ambas, y la API expone cada una con su propio interruptor.

### Elegir entre EAGAIN y ETIMEDOUT

Cuando se dispara el timeout, el driver elige qué error reportar. Las dos opciones razonables son `EAGAIN` y `ETIMEDOUT`.

`EAGAIN` (valor errno 35 en FreeBSD; el valor simbólico `EWOULDBLOCK` está `#define`d al mismo número en `/usr/src/sys/sys/errno.h`) es la respuesta UNIX convencional a "la operación bloquearía". Los programas de usuario que gestionan `O_NONBLOCK` lo entienden. Muchos programas de usuario ya reintentan ante `EAGAIN`. Devolver `EAGAIN` para un timeout es un valor predeterminado seguro; funciona correctamente para la mayoría de los llamadores.

`ETIMEDOUT` (valor errno 60 en FreeBSD) es más específico: "la operación tiene un plazo y el plazo ha expirado". Los protocolos de red lo usan; significa algo diferente a "bloquearía ahora mismo". Un programa de usuario que quiera distinguir "todavía no hay datos, reintenta" de "no hay datos tras el plazo acordado, abandona" necesita `ETIMEDOUT`.

Para `myfirst`, usamos `EAGAIN`. El driver no tiene un contrato de plazo con el llamador; el timeout es una cortesía, no una garantía. Otros drivers pueden tomar la otra decisión; ambas son válidas.

### Una nota sobre la equidad con timeouts

La espera acotada no cambia la historia de equidad del cv. `cv_timedwait_sig` se implementa sobre la misma cola de sleep que usa `cv_wait_sig`. Cuando llega un wakeup, la cola de sleep elige al waiter de mayor prioridad (FIFO entre prioridades iguales) independientemente de si cada waiter tiene un timeout pendiente. El timeout es un watchdog por waiter; no afecta al orden en que se despiertan los waiters que no han agotado su tiempo.

Consecuencia práctica: un thread con un timeout de 50 ms y un thread sin timeout, ambos esperando en el mismo cv, serán despertados por `cv_signal` en el orden que elija la cola de sleep. El thread con timeout no obtiene prioridad. Si necesitas que el waiter con timeout sea despertado primero, tienes un problema de diseño diferente (colas de prioridad, cvs separados por clase de prioridad) que va más allá de este capítulo.

Para `myfirst`, todos los lectores son equivalentes y la falta de priorización basada en timeout es perfectamente válida.

### Cuándo usar un timeout

Los timeouts no son gratuitos. Cada espera acotada configura un callout que dispara el wakeup del cv si no llega ningún wakeup real antes. El callout tiene un pequeño coste por tick y contribuye a la presión global de callouts del kernel. Los drivers que usan timeouts en cada llamada bloqueante generan más tráfico de callouts que los drivers que bloquean indefinidamente.

Tres reglas prácticas:

- Usa un timeout cuando el llamador tiene un plazo real (un protocolo de red, un watchdog de hardware, una respuesta visible por el usuario).
- Usa un timeout cuando la espera necesite un respaldo de tipo "esto no debería ser posible". Un driver que "por si acaso" establece un timeout de 60 segundos en una espera que normalmente debería completarse en microsegundos está usando el timeout como comprobación de cordura, no como plazo. Eso es válido.
- No uses un timeout cuando la espera sea naturalmente indefinida (un `cat /dev/myfirst` en un dispositivo inactivo debería bloquearse hasta que lleguen datos o el usuario decida abandonar; cualquiera de los dos está bien y ninguno necesita un timeout).

El driver `myfirst` en este capítulo expone un sysctl por dispositivo que permite al usuario elegir. El valor predeterminado de cero (bloquear indefinidamente) es el correcto para un pseudo-dispositivo. Los drivers reales pueden tener criterios más firmes.

### Precisión sub-tick con sbintime_t

Los macros `cv_timedwait` y `cv_timedwait_sig` reciben su timeout en ticks. Un tick en FreeBSD 14.3 es típicamente un milisegundo (porque `hz=1000` es el valor por defecto), de modo que la precisión de tick equivale a precisión de milisegundo. Para la mayoría de los casos de uso en drivers esto es suficiente. Los drivers de red y almacenamiento ocasionalmente necesitan precisión de microsegundo, y las variantes `_sbt` (scaled binary time) son la forma de conseguirla.

Los primitivos relevantes:

```c
int  cv_timedwait_sbt(struct cv *cvp, struct mtx *mtx,
         sbintime_t sbt, sbintime_t pr, int flags);
int  cv_timedwait_sig_sbt(struct cv *cvp, struct mtx *mtx,
         sbintime_t sbt, sbintime_t pr, int flags);
int  msleep_sbt(void *chan, struct mtx *mtx, int pri,
         const char *wmesg, sbintime_t sbt, sbintime_t pr, int flags);
```

El argumento `sbt` es el timeout, expresado como `sbintime_t` (un entero de 64 bits donde los 32 bits superiores son segundos y los 32 bits inferiores son una fracción binaria de segundo). El argumento `pr` es la precisión: el margen que se le permite al kernel al planificar el temporizador (se usa para la coalescencia de interrupciones de temporizador en favor del ahorro energético). El argumento `flags` es uno de `C_HARDCLOCK`, `C_ABSOLUTE`, `C_DIRECT_EXEC`, etc., que controlan cómo se registra el temporizador.

Para un timeout de 250 microsegundos:

```c
sbintime_t sbt = 250 * SBT_1US;  /* 250 microseconds */
int err = cv_timedwait_sig_sbt(&sc->data_cv, &sc->mtx, sbt,
    SBT_1US, C_HARDCLOCK);
```

La constante `SBT_1US` (definida en `/usr/src/sys/sys/time.h`) es un microsegundo como `sbintime_t`. Multiplicarla por 250 da 250 microsegundos. El argumento de precisión `SBT_1US` indica "acepto una precisión de un microsegundo"; el kernel no coalescerá este temporizador con otros que disten más de 1 microsegundo.

Para 5 segundos:

```c
sbintime_t sbt = 5 * SBT_1S;
int err = cv_timedwait_sig_sbt(&sc->data_cv, &sc->mtx, sbt,
    SBT_1MS, C_HARDCLOCK);
```

Espera de cinco segundos con precisión de milisegundo. El kernel puede coalescer hasta 1 ms.

Para la mayor parte del código de drivers, la API de tick en milisegundos (`cv_timedwait_sig` con un número de ticks) es el nivel de precisión adecuado. Recurre a `_sbt` cuando tengas una razón real: un protocolo de red con temporización submilisegundo, un controlador de hardware con un watchdog en escala de microsegundos, una medición en la que el propio sleep contribuye al resultado.

### Qué ocurre dentro de cv_timedwait_sig

Conceptualmente, `cv_timedwait_sig` hace lo mismo que `cv_wait_sig`, pero además programa un callout que disparará la señal del cv si no llega ninguna señal real antes. La implementación reside en `/usr/src/sys/kern/kern_condvar.c` en `_cv_timedwait_sig_sbt`. Hay tres observaciones que vale la pena llevarse.

Primera: el callout se registra mientras se mantiene el mutex de interbloqueo, y luego el thread duerme. Si el callout se dispara mientras el thread está durmiendo, el kernel marca el thread como despertado-por-timeout. El thread regresa del sleep con `EWOULDBLOCK`.

Segunda: si llega un `cv_signal` real antes del timeout, el callout se cancela cuando el thread despierta. La cancelación es técnicamente racy (el callout podría dispararse justo después de que el thread despierte por una razón real), pero el kernel lo gestiona comprobando si el thread sigue durmiendo cuando el callout se dispara; si no es así, el callout no tiene efecto.

Tercera: cada espera acotada crea y desmonta un callout. En un sistema con miles de esperas acotadas concurrentes, la maquinaria de callouts se convierte en un coste apreciable. Para un único driver con a lo sumo unas pocas docenas de waiters, el coste es despreciable.

Estos detalles no son algo que necesites memorizar. Sin embargo, explican por qué un driver que usa esperas acotadas en todas partes puede mostrar más actividad en el subsistema de callouts que un driver que usa esperas indefinidas con un thread watchdog separado. Si alguna vez te preguntas por qué tu driver genera muchos eventos de callout, las esperas acotadas son una causa probable.

### Cerrando la Sección 3

Las esperas acotadas y las esperas interrumpibles son las dos formas en que un primitivo de sleep del kernel coopera con el mundo exterior. Añadimos ambas a los caminos de bloqueo de `myfirst`: `cv_wait_sig` ya estaba; `cv_timedwait_sig` es la nueva incorporación, controlada por un sysctl. El programa de prueba en espacio de usuario confirma que tanto un Ctrl-C como un plazo de 100 milisegundos producen el valor de retorno esperado; el driver reporta `EINTR` y `EAGAIN` respectivamente.

La Sección 4 aborda una forma de sincronización completamente diferente: los locks compartidos/exclusivos, donde muchos threads pueden leer a la vez y solo los escritores deben esperar su turno.



## Sección 4: El lock sx(9): acceso compartido y exclusivo

El mutex `myfirst` que utilizamos hoy protege el cbuf, que posee un invariante compuesto. Para esa tarea, es la primitiva adecuada. Sin embargo, no todo el estado tiene un invariante compuesto. Hay estado que se lee con frecuencia, se escribe raramente y nunca abarca más que el campo concreto que se está leyendo. Para ese estado, serializar a todos los lectores a través de un mutex es una serialización desperdiciada. Un reader-writer lock encaja mejor con ese patrón.

Esta sección presenta `sx(9)`, el lock compartido/exclusivo con soporte de sleep de FreeBSD. Primero explicaremos qué significa compartido/exclusivo y por qué importa, después recorreremos la API, luego hablaremos brevemente del hermano en modo spin, `rw(9)`, y terminaremos con las reglas que distinguen a ambos y sitúan a cada uno en su contexto adecuado.

### Qué significan shared y exclusive

Un **shared lock** (también denominado *lock de lectura*) permite que múltiples threads lo retengan de forma simultánea. Un thread que retiene el lock en modo shared tiene la garantía de que ningún otro thread lo retiene en modo *exclusive* en ese momento. Los titulares en modo shared pueden ejecutarse de forma concurrente; no interfieren entre sí.

Un **exclusive lock** (también denominado *lock de escritura*) es retenido por exactamente un thread en cada momento. Un thread que retiene el lock en modo exclusive tiene la garantía de que ningún otro thread lo retiene en ningún modo.

Un lock puede transicionar entre modos en dos direcciones:

- **Downgrade**: un titular de un exclusive lock puede convertirlo a shared lock sin liberarlo. La conversión no es bloqueante; inmediatamente después, el titular original sigue reteniendo el lock (ahora en modo shared) y otros lectores pueden proceder.
- **Upgrade**: un titular de un shared lock puede intentar convertirlo a exclusive lock. El intento puede fallar si aún hay otros titulares en modo shared. La primitiva estándar es `sx_try_upgrade`, que devuelve éxito o fracaso en lugar de bloquearse.

La asimetría del upgrade (intentar, puede fallar) refleja una dificultad fundamental: si múltiples titulares en modo shared intentan hacer el upgrade a la vez, entrarían en deadlock esperándose mutuamente. El `sx_try_upgrade` no bloqueante permite que uno tenga éxito mientras los demás fallan y deben liberar el lock y volver a adquirirlo en modo exclusive.

Los shared/exclusive locks son la primitiva adecuada cuando el patrón de acceso es *muchos lectores, escritor ocasional*. Ejemplos en el kernel de FreeBSD incluyen el lock de espacio de nombres para sysctl, el lock de espacio de nombres para módulos del kernel, los locks de superbloque de sistemas de archivos, y muchos locks de estado de configuración en drivers de red.

### Por qué shared/exclusive supera a un mutex simple en este caso

Imagina un fragmento de estado del driver, el «nivel de verbosidad de depuración actual», leído al inicio de cada llamada I/O para decidir si registrar ciertos eventos, y modificado quizás una vez por hora mediante un sysctl. Con el diseño de mutex del capítulo 11:

- Cada llamada I/O adquiere el mutex, lee la verbosidad y lo libera.
- Cada llamada I/O se serializa en el mutex frente a la comprobación de verbosidad de todas las demás llamadas I/O.
- El mutex sufre una contención enorme aunque nadie compite por el *estado* subyacente (todos se limitan a leer).

Con un diseño basado en `sx`:

- Cada llamada I/O adquiere el lock en modo shared (barato en un sistema multinúcleo; la ruta rápida se reduce a unas pocas operaciones atómicas sin intervención del planificador).
- Múltiples llamadas I/O pueden retener el lock de forma concurrente. No se bloquean entre sí.
- El escritor del sysctl toma el lock en modo exclusive ocasionalmente, excluyendo brevemente a los lectores. Los lectores vuelven a intentar la adquisición como titulares en modo shared una vez que el escritor lo libera.

Para una carga de trabajo con predominio de lecturas, la diferencia es notable. El coste de serialización del mutex crece con el número de núcleos; el coste del modo shared de sx permanece constante.

La contrapartida: `sx_xlock` es más costoso por adquisición que `mtx_lock`, porque el lock es más elaborado internamente. Para un estado que solo se lee una vez y cuyos lectores no compiten, `mtx` sigue siendo mejor. El punto de equilibrio depende de la carga de trabajo, pero la regla general es *usa sx cuando los lectores son muchos y los escritores son escasos; usa mtx cuando el patrón de acceso es simétrico o dominado por escrituras*.

### La API de sx(9)

Las funciones de sx(9) se encuentran en `/usr/src/sys/sys/sx.h` y `/usr/src/sys/kern/kern_sx.c`. La API pública es pequeña.

```c
void  sx_init(struct sx *sx, const char *description);
void  sx_init_flags(struct sx *sx, const char *description, int opts);
void  sx_destroy(struct sx *sx);

void  sx_xlock(struct sx *sx);
int   sx_xlock_sig(struct sx *sx);
void  sx_xunlock(struct sx *sx);
int   sx_try_xlock(struct sx *sx);

void  sx_slock(struct sx *sx);
int   sx_slock_sig(struct sx *sx);
void  sx_sunlock(struct sx *sx);
int   sx_try_slock(struct sx *sx);

int   sx_try_upgrade(struct sx *sx);
void  sx_downgrade(struct sx *sx);

void  sx_unlock(struct sx *sx);  /* polymorphic: shared or exclusive */
void  sx_assert(struct sx *sx, int what);

int   sx_xlocked(struct sx *sx);
struct thread *sx_xholder(struct sx *sx);
```

Las variantes `_sig` son interrumpibles por señales; devuelven `EINTR` o `ERESTART` si se recibe una señal durante la espera. Las variantes sin `_sig` se bloquean de forma ininterrumpible. Los drivers que retienen un sx lock durante una operación larga deberían considerar las variantes `_sig` por la misma razón por la que se prefiere `cv_wait_sig` sobre `cv_wait`: un Ctrl-C debería ser capaz de interrumpir la espera.

Los flags aceptados por `sx_init_flags` son:

- `SX_DUPOK`: permite que el mismo thread adquiera el lock varias veces (principalmente una directiva de `WITNESS`).
- `SX_NOWITNESS`: no registra el lock en `WITNESS` (úsalo con moderación; es preferible registrarlo y documentar cualquier excepción).
- `SX_RECURSE`: permite la adquisición recursiva por el mismo thread; el lock se libera solo cuando cada adquisición tiene su correspondiente liberación.
- `SX_QUIET`, `SX_NOPROFILE`: desactivan diversas instrumentaciones de depuración.
- `SX_NEW`: declara que la memoria es nueva (omite la comprobación de inicialización previa).

Para la mayoría de los casos de uso en drivers, `sx_init(sx, "name")` sin flags es el valor predeterminado correcto.

`sx_assert(sx, what)` comprueba el estado del lock y provoca un panic bajo `INVARIANTS` si la aserción falla. El argumento `what` puede ser uno de los siguientes:

- `SA_LOCKED`: el lock está retenido en algún modo por el thread que llama.
- `SA_SLOCKED`: el lock está retenido en modo shared.
- `SA_XLOCKED`: el lock está retenido en modo exclusive por el thread que llama.
- `SA_UNLOCKED`: el lock no está retenido por el thread que llama.
- `SA_RECURSED`, `SA_NOTRECURSED`: comprueba el estado de recursión.

Usa `sx_assert` con generosidad dentro de las funciones auxiliares que esperan un estado de lock concreto, de la misma forma en que el capítulo 11 usaba `mtx_assert`.

### Un ejemplo práctico rápido

Supongamos que tenemos una struct que almacena la configuración del driver:

```c
struct myfirst_config {
        int     debug_level;
        int     soft_byte_limit;
        char    nickname[32];
};
```

La mayoría de las lecturas de estos campos se producen en la ruta de datos (cada `myfirst_read` y `myfirst_write` comprueba `debug_level`). Las escrituras ocurren con poca frecuencia, desde los manejadores de sysctl.

Añade un sx lock al softc:

```c
struct sx               cfg_sx;
struct myfirst_config   cfg;
```

Inicialización y destrucción:

```c
sx_init(&sc->cfg_sx, "myfirst cfg");
/* in detach: */
sx_destroy(&sc->cfg_sx);
```

Lectura en la ruta de datos:

```c
static bool
myfirst_debug_enabled(struct myfirst_softc *sc, int level)
{
        bool enabled;

        sx_slock(&sc->cfg_sx);
        enabled = (sc->cfg.debug_level >= level);
        sx_sunlock(&sc->cfg_sx);
        return (enabled);
}
```

Escritura desde un manejador de sysctl:

```c
static int
myfirst_sysctl_debug_level(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, error;

        sx_slock(&sc->cfg_sx);
        new = sc->cfg.debug_level;
        sx_sunlock(&sc->cfg_sx);

        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);

        if (new < 0 || new > 3)
                return (EINVAL);

        sx_xlock(&sc->cfg_sx);
        sc->cfg.debug_level = new;
        sx_xunlock(&sc->cfg_sx);
        return (0);
}
```

Hay tres aspectos que conviene observar en el escritor.

Primero, leemos el valor *actual* bajo el shared lock para que el framework de sysctl pueda completar el valor a mostrar cuando no se está estableciendo ningún valor nuevo. Podríamos leerlo sin el lock, pero hacerlo crearía la posibilidad de una lectura parcial del `int` (pequeño, es cierto). El shared lock es barato y explícito.

Segundo, liberamos el shared lock, validamos el nuevo valor con `sysctl_handle_int`, comprobamos el rango y luego adquirimos el exclusive lock para confirmar los cambios. No podemos hacer upgrade de shared a exclusive en esta ruta porque `sx_try_upgrade` puede fallar; implementarlo como una liberación seguida de una nueva adquisición es más sencillo y correcto.

Tercero, la validación ocurre antes del exclusive lock, lo que significa que retenemos el exclusive lock el mínimo tiempo posible. El titular del exclusive lock excluye a todos los lectores; queremos que lo libere lo antes posible.

### Try-upgrade y downgrade

`sx_try_upgrade` es la versión optimista de «tengo un shared lock, dámelo en exclusivo sin que tenga que liberarlo y volver a adquirirlo». Devuelve un valor distinto de cero en caso de éxito (el lock es ahora exclusive) y cero en caso de fallo (el lock sigue siendo shared; otro thread lo retenía en modo shared simultáneamente y el kernel no pudo promoverlo de forma segura).

El patrón:

```c
sx_slock(&sc->cfg_sx);
/* do some reading */
if (need_to_modify) {
        if (sx_try_upgrade(&sc->cfg_sx)) {
                /* now exclusive; modify */
                sx_downgrade(&sc->cfg_sx);
                /* back to shared; continue reading */
        } else {
                /* upgrade failed; drop and reacquire */
                sx_sunlock(&sc->cfg_sx);
                sx_xlock(&sc->cfg_sx);
                /* now exclusive but our prior view may be stale */
                /* re-validate and modify */
                sx_downgrade(&sc->cfg_sx);
        }
}
sx_sunlock(&sc->cfg_sx);
```

`sx_downgrade` siempre tiene éxito: un titular en modo exclusive siempre puede descender a shared sin bloquearse, porque no puede haber ningún otro escritor presente (éramos los titulares del exclusive) y los lectores existentes habrían tenido que adquirir sus shared locks mientras nosotros reteníamos el exclusive (cosa que no podían hacer), por lo que tampoco pueden existir.

Para la configuración de `myfirst`, no necesitamos upgrade/downgrade: la lectura y la escritura son rutas separadas y el manejador de sysctl está dispuesto a liberar y volver a adquirir. El upgrade/downgrade es más útil en algoritmos donde el mismo thread lee, decide y luego modifica condicionalmente, todo dentro de un único ciclo de adquisición y liberación del lock.

### Comparación entre sx(9) y rw(9)

`rw(9)` es el hermano en modo spin de `sx(9)`. Ambos implementan la idea del modo shared/exclusive. Difieren en cómo esperan un lock no disponible.

`sx(9)` utiliza sleep queues. Un thread que no puede adquirir el lock de inmediato se coloca en una sleep queue y cede el control del procesador. Otros threads se ejecutan en el CPU. Cuando el lock se libera, el kernel despierta al thread con mayor prioridad en espera, que vuelve a intentarlo.

`rw(9)` utiliza turnstiles, la primitiva del kernel basada en spin que soporta propagación de prioridades. Un thread que no puede adquirir el lock de inmediato hace spin brevemente y luego pasa al mecanismo de turnstile para bloquearse con herencia de prioridades. El bloqueo se realiza de forma que no cede el CPU tan fácilmente como `sx`.

Las diferencias prácticas:

- `sx(9)` es durmiente en sentido estricto: retener un lock `sx` permite llamar a funciones que pueden dormir (`uiomove`, `malloc(... M_WAITOK)`). Retener un lock `rw(9)` no lo permite; el lock `rw(9)` se trata como un spinlock a efectos de bloqueo por sueño.
- `sx(9)` admite variantes `_sig` para esperas interrumpibles por señales. `rw(9)` no las admite.
- `sx(9)` es generalmente apropiado para código en contexto de proceso; `rw(9)` es más apropiado cuando la sección crítica es corta y puede ejecutarse en contexto de interrupción (aunque la elección estricta para el contexto de interrupción sigue siendo `MTX_SPIN`).

Para `myfirst`, todos los accesos a la configuración son desde contexto de proceso, las secciones críticas son cortas pero incluyen llamadas que pueden dormir, y la interrupción por señales es una funcionalidad útil. `sx(9)` es la elección correcta.

Un driver cuya configuración se lea dentro de un manejador de interrupciones tendría que usar `rw(9)` en su lugar, porque `sx_slock` podría dormir y dormir en una interrupción es ilegal. No encontraremos tal driver en este libro hasta partes posteriores.

### La regla del sueño, revisada

El capítulo 11 introdujo la regla «no retener un lock no durmiente mientras dure una operación que puede dormir». Con sx y cv sobre la mesa, la regla necesita un pequeño refinamiento.

La regla completa es: *el lock que retienes determina qué operaciones son legales en la sección crítica.*

- Reteniendo un mutex `MTX_DEF`: la mayoría de las operaciones son legales. El sueño está permitido (con `mtx_sleep`, `cv_wait`). `uiomove`, `copyin`, `copyout` y `malloc(M_WAITOK)` son legales en principio, pero deberían evitarse para mantener las secciones críticas cortas. La convención en drivers es liberar el mutex alrededor de cualquiera de estas llamadas.
- Reteniendo un mutex `MTX_SPIN`: muy pocas operaciones son legales. Sin sueño. Sin `uiomove`. Sin `malloc(M_WAITOK)`. La sección crítica debe ser mínima.
- Reteniendo un lock `sx(9)` (shared o exclusive): similar a `MTX_DEF`. El sueño está permitido. Se aplica la misma convención de «liberar antes de dormir si puedes», pero no existe la prohibición absoluta de dormir.
- Reteniendo un lock `rw(9)`: similar a `MTX_SPIN`. Sin sueño. Sin llamadas de bloqueo prolongado.
- Reteniendo un `cv(9)` (es decir, dentro de `cv_wait`): el mutex de interlock subyacente fue liberado atómicamente por `cv_wait`; en términos de «qué se retiene», no retienes nada.

Este refinamiento dice: `sx` es durmiente, `rw` no lo es. Esa es la diferencia operativa entre ellos. Elige en función de en qué lado de la línea necesita estar tu sección crítica.

### Orden de locks y sx

`WITNESS` realiza un seguimiento del orden de locks en todas las clases: mutexes, sx locks y rw locks. Si tu driver adquiere un lock `sx` mientras retiene un mutex, eso establece un orden: mutex primero, sx después. El orden inverso desde cualquier ruta es una violación; `WITNESS` lo advertirá.

Para `myfirst` en la fase 3 (esta sección), retendremos `sc->mtx` y `sc->cfg_sx` juntos en algunas rutas. Debemos declarar el orden de forma explícita.

El orden natural es *mtx antes que sx*. El motivo: la ruta de datos retiene `sc->mtx` para las operaciones del cbuf; si necesita leer un valor de configuración durante esa sección crítica, adquiriría `sc->cfg_sx` mientras sigue reteniendo `sc->mtx`. El orden inverso (`cfg_sx` primero, `mtx` después) también es posible (un escritor de sysctl que quiera actualizar tanto la configuración como desencadenar un evento podría adquirir `cfg_sx` y luego `mtx`), pero un driver debe elegir un orden y documentarlo.

La sección 5 desarrolla este diseño y codifica la regla.

### Un vistazo a kern_sx.c

Si tienes unos minutos, abre `/usr/src/sys/kern/kern_sx.c` y dale un vistazo. La ruta rápida de `sx_xlock` es una única operación de compare-and-swap sobre la palabra del lock, exactamente la misma estructura que la ruta rápida de `mtx_lock`. La ruta lenta (en `_sx_xlock_hard`) entrega el thread a la sleep queue con propagación de prioridades. La ruta del shared lock (`_sx_slock_int`) es similar, pero actualiza el contador de titulares en modo shared en lugar de establecer el propietario.

Lo que importa para quien escribe drivers es que la ruta rápida sea barata, la ruta lenta sea correcta y que la API tenga la misma forma que la API de mutex que ya conoces. Si puedes usar `mtx_lock`, puedes usar `sx_xlock`; el nuevo vocabulario son las operaciones en modo compartido y las reglas que las rodean.

### Un breve recorrido por rw(9)

Hemos mencionado `rw(9)` varias veces como el hermano en modo spin de `sx(9)`. Aunque nuestro driver no lo utiliza, lo encontrarás en código fuente real de FreeBSD, por lo que merece la pena dedicarle unos minutos.

La API es un espejo de `sx(9)`:

```c
void  rw_init(struct rwlock *rw, const char *name);
void  rw_destroy(struct rwlock *rw);

void  rw_wlock(struct rwlock *rw);
void  rw_wunlock(struct rwlock *rw);
int   rw_try_wlock(struct rwlock *rw);

void  rw_rlock(struct rwlock *rw);
void  rw_runlock(struct rwlock *rw);
int   rw_try_rlock(struct rwlock *rw);

int   rw_try_upgrade(struct rwlock *rw);
void  rw_downgrade(struct rwlock *rw);

void  rw_assert(struct rwlock *rw, int what);
```

Las diferencias con `sx(9)`:

- Los nombres de modo son distintos: `wlock` (escritura/exclusivo) y `rlock` (lectura/compartido) en lugar de `xlock` y `slock`. La misma idea, vocabulario diferente.
- No existen las variantes `_sig`. `rw(9)` no puede ser interrumpido por señales porque se implementa sobre turnstiles, no sobre colas de espera.
- Un thread que mantiene cualquier lock `rw(9)` no puede dormir. Ni `cv_wait`, ni `mtx_sleep`, ni `uiomove`, ni `malloc(M_WAITOK)`.
- `rw(9)` soporta propagación de prioridad. Un thread que espera un lock exclusivo mantenido por un thread de menor prioridad elevará la prioridad del poseedor. Esta es la razón principal por la que `rw(9)` existe en lugar de ser simplemente una envoltura delgada sobre `sx(9)`.

Los flags de `rw_assert` son `RA_LOCKED`, `RA_RLOCKED`, `RA_WLOCKED`, junto con las mismas variantes de recursión que tiene `sx_assert`.

Dónde encontrarás `rw(9)` en el árbol de FreeBSD:

- La pila de red utiliza `rw(9)` para varias tablas de lectura frecuente (tablas de enrutamiento, la tabla de resolución de direcciones). El acceso de lectura ocurre en la ruta de recepción, que se ejecuta en el contexto de interrupción de red, donde dormir está prohibido.
- La capa VFS lo usa para algunas cachés de espacio de nombres.
- Varios subsistemas con rutas de lectura muy concurridas y actualizaciones de configuración poco frecuentes.

Para nuestro driver `myfirst`, cada acceso a la configuración ocurre en el contexto de proceso, cada escritor de configuración está dispuesto a soltar el lock alrededor de `sysctl_handle_*` (que duerme), y nos beneficiamos de la capacidad de ser interrumpidos por señales. `sx(9)` es la elección correcta. Si alguna vez necesitas acceder a la misma configuración desde un manejador de interrupción (el Capítulo 14 tratará este tema), la solución es cambiar a `rw(9)` y aceptar la restricción de que el escritor de configuración debe hacer todo su trabajo sin dormir.

### Un ejemplo práctico con rw(9)

Para que la alternativa sea concreta, así es como quedaría la ruta de configuración con `rw(9)`. El código es estructuralmente idéntico salvo por la API y la ausencia de interruptibilidad por señales:

```c
/* In the softc: */
struct rwlock           cfg_rw;
struct myfirst_config   cfg;

/* In attach: */
rw_init(&sc->cfg_rw, "myfirst cfg");

/* In detach: */
rw_destroy(&sc->cfg_rw);

/* Read path: */
static int
myfirst_get_debug_level_rw(struct myfirst_softc *sc)
{
        int level;

        rw_rlock(&sc->cfg_rw);
        level = sc->cfg.debug_level;
        rw_runlock(&sc->cfg_rw);
        return (level);
}

/* Write path (sysctl handler): */
static int
myfirst_sysctl_debug_level_rw(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, error;

        rw_rlock(&sc->cfg_rw);
        new = sc->cfg.debug_level;
        rw_runlock(&sc->cfg_rw);

        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);

        if (new < 0 || new > 3)
                return (EINVAL);

        rw_wlock(&sc->cfg_rw);
        sc->cfg.debug_level = new;
        rw_wunlock(&sc->cfg_rw);
        return (0);
}
```

Hay dos cosas a tener en cuenta. Primero, `sysctl_handle_int` está *fuera* del lock. Llamarlo dentro de una sección crítica de `rw(9)` sería ilegal porque `sysctl_handle_int` puede dormir. Esta es la misma disciplina que usamos en la versión con `sx(9)`, pero con `rw(9)` es obligatoria en lugar de meramente recomendable. Segundo, las rutas de lectura son idénticas a la versión con `sx(9)`; solo cambiaron los nombres de las funciones. Ese es el objetivo de la API simétrica: el modelo mental se traslada directamente.

Si nuestro driver algún día necesita soportar un lector de configuración en el contexto de interrupción (quizás un manejador de interrupción de hardware que quiera conocer el nivel de depuración actual), este sería el cambio que haríamos. Por ahora, `sx(9)` es la elección correcta y nos quedamos con él.

### Cerrando la sección 4

`sx(9)` nos ofrece una forma de expresar el esquema de "muchos lectores, escritor ocasional" sin serializar a cada lector. Es dormible, consciente de señales y sigue la misma disciplina de orden de locks que el mutex. `rw(9)` es su hermano no dormible, útil cuando la sección crítica puede ejecutarse en contextos donde dormir está prohibido; el ejemplo práctico anterior muestra las pequeñas diferencias. Usamos `sx(9)` para `myfirst` porque tanto el contexto de proceso como la interruptibilidad por señales son deseables.

La sección 5 reúne las nuevas primitivas. Añadimos un pequeño subsistema de configuración a `myfirst`, decidimos el orden de los locks entre la ruta de datos y la ruta de configuración, y verificamos el diseño contra `WITNESS`.



## Sección 5: Implementando un escenario seguro de múltiples lectores y un único escritor

Las tres secciones anteriores introdujeron las primitivas de forma aislada. Esta sección las combina en un diseño de driver coherente. Añadimos un pequeño subsistema de configuración a `myfirst`, le asignamos su propio sx lock, determinamos el orden de los locks con respecto al mutex existente de la ruta de datos, y verificamos el diseño resultante contra un kernel con `WITNESS`.

El subsistema de configuración es pequeño a propósito. El objetivo no es demostrar una funcionalidad compleja, sino mostrar la disciplina de orden de locks que cualquier driver con más de una clase de lock debe seguir.

### El subsistema de configuración

Añadimos tres parámetros configurables:

- `debug_level`: un entero de 0 a 3. Los valores más altos producen una salida de `dmesg` más detallada desde la ruta de datos del driver.
- `soft_byte_limit`: un entero. Si no es cero, el driver rechaza las escrituras que harían que el cbuf superara este número de bytes (devuelve `EAGAIN` anticipadamente). Es un mecanismo de control de flujo rudimentario.
- `nickname`: una cadena corta que el driver imprime en sus líneas de registro. Útil para distinguir múltiples instancias del driver en `dmesg`.

La estructura que los contiene:

```c
struct myfirst_config {
        int     debug_level;
        int     soft_byte_limit;
        char    nickname[32];
};
```

Añádela al softc, junto con su sx lock:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct sx               cfg_sx;
        struct myfirst_config   cfg;
        /* ... rest ... */
};
```

Inicializar y destruir:

```c
/* In attach: */
sx_init(&sc->cfg_sx, "myfirst cfg");
sc->cfg.debug_level = 0;
sc->cfg.soft_byte_limit = 0;
strlcpy(sc->cfg.nickname, "myfirst", sizeof(sc->cfg.nickname));

/* In detach: */
sx_destroy(&sc->cfg_sx);
```

Los valores iniciales se establecen antes de que se cree el cdev, por lo que ningún otro thread puede observar una configuración parcialmente inicializada.

### La decisión sobre el orden de los locks

El driver ahora tiene dos clases de locks que pueden mantenerse simultáneamente: `sc->mtx` (el cbuf y el estado del softc) y `sc->cfg_sx` (la configuración). Debemos decidir cuál se adquiere primero cuando se necesitan ambos.

Las preguntas naturales que hay que hacerse:

1. ¿Qué ruta mantiene cada lock con más frecuencia? La ruta de datos mantiene `sc->mtx` constantemente (cada `myfirst_read` y `myfirst_write` entra y sale de él). La ruta de datos también quiere leer `sc->cfg.debug_level` para decidir si registrar el evento; eso es un `sx_slock(&sc->cfg_sx)`. Así que la ruta de datos ya necesita ambos, en el orden *mtx primero, sx después*.

2. ¿Qué ruta mantiene el cfg lock y podría necesitar el data lock? Un manejador sysctl que actualiza la configuración toma `sx_xlock(&sc->cfg_sx)`. ¿Necesita alguna vez `sc->mtx`? En principio, sí: un manejador sysctl que restablece los contadores de bytes tomaría ambos. El diseño más limpio es *no* tomar el data mutex desde dentro de la sección crítica del sx; el escritor sysctl organiza su trabajo, libera el lock sx y luego toma el data mutex si es necesario. Eso mantiene el orden monotónico.

La decisión: **`sc->mtx` se adquiere antes que `sc->cfg_sx` siempre que ambos se mantengan simultáneamente.**

El orden inverso está prohibido. `WITNESS` detectará cualquier violación.

Documentamos la decisión en `LOCKING.md`:

```markdown
## Lock Order

sc->mtx -> sc->cfg_sx

A thread holding sc->mtx may acquire sc->cfg_sx (in either shared or
exclusive mode). A thread holding sc->cfg_sx may NOT acquire sc->mtx.

Rationale: the data path always holds sc->mtx and may need to read
configuration during its critical section. The configuration path
(sysctl writers) does not need to update data-path state while
holding sc->cfg_sx; if it needs to, it releases sc->cfg_sx first and
then acquires sc->mtx separately.
```

### Lectura de la configuración en la ruta de datos

El acceso más frecuente a la configuración es la comprobación de `debug_level` en la ruta de datos para decidir si emitir un mensaje de registro. Lo envolvemos en un pequeño helper:

```c
static int
myfirst_get_debug_level(struct myfirst_softc *sc)
{
        int level;

        sx_slock(&sc->cfg_sx);
        level = sc->cfg.debug_level;
        sx_sunlock(&sc->cfg_sx);
        return (level);
}
```

Nótese que este helper toma únicamente `sc->cfg_sx`, no `sc->mtx`. Eso es intencional: el helper no necesita el data mutex para leer la configuración. Si se llama desde un contexto que ya mantiene `sc->mtx`, el orden de los locks se respeta (mtx primero, sx después). Si se llama desde un contexto que no mantiene ningún lock, también está bien.

Una macro de registro con conciencia del nivel de depuración:

```c
#define MYFIRST_DBG(sc, level, fmt, ...) do {                          \
        if (myfirst_get_debug_level(sc) >= (level))                    \
                device_printf((sc)->dev, fmt, ##__VA_ARGS__);          \
} while (0)
```

Úsala en la ruta de datos:

```c
MYFIRST_DBG(sc, 2, "read got %zu bytes\n", got);
```

La adquisición del shared-lock es el coste de cada comprobación. En una máquina con varios núcleos, son unas pocas operaciones atómicas; los lectores no compiten entre sí. En una máquina de un solo núcleo, el coste es prácticamente nulo (ningún otro thread puede estar en medio de una escritura).

### Lectura del límite de bytes suave

El mismo patrón para el límite de bytes suave, utilizado por `myfirst_write` para decidir si rechazar la operación:

```c
static int
myfirst_get_soft_byte_limit(struct myfirst_softc *sc)
{
        int limit;

        sx_slock(&sc->cfg_sx);
        limit = sc->cfg.soft_byte_limit;
        sx_sunlock(&sc->cfg_sx);
        return (limit);
}
```

Dentro de `myfirst_write`, antes de que se produzca la escritura real (nótese que en este punto del bucle todavía no se ha calculado `want`), la comprobación del límite usa `sizeof(bounce)` como estimación del peor caso: cualquier iteración individual escribe como máximo los bytes de un bounce buffer, por lo que rechazar cuando `cbuf_used + sizeof(bounce)` superaría el límite es una salida anticipada conservadora:

```c
int limit = myfirst_get_soft_byte_limit(sc);

MYFIRST_LOCK(sc);
if (limit > 0 && cbuf_used(&sc->cb) + sizeof(bounce) > (size_t)limit) {
        MYFIRST_UNLOCK(sc);
        return (uio->uio_resid != nbefore ? 0 : EAGAIN);
}
/* fall through to wait_room and the rest of the iteration */
```

Dos adquisiciones consecutivas: el cfg sx para el límite y el mtx para la comprobación del cbuf. Nótese que adquirimos el sx *primero* y lo soltamos antes de tomar el mutex. En principio, podríamos mantener ambos (cfg_sx y luego mtx), pero el orden sería incorrecto; la regla dice mtx primero, sx después. Por eso adquirimos cada uno de forma independiente. El pequeño coste de dos adquisiciones es el precio de la corrección.

Un detalle sutil: entre la liberación del cfg_sx y la adquisición del mtx, el límite podría cambiar. Eso es aceptable; el límite es una sugerencia flexible, no una garantía estricta. Si un escritor sysctl aumenta el límite entre nuestras dos adquisiciones y aun así rechazamos la escritura, el usuario reintentará y tendrá éxito en el segundo intento. Si el límite se reduce y procedemos con una escritura que el nuevo límite habría rechazado, no se produce ningún daño porque el cbuf tiene su propio límite de tamaño estricto.

La elección de `sizeof(bounce)` en lugar del `want` real refleja otro detalle sutil: en esta etapa del bucle, el driver todavía no ha calculado `want` (eso requiere saber cuánto espacio tiene actualmente el cbuf, lo cual requiere mantener el mutex primero). Usar `sizeof(bounce)` como cota del peor caso permite que la comprobación ocurra antes del cálculo del espacio disponible. El archivo fuente de ejemplo sigue exactamente este patrón.

### Actualización de la configuración: un escritor sysctl

El lado del escritor, expuesto como un sysctl que puede leer y escribir `debug_level`:

```c
static int
myfirst_sysctl_debug_level(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, error;

        sx_slock(&sc->cfg_sx);
        new = sc->cfg.debug_level;
        sx_sunlock(&sc->cfg_sx);

        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);

        if (new < 0 || new > 3)
                return (EINVAL);

        sx_xlock(&sc->cfg_sx);
        sc->cfg.debug_level = new;
        sx_xunlock(&sc->cfg_sx);
        return (0);
}
```

Recorramos las adquisiciones de locks:

1. `sx_slock` para leer el valor actual (de modo que el framework sysctl pueda devolverlo en una consulta de solo lectura).
2. `sx_sunlock` antes de llamar a `sysctl_handle_int`, porque esa función puede copiar datos hacia y desde el espacio de usuario (lo que puede dormir) y no queremos mantener el lock sx durante ese proceso.
3. Tras la validación, `sx_xlock` para confirmar el nuevo valor.
4. `sx_xunlock` para liberar.

En esta ruta nunca mantenemos `sc->mtx`. La regla de orden de locks se cumple de forma trivial: esta ruta nunca mantiene ambos locks a la vez.

Registra el sysctl en attach:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "debug_level",
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    sc, 0, myfirst_sysctl_debug_level, "I",
    "Debug verbosity level (0-3)");
```

El flag `CTLFLAG_MPSAFE` indica al framework sysctl que nuestro manejador es seguro de llamar sin adquirir el giant lock; y así es. Este es el valor predeterminado moderno para los nuevos manejadores sysctl.

### Actualización del límite de bytes suave

La misma forma para el límite de bytes:

```c
static int
myfirst_sysctl_soft_byte_limit(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, error;

        sx_slock(&sc->cfg_sx);
        new = sc->cfg.soft_byte_limit;
        sx_sunlock(&sc->cfg_sx);

        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);

        if (new < 0)
                return (EINVAL);

        sx_xlock(&sc->cfg_sx);
        sc->cfg.soft_byte_limit = new;
        sx_xunlock(&sc->cfg_sx);
        return (0);
}
```

Y para el nickname (una cadena, por lo que el manejador sysctl es ligeramente diferente):

```c
static int
myfirst_sysctl_nickname(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        char buf[sizeof(sc->cfg.nickname)];
        int error;

        sx_slock(&sc->cfg_sx);
        strlcpy(buf, sc->cfg.nickname, sizeof(buf));
        sx_sunlock(&sc->cfg_sx);

        error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
        if (error || req->newptr == NULL)
                return (error);

        sx_xlock(&sc->cfg_sx);
        strlcpy(sc->cfg.nickname, buf, sizeof(sc->cfg.nickname));
        sx_xunlock(&sc->cfg_sx);
        return (0);
}
```

La estructura es idéntica: shared-lock para leer, soltar, validar a través del framework sysctl, exclusive-lock para confirmar. La versión de cadena usa `strlcpy` por seguridad.

### Una operación que mantiene ambos locks

A veces una ruta necesita legítimamente ambos locks. Como ejemplo, supón que añadimos un sysctl que restablece el cbuf y borra todos los contadores de bytes a la vez. Ese sysctl necesita:

1. El lock exclusivo de configuración si también va a restablecer algún parámetro de configuración (por ejemplo, restablecer el nivel de depuración).
2. El data mutex para manipular el cbuf.

Siguiendo nuestro orden de locks, adquirimos primero `sc->mtx` y luego `sc->cfg_sx`:

```c
static int
myfirst_sysctl_reset(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int reset = 0;
        int error;

        error = sysctl_handle_int(oidp, &reset, 0, req);
        if (error || req->newptr == NULL || reset != 1)
                return (error);

        MYFIRST_LOCK(sc);
        sx_xlock(&sc->cfg_sx);

        cbuf_reset(&sc->cb);
        sc->cfg.debug_level = 0;
        counter_u64_zero(sc->bytes_read);
        counter_u64_zero(sc->bytes_written);

        sx_xunlock(&sc->cfg_sx);
        MYFIRST_UNLOCK(sc);

        cv_broadcast(&sc->room_cv);  /* room is now available */
        return (0);
}
```

El orden de adquisición es `mtx` y después `sx`. El orden de liberación es el inverso: `sx` primero, `mtx` después. (Las liberaciones deben invertir el orden de adquisición para mantener el invariante de orden de locks para cualquier thread que observe el estado de un lock en medio del proceso.)

El broadcast de cv ocurre después de que se liberan ambos locks. Despertar a los durmientes no requiere mantener ninguno de los dos locks.

`cbuf_reset` es un pequeño helper que añadimos al módulo cbuf:

```c
void
cbuf_reset(struct cbuf *cb)
{
        cb->cb_head = 0;
        cb->cb_used = 0;
}
```

Pone a cero los índices pero no toca la memoria de respaldo; el contenido se vuelve irrelevante en el momento en que `cb_used` es cero.

### Verificación contra WITNESS

Compila el nuevo driver y cárgalo en un kernel con `WITNESS`. Ejecuta el kit de pruebas de estrés del Capítulo 11 más un nuevo tester que machaca los sysctls mientras se producen operaciones de I/O:

```c
/* config_writer.c: continuously update config sysctls. */
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sysctl.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
        int seconds = (argc > 1) ? atoi(argv[1]) : 30;
        time_t end = time(NULL) + seconds;
        int v = 0;

        while (time(NULL) < end) {
                v = (v + 1) % 4;
                if (sysctlbyname("dev.myfirst.0.debug_level",
                    NULL, NULL, &v, sizeof(v)) != 0)
                        warn("sysctl debug_level");

                int limit = (v == 0) ? 0 : 4096;
                if (sysctlbyname("dev.myfirst.0.soft_byte_limit",
                    NULL, NULL, &limit, sizeof(limit)) != 0)
                        warn("sysctl soft_byte_limit");

                usleep(10000);  /* 10 ms */
        }
        return (0);
}
```

Ejecuta `mp_stress` en un terminal, `mt_reader` en un segundo y `config_writer` en un tercero, todos simultáneamente. Observa `dmesg` en busca de advertencias.

Deberían ocurrir tres cosas:

1. Todas las pruebas pasan con recuentos de bytes coherentes.
2. El nivel de depuración cambia de forma visible en `dmesg` (cuando el nivel es alto, la ruta de datos emite mensajes de registro; cuando es bajo, permanece silenciosa).
3. `WITNESS` permanece en silencio. No se reportan inversiones de orden de locks.

Si `WITNESS` sí notifica una inversión, significa que el orden de adquisición de locks se ha violado en algún punto. Vuelve a leer la ruta de código afectada contrastándola con la regla (mtx primero, sx después) y corrige la violación.

### Qué ocurre si inviertes el orden

Para que la regla sea concreta, violémosla a propósito. Toma una ruta existente que mantiene `sc->mtx` y reescríbela para adquirir los locks en el orden incorrecto. Por ejemplo, en `myfirst_read`:

```c
/* WRONG: this is the bug we want WITNESS to catch. */
sx_slock(&sc->cfg_sx);   /* sx first */
MYFIRST_LOCK(sc);        /* mtx second; reverses the global order */
/* ... */
MYFIRST_UNLOCK(sc);
sx_sunlock(&sc->cfg_sx);
```

Compila, carga en un kernel con `WITNESS`, ejecuta el kit de pruebas. `WITNESS` debería activarse en la primera ejecución que ejercite esta ruta y cualquier otra que haga mtx-luego-sx:

```text
lock order reversal:
 1st 0xfffffe000a1b2c30 myfirst cfg (myfirst cfg, sx) @ ...:<line>
 2nd 0xfffffe000a1b2c50 myfirst0 (myfirst, sleep mutex) @ ...:<line>
lock order myfirst cfg -> myfirst0 attempted at ...
where myfirst0 -> myfirst cfg is established at ...
```

La advertencia nombra los dos locks, sus direcciones, la ubicación en el código fuente de cada adquisición y el orden establecido previamente. La solución es devolver los locks al orden canónico; revierte el cambio y la advertencia desaparece.

Esto es `WITNESS` haciendo su trabajo. Un driver con más de una clase de lock sin `WITNESS` es un driver esperando un deadlock que nadie podrá reproducir.

### Un patrón algo más amplio: snapshot-and-apply

Un patrón habitual cuando se necesitan ambos locks y la operación tiene trabajo que hacer bajo cada uno es el *snapshot-and-apply*. En su forma más simple, antes de que se conozca el tamaño real de transferencia:

```c
/* Phase 1: snapshot the configuration under the sx (in shared mode). */
sx_slock(&sc->cfg_sx);
int dbg = sc->cfg.debug_level;
int limit = sc->cfg.soft_byte_limit;
sx_sunlock(&sc->cfg_sx);

/* Phase 2: do the work under the mtx, using the snapshot. */
MYFIRST_LOCK(sc);
if (limit > 0 && cbuf_used(&sc->cb) + sizeof(bounce) > (size_t)limit) {
        MYFIRST_UNLOCK(sc);
        return (EAGAIN);
}
/* ... cbuf operations ... */
size_t actual = /* determined inside the critical section */;
MYFIRST_UNLOCK(sc);

if (dbg >= 2)
        device_printf(sc->dev, "wrote %zu bytes\n", actual);
```

El patrón snapshot-and-apply mantiene cada lock retenido el tiempo mínimo, evita retener ambos locks a la vez y produce una forma bifásica clara sobre la que es fácil razonar. El coste es que el snapshot puede estar ligeramente desactualizado cuando se ejecuta el apply; en la práctica, el desfase es de microsegundos y resulta aceptable para casi cualquier valor de configuración.

Si el desfase no es aceptable para algún valor específico (digamos, un indicador de seguridad crítico), el titular debe adquirir el lock (o los locks) de forma atómica y seguir el orden global. El patrón snapshot es una opción predeterminada, no una ley.

### Un recorrido por myfirst_sysctl_reset

El sysctl de reset es la única ruta del capítulo que legítimamente mantiene ambos locks a la vez. Vale la pena recorrerlo con cuidado, porque el patrón (`mtx` primero, `sx` después, ambos liberados en orden inverso, broadcasts después de que ambos se liberan) es el que hay que imitar siempre que haya que mantener ambos locks a la vez.

Cuando un usuario ejecuta `sysctl -w dev.myfirst.0.reset=1`, el kernel llama a `myfirst_sysctl_reset` con `req->newptr` distinto de NULL. El manejador:

1. Lee el nuevo valor mediante `sysctl_handle_int`. Si el valor no es 1, regresa sin hacer nada (trata solo `1` como una petición de reset confirmada).
2. Adquiere `MYFIRST_LOCK(sc)`. El mutex de datos ya está retenido.
3. Adquiere `sx_xlock(&sc->cfg_sx)`. El cfg sx ya está retenido en modo exclusivo. Ambos locks retenidos; orden de locks satisfecho (mtx primero, sx después).
4. Llama a `cbuf_reset(&sc->cb)`. El buffer está ahora vacío (`cb_used = 0`, `cb_head = 0`).
5. Establece `sc->cfg.debug_level = 0`. La configuración está ahora en su estado inicial.
6. Llama a `counter_u64_zero` en cada contador por CPU. Los contadores de bytes están ahora a cero.
7. Llama a `sx_xunlock(&sc->cfg_sx)`. El cfg sx se libera. Solo el mtx está retenido ahora.
8. Llama a `MYFIRST_UNLOCK(sc)`. El mtx se libera. No hay locks retenidos.
9. Llama a `cv_broadcast(&sc->room_cv)`. Se despiertan todos los escritores bloqueados por estar el buffer lleno; recomprobarán `cb_free`, encontrarán que es igual a `cb_size` y continuarán.
10. Devuelve 0.

Tres observaciones sobre esta secuencia.

Las adquisiciones de locks siguen el orden global; `WITNESS` está satisfecho. Las liberaciones siguen el orden inverso, lo que preserva el invariante de que cualquier thread que observe la secuencia vea un estado consistente.

Los broadcasts ocurren *después* de que ambos locks se liberan. Mantener cualquiera de los locks mientras se hace el broadcast bloquearía innecesariamente a los threads despertados cuando intenten adquirir lo que necesitan; el broadcast es de tipo fire-and-forget, y el kernel se encarga del resto.

El reset de la ruta de datos (`cbuf_reset`) y el reset de la configuración (`debug_level = 0`) son atómicos entre sí. Un thread que observe cualquier campo después del reset ve el valor posterior al reset de todos los campos; ningún thread puede observar un estado de reset a medias.

Si quisiéramos añadir más operaciones de reset (limpiar una máquina de estados, resetear un registro de hardware), cada una encaja en esta plantilla. Las retenciones de locks se amplían para cubrir los nuevos campos; los broadcasts al final notifican a los cvs apropiados.

### Cerrando la sección 5

El driver tiene ahora dos clases de lock: un mutex para la ruta de datos y un sx lock para la configuración. El orden está documentado (mtx primero, sx después), el orden lo hace cumplir `WITNESS` en tiempo de ejecución, y los patrones que utilizamos (snapshot-and-apply para el caso común, adquisición única para rutas que solo necesitan un lock, y la adquisición-liberación cuidadosamente ordenada para rutas que genuinamente necesitan ambos) mantienen el diseño auditable. Los nuevos sysctls permiten al espacio de usuario ajustar el comportamiento del driver en tiempo de ejecución sin interrumpir la ruta de datos.

La lección más importante de la sección 5 es que añadir una segunda clase de lock es una decisión de diseño real, no una optimización transparente. El coste es la disciplina necesaria para mantener el orden de los locks, el esfuerzo de documentación y el trabajo de auditoría para verificar ambos en tiempo de ejecución. El beneficio es que la ruta de datos ya no es el único lugar donde vive el estado; la configuración tiene su propia protección, su propio ritmo y su propia interfaz sysctl. A medida que los drivers crecen, suelen acabar con tres o cuatro clases de lock, cada una con su propia finalidad. El patrón de esta sección escala.

La sección 6 aborda la pregunta que siempre surge cuando se empiezan a mezclar clases de lock: ¿cómo depuras un bug de sincronización cuando aparece? El kernel te proporciona varias herramientas para ello; este es el capítulo que las presenta.



## Sección 6: Depuración de problemas de sincronización

El capítulo 11 introdujo los hooks básicos de depuración del kernel: `INVARIANTS`, `WITNESS`, `KASSERT` y `mtx_assert`. Los utilizamos para verificar que el mutex se mantenía en los momentos correctos y que se respetaban las reglas de orden de los locks. Con el kit de herramientas de sincronización más amplio que el capítulo 12 ha puesto en tus manos, las herramientas de depuración también se amplían. Esta sección recorre los patrones y las herramientas que utilizarás con más frecuencia: leer con atención una advertencia de `WITNESS`, inspeccionar un sistema colgado con el depurador del kernel, y reconocer los modos de fallo más comunes de las variables de condición y los locks compartidos/exclusivos.

### Un catálogo de bugs de sincronización

Seis formas de fallo cubren la mayor parte de lo que encontrarás en la práctica. Reconocer la forma es la mitad del diagnóstico.

**Wakeup perdido.** Un thread entra en `cv_wait_sig` (o `mtx_sleep`); la condición que espera se vuelve verdadera pero nunca se llama a `cv_signal` (o `wakeup`). El thread duerme indefinidamente. Causas: se olvidó el `cv_signal` tras el cambio de estado; se señalizó el cv incorrecto; se señalizó antes de que el estado hubiera cambiado realmente; el cambio de estado ocurre en una ruta que no incluye ninguna señal.

**Wakeup espurio mal gestionado.** Un thread es despertado por una señal u otro evento transitorio, pero la condición que esperaba sigue siendo falsa. Si el código circundante no vuelve a entrar en bucle y recomprobar, el thread continúa asumiendo que la condición es verdadera y opera sobre un estado desactualizado. Remedio: envuelve siempre las llamadas a `cv_wait` en `while (!condition) cv_wait(...)`, nunca en `if (!condition) cv_wait(...)`.

**Inversión del orden de locks.** Dos locks adquiridos en órdenes opuestos por dos rutas distintas. O se produce un deadlock bajo contención o `WITNESS` lo detecta. Remedio: define un orden global en `LOCKING.md` y síguelo en todas partes.

**Destrucción prematura.** Un cv o sx se destruye mientras un thread sigue esperando en él. Los síntomas son impredecibles: panics, uso tras liberar memoria, cuelgues por punteros obsoletos. Remedio: asegúrate de que cada waiter ha sido despertado (y ha regresado realmente de la primitiva de espera) antes de llamar a `cv_destroy` o `sx_destroy`. La ruta de detach debe ser especialmente cuidadosa aquí.

**Dormir mientras se retiene un lock que no lo permite.** Retener un mutex `MTX_SPIN` o un lock `rw(9)` y luego llamar a `cv_wait`, `mtx_sleep`, `uiomove` o `malloc(M_WAITOK)`. `WITNESS` lo detecta; en un kernel sin `WITNESS` el sistema sufre un deadlock o un panic. Remedio: libera el lock spin/rw antes de dormir, o usa un mutex `MTX_DEF` o un lock `sx(9)` en su lugar, ya que ambos permiten dormir.

**Condición de carrera entre detach y una operación activa.** Un descriptor está abierto, un thread está en `cv_wait_sig` y se le indica al dispositivo que haga detach. La ruta de detach debe despertar al thread que duerme, esperar hasta que haya regresado y mantener el cv (y el mutex) vivo hasta que eso haya ocurrido. Remedio: el patrón estándar de detach consiste en establecer el indicador de "going away", hacer broadcast a todos los cvs, esperar a que `active_fhs` llegue a cero y luego destruir las primitivas.

Un driver que sobreviva el tiempo suficiente para llegar a producción habrá gestionado cada uno de estos casos al menos una vez. Los laboratorios de este capítulo te permiten provocar y resolver varios de forma deliberada para que los patrones se vuelvan familiares.

### Cómo leer una advertencia de WITNESS con atención

Una advertencia de `WITNESS` tiene tres partes útiles: el texto de la advertencia, las ubicaciones en el código fuente de cada adquisición de lock y el orden establecido previamente. Analízalas por separado.

Una advertencia típica por inversión de orden de locks:

```text
lock order reversal:
 1st 0xfffffe000a1b2c30 myfirst cfg (myfirst cfg, sx) @ /var/.../myfirst.c:120
 2nd 0xfffffe000a1b2c50 myfirst0 (myfirst, sleep mutex) @ /var/.../myfirst.c:240
lock order myfirst cfg -> myfirst0 attempted at /var/.../myfirst.c:241
where myfirst0 -> myfirst cfg is established at /var/.../myfirst.c:280
```

Leyendo de arriba abajo:

- **`lock order reversal:`**: la clase de advertencia. Otras clases incluyen `acquiring duplicate lock of same type`, `sleeping thread (pid N) owns a non-sleepable lock`, `WITNESS exceeded the recursion limit`.
- **`1st 0x... myfirst cfg`**: el primer lock adquirido en la ruta problemática. La dirección (`0x...`), el nombre (`myfirst cfg`), el tipo (`sx`) y la ubicación en el código fuente (`myfirst.c:120`) te indican exactamente qué lock y dónde.
- **`2nd 0x... myfirst0`**: el segundo lock adquirido. El mismo conjunto de campos.
- **`lock order myfirst cfg -> myfirst0 attempted at ...`**: el orden que este código intenta usar.
- **`where myfirst0 -> myfirst cfg is established at ...`**: el orden que `WITNESS` observó y registró previamente como canónico, con la ubicación en el código fuente del ejemplo canónico.

La solución es una de dos cosas. O la nueva ruta es incorrecta y debe seguir el orden canónico (lo más habitual), o el propio orden canónico es incorrecto y necesita cambiar. Elegir cuál es una decisión de juicio; normalmente la respuesta es corregir la nueva ruta para que coincida con la canónica, porque la canónica probablemente era correcta.

A veces la advertencia es una falsa alarma: dos objetos distintos del mismo tipo de lock, en los que el orden entre ellos no importa porque son independientes. `WITNESS` no siempre lo sabe; el indicador `LOR_DUPOK` en la inicialización del lock le indica que omita la comprobación. No lo necesitamos para `myfirst`, pero los drivers reales con locks por instancia a veces sí.

### Uso de DDB para inspeccionar un sistema colgado

Si el test se cuelga y `dmesg` está en silencio, el culpable suele ser un wakeup perdido o un deadlock. El depurador del kernel (DDB) te permite inspeccionar el estado de cada thread y cada lock en el momento del cuelgue.

Para entrar en DDB en un sistema colgado, pulsa la tecla `Break` en la consola (o envía el escape `~b` si estás en una consola serie con `cu`). DDB te muestra el indicador `db>`.

Los comandos más útiles para la depuración de sincronización:

- `show locks`: lista los locks retenidos por el thread actual (el que estaba activo cuando entró DDB, que suele ser el thread idle del kernel; rara vez útil por sí solo).
- `show all locks` (alias `show alllocks`): lista todos los locks retenidos por todos los threads del sistema. Este es el comando que necesitarás la mayor parte del tiempo.
- `show witness`: vuelca el grafo completo de orden de locks de `WITNESS`. Detallado pero definitivo.
- `show sleepchain <thread>`: traza la cadena de locks y esperas en la que participa un thread específico. Útil cuando se sospecha un bucle de deadlock.
- `show lockchain <lock>`: traza desde un lock hasta el thread que lo retiene y cualquier otro lock que ese thread mantenga.
- `ps`: lista todos los procesos y threads con su estado.
- `bt <thread>`: backtrace de un thread específico.
- `continue`: sale de DDB y reanuda el sistema. Úsalo solo si no has realizado ningún cambio.
- `panic`: fuerza un panic para que el sistema se reinicie limpiamente. Úsalo si `continue` no es seguro.

Flujo de trabajo para un test colgado:

1. Accede a DDB mediante la señal de break de la consola.
2. `show all locks`. Anota los threads que mantengan locks.
3. `ps` para localizar los threads de prueba (busca `myfrd`, `myfwr` o `cv_w` en la columna wait channel).
4. Para cada thread de interés, usa `bt <pid> <tid>` para obtener un backtrace.
5. Si sospechas de un deadlock, ejecuta `show sleepchain` para cada thread en espera.
6. Cuando dispongas de suficiente información, ejecuta `panic` para reiniciar.

La transcripción de DDB se convierte en tu registro de depuración. Guárdala (DDB puede volcar la salida a la consola y al buffer de mensajes del kernel; en un kernel de depuración con `EARLY_AP_STARTUP`, puedes redirigir la salida al puerto serie).

### Reconocer los wakeups perdidos

Un wakeup perdido es el bug de cv más habitual. El síntoma es un waiter bloqueado indefinidamente; el thread está en `cv_wait_sig` y nada lo despierta.

Detección en DDB:

```text
db> ps
... (find the hung thread, in state "*myfirst data" for example)
db> bt 1234 1235
... (backtrace shows the thread inside cv_wait_sig)
db> show locks
... (the thread holds no locks; it is sleeping)
```

El cv en cuestión se identifica por el nombre del wait channel. Si el nombre del canal es `myfirst data`, el cv es `sc->data_cv`. Ahora pregúntate: ¿quién debería haber llamado a `cv_signal(&sc->data_cv)`? Busca en el código fuente:

```sh
$ grep -n 'cv_signal(&sc->data_cv\|cv_broadcast(&sc->data_cv' myfirst.c
```

Para cada punto de llamada, determina si se suponía que debía ejecutarse y si realmente lo hizo. Las causas más habituales son:

- La señal está dentro de un `if` que nunca fue verdadero.
- La señal está después de un `return` que la omitió.
- La señal apunta al cv incorrecto (`cv_signal(&sc->room_cv)` en lugar de `data_cv`).
- El cambio de estado no modifica realmente la condición (incrementaste `cb_used` pero el consumidor comprobaba `cb_free`, que es el mismo estado lógico pero `cb_free == cb_size - cb_used`; ese caso en concreto es correcto, pero un error de cálculo similar puede ocultarse fácilmente).

Corrige el código fuente, recompila y vuelve a probar. El síntoma debería desaparecer.

### Reconocer los wakeups espurios

Un wakeup espurio es uno que llega cuando la condición todavía es falsa. Las causas incluyen señales (`cv_wait_sig` retorna a causa de una señal aunque la condición no haya cambiado) y timeouts (`cv_timedwait_sig` retorna a causa del temporizador). Ambas son situaciones normales; el driver debe gestionarlas.

Detección: el bug *no* es el wakeup en sí, sino el hecho de no gestionarlo. La forma habitual:

```c
/* WRONG: */
if (cbuf_used(&sc->cb) == 0)
        cv_wait_sig(&sc->data_cv, &sc->mtx);
/* now reading cbuf assuming there is data, but a spurious wakeup
   could have brought us here while the buffer is still empty */
got = cbuf_read(&sc->cb, bounce, take);
```

`cbuf_read` devolvería cero en ese caso, lo que se propaga al espacio de usuario como una lectura de cero bytes, que `cat` y otras utilidades interpretan como EOF. El usuario ve un fin de archivo silencioso que no es realmente un fin de archivo.

La solución: utiliza siempre un bucle:

```c
/* CORRECT: */
while (cbuf_used(&sc->cb) == 0) {
        int error = cv_wait_sig(&sc->data_cv, &sc->mtx);
        if (error != 0)
                return (error);
        if (!sc->is_attached)
                return (ENXIO);
}
got = cbuf_read(&sc->cb, bounce, take);
```

El helper `myfirst_wait_data` de la Sección 2 ya sigue este patrón. La regla general es: *nunca uses `if` alrededor de un `cv_wait`; utiliza siempre `while`.*

### Reconocer la inversión del orden de locks

Antes vimos una advertencia de `WITNESS`. La alternativa, en un kernel sin `WITNESS`, es un deadlock. Dos threads tienen cada uno un lock y quieren el del otro; ninguno puede continuar.

Detección en DDB:

```text
db> show all locks
Process 1234 (test1) thread 0xfffffe...
shared sx myfirst cfg (myfirst cfg) ... locked @ ...:120
shared sx myfirst cfg (myfirst cfg) r = 1 ... locked @ ...:120

Process 5678 (test2) thread 0xfffffe...
exclusive sleep mutex myfirst0 (myfirst) r = 0 ... locked @ ...:240
```

Luego `show sleepchain` para cada thread en espera:

```text
db> show sleepchain 1234
Thread 1234 (pid X) blocked on lock myfirst0 owned by thread 5678
db> show sleepchain 5678
Thread 5678 (pid Y) blocked on lock myfirst cfg owned by thread 1234
```

Ese ciclo es el deadlock. Cada thread está bloqueado en un lock que el otro tiene. La solución es revisar el orden de los locks; uno de los dos caminos los está adquiriendo en el orden incorrecto. La corrección es la misma que para la advertencia de `WITNESS`: localiza la adquisición infractora y reordénala.

### Dormir con un lock no durmiente

Si tu driver usa mutexes `MTX_SPIN` o locks `rw(9)` y en algún punto llamas a `cv_wait`, `mtx_sleep`, `uiomove`, o `malloc(M_WAITOK)` mientras los tienes adquiridos, `WITNESS` disparará:

```text
sleeping thread (pid 1234, tid 5678) owns a non-sleepable lock:
exclusive rw lock myfirst rw (myfirst rw) r = 0 ... locked @ ...:100
```

La advertencia nombra el lock y la ubicación de su adquisición. La solución es liberar el lock antes de la operación bloqueante. `myfirst` no usa `MTX_SPIN` ni `rw(9)`, así que no nos encontraremos con esto directamente; si reutilizas los patrones en otro driver, tenlo presente.

### Destrucción prematura

Un cv o sx que se destruye mientras un thread todavía está esperando provoca crashes del tipo use-after-free. El síntoma suele ser un panic con un backtrace dentro de `cv_wait` o `sx_xlock` después de que `cv_destroy` o `sx_destroy` ya se hayan ejecutado.

El patrón de detach del Capítulo 11 (rechazar el detach mientras `active_fhs > 0`) previene esto en la mayoría de situaciones. El driver del Capítulo 12 extiende el patrón con llamadas a `cv_broadcast` antes de destruir:

```c
MYFIRST_LOCK(sc);
sc->is_attached = 0;
cv_broadcast(&sc->data_cv);
cv_broadcast(&sc->room_cv);
MYFIRST_UNLOCK(sc);

/* now wait for any thread that was sleeping to return.
   The cv_broadcast above wakes them; they see is_attached
   is false and return ENXIO. They release the mutex on
   their way out. */

/* Once we know no one is in the I/O paths, destroy the
   primitives. By construction, active_fhs == 0 when we get
   here, so no thread can re-enter. */
cv_destroy(&sc->data_cv);
cv_destroy(&sc->room_cv);
sx_destroy(&sc->cfg_sx);
mtx_destroy(&sc->mtx);
```

El orden es importante. El mutex es el interlock para los cvs (un thread dentro de `cv_wait_sig` estaba durmiendo con `sc->mtx` liberado al kernel; al despertar, `cv_wait_sig` vuelve a adquirir `sc->mtx` antes de retornar). Si destruimos `sc->mtx` primero y luego llamamos a `cv_destroy`, un thread que no haya terminado de despertar del todo puede quedar atrapado dentro del kernel intentando readquirir un mutex cuya memoria ya hemos liberado. Destruir los cvs primero garantiza que ningún thread siga dentro de `cv_wait_sig` cuando el mutex desaparezca. El mismo razonamiento se aplica al sx: un thread bloqueado dentro de `sx_xlock` no tiene el mutex, pero su ruta de readquisición al despertar puede tropezar con el orden si el sx y el mutex se destruyen simultáneamente. Destruye en orden inverso al que un thread podría todavía estar adquiriendo o esperando en cada primitiva: primero los cvs (waiters drenados), luego el sx (sin lectores ni escritores pendientes), y finalmente el mutex (sin interlock asociado).

### Asserts útiles para añadir

Añade llamadas a `sx_assert` y `mtx_assert` a lo largo de los helpers para documentar el estado de lock esperado. Cada una no tiene coste en producción (`INVARIANTS` se excluye de la compilación) y detecta nuevos bugs en el kernel de depuración.

Ejemplos:

```c
static int
myfirst_get_debug_level(struct myfirst_softc *sc)
{
        int level;

        sx_slock(&sc->cfg_sx);
        sx_assert(&sc->cfg_sx, SA_SLOCKED);  /* document the lock state */
        level = sc->cfg.debug_level;
        sx_sunlock(&sc->cfg_sx);
        return (level);
}
```

El `sx_assert(&sc->cfg_sx, SA_SLOCKED)` es técnicamente redundante después de `sx_slock` (el lock acaba de adquirirse), pero deja clara la intención para quien lea el código y detecta errores de refactorización (alguien mueve la función y olvida el lock).

Un patrón más útil: aserciones en helpers que *esperan* que el llamador ya haya adquirido un lock:

```c
static void
myfirst_apply_debug_level(struct myfirst_softc *sc, int level)
{
        sx_assert(&sc->cfg_sx, SA_XLOCKED);  /* caller must hold xlock */
        sc->cfg.debug_level = level;
}
```

Si un punto de llamada futuro intenta usar este helper sin el lock, la aserción dispara. Las expectativas de la función son ahora ejecutables, no solo documentadas.

### Rastrear la actividad de locks con dtrace y lockstat

Dos herramientas en espacio de usuario permiten observar el comportamiento de los locks sin modificar el driver.

`lockstat(1)` resume la contención de locks durante un período:

```sh
# lockstat -P sleep 10
```

Esto se ejecuta durante 10 segundos y muestra una tabla con todos los locks que tuvieron contención, junto con sus tiempos de retención y tiempos de espera. Para `myfirst` bajo `mp_stress`, deberías ver `myfirst0` (el mutex del dispositivo) y `myfirst cfg` (el sx) en la parte superior de la lista. Si alguno presenta contención que merezca atención depende de la carga de trabajo; para nuestro pseudodispositivo, ninguno debería tenerla.

`dtrace(1)` te permite rastrear eventos específicos. Para ver cada `cv_signal` en el cv de datos:

```sh
# dtrace -n 'fbt::cv_signal:entry /args[0]->cv_description == "myfirst data"/ { stack(); }'
```

Esto imprime un stack trace del kernel cada vez que se envía la señal. Resulta útil para identificar exactamente qué ruta está enviando la señal y desde dónde.

Ambas herramientas tienen una sobrecarga mínima y pueden usarse tanto en un kernel de producción como en uno de depuración.

### Un ejemplo práctico: un bug de wakeup perdido

Para que el flujo de diagnóstico sea concreto, veamos un bug hipotético de wakeup perdido desde el síntoma hasta la solución.

Realizas un cambio en el driver que rompe sutilmente el emparejamiento de señales en `myfirst_write`. Tras el cambio, la suite de pruebas pasa en su mayor parte, pero `mt_reader` ocasionalmente se queda colgado. Puedes reproducirlo ejecutando `mt_reader` diez o veinte veces; una o dos veces el programa se queda bloqueado en uno de sus threads.

**Paso 1: confirma el síntoma con `procstat`.**

```sh
$ ps -ax | grep mt_reader
12345 ?? S+    mt_reader

$ procstat -kk 12345
  PID    TID COMM             TDNAME           KSTACK
12345 67890 mt_reader        -                mi_switch+0xc1 _cv_wait_sig+0xff
                                              myfirst_wait_data+0x4e
                                              myfirst_read+0x91
                                              dofileread+0x82
                                              sys_read+0xb5
```

El thread está en `_cv_wait_sig`. Al buscar su wait channel:

```sh
$ ps -axHo pid,tid,wchan,command | grep mt_reader
12345 67890 myfirst         mt_reader
12345 67891 -               mt_reader
```

Un thread bloqueado en `myfirst ` (el truncamiento a ocho caracteres de `"myfirst data"`; ver Sección 2). Los demás han terminado. Así que el cv `data_cv` tiene un waiter, y presumiblemente el driver no lo señalizó cuando debía.

**Paso 2: comprueba el estado del cbuf.**

```sh
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 17
```

El buffer tiene 17 bytes. El lector debería poder vaciar esos bytes y devolverlos. ¿Por qué sigue dormido?

**Paso 3: examina el código fuente.** El lector está en `cv_wait_sig(&sc->data_cv, &sc->mtx)`. ¿Quién llama a `cv_signal(&sc->data_cv)`? Busca:

```sh
$ grep -n 'cv_signal(&sc->data_cv\|cv_broadcast(&sc->data_cv' myfirst.c
180:        cv_signal(&sc->data_cv);
220:        cv_broadcast(&sc->data_cv);  /* in detach */
```

Dos puntos de llamada. El relevante es la línea 180, en `myfirst_write`. Obsérvalo:

```c
put = myfirst_buf_write(sc, bounce, want);
fh->writes += put;
MYFIRST_UNLOCK(sc);

if (put > 0) {
        cv_signal(&sc->data_cv);
        selwakeup(&sc->rsel);
}
```

La señal es condicional a `put > 0`. Parece correcto. Pero el bug introducido antes puede haber cambiado algo más. Mira más arriba:

```c
MYFIRST_LOCK(sc);
error = myfirst_wait_room(sc, ioflag, nbefore, uio);
if (error != 0) {
        MYFIRST_UNLOCK(sc);
        return (error == -1 ? 0 : error);
}
room = cbuf_free(&sc->cb);
MYFIRST_UNLOCK(sc);

want = MIN((size_t)uio->uio_resid, sizeof(bounce));
want = MIN(want, room);
error = uiomove(bounce, want, uio);
if (error != 0)
        return (error);

MYFIRST_LOCK(sc);
put = myfirst_buf_write(sc, bounce, want);
```

Aquí está el bug. Después de `uiomove`, el código vuelve directamente a la escritura en el cbuf. Pero ¿qué pasa si `want` se calculó con un valor de `room` desactualizado? Supón que entre la llamada a `cbuf_free` y el segundo `MYFIRST_LOCK`, otro escritor añadió bytes. El segundo `myfirst_buf_write` podría llamarse con un `want` que supera el espacio real disponible.

En nuestro caso, `myfirst_buf_write` devuelve el número real de bytes escritos, que puede ser menor que `want`. Actualizamos `bytes_written` correctamente. Pero luego señalizamos `data_cv` solo si `put > 0`. Hasta aquí todo bien.

Pero espera. Observa la línea con el bug detenidamente: imagina que el cambio introducido fue envolver la señal en una condición diferente:

```c
if (put == want) {  /* WRONG: was put > 0 */
        cv_signal(&sc->data_cv);
        selwakeup(&sc->rsel);
}
```

Ahora, si `put < want` (porque otro escritor se adelantó al espacio disponible), no señalizamos. Los bytes se añadieron al cbuf, pero los lectores no se despiertan. Un lector que esté en `cv_wait_sig` seguirá durmiendo hasta que alguien más escriba un buffer completo.

Ese es el bug de wakeup perdido. La solución es señalizar siempre que `put > 0`, no solo cuando `put == want`. Aplica la corrección, recompila y vuelve a probar. El bloqueo desaparece.

**Paso 4: evita la regresión.** Añade un `KASSERT` en el punto de señalización que documente el contrato:

```c
KASSERT(put <= want, ("myfirst_buf_write returned %zu > want=%zu",
    put, want));
if (put > 0) {
        cv_signal(&sc->data_cv);
        selwakeup(&sc->rsel);
}
```

El KASSERT no detecta el bug que acabamos de corregir (lo provocaba `put != want`, lo cual está permitido). Pero documenta que la condición de señalización es "cualquier avance", que es la regla que el próximo mantenedor debe preservar.

Este ejemplo es hipotético; los bugs reales son más complicados. El patrón es real. Síntoma -> instrumentación -> examen del código fuente -> hipótesis -> corrección -> guardia contra regresión. Practica con los laboratorios de este capítulo.

### Un ejemplo práctico: una inversión del orden de locks

Otro escenario habitual: `WITNESS` informa de una inversión que no entiendes de inmediato.

La advertencia, simplificada:

```text
lock order reversal:
 1st 0xfffffe000a1b2c30 myfirst cfg (myfirst cfg, sx) @ myfirst.c:120
 2nd 0xfffffe000a1b2c50 myfirst0 (myfirst, sleep mutex) @ myfirst.c:240
lock order myfirst cfg -> myfirst0 attempted at myfirst.c:241
where myfirst0 -> myfirst cfg is established at myfirst.c:280
```

**Paso 1: identifica los locks.** `myfirst cfg` es el sx; `myfirst0` es el mutex del dispositivo.

**Paso 2: identifica el orden canónico.** Según la advertencia: `myfirst0 -> myfirst cfg`. Primero el mutex, luego el sx.

**Paso 3: identifica el camino infractor.** La advertencia indica que el camino infractor está en `myfirst.c:241`, donde intenta adquirir `myfirst0` mientras ya tiene `myfirst cfg`. Abre el código fuente en la línea 241 y traza hacia atrás para encontrar cuándo se adquirió el cfg sx (línea 120, según el campo `1st` de la advertencia).

**Paso 4: decide la corrección.** Dos opciones. O bien reordenas el camino infractor para que coincida con el orden canónico (adquirir el mutex primero, luego el sx; suele ser el cambio más económico), o bien aceptas que el camino infractor tiene una razón real para adquirir en orden inverso, en cuyo caso el orden canónico debe cambiar globalmente y actualizar `LOCKING.md`.

Para nuestro `myfirst`, el camino infractor casi con toda seguridad debe seguir el orden canónico. La solución es leer el valor de configuración mediante el patrón snapshot-and-apply: liberar el sx antes de adquirir el mutex.

**Paso 5: verifica.** Aplica la corrección, recompila y vuelve a ejecutar la prueba que disparó la advertencia. `WITNESS` debería estar silencioso ahora. Si no lo está, la advertencia se ha desplazado a otro camino y tienes una segunda infracción que investigar.

### Errores comunes que se ocultan de WITNESS

`WITNESS` es excelente en lo que comprueba, pero no lo comprueba todo. Hay tres tipos de bug que no puede detectar:

**Locks mantenidos a través de punteros a función.** Si una función mantiene el lock A y llama a un callback cuyo puntero a función lo suministra una configuración controlada por el usuario, `WITNESS` no puede predecir qué locks podría adquirir el callback. El orden de los locks con respecto al callback queda sin definir. Evita este patrón; si debes usarlo, documenta los estados de lock aceptables para cualquier callback.

**Condiciones de carrera en campos sin lock.** Un campo al que se accede intencionadamente sin un lock es invisible para `WITNESS`. Si dos threads compiten sobre ese campo y esa competición importa, `WITNESS` no advertirá. Usa atómicas o el lock apropiado; nunca asumas que un campo sin lock es seguro solo porque no aparezca ninguna advertencia.

**Protección incorrecta.** Un campo protegido por un mutex en la ruta de escritura pero leído sin el mutex en la ruta de lectura. El resultado son lecturas fragmentadas intermitentes. `WITNESS` no detecta esto; el procedimiento de auditoría de la Sección 3 del Capítulo 11 sí.

La solución para los tres casos es la disciplina de escribir `LOCKING.md` y mantenerlo preciso. `WITNESS` confirma que los locks que afirmas tener, realmente los tienes; el documento confirma que las reglas que afirmas seguir son las que el diseño pretende.

### Cerrando la sección 6

La depuración de sincronización es primero un vocabulario y luego un conjunto de herramientas. El vocabulario son las seis formas de fallo: despertar perdido, despertar espurio, inversión del orden de locks, destrucción prematura, dormir con un lock no suspendible, y condición de carrera entre detach y una operación activa. Cada una tiene una firma reconocible y una solución estándar. El conjunto de herramientas es `WITNESS`, `INVARIANTS`, `KASSERT`, `sx_assert`, los comandos del depurador integrado en el kernel `show all locks` y `show sleepchain`, y las herramientas de observabilidad en espacio de usuario `lockstat(1)` y `dtrace(1)`.

La sección 7 pone estas herramientas a trabajar en un escenario de estrés realista.



## Sección 7: Pruebas de estrés con patrones de I/O realistas

El kit de estrés del capítulo 11 (`producer_consumer`, `mp_stress`, `mt_reader`, `lat_tester`) probó la ruta de datos bajo cargas de trabajo sencillas con múltiples threads y múltiples procesos. El driver del capítulo 12 tiene nuevas primitivas (cv, sx) y una nueva ruta de código (los sysctls de configuración). Esta sección extiende las pruebas para ejercitar las nuevas superficies y muestra cómo interpretar los datos resultantes.

El objetivo no es una cobertura exhaustiva, sino *realista*. Un driver que supera una carga de trabajo de estrés que se asemeja al tráfico real de producción es un driver en el que puedes confiar.

### Cómo es un escenario realista

Un driver real suele tener:

- Múltiples productores y consumidores ejecutándose de forma concurrente.
- Lecturas de sysctl intercaladas (herramientas de monitorización, paneles de control).
- Escrituras ocasionales de sysctl (cambios de configuración realizados por un administrador).
- Ráfagas de actividad intercaladas con períodos de inactividad.
- Threads de distinta prioridad compitiendo por la CPU.

Una prueba que ejercita un solo eje puede pasar por alto errores que solo aparecen cuando varios ejes interactúan. Por ejemplo, una escritura de sysctl que toma el cfg sx en modo exclusivo mientras una operación de datos está a mitad de su ejecución podría exponer un problema sutil de ordenación que las pruebas de I/O puro no detectarían.

### Una carga de trabajo compuesta

Construye un script que ejecute tres cosas a la vez durante un período fijo:

```sh
#!/bin/sh
# Composite stress: I/O + sysctl readers + sysctl writers.

DUR=60

(./mp_stress &) >/tmp/mp.log
(./mt_reader &) >/tmp/mt.log
(./config_writer $DUR &) >/tmp/cw.log

# Burst sysctl readers
for i in 1 2 3 4; do
    (while sleep 0.5; do
        sysctl -q dev.myfirst.0.stats >/dev/null
        sysctl -q dev.myfirst.0.debug_level >/dev/null
        sysctl -q dev.myfirst.0.soft_byte_limit >/dev/null
    done) &
done
SREAD_PIDS=$!

sleep $DUR

# Stop everything
pkill -f mp_stress
pkill -f mt_reader
pkill -f config_writer
kill $SREAD_PIDS 2>/dev/null

wait

echo "=== mp_stress ==="
cat /tmp/mp.log
echo "=== mt_reader ==="
cat /tmp/mt.log
echo "=== config_writer ==="
cat /tmp/cw.log
```

El script se ejecuta durante un minuto. Durante ese minuto, el driver observa:

- Dos procesos de escritura enviando escrituras de forma intensa.
- Dos procesos de lectura enviando lecturas de forma intensa.
- Cuatro pthreads en `mt_reader` enviando lecturas intensas sobre un único descriptor.
- El `config_writer` alternando el nivel de depuración y el límite de bytes suave cada 10 ms.
- Cuatro bucles de shell leyendo sysctls cada 0,5 segundos.

Con un kernel de depuración con `WITNESS`, esta actividad es suficiente para detectar la mayoría de los errores de ordenación de locks y de emparejamiento de señales. Ejecútalo. Si completa sin pánico, sin advertencias de `WITNESS` y con recuentos de bytes consistentes en los lectores y escritores, el driver ha superado una prueba de sincronización significativa.

### Variante de larga duración

Para los errores más sutiles, ejecuta la prueba compuesta durante una hora:

```sh
$ for i in $(seq 1 60); do
    ./composite_stress.sh
    echo "iteration $i complete"
    sleep 5
  done
```

Sesenta iteraciones de una prueba de un minuto proporcionan una hora de cobertura acumulada. Los errores que aparecen una vez por millón de eventos (que es aproximadamente lo que produce una hora de `mp_stress` en una máquina moderna) suelen surgir durante esta ejecución.

### Latencia bajo carga mixta

El `lat_tester` del capítulo 11 medía la latencia de una única lectura sin ninguna otra carga. Bajo una carga realista, la latencia cuenta una historia diferente: incluye el tiempo de espera por el mutex, el tiempo de espera por el sx y el tiempo dentro de `cv_wait_sig`.

Ejecuta `lat_tester` mientras `mp_stress` y `config_writer` están en marcha. El histograma debería mostrar una cola más larga que en el caso sin carga. Unos pocos microsegundos para las operaciones sin contención, unas pocas decenas de microsegundos cuando el mutex está brevemente retenido por otro thread, y un pequeño pico de milisegundos cuando el cv tuvo que esperar realmente para recibir datos. Si la cola se extiende hasta los segundos, algo va mal.

### Lectura de la salida de lockstat

`lockstat(1)` es la herramienta canónica para medir la contención de locks. Ejecútala durante un estrés intenso:

```sh
# lockstat -P sleep 30 > /tmp/lockstat.out
```

La opción `-P` incluye datos de spinlock; sin ella, solo se informan los adaptive locks. El 30 significa "muestrear durante 30 segundos".

La salida está organizada por lock, con estadísticas de tiempo de retención y tiempo de espera. Para nuestro driver, busca líneas que mencionen `myfirst0` (el mtx), `myfirst cfg` (el sx) y los cvs (`myfirst data`, `myfirst room`).

Un resultado saludable para `myfirst` bajo un estrés típico:

- El mtx se adquiere millones de veces. El tiempo de retención por adquisición es de decenas de nanosegundos. El tiempo de espera es ocasional y pequeño.
- El sx se adquiere decenas de miles de veces. La mayoría de las adquisiciones son compartidas; las pocas adquisiciones exclusivas corresponden a escrituras de sysctl. El tiempo de retención es bajo.
- Los cvs se señalizan y se difunden en proporción a la tasa de I/O. Los recuentos de espera en cada cv corresponden al número de veces que un lector o escritor tuvo que bloquearse realmente.

Si algún lock muestra un tiempo de espera que supone una fracción significativa del tiempo total, ese lock está bajo contención. La solución es una de las siguientes: secciones críticas más cortas, granularidad de locking más fina, o un primitivo diferente.

Para nuestro pseudodispositivo con un diseño de mutex único en la ruta de datos, el mtx llegará a saturación alrededor de los 4-8 núcleos, dependiendo de la velocidad de las operaciones cbuf. Esto es lo esperado; no hemos optimizado para un elevado número de núcleos. El objetivo del capítulo es la corrección, no el rendimiento.

### Trazado con dtrace

Cuando un evento específico necesita visibilidad, `dtrace` es la herramienta adecuada. Ejemplo: contar cuántas veces se señalizó cada cv durante una ventana de 10 segundos:

```sh
# dtrace -n 'fbt::cv_signal:entry { @[args[0]->cv_description] = count(); }' \
    -n 'fbt::cv_broadcastpri:entry { @[args[0]->cv_description] = count(); }' \
    -n 'tick-10sec { exit(0); }'
```

Tras 10 segundos, dtrace imprime una tabla:

```text
 myfirst data           48512
 myfirst room           48317
 ...
```

Los números deberían ser aproximadamente iguales para `data_cv` y `room_cv` si la carga de trabajo es simétrica (igual número de lecturas y escrituras). Un desequilibrio grande sugiere que un lado duerme más que el otro, lo que habitualmente indica un problema de control de flujo.

Otra línea de comandos útil: histograma de latencia de cv_wait en el cv de datos:

```sh
# dtrace -n '
fbt::_cv_wait_sig:entry /args[0]->cv_description == "myfirst data"/ {
    self->ts = timestamp;
}
fbt::_cv_wait_sig:return /self->ts/ {
    @ = quantize(timestamp - self->ts);
    self->ts = 0;
}
tick-10sec { exit(0); }
'
```

El histograma muestra la distribución de los tiempos que los threads pasaron dentro de `_cv_wait_sig`. La mayoría deberían ser cortos (señalizados con prontitud). Una cola larga indica threads que duermen durante períodos prolongados, lo cual es normal para un dispositivo inactivo pero sospechoso para uno ocupado.

### Monitorización con vmstat y top

Para una vista más general, `vmstat` y `top` ejecutándose en segundo plano proporcionan contexto.

`vmstat 1` muestra estadísticas por segundo: tiempo de CPU empleado en espacio de usuario, en sistema y en reposo; cambios de contexto; interrupciones. Durante una ejecución de estrés, `sy` (tiempo de sistema) debería subir; `cs` (cambios de contexto) debería subir también, debido a la señalización de los cvs.

`top -SH` (la opción `-S` muestra los procesos del sistema; `-H` muestra los threads individuales) muestra el uso de CPU por thread. Durante una ejecución de estrés, los threads de prueba deberían ser visibles. La columna `WCHAN` muestra en qué están esperando; se espera ver las descripciones truncadas de los cvs (`myfirst ` tanto para `data_cv` como para `room_cv`, ya que la palabra final queda cortada por `WMESGLEN`) más, para cualquier thread que siga usando el canal anónimo del capítulo 11, la dirección de `&sc->cb` impresa como una pequeña cadena numérica.

Ambas herramientas son útiles como acompañantes de fondo durante una ejecución de estrés prolongada. No producen datos estructurados, pero confirman de un vistazo que las cosas están ocurriendo.

### Observación de los sysctls

Una comprobación de cordura sencilla durante el estrés: leer los sysctls periódicamente y verificar que tienen sentido.

```sh
$ while sleep 1; do
    sysctl dev.myfirst.0.stats.bytes_read \
           dev.myfirst.0.stats.bytes_written \
           dev.myfirst.0.stats.cb_used \
           dev.myfirst.0.debug_level \
           dev.myfirst.0.soft_byte_limit
  done
```

Los contadores de bytes deberían aumentar de forma monotónica. El `cb_used` debería mantenerse dentro de algún rango. La configuración debería cambiar a medida que `config_writer` la actualiza.

Si cualquier lectura de sysctl se bloquea (el comando `sysctl` no retorna), hay un problema de sincronización con el manejador de sysctl. Probablemente un mutex retenido que impide que el sysctl adquiera el sx, o viceversa. Usa `procstat -kk $$` desde otro terminal para ver en qué está esperando el shell bloqueado.

### Criterios de aceptación de la prueba de estrés

Un driver supera una prueba de estrés de sincronización si:

1. El script compuesto completa sin pánico.
2. `WITNESS` no reporta advertencias (`dmesg | grep -i witness | wc -l` devuelve cero).
3. Los recuentos de bytes de lectores y escritores se encuentran dentro del 1% entre sí (una pequeña deriva es aceptable debido al momento en que se detiene la prueba).
4. `lockstat(1)` no muestra ningún lock con un tiempo de espera superior al 5% del tiempo total.
5. El histograma de latencia de `lat_tester` muestra el percentil 99 por debajo de un milisegundo para un dispositivo inactivo, o por debajo del timeout configurado para uno ocupado.
6. Las ejecuciones repetidas (el bucle de larga duración) pasan todas.

Estos no son umbrales absolutos; son los valores que han resultado útiles en el ejemplo del capítulo. Los drivers reales pueden tener límites más estrictos o más laxos en función de su carga de trabajo.

### Interpretación detallada de la salida de lockstat

`lockstat(1)` produce tablas que pueden resultar intimidantes al primer encuentro. Un breve recorrido por las columnas las desmitifica.

Una línea típica para un lock bajo contención:

```text
Adaptive mutex spin: 1234 events in 30.000 seconds (41.13 events/sec)

------------------------------------------------------------------------
   Count   nsec     ----- Lock -----                       Hottest Caller
   1234     321     myfirst0                              myfirst_read+0x91
```

Qué significan las columnas:

- `Count`: número de eventos de este tipo (adquisiciones, en este caso).
- `nsec`: duración media del evento (aquí, tiempo medio en spin antes de adquirir el lock).
- `Lock`: el nombre del lock.
- `Hottest Caller`: la función que con mayor frecuencia experimentó este evento.

Más abajo en la salida:

```text
Adaptive mutex block: 47 events in 30.000 seconds (1.57 events/sec)

------------------------------------------------------------------------
   Count   nsec     ----- Lock -----                       Hottest Caller
     47   58432     myfirst0                              myfirst_read+0x91
```

El evento "block" es cuando el spin falló y el thread tuvo que dormir realmente. El tiempo medio de sueño fue de 58 microsegundos. Eso es alto; significa que un escritor retenía el mutex durante lo que debería haber sido una sección crítica corta.

En conjunto, los eventos de spin (1234) y los eventos de block (47) nos indican que el lock estuvo bajo contención 1281 veces en 30 segundos, y que el 96% de las veces el spin tuvo éxito. Ese es un patrón saludable: la mayoría de la contención es breve, y solo la retención prolongada ocasional provoca un sueño real.

Para los sleep locks (sx, cv), las columnas son similares, pero los eventos se categorizan de forma diferente:

```text
SX shared block: 2014 events in 30.000 seconds (67.13 events/sec)

------------------------------------------------------------------------
   Count   nsec     ----- Lock -----                       Hottest Caller
   2014    2105     myfirst cfg                            myfirst_get_debug_level+0x12
```

Esto indica: los waiters compartidos en el cfg sx se bloquearon 2014 veces, con una espera media de 2,1 microsegundos, principalmente desde el helper del nivel de depuración. Con un `config_writer` en ejecución, eso es lo esperado. Sin el escritor, debería estar cerca de cero.

La habilidad clave para leer la salida de `lockstat` es la calibración: saber qué números son los esperados para tu carga de trabajo. Un driver que nunca se ha medido bajo carga es un driver cuyos números esperados se desconocen. Ejecuta `lockstat` una vez con una carga de trabajo conocida y guarda la salida como línea base. Las ejecuciones futuras se comparan entonces con esa línea base; las desviaciones significativas son una señal.

### Trazado de rutas de código específicas con dtrace

Más allá de los ejemplos anteriores de conteo de cvs y latencia de sueño, hay algunas recetas más de `dtrace` que resultan útiles para un driver del estilo del capítulo 12.

**Contar las esperas de cv por cv por segundo:**

```sh
# dtrace -n '
fbt::_cv_wait_sig:entry { @[args[0]->cv_description] = count(); }
tick-1sec { printa(@); trunc(@); }'
```

Imprime un recuento por segundo de las esperas de cv, desglosado por nombre de cv. Útil para detectar ráfagas.

**Trazar qué thread adquiere el cfg sx de forma exclusiva:**

```sh
# dtrace -n '
fbt::_sx_xlock:entry /args[0]->lock_object.lo_name == "myfirst cfg"/ {
    printf("%s pid %d acquires cfg xlock\n", execname, pid);
    stack();
}'
```

Útil para confirmar que los únicos escritores son los manejadores de sysctl, y no alguna otra ruta inesperada.

**Histograma de latencia de myfirst_read:**

```sh
# dtrace -n '
fbt::myfirst_read:entry { self->ts = timestamp; }
fbt::myfirst_read:return /self->ts/ {
    @ = quantize(timestamp - self->ts);
    self->ts = 0;
}
tick-30sec { exit(0); }'
```

El mismo patrón que el histograma de latencia de cv_wait, pero a nivel del manejador. Incluye el tiempo pasado dentro de `cv_wait_sig` más el tiempo dentro de las operaciones cbuf y del `uiomove`.

Estas recetas son puntos de partida. El proveedor `dtrace` para funciones del kernel (`fbt`) da acceso a la entrada y el retorno de cada función; el lenguaje es suficientemente rico como para expresar casi cualquier agregación.

### Cerrando la sección 7

Las pruebas de estrés realistas ejercitan el driver completo, no solo una ruta. Una carga de trabajo compuesta que combina I/O, lecturas de sysctl y escrituras de sysctl detecta errores de ordenación de locks que las pruebas de I/O puro no detectarían. `lockstat(1)` y `dtrace(1)` te dan observabilidad de la actividad de locks y cvs sin modificar el driver. Un driver que supera el kit de estrés compuesto en un kernel con `WITNESS` durante una hora es un driver que puedes llevar al siguiente capítulo con confianza.

La sección 8 cierra el capítulo con el trabajo de mantenimiento: el repaso de la documentación, el incremento de versión, la prueba de regresión y la entrada en el changelog que le dice a tu yo futuro qué hiciste y por qué.



## Sección 8: Refactorización y versionado de tu driver sincronizado

El driver utiliza ahora tres primitivas (`mtx`, `cv`, `sx`), tiene dos clases de lock con un orden documentado, soporta lecturas interrumpibles y con tiempo límite, y dispone de un pequeño subsistema de configuración. El trabajo restante es la pasada de mantenimiento: limpiar el código fuente para mayor claridad, actualizar la documentación, incrementar la versión, ejecutar el análisis estático y validar que las pruebas de regresión pasan.

Esta sección cubre cada uno de estos puntos. Ninguno resulta especialmente vistoso. Todos ellos son lo que separa un driver que funciona de uno que se puede mantener.

### Limpieza del código fuente

Tras un capítulo de cambios concentrados, el código fuente ha acumulado ciertas inconsistencias que merece la pena corregir.

**Agrupa el código relacionado.** Mueve todos los helpers relacionados con cv junto a los demás (los helpers de espera, las llamadas de señalización, y los `cv_init`/`cv_destroy` en attach/detach). Mueve todos los helpers relacionados con sx agrupados entre sí. Al compilador no le importa el orden, pero a un lector sí.

**Estandariza el vocabulario de macros.** El Capítulo 11 introdujo `MYFIRST_LOCK`, `MYFIRST_UNLOCK`, `MYFIRST_ASSERT`. Añade el conjunto simétrico para el sx:

```c
#define MYFIRST_CFG_SLOCK(sc)   sx_slock(&(sc)->cfg_sx)
#define MYFIRST_CFG_SUNLOCK(sc) sx_sunlock(&(sc)->cfg_sx)
#define MYFIRST_CFG_XLOCK(sc)   sx_xlock(&(sc)->cfg_sx)
#define MYFIRST_CFG_XUNLOCK(sc) sx_xunlock(&(sc)->cfg_sx)
#define MYFIRST_CFG_ASSERT_X(sc) sx_assert(&(sc)->cfg_sx, SA_XLOCKED)
#define MYFIRST_CFG_ASSERT_S(sc) sx_assert(&(sc)->cfg_sx, SA_SLOCKED)
```

Ahora cada adquisición de lock en el driver pasa por una macro. Si más adelante cambiamos de `sx` a `rw`, el cambio estará en una sola cabecera, no disperso por el código fuente.

**Elimina el código muerto.** Si algún helper del Capítulo 11 ya no se llama (quizás el antiguo canal de wakeup ha desaparecido), elimínalo. El código muerto genera confusión.

**Comenta las partes no evidentes.** Cada adquisición de lock que sigue la regla del orden de lock merece un comentario de una línea. Cada lugar donde se usa el patrón snapshot-and-apply merece un comentario que explique el motivo. El locking es la parte más sutil del driver; los comentarios deben reflejarlo.

### Actualización de LOCKING.md

El archivo `LOCKING.md` del Capítulo 11 documentaba un lock y un pequeño conjunto de campos. El driver del Capítulo 12 tiene más que contar. La nueva versión:

```markdown
# myfirst Locking Strategy

Version 0.6-sync (Chapter 12).

## Overview

The driver uses three synchronization primitives: a sleep mutex
(sc->mtx) for the data path, an sx lock (sc->cfg_sx) for the
configuration subsystem, and two condition variables (sc->data_cv,
sc->room_cv) for blocking reads and writes. Byte counters use
counter(9) per-CPU counters and protect themselves.

## Locks Owned by This Driver

### sc->mtx (mutex(9), MTX_DEF)

Protects:
- sc->cb (the circular buffer's internal state)
- sc->open_count, sc->active_fhs
- sc->is_attached (writes; reads at handler entry may be unprotected
  as an optimization, re-checked after every sleep)

### Lock-Free Plain Integers

- sc->read_timeout_ms, sc->write_timeout_ms: plain ints, accessed
  without locking. Safe because aligned int reads and writes are
  atomic on every architecture FreeBSD supports, and the values are
  advisory; a stale read just produces a slightly different timeout
  for the next wait. The sysctl framework writes them directly via
  CTLFLAG_RW.

### sc->cfg_sx (sx(9))

Protects:
- sc->cfg.debug_level
- sc->cfg.soft_byte_limit
- sc->cfg.nickname

Shared mode: every read of any cfg field.
Exclusive mode: every write of any cfg field.

### sc->data_cv (cv(9))

Wait condition: data is available in the cbuf.
Interlock: sc->mtx.
Signalled by: myfirst_write after a successful cbuf write.
Broadcast by: myfirst_detach.
Waiters: myfirst_read in myfirst_wait_data.

### sc->room_cv (cv(9))

Wait condition: room is available in the cbuf.
Interlock: sc->mtx.
Signalled by: myfirst_read after a successful cbuf read, and
myfirst_sysctl_reset after resetting the cbuf.
Broadcast by: myfirst_detach.
Waiters: myfirst_write in myfirst_wait_room.

## Lock-Free Fields

- sc->bytes_read, sc->bytes_written: counter_u64_t. Updates via
  counter_u64_add; reads via counter_u64_fetch.

## Lock Order

sc->mtx -> sc->cfg_sx

A thread holding sc->mtx may acquire sc->cfg_sx in either mode.
A thread holding sc->cfg_sx may NOT acquire sc->mtx.

Rationale: the data path always holds sc->mtx and may need to read
configuration during its critical section. The configuration path
(sysctl writers) does not need the data mutex; if a future feature
requires both, it must acquire sc->mtx first.

## Locking Discipline

1. Acquire mutex with MYFIRST_LOCK(sc), release with MYFIRST_UNLOCK(sc).
2. Acquire sx in shared mode with MYFIRST_CFG_SLOCK, exclusive with
   MYFIRST_CFG_XLOCK. Release with the matching unlock.
3. Wait on a cv with cv_wait_sig (interruptible) or
   cv_timedwait_sig (interruptible + bounded).
4. Signal a cv with cv_signal (one waiter) or cv_broadcast (all
   waiters). Use cv_broadcast only for state changes that affect all
   waiters (detach, configuration reset).
5. NEVER hold sc->mtx across uiomove(9), copyin(9), copyout(9),
   selwakeup(9), or wakeup(9). Each of these may sleep or take
   other locks. cv_wait_sig is the exception (it atomically drops
   the interlock).
6. NEVER hold sc->cfg_sx across uiomove(9) etc., for the same
   reason.
7. All cbuf_* calls must happen with sc->mtx held (the helpers
   assert MA_OWNED).
8. The detach path clears sc->is_attached under sc->mtx, broadcasts
   both cvs, and refuses detach while active_fhs > 0.

## Snapshot-and-Apply Pattern

When a path needs both sc->mtx and sc->cfg_sx, it should follow
the snapshot-and-apply pattern:

  1. sx_slock(&sc->cfg_sx); read cfg into local variables;
     sx_sunlock(&sc->cfg_sx).
  2. MYFIRST_LOCK(sc); do cbuf operations using the snapshot;
     MYFIRST_UNLOCK(sc).

The snapshot may be slightly stale by the time it is used. For
configuration values that are advisory (debug level, soft byte
limit), this is acceptable.

## Known Non-Locked Accesses

### sc->is_attached at handler entry

Unprotected plain read. Safe because:
- A stale "true" is re-checked after every sleep via
  if (!sc->is_attached) return (ENXIO).
- A stale "false" causes the handler to return ENXIO early, which
  is also what it would do with a fresh false.

### sc->open_count, sc->active_fhs at sysctl read time

Unprotected plain loads. Safe on amd64 and arm64 (aligned 64-bit
loads are atomic). Acceptable on i386 because the torn read, if
it ever happened, would produce a single bad statistic with no
correctness impact.

## Wait Channels

- sc->data_cv: data has become available.
- sc->room_cv: room has become available.

(The legacy &sc->cb wakeup channel from Chapter 10 has been
retired in Chapter 12.)

## History

- 0.6-sync (Chapter 12): added cv channels, sx for configuration,
  bounded reads via cv_timedwait_sig.
- 0.5-kasserts (Chapter 11, Stage 5): KASSERT calls added
  throughout cbuf helpers and wait helpers.
- 0.5-counter9 (Chapter 11, Stage 3): byte counters migrated to
  counter(9).
- 0.5-concurrency (Chapter 11, Stage 2): MYFIRST_LOCK/UNLOCK/ASSERT
  macros, explicit locking strategy.
- Earlier versions: see Chapter 10 / Chapter 11 history.
```

Ese documento es ahora la descripción autoritativa de la historia de sincronización del driver. Cualquier cambio futuro actualiza el documento en el mismo commit que el cambio de código. Un revisor que quiera saber si un cambio es seguro lee el diff frente al documento, no frente al código.

### Incremento de la versión

Actualiza la cadena de versión:

```c
#define MYFIRST_VERSION "0.6-sync"
```

Imprímela en attach (la línea `device_printf` existente en attach ya incluye la versión):

```c
device_printf(dev,
    "Attached; version %s, node /dev/%s (alias /dev/myfirst), "
    "cbuf=%zu bytes\n",
    MYFIRST_VERSION, devtoname(sc->cdev), cbuf_size(&sc->cb));
```

Actualiza el changelog:

```markdown
## 0.6-sync (Chapter 12)

- Replaced anonymous wakeup channel (&sc->cb) with two named
  condition variables (sc->data_cv, sc->room_cv).
- Added bounded read support via sc->read_timeout_ms sysctl,
  using cv_timedwait_sig under the hood.
- Added a small configuration subsystem (sc->cfg) protected by
  an sx lock (sc->cfg_sx).
- Added sysctl handlers for debug_level, soft_byte_limit, and
  nickname.
- Added myfirst_sysctl_reset that takes both locks in the canonical
  order to clear the cbuf and reset counters.
- Updated LOCKING.md with the new primitives, the lock order, and
  the snapshot-and-apply pattern.
- Added MYFIRST_CFG_* macros symmetric with the existing MYFIRST_*
  mutex macros.
- All Chapter 11 tests continue to pass; new sysctl-based tests
  added under userland/.
```

### Actualización del README

El README del Capítulo 11 nombraba el driver y describía sus características. El README del Capítulo 12 añade las nuevas:

```markdown
# myfirst

A FreeBSD 14.3 pseudo-device driver that demonstrates buffered I/O,
concurrency, and modern synchronization primitives. Developed as the
running example for the book "FreeBSD Device Drivers: From First
Steps to Kernel Mastery."

## Status

Version 0.6-sync (Chapter 12).

## Features

- A Newbus pseudo-device under nexus0.
- A primary device node at /dev/myfirst/0 (alias: /dev/myfirst).
- A circular buffer (cbuf) as the I/O buffer.
- Blocking, non-blocking, and timed reads and writes.
- poll(2) support via d_poll and selinfo.
- Per-CPU byte counters via counter(9).
- A single sleep mutex protects composite cbuf state; see LOCKING.md.
- Two named condition variables (data_cv, room_cv) coordinate read
  and write blocking.
- An sx lock protects the runtime configuration (debug_level,
  soft_byte_limit, nickname).

## Configuration

Three runtime-tunable parameters via sysctl:

- dev.myfirst.<unit>.debug_level (0-3): controls dmesg verbosity.
- dev.myfirst.<unit>.soft_byte_limit: refuse writes that would
  push cb_used above this threshold (0 = no limit).
- dev.myfirst.<unit>.nickname: a string used in log messages.
- dev.myfirst.<unit>.read_timeout_ms: bound a blocking read.

(The last is per-instance; see myfirst.4 for details, when written.)

## Build and Load

    $ make
    # kldload ./myfirst.ko
    # dmesg | tail
    # ls -l /dev/myfirst
    # printf 'hello' > /dev/myfirst
    # cat /dev/myfirst
    # kldunload myfirst

## Tests

See ../../userland/ for the test programs. The Chapter 12 tests
include config_writer (toggles sysctls during stress) and
timeout_tester (verifies bounded reads).

## License

BSD 2-Clause. See individual source files for SPDX headers.
```

### Ejecución del análisis estático

Ejecuta `clang --analyze` sobre el driver del Capítulo 12:

```sh
$ make WARNS=6 clean all
$ clang --analyze -D_KERNEL -I/usr/src/sys \
    -I/usr/src/sys/amd64/conf/GENERIC myfirst.c
```

Clasifica el resultado. Los nuevos avisos desde el Capítulo 11 deben ser:

1. Falsos positivos (clang no entiende la disciplina de locking). Documenta cada uno.
2. Errores reales. Corrígelos todos.

Los falsos positivos habituales en código de drivers implican las macros `sx_assert` y `mtx_assert` que clang no puede analizar en profundidad; el analizador cree que el lock podría no estar adquirido incluso cuando el assert demuestra que sí lo está. Es aceptable silenciarlos con `__assert_unreachable()` o reestructurando el código para hacer el estado del lock más obvio para el analizador.

### Ejecución de la suite de regresión

El script de regresión del Capítulo 11 se extiende de forma natural:

```sh
#!/bin/sh
# regression.sh: full Chapter 12 regression.

set -eu

die() { echo "FAIL: $*" >&2; exit 1; }
ok()  { echo "PASS: $*"; }

[ $(id -u) -eq 0 ] || die "must run as root"
kldstat | grep -q myfirst && kldunload myfirst
[ -f ./myfirst.ko ] || die "myfirst.ko not built; run make first"

kldload ./myfirst.ko
trap 'kldunload myfirst 2>/dev/null || true' EXIT

sleep 1
[ -c /dev/myfirst ] || die "device node not created"
ok "load"

# Chapter 7-10 tests.
printf 'hello' > /dev/myfirst || die "write failed"
cat /dev/myfirst >/tmp/out.$$
[ "$(cat /tmp/out.$$)" = "hello" ] || die "round-trip content mismatch"
rm -f /tmp/out.$$
ok "round-trip"

cd ../userland && make -s clean && make -s && cd -

../userland/producer_consumer || die "producer_consumer failed"
ok "producer_consumer"

../userland/mp_stress || die "mp_stress failed"
ok "mp_stress"

# Chapter 12-specific tests.
../userland/timeout_tester || die "timeout_tester failed"
ok "timeout_tester"

../userland/config_writer 5 &
CW=$!
../userland/mt_reader || die "mt_reader (under config writer) failed"
wait $CW
ok "mt_reader under config writer"

sysctl dev.myfirst.0.stats >/dev/null || die "sysctl stats not accessible"
sysctl dev.myfirst.0.debug_level >/dev/null || die "sysctl debug_level not accessible"
sysctl dev.myfirst.0.soft_byte_limit >/dev/null || die "sysctl soft_byte_limit not accessible"
ok "sysctl"

# WITNESS check.
WITNESS_HITS=$(dmesg | grep -ci "witness\|lor" || true)
if [ "$WITNESS_HITS" -gt 0 ]; then
    die "WITNESS warnings detected ($WITNESS_HITS lines)"
fi
ok "witness clean"

echo "ALL TESTS PASSED"
```

Una ejecución limpia tras cada commit es el mínimo exigible. Una ejecución limpia en un kernel con `WITNESS` tras una ejecución compuesta de larga duración es el listón más alto.

### Lista de verificación previa al commit

La lista de verificación del Capítulo 11 recibe dos elementos nuevos para el Capítulo 12:

1. ¿He actualizado `LOCKING.md` con los nuevos locks, cvs o cambios de orden?
2. ¿He ejecutado la suite de regresión completa en un kernel con `WITNESS`?
3. ¿He ejecutado el estrés compuesto de larga duración durante al menos 30 minutos?
4. ¿He ejecutado `clang --analyze` y clasificado cada nuevo aviso?
5. ¿He añadido un `sx_assert` o `mtx_assert` para cualquier nuevo helper que espere un estado de lock determinado?
6. ¿He incrementado la cadena de versión y actualizado `CHANGELOG.md`?
7. ¿He verificado que el kit de pruebas compila y se ejecuta?
8. **(Nuevo)** ¿He comprobado que cada cv tiene tanto un señalizador como una condición documentada?
9. **(Nuevo)** ¿He comprobado que cada `sx_xlock` tiene un `sx_xunlock` correspondiente en cada ruta de código, incluidas las rutas de error?

Los dos nuevos elementos capturan los errores más comunes en código del estilo del Capítulo 12. Una cv sin señalizador es peso muerto (los procesos en espera nunca despertarán). Un `sx_xlock` sin un unlock correspondiente en una ruta de error es un deadlock silencioso esperando materializarse.

### Cerrando la sección 8

El driver es ahora no solo correcto, sino verificablemente correcto, bien documentado y versionado. Cuenta con:

- Un `LOCKING.md` actualizado que describe tres primitivas, dos clases de lock y un orden de lock canónico.
- Una nueva cadena de versión (0.6-sync) que refleja el trabajo del Capítulo 12.
- Un script de regresión que ejercita cada primitiva y valida la limpieza de `WITNESS`.
- Una lista de verificación previa al commit que detecta los dos nuevos modos de fallo que el Capítulo 12 ha introducido.

Con esto concluye el arco principal de enseñanza del capítulo. A continuación vienen los laboratorios y los desafíos.



## Laboratorios prácticos

Estos laboratorios consolidan los conceptos del Capítulo 12 mediante experiencia práctica directa. Están ordenados de menor a mayor dificultad. Cada uno está diseñado para completarse en una sola sesión de laboratorio.

### Lista de verificación previa al laboratorio

Antes de comenzar cualquier laboratorio, confirma los puntos siguientes. Se aplica la lista del Capítulo 11; añadimos tres específicos del Capítulo 12.

1. **Kernel de depuración en ejecución.** `sysctl kern.ident` informa del kernel con `INVARIANTS` y `WITNESS`.
2. **WITNESS activo.** `sysctl debug.witness.watch` devuelve un valor distinto de cero.
3. **El código fuente del driver coincide con el Stage 5 del Capítulo 11 (kasserts).** Desde el directorio de tu driver, `make clean && make` debe compilar sin errores.
4. **Un dmesg limpio.** Ejecuta `dmesg -c >/dev/null` una vez antes del primer laboratorio.
5. **(Nuevo)** **Userland de apoyo compilado.** Desde `examples/part-03/ch12-synchronization-mechanisms/userland/`, `make` debe producir los binarios `config_writer` y `timeout_tester`.
6. **(Nuevo)** **Kit de estrés del Capítulo 11 disponible.** Los laboratorios reutilizan `mp_stress`, `mt_reader` y `producer_consumer` del Capítulo 11.
7. **(Nuevo)** **Copia de seguridad del Stage 5.** Copia el driver Stage 5 funcional a un lugar seguro antes de iniciar cualquier laboratorio que modifique el código fuente. Varios laboratorios introducen errores intencionadamente que deben revertirse de forma limpia.

### Laboratorio 12.1: Sustitución de canales de wakeup anónimos por variables de condición

**Objetivo.** Convertir el driver del Capítulo 11 de usar `mtx_sleep`/`wakeup` sobre el canal anónimo `&sc->cb` a dos variables de condición con nombre (`data_cv` y `room_cv`).

**Pasos.**

1. Copia tu driver Stage 5 en `examples/part-03/ch12-synchronization-mechanisms/stage1-cv-channels/`.
2. Añade `struct cv data_cv` y `struct cv room_cv` a `struct myfirst_softc`.
3. En `myfirst_attach`, llama a `cv_init(&sc->data_cv, "myfirst data")` y `cv_init(&sc->room_cv, "myfirst room")`. Colócalas tras la inicialización del mutex.
4. En `myfirst_detach`, antes de `mtx_destroy`, llama a `cv_broadcast` en cada cv para despertar a cualquier proceso en espera y, a continuación, `cv_destroy` en cada una.
5. Sustituye las llamadas a `mtx_sleep(&sc->cb, ...)` en `myfirst_wait_data` y `myfirst_wait_room` por `cv_wait_sig(&sc->data_cv, &sc->mtx)` y `cv_wait_sig(&sc->room_cv, &sc->mtx)` respectivamente.
6. Sustituye las llamadas a `wakeup(&sc->cb)` en `myfirst_read` y `myfirst_write` por `cv_signal(&sc->room_cv)` y `cv_signal(&sc->data_cv)` respectivamente. Observa el intercambio: una lectura exitosa libera espacio (por lo que se despiertan los escritores); una escritura exitosa produce datos (por lo que se despiertan los lectores).
7. Compila, carga y ejecuta el kit de estrés del Capítulo 11.

**Verificación.** Todas las pruebas del Capítulo 11 pasan. `procstat -kk` aplicado a un lector en espera muestra el canal de espera `myfirst ` (la forma truncada de `"myfirst data"`; consulta la nota de la Sección 2 sobre `WMESGLEN`). Sin avisos de `WITNESS`.

**Objetivo adicional.** Usa `dtrace` para contar las señales a cada cv durante `mp_stress`. Confirma que el número de señales es aproximadamente igual entre `data_cv` y `room_cv` (porque las lecturas y escrituras son aproximadamente iguales).

### Laboratorio 12.2: Añadir lecturas acotadas

**Objetivo.** Añadir un sysctl `read_timeout_ms` que acote las lecturas bloqueantes.

**Pasos.**

1. Copia el Laboratorio 12.1 en `stage2-bounded-read/`.
2. Añade un campo `int read_timeout_ms` al softc. Inicialízalo a 0 en attach.
3. Registra un `SYSCTL_ADD_INT` para él bajo `dev.myfirst.<unit>.read_timeout_ms`, con `CTLFLAG_RW`.
4. Modifica `myfirst_wait_data` para usar `cv_timedwait_sig` cuando `read_timeout_ms > 0`, convirtiendo milisegundos a ticks. Traduce `EWOULDBLOCK` a `EAGAIN`.
5. Compila y carga.
6. Compila el `timeout_tester` desde `examples/part-03/ch12-synchronization-mechanisms/userland/`.
7. Ajusta el sysctl a 100, ejecuta `timeout_tester` y observa que `read(2)` devuelve `EAGAIN` tras unos 100 ms.
8. Restablece el sysctl a 0 y ejecuta `timeout_tester` de nuevo. La lectura se bloquea hasta que pulsas Ctrl-C y devuelve `EINTR`.

**Verificación.** La salida de `timeout_tester` se ajusta a lo esperado tanto en el caso de timeout como en el de interrupción por señal. El kit de estrés sigue pasando.

**Objetivo adicional.** Añade un sysctl simétrico `write_timeout_ms` y verifica que acota las escrituras cuando el buffer está lleno.

### Laboratorio 12.3: Añadir un subsistema de configuración protegido por sx

**Objetivo.** Añadir la estructura `cfg` y el lock `cfg_sx` de la Sección 5; exponer `debug_level` como sysctl.

**Pasos.**

1. Copia el Laboratorio 12.2 en `stage3-sx-config/`.
2. Añade `struct sx cfg_sx` y `struct myfirst_config cfg` al softc. Inicializa en attach (`sx_init(&sc->cfg_sx, "myfirst cfg")`; valores por defecto para los campos de cfg). Destruye en detach.
3. Añade un manejador `myfirst_sysctl_debug_level` siguiendo el patrón snapshot-and-apply. Regístralo.
4. Añade una macro `MYFIRST_DBG(sc, level, fmt, ...)` que consulte `sc->cfg.debug_level` a través de `sx_slock`.
5. Añade algunas llamadas a `MYFIRST_DBG(sc, 1, ...)` en las rutas de lectura/escritura para registrar cuándo el buffer se vacía o se llena.
6. Compila y carga.
7. Ejecuta `mp_stress`. Confirma que no hay spam en el registro (debug_level tiene valor 0 por defecto).
8. Ejecuta `sysctl -w dev.myfirst.0.debug_level=2` y vuelve a ejecutar `mp_stress`. Ahora `dmesg` debería mostrar mensajes de depuración.
9. Restablece el nivel a 0.

**Verificación.** Los mensajes de depuración aparecen y desaparecen conforme cambia el sysctl. Sin avisos de `WITNESS` durante el cambio.

**Objetivo adicional.** Añade el sysctl `soft_byte_limit`. Ajústalo a 1024 y ejecuta un escritor que produzca ráfagas de 4096 bytes; confirma que el escritor recibe `EAGAIN` antes de lo esperado.

### Laboratorio 12.4: Inspección de locks adquiridos con DDB

**Objetivo.** Usar el depurador del kernel para inspeccionar una prueba bloqueada.

**Pasos.**

1. Asegúrate de que el kernel de depuración tiene `options DDB` y una forma configurada de entrar en DDB (típicamente `Ctrl-Alt-Esc` en una consola serie, o la tecla `Break`).
2. Carga el driver del Laboratorio 12.3.
3. Inicia un `cat /dev/myfirst` en un terminal. Se bloquea (no hay productor).
4. Desde la consola (o mediante `sysctl debug.kdb.enter=1`), entra en DDB.
5. Ejecuta `show all locks`. Anota los threads que tienen locks adquiridos.
6. Ejecuta `ps`. Localiza el proceso `cat` y el canal de espera `myfirst data`.
7. Ejecuta `bt <pid> <tid>` para el thread de cat. Confirma que el backtrace termina en `_cv_wait_sig`.
8. Ejecuta `continue` para salir de DDB.
9. Envía `SIGINT` al cat (Ctrl-C).

**Verificación.** El cat devuelve `EINTR`. Sin panics. Tienes una transcripción de la sesión de DDB.

**Objetivo adicional.** Repite con `mp_stress` ejecutándose en paralelo. Compara la salida de `show all locks`: más locks, más actividad, pero la misma estructura.

### Laboratorio 12.5: Detección de una inversión deliberada del orden de lock

**Objetivo.** Introducir un LOR deliberado y observar cómo `WITNESS` lo detecta.

**Pasos.**

1. Copia el Laboratorio 12.3 en un directorio de trabajo `stage-lab12-5/`. No modifiques el Laboratorio 12.3 en su sitio.
2. Añade una ruta que viole el orden de lock. Por ejemplo, en un pequeño manejador sysctl experimental:

   ```c
   /* WRONG: sx first, then mtx, reversing the canonical order. */
   sx_xlock(&sc->cfg_sx);
   MYFIRST_LOCK(sc);
   /* trivial work */
   MYFIRST_UNLOCK(sc);
   sx_xunlock(&sc->cfg_sx);
   ```

3. Compila y carga en el kernel con `WITNESS`.
4. Ejecuta `mp_stress` (que ejercita el orden canónico a través de la ruta de datos) y activa el nuevo sysctl simultáneamente.
5. Observa `dmesg` en busca del aviso `lock order reversal`.
6. Registra el texto del aviso. Anota los números de línea.
7. Elimina el directorio de trabajo; no hagas commit del error.

**Verificación.** `dmesg` muestra un aviso `lock order reversal` que nombra ambos locks y ambas ubicaciones en el código fuente.

**Objetivo adicional.** Determina, solo a partir de la salida de `WITNESS`, dónde se estableció por primera vez el orden canónico. Abre el código fuente en esa línea y confírmalo.

### Laboratorio 12.6: Estrés compuesto de larga duración

**Objetivo.** Ejecutar la carga de estrés compuesta de la Sección 7 durante 30 minutos y verificar que el resultado es limpio.

**Pasos.**

1. Arranca el kernel de depuración.
2. Compila y carga `examples/part-03/ch12-synchronization-mechanisms/stage4-final/`. Este es el driver final integrado (canales cv + lecturas acotadas + configuración protegida con sx + el sysctl de reset). Todos los sysctls de la Sección 7 que toca el script compuesto están presentes aquí.
3. Compila los programas de prueba en espacio de usuario.
4. Guarda el script de estrés compuesto de la Sección 7 como `composite_stress.sh`.
5. Envuélvelo en un bucle de 30 minutos:
   ```sh
   for i in $(seq 1 30); do
     ./composite_stress.sh
     echo "iteration $i done"
   done
   ```
6. Monitoriza `dmesg` periódicamente.
7. Al terminar, comprueba:
   - `dmesg | grep -ci witness` devuelve 0.
   - Todas las iteraciones del bucle se completaron.
   - `vmstat -m | grep cbuf` muestra la asignación estática esperada (sin crecimiento).

**Verificación.** Todos los criterios se cumplen. El driver supera 30 minutos de estrés compuesto en un kernel de depuración sin ninguna advertencia, panic ni crecimiento de memoria.

**Objetivo adicional.** Ejecuta el mismo bucle durante 24 horas en una máquina de pruebas dedicada. Los errores que aparecen a esta escala son los que más cuestan en producción.

### Lab 12.7: Verifica que el patrón snapshot-and-apply se mantiene bajo contención

**Objetivo.** Demostrar que el patrón snapshot-and-apply en `myfirst_write` gestiona correctamente las actualizaciones concurrentes al límite de bytes suave.

**Pasos.**

1. Establece el límite de bytes suave en un valor pequeño: `sysctl -w dev.myfirst.0.soft_byte_limit=512`.
2. Inicia `mp_stress` con dos escritores y dos lectores.
3. Desde un tercer terminal, alterna el límite repetidamente: `while sleep 0.1; do sysctl -w dev.myfirst.0.soft_byte_limit=$RANDOM; done`.
4. Observa la salida del escritor. Algunas escrituras tendrán éxito; otras devolverán `EAGAIN` (el límite estaba por debajo del valor actual de cb_used en el momento de la comprobación).
5. Observa `dmesg` en busca de avisos de `WITNESS`.

**Verificación.** No aparecen avisos de `WITNESS`. Los conteos de bytes en `mp_stress` son ligeramente inferiores a lo habitual (porque algunas escrituras fueron rechazadas), pero el total escrito es aproximadamente igual al total leído.

**Objetivo adicional.** Modifica `myfirst_write` para violar la regla de orden de locks adquiriendo el sx de cfg mientras se mantiene el mutex de datos. Recarga el módulo y ejecuta el mismo test. `WITNESS` debería dispararse en la primera ejecución que ejercite ambos caminos simultáneamente. Revierte el cambio.

### Lab 12.8: Perfila con lockstat

**Objetivo.** Usar `lockstat(1)` para caracterizar los locks disputados bajo carga.

**Pasos.**

1. Carga el driver del Lab 12.3 en el kernel de depuración.
2. Inicia `mp_stress` en un terminal.
3. Desde otro, ejecuta `lockstat -P sleep 30 > /tmp/lockstat.out`.
4. Abre el archivo de salida. Busca las entradas de `myfirst0` (mtx) y `myfirst cfg` (sx).
5. Anota: el tiempo máximo de retención, el tiempo medio de retención, el tiempo máximo de espera, el tiempo medio de espera y el número de adquisiciones.
6. Repite la prueba con `config_writer` en ejecución. Compara los valores de `myfirst cfg`.

**Verificación.** Los valores coinciden con el perfil esperado. El mutex muestra millones de adquisiciones con tiempos de retención breves. El sx muestra decenas de miles de adquisiciones, mayoritariamente compartidas, con tiempos de retención muy cortos.

**Objetivo adicional.** Modifica el driver para extender artificialmente una sección crítica (por ejemplo, añade un `pause(9)` de 10 ms dentro del mutex). Vuelve a ejecutar `lockstat`. Observa el pico de contención. Revierte la modificación.



## Ejercicios de desafío

Los desafíos amplían el Capítulo 12 más allá de los laboratorios básicos. Cada uno es opcional; cada uno está diseñado para profundizar tu comprensión.

### Desafío 1: Usa sx_downgrade para una actualización de configuración

El handler `myfirst_sysctl_debug_level` actualmente libera el lock compartido y vuelve a adquirir el lock exclusivo. Una alternativa es adquirir el lock compartido, intentar `sx_try_upgrade` y llamar a `sx_downgrade` después de la modificación. Implementa esta variante. Compara el comportamiento bajo contención. ¿En qué situaciones gana cada patrón?

### Desafío 2: Implementa una operación de drenado con cv_broadcast

Añade un ioctl o sysctl que "drene" el cbuf: bloquea hasta que `cb_used == 0` y luego retorna. La implementación debe usar `cv_wait_sig(&sc->room_cv, ...)` en un bucle sobre la condición `cb_used > 0`. Verifica que `cv_broadcast(&sc->room_cv)` tras el drenado despierta a todos los waiters, no solo a uno.

### Desafío 3: Un script de dtrace para la latencia de cv_wait

Escribe un script de `dtrace` que produzca un histograma del tiempo que los threads pasan dentro de `cv_wait_sig` en cada uno de `data_cv` y `room_cv`. Ejecútalo durante `mp_stress`. ¿Qué aspecto tiene la distribución? ¿Dónde está la cola larga?

### Desafío 4: Reemplaza cv por canales anónimos

Reimplementa las condiciones de datos y de espacio usando `mtx_sleep` y `wakeup` sobre canales anónimos (una regresión al diseño del Capítulo 11). Ejecuta los tests. El driver debería seguir funcionando, pero la salida de `procstat -kk` y las consultas de `dtrace` serán menos informativas. Describe la diferencia en legibilidad.

### Desafío 5: Añade read_timeout_ms por descriptor

El sysctl `read_timeout_ms` es por dispositivo. Añade un timeout por descriptor mediante un `ioctl(2)`: `MYFIRST_SET_READ_TIMEOUT(int ms)` sobre un descriptor de archivo establece el timeout de ese descriptor. El código del driver se vuelve más interesante porque el timeout ahora reside en `struct myfirst_fh` en lugar de en `struct myfirst_softc`. Ten en cuenta que el estado por fh no se comparte con otros descriptores (no se necesita lock para el campo en sí), pero la elección del timeout sigue afectando al helper de espera.

### Desafío 6: Usa rw(9) en lugar de sx(9)

Reemplaza `sx_init` por `rw_init`, `sx_xlock` por `rw_wlock`, y así sucesivamente. Ejecuta los tests. ¿Qué falla? (Pista: la ruta de cfg puede incluir una operación durmiente; rw no es un lock sleepable.) ¿Qué aspecto tiene el fallo? ¿Cuándo sería `rw(9)` la elección correcta?

### Desafío 7: Implementa un drenado multi-CV

El driver tiene dos cvs. Supón que detach debe considerarse completo solo cuando ambos cvs tengan cero waiters. Implementa una comprobación en detach que itere hasta que `data_cv.cv_waiters == 0` y `room_cv.cv_waiters == 0`, durmiendo brevemente entre comprobaciones. (Nota: acceder a `cv_waiters` directamente desde fuera de la API de cv no es portable; esto es un ejercicio para comprender el estado interno. El código de producción real debería usar un mecanismo diferente.)

### Desafío 8: Visualización del orden de locks

Usa `dtrace` o `lockstat` para producir un grafo de adquisiciones de locks durante `mp_stress`. Los nodos son locks; las aristas representan "el poseedor de A adquirió B mientras aún mantenía A". Compara el grafo con el orden de locks de tu `LOCKING.md`. ¿Hay adquisiciones que no habías anticipado?

### Desafío 9: Comparación de canales de espera

Construye dos versiones del driver: una usando cv (la opción predeterminada del Capítulo 12) y otra usando `mtx_sleep`/`wakeup` heredados sobre canales anónimos (la opción predeterminada del Capítulo 11). Ejecuta cargas de trabajo idénticas en ambas. Mide: el throughput máximo, la latencia en el percentil 99, la limpieza de `WITNESS` y la legibilidad del código fuente. Escribe un informe de una página.

### Desafío 10: Limita las escrituras de configuración

El driver del Capítulo 12 permite escrituras de configuración en cualquier momento. Añade un sysctl `cfg_write_cooldown_ms` que limite la frecuencia con la que puede producirse un cambio de configuración (por ejemplo, como máximo una escritura cada 100 ms). Impleméntalo con un campo de marca de tiempo en la estructura cfg y una comprobación en cada handler sysctl de cfg. Decide qué hacer cuando se viola el cooldown: devolver `EBUSY`, encolar el cambio o fusionarlo silenciosamente. Documenta la decisión.



## Resolución de problemas

Esta referencia cataloga los bugs que con mayor probabilidad encontrarás al trabajar con el Capítulo 12.

### Síntoma: el lector se queda bloqueado indefinidamente aunque se estén escribiendo datos

**Causa.** Un wakeup perdido. El escritor añadió bytes pero no señalizó `data_cv`, o la señal apuntaba a un cv incorrecto.

**Solución.** Busca en el código fuente cada lugar que añada bytes; asegúrate de que se llama a `cv_signal(&sc->data_cv)`. Confirma que el cv es aquel en el que están bloqueados los waiters.

### Síntoma: WITNESS advierte de "inversión del orden de locks" entre sc->mtx y sc->cfg_sx

**Causa.** Algún camino del código tomó los locks en orden incorrecto. El orden canónico es primero mtx, luego sx.

**Solución.** Localiza el camino problemático (el aviso indica las líneas). O bien reordena las adquisiciones para que coincidan con el orden canónico, o bien refactoriza el camino para evitar mantener ambos locks simultáneamente (snapshot-and-apply).

### Síntoma: cv_timedwait_sig devuelve EWOULDBLOCK inmediatamente

**Causa.** El timeout en ticks era cero o negativo. Lo más probable es que la conversión de milisegundos a ticks se redondeara hacia abajo hasta cero.

**Solución.** Usa la fórmula `(timo_ms * hz + 999) / 1000` para redondear hacia arriba hasta al menos un tick. Verifica que `hz` tiene el valor esperado (normalmente 1000 en FreeBSD 14.3).

### Síntoma: detach se bloquea

**Causa.** Un thread está durmiendo en un cv que no ha recibido broadcast, o bien detach espera a que `active_fhs > 0` baje a cero pero hay un descriptor abierto.

**Solución.** Confirma que detach hace broadcast de ambos cvs antes de la comprobación de active_fhs. Usa `fstat | grep myfirst` desde un terminal separado para encontrar cualquier proceso que tenga el dispositivo abierto y termínalo.

### Síntoma: la escritura de sysctl se bloquea

**Causa.** El handler sysctl está esperando un lock que mantiene un thread ocupado en una operación bloqueante. Lo más habitual es que el sx de cfg esté en modo exclusivo retenido por un `sysctl_handle_string` lento.

**Solución.** Verifica que el handler sysctl sigue el patrón snapshot-and-apply: adquirir compartido, leer, liberar; luego `sysctl_handle_*` fuera del lock; luego lock exclusivo para confirmar. Mantener el lock durante `sysctl_handle_*` es el fallo.

### Síntoma: sx_destroy entra en pánico con "lock still held"

**Causa.** Se llamó a `sx_destroy` mientras otro thread aún mantenía el lock o estaba esperando por él.

**Solución.** Confirma que detach se niega a continuar mientras `active_fhs > 0`. Confirma que ningún thread del kernel ni callout usa el sx de cfg después de que detach empiece.

### Síntoma: cv_signal o cv_broadcast no despierta nada visible

**Causa.** Nadie estaba esperando en el cv en el momento de la señal. Tanto `cv_signal` como `cv_broadcast` son operaciones sin efecto cuando la cola de espera está vacía, y una sonda de `dtrace` en el lado del wakeup no ve actividad posterior.

**Solución.** No se necesita ninguna; el wakeup vacío es correcto e inofensivo. Si esperabas un waiter y no había ninguno, el fallo está antes: o bien el waiter nunca llegó a `cv_wait_sig`, o bien el que señaliza apunta al cv incorrecto. Confirma mediante `dtrace` que la señal se dispara en el cv que pretendes, y usa `procstat -kk` contra el waiter para confirmar dónde está durmiendo.

### Síntoma: read_timeout_ms configurado a 100 produce una latencia de 200 ms

**Causa.** El valor `hz` del kernel es inferior al esperado. El redondeo `+999` significa que un timeout de 100 ms con `hz=100` se convierte en 10 ticks (100 ms), pero con `hz=10` se convierte en 1 tick (100 ms). El redondeo difiere.

**Solución.** Confirma `hz` con `sysctl kern.clockrate`. Para timeouts más precisos, usa `cv_timedwait_sig_sbt` directamente con `SBT_1MS * timo_ms` para evitar el redondeo de ticks.

### Síntoma: un orden de locks deliberadamente incorrecto no produce un aviso de WITNESS

**Causa.** O bien el camino con el fallo no es ejercitado por el test, o bien `WITNESS` no está habilitado en el kernel en ejecución.

**Solución.** Confirma que `sysctl debug.witness.watch` devuelve un valor distinto de cero. Confirma que el camino problemático se ejecuta (añade un `device_printf` para verificarlo). Ejecuta el test bajo `mp_stress` para maximizar la probabilidad de que el fallo salga a la luz.

### Síntoma: lockstat muestra tiempos de espera enormes en el mutex de datos

**Causa.** El mutex se mantiene durante una operación larga. Los responsables habituales son: `uiomove` accidentalmente dentro de la sección crítica, o un `device_printf` de depuración que imprime una cadena larga mientras se mantiene el lock.

**Solución.** Audita las secciones críticas. Mueve las operaciones largas fuera de ellas. El mutex debería mantenerse durante decenas de nanosegundos, no microsegundos.

### Síntoma: mp_stress informa de discrepancia en el conteo de bytes tras los cambios del Capítulo 12

**Causa.** Se perdió un wakeup durante el refactor de cv. Un lector empezó a esperar después de que la señal de un escritor ya se hubiera entregado (no había waiter en el momento de la señal, señal perdida).

**Solución.** Verifica que los helpers de espera usan `while`, no `if`, alrededor de `cv_wait_sig`. Verifica que la señal ocurre después del cambio de estado, no antes.

### Síntoma: timeout_tester muestra una latencia superior al timeout configurado

**Causa.** Latencia del planificador. El kernel planificó el thread algunos milisegundos después de que el temporizador disparara. Esto es normal; espera algunos ms de jitter.

**Solución.** Ninguna para cargas de trabajo habituales. Para cargas de trabajo en tiempo real, eleva la prioridad del thread mediante `rtprio(2)`.

### Síntoma: kldunload informa de que está ocupado cuando no hay ningún descriptor abierto

**Causa.** Un taskqueue o thread en segundo plano sigue usando algún primitivo del driver. (No debería ocurrir en nuestro capítulo, pero conviene saberlo.)

**Solución.** Audita cualquier código que genere taskqueue, callout o kthread. El detach debe drenar o terminar todos ellos antes de declarar que es seguro descargar el módulo.

### Síntoma: cv_wait_sig despierta inmediatamente y devuelve 0

**Causa.** Una señal llegó mientras se estaba configurando la espera, o bien el cv fue señalizado por un thread que se ejecutó justo antes de que se emitiera la espera. En realidad no es un fallo; el bucle `while` debe gestionarlo.

**Solución.** Confirma que el `while (!condition)` circundante vuelve a comprobar. El bucle convierte el wakeup de apariencia espuria en una operación sin efecto: vuelve a comprobar, encuentra la condición falsa y duerme de nuevo.

### Síntoma: dos threads que esperan en el mismo cv se despiertan en un orden inesperado

**Causa.** `cv_signal` despierta a un único esperador elegido por la política de la sleep-queue (el de mayor prioridad, o FIFO entre los de igual prioridad). No los despierta en orden de llegada si sus prioridades son distintas.

**Solución.** Normalmente no es necesaria ninguna acción; la elección del kernel es correcta. Si necesitas un despertar en estricto orden de llegada, utiliza un diseño diferente (un `cv` por esperador, o una cola explícita).

### Síntoma: sx_xlock tarda segundos en adquirirse bajo una carga elevada de lectores

**Causa.** Hay muchos titulares en modo compartido, cada uno liberando lentamente porque el sx de `cfg` se adquiere y se libera en cada operación de I/O. El escritor sufre inanición a causa del goteo constante de lectores.

**Solución.** El kernel utiliza la opción `SX_LOCK_WRITE_SPINNER` para dar prioridad a los escritores una vez que comienzan a esperar; la inanición está acotada, pero puede seguir produciendo latencia visible. Si la latencia no es aceptable, rediseña el sistema para que las escrituras ocurran durante ventanas de inactividad o bajo un protocolo diferente.

### Síntoma: Las pruebas pasan en un kernel sin WITNESS pero fallan con WITNESS

**Causa.** Casi siempre se trata de un error real que `WITNESS` ha detectado. El más frecuente: un lock adquirido en una ruta de ejecución que viola el orden global, pero el deadlock todavía no se ha manifestado porque la carga de trabajo en conflicto no se ha producido.

**Solución.** Lee el aviso de `WITNESS` con atención. El texto del aviso incluye la ubicación en el código fuente de cada violación. Corrige la violación; la prueba debería pasar entonces en ambos kernels.

### Síntoma: Las macros de locking se expanden a nada en el kernel sin depuración

**Causa.** Esto es intencionado. `mtx_assert`, `sx_assert`, `KASSERT` y `MYFIRST_ASSERT(sc)` (que se expande a `mtx_assert`) se compilan sin producir código alguno si no está habilitado `INVARIANTS`. Las aserciones no tienen coste en producción y son informativas durante el desarrollo.

**Solución.** No se necesita ninguna. Confirma que tu kernel de prueba tiene `INVARIANTS` habilitado y las aserciones se activarán cuando se viole la condición.

### Síntoma: El handler del sysctl bloquea el sistema entero

**Causa.** Un handler de sysctl que mantiene un lock durante una operación lenta puede serializar en la práctica todas las demás operaciones que necesiten el mismo lock. Si el lock es el mutex principal del dispositivo, cada operación de I/O queda bloqueada hasta que el sysctl retorna.

**Solución.** Los handlers de sysctl deben seguir la misma disciplina que los handlers de I/O: mantener el lock el mínimo tiempo posible y liberarlo antes de cualquier operación que pueda ser lenta. El patrón de instantánea y aplicación (snapshot-and-apply) funciona igual de bien aquí.

### Síntoma: Un lector recibe EAGAIN incluso con read_timeout_ms=0

**Causa.** La lectura devolvió `EAGAIN` por culpa de `O_NONBLOCK` (el descriptor de archivo se abrió en modo no bloqueante, o `fcntl(2)` estableció `O_NONBLOCK` sobre él). La comprobación `IO_NDELAY` del driver devuelve `EAGAIN` independientemente del sysctl de tiempo de espera.

**Solución.** Confirma que el descriptor es bloqueante: `fcntl(fd, F_GETFL)` debe devolver un valor sin el bit `O_NONBLOCK` activado. Si el modo no bloqueante es lo que se desea, `EAGAIN` es la respuesta correcta.

### Síntoma: kldunload se queda colgado brevemente tras una prueba correcta

**Causa.** La ruta de detach espera a que los handlers en vuelo retornen. Cada thread que dormía sobre un cv debe despertar (por efecto del broadcast), readquirir el mutex, comprobar `!is_attached`, retornar y salir del kernel. Esto tarda unos pocos milisegundos con varios threads en espera.

**Solución.** Normalmente no se necesita ninguna; un retraso de pocos milisegundos es normal. Si el retraso es mayor, comprueba que cada thread en espera tiene la verificación de `is_attached` tras despertar.

### Síntoma: Dos instancias independientes del driver reportan avisos de WITNESS sobre el mismo nombre de lock

**Causa.** Ambas instancias inicializan sus locks con el mismo nombre (por ejemplo, `myfirst0` para las dos). `WITNESS` trata los locks con el mismo nombre como el mismo lock lógico y puede advertir sobre adquisiciones duplicadas o problemas de orden inventado entre instancias.

**Solución.** Inicializa el lock de cada instancia con un nombre único que incluya el número de unidad, por ejemplo mediante `device_get_nameunit(dev)`, que produce `myfirst0`, `myfirst1`, etc. El capítulo ya hace esto para el mutex del dispositivo; aplica el mismo criterio para los cvs y el sx.

### Síntoma: Un cv con muchos threads en espera tarda mucho en hacer el broadcast

**Causa.** `cv_broadcast` recorre la cola de espera y marca cada thread como ejecutable. El recorrido es O(n) respecto al número de threads en espera. Con cientos de threads, esto se convierte en un coste medible.

**Solución.** El broadcast en sí rara vez es un cuello de botella en cargas de trabajo normales; lo que provoca la pausa visible es la posterior contención en tropel (thundering-herd) cuando todos los threads despertados intentan adquirir el interlock. Si tu driver tiene habitualmente cientos de threads en espera sobre el mismo cv, reconsidera el diseño; cvs por waiter o un enfoque basado en cola pueden escalar mejor.



## En resumen

El capítulo 12 tomó el driver que construiste en el capítulo 11 y le dio un vocabulario de sincronización más rico. El mutex único del capítulo 11 sigue ahí, haciendo el mismo trabajo con las mismas reglas. A su alrededor se encuentran ahora dos condition variables con nombre que reemplazan el canal de wakeup anónimo, un sx lock que protege un pequeño pero real subsistema de configuración, y una capacidad de espera acotada que permite a la ruta de lectura retornar con prontitud cuando el usuario lo espera. El orden de los locks entre las dos clases queda documentado, impuesto por `WITNESS` y verificado por un kit de estrés que ejecuta la ruta de datos y la ruta de configuración de forma concurrente.

Aprendimos a pensar en la sincronización como un vocabulario, no solo como un mecanismo. Una condition variable dice *estoy esperando un cambio concreto; avísame cuando ocurra*. Un shared lock dice *estoy leyendo; no dejes entrar a un escritor*. Una espera temporizada dice *y abandona si tarda demasiado*. Cada primitiva es una forma distinta de acuerdo entre threads, y usar la forma adecuada para cada acuerdo produce código que refleja el diseño en lugar de luchar contra él.

También aprendimos a depurar la sincronización con cuidado. Las seis formas de fallo (wakeup perdido, wakeup espurio, inversión del orden de locks, destrucción prematura, dormir con un lock no durmiente, condición de carrera entre detach y una operación activa) cubren casi todos los errores que encontrarás en la práctica. `WITNESS` atrapa los que el kernel puede detectar en tiempo de ejecución; el depurador del kernel te permite inspeccionar un sistema colgado; `lockstat(1)` y `dtrace(1)` te proporcionan visibilidad sin modificar el código fuente.

Terminamos con una pasada de refactorización. El driver tiene ahora un orden de locks documentado, un `LOCKING.md` limpio, una cadena de versión actualizada, un registro de cambios al día y una prueba de regresión que verifica cada primitiva en cada carga de trabajo contemplada. Esa infraestructura escala: cuando el capítulo 13 añada temporizadores y el capítulo 14 añada interrupciones, el patrón de documentación absorberá las nuevas primitivas sin volverse frágil.

### Lo que deberías ser capaz de hacer ahora

Una breve lista de comprobación de las capacidades que deberías tener antes de pasar al capítulo 13:

- Observar cualquier estado compartido en un driver y elegir la primitiva adecuada (atomic, mutex, sx, rw, cv) siguiendo el árbol de decisión.
- Reemplazar cualquier canal de wakeup anónimo en cualquier driver por un cv con nombre y explicar por qué el cambio es una mejora.
- Añadir una primitiva de bloqueo acotado a cualquier ruta de espera y explicar cuándo usar `EAGAIN`, `EINTR`, `ERESTART` o `EWOULDBLOCK`.
- Diseñar un subsistema de múltiples lectores y un único escritor con el orden de locks documentado.
- Leer un aviso de `WITNESS` e identificar el par de locks problemático únicamente a partir de la ubicación en el código fuente.
- Diagnosticar un sistema colgado en DDB usando `show all locks` y `show sleepchain`.
- Ejecutar una carga de trabajo de estrés compuesta y medir la contención de locks con `lockstat(1)`.
- Escribir un documento `LOCKING.md` que otro desarrollador pueda usar como referencia autoritativa.

Si alguno de estos puntos te resulta incierto, los laboratorios del capítulo 12 son el lugar donde adquirir la soltura necesaria. Ninguno requiere más de un par de horas; juntos cubren cada primitiva y cada patrón que el capítulo ha presentado.

### Tres recordatorios finales

El primero es *ejecutar el estrés compuesto antes de hacer el commit*. El kit compuesto detecta los errores entre primitivas que se escapan a las pruebas de un solo eje. Treinta minutos en un kernel de depuración es una pequeña inversión para la confianza que genera.

El segundo es *mantener honesto el orden de los locks*. Cada nuevo lock que introduces plantea una nueva pregunta: ¿qué lugar ocupa en el orden? Responde la pregunta de forma explícita en `LOCKING.md` antes de escribir el código. El coste de equivocarse en la respuesta crece con el tamaño del driver; el coste de escribirla desde el principio es de un minuto.

El tercero es *confiar en las primitivas y usar la adecuada*. Los mutex, cv, sx y rw locks del kernel son el resultado de décadas de ingeniería. La tentación de crear tu propia coordinación con flags y flags atómicos es real y casi siempre errónea. Elige la primitiva que nombra lo que intentas expresar. El código resultará más corto, más claro y demostrablemente más correcto.



## Referencia: La progresión por etapas del driver

El capítulo 12 hace evolucionar el driver en cuatro etapas diferenciadas, cada una con su propio directorio bajo `examples/part-03/ch12-synchronization-mechanisms/`. La progresión refleja la narrativa del capítulo y permite al lector construir el driver una primitiva a la vez, observando qué aporta cada incorporación.

### Etapa 1: cv-channels

Reemplaza el canal de wakeup anónimo `&sc->cb` por dos condition variables con nombre (`data_cv`, `room_cv`). Los helpers de espera usan `cv_wait_sig` en lugar de `mtx_sleep`. Los señalizadores usan `cv_signal` (o `cv_broadcast` en detach) sobre el cv que corresponde al cambio de estado.

Qué cambia: el mecanismo de sleep/wake. El driver se comporta de forma idéntica desde el espacio de usuario.

Qué puedes verificar: `procstat -kk` muestra el nombre del cv (`myfirst data` o `myfirst room`) en lugar del wmesg (`myfrd`). `dtrace` puede engancharse a cvs concretos. El throughput es ligeramente mayor porque la señalización por evento evita despertar threads que esperan por otra condición.

### Etapa 2: bounded-read

Añade un sysctl `read_timeout_ms` que acota las lecturas bloqueantes mediante `cv_timedwait_sig`. También es posible un `write_timeout_ms` simétrico.

Qué cambia: la ruta de lectura puede devolver ahora `EAGAIN` tras un tiempo de espera configurable. El valor por defecto de cero preserva el comportamiento de espera indefinida de la etapa 1.

Qué puedes verificar: `timeout_tester` reporta `EAGAIN` tras aproximadamente el tiempo de espera configurado. Establecer el tiempo de espera en cero restaura las esperas indefinidas. Ctrl-C sigue funcionando en ambos casos.

### Etapa 3: sx-config

Añade una estructura `cfg` al softc, protegida por un `sx_lock` (`cfg_sx`). Tres campos de configuración (`debug_level`, `soft_byte_limit`, `nickname`) quedan expuestos como sysctls. La ruta de datos consulta `debug_level` para la emisión de mensajes de log y `soft_byte_limit` para rechazar escrituras.

Qué cambia: el driver gana una interfaz de configuración. La macro `MYFIRST_DBG` consulta el nivel de depuración actual. Las escrituras que superarían el límite flexible devuelven `EAGAIN`.

Qué puedes verificar: `sysctl -w dev.myfirst.0.debug_level=2` produce mensajes de depuración visibles. Establecer `soft_byte_limit` hace que las escrituras comiencen a fallar cuando el buffer alcanza ese límite. `WITNESS` reporta el orden de los locks (mtx primero, sx después) y no emite avisos bajo estrés.

### Etapa 4: final

La versión combinada con las tres primitivas, más una actualización del `LOCKING.md`, la subida de versión a `0.6-sync` y el nuevo `myfirst_sysctl_reset` que ejercita ambos locks conjuntamente.

Qué cambia: la integración. No hay nuevas primitivas.

Qué puedes verificar: la suite de regresión pasa; la carga de trabajo de estrés compuesta se ejecuta sin problemas durante al menos 30 minutos; `clang --analyze` no emite avisos.

Esta progresión de cuatro etapas es el driver canónico del capítulo 12. Los ejemplos acompañantes reflejan las etapas con exactitud, de modo que el lector puede compilar y cargar cualquiera de ellas.



## Referencia: Migrar de mtx_sleep a cv

Si trabajas en un driver existente que usa el mecanismo de canal heredado `mtx_sleep`/`wakeup`, la migración a `cv(9)` es mecánica. Una receta breve.

Una nota antes de empezar: el mecanismo heredado no está obsoleto y sigue siendo de uso amplio en el árbol de FreeBSD. Muchos drivers conservarán `mtx_sleep` indefinidamente y eso es perfectamente correcto. La migración vale la pena cuando se tienen múltiples condiciones distintas que comparten un mismo canal (el caso de thundering-herd), o cuando se desea la visibilidad que `procstat` y `dtrace` proporcionan con cvs con nombre. Para un driver con una sola condición y un solo canal, la migración es puramente cosmética; hazla por legibilidad si quieres, o sáltala si no.

### Paso 1: Identificar cada canal de espera lógico

Lee el código fuente. Encuentra cada llamada a `mtx_sleep`. Para cada una, pregúntate: ¿cuál es la condición por la que este thread está esperando?

En el driver del Capítulo 11, había dos condiciones lógicas que usaban `&sc->cb`:

- `myfirst_wait_data`: esperando a que `cbuf_used > 0`.
- `myfirst_wait_room`: esperando a que `cbuf_free > 0`.

Dos condiciones; un canal. La migración asigna a cada una su propia cv.

### Paso 2: Añadir campos cv al softc

Para cada condición lógica, añade un campo `struct cv`. Elige un nombre descriptivo:

```c
struct cv  data_cv;
struct cv  room_cv;
```

Inicialízalo en attach (`cv_init`) y destrúyelo en detach (`cv_destroy`).

### Paso 3: Sustituir mtx_sleep por cv_wait_sig

Para cada llamada a `mtx_sleep`, sustitúyela por `cv_wait_sig` (o `cv_timedwait_sig`):

```c
/* Before: */
error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", 0);

/* After: */
error = cv_wait_sig(&sc->data_cv, &sc->mtx);
```

El argumento wmesg desaparece (la cadena de descripción del cv ocupa su lugar). El `PCATCH` es implícito en el sufijo `_sig`. El argumento interlock es el mismo.

### Paso 4: Sustituir wakeup por cv_signal o cv_broadcast

Para cada llamada a `wakeup(&channel)`, decide si debe despertar a uno o a todos los waiters. Sustitúyela por `cv_signal` o `cv_broadcast`:

```c
/* Before: */
wakeup(&sc->cb);  /* both readers and writers were on this channel */

/* After: */
if (write_succeeded)
        cv_signal(&sc->data_cv);  /* only readers care about new data */
if (read_succeeded)
        cv_signal(&sc->room_cv);  /* only writers care about new room */
```

Este es también el momento de añadir la correspondencia por evento que la API cv fomenta: envía la señal solo cuando el estado haya cambiado realmente.

### Paso 5: Actualizar la ruta de detach

El detach despertaba el canal antes de destruir el estado:

```c
/* Before: */
sc->is_attached = 0;
wakeup(&sc->cb);

/* After: */
sc->is_attached = 0;
cv_broadcast(&sc->data_cv);
cv_broadcast(&sc->room_cv);
/* later, after all waiters have exited: */
cv_destroy(&sc->data_cv);
cv_destroy(&sc->room_cv);
```

`cv_broadcast` garantiza que todos los waiters despiertan; la comprobación de `is_attached` tras la espera devuelve `ENXIO` para cada uno de ellos.

### Paso 6: Actualizar LOCKING.md

Documenta cada nuevo cv: su nombre, su condición, su interlock, sus emisores de señal y sus waiters. El `LOCKING.md` del driver del Capítulo 12 sirve como plantilla.

### Paso 7: Volver a ejecutar el kit de estrés

La migración no debe cambiar el comportamiento observable, solo el mecanismo interno. Ejecuta las pruebas existentes; deberían pasar. Ejecútalas bajo `WITNESS`; no deberían aparecer nuevas advertencias.

La migración en nuestro capítulo implica unos centenares de líneas de código fuente; la receta anterior escala para drivers de cualquier tamaño. La ventaja es que ahora cada espera se documenta por sí misma y cada señal va dirigida a su destino.

### Cuándo merece la pena la migración y cuándo no

La migración cuesta unas horas de esfuerzo de refactorización, una nueva ejecución cuidadosa de la suite de pruebas y una actualización de la documentación. Las ventajas son:

- Cada espera adquiere un nombre visible en `procstat`, `dtrace` y en el código fuente.
- Los wakeups pasan a ser por condición; el problema del thundering herd se reduce.
- Los desajustes de canal de wakeup son más fáciles de detectar en el código fuente.

Para un driver pequeño con una única condición de espera, los costes y los beneficios se compensan aproximadamente; el mecanismo heredado es perfectamente válido. Para un driver con dos o más condiciones distintas, la migración casi siempre merece la pena. Para un driver mantenido por varios desarrolladores, la ganancia en legibilidad es considerable.



## Referencia: Lista de verificación previa a producción

Una auditoría breve que hay que realizar antes de promover un driver con uso intensivo de sincronización de desarrollo a producción. Cada elemento es una pregunta que debe poder responderse con seguridad.

### Inventario de locks

- [ ] ¿He listado todos los locks que posee el driver en `LOCKING.md`?
- [ ] Para cada lock, ¿he indicado qué protege?
- [ ] Para cada lock, ¿he indicado los contextos en los que puede adquirirse?
- [ ] Para cada lock, ¿he documentado su ciclo de vida (dónde se crea, dónde se destruye)?

### Orden de adquisición de locks

- [ ] ¿Está documentado el orden global de adquisición de locks en `LOCKING.md`?
- [ ] ¿Sigue cada ruta de código que mantiene dos locks el orden global?
- [ ] ¿He ejecutado `WITNESS` durante al menos 30 minutos bajo carga y no he observado inversiones de orden?
- [ ] Si el driver tiene más de una instancia, ¿he confirmado que el orden intra-instancia es coherente con el orden inter-instancia?

### Inventario de cvs

- [ ] ¿He listado todos los cvs que posee el driver?
- [ ] Para cada cv, ¿he indicado la condición que representa?
- [ ] Para cada cv, ¿he indicado el mutex interlock?
- [ ] Para cada cv, ¿he confirmado que existe al menos un emisor de señal y al menos un waiter?
- [ ] Para cada cv, ¿he confirmado que se llama a `cv_broadcast` en detach antes de `cv_destroy`?

### Funciones auxiliares de espera

- [ ] ¿Está toda llamada a `cv_wait` (o su variante) dentro de un bucle `while (!condition)`?
- [ ] ¿Comprueba cada función auxiliar de espera `is_attached` tras la espera?
- [ ] ¿Devuelve cada función auxiliar de espera un error adecuado (`ENXIO`, `EINTR`, `EAGAIN`)?

### Puntos de señalización

- [ ] ¿Tiene cada cambio de estado que debe despertar waiters su correspondiente `cv_signal` o `cv_broadcast`?
- [ ] ¿Se usa `cv_signal` cuando solo necesita despertar un waiter y `cv_broadcast` cuando deben despertar todos?
- [ ] ¿Están los puntos de señalización protegidos por `if (state_changed)` para evitar señales vacías?

### Ruta de detach

- [ ] ¿Rechaza el detach continuar mientras `active_fhs > 0`?
- [ ] ¿Pone el detach a cero `is_attached` bajo el mutex del dispositivo?
- [ ] ¿Hace el detach broadcast en todos los cvs antes de destruirlos?
- [ ] ¿Se destruyen los primitivos en orden inverso de adquisición (el lock más interno se destruye primero)?

### Análisis estático

- [ ] ¿Se ha ejecutado `clang --analyze` y se han clasificado las nuevas advertencias?
- [ ] ¿Ha generado el build con `WARNS=6` alguna advertencia?
- [ ] ¿Se ha ejecutado la suite de regresión en un kernel con `WITNESS` activado y han pasado todas las pruebas?

### Documentación

- [ ] ¿Está `LOCKING.md` actualizado con el código?
- [ ] ¿Se ha incrementado la cadena de versión en el código fuente?
- [ ] ¿Está actualizado `CHANGELOG.md`?
- [ ] ¿Describe `README.md` las nuevas funcionalidades y sus sysctls?

Un driver que supera esta auditoría es un driver en el que puedes confiar bajo carga.



## Referencia: Higiene del canal de sleep

Tanto el mecanismo de canal heredado de `mtx_sleep`/`wakeup` como la API moderna `cv(9)` se apoyan en un *canal* que identifica qué waiters se ven afectados por un wakeup. El canal es una clave en la tabla hash de la cola de sleep del kernel. Los errores en torno a los canales son la fuente de varios bugs frecuentes.

Algunas reglas de higiene.

### Un canal por condición lógica

Si tu driver tiene dos condiciones distintas que bloquean (por ejemplo, «datos disponibles» y «espacio disponible»), usa dos canales distintos. Compartir un único canal obliga a que cada wakeup despierte a todos los waiters; algunos de ellos volverán a dormirse de inmediato porque su condición sigue siendo falsa. El coste en rendimiento es real; el coste en legibilidad también lo es.

En nuestro capítulo, esta regla se materializa en que `data_cv` y `room_cv` son cvs separados. El driver del Capítulo 11 usaba un canal anónimo compartido `&sc->cb` y pagaba el coste del thundering herd; la separación del Capítulo 12 es la solución.

### Los punteros de canal deben ser estables

Un canal es una dirección de memoria. El kernel no la interpreta; la usa como clave hash. La dirección no debe cambiar entre la espera y la señal. Esto ocurre de forma automática en la mayoría de los casos (la dirección de un campo del softc es estable durante toda la vida del softc), pero conviene tener cuidado con los buffers temporales, las estructuras asignadas en la pila o la memoria liberada.

Si observas una espera que se bloquea tras una ruta de código concreta, sospecha de un desajuste de puntero de canal. El emisor de la señal y el waiter deben estar usando la misma dirección.

### Los punteros de canal deben ser exclusivos para su propósito

Si la misma dirección se usa para dos propósitos distintos (por ejemplo, el canal de «datos disponibles» y un canal de «finalización»), los wakeups para un propósito pueden despertar involuntariamente a los waiters del otro. Usa un campo distinto del softc como canal para cada propósito, o utiliza un cv (que tiene nombre y es un objeto independiente).

### Los emisores de wakeup deben mantener el interlock cuando el estado puede cambiar

Aunque `wakeup` y `cv_signal` no tienen estrictamente que llamarse bajo el interlock, hacerlo cierra una ventana de condición de carrera en la que un waiter comprueba la condición (falsa), el estado cambia, el wakeup se activa (no hay waiters en la cola) y el waiter encola y duerme para siempre. Mantener el interlock mientras se envía la señal es la opción segura por defecto; relájalo solo cuando puedas demostrar que el estado no puede revertir.

El diseño del Capítulo 12 envía la señal después de soltar el mutex, lo cual es seguro porque el propio contrato de encolado-bajo-interlock del cv cierra la condición de carrera para cv (pero no para `wakeup`). Para `wakeup`, envía la señal bajo el mutex.

### Una señal sin waiter no tiene coste

`cv_signal` y `wakeup` en un canal sin waiters no hacen nada. No hay penalización por una señal innecesaria; el coste es esencialmente el de adquirir y soltar el spinlock de la cola del cv. No evites las señales por miedo a perder rendimiento; envía la señal cuando el estado cambie, aunque a veces no despierte a nadie.

### Una espera sin emisor de señal es un bug

Una espera que nunca recibe señal es un cuelgue. Asegúrate de que cada espera tenga al menos un punto de señalización correspondiente y de que ese punto se alcance en cada ruta de código que produce el cambio de estado esperado.

Este es el bug más frecuente de cv. La lista de verificación de la auditoría plantea la pregunta; la disciplina de hacerla durante la revisión de código detecta la mayoría de los casos.



## Referencia: Patrones comunes de cv

Colección de consulta rápida con los patrones de cv que usarás con mayor frecuencia.

### Esperar una condición

```c
mtx_lock(&mtx);
while (!condition)
        cv_wait_sig(&cv, &mtx);
/* condition is true; do work */
mtx_unlock(&mtx);
```

El bucle `while` es esencial. Los wakeups espurios están permitidos; las señales interrumpen la espera; ambos casos tienen el aspecto de un retorno de `cv_wait_sig`. Comprueba de nuevo la condición tras cada retorno.

### Señalizar a un waiter

```c
mtx_lock(&mtx);
/* change state */
mtx_unlock(&mtx);
cv_signal(&cv);
```

`cv_signal` después del unlock evita un cambio de contexto (el thread despertado no compite de inmediato por el mutex). Es aceptable cuando el cambio de estado es inequívoco y ninguna ruta concurrente puede revertirlo.

### Difundir un cambio de estado

```c
mtx_lock(&mtx);
state_changed_globally = true;
cv_broadcast(&cv);
mtx_unlock(&mtx);
```

Usa `cv_broadcast` cuando todos los waiters necesiten conocer el cambio. Las rutas de detach y los reinicios de configuración son ejemplos típicos.

### Esperar con timeout

```c
mtx_lock(&mtx);
while (!condition) {
        int ticks = (ms * hz + 999) / 1000;
        int err = cv_timedwait_sig(&cv, &mtx, ticks);
        if (err == EWOULDBLOCK) {
                mtx_unlock(&mtx);
                return (EAGAIN);
        }
        if (err != 0) {
                mtx_unlock(&mtx);
                return (err);
        }
}
/* do work */
mtx_unlock(&mtx);
```

Convierte los milisegundos a ticks, redondea hacia arriba y gestiona explícitamente los tres casos de retorno (timeout, señal, wakeup normal).

### Esperar con detección de detach

```c
while (!condition) {
        int err = cv_wait_sig(&cv, &mtx);
        if (err != 0)
                return (err);
        if (!sc->is_attached)
                return (ENXIO);
}
```

La comprobación de `is_attached` tras la espera garantiza que salimos limpiamente si el dispositivo fue desconectado mientras dormíamos. El `cv_broadcast` en la ruta de detach hace posible esto.

### Vaciar los waiters antes de destruir

```c
mtx_lock(&mtx);
sc->is_attached = 0;
cv_broadcast(&cv);
mtx_unlock(&mtx);
/* waiters wake, see !is_attached, return ENXIO, exit */
/* by the time active_fhs == 0, no waiters remain */
cv_destroy(&cv);
```

La combinación del broadcast y la nueva comprobación de `is_attached` garantiza que no queda ningún waiter en el cv en el momento de destruirlo.



## Referencia: Patrones comunes de sx

### Campo con acceso predominantemente de lectura

```c
sx_slock(&sx);
value = field;
sx_sunlock(&sx);
```

Económico en un sistema multinúcleo; los múltiples lectores no compiten entre sí.

### Actualización con validación

```c
sx_slock(&sx);
old = field;
sx_sunlock(&sx);

/* validate possibly-new value with sysctl_handle_*, etc. */

sx_xlock(&sx);
field = new;
sx_xunlock(&sx);
```

Dos ciclos de adquisición y liberación. El lock compartido para la lectura; el lock exclusivo para la escritura. Libera entre ambos para que la validación no mantenga ninguno de los dos locks.

### Captura y aplicación con dos locks

```c
sx_slock(&cfg_sx);
local = cfg.value;
sx_sunlock(&cfg_sx);

mtx_lock(&data_mtx);
/* use local without holding either lock together */
mtx_unlock(&data_mtx);
```

Evita mantener ambos locks de forma simultánea; relaja la restricción de orden de adquisición.

### Patrón de upgrade optimista

```c
sx_slock(&sx);
if (need_modify) {
        if (sx_try_upgrade(&sx)) {
                /* exclusive */
                modify();
                sx_downgrade(&sx);
        } else {
                /* drop, reacquire as exclusive, re-validate */
                sx_sunlock(&sx);
                sx_xlock(&sx);
                if (still_need_modify())
                        modify();
                sx_downgrade(&sx);
        }
}
sx_sunlock(&sx);
```

Upgrade optimista. La ruta de respaldo debe volver a validar, porque el estado cambió durante la ventana de liberación del lock.

### Aserción de posesión

```c
sx_assert(&sx, SA_SLOCKED);  /* shared */
sx_assert(&sx, SA_XLOCKED);  /* exclusive */
sx_assert(&sx, SA_LOCKED);   /* either */
```

Úsalo al principio de las funciones auxiliares que esperan un estado de lock concreto.



## Referencia: Tabla de decisión para primitivos de sincronización

Una tabla de consulta rápida.

| Si necesitas... | Usa |
|---|---|
| Actualizar una sola palabra de forma atómica | `atomic(9)` |
| Actualizar un contador por CPU de forma económica | `counter(9)` |
| Proteger estado compuesto en contexto de proceso | `mtx(9)` (`MTX_DEF`) |
| Proteger estado compuesto en contexto de interrupción | `mtx(9)` (`MTX_SPIN`) |
| Proteger estado de lectura predominante en contexto de proceso | `sx(9)` |
| Proteger estado de lectura predominante donde el sleep está prohibido | `rw(9)` |
| Esperar a que una condición específica se vuelva verdadera | `cv(9)` (combinado con `mtx(9)` o `sx(9)`) |
| Esperar hasta una fecha límite | `cv_timedwait_sig`, o `mtx_sleep` con `timo` > 0 |
| Esperar de forma que Ctrl-C pueda interrumpir | La variante `_sig` de cualquier primitivo de espera |
| Ejecutar código en un momento futuro concreto | `callout(9)` (Capítulo 13) |
| Delegar trabajo a un thread trabajador | `taskqueue(9)` (Capítulo 16) |
| Leer de forma concurrente sin ninguna sincronización | `epoch(9)` (capítulos posteriores) |

Si dos primitivos se adaptan igualmente, usa el más sencillo.



## Referencia: Lectura de kern_condvar.c y kern_sx.c

Dos archivos en `/usr/src/sys/kern/` merecen abrirse una vez que hayas usado las APIs cv y sx en tu driver.

`/usr/src/sys/kern/kern_condvar.c` es la implementación de cv. Las funciones que merece la pena ver:

- `cv_init`: inicialización. Trivial.
- `_cv_wait` y `_cv_wait_sig`: las primitivas de bloqueo principales. Cada una toma el spinlock de la cola del cv, incrementa el contador de waiters, libera el interlock, entrega el thread a la sleep queue, cede la CPU y, al regresar, vuelve a adquirir el interlock. La atomicidad de la operación «liberar el interlock y dormir» la proporciona la capa de sleep queue.
- `_cv_timedwait_sbt` y `_cv_timedwait_sig_sbt`: las variantes con tiempo de espera. La misma estructura, con un callout que despierta el thread si el timeout se activa primero.
- `cv_signal`: toma el spinlock de la cola del cv y señaliza a un waiter mediante `sleepq_signal`.
- `cv_broadcastpri`: señaliza a todos los waiters con la prioridad indicada.

El archivo completo tiene unas 400 líneas. Con una tarde de lectura es más que suficiente para entenderlo de principio a fin.

`/usr/src/sys/kern/kern_sx.c` es la implementación del sx. Más largo y denso, porque el lock admite tanto modos compartidos como exclusivos con propagación de prioridad completa. Las funciones que merece la pena revisar:

- `sx_init_flags`: inicialización. Establece el estado inicial y se registra en `WITNESS`.
- `_sx_xlock_hard` y `_sx_xunlock_hard`: los caminos lentos para las operaciones exclusivas. Los caminos rápidos se definen inline en `sx.h`.
- `_sx_slock_int` y `_sx_sunlock_int`: las operaciones en modo compartido. El contador compartido se incrementa mediante comparación e intercambio atómico (compare-and-swap); si el lock está tomado en modo exclusivo, el thread se bloquea.
- `sx_try_upgrade_int` y `sx_downgrade_int`: las operaciones de cambio de modo.

Dale un vistazo rápido. Los internos son intrincados, pero la API pública se comporta tal como está documentada y el código fuente lo confirma.

## Referencia: Errores comunes con cv y sx

Cada nueva primitiva trae consigo un conjunto de errores que los principiantes cometen hasta que les ha costado caro. Un breve catálogo.

### Errores con cv

**Usar `if` en lugar de `while` alrededor de `cv_wait`.** La condición puede no ser verdadera al retornar debido a un despertar espurio. Itera siempre.

**Olvidar hacer broadcast en detach.** Los waiters nunca despiertan, el cv tiene waiters pendientes en el momento del destroy y el kernel puede entrar en pánico. Llama siempre a `cv_broadcast` antes de `cv_destroy`.

**Señalizar el cv equivocado.** Despertar a los lectores cuando se pretendía despertar a los escritores (o viceversa). Es un error fácil al refactorizar. El nombre del cv es tu mejor defensa; si `cv_signal(&sc->room_cv)` no te parece correcto en el punto de llamada, probablemente no lo es.

**Señalizar sin el interlock cuando el estado podría revertirse.** Si dos threads pueden modificar el estado, uno de ellos debe sostener el interlock al señalizar, o puede perderse un despertar. Por defecto, señaliza siempre bajo el interlock; relaja esa regla solo cuando puedas demostrar que el estado no puede revertirse.

**Omitir la comprobación tras la espera para detach.** Un waiter que despierta debido a un `cv_broadcast` de detach debe volver a comprobar `is_attached` y retornar `ENXIO`. Si se omite esa comprobación, el waiter continúa como si el dispositivo siguiera activo y provoca un crash.

**Llamar a `cv_wait` mientras se sostienen múltiples locks.** Solo se libera el interlock durante la espera. Los demás locks permanecen adquiridos. Si esos locks son necesarios para el thread que despierta, se produce un deadlock. Libera los demás locks primero.

### Errores con sx

**Mantener el sx durante una llamada que puede dormir.** Libéralo antes de llamar a `sysctl_handle_*`, `uiomove` o `malloc(M_WAITOK)`. El sx es sleepable, así que el kernel no entrará en pánico, pero el resto de waiters quedarán bloqueados durante toda la operación.

**Adquirir en modo compartido y luego intentar xlock sin liberar antes.** Llamar a `sx_xlock` mientras se sostiene el mismo sx en modo compartido es un deadlock; la llamada se bloqueará indefinidamente esperando a sí misma. Usa `sx_try_upgrade` o libera y vuelve a adquirir.

**Olvidar que sx es sleepable.** Llamar a `sx_xlock` desde un contexto donde está prohibido dormir (contexto de interrupción, dentro de un spin lock) provoca pánico. Usa `rw(9)` en esos contextos.

**Mantener el sx en modo compartido durante una operación larga.** Otros lectores pueden continuar, pero el escritor del sx quedará bloqueado indefinidamente. Si la operación es larga, libera el lock compartido, realiza el trabajo y vuelve a adquirirlo si necesitas confirmar los cambios.

**Liberar en el modo equivocado.** `sx_xunlock` sobre un lock en modo compartido es un error; `sx_sunlock` sobre un lock en modo exclusivo también lo es. Usa `sx_unlock` (la versión polimórfica) solo cuando no sepas en qué modo te encuentras (situación infrecuente).

### Errores específicos de combinar ambos

**Adquirir en el orden incorrecto.** El driver del Capítulo 12 requiere adquirir primero el mtx y luego el sx. El orden inverso produce una advertencia de `WITNESS` bajo carga.

**Liberar en el orden incorrecto.** Adquiere mtx, adquiere sx, libera mtx, libera sx. El orden de liberación *debe* ser el inverso del orden de adquisición: libera sx primero y luego mtx. De lo contrario, un observador entre las dos liberaciones verá una combinación inesperada.

**Snapshot-and-apply cuando la frescura de los datos importa.** El patrón es correcto solo cuando el snapshot puede tolerar cierta antigüedad en los datos. Para valores que deben estar actualizados (indicadores de seguridad, límites de cuota estrictos), el patrón snapshot-and-apply es incorrecto; debes mantener ambos locks de forma atómica.

**Olvidar actualizar LOCKING.md.** Añadir un lock o cambiar el orden sin actualizar la documentación provoca divergencia. Tres meses después, nadie recuerda cuál era la regla. Actualiza el documento en el mismo commit.



## Referencia: Primitivas de tiempo

Un recorrido breve por las formas en que el kernel expresa el tiempo. Útil al leer o escribir las variantes de espera con tiempo límite.

El kernel dispone de tres representaciones de tiempo de uso habitual:

- `int` ticks. La unidad heredada. `hz` ticks equivalen a un segundo. El valor predeterminado de `hz` en FreeBSD 14.3 es 1000, por lo que un tick equivale a un milisegundo. `mtx_sleep`, `cv_timedwait` y `tsleep` reciben sus timeouts en ticks.
- `sbintime_t`. Una representación en punto fijo binario de 64 bits con signo: los 32 bits superiores son segundos y los 32 inferiores son una fracción de segundo. Las constantes de unidad están en `/usr/src/sys/sys/time.h`: `SBT_1S`, `SBT_1MS`, `SBT_1US`, `SBT_1NS`. La API de tiempo más reciente (`msleep_sbt`, `cv_timedwait_sbt`, `callout_reset_sbt`) utiliza sbintime.
- `struct timespec`. Segundos y nanosegundos POSIX. Se utiliza en la frontera con el espacio de usuario; raramente es necesaria en el interior de un driver.

Funciones auxiliares de conversión en `time.h`:

- `tick_sbt`: una variable global que almacena `1 / hz` como sbintime, de modo que `tick_sbt * timo_in_ticks` da el sbintime equivalente.
- `nstosbt(ns)`, `ustosbt(us)`, `sbttous(sbt)`, `sbttons(sbt)`, `tstosbt(ts)`, `sbttots(ts)`: conversiones explícitas entre las distintas unidades.

La API de tiempo `_sbt` existe porque la granularidad de `hz` es demasiado gruesa para algunos usos. Con `hz=1000`, el timeout mínimo expresable es 1 ms y los timeouts se alinean a límites de tick. Con sbintime puedes expresar 100 microsegundos y pedirle al kernel que programe el despertar tan cerca de ese valor como lo permita el temporizador hardware.

En el Capítulo 12 utilizamos en todo momento la API basada en ticks porque la precisión es suficiente. Esta referencia está aquí para que sepas adónde acudir cuando la precisión sub-milisegundo sea importante.

El argumento `pr` de las funciones `_sbt` merece una explicación. Es la *precisión* que el llamador está dispuesto a aceptar: el margen que el kernel puede añadir para la coalescencia de temporizadores orientada al ahorro de energía. Una precisión de `SBT_1S` significa «no me importa que mi temporizador de 5 segundos se dispare hasta 1 segundo tarde; si puedes coalescerlo con otro temporizador para ahorrar energía, hazlo». Una precisión de `SBT_1NS` significa «dispara lo más cerca posible del plazo». Para código de driver, los valores habituales son `0` (sin margen) o `SBT_1MS` (un milisegundo de margen).

El argumento `flags` controla cómo se registra el temporizador. `C_HARDCLOCK` es el más habitual: alinea el temporizador con la interrupción hardclock del sistema para una temporización predecible. `C_DIRECT_EXEC` ejecuta el callout en la propia interrupción del temporizador en lugar de diferirlo a un thread de callout. `C_ABSOLUTE` interpreta `sbt` como un tiempo absoluto en lugar de un timeout relativo. En el Capítulo 12 usamos `C_HARDCLOCK` en todos los casos.



## Referencia: Advertencias habituales de WITNESS, descifradas

`WITNESS` produce varios tipos de advertencia. Cada uno tiene una forma reconocible.

### "lock order reversal"

La forma característica: dos líneas con el nombre del primer y del segundo lock, más una línea «established at». Ya hemos analizado el diagnóstico en la Sección 6.

Causa habitual: una ruta que adquiere los locks en un orden que contradice otro observado anteriormente. La corrección consiste en reordenar o reestructurar el código.

### "duplicate lock of same name"

La forma característica: una advertencia sobre la adquisición de un lock con el mismo `lo_name` que otro ya adquirido.

Causa habitual: dos instancias del mismo driver, cada una con su propio lock, ambos con el mismo nombre. `WITNESS` es conservador y asume que dos locks del mismo tipo pertenecen a la misma clase. La corrección consiste en inicializar cada lock con un nombre único (por ejemplo, incluyendo el número de unidad mediante `device_get_nameunit(dev)`), o bien pasando el indicador «duplicate-acquire OK» apropiado para cada clase en el momento de la inicialización: `MTX_DUPOK` para mutexes, `SX_DUPOK` para sx y `RW_DUPOK` para rwlocks. Cada uno de ellos expande el bit `LO_DUPOK` a nivel de objeto de lock; en el código del driver se escribe el nombre por clase.

### "sleeping thread (pid N) owns a non-sleepable lock"

La forma característica: un thread está en una primitiva de espera (`cv_wait`, `mtx_sleep`, `_sleep`) mientras sostiene un spin mutex o un rw lock.

Causa habitual: una función que adquiere un lock no sleepable y luego llama a algo que puede dormir. La corrección consiste en liberar antes el lock no sleepable.

### "exclusive sleep mutex foo not owned at"

La forma característica: un thread intentó liberar o verificar un mutex que no está adquirido.

Causa habitual: un puntero de mutex incorrecto, o una liberación sin la correspondiente adquisición en esta ruta de código. La corrección consiste en rastrear la adquisición del lock.

### "lock list reversal"

La forma característica: similar a lock order reversal, pero indica una inversión más compleja que involucra más de dos locks.

Causa habitual: una cadena de adquisiciones que, en conjunto, viola el orden global. La corrección consiste en simplificar el patrón de adquisición; si la cadena es realmente necesaria, considera si el diseño debería usar menos locks.

### "sleepable acquired while holding non-sleepable"

La forma característica: un thread intentó adquirir un lock sleepable (sx, mtx_def, lockmgr) mientras sostenía uno no sleepable (mtx_spin, rw).

Causa habitual: confusión sobre las clases de lock. La corrección consiste en cambiar el lock interior a una variante sleepable o en reestructurar el código para evitar el anidamiento.

### Actuar ante una advertencia

Cuando `WITNESS` se activa, la tentación es suprimir la advertencia. Resístela. La advertencia significa que el kernel ha observado una situación real que viola una regla real. Suprimirla oculta el error; no lo corrige.

Las respuestas correctas, en orden de preferencia:

1. Corrige el error (reordena los locks, libera uno, reestructura el código).
2. Explica por qué la advertencia es incorrecta para este caso y usa el indicador `_DUPOK` apropiado con un comentario en el código fuente.
3. Si no puedes hacer ninguna de las dos cosas, escala el problema. Pregunta en freebsd-hackers o abre una PR. Una advertencia de `WITNESS` que nadie puede explicar es un error real en algún lugar.



## Referencia: Consulta rápida de clases de lock

Una referencia compacta de las diferencias entre las clases de lock que has visto hasta ahora.

| Propiedad | `mtx_def` | `mtx_spin` | `sx` | `rw` | `rmlock` | `lockmgr` |
|---|---|---|---|---|---|---|
| Duerme cuando hay contención | Sí | No (espera activa) | Sí | No (espera activa) | No (mayoritariamente) | Sí |
| Múltiples poseedores | No | No | Sí (compartido) | Sí (lectura) | Sí (lectura) | Sí (compartido) |
| El poseedor puede dormir | Sí | No | Sí | No | No | Sí |
| Propagación de prioridad | Sí | n/a | No | Sí | n/a | Sí |
| Interrumpible por señal | n/a | n/a | `_sig` | No | No | Sí |
| Recursión soportada | Opcional | Sí | Opcional | No | No | Sí |
| Rastreado por WITNESS | Sí | Sí | Sí | Sí | Sí | Sí |
| Mejor uso en driver | Por defecto | Contexto de interrupción | Lecturas predominantes | Rutas de lectura críticas | Lecturas muy críticas | Sistemas de archivos |

`rmlock(9)` y `lockmgr(9)` se incluyen por completitud; este libro cubre `mtx`, `cv`, `sx` y `rw` en profundidad, y trata los demás como «se sabe que existen, consulta la página de manual si los necesitas».



## Referencia: Patrones de diseño con múltiples primitivas

Tres patrones se repiten en los drivers que combinan varias primitivas de sincronización. Merece la pena mencionar cada uno para que puedas reconocerlos en código real.

### Patrón: un mutex, un sx de configuración

El driver `myfirst` del Capítulo 12 sigue este patrón. El mutex protege la ruta de datos y un sx protege la configuración. El orden de adquisición es primero el mutex y luego el sx. La mayoría de los drivers sencillos encajan en este patrón.

Cuándo usarlo: la ruta de datos se ejecuta en contexto de proceso, tiene un invariante compuesto y lee la configuración ocasionalmente. La configuración se lee con frecuencia y se escribe raramente.

Cuándo no usarlo: cuando la ruta de datos se ejecuta en contexto de interrupción (el mutex debe ser `MTX_SPIN` y la configuración debe protegerse con `rw`), o cuando la propia ruta de datos tiene sub-rutas que se benefician de locks distintos.

### Patrón: lock por cola con un lock de configuración

Un driver con múltiples colas (una por CPU, una por consumidor, una por flujo) asigna a cada cola su propio lock y usa un sx independiente para la configuración. El orden de adquisición es primero el lock de la cola y luego el sx de configuración. El orden entre locks de distintas colas no está definido (nunca deberías sostener dos locks de cola simultáneamente).

Cuándo usarlo: sistemas con muchos núcleos, cuando la carga de trabajo se distribuye de forma natural por colas.

Cuándo no usarlo: cuando la carga de trabajo es simétrica y los locks por cola no aportarían ventajas, o cuando los datos cruzan colas con frecuencia y obligarían a definir reglas de orden.

### Patrón: lock por objeto con un lock de contenedor

Un driver que mantiene una lista de objetos (dispositivos, sesiones, descriptores) asigna a cada objeto su propio lock y usa un lock de contenedor para proteger la lista. Recorrer la lista requiere el lock de contenedor; modificar un objeto requiere el lock de ese objeto; ambos pueden sostenerse en el orden contenedor primero, objeto segundo.

Cuándo usarlo: cuando tanto las operaciones sobre la lista como las operaciones por objeto necesitan protección, con ciclos de vida distintos.

Cuándo no usarlo: con un único mutex sería suficiente (listas pequeñas, operaciones poco frecuentes).

El driver `myfirst` aún no necesita este patrón; los drivers futuros del libro sí lo necesitarán.

### Patrón: contadores por CPU con cola protegida por mutex

Este es el patrón del capítulo 11, que el capítulo 12 hereda. Los contadores de alta frecuencia (bytes_read, bytes_written) usan almacenamiento por CPU de `counter(9)`. El cbuf, con su invariante compuesto, usa un único mutex. Los dos son independientes; las actualizaciones de los contadores no necesitan el mutex; las actualizaciones del cbuf sí.

Cuándo usarlo: un contador de alta frecuencia se encuentra junto a una estructura con un invariante compuesto.

Cuándo no usarlo: las actualizaciones del contador necesitan ser consistentes con las actualizaciones de la estructura (en ese caso, ambas necesitan el mismo lock).

### Patrón: snapshot-and-apply entre dos clases de lock

Cada vez que un camino de ejecución necesita ambas clases de lock, el patrón snapshot-and-apply reduce la restricción de orden de lock a una única dirección. Lee de un lock, suéltalo y toma el otro. El snapshot puede estar ligeramente desactualizado; para valores orientativos, eso es aceptable.

Cuándo usarlo: el valor que se captura en el snapshot no requiere ser estrictamente actual; un desfase del orden de microsegundos es aceptable.

Cuándo no usarlo: el valor es un indicador de seguridad, un límite presupuestario estricto o cualquier cosa en la que el desfase pueda violar un contrato.

El camino de ejecución de `myfirst_write` utiliza esto para el límite suave de bytes: captura el límite bajo el cfg sx, lo suelta, toma el mutex de datos y comprueba el límite frente al `cb_used` actual. La operación combinada no es atómica, pero es correcta en el sentido de que cualquier condición de carrera hace que la respuesta incorrecta sea una respuesta incorrecta tolerable (rechazar una escritura que habría cabido, o aceptar una escritura que apenas desborda; ambas son recuperables).

---

## Referencia: precondiciones de cada primitiva

Cada primitiva tiene reglas sobre cuándo y cómo puede usarse. Violar una regla es un bug; las reglas se enumeran aquí para consulta rápida.

### mtx(9) (MTX_DEF)

Precondiciones para `mtx_init`:
- Existe memoria para el `struct mtx` y no tiene aliasing.
- El mutex no está ya inicializado.

Precondiciones para `mtx_lock`:
- El mutex está inicializado.
- El thread llamante está en contexto de proceso, contexto de kernel-thread o contexto callout-mpsafe.
- El thread llamante no tiene ya el mutex (salvo que se use `MTX_RECURSE`).
- El thread llamante no tiene ningún spin mutex.

Precondiciones para `mtx_unlock`:
- El thread llamante tiene el mutex.

Precondiciones para `mtx_destroy`:
- El mutex está inicializado.
- Ningún thread tiene el mutex.
- Ningún thread está bloqueado en el mutex.

### cv(9)

Precondiciones para `cv_init`:
- Existe memoria para el `struct cv`.
- El cv no está ya inicializado.

Precondiciones para `cv_wait` y `cv_wait_sig`:
- El thread llamante tiene el mutex de interlock.
- El thread llamante está en un contexto suspendible.

Precondiciones para `cv_signal` y `cv_broadcast`:
- El cv está inicializado.
- Por convención: el thread llamante tiene el mutex de interlock (no es estrictamente requerido por la API, pero es una práctica defensiva).

Precondiciones para `cv_destroy`:
- El cv está inicializado.
- Ningún thread está bloqueado en el cv (la cola de espera debe estar vacía).

### sx(9)

Precondiciones para `sx_init`:
- Existe memoria para el `struct sx`.
- El sx no está ya inicializado (salvo que se use `SX_NEW`).

Precondiciones para `sx_xlock` y `sx_xlock_sig`:
- El sx está inicializado.
- El thread llamante no tiene ya el sx en modo exclusivo (salvo que se use `SX_RECURSE`).
- El thread llamante está en un contexto suspendible.
- El thread llamante no tiene ningún lock no suspendible (ningún spin mutex, ningún rw lock).

Precondiciones para `sx_slock` y `sx_slock_sig`:
- Igual que `sx_xlock`, salvo que la comprobación de recursión se aplica al modo compartido.

Precondiciones para `sx_xunlock` y `sx_sunlock`:
- El thread llamante tiene el sx en el modo correspondiente.

Precondiciones para `sx_destroy`:
- El sx está inicializado.
- Ningún thread tiene el sx en ningún modo.
- Ningún thread está bloqueado en el sx.

### rw(9)

Precondiciones para `rw_init`, `rw_destroy`: la misma forma que `sx_init`, `sx_destroy`.

Precondiciones para `rw_wlock` y `rw_rlock`:
- El rw está inicializado.
- El thread llamante no tiene actualmente el rw en un modo conflictivo.
- El thread llamante *no* está obligado a estar en un contexto suspendible. El rw lock en sí no duerme; sin embargo, el thread llamante *no debe* llamar a ninguna función que pueda dormir mientras tenga el rw.

Precondiciones para `rw_wunlock` y `rw_runlock`:
- El thread llamante tiene el rw en el modo correspondiente.

Seguir estas precondiciones es la diferencia entre un driver que funciona limpiamente bajo `WITNESS` durante años y uno que provoca un panic inesperado en el primer camino de código poco habitual.

---

## Referencia: autoevaluación del capítulo 12

Usa esta rúbrica para confirmar que has interiorizado el material del capítulo 12 antes de pasar al capítulo 13. Cada pregunta debería poder responderse sin releer el capítulo.

### Preguntas conceptuales

1. **Nombra las tres formas principales de sincronización.** Exclusión mutua, acceso compartido con escrituras restringidas, espera coordinada.

2. **¿Por qué es preferible una variable de condición a un canal de activación anónimo?** Cada cv representa una condición lógica; las señales no despiertan a los waiters no relacionados; el cv tiene un nombre visible en `procstat` y `dtrace`; la API hace cumplir el contrato atómico de soltar-y-dormir a través de sus tipos.

3. **¿Cuál es la diferencia entre cv_signal y cv_broadcast?** `cv_signal` despierta a un waiter (el de mayor prioridad, FIFO entre iguales); `cv_broadcast` despierta a todos los waiters. Usa signal para cambios de estado por evento; usa broadcast para cambios globales (detach, reset).

4. **¿Qué devuelve cv_wait_sig cuando es interrumpido por una señal?** Devuelve `EINTR` o `ERESTART`, según la disposición de reinicio de la señal. El driver propaga el valor sin modificarlo.

5. **¿Cuál es la diferencia entre los locks sx y rw?** `sx(9)` es suspendible; `rw(9)` no lo es. Usa `sx` en contexto de proceso donde la sección crítica puede incluir llamadas que duerman; usa `rw` cuando la sección crítica pueda ejecutarse en contexto de interrupción o no deba dormir.

6. **¿Por qué existe sx_try_upgrade en lugar de un sx_upgrade incondicional?** Porque dos titulares simultáneos que intenten un upgrade incondicional provocarían un deadlock. La variante `try` devuelve un fallo cuando hay otro titular compartido, lo que permite al llamante retirarse limpiamente.

7. **¿Qué es el patrón snapshot-and-apply y por qué es útil?** Adquiere un lock, lee los valores necesarios en variables locales, suéltalo; luego adquiere un lock diferente y usa los valores locales. Evita mantener dos locks simultáneamente, relajando las restricciones de orden de lock. Es aceptable cuando el snapshot puede tolerar un pequeño desfase.

8. **¿Cuál es el orden canónico de lock en el driver del capítulo 12?** sc->mtx antes que sc->cfg_sx. Documentado en `LOCKING.md`; aplicado por `WITNESS`.

### Preguntas de lectura de código

Abre el código fuente de tu driver del capítulo 12 y verifica:

1. Cada `cv_wait_sig` está dentro de un bucle `while (!condition)`.
2. Cada cv tiene al menos un llamante de signal y uno de broadcast (el broadcast en detach es aceptable).
3. Cada `sx_xlock` tiene un `sx_xunlock` correspondiente en cada camino de código, incluidas las salidas por error.
4. El camino de detach hace broadcast en cada cv antes de destruir cualquier primitiva.
5. El cfg sx se suelta antes de cualquier llamada que pueda dormir (`sysctl_handle_*`, `uiomove`, `malloc(M_WAITOK)`).
6. La regla de orden de lock (mtx primero, sx segundo) se sigue en cada camino que tenga ambos.

### Preguntas prácticas

1. Carga el driver del capítulo 12 en un kernel con `WITNESS` y ejecuta el estrés compuesto durante 30 minutos. ¿Hay alguna advertencia? Si las hay, investiga.

2. Establece `read_timeout_ms` a 100 y ejecuta una `read(2)` contra un dispositivo inactivo. ¿Qué devuelve la llamada? ¿Al cabo de cuánto tiempo?

3. Alterna `debug_level` entre 0 y 3 con `sysctl -w` mientras se ejecuta `mp_stress`. ¿El nivel tiene efecto de inmediato? ¿Se rompe algo?

4. Usa `lockstat(1)` para medir la contención en el lock sx bajo una carga de trabajo con muchas escrituras de configuración. ¿Cuánto tiempo de espera hay?

5. Abre el código fuente de kern_condvar.c y encuentra la función `cv_signal`. Léela. ¿Puedes describir lo que hace en dos frases?

Si las cinco preguntas prácticas se superan y las preguntas conceptuales resultan sencillas, tu trabajo del capítulo 12 es sólido.

---

## Referencia: lecturas adicionales sobre sincronización

Para los lectores que quieran profundizar más allá de lo que cubre este capítulo:

### Páginas del manual

- `mutex(9)`: la API de mutex (cubierta íntegramente en el capítulo 11; se incluye aquí a modo de referencia).
- `condvar(9)`: la API de variables de condición.
- `sx(9)`: la API de lock compartido/exclusivo.
- `rwlock(9)`: la API de lock de lectura/escritura.
- `rmlock(9)`: la API de lock de lectura predominante (avanzado).
- `sema(9)`: la API de semáforo de conteo (avanzado).
- `epoch(9)`: el framework de lectura predominante con recuperación diferida (avanzado; relevante para drivers de red).
- `locking(9)`: una visión general de las primitivas de lock de FreeBSD.
- `lock(9)`: la infraestructura común de objetos de lock.
- `witness(4)`: el comprobador de orden de lock WITNESS (cubierto en el capítulo 11; revisado en este capítulo).
- `lockstat(1)`: la herramienta de perfilado de locks en espacio de usuario.
- `dtrace(1)`: el framework de trazado dinámico, cubierto con más profundidad en el capítulo 15.

### Archivos fuente

- `/usr/src/sys/kern/kern_condvar.c`: la implementación de cv.
- `/usr/src/sys/kern/kern_sx.c`: la implementación de sx.
- `/usr/src/sys/kern/kern_rwlock.c`: la implementación de rw.
- `/usr/src/sys/kern/subr_sleepqueue.c`: la maquinaria de la cola de sleep que subyace a cv y otras primitivas de sleep.
- `/usr/src/sys/kern/subr_turnstile.c`: la maquinaria de turnstile que subyace a rw y otras primitivas de propagación de prioridad.
- `/usr/src/sys/sys/condvar.h`, `/usr/src/sys/sys/sx.h`, `/usr/src/sys/sys/rwlock.h`: las cabeceras de la API pública.
- `/usr/src/sys/sys/_lock.h`, `/usr/src/sys/sys/lock.h`: la estructura común de objetos de lock y el registro de clases.

### Material externo

Para la teoría de concurrencia aplicable a cualquier sistema operativo, *The Art of Multiprocessor Programming* de Herlihy y Shavit es excelente. Para los aspectos internos del kernel específicos de FreeBSD, *The Design and Implementation of the FreeBSD Operating System* de McKusick y otros sigue siendo el libro de referencia canónico; los capítulos sobre lock y planificación son especialmente relevantes.

Ninguno de los dos libros es necesario para este capítulo. Ambos son útiles cuando llegue el momento de un estudio más profundo.

---

## Mirando al futuro: puente hacia el capítulo 13

El capítulo 13 se titula *Temporizadores y trabajo diferido*. Su alcance es la infraestructura de tiempo del kernel vista desde un driver: cómo programar una callback para algún momento en el futuro, cómo cancelarla limpiamente, cómo gestionar las reglas en torno a los callouts que pueden ejecutarse de forma concurrente con los otros caminos de código del driver, y cómo usar timers para patrones típicos de drivers como watchdogs, trabajo diferido y polling periódico.

El capítulo 12 preparó el terreno de tres maneras específicas.

En primer lugar, ya sabes cómo esperar con un timeout. El mecanismo `callout(9)` del capítulo 13 es la misma idea vista desde el otro lado: en lugar de "despiértame en el tiempo T", es "ejecuta esta función en el tiempo T". Las reglas de sincronización en torno a los callouts (los callouts se ejecutan en un kernel thread, pueden tener condiciones de carrera con el resto de tu código, deben drenarse antes de su destrucción) se basan en la disciplina que el capítulo 12 estableció para los cvs y los sxs.

En segundo lugar, ya sabes cómo diseñar un driver con múltiples primitivas. Los callouts del capítulo 13 añaden otro contexto de ejecución al driver: el handler del callout se ejecuta de forma concurrente con `myfirst_read`, `myfirst_write` y los handlers de sysctl. Eso significa que los handlers de callout participan en el orden de lock. El `LOCKING.md` que escribiste en el capítulo 12 absorberá el añadido con una nueva entrada.

En tercer lugar, ya sabes cómo depurar bajo carga. El capítulo 13 introduce una nueva clase de bugs (condiciones de carrera de callout en el momento de la descarga) que se beneficia del mismo flujo de trabajo con `WITNESS`, `lockstat` y `dtrace` que enseñó el capítulo 12.

Los temas específicos que cubrirá el capítulo 13 incluyen:

- La API `callout(9)`: `callout_init`, `callout_init_mtx`, `callout_reset`, `callout_stop`, `callout_drain`.
- El callout consciente del lock (`callout_init_mtx`) y por qué es la opción adecuada por defecto para el código de drivers.
- Reutilización de callouts: cómo programar el mismo callout múltiples veces de forma segura.
- La condición de carrera en la descarga: cómo un callout que se dispara después de `kldunload` puede provocar un fallo del kernel, y cómo prevenirlo con `callout_drain`.
- Patrones periódicos: el watchdog, el heartbeat, el reaper diferido.
- Las abstracciones de tiempo `tick_sbt` y `sbintime_t`, útiles para temporización submilisegundo.
- Comparación con `timeout(9)` (la interfaz más antigua, obsoleta para código nuevo).

No necesitas adelantarte. El material del Capítulo 12 es preparación suficiente. Trae contigo el driver del Capítulo 12, tu kit de pruebas y tu kernel con `WITNESS` habilitado. El Capítulo 13 empieza donde terminó el Capítulo 12.

Una breve reflexión de cierre. Empezaste este capítulo con un mutex, un canal anónimo y una idea clara de lo que significaba la sincronización. Lo abandonas con tres primitivas, un orden de locks documentado, un vocabulario más rico y la experiencia de depurar problemas de coordinación reales con herramientas reales del kernel. Esa progresión es el núcleo de la Parte 3 de este libro. A partir de aquí, el Capítulo 13 amplía la conciencia del driver sobre el *tiempo*, el Capítulo 14 amplía su conciencia sobre las *interrupciones*, y los capítulos restantes de la Parte 3 te preparan para los capítulos de hardware de la Parte 4.

Tómate un momento. El driver con el que comenzaste la Parte 3 solo sabía gestionar un thread a la vez. El driver que tienes ahora coordina muchos threads entre dos clases de locks, puede reconfigurarse en tiempo de ejecución sin interrumpir su ruta de datos, y respeta las señales y los plazos del usuario. Eso es un salto cualitativo real. Después, pasa la página.

Cuando abras el Capítulo 13, lo primero que verás es `callout(9)`, la infraestructura de callbacks temporizados del kernel. La disciplina que aprendiste aquí para los cvs, los sxs y el diseño consciente del orden de locks se transfiere directamente. Los callouts son simplemente otro contexto de ejecución concurrente que participa en el orden de locks; los patrones del Capítulo 12 los absorben sin volverse frágiles. El vocabulario de sincronización es el mismo; lo que es nuevo es el vocabulario del tiempo.
