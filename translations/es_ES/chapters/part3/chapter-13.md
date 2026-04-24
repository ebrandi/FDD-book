---
title: "Temporizadores y trabajo diferido"
description: "Cómo los drivers de FreeBSD gestionan el tiempo: programando trabajo para el futuro con callout(9), ejecutándolo de forma segura bajo un lock documentado y desmontándolo sin condiciones de carrera al descargar el módulo."
partNumber: 3
partName: "Concurrency and Synchronization"
chapter: 13
lastUpdated: "2026-04-18"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 195
language: "es-ES"
---
# Temporizadores y trabajo diferido

## Guía para el lector y resultados esperados

Hasta ahora, cada línea de código de driver que hemos escrito ha sido *reactiva*: el usuario llama a `read(2)`, el kernel llama a nuestro manejador, hacemos el trabajo y retornamos. Las primitivas de bloqueo del Capítulo 12 ampliaron ese modelo con la capacidad de *esperar* algo que nosotros no iniciamos. Pero el driver en sí nunca se asomaba al mundo por iniciativa propia. No tenía manera de decir "dentro de 100 milisegundos, por favor haz esto". No tenía manera de contar el tiempo en absoluto, salvo como algo que observaba transcurrir mientras ya estaba dentro de una syscall.

Eso cambia aquí. El Capítulo 13 introduce el *tiempo* como un concepto de primera clase en tu driver. El kernel dispone de todo un subsistema dedicado a ejecutar tu código en un momento concreto del futuro, de manera periódica si así lo pides, con reglas precisas de gestión de locks y semántica de limpieza ordenada. Se llama `callout(9)`, y es pequeño, regular y enormemente útil. Al final de este capítulo, tu driver `myfirst` habrá aprendido a planificar su propio trabajo, a actuar sobre el mundo sin que nadie lo estimule, y a liberar su trabajo planificado de forma segura cuando se descargue el dispositivo.

La estructura del capítulo refleja la del Capítulo 12. Cada nueva primitiva llega acompañada de una motivación, un recorrido preciso por la API, una refactorización guiada del driver en uso, un diseño verificado con `WITNESS` y una historia de locking documentada en `LOCKING.md`. El capítulo no se detiene en "ya puedes llamar a `callout_reset`"; te conduce por cada contrato que le importa al subsistema callout, de modo que los temporizadores que escribas sigan funcionando a medida que evolucione el resto del driver.

### Por qué este capítulo merece su propio espacio

Podrías intentar simular temporizadores. Un thread del kernel que duerme en bucle llamando a `cv_timedwait_sig` y hace trabajo cada vez que se despierta es, técnicamente, un temporizador. También lo es un proceso en espacio de usuario que abre el dispositivo una vez por segundo y toca un sysctl. Ninguno de los dos enfoques es incorrecto, pero ambos son torpes comparados con lo que ofrece el kernel, y ambos crean nuevos recursos (el thread del kernel, el proceso en espacio de usuario) cuyo ciclo de vida hay que gestionar por separado.

`callout(9)` es la respuesta correcta en casi todos los casos en que quieras que una función se ejecute "más adelante". Está construido sobre la infraestructura de reloj hardware del kernel, no cuesta prácticamente nada en reposo, escala a miles de callouts pendientes por sistema, se integra con `WITNESS` e `INVARIANTS`, y proporciona reglas claras sobre cómo interactuar con los locks y cómo drenar el trabajo pendiente en el momento del desmontaje. La mayoría de los drivers en `/usr/src/sys/dev/` lo usan. Una vez que lo conoces, el patrón se transfiere a cualquier tipo de driver que encuentres: USB, red, almacenamiento, watchdog, sensor, cualquier dispositivo cuyo mundo físico tenga un reloj.

El coste de *no* conocer `callout(9)` es alto. Un driver que reinventa la temporización crea un subsistema privado que nadie más sabe depurar. Un driver que usa `callout(9)` correctamente encaja en las herramientas de observabilidad existentes del kernel (`procstat`, `dtrace`, `lockstat`, `ddb`) y se comporta de manera predecible al descargar. El capítulo se paga solo la primera vez que tienes que extender un driver escrito por otra persona.

### Dónde dejó el driver el Capítulo 12

Un breve repaso de en qué punto deberías encontrarte, porque el Capítulo 13 parte directamente de las entregas del Capítulo 12. Si alguno de los puntos siguientes falta o te resulta incierto, vuelve al Capítulo 12 antes de empezar este.

- Tu driver `myfirst` compila sin errores y está en la versión `0.6-sync`.
- Usa los macros `MYFIRST_LOCK(sc)` / `MYFIRST_UNLOCK(sc)` alrededor de `sc->mtx` (el mutex de la ruta de datos).
- Usa `MYFIRST_CFG_SLOCK(sc)` / `MYFIRST_CFG_XLOCK(sc)` alrededor de `sc->cfg_sx` (el sx de configuración).
- Usa dos variables de condición con nombre (`sc->data_cv`, `sc->room_cv`) para lecturas y escrituras bloqueantes.
- Admite lecturas con tiempo límite mediante `cv_timedwait_sig` y el sysctl `read_timeout_ms`.
- El orden de locks `sc->mtx -> sc->cfg_sx` está documentado en `LOCKING.md` y es verificado por `WITNESS`.
- `INVARIANTS` y `WITNESS` están habilitados en tu kernel de pruebas; lo has compilado y arrancado.
- El kit de estrés del Capítulo 12 (los testers del Capítulo 11 más `timeout_tester` y `config_writer`) compila y se ejecuta correctamente.

Ese driver es el que extendemos en el Capítulo 13. Añadiremos un callout periódico, luego un callout watchdog, luego una fuente de ticks configurable, y por último los consolidaremos con una pasada de refactorización y una actualización de la documentación. La ruta de datos del driver permanece igual que estaba; el nuevo código vive junto a las primitivas existentes.

### Lo que aprenderás

Cuando pases al siguiente capítulo, serás capaz de:

- Explicar cuándo un callout es la primitiva adecuada para el trabajo y cuándo un thread del kernel, un `cv_timedwait` o un helper en espacio de usuario servirían mejor.
- Inicializar un callout con la gestión de lock apropiada usando `callout_init`, `callout_init_mtx`, `callout_init_rw` o `callout_init_rm`, y elegir entre las variantes con lock gestionado y las `mpsafe` según el contexto de tu driver.
- Planificar un temporizador de un solo disparo con `callout_reset` (basado en ticks) o `callout_reset_sbt` (precisión por debajo del tick), usando las constantes de tiempo `tick_sbt`, `SBT_1S`, `SBT_1MS` y `SBT_1US` cuando sea apropiado.
- Planificar un temporizador periódico haciendo que el callback se rearme a sí mismo, con el patrón correcto que sobrevive a `callout_drain`.
- Elegir entre `callout_reset` y `callout_schedule` y entender cuándo cada uno es la herramienta adecuada.
- Describir el contrato de lock que `callout(9)` impone cuando lo inicializas con un puntero a lock: el kernel adquiere ese lock antes de que se ejecute tu función, lo libera después (salvo que hayas indicado `CALLOUT_RETURNUNLOCKED`), y serializa el callout respecto a otros titulares del lock.
- Leer e interpretar los campos `c_iflags` y `c_flags` de un callout, y usar `callout_pending`, `callout_active` y `callout_deactivate` correctamente.
- Usar `callout_stop` para cancelar un callout pendiente en el código normal del driver, y `callout_drain` en el desmontaje para esperar a que termine un callback en vuelo.
- Reconocer la condición de carrera en la descarga (un callout se dispara después de `kldunload` y hace caer el kernel) y describir la solución estándar: drenar en detach, rechazar detach hasta que el dispositivo esté inactivo.
- Aplicar el patrón `is_attached` (que construimos para los waiters de cv en el Capítulo 12) a los callbacks de callout, de modo que un callback que se dispara durante el desmontaje retorne limpiamente sin reprogramarse.
- Construir un temporizador watchdog que detecte una condición atascada y actúe sobre ella.
- Construir un temporizador de debounce que ignore eventos repetidos en rápida sucesión.
- Construir una fuente de ticks periódica que inyecte datos sintéticos en el cbuf para pruebas.
- Verificar el driver con callouts habilitados frente a `WITNESS`, `lockstat(1)` y una prueba de estrés prolongada que incluya actividad de temporizadores.
- Ampliar `LOCKING.md` con una sección "Callouts" que nombre cada callout, su callback, su lock y su ciclo de vida.
- Refactorizar el driver a una forma en la que el código de temporización esté agrupado, nombrado y sea obviamente seguro de mantener.

Es una lista sustancial. Nada de ella es opcional para un driver que use el tiempo en absoluto. Todo ello se transfiere directamente a los drivers que aparecerán en la Parte 4 y más allá, donde el hardware real trae sus propios relojes y exige sus propios watchdogs.

### Lo que este capítulo no cubre

Varios temas adyacentes se difieren deliberadamente:

- **Taskqueues (`taskqueue(9)`).** El Capítulo 16 introduce el framework de trabajo diferido de propósito general del kernel. Los taskqueues y los callouts son complementarios: los callouts ejecutan una función en un momento concreto; los taskqueues ejecutan una función tan pronto como un thread worker pueda recogerla. Muchos drivers usan ambos: un callout se dispara en el momento oportuno, el callout encola una tarea, y la tarea ejecuta el trabajo real en contexto de proceso donde se permite dormir. El Capítulo 13 permanece dentro del propio callback del callout por simplicidad; el patrón de trabajo diferido pertenece al Capítulo 16.
- **Manejadores de interrupciones hardware.** El Capítulo 14 introduce las interrupciones. Un driver real puede instalar un manejador de interrupciones que se ejecute sin contexto de proceso. Las reglas de `callout(9)` en torno a las clases de lock son similares a las reglas para los manejadores de interrupciones (no se puede dormir), pero el enfoque es diferente. Revisitaremos la interacción temporizador-interrupción en el Capítulo 14.
- **`epoch(9)`.** Un framework de sincronización de solo lectura utilizado por los drivers de red. Fuera del alcance del Capítulo 13.
- **Planificación de eventos de alta resolución.** El kernel expone `sbintime_t` y las variantes `_sbt` para precisión por debajo del tick; abordamos brevemente las variantes basadas en sbintime de la API de callout, pero la historia completa de los drivers de temporizadores de eventos (`/usr/src/sys/kern/kern_clocksource.c`) pertenece a un libro de internos del kernel, no a un libro de drivers.
- **Planificación en tiempo real y por plazo.** Fuera del alcance. Nos apoyamos en el planificador general.
- **Cargas de trabajo periódicas mediante el tick del planificador (`hardclock`).** El propio kernel usa `hardclock(9)` para el trabajo periódico de todo el sistema; los drivers no interactúan directamente con `hardclock`. Lo mencionamos para contextualizar.

Mantenernos dentro de esos límites mantiene el capítulo enfocado. El lector del Capítulo 13 debería terminarlo con un control seguro de `callout(9)` y una idea clara de cuándo usar `taskqueue(9)` en su lugar. El Capítulo 14 y el Capítulo 16 completan el resto.

### Inversión de tiempo estimada

- **Solo lectura**: unas tres horas. La superficie de la API es pequeña, pero las reglas de lock y ciclo de vida merecen atención detenida.
- **Lectura más escritura de los ejemplos guiados**: de seis a ocho horas en dos sesiones. El driver evoluciona en cuatro etapas pequeñas; cada etapa añade un patrón de temporizador.
- **Lectura más todos los laboratorios y desafíos**: de diez a catorce horas en tres o cuatro sesiones, incluyendo tiempo para pruebas de estrés con temporizadores activos y mediciones con lockstat.

Si te encuentras confundido a mitad de la Sección 5 (las reglas del contexto de lock), es normal. La interacción entre callouts y locks es la parte más sorprendente de la API, e incluso programadores de kernel experimentados la hacen mal de vez en cuando. Para, vuelve a leer el ejemplo guiado de la Sección 5 y continúa cuando el modelo se haya asentado.

### Requisitos previos

Antes de comenzar este capítulo, confirma:

- El código fuente de tu driver coincide con el Stage 4 del Capítulo 12 (`stage4-final`). El punto de partida asume que los canales cv, las lecturas acotadas, la configuración protegida por sx y el sysctl de reinicio están todos en su lugar.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con `/usr/src` en disco y coincidiendo con el kernel en ejecución.
- Un kernel de depuración con `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` está compilado, instalado y arrancando correctamente.
- Has leído el Capítulo 12 con detenimiento. La disciplina de orden de locks, el patrón cv y el patrón snapshot-and-apply son conocimientos que aquí se dan por sentados.
- Has ejecutado el kit de estrés compuesto del Capítulo 12 al menos una vez y lo has visto pasar sin problemas.

Si alguno de los puntos anteriores es inestable, arreglarlo ahora es una inversión mucho mejor que avanzar por el Capítulo 13 e intentar depurar desde una base en movimiento.

### Cómo sacar el máximo partido a este capítulo

Tres hábitos darán sus frutos rápidamente.

Primero, ten marcados como favoritos `/usr/src/sys/kern/kern_timeout.c` y `/usr/src/sys/sys/callout.h`. La cabecera es corta y contiene la API que enseña el capítulo. El archivo de implementación es largo pero bien comentado; apuntaremos a funciones concretas en él unas cuantas veces a lo largo del capítulo. Dos minutos de atención en cada referencia merecen la pena.

> **Una nota sobre los números de línea.** Cuando este capítulo nombre una función concreta en `kern_timeout.c` o un macro en `callout.h`, trata el nombre como la dirección fija y cualquier número de línea que mencionemos como decorado a su alrededor. `callout_init_mtx` y `callout_reset_sbt_on` seguirán llevando esos nombres en futuras revisiones de FreeBSD 14.x; las líneas donde residan habrán cambiado para cuando tu editor abra el archivo. Salta al símbolo y deja que tu editor informe del resto.

Segundo, ejecuta cada cambio de código que hagas bajo `WITNESS`. El subsistema callout tiene sus propias reglas que `WITNESS` comprueba en tiempo de ejecución. El error más común del Capítulo 13 es planificar un callout cuya función intenta adquirir un lock dormible desde un contexto donde dormir está prohibido; `WITNESS` lo detecta inmediatamente en un kernel de depuración y corrompe silenciosamente en un kernel de producción. No ejecutes el código del Capítulo 13 en el kernel de producción hasta que pase el kernel de depuración.

Tercero, escribe los cambios a mano. El código fuente complementario en `examples/part-03/ch13-timers-and-delayed-work/` es la versión canónica, pero la memoria muscular de escribir `callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)` una sola vez vale más que leerlo diez. El capítulo muestra los cambios de forma incremental; reproduce ese ritmo incremental en tu propia copia del driver.

### Hoja de ruta del capítulo

Las secciones, en orden, son:

1. Por qué usar timers en un driver. Dónde entra el tiempo en el trabajo real con drivers y qué patrones encajan en `callout(9)` en lugar de en otra cosa.
2. Introducción a la API `callout(9)` de FreeBSD. La estructura, el ciclo de vida, las cuatro variantes de inicialización y qué ofrece cada una.
3. Programación de eventos de disparo único y repetitivos. `callout_reset`, `callout_reset_sbt`, `callout_schedule` y el patrón de rearme periódico del callback.
4. Integración de timers en tu driver. La primera refactorización: un callout de heartbeat que registra estadísticas periódicamente o dispara un evento sintético.
5. Gestión del locking y el contexto en los timers. La inicialización con conocimiento del lock, los flags `CALLOUT_RETURNUNLOCKED` y `CALLOUT_SHAREDLOCK`, y las reglas sobre qué puede y qué no puede hacer una función callout.
6. Limpieza de timers y gestión de recursos. La condición de carrera en la descarga, `callout_stop` frente a `callout_drain`, y el patrón estándar de detach con timers.
7. Casos de uso y extensiones para trabajo temporizado. Watchdogs, debouncing, sondeo periódico, reintentos con retardo, rotaciones de estadísticas, todos presentados como pequeñas recetas que puedes trasladar a otros drivers.
8. Refactorización y versionado. Una extensión limpia de `LOCKING.md`, una cadena de versión actualizada, un registro de cambios actualizado y una ejecución de regresión que incluye pruebas relacionadas con timers.

A continuación vienen los laboratorios prácticos y los ejercicios de desafío, seguidos de una referencia para la resolución de problemas, una sección de cierre y un puente hacia el Capítulo 14.

Si es tu primera lectura, avanza de forma lineal y realiza los laboratorios en orden. Si estás revisando el material, la sección de limpieza (Sección 6) y la sección de refactorización (Sección 8) son independientes y se pueden leer cómodamente en una sola sesión.



## Sección 1: ¿Por qué usar timers en un driver?

La mayor parte de lo que hace un driver es reactivo. Un usuario abre el dispositivo y el handler de apertura se ejecuta. Un usuario invoca `read(2)` y el handler de lectura se ejecuta. Llega una señal y quien estaba en espera despierta. El kernel cede el control al driver en respuesta a algo que ocurrió en el mundo exterior. Cada invocación tiene una causa clara, y una vez completado el trabajo, el driver regresa y espera la siguiente causa.

El hardware real no siempre coopera con ese modelo. Una tarjeta de red puede necesitar enviar un heartbeat cada pocos segundos aunque no ocurra nada más, simplemente para convencer al switch del otro extremo de que el enlace está activo. Un controlador de almacenamiento puede necesitar un reset del watchdog cada quinientos milisegundos; de lo contrario, asumirá que el host ha desaparecido y reiniciará el canal. El sondeo de un hub USB debe hacerse mediante un timer porque el bus USB no genera interrupciones para el tipo de cambio de estado que el driver necesita detectar. Un botón en una placa de desarrollo necesita debouncing porque los contactos de muelle producen muchos eventos en rápida sucesión cuando el usuario solo pretendía presionarlo una vez. Un driver que reintenta un fallo transitorio debería hacer un back-off, no un bucle cerrado.

Todas esas son razones para programar código para el futuro. El kernel dispone de un único primitivo para ello, `callout(9)`, y el Capítulo 13 lo enseña desde los fundamentos. Antes de llegar a la API, esta sección establece el escenario conceptual. Examinamos qué significa "más tarde" en un driver, qué formas suele tomar un callback de "más tarde" y dónde encaja `callout(9)` en relación con las otras formas en que un driver puede expresar la idea del tiempo.

### Las tres formas de «más tarde»

El código de un driver que quiere hacer algo en el futuro encaja en una de estas tres formas. Saber en cuál de ellas te encuentras es la mitad del trabajo a la hora de elegir el primitivo adecuado.

**Disparo único.** «Haz esto una vez, X milisegundos a partir de ahora, y olvídalo.» Ejemplos: programar un timeout de watchdog que solo se dispare si no se observa actividad en el próximo segundo; hacer debouncing de un botón ignorando todas las pulsaciones posteriores durante cincuenta milisegundos; posponer un paso de desmontaje hasta que la operación actual se complete. El callback se ejecuta una vez y el driver no lo vuelve a armar.

**Periódico.** «Haz esto cada X milisegundos, hasta que te diga que pares.» Ejemplos: sondear un registro de hardware que no genera interrupciones; emitir un heartbeat a un peer; refrescar un valor en caché; muestrear un sensor; rotar una ventana de estadísticas. El callback se ejecuta una vez, luego se vuelve a armar para el siguiente intervalo, y continúa así hasta que el driver lo detiene.

**Espera acotada.** «Haz esto cuando la condición Y se cumpla, pero desiste si Y no ha ocurrido en X milisegundos.» Ejemplos: esperar una respuesta de hardware con timeout; esperar a que el buffer se vacíe o a que se dispare el plazo; permitir que Ctrl-C interrumpa una espera. Nos encontramos con esta forma en el Capítulo 12 con `cv_timedwait_sig`. El thread del driver es el que espera, no un callback.

`callout(9)` es el primitivo para las dos primeras formas. La tercera utiliza `cv_timedwait_sig` (Capítulo 12), `mtx_sleep` con un `timo` distinto de cero, o una de las variantes `_sbt` para precisión por debajo del tick. Ambos son complementarios, no alternativos: muchos drivers usan los dos. Una espera acotada suspende el thread que llama; un callout se ejecuta en un contexto separado tras un retardo.

### Patrones del mundo real

Un breve recorrido por los patrones que se repiten en los drivers de `/usr/src/sys/dev/`. Reconocerlos pronto te proporciona un vocabulario para el resto del capítulo.

**El heartbeat.** Un callout periódico que se dispara cada N milisegundos y emite algún estado trivial (un incremento de contador, una línea de log, un paquete transmitido). Útil para la depuración y para los protocolos que necesitan una señal de actividad.

**El watchdog.** Un callout de disparo único programado al inicio de una operación. Si la operación se completa normalmente, el driver cancela el callout. Si la operación se bloquea, el callout se dispara y el driver toma medidas correctoras (reiniciar el hardware, registrar un aviso, eliminar una petición atascada). Casi todos los drivers de almacenamiento y red tienen al menos un watchdog.

**El debounce.** Un callout de disparo único que se programa cuando llega un evento. Los eventos idénticos posteriores dentro del timeout se ignoran. Cuando el callout se dispara, el driver actúa sobre el evento más reciente. Se usa para eventos de hardware que producen rebotes (interruptores mecánicos, sensores ópticos).

**El sondeo.** Un callout periódico que lee un registro de hardware y actúa según el valor. Se usa cuando el hardware no genera interrupciones para los eventos que interesan al driver, o cuando las interrupciones son demasiado ruidosas para ser útiles.

**El reintento con back-off.** Un callout de disparo único programado con un retardo creciente tras cada intento fallido. El primer fallo programa un reintento de 10 ms; el segundo, uno de 20 ms; y así sucesivamente. Limita la frecuencia con la que el driver molesta al hardware tras un fallo.

**La rotación de estadísticas.** Un callout periódico que toma una instantánea de los contadores internos a intervalos regulares, calcula una tasa por intervalo y la almacena en un buffer circular para su posterior inspección.

**El recolector diferido.** Un callout de disparo único que completa un desmontaje tras un período de gracia. Se usa cuando un objeto no puede liberarse inmediatamente porque alguna otra ruta de código puede seguir manteniendo una referencia; el callout espera el tiempo suficiente para que esas referencias se agoten y luego libera el objeto.

Implementaremos los tres primeros (heartbeat, watchdog y fuente de tick diferida) en `myfirst` a lo largo de este capítulo. Los demás siguen la misma forma; una vez que conoces el patrón, las variaciones son mecánicas.

### ¿Por qué no usar simplemente un kernel thread?

Una pregunta razonable para un principiante: ¿por qué `callout(9)` es una API separada? ¿No podrían conseguirse los mismos efectos con un kernel thread que haga un bucle, duerma y actúe?

En principio, sí. En la práctica, ningún driver debería recurrir a un kernel thread cuando un callout es suficiente.

Un kernel thread es un recurso pesado. Tiene su propia pila (típicamente 16 KB en amd64), su propia entrada en el planificador, su propia prioridad, su propio estado. Arrancar uno para una acción periódica que tarda 10 microsegundos por segundo es un desperdicio: 16 KB de memoria más la sobrecarga del planificador, solo para despertar, hacer un trabajo trivial y volver a dormir. Multiplicado por muchos drivers, el kernel acaba con cientos de threads mayoritariamente inactivos.

Un callout es esencialmente gratuito en reposo. La estructura de datos es unos pocos punteros e integers (véase `struct callout` en `/usr/src/sys/sys/_callout.h`). No hay thread, ni pila, ni entrada en el planificador. La interrupción del reloj de hardware del kernel recorre una rueda de callouts y ejecuta cada callout que ha vencido, y luego regresa. Miles de callouts pendientes no cuestan prácticamente nada hasta que se disparan.

Un callout también se integra en las herramientas de observabilidad existentes del kernel. `dtrace`, `lockstat` y `procstat` entienden los callouts. Un kernel thread personalizado no dispone de nada de eso de forma gratuita; tendrías que instrumentarlo tú mismo.

La excepción, por supuesto, es cuando el trabajo que el timer necesita hacer es genuinamente largo y se beneficiaría de estar en un contexto de thread que pueda dormir. Una función callout no puede dormir; si tu trabajo requiere dormir, el cometido del callout es *encolar* el trabajo en una taskqueue o despertar un kernel thread que pueda hacerlo de forma segura. El Capítulo 16 cubre ese patrón. Para el Capítulo 13, el trabajo que realiza el callout es breve, no duerme y es consciente de los locks.

### ¿Por qué no usar `cv_timedwait` en un bucle?

Otra alternativa razonable: un kernel thread que haga un bucle sobre `cv_timedwait_sig` también produciría un comportamiento periódico. Lo mismo ocurriría con un helper en espacio de usuario que sondee un sysctl. ¿Por qué usar callout?

La respuesta del kernel thread es el argumento de los recursos de la subsección anterior: los callouts son mucho más baratos que los threads.

La respuesta del helper en espacio de usuario es la corrección: un driver cuya temporización depende de un proceso en espacio de usuario es un driver que falla cuando ese proceso se bloquea, se pagina o el planificador le niega CPU por otra carga de trabajo. Un driver debe ser autosuficiente para su propia corrección, aunque las herramientas en espacio de usuario proporcionen características adicionales.

Hay una situación en la que `cv_timedwait_sig` es la respuesta correcta: cuando el *propio thread que llama* necesita esperar. El sysctl `read_timeout_ms` del Capítulo 12 usa `cv_timedwait_sig` porque el lector es quien espera; tiene trabajo que hacer en cuanto llegan datos o se dispara el plazo. Un callout sería incorrecto porque el thread de la syscall del lector no puede ser el que ejecute el callback (el callback se ejecuta en un contexto diferente).

Usa `cv_timedwait_sig` cuando el thread de la syscall espera. Usa `callout(9)` cuando algo independiente de cualquier thread de syscall deba ocurrir en un momento específico. Ambos coexisten cómodamente en el mismo driver; el Capítulo 13 terminará con un driver que usa los dos.

### Una breve nota sobre el tiempo

El kernel expone el tiempo a través de varias unidades, cada una con sus propias convenciones. Las vimos en el Capítulo 12; un resumen ayuda antes de adentrarnos en la API.

- **Ticks de tipo `int`.** La unidad heredada. `hz` ticks equivalen a un segundo. El valor por defecto de `hz` en FreeBSD 14.3 es 1000, por lo que un tick equivale a un milisegundo. `callout_reset` toma su retardo en ticks.
- **`sbintime_t`.** Una representación en punto fijo binario con signo de 64 bits: los 32 bits superiores representan los segundos y los 32 bits inferiores una fracción de segundo. Las constantes de unidad están en `/usr/src/sys/sys/time.h`: `SBT_1S`, `SBT_1MS`, `SBT_1US`, `SBT_1NS`. `callout_reset_sbt` toma su retardo en sbintime.
- **`tick_sbt`.** Una variable global que contiene `1 / hz` como sbintime. Útil cuando tienes un recuento de ticks y quieres el sbintime equivalente: `tick_sbt * timo_in_ticks`.
- **El argumento de precisión.** `callout_reset_sbt` toma un argumento adicional de `precision`. Le indica al kernel cuánta variación es aceptable al programar, lo que permite al subsistema de callouts fusionar timers cercanos para mejorar la eficiencia energética. Una precisión de cero significa «dispara lo más cerca posible del plazo». Una precisión de `SBT_1MS` significa «cualquier momento dentro de un milisegundo del plazo es válido».

Para la mayor parte del trabajo con drivers, la API basada en ticks es el nivel de precisión adecuado. Usamos `callout_reset` (ticks) a lo largo de las primeras secciones del capítulo y recurrimos a `callout_reset_sbt` solo cuando la precisión por debajo del milisegundo importa o cuando queremos indicarle al kernel la variación aceptable.

### Cuándo un callout no es la herramienta adecuada

Por completitud, tres situaciones en las que `callout(9)` *no* es la respuesta correcta.

- **El trabajo necesita bloquearse.** Las funciones callout se ejecutan en un contexto que no puede bloquearse. Si el trabajo implica `uiomove`, `copyin`, `malloc(M_WAITOK)` o cualquier otra llamada potencialmente bloqueante, el callout debe encolar una tarea en un taskqueue o despertar un thread del kernel que pueda realizar el trabajo en contexto de proceso. Capítulo 16.
- **El trabajo necesita ejecutarse en una CPU específica por razones de caché.** `callout_reset_on` permite vincular un callout a una CPU específica, lo cual resulta útil, pero si el requisito es «ejecutarse en la misma CPU que envió la solicitud», la solución podría ser una primitiva por CPU. Tratamos `callout_reset_on` brevemente y aplazamos la discusión más profunda sobre afinidad de CPU.
- **El trabajo está orientado a eventos, no al tiempo.** Si el disparador es «han llegado datos» y no «han transcurrido 100 ms», lo que necesitas es un cv o un wakeup, no un callout. Mezclar ambos suele generar complejidad innecesaria.

### Un modelo mental: la rueda de callouts

Para concretar el argumento de coste de las subsecciones anteriores, aquí se describe lo que el kernel hace realmente para gestionar los callouts. No necesitas saber esto para usar `callout(9)` correctamente, pero conocerlo hace que varias secciones posteriores sean más fáciles de seguir.

El kernel mantiene una rueda de callouts por CPU. Conceptualmente, la rueda es un array circular de cubetas. Cada cubeta corresponde a un pequeño rango de tiempo. Cuando llamas a `callout_reset(co, ticks_in_future, fn, arg)`, el kernel calcula en qué cubeta cae «ahora más ticks_in_future» y añade el callout a la lista de esa cubeta. La aritmética es `(current_tick + ticks_in_future) modulo wheel_size`.

Una interrupción periódica del temporizador (el reloj de hardware) se dispara en cada tick. El manejador de la interrupción incrementa un contador global de ticks, examina la cubeta actual de la rueda de cada CPU y recorre la lista. Para cada callout que ha alcanzado su plazo, el kernel lo extrae de la rueda y bien ejecuta el callback directamente (para los callouts con `C_DIRECT_EXEC`) o bien lo pasa a un thread de procesamiento de callouts.

Tres propiedades de este mecanismo son relevantes para el capítulo.

En primer lugar, planificar un callout es económico: consiste esencialmente en «calcular un índice de cubeta y enlazar la estructura en una lista». Unas pocas operaciones atómicas. Sin asignación de memoria. Sin cambio de contexto.

En segundo lugar, un callout no planificado no tiene ningún coste: es simplemente un `struct callout` en algún lugar de tu softc. El kernel no sabe nada de él hasta que llamas a `callout_reset`. No hay ninguna sobrecarga por callout en reposo.

En tercer lugar, la granularidad de la rueda es de un tick. Un retraso de 1,7 ticks se redondea hacia arriba a 2 ticks. El argumento `precision` de `callout_reset_sbt` permite intercambiar exactitud por la libertad del kernel de agrupar disparos próximos, lo que es una optimización de ahorro de energía en sistemas con muchos temporizadores concurrentes. Para el desarrollo de drivers, la precisión por defecto es casi siempre suficiente.

En la implementación real hay mucho más: ruedas por CPU para la localidad de caché, migración diferida cuando un callout se reprograma a otra CPU, tratamiento especial para los callouts con `C_DIRECT_EXEC` que se ejecutan en la propia interrupción del temporizador, etcétera. La implementación está en `/usr/src/sys/kern/kern_timeout.c` si tienes curiosidad. Vale la pena leerla una vez; no es necesario memorizarla.

### Qué significa «ahora» en el kernel

Hay una confusión pequeña pero recurrente: el kernel dispone de varias bases de tiempo, y cada una mide cosas distintas.

`ticks` es una variable global que cuenta las interrupciones del reloj de hardware desde el arranque. Se incrementa en uno en cada tick del reloj. Es rápido de leer (una sola carga de memoria), desborda cada pocas semanas con un valor típico de `hz=1000`, y es la base de tiempo que usa `callout_reset`. Expresa siempre los plazos de los callouts como «ahora más N ticks», que es lo que hace `callout_reset(co, N, ...)`.

`time_uptime` y `time_second` son valores de tipo `time_t` que cuentan segundos desde el arranque o desde la época Unix (respectivamente). Menos precisas; útiles para marcas de tiempo en registros y tiempos transcurridos legibles por humanos.

`sbinuptime()` devuelve un `sbintime_t` que representa segundos y fracciones desde el arranque. Esta es la base de tiempo con la que trabaja `callout_reset_sbt`. No desborda (bueno, lo haría en algunos cientos de años).

`getmicrouptime()` y `getnanouptime()` son accesos aproximados pero rápidos al «ahora»; pueden tener un tick o dos de antigüedad. `microuptime()` y `nanouptime()` son precisas pero más costosas (leen directamente el temporizador de hardware).

Para un driver que realice trabajo típico con temporizadores, las dos que aparecen son `ticks` (para el trabajo con callouts basados en ticks) y `getsbinuptime()` (para el trabajo basado en sbintime). Las usamos en los laboratorios sin comentarios adicionales; si te preguntas de dónde vienen, aquí tienes la respuesta.

### Cerrando la sección 1

El tiempo aparece en el desarrollo de drivers bajo tres formas: disparo único, periódico y espera acotada. Las dos primeras son exactamente para lo que sirve `callout(9)`; la tercera es para lo que sirve `cv_timedwait_sig` del Capítulo 12. Los patrones del mundo real (heartbeat, watchdog, debounce, poll, reintento, rollover, reaper diferido) son todos instancias de callouts de disparo único o periódicos; reconocerlos te permite reutilizar la misma primitiva en muchas situaciones.

Bajo la API, el kernel mantiene una rueda de callouts por CPU que no tiene prácticamente ningún coste en reposo y muy poco al planificar. La granularidad es de un tick (un milisegundo en un sistema FreeBSD 14.3 típico). La implementación gestiona miles de callouts pendientes por CPU sin ningún problema.

La sección 2 presenta la API: la estructura del callout, las cuatro variantes de inicialización y el ciclo de vida que sigue todo callout.



## Sección 2: Introducción a la API `callout(9)` de FreeBSD

