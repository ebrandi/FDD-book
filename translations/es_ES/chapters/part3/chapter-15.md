---
title: "Más sincronización: condiciones, semáforos y coordinación"
description: "La última milla de la Parte 3: semáforos contadores para el control de admisión, patrones refinados de sx(9) para estados de lectura predominante, esperas interrumpibles y con timeout, handshakes entre componentes, y una capa de wrapper que convierte la estrategia de sincronización del driver en algo que un futuro mantenedor pueda leer de verdad."
partNumber: 3
partName: "Concurrency and Synchronization"
chapter: 15
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 165
language: "es-ES"
---
# Más sincronización: condiciones, semáforos y coordinación

## Orientación al lector y resultados

Al final del Capítulo 14, tu driver `myfirst` había alcanzado un estado cualitativamente diferente al del inicio de la Parte 3. Dispone de un mutex de ruta de datos documentado, dos variables de condición, un sx lock de configuración, tres callouts, una taskqueue privada con tres tareas y una ruta de detach que vacía cada primitiva en el orden correcto. El driver es, por primera vez en el libro, algo más que una colección de manejadores. Es una composición de primitivas de sincronización que cooperan para ofrecer un comportamiento acotado y seguro bajo carga.

El Capítulo 15 trata de llevar esa composición más lejos. La mayoría de los drivers reales acaban descubriendo que los mutexes y las variables de condición básicas, incluso combinadas con sx locks y taskqueues, no son siempre las primitivas más adecuadas para expresar un problema concreto. Un driver puede necesitar limitar el número de escritores concurrentes, gestionar un pool acotado de ranuras de hardware reutilizables, coordinar un handshake entre un callout y una tarea, permitir que una lectura lenta se despierte ante una señal sin perder el progreso parcial realizado, o exponer el estado de shutdown a través de varios subsistemas de una manera que cualquier parte del código pueda comprobar con poco coste. Cada una de esas situaciones tiene solución con lo que ya conoces, pero cada una dispone también de una primitiva o de un patrón de uso que hace la solución más directa y el código más legible. Este capítulo enseña esas primitivas y patrones de uso uno a uno, los aplica al driver y cierra el resultado con una pequeña capa envolvente que convierte llamadas dispersas en un vocabulario con nombre.

El capítulo es también el último de la Parte 3. Después del Capítulo 15 comienza la Parte 4 y el libro se orienta hacia el hardware. Todas las primitivas que la Parte 3 te ha enseñado, desde el primer `mtx_init` del Capítulo 11 hasta el último `taskqueue_drain` del Capítulo 14, te acompañan hacia la Parte 4. Los patrones de coordinación de este capítulo no son un tema adicional. Son la pieza final del conjunto de herramientas de sincronización que tu driver lleva consigo a los capítulos orientados al hardware.

### Por qué este capítulo tiene sentido propio

Podrías saltarte este capítulo. El driver tal como queda al final del Capítulo 14 es funcional, está probado y es técnicamente correcto. Su disciplina de mutex y variable de condición es sólida. Su orden de detach funciona. Su taskqueue está limpia.

Lo que le falta al driver, y lo que el Capítulo 15 añade, es un pequeño conjunto de herramientas más precisas para situaciones específicas de coordinación que los mutexes y las variables de condición básicas expresan de forma incómoda. Un semáforo contador son unas pocas líneas de código que dicen "como máximo N participantes a la vez"; expresar el mismo invariante con un mutex, un contador y una variable de condición requiere más líneas y oculta la intención. Un patrón refinado de sx con `sx_try_upgrade` permite que una ruta de lectura promocione ocasionalmente a escritora sin liberar su ranura y competir con otros posibles escritores; sin la primitiva, hay que escribir bucles de reintento poco elegantes. Un uso correcto de `cv_timedwait_sig` distingue entre EINTR y ERESTART, y entre "el llamante fue interrumpido" y "el plazo expiró"; una espera ingenua deja al llamante bloqueado o abandona el trabajo parcial ante cualquier señal.

La recompensa de aprender estas herramientas no es solo que el refactor del capítulo actual quedará más limpio. Es que cuando leas un driver de producción de FreeBSD dentro de un año, reconocerás estas formas de inmediato. Cuando `/usr/src/sys/dev/hyperv/storvsc/hv_storvsc_drv_freebsd.c` llame a `sema_wait` en un semáforo por solicitud para bloquearse hasta que el hardware complete la operación, sabrás qué estaba pensando su autor. Cuando un driver de red recurra a `sx_try_upgrade` en una ruta de actualización de estadísticas, sabrás por qué era la llamada correcta. Sin el Capítulo 15, esas llamadas son opacas. Con el Capítulo 15, son obvias.

La otra recompensa es el mantenimiento. Un driver que dispersa su vocabulario de sincronización en cien lugares es difícil de modificar. Un driver que encapsula su sincronización en una pequeña capa con nombre (aunque solo sea un conjunto de funciones inline en un header) es fácil de cambiar. La Sección 6 recorre esa encapsulación explícitamente; al final del capítulo, tu driver dispondrá de un pequeño `myfirst_sync.h` que nombra cada primitiva de coordinación que utiliza. Añadir un nuevo estado sincronizado más adelante se convierte en ampliar el header, no en dispersar nuevas llamadas a `mtx_lock`/`mtx_unlock` por todo el archivo.

### Dónde dejó el driver el Capítulo 14

Antes de empezar, conviene verificar algunos prerrequisitos. El Capítulo 15 extiende el driver producido al final de la Etapa 4 del Capítulo 14 (versión `0.8-taskqueues`). Si alguno de los puntos siguientes te resulta incierto, vuelve al Capítulo 14 antes de comenzar este.

- Tu driver `myfirst` compila sin errores y se identifica como versión `0.8-taskqueues`.
- Usa las macros `MYFIRST_LOCK`/`MYFIRST_UNLOCK` alrededor de `sc->mtx` (el mutex de ruta de datos).
- Usa `MYFIRST_CFG_SLOCK`/`MYFIRST_CFG_XLOCK` alrededor de `sc->cfg_sx` (el sx de configuración).
- Usa dos variables de condición con nombre (`sc->data_cv`, `sc->room_cv`) para lecturas y escrituras bloqueantes.
- Admite lecturas con tiempo máximo mediante `cv_timedwait_sig` y el sysctl `read_timeout_ms`.
- Tiene tres callouts (`heartbeat_co`, `watchdog_co`, `tick_source_co`) con sus sysctls de intervalo.
- Tiene una taskqueue privada (`sc->tq`) con tres tareas (`selwake_task`, `bulk_writer_task`, `reset_delayed_task`).
- El orden de locks `sc->mtx -> sc->cfg_sx` está documentado en `LOCKING.md` y aplicado por `WITNESS`.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` están habilitados en tu kernel de prueba; lo has construido y arrancado.
- El kit de estrés del Capítulo 14 funciona sin errores bajo el kernel de depuración.

Ese driver es el que extendemos en el Capítulo 15. Los añadidos son modestos en volumen pero sustanciales en lo que posibilitan. La ruta de datos del driver no cambia a nivel mecánico; lo que cambia es el vocabulario que utiliza para hablar de concurrencia.

### Qué aprenderás

Al terminar este capítulo serás capaz de:

- Reconocer cuándo un mutex más una variable de condición no es la primitiva adecuada para un invariante concreto, y nombrar la alternativa (un semáforo, un patrón de upgrade de sx, una marca atómica con barrera de memoria, un contador por CPU o una función coordinadora encapsulada).
- Explicar qué es un semáforo contador, en qué se diferencia de un mutex y de un semáforo binario, y por qué la API `sema(9)` de FreeBSD es específicamente una API de semáforo contador sin concepto de propiedad.
- Usar correctamente `sema_init`, `sema_wait`, `sema_post`, `sema_trywait`, `sema_timedwait`, `sema_value` y `sema_destroy`, incluido el contrato de ciclo de vida que exige que no haya waiters presentes cuando se llama a `sema_destroy`.
- Describir las limitaciones conocidas del semáforo del kernel de FreeBSD: sin herencia de prioridad, sin espera interrumpible por señal, y la orientación que ofrece `/usr/src/sys/kern/kern_sema.c` sobre por qué no son un sustituto general de un mutex más una variable de condición.
- Refinar el uso de sx del driver con patrones de `sx_try_upgrade`, `sx_downgrade`, `sx_xlocked` y `sx_slock` que expresan cargas de trabajo predominantemente de lectura de forma limpia.
- Distinguir entre `cv_wait`, `cv_wait_sig`, `cv_timedwait` y `cv_timedwait_sig`, y saber qué devuelve cada uno en caso de tiempo agotado, señal y wakeup normal.
- Gestionar correctamente los valores de retorno EINTR y ERESTART de las esperas interrumpibles por señal, de modo que `read(2)` y `write(2)` sobre el driver respondan de forma sensata a `SIGINT` y similares.
- Construir handshakes entre componentes (entre un callout, una tarea y un thread de usuario) mediante una pequeña marca de estado protegida por el mutex del driver.
- Introducir un header `myfirst_sync.h` que nombre cada primitiva de sincronización que usa el driver, para que futuros colaboradores puedan cambiar la estrategia de locking en un único lugar.
- Usar correctamente la API `atomic(9)` para pequeños pasos de coordinación libres de locks, especialmente las marcas de shutdown que deben ser visibles entre contextos sin necesidad de un lock.
- Escribir pruebas de estrés que provoquen deliberadamente condiciones de carrera en la sincronización del driver y confirmen que las primitivas las gestionan correctamente.
- Refactorizar el driver a la versión `0.9-coordination` y actualizar `LOCKING.md` con una sección Semáforos y una sección Coordinación.

Es una lista larga. Cada punto en ella es pequeño; el valor del capítulo está en la composición.

### Qué no cubre este capítulo

Varios temas adyacentes quedan explícitamente aplazados para que el Capítulo 15 se mantenga enfocado.

- **Manejadores de interrupciones de hardware y la separación completa entre los contextos de ejecución `FILTER` e `ITHREAD`.** La Parte 4 introduce `bus_setup_intr(9)` y la historia completa de las interrupciones. El Capítulo 15 menciona los contextos relacionados con interrupciones solo cuando ilustran un patrón de sincronización que podrías reutilizar.
- **Estructuras de datos libres de locks a escala.** La familia `atomic(9)` cubre pequeñas marcas de coordinación; no cubre SMR, hazard pointers, análogos a RCU ni colas libres de locks completas. El Capítulo 15 aborda brevemente las operaciones atómicas y epoch; la historia más profunda sobre la ausencia de locks pertenece a un tratamiento especializado de las interioridades del kernel.
- **Ajuste detallado del planificador.** Prioridades de thread, clases RT, herencia de prioridad, afinidad de CPU: fuera del alcance. Se eligen valores predeterminados razonables y se continúa.
- **Semáforos POSIX de userland y SysV IPC.** `sema(9)` en el kernel es algo distinto. El Capítulo 15 se centra en la primitiva del kernel.
- **Microbenchmarks de rendimiento.** Lockstat y el perfilado de locks con DTrace reciben una mención, no un tratamiento completo. Un capítulo de rendimiento dedicado más adelante en el libro, cuando exista, asumirá esa carga.
- **Primitivas de coordinación entre procesos.** Algunos drivers necesitan coordinarse con procesos auxiliares en el espacio de usuario; ese problema es fundamentalmente diferente y pertenece a un capítulo posterior sobre protocolos basados en ioctl.

Mantenerse dentro de esos límites preserva la coherencia del modelo mental del capítulo. El Capítulo 15 añade el conjunto de herramientas de coordinación; la Parte 4 y los capítulos posteriores aplican ese conjunto a escenarios orientados al hardware.

### Tiempo estimado de dedicación

- **Solo lectura**: alrededor de tres o cuatro horas. La superficie de la API es pequeña, pero la composición requiere algo de reflexión.
- **Lectura más escritura de los ejemplos trabajados**: de siete a nueve horas en dos sesiones. El driver evoluciona en cuatro etapas.
- **Lectura más todos los laboratorios y desafíos**: de doce a dieciséis horas en tres o cuatro sesiones, incluido el tiempo para ejecutar pruebas de estrés contra rutas de código propensas a condiciones de carrera.

Si encuentras la Sección 5 (coordinación entre subsistemas) desconcertante en la primera lectura, es normal. El material es conceptualmente simple, pero requiere mantener varias partes del driver en la cabeza a la vez. Para, vuelve a leer el handshake trabajado en la Sección 5 y continúa cuando el diagrama se haya asentado.

### Prerrequisitos

Antes de comenzar este capítulo, confirma:

- El código fuente de tu driver coincide con la Etapa 4 del Capítulo 14 (`stage4-final`). El punto de partida asume todas las primitivas del Capítulo 14, todos los callouts del Capítulo 13, todas las variables de condición y sx del Capítulo 12, y el modelo de E/S concurrente del Capítulo 11.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con `/usr/src` en disco y coincidiendo con el kernel en ejecución.
- Un kernel de depuración con `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` está construido, instalado y arrancando correctamente.
- Entiendes el orden de detach del Capítulo 14 lo suficientemente bien como para extenderlo sin perderte.
- Tienes un modelo mental cómodo de `cv_wait_sig` y `cv_timedwait_sig` a partir de los Capítulos 12 y 13.

Si alguno de los puntos anteriores es inestable, corrígelo ahora en lugar de avanzar en el Capítulo 15 intentando razonar desde una base movediza. Las primitivas del Capítulo 15 son más precisas que las anteriores y amplifican la disciplina (o su ausencia) que el driver ya tiene.

### Cómo sacar el máximo partido a este capítulo

Tres hábitos darán sus frutos rápidamente.

En primer lugar, ten `/usr/src/sys/kern/kern_sema.c` y `/usr/src/sys/sys/sema.h` a mano como referencia. La implementación es corta, menos de doscientas líneas, y es el camino más directo para entender qué hace realmente un semáforo de FreeBSD. Lee `_sema_wait`, `_sema_post` y `_sema_timedwait` detenidamente una vez. Saber que un semáforo es "un contador más un mutex más una variable de condición, envueltos en una API" hace que el resto del capítulo resulte evidente.

> **Una nota sobre los números de línea.** Cada referencia al código fuente en este capítulo está anclada a un nombre de función, macro o estructura, no a un número de línea. `sema_init` y `_sema_wait` en `kern_sema.c`, y `sx_try_upgrade` en `/usr/src/sys/kern/kern_sx.c`, seguirán siendo localizables por su nombre en todas las versiones 14.x de FreeBSD; la línea que ocupa cada uno puede desplazarse a medida que se revisa el código circundante. En caso de duda, busca el símbolo con grep.

En segundo lugar, compara cada nueva primitiva con lo que habrías escrito con las anteriores. El ejercicio de preguntarte «si no tuviera `sema(9)`, ¿cómo expresaría esto?» resulta muy instructivo. Escribir la alternativa con `mtx` y `cv` suele ser posible, pero la versión con semáforo es con frecuencia la mitad de larga y considerablemente más clara. Ver ese contraste es la manera en que el valor de la primitiva se vuelve algo concreto.

En tercer lugar, escribe los cambios a mano y ejecuta cada etapa bajo `WITNESS`. Los errores de sincronización avanzados casi siempre los detecta `WITNESS` en el primer contacto si ejecutas un kernel de depuración; en un kernel de producción son casi siempre silenciosos hasta el primer fallo. El código de acompañamiento en `examples/part-03/ch15-more-synchronization/` es la versión de referencia, pero la memoria muscular de escribir `sema_init(&sc->writers_sema, 4, "myfirst writers")` una sola vez vale más que leerlo diez veces.

### Mapa de ruta del capítulo

Las secciones, en orden, son:

1. **Cuándo los mutexes y las condition variables no son suficientes.** Un repaso de las formas de problema que se benefician de una primitiva diferente.
2. **Uso de semáforos en el kernel de FreeBSD.** La API `sema(9)` en profundidad, con la refactorización del límite de escritores como Etapa 1 del driver del Capítulo 15.
3. **Escenarios de lectura predominante y acceso compartido.** Patrones refinados de `sx(9)`, incluidos `sx_try_upgrade` y `sx_downgrade`, con una pequeña refactorización de caché de estadísticas como Etapa 2.
4. **Condition variables con tiempo de espera e interrupción.** Un tratamiento detallado de `cv_timedwait_sig`, EINTR frente a ERESTART, el manejo del progreso parcial y el ajuste mediante sysctl que permite a los lectores observar el comportamiento. Etapa 3 del driver.
5. **Sincronización entre módulos o subsistemas.** Handshakes entre callouts, tareas y threads de usuario mediante pequeños flags de estado. Operaciones atómicas y ordenamiento de memoria a nivel introductorio. La Etapa 4 comienza aquí.
6. **Sincronización y diseño modular.** La cabecera `myfirst_sync.h`, la disciplina de nomenclatura y cómo cambia la forma del driver cuando la sincronización se encapsula.
7. **Pruebas de sincronización avanzada.** Conjuntos de pruebas de estrés, inyección de fallos y los sysctls de observabilidad que permiten ver las primitivas en funcionamiento.
8. **Refactorización y versiones.** Etapa 4 completada, incremento de versión a `0.9-coordination`, `LOCKING.md` ampliado y el pase de regresión final de la Parte 3.

Tras las ocho secciones llegan los laboratorios prácticos, los ejercicios de desafío, una referencia de solución de problemas, un apartado de cierre que clausura la Parte 3 y un puente hacia el Capítulo 16 que abre la Parte 4. El mismo material de referencia y resumen con el que terminaban los Capítulos 13 y 14 reaparece aquí al final.

Si es tu primera lectura, hazla de forma lineal y realiza los laboratorios en orden. Si estás repasando el contenido, las Secciones 5 y 6 son independientes y se pueden leer en una sola sesión.



## Sección 1: Cuándo los mutexes y las condition variables no son suficientes

El mutex del Capítulo 11 y la condition variable del Capítulo 12 son las primitivas predeterminadas de sincronización de drivers en FreeBSD. Casi todos los drivers los utilizan. Muchos drivers no usan nada más. Para una gran categoría de problemas, la combinación es exactamente lo adecuado: un mutex protege el estado compartido, una condition variable permite que un thread en espera duerma hasta que el estado coincida con un predicado, y ambos juntos expresan con claridad "espera a que el estado sea aceptable" y "avisa a otros de que el estado ha cambiado".

Esta sección trata los problemas en los que ese enfoque predeterminado resulta incómodo. No porque la combinación mutex-y-cv no pueda expresar la invariante, sino porque lo hace de forma más verbosa y más propensa a errores que otra primitiva diferente. Reconocer esas formas es el primer paso para usar la herramienta adecuada.

### La forma del desajuste

Cada primitiva de sincronización tiene un modelo subyacente sobre lo que protege. Un mutex protege la exclusión mutua: como máximo un thread ejecuta dentro del lock en un momento dado. Una condition variable protege un predicado: un thread en espera duerme hasta que el predicado se vuelve verdadero, y quien señaliza afirma que el predicado ha cambiado. Ambos se complementan porque la espera de la condition variable libera y vuelve a adquirir el mutex de forma automática, lo que permite al thread en espera observar el predicado bajo el lock, liberar el lock durante el sueño y recuperarlo al despertar.

El desajuste aparece cuando la invariante que proteges no se describe mejor como "como máximo uno" o "espera a un predicado". Hay un puñado de formas comunes que se repiten.

**Admisión acotada.** La invariante es "como máximo N de algo a la vez". Para N igual a uno, un mutex es lo natural. Para N mayor que uno, la versión mutex-más-cv-más-contador requiere que escribas un contador explícito, lo compruebes bajo el mutex, duermas en una cv si el contador está en N, lo decrementos al entrar, vuelvas a señalizar al salir y redescubras la política de despertar correcta. La primitiva semáforo expresa la misma invariante en tres llamadas: `sema_init(&s, N, ...)`, `sema_wait(&s)` al entrar, `sema_post(&s)` al salir.

**Estado de lectura predominante con promoción ocasional.** La invariante es "muchos lectores concurrentemente, o un escritor; cuando un lector detecta la necesidad de escribir, promuévelo". El lock `sx(9)` maneja de forma nativa la parte de muchos-lectores-o-un-escritor. La parte de promoción (`sx_try_upgrade`) es una primitiva que una versión mutex-más-cv tiene que simular con un contador similar a rwlock y lógica de reintento.

**Predicado que debe sobrevivir a la interrupción por señal con preservación de progreso parcial.** Un `read(2)` que copió la mitad de los bytes solicitados y ahora está durmiendo esperando más debería, al recibir una señal, devolver los bytes ya copiados en lugar de EINTR. `cv_timedwait_sig` te proporciona la distinción entre EINTR y ERESTART; escribir el equivalente con `cv_wait` directamente más una comprobación periódica de señales es posible pero propenso a errores.

**Coordinación del apagado entre componentes.** Varias partes del driver (callouts, tareas, threads de usuario) necesitan observar de forma coherente que "el driver está cerrándose". Un flag protegido por mutex es una opción. Un flag atómico con una barrera seq-cst en el escritor y cargas acquire en los lectores suele ser más económico y más claro para este patrón específico, y el capítulo mostrará cuándo elegir cada uno.

**Reintentos con limitación de tasa.** "Haz esto como máximo una vez cada 100 ms, sáltalo si ya está en curso." Se puede expresar con un mutex y un temporizador, pero una taskqueue más una tarea de timeout más un test-and-set atómico sobre un flag de "ya programado" suele ser más limpio. Este patrón apareció al final del Capítulo 14; el Capítulo 15 lo refina.

Para cada forma, el Capítulo 15 elige la primitiva adecuada y muestra la refactorización en paralelo. El objetivo no es argumentar que el semáforo, la promoción sx o el flag atómico son "mejores". El objetivo es permitirte elegir la herramienta que se ajusta al problema, de modo que tu driver resulte fácil de leer para la siguiente persona que lo abra.

### Un ejemplo motivador concreto: demasiados escritores

Un ejemplo motivador para concretar el desajuste. Supón que el driver quiere limitar el número de escritores concurrentes. Por "escritores concurrentes" se entiende los threads de usuario que están simultáneamente dentro del manejador `myfirst_write` pasada la validación inicial. El límite es un entero pequeño, digamos cuatro, expuesto como parámetro de ajuste sysctl.

La versión mutex-más-contador tiene esta apariencia:

```c
/* In the softc: */
int writers_active;
int writers_limit;   /* Configurable via sysctl. */
struct cv writer_cv;

/* In myfirst_write, at entry: */
MYFIRST_LOCK(sc);
while (sc->writers_active >= sc->writers_limit) {
        int error = cv_wait_sig(&sc->writer_cv, &sc->mtx);
        if (error != 0) {
                MYFIRST_UNLOCK(sc);
                return (error);
        }
        if (!sc->is_attached) {
                MYFIRST_UNLOCK(sc);
                return (ENXIO);
        }
}
sc->writers_active++;
MYFIRST_UNLOCK(sc);

/* At exit: */
MYFIRST_LOCK(sc);
sc->writers_active--;
cv_signal(&sc->writer_cv);
MYFIRST_UNLOCK(sc);
```

Cada línea es necesaria. El bucle gestiona los despertares espurios y los retornos por señal. La comprobación de señal preserva el progreso parcial (si lo hay). La comprobación de `is_attached` garantiza que no continuemos tras el detach. La `cv_signal` despierta al siguiente thread en espera. El llamador debe recordar decrementar el contador.

La versión con semáforo tiene esta apariencia:

```c
/* In the softc: */
struct sema writers_sema;

/* In attach: */
sema_init(&sc->writers_sema, 4, "myfirst writers");

/* In destroy: */
sema_destroy(&sc->writers_sema);

/* In myfirst_write, at entry: */
sema_wait(&sc->writers_sema);
if (!sc->is_attached) {
        sema_post(&sc->writers_sema);
        return (ENXIO);
}