`callout(9)` es, como la mayoría de las primitivas de sincronización, una API pequeña sobre una implementación cuidadosa. La estructura de datos es breve, el ciclo de vida es regular (init, schedule, fire, stop o drain, destroy), y las reglas son lo suficientemente explícitas como para que puedas verificar tu uso leyendo el código fuente. Esta sección recorre la estructura, nombra las variantes de init y presenta las etapas del ciclo de vida para que el resto del capítulo disponga de un vocabulario reutilizable.

### La estructura callout

La estructura de datos se encuentra en `/usr/src/sys/sys/_callout.h`:

```c
struct callout {
        union {
                LIST_ENTRY(callout) le;
                SLIST_ENTRY(callout) sle;
                TAILQ_ENTRY(callout) tqe;
        } c_links;
        sbintime_t c_time;       /* ticks to the event */
        sbintime_t c_precision;  /* delta allowed wrt opt */
        void    *c_arg;          /* function argument */
        callout_func_t *c_func;  /* function to call */
        struct lock_object *c_lock;   /* lock to handle */
        short    c_flags;        /* User State */
        short    c_iflags;       /* Internal State */
        volatile int c_cpu;      /* CPU we're scheduled on */
};
```

Es una estructura por callout, incrustada en el softc o dondequiera que la necesites. Los campos que tocas directamente son: ninguno. Toda interacción se realiza a través de llamadas a la API. Los campos que puedes leer con fines de diagnóstico son `c_flags` (mediante `callout_active` / `callout_pending`) y `c_arg` (raramente útil desde fuera).

Los dos campos de flags merecen una descripción cada uno.

`c_iflags` es interno. El kernel establece y borra bits en él bajo el propio lock del subsistema de callouts. Los bits codifican si el callout está en una rueda o en una lista de procesamiento, si está pendiente y un puñado de estados internos de contabilidad. El código del driver usa `callout_pending(c)` para leerlo; nada más.

`c_flags` es externo. Se supone que el llamador (tu driver) gestiona dos bits en él: `CALLOUT_ACTIVE` y `CALLOUT_RETURNUNLOCKED`. El bit activo sirve para rastrear «he pedido que este callout se planifique y aún no lo he cancelado». El bit returnunlocked cambia el contrato de gestión del lock; llegaremos a eso en la Sección 5. El código del driver lee el bit activo mediante `callout_active(c)` y lo borra mediante `callout_deactivate(c)`.

El campo `c_lock` merece su propio párrafo. Cuando inicializas el callout con `callout_init_mtx`, `callout_init_rw` o `callout_init_rm`, el kernel registra el puntero al lock aquí. Más adelante, cuando el callout se dispara, el kernel adquiere ese lock antes de llamar a tu función de callback y lo libera tras el retorno del callback (a menos que hayas solicitado lo contrario de forma explícita). Esto significa que tu callback se ejecuta como si el llamador hubiera adquirido el lock por él. El callout con gestión de lock es casi siempre lo que quieres para el código de un driver; diremos más al respecto en la Sección 5.

### La firma de la función de callback

La función de callback de un callout tiene un único argumento: un `void *`. El kernel pasa lo que hayas registrado con `callout_reset` (o sus variantes). La función devuelve `void`. Su firma completa, extraída de `/usr/src/sys/sys/_callout.h`:

```c
typedef void callout_func_t(void *);
```

Por convenio, pasa un puntero a tu softc (o a cualquier estado por instancia que el callback necesite). La primera línea del callback convierte el puntero void de vuelta al puntero de la estructura:

```c
static void
myfirst_heartbeat(void *arg)
{
        struct myfirst_softc *sc = arg;
        /* ... do timer work ... */
}
```

El argumento se fija en el momento del registro y no cambia entre disparos. Si necesitas pasar contexto variable al callback, guárdalo en algún lugar al que el callback pueda acceder a través del softc.

### Las cuatro variantes de inicialización

`callout(9)` ofrece cuatro formas de inicializar un callout, distinguidas por el tipo de lock (si lo hay) que el kernel adquirirá por ti antes de que se ejecute el callback.

```c
void  callout_init(struct callout *c, int mpsafe);

#define callout_init_mtx(c, mtx, flags) \
    _callout_init_lock((c), &(mtx)->lock_object, (flags))
#define callout_init_rw(c, rw, flags) \
    _callout_init_lock((c), &(rw)->lock_object, (flags))
#define callout_init_rm(c, rm, flags) \
    _callout_init_lock((c), &(rm)->lock_object, (flags))
```

`callout_init(c, mpsafe)` es la variante heredada, sin conciencia de locks. El argumento `mpsafe` tiene ahora un nombre poco afortunado; en realidad significa «puede ejecutarse sin que se adquiera Giant por mí». Pasa `1` para cualquier código de driver moderno; pasa `0` solo si realmente quieres que el kernel adquiera Giant antes de tu callback (casi nunca, y solo en rutas de código muy antiguas). Los drivers nuevos no deberían usar esta variante. El capítulo la menciona por completitud, porque la encontrarás en código antiguo.

`callout_init_mtx(c, mtx, flags)` registra un sleep mutex (`MTX_DEF`) como el lock del callout. Antes de cada disparo, el kernel adquiere el mutex y lo libera tras el retorno del callback. Esta es la variante que usarás en casi todo el código de driver. Se combina de forma natural con el mutex `MTX_DEF` que ya tienes en la ruta de datos.

`callout_init_rw(c, rw, flags)` registra un lock de lectura/escritura `rw(9)`. El kernel adquiere el lock de escritura a menos que establezcas `CALLOUT_SHAREDLOCK`, en cuyo caso adquiere el lock de lectura. Menos habitual en el código de driver; útil cuando el callback necesita leer un estado predominantemente de lectura y varios callouts comparten el mismo lock.

`callout_init_rm(c, rm, flags)` registra un `rmlock(9)`. Especializado; se usa en drivers de red con rutas de lectura muy activas que no deben contender.

Para el driver `myfirst`, todo callout que añadamos usará `callout_init_mtx(&sc->some_co, &sc->mtx, 0)`. El kernel adquiere `sc->mtx` antes de que se ejecute el callback, el callback puede manipular el cbuf y otro estado protegido por mutex sin tomar el lock él mismo, y el kernel libera `sc->mtx` después. El patrón es limpio, las reglas son explícitas y `WITNESS` protestará si las infringes.

### El argumento flags

El argumento flags para `_callout_init_lock` es uno de dos valores para el código de driver:

- `0`: el lock del callout se adquiere antes del callback y se libera después. Este es el valor por defecto y la respuesta correcta casi siempre.
- `CALLOUT_RETURNUNLOCKED`: el lock del callout se adquiere antes del callback. El callback es responsable de liberarlo (o puede que ya lo haya liberado algo llamado por el callback). Esto es ocasionalmente útil cuando la última acción del callback es soltar el lock y hacer algo que el lock no puede cubrir.
- `CALLOUT_SHAREDLOCK`: solo válido para `callout_init_rw` y `callout_init_rm`. El lock se adquiere en modo compartido en lugar de exclusivo.

En el Capítulo 13 usamos `0` en todos los casos. `CALLOUT_RETURNUNLOCKED` se menciona en la Sección 5 por completitud; el capítulo no lo necesita.

### Las cinco etapas del ciclo de vida

Todo callout sigue el mismo ciclo de vida de cinco etapas. Conocer las etapas por su nombre hará que el resto del capítulo sea mucho más fácil de seguir.

**Etapa 1: inicializado.** El `struct callout` se ha inicializado con una de las variantes de init. Tiene una asociación de lock (o `mpsafe`). No se ha planificado. Nada se disparará hasta que se lo indiques.

**Etapa 2: pendiente.** Has llamado a `callout_reset` o a `callout_reset_sbt`. El kernel ha colocado el callout en su rueda interna y ha anotado el momento en que debe dispararse. `callout_pending(c)` devuelve true. El callback aún no se ha ejecutado. Puedes cancelarlo llamando a `callout_stop(c)`, que lo extrae de la rueda.

**Etapa 3: disparando.** El plazo ha llegado y el kernel está ejecutando ahora el callback. Si el callout tiene un lock registrado, el kernel lo ha adquirido. Tu función de callback está en ejecución. Durante esta etapa `callout_active(c)` es true y `callout_pending(c)` puede ser false (ha sido extraído de la rueda). El callback puede llamar a `callout_reset` para rearmarse (este es el patrón periódico).

**Etapa 4: completado.** El callback ha retornado. Si el callback se rearmó mediante `callout_reset`, el callout vuelve a la etapa 2. De lo contrario, ahora está inactivo: `callout_pending(c)` es false. Si el kernel adquirió un lock para el callback, lo ha liberado.

**Etapa 5: destruido.** La memoria subyacente del callout ya no es necesaria. No existe una función `callout_destroy`; en su lugar, debes asegurarte de que el callout no está pendiente ni disparándose, y luego liberar la estructura que lo contiene. La herramienta estándar para la tarea de «esperar a que el callout esté en reposo de forma segura» es `callout_drain`. La Sección 6 lo cubre en detalle.

El ciclo es: init una vez, alternar entre pendiente y (disparando + completado) cuantas veces sea necesario, drain, liberar.

### Un primer vistazo a la API

Todavía no hemos programado nada. Leamos las cuatro llamadas más importantes, con un resumen de una línea para cada una:

```c
int  callout_reset(struct callout *c, int to_ticks,
                   void (*fn)(void *), void *arg);
int  callout_reset_sbt(struct callout *c, sbintime_t sbt,
                   sbintime_t prec, void (*fn)(void *), void *arg, int flags);
int  callout_stop(struct callout *c);
int  callout_drain(struct callout *c);
```

`callout_reset` programa el callout. El primer argumento es el callout que se desea programar. El segundo es el retardo en ticks (multiplica los segundos por `hz` para convertir; en FreeBSD 14.3, `hz=1000` habitualmente, de modo que un tick equivale a un milisegundo). El tercero es la función de callback. El cuarto es el argumento que se pasará al callback. Devuelve un valor distinto de cero si el callout estaba pendiente y fue cancelado (es decir, la nueva programación reemplaza a la anterior).

`callout_reset_sbt` funciona igual, pero acepta el retardo como `sbintime_t` y admite una precisión y unos indicadores adicionales. Se usa para precisión por debajo del tick o cuando se desea indicar al kernel el margen de imprecisión aceptable. La mayoría de los drivers usan `callout_reset` y recurren a `_sbt` solo cuando es necesario.

`callout_stop` cancela un callout pendiente. Si el callout estaba pendiente, se elimina de la rueda y no llega a ejecutarse. Devuelve un valor distinto de cero si se canceló un callout pendiente. Si el callout no estaba pendiente (ya había disparado, o nunca se programó), la llamada es una operación sin efecto y devuelve cero. Algo crucial: `callout_stop` *no* espera a que un callback en vuelo termine. Si el callback se está ejecutando en ese momento en otra CPU, `callout_stop` retorna antes de que el callback retorne.

`callout_drain` es la variante segura para el desmontaje del driver. Cancela el callout si está pendiente *y* espera a que cualquier callback en ejecución retorne antes de retornar ella misma. Tras retornar `callout_drain`, el callout tiene garantizado estar inactivo y sin ejecutarse en ningún lugar. Esta es la función que debes llamar en el momento del detach. La sección 6 explica por qué esto importa.

### Lectura del código fuente

Si tienes diez minutos, abre `/usr/src/sys/sys/callout.h` y `/usr/src/sys/kern/kern_timeout.c` y échales un vistazo. Hay tres cosas que buscar:

La cabecera define la API pública en menos de 130 líneas. Todas las funciones que menciona el capítulo están declaradas allí. Los macros que envuelven `_callout_init_lock` son claramente visibles.

El archivo de implementación es largo (unas 1550 líneas en FreeBSD 14.3), pero los nombres de las funciones coinciden con la API. `callout_reset_sbt_on` es la función central de programación; todo lo demás es un envoltorio. `_callout_stop_safe` es la función unificada de parada con o sin drenaje; `callout_stop` y `callout_drain` son macros que la invocan con indicadores distintos. `callout_init` y `_callout_init_lock` se encuentran cerca del final del archivo.

El capítulo cita las funciones y tablas de FreeBSD por nombre, no por número de línea, porque los números de línea varían entre versiones mientras que los nombres de funciones y símbolos permanecen. Si necesitas los números de línea aproximados de `kern_timeout.c` en FreeBSD 14.3: `callout_reset_sbt_on` cerca de la línea 936, `_callout_stop_safe` cerca de la 1085, `callout_init` cerca de la 1347. Abre el archivo y salta al símbolo; la línea será la que indique tu editor.

Los KASSERT dispersos por el código fuente son las reglas expresadas en forma de código. Por ejemplo, la aserción en `_callout_init_lock` que dice "no puedes darme un lock que pueda dormir" hace cumplir la regla de que los callouts no pueden bloquearse en un lock que pudiera hacer sleep. Leer esas aserciones genera confianza en que la API garantiza lo que promete.

### Un recorrido completo por el ciclo de vida

Situar las etapas del ciclo de vida en una línea de tiempo las hace concretas. Imagina un callout de latido que se inicializa en el attach, se activa en t=0 y se desactiva a los t=2,5 segundos.

- **t=-1s (momento del attach)**: El driver llama a `callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)`. El callout está ahora en la etapa 1 (inicializado). `callout_pending(c)` devuelve false. El kernel conoce la asociación de lock del callout.
- **t=0s**: El usuario activa el latido escribiendo en el sysctl. El manejador adquiere `sc->mtx`, establece `interval_ms = 1000` y llama a `callout_reset(&sc->heartbeat_co, hz, myfirst_heartbeat, sc)`. El callout pasa a la etapa 2 (pendiente). `callout_pending(c)` devuelve true. El kernel lo ha colocado en un cubo de la rueda correspondiente a t+1 segundo.
- **t=1s**: Llega el plazo. El kernel extrae el callout de la rueda (`callout_pending(c)` pasa a false). El kernel adquiere `sc->mtx`. El kernel llama a `myfirst_heartbeat(sc)`. El callout está ahora en la etapa 3 (disparando). El callback se ejecuta, emite una línea de log y llama a `callout_reset` para rearmarse. El rearme coloca el callout de nuevo en un cubo de la rueda para t+2 segundos. `callout_pending(c)` vuelve a ser true. El callback retorna. El kernel libera `sc->mtx`. El callout está de nuevo en la etapa 2 (pendiente), esperando su próximo disparo.
- **t=2s**: La misma secuencia. El callback dispara, se rearma, y el callout queda pendiente para t+3 segundos.
- **t=2,5s**: El usuario desactiva el latido escribiendo en el sysctl. El manejador adquiere `sc->mtx`, establece `interval_ms = 0` y llama a `callout_stop(&sc->heartbeat_co)`. El kernel elimina el callout de la rueda. `callout_stop` devuelve 1 (canceló un callout pendiente). `callout_pending(c)` pasa a false. El callout vuelve ahora a la etapa 1 (inicializado pero inactivo).
- **t=∞ (más tarde, en el momento del detach)**: La ruta de detach llama a `callout_drain(&sc->heartbeat_co)`. El callout ya está inactivo; `callout_drain` retorna de inmediato. El driver puede ya liberar con seguridad el estado circundante.

Observa tres cosas sobre esta línea de tiempo.

El ciclo de pendiente → disparando → pendiente se repite indefinidamente mientras el callback se rearme. No existe un límite máximo de iteraciones.

Un `callout_stop` puede interceptar el ciclo pendiente-disparando-pendiente en cualquier momento. Si el callout está en la etapa 2 (pendiente), `callout_stop` lo cancela. Si el callout está en la etapa 3 (disparando) en otra CPU, `callout_stop` *no* lo cancela (el callback se ejecutará hasta el final); la siguiente iteración del ciclo no ocurrirá porque la condición de rearme del callback (`interval_ms > 0`) es ahora false.

La comprobación de `is_attached` en el callback (que introduciremos en la sección 4) proporciona un punto de intercepción similar durante el desmontaje. Si el callback dispara después de que el detach haya borrado `is_attached`, el callback sale sin rearmarse, y la siguiente iteración no tiene lugar.

Esta línea de tiempo es la forma completa del uso de `callout(9)` en código de driver. Las variaciones implican añadir un patrón de disparo único (sin rearme), un patrón de watchdog (cancelar en caso de éxito) o un patrón de debounce (programar solo si no está ya pendiente). Las etapas del ciclo de vida son las mismas.

### Una nota sobre «active» frente a «pending»

Dos conceptos relacionados que los principiantes a veces confunden.

`callout_pending(c)` lo activa el kernel cuando el callout está en la rueda esperando disparar. El kernel lo borra cuando el callout dispara (el callback está a punto de ejecutarse) o cuando `callout_stop` lo cancela.

`callout_active(c)` lo activa el kernel cuando `callout_reset` tiene éxito. Lo borra `callout_deactivate` (una función que llamas tú) o `callout_stop`. Algo crucial: el kernel *no* borra `callout_active` cuando el callback dispara. El bit active es un indicador que dice "programé este callout y no lo he cancelado activamente"; si el callback ha disparado desde entonces es una cuestión aparte.

Un callout puede estar en cualquiera de estos cuatro estados:

- No activo y no pendiente: nunca programado, o cancelado mediante `callout_stop`, o desactivado mediante `callout_deactivate` tras disparar.
- Activo y pendiente: programado, en la rueda, esperando disparar.
- Activo y no pendiente: programado, disparado (o a punto de disparar), y el callback aún no ha llamado a `callout_deactivate`.
- No activo y pendiente: poco frecuente, pero posible si el driver llama a `callout_deactivate` mientras el callout sigue programado. La mayoría de los drivers nunca alcanzan este estado porque llaman a `callout_deactivate` solo dentro del callback, una vez que el bit pending ya se ha borrado.

Para la mayoría de los drivers solo necesitas `callout_pending` (que se usa en patrones como el debounce). El indicador `active` importa más en código que quiere saber "¿programamos un callout, aunque ya haya disparado?". En el capítulo 13 usamos `pending` una vez y nunca usamos `active`.

### Cerrando la sección 2

Los callouts son estructuras pequeñas con una API pequeña y un ciclo de vida regular. Las cuatro variantes de inicialización eligen el tipo de lock que el kernel adquirirá por ti (o ninguno). Las cuatro funciones que más usarás son `callout_reset`, `callout_reset_sbt`, `callout_stop` y `callout_drain`. La sección 3 las pone en práctica, programando temporizadores de disparo único y periódicos, y mostrando cómo funciona realmente el patrón de rearme periódico.



## Sección 3: Programación de eventos de disparo único y periódicos

Un temporizador en `callout(9)` es siempre conceptualmente de disparo único. No existe ninguna función `callout_reset_periodic`. El comportamiento periódico se construye haciendo que el callback se rearme al final de cada disparo. Tanto el patrón de disparo único como el periódico usan la misma llamada de la API (`callout_reset`); la diferencia está en si el callback decide programar el siguiente disparo.

Esta sección recorre ambos patrones con ejemplos detallados que compilan y se ejecutan. Aún no los integraremos en `myfirst`; eso corresponde a la sección 4. Aquí nos centramos en los primitivos de temporización y en los patrones que usarás.

### El patrón de disparo único

El callout más simple posible: programar un callback para que dispare una vez, en el futuro.

```c
static void
my_oneshot(void *arg)
{
        device_printf((device_t)arg, "one-shot fired\n");
}

void
schedule_a_one_shot(device_t dev, struct callout *co)
{
        callout_reset(co, hz / 10, my_oneshot, dev);
}
```

`hz / 10` significa "100 milisegundos a partir de ahora" en un sistema con `hz=1000`. El callback recibe el puntero al dispositivo que registramos. Se ejecuta una vez, imprime y retorna. El callout queda inactivo. Para volver a ejecutarlo, habría que llamar a `callout_reset` de nuevo.

Hay tres cosas que observar. Primera, el argumento del callback es lo que pasamos a `callout_reset`, sin tipo, recuperado mediante un cast. Segunda, el callback emite una línea de log y retorna; no vuelve a programarse. Este es el patrón de disparo único. Tercera, usamos `hz / 10` en lugar de un valor fijo. Expresa siempre los retardos de callout en términos de `hz` para que el código sea portable entre sistemas con frecuencias de reloj distintas.

Si quisieras un retardo de 250 ms, escribirías `hz / 4` (o `hz * 250 / 1000` para mayor claridad). Para un retardo de 5 segundos, `hz * 5`. La aritmética es entera; para valores fraccionarios, multiplica antes de dividir para preservar la precisión.

### El patrón periódico

Para el comportamiento periódico, el callback se rearma al final:

```c
static void
my_periodic(void *arg)
{
        struct myfirst_softc *sc = arg;
        device_printf(sc->dev, "tick\n");
        callout_reset(&sc->heartbeat_co, hz, my_periodic, sc);
}

void
start_periodic(struct myfirst_softc *sc)
{
        callout_reset(&sc->heartbeat_co, hz, my_periodic, sc);
}
```

La primera llamada a `callout_reset` (en `start_periodic`) arma el callout para dentro de un segundo. Cuando dispara, `my_periodic` se ejecuta, emite una línea de log y se rearma para un segundo después del momento actual. Se produce el siguiente disparo, el ciclo continúa. Para detener el disparo periódico, llama a `callout_stop(&sc->heartbeat_co)` (o a `callout_drain` en el desmontaje). Una vez detenido el callout, `my_periodic` no volverá a disparar hasta que se llame de nuevo a `start_periodic`.

Hay tres sutilezas.

Primera, el rearme ocurre al *final* del callback. Si el trabajo del callback lleva mucho tiempo, el siguiente disparo se retrasa ese tiempo. El intervalo real entre disparos es aproximadamente `hz` ticks más el tiempo que tomó el callback. Para la mayoría de los casos de uso en drivers esto es suficiente. Si necesitas un período exacto, usa `callout_schedule` o `callout_reset_sbt` con un plazo absoluto calculado.

Segunda, el callback se invoca con el lock del callout ya adquirido (veremos por qué en la sección 5). Cuando el callback llama a `callout_reset` para rearmarse, el subsistema de callouts gestiona el rearme correctamente aunque se esté llamando desde dentro del propio disparo del callout. La contabilidad interna del kernel está diseñada precisamente para este patrón.

Tercera, si el driver se está desmontando en el mismo momento en que el callback se rearma, existe una condición de carrera: el rearme vuelve a colocar el callout en la rueda después de que se haya ejecutado el cancel/drain. La sección 6 explica cómo manejar esto. La respuesta breve es: en el momento del detach, activa un indicador de "cerrando" en el softc bajo el mutex, y luego llama a `callout_drain` sobre el callout. El callback comprueba el indicador al entrar y retorna sin rearmarse si lo encuentra activo. El drain espera a que el callback en vuelo retorne.

### `callout_schedule` para rearmarse sin repetir argumentos

Para los callouts periódicos, el callback se reprograma con la misma función y el mismo argumento en cada ocasión. `callout_reset` requiere que los pases de nuevo. `callout_schedule` es una alternativa más cómoda que reutiliza la función y el argumento del último `callout_reset`:

```c
int  callout_schedule(struct callout *c, int to_ticks);
```

Dentro del callback periódico:

```c
static void
my_periodic(void *arg)
{
        struct myfirst_softc *sc = arg;
        device_printf(sc->dev, "tick\n");
        callout_schedule(&sc->heartbeat_co, hz);
}
```

El kernel utiliza el puntero a función y el argumento que recuerda de la última llamada a `callout_reset`. Se escribe menos y el código resulta ligeramente más claro. Tanto `callout_reset` como `callout_schedule` sirven para el patrón periódico; elige el que prefieras.

### Precisión por debajo del tick con `callout_reset_sbt`

Cuando necesitas una precisión mayor que la de un tick, o quieres indicarle al kernel qué margen de error es aceptable, utiliza la variante sbintime:

```c
int  callout_reset_sbt(struct callout *c, sbintime_t sbt,
                       sbintime_t prec,
                       void (*fn)(void *), void *arg, int flags);
```

Ejemplo: programar un temporizador de 250 microsegundos:

```c
sbintime_t sbt = 250 * SBT_1US;
callout_reset_sbt(&sc->fast_co, sbt, SBT_1US,
    my_callback, sc, C_HARDCLOCK);
```

El argumento `prec` es la precisión que el llamador está dispuesto a aceptar. `SBT_1US` indica que «cualquier momento dentro de un microsegundo del plazo es aceptable»; el kernel puede agrupar este temporizador con otros que estén a menos de un microsegundo de distancia. `0` significa «disparar lo más cerca posible del plazo». Los flags incluyen `C_HARDCLOCK` (alinear con la interrupción del reloj del sistema, valor predeterminado en la mayoría de los casos), `C_DIRECT_EXEC` (ejecutar en el contexto de la interrupción del temporizador, útil solo con un spinlock), `C_ABSOLUTE` (interpretar `sbt` como tiempo absoluto en lugar de un retardo relativo) y `C_PRECALC` (de uso interno; no lo establezcas).

A lo largo del capítulo usamos `callout_reset` (basado en ticks) en casi todos los casos. `callout_reset_sbt` se menciona por completitud; la sección de laboratorio incluye un ejercicio que lo utiliza.

### Cancelación: `callout_stop`

Para cancelar un callout pendiente, llama a `callout_stop`:

```c
int  callout_stop(struct callout *c);
```

Si el callout está pendiente, el kernel lo elimina del wheel y devuelve 1. Si el callout no está pendiente (ya se ha disparado, nunca fue programado o fue cancelado), la llamada no hace nada y devuelve 0.

Importante: `callout_stop` *no* espera. Si el callback se está ejecutando en otra CPU en ese momento cuando se llama a `callout_stop`, la llamada retorna de inmediato. El callback continúa ejecutándose en la otra CPU y termina cuando termina. Si el callback se vuelve a armar, el callout volverá a estar en el wheel tras retornar `callout_stop`.

Esto significa que `callout_stop` es la herramienta adecuada para la operación normal (cancelar un callout pendiente porque la condición que lo motivó ya se ha resuelto), pero la herramienta *equivocada* para el desmontaje (donde debes esperar a que cualquier callback en vuelo termine antes de liberar el estado circundante). Para el desmontaje, usa `callout_drain`. La sección 6 cubre esta distinción en profundidad.

El patrón estándar en operación normal:

```c
/* Decided we don't need this watchdog any more */
if (callout_stop(&sc->watchdog_co)) {
        /* The callout was pending; we just cancelled it. */
        device_printf(sc->dev, "watchdog cancelled\n");
}
/* If callout_stop returned 0, the callout had already fired
   or was never scheduled; nothing to do. */
```

Un pequeño punto de precaución: entre el retorno de `callout_stop` con valor 1 y la ejecución de la siguiente instrucción, ningún otro thread puede volver a armar el callout porque mantenemos el lock que protege el estado circundante. Sin el lock, `callout_stop` seguiría cancelando correctamente, pero el significado del valor de retorno se volvería susceptible a condiciones de carrera.

### Cancelación: `callout_drain`

`callout_drain` es la variante segura para el desmontaje:

```c
int  callout_drain(struct callout *c);
```

Al igual que `callout_stop`, cancela un callout pendiente. *A diferencia de* `callout_stop`, si el callback se está ejecutando en otra CPU en ese momento, `callout_drain` espera a que retorne antes de retornar él mismo. Tras el retorno de `callout_drain`, se garantiza que el callout está inactivo: no está pendiente, no se está disparando y, si el callback no se ha vuelto a armar, no volverá a dispararse.

Dos reglas importantes.

Primera: el llamador de `callout_drain` *no debe* mantener el lock del callout. Si el callout se está ejecutando en ese momento (ha adquirido el lock y está ejecutando el callback), `callout_drain` necesita esperar a que el callback retorne, lo que significa que el callback necesita liberar el lock, lo que significa que el llamador de `callout_drain` no puede mantenerlo. Mantenerlo causaría un deadlock.

Segunda: `callout_drain` puede dormir. El thread espera en una cola de suspensión a que el callback termine. Por tanto, `callout_drain` solo es válido en contextos donde se permite dormir (contexto de proceso o kernel thread; no en contextos de interrupción ni de spinlock).

El patrón estándar de desmontaje:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* mark "going away" so a re-arming callback will not re-schedule */
        MYFIRST_LOCK(sc);
        sc->is_attached = 0;
        MYFIRST_UNLOCK(sc);

        /* drain the callout: cancel pending, wait for in-flight */
        callout_drain(&sc->heartbeat_co);
        callout_drain(&sc->watchdog_co);

        /* now safe to destroy other primitives and free state */
        /* ... */
}
```

La sección 6 desarrolla este patrón con detalle.

### `callout_pending` y `callout_active`

Dos accesores de diagnóstico útiles cuando quieres saber en qué estado se encuentra un callout:

```c
int  callout_pending(const struct callout *c);
int  callout_active(const struct callout *c);
void callout_deactivate(struct callout *c);
```

`callout_pending(c)` devuelve un valor distinto de cero si el callout está programado y esperando dispararse. Devuelve cero si el callout ya se ha disparado (o nunca fue programado, o fue cancelado).

`callout_active(c)` devuelve un valor distinto de cero si se ha llamado a `callout_reset` sobre este callout desde el último `callout_deactivate`. El bit «activo» es algo que gestionas *tú*. El kernel nunca lo establece ni lo borra por su cuenta (con una pequeña excepción: un `callout_stop` exitoso lo borra). La convención es que el callback borra el bit al inicio, el resto del driver lo establece al programar, y el código que quiere saber «¿tengo un callout pendiente o recién disparado?» puede consultar `callout_active`.

Para la mayor parte del trabajo con drivers no necesitarás ninguno de estos accesores. Los mencionamos porque el código fuente real de drivers los usa y deberías reconocer el patrón. El driver `myfirst` del capítulo 13 usa `callout_pending` una vez, en la ruta de cancelación del watchdog; el resto del capítulo no los necesita.

### Un ejemplo completo: una programación en dos etapas

Juntando todas las piezas: un pequeño ejemplo completo que programa un callback para que se dispare dentro de 100 ms, luego lo reprograma para 500 ms después, lo ejecuta una vez y se detiene.

```c
static int g_count = 0;
static struct callout g_co;
static struct mtx g_mtx;

static void
my_callback(void *arg)
{
        printf("callback fired (count=%d)\n", ++g_count);
        if (g_count == 1) {
                /* Reschedule for 500 ms later. */
                callout_reset(&g_co, hz / 2, my_callback, NULL);
        } else if (g_count == 2) {
                /* Done; do nothing, callout becomes idle. */
        }
}

void
start_test(void)
{
        mtx_init(&g_mtx, "test_co", NULL, MTX_DEF);
        callout_init_mtx(&g_co, &g_mtx, 0);
        callout_reset(&g_co, hz / 10, my_callback, NULL);
}

void
stop_test(void)
{
        callout_drain(&g_co);
        mtx_destroy(&g_mtx);
}
```

Diez líneas de contenido. El callback decide si volver a armarse en función del contador. Tras dos disparos, deja de rearmarse y el callout queda inactivo. `stop_test` vacía el callout (esperando si es necesario a cualquier disparo en vuelo) y luego destruye el mutex.

Este patrón, con variaciones, resume toda la forma de uso de `callout(9)` en el código de drivers. La sección 4 lo integra dentro de `myfirst` y le asigna trabajo real.

### Cerrando la sección 3

Los callouts se programan con `callout_reset` (basado en ticks) o `callout_reset_sbt` (basado en sbintime). El comportamiento de disparo único proviene de un callback que no se vuelve a armar; el comportamiento periódico proviene de un callback que se vuelve a armar al final. La cancelación es `callout_stop` para la operación normal y `callout_drain` para el desmontaje. Los accesores `callout_pending`, `callout_active` y `callout_deactivate` son para inspección de diagnóstico.

La sección 4 toma los patrones de esta sección e integra un callout real en el driver `myfirst`: un latido que registra periódicamente una línea de estadísticas.



## Sección 4: Integrar temporizadores en tu driver

La teoría es cómoda; la integración es donde aparecen los casos difíciles. Esta sección guía el proceso de añadir un callout de latido a `myfirst`. El latido se dispara una vez por segundo, registra una breve línea de estadísticas y se vuelve a armar. Veremos cómo el callout se integra con el mutex existente, cómo la inicialización consciente del lock elimina una clase de condición de carrera, cómo el flag `is_attached` del capítulo 12 protege el callback durante el desmontaje, y cómo `WITNESS` confirma que el diseño es correcto.

Considera esto como la Etapa 1 de la evolución del driver del capítulo. Al final de esta sección, el driver `myfirst` tendrá su primer temporizador.

### Añadir un callout de latido

Añade dos campos a `struct myfirst_softc`:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct callout          heartbeat_co;
        int                     heartbeat_interval_ms;  /* 0 = disabled */
        /* ... rest ... */
};
```

`heartbeat_co` es el propio callout. `heartbeat_interval_ms` es un parámetro ajustable por sysctl que permite al usuario activar, desactivar y ajustar el latido en tiempo de ejecución. Un valor de cero desactiva el latido. Un valor positivo es el intervalo en milisegundos.

Inicializa el callout en `myfirst_attach`. Coloca la llamada después de que el mutex esté inicializado y antes de que se cree el cdev (para que el callout esté listo para programarse, pero ningún usuario pueda aún desencadenar nada):

```c
static int
myfirst_attach(device_t dev)
{
        /* ... existing setup ... */

        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);
        cv_init(&sc->data_cv, "myfirst data");
        cv_init(&sc->room_cv, "myfirst room");
        sx_init(&sc->cfg_sx, "myfirst cfg");
        callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0);

        /* ... rest of attach ... */
}
```

`callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)` registra `sc->mtx` como el lock del callout. A partir de este punto, cada vez que el callout de latido se dispare, el kernel adquirirá `sc->mtx` antes de llamar a nuestro callback y lo liberará una vez que el callback retorne. Este es exactamente el contrato que queremos: el callback puede manipular libremente el estado de cbuf y los campos del softc sin tener que tomar el lock por sí mismo.