/* At exit: */
sema_post(&sc->writers_sema);
```

Cinco líneas de lógica en tiempo de ejecución, incluida la comprobación de conexión. La primitiva expresa la invariante directamente. Un lector que ve `sema_wait(&sc->writers_sema)` entiende la intención de un vistazo.

Observa lo que la versión con semáforo sacrifica. `sema_wait` no es interrumpible por señales (como veremos en la Sección 2, el `sema_wait` de FreeBSD utiliza `cv_wait` internamente, no `cv_wait_sig`). Si necesitas interrumpibilidad, debes volver a la versión mutex-más-cv o combinar `sema_trywait` con una espera interrumpible separada. Cada primitiva tiene sus compromisos; la Sección 2 los enumera.

El punto más importante es que ninguna versión es "incorrecta". La versión mutex-más-contador es correcta y se ha utilizado en drivers durante décadas. La versión con semáforo es correcta y más clara para esta invariante específica. Conocer ambas te permite elegir la adecuada según las restricciones concretas de cada caso.

### El resto de la sección anticipa el capítulo

La Sección 1 es deliberadamente breve. El resto del capítulo desarrolla cada forma en su propia sección con su propia refactorización del driver `myfirst`:

- La Sección 2 realiza la refactorización del semáforo de límite de escritores como Etapa 1.
- La Sección 3 realiza el refinamiento sx de lectura predominante como Etapa 2.
- La Sección 4 realiza el refinamiento de espera interrumpible como Etapa 3.
- La Sección 5 realiza el handshake entre componentes como parte de la Etapa 4.
- La Sección 6 extrae el vocabulario de sincronización en `myfirst_sync.h`.
- La Sección 7 escribe las pruebas de estrés.
- La Sección 8 lo integra todo y lanza `0.9-coordination`.

Antes de entrar en materia, una observación general. Los cambios del Capítulo 15 son pequeños en líneas de código. El capítulo completo probablemente añade menos de doscientas líneas al driver. Lo que aporta en cuanto al modelo mental es mayor. Cada primitiva que introducimos expresa una invariante que estaba implícita en el driver del Capítulo 14; hacerla explícita es la mayor parte del valor.

### Cerrando la Sección 1

Un mutex y una condition variable cubren la mayor parte de la sincronización en drivers. Cuando la invariante es "como máximo N", "muchos lectores o un escritor con promoción ocasional", "espera interrumpible con progreso parcial", "apagado entre componentes" o "reintentos con limitación de tasa", otra primitiva expresa la intención de forma más directa y deja menos margen para los errores. La Sección 2 introduce la primera de esas primitivas: el semáforo contable.



## Sección 2: Uso de semáforos en el kernel de FreeBSD

Un semáforo contable es una primitiva sencilla. Internamente es un contador, un mutex y una condition variable; la API envuelve esos tres elementos en operaciones que exponen la semántica de contador-y-espera-a-positivo como su interfaz principal. El semáforo del kernel de FreeBSD reside en `/usr/src/sys/sys/sema.h` y `/usr/src/sys/kern/kern_sema.c`. La implementación completa tiene menos de doscientas líneas. Leerla una vez es la forma más rápida de entender qué garantiza la API.

Esta sección cubre la API en profundidad, compara los semáforos con los mutexes y las condition variables, recorre la refactorización del límite de escritores como Etapa 1 del driver del Capítulo 15 y enumera los compromisos que conlleva la primitiva.

### El semáforo contable, con precisión

Un semáforo contable mantiene un entero no negativo. La API expone dos operaciones fundamentales:

- `sema_post(&s)` incrementa el contador. Si alguien estaba esperando porque el contador era cero, uno de ellos es despertado.
- `sema_wait(&s)` decrementa el contador si es positivo. Si el contador es cero, el llamador duerme hasta que `sema_post` lo incrementa, luego decrementa y retorna.

Esas dos operaciones, combinadas, te proporcionan admisión acotada. Inicializa el semáforo con N. Cada participante llama a `sema_wait` al entrar y a `sema_post` al salir. La invariante "como máximo N participantes están entre su wait y su post" se preserva automáticamente.

El semáforo contable de FreeBSD se diferencia de un semáforo binario (que solo puede ser 0 o 1) en que el contador puede superar 1. Un semáforo binario es efectivamente un mutex, con una diferencia importante: un semáforo no tiene concepto de propiedad. Cualquier thread puede llamar a `sema_post`; cualquier thread puede llamar a `sema_wait`. Un mutex, en cambio, debe ser liberado por el mismo thread que lo adquirió. Esta ausencia de propiedad es importante precisamente para los casos de uso en los que los semáforos destacan: un productor que hace post y un consumidor que espera, que pueden ser threads distintos.

### La estructura de datos

La estructura de datos, de `/usr/src/sys/sys/sema.h`:

```c
struct sema {
        struct mtx      sema_mtx;       /* General protection lock. */
        struct cv       sema_cv;        /* Waiters. */
        int             sema_waiters;   /* Number of waiters. */
        int             sema_value;     /* Semaphore value. */
};
```

Cuatro campos. `sema_mtx` es el mutex interno propio del semáforo. `sema_cv` es la condition variable en la que bloquean los threads en espera. `sema_waiters` cuenta el número de threads actualmente bloqueados en espera (con fines de diagnóstico y para evitar broadcasts innecesarios). `sema_value` es el contador en sí.

Nunca accedes a estos campos directamente. La API es el contrato; la estructura se muestra aquí una vez para que puedas visualizar qué es la primitiva.

### La API

De `/usr/src/sys/sys/sema.h`:

```c
void sema_init(struct sema *sema, int value, const char *description);
void sema_destroy(struct sema *sema);
void sema_post(struct sema *sema);
void sema_wait(struct sema *sema);
int  sema_timedwait(struct sema *sema, int timo);
int  sema_trywait(struct sema *sema);
int  sema_value(struct sema *sema);
```

**`sema_init`**: inicializa el semáforo con el valor inicial dado y una descripción legible por humanos. La descripción es utilizada por las herramientas de rastreo del kernel. El valor debe ser no negativo; `sema_init` lo verifica con `KASSERT`.

**`sema_destroy`**: destruye el semáforo. Debes asegurarte de que no haya threads en espera cuando llames a `sema_destroy`; la implementación lo verifica con una aserción. Normalmente lo garantizas por diseño: la destrucción ocurre en detach, después de que todos los caminos que podían llamar a `sema_wait` hayan sido silenciados.

**`sema_post`**: incrementa el contador. Si hay threads en espera, despierta a uno de ellos. Siempre tiene éxito.

**`sema_wait`**: si el contador es positivo, lo decrementa y retorna. En caso contrario, duerme en la cv interna hasta que `sema_post` incrementa el contador, momento en el que lo decrementa y retorna. **`sema_wait` no es interrumpible por señales**; usa `cv_wait` internamente, no `cv_wait_sig`. Una señal no despertará al proceso en espera. Si necesitas interrumpibilidad, `sema_wait` no es la herramienta adecuada; usa directamente un patrón mutex más cv.

**`sema_timedwait`**: igual que `sema_wait` pero acotado por `timo` ticks. Retorna 0 en caso de éxito (el valor fue decrementado), `EWOULDBLOCK` si se agota el tiempo de espera. Internamente usa `cv_timedwait`, por lo que tampoco es interrumpible por señales.

**`sema_trywait`**: variante no bloqueante. Retorna 1 si el valor fue decrementado con éxito, 0 si el valor ya era cero. Conviene fijarse en la convención inusual: 1 significa éxito, 0 significa fallo. La mayoría de las APIs del kernel de FreeBSD retornan 0 en caso de éxito; `sema_trywait` es una excepción. Ten cuidado al leer o escribir código que lo utilice.

**`sema_value`**: retorna el valor actual del contador. Resulta útil para diagnósticos, pero no para tomar decisiones de sincronización, ya que el valor puede cambiar inmediatamente después de que la llamada retorne.

### Qué no es un semáforo

Tres propiedades que el semáforo del kernel de FreeBSD no tiene. Cada una es importante.

**Sin herencia de prioridad.** El comentario al inicio de `/usr/src/sys/kern/kern_sema.c` es explícito:

> Priority propagation will not generally raise the priority of semaphore "owners" (a misnomer in the context of semaphores), so should not be relied upon in combination with semaphores.

Si estás protegiendo un recurso y un thread de alta prioridad espera en un semáforo que mantiene un thread de baja prioridad, este último no hereda la prioridad alta. Esto es consecuencia del diseño sin propietario: no existe ningún «poseedor» al que elevar la prioridad. Para recursos en los que la herencia de prioridad sea importante, usa en su lugar un mutex o un lock de `lockmgr(9)`.

**No interrumpible por señales.** `sema_wait` y `sema_timedwait` no se interrumpen por señales. Una llamada a `read(2)` o `write(2)` que bloquee en `sema_wait` no devolverá EINTR ni ERESTART cuando el usuario envíe SIGINT. Si tu syscall necesita responder a señales, no puedes bloquear incondicionalmente en `sema_wait`. Las dos soluciones habituales: estructurar la espera como `sema_trywait` más un sleep interrumpible sobre un cv separado, o mantener `sema_wait` pero lograr que el productor (el código que llama a `sema_post`) también publique cuando el apagado esté en marcha.

**Sin propietario.** Cualquier thread puede publicar; cualquier thread puede esperar. Esto es una característica, no un defecto, para el patrón productor-consumidor en el que un thread señaliza la finalización y otro la espera. Resulta sorprendente si esperabas la semántica de propietario propia de un mutex.

Saber qué no es una primitiva es tan importante como saber qué es. El semáforo del kernel de FreeBSD es una herramienta pequeña y específica. Úsalo donde encaje; recurre a otras primitivas donde no lo haga.

### Un ejemplo real: Hyper-V storvsc

Antes de la refactorización del driver, echemos un vistazo a un driver real de FreeBSD que hace un uso intensivo de `sema(9)`. El driver de almacenamiento de Hyper-V se encuentra en `/usr/src/sys/dev/hyperv/storvsc/hv_storvsc_drv_freebsd.c`. Usa semáforos por petición para bloquear un thread que espera la finalización del hardware. El patrón:

```c
/* In the request submission path: */
sema_init(&request->synch_sema, 0, "stor_synch_sema");
/* ... send command to hypervisor ... */
sema_wait(&request->synch_sema);
/* At this point the completion handler has posted; work is done. */
sema_destroy(&request->synch_sema);
```

Y en el callback de finalización (ejecutado desde un contexto diferente):

```c
sema_post(&request->synch_sema);
```

El semáforo se inicializa a cero, por lo que `sema_wait` bloquea. Cuando el hardware finaliza y se ejecuta el manejador de finalización del driver, este publica, y el thread que realizó la petición se desbloquea. La naturaleza sin propietario del semáforo es precisamente lo que hace que este patrón funcione: el thread que publica (el manejador de finalización) es distinto del que espera.

El mismo driver usa un segundo semáforo (`hs_drain_sema`) para coordinar el vaciado durante el apagado. La ruta de apagado espera en el semáforo; la ruta de finalización de peticiones publica cuando todas las peticiones pendientes han terminado.

Estos patrones no son invenciones. Son los usos canónicos de `sema(9)` en el árbol de FreeBSD. La refactorización del Capítulo 15 usa una variación para el invariante «como máximo N escritores». La idea subyacente es la misma.

### La refactorización con límite de escritores: Etapa 1

El primer cambio del Capítulo 15 al driver añade un semáforo contador que limita el número de llamadas concurrentes a `myfirst_write`. El límite es configurable mediante un sysctl, con un valor predeterminado de 4.

El cambio no trata sobre rendimiento. El driver ya puede manejar muchos escritores concurrentes; el cbuf está protegido por el mutex y las escrituras se serializan de todos modos. El cambio trata de expresar el invariante «como máximo N escritores» como una primitiva de primera clase. Un driver real podría usar este patrón por razones más sustanciales (un pool de descriptores DMA de tamaño fijo, una cola de comandos de hardware con profundidad limitada, un dispositivo serie con una ventana de transmisión); la refactorización es un vehículo didáctico para aprender la primitiva en un contexto que puedes ejecutar y observar.

### La adición al softc

Añade tres miembros a `struct myfirst_softc`:

```c
struct sema     writers_sema;
int             writers_limit;              /* Current configured limit. */
int             writers_trywait_failures;   /* Diagnostic counter. */
```

`writers_sema` es el semáforo en sí. `writers_limit` registra el valor configurado actualmente para que el manejador del sysctl pueda detectar cambios. `writers_trywait_failures` cuenta el número de veces que un escritor intentó entrar, no pudo y devolvió EAGAIN (para aperturas con `O_NONBLOCK`) o EWOULDBLOCK (para esperas acotadas).

### Inicialización y destrucción del semáforo

En `myfirst_attach`, antes de cualquier código que pudiera llamar a `sema_wait` (normalmente junto con las otras llamadas a `sema_init`/`cv_init` al comienzo del attach):

```c
sema_init(&sc->writers_sema, 4, "myfirst writers");
sc->writers_limit = 4;
sc->writers_trywait_failures = 0;
```

El valor inicial de 4 coincide con el límite predeterminado. Si más adelante aumentamos el límite dinámicamente, ajustaremos el valor del semáforo para que coincida; la Sección 2 muestra cómo.

En `myfirst_detach`, después de que toda ruta que pudiera llamar a `sema_wait` haya quedado inactiva (lo que, en la Etapa 1, significa después de que `is_attached` se haya borrado y todas las syscalls de usuario hayan retornado o fallado con ENXIO):

```c
sema_destroy(&sc->writers_sema);
```

Aquí hay un detalle sutil, y genuinamente complicado, que merece detenerse. `sema_destroy` comprueba que no haya ningún waiter presente; y, lo que es más importante, a continuación llama a `mtx_destroy` sobre el mutex interno del semáforo y a `cv_destroy` sobre su cv interno. Si algún thread sigue ejecutándose dentro de alguna función `sema_*`, ese thread puede estar a punto de volver a adquirir el mutex interno cuando `mtx_destroy` se adelanta y lo libera. Eso es un use-after-free, no simplemente un fallo de aserción.

La solución ingenua «publica simplemente `writers_limit` slots para despertar a los waiters bloqueados y luego destruye» es *casi* correcta, pero tiene una condición de carrera real. Un thread despertado retorna de `cv_wait` con el `sema_mtx` interno retenido, y luego necesita ejecutar `sema_waiters--` y el `mtx_unlock` final. Si el thread de detach ejecuta `sema_destroy` antes de que el thread despertado llegue a su unlock final, el mutex interno se destruye bajo sus pies.

En la práctica, esa ventana es corta (el thread despertado suele ejecutarse en microsegundos tras `cv_signal`), pero la corrección exige que no podamos confiar en «suele funcionar». La solución es una pequeña extensión: rastrear cada thread que pueda estar actualmente dentro del código `sema_*` y esperar a que ese contador llegue a cero antes de llamar a `sema_destroy`.

Añadimos `sc->writers_inflight`, un int que el driver trata como atómico. La ruta de escritura lo incrementa antes de llamar a `sema_wait` y lo decrementa después de llamar al `sema_post` correspondiente. La ruta de detach, tras publicar los slots de despertar, espera a que el contador llegue a cero:

```c
/* In the write path, early: */
atomic_add_int(&sc->writers_inflight, 1);
if (!sc->is_attached) {
        atomic_subtract_int(&sc->writers_inflight, 1);
        return (ENXIO);
}
... sema_wait / work / sema_post ...
atomic_subtract_int(&sc->writers_inflight, 1);

/* In detach, after the posts: */
while (atomic_load_acq_int(&sc->writers_inflight) > 0)
        pause("myfwrd", 1);
sema_destroy(&sc->writers_sema);
```

Por qué funciona esto: cualquier thread que pudiera estar usando el estado interno del semáforo ha sido contabilizado. El detach espera hasta que todos los threads contabilizados hayan terminado su `sema_post` final, que en el momento en que se ejecuta el decremento ya ha retornado de todas las funciones `sema_*`. Ningún thread retiene ni está a punto de adquirir el mutex interno cuando se ejecuta `sema_destroy`.

El patrón merece recordarse porque es general: cualquier primitiva externa cuya destrucción compita con llamadas en vuelo puede vaciarse de la misma manera. `sema(9)` es el ejemplo inmediato; verás variantes de este contador en drivers reales siempre que una primitiva sin vaciado incorporado necesite desmontarse de forma limpia.

Los drivers de la Etapa 1 a la Etapa 4 del capítulo implementan todos este patrón. La Sección 6 encapsula la lógica en `myfirst_sync_writer_enter`/`myfirst_sync_writer_leave` para que los puntos de llamada sean legibles; la contabilidad de vuelo se oculta en el envoltorio.

### Uso del semáforo en la ruta de escritura

Añade `sema_wait`/`sema_post` alrededor del cuerpo de `myfirst_write`:

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        char bounce[MYFIRST_BOUNCE];
        size_t want, put, room;
        ssize_t nbefore;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        /* Chapter 15: enforce the writer cap. */
        if (ioflag & IO_NDELAY) {
                if (!sema_trywait(&sc->writers_sema)) {
                        MYFIRST_LOCK(sc);
                        sc->writers_trywait_failures++;
                        MYFIRST_UNLOCK(sc);
                        return (EAGAIN);
                }
        } else {
                sema_wait(&sc->writers_sema);
        }
        if (!sc->is_attached) {
                sema_post(&sc->writers_sema);
                return (ENXIO);
        }

        nbefore = uio->uio_resid;
        while (uio->uio_resid > 0) {
                /* ... same body as Chapter 14 ... */
        }

        sema_post(&sc->writers_sema);
        return (0);
}
```

Hay varias cosas que observar.

El caso `IO_NDELAY` (no bloqueante) usa `sema_trywait`, que devuelve 1 en caso de éxito y 0 en caso de fallo. Observa la convención invertida: `if (!sema_trywait(...))` significa «si no pudimos adquirir». Los principiantes lo pasan por alto con frecuencia; lee el valor de retorno con cuidado en cada ocasión.

Cuando `sema_trywait` falla, el llamador no bloqueante recibe EAGAIN. Un contador de diagnóstico se incrementa bajo el mutex (adquisición y liberación breve del mutex, sin relación con el semáforo).

El caso bloqueante usa `sema_wait`. No es interrumpible por señales, por lo que una llamada bloqueante a `write(2)` que espere el semáforo no puede interrumpirse con SIGINT. Esta es una propiedad importante; los usuarios deben conocerla. Para el driver actual, el semáforo rara vez está en disputa en la práctica (el límite predeterminado de 4 es generoso), por lo que la preocupación por la interrumpibilidad es en gran medida teórica. Si el límite fuera 1 y los escritores se pusieran en cola de verdad, quizás querrías replantearte el uso de un semáforo aquí y usar en su lugar una primitiva interrumpible. La Sección 4 vuelve sobre esta compensación.

Después de que el wait retorne, comprobamos `is_attached`. Si el detach ocurrió mientras estábamos bloqueados, no debemos proceder con la escritura; publicamos el semáforo (restaurando el contador) y devolvemos ENXIO.

El `sema_post` en la ruta de salida se ejecuta en todo camino exitoso. Un error habitual es olvidarlo en un retorno temprano (por ejemplo, si falla una validación intermedia). La disciplina habitual es hacer el post incondicional mediante un patrón de limpieza: adquirir, y luego todos los retornos subsiguientes pasan por una limpieza común.

### El manejador sysctl para el límite

Los usuarios del driver pueden querer ajustar el límite de escritores en tiempo de ejecución. El manejador sysctl:

```c
static int
myfirst_sysctl_writers_limit(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, old, error, delta;

        old = sc->writers_limit;
        new = old;
        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);
        if (new < 1 || new > 64)
                return (EINVAL);

        MYFIRST_LOCK(sc);
        delta = new - sc->writers_limit;
        sc->writers_limit = new;
        MYFIRST_UNLOCK(sc);

        if (delta > 0) {
                /* Raised the limit: post the extra slots. */
                int i;
                for (i = 0; i < delta; i++)
                        sema_post(&sc->writers_sema);
        }
        /*
         * Lowering is best-effort: we cannot reclaim posted slots from
         * threads already in the write path. New entries will observe
         * the lower limit once the counter drains below the new cap.
         */
        return (0);
}
```

Detalles interesantes.

Aumentar el límite requiere publicar slots adicionales en el semáforo. Si el límite anterior era 4 y el nuevo es 6, necesitamos publicar dos veces para que dos escritores más puedan entrar simultáneamente.

Reducir el límite es más difícil. Un semáforo no tiene forma de «consumir» de vuelta los slots en exceso. Si el contador actual es 4 y queremos un límite de 2, no podemos reducir el contador excepto esperando a que los escritores entren y no publicando a su salida. Eso es complicado y raramente justifica el código extra. En su lugar, el enfoque simple: bajar el campo `writers_limit` y dejar que el semáforo se vacíe de forma natural hasta el nuevo nivel a medida que los escritores entren sin reemplazo. El comentario del manejador sysctl documenta este comportamiento.

El mutex se retiene solo para la lectura/escritura de `writers_limit`, no para el bucle de sema_post. Tomar el mutex alrededor de `sema_post` sería incorrecto de todos modos: `sema_post` adquiere su propio mutex interno, y estaríamos introduciendo un orden de locks `sc->mtx -> sc->writers_sema.sema_mtx` que nada más usa. Dado que `writers_limit` es el único campo que realmente protegemos, la ventana del mutex es pequeña.

### Observación del efecto

Con la Etapa 1 cargada, algunos experimentos.

Inicia muchos escritores concurrentes con un pequeño bucle de shell:

```text
# for i in 1 2 3 4 5 6 7 8; do
    (yes "writer-$i" | dd of=/dev/myfirst bs=512 count=100 2>/dev/null) &
done
```

Ocho escritores se inician simultáneamente. Con `writers_limit=4` (el valor predeterminado), cuatro entran en el bucle de escritura y los otros cuatro bloquean en `sema_wait`. Cuando uno termina y llama a `sema_post`, uno bloqueado se despierta. El rendimiento es ligeramente inferior al sin restricciones (porque solo cuatro escritores progresan activamente en cualquier momento), pero el cbuf nunca tiene más de cuatro escritores compitiendo por el mutex.

Observa el valor del semáforo en tiempo real:

```text
# sysctl dev.myfirst.0.stats.writers_sema_value
dev.myfirst.0.stats.writers_sema_value: 0
```

Durante la prueba de estrés, el valor debería estar cerca de cero. Cuando no hay escritores, debería ser igual a `writers_limit`.

Ajusta el límite dinámicamente:

```text
# sysctl dev.myfirst.0.writers_limit=2
```

Vuelve a ejecutar la prueba de estrés con ocho escritores. Dos escritores progresan; seis bloquean. El rendimiento cae en consecuencia.

Sube el límite de nuevo:

```text
# sysctl dev.myfirst.0.writers_limit=8
```

Los ocho escritores proceden de forma concurrente.

Comprueba el contador de fallos de trywait usando escritores no bloqueantes (mediante `open` con `O_NONBLOCK`):

```text
# ./nonblock_writer_stress.sh
# sysctl dev.myfirst.0.stats.writers_trywait_failures
```

El contador crece cada vez que un escritor no bloqueante es rechazado porque el semáforo estaba a cero.

### Errores habituales

Una breve lista de errores que cometen los principiantes con `sema(9)`. Cada uno ha afectado a drivers reales; cada uno tiene una regla sencilla.

**Olvidar el `sema_post` en una ruta de error.** Si la ruta de escritura tiene un `return (error)` que omite el `sema_post`, el semáforo pierde un slot. Tras suficientes pérdidas, el semáforo queda permanentemente a cero y todos los escritores bloquean. La solución es o bien colocar el `sema_post` en un único bloque de limpieza por el que pasen todas las salidas, o auditar cada sentencia return para confirmar que publica.

**`sema_wait` en un contexto que no puede dormir.** `sema_wait` bloquea. No puede llamarse desde un callback de callout, un filtro de interrupción ni ningún otro contexto que no pueda dormir. La aserción de `WITNESS` detecta esto en un kernel de depuración; el kernel de producción puede caer en deadlock o en pánico sin aviso.

**Destruir un semáforo con hilos en espera.** `sema_destroy` verifica mediante una aserción que no haya hilos en espera. En el detach de un driver, lo correcto es agotar todos los caminos que pudieran bloquearse en espera antes de destruir el semáforo. Si el orden de detach es incorrecto (se destruye antes de que los hilos en espera hayan despertado), la aserción se dispara en un kernel de depuración y la destrucción corrompe silenciosamente el estado en un kernel de producción.

**Usar `sema_wait` cuando se necesita interrumpibilidad por señales.** Los usuarios esperan que `read(2)` y `write(2)` respondan a SIGINT. Si la syscall se bloquea en `sema_wait`, esto no ocurre. Elige una primitiva diferente o estructura el código de forma que `sema_wait` sea lo suficientemente breve como para que la latencia de las señales resulte aceptable.

**Confundir el valor de retorno de `sema_trywait`.** Devuelve 1 en caso de éxito y 0 en caso de fallo. La mayoría de las APIs del kernel de FreeBSD devuelven 0 en caso de éxito. Una lectura incorrecta del valor de retorno produce el comportamiento contrario al esperado. Comprueba siempre este detalle con cuidado.

**Asumir herencia de prioridad.** Si la invariante requiere que un hilo en espera de alta prioridad eleve la prioridad efectiva del thread que realizará el post, `sema(9)` no lo hará. Usa en su lugar un mutex o un lock de `lockmgr(9)`.

### Una nota sobre cuándo no usar semáforos

A modo de complemento, una lista breve de situaciones en las que `sema(9)` no es la herramienta adecuada.

- **Cuando la invariante es "propiedad exclusiva de un recurso".** Eso es un mutex. Un semáforo inicializado a 1 lo aproxima, pero pierde la semántica de propiedad y la herencia de prioridad.
- **Cuando el hilo en espera debe ser interrumpible por señales.** Usa `cv_wait_sig` o `cv_timedwait_sig` con tu propio contador.
- **Cuando el trabajo es breve y la contención es alta.** El mutex interno del semáforo es un punto único de serialización. En secciones críticas muy cortas, el coste puede dominar.
- **Cuando se requiere herencia de prioridad.** Usa un mutex o `lockmgr(9)`.
- **Cuando necesitas algo más que contar.** Si la invariante es «espera hasta que se cumpla este predicado complejo específico», la herramienta adecuada es un mutex y una variable de condición que compruebe el predicado.

Para el caso de uso del límite de escritores del driver, ninguna de estas contraindicaciones aplica. El semáforo es la herramienta adecuada, el refactor es pequeño y el código resultante es legible. La etapa 1 del driver del capítulo 15 conserva el nuevo vocabulario y continúa.

### Cerrando la sección 2

Un semáforo de conteo es un contador, un mutex y una variable de condición envueltos en una API pequeña. `sema_init`, `sema_wait`, `sema_post`, `sema_trywait`, `sema_timedwait`, `sema_value` y `sema_destroy` cubren toda la superficie. La primitiva es ideal para la admisión acotada y para patrones productor-consumidor de completitud en los que el productor y el consumidor son threads distintos. Carece de herencia de prioridad, interruptibilidad por señales y propiedad, y esas limitaciones son reales. La etapa 1 del driver del capítulo 15 aplicó un semáforo de límite de escritores; la siguiente sección aplica un refinamiento de sx con lectura mayoritaria.



## Sección 3: Escenarios de lectura mayoritaria y acceso compartido

El sx lock del capítulo 12 ya está en el driver. `sc->cfg_sx` protege la estructura `myfirst_config`, y los sysctls de configuración lo adquieren en modo compartido para lecturas y en modo exclusivo para escrituras. Ese patrón es correcto y, para el caso de uso de configuración, suficiente. Esta sección refina el patrón sx para cubrir una forma ligeramente diferente: una caché de lectura mayoritaria en la que los lectores detectan ocasionalmente que la caché necesita actualizarse y deben promover brevemente a escritor.

Esta sección también presenta un pequeño número de operaciones sx que el driver no ha utilizado todavía: `sx_try_upgrade`, `sx_downgrade`, `sx_xlocked` y algunas macros de introspección. El refactor del driver de la etapa 2 añade una pequeña caché de estadísticas protegida por su propio sx y utiliza el patrón de promoción para refrescar la caché bajo contención ligera.

### El problema de la caché de lectura mayoritaria

Un problema motivador concreto para el refactor de la etapa 2. Supón que el driver quiere exponer una estadística calculada: «bytes promedio escritos por segundo en los últimos 10 segundos». La estadística es costosa de calcular (requiere recorrer un buffer de historial por segundo) y se lee con frecuencia (en cada lectura de sysctl, en cada línea de log de heartbeat). Una implementación ingenua recalcula en cada lectura. Una implementación mejor almacena el resultado en caché e invalida la caché periódicamente.

La caché tiene tres propiedades:

1. Las lecturas superan ampliamente a las escrituras. Cualquier número de threads puede leer simultáneamente; solo el refresco ocasional de la caché necesita escribir.
2. Los lectores detectan a veces que la caché está obsoleta. Cuando eso ocurre, el lector quiere promover brevemente a escritor, refrescar la caché y volver a leer.
3. Refrescar la caché lleva unos pocos microsegundos. Un lector que promueve y refresca sigue queriendo liberar el lock exclusivo con rapidez.

El sx lock gestiona las propiedades 1 y 3 de forma nativa: muchos lectores pueden mantener `sx_slock` simultáneamente; un escritor con `sx_xlock` excluye a los lectores. La propiedad 2 requiere `sx_try_upgrade`.

### `sx_try_upgrade` y `sx_downgrade`

Dos operaciones sobre el sx lock que el capítulo 12 no presentó.

`sx_try_upgrade(&sx)` intenta promover atómicamente un lock compartido a un lock exclusivo. Devuelve un valor distinto de cero en caso de éxito, y cero en caso de fallo. Un fallo significa que otro thread también mantiene el lock compartido (el estado exclusivo-con-otros-lectores no es representable; la promoción solo puede tener éxito si el thread que llama es el único titular compartido). En caso de éxito, el lock compartido desaparece y el llamador ahora mantiene el lock exclusivo.

`sx_downgrade(&sx)` degrada atómicamente un lock exclusivo a un lock compartido. Siempre tiene éxito. El titular exclusivo se convierte en titular compartido; otros titulares compartidos pueden unirse entonces.

El patrón para lectura con promoción ocasional:

```c
sx_slock(&sx);
if (cache_stale(&cache)) {
        if (sx_try_upgrade(&sx)) {
                /* Promoted to exclusive. */
                refresh_cache(&cache);
                sx_downgrade(&sx);
        } else {
                /*
                 * Upgrade failed: another reader holds the lock.
                 * Release the shared lock, take the exclusive lock,
                 * refresh, downgrade.
                 */
                sx_sunlock(&sx);
                sx_xlock(&sx);
                if (cache_stale(&cache))
                        refresh_cache(&cache);
                sx_downgrade(&sx);
        }
}
use_cache(&cache);
sx_sunlock(&sx);
```

Tres cosas que observar.

El camino feliz es el éxito de `sx_try_upgrade`. La promoción es atómica: en ningún momento se libera y se vuelve a adquirir el lock, por lo que ningún otro escritor puede colarse en medio. Para una carga de trabajo de lectura mayoritaria en la que los lectores rara vez compiten entre sí, este camino domina.

El camino alternativo cuando `sx_try_upgrade` falla suelta completamente el lock compartido, adquiere el lock exclusivo desde cero y vuelve a comprobar el predicado de obsolescencia. La nueva comprobación es esencial: entre soltar el lock compartido y adquirir el lock exclusivo, otro thread puede haber refrescado la caché. Sin la nueva comprobación, se refrescaría de forma redundante.

El `sx_sunlock` final tras `sx_downgrade` es siempre correcto porque el estado degradado es compartido.

Este patrón es sorprendentemente habitual en el árbol de código fuente de FreeBSD. Busca `sx_try_upgrade` bajo `/usr/src/sys/` y lo encontrarás en varios subsistemas, incluidos VFS y las actualizaciones de la tabla de enrutamiento.

### Una aplicación práctica: el driver de la etapa 2

La etapa 2 del driver del capítulo 15 añade una pequeña caché de estadísticas protegida por su propio sx lock. La caché contiene un único entero, «bytes escritos en los últimos 10 segundos, en el momento del último refresco», y una marca de tiempo que registra cuándo se refrescó la caché por última vez.

La adición al softc:

```c
struct sx       stats_cache_sx;
uint64_t        stats_cache_bytes_10s;
uint64_t        stats_cache_last_refresh_ticks;
```

La validez de la caché se basa en una marca de tiempo. Si el `ticks` actual difiere de `stats_cache_last_refresh_ticks` en más de `hz` (un segundo de ticks), la caché se considera obsoleta. Cualquier lectura de sysctl del valor almacenado en caché desencadena una comprobación de obsolescencia; si está obsoleta, el lector promueve y refresca.

### La función de refresco de la caché

La función de refresco es trivial en la versión didáctica: simplemente lee el contador actual y registra el tiempo actual.

```c
static void
myfirst_stats_cache_refresh(struct myfirst_softc *sc)
{
        KASSERT(sx_xlocked(&sc->stats_cache_sx),
            ("stats cache not exclusively locked"));
        sc->stats_cache_bytes_10s = counter_u64_fetch(sc->bytes_written);
        sc->stats_cache_last_refresh_ticks = ticks;
}
```

El `KASSERT` documenta el contrato: esta función debe llamarse con el sx mantenido en modo exclusivo. Un kernel de depuración detecta las violaciones en tiempo de ejecución.

### El manejador de sysctl

El manejador de sysctl que lee el valor almacenado en caché:

```c
static int
myfirst_sysctl_stats_cached(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        uint64_t value;
        int stale;

        sx_slock(&sc->stats_cache_sx);
        stale = (ticks - sc->stats_cache_last_refresh_ticks) > hz;
        if (stale) {
                if (sx_try_upgrade(&sc->stats_cache_sx)) {
                        myfirst_stats_cache_refresh(sc);
                        sx_downgrade(&sc->stats_cache_sx);
                } else {
                        sx_sunlock(&sc->stats_cache_sx);
                        sx_xlock(&sc->stats_cache_sx);
                        if ((ticks - sc->stats_cache_last_refresh_ticks) > hz)
                                myfirst_stats_cache_refresh(sc);
                        sx_downgrade(&sc->stats_cache_sx);
                }
        }
        value = sc->stats_cache_bytes_10s;
        sx_sunlock(&sc->stats_cache_sx);

        return (sysctl_handle_64(oidp, &value, 0, req));
}
```

La forma coincide con el patrón de la subsección anterior. Vale la pena leerlo con atención una vez. La comprobación de obsolescencia ocurre dos veces en el camino alternativo: una para decidir si se debe adquirir el lock exclusivo en absoluto, y otra tras la adquisición para confirmar que la obsolescencia sigue aplicándose.

### Attach y detach

Inicializa el sx en `myfirst_attach`, junto al `cfg_sx` existente:

```c
sx_init(&sc->stats_cache_sx, "myfirst stats cache");
sc->stats_cache_bytes_10s = 0;
sc->stats_cache_last_refresh_ticks = 0;
```

Destrúyelo en `myfirst_detach`, después de desmantelar el taskqueue y el mutex (el sx está ordenado por debajo del mutex en el grafo de locks; destrúyelo después del mutex por simetría con la inicialización):

```c
sx_destroy(&sc->stats_cache_sx);
```

No debería haber ningún proceso en espera en el momento de la destrucción. Es posible que haya lectores en curso si el detach compite con un sysctl, pero el camino del detach no accede al sx de la caché, por lo que los dos no entran en conflicto directo. Si se está ejecutando un sysctl cuando el detach avanza, el framework de sysctl mantiene su propia referencia y el contexto se desmontará en orden.

### Observar el efecto

Lee la estadística almacenada en caché mil veces con rapidez:

```text
# for i in $(jot 1000 1); do
    sysctl -n dev.myfirst.0.stats.bytes_written_10s >/dev/null
done
```

La mayoría de las lecturas acceden a la caché sin promover. Solo la primera lectura tras la expiración de la caché refresca. El resultado: una contención cercana a cero en el sx de la caché de estadísticas bajo carga de lectura mayoritaria.

Observa la tasa de refresco mediante DTrace:

```text
# dtrace -n '
  fbt::myfirst_stats_cache_refresh:entry {
        @[execname] = count();
  }
' -c 'sleep 10'
```

Debería mostrar aproximadamente diez refrescos por segundo (uno por expiración de caché), independientemente de cuántas peticiones de lectura hayan llegado.

### El vocabulario de macros de sx

Algunas macros y funciones auxiliares que el capítulo no ha usado todavía y que vale la pena conocer.

`sx_xlocked(&sx)` devuelve un valor distinto de cero si el thread actual mantiene el sx en modo exclusivo. Útil dentro de aserciones. No indica si un thread diferente lo mantiene; no existe una consulta equivalente para eso.

`sx_xholder(&sx)` devuelve el puntero de thread del titular exclusivo, o NULL si nadie lo mantiene de forma exclusiva. Útil en la salida de depuración.

`sx_assert(&sx, what)` afirma una propiedad del estado del lock. `SX_LOCKED`, `SX_SLOCKED`, `SX_XLOCKED`, `SX_UNLOCKED`, `SX_XLOCKED | SX_NOTRECURSED` y otros son válidos. Produce un pánico en caso de discrepancia cuando `INVARIANTS` está activado.

Para el refactor del capítulo 15 usamos `sx_xlocked` en el KASSERT de refresco de la caché. Las demás macros están disponibles cuando las necesites.

### Compromisos y precauciones

Algunos compromisos que vale la pena mencionar.

**Los locks compartidos tienen coste.** Un sx en modo compartido sigue necesitando girar sobre el spinlock interno más un par de operaciones atómicas. En rutas extremadamente calientes (decenas de millones de operaciones por segundo), esto puede ser medible. `atomic(9)` con una barrera seq-cst es a veces más barato. Para las cargas de trabajo del driver, sx es adecuado.

**Los fallos de promoción son una posibilidad real.** Una carga de trabajo con muchos lectores concurrentes verá fallar `sx_try_upgrade` con frecuencia. El camino alternativo (soltar el compartido, adquirir el exclusivo, volver a comprobar) hace lo correcto pero tiene una latencia ligeramente mayor. Para cargas de trabajo verdaderamente de lectura mayoritaria en las que las promociones son raras, el camino de éxito domina.