Vacía el callout en `myfirst_detach`, antes de destruir las primitivas:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* ... refuse detach while active_fhs > 0 ... */
        /* ... clear is_attached and broadcast cvs under sc->mtx ... */

        seldrain(&sc->rsel);
        seldrain(&sc->wsel);

        callout_drain(&sc->heartbeat_co);

        if (sc->cdev_alias != NULL) { destroy_dev(sc->cdev_alias); /* ... */ }
        /* ... rest of detach as before ... */
}
```

La llamada a `callout_drain` debe venir *después* de que `is_attached` se haya borrado y las cvs hayan sido notificadas (para que un callback que se dispare durante el vaciado vea el flag borrado), y *antes* de que se destruya cualquier primitiva que el callback pudiera tocar. Con `is_attached` borrado, el callback no puede reprogramarse; el vaciado espera a que cualquier callback en vuelo termine. Tras el retorno de `callout_drain`, ningún callback puede estar ejecutándose ni ninguno está pendiente; el resto del proceso de detach puede liberar el estado con seguridad.

### El callback de latido

Ahora el propio callback:

```c
static void
myfirst_heartbeat(void *arg)
{
        struct myfirst_softc *sc = arg;
        size_t used;
        uint64_t br, bw;
        int interval;

        MYFIRST_ASSERT(sc);

        if (!sc->is_attached)
                return;  /* device going away; do not re-arm */

        used = cbuf_used(&sc->cb);
        br = counter_u64_fetch(sc->bytes_read);
        bw = counter_u64_fetch(sc->bytes_written);
        device_printf(sc->dev,
            "heartbeat: cb_used=%zu, bytes_read=%ju, bytes_written=%ju\n",
            used, (uintmax_t)br, (uintmax_t)bw);

        interval = sc->heartbeat_interval_ms;
        if (interval > 0)
                callout_reset(&sc->heartbeat_co,
                    (interval * hz + 999) / 1000,
                    myfirst_heartbeat, sc);
}
```

Diez líneas que capturan el patrón completo del latido periódico. Vamos a analizarlas.

`MYFIRST_ASSERT(sc)` confirma que `sc->mtx` está retenido. El callout fue inicializado con `callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)`, de modo que el kernel adquirió `sc->mtx` antes de llamarnos; la aserción es una comprobación de cordura que detecta el caso en que alguien (quizás un mantenedor futuro) cambie accidentalmente la inicialización a `callout_init` sin prestar atención.

`if (!sc->is_attached) return;` es la guarda de desmontaje. Si la ruta de detach ha borrado `is_attached`, salimos de inmediato sin hacer ningún trabajo y sin rearmarnos. El vaciado en `myfirst_detach` verá el callout inactivo y terminará limpiamente.

Las lecturas de cbuf-used y del contador se realizan bajo el lock. Llamamos a `cbuf_used` (que espera que `sc->mtx` esté retenido) y a `counter_u64_fetch` (que es lockless y seguro en cualquier contexto). La llamada a `device_printf` es potencialmente costosa, pero es la forma convencional de generar líneas de registro; toleramos el coste porque ocurre como máximo una vez por segundo.

El rearme al final utiliza el valor actual de `heartbeat_interval_ms`. Si el usuario lo ha puesto a cero (ha desactivado el latido), no nos rearmamos, y el callout queda inactivo hasta que algo más lo programe. Si el usuario ha cambiado el intervalo, el próximo disparo usará el nuevo valor. Esta es una característica pequeña pero significativa: la frecuencia del latido es configurable dinámicamente sin necesidad de reiniciar el driver.

La aritmética `(interval * hz + 999) / 1000` convierte milisegundos a ticks, redondeando hacia arriba. La misma fórmula que las esperas acotadas del capítulo 12, por la misma razón: nunca redondear por debajo de la duración solicitada.

### Iniciar el latido desde un sysctl

El usuario activa el latido escribiendo un valor distinto de cero en `dev.myfirst.<unit>.heartbeat_interval_ms`. Necesitamos un manejador sysctl que programe el primer disparo:

```c
static int
myfirst_sysctl_heartbeat_interval_ms(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, old, error;

        old = sc->heartbeat_interval_ms;
        new = old;
        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);

        if (new < 0)
                return (EINVAL);

        MYFIRST_LOCK(sc);
        sc->heartbeat_interval_ms = new;
        if (new > 0 && old == 0) {
                /* Enabling: schedule the first firing. */
                callout_reset(&sc->heartbeat_co,
                    (new * hz + 999) / 1000,
                    myfirst_heartbeat, sc);
        } else if (new == 0 && old > 0) {
                /* Disabling: cancel any pending heartbeat. */
                callout_stop(&sc->heartbeat_co);
        }
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

El manejador:

1. Lee el valor actual (para que una consulta de solo lectura devuelva el intervalo actual).
2. Deja que `sysctl_handle_int` valide y actualice la variable local `new`.
3. Valida que el nuevo valor no sea negativo.
4. Adquiere `sc->mtx` para confirmar el cambio de forma atómica frente a cualquier actividad del callout en paralelo.
5. Si el latido estaba desactivado y ahora se activa, programa el primer disparo.
6. Si el latido estaba activado y ahora se desactiva, cancela el callout pendiente.
7. Libera el lock y retorna.

Observa el manejo simétrico. Si el usuario activa y desactiva el latido rápidamente, el manejador hace lo correcto en cada ocasión. Un rearme en el callback no dispararía un nuevo latido si el usuario lo desactiva (el callback comprueba `heartbeat_interval_ms` antes de rearmarse). Una programación desde el sysctl no generaría una doble programación (el callback se rearma solo si `interval_ms > 0`, y el sysctl solo programa si `old == 0`).

Un punto sutil: el callout se inicializa con `sc->mtx` como su lock, y el manejador sysctl adquiere `sc->mtx` antes de llamar a `callout_reset`. El kernel también adquiere `sc->mtx` para los callbacks. Esto significa que el manejador sysctl y cualquier callback en vuelo están serializados: el sysctl espera si un callback se está ejecutando en ese momento, y el callback no puede ejecutarse mientras el sysctl mantiene el lock. La condición de carrera «el usuario desactiva el latido justo cuando el callback se rearma» queda cerrada por el lock.

Registra el sysctl en attach:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
    OID_AUTO, "heartbeat_interval_ms",
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    sc, 0, myfirst_sysctl_heartbeat_interval_ms, "I",
    "Heartbeat interval in milliseconds (0 = disabled)");
```

E inicializa `heartbeat_interval_ms = 0` en attach para que el latido esté desactivado por defecto. El usuario lo activa configurando el sysctl; el driver permanece silencioso hasta entonces.

### Verificar la refactorización

Construye el nuevo driver y cárgalo en un kernel con `WITNESS`. Tres pruebas:

**Prueba 1: latido desactivado por defecto.**

```sh
$ kldload ./myfirst.ko
$ dmesg | tail -3   # attach line shown; no heartbeat logs
$ sleep 5
$ dmesg | tail -3   # still no heartbeat logs
```

Resultado esperado: la línea de attach, y luego silencio. El latido está desactivado por defecto.

**Prueba 2: latido activado.**

```sh
$ sysctl -w dev.myfirst.0.heartbeat_interval_ms=1000
$ sleep 5
$ dmesg | tail -10
```

Se esperan unas cinco líneas de latido, una por segundo:

```text
myfirst0: heartbeat: cb_used=0, bytes_read=0, bytes_written=0
myfirst0: heartbeat: cb_used=0, bytes_read=0, bytes_written=0
myfirst0: heartbeat: cb_used=0, bytes_read=0, bytes_written=0
```

**Prueba 3: latido bajo carga.**

En un terminal:

```sh
$ ../../part-02/ch10-handling-io-efficiently/userland/producer_consumer
```

Mientras se ejecuta, observa `dmesg` en otro terminal. Las líneas de latido deberían mostrar ahora contadores de bytes distintos de cero:

```text
myfirst0: heartbeat: cb_used=0, bytes_read=1048576, bytes_written=1048576
```

**Prueba 4: desactivación limpia.**

```sh
$ sysctl -w dev.myfirst.0.heartbeat_interval_ms=0
$ sleep 5
$ dmesg | tail -3   # nothing new
```

El latido se detiene; no se emiten más líneas.

**Prueba 5: detach con el latido activo.**

```sh
$ sysctl -w dev.myfirst.0.heartbeat_interval_ms=1000
$ kldunload myfirst
```

Resultado esperado: el detach se completa correctamente. El drenado en `myfirst_detach` cancela el callout pendiente y espera a que cualquier disparo en vuelo termine. No aparecen advertencias de `WITNESS` ni panics.

Si alguna de estas pruebas falla, la causa más probable es que (a) falta la comprobación de `is_attached` en el callback (de modo que el callback se rearma durante el desmontaje y `callout_drain` nunca retorna) o bien (b) la inicialización del lock es incorrecta (de modo que el callback se ejecuta sin el mutex esperado y `MYFIRST_ASSERT` dispara).

### Una nota sobre el coste del heartbeat

Un heartbeat de 1 segundo es prácticamente gratuito: un callout por segundo, tres lecturas de contador, una línea de log, un re-arm. CPU total: microsegundos por disparo. Memoria: cero más allá del `struct callout` que ya está en el softc.

Un heartbeat de 1 milisegundo es otra historia. Mil líneas de log por segundo saturarán el buffer de `dmesg` en cuestión de segundos y dominarán el uso de CPU del driver. Usa intervalos cortos solo cuando el trabajo sea genuinamente rápido y el log esté controlado por un nivel de depuración.

Para fines de demostración, `1000` (uno por segundo) es razonable. Para un heartbeat en producción, el rango razonable es probablemente de 100 ms a 10 s. El capítulo no impone un mínimo; la elección es del lector.

### Un modelo mental: cómo se desarrolla un heartbeat

Una imagen paso a paso de lo que hacen el kernel y el driver durante un único disparo del heartbeat. Útil para afianzar el vocabulario del ciclo de vida en términos concretos.

- **t=0**: El usuario ejecuta `sysctl -w dev.myfirst.0.heartbeat_interval_ms=1000`.
- **t=0+δ**: Se ejecuta el handler del sysctl. Lee el valor actual (0), valida el nuevo valor (1000) y adquiere `sc->mtx`. Dentro del lock: asigna `sc->heartbeat_interval_ms = 1000`. Al detectar la transición de 0 a un valor distinto de cero, llama a `callout_reset(&sc->heartbeat_co, hz, myfirst_heartbeat, sc)`. El kernel calcula el bucket de la rueda para «ahora más 1000 ticks» y enlaza el callout en ese bucket. El handler libera `sc->mtx` y devuelve 0.
- **t=0 a t=1s**: El kernel realiza otro trabajo. El callout espera en la rueda.
- **t=1s**: Se dispara la interrupción del reloj hardware. El subsistema de callouts recorre el bucket actual de la rueda y encuentra `sc->heartbeat_co` en espera. El kernel lo elimina de la rueda y lo despacha.
- **t=1s+δ**: Un thread de procesamiento de callouts se despierta (o, si el sistema está inactivo, la propia interrupción del temporizador ejecuta el callback). El kernel adquiere `sc->mtx` (esto puede bloquearse brevemente si otro thread lo tiene; para nuestra carga de trabajo habitual, el lock está libre). Una vez que `sc->mtx` está adquirido, el kernel llama a `myfirst_heartbeat(sc)`.
- **Dentro del callback**: `MYFIRST_ASSERT(sc)` confirma que el lock está adquirido. La comprobación de `is_attached` pasa. El callback lee `cbuf_used` (lock adquirido; seguro), lee los contadores por CPU (sin lock; siempre seguro) y emite una línea con `device_printf`. Comprueba `sc->heartbeat_interval_ms` (1000); como es positivo, llama a `callout_reset(&sc->heartbeat_co, hz, myfirst_heartbeat, sc)` para programar el siguiente disparo. El kernel vuelve a enlazar el callout en el bucket de la rueda para «ahora más 1000 ticks». El callback retorna.
- **t=1s+ε**: El kernel libera `sc->mtx`. El callout está de nuevo en la rueda, esperando hasta t=2s.
- **t=2s**: El ciclo se repite.

Tres observaciones.

Primera: el kernel gestiona el lock en tu nombre. Tu callback se ejecuta como si algún llamante invisible hubiera adquirido `sc->mtx` por él. No hay `mtx_lock`/`mtx_unlock` en el callback porque el kernel los gestiona.

Segunda: el re-arm es simplemente otra llamada a `callout_reset`. Está permitido porque el lock del callout está adquirido; la contabilidad interna del kernel gestiona el caso «este callout se está disparando en este momento y se está re-armando dentro de su propio callback».

Tercera: el tiempo entre disparos es aproximadamente `hz` ticks, pero algo mayor: el tiempo de trabajo del callback más cualquier latencia de planificación se suma al intervalo. Para un heartbeat de 1 segundo, la deriva son microsegundos; para uno de 1 milisegundo, podría ser medible. Si la precisión del periodo importa, usa `callout_reset_sbt` y calcula el siguiente plazo como «plazo anterior + intervalo», no «ahora + intervalo».

### Visualización del temporizador con dtrace

Una comprobación de cordura útil: confirmar que el heartbeat se dispara a la frecuencia que has configurado.

```sh
# dtrace -n 'fbt::myfirst_heartbeat:entry { @ = count(); } tick-1sec { printa(@); trunc(@); }'
```

Esta línea de dtrace cuenta cuántas veces se entra en `myfirst_heartbeat` cada segundo. Con `heartbeat_interval_ms=1000`, el recuento debe ser 1 por segundo. Con `heartbeat_interval_ms=100`, debe ser 10. Con `heartbeat_interval_ms=10`, debe ser 100.

Si el recuento se desvía mucho del valor esperado, la configuración no ha surtido efecto. Causas habituales: el handler del sysctl no ha confirmado el cambio (bug en el handler), el callback sale antes de tiempo porque `is_attached == 0` (bug en el flujo de desmontaje) o el sistema está tan cargado que el callout se dispara tarde y se produce una acumulación. En funcionamiento normal, el recuento debe ser estable con una diferencia de como máximo una unidad por segundo.

Una receta más elaborada de dtrace: histograma del tiempo empleado en el callback.

```sh
# dtrace -n '
fbt::myfirst_heartbeat:entry { self->ts = timestamp; }
fbt::myfirst_heartbeat:return /self->ts/ {
    @ = quantize(timestamp - self->ts);
    self->ts = 0;
}
tick-30sec { exit(0); }'
```

Cada callback tarda normalmente unos pocos microsegundos (el tiempo de leer contadores y emitir una línea de log). Si el histograma muestra callbacks que tardan milisegundos o más, algo va mal; investiga.

### Cerrando la sección 4

El driver ya tiene su primer temporizador. El callout del heartbeat se dispara periódicamente, registra una línea de estadísticas y se re-arma. La bandera `is_attached` (introducida en el capítulo 12 para los esperadores de cv) desempeña exactamente el mismo papel aquí: permite que el callback salga limpiamente cuando el dispositivo está siendo desmontado. La inicialización consciente del lock (`callout_init_mtx` con `sc->mtx`) significa que el callback se ejecuta con el mutex de la ruta de datos adquirido, y el kernel gestiona la adquisición del lock por nosotros.

La sección 5 examina el contrato del lock con más detalle. El contrato es la regla más importante de la API de callout; si se respeta, el resto resulta sencillo; si se viola, se generan bugs difíciles de encontrar.



## Sección 5: Gestión del locking y el contexto en los temporizadores

La sección 4 usaba `callout_init_mtx` y confiaba en que el kernel adquiriría `sc->mtx` antes de cada disparo. Esta sección abre esa caja negra. Examinamos exactamente qué hace el kernel con el puntero al lock que registraste, qué garantías puedes asumir dentro del callback y qué está permitido y qué no durante un disparo.

El contrato del lock es la regla más importante de `callout(9)`. Un driver que lo respeta es correcto por construcción. Un driver que lo viola produce condiciones de carrera difíciles de reproducir y aún más difíciles de diagnosticar. Dedica tiempo a esta sección ahora; el resto del capítulo resulta más fácil cuando el modelo está bien asentado.

### Lo que hace el kernel antes de que se ejecute tu callback

Cuando llega el plazo de un callout, el código de procesamiento de callouts del kernel (en `/usr/src/sys/kern/kern_timeout.c`) encuentra el callout en la rueda y se prepara para dispararlo. La preparación depende del lock que hayas registrado:

- **Sin lock (`callout_init` con `mpsafe=1`).** El kernel asigna `c_iflags`, marca el callout como ya no pendiente y llama directamente a tu función. Tu función debe gestionar todo su propio locking.

- **Un mutex (`callout_init_mtx` con un mutex `MTX_DEF`).** El kernel adquiere el mutex con `mtx_lock`. Si el mutex está en disputa, el thread de disparo se bloquea hasta que puede adquirirlo. Una vez adquirido el mutex, el kernel llama a tu función. Cuando tu función retorna, el kernel libera el mutex con `mtx_unlock` (a menos que hayas establecido `CALLOUT_RETURNUNLOCKED`).

- **Un rw lock (`callout_init_rw`).** Igual que el caso del mutex, pero con `rw_wlock` (o `rw_rlock` si has establecido `CALLOUT_SHAREDLOCK`).

- **Un rmlock (`callout_init_rm`).** Misma estructura con las primitivas de rmlock.

- **Giant (el valor por defecto de `callout_init` con `mpsafe=0`).** El kernel adquiere Giant. Evítalo en código nuevo.

El lock se adquiere en el contexto del thread de disparo. Desde el punto de vista de tu callback, el lock simplemente está adquirido: se aplican los mismos invariantes que si cualquier otro thread hubiera llamado a `mtx_lock` y luego hubiera llamado a tu función.

### Por qué adquiere el lock el kernel

Una pregunta natural: ¿por qué no adquiere el callback el lock por sí mismo? El modelo en el que es el kernel quien adquiere el lock tiene tres ventajas sutiles.

**Coopera correctamente con `callout_drain`.** Cuando `callout_drain` espera a que un callback en vuelo termine, debe saber si hay un callback en ejecución en ese momento. El subsistema de callouts del kernel rastrea exactamente eso, pero solo porque es el código que adquirió el lock e inició el callback. Si el callback adquiriera su propio lock, el subsistema no podría distinguir entre «el callback está bloqueado intentando adquirir el lock» y «el callback ha retornado», y sería imposible implementar un drain limpio sin exponer estado privado del kernel. El modelo de adquisición por el kernel mantiene al subsistema firmemente en control de la línea temporal de disparos.

**Impone la regla de clase de lock.** El kernel comprueba en el momento del registro que el lock que proporcionas no permita que el thread duerma más de lo que los callouts toleran. Un lock sx o lockmgr que permita dormir dejaría que el callback llamara a `cv_wait`, lo cual es ilegal en contexto de callout. La función de inicialización (`_callout_init_lock` en `kern_timeout.c`) tiene la aserción `KASSERT(lock == NULL || !(LOCK_CLASS(lock)->lc_flags & LC_SLEEPABLE), ...)` para detectar esto.

**Serializa el callback frente a `callout_reset` y `callout_stop`.** Cuando el callback se dispara, el lock está adquirido. Cuando llamas a `callout_reset` o `callout_stop` desde el código de tu driver, debes tener el mismo lock adquirido (el kernel lo comprueba). Por tanto, la cancelación/reprogramación y el disparo son mutuamente excluyentes: en cualquier momento, o bien el callback está disparándose (lock adquirido por la ruta de adquisición del kernel) o bien el código del driver está reaccionando a un cambio de estado (lock adquirido por el código del driver). Nunca se ejecutan de forma concurrente.

Esta tercera propiedad es la que hace que el handler del sysctl del heartbeat de la sección 4 esté libre de condiciones de carrera. El handler adquiere `sc->mtx`, decide cancelar o programar, y la cancelación/programación se completa de forma atómica frente a cualquier callback en vuelo. No se necesitan precauciones especiales; el lock hace el trabajo.

### Qué puedes hacer dentro de un callback

El callback se ejecuta con el lock registrado adquirido. El lock determina qué es legal.

Para un mutex `MTX_DEF` (nuestro caso), las reglas son las mismas que para cualquier otro código que tenga adquirido un sleep mutex:

- Puedes leer y escribir cualquier estado protegido por el mutex.
- Puedes llamar a los helpers `cbuf_*` y otras operaciones internas del mutex.
- Puedes llamar a `cv_signal` y `cv_broadcast` (la API de cv no requiere que el interlock se libere primero).
- Puedes llamar a `callout_reset`, `callout_stop` o `callout_pending` sobre el mismo callout (re-arm, cancelar o comprobar).
- Puedes llamar a `callout_reset` sobre un callout *diferente* si también tienes adquirido su lock (o si es mpsafe).

### Qué no puedes hacer dentro de un callback

Las mismas reglas: no dormir mientras se tiene el mutex adquirido.

- **No** puedes llamar a `cv_wait`, `cv_wait_sig`, `mtx_sleep` ni ninguna otra primitiva de espera directamente. (El mutex está adquirido; dormir con él adquirido sería una violación de sleep-with-mutex que `WITNESS` detecta.)
- **No** puedes llamar a `uiomove`, `copyin` ni `copyout` (cada una puede dormir).
- **No** puedes llamar a `malloc(..., M_WAITOK)`. Usa `M_NOWAIT` en su lugar, con un manejo adecuado del caso de fallo de asignación.
- **No** puedes llamar a `selwakeup` (adquiere sus propios locks que pueden producir violaciones de orden).
- **No** puedes llamar a ninguna función que pueda dormir.

El callback debe ser corto. Lo habitual es unos pocos microsegundos de trabajo. Si necesitas hacer algo de larga duración, el callback debe *encolar* el trabajo en un taskqueue o despertar un thread del kernel que pueda realizarlo en contexto de proceso. El capítulo 16 cubre el patrón de taskqueue.

### ¿Y si necesitas dormir?

La respuesta estándar: no duermas en el callback. Pospón el trabajo. Hay dos patrones habituales.

**Patrón 1: establecer una bandera, señalar un thread del kernel.** El callback establece una bandera en el softc y señala un cv. Un thread del kernel (creado con `kproc_create` o `kthread_add`, ambos temas para capítulos posteriores) está esperando en el cv; se despierta, realiza el trabajo de larga duración en contexto de proceso y vuelve a esperar. El callback es corto; el trabajo no tiene restricciones.

**Patrón 2: encolar una tarea en un taskqueue.** El callback llama a `taskqueue_enqueue` para diferir el trabajo a un thread worker del taskqueue. El worker se ejecuta en contexto de proceso y puede dormir. De nuevo, el callback es corto; el trabajo no tiene restricciones. El capítulo 16 introduce esto en profundidad.

En el capítulo 13 mantenemos todo el trabajo con temporizadores breve y compatible con los locks; aún no necesitamos diferir. El patrón se menciona para que sepas que esa opción existe.

### El flag `CALLOUT_RETURNUNLOCKED`

`CALLOUT_RETURNUNLOCKED` cambia el contrato de lock. Sin él, el kernel adquiere el lock antes de llamar al callback y lo libera cuando el callback retorna. Con él, el kernel adquiere el lock antes de llamar al callback y el *callback* es el responsable de liberarlo (o el callback puede llamar a algo que suelte el lock).

¿Para qué querrías esto? Por dos razones.

**El callback suelta el lock para hacer algo que no puede hacerse bajo él.** Por ejemplo, el callback termina su trabajo protegido, suelta el lock y luego encola una tarea en un taskqueue. Encolar la tarea no requiere el lock y podría incluso violar el orden de adquisición de locks si se mantuviera retenido. Usar `CALLOUT_RETURNUNLOCKED` te permite escribir la liberación en el lugar natural.

**El callback transfiere la propiedad del lock a otra función.** Si el callback llama a un helper que toma la propiedad del lock y es responsable de liberarlo, `CALLOUT_RETURNUNLOCKED` documenta la transferencia a `WITNESS` para que la comprobación de la aserción pase.

Sin `CALLOUT_RETURNUNLOCKED`, el kernel afirmará que el lock sigue en posesión del thread que disparó el callback cuando este retorna. El flag le indica a la aserción que el callback tiene permitido salir de la función con el lock liberado.

En el Capítulo 13 no necesitamos `CALLOUT_RETURNUNLOCKED`. Ninguno de nuestros callbacks adquiere locks adicionales, libera locks ni retorna con un estado de lock diferente al de entrada. El flag se menciona para que lo reconozcas en código fuente de drivers reales.

### El flag `CALLOUT_SHAREDLOCK`

`CALLOUT_SHAREDLOCK` solo es válido para `callout_init_rw` y `callout_init_rm`. Le indica al kernel que adquiera el lock en modo compartido (lectura) en lugar de exclusivo (escritura) antes de llamar al callback.

Se usa cuando el callback solo lee estado y hay muchos callouts que comparten el mismo lock. Con `CALLOUT_SHAREDLOCK`, varios callbacks pueden ejecutarse de manera concurrente siempre que ningún escritor retenga el lock.

En el Capítulo 13 usamos `callout_init_mtx` con `MTX_DEF`, donde el modo compartido no existe. El flag se menciona por completitud.

### El modo de "ejecución directa"

El kernel ofrece un modo "directo" en el que la función del callout se ejecuta en el propio contexto de la interrupción del temporizador, en lugar de diferirse a un thread. El flag es `C_DIRECT_EXEC`, que se pasa a `callout_reset_sbt`. Está documentado en `/usr/src/sys/sys/callout.h` y solo es válido para callouts cuyo lock es un spin mutex (o ningún lock en absoluto).

La ejecución directa es rápida (sin cambio de contexto, sin despertar ningún thread), pero las reglas son más estrictas que las del contexto normal de un callout: sin dormir (lo cual ya era cierto), sin adquirir sleep mutexes, sin llamar a funciones que pudieran hacerlo. La función se ejecuta en contexto de interrupción, con todas las restricciones que eso implica (Capítulo 14).

En el Capítulo 13 nunca usamos `C_DIRECT_EXEC`. Nuestros callouts no son lo suficientemente críticos en cuanto al tiempo para necesitarlo. Lo mencionamos porque lo verás en algunos drivers de hardware (especialmente en drivers de red con rutas RX de alto rendimiento).

### Un ejemplo práctico: el contrato de lock en el heartbeat

Recuerda el callback del heartbeat de la Sección 4:

```c
static void
myfirst_heartbeat(void *arg)
{
        struct myfirst_softc *sc = arg;
        size_t used;
        uint64_t br, bw;
        int interval;

        MYFIRST_ASSERT(sc);

        if (!sc->is_attached)
                return;

        used = cbuf_used(&sc->cb);
        br = counter_u64_fetch(sc->bytes_read);
        bw = counter_u64_fetch(sc->bytes_written);
        device_printf(sc->dev,
            "heartbeat: cb_used=%zu, bytes_read=%ju, bytes_written=%ju\n",
            used, (uintmax_t)br, (uintmax_t)bw);

        interval = sc->heartbeat_interval_ms;
        if (interval > 0)
                callout_reset(&sc->heartbeat_co,
                    (interval * hz + 999) / 1000,
                    myfirst_heartbeat, sc);
}
```

Veamos el contrato de lock paso a paso:

- El callout se inicializó con `callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)`. El kernel mantiene `sc->mtx` antes de llamarnos.
- `MYFIRST_ASSERT(sc)` confirma que `sc->mtx` está retenido. Es una comprobación de sanidad.
- `sc->is_attached` se lee bajo el lock. Correcto.
- Se llama a `cbuf_used(&sc->cb)`. El helper cbuf espera que `sc->mtx` esté retenido; lo tenemos.
- Se llama a `counter_u64_fetch(sc->bytes_read)`. `counter(9)` no necesita lock y es seguro en cualquier contexto.
- Se llama a `device_printf`. `device_printf` no adquiere ninguno de nuestros locks; es seguro bajo nuestro mutex.
- `sc->heartbeat_interval_ms` se lee bajo el lock. Correcto.
- Se llama a `callout_reset` para reprogramar el callout. La API de callout exige que el lock del callout esté retenido al llamar a `callout_reset`; lo tenemos.

Cada operación del callback respeta el contrato de lock. El kernel liberará `sc->mtx` cuando el callback retorne.

Una comprobación específica: el callback *no* llama a nada que pueda dormir. `device_printf` no duerme. `cbuf_used` no duerme. `counter_u64_fetch` no duerme. `callout_reset` no duerme. El callback respeta la convención de no dormir del mutex.

Si añadiéramos accidentalmente un sleep, `WITNESS` lo detectaría en un kernel de depuración con un mensaje del tipo "sleeping thread (pid X) owns a non-sleepable lock". La lección: confía en que el kernel hace cumplir las reglas; simplemente mantén el callback breve.

### Qué ocurre cuando varios callouts comparten un lock

Un único lock puede ser el interlock de muchos callouts. Considera el siguiente ejemplo:

```c
callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0);
callout_init_mtx(&sc->watchdog_co, &sc->mtx, 0);
callout_init_mtx(&sc->tick_source_co, &sc->mtx, 0);
```

Tres callouts, todos usando `sc->mtx`. Cuando cualquiera de ellos se dispara, el kernel adquiere `sc->mtx` y ejecuta el callback. Mientras ese callback se está ejecutando, el lock está retenido; ningún otro callback (ni ningún otro thread que intente adquirir `sc->mtx`) puede avanzar.

Este es el patrón correcto: el mutex de la ruta de datos protege todo el estado por softc, y cualquier callout que necesite leer o modificar ese estado comparte el mismo lock. La serialización es automática y gratuita.

La desventaja: si el callback del heartbeat es lento, retrasa el callback del watchdog. Mantén los callbacks breves.

### Qué ocurre si el callback está en ejecución cuando llamas a `callout_reset`

Una pregunta sutil pero importante: ¿qué sucede si el callback se está ejecutando en una CPU mientras tú llamas a `callout_reset` en otra CPU para reprogramarlo?

El kernel maneja este caso correctamente. Veámoslo paso a paso.

El callback se está ejecutando en la CPU 0. Mantiene `sc->mtx` (el kernel lo adquirió antes de llamarlo). En la CPU 1, llamas a `callout_reset(&sc->heartbeat_co, hz, fn, arg)` (quizás porque el usuario cambió el intervalo). La API de callout exige que el llamador mantenga el mismo lock que usa el callout; tú lo tienes, en la CPU 1.

Pero la CPU 0 ya está dentro del callback, manteniendo `sc->mtx`. Por lo tanto, la CPU 1 no puede haberlo adquirido en ese momento. O bien la CPU 1 adquirió el lock mucho antes de que la CPU 0 lo obtuviera (en cuyo caso la CPU 0 está actualmente bloqueada esperando el lock y no está dentro del callback), o bien la CPU 1 está a punto de adquirir el lock y la CPU 0 está a punto de liberarlo.

El kernel gestiona este caso correctamente mediante el mismo mecanismo que usa para la sincronización ordinaria de `mtx_lock`. En cualquier instante dado, solo hay un titular de `sc->mtx`. Si la CPU 0 está ejecutando el callback, el `callout_reset` de la CPU 1 está bloqueado esperando el lock. Cuando el callback de la CPU 0 termina y el kernel libera el lock, la CPU 1 adquiere el lock y procede con la reprogramación. El callout queda programado para el nuevo plazo.

Si el callback se reprogramó a sí mismo antes de que la CPU 0 liberara el lock (el patrón periódico), el callout está actualmente pendiente. El `callout_reset` de la CPU 1 cancela el pendiente y lo sustituye con la nueva programación. El valor de retorno es 1 (cancelado).

Si el callback no se reprogramó (un disparo único, o el intervalo era 0), el callout está inactivo. El `callout_reset` de la CPU 1 lo programa. El valor de retorno es 0 (no se canceló ninguna programación anterior).

En cualquier caso, el resultado es correcto: tras retornar `callout_reset`, el callout está programado para el nuevo plazo, con la nueva función y el nuevo argumento.

### Qué ocurre si el callback está en ejecución cuando llamas a `callout_stop`

Una pregunta similar: el callback se está ejecutando en la CPU 0, y un llamador en la CPU 1 quiere cancelarlo.

La CPU 1 llama a `callout_stop`. Necesita mantener el lock del callout; lo tiene. La CPU 0 está ejecutando el callback mientras mantiene el mismo lock; la adquisición del lock por parte de la CPU 1 queda bloqueada. Cuando el callback de la CPU 0 retorna y libera el lock, la CPU 1 lo adquiere.

En ese punto, el callback puede haberse reprogramado (si era periódico). `callout_stop` cancela la programación pendiente. El valor de retorno es 1.

Si el callback no se reprogramó, el callout está inactivo. `callout_stop` es una operación nula. El valor de retorno es 0.

Tras retornar `callout_stop`, el callout no volverá a dispararse a menos que algo lo programe de nuevo. Es importante señalar que el callback que se estaba ejecutando en la CPU 0 *ya ha terminado* cuando retorna `callout_stop`; el lock se mantuvo durante toda su duración. Por tanto, `callout_stop` efectivamente espera a que termine el callback en vuelo, pero solo gracias a la espera de adquisición del lock, no por ningún mecanismo explícito de espera en el subsistema de callout.

Por eso `callout_stop` es seguro de usar en la operación normal del driver cuando se mantiene el lock, y por eso `callout_drain` solo se necesita cuando estás a punto de liberar el estado circundante (donde no puedes mantener el lock durante la espera).

### `callout_stop` desde un contexto sin el lock

¿Qué ocurre si llamas a `callout_stop` sin mantener el lock del callout? La función interna `_callout_stop_safe` del kernel detectará el lock ausente y emitirá una aserción (en kernels con `INVARIANTS`). En un kernel sin `INVARIANTS`, la llamada puede producir resultados incorrectos o condiciones de carrera.

La regla: cuando llames a `callout_stop` o `callout_reset`, debes mantener el mismo lock con el que se inicializó el callout. El kernel lo hace cumplir; una violación produce una advertencia de `WITNESS` o un panic con `INVARIANTS`.

En el Capítulo 13 siempre mantenemos `sc->mtx` cuando llamamos a `callout_reset` o `callout_stop` desde los manejadores de sysctl. La ruta de detach es la excepción: suelta el lock antes de llamar a `callout_drain`. `callout_drain` no requiere que el lock esté retenido; de hecho, requiere que *no* lo esté.

### Un patrón: la reactivación condicional

Un patrón útil para callouts periódicos: reactivar solo si se cumple cierta condición. En nuestro heartbeat:

```c
interval = sc->heartbeat_interval_ms;
if (interval > 0)
        callout_reset(&sc->heartbeat_co, ..., myfirst_heartbeat, sc);