**Los sx locks pueden dormir.** A diferencia de un mutex, la ruta lenta de sx bloquea. No llames a `sx_slock`, `sx_xlock`, `sx_try_upgrade` ni `sx_downgrade` desde un contexto que no pueda dormir (callouts inicializados sin un lock que pueda dormir, filtros de interrupción, etc.). El capítulo 13 explica que el antiguo flag `CALLOUT_MPSAFE` está obsoleto; la prueba moderna es si el callout se configuró mediante `callout_init(, 0)` o `callout_init_mtx(, &mtx, 0)`.

**El orden de los locks sigue siendo importante.** Añadir un sx al driver significa añadir un nuevo nodo al grafo de locks. Cada ruta de código que mantiene múltiples locks debe respetar un orden consistente. El orden final de locks del driver del capítulo 15 es `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx`; `WITNESS` lo impone.

### Cerrando la sección 3

Un sx lock cubre de forma natural el patrón de muchos-lectores-o-un-escritor. `sx_try_upgrade` y `sx_downgrade` lo amplían a lectura-mayoritaria-con-promoción. El patrón con una promoción en el camino feliz y una nueva comprobación en el camino alternativo es la forma canónica de expresar «un lector detectó la necesidad de escribir, brevemente». La etapa 2 del driver añadió una pequeña caché de estadísticas con este patrón; la etapa 3 refinará las esperas interrumpibles por señales.



## Sección 4: Variables de condición con tiempo de espera e interrupción

Los primitivos `cv_wait_sig` y `cv_timedwait_sig` ya están en el driver. El capítulo 12 los presentó; el capítulo 13 los refinó para el driver de fuente de ticks. Esta sección da el siguiente paso: distingue los valores de retorno que producen estos primitivos, muestra cómo gestionar EINTR y ERESTART correctamente, y refactoriza el camino de lectura del driver para preservar el progreso parcial a través de la interrupción por señales. Esta es la etapa 3 del driver del capítulo 15.

A diferencia de las secciones anteriores, esta no presenta un nuevo primitivo. Presenta una disciplina para usar un primitivo que ya conoces.

### Qué significan los valores de retorno

`cv_wait_sig`, `cv_timedwait_sig`, `mtx_sleep` con el flag `PCATCH` y esperas similares conscientes de señales pueden devolver varios valores:

- **0**: despertar normal. El llamador fue despertado por un `cv_signal` o `cv_broadcast` coincidente. Vuelve a comprobar el predicado; si es verdadero, continúa; si es falso, espera de nuevo.
- **EINTR**: interrumpido por una señal que tiene un manejador instalado. El llamador debe abandonar la espera, realizar la limpieza apropiada y devolver EINTR a su propio llamador.
- **ERESTART**: interrumpido por una señal cuyo manejador especifica reinicio automático. El kernel volverá a invocar el syscall. El driver debe devolver ERESTART a la capa del syscall, que se encarga del reinicio.
- **EWOULDBLOCK**: solo de esperas temporizadas. El tiempo de espera se agotó antes de que llegara ningún despertar ni señal.

La distinción entre `EINTR` y `ERESTART` importa porque el driver devuelve estos valores a través de la ruta del syscall y userland los gestiona de forma diferente:

- Si el syscall devuelve `EINTR`, el `read(2)` o `write(2)` de userland devuelve -1 con errno establecido a `EINTR`. El código de usuario que no instaló un manejador de señal `SA_RESTART` lo ve de forma explícita.
- Si el syscall devuelve `ERESTART`, la maquinaria de syscalls reinicia el syscall de forma transparente. Userland nunca percibe la entrega de la señal a este nivel; el manejador de señal se ejecutó, pero la llamada read continúa.

La consecuencia práctica: si tu `cv_wait_sig` devuelve `EINTR`, el usuario verá un `EINTR` desde su `read(2)`, y cualquier progreso parcial que pudiera esperarse debe ser explícito (por convención, el read devuelve los bytes copiados antes de la señal, no un error). Si devuelve `ERESTART`, el reinicio ocurre y el read continúa donde el kernel lo consideró oportuno.

### La convención de progreso parcial

La convención UNIX para `read(2)` y `write(2)`: si llega una señal después de haber transferido algunos datos, la syscall devuelve el número de bytes transferidos, no un error. Si no se ha transferido ningún dato, la syscall devuelve EINTR (o se reinicia, según la disposición de la señal).

Trasladando esto al driver: al entrar en la ruta de lectura, registra el `uio_resid` inicial. Cuando la espera bloqueante devuelve un error de señal, compara el `uio_resid` actual con el registrado. Si se produjo progreso, devuelve 0 (que la capa de syscall traduce como "devuelve el número de bytes copiados"). Si no se produjo progreso, devuelve el error de señal.

El driver del Capítulo 12 ya implementa esta convención para `myfirst_read` mediante la variable local `nbefore` y el truco de "devolver -1 al llamador para indicar progreso parcial". El Capítulo 15 refina el tratamiento, lo hace explícito y lo extiende a la ruta de escritura.

### La ruta de lectura refactorizada

La ruta de lectura del Capítulo 14, Etapa 4, tiene esta forma:

```c
while (uio->uio_resid > 0) {
        MYFIRST_LOCK(sc);
        error = myfirst_wait_data(sc, ioflag, nbefore, uio);
        if (error != 0) {
                MYFIRST_UNLOCK(sc);
                return (error == -1 ? 0 : error);
        }
        ...
}
```

Y `myfirst_wait_data` devuelve -1 para indicar "progreso parcial; devuelve 0 al usuario". Esa convención es correcta pero poco clara. La refactorización de la Etapa 3 sustituye el valor mágico -1 por un centinela con nombre y documenta la convención en un comentario:

```c
#define MYFIRST_WAIT_PARTIAL    (-1)    /* partial progress already made */

static int
myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error, timo;

        MYFIRST_ASSERT(sc);
        while (cbuf_used(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore) {
                        /*
                         * Some bytes already delivered on earlier loop
                         * iterations. Do not block further; return
                         * "partial progress" so the caller returns 0
                         * to the syscall layer, which surfaces the
                         * partial byte count.
                         */
                        return (MYFIRST_WAIT_PARTIAL);
                }
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
                switch (error) {
                case 0:
                        break;
                case EWOULDBLOCK:
                        return (EAGAIN);
                case EINTR:
                case ERESTART:
                        if (uio->uio_resid != nbefore)
                                return (MYFIRST_WAIT_PARTIAL);
                        return (error);
                default:
                        return (error);
                }
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}
```

Varios cambios respecto a la versión del Capítulo 14.

El -1 mágico es ahora `MYFIRST_WAIT_PARTIAL`, con un comentario que explica su significado.

El tratamiento de errores tras la espera en el cv es explícito sobre lo que significa cada valor de retorno. EWOULDBLOCK se convierte en EAGAIN (que es el error convencional para el usuario, equivalente a "inténtalo más tarde"). EINTR y ERESTART se comprueban para detectar progreso parcial: si se entregó algún byte, devolvemos el centinela parcial; si no, propagamos el error de señal.

El caso `default` gestiona cualquier otro error que pueda devolver la espera en el cv. Actualmente `cv_timedwait_sig` del kernel solo devuelve los valores listados anteriormente, pero gestionar explícitamente el caso inesperado es un hábito que merece la pena mantener.

### Tratamiento en el llamador

En `myfirst_read` el tratamiento del centinela queda ligeramente más claro:

```c
while (uio->uio_resid > 0) {
        MYFIRST_LOCK(sc);
        error = myfirst_wait_data(sc, ioflag, nbefore, uio);
        if (error != 0) {
                MYFIRST_UNLOCK(sc);
                if (error == MYFIRST_WAIT_PARTIAL)
                        return (0);
                return (error);
        }
        ...
}
```

El lector puede ver de un vistazo qué significa "progreso parcial". Un desarrollador que añada más adelante una nueva razón de salida anticipada sabrá que debe comprobar si debe propagarse al usuario o suprimirse como progreso parcial.

### La ruta de escritura recibe el mismo tratamiento

La ruta de escritura del Capítulo 14 ya implementa el tratamiento de progreso parcial en `myfirst_wait_room`. La Etapa 3 aplica la misma refactorización: sustituir -1 por `MYFIRST_WAIT_PARTIAL`, hacer explícito el switch de tratamiento de errores y documentar la convención.

Un pequeño cambio adicional para la ruta de escritura. El `sema_wait` de la ruta de escritura proveniente de la Sección 2 no es interrumpible por señales. Antes del cambio al semáforo, una escritura bloqueada era interrumpible mediante `cv_wait_sig` dentro de `myfirst_wait_room`. Tras añadir el semáforo, una escritura que se bloquee en `sema_wait` (esperando un hueco de escritor) no es interrumpible.

¿Es aceptable? Para la mayoría de las cargas de trabajo, sí, porque el límite de escritores habitualmente no genera contención. Para una carga de trabajo donde el límite es 1 y los escritores se encolan durante periodos prolongados, los usuarios esperarían que SIGINT funcionara. El compromiso es un compromiso explícito; la Sección 5 mostrará cómo hacer la espera interrumpible añadiendo una espera consciente de señales alrededor de `sema_trywait`.

Para la Etapa 3, aceptamos el `sema_wait` no interrumpible para el caso predeterminado y anotamos el compromiso en un comentario:

```c
/*
 * The writer-cap semaphore wait is not signal-interruptible. For a
 * workload where the cap is rarely contended this is acceptable. If
 * you set writers_limit=1 and create a real queue of writers, consider
 * the interruptible alternative in Section 5.
 */
sema_wait(&sc->writers_sema);
```

### El patrón de espera interrumpible

Para los lectores que quieran la versión completamente interrumpible ahora: combina `sema_trywait` con un bucle de reintento que usa un cv para la espera interrumpible. El código es moderadamente extenso, razón por la cual el Capítulo 15 lo difiere a una subsección opcional.

```c
static int
myfirst_writer_enter_interruptible(struct myfirst_softc *sc)
{
        int error;

        MYFIRST_LOCK(sc);
        while (!sema_trywait(&sc->writers_sema)) {
                if (!sc->is_attached) {
                        MYFIRST_UNLOCK(sc);
                        return (ENXIO);
                }
                error = cv_wait_sig(&sc->writers_wakeup_cv, &sc->mtx);
                if (error != 0) {
                        MYFIRST_UNLOCK(sc);
                        return (error);
                }
        }
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

Esto requiere un segundo cv (`writers_wakeup_cv`) que la ruta de salida señaliza después de cada `sema_post`:

```c
sema_post(&sc->writers_sema);
/* Wake one interruptible waiter so they can retry sema_trywait. */
cv_signal(&sc->writers_wakeup_cv);
```

La versión interrumpible preserva correctamente el tratamiento de EINTR/ERESTART. Es más larga que la versión simple con `sema_wait`, y para la mayoría de los drivers el compromiso no justifica el código adicional. Pero el patrón existe cuando se necesita.

### Errores comunes

**Tratar EWOULDBLOCK como un despertar normal.** La espera con tiempo de espera devuelve EWOULDBLOCK cuando el temporizador dispara. Tratarlo como 0 y volver a comprobar el predicado es incorrecto: el predicado probablemente sigue siendo falso y el bucle gira indefinidamente.

**Tratar EINTR como un despertar recuperable.** EINTR significa que el llamador debe abandonar la espera. Un bucle que hace `while (... != 0) cv_wait_sig(...)` sin tratamiento de EINTR nunca propaga las señales de vuelta al espacio de usuario.

**Olvidar la comprobación de progreso parcial.** Una lectura que copió la mitad de sus bytes y se interrumpe debería devolver esa mitad; una implementación ingenua devuelve EINTR con cero bytes copiados, perdiendo los datos parciales.

**Mezclar `cv_wait` con llamadores interrumpibles por señales.** `cv_wait` (sin `_sig`) bloquea incluso durante la entrega de señales. Una syscall que usa `cv_wait` no puede interrumpirse; el SIGINT del usuario no hace nada hasta que el predicado se satisface. Usa siempre `cv_wait_sig` en contextos de syscall.

**Olvidar volver a comprobar el predicado tras el despertar.** Tanto las señales como cv_signal despiertan al que espera. El predicado puede no ser verdadero al despertar (los despertares espurios están permitidos por la API). Comprueba siempre el predicado en un bucle.

### Cerrando la sección 4

Las esperas interrumpibles por señales tienen cuatro valores de retorno distintos: 0 (normal), EINTR (señal sin reinicio), ERESTART (señal con reinicio), EWOULDBLOCK (tiempo de espera agotado). Cada uno tiene un significado específico y el driver debe gestionar cada uno de forma explícita. La convención de progreso parcial (devolver los bytes copiados hasta el momento, no un error) es el estándar UNIX para lecturas y escrituras. La Etapa 3 del driver aplicó esta disciplina e hizo explícito el centinela de progreso parcial. La Sección 5 lleva más lejos la historia de la coordinación.



## Sección 5: Sincronización entre módulos o subsistemas

Hasta ahora, cada primitiva del driver ha sido local a una función o sección de archivo. Un mutex protege un buffer; un cv señaliza a los lectores; un sema limita a los escritores; un sx almacena en caché las estadísticas. Cada primitiva resuelve un problema en un lugar.

Los drivers reales tienen coordinación que abarca varios subsistemas. Un callout dispara, necesita que una tarea termine su trabajo, necesita que un thread de usuario vea el estado resultante y necesita que otro subsistema note que el apagado está en curso. Las primitivas que ya conoces son suficientes; la dificultad está en componerlas de forma que el handshake entre componentes sea explícito y fácil de mantener.

Esta sección enseña la composición. Introduce una pequeña disciplina de flag atómico para la visibilidad del apagado entre contextos, un handshake de flag de estado entre el callout y la tarea, y los comienzos de una capa de envoltura que la Sección 6 formalizará. La Etapa 4 del driver del Capítulo 15 comienza aquí.

### El problema del flag de apagado

Un problema recurrente en el detach del driver: varios contextos necesitan saber que el apagado está en curso. El driver del Capítulo 14 usa `sc->is_attached` como ese flag, leído bajo el mutex en la mayoría de los lugares y ocasionalmente leído sin protección (con el comentario "la lectura al inicio del manejador puede estar sin protección"). Esto funciona, pero tiene dos problemas sutiles.

En primer lugar, la lectura sin protección es técnicamente comportamiento indefinido en C puro. Un escritor concurrente y un lector no sincronizado constituyen una condición de carrera de datos; el compilador puede transformar el código asumiendo que no hay acceso concurrente. Los compiladores del kernel actuales raramente lo hacen, pero el código no es estrictamente portable y un compilador futuro podría romperlo.

En segundo lugar, las lecturas protegidas por mutex se serializan a través de un lock incluso cuando lo único que quieres es un rápido vistazo de "¿ya se ha apagado?". En una ruta crítica, ese coste es medible.

La disciplina moderna: usa `atomic_load_int` para las lecturas y `atomic_store_int` (o `atomic_store_rel_int`) para las escrituras. Estas operaciones están definidas por el modelo de memoria de C para ser bien ordenadas y libres de condiciones de carrera. También son muy baratas: en x86, una carga o almacenamiento simple con las barreras adecuadas; en otras arquitecturas, una única instrucción atómica.

### La API atómica en una página

`/usr/src/sys/sys/atomic_common.h` y los encabezados específicos de arquitectura definen las operaciones atómicas. Las que usarás con más frecuencia:

- `atomic_load_int(p)`: lee `*p` de forma atómica. Sin barrera de memoria.
- `atomic_load_acq_int(p)`: lee `*p` de forma atómica con semántica de adquisición (acquire). Los accesos a memoria posteriores no pueden reordenarse antes de la carga.
- `atomic_store_int(p, v)`: escribe `v` en `*p` de forma atómica. Sin barrera de memoria.
- `atomic_store_rel_int(p, v)`: escribe `v` en `*p` de forma atómica con semántica de liberación (release). Los accesos a memoria anteriores no pueden reordenarse después del almacenamiento.
- `atomic_fetchadd_int(p, v)`: devuelve el valor antiguo de `*p` y establece `*p = *p + v` de forma atómica.
- `atomic_cmpset_int(p, old, new)`: si `*p == old`, establece `*p = new` y devuelve 1; en caso contrario, devuelve 0.

Para el flag de apagado, el patrón es:

- Escritor (detach): `atomic_store_rel_int(&sc->is_attached, 0)`. El release garantiza que cualquier cambio de estado previo (drenado, broadcasts de cv) sea visible antes de que el flag se ponga a 0.
- Lectores (cualquier contexto): `if (atomic_load_acq_int(&sc->is_attached) == 0) { ... }`. El acquire garantiza que cualquier comprobación posterior vea el estado que pretendía el escritor.

El capítulo usa `atomic_load_acq_int` en las comprobaciones de apagado en el lado de lectura y `atomic_store_rel_int` en la ruta de detach. Esto hace que la visibilidad del apagado sea correcta en todos los contextos sin introducir el coste de un mutex en la ruta crítica.

### ¿Por qué no simplemente un flag protegido por mutex?

Es una pregunta razonable. La respuesta es: "porque el patrón atómico es más barato e igualmente correcto para este invariante específico". El flag tiene exactamente dos estados (1 y 0), la transición es unidireccional (de 1 a 0, sin retorno a 1 en esta instancia), y ningún lector necesita atomicidad junto con otros cambios de estado; cada lector solo quiere saber "¿sigue conectado?".

Para un invariante con múltiples campos o transiciones bidireccionales, un mutex es la herramienta adecuada. Para un flag monotónico de un bit, las operaciones atómicas ganan.

### Aplicación del flag atómico

La refactorización de la Etapa 4 convierte las lecturas de `sc->is_attached` en cargas atómicas donde actualmente ocurren fuera del mutex. Los lugares a modificar son:

- `myfirst_open`: la comprobación de entrada `if (sc == NULL || !sc->is_attached)`.
- `myfirst_read`: la comprobación de entrada tras `devfs_get_cdevpriv`.
- `myfirst_write`: la comprobación de entrada tras `devfs_get_cdevpriv`.
- `myfirst_poll`: la comprobación de entrada.
- Cada callback de callout: `if (!sc->is_attached) return;`.
- La comprobación equivalente de cada callback de tarea (si existe).
- `myfirst_tick_source` tras adquirir el mutex (esta está bajo el mutex; podría ser una carga atómica pero no tiene por qué serlo).

Las comprobaciones bajo mutex dentro de `myfirst_wait_data`, `myfirst_wait_room` y las recomprobaciones de bloqueo tras el despertar del cv permanecen como están: ya están serializadas por el mutex.

La escritura en detach se convierte en:

```c
MYFIRST_LOCK(sc);
if (sc->active_fhs > 0) {
        MYFIRST_UNLOCK(sc);
        return (EBUSY);
}
atomic_store_rel_int(&sc->is_attached, 0);
cv_broadcast(&sc->data_cv);
cv_broadcast(&sc->room_cv);
MYFIRST_UNLOCK(sc);
```

El store-release se empareja con las lecturas atomic-load-acquire en los demás contextos. Cualquier cambio de estado que haya ocurrido antes del almacenamiento (por ejemplo, cualquier preparación previa del apagado) es visible para cualquier thread que posteriormente realice una acquire-read.

Las comprobaciones de entrada del manejador se convierten en:

```c
if (sc == NULL || atomic_load_acq_int(&sc->is_attached) == 0)
        return (ENXIO);
```

Para los callbacks de callout, la comprobación estaba bajo el mutex; la dejamos bajo el mutex por coherencia con el resto de la serialización del callback. Algunos drivers convierten incluso la comprobación del callout a una lectura atómica por rendimiento; el driver del Capítulo 15 no lo hace, porque el coste del mutex es insignificante a la frecuencia de disparo del callout.

### El handshake entre callout y tarea

Un problema diferente de coordinación entre componentes. Supón que el callout de watchdog detecta un bloqueo y quiere desencadenar una acción de recuperación en una tarea. El callout no puede hacer la recuperación por sí mismo (podría dormir, llamar al espacio de usuario, etc.). El driver actual resuelve esto encadenando una tarea desde el callout. Lo que no resuelve es "no encadenar la tarea si la recuperación anterior sigue en curso".

Un pequeño flag de estado lo resuelve. Añade al softc:

```c
int recovery_in_progress;   /* 0 or 1; protected by sc->mtx */
```

El callout:

```c
static void
myfirst_watchdog(void *arg)
{
        struct myfirst_softc *sc = arg;
        /* ... existing watchdog logic ... */

        if (stall_detected && !sc->recovery_in_progress) {
                sc->recovery_in_progress = 1;
                taskqueue_enqueue(sc->tq, &sc->recovery_task);
        }

        /* ... re-arm as before ... */
}
```

La tarea:

```c
static void
myfirst_recovery_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        /* ... recovery work ... */

        MYFIRST_LOCK(sc);
        sc->recovery_in_progress = 0;
        MYFIRST_UNLOCK(sc);
}
```

El flag está protegido por el mutex (ambas escrituras ocurren bajo el mutex; la lectura en el callout ocurre bajo el mutex porque los callouts mantienen el mutex a través de `callout_init_mtx`). Se preserva el invariante «como máximo una tarea de recuperación a la vez». Un watchdog que se dispara durante la recuperación ve el flag establecido y no encola nada.

Este es un ejemplo mínimo, pero el patrón se generaliza. Cada vez que el driver necesita coordinar «haz X solo si Y no está ocurriendo ya», un flag de estado protegido por un lock adecuado es la herramienta correcta.

### El softc de la fase 4

Todo encaja. La fase 4 añade estos campos:

```c
/* Semaphore and its diagnostic fields (from Stage 1). */
struct sema     writers_sema;
int             writers_limit;
int             writers_trywait_failures;

/* Stats cache (from Stage 2). */
struct sx       stats_cache_sx;
uint64_t        stats_cache_bytes_10s;
uint64_t        stats_cache_last_refresh_ticks;