```

La reactivación condicional le da al usuario un control preciso sobre los disparos periódicos. Un usuario que establezca `interval_ms = 0` desactiva el heartbeat en el siguiente disparo. El callback sale sin reactivar el callout; el callout pasa a estar inactivo.

Una versión más elaborada: reactivar con intervalos variables según la actividad. Un heartbeat que dispara con más frecuencia cuando el buffer está ocupado y con menos frecuencia cuando está inactivo:

```c
if (cbuf_used(&sc->cb) > 0)
        interval = sc->heartbeat_busy_interval_ms;  /* short */
else
        interval = sc->heartbeat_idle_interval_ms;  /* long */

if (interval > 0)
        callout_reset(&sc->heartbeat_co, ..., myfirst_heartbeat, sc);
```

El intervalo variable permite que el heartbeat muestree el dispositivo de forma adaptativa. Cuando la actividad es alta, dispara con frecuencia (detectando cambios de estado rápidamente); cuando la actividad es baja, dispara raramente (ahorrando CPU y espacio en el registro).

### Cerrando la Sección 5

El contrato de lock es el corazón de `callout(9)`. El kernel adquiere el lock registrado antes de cada disparo, ejecuta tu callback y lo libera al terminar. Esto serializa el callback frente a otros titulares del lock y elimina una clase de condición de carrera que de otro modo requeriría un manejo explícito. Las reglas dentro del callback son las mismas que las reglas normales del lock: para un mutex `MTX_DEF`, sin dormir, sin `uiomove`, sin `malloc(M_WAITOK)`. El callback debe ser breve; si necesita hacer trabajo prolongado, delégalo a un taskqueue (Capítulo 16) o a un kernel thread.

La reprogramación y la cancelación funcionan correctamente incluso cuando el callback se está ejecutando en otra CPU; el mecanismo de adquisición del lock garantiza la atomicidad. El patrón de reactivación condicional (reactivar solo si se cumple cierta condición) es la forma natural de proporcionar a un callout periódico una ruta de desactivación elegante.

La Sección 6 aborda el corolario de todo esto: en el momento de la descarga, no puedes liberar el estado circundante mientras haya un callback en ejecución o pendiente. `callout_drain` es la herramienta, y la condición de carrera al descargar es el problema que resuelve.



## Sección 6: Limpieza de temporizadores y gestión de recursos

Todo callout tiene un problema de destrucción. Entre el momento en que decides eliminar el driver y el momento en que la memoria circundante se libera, debes asegurarte de que ningún callback esté en ejecución y ningún callback esté programado para ejecutarse. Si un callback se dispara después de que la memoria se haya liberado, el kernel entra en pánico. Si un callback está en ejecución cuando liberas la memoria, el kernel entra en pánico. El pánico es fiable, inmediato y fatal; es el tipo de error que cuelga la máquina de pruebas y es difícil de depurar porque el backtrace apunta a código que ya ha sido liberado.

`callout(9)` proporciona las herramientas para resolver esto de forma limpia: `callout_stop` para la cancelación normal, `callout_drain` para el desmontaje, y `callout_async_drain` para los casos excepcionales en que se quiere programar la limpieza sin bloquearse. Esta sección recorre cada una de ellas, nombra con precisión la condición de carrera al descargar y presenta el patrón estándar para un detach seguro del driver.

### La condición de carrera al descargar

Imagina el driver en la Fase 1 del Capítulo 13 (heartbeat activado, `kldunload` llamado). Sin `callout_drain`, la secuencia podría ser la siguiente:

1. El usuario ejecuta `kldunload myfirst`.
2. El kernel llama a `myfirst_detach`.
3. `myfirst_detach` borra `is_attached`, hace broadcast de los cvs, libera el mutex y llama a `mtx_destroy(&sc->mtx)`.
4. El módulo del driver se descarga; la memoria que contiene `sc->mtx`, `sc->heartbeat_co` y el código de `myfirst_heartbeat` se libera.
5. La interrupción del reloj de hardware se dispara, el subsistema de callout recorre la rueda, encuentra `sc->heartbeat_co` (aún en la rueda porque nunca lo cancelamos) y llama a `myfirst_heartbeat` con `sc` como argumento.
6. `myfirst_heartbeat` ya no está en memoria. El kernel salta a una dirección ahora inválida. El sistema entra en pánico.

La condición de carrera no es teórica. Aunque el paso 5 ocurra microsegundos después del paso 4, el kernel entra igualmente en pánico. La ventana temporal es pequeña, pero no es nula.

La solución consiste en garantizar que, llegado el paso 4, ningún callout esté pendiente y ningún callback esté en ejecución. Dos acciones:

- **Cancelar los callouts pendientes.** Si el callout está en la rueda, hay que eliminarlo. `callout_stop` hace esto.
- **Esperar a los callbacks en ejecución.** Si un callback se está ejecutando en este momento en otra CPU, hay que esperar a que termine. `callout_drain` hace esto.

`callout_drain` hace ambas cosas: cancela los pendientes y espera a los que están en ejecución. Es lo que debes llamar en el momento del detach.

### `callout_stop` vs `callout_drain`

La distinción está en si la llamada espera o no.

`callout_stop`: cancela los pendientes y retorna inmediatamente. No espera a un callback que esté en vuelo. Devuelve 1 si el callout estaba pendiente y se canceló; 0 en caso contrario.

`callout_drain`: cancela los pendientes *y* espera a que cualquier callback en vuelo retorne antes de retornar él mismo. Devuelve 1 si el callout estaba pendiente y se canceló; 0 en caso contrario. Cuando `callout_drain` retorna, se garantiza que el callout está inactivo.

Usa `callout_stop` en la operación normal del driver cuando quieras cancelar el temporizador porque la condición que lo motivó ya se ha resuelto. El caso de uso del watchdog: programa un watchdog al inicio de una operación; cancélalo (con `callout_stop`) cuando la operación se complete con éxito. Si el watchdog ya está disparándose en otra CPU, `callout_stop` retorna y el watchdog se ejecutará hasta su finalización; eso está bien porque el manejador del watchdog verá que la operación se ha completado y no hará nada (o tomará alguna acción de recuperación que ya no es necesaria pero es inofensiva).

Usa `callout_drain` en el momento del detach, donde esperar es necesario para evitar la condición de carrera del unload. No uses `callout_stop` en el momento del detach; el callback podría estar ejecutándose en otra CPU y la memoria circundante podría liberarse antes de que retorne.

### Dos reglas fundamentales para `callout_drain`

`callout_drain` tiene dos reglas que es fácil violar.

**Regla 1: no mantengas el lock del callout al llamar a `callout_drain`.** Si el callout se está ejecutando en ese momento, el callback está sosteniendo el lock (el kernel lo adquirió para el callback). `callout_drain` espera a que el callback retorne; el callback retorna cuando su trabajo termina; ese trabajo incluye la liberación del lock. Si quien llama a `callout_drain` también está sosteniendo el lock, quedaría bloqueado esperando liberarse a sí mismo. Deadlock.

**Regla 2: `callout_drain` puede dormir.** Espera en una sleep queue a que el callback en vuelo termine. Por tanto, `callout_drain` solo es válido en contextos donde está permitido dormir: contexto de proceso (la ruta típica de detach) o contexto de kernel-thread. No en contexto de interrupción. No mientras se sostiene un spinlock. No mientras se sostiene ningún otro lock que no permita dormir.

Estas reglas juntas implican que la ruta estándar de detach libera `sc->mtx` (y cualquier otro lock que no permita dormir) antes de llamar a `callout_drain`. El patrón de detach del capítulo sigue este esquema:

```c
MYFIRST_LOCK(sc);
sc->is_attached = 0;
cv_broadcast(&sc->data_cv);
cv_broadcast(&sc->room_cv);
MYFIRST_UNLOCK(sc);    /* drop the mutex before draining */

seldrain(&sc->rsel);
seldrain(&sc->wsel);

callout_drain(&sc->heartbeat_co);   /* now safe to call */
```

El mutex se libera después de limpiar `is_attached`. `callout_drain` se ejecuta sin sostener el mutex; está libre de esperar en una sleep queue. Cualquier callback que se dispare durante el drain ve `is_attached == 0` y sale sin volver a armarse. Tras el drain, el callout queda inactivo.

### El patrón `is_attached`, revisitado

En el capítulo 12 usamos `is_attached` como señal para los threads que esperan en un cv: «el dispositivo va a desaparecer; devuelve ENXIO». En el capítulo 13 lo usamos con el mismo propósito para los callouts: «el dispositivo va a desaparecer; no te vuelvas a armar».

El patrón es idéntico:

```c
static void
myfirst_some_callback(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);

        if (!sc->is_attached)
                return;  /* device going away; do not re-arm */

        /* ... do the work ... */

        /* re-arm if periodic */
        if (some_condition)
                callout_reset(&sc->some_co, ticks, myfirst_some_callback, sc);
}
```

La comprobación está al inicio, antes de realizar cualquier trabajo. Si `is_attached == 0`, el callback sale inmediatamente sin hacer trabajo y sin volver a armarse. El drain en detach verá el callout inactivo (sin disparo pendiente) y se completará de forma limpia.

Un punto sutil: la comprobación ocurre *bajo el lock* (el kernel lo adquirió por nosotros). La ruta de detach limpia `is_attached` *bajo el lock*. Así, el callback siempre ve el valor actual de `is_attached`; no hay condición de carrera. Esta es la misma propiedad en la que nos basamos en el capítulo 12 para los threads que esperan en cvs.

### ¿Por qué no usar `callout_stop` en su lugar?

Es una pregunta natural: en lugar de `callout_drain`, ¿por qué no usar `callout_stop` seguido de alguna espera manual?

La implementación de `callout_drain` (en `_callout_stop_safe` en `/usr/src/sys/kern/kern_timeout.c`) hace exactamente eso, pero dentro del kernel, donde puede usar sleep queues internas sin exponerlas. Intentar hacer lo mismo en código de driver es frágil: necesitarías saber si el callback se está ejecutando en ese momento, algo que no puedes determinar desde fuera sin inspeccionar campos privados del kernel.

Simplemente llama a `callout_drain`. Para eso existe la API.

### `callout_async_drain`

Para el caso poco común en que quieras hacer el drain sin bloquear, el kernel ofrece `callout_async_drain`:

```c
#define callout_async_drain(c, d) _callout_stop_safe(c, 0, d)
```

Cancela los pendientes y organiza para que se llame a un callback de «drain completado» (el puntero a función `d`) cuando el callback en vuelo termine. El que llama no se bloquea; el control retorna inmediatamente. Es útil en contextos donde no puedes dormir pero necesitas saber cuándo ha terminado el drain.

Para los propósitos de este capítulo, `callout_async_drain` es excesivo. Hacemos el detach en contexto de proceso, donde bloquear está bien. Lo mencionamos porque lo verás en el código fuente de algunos drivers reales.

### El patrón estándar de detach con temporizadores

Juntando todo, el patrón canónico de detach para un driver con uno o más callouts:

> **Cómo leer este ejemplo.** El listado siguiente es una vista compuesta de la secuencia canónica de desmontaje de `callout(9)`, destilada de drivers reales como `/usr/src/sys/dev/re/if_re.c` (donde `callout_drain(&sc->rl_stat_callout)` se ejecuta en el momento del detach) y `/usr/src/sys/dev/watchdog/watchdog.c` (donde dos callouts se drenan en secuencia). Hemos mantenido el orden de las fases, las llamadas obligatorias a `callout_drain()` y la disciplina de lock intactos; un driver de producción añade contabilidad por dispositivo que la función real de detach intercala con cada paso. Cada símbolo que el listado nombra, desde `callout_drain` hasta `seldrain` y `mtx_destroy`, es una API real de FreeBSD; los campos de `myfirst_softc` corresponden al driver que está evolucionando a lo largo de este capítulo.

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* 1. Refuse detach if the device is in use. */
        MYFIRST_LOCK(sc);
        if (sc->active_fhs > 0) {
                MYFIRST_UNLOCK(sc);
                return (EBUSY);
        }

        /* 2. Mark the device as going away. */
        sc->is_attached = 0;
        cv_broadcast(&sc->data_cv);
        cv_broadcast(&sc->room_cv);
        MYFIRST_UNLOCK(sc);

        /* 3. Drain the selinfo readiness machinery. */
        seldrain(&sc->rsel);
        seldrain(&sc->wsel);

        /* 4. Drain every callout. Each takes its own line. */
        callout_drain(&sc->heartbeat_co);
        callout_drain(&sc->watchdog_co);
        callout_drain(&sc->tick_source_co);

        /* 5. Destroy cdevs (no new opens after this). */
        if (sc->cdev_alias != NULL) {
                destroy_dev(sc->cdev_alias);
                sc->cdev_alias = NULL;
        }
        if (sc->cdev != NULL) {
                destroy_dev(sc->cdev);
                sc->cdev = NULL;
        }

        /* 6. Free other resources. */
        sysctl_ctx_free(&sc->sysctl_ctx);
        cbuf_destroy(&sc->cb);
        counter_u64_free(sc->bytes_read);
        counter_u64_free(sc->bytes_written);

        /* 7. Destroy primitives in reverse acquisition order:
         *    cvs first, then sx, then mutex. */
        cv_destroy(&sc->data_cv);
        cv_destroy(&sc->room_cv);
        sx_destroy(&sc->cfg_sx);
        mtx_destroy(&sc->mtx);

        return (0);
}
```

Siete fases. Cada una es un requisito estricto. Vamos a recorrerlas.

**Fase 1**: rechaza el detach mientras el dispositivo está en uso (`active_fhs > 0`). Sin esto, un usuario que tuviera el dispositivo abierto podría cerrar su descriptor en medio del detach, accediendo a rutas de código que ya no tienen estado válido.

**Fase 2**: marca el dispositivo como en proceso de desaparición. El flag `is_attached` es la señal para toda ruta de código bloqueada o futura de que el dispositivo está siendo eliminado. Los broadcasts de cv despiertan a cualquier thread que esté esperando en un cv; estos vuelven a comprobar `is_attached` y salen con `ENXIO`. El lock se mantiene durante esta fase para hacer el cambio atómico respecto a cualquier thread que acabe de entrar en un manejador.

**Fase 3**: drena `selinfo`. Esto garantiza que quienes llaman a `selrecord(9)` y `selwakeup(9)` ya no referencian las estructuras selinfo del dispositivo.

**Fase 4**: drena cada callout. Cada `callout_drain` cancela los pendientes y espera a los que están en vuelo. El mutex se libera antes del primer drain (se liberó al final de la fase 2). Tras la fase 4, ningún callout puede estar en ejecución.

**Fase 5**: destruye los cdevs. Tras esto, ningún nuevo `open(2)` puede llegar al driver. (Los que se colaron justo antes ya habrían sido rechazados en la fase 1, pero esa es la red de seguridad.)

**Fase 6**: libera los recursos auxiliares (contexto sysctl, cbuf, contadores).

**Fase 7**: destruye las primitivas en orden inverso. El orden importa por la misma razón discutida en el capítulo 12: los cvs usan el mutex como su interlock; si destruyéramos el mutex primero, un callback en medio de liberar el mutex provocaría un crash.

Es mucho. También es lo que todo driver tiene que hacer si tiene callouts y primitivas. El código de acompañamiento del capítulo 13 (`stage4-final/myfirst.c`) sigue este patrón exactamente.

### Una nota sobre la descarga de módulos del kernel

`kldunload myfirst` desencadena la ruta de detach a través del manejo de eventos de módulos del kernel. El evento `MOD_UNLOAD` hace que el kernel llame a la función detach del driver. Si la función detach devuelve un error (típicamente `EBUSY`), la descarga falla y el módulo permanece cargado.

El patrón estándar que acabamos de recorrer devuelve `EBUSY` si `active_fhs > 0`. Un usuario que quiera descargar el driver debe primero cerrar todos los descriptores abiertos. Desde una shell:

```sh
# List processes holding the device open.
$ fstat | grep myfirst
USER     CMD          PID    FD     ... NAME
root     cat        12345     3     ... /dev/myfirst
$ kill 12345
$ kldunload myfirst
```

Este es el comportamiento UNIX convencional; se espera que el usuario cierre los descriptores antes de descargar. El driver lo impone.

### Reinicialización tras el drain

Un punto sutil: tras `callout_drain`, el callout está inactivo, pero *no* se encuentra en el mismo estado que un callout recién inicializado. Los campos `c_func` y `c_arg` siguen apuntando al último callback y argumento, por si un `callout_schedule` posterior quisiera reutilizarlos. Los flags internos quedan limpios.

Si quisieras reutilizar el mismo `struct callout` para un propósito diferente (lock diferente, firma de callback diferente), necesitarías llamar de nuevo a `callout_init_mtx` (o una de sus variantes) para reinicializar. En la ruta de detach nunca reinicializamos; la memoria circundante está a punto de liberarse. El estado en el momento del drain es suficiente.

### Un recorrido práctico: detectar la condición de carrera del unload en DDB

Para hacer que la condición de carrera del unload sea algo visceral, recorramos qué ocurre cuando un driver descuidado omite `callout_drain` y el siguiente disparo del callout hace que el kernel entre en pánico.

Imagina un driver defectuoso que desactiva el sysctl del heartbeat en el detach pero no llama a `callout_drain`. La ruta de detach tiene este aspecto:

```c
static int
buggy_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        MYFIRST_LOCK(sc);
        sc->is_attached = 0;
        sc->heartbeat_interval_ms = 0;  /* hope the callback won't re-arm */
        MYFIRST_UNLOCK(sc);

        /* No callout_drain here! */

        destroy_dev(sc->cdev);
        mtx_destroy(&sc->mtx);
        return (0);
}
```

Los valores `is_attached = 0` y `heartbeat_interval_ms = 0` tienen la intención de hacer que el callback salga sin volver a armarse. Pero:

- El callback podría estar ya en medio de su ejecución cuando el detach comienza. El lock está en manos de la ruta adquirida por el kernel. `MYFIRST_LOCK(sc)` de la ruta de detach se bloquea hasta que el callback libera el lock. Una vez que el detach adquiere el lock, se establecen `is_attached` y `heartbeat_interval_ms`. El detach libera el lock. Hasta aquí todo bien.

- *Pero*: el callback que acaba de ejecutarse ya había entrado en la ruta de re-armado antes de comprobar `interval_ms`. Llama a `callout_reset` para programar el siguiente disparo, con el valor recién limpiado de `interval_ms` igual a 0... no, espera, el callback vuelve a leer `sc->heartbeat_interval_ms`, ve 0 y no se vuelve a armar. Bien, ese caso es seguro.

- *O bien*: el callback completó limpiamente sin re-armado. El callout está ahora inactivo. La ruta de detach continúa. Destruye `sc->mtx` y el estado circundante. Todo parece estar bien.

- *Entonces*: una invocación diferente del callback empieza a dispararse. El callout no estaba en la rueda (sin re-armado), así que esto no debería ocurrir, ¿verdad?

Puede ocurrir si había un disparo concurrente en una CPU diferente. Imagina: el callout se dispara en CPU 0 y CPU 1 en rápida sucesión. CPU 0 inicia el callback (adquiere el lock). CPU 1 entra en la ruta de disparo, intenta adquirir el lock, se bloquea. CPU 0 termina el callback y se vuelve a armar (pone el callout de nuevo en la rueda para el siguiente disparo). CPU 0 libera el lock. CPU 1 adquiere el lock y ejecuta el callback. El callback se vuelve a armar. CPU 1 libera el lock.

Ahora supón que la ruta de detach se ejecuta entre la liberación de CPU 0 y la adquisición de CPU 1. El detach toma el lock (que ahora está libre), limpia los flags, libera el lock. CPU 1 adquiere el lock y llama al callback. El callback vuelve a leer los flags, ve los valores limpiados y sale sin volver a armarse. Bien, todavía seguro.

Pero ahora considera esto: la ruta de detach ha destruido el mutex. La ejecución del callback de CPU 1 ha terminado. El kernel libera el mutex que ya ha sido destruido. La operación de liberación opera sobre memoria liberada. Pánico.

Esta es la condición de carrera del unload. La solución es sencilla pero absolutamente necesaria: llama a `callout_drain(&sc->heartbeat_co)` después de liberar el mutex y antes de destruir las primitivas. El drain espera a que todos los callbacks en vuelo (en cualquier CPU) retornen antes de retornar él mismo.

Recorramos el caso con el drain en su lugar:

- El detach adquiere el lock, limpia los flags, libera el lock.
- El detach llama a `callout_drain(&sc->heartbeat_co)`. El drain detecta cualquier callback en vuelo y espera.
- Todos los callbacks que estaban disparándose retornan limpiamente (vuelven a leer los flags, salen sin volver a armarse).
- El drain retorna.
- El detach destruye el cdev y luego destruye el mutex.
- Ningún callback puede estar en ejecución en este punto. Ningún callback puede dispararse más tarde porque la rueda no tiene el callout.

El drain es la red de seguridad. Omitirlo produce un pánico que puede no ocurrir en cada descarga, pero que ocurrirá tarde o temprano bajo carga. El drain es obligatorio.

### Qué ocurre si olvidas el drain en un kernel de producción

Un kernel de producción sin `INVARIANTS` ni `WITNESS` no detecta la condición de carrera del desinstalado de antemano. La primera vez que un callout se dispara tras reutilizarse la memoria del módulo liberado, el kernel lee instrucciones basura, salta a una ubicación aleatoria y entra en pánico con el patrón que los bytes aleatorios produzcan. El backtrace del crash apunta a código que nunca fue el problema; el error real ocurrió varios segundos antes, en el camino del detach que no ejecutó el drain.

Esta es exactamente la razón por la que el consejo habitual es "prueba en un kernel de depuración antes de promover a producción". `WITNESS` detecta algunas formas de la condición de carrera (avisa cuando se llama a callbacks con locks que no pueden dormir retenidos de manera inesperada); `INVARIANTS` detecta otras (el `mtx_destroy` de un mutex ya destruido). El kernel de producción solo ve el panic y el backtrace incorrecto.

### Qué devuelve `callout_drain`

`callout_drain` devuelve el mismo valor que `callout_stop`: 1 si se canceló un callout pendiente, 0 en caso contrario. Los llamadores normalmente no examinan el valor de retorno; la función se llama por su efecto secundario (esperar a que los callbacks en vuelo terminen).

Si quieres asegurarte de que el callout está completamente inactivo tras completar un camino de código concreto, la disciplina es: llama a `callout_drain` e ignora el valor de retorno. Independientemente de si el callout estaba pendiente o no, después del drain queda inactivo.

### Orden del detach con múltiples callouts

Si tu driver tiene tres callouts (heartbeat, watchdog, fuente de tick) y ejecutas `callout_drain` en cada uno por turno, el tiempo de espera total es como máximo el del callback en vuelo más largo, no la suma de todos. Los drains son independientes: cada uno espera a su propio callback. En la práctica pueden ejecutarse en paralelo porque cada uno solo bloquea sobre su callout específico.

Para el pseudo-dispositivo del capítulo, los callbacks son cortos (microsegundos). El tiempo de drain está dominado por el coste del wakeup en la sleep queue, no por el trabajo del callback. En total, los tres drains completan en mucho menos de un milisegundo, incluso bajo carga.

Para drivers con callbacks más largos, el tiempo de espera puede ser mayor. Un callback de watchdog que tarde 10 ms significa que el peor caso del drain es 10 ms (si llamaste a `callout_drain` justo mientras se estaba disparando). La mayor parte del tiempo el callout está inactivo y el drain es instantáneo. En cualquier caso, el drain está acotado; no itera indefinidamente.

### El mismo error en una primitiva distinta: un esbozo de taskqueue

El error de "callback ejecutado tras el detach" no es exclusivo de los callouts. Toda primitiva de trabajo diferido del kernel tiene el mismo problema, y la respuesta estándar es siempre una rutina de drain que espera a que los callbacks en vuelo terminen. Un breve recorrido paralelo refuerza la idea sin desviar el Capítulo 13 de su tema.

Supongamos que un driver encola trabajo en un taskqueue en lugar de usar un callout. El softc contiene un `struct task` y un `struct taskqueue *`, y en algún momento el driver llama a `taskqueue_enqueue(sc->tq, &sc->work)` cuando hay trabajo pendiente. Imagina ahora un detach con errores que borra `is_attached` y desmonta el softc pero olvida drenar la tarea:

```c
static int
buggy_tq_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        MYFIRST_LOCK(sc);
        sc->is_attached = 0;
        MYFIRST_UNLOCK(sc);

        /* No taskqueue_drain here! */

        destroy_dev(sc->cdev);
        free(sc->buf, M_MYFIRST);
        mtx_destroy(&sc->mtx);
        return (0);
}
```

El resultado tiene la misma forma que en el caso del callout. Si la tarea estaba pendiente cuando se ejecutó el detach, el thread trabajador la extrae después de que el detach haya liberado `sc->buf` y destruido `sc->mtx`. El manejador de la tarea desreferencia `sc`, encuentra memoria obsoleta y, o bien lee basura, o entra en pánico en la primera operación con lock. Si la tarea ya estaba ejecutándose en otra CPU, el thread trabajador sigue dentro del manejador cuando el detach libera la memoria que hay bajo sus pies, con el mismo desenlace.

La corrección es estructuralmente idéntica a `callout_drain`:

```c
taskqueue_drain(sc->tq, &sc->work);
```

`taskqueue_drain(9)` espera hasta que la tarea especificada no esté ni pendiente ni en ejecución en ningún thread trabajador. Después de que retorne, esa tarea no puede dispararse de nuevo a menos que algo la re-encole, que es exactamente lo que el detach pretende impedir borrando `is_attached` primero. Para drivers que usan muchas tareas en la misma cola, `taskqueue_drain_all(9)` espera a todas las tareas actualmente encoladas o en ejecución en ese taskqueue, que es la llamada habitual en un camino de desinstalado de módulo donde nada de lo que hay en la cola volverá a encolarse.

La conclusión no es una regla nueva sino una más amplia: cualquier primitiva de trabajo diferido del kernel, ya sea `callout(9)`, `taskqueue(9)` o los callbacks de epoch del network stack que encontrarás en la Parte 6, necesita un drain correspondiente antes de liberar la memoria que lee. El Capítulo 16 recorre `taskqueue(9)` en profundidad, incluida la forma en que el drain interactúa con el orden de encolado de tareas; por ahora, recuerda que el modelo mental es idéntico. Borra el flag, suelta el lock, drena la primitiva, destruye el almacenamiento. La palabra cambia con la primitiva, pero la forma del patrón no.

### Cerrando la sección 6

La condición de carrera del desinstalado es real. `callout_drain` es la solución. El patrón estándar de detach es: rechazar si está ocupado, borrar `is_attached` bajo el lock, hacer broadcast de las cvs, soltar el lock, drenar selinfo, drenar cada callout, destruir los cdevs, liberar recursos auxiliares, destruir las primitivas en orden inverso. Cada fase es necesaria; saltarse cualquiera de ellas crea una condición de carrera que hace entrar en pánico al kernel bajo carga.

La Sección 7 pone el framework a trabajar en casos de uso reales de temporizadores: watchdogs, debouncing y fuentes de tick periódicas.



## Sección 7: Casos de uso y extensiones para el trabajo temporizado

Las secciones 4 a 6 introdujeron el callout de heartbeat: periódico, consciente del lock y drenado en el desmontaje. El mismo patrón resuelve una amplia gama de problemas reales de drivers con pequeñas variaciones. Esta sección recorre tres callouts más que añadiremos a `myfirst`: un watchdog que detecta la obsolescencia del buffer, una fuente de tick que inyecta eventos sintéticos y, brevemente, un patrón de debounce usado en muchos drivers de hardware. Junto con el heartbeat, los cuatro cubren la mayor parte de los usos prácticos de los temporizadores en drivers.

Trata esta sección como una colección de recetas. Cada subsección es un patrón autocontenido que puedes trasladar a otros drivers.

### Patrón 1: un temporizador de watchdog

Un watchdog detecta una condición bloqueada y actúa en consecuencia. La forma clásica: programa un callout al inicio de una operación; si la operación completa con éxito, cancela el callout; si el callout se dispara, se presume que la operación está bloqueada y el driver toma medidas de recuperación.

Para `myfirst`, un watchdog útil es "el buffer no ha avanzado durante demasiado tiempo". Si `cb_used > 0` y el valor no ha cambiado en N segundos, ningún lector está drenando el buffer. Esto es inusual; registraremos un aviso.

Añade campos al softc:

```c
struct callout          watchdog_co;
int                     watchdog_interval_ms;   /* 0 = disabled */
size_t                  watchdog_last_used;
```

`watchdog_interval_ms` es un parámetro ajustable por sysctl. `watchdog_last_used` registra el valor de `cbuf_used` del tick anterior; el siguiente tick lo compara.

Inicializa en attach:

```c
callout_init_mtx(&sc->watchdog_co, &sc->mtx, 0);
sc->watchdog_interval_ms = 0;
sc->watchdog_last_used = 0;
```

Drena en detach:

```c
callout_drain(&sc->watchdog_co);
```

El callback:

```c
static void
myfirst_watchdog(void *arg)
{
        struct myfirst_softc *sc = arg;
        size_t used;
        int interval;

        MYFIRST_ASSERT(sc);

        if (!sc->is_attached)
                return;

        used = cbuf_used(&sc->cb);
        if (used > 0 && used == sc->watchdog_last_used) {
                device_printf(sc->dev,
                    "watchdog: buffer has %zu bytes, no progress in last "
                    "interval; reader stuck?\n", used);
        }
        sc->watchdog_last_used = used;

        interval = sc->watchdog_interval_ms;
        if (interval > 0)
                callout_reset(&sc->watchdog_co,
                    (interval * hz + 999) / 1000,
                    myfirst_watchdog, sc);
}
```

La estructura refleja la del heartbeat: assert, comprobar `is_attached`, realizar el trabajo, re-armar si el intervalo no es cero. El trabajo en este caso es la comprobación de obsolescencia: comparar el valor actual de `cbuf_used` con el último registrado; si coinciden y no son cero, no se ha producido ningún avance.

El manejador de sysctl es simétrico al del heartbeat:

```c
static int
myfirst_sysctl_watchdog_interval_ms(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, old, error;

        old = sc->watchdog_interval_ms;
        new = old;
        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);
        if (new < 0)
                return (EINVAL);

        MYFIRST_LOCK(sc);
        sc->watchdog_interval_ms = new;
        if (new > 0 && old == 0) {
                sc->watchdog_last_used = cbuf_used(&sc->cb);
                callout_reset(&sc->watchdog_co,
                    (new * hz + 999) / 1000,
                    myfirst_watchdog, sc);
        } else if (new == 0 && old > 0) {
                callout_stop(&sc->watchdog_co);
        }
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

La única adición: al habilitar, inicializamos `watchdog_last_used` con el valor actual de `cbuf_used`, de modo que la primera comparación tenga una referencia con sentido.

Para probarlo: habilita el watchdog con un intervalo de 2 segundos, escribe algunos bytes en el buffer y no los leas. Pasados dos segundos, `dmesg` debería mostrar el aviso del watchdog.

```sh
$ sysctl -w dev.myfirst.0.watchdog_interval_ms=2000
$ printf 'hello' > /dev/myfirst
$ sleep 5
$ dmesg | tail
myfirst0: watchdog: buffer has 5 bytes, no progress in last interval; reader stuck?
myfirst0: watchdog: buffer has 5 bytes, no progress in last interval; reader stuck?
```

Ahora drena el buffer:

```sh
$ cat /dev/myfirst
hello
```

El watchdog deja de avisar porque `cbuf_used` es ahora cero (la comparación `used > 0` falla).

Este es un watchdog simplificado. Los watchdogs reales hacen más: reinician un motor de hardware, abortan una petición bloqueada, registran en el ringbuffer del kernel con un formato específico que las herramientas de monitorización pueden buscar con grep. La forma es la misma: detectar, actuar, re-armar.

### Patrón 2: una fuente de tick para eventos sintéticos

Una fuente de tick es un callout que genera eventos periódicamente como si lo hiciera el hardware. Es útil para drivers que simulan algo o que quieren una carga de prueba estable independiente de la actividad del espacio de usuario.

Para `myfirst`, una fuente de tick puede escribir periódicamente un único byte en el cbuf. Con el heartbeat habilitado, los contadores de bytes aumentarán visiblemente sin ningún productor externo.

Añade campos:

```c
struct callout          tick_source_co;
int                     tick_source_interval_ms;  /* 0 = disabled */
char                    tick_source_byte;          /* the byte to write */
```

Inicializa en attach:

```c
callout_init_mtx(&sc->tick_source_co, &sc->mtx, 0);
sc->tick_source_interval_ms = 0;
sc->tick_source_byte = 't';
```

Drena en detach:

```c
callout_drain(&sc->tick_source_co);
```

El callback:

```c
static void
myfirst_tick_source(void *arg)
{
        struct myfirst_softc *sc = arg;
        size_t put;
        int interval;

        MYFIRST_ASSERT(sc);

        if (!sc->is_attached)
                return;

        if (cbuf_free(&sc->cb) > 0) {
                put = cbuf_write(&sc->cb, &sc->tick_source_byte, 1);
                if (put > 0) {
                        counter_u64_add(sc->bytes_written, put);
                        cv_signal(&sc->data_cv);
                        /* selwakeup omitted on purpose: it may sleep
                         * and we are inside a callout context with the
                         * mutex held. Defer to a taskqueue if real-time
                         * poll(2) wakeups are needed. */
                }
        }

        interval = sc->tick_source_interval_ms;
        if (interval > 0)
                callout_reset(&sc->tick_source_co,
                    (interval * hz + 999) / 1000,
                    myfirst_tick_source, sc);
}
```

La estructura es la misma que la del heartbeat. El trabajo es diferente: escribe un byte en el cbuf, incrementa el contador y señaliza `data_cv` para que cualquier lector despierte.

Observa la omisión deliberada de `selwakeup` en el callback. `selwakeup` puede dormir y puede adquirir otros locks, lo cual es ilegal bajo nuestro mutex. Llamarlo desde un contexto de callout con el mutex retenido sería una violación de `WITNESS`. La cv_signal es suficiente para despertar a los lectores bloqueados; los procesos en espera de `poll(2)` no se despertarán en tiempo real, pero detectarán el siguiente cambio de estado en su intervalo de poll habitual. Para un driver real que necesite wakeups inmediatos de `poll(2)` desde un callout, la solución es diferir el `selwakeup` a un taskqueue (Capítulo 16). Para el Capítulo 13, omitirlo es aceptable.

Un manejador de sysctl habilita y deshabilita, igual que los demás:

```c
static int
myfirst_sysctl_tick_source_interval_ms(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, old, error;

        old = sc->tick_source_interval_ms;
        new = old;
        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);
        if (new < 0)
                return (EINVAL);

        MYFIRST_LOCK(sc);
        sc->tick_source_interval_ms = new;
        if (new > 0 && old == 0)
                callout_reset(&sc->tick_source_co,
                    (new * hz + 999) / 1000,
                    myfirst_tick_source, sc);
        else if (new == 0 && old > 0)
                callout_stop(&sc->tick_source_co);
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

Para probarlo:

```sh
$ sysctl -w dev.myfirst.0.tick_source_interval_ms=100
$ cat /dev/myfirst
ttttttttttttttttttttttttttttttt    # ten 't's per second
^C
$ sysctl -w dev.myfirst.0.tick_source_interval_ms=0
```

La fuente de tick produce diez caracteres 't' por segundo, que `cat` lee e imprime. Deshabilítala poniendo el sysctl de nuevo a cero.

### Patrón 3: un patrón de debounce

Un debounce ignora eventos repetidos rápidamente. El patrón: cuando llega un evento, comprueba si ya hay un "temporizador de debounce" pendiente; si es así, ignora el evento; si no, programa un temporizador de debounce de N milisegundos y actúa sobre el evento cuando el temporizador se dispare.

Para `myfirst`, no tenemos una fuente de eventos de hardware, así que no implementaremos un debounce completo. El patrón, en pseudocódigo:

```c
static void
some_event_callback(struct myfirst_softc *sc)
{
        MYFIRST_LOCK(sc);
        sc->latest_event_time = ticks;
        if (!callout_pending(&sc->debounce_co)) {
                callout_reset(&sc->debounce_co,
                    DEBOUNCE_DURATION_TICKS,
                    myfirst_debounce_handler, sc);
        }
        MYFIRST_UNLOCK(sc);
}

static void
myfirst_debounce_handler(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        /* Act on the latest event seen. */
        process_event(sc, sc->latest_event_time);
        /* Do not re-arm; one-shot. */
}
```

Cuando llega el primer evento, se programa el temporizador de debounce. Los eventos posteriores actualizan el "tiempo del último evento" registrado pero no reprograman el temporizador (porque todavía está pendiente). Cuando el temporizador de debounce se dispara, el manejador procesa el evento más reciente. Después de que el manejador retorne, el temporizador ya no está pendiente; el siguiente evento lo reprogramará.

Este es un patrón de un solo disparo, no periódico. El callback no se re-arma. La comprobación de `callout_pending` en `some_event_callback` es la puerta de entrada.

El Laboratorio 13.5 recorre la implementación de un debounce similar como ejercicio de ampliación. El capítulo no lo añade a `myfirst` porque no tenemos ningún evento de hardware al que aplicar debounce, pero el patrón merece recordarse.

### Patrón 4: un reintento con retroceso exponencial

El patrón de reintento con retroceso: una operación falla; programa un reintento después de N milisegundos; si el reintento también falla, programa el siguiente después de 2N milisegundos; y así sucesivamente, hasta un máximo.

Para `myfirst`, ninguna operación falla de un modo que exija reintentos. El patrón:

```c
struct callout          retry_co;
int                     retry_attempt;          /* 0, 1, 2, ... */
int                     retry_base_ms;          /* base interval */
int                     retry_max_attempts;     /* cap */

static void
some_operation_failed(struct myfirst_softc *sc)
{
        int next_delay_ms;

        MYFIRST_LOCK(sc);
        if (sc->retry_attempt < sc->retry_max_attempts) {
                next_delay_ms = sc->retry_base_ms * (1 << sc->retry_attempt);
                callout_reset(&sc->retry_co,
                    (next_delay_ms * hz + 999) / 1000,
                    myfirst_retry, sc);
                sc->retry_attempt++;
        } else {
                /* Give up. */
                device_printf(sc->dev, "retry: exhausted attempts; failing\n");
                some_failure_action(sc);
        }
        MYFIRST_UNLOCK(sc);
}

static void
myfirst_retry(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        if (some_operation(sc)) {
                /* success */
                sc->retry_attempt = 0;
        } else {
                /* failure: schedule next retry */
                some_operation_failed(sc);
        }
}
```

El callback reintenta la operación. El éxito reinicia el contador de intentos. El fallo programa el siguiente reintento con un retraso que crece exponencialmente, limitado por `retry_max_attempts`.

Este patrón aparece en muchos drivers reales, en particular en drivers de almacenamiento y de red que gestionan errores transitorios de hardware. El Capítulo 13 no lo añade a `myfirst` porque no tenemos fallos que reintentar. El patrón está ya en tu caja de herramientas.

### Patrón 5: un recolector diferido

Un recolector diferido es un callout de un solo disparo que libera algo después de un período de gracia. Se utiliza cuando un objeto no puede liberarse inmediatamente porque algún otro camino de código aún puede retener una referencia, pero sabemos que pasado cierto tiempo todas las referencias se habrán agotado.

El patrón, esbozado en pseudocódigo (el tipo `some_object` representa cualquier objeto de liberación diferida que use tu driver):

```c
struct some_object {
        TAILQ_ENTRY(some_object) link;
        /* ... per-object fields ... */
};

TAILQ_HEAD(some_object_list, some_object);

struct myfirst_softc {
        /* ... existing fields ... */
        struct callout           reaper_co;
        struct some_object_list  pending_free;
        /* ... */
};

static void
schedule_free(struct myfirst_softc *sc, struct some_object *obj)
{
        MYFIRST_LOCK(sc);
        TAILQ_INSERT_TAIL(&sc->pending_free, obj, link);
        if (!callout_pending(&sc->reaper_co))
                callout_reset(&sc->reaper_co, hz, myfirst_reaper, sc);
        MYFIRST_UNLOCK(sc);
}

static void
myfirst_reaper(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct some_object *obj, *tmp;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        TAILQ_FOREACH_SAFE(obj, &sc->pending_free, link, tmp) {
                TAILQ_REMOVE(&sc->pending_free, obj, link);
                free(obj, M_DEVBUF);
        }

        /* Do not re-arm; new objects scheduled later will re-arm us. */
}
```

El recolector se ejecuta una vez por segundo (o con el intervalo que tenga sentido), libera todo lo que haya en la lista pendiente y se detiene. La nueva programación añade a la lista y re-arma solo si el recolector no está actualmente pendiente.

Se utiliza en drivers de red donde los buffers de recepción no pueden liberarse de inmediato porque la capa de red todavía mantiene referencias sobre ellos; el buffer se encola para el reaper, que lo libera tras un período de gracia.

`myfirst` no necesita este patrón. Está en la caja de herramientas.

### Patrón 6: Un sustituto para el bucle de sondeo

Algunos dispositivos de hardware no generan interrupciones para los eventos que le interesan al driver. Un ejemplo típico: un sensor con un registro de estado que el driver debe comprobar cada pocos milisegundos para conocer nuevas lecturas. Sin callouts, el driver tendría que hacer spinning (malgastando CPU) o ejecutar un thread del kernel que duerme y sondea (malgastando un thread). Con callouts, el bucle de sondeo es un callback periódico que lee el registro, toma la acción apropiada y se rearma.

```c
static void
myfirst_poll(void *arg)
{
        struct myfirst_softc *sc = arg;
        uint32_t status;
        int interval;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        status = bus_read_4(sc->res, REG_STATUS);   /* hypothetical */
        if (status & STATUS_DATA_READY) {
                /* Pull data from the device into the cbuf. */
                myfirst_drain_hardware(sc);
        }
        if (status & STATUS_ERROR) {
                /* Recover from the error. */
                myfirst_handle_error(sc);
        }

        interval = sc->poll_interval_ms;
        if (interval > 0)
                callout_reset(&sc->poll_co,
                    (interval * hz + 999) / 1000,
                    myfirst_poll, sc);
}
```

El callback lee un registro de hardware (no implementado en nuestro pseudo-dispositivo, pero la estructura es clara), comprueba bits, actúa y se rearma. El intervalo determina con qué frecuencia el driver realiza la comprobación; cuanto más corto, más reactivo, pero mayor consumo de CPU. Los drivers de sondeo reales suelen usar intervalos de 1 a 10 ms cuando están activos; más largos cuando están en reposo.

El comentario del código sobre `bus_read_4` es una referencia anticipada al Capítulo 19, que introduce el acceso al espacio de bus. Para el Capítulo 13, trátalo como pseudocódigo que demuestra el patrón; lo que importa es la lógica de sondeo.

### Patrón 7: Una ventana de estadísticas

Un callout periódico que toma una instantánea de los contadores internos a intervalos regulares y calcula tasas por intervalo. Resulta útil para la monitorización: el driver puede responder "¿cuántos bytes por segundo estoy moviendo ahora mismo?" sin que el usuario tenga que muestrear manualmente.

```c
struct myfirst_stats_window {
        uint64_t        last_bytes_read;
        uint64_t        last_bytes_written;
        uint64_t        rate_bytes_read;       /* bytes/sec, latest interval */
        uint64_t        rate_bytes_written;
};

struct myfirst_softc {
        /* ... existing fields ... */
        struct callout                  stats_window_co;
        int                             stats_window_interval_ms;
        struct myfirst_stats_window     stats_window;
        /* ... */
};

static void
myfirst_stats_window(void *arg)
{
        struct myfirst_softc *sc = arg;
        uint64_t cur_br, cur_bw;
        int interval;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        cur_br = counter_u64_fetch(sc->bytes_read);
        cur_bw = counter_u64_fetch(sc->bytes_written);
        interval = sc->stats_window_interval_ms;

        if (interval > 0) {
                /* bytes-per-second over this interval */
                sc->stats_window.rate_bytes_read = (cur_br -
                    sc->stats_window.last_bytes_read) * 1000 / interval;
                sc->stats_window.rate_bytes_written = (cur_bw -
                    sc->stats_window.last_bytes_written) * 1000 / interval;
        }

        sc->stats_window.last_bytes_read = cur_br;
        sc->stats_window.last_bytes_written = cur_bw;

        if (interval > 0)
                callout_reset(&sc->stats_window_co,
                    (interval * hz + 999) / 1000,
                    myfirst_stats_window, sc);
}
```

Expón las tasas como sysctls. Un usuario puede ejecutar `sysctl dev.myfirst.0.stats.rate_bytes_read` y ver la tasa por intervalo, calculada en tiempo real sin necesidad de muestrear y diferenciar manualmente.

Este patrón aparece en muchos drivers orientados a la monitorización. La granularidad (el intervalo) es configurable; los intervalos más largos suavizan las ráfagas cortas; los más cortos responden con mayor rapidez. Elige según lo que el usuario quiera medir.

### Patrón 8: Una actualización periódica de estado

Un callout periódico que refresca un valor en caché que el resto del driver lee. Resulta útil cuando el valor subyacente es costoso de calcular cada vez, pero es aceptable que esté ligeramente desactualizado.

Para nuestro `myfirst`, no tenemos ningún cálculo costoso que cachear. La estructura, en pseudocódigo:

```c
static void
myfirst_refresh_status(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        sc->cached_status = expensive_compute(sc);
        callout_reset(&sc->refresh_co, hz, myfirst_refresh_status, sc);
}

/* Other code reads sc->cached_status freely; it may be up to 1s stale. */
```

Se utiliza en drivers donde el cálculo es costoso (analizar una tabla de estado del hardware, comunicarse con un subsistema remoto) pero el consumidor puede tolerar valores no actualizados. El callback se ejecuta periódicamente y refresca la caché; el consumidor obtiene el valor almacenado.

`myfirst` no necesita este patrón. Queda en tu caja de herramientas.

### Patrón 9: Un reset periódico

Algunos dispositivos de hardware necesitan un reset periódico (una escritura en un registro específico) para evitar que se dispare un watchdog interno. El patrón:

```c
static void
myfirst_periodic_reset(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        bus_write_4(sc->res, REG_KEEPALIVE, KEEPALIVE_VALUE);
        callout_reset(&sc->keepalive_co, hz / 2,
            myfirst_periodic_reset, sc);
}
```

El hardware espera la escritura de mantenimiento (keepalive) al menos cada segundo; nosotros la enviamos cada 500 ms para tener margen. Si nos saltamos unas pocas escrituras por carga del sistema o replanificación, el hardware no entra en pánico.

Se utiliza en controladores de almacenamiento, controladores de red y sistemas embebidos donde el dispositivo tiene un watchdog en cada lado que el driver del host debe satisfacer.

### Combinando patrones

Un driver típicamente usa varios callouts a la vez. `myfirst` (la Etapa 4 de este capítulo) usa tres: heartbeat, watchdog y fuente de ticks. Cada uno tiene su propio callout y su propio sysctl configurable. Todos comparten el mismo lock (`sc->mtx`), lo que significa que solo uno se activa a la vez; la serialización es automática.

En un driver más complejo, podrías tener diez o veinte callouts, cada uno con un propósito específico. El patrón escala: cada callout tiene su propio `struct callout`, su propio callback, su propio sysctl (si es visible al usuario) y su propia línea en el bloque `callout_drain` del detach. Las disciplinas de este capítulo (inicialización con awareness del lock, comprobación de `is_attached`, drenado en el detach) se aplican a todos y cada uno de ellos.

### Cerrando la sección 7

Nueve patrones cubren la mayor parte de lo que hacen los temporizadores en un driver: heartbeat, watchdog, debounce, reintentos con backoff exponencial, reaper diferido, acumulación de estadísticas, bucle de sondeo, ventana de estadísticas, actualización periódica de estado y reset periódico. Cada uno es una pequeña variación sobre la forma periódica o de disparo único. Las disciplinas de las secciones 4 a 6 (inicialización con awareness del lock, comprobación de `is_attached`, drenado en el detach) se aplican de forma uniforme. Un driver que añade nuevos temporizadores sigue la misma receta; la superficie de código crece sin que aumente la carga de mantenimiento.

La sección 8 cierra el capítulo con el paso de limpieza: documentación, incremento de versión, prueba de regresión y lista de verificación previa al commit.



## Sección 8: Refactorización y versionado del driver con soporte de temporizadores

El driver cuenta ahora con tres callouts (heartbeat, watchdog y fuente de ticks), cuatro sysctls (los tres intervalos más la configuración existente) y una ruta de detach que drena cada callout de forma segura. El trabajo restante es el paso de limpieza: ordenar el código fuente para mayor claridad, actualizar la documentación, incrementar la versión, ejecutar el análisis estático y verificar que la suite de regresión pasa.

Esta sección sigue la misma estructura que las secciones equivalentes de los Capítulos 11 y 12. Nada de esto es glamuroso. Todo ello marca la diferencia entre un driver que se entrega una vez y un driver que sigue funcionando a medida que crece.

### Limpiando el código fuente

Después de las adiciones de este capítulo, vale la pena realizar tres pequeñas reorganizaciones.

**Agrupa el código relacionado con callouts.** Mueve todos los callbacks de callout (`myfirst_heartbeat`, `myfirst_watchdog`, `myfirst_tick_source`) a una sección única del archivo fuente, después de los helpers de espera y antes de los manejadores de cdevsw. Mueve los manejadores de sysctl correspondientes junto a ellos. Al compilador no le importa el orden; al lector sí.

**Estandariza el vocabulario de macros.** Añade un pequeño conjunto de macros para que las operaciones con callouts sean consistentes en todo el driver. El patrón existente con `MYFIRST_LOCK` y `MYFIRST_CFG_*` se extiende de forma natural:

```c
#define MYFIRST_CO_INIT(sc, co)  callout_init_mtx((co), &(sc)->mtx, 0)
#define MYFIRST_CO_DRAIN(co)     callout_drain((co))
```

La macro `MYFIRST_CO_INIT` toma `sc` explícitamente para que funcione en cualquier función, no solo en aquellas donde por casualidad haya una variable local llamada `sc` en el ámbito. `MYFIRST_CO_DRAIN` solo necesita el propio callout, porque drenar no requiere el softc.

Las macros son ligeras, pero documentan la convención: cada callout del driver usa `sc->mtx` como su lock y se drena en el detach. Un mantenedor futuro que añada un callout verá la macro y conocerá la regla.

**Comenta el orden del detach.** La función de detach es corta por sí sola, pero el orden de las operaciones es crítico. Añade comentarios en cada fase:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* Phase 1: refuse if in use. */
        MYFIRST_LOCK(sc);
        if (sc->active_fhs > 0) {
                MYFIRST_UNLOCK(sc);
                return (EBUSY);
        }

        /* Phase 2: signal "going away" to all waiters and callbacks. */
        sc->is_attached = 0;
        cv_broadcast(&sc->data_cv);
        cv_broadcast(&sc->room_cv);
        MYFIRST_UNLOCK(sc);

        /* Phase 3: drain selinfo. */
        seldrain(&sc->rsel);
        seldrain(&sc->wsel);

        /* Phase 4: drain every callout (no lock held; safe to sleep). */
        MYFIRST_CO_DRAIN(&sc->heartbeat_co);
        MYFIRST_CO_DRAIN(&sc->watchdog_co);
        MYFIRST_CO_DRAIN(&sc->tick_source_co);

        /* Phase 5: destroy cdevs (no new opens after this). */
        if (sc->cdev_alias != NULL) {
                destroy_dev(sc->cdev_alias);
                sc->cdev_alias = NULL;
        }
        if (sc->cdev != NULL) {
                destroy_dev(sc->cdev);
                sc->cdev = NULL;
        }

        /* Phase 6: free auxiliary resources. */
        sysctl_ctx_free(&sc->sysctl_ctx);
        cbuf_destroy(&sc->cb);
        counter_u64_free(sc->bytes_read);
        counter_u64_free(sc->bytes_written);

        /* Phase 7: destroy primitives in reverse order. */
        cv_destroy(&sc->data_cv);
        cv_destroy(&sc->room_cv);
        sx_destroy(&sc->cfg_sx);
        mtx_destroy(&sc->mtx);

        return (0);
}
```

En el attach, la inicialización correspondiente usa la forma de dos argumentos para que `sc` se pase explícitamente:

```c
MYFIRST_CO_INIT(sc, &sc->heartbeat_co);
MYFIRST_CO_INIT(sc, &sc->watchdog_co);
MYFIRST_CO_INIT(sc, &sc->tick_source_co);
```

Los comentarios transforman la función de una secuencia de llamadas aparentemente arbitrarias en una lista de verificación documentada.

### Actualizando LOCKING.md

El `LOCKING.md` del Capítulo 12 documentaba tres primitivas, dos clases de lock y un orden de lock. El Capítulo 13 añade tres callouts. Las nuevas secciones a incorporar:

```markdown
## Callouts Owned by This Driver

### sc->heartbeat_co (callout(9), MYFIRST_CO_INIT)

Lock: sc->mtx (registered via callout_init_mtx).
Callback: myfirst_heartbeat.
Behaviour: periodic; re-arms itself at the end of each firing if
  sc->heartbeat_interval_ms > 0.
Started by: the heartbeat sysctl handler (transition 0 -> non-zero).
Stopped by: the heartbeat sysctl handler (transition non-zero -> 0)
  via callout_stop, and by myfirst_detach via callout_drain.
Lifetime: initialised in attach via MYFIRST_CO_INIT; drained in detach
  via MYFIRST_CO_DRAIN.

### sc->watchdog_co (callout(9), MYFIRST_CO_INIT)

Lock: sc->mtx.
Callback: myfirst_watchdog.
Behaviour: periodic; emits a warning if cb_used has not changed and
  is non-zero between firings.
Started/stopped: via the watchdog sysctl handler and detach, parallel
  to the heartbeat.

### sc->tick_source_co (callout(9), MYFIRST_CO_INIT)

Lock: sc->mtx.
Callback: myfirst_tick_source.
Behaviour: periodic; injects a single byte into the cbuf each firing
  if there is room.
Started/stopped: via the tick_source sysctl handler and detach,
  parallel to the heartbeat.

## Callout Discipline

1. Every callout uses sc->mtx as its lock via callout_init_mtx.
2. Every callout callback asserts MYFIRST_ASSERT(sc) at entry.
3. Every callout callback checks !sc->is_attached at entry and
   returns early without re-arming.
4. The detach path clears sc->is_attached under sc->mtx, broadcasts
   both cvs, drops the mutex, and then calls callout_drain on every
   callout.
5. callout_stop is used to cancel pending callouts in normal driver
   operation (sysctl handlers); callout_drain is used at detach.
6. NEVER call selwakeup, uiomove, copyin, copyout, malloc(M_WAITOK),
   or any sleeping primitive from a callout callback. The mutex is
   held during the callback, and these calls would violate the
   sleep-with-mutex rule.

## History (extended)

- 0.7-timers (Chapter 13): added heartbeat, watchdog, and tick-source
  callouts; documented callout discipline; standardised callout
  detach pattern.
- 0.6-sync (Chapter 12, Stage 4): combined version with cv channels,
  bounded reads, sx-protected configuration, reset sysctl.
- ... (earlier history as before) ...
```

Añade esto al `LOCKING.md` existente en lugar de reemplazar el contenido actual. Las nuevas secciones se colocan junto a las ya existentes de "Locks Owned by This Driver", "Lock Order", "Locking Discipline" y demás.

### Incrementando la versión

Actualiza la cadena de versión:

```c
#define MYFIRST_VERSION "0.7-timers"
```

Actualiza la entrada del registro de cambios:

```markdown
## 0.7-timers (Chapter 13)

- Added struct callout heartbeat_co, watchdog_co, tick_source_co
  to the softc.
- Added sysctls dev.myfirst.<unit>.heartbeat_interval_ms,
  watchdog_interval_ms, tick_source_interval_ms.
- Added callbacks myfirst_heartbeat, myfirst_watchdog,
  myfirst_tick_source, each lock-aware via callout_init_mtx.
- Updated detach to drain every callout under the documented
  seven-phase pattern.
- Added MYFIRST_CO_INIT and MYFIRST_CO_DRAIN macros for callout
  init and teardown.
- Updated LOCKING.md with a Callouts section and callout
  discipline rules.
- Updated regression script to include callout tests.
```

### Actualizando el README

Dos nuevas funcionalidades en el README:

```markdown
## Features (additions)

- Callout-based heartbeat that periodically logs cbuf usage and
  byte counts.
- Callout-based watchdog that detects stalled buffer drainage.
- Callout-based tick source that injects synthetic data for testing.

## Configuration (additions)

- dev.myfirst.<unit>.heartbeat_interval_ms: periodic heartbeat
  in milliseconds (0 = disabled).
- dev.myfirst.<unit>.watchdog_interval_ms: watchdog interval in
  milliseconds (0 = disabled).
- dev.myfirst.<unit>.tick_source_interval_ms: tick-source interval
  in milliseconds (0 = disabled).
```

### Ejecutando el análisis estático

Ejecuta `clang --analyze` sobre el nuevo código. Los flags exactos dependen de tu configuración del kernel; la misma receta que usó la sección de regresión del Capítulo 11 sigue funcionando, con el conocimiento añadido de que las macros de inicialización de callouts se expanden en llamadas a funciones que `clang` ahora puede analizar:

```sh
$ make WARNS=6 clean all
$ clang --analyze -D_KERNEL -DKLD_MODULE \
    -I/usr/src/sys -I/usr/src/sys/contrib/ck/include \
    -fno-builtin -nostdinc myfirst.c
```

Clasifica la salida como antes. Pueden aparecer algunos falsos positivos alrededor de las macros de inicialización de callouts (el analizador no siempre rastrea la asociación de lock embebida en `_callout_init_lock`); documenta cada uno para que el próximo mantenedor no tenga que volver a clasificarlos.

### Ejecutando la suite de regresión

El script de regresión del Capítulo 12 se extiende de forma natural. Antes del script, merece la pena señalar dos aspectos de diseño: cada subprueba vacía el buffer de mensajes del kernel con `dmesg -c` para que `grep -c` solo cuente las líneas producidas *durante* esa subprueba; y las lecturas usan `dd` con un `count=` fijo en lugar de `cat`, de modo que un buffer inesperadamente vacío no pueda hacer que el script se quede colgado.

```sh
#!/bin/sh
# regression.sh: full Chapter 13 regression.

set -eu

die() { echo "FAIL: $*" >&2; exit 1; }
ok()  { echo "PASS: $*"; }

[ $(id -u) -eq 0 ] || die "must run as root"
kldstat | grep -q myfirst && kldunload myfirst
[ -f ./myfirst.ko ] || die "myfirst.ko not built; run make first"

# Clear any stale dmesg contents so per-subtest greps are scoped.
dmesg -c >/dev/null

kldload ./myfirst.ko
trap 'kldunload myfirst 2>/dev/null || true' EXIT

sleep 1
[ -c /dev/myfirst ] || die "device node not created"
ok "load"

# Chapter 7-12 tests (abbreviated; see prior chapters' scripts).
printf 'hello' > /dev/myfirst || die "write failed"
# dd with bs and count avoids blocking if the buffer is shorter
# than expected; if the read returns short, the test still proceeds.
ROUND=$(dd if=/dev/myfirst bs=5 count=1 2>/dev/null)
[ "$ROUND" = "hello" ] || die "round-trip mismatch (got '$ROUND')"
ok "round-trip"

# Chapter 13-specific tests. Each subtest clears dmesg first so the
# subsequent grep counts only the lines produced during that test.

# Heartbeat enable/disable.
dmesg -c >/dev/null
sysctl -w dev.myfirst.0.heartbeat_interval_ms=100 >/dev/null
sleep 1
HB_LINES=$(dmesg | grep -c "heartbeat:" || true)
[ "$HB_LINES" -ge 5 ] || die "expected >=5 heartbeat lines, got $HB_LINES"
sysctl -w dev.myfirst.0.heartbeat_interval_ms=0 >/dev/null
ok "heartbeat enable/disable"

# Watchdog: enable, write, wait, expect warning, then drain via dd
# (not cat, which would block once the 7 bytes are gone).
dmesg -c >/dev/null
sysctl -w dev.myfirst.0.watchdog_interval_ms=500 >/dev/null
printf 'wd_test' > /dev/myfirst
sleep 2
WD_LINES=$(dmesg | grep -c "watchdog:" || true)
[ "$WD_LINES" -ge 1 ] || die "expected >=1 watchdog line, got $WD_LINES"
sysctl -w dev.myfirst.0.watchdog_interval_ms=0 >/dev/null
dd if=/dev/myfirst bs=7 count=1 of=/dev/null 2>/dev/null  # drain
ok "watchdog warns on stuck buffer"

# Tick source: enable, read, expect synthetic bytes.
dmesg -c >/dev/null
sysctl -w dev.myfirst.0.tick_source_interval_ms=50 >/dev/null
TS_BYTES=$(dd if=/dev/myfirst bs=1 count=10 2>/dev/null | wc -c | tr -d ' ')
[ "$TS_BYTES" -eq 10 ] || die "expected 10 tick bytes, got $TS_BYTES"
sysctl -w dev.myfirst.0.tick_source_interval_ms=0 >/dev/null
ok "tick source produces bytes"

# Detach with callouts active. The trap will not fire after the
# explicit unload because the unload succeeds.
sysctl -w dev.myfirst.0.heartbeat_interval_ms=100 >/dev/null
sysctl -w dev.myfirst.0.tick_source_interval_ms=100 >/dev/null
sleep 1  # allow each callout to fire at least a few times
dmesg -c >/dev/null
kldunload myfirst
trap - EXIT  # the driver is now unloaded
ok "detach with active callouts"

# WITNESS check. Confined to events since the unload above.
WITNESS_HITS=$(dmesg | grep -ci "witness\|lor" || true)
if [ "$WITNESS_HITS" -gt 0 ]; then
    die "WITNESS warnings detected ($WITNESS_HITS lines)"
fi
ok "witness clean"