/* Recovery coordination (new in Stage 4). */
int             recovery_in_progress;
struct task     recovery_task;
int             recovery_task_runs;
```

Los tres campos forman un sustrato coherente de coordinación entre subsistemas. La sección 6 encapsula el vocabulario. La sección 8 presenta la versión definitiva.

### Ordenamiento de memoria en una subsección

El ordenamiento de memoria puede parecer abstracto; un resumen concreto ayuda.

En arquitecturas con ordenamiento estricto (x86, amd64), las cargas y almacenamientos simples de valores `int` alineados son atómicos con respecto a otros valores `int` alineados. Una escritura de `int flag = 0` es visible para todos los demás CPUs con prontitud. Rara vez necesitas barreras.

En arquitecturas con ordenamiento débil (arm64, riscv, powerpc), el compilador y la CPU son libres de reordenar cargas y almacenamientos siempre que la secuencia parezca correcta para un único thread. Una escritura de un CPU puede retrasarse antes de ser visible para otro CPU, y las lecturas en ese otro CPU pueden reordenarse entre sí.

La API `atomic(9)` suaviza esa diferencia. `atomic_store_rel_int` y `atomic_load_acq_int` producen las barreras correctas en todas las arquitecturas. No necesitas saber qué arquitectura es débil o fuerte; usas la API y lo correcto ocurre.

Para el driver del capítulo 15, usar `atomic_store_rel_int` en la escritura del detach y `atomic_load_acq_int` en las comprobaciones de entrada te da un driver que funciona correctamente tanto en x86 como en arm64. Si el driver alguna vez llega a sistemas arm64 (y FreeBSD 14.3 admite arm64 muy bien), la disciplina vale la pena.

### Cerrando la sección 5

La coordinación entre componentes en un driver usa los mismos primitivos que la sincronización local, compuestos. La API atomic cubre flags de apagado baratos con el ordenamiento de memoria correcto. Los flags de estado protegidos por el lock apropiado coordinan los invariantes de "como mucho uno" entre callouts, tareas y threads de usuario. La fase 4 del driver del capítulo 15 añadió ambos patrones. La sección 6 da el siguiente paso y encapsula el vocabulario de sincronización en un header dedicado.



## Sección 6: Sincronización y diseño modular

El driver usa ahora cinco tipos de primitivos de sincronización: un mutex, dos variables de condición, dos sx locks, un semáforo de conteo y operaciones atómicas. Cada uno aparece en varios puntos a lo largo del código fuente. Un mantenedor que lea el archivo por primera vez debe reconstruir la estrategia de sincronización a partir de los puntos de llamada dispersos.

Esta sección encapsula el vocabulario de sincronización en un pequeño header, `myfirst_sync.h`, que nombra cada operación que realiza el driver. El header no añade nuevos primitivos; les da nombres legibles a los existentes y documenta sus contratos en un único lugar. La fase 4 del driver del capítulo 15 introduce el header y actualiza el código fuente principal para usarlo.

Una nota sobre el estado antes de continuar. El wrapper `myfirst_sync.h` es **una recomendación, no una convención de FreeBSD**. La mayoría de los drivers bajo `/usr/src/sys/dev` llaman directamente a `mtx_lock`, `sx_xlock`, `cv_wait` y similares; no incluyen un header de sincronización privado. Si ojeas el árbol, no encontrarás una expectativa de la comunidad de que cada driver proporcione tal capa. Lo que la comunidad de FreeBSD *sí* espera es un orden de locks claro y documentado, y un bloque de comentario al estilo `LOCKING.md` que un revisor pueda seguir, y esa expectativa la cumplimos en cada capítulo de la parte 3. El header wrapper es una extensión estilística que ha funcionado bien en varios drivers de tamaño medio dentro y fuera del árbol; es valioso para este libro porque convierte el vocabulario de sincronización en algo que puedes nombrar, auditar y cambiar en un único lugar. Si tu futuro driver no necesita esa legibilidad adicional, omitir el header y mantener las llamadas a los primitivos directamente en el código fuente es una elección perfectamente normal. Lo que importa es la disciplina subyacente: orden de locks, drenaje en el detach, contratos explícitos. El wrapper es una forma de mantener esa disciplina visible, no la única.

### Por qué encapsular

Tres beneficios concretos.

**Legibilidad.** Una ruta de código que lee `myfirst_sync_writer_enter(sc)` le dice al lector exactamente qué hace la llamada (entrar en la sección de escritura). La misma ruta escrita como `if (ioflag & IO_NDELAY) { if (!sema_trywait(&sc->writers_sema)) ...` es correcta, pero le dice menos al lector.

**Modificabilidad.** Si la estrategia de sincronización cambia (por ejemplo, el semáforo de cuota de escritores se sustituye por una espera interruptible basada en cv de la sección 4), el cambio ocurre en un único lugar del header. Los puntos de llamada en `myfirst_write` no cambian.

**Verificabilidad.** El header es el único lugar donde se documentan los contratos de sincronización. Una revisión de código puede verificar si "¿cada enter tiene un leave correspondiente?" con un grep al header. Sin el header, la revisión debe recorrer cada punto de llamada.

El coste de la encapsulación es mínimo. Un header de entre 100 y 200 líneas. Media hora de refactorización. Una ligera capa de indirección que un compilador moderno elimina por inlining.

### La forma de `myfirst_sync.h`

El header nombra cada operación de sincronización. No define estructuras nuevas; las estructuras permanecen en el softc. Proporciona funciones inline o macros que envuelven los primitivos.

Un esquema:

```c
#ifndef MYFIRST_SYNC_H
#define MYFIRST_SYNC_H

#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/sema.h>
#include <sys/condvar.h>

struct myfirst_softc;       /* Forward declaration. */

/* Data-path mutex operations. */
static __inline void    myfirst_sync_lock(struct myfirst_softc *sc);
static __inline void    myfirst_sync_unlock(struct myfirst_softc *sc);
static __inline void    myfirst_sync_assert_locked(struct myfirst_softc *sc);

/* Configuration sx operations. */
static __inline void    myfirst_sync_cfg_read_begin(struct myfirst_softc *sc);
static __inline void    myfirst_sync_cfg_read_end(struct myfirst_softc *sc);
static __inline void    myfirst_sync_cfg_write_begin(struct myfirst_softc *sc);
static __inline void    myfirst_sync_cfg_write_end(struct myfirst_softc *sc);

/* Writer-cap semaphore operations. */
static __inline int     myfirst_sync_writer_enter(struct myfirst_softc *sc,
                            int ioflag);
static __inline void    myfirst_sync_writer_leave(struct myfirst_softc *sc);

/* Stats cache sx operations. */
static __inline void    myfirst_sync_stats_cache_read_begin(
                            struct myfirst_softc *sc);
static __inline void    myfirst_sync_stats_cache_read_end(
                            struct myfirst_softc *sc);
static __inline int     myfirst_sync_stats_cache_try_promote(
                            struct myfirst_softc *sc);
static __inline void    myfirst_sync_stats_cache_downgrade(
                            struct myfirst_softc *sc);
static __inline void    myfirst_sync_stats_cache_write_begin(
                            struct myfirst_softc *sc);
static __inline void    myfirst_sync_stats_cache_write_end(
                            struct myfirst_softc *sc);

/* Attach-flag atomic operations. */
static __inline int     myfirst_sync_is_attached(struct myfirst_softc *sc);
static __inline void    myfirst_sync_mark_detaching(struct myfirst_softc *sc);

#endif /* MYFIRST_SYNC_H */
```

Cada función envuelve exactamente una llamada a un primitivo más cualquier convención que necesite el punto de llamada. Por ejemplo, `myfirst_sync_writer_enter` recibe el parámetro `ioflag` y elige entre `sema_trywait` (para `IO_NDELAY`) y `sema_wait`. El llamador no necesita conocer la lógica trywait-vs-wait; el header se encarga de ello.

### La implementación

Cada función es un wrapper inline simple. Implementaciones de ejemplo (para las más interesantes):

```c
static __inline void
myfirst_sync_lock(struct myfirst_softc *sc)
{
        mtx_lock(&sc->mtx);
}

static __inline void
myfirst_sync_unlock(struct myfirst_softc *sc)
{
        mtx_unlock(&sc->mtx);
}

static __inline void
myfirst_sync_assert_locked(struct myfirst_softc *sc)
{
        mtx_assert(&sc->mtx, MA_OWNED);
}

static __inline int
myfirst_sync_writer_enter(struct myfirst_softc *sc, int ioflag)
{
        if (ioflag & IO_NDELAY) {
                if (!sema_trywait(&sc->writers_sema)) {
                        mtx_lock(&sc->mtx);
                        sc->writers_trywait_failures++;
                        mtx_unlock(&sc->mtx);
                        return (EAGAIN);
                }
        } else {
                sema_wait(&sc->writers_sema);
        }
        if (!myfirst_sync_is_attached(sc)) {
                sema_post(&sc->writers_sema);
                return (ENXIO);
        }
        return (0);
}

static __inline void
myfirst_sync_writer_leave(struct myfirst_softc *sc)
{
        sema_post(&sc->writers_sema);
}

static __inline int
myfirst_sync_is_attached(struct myfirst_softc *sc)
{
        return (atomic_load_acq_int(&sc->is_attached));
}

static __inline void
myfirst_sync_mark_detaching(struct myfirst_softc *sc)
{
        atomic_store_rel_int(&sc->is_attached, 0);
}
```

El wrapper de `writer_enter` es el más complejo; todo lo demás es una sola línea. Un header con esta forma produce cero sobrecarga en tiempo de ejecución (el compilador elimina por inlining cada llamada) y añade una legibilidad sustancial.

### Cómo cambia el código fuente

Cada `mtx_lock(&sc->mtx)` en el código fuente principal se convierte en `myfirst_sync_lock(sc)`. Cada `sema_wait(&sc->writers_sema)` se convierte en `myfirst_sync_writer_enter(sc, ioflag)` o una variante. Cada `atomic_load_acq_int(&sc->is_attached)` se convierte en `myfirst_sync_is_attached(sc)`.

El código fuente principal se lee con más claridad:

```c
/* Before: */
if (ioflag & IO_NDELAY) {
        if (!sema_trywait(&sc->writers_sema)) {
                MYFIRST_LOCK(sc);
                sc->writers_trywait_failures++;
                MYFIRST_UNLOCK(sc);
                return (EAGAIN);
        }
} else {
        sema_wait(&sc->writers_sema);
}
if (!sc->is_attached) {
        sema_post(&sc->writers_sema);
        return (ENXIO);
}

/* After: */
error = myfirst_sync_writer_enter(sc, ioflag);
if (error != 0)
        return (error);
```

Cinco líneas de implementación se convierten en una de intención. Un lector que quiera saber qué hace `myfirst_sync_writer_enter` abre el header y lee la implementación. Un lector que acepta la interfaz continúa leyendo.

### Convenciones de nomenclatura

Una pequeña disciplina para elegir nombres en una capa wrapper de sincronización.

**Nombra operaciones, no primitivos.** `myfirst_sync_writer_enter` describe lo que hace el llamador (entrar en la sección de escritura). `myfirst_sync_sema_wait` describiría el primitivo (llamar a sema_wait), lo cual es menos útil.

**Usa pares enter/leave para la adquisición con ámbito.** Cada `enter` tiene un `leave` correspondiente. Esto hace visualmente evidente si el driver siempre libera lo que adquiere.

**Usa pares read/write para el acceso compartido/exclusivo.** `cfg_read_begin`/`cfg_read_end` para compartido; `cfg_write_begin`/`cfg_write_end` para exclusivo. El sufijo begin/end refleja la estructura del punto de llamada.

**Usa `is_` para predicados que devuelven valores similares a bool.** `myfirst_sync_is_attached` se lee como inglés natural.

**Usa `mark_` para transiciones de estado atómicas.** `myfirst_sync_mark_detaching` describe la transición.

### Qué no poner en el header

El header debe envolver primitivos de sincronización, no lógica de negocio. Una función que adquiere un lock y también realiza trabajo "interesante" debe permanecer en el código fuente principal; solo la manipulación pura del lock pertenece al header.

El header tampoco debe ocultar detalles importantes. Por ejemplo, `myfirst_sync_writer_enter` devuelve `EAGAIN`, `ENXIO` o 0; el llamador debe comprobarlo. Un wrapper que silenciosamente "retornara" ante `ENXIO` ocultaría una ruta de error importante. El contrato del wrapper debe ser explícito.

### Una disciplina relacionada: las aserciones

El header es un buen lugar para poner las aserciones que documentan invariantes. Una función que debe llamarse bajo el mutex puede invocar `myfirst_sync_assert_locked(sc)` al principio:

```c
static void
myfirst_some_helper(struct myfirst_softc *sc)
{
        myfirst_sync_assert_locked(sc);
        /* ... */
}
```

En un kernel de depuración (con `INVARIANTS`) la aserción se activa si alguna vez se llama al helper sin el mutex. En un kernel de producción la aserción se elimina.

El código del capítulo 14 usa `MYFIRST_ASSERT`; la refactorización del capítulo 15 lo mantiene como `myfirst_sync_assert_locked` con el mismo comportamiento.

### Un recorrido breve por WITNESS: dormir bajo un lock no durmiente

El capítulo 34 recorre una inversión de orden de locks entre dos mutexes. Una clase distinta de aviso de WITNESS, igualmente común y igual de fácil de prevenir, merece una mención breve aquí porque cae justo en el centro del territorio del capítulo 15: la interacción entre mutexes y sx locks.

Imagina una primera refactorización de la ruta de lectura de configuración. El autor acaba de añadir un `sx_slock` sobre el blob de configuración para acceso mayoritariamente de lectura y, sin pensarlo, lo llama desde dentro de una ruta de código que todavía mantiene el mutex de la ruta de datos:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error;

        MYFIRST_LOCK(sc);                /* mtx_lock: non-sleepable */
        sx_slock(&sc->cfg_sx);           /* sx_slock: sleepable */
        error = myfirst_copy_out_locked(sc, uio);
        sx_sunlock(&sc->cfg_sx);
        MYFIRST_UNLOCK(sc);
        return (error);
}
```

El código compila y, en pruebas superficiales en un kernel sin depuración, parece funcionar correctamente. Cárgalo en un kernel construido con `options WITNESS` y ejecuta el mismo laboratorio, y la consola reporta algo parecido a esto:

```text
lock order reversal: (sleepable after non-sleepable)
 1st 0xfffff800...  myfirst_sc_mtx (mutex) @ /usr/src/sys/modules/myfirst/myfirst.c:...
 2nd 0xfffff800...  myfirst_cfg_sx (sx) @ /usr/src/sys/modules/myfirst/myfirst.c:...
stack backtrace:
 #0 witness_checkorder+0x...
 #1 _sx_slock+0x...
 #2 myfirst_read+0x...
```

Hay dos cosas que vale la pena leer con atención en este informe. El texto entre paréntesis "(sleepable after non-sleepable)" te dice exactamente cuál es la inversión: el thread tomó primero un lock no durmiente (el mutex) y luego pidió uno durmiente (el sx lock). WITNESS rechaza esto porque `sx_slock` puede dormir, y dormir mientras se mantiene un lock no durmiente es una clase de error del kernel bien definida: el planificador no puede sacar el thread de la CPU sin migrar también a los waiters del mutex, y los invariantes que hacen que `MTX_DEF` sea barato dejan de cumplirse. Lo segundo es que WITNESS lo reporta la primera vez que se ejecuta esa ruta, mucho antes de que haya contención real. No tienes que reproducir una condición de carrera; el aviso se activa con el propio ordenamiento.

La solución es disciplina en el orden, no un primitivo diferente. Toma el sx lock primero y luego el mutex:

```c
sx_slock(&sc->cfg_sx);
MYFIRST_LOCK(sc);
error = myfirst_copy_out_locked(sc, uio);
MYFIRST_UNLOCK(sc);
sx_sunlock(&sc->cfg_sx);
```

O, lo que es mejor para la mayoría de los drivers, lee la configuración bajo el sx lock en una instantánea local y libera el sx lock antes de tocar el mutex de la ruta de datos en absoluto. La encapsulación en `myfirst_sync.h` ayuda aquí porque el contrato de orden de locks está nombrado y documentado en un único lugar; una revisión que vea `myfirst_sync_cfg_slock` seguido de `myfirst_sync_lock` puede confirmar el orden de un vistazo.

Este recorrido se mantiene deliberadamente más breve que el del capítulo 34. La categoría de error es diferente, y la lección es específica de la distinción durmiente/no durmiente sobre la que se construye el capítulo 15. El recorrido más amplio sobre inversión de orden de locks corresponde al capítulo de depuración; este pertenece al lugar donde el lector compone por primera vez sx locks y mutexes en la misma ruta de código.

### Cerrando la sección 6

Un pequeño header de sincronización nombra cada operación que realiza el driver y centraliza los contratos. El código fuente principal se lee con más claridad; el header es el único lugar al que un mantenedor acude para entender o cambiar la estrategia. El `myfirst_sync.h` de la fase 4 no añade nuevos primitivos; encapsula los de las secciones 2 a 5. La sección 7 escribe las pruebas que validan toda la composición.



## Sección 7: Pruebas de sincronización avanzada

Cada primitivo que este capítulo introdujo tiene un modo de fallo. Un semáforo con un `sema_post` faltante pierde slots. Un upgrade de sx que no vuelve a comprobar el predicado actualiza de forma redundante. Una espera interruptible por señal que ignora EINTR bloquea al llamador. Un flag de estado leído sin el lock correcto o la disciplina atómica correcta lee valores obsoletos de forma silenciosa.

Esta sección trata de escribir pruebas que hagan aflorar esos modos de fallo antes de que los encuentren los usuarios. Las pruebas no son pruebas unitarias en el sentido puro; son arneses de estrés que ejercitan el driver bajo carga concurrente y comprueban invariantes. El código fuente de acompañamiento del capítulo 15 incluye tres programas de prueba; esta sección recorre cada uno de ellos.

### Por qué importan las pruebas de estrés

Los bugs de sincronización rara vez aparecen en las pruebas monohilo. Un `sema_post` olvidado es invisible hasta que suficientes escritores han pasado y el semáforo se agota. Una lectura atómica mal ubicada es invisible hasta que se produce un entrelazamiento concreto. Una condición de carrera en el detach es invisible hasta que el detach y la descarga ocurren con verdadera concurrencia.

Las pruebas de estrés encuentran estos bugs ejecutando el driver en configuraciones que exponen los entrelazamientos. Muchos lectores y escritores concurrentes. Una fuente de ticks rápida. Ciclos frecuentes de detach y recarga. Escrituras simultáneas de sysctl. Cuanto más trabaja el driver, más probable es que un bug latente salga a la superficie.

Las pruebas no reemplazan a `WITNESS` ni a `INVARIANTS`. `WITNESS` detecta violaciones de orden de lock con cualquier carga. `INVARIANTS` detecta violaciones estructurales. Las pruebas de estrés detectan errores lógicos que ni las comprobaciones estáticas ni las dinámicas ligeras pueden identificar.

### Test 1: Corrección del writer-cap

El invariante del semáforo writer-cap es "como máximo `writers_limit` escritores en `myfirst_write` a la vez". Un programa de prueba lanza múltiples escritores concurrentes, cada uno de los cuales escribe unos pocos bytes, registra su ID de proceso en un pequeño marcador al inicio de sus escrituras y continúa. Un proceso monitor lee el cbuf en segundo plano y cuenta los marcadores concurrentes.

La prueba se encuentra en `examples/part-03/ch15-more-synchronization/tests/writer_cap_test.c`:

```c
/*
 * writer_cap_test: start N writers and verify no more than
 * writers_limit are simultaneously inside the write path.
 */
#include <sys/param.h>
#include <sys/time.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

#define N_WRITERS 16

int
main(int argc, char **argv)
{
        int fd, i;
        char buf[64];
        int writers = (argc > 1) ? atoi(argv[1]) : N_WRITERS;

        for (i = 0; i < writers; i++) {
                if (fork() == 0) {
                        fd = open("/dev/myfirst", O_WRONLY);
                        if (fd < 0)
                                err(1, "open");
                        snprintf(buf, sizeof(buf), "w%d\n", i);
                        for (int j = 0; j < 100; j++) {
                                write(fd, buf, strlen(buf));
                                usleep(1000);
                        }
                        close(fd);
                        _exit(0);
                }
        }
        while (wait(NULL) > 0)
                ;
        return (0);
}
```

La prueba lanza `N_WRITERS` procesos, cada uno de los cuales escribe 100 mensajes cortos con un retardo de 1 ms. Un proceso lector lee `/dev/myfirst` y observa la intercalación.

Una comprobación simple del invariante: el lector lee 100 bytes cada vez y registra cuántos prefijos de escritor distintos aparecen en esa ventana. Si `writers_limit` es 4, el lector debería ver como máximo 4 prefijos en cualquier ventana de 100 bytes (más o menos). Más de 4 indica que el límite no se está aplicando.

Una comprobación más rigurosa utiliza `sysctl dev.myfirst.0.stats.writers_trywait_failures` para observar la tasa de fallos en el modo `O_NONBLOCK`. Si estableces `writers_limit=2` y lanzas 16 escritores no bloqueantes, la mayoría debería ver EAGAIN; el contador de fallos debería crecer rápidamente.

### Test 2: Concurrencia de la caché de estadísticas

El invariante de la caché de estadísticas es "muchos lectores concurrentes, refrescada como máximo una vez por segundo". Una prueba:

- Lanza 32 procesos lectores concurrentes, cada uno leyendo `dev.myfirst.0.stats.bytes_written_10s` en un bucle cerrado.
- Lanza 1 escritor que sigue escribiendo en el dispositivo.
- Observa la tasa de refresco de la caché mediante DTrace:

```text
# dtrace -n '
  fbt::myfirst_stats_cache_refresh:entry {
        @["refreshes"] = count();
  }
  tick-10s { printa(@); exit(0); }
'
```

Resultado esperado: aproximadamente 10 refrescos cada 10 segundos (uno por cada expiración de caché obsoleta). No 32 cada 10 segundos; la caché elimina el recálculo por lector.

Si la tasa de refresco aumenta bajo contención, la ruta rápida `sx_try_upgrade` falla con demasiada frecuencia y el mecanismo de reserva (soltar y readquirir) está introduciendo condiciones de carrera. El código del driver debería gestionar esto correctamente; si no lo hace, la prueba expone el fallo.

### Test 3: Detach bajo carga

El invariante del detach es "el detach completa de forma limpia incluso cuando todos los primitivos de los Capítulos 14 y 15 están bajo carga". Una prueba de detach:

```text
# ./stress_all.sh &
# STRESS_PID=$!
# sleep 5
# kldunload myfirst
# kill -TERM $STRESS_PID
```

Donde `stress_all.sh` ejecuta:

- Varios escritores concurrentes.
- Varios lectores concurrentes.
- La fuente de ticks habilitada a 1 ms.
- El heartbeat habilitado a 100 ms.
- El watchdog habilitado a 1 s.
- Escrituras ocasionales mediante sysctl de bulk_writer_flood.
- Ajustes ocasionales de sysctl de writers_limit.
- Lecturas ocasionales de la caché de estadísticas.

El detach debería completar. Si se bloquea o provoca un panic, la disciplina de ordenación tiene un fallo. Con el código de los Capítulos 14 y 15 correctamente ordenado, la prueba debería pasar de forma fiable.

### Observación con DTrace

DTrace es el bisturí para depurar la sincronización. Algunos comandos útiles de una sola línea:

**Contar wakeups de cv por nombre de cv.**

```text
# dtrace -n 'fbt::cv_signal:entry { @[stringof(arg0)] = count(); }'
```

Interpretar la salida requiere conocer los nombres de tus cvs; los del driver son `"myfirst data"` y `"myfirst room"`.

**Contar operaciones de semáforo.**

```text
# dtrace -n '
  fbt::_sema_wait:entry { @[probefunc] = count(); }
  fbt::_sema_post:entry { @[probefunc] = count(); }
'
```

En un driver equilibrado, el recuento de posts es igual al de waits a lo largo de una ejecución larga (más el valor inicial de `sema_init`).

**Observar los disparos de tareas.**

```text
# dtrace -n 'fbt::taskqueue_run_locked:entry { @[execname] = count(); }'
```

Muestra qué threads del taskqueue se están ejecutando, lo cual es útil para confirmar que el taskqueue privado está recibiendo trabajo.

**Observar los disparos de callout.**

```text
# dtrace -n 'fbt::callout_reset:entry { @[probefunc] = count(); }'
```

Muestra con qué frecuencia se están rearmando los callouts, lo que debería coincidir con la tasa de intervalo configurada.

Ejecuta cada comando bajo tu carga de estrés. Los conteos deberían coincidir con tus expectativas. Un desequilibrio inesperado es un punto de partida para la depuración.

### WITNESS, INVARIANTS y el kernel de depuración

La primera línea de defensa sigue siendo el kernel de depuración. Si `WITNESS` no está satisfecho con un orden de locks, corrige el orden antes de publicar. Si `INVARIANTS` falla en `cbuf_*` o `sema_*`, corrige el código que lo llama antes de publicar. Estas comprobaciones son económicas; prescindir de ellas durante el desarrollo es un ahorro falso.

Algunas salidas de `WITNESS` que puedes esperar o que debes evitar:

- **"acquiring duplicate lock of same type"**: adquiriste un lock que ya tienes, probablemente por error. Revisa la ruta de llamada.
- **"lock order reversal"**: dos locks se adquirieron en órdenes diferentes en rutas distintas. Elige un orden, aplícalo y actualiza `LOCKING.md`.
- **"blockable sleep from an invalid context"**: llamaste a algo que puede dormir desde un contexto en el que dormir no está permitido. Comprueba si el contexto es un callout o una interrupción.

Cada advertencia de `WITNESS` en `dmesg` de tu driver es un fallo. Trátalas como equivalentes a un panic; corrige cada una.

### Disciplina de regresión

Tras cada etapa del Capítulo 15, ejecuta:

1. Las pruebas de humo de I/O del Capítulo 11 (lectura, escritura, open y close básicos).
2. Las pruebas de sincronización del Capítulo 12 (lecturas acotadas, configuración protegida con sx).
3. Las pruebas de temporizadores del Capítulo 13 (heartbeat, watchdog, fuente de ticks a distintas frecuencias).
4. Las pruebas de taskqueue del Capítulo 14 (poll waiter, inundación del bulk writer, reset retardado).
5. Las pruebas del Capítulo 15 (writer-cap, caché de estadísticas, detach bajo carga).

Toda la suite debería pasar en un kernel de depuración. Si alguna prueba falla, la regresión es reciente; revierte a la etapa anterior, encuentra el delta y depura.

### Cerrando la sección 7

La sincronización avanzada requiere pruebas avanzadas. Las pruebas de estrés que ejercitan escritores, lectores y sysctls concurrentes sacan a la luz fallos que las pruebas de un solo thread pasan por alto. DTrace hace observables los mecanismos internos de los primitivos de sincronización. `WITNESS` e `INVARIANTS` detectan lo que queda. Ejecutar toda la pila bajo un kernel de depuración es lo más cercano a una prueba "suficientemente buena" que puede lograr un driver. La Sección 8 cierra la Parte 3.



## Sección 8: Refactorización y versionado de tu driver coordinado

La etapa 4 es la etapa de consolidación del Capítulo 15. No añade nueva funcionalidad; reorganiza y documenta las incorporaciones del Capítulo 15, actualiza `LOCKING.md`, incrementa la versión a `0.9-coordination` y ejecuta la suite de regresión completa de los Capítulos 11 al 15.

Esta sección recorre la consolidación, extiende `LOCKING.md` con las secciones del Capítulo 15 y cierra la Parte 3.

### Organización de archivos

La refactorización del Capítulo 15 introduce `myfirst_sync.h`. La lista de archivos queda así:

- `myfirst.c`: el código fuente principal del driver.
- `cbuf.c`, `cbuf.h`: sin cambios respecto al Capítulo 13.
- `myfirst_sync.h`: nueva cabecera con envoltorios de sincronización.
- `Makefile`: sin cambios excepto por la adición de `myfirst_sync.h` a las cabeceras de las que depende el código fuente (para el seguimiento de dependencias de `make` si es necesario).

Dentro de `myfirst.c`, la organización del Capítulo 15 sigue el mismo patrón que el Capítulo 14 con algunas adiciones:

1. Includes (ahora incluye `myfirst_sync.h`).
2. Estructura softc (ampliada con los campos del Capítulo 15).
3. Estructura de file-handle (sin cambios).
4. Declaración de cdevsw (sin cambios).
5. Funciones auxiliares de buffer (sin cambios).
6. Funciones auxiliares de caché (nuevas; `myfirst_stats_cache_refresh`).
7. Funciones auxiliares de espera con variable de condición (revisadas con gestión explícita de EINTR/ERESTART).
8. Manejadores de sysctl (ampliados con writers_limit, stats_cache y recuperación).
9. Callbacks de callout (revisados para usar atomic_load_acq_int para is_attached donde corresponde).
10. Callbacks de tarea (ampliados con recovery_task).
11. Manejadores de cdev (revisados para usar los envoltorios myfirst_sync_*).
12. Métodos de dispositivo (attach/detach ampliados con la disciplina de sema, sx y flag atómico).
13. Pegamento del módulo (incremento de versión).

El cambio clave en cada sección es el uso de los envoltorios de `myfirst_sync.h` donde el código del Capítulo 14 utilizaba llamadas directas a los primitivos. Esto es visible en attach, detach y en cada manejador.

### La actualización de `LOCKING.md`

El `LOCKING.md` del Capítulo 14 tenía secciones para el mutex, las cvs, el sx, los callouts y las tareas. El Capítulo 15 añade las secciones de Semáforos, Coordinación y una sección actualizada de Orden de Locks.

```markdown
## Semaphores

The driver owns one counting semaphore:

- `writers_sema`: caps concurrent writers at `sc->writers_limit`.
  Default limit: 4. Range: 1-64. Configurable via the
  `dev.myfirst.N.writers_limit` sysctl.

Semaphore operations happen outside `sc->mtx`. The internal `sema_mtx`
is not in the documented lock order because it does not conflict with
`sc->mtx`; the driver never holds `sc->mtx` across a `sema_wait` or
`sema_post`.

Lowering `writers_limit` below the current semaphore value is
best-effort: the handler lowers the target and lets new entries
observe the lower cap as the value drains. Raising posts additional
slots immediately.

### Sema Drain Discipline

The driver tracks `writers_inflight` as an atomic int. It is
incremented before any `sema_*` call (specifically at the top of
`myfirst_sync_writer_enter`) and decremented after the matching
`sema_post` (in `myfirst_sync_writer_leave` or on every error
return).

Detach waits for `writers_inflight` to reach zero before calling
`sema_destroy`. This closes the use-after-free race where a woken
waiter is between `cv_wait` return and its final
`mtx_unlock(&sema->sema_mtx)` when `sema_destroy` tears down the
internal mutex.

`sema_destroy` itself is called only after:

1. `is_attached` has been cleared atomically.
2. `writers_limit` wake-up slots have been posted to the sema.
3. `writers_inflight` has been observed to reach zero.
4. Every callout, task, and selinfo has been drained.

## Coordination

The driver uses three cross-component coordination mechanisms:

1. **Atomic is_attached flag.** Read via `atomic_load_acq_int`, written
   via `atomic_store_rel_int` in detach. Allows every context (callout,
   task, user thread) to check shutdown state without acquiring
   `sc->mtx`.
2. **recovery_in_progress state flag.** Protected by `sc->mtx`. Set by
   the watchdog callout, cleared by the recovery task. Ensures at most
   one recovery task is pending or running at a time.
3. **Stats cache sx.** Shared reads, occasional upgrade-promote-
   downgrade for refresh. See the Stats Cache section.

## Stats Cache

The `stats_cache_sx` protects a small cached statistic. The refresh
pattern is:

```c
sx_slock(&sc->stats_cache_sx);
if (stale) {
        if (sx_try_upgrade(&sc->stats_cache_sx)) {
                refresh();
                sx_downgrade(&sc->stats_cache_sx);
        } else {
                sx_sunlock(&sc->stats_cache_sx);
                sx_xlock(&sc->stats_cache_sx);
                if (still_stale)
                        refresh();
                sx_downgrade(&sc->stats_cache_sx);
        }
}
value = sc->stats_cache_bytes_10s;
sx_sunlock(&sc->stats_cache_sx);
```text

## Lock Order

The complete driver lock order is:

```text
sc->mtx  ->  sc->cfg_sx  ->  sc->stats_cache_sx
```text

`WITNESS` enforces this order. The writer-cap semaphore's internal
mutex is not in the graph because the driver never holds `sc->mtx`
(or any other driver lock) across a `sema_wait`/`sema_post` call.

## Detach Ordering (updated)

1. Refuse detach if `sc->active_fhs > 0`.
2. Clear `sc->is_attached` under `sc->mtx` via
   `atomic_store_rel_int`.
3. `cv_broadcast(&sc->data_cv)`; `cv_broadcast(&sc->room_cv)`.
4. Release `sc->mtx`.
5. Post `writers_limit` wake-up slots to `writers_sema`.
6. Wait for `writers_inflight == 0` (sema drain).
7. Drain the three callouts.
8. Drain every task including recovery_task.
9. `seldrain(&sc->rsel)`, `seldrain(&sc->wsel)`.
10. `taskqueue_free(sc->tq)`; `sc->tq = NULL`.
11. `sema_destroy(&sc->writers_sema)` (safe: drain completed).
12. `sx_destroy(&sc->stats_cache_sx)`.
13. Destroy cdev, free sysctl context, destroy cbuf, counters,
    cvs, cfg_sx, mtx.
```

### El incremento de versión

La cadena de versión avanza de `0.8-taskqueues` a `0.9-coordination`:

```c
#define MYFIRST_VERSION "0.9-coordination"
```

Y la cadena de probe del driver:

```c
device_set_desc(dev, "My First FreeBSD Driver (Chapter 15 Stage 4)");
```

### La pasada de regresión final

La suite de regresión del Capítulo 15 añade sus propias pruebas pero también vuelve a ejecutar todas las pruebas de los capítulos anteriores. Un orden compacto:

1. **Construir limpiamente** en un kernel de depuración con todas las opciones habituales habilitadas.
2. **Cargar** el driver.
3. **Pruebas del Capítulo 11**: lectura, escritura, open, close y reset básicos.
4. **Pruebas del Capítulo 12**: lecturas acotadas, lecturas temporizadas, broadcasts de cv, configuración con sx.
5. **Pruebas del Capítulo 13**: callouts a distintas frecuencias, detección por watchdog, fuente de ticks.
6. **Pruebas del Capítulo 14**: poll waiter, inundación de coalescing, reset retardado, detach bajo carga.
7. **Pruebas del Capítulo 15**: corrección del writer-cap, concurrencia de la caché de estadísticas, interrupción de señal con progreso parcial, detach bajo carga completa.
8. **Pasada de WITNESS**: todas las pruebas, cero advertencias en `dmesg`.
9. **Verificación con DTrace**: los recuentos de wakeups y los disparos de tareas coinciden con las expectativas.
10. **Estrés de larga duración**: horas de carga con ciclos periódicos de detach y recarga.

Todas las pruebas pasan. Cada advertencia de `WITNESS` está resuelta. El driver está en `0.9-coordination` y la Parte 3 ha concluido.

### Auditoría de la documentación

Una pasada final de documentación.

- El comentario al inicio de `myfirst.c` se actualiza con el vocabulario del Capítulo 15.
- `myfirst_sync.h` tiene un comentario al inicio que resume el diseño.
- `LOCKING.md` tiene las secciones de Semáforos, Coordinación y Caché de Estadísticas.
- El `README.md` del capítulo en `examples/part-03/ch15-more-synchronization/` describe cada etapa.
- Cada sysctl tiene una cadena de descripción.

Actualizar la documentación puede parecer una carga. Es la diferencia entre un driver que el siguiente mantenedor puede modificar y uno que tiene que reescribir.

### Lista de comprobación final

- [ ] ¿Cada `sema_wait` tiene un `sema_post` correspondiente?
- [ ] ¿Cada `sx_slock` tiene un `sx_sunlock` correspondiente?
- [ ] ¿Cada `sx_xlock` tiene un `sx_xunlock` correspondiente?
- [ ] ¿Cada lectura atómica usa `atomic_load_acq_int` y cada escritura atómica usa `atomic_store_rel_int` donde importa el orden?
- [ ] ¿Cada llamada a `cv_*_sig` gestiona explícitamente EINTR, ERESTART y EWOULDBLOCK?
- [ ] ¿Cada código que realiza esperas bloqueantes registra y comprueba el progreso parcial?
- [ ] ¿Cada primitivo de sincronización está envuelto en `myfirst_sync.h`?
- [ ] ¿Cada primitivo aparece en `LOCKING.md`?
- [ ] ¿El orden de detach en `LOCKING.md` es correcto?
- [ ] ¿El driver pasa la suite de regresión completa?

Un driver que responde afirmativamente a cada elemento es un driver que puedes entregar a otro ingeniero con confianza.

### Cerrando la sección 8

La etapa 4 consolida. La cabecera está en su lugar, `LOCKING.md` está actualizado, la versión refleja la nueva capacidad, la suite de regresión pasa y la auditoría está limpia. El driver es `0.9-coordination` y la historia de sincronización está completa.

La Parte 3 también está completa. Cinco capítulos, cinco primitivos añadidos de uno en uno, cada uno compuesto con lo anterior. La sección de Cierre tras los laboratorios y desafíos enmarca lo que logró la Parte 3 y cómo la Parte 4 lo aprovechará.



## Temas adicionales: operaciones atómicas, `epoch(9)` revisitado y ordenación de memoria

El cuerpo principal del Capítulo 15 enseñó lo esencial. Tres temas adyacentes merecen una mención algo más profunda porque reaparecen en el código real de drivers y porque las ideas subyacentes completan la historia de sincronización.

### Operaciones atómicas en mayor profundidad

El driver del Capítulo 15 utilizó tres primitivos atómicos: `atomic_load_acq_int`, `atomic_store_rel_int` y, de forma implícita, `atomic_fetchadd_int` a través de `counter(9)`. La familia `atomic(9)` es más amplia y estructurada de lo que sugieren estas tres operaciones.

**Primitivos de lectura-modificación-escritura.**

- `atomic_fetchadd_int(p, v)`: devuelve el valor anterior de `*p` y establece `*p += v`. Útil para contadores de avance libre.
- `atomic_cmpset_int(p, old, new)`: si `*p == old`, establece `*p = new` y devuelve 1; de lo contrario, devuelve 0 sin modificar nada. El clásico compare-and-swap. Útil para implementar máquinas de estado lock-free.
- `atomic_cmpset_acq_int`, `atomic_cmpset_rel_int`: variantes con semántica acquire o release.
- `atomic_readandclear_int(p)`: devuelve el valor anterior y establece `*p = 0`. Útil para el patrón «tomar el valor actual y reiniciar».
- `atomic_set_int(p, v)`: activa bits mediante `*p |= v`. Útil para coordinar la activación de flags.
- `atomic_clear_int(p)`: limpia bits mediante `*p &= ~v`. Útil para coordinar el borrado de flags.
- `atomic_swap_int(p, v)`: devuelve el valor anterior de `*p` y establece `*p = v`. Útil para tomar posesión de un puntero.

**Variantes por ancho.** `atomic_load_int`, `atomic_load_long`, `atomic_load_32`, `atomic_load_64`, `atomic_load_ptr`. El tamaño entero que aparece en el nombre corresponde al tipo C. Usa la que coincida con el tipo de tu variable.

**Variantes de barrera.** `atomic_thread_fence_acq`, `atomic_thread_fence_rel`, `atomic_thread_fence_acq_rel`, `atomic_thread_fence_seq_cst`. Barreras puras que ordenan los accesos a memoria anteriores y posteriores sin modificar atómicamente ninguna ubicación concreta. Resultan útiles de manera ocasional.

Elegir la variante adecuada es una disciplina sencilla. Para un flag que los lectores consultan mediante polling, usa `atomic_load_acq`. Para un escritor que confirma un flag tras una configuración previa, usa `atomic_store_rel`. Para un contador de avance libre cuyos lectores nunca necesitan sincronizarse con un valor exacto, usa `atomic_fetchadd` (sin barreras). Para una máquina de estado basada en CAS, usa `atomic_cmpset`.

### `epoch(9)` en una página

El capítulo 14 introdujo `epoch(9)` brevemente; aquí presentamos un esbozo algo más detallado, dentro de lo que a un escritor de drivers le resulta útil conocer.

Un epoch es una barrera de sincronización de corta duración que protege estructuras de datos de lectura mayoritaria sin necesidad de locks. El código que lee datos compartidos entra en el epoch con `epoch_enter(epoch)` y sale con `epoch_exit(epoch)`. El epoch garantiza que cualquier objeto liberado mediante `epoch_call(epoch, cb, ctx)` no se reclama realmente hasta que todos los lectores que se encontraban dentro del epoch en el momento de solicitar la liberación hayan salido.

Esto es similar en espíritu al RCU (read-copy-update) de Linux, aunque con una ergonomía diferente. El epoch de FreeBSD es una herramienta más gruesa; protege estructuras grandes y poco cambiantes, como la lista de ifnet.

Un driver que quiera usar epoch normalmente no crea el suyo propio. Utiliza uno de los epochs que proporciona el kernel, siendo el más habitual `net_epoch_preempt` para el estado de red. Los escritores de drivers fuera del código de red raramente recurren a epoch directamente.

Lo que debe saber un escritor de drivers es cómo reconocer el patrón en otro código y cuándo `NET_TASK_INIT` de un taskqueue está creando una tarea que se ejecuta dentro de un epoch. El capítulo 14 ya trató este tema.

### Ordenación de memoria, algo más en profundidad

La API atómica oculta los detalles específicos de la arquitectura relativos a la ordenación de memoria. Cuando se usan `atomic_store_rel_int` y `atomic_load_acq_int` en pares correspondientes, se inserta la barrera correcta en cada arquitectura. No necesitas conocer los detalles.

Pero una intuición básica resulta de ayuda.

En x86, cada carga es una acquire-load y cada almacenamiento es un release-store, a nivel de hardware. El modelo de memoria de la CPU es el «total store order». Así que `atomic_load_acq_int` en x86 es simplemente un `MOV` sin instrucciones adicionales. `atomic_store_rel_int` también es un `MOV` simple.

En arm64, las cargas y los almacenamientos tienen un orden predeterminado más débil. El compilador inserta `LDAR` (load-acquire) para `atomic_load_acq_int` y `STLR` (store-release) para `atomic_store_rel_int`. Son baratos (unos pocos ciclos) pero no gratuitos.

La implicación es la siguiente: en cuanto a corrección, se escribe el mismo código en ambas arquitecturas. En cuanto a rendimiento, x86 no «paga» nada por las barreras mientras que arm64 paga un pequeño coste. Para una operación poco frecuente como un flag de apagado, el coste es despreciable en ambos.

La implicación adicional es que probar únicamente en x86 no es suficiente para validar la ordenación de memoria. El código que funciona en x86 con cargas simples podría provocar un deadlock o un comportamiento incorrecto en arm64 si se omitieran las barreras atómicas. FreeBSD 14.3 tiene un buen soporte para arm64; los drivers distribuidos a usuarios que ejecutan hardware arm64 deben ser correctos con el modelo de memoria más débil. Usar la API `atomic(9)` de forma consistente es la manera de garantizarlo sin tener que pensar en la arquitectura en cada llamada.

### Cuándo usar cada herramienta

Un pequeño árbol de decisión para cerrar la sección.

- **¿Proteger un invariante pequeño leído por muchos y escrito raramente?** `atomic_load_acq_int` / `atomic_store_rel_int`.
- **¿Proteger un invariante pequeño con una relación compleja con otro estado?** Mutex.
- **¿Esperar un predicado?** Mutex más variable de condición.
- **¿Esperar un predicado con gestión de señales?** Mutex más `cv_wait_sig` o `cv_timedwait_sig`.
- **¿Admitir como máximo N participantes?** `sema(9)`.
- **¿Muchos lectores o un escritor con promoción ocasional?** `sx(9)` con `sx_try_upgrade`.
- **¿Ejecutar código más adelante en contexto de thread?** `taskqueue(9)`.
- **¿Ejecutar código en un plazo determinado?** `callout(9)`.
- **¿Coordinar el apagado entre contextos?** Flag atómico + broadcast de cv (para los waiters bloqueados).
- **¿Proteger una estructura de lectura mayoritaria en código de red?** `epoch(9)`.

Ese árbol de decisión es el mapa mental que la Parte 3 ha ido construyendo. El capítulo 15 añadió las últimas ramas. El capítulo 16 aplicará el mapa al hardware.

### Cerrando los temas adicionales

Las operaciones atómicas, `epoch(9)` y la ordenación de memoria completan el conjunto de herramientas de sincronización. Para la mayoría de los drivers, el caso habitual es un mutex más una cv más atomics ocasionales; los demás primitivos existen para formas específicas. Conocer el conjunto completo permite elegir la herramienta correcta sin necesidad de adivinar.



## Laboratorios prácticos

Cuatro laboratorios aplican el material del capítulo 15 a tareas concretas. Dedica una sesión a cada laboratorio. Los laboratorios 1 y 2 son los más importantes; los laboratorios 3 y 4 suponen un reto mayor para el lector.

### Laboratorio 1: Observar la aplicación del límite de escritores

**Objetivo.** Confirmar que el semáforo de límite de escritores restringe los escritores concurrentes y que el límite es configurable en tiempo de ejecución.

**Preparación.** Construye y carga el driver de la Etapa 4. Compila el helper `writer_cap_test` a partir del código fuente adjunto.

**Pasos.**

1. Verifica el límite predeterminado: `sysctl dev.myfirst.0.writers_limit`. Debería ser 4.
2. Comprueba el valor del semáforo: `sysctl dev.myfirst.0.stats.writers_sema_value`. También debería ser 4 (sin escritores activos).
3. Inicia dieciséis escritores bloqueantes en segundo plano:
   ```text
   # for i in $(jot 16 1); do
       (cat /dev/urandom | head -c 10000 > /dev/myfirst) &
   done
   ```
4. Observa el valor del semáforo mientras se ejecutan. Debería estar cerca de cero la mayor parte del tiempo (todas las ranuras en uso).
5. Reduce el límite a 2:
   ```text
   # sysctl dev.myfirst.0.writers_limit=2
   ```
6. Observa que el valor del semáforo acaba bajando a 0 y permanece ahí (los escritores drenan más rápido de lo que vuelven a entrar).
7. Sube el límite a 8:
   ```text
   # sysctl dev.myfirst.0.writers_limit=8
   ```
8. El driver libera cuatro ranuras adicionales de inmediato; los escritores que estaban bloqueados en `sema_wait` se despiertan y entran.
9. Espera a que todos los escritores terminen; verifica que el valor final del semáforo es igual al límite actual.

**Resultado esperado.** El semáforo actúa como controlador de admisión; reconfigurarlo en tiempo de ejecución reconfigura el límite. Bajo carga elevada, el semáforo se vacía hasta cero; cuando la carga disminuye, se vuelve a llenar hasta el límite configurado.

**Variación.** Prueba con escritores no bloqueantes usando un helper que abre con `O_NONBLOCK`. Observa cómo crece `sysctl dev.myfirst.0.stats.writers_trywait_failures` cuando el semáforo está agotado.

### Laboratorio 2: Contención en la caché de estadísticas

**Objetivo.** Observar que la caché de estadísticas sirve muchas lecturas con pocas actualizaciones bajo una carga de trabajo de lectura mayoritaria.

**Preparación.** Driver de la Etapa 4 cargado. DTrace disponible.

**Pasos.**

1. Inicia 32 procesos lectores concurrentes, cada uno leyendo la estadística en caché en un bucle cerrado:
   ```text
   # for i in $(jot 32 1); do
       (while :; do
           sysctl -n dev.myfirst.0.stats.bytes_written_10s >/dev/null
       done) &
   done
   ```
2. En una terminal separada, observa la tasa de actualización de la caché mediante DTrace:
   ```text
   # dtrace -n 'fbt::myfirst_stats_cache_refresh:entry { @ = count(); }'
   ```
3. Deja que la carga de trabajo se ejecute durante 30 segundos, luego detén DTrace con Ctrl-C. Anota el recuento.

**Resultado esperado.** Aproximadamente 30 actualizaciones en 30 segundos: una por segundo, independientemente del número de lectores. Si observas muchas más actualizaciones, la caché se está invalidando de forma demasiado agresiva; si observas bastantes menos, los lectores no están activando realmente la ruta de datos obsoletos.

**Variación.** Ejecuta el test con una carga de escritura en paralelo (también usando `/dev/myfirst`). La tasa de actualización no debería cambiar: la actualización se activa por la obsolescencia de la caché, no por las escrituras.

### Laboratorio 3: Gestión de señales y progreso parcial

**Objetivo.** Confirmar que una `read(2)` interrumpida por una señal devuelve los bytes copiados hasta ese momento, no EINTR.

**Preparación.** Driver de la Etapa 4 cargado. Fuente de ticks detenida, buffer vacío.

**Pasos.**

1. Inicia un lector que solicita 4096 bytes sin tiempo de espera:
   ```text
   # dd if=/dev/myfirst bs=4096 count=1 > /tmp/out 2>&1 &
   # READER=$!
   ```
2. Activa la fuente de ticks a una velocidad lenta:
   ```text
   # sysctl dev.myfirst.0.tick_source_interval_ms=500
   ```
   El lector acumula bytes lentamente, uno cada 500 ms.
3. Tras unos 2 segundos, envía SIGINT al lector:
   ```text
   # kill -INT $READER
   ```
4. `dd` informa del número de bytes copiados antes de la señal.

**Resultado esperado.** `dd` informa de un resultado parcial (por ejemplo, 4 bytes copiados de los 4096 solicitados) y termina con 0 (éxito parcial), no con un error. El driver devolvió el recuento parcial de bytes a la capa de syscall; la capa de syscall lo presentó como una lectura corta normal.

**Variación.** Establece `read_timeout_ms` en 1000 y repite. El driver debería devolver un resultado parcial (si llegó algún byte) o EAGAIN (si el tiempo de espera expiró primero con cero bytes). La gestión de señales debería seguir preservando los bytes parciales.

### Laboratorio 4: Detach bajo carga máxima

**Objetivo.** Confirmar que el orden de detach es correcto bajo carga concurrente máxima.

**Preparación.** Driver de la Etapa 4 cargado. Todas las herramientas de estrés de los capítulos 14 y 15 compiladas.

**Pasos.**

1. Inicia el conjunto completo de herramientas de estrés:
   - 8 escritores concurrentes.
   - 4 lectores concurrentes.
   - Fuente de ticks a 1 ms.
   - Heartbeat a 100 ms.
   - Watchdog a 1 s.
   - Flood de sysctl cada 100 ms: `bulk_writer_flood=1000`.
   - Flood de sysctl cada 500 ms: ajusta `writers_limit` entre 1 y 8.
   - Lecturas sysctl concurrentes de `stats.bytes_written_10s`.
2. Deja que el estrés se ejecute durante 30 segundos para garantizar la carga máxima.
3. Descarga el driver:
   ```text
   # kldunload myfirst
   ```
4. Termina los procesos de estrés. Observa que la descarga se completó correctamente.

**Resultado esperado.** La descarga se completa correctamente (sin EBUSY, sin pánico, sin cuelgue). Todos los procesos de estrés fallan de forma controlada (open devuelve ENXIO, las lecturas/escrituras pendientes devuelven ENXIO o resultados cortos). `dmesg` no muestra advertencias de `WITNESS`.

**Variación.** Repite el ciclo 20 veces (carga, estrés, descarga). Cada ciclo debería comportarse de forma idéntica. Si algún ciclo provoca un pánico o un cuelgue, existe una condición de carrera; investígala.



## Ejercicios de desafío

Los desafíos van más allá del contenido del capítulo. Son opcionales; cada uno consolida una idea específica del capítulo 15.

### Desafío 1: Reemplazar sema con espera interrumpible

El límite de escritores usa `sema_wait`, que no es interrumpible por señales. Reescribe el control de admisión de la ruta de escritura para usar `sema_trywait` más un bucle `cv_wait_sig` interrumpible. Preserva la convención de progreso parcial.

Resultado esperado: un escritor bloqueado esperando un slot puede ser interrumpido con SIGINT de forma limpia. Bonus: usa la encapsulación en `myfirst_sync.h` para que `myfirst_sync_writer_enter` tenga la misma firma pero distintas entrañas.

### Desafío 2: Caché de lectura mayoritaria con actualización en segundo plano

La caché de estadísticas se actualiza bajo demanda cuando un lector detecta obsolescencia. Cambia el diseño para que la caché se actualice mediante un callout periódico y los lectores nunca activen una actualización. Compara el código resultante con el patrón upgrade-promote-downgrade.

Resultado esperado: comprender cuándo la caché bajo demanda es mejor que la caché en segundo plano. Respuesta: la caché bajo demanda es más sencilla (no hay que gestionar el ciclo de vida del callout) pero malgasta una actualización en una caché que nadie lee. La caché en segundo plano es más predecible pero requiere el primitivo adicional. La mayoría de los drivers optan por la caché bajo demanda para caches pequeñas y la de segundo plano para las grandes.

### Desafío 3: Múltiples semáforos de límite de escritores

Imagina que el driver está apilado sobre un backend de almacenamiento que tiene grupos separados para diferentes clases de I/O: «escrituras pequeñas» y «escrituras grandes». Añade un segundo semáforo que limita las escrituras grandes por separado, con su propio límite. Los escritores que entran en la ruta de escritura eligen qué semáforo adquirir en función de su `uio_resid`.

Resultado esperado: experiencia práctica con múltiples semáforos. Reflexiona sobre lo siguiente: si un escritor adquiere el semáforo «grande» y resulta que el `uio` es pequeño, ¿quiere la ruta de escritura liberar y readquirir? ¿O mantener el slot adquirido? Documenta tu elección.

### Desafío 4: Eliminación del flag de recuperación mediante atomics

Reemplaza el flag de estado `recovery_in_progress` por un compare-and-swap atómico. El watchdog ejecuta `atomic_cmpset_int(&sc->recovery_in_progress, 0, 1)`; si tiene éxito, encola la tarea. La tarea borra el flag con `atomic_store_rel_int`.

Resultado esperado: el mecanismo de recuperación ya no requiere el mutex. Compara las dos implementaciones en términos de corrección, complejidad y observabilidad.

### Desafío 5: Epoch para una ruta de lectura hipotética

Estudia `/usr/src/sys/sys/epoch.h` y `/usr/src/sys/kern/subr_epoch.c`. Esboza (en comentarios, no en código) cómo convertirías la ruta de lectura para usar un epoch privado que proteja un puntero a la «configuración actual» que los escritores actualizan ocasionalmente.

Resultado esperado: una propuesta escrita con las compensaciones. Dado que la configuración actual del driver es pequeña y sx ya la gestiona bien, esto es un ejercicio teórico más que una refactorización práctica. El objetivo es entender cuándo epoch supone una mejora.

### Desafío 6: Prueba de estrés de una condición de carrera específica

Elige una condición de carrera de la Sección 7. Escribe un script que la dispare de forma fiable. Verifica que el driver de la Etapa 4 no manifiesta el bug. Verifica que una versión rota a propósito (revirtiendo la corrección) sí lo manifiesta.

Resultado esperado: la satisfacción de ver una condición de carrera producida bajo demanda, y la tranquilidad de saber que el código de producción la maneja correctamente.

### Desafío 7: Lee el vocabulario de sincronización de un driver real

Abre `/usr/src/sys/dev/bge/if_bge.c` (o un driver de tamaño medio similar que use muchos primitivos). Recorre sus rutas de attach y detach. Cuenta:

- Mutexes.
- Variables de condición.
- Sx locks.
- Semáforos (si los hay; muchos drivers no tienen ninguno).
- Callouts.
- Tareas.
- Operaciones atómicas.

Escribe un resumen de una página sobre la estrategia de sincronización del driver. Compárala con la de `myfirst`. ¿Qué hace el driver real de forma diferente, y por qué?

Resultado esperado: leer la estrategia de sincronización de un driver real es la manera más rápida de que la tuya propia te resulte familiar. Después de una sola lectura de este tipo, abrir cualquier otro driver en `/usr/src/sys/dev/` se vuelve mucho más sencillo.



## Referencia de resolución de problemas

Una lista de referencia para los problemas habituales del Capítulo 15.

### Deadlock o fuga de semáforo

- **Los escritores se acumulan y nunca avanzan.** El contador del semáforo es cero y nadie hace el post. Comprueba: ¿cada `sema_wait` tiene su `sema_post` correspondiente? ¿Hay alguna ruta de retorno anticipado que olvide el post?
- **`sema_destroy` entra en pánico con la aserción "waiters".** El destroy se ejecutó mientras un thread seguía dentro de `sema_wait`. Solución: asegúrate de que la ruta de detach deja en reposo a todos los posibles waiters antes de destruir. Normalmente esto implica limpiar `is_attached` y hacer un broadcast de la cv primero.
- **`sema_trywait` devuelve valores inesperados.** Recuerda: 1 en caso de éxito, 0 en caso de fallo. Es la inversa de la mayoría de APIs de FreeBSD. Revisa la lógica del sitio de llamada.

### Problemas con el Sx lock

- **`sx_try_upgrade` falla siempre.** Es probable que el thread que llama comparta el sx con otro lector. Comprueba: ¿hay alguna ruta que mantenga `sx_slock` de forma persistente en un thread diferente?
- **Deadlock entre el sx y otro lock.** Violación del orden de locks. Ejecuta bajo `WITNESS`; el kernel nombrará la violación.
- **`sx_downgrade` sin un par `sx_try_upgrade`.** Asegúrate de que el sx se mantiene en modo exclusivo antes de hacer el downgrade. `sx_xlocked(&sx)` lo verifica con una aserción.

### Problemas con el manejo de señales

- **`read(2)` no responde a SIGINT.** La espera bloqueante es `cv_wait` (no `cv_wait_sig`), o `sema_wait`. Conviértela a la variante con interrupción por señal.
- **`read(2)` devuelve EINTR con bytes parciales perdidos.** Falta la comprobación de progreso parcial. Añade la verificación `uio_resid != nbefore` en la ruta de error por señal.
- **`read(2)` entra en bucle tras EINTR.** El bucle continúa al recibir el error de señal en lugar de retornar. Añade el manejo de EINTR/ERESTART.

### Atomicidad y ordenamiento de memoria

- **La comprobación del flag de apagado falla.** Un contexto lee `sc->is_attached` con una carga normal y obtiene un valor obsoleto. Conviértela a `atomic_load_acq_int`.
- **El orden de escritura no se respeta en arm64.** Las escrituras fueron reordenadas entre arquitecturas. Usa `atomic_store_rel_int` para la última escritura en la secuencia previa al detach.

### Bugs de coordinación

- **La tarea de recuperación se ejecuta varias veces.** El flag de estado no está protegido o el CAS atómico se usa de forma incorrecta. Usa el patrón de flag protegido por mutex o revisa la lógica del CAS.
- **La tarea de recuperación nunca se ejecuta.** El flag nunca se limpia, o el watchdog no está encolando. Comprueba qué lado tiene el error con un `device_printf` en cada ruta.

### Problemas de pruebas

- **La prueba de estrés pasa a veces y falla otras.** Una condición de carrera de baja probabilidad. Ejecuta muchas más iteraciones, aumenta la concurrencia o añade ruido de temporización (`usleep` en puntos aleatorios) para hacerla aflorar.
- **Las sondas de DTrace no se disparan.** El kernel se compiló sin sondas FBT, o la función fue integrada en línea por el compilador. Comprueba con `dtrace -l | grep myfirst`.
- **Las advertencias de WITNESS inundan los registros.** No las ignores. Cada advertencia es un error real. Corrígelos uno a uno e itera.

### Problemas en el detach

- **`kldunload` devuelve EBUSY.** Todavía existen descriptores de archivo abiertos. Ciérralos y vuelve a intentarlo.
- **`kldunload` se bloquea.** Un drain está esperando a que complete un primitivo que no puede terminar. Suele ser una tarea o un callout. Usa `procstat -kk` sobre el thread de kldunload para averiguar dónde está bloqueado.
- **El kernel entra en pánico durante la descarga.** Use-after-free en una tarea o un callback de callout; el orden es incorrecto. Revisa la secuencia de detach en `LOCKING.md` frente al código real.



## Cerrando la Parte 3

El Capítulo 15 es el último capítulo de la Parte 3. La Parte 3 tenía una misión específica: dotar al driver `myfirst` de una historia de sincronización completa, desde el primer mutex del Capítulo 11 hasta el último flag atómico del Capítulo 15. La misión está cumplida.

Un breve inventario de lo que entregó la Parte 3.

### El mutex (Capítulo 11)

El primer primitivo. Un sleep mutex protege los datos compartidos del driver frente al acceso concurrente. Cada ruta que toca el cbuf, el contador de aperturas, el contador de fh activos u otros campos protegidos por el mutex adquiere `sc->mtx` primero. `WITNESS` impone la regla.

### La variable de condición (Capítulo 12)

El primitivo de espera. Dos cvs (`data_cv`, `room_cv`) permiten que los lectores y escritores duerman hasta que el estado del buffer sea aceptable. `cv_wait_sig` y `cv_timedwait_sig` hacen que las esperas sean interrumpibles por señales y estén acotadas en el tiempo.

### El lock compartido/exclusivo (Capítulo 12)

El primitivo de lectura mayoritaria. `sc->cfg_sx` protege la estructura de configuración. Adquisición compartida para lecturas, exclusiva para escrituras. El Capítulo 15 añadió `sc->stats_cache_sx` con el patrón de upgrade-promote-downgrade.

### El callout (Capítulo 13)

El primitivo de tiempo. Tres callouts (heartbeat, watchdog, fuente de ticks) dan al driver tiempo interno sin necesidad de un thread dedicado. `callout_init_mtx` los hace conscientes del lock; `callout_drain` permite desmontarlos de forma segura.

### El taskqueue (Capítulo 14)

El primitivo de trabajo diferido. Un taskqueue privado con tres tareas (selwake, escritor masivo, recuperación) mueve el trabajo desde contextos restringidos a contexto de thread. La secuencia de detach drena cada tarea antes de liberar la cola.

### El semáforo (Capítulo 15)

El primitivo de admisión acotada. `writers_sema` limita los escritores concurrentes. La API es pequeña y sin propiedad; el driver usa `sema_trywait` para entradas no bloqueantes y `sema_wait` para bloqueantes.

### Las operaciones atómicas (Capítulo 15)

El flag entre contextos. `atomic_load_acq_int` y `atomic_store_rel_int` sobre `is_attached` hacen que el flag de apagado sea visible para todos los contextos con el ordenamiento de memoria correcto.

### La encapsulación (Capítulo 15)

El primitivo de mantenimiento. `myfirst_sync.h` envuelve cada operación de sincronización en una función con nombre. Un lector futuro comprende la estrategia de sincronización del driver leyendo una sola cabecera.

### Lo que el driver puede hacer ahora

Un breve inventario de las capacidades del driver al final de la Parte 3:

- Atender lectores y escritores concurrentes en un buffer en anillo acotado.
- Bloquear a los lectores hasta que lleguen datos, con timeout opcional.
- Bloquear a los escritores hasta que haya espacio disponible, con timeout opcional.
- Limitar el número de escritores concurrentes mediante un semáforo configurable.
- Exponer configuración (nivel de depuración, apodo, límite de bytes blando) protegida por un sx lock.
- Emitir una línea de registro periódica de heartbeat.
- Detectar el estancamiento del drenaje del buffer mediante un watchdog.
- Inyectar datos sintéticos a través de un callout como fuente de ticks.
- Diferir `selwakeup` fuera del callback del callout mediante una tarea.
- Demostrar la coalescencia de tareas mediante un escritor masivo configurable.
- Programar un reinicio diferido mediante una tarea con timeout.
- Exponer una estadística en caché mediante un patrón sx consciente del upgrade.
- Coordinar el detach entre todos los primitivos sin condiciones de carrera.
- Responder correctamente a las señales durante las operaciones bloqueantes.
- Respetar la semántica de progreso parcial en lectura y escritura.

Eso es un driver considerable. El módulo `myfirst` en `0.9-coordination` es un ejemplo compacto pero completo de los patrones de sincronización que utiliza cualquier driver real de FreeBSD. Los patrones son transferibles.

### Lo que la Parte 3 no cubrió

Una breve lista de temas que la Parte 3 dejó deliberadamente para partes posteriores:

- Interrupciones de hardware (Parte 4).
- Acceso a registros con mapeado de memoria (Parte 4).
- Operaciones de DMA y bus space (Parte 4).
- Emparejamiento de dispositivos PCI (Parte 4).
- Subsistemas específicos de USB y red (Parte 6).
- Ajuste avanzado del rendimiento (capítulos especializados más adelante).

La Parte 3 se centró en la historia de sincronización interna. La Parte 4 añadirá la historia orientada al hardware. La historia de sincronización no desaparece; se convierte en la base sobre la que descansa la historia del hardware.

### Una reflexión

Empezaste el Capítulo 11 con un driver que admitía un solo usuario a la vez. Terminas el Capítulo 15 con un driver que admite muchos usuarios, coordina varios tipos de trabajo y se desmonta de forma limpia bajo carga. Por el camino aprendiste los principales primitivos de sincronización del kernel, cada uno introducido con una invariante específica en mente, cada uno compuesto con lo que vino antes.

El patrón de aprendizaje fue deliberado. Cada capítulo introdujo un nuevo concepto, lo aplicó al driver en una pequeña refactorización, lo documentó en `LOCKING.md` y añadió pruebas de regresión. El resultado es un driver cuya sincronización no es un accidente. Cada primitivo está ahí por una razón; cada primitivo está documentado; cada primitivo está probado.

Esa disciplina es lo más duradero que enseña la Parte 3. Los primitivos concretos (mutex, cv, sx, callout, taskqueue, sema, atómicos) son la moneda, pero la disciplina de "elige el primitivo correcto, documéntalo, pruébalo" es la inversión. Los drivers construidos con esa disciplina sobreviven al crecimiento, los traspasos de mantenimiento y los patrones de carga inesperados. Los drivers construidos sin ella acumulan errores sutiles y fallos difíciles de explicar.

La Parte 4 abre la puerta al hardware. Los primitivos que ya conoces te acompañan. La disciplina que has practicado es lo que te permitirá añadir la historia orientada al hardware sin perderte.

Tómate un momento. Es un logro real. Luego pasa al Capítulo 16.

## Checkpoint de la Parte 3

Cinco capítulos de sincronización son mucho material. Antes de que la Parte 4 abra la puerta al hardware, vale la pena confirmar que los primitivos y la disciplina han calado.

Al final de la Parte 3 deberías ser capaz de hacer cada una de las siguientes cosas con confianza:

- Elegir entre `mutex(9)`, `sx(9)`, `rw(9)`, `cv(9)`, `callout(9)`, `taskqueue(9)`, `sema(9)` y la familia `atomic(9)` con un criterio claro sobre para qué invariante es apropiado cada uno, en lugar de hacerlo por hábito o intuición.
- Documentar el locking de un driver en un `LOCKING.md` que nombre cada primitivo, la invariante que impone, los datos que protege y las reglas que deben seguir quienes lo llaman.
- Implementar un handshake de sleep-and-wake usando `mtx_sleep`/`wakeup` o `cv_wait`/`cv_signal`, y explicar por qué elegiste uno u otro.
- Programar trabajo temporizado con `callout(9)`, incluida la cancelación durante el detach, sin dejar timers colgados.
- Diferir trabajo pesado u ordenado a través de `taskqueue(9)`, incluido el drain en el momento del detach que evita que las tareas se ejecuten contra estado liberado.
- Mantener un `myfirst` funcionando limpiamente bajo kernels con `INVARIANTS` y `WITNESS` mientras las pruebas de estrés multihilo golpean cada punto de entrada.

Si alguno de esos puntos aún no está claro, vuelve a los laboratorios que los introdujeron:

- Disciplina de locking y regresión: Lab 11.2 (Verifica la disciplina de locking con INVARIANTS), Lab 11.4 (Construye un tester multihilo) y Lab 11.7 (Estrés prolongado).
- Variables de condición y sx: Lab 12.2 (Añade lecturas acotadas), Lab 12.5 (Detecta una inversión deliberada del orden de locks) y Lab 12.7 (Verifica que el patrón snapshot-and-apply aguanta bajo contención).
- Callouts y trabajo temporizado: Lab 13.1 (Añade un heartbeat callout) y Lab 13.4 (Verifica el detach con callouts activos).
- Taskqueues y trabajo diferido: Lab 2 (Medir la coalescencia bajo carga) y Lab 3 (Verificar el orden del detach) en el Capítulo 14.
- Semáforos: Lab 1 (Observa el cumplimiento del límite de escritores) y Lab 4 (Detach bajo carga máxima) en el Capítulo 15.

La Parte 4 añadirá hardware a todo lo que la Parte 3 acaba de construir. En concreto, los capítulos que siguen esperarán que:

- El modelo de sincronización interiorizado en lugar de memorizado, de modo que un contexto de interrupción pueda añadirse como un tipo más de llamador y no como un universo nuevo de reglas.
- El orden de detach tratado como una disciplina única y compartida entre primitivas, dado que la Parte 4 añadirá el desmontaje de interrupciones y la liberación de recursos de bus en la misma cadena.
- Comodidad continua con `INVARIANTS` y `WITNESS` como kernel de desarrollo predeterminado, dado que los errores más difíciles de la Parte 4 suelen activar uno de los dos mucho antes de que afloren como un panic visible.

Si se cumplen, la Parte 4 está al alcance. Si alguno todavía resulta inestable, la solución es dar una vuelta por el laboratorio correspondiente en lugar de forzar el avance.

## Puente al capítulo 16

El capítulo 16 abre la parte 4 del libro. La parte 4 lleva por título *Integración con hardware y plataforma*, y el capítulo 16 es *Fundamentos de hardware y Newbus*. La misión de la parte 4 es proporcionar al driver una historia de hardware: cómo un driver se anuncia a la capa de bus del kernel, cómo se empareja con el hardware que el kernel ha descubierto, cómo recibe interrupciones, cómo accede a los registros mapeados en memoria y cómo gestiona DMA.

La historia de sincronización de la parte 3 no desaparece. Se convierte en la base sobre la que la parte 4 construye. Un manejador de interrupción de hardware se ejecuta en un contexto sobre el que ya sabes razonar (sin dormir, sin locks que puedan dormir, sin `uiomove`). Se comunica con el resto del driver a través de primitivas que ya sabes usar (taskqueues para trabajo diferido, mutexes para serialización, flags atómicos para el apagado). La diferencia es que el driver ahora también tiene que hablar directamente con el hardware, y el hardware tiene sus propias reglas.

El capítulo 16 prepara el terreno del hardware de tres maneras concretas.

En primer lugar, **ya conoces los límites de contexto**. La parte 3 te enseñó que los callouts, las tasks y los threads de usuario tienen sus propias reglas. Las interrupciones añaden un contexto más con reglas más estrictas. El modelo mental («¿en qué contexto estoy?, ¿qué puedo hacer aquí de forma segura?») se transfiere directamente.

En segundo lugar, **ya conoces el orden de detach**. La parte 3 construyó una disciplina de detach a lo largo de cinco primitivas. La parte 4 añade dos más (desmontaje de interrupciones, liberación de recursos) que encajan en esa misma disciplina. Las reglas de orden crecen; la forma no.

En tercer lugar, **ya conoces `LOCKING.md` como documento vivo**. El capítulo 16 añade una sección de recursos de hardware. La disciplina es la misma; el vocabulario se amplía.

Los temas concretos que cubrirá el capítulo 16:

- El framework `newbus(9)`: cómo se identifican, prueban y adjuntan los drivers.
- `device_t`, `devclass`, `driver_t` y `device_method_t`.
- `bus_alloc_resource` y `bus_release_resource` para regiones mapeadas en memoria, líneas IRQ y otros recursos.
- `bus_setup_intr` y `bus_teardown_intr` para el registro de interrupciones.
- Manejadores de filtro frente a threads de interrupción.
- La relación entre newbus y el subsistema PCI (preparando el capítulo 17).

No necesitas leer por adelantado. El capítulo 15 es preparación suficiente. Trae tu driver `myfirst` en `0.9-coordination`, tu `LOCKING.md`, tu kernel con `WITNESS` habilitado y tu kit de pruebas. El capítulo 16 empieza donde terminó el capítulo 15.

Una pequeña reflexión final. El driver con el que comenzaste la parte 3 sabía cómo atender una syscall. El driver que tienes ahora tiene una historia completa de sincronización interna, con seis tipos de primitivas, cada una seleccionada para una forma específica de invariante, cada una encapsulada en una capa de envoltura legible, cada una documentada, cada una probada. Está listo para enfrentarse al hardware.

El hardware es el próximo capítulo. Luego el siguiente. Luego cada capítulo de la parte 4. Los cimientos están construidos. Las herramientas están en el banco. El plano está listo.

Da la vuelta a la página.



## Referencia: auditoría de sincronización antes de producción

Antes de publicar un driver con sincronización intensiva, recorre esta auditoría. Cada elemento es una pregunta; cada una debería poder responderse con confianza.

### Auditoría de mutex

- [ ] ¿Toda región donde se retiene `sc->mtx` termina con un `mtx_unlock` en todos los caminos posibles?
- [ ] ¿Se libera siempre el mutex antes de llamar a `uiomove`, `copyin`, `copyout`, `selwakeup` o cualquier otra operación que pueda dormir?
- [ ] ¿Existe algún lugar donde el mutex se mantiene durante una espera en cv? En ese caso, ¿es la espera la primitiva adecuada?
- [ ] ¿Se respeta el orden de locks `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx` en todos los sitios?

### Auditoría de variable de condición

- [ ] ¿Toda espera en cv llama a `cv_wait_sig` o `cv_timedwait_sig` en un contexto de syscall?
- [ ] ¿Se gestiona EINTR con preservación de progreso parcial donde corresponde?
- [ ] ¿Se propaga correctamente ERESTART?
- [ ] ¿Tiene cada cv un broadcast correspondiente en detach?
- [ ] ¿Se realizan los wakeups con el mutex tomado por corrección (o con el mutex liberado para mayor rendimiento, con el trade-off documentado)?

### Auditoría de sx

- [ ] ¿Todo `sx_slock` tiene un `sx_sunlock` correspondiente?
- [ ] ¿Todo `sx_xlock` tiene un `sx_xunlock` correspondiente?
- [ ] ¿El camino de fallo de todo `sx_try_upgrade` gestiona correctamente la recomprobación tras soltar y readquirir?
- [ ] ¿Todo `sx_downgrade` ocurre sobre un lock que realmente se tiene en modo exclusivo?

### Auditoría de semáforo

- [ ] ¿Todo `sema_wait` tiene un `sema_post` correspondiente en todos los caminos?
- [ ] ¿El semáforo se destruye únicamente después de que todos los waiters hayan sido liberados?
- [ ] ¿Se lee correctamente el valor de retorno de `sema_trywait` (1 en éxito, 0 en fallo)?
- [ ] Si el semáforo se usa con syscalls interrumpibles, ¿está documentada la no interrumpibilidad de `sema_wait`?
- [ ] ¿Existe un contador de operaciones en curso (p. ej., `writers_inflight`) que detach drena antes de llamar a `sema_destroy`?
- [ ] ¿Todo camino entre el incremento y el decremento del contador usa realmente el sema (sin retornos anticipados que se salten el incremento)?

### Auditoría de callout

- [ ] ¿Todo callout usa `callout_init_mtx` con el lock adecuado?
- [ ] ¿Todo callback de callout comprueba `is_attached` y sale antes si es falso?
- [ ] ¿Todo callout se drena en detach antes de que se libere el estado que toca?

### Auditoría de task

- [ ] ¿Todo callback de task toma los locks adecuados al tocar estado compartido?
- [ ] ¿Todo callback de task llama a `selwakeup` únicamente sin ningún lock del driver tomado?
- [ ] ¿Toda task se drena en detach después de haber drenado los callouts que la encolan?
- [ ] ¿Se libera la taskqueue privada después de drenar todas las tasks?

### Auditoría de atómicos

- [ ] ¿Toda lectura de flag de apagado usa `atomic_load_acq_int`?
- [ ] ¿Toda escritura de flag de apagado usa `atomic_store_rel_int`?
- [ ] ¿Están justificadas otras operaciones atómicas por un requisito específico de ordenación de memoria?

### Auditoría entre componentes

- [ ] ¿Tiene cada flag de estado entre componentes una propiedad clara (qué camino lo activa, qué camino lo limpia)?
- [ ] ¿Están los flags protegidos por el lock adecuado o por disciplina atómica?
- [ ] ¿Está el protocolo de sincronización documentado en `LOCKING.md`?

### Auditoría de documentación

- [ ] ¿Lista `LOCKING.md` todas las primitivas?
- [ ] ¿Documenta `LOCKING.md` el orden de detach?
- [ ] ¿Documenta `LOCKING.md` el orden de locks?
- [ ] ¿Se explican los protocolos sutiles entre componentes?

### Auditoría de pruebas

- [ ] ¿Se ha ejecutado el driver bajo `WITNESS` durante una prueba de estrés prolongada sin advertencias?
- [ ] ¿Se han probado los ciclos de detach bajo carga completa?
- [ ] ¿Han confirmado las pruebas de interrupción por señal la preservación del progreso parcial?
- [ ] ¿Se han probado los cambios de configuración en tiempo de ejecución (ajuste de sysctl)?

Un driver que supera esta auditoría es un driver que puedes publicar.



## Referencia: hoja de referencia rápida de primitivas de sincronización

### Cuándo usar cuál

| Primitiva | Mejor para | No adecuada para |
|---|---|---|
| `struct mtx` (MTX_DEF) | Secciones críticas cortas; exclusión mutua. | Esperar una condición. |
| `struct cv` + mtx | Esperar un predicado; wakeups por señal. | Admisión acotada. |
| `struct sx` | Estado de acceso predominantemente lector; lecturas compartidas con escrituras ocasionales. | Contención elevada. |
| `struct sema` | Admisión acotada; señalización de finalización productor-consumidor. | Esperas interrumpibles. |
| `callout` | Trabajo basado en tiempo. | Trabajo que debe dormir. |
| `taskqueue` | Trabajo diferido en contexto de thread. | Latencia de submicrosegundos. |
| `atomic_*` | Pequeños flags entre contextos; coordinación sin lock. | Invariantes complejos. |
| `epoch` | Estructuras compartidas de acceso predominantemente lector en código de red. | Drivers sin estructuras compartidas. |

### Referencia rápida de API

**Mutex.**
- `mtx_init(&mtx, name, type, MTX_DEF)`
- `mtx_lock(&mtx)`, `mtx_unlock(&mtx)`
- `mtx_assert(&mtx, MA_OWNED)`
- `mtx_destroy(&mtx)`

**Variable de condición.**
- `cv_init(&cv, name)`
- `cv_wait(&cv, &mtx)`, `cv_wait_sig`
- `cv_timedwait(&cv, &mtx, timo)`, `cv_timedwait_sig`
- `cv_signal(&cv)`, `cv_broadcast(&cv)`
- `cv_destroy(&cv)`

**Sx.**
- `sx_init(&sx, name)`
- `sx_slock(&sx)`, `sx_sunlock(&sx)`
- `sx_xlock(&sx)`, `sx_xunlock(&sx)`
- `sx_try_upgrade(&sx)`, `sx_downgrade(&sx)`
- `sx_xlocked(&sx)`, `sx_xholder(&sx)`
- `sx_destroy(&sx)`

**Semáforo.**
- `sema_init(&s, value, name)`
- `sema_wait(&s)`, `sema_timedwait(&s, timo)`, `sema_trywait(&s)`
- `sema_post(&s)`
- `sema_value(&s)`
- `sema_destroy(&s)`

**Callout.**
- `callout_init_mtx(&co, &mtx, 0)`
- `callout_reset(&co, ticks, fn, arg)`
- `callout_stop(&co)`, `callout_drain(&co)`

**Taskqueue.**
- `TASK_INIT(&t, 0, fn, ctx)`, `TIMEOUT_TASK_INIT(...)`
- `taskqueue_create(name, flags, enqueue, ctx)`
- `taskqueue_start_threads(&tq, count, pri, name, ...)`
- `taskqueue_enqueue(tq, &t)`, `taskqueue_enqueue_timeout(...)`
- `taskqueue_cancel(tq, &t, &pend)`, `taskqueue_drain(tq, &t)`
- `taskqueue_free(tq)`

**Atómicas.**
- `atomic_load_acq_int(p)`, `atomic_store_rel_int(p, v)`
- `atomic_fetchadd_int(p, v)`, `atomic_cmpset_int(p, old, new)`
- `atomic_set_int(p, v)`, `atomic_clear_int(p, v)`
- `atomic_thread_fence_seq_cst()`

### Reglas de contexto

| Contexto | ¿Puede dormir? | ¿Locks suspendibles? | Notas |
|---|---|---|---|
| Syscall | Sí | Sí | Contexto de thread completo. |
| Callback de callout (con lock) | No | No | Mutex asociado tomado. |
| Callback de task (respaldado por thread) | Sí | Sí | Sin lock del driver tomado. |
| Callback de task (fast/swi) | No | No | Contexto SWI. |
| Filtro de interrupción | No | No | Muy limitado. |
| Thread de interrupción | No | No | Algo más que el filtro. |
| Sección epoch | No | No | Muy limitado. |

### Convención de progreso parcial

Una llamada `read(2)` o `write(2)` que copia N bytes antes de ser interrumpida debe devolver N (como una lectura/escritura parcial corta exitosa), no EINTR. El helper de espera del driver devuelve un valor centinela (`MYFIRST_WAIT_PARTIAL`) en el camino parcial; quien lo llama lo convierte a 0 para que la capa de syscall devuelva el conteo de bytes.

### Orden de detach

El orden canónico de detach para el capítulo 15:

1. Rechazar si `active_fhs > 0`.
2. Limpiar `is_attached` de forma atómica.
3. Hacer broadcast a todos los cv; liberar el mutex.
4. Drenar todos los callouts.
5. Drenar todas las tasks (incluidas las timeout tasks y las de recuperación).
6. `seldrain` para rsel, wsel.
7. Liberar la taskqueue.
8. Destruir el semáforo.
9. Destruir el sx de la caché de estadísticas.
10. Destruir cdev, sysctls, cbuf, contadores.
11. Destruir los cv, el sx de configuración y el mutex.

Memoriza la forma. Adapta el orden cuando añadas nuevas primitivas.



## Referencia: lecturas adicionales

### Páginas del manual

- `sema(9)`: semáforos del kernel.
- `sx(9)`: locks compartidos/exclusivos.
- `mutex(9)`: primitivas de mutex.
- `condvar(9)`: variables de condición.
- `atomic(9)`: operaciones atómicas.
- `epoch(9)`: sincronización basada en épocas.
- `locking(9)`: visión general de las primitivas de sincronización del kernel.

### Archivos fuente

- `/usr/src/sys/kern/kern_sema.c`: implementación del semáforo.
- `/usr/src/sys/sys/sema.h`: API del semáforo.
- `/usr/src/sys/kern/kern_sx.c`: implementación de sx.
- `/usr/src/sys/sys/sx.h`: API de sx.
- `/usr/src/sys/kern/kern_mutex.c`: implementación del mutex.
- `/usr/src/sys/kern/kern_condvar.c`: implementación de cv.
- `/usr/src/sys/kern/subr_epoch.c`: implementación de epoch.
- `/usr/src/sys/sys/epoch.h`: API de epoch.
- `/usr/src/sys/dev/hyperv/storvsc/hv_storvsc_drv_freebsd.c`: uso real de `sema`.
- `/usr/src/sys/dev/bge/if_bge.c`: ejemplo rico de sincronización.

### Libros y material externo

- *The Design and Implementation of the FreeBSD Operating System* (McKusick et al.): contiene capítulos detallados sobre los subsistemas de sincronización del kernel.
- *FreeBSD Handbook*, sección de desarrolladores: secciones sobre los locks del kernel.
- Archivos de listas de correo de FreeBSD: las búsquedas por nombres de primitivas (`taskqueue`, `sema`, `sx`) revelan discusiones históricas de diseño.

### Orden de lectura sugerido

Para un lector nuevo en sincronización avanzada:

1. `mutex(9)`, `condvar(9)`: las primitivas básicas.
2. `sx(9)`: la primitiva para acceso predominantemente lector.
3. `sema(9)`: la primitiva de admisión acotada.
4. `atomic(9)`: la herramienta entre contextos.
5. `epoch(9)`: la herramienta de lectura sin lock para drivers de red.
6. Un código fuente real de driver: `/usr/src/sys/dev/bge/if_bge.c` o similar.

Leerlos en orden lleva toda una tarde y te da un mapa mental sólido.

## Referencia: glosario de términos del capítulo 15

**Semáforo de conteo.** Una primitiva que contiene un entero no negativo y admite las operaciones wait (decrementar, bloqueando si es cero) y post (incrementar, despertando a un hilo en espera).

**Semáforo binario.** Un semáforo de conteo que solo contiene 0 o 1. Funcionalmente similar a un mutex, pero sin propietario.

**Herencia de prioridad.** Una técnica del planificador en la que un thread de alta prioridad que espera un lock eleva temporalmente la prioridad del poseedor actual. Los mutex de FreeBSD la implementan; los semáforos no.

**Espera interrumpible por señal.** Una primitiva de bloqueo (p. ej., `cv_wait_sig`) que retorna con EINTR o ERESTART cuando llega una señal. El invocante puede entonces abandonar la espera y hacer visible la señal.

**Convención de progreso parcial.** Comportamiento estándar de UNIX: un `read(2)` o `write(2)` que transfirió algunos bytes y fue interrumpido devuelve el recuento de bytes como éxito, no como error.

**EINTR vs ERESTART.** Dos códigos de retorno de señal. EINTR se propaga al espacio de usuario como errno EINTR. ERESTART provoca que la capa de syscall reinicie la llamada al sistema de forma transparente, según la disposición de la señal.

**Barrera de adquisición.** Una barrera de memoria sobre una carga que impide que los accesos a memoria posteriores se reordenen antes de dicha carga.

**Barrera de liberación.** Una barrera de memoria sobre un almacenamiento que impide que los accesos a memoria precedentes se reordenen después de dicho almacenamiento.

**Comparar e intercambiar (CAS).** Una operación atómica que escribe un nuevo valor solo si el valor actual coincide con el esperado. Base para las máquinas de estado sin lock.

**Upgrade-promote-downgrade.** Un patrón de sx: adquirir en modo compartido, detectar la necesidad de escritura, intentar hacer upgrade a exclusivo, escribir, hacer downgrade de vuelta a compartido.

**Coalescencia.** Propiedad del taskqueue: los encolados redundantes de la misma tarea se fusionan en un único estado pendiente con el contador incrementado, en lugar de enlazarse por separado.

**Capa de encapsulación.** Un archivo de cabecera (`myfirst_sync.h`) que nombra cada operación de sincronización que realiza el driver, de modo que la estrategia puede cambiarse en un único lugar y entenderse de un vistazo.

**Bandera de estado.** Un entero pequeño en el softc que registra si una condición específica está en curso. Protegido por el lock o la disciplina atómica correspondiente.

**Protocolo de sincronización entre componentes.** Una coordinación entre múltiples contextos de ejecución (callout, task, thread de usuario) mediante una bandera de estado, un cv o una variable atómica.



El capítulo 15 termina aquí. La parte 4 comienza a continuación.


## Referencia: lectura de `kern_sema.c` línea a línea

La implementación de `sema(9)` es lo bastante breve para leerla de principio a fin. Hacerlo una vez consolida el modelo mental de lo que realmente hace la primitiva. El archivo es `/usr/src/sys/kern/kern_sema.c`, de menos de doscientas líneas. A continuación se ofrece un recorrido comentado.

### `sema_init`

```c
void
sema_init(struct sema *sema, int value, const char *description)
{

        KASSERT((value >= 0), ("%s(): negative value\n", __func__));

        bzero(sema, sizeof(*sema));
        mtx_init(&sema->sema_mtx, description, "sema backing lock",
            MTX_DEF | MTX_NOWITNESS | MTX_QUIET);
        cv_init(&sema->sema_cv, description);
        sema->sema_value = value;

        CTR4(KTR_LOCK, "%s(%p, %d, \"%s\")", __func__, sema, value, description);
}
```

Seis líneas de lógica. Verifica que el valor inicial no sea negativo. Inicializa a cero la estructura. Inicializa el mutex interno; nótense los flags `MTX_NOWITNESS | MTX_QUIET`, que indican a `WITNESS` que no rastree el mutex interno (pues lo que le importa al usuario es el semáforo en sí, no el mutex subyacente). Inicializa el cv interno. Establece el contador.

Lo que esto implica: un semáforo es literalmente un mutex más un cv más un contador, ensamblados en un pequeño paquete. Entender este ensamblaje es entender la primitiva.

### `sema_destroy`

```c
void
sema_destroy(struct sema *sema)
{
        CTR3(KTR_LOCK, "%s(%p) \"%s\"", __func__, sema,
            cv_wmesg(&sema->sema_cv));

        KASSERT((sema->sema_waiters == 0), ("%s(): waiters\n", __func__));

        mtx_destroy(&sema->sema_mtx);
        cv_destroy(&sema->sema_cv);
}
```

Dos líneas de lógica tras el rastreo. Verifica que no haya hilos en espera. Destruye el mutex interno y el cv. Esta verificación es la que te obliga a asegurarte de que no haya hilos en espera antes de destruir; si la incumples, el kernel en modo debug entra en pánico.

### `_sema_post`

```c
void
_sema_post(struct sema *sema, const char *file, int line)
{

        mtx_lock(&sema->sema_mtx);
        sema->sema_value++;
        if (sema->sema_waiters && sema->sema_value > 0)
                cv_signal(&sema->sema_cv);

        CTR6(KTR_LOCK, "%s(%p) \"%s\" v = %d at %s:%d", __func__, sema,
            cv_wmesg(&sema->sema_cv), sema->sema_value, file, line);

        mtx_unlock(&sema->sema_mtx);
}
```

Tres líneas de lógica. Bloquea, incrementa, señala a un hilo en espera si los hay. La señal depende de dos condiciones: que el recuento de hilos en espera sea distinto de cero (no hace falta señalar si nadie espera) y que el valor sea positivo (señalar con un valor cero despertaría a un hilo en espera que volvería a dormir de inmediato). La segunda condición es sutil; protege contra la situación en que `sema_value` se volvió positivo y luego regresó a cero entre un post y el post actual. En la práctica, con un uso sencillo, ambas condiciones se cumplen.

### `_sema_wait`

```c
void
_sema_wait(struct sema *sema, const char *file, int line)
{

        mtx_lock(&sema->sema_mtx);
        while (sema->sema_value == 0) {
                sema->sema_waiters++;
                cv_wait(&sema->sema_cv, &sema->sema_mtx);
                sema->sema_waiters--;
        }
        sema->sema_value--;

        CTR6(KTR_LOCK, "%s(%p) \"%s\" v = %d at %s:%d", __func__, sema,
            cv_wmesg(&sema->sema_cv), sema->sema_value, file, line);

        mtx_unlock(&sema->sema_mtx);
}
```

Cuatro líneas de lógica. Bloquea. Itera mientras el valor sea cero: incrementa el recuento de hilos en espera, espera en el cv, decrementa el recuento. Cuando el valor es positivo, lo decrementa y desbloquea.

Dos observaciones.

El bucle es lo que hace que la primitiva sea segura frente a los despertares espurios. `cv_wait` puede retornar sin que `cv_signal` haya sido llamado. El bucle comprueba el valor en cada iteración, de modo que un despertar espurio simplemente vuelve a dormir.

La espera usa `cv_wait`, no `cv_wait_sig`. Esto es lo que hace que `sema_wait` no sea interrumpible. Una señal al invocante no tiene efecto. El bucle continúa hasta que llega un `sema_post` real.

### `_sema_timedwait`

```c
int
_sema_timedwait(struct sema *sema, int timo, const char *file, int line)
{
        int error;

        mtx_lock(&sema->sema_mtx);

        for (error = 0; sema->sema_value == 0 && error == 0;) {
                sema->sema_waiters++;
                error = cv_timedwait(&sema->sema_cv, &sema->sema_mtx, timo);
                sema->sema_waiters--;
        }
        if (sema->sema_value > 0) {
                sema->sema_value--;
                error = 0;
                /* ... tracing ... */
        } else {
                /* ... tracing ... */
        }

        mtx_unlock(&sema->sema_mtx);
        return (error);
}
```

Algo más complejo. El bucle usa `cv_timedwait`, tampoco `cv_timedwait_sig`, de modo que la espera con tiempo límite tampoco es interrumpible. El bucle termina cuando el valor es positivo o cuando el error es distinto de cero (normalmente `EWOULDBLOCK`).

Tras el bucle: si el valor es positivo, lo tomamos y devolvemos 0 (éxito). De lo contrario, devolvemos el error (`EWOULDBLOCK`). El error del cv se conserva de la última iteración del bucle en el caso de error.

Una sutileza que señala el comentario en el código fuente: un despertar espurio reinicia el intervalo de tiempo de espera efectivo porque cada iteración usa un `timo` nuevo. Esto significa que la espera real puede ser ligeramente mayor de lo que solicitó el invocante, pero nunca menor. El retorno `EWOULDBLOCK` acaba produciéndose.

### `_sema_trywait`

```c
int
_sema_trywait(struct sema *sema, const char *file, int line)
{
        int ret;

        mtx_lock(&sema->sema_mtx);

        if (sema->sema_value > 0) {
                sema->sema_value--;
                ret = 1;
        } else {
                ret = 0;
        }

        mtx_unlock(&sema->sema_mtx);
        return (ret);
}
```

Dos líneas de lógica. Bloquea. Si el valor es positivo, lo decrementa y devuelve 1. De lo contrario, devuelve 0. Sin bloqueo, sin cv, sin contabilidad de hilos en espera.

### `sema_value`

```c
int
sema_value(struct sema *sema)
{
        int ret;

        mtx_lock(&sema->sema_mtx);
        ret = sema->sema_value;
        mtx_unlock(&sema->sema_mtx);
        return (ret);
}
```

Una línea de lógica. Devuelve el valor actual. El valor puede cambiar inmediatamente después de que se libere el mutex, de modo que el resultado es una instantánea, no una garantía. Útil para diagnósticos.

### Observaciones

Leer el archivo completo lleva diez minutos. Al terminar, entenderás:

- Los semáforos se construyen a partir de un mutex y un cv.
- `sema_wait` no es interrumpible por señal porque usa `cv_wait`.
- `sema_destroy` verifica que no haya hilos en espera, razón por la que debes asegurarte de ello antes de destruir.
- La coalescencia no existe (cada post decrementa el contador en uno; no hay contabilidad de «pendientes»).
- La primitiva es sencilla; sus contratos son precisos.

Este tipo de lectura es el camino más corto hacia el dominio de cualquier primitiva del kernel. El archivo de `sema(9)` es un punto de partida especialmente bueno porque es muy breve.



## Referencia: estandarización de las primitivas de sincronización en un driver

A medida que el driver acumula más primitivas, la consistencia importa más que la astucia.

### Una convención de nomenclatura

La convención del capítulo 15, que puedes adoptar o modificar:

- **Mutex**: `sc->mtx`. Solo uno por driver. Si el driver necesita más de uno, cada uno tiene un sufijo de propósito: `sc->tx_mtx`, `sc->rx_mtx`.
- **Variable de condición**: `sc-><purpose>_cv`. P. ej., `data_cv`, `room_cv`.
- **Sx lock**: `sc-><purpose>_sx`. P. ej., `cfg_sx`, `stats_cache_sx`.
- **Semáforo**: `sc-><purpose>_sema`. P. ej., `writers_sema`.
- **Callout**: `sc-><purpose>_co`. P. ej., `heartbeat_co`.
- **Task**: `sc-><purpose>_task`. P. ej., `selwake_task`.
- **Timeout task**: `sc-><purpose>_delayed_task`. P. ej., `reset_delayed_task`.
- **Bandera atómica**: `sc-><purpose>` como un `int`. Sin sufijo; el tipo habla por sí solo. P. ej., `is_attached`, `recovery_in_progress`.
- **Bandera de estado bajo mutex**: igual que la atómica; el comentario en el softc nombra el lock.

### Un patrón de init/destroy

Cada primitiva tiene un init y un destroy canónicos. El orden en attach es el inverso al de detach.

Orden en attach:

1. Mutex.
2. Cvs.
3. Sx locks.
4. Semáforos.
5. Taskqueue.
6. Callouts.
7. Tasks.
8. Banderas atómicas (no requieren init; se inicializan al poner el softc a cero).

Orden en detach (aproximadamente inverso):

1. Limpia la bandera atómica.
2. Envía un broadcast a los cvs.
3. Libera el mutex.
4. Drena los callouts.
5. Drena las tasks.
6. Libera el taskqueue.
7. Destruye los semáforos.
8. Destruye los sx.
9. Destruye los cvs.
10. Destruye el mutex.

La regla general: destruye en el orden opuesto al de init, y drena todo aquello que todavía pueda dispararse antes de destruir lo que toca.

### Un patrón de encapsulación

El patrón `myfirst_sync.h` de la sección 6 escala bien. Cada primitiva tiene un wrapper. Cada wrapper recibe el nombre de lo que hace, no de la primitiva que envuelve.

### Una plantilla de LOCKING.md

Una sección de `LOCKING.md` por primitiva. Cada sección especifica:

- La primitiva.
- Su propósito.
- Su ciclo de vida.
- Su contrato con otras primitivas (orden de lock, propiedad, interacción con la bandera atómica).

Una nueva primitiva añadida al driver suma una nueva sección. Una primitiva modificada actualiza su sección existente. El documento está siempre al día.

### Por qué estandarizar

Las ventajas son las mismas que en el capítulo 14. Menor carga cognitiva. Menos errores. Revisión más sencilla. Transferencia más fácil. Los costes son pequeños y se pagan una sola vez.



## Referencia: cuándo cada primitiva es incorrecta

Las primitivas de sincronización son herramientas. Toda herramienta puede usarse mal. A continuación se presenta una breve lista de antipatrones que conviene reconocer.

### Usos incorrectos del mutex

- **Mantener un mutex durante un sleep.** Impide que otros threads avancen; puede causar un deadlock si el sleep depende de un estado para el que otro thread necesita el mutex.
- **Anidar mutexes en orden inconsistente.** Crea un ciclo en el orden de lock; `WITNESS` lo detecta.
- **Usar un mutex donde bastaría una variable atómica.** Para una bandera de un solo bit consultada con frecuencia, un mutex es más pesado de lo necesario.
- **Omitir `mtx_assert` en funciones auxiliares que requieren el mutex.** Sin la aserción, la función auxiliar puede invocarse en un contexto en el que el mutex no está adquirido; el fallo puede ser silencioso.

### Usos incorrectos del cv

- **Usar `cv_wait` en un contexto de syscall.** No puede ser interrumpida por señales; hace que la syscall deje de responder.
- **No volver a comprobar el predicado tras el despertar.** Los despertares espurios están permitidos; el código que asume que un despertar significa que el predicado es verdadero es incorrecto.
- **Señalar sin mantener el mutex.** Generalmente permitido por la API, pero habitualmente desaconsejable; el hilo en espera puede perderse la señal si el momento es desafortunado.
- **Usar `cv_signal` donde se necesita `cv_broadcast`.** Una ruta de detach que solo despierta a un hilo deja a los demás bloqueados.

### Usos incorrectos de sx

- **Usar sx donde bastaría un mutex.** Sx tiene mayor coste que un mutex en el caso sin contención; si no hay beneficio de acceso compartido, el mutex es más sencillo.
- **Olvidar que sx puede dormir.** Sx no puede adquirirse desde un contexto que no pueda dormir. Los callouts inicializados con `callout_init_mtx(, &mtx, 0)` son válidos; las interrupciones de filtro no. (Nota histórica: el antiguo flag `CALLOUT_MPSAFE` identificaba la misma distinción. El capítulo 13 explica su obsolescencia.)
- **Usar `sx_try_upgrade` sin el camino alternativo.** Un upgrade ingenuo que no gestiona el fallo compite en condición de carrera con otro hilo que también intenta hacer upgrade.

### Usos incorrectos de sema

- **Esperar interrupción por señal.** `sema_wait` no es interrumpible.
- **Esperar herencia de prioridad.** `sema` no eleva la prioridad del invocante de post.
- **Destruir con hilos en espera.** Provoca pánico en un kernel de depuración y corrompe silenciosamente un kernel de producción.
- **Olvidar un post en una ruta de error.** Se pierde una ranura; con el tiempo el semáforo se agota hasta cero y todos los hilos en espera quedan bloqueados.

### Usos incorrectos de los atómicos

- **Usar variables atómicas simples donde se necesitan barreras de adquisición/liberación.** Correcto en x86, incorrecto en arm64. Piensa siempre en el ordenamiento de memoria.
- **Proteger un invariante complejo con una sola variable atómica.** Si el invariante implica varios campos, los atómicos por sí solos son insuficientes; se necesita un lock.
- **Usar CAS donde bastaría una carga-almacenamiento simple.** Malgasta una instrucción atómica.

### Usos incorrectos de los patrones

- **Implementar un semáforo propio a partir de mutex más contador más cv.** El `sema(9)` del kernel ya hace esto; reinventarlo genera deuda de mantenimiento.
- **Implementar un lock de lectura-escritura propio a partir de mutex más contador.** `sx(9)` ya hace esto; reinventarlo genera deuda de mantenimiento.
- **Implementar la capa de encapsulación directamente en el código fuente del driver.** Ponla en un archivo de cabecera (`myfirst_sync.h`); no la dupliques de forma dispersa.

Reconocer los antipatrones es la mitad del trabajo para lograr una buena sincronización. La otra mitad consiste en elegir la primitiva adecuada desde el principio, que es precisamente lo que el cuerpo principal del capítulo ha abordado.

## Referencia: hoja de consulta rápida sobre observabilidad

### Parámetros sysctl que tu driver debería exponer

Para el Capítulo 15, añade estos a la jerarquía sysctl del driver (en `dev.myfirst.N.stats.*` o `dev.myfirst.N.*` según corresponda):

- `writers_limit`: límite actual de escritores.
- `stats.writers_sema_value`: instantánea del valor del semáforo.
- `stats.writers_trywait_failures`: contador de escritores no bloqueantes rechazados.
- `stats.stats_cache_refreshes`: contador de refrescos de caché.
- `stats.recovery_task_runs`: contador de invocaciones de recuperación.
- `stats.is_attached`: valor actual del flag atómico.

Estos contadores de solo lectura dan al operador una ventana al comportamiento de sincronización del driver sin necesidad de un depurador.

### Sondas DTrace

**Contar señales de cv por cv:**

```text
dtrace -n 'fbt::cv_signal:entry { @[stringof(args[0]->cv_description)] = count(); }'
```

**Contar esperas y posts de sema:**

```text
dtrace -n '
  fbt::_sema_wait:entry { @["wait"] = count(); }
  fbt::_sema_post:entry { @["post"] = count(); }
'
```

**Medir la latencia de espera de sema:**

```text
dtrace -n '
  fbt::_sema_wait:entry { self->t = timestamp; }
  fbt::_sema_wait:return /self->t/ {
        @ = quantize(timestamp - self->t);
        self->t = 0;
  }
'
```

Útil para entender cuánto tiempo bloquean los escritores en el límite de escritores en la práctica.

**Medir la contención de sx:**

```text
dtrace -n '
  lockstat:::sx-block-enter /arg0 == (uintptr_t)&sc_addr/ {
        @ = count();
  }
'
```

Sustituye `sc_addr` por la dirección real de tu sx. Muestra con qué frecuencia bloqueó el sx.

**Observar taskqueue_run_locked para la recuperación:**

```text
dtrace -n 'fbt::myfirst_recovery_task:entry { printf("recovery at %Y", walltimestamp); }'
```

Imprime una marca de tiempo cada vez que se dispara la tarea de recuperación.

### procstat

`procstat -t | grep myfirst`: muestra los threads trabajadores del taskqueue y su estado.

`procstat -kk <pid>`: stack del kernel de un thread específico. Útil cuando algo se queda bloqueado.

### ps

`ps ax | grep taskq`: lista todos los trabajadores del taskqueue por nombre.

### ddb

`db> show witness`: vuelca el grafo de locks de WITNESS.

`db> show locks`: lista los locks actualmente retenidos.

`db> show sleepchain <tid>`: recorre una cadena de sleep para encontrar deadlocks.



## Referencia: resumen del diff etapa por etapa

Un resumen compacto del diff del driver desde la Etapa 4 del Capítulo 14 hasta la Etapa 4 del Capítulo 15, para los lectores que quieran ver el cambio completo de un vistazo.

### Diff de la Etapa 1 (v0.8 -> v0.8+writers_sema)

**Adiciones al softc:**

```c
struct sema     writers_sema;
int             writers_limit;
int             writers_trywait_failures;
int             writers_inflight;   /* atomic int; drain counter */
```

**Adiciones en attach:**

```c
sema_init(&sc->writers_sema, 4, "myfirst writers");
sc->writers_limit = 4;
sc->writers_trywait_failures = 0;
sc->writers_inflight = 0;
```

**Adiciones en detach (tras is_attached=0 y todos los broadcasts de cv):**

```c
sema_destroy(&sc->writers_sema);
```

**Nuevo handler sysctl:** `myfirst_sysctl_writers_limit`.

**Cambios en la ruta de escritura:** adquisición del sema en la entrada, liberación en la salida, O_NONBLOCK mediante sema_trywait.

### Diff de la Etapa 2 (+stats_cache_sx)

**Adiciones al softc:**

```c
struct sx       stats_cache_sx;
uint64_t        stats_cache_bytes_10s;
uint64_t        stats_cache_last_refresh_ticks;
```

**Adiciones en attach:**

```c
sx_init(&sc->stats_cache_sx, "myfirst stats cache");
sc->stats_cache_bytes_10s = 0;
sc->stats_cache_last_refresh_ticks = 0;
```

**Adiciones en detach (tras la destrucción del mutex):**

```c
sx_destroy(&sc->stats_cache_sx);
```

**Nueva función auxiliar:** `myfirst_stats_cache_refresh`.

**Nuevo handler sysctl:** `myfirst_sysctl_stats_cached`.

### Diff de la Etapa 3 (EINTR/ERESTART + progreso parcial)

**Sin cambios en el softc.**

**Refactorización del helper de espera:** centinela `MYFIRST_WAIT_PARTIAL`; switch explícito sobre los códigos de error de las esperas de cv.

**Cambios en los llamadores:** comprobaciones explícitas del centinela.

### Diff de la Etapa 4 (coordinación + encapsulación)

**Adiciones al softc:**

```c
int             recovery_in_progress;
struct task     recovery_task;
int             recovery_task_runs;
```

**Adiciones en attach:**

```c
TASK_INIT(&sc->recovery_task, 0, myfirst_recovery_task, sc);
sc->recovery_in_progress = 0;
sc->recovery_task_runs = 0;
```

**Adiciones en detach:**

```c
taskqueue_drain(sc->tq, &sc->recovery_task);
```

**Conversiones del flag atómico:** las lecturas de `is_attached` en los handlers y callbacks pasan a ser `atomic_load_acq_int`; la escritura en detach pasa a ser `atomic_store_rel_int`.

**Nuevo archivo de cabecera:** `myfirst_sync.h` con envoltorios inline.

**Ediciones en el código fuente:** cada llamada específica a una primitiva en el fuente principal pasa a ser una llamada al envoltorio correspondiente.

**Refactorización del watchdog:** encola la tarea de recuperación ante un bloqueo, protegida por el flag `recovery_in_progress`.

**Incremento de versión:** `MYFIRST_VERSION "0.9-coordination"`.

### Total de líneas añadidas

Recuento aproximado en las cuatro etapas:

- Softc: ~10 campos.
- Attach: ~15 líneas.
- Detach: ~10 líneas.
- Nuevas funciones: ~80 líneas (handlers sysctl, recovery_task, auxiliares).
- Funciones modificadas: ~20 ediciones de líneas.
- Archivo de cabecera: ~150 líneas.

Incorporación neta al driver: aproximadamente 300 líneas. Compáralo con las aproximadamente 100 líneas que añadió el Capítulo 14 y las 400 del Capítulo 13. Las adiciones del Capítulo 15 son modestas en volumen, pero enormes en lo que permiten.



## Referencia: el ciclo de vida del driver del Capítulo 15

Un resumen del ciclo de vida con las adiciones del Capítulo 15 detalladas explícitamente.

### Secuencia de attach

1. `mtx_init(&sc->mtx, ...)`.
2. `cv_init(&sc->data_cv, ...)`, `cv_init(&sc->room_cv, ...)`.
3. `sx_init(&sc->cfg_sx, ...)`.
4. `sx_init(&sc->stats_cache_sx, ...)`.
5. `sema_init(&sc->writers_sema, 4, ...)`.
6. `sc->tq = taskqueue_create(...)`; `taskqueue_start_threads(...)`.
7. `callout_init_mtx(&sc->heartbeat_co, ...)`, dos más.
8. `TASK_INIT(&sc->selwake_task, ...)`, tres más (incluyendo recovery_task).
9. `TIMEOUT_TASK_INIT(sc->tq, &sc->reset_delayed_task, ...)`.
10. Campos del softc inicializados.
11. `sc->bytes_read = counter_u64_alloc(M_WAITOK)`; lo mismo para bytes_written.
12. `cbuf_init(&sc->cb, ...)`.
13. `make_dev_s(...)` para el cdev.
14. Árbol sysctl configurado.
15. `sc->is_attached = 1` (el almacenamiento inicial no está estrictamente ordenado a nivel atómico, porque ningún lector puede ver el softc hasta que esté enlazado).

### En tiempo de ejecución

- Los threads de usuario entran y salen mediante open/close/read/write.
- Los callouts se disparan periódicamente.
- Las tareas se disparan cuando se encolan.
- El watchdog detecta bloqueos y encola la tarea de recuperación.
- El flag atómico se lee mediante `myfirst_sync_is_attached` en las comprobaciones de entrada.

### Secuencia de detach

1. Se llama a `myfirst_detach`.
2. Comprobar `active_fhs > 0`; devolver `EBUSY` si es el caso.
3. `atomic_store_rel_int(&sc->is_attached, 0)`.
4. `cv_broadcast(&sc->data_cv)`, `cv_broadcast(&sc->room_cv)`.
5. Liberar `sc->mtx`.
6. `callout_drain` tres veces.
7. `taskqueue_drain` tres veces (selwake, bulk_writer, recovery).
8. `taskqueue_drain_timeout` una vez (reset_delayed).
9. `seldrain` dos veces.
10. `taskqueue_free(sc->tq)`.
11. `sema_destroy(&sc->writers_sema)`.
12. `destroy_dev` dos veces.
13. `sysctl_ctx_free`.
14. `cbuf_destroy`, `counter_u64_free` dos veces.
15. `sx_destroy(&sc->stats_cache_sx)`.
16. `cv_destroy` dos veces.
17. `sx_destroy(&sc->cfg_sx)`.
18. `mtx_destroy(&sc->mtx)`.

### Notas sobre la secuencia

- Cada inicialización de primitiva en attach tiene su correspondiente destrucción en detach, en orden inverso.
- Cada drenado se produce antes de liberar el recurso drenado.
- El flag atómico es el primer paso tras la comprobación de `active_fhs`, de modo que todos los observadores posteriores ven el apagado.
- El taskqueue se libera antes de destruir el semáforo, porque una tarea podría (en un diseño más amplio) esperar en el semáforo.

Memorizar este ciclo de vida es lo más útil que puede hacer un lector con la Parte 3.



## Referencia: costes y comparaciones

Una tabla concisa de los costes de las primitivas de sincronización.

| Primitiva | Coste sin contención | Coste con contención | ¿Duerme? |
|---|---|---|---|
| `atomic_load_acq_int` | ~1 ns en amd64 | ~1 ns (igual) | No |
| `atomic_fetchadd_int` | ~10 ns | ~100 ns | No |
| `mtx_lock` (sin contención) | ~20 ns | microsegundos | El camino lento duerme |
| `cv_wait_sig` | n/a | activación completa del planificador | Sí |
| `sx_slock` | ~30 ns | microsegundos | El camino lento duerme |
| `sx_xlock` | ~30 ns | microsegundos | El camino lento duerme |
| `sx_try_upgrade` | ~30 ns | n/a (falla rápido) | No |
| `sema_wait` | ~40 ns | latencia de activación | Sí |
| `sema_post` | ~30 ns | ~100 ns | No |
| `callout_reset` | ~100 ns | n/a | No |
| `taskqueue_enqueue` | ~50 ns | latencia de activación | No |

Las cifras anteriores son estimaciones de orden de magnitud en hardware amd64 típico de FreeBSD 14.3, y las entradas en microsegundos de la columna de contención corresponden a latencias de activación de pocos microsegundos en esa misma clase de máquina. Los valores reales dependen del estado de la caché, la contención y la carga del sistema, y pueden variar por un factor de dos o más entre generaciones de CPU. Usa esta tabla para decidir dónde optimizar; una llamada que tarda cientos de nanosegundos no es un cuello de botella en un camino que se ejecuta una vez por syscall. Consulta el Apéndice F para obtener un benchmark reproducible de estas cifras en tu propio hardware.



## Referencia: un recorrido de sincronización paso a paso

Para un ejemplo completamente concreto, aquí está el flujo de control completo de una llamada `read(2)` bloqueante en el driver de la Etapa 4 del Capítulo 15, desde la entrada al syscall hasta la entrega de los datos.

1. El usuario llama a `read(fd, buf, 4096)`.
2. La capa VFS del kernel dirige la llamada a `myfirst_read`.
3. `myfirst_read` llama a `devfs_get_cdevpriv` para obtener el fh.
4. `myfirst_read` llama a `myfirst_sync_is_attached(sc)`:
   - Se expande a `atomic_load_acq_int(&sc->is_attached)`.
   - Devuelve 1 (enlazado).
5. El bucle entra: `while (uio->uio_resid > 0)`.
6. `myfirst_sync_lock(sc)`:
   - Se expande a `mtx_lock(&sc->mtx)`.
   - Adquiere el mutex.
7. `myfirst_wait_data(sc, ioflag, nbefore, uio)`:
   - `while (cbuf_used == 0)`: el buffer está vacío.
   - No es parcial (nbefore == uio_resid).
   - No es IO_NDELAY.
   - `read_timeout_ms` es 0, así que usa `cv_wait_sig`.
   - `cv_wait_sig(&sc->data_cv, &sc->mtx)`:
     - Libera el mutex.
     - El thread duerme en la cv.
   - Pasa el tiempo. Un escritor (o el tick_source) llama a `cv_signal(&sc->data_cv)`.
   - El thread se activa, `cv_wait_sig` vuelve a adquirir el mutex y devuelve 0.
   - `!sc->is_attached`: falso.
   - El bucle vuelve a iterar: `cbuf_used` es ahora > 0.
   - Sale del bucle; devuelve 0.
8. `myfirst_buf_read(sc, bounce, take)`:
   - Llama a `cbuf_read(&sc->cb, bounce, take)`.
   - Copia los datos en el bounce buffer.
   - Incrementa el contador `bytes_read`.
9. `myfirst_sync_unlock(sc)`.
10. `cv_signal(&sc->room_cv)`: activa un escritor bloqueado si lo hay.
11. `selwakeup(&sc->wsel)`: activa cualquier poller que esté esperando para escritura.
12. `uiomove(bounce, got, uio)`: copia del espacio del kernel al espacio de usuario.
13. El bucle continúa: comprueba `uio->uio_resid > 0`; finalmente sale.
14. Devuelve 0 a la capa del syscall.
15. La capa del syscall devuelve al usuario el número de bytes copiados.

Todas las primitivas del Capítulo 15 son visibles en el recorrido:

- El flag atómico (paso 4).
- El mutex (pasos 6, 7, 9).
- La espera de cv con manejo de señales (el cv_wait_sig del paso 7).
- El contador (el counter_u64_add del paso 8).
- La señal de cv (paso 10).
- El selwakeup (paso 11, realizado fuera del mutex según la disciplina establecida).

Un recorrido como este es una comprobación cruzada muy útil. Cada primitiva del vocabulario del driver se ejercita en el camino de lectura. Si puedes narrar el recorrido de memoria, la sincronización está interiorizada.



## Referencia: una plantilla mínima funcional

Para facilitar la copia y adaptación, aquí tienes una plantilla que compila y demuestra las adiciones esenciales del Capítulo 15 en la forma más compacta posible. Cada elemento ha sido presentado en el capítulo; la plantilla los ensambla.

```c
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/mutex.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/sema.h>
#include <sys/taskqueue.h>
#include <sys/priority.h>

struct template_softc {
        device_t           dev;
        struct mtx         mtx;
        struct sx          stats_sx;
        struct sema        admission_sema;
        struct taskqueue  *tq;
        struct task        work_task;
        int                is_attached;
        int                work_in_progress;
};

static void
template_work_task(void *arg, int pending)
{
        struct template_softc *sc = arg;
        mtx_lock(&sc->mtx);
        /* Work under mutex if needed. */
        sc->work_in_progress = 0;
        mtx_unlock(&sc->mtx);
        /* Unlocked work here. */
}

static int
template_attach(device_t dev)
{
        struct template_softc *sc = device_get_softc(dev);
        int error;

        sc->dev = dev;
        mtx_init(&sc->mtx, device_get_nameunit(dev), "template", MTX_DEF);
        sx_init(&sc->stats_sx, "template stats");
        sema_init(&sc->admission_sema, 4, "template admission");

        sc->tq = taskqueue_create("template taskq", M_WAITOK,
            taskqueue_thread_enqueue, &sc->tq);
        if (sc->tq == NULL) { error = ENOMEM; goto fail_sema; }
        error = taskqueue_start_threads(&sc->tq, 1, PWAIT,
            "%s taskq", device_get_nameunit(dev));
        if (error != 0) goto fail_tq;

        TASK_INIT(&sc->work_task, 0, template_work_task, sc);

        atomic_store_rel_int(&sc->is_attached, 1);
        return (0);

fail_tq:
        taskqueue_free(sc->tq);
fail_sema:
        sema_destroy(&sc->admission_sema);
        sx_destroy(&sc->stats_sx);
        mtx_destroy(&sc->mtx);
        return (error);
}

static int
template_detach(device_t dev)
{
        struct template_softc *sc = device_get_softc(dev);

        atomic_store_rel_int(&sc->is_attached, 0);

        taskqueue_drain(sc->tq, &sc->work_task);
        taskqueue_free(sc->tq);
        sema_destroy(&sc->admission_sema);
        sx_destroy(&sc->stats_sx);
        mtx_destroy(&sc->mtx);
        return (0);
}

/* Entry on the hot path: */
static int
template_hotpath_enter(struct template_softc *sc)
{
        sema_wait(&sc->admission_sema);
        if (!atomic_load_acq_int(&sc->is_attached)) {
                sema_post(&sc->admission_sema);
                return (ENXIO);
        }
        return (0);
}

static void
template_hotpath_leave(struct template_softc *sc)
{
        sema_post(&sc->admission_sema);
}
```

La plantilla no es un driver completo. Muestra la forma de las primitivas que el capítulo introdujo. Un driver real que combine esta plantilla con el resto de los patrones del Capítulo 14 y anteriores sería un driver de dispositivo sincronizado completamente funcional.



## Referencia: comparación con la sincronización POSIX en espacio de usuario

Muchos lectores llegan al desarrollo de drivers del kernel de FreeBSD desde la programación de sistemas en espacio de usuario. Una breve comparación aclara la correspondencia.

| Concepto | POSIX en espacio de usuario | Kernel de FreeBSD |
|---|---|---|
| Mutex | `pthread_mutex_t` | `struct mtx` |
| Variable de condición | `pthread_cond_t` | `struct cv` |
| Lock de lectura-escritura | `pthread_rwlock_t` | `struct sx` |
| Semáforo | `sem_t` (o `sem_open`) | `struct sema` |
| Creación de threads | `pthread_create` | `kproc_create`, `kthread_add` |
| Trabajo diferido | Sin equivalente directo; habitualmente se implementa a mano | `struct task` + `struct taskqueue` |
| Ejecución periódica | `timer_create` + `signal` | `struct callout` |
| Operaciones atómicas | `<stdatomic.h>` o `__atomic_*` | `atomic(9)` |

Las primitivas son similares en forma, pero distintas en los detalles. Diferencias clave:

- Los mutex del kernel tienen herencia de prioridad; los mutex POSIX solo la tienen con el atributo `PRIO_INHERIT`.
- Las cv del kernel no tienen nombre; las cv de POSIX tampoco son nombradas, así que hay paridad aquí.
- Los sx locks del kernel son más flexibles que pthread_rwlock (try_upgrade, downgrade).
- Los semáforos del kernel no son interrumpibles por señales; los semáforos POSIX (en algunas variantes) sí lo son.
- Los taskqueues del kernel no tienen un equivalente POSIX directo; los pools de threads POSIX se implementan a mano.
- Los atómicos del kernel son más completos (más operaciones, mejor control de barreras) que los atómicos de C11 más antiguos.

Un lector familiarizado con la sincronización POSIX encontrará las primitivas del kernel intuitivas con ajustes menores. El ajuste principal es la conciencia del contexto: el código del kernel no puede asumir la capacidad de bloquearse.

## Referencia: Un catálogo de patrones con ejemplos

Diez patrones de sincronización que se repiten en drivers reales. Cada uno es una variación sobre las primitivas que el Capítulo 15 introdujo.

### Patrón 1: Productor/consumidor con cola acotada

El cbuf del Capítulo 14 junto con las cvs del Capítulo 12 ya implementan este patrón. El productor escribe, el consumidor lee, el mutex protege la cola y las cvs señalizan las transiciones de vacía a no vacía y de llena a no llena.

### Patrón 2: Semáforo de finalización

Un emisor inicializa un semáforo a 0 y espera. Un manejador de finalización hace un post. El emisor se desbloquea. Se utiliza para patrones de petición-respuesta. Véase `/usr/src/sys/dev/hyperv/storvsc/hv_storvsc_drv_freebsd.c`.

### Patrón 3: Control de admisión

Un semáforo inicializado a N. Cada participante llama a `sema_wait` al entrar y a `sema_post` al salir. La Etapa 1 del Capítulo 15 utiliza este patrón.

### Patrón 4: Caché de lectura con upgrade, actualización y downgrade

Un sx lock con actualización basada en la obsolescencia de los datos. Los lectores toman el lock en modo compartido; cuando los datos están obsoletos, intentan hacer `sx_try_upgrade`, actualizan los datos y vuelven a downgrade. La Etapa 2 del Capítulo 15 utiliza este patrón.

### Patrón 5: Coordinación mediante flag atómico

Un flag atómico leído por muchos contextos y escrito por uno solo. Usa `atomic_load_acq` para las lecturas y `atomic_store_rel` para las escrituras. La Etapa 4 del Capítulo 15 utiliza este patrón para `is_attached`.

### Patrón 6: Flag de estado de a lo sumo uno

Un flag de estado protegido por un lock o por CAS. La ruta de inicio de operación activa el flag; la ruta de fin de operación lo desactiva. La Etapa 4 del Capítulo 15 utiliza este patrón para `recovery_in_progress`.

### Patrón 7: Espera acotada interrumpible por señal

`cv_timedwait_sig` con manejo explícito de EINTR/ERESTART/EWOULDBLOCK. La Etapa 3 del Capítulo 15 refina este patrón.

### Patrón 8: Actualización periódica mediante callout

Un callout invoca periódicamente una función de actualización. La actualización mantiene un lock de forma breve. Es más sencillo que el Patrón 4 cuando el intervalo de actualización es fijo.

### Patrón 9: Desmontaje diferido mediante contador de referencias

El objeto tiene un contador de referencias. "Liberar" decrementa; la liberación real ocurre cuando el contador llega a cero. El decremento atómico garantiza la corrección.

### Patrón 10: Handshake entre subsistemas

Dos subsistemas se coordinan mediante un flag de estado compartido más una cv o un sema. Uno señaliza que su parte está lista; el otro espera. Útil para el apagado por etapas.

Conocer estos patrones agiliza la lectura del código fuente de drivers reales. Cada primitiva del Capítulo 15 es un componente básico de uno o más de estos patrones, y cada driver real elige los patrones que necesita su carga de trabajo.



## Referencia: Glosario de términos de la Parte 3

Como referencia final, un glosario consolidado que abarca desde el Capítulo 11 hasta el Capítulo 15.

**Operación atómica.** Una primitiva de lectura, escritura o lectura-modificación-escritura que se ejecuta sin posibilidad de interferencia concurrente a nivel de hardware.

**Barrera.** Una directiva que impide al compilador o a la CPU reordenar las operaciones de memoria más allá de un punto específico.

**Espera bloqueante.** Una operación de sincronización que pone a dormir al llamador hasta que se cumple una condición o se dispara un timeout.

**Broadcast.** Despertar a todos los threads bloqueados en una cv o un sema.

**Callout.** Una función diferida programada para ejecutarse en un número de ticks específico en el futuro.

**Coalescencia.** Combinar múltiples peticiones en una sola operación (por ejemplo, los encolados de tareas en `ta_pending`).

**Variable de condición.** Una primitiva que permite a un thread dormirse hasta que otro thread señaliza un cambio de estado.

**Contexto.** El entorno de ejecución de una ruta de código (syscall, callout, tarea, interrupción, etc.) con sus propias reglas sobre qué operaciones son seguras.

**Contador.** Una primitiva acumuladora por CPU (`counter(9)`) utilizada para estadísticas sin lock.

**Drain.** Esperar hasta que una operación pendiente deje de estar pendiente y no esté en ejecución en ese momento.

**Encolar.** Añadir un elemento de trabajo a una cola. Normalmente desencadena un wakeup del consumidor.

**Epoch.** Un mecanismo de sincronización que permite lecturas sin lock de estructuras compartidas; los escritores difieren la liberación mediante `epoch_call`.

**Lock exclusivo.** Un lock que puede tener como máximo un thread; los escritores usan el modo exclusivo.

**Interrupción de filtro.** Un manejador de interrupción que se ejecuta en contexto hardware con severas restricciones sobre lo que puede hacer.

**Grouptaskqueue.** Una variante escalable de taskqueue con colas de trabajo por CPU; utilizada por drivers de red de alta velocidad.

**Espera interrumpible.** Una espera bloqueante que puede ser despertada por una señal, devolviendo EINTR o ERESTART.

**Ordenación de memoria.** Reglas sobre la visibilidad y el orden de los accesos a memoria entre CPUs.

**Mutex.** Una primitiva que garantiza la exclusión mutua; como máximo un thread dentro del lock en cada momento.

**Progreso parcial.** Bytes ya copiados en una lectura o escritura cuando se dispara una interrupción o un timeout; por convención se devuelve como éxito con un conteo reducido.

**Herencia de prioridad.** Un mecanismo del planificador por el que un esperador de alta prioridad eleva temporalmente la prioridad del poseedor actual del lock.

**Barrera de liberación.** Una barrera sobre un store que garantiza que los accesos anteriores no puedan reordenarse después del store.

**Semáforo.** Una primitiva con un contador no negativo; `post` lo incrementa, `wait` lo decrementa (bloqueando si es cero).

**Lock compartido.** Un lock que pueden tener muchos threads a la vez; los lectores usan el modo compartido.

**Spinlock.** Un lock cuya ruta lenta hace busy-wait en lugar de dormir. Mutex `MTX_SPIN`.

**Sx lock.** Un lock compartido/exclusivo; la primitiva de lock de lectura-escritura de FreeBSD.

**Tarea.** Un elemento de trabajo diferido con un callback y un contexto, enviado a un taskqueue.

**Taskqueue.** Una cola de tareas pendientes atendidas por uno o más threads de trabajo.

**Timeout.** Una duración a partir de la cual una espera debe abandonar y devolver EWOULDBLOCK.

**Tarea con timeout.** Una tarea programada para un momento futuro específico mediante `taskqueue_enqueue_timeout`.

**Upgrade.** Promover un sx lock compartido a exclusivo sin liberarlo (`sx_try_upgrade`).

**Wakeup.** Despertar a un thread bloqueado en una cv, un sema o una cola de sleep.



La Parte 3 termina aquí. La Parte 4 comienza con el Capítulo 16, *Conceptos básicos de hardware y Newbus*.


## Referencia: Un escenario de depuración con ejemplo

Una guía narrativa a través de un bug de sincronización realista. Imagina que heredas un driver escrito por un compañero de trabajo. El compañero está de vacaciones. Los usuarios informan de que "bajo carga intensa, el driver entra en pánico durante el detach con un stack trace que termina en `selwakeup`".

Esta sección muestra cómo diagnosticarías y resolverías el problema usando las herramientas del Capítulo 15.

### Paso 1: Reproducir

Primera prioridad: obtener una reproducción fiable. Sin ella, las correcciones son suposiciones.

Comienza leyendo el informe de error en busca de detalles concretos. "Carga intensa" más "pánico durante el detach" es una pista sólida de que la condición de carrera se produce entre un worker en ejecución y la ruta de detach. Un stack trace que termina en `selwakeup` sugiere que el pánico está dentro del código selinfo.

Escribe un script de estrés mínimo que reproduzca el escenario:

```text
#!/bin/sh
kldload ./myfirst.ko
sysctl dev.myfirst.0.tick_source_interval_ms=1
(while :; do dd if=/dev/myfirst of=/dev/null bs=1 count=100 2>/dev/null; done) &
READER=$!
sleep 5
kldunload myfirst
kill -TERM $READER
```

Ejecuta el script en un bucle hasta que se dispare el pánico. En un kernel de depuración, este tipo de bug provoca el pánico en cuestión de segundos. En un kernel de producción, puede tardar más.

### Paso 2: Capturar el stack

Un kernel de depuración tiene `KDB` habilitado. Tras el pánico, aterrizas en el depurador del kernel. Comandos disponibles:

- `bt`: stack trace del thread que ha generado el pánico.
- `show locks`: locks en posesión en el momento del pánico.
- `show witness`: el estado de WITNESS.

Registra todo. En un fallo en producción se recoge un volcado del kernel; recupéralo con `crashinfo` y analízalo sin conexión con `kgdb`.

Un stack típico para este bug:

```text
panic: selwakeup: freed selinfo
kdb_enter
vpanic
panic
selwakeup
myfirst_selwake_task
taskqueue_run_locked
taskqueue_thread_loop
fork_exit
```

El pánico está en `selwakeup`, llamado desde `myfirst_selwake_task`, llamado desde el worker del taskqueue. La aserción de `selwakeup` indica que el selinfo que recibió ha sido liberado.

### Paso 3: Identificar la condición de carrera

El bug es visible en el stack. El callback de la tarea llamó a `selwakeup(&sc->rsel)`, pero `sc->rsel` había sido drenado o liberado. Dos posibles causas:

A. La ruta de detach hizo `seldrain` del selinfo antes de drenar la tarea. Entonces la tarea se disparó después de `seldrain`, contra el estado ya liberado.
B. La ruta de detach liberó el softc (que contiene rsel) antes de drenar la tarea. Entonces la tarea se disparó contra memoria liberada.

Ambos son violaciones de orden. Observa el código de detach real del driver:

```c
/* Current order in the buggy driver: */
seldrain(&sc->rsel);
seldrain(&sc->wsel);
taskqueue_drain(sc->tq, &sc->selwake_task);
```

El `seldrain` está antes del `taskqueue_drain`. Esa es exactamente la causa A. La solución es intercambiar el orden.

### Paso 4: Aplicar la corrección

Cambia el detach a:

```c
taskqueue_drain(sc->tq, &sc->selwake_task);
seldrain(&sc->rsel);
seldrain(&sc->wsel);
```

El drenado de la tarea ahora garantiza que la tarea no está en ejecución. Solo entonces procede `seldrain`.

Pero espera. La tarea podría seguir encolándose desde un callout en ejecución. Comprueba: ¿se drena el callout antes del drenado de la tarea? Lee el código de nuevo:

```c
/* Full order after fix: */
MYFIRST_CO_DRAIN(&sc->heartbeat_co);
MYFIRST_CO_DRAIN(&sc->watchdog_co);
MYFIRST_CO_DRAIN(&sc->tick_source_co);
taskqueue_drain(sc->tq, &sc->selwake_task);
seldrain(&sc->rsel);
seldrain(&sc->wsel);
```

Callouts drenados, luego la tarea drenada, luego sel drenado. Este es el orden correcto.

### Paso 5: Verificar

Ejecuta el script de reproducción de nuevo con la corrección aplicada. El pánico ya no debería dispararse. Ejecútalo 100 veces para ganar confianza. En un kernel de depuración la condición de carrera aflora rápidamente; 100 ejecuciones limpias son una prueba sólida de que la corrección es correcta.

Añade una prueba de regresión que ejercite este escenario específico. La prueba debe formar parte del kit de pruebas del driver para que el bug no reaparezca.

### Paso 6: Documentar

Actualiza `LOCKING.md` con un comentario que explique por qué el orden es el que es. Un futuro mantenedor que considere reordenar los drains por algún motivo verá el comentario y lo reconsiderará.

### Conclusiones

- El bug era visible en el stack trace; la habilidad consistió en reconocer lo que significaba el trace.
- La corrección fue de una línea (reordenar dos llamadas); el diagnóstico fue el trabajo real.
- El kernel de depuración hizo el bug reproducible; sin él, el bug habría sido intermitente y misterioso.
- El kit de pruebas previene la regresión; sin él, una refactorización futura podría reintroducir el bug silenciosamente.

El Capítulo 14 ya enseñó esta regla de orden específica. Un driver de producción escrito por un compañero que no hubiera interiorizado el Capítulo 14 podría tener fácilmente este bug. La disciplina de ese capítulo y el capítulo de pruebas del Capítulo 15 son juntos lo que mantiene este bug fuera de tu propio código.

Este es un escenario breve y simplificado. Los bugs reales son más sutiles. La misma metodología se aplica: reproducir, capturar, identificar, corregir, verificar, documentar. Las primitivas y la disciplina de la Parte 3 son la caja de herramientas para el paso de "identificar", que suele ser el más difícil.



## Referencia: Lectura de la sincronización de un driver real

Para un ejercicio concreto, al estilo de un examen: elige un driver Ethernet en `/usr/src/sys/dev/` y recorre su vocabulario de sincronización. Esta sección recorre brevemente `/usr/src/sys/dev/ale/if_ale.c` como plantilla para el ejercicio.

El driver `ale(4)` es un driver Ethernet 10/100/1000 para el Atheros AR8121/AR8113/AR8114. No es grande (unos pocos miles de líneas) y tiene una estructura limpia.

### Primitivas que utiliza

Abre el archivo y busca las primitivas.

```text
$ grep -c 'mtx_init\|mtx_lock\|mtx_unlock' /usr/src/sys/dev/ale/if_ale.c
```

El driver utiliza un mutex (`sc->ale_mtx`). Adquisición uniforme mediante los macros `ALE_LOCK(sc)` y `ALE_UNLOCK(sc)`.

Utiliza callouts: `sc->ale_tick_ch` para el sondeo periódico del estado del enlace.

Utiliza un taskqueue: `sc->ale_tq`, creado con `taskqueue_create_fast` e iniciado con `taskqueue_start_threads(..., 1, PI_NET, ...)`. La variante fast (respaldada por spin-mutex) se usa porque el filtro de interrupción encola sobre ella.

Utiliza una tarea en la cola: `sc->ale_int_task` para el posprocesamiento de interrupciones.

No utiliza `sema` ni `sx`. Los invariantes del driver encajan en un único mutex.

Utiliza operaciones atómicas: varias llamadas a `atomic_set_32` y `atomic_clear_32` sobre registros de hardware (mediante `CSR_WRITE_4` y similares). Estas son para la manipulación de registros de hardware, no para la coordinación a nivel de driver.

### Patrones que demuestra

**División de la interrupción en filtro y tarea.** `ale_intr` es el filtro, que enmascara la IRQ en el hardware y encola la tarea. `ale_int_task` es la tarea, que procesa el trabajo de la interrupción en contexto de thread.

**Callout para el sondeo del enlace.** `ale_tick` es un callout periódico que se reensambla a sí mismo, utilizado para el sondeo del estado del enlace.

**Orden de detach estándar.** `ale_detach` drena los callouts, drena las tareas, libera el taskqueue y destruye el mutex. El mismo patrón que `myfirst`.

### Lo que no demuestra

- No hay sx lock. La configuración está protegida por el único mutex.
- No hay semaphore. No hay admisión acotada.
- No hay epoch. El driver no accede al estado de red directamente desde contextos inusuales.
- No hay `sx_try_upgrade`. No hay caché de lectura predominante.

### Conclusiones

El driver `ale(4)` usa el subconjunto de primitivas de la Parte 3 que su carga de trabajo necesita. Un driver que necesitara un sx lock o un sema lo añadiría; `ale(4)` no lo necesita, así que no lo añade.

Leer un driver real de esta manera vale más que leer el capítulo dos veces. Elige un driver, léelo durante 30 minutos y anota qué primitivas usa y por qué.

Haz el mismo ejercicio con un driver más grande (`bge(4)`, `iwm(4)`, `mlx5(4)` son buenos candidatos). Observa cómo escala el vocabulario. Un driver con más estado necesita más primitivas; un driver con estado más simple usa menos.

El driver `myfirst` al final del Capítulo 15 usa todas las primitivas que introdujo la Parte 3. La mayoría de los drivers reales usan un subconjunto. Ambas opciones son válidas; la elección depende de la carga de trabajo.



## Referencia: El diseño completo de `myfirst_sync.h`

El código fuente complementario en `examples/part-03/ch15-more-synchronization/stage4-final/` incluye el `myfirst_sync.h` completo. Como referencia, aquí tienes una versión completa que puede utilizarse como plantilla.

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_sync.h: the named synchronisation vocabulary of the
 * myfirst driver.
 *
 * Every primitive the driver uses has a wrapper here. The main
 * source calls these wrappers; the wrappers are inlined away, so
 * the runtime cost is zero. The benefit is a readable,
 * centralised, and easily-changeable synchronisation strategy.
 *
 * This file depends on the definition of `struct myfirst_softc`
 * in myfirst.c, which must be included before this header.
 */

#ifndef MYFIRST_SYNC_H
#define MYFIRST_SYNC_H

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/sema.h>
#include <sys/condvar.h>

/*
 * Data-path mutex. Single per-softc. Protects cbuf, counters, and
 * most of the per-softc state.
 */
static __inline void
myfirst_sync_lock(struct myfirst_softc *sc)
{
        mtx_lock(&sc->mtx);
}

static __inline void
myfirst_sync_unlock(struct myfirst_softc *sc)
{
        mtx_unlock(&sc->mtx);
}

static __inline void
myfirst_sync_assert_locked(struct myfirst_softc *sc)
{
        mtx_assert(&sc->mtx, MA_OWNED);
}

/*
 * Configuration sx. Protects the myfirst_config structure. Read
 * paths take shared; sysctl writers take exclusive.
 */
static __inline void
myfirst_sync_cfg_read_begin(struct myfirst_softc *sc)
{
        sx_slock(&sc->cfg_sx);
}

static __inline void
myfirst_sync_cfg_read_end(struct myfirst_softc *sc)
{
        sx_sunlock(&sc->cfg_sx);
}

static __inline void
myfirst_sync_cfg_write_begin(struct myfirst_softc *sc)
{
        sx_xlock(&sc->cfg_sx);
}

static __inline void
myfirst_sync_cfg_write_end(struct myfirst_softc *sc)
{
        sx_xunlock(&sc->cfg_sx);
}

/*
 * Writer-cap semaphore. Caps concurrent writers at
 * sc->writers_limit. Returns 0 on success, EAGAIN if O_NONBLOCK
 * and semaphore is exhausted, ENXIO if detach happened while
 * blocked.
 */
static __inline int
myfirst_sync_writer_enter(struct myfirst_softc *sc, int ioflag)
{
        if (ioflag & IO_NDELAY) {
                if (!sema_trywait(&sc->writers_sema)) {
                        mtx_lock(&sc->mtx);
                        sc->writers_trywait_failures++;
                        mtx_unlock(&sc->mtx);
                        return (EAGAIN);
                }
        } else {
                sema_wait(&sc->writers_sema);
        }
        if (!atomic_load_acq_int(&sc->is_attached)) {
                sema_post(&sc->writers_sema);
                return (ENXIO);
        }
        return (0);
}

static __inline void
myfirst_sync_writer_leave(struct myfirst_softc *sc)
{
        sema_post(&sc->writers_sema);
}

/*
 * Stats cache sx. Protects a small cached statistic.
 */
static __inline void
myfirst_sync_stats_cache_read_begin(struct myfirst_softc *sc)
{
        sx_slock(&sc->stats_cache_sx);
}

static __inline void
myfirst_sync_stats_cache_read_end(struct myfirst_softc *sc)
{
        sx_sunlock(&sc->stats_cache_sx);
}

static __inline int
myfirst_sync_stats_cache_try_promote(struct myfirst_softc *sc)
{
        return (sx_try_upgrade(&sc->stats_cache_sx));
}

static __inline void
myfirst_sync_stats_cache_downgrade(struct myfirst_softc *sc)
{
        sx_downgrade(&sc->stats_cache_sx);
}

static __inline void
myfirst_sync_stats_cache_write_begin(struct myfirst_softc *sc)
{
        sx_xlock(&sc->stats_cache_sx);
}

static __inline void
myfirst_sync_stats_cache_write_end(struct myfirst_softc *sc)
{
        sx_xunlock(&sc->stats_cache_sx);
}

/*
 * Attach-flag atomic operations. Every context that needs to
 * check "are we still attached?" uses these.
 */
static __inline int
myfirst_sync_is_attached(struct myfirst_softc *sc)
{
        return (atomic_load_acq_int(&sc->is_attached));
}

static __inline void
myfirst_sync_mark_detaching(struct myfirst_softc *sc)
{
        atomic_store_rel_int(&sc->is_attached, 0);
}

static __inline void
myfirst_sync_mark_attached(struct myfirst_softc *sc)
{
        atomic_store_rel_int(&sc->is_attached, 1);
}

#endif /* MYFIRST_SYNC_H */
```

El archivo tiene menos de 200 líneas incluidos los comentarios. Nombra cada operación primitiva. No añade ninguna sobrecarga en tiempo de ejecución. Es el único lugar donde un futuro mantenedor buscará para entender o cambiar la estrategia de sincronización.



## Referencia: Laboratorio extendido: el ejercicio «Romper una primitiva»

Un laboratorio opcional que amplía el material del Capítulo 15. Para cada una de las primitivas que introdujo el Capítulo 15, rómpela deliberadamente y observa el fallo.

Esto tiene un gran valor pedagógico, ya que ver el modo de fallo hace que el uso correcto sea más concreto.

### Romper el límite de escritores

En la Etapa 1, elimina una llamada a `sema_post` en la ruta de escritura (por ejemplo, en la ruta de retorno de error). Vuelve a compilar. Ejecuta la prueba del límite de escritores. El sysctl `writers_sema_value` debería descender con el tiempo sin recuperarse. Eventualmente, todos los escritores se bloquean. Esto demuestra por qué el post debe estar en cada ruta.

Restaura el post. Verifica que el descenso se detiene y que el sema se mantiene equilibrado bajo carga.

### Romper la actualización de sx

En la Etapa 2, elimina la comprobación posterior al fallback de actualización:

```c
sx_sunlock(&sc->stats_cache_sx);
sx_xlock(&sc->stats_cache_sx);
/* re-check removed */
myfirst_stats_cache_refresh(sc);
sx_downgrade(&sc->stats_cache_sx);
```

Vuelve a compilar. Bajo carga intensa de lectores, el refresco ocurre varias veces en rápida sucesión porque múltiples lectores acceden simultáneamente a la ruta de fallback. El contador de refrescos crece mucho más rápido de lo esperado, a una velocidad superior a uno por segundo.

Restaura la comprobación. Verifica que el contador vuelve a estabilizarse en un refresco por segundo.

### Romper el manejo de progreso parcial

En la Etapa 3, elimina la comprobación de progreso parcial en la ruta EINTR:

```c
case EINTR:
case ERESTART:
        return (error);  /* Partial check removed. */
```

Vuelve a compilar. Ejecuta el laboratorio de manejo de señales. Un SIGINT durante una lectura parcialmente completada ahora devuelve EINTR en lugar del recuento parcial de bytes. El código del espacio de usuario que espera la convención UNIX se encontrará con resultados inesperados.

Restaura la comprobación. Verifica que el progreso parcial vuelve a funcionar correctamente.

### Romper la lectura atómica

En la Etapa 4, sustituye `atomic_load_acq_int(&sc->is_attached)` por una lectura simple `sc->is_attached` en la comprobación de entrada de la ruta de lectura. En x86 esto sigue funcionando (modelo de memoria fuerte). En arm64, ocasionalmente puede perderse un detach, produciendo una condición de carrera con ENXIO.

Si no dispones de hardware arm64, este caso es difícil de demostrar experimentalmente. Entiéndelo de forma intelectual y sigue adelante.

Restaura el atómico. La disciplina es la misma independientemente del resultado de la prueba.

### Romper el orden de detach

Intercambia el orden de `seldrain` y `taskqueue_drain` en el detach de la Etapa 4 (como en el escenario de depuración anterior). Ejecuta la prueba de estrés con ciclos de detach. Observa el panic eventual.

Restaura el orden correcto. Verifica la estabilidad.

### Romper la vida útil de `sema_destroy`

Llama a `sema_destroy(&sc->writers_sema)` antes de tiempo, antes de que el taskqueue se haya drenado por completo. En un kernel de depuración, esto provoca un panic con «sema: waiters» en el momento en que un thread sigue dentro de `sema_wait`. La KASSERT se dispara.

Restaura el orden correcto. La destrucción ocurre después de que todos los threads en espera hayan sido drenados.

### Por qué importa esto

Romper código deliberadamente es incómodo. También es la forma más rápida de interiorizar por qué el código correcto está escrito de la manera que está. Cada primitiva del Capítulo 15 tiene modos de fallo; verlos en acción hace que el uso correcto sea inolvidable.

Después de ejecutar el ejercicio de romper-y-observar para cada primitiva, el material del capítulo se sentirá sólido. Sabrás no solo qué hacer, sino por qué, y qué ocurre si omites un paso.



## Referencia: Cuándo dividir un driver

Una metaobservación que encaja aquí mejor que en una sección específica.

La Parte 3 ha desarrollado el driver `myfirst` hasta un tamaño moderado: unas 1200 líneas en el código fuente principal, más la cabecera `myfirst_sync.h`, más el cbuf. Eso es pequeño para un driver real. Los drivers reales oscilan entre 2000 líneas (soporte de dispositivo simple) y 30000 o más (drivers de red con soporte de descarga de hardware).

¿A partir de qué tamaño merece un driver dividirse en varios archivos de código fuente?

Algunas heurísticas.

- **Menos de 1000 líneas**: un solo archivo. El coste de trabajar con varios archivos supera el beneficio de legibilidad.
- **De 1000 a 5000 líneas**: un solo archivo sigue siendo adecuado. Usa marcadores de sección claros en el archivo.
- **De 5000 a 15000 líneas**: dos o tres archivos. División típica: lógica principal de attach/detach en `foo.c`, lógica de subsistema dedicada (por ejemplo, un gestor de ring buffer) en `foo_ring.c`, definiciones de registros de hardware en `foo_reg.h`.
- **Más de 15000 líneas**: el diseño modular es necesario. Una cabecera para las estructuras compartidas; varios archivos de implementación para los subsistemas; un `foo.c` de nivel superior que los une.

El driver `myfirst` en la Etapa 4 del Capítulo 15 es cómodo como un único archivo más una cabecera de sincronización. A medida que los capítulos posteriores añadan lógica específica de hardware, surgirá una división natural: `myfirst_reg.h` para las definiciones de registros, `myfirst_intr.c` para el código relacionado con interrupciones, `myfirst_io.c` para la ruta de datos. Esas divisiones ocurrirán en la Parte 4 cuando la historia del hardware las justifique.

La regla general: divide cuando un archivo supere lo que puedes mantener mentalmente de una sola vez. Para la mayoría de los lectores, eso ronda las 2000 a 5000 líneas. Divide antes si el límite del subsistema es natural; divide más tarde si el código está tan entrelazado que una división resultaría forzada.



## Referencia: Resumen final de la Parte 3

La Parte 3 fue un recorrido por cinco primitivas y su composición. Un resumen final enmarca lo que se ha conseguido.

El **Capítulo 11** introdujo la concurrencia en el driver. Un usuario se convirtió en muchos usuarios. El mutex lo hizo seguro.

El **Capítulo 12** introdujo el bloqueo. Los lectores y escritores podían esperar cambios de estado. Las variables de condición hicieron la espera eficiente; los sx locks hicieron el acceso a la configuración escalable.

El **Capítulo 13** introdujo el tiempo. Los callouts permitieron al driver actuar por sí mismo en momentos elegidos. Los callouts con conciencia de locks y el drenaje en el detach hicieron seguros los temporizadores.

El **Capítulo 14** introdujo el trabajo diferido. Las tasks permitieron que los callouts y otros contextos de borde delegaran el trabajo en threads que realmente podían realizarlo. Los taskqueues privados y la coalescencia hicieron eficiente la primitiva.

El **Capítulo 15** introdujo las primitivas de coordinación restantes. Los semáforos limitaron la concurrencia. Los patrones refinados de sx habilitaron cachés de solo lectura mayoritaria. Las esperas interrumpibles por señales con progreso parcial preservaron las convenciones UNIX. Las operaciones atómicas hicieron baratos los flags entre contextos. La encapsulación hizo legible todo el vocabulario.

Juntos, los cinco capítulos construyeron un driver con una historia de sincronización interna completa. El driver en `0.9-coordination` no tiene ninguna característica de sincronización ausente; cada invariante que le importa tiene una primitiva con nombre, una operación con nombre en la cabecera de envoltura, una sección con nombre en `LOCKING.md` y una prueba en el kit de estrés.

La Parte 4 añade la historia del hardware. La historia de sincronización permanece.



## Referencia: Lista de verificación de entregables del Capítulo 15

Antes de cerrar el Capítulo 15, confirma que todos los entregables están en su lugar.

### Contenido del capítulo

- [ ] `content/chapters/part3/chapter-15.md` existe.
- [ ] Las secciones 1 a 8 están escritas.
- [ ] La sección de temas adicionales está escrita.
- [ ] Los laboratorios prácticos están escritos.
- [ ] Los ejercicios de desafío están escritos.
- [ ] La referencia de solución de problemas está escrita.
- [ ] La sección de cierre de la Parte 3 está escrita.
- [ ] El puente al Capítulo 16 está escrito.

### Ejemplos

- [ ] El directorio `examples/part-03/ch15-more-synchronization/` existe.
- [ ] `stage1-writers-sema/` tiene un driver funcional.
- [ ] `stage2-stats-cache/` tiene un driver funcional.
- [ ] `stage3-interruptible/` tiene un driver funcional.
- [ ] `stage4-final/` tiene el driver consolidado.
- [ ] Cada etapa tiene un `Makefile`.
- [ ] `stage4-final/` tiene `myfirst_sync.h`.
- [ ] `labs/` tiene programas de prueba y scripts.
- [ ] `README.md` describe cada etapa.
- [ ] `LOCKING.md` tiene el mapa de sincronización actualizado.

### Documentación

- [ ] El código fuente principal tiene un comentario al inicio del archivo que resume las adiciones del Capítulo 15.
- [ ] Cada nuevo sysctl tiene una cadena de descripción.
- [ ] Cada nuevo campo de estructura tiene un comentario.
- [ ] Las secciones de `LOCKING.md` coinciden con el driver.

### Pruebas

- [ ] El driver de la etapa 4 compila limpiamente.
- [ ] El driver de la etapa 4 supera WITNESS.
- [ ] Las pruebas de regresión de los Capítulos 11-14 siguen pasando.
- [ ] Las pruebas específicas del Capítulo 15 pasan.
- [ ] El detach bajo carga es limpio.

Un driver y un capítulo que superen esta lista de verificación están terminados.



## Referencia: Una invitación a experimentar

Antes de cerrar, una última invitación.

El driver del Capítulo 15 es un vehículo de aprendizaje, no un producto listo para distribuir. Cada primitiva que usa es real; cada técnica que demuestra se utiliza en drivers reales. Pero el driver en sí está deliberadamente construido para ejercitar el abanico completo de primitivas en un solo lugar. Un driver real normalmente usa un subconjunto.

Al cerrar la Parte 3, considera experimentar más allá de los ejemplos resueltos del capítulo.

- Añade un segundo tipo de control de admisión: un semáforo que limite tanto los lectores concurrentes como los escritores. ¿Mejora o perjudica al sistema? ¿Por qué?
- Añade un watchdog que provoque un timeout en una única operación de escritura (no en el driver completo). Impleméntalo con una task de timeout. ¿Qué casos límite encuentras?
- Convierte el sx de configuración en un conjunto de campos atómicos. Mide la diferencia de rendimiento con DTrace. ¿Qué diseño enviarías a producción? ¿Por qué?
- Escribe un arnés de prueba en espacio de usuario en C que ejercite el driver de maneras que el shell no puede. ¿A qué primitivas recurres?
- Lee un driver real en `/usr/src/sys/dev/` e identifica una sola decisión de sincronización que haya tomado. ¿Estás de acuerdo con la decisión? ¿Qué habrías elegido tú en su lugar, y por qué?

Cada experimento es un día o dos de trabajo. Cada uno enseña más que un capítulo de texto. El driver `myfirst` es un laboratorio; el código fuente de FreeBSD es una biblioteca; tu propia curiosidad es el programa.

La Parte 3 te ha enseñado las primitivas. El resto es práctica. Buena suerte con la Parte 4.


## Referencia: `cv_signal` frente a `cv_broadcast`, con precisión

Una pregunta recurrente al leer o escribir código de driver: ¿debería una señal despertar a un único thread en espera (`cv_signal`) o a todos ellos (`cv_broadcast`)? La respuesta no siempre es obvia; esta referencia lo analiza en profundidad.

### La diferencia semántica

`cv_signal(&cv)` despierta como máximo a un thread actualmente bloqueado en el cv. Si hay varios threads en espera, exactamente uno de ellos se despierta; el kernel elige cuál (normalmente en orden FIFO, pero esto no está garantizado por la API).

`cv_broadcast(&cv)` despierta a todos los threads actualmente bloqueados en el cv. Cada thread bloqueado se despierta y vuelve a competir por el mutex.

### Cuándo `cv_signal` es correcto

Deben cumplirse dos condiciones para que `cv_signal` sea seguro.

**Cualquier thread en espera puede satisfacer el cambio de estado.** Si envías la señal porque se liberó un hueco en un buffer acotado, y cualquier thread en espera puede ocupar ese hueco, signal es apropiado. Un único despertar es suficiente.

**Todos los threads en espera son equivalentes.** Si cada thread en espera ejecuta el mismo predicado y respondería de la misma manera al despertar, signal es apropiado. Despertar solo a uno evita el efecto de la estampida (thundering herd) que se produciría al despertar a todos y que todos menos uno vuelvan a dormirse inmediatamente.

Ejemplo clásico: un productor/consumidor con un elemento recién producido. Despertar a un único consumidor es suficiente; despertar a todos despertaría a muchos consumidores que luego verían una cola vacía y volverían a dormirse.

### Cuándo `cv_broadcast` es correcto

Algunos casos específicos hacen que broadcast sea la elección correcta.

**Varios threads en espera pueden tener éxito.** Si un cambio de estado desbloquea a más de uno (por ejemplo, «el buffer acotado pasó de estar lleno a tener 10 posiciones libres»), el broadcast los despierta a todos y cada uno puede intentarlo. Señalar solo a uno dejaría a los demás bloqueados aunque el progreso sea posible.

**Los threads en espera tienen predicados distintos.** Si algunos threads esperan «bytes > 0» y otros «bytes > 100», una señal podría despertar a un thread cuyo predicado no se cumple, mientras que otro cuyo predicado sí se cumple permanece dormido. `cv_broadcast` garantiza que cada thread en espera reevalúe su propio predicado.

**Apagado o invalidación de estado.** Cuando el driver hace detach, todos los threads en espera deben ver el cambio y salir. `cv_broadcast` es necesario porque todos deben retornar, no solo uno.

El driver de los capítulos 12 a 15 usa `cv_broadcast` en el detach (`cv_broadcast(&sc->data_cv)`, `cv_broadcast(&sc->room_cv)`) exactamente por esta razón. Usa `cv_signal` en las transiciones normales del estado del buffer porque cada transición desbloquea como máximo a un thread en espera de forma productiva.

### Un caso sutil: el sysctl de reinicio

El capítulo 12 añadió un sysctl de reinicio que vacía el cbuf. Tras el reinicio, el buffer está vacío y tiene capacidad completa. ¿Cuál es el wakeup correcto?

```c
cv_broadcast(&sc->room_cv);   /* Room is now fully available. */
```

El driver usa `cv_broadcast`. ¿Por qué no signal? Porque el reinicio desbloqueó potencialmente a muchos escritores que estaban esperando espacio. Despertarlos a todos les permite a todos volver a comprobar. Un signal solo despertaría a uno; los demás permanecerían bloqueados hasta que el camino de escritura enviara una señal byte a byte más adelante.

Este es el caso de «varios waiters pueden tener éxito». Broadcast es lo correcto.

### Consideración sobre el coste

`cv_broadcast` es más costoso que `cv_signal`. Cada thread despertado hace trabajar al planificador, y cada thread que despierta y vuelve a dormirse inmediatamente paga el coste del cambio de contexto. Para una cv con muchos waiters, broadcast puede ser caro.

Para una cv con uno o dos waiters habituales, la diferencia de coste es insignificante. Usa el que sea semánticamente correcto.

### Reglas generales

- **Wakeup de lectura bloqueante tras llegar un byte**: `cv_signal`. Un byte puede desbloquear como máximo a un lector.
- **Wakeup de escritura bloqueante tras vaciarse bytes**: depende de cuántos bytes se hayan vaciado. Si se vació un byte, signal es suficiente. Si el buffer se vació mediante un reinicio, broadcast.
- **Detach**: siempre `cv_broadcast`. Cada waiter debe salir.
- **Reinicio o invalidación de estado que pueda desbloquear a muchos**: `cv_broadcast`.
- **Cambio de estado incremental normal**: `cv_signal`.

En caso de duda, `cv_broadcast` es correcto (aunque más costoso). Prefiere signal cuando puedas demostrar que es suficiente.



## Referencia: el caso infrecuente en que escribes tu propio semáforo

Un experimento mental. Si `sema(9)` no existiera, ¿cómo implementarías un semáforo contador usando solo un mutex y una cv?

```c
struct my_sema {
        struct mtx      mtx;
        struct cv       cv;
        int             value;
};

static void
my_sema_init(struct my_sema *s, int value, const char *name)
{
        mtx_init(&s->mtx, name, NULL, MTX_DEF);
        cv_init(&s->cv, name);
        s->value = value;
}

static void
my_sema_destroy(struct my_sema *s)
{
        mtx_destroy(&s->mtx);
        cv_destroy(&s->cv);
}

static void
my_sema_wait(struct my_sema *s)
{
        mtx_lock(&s->mtx);
        while (s->value == 0)
                cv_wait(&s->cv, &s->mtx);
        s->value--;
        mtx_unlock(&s->mtx);
}

static void
my_sema_post(struct my_sema *s)
{
        mtx_lock(&s->mtx);
        s->value++;
        cv_signal(&s->cv);
        mtx_unlock(&s->mtx);
}
```

Compacto, correcto y funcionalmente idéntico a `sema(9)` para los casos simples. Leer esto deja claro lo que `sema(9)` hace internamente: exactamente esto, envuelto en una API.

¿Por qué existe `sema(9)` si es tan sencillo de escribir? Por varias razones:

- Extrae el código de cada driver que de otro modo lo reinventaría.
- Proporciona una primitiva documentada y probada con soporte de trazado.
- Optimiza el post para evitar cv_signal cuando no hay waiters en espera.
- Proporciona un vocabulario coherente para la revisión de código.

El mismo argumento se aplica a cada primitiva del kernel. Podrías implementar tu propio mutex, cv, sx, taskqueue, callout. No lo haces porque las primitivas del kernel están mejor probadas, mejor documentadas y son mejor comprendidas por la comunidad. Úsalas.

La excepción es una primitiva que no existe en el kernel. Si tu driver necesita un idioma de sincronización específico que ninguna primitiva del kernel proporciona, implementarlo está justificado. Documéntalo con cuidado.



## Referencia: una reflexión final sobre la sincronización en el kernel

Una observación que abarca la Parte 3.

Cada primitiva de sincronización del kernel se construye a partir de primitivas más simples. En la base hay un spinlock (técnicamente, una operación compare-and-swap sobre una posición de memoria, más barreras). Por encima de los spinlocks están los mutexes (spinlocks más herencia de prioridad más sleeping). Por encima de los mutexes están las variables de condición (colas de sleep más transferencia del mutex). Por encima de las cvs están los sx locks (cv más un contador de lectores). Por encima de los sx están los semáforos (cv más un contador). Por encima de los semáforos están las primitivas de nivel superior (taskqueues, gtaskqueues, epochs).

Cada capa añade una capacidad específica y oculta la complejidad de la capa inferior. Cuando llamas a `sema_wait`, no piensas en la cv que hay dentro, ni en el mutex dentro de la cv, ni en el spinlock dentro del mutex, ni en el CAS dentro del spinlock. La abstracción funciona.

El beneficio de esta estructura en capas es que puedes razonar sobre una capa a la vez. El beneficio de conocer las capas es que cuando una falla, puedes descender a la que hay debajo y depurar.

La Parte 3 introdujo las primitivas de cada capa en orden. La Parte 4 las utiliza. Si un bug de la Parte 4 te desconcierta, el diagnóstico puede requerir descender: desde «el taskqueue está bloqueado» hasta «el callback de la tarea está bloqueado en un mutex», luego «el mutex está en manos de un thread esperando una cv», y finalmente «la cv está esperando un cambio de estado que nunca ocurrirá por culpa de otro bug». Las herramientas para este descenso son las primitivas que ya conoces.

Ese es el verdadero beneficio de la Parte 3. No un patrón de driver concreto, aunque eso tiene valor. No un conjunto específico de llamadas a la API, aunque esas son necesarias. El verdadero beneficio es un modelo mental de sincronización que escala con la complejidad del problema. Ese modelo es lo que te llevará a través de la Parte 4 y el resto del libro.



El capítulo 15 está completo. La Parte 3 está completa.

Continúa con el capítulo 16.

## Referencia: una nota final sobre la disciplina de las pruebas

Cada capítulo de la Parte 3 terminó con pruebas. La disciplina fue constante: añadir una primitiva, refactorizar el driver, escribir una prueba que la ejercite, ejecutar toda la suite de regresión y actualizar `LOCKING.md`.

Esa disciplina es lo que convierte una secuencia de capítulos en un conjunto de código mantenible. Sin ella, el driver sería un mosaico de funcionalidades que funcionan por separado y fallan en combinación. Con ella, las incorporaciones de cada capítulo se integran con lo que había antes.

Mantén la disciplina en la Parte 4. El hardware introduce nuevas primitivas (manejadores de interrupciones, asignaciones de recursos, etiquetas DMA) y nuevos modos de fallo (condiciones de carrera a nivel de hardware, corrupción de DMA, sorpresas en el orden de los registros). Cada incorporación merece su propia prueba, su propia entrada en la documentación y su propia integración en la suite de regresión existente.

El coste de la disciplina es una pequeña cantidad de trabajo adicional por capítulo. El beneficio es que el driver, en cualquier etapa de desarrollo, está siempre listo para distribuirse. Puedes entregárselo a un colega y funcionará. Puedes dejarlo de lado seis meses, volver y seguir entendiendo qué hace. Puedes añadir una funcionalidad más sin temer que algo sin relación deje de funcionar.

Ese es el beneficio definitivo de la Parte 3. No solo primitivas; no solo patrones; una disciplina que funciona.