echo "ALL TESTS PASSED"
```

Algunas notas sobre portabilidad y robustez.

Las llamadas a `dmesg -c` vacían el buffer de mensajes del kernel entre subpruebas; en FreeBSD, `dmesg -c` está documentado para vaciar el buffer tras imprimirlo. Sin ellas, una prueba que se ejecute después de la subprueba del heartbeat podría ver líneas del heartbeat de ejecuciones anteriores y hacer un recuento incorrecto.

Se usa `dd` en lugar de `cat` para las lecturas de ida y vuelta y de drenado del watchdog. `cat` bloquea hasta EOF, que un dispositivo de caracteres nunca devuelve; `dd` termina después de haber leído `count=` bloques. El driver bloquea por defecto, por lo que un `cat` sobre un buffer vacío simplemente se quedaría colgado y rompería el script.

El paso de detach no vuelve a llamar a `kldload` al final, porque la única prueba posterior (`witness clean`) no necesita el driver cargado. El `trap` se limpia tras la descarga exitosa para que EXIT no intente descargar un módulo ya descargado.

Una ejecución en verde después de cada commit es el mínimo. Una ejecución en verde en un kernel con `WITNESS` tras el estrés compuesto de larga duración (el del Capítulo 12 más los callouts del Capítulo 13 activos) es el listón más alto.

### Lista de verificación previa al commit

La lista del Capítulo 12 recibe tres nuevos elementos para el Capítulo 13:

1. ¿He actualizado `LOCKING.md` con los nuevos callouts, intervalos o cambios en el detach?
2. ¿He ejecutado la suite de regresión completa en un kernel con `WITNESS`?
3. ¿He ejecutado el estrés compuesto de larga duración durante al menos 30 minutos con todos los temporizadores habilitados?
4. ¿He ejecutado `clang --analyze` y clasificado cada nuevo aviso?
5. ¿He añadido `MYFIRST_ASSERT(sc)` y `if (!sc->is_attached) return;` a cada nuevo callback de callout?
6. ¿He incrementado la cadena de versión y actualizado `CHANGELOG.md`?
7. ¿He verificado que el kit de pruebas compila y se ejecuta?
8. ¿He comprobado que cada cv tiene tanto un señalizador como una condición documentada?
9. ¿He comprobado que cada `sx_xlock` tiene un `sx_xunlock` emparejado en cada ruta de código?
10. **(Nuevo)** ¿He añadido un `MYFIRST_CO_DRAIN` para cada nuevo callout en la ruta del detach?
11. **(Nuevo)** ¿He confirmado que ningún callback de callout llama a `selwakeup`, `uiomove` ni a ninguna primitiva que pueda dormir?
12. **(Nuevo)** ¿He verificado que deshabilitar un callout mediante su sysctl realmente detiene el disparo periódico?

Los nuevos elementos capturan los errores más comunes del Capítulo 13. Un callout que se inicializa pero nunca se drena es un crash en `kldunload` esperando a ocurrir. Un callback que llama a una función que puede dormir es un aviso de `WITNESS` esperando a ocurrir. Un sysctl que no consigue detener un callout es una experiencia de usuario confusa.

### Una nota sobre la compatibilidad hacia atrás

Una preocupación razonable: el driver del Capítulo 13 añade tres nuevos sysctls. ¿Se romperán los scripts existentes que interactúan con `myfirst` (quizás el kit de estrés del Capítulo 12)?

La respuesta es no, por dos razones.

En primer lugar, los nuevos sysctls tienen todos el valor por defecto de deshabilitados (intervalo = 0). El comportamiento del driver no cambia a menos que el usuario habilite alguno de ellos.

En segundo lugar, los sysctls del Capítulo 12 (`debug_level`, `soft_byte_limit`, `nickname`, `read_timeout_ms`, `write_timeout_ms`) y las estadísticas (`cb_used`, `cb_free`, `bytes_read`, `bytes_written`) no han cambiado. Los scripts existentes leen y escriben los mismos valores. Las adiciones del Capítulo 13 son puramente aditivas.

Esta es la disciplina de los *cambios no disruptivos*: cuando añades una funcionalidad, no cambies el significado de las funcionalidades existentes. El coste es pequeño (pensar en el cambio antes de realizarlo); el beneficio es que los usuarios existentes no observan ninguna regresión.

Para el Capítulo 13, el heartbeat, el watchdog y la fuente de ticks son todos opcionales. Un usuario que no conoce el Capítulo 13 ve el mismo driver que antes. Un usuario que lee el capítulo y habilita uno de los temporizadores obtiene el nuevo comportamiento. Ambos grupos quedan satisfechos.

### Una nota sobre el nombrado de sysctls

El capítulo utiliza nombres de sysctl como `dev.myfirst.0.heartbeat_interval_ms`. El sufijo `_ms` es intencional: documenta la unidad. Un usuario que vea `heartbeat_interval` podría razonablemente suponer segundos, milisegundos o microsegundos; el sufijo elimina esa ambigüedad.

Otras convenciones:

- `_count` para contadores (siempre no negativos).
- `_max`, `_min` para límites.
- `_threshold` para conmutadores.
- `_ratio` para porcentajes o fracciones.

Seguir estas convenciones hace que el árbol de sysctl sea autodescriptivo. Un usuario que inspeccione `sysctl dev.myfirst.0` puede deducir el significado de cada entrada a partir de su nombre y su unidad.

### Cerrando la sección 8

El driver es ahora la versión `0.7-timers`. Incluye:

- Una disciplina de callout documentada en `LOCKING.md`.
- Un par de macros estandarizadas (`MYFIRST_CO_INIT`, `MYFIRST_CO_DRAIN`) para el ciclo de vida del callout.
- Un patrón de detach en siete fases documentado en los comentarios del código.
- Un script de regresión que ejercita cada callout.
- Una lista de comprobación previa al commit que detecta los modos de fallo específicos del Capítulo 13.
- Tres nuevos sysctls con nombres autodescriptivos y una postura desactivada por defecto.

Eso cierra el arco principal de enseñanza del capítulo. A continuación vienen los laboratorios y los ejercicios de desafío.



## Laboratorios prácticos

Estos laboratorios consolidan los conceptos del Capítulo 13 mediante experiencia práctica directa. Están ordenados de menor a mayor dificultad.

### Lista de comprobación previa al laboratorio

Antes de comenzar cualquier laboratorio, confirma lo siguiente:

1. **Kernel de depuración en ejecución.** `sysctl kern.ident` reporta el kernel con `INVARIANTS` y `WITNESS`.
2. **WITNESS activo.** `sysctl debug.witness.watch` devuelve un valor distinto de cero.
3. **El código fuente del driver coincide con la Etapa 4 del Capítulo 12.** Los ejemplos del Capítulo 13 parten de esta base.
4. **Un dmesg limpio.** Ejecuta `dmesg -c >/dev/null` una vez antes del primer laboratorio.
5. **El userland de acompañamiento compilado.** Desde `examples/part-03/ch12-synchronization-mechanisms/userland/`, los probadores de timeout/config deben estar presentes.
6. **Copia de seguridad de la Etapa 4.** Copia el driver de la Etapa 4 del Capítulo 12 en un lugar seguro antes de comenzar cualquier laboratorio que modifique el código fuente.

### Laboratorio 13.1: Añadir un callout de latido

**Objetivo.** Convierte tu driver de la Etapa 4 del Capítulo 12 en un driver de la Etapa 1 del Capítulo 13 añadiendo el callout de latido.

**Pasos.**

1. Copia tu driver de la Etapa 4 en `examples/part-03/ch13-timers-and-delayed-work/stage1-heartbeat/`.
2. Añade `struct callout heartbeat_co` e `int heartbeat_interval_ms` a `struct myfirst_softc`.
3. En `myfirst_attach`, llama a `callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)` e inicializa `heartbeat_interval_ms = 0`.
4. En `myfirst_detach`, suelta el mutex y añade `callout_drain(&sc->heartbeat_co);` antes de destruir los primitivos.
5. Implementa el callback `myfirst_heartbeat` tal como se muestra en la Sección 4.
6. Implementa `myfirst_sysctl_heartbeat_interval_ms` y regístralo.
7. Compila y carga en un kernel con `WITNESS`.
8. Verifica configurando el sysctl: `sysctl -w dev.myfirst.0.heartbeat_interval_ms=1000` y observando `dmesg` en busca de líneas de latido.

**Verificación.** Las líneas de latido aparecen una vez por segundo cuando está activado. Se detienen cuando el sysctl se establece en 0. El detach tiene éxito incluso con el latido activado. No hay advertencias de `WITNESS`.

**Objetivo adicional.** Usa `dtrace` para contar las llamadas al callback de latido por segundo:

```sh
# dtrace -n 'fbt::myfirst_heartbeat:entry { @ = count(); } tick-1sec { printa(@); trunc(@); }'
```

El conteo debe coincidir con el intervalo configurado (1 por segundo para 1000 ms).

### Laboratorio 13.2: Añadir un callout de watchdog

**Objetivo.** Añade el callout de watchdog que detecta el drenaje estancado del buffer.

**Pasos.**

1. Copia el Laboratorio 13.1 en `stage2-watchdog/`.
2. Añade `struct callout watchdog_co`, `int watchdog_interval_ms`, `size_t watchdog_last_used` al softc.
3. Inicializa y drena en attach/detach igual que para el latido.
4. Implementa `myfirst_watchdog` y el manejador de sysctl correspondiente de la Sección 7.
5. Compila y carga.
6. Prueba: activa un watchdog de 1 segundo, escribe algunos bytes, no los drenes, observa la advertencia.

**Verificación.** La advertencia del watchdog aparece cada segundo mientras el buffer tiene bytes sin consumir. La advertencia se detiene cuando el buffer se drena.

**Objetivo adicional.** Haz que el watchdog registre el tiempo transcurrido desde el último cambio en el mensaje de advertencia: "sin progreso durante X.Y segundos".

### Laboratorio 13.3: Añadir una fuente de ticks

**Objetivo.** Añade el callout de fuente de ticks que inyecta bytes sintéticos en el cbuf.

**Pasos.**

1. Copia el Laboratorio 13.2 en `stage3-tick-source/`.
2. Añade `struct callout tick_source_co`, `int tick_source_interval_ms`, `char tick_source_byte` al softc.
3. Inicializa y drena como antes.
4. Implementa `myfirst_tick_source` tal como se muestra en la Sección 7. Observa la omisión deliberada de `selwakeup` del callback.
5. Implementa el manejador de sysctl.
6. Compila y carga.
7. Activa una fuente de ticks de 100 ms, lee con `cat`, observa los bytes sintéticos.

**Verificación.** `cat /dev/myfirst` produce aproximadamente 10 bytes por segundo del byte de tick configurado (por defecto `'t'`).

**Objetivo adicional.** Añade un sysctl que permita al usuario cambiar el byte de tick en tiempo de ejecución. Verifica que el cambio tenga efecto inmediatamente en la siguiente activación.

### Laboratorio 13.4: Verificar el detach con callouts activos

**Objetivo.** Confirma que el detach funciona correctamente incluso cuando los tres callouts están en ejecución.

**Pasos.**

1. Carga el driver de la Etapa 3 (fuente de ticks).
2. Activa los tres callouts:
   ```sh
   sysctl -w dev.myfirst.0.heartbeat_interval_ms=500
   sysctl -w dev.myfirst.0.watchdog_interval_ms=500
   sysctl -w dev.myfirst.0.tick_source_interval_ms=100
   ```
3. Confirma la actividad en `dmesg`.
4. Ejecuta `kldunload myfirst`.
5. Verifica que no hay pánico, ni advertencia de `WITNESS`, ni bloqueo.

**Verificación.** La descarga se completa en unos pocos cientos de milisegundos. `dmesg` no muestra advertencias relacionadas con la descarga.

**Objetivo adicional.** Mide el tiempo de descarga con `time kldunload myfirst`. El drenaje debería ser el contribuidor dominante al tiempo; espera unos pocos cientos de milisegundos dependiendo de los intervalos.

### Laboratorio 13.5: Construir un temporizador antirrebote

**Objetivo.** Implementa una forma de debounce (no utilizada por `myfirst`, pero un ejercicio útil).

**Pasos.**

1. Crea un directorio de trabajo para un driver experimental.
2. Implementa un sysctl `dev.myfirst.0.event_count` que se incrementa en 1 cada vez que se escribe. (La escritura del usuario desencadena el "evento".)
3. Añade un callout antirrebote que se activa 100 ms después del evento más reciente e imprime el conteo total de eventos detectados durante la ventana.
4. Prueba: escribe el sysctl rápidamente cinco veces. Observa que aparece una sola línea de registro 100 ms después de la última escritura, indicando el conteo.

**Verificación.** Múltiples eventos rápidos producen una sola línea de registro, con un conteo igual al número de eventos.

### Laboratorio 13.6: Detectar una condición de carrera deliberada

**Objetivo.** Introduce un error deliberado (un callback de callout que llama a algo que puede bloquearse) y observa cómo `WITNESS` lo detecta.

**Pasos.**

1. En un directorio de trabajo, modifica el callback de latido para llamar a algo que pueda bloquearse, como `pause("test", hz / 100)`.
2. Compila y carga en un kernel con `WITNESS`.
3. Activa el latido con un intervalo de 1 segundo.
4. Observa `dmesg` en busca de la advertencia: "Sleeping on \"test\" with the following non-sleepable locks held: ..." o similar.
5. Revierte el cambio.

**Verificación.** `WITNESS` produce una advertencia que nombra la operación de bloqueo y el mutex retenido. La advertencia incluye la línea de código fuente.

### Laboratorio 13.7: Prueba de estrés compuesta de larga duración con temporizadores

**Objetivo.** Ejecuta el kit de estrés compuesto del Capítulo 12 durante 30 minutos con los nuevos callouts del Capítulo 13 activados.

**Pasos.**

1. Carga el driver de la Etapa 4.
2. Activa los tres callouts con intervalos de 100 ms.
3. Ejecuta el script de estrés compuesto del Capítulo 12 durante 30 minutos.
4. Tras la finalización, comprueba:
   - `dmesg | grep -ci witness` devuelve 0.
   - Todas las iteraciones del bucle se completaron.
   - `vmstat -m | grep cbuf` muestra la asignación estática esperada.

**Verificación.** Todos los criterios se cumplen; sin advertencias, sin pánicos, sin crecimiento de memoria.

### Laboratorio 13.8: Perfilar la actividad de callout con dtrace

**Objetivo.** Usa dtrace para observar los patrones de activación de callouts.

**Pasos.**

1. Carga el driver de la Etapa 4.
2. Activa los tres callouts con intervalos de 100 ms.
3. Ejecuta un one-liner de dtrace para contar las activaciones de callout por callback por segundo:
   ```sh
   # dtrace -n '
   fbt::myfirst_heartbeat:entry,
   fbt::myfirst_watchdog:entry,
   fbt::myfirst_tick_source:entry { @[probefunc] = count(); }
   tick-1sec { printa(@); trunc(@); }'
   ```
4. Observa los conteos por segundo.

**Verificación.** Cada callback se activa aproximadamente 10 veces por segundo (1000 ms / 100 ms).

**Objetivo adicional.** Modifica el script de dtrace para reportar el tiempo empleado dentro de cada callback (usando `quantize` y `timestamp`).

### Laboratorio 13.9: Cancelar un watchdog en línea

**Objetivo.** Convierte el watchdog en un temporizador de disparo único que el camino de lectura cancela en caso de éxito, demostrando el patrón de cancelación en caso de progreso.

**Pasos.**

1. Copia el Laboratorio 13.4 (`stage3-tick-source` más heartbeat/watchdog) en un directorio de trabajo.
2. Modifica `myfirst_watchdog` para que sea de disparo único: no lo reapuntes al final.
3. Programa el watchdog desde `myfirst_write` después de cada escritura exitosa.
4. Cancela el watchdog (usando `callout_stop`) desde `myfirst_read` después de un drenaje exitoso.
5. Prueba: escribe algunos bytes; no los leas; observa que la advertencia del watchdog se activa una vez.
6. Prueba: escribe algunos bytes; léelos; observa que no hay advertencia (porque la lectura canceló el watchdog).

**Verificación.** La advertencia del watchdog se activa solo cuando el buffer queda sin drenar. Los drenajes exitosos cancelan el watchdog pendiente.

**Objetivo adicional.** Añade un contador que registre con qué frecuencia se activó el watchdog frente a con qué frecuencia fue cancelado. Exponlo como un sysctl. La ratio es una métrica de calidad para el drenaje del buffer.

### Laboratorio 13.10: Programar desde dentro de un manejador de sysctl

**Objetivo.** Verifica que programar un callout desde un manejador de sysctl produce una temporización correcta.

**Pasos.**

1. Añade un sysctl `dev.myfirst.0.schedule_oneshot_ms` al driver de la Etapa 4. Escribir N en él programa un callback de disparo único para que se active N milisegundos después.
2. El callback simplemente registra "one-shot fired".
3. Prueba: escribe 100 en el sysctl. Observa la línea de registro aproximadamente 100 ms después.
4. Prueba: escribe 1000 en el sysctl. Observa aproximadamente 1 segundo después.
5. Prueba: escribe 1 en el sysctl cinco veces en rápida sucesión. Observa cómo gestiona el kernel la reprogramación rápida.

**Verificación.** Cada escritura produce una línea de registro en aproximadamente el intervalo configurado. Las escrituras rápidas pueden programar nuevas activaciones (cancelando la anterior) o fusionarse; observa cuál ocurre.

**Objetivo adicional.** Usa `dtrace` para medir el delta entre la escritura del sysctl y la activación real. El histograma debería concentrarse alrededor del intervalo configurado.



## Ejercicios de desafío

Los desafíos amplían el Capítulo 13 más allá de los laboratorios base. Cada uno es opcional; cada uno está diseñado para profundizar tu comprensión.

### Desafío 1: Fuente de ticks por debajo del milisegundo

Modifica el callout de fuente de ticks para usar `callout_reset_sbt` con un intervalo por debajo del milisegundo (por ejemplo, 250 microsegundos). Pruébalo. ¿Qué ocurre con la salida del latido (que registra contadores)? ¿Qué muestra `lockstat` para el mutex de datos?

### Desafío 2: Watchdog con intervalos adaptativos

Haz que el watchdog reduzca su intervalo cada vez que se activa (señal de problema) y aumente su intervalo cuando detecte progreso. Limita ambos extremos a valores razonables.

### Desafío 3: Diferir el selwakeup a un taskqueue

La fuente de ticks omite `selwakeup` porque no puede llamarse desde el contexto del callout. Lee `taskqueue(9)` (el Capítulo 16 lo presentará en profundidad) y usa un taskqueue para diferir el `selwakeup` a un thread de trabajo. Verifica que los waiters de `poll(2)` ahora se despiertan correctamente.

### Desafío 4: Distribución de callouts en múltiples CPU

Por defecto, los callouts se ejecutan en una sola CPU. Usa `callout_reset_on` para vincular cada uno de los tres callouts a una CPU diferente. Usa `dtrace` para verificar el enlace. Analiza las ventajas e inconvenientes.

### Desafío 5: Limitar el intervalo máximo

Añade validación a cada sysctl de intervalo para imponer un mínimo (por ejemplo, 10 ms) y un máximo (por ejemplo, 60000 ms). Por debajo del mínimo, rechaza con `EINVAL`. Por encima del máximo, rechaza también. Documenta la decisión.

### Desafío 6: Timeout de lectura basado en callout

Reemplaza el timeout de lectura basado en `cv_timedwait_sig` del Capítulo 12 con un mecanismo basado en callout: programa un callout de disparo único cuando el lector empieza a bloquearse; el callout dispara `cv_signal` sobre el cv de datos para despertar al lector. Compara los dos enfoques.

### Desafío 7: Estadísticas rotativas

Añade un callout que tome una instantánea de `bytes_read` y `bytes_written` cada 5 segundos y almacene las tasas por intervalo en un buffer circular (separado del cbuf). Expón las tasas más recientes a través de un sysctl.

### Desafío 8: Drenar sin retener el lock

Verifica experimentalmente que llamar a `callout_drain` mientras se retiene el lock del callout produce un deadlock. Escribe una variante pequeña del driver que haga esto deliberadamente, observa el deadlock con DDB y documenta el síntoma.

### Desafío 9: Reutilizar una estructura de callout

Usa el mismo `struct callout` para dos callbacks diferentes en momentos distintos: programa con el callback A, espera a que se dispare y luego programa con el callback B. ¿Qué ocurre si A sigue pendiente cuando llamas a `callout_reset` con la función de B? Escribe una prueba para verificar el comportamiento del kernel.

### Desafío 10: Módulo hello-world basado en callout

Escribe un módulo mínimo (sin ningún `myfirst` de por medio) que no haga más que instalar un único callout que imprima "tick" cada segundo. Úsalo como prueba de sanidad del subsistema de callouts en tu máquina de pruebas.

### Desafío 11: Verificar la serialización del lock

Demuestra que dos callouts que comparten el mismo lock están serializados. Escribe un driver con dos callouts; haz que cada callback se detenga brevemente (con `DELAY()` si es necesario, aunque `DELAY()` no duerme sino que hace spinning). Confirma mediante `dtrace` que los callbacks nunca se solapan.

### Desafío 12: Latencia de coalescencia

Usa `callout_reset_sbt` con distintos valores de precisión (0, `SBT_1MS`, `SBT_1S`) para un temporizador de 1 segundo. Usa `dtrace` para medir los tiempos de disparo reales. ¿Cuánto agrupa el kernel cuando se le da más margen? ¿Cuándo reduce la agrupación el uso de CPU?

### Desafío 13: Inspección del callout wheel

El kernel expone el estado del callout wheel mediante los sysctls `kern.callout_stat` y `kern.callout_*`. Léelos en un sistema bajo carga. ¿Puedes identificar los callouts que tu driver ha programado?

### Desafío 14: Sustitución del puntero de función del callout

Programa un callout con una función. Antes de que se dispare, prográmalo de nuevo con una función diferente. ¿Qué ocurre? ¿La segunda función reemplaza a la primera? Documenta el comportamiento con un pequeño experimento.

### Desafío 15: Heartbeat adaptativo

Haz que el heartbeat se dispare más rápido cuando haya actividad reciente (escrituras en el último segundo) y más lento en reposo. El intervalo debe oscilar entre 100 ms (activo) y 5 segundos (inactivo). Pruébalo bajo una carga de trabajo intensiva para verificar que se adapta según lo esperado.



## Resolución de problemas

Esta referencia cataloga los errores que con más probabilidad encontrarás mientras trabajas en el Capítulo 13.

### Síntoma: El callout nunca se dispara

**Causa.** O el intervalo es cero (el callout estaba deshabilitado), o el manejador sysctl no llamó realmente a `callout_reset`.

**Solución.** Revisa la lógica del manejador sysctl. Confirma que se detecta la transición de 0 a un valor distinto de cero. Añade un `device_printf` en el punto de llamada para verificarlo.

### Síntoma: kldunload provoca un panic poco después

**Causa.** Un callout no fue drenado durante el detach. El callout se disparó después de que el módulo fuera descargado.

**Solución.** Añade `callout_drain` para cada callout en la ruta de detach. Confirma el orden: drenar *después* de limpiar `is_attached`, *antes* de destruir los primitivos.

### Síntoma: WITNESS advierte "sleeping thread (pid X) owns a non-sleepable lock"

**Causa.** Un callback de callout llamó a algo que duerme (uiomove, copyin, `malloc(M_WAITOK)`, pause, o cualquier variante de cv_wait) mientras mantenía el mutex adquirido por el kernel.

**Solución.** Elimina la operación de sleep del callback. Si el trabajo requiere dormir, defiere la tarea a un taskqueue o a un thread del kernel.

### Síntoma: El heartbeat se dispara una vez y luego nunca más

**Causa.** El código de rearme del callback falta o está protegido por una condición que se vuelve falsa.

**Solución.** Comprueba el rearme al final del callback. Confirma que `interval_ms > 0` y que la llamada a `callout_reset` se ejecuta realmente.

### Síntoma: El callout se dispara con más frecuencia que el intervalo configurado

**Causa.** Dos rutas están programando el mismo callout. O bien el manejador sysctl y el callback llaman ambos a `callout_reset`, o dos callbacks comparten una misma estructura callout.

**Solución.** Audita los puntos de llamada. El manejador sysctl solo debe llamar a `callout_reset` en la transición de 0 a un valor distinto de cero; el callback solo se rearma al final de su propia ejecución.

### Síntoma: El detach se bloquea indefinidamente

**Causa.** Un callback de callout se rearmó entre la asignación `is_attached = 0` y la llamada a `callout_drain`. El drenado espera ahora a que el callback termine; el callback, que comprobó `is_attached` antes de que la asignación surtiese efecto, no está terminando.

**Solución.** Confirma que la asignación `is_attached = 0` ocurre bajo el mismo lock que el del callout. Confirma que el drenado ocurre después de la asignación, no antes. La comprobación dentro del callback debe ver el flag a cero.

### Síntoma: WITNESS advierte sobre problemas de orden de locks con el lock del callout

**Causa.** El lock del callout está siendo adquirido en órdenes conflictivos por diferentes rutas.

**Solución.** El lock del callout es `sc->mtx`. Confirma que toda ruta que adquiere `sc->mtx` sigue el orden canónico (mtx primero, luego cualquier otro lock). El callback se ejecuta con `sc->mtx` ya adquirido; el callback no debe adquirir ningún lock que deba adquirirse antes que `sc->mtx`.

### Síntoma: Callout-Drain duerme indefinidamente

**Causa.** Se llamó a `callout_drain` con el lock del callout adquirido. Deadlock: el drenado espera a que el callback libere el lock, y el callback espera porque el drenado es quien mantiene el lock.

**Solución.** Libera el lock antes de llamar a `callout_drain`. El patrón estándar de detach hace precisamente esto.

### Síntoma: El callback se ejecuta pero los datos están obsoletos

**Causa.** El callback está usando valores en caché previos al disparo. O bien almacenó datos en una variable local que quedó obsoleta, o desreferenció una estructura que fue modificada.

**Solución.** El callback se ejecuta con el lock adquirido. Vuelve a leer los campos cada vez que el callback se dispara; no almacenes en caché entre disparos.

### Síntoma: `procstat -kk` no muestra ningún thread esperando en el callout

**Causa.** Los callouts no tienen threads asociados. El callback se ejecuta en el contexto de un thread del kernel (el subsistema de callouts gestiona un pequeño pool), pero ningún thread específico es "el thread del callout" de la manera en que un thread del kernel podría poseer una condición de espera.

**Solución.** No se necesita ninguna corrección; esto es por diseño. Para ver la actividad de los callouts, usa `dtrace` o `lockstat`.

### Síntoma: callout_reset devuelve 1 de forma inesperada

**Causa.** El callout estaba pendiente anteriormente y fue cancelado por esta llamada a `callout_reset`. El valor de retorno es informativo, no un error.

**Solución.** No se necesita ninguna corrección; esto es normal. Usa el valor de retorno si te importa saber si la programación anterior fue sobreescrita.

### Síntoma: El manejador sysctl devuelve EINVAL para una entrada válida

**Causa.** La validación del manejador rechaza el valor. Causa habitual: el usuario pasó un número negativo que la validación rechaza correctamente, o el manejador tiene un límite demasiado estricto.

**Solución.** Inspecciona el código de validación. Confirma que la entrada del usuario cumple las restricciones documentadas.

### Síntoma: Dos callbacks de callouts distintos se ejecutan de forma concurrente y provocan un deadlock

**Causa.** Ambos callouts están vinculados al mismo lock, por lo que no pueden ejecutarse de forma concurrente. Si parecen entrar en deadlock, comprueba si alguno de los callbacks adquiere otro lock que la otra ruta ya mantiene.

**Solución.** Audita el orden de adquisición de locks. El thread que ejecuta el callback mantiene `sc->mtx`; si intenta adquirir `sc->cfg_sx`, el orden debe ser mtx primero y luego sx (que es nuestro orden canónico).

### Síntoma: tick_source produce el byte incorrecto

**Causa.** El callback lee `tick_source_byte` en el momento del disparo. Si un sysctl acaba de modificarlo, el callback puede ver el valor antiguo o el nuevo dependiendo del momento.

**Solución.** Este es el comportamiento correcto; el cambio de byte surte efecto en el siguiente disparo. Si se requiere efecto inmediato, usa el patrón snapshot-and-apply del Capítulo 12.

### Síntoma: lockstat muestra el mutex de datos adquirido durante un tiempo inusualmente largo en los heartbeats

**Causa.** El callback de heartbeat está realizando demasiado trabajo mientras mantiene el lock.

**Solución.** El heartbeat solo realiza lecturas de contadores y escribe una línea de log; si el tiempo de retención es largo, probablemente sea culpa de `device_printf` (que adquiere locks globales para el buffer de mensajes). Para heartbeats con poca sobrecarga, condiciona la línea de log a un nivel de depuración.

### Síntoma: El heartbeat continúa después de poner el sysctl a 0

**Causa.** La llamada a `callout_stop` no canceló realmente el callout porque el callback ya estaba en ejecución. El callback se rearmó antes de comprobar el nuevo valor.

**Solución.** La condición de carrera se cierra si el manejador sysctl mantiene `sc->mtx` mientras actualiza `interval_ms` y llama a `callout_stop`. El callback se ejecuta bajo el mismo lock; no puede ejecutarse entre la actualización y la parada. Verifica que el lock se mantiene en los lugares correctos.

### Síntoma: WITNESS advierte sobre la adquisición del lock del callout durante la inicialización

**Causa.** Alguna ruta anterior en attach no ha establecido aún las reglas de orden de locks. Añadir la asociación del lock del callout hace que WITNESS detecte la inconsistencia.

**Solución.** Mueve `callout_init_mtx` a después de que el mutex esté inicializado. El orden debe ser: `mtx_init` primero, luego `callout_init_mtx`.

### Síntoma: Un único callout rápido provoca un alto uso de CPU

**Causa.** Un callout de 1 ms que realiza incluso una pequeña cantidad de trabajo se dispara 1000 veces por segundo. Si cada disparo tarda 100 microsegundos, eso supone el 10% de una CPU.

**Solución.** Aumenta el intervalo. Los intervalos inferiores a un segundo solo deben usarse cuando sea realmente necesario.

### Síntoma: dtrace no puede encontrar la función del callback

**Causa.** El proveedor `fbt` de dtrace necesita que la función esté presente en la tabla de símbolos del kernel. Si la función fue inlineada o eliminada por la optimización, la sonda no está disponible.

**Solución.** Confirma que la función no está declarada como `static inline` ni envuelta de forma que impida el enlace externo. La declaración estándar `static void myfirst_heartbeat(void *arg)` es correcta; dtrace puede sondearla.

### Síntoma: heartbeat_interval_ms devuelve 0 después de haberlo configurado

**Causa.** El manejador sysctl actualiza una copia local y nunca la confirma en el campo del softc, o el campo se sobreescribe en otro lugar.

**Solución.** Confirma que el manejador asigna `sc->heartbeat_interval_ms = new` tras la validación, antes de retornar.

### Síntoma: WITNESS advierte "callout_init: lock has sleepable lock_class"

**Causa.** Llamaste a `callout_init_mtx` con un lock `sx` u otro primitivo con capacidad de sleep en lugar de un mutex `MTX_DEF`. Los locks con capacidad de sleep están prohibidos como interlocks de callout porque los callouts se ejecutan en un contexto donde dormir es ilegal.

**Solución.** Usa `callout_init_mtx` con un mutex `MTX_DEF`, o `callout_init_rw` con un lock `rw(9)`, o `callout_init_rm` con un `rmlock(9)`. No uses `sx`, `lockmgr`, ni ningún otro lock con capacidad de sleep.

### Síntoma: El detach tarda segundos aunque los callouts parecen inactivos

**Causa.** Un callout tiene un intervalo largo (por ejemplo, 30 segundos) y está actualmente pendiente. `callout_drain` espera al siguiente disparo o a la cancelación explícita. Si el plazo está lejos en el futuro, la espera puede ser larga.

**Solución.** En realidad, `callout_drain` no espera al plazo; cancela el callout pendiente y retorna una vez que cualquier callback en curso termina. Si tu detach tarda segundos, hay otro problema (un callback está tardando realmente ese tiempo, o hay un sleep diferente implicado). Usa `dtrace` sobre `_callout_stop_safe` para investigar.

### Síntoma: `callout_pending` devuelve true después de `callout_stop`

**Causa.** Condición de carrera: otra ruta programó el callout entre la llamada a `callout_stop` y tu comprobación de `callout_pending`. O bien: el callout se estaba disparando en otra CPU y acaba de rearmarse.

**Solución.** Mantén siempre el lock del callout al llamar a `callout_stop` y comprobar `callout_pending`. El lock hace que las operaciones sean atómicas.

### Síntoma: Una función de callout aparece en `dmesg` mucho después de que el driver fuera descargado

**Causa.** La condición de carrera en la descarga. El callout se disparó después de que el detach destruyese el estado. Si el kernel no entró en panic inmediatamente, la línea impresa proviene del código del callback ya liberado, ejecutándose en un kernel que ha perdido el rastro del módulo original.

**Solución.** Esto no debería ocurrir si llamaste a `callout_drain` correctamente. Si ocurre, tu ruta de detach está rota; revisa cada callout para confirmar que todos son drenados.

### Síntoma: Múltiples callouts se disparan todos a la vez después de una larga pausa

**Causa.** El sistema estaba bajo carga (una interrupción de larga duración, un thread de procesamiento de callouts bloqueado) y no pudo atender el callout wheel. Al recuperarse, procesa todos los callouts diferidos en rápida sucesión.

**Solución.** Esto es normal bajo una carga inusual. Si ocurre de forma habitual, investiga por qué el sistema no pudo atender los callouts a tiempo. `dtrace -n 'callout-end'` (usando el proveedor `callout`, si tu kernel lo expone) muestra los tiempos de disparo reales.

### Síntoma: Un callout periódico se desfasa: cada disparo ocurre ligeramente más tarde que el anterior

**Causa.** El re-arm se realiza con `callout_reset(&co, hz, ..., ...)`, que programa "1 segundo a partir de ahora". El "ahora" de cada disparo es ligeramente posterior al plazo del disparo anterior, por lo que el intervalo real crece con el tiempo de ejecución del callback.

**Solución.** Para lograr una periodicidad exacta, calcula el siguiente plazo como "plazo anterior + intervalo", no como "ahora + intervalo". Usa `callout_reset_sbt` con `C_ABSOLUTE` y un sbintime absoluto calculado a partir de la planificación original.

### Síntoma: el callout nunca se dispara aunque `callout_pending` devuelve true

**Causa.** O bien el callout está bloqueado en una CPU que está fuera de línea (poco frecuente, pero posible durante el hot-unplug de CPU), o bien la interrupción de reloj del sistema no se está disparando en esa CPU.

**Solución.** Comprueba los sysctls `kern.hz` y `kern.eventtimer`. El valor por defecto hz=1000 debería producir disparos regulares. Si una CPU está fuera de línea, el subsistema de callout migra los callouts pendientes a una CPU operativa, pero existe una ventana de tiempo. Para la mayoría de los drivers, esto no es una preocupación real.

### Síntoma: la prueba de estrés provoca un pánico intermitente en `callout_process`

**Causa.** Casi con toda seguridad se trata de la condición de carrera en la descarga o de una asociación de lock incorrecta en un callout. El subsistema de callout está bien probado; los bugs en este nivel suelen estar en el código del driver.

**Solución.** Audita el init y el drain de cada callout. Comprueba que la asociación de lock es correcta (sin locks que permitan entrar en modo sleep). Ejecuta con `INVARIANTS` para detectar violaciones de invariantes.

### Síntoma: el contador `kern.callout.busy` crece bajo carga

**Causa.** El subsistema de callout ha detectado callbacks que tardan demasiado. Cada evento «busy» es un callback que no completó dentro de la ventana esperada.

**Solución.** Inspecciona los callbacks lentos con `dtrace`. Los callbacks largos indican o demasiado trabajo (divídelo en múltiples callouts o delégalo a un taskqueue) o un problema de contención de lock (el callback está esperando a que el lock esté disponible).

### Síntoma: los logs del driver muestran «callout_drain detected migration» o similar

**Causa.** Un callout estaba ligado a una CPU específica (mediante `callout_reset_on`) y la migración del binding se solapó con el drain. El kernel lo resuelve internamente; el mensaje de log es meramente informativo.

**Solución.** Normalmente no se requiere ninguna acción. Si el mensaje es frecuente, considera si el binding por CPU es realmente necesario.

### Síntoma: `callout_reset_sbt` produce un timing inesperado

**Causa.** El argumento `precision` es demasiado holgado: el kernel ha agrupado tu callout con otros en una ventana mucho más amplia de lo esperado.

**Solución.** Establece precision a un valor menor (o 0 para «disparar lo más cerca posible del plazo»). El valor por defecto es `tick_sbt` (un tick de margen), que es adecuado para la mayoría de los trabajos con temporizadores.

### Síntoma: un callout que funcionaba deja de dispararse tras un evento de gestión de energía

**Causa.** La interrupción de reloj del sistema puede haberse reconfigurado (transición entre modos de event-timer durante el ciclo de suspensión y activación). El subsistema de callout reprograma los callouts pendientes después de tales transiciones, pero el timing puede estar ligeramente desajustado.

**Solución.** Verifica con `dtrace` que se está invocando el callback del callout. Si no es así, el callout ha sido migrado o descartado; reprógramalo desde un punto del código que se sepa correcto.

### Síntoma: todos los callouts del driver se disparan en la misma CPU

**Causa.** Este es el comportamiento por defecto. Los callouts quedan ligados a la CPU que los programó; si todas tus llamadas a `callout_reset` se ejecutan en la CPU 0 (porque la syscall del usuario se despachó allí), todos los callouts se disparan en la CPU 0.

**Solución.** Esto es correcto para la mayoría de los drivers. Si deseas distribuir la carga, usa `callout_reset_on` para ligar explícitamente a distintas CPUs. La mayoría de los drivers no necesita esto; las ruedas por CPU se equilibran de forma natural con el tiempo a medida que distintas syscalls llegan a distintas CPUs.

### Síntoma: `callout_drain` retorna pero la siguiente syscall ve un estado obsoleto

**Causa.** El callback se completó y retornó, pero un camino de código posterior observó un estado que el callback había establecido. Este es el comportamiento correcto, no un bug.

**Solución.** Ninguna. El drain solo garantiza que el callback ya no está en ejecución; cualquier cambio de estado que el callback haya realizado sigue vigente. Si los cambios no son deseados, el callback no debería haberlos realizado.

### Síntoma: la reprogramación en el callback falla silenciosamente

**Causa.** La condición `interval > 0` es falsa porque el usuario acaba de deshabilitar el temporizador. El callback termina sin reprogramar el callout, que queda inactivo.

**Solución.** Este es el comportamiento correcto. Si quieres saber cuándo el callback decide no reprogramar, añade un contador o una línea de log.

### Síntoma: el callout se dispara pero `device_printf` no produce salida

**Causa.** El campo `dev` del driver es NULL o el dispositivo ha sido desconectado y el cdev destruido. `device_printf` puede suprimir la salida en esos estados.

**Solución.** Añade un `printf("%s: ...\n", device_get_nameunit(dev), ...)` explícito para saltarte el wrapper. O confirma que `sc->dev` es válido mediante `KASSERT`.



## Referencia: la progresión por etapas del driver

El capítulo 13 hace evolucionar el driver `myfirst` en cuatro etapas diferenciadas, cada una con su propio directorio bajo `examples/part-03/ch13-timers-and-delayed-work/`. La progresión refleja el hilo narrativo del capítulo; permite al lector construir el driver un temporizador a la vez y observar lo que cada adición aporta.

### Etapa 1: heartbeat

Añade el callout de heartbeat que registra periódicamente el uso del cbuf y el recuento de bytes. El nuevo sysctl `dev.myfirst.<unit>.heartbeat_interval_ms` habilita, deshabilita y ajusta el heartbeat en tiempo de ejecución.

Qué cambia: un nuevo callout, un nuevo callback, un nuevo sysctl y el correspondiente init/drain en attach/detach.

Qué puedes verificar: establecer el sysctl a un valor positivo produce líneas de log periódicas; establecerlo a 0 las detiene; el detach tiene éxito incluso con el heartbeat habilitado.

### Etapa 2: watchdog

Añade el callout de watchdog que detecta el drenaje de buffer estancado. El nuevo sysctl `dev.myfirst.<unit>.watchdog_interval_ms` habilita, deshabilita y ajusta el intervalo.

Qué cambia: un nuevo callout, un nuevo callback, un nuevo sysctl y el correspondiente init/drain.

Qué puedes verificar: habilitar el watchdog y escribir bytes (sin leerlos) produce líneas de aviso; leer el buffer detiene los avisos.

### Etapa 3: tick-source

Añade el callout de tick source que inyecta bytes sintéticos en el cbuf. El nuevo sysctl `dev.myfirst.<unit>.tick_source_interval_ms` habilita, deshabilita y ajusta el intervalo.

Qué cambia: un nuevo callout, un nuevo callback, un nuevo sysctl y el correspondiente init/drain.

Qué puedes verificar: habilitar el tick source y leer desde `/dev/myfirst` produce bytes a la tasa configurada.

### Etapa 4: final

El driver combinado con los tres callouts, más una extensión de `LOCKING.md`, la actualización de versión a `0.7-timers` y las macros estandarizadas `MYFIRST_CO_INIT` y `MYFIRST_CO_DRAIN`.

Qué cambia: la integración. No hay nuevas primitivas.

Qué puedes verificar: la suite de regresión pasa; la prueba de estrés de larga duración con todos los callouts activos se ejecuta sin problemas; `WITNESS` permanece en silencio.

Esta progresión de cuatro etapas es el driver canónico del capítulo 13. Los ejemplos complementarios reflejan las etapas exactamente para que el lector pueda compilar y cargar cualquiera de ellas.



## Referencia: anatomía de un watchdog real

Un watchdog de producción real hace más que el ejemplo del capítulo. Un breve recorrido por lo que los watchdogs reales suelen incluir, útil cuando escribes o lees código fuente de un driver.

### Seguimiento por solicitud

Los watchdogs de I/O reales rastrean cada solicitud pendiente de forma individual. El callback del watchdog recorre una lista de solicitudes pendientes, encuentra las que llevan demasiado tiempo sin resolver y actúa sobre cada una.

```c
struct myfirst_request {
        TAILQ_ENTRY(myfirst_request) link;
        sbintime_t   submitted_sbt;
        int           op;
        /* ... other request state ... */
};

TAILQ_HEAD(, myfirst_request) pending_requests;
```

El watchdog recorre `pending_requests`, calcula la antigüedad de cada una y actúa sobre las que han expirado.

### Acción basada en umbrales

Distintas antigüedades reciben distintas acciones. Hasta T1, se ignora (la solicitud sigue en curso). De T1 a T2, se registra un aviso. De T2 a T3, se intenta una recuperación suave (se envía un reset a la solicitud). Por encima de T3, recuperación forzada (reset del canal, fallo de la solicitud).

```c
age_sbt = now - req->submitted_sbt;
if (age_sbt > sc->watchdog_hard_sbt) {
        /* hard recovery */
} else if (age_sbt > sc->watchdog_soft_sbt) {
        /* soft recovery */
} else if (age_sbt > sc->watchdog_warn_sbt) {
        /* log warning */
}
```

### Estadísticas

Un watchdog real lleva la cuenta de con qué frecuencia se alcanza cada umbral, qué porcentaje de solicitudes supera cada umbral, etcétera. Las estadísticas se exponen como sysctls para su monitorización.

### Umbrales configurables

Cada umbral (T1, T2, T3) es un sysctl. Diferentes despliegues necesitan diferentes límites; codificarlos de forma fija es incorrecto.

### Registro de recuperación

La acción de recuperación se registra en dmesg con un prefijo reconocible que las herramientas de monitorización pueden buscar con grep. Un mensaje detallado con la identidad de la solicitud, la acción tomada y cualquier estado del kernel que pueda ayudar a diagnosticar el problema subyacente.

### Coordinación con otros subsistemas

Una recuperación forzada a menudo requiere la cooperación de otras partes del driver: la capa de I/O debe saber que el canal está siendo reseteado, las solicitudes en cola deben volver a encolarse o marcarse como fallidas, y el estado «operativo» del driver debe actualizarse.

Para el capítulo 13, nuestro watchdog es mucho más sencillo. Detecta una condición específica (ausencia de progreso en el cbuf), registra un aviso y se reprograma. Esto captura el patrón esencial. Los watchdogs del mundo real añaden los elementos anteriores de forma incremental.



## Referencia: arquitectura de driver periódica frente a orientada a eventos

Una pequeña digresión arquitectónica. Algunos drivers están dominados por eventos (llega una interrupción, el driver responde). Otros están dominados por el polling (el driver se activa periódicamente para comprobar el estado). Entender cuál es el caso de tu driver ayuda a elegir las primitivas adecuadas.

### Orientado a eventos

En un diseño orientado a eventos, el driver está mayormente inactivo. La actividad se desencadena por:

- Syscalls del usuario (`open`, `read`, `write`, `ioctl`).
- Interrupciones de hardware (capítulo 14).
- Activaciones desde otros subsistemas (señales cv, ejecuciones de taskqueue).

Los callouts en un diseño orientado a eventos son típicamente watchdogs (rastrean un evento y se disparan si no ocurre) y reapers (limpian tras los eventos).

El driver `myfirst` era originalmente orientado a eventos (read/write lo desencadenaba todo). El capítulo 13 añade algo de comportamiento de tipo polling (heartbeat, tick source) a modo de demostración, pero el diseño subyacente sigue siendo orientado a eventos.

### Basado en polling

En un diseño basado en polling, el driver se activa periódicamente para realizar trabajo, independientemente de si alguien lo solicita. Esto es adecuado para hardware que no genera interrupciones para los eventos que le interesan al driver.

Los callouts en un diseño basado en polling son el latido del driver: en cada disparo, el callback comprueba el hardware y procesa lo que encuentra.

El patrón de bucle de polling (sección 7) es la forma básica. Los drivers de polling reales lo extienden con intervalos adaptativos (polling más rápido cuando hay carga, más lento cuando está inactivo), recuento de errores (rendirse tras demasiados polls fallidos), etcétera.

### Híbrido

La mayoría de los drivers reales son híbridos: los eventos impulsan la mayor parte de la actividad, pero un callout periódico captura lo que los eventos pasan por alto (timeouts, polling lento, estadísticas). Los patrones de este capítulo son aplicables a ambos enfoques; la elección de cuál usar y dónde es una decisión de diseño.

Para `myfirst`, nuestro enfoque híbrido usa:

- Manejadores de syscall orientados a eventos para el I/O principal.
- Un callout de heartbeat para el registro periódico.
- Un callout de watchdog para detectar estados bloqueados.
- Un callout de tick source opcional para la generación de eventos sintéticos.

Un driver real tendría muchos más callouts, pero la forma es la misma.



## Cerrando

El capítulo 13 tomó el driver que construiste en el capítulo 12 y le dio la capacidad de actuar según su propia agenda. Tres callouts conviven ahora junto a las primitivas existentes: un heartbeat que registra el estado periódicamente, un watchdog que detecta el drenaje estancado y un tick source que inyecta bytes sintéticos. Cada uno es consciente del lock, se drena en el detach, es configurable a través de un sysctl y está documentado en `LOCKING.md`. La ruta de datos del driver no ha cambiado; el nuevo código es puramente aditivo.

Aprendimos que `callout(9)` es pequeño, regular y está bien integrado con el resto del kernel. El ciclo de vida es siempre el mismo en cinco etapas: init, schedule, fire, complete, drain. El contrato de lock es siempre el mismo modelo: el kernel adquiere el lock registrado antes de cada disparo y lo libera después, serializando el callback frente a cualquier otro poseedor. El patrón de detach es siempre el mismo en siete fases: rechazar si está ocupado, marcar el estado de cierre bajo el lock, soltar el lock, drenar selinfo, drenar cada callout, destruir los cdevs, liberar el estado, destruir las primitivas en orden inverso.

También aprendimos un pequeño conjunto de recetas que reaparecen en distintos drivers: heartbeat, watchdog, debounce, reintentos con backoff, reaper diferido, rotación de estadísticas. Cada una es una pequeña variación de la forma periódica o de disparo único; una vez que conoces los patrones, las variaciones son mecánicas.

Cuatro recordatorios finales antes de continuar.

La primera es *drenar cada callout en el detach*. La condición de carrera al descargar el módulo es fiable, inmediata y fatal. La solución es mecánica: un `callout_drain` por cada callout, después de que `is_attached` se borre y antes de que se destruyan las primitivas. No hay excusa para omitir esto.

La segunda es *mantener los callbacks cortos y conscientes del lock*. El callback se ejecuta con el lock registrado adquirido, en un contexto en el que no se puede dormir. Trátalo como un manejador de interrupciones de hardware: haz lo mínimo y aplaza el resto. Si el trabajo necesita dormir, encólalo en un taskqueue (capítulo 16) o despierta un kernel thread.

La tercera es *usar sysctls para que el comportamiento del temporizador sea configurable*. Codificar en duro los intervalos es una carga de mantenimiento. Permitir que los usuarios ajusten el heartbeat, el watchdog o la fuente de ticks mediante `sysctl -w` hace que el driver sea útil en entornos que no habías anticipado. El coste es pequeño (un manejador de sysctl por cada parámetro) y el beneficio es grande.

La cuarta es *actualizar `LOCKING.md` en el mismo commit que cualquier cambio de código*. Un driver cuya documentación se aleja del código acumula bugs sutiles porque nadie sabe cuáles se supone que son las reglas. La disciplina cuesta un minuto por cambio; el beneficio son años de mantenimiento limpio.

Estas cuatro disciplinas juntas producen drivers que se integran bien con el resto de FreeBSD, que sobreviven al mantenimiento a largo plazo y que se comportan de forma predecible bajo carga. Son también las disciplinas que el capítulo 14 dará por sentadas; los patrones de este capítulo se trasladan directamente a los manejadores de interrupciones.

### Qué deberías ser capaz de hacer ahora

Una breve lista de verificación antes de pasar al Capítulo 14:

- Elegir entre `callout(9)` y `cv_timedwait_sig` para cualquier requisito del tipo "esperar hasta que ocurra X, o hasta que haya transcurrido el tiempo Y".
- Inicializar un callout con la variante de lock adecuada para las necesidades de tu driver.
- Programar callouts de disparo único y periódicos con `callout_reset` (o `callout_reset_sbt` para precisión sub-tick).
- Cancelar callouts con `callout_stop` en operación normal; drenar con `callout_drain` durante el detach.
- Escribir un callback de callout que respete el contrato de lock y la regla de no dormir.
- Usar el patrón `is_attached` para que los callbacks sean seguros durante el desmontaje.
- Documentar cada callout en `LOCKING.md`, incluyendo su lock, su callback y su ciclo de vida.
- Reconocer la condición de carrera al descargar el módulo y evitarla mediante el patrón estándar de detach en siete fases.
- Construir los patrones watchdog, heartbeat, debounce y tick-source según sea necesario.
- Usar `dtrace` para verificar las tasas de disparo, la latencia y el comportamiento del ciclo de vida de los callouts.
- Leer código fuente de drivers reales (led, uart, drivers de red) y reconocer en él los patrones de este capítulo.

Si alguno de esos puntos te resulta inseguro, los laboratorios de este capítulo son el lugar para afianzar la práctica. Ninguno requiere más de una o dos horas; en conjunto cubren todos los primitivos y todos los patrones que el capítulo ha presentado.

### Una nota sobre los ejemplos de acompañamiento

El código fuente de acompañamiento en `examples/part-03/ch13-timers-and-delayed-work/` refleja las etapas del capítulo. Cada etapa se construye sobre la anterior, de modo que puedes compilar y cargar cualquier etapa para ver exactamente el estado del driver que el capítulo describe en ese punto.

Si prefieres escribir los cambios a mano (recomendado en la primera lectura), usa los ejemplos desarrollados en el capítulo como guía y el código fuente de acompañamiento como referencia. Si prefieres leer el código terminado, el código fuente de acompañamiento es la versión canónica.

Una nota sobre el documento `LOCKING.md`: el texto del capítulo explica qué debe contener `LOCKING.md`. El archivo real se encuentra en el árbol de ejemplos junto al código fuente. Mantén ambos sincronizados al hacer cambios; la disciplina de actualizar `LOCKING.md` en el mismo commit que el cambio de código es la forma más fiable de mantener la documentación precisa.



## Referencia: resumen rápido de `callout(9)`

Un resumen compacto de la API para consulta cotidiana.

### Inicialización

```c
callout_init(&co, 1)                       /* mpsafe; no lock */
callout_init_mtx(&co, &mtx, 0)             /* lock is mtx (default) */
callout_init_mtx(&co, &mtx, CALLOUT_RETURNUNLOCKED)
callout_init_rw(&co, &rw, 0)               /* lock is rw, exclusive */
callout_init_rw(&co, &rw, CALLOUT_SHAREDLOCK)
callout_init_rm(&co, &rm, 0)               /* lock is rmlock */
```

### Programación

```c
callout_reset(&co, ticks, fn, arg)         /* tick-based delay */
callout_reset_sbt(&co, sbt, prec, fn, arg, flags)
callout_reset_on(&co, ticks, fn, arg, cpu) /* bind to CPU */
callout_schedule(&co, ticks)               /* re-use last fn/arg */
```

### Cancelación

```c
callout_stop(&co)                          /* cancel; do not wait */
callout_drain(&co)                         /* cancel + wait for in-flight */
callout_async_drain(&co, drain_fn)         /* drain async */
```

### Inspección

```c
callout_pending(&co)                       /* is the callout scheduled? */
callout_active(&co)                        /* user-managed active flag */
callout_deactivate(&co)                    /* clear the active flag */
```

### Indicadores más habituales

```c
CALLOUT_RETURNUNLOCKED   /* callback releases the lock itself */
CALLOUT_SHAREDLOCK       /* acquire rw/rm in shared mode */
C_HARDCLOCK              /* align to hardclock() */
C_DIRECT_EXEC            /* run in timer interrupt context */
C_ABSOLUTE               /* sbt is absolute time */
```



## Referencia: patrón estándar de detach

El patrón de detach en siete fases para un driver con callouts:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* Phase 1: refuse if in use. */
        MYFIRST_LOCK(sc);
        if (sc->active_fhs > 0) {
                MYFIRST_UNLOCK(sc);
                return (EBUSY);
        }

        /* Phase 2: mark going away; broadcast cvs. */
        sc->is_attached = 0;
        cv_broadcast(&sc->data_cv);
        cv_broadcast(&sc->room_cv);
        MYFIRST_UNLOCK(sc);

        /* Phase 3: drain selinfo. */
        seldrain(&sc->rsel);
        seldrain(&sc->wsel);

        /* Phase 4: drain every callout (no lock held). */
        callout_drain(&sc->heartbeat_co);
        callout_drain(&sc->watchdog_co);
        callout_drain(&sc->tick_source_co);

        /* Phase 5: destroy cdevs (no new opens). */
        if (sc->cdev_alias != NULL) {
                destroy_dev(sc->cdev_alias);
                sc->cdev_alias = NULL;
        }
        if (sc->cdev != NULL) {
                destroy_dev(sc->cdev);
                sc->cdev = NULL;
        }

        /* Phase 6: free auxiliary resources. */
        sysctl_ctx_free(&sc->sysctl_ctx);
        cbuf_destroy(&sc->cb);
        counter_u64_free(sc->bytes_read);
        counter_u64_free(sc->bytes_written);

        /* Phase 7: destroy primitives in reverse order. */
        cv_destroy(&sc->data_cv);
        cv_destroy(&sc->room_cv);
        sx_destroy(&sc->cfg_sx);
        mtx_destroy(&sc->mtx);

        return (0);
}
```

Omitir cualquier fase genera una clase de error que provoca el pánico del kernel bajo carga.



## Referencia: cuándo usar cada primitivo de temporización

Una tabla de decisión compacta.

| Necesidad | Primitivo |
|---|---|
| Callback de disparo único en el tiempo T | `callout_reset` |
| Callback periódico cada T ticks | `callout_reset` con re-arm |
| Temporización de callback sub-milisegundo | `callout_reset_sbt` |
| Vincular el callback a una CPU concreta | `callout_reset_on` |
| Ejecutar el callback en contexto de interrupción de temporizador | `callout_reset_sbt` con `C_DIRECT_EXEC` |
| Esperar hasta una condición, con límite de tiempo | `cv_timedwait_sig` (Capítulo 12) |
| Esperar hasta una condición, sin límite de tiempo | `cv_wait_sig` (Capítulo 12) |
| Diferir trabajo a un worker thread | `taskqueue_enqueue` (Capítulo 16) |
| Trabajo periódico de larga duración | Kernel thread + `cv_timedwait` |

Los cuatro primeros son casos de uso de `callout(9)`; los demás utilizan otros primitivos.



## Referencia: errores comunes que hay que evitar con los callouts

Una lista compacta de los errores más frecuentes:

- **Olvidar `callout_drain` en el detach.** Provoca un pánico la próxima vez que el callout se dispara.
- **Llamar a `callout_drain` mientras se mantiene el lock del callout.** Provoca un deadlock.
- **Llamar a funciones que pueden dormir desde el callback.** Provoca una advertencia de `WITNESS` o un pánico.
- **Usar `callout_init` (con `mpsafe=0`) en código nuevo.** Adquiere Giant, lo que perjudica la escalabilidad.
- **Olvidar la comprobación `is_attached` al inicio del callback.** El detach puede entrar en condición de carrera con el re-arm y no completarse nunca.
- **Compartir un `struct callout` entre dos callbacks.** Es confuso y raramente es lo que se desea; utiliza dos structs.
- **Codificar intervalos fijos en el callback.** No deja margen a los usuarios para ajustar el comportamiento.
- **No validar la entrada del sysctl.** Los intervalos negativos o absurdos generan un comportamiento inesperado.
- **Llamar a `selwakeup` desde un callback.** Adquiere otros locks y puede provocar violaciones de orden.
- **Usar `callout_stop` en el detach.** No espera a los callbacks en vuelo y provoca la condición de carrera al descargar el módulo.



## Referencia: lectura de kern_timeout.c

Dos funciones de `/usr/src/sys/kern/kern_timeout.c` merecen abrirse al menos una vez.

`callout_reset_sbt_on` es la función central de programación. Todas las demás variantes de `callout_reset` son wrappers que acaban desembocando en ella. La función gestiona los casos de "el callout está ejecutándose en este momento", "el callout está pendiente y se está reprogramando", "el callout debe migrarse a una CPU diferente" y "el callout es nuevo". La complejidad es real; el comportamiento público es sencillo.

`_callout_stop_safe` es la función unificada de parada y posible drenado. Tanto `callout_stop` como `callout_drain` son macros que llaman a esta función con distintas banderas. La bandera `CS_DRAIN` es la que activa el comportamiento de espera a los callbacks en vuelo. Leer esta función una vez muestra exactamente cómo el drenado interactúa con el callback que se está disparando.

El archivo tiene alrededor de 1550 líneas en FreeBSD 14.3. No es necesario leerlo entero. Recorre los nombres de las funciones, localiza las dos funciones mencionadas y léelas con detenimiento. Veinte minutos de lectura son suficientes para hacerse una idea clara del funcionamiento de la implementación.



## Referencia: los campos c_iflags y c_flags

Un vistazo breve a los dos campos de banderas, útil cuando lees código fuente de drivers reales que los inspecciona directamente.

`c_iflags` (banderas internas, establecidas por el kernel):

- `CALLOUT_PENDING`: el callout está en la rueda esperando dispararse. Se consulta mediante `callout_pending(c)`.
- `CALLOUT_PROCESSED`: contabilidad interna sobre en qué lista se encuentra el callout.
- `CALLOUT_DIRECT`: activado si se usó `C_DIRECT_EXEC`.
- `CALLOUT_DFRMIGRATION`: activado durante la migración diferida a una CPU distinta.
- `CALLOUT_RETURNUNLOCKED`: activado si el contrato de gestión del lock está configurado para que el callback libere el lock.
- `CALLOUT_SHAREDLOCK`: activado si el lock rw/rm debe adquirirse en modo compartido.

`c_flags` (banderas externas, gestionadas por el llamador):

- `CALLOUT_ACTIVE`: un bit gestionado por el usuario. El kernel lo activa tras un `callout_reset` exitoso; se borra mediante `callout_deactivate` o tras un `callout_stop` exitoso. El código del driver lo consulta mediante `callout_active(c)`.
- `CALLOUT_LOCAL_ALLOC`: obsoleto; solo se usa en el estilo heredado de `timeout(9)`.
- `CALLOUT_MPSAFE`: obsoleto; usa `callout_init_mtx` en su lugar.

El código del driver solo toca `CALLOUT_ACTIVE` (mediante `callout_active` y `callout_deactivate`) y `CALLOUT_PENDING` (mediante `callout_pending`). Todo lo demás es interno.



## Referencia: autoevaluación del Capítulo 13

Antes de pasar al Capítulo 14, usa esta rúbrica. El formato refleja el del Capítulo 11 y el Capítulo 12: preguntas conceptuales, preguntas de lectura de código y preguntas prácticas. Si algún punto te resulta inseguro, el nombre de la sección correspondiente aparece entre paréntesis.

Las preguntas no son exhaustivas; son una muestra de las ideas centrales del capítulo. Un lector que pueda responderlas todas con seguridad está listo para el siguiente capítulo. Un lector que tenga dificultades con algún punto debe releer la sección correspondiente antes de continuar.

### Preguntas conceptuales

Estas preguntas recorren el vocabulario del Capítulo 13. Un lector que pueda responderlas todas sin volver al capítulo ha interiorizado el material.

1. **¿Por qué usar `callout(9)` en lugar de un kernel thread para trabajo periódico?** Los callouts son prácticamente gratuitos cuando están en reposo; los threads tienen una pila de 16 KB y una sobrecarga del planificador. Para trabajo periódico de corta duración, un callout es la respuesta correcta.

2. **¿Cuál es la diferencia entre `callout_stop` y `callout_drain`?** `callout_stop` cancela los pendientes y retorna de inmediato; `callout_drain` cancela los pendientes y espera a que cualquier callback en vuelo finalice. Usa `callout_stop` en operación normal y `callout_drain` en el detach.

3. **¿Qué hace el argumento `lock` de `callout_init_mtx`?** El kernel adquiere ese lock antes de cada disparo del callback y lo libera después. El callback se ejecuta con el lock tomado.

4. **¿Qué no puede hacer un callback de callout?** Nada que pueda dormir, lo que incluye `cv_wait`, `mtx_sleep`, `uiomove`, `copyin`, `copyout`, `malloc(M_WAITOK)` y `selwakeup`.

5. **¿Por qué el patrón estándar de detach llama a `callout_drain` tras soltar el mutex?** `callout_drain` puede dormir mientras espera al callback en vuelo. Dormir con el mutex tomado es ilegal. Soltar el mutex antes de drenar es obligatorio.

6. **¿Qué es la condición de carrera al descargar el módulo?** Un callout se dispara después de que `kldunload` ha finalizado, encontrando su función y el estado circundante ya liberados. El kernel salta a memoria no válida y entra en pánico.

7. **¿En qué consiste el patrón de re-arm del callback periódico?** El callback realiza su trabajo y llama a `callout_reset` al final para programar el siguiente disparo.

8. **¿Por qué el callback comprueba `is_attached` antes de hacer su trabajo?** El detach borra `is_attached`; si el callback se dispara durante la breve ventana entre el borrado y `callout_drain`, la comprobación impide que el callback realice trabajo que dependa de un estado que está siendo desmontado.

9. **¿Qué ocurre si llamas a `callout_drain` mientras mantienes el lock del callout?** Deadlock: el drenado espera a que el callback libere el lock, pero el callback no puede liberar el lock que el drenador mantiene tomado. Suelta siempre el lock antes de llamar a `callout_drain`.

10. **¿Cuál es el propósito de `MYFIRST_CO_INIT` y `MYFIRST_CO_DRAIN`?** Son envoltorios de macro alrededor de `callout_init_mtx` y `callout_drain` que documentan la convención: cada callout usa `sc->mtx` y se drena en el detach. Estandarizar mediante macros hace que añadir nuevos callouts sea mecánico y fácil de revisar.

11. **¿Por qué es seguro llamar a `device_printf` desde un callback de callout mientras se mantiene el mutex?** No adquiere ninguno de los locks del driver y no duerme; escribe en un ringbuffer global con su propio mecanismo de lock interno. Es una de las pocas funciones de salida seguras para llamar desde un contexto de callout.

12. **¿Cuál es la diferencia entre programar un callout para `hz` ticks y programarlo para `tick_sbt * hz`?** Conceptualmente ninguna; ambos representan un segundo. El primero usa `callout_reset` (API basada en ticks); el segundo usa `callout_reset_sbt` (API basada en sbintime). Elige la API que se ajuste a la precisión que necesitas.

### Preguntas de lectura de código

Abre el código fuente de tu driver del Capítulo 13 y verifica:

1. Que cada `callout_init_mtx` tiene su correspondiente `callout_drain` en el detach.
2. Que cada callback comienza con `MYFIRST_ASSERT(sc)` y `if (!sc->is_attached) return;`.
3. Que ningún callback llama a `selwakeup`, `uiomove`, `copyin`, `copyout`, `malloc(M_WAITOK)` ni `cv_wait`.
4. Que la ruta de detach suelta el mutex antes de llamar a `callout_drain`.
5. Que cada callout tiene un sysctl que permite al usuario habilitarlo, deshabilitarlo o cambiar su intervalo.
6. Que el ciclo de vida de cada callout (init en attach, drain en detach) está documentado en `LOCKING.md`.
7. Que cada callback periódico solo se vuelve a armar cuando su intervalo es positivo.
8. Que cada manejador de sysctl mantiene el mutex al llamar a `callout_reset` o `callout_stop`.

### Preguntas prácticas

Todas deberían ejecutarse rápidamente; si alguna falla, el laboratorio correspondiente de este capítulo explica paso a paso la configuración.

1. Carga el driver del Capítulo 13. Activa el heartbeat con un intervalo de 1 segundo. Confirma que `dmesg` muestra una línea de registro por segundo.

2. Activa el watchdog con un intervalo de 1 segundo. Escribe algunos bytes. Espera. Confirma que aparece la advertencia.

3. Activa el tick source con un intervalo de 100 ms. Lee con `cat`. Confirma que se producen 10 bytes por segundo.

4. Activa los tres callouts. Ejecuta `kldunload myfirst`. Verifica que no se produce ningún pánico ni advertencia.

5. Abre `/usr/src/sys/kern/kern_timeout.c`. Localiza `callout_reset_sbt_on`. Lee las primeras 50 líneas. ¿Puedes describir en dos frases qué hace?

6. Usa `dtrace` para confirmar que el heartbeat se activa a la frecuencia esperada. Con `heartbeat_interval_ms=200`, la frecuencia debería ser de 5 activaciones por segundo.

7. Modifica el watchdog para registrar información adicional (por ejemplo, cuántos callbacks se han activado desde el arranque). Comprueba que el nuevo campo aparece en dmesg.

8. Abre `/usr/src/sys/dev/led/led.c`. Localiza las llamadas a `callout_init_mtx`, `callout_reset` y `callout_drain`. Compáralas con los patrones de este capítulo. ¿Hay diferencias?

Si las ocho preguntas prácticas se superan y las preguntas conceptuales resultan sencillas, el trabajo del Capítulo 13 está consolidado. Ya estás listo para el Capítulo 14.

Una nota sobre el ritmo: la Parte 3 de este libro ha sido densa. Tres capítulos (11, 12, 13) sobre temas relacionados con la sincronización suponen mucho vocabulario nuevo que asimilar. Si los laboratorios de este capítulo te han resultado fáciles, esa es una buena señal. Si te han resultado difíciles, tómate uno o dos días antes de empezar el Capítulo 14; el material se asentará bien tras una pausa breve, y comenzar el Capítulo 14 con atención renovada es mejor que seguir adelante con el cansancio encima.

Una nota sobre las pruebas: el script de regresión de la Sección 8 cubre la funcionalidad básica. Para tener confianza a largo plazo, ejecuta el kit de estrés compuesto del Capítulo 12 con los tres callouts del Capítulo 13 activos, en un kernel con `WITNESS`, durante al menos 30 minutos. Una ejecución limpia es el listón que hay que superar antes de declarar el driver listo para producción. Cualquier resultado inferior arriesga la condición de carrera en la descarga o un problema sutil de orden de locks que la regresión básica no detecta.

Si la ejecución de estrés encuentra problemas, la referencia de resolución de problemas que aparece antes en este capítulo es la primera parada. La mayoría de los problemas se encuadran en uno de los patrones de síntomas descritos allí. Si el síntoma no coincide con nada de esa referencia, el siguiente paso es el depurador integrado en el kernel; las recetas DDB de la sección de resolución de problemas te sirven de punto de partida.

Cuando tengas una ejecución de estrés limpia y una revisión limpia, el trabajo del capítulo habrá terminado. El driver es ahora definitivo en la etapa 4, versión 0.7-timers, con una disciplina de callout documentada y un conjunto de regresión que demuestra que esa disciplina se mantiene bajo carga. Tómate un momento para apreciarlo. Luego, continúa.

## Referencia: Callouts en drivers reales de FreeBSD

Un breve recorrido por el uso de `callout(9)` en el código fuente real de FreeBSD. Los patrones que has aprendido en este capítulo se corresponden directamente con los patrones que emplean estos drivers.

### `/usr/src/sys/dev/led/led.c`

Un ejemplo sencillo pero instructivo. El driver `led(4)` permite que scripts en el espacio de usuario hagan parpadear LEDs en hardware que los soporta. El driver programa un callout cada `hz / 10` (100 ms) para avanzar por el patrón de parpadeo.

La llamada clave dentro de `led_timeout` y `led_state` es:

```c
callout_reset(&led_ch, hz / 10, led_timeout, p);
```

El callout se inicializa en `led_drvinit`:

```c
callout_init_mtx(&led_ch, &led_mtx, 0);
```

Un callout periódico, con conciencia del lock a través del mutex del driver. Exactamente el patrón que enseña este capítulo.

### `/usr/src/sys/dev/uart/uart_core.c`

El driver del puerto serie (UART) usa un callout en algunas configuraciones para hacer polling de la entrada en hardware que no genera interrupciones al recibir caracteres. El patrón es el mismo: `callout_init_mtx`, callback periódico y drain al desconectar.

### Watchdogs en drivers de red

La mayoría de los drivers de red (ixgbe, em, mlx5, etc.) instalan un callout de watchdog durante el attach. El watchdog se dispara cada pocos segundos, comprueba si el hardware ha generado alguna interrupción recientemente y, si no lo ha hecho, reinicia el chip. El callback es breve, con conciencia del lock, y delega al resto del driver cuando necesita hacer algo complejo (normalmente encolando una tarea en un taskqueue).

### Timeouts de E/S en drivers de almacenamiento

Los drivers de ATA, NVMe y SCSI usan callouts como timeouts de E/S. Cuando se envía una petición al hardware, el driver programa un callout para dentro de un tiempo acotado. Si la petición completa con normalidad, el driver cancela el callout. Si el callout se dispara, el driver asume que la petición está bloqueada y toma medidas de recuperación (reinicia el canal, reintenta o devuelve un error al usuario).

Este es el patrón watchdog (Sección 7) aplicado a operaciones individuales en lugar de al dispositivo en su conjunto.

### Polling del hub USB

El driver del hub USB consulta el estado del hub cada pocos cientos de milisegundos (configurable). El poll detecta dispositivos conectados o desconectados, cambios de estado de los puertos y completaciones de transferencia para las que el hub no genera interrupciones. El patrón es el de bucle de polling (Sección 7).

### Qué hacen estos drivers de forma diferente

Los drivers anteriores emplean primitivas adicionales que van más allá de lo que cubre el Capítulo 13, en especial los taskqueues. Muchos de ellos programan un callout que, en lugar de realizar el trabajo directamente, encola una tarea en un taskqueue. La tarea se ejecuta en contexto de proceso y puede dormir. El Capítulo 16 presenta este patrón con detalle.

En el Capítulo 13 mantenemos todo el trabajo dentro del callback del callout, con conciencia del lock y sin dormir. Los drivers reales extienden este patrón difiriendo el trabajo largo; la infraestructura de temporización subyacente (el callout) es la misma.



## Referencia: Comparación de callout con otras primitivas de temporización

Una comparación más detallada que la tabla de decisión presentada anteriormente en el capítulo.

### `callout(9)` vs `cv_timedwait_sig`

La primitiva equivalente en la capa de syscall es `cv_timedwait_sig` (Capítulo 12): "espera hasta que la condición X sea verdadera, pero abandona pasados T milisegundos". El llamador es el que espera; la variable de condición (cv) es la que recibe la señal.

Compáralo con `callout(9)`: un callback se ejecuta en el tiempo T, independientemente de si alguien lo está esperando. El callback es el que actúa; hace su propio trabajo y, posiblemente, señaliza a otros.

Los dos difieren en *quién espera* y *quién actúa*. En `cv_timedwait_sig`, el thread de syscall es a la vez el que espera y (tras el despertar) el que actúa. En `callout(9)`, el thread de syscall no interviene; un contexto independiente dispara el callback.

Usa `cv_timedwait_sig` cuando el thread de syscall tenga trabajo que hacer en cuanto termine la espera. Usa `callout(9)` cuando algo independiente deba ocurrir en un plazo determinado.

### `callout(9)` vs `taskqueue(9)`

`taskqueue(9)` ejecuta una función "lo antes posible" encolándola en un thread trabajador. No hay retardo temporal; el trabajo se ejecuta en cuanto el trabajador puede atenderlo.

Compáralo con `callout(9)`: la función se ejecuta en un momento específico del futuro.

Un patrón habitual es combinar ambos: un callout se dispara en el tiempo T, decide que hay trabajo que hacer y encola una tarea. La tarea se ejecuta en contexto de proceso y realiza el trabajo real (que puede incluir dormir). El Capítulo 16 cubrirá esta combinación.

### `callout(9)` vs threads del kernel

Un thread del kernel puede ejecutar un bucle y llamar a `cv_timedwait_sig` para producir un comportamiento periódico. El thread es pesado: 16 KB de pila, entrada en el planificador y asignación de prioridad.

Compáralo con `callout(9)`: no hay thread; el mecanismo de interrupciones del temporizador del kernel gestiona el disparo, y un pequeño pool de procesamiento de callouts ejecuta los callbacks.

Usa un thread del kernel cuando el trabajo sea genuinamente de larga duración (el thread trabajador espera, realiza un trabajo sustancial y vuelve a esperar). Usa un callout cuando el trabajo sea breve y simplemente necesites que se dispare con una periodicidad determinada.

### `callout(9)` vs `SIGALRM` periódico en el espacio de usuario

Un proceso en el espacio de usuario puede instalar un manejador de `SIGALRM` y usar `alarm(2)` para obtener un comportamiento periódico. El manejador de señal se ejecuta en el proceso; es breve y tiene restricciones.

Compáralo con `callout(9)`: del lado del kernel, con conciencia del lock e integrado con el resto del driver.

Las alarmas del espacio de usuario son adecuadas para código en el espacio de usuario. No tienen ningún papel en el trabajo del driver; el kernel gestiona sus propias cosas.

### `callout(9)` vs temporizadores de hardware

Algunos dispositivos tienen sus propios registros de temporizador (un "GP timer" o "watchdog timer" que el driver del host programa). Estos temporizadores de hardware disparan interrupciones directamente al host. Son rápidos, precisos y se saltan el subsistema de callouts del kernel.

Usa el temporizador de hardware cuando:
- El hardware proporcione uno y dispongas de un manejador de interrupciones.
- La precisión requerida supere lo que `callout_reset_sbt` puede ofrecer.

Usa `callout(9)` cuando:
- El hardware no tenga un temporizador aprovechable para tu propósito.
- La precisión que el kernel puede ofrecer (hasta `tick_sbt` o subintervalo de tick con `_sbt`) sea suficiente.

Para nuestro pseudodispositivo no existe hardware; `callout(9)` es la elección correcta y la única.



## Referencia: Vocabulario habitual de callout

Un glosario de los términos empleados en el capítulo, útil cuando los encuentres en el código fuente de un driver.

**Callout**: una instancia de `struct callout`; un temporizador programado o sin programar.

**Wheel**: el array de cubos de callouts por CPU que mantiene el kernel, organizado por plazo de disparo.

**Bucket**: un elemento del wheel que contiene una lista de callouts que deben dispararse en un intervalo de tiempo reducido.

**Pending**: estado en que el callout está en el wheel esperando dispararse.

**Active**: un bit gestionado por el usuario que indica "programé este callout y no lo he cancelado activamente"; no es lo mismo que pending.

**Firing**: estado en que el callback del callout se está ejecutando en ese momento.

**Idle**: estado en que el callout está inicializado pero no pending; bien nunca se programó, bien se disparó y no se rearmó.

**Drain**: la operación de esperar a que un callback en curso complete (normalmente en detach); `callout_drain`.

**Stop**: la operación de cancelar un callout pending sin esperar; `callout_stop`.

**Direct execution**: una optimización en la que el callback se ejecuta en el propio contexto de la interrupción del temporizador, activada con `C_DIRECT_EXEC`.

**Migration**: la reubicación por parte del kernel de un callout a otra CPU (normalmente porque la CPU a la que estaba vinculado ha dejado de estar disponible).

**Lock-aware**: un callout inicializado con una de las variantes de lock (`callout_init_mtx`, `_rw` o `_rm`); el kernel adquiere el lock en cada disparo.

**Mpsafe**: un término heredado que significa "puede invocarse sin adquirir Giant"; en el uso moderno aparece como el argumento `mpsafe` de `callout_init`.

**Re-arm**: la acción que realiza un callback para programar el siguiente disparo del mismo callout.



## Referencia: Antipatrones de temporizador que conviene evitar

Un breve catálogo de patrones que parecen razonables pero son incorrectos.

**Antipatrón 1: polling en un bucle cerrado.** Algunos drivers, en especial los escritos por principiantes, hacen busy-wait sobre un registro de hardware: `while (!(read_reg() & READY)) ; /* keep checking */`. Esto consume CPU y produce un sistema que deja de responder bajo carga. El patrón de polling basado en callouts es la solución: programa un callback que compruebe el registro y se rearme.

**Antipatrón 2: intervalos fijos en el código.** Un driver que codifica "esperar 100 ms" en todas partes es un driver difícil de ajustar. Convierte el intervalo en un sysctl o en un campo del softc que el usuario pueda modificar.

**Antipatrón 3: omitir el drain en el detach.** El error más frecuente del Capítulo 13. La condición de carrera durante la descarga provoca un crash del kernel. Siempre haz drain.

**Antipatrón 4: dormir en el callback.** El callback se ejecuta con un lock no durmiente adquirido; dormir está prohibido. Si el trabajo necesita dormir, delégalo a un thread del kernel o a un taskqueue.

**Antipatrón 5: usar `callout_init` (la variante heredada) en código nuevo.** La variante sin conciencia del lock exige que gestiones todo el locking dentro del callback, lo que es más propenso a errores que dejar que lo haga el kernel. Usa `callout_init_mtx` para código nuevo.

**Antipatrón 6: compartir un `struct callout` entre varios callbacks.** Un `struct callout` no es una cola. Si necesitas disparar dos callbacks distintos, usa dos `struct callout`.

**Antipatrón 7: llamar a `callout_drain` mientras se sostiene el lock del callout.** Provoca un deadlock. Suelta el lock primero.

**Antipatrón 8: usar el mismo lock como interbloqueo de callout de varios subsistemas sin relación entre sí.** La serialización puede generar una contención del lock inesperada. Cada subsistema debería tener generalmente su propio lock; compártelo solo cuando el trabajo esté genuinamente relacionado.

**Antipatrón 9: reutilizar un `struct callout` tras `callout_drain` sin reinicializarlo.** Después del drain, el estado interno del callout se reinicia, pero la función y el argumento del último `callout_reset` siguen ahí. Si llamas a `callout_schedule` a continuación, los reutilizas. Es algo sutil. Por claridad, llama de nuevo a `callout_init_mtx` antes de reutilizarlo.

**Antipatrón 10: olvidar que `callout_stop` no espera.** En operación normal esto es correcto; en el detach es incorrecto. Usa `callout_drain` para el detach.

Estos patrones se repiten con la suficiente frecuencia como para que valga la pena memorizarlos. Un driver que evite los diez lo tendrá mucho más fácil.



## Referencia: Trazado de callouts con dtrace

Una pequeña colección de recetas de `dtrace` útiles para inspeccionar el comportamiento de los callouts. Cada una ocupa una o dos líneas; juntas cubren la mayor parte de las necesidades de diagnóstico.

### Contar los disparos de un callback concreto

```sh
# dtrace -n 'fbt::myfirst_heartbeat:entry { @ = count(); } tick-1sec { printa(@); trunc(@); }'
```

Recuento por segundo de cuántas veces se ejecutó el callback de heartbeat. Útil para confirmar la frecuencia configurada.

### Histograma del tiempo empleado en el callback

```sh
# dtrace -n '
fbt::myfirst_heartbeat:entry { self->ts = timestamp; }
fbt::myfirst_heartbeat:return /self->ts/ {
    @ = quantize(timestamp - self->ts);
    self->ts = 0;
}
tick-30sec { exit(0); }'
```

Distribución de las duraciones del callback, en nanosegundos. Útil para detectar disparos inusualmente lentos.

### Trazar todos los resets de callout

```sh
# dtrace -n 'fbt::callout_reset_sbt_on:entry { printf("co=%p, fn=%p, arg=%p", arg0, arg3, arg4); }'
```

Cada llamada a `callout_reset` (y sus variantes). Útil para confirmar qué rutas de código están programando callouts.

### Trazar los drains de callout

```sh
# dtrace -n 'fbt::_callout_stop_safe:entry /arg1 == 1/ { printf("drain co=%p", arg0); stack(); }'
```

Cada llamada a la ruta de drain (`flags == CS_DRAIN`). Útil para confirmar que el detach realiza el drain de cada callout.

### Actividad de callout por CPU

```sh
# dtrace -n 'fbt::callout_process:entry { @[cpu] = count(); } tick-1sec { printa(@); trunc(@); }'
```

Recuento por segundo de las invocaciones de procesamiento de callouts en cada CPU. Indica qué CPUs están realizando el trabajo de temporización.

### Identificar callouts lentos

```sh
# dtrace -n '
fbt::callout_process:entry { self->ts = timestamp; }
fbt::callout_process:return /self->ts/ {
    @ = quantize(timestamp - self->ts);
    self->ts = 0;
}
tick-30sec { exit(0); }'
```

Distribución del tiempo que tarda el bucle de procesamiento de callouts. Las duraciones largas indican que muchos callouts se disparan al mismo tiempo o que algún callback individual es lento.

### Un script de diagnóstico combinado

Para lectura por segundo:

```sh
# dtrace -n '
fbt::callout_reset_sbt_on:entry { @resets = count(); }
fbt::_callout_stop_safe:entry /arg1 == 1/ { @drains = count(); }
fbt::myfirst_heartbeat:entry { @hb = count(); }
fbt::myfirst_watchdog:entry { @wd = count(); }
fbt::myfirst_tick_source:entry { @ts = count(); }
tick-1sec {
    printa("resets=%@u drains=%@u hb=%@u wd=%@u ts=%@u\n",
        @resets, @drains, @hb, @wd, @ts);
    trunc(@resets); trunc(@drains);
    trunc(@hb); trunc(@wd); trunc(@ts);
}'
```

Una línea de diagnóstico condensada por segundo. Útil como comprobación rápida durante el desarrollo.



## Referencia: Inspección del estado de callout desde DDB

Cuando el sistema se cuelga y necesitas inspeccionar el estado de los callouts desde el depurador, varios comandos de DDB son de utilidad.

### `show callout <addr>`

Si conoces la dirección de un callout, este comando muestra su estado actual: pending o no, plazo de disparo programado, puntero a la función callback y argumento. Útil cuando sabes qué callout quieres inspeccionar.

### `show callout_stat`

Muestra las estadísticas globales de callout: cuántos están programados, cuántos se han disparado desde el boot y cuántos están pending. Útil para una visión general del sistema.

### `ps`

El listado estándar de procesos. Los threads dentro del procesamiento de callout suelen denominarse `clock` o similar. Normalmente se encuentran en `mi_switch` o en el callback que se está ejecutando.

### `bt <thread>`

Backtrace de un thread específico. Si el thread está dentro de un callback de callout, el backtrace muestra la cadena de llamadas: el subsistema callout del kernel en la parte inferior y el callback en la parte superior. Esto te indica qué callback está en ejecución.

### `show all locks`

Si el callback de un callout está en ejecución en ese momento, el backtrace mostrará `mtx_lock` (el kernel adquiriendo el lock del callout). `show all locks` confirma qué lock está retenido y por qué thread.

### Combinado: inspección de un callout bloqueado

```text
db> show all locks
... shows myfirst0 mutex held by thread 1234

db> ps
... 1234 is "myfirst_heartbeat" (or similar)

db> bt 1234
... backtrace shows _cv_wait or similar; the callback is sleeping (which it should not!)
```

Si ves esto, el callback está realizando una operación ilegal (durmiendo con un lock no durmiente retenido). La solución es eliminar la operación que duerme del callback.



## Referencia: comparación de las API basadas en ticks y en SBT

Las dos API de callout (basada en ticks y basada en sbintime) merecen una comparación en paralelo.

### API basada en ticks

```c
callout_reset(&co, ticks, fn, arg);
callout_schedule(&co, ticks);
```

El retardo se expresa en ticks: un recuento entero de interrupciones de reloj. En un kernel a 1000 Hz, un tick equivale a un milisegundo. Para convertir, multiplica los segundos por `hz`; por ejemplo, `5 * hz` para cinco segundos y `hz / 10` para 100 ms.

Ventajas: sencilla, bien conocida y rápida (sin aritmética sbintime).
Inconvenientes: la precisión está limitada a un tick (1 ms típicamente) y no puede expresar retardos inferiores a un tick.

Cuándo usarla: para la mayor parte del trabajo con callouts. Watchdogs a intervalos de segundos, heartbeats a intervalos de centenares de milisegundos y polling periódico a intervalos de decenas de milisegundos.

### API basada en SBT

```c
callout_reset_sbt(&co, sbt, prec, fn, arg, flags);
callout_schedule_sbt(&co, sbt, prec, flags);
```

El retardo es un `sbintime_t`: tiempo de punto fijo binario de alta precisión. Usa las constantes `SBT_1S`, `SBT_1MS`, `SBT_1US` y `SBT_1NS` para construir los valores.

Ventajas: precisión subtick; argumento explícito de precisión/coalescencia; indicadores explícitos para tiempo absoluto frente a relativo.
Inconvenientes: requiere más aritmética y es necesario comprender `sbintime_t`.

Cuándo usarla: para callouts que necesitan precisión submilisegundo (protocolos de red, controladores de hardware con requisitos de temporización estrictos). La mayor parte del trabajo con drivers no necesita esto.

### Conversores de unidades

```c
sbintime_t  ticks_to_sbt = tick_sbt * timo_in_ticks;  /* tick_sbt is global */
sbintime_t  ms_to_sbt = ms_value * SBT_1MS;
sbintime_t  us_to_sbt = us_value * SBT_1US;
```

La variable global `tick_sbt` te da el equivalente en sbintime de un tick; multiplica por tu recuento de ticks para convertir.



## Referencia: auditoría de callouts antes de producción

Una auditoría breve que conviene realizar antes de pasar un driver que usa callouts de desarrollo a producción. Cada elemento es una pregunta que deberías poder responder con seguridad.

### Inventario de callouts

- [ ] ¿He enumerado todos los callouts que posee el driver en `LOCKING.md`?
- [ ] ¿He indicado la función callback de cada callout?
- [ ] ¿He indicado el lock que utiliza cada callout (si corresponde)?
- [ ] ¿He documentado el ciclo de vida de cada callout (inicialización en attach, drain en detach)?
- [ ] ¿He documentado el disparador de cada callout (qué provoca que se planifique)?
- [ ] ¿He documentado si cada callout se reencola (periódico) o se dispara una sola vez (one-shot)?

### Inicialización

- [ ] ¿Todo callout usa `callout_init_mtx` (o `_rw`/`_rm`) en lugar del `callout_init` sin argumentos?
- [ ] ¿Se llama a la inicialización después de que el lock al que hace referencia esté inicializado?
- [ ] ¿Es correcto el tipo de lock (sleep mutex para contextos que pueden dormir, etc.)?

### Planificación

- [ ] ¿Cada `callout_reset` se realiza con el lock apropiado retenido?
- [ ] ¿Es razonable el intervalo para el trabajo que realiza el callback?
- [ ] ¿Es correcta la conversión de milisegundos a ticks (`(ms * hz + 999) / 1000` para el redondeo hacia arriba)?
- [ ] Si el callout es periódico, ¿el callback se reencola únicamente bajo una condición documentada?

### Higiene del callback

- [ ] ¿Comienza cada callback con `MYFIRST_ASSERT(sc)` (o equivalente)?
- [ ] ¿Comprueba cada callback `is_attached` antes de hacer trabajo?
- [ ] ¿Sale cada callback anticipadamente si `is_attached == 0`?
- [ ] ¿Evita el callback operaciones de dormición (`uiomove`, `cv_wait`, `mtx_sleep`, `malloc(M_WAITOK)`, `selwakeup`)?
- [ ] ¿Está acotado el tiempo total de trabajo del callback?

### Cancelación

- [ ] ¿Usa el manejador de sysctl `callout_stop` para desactivar el temporizador?
- [ ] ¿Retiene el manejador de sysctl el lock al llamar a `callout_stop` y `callout_reset`?
- [ ] ¿Existe algún camino de código que pueda competir en condición de carrera con el manejador de sysctl?

### Detach

- [ ] ¿Libera el camino de detach el mutex antes de llamar a `callout_drain`?
- [ ] ¿Drena el camino de detach cada callout?
- [ ] ¿Se drenan los callouts en la fase correcta (después de que se borre `is_attached`)?

### Documentación

- [ ] ¿Está cada callout documentado en `LOCKING.md`?
- [ ] ¿Están documentadas las reglas de disciplina (lock-aware, no-sleep, drain-at-detach)?
- [ ] ¿Se menciona el subsistema callout en el README?
- [ ] ¿Hay sysctls expuestos que permitan a los usuarios ajustar el comportamiento?

### Pruebas

- [ ] ¿He ejecutado la suite de regresión con `WITNESS` activado?
- [ ] ¿He probado el detach con todos los callouts activos?
- [ ] ¿He ejecutado una prueba de estrés de larga duración?
- [ ] ¿He utilizado `dtrace` para verificar que las frecuencias de disparo coinciden con los intervalos configurados?

Un driver que supera esta auditoría es un driver en el que puedes confiar bajo carga.



## Referencia: estandarización de temporizadores en un driver

Para un driver con varios callouts, la coherencia importa más que la ingeniosidad. Una breve disciplina.

### Una convención de nombres única

Elige una convención y síguela. La convención de este capítulo:

- La estructura callout se llama `<purpose>_co` (por ejemplo, `heartbeat_co`, `watchdog_co`, `tick_source_co`).
- El callback se llama `myfirst_<purpose>` (por ejemplo, `myfirst_heartbeat`, `myfirst_watchdog`, `myfirst_tick_source`).
- El sysctl de intervalo se llama `<purpose>_interval_ms` (por ejemplo, `heartbeat_interval_ms`, `watchdog_interval_ms`, `tick_source_interval_ms`).
- El manejador de sysctl se llama `myfirst_sysctl_<purpose>_interval_ms`.

Un nuevo mantenedor puede añadir un callout siguiendo la convención sin tener que pensar en los nombres. Por el contrario, una revisión de código detecta las desviaciones de inmediato.

### Un patrón único de init/drain

Cada callout usa la misma inicialización y drain:

```c
/* In attach: */
callout_init_mtx(&sc-><purpose>_co, &sc->mtx, 0);

/* In detach (after dropping the mutex): */
callout_drain(&sc-><purpose>_co);
```

O bien, con las macros:

```c
MYFIRST_CO_INIT(sc, &sc-><purpose>_co);
MYFIRST_CO_DRAIN(&sc-><purpose>_co);
```

Las macros documentan el patrón en su definición; los puntos de llamada son breves y uniformes.

### Un patrón único para el manejador de sysctl

Cada manejador de sysctl de intervalo sigue la misma estructura:

```c
static int
myfirst_sysctl_<purpose>_interval_ms(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, old, error;

        old = sc-><purpose>_interval_ms;
        new = old;
        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);
        if (new < 0)
                return (EINVAL);

        MYFIRST_LOCK(sc);
        sc-><purpose>_interval_ms = new;
        if (new > 0 && old == 0) {
                /* enabling */
                callout_reset(&sc-><purpose>_co,
                    (new * hz + 999) / 1000,
                    myfirst_<purpose>, sc);
        } else if (new == 0 && old > 0) {
                /* disabling */
                callout_stop(&sc-><purpose>_co);
        }
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

La forma del manejador es la misma para cada sysctl de intervalo. Añadir un nuevo sysctl es algo mecánico.

### Un patrón único de callback

Cada callback periódico sigue la misma estructura:

```c
static void
myfirst_<purpose>(void *arg)
{
        struct myfirst_softc *sc = arg;
        int interval;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        /* ... do the per-firing work ... */

        interval = sc-><purpose>_interval_ms;
        if (interval > 0)
                callout_reset(&sc-><purpose>_co,
                    (interval * hz + 999) / 1000,
                    myfirst_<purpose>, sc);
}
```

Aserción, comprobación de `is_attached`, trabajo y reencole condicional. Cada callback del driver tiene esta forma; las desviaciones saltan a la vista.

### Un patrón único de documentación

Cada callout está documentado en `LOCKING.md` con los mismos campos:

- Lock utilizado.
- Función callback.
- Comportamiento (periódico o one-shot).
- Iniciado por (qué camino de código lo planifica).
- Detenido por (qué camino de código lo detiene).
- Ciclo de vida (inicialización en attach, drain en detach).

La documentación de un nuevo callout es algo mecánico. Una revisión de código puede verificar la documentación frente al código.

### Por qué estandarizar

La estandarización tiene costes: un nuevo colaborador debe aprender las convenciones y las desviaciones requieren una razón especial. Los beneficios son mayores:

- Menor carga cognitiva. Un lector que conoce el patrón entiende instantáneamente cada callout.
- Menos errores. El patrón estándar maneja correctamente los casos comunes (adquisición del lock, comprobación de `is_attached`, drain); una desviación tiene más probabilidades de ser incorrecta.
- Revisión más sencilla. Los revisores pueden buscar la forma del patrón en lugar de leer cada línea.
- Transferencia más sencilla. Un mantenedor que no ha visto el driver puede añadir un nuevo callout siguiendo la plantilla existente.

El coste de la estandarización se paga una vez en el momento del diseño. Los beneficios se acumulan para siempre. Siempre merece la pena.



## Referencia: lecturas adicionales sobre temporizadores

Para los lectores que quieran profundizar:

### Páginas de manual

- `callout(9)`: la referencia canónica de la API.
- `timeout(9)`: la interfaz heredada (obsoleta; se menciona para lectura histórica).
- `microtime(9)`, `getmicrouptime(9)`, `getsbinuptime(9)`: primitivas de lectura de tiempo que los callouts suelen utilizar.
- `eventtimers(4)`: el subsistema de temporizadores de eventos que impulsa los callouts.
- `kern.eventtimer`: el árbol de sysctl que expone el estado del temporizador de eventos.

### Archivos fuente

- `/usr/src/sys/kern/kern_timeout.c`: la implementación del callout.
- `/usr/src/sys/kern/kern_clocksource.c`: la capa del driver de temporizadores de eventos.
- `/usr/src/sys/sys/callout.h`, `/usr/src/sys/sys/_callout.h`: la API pública y la estructura.
- `/usr/src/sys/sys/time.h`: las constantes sbintime y las macros de conversión.
- `/usr/src/sys/dev/led/led.c`: un driver pequeño que ilustra el patrón callout.
- `/usr/src/sys/dev/uart/uart_core.c`: un uso más elaborado, que incluye un mecanismo de polling de respaldo para hardware que no genera interrupciones de entrada.

### Páginas de manual para leer en orden

Para un lector que se inicia en el subsistema de tiempo de FreeBSD, un orden de lectura razonable:

1. `callout(9)`: la referencia canónica de la API.
2. `time(9)`: unidades y primitivas.
3. `eventtimers(4)`: el subsistema de temporizadores de eventos que impulsa los callouts.
4. Los sysctls `kern.eventtimer` y `kern.hz`: controles en tiempo de ejecución.
5. `microuptime(9)`, `getmicrouptime(9)`: primitivas de lectura de tiempo.
6. `kproc(9)`, `kthread(9)`: para cuando realmente necesitas un thread del kernel.

Cada una se apoya en la anterior; leerlas en orden lleva un par de horas y te proporciona un modelo mental sólido de la infraestructura de tiempo del kernel.

### Material externo

El capítulo sobre temporizadores en *The Design and Implementation of the FreeBSD Operating System* (McKusick et al.) abarca la evolución histórica de los subsistemas de temporizadores y el razonamiento detrás del diseño actual. Es útil como contexto, aunque no es imprescindible.

La lista de correo de los desarrolladores de FreeBSD (`freebsd-hackers@`) debate ocasionalmente mejoras en callout y casos límite. Buscar «callout» en el archivo devuelve contexto histórico relevante sobre cómo ha evolucionado la API.

Para comprender mejor cómo planifica el kernel los eventos a más bajo nivel, merece la pena leer con detenimiento la página de manual `eventtimers(4)` y el código fuente en `/usr/src/sys/kern/kern_clocksource.c`. Están por debajo del nivel de este capítulo (no interactuamos directamente con los temporizadores de eventos), pero explican por qué el subsistema callout puede ofrecer la precisión que ofrece.

Por último, código fuente real de drivers. Elige cualquier driver en `/usr/src/sys/dev/` que use callouts (la mayoría lo hacen), lee su código relacionado con callouts y compáralo con los patrones de este capítulo. La correspondencia es directa; reconocerás las formas de inmediato. Ese tipo de lectura convierte las abstracciones del capítulo en conocimiento práctico.



## Referencia: análisis de costes del callout

Una breve discusión sobre el coste real de los callouts, útil a la hora de decidir intervalos o diseñar un temporizador de alta frecuencia.

### Coste en reposo

Un `struct callout` que no ha sido planificado no cuesta nada más allá del sizeof de la estructura (unos 80 bytes en amd64). El kernel no sabe de su existencia. Reside en tu softc sin hacer nada.

Un `struct callout` que ha sido planificado pero aún no se ha disparado cuesta algo más: el kernel lo ha enlazado en un bucket de la rueda. Las entradas de enlace cuestan unos pocos bytes. El kernel no sondea la estructura; solo la examina cuando el bucket correspondiente llega a su vencimiento.

La interrupción del reloj de hardware (que impulsa la rueda) se dispara `hz` veces por segundo (típicamente 1000). Su coste es prácticamente nulo en el caso vacío (sin callouts pendientes) y proporcional al número de callouts pendientes en el caso de carga.

### Coste por disparo

Cuando un callout se dispara, el kernel realiza aproximadamente lo siguiente:

1. Recorre el bucket de la rueda y localiza el callout. Tiempo constante por callout en el bucket.
2. Adquiere el lock del callout (si existe). El coste depende de la contención; típicamente nanosegundos.
3. Llama a la función callback. El coste depende del callback.
4. Libera el lock. Microsegundos.

Para un callback corto típico (unos pocos microsegundos de trabajo), el coste por disparo está dominado por el propio callback más la adquisición del lock. La sobrecarga del kernel es despreciable.

### Coste en cancelación/drain

`callout_stop` es rápido: eliminación de la lista enlazada más una actualización atómica de indicador. Microsegundos.

`callout_drain` es rápido si el callout está inactivo (igual que `callout_stop`). Si el callback está en ejecución en ese momento, el drain espera mediante el mecanismo de sleep-queue; el tiempo de espera depende de cuánto tarde el callback.

### Implicaciones prácticas

Cientos de callouts pendientes: sin problema. La rueda los gestiona de forma eficiente.

Miles de callouts pendientes: tampoco hay problema en operación normal. Recorrer un bucket de la rueda con decenas de callouts es rápido.

Un callout que dispara a 1 Hz: prácticamente gratuito. Una interrupción de hardware de cada mil recorre el bucket y encuentra el callout.

Un callout que dispara a 1 kHz: empieza a ser medible. Mil callbacks por segundo se acumulan. Si el callback tarda 10 microsegundos, eso es el 1 % de una CPU. Si el callback es más costoso, más.

Un callout a 10 kHz o más rápido: probablemente es un diseño incorrecto. Recurre a un busy-poll, a un temporizador de hardware o a un mecanismo especializado.

### Comparación con otros enfoques

Un thread del kernel que hace bucle sobre `cv_timedwait` y realiza trabajo en cada despertar tiene este coste:

- Memoria: ~16 KB de stack.
- Por despertar: entrada al planificador, cambio de contexto, callback, retorno al contexto anterior.

Para una carga de trabajo a 1 Hz, el coste del thread del kernel (un despertar por segundo) es similar al coste del callout. Para una carga a 1 kHz, ambos son comparables. Para una carga a 10 kHz, los dos empiezan a ser costosos; plantéate si realmente necesitas esa frecuencia.

Un bucle en espacio de usuario que sondea un sysctl:

- Memoria: todo un proceso de usuario (megabytes).
- Por sondeo: ida y vuelta de syscall, invocación del manejador de sysctl, retorno al espacio de usuario.

Siempre más costoso que un callout del kernel. Solo es apropiado cuando la lógica de sondeo pertenece genuinamente al espacio de usuario (una herramienta de monitorización, una sonda externa).

### Cuándo preocuparse por el coste

La mayoría de los drivers no tienen que preocuparse. Los callouts son baratos; el kernel está bien ajustado. Preocúpate por el coste solo cuando:

- El profiling muestra que los callouts dominan el uso de CPU. (Usa `dtrace` para confirmarlo.)
- Estás escribiendo un driver de alta frecuencia (de red o almacenamiento con requisitos estrictos de latencia).
- El sistema tiene miles de callouts activos y quieres comprender la carga.

En todos los demás casos, escribe el callout de forma natural y confía en que el kernel gestionará la carga.



## De cara al futuro: puente hacia el capítulo 14

El capítulo 14 se titula *Taskqueues and Deferred Work*. Su ámbito es el framework de trabajo diferido del kernel desde la perspectiva de un driver: cómo mover trabajo fuera de un contexto que no puede ejecutarlo de forma segura (un callback de callout, un manejador de interrupciones, una sección de epoch) hacia un contexto que sí puede.

El capítulo 13 preparó el terreno de tres maneras concretas.

En primer lugar, ya sabes que los callbacks de callout se ejecutan bajo un contrato de contexto estricto: sin suspensión, sin adquisición de locks que permitan suspensión, sin `uiomove`, sin `copyin`, sin `copyout` y sin `selwakeup` con un mutex del driver tomado. Viste ese contrato aplicado en la línea de `myfirst_tick_source` donde se omitió deliberadamente `selwakeup` porque el contexto del callout no podía realizar esa llamada de forma legítima. El capítulo 14 presenta `taskqueue(9)`, que es la primitiva que ofrece el kernel para exactamente este tipo de transferencia: el callout encola una tarea, y la tarea se ejecuta en contexto de thread, donde la llamada omitida es legítima.

En segundo lugar, ya conoces la disciplina de drain durante el detach. `callout_drain` garantiza que ningún callback está en ejecución cuando procede el detach. Las tareas tienen una primitiva equivalente: `taskqueue_drain` espera hasta que una tarea específica no está ni pendiente ni en ejecución. El modelo mental es el mismo; el orden crece en un paso (primero los callouts, luego las tareas y después todo lo que afectan).

En tercer lugar, ya conoces la forma de `LOCKING.md` como documento vivo. El capítulo 14 lo amplía con una sección de tareas que nombra cada tarea, su callback, su ciclo de vida y su lugar en el orden de detach. La disciplina es la misma; el vocabulario es algo más amplio.

Temas específicos que cubrirá el capítulo 14:

- La API de `taskqueue(9)`: `struct task`, `TASK_INIT`, `taskqueue_create`, `taskqueue_start_threads`, `taskqueue_enqueue`, `taskqueue_drain`, `taskqueue_free`.
- Las taskqueues del sistema predefinidas (`taskqueue_thread`, `taskqueue_swi`, `taskqueue_fast`, `taskqueue_bus`) y cuándo es preferible una taskqueue privada.
- La regla de coalescencia: qué ocurre cuando una tarea se encola mientras ya está pendiente.
- `struct timeout_task` y `taskqueue_enqueue_timeout` para trabajo diferido y programado.
- Patrones que se repiten en drivers reales de FreeBSD, y cómo depurar cuando algo sale mal.

No necesitas leer por adelantado. El capítulo 13 es preparación suficiente. Trae tu driver `myfirst` (Etapa 4 del capítulo 13), tu kit de pruebas y tu kernel con `WITNESS` activado. El capítulo 14 empieza donde terminó el capítulo 13.

Una pequeña reflexión final. Empezaste este capítulo con un driver que no podía actuar por sí solo: cada línea de trabajo era disparada por algo que hacía el usuario. Lo dejas con un driver que tiene tiempo interno, que registra periódicamente su estado, que detecta drenajes bloqueados, que inyecta eventos sintéticos para pruebas y que desmantela toda esa infraestructura de forma limpia cuando se descarga el módulo. Ese es un salto cualitativo real, y los patrones se transfieren directamente a cada tipo de driver que presentará la Parte 4.

Tómate un momento. El driver con el que empezaste la Parte 3 sabía gestionar un thread a la vez. El driver que tienes ahora coordina muchos threads, admite trabajo temporizado configurable y se desmantela sin condiciones de carrera. A partir de aquí, el capítulo 14 añade la *tarea*, que es la pieza que falta para cualquier driver cuyos callbacks de temporizador necesitan disparar trabajo que los callouts no pueden realizar de forma segura. Luego pasa la página.

### Una nota final sobre el tiempo

Un último pensamiento antes del capítulo 14. Has dedicado dos capítulos a la sincronización (capítulo 12) y un capítulo al tiempo (capítulo 13). Los dos están profundamente relacionados: la sincronización trata, en su esencia, de *cuándo* ocurren los eventos entre sí, y el tiempo es la medida explícita de eso. Los locks serializan los accesos; los cvs coordinan las esperas; los callouts disparan en los plazos establecidos. Los tres son formas distintas de abordar la misma pregunta subyacente: ¿cómo acuerdan el orden los flujos de ejecución independientes?

El capítulo 14 añade una cuarta pieza: el *contexto*. Un callout dispara en un momento preciso, pero el contexto en el que dispara (sin suspensión, sin locks que permitan suspensión, sin copia al espacio de usuario) es más restringido de lo que la mayoría del trabajo real necesita. El trabajo diferido mediante `taskqueue(9)` es el puente desde ese contexto restringido hacia un contexto de thread donde el conjunto completo de operaciones del kernel es legítimo.

Los patrones se transfieren. La inicialización consciente del lock que usan los callouts para sus callbacks tiene la misma forma que aplicarás cuando decidas qué lock adquiere un callback de tarea. El patrón de drain que usan los callouts en el detach tiene la misma forma que usan las tareas en el desmontaje. La disciplina de «haz poco aquí, difiere el resto» que exigen los callouts es la disciplina para la que el capítulo 14 te da una herramienta concreta.

Así que cuando llegues al capítulo 14, el framework ya te resultará familiar. Estarás añadiendo una primitiva más al conjunto de herramientas de tu driver. Sus reglas se combinan limpiamente con las reglas de callout que ya conoces. Las herramientas que has construido (`LOCKING.md`, el detach en siete fases, el patrón assert-and-check-attached) absorberán la nueva primitiva sin volverse frágiles.

Eso es lo que hace que la Parte 3 de este libro funcione como una unidad. Cada capítulo añade una dimensión más a la conciencia del driver sobre el mundo (concurrencia, sincronización, tiempo, trabajo diferido), y cada uno se construye sobre la infraestructura del capítulo anterior. Al final de la Parte 3, tu driver estará listo para la Parte 4 y el hardware real que hay más allá.
